/******************************************************************************
 * gntdev.c
 *
 * Device for accessing (in user-space) pages that have been granted by other
 * domains.
 *
 * Copyright (c) 2006-2007, D G Murray.
 *           (c) 2009 Gerd Hoffmann <kraxel@redhat.com>
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#undef DEBUG

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/mmu_notifier.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/highmem.h>

#include <xen/xen.h>
#include <xen/grant_table.h>
#include <xen/gntdev.h>
#include <xen/events.h>
#include <asm/xen/hypervisor.h>
#include <asm/xen/hypercall.h>
#include <asm/xen/page.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Derek G. Murray <Derek.Murray@cl.cam.ac.uk>, "
	      "Gerd Hoffmann <kraxel@redhat.com>");
MODULE_DESCRIPTION("User-space granted page access driver");

static int limit = 1024*1024;
module_param(limit, int, 0644);
MODULE_PARM_DESC(limit, "Maximum number of grants that may be mapped by "
		"the gntdev device");

static atomic_t pages_mapped = ATOMIC_INIT(0);

static int use_ptemod;

struct gntdev_priv {
	struct list_head maps;
	/* lock protects maps from concurrent changes */
	spinlock_t lock;
	struct mm_struct *mm;
	struct mmu_notifier mn;
};

struct unmap_notify {
	int flags;
	/* Address relative to the start of the grant_map */
	int addr;
	int event;
};

struct grant_map {
	struct list_head next;
	struct vm_area_struct *vma;
	int index;
	int count;
	int flags;
	int is_mapped;
	atomic_t users;
	struct unmap_notify notify;
	struct ioctl_gntdev_grant_ref *grants;
	struct gnttab_map_grant_ref   *map_ops;
	struct gnttab_unmap_grant_ref *unmap_ops;
	struct page **pages;
};

static int unmap_grant_pages(struct grant_map *map, int offset, int pages);

/* ------------------------------------------------------------------ */

static void gntdev_print_maps(struct gntdev_priv *priv,
			      char *text, int text_index)
{
#ifdef DEBUG
	struct grant_map *map;

	pr_debug("%s: maps list (priv %p)\n", __func__, priv);
	list_for_each_entry(map, &priv->maps, next)
		pr_debug("  index %2d, count %2d %s\n",
		       map->index, map->count,
		       map->index == text_index && text ? text : "");
#endif
}

static struct grant_map *gntdev_alloc_map(struct gntdev_priv *priv, int count)
{
	struct grant_map *add;
	int i;

	add = kzalloc(sizeof(struct grant_map), GFP_KERNEL);
	if (NULL == add)
		return NULL;

	add->grants    = kzalloc(sizeof(add->grants[0])    * count, GFP_KERNEL);
	add->map_ops   = kzalloc(sizeof(add->map_ops[0])   * count, GFP_KERNEL);
	add->unmap_ops = kzalloc(sizeof(add->unmap_ops[0]) * count, GFP_KERNEL);
	add->pages     = kzalloc(sizeof(add->pages[0])     * count, GFP_KERNEL);
	if (NULL == add->grants    ||
	    NULL == add->map_ops   ||
	    NULL == add->unmap_ops ||
	    NULL == add->pages)
		goto err;

	for (i = 0; i < count; i++) {
		add->pages[i] = alloc_page(GFP_KERNEL | __GFP_HIGHMEM);
		if (add->pages[i] == NULL)
			goto err;
	}

	add->index = 0;
	add->count = count;
	atomic_set(&add->users, 1);

	return add;

err:
	if (add->pages)
		for (i = 0; i < count; i++) {
			if (add->pages[i])
				__free_page(add->pages[i]);
		}
	kfree(add->pages);
	kfree(add->grants);
	kfree(add->map_ops);
	kfree(add->unmap_ops);
	kfree(add);
	return NULL;
}

static void gntdev_add_map(struct gntdev_priv *priv, struct grant_map *add)
{
	struct grant_map *map;

	list_for_each_entry(map, &priv->maps, next) {
		if (add->index + add->count < map->index) {
			list_add_tail(&add->next, &map->next);
			goto done;
		}
		add->index = map->index + map->count;
	}
	list_add_tail(&add->next, &priv->maps);

done:
	gntdev_print_maps(priv, "[new]", add->index);
}

