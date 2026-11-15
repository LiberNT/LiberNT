/*
 * PROJECT:     LiberNT ext4 file system driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Growing a volume by whole or partial block groups, and shrinking it by whole block groups
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "e4core.h"
#include "e4priv.h"

#include <string.h>

#define E4_COPY_BLOCKS      64
#define E4_RESIZE_INODE     7
#define E4_DIND_SLOT        13
#define E4_DIRECT_SLOTS     12
#define E4_NO_LIMIT         0xFFFFFFFFFFFFFFFFULL

typedef struct _E4_MOVED_INODE
{
    uint32_t Old;
    uint32_t New;
} E4_MOVED_INODE;

typedef struct _E4_DEFERRED_RUN
{
    ext4_lblk_t Block;
    uint32_t Count;
    bool Unwritten;
} E4_DEFERRED_RUN;

typedef struct _E4_MOVED_BLOCK
{
    uint64_t Old;
    uint64_t New;
} E4_MOVED_BLOCK;

typedef struct _E4_SAVED_GROUP
{
    uint64_t BlockBitmap;
    uint64_t InodeBitmap;
    uint64_t InodeTable;
} E4_SAVED_GROUP;

typedef struct _E4_RESIZE
{
    E4_VOLUME *Volume;
    struct ext4_fs *Fs;
    struct ext4_sblock *Sb;
    struct ext4_blockdev *Device;
    uint32_t BlockSize;
    uint32_t BlocksPerGroup;
    uint32_t InodesPerGroup;
    uint32_t InodeSize;
    uint32_t TableBlocks;
    uint32_t DescPerBlock;
    uint32_t DescSize;
    uint64_t FirstDataBlock;
    uint64_t OldBlocks;
    uint64_t NewBlocks;
    uint32_t OldGroups;
    uint32_t NewGroups;
    uint32_t OldGdt;
    uint32_t NewGdt;
    uint32_t OldReserved;
    uint32_t Rotor;
    bool Checksums;
    E4_MOVED_INODE *Inodes;
    uint32_t InodeCount;
    uint32_t InodeCapacity;
    E4_DEFERRED_RUN *Deferred;
    uint32_t DeferredCount;
    uint32_t DeferredCapacity;
    E4_MOVED_BLOCK *Attributes;
    uint32_t AttributeCount;
    uint32_t AttributeCapacity;
    E4_SAVED_GROUP *Removed;
    uint8_t *Copy;
    uint8_t *Bitmap;
    int Error;
} E4_RESIZE;

typedef int (*E4_INODE_ROUTINE)(E4_RESIZE *Resize, uint32_t Inode);

static
uint64_t
E4GroupStart(
    E4_RESIZE *Resize,
    uint32_t Group)
{
    return Resize->FirstDataBlock + (uint64_t)Group * Resize->BlocksPerGroup;
}

static
uint32_t
E4GdtBlocks(
    E4_RESIZE *Resize,
    uint32_t Groups)
{
    return (Groups + Resize->DescPerBlock - 1) / Resize->DescPerBlock;
}

static
uint32_t
E4GroupsOf(
    E4_RESIZE *Resize,
    uint64_t Blocks)
{
    uint64_t Span = Blocks - Resize->FirstDataBlock;

    return (uint32_t)((Span + Resize->BlocksPerGroup - 1) / Resize->BlocksPerGroup);
}

static
void *
E4Grow(
    void *Array,
    uint32_t Count,
    uint32_t *Capacity,
    size_t Size)
{
    uint32_t Wanted;
    void *Larger;

    if (Count < *Capacity)
        return Array;
    Wanted = *Capacity ? *Capacity * 2 : 256;
    Larger = ext4_calloc(Wanted, Size);
    if (Larger == NULL)
        return NULL;
    if (Array != NULL)
    {
        memcpy(Larger, Array, (size_t)Count * Size);
        ext4_free(Array);
    }
    *Capacity = Wanted;
    return Larger;
}

/* Sets or clears the bits of a run of blocks in the bitmaps of the groups the volume keeps or gains, and keeps the
 * free counts of the groups in step. Blocks of a group that is going away are passed over. */
static
int
E4MarkBlocks(
    E4_RESIZE *Resize,
    uint64_t Block,
    uint32_t Count,
    bool Used)
{
    struct ext4_sblock *Sb = Resize->Sb;

    while (Count != 0)
    {
        uint32_t Group = (uint32_t)((Block - Resize->FirstDataBlock) / Resize->BlocksPerGroup);
        uint32_t Index = (uint32_t)((Block - Resize->FirstDataBlock) % Resize->BlocksPerGroup);
        uint32_t Chunk = Resize->BlocksPerGroup - Index;
        struct ext4_block_group_ref GroupRef;
        struct ext4_block Bitmap;
        uint32_t Changed = 0, Bit;
        int Result;

        if (Chunk > Count)
            Chunk = Count;
        if (Group < Resize->NewGroups)
        {
            Result = ext4_fs_get_block_group_ref(Resize->Fs, Group, &GroupRef);
            if (Result != EOK)
                return Result;
            Result = ext4_trans_block_get(Resize->Device, &Bitmap, ext4_bg_get_block_bitmap(GroupRef.block_group, Sb));
            if (Result != EOK)
            {
                ext4_fs_put_block_group_ref(&GroupRef);
                return Result;
            }

            for (Bit = Index; Bit < Index + Chunk; Bit++)
            {
                if (Used && ext4_bmap_is_bit_clr(Bitmap.data, Bit))
                {
                    ext4_bmap_bit_set(Bitmap.data, Bit);
                    Changed++;
                }
                else if (!Used && ext4_bmap_is_bit_set(Bitmap.data, Bit))
                {
                    ext4_bmap_bit_clr(Bitmap.data, Bit);
                    Changed++;
                }
            }
            if (Changed != 0)
            {
                uint32_t Free = ext4_bg_get_free_blocks_count(GroupRef.block_group, Sb);
                uint64_t Total = ext4_sb_get_free_blocks_cnt(Sb);

                ext4_balloc_set_bitmap_csum(Sb, GroupRef.block_group, Bitmap.data);
                ext4_trans_set_block_dirty(Bitmap.buf);
                ext4_bg_set_free_blocks_count(GroupRef.block_group, Sb, Used ? Free - Changed : Free + Changed);
                ext4_sb_set_free_blocks_cnt(Sb, Used ? Total - Changed : Total + Changed);
                GroupRef.dirty = true;
            }
            ext4_block_set(Resize->Device, &Bitmap);
            Result = ext4_fs_put_block_group_ref(&GroupRef);
            if (Result != EOK)
                return Result;
        }
        Block += Chunk;
        Count -= Chunk;
    }
    return EOK;
}

/* Takes a run of free blocks from the groups the volume keeps: exactly Count of them, or with Exact unset as many
 * as the first free run has, up to Count */
