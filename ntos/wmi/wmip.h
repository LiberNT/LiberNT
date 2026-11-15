
#pragma once

extern POBJECT_TYPE WmipGuidObjectType;
extern GENERIC_MAPPING WmipGenericMapping;

#define GUID_STRING_LENGTH 36

typedef struct _WMIP_IRP_CONTEXT
{
    LIST_ENTRY GuidObjectListHead;
} WMIP_IRP_CONTEXT, *PWMIP_IRP_CONTEXT;

typedef struct _WMIP_GUID_OBJECT
{
    KEVENT Event;
    GUID Guid;
    PIRP Irp;
    LIST_ENTRY IrpLink;
    LIST_ENTRY NotificationLink;
    WMI_NOTIFICATION_CALLBACK NotificationCallback;
    PVOID NotificationContext;
    BOOLEAN EventConsumer;
} WMIP_GUID_OBJECT, *PWMIP_GUID_OBJECT;

extern EX_PUSH_LOCK WmipNotificationLock;


_Function_class_(DRIVER_INITIALIZE)
_IRQL_requires_same_
NTSTATUS
NTAPI
WmipDriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath);

NTSTATUS
NTAPI

NTSTATUS
NTAPI
WmipOpenGuidObject(
    _In_ LPCGUID Guid,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ KPROCESSOR_MODE AccessMode,
    _Out_ PHANDLE OutGuidObjectHandle,
    _Outptr_ PVOID *OutGuidObject);

NTSTATUS
NTAPI
WmipOpenGuidObjectByName(
    _In_ POBJECT_ATTRIBUTES ObjectAttributes,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ KPROCESSOR_MODE AccessMode,
    _Out_ PHANDLE OutGuidObjectHandle,
    _Outptr_ PVOID *OutGuidObject);

NTAPI

NTSTATUS
NTAPI

NTAPI

VOID
NTAPI
WmipGuidObjectCreated(
    _In_ LPCGUID Guid);

VOID
NTAPI
WmipGuidObjectDeleted(
    _In_ PWMIP_GUID_OBJECT GuidObject);

VOID
NTAPI

BOOLEAN
WmipIsGuidRegistered(
    _In_ LPCGUID Guid);

NTAPI

NTSTATUS
NTAPI
WmipQueryAllData(
    _In_ LPCGUID Guid,
    _Inout_ PULONG InOutBufferSize,
    _Out_opt_ PVOID OutBuffer);

NTSTATUS
NTAPI
WmipQuerySingleInstance(
    _In_ LPCGUID Guid,
    _In_ PCUNICODE_STRING InstanceName,
    _Inout_ PULONG InOutBufferSize,
    _Out_opt_ PVOID OutBuffer);

NTAPI

NTAPI

NTSTATUS
NTAPI

NTAPI

NTSTATUS
    _In_reads_bytes_opt_(InBufferLen) PVOID InBuffer,
    _In_ ULONG InBufferLen,
    _Out_writes_bytes_opt_(OutBufferLen) PVOID OutBuffer,
    _In_ ULONG OutBufferLen,
    _Out_opt_ PULONG ReturnSize,
    _In_ KPROCESSOR_MODE PreviousMode);

NTSTATUS
    _In_reads_bytes_opt_(InBufferLen) PVOID InBuffer,
    _In_ ULONG InBufferLen,
    _Out_writes_bytes_opt_(OutBufferLen) PVOID OutBuffer,
    _In_ ULONG OutBufferLen,
    _Out_opt_ PULONG ReturnSize,
    _In_ KPROCESSOR_MODE PreviousMode);

NTSTATUS
    _In_reads_bytes_opt_(InBufferLen) PVOID InBuffer,
    _In_ ULONG InBufferLen,
    _Out_writes_bytes_opt_(OutBufferLen) PVOID OutBuffer,
    _In_ ULONG OutBufferLen,
    _Out_opt_ PULONG ReturnSize,
    _In_ KPROCESSOR_MODE PreviousMode);

NTSTATUS
