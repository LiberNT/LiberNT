/*
 * PROJECT:     LiberNT ext4 file system driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Driver structures and internal interfaces
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <ntifs.h>
#include <ntdddisk.h>
#include <mountdev.h>
#include <pseh/pseh2.h>

/* The 32-bit wdm.h defines SLIST_ENTRY as a macro, and so does lwext4's queue.h for its own lists */
#pragma push_macro("SLIST_ENTRY")
#undef SLIST_ENTRY
#include "core/e4core.h"
#undef SLIST_ENTRY
#pragma pop_macro("SLIST_ENTRY")

#define NDEBUG
#include <debug.h>

#define TAG_EXT4                '4txE'
#define TAG_EXT4_CORE           'c4xE'
#define TAG_EXT4_FCB            'f4xE'
#define TAG_EXT4_IO             'i4xE'

#define EXT4_VCB_SIGNATURE      'V4XE'
#define EXT4_FCB_SIGNATURE      'F4XE'
#define EXT4_CCB_SIGNATURE      'C4XE'

#define EXT4_IO_CHUNK           0x100000
#define EXT4_CACHE_BLOCKS       2048
#define EXT4_FCB_BUCKETS        256
#define EXT4_FLUSH_SECONDS      5
#define EXT4_NAME_BYTES         255
#define EXT4_DIR_BATCH          32

#define EXT4_WRITE_ACCESS (FILE_WRITE_DATA | FILE_APPEND_DATA | FILE_WRITE_EA | FILE_WRITE_ATTRIBUTES | \
                           DELETE | WRITE_DAC | WRITE_OWNER | FILE_DELETE_CHILD | ACCESS_SYSTEM_SECURITY)
#define EXT4_READ_ACCESS  (FILE_GENERIC_READ | FILE_GENERIC_EXECUTE)

struct _EXT4_FCB;

typedef struct _EXT4_VCB
{
    ULONG Signature;
    PDEVICE_OBJECT VolumeDeviceObject;
    PDEVICE_OBJECT TargetDeviceObject;
    PVPB Vpb;
    /* Names, handles and the FCB table: exclusive to change them, shared to use an open file */
    ERESOURCE Resource;
    /* Everything under Core: the lwext4 state has no locking of its own. Innermost lock of the driver */
    ERESOURCE CoreLock;
    LIST_ENTRY FcbTable[EXT4_FCB_BUCKETS];
    LIST_ENTRY VcbLinks;
    ULONG OpenHandles;
    LONG References;
    BOOLEAN Locked;
    BOOLEAN Dismounted;
    BOOLEAN Writable;
    BOOLEAN CoreMounted;
    PFILE_OBJECT LockFileObject;
    struct _EXT4_FCB *RootFcb;
    struct _EXT4_FCB *VolumeFcb;
    E4_VOLUME Core;
    struct ext4_blockdev_iface DeviceInterface;
    PUCHAR SectorBuffer;
    NTSTATUS DeviceStatus;
    ULONG DeviceSectorSize;
    ULONG BlockSize;
    ULONGLONG PartitionLength;
    /* The size a shrink was prepared for, in blocks; 0 when none is */
    ULONGLONG ShrinkBlocks;
    PNOTIFY_SYNC NotifySync;
    LIST_ENTRY DirNotifyList;
    KTIMER FlushTimer;
    KDPC FlushDpc;
    WORK_QUEUE_ITEM FlushWork;
    LONG FlushQueued;
    LONG CoreDirty;
    KEVENT FlushIdle;
} EXT4_VCB, *PEXT4_VCB;

typedef struct _EXT4_FCB
{
    FSRTL_ADVANCED_FCB_HEADER Header;
    FAST_MUTEX HeaderMutex;
    SECTION_OBJECT_POINTERS SectionObjectPointers;
    ERESOURCE MainResource;
    ERESOURCE PagingIoResource;
    ULONG Signature;
    PEXT4_VCB Vcb;
    LIST_ENTRY TableLinks;
    LONG References;
    ULONG OpenHandles;
    ULONG Inode;
    /* Mode, owner, link count and times as last read from or written to the inode; guarded by MainResource */
    E4_ATTR Attr;
    ULONG ReparseTag;
    BOOLEAN Directory;
    BOOLEAN Volume;
    BOOLEAN Symlink;
    BOOLEAN DeletePending;
    /* The last name of the inode is gone; the inode is released when the FCB goes away */
    BOOLEAN Orphaned;
    BOOLEAN TimesDirty;
    /* The link that a pending delete removes */
    ULONG DeleteParent;
    ULONG DeleteNameLength;
    CHAR DeleteName[EXT4_NAME_BYTES];
    SHARE_ACCESS ShareAccess;
    FILE_LOCK FileLock;
} EXT4_FCB, *PEXT4_FCB;

