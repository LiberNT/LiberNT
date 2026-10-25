# PROJECT:     ReactOS ARM64EC runtime
# PURPOSE:     Native NTDLL bridge exports for emulated AMD64 imports
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>

40 stdcall DbgBreakPoint() ChpeDbgBreakPoint
41 varargs DbgPrint(str) ChpeDbgPrint
42 varargs DbgPrintEx(long long str) ChpeDbgPrintEx
62 stdcall -version=0x600+ EtwEventActivityIdControl(long ptr) ChpeEtwEventActivityIdControl
65 stdcall -version=0x600+ EtwEventRegister(ptr ptr ptr ptr) ChpeEtwEventRegister
67 stdcall -version=0x600+ EtwEventUnregister(int64) ChpeEtwEventUnregister
68 stdcall -version=0x600+ EtwEventWrite(int64 ptr long ptr) ChpeEtwEventWrite
74 stdcall -version=0x600+ EtwEventWriteTransfer(int64 ptr ptr ptr long ptr) ChpeEtwEventWriteTransfer
1870 cdecl __C_specific_handler(ptr long ptr ptr) ChpeCSpecificHandler
2016 stdcall ChpeDispatchExceptionNative(ptr ptr)
108 stdcall KiUserExceptionDispatcher(ptr ptr) ChpeKiUserExceptionDispatcher
111 stdcall LdrAccessResource(ptr ptr ptr ptr) ChpeLdrAccessResource
113 stdcall LdrAddRefDll(long ptr) ChpeLdrAddRefDll
115 stdcall LdrEnumResources(ptr ptr long ptr ptr) ChpeLdrEnumResources
117 stdcall LdrFindEntryForAddress(ptr ptr) ChpeLdrFindEntryForAddress
118 stdcall LdrFindResourceDirectory_U(ptr ptr long ptr) ChpeLdrFindResourceDirectory_U
120 stdcall LdrFindResource_U(ptr ptr long ptr) ChpeLdrFindResource_U
122 stdcall LdrGetDllHandle(wstr ptr ptr ptr) ChpeLdrGetDllHandle
123 stdcall LdrGetDllHandleEx(long wstr ptr ptr ptr) ChpeLdrGetDllHandleEx
124 stdcall LdrGetDllHandleByName(ptr ptr ptr) ChpeLdrGetDllHandleByName
128 stdcall LdrGetProcedureAddress(ptr ptr long ptr) ChpeLdrGetProcedureAddress
135 stdcall LdrLoadDll(wstr long ptr ptr) ChpeLdrLoadDll
144 stdcall LdrQueryProcessModuleInformation(ptr long ptr) ChpeLdrQueryProcessModuleInformation
147 stdcall -version=0x602+ LdrResolveDelayLoadedAPI(ptr ptr ptr ptr ptr long) ChpeLdrResolveDelayLoadedAPI
148 stdcall -version=0x602+ LdrResolveDelayLoadsFromDll(ptr str long) ChpeLdrResolveDelayLoadsFromDll
161 stdcall LdrUnloadDll(ptr) ChpeLdrUnloadDll
191 stdcall NtAdjustPrivilegesToken(long long ptr long ptr ptr) ChpeAutoNtAdjustPrivilegesToken
200 stdcall NtAllocateVirtualMemory(long ptr ptr ptr long long) ChpeAutoNtAllocateVirtualMemory
201 stdcall NtAllocateVirtualMemoryEx(long ptr ptr long long ptr long) ChpeAutoNtAllocateVirtualMemoryEx
195 stdcall NtAlertThreadByThreadId(long) ChpeAutoNtAlertThreadByThreadId
235 stdcall NtClose(long) ChpeAutoNtClose
246 stdcall NtContinue(ptr long) ChpeNtContinue
247 stdcall -version=0xA00+ NtContinueEx(ptr ptr) ChpeNtContinueEx
254 stdcall NtCreateFile(ptr long ptr ptr ptr long long long long ptr long) ChpeAutoNtCreateFile
266 stdcall NtCreateNamedPipeFile(ptr long ptr ptr long long long long long long long long long ptr) ChpeAutoNtCreateNamedPipeFile
301 stdcall NtDeviceIoControlFile(long long ptr ptr ptr long ptr long ptr long) ChpeAutoNtDeviceIoControlFile
320 stdcall -version=0x600+ NtFlushProcessWriteBuffers() ChpeAutoNtFlushProcessWriteBuffers
318 stdcall NtFlushInstructionCache(long ptr long) ChpeAutoNtFlushInstructionCache
324 stdcall NtFreeVirtualMemory(long ptr ptr long) ChpeAutoNtFreeVirtualMemory
328 stdcall NtGetContextThread(long ptr) ChpeNtGetContextThread
334 stdcall -version=0x600+ NtGetNextThread(ptr ptr long long long ptr) ChpeAutoNtGetNextThread
363 stdcall NtMapViewOfSection(long long ptr long ptr ptr ptr long long long) ChpeAutoNtMapViewOfSection
364 stdcall NtMapViewOfSectionEx(long long ptr ptr ptr long long ptr long) ChpeNtMapViewOfSectionEx
369 stdcall NtNotifyChangeKey(ptr ptr ptr ptr ptr long long ptr long long) ChpeAutoNtNotifyChangeKey
370 stdcall NtNotifyChangeMultipleKeys(ptr long ptr ptr ptr ptr ptr long long ptr long long) ChpeAutoNtNotifyChangeMultipleKeys
375 stdcall NtOpenFile(ptr long ptr ptr long long) ChpeAutoNtOpenFile
410 stdcall NtProtectVirtualMemory(long ptr ptr long ptr) ChpeAutoNtProtectVirtualMemory
418 stdcall NtQueryDirectoryFile(long long ptr ptr ptr ptr long long long ptr long) ChpeAutoNtQueryDirectoryFile
428 stdcall NtQueryInformationFile(long ptr ptr long long) ChpeAutoNtQueryInformationFile
441 stdcall NtQueryKey(long long ptr long ptr) ChpeAutoNtQueryKey
261 stdcall NtCreateKey(ptr long ptr long ptr long ptr) ChpeAutoNtCreateKey
433 stdcall NtQueryInformationThread(long long ptr long ptr) ChpeAutoNtQueryInformationThread
431 stdcall NtQueryInformationProcess(ptr long ptr long ptr) ChpeAutoNtQueryInformationProcess
892 stdcall RtlFormatCurrentUserKeyPath(ptr) ChpeRtlFormatCurrentUserKeyPath
903 stdcall RtlFreeUnicodeString(ptr) ChpeRtlFreeUnicodeString
378 stdcall NtOpenKey(ptr long ptr) ChpeAutoNtOpenKey
840 stdcall NtOpenKeyEx(ptr long ptr long) ChpeAutoNtOpenKeyEx
462 stdcall NtQueryValueKey(long ptr long ptr long ptr) ChpeAutoNtQueryValueKey
559 stdcall NtSetValueKey(long ptr long long ptr long) ChpeAutoNtSetValueKey
295 stdcall NtDeleteKey(long) ChpeAutoNtDeleteKey
298 stdcall NtDeleteValueKey(long ptr) ChpeAutoNtDeleteValueKey
307 stdcall NtEnumerateKey(long long long ptr long ptr) ChpeAutoNtEnumerateKey
310 stdcall NtEnumerateValueKey(long long long ptr long ptr) ChpeAutoNtEnumerateValueKey
319 stdcall NtFlushKey(long) ChpeAutoNtFlushKey
256 stdcall NtCreateWaitCompletionPacket(ptr long ptr) ChpeAutoNtCreateWaitCompletionPacket
257 stdcall NtAssociateWaitCompletionPacket(long long long ptr ptr long long ptr) ChpeAutoNtAssociateWaitCompletionPacket
258 stdcall NtCancelWaitCompletionPacket(long long) ChpeAutoNtCancelWaitCompletionPacket
464 stdcall NtQueryVolumeInformationFile(long ptr ptr long long) ChpeAutoNtQueryVolumeInformationFile
445 stdcall NtQueryObject(long long long long long) ChpeAutoNtQueryObject
457 stdcall NtQuerySystemInformation(long ptr long ptr) ChpeAutoNtQuerySystemInformation
939 stdcall RtlGetNativeSystemInformation(long ptr long ptr) ChpeRtlGetNativeSystemInformation
463 stdcall NtQueryVirtualMemory(long ptr long ptr long ptr) ChpeAutoNtQueryVirtualMemory
470 stdcall NtRaiseException(ptr ptr long) ChpeNtRaiseException
472 stdcall NtReadFile(long long ptr ptr ptr ptr long ptr ptr) ChpeAutoNtReadFile
476 stdcall NtReadVirtualMemory(long ptr ptr long ptr) ChpeAutoNtReadVirtualMemory
517 stdcall NtSetContextThread(long ptr) ChpeNtSetContextThread
568 stdcall NtSuspendProcess(ptr) ChpeAutoNtSuspendProcess
569 stdcall NtSuspendThread(ptr ptr) ChpeAutoNtSuspendThread
506 stdcall NtResumeThread(ptr ptr) ChpeAutoNtResumeThread
572 stdcall NtTerminateProcess(long long) ChpeAutoNtTerminateProcess
573 stdcall NtTerminateThread(long long) ChpeAutoNtTerminateThread
586 stdcall NtUnmapViewOfSection(long ptr) ChpeAutoNtUnmapViewOfSection
587 stdcall NtUnmapViewOfSectionEx(long ptr long) ChpeNtUnmapViewOfSectionEx
593 stdcall NtWaitForAlertByThreadId(ptr ptr) ChpeAutoNtWaitForAlertByThreadId
601 stdcall NtWriteFile(long long ptr ptr ptr ptr long ptr ptr) ChpeAutoNtWriteFile
604 stdcall NtWriteVirtualMemory(long ptr ptr long ptr) ChpeAutoNtWriteVirtualMemory
639 stdcall RtlAddFunctionTable(ptr long long) ChpeRtlAddFunctionTable
640 stdcall RtlAddGrowableFunctionTable(ptr ptr long long long long) ChpeRtlAddGrowableFunctionTable
646 stdcall RtlAddVectoredContinueHandler(long ptr) ChpeRtlAddVectoredContinueHandler
647 stdcall RtlAddVectoredExceptionHandler(long ptr) ChpeRtlAddVectoredExceptionHandler
653 stdcall RtlAllocateHeap(ptr long ptr) ChpeRtlAllocateHeap
914 stdcall RtlGetCurrentPeb() ChpeRtlGetCurrentPeb
946 stdcall RtlGetProcessHeaps(long ptr) ChpeRtlGetProcessHeaps
1128 stdcall RtlQueryEnvironmentVariable(ptr ptr long ptr long ptr) ChpeRtlQueryEnvironmentVariable
617 stdcall RtlAcquirePrivilege(ptr long long ptr) ChpeRtlAcquirePrivilege
620 stdcall RtlAcquireSRWLockExclusive(ptr) ChpeRtlAcquireSRWLockExclusive
621 stdcall RtlAcquireSRWLockShared(ptr) ChpeRtlAcquireSRWLockShared
675 stdcall RtlCaptureContext(ptr) ChpeRtlCaptureContextX64
676 stdcall RtlCaptureStackBackTrace(long long ptr ptr) ChpeRtlCaptureStackBackTrace
692 stdcall RtlCompareMemory(ptr ptr long) ChpeRtlCompareMemory
695 stdcall RtlCompareUnicodeString(ptr ptr long) ChpeRtlCompareUnicodeString
723 stdcall RtlCopyUnicodeString(ptr ptr) ChpeRtlCopyUnicodeString
767 stdcall RtlDecodePointer(ptr) ChpeRtlDecodePointer
768 stdcall RtlDecodeSystemPointer(ptr) ChpeRtlDecodeSystemPointer
778 stdcall RtlDeleteCriticalSection(ptr) ChpeRtlDeleteCriticalSection
781 cdecl RtlDeleteFunctionTable(ptr) ChpeRtlDeleteFunctionTable
782 stdcall RtlDeleteGrowableFunctionTable(ptr) ChpeRtlDeleteGrowableFunctionTable
822 stdcall RtlEnterCriticalSection(ptr) ChpeRtlEnterCriticalSection
820 stdcall RtlEncodePointer(ptr) ChpeRtlEncodePointer
821 stdcall RtlEncodeSystemPointer(ptr) ChpeRtlEncodeSystemPointer
860 stdcall RtlExitUserThread(long) ChpeRtlExitUserThread
865 stdcall RtlFillMemory(ptr long long) ChpeRtlFillMemory
887 stdcall RtlFlsAlloc(ptr ptr) ChpeRtlFlsAlloc
888 stdcall RtlFlsFree(long) ChpeRtlFlsFree
889 stdcall RtlFlsGetValue(long ptr) ChpeRtlFlsGetValue
890 stdcall RtlFlsSetValue(long ptr) ChpeRtlFlsSetValue
898 stdcall RtlFreeHeap(long long long) ChpeRtlFreeHeap
930 stdcall RtlGetFunctionTableListHead() ChpeRtlGetFunctionTableListHead
933 stdcall RtlGetLastNtStatus() ChpeRtlGetLastNtStatus
934 stdcall RtlGetLastWin32Error() ChpeRtlGetLastWin32Error
915 stdcall RtlGetCurrentProcessorNumber() ChpeRtlGetCurrentProcessorNumber
916 stdcall -version=0x601+ RtlGetCurrentProcessorNumberEx(ptr) ChpeRtlGetCurrentProcessorNumberEx
947 stdcall -version=0x600+ RtlGetProductInfo(long long long long ptr) ChpeRtlGetProductInfo
960 stdcall RtlGetVersion(ptr) ChpeRtlGetVersion
1012 stdcall RtlGrowFunctionTable(ptr long) ChpeRtlGrowFunctionTable
985 stdcall RtlInitUnicodeString(ptr wstr) ChpeRtlInitUnicodeString
990 stdcall -version=0x600+ RtlInitializeConditionVariable(ptr) ChpeRtlInitializeConditionVariable
992 stdcall RtlInitializeCriticalSection(ptr) ChpeRtlInitializeCriticalSection
1003 stdcall RtlInitializeSListHead(ptr) ChpeRtlInitializeSListHead
1004 stdcall -version=0x600+ RtlInitializeSRWLock(ptr) ChpeRtlInitializeSRWLock
1011 cdecl RtlInstallFunctionTableCallback(double double long ptr ptr ptr) ChpeRtlInstallFunctionTableCallback
1016 stdcall RtlInterlockedFlushSList(ptr) ChpeRtlInterlockedFlushSList
1017 stdcall RtlInterlockedPopEntrySList(ptr) ChpeRtlInterlockedPopEntrySList
1018 stdcall RtlInterlockedPushEntrySList(ptr ptr) ChpeRtlInterlockedPushEntrySList
1019 stdcall RtlInterlockedPushListSList(ptr ptr ptr long) ChpeRtlInterlockedPushListSList
1020 stdcall -version=0x602+ RtlInterlockedPushListSListEx(ptr ptr ptr long) ChpeRtlInterlockedPushListSListEx
1050 stdcall RtlIsProcessorFeaturePresent(long) ChpeRtlIsProcessorFeaturePresent
1059 stdcall RtlLeaveCriticalSection(ptr) ChpeRtlLeaveCriticalSection
1081 stdcall RtlLookupFunctionEntry(long ptr ptr) ChpeRtlLookupFunctionEntry
1082 stdcall RtlLookupFunctionTable(int64 ptr ptr) ChpeRtlLookupFunctionTable
1072 stdcall -version=0xA00+ RtlLogUnexpectedCodepath(ptr) ChpeRtlLogUnexpectedCodepath
1086 stdcall RtlMoveMemory(ptr ptr long) ChpeRtlMoveMemory
1100 stdcall RtlNtStatusToDosError(long) ChpeRtlNtStatusToDosError
1113 stdcall RtlPcToFileHeader(ptr ptr) ChpeRtlPcToFileHeader
1125 stdcall RtlQueryDepthSList(ptr) ChpeRtlQueryDepthSList
1147 stdcall -norelay RtlRaiseException(ptr) ChpeRtlRaiseException
1148 stdcall RtlRaiseStatus(long) ChpeRtlRaiseStatus
1149 stdcall RtlRandom(ptr) ChpeRtlRandom
1151 stdcall RtlReAllocateHeap(long long ptr long) ChpeRtlReAllocateHeap
1163 stdcall RtlReleasePrivilege(ptr) ChpeRtlReleasePrivilege
1166 stdcall RtlReleaseSRWLockExclusive(ptr) ChpeRtlReleaseSRWLockExclusive
1167 stdcall RtlReleaseSRWLockShared(ptr) ChpeRtlReleaseSRWLockShared
1170 stdcall RtlRemoveVectoredContinueHandler(ptr) ChpeRtlRemoveVectoredContinueHandler
1171 stdcall RtlRemoveVectoredExceptionHandler(ptr) ChpeRtlRemoveVectoredExceptionHandler
1177 stdcall RtlRestoreContext(ptr ptr) ChpeRtlRestoreContext
1178 stdcall RtlRestoreLastWin32Error(long) ChpeRtlRestoreLastWin32Error
1185 stdcall -version=0x600+ RtlRunOnceExecuteOnce(ptr ptr ptr ptr) ChpeRtlRunOnceExecuteOnce
1186 stdcall -version=0x600+ RtlRunOnceInitialize(ptr) ChpeRtlRunOnceInitialize
1237 stdcall RtlSizeHeap(long long ptr) ChpeRtlSizeHeap
1198 stdcall RtlSetCriticalSectionSpinCount(ptr long) ChpeRtlSetCriticalSectionSpinCount
1212 stdcall RtlSetLastWin32Error(long) ChpeRtlSetLastWin32Error
1264 stdcall RtlTryAcquireSRWLockExclusive(ptr) ChpeRtlTryAcquireSRWLockExclusive
1265 stdcall RtlTryAcquireSRWLockShared(ptr) ChpeRtlTryAcquireSRWLockShared
1266 stdcall RtlTryEnterCriticalSection(ptr) ChpeRtlTryEnterCriticalSection
1272 stdcall -version=0x601+ RtlUTF8ToUnicodeN(ptr long ptr str long) ChpeRtlUTF8ToUnicodeN
1290 stdcall RtlUnwind(ptr ptr ptr ptr) ChpeRtlUnwind
1291 stdcall RtlUnwindEx(ptr ptr ptr ptr ptr ptr) ChpeRtlUnwindEx
1315 stdcall RtlVirtualUnwind(long int64 int64 ptr ptr ptr ptr ptr) ChpeRtlVirtualUnwind
1317 stdcall -version=0x602+ RtlWaitOnAddress(ptr ptr long ptr) ChpeRtlWaitOnAddress
1319 stdcall -version=0x602+ RtlWakeAddressAll(ptr) ChpeRtlWakeAddressAll
1320 stdcall -version=0x602+ RtlWakeAddressSingle(ptr) ChpeRtlWakeAddressSingle
1318 stdcall RtlWakeAllConditionVariable(ptr) ChpeRtlWakeAllConditionVariable
1321 stdcall RtlWakeConditionVariable(ptr) ChpeRtlWakeConditionVariable
1346 stdcall RtlZeroMemory(ptr long) ChpeRtlZeroMemory
1400 stdcall -version=0x600+ TpCallbackLeaveCriticalSectionOnCompletion(ptr ptr) ChpeTpCallbackLeaveCriticalSectionOnCompletion
1402 stdcall -version=0x600+ TpCallbackReleaseMutexOnCompletion(ptr ptr) ChpeTpCallbackReleaseMutexOnCompletion
1403 stdcall -version=0x600+ TpCallbackReleaseSemaphoreOnCompletion(ptr ptr long) ChpeTpCallbackReleaseSemaphoreOnCompletion
1406 stdcall -version=0x600+ TpCallbackSetEventOnCompletion(ptr ptr) ChpeTpCallbackSetEventOnCompletion
1407 stdcall -version=0x600+ TpCallbackUnloadDllOnCompletion(ptr ptr) ChpeTpCallbackUnloadDllOnCompletion
1408 stdcall -version=0x600+ TpCancelAsyncIoOperation(ptr) ChpeTpCancelAsyncIoOperation
1413 stdcall -version=0x600+ TpDisassociateCallback(ptr) ChpeTpDisassociateCallback
1414 stdcall -version=0x600+ TpIsTimerSet(ptr) ChpeTpIsTimerSet
1415 stdcall -version=0x600+ TpPostWork(ptr) ChpeTpPostWork
1418 stdcall -version=0x600+ TpReleaseCleanupGroup(ptr) ChpeTpReleaseCleanupGroup
1419 stdcall -version=0x600+ TpReleaseCleanupGroupMembers(ptr long ptr) ChpeTpReleaseCleanupGroupMembers
1420 stdcall -version=0x600+ TpReleaseIoCompletion(ptr) ChpeTpReleaseIoCompletion
1421 stdcall -version=0x600+ TpReleasePool(ptr) ChpeTpReleasePool
1422 stdcall TpReleaseTimer(ptr) ChpeTpReleaseTimer
1423 stdcall TpReleaseWait(ptr) ChpeTpReleaseWait
1424 stdcall -version=0x600+ TpReleaseWork(ptr) ChpeTpReleaseWork
1425 stdcall -version=0x600+ TpSetPoolMaxThreads(ptr long) ChpeTpSetPoolMaxThreads
1426 stdcall -version=0x600+ TpSetPoolMinThreads(ptr long) ChpeTpSetPoolMinThreads
1428 stdcall TpSetTimer(ptr ptr long long) ChpeTpSetTimer
1429 stdcall -version=0x602+ TpSetTimerEx(ptr ptr long long) ChpeTpSetTimerEx
1430 stdcall TpSetWait(ptr long ptr) ChpeTpSetWait
1431 stdcall -version=0x602+ TpSetWaitEx(ptr long ptr ptr) ChpeTpSetWaitEx
1433 stdcall -version=0x600+ TpStartAsyncIoOperation(ptr) ChpeTpStartAsyncIoOperation
1435 stdcall -version=0x600+ TpWaitForIoCompletion(ptr long) ChpeTpWaitForIoCompletion
1436 stdcall TpWaitForTimer(ptr long) ChpeTpWaitForTimer
1437 stdcall -version=0x600+ TpWaitForWait(ptr long) ChpeTpWaitForWait
1438 stdcall -version=0x600+ TpWaitForWork(ptr long) ChpeTpWaitForWork
1439 stdcall -ret64 VerSetConditionMask(double long long) ChpeVerSetConditionMask
1892 varargs _snprintf(ptr long str) ChpeSnprintf
1894 varargs _snwprintf(ptr long wstr) ChpeSnwprintf
1903 varargs _swprintf(ptr wstr) ChpeSwprintf
1961 varargs sprintf(ptr str) ChpeSprintf
1983 varargs swprintf(ptr wstr) ChpeSwprintf
1893 varargs _snprintf_s(ptr long long str) ChpeSnprintf_s
1895 varargs _snwprintf_s(ptr long long wstr) ChpeSnwprintf_s
1871 cdecl __chkstk() ChpeChkStk
1885 cdecl _local_unwind(ptr ptr) ChpeLocalUnwind
1955 cdecl memcpy(ptr ptr long) ChpeMemcpy
2017 stdcall ChpeEmulationDispatch(ptr)
1335 stdcall RtlWow64GetThreadSelectorEntry(ptr ptr long ptr) ChpeRtlWow64GetThreadSelectorEntry

278 stdcall NtCreateThread(ptr long ptr long ptr ptr ptr long) ChpeNtCreateThread
327 stdcall NtFsControlFile(long long ptr ptr ptr long ptr long ptr long) ChpeAutoNtFsControlFile
556 stdcall NtSetTimer(long ptr ptr ptr long long ptr) ChpeAutoNtSetTimer
758 stdcall RtlCreateUserThread(long ptr long long ptr ptr ptr ptr ptr) ChpeRtlCreateUserThread
770 stdcall RtlDecompressFragment(long ptr long ptr long long ptr ptr) ChpeRtlDecompressFragment
906 stdcall RtlGenerate8dot3Name(ptr long ptr ptr) ChpeRtlGenerate8dot3Name
936 stdcall RtlGetLengthWithoutLastFullDosOrNtPathElement(long ptr ptr) ChpeRtlGetLengthWithoutLastFullDosOrNtPathElement
956 stdcall RtlGetUnloadEventTrace() ChpeRtlGetUnloadEventTrace
1146 stdcall RtlQueueWorkItem(ptr ptr long) ChpeRtlQueueWorkItem
1159 stdcall RtlRegisterWait(ptr ptr ptr ptr long long) ChpeRtlRegisterWait
1348 stdcall RtlpApplyLengthFunction(long long ptr ptr) ChpeRtlpApplyLengthFunction
1990 stdcall vDbgPrintEx(long long str ptr) ChpevDbgPrintEx
1991 stdcall vDbgPrintExWithPrefix(str long long str ptr) ChpevDbgPrintExWithPrefix
839 stdcall LdrGetDllFullName(ptr ptr) ChpeLdrGetDllFullName
876 stdcall RtlFindExportedRoutineByName(ptr str) ChpeRtlFindExportedRoutineByName
842 stdcall RtlIsEcCode(ptr) ChpeRtlIsEcCode
851 stdcall RtlLocateExtendedFeature(ptr long ptr) ChpeRtlLocateExtendedFeature
852 stdcall RtlLocateExtendedFeature2(ptr long ptr ptr) ChpeRtlLocateExtendedFeature2
854 stdcall RtlQueryPerformanceCounter(ptr) ChpeRtlQueryPerformanceCounter
855 stdcall RtlQueryPerformanceFrequency(ptr) ChpeRtlQueryPerformanceFrequency
857 stdcall RtlQuerySystemTime(ptr) ChpeRtlQuerySystemTime
858 stdcall RtlSystemTimeToTimeFields(ptr ptr) ChpeRtlSystemTimeToTimeFields
43 varargs DbgPrintReturnControlC(str) ChpeDbgPrintReturnControlC
57 stdcall DbgUserBreakPoint() ChpeDbgUserBreakPoint
90 varargs EtwTraceMessage(int64 long ptr long) ChpeEtwTraceMessage
1306 stdcall RtlUserThreadStart(long long) ntdll.RtlUserThreadStart
1963 varargs sscanf(str str) ChpeSscanf
2010 cdecl ChpeVsscanf(str str ptr) ChpeAutoVsscanf
1964 varargs sscanf_s(str str) ChpeSscanf_s
1984 varargs swscanf_s(wstr wstr) ChpeSwscanf_s
1896 varargs _snwscanf_s(wstr long wstr) ChpeSnwscanf_s
2011 cdecl ChpeVsscanf_s(str str ptr) ChpeAutoVsscanf_s
2012 cdecl ChpeVswscanf_s(wstr wstr ptr) ChpeAutoVswscanf_s
2013 cdecl ChpeVsnwscanf_s(wstr long wstr ptr) ChpeAutoVsnwscanf_s
2014 stdcall ChpeVDbgPrintReturnControlC(str ptr) ChpeAutoVDbgPrintReturnControlC
1878 cdecl -private _errno() ChpeErrno
1884 cdecl _lfind(ptr ptr ptr long ptr) ChpeLfind
1927 cdecl bsearch(ptr ptr long long ptr) ChpeBsearch
1959 cdecl qsort(ptr long long ptr) ChpeQsort
1890 cdecl _setjmp(ptr ptr) ChpeSetJmpX64
1891 cdecl _setjmpex(ptr ptr) ChpeSetJmpX64
1951 cdecl longjmp(ptr long) ChpeLongJmp
979 stdcall RtlInitMemoryStream(ptr) ChpeRtlInitMemoryStream
981 stdcall RtlInitOutOfProcessMemoryStream(ptr) ChpeRtlInitOutOfProcessMemoryStream
1161 stdcall RtlReleaseMemoryStream(ptr) ChpeRtlReleaseMemoryStream
99 stdcall ExpInterlockedPopEntrySListEnd() ChpeUnsupportedKernelEntry
101 stdcall ExpInterlockedPopEntrySListFault() ChpeUnsupportedKernelEntry
103 stdcall ExpInterlockedPopEntrySListResume() ChpeUnsupportedKernelEntry
106 stdcall KiUserApcDispatcher(ptr ptr ptr ptr) ChpeUnsupportedApcEntry
110 stdcall KiUserEmulationDispatcher(ptr) ChpeUnsupportedEmulationEntry

