/*
 * wdm.h
 *
 * Windows NT WDM Driver Developer Kit
 *
 * This file is part of the ReactOS DDK package.
 *
 * Contributors:
 *   Amine Khaldi (amine.khaldi@reactos.org)
 *   Timo Kreuzer (timo.kreuzer@reactos.org)
 *
 * THIS SOFTWARE IS NOT COPYRIGHTED
 *
 * This source code is offered for use in the public domain. You may
 * use, modify or distribute it freely.
 *
 * This code is distributed in the hope that it will be useful but
 * WITHOUT ANY WARRANTY. ALL WARRANTIES, EXPRESS OR IMPLIED ARE HEREBY
 * DISCLAIMED. This includes but is not limited to warranties of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 */
#pragma once

#ifndef _WDMDDK_
#define _WDMDDK_

#define WDM_MAJORVERSION        0x06
#define WDM_MINORVERSION        0x00

/* Included via ntddk.h? */
#ifndef _NTDDK_
#define _NTDDK_
#define _WDM_INCLUDED_
#define _DDK_DRIVER_
#define NO_INTERLOCKED_INTRINSICS
#endif /* _NTDDK_ */

/* Dependencies */
#define NT_INCLUDED
#include <excpt.h>
#include <ntdef.h>
#include <ntstatus.h>
#include <kernelspecs.h>
#include <ntiologc.h>
#include <suppress.h>

#ifndef GUID_DEFINED
#include <guiddef.h>
#endif

#ifdef _MAC
#ifndef _INC_STRING
#include <string.h>
#endif /* _INC_STRING */
#else
#include <string.h>
#endif /* _MAC */

#ifndef _KTMTYPES_
typedef GUID UOW, *PUOW;
#endif

typedef GUID *PGUID;

#if (NTDDI_VERSION >= NTDDI_WINXP)
#include <dpfilter.h>
#endif

#include "intrin.h"

__internal_kernel_driver
__drv_Mode_impl(WDM_INCLUDED)

