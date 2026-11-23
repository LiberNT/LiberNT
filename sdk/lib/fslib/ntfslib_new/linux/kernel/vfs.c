// SPDX-License-Identifier: MIT
/*
 * PROJECT:     LiberNT NTFS library
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Linux VFS glue for the NTFS core
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/fs_context.h>
#include <linux/iversion.h>
#include <linux/buffer_head.h>
#include <linux/hash.h>
#include <linux/iomap.h>
#include <linux/blkdev.h>
#include <linux/pagemap.h>
#include <linux/slab.h>
#include <linux/statfs.h>
#include <linux/uio.h>
#include <linux/mm.h>
#include <linux/sched/mm.h>
#include <linux/vmalloc.h>
#include <linux/writeback.h>

#include "ntfslib_k.h"

#define NK_MAGIC 0x5346544e
#define NK_TICKS_TO_1970 116444736000000000ULL
#define NK_WRITE_CHUNK (1024u * 1024u)
#define NK_LIST_BYTES (64u * 1024u)
#define NK_LIST_LARGE (1024u * 1024u)
#define NK_RECENT_BITS 8

#define NK_READ_AHEAD 64
#define NK_LIST_RUNS 32
#define NK_RESIDENT_LIMIT 512

struct nk_sb {
	struct nk_volume *volume;
	u64 root;
	kuid_t uid;
	kgid_t gid;
	struct file_ra_state ra;
	sector_t blocks;
	/* The core put file data in the device's own cache: write it out before a file is read block by block. */
	bool core_wrote_data;
	/* The last listing's name buffer, for the next listing to take instead of allocating one. */
	char *list_buffer;
	/* No file that cannot hold holes grows past this. */
	u64 volume_bytes;
	/* The blocks the core touched last, held under the lock so that it finds them again without a search. */
	struct buffer_head *recent[1u << NK_RECENT_BITS];
};

struct nk_inode {
	/* Plain clusters, read and written through the page cache; otherwise the core does every byte. */
	bool mapped;
	bool empty;
	bool removed;
	bool holes;
	/* The record is a directory, whatever the inode shows it as: a junction is a link here. */
	bool directory_record;
	/*
	 * How far the clusters were ever written; beyond it they hold whatever was there before and read
	 * as zero. Only write-back moves it forward, under valid_lock, after zeroing what it skips.
	 */
	loff_t valid;
	struct mutex valid_lock;
	/* A size change and the store of the size the page cache has reached exclude each other. */
	struct mutex size_lock;
	u64 allocated;
	/* The last run of clusters looked up: file bytes [run_start, run_end) begin at device byte run_disk. */
	spinlock_t run_lock;
	u64 run_start;
	u64 run_end;
	u64 run_disk;
	/* Moves when the runs change, so that a lookup that began before does not leave its answer behind. */
	u64 run_changes;
	/* A directory: counts the changes to its names. */
	u64 changes;
	/* One more than the count at which a whole listing found or made a cached name for every entry. */
	u64 primed;
	struct inode vfs;
};

static struct kmem_cache *nk_inode_cache;

static struct nk_inode *NK_I(struct inode *inode)
{
	return container_of(inode, struct nk_inode, vfs);
}

static struct nk_sb *nk_sbi(struct super_block *sb)
{
	return sb->s_fs_info;
}

/* The core is single threaded: one lock around every call into it. */
static DEFINE_MUTEX(nk_lock);

static unsigned int nk_enter(void)
{
	mutex_lock(&nk_lock);
	return memalloc_nofs_save();
}

static void nk_leave(unsigned int flags)
{
	memalloc_nofs_restore(flags);
	mutex_unlock(&nk_lock);
}

static struct nk_volume *nk_vol(struct super_block *sb)
{
	return ((struct nk_sb *)sb->s_fs_info)->volume;
}

static u64 nk_ref(const struct inode *inode)
{
	return ((u64)inode->i_generation << 48) | inode->i_ino;
}

/*
 * The core allocates and frees scratch buffers on every operation, always under nk_lock. Freed blocks
 * are kept by size class and handed out again, which spares the allocator and the zeroing of fresh pages.
 */
#define NK_CLASS_MIN 6
#define NK_CLASS_MAX 17
#define NK_POOL_DEPTH 32
#define NK_UNPOOLED 0xFF

struct nk_block {
	u8 class;
	u8 unused[15];
};

static void *nk_pool[NK_CLASS_MAX + 1][NK_POOL_DEPTH];
static unsigned int nk_pooled[NK_CLASS_MAX + 1];

void *nk_alloc(unsigned long size)
{
	unsigned long total = size + sizeof(struct nk_block);
	unsigned int class = total <= (1UL << NK_CLASS_MIN) ? NK_CLASS_MIN : fls_long(total - 1);
	struct nk_block *block;

	if (class > NK_CLASS_MAX) {
		block = kvmalloc(total, GFP_NOFS);
		class = NK_UNPOOLED;
	} else if (nk_pooled[class]) {
		block = nk_pool[class][--nk_pooled[class]];
	} else {
		block = kvmalloc(1UL << class, GFP_NOFS);
	}
	if (!block)
		return NULL;
	block->class = class;
	return block + 1;
}

void nk_free(void *memory)
{
	struct nk_block *block;

	if (!memory)
		return;
	block = (struct nk_block *)memory - 1;
	if (block->class != NK_UNPOOLED && nk_pooled[block->class] < NK_POOL_DEPTH)
		nk_pool[block->class][nk_pooled[block->class]++] = block;
	else
		kvfree(block);
}

static void nk_drain_pool(void)
{
	unsigned int class;

	for (class = 0; class <= NK_CLASS_MAX; class++) {
		while (nk_pooled[class])
			kvfree(nk_pool[class][--nk_pooled[class]]);
	}
}

/* Starts reading device blocks without waiting for them. */
static void nk_read_ahead(struct super_block *sb, sector_t block, unsigned long count)
{
	struct nk_sb *sbi = sb->s_fs_info;
	DEFINE_READAHEAD(ractl, NULL, &sbi->ra, sb->s_bdev->bd_mapping, block);

	if (block >= sbi->blocks)
		return;
	page_cache_ra_unbounded(&ractl, min_t(sector_t, count, sbi->blocks - block), 0);
}

/* Metadata is read a block at a time: a miss brings its neighbours along in one request. */
static struct buffer_head *nk_bread(struct super_block *sb, sector_t block)
{
	struct buffer_head *bh = sb_getblk(sb, block);

	if (bh && buffer_uptodate(bh))
		return bh;
	brelse(bh);
	nk_read_ahead(sb, block, NK_READ_AHEAD);
	return sb_bread(sb, block);
}

/* The block for the core to read, or to overwrite as a whole; the table keeps the reference. */
static struct buffer_head *nk_block(struct super_block *sb, sector_t block, bool whole)
{
	struct nk_sb *sbi = sb->s_fs_info;
	struct buffer_head **slot = &sbi->recent[hash_64(block, NK_RECENT_BITS)];
	struct buffer_head *bh = *slot;

	lockdep_assert_held(&nk_lock);
	if (bh && bh->b_blocknr == block && (whole || buffer_uptodate(bh)))
		return bh;
	bh = whole ? sb_getblk(sb, block) : nk_bread(sb, block);
	if (!bh)
		return NULL;
	brelse(*slot);
	*slot = bh;
	return bh;
}

static void nk_forget_blocks(struct nk_sb *sbi)
{
	unsigned int index;

	for (index = 0; index < ARRAY_SIZE(sbi->recent); index++) {
		brelse(sbi->recent[index]);
		sbi->recent[index] = NULL;
	}
}

int nk_dev_read(void *device, nk_u64 offset, nk_u32 length, void *buffer)
{
	struct super_block *sb = device;

	while (length) {
		unsigned int within = offset & (sb->s_blocksize - 1);
		unsigned int chunk = min_t(unsigned int, length, sb->s_blocksize - within);
		struct buffer_head *bh = nk_block(sb, offset >> sb->s_blocksize_bits, false);

		if (!bh)
			return -EIO;
		memcpy(buffer, bh->b_data + within, chunk);
		buffer += chunk;
		offset += chunk;
		length -= chunk;
	}
	return 0;
}

int nk_dev_write(void *device, nk_u64 offset, nk_u32 length, const void *buffer)
{
	struct super_block *sb = device;

	while (length) {
		unsigned int within = offset & (sb->s_blocksize - 1);
		unsigned int chunk = min_t(unsigned int, length, sb->s_blocksize - within);
		sector_t block = offset >> sb->s_blocksize_bits;
		struct buffer_head *bh = nk_block(sb, block, chunk == sb->s_blocksize);

		if (!bh)
			return -EIO;
		lock_buffer(bh);
		memcpy(bh->b_data + within, buffer, chunk);
		set_buffer_uptodate(bh);
		unlock_buffer(bh);
		mark_buffer_dirty(bh);
		buffer += chunk;
		offset += chunk;
		length -= chunk;
	}
	return 0;
}

