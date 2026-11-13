/*
 * PROJECT:     LiberNT Btrfs file system driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Driver structures and internal interfaces
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <ntifs.h>
#include <ntdddisk.h>
#include <pseh/pseh2.h>
#include "btrfsfmt.h"

#define NDEBUG
#include <debug.h>

#define TAG_BTRFS               'frtB'
#define TAG_BTRFS_NODE          'nrtB'
#define TAG_BTRFS_FCB           'frtB'
#define TAG_BTRFS_IO            'irtB'

#define BTRFS_VCB_SIGNATURE     'BCVB'
#define BTRFS_FCB_SIGNATURE     'BCFB'
#define BTRFS_CCB_SIGNATURE     'BCCB'

#define BTRFS_NODE_CACHE_LIMIT  512
#define BTRFS_IO_CHUNK          0x100000
#define BTRFS_DIR_CURSOR_DOT    0
#define BTRFS_DIR_CURSOR_DOTDOT 1
#define BTRFS_DIR_CURSOR_FIRST  2

typedef struct _BTRFS_CHUNK_MAP
{
    ULONGLONG Logical;
    ULONGLONG Length;
    ULONGLONG Type;
    ULONGLONG StripeLength;
    USHORT StripeCount;
    USHORT SubStripes;
    ULONGLONG StripeOffsets[1];
} BTRFS_CHUNK_MAP, *PBTRFS_CHUNK_MAP;

typedef struct _BTRFS_NODE
{
    LIST_ENTRY LruLinks;
    ULONGLONG Logical;
    LONG References;
    BOOLEAN Cached;
    BOOLEAN Dirty;
    BOOLEAN Deleted;
    PBTRFS_HEADER Header;
} BTRFS_NODE, *PBTRFS_NODE;

typedef struct _BTRFS_ROOT
{
    ULONGLONG Id;
    ULONGLONG ByteNr;
    UCHAR Level;
    ULONGLONG RootDirId;
} BTRFS_ROOT, *PBTRFS_ROOT;

typedef struct _BTRFS_LIVE_ROOT
{
    LIST_ENTRY Links;
    BTRFS_ROOT Root;
    BOOLEAN Dirty;
    LONGLONG BytesUsedDelta;
    ULONGLONG HighestInode;
} BTRFS_LIVE_ROOT, *PBTRFS_LIVE_ROOT;

typedef struct _BTRFS_RANGE
{
    ULONGLONG Start;
    ULONGLONG Length;
} BTRFS_RANGE, *PBTRFS_RANGE;

typedef struct _BTRFS_SPACE
{
    ULONGLONG Start;
    ULONGLONG Length;
    ULONGLONG Flags;
    ULONGLONG Used;
    ULONGLONG CommittedUsed;
    PBTRFS_RANGE Free;
    ULONG FreeCount;
    ULONG FreeCapacity;
    PBTRFS_RANGE Pinned;
    ULONG PinnedCount;
    ULONG PinnedCapacity;
    BOOLEAN Changed;
} BTRFS_SPACE, *PBTRFS_SPACE;

#define BTRFS_REF_META_ADD  1
#define BTRFS_REF_META_DROP 2
#define BTRFS_REF_DATA_ADD  3
#define BTRFS_REF_DATA_DROP 4

typedef struct _BTRFS_PENDING_REF
{
    LIST_ENTRY Links;
    UCHAR Type;
    ULONGLONG ByteNr;
    ULONGLONG Length;
    ULONGLONG Root;
    ULONGLONG ObjectId;
    ULONGLONG Offset;
} BTRFS_PENDING_REF, *PBTRFS_PENDING_REF;

typedef struct _BTRFS_PATH
{
    PBTRFS_NODE Nodes[BTRFS_MAX_LEVEL];
    ULONG Slots[BTRFS_MAX_LEVEL];
    UCHAR Levels;
} BTRFS_PATH, *PBTRFS_PATH;

typedef struct _BTRFS_VCB
{
    ULONG Signature;
    PDEVICE_OBJECT VolumeDeviceObject;
    PDEVICE_OBJECT TargetDeviceObject;
    PVPB Vpb;
    ERESOURCE Resource;
    LIST_ENTRY FcbList;
    LIST_ENTRY VcbLinks;
    ULONG OpenHandles;
    ULONG References;
    BOOLEAN Locked;
    BOOLEAN Dismounted;
    PFILE_OBJECT LockFileObject;
    struct _BTRFS_FCB *RootFcb;
    struct _BTRFS_FCB *VolumeFcb;
    BTRFS_SUPERBLOCK Super;
    ULONG NodeSize;
    ULONG SectorSize;
    ULONG DeviceSectorSize;
    USHORT ChecksumType;
    ULONG ChecksumSize;
    ULONGLONG DeviceId;
    ULONGLONG PartitionLength;
    PUCHAR HeaderFsId;
    UCHAR ChunkTreeUuid[BTRFS_UUID_SIZE];
    PBTRFS_CHUNK_MAP *Chunks;
    ULONG ChunkCount;
    ULONG ChunkCapacity;
    BTRFS_ROOT RootTree;
    BTRFS_ROOT DefaultRoot;
    FAST_MUTEX NodeCacheLock;
    LIST_ENTRY NodeLru;
    ULONG NodeCount;
    PNOTIFY_SYNC NotifySync;
    LIST_ENTRY DirNotifyList;
    ERESOURCE TreeLock;
    LIST_ENTRY LiveRoots;
    BOOLEAN Writable;
    BOOLEAN TransActive;
    BOOLEAN SpaceLoaded;
    ULONGLONG TransId;
    LIST_ENTRY DirtyNodes;
    LIST_ENTRY PendingRefs;
    PBTRFS_SPACE Spaces;
    ULONG SpaceCount;
    LONGLONG BytesUsedDelta;
    KTIMER CommitTimer;
    KDPC CommitDpc;
    WORK_QUEUE_ITEM CommitWork;
    LONG CommitQueued;
    ULONG ChangeSequence;
    ULONGLONG DelallocBytes;
} BTRFS_VCB, *PBTRFS_VCB;

typedef struct _BTRFS_FCB
{
    FSRTL_ADVANCED_FCB_HEADER Header;
    FAST_MUTEX HeaderMutex;
    SECTION_OBJECT_POINTERS SectionObjectPointers;
    ERESOURCE MainResource;
    ERESOURCE PagingIoResource;
    ULONG Signature;
    PBTRFS_VCB Vcb;
    LIST_ENTRY VcbLinks;
    LONG References;
    ULONG OpenHandles;
    BTRFS_ROOT Root;
    ULONGLONG Inode;
    BTRFS_INODE_ITEM InodeItem;
    ULONG Attributes;
    ULONG ReparseTag;
    BOOLEAN Directory;
    BOOLEAN Volume;
    BOOLEAN CacheInitialized;
    BOOLEAN DeletePending;
    BOOLEAN Modified;
    BOOLEAN Removed;
    ULONGLONG ParentInode;
    UNICODE_STRING LinkName;
    SHARE_ACCESS ShareAccess;
    FILE_LOCK FileLock;
    FAST_MUTEX ExtentCacheLock;
    ULONGLONG CachedExtentByteNr;
    ULONG CachedExtentLength;
    PUCHAR CachedExtent;
    PBTRFS_RANGE Reserved;
    ULONG ReservedCount;
} BTRFS_FCB, *PBTRFS_FCB;

typedef struct _BTRFS_CCB
{
    ULONG Signature;
    UNICODE_STRING FullName;
    PWCHAR AllocatedName;
    ULONGLONG ParentInode;
    ULONGLONG ParentRootId;
    BOOLEAN DeleteOnClose;
    UNICODE_STRING SearchPattern;
    BOOLEAN PatternHasWildcards;
    BOOLEAN SearchStarted;
    BOOLEAN ReturnedAny;
    ULONGLONG Cursor;
} BTRFS_CCB, *PBTRFS_CCB;

typedef struct _BTRFS_DIR_ENTRY
{
    ULONGLONG Index;
    BTRFS_KEY Location;
    UCHAR Type;
    UNICODE_STRING Name;
} BTRFS_DIR_ENTRY, *PBTRFS_DIR_ENTRY;

typedef struct _BTRFS_GLOBAL
{
    PDRIVER_OBJECT DriverObject;
    PDEVICE_OBJECT FsDeviceObject;
    ERESOURCE Resource;
    LIST_ENTRY VcbList;
    FAST_IO_DISPATCH FastIoDispatch;
    CACHE_MANAGER_CALLBACKS CacheCallbacks;
} BTRFS_GLOBAL;

extern BTRFS_GLOBAL BtrfsGlobal;

/* checksum.c */
ULONG BtrfsCrc32c(_In_ ULONG Seed, _In_reads_bytes_(Length) const VOID *Data, _In_ SIZE_T Length);
ULONG BtrfsNameHash(_In_reads_bytes_(Length) const CHAR *Name, _In_ SIZE_T Length);
BOOLEAN BtrfsChecksumSupported(_In_ USHORT Type);
ULONG BtrfsChecksumSize(_In_ USHORT Type);
BOOLEAN BtrfsVerifyChecksum(_In_ USHORT Type, _In_reads_bytes_(Length) const VOID *Data, _In_ SIZE_T Length, _In_reads_bytes_(BTRFS_CSUM_SIZE) const UCHAR *Expected);

