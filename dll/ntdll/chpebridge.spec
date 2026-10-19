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
1869 cdecl __C_specific_handler(ptr long ptr ptr) ChpeCSpecificHandler
2009 stdcall ChpeDispatchExceptionNative(ptr ptr)
108 stdcall KiUserExceptionDispatcher(ptr ptr) ChpeKiUserExceptionDispatcher
111 stdcall LdrAccessResource(ptr ptr ptr ptr) ChpeLdrAccessResource
113 stdcall LdrAddRefDll(long ptr) ChpeLdrAddRefDll
115 stdcall LdrEnumResources(ptr ptr long ptr ptr) ChpeLdrEnumResources
117 stdcall LdrFindEntryForAddress(ptr ptr) ChpeLdrFindEntryForAddress
118 stdcall LdrFindResourceDirectory_U(ptr ptr long ptr) ChpeLdrFindResourceDirectory_U
120 stdcall LdrFindResource_U(ptr ptr long ptr) ChpeLdrFindResource_U
122 stdcall LdrGetDllHandle(wstr ptr ptr ptr) ChpeLdrGetDllHandle
123 stdcall LdrGetDllHandleEx(long wstr ptr ptr ptr) ChpeLdrGetDllHandleEx
127 stdcall LdrGetProcedureAddress(ptr ptr long ptr) ChpeLdrGetProcedureAddress
134 stdcall LdrLoadDll(wstr long ptr ptr) ChpeLdrLoadDll
143 stdcall LdrQueryProcessModuleInformation(ptr long ptr) ChpeLdrQueryProcessModuleInformation
146 stdcall -version=0x602+ LdrResolveDelayLoadedAPI(ptr ptr ptr ptr ptr long) ChpeLdrResolveDelayLoadedAPI
147 stdcall -version=0x602+ LdrResolveDelayLoadsFromDll(ptr str long) ChpeLdrResolveDelayLoadsFromDll
160 stdcall LdrUnloadDll(ptr) ChpeLdrUnloadDll
190 stdcall NtAdjustPrivilegesToken(long long ptr long ptr ptr) ChpeAutoNtAdjustPrivilegesToken
199 stdcall NtAllocateVirtualMemory(long ptr ptr ptr long long) ChpeAutoNtAllocateVirtualMemory
200 stdcall NtAllocateVirtualMemoryEx(long ptr ptr long long ptr long) ChpeAutoNtAllocateVirtualMemoryEx
194 stdcall NtAlertThreadByThreadId(long) ChpeAutoNtAlertThreadByThreadId
234 stdcall NtClose(long) ChpeAutoNtClose
245 stdcall NtContinue(ptr long) ChpeNtContinue
246 stdcall -version=0xA00+ NtContinueEx(ptr ptr) ChpeNtContinueEx
253 stdcall NtCreateFile(ptr long ptr ptr ptr long long long long ptr long) ChpeAutoNtCreateFile
265 stdcall NtCreateNamedPipeFile(ptr long ptr ptr long long long long long long long long long ptr) ChpeAutoNtCreateNamedPipeFile
300 stdcall NtDeviceIoControlFile(long long ptr ptr ptr long ptr long ptr long) ChpeAutoNtDeviceIoControlFile
319 stdcall -version=0x600+ NtFlushProcessWriteBuffers() ChpeAutoNtFlushProcessWriteBuffers
317 stdcall NtFlushInstructionCache(long ptr long) ChpeAutoNtFlushInstructionCache
323 stdcall NtFreeVirtualMemory(long ptr ptr long) ChpeAutoNtFreeVirtualMemory
327 stdcall NtGetContextThread(long ptr) ChpeNtGetContextThread
333 stdcall -version=0x600+ NtGetNextThread(ptr ptr long long long ptr) ChpeAutoNtGetNextThread
362 stdcall NtMapViewOfSection(long long ptr long ptr ptr ptr long long long) ChpeAutoNtMapViewOfSection
363 stdcall NtMapViewOfSectionEx(long long ptr ptr ptr long long ptr long) ChpeNtMapViewOfSectionEx
368 stdcall NtNotifyChangeKey(ptr ptr ptr ptr ptr long long ptr long long) ChpeAutoNtNotifyChangeKey
369 stdcall NtNotifyChangeMultipleKeys(ptr long ptr ptr ptr ptr ptr long long ptr long long) ChpeAutoNtNotifyChangeMultipleKeys
374 stdcall NtOpenFile(ptr long ptr ptr long long) ChpeAutoNtOpenFile
409 stdcall NtProtectVirtualMemory(long ptr ptr long ptr) ChpeAutoNtProtectVirtualMemory
417 stdcall NtQueryDirectoryFile(long long ptr ptr ptr ptr long long long ptr long) ChpeAutoNtQueryDirectoryFile
427 stdcall NtQueryInformationFile(long ptr ptr long long) ChpeAutoNtQueryInformationFile
440 stdcall NtQueryKey(long long ptr long ptr) ChpeAutoNtQueryKey
260 stdcall NtCreateKey(ptr long ptr long ptr long ptr) ChpeAutoNtCreateKey
432 stdcall NtQueryInformationThread(long long ptr long ptr) ChpeAutoNtQueryInformationThread
430 stdcall NtQueryInformationProcess(ptr long ptr long ptr) ChpeAutoNtQueryInformationProcess
891 stdcall RtlFormatCurrentUserKeyPath(ptr) ChpeRtlFormatCurrentUserKeyPath
902 stdcall RtlFreeUnicodeString(ptr) ChpeRtlFreeUnicodeString
377 stdcall NtOpenKey(ptr long ptr) ChpeAutoNtOpenKey
839 stdcall NtOpenKeyEx(ptr long ptr long) ChpeAutoNtOpenKeyEx
461 stdcall NtQueryValueKey(long ptr long ptr long ptr) ChpeAutoNtQueryValueKey
558 stdcall NtSetValueKey(long ptr long long ptr long) ChpeAutoNtSetValueKey
294 stdcall NtDeleteKey(long) ChpeAutoNtDeleteKey
297 stdcall NtDeleteValueKey(long ptr) ChpeAutoNtDeleteValueKey
306 stdcall NtEnumerateKey(long long long ptr long ptr) ChpeAutoNtEnumerateKey
309 stdcall NtEnumerateValueKey(long long long ptr long ptr) ChpeAutoNtEnumerateValueKey
318 stdcall NtFlushKey(long) ChpeAutoNtFlushKey
255 stdcall NtCreateWaitCompletionPacket(ptr long ptr) ChpeAutoNtCreateWaitCompletionPacket
256 stdcall NtAssociateWaitCompletionPacket(long long long ptr ptr long long ptr) ChpeAutoNtAssociateWaitCompletionPacket
257 stdcall NtCancelWaitCompletionPacket(long long) ChpeAutoNtCancelWaitCompletionPacket
463 stdcall NtQueryVolumeInformationFile(long ptr ptr long long) ChpeAutoNtQueryVolumeInformationFile
444 stdcall NtQueryObject(long long long long long) ChpeAutoNtQueryObject
456 stdcall NtQuerySystemInformation(long ptr long ptr) ChpeAutoNtQuerySystemInformation
938 stdcall RtlGetNativeSystemInformation(long ptr long ptr) ChpeRtlGetNativeSystemInformation
462 stdcall NtQueryVirtualMemory(long ptr long ptr long ptr) ChpeAutoNtQueryVirtualMemory
469 stdcall NtRaiseException(ptr ptr long) ChpeNtRaiseException
471 stdcall NtReadFile(long long ptr ptr ptr ptr long ptr ptr) ChpeAutoNtReadFile
475 stdcall NtReadVirtualMemory(long ptr ptr long ptr) ChpeAutoNtReadVirtualMemory
516 stdcall NtSetContextThread(long ptr) ChpeNtSetContextThread
567 stdcall NtSuspendProcess(ptr) ChpeAutoNtSuspendProcess
568 stdcall NtSuspendThread(ptr ptr) ChpeAutoNtSuspendThread
505 stdcall NtResumeThread(ptr ptr) ChpeAutoNtResumeThread
571 stdcall NtTerminateProcess(long long) ChpeAutoNtTerminateProcess
572 stdcall NtTerminateThread(long long) ChpeAutoNtTerminateThread
585 stdcall NtUnmapViewOfSection(long ptr) ChpeAutoNtUnmapViewOfSection
586 stdcall NtUnmapViewOfSectionEx(long ptr long) ChpeNtUnmapViewOfSectionEx
592 stdcall NtWaitForAlertByThreadId(ptr ptr) ChpeAutoNtWaitForAlertByThreadId
600 stdcall NtWriteFile(long long ptr ptr ptr ptr long ptr ptr) ChpeAutoNtWriteFile
603 stdcall NtWriteVirtualMemory(long ptr ptr long ptr) ChpeAutoNtWriteVirtualMemory
638 stdcall RtlAddFunctionTable(ptr long long) ChpeRtlAddFunctionTable
639 stdcall RtlAddGrowableFunctionTable(ptr ptr long long long long) ChpeRtlAddGrowableFunctionTable
645 stdcall RtlAddVectoredContinueHandler(long ptr) ChpeRtlAddVectoredContinueHandler
646 stdcall RtlAddVectoredExceptionHandler(long ptr) ChpeRtlAddVectoredExceptionHandler
652 stdcall RtlAllocateHeap(ptr long ptr) ChpeRtlAllocateHeap
913 stdcall RtlGetCurrentPeb() ChpeRtlGetCurrentPeb
945 stdcall RtlGetProcessHeaps(long ptr) ChpeRtlGetProcessHeaps
1127 stdcall RtlQueryEnvironmentVariable(ptr ptr long ptr long ptr) ChpeRtlQueryEnvironmentVariable
616 stdcall RtlAcquirePrivilege(ptr long long ptr) ChpeRtlAcquirePrivilege
619 stdcall RtlAcquireSRWLockExclusive(ptr) ChpeRtlAcquireSRWLockExclusive
620 stdcall RtlAcquireSRWLockShared(ptr) ChpeRtlAcquireSRWLockShared
674 stdcall RtlCaptureContext(ptr) ChpeRtlCaptureContextX64
675 stdcall RtlCaptureStackBackTrace(long long ptr ptr) ChpeRtlCaptureStackBackTrace
691 stdcall RtlCompareMemory(ptr ptr long) ChpeRtlCompareMemory
694 stdcall RtlCompareUnicodeString(ptr ptr long) ChpeRtlCompareUnicodeString
722 stdcall RtlCopyUnicodeString(ptr ptr) ChpeRtlCopyUnicodeString
766 stdcall RtlDecodePointer(ptr) ChpeRtlDecodePointer
767 stdcall RtlDecodeSystemPointer(ptr) ChpeRtlDecodeSystemPointer
777 stdcall RtlDeleteCriticalSection(ptr) ChpeRtlDeleteCriticalSection
780 cdecl RtlDeleteFunctionTable(ptr) ChpeRtlDeleteFunctionTable
781 stdcall RtlDeleteGrowableFunctionTable(ptr) ChpeRtlDeleteGrowableFunctionTable
821 stdcall RtlEnterCriticalSection(ptr) ChpeRtlEnterCriticalSection
819 stdcall RtlEncodePointer(ptr) ChpeRtlEncodePointer
820 stdcall RtlEncodeSystemPointer(ptr) ChpeRtlEncodeSystemPointer
859 stdcall RtlExitUserThread(long) ChpeRtlExitUserThread
864 stdcall RtlFillMemory(ptr long long) ChpeRtlFillMemory
886 stdcall RtlFlsAlloc(ptr ptr) ChpeRtlFlsAlloc
887 stdcall RtlFlsFree(long) ChpeRtlFlsFree
888 stdcall RtlFlsGetValue(long ptr) ChpeRtlFlsGetValue
889 stdcall RtlFlsSetValue(long ptr) ChpeRtlFlsSetValue
897 stdcall RtlFreeHeap(long long long) ChpeRtlFreeHeap
929 stdcall RtlGetFunctionTableListHead() ChpeRtlGetFunctionTableListHead
932 stdcall RtlGetLastNtStatus() ChpeRtlGetLastNtStatus
933 stdcall RtlGetLastWin32Error() ChpeRtlGetLastWin32Error
914 stdcall RtlGetCurrentProcessorNumber() ChpeRtlGetCurrentProcessorNumber
915 stdcall -version=0x601+ RtlGetCurrentProcessorNumberEx(ptr) ChpeRtlGetCurrentProcessorNumberEx
946 stdcall -version=0x600+ RtlGetProductInfo(long long long long ptr) ChpeRtlGetProductInfo
959 stdcall RtlGetVersion(ptr) ChpeRtlGetVersion
1011 stdcall RtlGrowFunctionTable(ptr long) ChpeRtlGrowFunctionTable
984 stdcall RtlInitUnicodeString(ptr wstr) ChpeRtlInitUnicodeString
989 stdcall -version=0x600+ RtlInitializeConditionVariable(ptr) ChpeRtlInitializeConditionVariable
991 stdcall RtlInitializeCriticalSection(ptr) ChpeRtlInitializeCriticalSection
1002 stdcall RtlInitializeSListHead(ptr) ChpeRtlInitializeSListHead
1003 stdcall -version=0x600+ RtlInitializeSRWLock(ptr) ChpeRtlInitializeSRWLock
1010 cdecl RtlInstallFunctionTableCallback(double double long ptr ptr ptr) ChpeRtlInstallFunctionTableCallback
1015 stdcall RtlInterlockedFlushSList(ptr) ChpeRtlInterlockedFlushSList
1016 stdcall RtlInterlockedPopEntrySList(ptr) ChpeRtlInterlockedPopEntrySList
1017 stdcall RtlInterlockedPushEntrySList(ptr ptr) ChpeRtlInterlockedPushEntrySList
1018 stdcall RtlInterlockedPushListSList(ptr ptr ptr long) ChpeRtlInterlockedPushListSList
1019 stdcall -version=0x602+ RtlInterlockedPushListSListEx(ptr ptr ptr long) ChpeRtlInterlockedPushListSListEx
1049 stdcall RtlIsProcessorFeaturePresent(long) ChpeRtlIsProcessorFeaturePresent
1058 stdcall RtlLeaveCriticalSection(ptr) ChpeRtlLeaveCriticalSection
1080 stdcall RtlLookupFunctionEntry(long ptr ptr) ChpeRtlLookupFunctionEntry
1081 stdcall RtlLookupFunctionTable(int64 ptr ptr) ChpeRtlLookupFunctionTable
1071 stdcall -version=0xA00+ RtlLogUnexpectedCodepath(ptr) ChpeRtlLogUnexpectedCodepath
1085 stdcall RtlMoveMemory(ptr ptr long) ChpeRtlMoveMemory
1099 stdcall RtlNtStatusToDosError(long) ChpeRtlNtStatusToDosError
1112 stdcall RtlPcToFileHeader(ptr ptr) ChpeRtlPcToFileHeader
1124 stdcall RtlQueryDepthSList(ptr) ChpeRtlQueryDepthSList
1146 stdcall -norelay RtlRaiseException(ptr) ChpeRtlRaiseException
1147 stdcall RtlRaiseStatus(long) ChpeRtlRaiseStatus
1148 stdcall RtlRandom(ptr) ChpeRtlRandom
1150 stdcall RtlReAllocateHeap(long long ptr long) ChpeRtlReAllocateHeap
1162 stdcall RtlReleasePrivilege(ptr) ChpeRtlReleasePrivilege
1165 stdcall RtlReleaseSRWLockExclusive(ptr) ChpeRtlReleaseSRWLockExclusive
1166 stdcall RtlReleaseSRWLockShared(ptr) ChpeRtlReleaseSRWLockShared
1169 stdcall RtlRemoveVectoredContinueHandler(ptr) ChpeRtlRemoveVectoredContinueHandler
1170 stdcall RtlRemoveVectoredExceptionHandler(ptr) ChpeRtlRemoveVectoredExceptionHandler
1176 stdcall RtlRestoreContext(ptr ptr) ChpeRtlRestoreContext
1177 stdcall RtlRestoreLastWin32Error(long) ChpeRtlRestoreLastWin32Error
1184 stdcall -version=0x600+ RtlRunOnceExecuteOnce(ptr ptr ptr ptr) ChpeRtlRunOnceExecuteOnce
1185 stdcall -version=0x600+ RtlRunOnceInitialize(ptr) ChpeRtlRunOnceInitialize
1236 stdcall RtlSizeHeap(long long ptr) ChpeRtlSizeHeap
1197 stdcall RtlSetCriticalSectionSpinCount(ptr long) ChpeRtlSetCriticalSectionSpinCount
1211 stdcall RtlSetLastWin32Error(long) ChpeRtlSetLastWin32Error
1263 stdcall RtlTryAcquireSRWLockExclusive(ptr) ChpeRtlTryAcquireSRWLockExclusive
1264 stdcall RtlTryAcquireSRWLockShared(ptr) ChpeRtlTryAcquireSRWLockShared
1265 stdcall RtlTryEnterCriticalSection(ptr) ChpeRtlTryEnterCriticalSection
1271 stdcall -version=0x601+ RtlUTF8ToUnicodeN(ptr long ptr str long) ChpeRtlUTF8ToUnicodeN
1289 stdcall RtlUnwind(ptr ptr ptr ptr) ChpeRtlUnwind
1290 stdcall RtlUnwindEx(ptr ptr ptr ptr ptr ptr) ChpeRtlUnwindEx
1314 stdcall RtlVirtualUnwind(long int64 int64 ptr ptr ptr ptr ptr) ChpeRtlVirtualUnwind
1316 stdcall -version=0x602+ RtlWaitOnAddress(ptr ptr long ptr) ChpeRtlWaitOnAddress
1318 stdcall -version=0x602+ RtlWakeAddressAll(ptr) ChpeRtlWakeAddressAll
1319 stdcall -version=0x602+ RtlWakeAddressSingle(ptr) ChpeRtlWakeAddressSingle
1317 stdcall RtlWakeAllConditionVariable(ptr) ChpeRtlWakeAllConditionVariable
1320 stdcall RtlWakeConditionVariable(ptr) ChpeRtlWakeConditionVariable
1345 stdcall RtlZeroMemory(ptr long) ChpeRtlZeroMemory
1399 stdcall -version=0x600+ TpCallbackLeaveCriticalSectionOnCompletion(ptr ptr) ChpeTpCallbackLeaveCriticalSectionOnCompletion
1401 stdcall -version=0x600+ TpCallbackReleaseMutexOnCompletion(ptr ptr) ChpeTpCallbackReleaseMutexOnCompletion
1402 stdcall -version=0x600+ TpCallbackReleaseSemaphoreOnCompletion(ptr ptr long) ChpeTpCallbackReleaseSemaphoreOnCompletion
1405 stdcall -version=0x600+ TpCallbackSetEventOnCompletion(ptr ptr) ChpeTpCallbackSetEventOnCompletion
1406 stdcall -version=0x600+ TpCallbackUnloadDllOnCompletion(ptr ptr) ChpeTpCallbackUnloadDllOnCompletion
1407 stdcall -version=0x600+ TpCancelAsyncIoOperation(ptr) ChpeTpCancelAsyncIoOperation
1412 stdcall -version=0x600+ TpDisassociateCallback(ptr) ChpeTpDisassociateCallback
1413 stdcall -version=0x600+ TpIsTimerSet(ptr) ChpeTpIsTimerSet
1414 stdcall -version=0x600+ TpPostWork(ptr) ChpeTpPostWork
1417 stdcall -version=0x600+ TpReleaseCleanupGroup(ptr) ChpeTpReleaseCleanupGroup
1418 stdcall -version=0x600+ TpReleaseCleanupGroupMembers(ptr long ptr) ChpeTpReleaseCleanupGroupMembers
1419 stdcall -version=0x600+ TpReleaseIoCompletion(ptr) ChpeTpReleaseIoCompletion
1420 stdcall -version=0x600+ TpReleasePool(ptr) ChpeTpReleasePool
1421 stdcall TpReleaseTimer(ptr) ChpeTpReleaseTimer
1422 stdcall TpReleaseWait(ptr) ChpeTpReleaseWait
1423 stdcall -version=0x600+ TpReleaseWork(ptr) ChpeTpReleaseWork
1424 stdcall -version=0x600+ TpSetPoolMaxThreads(ptr long) ChpeTpSetPoolMaxThreads
1425 stdcall -version=0x600+ TpSetPoolMinThreads(ptr long) ChpeTpSetPoolMinThreads
1427 stdcall TpSetTimer(ptr ptr long long) ChpeTpSetTimer
1428 stdcall -version=0x602+ TpSetTimerEx(ptr ptr long long) ChpeTpSetTimerEx
1429 stdcall TpSetWait(ptr long ptr) ChpeTpSetWait
1430 stdcall -version=0x602+ TpSetWaitEx(ptr long ptr ptr) ChpeTpSetWaitEx
1432 stdcall -version=0x600+ TpStartAsyncIoOperation(ptr) ChpeTpStartAsyncIoOperation
1434 stdcall -version=0x600+ TpWaitForIoCompletion(ptr long) ChpeTpWaitForIoCompletion
1435 stdcall TpWaitForTimer(ptr long) ChpeTpWaitForTimer
1436 stdcall -version=0x600+ TpWaitForWait(ptr long) ChpeTpWaitForWait
1437 stdcall -version=0x600+ TpWaitForWork(ptr long) ChpeTpWaitForWork
1438 stdcall -ret64 VerSetConditionMask(double long long) ChpeVerSetConditionMask
1891 varargs _snprintf(ptr long str) ChpeSnprintf
1893 varargs _snwprintf(ptr long wstr) ChpeSnwprintf
1901 varargs _swprintf(ptr wstr) ChpeSwprintf
1959 varargs sprintf(ptr str) ChpeSprintf
1980 varargs swprintf(ptr wstr) ChpeSwprintf
1870 cdecl __chkstk() ChpeChkStk
1884 cdecl _local_unwind(ptr ptr) ChpeLocalUnwind
1953 cdecl memcpy(ptr ptr long) ChpeMemcpy
2010 stdcall ChpeEmulationDispatch(ptr)
1334 stdcall RtlWow64GetThreadSelectorEntry(ptr ptr long ptr) ChpeRtlWow64GetThreadSelectorEntry

