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
    PowerLimitTypeMax,
    PowerLimitType1 = 0,
    PowerLimitType2 = 1,
    PowerLimitType3 = 2,
    PowerLimitType4 = 3,
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

typedef enum _CLFS_CONTEXT_MODE {
    ClfsContextNone = 0,
    ClfsContextUndoNext = 1,
    ClfsContextPrevious = 2,
    ClfsContextForward = 3
} CLFS_CONTEXT_MODE, *PCLFS_CONTEXT_MODE;

typedef enum _CLFS_IOSTATS_CLASS {
    ClfsIoStatsDefault = 0,
    ClfsIoStatsMax = 0xFFFF
} CLFS_IOSTATS_CLASS, *PCLFS_IOSTATS_CLASS;

typedef enum _CLFS_LOG_ARCHIVE_MODE {
    ClfsLogArchiveEnabled = 1,
    ClfsLogArchiveDisabled = 2
} CLFS_LOG_ARCHIVE_MODE, *PCLFS_LOG_ARCHIVE_MODE;

typedef enum _CLFS_MGMT_NOTIFICATION_TYPE {
    ClfsMgmtAdvanceTailNotification = 0,
    ClfsMgmtLogFullHandlerNotification = 1,
    ClfsMgmtLogUnpinnedNotification = 2,
    ClfsMgmtLogWriteNotification = 3
} CLFS_MGMT_NOTIFICATION_TYPE, *PCLFS_MGMT_NOTIFICATION_TYPE;

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

typedef enum _CLS_CONTEXT_MODE {
    ClsContextNone = 0,
    ClsContextUndoNext = 1,
    ClsContextPrevious = 2,
    ClsContextForward = 3
} CLS_CONTEXT_MODE, *PCLS_CONTEXT_MODE;

typedef enum _CLS_IOSTATS_CLASS {
    ClsIoStatsDefault = 0,
    ClsIoStatsMax = 0xFFFF
} CLS_IOSTATS_CLASS, *PCLS_IOSTATS_CLASS;

typedef enum _CLS_LOG_INFORMATION_CLASS {
    ClfsLogBasicInformation = 0,
    ClfsLogBasicInformationPhysical = 1,
    ClfsLogPhysicalNameInformation = 2,
    ClfsLogStreamIdentifierInformation = 3,
    ClfsLogSystemMarkingInformation = 4,
    ClfsLogPhysicalLsnInformation = 5
} CLS_LOG_INFORMATION_CLASS, *PCLS_LOG_INFORMATION_CLASS, CLFS_LOG_INFORMATION_CLASS, *PCLFS_LOG_INFORMATION_CLASS;

typedef enum _DRIVER_PROXY_REGISTER_CALLBACK_PHASE {
    DriverProxyRegisterCallbackPreProcess = 0,
    DriverProxyRegisterCallbackProxyStalled = 1,
    DriverProxyRegisterCallbackPostProcess = 2,
    DriverProxyRegisterCallbackMax = 3
} DRIVER_PROXY_REGISTER_CALLBACK_PHASE, *PDRIVER_PROXY_REGISTER_CALLBACK_PHASE;

typedef enum _ENERGY_SAVER_STATUS {
    ENERGY_SAVER_OFF = 0,
    ENERGY_SAVER_STANDARD = 1,
    ENERGY_SAVER_HIGH_SAVINGS = 2
} ENERGY_SAVER_STATUS, *PENERGY_SAVER_STATUS;

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

typedef enum _MEM_DEDICATED_ATTRIBUTE_TYPE {
    MemDedicatedAttributeReadBandwidth = 0,
    MemDedicatedAttributeReadLatency = 1,
    MemDedicatedAttributeWriteBandwidth = 2,
    MemDedicatedAttributeWriteLatency = 3,
    MemDedicatedAttributeMax = 4
} MEM_DEDICATED_ATTRIBUTE_TYPE, *PMEM_DEDICATED_ATTRIBUTE_TYPE;

typedef enum _MM_MDL_PAGE_CONTENTS_STATE {
    MmMdlPageContentsDynamic = 0,
    MmMdlPageContentsInvariant = 1,
    MmMdlPageContentsQuery = 2
} MM_MDL_PAGE_CONTENTS_STATE, PMM_MDL_PAGE_CONTENTS_STATE;

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

typedef enum _OS_DEPLOYEMENT_STATE_VALUES {
    OS_DEPLOYMENT_STANDARD = 1,
    OS_DEPLOYMENT_COMPACT = 2
} OS_DEPLOYEMENT_STATE_VALUES;

typedef enum _PARTITION_INFORMATION_CLASS {
    SystemMemoryPartitionInformation = 0,
    SystemMemoryPartitionDedicatedMemoryInformation = 9,
    SystemMemoryPartitionOpenDedicatedMemory = 10
} PARTITION_INFORMATION_CLASS, *PPARTITION_INFORMATION_CLASS;

typedef enum _PCI_ACS_BIT {
    PciAcsReserved = 0,
    PciAcsBitEnable = 1,
    PciAcsBitDisable = 2,
    PciAcsBitDontCare = 3
} PCI_ACS_BIT, *PPCI_ACS_BIT;

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

typedef enum _POOL_CREATE_EXTENDED_PARAMETER_TYPE {
    PoolCreateExtendedParameterInvalidType = 0,
    PoolCreateExtendedParameterName = 1
} POOL_CREATE_EXTENDED_PARAMETER_TYPE, *PPOOL_CREATE_EXTENDED_PARAMETER_TYPE;

typedef enum _POWER_MONITOR_REQUEST_TYPE {
    MonitorRequestTypeOff = 0,
    MonitorRequestTypeOnAndPresent = 1,
    MonitorRequestTypeToggleOn = 2
} POWER_MONITOR_REQUEST_TYPE;

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

typedef enum _USER_ACTIVITY_PRESENCE {
    PowerUserPresent = 0,
    PowerUserNotPresent = 1,
    PowerUserInactive = 2,
    PowerUserMaximum = 3,
    PowerUserInvalid = 3
} USER_ACTIVITY_PRESENCE, *PUSER_ACTIVITY_PRESENCE;

typedef enum MEM_SECTION_EXTENDED_PARAMETER_TYPE {
    MemSectionExtendedParameterInvalidType = 0,
    MemSectionExtendedParameterUserPhysicalFlags = 1,
    MemSectionExtendedParameterNumaNode = 2,
    MemSectionExtendedParameterSigningLevel = 3,
    MemSectionExtendedParameterMax = 4,
    MemSectionExtendedParameterAttributeFlags = 4,
} MEM_SECTION_EXTENDED_PARAMETER_TYPE, *PMEM_SECTION_EXTENDED_PARAMETER_TYPE;

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
    MonitorRequestReasonMax = 58,
    MonitorRequestReasonRestrictedStandbyBatteryDrain = 0x37,
} POWER_MONITOR_REQUEST_REASON;

typedef enum POWER_SETTING_ALTITUDE {
    ALTITUDE_GROUP_POLICY = 0,
    ALTITUDE_USER = 1,
    ALTITUDE_RUNTIME_OVERRIDE = 2,
    ALTITUDE_PROVISIONING = 3,
    ALTITUDE_OEM_CUSTOMIZATION = 4,
    ALTITUDE_INTERNAL_OVERRIDE = 5,
    ALTITUDE_OS_DEFAULT = 6
} POWER_SETTING_ALTITUDE, *PPOWER_SETTING_ALTITUDE;

typedef enum POWER_USER_PRESENCE_TYPE {
    UserNotPresent = 0,
    UserPresent = 1,
    UserUnknown = 255
} POWER_USER_PRESENCE_TYPE, *PPOWER_USER_PRESENCE_TYPE;

typedef ULONG CLFS_CONTAINER_ID;
typedef GUID CLFS_LOG_ID;
typedef PVOID CLFS_MGMT_CLIENT;
typedef UCHAR CLFS_SCAN_MODE;
typedef UINT32 CLS_CONTAINER_STATE;
typedef CLS_CONTAINER_STATE CLFS_CONTAINER_STATE;
typedef UCHAR CLS_RECORD_TYPE;
typedef CLS_RECORD_TYPE CLFS_RECORD_TYPE;
typedef ULONG DRIVER_PROXY_ENDPOINT_FUNCTION_ID;
typedef GUID KCRM_PROTOCOL_ID;
typedef FILE_OBJECT LOG_FILE_OBJECT;
typedef ULONG MM_NODE_NUMBER_ONE_BASED;
typedef ULONG MM_NODE_NUMBER_ZERO_BASED;
typedef PVOID PCLAIMS_BLOB;
typedef CLFS_CONTAINER_ID * PCLFS_CONTAINER_ID;
typedef CLS_CONTAINER_STATE * PCLFS_CONTAINER_STATE;
typedef PVOID * PCLFS_MGMT_CLIENT;
typedef CLS_RECORD_TYPE * PCLFS_RECORD_TYPE;
typedef UCHAR * PCLFS_SCAN_MODE;
typedef UINT32 * PCLS_CONTAINER_STATE;
typedef UCHAR * PCLS_RECORD_TYPE;
typedef ULONG * PDRIVER_PROXY_ENDPOINT_FUNCTION_ID;
typedef GUID * PKCRM_PROTOCOL_ID;
typedef FILE_OBJECT * PLOG_FILE_OBJECT;
typedef CLFS_CONTAINER_ID ** PPCLFS_CONTAINER_ID;
typedef CLS_CONTAINER_STATE * PPCLFS_CONTAINER_STATE;
typedef enum _CLFS_CONTEXT_MODE ** PPCLFS_CONTEXT_MODE;
typedef enum _CLFS_IOSTATS_CLASS ** PPCLFS_IOSTATS_CLASS;
typedef CLFS_LOG_INFORMATION_CLASS ** PPCLFS_LOG_INFORMATION_CLASS;
typedef CLS_RECORD_TYPE ** PPCLFS_RECORD_TYPE;
typedef UINT32 * PPCLS_CONTAINER_STATE;
typedef enum _CLS_CONTEXT_MODE ** PPCLS_CONTEXT_MODE;
typedef enum _CLS_IOSTATS_CLASS ** PPCLS_IOSTATS_CLASS;
typedef enum _CLS_LOG_INFORMATION_CLASS ** PPCLS_LOG_INFORMATION_CLASS;
typedef UCHAR ** PPCLS_RECORD_TYPE;
typedef FILE_OBJECT ** PPLOG_FILE_OBJECT;
typedef PVOID PS_AVAILABLE_CPUS_CHANGE_REGISTRATION;
typedef PVOID * PSE_IMAGE_VERIFICATION_CALLBACK_TOKEN;
typedef PVOID SE_IMAGE_VERIFICATION_CALLBACK_TOKEN;

#ifdef __cplusplus
extern "C" {
#endif
FIRMWARE_TYPE NTAPI ExGetFirmwareType(void);
BOOLEAN NTAPI ExIsManufacturingModeEnabled(void);
BOOLEAN NTAPI ExIsSoftBoot(void);
NTSTATUS NTAPI KeConvertAuxiliaryCounterToPerformanceCounter(ULONG64 AuxiliaryCounterValue, PULONG64 PerformanceCounterValue, PULONG64 ConversionError);
void NTAPI PoSetHiberRange(PVOID MemoryMap, ULONG Flags, PVOID Address, ULONG_PTR Length, ULONG Tag);
ULONG NTAPI RtlCrc32(const void * Buffer, size_t Size, ULONG InitialCrc);
ULONGLONG NTAPI RtlCrc64(const void * Buffer, size_t Size, ULONGLONG InitialCrc);
#ifdef __cplusplus
}
#endif

typedef struct _CLFS_LOG_NAME_INFORMATION {
  USHORT NameLengthInBytes;
  WCHAR Name[1];
} CLFS_LOG_NAME_INFORMATION, *PCLFS_LOG_NAME_INFORMATION;

typedef struct _CLFS_NODE_ID {
  ULONG cType;
  ULONG cbNode;
} CLFS_NODE_ID, *PCLFS_NODE_ID;

typedef struct _CLFS_STREAM_ID_INFORMATION {
  UCHAR StreamIdentifier;
} CLFS_STREAM_ID_INFORMATION, *PCLFS_STREAM_ID_INFORMATION;

typedef struct _CLS_CONTAINER_INFORMATION {
  ULONG FileAttributes;
  ULONGLONG CreationTime;
  ULONGLONG LastAccessTime;
  ULONGLONG LastWriteTime;
  LONGLONG ContainerSize;
  ULONG FileNameActualLength;
  ULONG FileNameLength;
  WCHAR FileName[256];
  CLFS_CONTAINER_STATE State;
  CLFS_CONTAINER_ID PhysicalContainerId;
  CLFS_CONTAINER_ID LogicalContainerId;
} CLS_CONTAINER_INFORMATION, *PCLS_CONTAINER_INFORMATION;

typedef struct _CLS_ARCHIVE_DESCRIPTOR {
  ULONGLONG coffLow;
  ULONGLONG coffHigh;
  CLS_CONTAINER_INFORMATION infoContainer;
} CLS_ARCHIVE_DESCRIPTOR, *PCLS_ARCHIVE_DESCRIPTOR;

typedef struct _CLS_IO_STATISTICS_HEADER {
  UCHAR ubMajorVersion;
  UCHAR ubMinorVersion;
  CLFS_IOSTATS_CLASS eStatsClass;
  USHORT cbLength;
  ULONG coffData;
} CLS_IO_STATISTICS_HEADER, *PCLS_IO_STATISTICS_HEADER;