static
int
E4TakeBlocks(
    E4_RESIZE *Resize,
    uint32_t Count,
    bool Exact,
    uint64_t *Start,
    uint32_t *Taken)
{
    struct ext4_sblock *Sb = Resize->Sb;
    uint32_t Step;

    for (Step = 0; Step < Resize->NewGroups; Step++)
    {
        uint32_t Group = (Resize->Rotor + Step) % Resize->NewGroups;
        struct ext4_block_group_ref GroupRef;
        struct ext4_block Bitmap;
        uint32_t InGroup, Free, Bit = 0, Position = 0, Run = 0;
        bool Found = false;
        int Result;

        Result = ext4_fs_get_block_group_ref(Resize->Fs, Group, &GroupRef);
        if (Result != EOK)
            return Result;
        Free = ext4_bg_get_free_blocks_count(GroupRef.block_group, Sb);
        if ((Free == 0) || (Exact && (Free < Count)))
        {
            ext4_fs_put_block_group_ref(&GroupRef);
            continue;
        }
        Result = ext4_trans_block_get(Resize->Device, &Bitmap, ext4_bg_get_block_bitmap(GroupRef.block_group, Sb));
        if (Result != EOK)
        {
            ext4_fs_put_block_group_ref(&GroupRef);
            return Result;
        }

        InGroup = ext4_blocks_in_group_cnt(Sb, Group);
        while (Bit < InGroup)
        {
            if (ext4_bmap_bit_find_clr(Bitmap.data, Bit, InGroup, &Position) != EOK)
                break;
            for (Run = 1; (Run < Count) && (Position + Run < InGroup) && ext4_bmap_is_bit_clr(Bitmap.data, Position + Run); Run++);
            if (!Exact || (Run == Count))
            {
                Found = true;
                break;
            }
            Bit = Position + Run;
        }

        if (Found)
        {
            for (Bit = Position; Bit < Position + Run; Bit++)
                ext4_bmap_bit_set(Bitmap.data, Bit);
            ext4_balloc_set_bitmap_csum(Sb, GroupRef.block_group, Bitmap.data);
            ext4_trans_set_block_dirty(Bitmap.buf);
            ext4_bg_set_free_blocks_count(GroupRef.block_group, Sb, Free - Run);
            ext4_sb_set_free_blocks_cnt(Sb, ext4_sb_get_free_blocks_cnt(Sb) - Run);
            GroupRef.dirty = true;
        }
        ext4_block_set(Resize->Device, &Bitmap);
        Result = ext4_fs_put_block_group_ref(&GroupRef);
        if (Result != EOK)
            return Result;
        if (Found)
        {
            *Start = E4GroupStart(Resize, Group) + Position;
            *Taken = Run;
            Resize->Rotor = Group;
            return EOK;
        }
    }
    return ENOSPC;
}

static
int
E4RelocatorAlloc(
    void *Context,
    uint32_t Count,
    ext4_fsblk_t *Start)
{
    uint64_t Block = 0;
    uint32_t Taken = 0;
    int Result = E4TakeBlocks(Context, Count, true, &Block, &Taken);

    *Start = Block;
    return Result;
}

static
void
E4RelocatorRelease(
    void *Context,
    ext4_fsblk_t Start,
    uint32_t Count)
{
    E4_RESIZE *Resize = Context;
    int Result;

    ext4_bcache_invalidate_lba(Resize->Device->bc, Start, Count);
    Result = E4MarkBlocks(Resize, Start, Count, false);
    if ((Result != EOK) && (Resize->Error == EOK))
        Resize->Error = Result;
}

static
int
E4RelocatorCopy(
    void *Context,
    ext4_fsblk_t From,
    ext4_fsblk_t To,
    uint32_t Count)
{
    E4_RESIZE *Resize = Context;

    ext4_bcache_invalidate_lba(Resize->Device->bc, To, Count);
    while (Count != 0)
    {
        uint32_t Chunk = (Count > E4_COPY_BLOCKS) ? E4_COPY_BLOCKS : Count;
        int Result = ext4_blocks_get_direct(Resize->Device, Resize->Copy, From, Chunk);

        if (Result == EOK)
            Result = ext4_blocks_set_direct(Resize->Device, Resize->Copy, To, Chunk);
        if (Result != EOK)
            return Result;
        From += Chunk;
        To += Chunk;
        Count -= Chunk;
    }
    return EOK;
}

static
int
E4RelocatorDefer(
    void *Context,
    ext4_lblk_t Block,
    uint32_t Count,
    bool Unwritten)
{
    E4_RESIZE *Resize = Context;

    Resize->Deferred = E4Grow(Resize->Deferred, Resize->DeferredCount, &Resize->DeferredCapacity, sizeof(E4_DEFERRED_RUN));
    if (Resize->Deferred == NULL)
        return ENOMEM;
    Resize->Deferred[Resize->DeferredCount].Block = Block;
    Resize->Deferred[Resize->DeferredCount].Count = Count;
    Resize->Deferred[Resize->DeferredCount].Unwritten = Unwritten;
    Resize->DeferredCount++;
    return EOK;
}

/* A mapped run that found no room in one piece: its blocks move one at a time, wherever the allocator puts them.
 * A run that was only reserved for the file, and never written, is given up instead. */
static
int
E4MovePiecewise(
    E4_RESIZE *Resize,
    struct ext4_inode_ref *Ref,
    const E4_DEFERRED_RUN *Run)
{
    ext4_lblk_t Block;

    if (Run->Unwritten)
        return ext4_extent_remove_space(Ref, Run->Block, Run->Block + Run->Count - 1);

    for (Block = Run->Block; Block < Run->Block + Run->Count; Block++)
    {
        ext4_fsblk_t Old = 0, Fresh = 0;
        struct ext4_block Buffer;
        uint32_t Mapped = 0;
        int Result;

        Result = ext4_extent_get_blocks(Ref, Block, 1, &Old, false, &Mapped);
        if (Result != EOK)
            return Result;
        if ((Old == 0) || (Old < Resize->NewBlocks))
            continue;

        Result = ext4_trans_block_get(Resize->Device, &Buffer, Old);
        if (Result != EOK)
            return Result;
        memcpy(Resize->Copy, Buffer.data, Resize->BlockSize);
        ext4_block_set(Resize->Device, &Buffer);
        ext4_bcache_invalidate_lba(Resize->Device->bc, Old, 1);

        Result = ext4_extent_remove_space(Ref, Block, Block);
        if (Result == EOK)
            Result = ext4_extent_get_blocks(Ref, Block, 1, &Fresh, true, &Mapped);
        if ((Result == EOK) && ((Fresh == 0) || (Fresh >= Resize->NewBlocks)))
            Result = EIO;
        if (Result == EOK)
            Result = ext4_trans_block_get_noread(Resize->Device, &Buffer, Fresh);
        if (Result != EOK)
            return Result;
        memcpy(Buffer.data, Resize->Copy, Resize->BlockSize);
        ext4_trans_set_block_dirty(Buffer.buf);
        Result = ext4_block_set(Resize->Device, &Buffer);
        if (Result != EOK)
            return Result;
    }
    return EOK;
}

/* The attribute block of an inode moves once, however many inodes share it */
static
int
E4MoveAttributeBlock(
    E4_RESIZE *Resize,
    struct ext4_inode_ref *Ref)
{
    uint64_t Old = ext4_inode_get_file_acl(Ref->inode, Resize->Sb);
    struct ext4_block From, To;
    uint64_t Fresh = 0;
    uint32_t Taken = 0, Index;
    int Result;

    if ((Old == 0) || (Old < Resize->NewBlocks))
        return EOK;

    for (Index = 0; Index < Resize->AttributeCount; Index++)
    {
        if (Resize->Attributes[Index].Old == Old)
        {
            ext4_inode_set_file_acl(Ref->inode, Resize->Sb, Resize->Attributes[Index].New);
            Ref->dirty = true;
            return EOK;
        }
    }

    Resize->Attributes = E4Grow(Resize->Attributes, Resize->AttributeCount, &Resize->AttributeCapacity, sizeof(E4_MOVED_BLOCK));
    if (Resize->Attributes == NULL)
        return ENOMEM;
    Result = E4TakeBlocks(Resize, 1, true, &Fresh, &Taken);
    if (Result != EOK)
        return Result;
    Result = ext4_trans_block_get(Resize->Device, &From, Old);
    if (Result != EOK)
        return Result;
    Result = ext4_trans_block_get_noread(Resize->Device, &To, Fresh);
    if (Result != EOK)
    {
        ext4_block_set(Resize->Device, &From);
        return Result;
    }
    memcpy(To.data, From.data, Resize->BlockSize);
    ext4_xattr_set_block_checksum(Ref, Fresh, (struct ext4_xattr_header *)To.data);
    ext4_trans_set_block_dirty(To.buf);
    ext4_block_set(Resize->Device, &From);
    Result = ext4_block_set(Resize->Device, &To);
    if (Result != EOK)
        return Result;
    ext4_bcache_invalidate_lba(Resize->Device->bc, Old, 1);

