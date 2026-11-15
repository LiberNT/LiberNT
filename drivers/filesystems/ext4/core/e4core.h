/*
 * PROJECT:     LiberNT ext4 file system driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Volume operations on top of lwext4, independent of the host system
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef E4CORE_H
#define E4CORE_H

#include <stdlib.h>
#include <string.h>
#include <ext4_config.h>
#include <ext4_types.h>
#include <ext4_misc.h>
#include <ext4_errno.h>
#include <ext4_debug.h>
#include <ext4_blockdev.h>
#include <ext4_bcache.h>
#include <ext4_fs.h>
#include <ext4_super.h>
#include <ext4_inode.h>
#include <ext4_dir.h>
#include <ext4_dir_idx.h>
#include <ext4_balloc.h>
#include <ext4_ialloc.h>
#include <ext4_bitmap.h>
#include <ext4_block_group.h>
#include <ext4_extent.h>
#include <ext4_xattr.h>
#include <ext4_journal.h>
#include <ext4_trans.h>
#include <ext4_crc32.h>

#ifndef ENAMETOOLONG
#define ENAMETOOLONG 36
#endif

#define E4_ROOT_INODE       EXT4_INODE_ROOT_INDEX
#define E4_NAME_MAX         EXT4_DIRECTORY_FILENAME_LEN
#define E4_LINK_MAX         65000
#define E4_SYMLINK_MAX      4095

#define E4_S_IFMT           0xF000
#define E4_S_IFIFO          0x1000
#define E4_S_IFCHR          0x2000
#define E4_S_IFDIR          0x4000
#define E4_S_IFBLK          0x6000
#define E4_S_IFREG          0x8000
#define E4_S_IFLNK          0xA000
#define E4_S_IFSOCK         0xC000

#define E4_SET_MODE         0x0001
#define E4_SET_UID          0x0002
#define E4_SET_GID          0x0004
#define E4_SET_ACCESS       0x0008
#define E4_SET_MODIFY       0x0010
#define E4_SET_CHANGE       0x0020
#define E4_SET_CREATE       0x0040
#define E4_SET_DEVICE       0x0080
#define E4_SET_TYPE         0x0100

typedef struct _E4_TIME
{
    int64_t Seconds;
    uint32_t Nanoseconds;
} E4_TIME;

typedef struct _E4_ATTR
{
    uint32_t Mode;
    uint32_t Uid;
    uint32_t Gid;
    uint32_t Links;
    uint32_t Flags;
    uint32_t Generation;
    uint32_t DeviceMajor;
    uint32_t DeviceMinor;
    uint64_t Size;
    uint64_t Sectors;
    E4_TIME Access;
    E4_TIME Modify;
    E4_TIME Change;
    E4_TIME Create;
} E4_ATTR;

typedef struct _E4_STATFS
{
    uint32_t BlockSize;
    uint64_t Blocks;
    uint64_t FreeBlocks;
    uint64_t AvailableBlocks;
    uint32_t Inodes;
    uint32_t FreeInodes;
    uint8_t Uuid[16];
    char Label[17];
} E4_STATFS;

#define E4_STAGE_RUNS   16

typedef struct _E4_VOLUME
{
    /* The caller fills Device.bdif, Device.part_offset and Device.part_size, and Now, before E4Mount */
    struct ext4_blockdev Device;
    struct ext4_fs Fs;
    struct ext4_bcache Cache;
    struct jbd_fs JournalFs;
    struct jbd_journal Journal;
    void (*Now)(struct _E4_VOLUME *Volume, E4_TIME *Time);
    void *Context;
    uint32_t BlockSize;
    uint32_t TransDepth;
    uint32_t GenerationSeed;
    bool Mounted;
    bool Journaled;
    bool ReadOnly;
    bool WasValid;
    bool Failed;

    /* Journal blocks waiting to be written together: see e4stage.c */
    struct ext4_blockdev_iface *StageRaw;
    struct ext4_blockdev_iface StageInterface;
    struct
    {
        uint64_t Start;
        uint64_t Count;
    } StageRuns[E4_STAGE_RUNS];
    uint32_t StageRunCount;
    uint8_t *StageOut;
    uint8_t *StageData;
    uint64_t *StageBlock;
    uint16_t *StageOrder;
    uint8_t *StageCommit;
    uint32_t StageCapacity;
    uint32_t StageCount;
    uint64_t StageFreeEvents;
    bool StageSettle;
} E4_VOLUME;

/* Return false to stop the enumeration; Offset is the position of the entry after this one */
typedef bool (*E4_DIR_CALLBACK)(void *Context, const char *Name, uint32_t NameLength, uint32_t Inode, uint8_t Type,
                                uint64_t Offset);

