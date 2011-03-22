/*
 *  linux/fs/9p/vfs_super.c
 *
 * This file contians superblock ops for 9P2000. It is intended that
 * you mount this file system on directories.
 *
 *  Copyright (C) 2004 by Eric Van Hensbergen <ericvh@gmail.com>
 *  Copyright (C) 2002 by Ron Minnich <rminnich@lanl.gov>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2
 *  as published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to:
 *  Free Software Foundation
 *  51 Franklin Street, Fifth Floor
 *  Boston, MA  02111-1301  USA
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/inet.h>
#include <linux/pagemap.h>
#include <linux/seq_file.h>
#include <linux/mount.h>
#include <linux/idr.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/statfs.h>
#include <linux/magic.h>
#include <net/9p/9p.h>
#include <net/9p/client.h>

#include "v9fs.h"
#include "v9fs_vfs.h"
#include "fid.h"
#include "xattr.h"
#include "acl.h"

static const struct super_operations v9fs_super_ops, v9fs_super_ops_dotl;

/**
 * v9fs_set_super - set the superblock
 * @s: super block
 * @data: file system specific data
 *
 */

static int v9fs_set_super(struct super_block *s, void *data)
{
	s->s_fs_info = data;
	return set_anon_super(s, data);
}

/**
 * v9fs_fill_super - populate superblock with info
 * @sb: superblock
 * @v9ses: session information
 * @flags: flags propagated from v9fs_mount()
 *
 */

static void
v9fs_fill_super(struct super_block *sb, struct v9fs_session_info *v9ses,
		int flags, void *data)
{
	sb->s_maxbytes = MAX_LFS_FILESIZE;
	sb->s_blocksize_bits = fls(v9ses->maxdata - 1);
	sb->s_blocksize = 1 << sb->s_blocksize_bits;
	sb->s_magic = V9FS_MAGIC;
	if (v9fs_proto_dotl(v9ses)) {
		sb->s_op = &v9fs_super_ops_dotl;
		sb->s_xattr = v9fs_xattr_handlers;
	} else
		sb->s_op = &v9fs_super_ops;
	sb->s_bdi = &v9ses->bdi;
	if (v9ses->cache)
		sb->s_bdi->ra_pages = (VM_MAX_READAHEAD * 1024)/PAGE_CACHE_SIZE;

	sb->s_flags = flags | MS_ACTIVE | MS_DIRSYNC | MS_NOATIME;
	if (!v9ses->cache)
		sb->s_flags |= MS_SYNCHRONOUS;

#ifdef CONFIG_9P_FS_POSIX_ACL
	if ((v9ses->flags & V9FS_ACL_MASK) == V9FS_POSIX_ACL)
		sb->s_flags |= MS_POSIXACL;
#endif

	save_mount_options(sb, data);
}

/**
 * v9fs_mount - mount a superblock
 * @fs_type: file system type
 * @flags: mount flags
 * @dev_name: device name that was mounted
 * @data: mount options
 *
 */

static struct dentry *v9fs_mount(struct file_system_type *fs_type, int flags,
		       const char *dev_name, void *data)
{
	struct super_block *sb = NULL;
	struct inode *inode = NULL;
	struct dentry *root = NULL;
	struct v9fs_session_info *v9ses = NULL;
	int mode = S_IRWXUGO | S_ISVTX;
	struct p9_fid *fid;
	int retval = 0;

	P9_DPRINTK(P9_DEBUG_VFS, " \n");

	v9ses = kzalloc(sizeof(struct v9fs_session_info), GFP_KERNEL);
	if (!v9ses)
		return ERR_PTR(-ENOMEM);

	fid = v9fs_session_init(v9ses, dev_name, data);
	if (IS_ERR(fid)) {
		retval = PTR_ERR(fid);
		/*
		 * we need to call session_close to tear down some
		 * of the data structure setup by session_init
		 */
		goto close_session;
	}

	sb = sget(fs_type, NULL, v9fs_set_super, v9ses);
	if (IS_ERR(sb)) {
		retval = PTR_ERR(sb);
		goto clunk_fid;
	}
	v9fs_fill_super(sb, v9ses, flags, data);

	if (v9ses->cache)
		sb->s_d_op = &v9fs_cached_dentry_operations;
	else
		sb->s_d_op = &v9fs_dentry_operations;

	inode = v9fs_get_inode(sb, S_IFDIR | mode);
	if (IS_ERR(inode)) {
		retval = PTR_ERR(inode);
		goto release_sb;
	}
	root = d_alloc_root(inode);
	if (!root) {
		iput(inode);
		retval = -ENOMEM;
		goto release_sb;
	}
	sb->s_root = root;
	if (v9fs_proto_dotl(v9ses)) {
		struct p9_stat_dotl *st = NULL;
		st = p9_client_getattr_dotl(fid, P9_STATS_BASIC);
		if (IS_ERR(st)) {
			retval = PTR_ERR(st);
			goto release_sb;
		}
		root->d_inode->i_ino = v9fs_qid2ino(&st->qid);
		v9fs_stat2inode_dotl(st, root->d_inode);
		kfree(st);
	} else {
		struct p9_wstat *st = NULL;
		st = p9_client_stat(fid);
		if (IS_ERR(st)) {
			retval = PTR_ERR(st);
			goto release_sb;
		}

		root->d_inode->i_ino = v9fs_qid2ino(&st->qid);
		v9fs_stat2inode(st, root->d_inode, sb);

		p9stat_free(st);
		kfree(st);
	}
	v9fs_fid_add(root, fid);
	retval = v9fs_get_acl(inode, fid);
	if (retval)
		goto release_sb;
	/*
	 * Add the root fid to session info. This is used
	 * for file system sync. We want a cloned fid here
	 * so that we can do a sync_filesystem after a
	 * shrink_dcache_for_umount
	 */
	v9ses->root_fid = v9fs_fid_clone(root);
	if (IS_ERR(v9ses->root_fid)) {
		retval = PTR_ERR(v9ses->root_fid);
		goto release_sb;
	}

	P9_DPRINTK(P9_DEBUG_VFS, " simple set mount, return 0\n");
	return dget(sb->s_root);

clunk_fid:
	p9_client_clunk(fid);
close_session:
	v9fs_session_close(v9ses);
	kfree(v9ses);
	return ERR_PTR(retval);
release_sb:
	/*
	 * we will do the session_close and root dentry
	 * release in the below call.
	 */
	deactivate_locked_super(sb);
	return ERR_PTR(retval);
}

