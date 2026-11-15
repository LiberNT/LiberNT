/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The state the kernel debugger keeps, and what the host reads of it
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "kdpriv.h"
#include "../kd/kd.h"
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

/* What the published API tells the rest of the system about the debugger */
BOOLEAN KdDebuggerEnabled = FALSE;
BOOLEAN KdDebuggerNotPresent = TRUE;
BOOLEAN KdEnteredDebugger = FALSE;
BOOLEAN KdPitchDebugger = TRUE;
BOOLEAN KdPreviouslyEnabled = FALSE;
BOOLEAN KdAutoEnableOnEvent = FALSE;
BOOLEAN KdBlockEnable = FALSE;
BOOLEAN KdIgnoreUmExceptions = FALSE;
BOOLEAN KdBreakAfterSymbolLoad = FALSE;
BOOLEAN KdpDebuggerStructuresInitialized = FALSE;
ULONG KdDisableCount = 0;

/*
 * The port the boot loader handed over, for whoever asks which COM port is
 * taken. It stays a pointer because that is how the HAL publishes it.
 */
PUCHAR KdComPortInUse = NULL;
ULONG KdHvComPortInUse = 0; /* TODO: no evidence for the published type; see INFO_NEEDED */

/* The engine's own locks and the processor that owns the link right now */
KSPIN_LOCK KdpDebuggerLock;
KSPIN_LOCK KdpDataSpinLock;
KSPIN_LOCK KdpPrintSpinLock;
volatile PKPRCB KdpPortOwnerPrcb = NULL;
BOOLEAN KdpPortLocked = FALSE;
BOOLEAN KdpControlCPressed = FALSE;
BOOLEAN KdpContextSent = FALSE;

/* Retry budget and the pending break-in the transport noticed */
KD_CONTEXT KdpContext;

/* Where a trap goes while the debugger is off, and where it goes once it is on */
PKDEBUG_ROUTINE KiDebugRoutine = KdpStub;

/* Timing the host asks about, and the slip the host corrects */
LARGE_INTEGER KdPerformanceCounterRate;
LARGE_INTEGER KdTimerStart;
LARGE_INTEGER KdTimerStop;
LARGE_INTEGER KdTimerDifference;

KDPC KdpTimeSlipDpc;
KTIMER KdpTimeSlipTimer;
WORK_QUEUE_ITEM KdpTimeSlipWorkItem;
LONG KdpTimeSlipPending = 0;
PKEVENT KdpTimeSlipEvent = NULL;
KSPIN_LOCK KdpTimeSlipEventLock;

/* The request and the path the engine builds its answers in */
CHAR KdpMessageBuffer[KDP_MSG_BUFFER_SIZE];
CHAR KdpPathBuffer[KDP_MSG_BUFFER_SIZE];
CHAR KdpResponseBuffer[KDP_RESPONSE_SIZE];

/* The DbgPrint log the host and the crash dump both read */
CHAR KdPrintDefaultCircularBuffer[KD_DEFAULT_LOG_BUFFER_SIZE];
PCHAR KdPrintCircularBuffer = KdPrintDefaultCircularBuffer;
PCHAR KdPrintWritePointer = KdPrintDefaultCircularBuffer;
ULONG KdPrintBufferSize = sizeof(KdPrintDefaultCircularBuffer);
ULONG KdPrintRolloverCount = 0;
ULONG KdPrintBufferChanges = 0;

/* The breakpoints the host owns, and the instruction that stands in for them */
BREAKPOINT_ENTRY KdpBreakpointTable[KD_BREAKPOINT_MAX];
KD_BREAKPOINT_TYPE KdpBreakpointInstruction = KD_BREAKPOINT_VALUE;
BOOLEAN KdpOweBreakpoint = FALSE;
BOOLEAN BreakpointsSuspended = FALSE;
ULONG KdpNumInternalBreakpoints = 0;

/* The range the host declared as the current symbol, for stepping decisions */
ULONG_PTR KdpCurrentSymbolStart = 0;
ULONG_PTR KdpCurrentSymbolEnd = 0;

