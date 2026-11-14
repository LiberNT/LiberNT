/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * A read-only F2FS reader for boot-stage code (FreeLoader).
 *
 * Portable C99 with no libc beyond <stdint.h>/<string.h>: the bootloader provides
 * `f2fs_dev_read(device, byte_offset, buffer, length)` and the reader does everything else.  All
 * offsets below are the on-disk ones and are checked against the Rust core's `layout.rs`, which is
 * oracle-tested against `mkfs.f2fs`/`sload.f2fs` images -- this file must not invent a layout.
 *
 * The derived geometry mirrors `crates/f2fs-core/src/layout.rs`:
 *
 *   SIZE_OF_DIR_ENTRY    11        `struct f2fs_dir_entry` (hash u32, ino u32, name_len u16,
 *                                  file_type u8) -- the *name bytes are not in it*; they live in a
 *                                  separate `filename[NR_DENTRY_IN_BLOCK][F2FS_SLOT_LEN]` array, and
 *                                  a name of L bytes occupies ceil(L/8) consecutive 8-byte slots.
 *   F2FS_SLOT_LEN         8        one name slot
 *   NR_DENTRY_IN_BLOCK  214        (8*4096) / ((11+8)*8 + 1)
 *   bitmap               27        ceil(214/8), LSB-first bit per slot
 *   reserved              3        4096 - (19*214 + 27)
 *   DENTRY_OFF           30        bitmap + reserved;  FILENAME_OFF 2384 = 30 + 214*11
 *
 * Inline directories use the same shape inside the inode (see `inline_dentry_geometry`), with the
 * slot count derived from MAX_INLINE_DATA, which in turn depends on `i_extra_isize` and the inline
 * xattr reservation -- so it is computed per inode, never assumed to be 214.
 */
#ifndef F2FS_BOOT_H
#define F2FS_BOOT_H

#include <stdint.h>
#include <stddef.h>

#define F2FS_BLK			4096u
#define F2FS_SUPER_OFFSET		1024u
#define F2FS_MAGIC			0xF2F52010u
#define F2FS_MAX_ACTIVE_LOGS		16
#define F2FS_NAT_ENTRY_SIZE		9u
#define F2FS_NAT_ENTRY_PER_BLOCK	(F2FS_BLK / F2FS_NAT_ENTRY_SIZE)
/* `SUM_JOURNAL_OFF` = ENTRIES_IN_SUM * sizeof(struct f2fs_summary) = 512 * 7, and the journal
 * ends where the summary footer begins (`SUM_FOOTER_OFF` 4091).  Both pinned in layout.rs. */
#define F2FS_SUM_JOURNAL_OFF		3584u
#define F2FS_SUM_JOURNAL_SIZE		507u
/* `NAT_JOURNAL_ENTRIES` = (SUM_JOURNAL_SIZE - 2) / sizeof(struct nat_journal_entry) */
#define F2FS_NAT_JOURNAL_ENTRY_SIZE	13u
#define F2FS_NAT_JOURNAL_ENTRIES	((F2FS_SUM_JOURNAL_SIZE - 2u) / F2FS_NAT_JOURNAL_ENTRY_SIZE)
#define F2FS_DEF_ADDRS_PER_INODE	923u
#define F2FS_DEF_NIDS_PER_INODE		5u
#define F2FS_ADDRS_PER_BLOCK		1018u
#define F2FS_NIDS_PER_BLOCK		1018u
#define F2FS_NODE_FOOTER_SIZE		24u
#define F2FS_NODE_BODY_SIZE		(F2FS_BLK - F2FS_NODE_FOOTER_SIZE)
#define F2FS_NAME_LEN			255u
#define F2FS_MAX_PATH			260u
#define F2FS_MAX_NODE_CACHE		8

