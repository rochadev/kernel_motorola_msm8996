/* -*- c -*- --------------------------------------------------------------- *
 *
 * linux/fs/autofs/root.c
 *
 *  Copyright 1997-1998 Transmeta Corporation -- All Rights Reserved
 *  Copyright 1999-2000 Jeremy Fitzhardinge <jeremy@goop.org>
 *  Copyright 2001-2006 Ian Kent <raven@themaw.net>
 *
 * This file is part of the Linux kernel and is made available under
 * the terms of the GNU General Public License, version 2, or at your
 * option, any later version, incorporated herein by reference.
 *
 * ------------------------------------------------------------------------- */

#include <linux/capability.h>
#include <linux/errno.h>
#include <linux/stat.h>
#include <linux/slab.h>
#include <linux/param.h>
#include <linux/time.h>
#include <linux/compat.h>
#include <linux/mutex.h>

#include "autofs_i.h"

DEFINE_SPINLOCK(autofs4_lock);

static int autofs4_dir_symlink(struct inode *,struct dentry *,const char *);
static int autofs4_dir_unlink(struct inode *,struct dentry *);
static int autofs4_dir_rmdir(struct inode *,struct dentry *);
static int autofs4_dir_mkdir(struct inode *,struct dentry *,int);
static long autofs4_root_ioctl(struct file *,unsigned int,unsigned long);
#ifdef CONFIG_COMPAT
static long autofs4_root_compat_ioctl(struct file *,unsigned int,unsigned long);
#endif
static int autofs4_dir_open(struct inode *inode, struct file *file);
static struct dentry *autofs4_lookup(struct inode *,struct dentry *, struct nameidata *);

#define TRIGGER_FLAGS   (LOOKUP_CONTINUE | LOOKUP_DIRECTORY)
#define TRIGGER_INTENTS (LOOKUP_OPEN | LOOKUP_CREATE)

const struct file_operations autofs4_root_operations = {
	.open		= dcache_dir_open,
	.release	= dcache_dir_close,
	.read		= generic_read_dir,
	.readdir	= dcache_readdir,
	.llseek		= dcache_dir_lseek,
	.unlocked_ioctl	= autofs4_root_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= autofs4_root_compat_ioctl,
#endif
};

const struct file_operations autofs4_dir_operations = {
	.open		= autofs4_dir_open,
	.release	= dcache_dir_close,
	.read		= generic_read_dir,
	.readdir	= dcache_readdir,
	.llseek		= dcache_dir_lseek,
};

const struct inode_operations autofs4_indirect_root_inode_operations = {
	.lookup		= autofs4_lookup,
	.unlink		= autofs4_dir_unlink,
	.symlink	= autofs4_dir_symlink,
	.mkdir		= autofs4_dir_mkdir,
	.rmdir		= autofs4_dir_rmdir,
};

const struct inode_operations autofs4_direct_root_inode_operations = {
	.lookup		= autofs4_lookup,
	.unlink		= autofs4_dir_unlink,
	.mkdir		= autofs4_dir_mkdir,
	.rmdir		= autofs4_dir_rmdir,
};

const struct inode_operations autofs4_dir_inode_operations = {
	.lookup		= autofs4_lookup,
	.unlink		= autofs4_dir_unlink,
	.symlink	= autofs4_dir_symlink,
	.mkdir		= autofs4_dir_mkdir,
	.rmdir		= autofs4_dir_rmdir,
};

static void autofs4_add_active(struct dentry *dentry)
{
	struct autofs_sb_info *sbi = autofs4_sbi(dentry->d_sb);
	struct autofs_info *ino = autofs4_dentry_ino(dentry);
	if (ino) {
		spin_lock(&sbi->lookup_lock);
		if (!ino->active_count) {
			if (list_empty(&ino->active))
				list_add(&ino->active, &sbi->active_list);
		}
		ino->active_count++;
		spin_unlock(&sbi->lookup_lock);
	}
	return;
}

static void autofs4_del_active(struct dentry *dentry)
{
	struct autofs_sb_info *sbi = autofs4_sbi(dentry->d_sb);
	struct autofs_info *ino = autofs4_dentry_ino(dentry);
	if (ino) {
		spin_lock(&sbi->lookup_lock);
		ino->active_count--;
		if (!ino->active_count) {
			if (!list_empty(&ino->active))
				list_del_init(&ino->active);
		}
		spin_unlock(&sbi->lookup_lock);
	}
	return;
}

