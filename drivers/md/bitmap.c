/*
 * bitmap.c two-level bitmap (C) Peter T. Breuer (ptb@ot.uc3m.es) 2003
 *
 * bitmap_create  - sets up the bitmap structure
 * bitmap_destroy - destroys the bitmap structure
 *
 * additions, Copyright (C) 2003-2004, Paul Clements, SteelEye Technology, Inc.:
 * - added disk storage for bitmap
 * - changes to allow various bitmap chunk sizes
 * - added bitmap daemon (to asynchronously clear bitmap bits from disk)
 */

/*
 * Still to do:
 *
 * flush after percent set rather than just time based. (maybe both).
 * wait if count gets too high, wake when it drops to half.
 * allow bitmap to be mirrored with superblock (before or after...)
 * allow hot-add to re-instate a current device.
 * allow hot-add of bitmap after quiessing device
 */

#include <linux/module.h>
#include <linux/version.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/config.h>
#include <linux/timer.h>
#include <linux/sched.h>
#include <linux/list.h>
#include <linux/file.h>
#include <linux/mount.h>
#include <linux/buffer_head.h>
#include <linux/raid/md.h>
#include <linux/raid/bitmap.h>

/* debug macros */

#define DEBUG 0

#if DEBUG
/* these are for debugging purposes only! */

/* define one and only one of these */
#define INJECT_FAULTS_1 0 /* cause bitmap_alloc_page to fail always */
#define INJECT_FAULTS_2 0 /* cause bitmap file to be kicked when first bit set*/
#define INJECT_FAULTS_3 0 /* treat bitmap file as kicked at init time */
#define INJECT_FAULTS_4 0 /* undef */
#define INJECT_FAULTS_5 0 /* undef */
#define INJECT_FAULTS_6 0

/* if these are defined, the driver will fail! debug only */
#define INJECT_FATAL_FAULT_1 0 /* fail kmalloc, causing bitmap_create to fail */
#define INJECT_FATAL_FAULT_2 0 /* undef */
#define INJECT_FATAL_FAULT_3 0 /* undef */
#endif

//#define DPRINTK PRINTK /* set this NULL to avoid verbose debug output */
#define DPRINTK(x...) do { } while(0)

#ifndef PRINTK
#  if DEBUG > 0
#    define PRINTK(x...) printk(KERN_DEBUG x)
#  else
#    define PRINTK(x...)
#  endif
#endif

static inline char * bmname(struct bitmap *bitmap)
{
	return bitmap->mddev ? mdname(bitmap->mddev) : "mdX";
}


/*
 * test if the bitmap is active
 */
int bitmap_active(struct bitmap *bitmap)
{
	unsigned long flags;
	int res = 0;

	if (!bitmap)
		return res;
	spin_lock_irqsave(&bitmap->lock, flags);
	res = bitmap->flags & BITMAP_ACTIVE;
	spin_unlock_irqrestore(&bitmap->lock, flags);
	return res;
}

#define WRITE_POOL_SIZE 256
/* mempool for queueing pending writes on the bitmap file */
static void *write_pool_alloc(unsigned int gfp_flags, void *data)
{
	return kmalloc(sizeof(struct page_list), gfp_flags);
}

static void write_pool_free(void *ptr, void *data)
{
	kfree(ptr);
}

/*
 * just a placeholder - calls kmalloc for bitmap pages
 */
static unsigned char *bitmap_alloc_page(struct bitmap *bitmap)
{
	unsigned char *page;

#if INJECT_FAULTS_1
	page = NULL;
#else
	page = kmalloc(PAGE_SIZE, GFP_NOIO);
#endif
	if (!page)
		printk("%s: bitmap_alloc_page FAILED\n", bmname(bitmap));
	else
		printk("%s: bitmap_alloc_page: allocated page at %p\n",
			bmname(bitmap), page);
	return page;
}

/*
 * for now just a placeholder -- just calls kfree for bitmap pages
 */
static void bitmap_free_page(struct bitmap *bitmap, unsigned char *page)
{
	PRINTK("%s: bitmap_free_page: free page %p\n", bmname(bitmap), page);
	kfree(page);
}

/*
 * check a page and, if necessary, allocate it (or hijack it if the alloc fails)
 *
 * 1) check to see if this page is allocated, if it's not then try to alloc
 * 2) if the alloc fails, set the page's hijacked flag so we'll use the
 *    page pointer directly as a counter
 *
 * if we find our page, we increment the page's refcount so that it stays
 * allocated while we're using it
 */
static int bitmap_checkpage(struct bitmap *bitmap, unsigned long page, int create)
{
	unsigned char *mappage;

	if (page >= bitmap->pages) {
		printk(KERN_ALERT
			"%s: invalid bitmap page request: %lu (> %lu)\n",
			bmname(bitmap), page, bitmap->pages-1);
		return -EINVAL;
	}


	if (bitmap->bp[page].hijacked) /* it's hijacked, don't try to alloc */
		return 0;

	if (bitmap->bp[page].map) /* page is already allocated, just return */
		return 0;

	if (!create)
		return -ENOENT;

	spin_unlock_irq(&bitmap->lock);

	/* this page has not been allocated yet */

	if ((mappage = bitmap_alloc_page(bitmap)) == NULL) {
		PRINTK("%s: bitmap map page allocation failed, hijacking\n",
			bmname(bitmap));
		/* failed - set the hijacked flag so that we can use the
		 * pointer as a counter */
		spin_lock_irq(&bitmap->lock);
		if (!bitmap->bp[page].map)
			bitmap->bp[page].hijacked = 1;
		goto out;
	}

	/* got a page */

	spin_lock_irq(&bitmap->lock);

	/* recheck the page */

	if (bitmap->bp[page].map || bitmap->bp[page].hijacked) {
		/* somebody beat us to getting the page */
		bitmap_free_page(bitmap, mappage);
		return 0;
	}

	/* no page was in place and we have one, so install it */

	memset(mappage, 0, PAGE_SIZE);
	bitmap->bp[page].map = mappage;
	bitmap->missing_pages--;
out:
	return 0;
}