static struct grant_map *gntdev_find_map_index(struct gntdev_priv *priv,
		int index, int count)
{
	struct grant_map *map;

	list_for_each_entry(map, &priv->maps, next) {
		if (map->index != index)
			continue;
		if (count && map->count != count)
			continue;
		return map;
	}
	return NULL;
}

static void gntdev_put_map(struct grant_map *map)
{
	int i;

	if (!map)
		return;

	if (!atomic_dec_and_test(&map->users))
		return;

	atomic_sub(map->count, &pages_mapped);

	if (map->notify.flags & UNMAP_NOTIFY_SEND_EVENT) {
		notify_remote_via_evtchn(map->notify.event);
	}

	if (map->pages) {
		if (!use_ptemod)
			unmap_grant_pages(map, 0, map->count);

		for (i = 0; i < map->count; i++) {
			uint32_t check, *tmp;
			if (!map->pages[i])
				continue;
			/* XXX When unmapping in an HVM domain, Xen will
			 * sometimes end up mapping the GFN to an invalid MFN.
			 * In this case, writes will be discarded and reads will
			 * return all 0xFF bytes.  Leak these unusable GFNs
			 * until Xen supports fixing their p2m mapping.
			 *
			 * Confirmed present in Xen 4.1-RC3 with HVM source
			 */
			tmp = kmap(map->pages[i]);
			*tmp = 0xdeaddead;
			mb();
			check = *tmp;
			kunmap(map->pages[i]);
			if (check == 0xdeaddead)
				__free_page(map->pages[i]);
			else
				pr_debug("Discard page %d=%ld\n", i,
					page_to_pfn(map->pages[i]));
		}
	}
	kfree(map->pages);
	kfree(map->grants);
	kfree(map->map_ops);
	kfree(map->unmap_ops);
	kfree(map);
}

/* ------------------------------------------------------------------ */

static int find_grant_ptes(pte_t *pte, pgtable_t token,
		unsigned long addr, void *data)
{
	struct grant_map *map = data;
	unsigned int pgnr = (addr - map->vma->vm_start) >> PAGE_SHIFT;
	int flags = map->flags | GNTMAP_application_map | GNTMAP_contains_pte;
	u64 pte_maddr;

	BUG_ON(pgnr >= map->count);
	pte_maddr = arbitrary_virt_to_machine(pte).maddr;

	gnttab_set_map_op(&map->map_ops[pgnr], pte_maddr, flags,
			  map->grants[pgnr].ref,
			  map->grants[pgnr].domid);
	gnttab_set_unmap_op(&map->unmap_ops[pgnr], pte_maddr, flags,
			    0 /* handle */);
	return 0;
}

static int map_grant_pages(struct grant_map *map)
{
	int i, err = 0;
	phys_addr_t addr;

	if (!use_ptemod) {
		for (i = 0; i < map->count; i++) {
			addr = (phys_addr_t)
				pfn_to_kaddr(page_to_pfn(map->pages[i]));
			gnttab_set_map_op(&map->map_ops[i], addr, map->flags,
				map->grants[i].ref,
				map->grants[i].domid);
			gnttab_set_unmap_op(&map->unmap_ops[i], addr,
				map->flags, 0 /* handle */);
		}
	}

	pr_debug("map %d+%d\n", map->index, map->count);
	err = gnttab_map_refs(map->map_ops, map->pages, map->count);
	if (err)
		return err;

	for (i = 0; i < map->count; i++) {
		if (map->map_ops[i].status)
			err = -EINVAL;
		map->unmap_ops[i].handle = map->map_ops[i].handle;
	}
	return err;
}

static int unmap_grant_pages(struct grant_map *map, int offset, int pages)
{
	int i, err = 0;

	if (map->notify.flags & UNMAP_NOTIFY_CLEAR_BYTE) {
		int pgno = (map->notify.addr >> PAGE_SHIFT);
		if (pgno >= offset && pgno < offset + pages) {
			uint8_t *tmp = kmap(map->pages[pgno]);
			tmp[map->notify.addr & (PAGE_SIZE-1)] = 0;
			kunmap(map->pages[pgno]);
			map->notify.flags &= ~UNMAP_NOTIFY_CLEAR_BYTE;
		}
	}

	pr_debug("map %d+%d [%d+%d]\n", map->index, map->count, offset, pages);
	err = gnttab_unmap_refs(map->unmap_ops + offset, map->pages, pages);
	if (err)
		return err;

	for (i = 0; i < pages; i++) {
		if (map->unmap_ops[offset+i].status)
			err = -EINVAL;
		map->unmap_ops[offset+i].handle = 0;
	}
	return err;
}