/* device.c */
NTSTATUS BtrfsReadDevice(_In_ PDEVICE_OBJECT Device, _In_ ULONGLONG Offset, _Out_writes_bytes_(Length) PVOID Buffer, _In_ ULONG Length, _In_ BOOLEAN OverrideVerify);
NTSTATUS BtrfsWriteDevice(_In_ PDEVICE_OBJECT Device, _In_ ULONGLONG Offset, _In_reads_bytes_(Length) const VOID *Buffer, _In_ ULONG Length);
NTSTATUS BtrfsFlushDevice(_In_ PDEVICE_OBJECT Device);
NTSTATUS BtrfsDeviceControl(_In_ PDEVICE_OBJECT Device, _In_ ULONG Code, _Out_writes_bytes_opt_(OutputLength) PVOID Output, _In_ ULONG OutputLength, _In_ BOOLEAN OverrideVerify);

/* chunk.c */
NTSTATUS BtrfsAddChunk(_Inout_ PBTRFS_VCB Vcb, _In_ ULONGLONG Logical, _In_reads_bytes_(Size) const BTRFS_CHUNK *Chunk, _In_ ULONG Size);
NTSTATUS BtrfsLoadSystemChunks(_Inout_ PBTRFS_VCB Vcb);
NTSTATUS BtrfsLoadChunkTree(_Inout_ PBTRFS_VCB Vcb);
VOID BtrfsFreeChunks(_Inout_ PBTRFS_VCB Vcb);
ULONG BtrfsMirrorCount(_In_ PBTRFS_VCB Vcb, _In_ ULONGLONG Logical);
NTSTATUS BtrfsReadLogical(_In_ PBTRFS_VCB Vcb, _In_ ULONGLONG Logical, _Out_writes_bytes_(Length) PVOID Buffer, _In_ ULONG Length, _In_ ULONG Mirror);

