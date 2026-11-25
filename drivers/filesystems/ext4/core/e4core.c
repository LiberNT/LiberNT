/*
 * PROJECT:     LiberNT ext4 file system driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Mount, transactions, attributes and name operations on top of lwext4
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "e4core.h"
#include "e4priv.h"

#include <string.h>

#define E4_TRUNCATE_STEP    (16ULL * 1024 * 1024)

static int E4JournalSuper(E4_VOLUME *Volume);
static bool E4OrphansAreReleasable(E4_VOLUME *Volume);
static int E4OrphanCleanup(E4_VOLUME *Volume);
#define E4_FAST_LINK_MAX    (sizeof(((struct ext4_inode *)0)->blocks))

void
E4Now(
    E4_VOLUME *Volume,
    E4_TIME *Time)
{
    Time->Seconds = 0;
    Time->Nanoseconds = 0;
    if (Volume->Now != NULL)
        Volume->Now(Volume, Time);
}

int
E4Get(
    E4_VOLUME *Volume,
    uint32_t Inode,
    struct ext4_inode_ref *Ref)
{
    if (!Volume->Mounted)
        return ENXIO;
    if ((Inode == 0) || (Inode > ext4_get32(&Volume->Fs.sb, inodes_count)))
        return ENOENT;
    return ext4_fs_get_inode_ref(&Volume->Fs, Inode, Ref);
}

static
bool
E4HasExtra(
    E4_VOLUME *Volume,
    struct ext4_inode *Inode,
    uint32_t End)
{
    if (ext4_get16(&Volume->Fs.sb, inode_size) <= EXT4_GOOD_OLD_INODE_SIZE)
        return false;
    return (uint32_t)EXT4_GOOD_OLD_INODE_SIZE + to_le16(Inode->extra_isize) >= End;
}

static
void
E4DecodeTime(
    uint32_t Base,
    uint32_t Extra,
    bool HasExtra,
    E4_TIME *Time)
{
    Time->Seconds = (int32_t)Base;
    Time->Nanoseconds = 0;
    if (HasExtra)
    {
        Time->Seconds += (int64_t)(Extra & 3) << 32;
        Time->Nanoseconds = Extra >> 2;
    }
}

static
void
E4EncodeTime(
    const E4_TIME *Time,
    uint32_t *Base,
    uint32_t *Extra)
{
    uint32_t Low = (uint32_t)Time->Seconds;
    uint32_t Epoch = (uint32_t)((Time->Seconds - (int32_t)Low) >> 32) & 3;

    *Base = Low;
    *Extra = (Time->Nanoseconds << 2) | Epoch;
}

#define E4_FIELD_END(Field) ((uint32_t)(offsetof(struct ext4_inode, Field) + sizeof(((struct ext4_inode *)0)->Field)))

static
void
E4SetTimes(
    E4_VOLUME *Volume,
    struct ext4_inode_ref *Ref,
    uint32_t Mask,
    const E4_TIME *Access,
    const E4_TIME *Modify,
    const E4_TIME *Change,
    const E4_TIME *Create)
{
    struct ext4_inode *Inode = Ref->inode;
    uint32_t Base, Extra;

    if (Mask & E4_SET_ACCESS)
    {
        E4EncodeTime(Access, &Base, &Extra);
        Inode->access_time = to_le32(Base);
        if (E4HasExtra(Volume, Inode, E4_FIELD_END(atime_extra)))
            Inode->atime_extra = to_le32(Extra);
    }
    if (Mask & E4_SET_MODIFY)
    {
        E4EncodeTime(Modify, &Base, &Extra);
        Inode->modification_time = to_le32(Base);
        if (E4HasExtra(Volume, Inode, E4_FIELD_END(mtime_extra)))
            Inode->mtime_extra = to_le32(Extra);
    }
    if (Mask & E4_SET_CHANGE)
    {
        E4EncodeTime(Change, &Base, &Extra);
        Inode->change_inode_time = to_le32(Base);
        if (E4HasExtra(Volume, Inode, E4_FIELD_END(ctime_extra)))
            Inode->ctime_extra = to_le32(Extra);
    }
    if ((Mask & E4_SET_CREATE) && E4HasExtra(Volume, Inode, E4_FIELD_END(crtime_extra)))
    {
        E4EncodeTime(Create, &Base, &Extra);
        Inode->crtime = to_le32(Base);
        Inode->crtime_extra = to_le32(Extra);
    }
    Ref->dirty = true;
}

static
void
E4Touch(
    E4_VOLUME *Volume,
    struct ext4_inode_ref *Ref,
    uint32_t Mask)
{
    E4_TIME Now;

    E4Now(Volume, &Now);
    E4SetTimes(Volume, Ref, Mask, &Now, &Now, &Now, &Now);
}

static
uint8_t
E4ModeToEntryType(
    uint32_t Mode)
{
    switch (Mode & E4_S_IFMT)
    {
        case E4_S_IFREG: return EXT4_DE_REG_FILE;
        case E4_S_IFDIR: return EXT4_DE_DIR;
        case E4_S_IFCHR: return EXT4_DE_CHRDEV;
        case E4_S_IFBLK: return EXT4_DE_BLKDEV;
        case E4_S_IFIFO: return EXT4_DE_FIFO;
        case E4_S_IFSOCK: return EXT4_DE_SOCK;
        case E4_S_IFLNK: return EXT4_DE_SYMLINK;
        default: return EXT4_DE_UNKNOWN;
    }
}

static
void
E4SetDevice(
    struct ext4_inode *Inode,
    uint32_t Major,
    uint32_t Minor)
{
    memset(Inode->blocks, 0, sizeof(Inode->blocks));
    if ((Major < 256) && (Minor < 256))
        Inode->blocks[0] = to_le32((Major << 8) | Minor);
    else
        Inode->blocks[1] = to_le32((Minor & 0xFF) | (Major << 8) | ((Minor & ~0xFFU) << 12));
}

static
void
E4GetDevice(
    struct ext4_inode *Inode,
    uint32_t *Major,
    uint32_t *Minor)
{
    uint32_t Old = to_le32(Inode->blocks[0]);
    uint32_t New = to_le32(Inode->blocks[1]);

    if (Old != 0)
    {
        *Major = (Old >> 8) & 0xFF;
        *Minor = Old & 0xFF;
    }
    else
    {
        *Major = (New & 0xFFF00) >> 8;
        *Minor = (New & 0xFF) | ((New >> 12) & 0xFFF00);
    }
}

int
E4CountFree(
    E4_VOLUME *Volume)
{
    struct ext4_sblock *Sb = &Volume->Fs.sb;
    uint32_t Groups = ext4_block_group_cnt(Sb);
    uint64_t FreeBlocks = 0;
    uint32_t FreeInodes = 0;
    uint32_t Group;

    for (Group = 0; Group < Groups; Group++)
    {
        struct ext4_block_group_ref Ref;
        int Result = ext4_fs_peek_block_group_ref(&Volume->Fs, Group, &Ref);

        if (Result != EOK)
            return Result;
        FreeBlocks += ext4_bg_get_free_blocks_count(Ref.block_group, Sb);
        FreeInodes += ext4_bg_get_free_inodes_count(Ref.block_group, Sb);
        ext4_fs_put_block_group_ref(&Ref);
    }
    ext4_sb_set_free_blocks_cnt(Sb, FreeBlocks);
    ext4_set32(Sb, free_inodes_count, FreeInodes);
    return EOK;
}

int
E4Begin(
    E4_VOLUME *Volume)
{
    if (!Volume->Mounted)
        return ENXIO;
    if (Volume->ReadOnly || Volume->Failed)
        return EROFS;
    if ((Volume->TransDepth == 0) && Volume->Journaled)
    {
        ext4_assert(Volume->Fs.curr_trans == NULL);
        Volume->Fs.curr_trans = jbd_journal_new_trans(&Volume->Journal);
        if (Volume->Fs.curr_trans == NULL)
            return ENOMEM;
    }
    Volume->TransDepth++;
    return EOK;
}

int
E4End(
    E4_VOLUME *Volume,
    int Result)
{
    ext4_assert(Volume->TransDepth != 0);
    Volume->TransDepth--;
    if ((Volume->TransDepth != 0) || !Volume->Journaled)
        return Result;

    ext4_assert(Volume->Fs.curr_trans != NULL);
    if (Result == EOK)
    {
        Result = jbd_journal_commit_trans(&Volume->Journal, Volume->Fs.curr_trans);
        if (Result != EOK)
            Volume->Failed = true;
        else
            E4StageCommit(Volume);
    }
    else
    {
        jbd_journal_free_trans(&Volume->Journal, Volume->Fs.curr_trans, true);
    }
    Volume->Fs.curr_trans = NULL;

    /* The caller is about to write file data into blocks this operation gave it */
    if (Volume->StageSettle)
    {
        Volume->StageSettle = false;
        if (Result == EOK)
            Result = E4StageSettleFreed(Volume);
    }
    return Result;
}