typedef struct _CLS_IO_STATISTICS {
  CLS_IO_STATISTICS_HEADER hdrIoStats;
  ULONGLONG cFlush;
  ULONGLONG cbFlush;
  ULONGLONG cMetaFlush;
  ULONGLONG cbMetaFlush;
} CLS_IO_STATISTICS, *PCLS_IO_STATISTICS;

typedef struct _CLS_WRITE_ENTRY {
  PVOID Buffer;
  ULONG ByteLength;
} CLS_WRITE_ENTRY, *PCLS_WRITE_ENTRY;

typedef struct _DPC_WATCHDOG_GLOBAL_TRIAGE_BLOCK {
  ULONG Signature;
  USHORT Revision;
  USHORT Size;
  USHORT DpcWatchdogProfileOffset;
  ULONG DpcWatchdogProfileLength;
} DPC_WATCHDOG_GLOBAL_TRIAGE_BLOCK, *PDPC_WATCHDOG_GLOBAL_TRIAGE_BLOCK;

typedef struct _EX_RCU_FREE_POOL_CONTEXT {
  ULONG_PTR Placeholder[6];
} EX_RCU_FREE_POOL_CONTEXT, *PEX_RCU_FREE_POOL_CONTEXT;

typedef struct _EXTENDED_CREATE_DUAL_OPLOCK_KEYS {
  GUID ParentOplockKey;
  GUID TargetOplockKey;
} EXTENDED_CREATE_DUAL_OPLOCK_KEYS, *PEXTENDED_CREATE_DUAL_OPLOCK_KEYS;

typedef struct _EXTENDED_CREATE_INFORMATION {
  LONGLONG ExtendedCreateFlags;
  PVOID EaBuffer;
  ULONG EaLength;
  PEXTENDED_CREATE_DUAL_OPLOCK_KEYS DualOplockKeys;
} EXTENDED_CREATE_INFORMATION, *PEXTENDED_CREATE_INFORMATION;

typedef struct _FILE_IO_PRIORITY_HINT_INFORMATION_EX {
  IO_PRIORITY_HINT PriorityHint;
  BOOLEAN BoostOutstanding;
} FILE_IO_PRIORITY_HINT_INFORMATION_EX, *PFILE_IO_PRIORITY_HINT_INFORMATION_EX;

typedef struct _FILE_STANDARD_INFORMATION_EX {
  LARGE_INTEGER AllocationSize;
  LARGE_INTEGER EndOfFile;
  ULONG NumberOfLinks;
  BOOLEAN DeletePending;
  BOOLEAN Directory;
  BOOLEAN AlternateStream;
  BOOLEAN MetadataAttribute;
} FILE_STANDARD_INFORMATION_EX, *PFILE_STANDARD_INFORMATION_EX;

typedef struct _KBUGCHECK_REMOVE_PAGES {
  PVOID Context;
  ULONG Flags;
  ULONG BugCheckCode;
  ULONG_PTR Address;
  ULONG_PTR Count;
} KBUGCHECK_REMOVE_PAGES, *PKBUGCHECK_REMOVE_PAGES;

typedef struct _KBUGCHECK_SECONDARY_DUMP_DATA_EX {
  PVOID InBuffer;
  ULONG InBufferLength;
  ULONG MaximumAllowed;
  GUID Guid;
  PVOID OutBuffer;
  ULONG OutBufferLength;
  PVOID Context;
  ULONG Flags;
  ULONG DumpType;
  ULONG BugCheckCode;
  ULONG_PTR BugCheckParameter1;
  ULONG_PTR BugCheckParameter2;
  ULONG_PTR BugCheckParameter3;
  ULONG_PTR BugCheckParameter4;
} KBUGCHECK_SECONDARY_DUMP_DATA_EX, *PKBUGCHECK_SECONDARY_DUMP_DATA_EX;

typedef struct _KBUGCHECK_TRIAGE_DUMP_DATA {
  PKTRIAGE_DUMP_DATA_ARRAY DataArray;
  ULONG Flags;
  ULONG MaxVirtMemSize;
  ULONG BugCheckCode;
  ULONG_PTR BugCheckParameter1;
  ULONG_PTR BugCheckParameter2;
  ULONG_PTR BugCheckParameter3;
  ULONG_PTR BugCheckParameter4;
} KBUGCHECK_TRIAGE_DUMP_DATA, *PKBUGCHECK_TRIAGE_DUMP_DATA;

typedef struct _KE_SRCU_LOCK {
  ULONG_PTR Placeholder[2];
} *PKE_SRCU_LOCK, KE_SRCU_LOCK;

typedef struct _KERNEL_SOFT_RESTART_NOTIFICATION {
  USHORT Version;
  USHORT Size;
  GUID Event;
} KERNEL_SOFT_RESTART_NOTIFICATION, *PKERNEL_SOFT_RESTART_NOTIFICATION;

typedef struct _KEY_TRUST_INFORMATION {
  ULONG TrustedKey : 1;
  ULONG Reserved : 31;
} KEY_TRUST_INFORMATION, *PKEY_TRUST_INFORMATION;

typedef struct _KEY_VALUE_LAYER_INFORMATION {
  ULONG IsTombstone : 1;
  ULONG Reserved : 31;
} KEY_VALUE_LAYER_INFORMATION, *PKEY_VALUE_LAYER_INFORMATION;

typedef struct _KWAIT_CHAIN {
  PVOID Head;
} KWAIT_CHAIN, *PKWAIT_CHAIN;

typedef struct _LINK_SHARE_ACCESS {
  ULONG OpenCount;
  ULONG Deleters;
  ULONG SharedDelete;
} LINK_SHARE_ACCESS, *PLINK_SHARE_ACCESS;

typedef struct _MEMORY_PARTITION_DEDICATED_MEMORY_OPEN_INFORMATION {
  ULONG64 DedicatedMemoryTypeId;
  ULONG HandleAttributes;
  ACCESS_MASK DesiredAccess;
  HANDLE DedicatedMemoryPartitionHandle;
} MEMORY_PARTITION_DEDICATED_MEMORY_OPEN_INFORMATION, *PMEMORY_PARTITION_DEDICATED_MEMORY_OPEN_INFORMATION;

typedef struct _POWER_IDLE_RESILIENCY {
  ULONG CoalescingTimeout;
  ULONG IdleResiliencyPeriod;
} POWER_IDLE_RESILIENCY, *PPOWER_IDLE_RESILIENCY;

typedef struct _POWER_MONITOR_INVOCATION {
  BOOLEAN Console;
  POWER_MONITOR_REQUEST_REASON RequestReason;
} POWER_MONITOR_INVOCATION, *PPOWER_MONITOR_INVOCATION;

typedef struct _POWER_SESSION_ALLOW_EXTERNAL_DMA_DEVICES {
  BOOLEAN IsAllowed;
} POWER_SESSION_ALLOW_EXTERNAL_DMA_DEVICES, *PPOWER_SESSION_ALLOW_EXTERNAL_DMA_DEVICES;

typedef struct _POWER_SESSION_CONNECT {
  BOOLEAN Connected;
  BOOLEAN Console;
} POWER_SESSION_CONNECT, *PPOWER_SESSION_CONNECT;

typedef struct _POWER_SESSION_RIT_STATE {
  BOOLEAN Active;
  ULONG64 LastInputTime;
} POWER_SESSION_RIT_STATE, *PPOWER_SESSION_RIT_STATE;

typedef struct _POWER_SESSION_TIMEOUTS {
  ULONG InputTimeout;
  ULONG DisplayTimeout;
} POWER_SESSION_TIMEOUTS, *PPOWER_SESSION_TIMEOUTS;

typedef struct _POWER_SESSION_WINLOGON {
  ULONG SessionId;
  BOOLEAN Console;
  BOOLEAN Locked;
} POWER_SESSION_WINLOGON, *PPOWER_SESSION_WINLOGON;

typedef struct _POWER_USER_PRESENCE {
  POWER_USER_PRESENCE_TYPE UserPresence;
} POWER_USER_PRESENCE, *PPOWER_USER_PRESENCE;

typedef struct _REG_LOAD_KEY_INFORMATION_V2 {
  PVOID Object;
  PUNICODE_STRING KeyName;
  PUNICODE_STRING SourceFile;
  ULONG Flags;
  PVOID TrustClassObject;
  PVOID UserEvent;
  ACCESS_MASK DesiredAccess;
  PHANDLE RootHandle;
  PVOID CallContext;
  PVOID ObjectContext;
  ULONG_PTR Version;
  PVOID FileAccessToken;
} REG_LOAD_KEY_INFORMATION_V2, *PREG_LOAD_KEY_INFORMATION_V2;

typedef struct _REG_QUERY_KEY_NAME {
  PVOID Object;
  POBJECT_NAME_INFORMATION ObjectNameInfo;
  ULONG Length;
  PULONG ReturnLength;
  PVOID CallContext;
  PVOID ObjectContext;
  PVOID Reserved;
} REG_QUERY_KEY_NAME, *PREG_QUERY_KEY_NAME;

typedef struct _REG_SAVE_MERGED_KEY_INFORMATION {
  PVOID Object;
  HANDLE FileHandle;
  PVOID HighKeyObject;
  PVOID LowKeyObject;
  PVOID CallContext;
  PVOID ObjectContext;
  PVOID Reserved;
} REG_SAVE_MERGED_KEY_INFORMATION, *PREG_SAVE_MERGED_KEY_INFORMATION;

typedef struct _RESUME_PERFORMANCE {
  ULONG PostTimeMs;
  ULONGLONG TotalResumeTimeMs;
  ULONGLONG ResumeCompleteTimestamp;
} RESUME_PERFORMANCE, *PRESUME_PERFORMANCE;

typedef struct _SE_ADT_CLAIMS {
  ULONG Length;
  PCLAIMS_BLOB Claims;
} SE_ADT_CLAIMS, *PSE_ADT_CLAIMS;

typedef struct _SE_ADT_PARAMETER_ARRAY_EX {
  ULONG CategoryId;
  ULONG AuditId;
  ULONG Version;
  ULONG ParameterCount;
  ULONG Length;
  USHORT FlatSubCategoryId;
  USHORT Type;
  ULONG Flags;
  SE_ADT_PARAMETER_ARRAY_ENTRY Parameters[32];
} SE_ADT_PARAMETER_ARRAY_EX, *PSE_ADT_PARAMETER_ARRAY_EX;

typedef struct _SYSTEM_POOL_ZEROING_INFORMATION {
  BOOLEAN PoolZeroingSupportPresent;
} SYSTEM_POOL_ZEROING_INFORMATION, *PSYSTEM_POOL_ZEROING_INFORMATION;

typedef struct _XSAVE_ARM64_SVE_HEADER {
  ULONG VectorLength;
  ULONG VectorRegisterOffset;
  ULONG PredicateRegisterOffset;
  ULONG Reserved[5];
} XSAVE_ARM64_SVE_HEADER, *PXSAVE_ARM64_SVE_HEADER;

typedef struct _XSAVE_CET_U_FORMAT {
  ULONG64 Ia32CetUMsr;
  ULONG64 Ia32Pl3SspMsr;
} XSAVE_CET_U_FORMAT, *PXSAVE_CET_U_FORMAT;

typedef struct PO_EPM_HANDLE__ {
  int unused;
} *PO_EPM_HANDLE;