/* tree.c */
NTSTATUS BtrfsReadNode(_In_ PBTRFS_VCB Vcb, _In_ ULONGLONG Logical, _In_ UCHAR ExpectedLevel, _Out_ PBTRFS_NODE *Node);
VOID BtrfsReleaseNode(_In_ PBTRFS_VCB Vcb, _In_ PBTRFS_NODE Node);
VOID BtrfsFlushNodeCache(_Inout_ PBTRFS_VCB Vcb);
LONG BtrfsCompareKeys(_In_ const BTRFS_KEY *First, _In_ const BTRFS_KEY *Second);
VOID BtrfsInitPath(_Out_ PBTRFS_PATH Path);
VOID BtrfsReleasePath(_In_ PBTRFS_VCB Vcb, _Inout_ PBTRFS_PATH Path);
NTSTATUS BtrfsSearch(_In_ PBTRFS_VCB Vcb, _In_ const BTRFS_ROOT *Root, _In_ const BTRFS_KEY *Key, _Out_ PBTRFS_PATH Path);
NTSTATUS BtrfsNextItem(_In_ PBTRFS_VCB Vcb, _Inout_ PBTRFS_PATH Path);
NTSTATUS BtrfsPreviousItem(_In_ PBTRFS_VCB Vcb, _Inout_ PBTRFS_PATH Path);
PBTRFS_ITEM BtrfsPathItem(_In_ PBTRFS_PATH Path);
PVOID BtrfsPathItemData(_In_ PBTRFS_VCB Vcb, _In_ PBTRFS_PATH Path, _Out_ PULONG Size);
NTSTATUS BtrfsFindItem(_In_ PBTRFS_VCB Vcb, _In_ const BTRFS_ROOT *Root, _In_ const BTRFS_KEY *Key, _Out_writes_bytes_(BufferSize) PVOID Buffer, _In_ ULONG BufferSize, _Out_opt_ PULONG ItemSize);

