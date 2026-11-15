/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Object manager internals
 * COPYRIGHT:   Copyright Alex Ionescu <alex.ionescu@reactos.org>
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define GENERIC_ACCESS                                  \
    (GENERIC_READ    |                                  \
     GENERIC_WRITE   |                                  \
     GENERIC_EXECUTE |                                  \
     GENERIC_ALL)

#define OBJ_PROTECT_CLOSE                               0x01

#define OBJ_AUDIT_OBJECT_CLOSE                          0x04

#define OBJ_HANDLE_ATTRIBUTES                           (OBJ_PROTECT_CLOSE |\
                                                         OBJ_INHERIT |      \
                                                         OBJ_AUDIT_OBJECT_CLOSE)

#define ObpAccessProtectCloseBit 0x02000000L

#define OB_HANDLE_EXCEPTIONS_ENABLED 0x2

#ifdef _WIN64
#define OBP_HANDLE_REFERENCE_INCREMENT 0x8000UL
#else
#define OBP_HANDLE_REFERENCE_INCREMENT 0x20UL
#endif

#ifdef _WIN64
#define KERNEL_HANDLE_FLAG 0xFFFFFFFF80000000ULL
#else
#define KERNEL_HANDLE_FLAG 0x80000000
#endif

#define ObpIsKernelHandle(Handle, ProcessorMode)        \
    ((((ULONG_PTR)(Handle) & KERNEL_HANDLE_FLAG) == KERNEL_HANDLE_FLAG) && \
     ((ProcessorMode) == KernelMode) && \
     ((Handle) != NtCurrentProcess()) && \
     ((Handle) != NtCurrentThread()))

#define ObKernelHandleToHandle(Handle)                  \
    (HANDLE)((ULONG_PTR)(Handle) & ~KERNEL_HANDLE_FLAG)

#define ObMarkHandleAsKernelHandle(Handle)              \
    (HANDLE)((ULONG_PTR)(Handle) | KERNEL_HANDLE_FLAG)

#define ObpGetHandleObject(x)                           \
    ((POBJECT_HEADER)((ULONG_PTR)x->Object & ~OBJ_HANDLE_ATTRIBUTES))

#define ObpGetHeaderForSd(x) \
    CONTAINING_RECORD((x), SECURITY_DESCRIPTOR_HEADER, SecurityDescriptor)

#define ObpGetHeaderForEntry(x) \
    CONTAINING_RECORD((x), SECURITY_DESCRIPTOR_HEADER, Link)

typedef struct _OBP_SET_HANDLE_ATTRIBUTES_CONTEXT
{
    KPROCESSOR_MODE PreviousMode;
    OBJECT_HANDLE_ATTRIBUTE_INFORMATION Information;
} OBP_SET_HANDLE_ATTRIBUTES_CONTEXT, *POBP_SET_HANDLE_ATTRIBUTES_CONTEXT;

typedef struct _OBP_FIND_HANDLE_DATA
{
    POBJECT_HEADER ObjectHeader;
    POBJECT_TYPE ObjectType;
    POBJECT_HANDLE_INFORMATION HandleInformation;
} OBP_FIND_HANDLE_DATA, *POBP_FIND_HANDLE_DATA;

typedef struct _SECURITY_DESCRIPTOR_HEADER
{
    LIST_ENTRY Link;
    ULONG RefCount;
    ULONG FullHash;
    QUAD DECLSPEC_ALIGN(MEMORY_ALLOCATION_ALIGNMENT) SecurityDescriptor;
} SECURITY_DESCRIPTOR_HEADER, *PSECURITY_DESCRIPTOR_HEADER;

C_ASSERT((FIELD_OFFSET(SECURITY_DESCRIPTOR_HEADER, SecurityDescriptor) & (MEMORY_ALLOCATION_ALIGNMENT - 1)) == 0);

typedef struct _OB_SD_CACHE_LIST
{
    EX_PUSH_LOCK PushLock;
    LIST_ENTRY Head;
} OB_SD_CACHE_LIST, *POB_SD_CACHE_LIST;

typedef union
{
    WCHAR Name[sizeof(ULARGE_INTEGER) / sizeof(WCHAR)];
    ULARGE_INTEGER Alignment;
} ALIGNEDNAME;

#define TAG_OB_TEMP_STORAGE 'tSbO'