/* ------------------------------------------------------------------ */

static void gntdev_vma_close(struct vm_area_struct *vma)
{
	struct grant_map *map = vma->vm_private_data;

	pr_debug("close %p\n", vma);
	map->is_mapped = 0;
	map->vma = NULL;
	vma->vm_private_data = NULL;
	gntdev_put_map(map);
}

static struct vm_operations_struct gntdev_vmops = {
	.close = gntdev_vma_close,
};

/* ------------------------------------------------------------------ */

static void mn_invl_range_start(struct mmu_notifier *mn,
				struct mm_struct *mm,
				unsigned long start, unsigned long end)
{
	struct gntdev_priv *priv = container_of(mn, struct gntdev_priv, mn);
	struct grant_map *map;
	unsigned long mstart, mend;
	int err;

	spin_lock(&priv->lock);
	list_for_each_entry(map, &priv->maps, next) {
		if (!map->vma)
			continue;
		if (!map->is_mapped)
			continue;
		if (map->vma->vm_start >= end)
			continue;
		if (map->vma->vm_end <= start)
			continue;
		mstart = max(start, map->vma->vm_start);
		mend   = min(end,   map->vma->vm_end);
		pr_debug("map %d+%d (%lx %lx), range %lx %lx, mrange %lx %lx\n",
				map->index, map->count,
				map->vma->vm_start, map->vma->vm_end,
				start, end, mstart, mend);
		err = unmap_grant_pages(map,
					(mstart - map->vma->vm_start) >> PAGE_SHIFT,
					(mend - mstart) >> PAGE_SHIFT);
		WARN_ON(err);
	}
	spin_unlock(&priv->lock);
}

static void mn_invl_page(struct mmu_notifier *mn,
			 struct mm_struct *mm,
			 unsigned long address)
{
	mn_invl_range_start(mn, mm, address, address + PAGE_SIZE);
}

static void mn_release(struct mmu_notifier *mn,
		       struct mm_struct *mm)
{
	struct gntdev_priv *priv = container_of(mn, struct gntdev_priv, mn);
	struct grant_map *map;
	int err;

	spin_lock(&priv->lock);
	list_for_each_entry(map, &priv->maps, next) {
		if (!map->vma)
			continue;
		pr_debug("map %d+%d (%lx %lx)\n",
				map->index, map->count,
				map->vma->vm_start, map->vma->vm_end);
		err = unmap_grant_pages(map, /* offset */ 0, map->count);
		WARN_ON(err);
	}
	spin_unlock(&priv->lock);
}

struct mmu_notifier_ops gntdev_mmu_ops = {
	.release                = mn_release,
	.invalidate_page        = mn_invl_page,
	.invalidate_range_start = mn_invl_range_start,
};

/* ------------------------------------------------------------------ */

static int gntdev_open(struct inode *inode, struct file *flip)
{
	struct gntdev_priv *priv;
	int ret = 0;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	INIT_LIST_HEAD(&priv->maps);
	spin_lock_init(&priv->lock);

	if (use_ptemod) {
		priv->mm = get_task_mm(current);
		if (!priv->mm) {
			kfree(priv);
			return -ENOMEM;
		}
		priv->mn.ops = &gntdev_mmu_ops;
		ret = mmu_notifier_register(&priv->mn, priv->mm);
		mmput(priv->mm);
	}

	if (ret) {
		kfree(priv);
		return ret;
	}

	flip->private_data = priv;
	pr_debug("priv %p\n", priv);

	return 0;
}

static int gntdev_release(struct inode *inode, struct file *flip)
{
	struct gntdev_priv *priv = flip->private_data;
	struct grant_map *map;

	pr_debug("priv %p\n", priv);

	spin_lock(&priv->lock);
	while (!list_empty(&priv->maps)) {
		map = list_entry(priv->maps.next, struct grant_map, next);
		list_del(&map->next);
		gntdev_put_map(map);
	}
	spin_unlock(&priv->lock);

	if (use_ptemod)
		mmu_notifier_unregister(&priv->mn, priv->mm);
	kfree(priv);
	return 0;
}

