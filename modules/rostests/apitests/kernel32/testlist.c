
#define STANDALONE
#include <apitest.h>

extern void func_ActCtxWithXmlNamespaces(void);
extern void func_ActCtxDependencyVersions(void);
extern void func_AlternateComputerNames(void);
extern void func_UniversalThunks(void);
extern void func_LoadAppInitDlls(void);
extern void func_ComPlusPackageInstallStatus(void);
extern void func_IntegratedDisplaySize(void);
extern void func_MessageWaitingIndicator(void);
extern void func_TermsrvAppInstallMode(void);
extern void func_ApiSetCoreSynch(void);
extern void func_ApplicationRecovery(void);
extern void func_ApplicationRestart(void);
extern void func_AuditAlarmApis(void);
extern void func_BaseSetLastNTError(void);
extern void func_BaseThreadInitThunk(void);
extern void func_BackupReadWrite(void);
extern void func_CalendarDateTime(void);
extern void func_CallbackCallingConventions(void);
extern void func_CallbackMayRunLong(void);
extern void func_Arm64ThreadContext(void);
extern void func_ConsoleAlias(void);
extern void func_ConsoleCommandHistory(void);
extern void func_ConsoleCP(void);
extern void func_ConsoleFontInfo(void);
extern void func_ConsoleInputSignal(void);
extern void func_ConsoleRequestStress(void);
extern void func_ConsoleDbcsCells(void);
extern void func_ConsoleProcessInheritance(void);
extern void func_ConsoleVirtualTerminal(void);
extern void func_CopyContext(void);
extern void func_CpuSetsApi(void);
extern void func_CreateAfterMissingName(void);
extern void func_CreateDirectoryRoot(void);
extern void func_CreateProcess(void);
extern void func_DefaultActCtx(void);
extern void func_DeleteFileCase(void);
extern void func_DeviceIoControl(void);
extern void func_dosdev(void);
extern void func_EnumDateTimeFormats(void);
extern void func_EnumSystemCodePages(void);
extern void func_FindActCtxSectionStringW(void);
extern void func_FileStorageInfo(void);
extern void func_FileIoOverlappedRange(void);
extern void func_FindFileNames(void);
extern void func_FileInfoByHandleEx(void);
extern void func_FileObjectId(void);
extern void func_FindFiles(void);
extern void func_FindPackagesByPackageFamily(void);
extern void func_FLS(void);
extern void func_FormatMessage(void);
extern void func_GetComputerNameEx(void);
extern void func_GetCPInfo(void);
extern void func_GetCurrentDirectory(void);
extern void func_GetCurrentThreadStackLimits(void);
extern void func_GetDriveType(void);
extern void func_GetEnvironmentVariable(void);
extern void func_GetFinalPathNameByHandle(void);
extern void func_GetLocaleInfo(void);
extern void func_GetModuleFileName(void);
extern void func_GetNumberOfConsoleMouseButtons(void);
extern void func_GetNumaNodeProcessorMaskEx(void);
extern void func_GetOsSafeBootMode(void);
extern void func_GetPackageFamilyName(void);
extern void func_GetProcessorSystemCycleTime(void);
extern void func_AppModelInternal(void);
extern void func_GetSystemWow64Directory(void);
extern void func_GetTempFileName(void);
extern void func_GetTempPath2(void);
extern void func_GetThreadTimes(void);
extern void func_GetUILanguageInfo(void);
extern void func_GetVolumeInformation(void);
extern void func_GetWsChangesEx(void);
extern void func_HeapLowFragmentation(void);
extern void func_HighResolutionTimerClock(void);
extern void func_HeapSummary(void);
extern void func_IdleClockAccounting(void);
extern void func_InitOnce(void);
extern void func_interlck(void);
extern void func_IoCompletion(void);
extern void func_CancelIoEx(void);
extern void func_IsDBCSLeadByteEx(void);
extern void func_JapaneseCalendar(void);
extern void func_JobObject(void);
extern void func_JobObjectUI(void);
extern void func_KernelBaseRegistry(void);
extern void func_KernelBaseUrl(void);
extern void func_KnownDlls(void);
extern void func_LCMapString(void);
extern void func_LoadLibraryExW(void);
extern void func_LockFileClose(void);
extern void func_LockFileEx(void);
extern void func_QuantumRoundRobin(void);
extern void func_RegisterWaitPeriodic(void);
extern void func_ProcessMitigationPolicy(void);
extern void func_ProcessWorkingSetSize(void);
extern void func_RemoteThreadInit(void);
extern void func_SandboxLaunch(void);
extern void func_SecurityBaseApis(void);
extern void func_SystemImageCatalog(void);
extern void func_SystemTimeChangeTimers(void);
extern void func_TzSpecificLocalTimeEx(void);
extern void func_TebSegmentAfterWait(void);
extern void func_SharedMemorySecurity(void);
extern void func_SharedMemoryTransfer(void);
extern void func_StarvationBoost(void);
extern void func_StdHandleInheritance(void);
extern void func_StreamRename(void);
extern void func_PosixRename(void);
extern void func_LocaleNameToLCID(void);
extern void func_lstrcpynW(void);
extern void func_lstrlen(void);
extern void func_MailslotRead(void);
extern void func_MuiResources(void);
extern void func_MultiByteToWideChar(void);
extern void func_NamedPipeClientComputerName(void);
extern void func_NlsStringApis(void);
extern void func_NumaApis(void);
extern void func_PerfConsumer(void);
extern void func_PerfProvider(void);
extern void func_Pipes(void);
extern void func_PackageIdentity(void);
extern void func_PackageNames(void);
extern void func_PixeloramaCompat(void);
extern void func_ProcessAffinityUpdateMode(void);
extern void func_ProcessPreferredUILanguages(void);
extern void func_PseudoConsoleCtrlC(void);
extern void func_PseudoConsoleVtInput(void);
extern void func_PssSnapshot(void);
extern void func_ResourceUILanguage(void);
extern void func_OpenFileByIdRename(void);
extern void func_LoaderLock(void);
extern void func_PagingFileOpen(void);
extern void func_PrivMoveFileIdentityW(void);
extern void func_RenameAsyncHandle(void);
extern void func_RenameDirectoryOldPath(void);
extern void func_QueueUserAPC(void);
extern void func_QueryProcessCycleTime(void);
extern void func_ResizePseudoConsole(void);
extern void func_SetCalendarInfo(void);
extern void func_SetComputerNameExW(void);
extern void func_SetConsoleWindowInfo(void);
extern void func_SetCurrentDirectory(void);
extern void func_SetFileAllocationInfo(void);
extern void func_SetStdHandleEx(void);
extern void func_SecureMemoryCache(void);
extern void func_SetThreadStackGuarantee(void);
extern void func_SetUnhandledExceptionFilter(void);
extern void func_SetWaitableTimerEx(void);
extern void func_WaitableTimerHighResolution(void);
extern void func_WaitableTimerApcReset(void);
extern void func_WaitableTimerCancelAtApc(void);
extern void func_WaitableTimerCloseAtExpiry(void);
extern void func_WaitableTimerSignalTime(void);
extern void func_WaitableTimerThreadExit(void);
extern void func_SwitchToThreadCost(void);
extern void func_SuspendThread(void);
extern void func_SynchronizationBarrier(void);
extern void func_SystemFirmware(void);
extern void func_SystemFirmwareAcpi(void);
extern void func_ProcessTokenSession(void);
extern void func_LimitedProcessQueries(void);
extern void func_SystemFileCacheSize(void);
extern void func_SecureBootFlag(void);
extern void func_TerminateProcess(void);
extern void func_TerminateThread(void);
extern void func_TlsFree(void);
extern void func_ExitedClientId(void);
extern void func_ExitedProcessImage(void);
extern void func_ThreadKernelStack(void);
extern void func_SleepTickRounding(void);
extern void func_SwitchToThreadPriority(void);
extern void func_QueryInterruptTimePrecise(void);
extern void func_ProcessAffinityQueued(void);
extern void func_SwitchToThreadRoundRobin(void);
extern void func_YieldStealSwitch(void);
extern void func_ThreadPowerThrottling(void);
extern void func_ThreadPreferredUILanguages(void);
extern void func_TimeZoneKeepsTime(void);
extern void func_ProcessPowerThrottling(void);
extern void func_TransactedFileApis(void);
extern void func_ProcessorEfficiency(void);
extern void func_TunnelCache(void);
extern void func_TimerResolutionSleep(void);
extern void func_ThreadContextInCallback(void);
extern void func_UEFIFirmware(void);
extern void func_VolumeMountPoints(void);
extern void func_WaitFileHandle(void);
extern void func_WaitForMultipleObjects(void);
extern void func_WerRegisterFile(void);
extern void func_WerRegistration(void);
extern void func_WerCrashReport(void);
extern void func_WerHangReport(void);
extern void func_WerReport(void);
extern void func_WideCharToMultiByte(void);
extern void func_WinSaferRestrictions(void);
extern void func_Wow64GetThreadContext(void);
extern void func_Wow64ChildProcess(void);
extern void func_Wow64SuspendThread(void);
extern void func_WriteFileBadBuffer(void);
extern void func_WriteFileSeekBack(void);
extern void func_XState(void);