typedef struct _OB_TEMP_BUFFER
{
    ACCESS_STATE LocalAccessState;
    OBJECT_CREATE_INFORMATION ObjectCreateInfo;
    OBP_LOOKUP_CONTEXT LookupContext;
    AUX_ACCESS_DATA AuxData;
} OB_TEMP_BUFFER, *POB_TEMP_BUFFER;

typedef struct _OBP_HANDLE_CALLBACK_ITEM
{
    struct _OBP_CALLBACK_ENTRY *Entry;
    PVOID CallContext;
} OBP_HANDLE_CALLBACK_ITEM, *POBP_HANDLE_CALLBACK_ITEM;

typedef struct _OBP_HANDLE_CALLBACK_STATE
{
    ULONG Count;
    OB_OPERATION Operation;
    POBJECT_TYPE ObjectType;
    PVOID Object;
    BOOLEAN PreCalled;
    BOOLEAN KernelHandle;
    POBP_HANDLE_CALLBACK_ITEM Items;
    OBP_HANDLE_CALLBACK_ITEM InlineItems[4];
} OBP_HANDLE_CALLBACK_STATE, *POBP_HANDLE_CALLBACK_STATE;

extern ULONG ObpTraceLevel;

extern KEVENT ObpDefaultObject;

extern KGUARDED_MUTEX ObpDeviceMapLock;

extern POBJECT_TYPE ObpTypeObjectType;

extern POBJECT_TYPE ObpDirectoryObjectType;

extern POBJECT_TYPE ObpSymbolicLinkObjectType;

extern POBJECT_TYPE ObTypeIndexTable[256];

extern UCHAR ObHeaderCookie;

extern UCHAR ObpInfoMaskToOffset[256];

extern POBJECT_DIRECTORY ObpRootDirectoryObject;

extern POBJECT_DIRECTORY ObpTypeDirectoryObject;

extern PHANDLE_TABLE ObpKernelHandleTable;

extern WORK_QUEUE_ITEM ObpReaperWorkItem;

extern volatile PVOID ObpReaperList;

extern GENERAL_LOOKASIDE ObpNameBufferLookasideList, ObpCreateInfoLookasideList;

extern BOOLEAN IoCountOperations;

extern ALIGNEDNAME ObpDosDevicesShortNamePrefix;

extern ALIGNEDNAME ObpDosDevicesShortNameRoot;

extern UNICODE_STRING ObpDosDevicesShortName;

extern WCHAR ObpUnsecureGlobalNamesBuffer[128];

extern ULONG ObpUnsecureGlobalNamesLength;

extern ULONG ObpCaseInsensitive;

extern ULONG ObpObjectSecurityMode;

extern ULONG ObpProtectionMode;

extern ULONG ObpLUIDDeviceMapsDisabled;

extern ULONG ObpLUIDDeviceMapsEnabled;

NTSTATUS
NTAPI
ObpBeginHandleCallbacks(
    _Out_ POBP_HANDLE_CALLBACK_STATE State,
    _In_ POBJECT_TYPE ObjectType,
    _In_ OB_OPERATION Operation);

VOID
NTAPI
ObpCallPreHandleCallbacks(
    _Inout_ POBP_HANDLE_CALLBACK_STATE State,
    _In_ PVOID Object,
    _In_ BOOLEAN KernelHandle,
    _Inout_ PACCESS_MASK DesiredAccess,
    _In_opt_ PEPROCESS SourceProcess,
    _In_opt_ PEPROCESS TargetProcess);

LONG
NTAPI
ObpCompareAltitude(
    _In_ PCUNICODE_STRING First,
    _In_ PCUNICODE_STRING Second);

VOID
NTAPI
ObpEndHandleCallbacks(
    _Inout_ POBP_HANDLE_CALLBACK_STATE State,
    _In_ NTSTATUS ReturnStatus,
    _In_ ACCESS_MASK GrantedAccess);

BOOLEAN
NTAPI
ObpIsValidAltitude(
    _In_ PCUNICODE_STRING Altitude);

VOID
NTAPI
ObDereferenceDeviceMap(
    _In_ PEPROCESS Process);

VOID
NTAPI
ObInheritDeviceMap(
    _In_ PEPROCESS Parent,
    _In_ PEPROCESS Process);