/* if page is completely empty, put it back on the free list, or dealloc it */
/* if page was hijacked, unmark the flag so it might get alloced next time */
/* Note: lock should be held when calling this */
static inline void bitmap_checkfree(struct bitmap *bitmap, unsigned long page)
{
	char *ptr;

	if (bitmap->bp[page].count) /* page is still busy */
		return;

	/* page is no longer in use, it can be released */

	if (bitmap->bp[page].hijacked) { /* page was hijacked, undo this now */
		bitmap->bp[page].hijacked = 0;
		bitmap->bp[page].map = NULL;
		return;
	}

	/* normal case, free the page */

#if 0
/* actually ... let's not.  We will probably need the page again exactly when
 * memory is tight and we are flusing to disk
 */
	return;
#else
	ptr = bitmap->bp[page].map;
	bitmap->bp[page].map = NULL;
	bitmap->missing_pages++;
	bitmap_free_page(bitmap, ptr);
	return;
#endif
}


/*
 * bitmap file handling - read and write the bitmap file and its superblock
 */

/* copy the pathname of a file to a buffer */
char *file_path(struct file *file, char *buf, int count)
{
	struct dentry *d;
	struct vfsmount *v;

	if (!buf)
		return NULL;

	d = file->f_dentry;
	v = file->f_vfsmnt;

	buf = d_path(d, v, buf, count);

	return IS_ERR(buf) ? NULL : buf;
}

/*
 * basic page I/O operations
 */

/*
 * write out a page
 */
static int write_page(struct page *page, int wait)
{
	int ret = -ENOMEM;

	lock_page(page);

	if (page->mapping == NULL)
		goto unlock_out;
	else if (i_size_read(page->mapping->host) < page->index << PAGE_SHIFT) {
		ret = -ENOENT;
		goto unlock_out;
	}

	ret = page->mapping->a_ops->prepare_write(NULL, page, 0, PAGE_SIZE);
	if (!ret)
		ret = page->mapping->a_ops->commit_write(NULL, page, 0,
			PAGE_SIZE);
	if (ret) {
unlock_out:
		unlock_page(page);
		return ret;
	}

	set_page_dirty(page); /* force it to be written out */
	return write_one_page(page, wait);
}

/* read a page from a file, pinning it into cache, and return bytes_read */
static struct page *read_page(struct file *file, unsigned long index,
					unsigned long *bytes_read)
{
	struct inode *inode = file->f_mapping->host;
	struct page *page = NULL;
	loff_t isize = i_size_read(inode);
	unsigned long end_index = isize >> PAGE_CACHE_SHIFT;

	PRINTK("read bitmap file (%dB @ %Lu)\n", (int)PAGE_CACHE_SIZE,
			(unsigned long long)index << PAGE_CACHE_SHIFT);

	page = read_cache_page(inode->i_mapping, index,
			(filler_t *)inode->i_mapping->a_ops->readpage, file);
	if (IS_ERR(page))
		goto out;
	wait_on_page_locked(page);
	if (!PageUptodate(page) || PageError(page)) {
		page_cache_release(page);
		page = ERR_PTR(-EIO);
		goto out;
	}

	if (index > end_index) /* we have read beyond EOF */
		*bytes_read = 0;
	else if (index == end_index) /* possible short read */
		*bytes_read = isize & ~PAGE_CACHE_MASK;
	else
		*bytes_read = PAGE_CACHE_SIZE; /* got a full page */
out:
	if (IS_ERR(page))
		printk(KERN_ALERT "md: bitmap read error: (%dB @ %Lu): %ld\n",
			(int)PAGE_CACHE_SIZE,
			(unsigned long long)index << PAGE_CACHE_SHIFT,
			PTR_ERR(page));
	return page;
}

/*
 * bitmap file superblock operations
 */

/* update the event counter and sync the superblock to disk */
int bitmap_update_sb(struct bitmap *bitmap)
{
	bitmap_super_t *sb;
	unsigned long flags;

	if (!bitmap || !bitmap->mddev) /* no bitmap for this array */
		return 0;
	spin_lock_irqsave(&bitmap->lock, flags);
	if (!bitmap->sb_page) { /* no superblock */
		spin_unlock_irqrestore(&bitmap->lock, flags);
		return 0;
	}
	page_cache_get(bitmap->sb_page);
	spin_unlock_irqrestore(&bitmap->lock, flags);
	sb = (bitmap_super_t *)kmap(bitmap->sb_page);
	sb->events = cpu_to_le64(bitmap->mddev->events);
	if (!bitmap->mddev->degraded)
		sb->events_cleared = cpu_to_le64(bitmap->mddev->events);
	kunmap(bitmap->sb_page);
	write_page(bitmap->sb_page, 0);
	return 0;
}

/* print out the bitmap file superblock */
void bitmap_print_sb(struct bitmap *bitmap)
{
	bitmap_super_t *sb;

	if (!bitmap || !bitmap->sb_page)
		return;
	sb = (bitmap_super_t *)kmap(bitmap->sb_page);
	printk(KERN_DEBUG "%s: bitmap file superblock:\n", bmname(bitmap));
	printk(KERN_DEBUG "       magic: %08x\n", le32_to_cpu(sb->magic));
	printk(KERN_DEBUG "     version: %d\n", le32_to_cpu(sb->version));
	printk(KERN_DEBUG "        uuid: %08x.%08x.%08x.%08x\n",
					*(__u32 *)(sb->uuid+0),
					*(__u32 *)(sb->uuid+4),
					*(__u32 *)(sb->uuid+8),
					*(__u32 *)(sb->uuid+12));
	printk(KERN_DEBUG "      events: %llu\n",
			(unsigned long long) le64_to_cpu(sb->events));
	printk(KERN_DEBUG "events_clred: %llu\n",
			(unsigned long long) le64_to_cpu(sb->events_cleared));
	printk(KERN_DEBUG "       state: %08x\n", le32_to_cpu(sb->state));
	printk(KERN_DEBUG "   chunksize: %d B\n", le32_to_cpu(sb->chunksize));
	printk(KERN_DEBUG "daemon sleep: %ds\n", le32_to_cpu(sb->daemon_sleep));
	printk(KERN_DEBUG "   sync size: %llu KB\n", le64_to_cpu(sb->sync_size));
	kunmap(bitmap->sb_page);
}