277 stdcall NtCreateThread(ptr long ptr long ptr ptr ptr long) ChpeNtCreateThread
326 stdcall NtFsControlFile(long long ptr ptr ptr long ptr long ptr long) ChpeAutoNtFsControlFile
555 stdcall NtSetTimer(long ptr ptr ptr long long ptr) ChpeAutoNtSetTimer
757 stdcall RtlCreateUserThread(long ptr long long ptr ptr ptr ptr ptr) ChpeRtlCreateUserThread
769 stdcall RtlDecompressFragment(long ptr long ptr long long ptr ptr) ChpeRtlDecompressFragment
905 stdcall RtlGenerate8dot3Name(ptr long ptr ptr) ChpeRtlGenerate8dot3Name
935 stdcall RtlGetLengthWithoutLastFullDosOrNtPathElement(long ptr ptr) ChpeRtlGetLengthWithoutLastFullDosOrNtPathElement
955 stdcall RtlGetUnloadEventTrace() ChpeRtlGetUnloadEventTrace
1145 stdcall RtlQueueWorkItem(ptr ptr long) ChpeRtlQueueWorkItem
1158 stdcall RtlRegisterWait(ptr ptr ptr ptr long long) ChpeRtlRegisterWait
1347 stdcall RtlpApplyLengthFunction(long long ptr ptr) ChpeRtlpApplyLengthFunction
1986 stdcall vDbgPrintEx(long long str ptr) ChpevDbgPrintEx
1987 stdcall vDbgPrintExWithPrefix(str long long str ptr) ChpevDbgPrintExWithPrefix
838 stdcall LdrGetDllFullName(ptr ptr) ChpeLdrGetDllFullName
875 stdcall RtlFindExportedRoutineByName(ptr str) ChpeRtlFindExportedRoutineByName
841 stdcall RtlIsEcCode(ptr) ChpeRtlIsEcCode
850 stdcall RtlLocateExtendedFeature(ptr long ptr) ChpeRtlLocateExtendedFeature
851 stdcall RtlLocateExtendedFeature2(ptr long ptr ptr) ChpeRtlLocateExtendedFeature2
853 stdcall RtlQueryPerformanceCounter(ptr) ChpeRtlQueryPerformanceCounter
854 stdcall RtlQueryPerformanceFrequency(ptr) ChpeRtlQueryPerformanceFrequency
856 stdcall RtlQuerySystemTime(ptr) ChpeRtlQuerySystemTime
857 stdcall RtlSystemTimeToTimeFields(ptr ptr) ChpeRtlSystemTimeToTimeFields
43 varargs DbgPrintReturnControlC(str) ChpeDbgPrintReturnControlC
57 stdcall DbgUserBreakPoint() ChpeDbgUserBreakPoint
90 varargs EtwTraceMessage(int64 long ptr long) ChpeEtwTraceMessage
1305 stdcall RtlUserThreadStart(long long) ntdll.RtlUserThreadStart
1961 varargs sscanf(str str) ChpeSscanf
2006 cdecl ChpeVsscanf(str str ptr) ChpeAutoVsscanf
2007 stdcall ChpeVDbgPrintReturnControlC(str ptr) ChpeAutoVDbgPrintReturnControlC
1877 cdecl -private _errno() ChpeErrno
1883 cdecl _lfind(ptr ptr ptr long ptr) ChpeLfind
1925 cdecl bsearch(ptr ptr long long ptr) ChpeBsearch
1957 cdecl qsort(ptr long long ptr) ChpeQsort
1889 cdecl _setjmp(ptr ptr) ChpeSetJmpX64
1890 cdecl _setjmpex(ptr ptr) ChpeSetJmpX64
1949 cdecl longjmp(ptr long) ChpeLongJmp
978 stdcall RtlInitMemoryStream(ptr) ChpeRtlInitMemoryStream
980 stdcall RtlInitOutOfProcessMemoryStream(ptr) ChpeRtlInitOutOfProcessMemoryStream
1160 stdcall RtlReleaseMemoryStream(ptr) ChpeRtlReleaseMemoryStream
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
124 stdcall -version=0x600+ LdrGetFailureData() ChpeStubLdrGetFailureData
125 stdcall -version=0x600+ LdrGetFileNameFromLoadAsDataTable(ptr ptr) ChpeStubLdrGetFileNameFromLoadAsDataTable
126 stdcall -version=0x600+ LdrGetKnownDllSectionHandle(wstr long ptr) ChpeStubLdrGetKnownDllSectionHandle
128 stdcall -version=0x600+ LdrGetProcedureAddressEx(ptr ptr long ptr long) ChpeAutoLdrGetProcedureAddressEx
129 stdcall  LdrHotPatchRoutine(ptr) ChpeStubLdrHotPatchRoutine
130 stdcall LdrInitShimEngineDynamic(ptr) ChpeAutoLdrInitShimEngineDynamic
131 stdcall LdrInitializeThunk(long long long long) ChpeAutoLdrInitializeThunk
132 stdcall LdrLoadAlternateResourceModule(ptr ptr) ChpeAutoLdrLoadAlternateResourceModule
133 stdcall -version=0x600+ LdrLoadAlternateResourceModuleEx(long long ptr ptr long) ChpeStubLdrLoadAlternateResourceModuleEx
135 stdcall LdrLockLoaderLock(long ptr ptr) ChpeAutoLdrLockLoaderLock
136 stdcall LdrOpenImageFileOptionsKey(ptr long ptr) ChpeAutoLdrOpenImageFileOptionsKey
137 stdcall -version=0x600+ LdrProcessInitializationComplete() ChpeStubLdrProcessInitializationComplete
138 stdcall LdrProcessRelocationBlock(ptr long ptr long) ChpeAutoLdrProcessRelocationBlock
139 stdcall LdrQueryImageFileExecutionOptions(ptr str long ptr long ptr) ChpeAutoLdrQueryImageFileExecutionOptions
140 stdcall LdrQueryImageFileExecutionOptionsEx(ptr ptr long ptr long ptr long) ChpeAutoLdrQueryImageFileExecutionOptionsEx
141 stdcall LdrQueryImageFileKeyOption(ptr ptr long ptr long ptr) ChpeAutoLdrQueryImageFileKeyOption
142 stdcall -version=0x600+ LdrQueryModuleServiceTags(ptr ptr ptr) ChpeStubLdrQueryModuleServiceTags
144 stdcall -version=0x600+ LdrRegisterDllNotification(long ptr ptr ptr) ChpeAutoLdrRegisterDllNotification
145 stdcall -version=0x600+ LdrRemoveLoadAsDataTable(ptr ptr ptr long) ChpeStubLdrRemoveLoadAsDataTable
148 stdcall -version=0x600+ LdrResFindResource(ptr long long long ptr ptr ptr ptr long) ChpeAutoLdrResFindResource
149 stdcall -version=0x600+ LdrResFindResourceDirectory(ptr long long ptr ptr ptr long ptr) ChpeAutoLdrResFindResourceDirectory
150 stdcall -version=0x600+ LdrResRelease(ptr ptr long long) ChpeStubLdrResRelease
151 stdcall -version=0x600+ LdrResSearchResource(wstr wstr long long long ptr long long) ChpeStubLdrResSearchResource
152 stdcall LdrSetAppCompatDllRedirectionCallback(long ptr ptr) ChpeAutoLdrSetAppCompatDllRedirectionCallback
153 stdcall LdrSetDllManifestProber(ptr) ChpeAutoLdrSetDllManifestProber
154 stdcall -version=0x600+ LdrSetMUICacheType(long) ChpeStubLdrSetMUICacheType
155 stdcall LdrShutdownProcess() ChpeAutoLdrShutdownProcess
156 stdcall LdrShutdownThread() ChpeAutoLdrShutdownThread
157 extern LdrSystemDllInitBlock ntdll.LdrSystemDllInitBlock
158 stdcall LdrUnloadAlternateResourceModule(ptr) ChpeAutoLdrUnloadAlternateResourceModule
159 stdcall -version=0x600+ LdrUnloadAlternateResourceModuleEx(long long) ChpeStubLdrUnloadAlternateResourceModuleEx
161 stdcall LdrUnlockLoaderLock(long ptr) ChpeAutoLdrUnlockLoaderLock
162 stdcall -version=0x600+ LdrUnregisterDllNotification(ptr) ChpeAutoLdrUnregisterDllNotification
163 stdcall LdrVerifyImageMatchesChecksum(ptr long long long) ChpeAutoLdrVerifyImageMatchesChecksum
164 stdcall -version=0x600+ LdrVerifyImageMatchesChecksumEx(ptr ptr) ChpeStubLdrVerifyImageMatchesChecksumEx
165 stdcall -version=0x600+ LdrpResGetMappingSize(long ptr long long) ChpeStubLdrpResGetMappingSize
166 stdcall -version=0x600+ LdrpResGetRCConfig(long long ptr long long) ChpeStubLdrpResGetRCConfig
167 stdcall -version=0x600+ LdrpResGetResourceDirectory(long long long ptr ptr) ChpeStubLdrpResGetResourceDirectory
168 stdcall -version=0x600+ MD4Final(ptr) ChpeAutoMD4Final
169 stdcall -version=0x600+ MD4Init(ptr) ChpeAutoMD4Init
170 stdcall -version=0x600+ MD4Update(ptr ptr long) ChpeAutoMD4Update
171 stdcall -version=0x600+ MD5Final(ptr) ChpeAutoMD5Final
172 stdcall -version=0x600+ MD5Init(ptr) ChpeAutoMD5Init
173 stdcall -version=0x600+ MD5Update(ptr ptr long) ChpeAutoMD5Update
174 extern NlsAnsiCodePage ntdll.NlsAnsiCodePage
175 extern NlsMbCodePageTag ntdll.NlsMbCodePageTag
176 extern NlsMbOemCodePageTag ntdll.NlsMbOemCodePageTag
177 stdcall NtAcceptConnectPort(ptr long ptr long long ptr) ChpeAutoNtAcceptConnectPort
178 stdcall NtAccessCheck(ptr long long ptr ptr ptr ptr ptr) ChpeAutoNtAccessCheck
179 stdcall NtAccessCheckAndAuditAlarm(ptr long ptr ptr ptr long ptr long ptr ptr ptr) ChpeAutoNtAccessCheckAndAuditAlarm
180 stdcall NtAccessCheckByType(ptr ptr ptr long ptr long ptr ptr long ptr ptr) ChpeAutoNtAccessCheckByType
181 stdcall NtAccessCheckByTypeAndAuditAlarm(ptr ptr ptr ptr ptr ptr long long long ptr long ptr long ptr ptr ptr) ChpeAutoNtAccessCheckByTypeAndAuditAlarm
182 stdcall NtAccessCheckByTypeResultList(ptr ptr ptr long ptr long ptr ptr long ptr ptr) ChpeAutoNtAccessCheckByTypeResultList
183 stdcall NtAccessCheckByTypeResultListAndAuditAlarm(ptr ptr ptr ptr ptr ptr long long long ptr long ptr long ptr ptr ptr) ChpeAutoNtAccessCheckByTypeResultListAndAuditAlarm
184 stdcall NtAccessCheckByTypeResultListAndAuditAlarmByHandle(ptr ptr ptr ptr ptr ptr ptr long long long ptr long ptr long ptr ptr ptr) ChpeAutoNtAccessCheckByTypeResultListAndAuditAlarmByHandle
185 stdcall -version=0x600+ NtAcquireCMFViewOwnership(ptr ptr long) ChpeStubNtAcquireCMFViewOwnership
186 stdcall NtAddAtom(ptr long ptr) ChpeAutoNtAddAtom
187 stdcall NtAddBootEntry(ptr long) ChpeAutoNtAddBootEntry
188 stdcall NtAddDriverEntry(ptr long) ChpeAutoNtAddDriverEntry
189 stdcall NtAdjustGroupsToken(long long ptr long ptr ptr) ChpeAutoNtAdjustGroupsToken
191 stdcall -version=0xA00+ NtAlertMultipleThreadByThreadId(ptr long ptr ptr) ChpeAutoNtAlertMultipleThreadByThreadId
192 stdcall NtAlertResumeThread(long ptr) ChpeAutoNtAlertResumeThread
193 stdcall NtAlertThread(long) ChpeAutoNtAlertThread
195 stdcall NtAllocateLocallyUniqueId(ptr) ChpeAutoNtAllocateLocallyUniqueId
196 stdcall -version=0x600+ NtAllocateReserveObject(ptr ptr long) ChpeAutoNtAllocateReserveObject
197 stdcall NtAllocateUserPhysicalPages(ptr ptr ptr) ChpeAutoNtAllocateUserPhysicalPages
198 stdcall NtAllocateUuids(ptr ptr ptr ptr) ChpeAutoNtAllocateUuids
201 stdcall -version=0x600+ NtAlpcAcceptConnectPort(ptr ptr long ptr ptr ptr ptr ptr long) ChpeAutoNtAlpcAcceptConnectPort
202 stdcall -version=0x600+ NtAlpcCancelMessage(ptr long ptr) ChpeAutoNtAlpcCancelMessage
203 stdcall -version=0x600+ NtAlpcConnectPort(ptr ptr ptr ptr long ptr ptr ptr ptr ptr ptr) ChpeAutoNtAlpcConnectPort
204 stdcall -version=0x602+ NtAlpcConnectPortEx(ptr ptr ptr ptr long ptr ptr ptr ptr ptr ptr) ChpeAutoNtAlpcConnectPortEx
205 stdcall -version=0x600+ NtAlpcCreatePort(ptr ptr ptr) ChpeAutoNtAlpcCreatePort
206 stdcall -version=0x600+ NtAlpcCreatePortSection(ptr long ptr long ptr ptr) ChpeAutoNtAlpcCreatePortSection
207 stdcall -version=0x600+ NtAlpcCreateResourceReserve(ptr long long ptr) ChpeAutoNtAlpcCreateResourceReserve
208 stdcall -version=0x600+ NtAlpcCreateSectionView(ptr long ptr) ChpeAutoNtAlpcCreateSectionView
209 stdcall -version=0x600+ NtAlpcCreateSecurityContext(ptr long ptr) ChpeAutoNtAlpcCreateSecurityContext
210 stdcall -version=0x600+ NtAlpcDeletePortSection(ptr long ptr) ChpeAutoNtAlpcDeletePortSection
211 stdcall -version=0x600+ NtAlpcDeleteResourceReserve(ptr long long) ChpeAutoNtAlpcDeleteResourceReserve
212 stdcall -version=0x600+ NtAlpcDeleteSectionView(ptr long ptr) ChpeAutoNtAlpcDeleteSectionView
213 stdcall -version=0x600+ NtAlpcDeleteSecurityContext(ptr long ptr) ChpeAutoNtAlpcDeleteSecurityContext
214 stdcall -version=0x600+ NtAlpcDisconnectPort(ptr long) ChpeAutoNtAlpcDisconnectPort
215 stdcall -version=0x600+ NtAlpcImpersonateClientOfPort(ptr ptr ptr) ChpeAutoNtAlpcImpersonateClientOfPort
216 stdcall -version=0xA00+ NtAlpcImpersonateClientContainerOfPort(ptr ptr long) ChpeAutoNtAlpcImpersonateClientContainerOfPort
217 stdcall -version=0x600+ NtAlpcOpenSenderProcess(ptr ptr ptr long long ptr) ChpeAutoNtAlpcOpenSenderProcess
218 stdcall -version=0x600+ NtAlpcOpenSenderThread(ptr ptr ptr long long ptr) ChpeAutoNtAlpcOpenSenderThread
219 stdcall -version=0x600+ NtAlpcQueryInformation(ptr long ptr long ptr) ChpeAutoNtAlpcQueryInformation
220 stdcall -version=0x600+ NtAlpcQueryInformationMessage(ptr ptr long ptr long ptr) ChpeAutoNtAlpcQueryInformationMessage
221 stdcall -version=0x600+ NtAlpcRevokeSecurityContext(ptr long ptr) ChpeAutoNtAlpcRevokeSecurityContext
222 stdcall -version=0x600+ NtAlpcSendWaitReceivePort(ptr long ptr ptr ptr ptr ptr ptr) ChpeAutoNtAlpcSendWaitReceivePort
223 stdcall -version=0x600+ NtAlpcSetInformation(ptr long ptr long) ChpeAutoNtAlpcSetInformation
224 stdcall NtApphelpCacheControl(long ptr) ChpeAutoNtApphelpCacheControl
225 stdcall NtAreMappedFilesTheSame(ptr ptr) ChpeAutoNtAreMappedFilesTheSame
226 stdcall NtAssignProcessToJobObject(long long) ChpeAutoNtAssignProcessToJobObject
227 stdcall NtCallbackReturn(ptr long long) ChpeAutoNtCallbackReturn
228 stdcall NtCancelDeviceWakeupRequest(ptr) ChpeAutoNtCancelDeviceWakeupRequest
229 stdcall NtCancelIoFile(long ptr) ChpeAutoNtCancelIoFile
230 stdcall -version=0x600+ NtCancelIoFileEx(long ptr ptr) ChpeAutoNtCancelIoFileEx
231 stdcall -version=0x600+ NtCancelSynchronousIoFile(long ptr ptr) ChpeAutoNtCancelSynchronousIoFile
232 stdcall NtCancelTimer(long ptr) ChpeAutoNtCancelTimer
233 stdcall NtClearEvent(long) ChpeAutoNtClearEvent
235 stdcall NtCloseObjectAuditAlarm(ptr ptr long) ChpeAutoNtCloseObjectAuditAlarm
236 stdcall -version=0x600+ NtCompareObjects(ptr ptr) ChpeAutoNtCompareObjects
237 stdcall -version=0x600+ NtCommitComplete(ptr ptr) ChpeAutoNtCommitComplete
238 stdcall -version=0x600+ NtCommitEnlistment(ptr ptr) ChpeAutoNtCommitEnlistment
239 stdcall -version=0x600+ NtCommitTransaction(ptr long) ChpeAutoNtCommitTransaction
240 stdcall NtCompactKeys(long ptr) ChpeAutoNtCompactKeys
241 stdcall NtCompareTokens(ptr ptr ptr) ChpeAutoNtCompareTokens
242 stdcall NtCompleteConnectPort(ptr) ChpeAutoNtCompleteConnectPort
243 stdcall NtCompressKey(ptr) ChpeAutoNtCompressKey
244 stdcall NtConnectPort(ptr ptr ptr ptr ptr ptr ptr ptr) ChpeAutoNtConnectPort
247 stdcall -version=0xA00+ NtConvertBetweenAuxiliaryCounterAndPerformanceCounter(long ptr ptr ptr) ChpeAutoNtConvertBetweenAuxiliaryCounterAndPerformanceCounter
248 stdcall NtCreateDebugObject(ptr long ptr long) ChpeAutoNtCreateDebugObject
249 stdcall NtCreateDirectoryObject(long long long) ChpeAutoNtCreateDirectoryObject
250 stdcall -version=0x600+ NtCreateEnlistment(ptr long ptr ptr ptr long long ptr) ChpeAutoNtCreateEnlistment
251 stdcall NtCreateEvent(long long long long long) ChpeAutoNtCreateEvent
252 stdcall NtCreateEventPair(ptr long ptr) ChpeAutoNtCreateEventPair
254 stdcall NtCreateIoCompletion(ptr long ptr long) ChpeAutoNtCreateIoCompletion
258 stdcall NtCreateJobObject(ptr long ptr) ChpeAutoNtCreateJobObject
259 stdcall NtCreateJobSet(long ptr long) ChpeAutoNtCreateJobSet
261 stdcall -version=0x600+ NtCreateKeyTransacted(ptr long ptr long ptr long ptr ptr) ChpeAutoNtCreateKeyTransacted
262 stdcall NtCreateKeyedEvent(ptr long ptr long) ChpeAutoNtCreateKeyedEvent
263 stdcall NtCreateMailslotFile(long long long long long long long long) ChpeAutoNtCreateMailslotFile
264 stdcall NtCreateMutant(ptr long ptr long) ChpeAutoNtCreateMutant
266 stdcall NtCreatePagingFile(ptr ptr ptr long) ChpeAutoNtCreatePagingFile
267 stdcall NtCreatePort(ptr ptr long long ptr) ChpeAutoNtCreatePort
268 stdcall -version=0x600+ NtCreatePrivateNamespace(ptr long ptr ptr) ChpeAutoNtCreatePrivateNamespace
269 stdcall NtCreateProcess(ptr long ptr ptr long ptr ptr ptr) ChpeAutoNtCreateProcess
270 stdcall NtCreateProcessEx(ptr long ptr ptr long ptr ptr ptr long) ChpeAutoNtCreateProcessEx
271 stdcall NtCreateProfile(ptr ptr ptr long long ptr long long long) ChpeAutoNtCreateProfile
272 stdcall -version=0x600+ NtCreateResourceManager(ptr long ptr ptr ptr long ptr) ChpeAutoNtCreateResourceManager
273 stdcall NtCreateSection(ptr long ptr ptr long long ptr) ChpeAutoNtCreateSection
274 stdcall NtCreateSectionEx(ptr long ptr ptr long long ptr ptr long) ChpeAutoNtCreateSectionEx
275 stdcall NtCreateSemaphore(ptr long ptr long long) ChpeAutoNtCreateSemaphore
276 stdcall NtCreateSymbolicLinkObject(ptr long ptr ptr) ChpeAutoNtCreateSymbolicLinkObject
278 stdcall -version=0x600+ NtCreateThreadEx(ptr long ptr ptr ptr ptr long long long long ptr) ChpeAutoNtCreateThreadEx
279 stdcall NtCreateTimer(ptr long ptr long) ChpeAutoNtCreateTimer
280 stdcall NtCreateToken(ptr long ptr long ptr ptr ptr ptr ptr ptr ptr ptr ptr) ChpeAutoNtCreateToken
281 stdcall -version=0x600+ NtCreateTransaction(ptr long ptr ptr ptr long long long ptr ptr) ChpeAutoNtCreateTransaction
282 stdcall -version=0x600+ NtCreateTransactionManager(ptr long ptr ptr long long) ChpeAutoNtCreateTransactionManager
283 stdcall -version=0x600+ NtCreateUserProcess(ptr ptr long long ptr ptr long long ptr ptr ptr) ChpeAutoNtCreateUserProcess
284 stdcall NtCreateWaitablePort(ptr ptr long long long) ChpeAutoNtCreateWaitablePort
285 stdcall -version=0x602+ NtCreateWnfStateName(ptr long long long ptr long ptr) ChpeAutoNtCreateWnfStateName
286 stdcall -version=0x600+ NtCreateWorkerFactory(ptr long long long long long long long long long) ChpeStubNtCreateWorkerFactory
287 stdcall NtDebugActiveProcess(ptr ptr) ChpeAutoNtDebugActiveProcess
288 stdcall NtDebugContinue(ptr ptr long) ChpeAutoNtDebugContinue
289 stdcall NtDelayExecution(long ptr) ChpeAutoNtDelayExecution
290 stdcall NtDeleteAtom(long) ChpeAutoNtDeleteAtom
291 stdcall NtDeleteBootEntry(long) ChpeAutoNtDeleteBootEntry
292 stdcall NtDeleteDriverEntry(long) ChpeAutoNtDeleteDriverEntry
293 stdcall NtDeleteFile(ptr) ChpeAutoNtDeleteFile
295 stdcall NtDeleteObjectAuditAlarm(ptr ptr long) ChpeAutoNtDeleteObjectAuditAlarm
296 stdcall -version=0x600+ NtDeletePrivateNamespace(ptr) ChpeAutoNtDeletePrivateNamespace
298 stdcall -version=0x602+ NtDeleteWnfStateData(ptr ptr) ChpeAutoNtDeleteWnfStateData
299 stdcall -version=0x602+ NtDeleteWnfStateName(ptr) ChpeAutoNtDeleteWnfStateName
301 stdcall NtDisplayString(ptr) ChpeAutoNtDisplayString
302 stdcall NtDuplicateObject(long long long ptr long long long) ChpeAutoNtDuplicateObject
303 stdcall NtDuplicateToken(long long long long long long) ChpeAutoNtDuplicateToken
304 stdcall NtEnumerateBootEntries(ptr ptr) ChpeAutoNtEnumerateBootEntries
305 stdcall NtEnumerateDriverEntries(ptr ptr) ChpeAutoNtEnumerateDriverEntries
307 stdcall NtEnumerateSystemEnvironmentValuesEx(long ptr long) ChpeAutoNtEnumerateSystemEnvironmentValuesEx
308 stdcall -version=0x600+ NtEnumerateTransactionObject(ptr long ptr long ptr) ChpeAutoNtEnumerateTransactionObject
310 stdcall NtExtendSection(ptr ptr) ChpeAutoNtExtendSection
311 stdcall NtFilterToken(ptr long ptr ptr ptr ptr) ChpeAutoNtFilterToken
312 stdcall -version=0x602+ NtCreateLowBoxToken(ptr ptr long ptr ptr long ptr long ptr) ChpeAutoNtCreateLowBoxToken
313 stdcall NtFindAtom(ptr long ptr) ChpeAutoNtFindAtom
314 stdcall NtFlushBuffersFile(long ptr) ChpeAutoNtFlushBuffersFile
315 stdcall NtFlushBuffersFileEx(long long ptr long ptr) ChpeAutoNtFlushBuffersFileEx
316 stdcall -version=0x600+ NtFlushInstallUILanguage(long long) ChpeStubNtFlushInstallUILanguage
320 stdcall NtFlushVirtualMemory(ptr ptr ptr ptr) ChpeAutoNtFlushVirtualMemory
321 stdcall NtFlushWriteBuffer() ChpeAutoNtFlushWriteBuffer
322 stdcall NtFreeUserPhysicalPages(ptr ptr ptr) ChpeAutoNtFreeUserPhysicalPages
324 stdcall -version=0x600+ NtFreezeRegistry(long) ChpeStubNtFreezeRegistry
325 stdcall -version=0x600+ NtFreezeTransactions(ptr ptr) ChpeStubNtFreezeTransactions
328 stdcall NtGetCurrentProcessorNumber() ChpeAutoNtGetCurrentProcessorNumber
329 stdcall -version=0xA00+ NtGetCurrentProcessorNumberEx(ptr) ChpeAutoNtGetCurrentProcessorNumberEx
330 stdcall NtGetDevicePowerState(ptr ptr) ChpeAutoNtGetDevicePowerState
331 stdcall -version=0x600+ NtGetMUIRegistryInfo(long ptr ptr) ChpeStubNtGetMUIRegistryInfo
332 stdcall -version=0x600+ NtGetNextProcess(long long long long ptr) ChpeStubNtGetNextProcess
334 stdcall -version=0x600+ NtGetNlsSectionPtr(long long ptr ptr ptr) ChpeAutoNtGetNlsSectionPtr
335 stdcall -version=0x600+ NtGetNotificationResourceManager(ptr ptr long ptr ptr long ptr) ChpeAutoNtGetNotificationResourceManager
336 stdcall NtGetPlugPlayEvent(long long ptr long) ChpeAutoNtGetPlugPlayEvent
337 stdcall NtGetTickCount() ChpeAutoNtGetTickCount
338 stdcall NtGetWriteWatch(long long ptr long ptr ptr ptr) ChpeAutoNtGetWriteWatch
339 stdcall NtImpersonateAnonymousToken(ptr) ChpeAutoNtImpersonateAnonymousToken
340 stdcall NtImpersonateClientOfPort(ptr ptr) ChpeAutoNtImpersonateClientOfPort
341 stdcall NtImpersonateThread(ptr ptr ptr) ChpeAutoNtImpersonateThread
342 stdcall -version=0x600+ NtInitializeNlsFiles(ptr ptr ptr) ChpeAutoNtInitializeNlsFiles
343 stdcall NtInitializeRegistry(long) ChpeAutoNtInitializeRegistry
344 stdcall NtInitiatePowerAction(long long long long) ChpeAutoNtInitiatePowerAction
345 stdcall NtIsProcessInJob(long long) ChpeAutoNtIsProcessInJob
346 stdcall NtIsSystemResumeAutomatic() ChpeAutoNtIsSystemResumeAutomatic
347 stdcall -version=0x600+ NtIsUILanguageComitted() ChpeStubNtIsUILanguageComitted
348 stdcall NtListenPort(ptr ptr) ChpeAutoNtListenPort
349 stdcall NtLoadDriver(ptr) ChpeAutoNtLoadDriver
350 stdcall NtLoadKey2(ptr ptr long) ChpeAutoNtLoadKey2
351 stdcall NtLoadKey(ptr ptr) ChpeAutoNtLoadKey
352 stdcall NtLoadKeyEx(ptr ptr long ptr ptr long ptr ptr) ChpeAutoNtLoadKeyEx
353 stdcall NtLockFile(long long ptr ptr ptr ptr ptr ptr long long) ChpeAutoNtLockFile
354 stdcall NtLockProductActivationKeys(ptr ptr) ChpeAutoNtLockProductActivationKeys
355 stdcall NtLockRegistryKey(ptr) ChpeAutoNtLockRegistryKey
356 stdcall NtLockVirtualMemory(long ptr ptr long) ChpeAutoNtLockVirtualMemory
357 stdcall NtMakePermanentObject(ptr) ChpeAutoNtMakePermanentObject
358 stdcall NtMakeTemporaryObject(long) ChpeAutoNtMakeTemporaryObject
359 stdcall -version=0x600+ NtMapCMFModule(long long ptr ptr ptr ptr) ChpeStubNtMapCMFModule
360 stdcall NtMapUserPhysicalPages(ptr ptr ptr) ChpeAutoNtMapUserPhysicalPages
361 stdcall NtMapUserPhysicalPagesScatter(ptr ptr ptr) ChpeAutoNtMapUserPhysicalPagesScatter
364 stdcall NtModifyBootEntry(ptr) ChpeAutoNtModifyBootEntry
365 stdcall NtModifyDriverEntry(ptr) ChpeAutoNtModifyDriverEntry
366 stdcall NtNotifyChangeDirectoryFile(long long ptr ptr ptr ptr long long long) ChpeAutoNtNotifyChangeDirectoryFile
367 stdcall NtNotifyChangeDirectoryFileEx(long long ptr ptr ptr ptr long long long long) ChpeAutoNtNotifyChangeDirectoryFileEx
370 stdcall NtOpenDirectoryObject(long long long) ChpeAutoNtOpenDirectoryObject
371 stdcall -version=0x600+ NtOpenEnlistment(ptr long ptr ptr ptr) ChpeAutoNtOpenEnlistment
372 stdcall NtOpenEvent(long long long) ChpeAutoNtOpenEvent
373 stdcall NtOpenEventPair(ptr long ptr) ChpeAutoNtOpenEventPair
375 stdcall NtOpenIoCompletion(ptr long ptr) ChpeAutoNtOpenIoCompletion
376 stdcall NtOpenJobObject(ptr long ptr) ChpeAutoNtOpenJobObject
378 stdcall -version=0x600+ NtOpenKeyTransacted(ptr long ptr ptr) ChpeAutoNtOpenKeyTransacted
379 stdcall -version=0x600+ NtOpenKeyTransactedEx(ptr long ptr long ptr) ChpeAutoNtOpenKeyTransactedEx
380 stdcall NtOpenKeyedEvent(ptr long ptr) ChpeAutoNtOpenKeyedEvent
381 stdcall NtOpenMutant(ptr long ptr) ChpeAutoNtOpenMutant
382 stdcall NtOpenObjectAuditAlarm(ptr ptr ptr ptr ptr ptr long long ptr long long ptr) ChpeAutoNtOpenObjectAuditAlarm
383 stdcall -version=0x600+ NtOpenPrivateNamespace(ptr long ptr ptr) ChpeAutoNtOpenPrivateNamespace
384 stdcall NtOpenProcess(ptr long ptr ptr) ChpeAutoNtOpenProcess
385 stdcall NtOpenProcessToken(long long ptr) ChpeAutoNtOpenProcessToken
386 stdcall NtOpenProcessTokenEx(long long long ptr) ChpeAutoNtOpenProcessTokenEx
387 stdcall -version=0x600+ NtOpenResourceManager(ptr long ptr ptr ptr) ChpeAutoNtOpenResourceManager
388 stdcall NtOpenSection(ptr long ptr) ChpeAutoNtOpenSection
389 stdcall NtOpenSemaphore(long long ptr) ChpeAutoNtOpenSemaphore
390 stdcall -version=0x600+ NtOpenSession(ptr long ptr) ChpeStubNtOpenSession
391 stdcall NtOpenSymbolicLinkObject(ptr long ptr) ChpeAutoNtOpenSymbolicLinkObject
392 stdcall NtOpenThread(ptr long ptr ptr) ChpeAutoNtOpenThread
393 stdcall NtOpenThreadToken(long long long ptr) ChpeAutoNtOpenThreadToken
394 stdcall NtOpenThreadTokenEx(long long long long ptr) ChpeAutoNtOpenThreadTokenEx
395 stdcall NtOpenTimer(ptr long ptr) ChpeAutoNtOpenTimer
396 stdcall -version=0x600+ NtOpenTransaction(ptr long ptr ptr ptr) ChpeAutoNtOpenTransaction
397 stdcall -version=0x600+ NtOpenTransactionManager(ptr long ptr ptr ptr long) ChpeAutoNtOpenTransactionManager
398 stdcall NtPlugPlayControl(ptr ptr long) ChpeAutoNtPlugPlayControl
399 stdcall NtPowerInformation(long ptr long ptr long) ChpeAutoNtPowerInformation
400 stdcall -version=0x600+ NtPrePrepareComplete(ptr ptr) ChpeAutoNtPrePrepareComplete
401 stdcall -version=0x600+ NtPrePrepareEnlistment(ptr ptr) ChpeAutoNtPrePrepareEnlistment
402 stdcall -version=0x600+ NtPrepareComplete(ptr ptr) ChpeAutoNtPrepareComplete
403 stdcall -version=0x600+ NtPrepareEnlistment(ptr ptr) ChpeAutoNtPrepareEnlistment
404 stdcall NtPrivilegeCheck(ptr ptr ptr) ChpeAutoNtPrivilegeCheck
405 stdcall NtPrivilegeObjectAuditAlarm(ptr ptr ptr long ptr long) ChpeAutoNtPrivilegeObjectAuditAlarm
406 stdcall NtPrivilegedServiceAuditAlarm(ptr ptr ptr ptr long) ChpeAutoNtPrivilegedServiceAuditAlarm
407 stdcall -version=0x600+ NtPropagationComplete(ptr long long ptr) ChpeAutoNtPropagationComplete
408 stdcall -version=0x600+ NtPropagationFailed(ptr long long) ChpeAutoNtPropagationFailed
410 stdcall NtPulseEvent(long ptr) ChpeAutoNtPulseEvent
411 stdcall NtQueryAttributesFile(ptr ptr) ChpeAutoNtQueryAttributesFile
412 stdcall NtQueryBootEntryOrder(ptr ptr) ChpeAutoNtQueryBootEntryOrder
413 stdcall NtQueryBootOptions(ptr ptr) ChpeAutoNtQueryBootOptions
414 stdcall NtQueryDebugFilterState(long long) ChpeAutoNtQueryDebugFilterState
415 stdcall NtQueryDefaultLocale(long ptr) ChpeAutoNtQueryDefaultLocale
416 stdcall NtQueryDefaultUILanguage(ptr) ChpeAutoNtQueryDefaultUILanguage
418 stdcall NtQueryDirectoryFileEx(long long ptr ptr ptr ptr long long long ptr) ChpeAutoNtQueryDirectoryFileEx
419 stdcall NtQueryDirectoryObject(long ptr long long long ptr ptr) ChpeAutoNtQueryDirectoryObject
420 stdcall NtQueryDriverEntryOrder(ptr ptr) ChpeAutoNtQueryDriverEntryOrder
421 stdcall NtQueryEaFile(long ptr ptr long long ptr long ptr long) ChpeAutoNtQueryEaFile
422 stdcall NtQueryEvent(long long ptr long ptr) ChpeAutoNtQueryEvent
423 stdcall NtQueryFullAttributesFile(ptr ptr) ChpeAutoNtQueryFullAttributesFile
424 stdcall NtQueryInformationAtom(long long ptr long ptr) ChpeAutoNtQueryInformationAtom
425 stdcall NtQueryInformationByName(ptr ptr ptr long long) ChpeAutoNtQueryInformationByName
426 stdcall -version=0x600+ NtQueryInformationEnlistment(ptr long ptr long ptr) ChpeAutoNtQueryInformationEnlistment
428 stdcall NtQueryInformationJobObject(ptr long ptr long ptr) ChpeAutoNtQueryInformationJobObject
429 stdcall NtQueryInformationPort(ptr long ptr long ptr) ChpeAutoNtQueryInformationPort
431 stdcall -version=0x600+ NtQueryInformationResourceManager(ptr long ptr long ptr) ChpeAutoNtQueryInformationResourceManager
433 stdcall NtQueryInformationToken(ptr long ptr long ptr) ChpeAutoNtQueryInformationToken
434 stdcall -version=0x600+ NtQueryInformationTransaction(ptr long ptr long ptr) ChpeAutoNtQueryInformationTransaction
435 stdcall -version=0x600+ NtQueryInformationTransactionManager(ptr long ptr long ptr) ChpeAutoNtQueryInformationTransactionManager
436 stdcall -version=0x600+ NtQueryInformationWorkerFactory(ptr long ptr long ptr) ChpeStubNtQueryInformationWorkerFactory
437 stdcall NtQueryInstallUILanguage(ptr) ChpeAutoNtQueryInstallUILanguage
438 stdcall NtQueryIntervalProfile(long ptr) ChpeAutoNtQueryIntervalProfile
439 stdcall NtQueryIoCompletion(long long ptr long ptr) ChpeAutoNtQueryIoCompletion
441 stdcall -version=0x600+ NtQueryLicenseValue(ptr ptr ptr long ptr) ChpeAutoNtQueryLicenseValue
442 stdcall NtQueryMultipleValueKey(long ptr long ptr long ptr) ChpeAutoNtQueryMultipleValueKey
443 stdcall NtQueryMutant(long long ptr long ptr) ChpeAutoNtQueryMutant
445 stdcall NtQueryOpenSubKeys(ptr ptr) ChpeAutoNtQueryOpenSubKeys
446 stdcall NtQueryOpenSubKeysEx(ptr long ptr ptr) ChpeAutoNtQueryOpenSubKeysEx
447 stdcall NtQueryPerformanceCounter(ptr ptr) ChpeAutoNtQueryPerformanceCounter
448 stdcall NtQueryPortInformationProcess() ChpeAutoNtQueryPortInformationProcess
449 stdcall NtQueryQuotaInformationFile(ptr ptr ptr long long ptr long ptr long) ChpeAutoNtQueryQuotaInformationFile
450 stdcall NtQuerySection(long long long long long) ChpeAutoNtQuerySection
451 stdcall NtQuerySecurityObject(long long long long long) ChpeAutoNtQuerySecurityObject
452 stdcall NtQuerySemaphore(long long ptr long ptr) ChpeAutoNtQuerySemaphore
453 stdcall NtQuerySymbolicLinkObject(long ptr ptr) ChpeAutoNtQuerySymbolicLinkObject
454 stdcall NtQuerySystemEnvironmentValue(ptr ptr long ptr) ChpeAutoNtQuerySystemEnvironmentValue
455 stdcall NtQuerySystemEnvironmentValueEx(ptr ptr ptr ptr ptr) ChpeAutoNtQuerySystemEnvironmentValueEx
457 stdcall -version=0x601+ NtQuerySystemInformationEx(long ptr long ptr long ptr) ChpeAutoNtQuerySystemInformationEx
458 stdcall NtQuerySystemTime(ptr) ChpeAutoNtQuerySystemTime
459 stdcall NtQueryTimer(ptr long ptr long ptr) ChpeAutoNtQueryTimer
460 stdcall NtQueryTimerResolution(long long long) ChpeAutoNtQueryTimerResolution
464 stdcall -version=0x602+ NtQueryWnfStateData(ptr ptr ptr ptr ptr ptr) ChpeAutoNtQueryWnfStateData
465 stdcall -version=0x602+ NtQueryWnfStateNameInformation(ptr long ptr ptr long) ChpeAutoNtQueryWnfStateNameInformation
466 stdcall NtQueueApcThread(long ptr long long long) ChpeAutoNtQueueApcThread
467 stdcall -version=0x601+ NtQueueApcThreadEx(long long ptr long long long) ChpeAutoNtQueueApcThreadEx
468 stdcall -version=0xA00+ NtQueueApcThreadEx2(long long long ptr long long long) ChpeAutoNtQueueApcThreadEx2
470 stdcall NtRaiseHardError(long long long ptr long ptr) ChpeAutoNtRaiseHardError
472 stdcall NtReadFileScatter(long long ptr ptr ptr ptr long ptr ptr) ChpeAutoNtReadFileScatter
473 stdcall -version=0x600+ NtReadOnlyEnlistment(ptr ptr) ChpeAutoNtReadOnlyEnlistment
474 stdcall NtReadRequestData(ptr ptr long ptr long ptr) ChpeAutoNtReadRequestData
476 stdcall -version=0x600+ NtRecoverEnlistment(ptr ptr) ChpeAutoNtRecoverEnlistment
477 stdcall -version=0x600+ NtRecoverResourceManager(ptr) ChpeAutoNtRecoverResourceManager
478 stdcall -version=0x600+ NtRecoverTransactionManager(ptr) ChpeAutoNtRecoverTransactionManager
479 stdcall -version=0x600+ NtRegisterProtocolAddressInformation(ptr ptr long ptr long) ChpeAutoNtRegisterProtocolAddressInformation
480 stdcall NtRegisterThreadTerminatePort(ptr) ChpeAutoNtRegisterThreadTerminatePort
481 stdcall -version=0x600+ NtReleaseCMFViewOwnership() ChpeStubNtReleaseCMFViewOwnership
482 stdcall NtReleaseKeyedEvent(ptr ptr long ptr) ChpeAutoNtReleaseKeyedEvent
483 stdcall NtReleaseMutant(long ptr) ChpeAutoNtReleaseMutant
484 stdcall NtReleaseSemaphore(long long ptr) ChpeAutoNtReleaseSemaphore
485 stdcall -version=0x600+ NtReleaseWorkerFactoryWorker(ptr) ChpeStubNtReleaseWorkerFactoryWorker
486 stdcall NtRemoveIoCompletion(ptr ptr ptr ptr ptr) ChpeAutoNtRemoveIoCompletion
487 stdcall -version=0x600+ NtRemoveIoCompletionEx(ptr ptr long ptr ptr long) ChpeAutoNtRemoveIoCompletionEx
488 stdcall NtRemoveProcessDebug(ptr ptr) ChpeAutoNtRemoveProcessDebug
489 stdcall NtRenameKey(ptr ptr) ChpeAutoNtRenameKey
490 stdcall -version=0x600+ NtRenameTransactionManager(ptr ptr) ChpeAutoNtRenameTransactionManager
491 stdcall NtReplaceKey(ptr long ptr) ChpeAutoNtReplaceKey
492 stdcall -version=0x600+ NtReplacePartitionUnit(wstr wstr long) ChpeStubNtReplacePartitionUnit
493 stdcall NtReplyPort(ptr ptr) ChpeAutoNtReplyPort
494 stdcall NtReplyWaitReceivePort(ptr ptr ptr ptr) ChpeAutoNtReplyWaitReceivePort
495 stdcall NtReplyWaitReceivePortEx(ptr ptr ptr ptr ptr) ChpeAutoNtReplyWaitReceivePortEx
496 stdcall NtReplyWaitReplyPort(ptr ptr) ChpeAutoNtReplyWaitReplyPort
497 stdcall NtRequestDeviceWakeup(ptr) ChpeAutoNtRequestDeviceWakeup
498 stdcall NtRequestPort(ptr ptr) ChpeAutoNtRequestPort
499 stdcall NtRequestWaitReplyPort(ptr ptr ptr) ChpeAutoNtRequestWaitReplyPort
500 stdcall NtRequestWakeupLatency(long) ChpeAutoNtRequestWakeupLatency
501 stdcall NtResetEvent(long ptr) ChpeAutoNtResetEvent
502 stdcall NtResetWriteWatch(long ptr long) ChpeAutoNtResetWriteWatch
503 stdcall NtRestoreKey(long long long) ChpeAutoNtRestoreKey
504 stdcall NtResumeProcess(ptr) ChpeAutoNtResumeProcess
506 stdcall -version=0x600+ NtRollbackComplete(ptr ptr) ChpeAutoNtRollbackComplete
507 stdcall -version=0x600+ NtRollbackEnlistment(ptr ptr) ChpeAutoNtRollbackEnlistment
508 stdcall -version=0x600+ NtRollbackTransaction(ptr long) ChpeAutoNtRollbackTransaction
509 stdcall -version=0x600+ NtRollforwardTransactionManager(ptr ptr) ChpeAutoNtRollforwardTransactionManager
510 stdcall NtSaveKey(long long) ChpeAutoNtSaveKey
511 stdcall NtSaveKeyEx(ptr ptr long) ChpeAutoNtSaveKeyEx
512 stdcall NtSaveMergedKeys(ptr ptr ptr) ChpeAutoNtSaveMergedKeys
513 stdcall NtSecureConnectPort(ptr ptr ptr ptr ptr ptr ptr ptr ptr) ChpeAutoNtSecureConnectPort
514 stdcall NtSetBootEntryOrder(ptr ptr) ChpeAutoNtSetBootEntryOrder
515 stdcall NtSetBootOptions(ptr long) ChpeAutoNtSetBootOptions
517 stdcall NtSetDebugFilterState(long long long) ChpeAutoNtSetDebugFilterState
518 stdcall NtSetDefaultHardErrorPort(ptr) ChpeAutoNtSetDefaultHardErrorPort
519 stdcall NtSetDefaultLocale(long long) ChpeAutoNtSetDefaultLocale
520 stdcall NtSetDefaultUILanguage(long) ChpeAutoNtSetDefaultUILanguage
521 stdcall NtSetDriverEntryOrder(ptr ptr) ChpeAutoNtSetDriverEntryOrder
522 stdcall NtSetEaFile(long ptr ptr long) ChpeAutoNtSetEaFile
523 stdcall NtSetEvent(long long) ChpeAutoNtSetEvent
524 stdcall NtSetEventBoostPriority(ptr) ChpeAutoNtSetEventBoostPriority
525 stdcall NtSetHighEventPair(ptr) ChpeAutoNtSetHighEventPair
526 stdcall NtSetHighWaitLowEventPair(ptr) ChpeAutoNtSetHighWaitLowEventPair
527 stdcall NtSetInformationDebugObject(ptr long ptr long ptr) ChpeAutoNtSetInformationDebugObject
528 stdcall -version=0x600+ NtSetInformationEnlistment(ptr long ptr long) ChpeAutoNtSetInformationEnlistment
529 stdcall NtSetInformationFile(ptr ptr ptr long long) ChpeAutoNtSetInformationFile
530 stdcall NtSetInformationJobObject(ptr long ptr long) ChpeAutoNtSetInformationJobObject
531 stdcall NtSetInformationKey(ptr long ptr long) ChpeAutoNtSetInformationKey
532 stdcall NtSetInformationObject(ptr long ptr long) ChpeAutoNtSetInformationObject
533 stdcall NtSetInformationProcess(ptr long ptr long) ChpeAutoNtSetInformationProcess
534 stdcall -version=0x600+ NtSetInformationResourceManager(ptr long ptr long) ChpeAutoNtSetInformationResourceManager
535 stdcall NtSetInformationThread(ptr long ptr long) ChpeAutoNtSetInformationThread
536 stdcall NtSetInformationVirtualMemory(ptr long ptr ptr ptr long) ChpeAutoNtSetInformationVirtualMemory
537 stdcall NtSetInformationToken(ptr long ptr long) ChpeAutoNtSetInformationToken
538 stdcall -version=0x600+ NtSetInformationTransaction(ptr long ptr long) ChpeAutoNtSetInformationTransaction
539 stdcall -version=0x600+ NtSetInformationTransactionManager(ptr long ptr long) ChpeAutoNtSetInformationTransactionManager
540 stdcall -version=0x600+ NtSetInformationWorkerFactory(ptr long ptr long) ChpeStubNtSetInformationWorkerFactory
541 stdcall NtSetIntervalProfile(long long) ChpeAutoNtSetIntervalProfile
542 stdcall NtSetIoCompletion(ptr long ptr long long) ChpeAutoNtSetIoCompletion
543 stdcall -version=0x600+ NtSetIoCompletionEx(ptr ptr long long long long) ChpeAutoNtSetIoCompletionEx
544 stdcall NtSetLdtEntries(long int64 long int64) ChpeAutoNtSetLdtEntries
545 stdcall NtSetLowEventPair(ptr) ChpeAutoNtSetLowEventPair
546 stdcall NtSetLowWaitHighEventPair(ptr) ChpeAutoNtSetLowWaitHighEventPair
547 stdcall NtSetQuotaInformationFile(ptr ptr ptr long) ChpeAutoNtSetQuotaInformationFile
548 stdcall NtSetSecurityObject(long long ptr) ChpeAutoNtSetSecurityObject
549 stdcall NtSetSystemEnvironmentValue(ptr ptr) ChpeAutoNtSetSystemEnvironmentValue
550 stdcall NtSetSystemEnvironmentValueEx(ptr ptr ptr ptr ptr) ChpeAutoNtSetSystemEnvironmentValueEx
551 stdcall NtSetSystemInformation(long ptr long) ChpeAutoNtSetSystemInformation
552 stdcall NtSetSystemPowerState(long long long) ChpeAutoNtSetSystemPowerState
553 stdcall NtSetSystemTime(ptr ptr) ChpeAutoNtSetSystemTime
554 stdcall NtSetThreadExecutionState(long ptr) ChpeAutoNtSetThreadExecutionState
556 stdcall NtSetTimerResolution(long long ptr) ChpeAutoNtSetTimerResolution
557 stdcall NtSetUuidSeed(ptr) ChpeAutoNtSetUuidSeed
559 stdcall NtSetVolumeInformationFile(long ptr ptr long long) ChpeAutoNtSetVolumeInformationFile
560 stdcall NtShutdownSystem(long) ChpeAutoNtShutdownSystem
561 stdcall -version=0x600+ NtShutdownWorkerFactory(ptr ptr) ChpeStubNtShutdownWorkerFactory
562 stdcall NtSignalAndWaitForSingleObject(long long long ptr) ChpeAutoNtSignalAndWaitForSingleObject
563 stdcall -version=0x600+ NtSinglePhaseReject(ptr ptr) ChpeAutoNtSinglePhaseReject
564 stdcall NtStartProfile(ptr) ChpeAutoNtStartProfile
565 stdcall NtStopProfile(ptr) ChpeAutoNtStopProfile
566 stdcall -version=0x602+ NtSubscribeWnfStateChange(ptr long long ptr) ChpeAutoNtSubscribeWnfStateChange
569 stdcall NtSystemDebugControl(long ptr long ptr long ptr) ChpeAutoNtSystemDebugControl
570 stdcall NtTerminateJobObject(ptr long) ChpeAutoNtTerminateJobObject
573 stdcall NtTestAlert() ChpeAutoNtTestAlert
574 stdcall -version=0x600+ NtThawRegistry() ChpeStubNtThawRegistry
575 stdcall -version=0x600+ NtThawTransactions() ChpeStubNtThawTransactions
576 stdcall -version=0x600+ NtTraceControl(long ptr long ptr long ptr) ChpeAutoNtTraceControl
577 stdcall NtTraceEvent(ptr long long ptr) ChpeAutoNtTraceEvent
578 stdcall NtTranslateFilePath(ptr long ptr long) ChpeAutoNtTranslateFilePath
579 stdcall NtUnloadDriver(ptr) ChpeAutoNtUnloadDriver
580 stdcall NtUnloadKey2(ptr long) ChpeAutoNtUnloadKey2
581 stdcall NtUnloadKey(long) ChpeAutoNtUnloadKey
582 stdcall NtUnloadKeyEx(ptr ptr) ChpeAutoNtUnloadKeyEx
583 stdcall NtUnlockFile(long ptr ptr ptr ptr) ChpeAutoNtUnlockFile
584 stdcall NtUnlockVirtualMemory(long ptr ptr long) ChpeAutoNtUnlockVirtualMemory
587 stdcall -version=0x602+ NtUnsubscribeWnfStateChange(ptr) ChpeAutoNtUnsubscribeWnfStateChange
588 stdcall -version=0x602+ NtUpdateWnfStateData(ptr ptr long ptr ptr long long) ChpeAutoNtUpdateWnfStateData
589 stdcall NtVdmControl(long ptr) ChpeAutoNtVdmControl
590 stdcall NtWaitForDebugEvent(ptr long ptr ptr) ChpeAutoNtWaitForDebugEvent
591 stdcall NtWaitForKeyedEvent(ptr ptr long ptr) ChpeAutoNtWaitForKeyedEvent
593 stdcall NtWaitForMultipleObjects32(long ptr long long ptr) ChpeAutoNtWaitForMultipleObjects32
594 stdcall NtWaitForMultipleObjects(long ptr long long ptr) ChpeAutoNtWaitForMultipleObjects
595 stdcall NtWaitForSingleObject(long long long) ChpeAutoNtWaitForSingleObject
596 stub -version=0x600+ NtWaitForWorkViaWorkerFactory
597 stdcall NtWaitHighEventPair(ptr) ChpeAutoNtWaitHighEventPair
598 stdcall NtWaitLowEventPair(ptr) ChpeAutoNtWaitLowEventPair
599 stdcall -version=0x600+ NtWorkerFactoryWorkerReady(long) ChpeStubNtWorkerFactoryWorkerReady
601 stdcall NtWriteFileGather(long long ptr ptr ptr ptr long ptr ptr) ChpeAutoNtWriteFileGather
602 stdcall NtWriteRequestData(ptr ptr long ptr long ptr) ChpeAutoNtWriteRequestData
604 stdcall NtYieldExecution() ChpeAutoNtYieldExecution
605 stdcall -version=0x600+ NtdllDefWindowProc_A(long long long long) ChpeStubNtdllDefWindowProc_A
606 stdcall -version=0x600+ NtdllDefWindowProc_W(long long long long) ChpeStubNtdllDefWindowProc_W
607 stdcall -version=0x600+ NtdllDialogWndProc_A(long long long long) ChpeStubNtdllDialogWndProc_A
608 stdcall -version=0x600+ NtdllDialogWndProc_W(long long long long) ChpeStubNtdllDialogWndProc_W
609 stdcall PfxFindPrefix(ptr ptr) ChpeAutoPfxFindPrefix
610 stdcall PfxInitialize(ptr) ChpeAutoPfxInitialize
611 stdcall PfxInsertPrefix(ptr ptr ptr) ChpeAutoPfxInsertPrefix
612 stdcall PfxRemovePrefix(ptr ptr) ChpeAutoPfxRemovePrefix
613 stdcall RtlAbortRXact(ptr) ChpeAutoRtlAbortRXact
614 stdcall RtlAbsoluteToSelfRelativeSD(ptr ptr ptr) ChpeAutoRtlAbsoluteToSelfRelativeSD
615 stdcall RtlAcquirePebLock() ChpeAutoRtlAcquirePebLock
617 stdcall RtlAcquireResourceExclusive(ptr long) ChpeAutoRtlAcquireResourceExclusive
618 stdcall RtlAcquireResourceShared(ptr long) ChpeAutoRtlAcquireResourceShared
621 stdcall RtlActivateActivationContext(long ptr ptr) ChpeAutoRtlActivateActivationContext
622 stdcall RtlActivateActivationContextEx(long ptr ptr ptr) ChpeAutoRtlActivateActivationContextEx
623 stdcall RtlActivateActivationContextUnsafeFast(ptr ptr) ChpeAutoRtlActivateActivationContextUnsafeFast
624 stdcall RtlAddAccessAllowedAce(ptr long long ptr) ChpeAutoRtlAddAccessAllowedAce
625 stdcall RtlAddAccessAllowedAceEx(ptr long long long ptr) ChpeAutoRtlAddAccessAllowedAceEx
626 stdcall RtlAddAccessAllowedObjectAce(ptr long long long ptr ptr ptr) ChpeAutoRtlAddAccessAllowedObjectAce
627 stdcall RtlAddAccessDeniedAce(ptr long long ptr) ChpeAutoRtlAddAccessDeniedAce
628 stdcall RtlAddAccessDeniedAceEx(ptr long long long ptr) ChpeAutoRtlAddAccessDeniedAceEx
629 stdcall RtlAddAccessDeniedObjectAce(ptr long long long ptr ptr ptr) ChpeAutoRtlAddAccessDeniedObjectAce
630 stdcall RtlAddAce(ptr long long ptr long) ChpeAutoRtlAddAce
631 stdcall RtlAddActionToRXact(ptr long ptr long ptr long) ChpeAutoRtlAddActionToRXact
632 stdcall RtlAddAtomToAtomTable(ptr wstr ptr) ChpeAutoRtlAddAtomToAtomTable
633 stdcall RtlAddAttributeActionToRXact(ptr long ptr ptr ptr long ptr long) ChpeAutoRtlAddAttributeActionToRXact
634 stdcall RtlAddAuditAccessAce(ptr long long ptr long long) ChpeAutoRtlAddAuditAccessAce
635 stdcall RtlAddAuditAccessAceEx(ptr long long long ptr long long) ChpeAutoRtlAddAuditAccessAceEx
636 stdcall RtlAddAuditAccessObjectAce(ptr long long long ptr ptr ptr long long) ChpeAutoRtlAddAuditAccessObjectAce
637 stdcall  RtlAddCompoundAce(ptr long long long ptr ptr) ChpeStubRtlAddCompoundAce
640 stdcall -version=0x600+ RtlAddMandatoryAce(ptr long long long long ptr) ChpeAutoRtlAddMandatoryAce
641 stdcall RtlAddRefActivationContext(ptr) ChpeAutoRtlAddRefActivationContext
642 stdcall RtlAddRefMemoryStream(ptr) ChpeAutoRtlAddRefMemoryStream
643 stdcall -version=0x600+ RtlAddSIDToBoundaryDescriptor(ptr ptr) ChpeAutoRtlAddSIDToBoundaryDescriptor
644 stdcall -version=0x600+ RtlAddIntegrityLabelToBoundaryDescriptor(ptr ptr) ChpeAutoRtlAddIntegrityLabelToBoundaryDescriptor
647 stdcall  RtlAddressInSectionTable(ptr ptr long) ChpeStubRtlAddressInSectionTable
648 stdcall RtlAdjustPrivilege(long long long ptr) ChpeAutoRtlAdjustPrivilege
649 stdcall RtlAllocateActivationContextStack(ptr) ChpeAutoRtlAllocateActivationContextStack
650 stdcall RtlAllocateAndInitializeSid(ptr long long long long long long long long long ptr) ChpeAutoRtlAllocateAndInitializeSid
651 stdcall RtlAllocateHandle(ptr ptr) ChpeAutoRtlAllocateHandle
653 stdcall -version=0x600+ RtlAllocateMemoryBlockLookaside(ptr long ptr) ChpeStubRtlAllocateMemoryBlockLookaside
654 stdcall -version=0x600+ RtlAllocateMemoryZone(long long ptr) ChpeStubRtlAllocateMemoryZone
655 stdcall RtlAnsiCharToUnicodeChar(ptr) ChpeAutoRtlAnsiCharToUnicodeChar
656 stdcall RtlAnsiStringToUnicodeSize(ptr) ChpeAutoRtlAnsiStringToUnicodeSize
657 stdcall RtlAnsiStringToUnicodeString(ptr ptr long) ChpeAutoRtlAnsiStringToUnicodeString
658 stdcall RtlAppendAsciizToString(ptr str) ChpeAutoRtlAppendAsciizToString
659 stdcall  RtlAppendPathElement(ptr ptr ptr) ChpeStubRtlAppendPathElement
660 stdcall RtlAppendStringToString(ptr ptr) ChpeAutoRtlAppendStringToString
661 stdcall RtlAppendUnicodeStringToString(ptr ptr) ChpeAutoRtlAppendUnicodeStringToString
662 stdcall RtlAppendUnicodeToString(ptr wstr) ChpeAutoRtlAppendUnicodeToString
663 stdcall RtlApplicationVerifierStop(ptr str ptr str ptr str ptr str ptr str) ChpeAutoRtlApplicationVerifierStop
664 stdcall RtlApplyRXact(ptr) ChpeAutoRtlApplyRXact
665 stdcall RtlApplyRXactNoFlush(ptr) ChpeAutoRtlApplyRXactNoFlush
666 stdcall RtlAreAllAccessesGranted(long long) ChpeAutoRtlAreAllAccessesGranted
667 stdcall RtlAreAnyAccessesGranted(long long) ChpeAutoRtlAreAnyAccessesGranted
668 stdcall RtlAreBitsClear(ptr long long) ChpeAutoRtlAreBitsClear
669 stdcall RtlAreBitsSet(ptr long long) ChpeAutoRtlAreBitsSet
670 stdcall RtlAssert(ptr ptr long ptr) ChpeAutoRtlAssert
671 stdcall -version=0x600+ RtlBarrier(ptr long) ChpeStubRtlBarrier
672 stdcall -version=0x600+ RtlBarrierForDelete(long long) ChpeStubRtlBarrierForDelete
673 stdcall RtlCancelTimer(ptr ptr) ChpeAutoRtlCancelTimer
676 stdcall RtlCharToInteger(ptr long ptr) ChpeAutoRtlCharToInteger
677 stdcall RtlCheckForOrphanedCriticalSections(ptr) ChpeAutoRtlCheckForOrphanedCriticalSections
678 stdcall -version=0x602+ RtlCheckPortableOperatingSystem(ptr) ChpeAutoRtlCheckPortableOperatingSystem
679 stdcall RtlCheckRegistryKey(long ptr) ChpeAutoRtlCheckRegistryKey
680 stdcall -version=0x600+ RtlCleanUpTEBLangLists() ChpeStubRtlCleanUpTEBLangLists
681 stdcall RtlClearAllBits(ptr) ChpeAutoRtlClearAllBits
682 stdcall RtlClearBits(ptr long long) ChpeAutoRtlClearBits
683 stdcall RtlCloneMemoryStream(ptr ptr) ChpeAutoRtlCloneMemoryStream
684 stdcall -version=0x600+ RtlCloneUserProcess(long long long long long) ChpeStubRtlCloneUserProcess
685 stdcall -version=0x600+ RtlCmDecodeMemIoResource(ptr ptr) ChpeStubRtlCmDecodeMemIoResource
686 stdcall -version=0x600+ RtlCmEncodeMemIoResource(ptr long long long) ChpeStubRtlCmEncodeMemIoResource
687 stdcall -version=0x600+ RtlCommitDebugInfo(ptr long) ChpeStubRtlCommitDebugInfo
688 stdcall RtlCommitMemoryStream(ptr long) ChpeAutoRtlCommitMemoryStream
689 stdcall RtlCompactHeap(long long) ChpeAutoRtlCompactHeap
690 stdcall -version=0x600+ RtlCompareAltitudes(wstr wstr) ChpeStubRtlCompareAltitudes
692 stdcall RtlCompareMemoryUlong(ptr long long) ChpeAutoRtlCompareMemoryUlong
693 stdcall RtlCompareString(ptr ptr long) ChpeAutoRtlCompareString
695 stdcall -version=0x600+ RtlCompareUnicodeStrings(wstr long wstr long long) ChpeAutoRtlCompareUnicodeStrings
696 stdcall -version=0x600+ RtlCompleteProcessCloning(long) ChpeStubRtlCompleteProcessCloning
697 stdcall RtlCompressBuffer(long ptr long ptr long long ptr ptr) ChpeAutoRtlCompressBuffer
698 stdcall RtlComputeCrc32(long ptr long) ChpeAutoRtlComputeCrc32
699 stdcall RtlComputeImportTableHash(ptr ptr long) ChpeAutoRtlComputeImportTableHash
700 stdcall RtlComputePrivatizedDllName_U(ptr ptr ptr) ChpeAutoRtlComputePrivatizedDllName_U
701 stdcall -version=0x600+ RtlConnectToSm(ptr ptr long ptr) ChpeStubRtlConnectToSm
702 stdcall RtlConsoleMultiByteToUnicodeN(ptr long ptr ptr long ptr) ChpeAutoRtlConsoleMultiByteToUnicodeN
703 stdcall RtlConvertExclusiveToShared(ptr) ChpeAutoRtlConvertExclusiveToShared
704 stdcall -version=0x600+ RtlConvertLCIDToString(long long long ptr long) ChpeAutoRtlConvertLCIDToString
705 stdcall RtlConvertSharedToExclusive(ptr) ChpeAutoRtlConvertSharedToExclusive
706 stdcall RtlConvertSidToUnicodeString(ptr ptr long) ChpeAutoRtlConvertSidToUnicodeString
707 stdcall RtlConvertToAutoInheritSecurityObject(ptr ptr ptr ptr long ptr) ChpeAutoRtlConvertToAutoInheritSecurityObject
708 stdcall RtlConvertUiListToApiList(ptr ptr long) ChpeAutoRtlConvertUiListToApiList
709 stdcall RtlCopyLuid(ptr ptr) ChpeAutoRtlCopyLuid
710 stdcall RtlCopyLuidAndAttributesArray(long ptr ptr) ChpeAutoRtlCopyLuidAndAttributesArray
711 stdcall RtlCopyMappedMemory(ptr ptr long) ChpeAutoRtlCopyMappedMemory
712 stdcall -version=0xA00+ RtlCopyContext(ptr long ptr) ChpeAutoRtlCopyContext
713 stdcall -version=0x600+ RtlCopyExtendedContext(ptr long ptr) ChpeAutoRtlCopyExtendedContext
714 cdecl -version=0x600+ RtlCopyMemory(ptr ptr long) ChpeAutoRtlCopyMemory
715 stdcall -version=0x600+ RtlCopyMemoryNonTemporal(ptr ptr long) ChpeStubRtlCopyMemoryNonTemporal
716 stdcall RtlCopyMemoryStreamTo(ptr ptr int64 ptr ptr) ChpeAutoRtlCopyMemoryStreamTo
717 stdcall RtlCopyOutOfProcessMemoryStreamTo(ptr ptr int64 ptr ptr) ChpeAutoRtlCopyOutOfProcessMemoryStreamTo
718 stdcall RtlCopySecurityDescriptor(ptr ptr) ChpeAutoRtlCopySecurityDescriptor
719 stdcall RtlCopySid(long ptr ptr) ChpeAutoRtlCopySid
720 stdcall RtlCopySidAndAttributesArray(long ptr long ptr ptr ptr ptr) ChpeAutoRtlCopySidAndAttributesArray
721 stdcall RtlCopyString(ptr ptr) ChpeAutoRtlCopyString
723 stdcall RtlCreateAcl(ptr long long) ChpeAutoRtlCreateAcl
724 stdcall RtlCreateActivationContext(long ptr long ptr ptr ptr) ChpeAutoRtlCreateActivationContext
725 stdcall RtlCreateAndSetSD(ptr long ptr ptr ptr) ChpeAutoRtlCreateAndSetSD
726 stdcall RtlContractHashTable(ptr) ChpeAutoRtlContractHashTable
727 stdcall RtlCreateAtomTable(long ptr) ChpeAutoRtlCreateAtomTable
728 stdcall RtlCreateHashTable(ptr long long) ChpeAutoRtlCreateHashTable
729 stdcall RtlCreateBootStatusDataFile() ChpeAutoRtlCreateBootStatusDataFile
730 stdcall -version=0x600+ RtlCreateBoundaryDescriptor(ptr long) ChpeAutoRtlCreateBoundaryDescriptor
731 stdcall RtlCreateEnvironment(long ptr) ChpeAutoRtlCreateEnvironment
732 stdcall -version=0x600+ RtlCreateEnvironmentEx(ptr ptr long) ChpeStubRtlCreateEnvironmentEx
733 stdcall RtlCreateHeap(long ptr long long ptr ptr) ChpeAutoRtlCreateHeap
734 stdcall -version=0x600+ RtlCreateMemoryBlockLookaside(ptr long long long long) ChpeStubRtlCreateMemoryBlockLookaside
735 stdcall -version=0x600+ RtlCreateMemoryZone(ptr long long) ChpeStubRtlCreateMemoryZone
736 stdcall RtlCreateProcessParameters(ptr ptr ptr ptr ptr ptr ptr ptr ptr ptr) ChpeAutoRtlCreateProcessParameters
737 stdcall -version=0x600+ RtlCreateProcessParametersEx(ptr wstr ptr wstr wstr ptr wstr wstr ptr ptr long) ChpeAutoRtlCreateProcessParametersEx
738 stdcall RtlCreateQueryDebugBuffer(long long) ChpeAutoRtlCreateQueryDebugBuffer
739 stdcall RtlCreateRegistryKey(long wstr) ChpeAutoRtlCreateRegistryKey
740 stdcall RtlCreateSecurityDescriptor(ptr long) ChpeAutoRtlCreateSecurityDescriptor
741 stdcall RtlCreateServiceSid(ptr ptr ptr) ChpeAutoRtlCreateServiceSid
742 stdcall -version=0xA00+ RtlDeriveCapabilitySidsFromName(ptr ptr ptr) ChpeAutoRtlDeriveCapabilitySidsFromName
743 stdcall -version=0x602+ RtlIsCapabilitySid(ptr) ChpeAutoRtlIsCapabilitySid
744 stdcall -version=0x602+ RtlGetAppContainerSidType(ptr ptr) ChpeAutoRtlGetAppContainerSidType
745 stdcall -version=0x602+ RtlGetAppContainerParent(ptr ptr) ChpeAutoRtlGetAppContainerParent
746 stdcall -version=0x602+ RtlIsParentOfChildAppContainer(ptr ptr) ChpeAutoRtlIsParentOfChildAppContainer
747 stdcall -version=0x602+ RtlCheckTokenMembershipEx(ptr ptr long ptr) ChpeAutoRtlCheckTokenMembershipEx
748 stdcall RtlCreateSystemVolumeInformationFolder(ptr) ChpeAutoRtlCreateSystemVolumeInformationFolder
749 stdcall RtlCreateTagHeap(ptr long wstr wstr) ChpeAutoRtlCreateTagHeap
750 stdcall RtlCreateTimer(ptr ptr ptr ptr long long long) ChpeAutoRtlCreateTimer
751 stdcall RtlCreateTimerQueue(ptr) ChpeAutoRtlCreateTimerQueue
752 stdcall RtlCreateUnicodeString(ptr wstr) ChpeAutoRtlCreateUnicodeString
753 stdcall RtlCreateUnicodeStringFromAsciiz(ptr str) ChpeAutoRtlCreateUnicodeStringFromAsciiz
754 stdcall RtlCreateUserProcess(ptr long ptr ptr ptr ptr long ptr ptr ptr) ChpeAutoRtlCreateUserProcess
755 stdcall RtlCreateUserSecurityObject(ptr long ptr ptr long ptr ptr) ChpeAutoRtlCreateUserSecurityObject
756 stdcall -version=0x600+ RtlCreateUserStack(long long long long long ptr) ChpeAutoRtlCreateUserStack
758 stdcall -version=0x600+ RtlCultureNameToLCID(ptr ptr) ChpeAutoRtlCultureNameToLCID
759 stdcall RtlCustomCPToUnicodeN(ptr wstr long ptr str long) ChpeAutoRtlCustomCPToUnicodeN
760 stdcall RtlCutoverTimeToSystemTime(ptr ptr ptr long) ChpeAutoRtlCutoverTimeToSystemTime
761 stdcall -version=0x600+ RtlDeCommitDebugInfo(long long long) ChpeStubRtlDeCommitDebugInfo
762 stdcall RtlDeNormalizeProcessParams(ptr) ChpeAutoRtlDeNormalizeProcessParams
763 stdcall RtlDeactivateActivationContext(long long) ChpeAutoRtlDeactivateActivationContext
764 stdcall RtlDeactivateActivationContextUnsafeFast(ptr) ChpeAutoRtlDeactivateActivationContextUnsafeFast
765 stdcall  RtlDebugPrintTimes() ChpeStubRtlDebugPrintTimes
768 stdcall RtlDecompressBuffer(long ptr long ptr long ptr) ChpeAutoRtlDecompressBuffer
770 stdcall RtlDefaultNpAcl(ptr) ChpeAutoRtlDefaultNpAcl
771 stdcall RtlDelete(ptr) ChpeAutoRtlDelete
772 stdcall RtlDeleteAce(ptr long) ChpeAutoRtlDeleteAce
773 stdcall RtlDeleteAtomFromAtomTable(ptr long) ChpeAutoRtlDeleteAtomFromAtomTable
774 stdcall RtlDeleteHashTable(ptr) ChpeAutoRtlDeleteHashTable
775 stdcall -version=0x600+ RtlDeleteBarrier(long) ChpeStubRtlDeleteBarrier
776 stdcall -version=0x600+ RtlDeleteBoundaryDescriptor(ptr) ChpeAutoRtlDeleteBoundaryDescriptor
778 stdcall RtlDeleteElementGenericTable(ptr ptr) ChpeAutoRtlDeleteElementGenericTable
779 stdcall RtlDeleteElementGenericTableAvl(ptr ptr) ChpeAutoRtlDeleteElementGenericTableAvl
782 stdcall RtlDeleteNoSplay(ptr ptr) ChpeAutoRtlDeleteNoSplay
783 stdcall RtlDeleteRegistryValue(long ptr ptr) ChpeAutoRtlDeleteRegistryValue
784 stdcall RtlDeleteResource(ptr) ChpeAutoRtlDeleteResource
785 stdcall RtlDeleteSecurityObject(ptr) ChpeAutoRtlDeleteSecurityObject
786 stdcall RtlDeleteTimer(ptr ptr ptr) ChpeAutoRtlDeleteTimer
787 stdcall RtlDeleteTimerQueue(ptr) ChpeAutoRtlDeleteTimerQueue
788 stdcall RtlDeleteTimerQueueEx(ptr ptr) ChpeAutoRtlDeleteTimerQueueEx
789 stdcall -version=0x600+ RtlDeregisterSecureMemoryCacheCallback(ptr) ChpeStubRtlDeregisterSecureMemoryCacheCallback
790 stdcall RtlDeregisterWait(ptr) ChpeAutoRtlDeregisterWait
791 stdcall RtlDeregisterWaitEx(ptr ptr) ChpeAutoRtlDeregisterWaitEx
792 stdcall RtlDestroyAtomTable(ptr) ChpeAutoRtlDestroyAtomTable
793 stdcall RtlDestroyEnvironment(ptr) ChpeAutoRtlDestroyEnvironment
794 stdcall RtlDestroyHandleTable(ptr) ChpeAutoRtlDestroyHandleTable
795 stdcall RtlDestroyHeap(long) ChpeAutoRtlDestroyHeap
796 stdcall -version=0x600+ RtlDestroyMemoryBlockLookaside(long) ChpeStubRtlDestroyMemoryBlockLookaside
797 stdcall -version=0x600+ RtlDestroyMemoryZone(long) ChpeStubRtlDestroyMemoryZone
798 stdcall RtlDestroyProcessParameters(ptr) ChpeAutoRtlDestroyProcessParameters
799 stdcall RtlDestroyQueryDebugBuffer(ptr) ChpeAutoRtlDestroyQueryDebugBuffer
800 stdcall RtlDetermineDosPathNameType_U(wstr) ChpeAutoRtlDetermineDosPathNameType_U
801 stdcall RtlDllShutdownInProgress() ChpeAutoRtlDllShutdownInProgress
802 stdcall RtlDnsHostNameToComputerName(ptr ptr long) ChpeAutoRtlDnsHostNameToComputerName
803 stdcall RtlDoesFileExists_U(wstr) ChpeAutoRtlDoesFileExists_U
804 stdcall RtlDosApplyFileIsolationRedirection_Ustr(long ptr ptr ptr ptr ptr ptr ptr ptr) ChpeAutoRtlDosApplyFileIsolationRedirection_Ustr
805 stdcall RtlDosPathNameToNtPathName_U(wstr ptr ptr ptr) ChpeAutoRtlDosPathNameToNtPathName_U
806 stdcall RtlDosPathNameToNtPathName_U_WithStatus(wstr ptr ptr ptr) ChpeAutoRtlDosPathNameToNtPathName_U_WithStatus
807 stdcall RtlDosPathNameToRelativeNtPathName_U(ptr ptr ptr ptr) ChpeAutoRtlDosPathNameToRelativeNtPathName_U
808 stdcall RtlDosPathNameToRelativeNtPathName_U_WithStatus(wstr ptr ptr ptr) ChpeAutoRtlDosPathNameToRelativeNtPathName_U_WithStatus
809 stdcall RtlDosSearchPath_U(wstr wstr wstr long ptr ptr) ChpeAutoRtlDosSearchPath_U
810 stdcall RtlDosSearchPath_Ustr(long ptr ptr ptr ptr ptr ptr ptr ptr) ChpeAutoRtlDosSearchPath_Ustr
811 stdcall RtlDowncaseUnicodeChar(long) ChpeAutoRtlDowncaseUnicodeChar
812 stdcall RtlDowncaseUnicodeString(ptr ptr long) ChpeAutoRtlDowncaseUnicodeString
813 stdcall RtlDumpResource(ptr) ChpeAutoRtlDumpResource
814 stdcall RtlDuplicateUnicodeString(long ptr ptr) ChpeAutoRtlDuplicateUnicodeString
815 stdcall RtlEmptyAtomTable(ptr long) ChpeAutoRtlEmptyAtomTable
816 stdcall RtlEndEnumerationHashTable(ptr ptr) ChpeAutoRtlEndEnumerationHashTable
817 stdcall RtlEndWeakEnumerationHashTable(ptr ptr) ChpeAutoRtlEndWeakEnumerationHashTable
818 stdcall  RtlEnableEarlyCriticalSectionEventCreation() ChpeStubRtlEnableEarlyCriticalSectionEventCreation
822 stdcall RtlEnumProcessHeaps(ptr ptr) ChpeAutoRtlEnumProcessHeaps
823 stdcall RtlEnumerateEntryHashTable(ptr ptr) ChpeAutoRtlEnumerateEntryHashTable
824 stdcall RtlEnumerateGenericTable(ptr long) ChpeAutoRtlEnumerateGenericTable
825 stdcall RtlEnumerateGenericTableAvl(ptr long) ChpeAutoRtlEnumerateGenericTableAvl
826 stdcall RtlEnumerateGenericTableLikeADirectory(ptr ptr ptr long ptr ptr ptr) ChpeAutoRtlEnumerateGenericTableLikeADirectory
827 stdcall RtlEnumerateGenericTableWithoutSplaying(ptr ptr) ChpeAutoRtlEnumerateGenericTableWithoutSplaying
828 stdcall RtlEnumerateGenericTableWithoutSplayingAvl(ptr ptr) ChpeAutoRtlEnumerateGenericTableWithoutSplayingAvl
829 stdcall RtlEqualComputerName(ptr ptr) ChpeAutoRtlEqualComputerName
830 stdcall RtlEqualDomainName(ptr ptr) ChpeAutoRtlEqualDomainName
831 stdcall RtlEqualLuid(ptr ptr) ChpeAutoRtlEqualLuid
832 stdcall RtlExpandHashTable(ptr) ChpeAutoRtlExpandHashTable
833 stdcall RtlEqualPrefixSid(ptr ptr) ChpeAutoRtlEqualPrefixSid
834 stdcall RtlEqualSid(long long) ChpeAutoRtlEqualSid
835 stdcall RtlEqualString(ptr ptr long) ChpeAutoRtlEqualString
836 stdcall RtlEqualUnicodeString(ptr ptr long) ChpeAutoRtlEqualUnicodeString
837 stdcall RtlEraseUnicodeString(ptr) ChpeAutoRtlEraseUnicodeString
840 stdcall RtlWow64GetCurrentCpuArea(ptr ptr ptr) ChpeAutoRtlWow64GetCurrentCpuArea
842 stdcall ChpeMarkEcCodeRange(ptr long) ChpeAutoChpeMarkEcCodeRange
843 stdcall ChpeCanContinueToGuest() ChpeAutoChpeCanContinueToGuest
844 stdcall ChpeContinueToGuest(ptr) ChpeAutoChpeContinueToGuest
845 stdcall ChpeContinueToGuestEx(ptr long) ChpeAutoChpeContinueToGuestEx
846 stdcall ProcessPendingCrossProcessEmulatorWork() ChpeAutoProcessPendingCrossProcessEmulatorWork
847 stdcall ChpeIsProcessorFeaturePresent(long) ChpeAutoChpeIsProcessorFeaturePresent
848 stdcall RtlWow64PopAllCrossProcessWorkFromWorkList(ptr ptr) ChpeAutoRtlWow64PopAllCrossProcessWorkFromWorkList
849 stdcall RtlWow64PushCrossProcessWorkOntoFreeList(ptr ptr) ChpeAutoRtlWow64PushCrossProcessWorkOntoFreeList
852 stdcall -version=0x600+ RtlLocateLegacyContext(ptr ptr) ChpeAutoRtlLocateLegacyContext
855 stdcall -version=0x601+ RtlQueryUnbiasedInterruptTime(ptr) ChpeAutoRtlQueryUnbiasedInterruptTime
858 stdcall RtlExitUserProcess(long) ChpeAutoRtlExitUserProcess
860 stdcall -version=0x600+ RtlExpandEnvironmentStrings(long ptr long ptr long ptr) ChpeAutoRtlExpandEnvironmentStrings
861 stdcall RtlExpandEnvironmentStrings_U(ptr ptr ptr ptr) ChpeAutoRtlExpandEnvironmentStrings_U
862 stdcall -version=0x600+ RtlExtendMemoryBlockLookaside(long) ChpeStubRtlExtendMemoryBlockLookaside
863 stdcall -version=0x600+ RtlExtendMemoryZone(long long) ChpeStubRtlExtendMemoryZone
865 stdcall RtlFillMemoryUlong(ptr long long) ChpeAutoRtlFillMemoryUlong
866 stdcall RtlFinalReleaseOutOfProcessMemoryStream(ptr) ChpeAutoRtlFinalReleaseOutOfProcessMemoryStream
867 stdcall -version=0x600+ RtlFindAceByType(long long ptr) ChpeStubRtlFindAceByType
868 stdcall RtlFindActivationContextSectionGuid(long ptr long ptr ptr) ChpeAutoRtlFindActivationContextSectionGuid
869 stdcall RtlFindActivationContextSectionString(long ptr long ptr ptr) ChpeAutoRtlFindActivationContextSectionString
870 stdcall RtlFindCharInUnicodeString(long ptr ptr ptr) ChpeAutoRtlFindCharInUnicodeString
871 stdcall RtlFindClearBits(ptr long long) ChpeAutoRtlFindClearBits
872 stdcall RtlFindClearBitsAndSet(ptr long long) ChpeAutoRtlFindClearBitsAndSet
873 stdcall RtlFindClearRuns(ptr ptr long long) ChpeAutoRtlFindClearRuns
874 stdcall -version=0x600+ RtlFindClosestEncodableLength(long ptr) ChpeStubRtlFindClosestEncodableLength
876 stdcall RtlFindLastBackwardRunClear(ptr long ptr) ChpeAutoRtlFindLastBackwardRunClear
877 stdcall RtlFindLeastSignificantBit(double) ChpeAutoRtlFindLeastSignificantBit
878 stdcall RtlFindLongestRunClear(ptr long) ChpeAutoRtlFindLongestRunClear
879 stdcall RtlFindMessage(long long long long ptr) ChpeAutoRtlFindMessage
880 stdcall RtlFindMostSignificantBit(double) ChpeAutoRtlFindMostSignificantBit
881 stdcall RtlFindNextForwardRunClear(ptr long ptr) ChpeAutoRtlFindNextForwardRunClear
882 stdcall RtlFindSetBits(ptr long long) ChpeAutoRtlFindSetBits
883 stdcall RtlFindSetBitsAndClear(ptr long long) ChpeAutoRtlFindSetBitsAndClear
884 stdcall RtlFirstEntrySList(ptr) ChpeAutoRtlFirstEntrySList
885 stdcall RtlFirstFreeAce(ptr ptr) ChpeAutoRtlFirstFreeAce
890 stdcall RtlFlushSecureMemoryCache(ptr ptr) ChpeAutoRtlFlushSecureMemoryCache
892 stdcall RtlFormatMessage(ptr long long long long ptr ptr long ptr) ChpeAutoRtlFormatMessage
893 stdcall RtlFormatMessageEx(ptr long long long long ptr ptr long ptr long) ChpeAutoRtlFormatMessageEx
894 stdcall RtlFreeActivationContextStack(ptr) ChpeAutoRtlFreeActivationContextStack
895 stdcall RtlFreeAnsiString(long) ChpeAutoRtlFreeAnsiString
896 stdcall RtlFreeHandle(ptr ptr) ChpeAutoRtlFreeHandle
898 stdcall -version=0x600+ RtlFreeMemoryBlockLookaside(long long) ChpeStubRtlFreeMemoryBlockLookaside
899 stdcall RtlFreeOemString(ptr) ChpeAutoRtlFreeOemString
900 stdcall RtlFreeSid(long) ChpeAutoRtlFreeSid
901 stdcall RtlFreeThreadActivationContextStack() ChpeAutoRtlFreeThreadActivationContextStack
903 stdcall -version=0x600+ RtlFreeUserStack(ptr) ChpeAutoRtlFreeUserStack
904 stdcall RtlGUIDFromString(ptr ptr) ChpeAutoRtlGUIDFromString
906 stdcall RtlGetAce(ptr long ptr) ChpeAutoRtlGetAce
907 stdcall RtlGetActiveActivationContext(ptr) ChpeAutoRtlGetActiveActivationContext
908 stdcall RtlGetCallersAddress(ptr ptr) ChpeAutoRtlGetCallersAddress
909 stdcall RtlGetCompressionWorkSpaceSize(long ptr ptr) ChpeAutoRtlGetCompressionWorkSpaceSize
910 stdcall RtlGetControlSecurityDescriptor(ptr ptr ptr) ChpeAutoRtlGetControlSecurityDescriptor
911 stdcall RtlGetCriticalSectionRecursionCount(ptr) ChpeAutoRtlGetCriticalSectionRecursionCount
912 stdcall RtlGetCurrentDirectory_U(long ptr) ChpeAutoRtlGetCurrentDirectory_U
916 stdcall -version=0x600+ RtlGetCurrentTransaction() ChpeStubRtlGetCurrentTransaction
917 stdcall RtlGetDaclSecurityDescriptor(ptr ptr ptr ptr) ChpeAutoRtlGetDaclSecurityDescriptor
918 stdcall -version=0xA00+ RtlGetDeviceFamilyInfoEnum(ptr ptr ptr) ChpeAutoRtlGetDeviceFamilyInfoEnum
919 stdcall -version=0x600+ RtlGetEnabledExtendedFeatures(int64) ChpeAutoRtlGetEnabledExtendedFeatures
920 stdcall RtlGetElementGenericTable(ptr long) ChpeAutoRtlGetElementGenericTable
921 stdcall RtlGetElementGenericTableAvl(ptr long) ChpeAutoRtlGetElementGenericTableAvl
922 stdcall -version=0x600+ RtlGetExtendedContextLength(long ptr) ChpeAutoRtlGetExtendedContextLength
923 stdcall -version=0x600+ RtlGetExtendedContextLength2(long ptr int64) ChpeAutoRtlGetExtendedContextLength2
924 stdcall -version=0x600+ -ret64 RtlGetExtendedFeaturesMask(ptr) ChpeAutoRtlGetExtendedFeaturesMask
925 stdcall -version=0x600+ RtlGetFileMUIPath(long long ptr ptr long long ptr) ChpeStubRtlGetFileMUIPath
926 stdcall RtlGetFrame() ChpeAutoRtlGetFrame
927 stdcall RtlGetFullPathName_U(wstr long ptr ptr) ChpeAutoRtlGetFullPathName_U
928 stdcall RtlGetFullPathName_UstrEx(ptr ptr ptr ptr ptr ptr ptr ptr) ChpeAutoRtlGetFullPathName_UstrEx
930 stdcall RtlGetGroupSecurityDescriptor(ptr ptr ptr) ChpeAutoRtlGetGroupSecurityDescriptor
931 stdcall -version=0x600+ RtlGetIntegerAtom(wstr ptr) ChpeAutoRtlGetIntegerAtom
934 stdcall -version=0x600+ RtlGetLocaleFileMappingAddress(ptr ptr ptr) ChpeAutoRtlGetLocaleFileMappingAddress
936 stdcall RtlGetLengthWithoutTrailingPathSeperators(long ptr ptr) ChpeAutoRtlGetLengthWithoutTrailingPathSeperators
937 stdcall RtlGetLongestNtPathLength() ChpeAutoRtlGetLongestNtPathLength
939 stdcall RtlGetNtGlobalFlags() ChpeAutoRtlGetNtGlobalFlags
940 stdcall RtlGetNtProductType(ptr) ChpeAutoRtlGetNtProductType
941 stdcall RtlGetNtVersionNumbers(ptr ptr ptr) ChpeAutoRtlGetNtVersionNumbers
942 stdcall RtlGetOwnerSecurityDescriptor(ptr ptr ptr) ChpeAutoRtlGetOwnerSecurityDescriptor
943 stdcall -version=0x600+ RtlGetParentLocaleName(wstr long long long) ChpeStubRtlGetParentLocaleName
944 stdcall -version=0x600+ RtlGetProcessPreferredUILanguages(long ptr ptr ptr) ChpeAutoRtlGetProcessPreferredUILanguages
947 stdcall RtlGetSaclSecurityDescriptor(ptr ptr ptr ptr) ChpeAutoRtlGetSaclSecurityDescriptor
948 stdcall RtlGetSecurityDescriptorRMControl(ptr ptr) ChpeAutoRtlGetSecurityDescriptorRMControl
949 stdcall RtlGetSetBootStatusData(ptr long long ptr long long) ChpeAutoRtlGetSetBootStatusData
950 stdcall -version=0x600+ RtlGetSystemPreferredUILanguages(long long ptr ptr ptr) ChpeAutoRtlGetSystemPreferredUILanguages
951 stdcall RtlGetThreadErrorMode() ChpeAutoRtlGetThreadErrorMode
952 stdcall -version=0x600+ RtlGetThreadLangIdByIndex(long long ptr ptr) ChpeStubRtlGetThreadLangIdByIndex
953 stdcall -version=0x600+ RtlGetThreadPreferredUILanguages(long ptr ptr ptr) ChpeAutoRtlGetThreadPreferredUILanguages
954 stdcall -version=0x600+ RtlGetUILanguageInfo(long ptr long ptr ptr) ChpeStubRtlGetUILanguageInfo
956 stdcall -version=0x600+ RtlGetUnloadEventTraceEx(ptr ptr ptr) ChpeAutoRtlGetUnloadEventTraceEx
957 stdcall RtlGetUserInfoHeap(ptr long ptr ptr ptr) ChpeAutoRtlGetUserInfoHeap
958 stdcall -version=0x600+ RtlGetUserPreferredUILanguages(long long ptr ptr ptr) ChpeAutoRtlGetUserPreferredUILanguages
960 stdcall RtlHashUnicodeString(ptr long long ptr) ChpeAutoRtlHashUnicodeString
961 stdcall -version=0x600+ RtlHeapTrkInitialize(ptr) ChpeStubRtlHeapTrkInitialize
962 stdcall RtlIdentifierAuthoritySid(ptr) ChpeAutoRtlIdentifierAuthoritySid
963 stdcall -version=0x600+ RtlIdnToAscii(long wstr long ptr ptr) ChpeAutoRtlIdnToAscii
964 stdcall -version=0x600+ RtlIdnToNameprepUnicode(long wstr long ptr ptr) ChpeAutoRtlIdnToNameprepUnicode
965 stdcall -version=0x600+ RtlIdnToUnicode(long wstr long ptr ptr) ChpeAutoRtlIdnToUnicode
966 stdcall RtlImageDirectoryEntryToData(ptr long long ptr) ChpeAutoRtlImageDirectoryEntryToData
967 stdcall RtlImageNtHeader(long) ChpeAutoRtlImageNtHeader
968 stdcall RtlImageNtHeaderEx(long ptr double ptr) ChpeAutoRtlImageNtHeaderEx
969 stdcall RtlImageRvaToSection(ptr long long) ChpeAutoRtlImageRvaToSection
970 stdcall RtlImageRvaToVa(ptr long long ptr) ChpeAutoRtlImageRvaToVa
971 stdcall RtlImpersonateSelf(long) ChpeAutoRtlImpersonateSelf
972 stdcall -version=0x600+ RtlImpersonateSelfEx(long long ptr) ChpeStubRtlImpersonateSelfEx
973 stdcall RtlInitAnsiString(ptr str) ChpeAutoRtlInitAnsiString
974 stdcall RtlInitAnsiStringEx(ptr str) ChpeAutoRtlInitAnsiStringEx
975 stdcall -version=0xA00+ RtlInitUTF8String(ptr str) ChpeAutoRtlInitUTF8String
976 stdcall -version=0x600+ RtlInitBarrier(long long) ChpeStubRtlInitBarrier
977 stdcall RtlInitCodePageTable(ptr ptr) ChpeAutoRtlInitCodePageTable
979 stdcall RtlInitNlsTables(ptr ptr ptr ptr) ChpeAutoRtlInitNlsTables
981 stdcall RtlInitString(ptr str) ChpeAutoRtlInitString
982 stdcall RtlGetNextEntryHashTable(ptr ptr) ChpeAutoRtlGetNextEntryHashTable
983 stdcall RtlInitEnumerationHashTable(ptr ptr) ChpeAutoRtlInitEnumerationHashTable
985 stdcall RtlInitWeakEnumerationHashTable(ptr ptr) ChpeAutoRtlInitWeakEnumerationHashTable
986 stdcall RtlInitUnicodeStringEx(ptr wstr) ChpeAutoRtlInitUnicodeStringEx
987 stdcall  RtlInitializeAtomPackage(ptr) ChpeStubRtlInitializeAtomPackage
988 stdcall RtlInitializeBitMap(ptr long long) ChpeAutoRtlInitializeBitMap
990 stdcall RtlInitializeContext(ptr ptr ptr ptr ptr) ChpeAutoRtlInitializeContext
992 stdcall RtlInitializeCriticalSectionAndSpinCount(ptr long) ChpeAutoRtlInitializeCriticalSectionAndSpinCount
993 stdcall -version=0x600+ RtlInitializeCriticalSectionEx(ptr long long) ChpeAutoRtlInitializeCriticalSectionEx
994 stdcall -version=0x600+ RtlInitializeExtendedContext(ptr long ptr) ChpeAutoRtlInitializeExtendedContext
995 stdcall -version=0x600+ RtlInitializeExtendedContext2(ptr long ptr int64) ChpeAutoRtlInitializeExtendedContext2
996 stdcall RtlInitializeGenericTable(ptr ptr ptr ptr ptr) ChpeAutoRtlInitializeGenericTable
997 stdcall RtlInitializeGenericTableAvl(ptr ptr ptr ptr ptr) ChpeAutoRtlInitializeGenericTableAvl
998 stdcall RtlInitializeHandleTable(long long ptr) ChpeAutoRtlInitializeHandleTable
999 stdcall -version=0x600+ RtlInitializeNtUserPfn(ptr long ptr long ptr long) ChpeAutoRtlInitializeNtUserPfn
1000 stdcall RtlInitializeRXact(ptr long ptr) ChpeAutoRtlInitializeRXact
1001 stdcall RtlInitializeResource(ptr) ChpeAutoRtlInitializeResource
1004 stdcall RtlInitializeSid(ptr ptr long) ChpeAutoRtlInitializeSid
1005 stdcall RtlInsertElementGenericTable(ptr ptr long ptr) ChpeAutoRtlInsertElementGenericTable
1006 stdcall RtlInsertEntryHashTable(ptr ptr long ptr) ChpeAutoRtlInsertEntryHashTable
1007 stdcall RtlInsertElementGenericTableAvl(ptr ptr long ptr) ChpeAutoRtlInsertElementGenericTableAvl
1008 stdcall RtlInsertElementGenericTableFull(ptr ptr long ptr ptr long) ChpeAutoRtlInsertElementGenericTableFull
1009 stdcall RtlInsertElementGenericTableFullAvl(ptr ptr long ptr ptr long) ChpeAutoRtlInsertElementGenericTableFullAvl
1012 stdcall RtlInt64ToUnicodeString(double long ptr) ChpeAutoRtlInt64ToUnicodeString
1013 stdcall RtlIntegerToChar(long long long ptr) ChpeAutoRtlIntegerToChar
1014 stdcall RtlIntegerToUnicodeString(long long ptr) ChpeAutoRtlIntegerToUnicodeString
1020 stdcall -version=0x600+ RtlIoDecodeMemIoResource(ptr ptr ptr ptr) ChpeStubRtlIoDecodeMemIoResource
1021 stdcall -version=0x600+ RtlIoEncodeMemIoResource(ptr long long long long long) ChpeStubRtlIoEncodeMemIoResource
1022 stdcall RtlIpv4AddressToStringA(ptr ptr) ChpeAutoRtlIpv4AddressToStringA
1023 stdcall RtlIpv4AddressToStringExA(ptr long ptr ptr) ChpeAutoRtlIpv4AddressToStringExA
1024 stdcall RtlIpv4AddressToStringExW(ptr long ptr ptr) ChpeAutoRtlIpv4AddressToStringExW
1025 stdcall RtlIpv4AddressToStringW(ptr ptr) ChpeAutoRtlIpv4AddressToStringW
1026 stdcall RtlIpv4StringToAddressA(str long ptr ptr) ChpeAutoRtlIpv4StringToAddressA
1027 stdcall RtlIpv4StringToAddressExA(str long ptr ptr) ChpeAutoRtlIpv4StringToAddressExA
1028 stdcall RtlIpv4StringToAddressExW(wstr long ptr ptr) ChpeAutoRtlIpv4StringToAddressExW
1029 stdcall RtlIpv4StringToAddressW(wstr long ptr ptr) ChpeAutoRtlIpv4StringToAddressW
1030 stdcall RtlIpv6AddressToStringA(ptr ptr) ChpeAutoRtlIpv6AddressToStringA
1031 stdcall RtlIpv6AddressToStringExA(ptr long long ptr ptr) ChpeAutoRtlIpv6AddressToStringExA
1032 stdcall RtlIpv6AddressToStringExW(ptr long long ptr ptr) ChpeAutoRtlIpv6AddressToStringExW
1033 stdcall RtlIpv6AddressToStringW(ptr ptr) ChpeAutoRtlIpv6AddressToStringW
1034 stdcall RtlIpv6StringToAddressA(str ptr ptr) ChpeAutoRtlIpv6StringToAddressA
1035 stdcall RtlIpv6StringToAddressExA(str ptr ptr ptr) ChpeAutoRtlIpv6StringToAddressExA
1036 stdcall RtlIpv6StringToAddressExW(wstr ptr ptr ptr) ChpeAutoRtlIpv6StringToAddressExW
1037 stdcall RtlIpv6StringToAddressW(wstr ptr ptr) ChpeAutoRtlIpv6StringToAddressW
1038 stdcall RtlIsActivationContextActive(ptr) ChpeAutoRtlIsActivationContextActive
1039 stdcall RtlIsCriticalSectionLocked(ptr) ChpeAutoRtlIsCriticalSectionLocked
1040 stdcall RtlIsCriticalSectionLockedByThread(ptr) ChpeAutoRtlIsCriticalSectionLockedByThread
1041 stdcall -version=0x600+ RtlIsCurrentProcess(ptr) ChpeAutoRtlIsCurrentProcess
1042 stdcall -version=0x600+ RtlIsCurrentThreadAttachExempt() ChpeStubRtlIsCurrentThreadAttachExempt
1043 stdcall RtlIsDosDeviceName_U(wstr) ChpeAutoRtlIsDosDeviceName_U
1044 stdcall RtlIsGenericTableEmpty(ptr) ChpeAutoRtlIsGenericTableEmpty
1045 stdcall RtlIsGenericTableEmptyAvl(ptr) ChpeAutoRtlIsGenericTableEmptyAvl
1046 stdcall -version=0x601+ RtlIsNameInExpression(ptr ptr long ptr) ChpeAutoRtlIsNameInExpression
1047 stdcall RtlIsNameLegalDOS8Dot3(ptr ptr ptr) ChpeAutoRtlIsNameLegalDOS8Dot3
1048 stdcall -version=0x600+ RtlIsNormalizedString(long ptr long ptr) ChpeAutoRtlIsNormalizedString
1050 stdcall RtlIsTextUnicode(ptr long ptr) ChpeAutoRtlIsTextUnicode
1051 stdcall RtlIsThreadWithinLoaderCallout() ChpeAutoRtlIsThreadWithinLoaderCallout
1052 stdcall RtlIsValidHandle(ptr ptr) ChpeAutoRtlIsValidHandle
1053 stdcall RtlIsValidIndexHandle(ptr long ptr) ChpeAutoRtlIsValidIndexHandle
1054 stdcall -version=0x600+ RtlIsValidLocaleName(wstr long) ChpeAutoRtlIsValidLocaleName
1055 stdcall -version=0x600+ RtlLCIDToCultureName(long ptr) ChpeAutoRtlLCIDToCultureName
1056 stdcall RtlLargeIntegerToChar(ptr long long ptr) ChpeAutoRtlLargeIntegerToChar
1057 stdcall -version=0x600+ RtlLcidToLocaleName(long ptr long long) ChpeAutoRtlLcidToLocaleName
1059 stdcall RtlLengthRequiredSid(long) ChpeAutoRtlLengthRequiredSid
1060 stdcall RtlLengthSecurityDescriptor(ptr) ChpeAutoRtlLengthSecurityDescriptor
1061 stdcall RtlLengthSid(ptr) ChpeAutoRtlLengthSid
1062 stdcall RtlLocalTimeToSystemTime(ptr ptr) ChpeAutoRtlLocalTimeToSystemTime
1063 stdcall -version=0x600+ RtlLocaleNameToLcid(wstr ptr long) ChpeAutoRtlLocaleNameToLcid
1064 stdcall RtlLockBootStatusData(ptr) ChpeAutoRtlLockBootStatusData
1065 stdcall -version=0x600+ RtlLockCurrentThread() ChpeStubRtlLockCurrentThread
1066 stdcall RtlLockHeap(long) ChpeAutoRtlLockHeap
1067 stdcall -version=0x600+ RtlLockMemoryBlockLookaside(long) ChpeStubRtlLockMemoryBlockLookaside
1068 stdcall RtlLockMemoryStreamRegion(ptr int64 int64 long) ChpeAutoRtlLockMemoryStreamRegion
1069 stdcall -version=0x600+ RtlLockMemoryZone(long) ChpeStubRtlLockMemoryZone
1070 stdcall -version=0x600+ RtlLockModuleSection(long) ChpeStubRtlLockModuleSection
1072 stdcall  RtlLogStackBackTrace() ChpeStubRtlLogStackBackTrace
1073 stdcall RtlLookupAtomInAtomTable(ptr wstr ptr) ChpeAutoRtlLookupAtomInAtomTable
1074 stdcall RtlLookupElementGenericTable(ptr ptr) ChpeAutoRtlLookupElementGenericTable
1075 stdcall RtlLookupEntryHashTable(ptr long ptr) ChpeAutoRtlLookupEntryHashTable
1076 stdcall RtlRemoveEntryHashTable(ptr ptr ptr) ChpeAutoRtlRemoveEntryHashTable
1077 stdcall RtlLookupElementGenericTableAvl(ptr ptr) ChpeAutoRtlLookupElementGenericTableAvl
1078 stdcall RtlLookupElementGenericTableFull(ptr ptr ptr long) ChpeAutoRtlLookupElementGenericTableFull
1079 stdcall RtlLookupElementGenericTableFullAvl(ptr ptr ptr long) ChpeAutoRtlLookupElementGenericTableFullAvl
1082 stdcall RtlMakeSelfRelativeSD(ptr ptr ptr) ChpeAutoRtlMakeSelfRelativeSD
1083 stdcall RtlMapGenericMask(long ptr) ChpeAutoRtlMapGenericMask
1084 stdcall RtlMapSecurityErrorToNtStatus(long) ChpeAutoRtlMapSecurityErrorToNtStatus
1086 stdcall RtlMultiAppendUnicodeStringBuffer(ptr long ptr) ChpeAutoRtlMultiAppendUnicodeStringBuffer
1087 stdcall RtlMultiByteToUnicodeN(ptr long ptr ptr long) ChpeAutoRtlMultiByteToUnicodeN
1088 stdcall RtlMultiByteToUnicodeSize(ptr str long) ChpeAutoRtlMultiByteToUnicodeSize
1089 stdcall RtlMultipleAllocateHeap(ptr long ptr long ptr) ChpeAutoRtlMultipleAllocateHeap
1090 stdcall RtlMultipleFreeHeap(ptr long long ptr) ChpeAutoRtlMultipleFreeHeap
1091 stdcall RtlNewInstanceSecurityObject(long long ptr ptr ptr ptr ptr long ptr ptr) ChpeAutoRtlNewInstanceSecurityObject
1092 stdcall RtlNewSecurityGrantedAccess(long ptr ptr ptr ptr ptr) ChpeAutoRtlNewSecurityGrantedAccess
1093 stdcall RtlNewSecurityObject(ptr ptr ptr long ptr ptr) ChpeAutoRtlNewSecurityObject
1094 stdcall RtlNewSecurityObjectEx(ptr ptr ptr ptr long long ptr ptr) ChpeAutoRtlNewSecurityObjectEx
1095 stdcall RtlNewSecurityObjectWithMultipleInheritance(ptr ptr ptr ptr long long long ptr ptr) ChpeAutoRtlNewSecurityObjectWithMultipleInheritance
1096 stdcall RtlNormalizeProcessParams(ptr) ChpeAutoRtlNormalizeProcessParams
1097 stdcall -version=0x600+ RtlNormalizeString(long ptr long ptr ptr) ChpeAutoRtlNormalizeString
1098 stdcall RtlNtPathNameToDosPathName(long ptr ptr ptr) ChpeAutoRtlNtPathNameToDosPathName
1100 stdcall RtlNtStatusToDosErrorNoTeb(long) ChpeAutoRtlNtStatusToDosErrorNoTeb
1101 stub -version=0x600+ RtlNtdllName
1102 stdcall RtlNumberGenericTableElements(ptr) ChpeAutoRtlNumberGenericTableElements
1103 stdcall RtlNumberGenericTableElementsAvl(ptr) ChpeAutoRtlNumberGenericTableElementsAvl
1104 stdcall RtlNumberOfClearBits(ptr) ChpeAutoRtlNumberOfClearBits
1105 stdcall RtlNumberOfSetBits(ptr) ChpeAutoRtlNumberOfSetBits
1106 stdcall -version=0x600+ RtlNumberOfSetBitsUlongPtr(long) ChpeAutoRtlNumberOfSetBitsUlongPtr
1107 stdcall RtlOemStringToUnicodeSize(ptr) ChpeAutoRtlOemStringToUnicodeSize
1108 stdcall RtlOemStringToUnicodeString(ptr ptr long) ChpeAutoRtlOemStringToUnicodeString
1109 stdcall RtlOemToUnicodeN(ptr long ptr ptr long) ChpeAutoRtlOemToUnicodeN
1110 stdcall RtlOpenCurrentUser(long ptr) ChpeAutoRtlOpenCurrentUser
1111 stdcall -version=0x600+ RtlOwnerAcesPresent(long) ChpeStubRtlOwnerAcesPresent
1113 stdcall RtlPinAtomInAtomTable(ptr long) ChpeAutoRtlPinAtomInAtomTable
1114 stdcall RtlPopFrame(ptr) ChpeAutoRtlPopFrame
1115 stdcall RtlPrefixString(ptr ptr long) ChpeAutoRtlPrefixString
1116 stdcall RtlPrefixUnicodeString(ptr ptr long) ChpeAutoRtlPrefixUnicodeString
1117 stdcall -version=0x600+ RtlPrepareForProcessCloning() ChpeStubRtlPrepareForProcessCloning
1118 stdcall -version=0x600+ RtlProcessFlsData(ptr long) ChpeAutoRtlProcessFlsData
1119 stdcall RtlProtectHeap(ptr long) ChpeAutoRtlProtectHeap
1120 stdcall RtlPushFrame(ptr) ChpeAutoRtlPushFrame
1121 stdcall -version=0x600+ RtlQueryActivationContextApplicationSettings(long ptr wstr wstr ptr ptr ptr) ChpeAutoRtlQueryActivationContextApplicationSettings
1122 stdcall RtlQueryAtomInAtomTable(ptr long ptr ptr ptr ptr) ChpeAutoRtlQueryAtomInAtomTable
1123 stdcall -version=0x600+ RtlQueryCriticalSectionOwner(ptr) ChpeStubRtlQueryCriticalSectionOwner
1125 stdcall -version=0x600+ RtlQueryDynamicTimeZoneInformation(ptr) ChpeAutoRtlQueryDynamicTimeZoneInformation
1126 stdcall -version=0x600+ RtlQueryElevationFlags(ptr) ChpeStubRtlQueryElevationFlags
1128 stdcall RtlQueryEnvironmentVariable_U(ptr ptr ptr) ChpeAutoRtlQueryEnvironmentVariable_U
1129 stdcall RtlQueryHeapInformation(long long ptr long ptr) ChpeAutoRtlQueryHeapInformation
1130 stdcall RtlQueryInformationAcl(ptr ptr long long) ChpeAutoRtlQueryInformationAcl
1131 stdcall RtlQueryInformationActivationContext(long long ptr long ptr long ptr) ChpeAutoRtlQueryInformationActivationContext
1132 stdcall RtlQueryInformationActiveActivationContext(long ptr long ptr) ChpeAutoRtlQueryInformationActiveActivationContext
1133 stdcall RtlQueryInterfaceMemoryStream(ptr ptr ptr) ChpeAutoRtlQueryInterfaceMemoryStream
1134 stdcall -version=0x600+ RtlQueryModuleInformation(ptr long ptr) ChpeStubRtlQueryModuleInformation
1135 stdcall  RtlQueryProcessBackTraceInformation(ptr) ChpeStubRtlQueryProcessBackTraceInformation
1136 stdcall RtlQueryProcessDebugInformation(long long ptr) ChpeAutoRtlQueryProcessDebugInformation
1137 stdcall RtlQueryProcessHeapInformation(ptr) ChpeAutoRtlQueryProcessHeapInformation
1138 stdcall  RtlQueryProcessLockInformation(ptr) ChpeStubRtlQueryProcessLockInformation
1139 stdcall RtlQueryRegistryValues(long ptr ptr ptr ptr) ChpeAutoRtlQueryRegistryValues
1140 stdcall RtlQueryRegistryValuesEx(long ptr ptr ptr ptr) ChpeAutoRtlQueryRegistryValuesEx
1141 stdcall RtlQuerySecurityObject(ptr long ptr long ptr) ChpeAutoRtlQuerySecurityObject
1142 stdcall RtlQueryTagHeap(ptr long long long ptr) ChpeAutoRtlQueryTagHeap
1143 stdcall RtlQueryTimeZoneInformation(ptr) ChpeAutoRtlQueryTimeZoneInformation
1144 stdcall RtlQueueApcWow64Thread(ptr ptr ptr ptr ptr) ChpeAutoRtlQueueApcWow64Thread
1149 stdcall RtlRandomEx(ptr) ChpeAutoRtlRandomEx
1151 stdcall RtlReadMemoryStream(ptr ptr long ptr) ChpeAutoRtlReadMemoryStream
1152 stdcall RtlReadOutOfProcessMemoryStream(ptr ptr long ptr) ChpeAutoRtlReadOutOfProcessMemoryStream
1153 stdcall RtlRealPredecessor(ptr) ChpeAutoRtlRealPredecessor
1154 stdcall RtlRealSuccessor(ptr) ChpeAutoRtlRealSuccessor
1155 stdcall RtlRegisterSecureMemoryCacheCallback(ptr) ChpeAutoRtlRegisterSecureMemoryCacheCallback
1156 stdcall RtlRegisterCfgTargetRange(ptr long) ChpeAutoRtlRegisterCfgTargetRange
1157 stdcall -version=0x600+ RtlRegisterThreadWithCsrss() ChpeStubRtlRegisterThreadWithCsrss
1159 stdcall RtlReleaseActivationContext(ptr) ChpeAutoRtlReleaseActivationContext
1161 stdcall RtlReleasePebLock() ChpeAutoRtlReleasePebLock
1163 stdcall RtlReleaseRelativeName(ptr) ChpeAutoRtlReleaseRelativeName
1164 stdcall RtlReleaseResource(ptr) ChpeAutoRtlReleaseResource
1167 stdcall RtlRemoteCall(ptr ptr ptr long ptr long long) ChpeAutoRtlRemoteCall
1168 stdcall -version=0x600+ RtlRemovePrivileges(ptr ptr long) ChpeAutoRtlRemovePrivileges
1171 stdcall -version=0x600+ RtlReportException(long long long) ChpeStubRtlReportException
1172 stdcall -version=0x600+ RtlResetMemoryBlockLookaside(long) ChpeStubRtlResetMemoryBlockLookaside
1173 stdcall -version=0x600+ RtlResetMemoryZone(long) ChpeStubRtlResetMemoryZone
1174 stdcall -version=0x600+ RtlResetNtUserPfn() ChpeAutoRtlResetNtUserPfn
1175 stdcall RtlResetRtlTranslations(ptr) ChpeAutoRtlResetRtlTranslations
1178 stdcall -version=0x600+ RtlRetrieveNtUserPfn(ptr ptr ptr) ChpeAutoRtlRetrieveNtUserPfn
1179 stdcall RtlRevertMemoryStream(ptr) ChpeAutoRtlRevertMemoryStream
1180 stdcall RtlRunDecodeUnicodeString(long ptr) ChpeAutoRtlRunDecodeUnicodeString
1181 stdcall RtlRunEncodeUnicodeString(long ptr) ChpeAutoRtlRunEncodeUnicodeString
1182 stdcall -version=0x600+ RtlRunOnceBeginInitialize(ptr long ptr) ChpeAutoRtlRunOnceBeginInitialize
1183 stdcall -version=0x600+ RtlRunOnceComplete(ptr long ptr) ChpeAutoRtlRunOnceComplete
1186 stdcall RtlSecondsSince1970ToTime(long ptr) ChpeAutoRtlSecondsSince1970ToTime
1187 stdcall RtlSecondsSince1980ToTime(long ptr) ChpeAutoRtlSecondsSince1980ToTime
1188 stdcall RtlSeekMemoryStream(ptr int64 long ptr) ChpeAutoRtlSeekMemoryStream
1189 stdcall RtlSelfRelativeToAbsoluteSD2(ptr ptr) ChpeAutoRtlSelfRelativeToAbsoluteSD2
1190 stdcall RtlSelfRelativeToAbsoluteSD(ptr ptr ptr ptr ptr ptr ptr ptr ptr ptr ptr) ChpeAutoRtlSelfRelativeToAbsoluteSD
1191 stdcall -version=0x600+ RtlSendMsgToSm(ptr ptr) ChpeStubRtlSendMsgToSm
1192 stdcall RtlSetAllBits(ptr) ChpeAutoRtlSetAllBits
1193 stdcall RtlSetAttributesSecurityDescriptor(ptr long ptr) ChpeAutoRtlSetAttributesSecurityDescriptor
1194 stdcall RtlSetBits(ptr long long) ChpeAutoRtlSetBits
1195 stdcall RtlSetCfgTargetValidity(ptr long long ptr) ChpeAutoRtlSetCfgTargetValidity
1196 stdcall RtlSetControlSecurityDescriptor(ptr long long) ChpeAutoRtlSetControlSecurityDescriptor
1198 stdcall RtlSetCurrentDirectory_U(ptr) ChpeAutoRtlSetCurrentDirectory_U
1199 stdcall RtlSetCurrentEnvironment(wstr ptr) ChpeAutoRtlSetCurrentEnvironment
1200 stdcall -version=0x600+ RtlSetCurrentTransaction(ptr) ChpeStubRtlSetCurrentTransaction
1201 stdcall RtlSetDaclSecurityDescriptor(ptr long ptr long) ChpeAutoRtlSetDaclSecurityDescriptor
1202 stdcall -version=0x600+ RtlSetDynamicTimeZoneInformation(long) ChpeStubRtlSetDynamicTimeZoneInformation
1203 stdcall RtlSetEnvironmentStrings(wstr long) ChpeAutoRtlSetEnvironmentStrings
1204 stdcall -version=0x600+ RtlSetEnvironmentVar(ptr ptr long ptr long) ChpeStubRtlSetEnvironmentVar
1205 stdcall RtlSetEnvironmentVariable(ptr ptr ptr) ChpeAutoRtlSetEnvironmentVariable
1206 stdcall -version=0x600+ RtlSetExtendedFeaturesMask(ptr int64) ChpeAutoRtlSetExtendedFeaturesMask
1207 stdcall RtlSetGroupSecurityDescriptor(ptr ptr long) ChpeAutoRtlSetGroupSecurityDescriptor
1208 stdcall RtlSetHeapInformation(ptr long ptr ptr) ChpeAutoRtlSetHeapInformation
1209 stdcall RtlSetInformationAcl(ptr ptr long long) ChpeAutoRtlSetInformationAcl
1210 stdcall RtlSetIoCompletionCallback(long ptr long) ChpeAutoRtlSetIoCompletionCallback
1212 stdcall RtlSetLastWin32ErrorAndNtStatusFromNtStatus(long) ChpeAutoRtlSetLastWin32ErrorAndNtStatusFromNtStatus
1213 stdcall RtlSetMemoryStreamSize(ptr int64) ChpeAutoRtlSetMemoryStreamSize
1214 stdcall RtlSetOwnerSecurityDescriptor(ptr ptr long) ChpeAutoRtlSetOwnerSecurityDescriptor
1215 stdcall -version=0x600+ RtlSetProcessDebugInformation(ptr long ptr) ChpeStubRtlSetProcessDebugInformation
1216 stdcall -version=0x600+ RtlSetProcessPreferredUILanguages(long ptr ptr) ChpeAutoRtlSetProcessPreferredUILanguages
1217 cdecl RtlSetProcessIsCritical(long ptr long) ChpeAutoRtlSetProcessIsCritical
1218 stdcall RtlSetSaclSecurityDescriptor(ptr long ptr long) ChpeAutoRtlSetSaclSecurityDescriptor
1219 stdcall RtlSetSecurityDescriptorRMControl(ptr ptr) ChpeAutoRtlSetSecurityDescriptorRMControl
1220 stdcall RtlSetSecurityObject(long ptr ptr ptr ptr) ChpeAutoRtlSetSecurityObject
1221 stdcall RtlSetSecurityObjectEx(long ptr ptr long ptr ptr) ChpeAutoRtlSetSecurityObjectEx
1222 stdcall RtlSetThreadErrorMode(long ptr) ChpeAutoRtlSetThreadErrorMode
1223 cdecl RtlSetThreadIsCritical(long ptr long) ChpeAutoRtlSetThreadIsCritical
1224 stdcall RtlSetThreadPoolStartFunc(ptr ptr) ChpeAutoRtlSetThreadPoolStartFunc
1225 stdcall -version=0x600+ RtlSetThreadPreferredUILanguages(long ptr ptr) ChpeStubRtlSetThreadPreferredUILanguages
1226 stdcall RtlSetTimeZoneInformation(ptr) ChpeAutoRtlSetTimeZoneInformation
1227 stdcall RtlSetTimer(ptr ptr ptr ptr long long long) ChpeAutoRtlSetTimer
1228 stdcall RtlSetUnhandledExceptionFilter(ptr) ChpeAutoRtlSetUnhandledExceptionFilter
1229 stdcall RtlSetUserFlagsHeap(ptr long ptr long long) ChpeAutoRtlSetUserFlagsHeap
1230 stdcall RtlSetUserValueHeap(ptr long ptr ptr) ChpeAutoRtlSetUserValueHeap
1231 stdcall -version=0x600+ RtlSidDominates(long long ptr) ChpeStubRtlSidDominates
1232 stdcall -version=0x600+ RtlSidEqualLevel(long long ptr) ChpeStubRtlSidEqualLevel
1233 stdcall -version=0x600+ RtlSidHashInitialize(ptr long ptr) ChpeStubRtlSidHashInitialize
1234 stdcall -version=0x600+ RtlSidHashLookup(long ptr) ChpeStubRtlSidHashLookup
1235 stdcall -version=0x600+ RtlSidIsHigherLevel(long long ptr) ChpeStubRtlSidIsHigherLevel
1237 stdcall -version=0x600+ RtlSleepConditionVariableCS(ptr ptr ptr) ChpeAutoRtlSleepConditionVariableCS
1238 stdcall -version=0x600+ RtlSleepConditionVariableSRW(ptr ptr ptr long) ChpeAutoRtlSleepConditionVariableSRW
1239 stdcall RtlSplay(ptr) ChpeAutoRtlSplay
1240 stdcall RtlStartRXact(ptr) ChpeAutoRtlStartRXact
1241 stdcall RtlStatMemoryStream(ptr ptr long) ChpeAutoRtlStatMemoryStream
1242 stdcall RtlStringFromGUID(ptr ptr) ChpeAutoRtlStringFromGUID
1243 stdcall RtlSubAuthorityCountSid(ptr) ChpeAutoRtlSubAuthorityCountSid
1244 stdcall RtlSubAuthoritySid(ptr long) ChpeAutoRtlSubAuthoritySid
1245 stdcall RtlSubtreePredecessor(ptr) ChpeAutoRtlSubtreePredecessor
1246 stdcall RtlSubtreeSuccessor(ptr) ChpeAutoRtlSubtreeSuccessor
1247 stdcall RtlSystemTimeToLocalTime(ptr ptr) ChpeAutoRtlSystemTimeToLocalTime
1248 stdcall -version=0x600+ RtlTestBit(ptr long) ChpeAutoRtlTestBit
1249 stdcall RtlTimeFieldsToTime(ptr ptr) ChpeAutoRtlTimeFieldsToTime
1250 stdcall RtlTimeToElapsedTimeFields(long long) ChpeAutoRtlTimeToElapsedTimeFields
1251 stdcall RtlTimeToSecondsSince1970(ptr ptr) ChpeAutoRtlTimeToSecondsSince1970
1252 stdcall RtlTimeToSecondsSince1980(ptr ptr) ChpeAutoRtlTimeToSecondsSince1980
1253 stdcall RtlTimeToTimeFields(long long) ChpeAutoRtlTimeToTimeFields
1254 stdcall RtlTraceDatabaseAdd(ptr long ptr ptr) ChpeAutoRtlTraceDatabaseAdd
1255 stdcall RtlTraceDatabaseCreate(long ptr long long ptr) ChpeAutoRtlTraceDatabaseCreate
1256 stdcall RtlTraceDatabaseDestroy(ptr) ChpeAutoRtlTraceDatabaseDestroy
1257 stdcall RtlTraceDatabaseEnumerate(ptr ptr ptr) ChpeAutoRtlTraceDatabaseEnumerate
1258 stdcall RtlTraceDatabaseFind(ptr long ptr ptr) ChpeAutoRtlTraceDatabaseFind
1259 stdcall RtlTraceDatabaseLock(ptr) ChpeAutoRtlTraceDatabaseLock
1260 stdcall RtlTraceDatabaseUnlock(ptr) ChpeAutoRtlTraceDatabaseUnlock
1261 stdcall RtlTraceDatabaseValidate(ptr) ChpeAutoRtlTraceDatabaseValidate
1262 stdcall -version=0x600+ RtlTryAcquirePebLock() ChpeStubRtlTryAcquirePebLock
1266 stdcall RtlUnhandledExceptionFilter2(ptr long) ChpeAutoRtlUnhandledExceptionFilter2
1267 stdcall RtlUnhandledExceptionFilter(ptr) ChpeAutoRtlUnhandledExceptionFilter
1268 stdcall RtlUnicodeStringToAnsiSize(ptr) ChpeAutoRtlUnicodeStringToAnsiSize
1269 stdcall -version=0x601+ RtlUnicodeToUTF8N(ptr long ptr wstr long) ChpeAutoRtlUnicodeToUTF8N
1270 stdcall -version=0xA00+ RtlUTF8StringToUnicodeString(ptr ptr long) ChpeAutoRtlUTF8StringToUnicodeString
1272 stdcall RtlUnicodeStringToAnsiString(ptr ptr long) ChpeAutoRtlUnicodeStringToAnsiString
1273 stdcall RtlUnicodeStringToCountedOemString(ptr ptr long) ChpeAutoRtlUnicodeStringToCountedOemString
1274 stdcall RtlUnicodeStringToInteger(ptr long ptr) ChpeAutoRtlUnicodeStringToInteger
1275 stdcall RtlUnicodeStringToOemSize(ptr) ChpeAutoRtlUnicodeStringToOemSize
1276 stdcall RtlUnicodeStringToOemString(ptr ptr long) ChpeAutoRtlUnicodeStringToOemString
1277 stdcall RtlUnicodeToCustomCPN(ptr ptr long ptr wstr long) ChpeAutoRtlUnicodeToCustomCPN
1278 stdcall RtlUnicodeToMultiByteN(ptr long ptr ptr long) ChpeAutoRtlUnicodeToMultiByteN
1279 stdcall RtlUnicodeToMultiByteSize(ptr ptr long) ChpeAutoRtlUnicodeToMultiByteSize
1280 stdcall RtlUnicodeToOemN(ptr long ptr ptr long) ChpeAutoRtlUnicodeToOemN
1281 stdcall RtlUniform(ptr) ChpeAutoRtlUniform
1282 stdcall RtlUnlockBootStatusData(ptr) ChpeAutoRtlUnlockBootStatusData
1283 stdcall -version=0x600+ RtlUnlockCurrentThread() ChpeStubRtlUnlockCurrentThread
1284 stdcall RtlUnlockHeap(long) ChpeAutoRtlUnlockHeap
1285 stdcall -version=0x600+ RtlUnlockMemoryBlockLookaside(long) ChpeStubRtlUnlockMemoryBlockLookaside
1286 stdcall RtlUnlockMemoryStreamRegion(ptr int64 int64 long) ChpeAutoRtlUnlockMemoryStreamRegion
1287 stdcall -version=0x600+ RtlUnlockMemoryZone(long) ChpeStubRtlUnlockMemoryZone
1288 stdcall -version=0x600+ RtlUnlockModuleSection(long) ChpeStubRtlUnlockModuleSection
1291 stdcall RtlUnregisterCfgTargetRange(ptr) ChpeAutoRtlUnregisterCfgTargetRange
1292 stdcall RtlUpcaseUnicodeChar(long) ChpeAutoRtlUpcaseUnicodeChar
1293 stdcall RtlUpcaseUnicodeString(ptr ptr long) ChpeAutoRtlUpcaseUnicodeString
1294 stdcall RtlUpcaseUnicodeStringToAnsiString(ptr ptr long) ChpeAutoRtlUpcaseUnicodeStringToAnsiString
1295 stdcall RtlUpcaseUnicodeStringToCountedOemString(ptr ptr long) ChpeAutoRtlUpcaseUnicodeStringToCountedOemString
1296 stdcall RtlUpcaseUnicodeStringToOemString(ptr ptr long) ChpeAutoRtlUpcaseUnicodeStringToOemString
1297 stdcall RtlUpcaseUnicodeToCustomCPN(ptr ptr long ptr wstr long) ChpeAutoRtlUpcaseUnicodeToCustomCPN
1298 stdcall RtlUpcaseUnicodeToMultiByteN(ptr long ptr ptr long) ChpeAutoRtlUpcaseUnicodeToMultiByteN
1299 stdcall RtlUpcaseUnicodeToOemN(ptr long ptr ptr long) ChpeAutoRtlUpcaseUnicodeToOemN
1300 stdcall -version=0x600+ RtlUpdateClonedCriticalSection(long) ChpeStubRtlUpdateClonedCriticalSection
1301 stdcall -version=0x600+ RtlUpdateClonedSRWLock(ptr long) ChpeStubRtlUpdateClonedSRWLock
1302 stdcall RtlUpdateTimer(ptr ptr long long) ChpeAutoRtlUpdateTimer
1303 stdcall RtlUpperChar(long) ChpeAutoRtlUpperChar
1304 stdcall RtlUpperString(ptr ptr) ChpeAutoRtlUpperString
1306 stdcall RtlValidAcl(ptr) ChpeAutoRtlValidAcl
1307 stdcall RtlValidRelativeSecurityDescriptor(ptr long long) ChpeAutoRtlValidRelativeSecurityDescriptor
1308 stdcall RtlValidSecurityDescriptor(ptr) ChpeAutoRtlValidSecurityDescriptor
1309 stdcall RtlValidSid(ptr) ChpeAutoRtlValidSid
1310 stdcall RtlValidateHeap(long long ptr) ChpeAutoRtlValidateHeap
1311 stdcall RtlValidateProcessHeaps() ChpeAutoRtlValidateProcessHeaps
1312 stdcall RtlValidateUnicodeString(long ptr) ChpeAutoRtlValidateUnicodeString
1313 stdcall RtlVerifyVersionInfo(ptr long double) ChpeAutoRtlVerifyVersionInfo
1315 stdcall RtlVirtualUnwind2(long int64 int64 ptr ptr ptr ptr ptr ptr ptr ptr ptr long) ChpeAutoRtlVirtualUnwind2
1321 stdcall RtlWalkFrameChain(ptr long long) ChpeAutoRtlWalkFrameChain
1322 stdcall RtlWeaklyEnumerateEntryHashTable(ptr ptr) ChpeAutoRtlWeaklyEnumerateEntryHashTable
1323 stdcall RtlWalkHeap(long ptr) ChpeAutoRtlWalkHeap
1324 stdcall -version=0x600+ RtlWerpReportException(long long ptr long long ptr) ChpeStubRtlWerpReportException
1325 stdcall -version=0x600+ RtlWow64CallFunction64() ChpeStubRtlWow64CallFunction64
1326 stdcall RtlWow64EnableFsRedirection(long) ChpeAutoRtlWow64EnableFsRedirection
1327 stdcall RtlWow64EnableFsRedirectionEx(long ptr) ChpeAutoRtlWow64EnableFsRedirectionEx
1328 stdcall -version=0x600+ RtlOpenCrossProcessEmulatorWorkConnection(ptr ptr ptr) ChpeAutoRtlOpenCrossProcessEmulatorWorkConnection
1329 stdcall -version=0x600+ RtlWow64GetCpuAreaInfo(ptr long ptr) ChpeAutoRtlWow64GetCpuAreaInfo
1330 stdcall -version=0x600+ RtlWow64GetCurrentMachine() ChpeAutoRtlWow64GetCurrentMachine
1331 stdcall -version=0x600+ RtlWow64GetProcessMachines(ptr ptr ptr) ChpeAutoRtlWow64GetProcessMachines
1332 stdcall -version=0x600+ RtlWow64GetSharedInfoProcess(ptr ptr ptr) ChpeAutoRtlWow64GetSharedInfoProcess
1333 stdcall -version=0x600+ RtlWow64GetThreadContext(ptr ptr) ChpeAutoRtlWow64GetThreadContext
1335 stdcall -version=0x600+ RtlWow64IsWowGuestMachineSupported(long ptr) ChpeAutoRtlWow64IsWowGuestMachineSupported
1336 stdcall -version=0x600+ RtlWow64LogMessageInEventLogger(long long long) ChpeStubRtlWow64LogMessageInEventLogger
1337 stdcall -version=0x600+ RtlWow64PopCrossProcessWorkFromFreeList(ptr) ChpeAutoRtlWow64PopCrossProcessWorkFromFreeList
1338 stdcall -version=0x600+ RtlWow64PushCrossProcessWorkOntoWorkList(ptr ptr ptr) ChpeAutoRtlWow64PushCrossProcessWorkOntoWorkList
1339 stdcall -version=0x600+ RtlWow64RequestCrossProcessHeavyFlush(ptr) ChpeAutoRtlWow64RequestCrossProcessHeavyFlush
1340 stdcall -version=0x600+ RtlWow64SetThreadContext(ptr ptr) ChpeAutoRtlWow64SetThreadContext
1341 stdcall -version=0x600+ RtlWow64SuspendThread(ptr ptr) ChpeAutoRtlWow64SuspendThread
1342 stdcall RtlWriteMemoryStream(ptr ptr long ptr) ChpeAutoRtlWriteMemoryStream
1343 stdcall RtlWriteRegistryValue(long ptr ptr long ptr long) ChpeAutoRtlWriteRegistryValue
1344 stdcall RtlZeroHeap(ptr long) ChpeAutoRtlZeroHeap
1346 stdcall RtlZombifyActivationContext(ptr) ChpeAutoRtlZombifyActivationContext
1348 stdcall -version=0x600+ RtlpCheckDynamicTimeZoneInformation(ptr long) ChpeStubRtlpCheckDynamicTimeZoneInformation
1349 stdcall -version=0x600+ RtlpCleanupRegistryKeys() ChpeStubRtlpCleanupRegistryKeys
1350 stdcall -version=0x600+ RtlpConvertCultureNamesToLCIDs(wstr ptr) ChpeStubRtlpConvertCultureNamesToLCIDs
1351 stdcall -version=0x600+ RtlpConvertLCIDsToCultureNames(wstr ptr) ChpeStubRtlpConvertLCIDsToCultureNames
1352 stdcall -version=0x600+ RtlpCreateProcessRegistryInfo(ptr) ChpeStubRtlpCreateProcessRegistryInfo
1353 stdcall RtlpEnsureBufferSize(long ptr long) ChpeAutoRtlpEnsureBufferSize
1354 stdcall -version=0x600+ RtlpGetLCIDFromLangInfoNode(long long ptr) ChpeStubRtlpGetLCIDFromLangInfoNode
1355 stdcall -version=0x600+ RtlpGetNameFromLangInfoNode(long long long) ChpeStubRtlpGetNameFromLangInfoNode
1356 stdcall -version=0x600+ RtlpGetSystemDefaultUILanguage(ptr long) ChpeStubRtlpGetSystemDefaultUILanguage
1357 stdcall -version=0x600+ RtlpGetUserOrMachineUILanguage4NLS(long long ptr) ChpeStubRtlpGetUserOrMachineUILanguage4NLS
1358 stdcall -version=0x600+ RtlpInitializeLangRegistryInfo(ptr) ChpeStubRtlpInitializeLangRegistryInfo
1359 stdcall -version=0x600+ RtlpIsQualifiedLanguage(long ptr long) ChpeStubRtlpIsQualifiedLanguage
1360 stdcall -version=0x600+ RtlpLoadMachineUIByPolicy(ptr long ptr) ChpeStubRtlpLoadMachineUIByPolicy
1361 stdcall -version=0x600+ RtlpLoadUserUIByPolicy(ptr long ptr) ChpeStubRtlpLoadUserUIByPolicy
1362 stdcall -version=0x600+ RtlpMuiFreeLangRegistryInfo(long) ChpeStubRtlpMuiFreeLangRegistryInfo
1363 stdcall -version=0x600+ RtlpMuiRegCreateRegistryInfo() ChpeStubRtlpMuiRegCreateRegistryInfo
1364 stdcall -version=0x600+ RtlpMuiRegFreeRegistryInfo(long long) ChpeStubRtlpMuiRegFreeRegistryInfo
1365 stdcall -version=0x600+ RtlpMuiRegLoadRegistryInfo(long long) ChpeStubRtlpMuiRegLoadRegistryInfo
1366 stdcall RtlpNotOwnerCriticalSection(ptr) ChpeAutoRtlpNotOwnerCriticalSection
1367 stdcall RtlpNtCreateKey(ptr long ptr long ptr ptr) ChpeAutoRtlpNtCreateKey
1368 stdcall RtlpNtEnumerateSubKey(ptr ptr long long) ChpeAutoRtlpNtEnumerateSubKey
1369 stdcall RtlpNtMakeTemporaryKey(ptr) ChpeAutoRtlpNtMakeTemporaryKey
1370 stdcall RtlpNtOpenKey(ptr long ptr long) ChpeAutoRtlpNtOpenKey
1371 stdcall RtlpNtQueryValueKey(ptr ptr ptr ptr long) ChpeAutoRtlpNtQueryValueKey
1372 stdcall RtlpNtSetValueKey(ptr long ptr long) ChpeAutoRtlpNtSetValueKey
1373 stdcall -version=0x600+ RtlpQueryDefaultUILanguage(ptr long) ChpeStubRtlpQueryDefaultUILanguage
1374 stdcall -version=0x600+ RtlpQueryProcessDebugInformationFromWow64(long ptr) ChpeStubRtlpQueryProcessDebugInformationFromWow64
1375 stdcall -version=0x600+ RtlpRefreshCachedUILanguage(wstr long) ChpeStubRtlpRefreshCachedUILanguage
1376 stdcall -version=0x600+ RtlpSetInstallLanguage(long ptr) ChpeStubRtlpSetInstallLanguage
1377 stdcall -version=0x600+ RtlpSetPreferredUILanguages(long ptr ptr) ChpeStubRtlpSetPreferredUILanguages
1378 stdcall RtlpUnWaitCriticalSection(ptr) ChpeAutoRtlpUnWaitCriticalSection
1379 stdcall -version=0x600+ RtlpVerifyAndCommitUILanguageSettings(long) ChpeStubRtlpVerifyAndCommitUILanguageSettings
1380 stdcall RtlpWaitForCriticalSection(ptr) ChpeAutoRtlpWaitForCriticalSection
1381 stdcall RtlxAnsiStringToUnicodeSize(ptr) ChpeAutoRtlxAnsiStringToUnicodeSize
1382 stdcall RtlxOemStringToUnicodeSize(ptr) ChpeAutoRtlxOemStringToUnicodeSize
1383 stdcall RtlxUnicodeStringToAnsiSize(ptr) ChpeAutoRtlxUnicodeStringToAnsiSize
1384 stdcall RtlxUnicodeStringToOemSize(ptr) ChpeAutoRtlxUnicodeStringToOemSize
1385 stdcall -version=0x600+ ShipAssert(long long) ChpeStubShipAssert
1386 stdcall -version=0x600+ ShipAssertGetBufferInfo(ptr ptr) ChpeStubShipAssertGetBufferInfo
1387 stdcall -version=0x600+ ShipAssertMsgA(long long) ChpeStubShipAssertMsgA
1388 stdcall -version=0x600+ ShipAssertMsgW(long long) ChpeStubShipAssertMsgW
1389 stdcall -version=0x600+ TpAllocAlpcCompletion(ptr ptr ptr ptr ptr) ChpeAutoTpAllocAlpcCompletion
1390 stdcall -version=0x600+ TpAllocAlpcCompletionEx(ptr ptr ptr ptr ptr) ChpeAutoTpAllocAlpcCompletionEx
1391 stdcall -version=0x600+ TpAllocCleanupGroup(ptr) ChpeAutoTpAllocCleanupGroup
1392 stdcall -version=0x600+ TpAllocIoCompletion(ptr ptr ptr ptr ptr) ChpeAutoTpAllocIoCompletion
1393 stdcall -version=0x600+ TpAllocPool(ptr ptr) ChpeAutoTpAllocPool
1394 stdcall -version=0x600+ TpAllocTimer(ptr ptr ptr ptr) ChpeAutoTpAllocTimer
1395 stdcall -version=0x600+ TpAllocWait(ptr ptr ptr ptr) ChpeAutoTpAllocWait
1396 stdcall -version=0x600+ TpAllocWork(ptr ptr ptr ptr) ChpeAutoTpAllocWork
1397 stdcall -version=0x600+ TpAlpcRegisterCompletionList(ptr) ChpeAutoTpAlpcRegisterCompletionList
1398 stdcall -version=0x600+ TpAlpcUnregisterCompletionList(ptr) ChpeAutoTpAlpcUnregisterCompletionList
1400 stdcall -version=0x600+ TpCallbackMayRunLong(ptr) ChpeAutoTpCallbackMayRunLong
1403 stdcall -version=0x600+ TpCallbackSendAlpcMessageOnCompletion(ptr ptr long ptr) ChpeAutoTpCallbackSendAlpcMessageOnCompletion
1404 stdcall -version=0x600+ TpCallbackSendPendingAlpcMessage(ptr) ChpeAutoTpCallbackSendPendingAlpcMessage
1408 stdcall -version=0x600+ TpCaptureCaller(long) ChpeStubTpCaptureCaller
1409 stdcall -version=0x600+ TpCheckTerminateWorker(ptr) ChpeStubTpCheckTerminateWorker
1410 stdcall -version=0x600+ TpDbgDumpHeapUsage(long ptr long) ChpeStubTpDbgDumpHeapUsage
1411 stdcall -version=0x600+ TpDbgSetLogRoutine() ChpeStubTpDbgSetLogRoutine
1415 stdcall -version=0x601+ TpQueryPoolStackInformation(ptr ptr) ChpeAutoTpQueryPoolStackInformation
1416 stdcall -version=0x600+ TpReleaseAlpcCompletion(ptr) ChpeAutoTpReleaseAlpcCompletion
1426 stdcall -version=0x601+ TpSetPoolStackInformation(ptr ptr) ChpeAutoTpSetPoolStackInformation
1431 stdcall -version=0x600+ TpSimpleTryPost(ptr ptr ptr) ChpeAutoTpSimpleTryPost
1433 stdcall -version=0x600+ TpWaitForAlpcCompletion(ptr) ChpeAutoTpWaitForAlpcCompletion
1439 stdcall -version=0x600+ WerCheckEventEscalation(long ptr) ChpeStubWerCheckEventEscalation
1440 stdcall -version=0x600+ WerReportSQMEvent(long long long) ChpeStubWerReportSQMEvent
1441 stdcall -version=0x600+ WerReportWatsonEvent(long long long long) ChpeStubWerReportWatsonEvent
1442 stdcall -version=0x600+ WinSqmAddToStream(ptr long long long) ChpeStubWinSqmAddToStream
1443 stdcall -version=0x600+ WinSqmAddToStreamEx(ptr long long ptr long) ChpeStubWinSqmAddToStreamEx
1444 stdcall -version=0x600+ WinSqmEndSession(ptr) ChpeStubWinSqmEndSession
1445 stdcall -version=0x600+ WinSqmEventEnabled(long ptr) ChpeAutoWinSqmEventEnabled
1446 stdcall -version=0x600+ WinSqmEventWrite(long long long) ChpeStubWinSqmEventWrite
1447 stdcall -version=0x600+ WinSqmIncrementDWORD(long long long) ChpeAutoWinSqmIncrementDWORD
1448 stdcall -version=0x600+ WinSqmIsOptedIn() ChpeAutoWinSqmIsOptedIn
1449 stdcall -version=0x600+ WinSqmSetDWORD(ptr long long) ChpeAutoWinSqmSetDWORD
1450 stdcall -version=0x600+ WinSqmSetDWORD64(ptr long int64) ChpeAutoWinSqmSetDWORD64
1451 stdcall -version=0x600+ WinSqmSetString(ptr long ptr) ChpeStubWinSqmSetString
1452 stdcall -version=0x600+ WinSqmStartSession(ptr) ChpeStubWinSqmStartSession
1453 stdcall ZwAcceptConnectPort(ptr long ptr long long ptr) ChpeAutoZwAcceptConnectPort
1454 stdcall ZwAccessCheck(ptr long long ptr ptr ptr ptr ptr) ChpeAutoZwAccessCheck
1455 stdcall ZwAccessCheckAndAuditAlarm(ptr long ptr ptr ptr long ptr long ptr ptr ptr) ChpeAutoZwAccessCheckAndAuditAlarm
1456 stdcall ZwAccessCheckByType(ptr ptr ptr long ptr long ptr ptr long ptr ptr) ChpeAutoZwAccessCheckByType
1457 stdcall ZwAccessCheckByTypeAndAuditAlarm(ptr ptr ptr ptr ptr ptr long long long ptr long ptr long ptr ptr ptr) ChpeAutoZwAccessCheckByTypeAndAuditAlarm
1458 stdcall ZwAccessCheckByTypeResultList(ptr ptr ptr long ptr long ptr ptr long ptr ptr) ChpeAutoZwAccessCheckByTypeResultList
1459 stdcall ZwAccessCheckByTypeResultListAndAuditAlarm(ptr ptr ptr ptr ptr ptr long long long ptr long ptr long ptr ptr ptr) ChpeAutoZwAccessCheckByTypeResultListAndAuditAlarm
1460 stdcall ZwAccessCheckByTypeResultListAndAuditAlarmByHandle(ptr ptr ptr ptr ptr ptr ptr long long long ptr long ptr long ptr ptr ptr) ChpeAutoZwAccessCheckByTypeResultListAndAuditAlarmByHandle
1461 stub -version=0x600+ ZwAcquireCMFViewOwnership
1462 stdcall ZwAddAtom(ptr long ptr) ChpeAutoZwAddAtom
1463 stdcall ZwAddBootEntry(ptr long) ChpeAutoZwAddBootEntry
1464 stdcall ZwAddDriverEntry(ptr long) ChpeAutoZwAddDriverEntry
1465 stdcall ZwAdjustGroupsToken(long long long long long long) ChpeAutoZwAdjustGroupsToken
1466 stdcall ZwAdjustPrivilegesToken(long long long long long long) ChpeAutoZwAdjustPrivilegesToken
1467 stdcall ZwAlertResumeThread(long ptr) ChpeAutoZwAlertResumeThread
1468 stdcall ZwAlertThread(long) ChpeAutoZwAlertThread
1469 stdcall ZwAlertThreadByThreadId(long) ChpeAutoZwAlertThreadByThreadId
1470 stdcall ZwAllocateLocallyUniqueId(ptr) ChpeAutoZwAllocateLocallyUniqueId
1471 stdcall ZwAllocateUserPhysicalPages(ptr ptr ptr) ChpeAutoZwAllocateUserPhysicalPages
1472 stdcall ZwAllocateUuids(ptr ptr ptr ptr) ChpeAutoZwAllocateUuids
1473 stdcall ZwAllocateVirtualMemory(long ptr ptr ptr long long) ChpeAutoZwAllocateVirtualMemory
1474 stdcall ZwAllocateVirtualMemoryEx(long ptr ptr long long ptr long) ChpeAutoZwAllocateVirtualMemoryEx
1475 stdcall -version=0x600+ ZwAlpcAcceptConnectPort(ptr ptr long ptr ptr ptr ptr ptr long) ChpeAutoZwAlpcAcceptConnectPort
1476 stdcall -version=0x600+ ZwAlpcCancelMessage(ptr long ptr) ChpeAutoZwAlpcCancelMessage
1477 stdcall -version=0x600+ ZwAlpcConnectPort(ptr ptr ptr ptr long ptr ptr ptr ptr ptr ptr) ChpeAutoZwAlpcConnectPort
1478 stdcall -version=0x602+ ZwAlpcConnectPortEx(ptr ptr ptr ptr long ptr ptr ptr ptr ptr ptr) ChpeAutoZwAlpcConnectPortEx
1479 stdcall -version=0x600+ ZwAlpcCreatePort(ptr ptr ptr) ChpeAutoZwAlpcCreatePort
1480 stdcall -version=0x600+ ZwAlpcCreatePortSection(ptr long ptr long ptr ptr) ChpeAutoZwAlpcCreatePortSection
1481 stdcall -version=0x600+ ZwAlpcCreateResourceReserve(ptr long long ptr) ChpeAutoZwAlpcCreateResourceReserve
1482 stdcall -version=0x600+ ZwAlpcCreateSectionView(ptr long ptr) ChpeAutoZwAlpcCreateSectionView
1483 stdcall -version=0x600+ ZwAlpcCreateSecurityContext(ptr long ptr) ChpeAutoZwAlpcCreateSecurityContext
1484 stdcall -version=0x600+ ZwAlpcDeletePortSection(ptr long ptr) ChpeAutoZwAlpcDeletePortSection
1485 stdcall -version=0x600+ ZwAlpcDeleteResourceReserve(ptr long long) ChpeAutoZwAlpcDeleteResourceReserve
1486 stdcall -version=0x600+ ZwAlpcDeleteSectionView(ptr long ptr) ChpeAutoZwAlpcDeleteSectionView
1487 stdcall -version=0x600+ ZwAlpcDeleteSecurityContext(ptr long ptr) ChpeAutoZwAlpcDeleteSecurityContext
1488 stdcall -version=0x600+ ZwAlpcDisconnectPort(ptr long) ChpeAutoZwAlpcDisconnectPort
1489 stdcall -version=0x600+ ZwAlpcImpersonateClientOfPort(ptr ptr ptr) ChpeAutoZwAlpcImpersonateClientOfPort
1490 stdcall -version=0xA00+ ZwAlpcImpersonateClientContainerOfPort(ptr ptr long) ChpeAutoZwAlpcImpersonateClientContainerOfPort
1491 stdcall -version=0x600+ ZwAlpcOpenSenderProcess(ptr ptr ptr long long ptr) ChpeAutoZwAlpcOpenSenderProcess
1492 stdcall -version=0x600+ ZwAlpcOpenSenderThread(ptr ptr ptr long long ptr) ChpeAutoZwAlpcOpenSenderThread
1493 stdcall -version=0x600+ ZwAlpcQueryInformation(ptr long ptr long ptr) ChpeAutoZwAlpcQueryInformation
1494 stdcall -version=0x600+ ZwAlpcQueryInformationMessage(ptr ptr long ptr long ptr) ChpeAutoZwAlpcQueryInformationMessage
1495 stdcall -version=0x600+ ZwAlpcRevokeSecurityContext(ptr long ptr) ChpeAutoZwAlpcRevokeSecurityContext
1496 stdcall -version=0x600+ ZwAlpcSendWaitReceivePort(ptr long ptr ptr ptr ptr ptr ptr) ChpeAutoZwAlpcSendWaitReceivePort
1497 stdcall -version=0x600+ ZwAlpcSetInformation(ptr long ptr long) ChpeAutoZwAlpcSetInformation
1498 stdcall ZwApphelpCacheControl(long ptr) ChpeAutoZwApphelpCacheControl
1499 stdcall ZwAreMappedFilesTheSame(ptr ptr) ChpeAutoZwAreMappedFilesTheSame
1500 stdcall ZwAssignProcessToJobObject(long long) ChpeAutoZwAssignProcessToJobObject
1501 stdcall ZwCallbackReturn(ptr long long) ChpeAutoZwCallbackReturn
1502 stdcall ZwCancelDeviceWakeupRequest(ptr) ChpeAutoZwCancelDeviceWakeupRequest
1503 stdcall ZwCancelIoFile(long ptr) ChpeAutoZwCancelIoFile
1504 stdcall -version=0x600+ ZwCancelIoFileEx(ptr ptr ptr) ChpeAutoZwCancelIoFileEx
1505 stdcall -version=0x600+ ZwCancelSynchronousIoFile(ptr ptr ptr) ChpeAutoZwCancelSynchronousIoFile
1506 stdcall ZwCancelTimer(long ptr) ChpeAutoZwCancelTimer
1507 stdcall ZwClearEvent(long) ChpeAutoZwClearEvent
1508 stdcall ZwClose(long) ChpeAutoZwClose
1509 stdcall ZwCloseObjectAuditAlarm(ptr ptr long) ChpeAutoZwCloseObjectAuditAlarm
1510 stdcall -version=0x600+ ZwCommitComplete(ptr ptr) ChpeAutoZwCommitComplete
1511 stdcall -version=0x600+ ZwCommitEnlistment(ptr ptr) ChpeAutoZwCommitEnlistment
1512 stdcall -version=0x600+ ZwCommitTransaction(ptr long) ChpeAutoZwCommitTransaction
1513 stdcall ZwCompactKeys(long ptr) ChpeAutoZwCompactKeys
1514 stdcall ZwCompareTokens(ptr ptr ptr) ChpeAutoZwCompareTokens
1515 stdcall ZwCompleteConnectPort(ptr) ChpeAutoZwCompleteConnectPort
1516 stdcall ZwCompressKey(ptr) ChpeAutoZwCompressKey
1517 stdcall ZwConnectPort(ptr ptr ptr ptr ptr ptr ptr ptr) ChpeAutoZwConnectPort
1518 stdcall ZwContinue(ptr long) ChpeAutoZwContinue
1519 stdcall ZwCreateDebugObject(ptr long ptr long) ChpeAutoZwCreateDebugObject
1520 stdcall ZwCreateDirectoryObject(long long long) ChpeAutoZwCreateDirectoryObject
1521 stdcall -version=0x600+ ZwCreateEnlistment(ptr long ptr ptr ptr long long ptr) ChpeAutoZwCreateEnlistment
1522 stdcall ZwCreateEvent(long long long long long) ChpeAutoZwCreateEvent
1523 stdcall ZwCreateEventPair(ptr long ptr) ChpeAutoZwCreateEventPair
1524 stdcall ZwCreateFile(ptr long ptr ptr long long long ptr long long ptr) ChpeAutoZwCreateFile
1525 stdcall ZwCreateIoCompletion(ptr long ptr long) ChpeAutoZwCreateIoCompletion
1526 stdcall -version=0x602+ ZwCreateWaitCompletionPacket(ptr long ptr) ChpeAutoZwCreateWaitCompletionPacket
1527 stdcall -version=0x602+ ZwAssociateWaitCompletionPacket(ptr ptr ptr ptr ptr long ptr ptr) ChpeAutoZwAssociateWaitCompletionPacket
1528 stdcall -version=0x602+ ZwCancelWaitCompletionPacket(ptr long) ChpeAutoZwCancelWaitCompletionPacket
1529 stdcall ZwCreateJobObject(ptr long ptr) ChpeAutoZwCreateJobObject
1530 stdcall ZwCreateJobSet(long ptr long) ChpeAutoZwCreateJobSet
1531 stdcall ZwCreateKey(ptr long ptr long ptr long long) ChpeAutoZwCreateKey
1532 stdcall -version=0x600+ ZwCreateKeyTransacted(ptr long ptr long ptr long ptr ptr) ChpeAutoZwCreateKeyTransacted
1533 stdcall ZwCreateKeyedEvent(ptr long ptr long) ChpeAutoZwCreateKeyedEvent
1534 stdcall ZwCreateMailslotFile(long long long long long long long long) ChpeAutoZwCreateMailslotFile
1535 stdcall ZwCreateMutant(ptr long ptr long) ChpeAutoZwCreateMutant
1536 stdcall ZwCreateNamedPipeFile(ptr long ptr ptr long long long long long long long long long ptr) ChpeAutoZwCreateNamedPipeFile
1537 stdcall ZwCreatePagingFile(ptr ptr ptr long) ChpeAutoZwCreatePagingFile
1538 stdcall ZwCreatePort(ptr ptr long long long) ChpeAutoZwCreatePort
1539 stdcall ZwCreateProcess(ptr long ptr ptr long ptr ptr ptr) ChpeAutoZwCreateProcess
1540 stdcall ZwCreateProcessEx(ptr long ptr ptr long ptr ptr ptr long) ChpeAutoZwCreateProcessEx
1541 stdcall ZwCreateProfile(ptr ptr ptr long long ptr long long long) ChpeAutoZwCreateProfile
1542 stdcall -version=0x600+ ZwCreateResourceManager(ptr long ptr ptr ptr long ptr) ChpeAutoZwCreateResourceManager
1543 stdcall ZwCreateSection(ptr long ptr ptr long long long) ChpeAutoZwCreateSection
1544 stdcall ZwCreateSemaphore(ptr long ptr long long) ChpeAutoZwCreateSemaphore
1545 stdcall ZwCreateSymbolicLinkObject(ptr long ptr ptr) ChpeAutoZwCreateSymbolicLinkObject
1546 stdcall ZwCreateThread(ptr long ptr ptr ptr ptr ptr long) ChpeAutoZwCreateThread
1547 stdcall -version=0x600+ ZwCreateThreadEx(ptr long ptr ptr ptr ptr long long long long ptr) ChpeAutoZwCreateThreadEx
1548 stdcall ZwCreateTimer(ptr long ptr long) ChpeAutoZwCreateTimer
1549 stdcall ZwCreateToken(ptr long ptr long ptr ptr ptr ptr ptr ptr ptr ptr ptr) ChpeAutoZwCreateToken
1550 stdcall -version=0x600+ ZwCreateTransaction(ptr long ptr ptr ptr long long long ptr ptr) ChpeAutoZwCreateTransaction
1551 stdcall -version=0x600+ ZwCreateTransactionManager(ptr long ptr ptr long long) ChpeAutoZwCreateTransactionManager
1552 stdcall -version=0x600+ ZwCreateUserProcess(ptr ptr long long ptr ptr long long ptr ptr ptr) ChpeAutoZwCreateUserProcess
1553 stdcall ZwCreateWaitablePort(ptr ptr long long long) ChpeAutoZwCreateWaitablePort
1554 stdcall -version=0x602+ ZwCreateWnfStateName(ptr long long long ptr long ptr) ChpeAutoZwCreateWnfStateName
1555 stdcall -version=0x600+ ZwCreateWorkerFactory(ptr long ptr ptr ptr ptr ptr long long long) ChpeStubZwCreateWorkerFactory
1556 stdcall ZwDebugActiveProcess(ptr ptr) ChpeAutoZwDebugActiveProcess
1557 stdcall ZwDebugContinue(ptr ptr long) ChpeAutoZwDebugContinue
1558 stdcall ZwDelayExecution(long ptr) ChpeAutoZwDelayExecution
1559 stdcall ZwDeleteAtom(long) ChpeAutoZwDeleteAtom
1560 stdcall ZwDeleteBootEntry(long) ChpeAutoZwDeleteBootEntry
1561 stdcall ZwDeleteDriverEntry(long) ChpeAutoZwDeleteDriverEntry
1562 stdcall ZwDeleteFile(ptr) ChpeAutoZwDeleteFile
1563 stdcall ZwDeleteKey(long) ChpeAutoZwDeleteKey
1564 stdcall ZwDeleteObjectAuditAlarm(ptr ptr long) ChpeAutoZwDeleteObjectAuditAlarm
1565 stdcall -version=0x600+ ZwDeletePrivateNamespace(ptr) ChpeAutoZwDeletePrivateNamespace
1566 stdcall ZwDeleteValueKey(long ptr) ChpeAutoZwDeleteValueKey
1567 stdcall -version=0x602+ ZwDeleteWnfStateData(ptr ptr) ChpeAutoZwDeleteWnfStateData
1568 stdcall -version=0x602+ ZwDeleteWnfStateName(ptr) ChpeAutoZwDeleteWnfStateName
1569 stdcall ZwDeviceIoControlFile(long long long long long long long long long long) ChpeAutoZwDeviceIoControlFile
1570 stdcall ZwDisplayString(ptr) ChpeAutoZwDisplayString
1571 stdcall ZwDuplicateObject(long long long ptr long long long) ChpeAutoZwDuplicateObject
1572 stdcall ZwDuplicateToken(long long long long long long) ChpeAutoZwDuplicateToken
1573 stdcall ZwEnumerateBootEntries(ptr ptr) ChpeAutoZwEnumerateBootEntries
1574 stdcall ZwEnumerateDriverEntries(ptr ptr) ChpeAutoZwEnumerateDriverEntries
1575 stdcall ZwEnumerateKey(long long long ptr long ptr) ChpeAutoZwEnumerateKey
1576 stdcall ZwEnumerateSystemEnvironmentValuesEx(long ptr long) ChpeAutoZwEnumerateSystemEnvironmentValuesEx
1577 stdcall -version=0x600+ ZwEnumerateTransactionObject(ptr long ptr long ptr) ChpeAutoZwEnumerateTransactionObject
1578 stdcall ZwEnumerateValueKey(long long long ptr long ptr) ChpeAutoZwEnumerateValueKey
1579 stdcall ZwExtendSection(ptr ptr) ChpeAutoZwExtendSection
1580 stdcall ZwFilterToken(ptr long ptr ptr ptr ptr) ChpeAutoZwFilterToken
1581 stdcall -version=0x602+ ZwCreateLowBoxToken(ptr ptr long ptr ptr long ptr long ptr) ChpeAutoZwCreateLowBoxToken
1582 stdcall -version=0x600+ ZwCreatePrivateNamespace(ptr long ptr ptr) ChpeAutoZwCreatePrivateNamespace
1583 stdcall ZwFindAtom(ptr long ptr) ChpeAutoZwFindAtom
1584 stdcall ZwFlushBuffersFile(long ptr) ChpeAutoZwFlushBuffersFile
1585 stdcall ZwFlushBuffersFileEx(long long ptr long ptr) ChpeAutoZwFlushBuffersFileEx
1586 stdcall -version=0x600+ ZwFlushInstallUILanguage(long long) ChpeStubZwFlushInstallUILanguage
1587 stdcall ZwFlushInstructionCache(long ptr long) ChpeAutoZwFlushInstructionCache
1588 stdcall ZwFlushKey(long) ChpeAutoZwFlushKey
1589 stdcall -version=0x600+ ZwFlushProcessWriteBuffers() ChpeAutoZwFlushProcessWriteBuffers
1590 stdcall ZwFlushVirtualMemory(ptr ptr ptr ptr) ChpeAutoZwFlushVirtualMemory
1591 stdcall ZwFlushWriteBuffer() ChpeAutoZwFlushWriteBuffer
1592 stdcall ZwFreeUserPhysicalPages(ptr ptr ptr) ChpeAutoZwFreeUserPhysicalPages
1593 stdcall ZwFreeVirtualMemory(long ptr ptr long) ChpeAutoZwFreeVirtualMemory
1594 stdcall -version=0x600+ ZwFreezeRegistry(long) ChpeStubZwFreezeRegistry
1595 stdcall -version=0x600+ ZwFreezeTransactions(ptr ptr) ChpeStubZwFreezeTransactions
1596 stdcall ZwFsControlFile(long long long long long long long long long long) ChpeAutoZwFsControlFile
1597 stdcall ZwGetContextThread(long ptr) ChpeAutoZwGetContextThread
1598 stdcall ZwGetCurrentProcessorNumber() ChpeAutoZwGetCurrentProcessorNumber
1599 stdcall -version=0xA00+ ZwGetCurrentProcessorNumberEx(ptr) ChpeAutoZwGetCurrentProcessorNumberEx
1600 stdcall ZwGetDevicePowerState(ptr ptr) ChpeAutoZwGetDevicePowerState
1601 stdcall -version=0x600+ ZwGetMUIRegistryInfo(long ptr ptr) ChpeStubZwGetMUIRegistryInfo
1602 stdcall -version=0x600+ ZwGetNextProcess(ptr long long long ptr) ChpeStubZwGetNextProcess
1603 stdcall -version=0x600+ ZwGetNextThread(ptr ptr long long long ptr) ChpeAutoZwGetNextThread
1604 stdcall -version=0x600+ ZwGetNlsSectionPtr(long long ptr ptr ptr) ChpeAutoZwGetNlsSectionPtr
1605 stdcall -version=0x600+ ZwGetNotificationResourceManager(ptr ptr long ptr ptr long ptr) ChpeAutoZwGetNotificationResourceManager
1606 stdcall ZwGetPlugPlayEvent(long long ptr long) ChpeAutoZwGetPlugPlayEvent
1607 stdcall ZwGetWriteWatch(long long ptr long ptr ptr ptr) ChpeAutoZwGetWriteWatch
1608 stdcall ZwImpersonateAnonymousToken(ptr) ChpeAutoZwImpersonateAnonymousToken
1609 stdcall ZwImpersonateClientOfPort(ptr ptr) ChpeAutoZwImpersonateClientOfPort
1610 stdcall ZwImpersonateThread(ptr ptr ptr) ChpeAutoZwImpersonateThread
1611 stdcall -version=0x600+ ZwInitializeNlsFiles(ptr ptr ptr ptr) ChpeStubZwInitializeNlsFiles
1612 stdcall ZwInitializeRegistry(long) ChpeAutoZwInitializeRegistry
1613 stdcall ZwInitiatePowerAction(long long long long) ChpeAutoZwInitiatePowerAction
1614 stdcall ZwIsProcessInJob(long long) ChpeAutoZwIsProcessInJob
1615 stdcall ZwIsSystemResumeAutomatic() ChpeAutoZwIsSystemResumeAutomatic
1616 stdcall -version=0x600+ ZwIsUILanguageComitted() ChpeStubZwIsUILanguageComitted
1617 stdcall ZwListenPort(ptr ptr) ChpeAutoZwListenPort
1618 stdcall ZwLoadDriver(ptr) ChpeAutoZwLoadDriver
1619 stdcall ZwLoadKey2(ptr ptr long) ChpeAutoZwLoadKey2
1620 stdcall ZwLoadKey(ptr ptr) ChpeAutoZwLoadKey
1621 stdcall ZwLoadKeyEx(ptr ptr long ptr ptr long ptr ptr) ChpeAutoZwLoadKeyEx
1622 stdcall ZwLockFile(long long ptr ptr ptr ptr ptr ptr long long) ChpeAutoZwLockFile
1623 stdcall ZwLockProductActivationKeys(ptr ptr) ChpeAutoZwLockProductActivationKeys
1624 stdcall ZwLockRegistryKey(ptr) ChpeAutoZwLockRegistryKey
1625 stdcall ZwLockVirtualMemory(long ptr ptr long) ChpeAutoZwLockVirtualMemory
1626 stdcall ZwMakePermanentObject(ptr) ChpeAutoZwMakePermanentObject
1627 stdcall ZwMakeTemporaryObject(long) ChpeAutoZwMakeTemporaryObject
1628 stdcall -version=0x600+ ZwMapCMFModule(long long ptr ptr ptr) ChpeStubZwMapCMFModule
1629 stdcall ZwMapUserPhysicalPages(ptr ptr ptr) ChpeAutoZwMapUserPhysicalPages
1630 stdcall ZwMapUserPhysicalPagesScatter(ptr ptr ptr) ChpeAutoZwMapUserPhysicalPagesScatter
1631 stdcall ZwMapViewOfSection(long long ptr long long ptr ptr long long long) ChpeAutoZwMapViewOfSection
1632 stdcall ZwModifyBootEntry(ptr) ChpeAutoZwModifyBootEntry
1633 stdcall ZwModifyDriverEntry(ptr) ChpeAutoZwModifyDriverEntry
1634 stdcall ZwNotifyChangeDirectoryFile(long long ptr ptr ptr ptr long long long) ChpeAutoZwNotifyChangeDirectoryFile
1635 stdcall ZwNotifyChangeDirectoryFileEx(long long ptr ptr ptr ptr long long long long) ChpeAutoZwNotifyChangeDirectoryFileEx
1636 stdcall ZwNotifyChangeKey(long long ptr ptr ptr long long ptr long long) ChpeAutoZwNotifyChangeKey
1637 stdcall ZwNotifyChangeMultipleKeys(ptr long ptr ptr ptr ptr ptr long long ptr long long) ChpeAutoZwNotifyChangeMultipleKeys
1638 stdcall ZwOpenDirectoryObject(long long long) ChpeAutoZwOpenDirectoryObject
1639 stdcall -version=0x600+ ZwOpenEnlistment(ptr long ptr ptr ptr) ChpeAutoZwOpenEnlistment
1640 stdcall ZwOpenEvent(long long long) ChpeAutoZwOpenEvent
1641 stdcall ZwOpenEventPair(ptr long ptr) ChpeAutoZwOpenEventPair
1642 stdcall ZwOpenFile(ptr long ptr ptr long long) ChpeAutoZwOpenFile
1643 stdcall ZwOpenIoCompletion(ptr long ptr) ChpeAutoZwOpenIoCompletion
1644 stdcall ZwOpenJobObject(ptr long ptr) ChpeAutoZwOpenJobObject
1645 stdcall ZwOpenKey(ptr long ptr) ChpeAutoZwOpenKey
1646 stdcall ZwOpenKeyEx(ptr long ptr long) ChpeAutoZwOpenKeyEx
1647 stdcall -version=0x600+ ZwOpenKeyTransacted(ptr long ptr ptr) ChpeAutoZwOpenKeyTransacted
1648 stdcall ZwOpenKeyedEvent(ptr long ptr) ChpeAutoZwOpenKeyedEvent
1649 stdcall ZwOpenMutant(ptr long ptr) ChpeAutoZwOpenMutant
1650 stdcall ZwOpenObjectAuditAlarm(ptr ptr ptr ptr ptr ptr long long ptr long long ptr) ChpeAutoZwOpenObjectAuditAlarm
1651 stdcall -version=0x600+ ZwOpenPrivateNamespace(ptr long ptr ptr) ChpeAutoZwOpenPrivateNamespace
1652 stdcall ZwOpenProcess(ptr long ptr ptr) ChpeAutoZwOpenProcess
1653 stdcall ZwOpenProcessToken(long long ptr) ChpeAutoZwOpenProcessToken
1654 stdcall ZwOpenProcessTokenEx(long long long ptr) ChpeAutoZwOpenProcessTokenEx
1655 stdcall -version=0x600+ ZwOpenResourceManager(ptr long ptr ptr ptr) ChpeAutoZwOpenResourceManager
1656 stdcall ZwOpenSection(ptr long ptr) ChpeAutoZwOpenSection
1657 stdcall ZwOpenSemaphore(long long ptr) ChpeAutoZwOpenSemaphore
1658 stdcall -version=0x600+ ZwOpenSession(ptr long ptr) ChpeStubZwOpenSession
1659 stdcall ZwOpenSymbolicLinkObject(ptr long ptr) ChpeAutoZwOpenSymbolicLinkObject
1660 stdcall ZwOpenThread(ptr long ptr ptr) ChpeAutoZwOpenThread
1661 stdcall ZwOpenThreadToken(long long long ptr) ChpeAutoZwOpenThreadToken
1662 stdcall ZwOpenThreadTokenEx(long long long long ptr) ChpeAutoZwOpenThreadTokenEx
1663 stdcall ZwOpenTimer(ptr long ptr) ChpeAutoZwOpenTimer
1664 stdcall -version=0x600+ ZwOpenTransaction(ptr long ptr ptr ptr) ChpeAutoZwOpenTransaction
1665 stdcall -version=0x600+ ZwOpenTransactionManager(ptr long ptr ptr ptr long) ChpeAutoZwOpenTransactionManager
1666 stdcall ZwPlugPlayControl(ptr ptr long) ChpeAutoZwPlugPlayControl
1667 stdcall ZwPowerInformation(long ptr long ptr long) ChpeAutoZwPowerInformation
1668 stdcall -version=0x600+ ZwPrePrepareComplete(ptr ptr) ChpeAutoZwPrePrepareComplete
1669 stdcall -version=0x600+ ZwPrePrepareEnlistment(ptr ptr) ChpeAutoZwPrePrepareEnlistment
1670 stdcall -version=0x600+ ZwPrepareComplete(ptr ptr) ChpeAutoZwPrepareComplete
1671 stdcall -version=0x600+ ZwPrepareEnlistment(ptr ptr) ChpeAutoZwPrepareEnlistment
1672 stdcall ZwPrivilegeCheck(ptr ptr ptr) ChpeAutoZwPrivilegeCheck
1673 stdcall ZwPrivilegeObjectAuditAlarm(ptr ptr ptr long ptr long) ChpeAutoZwPrivilegeObjectAuditAlarm
1674 stdcall ZwPrivilegedServiceAuditAlarm(ptr ptr ptr ptr long) ChpeAutoZwPrivilegedServiceAuditAlarm
1675 stdcall -version=0x600+ ZwPropagationComplete(ptr long long ptr) ChpeAutoZwPropagationComplete
1676 stdcall -version=0x600+ ZwPropagationFailed(ptr long long) ChpeAutoZwPropagationFailed
1677 stdcall ZwProtectVirtualMemory(long ptr ptr long ptr) ChpeAutoZwProtectVirtualMemory
1678 stdcall ZwPulseEvent(long ptr) ChpeAutoZwPulseEvent
1679 stdcall ZwQueryAttributesFile(ptr ptr) ChpeAutoZwQueryAttributesFile
1680 stdcall ZwQueryBootEntryOrder(ptr ptr) ChpeAutoZwQueryBootEntryOrder
1681 stdcall ZwQueryBootOptions(ptr ptr) ChpeAutoZwQueryBootOptions
1682 stdcall ZwQueryDebugFilterState(long long) ChpeAutoZwQueryDebugFilterState
1683 stdcall ZwQueryDefaultLocale(long ptr) ChpeAutoZwQueryDefaultLocale
1684 stdcall ZwQueryDefaultUILanguage(ptr) ChpeAutoZwQueryDefaultUILanguage
1685 stdcall ZwQueryDirectoryFile(long long ptr ptr ptr ptr long long long ptr long) ChpeAutoZwQueryDirectoryFile
1686 stdcall ZwQueryDirectoryFileEx(long long ptr ptr ptr ptr long long long ptr) ChpeAutoZwQueryDirectoryFileEx
1687 stdcall ZwQueryDirectoryObject(long ptr long long long ptr ptr) ChpeAutoZwQueryDirectoryObject
1688 stdcall ZwQueryDriverEntryOrder(ptr ptr) ChpeAutoZwQueryDriverEntryOrder
1689 stdcall ZwQueryEaFile(long ptr ptr long long ptr long ptr long) ChpeAutoZwQueryEaFile
1690 stdcall ZwQueryEvent(long long ptr long ptr) ChpeAutoZwQueryEvent
1691 stdcall ZwQueryFullAttributesFile(ptr ptr) ChpeAutoZwQueryFullAttributesFile
1692 stdcall ZwQueryInformationAtom(long long ptr long ptr) ChpeAutoZwQueryInformationAtom
1693 stdcall ZwQueryInformationByName(ptr ptr ptr long long) ChpeAutoZwQueryInformationByName
1694 stdcall -version=0x600+ ZwQueryInformationEnlistment(ptr long ptr long ptr) ChpeAutoZwQueryInformationEnlistment
1695 stdcall ZwQueryInformationFile(long ptr ptr long long) ChpeAutoZwQueryInformationFile
1696 stdcall ZwQueryInformationJobObject(long long ptr long ptr) ChpeAutoZwQueryInformationJobObject
1697 stdcall ZwQueryInformationPort(ptr long ptr long ptr) ChpeAutoZwQueryInformationPort
1698 stdcall ZwQueryInformationProcess(long long ptr long ptr) ChpeAutoZwQueryInformationProcess
1699 stdcall -version=0x600+ ZwQueryInformationResourceManager(ptr long ptr long ptr) ChpeAutoZwQueryInformationResourceManager
1700 stdcall ZwQueryInformationThread(long long ptr long ptr) ChpeAutoZwQueryInformationThread
1701 stdcall ZwQueryInformationToken(long long ptr long ptr) ChpeAutoZwQueryInformationToken
1702 stdcall -version=0x600+ ZwQueryInformationTransaction(ptr long ptr long ptr) ChpeAutoZwQueryInformationTransaction
1703 stdcall -version=0x600+ ZwQueryInformationTransactionManager(ptr long ptr long ptr) ChpeAutoZwQueryInformationTransactionManager
1704 stdcall -version=0x600+ ZwQueryInformationWorkerFactory(ptr long ptr long ptr) ChpeStubZwQueryInformationWorkerFactory
1705 stdcall ZwQueryInstallUILanguage(ptr) ChpeAutoZwQueryInstallUILanguage
1706 stdcall ZwQueryIntervalProfile(long ptr) ChpeAutoZwQueryIntervalProfile
1707 stdcall ZwQueryIoCompletion(long long ptr long ptr) ChpeAutoZwQueryIoCompletion
1708 stdcall ZwQueryKey(long long ptr long ptr) ChpeAutoZwQueryKey
1709 stdcall -version=0x600+ ZwQueryLicenseValue(ptr ptr ptr long ptr) ChpeAutoZwQueryLicenseValue
1710 stdcall ZwQueryMultipleValueKey(long ptr long ptr long ptr) ChpeAutoZwQueryMultipleValueKey
1711 stdcall ZwQueryMutant(long long ptr long ptr) ChpeAutoZwQueryMutant
1712 stdcall ZwQueryObject(long long long long long) ChpeAutoZwQueryObject
1713 stdcall ZwQueryOpenSubKeys(ptr ptr) ChpeAutoZwQueryOpenSubKeys
1714 stdcall ZwQueryOpenSubKeysEx(ptr long ptr ptr) ChpeAutoZwQueryOpenSubKeysEx
1715 stdcall ZwQueryPerformanceCounter(long long) ChpeAutoZwQueryPerformanceCounter
1716 stdcall ZwQueryPortInformationProcess() ChpeAutoZwQueryPortInformationProcess
1717 stdcall ZwQueryQuotaInformationFile(ptr ptr ptr long long ptr long ptr long) ChpeAutoZwQueryQuotaInformationFile
1718 stdcall ZwQuerySection(long long long long long) ChpeAutoZwQuerySection
1719 stdcall ZwQuerySecurityObject(long long long long long) ChpeAutoZwQuerySecurityObject
1720 stdcall ZwQuerySemaphore(long long long long long) ChpeAutoZwQuerySemaphore
1721 stdcall ZwQuerySymbolicLinkObject(long ptr ptr) ChpeAutoZwQuerySymbolicLinkObject
1722 stdcall ZwQuerySystemEnvironmentValue(ptr ptr long ptr) ChpeAutoZwQuerySystemEnvironmentValue
1723 stdcall ZwQuerySystemEnvironmentValueEx(ptr ptr ptr ptr ptr) ChpeAutoZwQuerySystemEnvironmentValueEx
1724 stdcall ZwQuerySystemInformation(long long long long) ChpeAutoZwQuerySystemInformation
1725 stdcall -version=0x601+ ZwQuerySystemInformationEx(long ptr long ptr long ptr) ChpeAutoZwQuerySystemInformationEx
1726 stdcall ZwQuerySystemTime(ptr) ChpeAutoZwQuerySystemTime
1727 stdcall ZwQueryTimer(ptr long ptr long ptr) ChpeAutoZwQueryTimer
1728 stdcall ZwQueryTimerResolution(long long long) ChpeAutoZwQueryTimerResolution
1729 stdcall ZwQueryValueKey(long ptr long ptr long ptr) ChpeAutoZwQueryValueKey
1730 stdcall ZwQueryVirtualMemory(long ptr long ptr long ptr) ChpeAutoZwQueryVirtualMemory
1731 stdcall ZwQueryVolumeInformationFile(long ptr ptr long long) ChpeAutoZwQueryVolumeInformationFile
1732 stdcall -version=0x602+ ZwQueryWnfStateData(ptr ptr ptr ptr ptr ptr) ChpeAutoZwQueryWnfStateData
1733 stdcall -version=0x602+ ZwQueryWnfStateNameInformation(ptr long ptr ptr long) ChpeAutoZwQueryWnfStateNameInformation
1734 stdcall ZwQueueApcThread(long ptr long long long) ChpeAutoZwQueueApcThread
1735 stdcall ZwRaiseException(ptr ptr long) ChpeAutoZwRaiseException
1736 stdcall ZwRaiseHardError(long long long ptr long ptr) ChpeAutoZwRaiseHardError
1737 stdcall ZwReadFile(long long ptr ptr ptr ptr long ptr ptr) ChpeAutoZwReadFile
1738 stdcall ZwReadFileScatter(long long ptr ptr ptr ptr long ptr ptr) ChpeAutoZwReadFileScatter
1739 stdcall -version=0x600+ ZwReadOnlyEnlistment(ptr ptr) ChpeAutoZwReadOnlyEnlistment
1740 stdcall ZwReadRequestData(ptr ptr long ptr long ptr) ChpeAutoZwReadRequestData
1741 stdcall ZwReadVirtualMemory(long ptr ptr long ptr) ChpeAutoZwReadVirtualMemory
1742 stdcall -version=0x600+ ZwRecoverEnlistment(ptr ptr) ChpeAutoZwRecoverEnlistment
1743 stdcall -version=0x600+ ZwRecoverResourceManager(ptr) ChpeAutoZwRecoverResourceManager
1744 stdcall -version=0x600+ ZwRecoverTransactionManager(ptr) ChpeAutoZwRecoverTransactionManager
1745 stdcall -version=0x600+ ZwRegisterProtocolAddressInformation(ptr ptr long ptr long) ChpeAutoZwRegisterProtocolAddressInformation
1746 stdcall ZwRegisterThreadTerminatePort(ptr) ChpeAutoZwRegisterThreadTerminatePort
1747 stdcall -version=0x600+ ZwReleaseCMFViewOwnership() ChpeStubZwReleaseCMFViewOwnership
1748 stdcall ZwReleaseKeyedEvent(ptr ptr long ptr) ChpeAutoZwReleaseKeyedEvent
1749 stdcall ZwReleaseMutant(long ptr) ChpeAutoZwReleaseMutant
1750 stdcall ZwReleaseSemaphore(long long ptr) ChpeAutoZwReleaseSemaphore
1751 stdcall -version=0x600+ ZwReleaseWorkerFactoryWorker(ptr) ChpeStubZwReleaseWorkerFactoryWorker
1752 stdcall ZwRemoveIoCompletion(ptr ptr ptr ptr ptr) ChpeAutoZwRemoveIoCompletion
1753 stdcall -version=0x600+ ZwRemoveIoCompletionEx(ptr ptr long ptr ptr long) ChpeAutoZwRemoveIoCompletionEx
1754 stdcall ZwRemoveProcessDebug(ptr ptr) ChpeAutoZwRemoveProcessDebug
1755 stdcall ZwRenameKey(ptr ptr) ChpeAutoZwRenameKey
1756 stdcall -version=0x600+ ZwRenameTransactionManager(ptr ptr) ChpeAutoZwRenameTransactionManager
1757 stdcall ZwReplaceKey(ptr long ptr) ChpeAutoZwReplaceKey
1758 stdcall -version=0x600+ ZwReplacePartitionUnit(wstr wstr long) ChpeStubZwReplacePartitionUnit
1759 stdcall ZwReplyPort(ptr ptr) ChpeAutoZwReplyPort
1760 stdcall ZwReplyWaitReceivePort(ptr ptr ptr ptr) ChpeAutoZwReplyWaitReceivePort
1761 stdcall ZwReplyWaitReceivePortEx(ptr ptr ptr ptr ptr) ChpeAutoZwReplyWaitReceivePortEx
1762 stdcall ZwReplyWaitReplyPort(ptr ptr) ChpeAutoZwReplyWaitReplyPort
1763 stdcall ZwRequestDeviceWakeup(ptr) ChpeAutoZwRequestDeviceWakeup
1764 stdcall ZwRequestPort(ptr ptr) ChpeAutoZwRequestPort
1765 stdcall ZwRequestWaitReplyPort(ptr ptr ptr) ChpeAutoZwRequestWaitReplyPort
1766 stdcall ZwRequestWakeupLatency(long) ChpeAutoZwRequestWakeupLatency
1767 stdcall ZwResetEvent(long ptr) ChpeAutoZwResetEvent
1768 stdcall ZwResetWriteWatch(long ptr long) ChpeAutoZwResetWriteWatch
1769 stdcall ZwRestoreKey(long long long) ChpeAutoZwRestoreKey
1770 stdcall ZwResumeProcess(ptr) ChpeAutoZwResumeProcess
1771 stdcall ZwResumeThread(long long) ChpeAutoZwResumeThread
1772 stdcall -version=0x600+ ZwRollbackComplete(ptr ptr) ChpeAutoZwRollbackComplete
1773 stdcall -version=0x600+ ZwRollbackEnlistment(ptr ptr) ChpeAutoZwRollbackEnlistment
1774 stdcall -version=0x600+ ZwRollbackTransaction(ptr long) ChpeAutoZwRollbackTransaction
1775 stdcall -version=0x600+ ZwRollforwardTransactionManager(ptr ptr) ChpeAutoZwRollforwardTransactionManager
1776 stdcall ZwSaveKey(long long) ChpeAutoZwSaveKey
1777 stdcall ZwSaveKeyEx(ptr ptr long) ChpeAutoZwSaveKeyEx
1778 stdcall ZwSaveMergedKeys(ptr ptr ptr) ChpeAutoZwSaveMergedKeys
1779 stdcall ZwSecureConnectPort(ptr ptr ptr ptr ptr ptr ptr ptr ptr) ChpeAutoZwSecureConnectPort
1780 stdcall ZwSetBootEntryOrder(ptr ptr) ChpeAutoZwSetBootEntryOrder
1781 stdcall ZwSetBootOptions(ptr long) ChpeAutoZwSetBootOptions
1782 stdcall ZwSetContextThread(long ptr) ChpeAutoZwSetContextThread
1783 stdcall ZwSetDebugFilterState(long long long) ChpeAutoZwSetDebugFilterState
1784 stdcall ZwSetDefaultHardErrorPort(ptr) ChpeAutoZwSetDefaultHardErrorPort
1785 stdcall ZwSetDefaultLocale(long long) ChpeAutoZwSetDefaultLocale
1786 stdcall ZwSetDefaultUILanguage(long) ChpeAutoZwSetDefaultUILanguage
1787 stdcall ZwSetDriverEntryOrder(ptr ptr) ChpeAutoZwSetDriverEntryOrder
1788 stdcall ZwSetEaFile(long ptr ptr long) ChpeAutoZwSetEaFile
1789 stdcall ZwSetEvent(long long) ChpeAutoZwSetEvent
1790 stdcall ZwSetEventBoostPriority(ptr) ChpeAutoZwSetEventBoostPriority
1791 stdcall ZwSetHighEventPair(ptr) ChpeAutoZwSetHighEventPair
1792 stdcall ZwSetHighWaitLowEventPair(ptr) ChpeAutoZwSetHighWaitLowEventPair
1793 stdcall ZwSetInformationDebugObject(ptr long ptr long ptr) ChpeAutoZwSetInformationDebugObject
1794 stdcall -version=0x600+ ZwSetInformationEnlistment(ptr long ptr long) ChpeAutoZwSetInformationEnlistment
1795 stdcall ZwSetInformationFile(long long long long long) ChpeAutoZwSetInformationFile
1796 stdcall ZwSetInformationJobObject(long long ptr long) ChpeAutoZwSetInformationJobObject
1797 stdcall ZwSetInformationKey(long long ptr long) ChpeAutoZwSetInformationKey
1798 stdcall ZwSetInformationObject(long long ptr long) ChpeAutoZwSetInformationObject
1799 stdcall ZwSetInformationProcess(long long long long) ChpeAutoZwSetInformationProcess
1800 stdcall -version=0x600+ ZwSetInformationResourceManager(ptr long ptr long) ChpeAutoZwSetInformationResourceManager
1801 stdcall ZwSetInformationThread(long long ptr long) ChpeAutoZwSetInformationThread
1802 stdcall ZwSetInformationToken(long long ptr long) ChpeAutoZwSetInformationToken
1803 stdcall -version=0x600+ ZwSetInformationTransaction(ptr long ptr long) ChpeAutoZwSetInformationTransaction
1804 stdcall -version=0x600+ ZwSetInformationTransactionManager(ptr long ptr long) ChpeAutoZwSetInformationTransactionManager
1805 stdcall ZwSetInformationVirtualMemory(ptr long ptr ptr ptr long) ChpeAutoZwSetInformationVirtualMemory
1806 stdcall -version=0x600+ ZwSetInformationWorkerFactory(ptr long ptr long) ChpeStubZwSetInformationWorkerFactory
1807 stdcall ZwSetIntervalProfile(long long) ChpeAutoZwSetIntervalProfile
1808 stdcall ZwSetIoCompletion(ptr long ptr long long) ChpeAutoZwSetIoCompletion
1809 stdcall ZwSetLdtEntries(long int64 long int64) ChpeAutoZwSetLdtEntries
1810 stdcall ZwSetLowEventPair(ptr) ChpeAutoZwSetLowEventPair
1811 stdcall ZwSetLowWaitHighEventPair(ptr) ChpeAutoZwSetLowWaitHighEventPair
1812 stdcall ZwSetQuotaInformationFile(ptr ptr ptr long) ChpeAutoZwSetQuotaInformationFile
1813 stdcall ZwSetSecurityObject(long long ptr) ChpeAutoZwSetSecurityObject
1814 stdcall ZwSetSystemEnvironmentValue(ptr ptr) ChpeAutoZwSetSystemEnvironmentValue
1815 stdcall ZwSetSystemEnvironmentValueEx(ptr ptr ptr ptr ptr) ChpeAutoZwSetSystemEnvironmentValueEx
1816 stdcall ZwSetSystemInformation(long ptr long) ChpeAutoZwSetSystemInformation
1817 stdcall ZwSetSystemPowerState(long long long) ChpeAutoZwSetSystemPowerState
1818 stdcall ZwSetSystemTime(ptr ptr) ChpeAutoZwSetSystemTime
1819 stdcall ZwSetThreadExecutionState(long ptr) ChpeAutoZwSetThreadExecutionState
1820 stdcall ZwSetTimer(long ptr ptr ptr long long ptr) ChpeAutoZwSetTimer
1821 stdcall ZwSetTimerResolution(long long ptr) ChpeAutoZwSetTimerResolution
1822 stdcall ZwSetUuidSeed(ptr) ChpeAutoZwSetUuidSeed
1823 stdcall ZwSetValueKey(long long long long long long) ChpeAutoZwSetValueKey
1824 stdcall ZwSetVolumeInformationFile(long ptr ptr long long) ChpeAutoZwSetVolumeInformationFile
1825 stdcall ZwShutdownSystem(long) ChpeAutoZwShutdownSystem
1826 stdcall -version=0x600+ ZwShutdownWorkerFactory(ptr ptr) ChpeStubZwShutdownWorkerFactory
1827 stdcall ZwSignalAndWaitForSingleObject(long long long ptr) ChpeAutoZwSignalAndWaitForSingleObject
1828 stdcall -version=0x600+ ZwSinglePhaseReject(ptr ptr) ChpeAutoZwSinglePhaseReject
1829 stdcall ZwStartProfile(ptr) ChpeAutoZwStartProfile
1830 stdcall ZwStopProfile(ptr) ChpeAutoZwStopProfile
1831 stdcall -version=0x602+ ZwSubscribeWnfStateChange(ptr long long ptr) ChpeAutoZwSubscribeWnfStateChange
1832 stdcall ZwSuspendProcess(ptr) ChpeAutoZwSuspendProcess
1833 stdcall ZwSuspendThread(long ptr) ChpeAutoZwSuspendThread
1834 stdcall ZwSystemDebugControl(long ptr long ptr long ptr) ChpeAutoZwSystemDebugControl
1835 stdcall ZwTerminateJobObject(ptr long) ChpeAutoZwTerminateJobObject
1836 stdcall ZwTerminateProcess(ptr long) ChpeAutoZwTerminateProcess
1837 stdcall ZwTerminateThread(ptr long) ChpeAutoZwTerminateThread
1838 stdcall ZwTestAlert() ChpeAutoZwTestAlert
1839 stdcall -version=0x600+ ZwThawRegistry() ChpeStubZwThawRegistry
1840 stdcall -version=0x600+ ZwThawTransactions() ChpeStubZwThawTransactions
1841 stdcall -version=0x600+ ZwTraceControl(long ptr long ptr long ptr) ChpeAutoZwTraceControl
1842 stdcall ZwTraceEvent(ptr long long ptr) ChpeAutoZwTraceEvent
1843 stdcall ZwTranslateFilePath(ptr long ptr long) ChpeAutoZwTranslateFilePath
1844 stdcall ZwUnloadDriver(ptr) ChpeAutoZwUnloadDriver
1845 stdcall ZwUnloadKey2(ptr long) ChpeAutoZwUnloadKey2
1846 stdcall ZwUnloadKey(long) ChpeAutoZwUnloadKey
1847 stdcall ZwUnloadKeyEx(ptr ptr) ChpeAutoZwUnloadKeyEx
1848 stdcall ZwUnlockFile(long ptr ptr ptr ptr) ChpeAutoZwUnlockFile
1849 stdcall ZwUnlockVirtualMemory(long ptr ptr long) ChpeAutoZwUnlockVirtualMemory
1850 stdcall ZwUnmapViewOfSection(long ptr) ChpeAutoZwUnmapViewOfSection
1851 stdcall -version=0x602+ ZwUnsubscribeWnfStateChange(ptr) ChpeAutoZwUnsubscribeWnfStateChange
1852 stdcall -version=0x602+ ZwUpdateWnfStateData(ptr ptr long ptr ptr long long) ChpeAutoZwUpdateWnfStateData
1853 stdcall ZwVdmControl(long ptr) ChpeAutoZwVdmControl
1854 stdcall ZwWaitForDebugEvent(ptr long ptr ptr) ChpeAutoZwWaitForDebugEvent
1855 stdcall ZwWaitForKeyedEvent(ptr ptr long ptr) ChpeAutoZwWaitForKeyedEvent
1856 stdcall ZwWaitForAlertByThreadId(ptr ptr) ChpeAutoZwWaitForAlertByThreadId
1857 stdcall ZwWaitForMultipleObjects32(long ptr long long ptr) ChpeAutoZwWaitForMultipleObjects32
1858 stdcall ZwWaitForMultipleObjects(long ptr long long ptr) ChpeAutoZwWaitForMultipleObjects
1859 stdcall ZwWaitForSingleObject(long long long) ChpeAutoZwWaitForSingleObject
1860 stdcall -version=0x600+ ZwWaitForWorkViaWorkerFactory(ptr ptr long ptr ptr) ChpeStubZwWaitForWorkViaWorkerFactory
1861 stdcall ZwWaitHighEventPair(ptr) ChpeAutoZwWaitHighEventPair
1862 stdcall ZwWaitLowEventPair(ptr) ChpeAutoZwWaitLowEventPair
1863 stdcall -version=0x600+ ZwWorkerFactoryWorkerReady(ptr) ChpeStubZwWorkerFactoryWorkerReady
1864 stdcall ZwWriteFile(long long ptr ptr ptr ptr long ptr ptr) ChpeAutoZwWriteFile
1865 stdcall ZwWriteFileGather(long long ptr ptr ptr ptr long ptr ptr) ChpeAutoZwWriteFileGather
1866 stdcall ZwWriteRequestData(ptr ptr long ptr long ptr) ChpeAutoZwWriteRequestData
1867 stdcall ZwWriteVirtualMemory(long ptr ptr long ptr) ChpeAutoZwWriteVirtualMemory
1868 stdcall ZwYieldExecution() ChpeAutoZwYieldExecution
1871 cdecl __isascii(long) ChpeAuto__isascii
1872 cdecl __iscsym(long) ChpeAuto__iscsym
1873 cdecl __iscsymf(long) ChpeAuto__iscsymf
1874 cdecl -version=0x600+ __misaligned_access() ChpeStub__misaligned_access
1875 cdecl __toascii(long) ChpeAuto__toascii
1876 cdecl -ret64 _atoi64(str) ChpeAuto_atoi64
1878 extern _fltused ntdll._fltused
1879 cdecl _i64toa(double ptr long) ChpeAuto_i64toa
1880 cdecl _i64tow(double ptr long) ChpeAuto_i64tow
1881 cdecl _itoa(long ptr long) ChpeAuto_itoa
1882 cdecl _itow(long ptr long) ChpeAuto_itow
1885 cdecl _ltoa(long ptr long) ChpeAuto_ltoa
1886 cdecl _ltow(long ptr long) ChpeAuto_ltow
1887 cdecl _memccpy(ptr ptr long long) ChpeAuto_memccpy
1888 cdecl _memicmp(str str long) ChpeAuto_memicmp
1895 cdecl _splitpath(str ptr ptr ptr ptr) ChpeAuto_splitpath
1896 cdecl _strcmpi(str str) ChpeAuto_strcmpi
1897 cdecl _stricmp(str str) ChpeAuto_stricmp
1898 cdecl _strlwr(str) ChpeAuto_strlwr
1899 cdecl _strnicmp(str str long) ChpeAuto_strnicmp
1900 cdecl _strupr(str) ChpeAuto_strupr
1902 cdecl _ui64toa(double ptr long) ChpeAuto_ui64toa
1903 cdecl _ui64tow(double ptr long) ChpeAuto_ui64tow
1904 cdecl _ultoa(long ptr long) ChpeAuto_ultoa
1905 cdecl _ultow(long ptr long) ChpeAuto_ultow
1906 cdecl _vscwprintf(wstr ptr) ChpeAuto_vscwprintf
1907 cdecl _vsnprintf(ptr long str ptr) ChpeAuto_vsnprintf
1908 cdecl _vsnprintf_s(ptr long long str ptr) ChpeAuto_vsnprintf_s
1909 cdecl _vsnwprintf(ptr long wstr ptr) ChpeAuto_vsnwprintf
1910 cdecl _vsnwprintf_s(ptr long long wstr ptr) ChpeAuto_vsnwprintf_s
1911 cdecl -version=0x600+ _vswprintf(ptr wstr ptr) ChpeStub_vswprintf
1912 cdecl _wcsicmp(wstr wstr) ChpeAuto_wcsicmp
1913 cdecl _wcslwr(wstr) ChpeAuto_wcslwr
1914 cdecl _wcsnicmp(wstr wstr long) ChpeAuto_wcsnicmp
1915 cdecl _wcstoui64(wstr ptr long) ChpeAuto_wcstoui64
1916 cdecl _wcsupr(wstr) ChpeAuto_wcsupr
1917 cdecl _wtoi(wstr) ChpeAuto_wtoi
1918 cdecl _wtoi64(wstr) ChpeAuto_wtoi64
1919 cdecl _wtol(wstr) ChpeAuto_wtol
1920 cdecl abs(long) ChpeAutoabs
1921 cdecl atan(double) ChpeAutoatan
1922 cdecl atan2(double double) ChpeAutoatan2
1923 cdecl atoi(str) ChpeAutoatoi
1924 cdecl atol(str) ChpeAutoatol
1926 cdecl ceil(double) ChpeAutoceil
1927 cdecl cos(double) ChpeAutocos
1928 cdecl fabs(double) ChpeAutofabs
1929 cdecl floor(double) ChpeAutofloor
1930 cdecl isalnum(long) ChpeAutoisalnum
1931 cdecl isalpha(long) ChpeAutoisalpha
1932 cdecl iscntrl(long) ChpeAutoiscntrl
1933 cdecl isdigit(long) ChpeAutoisdigit
1934 cdecl isgraph(long) ChpeAutoisgraph
1935 cdecl islower(long) ChpeAutoislower
1936 cdecl isprint(long) ChpeAutoisprint
1937 cdecl ispunct(long) ChpeAutoispunct
1938 cdecl isspace(long) ChpeAutoisspace
1939 cdecl isupper(long) ChpeAutoisupper
1940 cdecl iswalpha(long) ChpeAutoiswalpha
1941 cdecl iswctype(long long) ChpeAutoiswctype
1942 cdecl iswdigit(long) ChpeAutoiswdigit
1943 cdecl iswlower(long) ChpeAutoiswlower
1944 cdecl iswspace(long) ChpeAutoiswspace
1945 cdecl iswxdigit(long) ChpeAutoiswxdigit
1946 cdecl isxdigit(long) ChpeAutoisxdigit
1947 cdecl labs(long) ChpeAutolabs
1948 cdecl log(double) ChpeAutolog
1950 cdecl mbstowcs(ptr str long) ChpeAutombstowcs
1951 cdecl memchr(ptr long long) ChpeAutomemchr
1952 cdecl memcmp(ptr ptr long) ChpeAutomemcmp
1954 cdecl memmove(ptr ptr long) ChpeAutomemmove
1955 cdecl memset(ptr long long) ChpeAutomemset
1956 cdecl pow(double double) ChpeAutopow
1958 cdecl sin(double) ChpeAutosin
1960 cdecl sqrt(double) ChpeAutosqrt
1962 cdecl strcat(str str) ChpeAutostrcat
1963 cdecl strchr(str long) ChpeAutostrchr
1964 cdecl strcmp(str str) ChpeAutostrcmp
1965 cdecl strcpy(ptr str) ChpeAutostrcpy
1966 cdecl -version=0x600+ strcpy_s(ptr long str) ChpeAutostrcpy_s
1967 cdecl -version=0x600+ strcat_s(ptr long str) ChpeAutostrcat_s
1968 cdecl -version=0x600+ strncpy_s(ptr long str long) ChpeAutostrncpy_s
1969 cdecl strcspn(str str) ChpeAutostrcspn
1970 cdecl strlen(str) ChpeAutostrlen
1971 cdecl strncat(str str long) ChpeAutostrncat
1972 cdecl strncmp(str str long) ChpeAutostrncmp
1973 cdecl strncpy(ptr str long) ChpeAutostrncpy
1974 cdecl strpbrk(str str) ChpeAutostrpbrk
1975 cdecl strrchr(str long) ChpeAutostrrchr
1976 cdecl strspn(str str) ChpeAutostrspn
1977 cdecl strstr(str str) ChpeAutostrstr
1978 cdecl strtol(str ptr long) ChpeAutostrtol
1979 cdecl strtoul(str ptr long) ChpeAutostrtoul
1981 cdecl tan(double) ChpeAutotan
1982 cdecl tolower(long) ChpeAutotolower
1983 cdecl toupper(long) ChpeAutotoupper
1984 cdecl towlower(long) ChpeAutotowlower
1985 cdecl towupper(long) ChpeAutotowupper
1988 cdecl vsprintf(ptr str ptr) ChpeAutovsprintf
1989 cdecl wcscat(wstr wstr) ChpeAutowcscat
1990 cdecl wcschr(wstr long) ChpeAutowcschr
1991 cdecl wcscmp(wstr wstr) ChpeAutowcscmp
1992 cdecl wcscpy(ptr wstr) ChpeAutowcscpy
1993 cdecl wcscspn(wstr wstr) ChpeAutowcscspn
1994 cdecl wcslen(wstr) ChpeAutowcslen
1995 cdecl wcsncat(wstr wstr long) ChpeAutowcsncat
1996 cdecl wcsncmp(wstr wstr long) ChpeAutowcsncmp
1997 cdecl wcsncpy(ptr wstr long) ChpeAutowcsncpy
1998 cdecl wcsnlen(wstr long) ChpeAutowcsnlen
1999 cdecl wcspbrk(wstr wstr) ChpeAutowcspbrk
2000 cdecl wcsrchr(wstr long) ChpeAutowcsrchr
2001 cdecl wcsspn(wstr wstr) ChpeAutowcsspn
2002 cdecl wcsstr(wstr wstr) ChpeAutowcsstr
2003 cdecl wcstol(wstr ptr long) ChpeAutowcstol
2004 cdecl wcstombs(ptr ptr long) ChpeAutowcstombs
2005 cdecl wcstoul(wstr ptr long) ChpeAutowcstoul
2008 stdcall -version=0x602+ -ret64 RtlGetSystemTimePrecise() ChpeAutoRtlGetSystemTimePrecise
# END typed bridge wrappers
