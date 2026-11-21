/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     I/O manager internals
 * COPYRIGHT:   Copyright Alex Ionescu <alex.ionescu@reactos.org>
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include "ntdddisk.h"

#define ENUM_ROOT L"\\Registry\\Machine\\System\\CurrentControlSet\\Enum"

#define IO_METHOD_FROM_CTL_CODE(c)                      (c & 0x00000003)

#define RD_NO_XIPROM_DESCRIPTOR  1

#define RD_NO_RAMDISK_DRIVER     2

#define RD_FSCTL_FAILED          3

#define RD_GUID_CONVERT_FAILED   4

#define RD_SYMLINK_CREATE_FAILED 5

#define RD_INVALID_OFFSET 7

#define RD_INVALID_LENGTH 8

#define RD_NAME_BUILD_FAILED     9

#define RD_ENUMERATE_FAILED      10

#define IOP_USE_TOP_LEVEL_DEVICE_HINT       0x01

typedef struct _FILE_OBJECT_EXTENSION
{
    TXN_PARAMETER_BLOCK TxnParameters;
    PDEVICE_OBJECT TopDeviceObjectHint;
    PVOID FilterContext;
    ULONG FoExtFlags;
    IO_PRIORITY_HINT PriorityHint;
    PMDL IoStatusBlockRange;

} FILE_OBJECT_EXTENSION, *PFILE_OBJECT_EXTENSION;

#define IsIrpSynchronous(Irp, FileObject)               \
    (((Irp)->Flags & IRP_SYNCHRONOUS_API) ||            \
     (!(FileObject) ? FALSE :                           \
       (FileObject)->Flags & FO_SYNCHRONOUS_IO))

#define IoGetDevObjExtension(DeviceObject)              \
    ((PEXTENDED_DEVOBJ_EXTENSION)                       \
     ((DeviceObject)->DeviceObjectExtension))

#define IoGetDrvObjExtension(DriverObject)              \
    ((PEXTENDED_DRIVER_EXTENSION)                       \
     ((DriverObject)->DriverExtension))

#define IopDeviceNodeSetUserFlag(DeviceNode, UserFlag)  \
    ((DeviceNode)->UserFlags |= (UserFlag))

#define IopDeviceNodeClearUserFlag(DeviceNode, UserFlag)\
    ((DeviceNode)->UserFlags &= ~(UserFlag))

#define IopInitDeviceTreeTraverseContext(               \
    _DeviceTreeTraverseContext, _DeviceNode, _Action,   \
    _Context) {                                         \
    (_DeviceTreeTraverseContext)->FirstDeviceNode =     \
        (_DeviceNode);                                  \
    (_DeviceTreeTraverseContext)->Action = (_Action);   \
    (_DeviceTreeTraverseContext)->Context = (_Context); }

typedef enum _IOP_DEVICE_LIST_OPERATION
{
    IopRemove,
    IopAdd
} IOP_DEVICE_LIST_OPERATION, *PIOP_DEVICE_LIST_OPERATION;

typedef enum _IOP_TRANSFER_TYPE
{
    IopReadTransfer,
    IopWriteTransfer,
    IopOtherTransfer
} IOP_TRANSFER_TYPE, *PIOP_TRANSFER_TYPE;

typedef enum _COMPLETION_PACKET_TYPE
    {
    IopCompletionPacketIrp,
    IopCompletionPacketMini,
    IopCompletionPacketQuota,
    IopCompletionPacketWait
} COMPLETION_PACKET_TYPE, *PCOMPLETION_PACKET_TYPE;

typedef struct _IOP_MINI_COMPLETION_PACKET
{
    struct
    {
        LIST_ENTRY ListEntry;
        union
        {
            struct _IO_STACK_LOCATION *CurrentStackLocation;
            ULONG PacketType;
        };
    };
    PVOID KeyContext;
    PVOID ApcContext;
    NTSTATUS IoStatus;
    ULONG_PTR IoStatusInformation;
} IOP_MINI_COMPLETION_PACKET, *PIOP_MINI_COMPLETION_PACKET;

#define IOP_IRP_EXTENSION_ACTIVITY_ID 0x0001

typedef struct _IOP_IRP_EXTENSION
{
    union
    {
        USHORT ExtensionFlags;
        struct
        {
            USHORT Allocated : 1;
            USHORT PropagateId : 1;
            USHORT SpareBits : 14;
        };
    };
    USHORT TypesAllocated;
    UCHAR GenericExtension[4];
    PVOID VerifierContext;
    ULONG64 DiskIoAttributionHandle;
    GUID ActivityId;
    union
    {
        LARGE_INTEGER Timestamp;
        ULONG ZeroingOffset;
        struct
        {
            PVOID FsTrackOffsetBlob;
            LONGLONG FsTrackedOffset;
        };
        PVOID AdapterCryptoParameters;
    };
    union
    {
        ULONG64 Value;
        struct
        {
            ULONG64 UserFlags : 32;
            ULONG64 SystemFlags : 16;
            ULONG64 UserFlagsId : 16;
        };
    } DriverFlags;
    COPY_INFORMATION CopyInformation;
} IOP_IRP_EXTENSION, *PIOP_IRP_EXTENSION;

C_ASSERT(sizeof(PVOID) != 8 || FIELD_OFFSET(IRP, Tail.Overlay.IrpExtension) == 0xC8);

C_ASSERT(sizeof(PVOID) != 8 || sizeof(IRP) == 0xD0);

C_ASSERT(sizeof(PVOID) != 8 || FIELD_OFFSET(IOP_IRP_EXTENSION, ActivityId) == 0x18);

C_ASSERT(sizeof(PVOID) != 8 || FIELD_OFFSET(IOP_IRP_EXTENSION, DriverFlags) == 0x38);

C_ASSERT(sizeof(PVOID) != 8 || FIELD_OFFSET(IOP_IRP_EXTENSION, CopyInformation) == 0x40);

C_ASSERT(sizeof(PVOID) != 8 || sizeof(IOP_IRP_EXTENSION) == 0x50);

typedef struct _IO_UNLOAD_SAFE_COMPLETION_CONTEXT
{
    PDEVICE_OBJECT DeviceObject;
    PVOID Context;
    PIO_COMPLETION_ROUTINE CompletionRoutine;
} IO_UNLOAD_SAFE_COMPLETION_CONTEXT, *PIO_UNLOAD_SAFE_COMPLETION_CONTEXT;

typedef struct _IO_WORKITEM
{
    WORK_QUEUE_ITEM WorkItem;
    PIO_WORKITEM_ROUTINE Routine;
    PVOID IoObject;
    PVOID Context;

    struct _ETHREAD *WorkOnBehalfThread;
    ULONG Type;
    GUID ActivityId;
} IO_WORKITEM;

C_ASSERT((sizeof(PVOID) != 8) || (FIELD_OFFSET(IO_WORKITEM, Routine) == 0x20));

C_ASSERT((sizeof(PVOID) != 8) || (FIELD_OFFSET(IO_WORKITEM, IoObject) == 0x28));