int
E4Mount(
    E4_VOLUME *Volume,
    bool ReadOnly,
    uint32_t CacheBlocks)
{
    struct ext4_sblock *Sb = &Volume->Fs.sb;
    E4_TIME Now;
    int Result;

    Volume->Mounted = false;
    Volume->Journaled = false;
    Volume->Failed = false;
    Volume->TransDepth = 0;
    Volume->StageData = NULL;
    Volume->StageCount = 0;
    Volume->StageRunCount = 0;
    Volume->StageSettle = false;

    Result = ext4_block_init(&Volume->Device);
    if (Result != EOK)
        return Result;

    Result = ext4_fs_init(&Volume->Fs, &Volume->Device, ReadOnly);
    if (Result != EOK)
    {
        ext4_block_fini(&Volume->Device);
        return Result;
    }

    Volume->BlockSize = ext4_sb_get_block_size(Sb);
    ext4_block_set_lb_size(&Volume->Device, Volume->BlockSize);
    Result = ext4_bcache_init_dynamic(&Volume->Cache, CacheBlocks, Volume->BlockSize);
    if (Result != EOK)
    {
        ext4_block_fini(&Volume->Device);
        return Result;
    }
    ext4_block_bind_bcache(&Volume->Device, &Volume->Cache);
    Volume->Device.fs = &Volume->Fs;

    if (ext4_sb_feature_incom(Sb, EXT4_FINCOM_RECOVER))
    {
        if (!ext4_sb_feature_com(Sb, EXT4_FCOM_HAS_JOURNAL))
        {
            Result = ENOTSUP;
        }
        else
        {
            Result = jbd_get_fs(&Volume->Fs, &Volume->JournalFs);
            if (Result == EOK)
            {
                Result = jbd_recover(&Volume->JournalFs);
                jbd_put_fs(&Volume->JournalFs);
            }
            if (Result == EOK)
                Result = ext4_block_cache_flush(&Volume->Device);
            if (Result == EOK)
                Result = ext4_sb_read(&Volume->Device, Sb);
        }
        if (Result != EOK)
        {
            ext4_bcache_cleanup(&Volume->Cache);
            ext4_bcache_fini_dynamic(&Volume->Cache);
            ext4_block_fini(&Volume->Device);
            return Result;
        }
    }

    /* TODO: the orphan file a crash of another system leaves behind is not read; such a volume is mounted
     * read-only */
    if (ext4_sb_feature_ro_com(Sb, EXT4_FRO_COM_ORPHAN_PRESENT))
        Volume->Fs.read_only = true;
    else if ((ext4_get32(Sb, last_orphan) != 0) && !E4OrphansAreReleasable(Volume))
        Volume->Fs.read_only = true;

    Volume->ReadOnly = Volume->Fs.read_only;
    Volume->WasValid = (ext4_get16(Sb, state) & EXT4_SUPERBLOCK_STATE_VALID_FS) != 0;
    E4Now(Volume, &Now);
    Volume->GenerationSeed = (uint32_t)Now.Seconds ^ Now.Nanoseconds ^ ext4_get32(Sb, mount_count);

    if (!Volume->ReadOnly)
    {
        ext4_set32(Sb, mount_time, (uint32_t)Now.Seconds);
        if (ext4_sb_feature_com(Sb, EXT4_FCOM_HAS_JOURNAL))
        {
            Result = jbd_get_fs(&Volume->Fs, &Volume->JournalFs);
            if (Result == EOK)
            {
                Result = jbd_journal_start(&Volume->JournalFs, &Volume->Journal);
                if (Result != EOK)
                {
                    Volume->JournalFs.dirty = false;
                    jbd_put_fs(&Volume->JournalFs);
                }
            }
            if (Result == EOK)
            {
                Volume->Fs.jbd_fs = &Volume->JournalFs;
                Volume->Fs.jbd_journal = &Volume->Journal;
                Volume->Journaled = true;
            }
        }
        else
        {
            ext4_set16(Sb, state, ext4_get16(Sb, state) & ~EXT4_SUPERBLOCK_STATE_VALID_FS);
            Result = ext4_sb_write(&Volume->Device, Sb);
        }
        if (Result == EOK)
            Result = E4CountFree(Volume);
        if (Result != EOK)
        {
            ext4_bcache_cleanup(&Volume->Cache);
            ext4_bcache_fini_dynamic(&Volume->Cache);
            ext4_block_fini(&Volume->Device);
            return Result;
        }
    }

    ext4_block_cache_write_back(&Volume->Device, 1);
    Volume->Mounted = true;
    if (Volume->Journaled)
        E4StageStart(Volume);

    if (!Volume->ReadOnly && (ext4_get32(Sb, last_orphan) != 0))
    {
        Result = E4OrphanCleanup(Volume);
        if (Result != EOK)
        {
            E4Unmount(Volume);
            return Result;
        }
    }
    return EOK;
}

int
E4Sync(
    E4_VOLUME *Volume)
{
    E4_TIME Now;
    int Result;

    if (!Volume->Mounted || Volume->ReadOnly)
        return EOK;
    ext4_assert(Volume->TransDepth == 0);

    Result = E4StageFlush(Volume);
    if (Result == EOK)
        Result = ext4_block_cache_flush(&Volume->Device);
    if (Volume->Journaled)
        jbd_journal_purge_cp_trans(&Volume->Journal, true, false);
    E4Now(Volume, &Now);
    ext4_set32(&Volume->Fs.sb, write_time, (uint32_t)Now.Seconds);
    if (Result == EOK)
        Result = ext4_sb_write(&Volume->Device, &Volume->Fs.sb);
    if (Result != EOK)
        Volume->Failed = true;
    return Result;
}

int
E4Unmount(
    E4_VOLUME *Volume)
{
    struct ext4_sblock *Sb = &Volume->Fs.sb;
    int Result = EOK;

    if (!Volume->Mounted)
        return EOK;
    ext4_assert(Volume->TransDepth == 0);

    if (!Volume->ReadOnly)
    {
        Result = E4StageFlush(Volume);
        if (Result == EOK)
            Result = ext4_block_cache_flush(&Volume->Device);
        if (Volume->Journaled)
        {
            int Stop = jbd_journal_stop(&Volume->Journal);

            E4StageStop(Volume);
            if (Stop != EOK)
                Volume->JournalFs.dirty = false;
            jbd_put_fs(&Volume->JournalFs);
            Volume->Fs.jbd_fs = NULL;
            Volume->Fs.jbd_journal = NULL;
            Volume->Journaled = false;
            if (Result == EOK)
                Result = Stop;
        }
        else if (Volume->WasValid && !Volume->Failed)
        {
            ext4_set16(Sb, state, ext4_get16(Sb, state) | EXT4_SUPERBLOCK_STATE_VALID_FS);
        }
        if (Result == EOK)
            Result = ext4_block_cache_flush(&Volume->Device);
    }

    ext4_fs_fini(&Volume->Fs);
    ext4_block_cache_write_back(&Volume->Device, 0);
    ext4_bcache_cleanup(&Volume->Cache);
    ext4_bcache_fini_dynamic(&Volume->Cache);
    ext4_block_fini(&Volume->Device);
    Volume->Device.fs = NULL;
    Volume->Mounted = false;
    return Result;
}

int
E4StatFs(
    E4_VOLUME *Volume,
    E4_STATFS *Stat)
{
    struct ext4_sblock *Sb = &Volume->Fs.sb;
    uint64_t Reserved = ((uint64_t)ext4_get32(Sb, reserved_blocks_count_hi) << 32) |
                        ext4_get32(Sb, reserved_blocks_count_lo);

    Stat->BlockSize = Volume->BlockSize;
    Stat->Blocks = ext4_sb_get_blocks_cnt(Sb);
    Stat->FreeBlocks = ext4_sb_get_free_blocks_cnt(Sb);
    Stat->AvailableBlocks = (Stat->FreeBlocks > Reserved) ? Stat->FreeBlocks - Reserved : 0;
    Stat->Inodes = ext4_get32(Sb, inodes_count);
    Stat->FreeInodes = ext4_get32(Sb, free_inodes_count);
    memcpy(Stat->Uuid, Sb->uuid, sizeof(Stat->Uuid));
    memcpy(Stat->Label, Sb->volume_name, 16);
    Stat->Label[16] = 0;
    return EOK;
}

int
E4SetLabel(
    E4_VOLUME *Volume,
    const char *Label,
    uint32_t Length)
{
    struct ext4_sblock *Sb = &Volume->Fs.sb;
    int Result;

    if (!Volume->Mounted)
        return ENXIO;
    if (Volume->ReadOnly || Volume->Failed)
        return EROFS;
    if (Length > sizeof(Sb->volume_name))
        return EINVAL;
    memset(Sb->volume_name, 0, sizeof(Sb->volume_name));
    memcpy(Sb->volume_name, Label, Length);

    /* The journal may hold older copies of the superblock: the newest one has to be the one with the label */
    Result = E4Begin(Volume);
    if (Result != EOK)
        return Result;
    Result = E4End(Volume, E4JournalSuper(Volume));
    if (Result != EOK)
        return Result;
    return E4Sync(Volume);
}

