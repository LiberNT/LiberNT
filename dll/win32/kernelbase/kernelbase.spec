@ stdcall AccessCheck(ptr long long ptr ptr ptr ptr ptr)
@ stdcall AccessCheckAndAuditAlarmW(wstr ptr wstr wstr ptr long ptr long ptr ptr ptr)
@ stdcall AccessCheckByType(ptr ptr long long ptr long ptr ptr ptr ptr ptr)
@ stdcall AccessCheckByTypeAndAuditAlarmW(wstr ptr wstr wstr ptr ptr long long long ptr long ptr long ptr ptr ptr) advapi32.AccessCheckByTypeAndAuditAlarmW
@ stdcall AccessCheckByTypeResultList(ptr ptr long long ptr long ptr ptr ptr ptr ptr)
@ stdcall AccessCheckByTypeResultListAndAuditAlarmByHandleW(wstr ptr ptr wstr wstr ptr long long long long ptr long ptr long ptr ptr ptr) advapi32.AccessCheckByTypeResultListAndAuditAlarmByHandleW
@ stdcall AccessCheckByTypeResultListAndAuditAlarmW(wstr ptr wstr wstr ptr long long long long ptr long ptr long ptr ptr ptr) advapi32.AccessCheckByTypeResultListAndAuditAlarmW
@ stdcall -version=0x600+ AcquireSRWLockExclusive(ptr) ntdll.RtlAcquireSRWLockExclusive
@ stdcall -version=0x600+ AcquireSRWLockShared(ptr) ntdll.RtlAcquireSRWLockShared
@ stdcall -thunk ActivateActCtx(ptr ptr) kernel32.ActivateActCtx
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
@ stdcall -thunk -version=0x601+ AddDllDirectory(wstr) kernel32.AddDllDirectory
@ stdcall AddMandatoryAce(ptr long long long ptr)
@ stdcall -thunk AddRefActCtx(ptr) kernel32.AddRefActCtx
@ stdcall AddResourceAttributeAce(ptr long long long ptr ptr ptr)
@ stdcall -thunk AddSIDToBoundaryDescriptor(ptr ptr) kernel32.AddSIDToBoundaryDescriptor
# TODO: Windows 11 kernelbase does not export this
@ stdcall AddIntegrityLabelToBoundaryDescriptor(ptr ptr) kernel32.AddIntegrityLabelToBoundaryDescriptor
@ stdcall AddScopedPolicyIDAce(ptr long long long ptr)
@ stdcall AddVectoredContinueHandler(long ptr) ntdll.RtlAddVectoredContinueHandler
@ stdcall AddVectoredExceptionHandler(long ptr) ntdll.RtlAddVectoredExceptionHandler
@ stdcall AdjustTokenGroups(long long ptr long ptr ptr)
@ stdcall AdjustTokenPrivileges(long long ptr long ptr ptr)
@ stdcall AllocConsole()
@ stdcall AllocateAndInitializeSid(ptr long long long long long long long long long ptr)
@ stdcall AllocateLocallyUniqueId(ptr)
@ stdcall -thunk AllocateUserPhysicalPages(long ptr ptr) kernel32.AllocateUserPhysicalPages
@ stdcall -thunk -version=0x600+ AllocateUserPhysicalPagesNuma(long ptr ptr long) kernel32.AllocateUserPhysicalPagesNuma
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
@ stdcall -thunk AreFileApisANSI() kernel32.AreFileApisANSI
@ stdcall AttachConsole(long)
@ stdcall -thunk BaseCheckAppcompatCache(wstr ptr wstr ptr) kernel32.BaseCheckAppcompatCache
@ stdcall -thunk BaseCleanupAppcompatCacheSupport(ptr) kernel32.BaseCleanupAppcompatCacheSupport
@ stub BaseDllFreeResourceId
@ stub BaseDllMapResourceIdW
@ stdcall -thunk BaseDumpAppcompatCache() kernel32.BaseDumpAppcompatCache
@ stdcall -thunk BaseFlushAppcompatCache() kernel32.BaseFlushAppcompatCache
@ stdcall -thunk BaseFreeAppCompatDataForProcess(ptr) kernel32.BaseFreeAppCompatDataForProcess
@ stdcall -thunk BaseGetNamedObjectDirectory(ptr) kernel32.BaseGetNamedObjectDirectory
@ stdcall -thunk BaseInitAppcompatCacheSupport() kernel32.BaseInitAppcompatCacheSupport
@ stdcall -thunk BaseReadAppCompatDataForProcess(ptr ptr ptr) kernel32.BaseReadAppCompatDataForProcess
@ stdcall -thunk BaseUpdateAppcompatCache(long long long) kernel32.BaseUpdateAppcompatCache
@ stdcall -thunk BasepAdjustObjectAttributesForPrivateNamespace(ptr) kernel32.BasepAdjustObjectAttributesForPrivateNamespace
@ stdcall -thunk Beep(long long) kernel32.Beep
@ stdcall -thunk CallNamedPipeW(wstr ptr long ptr long ptr long) kernel32.CallNamedPipeW
@ stdcall CallbackMayRunLong(ptr)
@ stdcall -thunk CancelIo(long) kernel32.CancelIo
@ stdcall -thunk -version=0x600+ CancelIoEx(ptr ptr) kernel32.CancelIoEx
@ stdcall CancelSynchronousIo(long)
@ stdcall CancelThreadpoolIo(ptr) ntdll.TpCancelAsyncIoOperation
@ stdcall -thunk CancelWaitableTimer(long) kernel32.CancelWaitableTimer
@ stdcall -thunk ChangeTimerQueueTimer(ptr ptr long long) kernel32.ChangeTimerQueueTimer
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
@ stdcall -thunk CheckRemoteDebuggerPresent(long ptr) kernel32.CheckRemoteDebuggerPresent
@ stdcall CheckTokenCapability(ptr ptr ptr)
@ stdcall CheckTokenMembership(long ptr ptr)
@ stdcall CheckTokenMembershipEx(ptr ptr long ptr)
@ stdcall ChrCmpIA(long long)
@ stdcall ChrCmpIW(long long)
@ stdcall -thunk ClearCommBreak(long) kernel32.ClearCommBreak
@ stdcall -thunk ClearCommError(long ptr ptr) kernel32.ClearCommError
@ stdcall -thunk CloseHandle(long) kernel32.CloseHandle
@ stdcall -thunk ClosePrivateNamespace(ptr long) kernel32.ClosePrivateNamespace
@ stdcall ClosePseudoConsole(ptr)
@ stdcall CloseThreadpool(ptr) ntdll.TpReleasePool
@ stdcall CloseThreadpoolCleanupGroup(ptr) ntdll.TpReleaseCleanupGroup
@ stdcall CloseThreadpoolCleanupGroupMembers(ptr long ptr) ntdll.TpReleaseCleanupGroupMembers
@ stdcall CloseThreadpoolIo(ptr) ntdll.TpReleaseIoCompletion
@ stdcall CloseThreadpoolTimer(ptr) ntdll.TpReleaseTimer
@ stdcall CloseThreadpoolWait(ptr) ntdll.TpReleaseWait
@ stdcall CloseThreadpoolWork(ptr) ntdll.TpReleaseWork
@ stdcall CommandLineToArgvW(wstr ptr) shell32.CommandLineToArgvW
@ stdcall -thunk CompareFileTime(ptr ptr) kernel32.CompareFileTime
@ stdcall -thunk -version=0xA00+ CompareObjectHandles(ptr ptr) kernel32.CompareObjectHandles
@ stdcall CompareStringA(long long str long str long)
@ stdcall CompareStringEx(wstr long wstr long wstr long ptr ptr long)
@ stdcall CompareStringOrdinal(wstr long wstr long long)
@ stdcall CompareStringW(long long wstr long wstr long)
@ stdcall -thunk ConnectNamedPipe(long ptr) kernel32.ConnectNamedPipe
@ stdcall -thunk ContinueDebugEvent(long long long) kernel32.ContinueDebugEvent
@ stdcall -thunk ConvertDefaultLocale(long) kernel32.ConvertDefaultLocale
@ stdcall -thunk ConvertFiberToThread() kernel32.ConvertFiberToThread
@ stdcall -thunk ConvertThreadToFiber(ptr) kernel32.ConvertThreadToFiber
@ stdcall -thunk ConvertThreadToFiberEx(ptr long) kernel32.ConvertThreadToFiberEx
@ stdcall ConvertToAutoInheritPrivateObjectSecurity(ptr ptr ptr ptr long ptr)
@ stdcall CopyContext(ptr long ptr)
@ stdcall -thunk -version=0x602+ CopyFile2(wstr wstr ptr) kernel32.CopyFile2
@ stdcall -thunk CopyFileExW(wstr wstr ptr ptr ptr long) kernel32.CopyFileExW
@ stdcall -thunk CopyFileW(wstr wstr long) kernel32.CopyFileW
@ stdcall -arch=x86_64 CopyMemoryNonTemporal(ptr ptr long) ntdll.RtlCopyMemoryNonTemporal
@ stdcall CopySid(long ptr ptr)
@ stdcall -thunk CreateActCtxW(ptr) kernel32.CreateActCtxW
@ stdcall -thunk CreateBoundaryDescriptorW(wstr long) kernel32.CreateBoundaryDescriptorW
@ stdcall CreateConsoleScreenBuffer(long long ptr long ptr)
@ stdcall -thunk CreateDirectoryA(str ptr) kernel32.CreateDirectoryA
@ stdcall -thunk CreateDirectoryExW(wstr wstr ptr) kernel32.CreateDirectoryExW
@ stdcall -thunk CreateDirectoryW(wstr ptr) kernel32.CreateDirectoryW
@ stdcall -thunk CreateEventA(ptr long long str) kernel32.CreateEventA
@ stdcall CreateEventExA(ptr str long long)
@ stdcall CreateEventExW(ptr wstr long long)
@ stdcall -thunk CreateEventW(ptr long long wstr) kernel32.CreateEventW
@ stdcall -thunk CreateFiber(long ptr ptr) kernel32.CreateFiber
@ stdcall -thunk CreateFiberEx(long long long ptr ptr) kernel32.CreateFiberEx
@ stdcall -thunk -version=0x602+ CreateFile2(wstr long long long ptr) kernel32.CreateFile2
@ stdcall -thunk CreateFileA(str long long ptr long long long) kernel32.CreateFileA
@ stdcall CreateFileMappingFromApp(long ptr long int64 wstr)
@ stdcall -thunk -version=0x600+ CreateFileMappingNumaW(long ptr long long long wstr long) kernel32.CreateFileMappingNumaW
@ stdcall -thunk CreateFileMappingW(long ptr long long long wstr) kernel32.CreateFileMappingW
@ stdcall -thunk CreateFileW(wstr long long ptr long long long) kernel32.CreateFileW
@ stdcall -thunk CreateHardLinkA(str str ptr) kernel32.CreateHardLinkA
@ stdcall -thunk CreateHardLinkW(wstr wstr ptr) kernel32.CreateHardLinkW
@ stdcall -thunk CreateIoCompletionPort(long long long long) kernel32.CreateIoCompletionPort
@ stdcall -thunk CreateMemoryResourceNotification(long) kernel32.CreateMemoryResourceNotification
@ stdcall -thunk CreateMutexA(ptr long str) kernel32.CreateMutexA
@ stdcall CreateMutexExA(ptr str long long)
@ stdcall CreateMutexExW(ptr wstr long long)
@ stdcall -thunk CreateMutexW(ptr long wstr) kernel32.CreateMutexW
@ stdcall -thunk CreateNamedPipeW(wstr long long long long long long ptr) kernel32.CreateNamedPipeW
@ stdcall -thunk CreatePipe(ptr ptr ptr long) kernel32.CreatePipe
@ stdcall -thunk CreatePrivateNamespaceW(ptr ptr wstr) kernel32.CreatePrivateNamespaceW
@ stdcall CreatePrivateObjectSecurity(ptr ptr ptr long long ptr)
@ stdcall CreatePrivateObjectSecurityEx(ptr ptr ptr ptr long long long ptr)
@ stdcall CreatePrivateObjectSecurityWithMultipleInheritance(ptr ptr ptr ptr long long long long ptr)
@ stdcall -thunk CreateProcessA(str str ptr ptr long long ptr str ptr ptr) kernel32.CreateProcessA
@ stdcall CreateProcessAsUserA(long str str ptr ptr long long ptr str ptr ptr) advapi32.CreateProcessAsUserA
@ stdcall CreateProcessAsUserW(ptr wstr wstr ptr ptr long long ptr wstr ptr ptr) advapi32.CreateProcessAsUserW
@ stdcall -thunk CreateProcessInternalA(ptr str str ptr ptr long long ptr str ptr ptr long) kernel32.CreateProcessInternalA
@ stdcall -thunk CreateProcessInternalW(ptr wstr wstr ptr ptr long long ptr wstr ptr ptr long) kernel32.CreateProcessInternalW
@ stdcall -thunk CreateProcessW(wstr wstr ptr ptr long long ptr wstr ptr ptr) kernel32.CreateProcessW
@ stdcall CreatePseudoConsole(long long long long ptr)
@ stdcall -thunk CreateRemoteThread(long ptr long ptr long long ptr) kernel32.CreateRemoteThread
@ stdcall -thunk CreateRemoteThreadEx(long ptr long ptr long long ptr ptr) kernel32.CreateRemoteThreadEx
@ stdcall CreateRestrictedToken(long long long ptr long ptr long ptr ptr)
@ stdcall -thunk -version=0x600+ CreateSemaphoreExW(ptr long long wstr long long) kernel32.CreateSemaphoreExW
@ stdcall -thunk CreateSemaphoreW(ptr long long wstr) kernel32.CreateSemaphoreW
@ stdcall -thunk -version=0x600+ CreateSymbolicLinkW(wstr wstr long) kernel32.CreateSymbolicLinkW
@ stdcall -thunk CreateThread(ptr long ptr long long ptr) kernel32.CreateThread
@ stdcall CreateThreadpool(ptr)
@ stdcall CreateThreadpoolCleanupGroup()
@ stdcall CreateThreadpoolIo(ptr ptr ptr ptr)
@ stdcall CreateThreadpoolTimer(ptr ptr ptr)
@ stdcall CreateThreadpoolWait(ptr ptr ptr)
@ stdcall CreateThreadpoolWork(ptr ptr ptr)
@ stdcall -thunk CreateTimerQueue() kernel32.CreateTimerQueue
@ stdcall -thunk CreateTimerQueueTimer(ptr long ptr ptr long long long) kernel32.CreateTimerQueueTimer
@ stdcall -thunk -version=0x600+ CreateWaitableTimerExW(ptr wstr long long) kernel32.CreateWaitableTimerExW
@ stdcall -thunk CreateWaitableTimerW(ptr long wstr) kernel32.CreateWaitableTimerW
@ stdcall CreateWellKnownSid(long ptr ptr ptr)
@ stdcall CtrlRoutine(ptr)
@ stdcall -thunk DeactivateActCtx(long ptr) kernel32.DeactivateActCtx
@ stdcall -thunk DebugActiveProcess(long) kernel32.DebugActiveProcess
@ stdcall -thunk DebugActiveProcessStop(long) kernel32.DebugActiveProcessStop
@ stdcall DebugBreak() ntdll.DbgBreakPoint
@ stdcall DecodePointer(ptr) ntdll.RtlDecodePointer
@ stdcall DecodeSystemPointer(ptr) ntdll.RtlDecodeSystemPointer
@ stdcall -thunk DefineDosDeviceW(long wstr wstr) kernel32.DefineDosDeviceW
@ stdcall -thunk DelayLoadFailureHook(str str) kernel32.DelayLoadFailureHook
@ stdcall DeleteCriticalSection(ptr) ntdll.RtlDeleteCriticalSection
@ stdcall -thunk DeleteFiber(ptr) kernel32.DeleteFiber
@ stdcall -thunk DeleteFileA(str) kernel32.DeleteFileA
@ stdcall -thunk DeleteFileW(wstr) kernel32.DeleteFileW
@ stdcall -thunk DeleteTimerQueueEx(long long) kernel32.DeleteTimerQueueEx
@ stdcall -thunk DeleteTimerQueueTimer(long long long) kernel32.DeleteTimerQueueTimer
@ stdcall -thunk DeleteVolumeMountPointW(wstr) kernel32.DeleteVolumeMountPointW
@ stdcall DeriveCapabilitySidsFromName(ptr ptr ptr ptr ptr)
@ stdcall DeleteAce(ptr long)
@ stdcall -thunk DeleteBoundaryDescriptor(ptr) kernel32.DeleteBoundaryDescriptor
@ stdcall -thunk -version=0x600+ DeleteProcThreadAttributeList(ptr) kernel32.DeleteProcThreadAttributeList
@ stdcall -thunk DeleteSynchronizationBarrier(ptr) kernel32.DeleteSynchronizationBarrier
@ stdcall DestroyPrivateObjectSecurity(ptr)
@ stdcall -thunk DeviceIoControl(long long ptr long ptr long ptr ptr) kernel32.DeviceIoControl
@ stdcall DisablePredefinedHandleTableInternal(long)
@ stdcall -thunk DisableThreadLibraryCalls(ptr) kernel32.DisableThreadLibraryCalls
@ stdcall -version=0x600+ DisassociateCurrentThreadFromCallback(ptr) ntdll.TpDisassociateCallback
@ stdcall -thunk -version=0x603+ DiscardVirtualMemory(ptr long) kernel32.DiscardVirtualMemory
@ stdcall -thunk DisconnectNamedPipe(long) kernel32.DisconnectNamedPipe
@ stdcall DnsHostnameToComputerNameExW(wstr ptr ptr)
@ stdcall -thunk DuplicateHandle(long long long ptr long long long) kernel32.DuplicateHandle
@ stdcall DuplicateToken(long long ptr)
@ stdcall DuplicateTokenEx(long long ptr long long ptr)
@ stdcall -thunk -version=0x601+ EmptyWorkingSet(long) kernel32.K32EmptyWorkingSet
@ stdcall EncodePointer(ptr) ntdll.RtlEncodePointer
@ stdcall EncodeSystemPointer(ptr) ntdll.RtlEncodeSystemPointer
@ stdcall EnterCriticalSection(ptr) ntdll.RtlEnterCriticalSection
@ stdcall -thunk EnterSynchronizationBarrier(ptr long) kernel32.EnterSynchronizationBarrier
@ stdcall EnumCalendarInfoExEx(ptr wstr long wstr long long)
@ stdcall EnumCalendarInfoExW(ptr long long long)
@ stdcall EnumCalendarInfoW(ptr long long long)
@ stdcall -version=0x600+ EnumDateFormatsExEx(ptr wstr long long)
@ stdcall EnumDateFormatsExW(ptr long long)
@ stdcall EnumDateFormatsW(ptr long long)
@ stdcall -thunk -version=0x601+ EnumDeviceDrivers(ptr long ptr) kernel32.K32EnumDeviceDrivers
@ stdcall EnumDynamicTimeZoneInformation(long ptr)
@ stdcall -thunk EnumLanguageGroupLocalesW(ptr long long ptr) kernel32.EnumLanguageGroupLocalesW
@ stdcall -thunk -version=0x601+ EnumPageFilesA(ptr ptr) kernel32.K32EnumPageFilesA
@ stdcall -thunk -version=0x601+ EnumPageFilesW(ptr ptr) kernel32.K32EnumPageFilesW
@ stdcall -thunk -version=0x601+ EnumProcessModules(long ptr long ptr) kernel32.K32EnumProcessModules
@ stdcall -thunk -version=0x601+ EnumProcessModulesEx(long ptr long ptr long) kernel32.K32EnumProcessModulesEx
@ stdcall -thunk -version=0x601+ EnumProcesses(ptr long ptr) kernel32.K32EnumProcesses
@ stdcall -thunk EnumResourceLanguagesExA(long str str ptr long long long) kernel32.EnumResourceLanguagesExA
@ stdcall -thunk EnumResourceLanguagesExW(long wstr wstr ptr long long long) kernel32.EnumResourceLanguagesExW
@ stdcall -thunk EnumResourceNamesExA(long str ptr long long long) kernel32.EnumResourceNamesExA
@ stdcall -thunk EnumResourceNamesExW(long wstr ptr long long long) kernel32.EnumResourceNamesExW
@ stdcall -thunk EnumResourceNamesW(long wstr ptr long) kernel32.EnumResourceNamesW
@ stdcall -thunk EnumResourceTypesExA(long ptr long long long) kernel32.EnumResourceTypesExA
@ stdcall -thunk EnumResourceTypesExW(long ptr long long long) kernel32.EnumResourceTypesExW
@ stdcall -thunk EnumSystemCodePagesW(ptr long) kernel32.EnumSystemCodePagesW
@ stdcall -thunk EnumSystemFirmwareTables(long ptr long) kernel32.EnumSystemFirmwareTables
@ stdcall EnumSystemGeoID(long long ptr)
@ stdcall -thunk EnumSystemLanguageGroupsW(ptr long ptr) kernel32.EnumSystemLanguageGroupsW
@ stdcall EnumSystemLocalesA(ptr long)
@ stdcall EnumSystemLocalesEx(ptr long long ptr)
@ stdcall EnumSystemLocalesW(ptr long)
@ stdcall -version=0x600+ EnumTimeFormatsEx(ptr wstr long long)
@ stdcall EnumTimeFormatsW(ptr long long)
@ stdcall -thunk EnumUILanguagesW(ptr long long) kernel32.EnumUILanguagesW
@ stdcall EqualDomainSid(ptr ptr ptr)
@ stdcall EqualPrefixSid(ptr ptr)
@ stdcall EqualSid(ptr ptr)
@ stdcall -thunk EscapeCommFunction(long long) kernel32.EscapeCommFunction
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
# TODO: Windows 11 kernelbase forwards this to NTDLL
@ stdcall ExitProcess(long) kernel32.ExitProcess
@ stdcall ExitThread(long) kernel32.ExitThread
@ stdcall -thunk ExpandEnvironmentStringsA(str ptr long) kernel32.ExpandEnvironmentStringsA
@ stdcall -thunk ExpandEnvironmentStringsW(wstr ptr long) kernel32.ExpandEnvironmentStringsW
@ stdcall ExpungeConsoleCommandHistoryA(long)
@ stdcall ExpungeConsoleCommandHistoryW(long)
@ stdcall -thunk FatalAppExitA(long str) kernel32.FatalAppExitA
@ stdcall -thunk FatalAppExitW(long wstr) kernel32.FatalAppExitW
@ stdcall -thunk FileTimeToLocalFileTime(ptr ptr) kernel32.FileTimeToLocalFileTime
@ stdcall -thunk FileTimeToSystemTime(ptr ptr) kernel32.FileTimeToSystemTime
@ stdcall FillConsoleOutputAttribute(long long long long ptr)
@ stdcall FillConsoleOutputCharacterA(long long long long ptr)
@ stdcall FillConsoleOutputCharacterW(long long long long ptr)
@ stdcall -thunk FindActCtxSectionGuid(long ptr long ptr ptr) kernel32.FindActCtxSectionGuid
@ stdcall -thunk FindActCtxSectionStringW(long ptr long wstr ptr) kernel32.FindActCtxSectionStringW
@ stdcall -thunk FindClose(long) kernel32.FindClose
@ stdcall -thunk FindCloseChangeNotification(long) kernel32.FindCloseChangeNotification
@ stdcall -thunk FindFirstChangeNotificationA(str long long) kernel32.FindFirstChangeNotificationA
@ stdcall -thunk FindFirstChangeNotificationW(wstr long long) kernel32.FindFirstChangeNotificationW
@ stdcall -thunk FindFirstFileA(str ptr) kernel32.FindFirstFileA
@ stdcall -thunk FindFirstFileExA(str long ptr long ptr long) kernel32.FindFirstFileExA
@ stdcall -thunk FindFirstFileExW(wstr long ptr long ptr long) kernel32.FindFirstFileExW
@ stdcall -thunk -version=0x600+ FindFirstFileNameW(wstr long ptr ptr) kernel32.FindFirstFileNameW
@ stdcall -thunk FindFirstFileW(wstr ptr) kernel32.FindFirstFileW
@ stdcall FindFirstFreeAce(ptr ptr)
@ stdcall -thunk FindFirstStreamW(wstr ptr ptr long) kernel32.FindFirstStreamW
@ stdcall -thunk FindFirstVolumeW(ptr long) kernel32.FindFirstVolumeW
@ stdcall FindNLSString(long long wstr long wstr long ptr)
@ stdcall FindNLSStringEx(wstr long wstr long wstr long ptr ptr ptr long)
@ stdcall -thunk FindNextChangeNotification(long) kernel32.FindNextChangeNotification
@ stdcall -thunk FindNextFileA(long ptr) kernel32.FindNextFileA
@ stdcall -thunk -version=0x600+ FindNextFileNameW(ptr ptr ptr) kernel32.FindNextFileNameW
@ stdcall -thunk FindNextFileW(long ptr) kernel32.FindNextFileW
@ stdcall -thunk FindNextStreamW(ptr ptr) kernel32.FindNextStreamW
@ stdcall -thunk FindNextVolumeW(long ptr long) kernel32.FindNextVolumeW
@ stdcall FindPackagesByPackageFamily(wstr long ptr ptr ptr ptr ptr)
@ stdcall -thunk FindResourceExW(long wstr wstr long) kernel32.FindResourceExW
@ stdcall -thunk FindResourceW(long wstr wstr) kernel32.FindResourceW
@ stdcall FindStringOrdinal(long wstr long wstr long long)
@ stdcall -thunk FindVolumeClose(ptr) kernel32.FindVolumeClose
@ stdcall -thunk FlsAlloc(ptr) kernel32.FlsAlloc
@ stdcall -thunk FlsFree(long) kernel32.FlsFree
@ stdcall -thunk FlsGetValue(long) kernel32.FlsGetValue
@ stdcall -thunk FlsSetValue(long ptr) kernel32.FlsSetValue
@ stdcall FlushConsoleInputBuffer(long)
@ stdcall -thunk FlushFileBuffers(long) kernel32.FlushFileBuffers
@ stdcall -thunk FlushInstructionCache(long long long) kernel32.FlushInstructionCache
# TODO: Windows 11 kernelbase forwards this to NTDLL
@ stdcall -version=0x600+ FlushProcessWriteBuffers() kernel32.FlushProcessWriteBuffers
@ stdcall -thunk FlushViewOfFile(ptr long) kernel32.FlushViewOfFile
@ stdcall -thunk FoldStringW(long wstr long ptr long) kernel32.FoldStringW
@ stdcall FormatMessageA(long ptr long long ptr long ptr)
@ stdcall FormatMessageW(long ptr long long ptr long ptr)
@ stdcall FreeConsole()
@ stdcall -thunk FreeEnvironmentStringsA(ptr) kernel32.FreeEnvironmentStringsA
@ stdcall -thunk FreeEnvironmentStringsW(ptr) kernel32.FreeEnvironmentStringsW
@ stdcall -thunk FreeLibrary(long) kernel32.FreeLibrary
@ stdcall -thunk FreeLibraryAndExitThread(long long) kernel32.FreeLibraryAndExitThread
@ stdcall -version=0x600+ FreeLibraryWhenCallbackReturns(ptr ptr) ntdll.TpCallbackUnloadDllOnCompletion
@ stdcall -thunk FreeResource(long) kernel32.FreeResource
@ stdcall FreeSid(ptr)
@ stdcall -thunk FreeUserPhysicalPages(long long long) kernel32.FreeUserPhysicalPages
@ stdcall GenerateConsoleCtrlEvent(long long)
@ stdcall -thunk GetACP() kernel32.GetACP
@ stdcall GetAcceptLanguagesA(ptr ptr)
@ stdcall GetAcceptLanguagesW(ptr ptr)
@ stdcall GetAce(ptr long ptr)
@ stdcall GetAclInformation(ptr ptr long long)
@ stdcall GetAppContainerAce(ptr long ptr ptr)
@ stdcall GetAppContainerNamedObjectPath(ptr ptr long ptr ptr)
@ stdcall -thunk -version=0x600+ GetApplicationRecoveryCallback(ptr ptr ptr ptr ptr) kernel32.GetApplicationRecoveryCallback
@ stdcall -thunk -version=0x600+ GetApplicationRestartSettings(ptr ptr ptr ptr) kernel32.GetApplicationRestartSettings
@ stdcall GetApplicationUserModelId(ptr ptr ptr)
@ stdcall GetApplicationUserModelIdFromToken(ptr ptr ptr)
@ stub GetCPFileNameFromRegistry
@ stub GetCPHashNode
@ stdcall -thunk GetCPInfo(long ptr) kernel32.GetCPInfo
@ stdcall -thunk GetCPInfoExW(long long ptr) kernel32.GetCPInfoExW
@ stub GetCalendar
@ stdcall GetCalendarInfoEx(wstr long ptr long ptr long ptr)
@ stdcall GetCalendarInfoW(long long long ptr long ptr)
@ stdcall -thunk GetCommConfig(long ptr long) kernel32.GetCommConfig
@ stdcall -thunk GetCommMask(long ptr) kernel32.GetCommMask
@ stdcall -thunk GetCommModemStatus(long ptr) kernel32.GetCommModemStatus
@ stdcall -thunk GetCommProperties(long ptr) kernel32.GetCommProperties
@ stdcall -thunk GetCommState(long ptr) kernel32.GetCommState
@ stdcall -thunk GetCommTimeouts(long ptr) kernel32.GetCommTimeouts
@ stdcall -thunk GetCommandLineA() kernel32.GetCommandLineA
@ stdcall -thunk GetCommandLineW() kernel32.GetCommandLineW
@ stdcall -thunk GetCompressedFileSizeA(long ptr) kernel32.GetCompressedFileSizeA
@ stdcall -thunk GetCompressedFileSizeW(long ptr) kernel32.GetCompressedFileSizeW
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
@ stdcall -thunk -version=0x600+ GetCurrencyFormatEx(wstr long wstr ptr wstr long) kernel32.GetCurrencyFormatEx
@ stdcall -thunk GetCurrencyFormatW(long long wstr ptr wstr long) kernel32.GetCurrencyFormatW
@ stdcall -thunk GetCurrentActCtx(ptr) kernel32.GetCurrentActCtx
@ stdcall GetCurrentApplicationUserModelId(ptr ptr)
@ stdcall GetCurrentConsoleFont(long long ptr)
@ stdcall GetCurrentConsoleFontEx(ptr long ptr)
@ stdcall -thunk GetCurrentDirectoryA(long ptr) kernel32.GetCurrentDirectoryA
@ stdcall -thunk GetCurrentDirectoryW(long ptr) kernel32.GetCurrentDirectoryW
@ stdcall GetCurrentPackageFamilyName(ptr ptr)
@ stdcall -thunk -version=0x602+ GetCurrentPackageFullName(ptr ptr) kernel32.GetCurrentPackageFullName
@ stdcall GetCurrentPackageId(ptr ptr)
@ stdcall GetCurrentPackageInfo(long ptr ptr ptr)
@ stdcall GetCurrentPackagePath(ptr ptr)
@ stdcall -thunk -norelay GetCurrentProcess() kernel32.GetCurrentProcess
@ stdcall -thunk -norelay GetCurrentProcessId() kernel32.GetCurrentProcessId
@ stdcall GetCurrentProcessorNumber() ntdll.RtlGetCurrentProcessorNumber
@ stdcall -version=0x601+ GetCurrentProcessorNumberEx(ptr) ntdll.RtlGetCurrentProcessorNumberEx
@ stdcall -thunk -norelay GetCurrentThread() kernel32.GetCurrentThread
@ stdcall -thunk -norelay GetCurrentThreadId() kernel32.GetCurrentThreadId
@ stdcall GetCurrentThreadStackLimits(ptr ptr)
@ stdcall -thunk GetDateFormatA(long long ptr str ptr long) kernel32.GetDateFormatA
@ stdcall -thunk -version=0x600+ GetDateFormatEx(wstr long ptr wstr wstr long wstr) kernel32.GetDateFormatEx
@ stdcall -thunk GetDateFormatW(long long ptr wstr ptr long) kernel32.GetDateFormatW
@ stdcall -thunk -version=0x601+ GetDeviceDriverBaseNameA(ptr ptr long) kernel32.K32GetDeviceDriverBaseNameA
@ stdcall -thunk -version=0x601+ GetDeviceDriverBaseNameW(ptr ptr long) kernel32.K32GetDeviceDriverBaseNameW
@ stdcall -thunk -version=0x601+ GetDeviceDriverFileNameA(ptr ptr long) kernel32.K32GetDeviceDriverFileNameA
@ stdcall -thunk -version=0x601+ GetDeviceDriverFileNameW(ptr ptr long) kernel32.K32GetDeviceDriverFileNameW
@ stdcall -thunk GetDiskFreeSpaceA(str ptr ptr ptr ptr) kernel32.GetDiskFreeSpaceA
@ stdcall -thunk GetDiskFreeSpaceExA(str ptr ptr ptr) kernel32.GetDiskFreeSpaceExA
@ stdcall -thunk GetDiskFreeSpaceExW(wstr ptr ptr ptr) kernel32.GetDiskFreeSpaceExW
@ stdcall -thunk GetDiskFreeSpaceW(wstr ptr ptr ptr ptr) kernel32.GetDiskFreeSpaceW
@ stdcall -thunk GetDriveTypeA(str) kernel32.GetDriveTypeA
@ stdcall -thunk GetDriveTypeW(wstr) kernel32.GetDriveTypeW
@ stdcall GetDurationFormatEx(wstr long ptr int64 wstr ptr long)
@ stdcall -thunk -version=0x600+ GetDynamicTimeZoneInformation(ptr) kernel32.GetDynamicTimeZoneInformation
@ stdcall -thunk -version=0x602+ GetDynamicTimeZoneInformationEffectiveYears(ptr ptr ptr) kernel32.GetDynamicTimeZoneInformationEffectiveYears
@ stdcall -thunk -version=0x601+ GetEnabledXStateFeatures() kernel32.GetEnabledXStateFeatures
@ stdcall -thunk GetEnvironmentStrings() kernel32.GetEnvironmentStrings
@ stdcall -thunk GetEnvironmentStringsA() kernel32.GetEnvironmentStringsA
@ stdcall -thunk GetEnvironmentStringsW() kernel32.GetEnvironmentStringsW
@ stdcall -thunk GetEnvironmentVariableA(str ptr long) kernel32.GetEnvironmentVariableA
@ stdcall -thunk GetEnvironmentVariableW(wstr ptr long) kernel32.GetEnvironmentVariableW
@ stub GetEraNameCountedString
@ stdcall -thunk -version=0x600+ GetErrorMode() kernel32.GetErrorMode
@ stdcall -thunk GetExitCodeProcess(long ptr) kernel32.GetExitCodeProcess
@ stdcall -thunk GetExitCodeThread(long ptr) kernel32.GetExitCodeThread
@ stub GetFallbackDisplayName
@ stdcall -thunk GetFileAttributesA(str) kernel32.GetFileAttributesA
@ stdcall -thunk GetFileAttributesExA(str long ptr) kernel32.GetFileAttributesExA
@ stdcall -thunk GetFileAttributesExW(wstr long ptr) kernel32.GetFileAttributesExW
@ stdcall -thunk GetFileAttributesW(wstr) kernel32.GetFileAttributesW
@ stdcall -thunk GetFileInformationByHandle(long ptr) kernel32.GetFileInformationByHandle
@ stdcall -thunk -version=0x600+ GetFileInformationByHandleEx(ptr long ptr long) kernel32.GetFileInformationByHandleEx
@ stdcall GetFileMUIInfo(long wstr ptr ptr)
@ stdcall -thunk GetFileMUIPath(long wstr wstr ptr wstr ptr ptr) kernel32.GetFileMUIPath
@ stdcall GetFileSecurityW(wstr long ptr long ptr)
@ stdcall -thunk GetFileSize(long ptr) kernel32.GetFileSize
@ stdcall -thunk GetFileSizeEx(long ptr) kernel32.GetFileSizeEx
@ stdcall -thunk GetFileTime(long ptr ptr ptr) kernel32.GetFileTime
@ stdcall -thunk GetFileType(long) kernel32.GetFileType
@ stdcall GetFileVersionInfoA(str long long ptr)
@ stdcall GetFileVersionInfoExA(long str long long ptr)
@ stdcall GetFileVersionInfoExW(long wstr long long ptr)
@ stdcall GetFileVersionInfoSizeA(str ptr)
@ stdcall GetFileVersionInfoSizeExA(long str ptr)
@ stdcall GetFileVersionInfoSizeExW(long wstr ptr)
@ stdcall GetFileVersionInfoSizeW(wstr ptr)
@ stdcall GetFileVersionInfoW(wstr long long ptr)
@ stdcall -thunk -version=0x600+ GetFinalPathNameByHandleA(ptr str long long) kernel32.GetFinalPathNameByHandleA
@ stdcall -thunk -version=0x600+ GetFinalPathNameByHandleW(ptr wstr long long) kernel32.GetFinalPathNameByHandleW
@ stdcall -thunk GetFullPathNameA(str long ptr ptr) kernel32.GetFullPathNameA
@ stdcall -thunk GetFullPathNameW(wstr long ptr ptr) kernel32.GetFullPathNameW
@ stdcall GetGeoInfoW(long long ptr long long)
@ stdcall GetGeoInfoEx(ptr long ptr long)
@ stdcall -thunk GetHandleInformation(long ptr) kernel32.GetHandleInformation
@ stdcall GetIntegratedDisplaySize(ptr)
@ stdcall GetKernelObjectSecurity(long long ptr long ptr)
@ stdcall -thunk GetLargePageMinimum() kernel32.GetLargePageMinimum
@ stdcall GetLargestConsoleWindowSize(long)
@ stdcall GetLastError() ntdll.RtlGetLastWin32Error
@ stdcall GetLengthSid(ptr)
@ stdcall -thunk GetLocalTime(ptr) kernel32.GetLocalTime
@ stdcall GetLocaleInfoA(long long ptr long)
@ stdcall -version=0x600+ GetLocaleInfoEx(wstr long ptr long)
@ stub GetLocaleInfoHelper
@ stdcall GetLocaleInfoW(long long ptr long)
@ stdcall -thunk GetLogicalDriveStringsW(long ptr) kernel32.GetLogicalDriveStringsW
@ stdcall -thunk GetLogicalDrives() kernel32.GetLogicalDrives
@ stdcall -thunk GetLogicalProcessorInformation(ptr ptr) kernel32.GetLogicalProcessorInformation
@ stdcall -thunk -version=0x601+ GetLogicalProcessorInformationEx(long ptr ptr) kernel32.GetLogicalProcessorInformationEx
@ stdcall -thunk GetLongPathNameA(str long long) kernel32.GetLongPathNameA
@ stdcall -thunk GetLongPathNameW(wstr long long) kernel32.GetLongPathNameW
@ stdcall -thunk -version=0x601+ GetMappedFileNameA(long ptr ptr long) kernel32.K32GetMappedFileNameA
@ stdcall -thunk -version=0x601+ GetMappedFileNameW(long ptr ptr long) kernel32.K32GetMappedFileNameW
@ stdcall -thunk -version=0x601+ GetModuleBaseNameA(long long ptr long) kernel32.K32GetModuleBaseNameA
@ stdcall -thunk -version=0x601+ GetModuleBaseNameW(long long ptr long) kernel32.K32GetModuleBaseNameW
@ stdcall -thunk GetModuleFileNameA(long ptr long) kernel32.GetModuleFileNameA
@ stdcall -thunk -version=0x601+ GetModuleFileNameExA(long long ptr long) kernel32.K32GetModuleFileNameExA
@ stdcall -thunk -version=0x601+ GetModuleFileNameExW(long long ptr long) kernel32.K32GetModuleFileNameExW
@ stdcall -thunk GetModuleFileNameW(long ptr long) kernel32.GetModuleFileNameW
@ stdcall -thunk GetModuleHandleA(str) kernel32.GetModuleHandleA
@ stdcall -thunk GetModuleHandleExA(long ptr ptr) kernel32.GetModuleHandleExA
@ stdcall -thunk GetModuleHandleExW(long ptr ptr) kernel32.GetModuleHandleExW
@ stdcall -thunk GetModuleHandleW(wstr) kernel32.GetModuleHandleW
@ stdcall -thunk -version=0x601+ GetModuleInformation(long long ptr long) kernel32.K32GetModuleInformation
@ stdcall GetNLSVersion(long long ptr)
@ stdcall GetNLSVersionEx(long wstr ptr)
@ stub GetNamedLocaleHashNode
@ stub -version=0x600+ GetNamedPipeAttribute() kernel32.GetNamedPipeAttribute
@ stdcall -thunk -version=0x600+ GetNamedPipeClientComputerNameW(ptr ptr long) kernel32.GetNamedPipeClientComputerNameW
@ stdcall -thunk GetNamedPipeHandleStateW(long ptr ptr ptr ptr wstr long) kernel32.GetNamedPipeHandleStateW
@ stdcall -thunk GetNamedPipeInfo(long ptr ptr ptr ptr) kernel32.GetNamedPipeInfo
@ stdcall -thunk GetNativeSystemInfo(ptr) kernel32.GetNativeSystemInfo
@ stdcall -thunk GetNumaHighestNodeNumber(ptr) kernel32.GetNumaHighestNodeNumber
@ stdcall -thunk -version=0xA00+ GetNumaNodeProcessorMask2(long ptr long ptr) kernel32.GetNumaNodeProcessorMask2
@ stdcall -thunk -version=0x601+ GetNumaNodeProcessorMaskEx(long ptr) kernel32.GetNumaNodeProcessorMaskEx
@ stdcall -thunk GetNumaProximityNodeEx(long ptr) kernel32.GetNumaProximityNodeEx
@ stdcall -thunk -version=0x600+ GetNumberFormatEx(wstr long wstr ptr wstr long) kernel32.GetNumberFormatEx
@ stdcall -thunk GetNumberFormatW(long long wstr ptr ptr long) kernel32.GetNumberFormatW
@ stdcall GetNumberOfConsoleInputEvents(long ptr)
@ stdcall GetNumberOfConsoleMouseButtons(ptr)
@ stdcall -thunk GetOEMCP() kernel32.GetOEMCP
@ stdcall -thunk GetOsSafeBootMode(ptr) kernel32.GetOsSafeBootMode
@ stdcall -thunk GetOverlappedResult(long ptr ptr long) kernel32.GetOverlappedResult
@ stdcall -thunk -version=0x602+ GetOverlappedResultEx(long ptr ptr long long) kernel32.GetOverlappedResultEx
@ stdcall GetPackageFamilyName(long ptr ptr)
@ stdcall GetPackageFamilyNameFromToken(ptr ptr ptr)
@ stdcall GetPackageFullName(long ptr ptr)
@ stdcall GetPackageFullNameFromToken(ptr ptr ptr)
@ stdcall GetPackageId(ptr ptr ptr)
@ stdcall -thunk -version=0x602+ GetPackagePathByFullName(wstr ptr wstr) kernel32.GetPackagePathByFullName
@ stdcall -thunk -version=0x602+ GetPackagesByPackageFamily(wstr ptr ptr ptr ptr) kernel32.GetPackagesByPackageFamily
@ stdcall -thunk -version=0x601+ GetPerformanceInfo(ptr long) kernel32.K32GetPerformanceInfo
@ stdcall -thunk -version=0x600+ GetPhysicallyInstalledSystemMemory(ptr) kernel32.GetPhysicallyInstalledSystemMemory
@ stdcall -thunk GetPriorityClass(long) kernel32.GetPriorityClass
@ stdcall GetPrivateObjectSecurity(ptr long ptr long ptr)
@ stdcall -thunk GetProcAddress(long str) kernel32.GetProcAddress
@ stdcall GetProcessDefaultCpuSets(ptr ptr long ptr)
@ stdcall -thunk -version=0x601+ GetProcessGroupAffinity(long ptr ptr) kernel32.GetProcessGroupAffinity
@ stdcall -thunk GetProcessHandleCount(long ptr) kernel32.GetProcessHandleCount
@ stdcall -thunk GetProcessHeap() kernel32.GetProcessHeap
@ stdcall -thunk GetProcessHeaps(long ptr) kernel32.GetProcessHeaps
@ stdcall -thunk GetProcessId(long) kernel32.GetProcessId
@ stdcall -thunk GetProcessIdOfThread(ptr) kernel32.GetProcessIdOfThread
@ stdcall -thunk -version=0x601+ GetProcessImageFileNameA(long ptr long) kernel32.K32GetProcessImageFileNameA
@ stdcall -thunk -version=0x601+ GetProcessImageFileNameW(long ptr long) kernel32.K32GetProcessImageFileNameW
@ stdcall -thunk GetProcessInformation(long long ptr long) kernel32.GetProcessInformation
@ stdcall -thunk -version=0x601+ GetProcessMemoryInfo(long ptr long) kernel32.K32GetProcessMemoryInfo
@ stdcall -thunk GetProcessMitigationPolicy(long long ptr long) kernel32.GetProcessMitigationPolicy
@ stdcall GetProcessPreferredUILanguages(long ptr ptr ptr)
@ stdcall -thunk GetProcessPriorityBoost(long ptr) kernel32.GetProcessPriorityBoost
@ stdcall -thunk GetProcessShutdownParameters(ptr ptr) kernel32.GetProcessShutdownParameters
@ stdcall -thunk GetProcessTimes(long ptr ptr ptr ptr) kernel32.GetProcessTimes
@ stdcall -thunk GetProcessVersion(long) kernel32.GetProcessVersion
@ stdcall -thunk GetProcessWorkingSetSize(long ptr ptr) kernel32.GetProcessWorkingSetSize
@ stdcall -thunk GetProcessWorkingSetSizeEx(long ptr ptr long) kernel32.GetProcessWorkingSetSizeEx
@ stdcall -version=0x600+ GetProductInfo(long long long long ptr)
@ stub GetPtrCalData
@ stub GetPtrCalDataArray
@ stdcall -thunk GetQueuedCompletionStatus(long ptr ptr ptr long) kernel32.GetQueuedCompletionStatus
@ stdcall -thunk -version=0x600+ GetQueuedCompletionStatusEx(ptr ptr long ptr long long) kernel32.GetQueuedCompletionStatusEx
@ stdcall GetSecurityDescriptorControl(ptr ptr ptr)
@ stdcall GetSecurityDescriptorDacl(ptr ptr ptr ptr)
@ stdcall GetSecurityDescriptorGroup(ptr ptr ptr)
@ stdcall GetSecurityDescriptorLength(ptr)
@ stdcall GetSecurityDescriptorOwner(ptr ptr ptr)
@ stdcall GetSecurityDescriptorRMControl(ptr ptr) advapi32.GetSecurityDescriptorRMControl
@ stdcall GetSecurityDescriptorSacl(ptr ptr ptr ptr)
@ stdcall -thunk GetShortPathNameW(wstr ptr long) kernel32.GetShortPathNameW
@ stdcall GetSidIdentifierAuthority(ptr)
@ stdcall GetSidLengthRequired(long)
@ stdcall GetSidSubAuthority(ptr long)
@ stdcall GetSidSubAuthorityCount(ptr)
@ stdcall -thunk GetStartupInfoW(ptr) kernel32.GetStartupInfoW
@ stdcall GetStdHandle(long)
@ stdcall GetStringScripts(long wstr long ptr long)
@ stub GetStringTableEntry
@ stdcall GetStringTypeA(long long str long ptr)
@ stdcall GetStringTypeExW(long long wstr long ptr)
@ stdcall GetStringTypeW(long wstr long ptr)
@ stdcall GetSystemCpuSetInformation(ptr long ptr ptr long)
@ stdcall -thunk GetSystemDefaultLCID() kernel32.GetSystemDefaultLCID
@ stdcall -thunk GetSystemDefaultLangID() kernel32.GetSystemDefaultLangID
@ stdcall -version=0x600+ GetSystemDefaultLocaleName(ptr long)
@ stdcall -thunk GetSystemDefaultUILanguage() kernel32.GetSystemDefaultUILanguage
@ stdcall -thunk GetSystemDirectoryA(ptr long) kernel32.GetSystemDirectoryA
@ stdcall -thunk GetSystemDirectoryW(ptr long) kernel32.GetSystemDirectoryW
@ stdcall -thunk GetSystemFileCacheSize(ptr ptr ptr) kernel32.GetSystemFileCacheSize
@ stdcall -thunk GetSystemFirmwareTable(long long ptr long) kernel32.GetSystemFirmwareTable
@ stdcall -thunk GetSystemInfo(ptr) kernel32.GetSystemInfo
@ stdcall GetSystemPreferredUILanguages(long ptr ptr ptr)
@ stdcall -thunk GetProcessorSystemCycleTime(long ptr ptr) kernel32.GetProcessorSystemCycleTime
@ stdcall -thunk GetSystemTime(ptr) kernel32.GetSystemTime
@ stdcall -thunk GetSystemTimeAdjustment(ptr ptr ptr) kernel32.GetSystemTimeAdjustment
@ stdcall -thunk GetSystemTimeAsFileTime(ptr) kernel32.GetSystemTimeAsFileTime
@ stdcall -thunk -version=0x602+ GetSystemTimePreciseAsFileTime(ptr) kernel32.GetSystemTimePreciseAsFileTime
@ stdcall -thunk GetSystemTimes(ptr ptr ptr) kernel32.GetSystemTimes
@ stdcall -thunk GetSystemWindowsDirectoryA(ptr long) kernel32.GetSystemWindowsDirectoryA
@ stdcall -thunk GetSystemWindowsDirectoryW(ptr long) kernel32.GetSystemWindowsDirectoryW
@ stdcall GetSystemWow64Directory2A(ptr long long)
@ stdcall GetSystemWow64Directory2W(ptr long long)
@ stdcall -thunk GetSystemWow64DirectoryA(ptr long) kernel32.GetSystemWow64DirectoryA
@ stdcall -thunk GetSystemWow64DirectoryW(ptr long) kernel32.GetSystemWow64DirectoryW
@ stdcall -thunk GetTempFileNameA(str str long ptr) kernel32.GetTempFileNameA
@ stdcall -thunk GetTempFileNameW(wstr wstr long ptr) kernel32.GetTempFileNameW
@ stdcall -thunk GetTempPath2A(long ptr) kernel32.GetTempPath2A
@ stdcall -thunk GetTempPath2W(long ptr) kernel32.GetTempPath2W
@ stdcall -thunk GetTempPathA(long ptr) kernel32.GetTempPathA
@ stdcall -thunk GetTempPathW(long ptr) kernel32.GetTempPathW
@ stdcall -thunk GetThreadContext(long ptr) kernel32.GetThreadContext
@ stdcall GetThreadDescription(ptr ptr)
@ stdcall GetThreadErrorMode()
@ stdcall -thunk -version=0x601+ GetThreadGroupAffinity(long ptr) kernel32.GetThreadGroupAffinity
@ stdcall -thunk GetThreadIOPendingFlag(long ptr) kernel32.GetThreadIOPendingFlag
@ stdcall -thunk GetThreadId(ptr) kernel32.GetThreadId
@ stdcall -thunk GetThreadIdealProcessorEx(long ptr) kernel32.GetThreadIdealProcessorEx
@ stdcall GetThreadInformation(long long ptr long)
@ stdcall -thunk GetThreadLocale() kernel32.GetThreadLocale
@ stdcall GetThreadPreferredUILanguages(long ptr ptr ptr)
@ stdcall -thunk GetThreadPriority(long) kernel32.GetThreadPriority
@ stdcall -thunk GetThreadPriorityBoost(long ptr) kernel32.GetThreadPriorityBoost
@ stdcall GetThreadSelectedCpuSets(ptr ptr long ptr)
@ stdcall -thunk GetThreadTimes(long ptr ptr ptr ptr) kernel32.GetThreadTimes
@ stdcall -thunk -version=0x600+ GetThreadUILanguage() kernel32.GetThreadUILanguage
@ stdcall -thunk GetTickCount() kernel32.GetTickCount
@ stdcall -thunk -version=0x600+ GetTickCount64() kernel32.GetTickCount64
@ stdcall -thunk GetTimeFormatA(long long ptr str ptr long) kernel32.GetTimeFormatA
@ stdcall -thunk -version=0x600+ GetTimeFormatEx(wstr long ptr wstr wstr long) kernel32.GetTimeFormatEx
@ stdcall -thunk GetTimeFormatW(long long ptr wstr ptr long) kernel32.GetTimeFormatW
@ stdcall -thunk GetTimeZoneInformation(ptr) kernel32.GetTimeZoneInformation
@ stdcall -thunk -version=0x600+ GetTimeZoneInformationForYear(long ptr ptr) kernel32.GetTimeZoneInformationForYear
@ stdcall GetTokenInformation(long long ptr long ptr)
@ stdcall GetTraceEnableFlags(int64) ntdll.EtwGetTraceEnableFlags
@ stdcall GetTraceEnableLevel(int64) ntdll.EtwGetTraceEnableLevel
@ stdcall -ret64 GetTraceLoggerHandle(ptr) ntdll.EtwGetTraceLoggerHandle
@ stdcall -thunk -version=0x600+ GetUILanguageInfo(long wstr wstr ptr ptr) kernel32.GetUILanguageInfo
@ stdcall GetUserDefaultGeoName(ptr long)
@ stdcall -thunk GetUserDefaultLCID() kernel32.GetUserDefaultLCID
@ stdcall -thunk GetUserDefaultLangID() kernel32.GetUserDefaultLangID
@ stdcall -version=0x600+ GetUserDefaultLocaleName(wstr long)
@ stdcall -thunk GetUserDefaultUILanguage() kernel32.GetUserDefaultUILanguage
@ stdcall GetUserGeoID(long)
@ stub GetUserInfo
@ stub GetUserInfoWord
@ stdcall GetUserPreferredUILanguages(long ptr ptr ptr)
@ stdcall GetVersion()
@ stdcall GetVersionExA(ptr)
@ stdcall GetVersionExW(ptr)
@ stdcall -thunk GetVolumeInformationA(str ptr long ptr ptr ptr ptr long) kernel32.GetVolumeInformationA
@ stdcall GetVolumeInformationByHandleW(ptr ptr long ptr ptr ptr ptr long)
@ stdcall -thunk GetVolumeInformationW(wstr ptr long ptr ptr ptr ptr long) kernel32.GetVolumeInformationW
@ stdcall -thunk GetVolumeNameForVolumeMountPointW(wstr ptr long) kernel32.GetVolumeNameForVolumeMountPointW
@ stdcall -thunk GetVolumePathNameW(wstr ptr long) kernel32.GetVolumePathNameW
@ stdcall -thunk GetVolumePathNamesForVolumeNameW(wstr wstr long ptr) kernel32.GetVolumePathNamesForVolumeNameW
@ stdcall GetWindowsAccountDomainSid(ptr ptr ptr)
@ stdcall -thunk GetWindowsDirectoryA(ptr long) kernel32.GetWindowsDirectoryA
@ stdcall -thunk GetWindowsDirectoryW(ptr long) kernel32.GetWindowsDirectoryW
@ stdcall -thunk GetWriteWatch(long ptr long ptr ptr ptr) kernel32.GetWriteWatch
@ stdcall -thunk -version=0x601+ GetWsChanges(long ptr long) kernel32.K32GetWsChanges
@ stdcall -thunk -version=0x601+ GetWsChangesEx(long ptr ptr) kernel32.K32GetWsChangesEx
@ stdcall -arch=i386,x86_64,arm64,arm64ec GetXStateFeaturesMask(ptr ptr)
@ stdcall -thunk GlobalAlloc(long long) kernel32.GlobalAlloc
@ stdcall -thunk GlobalFree(long) kernel32.GlobalFree
@ stdcall -thunk GlobalMemoryStatusEx(ptr) kernel32.GlobalMemoryStatusEx
@ stdcall HashData(ptr long ptr long)
@ stdcall HeapAlloc(long long long) ntdll.RtlAllocateHeap
@ stdcall -thunk HeapCompact(long long) kernel32.HeapCompact
@ stdcall -thunk HeapCreate(long long long) kernel32.HeapCreate
@ stdcall -thunk HeapDestroy(long) kernel32.HeapDestroy
@ stdcall HeapFree(long long long) ntdll.RtlFreeHeap
@ stdcall -thunk HeapLock(long) kernel32.HeapLock
@ stdcall -thunk HeapQueryInformation(long long ptr long ptr) kernel32.HeapQueryInformation
@ stdcall HeapReAlloc(long long ptr long) ntdll.RtlReAllocateHeap
@ stdcall -thunk HeapSetInformation(ptr long ptr long) kernel32.HeapSetInformation
@ stdcall HeapSize(long long ptr) ntdll.RtlSizeHeap
@ stdcall -thunk HeapSummary(long long ptr) kernel32.HeapSummary
@ stdcall -thunk HeapUnlock(long) kernel32.HeapUnlock
@ stdcall -thunk HeapValidate(long long ptr) kernel32.HeapValidate
@ stdcall -thunk HeapWalk(long ptr) kernel32.HeapWalk
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
@ stdcall InitializeContext(ptr long ptr ptr)
@ stdcall InitializeContext2(ptr long ptr ptr int64)
# TODO: Windows 11 kernelbase forwards this to NTDLL
@ stdcall InitializeCriticalSection(ptr) kernel32.InitializeCriticalSection
@ stdcall -thunk InitializeCriticalSectionAndSpinCount(ptr long) kernel32.InitializeCriticalSectionAndSpinCount
@ stdcall -thunk -version=0x600+ InitializeCriticalSectionEx(ptr long long) kernel32.InitializeCriticalSectionEx
@ stdcall -thunk -version=0x600+ InitializeProcThreadAttributeList(ptr long long ptr) kernel32.InitializeProcThreadAttributeList
@ stdcall -thunk -version=0x601+ InitializeProcessForWsWatch(long) kernel32.K32InitializeProcessForWsWatch
@ stdcall InitializeSListHead(ptr) ntdll.RtlInitializeSListHead
@ stdcall -version=0x600+ InitializeSRWLock(ptr) ntdll.RtlInitializeSRWLock
@ stdcall -thunk InitializeSynchronizationBarrier(ptr long long) kernel32.InitializeSynchronizationBarrier
@ stdcall InitializeSecurityDescriptor(ptr long)
@ stdcall InitializeSid(ptr ptr long)
# TODO: Windows 11 kernelbase does not export this
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
@ stdcall -thunk IsDBCSLeadByte(long) kernel32.IsDBCSLeadByte
@ stdcall -thunk IsDBCSLeadByteEx(long long) kernel32.IsDBCSLeadByteEx
@ stdcall -thunk IsDebuggerPresent() kernel32.IsDebuggerPresent
@ stdcall IsInternetESCEnabled()
@ stdcall IsNLSDefinedString(long long ptr wstr long)
@ stdcall IsNormalizedString(long wstr long)
@ stdcall -thunk IsProcessInJob(long long ptr) kernel32.IsProcessInJob
@ stdcall -thunk IsProcessorFeaturePresent(long) kernel32.IsProcessorFeaturePresent
@ stdcall -thunk -version=0x600+ IsThreadAFiber() kernel32.IsThreadAFiber
@ stdcall IsThreadpoolTimerSet(ptr) ntdll.TpIsTimerSet
@ stdcall IsTokenRestricted(long)
@ stdcall IsValidAcl(ptr)
@ stdcall -thunk IsValidCodePage(long) kernel32.IsValidCodePage
@ stdcall -thunk IsValidLanguageGroup(long long) kernel32.IsValidLanguageGroup
@ stdcall -thunk IsValidLocale(long long) kernel32.IsValidLocale
@ stdcall IsValidLocaleName(wstr)
@ stdcall IsValidNLSVersion(long wstr ptr)
@ stub IsValidRelativeSecurityDescriptor
@ stdcall IsValidSecurityDescriptor(ptr)
@ stdcall IsValidSid(ptr)
@ stdcall IsWellKnownSid(ptr long)
@ stdcall -thunk IsWow64GuestMachineSupported(long ptr) kernel32.IsWow64GuestMachineSupported
@ stdcall -thunk IsWow64Process(ptr ptr) kernel32.IsWow64Process
@ stdcall -thunk -version=0xA00+ IsWow64Process2(ptr ptr ptr) kernel32.IsWow64Process2
@ stdcall -thunk -version=0x601+ K32EmptyWorkingSet(long) kernel32.K32EmptyWorkingSet
@ stdcall -thunk -version=0x601+ K32EnumDeviceDrivers(ptr long ptr) kernel32.K32EnumDeviceDrivers
@ stdcall -thunk -version=0x601+ K32EnumPageFilesA(ptr ptr) kernel32.K32EnumPageFilesA
@ stdcall -thunk -version=0x601+ K32EnumPageFilesW(ptr ptr) kernel32.K32EnumPageFilesW
@ stdcall -thunk -version=0x601+ K32EnumProcessModules(long ptr long ptr) kernel32.K32EnumProcessModules
@ stdcall -thunk -version=0x601+ K32EnumProcessModulesEx(long ptr long ptr long) kernel32.K32EnumProcessModulesEx
@ stdcall -thunk -version=0x601+ K32EnumProcesses(ptr long ptr) kernel32.K32EnumProcesses
@ stdcall -thunk -version=0x601+ K32GetDeviceDriverBaseNameA(ptr ptr long) kernel32.K32GetDeviceDriverBaseNameA
@ stdcall -thunk -version=0x601+ K32GetDeviceDriverBaseNameW(ptr ptr long) kernel32.K32GetDeviceDriverBaseNameW
@ stdcall -thunk -version=0x601+ K32GetDeviceDriverFileNameA(ptr ptr long) kernel32.K32GetDeviceDriverFileNameA
@ stdcall -thunk -version=0x601+ K32GetDeviceDriverFileNameW(ptr ptr long) kernel32.K32GetDeviceDriverFileNameW
@ stdcall -thunk -version=0x601+ K32GetMappedFileNameA(long ptr ptr long) kernel32.K32GetMappedFileNameA
@ stdcall -thunk -version=0x601+ K32GetMappedFileNameW(long ptr ptr long) kernel32.K32GetMappedFileNameW
@ stdcall -thunk -version=0x601+ K32GetModuleBaseNameA(long long ptr long) kernel32.K32GetModuleBaseNameA
@ stdcall -thunk -version=0x601+ K32GetModuleBaseNameW(long long ptr long) kernel32.K32GetModuleBaseNameW
@ stdcall -thunk -version=0x601+ K32GetModuleFileNameExA(long long ptr long) kernel32.K32GetModuleFileNameExA
@ stdcall -thunk -version=0x601+ K32GetModuleFileNameExW(long long ptr long) kernel32.K32GetModuleFileNameExW
@ stdcall -thunk -version=0x601+ K32GetModuleInformation(long long ptr long) kernel32.K32GetModuleInformation
@ stdcall -thunk -version=0x601+ K32GetPerformanceInfo(ptr long) kernel32.K32GetPerformanceInfo
@ stdcall -thunk -version=0x601+ K32GetProcessImageFileNameA(long ptr long) kernel32.K32GetProcessImageFileNameA
@ stdcall -thunk -version=0x601+ K32GetProcessImageFileNameW(long ptr long) kernel32.K32GetProcessImageFileNameW
@ stdcall -thunk -version=0x601+ K32GetProcessMemoryInfo(long ptr long) kernel32.K32GetProcessMemoryInfo
@ stdcall -thunk -version=0x601+ K32GetWsChanges(long ptr long) kernel32.K32GetWsChanges
@ stdcall -thunk -version=0x601+ K32GetWsChangesEx(long ptr ptr) kernel32.K32GetWsChangesEx
@ stdcall -thunk -version=0x601+ K32InitializeProcessForWsWatch(long) kernel32.K32InitializeProcessForWsWatch
@ stdcall -thunk -version=0x601+ K32QueryWorkingSet(long ptr long) kernel32.K32QueryWorkingSet
@ stdcall -thunk -version=0x601+ K32QueryWorkingSetEx(long ptr long) kernel32.K32QueryWorkingSetEx
@ stub KernelBaseGetGlobalData
@ stdcall LCIDToLocaleName(long ptr long long)
@ stdcall LCMapStringA(long long str long ptr long)
@ stdcall LCMapStringEx(wstr long wstr long ptr long ptr ptr long)
@ stdcall LCMapStringW(long long wstr long ptr long)
@ stdcall LeaveCriticalSection(ptr) ntdll.RtlLeaveCriticalSection
@ stdcall -version=0x600+ LeaveCriticalSectionWhenCallbackReturns(ptr ptr) ntdll.TpCallbackLeaveCriticalSectionOnCompletion
@ stdcall LoadAppInitDlls()
@ stdcall -thunk LoadLibraryA(str) kernel32.LoadLibraryA
@ stdcall -thunk LoadLibraryExA(str long long) kernel32.LoadLibraryExA
@ stdcall -thunk LoadLibraryExW(wstr long long) kernel32.LoadLibraryExW
@ stdcall -thunk LoadLibraryW(wstr) kernel32.LoadLibraryW
@ stdcall LoadPackagedLibrary(wstr long)
@ stdcall -thunk LoadResource(long long) kernel32.LoadResource
@ stdcall LoadStringA(long long ptr long)
@ stub -version=0x600+ LoadStringBaseExW() kernel32.LoadStringBaseExW
@ stub LoadStringByReference
@ stdcall LoadStringW(long long ptr long)
@ stdcall -thunk LocalAlloc(long long) kernel32.LocalAlloc
@ stdcall -thunk LocalFileTimeToFileTime(ptr ptr) kernel32.LocalFileTimeToFileTime
@ stdcall -thunk LocalFree(long) kernel32.LocalFree
@ stdcall -thunk LocalLock(long) kernel32.LocalLock
@ stdcall -thunk LocalReAlloc(long long long) kernel32.LocalReAlloc
@ stdcall -thunk LocalUnlock(long) kernel32.LocalUnlock
@ stdcall LocaleNameToLCID(wstr long)
@ stdcall -arch=i386,x86_64,arm64,arm64ec LocateXStateFeature(ptr long ptr)
@ stdcall -thunk LockFile(long long long long long) kernel32.LockFile
@ stdcall -thunk LockFileEx(long long long long long ptr) kernel32.LockFileEx
@ stdcall -thunk LockResource(long) kernel32.LockResource
@ stdcall MakeAbsoluteSD(ptr ptr ptr ptr ptr ptr ptr ptr ptr ptr ptr)
@ stdcall MakeAbsoluteSD2(ptr ptr) advapi32.MakeAbsoluteSD2
@ stdcall MakeSelfRelativeSD(ptr ptr ptr)
@ stdcall MapGenericMask(ptr ptr)
@ stdcall -thunk MapUserPhysicalPages(ptr long ptr) kernel32.MapUserPhysicalPages
@ stdcall -thunk MapViewOfFile(long long long long long) kernel32.MapViewOfFile
@ stdcall -arch=win64 MapViewOfFile3(long long ptr int64 long long long ptr long)
@ stdcall -thunk MapViewOfFileEx(long long long long long ptr) kernel32.MapViewOfFileEx
@ stdcall -thunk -version=0x600+ MapViewOfFileExNuma(long long long long long ptr long) kernel32.MapViewOfFileExNuma
@ stdcall MapViewOfFileFromApp(long long int64 long)
@ stdcall -thunk MoveFileExW(wstr wstr long) kernel32.MoveFileExW
@ stdcall -thunk MoveFileWithProgressW(wstr wstr ptr ptr long) kernel32.MoveFileWithProgressW
@ stdcall -thunk MulDiv(long long long) kernel32.MulDiv
@ stdcall MultiByteToWideChar(long long str long ptr long)
@ stdcall -thunk NeedCurrentDirectoryForExePathA(str) kernel32.NeedCurrentDirectoryForExePathA
@ stdcall -thunk NeedCurrentDirectoryForExePathW(wstr) kernel32.NeedCurrentDirectoryForExePathW
@ stub -version=0x600+ NlsCheckPolicy() kernel32.NlsCheckPolicy
@ stub NlsDispatchAnsiEnumProc
@ stub NlsGetACPFromLocale
@ stdcall -thunk NlsGetCacheUpdateCount() kernel32.NlsGetCacheUpdateCount
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
@ stdcall -thunk OpenEventA(long long str) kernel32.OpenEventA
@ stdcall -thunk OpenEventW(long long wstr) kernel32.OpenEventW
@ stdcall -thunk -version=0x600+ OpenFileById(ptr ptr long long ptr long) kernel32.OpenFileById
@ stdcall OpenFileMappingFromApp(long long wstr)
@ stdcall -thunk OpenFileMappingW(long long wstr) kernel32.OpenFileMappingW
@ stdcall -thunk OpenMutexW(long long wstr) kernel32.OpenMutexW
@ stdcall -thunk OpenPrivateNamespaceW(ptr wstr) kernel32.OpenPrivateNamespaceW
@ stdcall -thunk OpenProcess(long long long) kernel32.OpenProcess
@ stdcall OpenProcessToken(long long ptr)
@ stub OpenRegKey
@ stdcall -thunk OpenSemaphoreW(long long wstr) kernel32.OpenSemaphoreW
@ stdcall -thunk OpenThread(long long long) kernel32.OpenThread
@ stdcall OpenThreadToken(long long long ptr)
@ stdcall -thunk OpenWaitableTimerW(long long wstr) kernel32.OpenWaitableTimerW
@ stdcall -thunk OutputDebugStringA(str) kernel32.OutputDebugStringA
@ stdcall -thunk OutputDebugStringW(wstr) kernel32.OutputDebugStringW
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
@ stdcall -thunk PeekNamedPipe(long ptr long ptr ptr ptr) kernel32.PeekNamedPipe
@ stdcall PerfCreateInstance(long ptr wstr long)
@ stdcall PerfDecrementULongCounterValue(long ptr long long)
@ stdcall PerfDecrementULongLongCounterValue(long ptr long int64)
@ stdcall PerfDeleteInstance(long ptr)
@ stdcall PerfIncrementULongCounterValue(long ptr long long)
@ stdcall PerfIncrementULongLongCounterValue(long ptr long int64)
@ stdcall PerfQueryInstance(long ptr wstr long)
@ stdcall PerfSetCounterRefValue(long ptr long ptr)
@ stdcall PerfSetCounterSetInfo(long ptr long)
@ stdcall PerfSetULongCounterValue(long ptr long long)
@ stdcall PerfSetULongLongCounterValue(long ptr long int64)
@ stdcall PerfStartProvider(ptr ptr ptr)
@ stdcall PerfStartProviderEx(ptr ptr ptr)
@ stdcall PerfStopProvider(long)
@ stdcall -thunk PostQueuedCompletionStatus(long long ptr ptr) kernel32.PostQueuedCompletionStatus
@ stdcall -thunk PrivCopyFileExW(wstr wstr ptr ptr long long) kernel32.PrivCopyFileExW
@ stdcall PrivilegeCheck(ptr ptr ptr)
@ stdcall PrivilegedServiceAuditAlarmW(wstr wstr long ptr long)
@ stdcall -thunk ProcessIdToSessionId(long ptr) kernel32.ProcessIdToSessionId
@ stdcall PssCaptureSnapshot(ptr long long ptr)
@ stdcall PssDuplicateSnapshot(ptr ptr ptr ptr long)
@ stdcall PssFreeSnapshot(ptr ptr)
@ stdcall PssQuerySnapshot(ptr long ptr long)
@ stdcall PssWalkMarkerCreate(ptr ptr)
@ stdcall PssWalkMarkerFree(ptr)
@ stdcall PssWalkMarkerGetPosition(ptr ptr)
@ stdcall PssWalkMarkerSeekToBeginning(ptr)
@ stdcall PssWalkMarkerSetPosition(ptr long)
@ stdcall PssWalkSnapshot(ptr long ptr ptr long)
@ stdcall -thunk PulseEvent(long) kernel32.PulseEvent
@ stdcall -thunk PurgeComm(long long) kernel32.PurgeComm
@ stdcall QISearch(ptr ptr ptr ptr)
@ stdcall -thunk -version=0x600+ QueryActCtxSettingsW(long ptr wstr wstr ptr long ptr) kernel32.QueryActCtxSettingsW
@ stdcall -thunk QueryActCtxW(long ptr ptr long ptr long ptr) kernel32.QueryActCtxW
@ stdcall QueryDepthSList(ptr) ntdll.RtlQueryDepthSList
@ stdcall -thunk QueryDosDeviceW(wstr ptr long) kernel32.QueryDosDeviceW
@ stdcall -thunk -version=0x600+ QueryFullProcessImageNameA(ptr long ptr ptr) kernel32.QueryFullProcessImageNameA
@ stdcall -thunk -version=0x600+ QueryFullProcessImageNameW(ptr long ptr ptr) kernel32.QueryFullProcessImageNameW
@ stdcall -thunk -version=0x600+ QueryIdleProcessorCycleTime(ptr ptr) kernel32.QueryIdleProcessorCycleTime
@ stdcall -thunk -version=0x601+ QueryIdleProcessorCycleTimeEx(long ptr ptr) kernel32.QueryIdleProcessorCycleTimeEx
@ stub QueryIoRingCapabilities
@ stdcall -thunk QueryMemoryResourceNotification(ptr ptr) kernel32.QueryMemoryResourceNotification
# TODO: Windows 11 kernelbase forwards this to NTDLL
@ stdcall QueryPerformanceCounter(ptr) kernel32.QueryPerformanceCounter
@ stdcall QueryPerformanceFrequency(ptr) kernel32.QueryPerformanceFrequency
@ stdcall -thunk QueryProcessAffinityUpdateMode(ptr ptr) kernel32.QueryProcessAffinityUpdateMode
@ stdcall -thunk -version=0x600+ QueryProcessCycleTime(ptr ptr) kernel32.QueryProcessCycleTime
@ stdcall QuerySecurityAccessMask(long ptr)
@ stdcall -thunk -version=0x600+ QueryThreadCycleTime(ptr ptr) kernel32.QueryThreadCycleTime
@ stdcall QueryThreadpoolStackInformation(ptr ptr)
@ stdcall QueryVirtualMemoryInformation(long ptr long ptr long ptr)
@ stdcall -thunk -version=0x601+ QueryWorkingSet(long ptr long) kernel32.K32QueryWorkingSet
@ stdcall -thunk -version=0x601+ QueryWorkingSetEx(long ptr long) kernel32.K32QueryWorkingSetEx
@ stdcall -thunk QueueUserAPC(ptr long long) kernel32.QueueUserAPC
@ stdcall -thunk QueueUserWorkItem(ptr ptr long) kernel32.QueueUserWorkItem
@ stdcall QuirkIsEnabled(ptr)
@ stdcall QuirkIsEnabled3(ptr ptr)
@ stdcall -thunk RaiseException(long long long ptr) kernel32.RaiseException
@ stdcall RaiseFailFastException(ptr ptr long)
@ stdcall -thunk ReOpenFile(ptr long long long) kernel32.ReOpenFile
@ stdcall ReadConsoleA(long ptr long ptr ptr)
@ stdcall ReadConsoleInputA(long ptr long ptr)
@ stdcall ReadConsoleInputW(long ptr long ptr)
@ stdcall ReadConsoleOutputA(long ptr long long ptr)
@ stdcall ReadConsoleOutputAttribute(long ptr long long ptr)
@ stdcall ReadConsoleOutputCharacterA(long ptr long long ptr)
@ stdcall ReadConsoleOutputCharacterW(long ptr long long ptr)
@ stdcall ReadConsoleOutputW(long ptr long long ptr)
@ stdcall ReadConsoleW(long ptr long ptr ptr)
@ stdcall -thunk ReadDirectoryChangesW(long ptr long long long ptr ptr ptr) kernel32.ReadDirectoryChangesW
@ stdcall -thunk ReadFile(long ptr long ptr ptr) kernel32.ReadFile
@ stdcall -thunk ReadFileEx(long ptr long ptr ptr) kernel32.ReadFileEx
@ stdcall -thunk ReadFileScatter(long ptr long ptr ptr) kernel32.ReadFileScatter
@ stdcall -thunk ReadProcessMemory(long ptr ptr long ptr) kernel32.ReadProcessMemory
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
@ stdcall -thunk RegisterWaitForSingleObjectEx(long ptr ptr long long) kernel32.RegisterWaitForSingleObjectEx
@ stdcall -thunk ReleaseActCtx(ptr) kernel32.ReleaseActCtx
@ stdcall -thunk ReleaseMutex(long) kernel32.ReleaseMutex
@ stdcall -version=0x600+ ReleaseMutexWhenCallbackReturns(ptr ptr) ntdll.TpCallbackReleaseMutexOnCompletion
@ stdcall -version=0x600+ ReleaseSRWLockExclusive(ptr) ntdll.RtlReleaseSRWLockExclusive
@ stdcall -version=0x600+ ReleaseSRWLockShared(ptr) ntdll.RtlReleaseSRWLockShared
@ stdcall -thunk ReleaseSemaphore(long long ptr) kernel32.ReleaseSemaphore
@ stdcall -version=0x600+ ReleaseSemaphoreWhenCallbackReturns(ptr ptr long) ntdll.TpCallbackReleaseSemaphoreOnCompletion
@ stdcall RemapPredefinedHandleInternal(long long)
@ stdcall -thunk RemoveDirectoryA(str) kernel32.RemoveDirectoryA
@ stdcall -thunk RemoveDirectoryW(wstr) kernel32.RemoveDirectoryW
@ stdcall -thunk -version=0x601+ RemoveDllDirectory(ptr) kernel32.RemoveDllDirectory
@ stdcall RemoveVectoredContinueHandler(ptr) ntdll.RtlRemoveVectoredContinueHandler
@ stdcall RemoveVectoredExceptionHandler(ptr) ntdll.RtlRemoveVectoredExceptionHandler
@ stdcall -thunk ReplaceFileW(wstr wstr wstr long ptr ptr) kernel32.ReplaceFileW
@ stdcall -thunk ResetEvent(long) kernel32.ResetEvent
@ stdcall -thunk ResetWriteWatch(ptr long) kernel32.ResetWriteWatch
@ stdcall ResizePseudoConsole(ptr long)
@ stdcall -version=0x602+ ResolveDelayLoadedAPI(ptr ptr ptr ptr ptr long) ntdll.LdrResolveDelayLoadedAPI
@ stdcall ResolveLocaleName(wstr ptr long)
@ stdcall RestoreLastError(long) ntdll.RtlRestoreLastWin32Error
@ stdcall -thunk ResumeThread(long) kernel32.ResumeThread
@ stdcall RevertToSelf()
@ stdcall -thunk SHExpandEnvironmentStringsA(str ptr long) kernel32.ExpandEnvironmentStringsA
@ stdcall -thunk SHExpandEnvironmentStringsW(wstr ptr long) kernel32.ExpandEnvironmentStringsW
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
@ stdcall -thunk SearchPathA(str str str long ptr ptr) kernel32.SearchPathA
@ stdcall -thunk SearchPathW(wstr wstr wstr long ptr ptr) kernel32.SearchPathW
@ stdcall SetAclInformation(ptr ptr long long)
@ stdcall SetCachedSigningLevel(ptr long long long)
@ stdcall -thunk SetCalendarInfoW(long long long wstr) kernel32.SetCalendarInfoW
@ stdcall -thunk SetCommBreak(long) kernel32.SetCommBreak
@ stdcall -thunk SetCommConfig(long ptr long) kernel32.SetCommConfig
@ stdcall -thunk SetCommMask(long ptr) kernel32.SetCommMask
@ stdcall -thunk SetCommState(long ptr) kernel32.SetCommState
@ stdcall -thunk SetCommTimeouts(long ptr) kernel32.SetCommTimeouts
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
@ stdcall -thunk SetCurrentDirectoryA(str) kernel32.SetCurrentDirectoryA
@ stdcall -thunk SetCurrentDirectoryW(wstr) kernel32.SetCurrentDirectoryW
@ stdcall -thunk -version=0x602+ SetDefaultDllDirectories(long) kernel32.SetDefaultDllDirectories
@ stdcall -thunk SetEndOfFile(long) kernel32.SetEndOfFile
# TODO: Windows 11 kernelbase does not export this
@ stdcall SetEnvironmentStringsA(ptr) kernel32.SetEnvironmentStringsA
@ stdcall -thunk SetEnvironmentStringsW(ptr) kernel32.SetEnvironmentStringsW
@ stdcall -thunk SetEnvironmentVariableA(str str) kernel32.SetEnvironmentVariableA
@ stdcall -thunk SetEnvironmentVariableW(wstr wstr) kernel32.SetEnvironmentVariableW
@ stdcall -thunk SetErrorMode(long) kernel32.SetErrorMode
@ stdcall -thunk SetEvent(long) kernel32.SetEvent
@ stdcall -version=0x600+ SetEventWhenCallbackReturns(ptr ptr) ntdll.TpCallbackSetEventOnCompletion
@ stdcall -thunk SetFileApisToANSI() kernel32.SetFileApisToANSI
@ stdcall -thunk SetFileApisToOEM() kernel32.SetFileApisToOEM
@ stdcall -thunk SetFileAttributesA(str long) kernel32.SetFileAttributesA
@ stdcall -thunk SetFileAttributesW(wstr long) kernel32.SetFileAttributesW
@ stdcall -thunk -version=0x600+ SetFileInformationByHandle(ptr long ptr long) kernel32.SetFileInformationByHandle
@ stdcall -thunk -version=0x600+ SetFileIoOverlappedRange(ptr ptr long) kernel32.SetFileIoOverlappedRange
@ stdcall -thunk SetFilePointer(long long ptr long) kernel32.SetFilePointer
@ stdcall -thunk SetFilePointerEx(long double ptr long) kernel32.SetFilePointerEx
@ stdcall SetFileSecurityW(wstr long ptr)
@ stdcall -thunk SetFileTime(long ptr ptr ptr) kernel32.SetFileTime
@ stdcall -thunk SetFileValidData(long double) kernel32.SetFileValidData
@ stdcall -thunk SetHandleCount(long) kernel32.SetHandleCount
@ stdcall -thunk SetHandleInformation(long long long) kernel32.SetHandleInformation
@ stdcall SetKernelObjectSecurity(long long ptr)
@ stdcall SetLastError(long) ntdll.RtlSetLastWin32Error
@ stdcall -thunk SetLocalTime(ptr) kernel32.SetLocalTime
@ stdcall SetLocaleInfoW(long long wstr)
@ stdcall -thunk SetNamedPipeHandleState(long ptr ptr ptr) kernel32.SetNamedPipeHandleState
@ stdcall -thunk SetPriorityClass(long long) kernel32.SetPriorityClass
@ stdcall SetPrivateObjectSecurity(long ptr ptr ptr long)
@ stdcall SetPrivateObjectSecurityEx(long ptr ptr long ptr long)
@ stdcall -thunk SetProcessAffinityUpdateMode(ptr long) kernel32.SetProcessAffinityUpdateMode
@ stdcall SetProcessDefaultCpuSets(ptr ptr long)
@ stub SetProcessGroupAffinity
@ stdcall -thunk SetProcessInformation(long long ptr long) kernel32.SetProcessInformation
@ stdcall -thunk SetProcessMitigationPolicy(long ptr long) kernel32.SetProcessMitigationPolicy
@ stdcall SetProcessPreferredUILanguages(long ptr ptr)
@ stdcall -thunk SetProcessPriorityBoost(long long) kernel32.SetProcessPriorityBoost
@ stdcall -thunk SetProcessShutdownParameters(long long) kernel32.SetProcessShutdownParameters
@ stdcall -thunk SetProcessValidCallTargets(ptr ptr ptr long ptr) kernel32.SetProcessValidCallTargets
@ stdcall -arch=x86_64 SetProcessValidCallTargetsForMappedView(ptr ptr ptr long ptr ptr int64)
@ stdcall -thunk SetProcessWorkingSetSize(long long long) kernel32.SetProcessWorkingSetSize
@ stdcall -thunk SetProcessWorkingSetSizeEx(long long long long) kernel32.SetProcessWorkingSetSizeEx
@ stdcall SetSecurityAccessMask(long ptr) advapi32.SetSecurityAccessMask
@ stdcall SetSecurityDescriptorControl(ptr long long)
@ stdcall SetSecurityDescriptorDacl(ptr long ptr long)
@ stdcall SetSecurityDescriptorGroup(ptr ptr long)
@ stdcall SetSecurityDescriptorOwner(ptr ptr long)
@ stdcall SetSecurityDescriptorRMControl(ptr ptr) advapi32.SetSecurityDescriptorRMControl
@ stdcall SetSecurityDescriptorSacl(ptr long ptr long)
@ stdcall SetStdHandle(long long)
@ stdcall -version=0x600+ SetStdHandleEx(long long ptr)
@ stdcall -thunk SetSystemFileCacheSize(long long long) kernel32.SetSystemFileCacheSize
@ stdcall -thunk SetSystemTime(ptr) kernel32.SetSystemTime
@ stdcall -thunk SetSystemTimeAdjustment(long long) kernel32.SetSystemTimeAdjustment
@ stdcall -thunk SetThreadContext(long ptr) kernel32.SetThreadContext
@ stdcall SetThreadDescription(ptr wstr)
@ stdcall SetThreadErrorMode(long ptr)
@ stdcall -thunk -version=0x601+ SetThreadGroupAffinity(long ptr ptr) kernel32.SetThreadGroupAffinity
@ stdcall -thunk SetThreadIdealProcessor(long long) kernel32.SetThreadIdealProcessor
@ stdcall -thunk SetThreadIdealProcessorEx(long ptr ptr) kernel32.SetThreadIdealProcessorEx
@ stdcall SetThreadInformation(long long ptr long)
@ stdcall -thunk SetThreadLocale(long) kernel32.SetThreadLocale
@ stdcall SetThreadPreferredUILanguages(long ptr ptr)
@ stdcall -thunk SetThreadPriority(long long) kernel32.SetThreadPriority
@ stdcall -thunk SetThreadPriorityBoost(long long) kernel32.SetThreadPriorityBoost
@ stdcall SetThreadSelectedCpuSets(ptr ptr long)
@ stdcall -thunk SetThreadStackGuarantee(ptr) kernel32.SetThreadStackGuarantee
@ stdcall SetThreadToken(ptr ptr)
@ stdcall -thunk SetThreadUILanguage(long) kernel32.SetThreadUILanguage
@ stdcall SetThreadpoolStackInformation(ptr ptr)
@ stdcall SetThreadpoolThreadMaximum(ptr long) ntdll.TpSetPoolMaxThreads
@ stdcall SetThreadpoolThreadMinimum(ptr long) ntdll.TpSetPoolMinThreads
@ stdcall SetThreadpoolTimer(ptr ptr long long) ntdll.TpSetTimer
@ stdcall SetThreadpoolWait(ptr long ptr) ntdll.TpSetWait
@ stdcall -thunk SetTimeZoneInformation(ptr) kernel32.SetTimeZoneInformation
@ stdcall SetTokenInformation(long long ptr long)
@ stdcall -thunk SetUnhandledExceptionFilter(ptr) kernel32.SetUnhandledExceptionFilter
@ stdcall SetUserGeoID(long)
@ stdcall SetUserGeoName(wstr)
@ stdcall -thunk SetWaitableTimer(long ptr long ptr ptr long) kernel32.SetWaitableTimer
@ stdcall -thunk SetWaitableTimerEx(long ptr long ptr ptr ptr long) kernel32.SetWaitableTimerEx
@ stdcall -arch=i386,x86_64,arm64,arm64ec SetXStateFeaturesMask(ptr int64)
@ stdcall -thunk SetupComm(long long long) kernel32.SetupComm
@ stdcall -thunk SignalObjectAndWait(long long long long) kernel32.SignalObjectAndWait
@ stdcall -thunk SizeofResource(long long) kernel32.SizeofResource
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
@ stdcall -thunk SuspendThread(long) kernel32.SuspendThread
@ stdcall -thunk SwitchToFiber(ptr) kernel32.SwitchToFiber
@ stdcall -thunk SwitchToThread() kernel32.SwitchToThread
@ stdcall -thunk SystemTimeToFileTime(ptr ptr) kernel32.SystemTimeToFileTime
@ stdcall -thunk SystemTimeToTzSpecificLocalTime(ptr ptr ptr) kernel32.SystemTimeToTzSpecificLocalTime
@ stdcall -thunk SystemTimeToTzSpecificLocalTimeEx(ptr ptr ptr) kernel32.SystemTimeToTzSpecificLocalTimeEx
@ stdcall -thunk TerminateProcess(ptr long) kernel32.TerminateProcess
@ stdcall -thunk TerminateThread(ptr long) kernel32.TerminateThread
@ stdcall -thunk TlsAlloc() kernel32.TlsAlloc
@ stdcall -thunk TlsFree(long) kernel32.TlsFree
@ stdcall -thunk -norelay TlsGetValue(long) kernel32.TlsGetValue
@ stdcall -thunk -norelay TlsSetValue(long ptr) kernel32.TlsSetValue
@ stdcall TraceEvent(int64 ptr) ntdll.EtwLogTraceEvent
@ varargs TraceMessage(int64 long ptr long) ntdll.EtwTraceMessage
@ stdcall TraceMessageVa(int64 long ptr long ptr) ntdll.EtwTraceMessageVa
@ stdcall -thunk TransactNamedPipe(long ptr long ptr long ptr ptr) kernel32.TransactNamedPipe
@ stdcall -thunk TransmitCommChar(long long) kernel32.TransmitCommChar
@ stdcall -version=0x601+ TryAcquireSRWLockExclusive(ptr) ntdll.RtlTryAcquireSRWLockExclusive
@ stdcall -version=0x601+ TryAcquireSRWLockShared(ptr) ntdll.RtlTryAcquireSRWLockShared
@ stdcall TryEnterCriticalSection(ptr) ntdll.RtlTryEnterCriticalSection
@ stdcall TrySubmitThreadpoolCallback(ptr ptr ptr)
@ stdcall -thunk TzSpecificLocalTimeToSystemTime(ptr ptr ptr) kernel32.TzSpecificLocalTimeToSystemTime
@ stdcall -thunk TzSpecificLocalTimeToSystemTimeEx(ptr ptr ptr) kernel32.TzSpecificLocalTimeToSystemTimeEx
@ stdcall -thunk UnhandledExceptionFilter(ptr) kernel32.UnhandledExceptionFilter
@ stdcall -thunk UnlockFile(long long long long long) kernel32.UnlockFile
@ stdcall -thunk UnlockFileEx(long long long long ptr) kernel32.UnlockFileEx
@ stdcall -thunk UnmapViewOfFile(ptr) kernel32.UnmapViewOfFile
@ stdcall -arch=win64 UnmapViewOfFile2(long ptr long)
@ stdcall -thunk -version=0x602+ UnmapViewOfFileEx(ptr long) kernel32.UnmapViewOfFileEx
@ stdcall UnregisterTraceGuids(int64) ntdll.EtwUnregisterTraceGuids
@ stdcall -thunk UnregisterWaitEx(long long) kernel32.UnregisterWaitEx
@ stdcall -thunk -version=0x600+ UpdateProcThreadAttribute(ptr long ptr ptr ptr ptr ptr) kernel32.UpdateProcThreadAttribute
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
@ stdcall -thunk VerLanguageNameA(long str long) kernel32.VerLanguageNameA
@ stdcall -thunk VerLanguageNameW(long wstr long) kernel32.VerLanguageNameW
@ stdcall VerQueryValueA(ptr str ptr ptr)
@ stdcall VerQueryValueW(ptr wstr ptr ptr)
@ stdcall VerifyScripts(long wstr long wstr long)
@ stdcall -thunk PrefetchVirtualMemory(ptr ptr ptr long) kernel32.PrefetchVirtualMemory
@ stdcall VerSetConditionMask(long long long long) ntdll.VerSetConditionMask
@ stdcall -thunk VirtualAlloc(ptr long long long) kernel32.VirtualAlloc
@ stdcall VirtualAlloc2(long ptr long long long ptr long)
@ stdcall VirtualAlloc2FromApp(long ptr long long long ptr long)
@ stdcall -thunk VirtualAllocEx(long ptr long long long) kernel32.VirtualAllocEx
@ stdcall -thunk -version=0x600+ VirtualAllocExNuma(long ptr long long long long) kernel32.VirtualAllocExNuma
@ stdcall VirtualAllocFromApp(ptr long long long)
@ stdcall -thunk VirtualFree(ptr long long) kernel32.VirtualFree
@ stdcall -thunk VirtualFreeEx(long ptr long long) kernel32.VirtualFreeEx
@ stdcall -thunk VirtualLock(ptr long) kernel32.VirtualLock
@ stdcall -thunk VirtualProtect(ptr long long ptr) kernel32.VirtualProtect
@ stdcall -thunk VirtualProtectEx(long ptr long long ptr) kernel32.VirtualProtectEx
@ stdcall -thunk VirtualQuery(ptr ptr long) kernel32.VirtualQuery
@ stdcall -thunk VirtualQueryEx(long ptr ptr long) kernel32.VirtualQueryEx
@ stdcall -thunk VirtualUnlock(ptr long) kernel32.VirtualUnlock
@ stdcall -thunk WaitCommEvent(long ptr ptr) kernel32.WaitCommEvent
@ stdcall -thunk WaitForDebugEvent(ptr long) kernel32.WaitForDebugEvent
@ stdcall -thunk -version=0xA00+ WaitForDebugEventEx(ptr long) kernel32.WaitForDebugEventEx
@ stdcall -thunk WaitForMultipleObjects(long ptr long long) kernel32.WaitForMultipleObjects
@ stdcall -thunk WaitForMultipleObjectsEx(long ptr long long long) kernel32.WaitForMultipleObjectsEx
@ stdcall -thunk WaitForSingleObject(long long) kernel32.WaitForSingleObject
@ stdcall -thunk WaitForSingleObjectEx(long long long) kernel32.WaitForSingleObjectEx
@ stdcall WaitForThreadpoolIoCallbacks(ptr long) ntdll.TpWaitForIoCompletion
@ stdcall WaitForThreadpoolTimerCallbacks(ptr long) ntdll.TpWaitForTimer
@ stdcall WaitForThreadpoolWaitCallbacks(ptr long) ntdll.TpWaitForWait
@ stdcall WaitForThreadpoolWorkCallbacks(ptr long) ntdll.TpWaitForWork
@ stdcall -thunk WaitNamedPipeW(wstr long) kernel32.WaitNamedPipeW
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
@ stdcall -thunk Wow64DisableWow64FsRedirection(ptr) kernel32.Wow64DisableWow64FsRedirection
@ stdcall -thunk Wow64EnableWow64FsRedirection(long) kernel32.Wow64EnableWow64FsRedirection
@ stdcall Wow64GetThreadContext(long ptr)
@ stdcall -thunk Wow64RevertWow64FsRedirection(ptr) kernel32.Wow64RevertWow64FsRedirection
@ stdcall Wow64SetThreadContext(long ptr)
@ stdcall -thunk Wow64SuspendThread(ptr) kernel32.Wow64SuspendThread
@ stdcall WriteConsoleA(long ptr long ptr ptr)
@ stdcall WriteConsoleInputA(long ptr long ptr)
@ stdcall WriteConsoleInputW(long ptr long ptr)
@ stdcall WriteConsoleOutputA(long ptr long long ptr)
@ stdcall WriteConsoleOutputAttribute(long ptr long long ptr)
@ stdcall WriteConsoleOutputCharacterA(long ptr long long ptr)
@ stdcall WriteConsoleOutputCharacterW(long ptr long long ptr)
@ stdcall WriteConsoleOutputW(long ptr long long ptr)
@ stdcall WriteConsoleW(long ptr long ptr ptr)
@ stdcall -thunk WriteFile(long ptr long ptr ptr) kernel32.WriteFile
@ stdcall -thunk WriteFileEx(long ptr long ptr ptr) kernel32.WriteFileEx
@ stdcall -thunk WriteFileGather(long ptr long ptr ptr) kernel32.WriteFileGather
@ stdcall -thunk WriteProcessMemory(long ptr ptr long ptr) kernel32.WriteProcessMemory
@ stdcall -thunk ZombifyActCtx(ptr) kernel32.ZombifyActCtx
@ stdcall -arch=win64 __C_specific_handler() ntdll.__C_specific_handler
@ stdcall -arch=x86_64,arm64 __chkstk() ntdll.__chkstk
@ stdcall -arch=x86_64 _local_unwind() ntdll._local_unwind
@ stdcall lstrcmp(str str) lstrcmpA
@ stdcall lstrcmpA(str str)
@ stdcall lstrcmpW(wstr wstr)
@ stdcall lstrcmpi(str str) lstrcmpiA
@ stdcall lstrcmpiA(str str)
@ stdcall lstrcmpiW(wstr wstr)
@ stdcall -thunk lstrcpyn(ptr str long) kernel32.lstrcpyn
@ stdcall -thunk lstrcpynA(ptr str long) kernel32.lstrcpynA
@ stdcall -thunk lstrcpynW(ptr wstr long) kernel32.lstrcpynW
@ stdcall -thunk lstrlen(str) kernel32.lstrlen
@ stdcall -thunk lstrlenA(str) kernel32.lstrlenA
@ stdcall -thunk lstrlenW(wstr) kernel32.lstrlenW
# TODO: Windows 11 kernelbase does not export this
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
@ stdcall -thunk QueryInterruptTime(ptr) kernel32.QueryInterruptTime
@ stdcall -thunk QueryInterruptTimePrecise(ptr) kernel32.QueryInterruptTimePrecise
# TODO: Windows 11 kernelbase forwards this to NTDLL
@ stdcall QueryUnbiasedInterruptTime(ptr) kernel32.QueryUnbiasedInterruptTime
@ stdcall -thunk QueryUnbiasedInterruptTimePrecise(ptr) kernel32.QueryUnbiasedInterruptTimePrecise