    Resize->Attributes[Resize->AttributeCount].Old = Old;
    Resize->Attributes[Resize->AttributeCount].New = Fresh;
    Resize->AttributeCount++;
    ext4_inode_set_file_acl(Ref->inode, Resize->Sb, Fresh);
    Ref->dirty = true;
    return EOK;
}

/* Calls Routine for every inode in use of the groups First up to End, in ascending order */
static
int
E4ForEachInode(
    E4_RESIZE *Resize,
    uint32_t First,
    uint32_t End,
    E4_INODE_ROUTINE Routine)
{
    uint32_t Group;

    for (Group = First; Group < End; Group++)
    {
        struct ext4_block_group_ref GroupRef;
        struct ext4_block Bitmap;
        uint64_t BitmapBlock;
        bool Unused;
        uint32_t Bit;
        int Result;

        Result = ext4_fs_peek_block_group_ref(Resize->Fs, Group, &GroupRef);
        if (Result != EOK)
            return Result;
        Unused = ext4_bg_has_flag(GroupRef.block_group, EXT4_BLOCK_GROUP_INODE_UNINIT);
        BitmapBlock = ext4_bg_get_inode_bitmap(GroupRef.block_group, Resize->Sb);
        ext4_fs_put_block_group_ref(&GroupRef);
        if (Unused)
            continue;

        /* The routine may take inodes, which changes bitmaps: the walk goes over a copy */
        Result = ext4_trans_block_get(Resize->Device, &Bitmap, BitmapBlock);
        if (Result != EOK)
            return Result;
        memcpy(Resize->Bitmap, Bitmap.data, Resize->BlockSize);
        ext4_block_set(Resize->Device, &Bitmap);

        for (Bit = 0; Bit < Resize->InodesPerGroup; Bit++)
        {
            if (ext4_bmap_is_bit_clr(Resize->Bitmap, Bit))
                continue;
            Result = Routine(Resize, Group * Resize->InodesPerGroup + Bit + 1);
            if (Result != EOK)
                return Result;
        }
    }
    return EOK;
}

static
bool
E4HasBlocks(
    E4_RESIZE *Resize,
    struct ext4_inode *Inode)
{
    uint64_t Sectors = ext4_inode_get_blocks_count(Resize->Sb, Inode);
    uint64_t Attribute = ext4_inode_get_file_acl(Inode, Resize->Sb) ? (Resize->BlockSize >> 9) : 0;
    uint32_t Type = ext4_inode_type(Resize->Sb, Inode);

    if ((Type != EXT4_INODE_MODE_FILE) && (Type != EXT4_INODE_MODE_DIRECTORY) && (Type != EXT4_INODE_MODE_SOFTLINK))
        return false;
    return Sectors > Attribute;
}

/* Before anything changes: an inode that maps its blocks without extents must have none of them in the part of
 * the volume that goes away, since only extent trees are relocated */
static
int
E4CheckInode(
    E4_RESIZE *Resize,
    uint32_t Inode)
{
    struct ext4_inode_ref Ref;
    int Result = EOK;
    uint32_t Slot;

    if (Inode == E4_RESIZE_INODE)
        return EOK;
    Result = ext4_fs_get_inode_ref(Resize->Fs, Inode, &Ref);
    if (Result != EOK)
        return Result;

    if ((ext4_inode_get_mode(Resize->Sb, Ref.inode) != 0) && E4HasBlocks(Resize, Ref.inode) &&
        !ext4_inode_has_flag(Ref.inode, EXT4_INODE_FLAG_EXTENTS))
    {
        uint64_t Size = ext4_inode_get_size(Resize->Sb, Ref.inode);

        if (Size > (uint64_t)E4_DIRECT_SLOTS * Resize->BlockSize)
            Result = ENOTSUP;
        for (Slot = 0; (Result == EOK) && (Slot < E4_DIRECT_SLOTS); Slot++)
        {
            if (to_le32(Ref.inode->blocks[Slot]) >= Resize->NewBlocks)
                Result = ENOTSUP;
        }
    }
    Ref.dirty = false;
    ext4_fs_put_inode_ref(&Ref);
    return Result;
}

/* Every block of a directory carries a checksum seeded with the inode number of the directory */
static
int
E4ChecksumDirectory(
    E4_RESIZE *Resize,
    struct ext4_inode_ref *Ref)
{
    uint64_t Size = ext4_inode_get_size(Resize->Sb, Ref->inode);
    bool Indexed = ext4_inode_has_flag(Ref->inode, EXT4_INODE_FLAG_INDEX);
    ext4_lblk_t Block;

    if (!Resize->Checksums)
        return EOK;

    for (Block = 0; (uint64_t)Block * Resize->BlockSize < Size; Block++)
    {
        struct ext4_dir_en *Entry;
        struct ext4_block Buffer;
        ext4_fsblk_t Physical = 0;
        int Result;

        Result = ext4_fs_get_inode_dblk_idx(Ref, Block, &Physical, false);
        if (Result != EOK)
            return Result;
        if (Physical == 0)
            continue;
        Result = ext4_trans_block_get(Resize->Device, &Buffer, Physical);
        if (Result != EOK)
            return Result;

        /* The root of the index and its inner nodes end in an index tail, a block of entries in an entry tail.
         * An inner node is one unused entry that spans the whole block. */
        Entry = (struct ext4_dir_en *)Buffer.data;
        if (Indexed && ((Block == 0) || ((ext4_dir_en_get_inode(Entry) == 0) &&
                                        (ext4_dir_en_get_entry_len(Entry) == Resize->BlockSize))))
        {
            ext4_dir_set_dx_csum(Ref, Entry);
        }
        else
        {
            ext4_dir_set_csum(Ref, Entry);
        }
        ext4_trans_set_block_dirty(Buffer.buf);
        Result = ext4_block_set(Resize->Device, &Buffer);
        if (Result != EOK)
            return Result;
    }
    return EOK;
}

/* An inode of a group that goes away gets a number in a group that stays. Its content moves as it is; what is
 * checksummed with the inode number is checksummed again. The names that lead to it are rewritten afterwards. */
