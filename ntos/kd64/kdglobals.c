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
    .KiCallUserMode = (ULONG_PTR)KiCallUserMode,
    .KeUserCallbackDispatcher = (ULONG_PTR)&KeUserCallbackDispatcher,
    .PsLoadedModuleList = (ULONG_PTR)&PsLoadedModuleList,
    .PsActiveProcessHead = (ULONG_PTR)&PsActiveProcessHead,
    .PspCidTable = (ULONG_PTR)&PspCidTable,
    .ExpSystemResourcesList = (ULONG_PTR)&ExpSystemResourcesList,
    /* TODO: ExpPagedPoolDescriptor and ExpNumberOfPagedPools: the LiberNT pool has no such globals */
    .KeTimeIncrement = (ULONG_PTR)&KeTimeIncrement,
    /* TODO: KeBugCheckCallbackListHead is not a published LiberNT global */
    .KiBugcheckData = (ULONG_PTR)KiBugCheckData,
    .IopErrorLogListHead = (ULONG_PTR)&IopErrorLogListHead,
    .ObpRootDirectoryObject = (ULONG_PTR)&ObpRootDirectoryObject,
    .ObpTypeObjectType = (ULONG_PTR)&ObpTypeObjectType,
    /* TODO: the cache and pool extents are private to the LiberNT memory manager */
    .MmPfnDatabase = (ULONG_PTR)&MmPfnDatabase,
    .MmNumberOfPagingFiles = (ULONG_PTR)&MmNumberOfPagingFiles,
    .MmLowestPhysicalPage = (ULONG_PTR)&MmLowestPhysicalPage,
    .MmHighestPhysicalPage = (ULONG_PTR)&MmHighestPhysicalPage,
    .MmNumberOfPhysicalPages = (ULONG_PTR)&MmNumberOfPhysicalPages,
    .MmPageSize = PAGE_SIZE,
    .MmTotalCommitLimit = (ULONG_PTR)&MmTotalCommitLimit,
    .MmTotalCommittedPages = (ULONG_PTR)&MmTotalCommittedPages,
    .MmSharedCommit = (ULONG_PTR)&MmSharedCommit,
    .MmDriverCommit = (ULONG_PTR)&MmDriverCommit,
    .MmProcessCommit = (ULONG_PTR)&MmProcessCommit,
    .MmPagedPoolCommit = (ULONG_PTR)&MmPagedPoolCommit,
    .MmZeroedPageListHead = (ULONG_PTR)&MmZeroedPageListHead,
    .MmFreePageListHead = (ULONG_PTR)&MmFreePageListHead,
    .MmStandbyPageListHead = (ULONG_PTR)&MmStandbyPageListHead,
    .MmModifiedPageListHead = (ULONG_PTR)&MmModifiedPageListHead,
    .MmModifiedNoWritePageListHead = (ULONG_PTR)&MmModifiedNoWritePageListHead,
    .MmAvailablePages = (ULONG_PTR)&MmAvailablePages,
    .MmResidentAvailablePages = (ULONG_PTR)&MmResidentAvailablePages,
    /* TODO: PoolTrackTable and NonPagedPoolDescriptor: the LiberNT pool publishes neither */
    .MmHighestUserAddress = (ULONG_PTR)&MmHighestUserAddress,
    .MmSystemRangeStart = (ULONG_PTR)&MmSystemRangeStart,
    .MmUserProbeAddress = (ULONG_PTR)&MmUserProbeAddress,
    .KdPrintCircularBuffer = (ULONG_PTR)KdPrintDefaultCircularBuffer,
    .KdPrintCircularBufferEnd = (ULONG_PTR)KdPrintDefaultCircularBuffer +
                                sizeof(KdPrintDefaultCircularBuffer),
    .KdPrintWritePointer = (ULONG_PTR)&KdPrintWritePointer,
    .KdPrintRolloverCount = (ULONG_PTR)&KdPrintRolloverCount,
    .MmLoadedUserImageList = (ULONG_PTR)&MmLoadedUserImageList,
    .NtBuildLab = (ULONG_PTR)NtBuildLab,
    /* TODO: KiNormalSystemCall: LiberNT enters the kernel through its own stub */
    .KiProcessorBlock = (ULONG_PTR)KiProcessorBlock,
    .MmUnloadedDrivers = (ULONG_PTR)&MmUnloadedDrivers,
    /* TODO: MmLastUnloadedDriver is not a published LiberNT global */
    .MmTriageActionTaken = (ULONG_PTR)&MmTriageActionTaken,
    .KernelVerifier = (ULONG_PTR)&KernelVerifier,
    .MmVerifierData = (ULONG_PTR)&MmVerifierData,
    .MmPeakCommitment = (ULONG_PTR)&MmPeakCommitment,
    .MmTotalCommitLimitMaximum = (ULONG_PTR)&MmTotalCommitLimitMaximum,
    .CmNtCSDVersion = (ULONG_PTR)&CmNtCSDVersion,
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
    .OffsetPrcbCpuType = FIELD_OFFSET(KPRCB, CpuType),
    .OffsetPrcbVendorString = FIELD_OFFSET(KPRCB, VendorString),
    .OffsetPrcbProcStateContext = FIELD_OFFSET(KPRCB, ProcessorState.ContextFrame),
    .OffsetPrcbNumber = FIELD_OFFSET(KPRCB, Number),
    .SizeEThread = sizeof(ETHREAD),
    .KdPrintCircularBufferPtr = (ULONG_PTR)&KdPrintCircularBuffer,
    .KdPrintBufferSize = (ULONG_PTR)&KdPrintBufferSize,
    .KeLoaderBlock = (ULONG_PTR)&KeLoaderBlock,
    /* TODO: the PCR size and its member offsets belong to the architecture layer */
    .OffsetPrcbProcStateSpecialReg = FIELD_OFFSET(KPRCB, ProcessorState.SpecialRegisters),
    .OffsetPrcbContext = FIELD_OFFSET(KPRCB, CrashDumpContext),
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
ULONG Kd_SYSTEM_Mask;
ULONG Kd_SMSS_Mask;
ULONG Kd_SETUP_Mask;
ULONG Kd_NTFS_Mask;
ULONG Kd_FSTUB_Mask;
ULONG Kd_CRASHDUMP_Mask;
ULONG Kd_CDAUDIO_Mask;
ULONG Kd_CDROM_Mask;
ULONG Kd_CLASSPNP_Mask;
ULONG Kd_DISK_Mask;
ULONG Kd_REDBOOK_Mask;
ULONG Kd_STORPROP_Mask;
ULONG Kd_SCSIPORT_Mask;
ULONG Kd_SCSIMINIPORT_Mask;
ULONG Kd_CONFIG_Mask;
ULONG Kd_I8042PRT_Mask;
ULONG Kd_SERMOUSE_Mask;
ULONG Kd_LSERMOUS_Mask;
ULONG Kd_KBDHID_Mask;
ULONG Kd_MOUHID_Mask;
ULONG Kd_KBDCLASS_Mask;
ULONG Kd_MOUCLASS_Mask;
ULONG Kd_TWOTRACK_Mask;
ULONG Kd_WMILIB_Mask;
ULONG Kd_ACPI_Mask;
ULONG Kd_AMLI_Mask;
ULONG Kd_HALIA64_Mask;
ULONG Kd_VIDEO_Mask;
ULONG Kd_SVCHOST_Mask;
ULONG Kd_VIDEOPRT_Mask;
ULONG Kd_TCPIP_Mask;
ULONG Kd_DMSYNTH_Mask;
ULONG Kd_NTOSPNP_Mask;
ULONG Kd_FASTFAT_Mask;
ULONG Kd_SAMSS_Mask;
ULONG Kd_PNPMGR_Mask;
ULONG Kd_NETAPI_Mask;
ULONG Kd_SCSERVER_Mask;
ULONG Kd_SCCLIENT_Mask;
ULONG Kd_SERIAL_Mask;
ULONG Kd_SERENUM_Mask;
ULONG Kd_UHCD_Mask;
ULONG Kd_RPCPROXY_Mask;
ULONG Kd_AUTOCHK_Mask;
ULONG Kd_DCOMSS_Mask;
ULONG Kd_UNIMODEM_Mask;
ULONG Kd_SIS_Mask;
ULONG Kd_FLTMGR_Mask;
ULONG Kd_WMICORE_Mask;
ULONG Kd_BURNENG_Mask;
ULONG Kd_IMAPI_Mask;
ULONG Kd_SXS_Mask;
ULONG Kd_FUSION_Mask;
ULONG Kd_IDLETASK_Mask;
ULONG Kd_SOFTPCI_Mask;
ULONG Kd_TAPE_Mask;
ULONG Kd_MCHGR_Mask;
ULONG Kd_IDEP_Mask;
ULONG Kd_PCIIDE_Mask;
ULONG Kd_FLOPPY_Mask;
ULONG Kd_FDC_Mask;
ULONG Kd_TERMSRV_Mask;
ULONG Kd_W32TIME_Mask;
ULONG Kd_PREFETCHER_Mask;
ULONG Kd_RSFILTER_Mask;
ULONG Kd_FCPORT_Mask;
ULONG Kd_PCI_Mask;
ULONG Kd_DMIO_Mask;
ULONG Kd_DMCONFIG_Mask;
ULONG Kd_DMADMIN_Mask;
ULONG Kd_WSOCKTRANSPORT_Mask;
ULONG Kd_VSS_Mask;
ULONG Kd_PNPMEM_Mask;
ULONG Kd_PROCESSOR_Mask;
ULONG Kd_DMSERVER_Mask;
ULONG Kd_SR_Mask;
ULONG Kd_INFINIBAND_Mask;
ULONG Kd_IHVDRIVER_Mask;
ULONG Kd_IHVVIDEO_Mask;
ULONG Kd_IHVAUDIO_Mask;
ULONG Kd_IHVNETWORK_Mask;
ULONG Kd_IHVSTREAMING_Mask;
ULONG Kd_IHVBUS_Mask;
ULONG Kd_HPS_Mask;
ULONG Kd_RTLTHREADPOOL_Mask;
ULONG Kd_LDR_Mask;
ULONG Kd_TCPIP6_Mask;
ULONG Kd_ISAPNP_Mask;
ULONG Kd_SHPC_Mask;
ULONG Kd_STORPORT_Mask;
ULONG Kd_STORMINIPORT_Mask;
ULONG Kd_PRINTSPOOLER_Mask;
ULONG Kd_VSSDYNDISK_Mask;
ULONG Kd_VERIFIER_Mask;
ULONG Kd_VDS_Mask;
ULONG Kd_VDSBAS_Mask;
ULONG Kd_VDSDYN_Mask;
ULONG Kd_VDSDYNDR_Mask;
ULONG Kd_VDSLDR_Mask;
ULONG Kd_VDSUTIL_Mask;
ULONG Kd_DFRGIFC_Mask;
ULONG Kd_DEFAULT_Mask;
ULONG Kd_MM_Mask;
ULONG Kd_DFSC_Mask;
ULONG Kd_WOW64_Mask;
ULONG Kd_ALPC_Mask;
ULONG Kd_WDI_Mask;
ULONG Kd_PERFLIB_Mask;
ULONG Kd_KTM_Mask;
ULONG Kd_IOSTRESS_Mask;
ULONG Kd_HEAP_Mask;
ULONG Kd_WHEA_Mask;
ULONG Kd_USERGDI_Mask;
ULONG Kd_MMCSS_Mask;
ULONG Kd_TPM_Mask;
ULONG Kd_THREADORDER_Mask;
ULONG Kd_ENVIRON_Mask;
ULONG Kd_EMS_Mask;
ULONG Kd_WDT_Mask;
ULONG Kd_FVEVOL_Mask;
ULONG Kd_NDIS_Mask;
ULONG Kd_NVCTRACE_Mask;
ULONG Kd_LUAFV_Mask;
ULONG Kd_APPCOMPAT_Mask;
ULONG Kd_USBSTOR_Mask;
ULONG Kd_SBP2PORT_Mask;
ULONG Kd_COVERAGE_Mask;
ULONG Kd_CACHEMGR_Mask;
ULONG Kd_MOUNTMGR_Mask;
ULONG Kd_CFR_Mask;
ULONG Kd_TXF_Mask;
ULONG Kd_KSECDD_Mask;
ULONG Kd_FLTREGRESS_Mask;
ULONG Kd_MPIO_Mask;
ULONG Kd_MSDSM_Mask;
ULONG Kd_UDFS_Mask;
ULONG Kd_PSHED_Mask;
ULONG Kd_STORVSP_Mask;
ULONG Kd_LSASS_Mask;
ULONG Kd_SSPICLI_Mask;
ULONG Kd_CNG_Mask;
ULONG Kd_EXFAT_Mask;
ULONG Kd_FILETRACE_Mask;
ULONG Kd_XSAVE_Mask;
ULONG Kd_SE_Mask;
ULONG Kd_DRIVEEXTENDER_Mask;
ULONG Kd_POWER_Mask;
ULONG Kd_CRASHDUMPXHCI_Mask;
ULONG Kd_GPIO_Mask;
ULONG Kd_REFS_Mask;
ULONG Kd_WER_Mask;
ULONG Kd_CAPIMG_Mask;
ULONG Kd_VPCI_Mask;
ULONG Kd_STORAGECLASSMEMORY_Mask;
ULONG Kd_FSLIB_Mask;
ULONG Kd_ENDOFTABLE_Mask;