#ifdef __cplusplus
extern "C" {
#endif

$define(UCHAR=UCHAR)
$define(ULONG=ULONG)
$define(USHORT=USHORT)

#if !defined(_NTHALDLL_) && !defined(_BLDR_)
#define NTHALAPI DECLSPEC_IMPORT
#else
#define NTHALAPI
#endif

/* For ReactOS */
#if !defined(_NTOSKRNL_) && !defined(_BLDR_) && !defined(_NTSYSTEM_)
#define NTKERNELAPI DECLSPEC_IMPORT
#else
#define NTKERNELAPI
#endif

#if defined(_X86_) && !defined(_NTHAL_)
#define _DECL_HAL_KE_IMPORT  DECLSPEC_IMPORT
#elif defined(_X86_)
#define _DECL_HAL_KE_IMPORT
#else
#define _DECL_HAL_KE_IMPORT NTKERNELAPI
#endif

#if defined(_WIN64)
#define POINTER_ALIGNMENT DECLSPEC_ALIGN(8)
#else
#define POINTER_ALIGNMENT
#endif

/* Helper macro to enable gcc's extension */
#ifndef __GNU_EXTENSION
#ifdef __GNUC__
#define __GNU_EXTENSION __extension__
#else
#define __GNU_EXTENSION
#endif
#endif

#if defined(_MSC_VER)

/* Disable some warnings */
#pragma warning(disable:4115) /* Named type definition in parentheses */
#pragma warning(disable:4201) /* Nameless unions and structs */
#pragma warning(disable:4214) /* Bit fields of other types than int */
#pragma warning(disable:4820) /* Padding added, due to alignment requirement */

/* Indicate if #pragma alloc_text() is supported */
#if defined(_M_IX86) || defined(_M_AMD64) || defined(_M_IA64)
#define ALLOC_PRAGMA 1
#endif

/* Indicate if #pragma data_seg() is supported */
#if defined(_M_IX86) || defined(_M_AMD64)
#define ALLOC_DATA_PRAGMA 1
#endif

#endif /* _MSC_VER */

/* These macros are used to create aliases for imported data. We need to do
   this to have declarations that are compatible with MS DDK */
#ifdef _M_IX86
#define __SYMBOL(_Name) "_"#_Name
#define __IMPORTSYMBOL(_Name) "__imp__"#_Name
#define __IMPORTNAME(_Name) __imp__##_Name
#else
#define __SYMBOL(_Name) #_Name
#define __IMPORTSYMBOL(_Name) "__imp_"#_Name
#define __IMPORTNAME(_Name) __imp_##_Name
#endif
#if defined(_MSC_VER) && !defined(__clang__)
#define __CREATE_NTOS_DATA_IMPORT_ALIAS(_Name) \
    __pragma(comment(linker, "/alternatename:"__SYMBOL(_Name) "=" __IMPORTSYMBOL(_Name)))
#else /* !_MSC_VER */
#ifndef __STRINGIFY
#define __STRINGIFY(_exp) #_exp
#endif
#define _Pragma_redefine_extname(_Name, _Target) _Pragma(__STRINGIFY(redefine_extname _Name _Target))
#define __CREATE_NTOS_DATA_IMPORT_ALIAS(_Name) \
    _Pragma_redefine_extname(_Name,__IMPORTNAME(_Name))
#endif

#if defined(_WIN64)
#if !defined(USE_DMA_MACROS) && !defined(_NTHAL_)
#define USE_DMA_MACROS
#endif
#if !defined(NO_LEGACY_DRIVERS) && !defined(__REACTOS__)
#define NO_LEGACY_DRIVERS
#endif
#endif /* defined(_WIN64) */

/* Forward declarations */
struct _IRP;
struct _MDL;
struct _KAPC;
struct _KDPC;
struct _FILE_OBJECT;
struct _DMA_ADAPTER;
struct _DEVICE_OBJECT;
struct _DRIVER_OBJECT;
struct _IO_STATUS_BLOCK;
struct _DEVICE_DESCRIPTION;
struct _SCATTER_GATHER_LIST;
struct _DRIVE_LAYOUT_INFORMATION;
struct _COMPRESSED_DATA_INFO;
struct _IO_RESOURCE_DESCRIPTOR;

/* Structures not exposed to drivers */
typedef struct _OBJECT_TYPE *POBJECT_TYPE;
typedef struct _CALLBACK_OBJECT *PCALLBACK_OBJECT;
typedef struct _EPROCESS *PEPROCESS;
typedef struct _ETHREAD *PETHREAD;
typedef struct _EJOB *PESILO;
typedef struct _IO_TIMER *PIO_TIMER;
typedef struct _KINTERRUPT *PKINTERRUPT;
typedef struct _KPROCESS *PKPROCESS, *PRKPROCESS;
typedef struct _KTHREAD *PKTHREAD, *PRKTHREAD;
typedef struct _CONTEXT *PCONTEXT;

#if defined(USE_DMA_MACROS) && !defined(_NTHAL_)
typedef struct _DMA_ADAPTER *PADAPTER_OBJECT;
#elif defined(_WDM_INCLUDED_)
typedef struct _DMA_ADAPTER *PADAPTER_OBJECT;
#else
typedef struct _ADAPTER_OBJECT *PADAPTER_OBJECT;
#endif

#ifndef DEFINE_GUIDEX
#ifdef _MSC_VER
#define DEFINE_GUIDEX(name) EXTERN_C const CDECL GUID name
#else
#define DEFINE_GUIDEX(name) EXTERN_C const GUID name
#endif
#endif /* DEFINE_GUIDEX */

#ifndef STATICGUIDOF
#define STATICGUIDOF(guid) STATIC_##guid
#endif

/* GUID Comparison */
#ifndef __IID_ALIGNED__
#define __IID_ALIGNED__
#ifdef __cplusplus
inline int IsEqualGUIDAligned(REFGUID guid1, REFGUID guid2)
{
    return ( (*(PLONGLONG)(&guid1) == *(PLONGLONG)(&guid2)) &&
             (*((PLONGLONG)(&guid1) + 1) == *((PLONGLONG)(&guid2) + 1)) );
}
#else
#define IsEqualGUIDAligned(guid1, guid2) \
           ( (*(PLONGLONG)(guid1) == *(PLONGLONG)(guid2)) && \
             (*((PLONGLONG)(guid1) + 1) == *((PLONGLONG)(guid2) + 1)) )
#endif /* __cplusplus */
#endif /* !__IID_ALIGNED__ */


$define (_WDMDDK_)
$include (memaccess.h)
$include (interlocked.h)
$include (rtltypes.h)
$include (ketypes.h)
$include (mmtypes.h)
$include (extypes.h)
$include (setypes.h)
$include (potypes.h)
$include (cmtypes.h)
$include (iotypes.h)
$include (obtypes.h)
$include (pstypes.h)
$include (wmitypes.h)

$include (kdfuncs.h)
$include (kefuncs.h)
$include (rtlfuncs.h)
$include (mmfuncs.h)
$include (sefuncs.h)
$include (cmfuncs.h)
$include (iofuncs.h)
$include (pofuncs.h)
$include (exfuncs.h)
$include (obfuncs.h)
$include (psfuncs.h)
$include (wmifuncs.h)
$include (halfuncs.h)
$include (nttmapi.h)
$include (zwfuncs.h)

#if ((NTDDI_VERSION >= NTDDI_WIN11_GA))

typedef enum _POWER_LIMIT_TYPES {
    PowerLimitContinuous = 0,
    PowerLimitBurst,
    PowerLimitRapid,
    PowerLimitPreemptive,
    PowerLimitPreemptiveOffset,
    PowerLimitTypeMax
} POWER_LIMIT_TYPES, *PPOWER_LIMIT_TYPES;

typedef struct _POWER_LIMIT_ATTRIBUTES {
    POWER_LIMIT_TYPES   Type;
    ULONG               DomainId;
    ULONG               MaxValue;
    ULONG               MinValue;
    ULONG               MinTimeParameter;
    ULONG               MaxTimeParameter;
    ULONG               DefaultACValue;
    ULONG               DefaultDCValue;
    union {
        struct {
            ULONG       SupportTimeParameter : 1;
            ULONG       Reserved : 31;
        };
        ULONG           AsUlong;
    } Flags;
} POWER_LIMIT_ATTRIBUTES, *PPOWER_LIMIT_ATTRIBUTES;

typedef struct _POWER_LIMIT_VALUE {
    POWER_LIMIT_TYPES Type;
    ULONG DomainId;
    ULONG TargetValue;
    ULONG TimeParameter;
} POWER_LIMIT_VALUE, *PPOWER_LIMIT_VALUE;

#define POWER_LIMIT_VALUE_NO_CONTROL            ULONG_MAX

#endif
#if ((NTDDI_VERSION >= NTDDI_WIN8))

_IRQL_requires_max_(APC_LEVEL)
NTKERNELAPI
NTSTATUS
NTAPI
CmCallbackGetKeyObjectIDEx (
    _In_ PLARGE_INTEGER Cookie,
    _In_ PVOID Object,
    _Out_opt_ PULONG_PTR ObjectID,
    _Outptr_opt_ PCUNICODE_STRING *ObjectName,
    _In_ ULONG Flags
    );

_IRQL_requires_max_(APC_LEVEL)
NTKERNELAPI
VOID
NTAPI
CmCallbackReleaseKeyObjectIDEx (
    _In_ PCUNICODE_STRING ObjectName
    );

#endif

typedef struct _BDCB_IMAGE_INFORMATION *PBDCB_IMAGE_INFORMATION;

#if ((NTDDI_VERSION >= NTDDI_WIN11_GA))

_IRQL_requires_max_(APC_LEVEL)
NTKERNELAPI
NTSTATUS
NTAPI
PoCreatePowerLimitRequest (
    _Outptr_ PVOID *PowerLimitRequest,
    _In_ PDEVICE_OBJECT TargetDeviceObject,
    _In_ PDEVICE_OBJECT PolicyDeviceObject,
    _In_ PCOUNTED_REASON_CONTEXT Context
    );

_IRQL_requires_max_(APC_LEVEL)
NTKERNELAPI
NTSTATUS
NTAPI
PoQueryPowerLimitAttributes (
    _Inout_ PVOID PowerLimitRequest,
    _In_ ULONG BufferCount,
    _Inout_updates_opt_(BufferCount) PPOWER_LIMIT_ATTRIBUTES Buffer,
    _Out_ PULONG AttributeCount
    );

_IRQL_requires_max_(APC_LEVEL)
NTKERNELAPI
NTSTATUS
NTAPI
PoSetPowerLimitValue (
    _Inout_ PVOID PowerLimitRequest,
    _In_opt_ PCOUNTED_REASON_CONTEXT Reason,
    _In_ ULONG ValueCount,
    _In_reads_(ValueCount) PPOWER_LIMIT_VALUE Values
    );

_IRQL_requires_max_(APC_LEVEL)
NTKERNELAPI
NTSTATUS
NTAPI
PoQueryPowerLimitValue (
    _Inout_ PVOID PowerLimitRequest,
    _In_ ULONG ValueCount,
    _Inout_updates_(ValueCount) PPOWER_LIMIT_VALUE Values
    );

_IRQL_requires_max_(APC_LEVEL)
NTKERNELAPI
NTSTATUS
NTAPI
PoDeletePowerLimitRequest (
    _Inout_ PVOID PowerLimitRequest
    );

#endif

typedef struct _KTRANSACTION KTRANSACTION, *PKTRANSACTION, *RESTRICTED_POINTER PRKTRANSACTION;

typedef struct _KENLISTMENT KENLISTMENT, *PKENLISTMENT, *RESTRICTED_POINTER PRKENLISTMENT;

typedef struct _KRESOURCEMANAGER KRESOURCEMANAGER, *PKRESOURCEMANAGER, *RESTRICTED_POINTER PRKRESOURCEMANAGER;

typedef struct _KTM KTM, *PKTM, *RESTRICTED_POINTER PRKTM;

typedef
NTSTATUS
(NTAPI *PTM_RM_NOTIFICATION) (
    _In_     PKENLISTMENT EnlistmentObject,
    _In_     PVOID RMContext,
    _In_     PVOID TransactionContext,
    _In_     ULONG TransactionNotification,
    _Inout_  PLARGE_INTEGER TmVirtualClock,
    _In_     ULONG ArgumentLength,
    _In_     PVOID Argument
    );

_IRQL_requires_max_ (APC_LEVEL)
NTKERNELAPI
NTSTATUS
NTAPI
TmCommitComplete (
    _In_ PKENLISTMENT Enlistment,
    _In_opt_ PLARGE_INTEGER TmVirtualClock
    );

_IRQL_requires_max_ (APC_LEVEL)
NTKERNELAPI
NTSTATUS
NTAPI
TmRollbackComplete (
    _In_ PKENLISTMENT Enlistment,
    _In_opt_ PLARGE_INTEGER TmVirtualClock
    );

_Must_inspect_result_
_IRQL_requires_max_ (APC_LEVEL)
NTKERNELAPI
NTSTATUS
NTAPI
TmEnableCallbacks (
    _In_ PKRESOURCEMANAGER ResourceManager,
    _In_ PTM_RM_NOTIFICATION CallbackRoutine,
    _In_opt_ PVOID RMKey
    );

_Must_inspect_result_
_IRQL_requires_max_ (APC_LEVEL)
NTKERNELAPI
NTSTATUS
NTAPI
TmInitializeTransactionManager (
    _In_ PRKTM TransactionManager,
    _In_opt_ PCUNICODE_STRING LogFileName,
    _In_opt_ PGUID TmId,
    _In_ ULONG CreateOptions
    );

_Must_inspect_result_
_IRQL_requires_max_ (APC_LEVEL)
NTKERNELAPI
NTSTATUS
NTAPI
TmRenameTransactionManager (
    _In_ PUNICODE_STRING LogFileName,
    _In_ LPGUID ExistingTransactionManagerGuid
    );

_Must_inspect_result_
_IRQL_requires_max_ (APC_LEVEL)
NTKERNELAPI
NTSTATUS
NTAPI
TmRecoverTransactionManager (
    _In_ PKTM Tm,
    _In_ PLARGE_INTEGER TargetVirtualClock
    );

_IRQL_requires_max_ (APC_LEVEL)
NTKERNELAPI
NTSTATUS
NTAPI
TmCommitTransaction (
    _In_ PKTRANSACTION Transaction,
    _In_ BOOLEAN       Wait
    );

_IRQL_requires_max_ (APC_LEVEL)
NTKERNELAPI
NTSTATUS
NTAPI
TmRollbackTransaction (
    _In_ PKTRANSACTION Transaction,
    _In_ BOOLEAN       Wait
    );

_Must_inspect_result_
_IRQL_requires_max_ (APC_LEVEL)
NTKERNELAPI
NTSTATUS
NTAPI
TmCreateEnlistment (
    _Out_ PHANDLE           EnlistmentHandle,
    _In_ KPROCESSOR_MODE    PreviousMode,
    _In_ ACCESS_MASK        DesiredAccess,
    _In_ POBJECT_ATTRIBUTES ObjectAttributes,
    _In_ PRKRESOURCEMANAGER ResourceManager,
    _In_ PKTRANSACTION      Transaction,
    _In_opt_ ULONG          CreateOptions,
    _In_ NOTIFICATION_MASK  NotificationMask,
    _In_opt_ PVOID          EnlistmentKey
    );

_Must_inspect_result_
_IRQL_requires_max_ (APC_LEVEL)
NTKERNELAPI
NTSTATUS
NTAPI
TmRecoverEnlistment (
    _In_ PKENLISTMENT Enlistment,
    _In_ PVOID        EnlistmentKey
    );

_Must_inspect_result_
_IRQL_requires_max_ (APC_LEVEL)
NTKERNELAPI
NTSTATUS
NTAPI
TmPrePrepareEnlistment (
    _In_ PKENLISTMENT Enlistment,
    _In_ PLARGE_INTEGER TmVirtualClock
    );

_Must_inspect_result_
_IRQL_requires_max_ (APC_LEVEL)
NTKERNELAPI
NTSTATUS
NTAPI
TmPrepareEnlistment (
    _In_ PKENLISTMENT Enlistment,
    _In_ PLARGE_INTEGER TmVirtualClock
    );

_Must_inspect_result_
_IRQL_requires_max_ (APC_LEVEL)
NTKERNELAPI
NTSTATUS
NTAPI
TmCommitEnlistment (
    _In_ PKENLISTMENT Enlistment,
    _In_ PLARGE_INTEGER TmVirtualClock
    );

_IRQL_requires_max_ (APC_LEVEL)
NTKERNELAPI
NTSTATUS
NTAPI
TmRollbackEnlistment (
    _In_ PKENLISTMENT Enlistment,
    _In_opt_ PLARGE_INTEGER TmVirtualClock
    );

_IRQL_requires_max_ (APC_LEVEL)
NTKERNELAPI
NTSTATUS
NTAPI
TmPrePrepareComplete (
    _In_ PKENLISTMENT Enlistment,
    _In_ PLARGE_INTEGER TmVirtualClock
    );

_IRQL_requires_max_ (APC_LEVEL)
NTKERNELAPI
NTSTATUS
NTAPI
TmPrepareComplete (
    _In_ PKENLISTMENT Enlistment,
    _In_opt_ PLARGE_INTEGER TmVirtualClock
    );

_IRQL_requires_max_ (APC_LEVEL)
NTKERNELAPI
NTSTATUS
NTAPI
TmReadOnlyEnlistment (
    _In_ PKENLISTMENT Enlistment,
    _In_opt_ PLARGE_INTEGER TmVirtualClock
    );

_Must_inspect_result_
_IRQL_requires_max_ (APC_LEVEL)
NTKERNELAPI
NTSTATUS
NTAPI
TmReferenceEnlistmentKey (
    _In_ PKENLISTMENT Enlistment,
    _Out_ PVOID *Key
    );

_Must_inspect_result_
_IRQL_requires_max_ (APC_LEVEL)
NTKERNELAPI
NTSTATUS
NTAPI
TmDereferenceEnlistmentKey (
    _In_ PKENLISTMENT Enlistment,
    _Out_opt_ PBOOLEAN LastReference
    );

_IRQL_requires_max_ (APC_LEVEL)
NTKERNELAPI
NTSTATUS
NTAPI
TmSinglePhaseReject (
    _In_ PKENLISTMENT Enlistment,
    _In_ PLARGE_INTEGER TmVirtualClock
    );

_IRQL_requires_max_ (APC_LEVEL)
NTKERNELAPI
NTSTATUS
NTAPI
TmRequestOutcomeEnlistment (
    _In_ PKENLISTMENT Enlistment,
    _In_ PLARGE_INTEGER TmVirtualClock
    );

_Must_inspect_result_
_IRQL_requires_max_ (APC_LEVEL)
NTKERNELAPI
NTSTATUS
NTAPI
TmRecoverResourceManager (
    _In_ PKRESOURCEMANAGER ResourceManager
    );

_IRQL_requires_max_ (APC_LEVEL)
NTKERNELAPI
NTSTATUS
NTAPI
TmPropagationComplete (
    _In_  PKRESOURCEMANAGER ResourceManager,
    _In_  ULONG             RequestCookie,
    _In_  ULONG             BufferLength,
    _In_  PVOID             Buffer
    );

_IRQL_requires_max_ (APC_LEVEL)
NTKERNELAPI
NTSTATUS
NTAPI
TmPropagationFailed (
    _In_  PKRESOURCEMANAGER ResourceManager,
    _In_  ULONG             RequestCookie,
    _In_  NTSTATUS          Status
    );

_IRQL_requires_max_ (APC_LEVEL)
NTKERNELAPI
VOID
NTAPI
TmGetTransactionId (
    _In_  PKTRANSACTION Transaction,
    _Out_ PUOW TransactionId
    );

_Must_inspect_result_
_IRQL_requires_max_ (APC_LEVEL)
NTKERNELAPI
BOOLEAN
NTAPI
TmIsTransactionActive (
    _In_ PKTRANSACTION Transaction
    );

#if ((NTDDI_VERSION >= NTDDI_WINTHRESHOLD))

#define DEVICE_RESET_INTERFACE_VERSION  1

typedef enum _DEVICE_RESET_TYPE
{
    FunctionLevelDeviceReset,
    PlatformLevelDeviceReset
} DEVICE_RESET_TYPE;

typedef union _DEVICE_BUS_SPECIFIC_RESET_TYPE {
    struct {
        ULONGLONG FunctionLevelDeviceReset:1;
        ULONGLONG PlatformLevelDeviceReset:1;
        ULONGLONG SecondaryBusReset:1;
        ULONGLONG PowerControllerReset:1;
        ULONGLONG NoOpReset:1;
        ULONGLONG Reserved:59;
    } Pci;
    struct {
        ULONGLONG FunctionLevelDeviceReset:1;
        ULONGLONG PlatformLevelDeviceReset:1;
        ULONGLONG Reserved:62;
    } Acpi;
    ULONGLONG AsULONGLONG;
} DEVICE_BUS_SPECIFIC_RESET_TYPE, *PDEVICE_BUS_SPECIFIC_RESET_TYPE;

typedef
VOID
DEVICE_RESET_COMPLETION(
    _In_ NTSTATUS Status,
    _Inout_opt_ PVOID Context);

typedef DEVICE_RESET_COMPLETION *PDEVICE_RESET_COMPLETION;

typedef struct _FUNCTION_LEVEL_DEVICE_RESET_PARAMETERS {
    ULONG Size;
    PDEVICE_RESET_COMPLETION DeviceResetCompletion;
    PVOID CompletionContext;
} FUNCTION_LEVEL_DEVICE_RESET_PARAMETERS, *PFUNCTION_LEVEL_DEVICE_RESET_PARAMETERS;

typedef
_IRQL_requires_same_
_IRQL_requires_max_(PASSIVE_LEVEL)
_Function_class_(DEVICE_RESET_HANDLER)
NTSTATUS
DEVICE_RESET_HANDLER(
    _In_ PVOID InterfaceContext,
    _In_ DEVICE_RESET_TYPE ResetType,
    _In_ ULONG Flags,
    _In_opt_ PVOID ResetParameters
    );

typedef DEVICE_RESET_HANDLER *PDEVICE_RESET_HANDLER;

#if ((NTDDI_VERSION >= NTDDI_WIN10_FE))

typedef struct _DEVICE_BUS_SPECIFIC_RESET_INFO {
    GUID BusTypeGuid;
    DEVICE_BUS_SPECIFIC_RESET_TYPE ResetTypeSupported;
} DEVICE_BUS_SPECIFIC_RESET_INFO, *PDEVICE_BUS_SPECIFIC_RESET_INFO;

typedef
_IRQL_requires_same_
_IRQL_requires_max_(PASSIVE_LEVEL)
_Function_class_(DEVICE_QUERY_BUS_SPECIFIC_RESET_HANDLER)
NTSTATUS
DEVICE_QUERY_BUS_SPECIFIC_RESET_HANDLER(
    _In_ PVOID InterfaceContext,
    _Out_ PULONG ResetInfoCount,
    _Out_ PDEVICE_BUS_SPECIFIC_RESET_INFO ResetInfoSupported
);

typedef DEVICE_QUERY_BUS_SPECIFIC_RESET_HANDLER *PDEVICE_QUERY_BUS_SPECIFIC_RESET_HANDLER;

typedef union _BUS_SPECIFIC_RESET_FLAGS {
    struct {
        ULONGLONG KeepStackReset:1;
        ULONGLONG Reserved:63;
    } u;
    ULONGLONG AsUlonglong;
} BUS_SPECIFIC_RESET_FLAGS, *PBUS_SPECIFIC_RESET_FLAGS;

typedef
_IRQL_requires_same_
_IRQL_requires_max_(PASSIVE_LEVEL)
_Function_class_(DEVICE_BUS_SPECIFIC_RESET_HANDLER)
NTSTATUS
DEVICE_BUS_SPECIFIC_RESET_HANDLER(
    _In_ PVOID InterfaceContext,
    _In_ CONST GUID *BusType,
    _In_ DEVICE_BUS_SPECIFIC_RESET_TYPE ResetTypeSelected,
    _In_ PBUS_SPECIFIC_RESET_FLAGS Flags,
    _In_ PVOID ResetParameters
);

typedef DEVICE_BUS_SPECIFIC_RESET_HANDLER *PDEVICE_BUS_SPECIFIC_RESET_HANDLER;

typedef union _DEVICE_RESET_STATUS_FLAGS {
    struct {
        ULONGLONG KeepStackReset:1;
        ULONGLONG RecoveringFromBusError:1;
        ULONGLONG Reserved:62;
    } u;
    ULONGLONG AsUlonglong;
} DEVICE_RESET_STATUS_FLAGS, *PDEVICE_RESET_STATUS_FLAGS;

typedef
_IRQL_requires_same_
_IRQL_requires_max_(PASSIVE_LEVEL)
_Function_class_(GET_DEVICE_RESET_STATUS)
NTSTATUS
GET_DEVICE_RESET_STATUS(
    _In_ PVOID InterfaceContext,
    _Out_ PBOOLEAN IsResetting,
    _Out_ PDEVICE_BUS_SPECIFIC_RESET_TYPE ResetTypeSelected,
    _Out_ PDEVICE_RESET_STATUS_FLAGS Flags
);

typedef GET_DEVICE_RESET_STATUS *PGET_DEVICE_RESET_STATUS;

#endif

typedef struct _DEVICE_RESET_INTERFACE_STANDARD {
    USHORT Size;
    USHORT Version;
    PVOID Context;
    PINTERFACE_REFERENCE InterfaceReference;
    PINTERFACE_DEREFERENCE InterfaceDereference;
    PDEVICE_RESET_HANDLER DeviceReset;
    ULONG SupportedResetTypes;
    PVOID Reserved;
#if (NTDDI_VERSION >= NTDDI_WIN10_FE)
    PDEVICE_QUERY_BUS_SPECIFIC_RESET_HANDLER QueryBusSpecificResetInfo;
    PDEVICE_BUS_SPECIFIC_RESET_HANDLER DeviceBusSpecificReset;
    PGET_DEVICE_RESET_STATUS GetDeviceResetStatus;
#endif
} DEVICE_RESET_INTERFACE_STANDARD, *PDEVICE_RESET_INTERFACE_STANDARD;

#endif

#ifdef __cplusplus
}
#endif

