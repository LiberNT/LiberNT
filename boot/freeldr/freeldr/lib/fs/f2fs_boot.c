/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Read-only F2FS: superblock, checkpoint, NAT, inode, dentry and data.
 *
 * Every on-disk offset used below is named with the Rust core's field it corresponds to
 * (`crates/f2fs-core/src/layout.rs`), because a hand-guessed offset in this file has been wrong
 * before -- `i_addr` is at 360 and `i_nid` at 4052 only because `i_mode`(2) + `i_advise`(1) +
 * `i_inline`(1) + ... + `i_name[255]` + `i_dir_level`(1) + `i_ext`(12) add up to 360, and the two
 * arrays are 923*4 and 5*4 bytes before the 24-byte node footer at 4072.
 */
#include "f2fs_boot.h"
#include <stdio.h>
#include <string.h>

/* Compile-time checks on the derived geometry: if one of these is a negative array size, an
 * offset in this file no longer agrees with layout.rs and the build stops. */
typedef char f2fs_check_dentry_off
	[(F2FS_DENTRY_OFF == F2FS_DENTRY_BITMAP_SIZE + F2FS_DENTRY_RESERVED_SIZE) ? 1 : -1];
typedef char f2fs_check_filename_off
	[(F2FS_FILENAME_OFF == F2FS_DENTRY_OFF + F2FS_NR_DENTRY_IN_BLOCK * F2FS_SIZE_OF_DIR_ENTRY)
		? 1 : -1];
typedef char f2fs_check_dentry_block
	[(F2FS_FILENAME_OFF + F2FS_NR_DENTRY_IN_BLOCK * F2FS_SLOT_LEN == F2FS_BLK) ? 1 : -1];
typedef char f2fs_check_node_body
	[(360u + F2FS_DEF_ADDRS_PER_INODE * 4u + F2FS_DEF_NIDS_PER_INODE * 4u
	  == F2FS_NODE_BODY_SIZE) ? 1 : -1];

