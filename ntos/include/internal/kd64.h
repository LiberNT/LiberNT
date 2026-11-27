/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Kernel debugger protocol internals
 * COPYRIGHT:   Copyright Alex Ionescu <alex.ionescu@reactos.org>
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define KD_DEFAULT_LOG_BUFFER_SIZE  0x20000

#define KDP_MSG_BUFFER_SIZE 0x1000

#define KD_BREAKPOINT_MAX   32

#define KD_HIGHEST_USER_BREAKPOINT_ADDRESS  (PVOID)0x60000000

#define KD_BREAKPOINT_ACTIVE    0x01

#define KD_BREAKPOINT_PENDING   0x02

#define KD_BREAKPOINT_SUSPENDED 0x04

#define KD_BREAKPOINT_EXPIRED   0x08

typedef struct _BREAKPOINT_ENTRY
{
    ULONG Flags;
    ULONG_PTR DirectoryTableBase;
    PVOID Address;
    KD_BREAKPOINT_TYPE Content;
} BREAKPOINT_ENTRY, *PBREAKPOINT_ENTRY;

typedef
BOOLEAN
(NTAPI *PKDEBUG_ROUTINE)(
    IN PKTRAP_FRAME TrapFrame,
    IN PKEXCEPTION_FRAME ExceptionFrame,
    IN PEXCEPTION_RECORD ExceptionRecord,
    IN PCONTEXT Context,
    IN KPROCESSOR_MODE PreviousMode,
    IN BOOLEAN SecondChance
);

extern CHAR KdpBootLogPath[128];

struct _ROS_STORAGE_DUMP_INTERFACE;

#if (NTDDI_VERSION >= NTDDI_WINBLUE)
NTSTATUS NTAPI KdpWriteLiveKernelDump(_In_ PSYSDBG_LIVEDUMP_CONTROL Control, _In_ KPROCESSOR_MODE PreviousMode);
#endif

#define KD_FILE_REQUEST_PULL    1

#define KD_FILE_REQUEST_RELOAD  2

extern BOOLEAN KdpPrintDeferred;

extern DBGKD_GET_VERSION64 KdVersionBlock;

extern KDDEBUGGER_DATA64 KdDebuggerDataBlock;

extern LIST_ENTRY KdpDebuggerDataListHead;

extern KSPIN_LOCK KdpDataSpinLock;

extern LARGE_INTEGER KdPerformanceCounterRate;

extern LARGE_INTEGER KdTimerStart;

extern ULONG KdDisableCount;

extern KD_CONTEXT KdpContext;

extern PKDEBUG_ROUTINE KiDebugRoutine;

extern BOOLEAN KdBreakAfterSymbolLoad;

extern BOOLEAN KdPitchDebugger;

extern BOOLEAN KdAutoEnableOnEvent;

extern BOOLEAN KdBlockEnable;

extern BOOLEAN KdIgnoreUmExceptions;

extern BOOLEAN KdPreviouslyEnabled;

extern BOOLEAN KdpDebuggerStructuresInitialized;

extern BOOLEAN KdEnteredDebugger;

extern KDPC KdpTimeSlipDpc;

extern KTIMER KdpTimeSlipTimer;

extern WORK_QUEUE_ITEM KdpTimeSlipWorkItem;

extern LONG KdpTimeSlipPending;

extern PKEVENT KdpTimeSlipEvent;

extern KSPIN_LOCK KdpTimeSlipEventLock;

extern BOOLEAN KdpPortLocked;

extern BOOLEAN KdpControlCPressed;

extern BOOLEAN KdpContextSent;

extern KSPIN_LOCK KdpDebuggerLock;

extern volatile PKPRCB KdpPortOwnerPrcb;

extern LARGE_INTEGER KdTimerStop, KdTimerStart, KdTimerDifference;

#if defined(_M_ARM64)
ULONGLONG
NTAPI
KdpQueryDebugTimestamp(VOID);
#else
#define KdpQueryDebugTimestamp() KeQueryInterruptTime()
#endif