typedef enum _MEM_DEDICATED_ATTRIBUTE_TYPE {
    MemDedicatedAttributeReadBandwidth = 0,
    MemDedicatedAttributeReadLatency = 1,
    MemDedicatedAttributeWriteBandwidth = 2,
    MemDedicatedAttributeWriteLatency = 3,
    MemDedicatedAttributeMax = 4
} MEM_DEDICATED_ATTRIBUTE_TYPE, *PMEM_DEDICATED_ATTRIBUTE_TYPE;

typedef enum MEM_SECTION_EXTENDED_PARAMETER_TYPE {
    MemSectionExtendedParameterInvalidType = 0,
    MemSectionExtendedParameterUserPhysicalFlags = 1,
    MemSectionExtendedParameterNumaNode = 2,
    MemSectionExtendedParameterSigningLevel = 3,
    MemSectionExtendedParameterMax = 4
} MEM_SECTION_EXTENDED_PARAMETER_TYPE, *PMEM_SECTION_EXTENDED_PARAMETER_TYPE;

typedef enum _PARTITION_INFORMATION_CLASS {
    SystemMemoryPartitionInformation = 0,
    SystemMemoryPartitionDedicatedMemoryInformation = 9,
    SystemMemoryPartitionOpenDedicatedMemory = 10
} PARTITION_INFORMATION_CLASS, *PPARTITION_INFORMATION_CLASS;

typedef enum _USER_ACTIVITY_PRESENCE {
    PowerUserPresent = 0,
    PowerUserNotPresent = 1,
    PowerUserInactive = 2,
    PowerUserMaximum = 3,
    PowerUserInvalid = 3
} USER_ACTIVITY_PRESENCE, *PUSER_ACTIVITY_PRESENCE;

typedef enum _ENERGY_SAVER_STATUS {
    ENERGY_SAVER_OFF = 0,
    ENERGY_SAVER_STANDARD = 1,
    ENERGY_SAVER_HIGH_SAVINGS = 2
} ENERGY_SAVER_STATUS, *PENERGY_SAVER_STATUS;

typedef enum POWER_USER_PRESENCE_TYPE {
    UserNotPresent = 0,
    UserPresent = 1,
    UserUnknown = 255
} POWER_USER_PRESENCE_TYPE, *PPOWER_USER_PRESENCE_TYPE;

typedef enum POWER_MONITOR_REQUEST_REASON {
    MonitorRequestReasonUnknown = 0,
    MonitorRequestReasonPowerButton = 1,
    MonitorRequestReasonRemoteConnection = 2,
    MonitorRequestReasonScMonitorpower = 3,
    MonitorRequestReasonUserInput = 4,
    MonitorRequestReasonAcDcDisplayBurst = 5,
    MonitorRequestReasonUserDisplayBurst = 6,
    MonitorRequestReasonPoSetSystemState = 7,
    MonitorRequestReasonSetThreadExecutionState = 8,
    MonitorRequestReasonFullWake = 9,
    MonitorRequestReasonSessionUnlock = 10,
    MonitorRequestReasonScreenOffRequest = 11,
    MonitorRequestReasonIdleTimeout = 12,
    MonitorRequestReasonPolicyChange = 13,
    MonitorRequestReasonSleepButton = 14,
    MonitorRequestReasonLid = 15,
    MonitorRequestReasonBatteryCountChange = 16,
    MonitorRequestReasonGracePeriod = 17,
    MonitorRequestReasonPnP = 18,
    MonitorRequestReasonDP = 19,
    MonitorRequestReasonSxTransition = 20,
    MonitorRequestReasonSystemIdle = 21,
    MonitorRequestReasonNearProximity = 22,
    MonitorRequestReasonThermalStandby = 23,
    MonitorRequestReasonResumePdc = 24,
    MonitorRequestReasonResumeS4 = 25,
    MonitorRequestReasonTerminal = 26,
    MonitorRequestReasonPdcSignal = 27,
    MonitorRequestReasonAcDcDisplayBurstSuppressed = 28,
    MonitorRequestReasonSystemStateEntered = 29,
    MonitorRequestReasonWinrt = 30,
    MonitorRequestReasonUserInputKeyboard = 31,
    MonitorRequestReasonUserInputMouse = 32,
    MonitorRequestReasonUserInputTouchpad = 33,
    MonitorRequestReasonUserInputPen = 34,
    MonitorRequestReasonUserInputAccelerometer = 35,
    MonitorRequestReasonUserInputHid = 36,
    MonitorRequestReasonUserInputPoUserPresent = 37,
    MonitorRequestReasonUserInputSessionSwitch = 38,
    MonitorRequestReasonUserInputInitialization = 39,
    MonitorRequestReasonPdcSignalWindowsMobilePwrNotif = 40,
    MonitorRequestReasonPdcSignalWindowsMobileShell = 41,
    MonitorRequestReasonPdcSignalHeyCortana = 42,
    MonitorRequestReasonPdcSignalHolographicShell = 43,
    MonitorRequestReasonPdcSignalFingerprint = 44,
    MonitorRequestReasonDirectedDrips = 45,
    MonitorRequestReasonDim = 46,
    MonitorRequestReasonBuiltinPanel = 47,
    MonitorRequestReasonDisplayRequiredUnDim = 48,
    MonitorRequestReasonBatteryCountChangeSuppressed = 49,
    MonitorRequestReasonResumeModernStandby = 50,
    MonitorRequestReasonTerminalInit = 51,
    MonitorRequestReasonPdcSignalSensorsHumanPresence = 52,
    MonitorRequestReasonBatteryPreCritical = 53,
    MonitorRequestReasonUserInputTouch = 54,
    MonitorRequestReasonAusterityBatteryDrain = 55,
    MonitorRequestReasonDozeRestrictedStandby = 56,
    MonitorRequestReasonSmartRestrictedStandby = 57,
    MonitorRequestReasonMax = 58
} POWER_MONITOR_REQUEST_REASON;