int
E4GetAttr(
    E4_VOLUME *Volume,
    uint32_t Inode,
    E4_ATTR *Attr)
{
    struct ext4_sblock *Sb = &Volume->Fs.sb;
    struct ext4_inode_ref Ref;
    struct ext4_inode *Raw;
    int Result;

    Result = E4Get(Volume, Inode, &Ref);
    if (Result != EOK)
        return Result;
    Raw = Ref.inode;

    memset(Attr, 0, sizeof(*Attr));
    Attr->Mode = ext4_inode_get_mode(Sb, Raw) & 0xFFFF;
    Attr->Uid = ext4_inode_get_uid(Raw);
    Attr->Gid = ext4_inode_get_gid(Raw);
    Attr->Links = ext4_inode_get_links_cnt(Raw);
    Attr->Flags = ext4_inode_get_flags(Raw);
    Attr->Generation = ext4_inode_get_generation(Raw);
    Attr->Size = ext4_inode_get_size(Sb, Raw);
    Attr->Sectors = ext4_inode_get_blocks_count(Sb, Raw);
    if (((Attr->Mode & E4_S_IFMT) == E4_S_IFCHR) || ((Attr->Mode & E4_S_IFMT) == E4_S_IFBLK))
        E4GetDevice(Raw, &Attr->DeviceMajor, &Attr->DeviceMinor);

    E4DecodeTime(to_le32(Raw->access_time), E4HasExtra(Volume, Raw, E4_FIELD_END(atime_extra)) ? to_le32(Raw->atime_extra) : 0,
                 E4HasExtra(Volume, Raw, E4_FIELD_END(atime_extra)), &Attr->Access);
    E4DecodeTime(to_le32(Raw->modification_time), E4HasExtra(Volume, Raw, E4_FIELD_END(mtime_extra)) ? to_le32(Raw->mtime_extra) : 0,
                 E4HasExtra(Volume, Raw, E4_FIELD_END(mtime_extra)), &Attr->Modify);
    E4DecodeTime(to_le32(Raw->change_inode_time), E4HasExtra(Volume, Raw, E4_FIELD_END(ctime_extra)) ? to_le32(Raw->ctime_extra) : 0,
                 E4HasExtra(Volume, Raw, E4_FIELD_END(ctime_extra)), &Attr->Change);
    if (E4HasExtra(Volume, Raw, E4_FIELD_END(crtime_extra)))
        E4DecodeTime(to_le32(Raw->crtime), to_le32(Raw->crtime_extra), true, &Attr->Create);
    else
        Attr->Create = Attr->Change;

    Ref.dirty = false;
    return ext4_fs_put_inode_ref(&Ref);
}

int
E4SetAttr(
    E4_VOLUME *Volume,
    uint32_t Inode,
    uint32_t Mask,
    const E4_ATTR *Attr)
{
    struct ext4_sblock *Sb = &Volume->Fs.sb;
    struct ext4_inode_ref Ref;
    int Result;

    Result = E4Begin(Volume);
    if (Result != EOK)
        return Result;
    Result = E4Get(Volume, Inode, &Ref);
    if (Result != EOK)
        return E4End(Volume, Result);

    if (Mask & E4_SET_MODE)
    {
        uint32_t Mode = ext4_inode_get_mode(Sb, Ref.inode);

        ext4_inode_set_mode(Sb, Ref.inode, (Mode & E4_S_IFMT) | (Attr->Mode & 07777));
    }
    if (Mask & E4_SET_UID)
        ext4_inode_set_uid(Ref.inode, Attr->Uid);
    if (Mask & E4_SET_GID)
        ext4_inode_set_gid(Ref.inode, Attr->Gid);
    if (Mask & E4_SET_DEVICE)
    {
        uint32_t Type = ext4_inode_get_mode(Sb, Ref.inode) & E4_S_IFMT;

        if ((Type == E4_S_IFCHR) || (Type == E4_S_IFBLK))
            E4SetDevice(Ref.inode, Attr->DeviceMajor, Attr->DeviceMinor);
    }
    E4SetTimes(Volume, &Ref, Mask, &Attr->Access, &Attr->Modify, &Attr->Change, &Attr->Create);
    if (!(Mask & E4_SET_CHANGE))
        E4Touch(Volume, &Ref, E4_SET_CHANGE);
    Ref.dirty = true;

    Result = ext4_fs_put_inode_ref(&Ref);
    return E4End(Volume, Result);
}

static
int
E4StoreLinkTarget(
    E4_VOLUME *Volume,
    struct ext4_inode_ref *Ref,
    const char *Target,
    uint32_t Length)
{
    struct ext4_sblock *Sb = &Volume->Fs.sb;
    int Result = EOK;

    ext4_inode_clear_flag(Ref->inode, EXT4_INODE_FLAG_EXTENTS);
    memset(Ref->inode->blocks, 0, sizeof(Ref->inode->blocks));
    if (Length < E4_FAST_LINK_MAX)
    {
        memcpy(Ref->inode->blocks, Target, Length);
    }
    else
    {
        struct ext4_block Block;
        ext4_fsblk_t Physical;
        ext4_lblk_t Logical;

        if (ext4_sb_feature_incom(Sb, EXT4_FINCOM_EXTENTS))
        {
            ext4_inode_set_flag(Ref->inode, EXT4_INODE_FLAG_EXTENTS);
            ext4_extent_tree_init(Ref);
        }
        Result = ext4_fs_append_inode_dblk(Ref, &Physical, &Logical);
        if (Result == EOK)
            Result = ext4_trans_block_get_noread(&Volume->Device, &Block, Physical);
        if (Result == EOK)
        {
            memset(Block.data, 0, Volume->BlockSize);
            memcpy(Block.data, Target, Length);
            ext4_bcache_set_flag(Block.buf, BC_UPTODATE);
            ext4_trans_set_block_dirty(Block.buf);
            Result = ext4_block_set(&Volume->Device, &Block);
        }
    }
    if (Result == EOK)
        ext4_inode_set_size(Ref->inode, Length);
    return Result;
}

static
int
E4SetTypeNode(
    E4_VOLUME *Volume,
    uint32_t Directory,
    const char *Name,
    uint32_t NameLength,
    uint32_t Inode,
    uint32_t Mode,
    const char *LinkTarget,
    uint32_t LinkTargetLength)
{
    struct ext4_sblock *Sb = &Volume->Fs.sb;
    struct ext4_dir_search_result Found;
    struct ext4_inode_ref Parent, Child;
    uint32_t NewType = Mode & E4_S_IFMT;
    uint64_t Sectors, AttributeSectors;
    int Result;

    if (LinkTarget != NULL)
    {
        if (NewType != E4_S_IFLNK)
            return EINVAL;
        if ((LinkTargetLength == 0) || (LinkTargetLength > E4_SYMLINK_MAX) || (LinkTargetLength > Volume->BlockSize))
            return ENAMETOOLONG;
    }
    else if ((NewType != E4_S_IFIFO) && (NewType != E4_S_IFCHR) && (NewType != E4_S_IFBLK) &&
             (NewType != E4_S_IFSOCK))
    {
        return EINVAL;
    }
    if (Directory == 0)
        return ENOTSUP;

    Result = E4Begin(Volume);
    if (Result != EOK)
        return Result;
    Result = E4Get(Volume, Directory, &Parent);
    if (Result != EOK)
        return E4End(Volume, Result);
    Result = E4Get(Volume, Inode, &Child);
    if (Result != EOK)
    {
        ext4_fs_put_inode_ref(&Parent);
        return E4End(Volume, Result);
    }

    Sectors = ext4_inode_get_blocks_count(Sb, Child.inode);
    AttributeSectors = ext4_inode_get_file_acl(Child.inode, Sb) ? (Volume->BlockSize >> 9) : 0;
    if (!ext4_inode_is_type(Sb, Child.inode, EXT4_INODE_MODE_FILE) || (ext4_inode_get_size(Sb, Child.inode) != 0) ||
        (Sectors != AttributeSectors) || (ext4_inode_get_links_cnt(Child.inode) != 1))
    {
        Result = EINVAL;
    }

    if (Result == EOK)
    {
        Result = ext4_dir_find_entry(&Found, &Parent, Name, NameLength);
        if ((Result == EOK) && (ext4_dir_en_get_inode(Found.dentry) != Inode))
            Result = ENOENT;
        if (Result == EOK)
        {
            if (LinkTarget != NULL)
            {
                Result = E4StoreLinkTarget(Volume, &Child, LinkTarget, LinkTargetLength);
            }
            else
            {
                ext4_inode_clear_flag(Child.inode, EXT4_INODE_FLAG_EXTENTS);
                memset(Child.inode->blocks, 0, sizeof(Child.inode->blocks));
            }
        }
        if (Result == EOK)
        {
            if (ext4_sb_feature_incom(Sb, EXT4_FINCOM_FILETYPE))
            {
                ext4_dir_en_set_inode_type(Sb, Found.dentry, E4ModeToEntryType(NewType));
                ext4_dir_set_csum(&Parent, (struct ext4_dir_en *)Found.block.data);
                ext4_trans_set_block_dirty(Found.block.buf);
            }
            ext4_inode_set_mode(Sb, Child.inode, NewType | (Mode & 07777));
            E4Touch(Volume, &Child, (LinkTarget != NULL) ? (E4_SET_MODIFY | E4_SET_CHANGE) : E4_SET_CHANGE);
        }
        ext4_dir_destroy_result(&Parent, &Found);
    }

    if (Result == EOK)
        Result = ext4_fs_put_inode_ref(&Child);
    else
        ext4_fs_put_inode_ref(&Child);
    ext4_fs_put_inode_ref(&Parent);
    return E4End(Volume, Result);
}