/* read the superblock from the bitmap file and initialize some bitmap fields */
static int bitmap_read_sb(struct bitmap *bitmap)
{
	char *reason = NULL;
	bitmap_super_t *sb;
	unsigned long chunksize, daemon_sleep;
	unsigned long bytes_read;
	unsigned long long events;
	int err = -EINVAL;

	/* page 0 is the superblock, read it... */
	bitmap->sb_page = read_page(bitmap->file, 0, &bytes_read);
	if (IS_ERR(bitmap->sb_page)) {
		err = PTR_ERR(bitmap->sb_page);
		bitmap->sb_page = NULL;
		return err;
	}

	sb = (bitmap_super_t *)kmap(bitmap->sb_page);

	if (bytes_read < sizeof(*sb)) { /* short read */
		printk(KERN_INFO "%s: bitmap file superblock truncated\n",
			bmname(bitmap));
		err = -ENOSPC;
		goto out;
	}

	chunksize = le32_to_cpu(sb->chunksize);
	daemon_sleep = le32_to_cpu(sb->daemon_sleep);

	/* verify that the bitmap-specific fields are valid */
	if (sb->magic != cpu_to_le32(BITMAP_MAGIC))
		reason = "bad magic";
	else if (sb->version != cpu_to_le32(BITMAP_MAJOR))
		reason = "unrecognized superblock version";
	else if (chunksize < 512 || chunksize > (1024 * 1024 * 4))
		reason = "bitmap chunksize out of range (512B - 4MB)";
	else if ((1 << ffz(~chunksize)) != chunksize)
		reason = "bitmap chunksize not a power of 2";
	else if (daemon_sleep < 1 || daemon_sleep > 15)
		reason = "daemon sleep period out of range";
	if (reason) {
		printk(KERN_INFO "%s: invalid bitmap file superblock: %s\n",
			bmname(bitmap), reason);
		goto out;
	}

	/* keep the array size field of the bitmap superblock up to date */
	sb->sync_size = cpu_to_le64(bitmap->mddev->resync_max_sectors);

	if (!bitmap->mddev->persistent)
		goto success;

	/*
	 * if we have a persistent array superblock, compare the
	 * bitmap's UUID and event counter to the mddev's
	 */
	if (memcmp(sb->uuid, bitmap->mddev->uuid, 16)) {
		printk(KERN_INFO "%s: bitmap superblock UUID mismatch\n",
			bmname(bitmap));
		goto out;
	}
	events = le64_to_cpu(sb->events);
	if (events < bitmap->mddev->events) {
		printk(KERN_INFO "%s: bitmap file is out of date (%llu < %llu) "
			"-- forcing full recovery\n", bmname(bitmap), events,
			(unsigned long long) bitmap->mddev->events);
		sb->state |= BITMAP_STALE;
	}
success:
	/* assign fields using values from superblock */
	bitmap->chunksize = chunksize;
	bitmap->daemon_sleep = daemon_sleep;
	bitmap->flags |= sb->state;
	bitmap->events_cleared = le64_to_cpu(sb->events_cleared);
	err = 0;
out:
	kunmap(bitmap->sb_page);
	if (err)
		bitmap_print_sb(bitmap);
	return err;
}

enum bitmap_mask_op {
	MASK_SET,
	MASK_UNSET
};

/* record the state of the bitmap in the superblock */
static void bitmap_mask_state(struct bitmap *bitmap, enum bitmap_state bits,
				enum bitmap_mask_op op)
{
	bitmap_super_t *sb;
	unsigned long flags;

	spin_lock_irqsave(&bitmap->lock, flags);
	if (!bitmap || !bitmap->sb_page) { /* can't set the state */
		spin_unlock_irqrestore(&bitmap->lock, flags);
		return;
	}
	page_cache_get(bitmap->sb_page);
	spin_unlock_irqrestore(&bitmap->lock, flags);
	sb = (bitmap_super_t *)kmap(bitmap->sb_page);
	switch (op) {
		case MASK_SET: sb->state |= bits;
				break;
		case MASK_UNSET: sb->state &= ~bits;
				break;
		default: BUG();
	}
	kunmap(bitmap->sb_page);
	page_cache_release(bitmap->sb_page);
}

/*
 * general bitmap file operations
 */

/* calculate the index of the page that contains this bit */
static inline unsigned long file_page_index(unsigned long chunk)
{
	return CHUNK_BIT_OFFSET(chunk) >> PAGE_BIT_SHIFT;
}

/* calculate the (bit) offset of this bit within a page */
static inline unsigned long file_page_offset(unsigned long chunk)
{
	return CHUNK_BIT_OFFSET(chunk) & (PAGE_BITS - 1);
}

/*
 * return a pointer to the page in the filemap that contains the given bit
 *
 * this lookup is complicated by the fact that the bitmap sb might be exactly
 * 1 page (e.g., x86) or less than 1 page -- so the bitmap might start on page
 * 0 or page 1
 */
static inline struct page *filemap_get_page(struct bitmap *bitmap,
					unsigned long chunk)
{
	return bitmap->filemap[file_page_index(chunk) - file_page_index(0)];
}


static void bitmap_file_unmap(struct bitmap *bitmap)
{
	struct page **map, *sb_page;
	unsigned long *attr;
	int pages;
	unsigned long flags;

	spin_lock_irqsave(&bitmap->lock, flags);
	map = bitmap->filemap;
	bitmap->filemap = NULL;
	attr = bitmap->filemap_attr;
	bitmap->filemap_attr = NULL;
	pages = bitmap->file_pages;
	bitmap->file_pages = 0;
	sb_page = bitmap->sb_page;
	bitmap->sb_page = NULL;
	spin_unlock_irqrestore(&bitmap->lock, flags);

	while (pages--)
		if (map[pages]->index != 0) /* 0 is sb_page, release it below */
			page_cache_release(map[pages]);
	kfree(map);
	kfree(attr);

	if (sb_page)
		page_cache_release(sb_page);
}

static void bitmap_stop_daemons(struct bitmap *bitmap);

/* dequeue the next item in a page list -- don't call from irq context */
static struct page_list *dequeue_page(struct bitmap *bitmap,
					struct list_head *head)
{
	struct page_list *item = NULL;

	spin_lock(&bitmap->write_lock);
	if (list_empty(head))
		goto out;
	item = list_entry(head->prev, struct page_list, list);
	list_del(head->prev);
out:
	spin_unlock(&bitmap->write_lock);
	return item;
}

static void drain_write_queues(struct bitmap *bitmap)
{
	struct list_head *queues[] = { 	&bitmap->complete_pages, NULL };
	struct list_head *head;
	struct page_list *item;
	int i;

	for (i = 0; queues[i]; i++) {
		head = queues[i];
		while ((item = dequeue_page(bitmap, head))) {
			page_cache_release(item->page);
			mempool_free(item, bitmap->write_pool);
		}
	}

	spin_lock(&bitmap->write_lock);
	bitmap->writes_pending = 0; /* make sure waiters continue */
	wake_up(&bitmap->write_wait);
	spin_unlock(&bitmap->write_lock);
}

