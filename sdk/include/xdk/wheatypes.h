/******************************************************************************
 *                 Windows Hardware Error Architecture Types                  *
 ******************************************************************************/
$if (_NTDDK_)
#pragma pack(push, 1)

typedef union _WHEA_ERROR_RECORD_SECTION_DESCRIPTOR_FLAGS
{
    struct {
        ULONG Primary : 1;
        ULONG ContainmentWarning : 1;
        ULONG Reset : 1;
        ULONG ThresholdExceeded : 1;
        ULONG ResourceNotAvailable : 1;
        ULONG LatentError : 1;
        ULONG Reserved : 26;
    } DUMMYSTRUCTNAME;
    ULONG AsULONG;
} WHEA_ERROR_RECORD_SECTION_DESCRIPTOR_FLAGS, *PWHEA_ERROR_RECORD_SECTION_DESCRIPTOR_FLAGS;

typedef union _WHEA_ERROR_RECORD_SECTION_DESCRIPTOR_VALIDBITS
{
    struct {
        UCHAR FRUId : 1;
        UCHAR FRUText : 1;
        UCHAR Reserved : 6;
    } DUMMYSTRUCTNAME;
    UCHAR AsUCHAR;
} WHEA_ERROR_RECORD_SECTION_DESCRIPTOR_VALIDBITS, *PWHEA_ERROR_RECORD_SECTION_DESCRIPTOR_VALIDBITS;

typedef union _WHEA_REVISION
{
    struct {
        UCHAR MinorRevision;
        UCHAR MajorRevision;
    } DUMMYSTRUCTNAME;
    USHORT AsUSHORT;
} WHEA_REVISION, *PWHEA_REVISION;

typedef enum _WHEA_ERROR_SEVERITY
{
    WheaErrSevRecoverable   = 0,
    WheaErrSevFatal         = 1,
    WheaErrSevCorrected     = 2,
    WheaErrSevInformational = 3
} WHEA_ERROR_SEVERITY, *PWHEA_ERROR_SEVERITY;

typedef struct _WHEA_ERROR_RECORD_SECTION_DESCRIPTOR
{
    ULONG SectionOffset;
    ULONG SectionLength;
    WHEA_REVISION Revision;
    WHEA_ERROR_RECORD_SECTION_DESCRIPTOR_VALIDBITS ValidBits;
    UCHAR Reserved;
    WHEA_ERROR_RECORD_SECTION_DESCRIPTOR_FLAGS Flags;
    GUID SectionType;
    GUID FRUId;
    WHEA_ERROR_SEVERITY SectionSeverity;
    CCHAR FRUText[20];
} WHEA_ERROR_RECORD_SECTION_DESCRIPTOR, *PWHEA_ERROR_RECORD_SECTION_DESCRIPTOR;

typedef union _WHEA_PROCESSOR_GENERIC_ERROR_SECTION_VALIDBITS
{
    struct {
        ULONGLONG ProcessorType : 1;
        ULONGLONG InstructionSet : 1;
        ULONGLONG ErrorType : 1;
        ULONGLONG Operation : 1;
        ULONGLONG Flags : 1;
        ULONGLONG Level : 1;
        ULONGLONG CPUVersion : 1;
        ULONGLONG CPUBrandString : 1;
        ULONGLONG ProcessorId : 1;
        ULONGLONG TargetAddress : 1;
        ULONGLONG RequesterId : 1;
        ULONGLONG ResponderId : 1;
        ULONGLONG InstructionPointer : 1;
        ULONGLONG Reserved : 51;
    } DUMMYSTRUCTNAME;
    ULONGLONG ValidBits;
} WHEA_PROCESSOR_GENERIC_ERROR_SECTION_VALIDBITS, *PWHEA_PROCESSOR_GENERIC_ERROR_SECTION_VALIDBITS;

typedef struct _WHEA_PROCESSOR_GENERIC_ERROR_SECTION
{
    WHEA_PROCESSOR_GENERIC_ERROR_SECTION_VALIDBITS ValidBits;
    UCHAR ProcessorType;
    UCHAR InstructionSet;
    UCHAR ErrorType;
    UCHAR Operation;
    UCHAR Flags;
    UCHAR Level;
    USHORT Reserved;
    ULONGLONG CPUVersion;
    UCHAR CPUBrandString[128];
    ULONGLONG ProcessorId;
    ULONGLONG TargetAddress;
    ULONGLONG RequesterId;
    ULONGLONG ResponderId;
    ULONGLONG InstructionPointer;
} WHEA_PROCESSOR_GENERIC_ERROR_SECTION, *PWHEA_PROCESSOR_GENERIC_ERROR_SECTION;