void nk_note(const char *what, long value)
{
	pr_info("ntfslib: %s %ld (0x%lx)\n", what, value, value);
}

int nk_dev_flush(void *device)
{
	struct super_block *sb = device;

	return sync_blockdev(sb->s_bdev);
}

static u64 nk_ticks(struct timespec64 ts)
{
	return (u64)ts.tv_sec * 10000000ULL + ts.tv_nsec / 100 + NK_TICKS_TO_1970;
}

static struct timespec64 nk_timespec(u64 ticks)
{
	struct timespec64 ts;
	s64 since = (s64)(ticks - NK_TICKS_TO_1970);
	s64 seconds = div_s64(since, 10000000);

	if (since < 0 && seconds * 10000000 != since)
		seconds--;
	ts.tv_sec = seconds;
	ts.tv_nsec = (since - seconds * 10000000) * 100;
	return ts;
}

/* The clock current_time() reads, so that times the core stamps never run ahead of those the VFS stamps. */
nk_u64 nk_time_now(void)
{
	struct timespec64 now;

	ktime_get_coarse_real_ts64(&now);
	return nk_ticks(now);
}

static const struct inode_operations nk_dir_iops;
static const struct inode_operations nk_file_iops;
static const struct inode_operations nk_link_iops;
static const struct file_operations nk_dir_fops;
static const struct file_operations nk_file_fops;
static const struct address_space_operations nk_aops;

static void nk_forget_run(struct inode *inode)
{
	struct nk_inode *ni = NK_I(inode);

	spin_lock(&ni->run_lock);
	ni->run_start = ni->run_end = 0;
	ni->run_changes++;
	spin_unlock(&ni->run_lock);
}

/*
 * Says whether the file is read and written block by block from now on. A file that already had data got
 * it through the core, which may have left it in the device's own cache, also when it moved the data out
 * of the record just now: it has to be on the device before blocks are read from there.
 */
static void nk_set_mapped(struct inode *inode, bool mapped)
{
	struct nk_sb *sbi = inode->i_sb->s_fs_info;

	if (mapped && !NK_I(inode)->mapped && (!NK_I(inode)->empty || sbi->core_wrote_data)) {
		sbi->core_wrote_data = false;
		sync_blockdev(inode->i_sb->s_bdev);
	}
	NK_I(inode)->mapped = mapped;
}

static loff_t nk_valid(struct nk_inode *ni)
{
	loff_t valid;

	spin_lock(&ni->run_lock);
	valid = ni->valid;
	spin_unlock(&ni->run_lock);
	return valid;
}

static void nk_set_valid(struct nk_inode *ni, loff_t valid)
{
	spin_lock(&ni->run_lock);
	ni->valid = valid;
	spin_unlock(&ni->run_lock);
}

static void nk_apply(struct inode *inode, const struct nk_attr *attr)
{
	NK_I(inode)->mapped = attr->mapped;
	NK_I(inode)->empty = attr->empty;
	NK_I(inode)->holes = attr->holes;
	nk_set_valid(NK_I(inode), attr->valid);
	NK_I(inode)->allocated = attr->allocated;
	if (attr->directory)
		set_nlink(inode, 1);
	else
		set_nlink(inode, attr->links ? attr->links : 1);
	i_size_write(inode, attr->size);
	inode->i_blocks = attr->allocated >> 9;
	inode_set_atime_to_ts(inode, nk_timespec(attr->accessed));
	inode_set_mtime_to_ts(inode, nk_timespec(attr->written));
	inode_set_ctime_to_ts(inode, nk_timespec(attr->changed));
}

static struct inode *nk_iget(struct super_block *sb, const struct nk_attr *attr)
{
	struct nk_sb *sbi = sb->s_fs_info;
	struct inode *inode = iget_locked(sb, attr->reference & 0xFFFFFFFFFFFFULL);

	if (!inode)
		return ERR_PTR(-ENOMEM);
	if (!(inode_state_read_once(inode) & I_NEW)) {
		if (inode->i_generation != (u32)(attr->reference >> 48)) {
			iput(inode);
			return ERR_PTR(-ESTALE);
		}
		return inode;
	}
	inode->i_generation = attr->reference >> 48;
	/* A record without stored ownership or mode belongs to whoever mounted the volume, open to all. */
	inode->i_uid = sbi->uid;
	inode->i_gid = sbi->gid;
	if ((attr->has & NK_HAS_UID) && uid_valid(make_kuid(&init_user_ns, attr->uid)))
		inode->i_uid = make_kuid(&init_user_ns, attr->uid);
	if ((attr->has & NK_HAS_GID) && gid_valid(make_kgid(&init_user_ns, attr->gid)))
		inode->i_gid = make_kgid(&init_user_ns, attr->gid);
	NK_I(inode)->removed = false;
	NK_I(inode)->changes = 0;
	NK_I(inode)->primed = 0;
	if (attr->mapped && sbi->core_wrote_data) {
		sbi->core_wrote_data = false;
		sync_blockdev(sb->s_bdev);
	}
	NK_I(inode)->directory_record = attr->directory;
	if ((attr->has & NK_HAS_MODE) && S_ISLNK(attr->mode)) {
		inode->i_mode = S_IFLNK | 0777;
		inode->i_op = &nk_link_iops;
		inode_nohighmem(inode);
	} else if (attr->directory) {
		inode->i_mode = S_IFDIR | (attr->has & NK_HAS_MODE ? attr->mode & 07777 : 0777);
		inode->i_op = &nk_dir_iops;
		inode->i_fop = &nk_dir_fops;
	} else if ((attr->has & NK_HAS_MODE) &&
		   (S_ISFIFO(attr->mode) || S_ISSOCK(attr->mode) || S_ISCHR(attr->mode) || S_ISBLK(attr->mode))) {
		/* A FIFO, socket or device: an ordinary record that its stored mode says is one. */
		init_special_inode(inode, attr->mode,
				   attr->has & NK_HAS_DEVICE ? MKDEV(attr->device >> 20, attr->device & 0xFFFFF) : 0);
		inode->i_op = &nk_file_iops;
	} else {
		inode->i_mode = S_IFREG | (attr->has & NK_HAS_MODE ? attr->mode & 07777 : 0777);
		inode->i_op = &nk_file_iops;
		inode->i_fop = &nk_file_fops;
		inode->i_mapping->a_ops = &nk_aops;
		mapping_set_large_folios(inode->i_mapping);
	}
	nk_apply(inode, attr);
	unlock_new_inode(inode);
	return inode;
}

/* stamp is the time the core holds for the directory, so that memory and device agree; zero takes the clock. */
static void nk_touch_dir(struct inode *dir, u64 stamp)
{
	if (stamp)
		inode_set_mtime_to_ts(dir, inode_set_ctime_to_ts(dir, nk_timespec(stamp)));
	else
		inode_set_mtime_to_ts(dir, inode_set_ctime_current(dir));
	inode_inc_iversion(dir);
	NK_I(dir)->changes++;
}

static struct dentry *nk_lookup_op(struct inode *dir, struct dentry *dentry, unsigned int flags)
{
	struct inode *inode = NULL;
	struct nk_attr attr;
	unsigned int saved;
	int err;

	if (dentry->d_name.len > 255)
		return ERR_PTR(-ENAMETOOLONG);
	saved = nk_enter();
	err = nk_lookup(nk_vol(dir->i_sb), nk_ref(dir), dentry->d_name.name, dentry->d_name.len, &attr);
	nk_leave(saved);
	if (!err) {
		inode = nk_iget(dir->i_sb, &attr);
		if (IS_ERR(inode))
			return ERR_CAST(inode);
	} else if (err != -ENOENT) {
		return ERR_PTR(err);
	}
	return d_splice_alias(inode, dentry);
}

static int nk_make(struct inode *dir, struct dentry *dentry, umode_t mode, dev_t device, const char *target)
{
	struct nk_owner owner = {
		.has = NK_HAS_UID | NK_HAS_GID | NK_HAS_MODE,
		.uid = from_kuid(&init_user_ns, current_fsuid()),
		.gid = from_kgid(&init_user_ns, current_fsgid()),
		.mode = mode,
	};
	struct inode *inode;
	struct nk_attr attr;
	unsigned int saved;
	u64 stamp;
	int err;

	/* A set-group-ID directory passes its group on, and the bit to the directories made in it. */
	if (dir->i_mode & S_ISGID) {
		owner.gid = from_kgid(&init_user_ns, dir->i_gid);
		if (S_ISDIR(mode))
			owner.mode |= S_ISGID;
	}
	if (S_ISCHR(mode) || S_ISBLK(mode)) {
		owner.has |= NK_HAS_DEVICE;
		owner.device = (MAJOR(device) << 20) | (MINOR(device) & 0xFFFFF);
	}
	saved = nk_enter();
	if (target)
		err = nk_symlink(nk_vol(dir->i_sb), nk_ref(dir), dentry->d_name.name, dentry->d_name.len, target,
				 strlen(target), &owner, &attr);
	else
		err = nk_create(nk_vol(dir->i_sb), nk_ref(dir), dentry->d_name.name, dentry->d_name.len, S_ISDIR(mode),
				&owner, &attr);
	stamp = nk_dir_time(nk_vol(dir->i_sb), nk_ref(dir));
	nk_leave(saved);
	if (err)
		return err;
	inode = nk_iget(dir->i_sb, &attr);
	if (IS_ERR(inode))
		return PTR_ERR(inode);
	nk_touch_dir(dir, stamp);
	d_instantiate(dentry, inode);
	return 0;
}