typedef struct _EXT4_CCB
{
    ULONG Signature;
    UNICODE_STRING FullName;
    /* Holds FullName once a rename replaced the name the handle was opened with */
    PWCHAR AllocatedName;
    ULONG EaIndex;
    /* The directory and the on-disk name this handle was opened through; ParentInode is 0 for the root and for an open by file id */
    ULONG ParentInode;
    ULONG NameLength;
    CHAR Name[EXT4_NAME_BYTES];
    BOOLEAN DeleteOnClose;
    BOOLEAN SearchStarted;
    BOOLEAN PatternHasWildcards;
    BOOLEAN MatchAll;
    UNICODE_STRING SearchPattern;
    ULONGLONG DirOffset;
} EXT4_CCB, *PEXT4_CCB;

typedef struct _EXT4_GLOBAL
{
    PDRIVER_OBJECT DriverObject;
    PDEVICE_OBJECT FsDeviceObject;
    ERESOURCE Resource;
    LIST_ENTRY VcbList;
    FAST_IO_DISPATCH FastIoDispatch;
    CACHE_MANAGER_CALLBACKS CacheCallbacks;
} EXT4_GLOBAL;

extern EXT4_GLOBAL Ext4Global;

/* driver.c */
NTSTATUS Ext4CompleteRequest(_In_ PIRP Irp, _In_ NTSTATUS Status, _In_ ULONG_PTR Information);
BOOLEAN Ext4IsFsDevice(_In_ PDEVICE_OBJECT DeviceObject);
PEXT4_VCB Ext4DeviceVcb(_In_ PDEVICE_OBJECT DeviceObject);

/* device.c */
NTSTATUS Ext4ReadDevice(_In_ PDEVICE_OBJECT Device, _In_ ULONGLONG Offset, _Out_writes_bytes_(Length) PVOID Buffer, _In_ ULONG Length, _In_ BOOLEAN OverrideVerify);
NTSTATUS Ext4WriteDevice(_In_ PDEVICE_OBJECT Device, _In_ ULONGLONG Offset, _In_reads_bytes_(Length) const VOID *Buffer, _In_ ULONG Length);
NTSTATUS Ext4FlushDevice(_In_ PDEVICE_OBJECT Device);
NTSTATUS Ext4DeviceControl(_In_ PDEVICE_OBJECT Device, _In_ ULONG Code, _In_reads_bytes_opt_(InputLength) PVOID Input, _In_ ULONG InputLength, _Out_writes_bytes_opt_(OutputLength) PVOID Output, _In_ ULONG OutputLength, _In_ BOOLEAN OverrideVerify);

/* volume.c */
VOID Ext4ReferenceVcb(_In_ PEXT4_VCB Vcb);
VOID Ext4DereferenceVcb(_In_ PEXT4_VCB Vcb);
NTSTATUS Ext4MountVolume(_In_ PDEVICE_OBJECT TargetDevice, _In_ PVPB Vpb);
NTSTATUS Ext4VerifyVolume(_In_ PIRP Irp);
VOID Ext4LockCore(_In_ PEXT4_VCB Vcb);
VOID Ext4UnlockCore(_In_ PEXT4_VCB Vcb);
NTSTATUS Ext4Status(_In_ PEXT4_VCB Vcb, _In_ int Error);
NTSTATUS Ext4FlushVolume(_In_ PEXT4_VCB Vcb);
VOID Ext4StopCore(_In_ PEXT4_VCB Vcb);
VOID Ext4MarkCoreDirty(_In_ PEXT4_VCB Vcb);
VOID Ext4SetVolumeLabel(_In_ PEXT4_VCB Vcb, _Inout_ PVPB Vpb);

/* fcb.c */
NTSTATUS Ext4Utf8ToUnicode(_In_reads_bytes_(Length) const CHAR *Utf8, _In_ ULONG Length, _Out_ PUNICODE_STRING Name);
NTSTATUS Ext4UnicodeToUtf8(_In_ PCUNICODE_STRING Name, _Out_writes_bytes_(BufferSize) PCHAR Buffer, _In_ ULONG BufferSize, _Out_ PULONG Length);
NTSTATUS Ext4LookupName(_In_ PEXT4_VCB Vcb, _In_ ULONG Directory, _In_ PCUNICODE_STRING Name, _In_ BOOLEAN IgnoreCase, _Out_ PULONG Inode, _Out_ PUCHAR Type, _Out_writes_bytes_(EXT4_NAME_BYTES) PCHAR DiskName, _Out_ PULONG DiskNameLength);
NTSTATUS Ext4GetFcb(_In_ PEXT4_VCB Vcb, _In_ ULONG Inode, _Out_ PEXT4_FCB *Fcb);
PEXT4_FCB Ext4FindFcb(_In_ PEXT4_VCB Vcb, _In_ ULONG Inode);
PEXT4_FCB Ext4CreateVolumeFcb(_In_ PEXT4_VCB Vcb);
VOID Ext4ReferenceFcb(_In_ PEXT4_FCB Fcb);
VOID Ext4DereferenceFcb(_In_ PEXT4_FCB Fcb);
NTSTATUS Ext4RefreshFcb(_In_ PEXT4_FCB Fcb);
ULONG Ext4FileAttributes(_In_ const E4_ATTR *Attr, _In_reads_bytes_opt_(NameLength) const CHAR *Name, _In_ ULONG NameLength, _Out_opt_ PULONG ReparseTag);
LARGE_INTEGER Ext4TimeToNt(_In_ const E4_TIME *Time);
VOID Ext4TimeFromNt(_In_ LARGE_INTEGER Time, _Out_ E4_TIME *Result);
VOID Ext4CurrentTime(_Out_ E4_TIME *Time);
NTSTATUS Ext4WriteTimes(_In_ PEXT4_FCB Fcb);