extern CHAR KdpMessageBuffer[KDP_MSG_BUFFER_SIZE];

extern CHAR KdpPathBuffer[KDP_MSG_BUFFER_SIZE];

extern CHAR KdPrintDefaultCircularBuffer[KD_DEFAULT_LOG_BUFFER_SIZE];

extern PCHAR KdPrintWritePointer;

extern ULONG KdPrintRolloverCount;

extern PCHAR KdPrintCircularBuffer;

extern ULONG KdPrintBufferSize;

extern ULONG KdPrintBufferChanges;

extern KSPIN_LOCK KdpPrintSpinLock;

extern BREAKPOINT_ENTRY KdpBreakpointTable[KD_BREAKPOINT_MAX];

extern KD_BREAKPOINT_TYPE KdpBreakpointInstruction;

extern BOOLEAN KdpOweBreakpoint;

extern BOOLEAN BreakpointsSuspended;

extern ULONG KdpNumInternalBreakpoints;

extern ULONG_PTR KdpCurrentSymbolStart, KdpCurrentSymbolEnd;

extern ULONG TraceDataBuffer[40];

extern ULONG TraceDataBufferPosition;

#define MAX_KD_COMPONENT_TABLE_ENTRIES  (DPFLTR_ENDOFTABLE_ID + 1)

extern ULONG KdComponentTableSize;

extern PULONG KdComponentTable[MAX_KD_COMPONENT_TABLE_ENTRIES];

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

#define KD_DECLARE_MASK(Name) extern ULONG Kd_##Name##_Mask;
KD_COMPONENTS(KD_DECLARE_MASK)

extern ULONG Kd_WIN2000_Mask;

VOID
NTAPI
KdSystemRootAvailable(VOID);

#ifdef REMOTE_ASSISTANCE
BOOLEAN
NTAPI
KdDisplayTransportKey(
    _In_ BOOLEAN DarkPalette);
#endif

BOOLEAN
NTAPI
KdBugCheckDebuggerRequested(VOID);

BOOLEAN
NTAPI
KdBugCheckBreakInPending(VOID);

VOID
NTAPI
KdEnableBugCheckDebugger(VOID);

BOOLEAN
NTAPI
KdScreenIsBootLog(VOID);

BOOLEAN
NTAPI
KdShowShutdownLog(VOID);

VOID
KdpResetFileIoHost(VOID);

BOOLEAN
KdpFileRequestInProgress(VOID);

NTSTATUS
NTAPI
KdpAllowDisable(VOID);

VOID
NTAPI
KdpGetStateChange(
    _In_ PDBGKD_MANIPULATE_STATE64 State,
    _In_ PCONTEXT Context);

VOID
NTAPI
KdpSetContextState(
    _In_ PDBGKD_ANY_WAIT_STATE_CHANGE WaitStateChange,
    _In_ PCONTEXT Context);

NTSTATUS
NTAPI
KdpSysCheckLowMemory(
    _In_ ULONG Flags);

NTSTATUS
NTAPI
KdpSysReadMsr(
    _In_ ULONG Msr,
    _Out_ PULONGLONG MsrValue);

NTSTATUS
NTAPI
KdpSysWriteMsr(
    _In_ ULONG Msr,
    _In_ PULONGLONG MsrValue);

VOID
NTAPI
KdSetOwedBreakpoints(VOID);

ULONG
NTAPI
KdpAddBreakpoint(
    _In_ PVOID Address);

BOOLEAN
NTAPI
KdpDeleteBreakpoint(
    _In_ ULONG BpEntry);

BOOLEAN
NTAPI
KdpDeleteBreakpointRange(
    _In_ PVOID Base,
    _In_ PVOID Limit);

VOID
NTAPI
KdpRestoreAllBreakpoints(VOID);

VOID
NTAPI
KdpSuspendAllBreakPoints(VOID);