/*
 * The table the filter API indexes with a component id. Its order is the order
 * of the DPFLTR_<component>_ID values, so entry N answers for component N.
 */
PULONG KdComponentTable[MAX_KD_COMPONENT_TABLE_ENTRIES] =
{
    &Kd_SYSTEM_Mask,
    &Kd_SMSS_Mask,
    &Kd_SETUP_Mask,
    &Kd_NTFS_Mask,
    &Kd_FSTUB_Mask,
    &Kd_CRASHDUMP_Mask,
    &Kd_CDAUDIO_Mask,
    &Kd_CDROM_Mask,
    &Kd_CLASSPNP_Mask,
    &Kd_DISK_Mask,
    &Kd_REDBOOK_Mask,
    &Kd_STORPROP_Mask,
    &Kd_SCSIPORT_Mask,
    &Kd_SCSIMINIPORT_Mask,
    &Kd_CONFIG_Mask,
    &Kd_I8042PRT_Mask,
    &Kd_SERMOUSE_Mask,
    &Kd_LSERMOUS_Mask,
    &Kd_KBDHID_Mask,
    &Kd_MOUHID_Mask,
    &Kd_KBDCLASS_Mask,
    &Kd_MOUCLASS_Mask,
    &Kd_TWOTRACK_Mask,
    &Kd_WMILIB_Mask,
    &Kd_ACPI_Mask,
    &Kd_AMLI_Mask,
    &Kd_HALIA64_Mask,
    &Kd_VIDEO_Mask,
    &Kd_SVCHOST_Mask,
    &Kd_VIDEOPRT_Mask,
    &Kd_TCPIP_Mask,
    &Kd_DMSYNTH_Mask,
    &Kd_NTOSPNP_Mask,
    &Kd_FASTFAT_Mask,
    &Kd_SAMSS_Mask,
    &Kd_PNPMGR_Mask,
    &Kd_NETAPI_Mask,
    &Kd_SCSERVER_Mask,
    &Kd_SCCLIENT_Mask,
    &Kd_SERIAL_Mask,
    &Kd_SERENUM_Mask,
    &Kd_UHCD_Mask,
    &Kd_RPCPROXY_Mask,
    &Kd_AUTOCHK_Mask,
    &Kd_DCOMSS_Mask,
    &Kd_UNIMODEM_Mask,
    &Kd_SIS_Mask,
    &Kd_FLTMGR_Mask,
    &Kd_WMICORE_Mask,
    &Kd_BURNENG_Mask,
    &Kd_IMAPI_Mask,
    &Kd_SXS_Mask,
    &Kd_FUSION_Mask,
    &Kd_IDLETASK_Mask,
    &Kd_SOFTPCI_Mask,
    &Kd_TAPE_Mask,
    &Kd_MCHGR_Mask,
    &Kd_IDEP_Mask,
    &Kd_PCIIDE_Mask,
    &Kd_FLOPPY_Mask,
    &Kd_FDC_Mask,
    &Kd_TERMSRV_Mask,
    &Kd_W32TIME_Mask,
    &Kd_PREFETCHER_Mask,
    &Kd_RSFILTER_Mask,
    &Kd_FCPORT_Mask,
    &Kd_PCI_Mask,
    &Kd_DMIO_Mask,
    &Kd_DMCONFIG_Mask,
    &Kd_DMADMIN_Mask,
    &Kd_WSOCKTRANSPORT_Mask,
    &Kd_VSS_Mask,
    &Kd_PNPMEM_Mask,
    &Kd_PROCESSOR_Mask,
    &Kd_DMSERVER_Mask,
    &Kd_SR_Mask,
    &Kd_INFINIBAND_Mask,
    &Kd_IHVDRIVER_Mask,
    &Kd_IHVVIDEO_Mask,
    &Kd_IHVAUDIO_Mask,
    &Kd_IHVNETWORK_Mask,
    &Kd_IHVSTREAMING_Mask,
    &Kd_IHVBUS_Mask,
    &Kd_HPS_Mask,
    &Kd_RTLTHREADPOOL_Mask,
    &Kd_LDR_Mask,
    &Kd_TCPIP6_Mask,
    &Kd_ISAPNP_Mask,
    &Kd_SHPC_Mask,
    &Kd_STORPORT_Mask,
    &Kd_STORMINIPORT_Mask,
    &Kd_PRINTSPOOLER_Mask,
    &Kd_VSSDYNDISK_Mask,
    &Kd_VERIFIER_Mask,
    &Kd_VDS_Mask,
    &Kd_VDSBAS_Mask,
    &Kd_VDSDYN_Mask,
    &Kd_VDSDYNDR_Mask,
    &Kd_VDSLDR_Mask,
    &Kd_VDSUTIL_Mask,
    &Kd_DFRGIFC_Mask,
    &Kd_DEFAULT_Mask,
    &Kd_MM_Mask,
    &Kd_DFSC_Mask,
    &Kd_WOW64_Mask,
    &Kd_ALPC_Mask,
    &Kd_WDI_Mask,
    &Kd_PERFLIB_Mask,
    &Kd_KTM_Mask,
    &Kd_IOSTRESS_Mask,
    &Kd_HEAP_Mask,
    &Kd_WHEA_Mask,
    &Kd_USERGDI_Mask,
    &Kd_MMCSS_Mask,
    &Kd_TPM_Mask,
    &Kd_THREADORDER_Mask,
    &Kd_ENVIRON_Mask,
    &Kd_EMS_Mask,
    &Kd_WDT_Mask,
    &Kd_FVEVOL_Mask,
    &Kd_NDIS_Mask,
    &Kd_NVCTRACE_Mask,
    &Kd_LUAFV_Mask,
    &Kd_APPCOMPAT_Mask,
    &Kd_USBSTOR_Mask,
    &Kd_SBP2PORT_Mask,
    &Kd_COVERAGE_Mask,
    &Kd_CACHEMGR_Mask,
    &Kd_MOUNTMGR_Mask,
    &Kd_CFR_Mask,
    &Kd_TXF_Mask,
    &Kd_KSECDD_Mask,
    &Kd_FLTREGRESS_Mask,
    &Kd_MPIO_Mask,
    &Kd_MSDSM_Mask,
    &Kd_UDFS_Mask,
    &Kd_PSHED_Mask,
    &Kd_STORVSP_Mask,
    &Kd_LSASS_Mask,
    &Kd_SSPICLI_Mask,
    &Kd_CNG_Mask,
    &Kd_EXFAT_Mask,
    &Kd_FILETRACE_Mask,
    &Kd_XSAVE_Mask,
    &Kd_SE_Mask,
    &Kd_DRIVEEXTENDER_Mask,
    &Kd_POWER_Mask,
    &Kd_CRASHDUMPXHCI_Mask,
    &Kd_GPIO_Mask,
    &Kd_REFS_Mask,
    &Kd_WER_Mask,
    &Kd_CAPIMG_Mask,
    &Kd_VPCI_Mask,
    &Kd_STORAGECLASSMEMORY_Mask,
    &Kd_FSLIB_Mask,
    &Kd_ENDOFTABLE_Mask
};

ULONG KdComponentTableSize = RTL_NUMBER_OF(KdComponentTable);

C_ASSERT(RTL_NUMBER_OF(KdComponentTable) == DPFLTR_ENDOFTABLE_ID + 1);

/* EOF */
