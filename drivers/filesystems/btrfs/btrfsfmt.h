/*
 * PROJECT:     LiberNT Btrfs file system driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Btrfs on-disk format (btrfs.readthedocs.io On-disk Format, Linux include/uapi/linux/btrfs_tree.h layouts)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define BTRFS_SUPERBLOCK_OFFSET         0x10000ULL
#define BTRFS_SUPERBLOCK_SIZE           0x1000
#define BTRFS_MAGIC                     0x4D5F53665248425FULL
#define BTRFS_CSUM_SIZE                 32
#define BTRFS_UUID_SIZE                 16
#define BTRFS_LABEL_SIZE                256
#define BTRFS_SYSTEM_CHUNK_ARRAY_SIZE   2048
#define BTRFS_MAX_LEVEL                 8
#define BTRFS_MAX_NODESIZE              0x10000
#define BTRFS_MAX_UNCOMPRESSED          0x20000
#define BTRFS_NAME_LENGTH               255

#define BTRFS_CSUM_TYPE_CRC32           0
#define BTRFS_CSUM_TYPE_XXHASH          1
#define BTRFS_CSUM_TYPE_SHA256          2
#define BTRFS_CSUM_TYPE_BLAKE2          3

#define BTRFS_INCOMPAT_MIXED_BACKREF    (1ULL << 0)
#define BTRFS_INCOMPAT_DEFAULT_SUBVOL   (1ULL << 1)
#define BTRFS_INCOMPAT_MIXED_GROUPS     (1ULL << 2)
#define BTRFS_INCOMPAT_COMPRESS_LZO     (1ULL << 3)
#define BTRFS_INCOMPAT_COMPRESS_ZSTD    (1ULL << 4)
#define BTRFS_INCOMPAT_BIG_METADATA     (1ULL << 5)
#define BTRFS_INCOMPAT_EXTENDED_IREF    (1ULL << 6)
#define BTRFS_INCOMPAT_RAID56           (1ULL << 7)
#define BTRFS_INCOMPAT_SKINNY_METADATA  (1ULL << 8)
#define BTRFS_INCOMPAT_NO_HOLES         (1ULL << 9)
#define BTRFS_INCOMPAT_METADATA_UUID    (1ULL << 10)
#define BTRFS_INCOMPAT_RAID1C34         (1ULL << 11)
#define BTRFS_INCOMPAT_ZONED            (1ULL << 12)
#define BTRFS_INCOMPAT_EXTENT_TREE_V2   (1ULL << 13)
#define BTRFS_INCOMPAT_RAID_STRIPE_TREE (1ULL << 14)
#define BTRFS_INCOMPAT_SIMPLE_QUOTA     (1ULL << 16)
#define BTRFS_INCOMPAT_REMAP_TREE       (1ULL << 17)

#define BTRFS_COMPAT_RO_FREE_SPACE_TREE       (1ULL << 0)
#define BTRFS_COMPAT_RO_FREE_SPACE_TREE_VALID (1ULL << 1)
#define BTRFS_COMPAT_RO_VERITY                (1ULL << 2)
#define BTRFS_COMPAT_RO_BLOCK_GROUP_TREE      (1ULL << 3)

#define BTRFS_HEADER_FLAG_WRITTEN       (1ULL << 0)
#define BTRFS_HEADER_FLAG_RELOC         (1ULL << 1)
#define BTRFS_BACKREF_REV_SHIFT         56
#define BTRFS_MIXED_BACKREF_REV         1ULL

#define BTRFS_EXTENT_FLAG_DATA          (1ULL << 0)
#define BTRFS_EXTENT_FLAG_TREE_BLOCK    (1ULL << 1)
#define BTRFS_EXTENT_FLAG_FULL_BACKREF  (1ULL << 7)

#define BTRFS_FREE_SPACE_USING_BITMAPS  (1UL << 0)

#define BTRFS_ROOT_TREE_OBJECTID        1ULL
#define BTRFS_EXTENT_TREE_OBJECTID      2ULL
#define BTRFS_CHUNK_TREE_OBJECTID       3ULL
#define BTRFS_DEV_TREE_OBJECTID         4ULL
#define BTRFS_CSUM_TREE_OBJECTID        7ULL
#define BTRFS_FREE_SPACE_TREE_OBJECTID  10ULL
#define BTRFS_BLOCK_GROUP_TREE_OBJECTID 11ULL
#define BTRFS_EXTENT_CSUM_OBJECTID      ((ULONGLONG)-10LL)
#define BTRFS_FS_TREE_OBJECTID          5ULL
#define BTRFS_DEV_ITEMS_OBJECTID        1ULL
#define BTRFS_ROOT_TREE_DIR_OBJECTID    6ULL
#define BTRFS_FIRST_FREE_OBJECTID       256ULL
#define BTRFS_LAST_FREE_OBJECTID        ((ULONGLONG)-256LL)
#define BTRFS_FIRST_CHUNK_TREE_OBJECTID 256ULL

#define BTRFS_INODE_ITEM_KEY            1
#define BTRFS_INODE_REF_KEY             12
#define BTRFS_INODE_EXTREF_KEY          13
#define BTRFS_XATTR_ITEM_KEY            24
#define BTRFS_DIR_ITEM_KEY              84
#define BTRFS_DIR_INDEX_KEY             96
#define BTRFS_EXTENT_DATA_KEY           108
#define BTRFS_EXTENT_CSUM_KEY           128
#define BTRFS_ROOT_ITEM_KEY             132
#define BTRFS_EXTENT_ITEM_KEY           168
#define BTRFS_METADATA_ITEM_KEY         169
#define BTRFS_TREE_BLOCK_REF_KEY        176
#define BTRFS_EXTENT_DATA_REF_KEY       178
#define BTRFS_SHARED_BLOCK_REF_KEY      182
#define BTRFS_SHARED_DATA_REF_KEY       184
#define BTRFS_BLOCK_GROUP_ITEM_KEY      192
#define BTRFS_FREE_SPACE_INFO_KEY       198
#define BTRFS_FREE_SPACE_EXTENT_KEY     199
#define BTRFS_FREE_SPACE_BITMAP_KEY     200
#define BTRFS_DEV_EXTENT_KEY            204
#define BTRFS_DEV_ITEM_KEY              216
#define BTRFS_CHUNK_ITEM_KEY            228

#define BTRFS_STRIPE_LEN                0x10000ULL
#define BTRFS_NUM_BACKUP_ROOTS          4

#define BTRFS_BLOCK_GROUP_DATA          (1ULL << 0)
#define BTRFS_BLOCK_GROUP_SYSTEM        (1ULL << 1)
#define BTRFS_BLOCK_GROUP_METADATA      (1ULL << 2)
#define BTRFS_BLOCK_GROUP_RAID0         (1ULL << 3)
#define BTRFS_BLOCK_GROUP_RAID1         (1ULL << 4)
#define BTRFS_BLOCK_GROUP_DUP           (1ULL << 5)
#define BTRFS_BLOCK_GROUP_RAID10        (1ULL << 6)
#define BTRFS_BLOCK_GROUP_RAID5         (1ULL << 7)
#define BTRFS_BLOCK_GROUP_RAID6         (1ULL << 8)
#define BTRFS_BLOCK_GROUP_RAID1C3       (1ULL << 9)
#define BTRFS_BLOCK_GROUP_RAID1C4       (1ULL << 10)
#define BTRFS_BLOCK_GROUP_REMAPPED      (1ULL << 11)
#define BTRFS_BLOCK_GROUP_TYPES         (BTRFS_BLOCK_GROUP_DATA | BTRFS_BLOCK_GROUP_SYSTEM | BTRFS_BLOCK_GROUP_METADATA)
#define BTRFS_BLOCK_GROUP_PROFILES      (BTRFS_BLOCK_GROUP_RAID0 | BTRFS_BLOCK_GROUP_RAID1 | BTRFS_BLOCK_GROUP_DUP | \
                                         BTRFS_BLOCK_GROUP_RAID10 | BTRFS_BLOCK_GROUP_RAID5 | BTRFS_BLOCK_GROUP_RAID6 | \
                                         BTRFS_BLOCK_GROUP_RAID1C3 | BTRFS_BLOCK_GROUP_RAID1C4)

#define BTRFS_FILE_EXTENT_INLINE        0
#define BTRFS_FILE_EXTENT_REG           1
#define BTRFS_FILE_EXTENT_PREALLOC      2

#define BTRFS_COMPRESS_NONE             0
#define BTRFS_COMPRESS_ZLIB             1
#define BTRFS_COMPRESS_LZO              2
#define BTRFS_COMPRESS_ZSTD             3

#define BTRFS_FT_UNKNOWN                0
#define BTRFS_FT_REG_FILE               1
#define BTRFS_FT_DIR                    2
#define BTRFS_FT_CHRDEV                 3
#define BTRFS_FT_BLKDEV                 4
#define BTRFS_FT_FIFO                   5
#define BTRFS_FT_SOCK                   6
#define BTRFS_FT_SYMLINK                7
#define BTRFS_FT_XATTR                  8

#define BTRFS_S_IFMT                    0170000
#define BTRFS_S_IFSOCK                  0140000
#define BTRFS_S_IFLNK                   0120000
#define BTRFS_S_IFREG                   0100000
#define BTRFS_S_IFBLK                   0060000
#define BTRFS_S_IFDIR                   0040000
#define BTRFS_S_IFCHR                   0020000
#define BTRFS_S_IFIFO                   0010000

#pragma pack(push, 1)

typedef struct _BTRFS_KEY
{
    ULONGLONG ObjectId;
    UCHAR Type;
    ULONGLONG Offset;
} BTRFS_KEY, *PBTRFS_KEY;

typedef struct _BTRFS_TIME
{
    LONGLONG Seconds;
    ULONG Nanoseconds;
} BTRFS_TIME, *PBTRFS_TIME;

typedef struct _BTRFS_HEADER
{
    UCHAR Checksum[BTRFS_CSUM_SIZE];
    UCHAR FsId[BTRFS_UUID_SIZE];
    ULONGLONG ByteNr;
    ULONGLONG Flags;
    UCHAR ChunkTreeUuid[BTRFS_UUID_SIZE];
    ULONGLONG Generation;
    ULONGLONG Owner;
    ULONG ItemCount;
    UCHAR Level;
} BTRFS_HEADER, *PBTRFS_HEADER;

typedef struct _BTRFS_ITEM
{
    BTRFS_KEY Key;
    ULONG Offset;
    ULONG Size;
} BTRFS_ITEM, *PBTRFS_ITEM;

typedef struct _BTRFS_KEY_POINTER
{
    BTRFS_KEY Key;
    ULONGLONG BlockPointer;
    ULONGLONG Generation;
} BTRFS_KEY_POINTER, *PBTRFS_KEY_POINTER;

typedef struct _BTRFS_DEV_ITEM
{
    ULONGLONG DeviceId;
    ULONGLONG TotalBytes;
    ULONGLONG BytesUsed;
    ULONG IoAlign;
    ULONG IoWidth;
    ULONG SectorSize;
    ULONGLONG Type;
    ULONGLONG Generation;
    ULONGLONG StartOffset;
    ULONG DeviceGroup;
    UCHAR SeekSpeed;
    UCHAR Bandwidth;
    UCHAR Uuid[BTRFS_UUID_SIZE];
    UCHAR FsId[BTRFS_UUID_SIZE];
} BTRFS_DEV_ITEM, *PBTRFS_DEV_ITEM;

typedef struct _BTRFS_SUPERBLOCK
{
    UCHAR Checksum[BTRFS_CSUM_SIZE];
    UCHAR FsId[BTRFS_UUID_SIZE];
    ULONGLONG ByteNr;
    ULONGLONG Flags;
    ULONGLONG Magic;
    ULONGLONG Generation;
    ULONGLONG Root;
    ULONGLONG ChunkRoot;
    ULONGLONG LogRoot;
    ULONGLONG LogRootTransId;
    ULONGLONG TotalBytes;
    ULONGLONG BytesUsed;
    ULONGLONG RootDirObjectId;
    ULONGLONG DeviceCount;
    ULONG SectorSize;
    ULONG NodeSize;
    ULONG LeafSize;
    ULONG StripeSize;
    ULONG SysChunkArraySize;
    ULONGLONG ChunkRootGeneration;
    ULONGLONG CompatFlags;
    ULONGLONG CompatRoFlags;
    ULONGLONG IncompatFlags;
    USHORT ChecksumType;
    UCHAR RootLevel;
    UCHAR ChunkRootLevel;
    UCHAR LogRootLevel;
    BTRFS_DEV_ITEM DevItem;
    CHAR Label[BTRFS_LABEL_SIZE];
    ULONGLONG CacheGeneration;
    ULONGLONG UuidTreeGeneration;
    UCHAR MetadataUuid[BTRFS_UUID_SIZE];
    UCHAR Reserved[224];
    UCHAR SysChunkArray[BTRFS_SYSTEM_CHUNK_ARRAY_SIZE];
    UCHAR SuperRoots[672];
    UCHAR Padding[565];
} BTRFS_SUPERBLOCK, *PBTRFS_SUPERBLOCK;

typedef struct _BTRFS_STRIPE
{
    ULONGLONG DeviceId;
    ULONGLONG Offset;
    UCHAR DeviceUuid[BTRFS_UUID_SIZE];
} BTRFS_STRIPE, *PBTRFS_STRIPE;

typedef struct _BTRFS_CHUNK
{
    ULONGLONG Length;
    ULONGLONG Owner;
    ULONGLONG StripeLength;
    ULONGLONG Type;
    ULONG IoAlign;
    ULONG IoWidth;
    ULONG SectorSize;
    USHORT StripeCount;
    USHORT SubStripes;
    BTRFS_STRIPE Stripes[1];
} BTRFS_CHUNK, *PBTRFS_CHUNK;

typedef struct _BTRFS_INODE_ITEM
{
    ULONGLONG Generation;
    ULONGLONG TransId;
    ULONGLONG Size;
    ULONGLONG Bytes;
    ULONGLONG BlockGroup;
    ULONG LinkCount;
    ULONG Uid;
    ULONG Gid;
    ULONG Mode;
    ULONGLONG RDev;
    ULONGLONG Flags;
    ULONGLONG Sequence;
    ULONGLONG Reserved[4];
    BTRFS_TIME AccessTime;
    BTRFS_TIME ChangeTime;
    BTRFS_TIME ModifyTime;
    BTRFS_TIME CreateTime;
} BTRFS_INODE_ITEM, *PBTRFS_INODE_ITEM;

typedef struct _BTRFS_DIR_ITEM
{
    BTRFS_KEY Location;
    ULONGLONG TransId;
    USHORT DataLength;
    USHORT NameLength;
    UCHAR Type;
    CHAR Name[1];
} BTRFS_DIR_ITEM, *PBTRFS_DIR_ITEM;

typedef struct _BTRFS_ROOT_ITEM
{
    BTRFS_INODE_ITEM Inode;
    ULONGLONG Generation;
    ULONGLONG RootDirId;
    ULONGLONG ByteNr;
    ULONGLONG ByteLimit;
    ULONGLONG BytesUsed;
    ULONGLONG LastSnapshot;
    ULONGLONG Flags;
    ULONG Refs;
    BTRFS_KEY DropProgress;
    UCHAR DropLevel;
    UCHAR Level;
} BTRFS_ROOT_ITEM, *PBTRFS_ROOT_ITEM;

typedef struct _BTRFS_DEV_EXTENT
{
    ULONGLONG ChunkTree;
    ULONGLONG ChunkObjectId;
    ULONGLONG ChunkOffset;
    ULONGLONG Length;
    UCHAR ChunkTreeUuid[BTRFS_UUID_SIZE];
} BTRFS_DEV_EXTENT, *PBTRFS_DEV_EXTENT;

typedef struct _BTRFS_ROOT_BACKUP
{
    ULONGLONG TreeRoot;
    ULONGLONG TreeRootGeneration;
    ULONGLONG ChunkRoot;
    ULONGLONG ChunkRootGeneration;
    ULONGLONG ExtentRoot;
    ULONGLONG ExtentRootGeneration;
    ULONGLONG FsRoot;
    ULONGLONG FsRootGeneration;
    ULONGLONG DevRoot;
    ULONGLONG DevRootGeneration;
    ULONGLONG CsumRoot;
    ULONGLONG CsumRootGeneration;
    ULONGLONG TotalBytes;
    ULONGLONG BytesUsed;
    ULONGLONG DeviceCount;
    ULONGLONG Unused64[4];
    UCHAR TreeRootLevel;
    UCHAR ChunkRootLevel;
    UCHAR ExtentRootLevel;
    UCHAR FsRootLevel;
    UCHAR DevRootLevel;
    UCHAR CsumRootLevel;
    UCHAR Unused8[10];
} BTRFS_ROOT_BACKUP, *PBTRFS_ROOT_BACKUP;

typedef struct _BTRFS_INODE_REF
{
    ULONGLONG Index;
    USHORT NameLength;
    CHAR Name[1];
} BTRFS_INODE_REF, *PBTRFS_INODE_REF;

typedef struct _BTRFS_EXTENT_ITEM
{
    ULONGLONG Refs;
    ULONGLONG Generation;
    ULONGLONG Flags;
} BTRFS_EXTENT_ITEM, *PBTRFS_EXTENT_ITEM;

typedef struct _BTRFS_EXTENT_DATA_REF
{
    ULONGLONG Root;
    ULONGLONG ObjectId;
    ULONGLONG Offset;
    ULONG Count;
} BTRFS_EXTENT_DATA_REF, *PBTRFS_EXTENT_DATA_REF;

typedef struct _BTRFS_BLOCK_GROUP_ITEM
{
    ULONGLONG Used;
    ULONGLONG ChunkObjectId;
    ULONGLONG Flags;
} BTRFS_BLOCK_GROUP_ITEM, *PBTRFS_BLOCK_GROUP_ITEM;

typedef struct _BTRFS_FREE_SPACE_INFO
{
    ULONG ExtentCount;
    ULONG Flags;
} BTRFS_FREE_SPACE_INFO, *PBTRFS_FREE_SPACE_INFO;

typedef struct _BTRFS_ROOT_ITEM_V2
{
    BTRFS_ROOT_ITEM Legacy;
    ULONGLONG GenerationV2;
    UCHAR Uuid[BTRFS_UUID_SIZE];
    UCHAR ParentUuid[BTRFS_UUID_SIZE];
    UCHAR ReceivedUuid[BTRFS_UUID_SIZE];
    ULONGLONG CTransId;
    ULONGLONG OTransId;
    ULONGLONG STransId;
    ULONGLONG RTransId;
    BTRFS_TIME CTime;
    BTRFS_TIME OTime;
    BTRFS_TIME STime;
    BTRFS_TIME RTime;
    ULONGLONG Reserved[8];
} BTRFS_ROOT_ITEM_V2, *PBTRFS_ROOT_ITEM_V2;

typedef struct _BTRFS_FILE_EXTENT_ITEM
{
    ULONGLONG Generation;
    ULONGLONG RamBytes;
    UCHAR Compression;
    UCHAR Encryption;
    USHORT OtherEncoding;
    UCHAR Type;
    ULONGLONG DiskByteNr;
    ULONGLONG DiskNumBytes;
    ULONGLONG Offset;
    ULONGLONG NumBytes;
} BTRFS_FILE_EXTENT_ITEM, *PBTRFS_FILE_EXTENT_ITEM;

#pragma pack(pop)

#define BTRFS_DIR_ITEM_FIXED_SIZE       FIELD_OFFSET(BTRFS_DIR_ITEM, Name)
#define BTRFS_CHUNK_FIXED_SIZE          FIELD_OFFSET(BTRFS_CHUNK, Stripes)
#define BTRFS_FILE_EXTENT_INLINE_DATA   FIELD_OFFSET(BTRFS_FILE_EXTENT_ITEM, DiskByteNr)

C_ASSERT(sizeof(BTRFS_KEY) == 17);
C_ASSERT(sizeof(BTRFS_TIME) == 12);
C_ASSERT(sizeof(BTRFS_HEADER) == 101);
C_ASSERT(sizeof(BTRFS_ITEM) == 25);
C_ASSERT(sizeof(BTRFS_KEY_POINTER) == 33);
C_ASSERT(sizeof(BTRFS_DEV_ITEM) == 98);
C_ASSERT(sizeof(BTRFS_SUPERBLOCK) == BTRFS_SUPERBLOCK_SIZE);
C_ASSERT(FIELD_OFFSET(BTRFS_SUPERBLOCK, Magic) == 0x40);
C_ASSERT(FIELD_OFFSET(BTRFS_SUPERBLOCK, SectorSize) == 0x90);
C_ASSERT(FIELD_OFFSET(BTRFS_SUPERBLOCK, SysChunkArraySize) == 0xa0);
C_ASSERT(FIELD_OFFSET(BTRFS_SUPERBLOCK, IncompatFlags) == 0xbc);
C_ASSERT(FIELD_OFFSET(BTRFS_SUPERBLOCK, ChecksumType) == 0xc4);
C_ASSERT(FIELD_OFFSET(BTRFS_SUPERBLOCK, DevItem) == 0xc9);
C_ASSERT(FIELD_OFFSET(BTRFS_SUPERBLOCK, Label) == 0x12b);
C_ASSERT(FIELD_OFFSET(BTRFS_SUPERBLOCK, MetadataUuid) == 0x23b);
C_ASSERT(FIELD_OFFSET(BTRFS_SUPERBLOCK, SysChunkArray) == 0x32b);
C_ASSERT(FIELD_OFFSET(BTRFS_SUPERBLOCK, SuperRoots) == 0xb2b);
C_ASSERT(sizeof(BTRFS_STRIPE) == 32);
C_ASSERT(BTRFS_CHUNK_FIXED_SIZE == 48);
C_ASSERT(sizeof(BTRFS_INODE_ITEM) == 160);
C_ASSERT(FIELD_OFFSET(BTRFS_INODE_ITEM, Mode) == 52);
C_ASSERT(FIELD_OFFSET(BTRFS_INODE_ITEM, AccessTime) == 112);
C_ASSERT(FIELD_OFFSET(BTRFS_INODE_ITEM, CreateTime) == 148);
C_ASSERT(BTRFS_DIR_ITEM_FIXED_SIZE == 30);
C_ASSERT(FIELD_OFFSET(BTRFS_ROOT_ITEM, RootDirId) == 168);
C_ASSERT(FIELD_OFFSET(BTRFS_ROOT_ITEM, ByteNr) == 176);
C_ASSERT(FIELD_OFFSET(BTRFS_ROOT_ITEM, Level) == 238);
C_ASSERT(sizeof(BTRFS_ROOT_ITEM) == 239);
C_ASSERT(BTRFS_FILE_EXTENT_INLINE_DATA == 21);
C_ASSERT(sizeof(BTRFS_FILE_EXTENT_ITEM) == 53);
C_ASSERT(FIELD_OFFSET(BTRFS_INODE_REF, Name) == 10);
C_ASSERT(sizeof(BTRFS_EXTENT_ITEM) == 24);
C_ASSERT(sizeof(BTRFS_EXTENT_DATA_REF) == 28);
C_ASSERT(sizeof(BTRFS_BLOCK_GROUP_ITEM) == 24);
C_ASSERT(sizeof(BTRFS_DEV_EXTENT) == 48);
C_ASSERT(sizeof(BTRFS_ROOT_BACKUP) == 168);
C_ASSERT(RTL_FIELD_SIZE(BTRFS_SUPERBLOCK, SuperRoots) == BTRFS_NUM_BACKUP_ROOTS * sizeof(BTRFS_ROOT_BACKUP));
C_ASSERT(sizeof(BTRFS_FREE_SPACE_INFO) == 8);
C_ASSERT(FIELD_OFFSET(BTRFS_ROOT_ITEM_V2, GenerationV2) == 239);
C_ASSERT(FIELD_OFFSET(BTRFS_ROOT_ITEM_V2, CTransId) == 295);
C_ASSERT(FIELD_OFFSET(BTRFS_ROOT_ITEM_V2, CTime) == 327);
C_ASSERT(sizeof(BTRFS_ROOT_ITEM_V2) == 439);
