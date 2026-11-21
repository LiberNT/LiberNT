/*
 * ntddk.h
 *
 * Windows NT Device Driver Kit
 *
 * This file is part of the ReactOS DDK package.
 *
 * Contributors:
 *   Amine Khaldi
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

#define _NTDDK_

#if !defined(_NTHAL_) && !defined(_NTIFS_)
#define _NTDDK_INCLUDED_
#define _DDK_DRIVER_
#endif

/* Dependencies */

#define NT_INCLUDED
#define _CTYPE_DISABLE_MACROS

#include <wdm.h>
#include <excpt.h>
#include <ntdef.h>
#include <ntstatus.h>
#include <mce.h>
#include <bugcodes.h>
#include <ntiologc.h>

#include <stdarg.h> // FIXME
#include <basetyps.h> // FIXME


#ifdef __cplusplus
extern "C" {
#endif

/* GUID and UUID */
#ifndef _NTLSA_IFS_
#ifndef _NTLSA_AUDIT_
#define _NTLSA_AUDIT_

#ifndef GUID_DEFINED
#include <guiddef.h>
#endif

typedef enum _SE_IMAGE_SIGNATURE_TYPE {
    SeImageSignatureNone = 0,
    SeImageSignatureEmbedded = 1,
    SeImageSignatureCache = 2,
    SeImageSignatureCatalogCached = 3,
    SeImageSignatureCatalogNotCached = 4,
    SeImageSignatureCatalogHint = 5,
    SeImageSignaturePackageCatalog = 6,
    SeImageSignaturePplMitigated = 7
} SE_IMAGE_SIGNATURE_TYPE, *PSE_IMAGE_SIGNATURE_TYPE;

typedef enum _STATE_LOCATION_TYPE {
    LocationTypeRegistry = 0,
    LocationTypeFileSystem = 1,
    LocationTypeMaximum = 2
} STATE_LOCATION_TYPE;

typedef enum _RTL_SYSTEM_GLOBAL_DATA_ID {
    GlobalDataIdUnknown = 0,
    GlobalDataIdRngSeedVersion = 1,
    GlobalDataIdInterruptTime = 2,
    GlobalDataIdTimeZoneBias = 3,
    GlobalDataIdImageNumberLow = 4,
    GlobalDataIdImageNumberHigh = 5,
    GlobalDataIdTimeZoneId = 6,
    GlobalDataIdNtMajorVersion = 7,
    GlobalDataIdNtMinorVersion = 8,
    GlobalDataIdSystemExpirationDate = 9,
    GlobalDataIdKdDebuggerEnabled = 10,
    GlobalDataIdCyclesPerYield = 11,
    GlobalDataIdSafeBootMode = 12,
    GlobalDataIdLastSystemRITEventTickCount = 13,
    GlobalDataIdConsoleSharedDataFlags = 14,
    GlobalDataIdNtSystemRootDrive = 15,
    GlobalDataIdQpcBypassEnabled = 16,
    GlobalDataIdQpcData = 17,
    GlobalDataIdQpcBias = 18
} RTL_SYSTEM_GLOBAL_DATA_ID, *PRTL_SYSTEM_GLOBAL_DATA_ID;

typedef enum _PSCREATETHREADNOTIFYTYPE {
    PsCreateThreadNotifyNonSystem = 0,
    PsCreateThreadNotifySubsystems = 1
} PSCREATETHREADNOTIFYTYPE;

typedef enum _HAL_DMA_CRASH_DUMP_REGISTER_TYPE {
    HalDmaCrashDumpRegisterSet1 = 0,
    HalDmaCrashDumpRegisterSet2 = 1,
    HalDmaCrashDumpRegisterSetMax = 2
} HAL_DMA_CRASH_DUMP_REGISTER_TYPE, *PHAL_DMA_CRASH_DUMP_REGISTER_TYPE;

typedef enum _HAL_PMU_NOTIFICATION_TYPE {
    HalPmuAvailableNotification = 0,
    HalPmuReleaseNotification = 1
} HAL_PMU_NOTIFICATION_TYPE;

typedef enum _PCI_LINK_WIDTH {
    PciLinkWidth_1x = 1,
    PciLinkWidth_2x = 2,
    PciLinkWidth_4x = 4,
    PciLinkWidth_8x = 8,
    PciLinkWidth_16x = 16,
    PciLinkWidthMax = 32
} PCI_LINK_WIDTH, *PPCI_LINK_WIDTH;

typedef enum _PCI_LINK_SPEED {
    PciLinkSpeed2_5Gts = 0,
    PciLinkSpeed5_0Gts = 1,
    PciLinkSpeed8_0Gts = 2,
    PciLinkSpeed16_0Gts = 3,
    PciLinkSpeed32_0Gts = 4,
    PciLinkSpeed64_0Gts = 5,
    PciLinkSpeedMax = 6
} PCI_LINK_SPEED, *PPCI_LINK_SPEED;

typedef enum _WHEA_ERROR_SOURCE_STATE {
    WheaErrSrcStateStopped = 1,
    WheaErrSrcStateStarted = 2,
    WheaErrSrcStateRemoved = 3,
    WheaErrSrcStateRemovePending = 4
} WHEA_ERROR_SOURCE_STATE, *PWHEA_ERROR_SOURCE_STATE;

typedef enum _IPMI_OS_SEL_RECORD_TYPE {
    IpmiOsSelRecordTypeWhea = 0,
    IpmiOsSelRecordTypeOther = 1,
    IpmiOsSelRecordTypeWheaErrorXpfMca = 2,
    IpmiOsSelRecordTypeWheaErrorPci = 3,
    IpmiOsSelRecordTypeWheaErrorNmi = 4,
    IpmiOsSelRecordTypeWheaErrorOther = 5,
    IpmiOsSelRecordTypeRaw = 6,
    IpmiOsSelRecordTypeDriver = 7,
    IpmiOsSelRecordTypeBugcheckRecovery = 8,
    IpmiOsSelRecordTypeBugcheckData = 9,
    IpmiOsSelRecordTypeMax = 10
} IPMI_OS_SEL_RECORD_TYPE, *PIPMI_OS_SEL_RECORD_TYPE;

typedef enum _PAGE_OFFLINE_ERROR_TYPES {
    BitErrorDdr4 = 0,
    RowErrorDdr4 = 1,
    BitErrorDdr5 = 2,
    RowErrorDdr5 = 3
} PAGE_OFFLINE_ERROR_TYPES, *PPAGE_OFFLINE_ERROR_TYPES;

typedef enum _WHEA_MEMORY_DEFINITION {
    WheaMemoryUndefined = 0,
    WheaMemoryFm = 1,
    WheaMemoryNm = 2,
    WheaMemoryHbm = 3,
    WheaMemoryMax = 4
} WHEA_MEMORY_DEFINITION, *PWHEA_MEMORY_DEFINITION;

typedef enum _WHEA_PCIEXPRESS_DEVICE_TYPE {
    WheaPciExpressEndpoint = 0,
    WheaPciExpressLegacyEndpoint = 1,
    WheaPciExpressRootPort = 4,
    WheaPciExpressUpstreamSwitchPort = 5,
    WheaPciExpressDownstreamSwitchPort = 6,
    WheaPciExpressToPciXBridge = 7,
    WheaPciXToExpressBridge = 8,
    WheaPciExpressRootComplexIntegratedEndpoint = 9,
    WheaPciExpressRootComplexEventCollector = 10
} WHEA_PCIEXPRESS_DEVICE_TYPE;

typedef enum _WHEA_CPU_VENDOR {
    WheaCpuVendorOther = 0,
    WheaCpuVendorIntel = 1,
    WheaCpuVendorAmd = 2
} WHEA_CPU_VENDOR, *PWHEA_CPU_VENDOR;

typedef enum _WHEA_RECOVERY_TYPE {
    WheaRecoveryTypeActionRequired = 1,
    WheaRecoveryTypeActionOptional = 2,
    WheaRecoveryTypeMax = 3
} WHEA_RECOVERY_TYPE, *PWHEA_RECOVERY_TYPE;

typedef enum _WHEA_RECOVERY_FAILURE_REASON {
    WheaRecoveryFailureReasonKernelCouldNotMarkMemoryBad = 1,
    WheaRecoveryFailureReasonKernelMarkMemoryBadTimedOut = 2,
    WheaRecoveryFailureReasonNoRecoveryContext = 3,
    WheaRecoveryFailureReasonNotContinuable = 4,
    WheaRecoveryFailureReasonPcc = 5,
    WheaRecoveryFailureReasonOverflow = 6,
    WheaRecoveryFailureReasonNotSupported = 7,
    WheaRecoveryFailureReasonMiscOrAddrNotValid = 8,
    WheaRecoveryFailureReasonInvalidAddressMode = 9,
    WheaRecoveryFailureReasonHighIrql = 10,
    WheaRecoveryFailureReasonInsufficientAltContextWrappers = 11,
    WheaRecoveryFailureReasonInterruptsDisabled = 12,
    WheaRecoveryFailureReasonSwapBusy = 13,
    WheaRecoveryFailureReasonStackOverflow = 14,
    WheaRecoveryFailureReasonUnexpectedFailure = 15,
    WheaRecoveryFailureReasonKernelWillPageFaultBCAtCurrentIrql = 16,
    WheaRecoveryFailureReasonFarNotValid = 17,
    WheaRecoveryFailureReasonMax = 18
} WHEA_RECOVERY_FAILURE_REASON, *PWHEA_RECOVERY_FAILURE_REASON;

typedef enum _WHEA_ARM_RAS_NODE_INTERFACES {
    WheaArmRasNodeInterfaceSystemRegister = 0,
    WheaArmRasNodeInterfaceMmio = 1
} WHEA_ARM_RAS_NODE_INTERFACES, *PWHEA_ARM_RAS_NODE_INTERFACES;

typedef enum _WHEA_PCI_RECOVERY_SIGNAL {
    WheaPciRecoverySignalUnknown = 0,
    WheaPciRecoverySignalAer = 1,
    WheaPciRecoverySignalDpc = 2
} WHEA_PCI_RECOVERY_SIGNAL, *PWHEA_PCI_RECOVERY_SIGNAL;

typedef enum _WHEA_PCI_RECOVERY_STATUS {
    WheaPciREcoveryStatusUnknown = 0,
    WheaPciRecoveryStatusNoError = 1,
    WheaPciRecoveryStatusLinkDisableTimeout = 2,
    WheaPciRecoveryStatusLinkEnableTimeout = 3,
    WheaPciRecoveryStatusRpBusyTimeout = 4,
    WheaPciRecoveryStatusComplexTree = 5,
    WheaPciRecoveryStatusBusNotFound = 6,
    WheaPciRecoveryStatusDeviceNotFound = 7,
    WheaPciRecoveryStatusDdaAerNotRecoverable = 8,
    WheaPciRecoveryStatusFailedRecovery = 9
} WHEA_PCI_RECOVERY_STATUS, *PWHEA_PCI_RECOVERY_STATUS;

typedef enum _WHEA_RAW_DATA_FORMAT {
    WheaRawDataFormatIPFSalRecord = 0,
    WheaRawDataFormatIA32MCA = 1,
    WheaRawDataFormatIntel64MCA = 2,
    WheaRawDataFormatAMD64MCA = 3,
    WheaRawDataFormatMemory = 4,
    WheaRawDataFormatPCIExpress = 5,
    WheaRawDataFormatNMIPort = 6,
    WheaRawDataFormatPCIXBus = 7,
    WheaRawDataFormatPCIXDevice = 8,
    WheaRawDataFormatGeneric = 9,
    WheaRawDataFormatMax = 10
} WHEA_RAW_DATA_FORMAT, *PWHEA_RAW_DATA_FORMAT;

typedef enum _WHEA_EVENT_LOG_ENTRY_TYPE {
    WheaEventLogEntryTypeInformational = 0,
    WheaEventLogEntryTypeWarning = 1,
    WheaEventLogEntryTypeError = 2
} WHEA_EVENT_LOG_ENTRY_TYPE, *PWHEA_EVENT_LOG_ENTRY_TYPE;

typedef enum _WHEA_EVENT_LOG_ENTRY_ID {
    WheaEventLogEntryIdCmcPollingTimeout = (-2147483647),
    WheaEventLogEntryIdWheaInit = (-2147483646),
    WheaEventLogEntryIdCmcSwitchToPolling = (-2147483645),
    WheaEventLogEntryIdDroppedCorrectedError = (-2147483644),
    WheaEventLogEntryIdStartedReportHwError = (-2147483643),
    WheaEventLogEntryIdPFAMemoryOfflined = (-2147483642),
    WheaEventLogEntryIdPFAMemoryRemoveMonitor = (-2147483641),
    WheaEventLogEntryIdPFAMemoryPolicy = (-2147483640),
    WheaEventLogEntryIdPshedInjectError = (-2147483639),
    WheaEventLogEntryIdOscCapabilities = (-2147483638),
    WheaEventLogEntryIdPshedPluginRegister = (-2147483637),
    WheaEventLogEntryIdAddRemoveErrorSource = (-2147483636),
    WheaEventLogEntryIdWorkQueueItem = (-2147483635),
    WheaEventLogEntryIdAttemptErrorRecovery = (-2147483634),
    WheaEventLogEntryIdMcaFoundErrorInBank = (-2147483633),
    WheaEventLogEntryIdMcaStuckErrorCheck = (-2147483632),
    WheaEventLogEntryIdMcaErrorCleared = (-2147483631),
    WheaEventLogEntryIdClearedPoison = (-2147483630),
    WheaEventLogEntryIdProcessEINJ = (-2147483629),
    WheaEventLogEntryIdProcessHEST = (-2147483628),
    WheaEventLogEntryIdCreateGenericRecord = (-2147483627),
    WheaEventLogEntryIdErrorRecord = (-2147483626),
    WheaEventLogEntryIdErrorRecordLimit = (-2147483625),
    WheaEventLogEntryIdAerNotGrantedToOs = (-2147483624),
    WheaEventLogEntryIdErrSrcArrayInvalid = (-2147483623),
    WheaEventLogEntryIdAcpiTimeOut = (-2147483622),
    WheaEventLogCmciRestart = (-2147483621),
    WheaEventLogCmciFinalRestart = (-2147483620),
    WheaEventLogEntryEtwOverFlow = (-2147483619),
    WheaEventLogAzccRootBusSearchErr = (-2147483618),
    WheaEventLogAzccRootBusList = (-2147483617),
    WheaEventLogEntryIdErrSrcInvalid = (-2147483616),
    WheaEventLogEntryIdGenericErrMemMap = (-2147483615),
    WheaEventLogEntryIdPshedCallbackCollision = (-2147483614),
    WheaEventLogEntryIdSELBugCheckProgress = (-2147483613),
    WheaEventLogEntryIdPshedPluginLoad = (-2147483612),
    WheaEventLogEntryIdPshedPluginUnload = (-2147483611),
    WheaEventLogEntryIdPshedPluginSupported = (-2147483610),
    WheaEventLogEntryIdDeviceDriver = (-2147483609),
    WheaEventLogEntryIdCmciImplPresent = (-2147483608),
    WheaEventLogEntryIdCmciInitError = (-2147483607),
    WheaEventLogEntryIdSELBugCheckRecovery = (-2147483606),
    WheaEventLogEntryIdDrvErrSrcInvalid = (-2147483605),
    WheaEventLogEntryIdDrvHandleBusy = (-2147483604),
    WheaEventLogEntryIdWheaHeartbeat = (-2147483603),
    WheaEventLogAzccRootBusPoisonSet = (-2147483602),
    WheaEventLogEntryIdSELBugCheckInfo = (-2147483601),
    WheaEventLogEntryIdErrDimmInfoMismatch = (-2147483600),
    WheaEventLogEntryIdeDpcEnabled = (-2147483599),
    WheaEventLogEntryPageOfflineDone = (-2147483598),
    WheaEventLogEntryPageOfflinePendMax = (-2147483597),
    WheaEventLogEntryIdBadPageLimitReached = (-2147483596),
    WheaEventLogEntrySrarDetail = (-2147483595),
    WheaEventLogEntryEarlyError = (-2147483594),
    WheaEventLogEntryIdPcieOverrideInfo = (-2147483593),
    WheaEventLogEntryIdReadPcieOverridesErr = (-2147483592),
    WheaEventLogEntryIdPcieConfigInfo = (-2147483591),
    WheaEventLogEntryIdPcieSummaryFailed = (-2147483584),
    WheaEventLogEntryIdThrottleRegCorrupt = (-2147483583),
    WheaEventLogEntryIdThrottleAddErrSrcFailed = (-2147483582),
    WheaEventLogEntryIdThrottleRegDataIgnored = (-2147483581),
    WheaEventLogEntryIdEnableKeyNotifFailed = (-2147483580),
    WheaEventLogEntryIdKeyNotificationFailed = (-2147483579),
    WheaEventLogEntryIdPcieRemoveDevice = (-2147483578),
    WheaEventLogEntryIdPcieAddDevice = (-2147483577),
    WheaEventLogEntryIdPcieSpuriousErrSource = (-2147483576),
    WheaEventLogEntryIdMemoryAddDevice = (-2147483575),
    WheaEventLogEntryIdMemoryRemoveDevice = (-2147483574),
    WheaEventLogEntryIdMemorySummaryFailed = (-2147483573),
    WheaEventLogEntryIdPcieDpcError = (-2147483572),
    WheaEventLogEntryIdCpuBusesInitFailed = (-2147483571),
    WheaEventLogEntryIdPshedPluginInitFailed = (-2147483570),
    WheaEventLogEntryIdFailedAddToDefectList = (-2147483569),
    WheaEventLogEntryIdDefectListFull = (-2147483568),
    WheaEventLogEntryIdDefectListUEFIVarFailed = (-2147483567),
    WheaEventLogEntryIdDefectListCorrupt = (-2147483566),
    WheaEventLogEntryIdBadHestNotifyData = (-2147483565),
    WheaEventLogEntryIdRowFailure = (-2147483564),
    WheaEventLogEntryIdSrasTableNotFound = (-2147483563),
    WheaEventLogEntryIdSrasTableError = (-2147483562),
    WheaEventLogEntryIdSrasTableEntries = (-2147483561),
    WheaEventLogEntryIdPFANotifyCallbackAction = (-2147483560),
    WheaEventLogEntryIdSELBugCheckCpusQuiesced = (-2147483559),
    WheaEventLogEntryIdPshedPiCpuid = (-2147483558),
    WheaEventLogEntryIdSrasTableBadData = (-2147483557),
    WheaEventLogEntryIdDriFsStatus = (-2147483556),
    WheaEventLogEntryIdCpusFrozen = (-2147483552),
    WheaEventLogEntryIdCpusFrozenNoCrashDump = (-2147483551),
    WheaEventLogEntryIdRegNotifyPolicyChange = (-2147483550),
    WheaEventLogEntryIdRegError = (-2147483549),
    WheaEventLogEntryIdRowOfflineEvent = (-2147483548),
    WheaEventLogEntryIdBitOfflineEvent = (-2147483547),
    WheaEventLogEntryIdBadGasFields = (-2147483546),
    WheaEventLogEntryIdCrashDumpError = (-2147483545),
    WheaEventLogEntryIdCrashDumpCheckpoint = (-2147483544),
    WheaEventLogEntryIdCrashDumpProgressPercent = (-2147483543),
    WheaEventLogEntryIdPreviousCrashBugCheckProgress = (-2147483542),
    WheaEventLogEntryIdSELBugCheckStackDump = (-2147483541),
    WheaEventLogEntryIdPciePromotedAerErr = (-2147483540),
    WheaEventLogEntryIdPshedPiTraceLog = (-2147221488)
} WHEA_EVENT_LOG_ENTRY_ID, *PWHEA_EVENT_LOG_ENTRY_ID;

typedef enum _WHEAP_PFA_OFFLINE_DECISION_TYPE {
    WheapPfaOfflinePredictiveFailure = 1,
    WheapPfaOfflineUncorrectedError = 2
} WHEAP_PFA_OFFLINE_DECISION_TYPE, *PWHEAP_PFA_OFFLINE_DECISION_TYPE;

typedef enum _WHEA_PFA_REMOVE_TRIGGER {
    WheaPfaRemoveErrorThreshold = 1,
    WheaPfaRemoveTimeout = 2,
    WheaPfaRemoveCapacity = 3
} WHEA_PFA_REMOVE_TRIGGER, *PWHEA_PFA_REMOVE_TRIGGER;

typedef enum _WHEA_PSHED_PLUGIN_ENABLE_NOTIFY_ERRORS {
    PshedPiEnableNotifyErrorCreateNotifyEvent = 1,
    PshedPiEnableNotifyErrorCreateSystemThread = 2,
    PshedPiEnableNotifyErrorMax = 3
} WHEA_PSHED_PLUGIN_ENABLE_NOTIFY_ERRORS, *PWHEA_PSHED_PLUGIN_ENABLE_NOTIFY_ERRORS;

typedef enum _WHEA_BUGCHECK_RECOVERY_LOG_TYPE {
    WheaEventBugCheckRecoveryEntry = 0,
    WheaEventBugCheckRecoveryReturn = 1,
    WheaEventBugCheckRecoveryMax = 2
} WHEA_BUGCHECK_RECOVERY_LOG_TYPE, *PWHEA_BUGCHECK_RECOVERY_LOG_TYPE;

typedef enum _WHEAP_DPC_ERROR_EVENT_TYPE {
    WheapDpcErrNoErr = 0,
    WheapDpcErrBusNotFound = 1,
    WheapDpcErrDpcedSubtree = 2,
    WheapDpcErrDeviceIdBad = 3,
    WheapDpcErrResetFailed = 4,
    WheapDpcErrNoChildren = 5
} WHEAP_DPC_ERROR_EVENT_TYPE, *PWHEAP_DPC_ERROR_EVENT_TYPE;

typedef enum _PSHED_PI_ERR_READING_PCIE_OVERRIDES {
    PshedPiErrReadingPcieOverridesNoErr = 0,
    PshedPiErrReadingPcieOverridesNoMemory = 1,
    PshedPiErrReadingPcieOverridesQueryErr = 2,
    PshedPiErrReadingPcieOverridesBadSize = 3,
    PshedPiErrReadingPcieOverridesBadSignature = 4,
    PshedPiErrReadingPcieOverridesNoCapOffset = 5,
    PshedPiErrReadingPcieOverridesNotBinary = 6
} PSHED_PI_ERR_READING_PCIE_OVERRIDES, *PPSHED_PI_ERR_READING_PCIE_OVERRIDES;

typedef enum _WHEA_THROTTLE_TYPE {
    WheaPcieThrottle = 0,
    WheaMemoryThrottle = 1
} WHEA_THROTTLE_TYPE, *PWHEA_THROTTLE_TYPE;

typedef enum _WHEA_OFFLINE_ERRS {
    WheaOfflineNoError = 0,
    GetMemoryDetailsErr = 1,
    RatFailure = 2,
    RatFailureFirstCol = 3,
    RatFailureLastCol = 4,
    ClosedPage = 5,
    BadPageRange = 6,
    InvalidData = 7,
    NotDdr = 8,
    UnsupportedDdrVersion = 9,
    IncorrectDdrVersion = 10,
    NoMemoryForWrapper = 11
} WHEA_OFFLINE_ERRS, *PWHEA_OFFLINE_ERRS;

typedef enum _WHEA_REGISTRY_ERRORS {
    WheaRegErrNone = 0,
    WheaRegErrFailedToCreateWheaKey = 1,
    WheaRegErrFailedToCreatePolicyKey = 2,
    WheaRegErrFailedToOpenHandle = 3
} WHEA_REGISTRY_ERRORS, *PWHEA_REGISTRY_ERRORS;

typedef enum _WHEA_GAS_ERRORS {
    WheaGasErrNone = 0,
    WheaGasErrUnexpectedAddressSpaceId = 1,
    WheaGasErrInvalidStructFields = 2,
    WheaGasErrInvalidAccessSize = 3
} WHEA_GAS_ERRORS, *PWHEA_GAS_ERRORS;

typedef enum _SOC_SUBSYSTEM_TYPE {
    SOC_SUBSYS_WIRELESS_MODEM = 0,
    SOC_SUBSYS_AUDIO_DSP = 1,
    SOC_SUBSYS_WIRELSS_CONNECTIVITY = 2,
    SOC_SUBSYS_SENSORS = 3,
    SOC_SUBSYS_COMPUTE_DSP = 4,
    SOC_SUBSYS_SECURE_PROC = 5,
    SOC_SUBSYS_VENDOR_DEFINED = 0x10000
} SOC_SUBSYSTEM_TYPE, *PSOC_SUBSYSTEM_TYPE;

#endif /* _NTLSA_AUDIT_ */
#endif /* _NTLSA_IFS_ */

typedef GUID UUID;

/* Forward declarations */
struct _LOADER_PARAMETER_BLOCK;
struct _CREATE_DISK;
struct _DRIVE_LAYOUT_INFORMATION_EX;
struct _SET_PARTITION_INFORMATION_EX;
struct _DISK_GEOMETRY_EX;

/* Structures not exposed to drivers */
typedef struct _BUS_HANDLER *PBUS_HANDLER;
typedef struct _DEVICE_HANDLER_OBJECT *PDEVICE_HANDLER_OBJECT;
#if defined(_NTHAL_INCLUDED_) || defined(__REACTOS__)
typedef struct _KAFFINITY_EX *PKAFFINITY_EX;
#endif
typedef struct _PEB *PPEB;

#ifndef _NTIMAGE_

typedef struct _IMAGE_NT_HEADERS *PIMAGE_NT_HEADERS32;
typedef struct _IMAGE_NT_HEADERS64 *PIMAGE_NT_HEADERS64;

#ifdef _WIN64
typedef PIMAGE_NT_HEADERS64 PIMAGE_NT_HEADERS;
#else
typedef PIMAGE_NT_HEADERS32 PIMAGE_NT_HEADERS;
#endif

#endif /* _NTIMAGE_ */

$define (_NTDDK_)
$include (extypes.h)
$include (cmtypes.h)
$include (iotypes.h)
$include (haltypes.h)
$include (ketypes.h)
$include (kdtypes.h)
$include (mmtypes.h)
$include (pstypes.h)
$include (rtltypes.h)
$include (setypes.h)
$include (wheatypes.h)

#if defined(_M_ARM64EC)
$include(arm64/ke.h)
#elif defined(_M_IX86)
$include(x86/ke.h)
$include(x86/mm.h)
#elif defined(_M_AMD64)
$include(amd64/ke.h)
$include(amd64/mm.h)
#elif defined(_M_IA64)
$include(ia64/ke.h)
#elif defined(_M_PPC)
$include(ppc/ke.h)
$include(ppc/mm.h)
#elif defined(_M_MIPS)
$include(mips/ke.h)
#elif defined(_M_ARM)
$include(arm/ke.h)
$include(arm/mm.h)
#elif defined(_M_ARM64)
$include(arm64/ke.h)
$include(arm64/mm.h)
#elif defined(_M_RISCV64)
$include(riscv64/ke.h)
$include(riscv64/mm.h)
#else
#error Unknown Architecture
#endif

$include (exfuncs.h)
$include (halfuncs.h)
$include (iofuncs.h)
$include (kdfuncs.h)
$include (kefuncs.h)
$include (mmfuncs.h)
$include (psfuncs.h)
$include (rtlfuncs.h)
$include (sefuncs.h)
$include (zwfuncs.h)


/* UNSORTED */

#define VER_SET_CONDITION(ConditionMask, TypeBitMask, ComparisonType) \
  ((ConditionMask) = VerSetConditionMask((ConditionMask),             \
  (TypeBitMask), (ComparisonType)))

#if (NTDDI_VERSION >= NTDDI_WIN2K)
NTSYSAPI
ULONGLONG
NTAPI
VerSetConditionMask(
  IN ULONGLONG ConditionMask,
  IN ULONG TypeMask,
  IN UCHAR Condition);
#endif

typedef struct _KERNEL_USER_TIMES {
  LARGE_INTEGER CreateTime;
  LARGE_INTEGER ExitTime;
  LARGE_INTEGER KernelTime;
  LARGE_INTEGER UserTime;
} KERNEL_USER_TIMES, *PKERNEL_USER_TIMES;

typedef enum _SUBSYSTEM_INFORMATION_TYPE {
  SubsystemInformationTypeWin32 = 0,
  SubsystemInformationTypeWSL = 1,
  MaxSubsystemInformationType
} SUBSYSTEM_INFORMATION_TYPE, *PSUBSYSTEM_INFORMATION_TYPE;

/* NtXxx Functions */

typedef enum _SYSTEM_FIRMWARE_TABLE_ACTION {
  SystemFirmwareTable_Enumerate,
  SystemFirmwareTable_Get
} SYSTEM_FIRMWARE_TABLE_ACTION;

typedef struct _SYSTEM_FIRMWARE_TABLE_INFORMATION {
  ULONG ProviderSignature;
  SYSTEM_FIRMWARE_TABLE_ACTION Action;
  ULONG TableID;
  ULONG TableBufferLength;
  UCHAR TableBuffer[ANYSIZE_ARRAY];
} SYSTEM_FIRMWARE_TABLE_INFORMATION, *PSYSTEM_FIRMWARE_TABLE_INFORMATION;

typedef NTSTATUS
(__cdecl *PFNFTH)(
  _Inout_ PSYSTEM_FIRMWARE_TABLE_INFORMATION SystemFirmwareTableInfo);

typedef struct _SYSTEM_FIRMWARE_TABLE_HANDLER {
  ULONG ProviderSignature;
  BOOLEAN Register;
  PFNFTH FirmwareTableHandler;
  PVOID DriverObject;
} SYSTEM_FIRMWARE_TABLE_HANDLER, *PSYSTEM_FIRMWARE_TABLE_HANDLER;

typedef ULONG_PTR
(NTAPI *PDRIVER_VERIFIER_THUNK_ROUTINE)(
  _In_ PVOID Context);

typedef struct _DRIVER_VERIFIER_THUNK_PAIRS {
  PDRIVER_VERIFIER_THUNK_ROUTINE PristineRoutine;
  PDRIVER_VERIFIER_THUNK_ROUTINE NewRoutine;
} DRIVER_VERIFIER_THUNK_PAIRS, *PDRIVER_VERIFIER_THUNK_PAIRS;

#define DRIVER_VERIFIER_SPECIAL_POOLING             0x0001
#define DRIVER_VERIFIER_FORCE_IRQL_CHECKING         0x0002
#define DRIVER_VERIFIER_INJECT_ALLOCATION_FAILURES  0x0004
#define DRIVER_VERIFIER_TRACK_POOL_ALLOCATIONS      0x0008
#define DRIVER_VERIFIER_IO_CHECKING                 0x0010

#define SHARED_GLOBAL_FLAGS_ERROR_PORT_V        0x0
#define SHARED_GLOBAL_FLAGS_ERROR_PORT          (1UL << SHARED_GLOBAL_FLAGS_ERROR_PORT_V)

#define SHARED_GLOBAL_FLAGS_ELEVATION_ENABLED_V 0x1
#define SHARED_GLOBAL_FLAGS_ELEVATION_ENABLED   (1UL << SHARED_GLOBAL_FLAGS_ELEVATION_ENABLED_V)

#define SHARED_GLOBAL_FLAGS_VIRT_ENABLED_V      0x2
#define SHARED_GLOBAL_FLAGS_VIRT_ENABLED        (1UL << SHARED_GLOBAL_FLAGS_VIRT_ENABLED_V)

#define SHARED_GLOBAL_FLAGS_INSTALLER_DETECT_ENABLED_V  0x3
#define SHARED_GLOBAL_FLAGS_INSTALLER_DETECT_ENABLED    \
  (1UL << SHARED_GLOBAL_FLAGS_INSTALLER_DETECT_ENABLED_V)

#define SHARED_GLOBAL_FLAGS_SPARE_V                     0x4
#define SHARED_GLOBAL_FLAGS_SPARE                       \
  (1UL << SHARED_GLOBAL_FLAGS_SPARE_V)

#define SHARED_GLOBAL_FLAGS_DYNAMIC_PROC_ENABLED_V      0x5
#define SHARED_GLOBAL_FLAGS_DYNAMIC_PROC_ENABLED        \
  (1UL << SHARED_GLOBAL_FLAGS_DYNAMIC_PROC_ENABLED_V)

#define SHARED_GLOBAL_FLAGS_SEH_VALIDATION_ENABLED_V    0x6
#define SHARED_GLOBAL_FLAGS_SEH_VALIDATION_ENABLED        \
  (1UL << SHARED_GLOBAL_FLAGS_SEH_VALIDATION_ENABLED_V)

#define EX_INIT_BITS(Flags, Bit) \
  *((Flags)) |= (Bit)             // Safe to use before concurrently accessible

#define EX_TEST_SET_BIT(Flags, Bit) \
  InterlockedBitTestAndSet ((PLONG)(Flags), (Bit))

#define EX_TEST_CLEAR_BIT(Flags, Bit) \
  InterlockedBitTestAndReset ((PLONG)(Flags), (Bit))

#define PCCARD_MAP_ERROR               0x01
#define PCCARD_DEVICE_PCI              0x10

#define PCCARD_SCAN_DISABLED           0x01
#define PCCARD_MAP_ZERO                0x02
#define PCCARD_NO_TIMER                0x03
#define PCCARD_NO_PIC                  0x04
#define PCCARD_NO_LEGACY_BASE          0x05
#define PCCARD_DUP_LEGACY_BASE         0x06
#define PCCARD_NO_CONTROLLERS          0x07

#define MAXIMUM_EXPANSION_SIZE (KERNEL_LARGE_STACK_SIZE - (PAGE_SIZE / 2))

/* Filesystem runtime library routines */

#if (NTDDI_VERSION >= NTDDI_WIN2K)
_Must_inspect_result_
NTKERNELAPI
BOOLEAN
NTAPI
FsRtlIsTotalDeviceFailure(
  _In_ NTSTATUS Status);
#endif

#if ((_WIN32_WINNT >= _WIN32_WINNT_WIN10_RS1))

#define FILE_DISPOSITION_DO_NOT_DELETE              0x00000000
#define FILE_DISPOSITION_DELETE                     0x00000001
#define FILE_DISPOSITION_POSIX_SEMANTICS            0x00000002
#define FILE_DISPOSITION_FORCE_IMAGE_SECTION_CHECK  0x00000004
#define FILE_DISPOSITION_ON_CLOSE                   0x00000008
#define FILE_DISPOSITION_IGNORE_READONLY_ATTRIBUTE  0x00000010

typedef struct _FILE_DISPOSITION_INFORMATION_EX {
    ULONG Flags;
} FILE_DISPOSITION_INFORMATION_EX, *PFILE_DISPOSITION_INFORMATION_EX;

#endif

#if ((NTDDI_VERSION >= NTDDI_WIN8))

#define BDCB_IMAGEFLAGS_FAILED_CODE_INTEGRITY   (1UL << 0)

typedef enum _BDCB_CALLBACK_TYPE {
    BdCbStatusUpdate,
    BdCbInitializeImage,
} BDCB_CALLBACK_TYPE, *PBDCB_CALLBACK_TYPE;

typedef enum _BDCB_CLASSIFICATION {
    BdCbClassificationUnknownImage,
    BdCbClassificationKnownGoodImage,
    BdCbClassificationKnownBadImage,
    BdCbClassificationKnownBadImageBootCritical,
    BdCbClassificationEnd,
} BDCB_CLASSIFICATION, *PBDCB_CLASSIFICATION;

typedef enum _BDCB_STATUS_UPDATE_TYPE {
    BdCbStatusPrepareForDependencyLoad,
    BdCbStatusPrepareForDriverLoad,
    BdCbStatusPrepareForUnload,
} BDCB_STATUS_UPDATE_TYPE, *PBDCB_STATUS_UPDATE_TYPE;

typedef struct _BDCB_STATUS_UPDATE_CONTEXT {
    BDCB_STATUS_UPDATE_TYPE StatusType;
} BDCB_STATUS_UPDATE_CONTEXT, *PBDCB_STATUS_UPDATE_CONTEXT;

typedef struct _BDCB_IMAGE_INFORMATION {
    BDCB_CLASSIFICATION Classification;
    ULONG ImageFlags;
    UNICODE_STRING ImageName;
    UNICODE_STRING RegistryPath;
    UNICODE_STRING CertificatePublisher;
    UNICODE_STRING CertificateIssuer;
    PVOID ImageHash;
    PVOID CertificateThumbprint;
    ULONG ImageHashAlgorithm;
    ULONG ThumbprintHashAlgorithm;
    ULONG ImageHashLength;
    ULONG CertificateThumbprintLength;
} BDCB_IMAGE_INFORMATION, *PBDCB_IMAGE_INFORMATION;

typedef
VOID
NTAPI
BOOT_DRIVER_CALLBACK_FUNCTION (
    _In_opt_ PVOID CallbackContext,
    _In_ BDCB_CALLBACK_TYPE Classification,
    _Inout_ PBDCB_IMAGE_INFORMATION ImageInformation
    );

typedef BOOT_DRIVER_CALLBACK_FUNCTION *PBOOT_DRIVER_CALLBACK_FUNCTION;

PVOID
NTAPI
IoRegisterBootDriverCallback (
    _In_ PBOOT_DRIVER_CALLBACK_FUNCTION CallbackFunction,
    _In_opt_ PVOID CallbackContext
    );

VOID
NTAPI
IoUnregisterBootDriverCallback (
    _In_ PVOID CallbackHandle
    );

#endif

#ifdef __cplusplus
}
#endif