/* decompress.c */
NTSTATUS BtrfsDecompress(_In_ PBTRFS_VCB Vcb, _In_ UCHAR Type, _In_reads_bytes_(InputLength) const UCHAR *Input, _In_ ULONG InputLength, _Out_writes_bytes_(OutputLength) PUCHAR Output, _In_ ULONG OutputLength);

/* inode.c */
NTSTATUS BtrfsUtf8ToUnicode(_In_reads_bytes_(Length) const CHAR *Utf8, _In_ ULONG Length, _Out_ PUNICODE_STRING Name);
NTSTATUS BtrfsUnicodeToUtf8(_In_ PCUNICODE_STRING Name, _Out_writes_bytes_(BufferSize) PCHAR Buffer, _In_ ULONG BufferSize, _Out_ PULONG Length);
NTSTATUS BtrfsFindRoot(_In_ PBTRFS_VCB Vcb, _In_ ULONGLONG Id, _Out_ PBTRFS_ROOT Root);
NTSTATUS BtrfsLoadInode(_In_ PBTRFS_VCB Vcb, _In_ const BTRFS_ROOT *Root, _In_ ULONGLONG Inode, _Out_ PBTRFS_INODE_ITEM Item);
NTSTATUS BtrfsResolveLocation(_In_ PBTRFS_VCB Vcb, _In_ const BTRFS_ROOT *Parent, _In_ const BTRFS_KEY *Location, _Out_ PBTRFS_ROOT Root, _Out_ PULONGLONG Inode);
NTSTATUS BtrfsLookupName(_In_ PBTRFS_VCB Vcb, _In_ const BTRFS_ROOT *Root, _In_ ULONGLONG Directory, _In_ PCUNICODE_STRING Name, _In_ BOOLEAN IgnoreCase, _Out_ PBTRFS_KEY Location, _Out_ PUCHAR Type);
NTSTATUS BtrfsReadDirEntry(_In_ PBTRFS_VCB Vcb, _In_ const BTRFS_ROOT *Root, _In_ ULONGLONG Directory, _In_ ULONGLONG FromIndex, _Out_ PBTRFS_DIR_ENTRY Entry);
VOID BtrfsFreeDirEntry(_Inout_ PBTRFS_DIR_ENTRY Entry);
ULONG BtrfsInodeAttributes(_In_ const BTRFS_INODE_ITEM *Item, _In_opt_ PCUNICODE_STRING Name, _Out_opt_ PULONG ReparseTag);
LARGE_INTEGER BtrfsTime(_In_ const BTRFS_TIME *Time);
ULONGLONG BtrfsFileId(_In_ const BTRFS_ROOT *Root, _In_ ULONGLONG Inode);
NTSTATUS BtrfsGetFcb(_In_ PBTRFS_VCB Vcb, _In_ const BTRFS_ROOT *Root, _In_ ULONGLONG Inode, _In_opt_ PCUNICODE_STRING Name, _Out_ PBTRFS_FCB *Fcb);
PBTRFS_FCB BtrfsCreateVolumeFcb(_In_ PBTRFS_VCB Vcb);
VOID BtrfsReferenceFcb(_In_ PBTRFS_FCB Fcb);
VOID BtrfsDereferenceFcb(_In_ PBTRFS_FCB Fcb);