#pragma pack(pop)

typedef enum _WHEA_RECOVERY_CONTEXT_ACTION_TAKEN {
    WheaRecoveryContextActionTakenNone = 0,
    WheaRecoveryContextActionTakenOfflineDemotion,
    WheaRecoveryContextActionTakenPageNotReplaced,
    WheaRecoveryContextActionTakenPageReplaced,
    WheaRecoveryContextActionTakenMax
} WHEA_RECOVERY_CONTEXT_ACTION_TAKEN, *PWHEA_RECOVERY_CONTEXT_ACTION_TAKEN;

#pragma pack(push, 1)
typedef union _WHEA_RECOVERY_CONTEXT_ACTION_TAKEN_ADDITIONAL_INFO {
    struct {
        ULONG64 Reserved:64;
    } Bits;
    ULONG64 AsULONG64;
} WHEA_RECOVERY_CONTEXT_ACTION_TAKEN_ADDITIONAL_INFO, *PWHEA_RECOVERY_CONTEXT_ACTION_TAKEN_ADDITIONAL_INFO;
#pragma pack(pop)

typedef union _WHEA_IN_USE_PAGE_NOTIFY_FLAGS {
    struct {
        UCHAR PlatformDirected:1;
        UCHAR PageSwapped:1;
        UCHAR PageDemoted:1;
        UCHAR Reserved:3;
        UCHAR NotifyAllOfflines:1;
        UCHAR PageOfflined:1;
    } Bits;
    UINT8 AsUCHAR;
} WHEA_IN_USE_PAGE_NOTIFY_FLAGS, *PWHEA_IN_USE_PAGE_NOTIFY_FLAGS;

typedef enum _WHEA_RECOVERY_CONTEXT_ERROR_TYPE {
    WheaRecoveryContextErrorTypeMemory = 1,
    WheaRecoveryContextErrorTypePmem,
    WheaRecoveryContextErrorTypeMax
} WHEA_RECOVERY_CONTEXT_ERROR_TYPE, *PWHEA_RECOVERY_CONTEXT_ERROR_TYPE;

#define WHEA_PFA_PAGE_RANGE_MAX 256

typedef struct _WHEA_RECOVERY_CONTEXT_PAGE_INFO {
    ULONG ComponentTag;
    NTSTATUS PageStatus;
    WHEA_RECOVERY_CONTEXT_ACTION_TAKEN ActionTaken;
    WHEA_IN_USE_PAGE_NOTIFY_FLAGS NotifyFlags;
    BOOLEAN ImmediateSuccess;
    UINT16 Reserved;
    WHEA_RECOVERY_CONTEXT_ACTION_TAKEN_ADDITIONAL_INFO ActionTakenAdditionalInfo;
} WHEA_RECOVERY_CONTEXT_PAGE_INFO, *PWHEA_RECOVERY_CONTEXT_PAGE_INFO;

typedef struct _WHEA_RECOVERY_CONTEXT
{
    union {
        struct {
            ULONG_PTR Address;
            BOOLEAN Consumed;
            UINT16 ErrorCode;
            BOOLEAN ErrorIpValid;
            BOOLEAN RestartIpValid;
            BOOLEAN ClearPoison;
        } MemoryError;
        struct {
            ULONG_PTR PmemErrInfo;
        } PmemError;
    };
    UINT64 PartitionId;
    UINT32 VpIndex;
    WHEA_RECOVERY_CONTEXT_ERROR_TYPE ErrorType;
    ULONG PageCount;
    WHEA_RECOVERY_CONTEXT_PAGE_INFO PageInfo[WHEA_PFA_PAGE_RANGE_MAX];
} WHEA_RECOVERY_CONTEXT, *PWHEA_RECOVERY_CONTEXT;

#pragma pack(push, 1)