#define _OB_REFERENCE_TAGS_ 0
#define _RTL_VOL_MEM_ACCESSORS_ 0
#define BitScanForwardSizeT BitScanForward64
#define BitScanReverseSizeT 0
#define CLFS_BASELOG_EXTENSION L".blf"
#define CLFS_CONTAINER_RELATIVE_PREFIX L"%BLF%\\"
#define CLFS_CONTAINER_STREAM_PREFIX 0
#define CLFS_FLAG_FILTER_INTERMEDIATE_LEVEL 0x10
#define CLFS_FLAG_FILTER_TOP_LEVEL 0x20
#define CLFS_LOG_SIZE_MAXIMUM ((ULONGLONG)(-1))
#define CLFS_LOG_SIZE_MINIMUM ((ULONGLONG)0)
#define CLFS_MGMT_NUM_POLICIES ((ULONG)0xA)
#define ClfsClientRecord 3
#define CLFSUSER_API 0
#define DPC_WATCHDOG_GLOBAL_TRIAGE_BLOCK_VER_1_SIZE 0x10
#define DRIVER_PROXY_ENDPOINT_FUNCTION_ID_INVALID 0xFFFFFFFF
#define EA_CONTAINER_NAME 0
#define EA_CONTAINER_SIZE "ContainerSize"
#define EX_CARR_VALID_FLAGS 3
#define EXCEPTION_VIRTUALIZATION_FAULT 0x20
#define FAST_FAIL_ENCLAVE_CALL_FAILURE 0x35
#define FAST_FAIL_GUARD_SS_FAILURE 0x2C
#define FAST_FAIL_INVALID_BUFFER_ACCESS 0x1C
#define FAST_FAIL_INVALID_CONTROL_STACK 0x2F
#define FAST_FAIL_INVALID_NEXT_THREAD 0x1E
#define FAST_FAIL_SET_CONTEXT_DENIED 0x30
#define FILE_ATTRIBUTE_DEDICATED FILE_ATTRIBUTE_TEMPORARY
#define IMAGE_POLICY_ANSI_STRING_PolicyId_, _Value_) { ImagePolicyEntryTypeAnsiString, _PolicyId_, _Value_ },
#define IMAGE_POLICY_BOOL_PolicyId_, _Value_) { ImagePolicyEntryTypeBool, _PolicyId_, (const VOID *) _Value_ },
#define IMAGE_POLICY_INT16_PolicyId_, _Value_) { ImagePolicyEntryTypeInt16, _PolicyId_, (const VOID *) _Value_ },
#define IMAGE_POLICY_INT32_PolicyId_, _Value_) { ImagePolicyEntryTypeInt32, _PolicyId_, (const VOID *) _Value_ },
#define IMAGE_POLICY_INT64_PolicyId_, _Value_) { ImagePolicyEntryTypeInt64, _PolicyId_, (const VOID *) _Value_ },
#define IMAGE_POLICY_INT8_PolicyId_, _Value_) { ImagePolicyEntryTypeInt8, _PolicyId_, (const VOID *) _Value_ },
#define IMAGE_POLICY_OVERRIDE_PolicyId_) { ImagePolicyEntryTypeOverride, _PolicyId_, 0 },
#define IMAGE_POLICY_SECTION_NAME 0
#define IMAGE_POLICY_UINT16_PolicyId_, _Value_) { ImagePolicyEntryTypeUInt16, _PolicyId_, (const VOID *) _Value_ },
#define IMAGE_POLICY_UINT32_PolicyId_, _Value_) { ImagePolicyEntryTypeUInt32, _PolicyId_, (const VOID *) _Value_ },
#define IMAGE_POLICY_UINT64_PolicyId_, _Value_) { ImagePolicyEntryTypeUInt64, _PolicyId_, (const VOID *) _Value_ },
#define IMAGE_POLICY_UINT8_PolicyId_, _Value_) { ImagePolicyEntryTypeUInt8, _PolicyId_, (const VOID *) _Value_ },
#define IMAGE_POLICY_UNICODE_STRING_PolicyId_, _Value_) { ImagePolicyEntryTypeUnicodeString, _PolicyId_, _Value_ },
#define Iosb64ToIosb_iosb, _iosb64) { (_iosb) = (_iosb64); \
}
#define IosbToIosb64_iosb64, _iosb) { (_iosb64) = (_iosb); \
}
#define IS_VALIDATION_ENABLEDC, L) ((L) & (C))
#define KB_SECONDARY_DATA_FLAG_ADDITIONAL_DATA 0x00000001
#ifdef _WIN64
#define LOCK_QUEUE_VALID_FLAGS 7
#else
#define LOCK_QUEUE_VALID_FLAGS 3
#endif
#define MEM_DEDICATED_ATTRIBUTE_NOT_SPECIFIED ((ULONG64)(-1))
#ifdef _WIN64
#define MEMORY_CURRENT_PARTITION_HANDLE (-1)
#else
#define MEMORY_CURRENT_PARTITION_HANDLE 0xFFFFFFFF
#endif
#ifdef _WIN64
#define MEMORY_EXISTING_VAD_PARTITION_HANDLE (-3)
#else
#define MEMORY_EXISTING_VAD_PARTITION_HANDLE 0xFFFFFFFD
#endif
#define MEMORY_PARTITION_ALL_ACCESS 0x1F0003
#ifdef _WIN64
#define MEMORY_SYSTEM_PARTITION_HANDLE (-2)
#else
#define MEMORY_SYSTEM_PARTITION_HANDLE 0xFFFFFFFE
#endif
#define MM_BAD_POINTER 0
#define MM_MAPPING_ADDRESS_DIVISIBLE 0x1
#define MM_PROTECT_DRIVER_SECTION_VALID_FLAGS 1
#define NPEM_CONTROL_INTERFACE_CURRENT_VERSION 2
#define NtCurrentSession) ((HANDLE) (LONG_PTR) - 3)
#define PAGE_ENCLAVE_DECOMMIT 0x10000000
#define PAGE_ENCLAVE_SS_FIRST 0x10000001
#define PAGE_ENCLAVE_SS_REST 0x10000002
#ifdef _WIN64
#define PCI_ATS_INTERFACE_SIZE 0x30
#else
#define PCI_ATS_INTERFACE_SIZE 0x18
#endif
#define PO_EFFECTIVE_POWER_MODE_VMAX 2
#define PO_FX_DEVICE_FLAG_DISABLE_FAST_RESUME 8
#define POOL_CREATE_FLG_POOL_TYPES 0xD
#define POOL_FLAG_UNUSED_REQUIRED_BITS 0xFFFFF000
#define ReadCR8) __readcr8()
#define REFTAG_AFDCONN 'CdfA'
#define REFTAG_AFDENDPOINT 0x45646641
#define REFTAG_AFDPOLL 0x50646641
#define REFTAG_ALEIO 0x49656C41
#define REFTAG_ALEPROCTBL 0x50656C41
#define REFTAG_ALESIDTOKEN 0x53656C41
#define REFTAG_CFSFILTER 0x46736643
#define REFTAG_HTTP 0x70747448
#define REFTAG_MAILSLOT 0x7346734D
#define REFTAG_NFSVOLUME 0x5673664E
#define REFTAG_PGMFILE 0x546D6750
#define REFTAG_PSLOOKUP 0x554C7350
#define REFTAG_PSNOTIFICATION 0x6F4E7350
#define REFTAG_PSWAKE 0x6B577350
#define REFTAG_RAWENDPOINT 0x45776152
#define REFTAG_SUBJECTCONTEXT 0x75536553
#define REFTAG_TCPENDPOINT 0x45706354
#define REFTAG_TCPLISTENER 0x4C706354
#define REFTAG_TCPTCB 0x54706354
#define REFTAG_UDPENDPOINT 0x45706455
#define REFTAG_VIDEO_PORT 0x50646956
#define REFTAG_VIDEO_PORT_I386 0x69646956
#define REFTAG_WS2IFSL 0x69327357
#define REFTAG_WSKNAMERES 0x4E6B7357
#define REFTAG_WSKPROV 0x506B7357
#define REFTAG_WSKTDI 0x546B7357
#define REG_APP_HIVE_OPEN_READ_ONLY 0x2000
#define REG_UNLOAD_LEGAL_FLAGS 1
#define RTL_QUERY_REGISTRY_TYPECHECK_MASK (-16777216)
#define RtlCompareDeviceMemory RtlCompareMemory
#define RtlEqualDeviceMemory RtlEqualMemory
#define SERVICE_USER_OWN_PROCESS 0x50
#define SERVICE_USER_SHARE_PROCESS 0x60
#define SYMBOLIC_LINK_ALL_ACCESS_EX 0xFFFFF
#define TIMER_TOLERABLE_DELAY_BITS 6
#define VRL_CLASS_CONSISTENCY 1
#define VRL_CUSTOM_CLASS_BEGIN 0x100
#define VRL_ENABLE_KERNEL_BREAKS (-2147483647 - 1)
#define VRL_GLOBAL_LEVEL 0
#define VRL_PREDEFINED_CLASS_BEGIN (1 << 0)
#define WriteCR8Data) __writecr8(Data)
#define ZwCurrentSession) NtCurrentSession()

typedef int __prefast_analysis_mode_flag0;

typedef int __prefast_analysis_mode_flag1;

struct _CLFS_MGMT_POLICY
{
    ULONG Version;
    ULONG LengthInBytes;
    ULONG PolicyFlags;
    CLFS_MGMT_POLICY_TYPE PolicyType;
    union
    {
        struct
        {
            ULONG Containers;
        } MaximumSize;
        struct
        {
            ULONG Containers;
        } MinimumSize;
        struct
        {
            ULONG SizeInBytes;
        } NewContainerSize;
        struct
        {
            ULONG AbsoluteGrowthInContainers;
            ULONG RelativeGrowthPercentage;
        } GrowthRate;
        struct
        {
            ULONG MinimumAvailablePercentage;
            ULONG MinimumAvailableContainers;
        } LogTail;
        struct
        {
            ULONG Percentage;
        } AutoShrink;
        struct
        {
            ULONG Enabled;
        } AutoGrow;
        struct
        {
            USHORT PrefixLengthInBytes;
            WCHAR PrefixString[1];
        } NewContainerPrefix;
        struct
        {
            ULONGLONG NextContainerSuffix;
        } NewContainerSuffix;
        struct
        {
            USHORT ExtensionLengthInBytes;
            WCHAR ExtensionString[1];
        } NewContainerExtension;
    } PolicyParameters;
};

union _DRIVER_PROXY_EXTENSION_CREATION_FLAGS
{
    ULONG AsUlong;
    struct
    {
        ULONG ReservedZero : 32;
    };
};

union _DRIVER_PROXY_FEATURE_FLAGS
{
    ULONG AsUlong;
    struct
    {
        ULONG Enabled : 1;
        ULONG Reserved : 31;
    };
};

struct _FILE_MEMORY_PARTITION_INFORMATION
{
    ULONG_PTR OwnerPartitionHandle;
    union
    {
        struct
        {
            UCHAR NoCrossPartitionAccess;
            UCHAR Spare[3];
        };
        ULONG AllFlags;
    } Flags;
};

struct _IMAGE_POLICY_ENTRY
{
    IMAGE_POLICY_ENTRY_TYPE Type;
    IMAGE_POLICY_ID PolicyId;
    union
    {
        const void *None;
        BOOLEAN BoolValue;
        INT8 Int8Value;
        UINT8 UInt8Value;
        INT16 Int16Value;
        UINT16 UInt16Value;
        INT32 Int32Value;
        UINT32 UInt32Value;
        INT64 Int64Value;
        UINT64 UInt64Value;
        PCSTR AnsiStringValue;
        PCWSTR UnicodeStringValue;
    } u;
};

struct _IO_ATTRIBUTION_INFORMATION
{
    ULONG Version;
    union
    {
        struct
        {
            ULONG MajorCode : 8;
            ULONG IoStart : 1;
            ULONG QueueOnly : 1;
            ULONG IoFailed : 1;
            ULONG VirtualDevice : 1;
            ULONG Spare : 20;
        };
        ULONG AllFlags;
    } Flags;
    ULONG Length;
    ULONGLONG ServiceStartTime;
    ULONGLONG CurrentTime;
};

struct _KERNEL_CET_CONTEXT
{
    ULONG64 Ssp;
    ULONG64 Rip;
    USHORT SegCs;
    union
    {
        USHORT AllFlags;
        struct
        {
            USHORT UseWrss : 1;
            USHORT PopShadowStackOne : 1;
            USHORT Unused : 14;
        };
    };
    USHORT Fill[2];
};

union _NPEM_CAPABILITY_STANDARD
{
    struct
    {
        ULONG Capable : 1;
        ULONG ResetCapable : 1;
        ULONG OkCapable : 1;
        ULONG LocateCapable : 1;
        ULONG FailCapable : 1;
        ULONG RebuildCapable : 1;
        ULONG PFACapable : 1;
        ULONG HotSpareCapable : 1;
        ULONG InACriticalArrayCapable : 1;
        ULONG InAFailedArrayCapable : 1;
        ULONG InvalidDeviceTypeCapable : 1;
        ULONG DisabledCapable : 1;
        ULONG Rsvd : 20;
    };
    ULONG AsULONG;
};

struct _POOL_CREATE_EXTENDED_PARAMETER
{
    POOL_CREATE_EXTENDED_PARAMETER_TYPE Type;
    union
    {
        UNICODE_STRING PoolName;
    };
};

typedef CLS_ARCHIVE_DESCRIPTOR CLFS_ARCHIVE_DESCRIPTOR;

typedef PVOID (*CLFS_BLOCK_ALLOCATION)(ULONG, PVOID);

typedef void (*CLFS_BLOCK_DEALLOCATION)(PVOID, PVOID);

typedef CLS_CONTAINER_INFORMATION CLFS_CONTAINER_INFORMATION;

typedef CLS_IO_STATISTICS CLFS_IO_STATISTICS;

typedef CLS_IO_STATISTICS_HEADER CLFS_IO_STATISTICS_HEADER;

typedef struct _CLFS_MGMT_POLICY CLFS_MGMT_POLICY;

typedef CLS_WRITE_ENTRY CLFS_WRITE_ENTRY;

typedef void DRIVER_PROXY_ENDPOINT_FUNCTION(void);

typedef union _DRIVER_PROXY_EXTENSION_CREATION_FLAGS DRIVER_PROXY_EXTENSION_CREATION_FLAGS;

typedef union _DRIVER_PROXY_FEATURE_FLAGS DRIVER_PROXY_FEATURE_FLAGS;

typedef BOOLEAN DRIVER_PROXY_HOTSWAP_WORKER_ROUTINE(PVOID, NTSTATUS);

typedef NTSTATUS DRIVER_PROXY_REGISTER_CALLBACK(DRIVER_PROXY_REGISTER_CALLBACK_PHASE, PVOID);

typedef NTSTATUS ENABLE_VIRTUALIZATION(PVOID, UINT16, BOOLEAN, BOOLEAN, BOOLEAN);

typedef void NTAPI ETWENABLECALLBACK(LPCGUID, ULONG, UCHAR, ULONGLONG, ULONGLONG, PEVENT_FILTER_DESCRIPTOR, PVOID);

typedef struct _FILE_MEMORY_PARTITION_INFORMATION FILE_MEMORY_PARTITION_INFORMATION;

typedef void FPGA_BUS_SCAN(PVOID);

typedef NTSTATUS FPGA_CONTROL_CONFIG_SPACE(PVOID, BOOLEAN);

typedef NTSTATUS FPGA_CONTROL_ERROR_REPORTING(PVOID, ULONG, ULONG, BOOLEAN);

typedef NTSTATUS FPGA_CONTROL_LINK(PVOID, BOOLEAN);

typedef ULONGLONG GET_SDEV_IDENTIFIER(PVOID);

typedef NTSTATUS GET_UPDATED_BUS_RESOURCE(PVOID, PCM_RESOURCE_LIST *, PCM_RESOURCE_LIST *);

typedef ULONG GET_VIRTUAL_DEVICE_DATA(PVOID, USHORT, PVOID, ULONG, ULONG);