ULONG
NTAPI
ObIsLUIDDeviceMapsEnabled(VOID);

NTSTATUS
NTAPI
ObQueryDeviceMapInformation(
    _In_opt_ PEPROCESS Process,
    _Out_ PPROCESS_DEVICEMAP_INFORMATION DeviceMapInfo,
    _In_ ULONG Flags);

NTSTATUS
NTAPI
ObSetDeviceMap(
    _In_ PEPROCESS Process,
    _In_ HANDLE DirectoryHandle);

NTSTATUS
NTAPI
ObSetDirectoryDeviceMap(
    _Out_ PDEVICE_MAP * DeviceMap,
    _In_ HANDLE DirectoryHandle);

VOID
FASTCALL
ObfDereferenceDeviceMap(
    _In_ PDEVICE_MAP DeviceMap);

PDEVICE_MAP
NTAPI
ObpReferenceDeviceMap(VOID);

VOID
NTAPI
ObpCloseDirectoryObject(
    _In_opt_ PEPROCESS Process,
    _In_ PVOID Object,
    _In_ ULONG_PTR ProcessHandleCount,
    _In_ ULONG_PTR SystemHandleCount);

VOID
NTAPI
ObpDeleteDirectoryObject(
    _In_ PVOID Object);

BOOLEAN
NTAPI
ObpDeleteEntryDirectory(
    _In_ POBP_LOOKUP_CONTEXT Context);

BOOLEAN
NTAPI
ObpInsertEntryDirectory(
    _In_ POBJECT_DIRECTORY Parent,
    _In_ POBP_LOOKUP_CONTEXT Context,
    _In_ POBJECT_HEADER ObjectHeader);

PVOID
NTAPI
ObpLookupEntryDirectory(
    _In_ POBJECT_DIRECTORY Directory,
    _In_ PUNICODE_STRING Name,
    _In_ ULONG Attributes,
    _In_ BOOLEAN SearchShadow,
    _In_ POBP_LOOKUP_CONTEXT Context);

VOID
NTAPI
ObClearProcessHandleTable(
    _In_ PEPROCESS Process);

VOID
NTAPI
ObDereferenceProcessHandleTable(
    _In_ PEPROCESS Process);

NTSTATUS
NTAPI
ObDuplicateObject(
    _In_ PEPROCESS SourceProcess,
    _In_ HANDLE SourceHandle,
    _In_opt_ PEPROCESS TargetProcess,
    _In_opt_ PHANDLE TargetHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ ULONG HandleAttributes,
    _In_ ULONG Options,
    _In_ KPROCESSOR_MODE PreviousMode);

ULONG
NTAPI
ObGetProcessHandleCount(
    _In_ PEPROCESS Process);

NTSTATUS
NTAPI
ObInitProcess(
    _In_opt_ PEPROCESS Parent,
    _In_ PEPROCESS Process,
    _In_opt_ const HANDLE *HandleList,
    _In_ SIZE_T HandleCount);

VOID
NTAPI
ObKillProcess(
    _In_ PEPROCESS Process);

PHANDLE_TABLE
NTAPI
ObReferenceProcessHandleTable(
    _In_ PEPROCESS Process);

BOOLEAN
NTAPI
ObpQueryProcessGrantedAccess(
    _In_ PEPROCESS Process,
    _In_ PVOID Object,
    _Out_ PACCESS_MASK GrantedAccess);

VOID
NTAPI
ObpReferenceObjectByHandleEntry(
    _In_ PHANDLE_TABLE HandleTable,
    _In_ HANDLE Handle,
    _In_ PHANDLE_TABLE_ENTRY HandleTableEntry,
    _In_ POBJECT_HEADER ObjectHeader);

VOID
NTAPI
ObFreeObjectCreateInfoBuffer(
    _In_ POBJECT_CREATE_INFORMATION ObjectCreateInfo);

NTSTATUS
NTAPI
ObpCaptureObjectCreateInformation(
    _In_ POBJECT_ATTRIBUTES ObjectAttributes,
    _In_ KPROCESSOR_MODE AccessMode,
    _In_ KPROCESSOR_MODE CreatorMode,
    _In_ BOOLEAN AllocateFromLookaside,
    _In_ POBJECT_CREATE_INFORMATION ObjectCreateInfo,
    _Out_ PUNICODE_STRING ObjectName);

