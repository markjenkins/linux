/*
 *	fs/bfs/inode.c
 *	BFS superblock and inode operations.
 *	Copyright (C) 1999,2000 Tigran Aivazian <tigran@veritas.com>
 *	From fs/minix, Copyright (C) 1991, 1992 Linus Torvalds.
 */

#include <linux/module.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/smp_lock.h>
#include <linux/buffer_head.h>
#include <linux/vfs.h>
#include <asm/uaccess.h>
#include "bfs.h"

MODULE_AUTHOR("Tigran A. Aivazian <tigran@veritas.com>");
MODULE_DESCRIPTION("SCO UnixWare BFS filesystem for Linux");
MODULE_LICENSE("GPL");

#undef DEBUG

#ifdef DEBUG
#define dprintf(x...)	printf(x)
#else
#define dprintf(x...)
#endif

void dump_imap(const char *prefix, struct super_block * s);

static void bfs_read_inode(struct inode * inode)
{
	unsigned long ino = inode->i_ino;
	struct bfs_inode * di;
	struct buffer_head * bh;
	int block, off;

	if (ino < BFS_ROOT_INO || ino > BFS_SB(inode->i_sb)->si_lasti) {
		printf("Bad inode number %s:%08lx\n", inode->i_sb->s_id, ino);
		make_bad_inode(inode);
		return;
	}

	block = (ino - BFS_ROOT_INO)/BFS_INODES_PER_BLOCK + 1;
	bh = sb_bread(inode->i_sb, block);
	if (!bh) {
		printf("Unable to read inode %s:%08lx\n", inode->i_sb->s_id, ino);
		make_bad_inode(inode);
		return;
	}

	off = (ino - BFS_ROOT_INO) % BFS_INODES_PER_BLOCK;
	di = (struct bfs_inode *)bh->b_data + off;

	inode->i_mode = 0x0000FFFF & di->i_mode;
	if (di->i_vtype == BFS_VDIR) {
		inode->i_mode |= S_IFDIR;
		inode->i_op = &bfs_dir_inops;
		inode->i_fop = &bfs_dir_operations;
	} else if (di->i_vtype == BFS_VREG) {
		inode->i_mode |= S_IFREG;
		inode->i_op = &bfs_file_inops;
		inode->i_fop = &bfs_file_operations;
		inode->i_mapping->a_ops = &bfs_aops;
	}

	inode->i_uid = di->i_uid;
	inode->i_gid = di->i_gid;
	inode->i_nlink = di->i_nlink;
	inode->i_size = BFS_FILESIZE(di);
	inode->i_blocks = BFS_FILEBLOCKS(di);
	inode->i_blksize = PAGE_SIZE;
	inode->i_atime.tv_sec = di->i_atime;
	inode->i_mtime.tv_sec = di->i_mtime;
	inode->i_ctime.tv_sec = di->i_ctime;
	inode->i_atime.tv_nsec = 0;
	inode->i_mtime.tv_nsec = 0;
	inode->i_ctime.tv_nsec = 0;
	BFS_I(inode)->i_dsk_ino = di->i_ino; /* can be 0 so we store a copy */
	BFS_I(inode)->i_sblock = di->i_sblock;
	BFS_I(inode)->i_eblock = di->i_eblock;

	brelse(bh);
}

static void bfs_write_inode(struct inode * inode, int unused)
{
	unsigned long ino = inode->i_ino;
	struct bfs_inode * di;
	struct buffer_head * bh;
	int block, off;

	if (ino < BFS_ROOT_INO || ino > BFS_SB(inode->i_sb)->si_lasti) {
		printf("Bad inode number %s:%08lx\n", inode->i_sb->s_id, ino);
		return;
	}

	lock_kernel();
	block = (ino - BFS_ROOT_INO)/BFS_INODES_PER_BLOCK + 1;
	bh = sb_bread(inode->i_sb, block);
	if (!bh) {
		printf("Unable to read inode %s:%08lx\n", inode->i_sb->s_id, ino);
		unlock_kernel();
		return;
	}

	off = (ino - BFS_ROOT_INO)%BFS_INODES_PER_BLOCK;
	di = (struct bfs_inode *)bh->b_data + off;

	if (inode->i_ino == BFS_ROOT_INO)
		di->i_vtype = BFS_VDIR;
	else
		di->i_vtype = BFS_VREG;

	di->i_ino = inode->i_ino;
	di->i_mode = inode->i_mode;
	di->i_uid = inode->i_uid;
	di->i_gid = inode->i_gid;
	di->i_nlink = inode->i_nlink;
	di->i_atime = inode->i_atime.tv_sec;
	di->i_mtime = inode->i_mtime.tv_sec;
	di->i_ctime = inode->i_ctime.tv_sec;
	di->i_sblock = BFS_I(inode)->i_sblock;
	di->i_eblock = BFS_I(inode)->i_eblock;
	di->i_eoffset = di->i_sblock * BFS_BSIZE + inode->i_size - 1;

	mark_buffer_dirty(bh);
	brelse(bh);
	unlock_kernel();
}

