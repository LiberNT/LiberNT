/*
 * PROJECT:     LiberNT Hyper-V Integration Services Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Guest shutdown, time synchronization, heartbeat and data exchange
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntifs.h>
#include <ntpoapi.h>
#include <ntstrsafe.h>
#include <vmbusapi.h>

#define NDEBUG
#include <debug.h>

#define VMIC_TAG          'ciVL'
#define VMIC_RING_SIZE    (32 * 1024)
#define VMIC_PACKET_MAX   (16 * 1024)
#define VMIC_STRING_MAX   256

#define PIPE_MESSAGE_DATA 1
#define PIPE_HEADER_SIZE  8

#define IC_MSG_NEGOTIATE  0
#define IC_MSG_HEARTBEAT  1
#define IC_MSG_KVP        2
#define IC_MSG_SHUTDOWN   3
#define IC_MSG_TIMESYNC   4
#define IC_MSG_VSS        5

#define IC_FLAG_TRANSACTION 0x01
#define IC_FLAG_REQUEST     0x02
#define IC_FLAG_RESPONSE    0x04

#define IC_HEADER_SIZE 20

#define IC_STATUS_SUCCESS        0x00000000
#define IC_STATUS_FAIL           0x80004005
#define IC_STATUS_NOT_SUPPORTED  0x80070032
#define IC_STATUS_NO_MORE_ITEMS  0x80070103
#define IC_STATUS_NOT_FOUND      0x80041002

#define SHUTDOWN_FLAG_FORCE      0x01
#define SHUTDOWN_FLAG_RESTART    0x02
#define SHUTDOWN_FLAG_HIBERNATE  0x04

#define TIMESYNC_FLAG_SYNC    0x01
#define TIMESYNC_FLAG_SAMPLE  0x02

#define VSS_OP_CREATE           0
#define VSS_OP_DELETE           1
#define VSS_OP_CHECK_HOT_BACKUP 2
#define VSS_OP_GET_DM_INFO      3
#define VSS_OP_BACKUP_COMPLETE  4
#define VSS_OP_FREEZE           5
#define VSS_OP_THAW             6
#define VSS_HOT_BACKUP_NO_AUTO_RECOVERY 0x00000005
#define VMIC_MAX_FROZEN_VOLUMES 32
#define VMIC_FREEZE_WAIT_SECONDS 15

#define KVP_OP_GET              0
#define KVP_OP_SET              1
#define KVP_OP_DELETE           2
#define KVP_OP_ENUMERATE        3
#define KVP_OP_GET_IP_INFO      4
#define KVP_OP_SET_IP_INFO      5
#define KVP_POOL_AUTO           2
#define KVP_REG_SZ              1
#define KVP_KEY_BYTES           512
#define KVP_VALUE_BYTES         2048
#define KVP_ENUMERATE_SIZE      (4 + 12 + KVP_KEY_BYTES + KVP_VALUE_BYTES)
#define KVP_IP_ADAPTER_CHARS    128
#define KVP_IP_ADDRESS_CHARS    1024
#define KVP_IP_GATEWAY_CHARS    512
#define KVP_IP_INFO_SIZE        (2 + 2 * KVP_IP_ADAPTER_CHARS + 2 + 2 * (3 * KVP_IP_ADDRESS_CHARS + KVP_IP_GATEWAY_CHARS))
#define KVP_IP_BINARY_MAX_V4    64
#define KVP_IP_BINARY_MAX_GW    5
#define KVP_IP_BINARY_V4        284
#define KVP_IP_BINARY_V4_SUBNET 1564
#define KVP_IP_BINARY_V4_GW     2076
#define KVP_IP_BINARY_V4_DNS    2176
#define KVP_IP_BINARY_ORIGINS   3456
#define KVP_IP_BINARY_SIZE      3712
#define KVP_IP_ORIGIN_OTHER     1
#define KVP_IP_ORIGIN_STATIC    2

#define HV_CPUID_FEATURES               0x40000003
#define HV_FEATURE_TIME_REF_COUNT       0x00000002
#define HV_X64_MSR_TIME_REF_COUNT       0x40000020

#define VMIC_TIME_STEP_THRESHOLD (2LL * 10000000)

typedef enum _VMIC_KIND
{
    VmicUnknown,
    VmicShutdown,
    VmicTimeSync,
    VmicHeartbeat,
    VmicKvp,
    VmicVss
} VMIC_KIND;

typedef struct _VMIC_VERSION
{
    USHORT Major;
    USHORT Minor;
} VMIC_VERSION;

typedef struct _VMIC_FROZEN_VOLUME
{
    HANDLE Handle;
    PFILE_OBJECT FileObject;
    PKTHREAD Thread;
    KEVENT HeldEvent;
    NTSTATUS HoldStatus;
} VMIC_FROZEN_VOLUME, *PVMIC_FROZEN_VOLUME;

typedef enum _VMIC_VOLUME_KIND
{
    VmicVolumeSkip,
    VmicVolumeFreeze,
    VmicVolumeUnsupported
} VMIC_VOLUME_KIND;

typedef struct _VMIC_EXTENSION
{
    PDEVICE_OBJECT Self;
    PDEVICE_OBJECT LowerDevice;
    PDEVICE_OBJECT Pdo;
    PVMBUS_CHANNEL Channel;
    BOOLEAN Open;
    VMIC_KIND Kind;
    USHORT MessageMajor;
    PIO_WORKITEM WorkItem;
    LONG WorkQueued;
    KEVENT WorkIdle;
    BOOLEAN TimeSynced;
    PVMIC_FROZEN_VOLUME FrozenVolumes;
    ULONG FrozenCount;
    UCHAR Packet[VMIC_PACKET_MAX];
} VMIC_EXTENSION, *PVMIC_EXTENSION;

typedef struct _VMIC_IP_INFO
{
    WCHAR Addresses[VMIC_STRING_MAX];
    WCHAR Subnets[VMIC_STRING_MAX];
    WCHAR Gateways[VMIC_STRING_MAX];
    WCHAR DnsServers[VMIC_STRING_MAX];
    BOOLEAN Dhcp;
    BOOLEAN Valid;
} VMIC_IP_INFO, *PVMIC_IP_INFO;

static const GUID VmicShutdownGuid =
    { 0x0e0b6031, 0x5213, 0x4934, { 0x81, 0x8b, 0x38, 0xd9, 0x0c, 0xed, 0x39, 0xdb } };
static const GUID VmicTimeSyncGuid =
    { 0x9527e630, 0xd0ae, 0x497b, { 0xad, 0xce, 0xe8, 0x0a, 0xb0, 0x17, 0x5c, 0xaf } };
static const GUID VmicHeartbeatGuid =
    { 0x57164f39, 0x9115, 0x4e78, { 0xab, 0x55, 0x38, 0x2f, 0x3b, 0xd5, 0x42, 0x2d } };
static const GUID VmicKvpGuid =
    { 0xa9a0f4e7, 0x5a45, 0x4d96, { 0xb8, 0x27, 0x8a, 0x84, 0x1e, 0x8c, 0x03, 0xe6 } };
static const GUID VmicVolumeInterfaceGuid =
    { 0x53f5630d, 0xb6bf, 0x11d0, { 0x94, 0xf2, 0x00, 0xa0, 0xc9, 0x1e, 0xfb, 0x8b } };
static const GUID VmicVssGuid =
    { 0x35fa2e29, 0xea23, 0x4236, { 0x96, 0xae, 0x3a, 0x6e, 0xba, 0xcb, 0xa4, 0x40 } };

static const VMIC_VERSION VmicFrameworkVersions[] = { { 3, 0 }, { 1, 0 } };
static const VMIC_VERSION VmicShutdownVersions[] = { { 3, 2 }, { 3, 1 }, { 3, 0 }, { 1, 0 } };
static const VMIC_VERSION VmicTimeSyncVersions[] = { { 4, 0 }, { 3, 0 }, { 1, 0 } };
static const VMIC_VERSION VmicHeartbeatVersions[] = { { 3, 0 }, { 1, 0 } };
static const VMIC_VERSION VmicKvpVersions[] = { { 5, 0 }, { 4, 0 }, { 3, 0 } };
static const VMIC_VERSION VmicVssVersions[] = { { 7, 0 }, { 6, 0 }, { 5, 0 }, { 4, 0 } };

static
USHORT
VmicGet16(
    _In_ const UCHAR *Buffer,
    _In_ ULONG Offset)
{
    USHORT Value;

    RtlCopyMemory(&Value, Buffer + Offset, sizeof(Value));
    return Value;
}

static
ULONG
VmicGet32(
    _In_ const UCHAR *Buffer,
    _In_ ULONG Offset)
{
    ULONG Value;

    RtlCopyMemory(&Value, Buffer + Offset, sizeof(Value));
    return Value;
}

static
ULONG64
VmicGet64(
    _In_ const UCHAR *Buffer,
    _In_ ULONG Offset)
{
    ULONG64 Value;

    RtlCopyMemory(&Value, Buffer + Offset, sizeof(Value));
    return Value;
}

static
VOID
VmicPut16(
    _Out_ PUCHAR Buffer,
    _In_ ULONG Offset,
    _In_ USHORT Value)
{
    RtlCopyMemory(Buffer + Offset, &Value, sizeof(Value));
}

static
VOID
VmicPut32(
    _Out_ PUCHAR Buffer,
    _In_ ULONG Offset,
    _In_ ULONG Value)
{
    RtlCopyMemory(Buffer + Offset, &Value, sizeof(Value));
}

static
VOID
VmicPut64(
    _Out_ PUCHAR Buffer,
    _In_ ULONG Offset,
    _In_ ULONG64 Value)
{
    RtlCopyMemory(Buffer + Offset, &Value, sizeof(Value));
}

static
BOOLEAN
VmicSelectVersion(
    _In_reads_(OfferedCount) const UCHAR *Offered,
    _In_ ULONG OfferedCount,
    _In_reads_(SupportedCount) const VMIC_VERSION *Supported,
    _In_ ULONG SupportedCount,
    _Out_ VMIC_VERSION *Selected)
{
    ULONG Index;
    ULONG Candidate;

    for (Index = 0; Index < SupportedCount; Index++)
    {
        for (Candidate = 0; Candidate < OfferedCount; Candidate++)
        {
            if (VmicGet16(Offered, Candidate * 4) == Supported[Index].Major &&
                VmicGet16(Offered, Candidate * 4 + 2) == Supported[Index].Minor)
            {
                *Selected = Supported[Index];
                return TRUE;
            }
        }
    }

    return FALSE;
}

static
VOID
VmicNegotiate(
    _Inout_ PVMIC_EXTENSION Extension,
    _Inout_updates_bytes_(Length) PUCHAR Message,
    _In_ ULONG Length)
{
    const VMIC_VERSION *Versions;
    VMIC_VERSION Framework;
    VMIC_VERSION Selected;
    ULONG VersionCount;
    ULONG FrameworkCount;
    ULONG MessageCount;
    PUCHAR Body = Message + IC_HEADER_SIZE;

    switch (Extension->Kind)
    {
        case VmicShutdown:
            Versions = VmicShutdownVersions;
            VersionCount = RTL_NUMBER_OF(VmicShutdownVersions);
            break;
        case VmicTimeSync:
            Versions = VmicTimeSyncVersions;
            VersionCount = RTL_NUMBER_OF(VmicTimeSyncVersions);
            break;
        case VmicHeartbeat:
            Versions = VmicHeartbeatVersions;
            VersionCount = RTL_NUMBER_OF(VmicHeartbeatVersions);
            break;
        case VmicVss:
            Versions = VmicVssVersions;
            VersionCount = RTL_NUMBER_OF(VmicVssVersions);
            break;
        default:
            Versions = VmicKvpVersions;
            VersionCount = RTL_NUMBER_OF(VmicKvpVersions);
            break;
    }

    if (Length < IC_HEADER_SIZE + 8)
        return;

    FrameworkCount = VmicGet16(Body, 0);
    MessageCount = VmicGet16(Body, 2);
    if (Length < IC_HEADER_SIZE + 8 + 4 * (FrameworkCount + MessageCount) ||
        !VmicSelectVersion(Body + 8, FrameworkCount, VmicFrameworkVersions,
                           RTL_NUMBER_OF(VmicFrameworkVersions), &Framework) ||
        !VmicSelectVersion(Body + 8 + 4 * FrameworkCount, MessageCount, Versions, VersionCount, &Selected))
    {
        DPRINT1("VMIC: no common version for IC %d\n", Extension->Kind);
        VmicPut16(Body, 0, 0);
        VmicPut16(Body, 2, 0);
        VmicPut32(Message, 12, IC_STATUS_FAIL);
        return;
    }

    VmicPut16(Body, 0, 1);
    VmicPut16(Body, 2, 1);
    VmicPut16(Body, 8, Framework.Major);
    VmicPut16(Body, 10, Framework.Minor);
    VmicPut16(Body, 12, Selected.Major);
    VmicPut16(Body, 14, Selected.Minor);
    VmicPut16(Message, 10, 16);
    Extension->MessageMajor = Selected.Major;
}

static
VOID
NTAPI
VmicHoldThread(
    _In_ PVOID Context)
{
    PVMIC_FROZEN_VOLUME Volume = Context;
    PKEVENT HeldEvent = &Volume->HeldEvent;
    IO_STATUS_BLOCK IoStatusBlock;

    Volume->HoldStatus = ZwDeviceIoControlFile(Volume->Handle, NULL, NULL, NULL, &IoStatusBlock,
                                               IOCTL_VOLSNAP_FLUSH_AND_HOLD_WRITES,
                                               &HeldEvent, sizeof(HeldEvent), NULL, 0);
    PsTerminateSystemThread(STATUS_SUCCESS);
}

static
VMIC_VOLUME_KIND
VmicOpenVolume(
    _In_ PUNICODE_STRING Name,
    _Out_ PHANDLE Handle)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    FILE_FS_DEVICE_INFORMATION DeviceInfo;
    UCHAR Buffer[sizeof(FILE_FS_ATTRIBUTE_INFORMATION) + 32 * sizeof(WCHAR)];
    PFILE_FS_ATTRIBUTE_INFORMATION AttributeInfo = (PFILE_FS_ATTRIBUTE_INFORMATION)Buffer;
    UNICODE_STRING FileSystem;
    UNICODE_STRING Ntfs = RTL_CONSTANT_STRING(L"NTFS");
    UNICODE_STRING Fat = RTL_CONSTANT_STRING(L"FAT");
    UNICODE_STRING Fat32 = RTL_CONSTANT_STRING(L"FAT32");
    UNICODE_STRING Raw = RTL_CONSTANT_STRING(L"RAW");
    NTSTATUS Status;

    *Handle = NULL;
    InitializeObjectAttributes(&ObjectAttributes, Name, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = ZwOpenFile(Handle, SYNCHRONIZE | FILE_READ_DATA | FILE_READ_ATTRIBUTES, &ObjectAttributes, &IoStatusBlock,
                        FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_SYNCHRONOUS_IO_NONALERT);
    if (!NT_SUCCESS(Status))
    {
        *Handle = NULL;
        return VmicVolumeSkip;
    }

    Status = ZwQueryVolumeInformationFile(*Handle, &IoStatusBlock, &DeviceInfo, sizeof(DeviceInfo),
                                          FileFsDeviceInformation);
    if (!NT_SUCCESS(Status) ||
        DeviceInfo.DeviceType != FILE_DEVICE_DISK ||
        (DeviceInfo.Characteristics & (FILE_REMOVABLE_MEDIA | FILE_READ_ONLY_DEVICE | FILE_REMOTE_DEVICE)))
    {
        ZwClose(*Handle);
        *Handle = NULL;
        return VmicVolumeSkip;
    }

    Status = ZwQueryVolumeInformationFile(*Handle, &IoStatusBlock, AttributeInfo, sizeof(Buffer),
                                          FileFsAttributeInformation);
    if (NT_SUCCESS(Status))
    {
        FileSystem.Buffer = AttributeInfo->FileSystemName;
        FileSystem.Length = (USHORT)min(AttributeInfo->FileSystemNameLength, 32 * sizeof(WCHAR));
        FileSystem.MaximumLength = FileSystem.Length;

        if (RtlEqualUnicodeString(&FileSystem, &Ntfs, TRUE) ||
            RtlEqualUnicodeString(&FileSystem, &Fat, TRUE) ||
            RtlEqualUnicodeString(&FileSystem, &Fat32, TRUE))
        {
            return VmicVolumeFreeze;
        }

        if (RtlEqualUnicodeString(&FileSystem, &Raw, TRUE))
        {
            ZwClose(*Handle);
            *Handle = NULL;
            return VmicVolumeSkip;
        }
    }

    DPRINT1("VMIC: cannot freeze the file system of %wZ\n", Name);
    ZwClose(*Handle);
    *Handle = NULL;
    return VmicVolumeUnsupported;
}

static
VOID
VmicReleaseVolume(
    _In_ PFILE_OBJECT FileObject)
{
    PDEVICE_OBJECT Device;
    IO_STATUS_BLOCK IoStatusBlock;
    KEVENT Event;
    PIRP Irp;

    Device = IoGetAttachedDeviceReference(FileObject->DeviceObject);
    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp = IoBuildDeviceIoControlRequest(IOCTL_VOLSNAP_RELEASE_WRITES, Device, NULL, 0, NULL, 0,
                                        FALSE, &Event, &IoStatusBlock);
    if (Irp != NULL && IoCallDriver(Device, Irp) == STATUS_PENDING)
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
    ObDereferenceObject(Device);
}

static
VOID
VmicCloseFrozenVolume(
    _Inout_ PVMIC_FROZEN_VOLUME Volume)
{
    if (Volume->Thread != NULL)
        ObDereferenceObject(Volume->Thread);
    if (Volume->FileObject != NULL)
        ObDereferenceObject(Volume->FileObject);
    if (Volume->Handle != NULL)
        ZwClose(Volume->Handle);
    RtlZeroMemory(Volume, sizeof(*Volume));
}

static
BOOLEAN
VmicThawVolumes(
    _Inout_ PVMIC_EXTENSION Extension)
{
    PVMIC_FROZEN_VOLUME Volume;
    BOOLEAN Success = TRUE;
    ULONG Index;

    for (Index = 0; Index < Extension->FrozenCount; Index++)
    {
        Volume = &Extension->FrozenVolumes[Index];
        VmicReleaseVolume(Volume->FileObject);
        KeWaitForSingleObject(Volume->Thread, Executive, KernelMode, FALSE, NULL);
        if (!NT_SUCCESS(Volume->HoldStatus))
        {
            DPRINT1("VMIC: writes on frozen volume %lu were released early (0x%lx)\n", Index, Volume->HoldStatus);
            Success = FALSE;
        }
        VmicCloseFrozenVolume(Volume);
    }

    if (Extension->FrozenVolumes != NULL)
        ExFreePoolWithTag(Extension->FrozenVolumes, VMIC_TAG);
    Extension->FrozenVolumes = NULL;
    Extension->FrozenCount = 0;
    return Success;
}

static
BOOLEAN
VmicFreezeVolumes(
    _Inout_ PVMIC_EXTENSION Extension)
{
    PVMIC_FROZEN_VOLUME Volume;
    VMIC_VOLUME_KIND Kind;
    UNICODE_STRING Name;
    KWAIT_BLOCK WaitBlocks[2];
    PVOID Objects[2];
    LARGE_INTEGER Timeout;
    HANDLE Handle;
    HANDLE ThreadHandle;
    PWSTR Links = NULL;
    PWSTR Link;
    BOOLEAN Success = TRUE;
    NTSTATUS Status;

    VmicThawVolumes(Extension);

    Extension->FrozenVolumes = ExAllocatePoolZero(NonPagedPool,
                                                  sizeof(VMIC_FROZEN_VOLUME) * VMIC_MAX_FROZEN_VOLUMES,
                                                  VMIC_TAG);
    if (Extension->FrozenVolumes == NULL)
        return FALSE;

    if (!NT_SUCCESS(IoGetDeviceInterfaces(&VmicVolumeInterfaceGuid, NULL, 0, &Links)))
        return FALSE;

    for (Link = Links; *Link != UNICODE_NULL; Link += wcslen(Link) + 1)
    {
        RtlInitUnicodeString(&Name, Link);
        Kind = VmicOpenVolume(&Name, &Handle);
        if (Kind == VmicVolumeSkip)
            continue;
        if (Kind == VmicVolumeUnsupported || Extension->FrozenCount == VMIC_MAX_FROZEN_VOLUMES)
        {
            if (Handle != NULL)
                ZwClose(Handle);
            Success = FALSE;
            break;
        }

        Volume = &Extension->FrozenVolumes[Extension->FrozenCount];
        Volume->Handle = Handle;
        Volume->HoldStatus = STATUS_PENDING;
        KeInitializeEvent(&Volume->HeldEvent, NotificationEvent, FALSE);
        Status = ObReferenceObjectByHandle(Handle, 0, *IoFileObjectType, KernelMode,
                                           (PVOID *)&Volume->FileObject, NULL);
        if (NT_SUCCESS(Status))
        {
            Status = PsCreateSystemThread(&ThreadHandle, THREAD_ALL_ACCESS, NULL, NULL, NULL,
                                          VmicHoldThread, Volume);
        }
        if (NT_SUCCESS(Status))
        {
            Status = ObReferenceObjectByHandle(ThreadHandle, SYNCHRONIZE, *PsThreadType, KernelMode,
                                               (PVOID *)&Volume->Thread, NULL);
            ZwClose(ThreadHandle);
        }
        if (!NT_SUCCESS(Status))
        {
            VmicCloseFrozenVolume(Volume);
            Success = FALSE;
            break;
        }

        Extension->FrozenCount++;

        Objects[0] = &Volume->HeldEvent;
        Objects[1] = Volume->Thread;
        Timeout.QuadPart = -(LONGLONG)VMIC_FREEZE_WAIT_SECONDS * 10000000;
        Status = KeWaitForMultipleObjects(2, Objects, WaitAny, Executive, KernelMode, FALSE,
                                          &Timeout, WaitBlocks);
        if (Status == STATUS_WAIT_0)
            continue;

        if (Status == STATUS_WAIT_1)
        {
            DPRINT1("VMIC: %wZ cannot hold writes (0x%lx), not frozen\n", &Name, Volume->HoldStatus);
            Extension->FrozenCount--;
            VmicCloseFrozenVolume(Volume);
            continue;
        }

        DPRINT1("VMIC: %wZ did not freeze in time\n", &Name);
        Success = FALSE;
        break;
    }

    ExFreePool(Links);

    if (!Success)
    {
        VmicThawVolumes(Extension);
        return FALSE;
    }

    return TRUE;
}

static
VOID
VmicHandleVss(
    _Inout_ PVMIC_EXTENSION Extension,
    _Inout_updates_bytes_(Length) PUCHAR Message,
    _In_ ULONG Length)
{
    ULONG Status = IC_STATUS_SUCCESS;

    if (Length < IC_HEADER_SIZE + 8)
    {
        VmicPut32(Message, 12, IC_STATUS_FAIL);
        return;
    }

    switch (Message[IC_HEADER_SIZE])
    {
        case VSS_OP_CHECK_HOT_BACKUP:
            if (Length >= IC_HEADER_SIZE + 12)
                VmicPut32(Message, IC_HEADER_SIZE + 8, VSS_HOT_BACKUP_NO_AUTO_RECOVERY);
            break;

        case VSS_OP_GET_DM_INFO:
            if (Length >= IC_HEADER_SIZE + 12)
                VmicPut32(Message, IC_HEADER_SIZE + 8, 0);
            break;

        case VSS_OP_CREATE:
        case VSS_OP_FREEZE:
            if (!VmicFreezeVolumes(Extension))
                Status = IC_STATUS_FAIL;
            break;

        case VSS_OP_DELETE:
        case VSS_OP_THAW:
            if (!VmicThawVolumes(Extension))
                Status = IC_STATUS_FAIL;
            break;

        case VSS_OP_BACKUP_COMPLETE:
            break;

        default:
            Status = IC_STATUS_FAIL;
            break;
    }

    VmicPut32(Message, 12, Status);
}

static
VOID
VmicHandleShutdown(
    _Inout_ PVMIC_EXTENSION Extension,
    _Inout_updates_bytes_(Length) PUCHAR Message,
    _In_ ULONG Length,
    _Out_ PBOOLEAN Shutdown,
    _Out_ PBOOLEAN Restart,
    _Out_ PBOOLEAN Force)
{
    ULONG Flags;

    UNREFERENCED_PARAMETER(Extension);

    *Shutdown = FALSE;
    *Restart = FALSE;
    *Force = FALSE;

    if (Length < IC_HEADER_SIZE + 12)
    {
        VmicPut32(Message, 12, IC_STATUS_FAIL);
        return;
    }

    Flags = VmicGet32(Message, IC_HEADER_SIZE + 8);
    if (Flags & SHUTDOWN_FLAG_HIBERNATE)
    {
        VmicPut32(Message, 12, IC_STATUS_NOT_SUPPORTED);
        return;
    }

    *Shutdown = TRUE;
    *Restart = (Flags & SHUTDOWN_FLAG_RESTART) != 0;
    *Force = (Flags & SHUTDOWN_FLAG_FORCE) != 0;
}

static
BOOLEAN
VmicReferenceTimeAvailable(VOID)
{
    INT CpuInfo[4];

    __cpuid(CpuInfo, 0x40000000);
    if ((ULONG)CpuInfo[0] < HV_CPUID_FEATURES)
        return FALSE;

    __cpuid(CpuInfo, HV_CPUID_FEATURES);
    return (CpuInfo[0] & HV_FEATURE_TIME_REF_COUNT) != 0;
}

static
VOID
VmicHandleTimeSync(
    _Inout_ PVMIC_EXTENSION Extension,
    _In_reads_bytes_(Length) const UCHAR *Message,
    _In_ ULONG Length)
{
    LARGE_INTEGER HostTime;
    LARGE_INTEGER GuestTime;
    ULONG64 Reference;
    ULONG64 Now;
    UCHAR Flags;
    LONG64 Drift;
    NTSTATUS Status;

    if (Extension->MessageMajor >= 4)
    {
        if (Length < IC_HEADER_SIZE + 17)
            return;
        HostTime.QuadPart = (LONGLONG)VmicGet64(Message, IC_HEADER_SIZE);
        Reference = VmicGet64(Message, IC_HEADER_SIZE + 8);
        Flags = Message[IC_HEADER_SIZE + 16];
        if (Reference != 0 && VmicReferenceTimeAvailable())
        {
            Now = __readmsr(HV_X64_MSR_TIME_REF_COUNT);
            if (Now > Reference)
                HostTime.QuadPart += (LONGLONG)(Now - Reference);
        }
    }
    else
    {
        if (Length < IC_HEADER_SIZE + 25)
            return;
        HostTime.QuadPart = (LONGLONG)VmicGet64(Message, IC_HEADER_SIZE);
        Flags = Message[IC_HEADER_SIZE + 24];
    }

    if (HostTime.QuadPart <= 0 || (Flags & (TIMESYNC_FLAG_SYNC | TIMESYNC_FLAG_SAMPLE)) == 0)
        return;

    KeQuerySystemTime(&GuestTime);
    Drift = HostTime.QuadPart - GuestTime.QuadPart;
    if (!(Flags & TIMESYNC_FLAG_SYNC) && Extension->TimeSynced &&
        Drift < VMIC_TIME_STEP_THRESHOLD && Drift > -VMIC_TIME_STEP_THRESHOLD)
    {
        return;
    }

    if (Drift < VMIC_TIME_STEP_THRESHOLD / 2 && Drift > -VMIC_TIME_STEP_THRESHOLD / 2)
    {
        Extension->TimeSynced = TRUE;
        return;
    }

    Status = ZwSetSystemTime(&HostTime, NULL);
    if (NT_SUCCESS(Status))
        Extension->TimeSynced = TRUE;
    else
        DPRINT1("VMIC: ZwSetSystemTime failed with 0x%lx\n", Status);
}

static
NTSTATUS
VmicReadRegistryString(
    _In_ HANDLE Key,
    _In_ PCWSTR Name,
    _Out_writes_(Capacity) PWCHAR Buffer,
    _In_ ULONG Capacity,
    _In_ WCHAR Separator)
{
    UCHAR Data[sizeof(KEY_VALUE_PARTIAL_INFORMATION) + 512];
    PKEY_VALUE_PARTIAL_INFORMATION Info = (PKEY_VALUE_PARTIAL_INFORMATION)Data;
    UNICODE_STRING ValueName;
    ULONG ResultLength;
    ULONG Chars;
    ULONG Index;
    ULONG Out = 0;
    PWCHAR Source;
    NTSTATUS Status;

    Buffer[0] = UNICODE_NULL;
    RtlInitUnicodeString(&ValueName, Name);
    Status = ZwQueryValueKey(Key, &ValueName, KeyValuePartialInformation, Info, sizeof(Data) - sizeof(WCHAR),
                             &ResultLength);
    if (!NT_SUCCESS(Status))
        return Status;
    if (Info->Type != REG_SZ && Info->Type != REG_MULTI_SZ)
        return STATUS_OBJECT_TYPE_MISMATCH;

    Source = (PWCHAR)Info->Data;
    Chars = Info->DataLength / sizeof(WCHAR);
    for (Index = 0; Index < Chars && Out + 1 < Capacity; Index++)
    {
        WCHAR Char = Source[Index];

        if (Char == UNICODE_NULL || Char == L' ' || Char == L',')
        {
            if (Out != 0 && Buffer[Out - 1] != Separator)
                Buffer[Out++] = Separator;
            if (Char == UNICODE_NULL && Info->Type == REG_SZ)
                break;
            continue;
        }

        Buffer[Out++] = Char;
    }

    while (Out != 0 && Buffer[Out - 1] == Separator)
        Out--;
    Buffer[Out] = UNICODE_NULL;

    if (wcscmp(Buffer, L"0.0.0.0") == 0)
        Buffer[0] = UNICODE_NULL;

    return Buffer[0] != UNICODE_NULL ? STATUS_SUCCESS : STATUS_NOT_FOUND;
}

static
ULONG
VmicReadRegistryDword(
    _In_ HANDLE Key,
    _In_ PCWSTR Name,
    _In_ ULONG Default)
{
    UCHAR Data[sizeof(KEY_VALUE_PARTIAL_INFORMATION) + sizeof(ULONG)];
    PKEY_VALUE_PARTIAL_INFORMATION Info = (PKEY_VALUE_PARTIAL_INFORMATION)Data;
    UNICODE_STRING ValueName;
    ULONG ResultLength;
    ULONG Value;

    RtlInitUnicodeString(&ValueName, Name);
    if (!NT_SUCCESS(ZwQueryValueKey(Key, &ValueName, KeyValuePartialInformation, Info, sizeof(Data), &ResultLength)) ||
        Info->Type != REG_DWORD || Info->DataLength != sizeof(ULONG))
    {
        return Default;
    }

    RtlCopyMemory(&Value, Info->Data, sizeof(Value));
    return Value;
}

static
HANDLE
VmicOpenKey(
    _In_opt_ HANDLE Root,
    _In_ PCUNICODE_STRING Name)
{
    OBJECT_ATTRIBUTES Attributes;
    HANDLE Key;

    InitializeObjectAttributes(&Attributes, (PUNICODE_STRING)Name, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, Root, NULL);
    if (!NT_SUCCESS(ZwOpenKey(&Key, KEY_READ, &Attributes)))
        return NULL;
    return Key;
}

static
VOID
VmicQueryIpInfo(
    _Out_ PVMIC_IP_INFO IpInfo)
{
    static const UNICODE_STRING InterfacesPath =
        RTL_CONSTANT_STRING(L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters\\Interfaces");
    UCHAR Data[sizeof(KEY_BASIC_INFORMATION) + 256];
    PKEY_BASIC_INFORMATION Basic = (PKEY_BASIC_INFORMATION)Data;
    UNICODE_STRING SubkeyName;
    HANDLE Interfaces;
    HANDLE Interface;
    ULONG ResultLength;
    ULONG Index;
    BOOLEAN Dhcp;

    RtlZeroMemory(IpInfo, sizeof(*IpInfo));

    Interfaces = VmicOpenKey(NULL, &InterfacesPath);
    if (Interfaces == NULL)
        return;

    for (Index = 0; !IpInfo->Valid; Index++)
    {
        if (!NT_SUCCESS(ZwEnumerateKey(Interfaces, Index, KeyBasicInformation, Basic, sizeof(Data), &ResultLength)))
            break;

        SubkeyName.Buffer = Basic->Name;
        SubkeyName.Length = (USHORT)Basic->NameLength;
        SubkeyName.MaximumLength = (USHORT)Basic->NameLength;
        Interface = VmicOpenKey(Interfaces, &SubkeyName);
        if (Interface == NULL)
            continue;

        Dhcp = VmicReadRegistryDword(Interface, L"EnableDHCP", 1) != 0;
        if (NT_SUCCESS(VmicReadRegistryString(Interface, Dhcp ? L"DhcpIPAddress" : L"IPAddress",
                                              IpInfo->Addresses, VMIC_STRING_MAX, L';')))
        {
            IpInfo->Valid = TRUE;
            IpInfo->Dhcp = Dhcp;
            VmicReadRegistryString(Interface, Dhcp ? L"DhcpSubnetMask" : L"SubnetMask",
                                   IpInfo->Subnets, VMIC_STRING_MAX, L';');
            VmicReadRegistryString(Interface, Dhcp ? L"DhcpDefaultGateway" : L"DefaultGateway",
                                   IpInfo->Gateways, VMIC_STRING_MAX, L';');
            if (!NT_SUCCESS(VmicReadRegistryString(Interface, L"NameServer", IpInfo->DnsServers,
                                                   VMIC_STRING_MAX, L';')) && Dhcp)
            {
                VmicReadRegistryString(Interface, L"DhcpNameServer", IpInfo->DnsServers, VMIC_STRING_MAX, L';');
            }
        }

        ZwClose(Interface);
    }

    ZwClose(Interfaces);
}

static
VOID
VmicQueryHostName(
    _Out_writes_(Capacity) PWCHAR Buffer,
    _In_ ULONG Capacity)
{
    static const UNICODE_STRING ParametersPath =
        RTL_CONSTANT_STRING(L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters");
    WCHAR Domain[VMIC_STRING_MAX];
    HANDLE Key;

    Buffer[0] = UNICODE_NULL;
    Key = VmicOpenKey(NULL, &ParametersPath);
    if (Key == NULL)
        return;

    VmicReadRegistryString(Key, L"Hostname", Buffer, Capacity, L' ');
    if (Buffer[0] != UNICODE_NULL &&
        NT_SUCCESS(VmicReadRegistryString(Key, L"Domain", Domain, RTL_NUMBER_OF(Domain), L' ')))
    {
        RtlStringCchCatW(Buffer, Capacity, L".");
        RtlStringCchCatW(Buffer, Capacity, Domain);
    }

    ZwClose(Key);
}

static
ULONG
VmicWriteString(
    _Out_writes_bytes_(Capacity) PUCHAR Destination,
    _In_ ULONG Capacity,
    _In_ PCWSTR Source)
{
    SIZE_T Chars = wcslen(Source);
    ULONG Bytes;

    if (Capacity < sizeof(WCHAR))
        return 0;

    Chars = min(Chars, Capacity / sizeof(WCHAR) - 1);
    Bytes = (ULONG)(Chars * sizeof(WCHAR));
    RtlCopyMemory(Destination, Source, Bytes);
    RtlZeroMemory(Destination + Bytes, sizeof(WCHAR));
    return Bytes + sizeof(WCHAR);
}

static
BOOLEAN
VmicAutoPoolItem(
    _In_ ULONG Index,
    _Out_ PCWSTR *Key,
    _Out_writes_(Capacity) PWCHAR Value,
    _In_ ULONG Capacity)
{
    RTL_OSVERSIONINFOW Version;
    VMIC_IP_INFO IpInfo;

    Version.dwOSVersionInfoSize = sizeof(Version);
    RtlGetVersion(&Version);
    Value[0] = UNICODE_NULL;

    switch (Index)
    {
        case 0:
            *Key = L"FullyQualifiedDomainName";
            VmicQueryHostName(Value, Capacity);
            return TRUE;
        case 1:
            *Key = L"IntegrationServicesVersion";
            RtlStringCchPrintfW(Value, Capacity, L"%lu.%lu.%lu.0",
                                Version.dwMajorVersion, Version.dwMinorVersion, Version.dwBuildNumber);
            return TRUE;
        case 2:
            *Key = L"NetworkAddressIPv4";
            VmicQueryIpInfo(&IpInfo);
            RtlStringCchCopyW(Value, Capacity, IpInfo.Addresses);
            return TRUE;
        case 3:
            *Key = L"NetworkAddressIPv6";
            return TRUE;
        case 4:
            *Key = L"OSBuildNumber";
            RtlStringCchPrintfW(Value, Capacity, L"%lu", Version.dwBuildNumber);
            return TRUE;
        case 5:
            *Key = L"OSName";
            RtlStringCchCopyW(Value, Capacity, L"LiberNT");
            return TRUE;
        case 6:
            *Key = L"OSMajorVersion";
            RtlStringCchPrintfW(Value, Capacity, L"%lu", Version.dwMajorVersion);
            return TRUE;
        case 7:
            *Key = L"OSMinorVersion";
            RtlStringCchPrintfW(Value, Capacity, L"%lu", Version.dwMinorVersion);
            return TRUE;
        case 8:
            *Key = L"OSVersion";
            RtlStringCchPrintfW(Value, Capacity, L"%lu.%lu.%lu",
                                Version.dwMajorVersion, Version.dwMinorVersion, Version.dwBuildNumber);
            return TRUE;
        case 9:
            *Key = L"ProcessorArchitecture";
            RtlStringCchCopyW(Value, Capacity, L"9");
            return TRUE;
        default:
            return FALSE;
    }
}

static
ULONG
VmicParseIpv4List(
    _In_ PCWSTR Text,
    _Out_writes_bytes_(MaxCount * 4) PUCHAR Addresses,
    _In_ ULONG MaxCount)
{
    ULONG Count = 0;
    ULONG Part = 0;
    ULONG Value = 0;
    BOOLEAN Digits = FALSE;
    UCHAR Octets[4];

    for (;; Text++)
    {
        if (*Text >= L'0' && *Text <= L'9')
        {
            Value = Value * 10 + (*Text - L'0');
            Digits = Value <= 255;
            if (!Digits)
                Part = 4;
            continue;
        }

        if (Part < 4 && Digits)
            Octets[Part] = (UCHAR)Value;
        if (*Text == L'.')
        {
            Part = (Part < 4 && Digits) ? Part + 1 : 4;
        }
        else
        {
            if (Part == 3 && Digits && Count < MaxCount)
                RtlCopyMemory(Addresses + Count++ * 4, Octets, sizeof(Octets));
            Part = 0;
            if (*Text == UNICODE_NULL)
                break;
        }
        Value = 0;
        Digits = FALSE;
    }

    return Count;
}

static
VOID
VmicFillIpInfoBinary(
    _Out_writes_bytes_(KVP_IP_BINARY_SIZE) PUCHAR Body,
    _In_ const VMIC_IP_INFO *IpInfo)
{
    ULONG Count;
    ULONG Index;

    RtlZeroMemory(Body, 24);
    RtlZeroMemory(Body + 280, KVP_IP_BINARY_SIZE - 280);
    Count = VmicParseIpv4List(IpInfo->Addresses, Body + KVP_IP_BINARY_V4, KVP_IP_BINARY_MAX_V4);
    VmicPut32(Body, 0, Count);
    VmicParseIpv4List(IpInfo->Subnets, Body + KVP_IP_BINARY_V4_SUBNET, KVP_IP_BINARY_MAX_V4);
    VmicPut32(Body, 8, VmicParseIpv4List(IpInfo->Gateways, Body + KVP_IP_BINARY_V4_GW, KVP_IP_BINARY_MAX_GW));
    VmicPut32(Body, 16, VmicParseIpv4List(IpInfo->DnsServers, Body + KVP_IP_BINARY_V4_DNS, KVP_IP_BINARY_MAX_V4));
    Body[280] = 1;
    Body[281] = IpInfo->Dhcp ? 1 : 0;
    for (Index = 0; Index < Count; Index++)
        VmicPut16(Body, KVP_IP_BINARY_ORIGINS + Index * 2, IpInfo->Dhcp ? KVP_IP_ORIGIN_OTHER : KVP_IP_ORIGIN_STATIC);
}

static
VOID
VmicHandleKvp(
    _In_ USHORT MessageMajor,
    _Inout_updates_bytes_(Length) PUCHAR Message,
    _In_ ULONG Length)
{
    PUCHAR Kvp = Message + IC_HEADER_SIZE;
    ULONG KvpLength = Length - IC_HEADER_SIZE;
    WCHAR Value[VMIC_STRING_MAX];
    VMIC_IP_INFO IpInfo;
    PCWSTR Key;
    PUCHAR Body;
    UCHAR Operation;
    UCHAR Pool;
    ULONG Status = IC_STATUS_SUCCESS;

    if (Length < IC_HEADER_SIZE + 4)
    {
        VmicPut32(Message, 12, IC_STATUS_FAIL);
        return;
    }

    Operation = Kvp[0];
    Pool = Kvp[1];

    switch (Operation)
    {
        case KVP_OP_ENUMERATE:
            if (KvpLength < 4 + KVP_ENUMERATE_SIZE)
            {
                Status = IC_STATUS_FAIL;
                break;
            }
            Body = Kvp + 4;
            if (Pool != KVP_POOL_AUTO || !VmicAutoPoolItem(VmicGet32(Body, 0), &Key, Value, RTL_NUMBER_OF(Value)))
            {
                Status = IC_STATUS_NO_MORE_ITEMS;
                break;
            }
            RtlZeroMemory(Body + 4, 12 + KVP_KEY_BYTES + KVP_VALUE_BYTES);
            VmicPut32(Body, 4, KVP_REG_SZ);
            VmicPut32(Body, 8, VmicWriteString(Body + 16, KVP_KEY_BYTES, Key));
            VmicPut32(Body, 12, VmicWriteString(Body + 16 + KVP_KEY_BYTES, KVP_VALUE_BYTES, Value));
            break;

        case KVP_OP_GET_IP_INFO:
            if (MessageMajor >= 5)
            {
                if (KvpLength < 4 + KVP_IP_BINARY_SIZE)
                {
                    Status = IC_STATUS_FAIL;
                    break;
                }
                VmicQueryIpInfo(&IpInfo);
                if (!IpInfo.Valid)
                {
                    Status = IC_STATUS_NOT_FOUND;
                    break;
                }
                VmicFillIpInfoBinary(Kvp + 4, &IpInfo);
                break;
            }
            if (KvpLength < KVP_IP_INFO_SIZE)
            {
                Status = IC_STATUS_FAIL;
                break;
            }
            VmicQueryIpInfo(&IpInfo);
            if (!IpInfo.Valid)
            {
                Status = IC_STATUS_NOT_FOUND;
                break;
            }
            Body = Kvp + 2 + 2 * KVP_IP_ADAPTER_CHARS;
            Body[0] = 1;
            Body[1] = IpInfo.Dhcp ? 1 : 0;
            Body += 2;
            RtlZeroMemory(Body, 2 * (3 * KVP_IP_ADDRESS_CHARS + KVP_IP_GATEWAY_CHARS));
            VmicWriteString(Body, 2 * KVP_IP_ADDRESS_CHARS, IpInfo.Addresses);
            Body += 2 * KVP_IP_ADDRESS_CHARS;
            VmicWriteString(Body, 2 * KVP_IP_ADDRESS_CHARS, IpInfo.Subnets);
            Body += 2 * KVP_IP_ADDRESS_CHARS;
            VmicWriteString(Body, 2 * KVP_IP_GATEWAY_CHARS, IpInfo.Gateways);
            Body += 2 * KVP_IP_GATEWAY_CHARS;
            VmicWriteString(Body, 2 * KVP_IP_ADDRESS_CHARS, IpInfo.DnsServers);
            break;

        case KVP_OP_SET:
        case KVP_OP_DELETE:
            break;

        case KVP_OP_GET:
            Status = IC_STATUS_NOT_FOUND;
            break;

        default:
            Status = IC_STATUS_NOT_SUPPORTED;
            break;
    }

    VmicPut32(Message, 12, Status);
}

static
VOID
VmicProcessPacket(
    _Inout_ PVMIC_EXTENSION Extension,
    _In_ ULONG PacketLength)
{
    PVMBUS_PACKET_DESCRIPTOR Descriptor = (PVMBUS_PACKET_DESCRIPTOR)Extension->Packet;
    ULONG Offset = (ULONG)Descriptor->DataOffset8 * 8;
    PUCHAR Pipe;
    PUCHAR Message;
    ULONG PipeLength;
    ULONG Length;
    USHORT Type;
    BOOLEAN Shutdown = FALSE;
    BOOLEAN Restart = FALSE;
    BOOLEAN Force = FALSE;
    NTSTATUS Status;

    if (Descriptor->Type != VMBUS_PACKET_TYPE_INBAND || Offset + PIPE_HEADER_SIZE + IC_HEADER_SIZE > PacketLength)
        return;

    Pipe = Extension->Packet + Offset;
    PipeLength = VmicGet32(Pipe, 4);
    if (VmicGet32(Pipe, 0) != PIPE_MESSAGE_DATA || PipeLength < IC_HEADER_SIZE ||
        PipeLength > PacketLength - Offset - PIPE_HEADER_SIZE)
    {
        return;
    }

    Message = Pipe + PIPE_HEADER_SIZE;
    Length = PipeLength;
    Type = VmicGet16(Message, 4);

    if (Type == IC_MSG_NEGOTIATE)
    {
        VmicNegotiate(Extension, Message, Length);
    }
    else
    {
        if (Extension->MessageMajor == 0)
            Extension->MessageMajor = VmicGet16(Message, 6);

        switch (Type)
        {
            case IC_MSG_HEARTBEAT:
                if (Extension->Kind == VmicHeartbeat && Length >= IC_HEADER_SIZE + 12)
                {
                    VmicPut64(Message, IC_HEADER_SIZE, VmicGet64(Message, IC_HEADER_SIZE) + 1);
                    if (Extension->MessageMajor >= 3)
                        VmicPut32(Message, IC_HEADER_SIZE + 8, 1);
                }
                break;

            case IC_MSG_SHUTDOWN:
                if (Extension->Kind == VmicShutdown)
                    VmicHandleShutdown(Extension, Message, Length, &Shutdown, &Restart, &Force);
                break;

            case IC_MSG_TIMESYNC:
                if (Extension->Kind == VmicTimeSync)
                    VmicHandleTimeSync(Extension, Message, Length);
                break;

            case IC_MSG_KVP:
                if (Extension->Kind == VmicKvp)
                    VmicHandleKvp(Extension->MessageMajor, Message, Length);
                break;

            case IC_MSG_VSS:
                if (Extension->Kind == VmicVss)
                    VmicHandleVss(Extension, Message, Length);
                break;

            default:
                VmicPut32(Message, 12, IC_STATUS_NOT_SUPPORTED);
                break;
        }
    }

    Message[17] = IC_FLAG_TRANSACTION | IC_FLAG_RESPONSE;
    Status = VmbChannelSendPacket(Extension->Channel, VMBUS_PACKET_TYPE_INBAND, 0, Descriptor->TransactionId,
                                  Pipe, PIPE_HEADER_SIZE + Length);
    if (!NT_SUCCESS(Status))
        DPRINT1("VMIC: response to message %u failed with 0x%lx\n", Type, Status);

    if (Shutdown)
    {
        Status = ZwInitiatePowerAction(Restart ? PowerActionShutdownReset : PowerActionShutdownOff,
                                       PowerSystemShutdown,
                                       POWER_ACTION_QUERY_ALLOWED | POWER_ACTION_UI_ALLOWED |
                                       (Force ? POWER_ACTION_OVERRIDE_APPS : 0),
                                       TRUE);
        if (!NT_SUCCESS(Status))
            DPRINT1("VMIC: shutdown request failed with 0x%lx\n", Status);
    }
}

static
BOOLEAN
VmicReceiveOne(
    _Inout_ PVMIC_EXTENSION Extension)
{
    ULONG Length;

    if (!Extension->Open)
        return FALSE;

    if (!NT_SUCCESS(VmbChannelReceivePacket(Extension->Channel, Extension->Packet, sizeof(Extension->Packet), &Length)))
        return FALSE;

    VmicProcessPacket(Extension, Length);
    return TRUE;
}

static
VOID
NTAPI
VmicWorker(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_opt_ PVOID Context)
{
    PVMIC_EXTENSION Extension = Context;

    UNREFERENCED_PARAMETER(DeviceObject);

    for (;;)
    {
        while (VmicReceiveOne(Extension))
            ;

        InterlockedExchange(&Extension->WorkQueued, 0);
        if (!VmicReceiveOne(Extension))
            break;
        if (InterlockedExchange(&Extension->WorkQueued, 1) != 0)
            break;
    }

    KeSetEvent(&Extension->WorkIdle, IO_NO_INCREMENT, FALSE);
}

static
VOID
NTAPI
VmicChannelCallback(
    _In_opt_ PVOID Context)
{
    PVMIC_EXTENSION Extension = Context;

    if (Extension == NULL || !Extension->Open)
        return;

    if (InterlockedExchange(&Extension->WorkQueued, 1) == 0)
    {
        KeClearEvent(&Extension->WorkIdle);
        IoQueueWorkItem(Extension->WorkItem, VmicWorker, DelayedWorkQueue, Extension);
    }
}

static
VMIC_KIND
VmicKindFromGuid(
    _In_ const GUID *InterfaceType)
{
    if (IsEqualGUID(InterfaceType, &VmicShutdownGuid))
        return VmicShutdown;
    if (IsEqualGUID(InterfaceType, &VmicTimeSyncGuid))
        return VmicTimeSync;
    if (IsEqualGUID(InterfaceType, &VmicHeartbeatGuid))
        return VmicHeartbeat;
    if (IsEqualGUID(InterfaceType, &VmicKvpGuid))
        return VmicKvp;
    if (IsEqualGUID(InterfaceType, &VmicVssGuid))
        return VmicVss;
    return VmicUnknown;
}

static
NTSTATUS
VmicStart(
    _Inout_ PVMIC_EXTENSION Extension)
{
    GUID InterfaceType;
    NTSTATUS Status;

    Extension->Channel = VmbChannelFromDevice(Extension->Pdo);
    if (Extension->Channel == NULL)
        return STATUS_NO_SUCH_DEVICE;

    VmbChannelGetInterfaceType(Extension->Channel, &InterfaceType);
    Extension->Kind = VmicKindFromGuid(&InterfaceType);
    if (Extension->Kind == VmicUnknown)
        return STATUS_NOT_SUPPORTED;

    Extension->MessageMajor = 0;
    Extension->TimeSynced = FALSE;
    Extension->Open = TRUE;
    Status = VmbChannelOpen(Extension->Channel, VMIC_RING_SIZE, VmicChannelCallback, Extension);
    if (!NT_SUCCESS(Status))
    {
        Extension->Open = FALSE;
        DPRINT1("VMIC: VmbChannelOpen failed with 0x%lx\n", Status);
    }

    return Status;
}

static
VOID
VmicStop(
    _Inout_ PVMIC_EXTENSION Extension)
{
    if (!Extension->Open)
        return;

    Extension->Open = FALSE;
    VmbChannelClose(Extension->Channel);
    if (InterlockedCompareExchange(&Extension->WorkQueued, 0, 0) != 0)
        KeWaitForSingleObject(&Extension->WorkIdle, Executive, KernelMode, FALSE, NULL);
    VmicThawVolumes(Extension);
}

static
NTSTATUS
VmicComplete(
    _Inout_ PIRP Irp,
    _In_ NTSTATUS Status)
{
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

static
NTSTATUS
NTAPI
VmicPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PVMIC_EXTENSION Extension = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status;

    switch (Stack->MinorFunction)
    {
        case IRP_MN_START_DEVICE:
            Status = IoForwardIrpSynchronously(Extension->LowerDevice, Irp) ? Irp->IoStatus.Status : STATUS_UNSUCCESSFUL;
            if (NT_SUCCESS(Status))
                Status = VmicStart(Extension);
            return VmicComplete(Irp, Status);

        case IRP_MN_STOP_DEVICE:
        case IRP_MN_SURPRISE_REMOVAL:
            VmicStop(Extension);
            Irp->IoStatus.Status = STATUS_SUCCESS;
            break;

        case IRP_MN_QUERY_STOP_DEVICE:
        case IRP_MN_QUERY_REMOVE_DEVICE:
        case IRP_MN_CANCEL_STOP_DEVICE:
        case IRP_MN_CANCEL_REMOVE_DEVICE:
            Irp->IoStatus.Status = STATUS_SUCCESS;
            break;

        case IRP_MN_REMOVE_DEVICE:
            VmicStop(Extension);
            Irp->IoStatus.Status = STATUS_SUCCESS;
            IoSkipCurrentIrpStackLocation(Irp);
            Status = IoCallDriver(Extension->LowerDevice, Irp);
            IoDetachDevice(Extension->LowerDevice);
            IoFreeWorkItem(Extension->WorkItem);
            IoDeleteDevice(DeviceObject);
            return Status;

        default:
            break;
    }

    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(Extension->LowerDevice, Irp);
}

static
NTSTATUS
NTAPI
VmicPower(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PVMIC_EXTENSION Extension = DeviceObject->DeviceExtension;

    PoStartNextPowerIrp(Irp);
    IoSkipCurrentIrpStackLocation(Irp);
    return PoCallDriver(Extension->LowerDevice, Irp);
}

static
NTSTATUS
NTAPI
VmicPassThrough(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PVMIC_EXTENSION Extension = DeviceObject->DeviceExtension;

    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(Extension->LowerDevice, Irp);
}

static
NTSTATUS
NTAPI
VmicAddDevice(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT PhysicalDeviceObject)
{
    PVMIC_EXTENSION Extension;
    PDEVICE_OBJECT DeviceObject;
    NTSTATUS Status;

    Status = IoCreateDevice(DriverObject,
                            sizeof(VMIC_EXTENSION),
                            NULL,
                            FILE_DEVICE_UNKNOWN,
                            FILE_DEVICE_SECURE_OPEN,
                            FALSE,
                            &DeviceObject);
    if (!NT_SUCCESS(Status))
        return Status;

    Extension = DeviceObject->DeviceExtension;
    RtlZeroMemory(Extension, sizeof(*Extension));
    Extension->Self = DeviceObject;
    Extension->Pdo = PhysicalDeviceObject;
    KeInitializeEvent(&Extension->WorkIdle, NotificationEvent, TRUE);

    Extension->WorkItem = IoAllocateWorkItem(DeviceObject);
    if (Extension->WorkItem == NULL)
    {
        IoDeleteDevice(DeviceObject);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Extension->LowerDevice = IoAttachDeviceToDeviceStack(DeviceObject, PhysicalDeviceObject);
    if (Extension->LowerDevice == NULL)
    {
        IoFreeWorkItem(Extension->WorkItem);
        IoDeleteDevice(DeviceObject);
        return STATUS_NO_SUCH_DEVICE;
    }

    DeviceObject->Flags |= DO_POWER_PAGABLE;
    DeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;
}

static
VOID
NTAPI
VmicUnload(
    _In_ PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);
}

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    UNREFERENCED_PARAMETER(RegistryPath);

    DriverObject->DriverExtension->AddDevice = VmicAddDevice;
    DriverObject->DriverUnload = VmicUnload;
    DriverObject->MajorFunction[IRP_MJ_PNP] = VmicPnp;
    DriverObject->MajorFunction[IRP_MJ_POWER] = VmicPower;
    DriverObject->MajorFunction[IRP_MJ_SYSTEM_CONTROL] = VmicPassThrough;
    return STATUS_SUCCESS;
}