NTSTATUS
NTAPI
ObpCaptureObjectName(
    _In_ PUNICODE_STRING CapturedName,
    _In_ PUNICODE_STRING ObjectName,
    _In_ KPROCESSOR_MODE AccessMode,
    _In_ BOOLEAN AllocateFromLookaside);

VOID
NTAPI
ObpDeleteObject(
    _In_ PVOID Object,
    _In_ BOOLEAN CalledFromWorkerThread);

VOID
NTAPI
ObpDeleteObjectType(
    _In_ PVOID Object);

VOID
NTAPI
ObpFreeObjectNameBuffer(
    _In_ PUNICODE_STRING Name);

VOID
NTAPI
ObpReapObject(
    _In_ PVOID Unused);

VOID
FASTCALL
ObpSetPermanentObject(
    _In_ PVOID ObjectBody,
    _In_ BOOLEAN Permanent);

CODE_SEG("INIT")
NTSTATUS
NTAPI
ObpCreateDosDevicesDirectory(VOID);

VOID
NTAPI
ObpDeleteNameCheck(
    _In_ PVOID Object);

NTSTATUS
NTAPI
ObpLookupObjectName(
    _In_opt_ HANDLE RootHandle,
    _Inout_ PUNICODE_STRING ObjectName,
    _In_ ULONG Attributes,
    _In_ POBJECT_TYPE ObjectType,
    _In_ KPROCESSOR_MODE AccessMode,
    _Inout_ PVOID ParseContext,
    _In_opt_ PSECURITY_QUALITY_OF_SERVICE SecurityQos,
    _In_opt_ PVOID InsertObject,
    _Inout_ PACCESS_STATE AccessState,
    _Out_ POBP_LOOKUP_CONTEXT LookupContext,
    _Out_ PVOID *FoundObject);

VOID
NTAPI
ObpInitializePrivateNamespaces(VOID);

VOID
NTAPI
ObpRemovePrivateNamespace(
    _In_ POBJECT_DIRECTORY Directory);

LONG
FASTCALL
ObDereferenceObjectEx(
    _In_ PVOID Object,
    _In_ LONG Count);

VOID
FASTCALL
ObFastDereferenceObject(
    _In_ PEX_FAST_REF FastRef,
    _In_ PVOID Object);

PVOID
FASTCALL
ObFastReferenceObject(
    _In_ PEX_FAST_REF FastRef);

PVOID
FASTCALL
ObFastReferenceObjectLocked(
    _In_ PEX_FAST_REF FastRef);

PVOID
FASTCALL
ObFastReplaceObject(
    _In_ PEX_FAST_REF FastRef,
    _In_ PVOID Object);

VOID
FASTCALL
ObInitializeFastReference(
    _In_ PEX_FAST_REF FastRef,
    _In_ PVOID Object);

NTSTATUS
NTAPI
ObReferenceFileObjectForWrite(
    _In_ HANDLE Handle,
    _In_ KPROCESSOR_MODE AccessMode,
    _Out_ PFILE_OBJECT *FileObject,
    _Out_ POBJECT_HANDLE_INFORMATION HandleInformation);

LONG
FASTCALL
ObReferenceObjectEx(
    _In_ PVOID Object,
    _In_ LONG Count);

BOOLEAN
FASTCALL
ObReferenceObjectSafe(
    _In_ PVOID Object);

VOID
NTAPI
ObpCheckInvalidHandleReference(
    _In_ HANDLE Handle,
    _In_ KPROCESSOR_MODE AccessMode);

BOOLEAN
NTAPI
ObpIsStrictHandleCheckingEnabled(VOID);

CODE_SEG("INIT")
NTSTATUS
NTAPI
ObpInitSdCache(VOID);

PSECURITY_DESCRIPTOR
NTAPI
ObpReferenceSecurityDescriptor(
    _In_ POBJECT_HEADER ObjectHeader);

NTSTATUS
NTAPI
ObAssignObjectSecurityDescriptor(
    _In_ PVOID Object,
    _In_opt_ PSECURITY_DESCRIPTOR SecurityDescriptor,
    _In_ POOL_TYPE PoolType);