static
int
E4MoveInode(
    E4_RESIZE *Resize,
    uint32_t Inode)
{
    struct ext4_extent_relocator Relocator;
    struct ext4_inode_ref Old, Fresh;
    uint32_t Number = 0;
    bool IsDirectory;
    int Result;

    Result = ext4_fs_get_inode_ref(Resize->Fs, Inode, &Old);
    if (Result != EOK)
        return Result;
    if (ext4_inode_get_mode(Resize->Sb, Old.inode) == 0)
    {
        ext4_fs_put_inode_ref(&Old);
        return EOK;
    }
    IsDirectory = ext4_inode_is_type(Resize->Sb, Old.inode, EXT4_INODE_MODE_DIRECTORY);

    Resize->Inodes = E4Grow(Resize->Inodes, Resize->InodeCount, &Resize->InodeCapacity, sizeof(E4_MOVED_INODE));
    if (Resize->Inodes == NULL)
        Result = ENOMEM;
    if (Result == EOK)
        Result = ext4_ialloc_alloc_inode(Resize->Fs, &Number, IsDirectory);
    if (Result == EOK)
        Result = ext4_fs_get_inode_ref(Resize->Fs, Number, &Fresh);
    if (Result != EOK)
    {
        ext4_fs_put_inode_ref(&Old);
        return Result;
    }

    memcpy(Fresh.inode, Old.inode, Resize->InodeSize);
    Fresh.dirty = true;
    Old.dirty = false;
    ext4_fs_put_inode_ref(&Old);

    if (ext4_inode_has_flag(Fresh.inode, EXT4_INODE_FLAG_EXTENTS) && E4HasBlocks(Resize, Fresh.inode))
    {
        memset(&Relocator, 0, sizeof(Relocator));
        Relocator.limit = E4_NO_LIMIT;
        Relocator.recsum = true;
        Relocator.alloc = E4RelocatorAlloc;
        Relocator.release = E4RelocatorRelease;
        Relocator.copy = E4RelocatorCopy;
        Relocator.defer = E4RelocatorDefer;
        Relocator.ctx = Resize;
        Result = ext4_extent_relocate(&Fresh, &Relocator);
    }
    if ((Result == EOK) && IsDirectory)
        Result = E4ChecksumDirectory(Resize, &Fresh);

    if (Result == EOK)
        Result = ext4_fs_put_inode_ref(&Fresh);
    else
        ext4_fs_put_inode_ref(&Fresh);
    if (Result != EOK)
        return Result;

    Resize->Inodes[Resize->InodeCount].Old = Inode;
    Resize->Inodes[Resize->InodeCount].New = Number;
    Resize->InodeCount++;
    return EOK;
}

static
uint32_t
E4NewNumber(
    E4_RESIZE *Resize,
    uint32_t Inode)
{
    uint32_t Low = 0, High = Resize->InodeCount;

    while (Low < High)
    {
        uint32_t Middle = Low + (High - Low) / 2;

        if (Resize->Inodes[Middle].Old == Inode)
            return Resize->Inodes[Middle].New;
        if (Resize->Inodes[Middle].Old < Inode)
            Low = Middle + 1;
        else
            High = Middle;
    }
    return 0;
}

/* Every name in every directory that still says the old number of a moved inode gets the new one */
static
int
E4RenumberDirectory(
    E4_RESIZE *Resize,
    uint32_t Inode)
{
    struct ext4_inode_ref Ref;
    ext4_lblk_t Block;
    uint64_t Size;
    bool Indexed;
    int Result;

    Result = ext4_fs_get_inode_ref(Resize->Fs, Inode, &Ref);
    if (Result != EOK)
        return Result;
    if (!ext4_inode_is_type(Resize->Sb, Ref.inode, EXT4_INODE_MODE_DIRECTORY))
    {
        Ref.dirty = false;
        return ext4_fs_put_inode_ref(&Ref);
    }

    Size = ext4_inode_get_size(Resize->Sb, Ref.inode);
    Indexed = ext4_inode_has_flag(Ref.inode, EXT4_INODE_FLAG_INDEX);
    for (Block = 0; (Result == EOK) && ((uint64_t)Block * Resize->BlockSize < Size); Block++)
    {
        struct ext4_block Buffer;
        ext4_fsblk_t Physical = 0;
        uint32_t Offset = 0;
        bool Changed = false;

        Result = ext4_fs_get_inode_dblk_idx(&Ref, Block, &Physical, false);
        if ((Result != EOK) || (Physical == 0))
            continue;
        Result = ext4_trans_block_get(Resize->Device, &Buffer, Physical);
        if (Result != EOK)
            break;

        while (Offset + 8 <= Resize->BlockSize)
        {
            struct ext4_dir_en *Entry = (struct ext4_dir_en *)(Buffer.data + Offset);
            uint32_t Length = ext4_dir_en_get_entry_len(Entry);
            uint32_t Target = ext4_dir_en_get_inode(Entry);
            uint32_t Fresh;

            if ((Length < 8) || (Offset + Length > Resize->BlockSize))
                break;
            Fresh = (Target != 0) ? E4NewNumber(Resize, Target) : 0;
            if (Fresh != 0)
            {
                ext4_dir_en_set_inode(Entry, Fresh);
                Changed = true;
            }
            Offset += Length;
        }

        if (Changed)
        {
            if (Resize->Checksums)
            {
                if (Indexed && (Block == 0))
                    ext4_dir_set_dx_csum(&Ref, (struct ext4_dir_en *)Buffer.data);
                else
                    ext4_dir_set_csum(&Ref, (struct ext4_dir_en *)Buffer.data);
            }
            ext4_trans_set_block_dirty(Buffer.buf);
        }
        Result = ext4_block_set(Resize->Device, &Buffer);
    }

    Ref.dirty = false;
    if (Result == EOK)
        Result = ext4_fs_put_inode_ref(&Ref);
    else
        ext4_fs_put_inode_ref(&Ref);
    return Result;
}

/* Brings the blocks of one inode out of the part of the volume that goes away */
static
int
E4MoveInodeBlocks(
    E4_RESIZE *Resize,
    uint32_t Inode)
{
    struct ext4_extent_relocator Relocator;
    struct ext4_inode_ref Ref;
    uint32_t Index;
    int Result;

    if (Inode == E4_RESIZE_INODE)
        return EOK;
    Result = ext4_fs_get_inode_ref(Resize->Fs, Inode, &Ref);
    if (Result != EOK)
        return Result;
    if (ext4_inode_get_mode(Resize->Sb, Ref.inode) == 0)
    {
        Ref.dirty = false;
        return ext4_fs_put_inode_ref(&Ref);
    }

    Result = E4MoveAttributeBlock(Resize, &Ref);
    if ((Result == EOK) && ext4_inode_has_flag(Ref.inode, EXT4_INODE_FLAG_EXTENTS) && E4HasBlocks(Resize, Ref.inode))
    {
        memset(&Relocator, 0, sizeof(Relocator));
        Relocator.limit = Resize->NewBlocks;
        Relocator.alloc = E4RelocatorAlloc;
        Relocator.release = E4RelocatorRelease;
        Relocator.copy = E4RelocatorCopy;
        Relocator.defer = E4RelocatorDefer;
        Relocator.ctx = Resize;
        Resize->DeferredCount = 0;
        Result = ext4_extent_relocate(&Ref, &Relocator);
        for (Index = 0; (Result == EOK) && (Index < Resize->DeferredCount); Index++)
            Result = E4MovePiecewise(Resize, &Ref, &Resize->Deferred[Index]);
        if ((Result == EOK) && (Resize->Error != EOK))
            Result = Resize->Error;
    }

    if (Result == EOK)
        Result = ext4_fs_put_inode_ref(&Ref);
    else
        ext4_fs_put_inode_ref(&Ref);
    return Result;
}

/* The inode that owns the blocks reserved for a growing descriptor table is given up, and the blocks with it:
 * a table that changes its size no longer matches what the inode maps */
