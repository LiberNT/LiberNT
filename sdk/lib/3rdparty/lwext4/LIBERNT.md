# lwext4 in LiberNT

Upstream: https://github.com/gkostka/lwext4, commit 58bcf89a121b72d4fb66334f1693d3b30e4cb9c5.

Only `include/` and `src/` are kept. `src/ext4.c` (the path-based API with its static mount table) and
`src/ext4_mbr.c` are not built: the ext4 driver calls the lower layers through `drivers/filesystems/ext4/core`.

## Changes made here

Features
- `metadata_csum_seed`: every metadata checksum starts from `ext4_sb_csum_seed()`, which returns the seed stored in
  the superblock when the feature is set. The superblock structure gained the fields up to `orphan_file_inum`.
- Feature bits for `orphan_file`, `orphan_present` and the other bits `mke2fs` 1.47 can set.
- `ext4_fs_peek_block_group_ref()` reads a group descriptor without initializing the group.
- `ext4_fs_set_inode_data_block_index()` is exported for files that do not use extents.
- `CONFIG_HAVE_PORT_DEBUG` routes `ext4_dbg` and `ext4_assert` to `ext4_port_dbg` and `ext4_port_assert_fail`.

Fixes
- Owner and group ids are read and written as 32-bit values (the high halves were dropped).
- `ext4_fs_init()` and `ext4_fs_fini()` no longer rewrite the superblock state; the caller owns it.
- `ext4_block_get_noread()` hands out a zeroed buffer when the buffer holds no data yet.
- `ext4_dir_dx_init()` clears the inode of the first leaf entry before the leaf checksum is computed.
- Zeroing an unwritten extent writes to the device directly, never through the block cache or the journal.
- `ext4_extent_remove_space()` starts at the next extent when the first block to remove lies in a hole.
- Merging a new extent in front of an existing one moves the physical start of the merged extent as well.
- `ext4_fs_get_inode_dblk_idx()` no longer answers "not mapped" for every block of a file whose size is zero.
- `ext4_xattr_remove()` removed an in-inode attribute through an uninitialized search context, reported success for
  a missing attribute, and checksummed a copied attribute block under the old block number.
- The in-inode attribute validator measured value offsets from the header instead of the first entry.
- `jbd_journal_flush_trans()` read a transaction and its buffer records after the write that frees them.