int
E4SetType(
    E4_VOLUME *Volume,
    uint32_t Directory,
    const char *Name,
    uint32_t NameLength,
    uint32_t Inode,
    uint32_t Mode)
{
    return E4SetTypeNode(Volume, Directory, Name, NameLength, Inode, Mode, NULL, 0);
}

int
E4SetSymlink(
    E4_VOLUME *Volume,
    uint32_t Directory,
    const char *Name,
    uint32_t NameLength,
    uint32_t Inode,
    uint32_t Mode,
    const char *Target,
    uint32_t TargetLength)
{
    if (Target == NULL)
        return EINVAL;
    return E4SetTypeNode(Volume, Directory, Name, NameLength, Inode, E4_S_IFLNK | (Mode & 07777), Target, TargetLength);
}

int
E4Lookup(
    E4_VOLUME *Volume,
    uint32_t Directory,
    const char *Name,
    uint32_t NameLength,
    uint32_t *Inode,
    uint8_t *Type)
{
    struct ext4_sblock *Sb = &Volume->Fs.sb;
    struct ext4_dir_search_result Found;
    struct ext4_inode_ref Ref;
    int Result;

    if ((NameLength == 0) || (NameLength > E4_NAME_MAX))
        return ENOENT;
    Result = E4Get(Volume, Directory, &Ref);
    if (Result != EOK)
        return Result;
    if (!ext4_inode_is_type(Sb, Ref.inode, EXT4_INODE_MODE_DIRECTORY))
    {
        ext4_fs_put_inode_ref(&Ref);
        return ENOTDIR;
    }

    Result = ext4_dir_find_entry(&Found, &Ref, Name, NameLength);
    if (Result == EOK)
    {
        *Inode = ext4_dir_en_get_inode(Found.dentry);
        if (Type != NULL)
        {
            *Type = EXT4_DE_UNKNOWN;
            if (ext4_sb_feature_incom(Sb, EXT4_FINCOM_FILETYPE))
                *Type = ext4_dir_en_get_inode_type(Sb, Found.dentry);
        }
    }
    ext4_dir_destroy_result(&Ref, &Found);
    ext4_fs_put_inode_ref(&Ref);

    if ((Result == EOK) && (Type != NULL) && (*Type == EXT4_DE_UNKNOWN))
    {
        E4_ATTR Attr;

        Result = E4GetAttr(Volume, *Inode, &Attr);
        if (Result == EOK)
            *Type = E4ModeToEntryType(Attr.Mode);
    }
    return Result;
}

int
E4ReadDir(
    E4_VOLUME *Volume,
    uint32_t Directory,
    uint64_t *Offset,
    E4_DIR_CALLBACK Callback,
    void *Context)
{
    struct ext4_sblock *Sb = &Volume->Fs.sb;
    struct ext4_inode_ref Ref;
    struct ext4_dir_iter Iterator;
    int Result;

    Result = E4Get(Volume, Directory, &Ref);
    if (Result != EOK)
        return Result;
    if (!ext4_inode_is_type(Sb, Ref.inode, EXT4_INODE_MODE_DIRECTORY))
    {
        ext4_fs_put_inode_ref(&Ref);
        return ENOTDIR;
    }

    /* The saved position may have stopped being the start of an entry when a neighbour was removed: the block
     * is entered at its start, and the entries before the position are passed over */
    Result = ext4_dir_iterator_init(&Iterator, &Ref, *Offset - (*Offset % Volume->BlockSize));
    while ((Result == EOK) && (Iterator.curr != NULL))
    {
        struct ext4_dir_en *Entry = Iterator.curr;
        uint64_t Next = Iterator.curr_off + ext4_dir_en_get_entry_len(Entry);

        if ((ext4_dir_en_get_inode(Entry) != 0) && (Iterator.curr_off >= *Offset))
        {
            uint8_t Type = EXT4_DE_UNKNOWN;

            if (ext4_sb_feature_incom(Sb, EXT4_FINCOM_FILETYPE))
                Type = ext4_dir_en_get_inode_type(Sb, Entry);
            if (!Callback(Context, (const char *)Entry->name, ext4_dir_en_get_name_len(Sb, Entry),
                          ext4_dir_en_get_inode(Entry), Type, Next))
            {
                break;
            }
        }
        if (Next > *Offset)
            *Offset = Next;
        Result = ext4_dir_iterator_next(&Iterator);
        if ((Result == EOK) && (Iterator.curr == NULL) && (Iterator.curr_off > *Offset))
            *Offset = Iterator.curr_off;
    }
    ext4_dir_iterator_fini(&Iterator);
    ext4_fs_put_inode_ref(&Ref);
    return Result;
}

static
int
E4HasChildren(
    E4_VOLUME *Volume,
    struct ext4_inode_ref *Ref,
    bool *Children)
{
    struct ext4_sblock *Sb = &Volume->Fs.sb;
    struct ext4_dir_iter Iterator;
    int Result;

    *Children = false;
    Result = ext4_dir_iterator_init(&Iterator, Ref, 0);
    while ((Result == EOK) && (Iterator.curr != NULL))
    {
        struct ext4_dir_en *Entry = Iterator.curr;

        if (ext4_dir_en_get_inode(Entry) != 0)
        {
            uint16_t Length = ext4_dir_en_get_name_len(Sb, Entry);
            bool Dot = (Length == 1) && (Entry->name[0] == '.');
            bool DotDot = (Length == 2) && (Entry->name[0] == '.') && (Entry->name[1] == '.');

            if (!Dot && !DotDot)
            {
                *Children = true;
                break;
            }
        }
        Result = ext4_dir_iterator_next(&Iterator);
    }
    ext4_dir_iterator_fini(&Iterator);
    return Result;
}

int
E4IsEmptyDir(
    E4_VOLUME *Volume,
    uint32_t Directory,
    bool *Empty)
{
    struct ext4_inode_ref Ref;
    bool Children = false;
    int Result;

    Result = E4Get(Volume, Directory, &Ref);
    if (Result != EOK)
        return Result;
    if (!ext4_inode_is_type(&Volume->Fs.sb, Ref.inode, EXT4_INODE_MODE_DIRECTORY))
        Result = ENOTDIR;
    else
        Result = E4HasChildren(Volume, &Ref, &Children);
    ext4_fs_put_inode_ref(&Ref);
    *Empty = !Children;
    return Result;
}

/* Both a linear directory and the root block of an indexed one start with the "." and ".." entries */
static
int
E4DotDot(
    E4_VOLUME *Volume,
    struct ext4_inode_ref *Ref,
    uint32_t *Parent,
    bool Set)
{
    struct ext4_block Block;
    struct ext4_dir_en *Dot, *DotDot;
    ext4_fsblk_t Physical;
    int Result;

    Result = ext4_fs_get_inode_dblk_idx(Ref, 0, &Physical, false);
    if (Result != EOK)
        return Result;
    if (Physical == 0)
        return EIO;
    Result = ext4_trans_block_get(&Volume->Device, &Block, Physical);
    if (Result != EOK)
        return Result;

    Dot = (struct ext4_dir_en *)Block.data;
    if (ext4_dir_en_get_entry_len(Dot) > Volume->BlockSize - 12)
    {
        ext4_block_set(&Volume->Device, &Block);
        return EIO;
    }
    DotDot = (struct ext4_dir_en *)(Block.data + ext4_dir_en_get_entry_len(Dot));
    if (Set)
    {
        ext4_dir_en_set_inode(DotDot, *Parent);
        ext4_dir_set_csum(Ref, (struct ext4_dir_en *)Block.data);
        ext4_trans_set_block_dirty(Block.buf);
    }
    else
    {
        *Parent = ext4_dir_en_get_inode(DotDot);
    }
    return ext4_block_set(&Volume->Device, &Block);
}

int
E4Parent(
    E4_VOLUME *Volume,
    uint32_t Directory,
    uint32_t *Parent)
{
    struct ext4_inode_ref Ref;
    int Result;

    Result = E4Get(Volume, Directory, &Ref);
    if (Result != EOK)
        return Result;
    if (!ext4_inode_is_type(&Volume->Fs.sb, Ref.inode, EXT4_INODE_MODE_DIRECTORY))
        Result = ENOTDIR;
    else
        Result = E4DotDot(Volume, &Ref, Parent, false);
    ext4_fs_put_inode_ref(&Ref);
    return Result;
}

static
int
E4Exists(
    struct ext4_inode_ref *Directory,
    const char *Name,
    uint32_t NameLength)
{
    struct ext4_dir_search_result Found;
    int Result;

    Result = ext4_dir_find_entry(&Found, Directory, Name, NameLength);
    ext4_dir_destroy_result(Directory, &Found);
    if (Result == EOK)
        return EEXIST;
    return (Result == ENOENT) ? EOK : Result;
}