static unsigned int autofs4_need_mount(unsigned int flags)
{
	unsigned int res = 0;
	if (flags & (TRIGGER_FLAGS | TRIGGER_INTENTS))
		res = 1;
	return res;
}

static int autofs4_dir_open(struct inode *inode, struct file *file)
{
	struct dentry *dentry = file->f_path.dentry;
	struct autofs_sb_info *sbi = autofs4_sbi(dentry->d_sb);

	DPRINTK("file=%p dentry=%p %.*s",
		file, dentry, dentry->d_name.len, dentry->d_name.name);

	if (autofs4_oz_mode(sbi))
		goto out;

	/*
	 * An empty directory in an autofs file system is always a
	 * mount point. The daemon must have failed to mount this
	 * during lookup so it doesn't exist. This can happen, for
	 * example, if user space returns an incorrect status for a
	 * mount request. Otherwise we're doing a readdir on the
	 * autofs file system so just let the libfs routines handle
	 * it.
	 */
	spin_lock(&autofs4_lock);
	spin_lock(&dentry->d_lock);
	if (!d_mountpoint(dentry) && list_empty(&dentry->d_subdirs)) {
		spin_unlock(&dentry->d_lock);
		spin_unlock(&autofs4_lock);
		return -ENOENT;
	}
	spin_unlock(&dentry->d_lock);
	spin_unlock(&autofs4_lock);

out:
	return dcache_dir_open(inode, file);
}

static int try_to_fill_dentry(struct dentry *dentry, int flags)
{
	struct autofs_sb_info *sbi = autofs4_sbi(dentry->d_sb);
	struct autofs_info *ino = autofs4_dentry_ino(dentry);
	int status;

	DPRINTK("dentry=%p %.*s ino=%p",
		 dentry, dentry->d_name.len, dentry->d_name.name, dentry->d_inode);

	/*
	 * Wait for a pending mount, triggering one if there
	 * isn't one already
	 */
	if (dentry->d_inode == NULL) {
		DPRINTK("waiting for mount name=%.*s",
			 dentry->d_name.len, dentry->d_name.name);

		status = autofs4_wait(sbi, dentry, NFY_MOUNT);

		DPRINTK("mount done status=%d", status);

		/* Turn this into a real negative dentry? */
		if (status == -ENOENT) {
			spin_lock(&sbi->fs_lock);
			ino->flags &= ~AUTOFS_INF_PENDING;
			spin_unlock(&sbi->fs_lock);
			return status;
		} else if (status) {
			/* Return a negative dentry, but leave it "pending" */
			return status;
		}
	/* Trigger mount for path component or follow link */
	} else if (ino->flags & AUTOFS_INF_PENDING ||
			autofs4_need_mount(flags)) {
		DPRINTK("waiting for mount name=%.*s",
			dentry->d_name.len, dentry->d_name.name);

		spin_lock(&sbi->fs_lock);
		ino->flags |= AUTOFS_INF_PENDING;
		spin_unlock(&sbi->fs_lock);
		status = autofs4_wait(sbi, dentry, NFY_MOUNT);

		DPRINTK("mount done status=%d", status);

		if (status) {
			spin_lock(&sbi->fs_lock);
			ino->flags &= ~AUTOFS_INF_PENDING;
			spin_unlock(&sbi->fs_lock);
			return status;
		}
	}

	/* Initialize expiry counter after successful mount */
	ino->last_used = jiffies;

	spin_lock(&sbi->fs_lock);
	ino->flags &= ~AUTOFS_INF_PENDING;
	spin_unlock(&sbi->fs_lock);

	return 0;
}