/*
 * **An allowlist, not a denylist.**  These are the feature bits whose effect on the read path this reader
 * implements; anything else -- including a bit the format gains after this loader was written -- is
 * refused by name (F2FS_ENOTSUP).  A loader that boots a volume it does not understand reports success
 * wrongly, and that is the one failure there is no recovering from: the user is told nothing and the bytes
 * come from a format this code does not implement.
 *
 * Deliberately absent, each because it changes what the bytes *mean*:
 *   ENCRYPT (0x1)     the bytes on disk are not the file's bytes;
 *   BLKZONED (0x2)    block addressing goes through zones, so a linear read is not the file;
 *   VERITY (0x400)    the bytes are not what a reader may hand over unverified;
 *   COMPRESS (0x8000) handled by COMPRESS_ADDR in the inode, which the data path refuses separately.
 */
#define F2FS_FEATURE_EXTRA_ATTR		0x00000008u
#define F2FS_FEATURE_PRJQUOTA		0x00000010u
#define F2FS_FEATURE_INODE_CHKSUM	0x00000020u
#define F2FS_FEATURE_QUOTA_INO		0x00000080u
#define F2FS_FEATURE_INODE_CRTIME	0x00000100u
#define F2FS_FEATURE_LOST_FOUND		0x00000200u
#define F2FS_FEATURE_SB_CHKSUM		0x00000800u
#define F2FS_BOOT_SUPPORTED		(F2FS_FEATURE_EXTRA_ATTR | F2FS_FEATURE_PRJQUOTA | \
					 F2FS_FEATURE_INODE_CHKSUM | F2FS_FEATURE_FLEXIBLE_INLINE_XATTR | \
					 F2FS_FEATURE_QUOTA_INO | F2FS_FEATURE_INODE_CRTIME | \
					 F2FS_FEATURE_LOST_FOUND | F2FS_FEATURE_SB_CHKSUM)
#define F2FS_MAX_SYMLINK		4096u
#define F2FS_DIAG_LEN			256u

/* Directory geometry -- see the header comment for the derivation. */
#define F2FS_SIZE_OF_DIR_ENTRY		11u
#define F2FS_SLOT_LEN			8u
#define F2FS_NR_DENTRY_IN_BLOCK		214u
#define F2FS_DENTRY_BITMAP_SIZE		27u
#define F2FS_DENTRY_RESERVED_SIZE	3u
#define F2FS_DENTRY_OFF			30u
#define F2FS_FILENAME_OFF		2384u
#define F2FS_DEF_INLINE_RESERVED_SIZE	1u
#define F2FS_DEFAULT_INLINE_XATTR_ADDRS	50u
#define F2FS_MAX_DIR_HASH_DEPTH		63u
#define F2FS_MAX_DIR_BUCKETS		(1u << ((F2FS_MAX_DIR_HASH_DEPTH / 2) - 1))

/*
 * A deliberate bound, not the format's: the on-disk size of a directory is a hint, so the loops
 * that walk it stop here and report corruption instead of spinning on a bogus `i_size`.  F2FS's own
 * ceiling is far larger (a directory may address the whole file ceiling), but a boot loader that has
 * already read 256 MiB of directory blocks has a broken volume, not a slow one.
 */
#define F2FS_MAX_DIR_BLOCKS		65536u

/* Block pointers (`layout.rs`: NULL_ADDR / NEW_ADDR / COMPRESS_ADDR). */
#define F2FS_NULL_ADDR			0u
#define F2FS_NEW_ADDR			0xFFFFFFFFu
#define F2FS_COMPRESS_ADDR		0xFFFFFFFEu

/* `i_nid[]` sentinels: `NODE_DIR1_BLOCK`..`NODE_DIND_BLOCK`, index = sentinel - NODE_DIR1_BLOCK. */
#define F2FS_NODE_DIR1_BLOCK		(F2FS_DEF_ADDRS_PER_INODE + 1u)
#define F2FS_NODE_DIR2_BLOCK		(F2FS_DEF_ADDRS_PER_INODE + 2u)
#define F2FS_NODE_IND1_BLOCK		(F2FS_DEF_ADDRS_PER_INODE + 3u)
#define F2FS_NODE_IND2_BLOCK		(F2FS_DEF_ADDRS_PER_INODE + 4u)
#define F2FS_NODE_DIND_BLOCK		(F2FS_DEF_ADDRS_PER_INODE + 5u)