int
E4Create(
    E4_VOLUME *Volume,
    uint32_t Directory,
    const char *Name,
    uint32_t NameLength,
    const E4_ATTR *Attr,
    uint32_t *Inode)
{
    struct ext4_sblock *Sb = &Volume->Fs.sb;
    struct ext4_inode_ref Parent, Child;
    uint8_t Type = E4ModeToEntryType(Attr->Mode);
    bool IsDirectory = (Type == EXT4_DE_DIR);
    E4_TIME Now;
    int Result;

    if ((NameLength == 0) || (NameLength > E4_NAME_MAX))
        return ENAMETOOLONG;
    if (Type == EXT4_DE_UNKNOWN)
        return EINVAL;

    Result = E4Begin(Volume);
    if (Result != EOK)
        return Result;
    Result = E4Get(Volume, Directory, &Parent);
    if (Result != EOK)
        return E4End(Volume, Result);

    if (!ext4_inode_is_type(Sb, Parent.inode, EXT4_INODE_MODE_DIRECTORY))
        Result = ENOTDIR;
    else if (ext4_inode_get_links_cnt(Parent.inode) == 0)
        Result = ENOENT;
    else
        Result = E4Exists(&Parent, Name, NameLength);
    if (Result == EOK)
        Result = ext4_fs_alloc_inode(&Volume->Fs, &Child, Type);
    if (Result != EOK)
    {
        ext4_fs_put_inode_ref(&Parent);
        return E4End(Volume, Result);
    }

    E4Now(Volume, &Now);
    Volume->GenerationSeed = Volume->GenerationSeed * 1103515245 + 12345;
    ext4_inode_set_mode(Sb, Child.inode, Attr->Mode & 0xFFFF);
    ext4_inode_set_uid(Child.inode, Attr->Uid);
    ext4_inode_set_gid(Child.inode, Attr->Gid);
    ext4_inode_set_generation(Child.inode, Volume->GenerationSeed ^ Child.index);
    E4SetTimes(Volume, &Child, E4_SET_ACCESS | E4_SET_MODIFY | E4_SET_CHANGE | E4_SET_CREATE, &Now, &Now, &Now, &Now);
    ext4_fs_inode_blocks_init(&Volume->Fs, &Child);
    if ((Type == EXT4_DE_CHRDEV) || (Type == EXT4_DE_BLKDEV))
        E4SetDevice(Child.inode, Attr->DeviceMajor, Attr->DeviceMinor);

    Result = ext4_dir_add_entry(&Parent, Name, NameLength, &Child);
    if ((Result == EOK) && IsDirectory)
    {
        if (ext4_sb_feature_com(Sb, EXT4_FCOM_DIR_INDEX))
        {
            Result = ext4_dir_dx_init(&Child, &Parent);
            if (Result == EOK)
                ext4_inode_set_flag(Child.inode, EXT4_INODE_FLAG_INDEX);
        }
        else
        {
            Result = ext4_dir_add_entry(&Child, ".", 1, &Child);
            if (Result == EOK)
                Result = ext4_dir_add_entry(&Child, "..", 2, &Parent);
        }
        if (Result == EOK)
        {
            ext4_inode_set_links_cnt(Child.inode, 2);
            ext4_fs_inode_links_count_inc(&Parent);
        }
        else
        {
            ext4_dir_remove_entry(&Parent, Name, NameLength);
        }
    }
    else if (Result == EOK)
    {
        ext4_inode_set_links_cnt(Child.inode, 1);
    }

    if (Result == EOK)
    {
        *Inode = Child.index;
        Child.dirty = true;
        E4SetTimes(Volume, &Parent, E4_SET_MODIFY | E4_SET_CHANGE, &Now, &Now, &Now, &Now);
        Result = ext4_fs_put_inode_ref(&Child);
    }
    else
    {
        if (IsDirectory)
            ext4_fs_truncate_inode(&Child, 0);
        ext4_fs_free_inode(&Child);
        Child.dirty = false;
        ext4_fs_put_inode_ref(&Child);
    }

    if (Result == EOK)
        Result = ext4_fs_put_inode_ref(&Parent);
    else
        ext4_fs_put_inode_ref(&Parent);
    return E4End(Volume, Result);
}

int
E4Link(
    E4_VOLUME *Volume,
    uint32_t Directory,
    const char *Name,
    uint32_t NameLength,
    uint32_t Inode)
{
    struct ext4_sblock *Sb = &Volume->Fs.sb;
    struct ext4_inode_ref Parent, Child;
    int Result;

    if ((NameLength == 0) || (NameLength > E4_NAME_MAX))
        return ENAMETOOLONG;

    Result = E4Begin(Volume);
    if (Result != EOK)
        return Result;
    Result = E4Get(Volume, Directory, &Parent);
    if (Result != EOK)
        return E4End(Volume, Result);
    Result = E4Get(Volume, Inode, &Child);
    if (Result != EOK)
    {
        ext4_fs_put_inode_ref(&Parent);
        return E4End(Volume, Result);
    }

    if (!ext4_inode_is_type(Sb, Parent.inode, EXT4_INODE_MODE_DIRECTORY))
        Result = ENOTDIR;
    else if (ext4_inode_is_type(Sb, Child.inode, EXT4_INODE_MODE_DIRECTORY))
        Result = EPERM;
    else if (ext4_inode_get_links_cnt(Child.inode) >= E4_LINK_MAX)
        Result = EMLINK;
    else if (ext4_inode_get_links_cnt(Child.inode) == 0)
        Result = ENOENT;
    else
        Result = E4Exists(&Parent, Name, NameLength);
    if (Result == EOK)
        Result = ext4_dir_add_entry(&Parent, Name, NameLength, &Child);
    if (Result == EOK)
    {
        ext4_fs_inode_links_count_inc(&Child);
        E4Touch(Volume, &Child, E4_SET_CHANGE);
        E4Touch(Volume, &Parent, E4_SET_MODIFY | E4_SET_CHANGE);
    }

    if (Result == EOK)
        Result = ext4_fs_put_inode_ref(&Child);
    else
        ext4_fs_put_inode_ref(&Child);
    if (Result == EOK)
        Result = ext4_fs_put_inode_ref(&Parent);
    else
        ext4_fs_put_inode_ref(&Parent);
    return E4End(Volume, Result);
}

/* Puts the superblock in the running transaction: for the fields that must change together with other blocks */
static
int
E4JournalSuper(
    E4_VOLUME *Volume)
{
    struct ext4_sblock *Sb = &Volume->Fs.sb;
    struct ext4_block Block;
    int Result;

    ext4_assert(Volume->TransDepth != 0);
    if (!Volume->Journaled)
        return EOK;

    Result = ext4_trans_block_get(&Volume->Device, &Block, EXT4_SUPERBLOCK_OFFSET / Volume->BlockSize);
    if (Result != EOK)
        return Result;
    ext4_sb_set_csum(Sb);
    memcpy(Block.data + EXT4_SUPERBLOCK_OFFSET % Volume->BlockSize, Sb, EXT4_SUPERBLOCK_SIZE);
    ext4_trans_set_block_dirty(Block.buf);
    return ext4_block_set(&Volume->Device, &Block);
}

/* An inode without a name is on the orphan list until its blocks and the inode itself are released: the
 * superblock names the first one, and each one names the next where it will later store its deletion time.
 * Whoever mounts the volume after a crash releases the inodes it finds there. */
static
int
E4OrphanAdd(
    E4_VOLUME *Volume,
    struct ext4_inode_ref *Ref)
{
    struct ext4_sblock *Sb = &Volume->Fs.sb;

    ext4_assert(ext4_inode_get_links_cnt(Ref->inode) == 0);
    ext4_assert(ext4_get32(Sb, last_orphan) != Ref->index);
    ext4_inode_set_del_time(Ref->inode, ext4_get32(Sb, last_orphan));
    ext4_set32(Sb, last_orphan, Ref->index);
    Ref->dirty = true;
    return E4JournalSuper(Volume);
}

static
int
E4OrphanRemove(
    E4_VOLUME *Volume,
    struct ext4_inode_ref *Ref)
{
    struct ext4_sblock *Sb = &Volume->Fs.sb;
    uint32_t Count = ext4_get32(Sb, inodes_count);
    uint32_t Next = ext4_inode_get_del_time(Ref->inode);
    uint32_t Cursor = ext4_get32(Sb, last_orphan);
    uint32_t Guard;

    if (Cursor == Ref->index)
    {
        ext4_set32(Sb, last_orphan, Next);
        return E4JournalSuper(Volume);
    }

    /* An inode that is not on the list lost its last name before the list was kept */
    for (Guard = 0; (Cursor != 0) && (Cursor <= Count) && (Guard < Count); Guard++)
    {
        struct ext4_inode_ref Previous;
        uint32_t Following;
        int Result = ext4_fs_get_inode_ref(&Volume->Fs, Cursor, &Previous);

        if (Result != EOK)
            return Result;
        Following = ext4_inode_get_del_time(Previous.inode);
        if (Following == Ref->index)
        {
            ext4_inode_set_del_time(Previous.inode, Next);
            Previous.dirty = true;
            return ext4_fs_put_inode_ref(&Previous);
        }
        Result = ext4_fs_put_inode_ref(&Previous);
        if (Result != EOK)
            return Result;
        Cursor = Following;
    }
    return EOK;
}