static
int
E4DropResizeInode(
    E4_RESIZE *Resize,
    uint32_t Groups)
{
    struct ext4_sblock *Sb = Resize->Sb;
    struct ext4_inode_ref Ref;
    uint32_t Group;
    uint64_t Dind;
    int Result;

    if (!ext4_sb_feature_com(Sb, EXT4_FCOM_RESIZE_INODE))
        return EOK;

    /* From here on a group whose bitmap is still to be built is built without the reserved blocks */
    ext4_set16(Sb, s_reserved_gdt_blocks, 0);
    ext4_set32(Sb, features_compatible, ext4_get32(Sb, features_compatible) & ~EXT4_FCOM_RESIZE_INODE);

    for (Group = 0; (Resize->OldReserved != 0) && (Group < Groups); Group++)
    {
        struct ext4_block_group_ref GroupRef;
        bool Unbuilt;

        if (!ext4_sb_is_super_in_bg(Sb, Group))
            continue;
        Result = ext4_fs_peek_block_group_ref(Resize->Fs, Group, &GroupRef);
        if (Result != EOK)
            return Result;
        Unbuilt = ext4_bg_has_flag(GroupRef.block_group, EXT4_BLOCK_GROUP_BLOCK_UNINIT);
        ext4_fs_put_block_group_ref(&GroupRef);
        if (Unbuilt)
            continue;
        Result = E4MarkBlocks(Resize, E4GroupStart(Resize, Group) + 1 + Resize->OldGdt, Resize->OldReserved, false);
        if (Result != EOK)
            return Result;
    }

    Result = ext4_fs_get_inode_ref(Resize->Fs, E4_RESIZE_INODE, &Ref);
    if (Result != EOK)
        return Result;
    Dind = to_le32(Ref.inode->blocks[E4_DIND_SLOT]);
    memset(Ref.inode, 0, Resize->InodeSize);
    ext4_trans_set_block_dirty(Ref.block.buf);
    Ref.dirty = false;
    Result = ext4_fs_put_inode_ref(&Ref);
    if ((Result == EOK) && (Dind != 0) && (Dind < Resize->NewBlocks))
    {
        ext4_bcache_invalidate_lba(Resize->Device->bc, Dind, 1);
        Result = E4MarkBlocks(Resize, Dind, 1, false);
    }
    return Result;
}

/* Writes the superblock, and a copy of it and of the descriptor table into every group that keeps one */
static
int
E4WriteSuperblocks(
    E4_RESIZE *Resize)
{
    struct ext4_sblock *Sb = Resize->Sb;
    struct ext4_inode_ref Journal;
    uint32_t Group, Index;
    int Result;

    if (ext4_sb_feature_com(Sb, EXT4_FCOM_HAS_JOURNAL) && (ext4_get32(Sb, journal_inode_number) != 0))
    {
        /* The superblock keeps a copy of how the journal inode maps its blocks */
        Result = ext4_fs_get_inode_ref(Resize->Fs, ext4_get32(Sb, journal_inode_number), &Journal);
        if (Result != EOK)
            return Result;
        for (Index = 0; Index < 15; Index++)
            Sb->journal_blocks[Index] = Journal.inode->blocks[Index];
        Sb->journal_blocks[15] = Journal.inode->size_hi;
        Sb->journal_blocks[16] = Journal.inode->size_lo;
        Journal.dirty = false;
        ext4_fs_put_inode_ref(&Journal);
    }

    Result = ext4_block_cache_flush(Resize->Device);
    if (Result != EOK)
        return Result;
    Result = E4CountFree(Resize->Volume);
    if (Result != EOK)
        return Result;
    Result = ext4_sb_write(Resize->Device, Sb);
    if (Result != EOK)
        return Result;

    for (Group = 1; Group < Resize->NewGroups; Group++)
    {
        uint64_t Start = E4GroupStart(Resize, Group);

        if (!ext4_sb_is_super_in_bg(Sb, Group))
            continue;

        ext4_bcache_invalidate_lba(Resize->Device->bc, Start, 1 + Resize->NewGdt);
        ext4_set16(Sb, block_group_index, (uint16_t)Group);
        ext4_sb_set_csum(Sb);
        Result = ext4_block_writebytes(Resize->Device, Start * Resize->BlockSize, Sb, EXT4_SUPERBLOCK_SIZE);
        for (Index = 0; (Result == EOK) && (Index < Resize->NewGdt); Index++)
        {
            Result = ext4_blocks_get_direct(Resize->Device, Resize->Copy, Resize->FirstDataBlock + 1 + Index, 1);
            if (Result == EOK)
                Result = ext4_blocks_set_direct(Resize->Device, Resize->Copy, Start + 1 + Index, 1);
        }
        if (Result != EOK)
            break;
    }
    ext4_set16(Sb, block_group_index, 0);
    ext4_sb_set_csum(Sb);
    return Result;
}

static
void
E4ScaleReserve(
    E4_RESIZE *Resize)
{
    struct ext4_sblock *Sb = Resize->Sb;
    uint64_t Reserved = ((uint64_t)ext4_get32(Sb, reserved_blocks_count_hi) << 32) | ext4_get32(Sb, reserved_blocks_count_lo);

    /* The share of the volume held back for the superuser stays what it was */
    if (Reserved != 0)
        Reserved = Resize->NewBlocks / (Resize->OldBlocks / Reserved);
    ext4_set32(Sb, reserved_blocks_count_lo, (uint32_t)Reserved);
    ext4_set32(Sb, reserved_blocks_count_hi, (uint32_t)(Reserved >> 32));
}