C_ASSERT((sizeof(PVOID) != 8) || (FIELD_OFFSET(IO_WORKITEM, Context) == 0x30));

C_ASSERT((sizeof(PVOID) != 8) || (FIELD_OFFSET(IO_WORKITEM, WorkOnBehalfThread) == 0x38));

C_ASSERT((sizeof(PVOID) != 8) || (FIELD_OFFSET(IO_WORKITEM, Type) == 0x40));

C_ASSERT((sizeof(PVOID) != 8) || (FIELD_OFFSET(IO_WORKITEM, ActivityId) == 0x44));

C_ASSERT((sizeof(PVOID) != 8) || (sizeof(IO_WORKITEM) == 0x58));

typedef struct _IO_INTERRUPT
{
    KINTERRUPT FirstInterrupt;
    PKINTERRUPT Interrupt[MAXIMUM_PROCESSORS];
    KSPIN_LOCK SpinLock;
} IO_INTERRUPT, *PIO_INTERRUPT;

typedef struct _ERROR_LOG_ENTRY
{
    CSHORT Type;
    CSHORT Size;
    LIST_ENTRY ListEntry;
    PDEVICE_OBJECT DeviceObject;
    PDRIVER_OBJECT DriverObject;
    LARGE_INTEGER TimeStamp;
} ERROR_LOG_ENTRY, *PERROR_LOG_ENTRY;

typedef struct _EXTENDED_IO_REMOVE_LOCK
{
    IO_REMOVE_LOCK_COMMON_BLOCK Common;
    IO_REMOVE_LOCK_DBG_BLOCK Dbg;
} EXTENDED_IO_REMOVE_LOCK, *PEXTENDED_IO_REMOVE_LOCK;

typedef struct _DUMMY_FILE_OBJECT
{
    OBJECT_HEADER ObjectHeader;
    CHAR FileObjectBody[sizeof(FILE_OBJECT)];
} DUMMY_FILE_OBJECT, *PDUMMY_FILE_OBJECT;

typedef struct _OPEN_PACKET
{
    CSHORT Type;
    CSHORT Size;
    PFILE_OBJECT FileObject;
    NTSTATUS FinalStatus;
    ULONG_PTR Information;
    ULONG ParseCheck;
    union
    {
        PFILE_OBJECT RelatedFileObject;
        PDEVICE_OBJECT ReferencedDeviceObject;
    };
    POBJECT_ATTRIBUTES OriginalAttributes;
    LARGE_INTEGER AllocationSize;
    ULONG CreateOptions;
    USHORT FileAttributes;
    USHORT ShareAccess;
    PVOID EaBuffer;
    ULONG EaLength;
    ULONG Options;
    ULONG Disposition;
    PFILE_BASIC_INFORMATION BasicInformation;
    PFILE_NETWORK_OPEN_INFORMATION NetworkInformation;
    PVOID FileInformation;
    CREATE_FILE_TYPE CreateFileType;
    PVOID MailslotOrPipeParameters;
    BOOLEAN Override;
    BOOLEAN QueryOnly;
    BOOLEAN DeleteOnly;
    BOOLEAN FullAttributes;
    PDUMMY_FILE_OBJECT LocalFileObject;
    ULONG InternalFlags;
    KPROCESSOR_MODE AccessMode;
    IO_DRIVER_CREATE_CONTEXT DriverCreateContext;
    FILE_INFORMATION_CLASS FileInformationClass;
    ULONG FileInformationLength;
    BOOLEAN FilterQuery;
    LONGLONG ExtendedCreateFlags;
} OPEN_PACKET, *POPEN_PACKET;

C_ASSERT(sizeof(PVOID) != 8 || FIELD_OFFSET(OPEN_PACKET, RelatedFileObject) == 0x28);

C_ASSERT(sizeof(PVOID) != 8 || FIELD_OFFSET(OPEN_PACKET, AllocationSize) == 0x38);

C_ASSERT(sizeof(PVOID) != 8 || FIELD_OFFSET(OPEN_PACKET, CreateFileType) == 0x78);

C_ASSERT(sizeof(PVOID) != 8 || FIELD_OFFSET(OPEN_PACKET, MailslotOrPipeParameters) == 0x80);

C_ASSERT(sizeof(PVOID) != 8 || FIELD_OFFSET(OPEN_PACKET, LocalFileObject) == 0x90);

C_ASSERT(sizeof(PVOID) != 8 || FIELD_OFFSET(OPEN_PACKET, InternalFlags) == 0x98);

C_ASSERT(sizeof(PVOID) != 8 || FIELD_OFFSET(OPEN_PACKET, AccessMode) == 0x9c);

C_ASSERT(sizeof(PVOID) != 8 || FIELD_OFFSET(OPEN_PACKET, DriverCreateContext) == 0xa0);

C_ASSERT(sizeof(PVOID) != 8 || FIELD_OFFSET(OPEN_PACKET, FileInformationClass) == 0xc8);

C_ASSERT(sizeof(PVOID) != 8 || FIELD_OFFSET(OPEN_PACKET, ExtendedCreateFlags) == 0xd8);

C_ASSERT(sizeof(PVOID) != 8 || sizeof(OPEN_PACKET) == 0xe0);

typedef struct _DRIVER_INFORMATION
{
    LIST_ENTRY Link;
    PDRIVER_OBJECT DriverObject;
    PBOOT_DRIVER_LIST_ENTRY DataTableEntry;
    HANDLE ServiceHandle;
    USHORT TagPosition;
    ULONG Failed;
    ULONG Processed;
    NTSTATUS Status;
} DRIVER_INFORMATION, *PDRIVER_INFORMATION;

typedef struct _IO_BUS_TYPE_GUID_LIST
{
    FAST_MUTEX Lock;
    ULONG AllocatedCount;
    ULONG GuidCount;
    PGUID Guids;
} IO_BUS_TYPE_GUID_LIST, *PIO_BUS_TYPE_GUID_LIST;

typedef struct _SHUTDOWN_ENTRY
{
    LIST_ENTRY ShutdownList;
    PDEVICE_OBJECT DeviceObject;
} SHUTDOWN_ENTRY, *PSHUTDOWN_ENTRY;

typedef struct _DRIVER_REINIT_ITEM
{
    LIST_ENTRY ItemEntry;
    PDRIVER_OBJECT DriverObject;
    PDRIVER_REINITIALIZE ReinitRoutine;
    PVOID Context;
} DRIVER_REINIT_ITEM, *PDRIVER_REINIT_ITEM;

typedef
NTSTATUS
(*DEVICETREE_TRAVERSE_ROUTINE)(
    IN PDEVICE_NODE DeviceNode,
    IN PVOID Context
);

typedef struct _DEVICETREE_TRAVERSE_CONTEXT
{
    PDEVICE_NODE DeviceNode;

    PDEVICE_NODE FirstDeviceNode;

    DEVICETREE_TRAVERSE_ROUTINE Action;

    PVOID Context;
} DEVICETREE_TRAVERSE_CONTEXT, *PDEVICETREE_TRAVERSE_CONTEXT;