/* True when every inode on the orphan list is one this driver can release: an inode without a name */
static
bool
E4OrphansAreReleasable(
    E4_VOLUME *Volume)
{
    struct ext4_sblock *Sb = &Volume->Fs.sb;
    uint32_t Count = ext4_get32(Sb, inodes_count);
    uint32_t Cursor = ext4_get32(Sb, last_orphan);
    uint32_t Guard;

    for (Guard = 0; Cursor != 0; Guard++)
    {
        struct ext4_inode_ref Ref;
        uint16_t Links;

        if ((Cursor > Count) || (Cursor < EXT4_GOOD_OLD_FIRST_INO) || (Guard == Count))
            return false;
        if (ext4_fs_get_inode_ref(&Volume->Fs, Cursor, &Ref) != EOK)
            return false;
        Links = ext4_inode_get_links_cnt(Ref.inode);
        Cursor = ext4_inode_get_del_time(Ref.inode);
        ext4_fs_put_inode_ref(&Ref);
        /* TODO: an inode with names is a truncation that a crash interrupted; the volume is mounted read-only */
        if (Links != 0)
            return false;
    }
    return true;
}

static
int
E4OrphanCleanup(
    E4_VOLUME *Volume)
{
    struct ext4_sblock *Sb = &Volume->Fs.sb;
    uint32_t Count = ext4_get32(Sb, inodes_count);
    uint32_t Guard;

    for (Guard = 0; (ext4_get32(Sb, last_orphan) != 0) && (Guard <= Count); Guard++)
    {
        uint32_t Inode = ext4_get32(Sb, last_orphan);
        int Result = E4Release(Volume, Inode);

        if (Result != EOK)
            return Result;
        if (ext4_get32(Sb, last_orphan) == Inode)
            return EIO;
    }
    return EOK;
}

/* Takes the name out of the directory and drops the link it stood for */
static
int
E4DropEntry(
    E4_VOLUME *Volume,
    struct ext4_inode_ref *Parent,
    struct ext4_inode_ref *Child,
    const char *Name,
    uint32_t NameLength,
    bool *LastLink)
{
    struct ext4_sblock *Sb = &Volume->Fs.sb;
    bool IsDirectory = ext4_inode_is_type(Sb, Child->inode, EXT4_INODE_MODE_DIRECTORY);
    int Result;

    if (IsDirectory)
    {
        bool Children;

        Result = E4HasChildren(Volume, Child, &Children);
        if (Result != EOK)
            return Result;
        if (Children)
            return ENOTEMPTY;
    }

    Result = ext4_dir_remove_entry(Parent, Name, NameLength);
    if (Result != EOK)
        return Result;

    if (IsDirectory)
    {
        ext4_fs_inode_links_count_dec(Parent);
        ext4_inode_set_links_cnt(Child->inode, 0);
    }
    else
    {
        ext4_fs_inode_links_count_dec(Child);
    }
    E4Touch(Volume, Child, E4_SET_CHANGE);
    E4Touch(Volume, Parent, E4_SET_MODIFY | E4_SET_CHANGE);
    *LastLink = (ext4_inode_get_links_cnt(Child->inode) == 0);
    return *LastLink ? E4OrphanAdd(Volume, Child) : EOK;
}

int
E4Unlink(
    E4_VOLUME *Volume,
    uint32_t Directory,
    const char *Name,
    uint32_t NameLength,
    uint32_t Inode,
    bool *LastLink)
{
    struct ext4_inode_ref Parent, Child;
    struct ext4_dir_search_result Found;
    int Result;

    *LastLink = false;
    Result = E4Begin(Volume);
    if (Result != EOK)
        return Result;
    Result = E4Get(Volume, Directory, &Parent);
    if (Result != EOK)
        return E4End(Volume, Result);
    Result = E4Get(Volume, Inode, &Child);
    if (Result != EOK)
    {
        ext4_fs_put_inode_ref(&Parent);
        return E4End(Volume, Result);
    }

    Result = ext4_dir_find_entry(&Found, &Parent, Name, NameLength);
    if ((Result == EOK) && (ext4_dir_en_get_inode(Found.dentry) != Inode))
        Result = ENOENT;
    ext4_dir_destroy_result(&Parent, &Found);
    if (Result == EOK)
        Result = E4DropEntry(Volume, &Parent, &Child, Name, NameLength, LastLink);

    /* TODO: an inode whose last link is gone is not put on the orphan list; a crash before E4Release leaks it */
    if (Result == EOK)
        Result = ext4_fs_put_inode_ref(&Child);
    else
        ext4_fs_put_inode_ref(&Child);
    if (Result == EOK)
        Result = ext4_fs_put_inode_ref(&Parent);
    else
        ext4_fs_put_inode_ref(&Parent);
    return E4End(Volume, Result);
}

int
E4Rename(
    E4_VOLUME *Volume,
    uint32_t OldDirectory,
    const char *OldName,
    uint32_t OldLength,
    uint32_t NewDirectory,
    const char *NewName,
    uint32_t NewLength,
    uint32_t Inode,
    uint32_t Replaced,
    bool *ReplacedLastLink)
{
    struct ext4_sblock *Sb = &Volume->Fs.sb;
    struct ext4_inode_ref OldParent, NewParentStorage, Child, Victim;
    struct ext4_inode_ref *NewParent = &OldParent;
    struct ext4_dir_search_result Found;
    bool SameDirectory = (OldDirectory == NewDirectory);
    bool HaveNewParent = false, HaveChild = false, HaveVictim = false;
    bool IsDirectory;
    int Result;

    *ReplacedLastLink = false;
    if ((NewLength == 0) || (NewLength > E4_NAME_MAX))
        return ENAMETOOLONG;
    if (Replaced == Inode)
        return EINVAL;

    Result = E4Begin(Volume);
    if (Result != EOK)
        return Result;
    Result = E4Get(Volume, OldDirectory, &OldParent);
    if (Result != EOK)
        return E4End(Volume, Result);

    if (!SameDirectory)
    {
        Result = E4Get(Volume, NewDirectory, &NewParentStorage);
        if (Result == EOK)
        {
            NewParent = &NewParentStorage;
            HaveNewParent = true;
        }
    }
    if (Result == EOK)
    {
        Result = E4Get(Volume, Inode, &Child);
        HaveChild = (Result == EOK);
    }
    if ((Result == EOK) && (Replaced != 0))
    {
        Result = E4Get(Volume, Replaced, &Victim);
        HaveVictim = (Result == EOK);
    }

    if (Result == EOK)
    {
        if (!ext4_inode_is_type(Sb, NewParent->inode, EXT4_INODE_MODE_DIRECTORY))
            Result = ENOTDIR;
        else if (ext4_inode_get_links_cnt(NewParent->inode) == 0)
            Result = ENOENT;
    }
    if (Result == EOK)
    {
        Result = ext4_dir_find_entry(&Found, &OldParent, OldName, OldLength);
        if ((Result == EOK) && (ext4_dir_en_get_inode(Found.dentry) != Inode))
            Result = ENOENT;
        ext4_dir_destroy_result(&OldParent, &Found);
    }

    IsDirectory = false;
    if (HaveChild)
        IsDirectory = ext4_inode_is_type(Sb, Child.inode, EXT4_INODE_MODE_DIRECTORY);
    if (Result == EOK)
    {
        if (HaveVictim)
        {
            bool VictimIsDirectory = ext4_inode_is_type(Sb, Victim.inode, EXT4_INODE_MODE_DIRECTORY);

            if (IsDirectory && !VictimIsDirectory)
                Result = ENOTDIR;
            else if (!IsDirectory && VictimIsDirectory)
                Result = EISDIR;
            else
                Result = E4DropEntry(Volume, NewParent, &Victim, NewName, NewLength, ReplacedLastLink);
        }
        else
        {
            Result = E4Exists(NewParent, NewName, NewLength);
        }
    }

    if (Result == EOK)
        Result = ext4_dir_add_entry(NewParent, NewName, NewLength, &Child);
    if (Result == EOK)
        Result = ext4_dir_remove_entry(&OldParent, OldName, OldLength);
    if ((Result == EOK) && IsDirectory && !SameDirectory)
    {
        uint32_t Parent = NewParent->index;

        if (ext4_sb_feature_com(Sb, EXT4_FCOM_DIR_INDEX) && ext4_inode_has_flag(Child.inode, EXT4_INODE_FLAG_INDEX))
            Result = ext4_dir_dx_reset_parent_inode(&Child, Parent);
        else
            Result = E4DotDot(Volume, &Child, &Parent, true);
        if (Result == EOK)
        {
            ext4_fs_inode_links_count_dec(&OldParent);
            ext4_fs_inode_links_count_inc(NewParent);
        }
    }
    if (Result == EOK)
    {
        E4Touch(Volume, &Child, E4_SET_CHANGE);
        E4Touch(Volume, &OldParent, E4_SET_MODIFY | E4_SET_CHANGE);
        if (!SameDirectory)
            E4Touch(Volume, NewParent, E4_SET_MODIFY | E4_SET_CHANGE);
    }

    if (HaveVictim)
    {
        int Put = ext4_fs_put_inode_ref(&Victim);

        if (Result == EOK)
            Result = Put;
    }
    if (HaveChild)
    {
        int Put = ext4_fs_put_inode_ref(&Child);

        if (Result == EOK)
            Result = Put;
    }
    if (HaveNewParent)
    {
        int Put = ext4_fs_put_inode_ref(&NewParentStorage);

        if (Result == EOK)
            Result = Put;
    }
    {
        int Put = ext4_fs_put_inode_ref(&OldParent);

        if (Result == EOK)
            Result = Put;
    }
    return E4End(Volume, Result);
}