static int nk_create_op(struct mnt_idmap *idmap, struct inode *dir, struct dentry *dentry, umode_t mode, bool excl)
{
	return nk_make(dir, dentry, S_IFREG | (mode & 07777), 0, NULL);
}

static struct dentry *nk_mkdir_op(struct mnt_idmap *idmap, struct inode *dir, struct dentry *dentry, umode_t mode)
{
	return ERR_PTR(nk_make(dir, dentry, S_IFDIR | (mode & 07777), 0, NULL));
}

static int nk_mknod_op(struct mnt_idmap *idmap, struct inode *dir, struct dentry *dentry, umode_t mode, dev_t device)
{
	return nk_make(dir, dentry, mode, device, NULL);
}

static int nk_symlink_op(struct mnt_idmap *idmap, struct inode *dir, struct dentry *dentry, const char *target)
{
	return nk_make(dir, dentry, S_IFLNK | 0777, 0, target);
}

static int nk_link_op(struct dentry *old_dentry, struct inode *dir, struct dentry *dentry)
{
	struct inode *inode = d_inode(old_dentry);
	struct nk_attr attr;
	unsigned int saved;
	u64 stamp;
	int err;

	if (NK_I(inode)->directory_record)
		return -EPERM;
	saved = nk_enter();
	err = nk_link(nk_vol(dir->i_sb), nk_ref(inode), nk_ref(dir), dentry->d_name.name, dentry->d_name.len, &attr);
	stamp = nk_dir_time(nk_vol(dir->i_sb), nk_ref(dir));
	nk_leave(saved);
	if (err)
		return err;
	set_nlink(inode, attr.links ? attr.links : 1);
	inode_set_ctime_current(inode);
	mark_inode_dirty(inode);
	ihold(inode);
	nk_touch_dir(dir, stamp);
	d_instantiate(dentry, inode);
	return 0;
}

static const char *nk_get_link(struct dentry *dentry, struct inode *inode, struct delayed_call *done)
{
	unsigned int saved;
	char *target;
	long length;

	if (!dentry)
		return ERR_PTR(-ECHILD);
	target = kmalloc(PATH_MAX, GFP_KERNEL);
	if (!target)
		return ERR_PTR(-ENOMEM);
	saved = nk_enter();
	length = nk_readlink(nk_vol(inode->i_sb), nk_ref(inode), target, PATH_MAX - 1);
	nk_leave(saved);
	if (length < 0) {
		kfree(target);
		return ERR_PTR(length);
	}
	target[length] = 0;
	set_delayed_call(done, kfree_link, target);
	return target;
}

/* Removes one name. A file nothing else holds goes at once; otherwise its record waits for the last user. */
static int nk_remove_entry(struct inode *dir, struct dentry *dentry, int is_directory)
{
	struct inode *inode = d_inode(dentry);
	/*
	 * The record of a file can go with the name when nothing else can still reach the file: no other
	 * user of the name, no inode reference beyond the one unlink itself takes, and no pages still to write.
	 */
	int keep;
	unsigned int saved;
	u32 left = 0;
	u64 stamp;
	int err;

	/* A removed directory is dead to every later operation, so nobody can need its record again. */
	keep = !is_directory && (d_count(dentry) > 1 || atomic_read(&inode->i_count) > 2);

	if (!keep && inode->i_mapping->nrpages) {
		/* Clean pages go in one sweep; anything still to be written keeps the record for now. */
		if (!mapping_tagged(inode->i_mapping, PAGECACHE_TAG_DIRTY) &&
		    !mapping_tagged(inode->i_mapping, PAGECACHE_TAG_WRITEBACK))
			truncate_inode_pages(inode->i_mapping, 0);
		keep = inode->i_mapping->nrpages != 0;
	}
	saved = nk_enter();
	err = nk_remove(nk_vol(dir->i_sb), nk_ref(dir), dentry->d_name.name, dentry->d_name.len, nk_ref(inode),
			NK_I(inode)->directory_record, keep, &left);
	stamp = nk_dir_time(nk_vol(dir->i_sb), nk_ref(dir));
	nk_leave(saved);
	if (err)
		return err;
	if (left && !is_directory) {
		set_nlink(inode, left);
	} else {
		clear_nlink(inode);
		if (!keep) {
			NK_I(inode)->removed = true;
			remove_inode_hash(inode);
			/* Nobody else holds the name: it goes now, instead of staying behind as a cached absence. */
			d_drop(dentry);
		}
	}
	inode_set_ctime_current(inode);
	nk_touch_dir(dir, stamp);
	return 0;
}

static int nk_unlink_op(struct inode *dir, struct dentry *dentry)
{
	return nk_remove_entry(dir, dentry, 0);
}

static int nk_rmdir_op(struct inode *dir, struct dentry *dentry)
{
	return nk_remove_entry(dir, dentry, 1);
}

static int nk_rename_op(struct mnt_idmap *idmap, struct inode *old_dir, struct dentry *old_dentry,
			struct inode *new_dir, struct dentry *new_dentry, unsigned int flags)
{
	unsigned int saved;
	int err;

	if (flags & ~RENAME_NOREPLACE)
		return -EINVAL;
	if (d_really_is_positive(new_dentry)) {
		/* FIXME: the replaced name is removed before the rename publishes; not atomic. */
		err = nk_remove_entry(new_dir, new_dentry, d_is_dir(new_dentry));
		if (err)
			return err;
	}
	saved = nk_enter();
	err = nk_rename(nk_vol(old_dir->i_sb), nk_ref(old_dir), old_dentry->d_name.name, old_dentry->d_name.len,
			nk_ref(new_dir), new_dentry->d_name.name, new_dentry->d_name.len);
	nk_leave(saved);
	if (err)
		return err;
	/* FIXME: the core stamps both directories from its own clock reading; these can differ by a clock tick. */
	nk_touch_dir(old_dir, 0);
	nk_touch_dir(new_dir, 0);
	inode_set_ctime_current(d_inode(old_dentry));
	return 0;
}

static int nk_getattr_op(struct mnt_idmap *idmap, const struct path *path, struct kstat *stat, u32 request_mask,
			 unsigned int flags)
{
	generic_fillattr(idmap, request_mask, d_inode(path->dentry), stat);
	return 0;
}

static int nk_zero_tail(struct inode *inode, loff_t size);