typedef NTSTATUS GET_VIRTUAL_DEVICE_LOCATION(PVOID, USHORT, PUINT16, PUINT8, PUINT8);

typedef void GET_VIRTUAL_DEVICE_RESOURCES(PVOID, PUINT8);

typedef NTSTATUS GET_VIRTUAL_FUNCTION_PROBED_BARS(PVOID, PULONG);

typedef struct _IMAGE_POLICY_ENTRY IMAGE_POLICY_ENTRY;

struct _IMAGE_POLICY_METADATA
{
    UCHAR Version;
    UCHAR Reserved0[7];
    ULONGLONG ApplicationId;
    IMAGE_POLICY_ENTRY Policies[];
};

typedef struct _IMAGE_POLICY_METADATA IMAGE_POLICY_METADATA;

typedef struct _IO_ATTRIBUTION_INFORMATION IO_ATTRIBUTION_INFORMATION;

typedef NTSTATUS IO_PERSISTED_MEMORY_ENUMERATION_CALLBACK(PDRIVER_OBJECT, PDEVICE_OBJECT, PUNICODE_STRING, PUSHORT, PULONG, PVOID);

typedef IO_STATUS_BLOCK IO_STATUS_BLOCK64;

typedef struct _KERNEL_CET_CONTEXT KERNEL_CET_CONTEXT;

typedef void MM_MDL_ROUTINE(PVOID, PVOID);

typedef union _NPEM_CAPABILITY_STANDARD NPEM_CAPABILITY_STANDARD;

typedef NTSTATUS NPEM_CONTROL_ENABLE_DISABLE(PVOID, BOOLEAN);

typedef ULONG NPEM_CONTROL_QUERY_CONTROL(PVOID);

typedef NTSTATUS NPEM_CONTROL_SET_STANDARD_CONTROL(PVOID, NPEM_CONTROL_STANDARD_CONTROL_BIT, BOOLEAN);

typedef void NTFS_DEREF_EXPORTED_SECURITY_DESCRIPTOR(PVOID, PSECURITY_DESCRIPTOR);

typedef NTSTATUS PCI_SET_ACS(PVOID, PCI_ACS_BIT, PCI_ACS_BIT, PCI_ACS_BIT, PCI_ACS_BIT, PCI_ACS_BIT, PCI_ACS_BIT, PCI_ACS_BIT);

typedef NTSTATUS PCI_SET_ACS2(PVOID, ULONG, ULONG);

typedef NTSTATUS PCI_SET_ATS(PVOID, BOOLEAN);

typedef const IMAGE_POLICY_ENTRY *PCIMAGE_POLICY_ENTRY;

typedef const IMAGE_POLICY_METADATA *PCIMAGE_POLICY_METADATA;

typedef CLFS_ARCHIVE_DESCRIPTOR *PCLFS_ARCHIVE_DESCRIPTOR;

typedef void (*PCLFS_CLIENT_LFF_HANDLER_COMPLETE_CALLBACK)(PLOG_FILE_OBJECT, NTSTATUS, BOOLEAN, PVOID);

typedef void (*PCLFS_CLIENT_LOG_UNPINNED_CALLBACK)(PLOG_FILE_OBJECT, PVOID);

typedef CLFS_CONTAINER_INFORMATION *PCLFS_CONTAINER_INFORMATION;

typedef CLFS_IO_STATISTICS *PCLFS_IO_STATISTICS;

typedef CLFS_IO_STATISTICS_HEADER *PCLFS_IO_STATISTICS_HEADER;

typedef struct _CLFS_MGMT_POLICY *PCLFS_MGMT_POLICY;

typedef void (*PCLFS_SET_LOG_SIZE_COMPLETE_CALLBACK)(PLOG_FILE_OBJECT, NTSTATUS, PVOID);

typedef CLFS_WRITE_ENTRY *PCLFS_WRITE_ENTRY;

typedef NTSTATUS (*PCRASHDUMP_POWER_ON)(PVOID);

struct _CRASHDUMP_FUNCTIONS_INTERFACE
{
    USHORT Size;
    USHORT Version;
    PVOID Context;
    PINTERFACE_REFERENCE InterfaceReference;
    PINTERFACE_DEREFERENCE InterfaceDereference;
    PCRASHDUMP_POWER_ON PowerOn;
};

typedef struct _CRASHDUMP_FUNCTIONS_INTERFACE CRASHDUMP_FUNCTIONS_INTERFACE;

typedef struct _CRASHDUMP_FUNCTIONS_INTERFACE *PCRASHDUMP_FUNCTIONS_INTERFACE;

typedef DRIVER_PROXY_ENDPOINT_FUNCTION *PDRIVER_PROXY_ENDPOINT_FUNCTION;

struct _DRIVER_PROXY_ENDPOINT_INFORMATION
{
    DRIVER_PROXY_ENDPOINT_FUNCTION_ID Id;
    PDRIVER_PROXY_ENDPOINT_FUNCTION EndpointFunction;
    ULONG ParameterCount;
};

typedef struct _DRIVER_PROXY_ENDPOINT_INFORMATION DRIVER_PROXY_ENDPOINT_INFORMATION;

typedef struct _DRIVER_PROXY_ENDPOINT_INFORMATION *PDRIVER_PROXY_ENDPOINT_INFORMATION;

typedef union _DRIVER_PROXY_EXTENSION_CREATION_FLAGS *PDRIVER_PROXY_EXTENSION_CREATION_FLAGS;

typedef union _DRIVER_PROXY_FEATURE_FLAGS *PDRIVER_PROXY_FEATURE_FLAGS;

typedef DRIVER_PROXY_HOTSWAP_WORKER_ROUTINE *PDRIVER_PROXY_HOTSWAP_WORKER_ROUTINE;

struct _DRIVER_PROXY_HOTSWAP_WORKER_ROUTINE_START_CONTEXT
{
    PDRIVER_PROXY_HOTSWAP_WORKER_ROUTINE WorkerRoutine;
    PVOID Context;
    WAIT_TYPE WaitType;
    KWAIT_REASON WaitReason;
    KPROCESSOR_MODE WaitMode;
    BOOLEAN Altertable;
    BOOLEAN HasTimeout;
    LARGE_INTEGER Timeout;
    ULONG EventCount;
    PKEVENT Events[1];
};

typedef struct _DRIVER_PROXY_HOTSWAP_WORKER_ROUTINE_START_CONTEXT DRIVER_PROXY_HOTSWAP_WORKER_ROUTINE_START_CONTEXT;

typedef struct _DRIVER_PROXY_HOTSWAP_WORKER_ROUTINE_START_CONTEXT *PDRIVER_PROXY_HOTSWAP_WORKER_ROUTINE_START_CONTEXT;

typedef DRIVER_PROXY_REGISTER_CALLBACK *PDRIVER_PROXY_REGISTER_CALLBACK;

typedef DRIVER_PROXY_ENDPOINT_FUNCTION *PDRIVER_PROXY_WRAPPED_ENDPOINT_FUNCTION;

typedef ENABLE_VIRTUALIZATION *PENABLE_VIRTUALIZATION;

typedef struct _FILE_MEMORY_PARTITION_INFORMATION *PFILE_MEMORY_PARTITION_INFORMATION;

typedef FPGA_BUS_SCAN *PFPGA_BUS_SCAN;

typedef FPGA_CONTROL_CONFIG_SPACE *PFPGA_CONTROL_CONFIG_SPACE;

typedef FPGA_CONTROL_ERROR_REPORTING *PFPGA_CONTROL_ERROR_REPORTING;

typedef FPGA_CONTROL_LINK *PFPGA_CONTROL_LINK;

struct _FPGA_CONTROL_INTERFACE
{
    USHORT Size;
    USHORT Version;
    PVOID Context;
    PINTERFACE_REFERENCE InterfaceReference;
    PINTERFACE_DEREFERENCE InterfaceDereference;
    PFPGA_BUS_SCAN BusScan;
    PFPGA_CONTROL_LINK ControlLink;
    PFPGA_CONTROL_CONFIG_SPACE ControlConfigSpace;
    PFPGA_CONTROL_ERROR_REPORTING ControlErrorReporting;
};

typedef struct _FPGA_CONTROL_INTERFACE FPGA_CONTROL_INTERFACE;

typedef struct _FPGA_CONTROL_INTERFACE *PFPGA_CONTROL_INTERFACE;

typedef GET_SDEV_IDENTIFIER *PGET_SDEV_IDENTIFIER;

struct _SDEV_IDENTIFIER_INTERFACE
{
    INTERFACE InterfaceHeader;
    PGET_SDEV_IDENTIFIER GetIdentifier;
};

typedef GET_UPDATED_BUS_RESOURCE *PGET_UPDATED_BUS_RESOURCE;

struct _BUS_RESOURCE_UPDATE_INTERFACE
{
    USHORT Size;
    USHORT Version;
    PVOID Context;
    PINTERFACE_REFERENCE InterfaceReference;
    PINTERFACE_DEREFERENCE InterfaceDereference;
    PGET_UPDATED_BUS_RESOURCE GetUpdatedBusResource;
};

typedef struct _BUS_RESOURCE_UPDATE_INTERFACE BUS_RESOURCE_UPDATE_INTERFACE;

typedef struct _BUS_RESOURCE_UPDATE_INTERFACE *PBUS_RESOURCE_UPDATE_INTERFACE;

typedef GET_VIRTUAL_DEVICE_DATA *PGET_VIRTUAL_DEVICE_DATA;

typedef GET_VIRTUAL_DEVICE_LOCATION *PGET_VIRTUAL_DEVICE_LOCATION;

typedef GET_VIRTUAL_DEVICE_RESOURCES *PGET_VIRTUAL_DEVICE_RESOURCES;

typedef GET_VIRTUAL_FUNCTION_PROBED_BARS *PGET_VIRTUAL_FUNCTION_PROBED_BARS;

typedef struct _IO_ATTRIBUTION_INFORMATION *PIO_ATTRIBUTION_INFORMATION;

typedef IO_PERSISTED_MEMORY_ENUMERATION_CALLBACK *PIO_PERSISTED_MEMORY_ENUMERATION_CALLBACK;

typedef IO_STATUS_BLOCK64 *PIO_STATUS_BLOCK64;

typedef struct _KERNEL_CET_CONTEXT *PKERNEL_CET_CONTEXT;

typedef PVOID (*PMM_GET_SYSTEM_ROUTINE_ADDRESS_EX)(PUNICODE_STRING, PSTR);

typedef MM_MDL_ROUTINE *PMM_MDL_ROUTINE;

typedef union _NPEM_CAPABILITY_STANDARD *PNPEM_CAPABILITY_STANDARD;

typedef NTSTATUS NPEM_CONTROL_QUERY_STANDARD_CAPABILITIES(PVOID, PNPEM_CAPABILITY_STANDARD);

typedef NPEM_CONTROL_ENABLE_DISABLE *PNPEM_CONTROL_ENABLE_DISABLE;

typedef NPEM_CONTROL_QUERY_CONTROL *PNPEM_CONTROL_QUERY_CONTROL;

typedef NPEM_CONTROL_QUERY_STANDARD_CAPABILITIES *PNPEM_CONTROL_QUERY_STANDARD_CAPABILITIES;

typedef NPEM_CONTROL_SET_STANDARD_CONTROL *PNPEM_CONTROL_SET_STANDARD_CONTROL;

struct _NPEM_CONTROL_INTERFACE
{
    USHORT Size;
    USHORT Version;
    PVOID Context;
    PINTERFACE_REFERENCE InterfaceReference;
    PINTERFACE_DEREFERENCE InterfaceDereference;
    PNPEM_CONTROL_ENABLE_DISABLE SetNpemSupportState;
    PNPEM_CONTROL_QUERY_STANDARD_CAPABILITIES QueryStandardCapabilities;
    PNPEM_CONTROL_SET_STANDARD_CONTROL SetStandardControl;
    PNPEM_CONTROL_QUERY_CONTROL QueryNpemControl;
};

typedef struct _NPEM_CONTROL_INTERFACE NPEM_CONTROL_INTERFACE;

typedef struct _NPEM_CONTROL_INTERFACE *PNPEM_CONTROL_INTERFACE;

typedef void PO_EFFECTIVE_POWER_MODE_CALLBACK(PO_EFFECTIVE_POWER_MODE, void *);

typedef void PO_FX_DRIPS_WATCHDOG_CALLBACK(PVOID, PDEVICE_OBJECT, ULONG);

struct POHANDLE__
{
    int unused;
};

typedef struct _POOL_CREATE_EXTENDED_PARAMETER POOL_CREATE_EXTENDED_PARAMETER;

typedef const POOL_CREATE_EXTENDED_PARAMETER *PCPOOL_CREATE_EXTENDED_PARAMETER;

typedef PCI_SET_ACS *PPCI_SET_ACS;

struct _PCI_SECURITY_INTERFACE
{
    USHORT Size;
    USHORT Version;
    PVOID Context;
    PINTERFACE_REFERENCE InterfaceReference;
    PINTERFACE_DEREFERENCE InterfaceDereference;
    PPCI_SET_ACS SetAccessControlServices;
};

typedef struct _PCI_SECURITY_INTERFACE PCI_SECURITY_INTERFACE;

typedef struct _PCI_SECURITY_INTERFACE *PPCI_SECURITY_INTERFACE;

typedef PCI_SET_ACS2 *PPCI_SET_ACS2;

struct _PCI_SECURITY_INTERFACE2
{
    USHORT Size;
    USHORT Version;
    PVOID Context;
    PINTERFACE_REFERENCE InterfaceReference;
    PINTERFACE_DEREFERENCE InterfaceDereference;
    ULONG Flags;
    ULONG SupportedScenarios;
    PPCI_SET_ACS2 SetAccessControlServices;
};