#ifndef ARBITER_FLAG_OTHER_ENUM
#define ARBITER_FLAG_OTHER_ENUM 0x00000004
#endif
#ifndef ARBITER_FLAG_ROOT_ENUM
#define ARBITER_FLAG_ROOT_ENUM 0x00000002
#endif
#ifndef DBG_DEVICE_FLAG_HOST_VISIBLE_ALLOCATED
#define DBG_DEVICE_FLAG_HOST_VISIBLE_ALLOCATED 0x20
#endif
#ifndef DO_BOOT_CRITICAL
#define DO_BOOT_CRITICAL 0x20000000
#endif
#ifndef DO_DAX_VOLUME
#define DO_DAX_VOLUME 0x10000000
#endif
#ifndef DO_DEVICE_IRP_REQUIRES_EXTENSION
#define DO_DEVICE_IRP_REQUIRES_EXTENSION 0x08000000
#endif
#ifndef DO_DEVICE_TO_BE_RESET
#define DO_DEVICE_TO_BE_RESET 0x04000000
#endif
#ifndef DYNAMIC_EH_CONTINUATION_TARGET_ADD
#define DYNAMIC_EH_CONTINUATION_TARGET_ADD 0x00000001
#endif
#ifndef DYNAMIC_EH_CONTINUATION_TARGET_PROCESSED
#define DYNAMIC_EH_CONTINUATION_TARGET_PROCESSED 0x00000002
#endif
#ifndef DYNAMIC_ENFORCED_ADDRESS_RANGE_ADD
#define DYNAMIC_ENFORCED_ADDRESS_RANGE_ADD 0x00000001
#endif
#ifndef DYNAMIC_ENFORCED_ADDRESS_RANGE_PROCESSED
#define DYNAMIC_ENFORCED_ADDRESS_RANGE_PROCESSED 0x00000002
#endif
#ifndef ERRTYP_BUS
#define ERRTYP_BUS 0x10
#endif
#ifndef ERRTYP_CACHE
#define ERRTYP_CACHE 0x06
#endif
#ifndef ERRTYP_FLOW
#define ERRTYP_FLOW 0x09
#endif
#ifndef ERRTYP_FUNCTION
#define ERRTYP_FUNCTION 0x07
#endif
#ifndef ERRTYP_IMPROPER
#define ERRTYP_IMPROPER 0x12
#endif
#ifndef ERRTYP_INTERNAL
#define ERRTYP_INTERNAL 0x01
#endif
#ifndef ERRTYP_LOSSOFLOCKSTEP
#define ERRTYP_LOSSOFLOCKSTEP 0x14
#endif
#ifndef ERRTYP_MAP
#define ERRTYP_MAP 0x11
#endif
#ifndef ERRTYP_MEM
#define ERRTYP_MEM 0x04
#endif
#ifndef ERRTYP_PARITY
#define ERRTYP_PARITY 0x16
#endif
#ifndef ERRTYP_PATHERROR
#define ERRTYP_PATHERROR 0x18
#endif
#ifndef ERRTYP_POISONED
#define ERRTYP_POISONED 0x1A
#endif
#ifndef ERRTYP_PROTOCOL
#define ERRTYP_PROTOCOL 0x17
#endif
#ifndef ERRTYP_RESPONSE
#define ERRTYP_RESPONSE 0x15
#endif
#ifndef ERRTYP_SELFTEST
#define ERRTYP_SELFTEST 0x08
#endif
#ifndef ERRTYP_TIMEOUT
#define ERRTYP_TIMEOUT 0x19
#endif
#ifndef ERRTYP_TLB
#define ERRTYP_TLB 0x05
#endif
#ifndef ERRTYP_UNIMPL
#define ERRTYP_UNIMPL 0x13
#endif
#ifndef FILE_DEVICE_EVENT_COLLECTOR
#define FILE_DEVICE_EVENT_COLLECTOR 0x0000005f
#endif
#ifndef FILE_DEVICE_FABRIC_NVME
#define FILE_DEVICE_FABRIC_NVME 0x00000062
#endif
#ifndef FILE_DEVICE_HARDWARE_ACCELERATOR
#define FILE_DEVICE_HARDWARE_ACCELERATOR 0x00000064
#endif
#ifndef FILE_DEVICE_I3C
#define FILE_DEVICE_I3C 0x00000065
#endif
#ifndef FILE_DEVICE_PRM
#define FILE_DEVICE_PRM 0x0000005e
#endif
#ifndef FILE_DEVICE_SVM
#define FILE_DEVICE_SVM 0x00000063
#endif
#ifndef FILE_DEVICE_USB4
#define FILE_DEVICE_USB4 0x00000060
#endif
#ifndef FILL_NV_MEMORY_FLAG_FLUSH
#define FILL_NV_MEMORY_FLAG_FLUSH 0x00000001
#endif
#ifndef FILL_NV_MEMORY_FLAG_NON_TEMPORAL
#define FILL_NV_MEMORY_FLAG_NON_TEMPORAL 0x00000002
#endif
#ifndef FILL_NV_MEMORY_FLAG_NO_DRAIN
#define FILL_NV_MEMORY_FLAG_NO_DRAIN 0x00000100
#endif
#ifndef FLUSH_NV_MEMORY_IN_FLAG_NO_DRAIN
#define FLUSH_NV_MEMORY_IN_FLAG_NO_DRAIN 0x00000001
#endif
#ifndef GENPROC_FLAGS_CORRECTED
#define GENPROC_FLAGS_CORRECTED 0x08
#endif
#ifndef GENPROC_FLAGS_OVERFLOW
#define GENPROC_FLAGS_OVERFLOW 0x04
#endif
#ifndef GENPROC_FLAGS_PRECISEIP
#define GENPROC_FLAGS_PRECISEIP 0x02
#endif
#ifndef GENPROC_FLAGS_RESTARTABLE
#define GENPROC_FLAGS_RESTARTABLE 0x01
#endif
#ifndef GENPROC_OP_DATAREAD
#define GENPROC_OP_DATAREAD 1
#endif
#ifndef GENPROC_OP_DATAWRITE
#define GENPROC_OP_DATAWRITE 2
#endif
#ifndef GENPROC_OP_GENERIC
#define GENPROC_OP_GENERIC 0
#endif
#ifndef GENPROC_OP_INSTRUCTIONEXE
#define GENPROC_OP_INSTRUCTIONEXE 3
#endif
#ifndef GENPROC_PROCERRTYPE_BUS
#define GENPROC_PROCERRTYPE_BUS 4
#endif
#ifndef GENPROC_PROCERRTYPE_CACHE
#define GENPROC_PROCERRTYPE_CACHE 1
#endif
#ifndef GENPROC_PROCERRTYPE_MAE
#define GENPROC_PROCERRTYPE_MAE 8
#endif
#ifndef GENPROC_PROCERRTYPE_TLB
#define GENPROC_PROCERRTYPE_TLB 2
#endif
#ifndef GENPROC_PROCERRTYPE_UNKNOWN
#define GENPROC_PROCERRTYPE_UNKNOWN 0
#endif
#ifndef GENPROC_PROCISA_ARM32
#define GENPROC_PROCISA_ARM32 4
#endif
#ifndef GENPROC_PROCISA_ARM64
#define GENPROC_PROCISA_ARM64 8
#endif
#ifndef GENPROC_PROCISA_IPF
#define GENPROC_PROCISA_IPF 1
#endif
#ifndef GENPROC_PROCISA_X64
#define GENPROC_PROCISA_X64 2
#endif
#ifndef GENPROC_PROCISA_X86
#define GENPROC_PROCISA_X86 0
#endif
#ifndef GENPROC_PROCTYPE_ARM
#define GENPROC_PROCTYPE_ARM 2
#endif
#ifndef GENPROC_PROCTYPE_IPF
#define GENPROC_PROCTYPE_IPF 1
#endif
#ifndef GENPROC_PROCTYPE_XPF
#define GENPROC_PROCTYPE_XPF 0
#endif
#ifndef HAL_PMU_NOTIFCATION_CALLBACK_VERSION
#define HAL_PMU_NOTIFCATION_CALLBACK_VERSION 0x01
#endif
#ifndef HAL_REGISTER_PMU_NOTIFICATION_INPUT_VERSION
#define HAL_REGISTER_PMU_NOTIFICATION_INPUT_VERSION 0x01
#endif
#ifndef HAL_UNREGISTER_PMU_NOTIFICATION_INPUT_VERSION
#define HAL_UNREGISTER_PMU_NOTIFICATION_INPUT_VERSION 0x01
#endif
#ifndef INJECT_ERRTYPE_MEMORY_CORRECTABLE
#define INJECT_ERRTYPE_MEMORY_CORRECTABLE 0x00000008
#endif
#ifndef INJECT_ERRTYPE_MEMORY_UNCORRECTABLEFATAL
#define INJECT_ERRTYPE_MEMORY_UNCORRECTABLEFATAL 0x00000020
#endif
#ifndef INJECT_ERRTYPE_MEMORY_UNCORRECTABLENONFATAL
#define INJECT_ERRTYPE_MEMORY_UNCORRECTABLENONFATAL 0x00000010
#endif
#ifndef INJECT_ERRTYPE_PCIEXPRESS_CORRECTABLE
#define INJECT_ERRTYPE_PCIEXPRESS_CORRECTABLE 0x00000040
#endif
#ifndef INJECT_ERRTYPE_PCIEXPRESS_UNCORRECTABLEFATAL
#define INJECT_ERRTYPE_PCIEXPRESS_UNCORRECTABLEFATAL 0x00000100
#endif
#ifndef INJECT_ERRTYPE_PCIEXPRESS_UNCORRECTABLENONFATAL
#define INJECT_ERRTYPE_PCIEXPRESS_UNCORRECTABLENONFATAL 0x00000080
#endif
#ifndef INJECT_ERRTYPE_PLATFORM_CORRECTABLE
#define INJECT_ERRTYPE_PLATFORM_CORRECTABLE 0x00000200
#endif
#ifndef INJECT_ERRTYPE_PLATFORM_UNCORRECTABLEFATAL
#define INJECT_ERRTYPE_PLATFORM_UNCORRECTABLEFATAL 0x00000800
#endif
#ifndef INJECT_ERRTYPE_PLATFORM_UNCORRECTABLENONFATAL
#define INJECT_ERRTYPE_PLATFORM_UNCORRECTABLENONFATAL 0x00000400
#endif
#ifndef INJECT_ERRTYPE_PROCESSOR_CORRECTABLE
#define INJECT_ERRTYPE_PROCESSOR_CORRECTABLE 0x00000001
#endif
#ifndef INJECT_ERRTYPE_PROCESSOR_UNCORRECTABLEFATAL
#define INJECT_ERRTYPE_PROCESSOR_UNCORRECTABLEFATAL 0x00000004
#endif
#ifndef INJECT_ERRTYPE_PROCESSOR_UNCORRECTABLENONFATAL
#define INJECT_ERRTYPE_PROCESSOR_UNCORRECTABLENONFATAL 0x00000002
#endif
#ifndef IPMI_IOCTL_INDEX
#define IPMI_IOCTL_INDEX 0x0400
#endif
#ifndef IPMI_OS_SEL_RECORD_MASK
#define IPMI_OS_SEL_RECORD_MASK 0xFFFF
#endif
#ifndef IPMI_OS_SEL_RECORD_VERSION_1
#define IPMI_OS_SEL_RECORD_VERSION_1 1
#endif
#ifndef LINKSPEED_MTS_16000
#define LINKSPEED_MTS_16000 16000
#endif
#ifndef LINKSPEED_MTS_2500
#define LINKSPEED_MTS_2500 2500
#endif
#ifndef LINKSPEED_MTS_32000
#define LINKSPEED_MTS_32000 32000
#endif
#ifndef LINKSPEED_MTS_5000
#define LINKSPEED_MTS_5000 5000
#endif
#ifndef LINKSPEED_MTS_64000
#define LINKSPEED_MTS_64000 64000
#endif
#ifndef LINKSPEED_MTS_8000
#define LINKSPEED_MTS_8000 8000
#endif
#ifndef MAX_SEL_RAW_EVENT_PAYLOAD_LENGTH
#define MAX_SEL_RAW_EVENT_PAYLOAD_LENGTH 256
#endif
#ifndef MM_ADD_PHYSICAL_MEMORY_ALREADY_ZEROED
#define MM_ADD_PHYSICAL_MEMORY_ALREADY_ZEROED 0x1
#endif
#ifndef MM_ADD_PHYSICAL_MEMORY_HUGE_PAGES_ONLY
#define MM_ADD_PHYSICAL_MEMORY_HUGE_PAGES_ONLY 0x4
#endif
#ifndef MM_ADD_PHYSICAL_MEMORY_LARGE_PAGES_ONLY
#define MM_ADD_PHYSICAL_MEMORY_LARGE_PAGES_ONLY 0x2
#endif
#ifndef MM_ALLOCATE_CONTIGUOUS_MEMORY_FAST_ONLY
#define MM_ALLOCATE_CONTIGUOUS_MEMORY_FAST_ONLY 0x1
#endif
#ifndef MM_GET_CACHE_ATTRIBUTE_IO_SPACE
#define MM_GET_CACHE_ATTRIBUTE_IO_SPACE 0x1
#endif
#ifndef MM_GET_PHYSICAL_MEMORY_RANGES_INCLUDE_ALL_PARTITIONS
#define MM_GET_PHYSICAL_MEMORY_RANGES_INCLUDE_ALL_PARTITIONS 0x00000002
#endif
#ifndef MM_GET_PHYSICAL_MEMORY_RANGES_INCLUDE_FILE_ONLY
#define MM_GET_PHYSICAL_MEMORY_RANGES_INCLUDE_FILE_ONLY 0x00000001
#endif
#ifndef MM_REMOVE_PHYSICAL_MEMORY_BAD_ONLY
#define MM_REMOVE_PHYSICAL_MEMORY_BAD_ONLY 0x1
#endif
#ifndef MM_SECURE_EXCLUSIVE
#define MM_SECURE_EXCLUSIVE 0x1
#endif
#ifndef MM_SECURE_NO_CHANGE
#define MM_SECURE_NO_CHANGE 0x2
#endif
#ifndef MM_SECURE_NO_INHERIT
#define MM_SECURE_NO_INHERIT 0x8
#endif
#ifndef MM_SECURE_USER_MODE_ONLY
#define MM_SECURE_USER_MODE_ONLY 0x4
#endif
#ifndef OPLOCK_KEY_FLAG_PARENT_KEY
#define OPLOCK_KEY_FLAG_PARENT_KEY 0x0001
#endif
#ifndef OPLOCK_KEY_FLAG_TARGET_KEY
#define OPLOCK_KEY_FLAG_TARGET_KEY 0x0002
#endif
#ifndef OPLOCK_KEY_VERSION_WIN7
#define OPLOCK_KEY_VERSION_WIN7 0x0001
#endif
#ifndef OPLOCK_KEY_VERSION_WIN8
#define OPLOCK_KEY_VERSION_WIN8 0x0002
#endif
#ifndef PCIXBUS_ERRTYPE_ADDRESSPARITY
#define PCIXBUS_ERRTYPE_ADDRESSPARITY 0x0006
#endif
#ifndef PCIXBUS_ERRTYPE_BUSTIMEOUT
#define PCIXBUS_ERRTYPE_BUSTIMEOUT 0x0004
#endif
#ifndef PCIXBUS_ERRTYPE_COMMANDPARITY
#define PCIXBUS_ERRTYPE_COMMANDPARITY 0x0007
#endif
#ifndef PCIXBUS_ERRTYPE_DATAPARITY
#define PCIXBUS_ERRTYPE_DATAPARITY 0x0001
#endif
#ifndef PCIXBUS_ERRTYPE_MASTERABORT
#define PCIXBUS_ERRTYPE_MASTERABORT 0x0003
#endif
#ifndef PCIXBUS_ERRTYPE_MASTERDATAPARITY
#define PCIXBUS_ERRTYPE_MASTERDATAPARITY 0x0005
#endif
#ifndef PCIXBUS_ERRTYPE_SYSTEM
#define PCIXBUS_ERRTYPE_SYSTEM 0x0002
#endif
#ifndef PCIXBUS_ERRTYPE_UNKNOWN
#define PCIXBUS_ERRTYPE_UNKNOWN 0x0000
#endif
#ifndef PCI_EXPRESS_TPH_ST_LOCATION_MSIX_TABLE
#define PCI_EXPRESS_TPH_ST_LOCATION_MSIX_TABLE 2
#endif
#ifndef PCI_EXPRESS_TPH_ST_LOCATION_NONE
#define PCI_EXPRESS_TPH_ST_LOCATION_NONE 0
#endif
#ifndef PCI_EXPRESS_TPH_ST_LOCATION_RESERVED
#define PCI_EXPRESS_TPH_ST_LOCATION_RESERVED 3
#endif
#ifndef PCI_EXPRESS_TPH_ST_LOCATION_TPH_CAPABILITY
#define PCI_EXPRESS_TPH_ST_LOCATION_TPH_CAPABILITY 1
#endif
#ifndef PCI_INVALID_ALTERNATE_FUNCTION_NUMBER
#define PCI_INVALID_ALTERNATE_FUNCTION_NUMBER 0xFF
#endif
#ifndef PCI_LNKINTRF_VERSION
#define PCI_LNKINTRF_VERSION 1
#endif
#ifndef POWER_THROTTLING_PROCESS_CURRENT_VERSION
#define POWER_THROTTLING_PROCESS_CURRENT_VERSION 1
#endif
#ifndef POWER_THROTTLING_PROCESS_DELAYTIMERS
#define POWER_THROTTLING_PROCESS_DELAYTIMERS 0x2
#endif
#ifndef POWER_THROTTLING_PROCESS_EXECUTION_SPEED
#define POWER_THROTTLING_PROCESS_EXECUTION_SPEED 0x1
#endif
#ifndef POWER_THROTTLING_PROCESS_IGNORE_TIMER_RESOLUTION
#define POWER_THROTTLING_PROCESS_IGNORE_TIMER_RESOLUTION 0x4
#endif
#ifndef POWER_THROTTLING_THREAD_CURRENT_VERSION
#define POWER_THROTTLING_THREAD_CURRENT_VERSION 1
#endif
#ifndef POWER_THROTTLING_THREAD_EXECUTION_SPEED
#define POWER_THROTTLING_THREAD_EXECUTION_SPEED 0x1
#endif
#ifndef PROCESS_HANDLE_EXCEPTIONS_ENABLED
#define PROCESS_HANDLE_EXCEPTIONS_ENABLED 0x00000001ul
#endif
#ifndef PROCESS_HANDLE_RAISE_UM_EXCEPTION_ON_INVALID_HANDLE_CLOSE_DISABLED
#define PROCESS_HANDLE_RAISE_UM_EXCEPTION_ON_INVALID_HANDLE_CLOSE_DISABLED 0x00000000ul
#endif
#ifndef PROCESS_HANDLE_RAISE_UM_EXCEPTION_ON_INVALID_HANDLE_CLOSE_ENABLED
#define PROCESS_HANDLE_RAISE_UM_EXCEPTION_ON_INVALID_HANDLE_CLOSE_ENABLED 0x00000001ul
#endif
#ifndef PS_IMAGE_NOTIFY_CONFLICTING_ARCHITECTURE
#define PS_IMAGE_NOTIFY_CONFLICTING_ARCHITECTURE 0x1
#endif
#ifndef PS_INVALID_SILO_CONTEXT_SLOT
#define PS_INVALID_SILO_CONTEXT_SLOT 0xffffffffUL
#endif
#ifndef PshedFADiscovery
#define PshedFADiscovery 0x00000001
#endif
#ifndef PshedFAErrorInfoRetrieval
#define PshedFAErrorInfoRetrieval 0x00000008
#endif
#ifndef PshedFAErrorInjection
#define PshedFAErrorInjection 0x00000020
#endif
#ifndef PshedFAErrorRecordPersistence
#define PshedFAErrorRecordPersistence 0x00000004
#endif
#ifndef PshedFAErrorRecovery
#define PshedFAErrorRecovery 0x00000010
#endif
#ifndef PshedFAErrorSourceControl
#define PshedFAErrorSourceControl 0x00000002
#endif
#ifndef RTL_CORRELATION_VECTOR_STRING_LENGTH
#define RTL_CORRELATION_VECTOR_STRING_LENGTH 129
#endif
#ifndef RTL_CORRELATION_VECTOR_V1_LENGTH
#define RTL_CORRELATION_VECTOR_V1_LENGTH 64
#endif
#ifndef RTL_CORRELATION_VECTOR_V1_PREFIX_LENGTH
#define RTL_CORRELATION_VECTOR_V1_PREFIX_LENGTH 16
#endif
#ifndef RTL_CORRELATION_VECTOR_V2_LENGTH
#define RTL_CORRELATION_VECTOR_V2_LENGTH 128
#endif
#ifndef RTL_CORRELATION_VECTOR_V2_PREFIX_LENGTH
#define RTL_CORRELATION_VECTOR_V2_PREFIX_LENGTH 22
#endif
#ifndef SEH_VALIDATION_POLICY_DEFER
#define SEH_VALIDATION_POLICY_DEFER 3
#endif
#ifndef SEH_VALIDATION_POLICY_OFF
#define SEH_VALIDATION_POLICY_OFF 1
#endif
#ifndef SEH_VALIDATION_POLICY_ON
#define SEH_VALIDATION_POLICY_ON 0
#endif
#ifndef SEH_VALIDATION_POLICY_TELEMETRY
#define SEH_VALIDATION_POLICY_TELEMETRY 2
#endif
#ifndef SHARED_GLOBAL_FLAGS_ADMINAPPROVALMODE_TYPE_SHADOWADMIN_V
#define SHARED_GLOBAL_FLAGS_ADMINAPPROVALMODE_TYPE_SHADOWADMIN_V 0xC
#endif
#ifndef SHARED_GLOBAL_FLAGS_ADMINAPPROVALMODE_TYPE_SPLITTOKEN_V
#define SHARED_GLOBAL_FLAGS_ADMINAPPROVALMODE_TYPE_SPLITTOKEN_V 0xB
#endif
#ifndef SHARED_GLOBAL_FLAGS_CLEAR_GLOBAL_DATA_FLAG
#define SHARED_GLOBAL_FLAGS_CLEAR_GLOBAL_DATA_FLAG 0x80000000
#endif
#ifndef SHARED_GLOBAL_FLAGS_CONSOLE_BROKER_ENABLED_V
#define SHARED_GLOBAL_FLAGS_CONSOLE_BROKER_ENABLED_V 0x6
#endif
#ifndef SHARED_GLOBAL_FLAGS_LKG_ENABLED_V
#define SHARED_GLOBAL_FLAGS_LKG_ENABLED_V 0x4
#endif
#ifndef SHARED_GLOBAL_FLAGS_MULTIUSERS_IN_SESSION_SKU_V
#define SHARED_GLOBAL_FLAGS_MULTIUSERS_IN_SESSION_SKU_V 0x9
#endif
#ifndef SHARED_GLOBAL_FLAGS_MULTI_SESSION_SKU_V
#define SHARED_GLOBAL_FLAGS_MULTI_SESSION_SKU_V 0x8
#endif
#ifndef SHARED_GLOBAL_FLAGS_SECURE_BOOT_ENABLED_V
#define SHARED_GLOBAL_FLAGS_SECURE_BOOT_ENABLED_V 0x7
#endif
#ifndef SHARED_GLOBAL_FLAGS_SET_GLOBAL_DATA_FLAG
#define SHARED_GLOBAL_FLAGS_SET_GLOBAL_DATA_FLAG 0x40000000
#endif
#ifndef SHARED_GLOBAL_FLAGS_STATE_SEPARATION_ENABLED_V
#define SHARED_GLOBAL_FLAGS_STATE_SEPARATION_ENABLED_V 0xA
#endif
#ifndef SILO_MONITOR_REGISTRATION_VERSION
#define SILO_MONITOR_REGISTRATION_VERSION 1
#endif
#ifndef SYSTEM_CALL_SYSCALL
#define SYSTEM_CALL_SYSCALL 0
#endif
#ifndef WCS_RAS_REGISTER_NAME_MAX_LENGTH
#define WCS_RAS_REGISTER_NAME_MAX_LENGTH 32
#endif
#ifndef WHEA_AMD_EXT_REG_NUM
#define WHEA_AMD_EXT_REG_NUM 10
#endif
#ifndef WHEA_ARM_RAS_NODE_FIELD_COUNT
#define WHEA_ARM_RAS_NODE_FIELD_COUNT 8
#endif
#ifndef WHEA_BAD_PAGE_LIST_LOCATION
#define WHEA_BAD_PAGE_LIST_LOCATION 15
#endif
#ifndef WHEA_BAD_PAGE_LIST_MAX_SIZE
#define WHEA_BAD_PAGE_LIST_MAX_SIZE 14
#endif
#ifndef WHEA_CMCI_THRESHOLD_COUNT
#define WHEA_CMCI_THRESHOLD_COUNT 10
#endif
#ifndef WHEA_CMCI_THRESHOLD_POLL_COUNT
#define WHEA_CMCI_THRESHOLD_POLL_COUNT 12
#endif
#ifndef WHEA_CMCI_THRESHOLD_TIME
#define WHEA_CMCI_THRESHOLD_TIME 11
#endif
#ifndef WHEA_DEVICE_DRIVER_BUFFER_SET_MAX
#define WHEA_DEVICE_DRIVER_BUFFER_SET_MAX 1
#endif
#ifndef WHEA_DEVICE_DRIVER_BUFFER_SET_MIN
#define WHEA_DEVICE_DRIVER_BUFFER_SET_MIN 1
#endif
#ifndef WHEA_DEVICE_DRIVER_BUFFER_SET_V1
#define WHEA_DEVICE_DRIVER_BUFFER_SET_V1 1
#endif
#ifndef WHEA_DEVICE_DRIVER_CONFIG_MAX
#define WHEA_DEVICE_DRIVER_CONFIG_MAX 2
#endif
#ifndef WHEA_DEVICE_DRIVER_CONFIG_MIN
#define WHEA_DEVICE_DRIVER_CONFIG_MIN 1
#endif
#ifndef WHEA_DEVICE_DRIVER_CONFIG_V1
#define WHEA_DEVICE_DRIVER_CONFIG_V1 1
#endif
#ifndef WHEA_DEVICE_DRIVER_CONFIG_V2
#define WHEA_DEVICE_DRIVER_CONFIG_V2 2
#endif
#ifndef WHEA_DISABLE_DUMMY_WRITE
#define WHEA_DISABLE_DUMMY_WRITE 6
#endif
#ifndef WHEA_DISABLE_OFFLINE
#define WHEA_DISABLE_OFFLINE 0
#endif
#ifndef WHEA_DISABLE_PRM_ADDRESS_TRANSLATION
#define WHEA_DISABLE_PRM_ADDRESS_TRANSLATION 20
#endif
#ifndef WHEA_ENABLE_BATCHED_ROW_OFFLINE
#define WHEA_ENABLE_BATCHED_ROW_OFFLINE 21
#endif
#ifndef WHEA_ERROR_LOG_ENTRY_VERSION
#define WHEA_ERROR_LOG_ENTRY_VERSION 1
#endif
#ifndef WHEA_ERROR_PACKET_V1_VERSION
#define WHEA_ERROR_PACKET_V1_VERSION 2
#endif
#ifndef WHEA_ERROR_RECORD_FLAGS_DEVICE_DRIVER
#define WHEA_ERROR_RECORD_FLAGS_DEVICE_DRIVER 0x00000008
#endif
#ifndef WHEA_ERROR_RECORD_FLAGS_PREVIOUSERROR
#define WHEA_ERROR_RECORD_FLAGS_PREVIOUSERROR 0x00000002
#endif
#ifndef WHEA_ERROR_RECORD_FLAGS_RECOVERED
#define WHEA_ERROR_RECORD_FLAGS_RECOVERED 0x00000001
#endif
#ifndef WHEA_ERROR_RECORD_FLAGS_SIMULATED
#define WHEA_ERROR_RECORD_FLAGS_SIMULATED 0x00000004
#endif
#ifndef WHEA_ERROR_RECORD_REVISION
#define WHEA_ERROR_RECORD_REVISION 0x0210
#endif
#ifndef WHEA_ERROR_RECORD_SECTION_DESCRIPTOR_REVISION
#define WHEA_ERROR_RECORD_SECTION_DESCRIPTOR_REVISION 0x0300
#endif
#ifndef WHEA_ERROR_RECORD_SIGNATURE_END
#define WHEA_ERROR_RECORD_SIGNATURE_END 0xFFFFFFFF
#endif
#ifndef WHEA_ERROR_RECORD_VALID_PARTITIONID
#define WHEA_ERROR_RECORD_VALID_PARTITIONID 0x00000004
#endif
#ifndef WHEA_ERROR_RECORD_VALID_PLATFORMID
#define WHEA_ERROR_RECORD_VALID_PLATFORMID 0x00000001
#endif
#ifndef WHEA_ERROR_RECORD_VALID_TIMESTAMP
#define WHEA_ERROR_RECORD_VALID_TIMESTAMP 0x00000002
#endif
#ifndef WHEA_ERROR_SOURCE_DESCRIPTOR_TYPE_AERBRIDGE
#define WHEA_ERROR_SOURCE_DESCRIPTOR_TYPE_AERBRIDGE 8
#endif
#ifndef WHEA_ERROR_SOURCE_DESCRIPTOR_TYPE_AERENDPOINT
#define WHEA_ERROR_SOURCE_DESCRIPTOR_TYPE_AERENDPOINT 7
#endif
#ifndef WHEA_ERROR_SOURCE_DESCRIPTOR_TYPE_AERROOTPORT
#define WHEA_ERROR_SOURCE_DESCRIPTOR_TYPE_AERROOTPORT 6
#endif
#ifndef WHEA_ERROR_SOURCE_DESCRIPTOR_TYPE_GENERIC
#define WHEA_ERROR_SOURCE_DESCRIPTOR_TYPE_GENERIC 9
#endif
#ifndef WHEA_ERROR_SOURCE_DESCRIPTOR_TYPE_GENERIC_V2
#define WHEA_ERROR_SOURCE_DESCRIPTOR_TYPE_GENERIC_V2 10
#endif
#ifndef WHEA_ERROR_SOURCE_DESCRIPTOR_TYPE_IPFCMC
#define WHEA_ERROR_SOURCE_DESCRIPTOR_TYPE_IPFCMC 4
#endif
#ifndef WHEA_ERROR_SOURCE_DESCRIPTOR_TYPE_IPFCPE
#define WHEA_ERROR_SOURCE_DESCRIPTOR_TYPE_IPFCPE 5
#endif
#ifndef WHEA_ERROR_SOURCE_DESCRIPTOR_TYPE_IPFMCA
#define WHEA_ERROR_SOURCE_DESCRIPTOR_TYPE_IPFMCA 3
#endif
#ifndef WHEA_ERROR_SOURCE_DESCRIPTOR_TYPE_XPFCMC
#define WHEA_ERROR_SOURCE_DESCRIPTOR_TYPE_XPFCMC 1
#endif
#ifndef WHEA_ERROR_SOURCE_DESCRIPTOR_TYPE_XPFMCE
#define WHEA_ERROR_SOURCE_DESCRIPTOR_TYPE_XPFMCE 0
#endif
#ifndef WHEA_ERROR_SOURCE_DESCRIPTOR_TYPE_XPFNMI
#define WHEA_ERROR_SOURCE_DESCRIPTOR_TYPE_XPFNMI 2
#endif
#ifndef WHEA_ERROR_SOURCE_DESCRIPTOR_VERSION_11
#define WHEA_ERROR_SOURCE_DESCRIPTOR_VERSION_11 11
#endif
#ifndef WHEA_ERROR_SOURCE_FLAG_DEFAULTSOURCE
#define WHEA_ERROR_SOURCE_FLAG_DEFAULTSOURCE 0x80000000
#endif
#ifndef WHEA_ERROR_SOURCE_FLAG_FIRMWAREFIRST
#define WHEA_ERROR_SOURCE_FLAG_FIRMWAREFIRST 0x00000001
#endif
#ifndef WHEA_ERROR_SOURCE_FLAG_GHES_ASSIST
#define WHEA_ERROR_SOURCE_FLAG_GHES_ASSIST 0x00000004
#endif
#ifndef WHEA_ERROR_SOURCE_FLAG_GLOBAL
#define WHEA_ERROR_SOURCE_FLAG_GLOBAL 0x00000002
#endif
#ifndef WHEA_ERROR_SOURCE_INVALID_RELATED_SOURCE
#define WHEA_ERROR_SOURCE_INVALID_RELATED_SOURCE 0xFFFF
#endif
#ifndef WHEA_ERROR_TEXT_LEN
#define WHEA_ERROR_TEXT_LEN 32
#endif
#ifndef WHEA_ERR_SRC_OVERRIDE_FLAG
#define WHEA_ERR_SRC_OVERRIDE_FLAG 0x40000000
#endif
#ifndef WHEA_FIRMWARE_RECORD_TYPE_IPFSAL
#define WHEA_FIRMWARE_RECORD_TYPE_IPFSAL 0
#endif
#ifndef WHEA_GENERIC_ENTRY_TEXT_LEN
#define WHEA_GENERIC_ENTRY_TEXT_LEN 20
#endif
#ifndef WHEA_GENERIC_ENTRY_V2_VERSION
#define WHEA_GENERIC_ENTRY_V2_VERSION 0x300
#endif
#ifndef WHEA_INVALID_ERR_SRC_ID
#define WHEA_INVALID_ERR_SRC_ID 0
#endif
#ifndef WHEA_MAX_LOG_DATA_LEN
#define WHEA_MAX_LOG_DATA_LEN 36
#endif
#ifndef WHEA_MAX_MC_BANKS
#define WHEA_MAX_MC_BANKS 32
#endif
#ifndef WHEA_MEMERRTYPE_INVALIDADDRESS
#define WHEA_MEMERRTYPE_INVALIDADDRESS 0x0A
#endif
#ifndef WHEA_MEMERRTYPE_MASTERABORT
#define WHEA_MEMERRTYPE_MASTERABORT 0x06
#endif
#ifndef WHEA_MEMERRTYPE_MEMORYSPARING
#define WHEA_MEMERRTYPE_MEMORYSPARING 0x0C
#endif
#ifndef WHEA_MEMERRTYPE_MIRRORBROKEN
#define WHEA_MEMERRTYPE_MIRRORBROKEN 0x0B
#endif
#ifndef WHEA_MEMERRTYPE_MULTIBITECC
#define WHEA_MEMERRTYPE_MULTIBITECC 0x03
#endif
#ifndef WHEA_MEMERRTYPE_MULTISYMCHIPKILL
#define WHEA_MEMERRTYPE_MULTISYMCHIPKILL 0x05
#endif
#ifndef WHEA_MEMERRTYPE_NOERROR
#define WHEA_MEMERRTYPE_NOERROR 0x01
#endif
#ifndef WHEA_MEMERRTYPE_PARITYERROR
#define WHEA_MEMERRTYPE_PARITYERROR 0x08
#endif
#ifndef WHEA_MEMERRTYPE_SINGLEBITECC
#define WHEA_MEMERRTYPE_SINGLEBITECC 0x02
#endif
#ifndef WHEA_MEMERRTYPE_SINGLESYMCHIPKILL
#define WHEA_MEMERRTYPE_SINGLESYMCHIPKILL 0x04
#endif
#ifndef WHEA_MEMERRTYPE_TARGETABORT
#define WHEA_MEMERRTYPE_TARGETABORT 0x07
#endif
#ifndef WHEA_MEMERRTYPE_UNKNOWN
#define WHEA_MEMERRTYPE_UNKNOWN 0x00
#endif
#ifndef WHEA_MEMERRTYPE_WATCHDOGTIMEOUT
#define WHEA_MEMERRTYPE_WATCHDOGTIMEOUT 0x09
#endif
#ifndef WHEA_MEM_PERSISTOFFLINE
#define WHEA_MEM_PERSISTOFFLINE 1
#endif
#ifndef WHEA_MEM_PFA_DISABLE
#define WHEA_MEM_PFA_DISABLE 2
#endif
#ifndef WHEA_MEM_PFA_PAGECOUNT
#define WHEA_MEM_PFA_PAGECOUNT 3
#endif
#ifndef WHEA_MEM_PFA_THRESHOLD
#define WHEA_MEM_PFA_THRESHOLD 4
#endif
#ifndef WHEA_MEM_PFA_TIMEOUT
#define WHEA_MEM_PFA_TIMEOUT 5
#endif
#ifndef WHEA_NOTIFICATION_TYPE_ARMV8_SEA
#define WHEA_NOTIFICATION_TYPE_ARMV8_SEA 8
#endif
#ifndef WHEA_NOTIFICATION_TYPE_ARMV8_SEI
#define WHEA_NOTIFICATION_TYPE_ARMV8_SEI 9
#endif
#ifndef WHEA_NOTIFICATION_TYPE_CMCI
#define WHEA_NOTIFICATION_TYPE_CMCI 5
#endif
#ifndef WHEA_NOTIFICATION_TYPE_EXTERNALINTERRUPT
#define WHEA_NOTIFICATION_TYPE_EXTERNALINTERRUPT 1
#endif
#ifndef WHEA_NOTIFICATION_TYPE_EXTERNALINTERRUPT_GSIV
#define WHEA_NOTIFICATION_TYPE_EXTERNALINTERRUPT_GSIV 10
#endif
#ifndef WHEA_NOTIFICATION_TYPE_GPIO_SIGNAL
#define WHEA_NOTIFICATION_TYPE_GPIO_SIGNAL 7
#endif
#ifndef WHEA_NOTIFICATION_TYPE_LOCALINTERRUPT
#define WHEA_NOTIFICATION_TYPE_LOCALINTERRUPT 2
#endif
#ifndef WHEA_NOTIFICATION_TYPE_MCE
#define WHEA_NOTIFICATION_TYPE_MCE 6
#endif
#ifndef WHEA_NOTIFICATION_TYPE_NMI
#define WHEA_NOTIFICATION_TYPE_NMI 4
#endif
#ifndef WHEA_NOTIFICATION_TYPE_POLLED
#define WHEA_NOTIFICATION_TYPE_POLLED 0
#endif
#ifndef WHEA_NOTIFICATION_TYPE_SCI
#define WHEA_NOTIFICATION_TYPE_SCI 3
#endif
#ifndef WHEA_NOTIFICATION_TYPE_SDEI
#define WHEA_NOTIFICATION_TYPE_SDEI 11
#endif
#ifndef WHEA_NOTIFY_ALL_OFFLINES
#define WHEA_NOTIFY_ALL_OFFLINES 16
#endif
#ifndef WHEA_PCIE_CORRECTABLE_ERROR_SECTION_COUNT_SIZE
#define WHEA_PCIE_CORRECTABLE_ERROR_SECTION_COUNT_SIZE 32
#endif
#ifndef WHEA_PENDING_PAGE_LIST_SZ
#define WHEA_PENDING_PAGE_LIST_SZ 13
#endif
#ifndef WHEA_PLUGIN_REGISTRATION_PACKET_V1
#define WHEA_PLUGIN_REGISTRATION_PACKET_V1 0x00010000
#endif
#ifndef WHEA_PLUGIN_REGISTRATION_PACKET_V2
#define WHEA_PLUGIN_REGISTRATION_PACKET_V2 0x00020000
#endif
#ifndef WHEA_PMEM_ERROR_SECTION_LOCATION_INFO_SIZE
#define WHEA_PMEM_ERROR_SECTION_LOCATION_INFO_SIZE 64
#endif
#ifndef WHEA_PMEM_ERROR_SECTION_MAX_PAGES
#define WHEA_PMEM_ERROR_SECTION_MAX_PAGES 50
#endif
#ifndef WHEA_PRM_ADDRESS_TRANSLATION_INTEL_FORWARD_ADDRESS_TRANSLATE
#define WHEA_PRM_ADDRESS_TRANSLATION_INTEL_FORWARD_ADDRESS_TRANSLATE 2
#endif
#ifndef WHEA_PRM_ADDRESS_TRANSLATION_INTEL_GET_ADDRESS_PARAMETERS
#define WHEA_PRM_ADDRESS_TRANSLATION_INTEL_GET_ADDRESS_PARAMETERS 1
#endif
#ifndef WHEA_PRM_ADDRESS_TRANSLATION_INTEL_INTERNAL_ERROR
#define WHEA_PRM_ADDRESS_TRANSLATION_INTEL_INTERNAL_ERROR 3
#endif
#ifndef WHEA_PRM_ADDRESS_TRANSLATION_INTEL_INVALID_COMMAND
#define WHEA_PRM_ADDRESS_TRANSLATION_INTEL_INVALID_COMMAND 2
#endif
#ifndef WHEA_PRM_ADDRESS_TRANSLATION_INTEL_REVERSE_ADDRESS_TRANSLATE
#define WHEA_PRM_ADDRESS_TRANSLATION_INTEL_REVERSE_ADDRESS_TRANSLATE 3
#endif
#ifndef WHEA_PRM_ADDRESS_TRANSLATION_INTEL_SUCCESS
#define WHEA_PRM_ADDRESS_TRANSLATION_INTEL_SUCCESS 0
#endif
#ifndef WHEA_PRM_ADDRESS_TRANSLATION_INTEL_UNKNOWN_FAILURE
#define WHEA_PRM_ADDRESS_TRANSLATION_INTEL_UNKNOWN_FAILURE 1
#endif
#ifndef WHEA_RESTORE_CMCI_ATTEMPTS
#define WHEA_RESTORE_CMCI_ATTEMPTS 8
#endif
#ifndef WHEA_RESTORE_CMCI_ENABLED
#define WHEA_RESTORE_CMCI_ENABLED 7
#endif
#ifndef WHEA_RESTORE_CMCI_ERR_LIMIT
#define WHEA_RESTORE_CMCI_ERR_LIMIT 9
#endif
#ifndef WHEA_ROW_FAIL_CHECK_ENABLE
#define WHEA_ROW_FAIL_CHECK_ENABLE 18
#endif
#ifndef WHEA_ROW_FAIL_CHECK_EXTENT
#define WHEA_ROW_FAIL_CHECK_EXTENT 17
#endif
#ifndef WHEA_ROW_FAIL_CHECK_THRESHOLD
#define WHEA_ROW_FAIL_CHECK_THRESHOLD 19
#endif
#ifndef WHEA_SECTION_DESCRIPTOR_FLAGS_CONTAINMENTWRN
#define WHEA_SECTION_DESCRIPTOR_FLAGS_CONTAINMENTWRN 0x00000002
#endif
#ifndef WHEA_SECTION_DESCRIPTOR_FLAGS_FRU_TEXT_BY_PLUGIN
#define WHEA_SECTION_DESCRIPTOR_FLAGS_FRU_TEXT_BY_PLUGIN 0x00000080
#endif
#ifndef WHEA_SECTION_DESCRIPTOR_FLAGS_LATENTERROR
#define WHEA_SECTION_DESCRIPTOR_FLAGS_LATENTERROR 0x00000020
#endif
#ifndef WHEA_SECTION_DESCRIPTOR_FLAGS_PRIMARY
#define WHEA_SECTION_DESCRIPTOR_FLAGS_PRIMARY 0x00000001
#endif
#ifndef WHEA_SECTION_DESCRIPTOR_FLAGS_PROPAGATED
#define WHEA_SECTION_DESCRIPTOR_FLAGS_PROPAGATED 0x00000040
#endif
#ifndef WHEA_SECTION_DESCRIPTOR_FLAGS_RESET
#define WHEA_SECTION_DESCRIPTOR_FLAGS_RESET 0x00000004
#endif
#ifndef WHEA_SECTION_DESCRIPTOR_FLAGS_RESOURCENA
#define WHEA_SECTION_DESCRIPTOR_FLAGS_RESOURCENA 0x00000010
#endif
#ifndef WHEA_SECTION_DESCRIPTOR_FLAGS_THRESHOLDEXCEEDED
#define WHEA_SECTION_DESCRIPTOR_FLAGS_THRESHOLDEXCEEDED 0x00000008
#endif
#ifndef WHEA_SEL_BUGCHECK_RECOVERY_STATUS_PHASE1_VERSION
#define WHEA_SEL_BUGCHECK_RECOVERY_STATUS_PHASE1_VERSION 1
#endif
#ifndef WHEA_WRITE_FLAG_DUMMY
#define WHEA_WRITE_FLAG_DUMMY 0x00000001
#endif
#ifndef WHEA_XPF_MCA_EXBANK_COUNT
#define WHEA_XPF_MCA_EXBANK_COUNT 32
#endif
#ifndef WHEA_XPF_MCA_EXTREG_MAX_COUNT
#define WHEA_XPF_MCA_EXTREG_MAX_COUNT 24
#endif
#ifndef WHEA_XPF_MCA_SECTION_VERSION_2
#define WHEA_XPF_MCA_SECTION_VERSION_2 2
#endif
#ifndef WHEA_XPF_MCA_SECTION_VERSION_3
#define WHEA_XPF_MCA_SECTION_VERSION_3 3
#endif
#ifndef WHEA_XPF_MCA_SECTION_VERSION_4
#define WHEA_XPF_MCA_SECTION_VERSION_4 4
#endif
#ifndef WHEA_XPF_MC_BANK_STATUSFORMAT_AMD64MCA
#define WHEA_XPF_MC_BANK_STATUSFORMAT_AMD64MCA 2
#endif
#ifndef WHEA_XPF_MC_BANK_STATUSFORMAT_IA32MCA
#define WHEA_XPF_MC_BANK_STATUSFORMAT_IA32MCA 0
#endif
#ifndef WHEA_XPF_MC_BANK_STATUSFORMAT_Intel64MCA
#define WHEA_XPF_MC_BANK_STATUSFORMAT_Intel64MCA 1
#endif
#ifndef WRITE_NV_MEMORY_FLAG_FLUSH
#define WRITE_NV_MEMORY_FLAG_FLUSH 0x00000001
#endif
#ifndef WRITE_NV_MEMORY_FLAG_NON_TEMPORAL
#define WRITE_NV_MEMORY_FLAG_NON_TEMPORAL 0x00000002
#endif
#ifndef WRITE_NV_MEMORY_FLAG_NO_DRAIN
#define WRITE_NV_MEMORY_FLAG_NO_DRAIN 0x00000100
#endif
#ifndef XPF_BUS_CHECK_ADDRESS_IO
#define XPF_BUS_CHECK_ADDRESS_IO 2
#endif
#ifndef XPF_BUS_CHECK_ADDRESS_MEMORY
#define XPF_BUS_CHECK_ADDRESS_MEMORY 0
#endif
#ifndef XPF_BUS_CHECK_ADDRESS_OTHER
#define XPF_BUS_CHECK_ADDRESS_OTHER 3
#endif
#ifndef XPF_BUS_CHECK_ADDRESS_RESERVED
#define XPF_BUS_CHECK_ADDRESS_RESERVED 1
#endif
#ifndef XPF_BUS_CHECK_OPERATION_DATAREAD
#define XPF_BUS_CHECK_OPERATION_DATAREAD 3
#endif
#ifndef XPF_BUS_CHECK_OPERATION_DATAWRITE
#define XPF_BUS_CHECK_OPERATION_DATAWRITE 4
#endif
#ifndef XPF_BUS_CHECK_OPERATION_GENERIC
#define XPF_BUS_CHECK_OPERATION_GENERIC 0
#endif
#ifndef XPF_BUS_CHECK_OPERATION_GENREAD
#define XPF_BUS_CHECK_OPERATION_GENREAD 1
#endif
#ifndef XPF_BUS_CHECK_OPERATION_GENWRITE
#define XPF_BUS_CHECK_OPERATION_GENWRITE 2
#endif
#ifndef XPF_BUS_CHECK_OPERATION_INSTRUCTIONFETCH
#define XPF_BUS_CHECK_OPERATION_INSTRUCTIONFETCH 5
#endif
#ifndef XPF_BUS_CHECK_OPERATION_PREFETCH
#define XPF_BUS_CHECK_OPERATION_PREFETCH 6
#endif
#ifndef XPF_BUS_CHECK_PARTICIPATION_GENERIC
#define XPF_BUS_CHECK_PARTICIPATION_GENERIC 3
#endif
#ifndef XPF_BUS_CHECK_PARTICIPATION_PROCOBSERVED
#define XPF_BUS_CHECK_PARTICIPATION_PROCOBSERVED 2
#endif
#ifndef XPF_BUS_CHECK_PARTICIPATION_PROCORIGINATED
#define XPF_BUS_CHECK_PARTICIPATION_PROCORIGINATED 0
#endif
#ifndef XPF_BUS_CHECK_PARTICIPATION_PROCRESPONDED
#define XPF_BUS_CHECK_PARTICIPATION_PROCRESPONDED 1
#endif
#ifndef XPF_BUS_CHECK_TRANSACTIONTYPE_DATAACCESS
#define XPF_BUS_CHECK_TRANSACTIONTYPE_DATAACCESS 1
#endif
#ifndef XPF_BUS_CHECK_TRANSACTIONTYPE_GENERIC
#define XPF_BUS_CHECK_TRANSACTIONTYPE_GENERIC 2
#endif
#ifndef XPF_BUS_CHECK_TRANSACTIONTYPE_INSTRUCTION
#define XPF_BUS_CHECK_TRANSACTIONTYPE_INSTRUCTION 0
#endif
#ifndef XPF_CACHE_CHECK_OPERATION_DATAREAD
#define XPF_CACHE_CHECK_OPERATION_DATAREAD 3
#endif
#ifndef XPF_CACHE_CHECK_OPERATION_DATAWRITE
#define XPF_CACHE_CHECK_OPERATION_DATAWRITE 4
#endif
#ifndef XPF_CACHE_CHECK_OPERATION_EVICTION
#define XPF_CACHE_CHECK_OPERATION_EVICTION 7
#endif
#ifndef XPF_CACHE_CHECK_OPERATION_GENERIC
#define XPF_CACHE_CHECK_OPERATION_GENERIC 0
#endif
#ifndef XPF_CACHE_CHECK_OPERATION_GENREAD
#define XPF_CACHE_CHECK_OPERATION_GENREAD 1
#endif
#ifndef XPF_CACHE_CHECK_OPERATION_GENWRITE
#define XPF_CACHE_CHECK_OPERATION_GENWRITE 2
#endif
#ifndef XPF_CACHE_CHECK_OPERATION_INSTRUCTIONFETCH
#define XPF_CACHE_CHECK_OPERATION_INSTRUCTIONFETCH 5
#endif
#ifndef XPF_CACHE_CHECK_OPERATION_PREFETCH
#define XPF_CACHE_CHECK_OPERATION_PREFETCH 6
#endif
#ifndef XPF_CACHE_CHECK_OPERATION_SNOOP
#define XPF_CACHE_CHECK_OPERATION_SNOOP 8
#endif
#ifndef XPF_CACHE_CHECK_TRANSACTIONTYPE_DATAACCESS
#define XPF_CACHE_CHECK_TRANSACTIONTYPE_DATAACCESS 1
#endif
#ifndef XPF_CACHE_CHECK_TRANSACTIONTYPE_GENERIC
#define XPF_CACHE_CHECK_TRANSACTIONTYPE_GENERIC 2
#endif
#ifndef XPF_CACHE_CHECK_TRANSACTIONTYPE_INSTRUCTION
#define XPF_CACHE_CHECK_TRANSACTIONTYPE_INSTRUCTION 0
#endif
#ifndef XPF_CONTEXT_INFO_32BITCONTEXT
#define XPF_CONTEXT_INFO_32BITCONTEXT 2
#endif
#ifndef XPF_CONTEXT_INFO_32BITDEBUGREGS
#define XPF_CONTEXT_INFO_32BITDEBUGREGS 5
#endif
#ifndef XPF_CONTEXT_INFO_64BITCONTEXT
#define XPF_CONTEXT_INFO_64BITCONTEXT 3
#endif
#ifndef XPF_CONTEXT_INFO_64BITDEBUGREGS
#define XPF_CONTEXT_INFO_64BITDEBUGREGS 6
#endif
#ifndef XPF_CONTEXT_INFO_FXSAVE
#define XPF_CONTEXT_INFO_FXSAVE 4
#endif
#ifndef XPF_CONTEXT_INFO_MMREGISTERS
#define XPF_CONTEXT_INFO_MMREGISTERS 7
#endif
#ifndef XPF_CONTEXT_INFO_MSRREGISTERS
#define XPF_CONTEXT_INFO_MSRREGISTERS 1
#endif
#ifndef XPF_CONTEXT_INFO_UNCLASSIFIEDDATA
#define XPF_CONTEXT_INFO_UNCLASSIFIEDDATA 0
#endif
#ifndef XPF_MS_CHECK_ERRORTYPE_EXTERNAL
#define XPF_MS_CHECK_ERRORTYPE_EXTERNAL 3
#endif
#ifndef XPF_MS_CHECK_ERRORTYPE_FRC
#define XPF_MS_CHECK_ERRORTYPE_FRC 4
#endif
#ifndef XPF_MS_CHECK_ERRORTYPE_INTERNALUNCLASSIFIED
#define XPF_MS_CHECK_ERRORTYPE_INTERNALUNCLASSIFIED 5
#endif
#ifndef XPF_MS_CHECK_ERRORTYPE_MCROMPARITY
#define XPF_MS_CHECK_ERRORTYPE_MCROMPARITY 2
#endif
#ifndef XPF_MS_CHECK_ERRORTYPE_NOERROR
#define XPF_MS_CHECK_ERRORTYPE_NOERROR 0
#endif
#ifndef XPF_MS_CHECK_ERRORTYPE_UNCLASSIFIED
#define XPF_MS_CHECK_ERRORTYPE_UNCLASSIFIED 1
#endif
#ifndef XPF_TLB_CHECK_OPERATION_DATAREAD
#define XPF_TLB_CHECK_OPERATION_DATAREAD 3
#endif
#ifndef XPF_TLB_CHECK_OPERATION_DATAWRITE
#define XPF_TLB_CHECK_OPERATION_DATAWRITE 4
#endif
#ifndef XPF_TLB_CHECK_OPERATION_GENERIC
#define XPF_TLB_CHECK_OPERATION_GENERIC 0
#endif
#ifndef XPF_TLB_CHECK_OPERATION_GENREAD
#define XPF_TLB_CHECK_OPERATION_GENREAD 1
#endif
#ifndef XPF_TLB_CHECK_OPERATION_GENWRITE
#define XPF_TLB_CHECK_OPERATION_GENWRITE 2
#endif
#ifndef XPF_TLB_CHECK_OPERATION_INSTRUCTIONFETCH
#define XPF_TLB_CHECK_OPERATION_INSTRUCTIONFETCH 5
#endif
#ifndef XPF_TLB_CHECK_OPERATION_PREFETCH
#define XPF_TLB_CHECK_OPERATION_PREFETCH 6
#endif
#ifndef XPF_TLB_CHECK_TRANSACTIONTYPE_DATAACCESS
#define XPF_TLB_CHECK_TRANSACTIONTYPE_DATAACCESS 1
#endif
#ifndef XPF_TLB_CHECK_TRANSACTIONTYPE_GENERIC
#define XPF_TLB_CHECK_TRANSACTIONTYPE_GENERIC 2
#endif
#ifndef XPF_TLB_CHECK_TRANSACTIONTYPE_INSTRUCTION
#define XPF_TLB_CHECK_TRANSACTIONTYPE_INSTRUCTION 0
#endif
#ifndef XSTATE_ARM64_SVE
#define XSTATE_ARM64_SVE 2
#endif