/* read.c */
NTSTATUS BtrfsReadFileData(_In_ PBTRFS_FCB Fcb, _In_ ULONGLONG Offset, _Out_writes_bytes_(Length) PUCHAR Buffer, _In_ ULONG Length);
NTSTATUS BtrfsReadSymlink(_In_ PBTRFS_FCB Fcb, _Out_ PCHAR *Target, _Out_ PULONG Length);

/* volume.c */
VOID BtrfsReferenceVcb(_In_ PBTRFS_VCB Vcb);
VOID BtrfsDereferenceVcb(_In_ PBTRFS_VCB Vcb);
NTSTATUS BtrfsMountVolume(_In_ PDEVICE_OBJECT TargetDevice, _In_ PVPB Vpb);
NTSTATUS BtrfsVerifyVolume(_In_ PIRP Irp);

/* fsctl.c */
NTSTATUS BtrfsDismountVolume(_In_ PBTRFS_VCB Vcb);

/* fileinfo.c */
NTSTATUS BtrfsMarkDeletePending(_In_ PBTRFS_FCB Fcb, _In_ PBTRFS_CCB Ccb, _In_ PFILE_OBJECT FileObject, _In_ BOOLEAN Delete);

/* write.c */
VOID BtrfsNotifyChange(_In_ PBTRFS_VCB Vcb, _In_ PCUNICODE_STRING FullName, _In_ ULONG Filter, _In_ ULONG Action);

/* dispatch routines */
DRIVER_DISPATCH BtrfsDispatchCreate;
DRIVER_DISPATCH BtrfsDispatchCleanup;
DRIVER_DISPATCH BtrfsDispatchClose;
DRIVER_DISPATCH BtrfsDispatchRead;
DRIVER_DISPATCH BtrfsDispatchWrite;
DRIVER_DISPATCH BtrfsDispatchQueryInformation;
DRIVER_DISPATCH BtrfsDispatchSetInformation;
DRIVER_DISPATCH BtrfsDispatchQueryVolumeInformation;
DRIVER_DISPATCH BtrfsDispatchSetVolumeInformation;
DRIVER_DISPATCH BtrfsDispatchDirectoryControl;
DRIVER_DISPATCH BtrfsDispatchFileSystemControl;
DRIVER_DISPATCH BtrfsDispatchDeviceControl;
DRIVER_DISPATCH BtrfsDispatchLockControl;
DRIVER_DISPATCH BtrfsDispatchFlushBuffers;
DRIVER_DISPATCH BtrfsDispatchPnp;
DRIVER_DISPATCH BtrfsDispatchShutdown;
DRIVER_DISPATCH BtrfsDispatchUnsupported;

NTSTATUS BtrfsCompleteRequest(_In_ PIRP Irp, _In_ NTSTATUS Status, _In_ ULONG_PTR Information);
BOOLEAN BtrfsIsFsDevice(_In_ PDEVICE_OBJECT DeviceObject);
PBTRFS_VCB BtrfsDeviceVcb(_In_ PDEVICE_OBJECT DeviceObject);

/* tree locking: readers share the trees, a change or a commit owns them */
VOID BtrfsAcquireTreeShared(_In_ PBTRFS_VCB Vcb);
VOID BtrfsAcquireTreeExclusive(_In_ PBTRFS_VCB Vcb);
VOID BtrfsReleaseTree(_In_ PBTRFS_VCB Vcb);
PBTRFS_LIVE_ROOT BtrfsLiveRoot(_In_ PBTRFS_VCB Vcb, _In_ ULONGLONG Id);