/**
 * v9fs_kill_super - Kill Superblock
 * @s: superblock
 *
 */

static void v9fs_kill_super(struct super_block *s)
{
	struct v9fs_session_info *v9ses = s->s_fs_info;

	P9_DPRINTK(P9_DEBUG_VFS, " %p\n", s);

	kill_anon_super(s);
	p9_client_clunk(v9ses->root_fid);
	v9fs_session_cancel(v9ses);
	v9fs_session_close(v9ses);
	kfree(v9ses);
	s->s_fs_info = NULL;
	P9_DPRINTK(P9_DEBUG_VFS, "exiting kill_super\n");
}

static void
v9fs_umount_begin(struct super_block *sb)
{
	struct v9fs_session_info *v9ses;

	v9ses = sb->s_fs_info;
	v9fs_session_begin_cancel(v9ses);
}

static int v9fs_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	struct v9fs_session_info *v9ses;
	struct p9_fid *fid;
	struct p9_rstatfs rs;
	int res;

	fid = v9fs_fid_lookup(dentry);
	if (IS_ERR(fid)) {
		res = PTR_ERR(fid);
		goto done;
	}

	v9ses = v9fs_dentry2v9ses(dentry);
	if (v9fs_proto_dotl(v9ses)) {
		res = p9_client_statfs(fid, &rs);
		if (res == 0) {
			buf->f_type = V9FS_MAGIC;
			buf->f_bsize = rs.bsize;
			buf->f_blocks = rs.blocks;
			buf->f_bfree = rs.bfree;
			buf->f_bavail = rs.bavail;
			buf->f_files = rs.files;
			buf->f_ffree = rs.ffree;
			buf->f_fsid.val[0] = rs.fsid & 0xFFFFFFFFUL;
			buf->f_fsid.val[1] = (rs.fsid >> 32) & 0xFFFFFFFFUL;
			buf->f_namelen = rs.namelen;
		}
		if (res != -ENOSYS)
			goto done;
	}
	res = simple_statfs(dentry, buf);
done:
	return res;
}

static int v9fs_sync_fs(struct super_block *sb, int wait)
{
	struct v9fs_session_info *v9ses = sb->s_fs_info;

	P9_DPRINTK(P9_DEBUG_VFS, "v9fs_sync_fs: super_block %p\n", sb);
	return p9_client_sync_fs(v9ses->root_fid);
}

static int v9fs_drop_inode(struct inode *inode)
{
	struct v9fs_session_info *v9ses;
	v9ses = v9fs_inode2v9ses(inode);
	if (v9ses->cache)
		return generic_drop_inode(inode);
	/*
	 * in case of non cached mode always drop the
	 * the inode because we want the inode attribute
	 * to always match that on the server.
	 */
	return 1;
}

static const struct super_operations v9fs_super_ops = {
	.alloc_inode = v9fs_alloc_inode,
	.destroy_inode = v9fs_destroy_inode,
	.statfs = simple_statfs,
	.evict_inode = v9fs_evict_inode,
	.show_options = generic_show_options,
	.umount_begin = v9fs_umount_begin,
};

static const struct super_operations v9fs_super_ops_dotl = {
	.alloc_inode = v9fs_alloc_inode,
	.destroy_inode = v9fs_destroy_inode,
	.sync_fs = v9fs_sync_fs,
	.statfs = v9fs_statfs,
	.drop_inode = v9fs_drop_inode,
	.evict_inode = v9fs_evict_inode,
	.show_options = generic_show_options,
	.umount_begin = v9fs_umount_begin,
};

struct file_system_type v9fs_fs_type = {
	.name = "9p",
	.mount = v9fs_mount,
	.kill_sb = v9fs_kill_super,
	.owner = THIS_MODULE,
	.fs_flags = FS_RENAME_DOES_D_MOVE|FS_REVAL_DOT,
};