BOOLEAN
NTAPI
ObCheckCreateObjectAccess(
    _In_ PVOID Object,
    _In_ ACCESS_MASK CreateAccess,
    _In_ PACCESS_STATE AccessState,
    _In_ PUNICODE_STRING ComponentName,
    _In_ BOOLEAN LockHeld,
    _In_ KPROCESSOR_MODE AccessMode,
    _Out_ PNTSTATUS AccessStatus);

BOOLEAN
NTAPI
ObCheckObjectAccess(
    _In_ PVOID Object,
    _Inout_ PACCESS_STATE AccessState,
    _In_ BOOLEAN LockHeld,
    _In_ KPROCESSOR_MODE AccessMode,
    _Out_ PNTSTATUS ReturnedStatus);

NTSTATUS
NTAPI
ObDeassignSecurity(
    _Inout_ PSECURITY_DESCRIPTOR *SecurityDescriptor);

NTSTATUS
NTAPI
ObQuerySecurityDescriptorInfo(
    _In_ PVOID Object,
    _In_ PSECURITY_INFORMATION SecurityInformation,
    _Out_ PSECURITY_DESCRIPTOR SecurityDescriptor,
    _Inout_ PULONG Length,
    _In_ PSECURITY_DESCRIPTOR *OutputSecurityDescriptor);

NTSTATUS
NTAPI
ObSetSecurityDescriptorInfo(
    _In_ PVOID Object,
    _In_ PSECURITY_INFORMATION SecurityInformation,
    _Inout_ PSECURITY_DESCRIPTOR SecurityDescriptor,
    _Inout_ PSECURITY_DESCRIPTOR *OutputSecurityDescriptor,
    _In_ POOL_TYPE PoolType,
    _In_ PGENERIC_MAPPING GenericMapping);

BOOLEAN
NTAPI
ObpCheckObjectReference(
    _In_ PVOID Object,
    _Inout_ PACCESS_STATE AccessState,
    _In_ BOOLEAN LockHeld,
    _In_ KPROCESSOR_MODE AccessMode,
    _Out_ PNTSTATUS AccessStatus);

BOOLEAN
NTAPI
ObpCheckTraverseAccess(
    _In_ PVOID Object,
    _In_ ACCESS_MASK TraverseAccess,
    _In_opt_ PACCESS_STATE AccessState,
    _In_ BOOLEAN LockHeld,
    _In_ KPROCESSOR_MODE AccessMode,
    _Out_ PNTSTATUS AccessStatus);

CODE_SEG("INIT")
BOOLEAN
NTAPI
ObInitSystem(VOID);

VOID
NTAPI
ObpCreateSymbolicLinkName(
    _In_ POBJECT_SYMBOLIC_LINK SymbolicLink);

VOID
NTAPI
ObpDeleteSymbolicLink(
    _In_ PVOID ObjectBody);

VOID
NTAPI
ObpDeleteSymbolicLinkName(
    _In_ POBJECT_SYMBOLIC_LINK SymbolicLink);

NTSTATUS
NTAPI
ObpParseSymbolicLink(
    _In_ PVOID ParsedObject,
    _In_ PVOID ObjectType,
    _Inout_ PACCESS_STATE AccessState,
    _In_ KPROCESSOR_MODE AccessMode,
    _In_ ULONG Attributes,
    _Inout_ PUNICODE_STRING FullPath,
    _Inout_ PUNICODE_STRING RemainingName,
    _Inout_opt_ PVOID Context,
    _In_opt_ PSECURITY_QUALITY_OF_SERVICE SecurityQos,
    _Out_ PVOID *NextObject);

NTSTATUS
NTAPI
ObpParseSymbolicLinkEx(
    _In_ PVOID ParsedObject,
    _In_ PVOID ObjectType,
    _Inout_ PACCESS_STATE AccessState,
    _In_ KPROCESSOR_MODE AccessMode,
    _In_ ULONG Attributes,
    _Inout_ PUNICODE_STRING FullPath,
    _Inout_ PUNICODE_STRING RemainingName,
    _Inout_opt_ PVOID Context,
    _In_opt_ PSECURITY_QUALITY_OF_SERVICE SecurityQos,
    _In_ POB_EXTENDED_PARSE_PARAMETERS ExtendedParameters,
    _Out_ PVOID *NextObject);

PVOID
ObpGetObjectWaitObject(
    POBJECT_TYPE ObjectType,
    PVOID Object);

#include "ob_x.h"