static
bool
E4IsFastLink(
    E4_VOLUME *Volume,
    struct ext4_inode *Inode)
{
    struct ext4_sblock *Sb = &Volume->Fs.sb;
    uint64_t Sectors = ext4_inode_get_blocks_count(Sb, Inode);
    uint64_t AttributeSectors = ext4_inode_get_file_acl(Inode, Sb) ? (Volume->BlockSize >> 9) : 0;

    return ext4_inode_is_type(Sb, Inode, EXT4_INODE_MODE_SOFTLINK) && (Sectors == AttributeSectors);
}

/* Cuts the file down one bounded step at a time, each step in its own transaction, so no step outgrows the journal */
static
int
E4Shrink(
    E4_VOLUME *Volume,
    uint32_t Inode,
    uint64_t Size)
{
    struct ext4_sblock *Sb = &Volume->Fs.sb;
    int Result;

    for (;;)
    {
        struct ext4_inode_ref Ref;
        uint64_t Current, Target;
        uint32_t Type;

        Result = E4Begin(Volume);
        if (Result != EOK)
            return Result;
        Result = E4Get(Volume, Inode, &Ref);
        if (Result != EOK)
            return E4End(Volume, Result);

        Current = ext4_inode_get_size(Sb, Ref.inode);
        Type = ext4_inode_type(Sb, Ref.inode);
        if ((Current <= Size) ||
            ((Type != EXT4_INODE_MODE_FILE) && (Type != EXT4_INODE_MODE_DIRECTORY) && (Type != EXT4_INODE_MODE_SOFTLINK)))
        {
            ext4_fs_put_inode_ref(&Ref);
            return E4End(Volume, EOK);
        }

        Target = Size;
        if ((Current - Size > E4_TRUNCATE_STEP) && !E4IsFastLink(Volume, Ref.inode))
            Target = (Current - E4_TRUNCATE_STEP) & ~((uint64_t)Volume->BlockSize - 1);
        if (Target < Size)
            Target = Size;

        Result = ext4_fs_truncate_inode(&Ref, Target);
        if (Result == EOK)
        {
            Ref.dirty = true;
            Result = ext4_fs_put_inode_ref(&Ref);
        }
        else
        {
            ext4_fs_put_inode_ref(&Ref);
        }
        Result = E4End(Volume, Result);
        if ((Result != EOK) || (Target == Size))
            return Result;
    }
}

int
E4SetSize(
    E4_VOLUME *Volume,
    uint32_t Inode,
    uint64_t Size)
{
    struct ext4_sblock *Sb = &Volume->Fs.sb;
    struct ext4_inode_ref Ref;
    uint64_t Current;
    int Result;

    Result = E4Get(Volume, Inode, &Ref);
    if (Result != EOK)
        return Result;
    Current = ext4_inode_get_size(Sb, Ref.inode);
    if (!ext4_inode_is_type(Sb, Ref.inode, EXT4_INODE_MODE_FILE))
        Result = EINVAL;
    ext4_fs_put_inode_ref(&Ref);
    if (Result != EOK)
        return Result;

    if (Size < Current)
    {
        uint32_t Tail = (uint32_t)(Size % Volume->BlockSize);

        Result = E4Shrink(Volume, Inode, Size);
        if (Result != EOK)
            return Result;

        /* What the last block holds past the new end must read as zeros if the file grows again */
        if (Tail != 0)
        {
            uint64_t Physical;
            uint32_t Mapped;

            Result = E4Map(Volume, Inode, Size / Volume->BlockSize, 1, false, &Physical, &Mapped, NULL);
            if ((Result == EOK) && (Physical != 0))
            {
                void *Zero = ext4_calloc(1, Volume->BlockSize - Tail);

                if (Zero == NULL)
                    return ENOMEM;
                Result = ext4_block_writebytes(&Volume->Device, Physical * Volume->BlockSize + Tail, Zero,
                                               Volume->BlockSize - Tail);
                ext4_free(Zero);
            }
            if (Result != EOK)
                return Result;
        }
    }

    Result = E4Begin(Volume);
    if (Result != EOK)
        return Result;
    Result = E4Get(Volume, Inode, &Ref);
    if (Result != EOK)
        return E4End(Volume, Result);
    if (ext4_inode_get_size(Sb, Ref.inode) != Size)
        ext4_inode_set_size(Ref.inode, Size);
    E4Touch(Volume, &Ref, E4_SET_MODIFY | E4_SET_CHANGE);
    Result = ext4_fs_put_inode_ref(&Ref);
    return E4End(Volume, Result);
}

int
E4GrowSize(
    E4_VOLUME *Volume,
    uint32_t Inode,
    uint64_t Size)
{
    struct ext4_sblock *Sb = &Volume->Fs.sb;
    struct ext4_inode_ref Ref;
    int Result;

    Result = E4Begin(Volume);
    if (Result != EOK)
        return Result;
    Result = E4Get(Volume, Inode, &Ref);
    if (Result != EOK)
        return E4End(Volume, Result);
    if (!ext4_inode_is_type(Sb, Ref.inode, EXT4_INODE_MODE_FILE))
    {
        ext4_fs_put_inode_ref(&Ref);
        return E4End(Volume, EINVAL);
    }
    if (ext4_inode_get_size(Sb, Ref.inode) < Size)
    {
        ext4_inode_set_size(Ref.inode, Size);
        Ref.dirty = true;
    }
    Result = ext4_fs_put_inode_ref(&Ref);
    return E4End(Volume, Result);
}

int
E4Release(
    E4_VOLUME *Volume,
    uint32_t Inode)
{
    struct ext4_inode_ref Ref;
    E4_TIME Now;
    int Result;

    Result = E4Get(Volume, Inode, &Ref);
    if (Result != EOK)
        return Result;
    if (ext4_inode_get_links_cnt(Ref.inode) != 0)
    {
        ext4_fs_put_inode_ref(&Ref);
        return EOK;
    }
    ext4_fs_put_inode_ref(&Ref);

    Result = E4Shrink(Volume, Inode, 0);
    if (Result != EOK)
        return Result;

    Result = E4Begin(Volume);
    if (Result != EOK)
        return Result;
    Result = E4Get(Volume, Inode, &Ref);
    if (Result != EOK)
        return E4End(Volume, Result);

    Result = E4OrphanRemove(Volume, &Ref);
    if (Result != EOK)
    {
        ext4_fs_put_inode_ref(&Ref);
        return E4End(Volume, Result);
    }
    E4Now(Volume, &Now);
    ext4_inode_set_del_time(Ref.inode, (uint32_t)Now.Seconds);
    Result = ext4_fs_free_inode(&Ref);
    Ref.dirty = true;
    if (Result == EOK)
        Result = ext4_fs_put_inode_ref(&Ref);
    else
        ext4_fs_put_inode_ref(&Ref);
    return E4End(Volume, Result);
}

int
E4Map(
    E4_VOLUME *Volume,
    uint32_t Inode,
    uint64_t Block,
    uint32_t Count,
    bool Allocate,
    uint64_t *Physical,
    uint32_t *Mapped,
    bool *Fresh)
{
    struct ext4_sblock *Sb = &Volume->Fs.sb;
    struct ext4_inode_ref Ref;
    ext4_fsblk_t Start = 0;
    uint32_t Run = 0;
    bool Extents;
    int Result;

    *Physical = 0;
    *Mapped = 0;
    if (Fresh != NULL)
        *Fresh = false;
    if ((Count == 0) || (Block > 0xFFFFFFFEULL))
        return EINVAL;
    if (Count > 0xFFFFFFFFULL - Block)
        Count = (uint32_t)(0xFFFFFFFFULL - Block);

    if (Allocate)
    {
        Result = E4Begin(Volume);
        if (Result != EOK)
            return Result;
    }
    Result = E4Get(Volume, Inode, &Ref);
    if (Result != EOK)
        return Allocate ? E4End(Volume, Result) : Result;

    Extents = ext4_sb_feature_incom(Sb, EXT4_FINCOM_EXTENTS) && ext4_inode_has_flag(Ref.inode, EXT4_INODE_FLAG_EXTENTS);
    if (Extents)
    {
        Result = ext4_extent_get_blocks(&Ref, (ext4_lblk_t)Block, Count, &Start, false, &Run);
        if ((Result == EOK) && (Start == 0) && Allocate)
        {
            Result = ext4_extent_get_blocks(&Ref, (ext4_lblk_t)Block, Count, &Start, true, &Run);
            if (Result == EOK)
                Volume->StageSettle = true;
            if ((Result == EOK) && (Fresh != NULL))
                *Fresh = true;
        }
    }
    else
    {
        Result = ext4_fs_get_inode_dblk_idx(&Ref, (ext4_lblk_t)Block, &Start, false);
        if ((Result == EOK) && (Start == 0) && Allocate)
        {
            ext4_fsblk_t Goal;

            Result = ext4_fs_indirect_find_goal(&Ref, &Goal);
            if (Result == EOK)
                Result = ext4_balloc_alloc_block(&Ref, Goal, &Start);
            if (Result == EOK)
            {
                Result = ext4_fs_set_inode_data_block_index(&Ref, (ext4_lblk_t)Block, Start);
                if (Result != EOK)
                    ext4_balloc_free_block(&Ref, Start);
            }
            if (Result != EOK)
                Start = 0;
            else
                Volume->StageSettle = true;
            if ((Result == EOK) && (Fresh != NULL))
                *Fresh = true;
        }
        Run = 1;
        while ((Result == EOK) && (Start != 0) && !Allocate && (Run < Count))
        {
            ext4_fsblk_t Next;

            Result = ext4_fs_get_inode_dblk_idx(&Ref, (ext4_lblk_t)(Block + Run), &Next, false);
            if ((Result != EOK) || (Next != Start + Run))
                break;
            Run++;
        }
    }

    if (Result == EOK)
    {
        *Physical = Start;
        *Mapped = (Start == 0) ? 1 : ((Run == 0) ? 1 : Run);
        if (*Mapped > Count)
            *Mapped = Count;
    }

    if (Result == EOK)
        Result = ext4_fs_put_inode_ref(&Ref);
    else
        ext4_fs_put_inode_ref(&Ref);
    return Allocate ? E4End(Volume, Result) : Result;
}