typedef struct _PCI_SECURITY_INTERFACE2 PCI_SECURITY_INTERFACE2;

typedef struct _PCI_SECURITY_INTERFACE2 *PPCI_SECURITY_INTERFACE2;

typedef PCI_SET_ATS *PPCI_SET_ATS;

struct _PCI_ATS_INTERFACE
{
    USHORT Size;
    USHORT Version;
    PVOID Context;
    PINTERFACE_REFERENCE InterfaceReference;
    PINTERFACE_DEREFERENCE InterfaceDereference;
    PPCI_SET_ATS SetAddressTranslationServices;
    UCHAR InvalidateQueueDepth;
};

typedef struct _PCI_ATS_INTERFACE PCI_ATS_INTERFACE;

typedef struct _PCI_ATS_INTERFACE *PPCI_ATS_INTERFACE;

typedef CLFS_ARCHIVE_DESCRIPTOR **PPCLFS_ARCHIVE_DESCRIPTOR;

typedef CLFS_CONTAINER_INFORMATION **PPCLFS_CONTAINER_INFORMATION;

typedef CLFS_IO_STATISTICS **PPCLFS_IO_STATISTICS;

typedef CLFS_IO_STATISTICS_HEADER **PPCLFS_IO_STATISTICS_HEADER;

typedef struct _CLFS_LOG_NAME_INFORMATION **PPCLFS_LOG_NAME_INFORMATION;

typedef struct _CLFS_STREAM_ID_INFORMATION **PPCLFS_STREAM_ID_INFORMATION;

typedef CLFS_WRITE_ENTRY **PPCLFS_WRITE_ENTRY;

typedef struct _CLS_ARCHIVE_DESCRIPTOR **PPCLS_ARCHIVE_DESCRIPTOR;

typedef struct _CLS_CONTAINER_INFORMATION **PPCLS_CONTAINER_INFORMATION;

typedef struct _CLS_IO_STATISTICS **PPCLS_IO_STATISTICS;

typedef struct _CLS_IO_STATISTICS_HEADER **PPCLS_IO_STATISTICS_HEADER;

typedef struct _CLS_WRITE_ENTRY **PPCLS_WRITE_ENTRY;

typedef PO_EFFECTIVE_POWER_MODE_CALLBACK *PPO_EFFECTIVE_POWER_MODE_CALLBACK;

typedef PO_FX_DRIPS_WATCHDOG_CALLBACK *PPO_FX_DRIPS_WATCHDOG_CALLBACK;

typedef struct _POOL_CREATE_EXTENDED_PARAMETER *PPOOL_CREATE_EXTENDED_PARAMETER;

struct _POOL_CREATE_EXTENDED_PARAMS
{
    ULONG Version;
    ULONG ParameterCount;
    PPOOL_CREATE_EXTENDED_PARAMETER Parameters;
};

typedef struct _POOL_CREATE_EXTENDED_PARAMS POOL_CREATE_EXTENDED_PARAMS;

typedef const POOL_CREATE_EXTENDED_PARAMS *PCPOOL_CREATE_EXTENDED_PARAMS;

typedef struct _POOL_CREATE_EXTENDED_PARAMS *PPOOL_CREATE_EXTENDED_PARAMS;

typedef void (*PQUERYEXTENDEDADDRESS)(PVOID, PULONG64);

struct _PNP_EXTENDED_ADDRESS_INTERFACE
{
    USHORT Size;
    USHORT Version;
    PVOID Context;
    PINTERFACE_REFERENCE InterfaceReference;
    PINTERFACE_DEREFERENCE InterfaceDereference;
    PQUERYEXTENDEDADDRESS QueryExtendedAddress;
};

typedef struct _PNP_EXTENDED_ADDRESS_INTERFACE PNP_EXTENDED_ADDRESS_INTERFACE;

typedef struct _PNP_EXTENDED_ADDRESS_INTERFACE *PPNP_EXTENDED_ADDRESS_INTERFACE;

typedef struct _SDEV_IDENTIFIER_INTERFACE *PSDEV_IDENTIFIER_INTERFACE;

typedef NTSTATUS PTM_DEVICE_DISABLE(PVOID);

typedef PTM_DEVICE_DISABLE *PPTM_DEVICE_DISABLE;

typedef NTSTATUS PTM_DEVICE_ENABLE(PVOID);

typedef PTM_DEVICE_ENABLE *PPTM_DEVICE_ENABLE;

typedef NTSTATUS PTM_DEVICE_QUERY_GRANULARITY(PVOID, PUCHAR);

typedef PTM_DEVICE_QUERY_GRANULARITY *PPTM_DEVICE_QUERY_GRANULARITY;

typedef NTSTATUS PTM_DEVICE_QUERY_TIME_SOURCE(PVOID, PULONG);

typedef PTM_DEVICE_QUERY_TIME_SOURCE *PPTM_DEVICE_QUERY_TIME_SOURCE;

struct _PTM_CONTROL_INTERFACE
{
    USHORT Size;
    USHORT Version;
    PVOID Context;
    PINTERFACE_REFERENCE InterfaceReference;
    PINTERFACE_DEREFERENCE InterfaceDereference;
    PPTM_DEVICE_QUERY_GRANULARITY QueryGranularity;
    PPTM_DEVICE_QUERY_TIME_SOURCE QueryTimeSource;
    PPTM_DEVICE_ENABLE Enable;
    PPTM_DEVICE_DISABLE Disable;
};

typedef struct _PTM_CONTROL_INTERFACE *PPTM_CONTROL_INTERFACE;

typedef struct _PTM_CONTROL_INTERFACE PTM_CONTROL_INTERFACE;

typedef NTSTATUS (NTAPI *PTM_PROPAGATE_ROUTINE)(PVOID, PVOID, NTSTATUS, GUID);

typedef struct _SDEV_IDENTIFIER_INTERFACE SDEV_IDENTIFIER_INTERFACE;

typedef void SE_IMAGE_VERIFICATION_CALLBACK_FUNCTION(PVOID, SE_IMAGE_TYPE, PBDCB_IMAGE_INFORMATION);

typedef SE_IMAGE_VERIFICATION_CALLBACK_FUNCTION *PSE_IMAGE_VERIFICATION_CALLBACK_FUNCTION;

typedef void SECURE_DRIVER_PROCESS_DEREFERENCE(PVOID, PEPROCESS);

typedef SECURE_DRIVER_PROCESS_DEREFERENCE *PSECURE_DRIVER_PROCESS_DEREFERENCE;

typedef PEPROCESS SECURE_DRIVER_PROCESS_REFERENCE(PVOID);

typedef SECURE_DRIVER_PROCESS_REFERENCE *PSECURE_DRIVER_PROCESS_REFERENCE;

struct _SECURE_DRIVER_INTERFACE
{
    INTERFACE InterfaceHeader;
    PSECURE_DRIVER_PROCESS_REFERENCE ProcessReference;
    PSECURE_DRIVER_PROCESS_DEREFERENCE ProcessDereference;
    ULONG Reserved;
};

typedef struct _SECURE_DRIVER_INTERFACE *PSECURE_DRIVER_INTERFACE;

typedef struct _SECURE_DRIVER_INTERFACE SECURE_DRIVER_INTERFACE;

typedef ULONG SET_VIRTUAL_DEVICE_DATA(PVOID, USHORT, PVOID, ULONG, ULONG);

typedef SET_VIRTUAL_DEVICE_DATA *PSET_VIRTUAL_DEVICE_DATA;

struct _PCI_VIRTUALIZATION_INTERFACE
{
    USHORT Size;
    USHORT Version;
    PVOID Context;
    PINTERFACE_REFERENCE InterfaceReference;
    PINTERFACE_DEREFERENCE InterfaceDereference;
    PSET_VIRTUAL_DEVICE_DATA SetVirtualFunctionData;
    PGET_VIRTUAL_DEVICE_DATA GetVirtualFunctionData;
    PGET_VIRTUAL_DEVICE_LOCATION GetLocation;
    PGET_VIRTUAL_DEVICE_RESOURCES GetResources;
    PENABLE_VIRTUALIZATION EnableVirtualization;
    PGET_VIRTUAL_FUNCTION_PROBED_BARS GetVirtualFunctionProbedBars;
};

typedef struct _PCI_VIRTUALIZATION_INTERFACE PCI_VIRTUALIZATION_INTERFACE;

typedef struct _PCI_VIRTUALIZATION_INTERFACE *PPCI_VIRTUALIZATION_INTERFACE;