/* For autofs direct mounts the follow link triggers the mount */
static void *autofs4_follow_link(struct dentry *dentry, struct nameidata *nd)
{
	struct autofs_sb_info *sbi = autofs4_sbi(dentry->d_sb);
	struct autofs_info *ino = autofs4_dentry_ino(dentry);
	int oz_mode = autofs4_oz_mode(sbi);
	unsigned int lookup_type;
	int status;

	DPRINTK("dentry=%p %.*s oz_mode=%d nd->flags=%d",
		dentry, dentry->d_name.len, dentry->d_name.name, oz_mode,
		nd->flags);
	/*
	 * For an expire of a covered direct or offset mount we need
	 * to break out of follow_down_one() at the autofs mount trigger
	 * (d_mounted--), so we can see the expiring flag, and manage
	 * the blocking and following here until the expire is completed.
	 */
	if (oz_mode) {
		spin_lock(&sbi->fs_lock);
		if (ino->flags & AUTOFS_INF_EXPIRING) {
			spin_unlock(&sbi->fs_lock);
			/* Follow down to our covering mount. */
			if (!follow_down_one(&nd->path))
				goto done;
			goto follow;
		}
		spin_unlock(&sbi->fs_lock);
		goto done;
	}

	/* If an expire request is pending everyone must wait. */
	autofs4_expire_wait(dentry);

	/* We trigger a mount for almost all flags */
	lookup_type = autofs4_need_mount(nd->flags);
	spin_lock(&sbi->fs_lock);
	spin_lock(&autofs4_lock);
	spin_lock(&dentry->d_lock);
	if (!(lookup_type || ino->flags & AUTOFS_INF_PENDING)) {
		spin_unlock(&dentry->d_lock);
		spin_unlock(&autofs4_lock);
		spin_unlock(&sbi->fs_lock);
		goto follow;
	}

	/*
	 * If the dentry contains directories then it is an autofs
	 * multi-mount with no root mount offset. So don't try to
	 * mount it again.
	 */
	if (ino->flags & AUTOFS_INF_PENDING ||
	    (!d_mountpoint(dentry) && list_empty(&dentry->d_subdirs))) {
		spin_unlock(&dentry->d_lock);
		spin_unlock(&autofs4_lock);
		spin_unlock(&sbi->fs_lock);

		status = try_to_fill_dentry(dentry, nd->flags);
		if (status)
			goto out_error;

		goto follow;
	}
	spin_unlock(&dentry->d_lock);
	spin_unlock(&autofs4_lock);
	spin_unlock(&sbi->fs_lock);
follow:
	/*
	 * If there is no root mount it must be an autofs
	 * multi-mount with no root offset so we don't need
	 * to follow it.
	 */
	if (d_managed(dentry)) {
		status = follow_down(&nd->path, false);
		if (status < 0)
			goto out_error;
	}

done:
	return NULL;

out_error:
	path_put(&nd->path);
	return ERR_PTR(status);
}

/*
 * Revalidate is called on every cache lookup.  Some of those
 * cache lookups may actually happen while the dentry is not
 * yet completely filled in, and revalidate has to delay such
 * lookups..
 */
static int autofs4_revalidate(struct dentry *dentry, struct nameidata *nd)
{
	struct inode *dir;
	struct autofs_sb_info *sbi;
	int oz_mode;
	int flags = nd ? nd->flags : 0;
	int status = 1;

	if (flags & LOOKUP_RCU)
		return -ECHILD;

	dir = dentry->d_parent->d_inode;
	sbi = autofs4_sbi(dir->i_sb);
	oz_mode = autofs4_oz_mode(sbi);

	/* Pending dentry */
	spin_lock(&sbi->fs_lock);
	if (autofs4_ispending(dentry)) {
		/* The daemon never causes a mount to trigger */
		spin_unlock(&sbi->fs_lock);

		if (oz_mode)
			return 1;

		/*
		 * If the directory has gone away due to an expire
		 * we have been called as ->d_revalidate() and so
		 * we need to return false and proceed to ->lookup().
		 */
		if (autofs4_expire_wait(dentry) == -EAGAIN)
			return 0;

		/*
		 * A zero status is success otherwise we have a
		 * negative error code.
		 */
		status = try_to_fill_dentry(dentry, flags);
		if (status == 0)
			return 1;

		return status;
	}
	spin_unlock(&sbi->fs_lock);

	/* Negative dentry.. invalidate if "old" */
	if (dentry->d_inode == NULL)
		return 0;

	/* Check for a non-mountpoint directory with no contents */
	spin_lock(&autofs4_lock);
	spin_lock(&dentry->d_lock);
	if (S_ISDIR(dentry->d_inode->i_mode) &&
	    !d_mountpoint(dentry) && list_empty(&dentry->d_subdirs)) {
		DPRINTK("dentry=%p %.*s, emptydir",
			 dentry, dentry->d_name.len, dentry->d_name.name);
		spin_unlock(&dentry->d_lock);
		spin_unlock(&autofs4_lock);

		/* The daemon never causes a mount to trigger */
		if (oz_mode)
			return 1;

		/*
		 * A zero status is success otherwise we have a
		 * negative error code.
		 */
		status = try_to_fill_dentry(dentry, flags);
		if (status == 0)
			return 1;

		return status;
	}
	spin_unlock(&dentry->d_lock);
	spin_unlock(&autofs4_lock);

	return 1;
}