const struct test winetest_testlist[] =
{
    { "SetFileAllocationInfo", func_SetFileAllocationInfo },
    { "SetStdHandleEx",              func_SetStdHandleEx },
    { "SecureMemoryCache",           func_SecureMemoryCache },
    { "ActCtxWithXmlNamespaces",     func_ActCtxWithXmlNamespaces },
    { "ActCtxDependencyVersions",    func_ActCtxDependencyVersions },
    { "AlternateComputerNames",      func_AlternateComputerNames },
    { "UniversalThunks",              func_UniversalThunks },
    { "LoadAppInitDlls",              func_LoadAppInitDlls },
    { "ComPlusPackageInstallStatus",  func_ComPlusPackageInstallStatus },
    { "IntegratedDisplaySize",        func_IntegratedDisplaySize },
    { "MessageWaitingIndicator",      func_MessageWaitingIndicator },
    { "TermsrvAppInstallMode",        func_TermsrvAppInstallMode },
    { "ApiSetCoreSynch",             func_ApiSetCoreSynch },
    { "ApplicationRecovery",         func_ApplicationRecovery },
    { "ApplicationRestart",          func_ApplicationRestart },
    { "AuditAlarmApis",              func_AuditAlarmApis },
    { "BaseSetLastNTError",          func_BaseSetLastNTError },
    { "BaseThreadInitThunk",         func_BaseThreadInitThunk },
    { "BackupReadWrite",             func_BackupReadWrite },
    { "CalendarDateTime",            func_CalendarDateTime },
    { "CallbackCallingConventions", func_CallbackCallingConventions },
    { "CallbackMayRunLong",          func_CallbackMayRunLong },
    { "Arm64ThreadContext",          func_Arm64ThreadContext },
    { "ConsoleAlias", func_ConsoleAlias },
    { "ConsoleCommandHistory",     func_ConsoleCommandHistory },
    { "ConsoleCP",                   func_ConsoleCP },
    { "ConsoleFontInfo",           func_ConsoleFontInfo },
    { "ConsoleInputSignal",          func_ConsoleInputSignal },
    { "ConsoleRequestStress",        func_ConsoleRequestStress },
    { "ConsoleDbcsCells",            func_ConsoleDbcsCells },
    { "ConsoleProcessInheritance",   func_ConsoleProcessInheritance },
    { "ConsoleVirtualTerminal",      func_ConsoleVirtualTerminal },
    { "CopyContext",                 func_CopyContext },
    { "CpuSetsApi",                  func_CpuSetsApi },
    { "CreateAfterMissingName",      func_CreateAfterMissingName },
    { "CreateDirectoryRoot",         func_CreateDirectoryRoot },
    { "CreateProcess",               func_CreateProcess },
    { "DefaultActCtx",               func_DefaultActCtx },
    { "DeleteFileCase",              func_DeleteFileCase },
    { "DeviceIoControl",             func_DeviceIoControl },
    { "dosdev",                      func_dosdev },
    { "EnumDateTimeFormats",         func_EnumDateTimeFormats },
    { "EnumSystemCodePages",         func_EnumSystemCodePages },
    { "FindActCtxSectionStringW",    func_FindActCtxSectionStringW },
    { "FileStorageInfo",             func_FileStorageInfo },
    { "FileIoOverlappedRange",       func_FileIoOverlappedRange },
    { "FindFileNames",               func_FindFileNames },
    { "FileInfoByHandleEx",          func_FileInfoByHandleEx },
    { "FileObjectId",                func_FileObjectId },
    { "FindFiles",                   func_FindFiles },
    { "FindPackagesByPackageFamily", func_FindPackagesByPackageFamily },
    { "FLS",                         func_FLS },
    { "FormatMessage",               func_FormatMessage },
    { "GetComputerNameEx",           func_GetComputerNameEx },
    { "GetCPInfo",                   func_GetCPInfo },
    { "GetCurrentDirectory",         func_GetCurrentDirectory },
    { "GetCurrentThreadStackLimits", func_GetCurrentThreadStackLimits },
    { "GetDriveType",                func_GetDriveType },
    { "GetEnvironmentVariable",      func_GetEnvironmentVariable },
    { "GetFinalPathNameByHandle",    func_GetFinalPathNameByHandle },
    { "GetLocaleInfo",               func_GetLocaleInfo },
    { "GetModuleFileName",           func_GetModuleFileName },
    { "GetNumberOfConsoleMouseButtons",  func_GetNumberOfConsoleMouseButtons },
    { "GetNumaNodeProcessorMaskEx",  func_GetNumaNodeProcessorMaskEx },
    { "GetOsSafeBootMode",           func_GetOsSafeBootMode },
    { "GetPackageFamilyName",        func_GetPackageFamilyName },
    { "GetProcessorSystemCycleTime", func_GetProcessorSystemCycleTime },
    { "AppModelInternal",            func_AppModelInternal },
    { "GetSystemWow64Directory",     func_GetSystemWow64Directory },
    { "GetTempFileName",             func_GetTempFileName },
    { "GetTempPath2",                func_GetTempPath2 },
    { "GetThreadTimes",              func_GetThreadTimes },
    { "GetUILanguageInfo",           func_GetUILanguageInfo },
    { "GetVolumeInformation",        func_GetVolumeInformation },
    { "GetWsChangesEx",              func_GetWsChangesEx },
    { "HeapLowFragmentation",        func_HeapLowFragmentation },
    { "HighResolutionTimerClock",    func_HighResolutionTimerClock },
    { "HeapSummary",                 func_HeapSummary },
    { "IdleClockAccounting",         func_IdleClockAccounting },
    { "InitOnce",                    func_InitOnce },
    { "interlck",                    func_interlck },
    { "IoCompletion",                func_IoCompletion },
    { "CancelIoEx",                  func_CancelIoEx },
    { "IsDBCSLeadByteEx",            func_IsDBCSLeadByteEx },
    { "JapaneseCalendar",            func_JapaneseCalendar },
    { "JobObject",                   func_JobObject },
    { "JobObjectUI",                 func_JobObjectUI },
    { "KernelBaseRegistry",          func_KernelBaseRegistry },
    { "KernelBaseUrl",               func_KernelBaseUrl },
    { "KnownDlls",                   func_KnownDlls },
    { "LCMapString",                 func_LCMapString },
    { "LoadLibraryExW",              func_LoadLibraryExW },
    { "LockFileClose",               func_LockFileClose },
    { "LockFileEx",                  func_LockFileEx },
    { "SystemImageCatalog",            func_SystemImageCatalog },
    { "SystemTimeChangeTimers",      func_SystemTimeChangeTimers },
    { "TzSpecificLocalTimeEx",       func_TzSpecificLocalTimeEx },
    { "TebSegmentAfterWait",           func_TebSegmentAfterWait },
    { "QuantumRoundRobin",           func_QuantumRoundRobin },
    { "RegisterWaitPeriodic",        func_RegisterWaitPeriodic },
    { "ProcessMitigationPolicy",     func_ProcessMitigationPolicy },
    { "ProcessWorkingSetSize",       func_ProcessWorkingSetSize },
    { "RemoteThreadInit",            func_RemoteThreadInit },
    { "SandboxLaunch",               func_SandboxLaunch },
    { "SecurityBaseApis",            func_SecurityBaseApis },
    { "SharedMemorySecurity",        func_SharedMemorySecurity },
    { "SharedMemoryTransfer",        func_SharedMemoryTransfer },
    { "StarvationBoost",             func_StarvationBoost },
    { "StdHandleInheritance",        func_StdHandleInheritance },
    { "StreamRename",                func_StreamRename },
    { "PosixRename",                 func_PosixRename },
    { "LocaleNameToLCID",            func_LocaleNameToLCID },
    { "lstrcpynW",                   func_lstrcpynW },
    { "lstrlen",                     func_lstrlen },
    { "MailslotRead",                func_MailslotRead },
    { "MuiResources",                func_MuiResources },
    { "MultiByteToWideChar",         func_MultiByteToWideChar },
    { "NamedPipeClientComputerName", func_NamedPipeClientComputerName },
    { "NlsStringApis",               func_NlsStringApis },
    { "NumaApis",                    func_NumaApis },
    { "PerfConsumer",              func_PerfConsumer },
    { "PerfProvider",              func_PerfProvider },
    { "Pipes",                       func_Pipes },
    { "PackageIdentity",             func_PackageIdentity },
    { "PackageNames",                func_PackageNames },
    { "PixeloramaCompat",            func_PixeloramaCompat },
    { "ProcessAffinityUpdateMode",   func_ProcessAffinityUpdateMode },
    { "ProcessPreferredUILanguages", func_ProcessPreferredUILanguages },
    { "PseudoConsoleCtrlC",          func_PseudoConsoleCtrlC },
    { "PseudoConsoleVtInput",        func_PseudoConsoleVtInput },
    { "PssSnapshot",                 func_PssSnapshot },
    { "ResourceUILanguage",          func_ResourceUILanguage },
    { "OpenFileByIdRename",          func_OpenFileByIdRename },
    { "LoaderLock",                  func_LoaderLock },
    { "PagingFileOpen",              func_PagingFileOpen },
    { "PrivMoveFileIdentityW",       func_PrivMoveFileIdentityW },
    { "RenameAsyncHandle",           func_RenameAsyncHandle },
    { "RenameDirectoryOldPath",      func_RenameDirectoryOldPath },
    { "QueueUserAPC",                func_QueueUserAPC },
    { "QueryProcessCycleTime",       func_QueryProcessCycleTime },
    { "ResizePseudoConsole",         func_ResizePseudoConsole },
    { "SetCalendarInfo",             func_SetCalendarInfo },
    { "SetComputerNameExW",          func_SetComputerNameExW },
    { "SetConsoleWindowInfo",        func_SetConsoleWindowInfo },
    { "SetCurrentDirectory",         func_SetCurrentDirectory },
    { "SetThreadStackGuarantee",     func_SetThreadStackGuarantee },
    { "SetUnhandledExceptionFilter", func_SetUnhandledExceptionFilter },
    { "SetWaitableTimerEx",          func_SetWaitableTimerEx },
    { "WaitableTimerHighResolution", func_WaitableTimerHighResolution },
    { "WaitableTimerApcReset",       func_WaitableTimerApcReset },
    { "WaitableTimerCancelAtApc",    func_WaitableTimerCancelAtApc },
    { "WaitableTimerCloseAtExpiry",  func_WaitableTimerCloseAtExpiry },
    { "WaitableTimerSignalTime",     func_WaitableTimerSignalTime },
    { "WaitableTimerThreadExit",     func_WaitableTimerThreadExit },
    { "SwitchToThreadCost", func_SwitchToThreadCost },
    { "SuspendThread",               func_SuspendThread },
    { "SynchronizationBarrier",      func_SynchronizationBarrier },
    { "SystemFirmware",              func_SystemFirmware },
    { "SystemFirmwareAcpi",          func_SystemFirmwareAcpi },
    { "ProcessTokenSession",         func_ProcessTokenSession },
    { "LimitedProcessQueries",       func_LimitedProcessQueries },
    { "SystemFileCacheSize",         func_SystemFileCacheSize },
    { "SecureBootFlag",              func_SecureBootFlag },
    { "TerminateProcess",            func_TerminateProcess },
    { "TerminateThread",             func_TerminateThread },
    { "TlsFree",                     func_TlsFree },
    { "ExitedClientId",              func_ExitedClientId },
    { "ExitedProcessImage",          func_ExitedProcessImage },
    { "ThreadKernelStack",           func_ThreadKernelStack },
    { "SleepTickRounding",           func_SleepTickRounding },
    { "SwitchToThreadPriority",      func_SwitchToThreadPriority },
    { "QueryInterruptTimePrecise",   func_QueryInterruptTimePrecise },
    { "ProcessAffinityQueued",       func_ProcessAffinityQueued },
    { "SwitchToThreadRoundRobin",    func_SwitchToThreadRoundRobin },
    { "YieldStealSwitch",            func_YieldStealSwitch },
    { "ThreadPowerThrottling",       func_ThreadPowerThrottling },
    { "ThreadPreferredUILanguages",  func_ThreadPreferredUILanguages },
    { "TimeZoneKeepsTime",           func_TimeZoneKeepsTime },
    { "ProcessPowerThrottling",      func_ProcessPowerThrottling },
    { "TransactedFileApis",          func_TransactedFileApis },
    { "ProcessorEfficiency",         func_ProcessorEfficiency },
    { "TunnelCache",                 func_TunnelCache },
    { "TimerResolutionSleep",        func_TimerResolutionSleep },
    { "ThreadContextInCallback",     func_ThreadContextInCallback },
    { "UEFIFirmware",                func_UEFIFirmware },
    { "VolumeMountPoints",           func_VolumeMountPoints },
    { "WaitFileHandle",              func_WaitFileHandle },
    { "WaitForMultipleObjects",      func_WaitForMultipleObjects },
    { "WerRegisterFile",             func_WerRegisterFile },
    { "WerRegistration",             func_WerRegistration },
    { "WerCrashReport",              func_WerCrashReport },
    { "WerHangReport",               func_WerHangReport },
    { "WerReport",                   func_WerReport },
    { "WideCharToMultiByte",         func_WideCharToMultiByte },
    { "WinSaferRestrictions",        func_WinSaferRestrictions },
    { "Wow64ChildProcess",           func_Wow64ChildProcess },
    { "Wow64GetThreadContext",       func_Wow64GetThreadContext },
    { "Wow64SuspendThread",          func_Wow64SuspendThread },
    { "WriteFileBadBuffer",          func_WriteFileBadBuffer },
    { "WriteFileSeekBack",           func_WriteFileSeekBack },
    { "XState",                      func_XState },
    { 0, 0 }
};