/* `i_inline` flags (`layout.rs`).  They are the LOW bits of the field, not the high ones. */
#define F2FS_INLINE_XATTR		0x01u
#define F2FS_INLINE_DATA		0x02u
#define F2FS_INLINE_DENTRY		0x04u
#define F2FS_DATA_EXIST			0x08u
#define F2FS_EXTRA_ATTR			0x20u

/* Superblock feature bits we have to honour on the read path (`layout.rs` FEATURE_*). */
#define F2FS_FEATURE_FLEXIBLE_INLINE_XATTR 0x00000040u

/* Checkpoint flags (`layout.rs` CP_*), only the ones the read path consults. */
#define F2FS_CP_COMPACT_SUM_FLAG	0x00000004u
#define F2FS_CP_LARGE_NAT_BITMAP_FLAG	0x00000400u

/* `F2FS_FT_*`: the file type stored in a directory entry. */
#define F2FS_FT_UNKNOWN			0u
#define F2FS_FT_REG_FILE		1u
#define F2FS_FT_DIR			2u
#define F2FS_FT_CHRDEV			3u
#define F2FS_FT_BLKDEV			4u
#define F2FS_FT_FIFO			5u
#define F2FS_FT_SOCK			6u
#define F2FS_FT_SYMLINK			7u

/* `i_mode` type bits, so callers do not need <sys/stat.h> in a boot loader. */
#define F2FS_S_IFMT			0xF000u
#define F2FS_S_IFIFO			0x1000u
#define F2FS_S_IFCHR			0x2000u
#define F2FS_S_IFDIR			0x4000u
#define F2FS_S_IFBLK			0x6000u
#define F2FS_S_IFREG			0x8000u
#define F2FS_S_IFLNK			0xA000u
#define F2FS_S_IFSOCK			0xC000u

/* Return codes.  Negative, errno-shaped; the boot loader prints `f2fs_diag()` beside them. */
#define F2FS_OK				0
#define F2FS_EIO			(-1)	/* the device callback failed */
#define F2FS_ENOENT			(-2)	/* no such name */
#define F2FS_EPROTO			(-3)	/* the on-disk structure is inconsistent */
#define F2FS_ENOTDIR			(-4)	/* a path component is not a directory */
#define F2FS_ENOTSUP			(-5)	/* a format feature this reader refuses by name */
#define F2FS_EINVAL			(-6)	/* a caller argument (path, buffer) is unusable */
#define F2FS_EFBIG			(-7)	/* past the addressable space, or a bound was hit */

/* Checksum range: the checkpoint's own `checksum_offset` is read from the structure. */
struct f2fs_volume {
	uint32_t device;
	uint32_t log_blocks_per_seg;
	uint32_t blocks_per_seg;
	uint32_t cp_blkaddr;
	uint32_t nat_blkaddr;
	uint32_t main_blkaddr;
	uint32_t root_ino;
	uint32_t segment_count_nat;
	uint32_t sit_bitmap_bytesize;
	uint32_t nat_bitmap_bytesize;
	uint64_t block_count;
	uint64_t checkpoint_ver;
	uint32_t ckpt_flags;		/* CP_COMPACT_SUM_FLAG decides where the NAT journal lives */

	/* Superblock feature bits: FLEXIBLE_INLINE_XATTR changes where the inline xattr area starts. */
	uint32_t feature;

	/*
	 * The NAT *version* bitmap lives inside the checkpoint pack, and the pack moves, so copy what
	 * fits.  It is a hint, not the only way to pick a NAT block's copy: a lookup whose block is
	 * outside this copy still resolves, because the node footer proves which copy is live (see
	 * `node_read`).
	 */
	uint8_t nat_bitmap[64];

	/* Replayed NAT journal entries, exactly as the checkpoint carried them. */
	struct {
		uint32_t nid;
		uint8_t version;
		uint32_t ino;
		uint32_t block_addr;
	} nat_journal[F2FS_NAT_JOURNAL_ENTRIES];
	uint32_t nat_journal_count;

	/*
	 * The last lookup/read diagnostic, printable by the caller.  A boot loader has no console
	 * protocol of its own, so the reader formats a two-line message here and the loader prints it:
	 * a component that was not found names the parent directory and how many entries it holds,
	 * which is what distinguishes "wrong path" from "wrong case" from "wrong volume".
	 */
	char diag[F2FS_DIAG_LEN];