void autofs4_dentry_release(struct dentry *de)
{
	struct autofs_info *inf;

	DPRINTK("releasing %p", de);

	inf = autofs4_dentry_ino(de);
	de->d_fsdata = NULL;

	if (inf) {
		struct autofs_sb_info *sbi = autofs4_sbi(de->d_sb);

		if (sbi) {
			spin_lock(&sbi->lookup_lock);
			if (!list_empty(&inf->active))
				list_del(&inf->active);
			if (!list_empty(&inf->expiring))
				list_del(&inf->expiring);
			spin_unlock(&sbi->lookup_lock);
		}

		inf->dentry = NULL;
		inf->inode = NULL;

		autofs4_free_ino(inf);
	}
}

/* For dentries of directories in the root dir */
static const struct dentry_operations autofs4_root_dentry_operations = {
	.d_release	= autofs4_dentry_release,
};

/* For other dentries */
static const struct dentry_operations autofs4_dentry_operations = {
	.d_automount	= autofs4_d_automount,
	.d_release	= autofs4_dentry_release,
};

static struct dentry *autofs4_lookup_active(struct dentry *dentry)
{
	struct autofs_sb_info *sbi = autofs4_sbi(dentry->d_sb);
	struct dentry *parent = dentry->d_parent;
	struct qstr *name = &dentry->d_name;
	unsigned int len = name->len;
	unsigned int hash = name->hash;
	const unsigned char *str = name->name;
	struct list_head *p, *head;

	spin_lock(&autofs4_lock);
	spin_lock(&sbi->lookup_lock);
	head = &sbi->active_list;
	list_for_each(p, head) {
		struct autofs_info *ino;
		struct dentry *active;
		struct qstr *qstr;

		ino = list_entry(p, struct autofs_info, active);
		active = ino->dentry;

		spin_lock(&active->d_lock);

		/* Already gone? */
		if (active->d_count == 0)
			goto next;

		qstr = &active->d_name;

		if (active->d_name.hash != hash)
			goto next;
		if (active->d_parent != parent)
			goto next;

		if (qstr->len != len)
			goto next;
		if (memcmp(qstr->name, str, len))
			goto next;

		if (d_unhashed(active)) {
			dget_dlock(active);
			spin_unlock(&active->d_lock);
			spin_unlock(&sbi->lookup_lock);
			spin_unlock(&autofs4_lock);
			return active;
		}
next:
		spin_unlock(&active->d_lock);
	}
	spin_unlock(&sbi->lookup_lock);
	spin_unlock(&autofs4_lock);

	return NULL;
}

static struct dentry *autofs4_lookup_expiring(struct dentry *dentry)
{
	struct autofs_sb_info *sbi = autofs4_sbi(dentry->d_sb);
	struct dentry *parent = dentry->d_parent;
	struct qstr *name = &dentry->d_name;
	unsigned int len = name->len;
	unsigned int hash = name->hash;
	const unsigned char *str = name->name;
	struct list_head *p, *head;

	spin_lock(&autofs4_lock);
	spin_lock(&sbi->lookup_lock);
	head = &sbi->expiring_list;
	list_for_each(p, head) {
		struct autofs_info *ino;
		struct dentry *expiring;
		struct qstr *qstr;

		ino = list_entry(p, struct autofs_info, expiring);
		expiring = ino->dentry;

		spin_lock(&expiring->d_lock);

		/* Bad luck, we've already been dentry_iput */
		if (!expiring->d_inode)
			goto next;

		qstr = &expiring->d_name;

		if (expiring->d_name.hash != hash)
			goto next;
		if (expiring->d_parent != parent)
			goto next;

		if (qstr->len != len)
			goto next;
		if (memcmp(qstr->name, str, len))
			goto next;

		if (d_unhashed(expiring)) {
			dget_dlock(expiring);
			spin_unlock(&expiring->d_lock);
			spin_unlock(&sbi->lookup_lock);
			spin_unlock(&autofs4_lock);
			return expiring;
		}
next:
		spin_unlock(&expiring->d_lock);
	}
	spin_unlock(&sbi->lookup_lock);
	spin_unlock(&autofs4_lock);

	return NULL;
}