static int nk_setattr_op(struct mnt_idmap *idmap, struct dentry *dentry, struct iattr *attr)
{
	struct inode *inode = d_inode(dentry);
	struct nk_inode *ni = NK_I(inode);
	unsigned int saved;
	int err = setattr_prepare(idmap, dentry, attr);

	if (err)
		return err;
	if (ni->removed) {
		/* Still open, but its record is gone: the change lives as long as the inode. */
		setattr_copy(idmap, inode, attr);
		return 0;
	}
	if ((attr->ia_valid & ATTR_SIZE) && attr->ia_size != i_size_read(inode)) {
		loff_t old = i_size_read(inode);
		struct nk_attr after;

		if (!ni->holes && attr->ia_size > nk_sbi(inode->i_sb)->volume_bytes)
			return -EFBIG;

		/* What the last block holds past the old or the new end must not come back when the file grows. */
		err = nk_zero_tail(inode, min_t(loff_t, attr->ia_size, old));
		if (err)
			return err;
		mutex_lock(&ni->size_lock);
		/* A file is cut in memory first and grown there last, so that a refusal by the core leaves no size it does not have. */
		if (attr->ia_size < old)
			truncate_setsize(inode, attr->ia_size);
		saved = nk_enter();
		err = nk_resize(nk_vol(inode->i_sb), nk_ref(inode), attr->ia_size, &after);
		nk_leave(saved);
		if (err) {
			if (attr->ia_size < old)
				i_size_write(inode, old);
			mutex_unlock(&ni->size_lock);
			return err;
		}
		if (attr->ia_size > old)
			truncate_setsize(inode, attr->ia_size);
		nk_forget_run(inode);
		ni->allocated = after.allocated;
		nk_set_mapped(inode, after.mapped);
		ni->empty = after.empty;
		mutex_lock(&ni->valid_lock);
		if (ni->valid > attr->ia_size)
			nk_set_valid(ni, attr->ia_size);
		mutex_unlock(&ni->valid_lock);
		inode->i_blocks = after.allocated >> 9;
		mutex_unlock(&ni->size_lock);
		/* truncate(2) leaves the times to the file system when the size really changes. */
		inode_set_mtime_to_ts(inode, inode_set_ctime_current(inode));
		mark_inode_dirty(inode);
	}
	if (attr->ia_valid & (ATTR_ATIME | ATTR_MTIME)) {
		saved = nk_enter();
		err = nk_set_times(nk_vol(inode->i_sb), nk_ref(inode),
				   attr->ia_valid & ATTR_ATIME ? nk_ticks(attr->ia_atime) : 0,
				   attr->ia_valid & ATTR_MTIME ? nk_ticks(attr->ia_mtime) : 0,
				   nk_time_now());
		nk_leave(saved);
		if (err)
			return err;
	}
	if (attr->ia_valid & (ATTR_MODE | ATTR_UID | ATTR_GID)) {
		kuid_t uid = inode->i_uid;
		kgid_t gid = inode->i_gid;
		umode_t mode = inode->i_mode;

		setattr_copy(idmap, inode, attr);
		/* An archiver sets what a new file already has: nothing to store then. */
		if (uid_eq(uid, inode->i_uid) && gid_eq(gid, inode->i_gid) && mode == inode->i_mode)
			return 0;
		saved = nk_enter();
		err = nk_set_owner(nk_vol(inode->i_sb), nk_ref(inode), from_kuid(&init_user_ns, inode->i_uid),
				   from_kgid(&init_user_ns, inode->i_gid), inode->i_mode);
		nk_leave(saved);
		if (err) {
			inode->i_uid = uid;
			inode->i_gid = gid;
			inode->i_mode = mode;
		}
		return err;
	}
	setattr_copy(idmap, inode, attr);
	return 0;
}

struct nk_list {
	char *buffer;
	unsigned int size;
	unsigned int used;
	unsigned int count;
	bool full;
};

/* One open directory's place in a listing: names gathered in one walk of the index are handed out over several calls. */
struct nk_dir {
	struct nk_list list;
	/* Position of the first name gathered, and of the next one to hand out with its place in the buffer. */
	loff_t first;
	loff_t next;
	unsigned int next_offset;
	/* The directory as it was when gathered; a change sends the listing back to the index. */
	u64 changes;
	/* The last walk filled the buffer before the names ended: the next one takes more at once. */
	bool large;
	bool ended;
	loff_t end;
	/* The directory had no cached names when this listing began. */
	bool fresh;
	/* This listing began at the first name and has handed out every one since. */
	bool whole;
	/* An earlier listing cached every name and the directory has not changed: nothing to prime. */
	bool primed;
};

struct nk_list_entry {
	u64 ino;
	u64 reference;
	u16 length;
	u8 kind;
	char name[];
};

static int nk_list_emit(void *context, const char *name, nk_u32 length, nk_u64 reference, int kind)
{
	struct nk_list *list = context;
	unsigned int need = ALIGN(sizeof(struct nk_list_entry) + length, 8);
	struct nk_list_entry *entry = (struct nk_list_entry *)(list->buffer + list->used);

	if (list->used + need > list->size) {
		list->full = true;
		return 1;
	}
	entry->ino = reference & 0xFFFFFFFFFFFFULL;
	entry->reference = reference;
	entry->length = length;
	entry->kind = kind;
	memcpy(entry->name, name, length);
	list->used += need;
	list->count++;
	return 0;
}

/* Whoever lists a directory usually looks its names up next: each name listed becomes a cached name and inode. */
static void nk_prime(struct dentry *parent, const struct nk_list_entry *entry, bool fresh)
{
	DECLARE_WAIT_QUEUE_HEAD_ONSTACK(wq);
	struct qstr name = QSTR_INIT(entry->name, entry->length);
	struct dentry *dentry;
	struct dentry *alias;
	struct inode *inode;
	struct nk_attr attr;
	unsigned int saved;
	int err;

	name.hash = full_name_hash(parent, name.name, name.len);
	/* A directory that had no cached names when the listing began is not searched for each one first. */
	dentry = fresh ? NULL : d_lookup(parent, &name);
	if (!dentry) {
		dentry = d_alloc_parallel(parent, &name, &wq);
		if (IS_ERR(dentry))
			return;
	}
	if (d_in_lookup(dentry)) {
		saved = nk_enter();
		err = nk_getattr(nk_vol(parent->d_sb), entry->reference, &attr);
		nk_leave(saved);
		inode = err ? ERR_PTR(err) : nk_iget(parent->d_sb, &attr);
		alias = IS_ERR(inode) ? NULL : d_splice_alias(inode, dentry);
		d_lookup_done(dentry);
		if (!IS_ERR_OR_NULL(alias))
			dput(alias);
	}
	dput(dentry);
}

static void nk_dir_drop(struct nk_sb *sbi, struct nk_dir *state)
{
	if (!state->list.buffer)
		return;
	if (state->list.size == NK_LIST_BYTES)
		kvfree(xchg(&sbi->list_buffer, state->list.buffer));
	else
		kvfree(state->list.buffer);
	state->list.buffer = NULL;
	state->list.count = 0;
}

/* A name its index entry cannot type, such as a link or a device: its inode, which listings cache anyway, can. */
static unsigned char nk_entry_type(struct dentry *parent, const struct nk_list_entry *entry)
{
	struct qstr name = QSTR_INIT(entry->name, entry->length);
	unsigned char type = DT_UNKNOWN;
	struct dentry *dentry;

	nk_prime(parent, entry, false);
	name.hash = full_name_hash(parent, name.name, name.len);
	dentry = d_lookup(parent, &name);
	if (dentry) {
		if (d_really_is_positive(dentry))
			type = S_DT(d_inode(dentry)->i_mode);
		dput(dentry);
	}
	return type;
}

/* Names are gathered under the lock and handed to the caller after it: filling its buffer can fault. */
static int nk_dir_gather(struct inode *inode, struct nk_dir *state, loff_t pos)
{
	struct nk_sb *sbi = inode->i_sb->s_fs_info;
	struct nk_list *list = &state->list;
	unsigned int want = state->large ? NK_LIST_LARGE : NK_LIST_BYTES;
	sector_t ahead[NK_LIST_RUNS];
	sector_t first = 0;
	unsigned int runs = 0;
	unsigned int offset = 0;
	unsigned int index;
	unsigned int saved;
	int err;

	if (list->buffer && list->size != want)
		nk_dir_drop(sbi, state);
	if (!list->buffer) {
		list->buffer = want == NK_LIST_BYTES ? xchg(&sbi->list_buffer, NULL) : NULL;
		if (!list->buffer)
			list->buffer = kvmalloc(want, GFP_KERNEL);
		if (!list->buffer)
			return -ENOMEM;
		list->size = want;
	}
	list->used = 0;
	list->count = 0;
	list->full = false;
	saved = nk_enter();
	err = nk_readdir(nk_vol(inode->i_sb), nk_ref(inode), pos - 2, nk_list_emit, list);
	/* Whoever lists a directory usually looks at its files next: start reading their records. */
	for (index = 0; !err && index < list->count; index++) {
		struct nk_list_entry *entry = (struct nk_list_entry *)(list->buffer + offset);
		sector_t block = nk_record_offset(nk_vol(inode->i_sb), entry->reference) >> PAGE_SHIFT;

		if (block && (block < first || block >= first + NK_READ_AHEAD)) {
			if (runs < NK_LIST_RUNS)
				ahead[runs++] = block;
			first = block;
		}
		offset += ALIGN(sizeof(struct nk_list_entry) + entry->length, 8);
	}
	nk_leave(saved);
	if (err) {
		nk_dir_drop(sbi, state);
		return err;
	}
	while (runs) {
		struct folio *cached = filemap_get_folio(inode->i_sb->s_bdev->bd_mapping, ahead[--runs]);

		/* Records already in memory need no reading. */
		if (IS_ERR(cached))
			nk_read_ahead(inode->i_sb, ahead[runs], NK_READ_AHEAD);
		else
			folio_put(cached);
	}
	state->first = pos;
	state->next = pos;
	state->next_offset = 0;
	state->large = list->full;
	state->ended = !list->full;
	state->end = pos + list->count;
	return 0;
}