int E4Mount(E4_VOLUME *Volume, bool ReadOnly, uint32_t CacheBlocks);
int E4Unmount(E4_VOLUME *Volume);
int E4Sync(E4_VOLUME *Volume);
int E4Begin(E4_VOLUME *Volume);
int E4End(E4_VOLUME *Volume, int Result);
int E4StatFs(E4_VOLUME *Volume, E4_STATFS *Stat);
int E4SetLabel(E4_VOLUME *Volume, const char *Label, uint32_t Length);

int E4GetAttr(E4_VOLUME *Volume, uint32_t Inode, E4_ATTR *Attr);
int E4SetAttr(E4_VOLUME *Volume, uint32_t Inode, uint32_t Mask, const E4_ATTR *Attr);

/* Turns an empty regular file into a symbolic link, a device node, a fifo or a socket, in the inode and in the name that leads to it */
int E4SetType(E4_VOLUME *Volume, uint32_t Directory, const char *Name, uint32_t NameLength, uint32_t Inode, uint32_t Mode);

int E4Lookup(E4_VOLUME *Volume, uint32_t Directory, const char *Name, uint32_t NameLength, uint32_t *Inode, uint8_t *Type);
int E4ReadDir(E4_VOLUME *Volume, uint32_t Directory, uint64_t *Offset, E4_DIR_CALLBACK Callback, void *Context);
int E4IsEmptyDir(E4_VOLUME *Volume, uint32_t Directory, bool *Empty);
int E4Parent(E4_VOLUME *Volume, uint32_t Directory, uint32_t *Parent);

int E4Create(E4_VOLUME *Volume, uint32_t Directory, const char *Name, uint32_t NameLength, const E4_ATTR *Attr,
             uint32_t *Inode);
int E4Link(E4_VOLUME *Volume, uint32_t Directory, const char *Name, uint32_t NameLength, uint32_t Inode);
int E4Unlink(E4_VOLUME *Volume, uint32_t Directory, const char *Name, uint32_t NameLength, uint32_t Inode,
             bool *LastLink);
int E4Rename(E4_VOLUME *Volume, uint32_t OldDirectory, const char *OldName, uint32_t OldLength, uint32_t NewDirectory,
             const char *NewName, uint32_t NewLength, uint32_t Inode, uint32_t Replaced, bool *ReplacedLastLink);
int E4Release(E4_VOLUME *Volume, uint32_t Inode);

int E4Map(E4_VOLUME *Volume, uint32_t Inode, uint64_t Block, uint32_t Count, bool Allocate, uint64_t *Physical,
          uint32_t *Mapped, bool *Fresh);
int E4SetSize(E4_VOLUME *Volume, uint32_t Inode, uint64_t Size);

/* Raises the size of a file to Size if it is smaller: for the transaction that gives the file the blocks up to there */
int E4GrowSize(E4_VOLUME *Volume, uint32_t Inode, uint64_t Size);
int E4ReadLink(E4_VOLUME *Volume, uint32_t Inode, char *Buffer, uint32_t Size, uint32_t *Length);
int E4WriteLink(E4_VOLUME *Volume, uint32_t Inode, const char *Target, uint32_t Length);

int E4XattrGet(E4_VOLUME *Volume, uint32_t Inode, const char *Name, uint32_t NameLength, void *Buffer, uint32_t Size,
               uint32_t *Length);
int E4XattrSet(E4_VOLUME *Volume, uint32_t Inode, const char *Name, uint32_t NameLength, const void *Value,
               uint32_t Length);
int E4XattrRemove(E4_VOLUME *Volume, uint32_t Inode, const char *Name, uint32_t NameLength);
int E4XattrList(E4_VOLUME *Volume, uint32_t Inode, char *Buffer, uint32_t Size, uint32_t *Length);

typedef struct _E4_RESIZE_INFO
{
    uint64_t Blocks;
    /* The smallest size a shrink can reach and what the volume would then have free */
    uint64_t MinimumBlocks;
    uint32_t BlocksPerGroup;
    uint32_t BlockSize;
} E4_RESIZE_INFO;

/* Blocks is rounded down to what the layout allows; the size reached comes back in *Reached. The caller holds
 * the volume with no file open on it when it shrinks; growing needs no such care. */
int E4ResizeQuery(E4_VOLUME *Volume, E4_RESIZE_INFO *Info);
int E4Resize(E4_VOLUME *Volume, uint64_t Blocks, uint64_t *Reached);

#endif