#ifdef __cplusplus
extern "C" {
#endif

NTSTATUS NTAPI RtlGenerateClass5Guid(const GUID *const NamespaceGuid, PVOID Buffer, ULONG BufferSize, GUID *Guid);

/* TODO: LiberNT does not implement the functions below yet */
void BugCheckSecondaryMultiPartDumpDataCallback(KBUGCHECK_CALLBACK_REASON, struct _KBUGCHECK_REASON_CALLBACK_RECORD *, PVOID, ULONG);
NTSTATUS NTAPI ClfsAddLogContainer(PLOG_FILE_OBJECT plfoLog, PULONGLONG pcbContainer, PUNICODE_STRING puszContainerPath);
NTSTATUS NTAPI ClfsAddLogContainerSet(PLOG_FILE_OBJECT plfoLog, USHORT cContainers, PULONGLONG pcbContainer, PUNICODE_STRING rguszContainerPath);
NTSTATUS NTAPI ClfsAlignReservedLog(PVOID pvMarshalContext, ULONG cRecords, LONGLONG rgcbReservation[], PLONGLONG pcbAlignReservation);
NTSTATUS NTAPI ClfsAllocReservedLog(PVOID pvMarshalContext, ULONG cRecords, PLONGLONG pcbAdjustment);
NTSTATUS NTAPI ClfsCloseAndResetLogFile(PLOG_FILE_OBJECT plfoLog);
NTSTATUS NTAPI ClfsCloseLogFileObject(PLOG_FILE_OBJECT plfoLog);
NTSTATUS NTAPI ClfsCreateLogFile(PPLOG_FILE_OBJECT pplfoLog, PUNICODE_STRING puszLogFileName, ACCESS_MASK fDesiredAccess, ULONG dwShareMode, PSECURITY_DESCRIPTOR psdLogFile, ULONG fCreateDisposition, ULONG fCreateOptions, ULONG fFlagsAndAttributes, ULONG fLogOptionFlag, PVOID pvContext, ULONG cbContext);
NTSTATUS NTAPI ClfsCreateMarshallingArea(PLOG_FILE_OBJECT plfoLog, POOL_TYPE ePoolType, PALLOCATE_FUNCTION pfnAllocBuffer, PFREE_FUNCTION pfnFreeBuffer, ULONG cbMarshallingBuffer, ULONG cMaxWriteBuffers, ULONG cMaxReadBuffers, PVOID *ppvMarshalContext);
NTSTATUS NTAPI ClfsCreateMarshallingAreaEx(PLOG_FILE_OBJECT plfoLog, POOL_TYPE ePoolType, PALLOCATE_FUNCTION pfnAllocBuffer, PFREE_FUNCTION pfnFreeBuffer, ULONG cbMarshallingBuffer, ULONG cMaxWriteBuffers, ULONG cMaxReadBuffers, ULONG cAlignmentSize, ULONGLONG fFlags, PVOID *ppvMarshalContext);
NTSTATUS NTAPI ClfsDeleteLogByPointer(PLOG_FILE_OBJECT plfoLog);
NTSTATUS NTAPI ClfsDeleteLogFile(PUNICODE_STRING puszLogFileName, PVOID pvReserved, ULONG fLogOptionFlag, PVOID pvContext, ULONG cbContext);
NTSTATUS NTAPI ClfsDeleteMarshallingArea(PVOID pvMarshalContext);
void NTAPI ClfsFinalize(void);
NTSTATUS NTAPI ClfsFlushBuffers(PVOID pvMarshalContext);
NTSTATUS NTAPI ClfsFreeReservedLog(PVOID pvMarshalContext, ULONG cRecords, PLONGLONG pcbAdjustment);
NTSTATUS NTAPI ClfsGetContainerName(PLOG_FILE_OBJECT plfoLog, CLFS_CONTAINER_ID cidLogicalContainer, PUNICODE_STRING puszContainerName, PULONG pcActualLenContainerName);
NTSTATUS NTAPI ClfsGetIoStatistics(PLOG_FILE_OBJECT plfoLog, PVOID pvStatsBuffer, ULONG cbStatsBuffer, CLFS_IOSTATS_CLASS eStatsClass, PULONG pcbStatsWritten);
NTSTATUS NTAPI ClfsInitialize(void);
NTSTATUS NTAPI ClfsMgmtDeregisterManagedClient(CLFS_MGMT_CLIENT ClientCookie);
NTSTATUS NTAPI ClfsMgmtHandleLogFileFull(CLFS_MGMT_CLIENT Client);
NTSTATUS NTAPI ClfsMgmtInstallPolicy(PLOG_FILE_OBJECT LogFile, PCLFS_MGMT_POLICY Policy, ULONG PolicyLength);
NTSTATUS NTAPI ClfsMgmtQueryPolicy(PLOG_FILE_OBJECT LogFile, CLFS_MGMT_POLICY_TYPE PolicyType, PCLFS_MGMT_POLICY Policy, PULONG PolicyLength);
NTSTATUS NTAPI ClfsMgmtRemovePolicy(PLOG_FILE_OBJECT LogFile, CLFS_MGMT_POLICY_TYPE PolicyType);
NTSTATUS NTAPI ClfsMgmtSetLogFileSize(PLOG_FILE_OBJECT LogFile, PULONGLONG NewSizeInContainers, PULONGLONG ResultingSizeInContainers, PCLFS_SET_LOG_SIZE_COMPLETE_CALLBACK CompletionRoutine, PVOID CompletionRoutineData);
NTSTATUS NTAPI ClfsMgmtSetLogFileSizeAsClient(PLOG_FILE_OBJECT LogFile, CLFS_MGMT_CLIENT ClientCookie, PULONGLONG NewSizeInContainers, PULONGLONG ResultingSizeInContainers, PCLFS_SET_LOG_SIZE_COMPLETE_CALLBACK CompletionRoutine, PVOID CompletionRoutineData);
NTSTATUS NTAPI ClfsMgmtTailAdvanceFailure(CLFS_MGMT_CLIENT Client, NTSTATUS Reason);
NTSTATUS NTAPI ClfsQueryLogFileInformation(PLOG_FILE_OBJECT plfoLog, CLFS_LOG_INFORMATION_CLASS eInformationClass, PVOID pinfoInputBuffer, ULONG cbinfoInputBuffer, PVOID pinfoBuffer, PULONG pcbInfoBuffer);
NTSTATUS NTAPI ClfsRemoveLogContainer(PLOG_FILE_OBJECT plfoLog, PUNICODE_STRING puszContainerPath, BOOLEAN fForce);
NTSTATUS NTAPI ClfsRemoveLogContainerSet(PLOG_FILE_OBJECT plfoLog, USHORT cContainers, PUNICODE_STRING rgwszContainerPath, BOOLEAN fForce);
NTSTATUS NTAPI ClfsSetLogFileInformation(PLOG_FILE_OBJECT plfoLog, CLFS_LOG_INFORMATION_CLASS eInformationClass, PVOID pinfoBuffer, ULONG cbBuffer);
NTSTATUS NTAPI ClfsTerminateReadLog(PVOID pvCursorContext);
BOOLEAN NTAPI ClfsValidTopLevelContext(PIRP pirpTopLevelContext);
NTSTATUS EtwSetInformation(REGHANDLE RegHandle, EVENT_INFO_CLASS InformationClass, PVOID EventInformation, ULONG InformationLength);
void ExCleanupRundownProtectionCacheAware(PEX_RUNDOWN_REF_CACHE_AWARE RunRefCacheAware);
NTSTATUS ExCreatePool(ULONG Flags, ULONG_PTR Tag, POOL_CREATE_EXTENDED_PARAMS *Params, HANDLE *PoolHandle);
void ExDestroyPool(HANDLE PoolHandle);
NTSTATUS ExEnumerateSystemFirmwareTables(ULONG FirmwareTableProviderSignature, PVOID FirmwareTableBuffer, ULONG BufferLength, PULONG ReturnLength);
NTSTATUS ExInitializeDeviceAts(struct _DEVICE_OBJECT *PhysicalDeviceObject, ULONG Flags);
void ExInitializeNPagedLookasideList_NXPoolOptIn(PNPAGED_LOOKASIDE_LIST Lookaside, PALLOCATE_FUNCTION Allocate, PFREE_FUNCTION Free, ULONG Flags, SIZE_T Size, ULONG Tag, USHORT Depth);
void ExInitializeRundownProtectionCacheAwareEx(PEX_RUNDOWN_REF_CACHE_AWARE RunRefCacheAware, ULONG Flags);
void ExRcuFreePool(PEX_RCU_FREE_POOL_CONTEXT Context, PVOID Ptr);
NTSTATUS ExSecurePoolUpdate(HANDLE SecurePoolHandle, ULONG Tag, PVOID Allocation, ULONG_PTR Cookie, SIZE_T Offset, SIZE_T Size, PVOID Buffer);
LOGICAL ExSecurePoolValidate(HANDLE SecurePoolHandle, ULONG Tag, PVOID Allocation, ULONG_PTR Cookie);
NTSTATUS IoAcquireKsrPersistentMemory(PDRIVER_OBJECT DriverObject, PDEVICE_OBJECT PhysicalDeviceObject, PVOID Buffer, PSIZE_T Size);
NTSTATUS IoAcquireKsrPersistentMemoryEx(PDRIVER_OBJECT DriverObject, PDEVICE_OBJECT PhysicalDeviceObject, PUNICODE_STRING PhysicalDeviceId, PUSHORT DataTag, PULONG DataVersion, PVOID Buffer, PSIZE_T Size);
NTSTATUS IoCheckLinkShareAccess(ACCESS_MASK DesiredAccess, ULONG DesiredShareAccess, PFILE_OBJECT FileObject, PSHARE_ACCESS ShareAccess, PLINK_SHARE_ACCESS LinkShareAccess, ULONG IoShareAccessFlags);
NTSTATUS IoEnumerateKsrPersistentMemoryEx(PDRIVER_OBJECT DriverObject, PDEVICE_OBJECT PhysicalDeviceObject, PUNICODE_STRING PhysicalDeviceId, PIO_PERSISTED_MEMORY_ENUMERATION_CALLBACK Callback, PVOID CallbackContext);
NTSTATUS IoFreeKsrPersistentMemory(PVOID DataHandle);
void IoGetDriverProxyFeatures(PDRIVER_OBJECT DriverObject, PDRIVER_PROXY_FEATURE_FLAGS Flags);
NTSTATUS IoGetIoAttributionHandle(PIRP Irp, PVOID *IoAttributionHandle);
NTSTATUS IoGetKsrPersistentMemoryBuffer(PVOID DataHandle, PVOID *BufferPtr, PSIZE_T BufferSize);
void IoInitializeIrpEx(PIRP Irp, PDEVICE_OBJECT DeviceObject, USHORT PacketSize, CCHAR StackSize);
BOOLEAN IoIsInitiator32bitProcess(PIRP Irp);
NTSTATUS IoMapKsrPersistentMemoryEx(PDRIVER_OBJECT DriverObject, PDEVICE_OBJECT PhysicalDeviceObject, PUNICODE_STRING PhysicalDeviceId, PUSHORT DataTag, PULONG DataVersion, PVOID *DataHandle);
NTSTATUS IoQueryKsrPersistentMemorySize(PDRIVER_OBJECT DriverObject, PDEVICE_OBJECT PhysicalDeviceObject, PSIZE_T BufferSize);
NTSTATUS IoQueryKsrPersistentMemorySizeEx(PDRIVER_OBJECT DriverObject, PDEVICE_OBJECT PhysicalDeviceObject, PUNICODE_STRING PhysicalDeviceId, PUSHORT DataTag, PULONG DataVersion, PSIZE_T BufferSize);
NTSTATUS IoRecordIoAttribution(PVOID OpaqueHandle, IO_ATTRIBUTION_INFORMATION *AttributionInformation);
void IoRemoveLinkShareAccess(PFILE_OBJECT FileObject, PSHARE_ACCESS ShareAccess, PLINK_SHARE_ACCESS LinkShareAccess);
void IoRemoveLinkShareAccessEx(PFILE_OBJECT FileObject, PSHARE_ACCESS ShareAccess, PLINK_SHARE_ACCESS LinkShareAccess, ULONG IoShareAccessFlags);
NTSTATUS IoReserveKsrPersistentMemory(PDRIVER_OBJECT DriverObject, PDEVICE_OBJECT PhysicalDeviceObject, SIZE_T Size, ULONG Flags, PVOID *DataHandle);
NTSTATUS IoReserveKsrPersistentMemoryEx(PDRIVER_OBJECT DriverObject, PDEVICE_OBJECT PhysicalDeviceObject, PUNICODE_STRING PhysicalDeviceId, PUSHORT DataTag, ULONG DataVersion, SIZE_T Size, ULONG Flags, PVOID *DataHandle);
NTSTATUS IoSetIoAttributionIrp(PIRP Irp, PVOID AttributionSource, ULONG Flags);
void IoSetLinkShareAccess(ACCESS_MASK DesiredAccess, ULONG DesiredShareAccess, PFILE_OBJECT FileObject, PSHARE_ACCESS ShareAccess, PLINK_SHARE_ACCESS LinkShareAccess, ULONG IoShareAccessFlags);
BOOLEAN IoTryQueueWorkItem(PIO_WORKITEM IoWorkItem, PIO_WORKITEM_ROUTINE_EX WorkerRoutine, WORK_QUEUE_TYPE QueueType, PVOID Context);
void IoUpdateLinkShareAccess(PFILE_OBJECT FileObject, PSHARE_ACCESS ShareAccess, PLINK_SHARE_ACCESS LinkShareAccess);
void IoUpdateLinkShareAccessEx(PFILE_OBJECT FileObject, PSHARE_ACCESS ShareAccess, PLINK_SHARE_ACCESS LinkShareAccess, ULONG IoShareAccessFlags);
NTSTATUS IoWriteKsrPersistentMemory(PVOID DataHandle, PVOID Buffer, SIZE_T Size);
NTSTATUS KeConvertPerformanceCounterToAuxiliaryCounter(ULONG64 PerformanceCounterValue, PULONG64 AuxiliaryCounterValue, PULONG64 ConversionError);
NTSTATUS KeDeregisterBoundCallback(PVOID Handle);
void KeInitializeTimer2SetParameters(PKT2_SET_PARAMETERS Parameters);
NTSTATUS KeQueryAuxiliaryCounterFrequency(PULONG64 AuxiliaryCounterFrequency);
void KeRcuReadLock(void);
void KeRcuReadUnlock(void);
void KeRcuSynchronize(void);
NTSTATUS MmAddVerifierSpecialThunks(ULONG_PTR EntryRoutine, PVOID ThunkBuffer, ULONG ThunkBufferSize);
PVOID MmAllocateMappingAddressEx(SIZE_T NumberOfBytes, ULONG PoolTag, ULONG Flags);
PMDL MmAllocateNodePagesForMdlEx(PHYSICAL_ADDRESS LowAddress, PHYSICAL_ADDRESS HighAddress, PHYSICAL_ADDRESS SkipBytes, SIZE_T TotalBytes, MEMORY_CACHING_TYPE CacheType, ULONG IdealNode, ULONG Flags);
PMDL MmAllocatePartitionNodePagesForMdlEx(PHYSICAL_ADDRESS LowAddress, PHYSICAL_ADDRESS HighAddress, PHYSICAL_ADDRESS SkipBytes, SIZE_T TotalBytes, MEMORY_CACHING_TYPE CacheType, MM_NODE_NUMBER_ZERO_BASED IdealNode, ULONG Flags, PVOID PartitionObject);
LOGICAL MmAreMdlPagesCached(PMDL MemoryDescriptorList);
void MmFreePagesFromMdlEx(PMDL MemoryDescriptorList, ULONG Flags);
LOGICAL MmIsDriverSuspectForVerifier(struct _DRIVER_OBJECT *DriverObject);
PVOID MmMapLockedPagesSpecifyCache_NXOptIn(PMDL MemoryDescriptorList, KPROCESSOR_MODE AccessMode, MEMORY_CACHING_TYPE CacheType, PVOID RequestedAddress, ULONG BugCheckOnFailure, ULONG Priority);
NTSTATUS MmMapMdl(PMDL MemoryDescriptorList, ULONG Protection, PMM_MDL_ROUTINE DriverRoutine, PVOID DriverContext);
NTSTATUS MmMapMemoryDumpMdlEx(PVOID Va, PFN_NUMBER PageTotal, PMDL MemoryDumpMdl, ULONG Flags);
MM_MDL_PAGE_CONTENTS_STATE MmMdlPageContentsState(PMDL MemoryDescriptorList, MM_MDL_PAGE_CONTENTS_STATE State);
NTSTATUS MmProtectDriverSection(PVOID AddressWithinSection, SIZE_T Size, ULONG Flags);
NTSTATUS MmSetPermanentCacheAttribute(PHYSICAL_ADDRESS StartAddress, LARGE_INTEGER NumberOfBytes, MEMORY_CACHING_TYPE CacheType, ULONG Flags);
NTSTATUS NtManagePartition(HANDLE TargetHandle, HANDLE SourceHandle, PARTITION_INFORMATION_CLASS PartitionInformationClass, PVOID PartitionInformation, ULONG PartitionInformationLength);
NTSTATUS NtOpenRegistryTransaction(PHANDLE TransactionHandle, ACCESS_MASK DesiredAccess, POBJECT_ATTRIBUTES ObjectAttributes);
NTSTATUS NtRollbackRegistryTransaction(HANDLE TransactionHandle, ULONG Flags);
BOOLEAN ObReferenceObjectSafe(PVOID Object);
BOOLEAN ObReferenceObjectSafeWithTag(PVOID Object, ULONG Tag);
void PoFxIssueComponentPerfStateChangeMultiple(POHANDLE Handle, ULONG Flags, ULONG Component, ULONG PerfChangesCount, PO_FX_PERF_STATE_CHANGE PerfChanges[], PVOID Context);
void PoFxNotifySurprisePowerOn(PDEVICE_OBJECT Pdo);
void PoFxRegisterDripsWatchdogCallback(POHANDLE Handle, PPO_FX_DRIPS_WATCHDOG_CALLBACK Callback, BOOLEAN IncludeChildDevices, PDRIVER_OBJECT MatchingDriverObject);
void PoFxSetComponentWake(POHANDLE Handle, ULONG Component, BOOLEAN WakeHint);
NTSTATUS PoFxSetTargetDripsDevicePowerState(POHANDLE Handle, DEVICE_POWER_STATE TargetState);
NTSTATUS PoRegisterForEffectivePowerModeNotifications(ULONG Version, PPO_EFFECTIVE_POWER_MODE_CALLBACK Callback, PVOID Context, PO_EPM_HANDLE *RegistrationHandle, PDEVICE_OBJECT DeviceObject);
void PoSetSystemWakeDevice(PDEVICE_OBJECT DeviceObject);
NTSTATUS PoUnregisterFromEffectivePowerModeNotifications(PO_EPM_HANDLE RegistrationHandle);
ULONG64 PsQueryTotalCycleTimeProcess(PEPROCESS Process, PULONG64 CycleTimeStamp);
int RtlConstantTimeEqualMemory(const void *v1, const void *v2, unsigned long len);
volatile void * __cdecl RtlCopyDeviceMemory(volatile void *Destination, const volatile void *Source, size_t Length);
volatile void * __cdecl RtlCopyVolatileMemory(volatile void *Destination, const volatile void *Source, size_t Length);
void RtlExtractBitMap(PRTL_BITMAP Source, PRTL_BITMAP Destination, ULONG TargetBit, ULONG NumberOfBits);
volatile void * RtlFillDeviceMemory(volatile void *Destination, size_t Length, int Fill);
void RtlFillMemoryNonTemporal(__unaligned void *Destination, SIZE_T Length, const UCHAR Value);
volatile void * RtlFillVolatileMemory(volatile void *Destination, size_t Length, int Fill);
void RtlFreeUTF8String(PUTF8_STRING utf8String);
volatile void * __cdecl RtlMoveVolatileMemory(volatile void *Destination, const volatile void *Source, size_t Length);
ULONG RtlNumberOfClearBitsInRange(PRTL_BITMAP BitMapHeader, ULONG StartingIndex, ULONG Length);
OS_DEPLOYEMENT_STATE_VALUES RtlOsDeploymentState(ULONG Flags);
ULONG RtlQueryValidationRunlevel(PCUNICODE_STRING ComponentName);
void RtlSanitizeUnicodeStringPadding(PUNICODE_STRING String);
volatile void * RtlSecureZeroMemory2(volatile void *Destination, size_t Length);
volatile void * __cdecl RtlSetVolatileMemory(volatile void *Destination, int Fill, size_t Length);
NTSTATUS RtlUnicodeStringToInt64(PCUNICODE_STRING String, ULONG Base, PLONG64 Number, PWSTR *EndPointer);
NTSTATUS RtlxQueryRegistryValues(ULONG RelativeTo, PCWSTR Path, PRTL_QUERY_REGISTRY_TABLE QueryTable, PVOID Context, PVOID Environment);
volatile void * RtlZeroDeviceMemory(volatile void *Destination, size_t Length);
volatile void * RtlZeroVolatileMemory(volatile void *Destination, size_t Length);
NTSTATUS SeEtwWriteKMCveEvent(PCUNICODE_STRING CveId, PCUNICODE_STRING AdditionalDetails);
NTSTATUS SeRegisterImageVerificationCallback(SE_IMAGE_TYPE ImageType, SE_IMAGE_VERIFICATION_CALLBACK_TYPE CallbackType, PSE_IMAGE_VERIFICATION_CALLBACK_FUNCTION CallbackFunction, PVOID CallbackContext, SE_IMAGE_VERIFICATION_CALLBACK_TOKEN Token, PVOID *CallbackHandle);
void SeUnregisterImageVerificationCallback(PVOID CallbackHandle);
NTSTATUS VslCreateSecureSection(PHANDLE Handle, PEPROCESS TargetProcess, PMDL Mdl, ULONG DevicePageProtection, ULONG Attributes);
NTSTATUS VslDeleteSecureSection(HANDLE GlobalHandle);
NTSTATUS ZwCommitRegistryTransaction(HANDLE TransactionHandle, ULONG Flags);
NTSTATUS ZwCopyFileChunk(HANDLE SourceHandle, HANDLE DestHandle, HANDLE Event, PIO_STATUS_BLOCK IoStatusBlock, ULONG Length, PLARGE_INTEGER SourceOffset, PLARGE_INTEGER DestOffset, PULONG SourceKey, PULONG DestKey, ULONG Flags);
NTSTATUS ZwCreateRegistryTransaction(PHANDLE TransactionHandle, ACCESS_MASK DesiredAccess, POBJECT_ATTRIBUTES ObjectAttributes, ULONG CreateOptions);
NTSTATUS ZwMapViewOfSectionEx(HANDLE SectionHandle, HANDLE ProcessHandle, PVOID *BaseAddress, PLARGE_INTEGER SectionOffset, PSIZE_T ViewSize, ULONG AllocationType, ULONG PageProtection, PMEM_EXTENDED_PARAMETER ExtendedParameters, ULONG ExtendedParameterCount);

#ifdef __cplusplus
}
#endif