static int nk_iterate(struct file *file, struct dir_context *ctx)
{
	struct inode *inode = file_inode(file);
	struct nk_sb *sbi = inode->i_sb->s_fs_info;
	struct nk_dir *state = file->private_data;
	struct nk_list *list;
	int err;

	if (!dir_emit_dots(file, ctx))
		return 0;
	if (!state) {
		state = kzalloc(sizeof(*state), GFP_KERNEL);
		if (!state)
			return -ENOMEM;
		file->private_data = state;
	}
	list = &state->list;
	if (state->changes != NK_I(inode)->changes) {
		nk_dir_drop(sbi, state);
		state->ended = false;
		state->whole = false;
		state->changes = NK_I(inode)->changes;
	}
	if (ctx->pos == 2) {
		state->fresh = hlist_empty(&file->f_path.dentry->d_children);
		state->whole = true;
		if (!list->buffer)
			state->next = 2;
		state->primed = !state->fresh && NK_I(inode)->primed == state->changes + 1;
	}
	for (;;) {
		if (list->buffer && ctx->pos >= state->first && ctx->pos < state->first + list->count) {
			if (ctx->pos != state->next) {
				state->whole = false;
				state->next = state->first;
				state->next_offset = 0;
				while (state->next < ctx->pos) {
					struct nk_list_entry *entry = (struct nk_list_entry *)(list->buffer + state->next_offset);

					state->next_offset += ALIGN(sizeof(struct nk_list_entry) + entry->length, 8);
					state->next++;
				}
			}
			while (state->next < state->first + list->count) {
				struct nk_list_entry *entry = (struct nk_list_entry *)(list->buffer + state->next_offset);

				if (!dir_emit(ctx, entry->name, entry->length, entry->ino,
					      entry->kind == NK_NAME_DIRECTORY ? DT_DIR
					      : entry->kind == NK_NAME_FILE ? DT_REG
					      : nk_entry_type(file->f_path.dentry, entry)))
					return 0;
				ctx->pos++;
				state->next++;
				state->next_offset += ALIGN(sizeof(struct nk_list_entry) + entry->length, 8);
				if (state->primed && state->next == state->first + 1) {
					/* One probe per batch: cached names can be reclaimed behind the directory's back. */
					struct qstr name = QSTR_INIT(entry->name, entry->length);
					struct dentry *cached;

					name.hash = full_name_hash(file->f_path.dentry, name.name, name.len);
					cached = d_lookup(file->f_path.dentry, &name);
					if (cached)
						dput(cached);
					else
						state->primed = false;
				}
				if (!state->primed)
					nk_prime(file->f_path.dentry, entry, state->fresh);
			}
			/* All handed out: an open directory keeps no buffer between listings. */
			nk_dir_drop(sbi, state);
		}
		if (state->ended && ctx->pos >= state->end) {
			if (state->whole && ctx->pos == state->end)
				NK_I(inode)->primed = state->changes + 1;
			return 0;
		}
		if (ctx->pos != state->next)
			state->whole = false;
		err = nk_dir_gather(inode, state, ctx->pos);
		if (err)
			return err;
		if (!list->count) {
			nk_dir_drop(sbi, state);
			return 0;
		}
	}
}

static int nk_dir_release(struct inode *inode, struct file *file)
{
	struct nk_dir *state = file->private_data;

	if (state) {
		nk_dir_drop(inode->i_sb->s_fs_info, state);
		kfree(state);
	}
	return 0;
}

/* The clusters behind a block of a file, from the last run looked up or from the core; none leaves *run zero. */
static int nk_find_run(struct inode *inode, u64 offset, u64 *disk, u64 *run)
{
	struct nk_inode *ni = NK_I(inode);
	unsigned int saved;
	u64 changes;
	int err;

	*run = 0;
	spin_lock(&ni->run_lock);
	if (offset >= ni->run_start && offset < ni->run_end) {
		*disk = ni->run_disk + (offset - ni->run_start);
		*run = ni->run_end - offset;
	}
	changes = ni->run_changes;
	spin_unlock(&ni->run_lock);
	if (*run)
		return 0;
	saved = nk_enter();
	err = nk_map(nk_vol(inode->i_sb), nk_ref(inode), offset, disk, run);
	nk_leave(saved);
	if (!err && *run) {
		spin_lock(&ni->run_lock);
		if (changes == ni->run_changes) {
			ni->run_start = offset;
			ni->run_end = offset + *run;
			ni->run_disk = *disk;
		}
		spin_unlock(&ni->run_lock);
	}
	return err;
}

/*
 * Blocks below the written length are read and written in place. Blocks past it are holes to a reader;
 * a writer gets their clusters marked new, so that what they held before is never read.
 */
static int nk_iomap_begin(struct inode *inode, loff_t pos, loff_t length, unsigned int flags, struct iomap *iomap,
			  struct iomap *srcmap)
{
	u64 block = i_blocksize(inode);
	u64 offset = round_down(pos, block);
	u64 end = round_up(pos + length, block);
	u64 valid = nk_valid(NK_I(inode));
	u64 edge = round_up(valid, block);
	bool writing = flags & (IOMAP_WRITE | IOMAP_ZERO);
	u64 disk = 0;
	u64 run = 0;
	int err = 0;

	iomap->bdev = inode->i_sb->s_bdev;
	iomap->offset = offset;
	iomap->flags = 0;
	if (writing || offset < edge)
		err = nk_find_run(inode, offset, &disk, &run);
	if (err)
		return err;
	if (!run) {
		if (writing) {
			pr_warn_ratelimited("ntfslib: no clusters behind byte %llu of inode %lu (allocated %llu, size %lld)\n",
					    offset, inode->i_ino, NK_I(inode)->allocated, i_size_read(inode));
			return -ENOSPC;
		}
		iomap->type = IOMAP_HOLE;
		iomap->addr = IOMAP_NULL_ADDR;
		iomap->length = end - offset;
		return 0;
	}
	iomap->type = IOMAP_MAPPED;
	iomap->addr = disk;
	if (offset < edge) {
		/* A block the written length ends in, short of the size, is read on its own: see nk_read_range(). */
		u64 last = edge - block;
		bool ragged = (valid & (block - 1)) && valid < i_size_read(inode);

		iomap->length = min(run, ragged && offset < last ? last - offset : edge - offset);
	} else {
		iomap->flags = IOMAP_F_NEW;
		iomap->length = run;
	}
	return 0;
}

static int nk_iomap_end(struct inode *inode, loff_t pos, loff_t length, ssize_t written, unsigned int flags,
			struct iomap *iomap)
{
	if ((flags & IOMAP_WRITE) && (iomap->flags & IOMAP_F_SIZE_CHANGED))
		mark_inode_dirty(inode);
	return 0;
}

static const struct iomap_ops nk_iomap_ops = {
	.iomap_begin = nk_iomap_begin,
	.iomap_end = nk_iomap_end,
};

/*
 * Reads blocks of a file into a folio at once when the written length ends inside them and the file goes
 * on after it: what the device holds from there to the end of that block was never written and has to
 * read as zeros. Returns 1 when the range does not hold that point and the ordinary read applies. When
 * the written length is also the size, those bytes lie past the end: nk_fault() and nk_zero_tail() see to
 * them.
 */
static int nk_read_range(const struct iomap_iter *iter, struct folio *folio, loff_t pos, size_t length)
{
	u64 valid = nk_valid(NK_I(iter->inode));
	char *data = folio_address(folio) + offset_in_folio(folio, pos);
	int err;

	if (!(valid & (i_blocksize(iter->inode) - 1)) || valid <= pos || valid >= pos + length ||
	    valid >= i_size_read(iter->inode))
		return 1;
	err = bdev_rw_virt(iter->iomap.bdev, iomap_sector(&iter->iomap, pos), data, length, REQ_OP_READ);
	if (!err)
		memset(data + (valid - pos), 0, pos + length - valid);
	return err;
}

static int nk_read_folio_range(const struct iomap_iter *iter, struct iomap_read_folio_ctx *ctx, size_t length)
{
	struct folio *folio = ctx->cur_folio;
	int err = nk_read_range(iter, folio, iter->pos, length);

	if (err > 0)
		return iomap_bio_read_ops.read_folio_range(iter, ctx, length);
	if (!err)
		iomap_finish_folio_read(folio, offset_in_folio(folio, iter->pos), length, 0);
	return err;
}

static void nk_submit_read(struct iomap_read_folio_ctx *ctx)
{
	if (iomap_bio_read_ops.submit_read)
		iomap_bio_read_ops.submit_read(ctx);
}

static const struct iomap_read_ops nk_read_ops = {
	.read_folio_range = nk_read_folio_range,
	.submit_read = nk_submit_read,
};

/* The same for a block a write fills only in part. A null hook result sends iomap to its own read. */
static int nk_write_read_range(const struct iomap_iter *iter, struct folio *folio, loff_t pos, size_t length)
{
	int err = nk_read_range(iter, folio, pos, length);

	if (err > 0)
		err = bdev_rw_virt(iter->iomap.bdev, iomap_sector(&iter->iomap, pos),
				   folio_address(folio) + offset_in_folio(folio, pos), length, REQ_OP_READ);
	return err;
}