int
E4ReadLink(
    E4_VOLUME *Volume,
    uint32_t Inode,
    char *Buffer,
    uint32_t Size,
    uint32_t *Length)
{
    struct ext4_sblock *Sb = &Volume->Fs.sb;
    struct ext4_inode_ref Ref;
    uint64_t LinkSize;
    int Result;

    *Length = 0;
    Result = E4Get(Volume, Inode, &Ref);
    if (Result != EOK)
        return Result;

    LinkSize = ext4_inode_get_size(Sb, Ref.inode);
    if (!ext4_inode_is_type(Sb, Ref.inode, EXT4_INODE_MODE_SOFTLINK))
    {
        Result = EINVAL;
    }
    else if (LinkSize > Size)
    {
        Result = ERANGE;
    }
    else if (E4IsFastLink(Volume, Ref.inode))
    {
        if (LinkSize > E4_FAST_LINK_MAX)
            Result = EIO;
        else
            memcpy(Buffer, Ref.inode->blocks, (size_t)LinkSize);
    }
    else
    {
        uint64_t Done = 0;

        while ((Result == EOK) && (Done < LinkSize))
        {
            struct ext4_block Block;
            ext4_fsblk_t Physical;
            uint32_t Chunk = Volume->BlockSize;

            if (Chunk > LinkSize - Done)
                Chunk = (uint32_t)(LinkSize - Done);
            Result = ext4_fs_get_inode_dblk_idx(&Ref, (ext4_lblk_t)(Done / Volume->BlockSize), &Physical, false);
            if (Result != EOK)
                break;
            if (Physical == 0)
            {
                Result = EIO;
                break;
            }
            Result = ext4_trans_block_get(&Volume->Device, &Block, Physical);
            if (Result != EOK)
                break;
            memcpy(Buffer + Done, Block.data, Chunk);
            Result = ext4_block_set(&Volume->Device, &Block);
            Done += Chunk;
        }
    }
    if (Result == EOK)
        *Length = (uint32_t)LinkSize;
    ext4_fs_put_inode_ref(&Ref);
    return Result;
}

int
E4WriteLink(
    E4_VOLUME *Volume,
    uint32_t Inode,
    const char *Target,
    uint32_t Length)
{
    struct ext4_sblock *Sb = &Volume->Fs.sb;
    struct ext4_inode_ref Ref;
    int Result;

    if ((Length == 0) || (Length > E4_SYMLINK_MAX) || (Length > Volume->BlockSize))
        return ENAMETOOLONG;

    Result = E4Begin(Volume);
    if (Result != EOK)
        return Result;
    Result = E4Get(Volume, Inode, &Ref);
    if (Result != EOK)
        return E4End(Volume, Result);

    if (!ext4_inode_is_type(Sb, Ref.inode, EXT4_INODE_MODE_SOFTLINK))
        Result = EINVAL;
    else if (ext4_inode_get_size(Sb, Ref.inode) != 0)
        Result = ext4_fs_truncate_inode(&Ref, 0);

    if (Result == EOK)
        Result = E4StoreLinkTarget(Volume, &Ref, Target, Length);
    if (Result == EOK)
    {
        E4Touch(Volume, &Ref, E4_SET_MODIFY | E4_SET_CHANGE);
        Result = ext4_fs_put_inode_ref(&Ref);
    }
    else
    {
        ext4_fs_put_inode_ref(&Ref);
    }
    return E4End(Volume, Result);
}

int
E4XattrGet(
    E4_VOLUME *Volume,
    uint32_t Inode,
    const char *Name,
    uint32_t NameLength,
    void *Buffer,
    uint32_t Size,
    uint32_t *Length)
{
    struct ext4_inode_ref Ref;
    const char *Short;
    size_t ShortLength, DataLength = 0;
    uint8_t Index;
    bool Found;
    int Result;

    *Length = 0;
    Short = ext4_extract_xattr_name(Name, NameLength, &Index, &ShortLength, &Found);
    if (!Found)
        return ENOTSUP;
    Result = E4Get(Volume, Inode, &Ref);
    if (Result != EOK)
        return Result;
    Result = ext4_xattr_get(&Ref, Index, Short, ShortLength, Buffer, Size, &DataLength);
    if (Result == EOK)
        *Length = (uint32_t)DataLength;
    Ref.dirty = false;
    ext4_fs_put_inode_ref(&Ref);
    return Result;
}

int
E4XattrSet(
    E4_VOLUME *Volume,
    uint32_t Inode,
    const char *Name,
    uint32_t NameLength,
    const void *Value,
    uint32_t Length)
{
    struct ext4_inode_ref Ref;
    const char *Short;
    size_t ShortLength;
    uint8_t Index;
    bool Found;
    int Result;

    Short = ext4_extract_xattr_name(Name, NameLength, &Index, &ShortLength, &Found);
    if (!Found)
        return ENOTSUP;
    Result = E4Begin(Volume);
    if (Result != EOK)
        return Result;
    Result = E4Get(Volume, Inode, &Ref);
    if (Result != EOK)
        return E4End(Volume, Result);
    Result = ext4_xattr_set(&Ref, Index, Short, ShortLength, Value, Length);
    if (Result == EOK)
    {
        E4Touch(Volume, &Ref, E4_SET_CHANGE);
        Result = ext4_fs_put_inode_ref(&Ref);
    }
    else
    {
        ext4_fs_put_inode_ref(&Ref);
    }
    return E4End(Volume, Result);
}

int
E4XattrRemove(
    E4_VOLUME *Volume,
    uint32_t Inode,
    const char *Name,
    uint32_t NameLength)
{
    struct ext4_inode_ref Ref;
    const char *Short;
    size_t ShortLength;
    uint8_t Index;
    bool Found;
    int Result;

    Short = ext4_extract_xattr_name(Name, NameLength, &Index, &ShortLength, &Found);
    if (!Found)
        return ENOTSUP;
    Result = E4Begin(Volume);
    if (Result != EOK)
        return Result;
    Result = E4Get(Volume, Inode, &Ref);
    if (Result != EOK)
        return E4End(Volume, Result);
    Result = ext4_xattr_remove(&Ref, Index, Short, ShortLength);
    if (Result == EOK)
    {
        E4Touch(Volume, &Ref, E4_SET_CHANGE);
        Result = ext4_fs_put_inode_ref(&Ref);
    }
    else
    {
        ext4_fs_put_inode_ref(&Ref);
    }
    return E4End(Volume, Result);
}

int
E4XattrList(
    E4_VOLUME *Volume,
    uint32_t Inode,
    char *Buffer,
    uint32_t Size,
    uint32_t *Length)
{
    struct ext4_xattr_list_entry *List = NULL, *Entry;
    struct ext4_inode_ref Ref;
    size_t ListLength = 0;
    uint32_t Used = 0;
    int Result;

    *Length = 0;
    Result = E4Get(Volume, Inode, &Ref);
    if (Result != EOK)
        return Result;

    Result = ext4_xattr_list(&Ref, NULL, &ListLength);
    if ((Result == EOK) && (ListLength != 0))
    {
        List = ext4_malloc(ListLength);
        if (List == NULL)
            Result = ENOMEM;
        else
            Result = ext4_xattr_list(&Ref, List, &ListLength);
    }
    for (Entry = List; (Result == EOK) && (ListLength != 0) && (Entry != NULL); Entry = Entry->next)
    {
        size_t PrefixLength;
        const char *Prefix = ext4_get_xattr_name_prefix(Entry->name_index, &PrefixLength);
        uint32_t Needed;

        if (Prefix == NULL)
            continue;
        Needed = (uint32_t)(PrefixLength + Entry->name_len + 1);
        if ((Buffer != NULL) && (Used + Needed <= Size))
        {
            memcpy(Buffer + Used, Prefix, PrefixLength);
            memcpy(Buffer + Used + PrefixLength, Entry->name, Entry->name_len);
            Buffer[Used + Needed - 1] = 0;
        }
        else if (Buffer != NULL)
        {
            Result = ERANGE;
        }
        Used += Needed;
    }
    if (List != NULL)
        ext4_free(List);
    Ref.dirty = false;
    ext4_fs_put_inode_ref(&Ref);
    *Length = Used;
    return Result;
}