typedef enum _WHEA_ERROR_TYPE {
    WheaErrTypeProcessor = 0,
    WheaErrTypeMemory,
    WheaErrTypePCIExpress,
    WheaErrTypeNMI,
    WheaErrTypePCIXBus,
    WheaErrTypePCIXDevice,
    WheaErrTypeGeneric
} WHEA_ERROR_TYPE, *PWHEA_ERROR_TYPE;

#pragma pack(pop)

typedef enum _WHEA_ERROR_SOURCE_TYPE {
    WheaErrSrcTypeMCE = 0,
    WheaErrSrcTypeCMC,
    WheaErrSrcTypeCPE,
    WheaErrSrcTypeNMI,
    WheaErrSrcTypePCIe,
    WheaErrSrcTypeGeneric,
    WheaErrSrcTypeINIT,
    WheaErrSrcTypeBOOT,
    WheaErrSrcTypeSCIGeneric,
    WheaErrSrcTypeIPFMCA,
    WheaErrSrcTypeIPFCMC,
    WheaErrSrcTypeIPFCPE,
    WheaErrSrcTypeGenericV2,
    WheaErrSrcTypeSCIGenericV2,
    WheaErrSrcTypeBMC,
    WheaErrSrcTypePMEM,
    WheaErrSrcTypeDeviceDriver,
    WheaErrSrcTypeSea,
    WheaErrSrcTypeSei,
    WheaErrSrcTypeMax
} WHEA_ERROR_SOURCE_TYPE, *PWHEA_ERROR_SOURCE_TYPE;

#pragma pack(push, 1)

typedef union _WHEA_ERROR_PACKET_FLAGS {
    struct {
        ULONG PreviousError : 1;
        ULONG Reserved1 : 1;
        ULONG HypervisorError : 1;
        ULONG Simulated : 1;
        ULONG PlatformPfaControl : 1;
        ULONG PlatformDirectedOffline : 1;
        ULONG Reserved2 : 26;
    } DUMMYSTRUCTNAME;
    ULONG AsULONG;
} WHEA_ERROR_PACKET_FLAGS, *PWHEA_ERROR_PACKET_FLAGS;

typedef enum _WHEA_ERROR_PACKET_DATA_FORMAT {
    WheaDataFormatIPFSalRecord = 0,
    WheaDataFormatXPFMCA,
    WheaDataFormatMemory,
    WheaDataFormatPCIExpress,
    WheaDataFormatNMIPort,
    WheaDataFormatPCIXBus,
    WheaDataFormatPCIXDevice,
    WheaDataFormatGeneric,
    WheaDataFormatMax
} WHEA_ERROR_PACKET_DATA_FORMAT, *PWHEA_ERROR_PACKET_DATA_FORMAT;

#if (NTDDI_VERSION >= NTDDI_WIN7)

#define WHEA_ERROR_PACKET_V2_SIGNATURE 'AEHW'
#define WHEA_ERROR_PACKET_V2_VERSION   3

typedef struct _WHEA_ERROR_PACKET_V2 {
    ULONG Signature;
    ULONG Version;
    ULONG Length;
    WHEA_ERROR_PACKET_FLAGS Flags;
    WHEA_ERROR_TYPE ErrorType;
    WHEA_ERROR_SEVERITY ErrorSeverity;
    ULONG ErrorSourceId;
    WHEA_ERROR_SOURCE_TYPE ErrorSourceType;
    GUID NotifyType;
    ULONGLONG Context;
    WHEA_ERROR_PACKET_DATA_FORMAT DataFormat;
    ULONG Reserved1;
    ULONG DataOffset;
    ULONG DataLength;
    ULONG PshedDataOffset;
    ULONG PshedDataLength;
} WHEA_ERROR_PACKET_V2, *PWHEA_ERROR_PACKET_V2;

#define WHEA_ERROR_PACKET_SIGNATURE WHEA_ERROR_PACKET_V2_SIGNATURE
#define WHEA_ERROR_PACKET_VERSION   WHEA_ERROR_PACKET_V2_VERSION
typedef WHEA_ERROR_PACKET_V2 WHEA_ERROR_PACKET;
typedef WHEA_ERROR_PACKET_V2 *PWHEA_ERROR_PACKET;

#endif /* (NTDDI_VERSION >= NTDDI_WIN7) */

#pragma pack(pop)

$endif (_NTDDK_)