static const struct iomap_write_ops nk_write_ops = {
	.read_folio_range = nk_write_read_range,
};

/*
 * The bytes from the end of the file to the end of its block, in the page cache: they become part of the
 * file when it grows. A block that was never written and has nothing pending is a hole already.
 */
static int nk_zero_tail(struct inode *inode, loff_t size)
{
	u64 block = i_blocksize(inode);
	u64 within = size & (block - 1);

	if (!NK_I(inode)->mapped || !within)
		return 0;
	if (size - within >= round_up(nk_valid(NK_I(inode)), block) &&
	    !filemap_range_needs_writeback(inode->i_mapping, size, size))
		return 0;
	return iomap_zero_range(inode, size, block - within, NULL, &nk_iomap_ops, &nk_write_ops, NULL);
}

/*
 * Write-back is about to store [pos, pos + length): the written length moves over it. Clusters between the
 * old length and pos are zeroed on the device first, as nothing else will ever write them.
 */
static int nk_reach(struct inode *inode, u64 pos, u64 length)
{
	struct nk_inode *ni = NK_I(inode);
	u64 block = i_blocksize(inode);
	u64 edge;
	int err = 0;

	if (pos + length <= nk_valid(ni))
		return 0;
	mutex_lock(&ni->valid_lock);
	edge = round_up(ni->valid, block);
	if ((ni->valid & (block - 1)) && pos >= edge) {
		/* The length ends inside a block this write-back passes over: the rest of that block too. */
		void *scratch = (void *)__get_free_page(GFP_NOFS);
		u64 within = ni->valid & (block - 1);
		u64 disk = 0;
		u64 run = 0;

		err = scratch ? nk_find_run(inode, edge - block, &disk, &run) : -ENOMEM;
		if (!err && !run)
			err = -EIO;
		if (!err)
			err = bdev_rw_virt(inode->i_sb->s_bdev, disk >> SECTOR_SHIFT, scratch, block, REQ_OP_READ);
		if (!err) {
			memset(scratch + within, 0, block - within);
			err = bdev_rw_virt(inode->i_sb->s_bdev, disk >> SECTOR_SHIFT, scratch, block, REQ_OP_WRITE);
		}
		free_page((unsigned long)scratch);
	}
	while (!err && edge < pos) {
		u64 disk = 0;
		u64 run = 0;

		err = nk_find_run(inode, edge, &disk, &run);
		if (!err && !run)
			err = -EIO;
		if (err)
			break;
		run = min(run, pos - edge);
		err = blkdev_issue_zeroout(inode->i_sb->s_bdev, disk >> SECTOR_SHIFT, run >> SECTOR_SHIFT, GFP_NOFS, 0);
		edge += run;
	}
	if (!err && pos + length > ni->valid)
		nk_set_valid(ni, pos + length);
	mutex_unlock(&ni->valid_lock);
	if (!err)
		mark_inode_dirty_sync(inode);
	return err;
}

static ssize_t nk_writeback_range(struct iomap_writepage_ctx *wpc, struct folio *folio, u64 pos, unsigned int length,
				  u64 end_pos)
{
	struct inode *inode = wpc->inode;
	int err;

	if (pos < wpc->iomap.offset || pos >= wpc->iomap.offset + wpc->iomap.length) {
		u64 disk = 0;
		u64 run = 0;

		err = nk_find_run(inode, pos, &disk, &run);
		if (!err && !run)
			err = -EIO;
		if (err)
			return err;
		wpc->iomap.type = IOMAP_MAPPED;
		wpc->iomap.flags = 0;
		wpc->iomap.bdev = inode->i_sb->s_bdev;
		wpc->iomap.offset = pos;
		wpc->iomap.addr = disk;
		wpc->iomap.length = run;
	}
	err = nk_reach(inode, pos, min_t(u64, length, wpc->iomap.offset + wpc->iomap.length - pos));
	if (err)
		return err;
	return iomap_add_to_ioend(wpc, folio, pos, end_pos, length);
}

static const struct iomap_writeback_ops nk_writeback_ops = {
	.writeback_range = nk_writeback_range,
	.writeback_submit = iomap_ioend_writeback_submit,
};

static int nk_fill_folio(struct inode *inode, struct folio *folio)
{
	size_t length = folio_size(folio);
	char *data = folio_address(folio);
	long done = nk_read(nk_vol(inode->i_sb), nk_ref(inode), folio_pos(folio), data, length);

	if (done < 0)
		return done;
	if ((size_t)done < length)
		memset(data + done, 0, length - done);
	flush_dcache_folio(folio);
	folio_mark_uptodate(folio);
	return 0;
}

static int nk_read_folio(struct file *file, struct folio *folio)
{
	struct inode *inode = folio->mapping->host;
	unsigned int saved;
	int err;

	if (NK_I(inode)->mapped) {
		struct iomap_read_folio_ctx ctx = {
			.ops = &nk_read_ops,
			.cur_folio = folio,
		};

		iomap_read_folio(&nk_iomap_ops, &ctx, NULL);
		return 0;
	}
	saved = nk_enter();
	err = nk_fill_folio(inode, folio);
	nk_leave(saved);
	folio_unlock(folio);
	return err;
}

static void nk_readahead(struct readahead_control *rac)
{
	struct inode *inode = rac->mapping->host;
	struct folio *folio;
	unsigned int saved;

	if (NK_I(inode)->mapped) {
		struct iomap_read_folio_ctx ctx = {
			.ops = &nk_read_ops,
			.rac = rac,
		};

		iomap_readahead(&nk_iomap_ops, &ctx, NULL);
		return;
	}
	saved = nk_enter();
	while ((folio = readahead_folio(rac)) != NULL) {
		nk_fill_folio(inode, folio);
		folio_unlock(folio);
	}
	nk_leave(saved);
}

/* A stream the core must write itself, changed through a mapping, reaches the core a folio at a time. */
static int nk_writepages_through(struct address_space *mapping, struct writeback_control *wbc)
{
	struct inode *inode = mapping->host;
	struct nk_sb *sbi = inode->i_sb->s_fs_info;
	struct folio *folio = NULL;
	int err = 0;

	while ((folio = writeback_iter(mapping, wbc, folio, &err)) != NULL) {
		loff_t size = i_size_read(inode);
		loff_t pos = folio_pos(folio);
		size_t length = pos < size ? min_t(u64, folio_size(folio), size - pos) : 0;
		struct nk_attr attr;
		long written = 0;

		folio_start_writeback(folio);
		if (length) {
			unsigned int saved = nk_enter();

			written = nk_write(nk_vol(inode->i_sb), nk_ref(inode), pos, folio_address(folio), length, &attr);
			nk_leave(saved);
		}
		if (written < 0) {
			err = written;
			mapping_set_error(mapping, err);
		} else if (length) {
			if (attr.allocated > NK_RESIDENT_LIMIT * 2)
				sbi->core_wrote_data = true;
			NK_I(inode)->allocated = attr.allocated;
			inode->i_blocks = attr.allocated >> 9;
		}
		folio_end_writeback(folio);
		folio_unlock(folio);
	}
	return err;
}

static int nk_writepages(struct address_space *mapping, struct writeback_control *wbc)
{
	struct iomap_writepage_ctx wpc = {
		.inode = mapping->host,
		.wbc = wbc,
		.ops = &nk_writeback_ops,
	};

	if (!NK_I(mapping->host)->mapped)
		return nk_writepages_through(mapping, wbc);
	return iomap_writepages(&wpc);
}

static sector_t nk_bmap(struct address_space *mapping, sector_t block)
{
	return NK_I(mapping->host)->mapped ? iomap_bmap(mapping, block, &nk_iomap_ops) : 0;
}

static const struct address_space_operations nk_aops = {
	.read_folio = nk_read_folio,
	.readahead = nk_readahead,
	.writepages = nk_writepages,
	.dirty_folio = iomap_dirty_folio,
	.release_folio = iomap_release_folio,
	.invalidate_folio = iomap_invalidate_folio,
	.migrate_folio = filemap_migrate_folio,
	.is_partially_uptodate = iomap_is_partially_uptodate,
	.error_remove_folio = generic_error_remove_folio,
	.bmap = nk_bmap,
};

/* A first store through a mapping: the clusters are there already, so the folio only has to be marked. */
static vm_fault_t nk_page_mkwrite(struct vm_fault *vmf)
{
	struct inode *inode = file_inode(vmf->vma->vm_file);
	vm_fault_t ret;

	sb_start_pagefault(inode->i_sb);
	file_update_time(vmf->vma->vm_file);
	ret = NK_I(inode)->mapped ? iomap_page_mkwrite(vmf, &nk_iomap_ops, NULL) : filemap_page_mkwrite(vmf);
	sb_end_pagefault(inode->i_sb);
	return ret;
}