static void bitmap_file_put(struct bitmap *bitmap)
{
	struct file *file;
	struct inode *inode;
	unsigned long flags;

	spin_lock_irqsave(&bitmap->lock, flags);
	file = bitmap->file;
	bitmap->file = NULL;
	spin_unlock_irqrestore(&bitmap->lock, flags);

	bitmap_stop_daemons(bitmap);

	drain_write_queues(bitmap);

	bitmap_file_unmap(bitmap);

	if (file) {
		inode = file->f_mapping->host;
		spin_lock(&inode->i_lock);
		atomic_set(&inode->i_writecount, 1); /* allow writes again */
		spin_unlock(&inode->i_lock);
		fput(file);
	}
}


/*
 * bitmap_file_kick - if an error occurs while manipulating the bitmap file
 * then it is no longer reliable, so we stop using it and we mark the file
 * as failed in the superblock
 */
static void bitmap_file_kick(struct bitmap *bitmap)
{
	char *path, *ptr = NULL;

	bitmap_mask_state(bitmap, BITMAP_STALE, MASK_SET);
	bitmap_update_sb(bitmap);

	path = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (path)
		ptr = file_path(bitmap->file, path, PAGE_SIZE);

	printk(KERN_ALERT "%s: kicking failed bitmap file %s from array!\n",
		bmname(bitmap), ptr ? ptr : "");

	kfree(path);

	bitmap_file_put(bitmap);

	return;
}

enum bitmap_page_attr {
	BITMAP_PAGE_DIRTY = 1, // there are set bits that need to be synced
	BITMAP_PAGE_CLEAN = 2, // there are bits that might need to be cleared
	BITMAP_PAGE_NEEDWRITE=4, // there are cleared bits that need to be synced
};

static inline void set_page_attr(struct bitmap *bitmap, struct page *page,
				enum bitmap_page_attr attr)
{
	bitmap->filemap_attr[page->index] |= attr;
}

static inline void clear_page_attr(struct bitmap *bitmap, struct page *page,
				enum bitmap_page_attr attr)
{
	bitmap->filemap_attr[page->index] &= ~attr;
}

static inline unsigned long get_page_attr(struct bitmap *bitmap, struct page *page)
{
	return bitmap->filemap_attr[page->index];
}

/*
 * bitmap_file_set_bit -- called before performing a write to the md device
 * to set (and eventually sync) a particular bit in the bitmap file
 *
 * we set the bit immediately, then we record the page number so that
 * when an unplug occurs, we can flush the dirty pages out to disk
 */
static void bitmap_file_set_bit(struct bitmap *bitmap, sector_t block)
{
	unsigned long bit;
	struct page *page;
	void *kaddr;
	unsigned long chunk = block >> CHUNK_BLOCK_SHIFT(bitmap);

	if (!bitmap->file || !bitmap->filemap) {
		return;
	}

	page = filemap_get_page(bitmap, chunk);
	bit = file_page_offset(chunk);


	/* make sure the page stays cached until it gets written out */
	if (! (get_page_attr(bitmap, page) & BITMAP_PAGE_DIRTY))
		page_cache_get(page);

 	/* set the bit */
	kaddr = kmap_atomic(page, KM_USER0);
	set_bit(bit, kaddr);
	kunmap_atomic(kaddr, KM_USER0);
	PRINTK("set file bit %lu page %lu\n", bit, page->index);

	/* record page number so it gets flushed to disk when unplug occurs */
	set_page_attr(bitmap, page, BITMAP_PAGE_DIRTY);

}

/* this gets called when the md device is ready to unplug its underlying
 * (slave) device queues -- before we let any writes go down, we need to
 * sync the dirty pages of the bitmap file to disk */
int bitmap_unplug(struct bitmap *bitmap)
{
	unsigned long i, attr, flags;
	struct page *page;
	int wait = 0;

	if (!bitmap)
		return 0;

	/* look at each page to see if there are any set bits that need to be
	 * flushed out to disk */
	for (i = 0; i < bitmap->file_pages; i++) {
		spin_lock_irqsave(&bitmap->lock, flags);
		if (!bitmap->file || !bitmap->filemap) {
			spin_unlock_irqrestore(&bitmap->lock, flags);
			return 0;
		}
		page = bitmap->filemap[i];
		attr = get_page_attr(bitmap, page);
		clear_page_attr(bitmap, page, BITMAP_PAGE_DIRTY);
		clear_page_attr(bitmap, page, BITMAP_PAGE_NEEDWRITE);
		if ((attr & BITMAP_PAGE_DIRTY))
			wait = 1;
		spin_unlock_irqrestore(&bitmap->lock, flags);

		if (attr & (BITMAP_PAGE_DIRTY | BITMAP_PAGE_NEEDWRITE))
			write_page(page, 0);
	}
	if (wait) { /* if any writes were performed, we need to wait on them */
		spin_lock_irq(&bitmap->write_lock);
		wait_event_lock_irq(bitmap->write_wait,
			bitmap->writes_pending == 0, bitmap->write_lock,
			wake_up_process(bitmap->writeback_daemon->tsk));
		spin_unlock_irq(&bitmap->write_lock);
	}
	return 0;
}

static void bitmap_set_memory_bits(struct bitmap *bitmap, sector_t offset,
	unsigned long sectors, int set);
/* * bitmap_init_from_disk -- called at bitmap_create time to initialize
 * the in-memory bitmap from the on-disk bitmap -- also, sets up the
 * memory mapping of the bitmap file
 * Special cases:
 *   if there's no bitmap file, or if the bitmap file had been
 *   previously kicked from the array, we mark all the bits as
 *   1's in order to cause a full resync.
 */