typedef struct _RESERVE_IRP_ALLOCATOR
{
    PIRP ReserveIrp;
    volatile LONG ReserveIrpInUse;
    KEVENT WaitEvent;
    CCHAR StackSize;
} RESERVE_IRP_ALLOCATOR, *PRESERVE_IRP_ALLOCATOR;

typedef enum _SECURITY_DESCRIPTOR_TYPE
{
    RestrictedPublic = 1,
    UnrestrictedPublic,
    RestrictedPublicOpen,
    UnrestrictedPublicOpen,
    SystemDefault,
} SECURITY_DESCRIPTOR_TYPE, *PSECURITY_DESCRIPTOR_TYPE;

typedef enum _DEVICE_ACTION
{
    PiActionEnumDeviceTree,
    PiActionEnumRootDevices,
    PiActionResetDevice,
    PiActionAddBootDevices,
    PiActionStartDevice,
    PiActionQueryState,
    PiActionRemoveDevice,
    PiActionQueryRemoveDevice,
    PiActionCloseRemovedDevice,
    PiActionEjectDevice,
} DEVICE_ACTION;

extern BOOLEAN PnpEnableParallelEnum;

typedef VOID (NTAPI *PPI_PARALLEL_ROUTINE)(_In_ PVOID Item);

#if DBG
VOID PiDiagAcquireDevNode(_In_ PDEVICE_NODE Node);
VOID PiDiagReleaseDevNode(_In_ PDEVICE_NODE Node);
#endif

typedef struct _PI_QUERY_REMOVE_DATA
{
    PNP_VETO_TYPE VetoType;
    UNICODE_STRING VetoName;
    PVOID VetoObject;
} PI_QUERY_REMOVE_DATA, *PPI_QUERY_REMOVE_DATA;

extern POBJECT_TYPE IoCompletionType;

extern POBJECT_TYPE IopWaitCompletionPacketType;

extern PDEVICE_NODE IopRootDeviceNode;

extern KSPIN_LOCK IopDeviceTreeLock;

extern ERESOURCE IopDeviceTreeResource;

extern KMUTEX IopResourceAssignmentLock;

#define IOP_SERVICE_ENUM_LOCK_COUNT 32

extern KMUTEX IopServiceEnumLocks[IOP_SERVICE_ENUM_LOCK_COUNT];

extern ULONG IopTraceLevel;

extern GENERAL_LOOKASIDE IopMdlLookasideList;

extern GENERIC_MAPPING IopCompletionMapping;

extern GENERIC_MAPPING IopFileMapping;

extern LIST_ENTRY IopErrorLogListHead;

extern ULONG IopAutoReboot;

extern ULONG IopNumTriageDumpDataBlocks;

extern PVOID IopTriageDumpDataBlocks[64];

extern IO_BUS_TYPE_GUID_LIST PnpBusTypeGuidList;

extern PDRIVER_OBJECT IopRootDriverObject;

extern KSPIN_LOCK IopDeviceActionLock;

extern LIST_ENTRY IopDeviceActionRequestList;

extern RESERVE_IRP_ALLOCATOR IopReserveIrpAllocator;

extern BOOLEAN IoRemoteBootClient;

typedef struct _IOP_IOMMU_PROVIDER
{
    DMA_IOMMU_INTERFACE_V1 Legacy;
    DMA_IOMMU_INTERFACE_V3 Extended;
} IOP_IOMMU_PROVIDER, *PIOP_IOMMU_PROVIDER;

typedef NTSTATUS
(NTAPI *PIOP_DIF_REGISTER_CLASS_DRIVER_PLUGIN)(
    _In_ ULONG Version,
    _In_opt_ PVOID Plugin,
    _In_ ULONG Flags,
    _In_opt_ PDRIVER_OBJECT DriverObject);

typedef struct _IOP_DIF_PROVIDER
{
    PIOP_DIF_REGISTER_CLASS_DRIVER_PLUGIN RegisterClassDriverPlugin;
} IOP_DIF_PROVIDER, *PIOP_DIF_PROVIDER;

typedef NTSTATUS
(NTAPI *PIOP_KSR_ENUMERATE_CALLBACK)(
    _In_ const GUID *MemoryId,
    _In_ ULONGLONG MemoryLength,
    _In_opt_ PVOID Context);

typedef struct _IOP_KSR_FIRMWARE_INFORMATION IOP_KSR_FIRMWARE_INFORMATION;

typedef const IOP_KSR_FIRMWARE_INFORMATION *PCIOP_KSR_FIRMWARE_INFORMATION;

typedef struct _IOP_KSR_PROVIDER
{
    ULONG Size;
    ULONG Version;
    NTSTATUS (NTAPI *ClaimPersistedMemory)(const GUID *, ULONGLONG, PULONGLONG, ULONG, BOOLEAN, PULONG);
    NTSTATUS (NTAPI *QueryMetadata)(const GUID *, ULONGLONG, PVOID, ULONG, PULONG);
    VOID (NTAPI *FreePersistedMemory)(const GUID *, BOOLEAN);
    NTSTATUS (NTAPI *PersistMemoryWithMetadata)(const GUID *, PULONGLONG, ULONG, PVOID, ULONG, PULONGLONG);
    NTSTATUS (NTAPI *GetFirmwareInformation)(PCIOP_KSR_FIRMWARE_INFORMATION *);
    NTSTATUS (NTAPI *EnumeratePersistedMemory)(const GUID *, PIOP_KSR_ENUMERATE_CALLBACK, PVOID);
    NTSTATUS (NTAPI *MdlToMemoryRuns)(PMDL, PULONGLONG, ULONG, PULONG);
} IOP_KSR_PROVIDER, *PIOP_KSR_PROVIDER;

#define IOP_KSR_PROVIDER_VERSION 1

VOID
NTAPI
FsRtlpFreeAddedEcps(
    _In_ struct _ECP_LIST *EcpList);

VOID
NTAPI
FsRtlpMarkCallerEcps(
    _In_ struct _ECP_LIST *EcpList);

//
// PnP software devices
//
VOID
PnpSwDeviceInitialize(VOID);

NTSTATUS
NTAPI
PnpSwDeviceDriverEntry(
    _In_opt_ PDRIVER_OBJECT DriverObject,
    _In_opt_ PUNICODE_STRING RegistryPath);

NTSTATUS
PnpSwDeviceAddChildren(
    _In_ PDEVICE_NODE DeviceNode,
    _In_ NTSTATUS QueryStatus);

VOID
PnpSwDeviceParentRemoved(
    _In_ PDEVICE_OBJECT Parent);

VOID
NTAPI
IopBootDriverStatusUpdate(
    _In_ BDCB_STATUS_UPDATE_TYPE StatusType);

BOOLEAN
NTAPI
IopBootImageAllowed(
    _In_ PCUNICODE_STRING ImageName,
    _In_opt_ PCUNICODE_STRING RegistryPath);

