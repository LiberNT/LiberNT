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
- `ext4_extent_relocate()` moves the blocks of an extent tree, and the blocks it maps, out of the part of a volume
  that is given up when the volume shrinks, without changing the shape of the tree.
- `ext4_dir_set_dx_csum()`, `ext4_xattr_set_block_checksum()` and `ext4_sb_set_csum()` are exported.
- `CONFIG_HAVE_PORT_DEBUG` routes `ext4_dbg` and `ext4_assert` to `ext4_port_dbg` and `ext4_port_assert_fail`.
- `ext4_balloc_alloc_blocks()` allocates a run of contiguous blocks, and `ext4_extent_get_blocks()` maps a hole
  with one run instead of one block per call.
- `struct ext4_fs` counts the calls that freed blocks (`free_events`), for a caller that delays the journal.

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
- The number of block groups and the size of the last one were computed without the first data block, which is
  block 1 on a volume with 1024-byte blocks.
- `ext4_balloc_alloc_block()` started its search in the group of a goal that lies past the end of the volume.
- `ext4_extent_get_blocks()` sized a new extent up to the extent after the one the search stopped at, even when
  that one already lay past the requested block; with runs longer than one block the new extent overlapped it.
- `jbd_journal_stop()` reset the transaction sequence to zero, so every session numbered its transactions from one
  and a later recovery ran on into the records an earlier session had left in the log. The sequence now carries on.
- A checkpoint wrote, for every transaction, its own copy of each block from the journal to the block's place, and
  the journal superblock after each transaction. A copy that a later committed transaction supersedes is no longer
  written, and the journal superblock is written once per checkpoint.
- A full journal checkpoints every transaction instead of one, which cost a checkpoint for each later commit.
- Recovery asserted on a failed allocation of a revoke entry and trusted the entry count of a revoke block; both
  are now reported as errors.
- A failed insertion while splitting an unwritten extent returned success and could dereference a dropped path.
- Recovery took block 0 for the block of the superblock; with 1024-byte blocks that is block 1.
- The block cache no longer rejects block 0, which holds the superblock when blocks are larger than 1024 bytes
  and has to go through the journal with the orphan list.
- `jbd_journal_stop()` cleared the recovery flag of the file system before it marked the journal empty.
- `jbd_journal_prepare()` passed the revoke record it found where a `bool` is expected; it now passes whether a
  record was found, because MSVC 14.52 rejects the pointer.