/* create.c */
VOID Ext4NotifyChange(_In_ PEXT4_VCB Vcb, _In_ PCUNICODE_STRING FullName, _In_ ULONG Filter, _In_ ULONG Action);

/* read.c */
NTSTATUS Ext4ReadFileData(_In_ PEXT4_FCB Fcb, _In_ ULONGLONG Offset, _Out_writes_bytes_(Length) PUCHAR Buffer, _In_ ULONG Length);

/* write.c */
NTSTATUS Ext4WriteFileData(_In_ PEXT4_FCB Fcb, _In_ ULONGLONG Offset, _In_reads_bytes_(Length) const UCHAR *Buffer, _In_ ULONG Length, _In_ ULONGLONG FileSize);
NTSTATUS Ext4SetFileSize(_In_ PEXT4_FCB Fcb, _In_opt_ PFILE_OBJECT FileObject, _In_ ULONGLONG Size);
NTSTATUS Ext4AllocateRange(_In_ PEXT4_FCB Fcb, _In_ ULONGLONG Offset, _In_ ULONGLONG Length, _In_ ULONGLONG FileSize);

/* fileinfo.c */
NTSTATUS Ext4MarkDeletePending(_In_ PEXT4_FCB Fcb, _In_ PEXT4_CCB Ccb, _In_ PFILE_OBJECT FileObject, _In_ BOOLEAN Delete);
VOID Ext4FillStatLx(_In_ PEXT4_FCB Fcb, _Out_ PFILE_STAT_LX_INFORMATION Stat);

/* ea.c */
NTSTATUS Ext4ApplyEaList(_In_ PEXT4_FCB Fcb, _In_ PEXT4_CCB Ccb, _In_reads_bytes_(Length) PFILE_FULL_EA_INFORMATION List, _In_ ULONG Length);
ULONG Ext4EaSize(_In_ PEXT4_FCB Fcb);

/* close.c */
NTSTATUS Ext4UnlinkName(_In_ PEXT4_FCB Fcb, _In_ ULONG ParentInode, _In_reads_bytes_(NameLength) const CHAR *Name, _In_ ULONG NameLength);

/* fsctl.c */
NTSTATUS Ext4DismountVolume(_In_ PEXT4_VCB Vcb);

/* fastio.c */
VOID Ext4InitializeFastIo(_Out_ PFAST_IO_DISPATCH Dispatch, _Out_ PCACHE_MANAGER_CALLBACKS Callbacks);

DRIVER_DISPATCH Ext4DispatchCreate;
DRIVER_DISPATCH Ext4DispatchCleanup;
DRIVER_DISPATCH Ext4DispatchClose;
DRIVER_DISPATCH Ext4DispatchRead;
DRIVER_DISPATCH Ext4DispatchWrite;
DRIVER_DISPATCH Ext4DispatchQueryInformation;
DRIVER_DISPATCH Ext4DispatchSetInformation;
DRIVER_DISPATCH Ext4DispatchQueryEa;
DRIVER_DISPATCH Ext4DispatchSetEa;
DRIVER_DISPATCH Ext4DispatchQueryVolumeInformation;
DRIVER_DISPATCH Ext4DispatchSetVolumeInformation;
DRIVER_DISPATCH Ext4DispatchDirectoryControl;
DRIVER_DISPATCH Ext4DispatchFileSystemControl;
DRIVER_DISPATCH Ext4DispatchDeviceControl;
DRIVER_DISPATCH Ext4DispatchLockControl;
DRIVER_DISPATCH Ext4DispatchFlushBuffers;
DRIVER_DISPATCH Ext4DispatchQuerySecurity;
DRIVER_DISPATCH Ext4DispatchSetSecurity;
DRIVER_DISPATCH Ext4DispatchPnp;
DRIVER_DISPATCH Ext4DispatchShutdown;
DRIVER_DISPATCH Ext4DispatchUnsupported;