VOID
NTAPI
KdpSuspendBreakPoint(
    _In_ ULONG BpEntry);

NTSTATUS
NTAPI
KdDisableDebuggerWithLock(
    _In_ BOOLEAN NeedLock);

NTSTATUS
NTAPI
KdEnableDebuggerWithLock(
    _In_ BOOLEAN NeedLock);

BOOLEAN
NTAPI
KdEnterDebugger(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ PKEXCEPTION_FRAME ExceptionFrame);

VOID
NTAPI
KdExitDebugger(
    _In_ BOOLEAN Enable);

KCONTINUE_STATUS
NTAPI
KdReportProcessorChange(VOID);

VOID
NTAPI
KdpReportCommandStringStateChange(
    _In_ PSTRING NameString,
    _In_ PSTRING CommandString,
    _Inout_ PCONTEXT Context);

BOOLEAN
NTAPI
KdpReportExceptionStateChange(
    _In_ PEXCEPTION_RECORD ExceptionRecord,
    _Inout_ PCONTEXT Context,
    _In_ BOOLEAN SecondChanceException);

VOID
NTAPI
KdpReportLoadSymbolsStateChange(
    _In_ PSTRING PathName,
    _In_ PKD_SYMBOLS_INFO SymbolInfo,
    _In_ BOOLEAN Unload,
    _Inout_ PCONTEXT Context);

BOOLEAN
NTAPI
KdIsThisAKdTrap(
    _In_ PEXCEPTION_RECORD ExceptionRecord,
    _In_ PCONTEXT Context,
    _In_ KPROCESSOR_MODE PreviousMode);

BOOLEAN
NTAPI
KdpStub(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ PKEXCEPTION_FRAME ExceptionFrame,
    _In_ PEXCEPTION_RECORD ExceptionRecord,
    _In_ PCONTEXT ContextRecord,
    _In_ KPROCESSOR_MODE PreviousMode,
    _In_ BOOLEAN SecondChanceException);

BOOLEAN
NTAPI
KdpTrap(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ PKEXCEPTION_FRAME ExceptionFrame,
    _In_ PEXCEPTION_RECORD ExceptionRecord,
    _In_ PCONTEXT ContextRecord,
    _In_ KPROCESSOR_MODE PreviousMode,
    _In_ BOOLEAN SecondChanceException);

VOID
NTAPI
KdInitializeFileService(VOID);

NTSTATUS
NTAPI
KdPullRemoteFile(
    _In_ PUNICODE_STRING FileName);

NTSTATUS
NTAPI
KdQueueFileRequest(
    _In_ ULONG Kind,
    _In_z_ PCSTR Name);

NTSTATUS
NTAPI
KdpCopyMemoryChunks(
    _In_ ULONG64 Address,
    _In_ PVOID Buffer,
    _In_ ULONG TotalSize,
    _In_ ULONG ChunkSize,
    _In_ ULONG Flags,
    _Out_opt_ PULONG ActualSize);

NTSTATUS
NTAPI
KdpCopyProcessMemoryChunks(
    _In_opt_ PEPROCESS Process,
    _In_ ULONG64 Address,
    _In_ PVOID Buffer,
    _In_ ULONG TotalSize,
    _In_ ULONG ChunkSize,
    _In_ ULONG Flags,
    _Out_opt_ PULONG ActualSize);

VOID
NTAPI
KdpMoveMemory(
    _In_ PVOID Destination,
    _In_ PVOID Source,
    _In_ SIZE_T Length);

VOID
NTAPI
KdpZeroMemory(
    _In_ PVOID Destination,
    _In_ SIZE_T Length);

VOID
NTAPI
KdLogDbgPrint(
    _In_ PSTRING String);

VOID
NTAPI
KdStartPrintWriter(VOID);

VOID
NTAPI
KdStopPrintWriter(VOID);

VOID
NTAPI
KdpCommandString(
    _In_ PSTRING NameString,
    _In_ PSTRING CommandString,
    _In_ KPROCESSOR_MODE PreviousMode,
    _In_ PCONTEXT ContextRecord,
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ PKEXCEPTION_FRAME ExceptionFrame);