# BEGIN typed bridge wrappers (generate_chpe_bridge.py)
1 stdcall ApiSetQueryApiSetPresence(ptr ptr) ChpeAutoApiSetQueryApiSetPresence
2 stdcall ApiSetQueryApiSetPresenceEx(ptr ptr ptr) ChpeAutoApiSetQueryApiSetPresenceEx
3 stdcall -version=0x600+ RtlFlushHeaps() ChpeAutoRtlFlushHeaps
4 stdcall -version=0x602+ RtlQueryWnfStateData(ptr int64 ptr ptr ptr long) ChpeAutoRtlQueryWnfStateData
5 stdcall -version=0x602+ RtlSubscribeWnfStateChangeNotification(ptr int64 long ptr ptr ptr long long long) ChpeAutoRtlSubscribeWnfStateChangeNotification
6 stdcall -version=0x602+ RtlUnsubscribeWnfNotificationWaitForCompletion(ptr) ChpeAutoRtlUnsubscribeWnfNotificationWaitForCompletion
7 stdcall -version=0x602+ RtlUnsubscribeWnfStateChangeNotification(ptr) ChpeAutoRtlUnsubscribeWnfStateChangeNotification
8 stdcall -version=0x600+ A_SHAFinal(ptr ptr) ChpeAutoA_SHAFinal
9 stdcall -version=0x600+ A_SHAInit(ptr) ChpeAutoA_SHAInit
10 stdcall -version=0x600+ A_SHAUpdate(ptr ptr long) ChpeAutoA_SHAUpdate
11 stdcall -version=0x600+ AlpcAdjustCompletionListConcurrencyCount(ptr long) ChpeAutoAlpcAdjustCompletionListConcurrencyCount
12 stdcall -version=0x600+ AlpcFreeCompletionListMessage(ptr ptr) ChpeAutoAlpcFreeCompletionListMessage
13 stdcall -version=0x600+ AlpcGetCompletionListLastMessageInformation(ptr ptr ptr) ChpeAutoAlpcGetCompletionListLastMessageInformation
14 stdcall -version=0x600+ AlpcGetCompletionListMessageAttributes(ptr ptr) ChpeAutoAlpcGetCompletionListMessageAttributes
15 stdcall -version=0x600+ AlpcGetHeaderSize(long) ChpeAutoAlpcGetHeaderSize
16 stdcall -version=0x600+ AlpcGetMessageAttribute(ptr long) ChpeAutoAlpcGetMessageAttribute
17 stdcall -version=0x600+ AlpcGetMessageFromCompletionList(ptr ptr) ChpeAutoAlpcGetMessageFromCompletionList
18 stdcall -version=0x600+ AlpcGetOutstandingCompletionListMessageCount(ptr) ChpeAutoAlpcGetOutstandingCompletionListMessageCount
19 stdcall -version=0x600+ AlpcInitializeMessageAttribute(long ptr long ptr) ChpeAutoAlpcInitializeMessageAttribute
20 stdcall -version=0x600+ AlpcMaxAllowedMessageLength() ChpeAutoAlpcMaxAllowedMessageLength
21 stdcall -version=0x600+ AlpcRegisterCompletionList(ptr ptr long long long) ChpeAutoAlpcRegisterCompletionList
22 stdcall -version=0x600+ AlpcRegisterCompletionListWorkerThread(ptr) ChpeAutoAlpcRegisterCompletionListWorkerThread
23 stdcall -version=0x600+ AlpcRundownCompletionList(ptr) ChpeAutoAlpcRundownCompletionList
24 stdcall -version=0x600+ AlpcUnregisterCompletionList(ptr) ChpeAutoAlpcUnregisterCompletionList
25 stdcall -version=0x600+ AlpcUnregisterCompletionListWorkerThread(ptr) ChpeAutoAlpcUnregisterCompletionListWorkerThread
26 stdcall CsrAllocateCaptureBuffer(long long) ChpeAutoCsrAllocateCaptureBuffer
27 stdcall CsrAllocateMessagePointer(ptr long ptr) ChpeAutoCsrAllocateMessagePointer
28 stdcall CsrCaptureMessageBuffer(ptr ptr long ptr) ChpeAutoCsrCaptureMessageBuffer
29 stdcall CsrCaptureMessageMultiUnicodeStringsInPlace(ptr long ptr) ChpeAutoCsrCaptureMessageMultiUnicodeStringsInPlace
30 stdcall CsrCaptureMessageString(ptr str long long ptr) ChpeAutoCsrCaptureMessageString
31 stdcall CsrCaptureTimeout(long ptr) ChpeAutoCsrCaptureTimeout
32 stdcall CsrClientCallServer(ptr ptr long long) ChpeAutoCsrClientCallServer
33 stdcall CsrClientConnectToServer(str long ptr ptr ptr) ChpeAutoCsrClientConnectToServer
34 stdcall CsrFreeCaptureBuffer(ptr) ChpeAutoCsrFreeCaptureBuffer
35 stdcall CsrGetProcessId() ChpeAutoCsrGetProcessId
36 stdcall CsrIdentifyAlertableThread() ChpeAutoCsrIdentifyAlertableThread
37 stdcall -version=0x502+ CsrNewThread() ChpeAutoCsrNewThread
38 stdcall CsrSetPriorityClass(ptr ptr) ChpeAutoCsrSetPriorityClass
39 stdcall -version=0x600+ CsrVerifyRegion(ptr long) ChpeStubCsrVerifyRegion
44 stdcall DbgPrompt(ptr ptr long) ChpeAutoDbgPrompt
45 stdcall DbgQueryDebugFilterState(long long) ChpeAutoDbgQueryDebugFilterState
46 stdcall DbgSetDebugFilterState(long long long) ChpeAutoDbgSetDebugFilterState
47 stdcall DbgUiConnectToDbg() ChpeAutoDbgUiConnectToDbg
48 stdcall DbgUiContinue(ptr long) ChpeAutoDbgUiContinue
49 stdcall DbgUiConvertStateChangeStructure(ptr ptr) ChpeAutoDbgUiConvertStateChangeStructure
50 stdcall DbgUiDebugActiveProcess(ptr) ChpeAutoDbgUiDebugActiveProcess
51 stdcall DbgUiGetThreadDebugObject() ChpeAutoDbgUiGetThreadDebugObject
52 stdcall DbgUiIssueRemoteBreakin(ptr) ChpeAutoDbgUiIssueRemoteBreakin
53 stdcall DbgUiRemoteBreakin() ChpeAutoDbgUiRemoteBreakin
54 stdcall DbgUiSetThreadDebugObject(ptr) ChpeAutoDbgUiSetThreadDebugObject
55 stdcall DbgUiStopDebugging(ptr) ChpeAutoDbgUiStopDebugging
56 stdcall DbgUiWaitStateChange(ptr ptr) ChpeAutoDbgUiWaitStateChange
58 stdcall -version=0xA00+ EtwCheckCoverage(ptr) ChpeAutoEtwCheckCoverage
59 stdcall  EtwCreateTraceInstanceId(ptr ptr) ChpeStubEtwCreateTraceInstanceId
60 stdcall -version=0x600+ EtwDeliverDataBlock(long) ChpeStubEtwDeliverDataBlock
61 stdcall -version=0x600+ EtwEnumerateProcessRegGuids(ptr long ptr) ChpeStubEtwEnumerateProcessRegGuids
63 stdcall -version=0x600+ EtwEventEnabled(int64 ptr) ChpeAutoEtwEventEnabled
64 stdcall -version=0x600+ EtwEventProviderEnabled(int64 long int64) ChpeAutoEtwEventProviderEnabled
66 stdcall -version=0x600+ EtwEventSetInformation(int64 long ptr long) ChpeAutoEtwEventSetInformation
69 stdcall -version=0x601+ EtwEventWriteEx(int64 ptr int64 long ptr ptr long ptr) ChpeAutoEtwEventWriteEx
70 stdcall -version=0x600+ EtwEventWriteEndScenario(long ptr long long) ChpeStubEtwEventWriteEndScenario
71 stdcall -version=0x600+ EtwEventWriteFull(long long long long long long long) ChpeStubEtwEventWriteFull
72 stdcall -version=0x600+ EtwEventWriteStartScenario(long ptr long long) ChpeStubEtwEventWriteStartScenario
73 stdcall -version=0x600+ EtwEventWriteString(int64 long int64 wstr) ChpeAutoEtwEventWriteString
75 stdcall EtwGetTraceEnableFlags(double) ChpeAutoEtwGetTraceEnableFlags
76 stdcall EtwGetTraceEnableLevel(double) ChpeAutoEtwGetTraceEnableLevel
77 stdcall EtwGetTraceLoggerHandle(ptr) ChpeAutoEtwGetTraceLoggerHandle
78 stdcall -version=0x600+ EtwLogTraceEvent(long long) ChpeStubEtwLogTraceEvent
79 stdcall -version=0x600+ EtwNotificationRegister(ptr long long long ptr) ChpeStubEtwNotificationRegister
80 stdcall -version=0x600+ EtwNotificationUnregister(long ptr) ChpeStubEtwNotificationUnregister
81 stdcall -version=0x600+ EtwProcessPrivateLoggerRequest(ptr) ChpeStubEtwProcessPrivateLoggerRequest
82 stdcall -version=0x600+ EtwRegister(ptr ptr ptr ptr) ChpeStubEtwRegister
83 stdcall -version=0x600+ EtwRegisterSecurityProvider() ChpeStubEtwRegisterSecurityProvider
84 stdcall EtwRegisterTraceGuidsA(ptr ptr ptr long ptr str str ptr) ChpeAutoEtwRegisterTraceGuidsA
85 stdcall EtwRegisterTraceGuidsW(ptr ptr ptr long ptr wstr wstr ptr) ChpeAutoEtwRegisterTraceGuidsW
86 stdcall -version=0x600+ EtwReplyNotification(long) ChpeStubEtwReplyNotification
87 stdcall -version=0x600+ EtwSendNotification(long long ptr long long) ChpeStubEtwSendNotification
88 stdcall -version=0x600+ EtwSetMark(long long long) ChpeStubEtwSetMark
89 stdcall  EtwTraceEventInstance(double ptr ptr ptr) ChpeStubEtwTraceEventInstance
91 stdcall  EtwTraceMessageVa(int64 long ptr long ptr) ChpeStubEtwTraceMessageVa
92 stdcall -version=0x600+ EtwUnregister(int64) ChpeStubEtwUnregister
93 stdcall EtwUnregisterTraceGuids(double) ChpeAutoEtwUnregisterTraceGuids
94 stdcall -version=0x600+ EtwWrite(int64 ptr ptr long ptr) ChpeStubEtwWrite
95 stdcall -version=0x600+ EtwWriteUMSecurityEvent(ptr long long long) ChpeStubEtwWriteUMSecurityEvent
96 stdcall -version=0x600+ EtwpCreateEtwThread(long long) ChpeStubEtwpCreateEtwThread
97 stdcall -version=0x600+ EtwpGetCpuSpeed(ptr) ChpeStubEtwpGetCpuSpeed
98 stdcall -version=0x600+ EtwpNotificationThread() ChpeStubEtwpNotificationThread
100 stub -version=0x600+ ExpInterlockedPopEntrySListEnd8
102 stub -version=0x600+ ExpInterlockedPopEntrySListFault8
104 stub -version=0x600+ ExpInterlockedPopEntrySListResume8
105 stdcall KiRaiseUserExceptionDispatcher() ChpeAutoKiRaiseUserExceptionDispatcher
107 stdcall KiUserCallbackDispatcher(ptr ptr long) ChpeAutoKiUserCallbackDispatcher
109 stdcall KiUserExceptionDispatcherWorker(ptr ptr) ChpeAutoKiUserExceptionDispatcherWorker
112 stdcall -version=0x600+ LdrAddLoadAsDataTable(ptr wstr long ptr) ChpeStubLdrAddLoadAsDataTable
114 stdcall LdrDisableThreadCalloutsForDll(ptr) ChpeAutoLdrDisableThreadCalloutsForDll
116 stdcall LdrEnumerateLoadedModules(long ptr ptr) ChpeAutoLdrEnumerateLoadedModules
119 stdcall  LdrFindResourceEx_U(ptr ptr ptr ptr ptr) ChpeStubLdrFindResourceEx_U
121 stdcall LdrFlushAlternateResourceModules() ChpeAutoLdrFlushAlternateResourceModules
125 stdcall -version=0x600+ LdrGetFailureData() ChpeStubLdrGetFailureData
126 stdcall -version=0x600+ LdrGetFileNameFromLoadAsDataTable(ptr ptr) ChpeStubLdrGetFileNameFromLoadAsDataTable
127 stdcall -version=0x600+ LdrGetKnownDllSectionHandle(wstr long ptr) ChpeStubLdrGetKnownDllSectionHandle
129 stdcall -version=0x600+ LdrGetProcedureAddressEx(ptr ptr long ptr long) ChpeAutoLdrGetProcedureAddressEx
130 stdcall  LdrHotPatchRoutine(ptr) ChpeStubLdrHotPatchRoutine
131 stdcall LdrInitShimEngineDynamic(ptr) ChpeAutoLdrInitShimEngineDynamic
132 stdcall LdrInitializeThunk(long long long long) ChpeAutoLdrInitializeThunk
133 stdcall LdrLoadAlternateResourceModule(ptr ptr) ChpeAutoLdrLoadAlternateResourceModule
134 stdcall -version=0x600+ LdrLoadAlternateResourceModuleEx(long long ptr ptr long) ChpeStubLdrLoadAlternateResourceModuleEx
136 stdcall LdrLockLoaderLock(long ptr ptr) ChpeAutoLdrLockLoaderLock
137 stdcall LdrOpenImageFileOptionsKey(ptr long ptr) ChpeAutoLdrOpenImageFileOptionsKey
138 stdcall -version=0x600+ LdrProcessInitializationComplete() ChpeStubLdrProcessInitializationComplete
139 stdcall LdrProcessRelocationBlock(ptr long ptr long) ChpeAutoLdrProcessRelocationBlock
140 stdcall LdrQueryImageFileExecutionOptions(ptr str long ptr long ptr) ChpeAutoLdrQueryImageFileExecutionOptions
141 stdcall LdrQueryImageFileExecutionOptionsEx(ptr ptr long ptr long ptr long) ChpeAutoLdrQueryImageFileExecutionOptionsEx
142 stdcall LdrQueryImageFileKeyOption(ptr ptr long ptr long ptr) ChpeAutoLdrQueryImageFileKeyOption
143 stdcall -version=0x600+ LdrQueryModuleServiceTags(ptr ptr ptr) ChpeStubLdrQueryModuleServiceTags
145 stdcall -version=0x600+ LdrRegisterDllNotification(long ptr ptr ptr) ChpeAutoLdrRegisterDllNotification
146 stdcall -version=0x600+ LdrRemoveLoadAsDataTable(ptr ptr ptr long) ChpeStubLdrRemoveLoadAsDataTable
149 stdcall -version=0x600+ LdrResFindResource(ptr long long long ptr ptr ptr ptr long) ChpeAutoLdrResFindResource
150 stdcall -version=0x600+ LdrResFindResourceDirectory(ptr long long ptr ptr ptr long ptr) ChpeAutoLdrResFindResourceDirectory
151 stdcall -version=0x600+ LdrResRelease(ptr ptr long long) ChpeStubLdrResRelease
152 stdcall -version=0x600+ LdrResSearchResource(wstr wstr long long long ptr long long) ChpeStubLdrResSearchResource
153 stdcall LdrSetAppCompatDllRedirectionCallback(long ptr ptr) ChpeAutoLdrSetAppCompatDllRedirectionCallback
154 stdcall LdrSetDllManifestProber(ptr) ChpeAutoLdrSetDllManifestProber
155 stdcall -version=0x600+ LdrSetMUICacheType(long) ChpeStubLdrSetMUICacheType
156 stdcall LdrShutdownProcess() ChpeAutoLdrShutdownProcess
157 stdcall LdrShutdownThread() ChpeAutoLdrShutdownThread
158 extern LdrSystemDllInitBlock ntdll.LdrSystemDllInitBlock
159 stdcall LdrUnloadAlternateResourceModule(ptr) ChpeAutoLdrUnloadAlternateResourceModule
160 stdcall -version=0x600+ LdrUnloadAlternateResourceModuleEx(long long) ChpeStubLdrUnloadAlternateResourceModuleEx
162 stdcall LdrUnlockLoaderLock(long ptr) ChpeAutoLdrUnlockLoaderLock
163 stdcall -version=0x600+ LdrUnregisterDllNotification(ptr) ChpeAutoLdrUnregisterDllNotification
164 stdcall LdrVerifyImageMatchesChecksum(ptr long long long) ChpeAutoLdrVerifyImageMatchesChecksum
165 stdcall -version=0x600+ LdrVerifyImageMatchesChecksumEx(ptr ptr) ChpeStubLdrVerifyImageMatchesChecksumEx
166 stdcall -version=0x600+ LdrpResGetMappingSize(long ptr long long) ChpeStubLdrpResGetMappingSize
167 stdcall -version=0x600+ LdrpResGetRCConfig(long long ptr long long) ChpeStubLdrpResGetRCConfig
168 stdcall -version=0x600+ LdrpResGetResourceDirectory(long long long ptr ptr) ChpeStubLdrpResGetResourceDirectory
169 stdcall -version=0x600+ MD4Final(ptr) ChpeAutoMD4Final
170 stdcall -version=0x600+ MD4Init(ptr) ChpeAutoMD4Init
171 stdcall -version=0x600+ MD4Update(ptr ptr long) ChpeAutoMD4Update
172 stdcall -version=0x600+ MD5Final(ptr) ChpeAutoMD5Final
173 stdcall -version=0x600+ MD5Init(ptr) ChpeAutoMD5Init
174 stdcall -version=0x600+ MD5Update(ptr ptr long) ChpeAutoMD5Update
175 extern NlsAnsiCodePage ntdll.NlsAnsiCodePage
176 extern NlsMbCodePageTag ntdll.NlsMbCodePageTag
177 extern NlsMbOemCodePageTag ntdll.NlsMbOemCodePageTag
178 stdcall NtAcceptConnectPort(ptr long ptr long long ptr) ChpeAutoNtAcceptConnectPort
179 stdcall NtAccessCheck(ptr long long ptr ptr ptr ptr ptr) ChpeAutoNtAccessCheck
180 stdcall NtAccessCheckAndAuditAlarm(ptr long ptr ptr ptr long ptr long ptr ptr ptr) ChpeAutoNtAccessCheckAndAuditAlarm
181 stdcall NtAccessCheckByType(ptr ptr ptr long ptr long ptr ptr long ptr ptr) ChpeAutoNtAccessCheckByType
182 stdcall NtAccessCheckByTypeAndAuditAlarm(ptr ptr ptr ptr ptr ptr long long long ptr long ptr long ptr ptr ptr) ChpeAutoNtAccessCheckByTypeAndAuditAlarm
183 stdcall NtAccessCheckByTypeResultList(ptr ptr ptr long ptr long ptr ptr long ptr ptr) ChpeAutoNtAccessCheckByTypeResultList
184 stdcall NtAccessCheckByTypeResultListAndAuditAlarm(ptr ptr ptr ptr ptr ptr long long long ptr long ptr long ptr ptr ptr) ChpeAutoNtAccessCheckByTypeResultListAndAuditAlarm
185 stdcall NtAccessCheckByTypeResultListAndAuditAlarmByHandle(ptr ptr ptr ptr ptr ptr ptr long long long ptr long ptr long ptr ptr ptr) ChpeAutoNtAccessCheckByTypeResultListAndAuditAlarmByHandle
186 stdcall -version=0x600+ NtAcquireCMFViewOwnership(ptr ptr long) ChpeStubNtAcquireCMFViewOwnership
187 stdcall NtAddAtom(ptr long ptr) ChpeAutoNtAddAtom
188 stdcall NtAddBootEntry(ptr long) ChpeAutoNtAddBootEntry
189 stdcall NtAddDriverEntry(ptr long) ChpeAutoNtAddDriverEntry
190 stdcall NtAdjustGroupsToken(long long ptr long ptr ptr) ChpeAutoNtAdjustGroupsToken
192 stdcall -version=0xA00+ NtAlertMultipleThreadByThreadId(ptr long ptr ptr) ChpeAutoNtAlertMultipleThreadByThreadId
193 stdcall NtAlertResumeThread(long ptr) ChpeAutoNtAlertResumeThread
194 stdcall NtAlertThread(long) ChpeAutoNtAlertThread
196 stdcall NtAllocateLocallyUniqueId(ptr) ChpeAutoNtAllocateLocallyUniqueId
197 stdcall -version=0x600+ NtAllocateReserveObject(ptr ptr long) ChpeAutoNtAllocateReserveObject
198 stdcall NtAllocateUserPhysicalPages(ptr ptr ptr) ChpeAutoNtAllocateUserPhysicalPages
199 stdcall NtAllocateUuids(ptr ptr ptr ptr) ChpeAutoNtAllocateUuids
202 stdcall -version=0x600+ NtAlpcAcceptConnectPort(ptr ptr long ptr ptr ptr ptr ptr long) ChpeAutoNtAlpcAcceptConnectPort
203 stdcall -version=0x600+ NtAlpcCancelMessage(ptr long ptr) ChpeAutoNtAlpcCancelMessage
204 stdcall -version=0x600+ NtAlpcConnectPort(ptr ptr ptr ptr long ptr ptr ptr ptr ptr ptr) ChpeAutoNtAlpcConnectPort
205 stdcall -version=0x602+ NtAlpcConnectPortEx(ptr ptr ptr ptr long ptr ptr ptr ptr ptr ptr) ChpeAutoNtAlpcConnectPortEx
206 stdcall -version=0x600+ NtAlpcCreatePort(ptr ptr ptr) ChpeAutoNtAlpcCreatePort
207 stdcall -version=0x600+ NtAlpcCreatePortSection(ptr long ptr long ptr ptr) ChpeAutoNtAlpcCreatePortSection
208 stdcall -version=0x600+ NtAlpcCreateResourceReserve(ptr long long ptr) ChpeAutoNtAlpcCreateResourceReserve
209 stdcall -version=0x600+ NtAlpcCreateSectionView(ptr long ptr) ChpeAutoNtAlpcCreateSectionView
210 stdcall -version=0x600+ NtAlpcCreateSecurityContext(ptr long ptr) ChpeAutoNtAlpcCreateSecurityContext
211 stdcall -version=0x600+ NtAlpcDeletePortSection(ptr long ptr) ChpeAutoNtAlpcDeletePortSection
212 stdcall -version=0x600+ NtAlpcDeleteResourceReserve(ptr long long) ChpeAutoNtAlpcDeleteResourceReserve
213 stdcall -version=0x600+ NtAlpcDeleteSectionView(ptr long ptr) ChpeAutoNtAlpcDeleteSectionView
214 stdcall -version=0x600+ NtAlpcDeleteSecurityContext(ptr long ptr) ChpeAutoNtAlpcDeleteSecurityContext
215 stdcall -version=0x600+ NtAlpcDisconnectPort(ptr long) ChpeAutoNtAlpcDisconnectPort
216 stdcall -version=0x600+ NtAlpcImpersonateClientOfPort(ptr ptr ptr) ChpeAutoNtAlpcImpersonateClientOfPort
217 stdcall -version=0xA00+ NtAlpcImpersonateClientContainerOfPort(ptr ptr long) ChpeAutoNtAlpcImpersonateClientContainerOfPort
218 stdcall -version=0x600+ NtAlpcOpenSenderProcess(ptr ptr ptr long long ptr) ChpeAutoNtAlpcOpenSenderProcess
219 stdcall -version=0x600+ NtAlpcOpenSenderThread(ptr ptr ptr long long ptr) ChpeAutoNtAlpcOpenSenderThread
220 stdcall -version=0x600+ NtAlpcQueryInformation(ptr long ptr long ptr) ChpeAutoNtAlpcQueryInformation
221 stdcall -version=0x600+ NtAlpcQueryInformationMessage(ptr ptr long ptr long ptr) ChpeAutoNtAlpcQueryInformationMessage
222 stdcall -version=0x600+ NtAlpcRevokeSecurityContext(ptr long ptr) ChpeAutoNtAlpcRevokeSecurityContext
223 stdcall -version=0x600+ NtAlpcSendWaitReceivePort(ptr long ptr ptr ptr ptr ptr ptr) ChpeAutoNtAlpcSendWaitReceivePort
224 stdcall -version=0x600+ NtAlpcSetInformation(ptr long ptr long) ChpeAutoNtAlpcSetInformation
225 stdcall NtApphelpCacheControl(long ptr) ChpeAutoNtApphelpCacheControl
226 stdcall NtAreMappedFilesTheSame(ptr ptr) ChpeAutoNtAreMappedFilesTheSame
227 stdcall NtAssignProcessToJobObject(long long) ChpeAutoNtAssignProcessToJobObject
228 stdcall NtCallbackReturn(ptr long long) ChpeAutoNtCallbackReturn
229 stdcall NtCancelDeviceWakeupRequest(ptr) ChpeAutoNtCancelDeviceWakeupRequest
230 stdcall NtCancelIoFile(long ptr) ChpeAutoNtCancelIoFile
231 stdcall -version=0x600+ NtCancelIoFileEx(long ptr ptr) ChpeAutoNtCancelIoFileEx
232 stdcall -version=0x600+ NtCancelSynchronousIoFile(long ptr ptr) ChpeAutoNtCancelSynchronousIoFile
233 stdcall NtCancelTimer(long ptr) ChpeAutoNtCancelTimer
234 stdcall NtClearEvent(long) ChpeAutoNtClearEvent
236 stdcall NtCloseObjectAuditAlarm(ptr ptr long) ChpeAutoNtCloseObjectAuditAlarm
237 stdcall -version=0x600+ NtCompareObjects(ptr ptr) ChpeAutoNtCompareObjects
238 stdcall -version=0x600+ NtCommitComplete(ptr ptr) ChpeAutoNtCommitComplete
239 stdcall -version=0x600+ NtCommitEnlistment(ptr ptr) ChpeAutoNtCommitEnlistment
240 stdcall -version=0x600+ NtCommitTransaction(ptr long) ChpeAutoNtCommitTransaction
241 stdcall NtCompactKeys(long ptr) ChpeAutoNtCompactKeys
242 stdcall NtCompareTokens(ptr ptr ptr) ChpeAutoNtCompareTokens
243 stdcall NtCompleteConnectPort(ptr) ChpeAutoNtCompleteConnectPort
244 stdcall NtCompressKey(ptr) ChpeAutoNtCompressKey
245 stdcall NtConnectPort(ptr ptr ptr ptr ptr ptr ptr ptr) ChpeAutoNtConnectPort
248 stdcall -version=0xA00+ NtConvertBetweenAuxiliaryCounterAndPerformanceCounter(long ptr ptr ptr) ChpeAutoNtConvertBetweenAuxiliaryCounterAndPerformanceCounter
249 stdcall NtCreateDebugObject(ptr long ptr long) ChpeAutoNtCreateDebugObject
250 stdcall NtCreateDirectoryObject(long long long) ChpeAutoNtCreateDirectoryObject
251 stdcall -version=0x600+ NtCreateEnlistment(ptr long ptr ptr ptr long long ptr) ChpeAutoNtCreateEnlistment
252 stdcall NtCreateEvent(long long long long long) ChpeAutoNtCreateEvent
253 stdcall NtCreateEventPair(ptr long ptr) ChpeAutoNtCreateEventPair
255 stdcall NtCreateIoCompletion(ptr long ptr long) ChpeAutoNtCreateIoCompletion
259 stdcall NtCreateJobObject(ptr long ptr) ChpeAutoNtCreateJobObject
260 stdcall NtCreateJobSet(long ptr long) ChpeAutoNtCreateJobSet
262 stdcall -version=0x600+ NtCreateKeyTransacted(ptr long ptr long ptr long ptr ptr) ChpeAutoNtCreateKeyTransacted
263 stdcall NtCreateKeyedEvent(ptr long ptr long) ChpeAutoNtCreateKeyedEvent
264 stdcall NtCreateMailslotFile(long long long long long long long long) ChpeAutoNtCreateMailslotFile
265 stdcall NtCreateMutant(ptr long ptr long) ChpeAutoNtCreateMutant
267 stdcall NtCreatePagingFile(ptr ptr ptr long) ChpeAutoNtCreatePagingFile
268 stdcall NtCreatePort(ptr ptr long long ptr) ChpeAutoNtCreatePort
269 stdcall -version=0x600+ NtCreatePrivateNamespace(ptr long ptr ptr) ChpeAutoNtCreatePrivateNamespace
270 stdcall NtCreateProcess(ptr long ptr ptr long ptr ptr ptr) ChpeAutoNtCreateProcess
271 stdcall NtCreateProcessEx(ptr long ptr ptr long ptr ptr ptr long) ChpeAutoNtCreateProcessEx
272 stdcall NtCreateProfile(ptr ptr ptr long long ptr long long long) ChpeAutoNtCreateProfile
273 stdcall -version=0x600+ NtCreateResourceManager(ptr long ptr ptr ptr long ptr) ChpeAutoNtCreateResourceManager
274 stdcall NtCreateSection(ptr long ptr ptr long long ptr) ChpeAutoNtCreateSection
275 stdcall NtCreateSectionEx(ptr long ptr ptr long long ptr ptr long) ChpeAutoNtCreateSectionEx
276 stdcall NtCreateSemaphore(ptr long ptr long long) ChpeAutoNtCreateSemaphore
277 stdcall NtCreateSymbolicLinkObject(ptr long ptr ptr) ChpeAutoNtCreateSymbolicLinkObject
279 stdcall -version=0x600+ NtCreateThreadEx(ptr long ptr ptr ptr ptr long long long long ptr) ChpeAutoNtCreateThreadEx
280 stdcall NtCreateTimer(ptr long ptr long) ChpeAutoNtCreateTimer
281 stdcall NtCreateToken(ptr long ptr long ptr ptr ptr ptr ptr ptr ptr ptr ptr) ChpeAutoNtCreateToken
282 stdcall -version=0x600+ NtCreateTransaction(ptr long ptr ptr ptr long long long ptr ptr) ChpeAutoNtCreateTransaction
283 stdcall -version=0x600+ NtCreateTransactionManager(ptr long ptr ptr long long) ChpeAutoNtCreateTransactionManager
284 stdcall -version=0x600+ NtCreateUserProcess(ptr ptr long long ptr ptr long long ptr ptr ptr) ChpeAutoNtCreateUserProcess
285 stdcall NtCreateWaitablePort(ptr ptr long long long) ChpeAutoNtCreateWaitablePort
286 stdcall -version=0x602+ NtCreateWnfStateName(ptr long long long ptr long ptr) ChpeAutoNtCreateWnfStateName
287 stdcall -version=0x600+ NtCreateWorkerFactory(ptr long long long long long long long long long) ChpeStubNtCreateWorkerFactory
288 stdcall NtDebugActiveProcess(ptr ptr) ChpeAutoNtDebugActiveProcess
289 stdcall NtDebugContinue(ptr ptr long) ChpeAutoNtDebugContinue
290 stdcall NtDelayExecution(long ptr) ChpeAutoNtDelayExecution
291 stdcall NtDeleteAtom(long) ChpeAutoNtDeleteAtom
292 stdcall NtDeleteBootEntry(long) ChpeAutoNtDeleteBootEntry
293 stdcall NtDeleteDriverEntry(long) ChpeAutoNtDeleteDriverEntry
294 stdcall NtDeleteFile(ptr) ChpeAutoNtDeleteFile
296 stdcall NtDeleteObjectAuditAlarm(ptr ptr long) ChpeAutoNtDeleteObjectAuditAlarm
297 stdcall -version=0x600+ NtDeletePrivateNamespace(ptr) ChpeAutoNtDeletePrivateNamespace
299 stdcall -version=0x602+ NtDeleteWnfStateData(ptr ptr) ChpeAutoNtDeleteWnfStateData
300 stdcall -version=0x602+ NtDeleteWnfStateName(ptr) ChpeAutoNtDeleteWnfStateName
302 stdcall NtDisplayString(ptr) ChpeAutoNtDisplayString
303 stdcall NtDuplicateObject(long long long ptr long long long) ChpeAutoNtDuplicateObject
304 stdcall NtDuplicateToken(long long long long long long) ChpeAutoNtDuplicateToken
305 stdcall NtEnumerateBootEntries(ptr ptr) ChpeAutoNtEnumerateBootEntries
306 stdcall NtEnumerateDriverEntries(ptr ptr) ChpeAutoNtEnumerateDriverEntries
308 stdcall NtEnumerateSystemEnvironmentValuesEx(long ptr long) ChpeAutoNtEnumerateSystemEnvironmentValuesEx
309 stdcall -version=0x600+ NtEnumerateTransactionObject(ptr long ptr long ptr) ChpeAutoNtEnumerateTransactionObject
311 stdcall NtExtendSection(ptr ptr) ChpeAutoNtExtendSection
312 stdcall NtFilterToken(ptr long ptr ptr ptr ptr) ChpeAutoNtFilterToken
313 stdcall -version=0x602+ NtCreateLowBoxToken(ptr ptr long ptr ptr long ptr long ptr) ChpeAutoNtCreateLowBoxToken
314 stdcall NtFindAtom(ptr long ptr) ChpeAutoNtFindAtom
315 stdcall NtFlushBuffersFile(long ptr) ChpeAutoNtFlushBuffersFile
316 stdcall NtFlushBuffersFileEx(long long ptr long ptr) ChpeAutoNtFlushBuffersFileEx
317 stdcall -version=0x600+ NtFlushInstallUILanguage(long long) ChpeStubNtFlushInstallUILanguage
321 stdcall NtFlushVirtualMemory(ptr ptr ptr ptr) ChpeAutoNtFlushVirtualMemory
322 stdcall NtFlushWriteBuffer() ChpeAutoNtFlushWriteBuffer
323 stdcall NtFreeUserPhysicalPages(ptr ptr ptr) ChpeAutoNtFreeUserPhysicalPages
325 stdcall -version=0x600+ NtFreezeRegistry(long) ChpeStubNtFreezeRegistry
326 stdcall -version=0x600+ NtFreezeTransactions(ptr ptr) ChpeStubNtFreezeTransactions
329 stdcall NtGetCurrentProcessorNumber() ChpeAutoNtGetCurrentProcessorNumber
330 stdcall -version=0xA00+ NtGetCurrentProcessorNumberEx(ptr) ChpeAutoNtGetCurrentProcessorNumberEx
331 stdcall NtGetDevicePowerState(ptr ptr) ChpeAutoNtGetDevicePowerState
332 stdcall -version=0x600+ NtGetMUIRegistryInfo(long ptr ptr) ChpeStubNtGetMUIRegistryInfo
333 stdcall -version=0x600+ NtGetNextProcess(long long long long ptr) ChpeStubNtGetNextProcess
335 stdcall -version=0x600+ NtGetNlsSectionPtr(long long ptr ptr ptr) ChpeAutoNtGetNlsSectionPtr
336 stdcall -version=0x600+ NtGetNotificationResourceManager(ptr ptr long ptr ptr long ptr) ChpeAutoNtGetNotificationResourceManager
337 stdcall NtGetPlugPlayEvent(long long ptr long) ChpeAutoNtGetPlugPlayEvent
338 stdcall NtGetTickCount() ChpeAutoNtGetTickCount
339 stdcall NtGetWriteWatch(long long ptr long ptr ptr ptr) ChpeAutoNtGetWriteWatch
340 stdcall NtImpersonateAnonymousToken(ptr) ChpeAutoNtImpersonateAnonymousToken
341 stdcall NtImpersonateClientOfPort(ptr ptr) ChpeAutoNtImpersonateClientOfPort
342 stdcall NtImpersonateThread(ptr ptr ptr) ChpeAutoNtImpersonateThread
343 stdcall -version=0x600+ NtInitializeNlsFiles(ptr ptr ptr) ChpeAutoNtInitializeNlsFiles
344 stdcall NtInitializeRegistry(long) ChpeAutoNtInitializeRegistry
345 stdcall NtInitiatePowerAction(long long long long) ChpeAutoNtInitiatePowerAction
346 stdcall NtIsProcessInJob(long long) ChpeAutoNtIsProcessInJob
347 stdcall NtIsSystemResumeAutomatic() ChpeAutoNtIsSystemResumeAutomatic
348 stdcall -version=0x600+ NtIsUILanguageComitted() ChpeStubNtIsUILanguageComitted
349 stdcall NtListenPort(ptr ptr) ChpeAutoNtListenPort
350 stdcall NtLoadDriver(ptr) ChpeAutoNtLoadDriver
351 stdcall NtLoadKey2(ptr ptr long) ChpeAutoNtLoadKey2
352 stdcall NtLoadKey(ptr ptr) ChpeAutoNtLoadKey
353 stdcall NtLoadKeyEx(ptr ptr long ptr ptr long ptr ptr) ChpeAutoNtLoadKeyEx
354 stdcall NtLockFile(long long ptr ptr ptr ptr ptr ptr long long) ChpeAutoNtLockFile
355 stdcall NtLockProductActivationKeys(ptr ptr) ChpeAutoNtLockProductActivationKeys
356 stdcall NtLockRegistryKey(ptr) ChpeAutoNtLockRegistryKey
357 stdcall NtLockVirtualMemory(long ptr ptr long) ChpeAutoNtLockVirtualMemory
358 stdcall NtMakePermanentObject(ptr) ChpeAutoNtMakePermanentObject
359 stdcall NtMakeTemporaryObject(long) ChpeAutoNtMakeTemporaryObject
360 stdcall -version=0x600+ NtMapCMFModule(long long ptr ptr ptr ptr) ChpeStubNtMapCMFModule
361 stdcall NtMapUserPhysicalPages(ptr ptr ptr) ChpeAutoNtMapUserPhysicalPages
362 stdcall NtMapUserPhysicalPagesScatter(ptr ptr ptr) ChpeAutoNtMapUserPhysicalPagesScatter
365 stdcall NtModifyBootEntry(ptr) ChpeAutoNtModifyBootEntry
366 stdcall NtModifyDriverEntry(ptr) ChpeAutoNtModifyDriverEntry
367 stdcall NtNotifyChangeDirectoryFile(long long ptr ptr ptr ptr long long long) ChpeAutoNtNotifyChangeDirectoryFile
368 stdcall NtNotifyChangeDirectoryFileEx(long long ptr ptr ptr ptr long long long long) ChpeAutoNtNotifyChangeDirectoryFileEx
371 stdcall NtOpenDirectoryObject(long long long) ChpeAutoNtOpenDirectoryObject
372 stdcall -version=0x600+ NtOpenEnlistment(ptr long ptr ptr ptr) ChpeAutoNtOpenEnlistment
373 stdcall NtOpenEvent(long long long) ChpeAutoNtOpenEvent
374 stdcall NtOpenEventPair(ptr long ptr) ChpeAutoNtOpenEventPair
376 stdcall NtOpenIoCompletion(ptr long ptr) ChpeAutoNtOpenIoCompletion
377 stdcall NtOpenJobObject(ptr long ptr) ChpeAutoNtOpenJobObject
379 stdcall -version=0x600+ NtOpenKeyTransacted(ptr long ptr ptr) ChpeAutoNtOpenKeyTransacted
380 stdcall -version=0x600+ NtOpenKeyTransactedEx(ptr long ptr long ptr) ChpeAutoNtOpenKeyTransactedEx
381 stdcall NtOpenKeyedEvent(ptr long ptr) ChpeAutoNtOpenKeyedEvent
382 stdcall NtOpenMutant(ptr long ptr) ChpeAutoNtOpenMutant
383 stdcall NtOpenObjectAuditAlarm(ptr ptr ptr ptr ptr ptr long long ptr long long ptr) ChpeAutoNtOpenObjectAuditAlarm
384 stdcall -version=0x600+ NtOpenPrivateNamespace(ptr long ptr ptr) ChpeAutoNtOpenPrivateNamespace
385 stdcall NtOpenProcess(ptr long ptr ptr) ChpeAutoNtOpenProcess
386 stdcall NtOpenProcessToken(long long ptr) ChpeAutoNtOpenProcessToken
387 stdcall NtOpenProcessTokenEx(long long long ptr) ChpeAutoNtOpenProcessTokenEx
388 stdcall -version=0x600+ NtOpenResourceManager(ptr long ptr ptr ptr) ChpeAutoNtOpenResourceManager
389 stdcall NtOpenSection(ptr long ptr) ChpeAutoNtOpenSection
390 stdcall NtOpenSemaphore(long long ptr) ChpeAutoNtOpenSemaphore
391 stdcall -version=0x600+ NtOpenSession(ptr long ptr) ChpeStubNtOpenSession
392 stdcall NtOpenSymbolicLinkObject(ptr long ptr) ChpeAutoNtOpenSymbolicLinkObject
393 stdcall NtOpenThread(ptr long ptr ptr) ChpeAutoNtOpenThread
394 stdcall NtOpenThreadToken(long long long ptr) ChpeAutoNtOpenThreadToken
395 stdcall NtOpenThreadTokenEx(long long long long ptr) ChpeAutoNtOpenThreadTokenEx
396 stdcall NtOpenTimer(ptr long ptr) ChpeAutoNtOpenTimer
397 stdcall -version=0x600+ NtOpenTransaction(ptr long ptr ptr ptr) ChpeAutoNtOpenTransaction
398 stdcall -version=0x600+ NtOpenTransactionManager(ptr long ptr ptr ptr long) ChpeAutoNtOpenTransactionManager
399 stdcall NtPlugPlayControl(ptr ptr long) ChpeAutoNtPlugPlayControl
400 stdcall NtPowerInformation(long ptr long ptr long) ChpeAutoNtPowerInformation
401 stdcall -version=0x600+ NtPrePrepareComplete(ptr ptr) ChpeAutoNtPrePrepareComplete
402 stdcall -version=0x600+ NtPrePrepareEnlistment(ptr ptr) ChpeAutoNtPrePrepareEnlistment
403 stdcall -version=0x600+ NtPrepareComplete(ptr ptr) ChpeAutoNtPrepareComplete
404 stdcall -version=0x600+ NtPrepareEnlistment(ptr ptr) ChpeAutoNtPrepareEnlistment
405 stdcall NtPrivilegeCheck(ptr ptr ptr) ChpeAutoNtPrivilegeCheck
406 stdcall NtPrivilegeObjectAuditAlarm(ptr ptr ptr long ptr long) ChpeAutoNtPrivilegeObjectAuditAlarm
407 stdcall NtPrivilegedServiceAuditAlarm(ptr ptr ptr ptr long) ChpeAutoNtPrivilegedServiceAuditAlarm
408 stdcall -version=0x600+ NtPropagationComplete(ptr long long ptr) ChpeAutoNtPropagationComplete
409 stdcall -version=0x600+ NtPropagationFailed(ptr long long) ChpeAutoNtPropagationFailed
411 stdcall NtPulseEvent(long ptr) ChpeAutoNtPulseEvent
412 stdcall NtQueryAttributesFile(ptr ptr) ChpeAutoNtQueryAttributesFile
413 stdcall NtQueryBootEntryOrder(ptr ptr) ChpeAutoNtQueryBootEntryOrder
414 stdcall NtQueryBootOptions(ptr ptr) ChpeAutoNtQueryBootOptions
415 stdcall NtQueryDebugFilterState(long long) ChpeAutoNtQueryDebugFilterState
416 stdcall NtQueryDefaultLocale(long ptr) ChpeAutoNtQueryDefaultLocale
417 stdcall NtQueryDefaultUILanguage(ptr) ChpeAutoNtQueryDefaultUILanguage
419 stdcall NtQueryDirectoryFileEx(long long ptr ptr ptr ptr long long long ptr) ChpeAutoNtQueryDirectoryFileEx
420 stdcall NtQueryDirectoryObject(long ptr long long long ptr ptr) ChpeAutoNtQueryDirectoryObject
421 stdcall NtQueryDriverEntryOrder(ptr ptr) ChpeAutoNtQueryDriverEntryOrder
422 stdcall NtQueryEaFile(long ptr ptr long long ptr long ptr long) ChpeAutoNtQueryEaFile
423 stdcall NtQueryEvent(long long ptr long ptr) ChpeAutoNtQueryEvent
424 stdcall NtQueryFullAttributesFile(ptr ptr) ChpeAutoNtQueryFullAttributesFile
425 stdcall NtQueryInformationAtom(long long ptr long ptr) ChpeAutoNtQueryInformationAtom
426 stdcall NtQueryInformationByName(ptr ptr ptr long long) ChpeAutoNtQueryInformationByName
427 stdcall -version=0x600+ NtQueryInformationEnlistment(ptr long ptr long ptr) ChpeAutoNtQueryInformationEnlistment
429 stdcall NtQueryInformationJobObject(ptr long ptr long ptr) ChpeAutoNtQueryInformationJobObject
430 stdcall NtQueryInformationPort(ptr long ptr long ptr) ChpeAutoNtQueryInformationPort
432 stdcall -version=0x600+ NtQueryInformationResourceManager(ptr long ptr long ptr) ChpeAutoNtQueryInformationResourceManager
434 stdcall NtQueryInformationToken(ptr long ptr long ptr) ChpeAutoNtQueryInformationToken
435 stdcall -version=0x600+ NtQueryInformationTransaction(ptr long ptr long ptr) ChpeAutoNtQueryInformationTransaction
436 stdcall -version=0x600+ NtQueryInformationTransactionManager(ptr long ptr long ptr) ChpeAutoNtQueryInformationTransactionManager
437 stdcall -version=0x600+ NtQueryInformationWorkerFactory(ptr long ptr long ptr) ChpeStubNtQueryInformationWorkerFactory
438 stdcall NtQueryInstallUILanguage(ptr) ChpeAutoNtQueryInstallUILanguage
439 stdcall NtQueryIntervalProfile(long ptr) ChpeAutoNtQueryIntervalProfile
440 stdcall NtQueryIoCompletion(long long ptr long ptr) ChpeAutoNtQueryIoCompletion
442 stdcall -version=0x600+ NtQueryLicenseValue(ptr ptr ptr long ptr) ChpeAutoNtQueryLicenseValue
443 stdcall NtQueryMultipleValueKey(long ptr long ptr long ptr) ChpeAutoNtQueryMultipleValueKey
444 stdcall NtQueryMutant(long long ptr long ptr) ChpeAutoNtQueryMutant
446 stdcall NtQueryOpenSubKeys(ptr ptr) ChpeAutoNtQueryOpenSubKeys
447 stdcall NtQueryOpenSubKeysEx(ptr long ptr ptr) ChpeAutoNtQueryOpenSubKeysEx
448 stdcall NtQueryPerformanceCounter(ptr ptr) ChpeAutoNtQueryPerformanceCounter
449 stdcall NtQueryPortInformationProcess() ChpeAutoNtQueryPortInformationProcess
450 stdcall NtQueryQuotaInformationFile(ptr ptr ptr long long ptr long ptr long) ChpeAutoNtQueryQuotaInformationFile
451 stdcall NtQuerySection(long long long long long) ChpeAutoNtQuerySection
452 stdcall NtQuerySecurityObject(long long long long long) ChpeAutoNtQuerySecurityObject
453 stdcall NtQuerySemaphore(long long ptr long ptr) ChpeAutoNtQuerySemaphore
454 stdcall NtQuerySymbolicLinkObject(long ptr ptr) ChpeAutoNtQuerySymbolicLinkObject
455 stdcall NtQuerySystemEnvironmentValue(ptr ptr long ptr) ChpeAutoNtQuerySystemEnvironmentValue
456 stdcall NtQuerySystemEnvironmentValueEx(ptr ptr ptr ptr ptr) ChpeAutoNtQuerySystemEnvironmentValueEx
458 stdcall -version=0x601+ NtQuerySystemInformationEx(long ptr long ptr long ptr) ChpeAutoNtQuerySystemInformationEx
459 stdcall NtQuerySystemTime(ptr) ChpeAutoNtQuerySystemTime
460 stdcall NtQueryTimer(ptr long ptr long ptr) ChpeAutoNtQueryTimer
461 stdcall NtQueryTimerResolution(long long long) ChpeAutoNtQueryTimerResolution
465 stdcall -version=0x602+ NtQueryWnfStateData(ptr ptr ptr ptr ptr ptr) ChpeAutoNtQueryWnfStateData
466 stdcall -version=0x602+ NtQueryWnfStateNameInformation(ptr long ptr ptr long) ChpeAutoNtQueryWnfStateNameInformation
467 stdcall NtQueueApcThread(long ptr long long long) ChpeAutoNtQueueApcThread
468 stdcall -version=0x601+ NtQueueApcThreadEx(long long ptr long long long) ChpeAutoNtQueueApcThreadEx
469 stdcall -version=0xA00+ NtQueueApcThreadEx2(long long long ptr long long long) ChpeAutoNtQueueApcThreadEx2
471 stdcall NtRaiseHardError(long long long ptr long ptr) ChpeAutoNtRaiseHardError
473 stdcall NtReadFileScatter(long long ptr ptr ptr ptr long ptr ptr) ChpeAutoNtReadFileScatter
474 stdcall -version=0x600+ NtReadOnlyEnlistment(ptr ptr) ChpeAutoNtReadOnlyEnlistment
475 stdcall NtReadRequestData(ptr ptr long ptr long ptr) ChpeAutoNtReadRequestData
477 stdcall -version=0x600+ NtRecoverEnlistment(ptr ptr) ChpeAutoNtRecoverEnlistment
478 stdcall -version=0x600+ NtRecoverResourceManager(ptr) ChpeAutoNtRecoverResourceManager
479 stdcall -version=0x600+ NtRecoverTransactionManager(ptr) ChpeAutoNtRecoverTransactionManager
480 stdcall -version=0x600+ NtRegisterProtocolAddressInformation(ptr ptr long ptr long) ChpeAutoNtRegisterProtocolAddressInformation
481 stdcall NtRegisterThreadTerminatePort(ptr) ChpeAutoNtRegisterThreadTerminatePort
482 stdcall -version=0x600+ NtReleaseCMFViewOwnership() ChpeStubNtReleaseCMFViewOwnership
483 stdcall NtReleaseKeyedEvent(ptr ptr long ptr) ChpeAutoNtReleaseKeyedEvent
484 stdcall NtReleaseMutant(long ptr) ChpeAutoNtReleaseMutant
485 stdcall NtReleaseSemaphore(long long ptr) ChpeAutoNtReleaseSemaphore
486 stdcall -version=0x600+ NtReleaseWorkerFactoryWorker(ptr) ChpeStubNtReleaseWorkerFactoryWorker
487 stdcall NtRemoveIoCompletion(ptr ptr ptr ptr ptr) ChpeAutoNtRemoveIoCompletion
488 stdcall -version=0x600+ NtRemoveIoCompletionEx(ptr ptr long ptr ptr long) ChpeAutoNtRemoveIoCompletionEx
489 stdcall NtRemoveProcessDebug(ptr ptr) ChpeAutoNtRemoveProcessDebug
490 stdcall NtRenameKey(ptr ptr) ChpeAutoNtRenameKey
491 stdcall -version=0x600+ NtRenameTransactionManager(ptr ptr) ChpeAutoNtRenameTransactionManager
492 stdcall NtReplaceKey(ptr long ptr) ChpeAutoNtReplaceKey
493 stdcall -version=0x600+ NtReplacePartitionUnit(wstr wstr long) ChpeStubNtReplacePartitionUnit
494 stdcall NtReplyPort(ptr ptr) ChpeAutoNtReplyPort
495 stdcall NtReplyWaitReceivePort(ptr ptr ptr ptr) ChpeAutoNtReplyWaitReceivePort
496 stdcall NtReplyWaitReceivePortEx(ptr ptr ptr ptr ptr) ChpeAutoNtReplyWaitReceivePortEx
497 stdcall NtReplyWaitReplyPort(ptr ptr) ChpeAutoNtReplyWaitReplyPort
498 stdcall NtRequestDeviceWakeup(ptr) ChpeAutoNtRequestDeviceWakeup
499 stdcall NtRequestPort(ptr ptr) ChpeAutoNtRequestPort
500 stdcall NtRequestWaitReplyPort(ptr ptr ptr) ChpeAutoNtRequestWaitReplyPort
501 stdcall NtRequestWakeupLatency(long) ChpeAutoNtRequestWakeupLatency
502 stdcall NtResetEvent(long ptr) ChpeAutoNtResetEvent
503 stdcall NtResetWriteWatch(long ptr long) ChpeAutoNtResetWriteWatch
504 stdcall NtRestoreKey(long long long) ChpeAutoNtRestoreKey
505 stdcall NtResumeProcess(ptr) ChpeAutoNtResumeProcess
507 stdcall -version=0x600+ NtRollbackComplete(ptr ptr) ChpeAutoNtRollbackComplete
508 stdcall -version=0x600+ NtRollbackEnlistment(ptr ptr) ChpeAutoNtRollbackEnlistment
509 stdcall -version=0x600+ NtRollbackTransaction(ptr long) ChpeAutoNtRollbackTransaction
510 stdcall -version=0x600+ NtRollforwardTransactionManager(ptr ptr) ChpeAutoNtRollforwardTransactionManager
511 stdcall NtSaveKey(long long) ChpeAutoNtSaveKey
512 stdcall NtSaveKeyEx(ptr ptr long) ChpeAutoNtSaveKeyEx
513 stdcall NtSaveMergedKeys(ptr ptr ptr) ChpeAutoNtSaveMergedKeys
514 stdcall NtSecureConnectPort(ptr ptr ptr ptr ptr ptr ptr ptr ptr) ChpeAutoNtSecureConnectPort
515 stdcall NtSetBootEntryOrder(ptr ptr) ChpeAutoNtSetBootEntryOrder
516 stdcall NtSetBootOptions(ptr long) ChpeAutoNtSetBootOptions
518 stdcall NtSetDebugFilterState(long long long) ChpeAutoNtSetDebugFilterState
519 stdcall NtSetDefaultHardErrorPort(ptr) ChpeAutoNtSetDefaultHardErrorPort
520 stdcall NtSetDefaultLocale(long long) ChpeAutoNtSetDefaultLocale
521 stdcall NtSetDefaultUILanguage(long) ChpeAutoNtSetDefaultUILanguage
522 stdcall NtSetDriverEntryOrder(ptr ptr) ChpeAutoNtSetDriverEntryOrder
523 stdcall NtSetEaFile(long ptr ptr long) ChpeAutoNtSetEaFile
524 stdcall NtSetEvent(long long) ChpeAutoNtSetEvent
525 stdcall NtSetEventBoostPriority(ptr) ChpeAutoNtSetEventBoostPriority
526 stdcall NtSetHighEventPair(ptr) ChpeAutoNtSetHighEventPair
527 stdcall NtSetHighWaitLowEventPair(ptr) ChpeAutoNtSetHighWaitLowEventPair
528 stdcall NtSetInformationDebugObject(ptr long ptr long ptr) ChpeAutoNtSetInformationDebugObject
529 stdcall -version=0x600+ NtSetInformationEnlistment(ptr long ptr long) ChpeAutoNtSetInformationEnlistment
530 stdcall NtSetInformationFile(ptr ptr ptr long long) ChpeAutoNtSetInformationFile
531 stdcall NtSetInformationJobObject(ptr long ptr long) ChpeAutoNtSetInformationJobObject
532 stdcall NtSetInformationKey(ptr long ptr long) ChpeAutoNtSetInformationKey
533 stdcall NtSetInformationObject(ptr long ptr long) ChpeAutoNtSetInformationObject
534 stdcall NtSetInformationProcess(ptr long ptr long) ChpeAutoNtSetInformationProcess
535 stdcall -version=0x600+ NtSetInformationResourceManager(ptr long ptr long) ChpeAutoNtSetInformationResourceManager
536 stdcall NtSetInformationThread(ptr long ptr long) ChpeAutoNtSetInformationThread
537 stdcall NtSetInformationVirtualMemory(ptr long ptr ptr ptr long) ChpeAutoNtSetInformationVirtualMemory
538 stdcall NtSetInformationToken(ptr long ptr long) ChpeAutoNtSetInformationToken
539 stdcall -version=0x600+ NtSetInformationTransaction(ptr long ptr long) ChpeAutoNtSetInformationTransaction
540 stdcall -version=0x600+ NtSetInformationTransactionManager(ptr long ptr long) ChpeAutoNtSetInformationTransactionManager
541 stdcall -version=0x600+ NtSetInformationWorkerFactory(ptr long ptr long) ChpeStubNtSetInformationWorkerFactory
542 stdcall NtSetIntervalProfile(long long) ChpeAutoNtSetIntervalProfile
543 stdcall NtSetIoCompletion(ptr long ptr long long) ChpeAutoNtSetIoCompletion
544 stdcall -version=0x600+ NtSetIoCompletionEx(ptr ptr long long long long) ChpeAutoNtSetIoCompletionEx
545 stdcall NtSetLdtEntries(long int64 long int64) ChpeAutoNtSetLdtEntries
546 stdcall NtSetLowEventPair(ptr) ChpeAutoNtSetLowEventPair
547 stdcall NtSetLowWaitHighEventPair(ptr) ChpeAutoNtSetLowWaitHighEventPair
548 stdcall NtSetQuotaInformationFile(ptr ptr ptr long) ChpeAutoNtSetQuotaInformationFile
549 stdcall NtSetSecurityObject(long long ptr) ChpeAutoNtSetSecurityObject
550 stdcall NtSetSystemEnvironmentValue(ptr ptr) ChpeAutoNtSetSystemEnvironmentValue
551 stdcall NtSetSystemEnvironmentValueEx(ptr ptr ptr ptr ptr) ChpeAutoNtSetSystemEnvironmentValueEx
552 stdcall NtSetSystemInformation(long ptr long) ChpeAutoNtSetSystemInformation
553 stdcall NtSetSystemPowerState(long long long) ChpeAutoNtSetSystemPowerState
554 stdcall NtSetSystemTime(ptr ptr) ChpeAutoNtSetSystemTime
555 stdcall NtSetThreadExecutionState(long ptr) ChpeAutoNtSetThreadExecutionState
557 stdcall NtSetTimerResolution(long long ptr) ChpeAutoNtSetTimerResolution
558 stdcall NtSetUuidSeed(ptr) ChpeAutoNtSetUuidSeed
560 stdcall NtSetVolumeInformationFile(long ptr ptr long long) ChpeAutoNtSetVolumeInformationFile
561 stdcall NtShutdownSystem(long) ChpeAutoNtShutdownSystem
562 stdcall -version=0x600+ NtShutdownWorkerFactory(ptr ptr) ChpeStubNtShutdownWorkerFactory
563 stdcall NtSignalAndWaitForSingleObject(long long long ptr) ChpeAutoNtSignalAndWaitForSingleObject
564 stdcall -version=0x600+ NtSinglePhaseReject(ptr ptr) ChpeAutoNtSinglePhaseReject
565 stdcall NtStartProfile(ptr) ChpeAutoNtStartProfile
566 stdcall NtStopProfile(ptr) ChpeAutoNtStopProfile
567 stdcall -version=0x602+ NtSubscribeWnfStateChange(ptr long long ptr) ChpeAutoNtSubscribeWnfStateChange
570 stdcall NtSystemDebugControl(long ptr long ptr long ptr) ChpeAutoNtSystemDebugControl
571 stdcall NtTerminateJobObject(ptr long) ChpeAutoNtTerminateJobObject
574 stdcall NtTestAlert() ChpeAutoNtTestAlert
575 stdcall -version=0x600+ NtThawRegistry() ChpeStubNtThawRegistry
576 stdcall -version=0x600+ NtThawTransactions() ChpeStubNtThawTransactions
577 stdcall -version=0x600+ NtTraceControl(long ptr long ptr long ptr) ChpeAutoNtTraceControl
578 stdcall NtTraceEvent(ptr long long ptr) ChpeAutoNtTraceEvent
579 stdcall NtTranslateFilePath(ptr long ptr long) ChpeAutoNtTranslateFilePath
580 stdcall NtUnloadDriver(ptr) ChpeAutoNtUnloadDriver
581 stdcall NtUnloadKey2(ptr long) ChpeAutoNtUnloadKey2
582 stdcall NtUnloadKey(long) ChpeAutoNtUnloadKey
583 stdcall NtUnloadKeyEx(ptr ptr) ChpeAutoNtUnloadKeyEx
584 stdcall NtUnlockFile(long ptr ptr ptr ptr) ChpeAutoNtUnlockFile
585 stdcall NtUnlockVirtualMemory(long ptr ptr long) ChpeAutoNtUnlockVirtualMemory
588 stdcall -version=0x602+ NtUnsubscribeWnfStateChange(ptr) ChpeAutoNtUnsubscribeWnfStateChange
589 stdcall -version=0x602+ NtUpdateWnfStateData(ptr ptr long ptr ptr long long) ChpeAutoNtUpdateWnfStateData
590 stdcall NtVdmControl(long ptr) ChpeAutoNtVdmControl
591 stdcall NtWaitForDebugEvent(ptr long ptr ptr) ChpeAutoNtWaitForDebugEvent
592 stdcall NtWaitForKeyedEvent(ptr ptr long ptr) ChpeAutoNtWaitForKeyedEvent
594 stdcall NtWaitForMultipleObjects32(long ptr long long ptr) ChpeAutoNtWaitForMultipleObjects32
595 stdcall NtWaitForMultipleObjects(long ptr long long ptr) ChpeAutoNtWaitForMultipleObjects
596 stdcall NtWaitForSingleObject(long long long) ChpeAutoNtWaitForSingleObject
597 stub -version=0x600+ NtWaitForWorkViaWorkerFactory
598 stdcall NtWaitHighEventPair(ptr) ChpeAutoNtWaitHighEventPair
599 stdcall NtWaitLowEventPair(ptr) ChpeAutoNtWaitLowEventPair
600 stdcall -version=0x600+ NtWorkerFactoryWorkerReady(long) ChpeStubNtWorkerFactoryWorkerReady
602 stdcall NtWriteFileGather(long long ptr ptr ptr ptr long ptr ptr) ChpeAutoNtWriteFileGather
603 stdcall NtWriteRequestData(ptr ptr long ptr long ptr) ChpeAutoNtWriteRequestData
605 stdcall NtYieldExecution() ChpeAutoNtYieldExecution
606 stdcall -version=0x600+ NtdllDefWindowProc_A(long long long long) ChpeStubNtdllDefWindowProc_A
607 stdcall -version=0x600+ NtdllDefWindowProc_W(long long long long) ChpeStubNtdllDefWindowProc_W
608 stdcall -version=0x600+ NtdllDialogWndProc_A(long long long long) ChpeStubNtdllDialogWndProc_A
609 stdcall -version=0x600+ NtdllDialogWndProc_W(long long long long) ChpeStubNtdllDialogWndProc_W
610 stdcall PfxFindPrefix(ptr ptr) ChpeAutoPfxFindPrefix
611 stdcall PfxInitialize(ptr) ChpeAutoPfxInitialize
612 stdcall PfxInsertPrefix(ptr ptr ptr) ChpeAutoPfxInsertPrefix
613 stdcall PfxRemovePrefix(ptr ptr) ChpeAutoPfxRemovePrefix
614 stdcall RtlAbortRXact(ptr) ChpeAutoRtlAbortRXact
615 stdcall RtlAbsoluteToSelfRelativeSD(ptr ptr ptr) ChpeAutoRtlAbsoluteToSelfRelativeSD
616 stdcall RtlAcquirePebLock() ChpeAutoRtlAcquirePebLock
618 stdcall RtlAcquireResourceExclusive(ptr long) ChpeAutoRtlAcquireResourceExclusive
619 stdcall RtlAcquireResourceShared(ptr long) ChpeAutoRtlAcquireResourceShared
622 stdcall RtlActivateActivationContext(long ptr ptr) ChpeAutoRtlActivateActivationContext
623 stdcall RtlActivateActivationContextEx(long ptr ptr ptr) ChpeAutoRtlActivateActivationContextEx
624 stdcall RtlActivateActivationContextUnsafeFast(ptr ptr) ChpeAutoRtlActivateActivationContextUnsafeFast
625 stdcall RtlAddAccessAllowedAce(ptr long long ptr) ChpeAutoRtlAddAccessAllowedAce
626 stdcall RtlAddAccessAllowedAceEx(ptr long long long ptr) ChpeAutoRtlAddAccessAllowedAceEx
627 stdcall RtlAddAccessAllowedObjectAce(ptr long long long ptr ptr ptr) ChpeAutoRtlAddAccessAllowedObjectAce
628 stdcall RtlAddAccessDeniedAce(ptr long long ptr) ChpeAutoRtlAddAccessDeniedAce
629 stdcall RtlAddAccessDeniedAceEx(ptr long long long ptr) ChpeAutoRtlAddAccessDeniedAceEx
630 stdcall RtlAddAccessDeniedObjectAce(ptr long long long ptr ptr ptr) ChpeAutoRtlAddAccessDeniedObjectAce
631 stdcall RtlAddAce(ptr long long ptr long) ChpeAutoRtlAddAce
632 stdcall RtlAddActionToRXact(ptr long ptr long ptr long) ChpeAutoRtlAddActionToRXact
633 stdcall RtlAddAtomToAtomTable(ptr wstr ptr) ChpeAutoRtlAddAtomToAtomTable
634 stdcall RtlAddAttributeActionToRXact(ptr long ptr ptr ptr long ptr long) ChpeAutoRtlAddAttributeActionToRXact
635 stdcall RtlAddAuditAccessAce(ptr long long ptr long long) ChpeAutoRtlAddAuditAccessAce
636 stdcall RtlAddAuditAccessAceEx(ptr long long long ptr long long) ChpeAutoRtlAddAuditAccessAceEx
637 stdcall RtlAddAuditAccessObjectAce(ptr long long long ptr ptr ptr long long) ChpeAutoRtlAddAuditAccessObjectAce
638 stdcall  RtlAddCompoundAce(ptr long long long ptr ptr) ChpeStubRtlAddCompoundAce
641 stdcall -version=0x600+ RtlAddMandatoryAce(ptr long long long long ptr) ChpeAutoRtlAddMandatoryAce
642 stdcall RtlAddRefActivationContext(ptr) ChpeAutoRtlAddRefActivationContext
643 stdcall RtlAddRefMemoryStream(ptr) ChpeAutoRtlAddRefMemoryStream
644 stdcall -version=0x600+ RtlAddSIDToBoundaryDescriptor(ptr ptr) ChpeAutoRtlAddSIDToBoundaryDescriptor
645 stdcall -version=0x600+ RtlAddIntegrityLabelToBoundaryDescriptor(ptr ptr) ChpeAutoRtlAddIntegrityLabelToBoundaryDescriptor
648 stdcall  RtlAddressInSectionTable(ptr ptr long) ChpeStubRtlAddressInSectionTable
649 stdcall RtlAdjustPrivilege(long long long ptr) ChpeAutoRtlAdjustPrivilege
650 stdcall RtlAllocateActivationContextStack(ptr) ChpeAutoRtlAllocateActivationContextStack
651 stdcall RtlAllocateAndInitializeSid(ptr long long long long long long long long long ptr) ChpeAutoRtlAllocateAndInitializeSid
652 stdcall RtlAllocateHandle(ptr ptr) ChpeAutoRtlAllocateHandle
654 stdcall -version=0x600+ RtlAllocateMemoryBlockLookaside(ptr long ptr) ChpeStubRtlAllocateMemoryBlockLookaside
655 stdcall -version=0x600+ RtlAllocateMemoryZone(long long ptr) ChpeStubRtlAllocateMemoryZone
656 stdcall RtlAnsiCharToUnicodeChar(ptr) ChpeAutoRtlAnsiCharToUnicodeChar
657 stdcall RtlAnsiStringToUnicodeSize(ptr) ChpeAutoRtlAnsiStringToUnicodeSize
658 stdcall RtlAnsiStringToUnicodeString(ptr ptr long) ChpeAutoRtlAnsiStringToUnicodeString
659 stdcall RtlAppendAsciizToString(ptr str) ChpeAutoRtlAppendAsciizToString
660 stdcall  RtlAppendPathElement(ptr ptr ptr) ChpeStubRtlAppendPathElement
661 stdcall RtlAppendStringToString(ptr ptr) ChpeAutoRtlAppendStringToString
662 stdcall RtlAppendUnicodeStringToString(ptr ptr) ChpeAutoRtlAppendUnicodeStringToString
663 stdcall RtlAppendUnicodeToString(ptr wstr) ChpeAutoRtlAppendUnicodeToString
664 stdcall RtlApplicationVerifierStop(ptr str ptr str ptr str ptr str ptr str) ChpeAutoRtlApplicationVerifierStop
665 stdcall RtlApplyRXact(ptr) ChpeAutoRtlApplyRXact
666 stdcall RtlApplyRXactNoFlush(ptr) ChpeAutoRtlApplyRXactNoFlush
667 stdcall RtlAreAllAccessesGranted(long long) ChpeAutoRtlAreAllAccessesGranted
668 stdcall RtlAreAnyAccessesGranted(long long) ChpeAutoRtlAreAnyAccessesGranted
669 stdcall RtlAreBitsClear(ptr long long) ChpeAutoRtlAreBitsClear
670 stdcall RtlAreBitsSet(ptr long long) ChpeAutoRtlAreBitsSet
671 stdcall RtlAssert(ptr ptr long ptr) ChpeAutoRtlAssert
672 stdcall -version=0x600+ RtlBarrier(ptr long) ChpeStubRtlBarrier
673 stdcall -version=0x600+ RtlBarrierForDelete(long long) ChpeStubRtlBarrierForDelete
674 stdcall RtlCancelTimer(ptr ptr) ChpeAutoRtlCancelTimer
677 stdcall RtlCharToInteger(ptr long ptr) ChpeAutoRtlCharToInteger
678 stdcall RtlCheckForOrphanedCriticalSections(ptr) ChpeAutoRtlCheckForOrphanedCriticalSections
679 stdcall -version=0x602+ RtlCheckPortableOperatingSystem(ptr) ChpeAutoRtlCheckPortableOperatingSystem
680 stdcall RtlCheckRegistryKey(long ptr) ChpeAutoRtlCheckRegistryKey
681 stdcall -version=0x600+ RtlCleanUpTEBLangLists() ChpeStubRtlCleanUpTEBLangLists
682 stdcall RtlClearAllBits(ptr) ChpeAutoRtlClearAllBits
683 stdcall RtlClearBits(ptr long long) ChpeAutoRtlClearBits
684 stdcall RtlCloneMemoryStream(ptr ptr) ChpeAutoRtlCloneMemoryStream
685 stdcall -version=0x600+ RtlCloneUserProcess(long long long long long) ChpeStubRtlCloneUserProcess
686 stdcall -version=0x600+ RtlCmDecodeMemIoResource(ptr ptr) ChpeStubRtlCmDecodeMemIoResource
687 stdcall -version=0x600+ RtlCmEncodeMemIoResource(ptr long long long) ChpeStubRtlCmEncodeMemIoResource
688 stdcall -version=0x600+ RtlCommitDebugInfo(ptr long) ChpeStubRtlCommitDebugInfo
689 stdcall RtlCommitMemoryStream(ptr long) ChpeAutoRtlCommitMemoryStream
690 stdcall RtlCompactHeap(long long) ChpeAutoRtlCompactHeap
691 stdcall -version=0x600+ RtlCompareAltitudes(wstr wstr) ChpeStubRtlCompareAltitudes
693 stdcall RtlCompareMemoryUlong(ptr long long) ChpeAutoRtlCompareMemoryUlong
694 stdcall RtlCompareString(ptr ptr long) ChpeAutoRtlCompareString
696 stdcall -version=0x600+ RtlCompareUnicodeStrings(wstr long wstr long long) ChpeAutoRtlCompareUnicodeStrings
697 stdcall -version=0x600+ RtlCompleteProcessCloning(long) ChpeStubRtlCompleteProcessCloning
698 stdcall RtlCompressBuffer(long ptr long ptr long long ptr ptr) ChpeAutoRtlCompressBuffer
699 stdcall RtlComputeCrc32(long ptr long) ChpeAutoRtlComputeCrc32
700 stdcall RtlComputeImportTableHash(ptr ptr long) ChpeAutoRtlComputeImportTableHash
701 stdcall RtlComputePrivatizedDllName_U(ptr ptr ptr) ChpeAutoRtlComputePrivatizedDllName_U
702 stdcall -version=0x600+ RtlConnectToSm(ptr ptr long ptr) ChpeStubRtlConnectToSm
703 stdcall RtlConsoleMultiByteToUnicodeN(ptr long ptr ptr long ptr) ChpeAutoRtlConsoleMultiByteToUnicodeN
704 stdcall RtlConvertExclusiveToShared(ptr) ChpeAutoRtlConvertExclusiveToShared
705 stdcall -version=0x600+ RtlConvertLCIDToString(long long long ptr long) ChpeAutoRtlConvertLCIDToString
706 stdcall RtlConvertSharedToExclusive(ptr) ChpeAutoRtlConvertSharedToExclusive
707 stdcall RtlConvertSidToUnicodeString(ptr ptr long) ChpeAutoRtlConvertSidToUnicodeString
708 stdcall RtlConvertToAutoInheritSecurityObject(ptr ptr ptr ptr long ptr) ChpeAutoRtlConvertToAutoInheritSecurityObject
709 stdcall RtlConvertUiListToApiList(ptr ptr long) ChpeAutoRtlConvertUiListToApiList
710 stdcall RtlCopyLuid(ptr ptr) ChpeAutoRtlCopyLuid
711 stdcall RtlCopyLuidAndAttributesArray(long ptr ptr) ChpeAutoRtlCopyLuidAndAttributesArray
712 stdcall RtlCopyMappedMemory(ptr ptr long) ChpeAutoRtlCopyMappedMemory
713 stdcall -version=0xA00+ RtlCopyContext(ptr long ptr) ChpeAutoRtlCopyContext
714 stdcall -version=0x600+ RtlCopyExtendedContext(ptr long ptr) ChpeAutoRtlCopyExtendedContext
715 cdecl -version=0x600+ RtlCopyMemory(ptr ptr long) ChpeAutoRtlCopyMemory
716 stdcall -version=0x600+ RtlCopyMemoryNonTemporal(ptr ptr long) ChpeStubRtlCopyMemoryNonTemporal
717 stdcall RtlCopyMemoryStreamTo(ptr ptr int64 ptr ptr) ChpeAutoRtlCopyMemoryStreamTo
718 stdcall RtlCopyOutOfProcessMemoryStreamTo(ptr ptr int64 ptr ptr) ChpeAutoRtlCopyOutOfProcessMemoryStreamTo
719 stdcall RtlCopySecurityDescriptor(ptr ptr) ChpeAutoRtlCopySecurityDescriptor
720 stdcall RtlCopySid(long ptr ptr) ChpeAutoRtlCopySid
721 stdcall RtlCopySidAndAttributesArray(long ptr long ptr ptr ptr ptr) ChpeAutoRtlCopySidAndAttributesArray
722 stdcall RtlCopyString(ptr ptr) ChpeAutoRtlCopyString
724 stdcall RtlCreateAcl(ptr long long) ChpeAutoRtlCreateAcl
725 stdcall RtlCreateActivationContext(long ptr long ptr ptr ptr) ChpeAutoRtlCreateActivationContext
726 stdcall RtlCreateAndSetSD(ptr long ptr ptr ptr) ChpeAutoRtlCreateAndSetSD
727 stdcall RtlContractHashTable(ptr) ChpeAutoRtlContractHashTable
728 stdcall RtlCreateAtomTable(long ptr) ChpeAutoRtlCreateAtomTable
729 stdcall RtlCreateHashTable(ptr long long) ChpeAutoRtlCreateHashTable
730 stdcall RtlCreateBootStatusDataFile() ChpeAutoRtlCreateBootStatusDataFile
731 stdcall -version=0x600+ RtlCreateBoundaryDescriptor(ptr long) ChpeAutoRtlCreateBoundaryDescriptor
732 stdcall RtlCreateEnvironment(long ptr) ChpeAutoRtlCreateEnvironment
733 stdcall -version=0x600+ RtlCreateEnvironmentEx(ptr ptr long) ChpeStubRtlCreateEnvironmentEx
734 stdcall RtlCreateHeap(long ptr long long ptr ptr) ChpeAutoRtlCreateHeap
735 stdcall -version=0x600+ RtlCreateMemoryBlockLookaside(ptr long long long long) ChpeStubRtlCreateMemoryBlockLookaside
736 stdcall -version=0x600+ RtlCreateMemoryZone(ptr long long) ChpeStubRtlCreateMemoryZone
737 stdcall RtlCreateProcessParameters(ptr ptr ptr ptr ptr ptr ptr ptr ptr ptr) ChpeAutoRtlCreateProcessParameters
738 stdcall -version=0x600+ RtlCreateProcessParametersEx(ptr wstr ptr wstr wstr ptr wstr wstr ptr ptr long) ChpeAutoRtlCreateProcessParametersEx
739 stdcall RtlCreateQueryDebugBuffer(long long) ChpeAutoRtlCreateQueryDebugBuffer
740 stdcall RtlCreateRegistryKey(long wstr) ChpeAutoRtlCreateRegistryKey
741 stdcall RtlCreateSecurityDescriptor(ptr long) ChpeAutoRtlCreateSecurityDescriptor
742 stdcall RtlCreateServiceSid(ptr ptr ptr) ChpeAutoRtlCreateServiceSid
743 stdcall -version=0xA00+ RtlDeriveCapabilitySidsFromName(ptr ptr ptr) ChpeAutoRtlDeriveCapabilitySidsFromName
744 stdcall -version=0x602+ RtlIsCapabilitySid(ptr) ChpeAutoRtlIsCapabilitySid
745 stdcall -version=0x602+ RtlGetAppContainerSidType(ptr ptr) ChpeAutoRtlGetAppContainerSidType
746 stdcall -version=0x602+ RtlGetAppContainerParent(ptr ptr) ChpeAutoRtlGetAppContainerParent
747 stdcall -version=0x602+ RtlIsParentOfChildAppContainer(ptr ptr) ChpeAutoRtlIsParentOfChildAppContainer
748 stdcall -version=0x602+ RtlCheckTokenMembershipEx(ptr ptr long ptr) ChpeAutoRtlCheckTokenMembershipEx
749 stdcall RtlCreateSystemVolumeInformationFolder(ptr) ChpeAutoRtlCreateSystemVolumeInformationFolder
750 stdcall RtlCreateTagHeap(ptr long wstr wstr) ChpeAutoRtlCreateTagHeap
751 stdcall RtlCreateTimer(ptr ptr ptr ptr long long long) ChpeAutoRtlCreateTimer
752 stdcall RtlCreateTimerQueue(ptr) ChpeAutoRtlCreateTimerQueue
753 stdcall RtlCreateUnicodeString(ptr wstr) ChpeAutoRtlCreateUnicodeString
754 stdcall RtlCreateUnicodeStringFromAsciiz(ptr str) ChpeAutoRtlCreateUnicodeStringFromAsciiz
755 stdcall RtlCreateUserProcess(ptr long ptr ptr ptr ptr long ptr ptr ptr) ChpeAutoRtlCreateUserProcess
756 stdcall RtlCreateUserSecurityObject(ptr long ptr ptr long ptr ptr) ChpeAutoRtlCreateUserSecurityObject
757 stdcall -version=0x600+ RtlCreateUserStack(long long long long long ptr) ChpeAutoRtlCreateUserStack
759 stdcall -version=0x600+ RtlCultureNameToLCID(ptr ptr) ChpeAutoRtlCultureNameToLCID
760 stdcall RtlCustomCPToUnicodeN(ptr wstr long ptr str long) ChpeAutoRtlCustomCPToUnicodeN
761 stdcall RtlCutoverTimeToSystemTime(ptr ptr ptr long) ChpeAutoRtlCutoverTimeToSystemTime
762 stdcall -version=0x600+ RtlDeCommitDebugInfo(long long long) ChpeStubRtlDeCommitDebugInfo
763 stdcall RtlDeNormalizeProcessParams(ptr) ChpeAutoRtlDeNormalizeProcessParams
764 stdcall RtlDeactivateActivationContext(long long) ChpeAutoRtlDeactivateActivationContext
765 stdcall RtlDeactivateActivationContextUnsafeFast(ptr) ChpeAutoRtlDeactivateActivationContextUnsafeFast
766 stdcall  RtlDebugPrintTimes() ChpeStubRtlDebugPrintTimes
769 stdcall RtlDecompressBuffer(long ptr long ptr long ptr) ChpeAutoRtlDecompressBuffer
771 stdcall RtlDefaultNpAcl(ptr) ChpeAutoRtlDefaultNpAcl
772 stdcall RtlDelete(ptr) ChpeAutoRtlDelete
773 stdcall RtlDeleteAce(ptr long) ChpeAutoRtlDeleteAce
774 stdcall RtlDeleteAtomFromAtomTable(ptr long) ChpeAutoRtlDeleteAtomFromAtomTable
775 stdcall RtlDeleteHashTable(ptr) ChpeAutoRtlDeleteHashTable
776 stdcall -version=0x600+ RtlDeleteBarrier(long) ChpeStubRtlDeleteBarrier
777 stdcall -version=0x600+ RtlDeleteBoundaryDescriptor(ptr) ChpeAutoRtlDeleteBoundaryDescriptor
779 stdcall RtlDeleteElementGenericTable(ptr ptr) ChpeAutoRtlDeleteElementGenericTable
780 stdcall RtlDeleteElementGenericTableAvl(ptr ptr) ChpeAutoRtlDeleteElementGenericTableAvl
783 stdcall RtlDeleteNoSplay(ptr ptr) ChpeAutoRtlDeleteNoSplay
784 stdcall RtlDeleteRegistryValue(long ptr ptr) ChpeAutoRtlDeleteRegistryValue
785 stdcall RtlDeleteResource(ptr) ChpeAutoRtlDeleteResource
786 stdcall RtlDeleteSecurityObject(ptr) ChpeAutoRtlDeleteSecurityObject
787 stdcall RtlDeleteTimer(ptr ptr ptr) ChpeAutoRtlDeleteTimer
788 stdcall RtlDeleteTimerQueue(ptr) ChpeAutoRtlDeleteTimerQueue
789 stdcall RtlDeleteTimerQueueEx(ptr ptr) ChpeAutoRtlDeleteTimerQueueEx
790 stdcall -version=0x600+ RtlDeregisterSecureMemoryCacheCallback(ptr) ChpeStubRtlDeregisterSecureMemoryCacheCallback
791 stdcall RtlDeregisterWait(ptr) ChpeAutoRtlDeregisterWait
792 stdcall RtlDeregisterWaitEx(ptr ptr) ChpeAutoRtlDeregisterWaitEx
793 stdcall RtlDestroyAtomTable(ptr) ChpeAutoRtlDestroyAtomTable
794 stdcall RtlDestroyEnvironment(ptr) ChpeAutoRtlDestroyEnvironment
795 stdcall RtlDestroyHandleTable(ptr) ChpeAutoRtlDestroyHandleTable
796 stdcall RtlDestroyHeap(long) ChpeAutoRtlDestroyHeap
797 stdcall -version=0x600+ RtlDestroyMemoryBlockLookaside(long) ChpeStubRtlDestroyMemoryBlockLookaside
798 stdcall -version=0x600+ RtlDestroyMemoryZone(long) ChpeStubRtlDestroyMemoryZone
799 stdcall RtlDestroyProcessParameters(ptr) ChpeAutoRtlDestroyProcessParameters
800 stdcall RtlDestroyQueryDebugBuffer(ptr) ChpeAutoRtlDestroyQueryDebugBuffer
801 stdcall RtlDetermineDosPathNameType_U(wstr) ChpeAutoRtlDetermineDosPathNameType_U
802 stdcall RtlDllShutdownInProgress() ChpeAutoRtlDllShutdownInProgress
803 stdcall RtlDnsHostNameToComputerName(ptr ptr long) ChpeAutoRtlDnsHostNameToComputerName
804 stdcall RtlDoesFileExists_U(wstr) ChpeAutoRtlDoesFileExists_U
805 stdcall RtlDosApplyFileIsolationRedirection_Ustr(long ptr ptr ptr ptr ptr ptr ptr ptr) ChpeAutoRtlDosApplyFileIsolationRedirection_Ustr
806 stdcall RtlDosPathNameToNtPathName_U(wstr ptr ptr ptr) ChpeAutoRtlDosPathNameToNtPathName_U
807 stdcall RtlDosPathNameToNtPathName_U_WithStatus(wstr ptr ptr ptr) ChpeAutoRtlDosPathNameToNtPathName_U_WithStatus
808 stdcall RtlDosPathNameToRelativeNtPathName_U(ptr ptr ptr ptr) ChpeAutoRtlDosPathNameToRelativeNtPathName_U
809 stdcall RtlDosPathNameToRelativeNtPathName_U_WithStatus(wstr ptr ptr ptr) ChpeAutoRtlDosPathNameToRelativeNtPathName_U_WithStatus
810 stdcall RtlDosSearchPath_U(wstr wstr wstr long ptr ptr) ChpeAutoRtlDosSearchPath_U
811 stdcall RtlDosSearchPath_Ustr(long ptr ptr ptr ptr ptr ptr ptr ptr) ChpeAutoRtlDosSearchPath_Ustr
812 stdcall RtlDowncaseUnicodeChar(long) ChpeAutoRtlDowncaseUnicodeChar
813 stdcall RtlDowncaseUnicodeString(ptr ptr long) ChpeAutoRtlDowncaseUnicodeString
814 stdcall RtlDumpResource(ptr) ChpeAutoRtlDumpResource
815 stdcall RtlDuplicateUnicodeString(long ptr ptr) ChpeAutoRtlDuplicateUnicodeString
816 stdcall RtlEmptyAtomTable(ptr long) ChpeAutoRtlEmptyAtomTable
817 stdcall RtlEndEnumerationHashTable(ptr ptr) ChpeAutoRtlEndEnumerationHashTable
818 stdcall RtlEndWeakEnumerationHashTable(ptr ptr) ChpeAutoRtlEndWeakEnumerationHashTable
819 stdcall  RtlEnableEarlyCriticalSectionEventCreation() ChpeStubRtlEnableEarlyCriticalSectionEventCreation
823 stdcall RtlEnumProcessHeaps(ptr ptr) ChpeAutoRtlEnumProcessHeaps
824 stdcall RtlEnumerateEntryHashTable(ptr ptr) ChpeAutoRtlEnumerateEntryHashTable
825 stdcall RtlEnumerateGenericTable(ptr long) ChpeAutoRtlEnumerateGenericTable
826 stdcall RtlEnumerateGenericTableAvl(ptr long) ChpeAutoRtlEnumerateGenericTableAvl
827 stdcall RtlEnumerateGenericTableLikeADirectory(ptr ptr ptr long ptr ptr ptr) ChpeAutoRtlEnumerateGenericTableLikeADirectory
828 stdcall RtlEnumerateGenericTableWithoutSplaying(ptr ptr) ChpeAutoRtlEnumerateGenericTableWithoutSplaying
829 stdcall RtlEnumerateGenericTableWithoutSplayingAvl(ptr ptr) ChpeAutoRtlEnumerateGenericTableWithoutSplayingAvl
830 stdcall RtlEqualComputerName(ptr ptr) ChpeAutoRtlEqualComputerName
831 stdcall RtlEqualDomainName(ptr ptr) ChpeAutoRtlEqualDomainName
832 stdcall RtlEqualLuid(ptr ptr) ChpeAutoRtlEqualLuid
833 stdcall RtlExpandHashTable(ptr) ChpeAutoRtlExpandHashTable
834 stdcall RtlEqualPrefixSid(ptr ptr) ChpeAutoRtlEqualPrefixSid
835 stdcall RtlEqualSid(long long) ChpeAutoRtlEqualSid
836 stdcall RtlEqualString(ptr ptr long) ChpeAutoRtlEqualString
837 stdcall RtlEqualUnicodeString(ptr ptr long) ChpeAutoRtlEqualUnicodeString
838 stdcall RtlEraseUnicodeString(ptr) ChpeAutoRtlEraseUnicodeString
841 stdcall RtlWow64GetCurrentCpuArea(ptr ptr ptr) ChpeAutoRtlWow64GetCurrentCpuArea
843 stdcall ChpeMarkEcCodeRange(ptr long) ChpeAutoChpeMarkEcCodeRange
844 stdcall ChpeCanContinueToGuest() ChpeAutoChpeCanContinueToGuest
845 stdcall ChpeContinueToGuest(ptr) ChpeAutoChpeContinueToGuest
846 stdcall ChpeContinueToGuestEx(ptr long) ChpeAutoChpeContinueToGuestEx
847 stdcall ProcessPendingCrossProcessEmulatorWork() ChpeAutoProcessPendingCrossProcessEmulatorWork
848 stdcall ChpeIsProcessorFeaturePresent(long) ChpeAutoChpeIsProcessorFeaturePresent
849 stdcall RtlWow64PopAllCrossProcessWorkFromWorkList(ptr ptr) ChpeAutoRtlWow64PopAllCrossProcessWorkFromWorkList
850 stdcall RtlWow64PushCrossProcessWorkOntoFreeList(ptr ptr) ChpeAutoRtlWow64PushCrossProcessWorkOntoFreeList
853 stdcall -version=0x600+ RtlLocateLegacyContext(ptr ptr) ChpeAutoRtlLocateLegacyContext
856 stdcall -version=0x601+ RtlQueryUnbiasedInterruptTime(ptr) ChpeAutoRtlQueryUnbiasedInterruptTime
859 stdcall RtlExitUserProcess(long) ChpeAutoRtlExitUserProcess
861 stdcall -version=0x600+ RtlExpandEnvironmentStrings(long ptr long ptr long ptr) ChpeAutoRtlExpandEnvironmentStrings
862 stdcall RtlExpandEnvironmentStrings_U(ptr ptr ptr ptr) ChpeAutoRtlExpandEnvironmentStrings_U
863 stdcall -version=0x600+ RtlExtendMemoryBlockLookaside(long) ChpeStubRtlExtendMemoryBlockLookaside
864 stdcall -version=0x600+ RtlExtendMemoryZone(long long) ChpeStubRtlExtendMemoryZone
866 stdcall RtlFillMemoryUlong(ptr long long) ChpeAutoRtlFillMemoryUlong
867 stdcall RtlFinalReleaseOutOfProcessMemoryStream(ptr) ChpeAutoRtlFinalReleaseOutOfProcessMemoryStream
868 stdcall -version=0x600+ RtlFindAceByType(long long ptr) ChpeStubRtlFindAceByType
869 stdcall RtlFindActivationContextSectionGuid(long ptr long ptr ptr) ChpeAutoRtlFindActivationContextSectionGuid
870 stdcall RtlFindActivationContextSectionString(long ptr long ptr ptr) ChpeAutoRtlFindActivationContextSectionString
871 stdcall RtlFindCharInUnicodeString(long ptr ptr ptr) ChpeAutoRtlFindCharInUnicodeString
872 stdcall RtlFindClearBits(ptr long long) ChpeAutoRtlFindClearBits
873 stdcall RtlFindClearBitsAndSet(ptr long long) ChpeAutoRtlFindClearBitsAndSet
874 stdcall RtlFindClearRuns(ptr ptr long long) ChpeAutoRtlFindClearRuns
875 stdcall -version=0x600+ RtlFindClosestEncodableLength(long ptr) ChpeStubRtlFindClosestEncodableLength
877 stdcall RtlFindLastBackwardRunClear(ptr long ptr) ChpeAutoRtlFindLastBackwardRunClear
878 stdcall RtlFindLeastSignificantBit(double) ChpeAutoRtlFindLeastSignificantBit
879 stdcall RtlFindLongestRunClear(ptr long) ChpeAutoRtlFindLongestRunClear
880 stdcall RtlFindMessage(long long long long ptr) ChpeAutoRtlFindMessage
881 stdcall RtlFindMostSignificantBit(double) ChpeAutoRtlFindMostSignificantBit
882 stdcall RtlFindNextForwardRunClear(ptr long ptr) ChpeAutoRtlFindNextForwardRunClear
883 stdcall RtlFindSetBits(ptr long long) ChpeAutoRtlFindSetBits
884 stdcall RtlFindSetBitsAndClear(ptr long long) ChpeAutoRtlFindSetBitsAndClear
885 stdcall RtlFirstEntrySList(ptr) ChpeAutoRtlFirstEntrySList
886 stdcall RtlFirstFreeAce(ptr ptr) ChpeAutoRtlFirstFreeAce
891 stdcall RtlFlushSecureMemoryCache(ptr ptr) ChpeAutoRtlFlushSecureMemoryCache
893 stdcall RtlFormatMessage(ptr long long long long ptr ptr long ptr) ChpeAutoRtlFormatMessage
894 stdcall RtlFormatMessageEx(ptr long long long long ptr ptr long ptr long) ChpeAutoRtlFormatMessageEx
895 stdcall RtlFreeActivationContextStack(ptr) ChpeAutoRtlFreeActivationContextStack
896 stdcall RtlFreeAnsiString(long) ChpeAutoRtlFreeAnsiString
897 stdcall RtlFreeHandle(ptr ptr) ChpeAutoRtlFreeHandle
899 stdcall -version=0x600+ RtlFreeMemoryBlockLookaside(long long) ChpeStubRtlFreeMemoryBlockLookaside
900 stdcall RtlFreeOemString(ptr) ChpeAutoRtlFreeOemString
901 stdcall RtlFreeSid(long) ChpeAutoRtlFreeSid
902 stdcall RtlFreeThreadActivationContextStack() ChpeAutoRtlFreeThreadActivationContextStack
904 stdcall -version=0x600+ RtlFreeUserStack(ptr) ChpeAutoRtlFreeUserStack
905 stdcall RtlGUIDFromString(ptr ptr) ChpeAutoRtlGUIDFromString
907 stdcall RtlGetAce(ptr long ptr) ChpeAutoRtlGetAce
908 stdcall RtlGetActiveActivationContext(ptr) ChpeAutoRtlGetActiveActivationContext
909 stdcall RtlGetCallersAddress(ptr ptr) ChpeAutoRtlGetCallersAddress
910 stdcall RtlGetCompressionWorkSpaceSize(long ptr ptr) ChpeAutoRtlGetCompressionWorkSpaceSize
911 stdcall RtlGetControlSecurityDescriptor(ptr ptr ptr) ChpeAutoRtlGetControlSecurityDescriptor
912 stdcall RtlGetCriticalSectionRecursionCount(ptr) ChpeAutoRtlGetCriticalSectionRecursionCount
913 stdcall RtlGetCurrentDirectory_U(long ptr) ChpeAutoRtlGetCurrentDirectory_U
917 stdcall -version=0x600+ RtlGetCurrentTransaction() ChpeStubRtlGetCurrentTransaction
918 stdcall RtlGetDaclSecurityDescriptor(ptr ptr ptr ptr) ChpeAutoRtlGetDaclSecurityDescriptor
919 stdcall -version=0xA00+ RtlGetDeviceFamilyInfoEnum(ptr ptr ptr) ChpeAutoRtlGetDeviceFamilyInfoEnum
920 stdcall -version=0x600+ RtlGetEnabledExtendedFeatures(int64) ChpeAutoRtlGetEnabledExtendedFeatures
921 stdcall RtlGetElementGenericTable(ptr long) ChpeAutoRtlGetElementGenericTable
922 stdcall RtlGetElementGenericTableAvl(ptr long) ChpeAutoRtlGetElementGenericTableAvl
923 stdcall -version=0x600+ RtlGetExtendedContextLength(long ptr) ChpeAutoRtlGetExtendedContextLength
924 stdcall -version=0x600+ RtlGetExtendedContextLength2(long ptr int64) ChpeAutoRtlGetExtendedContextLength2
925 stdcall -version=0x600+ -ret64 RtlGetExtendedFeaturesMask(ptr) ChpeAutoRtlGetExtendedFeaturesMask
926 stdcall -version=0x600+ RtlGetFileMUIPath(long long ptr ptr long long ptr) ChpeStubRtlGetFileMUIPath
927 stdcall RtlGetFrame() ChpeAutoRtlGetFrame
928 stdcall RtlGetFullPathName_U(wstr long ptr ptr) ChpeAutoRtlGetFullPathName_U
929 stdcall RtlGetFullPathName_UstrEx(ptr ptr ptr ptr ptr ptr ptr ptr) ChpeAutoRtlGetFullPathName_UstrEx
931 stdcall RtlGetGroupSecurityDescriptor(ptr ptr ptr) ChpeAutoRtlGetGroupSecurityDescriptor
932 stdcall -version=0x600+ RtlGetIntegerAtom(wstr ptr) ChpeAutoRtlGetIntegerAtom
935 stdcall -version=0x600+ RtlGetLocaleFileMappingAddress(ptr ptr ptr) ChpeAutoRtlGetLocaleFileMappingAddress
937 stdcall RtlGetLengthWithoutTrailingPathSeperators(long ptr ptr) ChpeAutoRtlGetLengthWithoutTrailingPathSeperators
938 stdcall RtlGetLongestNtPathLength() ChpeAutoRtlGetLongestNtPathLength
940 stdcall RtlGetNtGlobalFlags() ChpeAutoRtlGetNtGlobalFlags
941 stdcall RtlGetNtProductType(ptr) ChpeAutoRtlGetNtProductType
942 stdcall RtlGetNtVersionNumbers(ptr ptr ptr) ChpeAutoRtlGetNtVersionNumbers
943 stdcall RtlGetOwnerSecurityDescriptor(ptr ptr ptr) ChpeAutoRtlGetOwnerSecurityDescriptor
944 stdcall -version=0x600+ RtlGetParentLocaleName(wstr long long long) ChpeStubRtlGetParentLocaleName
945 stdcall -version=0x600+ RtlGetProcessPreferredUILanguages(long ptr ptr ptr) ChpeAutoRtlGetProcessPreferredUILanguages
948 stdcall RtlGetSaclSecurityDescriptor(ptr ptr ptr ptr) ChpeAutoRtlGetSaclSecurityDescriptor
949 stdcall RtlGetSecurityDescriptorRMControl(ptr ptr) ChpeAutoRtlGetSecurityDescriptorRMControl
950 stdcall RtlGetSetBootStatusData(ptr long long ptr long long) ChpeAutoRtlGetSetBootStatusData
951 stdcall -version=0x600+ RtlGetSystemPreferredUILanguages(long long ptr ptr ptr) ChpeAutoRtlGetSystemPreferredUILanguages
952 stdcall RtlGetThreadErrorMode() ChpeAutoRtlGetThreadErrorMode
953 stdcall -version=0x600+ RtlGetThreadLangIdByIndex(long long ptr ptr) ChpeStubRtlGetThreadLangIdByIndex
954 stdcall -version=0x600+ RtlGetThreadPreferredUILanguages(long ptr ptr ptr) ChpeAutoRtlGetThreadPreferredUILanguages
955 stdcall -version=0x600+ RtlGetUILanguageInfo(long ptr long ptr ptr) ChpeStubRtlGetUILanguageInfo
957 stdcall -version=0x600+ RtlGetUnloadEventTraceEx(ptr ptr ptr) ChpeAutoRtlGetUnloadEventTraceEx
958 stdcall RtlGetUserInfoHeap(ptr long ptr ptr ptr) ChpeAutoRtlGetUserInfoHeap
959 stdcall -version=0x600+ RtlGetUserPreferredUILanguages(long long ptr ptr ptr) ChpeAutoRtlGetUserPreferredUILanguages
961 stdcall RtlHashUnicodeString(ptr long long ptr) ChpeAutoRtlHashUnicodeString
962 stdcall -version=0x600+ RtlHeapTrkInitialize(ptr) ChpeStubRtlHeapTrkInitialize
963 stdcall RtlIdentifierAuthoritySid(ptr) ChpeAutoRtlIdentifierAuthoritySid
964 stdcall -version=0x600+ RtlIdnToAscii(long wstr long ptr ptr) ChpeAutoRtlIdnToAscii
965 stdcall -version=0x600+ RtlIdnToNameprepUnicode(long wstr long ptr ptr) ChpeAutoRtlIdnToNameprepUnicode
966 stdcall -version=0x600+ RtlIdnToUnicode(long wstr long ptr ptr) ChpeAutoRtlIdnToUnicode
967 stdcall RtlImageDirectoryEntryToData(ptr long long ptr) ChpeAutoRtlImageDirectoryEntryToData
968 stdcall RtlImageNtHeader(long) ChpeAutoRtlImageNtHeader
969 stdcall RtlImageNtHeaderEx(long ptr double ptr) ChpeAutoRtlImageNtHeaderEx
970 stdcall RtlImageRvaToSection(ptr long long) ChpeAutoRtlImageRvaToSection
971 stdcall RtlImageRvaToVa(ptr long long ptr) ChpeAutoRtlImageRvaToVa
972 stdcall RtlImpersonateSelf(long) ChpeAutoRtlImpersonateSelf
973 stdcall -version=0x600+ RtlImpersonateSelfEx(long long ptr) ChpeStubRtlImpersonateSelfEx
974 stdcall RtlInitAnsiString(ptr str) ChpeAutoRtlInitAnsiString
975 stdcall RtlInitAnsiStringEx(ptr str) ChpeAutoRtlInitAnsiStringEx
976 stdcall -version=0xA00+ RtlInitUTF8String(ptr str) ChpeAutoRtlInitUTF8String
977 stdcall -version=0x600+ RtlInitBarrier(long long) ChpeStubRtlInitBarrier
978 stdcall RtlInitCodePageTable(ptr ptr) ChpeAutoRtlInitCodePageTable
980 stdcall RtlInitNlsTables(ptr ptr ptr ptr) ChpeAutoRtlInitNlsTables
982 stdcall RtlInitString(ptr str) ChpeAutoRtlInitString
983 stdcall RtlGetNextEntryHashTable(ptr ptr) ChpeAutoRtlGetNextEntryHashTable
984 stdcall RtlInitEnumerationHashTable(ptr ptr) ChpeAutoRtlInitEnumerationHashTable
986 stdcall RtlInitWeakEnumerationHashTable(ptr ptr) ChpeAutoRtlInitWeakEnumerationHashTable
987 stdcall RtlInitUnicodeStringEx(ptr wstr) ChpeAutoRtlInitUnicodeStringEx
988 stdcall  RtlInitializeAtomPackage(ptr) ChpeStubRtlInitializeAtomPackage
989 stdcall RtlInitializeBitMap(ptr long long) ChpeAutoRtlInitializeBitMap
991 stdcall RtlInitializeContext(ptr ptr ptr ptr ptr) ChpeAutoRtlInitializeContext
993 stdcall RtlInitializeCriticalSectionAndSpinCount(ptr long) ChpeAutoRtlInitializeCriticalSectionAndSpinCount
994 stdcall -version=0x600+ RtlInitializeCriticalSectionEx(ptr long long) ChpeAutoRtlInitializeCriticalSectionEx
995 stdcall -version=0x600+ RtlInitializeExtendedContext(ptr long ptr) ChpeAutoRtlInitializeExtendedContext
996 stdcall -version=0x600+ RtlInitializeExtendedContext2(ptr long ptr int64) ChpeAutoRtlInitializeExtendedContext2
997 stdcall RtlInitializeGenericTable(ptr ptr ptr ptr ptr) ChpeAutoRtlInitializeGenericTable
998 stdcall RtlInitializeGenericTableAvl(ptr ptr ptr ptr ptr) ChpeAutoRtlInitializeGenericTableAvl
999 stdcall RtlInitializeHandleTable(long long ptr) ChpeAutoRtlInitializeHandleTable
1000 stdcall -version=0x600+ RtlInitializeNtUserPfn(ptr long ptr long ptr long) ChpeAutoRtlInitializeNtUserPfn
1001 stdcall RtlInitializeRXact(ptr long ptr) ChpeAutoRtlInitializeRXact
1002 stdcall RtlInitializeResource(ptr) ChpeAutoRtlInitializeResource
1005 stdcall RtlInitializeSid(ptr ptr long) ChpeAutoRtlInitializeSid
1006 stdcall RtlInsertElementGenericTable(ptr ptr long ptr) ChpeAutoRtlInsertElementGenericTable
1007 stdcall RtlInsertEntryHashTable(ptr ptr long ptr) ChpeAutoRtlInsertEntryHashTable
1008 stdcall RtlInsertElementGenericTableAvl(ptr ptr long ptr) ChpeAutoRtlInsertElementGenericTableAvl
1009 stdcall RtlInsertElementGenericTableFull(ptr ptr long ptr ptr long) ChpeAutoRtlInsertElementGenericTableFull
1010 stdcall RtlInsertElementGenericTableFullAvl(ptr ptr long ptr ptr long) ChpeAutoRtlInsertElementGenericTableFullAvl
1013 stdcall RtlInt64ToUnicodeString(double long ptr) ChpeAutoRtlInt64ToUnicodeString
1014 stdcall RtlIntegerToChar(long long long ptr) ChpeAutoRtlIntegerToChar
1015 stdcall RtlIntegerToUnicodeString(long long ptr) ChpeAutoRtlIntegerToUnicodeString
1021 stdcall -version=0x600+ RtlIoDecodeMemIoResource(ptr ptr ptr ptr) ChpeStubRtlIoDecodeMemIoResource
1022 stdcall -version=0x600+ RtlIoEncodeMemIoResource(ptr long long long long long) ChpeStubRtlIoEncodeMemIoResource
1023 stdcall RtlIpv4AddressToStringA(ptr ptr) ChpeAutoRtlIpv4AddressToStringA
1024 stdcall RtlIpv4AddressToStringExA(ptr long ptr ptr) ChpeAutoRtlIpv4AddressToStringExA
1025 stdcall RtlIpv4AddressToStringExW(ptr long ptr ptr) ChpeAutoRtlIpv4AddressToStringExW
1026 stdcall RtlIpv4AddressToStringW(ptr ptr) ChpeAutoRtlIpv4AddressToStringW
1027 stdcall RtlIpv4StringToAddressA(str long ptr ptr) ChpeAutoRtlIpv4StringToAddressA
1028 stdcall RtlIpv4StringToAddressExA(str long ptr ptr) ChpeAutoRtlIpv4StringToAddressExA
1029 stdcall RtlIpv4StringToAddressExW(wstr long ptr ptr) ChpeAutoRtlIpv4StringToAddressExW
1030 stdcall RtlIpv4StringToAddressW(wstr long ptr ptr) ChpeAutoRtlIpv4StringToAddressW
1031 stdcall RtlIpv6AddressToStringA(ptr ptr) ChpeAutoRtlIpv6AddressToStringA
1032 stdcall RtlIpv6AddressToStringExA(ptr long long ptr ptr) ChpeAutoRtlIpv6AddressToStringExA
1033 stdcall RtlIpv6AddressToStringExW(ptr long long ptr ptr) ChpeAutoRtlIpv6AddressToStringExW
1034 stdcall RtlIpv6AddressToStringW(ptr ptr) ChpeAutoRtlIpv6AddressToStringW
1035 stdcall RtlIpv6StringToAddressA(str ptr ptr) ChpeAutoRtlIpv6StringToAddressA
1036 stdcall RtlIpv6StringToAddressExA(str ptr ptr ptr) ChpeAutoRtlIpv6StringToAddressExA
1037 stdcall RtlIpv6StringToAddressExW(wstr ptr ptr ptr) ChpeAutoRtlIpv6StringToAddressExW
1038 stdcall RtlIpv6StringToAddressW(wstr ptr ptr) ChpeAutoRtlIpv6StringToAddressW
1039 stdcall RtlIsActivationContextActive(ptr) ChpeAutoRtlIsActivationContextActive
1040 stdcall RtlIsCriticalSectionLocked(ptr) ChpeAutoRtlIsCriticalSectionLocked
1041 stdcall RtlIsCriticalSectionLockedByThread(ptr) ChpeAutoRtlIsCriticalSectionLockedByThread
1042 stdcall -version=0x600+ RtlIsCurrentProcess(ptr) ChpeAutoRtlIsCurrentProcess
1043 stdcall -version=0x600+ RtlIsCurrentThreadAttachExempt() ChpeStubRtlIsCurrentThreadAttachExempt
1044 stdcall RtlIsDosDeviceName_U(wstr) ChpeAutoRtlIsDosDeviceName_U
1045 stdcall RtlIsGenericTableEmpty(ptr) ChpeAutoRtlIsGenericTableEmpty
1046 stdcall RtlIsGenericTableEmptyAvl(ptr) ChpeAutoRtlIsGenericTableEmptyAvl
1047 stdcall -version=0x601+ RtlIsNameInExpression(ptr ptr long ptr) ChpeAutoRtlIsNameInExpression
1048 stdcall RtlIsNameLegalDOS8Dot3(ptr ptr ptr) ChpeAutoRtlIsNameLegalDOS8Dot3
1049 stdcall -version=0x600+ RtlIsNormalizedString(long ptr long ptr) ChpeAutoRtlIsNormalizedString
1051 stdcall RtlIsTextUnicode(ptr long ptr) ChpeAutoRtlIsTextUnicode
1052 stdcall RtlIsThreadWithinLoaderCallout() ChpeAutoRtlIsThreadWithinLoaderCallout
1053 stdcall RtlIsValidHandle(ptr ptr) ChpeAutoRtlIsValidHandle
1054 stdcall RtlIsValidIndexHandle(ptr long ptr) ChpeAutoRtlIsValidIndexHandle
1055 stdcall -version=0x600+ RtlIsValidLocaleName(wstr long) ChpeAutoRtlIsValidLocaleName
1056 stdcall -version=0x600+ RtlLCIDToCultureName(long ptr) ChpeAutoRtlLCIDToCultureName
1057 stdcall RtlLargeIntegerToChar(ptr long long ptr) ChpeAutoRtlLargeIntegerToChar
1058 stdcall -version=0x600+ RtlLcidToLocaleName(long ptr long long) ChpeAutoRtlLcidToLocaleName
1060 stdcall RtlLengthRequiredSid(long) ChpeAutoRtlLengthRequiredSid
1061 stdcall RtlLengthSecurityDescriptor(ptr) ChpeAutoRtlLengthSecurityDescriptor
1062 stdcall RtlLengthSid(ptr) ChpeAutoRtlLengthSid
1063 stdcall RtlLocalTimeToSystemTime(ptr ptr) ChpeAutoRtlLocalTimeToSystemTime
1064 stdcall -version=0x600+ RtlLocaleNameToLcid(wstr ptr long) ChpeAutoRtlLocaleNameToLcid
1065 stdcall RtlLockBootStatusData(ptr) ChpeAutoRtlLockBootStatusData
1066 stdcall -version=0x600+ RtlLockCurrentThread() ChpeStubRtlLockCurrentThread
1067 stdcall RtlLockHeap(long) ChpeAutoRtlLockHeap
1068 stdcall -version=0x600+ RtlLockMemoryBlockLookaside(long) ChpeStubRtlLockMemoryBlockLookaside
1069 stdcall RtlLockMemoryStreamRegion(ptr int64 int64 long) ChpeAutoRtlLockMemoryStreamRegion
1070 stdcall -version=0x600+ RtlLockMemoryZone(long) ChpeStubRtlLockMemoryZone
1071 stdcall -version=0x600+ RtlLockModuleSection(long) ChpeStubRtlLockModuleSection
1073 stdcall  RtlLogStackBackTrace() ChpeStubRtlLogStackBackTrace
1074 stdcall RtlLookupAtomInAtomTable(ptr wstr ptr) ChpeAutoRtlLookupAtomInAtomTable
1075 stdcall RtlLookupElementGenericTable(ptr ptr) ChpeAutoRtlLookupElementGenericTable
1076 stdcall RtlLookupEntryHashTable(ptr long ptr) ChpeAutoRtlLookupEntryHashTable
1077 stdcall RtlRemoveEntryHashTable(ptr ptr ptr) ChpeAutoRtlRemoveEntryHashTable
1078 stdcall RtlLookupElementGenericTableAvl(ptr ptr) ChpeAutoRtlLookupElementGenericTableAvl
1079 stdcall RtlLookupElementGenericTableFull(ptr ptr ptr long) ChpeAutoRtlLookupElementGenericTableFull
1080 stdcall RtlLookupElementGenericTableFullAvl(ptr ptr ptr long) ChpeAutoRtlLookupElementGenericTableFullAvl
1083 stdcall RtlMakeSelfRelativeSD(ptr ptr ptr) ChpeAutoRtlMakeSelfRelativeSD
1084 stdcall RtlMapGenericMask(long ptr) ChpeAutoRtlMapGenericMask
1085 stdcall RtlMapSecurityErrorToNtStatus(long) ChpeAutoRtlMapSecurityErrorToNtStatus
1087 stdcall RtlMultiAppendUnicodeStringBuffer(ptr long ptr) ChpeAutoRtlMultiAppendUnicodeStringBuffer
1088 stdcall RtlMultiByteToUnicodeN(ptr long ptr ptr long) ChpeAutoRtlMultiByteToUnicodeN
1089 stdcall RtlMultiByteToUnicodeSize(ptr str long) ChpeAutoRtlMultiByteToUnicodeSize
1090 stdcall RtlMultipleAllocateHeap(ptr long ptr long ptr) ChpeAutoRtlMultipleAllocateHeap
1091 stdcall RtlMultipleFreeHeap(ptr long long ptr) ChpeAutoRtlMultipleFreeHeap
1092 stdcall RtlNewInstanceSecurityObject(long long ptr ptr ptr ptr ptr long ptr ptr) ChpeAutoRtlNewInstanceSecurityObject
1093 stdcall RtlNewSecurityGrantedAccess(long ptr ptr ptr ptr ptr) ChpeAutoRtlNewSecurityGrantedAccess
1094 stdcall RtlNewSecurityObject(ptr ptr ptr long ptr ptr) ChpeAutoRtlNewSecurityObject
1095 stdcall RtlNewSecurityObjectEx(ptr ptr ptr ptr long long ptr ptr) ChpeAutoRtlNewSecurityObjectEx
1096 stdcall RtlNewSecurityObjectWithMultipleInheritance(ptr ptr ptr ptr long long long ptr ptr) ChpeAutoRtlNewSecurityObjectWithMultipleInheritance
1097 stdcall RtlNormalizeProcessParams(ptr) ChpeAutoRtlNormalizeProcessParams
1098 stdcall -version=0x600+ RtlNormalizeString(long ptr long ptr ptr) ChpeAutoRtlNormalizeString
1099 stdcall RtlNtPathNameToDosPathName(long ptr ptr ptr) ChpeAutoRtlNtPathNameToDosPathName
1101 stdcall RtlNtStatusToDosErrorNoTeb(long) ChpeAutoRtlNtStatusToDosErrorNoTeb
1102 stub -version=0x600+ RtlNtdllName
1103 stdcall RtlNumberGenericTableElements(ptr) ChpeAutoRtlNumberGenericTableElements
1104 stdcall RtlNumberGenericTableElementsAvl(ptr) ChpeAutoRtlNumberGenericTableElementsAvl
1105 stdcall RtlNumberOfClearBits(ptr) ChpeAutoRtlNumberOfClearBits
1106 stdcall RtlNumberOfSetBits(ptr) ChpeAutoRtlNumberOfSetBits
1107 stdcall -version=0x600+ RtlNumberOfSetBitsUlongPtr(long) ChpeAutoRtlNumberOfSetBitsUlongPtr
1108 stdcall RtlOemStringToUnicodeSize(ptr) ChpeAutoRtlOemStringToUnicodeSize
1109 stdcall RtlOemStringToUnicodeString(ptr ptr long) ChpeAutoRtlOemStringToUnicodeString
1110 stdcall RtlOemToUnicodeN(ptr long ptr ptr long) ChpeAutoRtlOemToUnicodeN
1111 stdcall RtlOpenCurrentUser(long ptr) ChpeAutoRtlOpenCurrentUser
1112 stdcall -version=0x600+ RtlOwnerAcesPresent(long) ChpeStubRtlOwnerAcesPresent
1114 stdcall RtlPinAtomInAtomTable(ptr long) ChpeAutoRtlPinAtomInAtomTable
1115 stdcall RtlPopFrame(ptr) ChpeAutoRtlPopFrame
1116 stdcall RtlPrefixString(ptr ptr long) ChpeAutoRtlPrefixString
1117 stdcall RtlPrefixUnicodeString(ptr ptr long) ChpeAutoRtlPrefixUnicodeString
1118 stdcall -version=0x600+ RtlPrepareForProcessCloning() ChpeStubRtlPrepareForProcessCloning
1119 stdcall -version=0x600+ RtlProcessFlsData(ptr long) ChpeAutoRtlProcessFlsData
1120 stdcall RtlProtectHeap(ptr long) ChpeAutoRtlProtectHeap
1121 stdcall RtlPushFrame(ptr) ChpeAutoRtlPushFrame
1122 stdcall -version=0x600+ RtlQueryActivationContextApplicationSettings(long ptr wstr wstr ptr ptr ptr) ChpeAutoRtlQueryActivationContextApplicationSettings
1123 stdcall RtlQueryAtomInAtomTable(ptr long ptr ptr ptr ptr) ChpeAutoRtlQueryAtomInAtomTable
1124 stdcall -version=0x600+ RtlQueryCriticalSectionOwner(ptr) ChpeStubRtlQueryCriticalSectionOwner
1126 stdcall -version=0x600+ RtlQueryDynamicTimeZoneInformation(ptr) ChpeAutoRtlQueryDynamicTimeZoneInformation
1127 stdcall -version=0x600+ RtlQueryElevationFlags(ptr) ChpeStubRtlQueryElevationFlags
1129 stdcall RtlQueryEnvironmentVariable_U(ptr ptr ptr) ChpeAutoRtlQueryEnvironmentVariable_U
1130 stdcall RtlQueryHeapInformation(long long ptr long ptr) ChpeAutoRtlQueryHeapInformation
1131 stdcall RtlQueryInformationAcl(ptr ptr long long) ChpeAutoRtlQueryInformationAcl
1132 stdcall RtlQueryInformationActivationContext(long long ptr long ptr long ptr) ChpeAutoRtlQueryInformationActivationContext
1133 stdcall RtlQueryInformationActiveActivationContext(long ptr long ptr) ChpeAutoRtlQueryInformationActiveActivationContext
1134 stdcall RtlQueryInterfaceMemoryStream(ptr ptr ptr) ChpeAutoRtlQueryInterfaceMemoryStream
1135 stdcall -version=0x600+ RtlQueryModuleInformation(ptr long ptr) ChpeStubRtlQueryModuleInformation
1136 stdcall  RtlQueryProcessBackTraceInformation(ptr) ChpeStubRtlQueryProcessBackTraceInformation
1137 stdcall RtlQueryProcessDebugInformation(long long ptr) ChpeAutoRtlQueryProcessDebugInformation
1138 stdcall RtlQueryProcessHeapInformation(ptr) ChpeAutoRtlQueryProcessHeapInformation
1139 stdcall  RtlQueryProcessLockInformation(ptr) ChpeStubRtlQueryProcessLockInformation
1140 stdcall RtlQueryRegistryValues(long ptr ptr ptr ptr) ChpeAutoRtlQueryRegistryValues
1141 stdcall RtlQueryRegistryValuesEx(long ptr ptr ptr ptr) ChpeAutoRtlQueryRegistryValuesEx
1142 stdcall RtlQuerySecurityObject(ptr long ptr long ptr) ChpeAutoRtlQuerySecurityObject
1143 stdcall RtlQueryTagHeap(ptr long long long ptr) ChpeAutoRtlQueryTagHeap
1144 stdcall RtlQueryTimeZoneInformation(ptr) ChpeAutoRtlQueryTimeZoneInformation
1145 stdcall RtlQueueApcWow64Thread(ptr ptr ptr ptr ptr) ChpeAutoRtlQueueApcWow64Thread
1150 stdcall RtlRandomEx(ptr) ChpeAutoRtlRandomEx
1152 stdcall RtlReadMemoryStream(ptr ptr long ptr) ChpeAutoRtlReadMemoryStream
1153 stdcall RtlReadOutOfProcessMemoryStream(ptr ptr long ptr) ChpeAutoRtlReadOutOfProcessMemoryStream
1154 stdcall RtlRealPredecessor(ptr) ChpeAutoRtlRealPredecessor
1155 stdcall RtlRealSuccessor(ptr) ChpeAutoRtlRealSuccessor
1156 stdcall RtlRegisterSecureMemoryCacheCallback(ptr) ChpeAutoRtlRegisterSecureMemoryCacheCallback
1157 stdcall RtlRegisterCfgTargetRange(ptr long) ChpeAutoRtlRegisterCfgTargetRange
1158 stdcall -version=0x600+ RtlRegisterThreadWithCsrss() ChpeStubRtlRegisterThreadWithCsrss
1160 stdcall RtlReleaseActivationContext(ptr) ChpeAutoRtlReleaseActivationContext
1162 stdcall RtlReleasePebLock() ChpeAutoRtlReleasePebLock
1164 stdcall RtlReleaseRelativeName(ptr) ChpeAutoRtlReleaseRelativeName
1165 stdcall RtlReleaseResource(ptr) ChpeAutoRtlReleaseResource
1168 stdcall RtlRemoteCall(ptr ptr ptr long ptr long long) ChpeAutoRtlRemoteCall
1169 stdcall -version=0x600+ RtlRemovePrivileges(ptr ptr long) ChpeAutoRtlRemovePrivileges
1172 stdcall -version=0x600+ RtlReportException(long long long) ChpeStubRtlReportException
1173 stdcall -version=0x600+ RtlResetMemoryBlockLookaside(long) ChpeStubRtlResetMemoryBlockLookaside
1174 stdcall -version=0x600+ RtlResetMemoryZone(long) ChpeStubRtlResetMemoryZone
1175 stdcall -version=0x600+ RtlResetNtUserPfn() ChpeAutoRtlResetNtUserPfn
1176 stdcall RtlResetRtlTranslations(ptr) ChpeAutoRtlResetRtlTranslations
1179 stdcall -version=0x600+ RtlRetrieveNtUserPfn(ptr ptr ptr) ChpeAutoRtlRetrieveNtUserPfn
1180 stdcall RtlRevertMemoryStream(ptr) ChpeAutoRtlRevertMemoryStream
1181 stdcall RtlRunDecodeUnicodeString(long ptr) ChpeAutoRtlRunDecodeUnicodeString
1182 stdcall RtlRunEncodeUnicodeString(long ptr) ChpeAutoRtlRunEncodeUnicodeString
1183 stdcall -version=0x600+ RtlRunOnceBeginInitialize(ptr long ptr) ChpeAutoRtlRunOnceBeginInitialize
1184 stdcall -version=0x600+ RtlRunOnceComplete(ptr long ptr) ChpeAutoRtlRunOnceComplete
1187 stdcall RtlSecondsSince1970ToTime(long ptr) ChpeAutoRtlSecondsSince1970ToTime
1188 stdcall RtlSecondsSince1980ToTime(long ptr) ChpeAutoRtlSecondsSince1980ToTime
1189 stdcall RtlSeekMemoryStream(ptr int64 long ptr) ChpeAutoRtlSeekMemoryStream
1190 stdcall RtlSelfRelativeToAbsoluteSD2(ptr ptr) ChpeAutoRtlSelfRelativeToAbsoluteSD2
1191 stdcall RtlSelfRelativeToAbsoluteSD(ptr ptr ptr ptr ptr ptr ptr ptr ptr ptr ptr) ChpeAutoRtlSelfRelativeToAbsoluteSD
1192 stdcall -version=0x600+ RtlSendMsgToSm(ptr ptr) ChpeStubRtlSendMsgToSm
1193 stdcall RtlSetAllBits(ptr) ChpeAutoRtlSetAllBits
1194 stdcall RtlSetAttributesSecurityDescriptor(ptr long ptr) ChpeAutoRtlSetAttributesSecurityDescriptor
1195 stdcall RtlSetBits(ptr long long) ChpeAutoRtlSetBits
1196 stdcall RtlSetCfgTargetValidity(ptr long long ptr) ChpeAutoRtlSetCfgTargetValidity
1197 stdcall RtlSetControlSecurityDescriptor(ptr long long) ChpeAutoRtlSetControlSecurityDescriptor
1199 stdcall RtlSetCurrentDirectory_U(ptr) ChpeAutoRtlSetCurrentDirectory_U
1200 stdcall RtlSetCurrentEnvironment(wstr ptr) ChpeAutoRtlSetCurrentEnvironment
1201 stdcall -version=0x600+ RtlSetCurrentTransaction(ptr) ChpeStubRtlSetCurrentTransaction
1202 stdcall RtlSetDaclSecurityDescriptor(ptr long ptr long) ChpeAutoRtlSetDaclSecurityDescriptor
1203 stdcall -version=0x600+ RtlSetDynamicTimeZoneInformation(long) ChpeStubRtlSetDynamicTimeZoneInformation
1204 stdcall RtlSetEnvironmentStrings(wstr long) ChpeAutoRtlSetEnvironmentStrings
1205 stdcall -version=0x600+ RtlSetEnvironmentVar(ptr ptr long ptr long) ChpeStubRtlSetEnvironmentVar
1206 stdcall RtlSetEnvironmentVariable(ptr ptr ptr) ChpeAutoRtlSetEnvironmentVariable
1207 stdcall -version=0x600+ RtlSetExtendedFeaturesMask(ptr int64) ChpeAutoRtlSetExtendedFeaturesMask
1208 stdcall RtlSetGroupSecurityDescriptor(ptr ptr long) ChpeAutoRtlSetGroupSecurityDescriptor
1209 stdcall RtlSetHeapInformation(ptr long ptr ptr) ChpeAutoRtlSetHeapInformation
1210 stdcall RtlSetInformationAcl(ptr ptr long long) ChpeAutoRtlSetInformationAcl
1211 stdcall RtlSetIoCompletionCallback(long ptr long) ChpeAutoRtlSetIoCompletionCallback
1213 stdcall RtlSetLastWin32ErrorAndNtStatusFromNtStatus(long) ChpeAutoRtlSetLastWin32ErrorAndNtStatusFromNtStatus
1214 stdcall RtlSetMemoryStreamSize(ptr int64) ChpeAutoRtlSetMemoryStreamSize
1215 stdcall RtlSetOwnerSecurityDescriptor(ptr ptr long) ChpeAutoRtlSetOwnerSecurityDescriptor
1216 stdcall -version=0x600+ RtlSetProcessDebugInformation(ptr long ptr) ChpeStubRtlSetProcessDebugInformation
1217 stdcall -version=0x600+ RtlSetProcessPreferredUILanguages(long ptr ptr) ChpeAutoRtlSetProcessPreferredUILanguages
1218 cdecl RtlSetProcessIsCritical(long ptr long) ChpeAutoRtlSetProcessIsCritical
1219 stdcall RtlSetSaclSecurityDescriptor(ptr long ptr long) ChpeAutoRtlSetSaclSecurityDescriptor
1220 stdcall RtlSetSecurityDescriptorRMControl(ptr ptr) ChpeAutoRtlSetSecurityDescriptorRMControl
1221 stdcall RtlSetSecurityObject(long ptr ptr ptr ptr) ChpeAutoRtlSetSecurityObject
1222 stdcall RtlSetSecurityObjectEx(long ptr ptr long ptr ptr) ChpeAutoRtlSetSecurityObjectEx
1223 stdcall RtlSetThreadErrorMode(long ptr) ChpeAutoRtlSetThreadErrorMode
1224 cdecl RtlSetThreadIsCritical(long ptr long) ChpeAutoRtlSetThreadIsCritical
1225 stdcall RtlSetThreadPoolStartFunc(ptr ptr) ChpeAutoRtlSetThreadPoolStartFunc
1226 stdcall -version=0x600+ RtlSetThreadPreferredUILanguages(long ptr ptr) ChpeStubRtlSetThreadPreferredUILanguages
1227 stdcall RtlSetTimeZoneInformation(ptr) ChpeAutoRtlSetTimeZoneInformation
1228 stdcall RtlSetTimer(ptr ptr ptr ptr long long long) ChpeAutoRtlSetTimer
1229 stdcall RtlSetUnhandledExceptionFilter(ptr) ChpeAutoRtlSetUnhandledExceptionFilter
1230 stdcall RtlSetUserFlagsHeap(ptr long ptr long long) ChpeAutoRtlSetUserFlagsHeap
1231 stdcall RtlSetUserValueHeap(ptr long ptr ptr) ChpeAutoRtlSetUserValueHeap
1232 stdcall -version=0x600+ RtlSidDominates(long long ptr) ChpeStubRtlSidDominates
1233 stdcall -version=0x600+ RtlSidEqualLevel(long long ptr) ChpeStubRtlSidEqualLevel
1234 stdcall -version=0x600+ RtlSidHashInitialize(ptr long ptr) ChpeStubRtlSidHashInitialize
1235 stdcall -version=0x600+ RtlSidHashLookup(long ptr) ChpeStubRtlSidHashLookup
1236 stdcall -version=0x600+ RtlSidIsHigherLevel(long long ptr) ChpeStubRtlSidIsHigherLevel
1238 stdcall -version=0x600+ RtlSleepConditionVariableCS(ptr ptr ptr) ChpeAutoRtlSleepConditionVariableCS
1239 stdcall -version=0x600+ RtlSleepConditionVariableSRW(ptr ptr ptr long) ChpeAutoRtlSleepConditionVariableSRW
1240 stdcall RtlSplay(ptr) ChpeAutoRtlSplay
1241 stdcall RtlStartRXact(ptr) ChpeAutoRtlStartRXact
1242 stdcall RtlStatMemoryStream(ptr ptr long) ChpeAutoRtlStatMemoryStream
1243 stdcall RtlStringFromGUID(ptr ptr) ChpeAutoRtlStringFromGUID
1244 stdcall RtlSubAuthorityCountSid(ptr) ChpeAutoRtlSubAuthorityCountSid
1245 stdcall RtlSubAuthoritySid(ptr long) ChpeAutoRtlSubAuthoritySid
1246 stdcall RtlSubtreePredecessor(ptr) ChpeAutoRtlSubtreePredecessor
1247 stdcall RtlSubtreeSuccessor(ptr) ChpeAutoRtlSubtreeSuccessor
1248 stdcall RtlSystemTimeToLocalTime(ptr ptr) ChpeAutoRtlSystemTimeToLocalTime
1249 stdcall -version=0x600+ RtlTestBit(ptr long) ChpeAutoRtlTestBit
1250 stdcall RtlTimeFieldsToTime(ptr ptr) ChpeAutoRtlTimeFieldsToTime
1251 stdcall RtlTimeToElapsedTimeFields(long long) ChpeAutoRtlTimeToElapsedTimeFields
1252 stdcall RtlTimeToSecondsSince1970(ptr ptr) ChpeAutoRtlTimeToSecondsSince1970
1253 stdcall RtlTimeToSecondsSince1980(ptr ptr) ChpeAutoRtlTimeToSecondsSince1980
1254 stdcall RtlTimeToTimeFields(long long) ChpeAutoRtlTimeToTimeFields
1255 stdcall RtlTraceDatabaseAdd(ptr long ptr ptr) ChpeAutoRtlTraceDatabaseAdd
1256 stdcall RtlTraceDatabaseCreate(long ptr long long ptr) ChpeAutoRtlTraceDatabaseCreate
1257 stdcall RtlTraceDatabaseDestroy(ptr) ChpeAutoRtlTraceDatabaseDestroy
1258 stdcall RtlTraceDatabaseEnumerate(ptr ptr ptr) ChpeAutoRtlTraceDatabaseEnumerate
1259 stdcall RtlTraceDatabaseFind(ptr long ptr ptr) ChpeAutoRtlTraceDatabaseFind
1260 stdcall RtlTraceDatabaseLock(ptr) ChpeAutoRtlTraceDatabaseLock
1261 stdcall RtlTraceDatabaseUnlock(ptr) ChpeAutoRtlTraceDatabaseUnlock
1262 stdcall RtlTraceDatabaseValidate(ptr) ChpeAutoRtlTraceDatabaseValidate
1263 stdcall -version=0x600+ RtlTryAcquirePebLock() ChpeStubRtlTryAcquirePebLock
1267 stdcall RtlUnhandledExceptionFilter2(ptr long) ChpeAutoRtlUnhandledExceptionFilter2
1268 stdcall RtlUnhandledExceptionFilter(ptr) ChpeAutoRtlUnhandledExceptionFilter
1269 stdcall RtlUnicodeStringToAnsiSize(ptr) ChpeAutoRtlUnicodeStringToAnsiSize
1270 stdcall -version=0x601+ RtlUnicodeToUTF8N(ptr long ptr wstr long) ChpeAutoRtlUnicodeToUTF8N
1271 stdcall -version=0xA00+ RtlUTF8StringToUnicodeString(ptr ptr long) ChpeAutoRtlUTF8StringToUnicodeString
1273 stdcall RtlUnicodeStringToAnsiString(ptr ptr long) ChpeAutoRtlUnicodeStringToAnsiString
1274 stdcall RtlUnicodeStringToCountedOemString(ptr ptr long) ChpeAutoRtlUnicodeStringToCountedOemString
1275 stdcall RtlUnicodeStringToInteger(ptr long ptr) ChpeAutoRtlUnicodeStringToInteger
1276 stdcall RtlUnicodeStringToOemSize(ptr) ChpeAutoRtlUnicodeStringToOemSize
1277 stdcall RtlUnicodeStringToOemString(ptr ptr long) ChpeAutoRtlUnicodeStringToOemString
1278 stdcall RtlUnicodeToCustomCPN(ptr ptr long ptr wstr long) ChpeAutoRtlUnicodeToCustomCPN
1279 stdcall RtlUnicodeToMultiByteN(ptr long ptr ptr long) ChpeAutoRtlUnicodeToMultiByteN
1280 stdcall RtlUnicodeToMultiByteSize(ptr ptr long) ChpeAutoRtlUnicodeToMultiByteSize
1281 stdcall RtlUnicodeToOemN(ptr long ptr ptr long) ChpeAutoRtlUnicodeToOemN
1282 stdcall RtlUniform(ptr) ChpeAutoRtlUniform
1283 stdcall RtlUnlockBootStatusData(ptr) ChpeAutoRtlUnlockBootStatusData
1284 stdcall -version=0x600+ RtlUnlockCurrentThread() ChpeStubRtlUnlockCurrentThread
1285 stdcall RtlUnlockHeap(long) ChpeAutoRtlUnlockHeap
1286 stdcall -version=0x600+ RtlUnlockMemoryBlockLookaside(long) ChpeStubRtlUnlockMemoryBlockLookaside
1287 stdcall RtlUnlockMemoryStreamRegion(ptr int64 int64 long) ChpeAutoRtlUnlockMemoryStreamRegion
1288 stdcall -version=0x600+ RtlUnlockMemoryZone(long) ChpeStubRtlUnlockMemoryZone
1289 stdcall -version=0x600+ RtlUnlockModuleSection(long) ChpeStubRtlUnlockModuleSection
1292 stdcall RtlUnregisterCfgTargetRange(ptr) ChpeAutoRtlUnregisterCfgTargetRange
1293 stdcall RtlUpcaseUnicodeChar(long) ChpeAutoRtlUpcaseUnicodeChar
1294 stdcall RtlUpcaseUnicodeString(ptr ptr long) ChpeAutoRtlUpcaseUnicodeString
1295 stdcall RtlUpcaseUnicodeStringToAnsiString(ptr ptr long) ChpeAutoRtlUpcaseUnicodeStringToAnsiString
1296 stdcall RtlUpcaseUnicodeStringToCountedOemString(ptr ptr long) ChpeAutoRtlUpcaseUnicodeStringToCountedOemString
1297 stdcall RtlUpcaseUnicodeStringToOemString(ptr ptr long) ChpeAutoRtlUpcaseUnicodeStringToOemString
1298 stdcall RtlUpcaseUnicodeToCustomCPN(ptr ptr long ptr wstr long) ChpeAutoRtlUpcaseUnicodeToCustomCPN
1299 stdcall RtlUpcaseUnicodeToMultiByteN(ptr long ptr ptr long) ChpeAutoRtlUpcaseUnicodeToMultiByteN
1300 stdcall RtlUpcaseUnicodeToOemN(ptr long ptr ptr long) ChpeAutoRtlUpcaseUnicodeToOemN
1301 stdcall -version=0x600+ RtlUpdateClonedCriticalSection(long) ChpeStubRtlUpdateClonedCriticalSection
1302 stdcall -version=0x600+ RtlUpdateClonedSRWLock(ptr long) ChpeStubRtlUpdateClonedSRWLock
1303 stdcall RtlUpdateTimer(ptr ptr long long) ChpeAutoRtlUpdateTimer
1304 stdcall RtlUpperChar(long) ChpeAutoRtlUpperChar
1305 stdcall RtlUpperString(ptr ptr) ChpeAutoRtlUpperString
1307 stdcall RtlValidAcl(ptr) ChpeAutoRtlValidAcl
1308 stdcall RtlValidRelativeSecurityDescriptor(ptr long long) ChpeAutoRtlValidRelativeSecurityDescriptor
1309 stdcall RtlValidSecurityDescriptor(ptr) ChpeAutoRtlValidSecurityDescriptor
1310 stdcall RtlValidSid(ptr) ChpeAutoRtlValidSid
1311 stdcall RtlValidateHeap(long long ptr) ChpeAutoRtlValidateHeap
1312 stdcall RtlValidateProcessHeaps() ChpeAutoRtlValidateProcessHeaps
1313 stdcall RtlValidateUnicodeString(long ptr) ChpeAutoRtlValidateUnicodeString
1314 stdcall RtlVerifyVersionInfo(ptr long double) ChpeAutoRtlVerifyVersionInfo
1316 stdcall RtlVirtualUnwind2(long int64 int64 ptr ptr ptr ptr ptr ptr ptr ptr ptr long) ChpeAutoRtlVirtualUnwind2
1322 stdcall RtlWalkFrameChain(ptr long long) ChpeAutoRtlWalkFrameChain
1323 stdcall RtlWeaklyEnumerateEntryHashTable(ptr ptr) ChpeAutoRtlWeaklyEnumerateEntryHashTable
1324 stdcall RtlWalkHeap(long ptr) ChpeAutoRtlWalkHeap
1325 stdcall -version=0x600+ RtlWerpReportException(long long ptr long long ptr) ChpeStubRtlWerpReportException
1326 stdcall -version=0x600+ RtlWow64CallFunction64() ChpeStubRtlWow64CallFunction64
1327 stdcall RtlWow64EnableFsRedirection(long) ChpeAutoRtlWow64EnableFsRedirection
1328 stdcall RtlWow64EnableFsRedirectionEx(long ptr) ChpeAutoRtlWow64EnableFsRedirectionEx
1329 stdcall -version=0x600+ RtlOpenCrossProcessEmulatorWorkConnection(ptr ptr ptr) ChpeAutoRtlOpenCrossProcessEmulatorWorkConnection
1330 stdcall -version=0x600+ RtlWow64GetCpuAreaInfo(ptr long ptr) ChpeAutoRtlWow64GetCpuAreaInfo
1331 stdcall -version=0x600+ RtlWow64GetCurrentMachine() ChpeAutoRtlWow64GetCurrentMachine
1332 stdcall -version=0x600+ RtlWow64GetProcessMachines(ptr ptr ptr) ChpeAutoRtlWow64GetProcessMachines
1333 stdcall -version=0x600+ RtlWow64GetSharedInfoProcess(ptr ptr ptr) ChpeAutoRtlWow64GetSharedInfoProcess
1334 stdcall -version=0x600+ RtlWow64GetThreadContext(ptr ptr) ChpeAutoRtlWow64GetThreadContext
1336 stdcall -version=0x600+ RtlWow64IsWowGuestMachineSupported(long ptr) ChpeAutoRtlWow64IsWowGuestMachineSupported
1337 stdcall -version=0x600+ RtlWow64LogMessageInEventLogger(long long long) ChpeStubRtlWow64LogMessageInEventLogger
1338 stdcall -version=0x600+ RtlWow64PopCrossProcessWorkFromFreeList(ptr) ChpeAutoRtlWow64PopCrossProcessWorkFromFreeList
1339 stdcall -version=0x600+ RtlWow64PushCrossProcessWorkOntoWorkList(ptr ptr ptr) ChpeAutoRtlWow64PushCrossProcessWorkOntoWorkList
1340 stdcall -version=0x600+ RtlWow64RequestCrossProcessHeavyFlush(ptr) ChpeAutoRtlWow64RequestCrossProcessHeavyFlush
1341 stdcall -version=0x600+ RtlWow64SetThreadContext(ptr ptr) ChpeAutoRtlWow64SetThreadContext
1342 stdcall -version=0x600+ RtlWow64SuspendThread(ptr ptr) ChpeAutoRtlWow64SuspendThread
1343 stdcall RtlWriteMemoryStream(ptr ptr long ptr) ChpeAutoRtlWriteMemoryStream
1344 stdcall RtlWriteRegistryValue(long ptr ptr long ptr long) ChpeAutoRtlWriteRegistryValue
1345 stdcall RtlZeroHeap(ptr long) ChpeAutoRtlZeroHeap
1347 stdcall RtlZombifyActivationContext(ptr) ChpeAutoRtlZombifyActivationContext
1349 stdcall -version=0x600+ RtlpCheckDynamicTimeZoneInformation(ptr long) ChpeStubRtlpCheckDynamicTimeZoneInformation
1350 stdcall -version=0x600+ RtlpCleanupRegistryKeys() ChpeStubRtlpCleanupRegistryKeys
1351 stdcall -version=0x600+ RtlpConvertCultureNamesToLCIDs(wstr ptr) ChpeStubRtlpConvertCultureNamesToLCIDs
1352 stdcall -version=0x600+ RtlpConvertLCIDsToCultureNames(wstr ptr) ChpeStubRtlpConvertLCIDsToCultureNames
1353 stdcall -version=0x600+ RtlpCreateProcessRegistryInfo(ptr) ChpeStubRtlpCreateProcessRegistryInfo
1354 stdcall RtlpEnsureBufferSize(long ptr long) ChpeAutoRtlpEnsureBufferSize
1355 stdcall -version=0x600+ RtlpGetLCIDFromLangInfoNode(long long ptr) ChpeStubRtlpGetLCIDFromLangInfoNode
1356 stdcall -version=0x600+ RtlpGetNameFromLangInfoNode(long long long) ChpeStubRtlpGetNameFromLangInfoNode
1357 stdcall -version=0x600+ RtlpGetSystemDefaultUILanguage(ptr long) ChpeStubRtlpGetSystemDefaultUILanguage
1358 stdcall -version=0x600+ RtlpGetUserOrMachineUILanguage4NLS(long long ptr) ChpeStubRtlpGetUserOrMachineUILanguage4NLS
1359 stdcall -version=0x600+ RtlpInitializeLangRegistryInfo(ptr) ChpeStubRtlpInitializeLangRegistryInfo
1360 stdcall -version=0x600+ RtlpIsQualifiedLanguage(long ptr long) ChpeStubRtlpIsQualifiedLanguage
1361 stdcall -version=0x600+ RtlpLoadMachineUIByPolicy(ptr long ptr) ChpeStubRtlpLoadMachineUIByPolicy
1362 stdcall -version=0x600+ RtlpLoadUserUIByPolicy(ptr long ptr) ChpeStubRtlpLoadUserUIByPolicy
1363 stdcall -version=0x600+ RtlpMuiFreeLangRegistryInfo(long) ChpeStubRtlpMuiFreeLangRegistryInfo
1364 stdcall -version=0x600+ RtlpMuiRegCreateRegistryInfo() ChpeStubRtlpMuiRegCreateRegistryInfo
1365 stdcall -version=0x600+ RtlpMuiRegFreeRegistryInfo(long long) ChpeStubRtlpMuiRegFreeRegistryInfo
1366 stdcall -version=0x600+ RtlpMuiRegLoadRegistryInfo(long long) ChpeStubRtlpMuiRegLoadRegistryInfo
1367 stdcall RtlpNotOwnerCriticalSection(ptr) ChpeAutoRtlpNotOwnerCriticalSection
1368 stdcall RtlpNtCreateKey(ptr long ptr long ptr ptr) ChpeAutoRtlpNtCreateKey
1369 stdcall RtlpNtEnumerateSubKey(ptr ptr long long) ChpeAutoRtlpNtEnumerateSubKey
1370 stdcall RtlpNtMakeTemporaryKey(ptr) ChpeAutoRtlpNtMakeTemporaryKey
1371 stdcall RtlpNtOpenKey(ptr long ptr long) ChpeAutoRtlpNtOpenKey
1372 stdcall RtlpNtQueryValueKey(ptr ptr ptr ptr long) ChpeAutoRtlpNtQueryValueKey
1373 stdcall RtlpNtSetValueKey(ptr long ptr long) ChpeAutoRtlpNtSetValueKey
1374 stdcall -version=0x600+ RtlpQueryDefaultUILanguage(ptr long) ChpeStubRtlpQueryDefaultUILanguage
1375 stdcall -version=0x600+ RtlpQueryProcessDebugInformationFromWow64(long ptr) ChpeStubRtlpQueryProcessDebugInformationFromWow64
1376 stdcall -version=0x600+ RtlpRefreshCachedUILanguage(wstr long) ChpeStubRtlpRefreshCachedUILanguage
1377 stdcall -version=0x600+ RtlpSetInstallLanguage(long ptr) ChpeStubRtlpSetInstallLanguage
1378 stdcall -version=0x600+ RtlpSetPreferredUILanguages(long ptr ptr) ChpeStubRtlpSetPreferredUILanguages
1379 stdcall RtlpUnWaitCriticalSection(ptr) ChpeAutoRtlpUnWaitCriticalSection
1380 stdcall -version=0x600+ RtlpVerifyAndCommitUILanguageSettings(long) ChpeStubRtlpVerifyAndCommitUILanguageSettings
1381 stdcall RtlpWaitForCriticalSection(ptr) ChpeAutoRtlpWaitForCriticalSection
1382 stdcall RtlxAnsiStringToUnicodeSize(ptr) ChpeAutoRtlxAnsiStringToUnicodeSize
1383 stdcall RtlxOemStringToUnicodeSize(ptr) ChpeAutoRtlxOemStringToUnicodeSize
1384 stdcall RtlxUnicodeStringToAnsiSize(ptr) ChpeAutoRtlxUnicodeStringToAnsiSize
1385 stdcall RtlxUnicodeStringToOemSize(ptr) ChpeAutoRtlxUnicodeStringToOemSize
1386 stdcall -version=0x600+ ShipAssert(long long) ChpeStubShipAssert
1387 stdcall -version=0x600+ ShipAssertGetBufferInfo(ptr ptr) ChpeStubShipAssertGetBufferInfo
1388 stdcall -version=0x600+ ShipAssertMsgA(long long) ChpeStubShipAssertMsgA
1389 stdcall -version=0x600+ ShipAssertMsgW(long long) ChpeStubShipAssertMsgW
1390 stdcall -version=0x600+ TpAllocAlpcCompletion(ptr ptr ptr ptr ptr) ChpeAutoTpAllocAlpcCompletion
1391 stdcall -version=0x600+ TpAllocAlpcCompletionEx(ptr ptr ptr ptr ptr) ChpeAutoTpAllocAlpcCompletionEx
1392 stdcall -version=0x600+ TpAllocCleanupGroup(ptr) ChpeAutoTpAllocCleanupGroup
1393 stdcall -version=0x600+ TpAllocIoCompletion(ptr ptr ptr ptr ptr) ChpeAutoTpAllocIoCompletion
1394 stdcall -version=0x600+ TpAllocPool(ptr ptr) ChpeAutoTpAllocPool
1395 stdcall -version=0x600+ TpAllocTimer(ptr ptr ptr ptr) ChpeAutoTpAllocTimer
1396 stdcall -version=0x600+ TpAllocWait(ptr ptr ptr ptr) ChpeAutoTpAllocWait
1397 stdcall -version=0x600+ TpAllocWork(ptr ptr ptr ptr) ChpeAutoTpAllocWork
1398 stdcall -version=0x600+ TpAlpcRegisterCompletionList(ptr) ChpeAutoTpAlpcRegisterCompletionList
1399 stdcall -version=0x600+ TpAlpcUnregisterCompletionList(ptr) ChpeAutoTpAlpcUnregisterCompletionList
1401 stdcall -version=0x600+ TpCallbackMayRunLong(ptr) ChpeAutoTpCallbackMayRunLong
1404 stdcall -version=0x600+ TpCallbackSendAlpcMessageOnCompletion(ptr ptr long ptr) ChpeAutoTpCallbackSendAlpcMessageOnCompletion
1405 stdcall -version=0x600+ TpCallbackSendPendingAlpcMessage(ptr) ChpeAutoTpCallbackSendPendingAlpcMessage
1409 stdcall -version=0x600+ TpCaptureCaller(long) ChpeStubTpCaptureCaller
1410 stdcall -version=0x600+ TpCheckTerminateWorker(ptr) ChpeStubTpCheckTerminateWorker
1411 stdcall -version=0x600+ TpDbgDumpHeapUsage(long ptr long) ChpeStubTpDbgDumpHeapUsage
1412 stdcall -version=0x600+ TpDbgSetLogRoutine() ChpeStubTpDbgSetLogRoutine
1416 stdcall -version=0x601+ TpQueryPoolStackInformation(ptr ptr) ChpeAutoTpQueryPoolStackInformation
1417 stdcall -version=0x600+ TpReleaseAlpcCompletion(ptr) ChpeAutoTpReleaseAlpcCompletion
1427 stdcall -version=0x601+ TpSetPoolStackInformation(ptr ptr) ChpeAutoTpSetPoolStackInformation
1432 stdcall -version=0x600+ TpSimpleTryPost(ptr ptr ptr) ChpeAutoTpSimpleTryPost
1434 stdcall -version=0x600+ TpWaitForAlpcCompletion(ptr) ChpeAutoTpWaitForAlpcCompletion
1440 stdcall -version=0x600+ WerCheckEventEscalation(long ptr) ChpeStubWerCheckEventEscalation
1441 stdcall -version=0x600+ WerReportSQMEvent(long long long) ChpeStubWerReportSQMEvent
1442 stdcall -version=0x600+ WerReportWatsonEvent(long long long long) ChpeStubWerReportWatsonEvent
1443 stdcall -version=0x600+ WinSqmAddToStream(ptr long long long) ChpeStubWinSqmAddToStream
1444 stdcall -version=0x600+ WinSqmAddToStreamEx(ptr long long ptr long) ChpeStubWinSqmAddToStreamEx
1445 stdcall -version=0x600+ WinSqmEndSession(ptr) ChpeStubWinSqmEndSession
1446 stdcall -version=0x600+ WinSqmEventEnabled(long ptr) ChpeAutoWinSqmEventEnabled
1447 stdcall -version=0x600+ WinSqmEventWrite(long long long) ChpeStubWinSqmEventWrite
1448 stdcall -version=0x600+ WinSqmIncrementDWORD(long long long) ChpeAutoWinSqmIncrementDWORD
1449 stdcall -version=0x600+ WinSqmIsOptedIn() ChpeAutoWinSqmIsOptedIn
1450 stdcall -version=0x600+ WinSqmSetDWORD(ptr long long) ChpeAutoWinSqmSetDWORD
1451 stdcall -version=0x600+ WinSqmSetDWORD64(ptr long int64) ChpeAutoWinSqmSetDWORD64
1452 stdcall -version=0x600+ WinSqmSetString(ptr long ptr) ChpeStubWinSqmSetString
1453 stdcall -version=0x600+ WinSqmStartSession(ptr) ChpeStubWinSqmStartSession
1454 stdcall ZwAcceptConnectPort(ptr long ptr long long ptr) ChpeAutoZwAcceptConnectPort
1455 stdcall ZwAccessCheck(ptr long long ptr ptr ptr ptr ptr) ChpeAutoZwAccessCheck
1456 stdcall ZwAccessCheckAndAuditAlarm(ptr long ptr ptr ptr long ptr long ptr ptr ptr) ChpeAutoZwAccessCheckAndAuditAlarm
1457 stdcall ZwAccessCheckByType(ptr ptr ptr long ptr long ptr ptr long ptr ptr) ChpeAutoZwAccessCheckByType
1458 stdcall ZwAccessCheckByTypeAndAuditAlarm(ptr ptr ptr ptr ptr ptr long long long ptr long ptr long ptr ptr ptr) ChpeAutoZwAccessCheckByTypeAndAuditAlarm
1459 stdcall ZwAccessCheckByTypeResultList(ptr ptr ptr long ptr long ptr ptr long ptr ptr) ChpeAutoZwAccessCheckByTypeResultList
1460 stdcall ZwAccessCheckByTypeResultListAndAuditAlarm(ptr ptr ptr ptr ptr ptr long long long ptr long ptr long ptr ptr ptr) ChpeAutoZwAccessCheckByTypeResultListAndAuditAlarm
1461 stdcall ZwAccessCheckByTypeResultListAndAuditAlarmByHandle(ptr ptr ptr ptr ptr ptr ptr long long long ptr long ptr long ptr ptr ptr) ChpeAutoZwAccessCheckByTypeResultListAndAuditAlarmByHandle
1462 stub -version=0x600+ ZwAcquireCMFViewOwnership
1463 stdcall ZwAddAtom(ptr long ptr) ChpeAutoZwAddAtom
1464 stdcall ZwAddBootEntry(ptr long) ChpeAutoZwAddBootEntry
1465 stdcall ZwAddDriverEntry(ptr long) ChpeAutoZwAddDriverEntry
1466 stdcall ZwAdjustGroupsToken(long long long long long long) ChpeAutoZwAdjustGroupsToken
1467 stdcall ZwAdjustPrivilegesToken(long long long long long long) ChpeAutoZwAdjustPrivilegesToken
1468 stdcall ZwAlertResumeThread(long ptr) ChpeAutoZwAlertResumeThread
1469 stdcall ZwAlertThread(long) ChpeAutoZwAlertThread
1470 stdcall ZwAlertThreadByThreadId(long) ChpeAutoZwAlertThreadByThreadId
1471 stdcall ZwAllocateLocallyUniqueId(ptr) ChpeAutoZwAllocateLocallyUniqueId
1472 stdcall ZwAllocateUserPhysicalPages(ptr ptr ptr) ChpeAutoZwAllocateUserPhysicalPages
1473 stdcall ZwAllocateUuids(ptr ptr ptr ptr) ChpeAutoZwAllocateUuids
1474 stdcall ZwAllocateVirtualMemory(long ptr ptr ptr long long) ChpeAutoZwAllocateVirtualMemory
1475 stdcall ZwAllocateVirtualMemoryEx(long ptr ptr long long ptr long) ChpeAutoZwAllocateVirtualMemoryEx
1476 stdcall -version=0x600+ ZwAlpcAcceptConnectPort(ptr ptr long ptr ptr ptr ptr ptr long) ChpeAutoZwAlpcAcceptConnectPort
1477 stdcall -version=0x600+ ZwAlpcCancelMessage(ptr long ptr) ChpeAutoZwAlpcCancelMessage
1478 stdcall -version=0x600+ ZwAlpcConnectPort(ptr ptr ptr ptr long ptr ptr ptr ptr ptr ptr) ChpeAutoZwAlpcConnectPort
1479 stdcall -version=0x602+ ZwAlpcConnectPortEx(ptr ptr ptr ptr long ptr ptr ptr ptr ptr ptr) ChpeAutoZwAlpcConnectPortEx
1480 stdcall -version=0x600+ ZwAlpcCreatePort(ptr ptr ptr) ChpeAutoZwAlpcCreatePort
1481 stdcall -version=0x600+ ZwAlpcCreatePortSection(ptr long ptr long ptr ptr) ChpeAutoZwAlpcCreatePortSection
1482 stdcall -version=0x600+ ZwAlpcCreateResourceReserve(ptr long long ptr) ChpeAutoZwAlpcCreateResourceReserve
1483 stdcall -version=0x600+ ZwAlpcCreateSectionView(ptr long ptr) ChpeAutoZwAlpcCreateSectionView
1484 stdcall -version=0x600+ ZwAlpcCreateSecurityContext(ptr long ptr) ChpeAutoZwAlpcCreateSecurityContext
1485 stdcall -version=0x600+ ZwAlpcDeletePortSection(ptr long ptr) ChpeAutoZwAlpcDeletePortSection
1486 stdcall -version=0x600+ ZwAlpcDeleteResourceReserve(ptr long long) ChpeAutoZwAlpcDeleteResourceReserve
1487 stdcall -version=0x600+ ZwAlpcDeleteSectionView(ptr long ptr) ChpeAutoZwAlpcDeleteSectionView
1488 stdcall -version=0x600+ ZwAlpcDeleteSecurityContext(ptr long ptr) ChpeAutoZwAlpcDeleteSecurityContext
1489 stdcall -version=0x600+ ZwAlpcDisconnectPort(ptr long) ChpeAutoZwAlpcDisconnectPort
1490 stdcall -version=0x600+ ZwAlpcImpersonateClientOfPort(ptr ptr ptr) ChpeAutoZwAlpcImpersonateClientOfPort
1491 stdcall -version=0xA00+ ZwAlpcImpersonateClientContainerOfPort(ptr ptr long) ChpeAutoZwAlpcImpersonateClientContainerOfPort
1492 stdcall -version=0x600+ ZwAlpcOpenSenderProcess(ptr ptr ptr long long ptr) ChpeAutoZwAlpcOpenSenderProcess
1493 stdcall -version=0x600+ ZwAlpcOpenSenderThread(ptr ptr ptr long long ptr) ChpeAutoZwAlpcOpenSenderThread
1494 stdcall -version=0x600+ ZwAlpcQueryInformation(ptr long ptr long ptr) ChpeAutoZwAlpcQueryInformation
1495 stdcall -version=0x600+ ZwAlpcQueryInformationMessage(ptr ptr long ptr long ptr) ChpeAutoZwAlpcQueryInformationMessage
1496 stdcall -version=0x600+ ZwAlpcRevokeSecurityContext(ptr long ptr) ChpeAutoZwAlpcRevokeSecurityContext
1497 stdcall -version=0x600+ ZwAlpcSendWaitReceivePort(ptr long ptr ptr ptr ptr ptr ptr) ChpeAutoZwAlpcSendWaitReceivePort
1498 stdcall -version=0x600+ ZwAlpcSetInformation(ptr long ptr long) ChpeAutoZwAlpcSetInformation
1499 stdcall ZwApphelpCacheControl(long ptr) ChpeAutoZwApphelpCacheControl
1500 stdcall ZwAreMappedFilesTheSame(ptr ptr) ChpeAutoZwAreMappedFilesTheSame
1501 stdcall ZwAssignProcessToJobObject(long long) ChpeAutoZwAssignProcessToJobObject
1502 stdcall ZwCallbackReturn(ptr long long) ChpeAutoZwCallbackReturn
1503 stdcall ZwCancelDeviceWakeupRequest(ptr) ChpeAutoZwCancelDeviceWakeupRequest
1504 stdcall ZwCancelIoFile(long ptr) ChpeAutoZwCancelIoFile
1505 stdcall -version=0x600+ ZwCancelIoFileEx(ptr ptr ptr) ChpeAutoZwCancelIoFileEx
1506 stdcall -version=0x600+ ZwCancelSynchronousIoFile(ptr ptr ptr) ChpeAutoZwCancelSynchronousIoFile
1507 stdcall ZwCancelTimer(long ptr) ChpeAutoZwCancelTimer
1508 stdcall ZwClearEvent(long) ChpeAutoZwClearEvent
1509 stdcall ZwClose(long) ChpeAutoZwClose
1510 stdcall ZwCloseObjectAuditAlarm(ptr ptr long) ChpeAutoZwCloseObjectAuditAlarm
1511 stdcall -version=0x600+ ZwCommitComplete(ptr ptr) ChpeAutoZwCommitComplete
1512 stdcall -version=0x600+ ZwCommitEnlistment(ptr ptr) ChpeAutoZwCommitEnlistment
1513 stdcall -version=0x600+ ZwCommitTransaction(ptr long) ChpeAutoZwCommitTransaction
1514 stdcall ZwCompactKeys(long ptr) ChpeAutoZwCompactKeys
1515 stdcall ZwCompareTokens(ptr ptr ptr) ChpeAutoZwCompareTokens
1516 stdcall ZwCompleteConnectPort(ptr) ChpeAutoZwCompleteConnectPort
1517 stdcall ZwCompressKey(ptr) ChpeAutoZwCompressKey
1518 stdcall ZwConnectPort(ptr ptr ptr ptr ptr ptr ptr ptr) ChpeAutoZwConnectPort
1519 stdcall ZwContinue(ptr long) ChpeAutoZwContinue
1520 stdcall ZwCreateDebugObject(ptr long ptr long) ChpeAutoZwCreateDebugObject
1521 stdcall ZwCreateDirectoryObject(long long long) ChpeAutoZwCreateDirectoryObject
1522 stdcall -version=0x600+ ZwCreateEnlistment(ptr long ptr ptr ptr long long ptr) ChpeAutoZwCreateEnlistment
1523 stdcall ZwCreateEvent(long long long long long) ChpeAutoZwCreateEvent
1524 stdcall ZwCreateEventPair(ptr long ptr) ChpeAutoZwCreateEventPair
1525 stdcall ZwCreateFile(ptr long ptr ptr long long long ptr long long ptr) ChpeAutoZwCreateFile
1526 stdcall ZwCreateIoCompletion(ptr long ptr long) ChpeAutoZwCreateIoCompletion
1527 stdcall -version=0x602+ ZwCreateWaitCompletionPacket(ptr long ptr) ChpeAutoZwCreateWaitCompletionPacket
1528 stdcall -version=0x602+ ZwAssociateWaitCompletionPacket(ptr ptr ptr ptr ptr long ptr ptr) ChpeAutoZwAssociateWaitCompletionPacket
1529 stdcall -version=0x602+ ZwCancelWaitCompletionPacket(ptr long) ChpeAutoZwCancelWaitCompletionPacket
1530 stdcall ZwCreateJobObject(ptr long ptr) ChpeAutoZwCreateJobObject
1531 stdcall ZwCreateJobSet(long ptr long) ChpeAutoZwCreateJobSet
1532 stdcall ZwCreateKey(ptr long ptr long ptr long long) ChpeAutoZwCreateKey
1533 stdcall -version=0x600+ ZwCreateKeyTransacted(ptr long ptr long ptr long ptr ptr) ChpeAutoZwCreateKeyTransacted
1534 stdcall ZwCreateKeyedEvent(ptr long ptr long) ChpeAutoZwCreateKeyedEvent
1535 stdcall ZwCreateMailslotFile(long long long long long long long long) ChpeAutoZwCreateMailslotFile
1536 stdcall ZwCreateMutant(ptr long ptr long) ChpeAutoZwCreateMutant
1537 stdcall ZwCreateNamedPipeFile(ptr long ptr ptr long long long long long long long long long ptr) ChpeAutoZwCreateNamedPipeFile
1538 stdcall ZwCreatePagingFile(ptr ptr ptr long) ChpeAutoZwCreatePagingFile
1539 stdcall ZwCreatePort(ptr ptr long long long) ChpeAutoZwCreatePort
1540 stdcall ZwCreateProcess(ptr long ptr ptr long ptr ptr ptr) ChpeAutoZwCreateProcess
1541 stdcall ZwCreateProcessEx(ptr long ptr ptr long ptr ptr ptr long) ChpeAutoZwCreateProcessEx
1542 stdcall ZwCreateProfile(ptr ptr ptr long long ptr long long long) ChpeAutoZwCreateProfile
1543 stdcall -version=0x600+ ZwCreateResourceManager(ptr long ptr ptr ptr long ptr) ChpeAutoZwCreateResourceManager
1544 stdcall ZwCreateSection(ptr long ptr ptr long long long) ChpeAutoZwCreateSection
1545 stdcall ZwCreateSemaphore(ptr long ptr long long) ChpeAutoZwCreateSemaphore
1546 stdcall ZwCreateSymbolicLinkObject(ptr long ptr ptr) ChpeAutoZwCreateSymbolicLinkObject
1547 stdcall ZwCreateThread(ptr long ptr ptr ptr ptr ptr long) ChpeAutoZwCreateThread
1548 stdcall -version=0x600+ ZwCreateThreadEx(ptr long ptr ptr ptr ptr long long long long ptr) ChpeAutoZwCreateThreadEx
1549 stdcall ZwCreateTimer(ptr long ptr long) ChpeAutoZwCreateTimer
1550 stdcall ZwCreateToken(ptr long ptr long ptr ptr ptr ptr ptr ptr ptr ptr ptr) ChpeAutoZwCreateToken
1551 stdcall -version=0x600+ ZwCreateTransaction(ptr long ptr ptr ptr long long long ptr ptr) ChpeAutoZwCreateTransaction
1552 stdcall -version=0x600+ ZwCreateTransactionManager(ptr long ptr ptr long long) ChpeAutoZwCreateTransactionManager
1553 stdcall -version=0x600+ ZwCreateUserProcess(ptr ptr long long ptr ptr long long ptr ptr ptr) ChpeAutoZwCreateUserProcess
1554 stdcall ZwCreateWaitablePort(ptr ptr long long long) ChpeAutoZwCreateWaitablePort
1555 stdcall -version=0x602+ ZwCreateWnfStateName(ptr long long long ptr long ptr) ChpeAutoZwCreateWnfStateName
1556 stdcall -version=0x600+ ZwCreateWorkerFactory(ptr long ptr ptr ptr ptr ptr long long long) ChpeStubZwCreateWorkerFactory
1557 stdcall ZwDebugActiveProcess(ptr ptr) ChpeAutoZwDebugActiveProcess
1558 stdcall ZwDebugContinue(ptr ptr long) ChpeAutoZwDebugContinue
1559 stdcall ZwDelayExecution(long ptr) ChpeAutoZwDelayExecution
1560 stdcall ZwDeleteAtom(long) ChpeAutoZwDeleteAtom
1561 stdcall ZwDeleteBootEntry(long) ChpeAutoZwDeleteBootEntry
1562 stdcall ZwDeleteDriverEntry(long) ChpeAutoZwDeleteDriverEntry
1563 stdcall ZwDeleteFile(ptr) ChpeAutoZwDeleteFile
1564 stdcall ZwDeleteKey(long) ChpeAutoZwDeleteKey
1565 stdcall ZwDeleteObjectAuditAlarm(ptr ptr long) ChpeAutoZwDeleteObjectAuditAlarm
1566 stdcall -version=0x600+ ZwDeletePrivateNamespace(ptr) ChpeAutoZwDeletePrivateNamespace
1567 stdcall ZwDeleteValueKey(long ptr) ChpeAutoZwDeleteValueKey
1568 stdcall -version=0x602+ ZwDeleteWnfStateData(ptr ptr) ChpeAutoZwDeleteWnfStateData
1569 stdcall -version=0x602+ ZwDeleteWnfStateName(ptr) ChpeAutoZwDeleteWnfStateName
1570 stdcall ZwDeviceIoControlFile(long long long long long long long long long long) ChpeAutoZwDeviceIoControlFile
1571 stdcall ZwDisplayString(ptr) ChpeAutoZwDisplayString
1572 stdcall ZwDuplicateObject(long long long ptr long long long) ChpeAutoZwDuplicateObject
1573 stdcall ZwDuplicateToken(long long long long long long) ChpeAutoZwDuplicateToken
1574 stdcall ZwEnumerateBootEntries(ptr ptr) ChpeAutoZwEnumerateBootEntries
1575 stdcall ZwEnumerateDriverEntries(ptr ptr) ChpeAutoZwEnumerateDriverEntries
1576 stdcall ZwEnumerateKey(long long long ptr long ptr) ChpeAutoZwEnumerateKey
1577 stdcall ZwEnumerateSystemEnvironmentValuesEx(long ptr long) ChpeAutoZwEnumerateSystemEnvironmentValuesEx
1578 stdcall -version=0x600+ ZwEnumerateTransactionObject(ptr long ptr long ptr) ChpeAutoZwEnumerateTransactionObject
1579 stdcall ZwEnumerateValueKey(long long long ptr long ptr) ChpeAutoZwEnumerateValueKey
1580 stdcall ZwExtendSection(ptr ptr) ChpeAutoZwExtendSection
1581 stdcall ZwFilterToken(ptr long ptr ptr ptr ptr) ChpeAutoZwFilterToken
1582 stdcall -version=0x602+ ZwCreateLowBoxToken(ptr ptr long ptr ptr long ptr long ptr) ChpeAutoZwCreateLowBoxToken
1583 stdcall -version=0x600+ ZwCreatePrivateNamespace(ptr long ptr ptr) ChpeAutoZwCreatePrivateNamespace
1584 stdcall ZwFindAtom(ptr long ptr) ChpeAutoZwFindAtom
1585 stdcall ZwFlushBuffersFile(long ptr) ChpeAutoZwFlushBuffersFile
1586 stdcall ZwFlushBuffersFileEx(long long ptr long ptr) ChpeAutoZwFlushBuffersFileEx
1587 stdcall -version=0x600+ ZwFlushInstallUILanguage(long long) ChpeStubZwFlushInstallUILanguage
1588 stdcall ZwFlushInstructionCache(long ptr long) ChpeAutoZwFlushInstructionCache
1589 stdcall ZwFlushKey(long) ChpeAutoZwFlushKey
1590 stdcall -version=0x600+ ZwFlushProcessWriteBuffers() ChpeAutoZwFlushProcessWriteBuffers
1591 stdcall ZwFlushVirtualMemory(ptr ptr ptr ptr) ChpeAutoZwFlushVirtualMemory
1592 stdcall ZwFlushWriteBuffer() ChpeAutoZwFlushWriteBuffer
1593 stdcall ZwFreeUserPhysicalPages(ptr ptr ptr) ChpeAutoZwFreeUserPhysicalPages
1594 stdcall ZwFreeVirtualMemory(long ptr ptr long) ChpeAutoZwFreeVirtualMemory
1595 stdcall -version=0x600+ ZwFreezeRegistry(long) ChpeStubZwFreezeRegistry
1596 stdcall -version=0x600+ ZwFreezeTransactions(ptr ptr) ChpeStubZwFreezeTransactions
1597 stdcall ZwFsControlFile(long long long long long long long long long long) ChpeAutoZwFsControlFile
1598 stdcall ZwGetContextThread(long ptr) ChpeAutoZwGetContextThread
1599 stdcall ZwGetCurrentProcessorNumber() ChpeAutoZwGetCurrentProcessorNumber
1600 stdcall -version=0xA00+ ZwGetCurrentProcessorNumberEx(ptr) ChpeAutoZwGetCurrentProcessorNumberEx
1601 stdcall ZwGetDevicePowerState(ptr ptr) ChpeAutoZwGetDevicePowerState
1602 stdcall -version=0x600+ ZwGetMUIRegistryInfo(long ptr ptr) ChpeStubZwGetMUIRegistryInfo
1603 stdcall -version=0x600+ ZwGetNextProcess(ptr long long long ptr) ChpeStubZwGetNextProcess
1604 stdcall -version=0x600+ ZwGetNextThread(ptr ptr long long long ptr) ChpeAutoZwGetNextThread
1605 stdcall -version=0x600+ ZwGetNlsSectionPtr(long long ptr ptr ptr) ChpeAutoZwGetNlsSectionPtr
1606 stdcall -version=0x600+ ZwGetNotificationResourceManager(ptr ptr long ptr ptr long ptr) ChpeAutoZwGetNotificationResourceManager
1607 stdcall ZwGetPlugPlayEvent(long long ptr long) ChpeAutoZwGetPlugPlayEvent
1608 stdcall ZwGetWriteWatch(long long ptr long ptr ptr ptr) ChpeAutoZwGetWriteWatch
1609 stdcall ZwImpersonateAnonymousToken(ptr) ChpeAutoZwImpersonateAnonymousToken
1610 stdcall ZwImpersonateClientOfPort(ptr ptr) ChpeAutoZwImpersonateClientOfPort
1611 stdcall ZwImpersonateThread(ptr ptr ptr) ChpeAutoZwImpersonateThread
1612 stdcall -version=0x600+ ZwInitializeNlsFiles(ptr ptr ptr ptr) ChpeStubZwInitializeNlsFiles
1613 stdcall ZwInitializeRegistry(long) ChpeAutoZwInitializeRegistry
1614 stdcall ZwInitiatePowerAction(long long long long) ChpeAutoZwInitiatePowerAction
1615 stdcall ZwIsProcessInJob(long long) ChpeAutoZwIsProcessInJob
1616 stdcall ZwIsSystemResumeAutomatic() ChpeAutoZwIsSystemResumeAutomatic
1617 stdcall -version=0x600+ ZwIsUILanguageComitted() ChpeStubZwIsUILanguageComitted
1618 stdcall ZwListenPort(ptr ptr) ChpeAutoZwListenPort
1619 stdcall ZwLoadDriver(ptr) ChpeAutoZwLoadDriver
1620 stdcall ZwLoadKey2(ptr ptr long) ChpeAutoZwLoadKey2
1621 stdcall ZwLoadKey(ptr ptr) ChpeAutoZwLoadKey
1622 stdcall ZwLoadKeyEx(ptr ptr long ptr ptr long ptr ptr) ChpeAutoZwLoadKeyEx
1623 stdcall ZwLockFile(long long ptr ptr ptr ptr ptr ptr long long) ChpeAutoZwLockFile
1624 stdcall ZwLockProductActivationKeys(ptr ptr) ChpeAutoZwLockProductActivationKeys
1625 stdcall ZwLockRegistryKey(ptr) ChpeAutoZwLockRegistryKey
1626 stdcall ZwLockVirtualMemory(long ptr ptr long) ChpeAutoZwLockVirtualMemory
1627 stdcall ZwMakePermanentObject(ptr) ChpeAutoZwMakePermanentObject
1628 stdcall ZwMakeTemporaryObject(long) ChpeAutoZwMakeTemporaryObject
1629 stdcall -version=0x600+ ZwMapCMFModule(long long ptr ptr ptr) ChpeStubZwMapCMFModule
1630 stdcall ZwMapUserPhysicalPages(ptr ptr ptr) ChpeAutoZwMapUserPhysicalPages
1631 stdcall ZwMapUserPhysicalPagesScatter(ptr ptr ptr) ChpeAutoZwMapUserPhysicalPagesScatter
1632 stdcall ZwMapViewOfSection(long long ptr long long ptr ptr long long long) ChpeAutoZwMapViewOfSection
1633 stdcall ZwModifyBootEntry(ptr) ChpeAutoZwModifyBootEntry
1634 stdcall ZwModifyDriverEntry(ptr) ChpeAutoZwModifyDriverEntry
1635 stdcall ZwNotifyChangeDirectoryFile(long long ptr ptr ptr ptr long long long) ChpeAutoZwNotifyChangeDirectoryFile
1636 stdcall ZwNotifyChangeDirectoryFileEx(long long ptr ptr ptr ptr long long long long) ChpeAutoZwNotifyChangeDirectoryFileEx
1637 stdcall ZwNotifyChangeKey(long long ptr ptr ptr long long ptr long long) ChpeAutoZwNotifyChangeKey
1638 stdcall ZwNotifyChangeMultipleKeys(ptr long ptr ptr ptr ptr ptr long long ptr long long) ChpeAutoZwNotifyChangeMultipleKeys
1639 stdcall ZwOpenDirectoryObject(long long long) ChpeAutoZwOpenDirectoryObject
1640 stdcall -version=0x600+ ZwOpenEnlistment(ptr long ptr ptr ptr) ChpeAutoZwOpenEnlistment
1641 stdcall ZwOpenEvent(long long long) ChpeAutoZwOpenEvent
1642 stdcall ZwOpenEventPair(ptr long ptr) ChpeAutoZwOpenEventPair
1643 stdcall ZwOpenFile(ptr long ptr ptr long long) ChpeAutoZwOpenFile
1644 stdcall ZwOpenIoCompletion(ptr long ptr) ChpeAutoZwOpenIoCompletion
1645 stdcall ZwOpenJobObject(ptr long ptr) ChpeAutoZwOpenJobObject
1646 stdcall ZwOpenKey(ptr long ptr) ChpeAutoZwOpenKey
1647 stdcall ZwOpenKeyEx(ptr long ptr long) ChpeAutoZwOpenKeyEx
1648 stdcall -version=0x600+ ZwOpenKeyTransacted(ptr long ptr ptr) ChpeAutoZwOpenKeyTransacted
1649 stdcall ZwOpenKeyedEvent(ptr long ptr) ChpeAutoZwOpenKeyedEvent
1650 stdcall ZwOpenMutant(ptr long ptr) ChpeAutoZwOpenMutant
1651 stdcall ZwOpenObjectAuditAlarm(ptr ptr ptr ptr ptr ptr long long ptr long long ptr) ChpeAutoZwOpenObjectAuditAlarm
1652 stdcall -version=0x600+ ZwOpenPrivateNamespace(ptr long ptr ptr) ChpeAutoZwOpenPrivateNamespace
1653 stdcall ZwOpenProcess(ptr long ptr ptr) ChpeAutoZwOpenProcess
1654 stdcall ZwOpenProcessToken(long long ptr) ChpeAutoZwOpenProcessToken
1655 stdcall ZwOpenProcessTokenEx(long long long ptr) ChpeAutoZwOpenProcessTokenEx
1656 stdcall -version=0x600+ ZwOpenResourceManager(ptr long ptr ptr ptr) ChpeAutoZwOpenResourceManager
1657 stdcall ZwOpenSection(ptr long ptr) ChpeAutoZwOpenSection
1658 stdcall ZwOpenSemaphore(long long ptr) ChpeAutoZwOpenSemaphore
1659 stdcall -version=0x600+ ZwOpenSession(ptr long ptr) ChpeStubZwOpenSession
1660 stdcall ZwOpenSymbolicLinkObject(ptr long ptr) ChpeAutoZwOpenSymbolicLinkObject
1661 stdcall ZwOpenThread(ptr long ptr ptr) ChpeAutoZwOpenThread
1662 stdcall ZwOpenThreadToken(long long long ptr) ChpeAutoZwOpenThreadToken
1663 stdcall ZwOpenThreadTokenEx(long long long long ptr) ChpeAutoZwOpenThreadTokenEx
1664 stdcall ZwOpenTimer(ptr long ptr) ChpeAutoZwOpenTimer
1665 stdcall -version=0x600+ ZwOpenTransaction(ptr long ptr ptr ptr) ChpeAutoZwOpenTransaction
1666 stdcall -version=0x600+ ZwOpenTransactionManager(ptr long ptr ptr ptr long) ChpeAutoZwOpenTransactionManager
1667 stdcall ZwPlugPlayControl(ptr ptr long) ChpeAutoZwPlugPlayControl
1668 stdcall ZwPowerInformation(long ptr long ptr long) ChpeAutoZwPowerInformation
1669 stdcall -version=0x600+ ZwPrePrepareComplete(ptr ptr) ChpeAutoZwPrePrepareComplete
1670 stdcall -version=0x600+ ZwPrePrepareEnlistment(ptr ptr) ChpeAutoZwPrePrepareEnlistment
1671 stdcall -version=0x600+ ZwPrepareComplete(ptr ptr) ChpeAutoZwPrepareComplete
1672 stdcall -version=0x600+ ZwPrepareEnlistment(ptr ptr) ChpeAutoZwPrepareEnlistment
1673 stdcall ZwPrivilegeCheck(ptr ptr ptr) ChpeAutoZwPrivilegeCheck
1674 stdcall ZwPrivilegeObjectAuditAlarm(ptr ptr ptr long ptr long) ChpeAutoZwPrivilegeObjectAuditAlarm
1675 stdcall ZwPrivilegedServiceAuditAlarm(ptr ptr ptr ptr long) ChpeAutoZwPrivilegedServiceAuditAlarm
1676 stdcall -version=0x600+ ZwPropagationComplete(ptr long long ptr) ChpeAutoZwPropagationComplete
1677 stdcall -version=0x600+ ZwPropagationFailed(ptr long long) ChpeAutoZwPropagationFailed
1678 stdcall ZwProtectVirtualMemory(long ptr ptr long ptr) ChpeAutoZwProtectVirtualMemory
1679 stdcall ZwPulseEvent(long ptr) ChpeAutoZwPulseEvent
1680 stdcall ZwQueryAttributesFile(ptr ptr) ChpeAutoZwQueryAttributesFile
1681 stdcall ZwQueryBootEntryOrder(ptr ptr) ChpeAutoZwQueryBootEntryOrder
1682 stdcall ZwQueryBootOptions(ptr ptr) ChpeAutoZwQueryBootOptions
1683 stdcall ZwQueryDebugFilterState(long long) ChpeAutoZwQueryDebugFilterState
1684 stdcall ZwQueryDefaultLocale(long ptr) ChpeAutoZwQueryDefaultLocale
1685 stdcall ZwQueryDefaultUILanguage(ptr) ChpeAutoZwQueryDefaultUILanguage
1686 stdcall ZwQueryDirectoryFile(long long ptr ptr ptr ptr long long long ptr long) ChpeAutoZwQueryDirectoryFile
1687 stdcall ZwQueryDirectoryFileEx(long long ptr ptr ptr ptr long long long ptr) ChpeAutoZwQueryDirectoryFileEx
1688 stdcall ZwQueryDirectoryObject(long ptr long long long ptr ptr) ChpeAutoZwQueryDirectoryObject
1689 stdcall ZwQueryDriverEntryOrder(ptr ptr) ChpeAutoZwQueryDriverEntryOrder
1690 stdcall ZwQueryEaFile(long ptr ptr long long ptr long ptr long) ChpeAutoZwQueryEaFile
1691 stdcall ZwQueryEvent(long long ptr long ptr) ChpeAutoZwQueryEvent
1692 stdcall ZwQueryFullAttributesFile(ptr ptr) ChpeAutoZwQueryFullAttributesFile
1693 stdcall ZwQueryInformationAtom(long long ptr long ptr) ChpeAutoZwQueryInformationAtom
1694 stdcall ZwQueryInformationByName(ptr ptr ptr long long) ChpeAutoZwQueryInformationByName
1695 stdcall -version=0x600+ ZwQueryInformationEnlistment(ptr long ptr long ptr) ChpeAutoZwQueryInformationEnlistment
1696 stdcall ZwQueryInformationFile(long ptr ptr long long) ChpeAutoZwQueryInformationFile
1697 stdcall ZwQueryInformationJobObject(long long ptr long ptr) ChpeAutoZwQueryInformationJobObject
1698 stdcall ZwQueryInformationPort(ptr long ptr long ptr) ChpeAutoZwQueryInformationPort
1699 stdcall ZwQueryInformationProcess(long long ptr long ptr) ChpeAutoZwQueryInformationProcess
1700 stdcall -version=0x600+ ZwQueryInformationResourceManager(ptr long ptr long ptr) ChpeAutoZwQueryInformationResourceManager
1701 stdcall ZwQueryInformationThread(long long ptr long ptr) ChpeAutoZwQueryInformationThread
1702 stdcall ZwQueryInformationToken(long long ptr long ptr) ChpeAutoZwQueryInformationToken
1703 stdcall -version=0x600+ ZwQueryInformationTransaction(ptr long ptr long ptr) ChpeAutoZwQueryInformationTransaction
1704 stdcall -version=0x600+ ZwQueryInformationTransactionManager(ptr long ptr long ptr) ChpeAutoZwQueryInformationTransactionManager
1705 stdcall -version=0x600+ ZwQueryInformationWorkerFactory(ptr long ptr long ptr) ChpeStubZwQueryInformationWorkerFactory
1706 stdcall ZwQueryInstallUILanguage(ptr) ChpeAutoZwQueryInstallUILanguage
1707 stdcall ZwQueryIntervalProfile(long ptr) ChpeAutoZwQueryIntervalProfile
1708 stdcall ZwQueryIoCompletion(long long ptr long ptr) ChpeAutoZwQueryIoCompletion
1709 stdcall ZwQueryKey(long long ptr long ptr) ChpeAutoZwQueryKey
1710 stdcall -version=0x600+ ZwQueryLicenseValue(ptr ptr ptr long ptr) ChpeAutoZwQueryLicenseValue
1711 stdcall ZwQueryMultipleValueKey(long ptr long ptr long ptr) ChpeAutoZwQueryMultipleValueKey
1712 stdcall ZwQueryMutant(long long ptr long ptr) ChpeAutoZwQueryMutant
1713 stdcall ZwQueryObject(long long long long long) ChpeAutoZwQueryObject
1714 stdcall ZwQueryOpenSubKeys(ptr ptr) ChpeAutoZwQueryOpenSubKeys
1715 stdcall ZwQueryOpenSubKeysEx(ptr long ptr ptr) ChpeAutoZwQueryOpenSubKeysEx
1716 stdcall ZwQueryPerformanceCounter(long long) ChpeAutoZwQueryPerformanceCounter
1717 stdcall ZwQueryPortInformationProcess() ChpeAutoZwQueryPortInformationProcess
1718 stdcall ZwQueryQuotaInformationFile(ptr ptr ptr long long ptr long ptr long) ChpeAutoZwQueryQuotaInformationFile
1719 stdcall ZwQuerySection(long long long long long) ChpeAutoZwQuerySection
1720 stdcall ZwQuerySecurityObject(long long long long long) ChpeAutoZwQuerySecurityObject
1721 stdcall ZwQuerySemaphore(long long long long long) ChpeAutoZwQuerySemaphore
1722 stdcall ZwQuerySymbolicLinkObject(long ptr ptr) ChpeAutoZwQuerySymbolicLinkObject
1723 stdcall ZwQuerySystemEnvironmentValue(ptr ptr long ptr) ChpeAutoZwQuerySystemEnvironmentValue
1724 stdcall ZwQuerySystemEnvironmentValueEx(ptr ptr ptr ptr ptr) ChpeAutoZwQuerySystemEnvironmentValueEx
1725 stdcall ZwQuerySystemInformation(long long long long) ChpeAutoZwQuerySystemInformation
1726 stdcall -version=0x601+ ZwQuerySystemInformationEx(long ptr long ptr long ptr) ChpeAutoZwQuerySystemInformationEx
1727 stdcall ZwQuerySystemTime(ptr) ChpeAutoZwQuerySystemTime
1728 stdcall ZwQueryTimer(ptr long ptr long ptr) ChpeAutoZwQueryTimer
1729 stdcall ZwQueryTimerResolution(long long long) ChpeAutoZwQueryTimerResolution
1730 stdcall ZwQueryValueKey(long ptr long ptr long ptr) ChpeAutoZwQueryValueKey
1731 stdcall ZwQueryVirtualMemory(long ptr long ptr long ptr) ChpeAutoZwQueryVirtualMemory
1732 stdcall ZwQueryVolumeInformationFile(long ptr ptr long long) ChpeAutoZwQueryVolumeInformationFile
1733 stdcall -version=0x602+ ZwQueryWnfStateData(ptr ptr ptr ptr ptr ptr) ChpeAutoZwQueryWnfStateData
1734 stdcall -version=0x602+ ZwQueryWnfStateNameInformation(ptr long ptr ptr long) ChpeAutoZwQueryWnfStateNameInformation
1735 stdcall ZwQueueApcThread(long ptr long long long) ChpeAutoZwQueueApcThread
1736 stdcall ZwRaiseException(ptr ptr long) ChpeAutoZwRaiseException
1737 stdcall ZwRaiseHardError(long long long ptr long ptr) ChpeAutoZwRaiseHardError
1738 stdcall ZwReadFile(long long ptr ptr ptr ptr long ptr ptr) ChpeAutoZwReadFile
1739 stdcall ZwReadFileScatter(long long ptr ptr ptr ptr long ptr ptr) ChpeAutoZwReadFileScatter
1740 stdcall -version=0x600+ ZwReadOnlyEnlistment(ptr ptr) ChpeAutoZwReadOnlyEnlistment
1741 stdcall ZwReadRequestData(ptr ptr long ptr long ptr) ChpeAutoZwReadRequestData
1742 stdcall ZwReadVirtualMemory(long ptr ptr long ptr) ChpeAutoZwReadVirtualMemory
1743 stdcall -version=0x600+ ZwRecoverEnlistment(ptr ptr) ChpeAutoZwRecoverEnlistment
1744 stdcall -version=0x600+ ZwRecoverResourceManager(ptr) ChpeAutoZwRecoverResourceManager
1745 stdcall -version=0x600+ ZwRecoverTransactionManager(ptr) ChpeAutoZwRecoverTransactionManager
1746 stdcall -version=0x600+ ZwRegisterProtocolAddressInformation(ptr ptr long ptr long) ChpeAutoZwRegisterProtocolAddressInformation
1747 stdcall ZwRegisterThreadTerminatePort(ptr) ChpeAutoZwRegisterThreadTerminatePort
1748 stdcall -version=0x600+ ZwReleaseCMFViewOwnership() ChpeStubZwReleaseCMFViewOwnership
1749 stdcall ZwReleaseKeyedEvent(ptr ptr long ptr) ChpeAutoZwReleaseKeyedEvent
1750 stdcall ZwReleaseMutant(long ptr) ChpeAutoZwReleaseMutant
1751 stdcall ZwReleaseSemaphore(long long ptr) ChpeAutoZwReleaseSemaphore
1752 stdcall -version=0x600+ ZwReleaseWorkerFactoryWorker(ptr) ChpeStubZwReleaseWorkerFactoryWorker
1753 stdcall ZwRemoveIoCompletion(ptr ptr ptr ptr ptr) ChpeAutoZwRemoveIoCompletion
1754 stdcall -version=0x600+ ZwRemoveIoCompletionEx(ptr ptr long ptr ptr long) ChpeAutoZwRemoveIoCompletionEx
1755 stdcall ZwRemoveProcessDebug(ptr ptr) ChpeAutoZwRemoveProcessDebug
1756 stdcall ZwRenameKey(ptr ptr) ChpeAutoZwRenameKey
1757 stdcall -version=0x600+ ZwRenameTransactionManager(ptr ptr) ChpeAutoZwRenameTransactionManager
1758 stdcall ZwReplaceKey(ptr long ptr) ChpeAutoZwReplaceKey
1759 stdcall -version=0x600+ ZwReplacePartitionUnit(wstr wstr long) ChpeStubZwReplacePartitionUnit
1760 stdcall ZwReplyPort(ptr ptr) ChpeAutoZwReplyPort
1761 stdcall ZwReplyWaitReceivePort(ptr ptr ptr ptr) ChpeAutoZwReplyWaitReceivePort
1762 stdcall ZwReplyWaitReceivePortEx(ptr ptr ptr ptr ptr) ChpeAutoZwReplyWaitReceivePortEx
1763 stdcall ZwReplyWaitReplyPort(ptr ptr) ChpeAutoZwReplyWaitReplyPort
1764 stdcall ZwRequestDeviceWakeup(ptr) ChpeAutoZwRequestDeviceWakeup
1765 stdcall ZwRequestPort(ptr ptr) ChpeAutoZwRequestPort
1766 stdcall ZwRequestWaitReplyPort(ptr ptr ptr) ChpeAutoZwRequestWaitReplyPort
1767 stdcall ZwRequestWakeupLatency(long) ChpeAutoZwRequestWakeupLatency
1768 stdcall ZwResetEvent(long ptr) ChpeAutoZwResetEvent
1769 stdcall ZwResetWriteWatch(long ptr long) ChpeAutoZwResetWriteWatch
1770 stdcall ZwRestoreKey(long long long) ChpeAutoZwRestoreKey
1771 stdcall ZwResumeProcess(ptr) ChpeAutoZwResumeProcess
1772 stdcall ZwResumeThread(long long) ChpeAutoZwResumeThread
1773 stdcall -version=0x600+ ZwRollbackComplete(ptr ptr) ChpeAutoZwRollbackComplete
1774 stdcall -version=0x600+ ZwRollbackEnlistment(ptr ptr) ChpeAutoZwRollbackEnlistment
1775 stdcall -version=0x600+ ZwRollbackTransaction(ptr long) ChpeAutoZwRollbackTransaction
1776 stdcall -version=0x600+ ZwRollforwardTransactionManager(ptr ptr) ChpeAutoZwRollforwardTransactionManager
1777 stdcall ZwSaveKey(long long) ChpeAutoZwSaveKey
1778 stdcall ZwSaveKeyEx(ptr ptr long) ChpeAutoZwSaveKeyEx
1779 stdcall ZwSaveMergedKeys(ptr ptr ptr) ChpeAutoZwSaveMergedKeys
1780 stdcall ZwSecureConnectPort(ptr ptr ptr ptr ptr ptr ptr ptr ptr) ChpeAutoZwSecureConnectPort
1781 stdcall ZwSetBootEntryOrder(ptr ptr) ChpeAutoZwSetBootEntryOrder
1782 stdcall ZwSetBootOptions(ptr long) ChpeAutoZwSetBootOptions
1783 stdcall ZwSetContextThread(long ptr) ChpeAutoZwSetContextThread
1784 stdcall ZwSetDebugFilterState(long long long) ChpeAutoZwSetDebugFilterState
1785 stdcall ZwSetDefaultHardErrorPort(ptr) ChpeAutoZwSetDefaultHardErrorPort
1786 stdcall ZwSetDefaultLocale(long long) ChpeAutoZwSetDefaultLocale
1787 stdcall ZwSetDefaultUILanguage(long) ChpeAutoZwSetDefaultUILanguage
1788 stdcall ZwSetDriverEntryOrder(ptr ptr) ChpeAutoZwSetDriverEntryOrder
1789 stdcall ZwSetEaFile(long ptr ptr long) ChpeAutoZwSetEaFile
1790 stdcall ZwSetEvent(long long) ChpeAutoZwSetEvent
1791 stdcall ZwSetEventBoostPriority(ptr) ChpeAutoZwSetEventBoostPriority
1792 stdcall ZwSetHighEventPair(ptr) ChpeAutoZwSetHighEventPair
1793 stdcall ZwSetHighWaitLowEventPair(ptr) ChpeAutoZwSetHighWaitLowEventPair
1794 stdcall ZwSetInformationDebugObject(ptr long ptr long ptr) ChpeAutoZwSetInformationDebugObject
1795 stdcall -version=0x600+ ZwSetInformationEnlistment(ptr long ptr long) ChpeAutoZwSetInformationEnlistment
1796 stdcall ZwSetInformationFile(long long long long long) ChpeAutoZwSetInformationFile
1797 stdcall ZwSetInformationJobObject(long long ptr long) ChpeAutoZwSetInformationJobObject
1798 stdcall ZwSetInformationKey(long long ptr long) ChpeAutoZwSetInformationKey
1799 stdcall ZwSetInformationObject(long long ptr long) ChpeAutoZwSetInformationObject
1800 stdcall ZwSetInformationProcess(long long long long) ChpeAutoZwSetInformationProcess
1801 stdcall -version=0x600+ ZwSetInformationResourceManager(ptr long ptr long) ChpeAutoZwSetInformationResourceManager
1802 stdcall ZwSetInformationThread(long long ptr long) ChpeAutoZwSetInformationThread
1803 stdcall ZwSetInformationToken(long long ptr long) ChpeAutoZwSetInformationToken
1804 stdcall -version=0x600+ ZwSetInformationTransaction(ptr long ptr long) ChpeAutoZwSetInformationTransaction
1805 stdcall -version=0x600+ ZwSetInformationTransactionManager(ptr long ptr long) ChpeAutoZwSetInformationTransactionManager
1806 stdcall ZwSetInformationVirtualMemory(ptr long ptr ptr ptr long) ChpeAutoZwSetInformationVirtualMemory
1807 stdcall -version=0x600+ ZwSetInformationWorkerFactory(ptr long ptr long) ChpeStubZwSetInformationWorkerFactory
1808 stdcall ZwSetIntervalProfile(long long) ChpeAutoZwSetIntervalProfile
1809 stdcall ZwSetIoCompletion(ptr long ptr long long) ChpeAutoZwSetIoCompletion
1810 stdcall ZwSetLdtEntries(long int64 long int64) ChpeAutoZwSetLdtEntries
1811 stdcall ZwSetLowEventPair(ptr) ChpeAutoZwSetLowEventPair
1812 stdcall ZwSetLowWaitHighEventPair(ptr) ChpeAutoZwSetLowWaitHighEventPair
1813 stdcall ZwSetQuotaInformationFile(ptr ptr ptr long) ChpeAutoZwSetQuotaInformationFile
1814 stdcall ZwSetSecurityObject(long long ptr) ChpeAutoZwSetSecurityObject
1815 stdcall ZwSetSystemEnvironmentValue(ptr ptr) ChpeAutoZwSetSystemEnvironmentValue
1816 stdcall ZwSetSystemEnvironmentValueEx(ptr ptr ptr ptr ptr) ChpeAutoZwSetSystemEnvironmentValueEx
1817 stdcall ZwSetSystemInformation(long ptr long) ChpeAutoZwSetSystemInformation
1818 stdcall ZwSetSystemPowerState(long long long) ChpeAutoZwSetSystemPowerState
1819 stdcall ZwSetSystemTime(ptr ptr) ChpeAutoZwSetSystemTime
1820 stdcall ZwSetThreadExecutionState(long ptr) ChpeAutoZwSetThreadExecutionState
1821 stdcall ZwSetTimer(long ptr ptr ptr long long ptr) ChpeAutoZwSetTimer
1822 stdcall ZwSetTimerResolution(long long ptr) ChpeAutoZwSetTimerResolution
1823 stdcall ZwSetUuidSeed(ptr) ChpeAutoZwSetUuidSeed
1824 stdcall ZwSetValueKey(long long long long long long) ChpeAutoZwSetValueKey
1825 stdcall ZwSetVolumeInformationFile(long ptr ptr long long) ChpeAutoZwSetVolumeInformationFile
1826 stdcall ZwShutdownSystem(long) ChpeAutoZwShutdownSystem
1827 stdcall -version=0x600+ ZwShutdownWorkerFactory(ptr ptr) ChpeStubZwShutdownWorkerFactory
1828 stdcall ZwSignalAndWaitForSingleObject(long long long ptr) ChpeAutoZwSignalAndWaitForSingleObject
1829 stdcall -version=0x600+ ZwSinglePhaseReject(ptr ptr) ChpeAutoZwSinglePhaseReject
1830 stdcall ZwStartProfile(ptr) ChpeAutoZwStartProfile
1831 stdcall ZwStopProfile(ptr) ChpeAutoZwStopProfile
1832 stdcall -version=0x602+ ZwSubscribeWnfStateChange(ptr long long ptr) ChpeAutoZwSubscribeWnfStateChange
1833 stdcall ZwSuspendProcess(ptr) ChpeAutoZwSuspendProcess
1834 stdcall ZwSuspendThread(long ptr) ChpeAutoZwSuspendThread
1835 stdcall ZwSystemDebugControl(long ptr long ptr long ptr) ChpeAutoZwSystemDebugControl
1836 stdcall ZwTerminateJobObject(ptr long) ChpeAutoZwTerminateJobObject
1837 stdcall ZwTerminateProcess(ptr long) ChpeAutoZwTerminateProcess
1838 stdcall ZwTerminateThread(ptr long) ChpeAutoZwTerminateThread
1839 stdcall ZwTestAlert() ChpeAutoZwTestAlert
1840 stdcall -version=0x600+ ZwThawRegistry() ChpeStubZwThawRegistry
1841 stdcall -version=0x600+ ZwThawTransactions() ChpeStubZwThawTransactions
1842 stdcall -version=0x600+ ZwTraceControl(long ptr long ptr long ptr) ChpeAutoZwTraceControl
1843 stdcall ZwTraceEvent(ptr long long ptr) ChpeAutoZwTraceEvent
1844 stdcall ZwTranslateFilePath(ptr long ptr long) ChpeAutoZwTranslateFilePath
1845 stdcall ZwUnloadDriver(ptr) ChpeAutoZwUnloadDriver
1846 stdcall ZwUnloadKey2(ptr long) ChpeAutoZwUnloadKey2
1847 stdcall ZwUnloadKey(long) ChpeAutoZwUnloadKey
1848 stdcall ZwUnloadKeyEx(ptr ptr) ChpeAutoZwUnloadKeyEx
1849 stdcall ZwUnlockFile(long ptr ptr ptr ptr) ChpeAutoZwUnlockFile
1850 stdcall ZwUnlockVirtualMemory(long ptr ptr long) ChpeAutoZwUnlockVirtualMemory
1851 stdcall ZwUnmapViewOfSection(long ptr) ChpeAutoZwUnmapViewOfSection
1852 stdcall -version=0x602+ ZwUnsubscribeWnfStateChange(ptr) ChpeAutoZwUnsubscribeWnfStateChange
1853 stdcall -version=0x602+ ZwUpdateWnfStateData(ptr ptr long ptr ptr long long) ChpeAutoZwUpdateWnfStateData
1854 stdcall ZwVdmControl(long ptr) ChpeAutoZwVdmControl
1855 stdcall ZwWaitForDebugEvent(ptr long ptr ptr) ChpeAutoZwWaitForDebugEvent
1856 stdcall ZwWaitForKeyedEvent(ptr ptr long ptr) ChpeAutoZwWaitForKeyedEvent
1857 stdcall ZwWaitForAlertByThreadId(ptr ptr) ChpeAutoZwWaitForAlertByThreadId
1858 stdcall ZwWaitForMultipleObjects32(long ptr long long ptr) ChpeAutoZwWaitForMultipleObjects32
1859 stdcall ZwWaitForMultipleObjects(long ptr long long ptr) ChpeAutoZwWaitForMultipleObjects
1860 stdcall ZwWaitForSingleObject(long long long) ChpeAutoZwWaitForSingleObject
1861 stdcall -version=0x600+ ZwWaitForWorkViaWorkerFactory(ptr ptr long ptr ptr) ChpeStubZwWaitForWorkViaWorkerFactory
1862 stdcall ZwWaitHighEventPair(ptr) ChpeAutoZwWaitHighEventPair
1863 stdcall ZwWaitLowEventPair(ptr) ChpeAutoZwWaitLowEventPair
1864 stdcall -version=0x600+ ZwWorkerFactoryWorkerReady(ptr) ChpeStubZwWorkerFactoryWorkerReady
1865 stdcall ZwWriteFile(long long ptr ptr ptr ptr long ptr ptr) ChpeAutoZwWriteFile
1866 stdcall ZwWriteFileGather(long long ptr ptr ptr ptr long ptr ptr) ChpeAutoZwWriteFileGather
1867 stdcall ZwWriteRequestData(ptr ptr long ptr long ptr) ChpeAutoZwWriteRequestData
1868 stdcall ZwWriteVirtualMemory(long ptr ptr long ptr) ChpeAutoZwWriteVirtualMemory
1869 stdcall ZwYieldExecution() ChpeAutoZwYieldExecution
1872 cdecl __isascii(long) ChpeAuto__isascii
1873 cdecl __iscsym(long) ChpeAuto__iscsym
1874 cdecl __iscsymf(long) ChpeAuto__iscsymf
1875 cdecl -version=0x600+ __misaligned_access() ChpeStub__misaligned_access
1876 cdecl __toascii(long) ChpeAuto__toascii
1877 cdecl -ret64 _atoi64(str) ChpeAuto_atoi64
1879 extern _fltused ntdll._fltused
1880 cdecl _i64toa(double ptr long) ChpeAuto_i64toa
1881 cdecl _i64tow(double ptr long) ChpeAuto_i64tow
1882 cdecl _itoa(long ptr long) ChpeAuto_itoa
1883 cdecl _itow(long ptr long) ChpeAuto_itow
1886 cdecl _ltoa(long ptr long) ChpeAuto_ltoa
1887 cdecl _ltow(long ptr long) ChpeAuto_ltow
1888 cdecl _memccpy(ptr ptr long long) ChpeAuto_memccpy
1889 cdecl _memicmp(str str long) ChpeAuto_memicmp
1897 cdecl _splitpath(str ptr ptr ptr ptr) ChpeAuto_splitpath
1898 cdecl _strcmpi(str str) ChpeAuto_strcmpi
1899 cdecl _stricmp(str str) ChpeAuto_stricmp
1900 cdecl _strlwr(str) ChpeAuto_strlwr
1901 cdecl _strnicmp(str str long) ChpeAuto_strnicmp
1902 cdecl _strupr(str) ChpeAuto_strupr
1904 cdecl _ui64toa(double ptr long) ChpeAuto_ui64toa
1905 cdecl _ui64tow(double ptr long) ChpeAuto_ui64tow
1906 cdecl _ultoa(long ptr long) ChpeAuto_ultoa
1907 cdecl _ultow(long ptr long) ChpeAuto_ultow
1908 cdecl _vscwprintf(wstr ptr) ChpeAuto_vscwprintf
1909 cdecl _vsnprintf(ptr long str ptr) ChpeAuto_vsnprintf
1910 cdecl _vsnprintf_s(ptr long long str ptr) ChpeAuto_vsnprintf_s
1911 cdecl _vsnwprintf(ptr long wstr ptr) ChpeAuto_vsnwprintf
1912 cdecl _vsnwprintf_s(ptr long long wstr ptr) ChpeAuto_vsnwprintf_s
1913 cdecl -version=0x600+ _vswprintf(ptr wstr ptr) ChpeStub_vswprintf
1914 cdecl _wcsicmp(wstr wstr) ChpeAuto_wcsicmp
1915 cdecl _wcslwr(wstr) ChpeAuto_wcslwr
1916 cdecl _wcsnicmp(wstr wstr long) ChpeAuto_wcsnicmp
1917 cdecl _wcstoui64(wstr ptr long) ChpeAuto_wcstoui64
1918 cdecl _wcsupr(wstr) ChpeAuto_wcsupr
1919 cdecl _wtoi(wstr) ChpeAuto_wtoi
1920 cdecl _wtoi64(wstr) ChpeAuto_wtoi64
1921 cdecl _wtol(wstr) ChpeAuto_wtol
1922 cdecl abs(long) ChpeAutoabs
1923 cdecl atan(double) ChpeAutoatan
1924 cdecl atan2(double double) ChpeAutoatan2
1925 cdecl atoi(str) ChpeAutoatoi
1926 cdecl atol(str) ChpeAutoatol
1928 cdecl ceil(double) ChpeAutoceil
1929 cdecl cos(double) ChpeAutocos
1930 cdecl fabs(double) ChpeAutofabs
1931 cdecl floor(double) ChpeAutofloor
1932 cdecl isalnum(long) ChpeAutoisalnum
1933 cdecl isalpha(long) ChpeAutoisalpha
1934 cdecl iscntrl(long) ChpeAutoiscntrl
1935 cdecl isdigit(long) ChpeAutoisdigit
1936 cdecl isgraph(long) ChpeAutoisgraph
1937 cdecl islower(long) ChpeAutoislower
1938 cdecl isprint(long) ChpeAutoisprint
1939 cdecl ispunct(long) ChpeAutoispunct
1940 cdecl isspace(long) ChpeAutoisspace
1941 cdecl isupper(long) ChpeAutoisupper
1942 cdecl iswalpha(long) ChpeAutoiswalpha
1943 cdecl iswctype(long long) ChpeAutoiswctype
1944 cdecl iswdigit(long) ChpeAutoiswdigit
1945 cdecl iswlower(long) ChpeAutoiswlower
1946 cdecl iswspace(long) ChpeAutoiswspace
1947 cdecl iswxdigit(long) ChpeAutoiswxdigit
1948 cdecl isxdigit(long) ChpeAutoisxdigit
1949 cdecl labs(long) ChpeAutolabs
1950 cdecl log(double) ChpeAutolog
1952 cdecl mbstowcs(ptr str long) ChpeAutombstowcs
1953 cdecl memchr(ptr long long) ChpeAutomemchr
1954 cdecl memcmp(ptr ptr long) ChpeAutomemcmp
1956 cdecl memmove(ptr ptr long) ChpeAutomemmove
1957 cdecl memset(ptr long long) ChpeAutomemset
1958 cdecl pow(double double) ChpeAutopow
1960 cdecl sin(double) ChpeAutosin
1962 cdecl sqrt(double) ChpeAutosqrt
1965 cdecl strcat(str str) ChpeAutostrcat
1966 cdecl strchr(str long) ChpeAutostrchr
1967 cdecl strcmp(str str) ChpeAutostrcmp
1968 cdecl strcpy(ptr str) ChpeAutostrcpy
1969 cdecl -version=0x600+ strcpy_s(ptr long str) ChpeAutostrcpy_s
1970 cdecl -version=0x600+ strcat_s(ptr long str) ChpeAutostrcat_s
1971 cdecl -version=0x600+ strncpy_s(ptr long str long) ChpeAutostrncpy_s
1972 cdecl strcspn(str str) ChpeAutostrcspn
1973 cdecl strlen(str) ChpeAutostrlen
1974 cdecl strncat(str str long) ChpeAutostrncat
1975 cdecl strncmp(str str long) ChpeAutostrncmp
1976 cdecl strncpy(ptr str long) ChpeAutostrncpy
1977 cdecl strpbrk(str str) ChpeAutostrpbrk
1978 cdecl strrchr(str long) ChpeAutostrrchr
1979 cdecl strspn(str str) ChpeAutostrspn
1980 cdecl strstr(str str) ChpeAutostrstr
1981 cdecl strtol(str ptr long) ChpeAutostrtol
1982 cdecl strtoul(str ptr long) ChpeAutostrtoul
1985 cdecl tan(double) ChpeAutotan
1986 cdecl tolower(long) ChpeAutotolower
1987 cdecl toupper(long) ChpeAutotoupper
1988 cdecl towlower(long) ChpeAutotowlower
1989 cdecl towupper(long) ChpeAutotowupper
1992 cdecl vsprintf(ptr str ptr) ChpeAutovsprintf
1993 cdecl wcscat(wstr wstr) ChpeAutowcscat
1994 cdecl wcschr(wstr long) ChpeAutowcschr
1995 cdecl wcscmp(wstr wstr) ChpeAutowcscmp
1996 cdecl wcscpy(ptr wstr) ChpeAutowcscpy
1997 cdecl wcscspn(wstr wstr) ChpeAutowcscspn
1998 cdecl wcslen(wstr) ChpeAutowcslen
1999 cdecl wcsncat(wstr wstr long) ChpeAutowcsncat
2000 cdecl wcsncmp(wstr wstr long) ChpeAutowcsncmp
2001 cdecl wcsncpy(ptr wstr long) ChpeAutowcsncpy
2002 cdecl wcsnlen(wstr long) ChpeAutowcsnlen
2003 cdecl wcspbrk(wstr wstr) ChpeAutowcspbrk
2004 cdecl wcsrchr(wstr long) ChpeAutowcsrchr
2005 cdecl wcsspn(wstr wstr) ChpeAutowcsspn
2006 cdecl wcsstr(wstr wstr) ChpeAutowcsstr
2007 cdecl wcstol(wstr ptr long) ChpeAutowcstol
2008 cdecl wcstombs(ptr ptr long) ChpeAutowcstombs
2009 cdecl wcstoul(wstr ptr long) ChpeAutowcstoul
2015 stdcall -version=0x602+ -ret64 RtlGetSystemTimePrecise() ChpeAutoRtlGetSystemTimePrecise
# END typed bridge wrappers