#define IMAGE_POLICY_ANSI_STRING_PolicyId_, _Value_) { ImagePolicyEntryTypeAnsiString, _PolicyId_, _Value_ },
#define IMAGE_POLICY_BOOL_PolicyId_, _Value_) { ImagePolicyEntryTypeBool, _PolicyId_, (const VOID *) _Value_ },
#define IMAGE_POLICY_INT16_PolicyId_, _Value_) { ImagePolicyEntryTypeInt16, _PolicyId_, (const VOID *) _Value_ },
#define IMAGE_POLICY_INT32_PolicyId_, _Value_) { ImagePolicyEntryTypeInt32, _PolicyId_, (const VOID *) _Value_ },
#define IMAGE_POLICY_INT64_PolicyId_, _Value_) { ImagePolicyEntryTypeInt64, _PolicyId_, (const VOID *) _Value_ },
#define IMAGE_POLICY_INT8_PolicyId_, _Value_) { ImagePolicyEntryTypeInt8, _PolicyId_, (const VOID *) _Value_ },
#define IMAGE_POLICY_METADATA_NAME 0
#define IMAGE_POLICY_OVERRIDE_PolicyId_) { ImagePolicyEntryTypeOverride, _PolicyId_, 0 },
#define IMAGE_POLICY_UINT16_PolicyId_, _Value_) { ImagePolicyEntryTypeUInt16, _PolicyId_, (const VOID *) _Value_ },
#define IMAGE_POLICY_UINT32_PolicyId_, _Value_) { ImagePolicyEntryTypeUInt32, _PolicyId_, (const VOID *) _Value_ },
#define IMAGE_POLICY_UINT64_PolicyId_, _Value_) { ImagePolicyEntryTypeUInt64, _PolicyId_, (const VOID *) _Value_ },
#define IMAGE_POLICY_UINT8_PolicyId_, _Value_) { ImagePolicyEntryTypeUInt8, _PolicyId_, (const VOID *) _Value_ },
#define IMAGE_POLICY_UNICODE_STRING_PolicyId_, _Value_) { ImagePolicyEntryTypeUnicodeString, _PolicyId_, _Value_ },
#define IoGetDriverProxyWrapperFromEndpointDriverProxyExtension, Endpoint, Wrapper) __pragma(warning(push)) __pragma(warning(disable : 28024)) do { IofGetDriverProxyWrapperFromEndpoint((DriverProxyExtension), (PDRIVER_PROXY_ENDPOINT_FUNCTION) (Endpoint), (PDRIVER_PROXY_WRAPPED_ENDPOINT_FUNCTION *) (Wrapper)); } while (0) __pragma(warning(pop))
#define Iosb64ToIosb_iosb, _iosb64) { (_iosb) = (_iosb64); \
}
#define IosbToIosb64_iosb64, _iosb) { (_iosb64) = (_iosb); \
}
#define IS_VALIDATION_ENABLEDC, L) ((L) & (C))
#define WriteCR8Data) __writecr8(Data)

enum _BOUND_CALLBACK_STATUS
{
    BoundExceptionMaximum = 3,
};

typedef enum _BOUND_CALLBACK_STATUS BOUND_CALLBACK_STATUS;

typedef BOUND_CALLBACK_STATUS BOUND_CALLBACK(void);

typedef struct _AFFINITY_TOKEN *PAFFINITY_TOKEN;

typedef BOUND_CALLBACK *PBOUND_CALLBACK;

typedef enum _BOUND_CALLBACK_STATUS *PBOUND_CALLBACK_STATUS;

typedef struct _DRIVER_PROXY_EXTENSION *PDRIVER_PROXY_EXTENSION;

typedef struct _KE_SRCU *PKE_SRCU;

#ifdef __cplusplus
extern "C" {
#endif

/* TODO: LiberNT does not implement the functions below yet */
NTSTATUS IoCreateDriverProxyExtension(PDRIVER_OBJECT OwningDriverObject, DRIVER_PROXY_EXTENSION_CREATION_FLAGS Flags, PDRIVER_PROXY_EXTENSION *DriverProxyExtension);
NTSTATUS IoDriverProxyCreateHotSwappableWorkerThread(PDRIVER_PROXY_EXTENSION DriverProxyExtension, PHANDLE ThreadHandle, ULONG DesiredAccess, POBJECT_ATTRIBUTES ObjectAttributes, HANDLE ProcessHandle, PCLIENT_ID ClientId, PDRIVER_PROXY_HOTSWAP_WORKER_ROUTINE_START_CONTEXT WorkerStartContext);
NTSTATUS IofGetDriverProxyWrapperFromEndpoint(PDRIVER_PROXY_EXTENSION DriverProxyExtension, PDRIVER_PROXY_ENDPOINT_FUNCTION Endpoint, PDRIVER_PROXY_WRAPPED_ENDPOINT_FUNCTION *Wrapper);
PDRIVER_PROXY_WRAPPED_ENDPOINT_FUNCTION IoGetDriverProxyEndpointWrapper(PDRIVER_PROXY_EXTENSION DriverProxyExtension, DRIVER_PROXY_ENDPOINT_FUNCTION_ID FunctionId);
PDRIVER_PROXY_EXTENSION IoGetDriverProxyExtensionFromDriverObject(PDRIVER_OBJECT DriverObject);
NTSTATUS IoRegisterDriverProxyEndpoints(PDRIVER_PROXY_EXTENSION DriverProxyExtension, PDRIVER_PROXY_ENDPOINT_INFORMATION EndpointInfo, ULONG Count, PDRIVER_PROXY_REGISTER_CALLBACK PhasedCallback, PVOID Context);
PVOID KeRegisterBoundCallback(PBOUND_CALLBACK CallbackRoutine);
PKE_SRCU KeSrcuAllocate(ULONG Tag);
void KeSrcuFree(PKE_SRCU Rcu);
void KeSrcuReadLock(PKE_SRCU Rcu, PKE_SRCU_LOCK Lock);
void KeSrcuReadUnlock(PKE_SRCU Rcu, PKE_SRCU_LOCK Lock);
void KeSrcuSynchronize(PKE_SRCU Rcu);
NTSTATUS PsAllocateAffinityToken(PAFFINITY_TOKEN *AffinityToken);
void PsFreeAffinityToken(PAFFINITY_TOKEN AffinityToken);
void PsRevertToUserMultipleGroupAffinityThread(PAFFINITY_TOKEN AffinityToken);
NTSTATUS PsSetSystemMultipleGroupAffinityThread(PGROUP_AFFINITY GroupAffinities, USHORT GroupCount, PAFFINITY_TOKEN AffinityToken);

#ifdef __cplusplus
}
#endif

#define IMAGE_POLICY_ANSI_STRING_PolicyId_, _Value_) { ImagePolicyEntryTypeAnsiString, _PolicyId_, _Value_ },
#define IMAGE_POLICY_BOOL_PolicyId_, _Value_) { ImagePolicyEntryTypeBool, _PolicyId_, (const VOID *) _Value_ },
#define IMAGE_POLICY_INT16_PolicyId_, _Value_) { ImagePolicyEntryTypeInt16, _PolicyId_, (const VOID *) _Value_ },
#define IMAGE_POLICY_INT32_PolicyId_, _Value_) { ImagePolicyEntryTypeInt32, _PolicyId_, (const VOID *) _Value_ },
#define IMAGE_POLICY_INT64_PolicyId_, _Value_) { ImagePolicyEntryTypeInt64, _PolicyId_, (const VOID *) _Value_ },
#define IMAGE_POLICY_INT8_PolicyId_, _Value_) { ImagePolicyEntryTypeInt8, _PolicyId_, (const VOID *) _Value_ },
#define IMAGE_POLICY_OVERRIDE_PolicyId_) { ImagePolicyEntryTypeOverride, _PolicyId_, 0 },
#define IMAGE_POLICY_UINT16_PolicyId_, _Value_) { ImagePolicyEntryTypeUInt16, _PolicyId_, (const VOID *) _Value_ },
#define IMAGE_POLICY_UINT32_PolicyId_, _Value_) { ImagePolicyEntryTypeUInt32, _PolicyId_, (const VOID *) _Value_ },
#define IMAGE_POLICY_UINT64_PolicyId_, _Value_) { ImagePolicyEntryTypeUInt64, _PolicyId_, (const VOID *) _Value_ },
#define IMAGE_POLICY_UINT8_PolicyId_, _Value_) { ImagePolicyEntryTypeUInt8, _PolicyId_, (const VOID *) _Value_ },
#define IMAGE_POLICY_UNICODE_STRING_PolicyId_, _Value_) { ImagePolicyEntryTypeUnicodeString, _PolicyId_, _Value_ },
#define IoGetDriverProxyWrapperFromEndpointDriverProxyExtension, Endpoint, Wrapper) __pragma(warning(push)) __pragma(warning(disable : 28024)) do { IofGetDriverProxyWrapperFromEndpoint((DriverProxyExtension), (PDRIVER_PROXY_ENDPOINT_FUNCTION) (Endpoint), (PDRIVER_PROXY_WRAPPED_ENDPOINT_FUNCTION *) (Wrapper)); } while (0) __pragma(warning(pop))
#define Iosb64ToIosb_iosb, _iosb64) { (_iosb) = (_iosb64); \
}
#define IosbToIosb64_iosb64, _iosb) { (_iosb64) = (_iosb); \
}
#define IS_VALIDATION_ENABLEDC, L) ((L) & (C))
#define WriteCR8Data) __writecr8(Data)

