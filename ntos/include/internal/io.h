/*
 */



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

































VOID

VOID

VOID

NTSTATUS

NTSTATUS


BOOLEAN

VOID


NTAPI


NTSTATUS
NTAPI

FASTCALL

CODE_SEG("INIT")

NTSTATUS


NTSTATUS



NTAPI

BOOLEAN

VOID

NTSTATUS

NTSTATUS

NTSTATUS
NTAPI


NTSTATUS

NTSTATUS
NTAPI

NTSTATUS
NTAPI

NTSTATUS
NTAPI

NTAPI

NTAPI

NTSTATUS
NTAPI

NTAPI


NTSTATUS

CODE_SEG("INIT")
NTSTATUS
NTAPI
IopCreateArcNames(

CODE_SEG("INIT")
NTSTATUS
NTAPI
IopCreateArcNamesDisk(

CODE_SEG("INIT")
NTSTATUS
NTAPI
IopReassignSystemRoot(

BOOLEAN
IopVerifyDiskSignature(
    _In_ PDRIVE_LAYOUT_INFORMATION_EX DriveLayout,
    _In_ PARC_DISK_SIGNATURE ArcDiskSignature,
    _Out_ PULONG Signature);






VOID
NTAPI

VOID

VOID
NTAPI

NTAPI

NTSTATUS
NTAPI



NTSTATUS
NTAPI

VOID
NTAPI

VOID
NTAPI

NTAPI

NTAPI

NTAPI

NTAPI

VOID
NTAPI

NTAPI

VOID
NTAPI

NTAPI

NTSTATUS
NTAPI

NTAPI

NTAPI

NTAPI

VOID
NTAPI

VOID
NTAPI

NTAPI

NTAPI

NTAPI

VOID
NTAPI

BOOLEAN

NTSTATUS
NTAPI

NTSTATUS
NTAPI

NTSTATUS

NTSTATUS

NTSTATUS


BOOLEAN
    _In_ PDEVICE_OBJECT DeviceObject);

NTSTATUS
NTAPI



VOID
NTAPI

BOOLEAN

VOID

NTAPI

VOID


VOID
NTAPI

VOID
NTAPI

VOID
NTAPI

NTAPI

NTSTATUS
NTAPI

NTAPI

NTSTATUS
NTAPI

VOID
NTAPI

NTSTATUS
NTAPI

NTAPI

NTAPI

VOID
NTAPI

NTSTATUS
NTAPI

VOID


NTSTATUS


NTSTATUS

NTSTATUS

NTSTATUS
NTAPI


FASTCALL

NTAPI



VOID

VOID

VOID
NTAPI

VOID

VOID

VOID



VOID

VOID


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

NTSTATUS

NTSTATUS

NTSTATUS

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

VOID
NTAPI
    _In_ PDEVICE_NODE DeviceNode,


NTSTATUS
NTAPI



NTSTATUS
NTAPI





NTSTATUS
NTAPI

NTAPI

#include "io_x.h"