static int bitmap_init_from_disk(struct bitmap *bitmap)
{
	unsigned long i, chunks, index, oldindex, bit;
	struct page *page = NULL, *oldpage = NULL;
	unsigned long num_pages, bit_cnt = 0;
	struct file *file;
	unsigned long bytes, offset, dummy;
	int outofdate;
	int ret = -ENOSPC;

	chunks = bitmap->chunks;
	file = bitmap->file;

	BUG_ON(!file);

#if INJECT_FAULTS_3
	outofdate = 1;
#else
	outofdate = bitmap->flags & BITMAP_STALE;
#endif
	if (outofdate)
		printk(KERN_INFO "%s: bitmap file is out of date, doing full "
			"recovery\n", bmname(bitmap));

	bytes = (chunks + 7) / 8;
	num_pages = (bytes + PAGE_SIZE - 1) / PAGE_SIZE;
	if (i_size_read(file->f_mapping->host) < bytes + sizeof(bitmap_super_t)) {
		printk(KERN_INFO "%s: bitmap file too short %lu < %lu\n",
			bmname(bitmap),
			(unsigned long) i_size_read(file->f_mapping->host),
			bytes + sizeof(bitmap_super_t));
		goto out;
	}
	num_pages++;
	bitmap->filemap = kmalloc(sizeof(struct page *) * num_pages, GFP_KERNEL);
	if (!bitmap->filemap) {
		ret = -ENOMEM;
		goto out;
	}

	bitmap->filemap_attr = kmalloc(sizeof(long) * num_pages, GFP_KERNEL);
	if (!bitmap->filemap_attr) {
		ret = -ENOMEM;
		goto out;
	}

	memset(bitmap->filemap_attr, 0, sizeof(long) * num_pages);

	oldindex = ~0L;

	for (i = 0; i < chunks; i++) {
		index = file_page_index(i);
		bit = file_page_offset(i);
		if (index != oldindex) { /* this is a new page, read it in */
			/* unmap the old page, we're done with it */
			if (oldpage != NULL)
				kunmap(oldpage);
			if (index == 0) {
				/*
				 * if we're here then the superblock page
				 * contains some bits (PAGE_SIZE != sizeof sb)
				 * we've already read it in, so just use it
				 */
				page = bitmap->sb_page;
				offset = sizeof(bitmap_super_t);
			} else {
				page = read_page(file, index, &dummy);
				if (IS_ERR(page)) { /* read error */
					ret = PTR_ERR(page);
					goto out;
				}
				offset = 0;
			}
			oldindex = index;
			oldpage = page;
			kmap(page);

			if (outofdate) {
				/*
				 * if bitmap is out of date, dirty the
			 	 * whole page and write it out
				 */
				memset(page_address(page) + offset, 0xff,
					PAGE_SIZE - offset);
				ret = write_page(page, 1);
				if (ret) {
					kunmap(page);
					/* release, page not in filemap yet */
					page_cache_release(page);
					goto out;
				}
			}

			bitmap->filemap[bitmap->file_pages++] = page;
		}
		if (test_bit(bit, page_address(page))) {
			/* if the disk bit is set, set the memory bit */
			bitmap_set_memory_bits(bitmap,
					i << CHUNK_BLOCK_SHIFT(bitmap), 1, 1);
			bit_cnt++;
		}
#if 0
		else
			bitmap_set_memory_bits(bitmap,
				       i << CHUNK_BLOCK_SHIFT(bitmap), 1, 0);
#endif
	}

 	/* everything went OK */
	ret = 0;
	bitmap_mask_state(bitmap, BITMAP_STALE, MASK_UNSET);

	if (page) /* unmap the last page */
		kunmap(page);

	if (bit_cnt) { /* Kick recovery if any bits were set */
		set_bit(MD_RECOVERY_NEEDED, &bitmap->mddev->recovery);
		md_wakeup_thread(bitmap->mddev->thread);
	}

out:
	printk(KERN_INFO "%s: bitmap initialized from disk: "
		"read %lu/%lu pages, set %lu bits, status: %d\n",
		bmname(bitmap), bitmap->file_pages, num_pages, bit_cnt, ret);

	return ret;
}


static void bitmap_count_page(struct bitmap *bitmap, sector_t offset, int inc)
{
	sector_t chunk = offset >> CHUNK_BLOCK_SHIFT(bitmap);
	unsigned long page = chunk >> PAGE_COUNTER_SHIFT;
	bitmap->bp[page].count += inc;
/*
	if (page == 0) printk("count page 0, offset %llu: %d gives %d\n",
			      (unsigned long long)offset, inc, bitmap->bp[page].count);
*/
	bitmap_checkfree(bitmap, page);
}
static bitmap_counter_t *bitmap_get_counter(struct bitmap *bitmap,
					    sector_t offset, int *blocks,
					    int create);

/*
 * bitmap daemon -- periodically wakes up to clean bits and flush pages
 *			out to disk
 */