/* cow.c */
VOID BtrfsMarkNodeDirty(_In_ PBTRFS_VCB Vcb, _In_ PBTRFS_NODE Node);
NTSTATUS BtrfsSearchForWrite(_In_ PBTRFS_VCB Vcb, _In_ PBTRFS_LIVE_ROOT Root, _In_ const BTRFS_KEY *Key, _In_ LONG InsertLength, _Out_ PBTRFS_PATH Path);
NTSTATUS BtrfsInsertItem(_In_ PBTRFS_VCB Vcb, _In_ PBTRFS_LIVE_ROOT Root, _In_ const BTRFS_KEY *Key, _In_reads_bytes_opt_(Size) const VOID *Data, _In_ ULONG Size, _Out_opt_ PVOID *Written);
NTSTATUS BtrfsDeleteItemAt(_In_ PBTRFS_VCB Vcb, _In_ PBTRFS_LIVE_ROOT Root, _Inout_ PBTRFS_PATH Path);
NTSTATUS BtrfsDeleteItem(_In_ PBTRFS_VCB Vcb, _In_ PBTRFS_LIVE_ROOT Root, _In_ const BTRFS_KEY *Key);
NTSTATUS BtrfsResizeItemAt(_In_ PBTRFS_VCB Vcb, _Inout_ PBTRFS_PATH Path, _In_ ULONG NewSize);
VOID BtrfsSetKeyAt(_Inout_ PBTRFS_PATH Path, _In_ const BTRFS_KEY *Key);
NTSTATUS BtrfsUpdateItem(_In_ PBTRFS_VCB Vcb, _In_ PBTRFS_LIVE_ROOT Root, _In_ const BTRFS_KEY *Key, _In_reads_bytes_(Size) const VOID *Data, _In_ ULONG Size);
NTSTATUS BtrfsWriteNodes(_In_ PBTRFS_VCB Vcb);

/* space.c */
NTSTATUS BtrfsLoadSpace(_In_ PBTRFS_VCB Vcb);
VOID BtrfsFreeSpaceMaps(_In_ PBTRFS_VCB Vcb);
NTSTATUS BtrfsAllocate(_In_ PBTRFS_VCB Vcb, _In_ ULONGLONG Want, _In_ ULONGLONG Length, _Out_ PULONGLONG Logical);
ULONGLONG BtrfsFreeBytes(_In_ PBTRFS_VCB Vcb, _In_ ULONGLONG Want);
NTSTATUS BtrfsAddSpace(_In_ PBTRFS_VCB Vcb, _In_ ULONGLONG Start, _In_ ULONGLONG Length, _In_ ULONGLONG Flags);

/* reserve.c */
NTSTATUS BtrfsReserveCachedWrite(_In_ struct _BTRFS_FCB *Fcb, _In_ ULONGLONG Offset, _In_ ULONG Length);
VOID BtrfsReleaseReservation(_In_ struct _BTRFS_FCB *Fcb, _In_ ULONGLONG Offset, _In_ ULONGLONG End);
VOID BtrfsReleaseAllReservations(_In_ struct _BTRFS_FCB *Fcb);

/* blockgrp.c */
NTSTATUS BtrfsReserveSpace(_In_ PBTRFS_VCB Vcb, _In_ ULONGLONG Want, _In_ ULONGLONG Bytes);
NTSTATUS BtrfsPin(_In_ PBTRFS_VCB Vcb, _In_ ULONGLONG Logical, _In_ ULONGLONG Length);
NTSTATUS BtrfsWriteFreeSpaceTree(_In_ PBTRFS_VCB Vcb, _Out_ PBOOLEAN Changed);
NTSTATUS BtrfsWriteBlockGroups(_In_ PBTRFS_VCB Vcb);
VOID BtrfsUnpinAll(_In_ PBTRFS_VCB Vcb);

/* extent.c */
NTSTATUS BtrfsQueueRef(_In_ PBTRFS_VCB Vcb, _In_ UCHAR Type, _In_ ULONGLONG ByteNr, _In_ ULONGLONG Length, _In_ ULONGLONG Root, _In_ ULONGLONG ObjectId, _In_ ULONGLONG Offset);
NTSTATUS BtrfsRunPendingRefs(_In_ PBTRFS_VCB Vcb, _Out_ PBOOLEAN Ran);
NTSTATUS BtrfsInsertChecksums(_In_ PBTRFS_VCB Vcb, _In_ ULONGLONG Logical, _In_reads_bytes_(Length) const UCHAR *Data, _In_ ULONG Length);
NTSTATUS BtrfsDeleteChecksums(_In_ PBTRFS_VCB Vcb, _In_ ULONGLONG Logical, _In_ ULONGLONG Length);
VOID BtrfsComputeChecksum(_In_ USHORT Type, _In_reads_bytes_(Length) const VOID *Data, _In_ SIZE_T Length, _Out_writes_bytes_(BTRFS_CSUM_SIZE) UCHAR *Digest);