static
int
E4Shrink(
    E4_RESIZE *Resize)
{
    struct ext4_sblock *Sb = Resize->Sb;
    uint32_t Removed = Resize->OldGroups - Resize->NewGroups;
    uint32_t Group, Used = 0, Free = 0, Slot;
    int Result;

    /* TODO: only volumes whose files map their blocks with extents shrink; so do neither meta_bg nor bigalloc volumes */
    if (!ext4_sb_feature_incom(Sb, EXT4_FINCOM_EXTENTS) || ext4_sb_feature_incom(Sb, EXT4_FINCOM_META_BG) ||
        ext4_sb_feature_ro_com(Sb, EXT4_FRO_COM_BIGALLOC))
    {
        return ENOTSUP;
    }

    Resize->Removed = ext4_calloc(Removed, sizeof(E4_SAVED_GROUP));
    if (Resize->Removed == NULL)
        return ENOMEM;

    for (Group = 0; Group < Resize->OldGroups; Group++)
    {
        struct ext4_block_group_ref GroupRef;
        uint64_t BlockBitmap, InodeBitmap, InodeTable;

        Result = ext4_fs_peek_block_group_ref(Resize->Fs, Group, &GroupRef);
        if (Result != EOK)
            return Result;
        BlockBitmap = ext4_bg_get_block_bitmap(GroupRef.block_group, Sb);
        InodeBitmap = ext4_bg_get_inode_bitmap(GroupRef.block_group, Sb);
        InodeTable = ext4_bg_get_inode_table_first_block(GroupRef.block_group, Sb);
        if (Group < Resize->NewGroups)
        {
            Free += ext4_bg_get_free_inodes_count(GroupRef.block_group, Sb);
        }
        else
        {
            Used += Resize->InodesPerGroup - ext4_bg_get_free_inodes_count(GroupRef.block_group, Sb);
            Resize->Removed[Group - Resize->NewGroups].BlockBitmap = BlockBitmap;
            Resize->Removed[Group - Resize->NewGroups].InodeBitmap = InodeBitmap;
            Resize->Removed[Group - Resize->NewGroups].InodeTable = InodeTable;
        }
        ext4_fs_put_block_group_ref(&GroupRef);

        /* A group that stays must not keep its own bitmaps or inodes where the volume ends */
        if ((Group < Resize->NewGroups) &&
            ((BlockBitmap >= Resize->NewBlocks) || (InodeBitmap >= Resize->NewBlocks) ||
             (InodeTable + Resize->TableBlocks > Resize->NewBlocks)))
        {
            return ENOTSUP;
        }
    }
    if (Used > Free)
        return ENOSPC;
    for (Slot = 0; Slot < 3; Slot++)
    {
        /* TODO: quota files with an inode number in a group that goes away are not renumbered */
        uint32_t Quota = (Slot == 0) ? ext4_get32(Sb, usr_quota_inum) : (Slot == 1) ? ext4_get32(Sb, grp_quota_inum) :
                         ext4_get32(Sb, prj_quota_inum);

        if (Quota > Resize->NewGroups * Resize->InodesPerGroup)
            return ENOTSUP;
    }
    if (ext4_get32(Sb, journal_inode_number) > Resize->NewGroups * Resize->InodesPerGroup)
        return ENOTSUP;

    Result = E4ForEachInode(Resize, 0, Resize->OldGroups, E4CheckInode);
    if (Result != EOK)
        return Result;

    /* From here on the allocators see the volume at its new size */
    ext4_sb_set_blocks_cnt(Sb, Resize->NewBlocks);
    Resize->Fs->last_inode_bg_id = 0;
    Result = E4DropResizeInode(Resize, Resize->NewGroups);
    if (Result != EOK)
        return Result;

    Result = E4ForEachInode(Resize, Resize->NewGroups, Resize->OldGroups, E4MoveInode);
    if ((Result == EOK) && (Resize->InodeCount != 0))
        Result = E4ForEachInode(Resize, 0, Resize->NewGroups, E4RenumberDirectory);
    if (Result != EOK)
        return Result;

    /* The blocks are copied from the device, so what the cache holds of them goes to the device first */
    Result = ext4_block_cache_flush(Resize->Device);
    if (Result != EOK)
        return Result;
    Result = E4ForEachInode(Resize, 0, Resize->NewGroups, E4MoveInodeBlocks);
    if (Result != EOK)
        return Result;

    /* What the groups that go away kept in the groups that stay is free now, and so is the end of the table */
    for (Group = 0; Group < Removed; Group++)
    {
        Result = E4MarkBlocks(Resize, Resize->Removed[Group].BlockBitmap, 1, false);
        if (Result == EOK)
            Result = E4MarkBlocks(Resize, Resize->Removed[Group].InodeBitmap, 1, false);
        if (Result == EOK)
            Result = E4MarkBlocks(Resize, Resize->Removed[Group].InodeTable, Resize->TableBlocks, false);
        if (Result != EOK)
            return Result;
        if (Resize->Removed[Group].BlockBitmap < Resize->NewBlocks)
            ext4_bcache_invalidate_lba(Resize->Device->bc, Resize->Removed[Group].BlockBitmap, 1);
        if (Resize->Removed[Group].InodeBitmap < Resize->NewBlocks)
            ext4_bcache_invalidate_lba(Resize->Device->bc, Resize->Removed[Group].InodeBitmap, 1);
    }
    for (Group = 0; (Resize->NewGdt < Resize->OldGdt) && (Group < Resize->NewGroups); Group++)
    {
        struct ext4_block_group_ref GroupRef;
        bool Unbuilt;

        if (!ext4_sb_is_super_in_bg(Sb, Group))
            continue;
        Result = ext4_fs_peek_block_group_ref(Resize->Fs, Group, &GroupRef);
        if (Result != EOK)
            return Result;
        Unbuilt = ext4_bg_has_flag(GroupRef.block_group, EXT4_BLOCK_GROUP_BLOCK_UNINIT);
        ext4_fs_put_block_group_ref(&GroupRef);
        if (Unbuilt)
            continue;
        ext4_bcache_invalidate_lba(Resize->Device->bc, E4GroupStart(Resize, Group) + 1 + Resize->NewGdt,
                                   Resize->OldGdt - Resize->NewGdt);
        Result = E4MarkBlocks(Resize, E4GroupStart(Resize, Group) + 1 + Resize->NewGdt, Resize->OldGdt - Resize->NewGdt, false);
        if (Result != EOK)
            return Result;
    }

    Slot = Resize->NewGroups % Resize->DescPerBlock;
    if (Slot != 0)
    {
        struct ext4_block Table;

        Result = ext4_trans_block_get(Resize->Device, &Table, Resize->FirstDataBlock + 1 + Resize->NewGroups / Resize->DescPerBlock);
        if (Result != EOK)
            return Result;
        memset(Table.data + Slot * Resize->DescSize, 0, Resize->BlockSize - Slot * Resize->DescSize);
        ext4_trans_set_block_dirty(Table.buf);
        Result = ext4_block_set(Resize->Device, &Table);
        if (Result != EOK)
            return Result;
    }

    ext4_set32(Sb, inodes_count, Resize->NewGroups * Resize->InodesPerGroup);
    E4ScaleReserve(Resize);
    return E4WriteSuperblocks(Resize);
}