static uint32_t rd16(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
	((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint64_t rd64(const uint8_t *p) { return (uint64_t)rd32(p) | ((uint64_t)rd32(p + 4) << 32); }

/* f2fs's checksum: the table-driven CRC-32 with seed `F2FS_SUPER_MAGIC` and no final xor. */
static uint32_t f2fs_crc(uint32_t crc, const uint8_t *p, size_t len)
{
	for (size_t i = 0; i < len; i++) {
		crc ^= p[i];
		for (int k = 0; k < 8; k++)
			crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320u : 0u);
	}
	return crc;
}

/* ==========================================================================================
 * The directory-entry hash: a direct port of `crates/f2fs-core/src/hash.rs`, which is itself a
 * port of `fs/f2fs/hash.c` (TEA_hash_name / str2hashbuf / TEA_transform) and is pinned by unit
 * tests to hash values read out of a real mkfs.f2fs image.  A lookup that gates on a wrong hash
 * silently reports "not found", so this is not a place to improvise: the seed words, the
 * four words fed to each 16-round transform and the fold back into `buf` are all load-bearing.
 * ========================================================================================== */
#define F2FS_TEA_DELTA	0x9E3779B9u

static const uint32_t f2fs_tea_seed[4] = {
	0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u
};

/* `str2hashbuf()`: fold up to `num * 4` bytes into `num` words.  The padding word is derived from
 * the *whole remaining length* while only the first `num * 4` bytes are consumed. */
static void str2hashbuf(const uint8_t *msg, uint32_t len_in, int32_t num, uint32_t out[4])
{
	uint32_t pad = len_in | (len_in << 8);
	uint32_t val, len;
	int32_t left = num;
	uint32_t w = 0;

	pad |= pad << 16;
	val = pad;
	len = len_in;
	if (len > (uint32_t)num * 4u) len = (uint32_t)num * 4u;

	for (uint32_t i = 0; i < len; i++) {
		if ((i & 3u) == 0) val = pad;
		val = (uint32_t)msg[i] + (val << 8);
		if ((i & 3u) == 3u) {
			if (w < 4) out[w++] = val;
			val = pad;
			left--;
		}
	}
	left--;
	if (left >= 0 && w < 4) out[w++] = val;
	for (;;) {
		left--;
		if (left < 0) break;
		if (w < 4) out[w++] = pad;
	}
}

/* `TEA_transform()`: 16 rounds, then fold the result back into `buf`. */
static void tea_transform(uint32_t buf[4], const uint32_t in[4])
{
	uint32_t sum = 0;
	uint32_t b0 = buf[0], b1 = buf[1];
	const uint32_t a = in[0], b = in[1], c = in[2], d = in[3];

	for (uint32_t i = 0; i < 16; i++) {
		sum += F2FS_TEA_DELTA;
		b0 += ((b1 << 4) + a) ^ (b1 + sum) ^ ((b1 >> 5) + b);
		b1 += ((b0 << 4) + c) ^ (b0 + sum) ^ ((b0 >> 5) + d);
	}
	buf[0] += b0;
	buf[1] += b1;
}

/* `TEA_hash_name()`: the value stored in `f2fs_dir_entry::hash_code`. */
static uint32_t tea_hash_name(const uint8_t *name, uint32_t len)
{
	uint32_t buf[4];
	uint32_t off = 0, rem = len;

	buf[0] = f2fs_tea_seed[0];
	buf[1] = f2fs_tea_seed[1];
	buf[2] = f2fs_tea_seed[2];
	buf[3] = f2fs_tea_seed[3];

	for (;;) {
		uint32_t in[4] = { 0, 0, 0, 0 };
		const uint8_t *chunk = name + (off <= len ? off : len);

		str2hashbuf(chunk, rem, 4, in);
		tea_transform(buf, in);
		off += 16;
		if (rem <= 16) break;
		rem -= 16;
	}
	/* The C expression is `buf[0] & ~F2FS_HASH_COL_BIT`; the collision bit is bit 63 of a u64
	 * macro while buf[0] is 32 bits, so the mask is a no-op. */
	return buf[0];
}

/* `dentry_hash()` / `f2fs_hash_filename()` for an unencrypted, unfolded name: "." and ".." hash to
 * zero by definition and everything else goes through TEA. */
static uint32_t dentry_hash(const uint8_t *name, uint32_t len)
{
	if (len == 1 && name[0] == '.') return 0;
	if (len == 2 && name[0] == '.' && name[1] == '.') return 0;
	return tea_hash_name(name, len);
}

/* ==========================================================================================
 * Diagnostics
 * ========================================================================================== */
struct diagbuf {
	char *p;
	uint32_t len;
	uint32_t cap;
};

static void dg_init(struct diagbuf *d, char *buf, uint32_t cap)
{
	d->p = buf;
	d->len = 0;
	d->cap = cap;
	if (cap) buf[0] = 0;
}

static void dg_ch(struct diagbuf *d, char c)
{
	if (d->len + 1 < d->cap) {
		d->p[d->len++] = c;
		d->p[d->len] = 0;
	}
}

static void dg_mem(struct diagbuf *d, const void *src, uint32_t n)
{
	const char *s = (const char *)src;
	for (uint32_t i = 0; i < n; i++) dg_ch(d, s[i]);
}

static void dg_str(struct diagbuf *d, const char *s)
{
	while (*s) dg_ch(d, *s++);
}

static void dg_u32(struct diagbuf *d, uint32_t v)
{
	char tmp[11];
	uint32_t n = 0;

	do { tmp[n++] = (char)('0' + (v % 10u)); v /= 10u; } while (v);
	while (n) dg_ch(d, tmp[--n]);
}

static void dg_mode(struct diagbuf *d, uint32_t mode)
{
	char tmp[12];
	uint32_t n = 0;

	dg_str(d, "0");
	do { tmp[n++] = (char)('0' + (mode & 7u)); mode >>= 3; } while (mode);
	while (n) dg_ch(d, tmp[--n]);
}

const char *f2fs_diag(const struct f2fs_volume *vol)
{
	return vol->diag;
}

const char *f2fs_file_type_name(uint8_t file_type)
{
	switch (file_type) {
	case F2FS_FT_REG_FILE: return "file";
	case F2FS_FT_DIR:      return "dir";
	case F2FS_FT_CHRDEV:   return "chrdev";
	case F2FS_FT_BLKDEV:   return "blkdev";
	case F2FS_FT_FIFO:     return "fifo";
	case F2FS_FT_SOCK:     return "sock";
	case F2FS_FT_SYMLINK:  return "symlink";
	default:               return "unknown";
	}
}

/* ==========================================================================================
 * Probe: superblock, both checkpoint packs, NAT journal, NAT bitmap.  (This layer is unchanged
 * from the working version -- only the feature word is read, because the inline xattr
 * reservation depends on it.)
 * ========================================================================================== */
static int read_block(uint32_t device, uint32_t blk, void *buf)
{
	return f2fs_dev_read(device, (uint64_t)blk * F2FS_BLK, buf, F2FS_BLK);
}

/* Reads one checkpoint pack and reports whether its checksum holds. */
static int try_checkpoint(struct f2fs_volume *vol, uint32_t pack_addr,
			  uint8_t *out, uint64_t *ver_out)
{
	uint32_t checksum_offset, stored, calc;

	if (read_block(vol->device, pack_addr, out)) return -1;
	if (rd32(out) == 0 &&
	    rd64(out) == 0) return -1;			/* a zeroed pack is a candidate, not a checkpoint */
	checksum_offset = rd32(out + 0xA4);		/* checksum_offset */
	if (checksum_offset < 4 || checksum_offset > F2FS_BLK - 4) return -1;
	stored = rd32(out + checksum_offset);
	calc = f2fs_crc(F2FS_MAGIC, out, checksum_offset);
	if (stored != calc) return -1;
	*ver_out = rd64(out);
	return 0;
}

int f2fs_probe(uint32_t device, struct f2fs_volume *vol)
{
	uint8_t sb[F2FS_BLK], cp_one[F2FS_BLK], cp_two[F2FS_BLK], sum[F2FS_BLK];
	uint64_t ver_one = 0, ver_two = 0;
	const uint8_t *cp;
	uint32_t start_sum, blocks_per_seg, cp_addr;
	int got_one, got_two;

	memset(vol, 0, sizeof(*vol));
	vol->device = device;

	if (f2fs_dev_read(device, F2FS_SUPER_OFFSET, sb, F2FS_BLK)) return -1;
	if (rd32(sb) != F2FS_MAGIC) return -2;

	/*
	 * `feature` at 0x884, derived rather than remembered: `root_ino` is at 0x60 (the reader already
	 * uses it), and the field order in `layout.rs` puts feature after uuid[16], volume_name[512],
	 * extension_count, extension_list[64][8], cp_payload, version[256] and init_version[256].
	 * Confirmed against the Rust oracle -- patching a copy at this offset makes `f2fsnt info` report
	 * `features: 0x00000400`, and at 0x6C (which is uuid) it does not.
	 */
	vol->feature = rd32(sb + 0x884);
	/*
	 * **Refuse by name.**  A loader that boots a volume it cannot honour reports success wrongly, which
	 * is the one failure mode there is no recovering from: the user is told nothing and the bytes are
	 * from a format this code does not implement.  Naming the bits is what makes the refusal actionable.
	 */
	{
		uint32_t bits = vol->feature & ~F2FS_BOOT_SUPPORTED;
		if (bits) {
			(void)snprintf(vol->diag, sizeof(vol->diag),
				"f2fs: unsupported feature bits 0x%08x: this loader cannot read this volume "
				"(use a newer boot loader, or format without those features)", bits);
			return -6;
		}
	}

	vol->block_count = rd64(sb + 0x24);
	vol->blocks_per_seg = 1u << rd32(sb + 0x14);
	vol->log_blocks_per_seg = rd32(sb + 0x14);
	vol->cp_blkaddr = rd32(sb + 0x4C);
	vol->nat_blkaddr = rd32(sb + 0x54);
	vol->main_blkaddr = rd32(sb + 0x5C);
	vol->root_ino = rd32(sb + 0x60);
	vol->segment_count_nat = rd32(sb + 0x3C);
	/* `F2fsSuperBlock::feature` is the 32-bit word at 2180: magic..segment_count_nat (64) +
	 * segment_count_ssa..main_blkaddr (32) + root/node/meta_ino (12) + uuid (16) +
	 * volume_name (1024) + extension_count (4) + extension_list (512) + cp_payload (4) +
	 * version (256) + init_version (256). */
	vol->feature = rd32(sb + 2180);
	blocks_per_seg = vol->blocks_per_seg;

	/* Both packs are candidates: the last commit wrote one and the other holds an older checkpoint,
	 * so taking pack One blindly fails on every volume whose last commit alternated. */
	cp_addr = vol->cp_blkaddr;
	got_one = try_checkpoint(vol, cp_addr, cp_one, &ver_one) == 0;
	got_two = try_checkpoint(vol, cp_addr + blocks_per_seg, cp_two, &ver_two) == 0;
	if (!got_one && !got_two) return -3;
	if (got_one && got_two) cp = (ver_one >= ver_two) ? cp_one : cp_two;
	else cp = got_one ? cp_one : cp_two;
	vol->checkpoint_ver = got_one && got_two ? (ver_one >= ver_two ? ver_one : ver_two)
						  : (got_one ? ver_one : ver_two);
	{
		uint32_t use = (cp == cp_one) ? cp_addr : cp_addr + blocks_per_seg;
		uint32_t journal_off;

		start_sum = rd32(cp + 0x8C);		/* cp_pack_start_sum */
		vol->ckpt_flags = rd32(cp + 0x84);	/* ckpt_flags */
		vol->sit_bitmap_bytesize = rd32(cp + 0x9C);
		vol->nat_bitmap_bytesize = rd32(cp + 0xA0);

		/* **Replay the NAT journal.**  Where it lives depends on CP_COMPACT_SUM_FLAG
		 * (`Checkpoint::journal_location`): a compacted pack puts the NAT journal at offset 0 of
		 * the first data-summary block and the SIT journal right after it; a normal pack puts each
		 * journal at SUM_JOURNAL_OFF of its own data-summary block (NAT in block 0, SIT in block 2).
		 * Reading offset 0 unconditionally -- which this probe used to do -- parses *summary
		 * entries* as journal entries on any volume whose pack is not compacted, and a summary
		 * entry's `nid` word then shadows the real NAT entry for that node id. */
		journal_off = (vol->ckpt_flags & F2FS_CP_COMPACT_SUM_FLAG) ? 0u : F2FS_SUM_JOURNAL_OFF;
		{
			const uint8_t *journal;
			uint32_t n, off;

			if (read_block(device, use + start_sum, sum)) return -4;
			journal = sum + journal_off;
			n = rd16(journal);
			if (n > F2FS_NAT_JOURNAL_ENTRIES) return -5;	/* not a count this format can hold */
			off = 2;
			for (uint32_t i = 0; i < n; i++, off += F2FS_NAT_JOURNAL_ENTRY_SIZE) {
				vol->nat_journal[i].nid = rd32(journal + off);
				vol->nat_journal[i].version = journal[off + 4];
				vol->nat_journal[i].ino = rd32(journal + off + 5);
				vol->nat_journal[i].block_addr = rd32(journal + off + 9);
			}
			vol->nat_journal_count = n;
		}

		/* The NAT version bitmap decides which of a NAT block's two copies is current.  It sits
		 * after the SIT bitmap inside the pack -- except under CP_LARGE_NAT_BITMAP_FLAG, where the
		 * two halves swap (`__bitmap_ptr`). */
		{
			const uint8_t *bitmaps = cp + 192;	/* CP_MIN_CHKSUM_OFFSET */
			uint32_t nat_off;

			if (vol->ckpt_flags & F2FS_CP_LARGE_NAT_BITMAP_FLAG)
				nat_off = 0;
			else if (vol->sit_bitmap_bytesize != 0)
				nat_off = vol->sit_bitmap_bytesize;
			else
				nat_off = 0;
			if (vol->nat_bitmap_bytesize > sizeof(vol->nat_bitmap))
				vol->nat_bitmap_bytesize = sizeof(vol->nat_bitmap);
			if (nat_off + vol->nat_bitmap_bytesize <= F2FS_BLK)
				memcpy(vol->nat_bitmap, bitmaps + nat_off, vol->nat_bitmap_bytesize);
			else
				vol->nat_bitmap_bytesize = 0;
		}
	}
	return 0;
}

/*
 * NAT block `index` has two homes inside one NAT segment: `base` and `base + blocks_per_seg`.
 * `current_nat_addr()` in `fs/f2fs/node.h` picks between them with one bit of the checkpoint's
 * version bitmap -- and that bitmap is MSB-first (`bit::test_bit_msb`), unlike the dentry bitmap,
 * which is LSB-first.  Reading it LSB-first silently selects the *other* copy for half the blocks.
 *
 * The bitmap is treated as the fast path, not as the only answer: `alt_addr` receives the other
 * copy so `node_read` can verify the node footer and flip when the chosen copy turns out to be
 * stale (or when the NAT block lies outside the part of the bitmap this loader keeps).
 */
static uint32_t nat_pair_base(struct f2fs_volume *vol, uint32_t index)
{
	uint32_t seg = index >> vol->log_blocks_per_seg;

	return vol->nat_blkaddr + (seg << (vol->log_blocks_per_seg + 1)) +
	       (index & (vol->blocks_per_seg - 1));
}

static int nat_lookup(struct f2fs_volume *vol, uint32_t nid, uint32_t *block_addr, uint32_t *ino,
		      uint32_t *alt_addr)
{
	uint32_t index;

	*alt_addr = 0;
	for (uint32_t i = 0; i < vol->nat_journal_count; i++) {
		if (vol->nat_journal[i].nid == nid) {
			*block_addr = vol->nat_journal[i].block_addr;
			*ino = vol->nat_journal[i].ino;
			return *block_addr ? F2FS_OK : F2FS_ENOENT;
		}
	}
	{
		uint8_t blk[F2FS_BLK];
		uint32_t base, addr;
		uint32_t ent_off = (nid % F2FS_NAT_ENTRY_PER_BLOCK) * F2FS_NAT_ENTRY_SIZE;
		int second = 0;

		index = nid / F2FS_NAT_ENTRY_PER_BLOCK;
		base = nat_pair_base(vol, index);
		if (index / 8 < vol->nat_bitmap_bytesize)
			second = (vol->nat_bitmap[index / 8] >> (7 - (index % 8))) & 1;
		addr = second ? base + vol->blocks_per_seg : base;
		*alt_addr = second ? base : base + vol->blocks_per_seg;
		if (read_block(vol->device, addr, blk)) return F2FS_EIO;
		*block_addr = rd32(blk + ent_off + 5);
		*ino = rd32(blk + ent_off + 1);
		return *block_addr ? F2FS_OK : F2FS_ENOENT;
	}
}

/* A node block's footer, at 4072 inside the block: nid, ino, flag, cp_ver, next_blkaddr. */
static uint32_t node_footer_nid(const uint8_t *blk) { return rd32(blk + F2FS_NODE_BODY_SIZE); }
static uint32_t node_footer_ino(const uint8_t *blk) { return rd32(blk + F2FS_NODE_BODY_SIZE + 4); }

/*
 * Resolve `nid` through the NAT and read its node block, checking that the block really is that
 * node.  Two guards matter here: an address outside the main area means the NAT entry is garbage
 * (reading it would return a checkpoint or SIT block as if it were a directory), and a footer that
 * names another nid means the NAT entry is stale -- after a crash both are possible, and both must
 * become a message rather than a wrong answer.
 */
static int node_read(struct f2fs_volume *vol, uint32_t nid, uint8_t *blk)
{
	uint32_t addr = 0, owner = 0, alt_nat = 0;
	int rc;

	if (nid == 0) return F2FS_ENOENT;
	rc = nat_lookup(vol, nid, &addr, &owner, &alt_nat);
	if (rc) return rc;
	if (addr < vol->main_blkaddr || (uint64_t)addr >= vol->block_count) return F2FS_EPROTO;
	if (read_block(vol->device, addr, blk)) return F2FS_EIO;
	if (node_footer_nid(blk) == nid) return F2FS_OK;

	/*
	 * The footer disagrees with the nid we asked for, so this NAT entry points at a block that is
	 * not the node: either the version bitmap selected the older of the NAT block's two copies, or
	 * the entry is garbage.  The other copy is the only other place the answer can be, and its own
	 * entry has to be read first -- `alt_nat` is a NAT block, not a node.  The footer then decides,
	 * so this is a verified answer rather than a second guess.
	 */
	if (alt_nat != 0) {
		uint8_t nat_blk[F2FS_BLK];
		uint32_t ent_off = (nid % F2FS_NAT_ENTRY_PER_BLOCK) * F2FS_NAT_ENTRY_SIZE;
		uint32_t alt_addr;

		if (read_block(vol->device, alt_nat, nat_blk)) return F2FS_EIO;
		alt_addr = rd32(nat_blk + ent_off + 5);
		if (alt_addr != 0 && alt_addr != addr &&
		    alt_addr >= vol->main_blkaddr && (uint64_t)alt_addr < vol->block_count) {
			if (read_block(vol->device, alt_addr, blk)) return F2FS_EIO;
			if (node_footer_nid(blk) == nid) return F2FS_OK;
		}
	}
	return F2FS_EPROTO;
}

/*
 * Load the inode `ino`.  The layout scalars are resolved here, once, exactly as `InodeLayout::new`
 * does: `i_extra_isize` shifts the start of `i_addr[]`, and the inline xattr area is reserved at
 * the *end* of it -- with the twist that the on-disk `i_inline_xattr_size` is only meaningful when
 * the volume has FLEXIBLE_INLINE_XATTR, and otherwise the kernel assumes 50 pointers whenever
 * F2FS_INLINE_XATTR is set.  Getting that wrong moves the direct/data boundary and misreads every
 * file larger than 923 - 50 blocks while smaller files still look right.
 */
static int inode_load(struct f2fs_volume *vol, uint32_t ino, struct f2fs_inode *out)
{
	uint8_t blk[F2FS_BLK];
	uint32_t extra_units, xattr_addrs, addrs;
	int rc;

	rc = node_read(vol, ino, blk);
	if (rc) return rc;
	/* `IS_INODE`: the only node whose nid equals its owner inode number. */
	if (node_footer_ino(blk) != ino) return F2FS_EPROTO;

	memcpy(out->node, blk, F2FS_BLK);
	out->ino = ino;
	/* Offsets from the Rust core's `F2fsInode`, which is checked against real images: i_mode is the
	 * first field, `i_inline` the third byte, `i_size` at 16, `i_addr` at 360 and the five `i_nid`
	 * entries after it.  The flag bits are low bits of `i_inline`, not high ones. */
	out->mode = rd16(blk + 0);
	out->inline_flags = blk[3];
	out->has_inline_xattr = (blk[3] & F2FS_INLINE_XATTR) != 0;
	out->has_inline_data = (blk[3] & F2FS_INLINE_DATA) != 0;
	out->has_inline_dentry = (blk[3] & F2FS_INLINE_DENTRY) != 0;
	out->has_data_exist = (blk[3] & F2FS_DATA_EXIST) != 0;
	out->has_extra_attr = (blk[3] & F2FS_EXTRA_ATTR) != 0;
	out->size = rd64(blk + 16);
	out->parent_ino = rd32(blk + 84);		/* i_pino */
	out->current_depth = rd32(blk + 72);		/* i_current_depth */
	/* `i_dir_level` is the byte after `i_name[255]`: i_namelen ends at 92, so 92 + 255 = 347,
	 * and `i_dir_level`(1) + `i_ext`(12) then carry the struct to `i_addr` at 360. */
	out->dir_level = blk[347];

	for (uint32_t i = 0; i < F2FS_DEF_ADDRS_PER_INODE; i++)
		out->addr[i] = rd32(blk + 360 + i * 4);	/* i_addr */
	for (uint32_t i = 0; i < F2FS_DEF_NIDS_PER_INODE; i++)
		out->nid[i] = rd32(blk + 360 + F2FS_DEF_ADDRS_PER_INODE * 4 + i * 4);

	/* `i_extra_isize` lives in the low 16 bits of i_addr[0] and is meaningful only with
	 * F2FS_EXTRA_ATTR; the cap is F2FS_TOTAL_EXTRA_ATTR_SIZE = 36 bytes, 4-byte aligned. */
	extra_units = 0;
	if (out->has_extra_attr) {
		uint32_t bytes = out->addr[0] & 0xFFFFu;
		if ((bytes & 3u) || bytes > 36u) return F2FS_EPROTO;
		extra_units = bytes / 4u;
	}
	if (!out->has_inline_xattr) {
		xattr_addrs = 0;
	} else if (vol->feature & F2FS_FEATURE_FLEXIBLE_INLINE_XATTR) {
		xattr_addrs = out->addr[0] >> 16;	/* i_inline_xattr_size, off disk */
	} else {
		xattr_addrs = F2FS_DEFAULT_INLINE_XATTR_ADDRS;
	}
	if (extra_units + xattr_addrs >= F2FS_DEF_ADDRS_PER_INODE) return F2FS_EPROTO;
	addrs = F2FS_DEF_ADDRS_PER_INODE - extra_units - xattr_addrs;

	out->extra_isize_units = extra_units;
	out->inline_xattr_addrs = xattr_addrs;
	out->addrs_per_inode = addrs;
	/* MAX_INLINE_DATA = 4 * (CUR_ADDRS_PER_INODE - i_inline_xattr_size - DEF_INLINE_RESERVED_SIZE) */
	out->max_inline_data = 4u * (addrs - F2FS_DEF_INLINE_RESERVED_SIZE);
	return F2FS_OK;
}

/* Byte offset of the inline data / inline dentry area inside the inode block:
 * `inline_data_offset = OFFSET_OF_END_OF_I_EXT + 4 * (i_extra_isize / 4 + 1)`. */
static uint32_t inline_area_offset(const struct f2fs_inode *inode)
{
	return 360u + 4u * (inode->extra_isize_units + 1u);
}

/* ==========================================================================================
 * Dentry views: one shape for a 4096-byte dentry block and for an inode's inline dentry area.
 * ========================================================================================== */
struct dentry_view {
	const uint8_t *data;
	uint32_t data_len;
	uint32_t max;			/* slots: 214 in a block, NR_INLINE_DENTRY(inode) inline */
	uint32_t bitmap_off;
	uint32_t dentry_off;
	uint32_t filename_off;
};

static void view_block(struct dentry_view *v, const uint8_t *block)
{
	v->data = block;
	v->data_len = F2FS_BLK;
	v->max = F2FS_NR_DENTRY_IN_BLOCK;
	v->bitmap_off = 0;
	v->dentry_off = F2FS_DENTRY_OFF;
	v->filename_off = F2FS_FILENAME_OFF;
}

/*
 * `NR_INLINE_DENTRY(inode)` and friends, from layout.rs:
 *   nr      = (MAX_INLINE_DATA * 8) / ((SIZE_OF_DIR_ENTRY + F2FS_SLOT_LEN) * 8 + 1)
 *   bitmap  = ceil(nr / 8)
 *   reserved= MAX_INLINE_DATA - ((SIZE_OF_DIR_ENTRY + F2FS_SLOT_LEN) * nr + bitmap)
 *   dentry  = bitmap + reserved,  filename = dentry + nr * SIZE_OF_DIR_ENTRY
 */
static int inline_dentry_geometry(const struct f2fs_inode *inode, uint32_t *nr_out,
				  uint32_t *dentry_off_out, uint32_t *filename_off_out)
{
	const uint32_t mid = inode->max_inline_data;
	const uint32_t unit = F2FS_SIZE_OF_DIR_ENTRY + F2FS_SLOT_LEN;
	uint32_t nr, bitmap, reserved, dentry_off, filename_off;

	if (mid < unit + 1u) return F2FS_EPROTO;
	nr = (mid * 8u) / (unit * 8u + 1u);
	if (nr == 0) return F2FS_EPROTO;
	bitmap = (nr + 7u) / 8u;
	if (unit * nr + bitmap > mid) return F2FS_EPROTO;
	reserved = mid - (unit * nr + bitmap);
	dentry_off = bitmap + reserved;
	filename_off = dentry_off + nr * F2FS_SIZE_OF_DIR_ENTRY;
	if (filename_off > mid) return F2FS_EPROTO;
	*nr_out = nr;
	*dentry_off_out = dentry_off;
	*filename_off_out = filename_off;
	return F2FS_OK;
}

static int view_inline(struct dentry_view *v, const struct f2fs_inode *inode)
{
	uint32_t nr, dentry_off, filename_off, base;
	int rc;

	if (!inode->has_inline_dentry) return F2FS_EINVAL;
	rc = inline_dentry_geometry(inode, &nr, &dentry_off, &filename_off);
	if (rc) return rc;
	base = inline_area_offset(inode);
	/* The inline area must stop where `i_nid[]` starts, which is the byte after `i_addr[]`:
	 * 360 + 923*4 = 4052.  (The geometry cannot actually overflow -- base grows by 4 per extra
	 * attribute unit and MAX_INLINE_DATA shrinks by the same 4 -- but a corrupt inode must not be
	 * able to make this function hand out a view over the node footer.) */
	if (base + inode->max_inline_data > 360u + F2FS_DEF_ADDRS_PER_INODE * 4u) return F2FS_EPROTO;
	v->data = inode->node + base;
	v->data_len = inode->max_inline_data;
	v->max = nr;
	v->bitmap_off = 0;
	v->dentry_off = dentry_off;
	v->filename_off = filename_off;
	return F2FS_OK;
}

/* Slot `i` holds a live entry: bit i of the bitmap, LSB-first (`bit::test_bit_le`). */
static int view_is_used(const struct dentry_view *v, uint32_t slot)
{
	uint32_t byte = v->bitmap_off + (slot >> 3);

	if (byte >= v->data_len) return 0;
	return (v->data[byte] >> (slot & 7u)) & 1u;
}

static int view_entry(const struct dentry_view *v, uint32_t slot, uint32_t *hash, uint32_t *ino,
		      uint32_t *name_len, uint8_t *file_type)
{
	uint32_t off;

	if (slot >= v->max) return F2FS_EPROTO;
	off = v->dentry_off + slot * F2FS_SIZE_OF_DIR_ENTRY;
	if (off + F2FS_SIZE_OF_DIR_ENTRY > v->data_len) return F2FS_EPROTO;
	*hash = rd32(v->data + off);
	*ino = rd32(v->data + off + 4);
	*name_len = rd16(v->data + off + 8);
	*file_type = v->data[off + 10];
	return F2FS_OK;
}

/* The name of the entry in `slot`: `name_len` bytes starting at filename[slot], which may run into
 * the following 8-byte slots -- that is how a name longer than 8 bytes is stored. */
static int view_name(const struct dentry_view *v, uint32_t slot, uint32_t name_len,
		     const uint8_t **name_out)
{
	uint32_t off;

	if (name_len == 0 || name_len > F2FS_NAME_LEN) return F2FS_EPROTO;
	off = v->filename_off + slot * F2FS_SLOT_LEN;
	if (off + name_len > v->data_len) return F2FS_EPROTO;
	*name_out = v->data + off;
	return F2FS_OK;
}

/* `GET_DENTRY_SLOTS`: slots a name of `len` bytes occupies. */
static uint32_t dentry_slots(uint32_t len)
{
	return (len + F2FS_SLOT_LEN - 1) >> 3;
}

/* One visited entry, handed to the walker callback. */
typedef int (*view_cb)(void *ctx, uint32_t slot, uint32_t hash, uint32_t ino, uint8_t file_type,
		       uint32_t name_len, const uint8_t *name);

/*
 * Walk the live entries of a view.  The loop bound is the view's own slot count (214, or the
 * per-inode inline count), and every step advances by at least one slot, so a corrupt name length
 * cannot make it run backwards; a name that claims to reach past the area is corruption, not a
 * silent skip.  A set bit with a zero-length name advances by one slot -- that detail is from
 * `f2fs_find_target_dentry()` and is what keeps a half-written entry from swallowing the next.
 */
static int view_walk(const struct dentry_view *v, view_cb cb, void *ctx)
{
	uint32_t slot = 0;

	while (slot < v->max) {
		uint32_t hash, ino, name_len;
		uint8_t file_type;
		const uint8_t *name;
		int rc;

		if (!view_is_used(v, slot)) { slot++; continue; }
		rc = view_entry(v, slot, &hash, &ino, &name_len, &file_type);
		if (rc) return rc;
		if (name_len == 0) { slot++; continue; }
		if (name_len > F2FS_NAME_LEN || slot + dentry_slots(name_len) > v->max)
			return F2FS_EPROTO;
		rc = view_name(v, slot, name_len, &name);
		if (rc) return rc;
		rc = cb(ctx, slot, hash, ino, file_type, name_len, name);
		if (rc) return rc;
		slot += dentry_slots(name_len);
	}
	return F2FS_OK;
}

/* ---- exact, hash-gated search: `f2fs_find_target_dentry()` with `use_hash` on ---- */
struct find_ctx {
	uint32_t hash;
	const uint8_t *name;
	uint32_t len;
	uint32_t slot;
	uint32_t ino;
	int found;
};

static int find_cb(void *p, uint32_t slot, uint32_t hash, uint32_t ino, uint8_t file_type,
		   uint32_t name_len, const uint8_t *name)
{
	struct find_ctx *c = (struct find_ctx *)p;

	(void)file_type;

	if (hash != c->hash || name_len != c->len) return 0;
	if (memcmp(name, c->name, c->len) != 0) return 0;
	c->slot = slot;
	c->ino = ino;
	c->found = 1;
	return 1;			/* stop the walk */
}

/* ---- case-folded search: `use_hash` off, every entry compared, deterministic tie-break ---- */
static uint8_t ascii_lower(uint8_t c)
{
	return (c >= 'A' && c <= 'Z') ? (uint8_t)(c + ('a' - 'A')) : c;
}

/*
 * ASCII-only folding, stated as a limit rather than hidden: full Unicode casefolding needs the
 * volume's `s_encoding` tables and the `casefold` feature, which a boot loader will not carry.  For
 * the names a boot path is made of -- freeldr.ini, ntoskrnl.exe, SystemRoot, WinSxS -- ASCII is
 * exactly the case space that differs, and a non-ASCII byte must match byte for byte.
 */
struct fold_ctx {
	const uint8_t *name;
	uint32_t len;
	uint8_t best[F2FS_NAME_LEN];
	uint32_t best_len;
	uint32_t best_slot;
	uint32_t best_ino;
	int found;
};

static int fold_less(const uint8_t *a, uint32_t alen, const uint8_t *b, uint32_t blen)
{
	uint32_t n = alen < blen ? alen : blen;

	for (uint32_t i = 0; i < n; i++) {
		if (a[i] != b[i]) return a[i] < b[i];
	}
	return alen < blen;
}

static int fold_cb(void *p, uint32_t slot, uint32_t hash, uint32_t ino, uint8_t file_type,
		   uint32_t name_len, const uint8_t *name)
{
	struct fold_ctx *c = (struct fold_ctx *)p;

	(void)hash;
	(void)file_type;
	if (name_len != c->len) return 0;
	for (uint32_t i = 0; i < name_len; i++) {
		if (ascii_lower(name[i]) != ascii_lower(c->name[i])) return 0;
	}
	/* Several entries can fold onto the requested name (`README` and `readme`); take the
	 * lexicographically smallest so the answer does not depend on slot order. */
	if (c->found && !fold_less(name, name_len, c->best, c->best_len)) return 0;
	memcpy(c->best, name, name_len);
	c->best_len = name_len;
	c->best_slot = slot;
	c->best_ino = ino;
	c->found = 1;
	return 0;			/* keep looking: the smallest may come later */
}

/* ==========================================================================================
 * Directory geometry for the hashed (multi-block) layout, from dir.rs.
 * ========================================================================================== */
static uint32_t dir_blocks(uint64_t size)
{
	return (uint32_t)((size + F2FS_BLK - 1) >> 12);
}

static uint32_t dir_buckets(uint32_t level, int32_t dir_level)
{
	int32_t l = (int32_t)level + dir_level;

	if (l < (int32_t)(F2FS_MAX_DIR_HASH_DEPTH / 2)) return 1u << l;
	return F2FS_MAX_DIR_BUCKETS;
}

static uint32_t bucket_blocks(uint32_t level)
{
	return level < F2FS_MAX_DIR_HASH_DEPTH / 2 ? 2u : 4u;
}

/* `dir_block_index()`: the first dentry block of bucket `idx` at `level`. */
static uint64_t dir_block_index(uint32_t level, int32_t dir_level, uint32_t idx)
{
	uint64_t bidx = 0;

	for (uint32_t i = 0; i < level && i < F2FS_MAX_DIR_HASH_DEPTH; i++)
		bidx += (uint64_t)dir_buckets(i, dir_level) * bucket_blocks(i);
	return bidx + (uint64_t)idx * bucket_blocks(level);
}

/* ==========================================================================================
 * Block mapping: `i_addr[]`, then the five `i_nid[]` slots.  Same arithmetic as data.rs
 * (node_path) and mutate.rs (group_slot).
 * ========================================================================================== */
struct node_path {
	uint8_t level;
	uint32_t off[4];	/* off[0] is a direct index at level 0, else a NODE_*_BLOCK sentinel */
};

/* `get_node_path()`: where block `block` lives, or EFBIG past the addressable space. */
static int node_path(uint32_t addrs_per_inode, uint64_t block, struct node_path *out)
{
	uint64_t b = block;
	const uint64_t direct_index = addrs_per_inode;
	const uint64_t direct_blks = F2FS_ADDRS_PER_BLOCK;
	const uint64_t ind_blks = direct_blks * F2FS_NIDS_PER_BLOCK;
	const uint64_t dind_blks = ind_blks * F2FS_NIDS_PER_BLOCK;

	out->off[0] = out->off[1] = out->off[2] = out->off[3] = 0;
	if (b < direct_index) {
		out->level = 0;
		out->off[0] = (uint32_t)b;
		return F2FS_OK;
	}
	b -= direct_index;
	if (b < direct_blks) {
		out->level = 1;
		out->off[0] = F2FS_NODE_DIR1_BLOCK;
		out->off[1] = (uint32_t)b;
		return F2FS_OK;
	}
	b -= direct_blks;
	if (b < direct_blks) {
		out->level = 1;
		out->off[0] = F2FS_NODE_DIR2_BLOCK;
		out->off[1] = (uint32_t)b;
		return F2FS_OK;
	}
	b -= direct_blks;
	if (b < ind_blks) {
		out->level = 2;
		out->off[0] = F2FS_NODE_IND1_BLOCK;
		out->off[1] = (uint32_t)(b / direct_blks);
		out->off[2] = (uint32_t)(b % direct_blks);
		return F2FS_OK;
	}
	b -= ind_blks;
	if (b < ind_blks) {
		out->level = 2;
		out->off[0] = F2FS_NODE_IND2_BLOCK;
		out->off[1] = (uint32_t)(b / direct_blks);
		out->off[2] = (uint32_t)(b % direct_blks);
		return F2FS_OK;
	}
	b -= ind_blks;
	if (b < dind_blks) {
		out->level = 3;
		out->off[0] = F2FS_NODE_DIND_BLOCK;
		out->off[1] = (uint32_t)(b / ind_blks);
		out->off[2] = (uint32_t)((b % ind_blks) / direct_blks);
		out->off[3] = (uint32_t)(b % direct_blks);
		return F2FS_OK;
	}
	return F2FS_EFBIG;
}

/* The block pointer at `index` inside a direct node (or the nid at `index` inside an indirect one:
 * F2FS gives both the same body shape). */
static uint32_t node_slot(const uint8_t *blk, uint32_t index)
{
	return rd32(blk + (uint64_t)index * 4u);
}

int f2fs_block_addr(struct f2fs_volume *vol, const struct f2fs_inode *inode, uint64_t block,
		    uint32_t *addr_out)
{
	struct node_path path;
	uint8_t blk[F2FS_BLK];
	uint32_t first, nid;
	int rc;

	*addr_out = F2FS_NULL_ADDR;

	/* For an inline-data inode the content is in the inode block itself; f2fs marks block 0 with
	 * COMPRESS_ADDR.  (Same convention as `Fs::data_block_ref`.) */
	if (inode->has_inline_data) {
		*addr_out = (block == 0) ? F2FS_COMPRESS_ADDR : F2FS_NULL_ADDR;
		return F2FS_OK;
	}
	rc = node_path(inode->addrs_per_inode, block, &path);
	if (rc) return rc;

	first = path.off[0];
	if (path.level == 0) {
		uint32_t idx = inode->extra_isize_units + first;
		if (idx >= F2FS_DEF_ADDRS_PER_INODE) return F2FS_EPROTO;
		*addr_out = inode->addr[idx];
		return F2FS_OK;
	}
	if (first < F2FS_NODE_DIR1_BLOCK || first > F2FS_NODE_DIND_BLOCK) return F2FS_EPROTO;
	nid = inode->nid[first - F2FS_NODE_DIR1_BLOCK];
	if (nid == 0) return F2FS_OK;		/* hole: the node was never allocated */

	if (path.level == 1) {
		rc = node_read(vol, nid, blk);
		if (rc) return rc;
		*addr_out = node_slot(blk, path.off[1]);
		return F2FS_OK;
	}
	if (path.level == 2) {
		uint32_t dnid;
		rc = node_read(vol, nid, blk);
		if (rc) return rc;
		dnid = node_slot(blk, path.off[1]);
		if (dnid == 0) return F2FS_OK;
		rc = node_read(vol, dnid, blk);
		if (rc) return rc;
		*addr_out = node_slot(blk, path.off[2]);
		return F2FS_OK;
	}
	if (path.level == 3) {
		uint32_t inid, dnid;
		rc = node_read(vol, nid, blk);
		if (rc) return rc;
		inid = node_slot(blk, path.off[1]);
		if (inid == 0) return F2FS_OK;
		rc = node_read(vol, inid, blk);
		if (rc) return rc;
		dnid = node_slot(blk, path.off[2]);
		if (dnid == 0) return F2FS_OK;
		rc = node_read(vol, dnid, blk);
		if (rc) return rc;
		*addr_out = node_slot(blk, path.off[3]);
		return F2FS_OK;
	}
	return F2FS_EPROTO;
}

/* ==========================================================================================
 * Directory lookup
 * ========================================================================================== */
static int block_dentry_load(struct f2fs_volume *vol, const struct f2fs_inode *dir, uint64_t bidx,
			     uint8_t *blk, int *is_hole)
{
	uint32_t addr;
	int rc;

	*is_hole = 0;
	rc = f2fs_block_addr(vol, dir, bidx, &addr);
	if (rc) return rc;
	if (addr == F2FS_NULL_ADDR || addr == F2FS_NEW_ADDR) { *is_hole = 1; return F2FS_OK; }
	if (addr == F2FS_COMPRESS_ADDR) return F2FS_ENOTSUP;
	if (addr < vol->main_blkaddr || (uint64_t)addr >= vol->block_count) return F2FS_EPROTO;
	if (read_block(vol->device, addr, blk)) return F2FS_EIO;
	return F2FS_OK;
}

/* Search one view for an exact, hash-gated match. */
static int view_find_exact(const struct dentry_view *v, uint32_t hash, const uint8_t *name,
			   uint32_t len, uint32_t *slot_out, uint32_t *ino_out)
{
	struct find_ctx c;
	int rc;

	memset(&c, 0, sizeof(c));
	c.hash = hash;
	c.name = name;
	c.len = len;
	rc = view_walk(v, find_cb, &c);
	if (rc == F2FS_OK || rc == 1) rc = F2FS_OK;	/* 1 is the callback's "found, stop" */
	if (rc) return rc;
	if (!c.found) return F2FS_ENOENT;
	*slot_out = c.slot;
	*ino_out = c.ino;
	return F2FS_OK;
}

/* Search one view case-insensitively, keeping the lexicographically smallest match. */
static int view_find_folded(const struct dentry_view *v, const uint8_t *name, uint32_t len,
			    struct fold_ctx *out)
{
	int rc;

	memset(out, 0, sizeof(*out));
	out->name = name;
	out->len = len;
	rc = view_walk(v, fold_cb, out);
	if (rc) return rc;
	return out->found ? F2FS_OK : F2FS_ENOENT;
}

enum dir_match { DIR_MISS = 0, DIR_EXACT = 1, DIR_FOLDED = 2 };

/*
 * Resolve `name` in `dir`: exact match first (hash-gated, then the hashed multi-level walk), then
 * -- if `allow_fold` -- a case-insensitive pass over every entry.  `raw` receives the on-disk name
 * that actually matched, which is how the caller tells the operator that the path it asked for was
 * not the path on disk.
 */
static int dir_find(struct f2fs_volume *vol, const struct f2fs_inode *dir, const uint8_t *name,
		    uint32_t len, int allow_fold, uint32_t *ino_out, enum dir_match *how_out,
		    uint8_t *raw, uint32_t *raw_len)
{
	struct dentry_view view;
	struct fold_ctx fold;
	uint32_t hash, slot, ino, npages, max_depth;
	int rc;

	*how_out = DIR_MISS;
	if (len == 0 || len > F2FS_NAME_LEN) return F2FS_EINVAL;
	hash = dentry_hash(name, len);

	if (dir->has_inline_dentry) {
		rc = view_inline(&view, dir);
		if (rc) return rc;
		rc = view_find_exact(&view, hash, name, len, &slot, &ino);
		if (rc == F2FS_OK) {
			*ino_out = ino;
			*how_out = DIR_EXACT;
			if (raw) memcpy(raw, name, len);
			if (raw_len) *raw_len = len;
			return F2FS_OK;
		}
		if (rc != F2FS_ENOENT || !allow_fold) return rc;
		rc = view_find_folded(&view, name, len, &fold);
		if (rc) return rc;
		*ino_out = fold.best_ino;
		*how_out = DIR_FOLDED;
		if (raw) memcpy(raw, fold.best, fold.best_len);
		if (raw_len) *raw_len = fold.best_len;
		return F2FS_OK;
	}

	npages = dir_blocks(dir->size);
	if (npages > F2FS_MAX_DIR_BLOCKS) return F2FS_EFBIG;

	/* 1) The hashed walk: level 0 covers one bucket of two blocks, then buckets double until
	 * MAX_DIR_BUCKETS.  `i_current_depth` says how many levels exist, and it is clamped and then
	 * bounded by the block count, so a corrupt depth cannot make this read forever. */
	max_depth = dir->current_depth;
	if (max_depth > F2FS_MAX_DIR_HASH_DEPTH) max_depth = F2FS_MAX_DIR_HASH_DEPTH;
	for (uint32_t level = 0; level < max_depth && npages; level++) {
		uint32_t nbucket = dir_buckets(level, (int32_t)dir->dir_level);
		uint64_t start;
		uint32_t nblock;

		if (nbucket == 0) break;
		nblock = bucket_blocks(level);
		/* `i_size` counts the directory's dentry blocks, so a bucket block at or past it is not
		 * part of the directory: skipping it keeps a stray `i_addr[]` value from being scanned. */
		start = dir_block_index(level, (int32_t)dir->dir_level, hash % nbucket);
		for (uint32_t b = 0; b < nblock; b++) {
			uint8_t blk[F2FS_BLK];
			int hole = 0;

			if (start + b >= npages) break;
			rc = block_dentry_load(vol, dir, start + b, blk, &hole);
			if (rc) return rc;
			if (hole) continue;
			view_block(&view, blk);
			rc = view_find_exact(&view, hash, name, len, &slot, &ino);
			if (rc == F2FS_OK) {
				*ino_out = ino;
				*how_out = DIR_EXACT;
				if (raw) memcpy(raw, name, len);
				if (raw_len) *raw_len = len;
				return F2FS_OK;
			}
			if (rc != F2FS_ENOENT) return rc;
		}
	}
	if (!allow_fold) return F2FS_ENOENT;

	/* 2) Folded fallback over every dentry block, in block order. */
	memset(&fold, 0, sizeof(fold));
	fold.name = name;
	fold.len = len;
	for (uint32_t b = 0; b < npages; b++) {
		uint8_t blk[F2FS_BLK];
		struct fold_ctx one;
		int hole = 0;

		rc = block_dentry_load(vol, dir, b, blk, &hole);
		if (rc) return rc;
		if (hole) continue;
		view_block(&view, blk);
		rc = view_find_folded(&view, name, len, &one);
		if (rc == F2FS_ENOENT) continue;
		if (rc) return rc;
		if (!fold.found || fold_less(one.best, one.best_len, fold.best, fold.best_len)) {
			fold.found = 1;
			memcpy(fold.best, one.best, one.best_len);
			fold.best_len = one.best_len;
			fold.best_ino = one.best_ino;
		}
	}
	if (!fold.found) return F2FS_ENOENT;
	*ino_out = fold.best_ino;
	*how_out = DIR_FOLDED;
	if (raw) memcpy(raw, fold.best, fold.best_len);
	if (raw_len) *raw_len = fold.best_len;
	return F2FS_OK;
}

/* Count the entries of `dir`, excluding "." and "..". */
struct count_ctx {
	uint32_t n;
};

static int count_cb(void *p, uint32_t slot, uint32_t hash, uint32_t ino, uint8_t file_type,
		    uint32_t name_len, const uint8_t *name)
{
	struct count_ctx *c = (struct count_ctx *)p;

	(void)slot; (void)hash; (void)ino; (void)file_type;
	if (name_len == 1 && name[0] == '.') return 0;
	if (name_len == 2 && name[0] == '.' && name[1] == '.') return 0;
	c->n++;
	return 0;
}

/*
 * Entry count for the diagnostic.  Returns F2FS_OK and the count, or a failure the caller reports
 * as "entry count unavailable" -- an unreadable directory must not hide the message that the name
 * was not found in it.
 */
static int dir_entry_count(struct f2fs_volume *vol, const struct f2fs_inode *dir, uint32_t *n_out)
{
	struct dentry_view view;
	struct count_ctx c;
	uint32_t npages;
	int rc;

	c.n = 0;
	if (dir->has_inline_dentry) {
		rc = view_inline(&view, dir);
		if (rc) return rc;
		rc = view_walk(&view, count_cb, &c);
		if (rc) return rc;
		*n_out = c.n;
		return F2FS_OK;
	}
	npages = dir_blocks(dir->size);
	if (npages > F2FS_MAX_DIR_BLOCKS) return F2FS_EFBIG;
	for (uint32_t b = 0; b < npages; b++) {
		uint8_t blk[F2FS_BLK];
		int hole = 0;

		rc = block_dentry_load(vol, dir, b, blk, &hole);
		if (rc) return rc;
		if (hole) continue;
		view_block(&view, blk);
		rc = view_walk(&view, count_cb, &c);
		if (rc) return rc;
	}
	*n_out = c.n;
	return F2FS_OK;
}

/* ==========================================================================================
 * f2fs_lookup: walk the path one component at a time
 * ========================================================================================== */
static int is_sep(char c)
{
	return c == '/' || c == '\\';
}

/*
 * The diagnostic for a component that is not in its parent, in the shape the boot documentation
 * asks for: the path, the fact that folding was tried, the component by name, and how many entries
 * the parent holds.  Those four facts separate "wrong path" from "wrong case" from "wrong volume".
 */
static void diag_not_found(struct f2fs_volume *vol, const char *path, uint32_t prefix_len,
			   const uint8_t *comp, uint32_t comp_len, int have_count, uint32_t count,
			   int count_rc)
{
	struct diagbuf d;

	dg_init(&d, vol->diag, F2FS_DIAG_LEN);
	dg_str(&d, "f2fs: not found: ");
	dg_str(&d, path);
	dg_str(&d, "  (no case-insensitive match either)\n      ");
	if (prefix_len == 0) {
		dg_ch(&d, '/');
	} else {
		uint32_t end = prefix_len;
		while (end > 0 && is_sep(path[end - 1])) end--;
		if (end == 0) dg_ch(&d, '/');
		else dg_mem(&d, path, end);
	}
	dg_str(&d, " contains ");
	if (have_count) {
		dg_u32(&d, count);
	} else {
		dg_str(&d, "? (unreadable: error ");
		if (count_rc < 0) { dg_ch(&d, '-'); dg_u32(&d, (uint32_t)(-count_rc)); }
		else dg_u32(&d, (uint32_t)count_rc);
		dg_ch(&d, ')');
	}
	dg_str(&d, " entries; \"");
	for (uint32_t i = 0; i < comp_len && i < 64; i++) dg_ch(&d, (char)comp[i]);
	if (comp_len > 64) dg_str(&d, "...");
	dg_str(&d, "\" is not one of them");
}

/* Note a folded match so the loader can log it: a warning, not an error. */
static void diag_folded(struct f2fs_volume *vol, const char *path, uint32_t prefix_len,
			const uint8_t *comp, uint32_t comp_len, const uint8_t *raw, uint32_t raw_len)
{
	struct diagbuf d;

	vol->fold_hits++;
	dg_init(&d, vol->diag, F2FS_DIAG_LEN);
	dg_str(&d, "f2fs: case-folded \"");
	for (uint32_t i = 0; i < comp_len && i < 64; i++) dg_ch(&d, (char)comp[i]);
	dg_str(&d, "\" to \"");
	for (uint32_t i = 0; i < raw_len && i < 64; i++) dg_ch(&d, (char)raw[i]);
	dg_str(&d, "\" in ");
	if (prefix_len == 0) dg_ch(&d, '/');
	else dg_mem(&d, path, prefix_len);
	dg_str(&d, " (exact match not present)");
}

static void diag_mode_error(struct f2fs_volume *vol, const char *path, uint32_t prefix_len,
			    const uint8_t *comp, uint32_t comp_len, uint32_t mode, const char *what)
{
	struct diagbuf d;

	dg_init(&d, vol->diag, F2FS_DIAG_LEN);
	dg_str(&d, "f2fs: ");
	dg_str(&d, path);
	dg_str(&d, ": \"");
	for (uint32_t i = 0; i < comp_len && i < 64; i++) dg_ch(&d, (char)comp[i]);
	dg_str(&d, "\" ");
	dg_str(&d, what);
	dg_str(&d, " (parent ");
	if (prefix_len) dg_mem(&d, path, prefix_len);
	else dg_ch(&d, '/');
	dg_str(&d, ", mode ");
	dg_mode(&d, mode);
	dg_ch(&d, ')');
}

static void diag_plain(struct f2fs_volume *vol, const char *path, int rc, const char *what)
{
	struct diagbuf d;

	dg_init(&d, vol->diag, F2FS_DIAG_LEN);
	dg_str(&d, "f2fs: ");
	if (path) { dg_str(&d, path); dg_str(&d, ": "); }
	dg_str(&d, what);
	dg_str(&d, " (error -");
	dg_u32(&d, (uint32_t)(rc < 0 ? -rc : rc));
	dg_ch(&d, ')');
}

int f2fs_lookup(struct f2fs_volume *vol, const char *path, struct f2fs_inode *out)
{
	const char *p;
	uint32_t prefix_len;
	int rc;

	if (!vol || !path || !out) return F2FS_EINVAL;
	vol->diag[0] = 0;

	rc = inode_load(vol, vol->root_ino, out);
	if (rc) {
		diag_plain(vol, NULL, rc, "the root inode is unreadable");
		return rc;
	}

	p = path;
	while (is_sep(*p)) p++;

	while (*p) {
		const char *start = p;
		uint32_t len = 0, ino = 0;
		enum dir_match how = DIR_MISS;
		uint8_t raw[F2FS_NAME_LEN];
		uint32_t raw_len = 0;
		int allow_fold = 1;

		while (*p && !is_sep(*p)) { p++; len++; }
		prefix_len = (uint32_t)(start - path);
		while (is_sep(*p)) p++;
		if (len == 0) continue;
		if (len > F2FS_NAME_LEN) {
			diag_plain(vol, path, F2FS_EINVAL, "a path component is longer than 255 bytes");
			return F2FS_EINVAL;
		}

		if (len == 1 && start[0] == '.') continue;

		if (len == 2 && start[0] == '.' && start[1] == '.') {
			/* ".." is a real dentry; i_pino is the redundancy if the dentry is missing. */
			rc = dir_find(vol, out, (const uint8_t *)"..", 2, 0, &ino, &how, NULL, NULL);
			if (rc == F2FS_OK) {
				rc = inode_load(vol, ino, out);
				if (rc) { diag_plain(vol, path, rc, "\"..\" is unreadable"); return rc; }
				continue;
			}
			if (rc != F2FS_ENOENT) { diag_plain(vol, path, rc, "\"..\" lookup failed"); return rc; }
			if (out->parent_ino == 0) {
				diag_plain(vol, path, F2FS_ENOENT, "\"..\" has no dentry and no i_pino");
				return F2FS_ENOENT;
			}
			rc = inode_load(vol, out->parent_ino, out);
			if (rc) { diag_plain(vol, path, rc, "\"..\" is unreadable"); return rc; }
			continue;
		}

		if ((out->mode & F2FS_S_IFMT) != F2FS_S_IFDIR) {
			diag_mode_error(vol, path, prefix_len, (const uint8_t *)start, len, out->mode,
					"is not in a directory: its parent is not one");
			return F2FS_ENOTDIR;
		}

		rc = dir_find(vol, out, (const uint8_t *)start, len, allow_fold, &ino, &how, raw,
			      &raw_len);
		if (rc == F2FS_ENOENT) {
			uint32_t count = 0;
			int crc = dir_entry_count(vol, out, &count);

			diag_not_found(vol, path, prefix_len, (const uint8_t *)start, len, crc == 0,
				       count, crc);
			return F2FS_ENOENT;
		}
		if (rc == F2FS_EINVAL) {
			diag_plain(vol, path, rc, "the directory entry name length is unusable");
			return rc;
		}
		if (rc) {
			diag_plain(vol, path, rc, "the parent directory is unreadable");
			return rc;
		}
		if (how == DIR_FOLDED)
			diag_folded(vol, path, prefix_len, (const uint8_t *)start, len, raw, raw_len);
		else
			vol->diag[0] = 0;

		rc = inode_load(vol, ino, out);
		if (rc) {
			diag_plain(vol, path, rc, "the inode a directory entry points at is unreadable");
			return rc;
		}
	}
	return F2FS_OK;
}

/* ==========================================================================================
 * f2fs_readdir / f2fs_readlink
 * ========================================================================================== */
struct readdir_ctx {
	f2fs_dir_visit visit;
	void *user;
	uint32_t count;
	int stop;
};

static int readdir_cb(void *p, uint32_t slot, uint32_t hash, uint32_t ino, uint8_t file_type,
		      uint32_t name_len, const uint8_t *name)
{
	struct readdir_ctx *c = (struct readdir_ctx *)p;
	int rc;

	(void)hash;
	c->count++;
	rc = c->visit(c->user, (const char *)name, name_len, ino, file_type, slot);
	if (rc) { c->stop = rc; return 1; }	/* stop the walk, report the visit's value */
	return 0;
}

int f2fs_readdir(struct f2fs_volume *vol, const struct f2fs_inode *dir, f2fs_dir_visit visit,
		 void *ctx, uint32_t *entries_out)
{
	struct dentry_view view;
	struct readdir_ctx c;
	uint32_t npages;
	int rc;

	if (entries_out) *entries_out = 0;
	if (!vol || !dir || !visit) return F2FS_EINVAL;
	if ((dir->mode & F2FS_S_IFMT) && (dir->mode & F2FS_S_IFMT) != F2FS_S_IFDIR) {
		diag_plain(vol, NULL, F2FS_ENOTDIR, "not a directory");
		return F2FS_ENOTDIR;
	}
	c.visit = visit;
	c.user = ctx;
	c.count = 0;
	c.stop = 0;

	if (dir->has_inline_dentry) {
		rc = view_inline(&view, dir);
		if (rc) { diag_plain(vol, NULL, rc, "inline dentry geometry is unusable"); return rc; }
		rc = view_walk(&view, readdir_cb, &c);
		if (c.stop) return c.stop;
		if (rc) { diag_plain(vol, NULL, rc, "the inline dentry area is corrupt"); return rc; }
		if (entries_out) *entries_out = c.count;
		return F2FS_OK;
	}
	npages = dir_blocks(dir->size);
	if (npages > F2FS_MAX_DIR_BLOCKS) {
		diag_plain(vol, NULL, F2FS_EFBIG, "the directory size is past the block-count bound");
		return F2FS_EFBIG;
	}
	for (uint32_t b = 0; b < npages; b++) {
		uint8_t blk[F2FS_BLK];
		int hole = 0;

		rc = block_dentry_load(vol, dir, b, blk, &hole);
		if (rc) { diag_plain(vol, NULL, rc, "a directory block is unreadable"); return rc; }
		if (hole) continue;
		view_block(&view, blk);
		rc = view_walk(&view, readdir_cb, &c);
		if (c.stop) return c.stop;
		if (rc) { diag_plain(vol, NULL, rc, "a directory block is corrupt"); return rc; }
	}
	if (entries_out) *entries_out = c.count;
	return F2FS_OK;
}

int f2fs_readlink(struct f2fs_volume *vol, const struct f2fs_inode *inode, char *buffer,
		  uint32_t capacity, uint32_t *len_out)
{
	uint64_t got = 0;
	int rc;

	if (len_out) *len_out = 0;
	if (!vol || !inode || !buffer || capacity == 0) return F2FS_EINVAL;
	if ((inode->mode & F2FS_S_IFMT) != F2FS_S_IFLNK) {
		diag_plain(vol, NULL, F2FS_EINVAL, "not a symbolic link");
		return F2FS_EINVAL;
	}
	if (inode->size >= capacity) {
		diag_plain(vol, NULL, F2FS_EFBIG, "the symbolic link target does not fit the buffer");
		return F2FS_EFBIG;
	}
	rc = f2fs_read(vol, inode, 0, buffer, inode->size, &got);
	if (rc) return rc;
	if (got != inode->size) {
		diag_plain(vol, NULL, F2FS_EPROTO, "a symbolic link read short of its size");
		return F2FS_EPROTO;
	}
	buffer[got] = 0;
	if (len_out) *len_out = (uint32_t)got;
	return F2FS_OK;
}

/* ==========================================================================================
 * f2fs_read
 * ========================================================================================== */
/*
 * Inline data: the content sits at `inline_data_offset` inside the inode block and is at most
 * MAX_INLINE_DATA bytes; F2FS_DATA_EXIST says whether the area holds anything (an inode can carry
 * the reservation with no content).
 */
static int read_inline(struct f2fs_volume *vol, const struct f2fs_inode *inode, uint64_t offset,
		       uint8_t *out, uint32_t want)
{
	uint32_t base = inline_area_offset(inode);
	uint32_t avail = inode->max_inline_data;

	memset(out, 0, want);
	if (!inode->has_data_exist) return F2FS_OK;
	if (inode->size < avail) avail = (uint32_t)inode->size;
	if (base + avail > 360u + F2FS_DEF_ADDRS_PER_INODE * 4u) {
		diag_plain(vol, NULL, F2FS_EPROTO, "the inline data area runs past the inode block");
		return F2FS_EPROTO;
	}
	for (uint32_t i = 0; i < want; i++) {
		uint64_t pos = offset + i;
		if (pos < avail) out[i] = inode->node[base + pos];
	}
	return F2FS_OK;
}

int f2fs_read(struct f2fs_volume *vol, const struct f2fs_inode *inode, uint64_t offset,
	      void *buffer, uint64_t length, uint64_t *read_out)
{
	uint8_t *out = (uint8_t *)buffer;
	uint64_t want, done = 0;
	int rc;

	if (read_out) *read_out = 0;
	if (!vol || !inode || (!buffer && length)) return F2FS_EINVAL;
	if (offset >= inode->size) return F2FS_OK;		/* a read at or past EOF: zero bytes */

	want = inode->size - offset;
	if (want > length) want = length;
	if (want == 0) return F2FS_OK;

	if (inode->has_inline_data) {
		/* The content of an inline-data inode lives in its own block, so `i_size` above
		 * MAX_INLINE_DATA cannot be describing content: refuse rather than cast a 64-bit length
		 * down to 32 bits. */
		if (want > inode->max_inline_data) {
			diag_plain(vol, NULL, F2FS_EPROTO,
				   "an inline-data inode claims more bytes than the inline area holds");
			return F2FS_EPROTO;
		}
		rc = read_inline(vol, inode, offset, out, (uint32_t)want);
		if (rc) return rc;
		if (read_out) *read_out = want;
		return F2FS_OK;
	}

	while (done < want) {
		uint64_t pos = offset + done;
		uint64_t blkidx = pos >> 12;
		uint32_t in_blk = (uint32_t)(pos & (F2FS_BLK - 1u));
		uint64_t n = F2FS_BLK - in_blk;
		uint32_t addr;
		uint8_t blk[F2FS_BLK];

		if (n > want - done) n = want - done;
		rc = f2fs_block_addr(vol, inode, blkidx, &addr);
		if (rc) {
			diag_plain(vol, NULL, rc, "the block map of this file is unreadable");
			return rc;
		}
		if (addr == F2FS_NULL_ADDR || addr == F2FS_NEW_ADDR) {
			memset(out + done, 0, (size_t)n);	/* a hole reads as zeros */
		} else if (addr == F2FS_COMPRESS_ADDR) {
			diag_plain(vol, NULL, F2FS_ENOTSUP,
				   "a compressed cluster (this reader refuses compression by name)");
			return F2FS_ENOTSUP;
		} else {
			if (addr < vol->main_blkaddr || (uint64_t)addr >= vol->block_count) {
				diag_plain(vol, NULL, F2FS_EPROTO,
					   "a block pointer outside the main area");
				return F2FS_EPROTO;
			}
			if (read_block(vol->device, addr, blk)) {
				diag_plain(vol, NULL, F2FS_EIO, "a data block read failed");
				return F2FS_EIO;
			}
			memcpy(out + done, blk + in_blk, (size_t)n);
		}
		done += n;
	}
	if (read_out) *read_out = done;
	return F2FS_OK;
}