/* trans.c */
NTSTATUS BtrfsCheckWritable(_In_ PBTRFS_VCB Vcb);
NTSTATUS BtrfsStartTransaction(_In_ PBTRFS_VCB Vcb);
NTSTATUS BtrfsCommit(_In_ PBTRFS_VCB Vcb);
VOID BtrfsAbortTransaction(_In_ PBTRFS_VCB Vcb);
VOID BtrfsQueueCommit(_In_ PBTRFS_VCB Vcb);
NTSTATUS BtrfsInitializeTransactions(_Inout_ PBTRFS_VCB Vcb);
VOID BtrfsUninitializeTransactions(_Inout_ PBTRFS_VCB Vcb);
NTSTATUS BtrfsFlushVolume(_In_ PBTRFS_VCB Vcb);
NTSTATUS BtrfsFinishChange(_In_ PBTRFS_VCB Vcb, _In_ NTSTATUS Status, _In_ ULONG Sequence);
NTSTATUS BtrfsWriteLogical(_In_ PBTRFS_VCB Vcb, _In_ ULONGLONG Logical, _In_reads_bytes_(Length) const VOID *Buffer, _In_ ULONG Length);

/* modify.c */
NTSTATUS BtrfsWriteInode(_In_ PBTRFS_VCB Vcb, _In_ const BTRFS_ROOT *Root, _In_ ULONGLONG Inode, _In_ const BTRFS_INODE_ITEM *Item);
NTSTATUS BtrfsCreateEntry(_In_ PBTRFS_VCB Vcb, _In_ const BTRFS_ROOT *Root, _In_ ULONGLONG Directory, _In_ PCUNICODE_STRING Name, _In_ BOOLEAN IsDirectory, _Out_ PULONGLONG Inode);
NTSTATUS BtrfsUnlinkEntry(_In_ PBTRFS_VCB Vcb, _In_ const BTRFS_ROOT *Root, _In_ ULONGLONG Directory, _In_ PCUNICODE_STRING Name, _In_ ULONGLONG Inode);
NTSTATUS BtrfsLinkEntry(_In_ PBTRFS_VCB Vcb, _In_ const BTRFS_ROOT *Root, _In_ ULONGLONG Directory, _In_ PCUNICODE_STRING Name, _In_ ULONGLONG Inode, _In_ UCHAR Type);
NTSTATUS BtrfsWriteFileData(_In_ PBTRFS_FCB Fcb, _In_ ULONGLONG Offset, _In_reads_bytes_(Length) const UCHAR *Buffer, _In_ ULONG Length);
NTSTATUS BtrfsTruncate(_In_ PBTRFS_FCB Fcb, _In_ ULONGLONG NewSize);
NTSTATUS BtrfsRemoveInode(_In_ PBTRFS_VCB Vcb, _In_ const BTRFS_ROOT *Root, _In_ ULONGLONG Inode);
NTSTATUS BtrfsDeleteLink(_In_ PBTRFS_VCB Vcb, _In_ const BTRFS_ROOT *Root, _In_ ULONGLONG Directory, _In_ PCUNICODE_STRING Name, _In_ ULONGLONG Inode, _Inout_ PBTRFS_INODE_ITEM Item);
BTRFS_TIME BtrfsNow(VOID);
UCHAR BtrfsFileType(_In_ ULONG Mode);
NTSTATUS BtrfsIsWithinDirectory(_In_ PBTRFS_VCB Vcb, _In_ const BTRFS_ROOT *Root, _In_ ULONGLONG Ancestor, _In_ ULONGLONG Directory, _Out_ PBOOLEAN Within);
BTRFS_TIME BtrfsFromNtTime(_In_ LARGE_INTEGER Time);

/* fastio.c */
VOID BtrfsInitializeFastIo(_Out_ PFAST_IO_DISPATCH Dispatch, _Out_ PCACHE_MANAGER_CALLBACKS Callbacks);