VOID
NTAPI
IopDeleteDevice(
    _In_ PVOID ObjectBody);

VOID
NTAPI
IopDereferenceDeviceObject(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ BOOLEAN ForceUnload);

PDEVICE_OBJECT
NTAPI
IopGetDeviceAttachmentBase(
    _In_ PDEVICE_OBJECT DeviceObject);

VOID
NTAPI
IopReadyDeviceObjects(
    _In_ PDRIVER_OBJECT Driver);

NTSTATUS
NTAPI
IopReferenceDeviceObject(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_opt_ POPEN_PACKET OpenPacket);

VOID
NTAPI
IopUnloadDevice(
    _In_ PDEVICE_OBJECT DeviceObject);

BOOLEAN
NTAPI
IopVerifyDeviceObjectOnStack(
    _In_ PDEVICE_OBJECT BaseDeviceObject,
    _In_ PDEVICE_OBJECT TopDeviceObjectHint);

VOID
FASTCALL
IopInitTimerImplementation(VOID);

VOID
NTAPI
IopRemoveTimerFromTimerList(
    _In_ PIO_TIMER Timer);

CODE_SEG("INIT")
VOID
FASTCALL
IopInitializeBootDrivers(VOID);

NTSTATUS
IopInitializeDriverModule(
    _In_ PKLDR_DATA_TABLE_ENTRY ModuleObject,
    _In_ HANDLE ServiceHandle,
    _Out_ PDRIVER_OBJECT *DriverObject,
    _Out_ NTSTATUS *DriverEntryStatus,
    _In_ BOOLEAN HoldPnpUnloadBarrier);

CODE_SEG("INIT")
VOID
FASTCALL
IopInitializeSystemDrivers(VOID);

CODE_SEG("INIT")
NTSTATUS
NTAPI
PiInitCacheGroupInformation(VOID);

USHORT
NTAPI
PipGetDriverTagPriority(
    _In_ HANDLE ServiceHandle);

USHORT
NTAPI
PpInitGetGroupOrderIndex(
    _In_ HANDLE ServiceHandle);

VOID
NTAPI
IopAcquirePnpUnloadBarrier(
    _In_ PDRIVER_OBJECT DriverObject);

VOID
NTAPI
IopDeleteDriver(
    _In_ PVOID ObjectBody);

NTSTATUS
NTAPI
IopCreateDriver(
    _In_opt_ PUNICODE_STRING DriverName,
    _In_ PDRIVER_INITIALIZE InitializationFunction,
    _Out_opt_ PDRIVER_OBJECT *CreatedDriver);

VOID
NTAPI
IoUnloadSessionDriver(
    _In_ PVOID Driver);

NTSTATUS
IopGetDriverNames(
    _In_ HANDLE ServiceHandle,
    _Out_ PUNICODE_STRING DriverName,
    _Out_opt_ PUNICODE_STRING ServiceName);

BOOLEAN
IopHasSafeBootEntry(
    _In_ HANDLE SafeBootHandle,
    _In_ PUNICODE_STRING Name);

VOID
IopInitDriverImplementation(VOID);

VOID
IopInitDriverLoadSlots(VOID);

NTSTATUS
IopLoadDriverForPnp(
    _In_ HANDLE ServiceHandle,
    _Out_ PDRIVER_OBJECT *DriverObject);

NTSTATUS
IopOpenSafeBootKey(
    _Out_ PHANDLE SafeBootHandle);

NTSTATUS
IopReferenceDriverObject(
    _In_ PCUNICODE_STRING DriverName,
    _Out_ PDRIVER_OBJECT *DriverObject);

VOID
NTAPI
IopReinitializeBootDrivers(VOID);

VOID
NTAPI
IopReinitializeDrivers(VOID);

VOID
NTAPI
IopReleasePnpUnloadBarrier(
    _In_ PDRIVER_OBJECT DriverObject);

NTSTATUS
NTAPI
IoChangeFileObjectFilterContext(
    _In_ PFILE_OBJECT FileObject,
    _In_ PVOID FilterContext,
    _In_ BOOLEAN Define);

NTSTATUS
NTAPI
IoComputeDesiredAccessFileObject(
    _In_ PFILE_OBJECT FileObject,
    _In_ PACCESS_MASK DesiredAccess);

PVOID
NTAPI
IoGetFileObjectFilterContext(
    _In_ PFILE_OBJECT FileObject);

NTSTATUS
NTAPI
IopAcquireFileObjectLock(
    _In_ PFILE_OBJECT FileObject,
    _In_ KPROCESSOR_MODE AccessMode,
    _In_ BOOLEAN Alertable,
    _Out_ PBOOLEAN LockFailed);

NTSTATUS
NTAPI
IopAllocateFileObjectExtension(
    _In_ PFILE_OBJECT FileObject);

VOID
NTAPI
IopCloseFile(
    _In_opt_ PEPROCESS Process,
    _In_ PVOID Object,
    _In_ ULONG_PTR ProcessHandleCount,
    _In_ ULONG_PTR SystemHandleCount);

VOID
NTAPI
IopDeleteFile(
    _In_ PVOID ObjectBody);

NTSTATUS
NTAPI
IopGetSetSecurityObject(
    _In_ PVOID ObjectBody,
    _In_ SECURITY_OPERATION_CODE OperationCode,
    _In_ PSECURITY_INFORMATION SecurityInformation,
    _Inout_ PSECURITY_DESCRIPTOR SecurityDescriptor,
    _Inout_ PULONG BufferLength,
    _Out_ PSECURITY_DESCRIPTOR *OldSecurityDescriptor,
    _In_ POOL_TYPE PoolType,
    _Inout_ PGENERIC_MAPPING GenericMapping,
    _In_ KPROCESSOR_MODE AccessMode);

NTSTATUS
NTAPI
IopQueryName(
    _In_ PVOID ObjectBody,
    _In_ BOOLEAN HasName,
    _Out_ POBJECT_NAME_INFORMATION ObjectNameInfo,
    _In_ ULONG Length,
    _Out_ PULONG ReturnLength,
    _In_ KPROCESSOR_MODE PreviousMode);

NTSTATUS
NTAPI
IopQueryNameInternal(
    _In_ PVOID ObjectBody,
    _In_ BOOLEAN HasName,
    _In_ BOOLEAN QueryDosName,
    _Out_ POBJECT_NAME_INFORMATION ObjectNameInfo,
    _In_ ULONG Length,
    _Out_ PULONG ReturnLength,
    _In_ KPROCESSOR_MODE PreviousMode);

NTSTATUS
NTAPI
IopSetDeviceSecurityDescriptors(
    _In_ PDEVICE_OBJECT UpperDeviceObject,
    _In_ PDEVICE_OBJECT PhysicalDeviceObject,
    _In_ PSECURITY_INFORMATION SecurityInformation,
    _In_ PSECURITY_DESCRIPTOR SecurityDescriptor,
    _In_ POOL_TYPE PoolType,
    _In_ PGENERIC_MAPPING GenericMapping);