int bitmap_daemon_work(struct bitmap *bitmap)
{
	unsigned long bit, j;
	unsigned long flags;
	struct page *page = NULL, *lastpage = NULL;
	int err = 0;
	int blocks;
	int attr;

	if (bitmap == NULL)
		return 0;
	if (time_before(jiffies, bitmap->daemon_lastrun + bitmap->daemon_sleep*HZ))
		return 0;
	bitmap->daemon_lastrun = jiffies;

	for (j = 0; j < bitmap->chunks; j++) {
		bitmap_counter_t *bmc;
		spin_lock_irqsave(&bitmap->lock, flags);
		if (!bitmap->file || !bitmap->filemap) {
			/* error or shutdown */
			spin_unlock_irqrestore(&bitmap->lock, flags);
			break;
		}

		page = filemap_get_page(bitmap, j);
		/* skip this page unless it's marked as needing cleaning */
		if (!((attr=get_page_attr(bitmap, page)) & BITMAP_PAGE_CLEAN)) {
			if (attr & BITMAP_PAGE_NEEDWRITE) {
				page_cache_get(page);
				clear_page_attr(bitmap, page, BITMAP_PAGE_NEEDWRITE);
			}
			spin_unlock_irqrestore(&bitmap->lock, flags);
			if (attr & BITMAP_PAGE_NEEDWRITE) {
				if (write_page(page, 0))
					bitmap_file_kick(bitmap);
				page_cache_release(page);
			}
			continue;
		}

		bit = file_page_offset(j);

		if (page != lastpage) {
			/* grab the new page, sync and release the old */
			page_cache_get(page);
			if (lastpage != NULL) {
				if (get_page_attr(bitmap, lastpage) & BITMAP_PAGE_NEEDWRITE) {
					clear_page_attr(bitmap, lastpage, BITMAP_PAGE_NEEDWRITE);
					spin_unlock_irqrestore(&bitmap->lock, flags);
					write_page(lastpage, 0);
				} else {
					set_page_attr(bitmap, lastpage, BITMAP_PAGE_NEEDWRITE);
					spin_unlock_irqrestore(&bitmap->lock, flags);
				}
				kunmap(lastpage);
				page_cache_release(lastpage);
				if (err)
					bitmap_file_kick(bitmap);
			} else
				spin_unlock_irqrestore(&bitmap->lock, flags);
			lastpage = page;
			kmap(page);
/*
			printk("bitmap clean at page %lu\n", j);
*/
			spin_lock_irqsave(&bitmap->lock, flags);
			clear_page_attr(bitmap, page, BITMAP_PAGE_CLEAN);
		}
		bmc = bitmap_get_counter(bitmap, j << CHUNK_BLOCK_SHIFT(bitmap),
					&blocks, 0);
		if (bmc) {
/*
  if (j < 100) printk("bitmap: j=%lu, *bmc = 0x%x\n", j, *bmc);
*/
			if (*bmc == 2) {
				*bmc=1; /* maybe clear the bit next time */
				set_page_attr(bitmap, page, BITMAP_PAGE_CLEAN);
			} else if (*bmc == 1) {
				/* we can clear the bit */
				*bmc = 0;
				bitmap_count_page(bitmap, j << CHUNK_BLOCK_SHIFT(bitmap),
						  -1);

				/* clear the bit */
				clear_bit(bit, page_address(page));
			}
		}
		spin_unlock_irqrestore(&bitmap->lock, flags);
	}

	/* now sync the final page */
	if (lastpage != NULL) {
		kunmap(lastpage);
		spin_lock_irqsave(&bitmap->lock, flags);
		if (get_page_attr(bitmap, lastpage) &BITMAP_PAGE_NEEDWRITE) {
			clear_page_attr(bitmap, lastpage, BITMAP_PAGE_NEEDWRITE);
			spin_unlock_irqrestore(&bitmap->lock, flags);
			write_page(lastpage, 0);
		} else {
			set_page_attr(bitmap, lastpage, BITMAP_PAGE_NEEDWRITE);
			spin_unlock_irqrestore(&bitmap->lock, flags);
		}

		page_cache_release(lastpage);
	}

	return err;
}

static void daemon_exit(struct bitmap *bitmap, mdk_thread_t **daemon)
{
	mdk_thread_t *dmn;
	unsigned long flags;

	/* if no one is waiting on us, we'll free the md thread struct
	 * and exit, otherwise we let the waiter clean things up */
	spin_lock_irqsave(&bitmap->lock, flags);
	if ((dmn = *daemon)) { /* no one is waiting, cleanup and exit */
		*daemon = NULL;
		spin_unlock_irqrestore(&bitmap->lock, flags);
		kfree(dmn);
		complete_and_exit(NULL, 0); /* do_exit not exported */
	}
	spin_unlock_irqrestore(&bitmap->lock, flags);
}

static void bitmap_writeback_daemon(mddev_t *mddev)
{
	struct bitmap *bitmap = mddev->bitmap;
	struct page *page;
	struct page_list *item;
	int err = 0;

	while (1) {
		PRINTK("%s: bitmap writeback daemon waiting...\n", bmname(bitmap));
		down_interruptible(&bitmap->write_done);
		if (signal_pending(current)) {
			printk(KERN_INFO
			    "%s: bitmap writeback daemon got signal, exiting...\n",
			    bmname(bitmap));
			break;
		}

		PRINTK("%s: bitmap writeback daemon woke up...\n", bmname(bitmap));
		/* wait on bitmap page writebacks */
		while ((item = dequeue_page(bitmap, &bitmap->complete_pages))) {
			page = item->page;
			mempool_free(item, bitmap->write_pool);
			PRINTK("wait on page writeback: %p %lu\n", page, bitmap->writes_pending);
			wait_on_page_writeback(page);
			PRINTK("finished page writeback: %p %lu\n", page, bitmap->writes_pending);
			spin_lock(&bitmap->write_lock);
			if (!--bitmap->writes_pending)
				wake_up(&bitmap->write_wait);
			spin_unlock(&bitmap->write_lock);
			err = PageError(page);
			page_cache_release(page);
			if (err) {
				printk(KERN_WARNING "%s: bitmap file writeback "
					"failed (page %lu): %d\n",
					bmname(bitmap), page->index, err);
				bitmap_file_kick(bitmap);
				goto out;
			}
		}
	}
out:
	if (err) {
		printk(KERN_INFO "%s: bitmap writeback daemon exiting (%d)\n",
			bmname(bitmap), err);
		daemon_exit(bitmap, &bitmap->writeback_daemon);
	}
	return;
}

static int bitmap_start_daemon(struct bitmap *bitmap, mdk_thread_t **ptr,
				void (*func)(mddev_t *), char *name)
{
	mdk_thread_t *daemon;
	unsigned long flags;
	char namebuf[32];

	spin_lock_irqsave(&bitmap->lock, flags);
	*ptr = NULL;
	if (!bitmap->file) /* no need for daemon if there's no backing file */
		goto out_unlock;

	spin_unlock_irqrestore(&bitmap->lock, flags);

#if INJECT_FATAL_FAULT_2
	daemon = NULL;
#else
	sprintf(namebuf, "%%s_%s", name);
	daemon = md_register_thread(func, bitmap->mddev, namebuf);
#endif
	if (!daemon) {
		printk(KERN_ERR "%s: failed to start bitmap daemon\n",
			bmname(bitmap));
		return -ECHILD;
	}

	spin_lock_irqsave(&bitmap->lock, flags);
	*ptr = daemon;

	md_wakeup_thread(daemon); /* start it running */

	PRINTK("%s: %s daemon (pid %d) started...\n",
		bmname(bitmap), name, bitmap->daemon->tsk->pid);
out_unlock:
	spin_unlock_irqrestore(&bitmap->lock, flags);
	return 0;
}

static int bitmap_start_daemons(struct bitmap *bitmap)
{
	int err = bitmap_start_daemon(bitmap, &bitmap->writeback_daemon,
					bitmap_writeback_daemon, "bitmap_wb");
	return err;
}

static void bitmap_stop_daemon(struct bitmap *bitmap, mdk_thread_t **ptr)
{
	mdk_thread_t *daemon;
	unsigned long flags;

	spin_lock_irqsave(&bitmap->lock, flags);
	daemon = *ptr;
	*ptr = NULL;
	spin_unlock_irqrestore(&bitmap->lock, flags);
	if (daemon)
		md_unregister_thread(daemon); /* destroy the thread */
}