static long gntdev_ioctl_map_grant_ref(struct gntdev_priv *priv,
				       struct ioctl_gntdev_map_grant_ref __user *u)
{
	struct ioctl_gntdev_map_grant_ref op;
	struct grant_map *map;
	int err;

	if (copy_from_user(&op, u, sizeof(op)) != 0)
		return -EFAULT;
	pr_debug("priv %p, add %d\n", priv, op.count);
	if (unlikely(op.count <= 0))
		return -EINVAL;

	err = -ENOMEM;
	map = gntdev_alloc_map(priv, op.count);
	if (!map)
		return err;

	if (unlikely(atomic_add_return(op.count, &pages_mapped) > limit)) {
		pr_debug("can't map: over limit\n");
		gntdev_put_map(map);
		return err;
	}

	if (copy_from_user(map->grants, &u->refs,
			   sizeof(map->grants[0]) * op.count) != 0) {
		gntdev_put_map(map);
		return err;
	}

	spin_lock(&priv->lock);
	gntdev_add_map(priv, map);
	op.index = map->index << PAGE_SHIFT;
	spin_unlock(&priv->lock);

	if (copy_to_user(u, &op, sizeof(op)) != 0)
		return -EFAULT;

	return 0;
}

static long gntdev_ioctl_unmap_grant_ref(struct gntdev_priv *priv,
					 struct ioctl_gntdev_unmap_grant_ref __user *u)
{
	struct ioctl_gntdev_unmap_grant_ref op;
	struct grant_map *map;
	int err = -ENOENT;

	if (copy_from_user(&op, u, sizeof(op)) != 0)
		return -EFAULT;
	pr_debug("priv %p, del %d+%d\n", priv, (int)op.index, (int)op.count);

	spin_lock(&priv->lock);
	map = gntdev_find_map_index(priv, op.index >> PAGE_SHIFT, op.count);
	if (map) {
		list_del(&map->next);
		gntdev_put_map(map);
		err = 0;
	}
	spin_unlock(&priv->lock);
	return err;
}

static long gntdev_ioctl_get_offset_for_vaddr(struct gntdev_priv *priv,
					      struct ioctl_gntdev_get_offset_for_vaddr __user *u)
{
	struct ioctl_gntdev_get_offset_for_vaddr op;
	struct vm_area_struct *vma;
	struct grant_map *map;

	if (copy_from_user(&op, u, sizeof(op)) != 0)
		return -EFAULT;
	pr_debug("priv %p, offset for vaddr %lx\n", priv, (unsigned long)op.vaddr);

	vma = find_vma(current->mm, op.vaddr);
	if (!vma || vma->vm_ops != &gntdev_vmops)
		return -EINVAL;

	map = vma->vm_private_data;
	if (!map)
		return -EINVAL;

	op.offset = map->index << PAGE_SHIFT;
	op.count = map->count;

	if (copy_to_user(u, &op, sizeof(op)) != 0)
		return -EFAULT;
	return 0;
}

static long gntdev_ioctl_notify(struct gntdev_priv *priv, void __user *u)
{
	struct ioctl_gntdev_unmap_notify op;
	struct grant_map *map;
	int rc;

	if (copy_from_user(&op, u, sizeof(op)))
		return -EFAULT;

	if (op.action & ~(UNMAP_NOTIFY_CLEAR_BYTE|UNMAP_NOTIFY_SEND_EVENT))
		return -EINVAL;

	spin_lock(&priv->lock);

	list_for_each_entry(map, &priv->maps, next) {
		uint64_t begin = map->index << PAGE_SHIFT;
		uint64_t end = (map->index + map->count) << PAGE_SHIFT;
		if (op.index >= begin && op.index < end)
			goto found;
	}
	rc = -ENOENT;
	goto unlock_out;

 found:
	map->notify.flags = op.action;
	map->notify.addr = op.index - (map->index << PAGE_SHIFT);
	map->notify.event = op.event_channel_port;
	rc = 0;
 unlock_out:
	spin_unlock(&priv->lock);
	return rc;
}

