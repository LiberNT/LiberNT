/*
 * miniport.h
 *
 * Type definitions for miniport drivers
 *
 * This file is part of the w32api package.
 *
 * Contributors:
 *   Created by Casper S. Hornstrup <chorns@users.sourceforge.net>
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

#ifndef _MINIPORT_
#define _MINIPORT_

#ifndef _NTDDK_
#define DEVICE_FLAGS CM_DEVICE_FLAGS
#define PDEVICE_FLAGS PCM_DEVICE_FLAGS
#include <ntddk.h>
#undef DEVICE_FLAGS
#undef PDEVICE_FLAGS
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define EMULATOR_READ_ACCESS              0x01
#define EMULATOR_WRITE_ACCESS             0x02

typedef enum _EMULATOR_PORT_ACCESS_TYPE {
  Uchar,
  Ushort,
  Ulong
} EMULATOR_PORT_ACCESS_TYPE, *PEMULATOR_PORT_ACCESS_TYPE;


typedef struct _EMULATOR_ACCESS_ENTRY {
  ULONG BasePort;
  ULONG NumConsecutivePorts;
  EMULATOR_PORT_ACCESS_TYPE AccessType;
  UCHAR AccessMode;
  UCHAR StringSupport;
  PVOID Routine;
} EMULATOR_ACCESS_ENTRY, *PEMULATOR_ACCESS_ENTRY;

typedef VOID
(NTAPI *PBANKED_SECTION_ROUTINE)(
  IN ULONG ReadBank,
  IN ULONG WriteBank,
  IN PVOID Context);


#ifdef __cplusplus
}
#endif

#ifndef ARM_CACHE_ALIGNMENT_SIZE
#define ARM_CACHE_ALIGNMENT_SIZE 128
#endif
#ifndef DMAV3_TRANFER_WIDTH_128
#define DMAV3_TRANFER_WIDTH_128 0x04
#endif
#ifndef DMAV3_TRANFER_WIDTH_16
#define DMAV3_TRANFER_WIDTH_16 0x01
#endif
#ifndef DMAV3_TRANFER_WIDTH_256
#define DMAV3_TRANFER_WIDTH_256 0x05
#endif
#ifndef DMAV3_TRANFER_WIDTH_32
#define DMAV3_TRANFER_WIDTH_32 0x02
#endif
#ifndef DMAV3_TRANFER_WIDTH_64
#define DMAV3_TRANFER_WIDTH_64 0x03
#endif
#ifndef DMAV3_TRANFER_WIDTH_8
#define DMAV3_TRANFER_WIDTH_8 0x00
#endif
#ifndef FPB_MEM_HIGH_VECTOR_GRANULARITY_16GB
#define FPB_MEM_HIGH_VECTOR_GRANULARITY_16GB 0x6
#endif
#ifndef FPB_MEM_HIGH_VECTOR_GRANULARITY_1GB
#define FPB_MEM_HIGH_VECTOR_GRANULARITY_1GB 0x2
#endif
#ifndef FPB_MEM_HIGH_VECTOR_GRANULARITY_1MB
#define FPB_MEM_HIGH_VECTOR_GRANULARITY_1MB 0x9
#endif
#ifndef FPB_MEM_HIGH_VECTOR_GRANULARITY_256MB
#define FPB_MEM_HIGH_VECTOR_GRANULARITY_256MB 0x0
#endif
#ifndef FPB_MEM_HIGH_VECTOR_GRANULARITY_2GB
#define FPB_MEM_HIGH_VECTOR_GRANULARITY_2GB 0x3
#endif
#ifndef FPB_MEM_HIGH_VECTOR_GRANULARITY_32GB
#define FPB_MEM_HIGH_VECTOR_GRANULARITY_32GB 0x7
#endif
#ifndef FPB_MEM_HIGH_VECTOR_GRANULARITY_4GB
#define FPB_MEM_HIGH_VECTOR_GRANULARITY_4GB 0x4
#endif
#ifndef FPB_MEM_HIGH_VECTOR_GRANULARITY_512MB
#define FPB_MEM_HIGH_VECTOR_GRANULARITY_512MB 0x1
#endif
#ifndef FPB_MEM_HIGH_VECTOR_GRANULARITY_8GB
#define FPB_MEM_HIGH_VECTOR_GRANULARITY_8GB 0x5
#endif
#ifndef FPB_MEM_LOW_VECTOR_GRANULARITY_16MB
#define FPB_MEM_LOW_VECTOR_GRANULARITY_16MB 0x4
#endif
#ifndef FPB_MEM_LOW_VECTOR_GRANULARITY_1MB
#define FPB_MEM_LOW_VECTOR_GRANULARITY_1MB 0x0
#endif
#ifndef FPB_MEM_LOW_VECTOR_GRANULARITY_2MB
#define FPB_MEM_LOW_VECTOR_GRANULARITY_2MB 0x1
#endif
#ifndef FPB_MEM_LOW_VECTOR_GRANULARITY_4MB
#define FPB_MEM_LOW_VECTOR_GRANULARITY_4MB 0x2
#endif
#ifndef FPB_MEM_LOW_VECTOR_GRANULARITY_8MB
#define FPB_MEM_LOW_VECTOR_GRANULARITY_8MB 0x3
#endif
#ifndef FPB_MEM_VECTOR_GRANULARITY_1B
#define FPB_MEM_VECTOR_GRANULARITY_1B 0x8
#endif
#ifndef FPB_RID_VECTOR_GRANULARITY_256RIDS
#define FPB_RID_VECTOR_GRANULARITY_256RIDS 0x5
#endif
#ifndef FPB_RID_VECTOR_GRANULARITY_64RIDS
#define FPB_RID_VECTOR_GRANULARITY_64RIDS 0x3
#endif
#ifndef FPB_RID_VECTOR_GRANULARITY_8RIDS
#define FPB_RID_VECTOR_GRANULARITY_8RIDS 0x0
#endif
#ifndef FPB_VECTOR_SELECT_MEM_HIGH
#define FPB_VECTOR_SELECT_MEM_HIGH 0x2
#endif
#ifndef FPB_VECTOR_SELECT_MEM_LOW
#define FPB_VECTOR_SELECT_MEM_LOW 0x1
#endif
#ifndef FPB_VECTOR_SELECT_RID
#define FPB_VECTOR_SELECT_RID 0x0
#endif
#ifndef FPB_VECTOR_SIZE_SUPPORTED_1KBITS
#define FPB_VECTOR_SIZE_SUPPORTED_1KBITS 0x2
#endif
#ifndef FPB_VECTOR_SIZE_SUPPORTED_256BITS
#define FPB_VECTOR_SIZE_SUPPORTED_256BITS 0x0
#endif
#ifndef FPB_VECTOR_SIZE_SUPPORTED_2KBITS
#define FPB_VECTOR_SIZE_SUPPORTED_2KBITS 0x3
#endif
#ifndef FPB_VECTOR_SIZE_SUPPORTED_4KBITS
#define FPB_VECTOR_SIZE_SUPPORTED_4KBITS 0x4
#endif
#ifndef FPB_VECTOR_SIZE_SUPPORTED_512BITS
#define FPB_VECTOR_SIZE_SUPPORTED_512BITS 0x1
#endif
#ifndef FPB_VECTOR_SIZE_SUPPORTED_8KBITS
#define FPB_VECTOR_SIZE_SUPPORTED_8KBITS 0x5
#endif
#ifndef PCI_ACS_ALLOWED
#define PCI_ACS_ALLOWED 0
#endif
#ifndef PCI_ACS_BLOCKED
#define PCI_ACS_BLOCKED 1
#endif
#ifndef PCI_ACS_REDIRECTED
#define PCI_ACS_REDIRECTED 2
#endif
#ifndef PCI_CAPABILITY_ID_ADVANCED_FEATURES
#define PCI_CAPABILITY_ID_ADVANCED_FEATURES 0x13
#endif
#ifndef PCI_CAPABILITY_ID_FPB
#define PCI_CAPABILITY_ID_FPB 0x15
#endif
#ifndef PCI_CAPABILITY_ID_SATA_CONFIG
#define PCI_CAPABILITY_ID_SATA_CONFIG 0x12
#endif
#ifndef PCI_EXPRESS_ACCESS_CONTROL_SERVICES_CAP_ID
#define PCI_EXPRESS_ACCESS_CONTROL_SERVICES_CAP_ID 0x000D
#endif
#ifndef PCI_EXPRESS_ARI_CAP_ID
#define PCI_EXPRESS_ARI_CAP_ID 0x000E
#endif
#ifndef PCI_EXPRESS_ATS_CAP_ID
#define PCI_EXPRESS_ATS_CAP_ID 0x000F
#endif
#ifndef PCI_EXPRESS_CONFIGURATION_ACCESS_CORRELATION_CAP_ID
#define PCI_EXPRESS_CONFIGURATION_ACCESS_CORRELATION_CAP_ID 0x000C
#endif
#ifndef PCI_EXPRESS_DESIGNATED_VENDOR_SPECIFIC_CAP_ID
#define PCI_EXPRESS_DESIGNATED_VENDOR_SPECIFIC_CAP_ID 0x0023
#endif
#ifndef PCI_EXPRESS_DEVICE_3_CAP_ID
#define PCI_EXPRESS_DEVICE_3_CAP_ID 0x002F
#endif
#ifndef PCI_EXPRESS_DPA_CAP_ID
#define PCI_EXPRESS_DPA_CAP_ID 0x0016
#endif
#ifndef PCI_EXPRESS_DPC_CAP_ID
#define PCI_EXPRESS_DPC_CAP_ID 0x001D
#endif
#ifndef PCI_EXPRESS_FRS_QUEUEING_CAP_ID
#define PCI_EXPRESS_FRS_QUEUEING_CAP_ID 0x0021
#endif
#ifndef PCI_EXPRESS_L1_PM_SS_CAP_ID
#define PCI_EXPRESS_L1_PM_SS_CAP_ID 0x001E
#endif
#ifndef PCI_EXPRESS_LN_REQUESTER_CAP_ID
#define PCI_EXPRESS_LN_REQUESTER_CAP_ID 0x001C
#endif
#ifndef PCI_EXPRESS_LTR_CAP_ID
#define PCI_EXPRESS_LTR_CAP_ID 0x0018
#endif
#ifndef PCI_EXPRESS_MPCIE_CAP_ID
#define PCI_EXPRESS_MPCIE_CAP_ID 0x0020
#endif
#ifndef PCI_EXPRESS_MULTICAST_CAP_ID
#define PCI_EXPRESS_MULTICAST_CAP_ID 0x0012
#endif
#ifndef PCI_EXPRESS_MULTI_ROOT_IO_VIRTUALIZATION_CAP_ID
#define PCI_EXPRESS_MULTI_ROOT_IO_VIRTUALIZATION_CAP_ID 0x0011
#endif
#ifndef PCI_EXPRESS_NPEM_CAP_ID
#define PCI_EXPRESS_NPEM_CAP_ID 0x0029
#endif
#ifndef PCI_EXPRESS_PAGE_REQUEST_CAP_ID
#define PCI_EXPRESS_PAGE_REQUEST_CAP_ID 0x0013
#endif
#ifndef PCI_EXPRESS_PASID_CAP_ID
#define PCI_EXPRESS_PASID_CAP_ID 0x001B
#endif
#ifndef PCI_EXPRESS_PMUX_CAP_ID
#define PCI_EXPRESS_PMUX_CAP_ID 0x001A
#endif
#ifndef PCI_EXPRESS_PTM_CAP_ID
#define PCI_EXPRESS_PTM_CAP_ID 0x001F
#endif
#ifndef PCI_EXPRESS_READINESS_TIME_REPORTING_CAP_ID
#define PCI_EXPRESS_READINESS_TIME_REPORTING_CAP_ID 0x0022
#endif
#ifndef PCI_EXPRESS_RESERVED_FOR_AMD_CAP_ID
#define PCI_EXPRESS_RESERVED_FOR_AMD_CAP_ID 0x0014
#endif
#ifndef PCI_EXPRESS_RESIZABLE_BAR_CAP_ID
#define PCI_EXPRESS_RESIZABLE_BAR_CAP_ID 0x0015
#endif
#ifndef PCI_EXPRESS_SECONDARY_PCI_EXPRESS_CAP_ID
#define PCI_EXPRESS_SECONDARY_PCI_EXPRESS_CAP_ID 0x0019
#endif
#ifndef PCI_EXPRESS_TPH_REQUESTER_CAP_ID
#define PCI_EXPRESS_TPH_REQUESTER_CAP_ID 0x0017
#endif
#ifndef PCI_EXPRESS_VENDOR_SPECIFIC_CAP_ID
#define PCI_EXPRESS_VENDOR_SPECIFIC_CAP_ID 0x000B
#endif
#ifndef PCI_MAX_SEGMENT_NUMBER
#define PCI_MAX_SEGMENT_NUMBER 0xFFFF
#endif
#ifndef PCI_STATUS_IMMEDIATE_READINESS
#define PCI_STATUS_IMMEDIATE_READINESS 0x0001
#endif
#ifndef PCI_SUBCLASS_SB_THUNDERBOLT
#define PCI_SUBCLASS_SB_THUNDERBOLT 0x0A
#endif
#ifndef PCI_SUBCLASS_SYS_RCEC
#define PCI_SUBCLASS_SYS_RCEC 0x07
#endif
#ifndef PCI_SUBLCASS_VID_3D_CTLR
#define PCI_SUBLCASS_VID_3D_CTLR 0x02
#endif
#ifndef PCI_VIRTUAL_RESOURCE_COUNT
#define PCI_VIRTUAL_RESOURCE_COUNT 0x8
#endif

typedef enum _PCI_OSC_CONTROL_BITS {
    PciOscControlBitExpressNativeHotPlug = 1,
    PciOscControlBitShpcNativeHotPlug = 2,
    PciOscControlBitExpressNativePME = 4,
    PciOscControlBitExpressAdvancedErrorReporting = 8,
    PciOscControlBitExpressCapabilityStructure = 16,
    PciOscControlBitLatencyToleranceReporting = 32,
    PciOscControlBitAllOnesMmioInvalidToDrivers = 64,
    PciOscControlBitDownstreamPortContainment = 128,
    PciOscControlBitCompletionTimeout = 0x100,
    PciOscControlBitFirmwareIntermediaryConfig = 0x200
} PCI_OSC_CONTROL_BITS, *PPCI_OSC_CONTROL_BITS;

typedef enum PCI_DEVICE_D3COLD_STATE_REASON {
    PciDeviceD3Cold_State_Disabled_BitIndex = 1,
    PciDeviceD3Cold_State_Enabled_BitIndex = 2,
    PciDeviceD3Cold_State_ParentRootPortS0WakeSupported_BitIndex = 3,
    PciDeviceD3Cold_State_Disabled_Bridge_HackFlags_BitIndex = 4,
    PciDeviceD3Cold_Reason_Default_State_BitIndex = 8,
    PciDeviceD3Cold_Reason_INF_BitIndex = 9,
    PciDeviceD3Cold_Reason_Interface_Api_BitIndex = 10
} PCI_DEVICE_D3COLD_STATE_REASON;

typedef enum PCI_EXPRESS_LINK_SUBSTATE {
    PciExpressPciPmLinkSubState_L11_BitIndex = 0,
    PciExpressPciPmLinkSubState_L12_BitIndex = 1,
    PciExpressASPMLinkSubState_L11_BitIndex = 2,
    PciExpressASPMLinkSubState_L12_BitIndex = 3
} PCI_EXPRESS_LINK_SUBSTATE;

typedef enum PCI_EXPRESS_RCB {
    RCB64Bytes = 0,
    RCB128Bytes = 1
} PCI_EXPRESS_RCB;

typedef const PCSTR * PCZPCSTR;
typedef const PCWSTR * PCZPCWSTR;
typedef enum _PROCESSOR_CACHE_TYPE * PPROCESSOR_CACHE_TYPE;
typedef LONG_PTR * PRTL_REFERENCE_COUNT;
typedef LONG * PRTL_REFERENCE_COUNT32;
typedef LONG_PTR RTL_REFERENCE_COUNT;
typedef LONG RTL_REFERENCE_COUNT32;

typedef struct _PCI_EXPRESS_ARI_CAPABILITY_REGISTER {
  USHORT MfvcFunctionGroupsCapability : 1;
  USHORT AcsFunctionGroupsCapability : 1;
  USHORT Reserved : 6;
  USHORT NextFunctionNumber : 8;
} PCI_EXPRESS_ARI_CAPABILITY_REGISTER, *PPCI_EXPRESS_ARI_CAPABILITY_REGISTER;

typedef struct _PCI_EXPRESS_ARI_CONTROL_REGISTER {
  USHORT MfvcFunctionGroupsEnable : 1;
  USHORT AcsFunctionGroupsEnable : 1;
  USHORT Reserved1 : 2;
  USHORT FunctionGroup : 3;
  USHORT Reserved2 : 9;
} PCI_EXPRESS_ARI_CONTROL_REGISTER, *PPCI_EXPRESS_ARI_CONTROL_REGISTER;

typedef struct _PCI_EXPRESS_ARI_CAPABILITY {
  PCI_EXPRESS_ENHANCED_CAPABILITY_HEADER Header;
  PCI_EXPRESS_ARI_CAPABILITY_REGISTER Capability;
  PCI_EXPRESS_ARI_CONTROL_REGISTER Control;
} PCI_EXPRESS_ARI_CAPABILITY, *PPCI_EXPRESS_ARI_CAPABILITY;

typedef struct _PCI_EXPRESS_ATS_CAPABILITY_REGISTER {
  USHORT InvalidateQueueDepth : 5;
  USHORT PageAlignedRequest : 1;
  USHORT GlobalInvalidateSupported : 1;
  USHORT RelaxedOrderingSupported : 1;
  USHORT Reserved : 8;
} PCI_EXPRESS_ATS_CAPABILITY_REGISTER, *PPCI_EXPRESS_ATS_CAPABILITY_REGISTER;

typedef struct _PCI_EXPRESS_VENDOR_SPECIFIC_CAPABILITY {
  PCI_EXPRESS_ENHANCED_CAPABILITY_HEADER Header;
  USHORT VsecId;
  USHORT VsecRev : 4;
  USHORT VsecLength : 12;
} PCI_EXPRESS_VENDOR_SPECIFIC_CAPABILITY, *PPCI_EXPRESS_VENDOR_SPECIFIC_CAPABILITY;

typedef struct _PCI_FPB_CAPABILITY_HEADER {
  PCI_CAPABILITIES_HEADER Header;
  USHORT Reserved;
} PCI_FPB_CAPABILITY_HEADER, *PPCI_FPB_CAPABILITY_HEADER;

typedef struct _PCI_FPB_MEM_HIGH_VECTOR_CONTROL2_REGISTER {
  ULONG MemHighVectorStartUpper;
} PCI_FPB_MEM_HIGH_VECTOR_CONTROL2_REGISTER, *PPCI_FPB_MEM_HIGH_VECTOR_CONTROL2_REGISTER;

typedef struct _PCI_FPB_VECTOR_ACCESS_DATA_REGISTER {
  ULONG VectorAccessData;
} PCI_FPB_VECTOR_ACCESS_DATA_REGISTER, *PPCI_FPB_VECTOR_ACCESS_DATA_REGISTER;

typedef struct _PCI_VENDOR_SPECIFIC_CAPABILITY {
  PCI_CAPABILITIES_HEADER Header;
  UCHAR VscLength;
  UCHAR VendorSpecific;
} PCI_VENDOR_SPECIFIC_CAPABILITY, *PPCI_VENDOR_SPECIFIC_CAPABILITY;

#define _DBGRAISEASSERTIONFAILURE_ 0
#define ASAN_WARNING_DISABLE_4714_POP
#define ASAN_WARNING_DISABLE_4714_PUSH
#define CFORCEINLINE FORCEINLINE
#define CpuIdEx __cpuidex
#define DECLSPEC_CHPE_GUEST
#define DECLSPEC_CHPE_PATCHABLE DECLSPEC_NOINLINE
#define DECLSPEC_NOSANITIZEADDRESS
#define DECLSPEC_RESTRICT __declspec(restrict)
#define DEPRECATED_NO_MESSAGE_STDAPI EXTERN_C __declspec(deprecated) HRESULT STDAPICALLTYPE
#define DEPRECATED_NO_MESSAGE_STDAPIV EXTERN_C __declspec(deprecated) HRESULT STDAPIVCALLTYPE
#define DUMMYSTRUCTNAME6
#define GetSegmentLimit __segmentlimit
#define InterlockedCompareExchangeAcquire128 _InterlockedCompareExchange128
#define InterlockedCompareExchangeNoFence128 _InterlockedCompareExchange128
#define InterlockedCompareExchangeRelease128 _InterlockedCompareExchange128
#define InterlockedExchangeAcquire8 InterlockedExchange8
#define InterlockedExchangeAdd16 _InterlockedExchangeAdd16
#define InterlockedExchangeAdd8 _InterlockedExchangeAdd8
#define InterlockedExchangeNoFence8 InterlockedExchange8
#define Multiply128 _mul128
#define PFORCEINLINE FORCEINLINE
#define PopulationCount64 __popcnt64
#define ReadMxCsr _mm_getcsr
#define ReadPMC __readpmc
#define ShiftLeft128 __shiftleft128
#define ShiftRight128 __shiftright128
#define SpeculationFence LoadFence
#define STDAPI_CHPE_PATCHABLE EXTERN_C DECLSPEC_CHPE_PATCHABLE HRESULT STDAPICALLTYPE
#define STKFORCEINLINE FORCEINLINE
#define WIN_NOEXCEPT
#define WIN_NOEXCEPT_PFN
#define WriteMxCsr _mm_setcsr

struct _PCI_ADVANCED_FEATURES_CAPABILITY
{
    PCI_CAPABILITIES_HEADER Header;
    UCHAR Length;
    union
    {
        struct
        {
            UCHAR FunctionLevelResetSupported : 1;
            UCHAR TransactionsPendingSupported : 1;
            UCHAR Rsvd : 6;
        };
        UCHAR AsUCHAR;
    } Capabilities;
    union
    {
        struct
        {
            UCHAR InitiateFunctionLevelReset : 1;
            UCHAR Rsvd : 7;
        };
        UCHAR AsUCHAR;
    } Control;
    union
    {
        struct
        {
            UCHAR TransactionsPending : 1;
            UCHAR Rsvd : 7;
        };
        UCHAR AsUCHAR;
    } Status;
};

union _PCI_EXPRESS_ACS_CAPABILITY_REGISTER
{
    struct
    {
        USHORT SourceValidation : 1;
        USHORT TranslationBlocking : 1;
        USHORT RequestRedirect : 1;
        USHORT CompletionRedirect : 1;
        USHORT UpstreamForwarding : 1;
        USHORT EgressControl : 1;
        USHORT DirectTranslation : 1;
        USHORT EnhancedCapability : 1;
        USHORT EgressControlVectorSize : 8;
    };
    USHORT AsUSHORT;
};

union _PCI_EXPRESS_ACS_CONTROL
{
    struct
    {
        USHORT SourceValidation : 1;
        USHORT TranslationBlocking : 1;
        USHORT RequestRedirect : 1;
        USHORT CompletionRedirect : 1;
        USHORT UpstreamForwarding : 1;
        USHORT EgressControl : 1;
        USHORT DirectTranslation : 1;
        USHORT IoBlocking : 1;
        USHORT DspMemoryControl : 2;
        USHORT UspMemoryControl : 2;
        USHORT UnclaimedRedirect : 1;
        USHORT Reserved : 3;
    };
    USHORT AsUSHORT;
};

union _PCI_EXPRESS_ATS_CONTROL_REGISTER
{
    struct
    {
        USHORT SmallestTransactionUnit : 5;
        USHORT Reserved : 10;
        USHORT Enable : 1;
    };
    USHORT AsUSHORT;
};

union _PCI_EXPRESS_DEVICE_CAPABILITIES_3_REGISTER
{
    struct
    {
        ULONG DmwrRequestRoutingSupported : 1;
        ULONG Rsvd : 31;
    };
    ULONG AsULONG;
};

union _PCI_EXPRESS_DEVICE_CONTROL_3_REGISTER
{
    struct
    {
        ULONG DmwrRequesterEnable : 1;
        ULONG Rsvd : 31;
    };
    ULONG AsULONG;
};

union _PCI_EXPRESS_PASID_CAPABILITY_REGISTER
{
    struct
    {
        USHORT Rsvd : 1;
        USHORT ExecutePermissionSupported : 1;
        USHORT PrivilegedModeSupported : 1;
        USHORT Rsvd2 : 5;
        USHORT MaxPASIDWidth : 5;
        USHORT Rsvd3 : 3;
    };
    USHORT AsUSHORT;
};

union _PCI_EXPRESS_PASID_CONTROL_REGISTER
{
    struct
    {
        USHORT PASIDEnable : 1;
        USHORT ExecutePermissionEnable : 1;
        USHORT PrivilegedModeEnable : 1;
        USHORT Rsvd : 13;
    };
    USHORT AsUSHORT;
};

union _PCI_EXPRESS_PRI_CONTROL_REGISTER
{
    struct
    {
        USHORT Enable : 1;
        USHORT Reset : 1;
        USHORT Rsvd : 14;
    };
    USHORT AsUSHORT;
};

union _PCI_EXPRESS_PRI_STATUS_REGISTER
{
    struct
    {
        USHORT ResponseFailure : 1;
        USHORT UnexpectedPageRequestGroupIndex : 1;
        USHORT Rsvd : 6;
        USHORT Stopped : 1;
        USHORT Rsvd2 : 6;
        USHORT PrgResponsePasidRequired : 1;
    };
    USHORT AsUSHORT;
};

union _PCI_EXPRESS_PTM_CAPABILITY_REGISTER
{
    struct
    {
        ULONG RequesterCapable : 1;
        ULONG ResponderCapable : 1;
        ULONG RootCapable : 1;
        ULONG Rsvd : 5;
        ULONG LocalGranularity : 8;
        ULONG Rsvd2 : 16;
    };
    ULONG AsULONG;
};

union _PCI_EXPRESS_PTM_CONTROL_REGISTER
{
    struct
    {
        ULONG Enable : 1;
        ULONG RootSelect : 1;
        ULONG Rsvd : 6;
        ULONG EffectiveGranularity : 8;
        ULONG Rsvd2 : 16;
    };
    ULONG AsULONG;
};

struct _PCI_FIRMWARE_BUS_CAPS
{
    USHORT Type;
    USHORT Length;
    struct
    {
        UCHAR SixtyFourBitDevice : 1;
        UCHAR PciXMode1EccCapable : 1;
        UCHAR DeviceIdMessagingCapable : 1;
        UCHAR ObffWakeSignalCapable : 1;
        UCHAR Reserved1 : 4;
    };
    UCHAR CurrentSpeedAndMode;
    UCHAR SupportedSpeedsAndModesLowByte;
    UCHAR SupportedSpeedsAndModesHighByte;
    UCHAR Voltage;
    UCHAR Reserved2[7];
};

union _PCI_FPB_CAPABILITIES_REGISTER
{
    struct
    {
        ULONG RidDecodeMechanismSupported : 1;
        ULONG MemLowDecodeMechanismSupported : 1;
        ULONG MemHighDecodeMechanismSupported : 1;
        ULONG NumSecDev : 5;
        ULONG RidVectorSizeSupported : 3;
        ULONG Rsvd0 : 5;
        ULONG MemLowVectorSizeSupported : 3;
        ULONG Rsvd1 : 5;
        ULONG MemHighVectorSizeSupported : 3;
        ULONG Rsvd2 : 5;
    };
    ULONG AsULONG;
};

union _PCI_FPB_MEM_HIGH_VECTOR_CONTROL1_REGISTER
{
    struct
    {
        ULONG MemHighDecodeMechanismEnable : 1;
        ULONG Rsvd0 : 3;
        ULONG MemHighVectorGranularity : 4;
        ULONG Rsvd1 : 20;
        ULONG MemHighVectorStartLower : 4;
    };
    ULONG AsULONG;
};

union _PCI_FPB_MEM_LOW_VECTOR_CONTROL_REGISTER
{
    struct
    {
        ULONG MemLowDecodeMechanismEnable : 1;
        ULONG Rsvd0 : 3;
        ULONG MemLowVectorGranularity : 4;
        ULONG Rsvd1 : 12;
        ULONG MemLowVectorStart : 12;
    };
    ULONG AsULONG;
};

union _PCI_FPB_RID_VECTOR_CONTROL1_REGISTER
{
    struct
    {
        ULONG RidDecodeMechanismEnable : 1;
        ULONG Rsvd0 : 3;
        ULONG RidVectorGranularity : 4;
        ULONG Rsvd1 : 11;
        ULONG RidVectorStart : 13;
    };
    ULONG AsULONG;
};

union _PCI_FPB_RID_VECTOR_CONTROL2_REGISTER
{
    struct
    {
        ULONG Rsvd0 : 3;
        ULONG RidSecondaryStart : 13;
        ULONG Rsvd1 : 16;
    };
    ULONG AsULONG;
};

union _PCI_FPB_VECTOR_ACCESS_CONTROL_REGISTER
{
    struct
    {
        ULONG VectorAccessOffset : 8;
        ULONG Rsvd0 : 6;
        ULONG VectorSelect : 2;
        ULONG Rsvd1 : 16;
    };
    ULONG AsULONG;
};

struct _PCI_SEGMENT_BUS_NUMBER
{
    union
    {
        struct
        {
            ULONG BusNumber : 8;
            ULONG SegmentNumber : 16;
            ULONG Reserved : 8;
        } bits;
        ULONG AsULONG;
    } u;
};

union _VIRTUAL_CHANNEL_CAPABILITIES1
{
    struct
    {
        ULONG ExtendedVCCount : 3;
        ULONG RsvdP1 : 1;
        ULONG LowPriorityExtendedVCCount : 3;
        ULONG RsvdP2 : 1;
        ULONG ReferenceClock : 2;
        ULONG PortArbitrationTableEntrySize : 2;
        ULONG RsvdP3 : 20;
    };
    ULONG AsULONG;
};

union _VIRTUAL_CHANNEL_CAPABILITIES2
{
    struct
    {
        ULONG VCArbitrationCapability : 8;
        ULONG RsvdP : 16;
        ULONG VCArbitrationTableOffset : 8;
    };
    ULONG AsULONG;
};

union _VIRTUAL_CHANNEL_CONTROL
{
    struct
    {
        USHORT LoadVCArbitrationTable : 1;
        USHORT VCArbitrationSelect : 3;
        USHORT RsvdP : 12;
    };
    USHORT AsUSHORT;
};

union _VIRTUAL_CHANNEL_STATUS
{
    struct
    {
        USHORT VCArbitrationTableStatus : 1;
        USHORT RsvdZ : 15;
    };
    USHORT AsUSHORT;
};

union _VIRTUAL_RESOURCE_CAPABILITY
{
    struct
    {
        ULONG PortArbitrationCapability : 8;
        ULONG RsvdP1 : 6;
        ULONG Undefined : 1;
        ULONG RejectSnoopTransactions : 1;
        ULONG MaximumTimeSlots : 7;
        ULONG RsvdP2 : 1;
        ULONG PortArbitrationTableOffset : 8;
    };
    ULONG AsULONG;
};

union _VIRTUAL_RESOURCE_CONTROL
{
    struct
    {
        ULONG TcVcMap : 8;
        ULONG RsvdP1 : 8;
        ULONG LoadPortArbitrationTable : 1;
        ULONG PortArbitrationSelect : 3;
        ULONG RsvdP2 : 4;
        ULONG VcID : 3;
        ULONG RsvdP3 : 4;
        ULONG VcEnable : 1;
    };
    ULONG AsULONG;
};

union _VIRTUAL_RESOURCE_STATUS
{
    struct
    {
        USHORT PortArbitrationTableStatus : 1;
        USHORT VcNegotiationPending : 1;
        USHORT RsvdZ : 14;
    };
    USHORT AsUSHORT;
};

typedef struct _PCI_ADVANCED_FEATURES_CAPABILITY PCI_ADVANCED_FEATURES_CAPABILITY;

typedef union _PCI_EXPRESS_ACS_CAPABILITY_REGISTER PCI_EXPRESS_ACS_CAPABILITY_REGISTER;

typedef union _PCI_EXPRESS_ACS_CONTROL PCI_EXPRESS_ACS_CONTROL;

struct _PCI_EXPRESS_ACS_CAPABILITY
{
    PCI_EXPRESS_ENHANCED_CAPABILITY_HEADER Header;
    PCI_EXPRESS_ACS_CAPABILITY_REGISTER Capability;
    PCI_EXPRESS_ACS_CONTROL Control;
    ULONG EgressControl[1];
};

typedef struct _PCI_EXPRESS_ACS_CAPABILITY PCI_EXPRESS_ACS_CAPABILITY;

typedef union _PCI_EXPRESS_ATS_CONTROL_REGISTER PCI_EXPRESS_ATS_CONTROL_REGISTER;

struct _PCI_EXPRESS_ATS_CAPABILITY
{
    PCI_EXPRESS_ENHANCED_CAPABILITY_HEADER Header;
    PCI_EXPRESS_ATS_CAPABILITY_REGISTER Capability;
    PCI_EXPRESS_ATS_CONTROL_REGISTER Control;
};

typedef struct _PCI_EXPRESS_ATS_CAPABILITY PCI_EXPRESS_ATS_CAPABILITY;

typedef union _PCI_EXPRESS_DEVICE_CAPABILITIES_3_REGISTER PCI_EXPRESS_DEVICE_CAPABILITIES_3_REGISTER;

typedef union _PCI_EXPRESS_DEVICE_CONTROL_3_REGISTER PCI_EXPRESS_DEVICE_CONTROL_3_REGISTER;

struct _PCI_EXPRESS_DEVICE_3_EXTENDED_CAPABILITY
{
    PCI_EXPRESS_ENHANCED_CAPABILITY_HEADER Header;
    PCI_EXPRESS_DEVICE_CAPABILITIES_3_REGISTER Capability;
    PCI_EXPRESS_DEVICE_CONTROL_3_REGISTER Control;
};

typedef struct _PCI_EXPRESS_DEVICE_3_EXTENDED_CAPABILITY PCI_EXPRESS_DEVICE_3_EXTENDED_CAPABILITY;

typedef union _PCI_EXPRESS_PASID_CAPABILITY_REGISTER PCI_EXPRESS_PASID_CAPABILITY_REGISTER;

typedef union _PCI_EXPRESS_PASID_CONTROL_REGISTER PCI_EXPRESS_PASID_CONTROL_REGISTER;

struct _PCI_EXPRESS_PASID_CAPABILITY
{
    PCI_EXPRESS_ENHANCED_CAPABILITY_HEADER Header;
    PCI_EXPRESS_PASID_CAPABILITY_REGISTER Capability;
    PCI_EXPRESS_PASID_CONTROL_REGISTER Control;
};

typedef struct _PCI_EXPRESS_PASID_CAPABILITY PCI_EXPRESS_PASID_CAPABILITY;

typedef union _PCI_EXPRESS_PRI_CONTROL_REGISTER PCI_EXPRESS_PRI_CONTROL_REGISTER;

typedef union _PCI_EXPRESS_PRI_STATUS_REGISTER PCI_EXPRESS_PRI_STATUS_REGISTER;

struct _PCI_EXPRESS_PRI_CAPABILITY
{
    PCI_EXPRESS_ENHANCED_CAPABILITY_HEADER Header;
    PCI_EXPRESS_PRI_CONTROL_REGISTER Control;
    PCI_EXPRESS_PRI_STATUS_REGISTER Status;
    ULONG PRCapacity;
    ULONG PRAllocation;
};

typedef struct _PCI_EXPRESS_PRI_CAPABILITY PCI_EXPRESS_PRI_CAPABILITY;

typedef union _PCI_EXPRESS_PTM_CAPABILITY_REGISTER PCI_EXPRESS_PTM_CAPABILITY_REGISTER;

typedef union _PCI_EXPRESS_PTM_CONTROL_REGISTER PCI_EXPRESS_PTM_CONTROL_REGISTER;

struct _PCI_EXPRESS_PTM_CAPABILITY
{
    PCI_EXPRESS_ENHANCED_CAPABILITY_HEADER Header;
    PCI_EXPRESS_PTM_CAPABILITY_REGISTER PtmCapability;
    PCI_EXPRESS_PTM_CONTROL_REGISTER PtmControl;
};

typedef struct _PCI_EXPRESS_PTM_CAPABILITY PCI_EXPRESS_PTM_CAPABILITY;

typedef struct _PCI_FIRMWARE_BUS_CAPS PCI_FIRMWARE_BUS_CAPS;

struct _PCI_FIRMWARE_BUS_CAPS_RETURN_BUFFER
{
    USHORT Version;
    USHORT Status;
    ULONG Length;
    PCI_FIRMWARE_BUS_CAPS Caps;
};

typedef struct _PCI_FIRMWARE_BUS_CAPS_RETURN_BUFFER PCI_FIRMWARE_BUS_CAPS_RETURN_BUFFER;

typedef union _PCI_FPB_CAPABILITIES_REGISTER PCI_FPB_CAPABILITIES_REGISTER;

typedef union _PCI_FPB_MEM_HIGH_VECTOR_CONTROL1_REGISTER PCI_FPB_MEM_HIGH_VECTOR_CONTROL1_REGISTER;

typedef union _PCI_FPB_MEM_LOW_VECTOR_CONTROL_REGISTER PCI_FPB_MEM_LOW_VECTOR_CONTROL_REGISTER;

typedef union _PCI_FPB_RID_VECTOR_CONTROL1_REGISTER PCI_FPB_RID_VECTOR_CONTROL1_REGISTER;

typedef union _PCI_FPB_RID_VECTOR_CONTROL2_REGISTER PCI_FPB_RID_VECTOR_CONTROL2_REGISTER;

typedef union _PCI_FPB_VECTOR_ACCESS_CONTROL_REGISTER PCI_FPB_VECTOR_ACCESS_CONTROL_REGISTER;

struct _PCI_FPB_CAPABILITY
{
    PCI_FPB_CAPABILITY_HEADER Header;
    PCI_FPB_CAPABILITIES_REGISTER CapabilitiesRegister;
    PCI_FPB_RID_VECTOR_CONTROL1_REGISTER RidVectorControl1Register;
    PCI_FPB_RID_VECTOR_CONTROL2_REGISTER RidVectorControl2Register;
    PCI_FPB_MEM_LOW_VECTOR_CONTROL_REGISTER MemLowVectorControlRegister;
    PCI_FPB_MEM_HIGH_VECTOR_CONTROL1_REGISTER MemHighVectorControl1Register;
    PCI_FPB_MEM_HIGH_VECTOR_CONTROL2_REGISTER MemHighVectorControl2Register;
    PCI_FPB_VECTOR_ACCESS_CONTROL_REGISTER VectorAccessControlRegister;
    PCI_FPB_VECTOR_ACCESS_DATA_REGISTER VectorAccessDataRegister;
};

typedef struct _PCI_FPB_CAPABILITY PCI_FPB_CAPABILITY;

typedef struct _PCI_SEGMENT_BUS_NUMBER PCI_SEGMENT_BUS_NUMBER;

typedef struct _PCI_ADVANCED_FEATURES_CAPABILITY *PPCI_ADVANCED_FEATURES_CAPABILITY;

typedef struct _PCI_EXPRESS_ACS_CAPABILITY *PPCI_EXPRESS_ACS_CAPABILITY;

typedef union _PCI_EXPRESS_ACS_CAPABILITY_REGISTER *PPCI_EXPRESS_ACS_CAPABILITY_REGISTER;

typedef union _PCI_EXPRESS_ACS_CONTROL *PPCI_EXPRESS_ACS_CONTROL;

typedef struct _PCI_EXPRESS_ATS_CAPABILITY *PPCI_EXPRESS_ATS_CAPABILITY;

typedef union _PCI_EXPRESS_ATS_CONTROL_REGISTER *PPCI_EXPRESS_ATS_CONTROL_REGISTER;

typedef struct _PCI_EXPRESS_DEVICE_3_EXTENDED_CAPABILITY *PPCI_EXPRESS_DEVICE_3_EXTENDED_CAPABILITY;

typedef union _PCI_EXPRESS_DEVICE_CAPABILITIES_3_REGISTER *PPCI_EXPRESS_DEVICE_CAPABILITIES_3_REGISTER;

typedef union _PCI_EXPRESS_DEVICE_CONTROL_3_REGISTER *PPCI_EXPRESS_DEVICE_CONTROL_3_REGISTER;

typedef struct _PCI_EXPRESS_PASID_CAPABILITY *PPCI_EXPRESS_PASID_CAPABILITY;

typedef union _PCI_EXPRESS_PASID_CAPABILITY_REGISTER *PPCI_EXPRESS_PASID_CAPABILITY_REGISTER;

typedef union _PCI_EXPRESS_PASID_CONTROL_REGISTER *PPCI_EXPRESS_PASID_CONTROL_REGISTER;

typedef struct _PCI_EXPRESS_PRI_CAPABILITY *PPCI_EXPRESS_PRI_CAPABILITY;

typedef union _PCI_EXPRESS_PRI_CONTROL_REGISTER *PPCI_EXPRESS_PRI_CONTROL_REGISTER;

typedef union _PCI_EXPRESS_PRI_STATUS_REGISTER *PPCI_EXPRESS_PRI_STATUS_REGISTER;

typedef struct _PCI_EXPRESS_PTM_CAPABILITY *PPCI_EXPRESS_PTM_CAPABILITY;

typedef union _PCI_EXPRESS_PTM_CAPABILITY_REGISTER *PPCI_EXPRESS_PTM_CAPABILITY_REGISTER;

typedef union _PCI_EXPRESS_PTM_CONTROL_REGISTER *PPCI_EXPRESS_PTM_CONTROL_REGISTER;

typedef struct _PCI_FIRMWARE_BUS_CAPS *PPCI_FIRMWARE_BUS_CAPS;

typedef struct _PCI_FIRMWARE_BUS_CAPS_RETURN_BUFFER *PPCI_FIRMWARE_BUS_CAPS_RETURN_BUFFER;

typedef union _PCI_FPB_CAPABILITIES_REGISTER *PPCI_FPB_CAPABILITIES_REGISTER;

typedef struct _PCI_FPB_CAPABILITY *PPCI_FPB_CAPABILITY;

typedef union _PCI_FPB_MEM_HIGH_VECTOR_CONTROL1_REGISTER *PPCI_FPB_MEM_HIGH_VECTOR_CONTROL1_REGISTER;

typedef union _PCI_FPB_MEM_LOW_VECTOR_CONTROL_REGISTER *PPCI_FPB_MEM_LOW_VECTOR_CONTROL_REGISTER;

typedef union _PCI_FPB_RID_VECTOR_CONTROL1_REGISTER *PPCI_FPB_RID_VECTOR_CONTROL1_REGISTER;

typedef union _PCI_FPB_RID_VECTOR_CONTROL2_REGISTER *PPCI_FPB_RID_VECTOR_CONTROL2_REGISTER;

typedef union _PCI_FPB_VECTOR_ACCESS_CONTROL_REGISTER *PPCI_FPB_VECTOR_ACCESS_CONTROL_REGISTER;

typedef struct _PCI_SEGMENT_BUS_NUMBER *PPCI_SEGMENT_BUS_NUMBER;

typedef union _VIRTUAL_CHANNEL_CAPABILITIES1 *PVIRTUAL_CHANNEL_CAPABILITIES1;

typedef union _VIRTUAL_CHANNEL_CAPABILITIES2 *PVIRTUAL_CHANNEL_CAPABILITIES2;

typedef union _VIRTUAL_CHANNEL_CONTROL *PVIRTUAL_CHANNEL_CONTROL;

typedef union _VIRTUAL_CHANNEL_STATUS *PVIRTUAL_CHANNEL_STATUS;

typedef union _VIRTUAL_RESOURCE_CAPABILITY *PVIRTUAL_RESOURCE_CAPABILITY;

typedef union _VIRTUAL_RESOURCE_CONTROL *PVIRTUAL_RESOURCE_CONTROL;

typedef union _VIRTUAL_RESOURCE_STATUS *PVIRTUAL_RESOURCE_STATUS;

typedef union _VIRTUAL_CHANNEL_CAPABILITIES1 VIRTUAL_CHANNEL_CAPABILITIES1;

typedef union _VIRTUAL_CHANNEL_CAPABILITIES2 VIRTUAL_CHANNEL_CAPABILITIES2;

typedef union _VIRTUAL_CHANNEL_CONTROL VIRTUAL_CHANNEL_CONTROL;

typedef union _VIRTUAL_CHANNEL_STATUS VIRTUAL_CHANNEL_STATUS;

typedef union _VIRTUAL_RESOURCE_CAPABILITY VIRTUAL_RESOURCE_CAPABILITY;

typedef union _VIRTUAL_RESOURCE_CONTROL VIRTUAL_RESOURCE_CONTROL;

typedef union _VIRTUAL_RESOURCE_STATUS VIRTUAL_RESOURCE_STATUS;

struct _VIRTUAL_RESOURCE
{
    VIRTUAL_RESOURCE_CAPABILITY Capability;
    VIRTUAL_RESOURCE_CONTROL Control;
    USHORT RsvdP;
    VIRTUAL_RESOURCE_STATUS Status;
};

typedef struct _VIRTUAL_RESOURCE *PVIRTUAL_RESOURCE;

typedef struct _VIRTUAL_RESOURCE VIRTUAL_RESOURCE;

struct _PCI_EXPRESS_VIRTUAL_CHANNEL_CAPABILITY
{
    PCI_EXPRESS_ENHANCED_CAPABILITY_HEADER Header;
    VIRTUAL_CHANNEL_CAPABILITIES1 Capabilities1;
    VIRTUAL_CHANNEL_CAPABILITIES2 Capabilities2;
    VIRTUAL_CHANNEL_CONTROL Control;
    VIRTUAL_CHANNEL_STATUS Status;
    VIRTUAL_RESOURCE Resource[8];
};

typedef struct _PCI_EXPRESS_VIRTUAL_CHANNEL_CAPABILITY PCI_EXPRESS_VIRTUAL_CHANNEL_CAPABILITY;

typedef struct _PCI_EXPRESS_VIRTUAL_CHANNEL_CAPABILITY *PPCI_EXPRESS_VIRTUAL_CHANNEL_CAPABILITY;

#ifdef __cplusplus
extern "C" {
#endif

/* TODO: LiberNT does not implement the functions below yet */
LONG FASTCALL AddRaw(volatile LONG *Destination, LONG Value);
LONG64 FASTCALL AddRaw64(volatile LONG64 *Destination, LONG64 Value);
ULONG64 FASTCALL AddULong64Raw(volatile ULONG64 *Destination, ULONG64 Value);
ULONG FASTCALL AddULongRaw(volatile ULONG *Destination, ULONG Value);
void FASTCALL BarrierAfterRead(void);
LONG FASTCALL IncrementRaw(volatile LONG *Destination);
LONG64 FASTCALL IncrementRaw64(volatile LONG64 *Destination);
ULONG64 FASTCALL IncrementULong64Raw(volatile ULONG64 *Destination);
ULONG FASTCALL IncrementULongRaw(volatile ULONG *Destination);
LONG64 MultiplyExtract128(LONG64 Multiplier, LONG64 Multiplicand, UCHAR Shift);
UCHAR FASTCALL ReadBooleanRaw(const volatile BOOLEAN *Source);
INT32 FASTCALL ReadInt32Acquire(const volatile INT32 *Source);
INT32 FASTCALL ReadInt32NoFence(const volatile INT32 *Source);
INT32 FASTCALL ReadInt32Raw(const volatile INT32 *Source);
UINT32 FASTCALL ReadUInt32Acquire(const volatile UINT32 *Source);
UINT32 FASTCALL ReadUInt32NoFence(const volatile UINT32 *Source);
UINT32 FASTCALL ReadUInt32Raw(const volatile UINT32 *Source);
ULONG64 UnsignedMultiply128(ULONG64 Multiplier, ULONG64 Multiplicand, ULONG64 *HighProduct);
ULONG64 UnsignedMultiplyExtract128(ULONG64 Multiplier, ULONG64 Multiplicand, UCHAR Shift);
void FASTCALL WriteInt32NoFence(volatile INT32 *Destination, INT32 Value);
void FASTCALL WriteInt32Raw(volatile INT32 *Destination, INT32 Value);
void FASTCALL WriteInt32Release(volatile INT32 *Destination, INT32 Value);
void FASTCALL WriteUInt32NoFence(volatile UINT32 *Destination, UINT32 Value);
void FASTCALL WriteUInt32Raw(volatile UINT32 *Destination, UINT32 Value);
void FASTCALL WriteUInt32Release(volatile UINT32 *Destination, UINT32 Value);