/* The stop being reported, for an interactive debugger inside this kernel */
PCONTEXT KdpReportedContext = NULL;
KPROCESSOR_MODE KdpReportedMode = KernelMode;
BOOLEAN KdpReportedFirstChance = TRUE;

/* Branch trace the host collects out of band */
ULONG TraceDataBuffer[40];
ULONG TraceDataBufferPosition = 1;

/* VERSION AND DEBUGGER DATA ***************************************************/

LIST_ENTRY KdpDebuggerDataListHead;

DBGKD_GET_VERSION64 KdVersionBlock =
{
    .MajorVersion = (DBGKD_MAJOR_NT << 8),
    .MinorVersion = 0,
    .ProtocolVersion = DBGKD_64BIT_PROTOCOL_VERSION2,
    .KdSecondaryVersion = CURRENT_KD_SECONDARY_VERSION,
    .Flags = DBGKD_VERS_FLAG_DATA | DBGKD_VERS_FLAG_MP |
             (sizeof(PVOID) == sizeof(ULONG64) ? DBGKD_VERS_FLAG_PTR64 : 0),
    .MachineType = IMAGE_FILE_MACHINE_NATIVE,
    .MaxPacketType = PACKET_TYPE_MAX,
    .MaxStateChange = DbgKdMaximumStateChange - DbgKdMinimumStateChange,
    .MaxManipulate = DbgKdMaximumManipulate - DbgKdMinimumManipulate,
    .Simulation = DBGKD_SIMULATION_NONE
};

/*
 * Everything the host needs in order to read this kernel without symbols for
 * the kernel's own private names. Addresses are filled in here when they are
 * link-time constants; the rest is filled in by KdUpdateDataBlock() once the
 * structures they describe exist.
 */