static void bfs_delete_inode(struct inode * inode)
{
	unsigned long ino = inode->i_ino;
	struct bfs_inode * di;
	struct buffer_head * bh;
	int block, off;
	struct super_block * s = inode->i_sb;
	struct bfs_sb_info * info = BFS_SB(s);

	dprintf("ino=%08lx\n", inode->i_ino);

	if (inode->i_ino < BFS_ROOT_INO || inode->i_ino > info->si_lasti) {
		printf("invalid ino=%08lx\n", inode->i_ino);
		return;
	}
	
	inode->i_size = 0;
	inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
	lock_kernel();
	mark_inode_dirty(inode);
	block = (ino - BFS_ROOT_INO)/BFS_INODES_PER_BLOCK + 1;
	bh = sb_bread(s, block);
	if (!bh) {
		printf("Unable to read inode %s:%08lx\n", inode->i_sb->s_id, ino);
		unlock_kernel();
		return;
	}
	off = (ino - BFS_ROOT_INO)%BFS_INODES_PER_BLOCK;
	di = (struct bfs_inode *)bh->b_data + off;
	if (di->i_ino) {
		info->si_freeb += BFS_FILEBLOCKS(di);
		info->si_freei++;
		clear_bit(di->i_ino, info->si_imap);
		dump_imap("delete_inode", s);
	}
	di->i_ino = 0;
	di->i_sblock = 0;
	mark_buffer_dirty(bh);
	brelse(bh);

	/* if this was the last file, make the previous 
	   block "last files last block" even if there is no real file there,
	   saves us 1 gap */
	if (info->si_lf_eblk == BFS_I(inode)->i_eblock) {
		info->si_lf_eblk = BFS_I(inode)->i_sblock - 1;
		mark_buffer_dirty(info->si_sbh);
	}
	unlock_kernel();
	clear_inode(inode);
}

static void bfs_put_super(struct super_block *s)
{
	struct bfs_sb_info *info = BFS_SB(s);
	brelse(info->si_sbh);
	kfree(info->si_imap);
	kfree(info);
	s->s_fs_info = NULL;
}

static int bfs_statfs(struct super_block *s, struct kstatfs *buf)
{
	struct bfs_sb_info *info = BFS_SB(s);
	buf->f_type = BFS_MAGIC;
	buf->f_bsize = s->s_blocksize;
	buf->f_blocks = info->si_blocks;
	buf->f_bfree = buf->f_bavail = info->si_freeb;
	buf->f_files = info->si_lasti + 1 - BFS_ROOT_INO;
	buf->f_ffree = info->si_freei;
	buf->f_fsid.val[0] = s->s_dev;
	buf->f_namelen = BFS_NAMELEN;
	return 0;
}

static void bfs_write_super(struct super_block *s)
{
	lock_kernel();
	if (!(s->s_flags & MS_RDONLY))
		mark_buffer_dirty(BFS_SB(s)->si_sbh);
	s->s_dirt = 0;
	unlock_kernel();
}

static kmem_cache_t * bfs_inode_cachep;

static struct inode *bfs_alloc_inode(struct super_block *sb)
{
	struct bfs_inode_info *bi;
	bi = kmem_cache_alloc(bfs_inode_cachep, SLAB_KERNEL);
	if (!bi)
		return NULL;
	return &bi->vfs_inode;
}

static void bfs_destroy_inode(struct inode *inode)
{
	kmem_cache_free(bfs_inode_cachep, BFS_I(inode));
}

static void init_once(void * foo, kmem_cache_t * cachep, unsigned long flags)
{
	struct bfs_inode_info *bi = foo;

	if ((flags & (SLAB_CTOR_VERIFY|SLAB_CTOR_CONSTRUCTOR)) ==
	    SLAB_CTOR_CONSTRUCTOR)
		inode_init_once(&bi->vfs_inode);
}
 
static int init_inodecache(void)
{
	bfs_inode_cachep = kmem_cache_create("bfs_inode_cache",
					     sizeof(struct bfs_inode_info),
					     0, SLAB_HWCACHE_ALIGN|SLAB_RECLAIM_ACCOUNT,
					     init_once, NULL);
	if (bfs_inode_cachep == NULL)
		return -ENOMEM;
	return 0;
}

static void destroy_inodecache(void)
{
	if (kmem_cache_destroy(bfs_inode_cachep))
		printk(KERN_INFO "bfs_inode_cache: not all structures were freed\n");
}

static struct super_operations bfs_sops = {
	.alloc_inode	= bfs_alloc_inode,
	.destroy_inode	= bfs_destroy_inode,
	.read_inode	= bfs_read_inode,
	.write_inode	= bfs_write_inode,
	.delete_inode	= bfs_delete_inode,
	.put_super	= bfs_put_super,
	.write_super	= bfs_write_super,
	.statfs		= bfs_statfs,
};