static int autofs4_mount_wait(struct dentry *dentry)
{
	struct autofs_sb_info *sbi = autofs4_sbi(dentry->d_sb);
	struct autofs_info *ino = autofs4_dentry_ino(dentry);
	int status;

	if (ino->flags & AUTOFS_INF_PENDING) {
		DPRINTK("waiting for mount name=%.*s",
			dentry->d_name.len, dentry->d_name.name);
		status = autofs4_wait(sbi, dentry, NFY_MOUNT);
		DPRINTK("mount wait done status=%d", status);
		ino->last_used = jiffies;
		return status;
	}
	return 0;
}

static int do_expire_wait(struct dentry *dentry)
{
	struct dentry *expiring;

	expiring = autofs4_lookup_expiring(dentry);
	if (!expiring)
		return autofs4_expire_wait(dentry);
	else {
		/*
		 * If we are racing with expire the request might not
		 * be quite complete, but the directory has been removed
		 * so it must have been successful, just wait for it.
		 */
		autofs4_expire_wait(expiring);
		autofs4_del_expiring(expiring);
		dput(expiring);
	}
	return 0;
}

static struct dentry *autofs4_mountpoint_changed(struct path *path)
{
	struct dentry *dentry = path->dentry;
	struct autofs_sb_info *sbi = autofs4_sbi(dentry->d_sb);

	/*
	 * If this is an indirect mount the dentry could have gone away
	 * as a result of an expire and a new one created.
	 */
	if (autofs_type_indirect(sbi->type) && d_unhashed(dentry)) {
		struct dentry *parent = dentry->d_parent;
		struct dentry *new = d_lookup(parent, &dentry->d_name);
		if (!new)
			return NULL;
		dput(path->dentry);
		path->dentry = new;
	}
	return path->dentry;
}

struct vfsmount *autofs4_d_automount(struct path *path)
{
	struct dentry *dentry = path->dentry;
	struct autofs_sb_info *sbi = autofs4_sbi(dentry->d_sb);
	struct autofs_info *ino = autofs4_dentry_ino(dentry);
	int status;

	DPRINTK("dentry=%p %.*s",
		dentry, dentry->d_name.len, dentry->d_name.name);

	/* The daemon never triggers a mount. */
	if (autofs4_oz_mode(sbi))
		return NULL;

	/*
	 * If an expire request is pending everyone must wait.
	 * If the expire fails we're still mounted so continue
	 * the follow and return. A return of -EAGAIN (which only
	 * happens with indirect mounts) means the expire completed
	 * and the directory was removed, so just go ahead and try
	 * the mount.
	 */
	status = do_expire_wait(dentry);
	if (status && status != -EAGAIN)
		return NULL;

	/* Callback to the daemon to perform the mount or wait */
	spin_lock(&sbi->fs_lock);
	if (ino->flags & AUTOFS_INF_PENDING) {
		spin_unlock(&sbi->fs_lock);
		status = autofs4_mount_wait(dentry);
		if (status)
			return ERR_PTR(status);
		spin_lock(&sbi->fs_lock);
		goto done;
	}

	/*
	 * If the dentry is a symlink it's equivalent to a directory
	 * having d_mounted() true, so there's no need to call back
	 * to the daemon.
	 */
	if (dentry->d_inode && S_ISLNK(dentry->d_inode->i_mode))
		goto done;
	spin_lock(&dentry->d_lock);
	if (!d_mountpoint(dentry) && list_empty(&dentry->d_subdirs)) {
		ino->flags |= AUTOFS_INF_PENDING;
		spin_unlock(&dentry->d_lock);
		spin_unlock(&sbi->fs_lock);
		status = autofs4_mount_wait(dentry);
		if (status)
			return ERR_PTR(status);
		spin_lock(&sbi->fs_lock);
		ino->flags &= ~AUTOFS_INF_PENDING;
		goto done;
	}
	spin_unlock(&dentry->d_lock);
done:
	/*
	 * Any needed mounting has been completed and the path updated
	 * so turn this into a normal dentry so we don't continually
	 * call ->d_automount().
	 */
	managed_dentry_clear_automount(dentry);
	spin_unlock(&sbi->fs_lock);

	/* Mount succeeded, check if we ended up with a new dentry */
	dentry = autofs4_mountpoint_changed(path);
	if (!dentry)
		return ERR_PTR(-ENOENT);

	return NULL;
}

/* Lookups in the root directory */
static struct dentry *autofs4_lookup(struct inode *dir, struct dentry *dentry, struct nameidata *nd)
{
	struct autofs_sb_info *sbi;
	struct autofs_info *ino;
	struct dentry *active;

	DPRINTK("name = %.*s", dentry->d_name.len, dentry->d_name.name);