static
int
E4Extend(
    E4_RESIZE *Resize)
{
    struct ext4_sblock *Sb = Resize->Sb;
    bool Lazy = ext4_sb_feature_ro_com(Sb, EXT4_FRO_COM_GDT_CSUM) || Resize->Checksums;
    uint32_t Group, LastOld = Resize->OldGroups - 1;
    uint64_t OldEnd = Resize->OldBlocks, LastEnd;
    int Result;

    /* TODO: a meta_bg or bigalloc volume does not grow */
    if (ext4_sb_feature_incom(Sb, EXT4_FINCOM_META_BG) || ext4_sb_feature_ro_com(Sb, EXT4_FRO_COM_BIGALLOC))
        return ENOTSUP;

    if (Resize->NewGroups != Resize->OldGroups)
    {
        /* The table of a larger volume takes the blocks that follow it in every group with a copy: they are the
         * reserved ones, or they have to be free */
        for (Group = 0; (Resize->NewGdt > Resize->OldGdt) && (Group < Resize->OldGroups); Group++)
        {
            uint64_t First = E4GroupStart(Resize, Group) + 1 + Resize->OldGdt;
            uint32_t Needed = Resize->NewGdt - Resize->OldGdt, Bit;
            struct ext4_block_group_ref GroupRef;
            struct ext4_block Bitmap;
            bool Taken = false;

            if (!ext4_sb_is_super_in_bg(Sb, Group) || (Needed <= Resize->OldReserved))
                continue;
            Result = ext4_fs_get_block_group_ref(Resize->Fs, Group, &GroupRef);
            if (Result != EOK)
                return Result;
            Result = ext4_trans_block_get(Resize->Device, &Bitmap, ext4_bg_get_block_bitmap(GroupRef.block_group, Sb));
            if (Result != EOK)
            {
                ext4_fs_put_block_group_ref(&GroupRef);
                return Result;
            }
            for (Bit = Resize->OldReserved; Bit < Needed; Bit++)
            {
                if (ext4_bmap_is_bit_set(Bitmap.data, (uint32_t)(First + Bit - E4GroupStart(Resize, Group))))
                    Taken = true;
            }
            ext4_block_set(Resize->Device, &Bitmap);
            ext4_fs_put_block_group_ref(&GroupRef);
            if (Taken)
                return ENOTSUP;
        }
    }

    ext4_sb_set_blocks_cnt(Sb, Resize->NewBlocks);
    ext4_set32(Sb, inodes_count, Resize->NewGroups * Resize->InodesPerGroup);

    if (Resize->NewGroups != Resize->OldGroups)
    {
        Result = E4DropResizeInode(Resize, Resize->OldGroups);
        if (Result != EOK)
            return Result;

        for (Group = 0; (Resize->NewGdt > Resize->OldGdt) && (Group < Resize->OldGroups); Group++)
        {
            uint64_t First = E4GroupStart(Resize, Group) + 1 + Resize->OldGdt;
            struct ext4_block_group_ref GroupRef;
            uint32_t Index;
            bool Unbuilt;

            if (!ext4_sb_is_super_in_bg(Sb, Group))
                continue;
            for (Index = 0; Index < Resize->NewGdt - Resize->OldGdt; Index++)
            {
                struct ext4_block Table;

                Result = ext4_trans_block_get_noread(Resize->Device, &Table, First + Index);
                if (Result != EOK)
                    return Result;
                memset(Table.data, 0, Resize->BlockSize);
                ext4_trans_set_block_dirty(Table.buf);
                Result = ext4_block_set(Resize->Device, &Table);
                if (Result != EOK)
                    return Result;
            }
            Result = ext4_fs_peek_block_group_ref(Resize->Fs, Group, &GroupRef);
            if (Result != EOK)
                return Result;
            Unbuilt = ext4_bg_has_flag(GroupRef.block_group, EXT4_BLOCK_GROUP_BLOCK_UNINIT);
            ext4_fs_put_block_group_ref(&GroupRef);
            if (!Unbuilt)
            {
                Result = E4MarkBlocks(Resize, First, Resize->NewGdt - Resize->OldGdt, true);
                if (Result != EOK)
                    return Result;
            }
        }
    }

    /* The group that was the last one reaches its full size, or the new end of the volume */
    LastEnd = E4GroupStart(Resize, LastOld) + Resize->BlocksPerGroup;
    if (LastEnd > Resize->NewBlocks)
        LastEnd = Resize->NewBlocks;
    if (LastEnd > OldEnd)
    {
        struct ext4_block_group_ref GroupRef;
        uint32_t Added = (uint32_t)(LastEnd - OldEnd);
        bool Unbuilt;

        Result = ext4_fs_peek_block_group_ref(Resize->Fs, LastOld, &GroupRef);
        if (Result != EOK)
            return Result;
        Unbuilt = ext4_bg_has_flag(GroupRef.block_group, EXT4_BLOCK_GROUP_BLOCK_UNINIT);
        if (!Unbuilt)
        {
            /* The bits of the blocks the group did not have may have been kept set: they are free blocks now */
            uint32_t First = (uint32_t)(OldEnd - E4GroupStart(Resize, LastOld)), Bit;
            struct ext4_block Bitmap;

            Result = ext4_trans_block_get(Resize->Device, &Bitmap, ext4_bg_get_block_bitmap(GroupRef.block_group, Sb));
            if (Result != EOK)
            {
                ext4_fs_put_block_group_ref(&GroupRef);
                return Result;
            }
            for (Bit = First; Bit < First + Added; Bit++)
                ext4_bmap_bit_clr(Bitmap.data, Bit);
            ext4_balloc_set_bitmap_csum(Sb, GroupRef.block_group, Bitmap.data);
            ext4_trans_set_block_dirty(Bitmap.buf);
            ext4_block_set(Resize->Device, &Bitmap);
        }
        ext4_bg_set_free_blocks_count(GroupRef.block_group, Sb, ext4_bg_get_free_blocks_count(GroupRef.block_group, Sb) + Added);
        GroupRef.dirty = true;
        Result = ext4_fs_put_block_group_ref(&GroupRef);
        if (Result != EOK)
            return Result;
    }

    for (Group = Resize->OldGroups; Group < Resize->NewGroups; Group++)
    {
        uint64_t Start = E4GroupStart(Resize, Group);
        uint64_t End = Start + Resize->BlocksPerGroup;
        uint32_t Overhead = ext4_sb_is_super_in_bg(Sb, Group) ? 1 + Resize->NewGdt : 0;
        struct ext4_block_group_ref GroupRef;
        uint32_t InGroup;

        if (End > Resize->NewBlocks)
            End = Resize->NewBlocks;
        InGroup = (uint32_t)(End - Start);

        Result = ext4_fs_peek_block_group_ref(Resize->Fs, Group, &GroupRef);
        if (Result != EOK)
            return Result;
        memset(GroupRef.block_group, 0, Resize->DescSize);
        ext4_bg_set_block_bitmap(GroupRef.block_group, Sb, Start + Overhead);
        ext4_bg_set_inode_bitmap(GroupRef.block_group, Sb, Start + Overhead + 1);
        ext4_bg_set_inode_table_first_block(GroupRef.block_group, Sb, Start + Overhead + 2);
        ext4_bg_set_free_blocks_count(GroupRef.block_group, Sb, InGroup - Overhead - 2 - Resize->TableBlocks);
        ext4_bg_set_free_inodes_count(GroupRef.block_group, Sb, Resize->InodesPerGroup);
        ext4_bg_set_itable_unused(GroupRef.block_group, Sb, Resize->InodesPerGroup);
        ext4_bg_set_flag(GroupRef.block_group, EXT4_BLOCK_GROUP_BLOCK_UNINIT | EXT4_BLOCK_GROUP_INODE_UNINIT);
        GroupRef.dirty = true;
        Result = ext4_fs_put_block_group_ref(&GroupRef);
        if (Result != EOK)
            return Result;

        /* Without descriptor checksums a group cannot be left to be built on first use, and the last group of a
         * volume never is: its bitmap marks where the volume ends */
        if (!Lazy || (Group == Resize->NewGroups - 1))
        {
            Result = ext4_fs_get_block_group_ref(Resize->Fs, Group, &GroupRef);
            if (Result != EOK)
                return Result;
            GroupRef.dirty = true;
            Result = ext4_fs_put_block_group_ref(&GroupRef);
            if (Result != EOK)
                return Result;
        }
    }

    E4ScaleReserve(Resize);
    return E4WriteSuperblocks(Resize);
}

static
void
E4ResizeSetup(
    E4_RESIZE *Resize,
    E4_VOLUME *Volume)
{
    struct ext4_sblock *Sb = &Volume->Fs.sb;

    memset(Resize, 0, sizeof(*Resize));
    Resize->Volume = Volume;
    Resize->Fs = &Volume->Fs;
    Resize->Sb = Sb;
    Resize->Device = &Volume->Device;
    Resize->BlockSize = Volume->BlockSize;
    Resize->BlocksPerGroup = ext4_get32(Sb, blocks_per_group);
    Resize->InodesPerGroup = ext4_get32(Sb, inodes_per_group);
    Resize->InodeSize = ext4_get16(Sb, inode_size);
    Resize->TableBlocks = (Resize->InodesPerGroup * Resize->InodeSize + Resize->BlockSize - 1) / Resize->BlockSize;
    Resize->DescSize = ext4_sb_get_desc_size(Sb);
    Resize->DescPerBlock = Resize->BlockSize / Resize->DescSize;
    Resize->FirstDataBlock = ext4_get32(Sb, first_data_block);
    Resize->OldBlocks = ext4_sb_get_blocks_cnt(Sb);
    Resize->OldGroups = ext4_block_group_cnt(Sb);
    Resize->OldGdt = E4GdtBlocks(Resize, Resize->OldGroups);
    Resize->OldReserved = ext4_get16(Sb, s_reserved_gdt_blocks);
    Resize->Checksums = ext4_sb_feature_ro_com(Sb, EXT4_FRO_COM_METADATA_CSUM);
}