static long gntdev_ioctl(struct file *flip,
			 unsigned int cmd, unsigned long arg)
{
	struct gntdev_priv *priv = flip->private_data;
	void __user *ptr = (void __user *)arg;

	switch (cmd) {
	case IOCTL_GNTDEV_MAP_GRANT_REF:
		return gntdev_ioctl_map_grant_ref(priv, ptr);

	case IOCTL_GNTDEV_UNMAP_GRANT_REF:
		return gntdev_ioctl_unmap_grant_ref(priv, ptr);

	case IOCTL_GNTDEV_GET_OFFSET_FOR_VADDR:
		return gntdev_ioctl_get_offset_for_vaddr(priv, ptr);

	case IOCTL_GNTDEV_SET_UNMAP_NOTIFY:
		return gntdev_ioctl_notify(priv, ptr);

	default:
		pr_debug("priv %p, unknown cmd %x\n", priv, cmd);
		return -ENOIOCTLCMD;
	}

	return 0;
}

static int gntdev_mmap(struct file *flip, struct vm_area_struct *vma)
{
	struct gntdev_priv *priv = flip->private_data;
	int index = vma->vm_pgoff;
	int count = (vma->vm_end - vma->vm_start) >> PAGE_SHIFT;
	struct grant_map *map;
	int i, err = -EINVAL;

	if ((vma->vm_flags & VM_WRITE) && !(vma->vm_flags & VM_SHARED))
		return -EINVAL;

	pr_debug("map %d+%d at %lx (pgoff %lx)\n",
			index, count, vma->vm_start, vma->vm_pgoff);

	spin_lock(&priv->lock);
	map = gntdev_find_map_index(priv, index, count);
	if (!map)
		goto unlock_out;
	if (use_ptemod && map->vma)
		goto unlock_out;
	if (use_ptemod && priv->mm != vma->vm_mm) {
		printk(KERN_WARNING "Huh? Other mm?\n");
		goto unlock_out;
	}

	atomic_inc(&map->users);

	vma->vm_ops = &gntdev_vmops;

	vma->vm_flags |= VM_RESERVED|VM_DONTCOPY|VM_DONTEXPAND|VM_PFNMAP;

	vma->vm_private_data = map;

	if (use_ptemod)
		map->vma = vma;

	map->flags = GNTMAP_host_map;
	if (!(vma->vm_flags & VM_WRITE))
		map->flags |= GNTMAP_readonly;

	spin_unlock(&priv->lock);

	if (use_ptemod) {
		err = apply_to_page_range(vma->vm_mm, vma->vm_start,
					  vma->vm_end - vma->vm_start,
					  find_grant_ptes, map);
		if (err) {
			printk(KERN_WARNING "find_grant_ptes() failure.\n");
			return err;
		}
	}

	err = map_grant_pages(map);
	if (err) {
		printk(KERN_WARNING "map_grant_pages() failure.\n");
		return err;
	}

	map->is_mapped = 1;

	if (!use_ptemod) {
		for (i = 0; i < count; i++) {
			err = vm_insert_page(vma, vma->vm_start + i*PAGE_SIZE,
				map->pages[i]);
			if (err)
				return err;
		}
	}

	return 0;

unlock_out:
	spin_unlock(&priv->lock);
	return err;
}

static const struct file_operations gntdev_fops = {
	.owner = THIS_MODULE,
	.open = gntdev_open,
	.release = gntdev_release,
	.mmap = gntdev_mmap,
	.unlocked_ioctl = gntdev_ioctl
};

static struct miscdevice gntdev_miscdev = {
	.minor        = MISC_DYNAMIC_MINOR,
	.name         = "xen/gntdev",
	.fops         = &gntdev_fops,
};

/* ------------------------------------------------------------------ */

static int __init gntdev_init(void)
{
	int err;

	if (!xen_domain())
		return -ENODEV;

	use_ptemod = xen_pv_domain();

	err = misc_register(&gntdev_miscdev);
	if (err != 0) {
		printk(KERN_ERR "Could not register gntdev device\n");
		return err;
	}
	return 0;
}

static void __exit gntdev_exit(void)
{
	misc_deregister(&gntdev_miscdev);
}

module_init(gntdev_init);
module_exit(gntdev_exit);

/* ------------------------------------------------------------------ */