typedef enum _POWER_MONITOR_REQUEST_TYPE {
    MonitorRequestTypeOff = 0,
    MonitorRequestTypeOnAndPresent = 1,
    MonitorRequestTypeToggleOn = 2
} POWER_MONITOR_REQUEST_TYPE;

typedef enum POWER_SETTING_ALTITUDE {
    ALTITUDE_GROUP_POLICY = 0,
    ALTITUDE_USER = 1,
    ALTITUDE_RUNTIME_OVERRIDE = 2,
    ALTITUDE_PROVISIONING = 3,
    ALTITUDE_OEM_CUSTOMIZATION = 4,
    ALTITUDE_INTERNAL_OVERRIDE = 5,
    ALTITUDE_OS_DEFAULT = 6
} POWER_SETTING_ALTITUDE, *PPOWER_SETTING_ALTITUDE;

typedef enum _OS_DEPLOYEMENT_STATE_VALUES {
    OS_DEPLOYMENT_STANDARD = 1,
    OS_DEPLOYMENT_COMPACT = 2
} OS_DEPLOYEMENT_STATE_VALUES;

typedef enum _IMAGE_POLICY_ENTRY_TYPE {
    ImagePolicyEntryTypeNone = 0,
    ImagePolicyEntryTypeBool = 1,
    ImagePolicyEntryTypeInt8 = 2,
    ImagePolicyEntryTypeUInt8 = 3,
    ImagePolicyEntryTypeInt16 = 4,
    ImagePolicyEntryTypeUInt16 = 5,
    ImagePolicyEntryTypeInt32 = 6,
    ImagePolicyEntryTypeUInt32 = 7,
    ImagePolicyEntryTypeInt64 = 8,
    ImagePolicyEntryTypeUInt64 = 9,
    ImagePolicyEntryTypeAnsiString = 10,
    ImagePolicyEntryTypeUnicodeString = 11,
    ImagePolicyEntryTypeOverride = 12,
    ImagePolicyEntryTypeMaximum = 13
} IMAGE_POLICY_ENTRY_TYPE;

typedef enum _IMAGE_POLICY_ID {
    ImagePolicyIdNone = 0,
    ImagePolicyIdEtw = 1,
    ImagePolicyIdDebug = 2,
    ImagePolicyIdCrashDump = 3,
    ImagePolicyIdCrashDumpKey = 4,
    ImagePolicyIdCrashDumpKeyGuid = 5,
    ImagePolicyIdParentSd = 6,
    ImagePolicyIdParentSdRev = 7,
    ImagePolicyIdSvn = 8,
    ImagePolicyIdDeviceId = 9,
    ImagePolicyIdCapability = 10,
    ImagePolicyIdScenarioId = 11,
    ImagePolicyIdCapabilityOverridable = 12,
    ImagePolicyIdTrustletIdOverridable = 13,
    ImagePolicyIdMaximum = 14
} IMAGE_POLICY_ID;

typedef enum _POOL_CREATE_EXTENDED_PARAMETER_TYPE {
    PoolCreateExtendedParameterInvalidType = 0,
    PoolCreateExtendedParameterName = 1
} POOL_CREATE_EXTENDED_PARAMETER_TYPE, *PPOOL_CREATE_EXTENDED_PARAMETER_TYPE;

typedef enum _MM_MDL_PAGE_CONTENTS_STATE {
    MmMdlPageContentsDynamic = 0,
    MmMdlPageContentsInvariant = 1,
    MmMdlPageContentsQuery = 2
} MM_MDL_PAGE_CONTENTS_STATE, PMM_MDL_PAGE_CONTENTS_STATE;

typedef enum _SE_IMAGE_TYPE {
    SeImageTypeElamDriver = 0,
    SeImageTypeDriver = 1,
    SeImageTypePlatformSecureFile = 2,
    SeImageTypeDynamicCodeFile = 3,
    SeImageTypeAll = 4,
    SeImageTypeMax = 5
} SE_IMAGE_TYPE, *PSE_IMAGE_TYPE;

typedef enum _SE_IMAGE_VERIFICATION_CALLBACK_TYPE {
    SeImageVerificationCallbackInformational = 0,
    SeImageVerificationCallbackBlock = 1
} SE_IMAGE_VERIFICATION_CALLBACK_TYPE, *PSE_IMAGE_VERIFICATION_CALLBACK_TYPE;

typedef enum _DRIVER_PROXY_REGISTER_CALLBACK_PHASE {
    DriverProxyRegisterCallbackPreProcess = 0,
    DriverProxyRegisterCallbackProxyStalled = 1,
    DriverProxyRegisterCallbackPostProcess = 2,
    DriverProxyRegisterCallbackMax = 3
} DRIVER_PROXY_REGISTER_CALLBACK_PHASE, *PDRIVER_PROXY_REGISTER_CALLBACK_PHASE;

typedef enum _PCI_ACS_BIT {
    PciAcsReserved = 0,
    PciAcsBitEnable = 1,
    PciAcsBitDisable = 2,
    PciAcsBitDontCare = 3
} PCI_ACS_BIT, *PPCI_ACS_BIT;

typedef enum _NPEM_CONTROL_STANDARD_CONTROL_BIT {
    InitiateReset = 1,
    OkControl = 2,
    LocateControl = 3,
    FailControl = 4,
    RebuildControl = 5,
    PFAControl = 6,
    HotSpareControl = 7,
    InACriticalArrayControl = 8,
    InAFailedArrayControl = 9,
    InvalidDeviceTypeControl = 10,
    DisabledControl = 11
} NPEM_CONTROL_STANDARD_CONTROL_BIT, *PNPEM_CONTROL_STANDARD_CONTROL_BIT;

typedef enum _PO_EFFECTIVE_POWER_MODE {
    PoEffectivePowerModeBatterySaver = 0,
    PoEffectivePowerModeEnergySaverHighSavings = 0,
    PoEffectivePowerModeBetterBattery = 1,
    PoEffectivePowerModeEnergySaverStandard = 1,
    PoEffectivePowerModeBalanced = 2,
    PoEffectivePowerModeHighPerformance = 3,
    PoEffectivePowerModeMaxPerformance = 4,
    PoEffectivePowerModeGameMode = 5,
    PoEffectivePowerModeMixedReality = 6
} PO_EFFECTIVE_POWER_MODE, *PPO_EFFECTIVE_POWER_MODE;

typedef enum _CLS_CONTEXT_MODE {
    ClsContextNone = 0,
    ClsContextUndoNext = 1,
    ClsContextPrevious = 2,
    ClsContextForward = 3
} CLS_CONTEXT_MODE, *PCLS_CONTEXT_MODE;

typedef enum _CLFS_CONTEXT_MODE {
    ClfsContextNone = 0,
    ClfsContextUndoNext = 1,
    ClfsContextPrevious = 2,
    ClfsContextForward = 3
} CLFS_CONTEXT_MODE, *PCLFS_CONTEXT_MODE;

typedef enum _CLS_LOG_INFORMATION_CLASS {
    ClfsLogBasicInformation = 0,
    ClfsLogBasicInformationPhysical = 1,
    ClfsLogPhysicalNameInformation = 2,
    ClfsLogStreamIdentifierInformation = 3,
    ClfsLogSystemMarkingInformation = 4,
    ClfsLogPhysicalLsnInformation = 5
} CLS_LOG_INFORMATION_CLASS, *PCLS_LOG_INFORMATION_CLASS, CLFS_LOG_INFORMATION_CLASS, *PCLFS_LOG_INFORMATION_CLASS;

typedef enum _CLS_IOSTATS_CLASS {
    ClsIoStatsDefault = 0,
    ClsIoStatsMax = 0xFFFF
} CLS_IOSTATS_CLASS, *PCLS_IOSTATS_CLASS;

typedef enum _CLFS_IOSTATS_CLASS {
    ClfsIoStatsDefault = 0,
    ClfsIoStatsMax = 0xFFFF
} CLFS_IOSTATS_CLASS, *PCLFS_IOSTATS_CLASS;

typedef enum _CLFS_LOG_ARCHIVE_MODE {
    ClfsLogArchiveEnabled = 1,
    ClfsLogArchiveDisabled = 2
} CLFS_LOG_ARCHIVE_MODE, *PCLFS_LOG_ARCHIVE_MODE;

typedef enum _CLFS_MGMT_POLICY_TYPE {
    ClfsMgmtPolicyMaximumSize = 0,
    ClfsMgmtPolicyMinimumSize = 1,
    ClfsMgmtPolicyNewContainerSize = 2,
    ClfsMgmtPolicyGrowthRate = 3,
    ClfsMgmtPolicyLogTail = 4,
    ClfsMgmtPolicyAutoShrink = 5,
    ClfsMgmtPolicyAutoGrow = 6,
    ClfsMgmtPolicyNewContainerPrefix = 7,
    ClfsMgmtPolicyNewContainerSuffix = 8,
    ClfsMgmtPolicyNewContainerExtension = 9,
    ClfsMgmtPolicyInvalid = 10
} CLFS_MGMT_POLICY_TYPE, *PCLFS_MGMT_POLICY_TYPE;

typedef enum _CLFS_MGMT_NOTIFICATION_TYPE {
    ClfsMgmtAdvanceTailNotification = 0,
    ClfsMgmtLogFullHandlerNotification = 1,
    ClfsMgmtLogUnpinnedNotification = 2,
    ClfsMgmtLogWriteNotification = 3
} CLFS_MGMT_NOTIFICATION_TYPE, *PCLFS_MGMT_NOTIFICATION_TYPE;

#endif /* !_WDMDDK_ */