CODE_SEG("INIT")
NTSTATUS
NTAPI
IopCreateArcNames(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock);

CODE_SEG("INIT")
NTSTATUS
NTAPI
IopCreateArcNamesDisk(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _In_ BOOLEAN SingleDisk,
    _Out_ PBOOLEAN FoundBoot);

CODE_SEG("INIT")
NTSTATUS
NTAPI
IopReassignSystemRoot(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _Out_ PANSI_STRING NtDeviceName);

BOOLEAN
IopVerifyDiskSignature(
    _In_ PDRIVE_LAYOUT_INFORMATION_EX DriveLayout,
    _In_ PARC_DISK_SIGNATURE ArcDiskSignature,
    _Out_ PULONG Signature);

VOID
IopBootLog(
    _In_ PUNICODE_STRING DriverName,
    _In_ BOOLEAN Success);

CODE_SEG("INIT")
VOID
IopInitBootLog(
    _In_ BOOLEAN StartBootLog);

VOID
IopSaveBootLogToFile(VOID);

CODE_SEG("INIT")
VOID
IopStartBootLog(VOID);

VOID
IopStopBootLog(VOID);

VOID
NTAPI
IoCancelThreadIo(
    _In_ PETHREAD Thread);

VOID
IoInitCancelHandling(VOID);

VOID
NTAPI
IopAbandonThreadAgnosticIrp(
    _In_ PIRP Irp);

VOID
NTAPI
IopAbortInterruptedIrp(
    _In_ PKEVENT EventObject,
    _In_ PIRP Irp);

NTSTATUS
NTAPI
IopCleanupFailedIrp(
    _In_ PFILE_OBJECT FileObject,
    _In_ PKEVENT EventObject,
    _In_opt_ PVOID Buffer);

VOID
NTAPI
IopCompleteRequest(
    _In_ PKAPC Apc,
    _In_ PKNORMAL_ROUTINE* NormalRoutine,
    _In_ PVOID* NormalContext,
    _In_ PVOID* SystemArgument1,
    _In_ PVOID* SystemArgument2);

VOID
NTAPI
IopQueueThreadAgnosticIrp(
    _In_ PIRP Irp,
    _In_ PFILE_OBJECT FileObject);

NTSTATUS
NTAPI
IoSetIoCompletion(
    _In_ PVOID IoCompletion,
    _In_ PVOID KeyContext,
    _In_ PVOID ApcContext,
    _In_ NTSTATUS IoStatus,
    _In_ ULONG_PTR IoStatusInformation,
    _In_ BOOLEAN Quota);

VOID
NTAPI
IopCloseIoCompletion(
    _In_opt_ PEPROCESS Process,
    _In_ PVOID Object,
    _In_ ULONG_PTR ProcessHandleCount,
    _In_ ULONG_PTR SystemHandleCount);

VOID
NTAPI
IopDeleteIoCompletion(
    PVOID ObjectBody);

BOOLEAN
NTAPI
IoInitializeBootPageFileCrashDump(
    _In_ PCUNICODE_STRING BootDeviceName);

BOOLEAN
NTAPI
IoInitializeCrashDump(
    _In_ HANDLE DumpFileHandle);

BOOLEAN
NTAPI
IoInitializeCrashDumpCore(
    _In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock);

BOOLEAN
NTAPI
IoInitializeDedicatedCrashDump(
    _In_ PFILE_OBJECT BootFileObject);

VOID
NTAPI
IoPublishCrashDumpArtifacts(VOID);

BOOLEAN
NTAPI
IoReleaseCrashDumpFile(VOID);

VOID
NTAPI
IopInitErrorLog(VOID);

VOID
NTAPI
IopLogWorker(
    _In_ PVOID Parameter);

NTSTATUS
NTAPI
IopGetFileInformation(
    _In_ PFILE_OBJECT FileObject,
    _In_ ULONG Length,
    _In_ FILE_INFORMATION_CLASS FileInfoClass,
    _Out_ PVOID Buffer,
    _Out_ PULONG ReturnedLength);

CODE_SEG("INIT")
BOOLEAN
NTAPI
IoInitSystem(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock);

KIRQL
NTAPI
IopAcquireDatabaseLockExclusive(VOID);

KIRQL
NTAPI
IopAcquireDatabaseLockShared(VOID);

VOID
NTAPI
IopReleaseDatabaseLockExclusive(
    _In_ KIRQL OldIrql);

VOID
NTAPI
IopReleaseDatabaseLockShared(
    _In_ KIRQL OldIrql);

NTSTATUS
NTAPI
IopRegisterIommuProvider(
    _In_ const IOP_IOMMU_PROVIDER *Provider);

PIRP
NTAPI
IopAllocateIrpMustSucceed(
    _In_ CCHAR StackSize);

PIRP
NTAPI
IopAllocateReserveIrp(
    _In_ CCHAR StackSize);

VOID
NTAPI
IopFreeIrpExtension(
    _Inout_ PIRP Irp);

BOOLEAN
NTAPI
IopInitializeReserveIrp(
    _In_ PRESERVE_IRP_ALLOCATOR ReserveIrpAllocator);

NTSTATUS
NTAPI
IopParseDevice(
    _In_ PVOID ParseObject,
    _In_ PVOID ObjectType,
    _Inout_ PACCESS_STATE AccessState,
    _In_ KPROCESSOR_MODE AccessMode,
    _In_ ULONG Attributes,
    _Inout_ PUNICODE_STRING CompleteName,
    _Inout_ PUNICODE_STRING RemainingName,
    _Inout_ PVOID Context,
    _In_opt_ PSECURITY_QUALITY_OF_SERVICE SecurityQos,
    _Out_ PVOID *Object);

NTSTATUS
NTAPI
IopParseDeviceEx(
    _In_ PVOID ParseObject,
    _In_ PVOID ObjectType,
    _Inout_ PACCESS_STATE AccessState,
    _In_ KPROCESSOR_MODE AccessMode,
    _In_ ULONG Attributes,
    _Inout_ PUNICODE_STRING CompleteName,
    _Inout_ PUNICODE_STRING RemainingName,
    _Inout_opt_ PVOID Context,
    _In_opt_ PSECURITY_QUALITY_OF_SERVICE SecurityQos,
    _In_ POB_EXTENDED_PARSE_PARAMETERS ExtendedParameters,
    _Out_ PVOID *Object);

NTSTATUS
NTAPI
IopParseFile(
    _In_ PVOID ParseObject,
    _In_ PVOID ObjectType,
    _Inout_ PACCESS_STATE AccessState,
    _In_ KPROCESSOR_MODE AccessMode,
    _In_ ULONG Attributes,
    _Inout_ PUNICODE_STRING CompleteName,
    _Inout_ PUNICODE_STRING RemainingName,
    _Inout_opt_ PVOID Context,
    _In_opt_ PSECURITY_QUALITY_OF_SERVICE SecurityQos,
    _Out_ PVOID *Object);