	/* File name too long to exist */
	if (dentry->d_name.len > NAME_MAX)
		return ERR_PTR(-ENAMETOOLONG);

	sbi = autofs4_sbi(dir->i_sb);

	DPRINTK("pid = %u, pgrp = %u, catatonic = %d, oz_mode = %d",
		current->pid, task_pgrp_nr(current), sbi->catatonic, oz_mode);

	active = autofs4_lookup_active(dentry);
	if (active) {
		return active;
	} else {
		d_set_d_op(dentry, &autofs4_root_dentry_operations);

		/*
		 * A dentry that is not within the root can never trigger a
		 * mount operation, unless the directory already exists, so we
		 * can return fail immediately.  The daemon however does need
		 * to create directories within the file system.
		 */
		if (!autofs4_oz_mode(sbi) && !IS_ROOT(dentry->d_parent))
			return ERR_PTR(-ENOENT);

		/* Mark entries in the root as mount triggers */
		if (autofs_type_indirect(sbi->type) && IS_ROOT(dentry->d_parent)) {
			d_set_d_op(dentry, &autofs4_dentry_operations);
			managed_dentry_set_automount(dentry);
		}

		ino = autofs4_init_ino(NULL, sbi, 0555);
		if (!ino)
			return ERR_PTR(-ENOMEM);

		dentry->d_fsdata = ino;
		ino->dentry = dentry;

		autofs4_add_active(dentry);

		d_instantiate(dentry, NULL);
	}
	return NULL;
}

static int autofs4_dir_symlink(struct inode *dir, 
			       struct dentry *dentry,
			       const char *symname)
{
	struct autofs_sb_info *sbi = autofs4_sbi(dir->i_sb);
	struct autofs_info *ino = autofs4_dentry_ino(dentry);
	struct autofs_info *p_ino;
	struct inode *inode;
	char *cp;

	DPRINTK("%s <- %.*s", symname,
		dentry->d_name.len, dentry->d_name.name);

	if (!autofs4_oz_mode(sbi))
		return -EACCES;

	ino = autofs4_init_ino(ino, sbi, S_IFLNK | 0555);
	if (!ino)
		return -ENOMEM;

	autofs4_del_active(dentry);

	ino->size = strlen(symname);
	cp = kmalloc(ino->size + 1, GFP_KERNEL);
	if (!cp) {
		if (!dentry->d_fsdata)
			kfree(ino);
		return -ENOMEM;
	}

	strcpy(cp, symname);

	inode = autofs4_get_inode(dir->i_sb, ino);
	if (!inode) {
		kfree(cp);
		if (!dentry->d_fsdata)
			kfree(ino);
		return -ENOMEM;
	}
	d_add(dentry, inode);

	dentry->d_fsdata = ino;
	ino->dentry = dget(dentry);
	atomic_inc(&ino->count);
	p_ino = autofs4_dentry_ino(dentry->d_parent);
	if (p_ino && dentry->d_parent != dentry)
		atomic_inc(&p_ino->count);
	ino->inode = inode;

	ino->u.symlink = cp;
	dir->i_mtime = CURRENT_TIME;

	return 0;
}

/*
 * NOTE!
 *
 * Normal filesystems would do a "d_delete()" to tell the VFS dcache
 * that the file no longer exists. However, doing that means that the
 * VFS layer can turn the dentry into a negative dentry.  We don't want
 * this, because the unlink is probably the result of an expire.
 * We simply d_drop it and add it to a expiring list in the super block,
 * which allows the dentry lookup to check for an incomplete expire.
 *
 * If a process is blocked on the dentry waiting for the expire to finish,
 * it will invalidate the dentry and try to mount with a new one.
 *
 * Also see autofs4_dir_rmdir()..
 */
static int autofs4_dir_unlink(struct inode *dir, struct dentry *dentry)
{
	struct autofs_sb_info *sbi = autofs4_sbi(dir->i_sb);
	struct autofs_info *ino = autofs4_dentry_ino(dentry);
	struct autofs_info *p_ino;
	
	/* This allows root to remove symlinks */
	if (!autofs4_oz_mode(sbi) && !capable(CAP_SYS_ADMIN))
		return -EACCES;

	if (atomic_dec_and_test(&ino->count)) {
		p_ino = autofs4_dentry_ino(dentry->d_parent);
		if (p_ino && dentry->d_parent != dentry)
			atomic_dec(&p_ino->count);
	}
	dput(ino->dentry);

	dentry->d_inode->i_size = 0;
	clear_nlink(dentry->d_inode);

	dir->i_mtime = CURRENT_TIME;

	spin_lock(&autofs4_lock);
	autofs4_add_expiring(dentry);
	spin_lock(&dentry->d_lock);
	__d_drop(dentry);
	spin_unlock(&dentry->d_lock);
	spin_unlock(&autofs4_lock);

	return 0;
}