#ifndef ADAPTER_INFO_HYBRID_PASSTHROUGH
#define ADAPTER_INFO_HYBRID_PASSTHROUGH 0x0004
#endif
#ifndef ATS_DEVICE_SVM_OPTOUT
#define ATS_DEVICE_SVM_OPTOUT 0x1
#endif
#ifndef BACKUP_SECURITY_INFORMATION
#define BACKUP_SECURITY_INFORMATION 0x00010000L
#endif
#ifndef CLFS_FLAG_FORCE_APPEND
#define CLFS_FLAG_FORCE_APPEND 0x00000001
#endif
#ifndef CLFS_FLAG_FORCE_FLUSH
#define CLFS_FLAG_FORCE_FLUSH 0x00000002
#endif
#ifndef CLFS_FLAG_HIDDEN_SYSTEM_LOG
#define CLFS_FLAG_HIDDEN_SYSTEM_LOG 0x00000200
#endif
#ifndef CLFS_FLAG_IGNORE_SHARE_ACCESS
#define CLFS_FLAG_IGNORE_SHARE_ACCESS 0x00000040
#endif
#ifndef CLFS_FLAG_MINIFILTER_LEVEL
#define CLFS_FLAG_MINIFILTER_LEVEL 0x00000100
#endif
#ifndef CLFS_FLAG_NON_REENTRANT_FILTER
#define CLFS_FLAG_NON_REENTRANT_FILTER 0x00000010
#endif
#ifndef CLFS_FLAG_NO_FLAGS
#define CLFS_FLAG_NO_FLAGS 0x00000000
#endif
#ifndef CLFS_FLAG_READ_IN_PROGRESS
#define CLFS_FLAG_READ_IN_PROGRESS 0x00000080
#endif
#ifndef CLFS_FLAG_REENTRANT_FILE_SYSTEM
#define CLFS_FLAG_REENTRANT_FILE_SYSTEM 0x00000008
#endif
#ifndef CLFS_FLAG_REENTRANT_FILTER
#define CLFS_FLAG_REENTRANT_FILTER 0x00000020
#endif
#ifndef CLFS_FLAG_USE_RESERVATION
#define CLFS_FLAG_USE_RESERVATION 0x00000004
#endif
#ifndef CLFS_MARSHALLING_FLAG_DISABLE_BUFF_INIT
#define CLFS_MARSHALLING_FLAG_DISABLE_BUFF_INIT 0x00000001
#endif
#ifndef CLFS_MARSHALLING_FLAG_NONE
#define CLFS_MARSHALLING_FLAG_NONE 0x00000000
#endif
#ifndef CLFS_MAX_CONTAINER_INFO
#define CLFS_MAX_CONTAINER_INFO 256
#endif
#ifndef CLFS_MGMT_CLIENT_REGISTRATION_VERSION
#define CLFS_MGMT_CLIENT_REGISTRATION_VERSION 0x1
#endif
#ifndef CLFS_MGMT_POLICY_VERSION
#define CLFS_MGMT_POLICY_VERSION 0x01
#endif
#ifndef CLFS_SCAN_BACKWARD
#define CLFS_SCAN_BACKWARD 0x04
#endif
#ifndef CLFS_SCAN_BUFFERED
#define CLFS_SCAN_BUFFERED 0x20
#endif
#ifndef CLFS_SCAN_CLOSE
#define CLFS_SCAN_CLOSE 0x08
#endif
#ifndef CLFS_SCAN_FORWARD
#define CLFS_SCAN_FORWARD 0x02
#endif
#ifndef CLFS_SCAN_INIT
#define CLFS_SCAN_INIT 0x01
#endif
#ifndef CLFS_SCAN_INITIALIZED
#define CLFS_SCAN_INITIALIZED 0x10
#endif
#ifndef ClfsContainerActive
#define ClfsContainerActive 0x04
#endif
#ifndef ClfsContainerActivePendingDelete
#define ClfsContainerActivePendingDelete 0x08
#endif
#ifndef ClfsContainerInactive
#define ClfsContainerInactive 0x02
#endif
#ifndef ClfsContainerInitializing
#define ClfsContainerInitializing 0x01
#endif
#ifndef ClfsContainerPendingArchive
#define ClfsContainerPendingArchive 0x10
#endif
#ifndef ClfsContainerPendingArchiveAndDelete
#define ClfsContainerPendingArchiveAndDelete 0x20
#endif
#ifndef ClfsDataRecord
#define ClfsDataRecord 0x01
#endif
#ifndef ClfsNullRecord
#define ClfsNullRecord 0x00
#endif
#ifndef ClfsRestartRecord
#define ClfsRestartRecord 0x02
#endif
#ifndef ClsContainerActive
#define ClsContainerActive 0x04
#endif
#ifndef ClsContainerActivePendingDelete
#define ClsContainerActivePendingDelete 0x08
#endif
#ifndef ClsContainerInactive
#define ClsContainerInactive 0x02
#endif
#ifndef ClsContainerInitializing
#define ClsContainerInitializing 0x01
#endif
#ifndef ClsContainerPendingArchive
#define ClsContainerPendingArchive 0x10
#endif
#ifndef ClsContainerPendingArchiveAndDelete
#define ClsContainerPendingArchiveAndDelete 0x20
#endif
#ifndef DEVICE_RESET_INTERFACE_VERSION_1
#define DEVICE_RESET_INTERFACE_VERSION_1 1
#endif
#ifndef DEVICE_RESET_INTERFACE_VERSION_2
#define DEVICE_RESET_INTERFACE_VERSION_2 2
#endif
#ifndef DEVICE_RESET_INTERFACE_VERSION_3
#define DEVICE_RESET_INTERFACE_VERSION_3 3
#endif
#ifndef DMA_FAIL_ON_BOUNCE
#define DMA_FAIL_ON_BOUNCE 0x04
#endif
#ifndef DMA_ZERO_BUFFERS
#define DMA_ZERO_BUFFERS 0x02
#endif
#ifndef DOMAIN_COMMON_BUFFER_LARGE_PAGE
#define DOMAIN_COMMON_BUFFER_LARGE_PAGE 0x0001
#endif
#ifndef DPC_WATCHDOG_GLOBAL_TRIAGE_BLOCK_REVISION_1
#define DPC_WATCHDOG_GLOBAL_TRIAGE_BLOCK_REVISION_1 0x1
#endif
#ifndef DPC_WATCHDOG_GLOBAL_TRIAGE_BLOCK_SIGNATURE
#define DPC_WATCHDOG_GLOBAL_TRIAGE_BLOCK_SIGNATURE 0xAEBECEDE
#endif
#ifndef EXCEPTION_CP_FAULT
#define EXCEPTION_CP_FAULT 0x15
#endif
#ifndef EXCEPTION_SE_FAULT
#define EXCEPTION_SE_FAULT 0x17
#endif
#ifndef EX_CARR_ALLOCATE_NONPAGED_POOL
#define EX_CARR_ALLOCATE_NONPAGED_POOL 0x00000001
#endif
#ifndef EX_CARR_ALLOCATE_PAGED_POOL
#define EX_CARR_ALLOCATE_PAGED_POOL 0x00000000
#endif
#ifndef EX_CARR_DISABLE_EXPANSION
#define EX_CARR_DISABLE_EXPANSION 0x00000002
#endif
#ifndef EX_CREATE_FLAG_FILE_DEST_OPEN_FOR_COPY
#define EX_CREATE_FLAG_FILE_DEST_OPEN_FOR_COPY 0x00000002
#endif
#ifndef EX_CREATE_FLAG_FILE_SOURCE_OPEN_FOR_COPY
#define EX_CREATE_FLAG_FILE_SOURCE_OPEN_FOR_COPY 0x00000001
#endif
#ifndef FAST_FAIL_ADMINLESS_ACCESS_DENIED
#define FAST_FAIL_ADMINLESS_ACCESS_DENIED 55
#endif
#ifndef FAST_FAIL_APCS_DISABLED
#define FAST_FAIL_APCS_DISABLED 32
#endif
#ifndef FAST_FAIL_ASAN_ERROR
#define FAST_FAIL_ASAN_ERROR 71
#endif
#ifndef FAST_FAIL_CAST_GUARD
#define FAST_FAIL_CAST_GUARD 65
#endif
#ifndef FAST_FAIL_CERTIFICATION_FAILURE
#define FAST_FAIL_CERTIFICATION_FAILURE 20
#endif
#ifndef FAST_FAIL_CLR_EXCEPTION_AOT
#define FAST_FAIL_CLR_EXCEPTION_AOT 72
#endif
#ifndef FAST_FAIL_CONTROL_INVALID_RETURN_ADDRESS
#define FAST_FAIL_CONTROL_INVALID_RETURN_ADDRESS 57
#endif
#ifndef FAST_FAIL_CORRUPT_WOW64_STATE
#define FAST_FAIL_CORRUPT_WOW64_STATE 75
#endif
#ifndef FAST_FAIL_CRYPTO_LIBRARY
#define FAST_FAIL_CRYPTO_LIBRARY 22
#endif
#ifndef FAST_FAIL_DLOAD_PROTECTION_FAILURE
#define FAST_FAIL_DLOAD_PROTECTION_FAILURE 25
#endif
#ifndef FAST_FAIL_ETW_CORRUPTION
#define FAST_FAIL_ETW_CORRUPTION 61
#endif
#ifndef FAST_FAIL_FLAGS_CORRUPTION
#define FAST_FAIL_FLAGS_CORRUPTION 59
#endif
#ifndef FAST_FAIL_GUARD_EXPORT_SUPPRESSION_FAILURE
#define FAST_FAIL_GUARD_EXPORT_SUPPRESSION_FAILURE 46
#endif
#ifndef FAST_FAIL_GUARD_ICALL_CHECK_FAILURE_XFG
#define FAST_FAIL_GUARD_ICALL_CHECK_FAILURE_XFG 64
#endif
#ifndef FAST_FAIL_GUARD_ICALL_CHECK_SUPPRESSED
#define FAST_FAIL_GUARD_ICALL_CHECK_SUPPRESSED 31
#endif
#ifndef FAST_FAIL_GUARD_JUMPTABLE
#define FAST_FAIL_GUARD_JUMPTABLE 37
#endif
#ifndef FAST_FAIL_HEAP_METADATA_CORRUPTION
#define FAST_FAIL_HEAP_METADATA_CORRUPTION 50
#endif
#ifndef FAST_FAIL_HOST_VISIBILITY_CHANGE
#define FAST_FAIL_HOST_VISIBILITY_CHANGE 66
#endif
#ifndef FAST_FAIL_INVALID_BALANCED_TREE
#define FAST_FAIL_INVALID_BALANCED_TREE 29
#endif
#ifndef FAST_FAIL_INVALID_CALL_IN_DLL_CALLOUT
#define FAST_FAIL_INVALID_CALL_IN_DLL_CALLOUT 23
#endif
#ifndef FAST_FAIL_INVALID_DISPATCH_CONTEXT
#define FAST_FAIL_INVALID_DISPATCH_CONTEXT 39
#endif
#ifndef FAST_FAIL_INVALID_EXCEPTION_CHAIN
#define FAST_FAIL_INVALID_EXCEPTION_CHAIN 21
#endif
#ifndef FAST_FAIL_INVALID_EXTENDED_STATE
#define FAST_FAIL_INVALID_EXTENDED_STATE 76
#endif
#ifndef FAST_FAIL_INVALID_FILE_OPERATION
#define FAST_FAIL_INVALID_FILE_OPERATION 42
#endif
#ifndef FAST_FAIL_INVALID_FLS_DATA
#define FAST_FAIL_INVALID_FLS_DATA 70
#endif
#ifndef FAST_FAIL_INVALID_IAT
#define FAST_FAIL_INVALID_IAT 49
#endif
#ifndef FAST_FAIL_INVALID_IDLE_STATE
#define FAST_FAIL_INVALID_IDLE_STATE 33
#endif
#ifndef FAST_FAIL_INVALID_IMAGE_BASE
#define FAST_FAIL_INVALID_IMAGE_BASE 24
#endif
#ifndef FAST_FAIL_INVALID_LOCK_STATE
#define FAST_FAIL_INVALID_LOCK_STATE 36
#endif
#ifndef FAST_FAIL_INVALID_LONGJUMP_TARGET
#define FAST_FAIL_INVALID_LONGJUMP_TARGET 38
#endif
#ifndef FAST_FAIL_INVALID_PFN
#define FAST_FAIL_INVALID_PFN 63
#endif
#ifndef FAST_FAIL_INVALID_SYSCALL_NUMBER
#define FAST_FAIL_INVALID_SYSCALL_NUMBER 41
#endif
#ifndef FAST_FAIL_INVALID_THREAD
#define FAST_FAIL_INVALID_THREAD 40
#endif
#ifndef FAST_FAIL_INVALID_THREAD_STATE
#define FAST_FAIL_INVALID_THREAD_STATE 74
#endif
#ifndef FAST_FAIL_KERNEL_CET_SHADOW_STACK_ASSIST
#define FAST_FAIL_KERNEL_CET_SHADOW_STACK_ASSIST 67
#endif
#ifndef FAST_FAIL_KERNEL_POINTER_EXPECTED
#define FAST_FAIL_KERNEL_POINTER_EXPECTED 77
#endif
#ifndef FAST_FAIL_LOADER_CONTINUITY_FAILURE
#define FAST_FAIL_LOADER_CONTINUITY_FAILURE 45
#endif
#ifndef FAST_FAIL_LOW_LABEL_ACCESS_DENIED
#define FAST_FAIL_LOW_LABEL_ACCESS_DENIED 52
#endif
#ifndef FAST_FAIL_LPAC_ACCESS_DENIED
#define FAST_FAIL_LPAC_ACCESS_DENIED 43
#endif
#ifndef FAST_FAIL_MRDATA_PROTECTION_FAILURE
#define FAST_FAIL_MRDATA_PROTECTION_FAILURE 34
#endif
#ifndef FAST_FAIL_NTDLL_PATCH_FAILED
#define FAST_FAIL_NTDLL_PATCH_FAILED 69
#endif
#ifndef FAST_FAIL_PATCH_CALLBACK_FAILED
#define FAST_FAIL_PATCH_CALLBACK_FAILED 68
#endif
#ifndef FAST_FAIL_PAYLOAD_RESTRICTION_VIOLATION
#define FAST_FAIL_PAYLOAD_RESTRICTION_VIOLATION 51
#endif
#ifndef FAST_FAIL_POINTER_AUTH_INVALID_RETURN_ADDRESS
#define FAST_FAIL_POINTER_AUTH_INVALID_RETURN_ADDRESS 73
#endif
#ifndef FAST_FAIL_RIO_ABORT
#define FAST_FAIL_RIO_ABORT 62
#endif
#ifndef FAST_FAIL_UNEXPECTED_CALL
#define FAST_FAIL_UNEXPECTED_CALL 56
#endif
#ifndef FAST_FAIL_UNEXPECTED_HEAP_EXCEPTION
#define FAST_FAIL_UNEXPECTED_HEAP_EXCEPTION 35
#endif
#ifndef FAST_FAIL_UNEXPECTED_HOST_BEHAVIOR
#define FAST_FAIL_UNEXPECTED_HOST_BEHAVIOR 58
#endif
#ifndef FAST_FAIL_UNHANDLED_LSS_EXCEPTON
#define FAST_FAIL_UNHANDLED_LSS_EXCEPTON 54
#endif
#ifndef FAST_FAIL_UNSAFE_EXTENSION_CALL
#define FAST_FAIL_UNSAFE_EXTENSION_CALL 26
#endif
#ifndef FAST_FAIL_VEH_CORRUPTION
#define FAST_FAIL_VEH_CORRUPTION 60
#endif
#ifndef FILE_CONTAINS_EXTENDED_CREATE_INFORMATION
#define FILE_CONTAINS_EXTENDED_CREATE_INFORMATION 0x10000000
#endif
#ifndef FILE_QUERY_INDEX_SPECIFIED
#define FILE_QUERY_INDEX_SPECIFIED 0x00000004
#endif
#ifndef FILE_QUERY_NO_CURSOR_UPDATE
#define FILE_QUERY_NO_CURSOR_UPDATE 0x00000010
#endif
#ifndef FILE_QUERY_RESTART_SCAN
#define FILE_QUERY_RESTART_SCAN 0x00000001
#endif
#ifndef FILE_QUERY_RETURN_ON_DISK_ENTRIES_ONLY
#define FILE_QUERY_RETURN_ON_DISK_ENTRIES_ONLY 0x00000008
#endif
#ifndef FILE_QUERY_RETURN_SINGLE_ENTRY
#define FILE_QUERY_RETURN_SINGLE_ENTRY 0x00000002
#endif
#ifndef FILE_SESSION_AWARE
#define FILE_SESSION_AWARE 0x00040000
#endif
#ifndef FILE_VALID_EXTENDED_OPTION_FLAGS
#define FILE_VALID_EXTENDED_OPTION_FLAGS 0x10000000
#endif
#ifndef FO_BYPASS_IO_ENABLED
#define FO_BYPASS_IO_ENABLED 0x00800000
#endif
#ifndef HAL_DMA_ADAPTER_VERSION_1
#define HAL_DMA_ADAPTER_VERSION_1 1
#endif
#ifndef HAL_MASK_UNMASK_FLAGS_NONE
#define HAL_MASK_UNMASK_FLAGS_NONE 0x0
#endif
#ifndef HAL_MASK_UNMASK_FLAGS_SERVICING_COMPLETE
#define HAL_MASK_UNMASK_FLAGS_SERVICING_COMPLETE 0x2
#endif
#ifndef HAL_MASK_UNMASK_FLAGS_SERVICING_DEFERRED
#define HAL_MASK_UNMASK_FLAGS_SERVICING_DEFERRED 0x1
#endif
#ifndef IMAGE_POLICY_METADATA_VERSION
#define IMAGE_POLICY_METADATA_VERSION 1
#endif
#ifndef IOMMU_ACCESS_NONE
#define IOMMU_ACCESS_NONE 0x00
#endif
#ifndef IOMMU_ACCESS_READ
#define IOMMU_ACCESS_READ 0x01
#endif
#ifndef IOMMU_ACCESS_WRITE
#define IOMMU_ACCESS_WRITE 0x02
#endif
#ifndef IO_ATTRIBUTION_INFO_V1
#define IO_ATTRIBUTION_INFO_V1 1
#endif
#ifndef IO_CHECK_SHARE_ACCESS_DONT_CHECK_DELETE
#define IO_CHECK_SHARE_ACCESS_DONT_CHECK_DELETE 0x00000010
#endif
#ifndef IO_CHECK_SHARE_ACCESS_DONT_CHECK_READ
#define IO_CHECK_SHARE_ACCESS_DONT_CHECK_READ 0x00000004
#endif
#ifndef IO_CHECK_SHARE_ACCESS_DONT_CHECK_WRITE
#define IO_CHECK_SHARE_ACCESS_DONT_CHECK_WRITE 0x00000008
#endif
#ifndef IO_CHECK_SHARE_ACCESS_DONT_UPDATE_FILE_OBJECT
#define IO_CHECK_SHARE_ACCESS_DONT_UPDATE_FILE_OBJECT 0x00000002
#endif
#ifndef IO_CHECK_SHARE_ACCESS_FORCE_CHECK
#define IO_CHECK_SHARE_ACCESS_FORCE_CHECK 0x00000020
#endif
#ifndef IO_CHECK_SHARE_ACCESS_FORCE_USING_SCB
#define IO_CHECK_SHARE_ACCESS_FORCE_USING_SCB 0x00000040
#endif
#ifndef IO_CHECK_SHARE_ACCESS_UPDATE_SHARE_ACCESS
#define IO_CHECK_SHARE_ACCESS_UPDATE_SHARE_ACCESS 0x00000001
#endif
#ifndef IO_REPARSE_GLOBAL
#define IO_REPARSE_GLOBAL 0x2
#endif
#ifndef IO_SET_IRP_IO_ATTRIBUTION_FLAGS_MASK
#define IO_SET_IRP_IO_ATTRIBUTION_FLAGS_MASK 0x3
#endif
#ifndef IO_SET_IRP_IO_ATTRIBUTION_FROM_PROCESS
#define IO_SET_IRP_IO_ATTRIBUTION_FROM_PROCESS 0x2
#endif
#ifndef IO_SET_IRP_IO_ATTRIBUTION_FROM_THREAD
#define IO_SET_IRP_IO_ATTRIBUTION_FROM_THREAD 0x1
#endif
#ifndef IO_SHARE_ACCESS_NON_PRIMARY_STREAM
#define IO_SHARE_ACCESS_NON_PRIMARY_STREAM 0x00000080
#endif
#ifndef IO_SHARE_ACCESS_NO_WRITE_PERMISSION
#define IO_SHARE_ACCESS_NO_WRITE_PERMISSION 0x80000000
#endif
#ifndef IO_TYPE_IORING
#define IO_TYPE_IORING 0x0000000e
#endif
#ifndef IRP_UM_DRIVER_INITIATED_IO
#define IRP_UM_DRIVER_INITIATED_IO 0x00400000
#endif
#ifndef KB_REMOVE_PAGES_FLAG_ADDITIONAL_RANGES_EXIST
#define KB_REMOVE_PAGES_FLAG_ADDITIONAL_RANGES_EXIST 0x80000000UL
#endif
#ifndef KB_REMOVE_PAGES_FLAG_PHYSICAL_ADDRESS
#define KB_REMOVE_PAGES_FLAG_PHYSICAL_ADDRESS 0x00000002UL
#endif
#ifndef KB_REMOVE_PAGES_FLAG_VIRTUAL_ADDRESS
#define KB_REMOVE_PAGES_FLAG_VIRTUAL_ADDRESS 0x00000001UL
#endif
#ifndef KB_SECONDARY_DATA_FLAG_NO_DEVICE_ACCESS
#define KB_SECONDARY_DATA_FLAG_NO_DEVICE_ACCESS 0x00000002
#endif
#ifndef KB_TRIAGE_DUMP_DATA_FLAG_BUGCHECK_ACTIVE
#define KB_TRIAGE_DUMP_DATA_FLAG_BUGCHECK_ACTIVE 0x00000001ul
#endif
#ifndef KERNEL_SOFT_RESTART_NOTIFICATION_VERSION
#define KERNEL_SOFT_RESTART_NOTIFICATION_VERSION 1
#endif
#ifndef LOCK_QUEUE_HALTED
#define LOCK_QUEUE_HALTED 4
#endif
#ifndef LOCK_QUEUE_HALTED_BIT
#define LOCK_QUEUE_HALTED_BIT 2
#endif
#ifndef LOG_POLICY_OVERWRITE
#define LOG_POLICY_OVERWRITE 0x01
#endif
#ifndef LOG_POLICY_PERSIST
#define LOG_POLICY_PERSIST 0x02
#endif
#ifndef LockQueueUnusedSpare0
#define LockQueueUnusedSpare0 0
#endif
#ifndef LockQueueUnusedSpare1
#define LockQueueUnusedSpare1 1
#endif
#ifndef LockQueueUnusedSpare15
#define LockQueueUnusedSpare15 15
#endif
#ifndef LockQueueUnusedSpare2
#define LockQueueUnusedSpare2 2
#endif
#ifndef LockQueueUnusedSpare3
#define LockQueueUnusedSpare3 3
#endif
#ifndef LockQueueUnusedSpare8
#define LockQueueUnusedSpare8 8
#endif
#ifndef MDL_LOCKED_PAGE_TABLES
#define MDL_LOCKED_PAGE_TABLES 0x0100
#endif
#ifndef MDL_PAGE_CONTENTS_INVARIANT
#define MDL_PAGE_CONTENTS_INVARIANT 0x4000
#endif
#ifndef MEMORY_PARTITION_MODIFY_ACCESS
#define MEMORY_PARTITION_MODIFY_ACCESS 0x0002
#endif
#ifndef MEMORY_PARTITION_QUERY_ACCESS
#define MEMORY_PARTITION_QUERY_ACCESS 0x0001
#endif
#ifndef MM_DUMP_MAP_CACHED
#define MM_DUMP_MAP_CACHED 0x1
#endif
#ifndef MM_DUMP_MAP_INVALIDATE
#define MM_DUMP_MAP_INVALIDATE 0x2
#endif
#ifndef MM_FREE_MDL_PAGES_ZERO
#define MM_FREE_MDL_PAGES_ZERO 0x1
#endif
#ifndef MM_PERMANENT_ADDRESS_IS_IO_SPACE
#define MM_PERMANENT_ADDRESS_IS_IO_SPACE 0x1
#endif
#ifndef MM_PROTECT_DRIVER_SECTION_ALLOW_UNLOAD
#define MM_PROTECT_DRIVER_SECTION_ALLOW_UNLOAD 0x1
#endif
#ifndef MdlMappingWithGuardPtes
#define MdlMappingWithGuardPtes 0x20000000
#endif
#ifndef NPEM_CONTROL_INTERFACE_VERSION1
#define NPEM_CONTROL_INTERFACE_VERSION1 1
#endif
#ifndef NPEM_CONTROL_INTERFACE_VERSION2
#define NPEM_CONTROL_INTERFACE_VERSION2 2
#endif
#ifndef PAGE_ENCLAVE_MASK
#define PAGE_ENCLAVE_MASK 0x10000000
#endif
#ifndef PAGE_ENCLAVE_NO_CHANGE
#define PAGE_ENCLAVE_NO_CHANGE 0x20000000
#endif
#ifndef PAGE_ENCLAVE_THREAD_CONTROL
#define PAGE_ENCLAVE_THREAD_CONTROL 0x80000000
#endif
#ifndef PAGE_ENCLAVE_UNVALIDATED
#define PAGE_ENCLAVE_UNVALIDATED 0x20000000
#endif
#ifndef PAGE_REVERT_TO_FILE_MAP
#define PAGE_REVERT_TO_FILE_MAP 0x80000000
#endif
#ifndef PCI_ATS_INTERFACE_VERSION
#define PCI_ATS_INTERFACE_VERSION 1
#endif
#ifndef PCI_PTM_TIME_SOURCE_AUX
#define PCI_PTM_TIME_SOURCE_AUX 0xFFFFFFFF
#endif
#ifndef PCI_SECURITY_DIRECT_TRANSLATED_P2P
#define PCI_SECURITY_DIRECT_TRANSLATED_P2P 0x4
#endif
#ifndef PCI_SECURITY_ENHANCED
#define PCI_SECURITY_ENHANCED 0x2
#endif
#ifndef PCI_SECURITY_FULLY_SUPPORTED
#define PCI_SECURITY_FULLY_SUPPORTED 0x1
#endif
#ifndef PCI_SECURITY_GUEST_ASSIGNED
#define PCI_SECURITY_GUEST_ASSIGNED 0x1
#endif
#ifndef PCI_SECURITY_INTERFACE_VERSION
#define PCI_SECURITY_INTERFACE_VERSION 1
#endif
#ifndef PCI_SECURITY_INTERFACE_VERSION2
#define PCI_SECURITY_INTERFACE_VERSION2 2
#endif
#ifndef PCI_SECURITY_SRIOV_DIRECT_TRANSLATED_P2P
#define PCI_SECURITY_SRIOV_DIRECT_TRANSLATED_P2P 0x40000
#endif
#ifndef PF_ARM_LSE2_AVAILABLE
#define PF_ARM_LSE2_AVAILABLE 62
#endif
#ifndef PF_ARM_SHA3_INSTRUCTIONS_AVAILABLE
#define PF_ARM_SHA3_INSTRUCTIONS_AVAILABLE 64
#endif
#ifndef PF_ARM_SHA512_INSTRUCTIONS_AVAILABLE
#define PF_ARM_SHA512_INSTRUCTIONS_AVAILABLE 65
#endif
#ifndef PF_ARM_SME2_1_INSTRUCTIONS_AVAILABLE
#define PF_ARM_SME2_1_INSTRUCTIONS_AVAILABLE 72
#endif
#ifndef PF_ARM_SME2_2_INSTRUCTIONS_AVAILABLE
#define PF_ARM_SME2_2_INSTRUCTIONS_AVAILABLE 73
#endif
#ifndef PF_ARM_SME2_INSTRUCTIONS_AVAILABLE
#define PF_ARM_SME2_INSTRUCTIONS_AVAILABLE 71
#endif
#ifndef PF_ARM_SME_AES_INSTRUCTIONS_AVAILABLE
#define PF_ARM_SME_AES_INSTRUCTIONS_AVAILABLE 74
#endif
#ifndef PF_ARM_SME_B16B16_INSTRUCTIONS_AVAILABLE
#define PF_ARM_SME_B16B16_INSTRUCTIONS_AVAILABLE 84
#endif
#ifndef PF_ARM_SME_F16F16_INSTRUCTIONS_AVAILABLE
#define PF_ARM_SME_F16F16_INSTRUCTIONS_AVAILABLE 83
#endif
#ifndef PF_ARM_SME_F64F64_INSTRUCTIONS_AVAILABLE
#define PF_ARM_SME_F64F64_INSTRUCTIONS_AVAILABLE 85
#endif
#ifndef PF_ARM_SME_F8F16_INSTRUCTIONS_AVAILABLE
#define PF_ARM_SME_F8F16_INSTRUCTIONS_AVAILABLE 82
#endif
#ifndef PF_ARM_SME_F8F32_INSTRUCTIONS_AVAILABLE
#define PF_ARM_SME_F8F32_INSTRUCTIONS_AVAILABLE 81
#endif
#ifndef PF_ARM_SME_FA64_INSTRUCTIONS_AVAILABLE
#define PF_ARM_SME_FA64_INSTRUCTIONS_AVAILABLE 88
#endif
#ifndef PF_ARM_SME_I16I64_INSTRUCTIONS_AVAILABLE
#define PF_ARM_SME_I16I64_INSTRUCTIONS_AVAILABLE 86
#endif
#ifndef PF_ARM_SME_INSTRUCTIONS_AVAILABLE
#define PF_ARM_SME_INSTRUCTIONS_AVAILABLE 70
#endif
#ifndef PF_ARM_SME_LUTv2_INSTRUCTIONS_AVAILABLE
#define PF_ARM_SME_LUTv2_INSTRUCTIONS_AVAILABLE 87
#endif
#ifndef PF_ARM_SME_SBITPERM_INSTRUCTIONS_AVAILABLE
#define PF_ARM_SME_SBITPERM_INSTRUCTIONS_AVAILABLE 75
#endif
#ifndef PF_ARM_SME_SF8DP2_INSTRUCTIONS_AVAILABLE
#define PF_ARM_SME_SF8DP2_INSTRUCTIONS_AVAILABLE 78
#endif
#ifndef PF_ARM_SME_SF8DP4_INSTRUCTIONS_AVAILABLE
#define PF_ARM_SME_SF8DP4_INSTRUCTIONS_AVAILABLE 79
#endif
#ifndef PF_ARM_SME_SF8FMA_INSTRUCTIONS_AVAILABLE
#define PF_ARM_SME_SF8FMA_INSTRUCTIONS_AVAILABLE 80
#endif
#ifndef PF_ARM_SME_SF8MM4_INSTRUCTIONS_AVAILABLE
#define PF_ARM_SME_SF8MM4_INSTRUCTIONS_AVAILABLE 76
#endif
#ifndef PF_ARM_SME_SF8MM8_INSTRUCTIONS_AVAILABLE
#define PF_ARM_SME_SF8MM8_INSTRUCTIONS_AVAILABLE 77
#endif
#ifndef PF_ARM_SVE2_1_INSTRUCTIONS_AVAILABLE
#define PF_ARM_SVE2_1_INSTRUCTIONS_AVAILABLE 48
#endif
#ifndef PF_ARM_SVE2_INSTRUCTIONS_AVAILABLE
#define PF_ARM_SVE2_INSTRUCTIONS_AVAILABLE 47
#endif
#ifndef PF_ARM_SVE_AES_INSTRUCTIONS_AVAILABLE
#define PF_ARM_SVE_AES_INSTRUCTIONS_AVAILABLE 49
#endif
#ifndef PF_ARM_SVE_B16B16_INSTRUCTIONS_AVAILABLE
#define PF_ARM_SVE_B16B16_INSTRUCTIONS_AVAILABLE 54
#endif
#ifndef PF_ARM_SVE_BF16_INSTRUCTIONS_AVAILABLE
#define PF_ARM_SVE_BF16_INSTRUCTIONS_AVAILABLE 52
#endif
#ifndef PF_ARM_SVE_BITPERM_INSTRUCTIONS_AVAILABLE
#define PF_ARM_SVE_BITPERM_INSTRUCTIONS_AVAILABLE 51
#endif
#ifndef PF_ARM_SVE_EBF16_INSTRUCTIONS_AVAILABLE
#define PF_ARM_SVE_EBF16_INSTRUCTIONS_AVAILABLE 53
#endif
#ifndef PF_ARM_SVE_F32MM_INSTRUCTIONS_AVAILABLE
#define PF_ARM_SVE_F32MM_INSTRUCTIONS_AVAILABLE 58
#endif
#ifndef PF_ARM_SVE_F64MM_INSTRUCTIONS_AVAILABLE
#define PF_ARM_SVE_F64MM_INSTRUCTIONS_AVAILABLE 59
#endif
#ifndef PF_ARM_SVE_I8MM_INSTRUCTIONS_AVAILABLE
#define PF_ARM_SVE_I8MM_INSTRUCTIONS_AVAILABLE 57
#endif
#ifndef PF_ARM_SVE_INSTRUCTIONS_AVAILABLE
#define PF_ARM_SVE_INSTRUCTIONS_AVAILABLE 46
#endif
#ifndef PF_ARM_SVE_PMULL128_INSTRUCTIONS_AVAILABLE
#define PF_ARM_SVE_PMULL128_INSTRUCTIONS_AVAILABLE 50
#endif
#ifndef PF_ARM_SVE_SHA3_INSTRUCTIONS_AVAILABLE
#define PF_ARM_SVE_SHA3_INSTRUCTIONS_AVAILABLE 55
#endif
#ifndef PF_ARM_SVE_SM4_INSTRUCTIONS_AVAILABLE
#define PF_ARM_SVE_SM4_INSTRUCTIONS_AVAILABLE 56
#endif
#ifndef PF_ARM_V82_FP16_INSTRUCTIONS_AVAILABLE
#define PF_ARM_V82_FP16_INSTRUCTIONS_AVAILABLE 67
#endif
#ifndef PF_ARM_V82_I8MM_INSTRUCTIONS_AVAILABLE
#define PF_ARM_V82_I8MM_INSTRUCTIONS_AVAILABLE 66
#endif
#ifndef PF_ARM_V86_BF16_INSTRUCTIONS_AVAILABLE
#define PF_ARM_V86_BF16_INSTRUCTIONS_AVAILABLE 68
#endif
#ifndef PF_ARM_V86_EBF16_INSTRUCTIONS_AVAILABLE
#define PF_ARM_V86_EBF16_INSTRUCTIONS_AVAILABLE 69
#endif
#ifndef PF_BMI2_INSTRUCTIONS_AVAILABLE
#define PF_BMI2_INSTRUCTIONS_AVAILABLE 60
#endif
#ifndef PF_ERMS_AVAILABLE
#define PF_ERMS_AVAILABLE 42
#endif
#ifndef PF_MONITORX_INSTRUCTION_AVAILABLE
#define PF_MONITORX_INSTRUCTION_AVAILABLE 35
#endif
#ifndef PF_MOVDIR64B_INSTRUCTION_AVAILABLE
#define PF_MOVDIR64B_INSTRUCTION_AVAILABLE 61
#endif
#ifndef PF_RESERVED_FEATURE
#define PF_RESERVED_FEATURE 63
#endif
#ifndef PNP_DEVICE_ASSIGNED_TO_GUEST
#define PNP_DEVICE_ASSIGNED_TO_GUEST 0x00000100
#endif
#ifndef PNP_DEVICE_DISCONNECTED
#define PNP_DEVICE_DISCONNECTED 0x00000040
#endif
#ifndef PNP_DEVICE_RESOURCE_UPDATED
#define PNP_DEVICE_RESOURCE_UPDATED 0x00000080
#endif
#ifndef PNP_EXTENDED_ADDRESS_INTERFACE_VERSION
#define PNP_EXTENDED_ADDRESS_INTERFACE_VERSION 0x1
#endif
#ifndef POOL_CREATE_FLG_NONPAGED_POOL
#define POOL_CREATE_FLG_NONPAGED_POOL 0x00000008
#endif
#ifndef POOL_CREATE_FLG_NUMA_AWARE
#define POOL_CREATE_FLG_NUMA_AWARE 0x00000010
#endif
#ifndef POOL_CREATE_FLG_PAGED_POOL
#define POOL_CREATE_FLG_PAGED_POOL 0x00000004
#endif
#ifndef POOL_CREATE_FLG_SECURE_POOL
#define POOL_CREATE_FLG_SECURE_POOL 0x00000001
#endif
#ifndef POOL_CREATE_FLG_USE_GLOBAL_POOL
#define POOL_CREATE_FLG_USE_GLOBAL_POOL 0x00000002
#endif
#ifndef POOL_CREATE_PARAMS_VERSION
#define POOL_CREATE_PARAMS_VERSION 1
#endif
#ifndef PO_EFFECTIVE_POWER_MODE_V1
#define PO_EFFECTIVE_POWER_MODE_V1 0x00000001
#endif
#ifndef PO_EFFECTIVE_POWER_MODE_V2
#define PO_EFFECTIVE_POWER_MODE_V2 0x00000002
#endif
#ifndef PO_FX_COMPONENT_FLAG_F0_ON_CHILD_D0
#define PO_FX_COMPONENT_FLAG_F0_ON_CHILD_D0 0x0000000000000004
#endif
#ifndef PO_FX_DEVICE_FLAG_ENABLE_FAST_RESUME
#define PO_FX_DEVICE_FLAG_ENABLE_FAST_RESUME 0x0000000000000010ull
#endif
#ifndef PO_FX_DEVICE_FLAG_NO_FAULT_CALLBACKS
#define PO_FX_DEVICE_FLAG_NO_FAULT_CALLBACKS 0x0000000000000020ull
#endif
#ifndef PO_FX_DEVICE_FLAG_RESERVED_2
#define PO_FX_DEVICE_FLAG_RESERVED_2 0x0000000000000040ull
#endif
#ifndef PO_MEM_BOOT_PHASE
#define PO_MEM_BOOT_PHASE 0x00010000
#endif
#ifndef PO_MEM_CLONE
#define PO_MEM_CLONE 0x00000002
#endif
#ifndef PO_MEM_CL_OR_NCHK
#define PO_MEM_CL_OR_NCHK 0x00000004
#endif
#ifndef PO_MEM_DISCARD
#define PO_MEM_DISCARD 0x00008000
#endif
#ifndef PO_MEM_PAGE_ADDRESS
#define PO_MEM_PAGE_ADDRESS 0x00004000
#endif
#ifndef PO_MEM_PRESERVE
#define PO_MEM_PRESERVE 0x00000001
#endif
#ifndef REG_FLUSH_HIVE_FILE_GROWTH
#define REG_FLUSH_HIVE_FILE_GROWTH 0x00001000L
#endif
#ifndef REG_IMMUTABLE
#define REG_IMMUTABLE 0x00004000L
#endif
#ifndef REG_LOAD_HIVE_OPEN_HANDLE
#define REG_LOAD_HIVE_OPEN_HANDLE 0x00000800L
#endif
#ifndef REG_NO_IMPERSONATION_FALLBACK
#define REG_NO_IMPERSONATION_FALLBACK 0x00008000L
#endif
#ifndef REG_OPEN_READ_ONLY
#define REG_OPEN_READ_ONLY 0x00002000L
#endif
#ifndef RTL_GUID_STRING_SIZE
#define RTL_GUID_STRING_SIZE 38
#endif
#ifndef RTL_QUERY_REGISTRY_NOSTRING
#define RTL_QUERY_REGISTRY_NOSTRING 0x00000080
#endif
#ifndef SDEV_IDENTIFIER_INTERFACE_VERSION
#define SDEV_IDENTIFIER_INTERFACE_VERSION 1
#endif
#ifndef SECURE_DRIVER_INTERFACE_VERSION
#define SECURE_DRIVER_INTERFACE_VERSION 1
#endif
#ifndef SECURE_SECTION_ALLOW_PARTIAL_MDL
#define SECURE_SECTION_ALLOW_PARTIAL_MDL 1
#endif
#ifndef SEC_64K_PAGES
#define SEC_64K_PAGES 0x00080000
#endif
#ifndef SEC_HUGE_PAGES
#define SEC_HUGE_PAGES 0x00020000
#endif
#ifndef SL_BYPASS_ACCESS_CHECK
#define SL_BYPASS_ACCESS_CHECK 0x01
#endif
#ifndef SL_BYPASS_IO
#define SL_BYPASS_IO 0x40
#endif
#ifndef SL_FORCE_ASYNCHRONOUS
#define SL_FORCE_ASYNCHRONOUS 0x01
#endif
#ifndef SL_IGNORE_READONLY_ATTRIBUTE
#define SL_IGNORE_READONLY_ATTRIBUTE 0x40
#endif
#ifndef SL_INFO_FORCE_ACCESS_CHECK
#define SL_INFO_FORCE_ACCESS_CHECK 0x01
#endif
#ifndef SL_INFO_IGNORE_READONLY_ATTRIBUTE
#define SL_INFO_IGNORE_READONLY_ATTRIBUTE 0x40
#endif
#ifndef SL_NO_CURSOR_UPDATE
#define SL_NO_CURSOR_UPDATE 0x10
#endif
#ifndef SL_PERSISTENT_MEMORY_FIXED_MAPPING
#define SL_PERSISTENT_MEMORY_FIXED_MAPPING 0x20
#endif
#ifndef SL_QUERY_DIRECTORY_MASK
#define SL_QUERY_DIRECTORY_MASK 0x1b
#endif
#ifndef SL_RETURN_ON_DISK_ENTRIES_ONLY
#define SL_RETURN_ON_DISK_ENTRIES_ONLY 0x08
#endif
#ifndef SYMBOLIC_LINK_SET
#define SYMBOLIC_LINK_SET 0x0002
#endif
#ifndef TREE_CONNECT_ATTRIBUTE_GLOBAL
#define TREE_CONNECT_ATTRIBUTE_GLOBAL 0x00000004
#endif
#ifndef TREE_CONNECT_ATTRIBUTE_INTEGRITY
#define TREE_CONNECT_ATTRIBUTE_INTEGRITY 0x00008000
#endif
#ifndef TREE_CONNECT_ATTRIBUTE_PINNED
#define TREE_CONNECT_ATTRIBUTE_PINNED 0x00000002
#endif
#ifndef TREE_CONNECT_ATTRIBUTE_PRIVACY
#define TREE_CONNECT_ATTRIBUTE_PRIVACY 0x00004000
#endif
#ifndef TREE_CONNECT_NO_CLIENT_BUFFERING
#define TREE_CONNECT_NO_CLIENT_BUFFERING 0x00000008
#endif
#ifndef TREE_CONNECT_WRITE_THROUGH
#define TREE_CONNECT_WRITE_THROUGH 0x00000002
#endif
#ifndef VPB_DISMOUNTING
#define VPB_DISMOUNTING 0x0080
#endif
#ifndef VPB_FLAGS_BYPASSIO_BLOCKED
#define VPB_FLAGS_BYPASSIO_BLOCKED 0x0040
#endif
#ifndef XSTATE_CONTEXT_FLAG_LOOKASIDE
#define XSTATE_CONTEXT_FLAG_LOOKASIDE 0x1
#endif
#ifndef _STRSAFE_USE_SECURE_CRT
#define _STRSAFE_USE_SECURE_CRT 0
#endif