/*
 * A mapping shows the whole last page of a file. What the device holds there past the end is only known
 * to be zeros when this driver wrote the block, so the page with the end in it is cleared from the end on
 * each time it is mapped; the folio lock keeps a write that grows the file out.
 */
static vm_fault_t nk_fault(struct vm_fault *vmf)
{
	struct inode *inode = file_inode(vmf->vma->vm_file);
	vm_fault_t ret = filemap_fault(vmf);

	if ((ret & VM_FAULT_LOCKED) && NK_I(inode)->mapped) {
		struct folio *folio = page_folio(vmf->page);
		loff_t size = i_size_read(inode);

		if (size > folio_pos(folio) && size < folio_pos(folio) + folio_size(folio))
			folio_zero_segment(folio, offset_in_folio(folio, size), folio_size(folio));
	}
	return ret;
}

/* Pages are mapped around a fault without it; the one with the end of the file in it waits for its own. */
static vm_fault_t nk_map_pages(struct vm_fault *vmf, pgoff_t start, pgoff_t end)
{
	struct inode *inode = file_inode(vmf->vma->vm_file);
	loff_t size = i_size_read(inode);
	pgoff_t last = size >> PAGE_SHIFT;

	if (NK_I(inode)->mapped && (size & (PAGE_SIZE - 1))) {
		if (start >= last)
			return 0;
		end = min(end, last - 1);
	}
	return filemap_map_pages(vmf, start, end);
}

static const struct vm_operations_struct nk_vm_ops = {
	.fault = nk_fault,
	.map_pages = nk_map_pages,
	.page_mkwrite = nk_page_mkwrite,
};

static int nk_mmap_prepare(struct vm_area_desc *desc)
{
	int err = generic_file_mmap_prepare(desc);

	if (!err)
		desc->vm_ops = &nk_vm_ops;
	return err;
}

/* Stores the size, the written length and the times the page cache has reached. */
static int nk_commit_inode(struct inode *inode, int trim)
{
	struct nk_inode *ni = NK_I(inode);
	struct nk_attr after;
	unsigned int saved;
	loff_t size;
	int err;

	if (!inode->i_nlink || ni->removed)
		return 0;
	if (!S_ISREG(inode->i_mode) || !ni->mapped) {
		/* The core keeps the sizes of these itself; the times the inode gathered still have to reach it. */
		saved = nk_enter();
		err = nk_set_times(nk_vol(inode->i_sb), nk_ref(inode), nk_ticks(inode_get_atime(inode)),
				   nk_ticks(inode_get_mtime(inode)), nk_ticks(inode_get_ctime(inode)));
		nk_leave(saved);
		return err;
	}
	/* The size is read and stored under the lock: a truncation in between would be undone by an old one. */
	mutex_lock(&ni->size_lock);
	size = i_size_read(inode);
	saved = nk_enter();
	err = nk_commit(nk_vol(inode->i_sb), nk_ref(inode), size, min_t(loff_t, nk_valid(ni), size), trim,
			nk_ticks(inode_get_atime(inode)), nk_ticks(inode_get_mtime(inode)), nk_ticks(inode_get_ctime(inode)),
			&after);
	nk_leave(saved);
	if (!err) {
		if (ni->allocated != after.allocated)
			nk_forget_run(inode);
		ni->allocated = after.allocated;
		inode->i_blocks = after.allocated >> 9;
	}
	mutex_unlock(&ni->size_lock);
	return err;
}

/* For streams the core must write itself: resident, compressed or sparse data. */
static ssize_t nk_write_through(struct kiocb *iocb, struct iov_iter *from)
{
	struct inode *inode = file_inode(iocb->ki_filp);
	struct nk_sb *sbi = inode->i_sb->s_fs_info;
	ssize_t done = 0;
	ssize_t ret = 0;
	char *buffer = kvmalloc(min_t(size_t, iov_iter_count(from), NK_WRITE_CHUNK), GFP_KERNEL);

	if (!buffer)
		return -ENOMEM;
	while (iov_iter_count(from)) {
		size_t chunk = min_t(size_t, iov_iter_count(from), NK_WRITE_CHUNK);
		loff_t pos = iocb->ki_pos;
		struct nk_attr attr;
		unsigned int saved;
		long written;

		if (copy_from_iter(buffer, chunk, from) != chunk) {
			ret = -EFAULT;
			break;
		}
		/* What a mapping changed in these pages goes to the core first, so that this write lands on top. */
		ret = filemap_write_and_wait_range(inode->i_mapping, pos, pos + chunk - 1);
		if (ret)
			break;
		saved = nk_enter();
		written = nk_write(nk_vol(inode->i_sb), nk_ref(inode), pos, buffer, chunk, &attr);
		nk_leave(saved);
		if (written < 0) {
			ret = written;
			break;
		}
		if (attr.allocated > NK_RESIDENT_LIMIT * 2)
			sbi->core_wrote_data = true;
		invalidate_inode_pages2_range(inode->i_mapping, pos >> PAGE_SHIFT, (pos + written - 1) >> PAGE_SHIFT);
		iocb->ki_pos = pos + written;
		done += written;
		if (iocb->ki_pos > i_size_read(inode))
			i_size_write(inode, iocb->ki_pos);
		NK_I(inode)->empty = false;
		NK_I(inode)->allocated = attr.allocated;
		nk_set_valid(NK_I(inode), attr.valid);
		inode->i_blocks = attr.allocated >> 9;
		inode_set_mtime_to_ts(inode, nk_timespec(attr.written));
		inode_set_ctime_to_ts(inode, nk_timespec(attr.changed));
	}
	kvfree(buffer);
	return done ? done : ret;
}

static ssize_t nk_write_iter(struct kiocb *iocb, struct iov_iter *from)
{
	struct inode *inode = file_inode(iocb->ki_filp);
	struct nk_inode *ni = NK_I(inode);
	ssize_t ret;

	inode_lock(inode);
	ret = generic_write_checks(iocb, from);
	if (ret > 0 && !ni->holes && iocb->ki_pos + ret > nk_sbi(inode->i_sb)->volume_bytes) {
		/* As past s_maxbytes: what fits is written, a write that starts beyond fails. */
		if (iocb->ki_pos >= nk_sbi(inode->i_sb)->volume_bytes) {
			ret = -EFBIG;
			goto unlock;
		}
		iov_iter_truncate(from, nk_sbi(inode->i_sb)->volume_bytes - iocb->ki_pos);
		ret = iov_iter_count(from);
	}
	if (ret > 0) {
		u64 end = iocb->ki_pos + ret;
		int privs = file_modified(iocb->ki_filp);

		/* The set-ID bits go and the times move whichever way the bytes take. */
		if (privs) {
			ret = privs;
			goto unlock;
		}
		/* A new file that takes more than fits in its record starts on clusters of its own. */
		if (!ni->mapped && !(ni->empty && end > NK_RESIDENT_LIMIT)) {
			ret = nk_write_through(iocb, from);
		} else {
			if (end > ni->allocated) {
				/* Appends reserve ahead; the last close gives the rest back. */
				u64 target = ni->allocated ? end + clamp_t(u64, end / 4, 1024 * 1024, 64 * 1024 * 1024) : end;
				struct nk_attr after;
				unsigned int saved = nk_enter();
				int err = nk_reserve(nk_vol(inode->i_sb), nk_ref(inode), target, &after);

				nk_leave(saved);
				if (err) {
					pr_warn_ratelimited("ntfslib: reserving %llu bytes for inode %lu failed (%d), had %llu\n",
							    target, inode->i_ino, err, ni->allocated);
					ret = err;
					goto unlock;
				}
				nk_forget_run(inode);
				ni->allocated = after.allocated;
				nk_set_mapped(inode, after.mapped);
				ni->empty = false;
				inode->i_blocks = after.allocated >> 9;
				if (!ni->mapped) {
					ret = nk_write_through(iocb, from);
					goto unlock;
				}
			}
			ret = iocb->ki_pos > i_size_read(inode) ? nk_zero_tail(inode, i_size_read(inode)) : 0;
			if (!ret)
				ret = iomap_file_buffered_write(iocb, from, &nk_iomap_ops, &nk_write_ops, NULL);
		}
	}
unlock:
	inode_unlock(inode);
	if (ret > 0 && ni->mapped)
		ret = generic_write_sync(iocb, ret);
	return ret;
}

static int nk_fsync(struct file *file, loff_t start, loff_t end, int datasync)
{
	struct inode *inode = file_inode(file);
	int err = file_write_and_wait_range(file, start, end);

	if (!err)
		err = nk_commit_inode(inode, 0);
	if (!err) {
		unsigned int saved = nk_enter();

		nk_settle(nk_vol(inode->i_sb));
		nk_leave(saved);
		err = sync_blockdev(inode->i_sb->s_bdev);
	}
	return err;
}