NTSTATUS
NTAPI
IopParseFileEx(
    _In_ PVOID ParseObject,
    _In_ PVOID ObjectType,
    _Inout_ PACCESS_STATE AccessState,
    _In_ KPROCESSOR_MODE AccessMode,
    _In_ ULONG Attributes,
    _Inout_ PUNICODE_STRING CompleteName,
    _Inout_ PUNICODE_STRING RemainingName,
    _Inout_opt_ PVOID Context,
    _In_opt_ PSECURITY_QUALITY_OF_SERVICE SecurityQos,
    _In_ POB_EXTENDED_PARSE_PARAMETERS ExtendedParameters,
    _Out_ PVOID *Object);

CODE_SEG("INIT")
NTSTATUS
NTAPI
IopStartRamdisk(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock);

CODE_SEG("INIT")
NTSTATUS
NTAPI
RawFsDriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath);

BOOLEAN
RawFsIsRawFileSystemDeviceObject(
    _In_ PDEVICE_OBJECT DeviceObject);

NTSTATUS
NTAPI
IopDetectResourceConflict(
    _In_ PCM_RESOURCE_LIST ResourceList,
    _In_ BOOLEAN Silent,
    _In_opt_ PVOID Claimant,
    _Out_ OPTIONAL PCM_PARTIAL_RESOURCE_DESCRIPTOR ConflictingDescriptor);

BOOLEAN
IopDmaDescriptorsConflict(
    _In_ PCM_PARTIAL_RESOURCE_DESCRIPTOR First,
    _In_ PCM_PARTIAL_RESOURCE_DESCRIPTOR Second);

NTSTATUS
IopFetchConfigurationInformation(
    _Out_ PWSTR* SymbolicLinkList,
    _In_ GUID Guid,
    _In_ ULONG ExpectedInterfaces,
    _Out_ PULONG Interfaces);

VOID
NTAPI
IopInitializeSystemResourceMap(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock);

BOOLEAN
IopIsResourceListValid(
    _In_ PCM_RESOURCE_LIST ResourceList,
    _In_ ULONG ResourceListLength);

VOID
IopStoreSystemPartitionInformation(
    _In_ PUNICODE_STRING NtSystemPartitionDeviceName,
    _In_ PUNICODE_STRING OsLoaderPathName);

ULONG
NTAPI
PnpDetermineResourceListSize(
    _In_ PCM_RESOURCE_LIST ResourceList);

VOID
IoInitShutdownNotification(VOID);

VOID
NTAPI
IoShutdownSystem(
    _In_ ULONG Phase);

VOID
NTAPI
IopShutdownBaseFileSystems(
    _In_ PLIST_ENTRY ListHead);

VOID
NTAPI
IoInitFileSystemImplementation(VOID);

VOID
NTAPI
IoInitVpbImplementation(VOID);

PVPB
NTAPI
IopCheckVpbMounted(
    _In_ POPEN_PACKET OpenPacket,
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PUNICODE_STRING RemainingName,
    _Out_ PNTSTATUS Status);

NTSTATUS
NTAPI
IopCreateVpb(
    _In_ PDEVICE_OBJECT DeviceObject);

VOID
NTAPI
IopDereferenceVpbAndFree(
    _In_ PVPB Vpb);

NTSTATUS
NTAPI
IopMountVolume(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ BOOLEAN AllowRawMount,
    _In_ BOOLEAN DeviceIsLocked,
    _In_ BOOLEAN Alertable,
    _Out_ PVPB *Vpb);

VOID
NTAPI
IopReleaseStorageVpb(
    _In_ PDEVICE_OBJECT DeviceObject);

NTSTATUS
NTAPI
IopRegisterKsrProvider(
    _In_ const IOP_KSR_PROVIDER *Provider);

PVOID
NTAPI
IopResolveKsrApiSetRoutine(
    _In_ PCSTR RoutineName);

BOOLEAN
NTAPI
IopInitializeWaitCompletionPacketType(VOID);

VOID
NTAPI
IopReleaseWaitCompletionPacket(
    PIOP_MINI_COMPLETION_PACKET Packet);

NTSTATUS
NTAPI
IopRegisterDifProvider(
    _In_ const IOP_DIF_PROVIDER *Provider);

VOID
IopDisableDeviceInterfaces(
    _In_ PDEVICE_NODE DeviceNode);

CODE_SEG("INIT")
NTSTATUS
IopInitPlugPlayEvents(VOID);

NTSTATUS
IopQueueCustomTargetEvent(
    _In_ PUNICODE_STRING DeviceIds,
    _In_ PTARGET_DEVICE_CUSTOM_NOTIFICATION Notification);

NTSTATUS
IopQueueDeviceChangeEvent(
    _In_ const GUID *EventGuid,
    _In_ const GUID *InterfaceClassGuid,
    _In_ PUNICODE_STRING SymbolicLinkName);

NTSTATUS
IopQueueDeviceInstallEvent(
    _In_ const GUID *Guid,
    _In_ PUNICODE_STRING DeviceId);

NTSTATUS
IopQueueTargetDeviceEvent(
    _In_ const GUID *Guid,
    _In_ PUNICODE_STRING DeviceIds);

NTSTATUS
NTAPI
IoGetRelatedTargetDevice(
    _In_ PFILE_OBJECT FileObject,
    _Out_ PDEVICE_OBJECT *DeviceObject);

NTSTATUS
IopFreeDeviceNode(
    _In_ PDEVICE_NODE DeviceNode);

PDEVICE_NODE
FASTCALL
IopGetDeviceNode(
    _In_ PDEVICE_OBJECT DeviceObject);

NTSTATUS
NTAPI
IopGetRelatedTargetDevice(
    _In_ PFILE_OBJECT FileObject,
    _Out_ PDEVICE_NODE *DeviceNode);

NTSTATUS
IopGetSystemPowerDeviceObject(
    _In_ PDEVICE_OBJECT *DeviceObject);

NTSTATUS
IopTraverseDeviceTree(
    PDEVICETREE_TRAVERSE_CONTEXT Context);

VOID
PiClearDevNodeFlag(
    _In_ PDEVICE_NODE DeviceNode,
    _In_ ULONG Flags);

VOID
PiClearDevNodeProblem(
    _In_ PDEVICE_NODE DeviceNode);

VOID
NTAPI
PiDeviceObjectClosed(
    _In_ PDEVICE_OBJECT DeviceObject);

VOID
PiInsertDevNode(
    _In_ PDEVICE_NODE DeviceNode,
    _In_ PDEVICE_NODE ParentNode);

VOID
PiSetDevNodeFlag(
    _In_ PDEVICE_NODE DeviceNode,
    _In_ ULONG Flags);

VOID
PiSetDevNodeProblem(
    _In_ PDEVICE_NODE DeviceNode,
    _In_ UINT32 Problem);

PNP_DEVNODE_STATE
PiSetDevNodeState(
    _In_ PDEVICE_NODE DeviceNode,
    _In_ PNP_DEVNODE_STATE NewState);

VOID
PiSetDevNodeText(
    _In_ PDEVICE_NODE DeviceNode,
    _In_ HANDLE InstanceKey);