VOID
__cdecl
KdpDprintf(
    _In_ PCSTR Format,
    ...);

BOOLEAN
NTAPI
KdpPrintString(
    _In_ PSTRING Output);

VOID
NTAPI
KdpResetPrintLock(VOID);

BOOLEAN
NTAPI
KdpShowPrintLog(VOID);

VOID
NTAPI
KdpSymbol(
    _In_ PSTRING DllPath,
    _In_ PKD_SYMBOLS_INFO SymbolInfo,
    _In_ BOOLEAN Unload,
    _In_ KPROCESSOR_MODE PreviousMode,
    _In_ PCONTEXT ContextRecord,
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ PKEXCEPTION_FRAME ExceptionFrame);

BOOLEAN
NTAPI
KdpPollBreakInWithPortLock(VOID);

VOID
NTAPI
KdpPortLock(VOID);

VOID
NTAPI
KdpPortUnlock(VOID);

VOID
NTAPI
KdpSysGetVersion(
    _Out_ PDBGKD_GET_VERSION64 Version);

BOOLEAN
NTAPI
KdInitSystem(
    _In_ ULONG BootPhase,
    _In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock);

VOID
NTAPI
KdUpdateDataBlock(VOID);

VOID
NTAPI
KdUpdateTimeSlipEvent(
    _In_opt_ PKEVENT Event);

VOID
NTAPI
KdpTimeSlipDpcRoutine(
    _In_ PKDPC Dpc,
    _In_ PVOID DeferredContext,
    _In_ PVOID SystemArgument1,
    _In_ PVOID SystemArgument2);

VOID
NTAPI
KdpTimeSlipWork(
    _In_ PVOID Context);

VOID
NTAPI
KdInitializeBootLog(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock);

ULONG
NTAPI
KdQueryTransportKey(
    _Out_writes_z_(Size) PCHAR Buffer,
    _In_ ULONG Size);

ULONG
NTAPI
KdQueryTransportStatus(
    _Out_writes_z_(Size) PCHAR Buffer,
    _In_ ULONG Size);

VOID
NTAPI
KdShutdownBootLog(VOID);

ULONG
NTAPI
KdpCopyPrintBuffer(
    _Out_writes_bytes_(BufferSize) PCHAR Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG RolloverCount);

ULONG
NTAPI
KdpCopyPrintBufferFrom(
    _Inout_ PULONG64 Position,
    _Out_writes_bytes_(BufferSize) PCHAR Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG64 Lost);

ULONG
NTAPI
KdpCopyPrintBufferTail(
    _Out_writes_bytes_(BufferSize) PCHAR Buffer,
    _In_ ULONG BufferSize);

BOOLEAN
NTAPI
KdpInitializeBootPageFileCrashDump(
    _In_ PCUNICODE_STRING BootDeviceName);

BOOLEAN
NTAPI
KdpInitializeCrashDump(
    _In_ HANDLE DumpFileHandle);

BOOLEAN
NTAPI
KdpInitializeCrashDumpCore(
    _In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock);

BOOLEAN
NTAPI
KdpInitializeDedicatedCrashDump(
    _In_ PFILE_OBJECT BootFileObject);

VOID
NTAPI
KdpLogWatchdogCheck(
    _In_ ULONGLONG CurrentInterruptTime);

VOID
NTAPI
KdpLogWatchdogDisable(VOID);

VOID
NTAPI
KdpLogWatchdogInitialize(
    _In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock);

VOID
NTAPI
KdpLogWatchdogNotePrint(VOID);

VOID
NTAPI
KdpLogWatchdogResume(VOID);

VOID
NTAPI
KdpLogWatchdogSuspend(VOID);