KDDEBUGGER_DATA64 KdDebuggerDataBlock =
{
    .Header = {{0, 0}, 'GBDK', sizeof(KDDEBUGGER_DATA64)},
    .ThCallbackStack = FIELD_OFFSET(KTHREAD, CallbackStack),
    /* TODO: NextCallback and FramePointer: the LiberNT callout frame has no such members */
    .PaeEnabled = FALSE,
    .KiCallUserMode = ULPTR64_INITIALIZER(KI_USER_MODE_CALLBACK_ENTRY),
    .KeUserCallbackDispatcher = ULPTR64_INITIALIZER(&KeUserCallbackDispatcher),
    .PsLoadedModuleList = ULPTR64_INITIALIZER(&PsLoadedModuleList),
    .PsActiveProcessHead = ULPTR64_INITIALIZER(&PsActiveProcessHead),
    .PspCidTable = ULPTR64_INITIALIZER(&PspCidTable),
    .ExpSystemResourcesList = ULPTR64_INITIALIZER(&ExpSystemResourcesList),
    /* TODO: ExpPagedPoolDescriptor and ExpNumberOfPagedPools: the LiberNT pool has no such globals */
    .KeTimeIncrement = ULPTR64_INITIALIZER(&KeTimeIncrement),
    .KeBugCheckCallbackListHead = ULPTR64_INITIALIZER(&KeBugcheckCallbackListHead),
    .KiBugcheckData = ULPTR64_INITIALIZER(KiBugCheckData),
    .IopErrorLogListHead = ULPTR64_INITIALIZER(&IopErrorLogListHead),
    .ObpRootDirectoryObject = ULPTR64_INITIALIZER(&ObpRootDirectoryObject),
    .ObpTypeObjectType = ULPTR64_INITIALIZER(&ObpTypeObjectType),
    /* TODO: the cache and pool extents are private to the LiberNT memory manager */
    .MmPfnDatabase = ULPTR64_INITIALIZER(&MmPfnDatabase),
    .MmNumberOfPagingFiles = ULPTR64_INITIALIZER(&MmNumberOfPagingFiles),
    .MmLowestPhysicalPage = ULPTR64_INITIALIZER(&MmLowestPhysicalPage),
    .MmHighestPhysicalPage = ULPTR64_INITIALIZER(&MmHighestPhysicalPage),
    .MmNumberOfPhysicalPages = ULPTR64_INITIALIZER(&MmNumberOfPhysicalPages),
    .MmPageSize = PAGE_SIZE,
    .MmTotalCommitLimit = ULPTR64_INITIALIZER(&MmTotalCommitLimit),
    .MmTotalCommittedPages = ULPTR64_INITIALIZER(&MmTotalCommittedPages),
    .MmSharedCommit = ULPTR64_INITIALIZER(&MmSharedCommit),
    .MmDriverCommit = ULPTR64_INITIALIZER(&MmDriverCommit),
    .MmProcessCommit = ULPTR64_INITIALIZER(&MmProcessCommit),
    .MmPagedPoolCommit = ULPTR64_INITIALIZER(&MmPagedPoolCommit),
    .MmZeroedPageListHead = ULPTR64_INITIALIZER(&MmZeroedPageListHead),
    .MmFreePageListHead = ULPTR64_INITIALIZER(&MmFreePageListHead),
    .MmStandbyPageListHead = ULPTR64_INITIALIZER(&MmStandbyPageListHead),
    .MmModifiedPageListHead = ULPTR64_INITIALIZER(&MmModifiedPageListHead),
    .MmModifiedNoWritePageListHead = ULPTR64_INITIALIZER(&MmModifiedNoWritePageListHead),
    .MmAvailablePages = ULPTR64_INITIALIZER(&MmAvailablePages),
    .MmResidentAvailablePages = ULPTR64_INITIALIZER(&MmResidentAvailablePages),
    /* TODO: PoolTrackTable and NonPagedPoolDescriptor: the LiberNT pool publishes neither */
    .MmHighestUserAddress = ULPTR64_INITIALIZER(&MmHighestUserAddress),
    .MmSystemRangeStart = ULPTR64_INITIALIZER(&MmSystemRangeStart),
    .MmUserProbeAddress = ULPTR64_INITIALIZER(&MmUserProbeAddress),
    .KdPrintCircularBuffer = ULPTR64_INITIALIZER(KdPrintDefaultCircularBuffer),
    .KdPrintCircularBufferEnd = ULPTR64_INITIALIZER(KdPrintDefaultCircularBuffer +
                                sizeof(KdPrintDefaultCircularBuffer)),
    .KdPrintWritePointer = ULPTR64_INITIALIZER(&KdPrintWritePointer),
    .KdPrintRolloverCount = ULPTR64_INITIALIZER(&KdPrintRolloverCount),
    .MmLoadedUserImageList = ULPTR64_INITIALIZER(&MmLoadedUserImageList),
    .NtBuildLab = ULPTR64_INITIALIZER(NtBuildLab),
    /* TODO: KiNormalSystemCall: LiberNT enters the kernel through its own stub */
    .KiProcessorBlock = ULPTR64_INITIALIZER(KiProcessorBlock),
    .MmUnloadedDrivers = ULPTR64_INITIALIZER(&MmUnloadedDrivers),
    /* TODO: MmLastUnloadedDriver is not a published LiberNT global */
    .MmTriageActionTaken = ULPTR64_INITIALIZER(&MmTriageActionTaken),
    .KernelVerifier = ULPTR64_INITIALIZER(&KernelVerifier),
    .MmVerifierData = ULPTR64_INITIALIZER(&MmVerifierData),
    .MmPeakCommitment = ULPTR64_INITIALIZER(&MmPeakCommitment),
    .MmTotalCommitLimitMaximum = ULPTR64_INITIALIZER(&MmTotalCommitLimitMaximum),
    .CmNtCSDVersion = ULPTR64_INITIALIZER(&CmNtCSDVersion),
    .OffsetKThreadNextProcessor = FIELD_OFFSET(KTHREAD, NextProcessor),
    .OffsetKThreadTeb = FIELD_OFFSET(KTHREAD, Teb),
    .OffsetKThreadKernelStack = FIELD_OFFSET(KTHREAD, KernelStack),
    .OffsetKThreadInitialStack = FIELD_OFFSET(KTHREAD, InitialStack),
    .OffsetKThreadApcProcess = FIELD_OFFSET(KTHREAD, ApcState.Process),
    .OffsetKThreadState = FIELD_OFFSET(KTHREAD, State),
    .SizeEProcess = sizeof(EPROCESS),
    .OffsetEprocessPeb = FIELD_OFFSET(EPROCESS, Peb),
    .OffsetEprocessParentCID = FIELD_OFFSET(EPROCESS, InheritedFromUniqueProcessId),
    .OffsetEprocessDirectoryTableBase = FIELD_OFFSET(KPROCESS, DirectoryTableBase),
    .SizePrcb = sizeof(KPRCB),
    .OffsetPrcbDpcRoutine = FIELD_OFFSET(KPRCB, DpcRoutineActive),
    .OffsetPrcbCurrentThread = FIELD_OFFSET(KPRCB, CurrentThread),
    .OffsetPrcbMhz = FIELD_OFFSET(KPRCB, MHz),
    .OffsetPrcbCpuType = KI_PRCB_CPU_TYPE_OFFSET,
    .OffsetPrcbVendorString = KI_PRCB_VENDOR_STRING_OFFSET,
    .OffsetPrcbProcStateContext = FIELD_OFFSET(KPRCB, ProcessorState.ContextFrame),
    .OffsetPrcbNumber = FIELD_OFFSET(KPRCB, Number),
    .SizeEThread = sizeof(ETHREAD),
    .KdPrintCircularBufferPtr = ULPTR64_INITIALIZER(&KdPrintCircularBuffer),
    .KdPrintBufferSize = ULPTR64_INITIALIZER(&KdPrintBufferSize),
    .KeLoaderBlock = ULPTR64_INITIALIZER(&KeLoaderBlock),
    /* TODO: the PCR size and its member offsets belong to the architecture layer */
    .OffsetPrcbProcStateSpecialReg = FIELD_OFFSET(KPRCB, ProcessorState.SpecialRegisters),
    .OffsetPrcbContext = KI_PRCB_CONTEXT_OFFSET,
    .OffsetKThreadStackLimit = FIELD_OFFSET(KTHREAD, StackLimit),
    .OffsetKThreadStackBase = FIELD_OFFSET(KTHREAD, StackBase),
    .OffsetKThreadQueueListEntry = FIELD_OFFSET(KTHREAD, QueueListEntry),
    .OffsetEThreadIrpList = FIELD_OFFSET(ETHREAD, IrpList),
    .OffsetPrcbIdleThread = FIELD_OFFSET(KPRCB, IdleThread),
    .OffsetPrcbDpcStack = FIELD_OFFSET(KPRCB, DpcStack),
    .OffsetKThreadWaitReason = FIELD_OFFSET(KTHREAD, WaitReason)
    /* TODO: the GDT selectors, the triage data blocks, the ETW debugger data,
     * the retpoline stub table and PteBase have no LiberNT counterpart yet */
};