int
E4ResizeQuery(
    E4_VOLUME *Volume,
    E4_RESIZE_INFO *Info)
{
    struct ext4_sblock *Sb = &Volume->Fs.sb;
    uint64_t *FreeBlocks;
    uint32_t *FreeInodes;
    uint64_t BlocksAbove = 0, FreeBelow = 0;
    uint32_t InodesAbove = 0, InodesBelow = 0;
    uint32_t Group, Keep;
    E4_RESIZE Resize;
    int Result = EOK;

    if (!Volume->Mounted)
        return ENXIO;
    E4ResizeSetup(&Resize, Volume);
    Info->Blocks = Resize.OldBlocks;
    Info->MinimumBlocks = Resize.OldBlocks;
    Info->BlocksPerGroup = Resize.BlocksPerGroup;
    Info->BlockSize = Resize.BlockSize;

    FreeBlocks = ext4_calloc(Resize.OldGroups, sizeof(uint64_t));
    FreeInodes = ext4_calloc(Resize.OldGroups, sizeof(uint32_t));
    if ((FreeBlocks == NULL) || (FreeInodes == NULL))
        Result = ENOMEM;
    for (Group = 0; (Result == EOK) && (Group < Resize.OldGroups); Group++)
    {
        struct ext4_block_group_ref GroupRef;

        Result = ext4_fs_peek_block_group_ref(&Volume->Fs, Group, &GroupRef);
        if (Result != EOK)
            break;
        FreeBlocks[Group] = ext4_bg_get_free_blocks_count(GroupRef.block_group, Sb);
        FreeInodes[Group] = ext4_bg_get_free_inodes_count(GroupRef.block_group, Sb);
        FreeBelow += FreeBlocks[Group];
        InodesBelow += FreeInodes[Group];
        ext4_fs_put_block_group_ref(&GroupRef);
    }

    /* Groups come off the end for as long as what they hold fits, with room to spare, into what the others have free */
    for (Keep = Resize.OldGroups; (Result == EOK) && (Keep > 1); Keep--)
    {
        uint32_t Last = Keep - 1;
        uint64_t InGroup = (Last == Resize.OldGroups - 1) ? Resize.OldBlocks - E4GroupStart(&Resize, Last) : Resize.BlocksPerGroup;
        uint64_t Above = BlocksAbove + (InGroup - FreeBlocks[Last]);
        uint64_t Below = FreeBelow - FreeBlocks[Last];
        uint32_t UsedInodes = InodesAbove + (Resize.InodesPerGroup - FreeInodes[Last]);
        uint32_t RoomInodes = InodesBelow - FreeInodes[Last];

        if ((Above + Above / 16 + 4096 > Below) || (UsedInodes > RoomInodes))
            break;
        BlocksAbove = Above;
        FreeBelow = Below;
        InodesAbove = UsedInodes;
        InodesBelow = RoomInodes;
    }
    if (Result == EOK)
        Info->MinimumBlocks = E4GroupStart(&Resize, Keep);
    if (Info->MinimumBlocks > Resize.OldBlocks)
        Info->MinimumBlocks = Resize.OldBlocks;

    if (FreeBlocks != NULL)
        ext4_free(FreeBlocks);
    if (FreeInodes != NULL)
        ext4_free(FreeInodes);
    return Result;
}

int
E4Resize(
    E4_VOLUME *Volume,
    uint64_t Blocks,
    uint64_t *Reached)
{
    struct ext4_sblock *Sb = &Volume->Fs.sb;
    struct ext4_sblock Saved;
    bool Journaled = Volume->Journaled;
    E4_RESIZE Resize;
    uint64_t LastStart;
    int Result, Restart = EOK;

    if (!Volume->Mounted)
        return ENXIO;
    if (Volume->ReadOnly || Volume->Failed)
        return EROFS;
    ext4_assert(Volume->TransDepth == 0);

    E4ResizeSetup(&Resize, Volume);
    *Reached = Resize.OldBlocks;
    if (Blocks > Volume->Device.part_size / Volume->BlockSize)
        Blocks = Volume->Device.part_size / Volume->BlockSize;

    if (Blocks < Resize.OldBlocks)
    {
        /* A smaller volume ends where a group ends */
        Resize.NewGroups = (uint32_t)((Blocks - Resize.FirstDataBlock) / Resize.BlocksPerGroup);
        if (Resize.NewGroups == 0)
            return EINVAL;
        Resize.NewBlocks = E4GroupStart(&Resize, Resize.NewGroups);
        if (Resize.NewBlocks >= Resize.OldBlocks)
            return EOK;
    }
    else
    {
        /* A last group too small to hold its own bitmaps and inodes is left out */
        Resize.NewBlocks = Blocks;
        Resize.NewGroups = E4GroupsOf(&Resize, Blocks);
        LastStart = E4GroupStart(&Resize, Resize.NewGroups - 1);
        if ((Resize.NewGroups > Resize.OldGroups) &&
            (Blocks - LastStart < (uint64_t)E4GdtBlocks(&Resize, Resize.NewGroups) + Resize.TableBlocks + 3 + 64))
        {
            Resize.NewBlocks = LastStart;
            Resize.NewGroups--;
        }
        if (Resize.NewBlocks <= Resize.OldBlocks)
            return EOK;
    }
    Resize.NewGdt = E4GdtBlocks(&Resize, Resize.NewGroups);

    Resize.Copy = ext4_malloc((size_t)E4_COPY_BLOCKS * Resize.BlockSize);
    Resize.Bitmap = ext4_malloc(Resize.BlockSize);
    if ((Resize.Copy == NULL) || (Resize.Bitmap == NULL))
    {
        if (Resize.Copy != NULL)
            ext4_free(Resize.Copy);
        if (Resize.Bitmap != NULL)
            ext4_free(Resize.Bitmap);
        return ENOMEM;
    }

    /* The change is too large for one transaction, so it is made with the journal empty and closed, and with the
     * volume marked as one to check: FIXME: a crash in the middle leaves a volume that needs e2fsck */
    Result = ext4_block_cache_flush(&Volume->Device);
    if ((Result == EOK) && Journaled)
    {
        Result = jbd_journal_stop(&Volume->Journal);
        E4StageStop(Volume);
        if (Result != EOK)
            Volume->JournalFs.dirty = false;
        jbd_put_fs(&Volume->JournalFs);
        Volume->Fs.jbd_fs = NULL;
        Volume->Fs.jbd_journal = NULL;
        Volume->Journaled = false;
    }
    if (Result == EOK)
    {
        memcpy(&Saved, Sb, sizeof(Saved));
        ext4_set16(Sb, state, ext4_get16(Sb, state) & ~EXT4_SUPERBLOCK_STATE_VALID_FS);
        Result = ext4_sb_write(&Volume->Device, Sb);
    }

    if (Result == EOK)
    {
        Result = (Resize.NewBlocks < Resize.OldBlocks) ? E4Shrink(&Resize) : E4Extend(&Resize);
        if (Result == EOK)
        {
            ext4_set16(Sb, state, ext4_get16(&Saved, state));
            Result = ext4_sb_write(&Volume->Device, Sb);
        }
        else if ((ext4_sb_get_blocks_cnt(Sb) == Resize.OldBlocks) && (Resize.InodeCount == 0))
        {
            /* Nothing was changed yet: the volume is what it was */
            memcpy(Sb, &Saved, sizeof(Saved));
            ext4_sb_write(&Volume->Device, Sb);
        }
        else
        {
            Volume->Failed = true;
        }
    }
    if (Result == EOK)
        Result = ext4_block_cache_flush(&Volume->Device);

    if (Journaled && !Volume->Failed)
    {
        Restart = jbd_get_fs(&Volume->Fs, &Volume->JournalFs);
        if (Restart == EOK)
        {
            Restart = jbd_journal_start(&Volume->JournalFs, &Volume->Journal);
            if (Restart != EOK)
            {
                Volume->JournalFs.dirty = false;
                jbd_put_fs(&Volume->JournalFs);
            }
        }
        if (Restart == EOK)
        {
            Volume->Fs.jbd_fs = &Volume->JournalFs;
            Volume->Fs.jbd_journal = &Volume->Journal;
            Volume->Journaled = true;
            E4StageStart(Volume);
        }
        else
        {
            Volume->Failed = true;
        }
    }

    if (Resize.Inodes != NULL)
        ext4_free(Resize.Inodes);
    if (Resize.Deferred != NULL)
        ext4_free(Resize.Deferred);
    if (Resize.Attributes != NULL)
        ext4_free(Resize.Attributes);
    if (Resize.Removed != NULL)
        ext4_free(Resize.Removed);
    ext4_free(Resize.Copy);
    ext4_free(Resize.Bitmap);

    if (Result == EOK)
        Result = Restart;
    if (Result == EOK)
        *Reached = Resize.NewBlocks;
    return Result;
}