NTSTATUS
NTAPI
KdpPrint(
    _In_ ULONG ComponentId,
    _In_ ULONG Level,
    _In_reads_bytes_(Length) PCHAR String,
    _In_ USHORT Length,
    _In_ KPROCESSOR_MODE PreviousMode,
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ PKEXCEPTION_FRAME ExceptionFrame,
    _Out_ PBOOLEAN Handled);

VOID
NTAPI
KdpPrintToCallbacks(
    _In_ ULONG ComponentId,
    _In_ ULONG Level,
    _In_reads_bytes_(Length) PCHAR String,
    _In_ USHORT Length,
    _In_ KPROCESSOR_MODE PreviousMode);

USHORT
NTAPI
KdpPrompt(
    _In_reads_bytes_(PromptLength) PCHAR PromptString,
    _In_ USHORT PromptLength,
    _Out_writes_bytes_(MaximumResponseLength) PCHAR ResponseString,
    _In_ USHORT MaximumResponseLength,
    _In_ KPROCESSOR_MODE PreviousMode,
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ PKEXCEPTION_FRAME ExceptionFrame);

BOOLEAN
NTAPI
KdpPromptBuffersAccessible(
    _In_reads_bytes_(PromptLength) PCHAR PromptString,
    _In_ USHORT PromptLength,
    _In_ PCHAR ResponseString,
    _In_ USHORT MaximumResponseLength,
    _In_ KPROCESSOR_MODE PreviousMode);

VOID
NTAPI
KdpPublishCrashDumpArtifacts(VOID);

BOOLEAN
NTAPI
KdpReleaseCrashDumpFile(VOID);

NTSTATUS
NTAPI
KdpSysReadBusData(
    _In_ BUS_DATA_TYPE BusDataType,
    _In_ ULONG BusNumber,
    _In_ ULONG SlotNumber,
    _In_ ULONG Offset,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length,
    _Out_ PULONG ActualLength);

NTSTATUS
NTAPI
KdpSysReadControlSpace(
    _In_ ULONG Processor,
    _In_ ULONG64 BaseAddress,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length,
    _Out_ PULONG ActualLength);

NTSTATUS
NTAPI
KdpSysReadIoSpace(
    _In_ INTERFACE_TYPE InterfaceType,
    _In_ ULONG BusNumber,
    _In_ ULONG AddressSpace,
    _In_ ULONG64 IoAddress,
    _Out_writes_bytes_(DataSize) PVOID DataValue,
    _In_ ULONG DataSize,
    _Out_ PULONG ActualDataSize);

NTSTATUS
NTAPI
KdpSysWriteBusData(
    _In_ BUS_DATA_TYPE BusDataType,
    _In_ ULONG BusNumber,
    _In_ ULONG SlotNumber,
    _In_ ULONG Offset,
    _In_reads_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length,
    _Out_ PULONG ActualLength);

NTSTATUS
NTAPI
KdpSysWriteControlSpace(
    _In_ ULONG Processor,
    _In_ ULONG64 BaseAddress,
    _In_reads_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length,
    _Out_ PULONG ActualLength);

NTSTATUS
NTAPI
KdpSysWriteIoSpace(
    _In_ INTERFACE_TYPE InterfaceType,
    _In_ ULONG BusNumber,
    _In_ ULONG AddressSpace,
    _In_ ULONG64 IoAddress,
    _In_reads_bytes_(DataSize) PVOID DataValue,
    _In_ ULONG DataSize,
    _Out_ PULONG ActualDataSize);

NTSTATUS
NTAPI
KdpWriteBootLogAtCrash(
    _In_opt_ struct _ROS_STORAGE_DUMP_INTERFACE *Storage,
    _In_opt_ PFILE_OBJECT DumpFileObject);

NTSTATUS
NTAPI
KdpWriteCrashDump(VOID);

FORCEINLINE
BOOLEAN
KdpDebuggerLockOwnedByCurrentThread(VOID)
{
#if DBG
    return (((KSPIN_LOCK)KeGetCurrentThread() | 1) == KdpDebuggerLock);
#else
    return FALSE;
#endif
}