static void bitmap_stop_daemons(struct bitmap *bitmap)
{
	/* the daemons can't stop themselves... they'll just exit instead... */
	if (bitmap->writeback_daemon &&
	    current->pid != bitmap->writeback_daemon->tsk->pid)
		bitmap_stop_daemon(bitmap, &bitmap->writeback_daemon);
}

static bitmap_counter_t *bitmap_get_counter(struct bitmap *bitmap,
					    sector_t offset, int *blocks,
					    int create)
{
	/* If 'create', we might release the lock and reclaim it.
	 * The lock must have been taken with interrupts enabled.
	 * If !create, we don't release the lock.
	 */
	sector_t chunk = offset >> CHUNK_BLOCK_SHIFT(bitmap);
	unsigned long page = chunk >> PAGE_COUNTER_SHIFT;
	unsigned long pageoff = (chunk & PAGE_COUNTER_MASK) << COUNTER_BYTE_SHIFT;
	sector_t csize;

	if (bitmap_checkpage(bitmap, page, create) < 0) {
		csize = ((sector_t)1) << (CHUNK_BLOCK_SHIFT(bitmap));
		*blocks = csize - (offset & (csize- 1));
		return NULL;
	}
	/* now locked ... */

	if (bitmap->bp[page].hijacked) { /* hijacked pointer */
		/* should we use the first or second counter field
		 * of the hijacked pointer? */
		int hi = (pageoff > PAGE_COUNTER_MASK);
		csize = ((sector_t)1) << (CHUNK_BLOCK_SHIFT(bitmap) +
					  PAGE_COUNTER_SHIFT - 1);
		*blocks = csize - (offset & (csize- 1));
		return  &((bitmap_counter_t *)
			  &bitmap->bp[page].map)[hi];
	} else { /* page is allocated */
		csize = ((sector_t)1) << (CHUNK_BLOCK_SHIFT(bitmap));
		*blocks = csize - (offset & (csize- 1));
		return (bitmap_counter_t *)
			&(bitmap->bp[page].map[pageoff]);
	}
}

int bitmap_startwrite(struct bitmap *bitmap, sector_t offset, unsigned long sectors)
{
	if (!bitmap) return 0;
	while (sectors) {
		int blocks;
		bitmap_counter_t *bmc;

		spin_lock_irq(&bitmap->lock);
		bmc = bitmap_get_counter(bitmap, offset, &blocks, 1);
		if (!bmc) {
			spin_unlock_irq(&bitmap->lock);
			return 0;
		}

		switch(*bmc) {
		case 0:
			bitmap_file_set_bit(bitmap, offset);
			bitmap_count_page(bitmap,offset, 1);
			blk_plug_device(bitmap->mddev->queue);
			/* fall through */
		case 1:
			*bmc = 2;
		}
		if ((*bmc & COUNTER_MAX) == COUNTER_MAX) BUG();
		(*bmc)++;

		spin_unlock_irq(&bitmap->lock);

		offset += blocks;
		if (sectors > blocks)
			sectors -= blocks;
		else sectors = 0;
	}
	return 0;
}

void bitmap_endwrite(struct bitmap *bitmap, sector_t offset, unsigned long sectors,
		     int success)
{
	if (!bitmap) return;
	while (sectors) {
		int blocks;
		unsigned long flags;
		bitmap_counter_t *bmc;

		spin_lock_irqsave(&bitmap->lock, flags);
		bmc = bitmap_get_counter(bitmap, offset, &blocks, 0);
		if (!bmc) {
			spin_unlock_irqrestore(&bitmap->lock, flags);
			return;
		}

		if (!success && ! (*bmc & NEEDED_MASK))
			*bmc |= NEEDED_MASK;

		(*bmc)--;
		if (*bmc <= 2) {
			set_page_attr(bitmap,
				      filemap_get_page(bitmap, offset >> CHUNK_BLOCK_SHIFT(bitmap)),
				      BITMAP_PAGE_CLEAN);
		}
		spin_unlock_irqrestore(&bitmap->lock, flags);
		offset += blocks;
		if (sectors > blocks)
			sectors -= blocks;
		else sectors = 0;
	}
}

int bitmap_start_sync(struct bitmap *bitmap, sector_t offset, int *blocks)
{
	bitmap_counter_t *bmc;
	int rv;
	if (bitmap == NULL) {/* FIXME or bitmap set as 'failed' */
		*blocks = 1024;
		return 1; /* always resync if no bitmap */
	}
	spin_lock_irq(&bitmap->lock);
	bmc = bitmap_get_counter(bitmap, offset, blocks, 0);
	rv = 0;
	if (bmc) {
		/* locked */
		if (RESYNC(*bmc))
			rv = 1;
		else if (NEEDED(*bmc)) {
			rv = 1;
			*bmc |= RESYNC_MASK;
			*bmc &= ~NEEDED_MASK;
		}
	}
	spin_unlock_irq(&bitmap->lock);
	return rv;
}

void bitmap_end_sync(struct bitmap *bitmap, sector_t offset, int *blocks, int aborted)
{
	bitmap_counter_t *bmc;
	unsigned long flags;
/*
	if (offset == 0) printk("bitmap_end_sync 0 (%d)\n", aborted);
*/	if (bitmap == NULL) {
		*blocks = 1024;
		return;
	}
	spin_lock_irqsave(&bitmap->lock, flags);
	bmc = bitmap_get_counter(bitmap, offset, blocks, 0);
	if (bmc == NULL)
		goto unlock;
	/* locked */
/*
	if (offset == 0) printk("bitmap_end sync found 0x%x, blocks %d\n", *bmc, *blocks);
*/
	if (RESYNC(*bmc)) {
		*bmc &= ~RESYNC_MASK;

		if (!NEEDED(*bmc) && aborted)
			*bmc |= NEEDED_MASK;
		else {
			if (*bmc <= 2) {
				set_page_attr(bitmap,
					      filemap_get_page(bitmap, offset >> CHUNK_BLOCK_SHIFT(bitmap)),
					      BITMAP_PAGE_CLEAN);
			}
		}
	}
 unlock:
	spin_unlock_irqrestore(&bitmap->lock, flags);
}