VOID
PiUnlinkDevNode(
    _In_ PDEVICE_NODE DeviceNode);

VOID
NTAPI
PiUpdateServiceEnum(
    _In_ PDEVICE_NODE DeviceNode,
    _In_ BOOLEAN Add);

PDEVICE_NODE
PipAllocateDeviceNode(
    _In_ PDEVICE_OBJECT PhysicalDeviceObject);

CODE_SEG("INIT")
VOID
PiInitializeNotifications(VOID);

CODE_SEG("PAGE")
VOID
PiNotifyDeviceInterfaceChange(
    _In_ LPCGUID Event,
    _In_ LPCGUID InterfaceClassGuid,
    _In_ PUNICODE_STRING SymbolicLinkName);

CODE_SEG("PAGE")
VOID
PiNotifyHardwareProfileChange(
    _In_ LPCGUID Event);

CODE_SEG("PAGE")
VOID
PiNotifyTargetDeviceChange(
    _In_ LPCGUID Event,
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_opt_ PTARGET_DEVICE_CUSTOM_NOTIFICATION CustomNotification);

CODE_SEG("PAGE")
NTSTATUS
PiNotifyTargetDeviceQueryRemove(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Out_ PDRIVER_OBJECT *VetoDriver);

BOOLEAN
PiProcessChildrenParallel(
    _In_ PDEVICE_NODE Parent);

BOOLEAN
PiRunParallel(
    _In_ PVOID *Items,
    _In_ LONG Count,
    _In_ PPI_PARALLEL_ROUTINE Routine);

NTSTATUS
NTAPI
IopInitiatePnpIrp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIO_STATUS_BLOCK IoStatusBlock,
    _In_ UCHAR MinorFunction,
    _In_ PIO_STACK_LOCATION Stack);

NTSTATUS
NTAPI
IopQueryDeviceCapabilities(
    PDEVICE_NODE DeviceNode,
    PDEVICE_CAPABILITIES DeviceCaps);

NTSTATUS
IopSynchronousCall(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIO_STACK_LOCATION IoStackLocation,
    _Out_ PVOID *Information);

NTSTATUS
PiIrpCancelStopDevice(
    _In_ PDEVICE_NODE DeviceNode);

NTSTATUS
PiIrpQueryDeviceRelations(
    _In_ PDEVICE_NODE DeviceNode,
    _In_ DEVICE_RELATION_TYPE Type);

NTSTATUS
PiIrpQueryDeviceText(
    _In_ PDEVICE_NODE DeviceNode,
    _In_ LCID LocaleId,
    _In_ DEVICE_TEXT_TYPE Type,
    _Out_ PWSTR *DeviceText);

NTSTATUS
PiIrpQueryPnPDeviceState(
    _In_ PDEVICE_NODE DeviceNode,
    _Out_ PPNP_DEVICE_STATE DeviceState);

NTSTATUS
PiIrpQueryResourceRequirements(
    _In_ PDEVICE_NODE DeviceNode,
    _Out_ PIO_RESOURCE_REQUIREMENTS_LIST *Resources);

NTSTATUS
PiIrpQueryResources(
    _In_ PDEVICE_NODE DeviceNode,
    _Out_ PCM_RESOURCE_LIST *Resources);

NTSTATUS
PiIrpQueryStopDevice(
    _In_ PDEVICE_NODE DeviceNode);

NTSTATUS
PiIrpStartDevice(
    _In_ PDEVICE_NODE DeviceNode);

NTSTATUS
PiIrpStopDevice(
    _In_ PDEVICE_NODE DeviceNode);

NTSTATUS
PiQueryRemoveDevice(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Out_ PPI_QUERY_REMOVE_DATA RemoveData);

VOID
PiReleaseQueryRemoveData(
    _Inout_ PPI_QUERY_REMOVE_DATA RemoveData);

NTSTATUS
NTAPI
IopCreateDeviceKeyPath(
    _In_ PCUNICODE_STRING RegistryPath,
    _In_ ULONG CreateOptions,
    _Out_ PHANDLE Handle);

NTSTATUS
NTAPI
IopCreateRegistryKeyEx(
    _Out_ PHANDLE Handle,
    _In_opt_ HANDLE BaseHandle,
    _In_ PUNICODE_STRING KeyName,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ ULONG CreateOptions,
    _Out_opt_ PULONG Disposition);

NTSTATUS
NTAPI
IopGetRegistryValue(
    _In_ HANDLE Handle,
    _In_ PWSTR ValueName,
    _Out_ PKEY_VALUE_FULL_INFORMATION *Information);

NTSTATUS
NTAPI
IopOpenRegistryKeyEx(
    PHANDLE KeyHandle,
    HANDLE ParentKey,
    PUNICODE_STRING Name,
    ACCESS_MASK DesiredAccess);

NTSTATUS
NTAPI
PnpRegMultiSzToUnicodeStrings(
    _In_ PKEY_VALUE_FULL_INFORMATION KeyValueInformation,
    _Out_ PUNICODE_STRING *UnicodeStringList,
    _Out_ PULONG UnicodeStringCount);

BOOLEAN
NTAPI
PnpRegSzToString(
    _In_ PWCHAR RegSzData,
    _In_ ULONG RegSzLength,
    _Out_opt_ PUSHORT StringLength);

BOOLEAN
IopResDbCheckDescriptor(
    _In_ PCM_PARTIAL_RESOURCE_DESCRIPTOR Candidate,
    _In_opt_ PDEVICE_NODE CandidateOwner,
    _In_opt_ PVOID Claimant,
    _Out_opt_ PCM_PARTIAL_RESOURCE_DESCRIPTOR Conflicting);

BOOLEAN
IopResDbCheckBootDescriptor(
    _In_ PCM_PARTIAL_RESOURCE_DESCRIPTOR Candidate,
    _In_opt_ PDEVICE_NODE CandidateOwner,
    _In_opt_ PVOID Claimant);

BOOLEAN
IopResDbGetDecodingWindows(
    _In_ PDEVICE_NODE DeviceNode,
    _In_ UCHAR Type,
    _Out_writes_to_(MaxWindows, *WindowCount) PCM_PARTIAL_RESOURCE_DESCRIPTOR Windows,
    _In_ ULONG MaxWindows,
    _Out_ PULONG WindowCount);

NTSTATUS
IopResDbReserveBoot(
    _In_ PDEVICE_NODE DeviceNode,
    _In_opt_ PCM_RESOURCE_LIST BootResources);

BOOLEAN
IopResDbCheckList(
    _In_opt_ PCM_RESOURCE_LIST ResourceList);

VOID
IopResDbEnsureSeeded(VOID);

VOID
IopResDbInitialize(VOID);

VOID
IopResDbRelease(
    _In_ PDEVICE_NODE DeviceNode);

NTSTATUS
IopResDbReplaceClaim(
    _In_opt_ PDEVICE_NODE DeviceNode,
    _In_ PVOID Claimant,
    _In_ PCM_RESOURCE_LIST ResourceList);