/* DEBUG FILTER STATE **********************************************************/

/*
 * One mask per component, as the published Kd_<component>_Mask globals, plus
 * the system-wide mask that answers for every component. Only the system-wide
 * mask starts non-zero, and only for the error level, so that the output a
 * driver sends with no component of its own is seen while the rest is quiet.
 */
ULONG Kd_WIN2000_Mask = 1;

#define KD_COMPONENTS(Component) \
    Component(SYSTEM) Component(SMSS) Component(SETUP) Component(NTFS) Component(FSTUB) Component(CRASHDUMP) \
    Component(CDAUDIO) Component(CDROM) Component(CLASSPNP) Component(DISK) Component(REDBOOK) Component(STORPROP) \
    Component(SCSIPORT) Component(SCSIMINIPORT) Component(CONFIG) Component(I8042PRT) Component(SERMOUSE) Component(LSERMOUS) \
    Component(KBDHID) Component(MOUHID) Component(KBDCLASS) Component(MOUCLASS) Component(TWOTRACK) Component(WMILIB) \
    Component(ACPI) Component(AMLI) Component(HALIA64) Component(VIDEO) Component(SVCHOST) Component(VIDEOPRT) \
    Component(TCPIP) Component(DMSYNTH) Component(NTOSPNP) Component(FASTFAT) Component(SAMSS) Component(PNPMGR) \
    Component(NETAPI) Component(SCSERVER) Component(SCCLIENT) Component(SERIAL) Component(SERENUM) Component(UHCD) \
    Component(RPCPROXY) Component(AUTOCHK) Component(DCOMSS) Component(UNIMODEM) Component(SIS) Component(FLTMGR) \
    Component(WMICORE) Component(BURNENG) Component(IMAPI) Component(SXS) Component(FUSION) Component(IDLETASK) \
    Component(SOFTPCI) Component(TAPE) Component(MCHGR) Component(IDEP) Component(PCIIDE) Component(FLOPPY) \
    Component(FDC) Component(TERMSRV) Component(W32TIME) Component(PREFETCHER) Component(RSFILTER) Component(FCPORT) \
    Component(PCI) Component(DMIO) Component(DMCONFIG) Component(DMADMIN) Component(WSOCKTRANSPORT) Component(VSS) \
    Component(PNPMEM) Component(PROCESSOR) Component(DMSERVER) Component(SR) Component(INFINIBAND) Component(IHVDRIVER) \
    Component(IHVVIDEO) Component(IHVAUDIO) Component(IHVNETWORK) Component(IHVSTREAMING) Component(IHVBUS) Component(HPS) \
    Component(RTLTHREADPOOL) Component(LDR) Component(TCPIP6) Component(ISAPNP) Component(SHPC) Component(STORPORT) \
    Component(STORMINIPORT) Component(PRINTSPOOLER) Component(VSSDYNDISK) Component(VERIFIER) Component(VDS) Component(VDSBAS) \
    Component(VDSDYN) Component(VDSDYNDR) Component(VDSLDR) Component(VDSUTIL) Component(DFRGIFC) Component(DEFAULT) \
    Component(MM) Component(DFSC) Component(WOW64) Component(ALPC) Component(WDI) Component(PERFLIB) \
    Component(KTM) Component(IOSTRESS) Component(HEAP) Component(WHEA) Component(USERGDI) Component(MMCSS) \
    Component(TPM) Component(THREADORDER) Component(ENVIRON) Component(EMS) Component(WDT) Component(FVEVOL) \
    Component(NDIS) Component(NVCTRACE) Component(LUAFV) Component(APPCOMPAT) Component(USBSTOR) Component(SBP2PORT) \
    Component(COVERAGE) Component(CACHEMGR) Component(MOUNTMGR) Component(CFR) Component(TXF) Component(KSECDD) \
    Component(FLTREGRESS) Component(MPIO) Component(MSDSM) Component(UDFS) Component(PSHED) Component(STORVSP) \
    Component(LSASS) Component(SSPICLI) Component(CNG) Component(EXFAT) Component(FILETRACE) Component(XSAVE) \
    Component(SE) Component(DRIVEEXTENDER) Component(POWER) Component(CRASHDUMPXHCI) Component(GPIO) Component(REFS) \
    Component(WER) Component(CAPIMG) Component(VPCI) Component(STORAGECLASSMEMORY) Component(FSLIB) Component(ENDOFTABLE)

#define KD_DEFINE_MASK(Name) ULONG Kd_##Name##_Mask;
KD_COMPONENTS(KD_DEFINE_MASK)

#define KD_COMPONENT_SLOT(Name) [DPFLTR_##Name##_ID] = &Kd_##Name##_Mask,
PULONG KdComponentTable[MAX_KD_COMPONENT_TABLE_ENTRIES] =
{
    KD_COMPONENTS(KD_COMPONENT_SLOT)
};

ULONG KdComponentTableSize = RTL_NUMBER_OF(KdComponentTable);

#define KD_COUNT_COMPONENT(Name) + 1
C_ASSERT((0 KD_COMPONENTS(KD_COUNT_COMPONENT)) == MAX_KD_COMPONENT_TABLE_ENTRIES);

/* EOF */