static int autofs4_dir_rmdir(struct inode *dir, struct dentry *dentry)
{
	struct autofs_sb_info *sbi = autofs4_sbi(dir->i_sb);
	struct autofs_info *ino = autofs4_dentry_ino(dentry);
	struct autofs_info *p_ino;
	
	DPRINTK("dentry %p, removing %.*s",
		dentry, dentry->d_name.len, dentry->d_name.name);

	if (!autofs4_oz_mode(sbi))
		return -EACCES;

	spin_lock(&autofs4_lock);
	spin_lock(&sbi->lookup_lock);
	spin_lock(&dentry->d_lock);
	if (!list_empty(&dentry->d_subdirs)) {
		spin_unlock(&dentry->d_lock);
		spin_unlock(&sbi->lookup_lock);
		spin_unlock(&autofs4_lock);
		return -ENOTEMPTY;
	}
	__autofs4_add_expiring(dentry);
	spin_unlock(&sbi->lookup_lock);
	__d_drop(dentry);
	spin_unlock(&dentry->d_lock);
	spin_unlock(&autofs4_lock);

	if (atomic_dec_and_test(&ino->count)) {
		p_ino = autofs4_dentry_ino(dentry->d_parent);
		if (p_ino && dentry->d_parent != dentry)
			atomic_dec(&p_ino->count);
	}
	dput(ino->dentry);
	dentry->d_inode->i_size = 0;
	clear_nlink(dentry->d_inode);

	if (dir->i_nlink)
		drop_nlink(dir);

	return 0;
}

static int autofs4_dir_mkdir(struct inode *dir, struct dentry *dentry, int mode)
{
	struct autofs_sb_info *sbi = autofs4_sbi(dir->i_sb);
	struct autofs_info *ino = autofs4_dentry_ino(dentry);
	struct autofs_info *p_ino;
	struct inode *inode;

	if (!autofs4_oz_mode(sbi))
		return -EACCES;

	DPRINTK("dentry %p, creating %.*s",
		dentry, dentry->d_name.len, dentry->d_name.name);

	ino = autofs4_init_ino(ino, sbi, S_IFDIR | 0555);
	if (!ino)
		return -ENOMEM;

	autofs4_del_active(dentry);

	inode = autofs4_get_inode(dir->i_sb, ino);
	if (!inode) {
		if (!dentry->d_fsdata)
			kfree(ino);
		return -ENOMEM;
	}
	d_add(dentry, inode);

	dentry->d_fsdata = ino;
	ino->dentry = dget(dentry);
	atomic_inc(&ino->count);
	p_ino = autofs4_dentry_ino(dentry->d_parent);
	if (p_ino && dentry->d_parent != dentry)
		atomic_inc(&p_ino->count);
	ino->inode = inode;
	inc_nlink(dir);
	dir->i_mtime = CURRENT_TIME;

	return 0;
}

/* Get/set timeout ioctl() operation */
#ifdef CONFIG_COMPAT
static inline int autofs4_compat_get_set_timeout(struct autofs_sb_info *sbi,
					 compat_ulong_t __user *p)
{
	int rv;
	unsigned long ntimeout;

	if ((rv = get_user(ntimeout, p)) ||
	     (rv = put_user(sbi->exp_timeout/HZ, p)))
		return rv;

	if (ntimeout > UINT_MAX/HZ)
		sbi->exp_timeout = 0;
	else
		sbi->exp_timeout = ntimeout * HZ;

	return 0;
}
#endif

static inline int autofs4_get_set_timeout(struct autofs_sb_info *sbi,
					 unsigned long __user *p)
{
	int rv;
	unsigned long ntimeout;

	if ((rv = get_user(ntimeout, p)) ||
	     (rv = put_user(sbi->exp_timeout/HZ, p)))
		return rv;

	if (ntimeout > ULONG_MAX/HZ)
		sbi->exp_timeout = 0;
	else
		sbi->exp_timeout = ntimeout * HZ;

	return 0;
}

/* Return protocol version */
static inline int autofs4_get_protover(struct autofs_sb_info *sbi, int __user *p)
{
	return put_user(sbi->version, p);
}