void bitmap_close_sync(struct bitmap *bitmap)
{
	/* Sync has finished, and any bitmap chunks that weren't synced
	 * properly have been aborted.  It remains to us to clear the
	 * RESYNC bit wherever it is still on
	 */
	sector_t sector = 0;
	int blocks;
	if (!bitmap) return;
	while (sector < bitmap->mddev->resync_max_sectors) {
		bitmap_end_sync(bitmap, sector, &blocks, 0);
/*
		if (sector < 500) printk("bitmap_close_sync: sec %llu blks %d\n",
					 (unsigned long long)sector, blocks);
*/		sector += blocks;
	}
}

static void bitmap_set_memory_bits(struct bitmap *bitmap, sector_t offset,
				   unsigned long sectors, int set)
{
	/* For each chunk covered by any of these sectors, set the
	 * resync needed bit, and the counter to 1.  They should all
	 * be 0 at this point
	 */
	while (sectors) {
		int secs;
		bitmap_counter_t *bmc;
		spin_lock_irq(&bitmap->lock);
		bmc = bitmap_get_counter(bitmap, offset, &secs, 1);
		if (!bmc) {
			spin_unlock_irq(&bitmap->lock);
			return;
		}
		if (set && !NEEDED(*bmc)) {
			BUG_ON(*bmc);
			*bmc = NEEDED_MASK | 1;
			bitmap_count_page(bitmap, offset, 1);
		}
		spin_unlock_irq(&bitmap->lock);
		if (sectors > secs)
			sectors -= secs;
		else
			sectors = 0;
	}
}

/* dirty the entire bitmap */
int bitmap_setallbits(struct bitmap *bitmap)
{
	unsigned long flags;
	unsigned long j;

	/* dirty the in-memory bitmap */
	bitmap_set_memory_bits(bitmap, 0, bitmap->chunks << CHUNK_BLOCK_SHIFT(bitmap), 1);

	/* dirty the bitmap file */
	for (j = 0; j < bitmap->file_pages; j++) {
		struct page *page = bitmap->filemap[j];

		spin_lock_irqsave(&bitmap->lock, flags);
		page_cache_get(page);
		spin_unlock_irqrestore(&bitmap->lock, flags);
		memset(kmap(page), 0xff, PAGE_SIZE);
		kunmap(page);
		write_page(page, 0);
	}

	return 0;
}

/*
 * free memory that was allocated
 */
void bitmap_destroy(mddev_t *mddev)
{
	unsigned long k, pages;
	struct bitmap_page *bp;
	struct bitmap *bitmap = mddev->bitmap;

	if (!bitmap) /* there was no bitmap */
		return;

	mddev->bitmap = NULL; /* disconnect from the md device */

	/* release the bitmap file and kill the daemon */
	bitmap_file_put(bitmap);

	bp = bitmap->bp;
	pages = bitmap->pages;

	/* free all allocated memory */

	mempool_destroy(bitmap->write_pool);

	if (bp) /* deallocate the page memory */
		for (k = 0; k < pages; k++)
			if (bp[k].map && !bp[k].hijacked)
				kfree(bp[k].map);
	kfree(bp);
	kfree(bitmap);
}

/*
 * initialize the bitmap structure
 * if this returns an error, bitmap_destroy must be called to do clean up
 */
int bitmap_create(mddev_t *mddev)
{
	struct bitmap *bitmap;
	unsigned long blocks = mddev->resync_max_sectors;
	unsigned long chunks;
	unsigned long pages;
	struct file *file = mddev->bitmap_file;
	int err;

	BUG_ON(sizeof(bitmap_super_t) != 256);

	if (!file) /* bitmap disabled, nothing to do */
		return 0;

	bitmap = kmalloc(sizeof(*bitmap), GFP_KERNEL);
	if (!bitmap)
		return -ENOMEM;

	memset(bitmap, 0, sizeof(*bitmap));

	spin_lock_init(&bitmap->lock);
	bitmap->mddev = mddev;
	mddev->bitmap = bitmap;

	spin_lock_init(&bitmap->write_lock);
	init_MUTEX_LOCKED(&bitmap->write_done);
	INIT_LIST_HEAD(&bitmap->complete_pages);
	init_waitqueue_head(&bitmap->write_wait);
	bitmap->write_pool = mempool_create(WRITE_POOL_SIZE, write_pool_alloc,
				write_pool_free, NULL);
	if (!bitmap->write_pool)
		return -ENOMEM;

	bitmap->file = file;
	get_file(file);
	/* read superblock from bitmap file (this sets bitmap->chunksize) */
	err = bitmap_read_sb(bitmap);
	if (err)
		return err;

	bitmap->chunkshift = find_first_bit(&bitmap->chunksize,
					sizeof(bitmap->chunksize));

	/* now that chunksize and chunkshift are set, we can use these macros */
 	chunks = (blocks + CHUNK_BLOCK_RATIO(bitmap) - 1) /
			CHUNK_BLOCK_RATIO(bitmap);
 	pages = (chunks + PAGE_COUNTER_RATIO - 1) / PAGE_COUNTER_RATIO;

	BUG_ON(!pages);

	bitmap->chunks = chunks;
	bitmap->pages = pages;
	bitmap->missing_pages = pages;
	bitmap->counter_bits = COUNTER_BITS;

	bitmap->syncchunk = ~0UL;

#if INJECT_FATAL_FAULT_1
	bitmap->bp = NULL;
#else
	bitmap->bp = kmalloc(pages * sizeof(*bitmap->bp), GFP_KERNEL);
#endif
	if (!bitmap->bp)
		return -ENOMEM;
	memset(bitmap->bp, 0, pages * sizeof(*bitmap->bp));

	bitmap->flags |= BITMAP_ACTIVE;

	/* now that we have some pages available, initialize the in-memory
	 * bitmap from the on-disk bitmap */
	err = bitmap_init_from_disk(bitmap);
	if (err)
		return err;

	printk(KERN_INFO "created bitmap (%lu pages) for device %s\n",
		pages, bmname(bitmap));

	/* kick off the bitmap daemons */
	err = bitmap_start_daemons(bitmap);
	if (err)
		return err;
	return bitmap_update_sb(bitmap);
}

/* the bitmap API -- for raid personalities */
EXPORT_SYMBOL(bitmap_startwrite);
EXPORT_SYMBOL(bitmap_endwrite);
EXPORT_SYMBOL(bitmap_start_sync);
EXPORT_SYMBOL(bitmap_end_sync);
EXPORT_SYMBOL(bitmap_unplug);
EXPORT_SYMBOL(bitmap_close_sync);
EXPORT_SYMBOL(bitmap_daemon_work);