#ifdef __cplusplus
}
#endif

#define DECLSPEC_SAFEBUFFERS 0

#define DECLSPEC_GUARDNOCF 0

#define CacheLineFlush(Address) _mm_clflush(Address)
#define COMPILETIME_OR_6FLAGS(a, b, c, d, e, f) ((UINT) (a) | (UINT) (b) | (UINT) (c) | (UINT) (d) | (UINT) (e) | (UINT) (f))
#define DEPRECATED_NO_MESSAGE_STDAPI_(type) EXTERN_C __declspec(deprecated) type STDAPICALLTYPE
#define DEPRECATED_NO_MESSAGE_STDAPIV_(type) EXTERN_C __declspec(deprecated) type STDAPIVCALLTYPE
#define DEPRECATED_STDAPI(message) EXTERN_C __declspec(deprecated(message)) HRESULT STDAPICALLTYPE
#define DEPRECATED_STDAPI_(type, message) EXTERN_C __declspec(deprecated(message)) type STDAPICALLTYPE
#define DEPRECATED_STDAPIV(message) EXTERN_C __declspec(deprecated(message)) HRESULT STDAPIVCALLTYPE
#define DEPRECATED_STDAPIV_(type, message) EXTERN_C __declspec(deprecated(message)) type STDAPIVCALLTYPE
#define GetCallersEflags() __getcallerseflags()
#define STDAPI_CHPE_PATCHABLE_(type) EXTERN_C DECLSPEC_CHPE_PATCHABLE type STDAPICALLTYPE

#endif /* __MINIPORT_H */