struct _CLS_LSN
{
    ULONGLONG Internal;
};

struct _CLS_SCAN_CONTEXT
{
    CLFS_NODE_ID cidNode;
    PLOG_FILE_OBJECT plfoLog;
    ULONG cIndex;
    ULONG cContainers;
    ULONG cContainersReturned;
    CLFS_SCAN_MODE eScanMode;
    PCLS_CONTAINER_INFORMATION pinfoContainer;
};

typedef struct _CLFS_MGMT_CLIENT_REGISTRATION CLFS_MGMT_CLIENT_REGISTRATION;

typedef struct _CLFS_MGMT_NOTIFICATION CLFS_MGMT_NOTIFICATION;

typedef struct _CLFS_PHYSICAL_LSN_INFORMATION CLFS_PHYSICAL_LSN_INFORMATION;

typedef struct _CLS_INFORMATION CLS_INFORMATION;

typedef CLS_INFORMATION CLFS_INFORMATION;

typedef struct _CLS_LSN CLS_LSN;

struct _CLS_INFORMATION
{
    LONGLONG TotalAvailable;
    LONGLONG CurrentAvailable;
    LONGLONG TotalReservation;
    ULONGLONG BaseFileSize;
    ULONGLONG ContainerSize;
    ULONG TotalContainers;
    ULONG FreeContainers;
    ULONG TotalClients;
    ULONG Attributes;
    ULONG FlushThreshold;
    ULONG SectorSize;
    CLS_LSN MinArchiveTailLsn;
    CLS_LSN BaseLsn;
    CLS_LSN LastFlushedLsn;
    CLS_LSN LastLsn;
    CLS_LSN RestartLsn;
    GUID Identity;
};

typedef CLS_LSN CLFS_LSN;

struct _CLFS_MGMT_NOTIFICATION
{
    CLFS_MGMT_NOTIFICATION_TYPE Notification;
    CLFS_LSN Lsn;
    USHORT LogIsPinned;
};

struct _CLFS_PHYSICAL_LSN_INFORMATION
{
    UCHAR StreamIdentifier;
    CLFS_LSN VirtualLsn;
    CLFS_LSN PhysicalLsn;
};

typedef struct _CLS_SCAN_CONTEXT CLS_SCAN_CONTEXT;

typedef CLS_SCAN_CONTEXT CLFS_SCAN_CONTEXT;

typedef CLFS_INFORMATION *PCLFS_INFORMATION;

typedef CLFS_LSN *PCLFS_LSN;

typedef NTSTATUS (*PCLFS_CLIENT_ADVANCE_TAIL_CALLBACK)(PLOG_FILE_OBJECT, PCLFS_LSN, PVOID);

struct _CLFS_MGMT_CLIENT_REGISTRATION
{
    ULONG Version;
    PCLFS_CLIENT_ADVANCE_TAIL_CALLBACK AdvanceTailCallback;
    PVOID AdvanceTailCallbackData;
    PCLFS_CLIENT_LFF_HANDLER_COMPLETE_CALLBACK LogGrowthCompleteCallback;
    PVOID LogGrowthCompleteCallbackData;
    PCLFS_CLIENT_LOG_UNPINNED_CALLBACK LogUnpinnedCallback;
    PVOID LogUnpinnedCallbackData;
};

typedef struct _CLFS_MGMT_CLIENT_REGISTRATION *PCLFS_MGMT_CLIENT_REGISTRATION;

typedef struct _CLFS_MGMT_NOTIFICATION *PCLFS_MGMT_NOTIFICATION;

typedef struct _CLFS_PHYSICAL_LSN_INFORMATION *PCLFS_PHYSICAL_LSN_INFORMATION;

typedef CLFS_SCAN_CONTEXT *PCLFS_SCAN_CONTEXT;

typedef struct _CLS_INFORMATION *PCLS_INFORMATION;

typedef struct _CLS_LSN *PCLS_LSN;

typedef struct _CLS_SCAN_CONTEXT *PCLS_SCAN_CONTEXT;

typedef CLFS_INFORMATION *PPCLFS_INFORMATION;

typedef CLFS_LSN **PPCLFS_LSN;

typedef CLFS_SCAN_CONTEXT **PPCLFS_SCAN_CONTEXT;

typedef struct _CLS_INFORMATION *PPCLS_INFORMATION;

typedef struct _CLS_LSN **PPCLS_LSN;

typedef struct _CLS_SCAN_CONTEXT **PPCLS_SCAN_CONTEXT;

#ifdef __cplusplus
extern "C" {
#endif

/* TODO: LiberNT does not implement the functions below yet */
NTSTATUS NTAPI ClfsAdvanceLogBase(PVOID pvMarshalContext, PCLFS_LSN plsnBase, ULONG fFlags);
NTSTATUS NTAPI ClfsCreateScanContext(PLOG_FILE_OBJECT plfoLog, ULONG cFromContainer, ULONG cContainers, CLFS_SCAN_MODE eScanMode, PCLFS_SCAN_CONTEXT pcxScan);
CLFS_LSN NTAPI ClfsEarlierLsn(PCLFS_LSN plsn);
NTSTATUS NTAPI ClfsFlushToLsn(PVOID pvMarshalContext, PCLFS_LSN plsnFlush, PCLFS_LSN plsnLastFlushed);
NTSTATUS NTAPI ClfsGetLastLsn(PLOG_FILE_OBJECT plfoLog, PCLFS_LSN plsnLast);
NTSTATUS NTAPI ClfsGetLogFileInformation(PLOG_FILE_OBJECT plfoLog, PCLFS_INFORMATION pinfoBuffer, PULONG pcbInfoBuffer);
CLFS_LSN NTAPI ClfsLaterLsn(PCLFS_LSN plsn);
ULONG NTAPI ClfsLsnBlockOffset(const CLFS_LSN *plsn);
CLFS_CONTAINER_ID NTAPI ClfsLsnContainer(const CLFS_LSN *plsn);
CLFS_LSN NTAPI ClfsLsnCreate(CLFS_CONTAINER_ID cidContainer, ULONG offBlock, ULONG cRecord);
NTSTATUS NTAPI ClfsLsnDifference(PCLFS_LSN plsnStart, PCLFS_LSN plsnFinish, ULONG cbContainer, ULONG cbMaxBlock, PLONGLONG pcbDifference);
BOOLEAN NTAPI ClfsLsnEqual(const CLFS_LSN *plsn1, const CLFS_LSN *plsn2);
BOOLEAN NTAPI ClfsLsnGreater(const CLFS_LSN *plsn1, const CLFS_LSN *plsn2);
CLFS_LSN NTAPI ClfsLsnIncrement(PCLFS_LSN plsn);
BOOLEAN NTAPI ClfsLsnInvalid(const CLFS_LSN *plsn);
BOOLEAN NTAPI ClfsLsnLess(const CLFS_LSN *plsn1, const CLFS_LSN *plsn2);
BOOLEAN NTAPI ClfsLsnNull(const CLFS_LSN *plsn);
ULONG NTAPI ClfsLsnRecordSequence(const CLFS_LSN *plsn);
NTSTATUS NTAPI ClfsMgmtRegisterManagedClient(PLOG_FILE_OBJECT LogFile, PCLFS_MGMT_CLIENT_REGISTRATION RegistrationData, PCLFS_MGMT_CLIENT ClientCookie);
NTSTATUS NTAPI ClfsReadLogRecord(PVOID pvMarshalContext, PCLFS_LSN plsnFirst, CLFS_CONTEXT_MODE peContextMode, PVOID *ppvReadBuffer, PULONG pcbReadBuffer, PCLFS_RECORD_TYPE peRecordType, PCLFS_LSN plsnUndoNext, PCLFS_LSN plsnPrevious, PVOID *ppvReadContext);
NTSTATUS NTAPI ClfsReadNextLogRecord(PVOID pvReadContext, PVOID *ppvBuffer, PULONG pcbBuffer, PCLFS_RECORD_TYPE peRecordType, PCLFS_LSN plsnUser, PCLFS_LSN plsnUndoNext, PCLFS_LSN plsnPrevious, PCLFS_LSN plsnRecord);
NTSTATUS NTAPI ClfsReadPreviousRestartArea(PVOID pvReadContext, PVOID *ppvRestartBuffer, PULONG pcbRestartBuffer, PCLFS_LSN plsnRestart);
NTSTATUS NTAPI ClfsReadRestartArea(PVOID pvMarshalContext, PVOID *ppvRestartBuffer, PULONG pcbRestartBuffer, PCLFS_LSN plsn, PVOID *ppvReadContext);
NTSTATUS NTAPI ClfsReserveAndAppendLog(PVOID pvMarshalContext, PCLFS_WRITE_ENTRY rgWriteEntries, ULONG cWriteEntries, PCLFS_LSN plsnUndoNext, PCLFS_LSN plsnPrevious, ULONG cReserveRecords, PLONGLONG rgcbReservation, ULONG fFlags, PCLFS_LSN plsn);
NTSTATUS NTAPI ClfsReserveAndAppendLogAligned(PVOID pvMarshalContext, PCLFS_WRITE_ENTRY rgWriteEntries, ULONG cWriteEntries, ULONG cbEntryAlignment, PCLFS_LSN plsnUndoNext, PCLFS_LSN plsnPrevious, ULONG cReserveRecords, PLONGLONG rgcbReservation, ULONG fFlags, PCLFS_LSN plsn);
NTSTATUS NTAPI ClfsScanLogContainers(PCLFS_SCAN_CONTEXT pcxScan, CLFS_SCAN_MODE eScanMode);
NTSTATUS NTAPI ClfsSetArchiveTail(PLOG_FILE_OBJECT plfoLog, PCLFS_LSN plsnArchiveTail);
NTSTATUS NTAPI ClfsSetEndOfLog(PLOG_FILE_OBJECT plfoLog, PCLFS_LSN plsnEnd);
NTSTATUS NTAPI ClfsWriteRestartArea(PVOID pvMarshalContext, PVOID pvRestartBuffer, ULONG cbRestartBuffer, PCLFS_LSN plsnBase, ULONG fFlags, PULONG pcbWritten, PCLFS_LSN plsnNext);
NTSTATUS ExGetSystemFirmwareTable(ULONG FirmwareTableProviderSignature, ULONG FirmwareTableID, PVOID FirmwareTableBuffer, ULONG BufferLength, PULONG ReturnLength);

#ifdef __cplusplus
}
#endif

#define IMAGE_POLICY_ANSI_STRING_PolicyId_, _Value_) { ImagePolicyEntryTypeAnsiString, _PolicyId_, _Value_ },
#define IMAGE_POLICY_BOOL_PolicyId_, _Value_) { ImagePolicyEntryTypeBool, _PolicyId_, (const VOID *) _Value_ },
#define IMAGE_POLICY_INT16_PolicyId_, _Value_) { ImagePolicyEntryTypeInt16, _PolicyId_, (const VOID *) _Value_ },
#define IMAGE_POLICY_INT32_PolicyId_, _Value_) { ImagePolicyEntryTypeInt32, _PolicyId_, (const VOID *) _Value_ },
#define IMAGE_POLICY_INT64_PolicyId_, _Value_) { ImagePolicyEntryTypeInt64, _PolicyId_, (const VOID *) _Value_ },
#define IMAGE_POLICY_INT8_PolicyId_, _Value_) { ImagePolicyEntryTypeInt8, _PolicyId_, (const VOID *) _Value_ },
#define IMAGE_POLICY_OVERRIDE_PolicyId_) { ImagePolicyEntryTypeOverride, _PolicyId_, 0 },
#define IMAGE_POLICY_UINT16_PolicyId_, _Value_) { ImagePolicyEntryTypeUInt16, _PolicyId_, (const VOID *) _Value_ },
#define IMAGE_POLICY_UINT32_PolicyId_, _Value_) { ImagePolicyEntryTypeUInt32, _PolicyId_, (const VOID *) _Value_ },
#define IMAGE_POLICY_UINT64_PolicyId_, _Value_) { ImagePolicyEntryTypeUInt64, _PolicyId_, (const VOID *) _Value_ },
#define IMAGE_POLICY_UINT8_PolicyId_, _Value_) { ImagePolicyEntryTypeUInt8, _PolicyId_, (const VOID *) _Value_ },
#define IMAGE_POLICY_UNICODE_STRING_PolicyId_, _Value_) { ImagePolicyEntryTypeUnicodeString, _PolicyId_, _Value_ },
#define IoGetDriverProxyWrapperFromEndpointDriverProxyExtension, Endpoint, Wrapper) __pragma(warning(push)) __pragma(warning(disable : 28024)) do { IofGetDriverProxyWrapperFromEndpoint((DriverProxyExtension), (PDRIVER_PROXY_ENDPOINT_FUNCTION) (Endpoint), (PDRIVER_PROXY_WRAPPED_ENDPOINT_FUNCTION *) (Wrapper)); } while (0) __pragma(warning(pop))
#define Iosb64ToIosb_iosb, _iosb64) { (_iosb) = (_iosb64); \
}
#define IosbToIosb64_iosb64, _iosb) { (_iosb64) = (_iosb); \
}
#define IS_VALIDATION_ENABLEDC, L) ((L) & (C))
#define WriteCR8Data) __writecr8(Data)

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