/* Return protocol sub version */
static inline int autofs4_get_protosubver(struct autofs_sb_info *sbi, int __user *p)
{
	return put_user(sbi->sub_version, p);
}

/*
* Tells the daemon whether it can umount the autofs mount.
*/
static inline int autofs4_ask_umount(struct vfsmount *mnt, int __user *p)
{
	int status = 0;

	if (may_umount(mnt))
		status = 1;

	DPRINTK("returning %d", status);

	status = put_user(status, p);

	return status;
}

/* Identify autofs4_dentries - this is so we can tell if there's
   an extra dentry refcount or not.  We only hold a refcount on the
   dentry if its non-negative (ie, d_inode != NULL)
*/
int is_autofs4_dentry(struct dentry *dentry)
{
	return dentry && dentry->d_inode &&
		(dentry->d_op == &autofs4_root_dentry_operations ||
		 dentry->d_op == &autofs4_dentry_operations) &&
		dentry->d_fsdata != NULL;
}

/*
 * ioctl()'s on the root directory is the chief method for the daemon to
 * generate kernel reactions
 */
static int autofs4_root_ioctl_unlocked(struct inode *inode, struct file *filp,
				       unsigned int cmd, unsigned long arg)
{
	struct autofs_sb_info *sbi = autofs4_sbi(inode->i_sb);
	void __user *p = (void __user *)arg;

	DPRINTK("cmd = 0x%08x, arg = 0x%08lx, sbi = %p, pgrp = %u",
		cmd,arg,sbi,task_pgrp_nr(current));

	if (_IOC_TYPE(cmd) != _IOC_TYPE(AUTOFS_IOC_FIRST) ||
	     _IOC_NR(cmd) - _IOC_NR(AUTOFS_IOC_FIRST) >= AUTOFS_IOC_COUNT)
		return -ENOTTY;
	
	if (!autofs4_oz_mode(sbi) && !capable(CAP_SYS_ADMIN))
		return -EPERM;
	
	switch(cmd) {
	case AUTOFS_IOC_READY:	/* Wait queue: go ahead and retry */
		return autofs4_wait_release(sbi,(autofs_wqt_t)arg,0);
	case AUTOFS_IOC_FAIL:	/* Wait queue: fail with ENOENT */
		return autofs4_wait_release(sbi,(autofs_wqt_t)arg,-ENOENT);
	case AUTOFS_IOC_CATATONIC: /* Enter catatonic mode (daemon shutdown) */
		autofs4_catatonic_mode(sbi);
		return 0;
	case AUTOFS_IOC_PROTOVER: /* Get protocol version */
		return autofs4_get_protover(sbi, p);
	case AUTOFS_IOC_PROTOSUBVER: /* Get protocol sub version */
		return autofs4_get_protosubver(sbi, p);
	case AUTOFS_IOC_SETTIMEOUT:
		return autofs4_get_set_timeout(sbi, p);
#ifdef CONFIG_COMPAT
	case AUTOFS_IOC_SETTIMEOUT32:
		return autofs4_compat_get_set_timeout(sbi, p);
#endif

	case AUTOFS_IOC_ASKUMOUNT:
		return autofs4_ask_umount(filp->f_path.mnt, p);

	/* return a single thing to expire */
	case AUTOFS_IOC_EXPIRE:
		return autofs4_expire_run(inode->i_sb,filp->f_path.mnt,sbi, p);
	/* same as above, but can send multiple expires through pipe */
	case AUTOFS_IOC_EXPIRE_MULTI:
		return autofs4_expire_multi(inode->i_sb,filp->f_path.mnt,sbi, p);

	default:
		return -ENOSYS;
	}
}

static long autofs4_root_ioctl(struct file *filp,
			       unsigned int cmd, unsigned long arg)
{
	struct inode *inode = filp->f_dentry->d_inode;
	return autofs4_root_ioctl_unlocked(inode, filp, cmd, arg);
}

#ifdef CONFIG_COMPAT
static long autofs4_root_compat_ioctl(struct file *filp,
			     unsigned int cmd, unsigned long arg)
{
	struct inode *inode = filp->f_path.dentry->d_inode;
	int ret;

	if (cmd == AUTOFS_IOC_READY || cmd == AUTOFS_IOC_FAIL)
		ret = autofs4_root_ioctl_unlocked(inode, filp, cmd, arg);
	else
		ret = autofs4_root_ioctl_unlocked(inode, filp, cmd,
			(unsigned long)compat_ptr(arg));

	return ret;
}
#endif
