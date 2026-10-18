@ stdcall AccessCheck(ptr long long ptr ptr ptr ptr ptr)
@ stdcall AccessCheckAndAuditAlarmW(wstr ptr wstr wstr ptr long ptr long ptr ptr ptr)
@ stdcall AccessCheckByType(ptr ptr long long ptr long ptr ptr ptr ptr ptr)
@ stdcall AccessCheckByTypeAndAuditAlarmW(wstr ptr wstr wstr ptr ptr long long long ptr long ptr long ptr ptr ptr) advapi32.AccessCheckByTypeAndAuditAlarmW
@ stdcall AccessCheckByTypeResultList(ptr ptr long long ptr long ptr ptr ptr ptr ptr)
@ stdcall AccessCheckByTypeResultListAndAuditAlarmByHandleW(wstr ptr ptr wstr wstr ptr long long long long ptr long ptr long ptr ptr ptr) advapi32.AccessCheckByTypeResultListAndAuditAlarmByHandleW
@ stdcall AccessCheckByTypeResultListAndAuditAlarmW(wstr ptr wstr wstr ptr long long long long ptr long ptr long ptr ptr ptr) advapi32.AccessCheckByTypeResultListAndAuditAlarmW
@ stdcall -version=0x600+ AcquireSRWLockExclusive(ptr) ntdll.RtlAcquireSRWLockExclusive
@ stdcall -version=0x600+ AcquireSRWLockShared(ptr) ntdll.RtlAcquireSRWLockShared
@ stdcall ActivateActCtx(ptr ptr) kernel32.ActivateActCtx
@ stdcall AddAccessAllowedAce(ptr long long ptr)
@ stdcall AddAccessAllowedAceEx(ptr long long long ptr)
@ stdcall AddAccessAllowedObjectAce(ptr long long long ptr ptr ptr)
@ stdcall AddAccessDeniedAce(ptr long long ptr)
@ stdcall AddAccessDeniedAceEx(ptr long long long ptr)
@ stdcall AddAccessDeniedObjectAce(ptr long long long ptr ptr ptr)
@ stdcall AddAce(ptr long long ptr long)
@ stdcall AddAuditAccessAce(ptr long long ptr long long)
@ stdcall AddAuditAccessAceEx(ptr long long long ptr long long)
@ stdcall AddAuditAccessObjectAce(ptr long long long ptr ptr ptr long long)
@ stdcall AddConsoleAliasA(str str str)
@ stdcall AddConsoleAliasW(wstr wstr wstr)
@ stdcall AddDependencyToProcessPackageGraph(wstr ptr ptr ptr)
@ stdcall -version=0x601+ AddDllDirectory(wstr) kernel32.AddDllDirectory
@ stdcall AddMandatoryAce(ptr long long long ptr)
@ stdcall AddRefActCtx(ptr) kernel32.AddRefActCtx
@ stdcall AddResourceAttributeAce(ptr long long long ptr ptr ptr)
@ stdcall AddSIDToBoundaryDescriptor(ptr ptr) kernel32.AddSIDToBoundaryDescriptor
@ stdcall AddIntegrityLabelToBoundaryDescriptor(ptr ptr) kernel32.AddIntegrityLabelToBoundaryDescriptor
@ stdcall AddScopedPolicyIDAce(ptr long long long ptr)
@ stdcall AddVectoredContinueHandler(long ptr) ntdll.RtlAddVectoredContinueHandler
@ stdcall AddVectoredExceptionHandler(long ptr) ntdll.RtlAddVectoredExceptionHandler
@ stdcall AdjustTokenGroups(long long ptr long ptr ptr)
@ stdcall AdjustTokenPrivileges(long long ptr long ptr ptr)
@ stdcall AllocConsole()
@ stdcall AllocateAndInitializeSid(ptr long long long long long long long long long ptr)
@ stdcall AllocateLocallyUniqueId(ptr)
@ stdcall AllocateUserPhysicalPages(long ptr ptr) kernel32.AllocateUserPhysicalPages
@ stdcall -version=0x600+ AllocateUserPhysicalPagesNuma(long ptr ptr long) kernel32.AllocateUserPhysicalPagesNuma
@ stdcall AppContainerFreeMemory(ptr)
@ stdcall AppContainerLookupMoniker(ptr ptr)
@ stdcall AppContainerRegisterSid(ptr wstr wstr)
@ stdcall AppContainerUnregisterSid(ptr)
@ stdcall AppPolicyGetMediaFoundationCodecLoading(ptr ptr)
@ stdcall AppPolicyGetProcessTerminationMethod(ptr ptr)
@ stdcall AppPolicyGetShowDeveloperDiagnostic(ptr ptr)
@ stdcall AppPolicyGetThreadInitializationType(ptr ptr)
@ stdcall AppPolicyGetWindowingModel(ptr ptr)
@ stdcall AppXFreeMemory(ptr)
@ stdcall AppXGetPackageSid(wstr ptr)
@ stdcall AreAllAccessesGranted(long long)
@ stdcall AreAnyAccessesGranted(long long)
@ stdcall AreFileApisANSI() kernel32.AreFileApisANSI
@ stdcall AttachConsole(long)
@ stdcall BaseCheckAppcompatCache(wstr ptr wstr ptr) kernel32.BaseCheckAppcompatCache
@ stdcall BaseCleanupAppcompatCacheSupport(ptr) kernel32.BaseCleanupAppcompatCacheSupport
@ stub BaseDllFreeResourceId
@ stub BaseDllMapResourceIdW
@ stdcall BaseDumpAppcompatCache() kernel32.BaseDumpAppcompatCache
@ stdcall BaseFlushAppcompatCache() kernel32.BaseFlushAppcompatCache
@ stdcall BaseFreeAppCompatDataForProcess(ptr) kernel32.BaseFreeAppCompatDataForProcess
@ stdcall BaseGetNamedObjectDirectory(ptr) kernel32.BaseGetNamedObjectDirectory
@ stdcall BaseInitAppcompatCacheSupport() kernel32.BaseInitAppcompatCacheSupport
@ stdcall BaseReadAppCompatDataForProcess(ptr ptr ptr) kernel32.BaseReadAppCompatDataForProcess
@ stdcall BaseUpdateAppcompatCache(long long long) kernel32.BaseUpdateAppcompatCache
@ stdcall BasepAdjustObjectAttributesForPrivateNamespace(ptr) kernel32.BasepAdjustObjectAttributesForPrivateNamespace
@ stdcall Beep(long long) kernel32.Beep
@ stdcall CallNamedPipeW(wstr ptr long ptr long ptr long) kernel32.CallNamedPipeW
@ stdcall CallbackMayRunLong(ptr)
@ stdcall CancelIo(long) kernel32.CancelIo
@ stdcall -version=0x600+ CancelIoEx(ptr ptr) kernel32.CancelIoEx
@ stdcall CancelSynchronousIo(long)
@ stdcall CancelThreadpoolIo(ptr) ntdll.TpCancelAsyncIoOperation
@ stdcall CancelWaitableTimer(long) kernel32.CancelWaitableTimer
@ stdcall ChangeTimerQueueTimer(ptr ptr long long) kernel32.ChangeTimerQueueTimer
@ stdcall CharLowerA(str)
@ stdcall CharLowerBuffA(str long)
@ stdcall CharLowerBuffW(wstr long)
@ stdcall CharLowerW(wstr)
@ stdcall CharNextA(str)
@ stdcall CharNextExA(long str long)
@ stdcall CharNextW(wstr)
@ stdcall CharPrevA(str str)
@ stdcall CharPrevExA(long str str long)
@ stdcall CharPrevW(wstr wstr)
@ stdcall CharUpperA(str)
@ stdcall CharUpperBuffA(str long)
@ stdcall CharUpperBuffW(wstr long)
@ stdcall CharUpperW(wstr)
@ stub CheckGroupPolicyEnabled
@ stdcall CheckRemoteDebuggerPresent(long ptr) kernel32.CheckRemoteDebuggerPresent
@ stdcall CheckTokenCapability(ptr ptr ptr)
@ stdcall CheckTokenMembership(long ptr ptr)
@ stdcall CheckTokenMembershipEx(ptr ptr long ptr)
@ stdcall ChrCmpIA(long long)
@ stdcall ChrCmpIW(long long)
@ stdcall ClearCommBreak(long) kernel32.ClearCommBreak
@ stdcall ClearCommError(long ptr ptr) kernel32.ClearCommError
@ stdcall CloseHandle(long) kernel32.CloseHandle
@ stdcall ClosePrivateNamespace(ptr long) kernel32.ClosePrivateNamespace
@ stdcall ClosePseudoConsole(ptr)
@ stdcall CloseThreadpool(ptr) ntdll.TpReleasePool
@ stdcall CloseThreadpoolCleanupGroup(ptr) ntdll.TpReleaseCleanupGroup
@ stdcall CloseThreadpoolCleanupGroupMembers(ptr long ptr) ntdll.TpReleaseCleanupGroupMembers
@ stdcall CloseThreadpoolIo(ptr) ntdll.TpReleaseIoCompletion
@ stdcall CloseThreadpoolTimer(ptr) ntdll.TpReleaseTimer
@ stdcall CloseThreadpoolWait(ptr) ntdll.TpReleaseWait
@ stdcall CloseThreadpoolWork(ptr) ntdll.TpReleaseWork
@ stdcall CompareFileTime(ptr ptr) kernel32.CompareFileTime
@ stdcall -version=0xA00+ CompareObjectHandles(ptr ptr) kernel32.CompareObjectHandles
@ stdcall CompareStringA(long long str long str long)
@ stdcall CompareStringEx(wstr long wstr long wstr long ptr ptr long)
@ stdcall CompareStringOrdinal(wstr long wstr long long)
@ stdcall CompareStringW(long long wstr long wstr long)
@ stdcall ConnectNamedPipe(long ptr) kernel32.ConnectNamedPipe
@ stdcall ContinueDebugEvent(long long long) kernel32.ContinueDebugEvent
@ stdcall ConvertDefaultLocale(long) kernel32.ConvertDefaultLocale
@ stdcall ConvertFiberToThread() kernel32.ConvertFiberToThread
@ stdcall ConvertThreadToFiber(ptr) kernel32.ConvertThreadToFiber
@ stdcall ConvertThreadToFiberEx(ptr long) kernel32.ConvertThreadToFiberEx
@ stdcall ConvertToAutoInheritPrivateObjectSecurity(ptr ptr ptr ptr long ptr)
@ stdcall -arch=win64 CopyContext(ptr long ptr)
@ stdcall -version=0x602+ CopyFile2(wstr wstr ptr) kernel32.CopyFile2
@ stdcall CopyFileExW(wstr wstr ptr ptr ptr long) kernel32.CopyFileExW
@ stdcall CopyFileW(wstr wstr long) kernel32.CopyFileW
@ stdcall -arch=x86_64 CopyMemoryNonTemporal(ptr ptr long) ntdll.RtlCopyMemoryNonTemporal
@ stdcall CopySid(long ptr ptr)
@ stdcall CreateActCtxW(ptr) kernel32.CreateActCtxW
@ stdcall CreateBoundaryDescriptorW(wstr long) kernel32.CreateBoundaryDescriptorW
@ stdcall CreateConsoleScreenBuffer(long long ptr long ptr)
@ stdcall CreateDirectoryA(str ptr) kernel32.CreateDirectoryA
@ stdcall CreateDirectoryExW(wstr wstr ptr) kernel32.CreateDirectoryExW
@ stdcall CreateDirectoryW(wstr ptr) kernel32.CreateDirectoryW
@ stdcall CreateEventA(ptr long long str) kernel32.CreateEventA
@ stdcall CreateEventExA(ptr str long long)
@ stdcall CreateEventExW(ptr wstr long long)
@ stdcall CreateEventW(ptr long long wstr) kernel32.CreateEventW
@ stdcall CreateFiber(long ptr ptr) kernel32.CreateFiber
@ stdcall CreateFiberEx(long long long ptr ptr) kernel32.CreateFiberEx
@ stdcall -version=0x602+ CreateFile2(wstr long long long ptr) kernel32.CreateFile2
@ stdcall CreateFileA(str long long ptr long long long) kernel32.CreateFileA
@ stdcall CreateFileMappingFromApp(long ptr long int64 wstr)
@ stdcall -version=0x600+ CreateFileMappingNumaW(long ptr long long long wstr long) kernel32.CreateFileMappingNumaW
@ stdcall CreateFileMappingW(long ptr long long long wstr) kernel32.CreateFileMappingW
@ stdcall CreateFileW(wstr long long ptr long long long) kernel32.CreateFileW
@ stdcall CreateHardLinkA(str str ptr) kernel32.CreateHardLinkA
@ stdcall CreateHardLinkW(wstr wstr ptr) kernel32.CreateHardLinkW
@ stdcall CreateIoCompletionPort(long long long long) kernel32.CreateIoCompletionPort
@ stdcall CreateMemoryResourceNotification(long) kernel32.CreateMemoryResourceNotification
@ stdcall CreateMutexA(ptr long str) kernel32.CreateMutexA
@ stdcall CreateMutexExA(ptr str long long)
@ stdcall CreateMutexExW(ptr wstr long long)
@ stdcall CreateMutexW(ptr long wstr) kernel32.CreateMutexW
@ stdcall CreateNamedPipeW(wstr long long long long long long ptr) kernel32.CreateNamedPipeW
@ stdcall CreatePipe(ptr ptr ptr long) kernel32.CreatePipe
@ stdcall CreatePrivateNamespaceW(ptr ptr wstr) kernel32.CreatePrivateNamespaceW
@ stdcall CreatePrivateObjectSecurity(ptr ptr ptr long long ptr)
@ stdcall CreatePrivateObjectSecurityEx(ptr ptr ptr ptr long long long ptr)
@ stdcall CreatePrivateObjectSecurityWithMultipleInheritance(ptr ptr ptr ptr long long long long ptr)
@ stdcall CreateProcessA(str str ptr ptr long long ptr str ptr ptr) kernel32.CreateProcessA
@ stdcall CreateProcessAsUserA(long str str ptr ptr long long ptr str ptr ptr) advapi32.CreateProcessAsUserA
@ stdcall CreateProcessAsUserW(ptr wstr wstr ptr ptr long long ptr wstr ptr ptr) advapi32.CreateProcessAsUserW
@ stdcall CreateProcessInternalA(ptr str str ptr ptr long long ptr str ptr ptr long) kernel32.CreateProcessInternalA
@ stdcall CreateProcessInternalW(ptr wstr wstr ptr ptr long long ptr wstr ptr ptr long) kernel32.CreateProcessInternalW
@ stdcall CreateProcessW(wstr wstr ptr ptr long long ptr wstr ptr ptr) kernel32.CreateProcessW
@ stdcall CreatePseudoConsole(long long long long ptr)
@ stdcall CreateRemoteThread(long ptr long ptr long long ptr) kernel32.CreateRemoteThread
@ stdcall CreateRemoteThreadEx(long ptr long ptr long long ptr ptr) kernel32.CreateRemoteThreadEx
@ stdcall CreateRestrictedToken(long long long ptr long ptr long ptr ptr)
@ stdcall -version=0x600+ CreateSemaphoreExW(ptr long long wstr long long) kernel32.CreateSemaphoreExW
@ stdcall CreateSemaphoreW(ptr long long wstr) kernel32.CreateSemaphoreW
@ stdcall -version=0x600+ CreateSymbolicLinkW(wstr wstr long) kernel32.CreateSymbolicLinkW
@ stdcall CreateThread(ptr long ptr long long ptr) kernel32.CreateThread
@ stdcall CreateThreadpool(ptr)
@ stdcall CreateThreadpoolCleanupGroup()
@ stdcall CreateThreadpoolIo(ptr ptr ptr ptr)
@ stdcall CreateThreadpoolTimer(ptr ptr ptr)
@ stdcall CreateThreadpoolWait(ptr ptr ptr)
@ stdcall CreateThreadpoolWork(ptr ptr ptr)
@ stdcall CreateTimerQueue() kernel32.CreateTimerQueue
@ stdcall CreateTimerQueueTimer(ptr long ptr ptr long long long) kernel32.CreateTimerQueueTimer
@ stdcall -version=0x600+ CreateWaitableTimerExW(ptr wstr long long) kernel32.CreateWaitableTimerExW
@ stdcall CreateWaitableTimerW(ptr long wstr) kernel32.CreateWaitableTimerW
@ stdcall CreateWellKnownSid(long ptr ptr ptr)
@ stdcall CtrlRoutine(ptr)
@ stdcall DeactivateActCtx(long ptr) kernel32.DeactivateActCtx
@ stdcall DebugActiveProcess(long) kernel32.DebugActiveProcess
@ stdcall DebugActiveProcessStop(long) kernel32.DebugActiveProcessStop
@ stdcall DebugBreak() ntdll.DbgBreakPoint
@ stdcall DecodePointer(ptr) ntdll.RtlDecodePointer
@ stdcall DecodeSystemPointer(ptr) ntdll.RtlDecodeSystemPointer
@ stdcall DefineDosDeviceW(long wstr wstr) kernel32.DefineDosDeviceW
@ stdcall DelayLoadFailureHook(str str) kernel32.DelayLoadFailureHook
@ stdcall DeleteCriticalSection(ptr) ntdll.RtlDeleteCriticalSection
@ stdcall DeleteFiber(ptr) kernel32.DeleteFiber
@ stdcall DeleteFileA(str) kernel32.DeleteFileA
@ stdcall DeleteFileW(wstr) kernel32.DeleteFileW
@ stdcall DeleteTimerQueueEx(long long) kernel32.DeleteTimerQueueEx
@ stdcall DeleteTimerQueueTimer(long long long) kernel32.DeleteTimerQueueTimer
@ stdcall DeleteVolumeMountPointW(wstr) kernel32.DeleteVolumeMountPointW
@ stdcall DeriveCapabilitySidsFromName(ptr ptr ptr ptr ptr)
@ stdcall DeleteAce(ptr long)
@ stdcall DeleteBoundaryDescriptor(ptr) kernel32.DeleteBoundaryDescriptor
@ stdcall -version=0x600+ DeleteProcThreadAttributeList(ptr) kernel32.DeleteProcThreadAttributeList
@ stdcall DestroyPrivateObjectSecurity(ptr)
@ stdcall DeviceIoControl(long long ptr long ptr long ptr ptr) kernel32.DeviceIoControl
@ stdcall DisablePredefinedHandleTableInternal(long)
@ stdcall DisableThreadLibraryCalls(ptr) kernel32.DisableThreadLibraryCalls
@ stdcall -version=0x600+ DisassociateCurrentThreadFromCallback(ptr) ntdll.TpDisassociateCallback
@ stdcall -version=0x603+ DiscardVirtualMemory(ptr long) kernel32.DiscardVirtualMemory
@ stdcall DisconnectNamedPipe(long) kernel32.DisconnectNamedPipe
@ stdcall DnsHostnameToComputerNameExW(wstr ptr ptr)
@ stdcall DuplicateHandle(long long long ptr long long long) kernel32.DuplicateHandle
@ stdcall DuplicateToken(long long ptr)
@ stdcall DuplicateTokenEx(long long ptr long long ptr)
@ stdcall -version=0x601+ EmptyWorkingSet(long) kernel32.K32EmptyWorkingSet
@ stdcall EncodePointer(ptr) ntdll.RtlEncodePointer
@ stdcall EncodeSystemPointer(ptr) ntdll.RtlEncodeSystemPointer
@ stdcall EnterCriticalSection(ptr) ntdll.RtlEnterCriticalSection
@ stdcall EnumCalendarInfoExEx(ptr wstr long wstr long long)
@ stdcall EnumCalendarInfoExW(ptr long long long)
@ stdcall EnumCalendarInfoW(ptr long long long)
@ stdcall -version=0x600+ EnumDateFormatsExEx(ptr wstr long long) kernel32.EnumDateFormatsExEx
@ stdcall EnumDateFormatsExW(ptr long long) kernel32.EnumDateFormatsExW
@ stdcall EnumDateFormatsW(ptr long long) kernel32.EnumDateFormatsW
@ stdcall -version=0x601+ EnumDeviceDrivers(ptr long ptr) kernel32.K32EnumDeviceDrivers
@ stdcall EnumDynamicTimeZoneInformation(long ptr)
@ stdcall EnumLanguageGroupLocalesW(ptr long long ptr) kernel32.EnumLanguageGroupLocalesW
@ stdcall -version=0x601+ EnumPageFilesA(ptr ptr) kernel32.K32EnumPageFilesA
@ stdcall -version=0x601+ EnumPageFilesW(ptr ptr) kernel32.K32EnumPageFilesW
@ stdcall -version=0x601+ EnumProcessModules(long ptr long ptr) kernel32.K32EnumProcessModules
@ stdcall -version=0x601+ EnumProcessModulesEx(long ptr long ptr long) kernel32.K32EnumProcessModulesEx
@ stdcall -version=0x601+ EnumProcesses(ptr long ptr) kernel32.K32EnumProcesses
@ stdcall EnumResourceLanguagesExA(long str str ptr long long long) kernel32.EnumResourceLanguagesExA
@ stdcall EnumResourceLanguagesExW(long wstr wstr ptr long long long) kernel32.EnumResourceLanguagesExW
@ stdcall EnumResourceNamesExA(long str ptr long long long) kernel32.EnumResourceNamesExA
@ stdcall EnumResourceNamesExW(long wstr ptr long long long) kernel32.EnumResourceNamesExW
@ stdcall EnumResourceNamesW(long wstr ptr long) kernel32.EnumResourceNamesW
@ stdcall EnumResourceTypesExA(long ptr long long long) kernel32.EnumResourceTypesExA
@ stdcall EnumResourceTypesExW(long ptr long long long) kernel32.EnumResourceTypesExW
@ stdcall EnumSystemCodePagesW(ptr long) kernel32.EnumSystemCodePagesW
@ stdcall EnumSystemFirmwareTables(long ptr long) kernel32.EnumSystemFirmwareTables
@ stdcall EnumSystemGeoID(long long ptr)
@ stdcall EnumSystemLanguageGroupsW(ptr long ptr) kernel32.EnumSystemLanguageGroupsW
@ stdcall EnumSystemLocalesA(ptr long)
@ stdcall EnumSystemLocalesEx(ptr long long ptr)
@ stdcall EnumSystemLocalesW(ptr long)
@ stdcall -version=0x600+ EnumTimeFormatsEx(ptr wstr long long) kernel32.EnumTimeFormatsEx
@ stdcall EnumTimeFormatsW(ptr long long) kernel32.EnumTimeFormatsW
@ stdcall EnumUILanguagesW(ptr long long) kernel32.EnumUILanguagesW
@ stdcall EqualDomainSid(ptr ptr ptr)
@ stdcall EqualPrefixSid(ptr ptr)
@ stdcall EqualSid(ptr ptr)
@ stdcall EscapeCommFunction(long long) kernel32.EscapeCommFunction
@ stdcall EventActivityIdControl(long ptr) ntdll.EtwEventActivityIdControl
@ stdcall EventEnabled(int64 ptr) ntdll.EtwEventEnabled
@ stdcall EventProviderEnabled(int64 long int64) ntdll.EtwEventProviderEnabled
@ stdcall EventRegister(ptr ptr ptr ptr) ntdll.EtwEventRegister
@ stdcall EventSetInformation(int64 long ptr long) ntdll.EtwEventSetInformation
@ stdcall EventUnregister(int64) ntdll.EtwEventUnregister
@ stdcall EventWrite(int64 ptr long ptr) ntdll.EtwEventWrite
@ stdcall EventWriteEx(int64 ptr int64 long ptr ptr long ptr) ntdll.EtwEventWriteEx
@ stdcall EventWriteString(int64 long int64 wstr) ntdll.EtwEventWriteString
@ stdcall EventWriteTransfer(int64 ptr ptr ptr long ptr) ntdll.EtwEventWriteTransfer
@ stdcall ExitProcess(long) kernel32.ExitProcess
@ stdcall ExitThread(long) kernel32.ExitThread
@ stdcall ExpandEnvironmentStringsA(str ptr long) kernel32.ExpandEnvironmentStringsA
@ stdcall ExpandEnvironmentStringsW(wstr ptr long) kernel32.ExpandEnvironmentStringsW
@ stdcall ExpungeConsoleCommandHistoryA(long)
@ stdcall ExpungeConsoleCommandHistoryW(long)
@ stdcall FatalAppExitA(long str) kernel32.FatalAppExitA
@ stdcall FatalAppExitW(long wstr) kernel32.FatalAppExitW
@ stdcall FileTimeToLocalFileTime(ptr ptr) kernel32.FileTimeToLocalFileTime
@ stdcall FileTimeToSystemTime(ptr ptr) kernel32.FileTimeToSystemTime
@ stdcall FillConsoleOutputAttribute(long long long long ptr)
@ stdcall FillConsoleOutputCharacterA(long long long long ptr)
@ stdcall FillConsoleOutputCharacterW(long long long long ptr)
@ stdcall FindActCtxSectionGuid(long ptr long ptr ptr) kernel32.FindActCtxSectionGuid
@ stdcall FindActCtxSectionStringW(long ptr long wstr ptr) kernel32.FindActCtxSectionStringW
@ stdcall FindClose(long) kernel32.FindClose
@ stdcall FindCloseChangeNotification(long) kernel32.FindCloseChangeNotification
@ stdcall FindFirstChangeNotificationA(str long long) kernel32.FindFirstChangeNotificationA
@ stdcall FindFirstChangeNotificationW(wstr long long) kernel32.FindFirstChangeNotificationW
@ stdcall FindFirstFileA(str ptr) kernel32.FindFirstFileA
@ stdcall FindFirstFileExA(str long ptr long ptr long) kernel32.FindFirstFileExA
@ stdcall FindFirstFileExW(wstr long ptr long ptr long) kernel32.FindFirstFileExW
@ stdcall -version=0x600+ FindFirstFileNameW(wstr long ptr ptr) kernel32.FindFirstFileNameW
@ stdcall FindFirstFileW(wstr ptr) kernel32.FindFirstFileW
@ stdcall FindFirstFreeAce(ptr ptr)
@ stdcall FindFirstStreamW(wstr ptr ptr long) kernel32.FindFirstStreamW
@ stdcall FindFirstVolumeW(ptr long) kernel32.FindFirstVolumeW
@ stdcall FindNLSString(long long wstr long wstr long ptr)
@ stdcall FindNLSStringEx(wstr long wstr long wstr long ptr ptr ptr long)
@ stdcall FindNextChangeNotification(long) kernel32.FindNextChangeNotification
@ stdcall FindNextFileA(long ptr) kernel32.FindNextFileA
@ stdcall FindNextFileW(long ptr) kernel32.FindNextFileW
@ stdcall FindNextStreamW(ptr ptr) kernel32.FindNextStreamW
@ stdcall FindNextVolumeW(long ptr long) kernel32.FindNextVolumeW
@ stdcall FindPackagesByPackageFamily(wstr long ptr ptr ptr ptr ptr)
@ stdcall FindResourceExW(long wstr wstr long) kernel32.FindResourceExW
@ stdcall FindResourceW(long wstr wstr) kernel32.FindResourceW
@ stdcall FindStringOrdinal(long wstr long wstr long long)
@ stdcall FindVolumeClose(ptr) kernel32.FindVolumeClose
@ stdcall FlsAlloc(ptr) kernel32.FlsAlloc
@ stdcall FlsFree(long) kernel32.FlsFree
@ stdcall FlsGetValue(long) kernel32.FlsGetValue
@ stdcall FlsSetValue(long ptr) kernel32.FlsSetValue
@ stdcall FlushConsoleInputBuffer(long)
@ stdcall FlushFileBuffers(long) kernel32.FlushFileBuffers
@ stdcall FlushInstructionCache(long long long) kernel32.FlushInstructionCache
@ stdcall -version=0x600+ FlushProcessWriteBuffers() kernel32.FlushProcessWriteBuffers
@ stdcall FlushViewOfFile(ptr long) kernel32.FlushViewOfFile
@ stdcall FoldStringW(long wstr long ptr long) kernel32.FoldStringW
@ stdcall FormatMessageA(long ptr long long ptr long ptr)
@ stdcall FormatMessageW(long ptr long long ptr long ptr)
@ stdcall FreeConsole()
@ stdcall FreeEnvironmentStringsA(ptr) kernel32.FreeEnvironmentStringsA
@ stdcall FreeEnvironmentStringsW(ptr) kernel32.FreeEnvironmentStringsW
@ stdcall FreeLibrary(long) kernel32.FreeLibrary
@ stdcall FreeLibraryAndExitThread(long long) kernel32.FreeLibraryAndExitThread
@ stdcall -version=0x600+ FreeLibraryWhenCallbackReturns(ptr ptr) ntdll.TpCallbackUnloadDllOnCompletion
@ stdcall FreeResource(long) kernel32.FreeResource
@ stdcall FreeSid(ptr)
@ stdcall FreeUserPhysicalPages(long long long) kernel32.FreeUserPhysicalPages
@ stdcall GenerateConsoleCtrlEvent(long long)
@ stdcall GetACP() kernel32.GetACP
@ stdcall GetAcceptLanguagesA(ptr ptr)
@ stdcall GetAcceptLanguagesW(ptr ptr)
@ stdcall GetAce(ptr long ptr)
@ stdcall GetAclInformation(ptr ptr long long)
@ stdcall GetAppContainerAce(ptr long ptr ptr)
@ stdcall GetAppContainerNamedObjectPath(ptr ptr long ptr ptr)
@ stdcall -version=0x600+ GetApplicationRestartSettings(ptr ptr ptr ptr) kernel32.GetApplicationRestartSettings
@ stdcall GetApplicationUserModelIdFromToken(ptr ptr ptr)
@ stub GetCPFileNameFromRegistry
@ stub GetCPHashNode
@ stdcall GetCPInfo(long ptr) kernel32.GetCPInfo
@ stdcall GetCPInfoExW(long long ptr) kernel32.GetCPInfoExW
@ stub GetCalendar
@ stdcall GetCalendarInfoEx(wstr long ptr long ptr long ptr)
@ stdcall GetCalendarInfoW(long long long ptr long ptr)
@ stdcall GetCommConfig(long ptr long) kernel32.GetCommConfig
@ stdcall GetCommMask(long ptr) kernel32.GetCommMask
@ stdcall GetCommModemStatus(long ptr) kernel32.GetCommModemStatus
@ stdcall GetCommProperties(long ptr) kernel32.GetCommProperties
@ stdcall GetCommState(long ptr) kernel32.GetCommState
@ stdcall GetCommTimeouts(long ptr) kernel32.GetCommTimeouts
@ stdcall GetCommandLineA() kernel32.GetCommandLineA
@ stdcall GetCommandLineW() kernel32.GetCommandLineW
@ stdcall GetCompressedFileSizeA(long ptr) kernel32.GetCompressedFileSizeA
@ stdcall GetCompressedFileSizeW(long ptr) kernel32.GetCompressedFileSizeW
@ stdcall GetComputerNameExA(long ptr ptr)
@ stdcall GetComputerNameExW(long ptr ptr)
@ stdcall GetConsoleAliasA(str str long str)
@ stdcall GetConsoleAliasExesLengthA()
@ stdcall GetConsoleAliasExesLengthW()
@ stdcall GetConsoleAliasW(wstr ptr long wstr)
@ stdcall GetConsoleAliasesLengthA(str)
@ stdcall GetConsoleAliasesLengthW(wstr)
@ stdcall GetConsoleCP()
@ stdcall GetConsoleCommandHistoryA(long long long)
@ stdcall GetConsoleCommandHistoryLengthA(long)
@ stdcall GetConsoleCommandHistoryLengthW(long)
@ stdcall GetConsoleCommandHistoryW(long long long)
@ stdcall GetConsoleCursorInfo(long ptr)
@ stdcall GetConsoleDisplayMode(ptr)
@ stdcall GetConsoleFontSize(long long)
@ stdcall GetConsoleHistoryInfo(ptr)
@ stdcall GetConsoleInputExeNameA(long ptr)
@ stdcall GetConsoleInputExeNameW(long ptr)
@ stdcall GetConsoleMode(long ptr)
@ stdcall GetConsoleOriginalTitleA(ptr long)
@ stdcall GetConsoleOriginalTitleW(ptr long)
@ stdcall GetConsoleOutputCP()
@ stdcall GetConsoleProcessList(ptr long)
@ stdcall GetConsoleScreenBufferInfo(long ptr)
@ stdcall GetConsoleScreenBufferInfoEx(ptr ptr)
@ stdcall GetConsoleTitleA(ptr long)
@ stdcall GetConsoleTitleW(ptr long)
@ stdcall GetConsoleWindow()
@ stdcall -version=0x600+ GetCurrencyFormatEx(wstr long wstr ptr wstr long) kernel32.GetCurrencyFormatEx
@ stdcall GetCurrencyFormatW(long long wstr ptr wstr long) kernel32.GetCurrencyFormatW
@ stdcall GetCurrentActCtx(ptr) kernel32.GetCurrentActCtx
@ stdcall GetCurrentApplicationUserModelId(ptr ptr)
@ stdcall GetCurrentConsoleFont(long long ptr)
@ stdcall GetCurrentConsoleFontEx(ptr long ptr)
@ stdcall GetCurrentDirectoryA(long ptr) kernel32.GetCurrentDirectoryA
@ stdcall GetCurrentDirectoryW(long ptr) kernel32.GetCurrentDirectoryW
@ stdcall GetCurrentPackageFamilyName(ptr ptr)
@ stdcall -version=0x602+ GetCurrentPackageFullName(ptr ptr) kernel32.GetCurrentPackageFullName
@ stdcall -version=0x602+ GetCurrentPackageId(ptr ptr) kernel32.GetCurrentPackageId
@ stdcall GetCurrentPackageInfo(long ptr ptr ptr)
@ stdcall GetCurrentPackagePath(ptr ptr)
@ stdcall -norelay GetCurrentProcess() kernel32.GetCurrentProcess
@ stdcall -norelay GetCurrentProcessId() kernel32.GetCurrentProcessId
@ stdcall GetCurrentProcessorNumber() ntdll.RtlGetCurrentProcessorNumber
@ stdcall -version=0x601+ GetCurrentProcessorNumberEx(ptr) ntdll.RtlGetCurrentProcessorNumberEx
@ stdcall -norelay GetCurrentThread() kernel32.GetCurrentThread
@ stdcall -norelay GetCurrentThreadId() kernel32.GetCurrentThreadId
@ stdcall GetCurrentThreadStackLimits(ptr ptr)
@ stdcall GetDateFormatA(long long ptr str ptr long) kernel32.GetDateFormatA
@ stdcall -version=0x600+ GetDateFormatEx(wstr long ptr wstr wstr long wstr) kernel32.GetDateFormatEx
@ stdcall GetDateFormatW(long long ptr wstr ptr long) kernel32.GetDateFormatW
@ stdcall -version=0x601+ GetDeviceDriverBaseNameA(ptr ptr long) kernel32.K32GetDeviceDriverBaseNameA
@ stdcall -version=0x601+ GetDeviceDriverBaseNameW(ptr ptr long) kernel32.K32GetDeviceDriverBaseNameW
@ stdcall -version=0x601+ GetDeviceDriverFileNameA(ptr ptr long) kernel32.K32GetDeviceDriverFileNameA
@ stdcall -version=0x601+ GetDeviceDriverFileNameW(ptr ptr long) kernel32.K32GetDeviceDriverFileNameW
@ stdcall GetDiskFreeSpaceA(str ptr ptr ptr ptr) kernel32.GetDiskFreeSpaceA
@ stdcall GetDiskFreeSpaceExA(str ptr ptr ptr) kernel32.GetDiskFreeSpaceExA
@ stdcall GetDiskFreeSpaceExW(wstr ptr ptr ptr) kernel32.GetDiskFreeSpaceExW
@ stdcall GetDiskFreeSpaceW(wstr ptr ptr ptr ptr) kernel32.GetDiskFreeSpaceW
@ stdcall GetDriveTypeA(str) kernel32.GetDriveTypeA
@ stdcall GetDriveTypeW(wstr) kernel32.GetDriveTypeW
@ stdcall -version=0x600+ GetDynamicTimeZoneInformation(ptr) kernel32.GetDynamicTimeZoneInformation
@ stdcall -version=0x602+ GetDynamicTimeZoneInformationEffectiveYears(ptr ptr ptr) kernel32.GetDynamicTimeZoneInformationEffectiveYears
@ stdcall -version=0x601+ GetEnabledXStateFeatures() kernel32.GetEnabledXStateFeatures
@ stdcall GetEnvironmentStrings() kernel32.GetEnvironmentStrings
@ stdcall GetEnvironmentStringsA() kernel32.GetEnvironmentStringsA
@ stdcall GetEnvironmentStringsW() kernel32.GetEnvironmentStringsW
@ stdcall GetEnvironmentVariableA(str ptr long) kernel32.GetEnvironmentVariableA
@ stdcall GetEnvironmentVariableW(wstr ptr long) kernel32.GetEnvironmentVariableW
@ stub GetEraNameCountedString
@ stdcall -version=0x600+ GetErrorMode() kernel32.GetErrorMode
@ stdcall GetExitCodeProcess(long ptr) kernel32.GetExitCodeProcess
@ stdcall GetExitCodeThread(long ptr) kernel32.GetExitCodeThread
@ stub GetFallbackDisplayName
@ stdcall GetFileAttributesA(str) kernel32.GetFileAttributesA
@ stdcall GetFileAttributesExA(str long ptr) kernel32.GetFileAttributesExA
@ stdcall GetFileAttributesExW(wstr long ptr) kernel32.GetFileAttributesExW
@ stdcall GetFileAttributesW(wstr) kernel32.GetFileAttributesW
@ stdcall GetFileInformationByHandle(long ptr) kernel32.GetFileInformationByHandle
@ stdcall -version=0x600+ GetFileInformationByHandleEx(ptr long ptr long) kernel32.GetFileInformationByHandleEx
@ stdcall GetFileMUIInfo(long wstr ptr ptr)
@ stdcall GetFileMUIPath(long wstr wstr ptr wstr ptr ptr) kernel32.GetFileMUIPath
@ stdcall GetFileSecurityW(wstr long ptr long ptr)
@ stdcall GetFileSize(long ptr) kernel32.GetFileSize
@ stdcall GetFileSizeEx(long ptr) kernel32.GetFileSizeEx
@ stdcall GetFileTime(long ptr ptr ptr) kernel32.GetFileTime
@ stdcall GetFileType(long) kernel32.GetFileType
@ stdcall GetFileVersionInfoA(str long long ptr)
@ stdcall GetFileVersionInfoExA(long str long long ptr)
@ stdcall GetFileVersionInfoExW(long wstr long long ptr)
@ stdcall GetFileVersionInfoSizeA(str ptr)
@ stdcall GetFileVersionInfoSizeExA(long str ptr)
@ stdcall GetFileVersionInfoSizeExW(long wstr ptr)
@ stdcall GetFileVersionInfoSizeW(wstr ptr)
@ stdcall GetFileVersionInfoW(wstr long long ptr)
@ stdcall -version=0x600+ GetFinalPathNameByHandleA(ptr str long long) kernel32.GetFinalPathNameByHandleA
@ stdcall -version=0x600+ GetFinalPathNameByHandleW(ptr wstr long long) kernel32.GetFinalPathNameByHandleW
@ stdcall GetFullPathNameA(str long ptr ptr) kernel32.GetFullPathNameA
@ stdcall GetFullPathNameW(wstr long ptr ptr) kernel32.GetFullPathNameW
@ stdcall GetGeoInfoW(long long ptr long long)
@ stdcall GetGeoInfoEx(ptr long ptr long)
@ stdcall GetHandleInformation(long ptr) kernel32.GetHandleInformation
@ stdcall GetIntegratedDisplaySize(ptr)
@ stdcall GetKernelObjectSecurity(long long ptr long ptr)
@ stdcall GetLargePageMinimum() kernel32.GetLargePageMinimum
@ stdcall GetLargestConsoleWindowSize(long)
@ stdcall GetLastError() ntdll.RtlGetLastWin32Error
@ stdcall GetLengthSid(ptr)
@ stdcall GetLocalTime(ptr) kernel32.GetLocalTime
@ stdcall GetLocaleInfoA(long long ptr long) kernel32.GetLocaleInfoA
@ stdcall -version=0x600+ GetLocaleInfoEx(wstr long ptr long) kernel32.GetLocaleInfoEx
@ stub GetLocaleInfoHelper
@ stdcall GetLocaleInfoW(long long ptr long) kernel32.GetLocaleInfoW
@ stdcall GetLogicalDriveStringsW(long ptr) kernel32.GetLogicalDriveStringsW
@ stdcall GetLogicalDrives() kernel32.GetLogicalDrives
@ stdcall GetLogicalProcessorInformation(ptr ptr) kernel32.GetLogicalProcessorInformation
@ stdcall -version=0x601+ GetLogicalProcessorInformationEx(long ptr ptr) kernel32.GetLogicalProcessorInformationEx
@ stdcall GetLongPathNameA(str long long) kernel32.GetLongPathNameA
@ stdcall GetLongPathNameW(wstr long long) kernel32.GetLongPathNameW
@ stdcall -version=0x601+ GetMappedFileNameA(long ptr ptr long) kernel32.K32GetMappedFileNameA
@ stdcall -version=0x601+ GetMappedFileNameW(long ptr ptr long) kernel32.K32GetMappedFileNameW
@ stdcall -version=0x601+ GetModuleBaseNameA(long long ptr long) kernel32.K32GetModuleBaseNameA
@ stdcall -version=0x601+ GetModuleBaseNameW(long long ptr long) kernel32.K32GetModuleBaseNameW
@ stdcall GetModuleFileNameA(long ptr long) kernel32.GetModuleFileNameA
@ stdcall -version=0x601+ GetModuleFileNameExA(long long ptr long) kernel32.K32GetModuleFileNameExA
@ stdcall -version=0x601+ GetModuleFileNameExW(long long ptr long) kernel32.K32GetModuleFileNameExW
@ stdcall GetModuleFileNameW(long ptr long) kernel32.GetModuleFileNameW
@ stdcall GetModuleHandleA(str) kernel32.GetModuleHandleA
@ stdcall GetModuleHandleExA(long ptr ptr) kernel32.GetModuleHandleExA
@ stdcall GetModuleHandleExW(long ptr ptr) kernel32.GetModuleHandleExW
@ stdcall GetModuleHandleW(wstr) kernel32.GetModuleHandleW
@ stdcall -version=0x601+ GetModuleInformation(long long ptr long) kernel32.K32GetModuleInformation
@ stdcall GetNLSVersion(long long ptr)
@ stdcall GetNLSVersionEx(long wstr ptr)
@ stub GetNamedLocaleHashNode
@ stub -version=0x600+ GetNamedPipeAttribute() kernel32.GetNamedPipeAttribute
@ stub -version=0x600+ GetNamedPipeClientComputerNameW() kernel32.GetNamedPipeClientComputerNameW
@ stdcall GetNamedPipeHandleStateW(long ptr ptr ptr ptr wstr long) kernel32.GetNamedPipeHandleStateW
@ stdcall GetNamedPipeInfo(long ptr ptr ptr ptr) kernel32.GetNamedPipeInfo
@ stdcall GetNativeSystemInfo(ptr) kernel32.GetNativeSystemInfo
@ stdcall GetNumaHighestNodeNumber(ptr) kernel32.GetNumaHighestNodeNumber
@ stdcall -version=0x601+ GetNumaNodeProcessorMaskEx(long ptr) kernel32.GetNumaNodeProcessorMaskEx
@ stub GetNumaProximityNodeEx
@ stdcall -version=0x600+ GetNumberFormatEx(wstr long wstr ptr wstr long) kernel32.GetNumberFormatEx
@ stdcall GetNumberFormatW(long long wstr ptr ptr long) kernel32.GetNumberFormatW
@ stdcall GetNumberOfConsoleInputEvents(long ptr)
@ stdcall GetNumberOfConsoleMouseButtons(ptr)
@ stdcall GetOEMCP() kernel32.GetOEMCP
@ stdcall GetOverlappedResult(long ptr ptr long) kernel32.GetOverlappedResult
@ stdcall -version=0x602+ GetOverlappedResultEx(long ptr ptr long long) kernel32.GetOverlappedResultEx
@ stdcall GetPackageFamilyName(long ptr ptr)
@ stdcall GetPackageFamilyNameFromToken(ptr ptr ptr)
@ stdcall GetPackageFullName(long ptr ptr)
@ stdcall GetPackageFullNameFromToken(ptr ptr ptr)
@ stdcall -version=0x602+ GetPackagePathByFullName(wstr ptr wstr) kernel32.GetPackagePathByFullName
@ stdcall -version=0x602+ GetPackagesByPackageFamily(wstr ptr ptr ptr ptr) kernel32.GetPackagesByPackageFamily
@ stdcall -version=0x601+ GetPerformanceInfo(ptr long) kernel32.K32GetPerformanceInfo
@ stdcall -version=0x600+ GetPhysicallyInstalledSystemMemory(ptr) kernel32.GetPhysicallyInstalledSystemMemory
@ stdcall GetPriorityClass(long) kernel32.GetPriorityClass
@ stdcall GetPrivateObjectSecurity(ptr long ptr long ptr)
@ stdcall GetProcAddress(long str) kernel32.GetProcAddress
@ stdcall -version=0x601+ GetProcessGroupAffinity(long ptr ptr) kernel32.GetProcessGroupAffinity
@ stdcall GetProcessHandleCount(long ptr) kernel32.GetProcessHandleCount
@ stdcall GetProcessHeap() kernel32.GetProcessHeap
@ stdcall GetProcessHeaps(long ptr) kernel32.GetProcessHeaps
@ stdcall GetProcessId(long) kernel32.GetProcessId
@ stdcall GetProcessIdOfThread(ptr) kernel32.GetProcessIdOfThread
@ stdcall -version=0x601+ GetProcessImageFileNameA(long ptr long) kernel32.K32GetProcessImageFileNameA
@ stdcall -version=0x601+ GetProcessImageFileNameW(long ptr long) kernel32.K32GetProcessImageFileNameW
@ stdcall GetProcessInformation(long long ptr long) kernel32.GetProcessInformation
@ stdcall -version=0x601+ GetProcessMemoryInfo(long ptr long) kernel32.K32GetProcessMemoryInfo
@ stdcall GetProcessMitigationPolicy(long long ptr long) kernel32.GetProcessMitigationPolicy
@ stdcall GetProcessPreferredUILanguages(long ptr ptr ptr)
@ stdcall GetProcessPriorityBoost(long ptr) kernel32.GetProcessPriorityBoost
@ stdcall GetProcessShutdownParameters(ptr ptr) kernel32.GetProcessShutdownParameters
@ stdcall GetProcessTimes(long ptr ptr ptr ptr) kernel32.GetProcessTimes
@ stdcall GetProcessVersion(long) kernel32.GetProcessVersion
@ stdcall GetProcessWorkingSetSizeEx(long ptr ptr long) kernel32.GetProcessWorkingSetSizeEx
@ stdcall -version=0x600+ GetProductInfo(long long long long ptr) ntdll.RtlGetProductInfo
@ stub GetPtrCalData
@ stub GetPtrCalDataArray
@ stdcall GetQueuedCompletionStatus(long ptr ptr ptr long) kernel32.GetQueuedCompletionStatus
@ stdcall -version=0x600+ GetQueuedCompletionStatusEx(ptr ptr long ptr long long) kernel32.GetQueuedCompletionStatusEx
@ stdcall GetSecurityDescriptorControl(ptr ptr ptr)
@ stdcall GetSecurityDescriptorDacl(ptr ptr ptr ptr)
@ stdcall GetSecurityDescriptorGroup(ptr ptr ptr)
@ stdcall GetSecurityDescriptorLength(ptr)
@ stdcall GetSecurityDescriptorOwner(ptr ptr ptr)
@ stdcall GetSecurityDescriptorRMControl(ptr ptr) advapi32.GetSecurityDescriptorRMControl
@ stdcall GetSecurityDescriptorSacl(ptr ptr ptr ptr)
@ stdcall GetShortPathNameW(wstr ptr long) kernel32.GetShortPathNameW
@ stdcall GetSidIdentifierAuthority(ptr)
@ stdcall GetSidLengthRequired(long)
@ stdcall GetSidSubAuthority(ptr long)
@ stdcall GetSidSubAuthorityCount(ptr)
@ stdcall GetStartupInfoW(ptr) kernel32.GetStartupInfoW
@ stdcall GetStdHandle(long)
@ stub GetStringTableEntry
@ stdcall GetStringTypeA(long long str long ptr)
@ stdcall GetStringTypeExW(long long wstr long ptr)
@ stdcall GetStringTypeW(long wstr long ptr)
@ stdcall GetSystemCpuSetInformation(ptr long ptr ptr long)
@ stdcall GetSystemDefaultLCID() kernel32.GetSystemDefaultLCID
@ stdcall GetSystemDefaultLangID() kernel32.GetSystemDefaultLangID
@ stdcall -version=0x600+ GetSystemDefaultLocaleName(ptr long) kernel32.GetSystemDefaultLocaleName
@ stdcall GetSystemDefaultUILanguage() kernel32.GetSystemDefaultUILanguage
@ stdcall GetSystemDirectoryA(ptr long) kernel32.GetSystemDirectoryA
@ stdcall GetSystemDirectoryW(ptr long) kernel32.GetSystemDirectoryW
@ stdcall GetSystemFileCacheSize(ptr ptr ptr) kernel32.GetSystemFileCacheSize
@ stdcall GetSystemFirmwareTable(long long ptr long) kernel32.GetSystemFirmwareTable
@ stdcall GetSystemInfo(ptr) kernel32.GetSystemInfo
@ stdcall GetSystemPreferredUILanguages(long ptr ptr ptr)
@ stdcall GetProcessorSystemCycleTime(long ptr ptr) kernel32.GetProcessorSystemCycleTime
@ stdcall GetSystemTime(ptr) kernel32.GetSystemTime
@ stdcall GetSystemTimeAdjustment(ptr ptr ptr) kernel32.GetSystemTimeAdjustment
@ stdcall GetSystemTimeAsFileTime(ptr) kernel32.GetSystemTimeAsFileTime
@ stdcall -version=0x602+ GetSystemTimePreciseAsFileTime(ptr) kernel32.GetSystemTimePreciseAsFileTime
@ stdcall GetSystemTimes(ptr ptr ptr) kernel32.GetSystemTimes
@ stdcall GetSystemWindowsDirectoryA(ptr long) kernel32.GetSystemWindowsDirectoryA
@ stdcall GetSystemWindowsDirectoryW(ptr long) kernel32.GetSystemWindowsDirectoryW
@ stdcall GetSystemWow64Directory2A(ptr long long)
@ stdcall GetSystemWow64Directory2W(ptr long long)
@ stdcall GetSystemWow64DirectoryA(ptr long) kernel32.GetSystemWow64DirectoryA
@ stdcall GetSystemWow64DirectoryW(ptr long) kernel32.GetSystemWow64DirectoryW
@ stdcall GetTempFileNameA(str str long ptr) kernel32.GetTempFileNameA
@ stdcall GetTempFileNameW(wstr wstr long ptr) kernel32.GetTempFileNameW
@ stdcall GetTempPath2A(long ptr) kernel32.GetTempPath2A
@ stdcall GetTempPath2W(long ptr) kernel32.GetTempPath2W
@ stdcall GetTempPathA(long ptr) kernel32.GetTempPathA
@ stdcall GetTempPathW(long ptr) kernel32.GetTempPathW
@ stdcall GetThreadContext(long ptr) kernel32.GetThreadContext
@ stdcall GetThreadDescription(ptr ptr)
@ stdcall GetThreadErrorMode()
@ stdcall -version=0x601+ GetThreadGroupAffinity(long ptr) kernel32.GetThreadGroupAffinity
@ stdcall GetThreadIOPendingFlag(long ptr) kernel32.GetThreadIOPendingFlag
@ stdcall GetThreadId(ptr) kernel32.GetThreadId
@ stdcall GetThreadIdealProcessorEx(long ptr) kernel32.GetThreadIdealProcessorEx
@ stdcall GetThreadInformation(long long ptr long)
@ stdcall GetThreadLocale() kernel32.GetThreadLocale
@ stdcall GetThreadPreferredUILanguages(long ptr ptr ptr)
@ stdcall GetThreadPriority(long) kernel32.GetThreadPriority
@ stdcall GetThreadPriorityBoost(long ptr) kernel32.GetThreadPriorityBoost
@ stdcall GetThreadTimes(long ptr ptr ptr ptr) kernel32.GetThreadTimes
@ stdcall -version=0x600+ GetThreadUILanguage() kernel32.GetThreadUILanguage
@ stdcall GetTickCount() kernel32.GetTickCount
@ stdcall -version=0x600+ GetTickCount64() kernel32.GetTickCount64
@ stdcall GetTimeFormatA(long long ptr str ptr long) kernel32.GetTimeFormatA
@ stdcall -version=0x600+ GetTimeFormatEx(wstr long ptr wstr wstr long) kernel32.GetTimeFormatEx
@ stdcall GetTimeFormatW(long long ptr wstr ptr long) kernel32.GetTimeFormatW
@ stdcall GetTimeZoneInformation(ptr) kernel32.GetTimeZoneInformation
@ stdcall -version=0x600+ GetTimeZoneInformationForYear(long ptr ptr) kernel32.GetTimeZoneInformationForYear
@ stdcall GetTokenInformation(long long ptr long ptr)
@ stdcall GetTraceEnableFlags(int64) ntdll.EtwGetTraceEnableFlags
@ stdcall GetTraceEnableLevel(int64) ntdll.EtwGetTraceEnableLevel
@ stdcall -ret64 GetTraceLoggerHandle(ptr) ntdll.EtwGetTraceLoggerHandle
@ stdcall -version=0x600+ GetUILanguageInfo(long wstr wstr ptr ptr) kernel32.GetUILanguageInfo
@ stdcall GetUserDefaultGeoName(ptr long)
@ stdcall GetUserDefaultLCID() kernel32.GetUserDefaultLCID
@ stdcall GetUserDefaultLangID() kernel32.GetUserDefaultLangID
@ stdcall -version=0x600+ GetUserDefaultLocaleName(wstr long) kernel32.GetUserDefaultLocaleName
@ stdcall GetUserDefaultUILanguage() kernel32.GetUserDefaultUILanguage
@ stdcall GetUserGeoID(long)
@ stub GetUserInfo
@ stub GetUserInfoWord
@ stdcall GetUserPreferredUILanguages(long ptr ptr ptr)
@ stdcall GetVersion()
@ stdcall GetVersionExA(ptr)
@ stdcall GetVersionExW(ptr)
@ stdcall GetVolumeInformationA(str ptr long ptr ptr ptr ptr long) kernel32.GetVolumeInformationA
@ stdcall GetVolumeInformationByHandleW(ptr ptr long ptr ptr ptr ptr long)
@ stdcall GetVolumeInformationW(wstr ptr long ptr ptr ptr ptr long) kernel32.GetVolumeInformationW
@ stdcall GetVolumeNameForVolumeMountPointW(wstr ptr long) kernel32.GetVolumeNameForVolumeMountPointW
@ stdcall GetVolumePathNameW(wstr ptr long) kernel32.GetVolumePathNameW
@ stdcall GetVolumePathNamesForVolumeNameW(wstr wstr long ptr) kernel32.GetVolumePathNamesForVolumeNameW
@ stdcall GetWindowsAccountDomainSid(ptr ptr ptr)
@ stdcall GetWindowsDirectoryA(ptr long) kernel32.GetWindowsDirectoryA
@ stdcall GetWindowsDirectoryW(ptr long) kernel32.GetWindowsDirectoryW
@ stdcall GetWriteWatch(long ptr long ptr ptr ptr) kernel32.GetWriteWatch
@ stdcall -version=0x601+ GetWsChanges(long ptr long) kernel32.K32GetWsChanges
@ stdcall -version=0x601+ GetWsChangesEx(long ptr ptr) kernel32.K32GetWsChangesEx
@ stdcall -arch=x86_64,arm64ec GetXStateFeaturesMask(ptr ptr)
@ stdcall GlobalAlloc(long long) kernel32.GlobalAlloc
@ stdcall GlobalFree(long) kernel32.GlobalFree
@ stdcall GlobalMemoryStatusEx(ptr) kernel32.GlobalMemoryStatusEx
@ stdcall HashData(ptr long ptr long)
@ stdcall HeapAlloc(long long long) ntdll.RtlAllocateHeap
@ stdcall HeapCompact(long long) kernel32.HeapCompact
@ stdcall HeapCreate(long long long) kernel32.HeapCreate
@ stdcall HeapDestroy(long) kernel32.HeapDestroy
@ stdcall HeapFree(long long long) ntdll.RtlFreeHeap
@ stdcall HeapLock(long) kernel32.HeapLock
@ stdcall HeapQueryInformation(long long ptr long ptr) kernel32.HeapQueryInformation
@ stdcall HeapReAlloc(long long ptr long) ntdll.RtlReAllocateHeap
@ stdcall HeapSetInformation(ptr long ptr long) kernel32.HeapSetInformation
@ stdcall HeapSize(long long ptr) ntdll.RtlSizeHeap
@ stdcall HeapSummary(long long ptr) kernel32.HeapSummary
@ stdcall HeapUnlock(long) kernel32.HeapUnlock
@ stdcall HeapValidate(long long ptr) kernel32.HeapValidate
@ stdcall HeapWalk(long ptr) kernel32.HeapWalk
@ stdcall IdnToAscii(long wstr long ptr long)
@ stdcall IdnToNameprepUnicode(long wstr long ptr long)
@ stdcall IdnToUnicode(long wstr long ptr long)
@ stdcall ImpersonateAnonymousToken(long)
@ stdcall ImpersonateLoggedOnUser(long)
@ stdcall ImpersonateNamedPipeClient(long)
@ stdcall ImpersonateSelf(long)
@ stdcall InitOnceBeginInitialize(ptr long ptr ptr)
@ stdcall InitOnceComplete(ptr long ptr)
@ stdcall InitOnceExecuteOnce(ptr ptr ptr ptr)
@ stdcall -version=0x600+ InitOnceInitialize(ptr) ntdll.RtlRunOnceInitialize
@ stdcall InitializeAcl(ptr long long)
@ stdcall InitializeConditionVariable(ptr) ntdll.RtlInitializeConditionVariable
@ stdcall -arch=win64 InitializeContext(ptr long ptr ptr)
@ stdcall -arch=win64 InitializeContext2(ptr long ptr ptr int64)
@ stdcall InitializeCriticalSection(ptr) kernel32.InitializeCriticalSection
@ stdcall InitializeCriticalSectionAndSpinCount(ptr long) kernel32.InitializeCriticalSectionAndSpinCount
@ stdcall -version=0x600+ InitializeCriticalSectionEx(ptr long long) kernel32.InitializeCriticalSectionEx
@ stdcall -version=0x600+ InitializeProcThreadAttributeList(ptr long long ptr) kernel32.InitializeProcThreadAttributeList
@ stdcall -version=0x601+ InitializeProcessForWsWatch(long) kernel32.K32InitializeProcessForWsWatch
@ stdcall InitializeSListHead(ptr) ntdll.RtlInitializeSListHead
@ stdcall -version=0x600+ InitializeSRWLock(ptr) ntdll.RtlInitializeSRWLock
@ stdcall InitializeSecurityDescriptor(ptr long)
@ stdcall InitializeSid(ptr ptr long)
@ stdcall -arch=i386,ppc InterlockedCompareExchange(ptr long long) kernel32.InterlockedCompareExchange
@ stdcall -arch=i386 InterlockedCompareExchange64(ptr double double) ntdll.RtlInterlockedCompareExchange64
@ stdcall -arch=i386,ppc InterlockedDecrement(ptr) kernel32.InterlockedDecrement
@ stdcall -arch=i386,ppc InterlockedExchange(ptr long) kernel32.InterlockedExchange
@ stdcall -arch=i386,ppc InterlockedExchangeAdd(ptr long) kernel32.InterlockedExchangeAdd
@ stdcall InterlockedFlushSList(ptr) ntdll.RtlInterlockedFlushSList
@ stdcall -arch=i386,ppc InterlockedIncrement(ptr) kernel32.InterlockedIncrement
@ stdcall InterlockedPopEntrySList(ptr) ntdll.RtlInterlockedPopEntrySList
@ stdcall InterlockedPushEntrySList(ptr ptr) ntdll.RtlInterlockedPushEntrySList
@ fastcall -version=0x600+ InterlockedPushListSList(ptr ptr ptr long) ntdll.RtlInterlockedPushListSList
@ stdcall -version=0x602+ InterlockedPushListSListEx(ptr ptr ptr long) ntdll.RtlInterlockedPushListSListEx
@ stub InternalLcidToName
@ stdcall Internal_EnumCalendarInfo(ptr ptr long long long long long long)
@ stdcall Internal_EnumDateFormats(ptr long long long long long long)
@ stdcall Internal_EnumLanguageGroupLocales(ptr long long ptr long)
@ stdcall Internal_EnumSystemCodePages(ptr long long)
@ stdcall Internal_EnumSystemLanguageGroups(ptr long ptr long)
@ stub Internal_EnumSystemLocales
@ stdcall Internal_EnumTimeFormats(ptr long long long long long)
@ stdcall Internal_EnumUILanguages(ptr long long long)
@ stdcall IsApiSetImplemented(str)
@ stdcall IsCharAlphaA(long)
@ stdcall IsCharAlphaNumericA(long)
@ stdcall IsCharAlphaNumericW(long)
@ stdcall IsCharAlphaW(long)
@ stdcall IsCharBlankW(long)
@ stdcall IsCharCntrlW(long)
@ stdcall IsCharDigitW(long)
@ stdcall IsCharLowerA(long)
@ stdcall IsCharLowerW(long)
@ stdcall IsCharPunctW(long)
@ stdcall IsCharSpaceA(long)
@ stdcall IsCharSpaceW(long)
@ stdcall IsCharUpperA(long)
@ stdcall IsCharUpperW(long)
@ stdcall IsCharXDigitW(long)
@ stdcall IsDBCSLeadByte(long) kernel32.IsDBCSLeadByte
@ stdcall IsDBCSLeadByteEx(long long) kernel32.IsDBCSLeadByteEx
@ stdcall IsDebuggerPresent() kernel32.IsDebuggerPresent
@ stdcall IsInternetESCEnabled()
@ stdcall IsNLSDefinedString(long long ptr wstr long)
@ stdcall IsNormalizedString(long wstr long)
@ stdcall IsProcessInJob(long long ptr) kernel32.IsProcessInJob
@ stdcall IsProcessorFeaturePresent(long) kernel32.IsProcessorFeaturePresent
@ stdcall -version=0x600+ IsThreadAFiber() kernel32.IsThreadAFiber
@ stdcall IsThreadpoolTimerSet(ptr) ntdll.TpIsTimerSet
@ stdcall IsTokenRestricted(long)
@ stdcall IsValidAcl(ptr)
@ stdcall IsValidCodePage(long) kernel32.IsValidCodePage
@ stdcall IsValidLanguageGroup(long long) kernel32.IsValidLanguageGroup
@ stdcall IsValidLocale(long long) kernel32.IsValidLocale
@ stdcall IsValidLocaleName(wstr)
@ stdcall IsValidNLSVersion(long wstr ptr)
@ stub IsValidRelativeSecurityDescriptor
@ stdcall IsValidSecurityDescriptor(ptr)
@ stdcall IsValidSid(ptr)
@ stdcall IsWellKnownSid(ptr long)
@ stdcall IsWow64Process(ptr ptr) kernel32.IsWow64Process
@ stdcall -version=0xA00+ IsWow64Process2(ptr ptr ptr) kernel32.IsWow64Process2
@ stdcall -version=0x601+ K32EmptyWorkingSet(long) kernel32.K32EmptyWorkingSet
@ stdcall -version=0x601+ K32EnumDeviceDrivers(ptr long ptr) kernel32.K32EnumDeviceDrivers
@ stdcall -version=0x601+ K32EnumPageFilesA(ptr ptr) kernel32.K32EnumPageFilesA
@ stdcall -version=0x601+ K32EnumPageFilesW(ptr ptr) kernel32.K32EnumPageFilesW
@ stdcall -version=0x601+ K32EnumProcessModules(long ptr long ptr) kernel32.K32EnumProcessModules
@ stdcall -version=0x601+ K32EnumProcessModulesEx(long ptr long ptr long) kernel32.K32EnumProcessModulesEx
@ stdcall -version=0x601+ K32EnumProcesses(ptr long ptr) kernel32.K32EnumProcesses
@ stdcall -version=0x601+ K32GetDeviceDriverBaseNameA(ptr ptr long) kernel32.K32GetDeviceDriverBaseNameA
@ stdcall -version=0x601+ K32GetDeviceDriverBaseNameW(ptr ptr long) kernel32.K32GetDeviceDriverBaseNameW
@ stdcall -version=0x601+ K32GetDeviceDriverFileNameA(ptr ptr long) kernel32.K32GetDeviceDriverFileNameA
@ stdcall -version=0x601+ K32GetDeviceDriverFileNameW(ptr ptr long) kernel32.K32GetDeviceDriverFileNameW
@ stdcall -version=0x601+ K32GetMappedFileNameA(long ptr ptr long) kernel32.K32GetMappedFileNameA
@ stdcall -version=0x601+ K32GetMappedFileNameW(long ptr ptr long) kernel32.K32GetMappedFileNameW
@ stdcall -version=0x601+ K32GetModuleBaseNameA(long long ptr long) kernel32.K32GetModuleBaseNameA
@ stdcall -version=0x601+ K32GetModuleBaseNameW(long long ptr long) kernel32.K32GetModuleBaseNameW
@ stdcall -version=0x601+ K32GetModuleFileNameExA(long long ptr long) kernel32.K32GetModuleFileNameExA
@ stdcall -version=0x601+ K32GetModuleFileNameExW(long long ptr long) kernel32.K32GetModuleFileNameExW
@ stdcall -version=0x601+ K32GetModuleInformation(long long ptr long) kernel32.K32GetModuleInformation
@ stdcall -version=0x601+ K32GetPerformanceInfo(ptr long) kernel32.K32GetPerformanceInfo
@ stdcall -version=0x601+ K32GetProcessImageFileNameA(long ptr long) kernel32.K32GetProcessImageFileNameA
@ stdcall -version=0x601+ K32GetProcessImageFileNameW(long ptr long) kernel32.K32GetProcessImageFileNameW
@ stdcall -version=0x601+ K32GetProcessMemoryInfo(long ptr long) kernel32.K32GetProcessMemoryInfo
@ stdcall -version=0x601+ K32GetWsChanges(long ptr long) kernel32.K32GetWsChanges
@ stdcall -version=0x601+ K32GetWsChangesEx(long ptr ptr) kernel32.K32GetWsChangesEx
@ stdcall -version=0x601+ K32InitializeProcessForWsWatch(long) kernel32.K32InitializeProcessForWsWatch
@ stdcall -version=0x601+ K32QueryWorkingSet(long ptr long) kernel32.K32QueryWorkingSet
@ stdcall -version=0x601+ K32QueryWorkingSetEx(long ptr long) kernel32.K32QueryWorkingSetEx
@ stub KernelBaseGetGlobalData
@ stdcall LCIDToLocaleName(long ptr long long)
@ stdcall LCMapStringA(long long str long ptr long)
@ stdcall LCMapStringEx(wstr long wstr long ptr long ptr ptr long)
@ stdcall LCMapStringW(long long wstr long ptr long)
@ stdcall LeaveCriticalSection(ptr) ntdll.RtlLeaveCriticalSection
@ stdcall -version=0x600+ LeaveCriticalSectionWhenCallbackReturns(ptr ptr) ntdll.TpCallbackLeaveCriticalSectionOnCompletion
@ stub LoadAppInitDlls
@ stdcall LoadLibraryA(str) kernel32.LoadLibraryA
@ stdcall LoadLibraryExA(str long long) kernel32.LoadLibraryExA
@ stdcall LoadLibraryExW(wstr long long) kernel32.LoadLibraryExW
@ stdcall LoadLibraryW(wstr) kernel32.LoadLibraryW
@ stdcall LoadPackagedLibrary(wstr long)
@ stdcall LoadResource(long long) kernel32.LoadResource
@ stdcall LoadStringA(long long ptr long)
@ stub -version=0x600+ LoadStringBaseExW() kernel32.LoadStringBaseExW
@ stub LoadStringByReference
@ stdcall LoadStringW(long long ptr long)
@ stdcall LocalAlloc(long long) kernel32.LocalAlloc
@ stdcall LocalFileTimeToFileTime(ptr ptr) kernel32.LocalFileTimeToFileTime
@ stdcall LocalFree(long) kernel32.LocalFree
@ stdcall LocalLock(long) kernel32.LocalLock
@ stdcall LocalReAlloc(long long long) kernel32.LocalReAlloc
@ stdcall LocalUnlock(long) kernel32.LocalUnlock
@ stdcall LocaleNameToLCID(wstr long)
@ stdcall -arch=x86_64,arm64ec LocateXStateFeature(ptr long ptr)
@ stdcall LockFile(long long long long long) kernel32.LockFile
@ stdcall LockFileEx(long long long long long ptr) kernel32.LockFileEx
@ stdcall LockResource(long) kernel32.LockResource
@ stdcall MakeAbsoluteSD(ptr ptr ptr ptr ptr ptr ptr ptr ptr ptr ptr)
@ stdcall MakeAbsoluteSD2(ptr ptr) advapi32.MakeAbsoluteSD2
@ stdcall MakeSelfRelativeSD(ptr ptr ptr)
@ stdcall MapGenericMask(ptr ptr)
@ stdcall MapUserPhysicalPages(ptr long ptr) kernel32.MapUserPhysicalPages
@ stdcall MapViewOfFile(long long long long long) kernel32.MapViewOfFile
@ stdcall -arch=win64 MapViewOfFile3(long long ptr int64 long long long ptr long)
@ stdcall MapViewOfFileEx(long long long long long ptr) kernel32.MapViewOfFileEx
@ stdcall -version=0x600+ MapViewOfFileExNuma(long long long long long ptr long) kernel32.MapViewOfFileExNuma
@ stdcall MapViewOfFileFromApp(long long int64 long)
@ stdcall MoveFileExW(wstr wstr long) kernel32.MoveFileExW
@ stdcall MoveFileWithProgressW(wstr wstr ptr ptr long) kernel32.MoveFileWithProgressW
@ stdcall MulDiv(long long long) kernel32.MulDiv
@ stdcall MultiByteToWideChar(long long str long ptr long)
@ stdcall NeedCurrentDirectoryForExePathA(str) kernel32.NeedCurrentDirectoryForExePathA
@ stdcall NeedCurrentDirectoryForExePathW(wstr) kernel32.NeedCurrentDirectoryForExePathW
@ stub -version=0x600+ NlsCheckPolicy() kernel32.NlsCheckPolicy
@ stub NlsDispatchAnsiEnumProc
@ stub NlsGetACPFromLocale
@ stdcall NlsGetCacheUpdateCount() kernel32.NlsGetCacheUpdateCount
@ stub NlsIsUserDefaultLocale
@ stub -version=0x600+ NlsUpdateLocale() kernel32.NlsUpdateLocale
@ stub -version=0x600+ NlsUpdateSystemLocale() kernel32.NlsUpdateSystemLocale
@ stdcall NlsValidateLocale(ptr long)
@ stdcall NormalizeString(long wstr long ptr long)
@ stub NotifyMountMgr
@ stub NotifyRedirectedStringChange
@ stdcall ObjectCloseAuditAlarmW(wstr ptr long)
@ stdcall ObjectDeleteAuditAlarmW(wstr ptr long)
@ stdcall ObjectOpenAuditAlarmW(wstr ptr wstr wstr ptr long long long ptr long long ptr)
@ stdcall ObjectPrivilegeAuditAlarmW(wstr ptr long long ptr long)
@ stdcall OpenEventA(long long str) kernel32.OpenEventA
@ stdcall OpenEventW(long long wstr) kernel32.OpenEventW
@ stdcall -version=0x600+ OpenFileById(ptr ptr long long ptr long) kernel32.OpenFileById
@ stdcall OpenFileMappingFromApp(long long wstr)
@ stdcall OpenFileMappingW(long long wstr) kernel32.OpenFileMappingW
@ stdcall OpenMutexW(long long wstr) kernel32.OpenMutexW
@ stdcall OpenPrivateNamespaceW(ptr wstr) kernel32.OpenPrivateNamespaceW
@ stdcall OpenProcess(long long long) kernel32.OpenProcess
@ stdcall OpenProcessToken(long long ptr)
@ stub OpenRegKey
@ stdcall OpenSemaphoreW(long long wstr) kernel32.OpenSemaphoreW
@ stdcall OpenThread(long long long) kernel32.OpenThread
@ stdcall OpenThreadToken(long long long ptr)
@ stdcall OpenWaitableTimerW(long long wstr) kernel32.OpenWaitableTimerW
@ stdcall OutputDebugStringA(str) kernel32.OutputDebugStringA
@ stdcall OutputDebugStringW(wstr) kernel32.OutputDebugStringW
@ stdcall PackageFamilyNameFromFullName(wstr ptr ptr)
@ stdcall PackageIdFromFullName(wstr long ptr ptr)
@ stdcall ParseApplicationUserModelId(wstr ptr ptr ptr ptr)
@ stdcall ParseURLA(str ptr)
@ stdcall ParseURLW(wstr ptr)
@ stdcall PathAddBackslashA(str)
@ stdcall PathAddBackslashW(wstr)
@ stdcall PathAddExtensionA(str str)
@ stdcall PathAddExtensionW(wstr wstr)
@ stdcall PathAllocCanonicalize(wstr long ptr)
@ stdcall PathAllocCombine(wstr wstr long ptr)
@ stdcall PathAppendA(str str)
@ stdcall PathAppendW(wstr wstr)
@ stdcall PathCanonicalizeA(ptr str)
@ stdcall PathCanonicalizeW(ptr wstr)
@ stdcall PathCchAddBackslash(wstr long)
@ stdcall PathCchAddBackslashEx(wstr long ptr ptr)
@ stdcall PathCchAddExtension(wstr long wstr)
@ stdcall PathCchAppend(wstr long wstr)
@ stdcall PathCchAppendEx(wstr long wstr long)
@ stdcall PathCchCanonicalize(ptr long wstr)
@ stdcall PathCchCanonicalizeEx(ptr long wstr long)
@ stdcall PathCchCombine(ptr long wstr wstr)
@ stdcall PathCchCombineEx(ptr long wstr wstr long)
@ stdcall PathCchFindExtension(wstr long ptr)
@ stdcall PathCchIsRoot(wstr)
@ stdcall PathCchRemoveBackslash(wstr long)
@ stdcall PathCchRemoveBackslashEx(wstr long ptr ptr)
@ stdcall PathCchRemoveExtension(wstr long)
@ stdcall PathCchRemoveFileSpec(wstr long)
@ stdcall PathCchRenameExtension(wstr long wstr)
@ stdcall PathCchSkipRoot(wstr ptr)
@ stdcall PathCchStripPrefix(wstr long)
@ stdcall PathCchStripToRoot(wstr long)
@ stdcall PathCombineA(ptr str str)
@ stdcall PathCombineW(ptr wstr wstr)
@ stdcall PathCommonPrefixA(str str ptr)
@ stdcall PathCommonPrefixW(wstr wstr ptr)
@ stdcall PathCreateFromUrlA(str ptr ptr long)
@ stdcall PathCreateFromUrlAlloc(wstr ptr long)
@ stdcall PathCreateFromUrlW(wstr ptr ptr long)
@ stdcall PathFileExistsA(str)
@ stdcall PathFileExistsW(wstr)
@ stdcall PathFindExtensionA(str)
@ stdcall PathFindExtensionW(wstr)
@ stdcall PathFindFileNameA(str)
@ stdcall PathFindFileNameW(wstr)
@ stdcall PathFindNextComponentA(str)
@ stdcall PathFindNextComponentW(wstr)
@ stdcall PathGetArgsA(str)
@ stdcall PathGetArgsW(wstr)
@ stdcall PathGetCharTypeA(long)
@ stdcall PathGetCharTypeW(long)
@ stdcall PathGetDriveNumberA(str)
@ stdcall PathGetDriveNumberW(wstr)
@ stdcall PathIsFileSpecA(str)
@ stdcall PathIsFileSpecW(wstr)
@ stdcall PathIsLFNFileSpecA(str)
@ stdcall PathIsLFNFileSpecW(wstr)
@ stdcall PathIsPrefixA(str str)
@ stdcall PathIsPrefixW(wstr wstr)
@ stdcall PathIsRelativeA(str)
@ stdcall PathIsRelativeW(wstr)
@ stdcall PathIsRootA(str)
@ stdcall PathIsRootW(wstr)
@ stdcall PathIsSameRootA(str str)
@ stdcall PathIsSameRootW(wstr wstr)
@ stdcall PathIsUNCA(str)
@ stdcall PathIsUNCEx(wstr ptr)
@ stdcall PathIsUNCServerA(str)
@ stdcall PathIsUNCServerShareA(str)
@ stdcall PathIsUNCServerShareW(wstr)
@ stdcall PathIsUNCServerW(wstr)
@ stdcall PathIsUNCW(wstr)
@ stdcall PathIsURLA(str)
@ stdcall PathIsURLW(wstr)
@ stdcall PathIsValidCharA(long long)
@ stdcall PathIsValidCharW(long long)
@ stdcall PathMatchSpecA(str str)
@ stdcall PathMatchSpecExA(str str long)
@ stdcall PathMatchSpecExW(wstr wstr long)
@ stdcall PathMatchSpecW(wstr wstr)
@ stdcall PathParseIconLocationA(str)
@ stdcall PathParseIconLocationW(wstr)
@ stdcall PathQuoteSpacesA(str)
@ stdcall PathQuoteSpacesW(wstr)
@ stdcall PathRelativePathToA(ptr str long str long)
@ stdcall PathRelativePathToW(ptr wstr long wstr long)
@ stdcall PathRemoveBackslashA(str)
@ stdcall PathRemoveBackslashW(wstr)
@ stdcall PathRemoveBlanksA(str)
@ stdcall PathRemoveBlanksW(wstr)
@ stdcall PathRemoveExtensionA(str)
@ stdcall PathRemoveExtensionW(wstr)
@ stdcall PathRemoveFileSpecA(str)
@ stdcall PathRemoveFileSpecW(wstr)
@ stdcall PathRenameExtensionA(str str)
@ stdcall PathRenameExtensionW(wstr wstr)
@ stdcall PathSearchAndQualifyA(str ptr long)
@ stdcall PathSearchAndQualifyW(wstr ptr long)
@ stdcall PathSkipRootA(str)
@ stdcall PathSkipRootW(wstr)
@ stdcall PathStripPathA(str)
@ stdcall PathStripPathW(wstr)
@ stdcall PathStripToRootA(str)
@ stdcall PathStripToRootW(wstr)
@ stdcall PathUnExpandEnvStringsA(str ptr long)
@ stdcall PathUnExpandEnvStringsW(wstr ptr long)
@ stdcall PathUnquoteSpacesA(str)
@ stdcall PathUnquoteSpacesW(wstr)
@ stdcall PeekConsoleInputA(ptr ptr long ptr)
@ stdcall PeekConsoleInputW(ptr ptr long ptr)
@ stdcall PeekNamedPipe(long ptr long ptr ptr ptr) kernel32.PeekNamedPipe
@ stdcall PerfCreateInstance(long ptr wstr long)
@ stdcall PerfDeleteInstance(long ptr)
@ stdcall PerfSetCounterRefValue(long ptr long ptr)
@ stdcall PerfSetCounterSetInfo(long ptr long)
@ stdcall PerfSetULongCounterValue(long ptr long long)
@ stdcall PerfSetULongLongCounterValue(long ptr long int64)
@ stdcall PerfStartProvider(ptr ptr ptr)
@ stdcall PerfStartProviderEx(ptr ptr ptr)
@ stdcall PerfStopProvider(long)
@ stdcall PostQueuedCompletionStatus(long long ptr ptr) kernel32.PostQueuedCompletionStatus
@ stdcall PrivCopyFileExW(wstr wstr ptr ptr long long) kernel32.PrivCopyFileExW
@ stdcall PrivilegeCheck(ptr ptr ptr)
@ stdcall PrivilegedServiceAuditAlarmW(wstr wstr long ptr long)
@ stdcall ProcessIdToSessionId(long ptr) kernel32.ProcessIdToSessionId
@ stdcall PssCaptureSnapshot(ptr long long ptr)
@ stdcall PssFreeSnapshot(ptr ptr)
@ stdcall PssQuerySnapshot(ptr long ptr long)
@ stdcall PulseEvent(long) kernel32.PulseEvent
@ stdcall PurgeComm(long long) kernel32.PurgeComm
@ stdcall QISearch(ptr ptr ptr ptr)
@ stdcall -version=0x600+ QueryActCtxSettingsW(long ptr wstr wstr ptr long ptr) kernel32.QueryActCtxSettingsW
@ stdcall QueryActCtxW(long ptr ptr long ptr long ptr) kernel32.QueryActCtxW
@ stdcall QueryDepthSList(ptr) ntdll.RtlQueryDepthSList
@ stdcall QueryDosDeviceW(wstr ptr long) kernel32.QueryDosDeviceW
@ stdcall -version=0x600+ QueryFullProcessImageNameA(ptr long ptr ptr) kernel32.QueryFullProcessImageNameA
@ stdcall -version=0x600+ QueryFullProcessImageNameW(ptr long ptr ptr) kernel32.QueryFullProcessImageNameW
@ stdcall -version=0x600+ QueryIdleProcessorCycleTime(ptr ptr) kernel32.QueryIdleProcessorCycleTime
@ stdcall -version=0x601+ QueryIdleProcessorCycleTimeEx(long ptr ptr) kernel32.QueryIdleProcessorCycleTimeEx
@ stub QueryIoRingCapabilities
@ stdcall QueryMemoryResourceNotification(ptr ptr) kernel32.QueryMemoryResourceNotification
@ stdcall QueryPerformanceCounter(ptr) kernel32.QueryPerformanceCounter
@ stdcall QueryPerformanceFrequency(ptr) kernel32.QueryPerformanceFrequency
@ stdcall QueryProcessAffinityUpdateMode(ptr ptr) kernel32.QueryProcessAffinityUpdateMode
@ stdcall -version=0x600+ QueryProcessCycleTime(ptr ptr) kernel32.QueryProcessCycleTime
@ stub QuerySecurityAccessMask
@ stdcall -version=0x600+ QueryThreadCycleTime(ptr ptr) kernel32.QueryThreadCycleTime
@ stdcall QueryThreadpoolStackInformation(ptr ptr)
@ stdcall QueryVirtualMemoryInformation(long ptr long ptr long ptr)
@ stdcall -version=0x601+ QueryWorkingSet(long ptr long) kernel32.K32QueryWorkingSet
@ stdcall -version=0x601+ QueryWorkingSetEx(long ptr long) kernel32.K32QueryWorkingSetEx
@ stdcall QueueUserAPC(ptr long long) kernel32.QueueUserAPC
@ stdcall QueueUserWorkItem(ptr ptr long) kernel32.QueueUserWorkItem
@ stdcall QuirkIsEnabled(ptr)
@ stdcall QuirkIsEnabled3(ptr ptr)
@ stdcall RaiseException(long long long ptr) kernel32.RaiseException
@ stdcall RaiseFailFastException(ptr ptr long)
@ stdcall ReOpenFile(ptr long long long) kernel32.ReOpenFile
@ stdcall ReadConsoleA(long ptr long ptr ptr)
@ stdcall ReadConsoleInputA(long ptr long ptr)
@ stdcall ReadConsoleInputW(long ptr long ptr)
@ stdcall ReadConsoleOutputA(long ptr long long ptr)
@ stdcall ReadConsoleOutputAttribute(long ptr long long ptr)
@ stdcall ReadConsoleOutputCharacterA(long ptr long long ptr)
@ stdcall ReadConsoleOutputCharacterW(long ptr long long ptr)
@ stdcall ReadConsoleOutputW(long ptr long long ptr)
@ stdcall ReadConsoleW(long ptr long ptr ptr)
@ stdcall ReadDirectoryChangesW(long ptr long long long ptr ptr ptr) kernel32.ReadDirectoryChangesW
@ stdcall ReadFile(long ptr long ptr ptr) kernel32.ReadFile
@ stdcall ReadFileEx(long ptr long ptr ptr) kernel32.ReadFileEx
@ stdcall ReadFileScatter(long ptr long ptr ptr) kernel32.ReadFileScatter
@ stdcall ReadProcessMemory(long ptr ptr long ptr) kernel32.ReadProcessMemory
@ stdcall RegCloseKey(long)
@ stdcall RegCopyTreeW(long wstr long)
@ stdcall RegCreateKeyExA(long str long ptr long long ptr ptr ptr)
@ stdcall RegCreateKeyExW(long wstr long ptr long long ptr ptr ptr)
@ stdcall RegDeleteKeyExA(long str long long)
@ stdcall RegDeleteKeyExW(long wstr long long)
@ stdcall RegDeleteKeyValueA(long str str)
@ stdcall RegDeleteKeyValueW(long wstr wstr)
@ stdcall RegDeleteTreeA(long str)
@ stdcall RegDeleteTreeW(long wstr)
@ stdcall RegDeleteValueA(long str)
@ stdcall RegDeleteValueW(long wstr)
@ stdcall RegEnumKeyExA(long long ptr ptr ptr ptr ptr ptr)
@ stdcall RegEnumKeyExW(long long ptr ptr ptr ptr ptr ptr)
@ stdcall RegEnumValueA(long long ptr ptr ptr ptr ptr ptr)
@ stdcall RegEnumValueW(long long ptr ptr ptr ptr ptr ptr)
@ stdcall RegFlushKey(long)
@ stdcall RegGetKeySecurity(long long ptr ptr)
@ stdcall RegGetValueA(long str str long ptr ptr ptr)
@ stdcall RegGetValueW(long wstr wstr long ptr ptr ptr)
@ stdcall RegLoadAppKeyA(str ptr long long long)
@ stdcall RegLoadAppKeyW(wstr ptr long long long)
@ stdcall RegLoadKeyA(long str str)
@ stdcall RegLoadKeyW(long wstr wstr)
@ stdcall RegLoadMUIStringA(long str str long ptr long str)
@ stdcall RegLoadMUIStringW(long wstr wstr long ptr long wstr)
@ stdcall RegNotifyChangeKeyValue(long long long long long)
@ stdcall RegOpenCurrentUser(long ptr)
@ stdcall RegOpenKeyExA(long str long long ptr)
@ stdcall RegOpenKeyExW(long wstr long long ptr)
@ stdcall RegOpenUserClassesRoot(ptr long long ptr)
@ stdcall RegQueryInfoKeyA(long ptr ptr ptr ptr ptr ptr ptr ptr ptr ptr ptr)
@ stdcall RegQueryInfoKeyW(long ptr ptr ptr ptr ptr ptr ptr ptr ptr ptr ptr)
@ stdcall RegQueryValueExA(long str ptr ptr ptr ptr)
@ stdcall RegQueryValueExW(long wstr ptr ptr ptr ptr)
@ stdcall RegRestoreKeyA(long str long)
@ stdcall RegRestoreKeyW(long wstr long)
@ stdcall RegSaveKeyExA(long str ptr long)
@ stdcall RegSaveKeyExW(long wstr ptr long)
@ stdcall RegSetKeySecurity(long long ptr)
@ stdcall RegSetKeyValueA(long str str long ptr long)
@ stdcall RegSetKeyValueW(long wstr wstr long ptr long)
@ stdcall RegSetValueExA(long str long long ptr long)
@ stdcall RegSetValueExW(long wstr long long ptr long)
@ stdcall RegUnLoadKeyA(long str)
@ stdcall RegUnLoadKeyW(long wstr)
@ stdcall RegisterTraceGuidsW(ptr ptr ptr long ptr wstr wstr ptr) ntdll.EtwRegisterTraceGuidsW
@ stdcall RegisterWaitForSingleObjectEx(long ptr ptr long long) kernel32.RegisterWaitForSingleObjectEx
@ stdcall ReleaseActCtx(ptr) kernel32.ReleaseActCtx
@ stdcall ReleaseMutex(long) kernel32.ReleaseMutex
@ stdcall -version=0x600+ ReleaseMutexWhenCallbackReturns(ptr ptr) ntdll.TpCallbackReleaseMutexOnCompletion
@ stdcall -version=0x600+ ReleaseSRWLockExclusive(ptr) ntdll.RtlReleaseSRWLockExclusive
@ stdcall -version=0x600+ ReleaseSRWLockShared(ptr) ntdll.RtlReleaseSRWLockShared
@ stdcall ReleaseSemaphore(long long ptr) kernel32.ReleaseSemaphore
@ stdcall -version=0x600+ ReleaseSemaphoreWhenCallbackReturns(ptr ptr long) ntdll.TpCallbackReleaseSemaphoreOnCompletion
@ stdcall RemapPredefinedHandleInternal(long long)
@ stdcall RemoveDirectoryA(str) kernel32.RemoveDirectoryA
@ stdcall RemoveDirectoryW(wstr) kernel32.RemoveDirectoryW
@ stdcall -version=0x601+ RemoveDllDirectory(ptr) kernel32.RemoveDllDirectory
@ stdcall RemoveVectoredContinueHandler(ptr) ntdll.RtlRemoveVectoredContinueHandler
@ stdcall RemoveVectoredExceptionHandler(ptr) ntdll.RtlRemoveVectoredExceptionHandler
@ stdcall ReplaceFileW(wstr wstr wstr long ptr ptr) kernel32.ReplaceFileW
@ stdcall ResetEvent(long) kernel32.ResetEvent
@ stdcall ResetWriteWatch(ptr long) kernel32.ResetWriteWatch
@ stdcall ResizePseudoConsole(ptr long)
@ stdcall -version=0x602+ ResolveDelayLoadedAPI(ptr ptr ptr ptr ptr long) ntdll.LdrResolveDelayLoadedAPI
@ stdcall ResolveLocaleName(wstr ptr long)
@ stdcall RestoreLastError(long) ntdll.RtlRestoreLastWin32Error
@ stdcall ResumeThread(long) kernel32.ResumeThread
@ stdcall RevertToSelf()
@ stdcall SHExpandEnvironmentStringsA(str ptr long) kernel32.ExpandEnvironmentStringsA
@ stdcall SHExpandEnvironmentStringsW(wstr ptr long) kernel32.ExpandEnvironmentStringsW
@ stdcall SHLoadIndirectString(wstr ptr long ptr)
@ stdcall SHRegCloseUSKey(ptr)
@ stdcall SHRegCreateUSKeyA(str long long ptr long)
@ stdcall SHRegCreateUSKeyW(wstr long long ptr long)
@ stdcall SHRegDeleteEmptyUSKeyA(long str long)
@ stdcall SHRegDeleteEmptyUSKeyW(long wstr long)
@ stdcall SHRegDeleteUSValueA(long str long)
@ stdcall SHRegDeleteUSValueW(long wstr long)
@ stdcall SHRegEnumUSKeyA(long long str ptr long)
@ stdcall SHRegEnumUSKeyW(long long wstr ptr long)
@ stdcall SHRegEnumUSValueA(long long ptr ptr ptr ptr ptr long)
@ stdcall SHRegEnumUSValueW(long long ptr ptr ptr ptr ptr long)
@ stdcall SHRegGetBoolUSValueA(str str long long)
@ stdcall SHRegGetBoolUSValueW(wstr wstr long long)
@ stdcall SHRegGetUSValueA(str str ptr ptr ptr long ptr long)
@ stdcall SHRegGetUSValueW(wstr wstr ptr ptr ptr long ptr long)
@ stdcall SHRegOpenUSKeyA(str long long ptr long)
@ stdcall SHRegOpenUSKeyW(wstr long long ptr long)
@ stdcall SHRegQueryInfoUSKeyA(long ptr ptr ptr ptr long)
@ stdcall SHRegQueryInfoUSKeyW(long ptr ptr ptr ptr long)
@ stdcall SHRegQueryUSValueA(long str ptr ptr ptr long ptr long)
@ stdcall SHRegQueryUSValueW(long wstr ptr ptr ptr long ptr long)
@ stdcall SHRegSetUSValueA(str str long ptr long long)
@ stdcall SHRegSetUSValueW(wstr wstr long ptr long long)
@ stdcall SHRegWriteUSValueA(long str long ptr long long)
@ stdcall SHRegWriteUSValueW(long wstr long ptr long long)
@ stdcall SHTruncateString(str long)
@ stdcall ScrollConsoleScreenBufferA(long ptr ptr ptr ptr)
@ stdcall ScrollConsoleScreenBufferW(long ptr ptr ptr ptr)
@ stdcall SearchPathA(str str str long ptr ptr) kernel32.SearchPathA
@ stdcall SearchPathW(wstr wstr wstr long ptr ptr) kernel32.SearchPathW
@ stdcall SetAclInformation(ptr ptr long long)
@ stdcall SetCachedSigningLevel(ptr long long long)
@ stdcall SetCalendarInfoW(long long long wstr) kernel32.SetCalendarInfoW
@ stdcall SetCommBreak(long) kernel32.SetCommBreak
@ stdcall SetCommConfig(long ptr long) kernel32.SetCommConfig
@ stdcall SetCommMask(long ptr) kernel32.SetCommMask
@ stdcall SetCommState(long ptr) kernel32.SetCommState
@ stdcall SetCommTimeouts(long ptr) kernel32.SetCommTimeouts
@ stdcall SetComputerNameA(str)
@ stdcall SetComputerNameExA(long str)
@ stdcall SetComputerNameExW(long wstr)
@ stdcall SetComputerNameW(wstr)
@ stdcall SetConsoleActiveScreenBuffer(long)
@ stdcall SetConsoleCP(long)
@ stdcall SetConsoleCtrlHandler(ptr long)
@ stdcall SetConsoleCursorInfo(long ptr)
@ stdcall SetConsoleCursorPosition(long long)
@ stdcall SetConsoleDisplayMode(long long ptr)
@ stdcall SetConsoleHistoryInfo(ptr)
@ stdcall SetConsoleInputExeNameA(ptr)
@ stdcall SetConsoleInputExeNameW(ptr)
@ stdcall SetConsoleMode(long long)
@ stdcall SetConsoleOutputCP(long)
@ stdcall SetConsoleScreenBufferInfoEx(ptr ptr)
@ stdcall SetConsoleScreenBufferSize(long long)
@ stdcall SetConsoleTextAttribute(long long)
@ stdcall SetCriticalSectionSpinCount(ptr long) ntdll.RtlSetCriticalSectionSpinCount
@ stdcall SetCurrentConsoleFontEx(ptr long ptr)
@ stdcall SetConsoleTitleA(str)
@ stdcall SetConsoleTitleW(wstr)
@ stdcall SetConsoleWindowInfo(long long ptr)
@ stdcall SetCurrentDirectoryA(str) kernel32.SetCurrentDirectoryA
@ stdcall SetCurrentDirectoryW(wstr) kernel32.SetCurrentDirectoryW
@ stdcall -version=0x602+ SetDefaultDllDirectories(long) kernel32.SetDefaultDllDirectories
@ stdcall SetEndOfFile(long) kernel32.SetEndOfFile
@ stdcall SetEnvironmentStringsA(ptr) kernel32.SetEnvironmentStringsA
@ stdcall SetEnvironmentStringsW(ptr) kernel32.SetEnvironmentStringsW
@ stdcall SetEnvironmentVariableA(str str) kernel32.SetEnvironmentVariableA
@ stdcall SetEnvironmentVariableW(wstr wstr) kernel32.SetEnvironmentVariableW
@ stdcall SetErrorMode(long) kernel32.SetErrorMode
@ stdcall SetEvent(long) kernel32.SetEvent
@ stdcall -version=0x600+ SetEventWhenCallbackReturns(ptr ptr) ntdll.TpCallbackSetEventOnCompletion
@ stdcall SetFileApisToANSI() kernel32.SetFileApisToANSI
@ stdcall SetFileApisToOEM() kernel32.SetFileApisToOEM
@ stdcall SetFileAttributesA(str long) kernel32.SetFileAttributesA
@ stdcall SetFileAttributesW(wstr long) kernel32.SetFileAttributesW
@ stdcall -version=0x600+ SetFileInformationByHandle(ptr long ptr long) kernel32.SetFileInformationByHandle
@ stdcall SetFilePointer(long long ptr long) kernel32.SetFilePointer
@ stdcall SetFilePointerEx(long double ptr long) kernel32.SetFilePointerEx
@ stdcall SetFileSecurityW(wstr long ptr)
@ stdcall SetFileTime(long ptr ptr ptr) kernel32.SetFileTime
@ stdcall SetFileValidData(long double) kernel32.SetFileValidData
@ stdcall SetHandleCount(long) kernel32.SetHandleCount
@ stdcall SetHandleInformation(long long long) kernel32.SetHandleInformation
@ stdcall SetKernelObjectSecurity(long long ptr)
@ stdcall SetLastError(long) ntdll.RtlSetLastWin32Error
@ stdcall SetLocalTime(ptr) kernel32.SetLocalTime
@ stdcall SetLocaleInfoW(long long wstr) kernel32.SetLocaleInfoW
@ stdcall SetNamedPipeHandleState(long ptr ptr ptr) kernel32.SetNamedPipeHandleState
@ stdcall SetPriorityClass(long long) kernel32.SetPriorityClass
@ stdcall SetPrivateObjectSecurity(long ptr ptr ptr long)
@ stdcall SetPrivateObjectSecurityEx(long ptr ptr long ptr long)
@ stdcall SetProcessAffinityUpdateMode(ptr long) kernel32.SetProcessAffinityUpdateMode
@ stub SetProcessDefaultCpuSets
@ stub SetProcessGroupAffinity
@ stdcall SetProcessInformation(long long ptr long) kernel32.SetProcessInformation
@ stdcall SetProcessMitigationPolicy(long ptr long) kernel32.SetProcessMitigationPolicy
@ stdcall SetProcessPreferredUILanguages(long ptr ptr)
@ stdcall SetProcessPriorityBoost(long long) kernel32.SetProcessPriorityBoost
@ stdcall SetProcessShutdownParameters(long long) kernel32.SetProcessShutdownParameters
@ stdcall SetProcessValidCallTargets(long ptr long long ptr) kernel32.SetProcessValidCallTargets
@ stdcall SetProcessWorkingSetSizeEx(long long long long) kernel32.SetProcessWorkingSetSizeEx
@ stdcall SetSecurityAccessMask(long ptr) advapi32.SetSecurityAccessMask
@ stdcall SetSecurityDescriptorControl(ptr long long)
@ stdcall SetSecurityDescriptorDacl(ptr long ptr long)
@ stdcall SetSecurityDescriptorGroup(ptr ptr long)
@ stdcall SetSecurityDescriptorOwner(ptr ptr long)
@ stdcall SetSecurityDescriptorRMControl(ptr ptr) advapi32.SetSecurityDescriptorRMControl
@ stdcall SetSecurityDescriptorSacl(ptr long ptr long)
@ stdcall SetStdHandle(long long)
@ stdcall -version=0x600+ SetStdHandleEx(long long ptr) kernel32.SetStdHandleEx
@ stdcall SetSystemFileCacheSize(long long long) kernel32.SetSystemFileCacheSize
@ stdcall SetSystemTime(ptr) kernel32.SetSystemTime
@ stdcall SetSystemTimeAdjustment(long long) kernel32.SetSystemTimeAdjustment
@ stdcall SetThreadContext(long ptr) kernel32.SetThreadContext
@ stdcall SetThreadDescription(ptr wstr)
@ stdcall SetThreadErrorMode(long ptr)
@ stdcall -version=0x601+ SetThreadGroupAffinity(long ptr ptr) kernel32.SetThreadGroupAffinity
@ stdcall SetThreadIdealProcessor(long long) kernel32.SetThreadIdealProcessor
@ stdcall SetThreadIdealProcessorEx(long ptr ptr) kernel32.SetThreadIdealProcessorEx
@ stdcall SetThreadInformation(long long ptr long)
@ stdcall SetThreadLocale(long) kernel32.SetThreadLocale
@ stdcall SetThreadPreferredUILanguages(long ptr ptr)
@ stdcall SetThreadPriority(long long) kernel32.SetThreadPriority
@ stdcall SetThreadPriorityBoost(long long) kernel32.SetThreadPriorityBoost
@ stub SetThreadSelectedCpuSets
@ stdcall SetThreadStackGuarantee(ptr) kernel32.SetThreadStackGuarantee
@ stdcall SetThreadToken(ptr ptr)
@ stdcall SetThreadUILanguage(long) kernel32.SetThreadUILanguage
@ stdcall SetThreadpoolStackInformation(ptr ptr)
@ stdcall SetThreadpoolThreadMaximum(ptr long) ntdll.TpSetPoolMaxThreads
@ stdcall SetThreadpoolThreadMinimum(ptr long) ntdll.TpSetPoolMinThreads
@ stdcall SetThreadpoolTimer(ptr ptr long long) ntdll.TpSetTimer
@ stdcall SetThreadpoolWait(ptr long ptr) ntdll.TpSetWait
@ stdcall SetTimeZoneInformation(ptr) kernel32.SetTimeZoneInformation
@ stdcall SetTokenInformation(long long ptr long)
@ stdcall SetUnhandledExceptionFilter(ptr) kernel32.SetUnhandledExceptionFilter
@ stdcall SetUserGeoID(long)
@ stdcall SetUserGeoName(wstr)
@ stdcall SetWaitableTimer(long ptr long ptr ptr long) kernel32.SetWaitableTimer
@ stdcall SetWaitableTimerEx(long ptr long ptr ptr ptr long) kernel32.SetWaitableTimerEx
@ stdcall -arch=x86_64,arm64ec SetXStateFeaturesMask(ptr int64)
@ stdcall SetupComm(long long long) kernel32.SetupComm
@ stdcall SignalObjectAndWait(long long long long) kernel32.SignalObjectAndWait
@ stdcall SizeofResource(long long) kernel32.SizeofResource
@ stdcall Sleep(long)
@ stdcall SleepConditionVariableCS(ptr ptr long)
@ stdcall SleepConditionVariableSRW(ptr ptr long long)
@ stdcall SleepEx(long long)
@ stub SpecialMBToWC
@ stdcall StartThreadpoolIo(ptr) ntdll.TpStartAsyncIoOperation
@ stdcall StrCSpnA(str str)
@ stdcall StrCSpnIA(str str)
@ stdcall StrCSpnIW(wstr wstr)
@ stdcall StrCSpnW(wstr wstr)
@ stdcall StrCatBuffA(str str long)
@ stdcall StrCatBuffW(wstr wstr long)
@ stdcall StrCatChainW(ptr long long wstr)
@ stdcall StrChrA(str long)
@ stdcall StrChrIA(str long)
@ stdcall StrChrIW(wstr long)
@ stdcall StrChrNW(wstr long long)
@ stdcall StrChrW(wstr long)
@ stdcall StrCmpCA(str str)
@ stdcall StrCmpCW(wstr wstr)
@ stdcall StrCmpICA(str str)
@ stdcall StrCmpICW(wstr wstr)
@ stdcall StrCmpIW(wstr wstr)
@ stdcall StrCmpLogicalW(wstr wstr)
@ stdcall StrCmpNA(str str long)
@ stdcall StrCmpNCA(str str long)
@ stdcall StrCmpNCW(wstr wstr long)
@ stdcall StrCmpNIA(str str long)
@ stdcall StrCmpNICA(str str long)
@ stdcall StrCmpNICW(wstr wstr long)
@ stdcall StrCmpNIW(wstr wstr long)
@ stdcall StrCmpNW(wstr wstr long)
@ stdcall StrCmpW(wstr wstr)
@ stdcall StrCpyNW(ptr wstr long)
@ stdcall StrCpyNXA(ptr str long)
@ stdcall StrCpyNXW(ptr wstr long)
@ stdcall StrDupA(str)
@ stdcall StrDupW(wstr)
@ stdcall StrIsIntlEqualA(long str str long)
@ stdcall StrIsIntlEqualW(long wstr wstr long)
@ stdcall StrPBrkA(str str)
@ stdcall StrPBrkW(wstr wstr)
@ stdcall StrRChrA(str str long)
@ stdcall StrRChrIA(str str long)
@ stdcall StrRChrIW(wstr wstr long)
@ stdcall StrRChrW(wstr wstr long)
@ stdcall StrRStrIA(str str str)
@ stdcall StrRStrIW(wstr wstr wstr)
@ stdcall StrSpnA(str str)
@ stdcall StrSpnW(wstr wstr)
@ stdcall StrStrA(str str)
@ stdcall StrStrIA(str str)
@ stdcall StrStrIW(wstr wstr)
@ stdcall StrStrNIW(wstr wstr long)
@ stdcall StrStrNW(wstr wstr long)
@ stdcall StrStrW(wstr wstr)
@ stdcall StrToInt64ExA(str long ptr)
@ stdcall StrToInt64ExW(wstr long ptr)
@ stdcall StrToIntA(str)
@ stdcall StrToIntExA(str long ptr)
@ stdcall StrToIntExW(wstr long ptr)
@ stdcall StrToIntW(wstr)
@ stdcall StrTrimA(str str)
@ stdcall StrTrimW(wstr wstr)
@ stdcall SubmitThreadpoolWork(ptr) ntdll.TpPostWork
@ stdcall SuspendThread(long) kernel32.SuspendThread
@ stdcall SwitchToFiber(ptr) kernel32.SwitchToFiber
@ stdcall SwitchToThread() kernel32.SwitchToThread
@ stdcall SystemTimeToFileTime(ptr ptr) kernel32.SystemTimeToFileTime
@ stdcall SystemTimeToTzSpecificLocalTime(ptr ptr ptr) kernel32.SystemTimeToTzSpecificLocalTime
@ stub SystemTimeToTzSpecificLocalTimeEx
@ stdcall TerminateProcess(ptr long) kernel32.TerminateProcess
@ stdcall TerminateThread(ptr long) kernel32.TerminateThread
@ stdcall TlsAlloc() kernel32.TlsAlloc
@ stdcall TlsFree(long) kernel32.TlsFree
@ stdcall -norelay TlsGetValue(long) kernel32.TlsGetValue
@ stdcall -norelay TlsSetValue(long ptr) kernel32.TlsSetValue
@ stdcall TraceEvent(double ptr) advapi32.TraceEvent
@ varargs TraceMessage(int64 long ptr long) ntdll.EtwTraceMessage
@ stdcall TraceMessageVa(int64 long ptr long ptr) ntdll.EtwTraceMessageVa
@ stdcall TransactNamedPipe(long ptr long ptr long ptr ptr) kernel32.TransactNamedPipe
@ stdcall TransmitCommChar(long long) kernel32.TransmitCommChar
@ stdcall -version=0x601+ TryAcquireSRWLockExclusive(ptr) ntdll.RtlTryAcquireSRWLockExclusive
@ stdcall -version=0x601+ TryAcquireSRWLockShared(ptr) ntdll.RtlTryAcquireSRWLockShared
@ stdcall TryEnterCriticalSection(ptr) ntdll.RtlTryEnterCriticalSection
@ stdcall TrySubmitThreadpoolCallback(ptr ptr ptr)
@ stdcall TzSpecificLocalTimeToSystemTime(ptr ptr ptr) kernel32.TzSpecificLocalTimeToSystemTime
@ stub TzSpecificLocalTimeToSystemTimeEx
@ stdcall UnhandledExceptionFilter(ptr) kernel32.UnhandledExceptionFilter
@ stdcall UnlockFile(long long long long long) kernel32.UnlockFile
@ stdcall UnlockFileEx(long long long long ptr) kernel32.UnlockFileEx
@ stdcall UnmapViewOfFile(ptr) kernel32.UnmapViewOfFile
@ stdcall -arch=win64 UnmapViewOfFile2(long ptr long)
@ stdcall -version=0x602+ UnmapViewOfFileEx(ptr long) kernel32.UnmapViewOfFileEx
@ stdcall UnregisterTraceGuids(int64) ntdll.EtwUnregisterTraceGuids
@ stdcall UnregisterWaitEx(long long) kernel32.UnregisterWaitEx
@ stdcall -version=0x600+ UpdateProcThreadAttribute(ptr long ptr ptr ptr ptr ptr) kernel32.UpdateProcThreadAttribute
@ stdcall UrlApplySchemeA(str ptr ptr long)
@ stdcall UrlApplySchemeW(wstr ptr ptr long)
@ stdcall UrlCanonicalizeA(str ptr ptr long)
@ stdcall UrlCanonicalizeW(wstr ptr ptr long)
@ stdcall UrlCombineA(str str ptr ptr long)
@ stdcall UrlCombineW(wstr wstr ptr ptr long)
@ stdcall UrlCompareA(str str long)
@ stdcall UrlCompareW(wstr wstr long)
@ stdcall UrlCreateFromPathA(str ptr ptr long)
@ stdcall UrlCreateFromPathW(wstr ptr ptr long)
@ stdcall UrlEscapeA(str ptr ptr long)
@ stdcall UrlEscapeW(wstr ptr ptr long)
@ stdcall UrlFixupW(wstr wstr long)
@ stdcall UrlGetLocationA(str)
@ stdcall UrlGetLocationW(wstr)
@ stdcall UrlGetPartA(str ptr ptr long long)
@ stdcall UrlGetPartW(wstr ptr ptr long long)
@ stdcall UrlHashA(str ptr long)
@ stdcall UrlHashW(wstr ptr long)
@ stdcall UrlIsA(str long)
@ stdcall UrlIsNoHistoryA(str)
@ stdcall UrlIsNoHistoryW(wstr)
@ stdcall UrlIsOpaqueA(str)
@ stdcall UrlIsOpaqueW(wstr)
@ stdcall UrlIsW(wstr long)
@ stdcall UrlUnescapeA(str ptr ptr long)
@ stdcall UrlUnescapeW(wstr ptr ptr long)
@ stdcall VerFindFileA(long str str str ptr ptr ptr ptr)
@ stdcall VerFindFileW(long wstr wstr wstr ptr ptr ptr ptr)
@ stdcall VerLanguageNameA(long str long) kernel32.VerLanguageNameA
@ stdcall VerLanguageNameW(long wstr long) kernel32.VerLanguageNameW
@ stdcall VerQueryValueA(ptr str ptr ptr)
@ stdcall VerQueryValueW(ptr wstr ptr ptr)
@ stdcall PrefetchVirtualMemory(ptr ptr ptr long) kernel32.PrefetchVirtualMemory
@ stdcall VerSetConditionMask(long long long long) ntdll.VerSetConditionMask
@ stdcall VirtualAlloc(ptr long long long) kernel32.VirtualAlloc
@ stdcall VirtualAlloc2(long ptr long long long ptr long)
@ stdcall VirtualAlloc2FromApp(long ptr long long long ptr long)
@ stdcall VirtualAllocEx(long ptr long long long) kernel32.VirtualAllocEx
@ stdcall -version=0x600+ VirtualAllocExNuma(long ptr long long long long) kernel32.VirtualAllocExNuma
@ stdcall VirtualAllocFromApp(ptr long long long)
@ stdcall VirtualFree(ptr long long) kernel32.VirtualFree
@ stdcall VirtualFreeEx(long ptr long long) kernel32.VirtualFreeEx
@ stdcall VirtualLock(ptr long) kernel32.VirtualLock
@ stdcall VirtualProtect(ptr long long ptr) kernel32.VirtualProtect
@ stdcall VirtualProtectEx(long ptr long long ptr) kernel32.VirtualProtectEx
@ stdcall VirtualQuery(ptr ptr long) kernel32.VirtualQuery
@ stdcall VirtualQueryEx(long ptr ptr long) kernel32.VirtualQueryEx
@ stdcall VirtualUnlock(ptr long) kernel32.VirtualUnlock
@ stdcall WaitCommEvent(long ptr ptr) kernel32.WaitCommEvent
@ stdcall WaitForDebugEvent(ptr long) kernel32.WaitForDebugEvent
@ stdcall -version=0xA00+ WaitForDebugEventEx(ptr long) kernel32.WaitForDebugEventEx
@ stdcall WaitForMultipleObjects(long ptr long long) kernel32.WaitForMultipleObjects
@ stdcall WaitForMultipleObjectsEx(long ptr long long long) kernel32.WaitForMultipleObjectsEx
@ stdcall WaitForSingleObject(long long) kernel32.WaitForSingleObject
@ stdcall WaitForSingleObjectEx(long long long) kernel32.WaitForSingleObjectEx
@ stdcall WaitForThreadpoolIoCallbacks(ptr long) ntdll.TpWaitForIoCompletion
@ stdcall WaitForThreadpoolTimerCallbacks(ptr long) ntdll.TpWaitForTimer
@ stdcall WaitForThreadpoolWaitCallbacks(ptr long) ntdll.TpWaitForWait
@ stdcall WaitForThreadpoolWorkCallbacks(ptr long) ntdll.TpWaitForWork
@ stdcall WaitNamedPipeW(wstr long) kernel32.WaitNamedPipeW
@ stdcall WaitOnAddress(ptr ptr long long)
@ stdcall WakeAllConditionVariable(ptr) ntdll.RtlWakeAllConditionVariable
@ stdcall WakeByAddressAll(ptr) ntdll.RtlWakeAddressAll
@ stdcall WakeByAddressSingle(ptr) ntdll.RtlWakeAddressSingle
@ stdcall WakeConditionVariable(ptr) ntdll.RtlWakeConditionVariable
@ stdcall WerGetFlags(ptr ptr)
@ stdcall WerRegisterFile(wstr long long)
@ stdcall WerRegisterMemoryBlock(ptr long)
@ stdcall WerRegisterRuntimeExceptionModule(wstr ptr)
@ stdcall WerSetFlags(long)
@ stdcall WerUnregisterFile(wstr)
@ stdcall WerUnregisterMemoryBlock(ptr)
@ stdcall WerUnregisterRuntimeExceptionModule(wstr ptr)
@ stdcall WideCharToMultiByte(long long wstr long ptr long ptr ptr)
@ stdcall Wow64DisableWow64FsRedirection(ptr) kernel32.Wow64DisableWow64FsRedirection
@ stdcall Wow64EnableWow64FsRedirection(long) kernel32.Wow64EnableWow64FsRedirection
@ stdcall Wow64GetThreadContext(long ptr)
@ stdcall Wow64RevertWow64FsRedirection(ptr) kernel32.Wow64RevertWow64FsRedirection
@ stdcall Wow64SetThreadContext(long ptr)
@ stdcall WriteConsoleA(long ptr long ptr ptr)
@ stdcall WriteConsoleInputA(long ptr long ptr)
@ stdcall WriteConsoleInputW(long ptr long ptr)
@ stdcall WriteConsoleOutputA(long ptr long long ptr)
@ stdcall WriteConsoleOutputAttribute(long ptr long long ptr)
@ stdcall WriteConsoleOutputCharacterA(long ptr long long ptr)
@ stdcall WriteConsoleOutputCharacterW(long ptr long long ptr)
@ stdcall WriteConsoleOutputW(long ptr long long ptr)
@ stdcall WriteConsoleW(long ptr long ptr ptr)
@ stdcall WriteFile(long ptr long ptr ptr) kernel32.WriteFile
@ stdcall WriteFileEx(long ptr long ptr ptr) kernel32.WriteFileEx
@ stdcall WriteFileGather(long ptr long ptr ptr) kernel32.WriteFileGather
@ stdcall WriteProcessMemory(long ptr ptr long ptr) kernel32.WriteProcessMemory
@ stdcall ZombifyActCtx(ptr) kernel32.ZombifyActCtx
@ stdcall -arch=win64 __C_specific_handler() ntdll.__C_specific_handler
@ stdcall -arch=x86_64,arm64 __chkstk() ntdll.__chkstk
@ stdcall -arch=x86_64 _local_unwind() ntdll._local_unwind
@ stdcall lstrcmp(str str) lstrcmpA
@ stdcall lstrcmpA(str str)
@ stdcall lstrcmpW(wstr wstr)
@ stdcall lstrcmpi(str str) lstrcmpiA
@ stdcall lstrcmpiA(str str)
@ stdcall lstrcmpiW(wstr wstr)
@ stdcall lstrcpyn(ptr str long) kernel32.lstrcpyn
@ stdcall lstrcpynA(ptr str long) kernel32.lstrcpynA
@ stdcall lstrcpynW(ptr wstr long) kernel32.lstrcpynW
@ stdcall lstrlen(str) kernel32.lstrlen
@ stdcall lstrlenA(str) kernel32.lstrlenA
@ stdcall lstrlenW(wstr) kernel32.lstrlenW
@ stdcall GetNumaProcessorNodeEx(ptr ptr) kernel32.GetNumaProcessorNodeEx
@ stdcall CloseConsoleHandle(long)
@ stdcall -version=0x502-0x600 -arch=win64 ConsoleIMERoutine(ptr)
@ stdcall ConsoleMenuControl(long long long)
@ stdcall DuplicateConsoleHandle(long long long long)
@ stdcall GetConsoleAliasExesA(str long)
@ stdcall GetConsoleAliasExesW(wstr long)
@ stdcall GetConsoleAliasesA(str long str)
@ stdcall GetConsoleAliasesW(wstr long wstr)
@ stdcall GetConsoleCharType(long long ptr)
@ stdcall GetConsoleCursorMode(long ptr ptr)
@ stdcall GetConsoleFontInfo(long long long ptr)
@ stdcall GetConsoleHardwareState(long long ptr)
@ stdcall GetConsoleInputWaitHandle()
@ stdcall GetConsoleKeyboardLayoutNameA(ptr)
@ stdcall GetConsoleKeyboardLayoutNameW(ptr)
@ stdcall GetConsoleNlsMode(long ptr)
@ stdcall GetConsoleSelectionInfo(ptr)
@ stdcall GetNumberOfConsoleFonts()
@ stdcall InvalidateConsoleDIBits(long long)
@ stdcall OpenConsoleW(wstr long long long)
@ stdcall ReadConsoleInputExA(long ptr long ptr long)
@ stdcall ReadConsoleInputExW(long ptr long ptr long)
@ stdcall RegisterConsoleIME(ptr ptr)
@ stdcall RegisterConsoleOS2(long)
@ stdcall -version=0x351-0x502 SetConsoleCommandHistoryMode(long)
@ stdcall SetConsoleCursor(long long)
@ stdcall SetConsoleCursorMode(long long long)
@ stdcall SetConsoleFont(long long)
@ stdcall SetConsoleHardwareState(long long long)
@ stdcall SetConsoleIcon(ptr)
@ stdcall SetConsoleKeyShortcuts(long long long long)
@ stdcall SetConsoleLocalEUDC(long long long long)
@ stdcall SetConsoleMaximumWindowSize(long long)
@ stdcall SetConsoleMenuClose(long)
@ stdcall SetConsoleNlsMode(long long)
@ stdcall SetConsoleNumberOfCommandsA(long long)
@ stdcall SetConsoleNumberOfCommandsW(long long)
@ stdcall SetConsoleOS2OemFormat(long)
@ stdcall SetConsolePalette(long long long)
@ stdcall SetLastConsoleEventActive()
@ stdcall ShowConsoleCursor(long long)
@ stdcall UnregisterConsoleIME()
@ stdcall VerifyConsoleIoHandle(long)
@ stdcall WriteConsoleInputVDMA(long long long long)
@ stdcall WriteConsoleInputVDMW(long long long long)
@ stdcall ConDllInitialize(long wstr)
@ stdcall QueryInterruptTime(ptr) kernel32.QueryInterruptTime
@ stdcall QueryInterruptTimePrecise(ptr) kernel32.QueryInterruptTimePrecise
@ stdcall QueryUnbiasedInterruptTime(ptr) kernel32.QueryUnbiasedInterruptTime
@ stdcall QueryUnbiasedInterruptTimePrecise(ptr) kernel32.QueryUnbiasedInterruptTimePrecise