void dump_imap(const char *prefix, struct super_block * s)
{
#if 0
	int i;
	char *tmpbuf = (char *)get_zeroed_page(GFP_KERNEL);

	if (!tmpbuf)
		return;
	for (i=BFS_SB(s)->si_lasti; i>=0; i--) {
		if (i>PAGE_SIZE-100) break;
		if (test_bit(i, BFS_SB(s)->si_imap))
			strcat(tmpbuf, "1");
		else
			strcat(tmpbuf, "0");
	}
	printk(KERN_ERR "BFS-fs: %s: lasti=%08lx <%s>\n", prefix, BFS_SB(s)->si_lasti, tmpbuf);
	free_page((unsigned long)tmpbuf);
#endif
}

static int bfs_fill_super(struct super_block *s, void *data, int silent)
{
	struct buffer_head * bh;
	struct bfs_super_block * bfs_sb;
	struct inode * inode;
	int i, imap_len;
	struct bfs_sb_info * info;

	info = kmalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;
	s->s_fs_info = info;
	memset(info, 0, sizeof(*info));

	sb_set_blocksize(s, BFS_BSIZE);

	bh = sb_bread(s, 0);
	if(!bh)
		goto out;
	bfs_sb = (struct bfs_super_block *)bh->b_data;
	if (bfs_sb->s_magic != BFS_MAGIC) {
		if (!silent)
			printf("No BFS filesystem on %s (magic=%08x)\n", 
				s->s_id, bfs_sb->s_magic);
		goto out;
	}
	if (BFS_UNCLEAN(bfs_sb, s) && !silent)
		printf("%s is unclean, continuing\n", s->s_id);

	s->s_magic = BFS_MAGIC;
	info->si_bfs_sb = bfs_sb;
	info->si_sbh = bh;
	info->si_lasti = (bfs_sb->s_start - BFS_BSIZE)/sizeof(struct bfs_inode) 
			+ BFS_ROOT_INO - 1;

	imap_len = info->si_lasti/8 + 1;
	info->si_imap = kmalloc(imap_len, GFP_KERNEL);
	if (!info->si_imap)
		goto out;
	memset(info->si_imap, 0, imap_len);
	for (i=0; i<BFS_ROOT_INO; i++) 
		set_bit(i, info->si_imap);

	s->s_op = &bfs_sops;
	inode = iget(s, BFS_ROOT_INO);
	if (!inode) {
		kfree(info->si_imap);
		goto out;
	}
	s->s_root = d_alloc_root(inode);
	if (!s->s_root) {
		iput(inode);
		kfree(info->si_imap);
		goto out;
	}

	info->si_blocks = (bfs_sb->s_end + 1)>>BFS_BSIZE_BITS; /* for statfs(2) */
	info->si_freeb = (bfs_sb->s_end + 1 - bfs_sb->s_start)>>BFS_BSIZE_BITS;
	info->si_freei = 0;
	info->si_lf_eblk = 0;
	info->si_lf_sblk = 0;
	info->si_lf_ioff = 0;
	for (i=BFS_ROOT_INO; i<=info->si_lasti; i++) {
		inode = iget(s,i);
		if (BFS_I(inode)->i_dsk_ino == 0)
			info->si_freei++;
		else {
			set_bit(i, info->si_imap);
			info->si_freeb -= inode->i_blocks;
			if (BFS_I(inode)->i_eblock > info->si_lf_eblk) {
				info->si_lf_eblk = BFS_I(inode)->i_eblock;
				info->si_lf_sblk = BFS_I(inode)->i_sblock;
				info->si_lf_ioff = BFS_INO2OFF(i);
			}
		}
		iput(inode);
	}
	if (!(s->s_flags & MS_RDONLY)) {
		mark_buffer_dirty(bh);
		s->s_dirt = 1;
	} 
	dump_imap("read_super", s);
	return 0;

out:
	brelse(bh);
	kfree(info);
	s->s_fs_info = NULL;
	return -EINVAL;
}

static struct super_block *bfs_get_sb(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data)
{
	return get_sb_bdev(fs_type, flags, dev_name, data, bfs_fill_super);
}

static struct file_system_type bfs_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "bfs",
	.get_sb		= bfs_get_sb,
	.kill_sb	= kill_block_super,
	.fs_flags	= FS_REQUIRES_DEV,
};

static int __init init_bfs_fs(void)
{
	int err = init_inodecache();
	if (err)
		goto out1;
        err = register_filesystem(&bfs_fs_type);
	if (err)
		goto out;
	return 0;
out:
	destroy_inodecache();
out1:
	return err;
}

static void __exit exit_bfs_fs(void)
{
	unregister_filesystem(&bfs_fs_type);
	destroy_inodecache();
}

module_init(init_bfs_fs)
module_exit(exit_bfs_fs)