NTSTATUS
IopResDbReserve(
    _In_ PDEVICE_NODE DeviceNode,
    _In_opt_ PCM_RESOURCE_LIST ResourceList,
    _Out_opt_ PBOOLEAN Conflict);

NTSTATUS
NTAPI
IopAssignDeviceResources(
    _In_ PDEVICE_NODE DeviceNode);

NTSTATUS
IopFilterResourceRequirements(
    _In_ PDEVICE_NODE DeviceNode);

NTSTATUS
NTAPI
IopFixupResourceListWithRequirements(
    _In_ PIO_RESOURCE_REQUIREMENTS_LIST RequirementsList,
    _Out_ PCM_RESOURCE_LIST *ResourceList,
    _In_opt_ PDEVICE_NODE DeviceNode,
    _In_opt_ PVOID Claimant);

VOID
IopReleaseMessageTokens(
    _In_opt_ PCM_RESOURCE_LIST ResourceList);

VOID
IopReleaseNewMessageVectors(
    _In_opt_ PCM_RESOURCE_LIST ResourceList,
    _In_ ULONG OldCount);

NTSTATUS
IopUpdateControlKeyWithResources(
    _In_ PDEVICE_NODE DeviceNode);

NTSTATUS
PnpRootCreateDeviceObject(
    _Out_ PDEVICE_OBJECT *DeviceObject);

NTSTATUS
PnpRootCreateInstance(
    _In_ PCUNICODE_STRING InstancePath);

NTSTATUS
NTAPI
PnpRootDriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath);

BOOLEAN
PnpRootForgetDevice(
    _In_ PDEVICE_OBJECT DeviceObject);

VOID
PnpRootInitializeDevExtension(VOID);

NTSTATUS
PnpRootRegisterDevice(
    _In_ PDEVICE_OBJECT DeviceObject);

VOID
PnpRootRetireDevice(
    _In_ PDEVICE_OBJECT DeviceObject);

CODE_SEG("INIT")
NTSTATUS
NTAPI
IopInitializePlugPlayServices(VOID);

CODE_SEG("INIT")
NTSTATUS
NTAPI
IopUpdateRootKey(VOID);

VOID
PnpInit2(VOID);

BOOLEAN
NTAPI
PpInitSystem(VOID);

VOID
PiDevNodeAddDrivers(
    _In_ PDEVICE_NODE DeviceNode);

VOID
PiDevNodeStateMachine(
    _In_ PDEVICE_NODE RootNode);

BOOLEAN
PiIsRemovingRenamedStack(VOID);

NTSTATUS
PiPerformSyncDeviceAction(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ DEVICE_ACTION Action);

NTSTATUS
PiPerformSyncDeviceActionEx(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ DEVICE_ACTION Action,
    _Out_opt_ PPI_QUERY_REMOVE_DATA RemoveData);

VOID
PiQueueDeviceAction(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ DEVICE_ACTION Action,
    _In_opt_ PKEVENT CompletionEvent,
    _Out_opt_ NTSTATUS *CompletionStatus);

VOID
PiQueueDeviceActionEx(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ DEVICE_ACTION Action,
    _In_opt_ PKEVENT CompletionEvent,
    _Out_opt_ NTSTATUS *CompletionStatus,
    _Out_opt_ PPI_QUERY_REMOVE_DATA RemoveData);

NTSTATUS
NTAPI
IopInitializeArbiters(VOID);

BOOLEAN
NTAPI
IoPnPDeliverServicePowerSetting(
    _In_ ULONG PowerNotificationCode,
    _In_ ULONG ServiceStatusHandle,
    _In_reads_bytes_(SettingSize) PVOID Setting,
    _In_ ULONG SettingSize);

NTSTATUS
IopQueuePowerEvent(
    _In_ ULONG NotificationCode,
    _In_ ULONG NotificationData,
    _In_reads_bytes_opt_(PayloadSize) const VOID *Payload,
    _In_ ULONG PayloadSize,
    _In_opt_ PLARGE_INTEGER Timeout);

BOOLEAN
IopResDbConflictsWithChosen(
    _In_ PCM_PARTIAL_RESOURCE_DESCRIPTOR Candidate,
    _In_opt_ PDEVICE_NODE CandidateOwner,
    _In_reads_opt_(ChosenCount) PCM_PARTIAL_RESOURCE_DESCRIPTOR Chosen,
    _In_ ULONG ChosenCount);

NTSTATUS
PiSetInstanceProperty(
    _In_ HANDLE InstanceKey,
    _In_ CONST DEVPROPKEY *PropertyKey,
    _In_ DEVPROPTYPE Type,
    _In_ ULONG Size,
    _In_reads_bytes_opt_(Size) PVOID Data);

NTSTATUS
PnpRootCreateDevice(
    _In_ PCUNICODE_STRING ServiceName,
    _In_reads_bytes_opt_(CompatibleIdsSize) PCWSTR CompatibleIds,
    _In_ ULONG CompatibleIdsSize,
    _Out_ PDEVICE_OBJECT *PhysicalDeviceObject,
    _Out_opt_ PUNICODE_STRING FullInstancePath);

FORCEINLINE
BOOLEAN
IopIs32BitFileIo(PFILE_OBJECT FileObject, KPROCESSOR_MODE RequestorMode)
{
#ifdef _WIN64
    return RequestorMode != KernelMode &&
           FileObject != NULL &&
           !(FileObject->Flags & FO_SYNCHRONOUS_IO) &&
           PsGetCurrentProcessWow64Process() != NULL;
#else
    UNREFERENCED_PARAMETER(FileObject);
    UNREFERENCED_PARAMETER(RequestorMode);
    return FALSE;
#endif
}

FORCEINLINE
VOID
IopWriteIoStatusBlock(PIO_STATUS_BLOCK Destination,
                      const IO_STATUS_BLOCK *Source,
                      BOOLEAN Is32Bit)
{
#ifdef _WIN64
    if (Is32Bit)
    {
        PIO_STATUS_BLOCK32 IoStatusBlock32 = (PIO_STATUS_BLOCK32)Destination;
        IoStatusBlock32->Status = Source->Status;
        IoStatusBlock32->Information = (ULONG)Source->Information;
        return;
    }
#else
    UNREFERENCED_PARAMETER(Is32Bit);
#endif
    *Destination = *Source;
}

FORCEINLINE
PIO_STATUS_BLOCK
IopGetWow64IoStatusBlock(PIO_STATUS_BLOCK IoStatusBlock)
{
    PIO_STATUS_BLOCK Wow64IoStatusBlock;

    ProbeForRead(IoStatusBlock, sizeof(*IoStatusBlock), sizeof(ULONG));
    Wow64IoStatusBlock = IoStatusBlock->Pointer;
    ProbeForWrite(Wow64IoStatusBlock, sizeof(NTSTATUS) + sizeof(ULONG), sizeof(ULONG));

    return Wow64IoStatusBlock;
}

#include "io_x.h"