static int nk_release(struct inode *inode, struct file *file)
{
	if ((file->f_mode & FMODE_WRITE) && atomic_read(&inode->i_writecount) == 1 &&
	    NK_I(inode)->allocated > round_up(i_size_read(inode), 4096) + 65536)
		nk_commit_inode(inode, 1);
	return 0;
}

static const struct file_operations nk_file_fops = {
	.llseek = generic_file_llseek,
	.read_iter = generic_file_read_iter,
	.write_iter = nk_write_iter,
	.mmap_prepare = nk_mmap_prepare,
	.splice_read = filemap_splice_read,
	.splice_write = iter_file_splice_write,
	.fsync = nk_fsync,
	.release = nk_release,
};

static const struct file_operations nk_dir_fops = {
	.llseek = generic_file_llseek,
	.read = generic_read_dir,
	.iterate_shared = nk_iterate,
	.release = nk_dir_release,
	.fsync = nk_fsync,
};

static const struct inode_operations nk_dir_iops = {
	.lookup = nk_lookup_op,
	.create = nk_create_op,
	.mkdir = nk_mkdir_op,
	.mknod = nk_mknod_op,
	.symlink = nk_symlink_op,
	.link = nk_link_op,
	.unlink = nk_unlink_op,
	.rmdir = nk_rmdir_op,
	.rename = nk_rename_op,
	.getattr = nk_getattr_op,
	.setattr = nk_setattr_op,
};

static const struct inode_operations nk_file_iops = {
	.getattr = nk_getattr_op,
	.setattr = nk_setattr_op,
};

static const struct inode_operations nk_link_iops = {
	.get_link = nk_get_link,
	.getattr = nk_getattr_op,
	.setattr = nk_setattr_op,
};

static struct inode *nk_alloc_inode(struct super_block *sb)
{
	struct nk_inode *ni = alloc_inode_sb(sb, nk_inode_cache, GFP_NOFS);

	if (!ni)
		return NULL;
	ni->mapped = false;
	ni->empty = false;
	ni->removed = false;
	ni->holes = false;
	ni->directory_record = false;
	ni->valid = 0;
	ni->allocated = 0;
	mutex_init(&ni->valid_lock);
	mutex_init(&ni->size_lock);
	spin_lock_init(&ni->run_lock);
	ni->run_start = ni->run_end = ni->run_disk = 0;
	ni->run_changes = 0;
	return &ni->vfs;
}

static void nk_free_inode(struct inode *inode)
{
	kmem_cache_free(nk_inode_cache, NK_I(inode));
}

static int nk_write_inode(struct inode *inode, struct writeback_control *wbc)
{
	return nk_commit_inode(inode, 0);
}

static void nk_evict(struct inode *inode)
{
	truncate_inode_pages_final(&inode->i_data);
	invalidate_inode_buffers(inode);
	clear_inode(inode);
	if (!inode->i_nlink && !NK_I(inode)->removed && inode->i_sb->s_fs_info && nk_vol(inode->i_sb)) {
		unsigned int saved = nk_enter();

		nk_delete_unlinked(nk_vol(inode->i_sb), nk_ref(inode));
		nk_leave(saved);
	}
}

static int nk_statfs_op(struct dentry *dentry, struct kstatfs *stat)
{
	struct super_block *sb = dentry->d_sb;
	struct nk_space space;
	unsigned int saved = nk_enter();
	int err = nk_space(nk_vol(sb), &space);

	nk_leave(saved);
	if (err)
		return err;
	stat->f_type = NK_MAGIC;
	stat->f_bsize = space.cluster_bytes;
	stat->f_blocks = space.clusters;
	stat->f_bfree = space.free_clusters;
	stat->f_bavail = space.free_clusters;
	stat->f_namelen = 255;
	return 0;
}

static int nk_sync_fs(struct super_block *sb, int wait)
{
	unsigned int saved = nk_enter();

	nk_settle(nk_vol(sb));
	nk_leave(saved);
	return wait ? sync_blockdev(sb->s_bdev) : 0;
}

static void nk_put_super(struct super_block *sb)
{
	struct nk_sb *sbi = sb->s_fs_info;
	unsigned int saved = nk_enter();

	if (sbi->volume)
		nk_unmount(sbi->volume);
	sbi->volume = NULL;
	nk_forget_blocks(sbi);
	nk_leave(saved);
}

static const struct super_operations nk_sops = {
	.alloc_inode = nk_alloc_inode,
	.free_inode = nk_free_inode,
	.write_inode = nk_write_inode,
	.statfs = nk_statfs_op,
	.evict_inode = nk_evict,
	.put_super = nk_put_super,
	.sync_fs = nk_sync_fs,
};

static int nk_fill_super(struct super_block *sb, struct fs_context *fc)
{
	struct nk_sb *sbi = kzalloc(sizeof(*sbi), GFP_KERNEL);
	struct nk_attr attr;
	struct inode *root;
	unsigned int saved;
	int err;

	if (!sbi)
		return -ENOMEM;
	sb->s_fs_info = sbi;
	sb->s_magic = NK_MAGIC;
	sb->s_op = &nk_sops;
	sb->s_maxbytes = MAX_LFS_FILESIZE;
	sb->s_time_gran = 100;
	/* What a signed count of 100 ns ticks since 1601 can hold; a count of zero means "leave as it is" to the core. */
	sb->s_time_min = 1 - (s64)(NK_TICKS_TO_1970 / 10000000ULL);
	sb->s_time_max = S64_MAX / 10000000 - (s64)(NK_TICKS_TO_1970 / 10000000ULL);
	sbi->uid = current_fsuid();
	sbi->gid = current_fsgid();
	file_ra_state_init(&sbi->ra, sb->s_bdev->bd_mapping);
	sbi->blocks = bdev_nr_bytes(sb->s_bdev) >> PAGE_SHIFT;
	if (!sb_set_blocksize(sb, PAGE_SIZE)) {
		pr_err("ntfslib: cannot use %lu-byte blocks on this device\n", PAGE_SIZE);
		return -EINVAL;
	}
	saved = nk_enter();
	err = nk_mount(sb, bdev_logical_block_size(sb->s_bdev), sb_rdonly(sb), &sbi->volume, &sbi->root);
	if (!err)
		err = nk_getattr(sbi->volume, sbi->root, &attr);
	if (!err) {
		struct nk_space space;

		err = nk_space(sbi->volume, &space);
		sbi->volume_bytes = err ? 0 : space.clusters * space.cluster_bytes;
	}
	nk_leave(saved);
	root = err ? ERR_PTR(err) : nk_iget(sb, &attr);
	if (!IS_ERR(root))
		sb->s_root = d_make_root(root);
	if (sb->s_root)
		return 0;
	err = IS_ERR(root) ? PTR_ERR(root) : -ENOMEM;
	pr_err("ntfslib: opening the volume failed (%d)\n", err);
	/* Without a root the superblock is torn down without put_super. */
	nk_put_super(sb);
	return err;
}

static int nk_get_tree(struct fs_context *fc)
{
	return get_tree_bdev(fc, nk_fill_super);
}

static const struct fs_context_operations nk_context_ops = {
	.get_tree = nk_get_tree,
};

static int nk_init_fs_context(struct fs_context *fc)
{
	fc->ops = &nk_context_ops;
	return 0;
}

static void nk_kill_sb(struct super_block *sb)
{
	struct nk_sb *sbi = sb->s_fs_info;

	kill_block_super(sb);
	if (sbi)
		kvfree(sbi->list_buffer);
	kfree(sbi);
}

static struct file_system_type nk_fs_type = {
	.owner = THIS_MODULE,
	.name = "ntfslib",
	.init_fs_context = nk_init_fs_context,
	.kill_sb = nk_kill_sb,
	.fs_flags = FS_REQUIRES_DEV,
};

static void nk_inode_once(void *object)
{
	inode_init_once(&((struct nk_inode *)object)->vfs);
}

static int __init nk_init(void)
{
	int err;

	nk_inode_cache = kmem_cache_create("ntfslib_inode", sizeof(struct nk_inode), 0,
					   SLAB_RECLAIM_ACCOUNT | SLAB_ACCOUNT, nk_inode_once);
	if (!nk_inode_cache)
		return -ENOMEM;
	err = register_filesystem(&nk_fs_type);
	if (err)
		kmem_cache_destroy(nk_inode_cache);
	return err;
}

static void __exit nk_exit(void)
{
	unregister_filesystem(&nk_fs_type);
	nk_drain_pool();
	rcu_barrier();
	kmem_cache_destroy(nk_inode_cache);
}

module_init(nk_init);
module_exit(nk_exit);
MODULE_DESCRIPTION("NTFS through the LiberNT NTFS library");
MODULE_LICENSE("Dual MIT/GPL");
MODULE_ALIAS_FS("ntfslib");