	/* Components that only resolved by case folding: a warning, not an error. */
	uint32_t fold_hits;
};

struct f2fs_inode {
	uint32_t ino;
	uint64_t size;
	uint32_t mode;

	/* `i_inline` flags, raw and decoded. */
	uint8_t inline_flags;
	uint8_t has_inline_data;	/* content lives in the inode block */
	uint8_t has_inline_dentry;	/* directory entries live in the inode block */
	uint8_t has_inline_xattr;
	uint8_t has_extra_attr;
	uint8_t has_data_exist;		/* inline area holds content, not just reserved space */

	/* Layout scalars resolved from the inode plus the volume's feature bits (`InodeLayout`). */
	uint32_t extra_isize_units;	/* i_extra_isize / 4: shift applied to i_addr[] */
	uint32_t inline_xattr_addrs;	/* effective reservation at the end of i_addr[] */
	uint32_t addrs_per_inode;	/* usable direct block pointers (923 - extra - xattr) */
	uint32_t max_inline_data;	/* bytes of inline area (after the 4-byte reservation) */

	uint32_t parent_ino;		/* i_pino */
	uint32_t current_depth;		/* i_current_depth: how many hash levels to search */
	uint8_t dir_level;		/* i_dir_level */

	uint8_t node[F2FS_BLK];		/* the inode's own block, kept for inline content */
	uint32_t addr[F2FS_DEF_ADDRS_PER_INODE];
	uint32_t nid[F2FS_DEF_NIDS_PER_INODE];
};

/* Provided by the caller.  Returns 0 on success. */
int f2fs_dev_read(uint32_t device, uint64_t byte_offset, void *buffer, uint32_t length);

/* Probe: superblock, checkpoint (both packs), NAT journal, NAT bitmap. */
int f2fs_probe(uint32_t device, struct f2fs_volume *vol);

/*
 * Resolve `path` (absolute or relative to the root, separated by '/' or '\') and load the inode.
 * Every component is matched exactly first and then case-insensitively; see `f2fs_diag()` for the
 * diagnostic on failure and for the warning when a component only matched by folding.
 */
int f2fs_lookup(struct f2fs_volume *vol, const char *path, struct f2fs_inode *out);

/*
 * Read up to `length` bytes at `offset`.  Short at EOF (POSIX semantics), holes read as zeros,
 * inline data comes out of the inode block.  `read_out` is always written when it is non-NULL.
 */
int f2fs_read(struct f2fs_volume *vol, const struct f2fs_inode *inode, uint64_t offset,
	      void *buffer, uint64_t length, uint64_t *read_out);

/*
 * Enumerate a directory.  `.` and `..` are returned: F2FS stores them as real dentries and the
 * kernel does not synthesise them, so a caller that recurses must skip them itself.
 *
 * `visit` returns 0 to continue or non-zero to stop; a non-zero return becomes `f2fs_readdir`'s
 * return value.  `entries_out` (optional) receives the number of entries visited.
 */
typedef int (*f2fs_dir_visit)(void *ctx, const char *name, uint32_t name_len, uint32_t ino,
			      uint8_t file_type, uint32_t slot);

int f2fs_readdir(struct f2fs_volume *vol, const struct f2fs_inode *dir, f2fs_dir_visit visit,
		 void *ctx, uint32_t *entries_out);

/* A symlink's target: file content, so `i_size` bytes from `f2fs_read`.  NUL-terminated. */
int f2fs_readlink(struct f2fs_volume *vol, const struct f2fs_inode *inode, char *buffer,
		  uint32_t capacity, uint32_t *len_out);

/* The block address backing file block `block` (0 = hole). */
int f2fs_block_addr(struct f2fs_volume *vol, const struct f2fs_inode *inode, uint64_t block,
		    uint32_t *addr_out);

/* The last diagnostic (never NULL; empty after a successful exact-match call). */
const char *f2fs_diag(const struct f2fs_volume *vol);

/* Name of a `F2FS_FT_*` value, for listings and diagnostics. */
const char *f2fs_file_type_name(uint8_t file_type);

#endif
