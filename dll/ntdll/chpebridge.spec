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
1873 cdecl __C_specific_handler(ptr long ptr ptr) ChpeCSpecificHandler
2019 stdcall ChpeDispatchExceptionNative(ptr ptr)
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
302 stdcall NtDeviceIoControlFile(long long ptr ptr ptr long ptr long ptr long) ChpeAutoNtDeviceIoControlFile
321 stdcall -version=0x600+ NtFlushProcessWriteBuffers() ChpeAutoNtFlushProcessWriteBuffers
319 stdcall NtFlushInstructionCache(long ptr long) ChpeAutoNtFlushInstructionCache
325 stdcall NtFreeVirtualMemory(long ptr ptr long) ChpeAutoNtFreeVirtualMemory
329 stdcall NtGetContextThread(long ptr) ChpeNtGetContextThread
335 stdcall -version=0x600+ NtGetNextThread(ptr ptr long long long ptr) ChpeAutoNtGetNextThread
364 stdcall NtMapViewOfSection(long long ptr long ptr ptr ptr long long long) ChpeAutoNtMapViewOfSection
365 stdcall NtMapViewOfSectionEx(long long ptr ptr ptr long long ptr long) ChpeNtMapViewOfSectionEx
370 stdcall NtNotifyChangeKey(ptr ptr ptr ptr ptr long long ptr long long) ChpeAutoNtNotifyChangeKey
371 stdcall NtNotifyChangeMultipleKeys(ptr long ptr ptr ptr ptr ptr long long ptr long long) ChpeAutoNtNotifyChangeMultipleKeys
376 stdcall NtOpenFile(ptr long ptr ptr long long) ChpeAutoNtOpenFile
411 stdcall NtProtectVirtualMemory(long ptr ptr long ptr) ChpeAutoNtProtectVirtualMemory
419 stdcall NtQueryDirectoryFile(long long ptr ptr ptr ptr long long long ptr long) ChpeAutoNtQueryDirectoryFile
429 stdcall NtQueryInformationFile(long ptr ptr long long) ChpeAutoNtQueryInformationFile
442 stdcall NtQueryKey(long long ptr long ptr) ChpeAutoNtQueryKey
261 stdcall NtCreateKey(ptr long ptr long ptr long ptr) ChpeAutoNtCreateKey
434 stdcall NtQueryInformationThread(long long ptr long ptr) ChpeAutoNtQueryInformationThread
432 stdcall NtQueryInformationProcess(ptr long ptr long ptr) ChpeAutoNtQueryInformationProcess
893 stdcall RtlFormatCurrentUserKeyPath(ptr) ChpeRtlFormatCurrentUserKeyPath
904 stdcall RtlFreeUnicodeString(ptr) ChpeRtlFreeUnicodeString
379 stdcall NtOpenKey(ptr long ptr) ChpeAutoNtOpenKey
841 stdcall NtOpenKeyEx(ptr long ptr long) ChpeAutoNtOpenKeyEx
463 stdcall NtQueryValueKey(long ptr long ptr long ptr) ChpeAutoNtQueryValueKey
560 stdcall NtSetValueKey(long ptr long long ptr long) ChpeAutoNtSetValueKey
296 stdcall NtDeleteKey(long) ChpeAutoNtDeleteKey
299 stdcall NtDeleteValueKey(long ptr) ChpeAutoNtDeleteValueKey
308 stdcall NtEnumerateKey(long long long ptr long ptr) ChpeAutoNtEnumerateKey
311 stdcall NtEnumerateValueKey(long long long ptr long ptr) ChpeAutoNtEnumerateValueKey
320 stdcall NtFlushKey(long) ChpeAutoNtFlushKey
256 stdcall NtCreateWaitCompletionPacket(ptr long ptr) ChpeAutoNtCreateWaitCompletionPacket
257 stdcall NtAssociateWaitCompletionPacket(long long long ptr ptr long long ptr) ChpeAutoNtAssociateWaitCompletionPacket
258 stdcall NtCancelWaitCompletionPacket(long long) ChpeAutoNtCancelWaitCompletionPacket
465 stdcall NtQueryVolumeInformationFile(long ptr ptr long long) ChpeAutoNtQueryVolumeInformationFile
446 stdcall NtQueryObject(long long long long long) ChpeAutoNtQueryObject
458 stdcall NtQuerySystemInformation(long ptr long ptr) ChpeAutoNtQuerySystemInformation
940 stdcall RtlGetNativeSystemInformation(long ptr long ptr) ChpeRtlGetNativeSystemInformation
464 stdcall NtQueryVirtualMemory(long ptr long ptr long ptr) ChpeAutoNtQueryVirtualMemory
471 stdcall NtRaiseException(ptr ptr long) ChpeNtRaiseException
473 stdcall NtReadFile(long long ptr ptr ptr ptr long ptr ptr) ChpeAutoNtReadFile
477 stdcall NtReadVirtualMemory(long ptr ptr long ptr) ChpeAutoNtReadVirtualMemory
518 stdcall NtSetContextThread(long ptr) ChpeNtSetContextThread
569 stdcall NtSuspendProcess(ptr) ChpeAutoNtSuspendProcess
570 stdcall NtSuspendThread(ptr ptr) ChpeAutoNtSuspendThread
507 stdcall NtResumeThread(ptr ptr) ChpeAutoNtResumeThread
573 stdcall NtTerminateProcess(long long) ChpeAutoNtTerminateProcess
574 stdcall NtTerminateThread(long long) ChpeAutoNtTerminateThread
587 stdcall NtUnmapViewOfSection(long ptr) ChpeAutoNtUnmapViewOfSection
588 stdcall NtUnmapViewOfSectionEx(long ptr long) ChpeNtUnmapViewOfSectionEx
594 stdcall NtWaitForAlertByThreadId(ptr ptr) ChpeAutoNtWaitForAlertByThreadId
602 stdcall NtWriteFile(long long ptr ptr ptr ptr long ptr ptr) ChpeAutoNtWriteFile
605 stdcall NtWriteVirtualMemory(long ptr ptr long ptr) ChpeAutoNtWriteVirtualMemory
640 stdcall RtlAddFunctionTable(ptr long long) ChpeRtlAddFunctionTable
641 stdcall RtlAddGrowableFunctionTable(ptr ptr long long long long) ChpeRtlAddGrowableFunctionTable
647 stdcall RtlAddVectoredContinueHandler(long ptr) ChpeRtlAddVectoredContinueHandler
648 stdcall RtlAddVectoredExceptionHandler(long ptr) ChpeRtlAddVectoredExceptionHandler
654 stdcall RtlAllocateHeap(ptr long ptr) ChpeRtlAllocateHeap
915 stdcall RtlGetCurrentPeb() ChpeRtlGetCurrentPeb
947 stdcall RtlGetProcessHeaps(long ptr) ChpeRtlGetProcessHeaps
1129 stdcall RtlQueryEnvironmentVariable(ptr ptr long ptr long ptr) ChpeRtlQueryEnvironmentVariable
618 stdcall RtlAcquirePrivilege(ptr long long ptr) ChpeRtlAcquirePrivilege
621 stdcall RtlAcquireSRWLockExclusive(ptr) ChpeRtlAcquireSRWLockExclusive
622 stdcall RtlAcquireSRWLockShared(ptr) ChpeRtlAcquireSRWLockShared
676 stdcall RtlCaptureContext(ptr) ChpeRtlCaptureContextX64
677 stdcall RtlCaptureStackBackTrace(long long ptr ptr) ChpeRtlCaptureStackBackTrace
693 stdcall RtlCompareMemory(ptr ptr long) ChpeRtlCompareMemory
696 stdcall RtlCompareUnicodeString(ptr ptr long) ChpeRtlCompareUnicodeString
724 stdcall RtlCopyUnicodeString(ptr ptr) ChpeRtlCopyUnicodeString
768 stdcall RtlDecodePointer(ptr) ChpeRtlDecodePointer
769 stdcall RtlDecodeSystemPointer(ptr) ChpeRtlDecodeSystemPointer
779 stdcall RtlDeleteCriticalSection(ptr) ChpeRtlDeleteCriticalSection
782 cdecl RtlDeleteFunctionTable(ptr) ChpeRtlDeleteFunctionTable
783 stdcall RtlDeleteGrowableFunctionTable(ptr) ChpeRtlDeleteGrowableFunctionTable
823 stdcall RtlEnterCriticalSection(ptr) ChpeRtlEnterCriticalSection
821 stdcall RtlEncodePointer(ptr) ChpeRtlEncodePointer
822 stdcall RtlEncodeSystemPointer(ptr) ChpeRtlEncodeSystemPointer
861 stdcall RtlExitUserThread(long) ChpeRtlExitUserThread
866 stdcall RtlFillMemory(ptr long long) ChpeRtlFillMemory
888 stdcall RtlFlsAlloc(ptr ptr) ChpeRtlFlsAlloc
889 stdcall RtlFlsFree(long) ChpeRtlFlsFree
890 stdcall RtlFlsGetValue(long ptr) ChpeRtlFlsGetValue
891 stdcall RtlFlsSetValue(long ptr) ChpeRtlFlsSetValue
899 stdcall RtlFreeHeap(long long long) ChpeRtlFreeHeap
931 stdcall RtlGetFunctionTableListHead() ChpeRtlGetFunctionTableListHead
934 stdcall RtlGetLastNtStatus() ChpeRtlGetLastNtStatus
935 stdcall RtlGetLastWin32Error() ChpeRtlGetLastWin32Error
916 stdcall RtlGetCurrentProcessorNumber() ChpeRtlGetCurrentProcessorNumber
917 stdcall -version=0x601+ RtlGetCurrentProcessorNumberEx(ptr) ChpeRtlGetCurrentProcessorNumberEx
948 stdcall -version=0x600+ RtlGetProductInfo(long long long long ptr) ChpeRtlGetProductInfo
961 stdcall RtlGetVersion(ptr) ChpeRtlGetVersion
1013 stdcall RtlGrowFunctionTable(ptr long) ChpeRtlGrowFunctionTable
986 stdcall RtlInitUnicodeString(ptr wstr) ChpeRtlInitUnicodeString
991 stdcall -version=0x600+ RtlInitializeConditionVariable(ptr) ChpeRtlInitializeConditionVariable
993 stdcall RtlInitializeCriticalSection(ptr) ChpeRtlInitializeCriticalSection
1004 stdcall RtlInitializeSListHead(ptr) ChpeRtlInitializeSListHead
1005 stdcall -version=0x600+ RtlInitializeSRWLock(ptr) ChpeRtlInitializeSRWLock
1012 cdecl RtlInstallFunctionTableCallback(double double long ptr ptr ptr) ChpeRtlInstallFunctionTableCallback
1017 stdcall RtlInterlockedFlushSList(ptr) ChpeRtlInterlockedFlushSList
1018 stdcall RtlInterlockedPopEntrySList(ptr) ChpeRtlInterlockedPopEntrySList
1019 stdcall RtlInterlockedPushEntrySList(ptr ptr) ChpeRtlInterlockedPushEntrySList
1020 stdcall RtlInterlockedPushListSList(ptr ptr ptr long) ChpeRtlInterlockedPushListSList
1021 stdcall -version=0x602+ RtlInterlockedPushListSListEx(ptr ptr ptr long) ChpeRtlInterlockedPushListSListEx
1051 stdcall RtlIsProcessorFeaturePresent(long) ChpeRtlIsProcessorFeaturePresent
1060 stdcall RtlLeaveCriticalSection(ptr) ChpeRtlLeaveCriticalSection
1082 stdcall RtlLookupFunctionEntry(long ptr ptr) ChpeRtlLookupFunctionEntry
1083 stdcall RtlLookupFunctionTable(int64 ptr ptr) ChpeRtlLookupFunctionTable
1073 stdcall -version=0xA00+ RtlLogUnexpectedCodepath(ptr) ChpeRtlLogUnexpectedCodepath
1087 stdcall RtlMoveMemory(ptr ptr long) ChpeRtlMoveMemory
1101 stdcall RtlNtStatusToDosError(long) ChpeRtlNtStatusToDosError
1114 stdcall RtlPcToFileHeader(ptr ptr) ChpeRtlPcToFileHeader
1126 stdcall RtlQueryDepthSList(ptr) ChpeRtlQueryDepthSList
1148 stdcall -norelay RtlRaiseException(ptr) ChpeRtlRaiseException
1149 stdcall RtlRaiseStatus(long) ChpeRtlRaiseStatus
1150 stdcall RtlRandom(ptr) ChpeRtlRandom
1152 stdcall RtlReAllocateHeap(long long ptr long) ChpeRtlReAllocateHeap
1164 stdcall RtlReleasePrivilege(ptr) ChpeRtlReleasePrivilege
1167 stdcall RtlReleaseSRWLockExclusive(ptr) ChpeRtlReleaseSRWLockExclusive
1168 stdcall RtlReleaseSRWLockShared(ptr) ChpeRtlReleaseSRWLockShared
1171 stdcall RtlRemoveVectoredContinueHandler(ptr) ChpeRtlRemoveVectoredContinueHandler
1172 stdcall RtlRemoveVectoredExceptionHandler(ptr) ChpeRtlRemoveVectoredExceptionHandler
1178 stdcall RtlRestoreContext(ptr ptr) ChpeRtlRestoreContext
1179 stdcall RtlRestoreLastWin32Error(long) ChpeRtlRestoreLastWin32Error
1186 stdcall -version=0x600+ RtlRunOnceExecuteOnce(ptr ptr ptr ptr) ChpeRtlRunOnceExecuteOnce
1187 stdcall -version=0x600+ RtlRunOnceInitialize(ptr) ChpeRtlRunOnceInitialize
1239 stdcall RtlSizeHeap(long long ptr) ChpeRtlSizeHeap
1199 stdcall RtlSetCriticalSectionSpinCount(ptr long) ChpeRtlSetCriticalSectionSpinCount
1213 stdcall RtlSetLastWin32Error(long) ChpeRtlSetLastWin32Error
1266 stdcall RtlTryAcquireSRWLockExclusive(ptr) ChpeRtlTryAcquireSRWLockExclusive
1267 stdcall RtlTryAcquireSRWLockShared(ptr) ChpeRtlTryAcquireSRWLockShared
1268 stdcall RtlTryEnterCriticalSection(ptr) ChpeRtlTryEnterCriticalSection
1274 stdcall -version=0x601+ RtlUTF8ToUnicodeN(ptr long ptr str long) ChpeRtlUTF8ToUnicodeN
1292 stdcall RtlUnwind(ptr ptr ptr ptr) ChpeRtlUnwind
1293 stdcall RtlUnwindEx(ptr ptr ptr ptr ptr ptr) ChpeRtlUnwindEx
1317 stdcall RtlVirtualUnwind(long int64 int64 ptr ptr ptr ptr ptr) ChpeRtlVirtualUnwind
1319 stdcall -version=0x602+ RtlWaitOnAddress(ptr ptr long ptr) ChpeRtlWaitOnAddress
1321 stdcall -version=0x602+ RtlWakeAddressAll(ptr) ChpeRtlWakeAddressAll
1322 stdcall -version=0x602+ RtlWakeAddressSingle(ptr) ChpeRtlWakeAddressSingle
1320 stdcall RtlWakeAllConditionVariable(ptr) ChpeRtlWakeAllConditionVariable
1323 stdcall RtlWakeConditionVariable(ptr) ChpeRtlWakeConditionVariable
1348 stdcall RtlZeroMemory(ptr long) ChpeRtlZeroMemory
1402 stdcall -version=0x600+ TpCallbackLeaveCriticalSectionOnCompletion(ptr ptr) ChpeTpCallbackLeaveCriticalSectionOnCompletion
1404 stdcall -version=0x600+ TpCallbackReleaseMutexOnCompletion(ptr ptr) ChpeTpCallbackReleaseMutexOnCompletion
1405 stdcall -version=0x600+ TpCallbackReleaseSemaphoreOnCompletion(ptr ptr long) ChpeTpCallbackReleaseSemaphoreOnCompletion
1408 stdcall -version=0x600+ TpCallbackSetEventOnCompletion(ptr ptr) ChpeTpCallbackSetEventOnCompletion
1409 stdcall -version=0x600+ TpCallbackUnloadDllOnCompletion(ptr ptr) ChpeTpCallbackUnloadDllOnCompletion
1410 stdcall -version=0x600+ TpCancelAsyncIoOperation(ptr) ChpeTpCancelAsyncIoOperation
1415 stdcall -version=0x600+ TpDisassociateCallback(ptr) ChpeTpDisassociateCallback
1416 stdcall -version=0x600+ TpIsTimerSet(ptr) ChpeTpIsTimerSet
1417 stdcall -version=0x600+ TpPostWork(ptr) ChpeTpPostWork
1420 stdcall -version=0x600+ TpReleaseCleanupGroup(ptr) ChpeTpReleaseCleanupGroup
1421 stdcall -version=0x600+ TpReleaseCleanupGroupMembers(ptr long ptr) ChpeTpReleaseCleanupGroupMembers
1422 stdcall -version=0x600+ TpReleaseIoCompletion(ptr) ChpeTpReleaseIoCompletion
1423 stdcall -version=0x600+ TpReleasePool(ptr) ChpeTpReleasePool
1424 stdcall TpReleaseTimer(ptr) ChpeTpReleaseTimer
1425 stdcall TpReleaseWait(ptr) ChpeTpReleaseWait
1426 stdcall -version=0x600+ TpReleaseWork(ptr) ChpeTpReleaseWork
1427 stdcall -version=0x600+ TpSetPoolMaxThreads(ptr long) ChpeTpSetPoolMaxThreads
1428 stdcall -version=0x600+ TpSetPoolMinThreads(ptr long) ChpeTpSetPoolMinThreads
1430 stdcall TpSetTimer(ptr ptr long long) ChpeTpSetTimer
1431 stdcall -version=0x602+ TpSetTimerEx(ptr ptr long long) ChpeTpSetTimerEx
1432 stdcall TpSetWait(ptr long ptr) ChpeTpSetWait
1433 stdcall -version=0x602+ TpSetWaitEx(ptr long ptr ptr) ChpeTpSetWaitEx
1435 stdcall -version=0x600+ TpStartAsyncIoOperation(ptr) ChpeTpStartAsyncIoOperation
1437 stdcall -version=0x600+ TpWaitForIoCompletion(ptr long) ChpeTpWaitForIoCompletion
1438 stdcall TpWaitForTimer(ptr long) ChpeTpWaitForTimer
1439 stdcall -version=0x600+ TpWaitForWait(ptr long) ChpeTpWaitForWait
1440 stdcall -version=0x600+ TpWaitForWork(ptr long) ChpeTpWaitForWork
1441 stdcall -ret64 VerSetConditionMask(double long long) ChpeVerSetConditionMask
1895 varargs _snprintf(ptr long str) ChpeSnprintf
1897 varargs _snwprintf(ptr long wstr) ChpeSnwprintf
1906 varargs _swprintf(ptr wstr) ChpeSwprintf
1964 varargs sprintf(ptr str) ChpeSprintf
1986 varargs swprintf(ptr wstr) ChpeSwprintf
1896 varargs _snprintf_s(ptr long long str) ChpeSnprintf_s
1898 varargs _snwprintf_s(ptr long long wstr) ChpeSnwprintf_s
1874 cdecl __chkstk() ChpeChkStk
1888 cdecl _local_unwind(ptr ptr) ChpeLocalUnwind
1958 cdecl memcpy(ptr ptr long) ChpeMemcpy
2020 stdcall ChpeEmulationDispatch(ptr)
1337 stdcall RtlWow64GetThreadSelectorEntry(ptr ptr long ptr) ChpeRtlWow64GetThreadSelectorEntry

278 stdcall NtCreateThread(ptr long ptr long ptr ptr ptr long) ChpeNtCreateThread
328 stdcall NtFsControlFile(long long ptr ptr ptr long ptr long ptr long) ChpeAutoNtFsControlFile
557 stdcall NtSetTimer(long ptr ptr ptr long long ptr) ChpeAutoNtSetTimer
759 stdcall RtlCreateUserThread(long ptr long long ptr ptr ptr ptr ptr) ChpeRtlCreateUserThread
771 stdcall RtlDecompressFragment(long ptr long ptr long long ptr ptr) ChpeRtlDecompressFragment
907 stdcall RtlGenerate8dot3Name(ptr long ptr ptr) ChpeRtlGenerate8dot3Name
937 stdcall RtlGetLengthWithoutLastFullDosOrNtPathElement(long ptr ptr) ChpeRtlGetLengthWithoutLastFullDosOrNtPathElement
957 stdcall RtlGetUnloadEventTrace() ChpeRtlGetUnloadEventTrace
1147 stdcall RtlQueueWorkItem(ptr ptr long) ChpeRtlQueueWorkItem
1160 stdcall RtlRegisterWait(ptr ptr ptr ptr long long) ChpeRtlRegisterWait
1350 stdcall RtlpApplyLengthFunction(long long ptr ptr) ChpeRtlpApplyLengthFunction
1993 stdcall vDbgPrintEx(long long str ptr) ChpevDbgPrintEx
1994 stdcall vDbgPrintExWithPrefix(str long long str ptr) ChpevDbgPrintExWithPrefix
840 stdcall LdrGetDllFullName(ptr ptr) ChpeLdrGetDllFullName
877 stdcall RtlFindExportedRoutineByName(ptr str) ChpeRtlFindExportedRoutineByName
843 stdcall RtlIsEcCode(ptr) ChpeRtlIsEcCode
852 stdcall RtlLocateExtendedFeature(ptr long ptr) ChpeRtlLocateExtendedFeature
853 stdcall RtlLocateExtendedFeature2(ptr long ptr ptr) ChpeRtlLocateExtendedFeature2
855 stdcall RtlQueryPerformanceCounter(ptr) ChpeRtlQueryPerformanceCounter
856 stdcall RtlQueryPerformanceFrequency(ptr) ChpeRtlQueryPerformanceFrequency
858 stdcall RtlQuerySystemTime(ptr) ChpeRtlQuerySystemTime
859 stdcall RtlSystemTimeToTimeFields(ptr ptr) ChpeRtlSystemTimeToTimeFields
43 varargs DbgPrintReturnControlC(str) ChpeDbgPrintReturnControlC
57 stdcall DbgUserBreakPoint() ChpeDbgUserBreakPoint
90 varargs EtwTraceMessage(int64 long ptr long) ChpeEtwTraceMessage
1308 stdcall RtlUserThreadStart(long long) ntdll.RtlUserThreadStart
1966 varargs sscanf(str str) ChpeSscanf
2013 cdecl ChpeVsscanf(str str ptr) ChpeAutoVsscanf
1967 varargs sscanf_s(str str) ChpeSscanf_s
1987 varargs swscanf_s(wstr wstr) ChpeSwscanf_s
1899 varargs _snwscanf_s(wstr long wstr) ChpeSnwscanf_s
2014 cdecl ChpeVsscanf_s(str str ptr) ChpeAutoVsscanf_s
2015 cdecl ChpeVswscanf_s(wstr wstr ptr) ChpeAutoVswscanf_s
2016 cdecl ChpeVsnwscanf_s(wstr long wstr ptr) ChpeAutoVsnwscanf_s
2017 stdcall ChpeVDbgPrintReturnControlC(str ptr) ChpeAutoVDbgPrintReturnControlC
1881 cdecl -private _errno() ChpeErrno
1887 cdecl _lfind(ptr ptr ptr long ptr) ChpeLfind
1930 cdecl bsearch(ptr ptr long long ptr) ChpeBsearch
1962 cdecl qsort(ptr long long ptr) ChpeQsort
1893 cdecl _setjmp(ptr ptr) ChpeSetJmpX64
1894 cdecl _setjmpex(ptr ptr) ChpeSetJmpX64
1954 cdecl longjmp(ptr long) ChpeLongJmp
980 stdcall RtlInitMemoryStream(ptr) ChpeRtlInitMemoryStream
982 stdcall RtlInitOutOfProcessMemoryStream(ptr) ChpeRtlInitOutOfProcessMemoryStream
1162 stdcall RtlReleaseMemoryStream(ptr) ChpeRtlReleaseMemoryStream
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
59 stdcall EtwCreateTraceInstanceId(ptr ptr) ChpeAutoEtwCreateTraceInstanceId
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
78 stdcall -version=0x600+ EtwLogTraceEvent(int64 ptr) ChpeAutoEtwLogTraceEvent
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
89 stdcall EtwTraceEventInstance(int64 ptr ptr ptr) ChpeAutoEtwTraceEventInstance
91 stdcall EtwTraceMessageVa(int64 long ptr long ptr) ChpeAutoEtwTraceMessageVa
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
281 stdcall NtCreateTimer2(ptr ptr ptr long long) ChpeAutoNtCreateTimer2
282 stdcall NtCreateToken(ptr long ptr long ptr ptr ptr ptr ptr ptr ptr ptr ptr) ChpeAutoNtCreateToken
283 stdcall -version=0x600+ NtCreateTransaction(ptr long ptr ptr ptr long long long ptr ptr) ChpeAutoNtCreateTransaction
284 stdcall -version=0x600+ NtCreateTransactionManager(ptr long ptr ptr long long) ChpeAutoNtCreateTransactionManager
285 stdcall -version=0x600+ NtCreateUserProcess(ptr ptr long long ptr ptr long long ptr ptr ptr) ChpeAutoNtCreateUserProcess
286 stdcall NtCreateWaitablePort(ptr ptr long long long) ChpeAutoNtCreateWaitablePort
287 stdcall -version=0x602+ NtCreateWnfStateName(ptr long long long ptr long ptr) ChpeAutoNtCreateWnfStateName
288 stdcall -version=0x600+ NtCreateWorkerFactory(ptr long long long long long long long long long) ChpeStubNtCreateWorkerFactory
289 stdcall NtDebugActiveProcess(ptr ptr) ChpeAutoNtDebugActiveProcess
290 stdcall NtDebugContinue(ptr ptr long) ChpeAutoNtDebugContinue
291 stdcall NtDelayExecution(long ptr) ChpeAutoNtDelayExecution
292 stdcall NtDeleteAtom(long) ChpeAutoNtDeleteAtom
293 stdcall NtDeleteBootEntry(long) ChpeAutoNtDeleteBootEntry
294 stdcall NtDeleteDriverEntry(long) ChpeAutoNtDeleteDriverEntry
295 stdcall NtDeleteFile(ptr) ChpeAutoNtDeleteFile
297 stdcall NtDeleteObjectAuditAlarm(ptr ptr long) ChpeAutoNtDeleteObjectAuditAlarm
298 stdcall -version=0x600+ NtDeletePrivateNamespace(ptr) ChpeAutoNtDeletePrivateNamespace
300 stdcall -version=0x602+ NtDeleteWnfStateData(ptr ptr) ChpeAutoNtDeleteWnfStateData
301 stdcall -version=0x602+ NtDeleteWnfStateName(ptr) ChpeAutoNtDeleteWnfStateName
303 stdcall NtDisplayString(ptr) ChpeAutoNtDisplayString
304 stdcall NtDuplicateObject(long long long ptr long long long) ChpeAutoNtDuplicateObject
305 stdcall NtDuplicateToken(long long long long long long) ChpeAutoNtDuplicateToken
306 stdcall NtEnumerateBootEntries(ptr ptr) ChpeAutoNtEnumerateBootEntries
307 stdcall NtEnumerateDriverEntries(ptr ptr) ChpeAutoNtEnumerateDriverEntries
309 stdcall NtEnumerateSystemEnvironmentValuesEx(long ptr long) ChpeAutoNtEnumerateSystemEnvironmentValuesEx
310 stdcall -version=0x600+ NtEnumerateTransactionObject(ptr long ptr long ptr) ChpeAutoNtEnumerateTransactionObject
312 stdcall NtExtendSection(ptr ptr) ChpeAutoNtExtendSection
313 stdcall NtFilterToken(ptr long ptr ptr ptr ptr) ChpeAutoNtFilterToken
314 stdcall -version=0x602+ NtCreateLowBoxToken(ptr ptr long ptr ptr long ptr long ptr) ChpeAutoNtCreateLowBoxToken
315 stdcall NtFindAtom(ptr long ptr) ChpeAutoNtFindAtom
316 stdcall NtFlushBuffersFile(long ptr) ChpeAutoNtFlushBuffersFile
317 stdcall NtFlushBuffersFileEx(long long ptr long ptr) ChpeAutoNtFlushBuffersFileEx
318 stdcall -version=0x600+ NtFlushInstallUILanguage(long long) ChpeStubNtFlushInstallUILanguage
322 stdcall NtFlushVirtualMemory(ptr ptr ptr ptr) ChpeAutoNtFlushVirtualMemory
323 stdcall NtFlushWriteBuffer() ChpeAutoNtFlushWriteBuffer
324 stdcall NtFreeUserPhysicalPages(ptr ptr ptr) ChpeAutoNtFreeUserPhysicalPages
326 stdcall -version=0x600+ NtFreezeRegistry(long) ChpeStubNtFreezeRegistry
327 stdcall -version=0x600+ NtFreezeTransactions(ptr ptr) ChpeStubNtFreezeTransactions
330 stdcall NtGetCurrentProcessorNumber() ChpeAutoNtGetCurrentProcessorNumber
331 stdcall -version=0xA00+ NtGetCurrentProcessorNumberEx(ptr) ChpeAutoNtGetCurrentProcessorNumberEx
332 stdcall NtGetDevicePowerState(ptr ptr) ChpeAutoNtGetDevicePowerState
333 stdcall -version=0x600+ NtGetMUIRegistryInfo(long ptr ptr) ChpeStubNtGetMUIRegistryInfo
334 stdcall -version=0x600+ NtGetNextProcess(long long long long ptr) ChpeStubNtGetNextProcess
336 stdcall -version=0x600+ NtGetNlsSectionPtr(long long ptr ptr ptr) ChpeAutoNtGetNlsSectionPtr
337 stdcall -version=0x600+ NtGetNotificationResourceManager(ptr ptr long ptr ptr long ptr) ChpeAutoNtGetNotificationResourceManager
338 stdcall NtGetPlugPlayEvent(long long ptr long) ChpeAutoNtGetPlugPlayEvent
339 stdcall NtGetTickCount() ChpeAutoNtGetTickCount
340 stdcall NtGetWriteWatch(long long ptr long ptr ptr ptr) ChpeAutoNtGetWriteWatch
341 stdcall NtImpersonateAnonymousToken(ptr) ChpeAutoNtImpersonateAnonymousToken
342 stdcall NtImpersonateClientOfPort(ptr ptr) ChpeAutoNtImpersonateClientOfPort
343 stdcall NtImpersonateThread(ptr ptr ptr) ChpeAutoNtImpersonateThread
344 stdcall -version=0x600+ NtInitializeNlsFiles(ptr ptr ptr) ChpeAutoNtInitializeNlsFiles
345 stdcall NtInitializeRegistry(long) ChpeAutoNtInitializeRegistry
346 stdcall NtInitiatePowerAction(long long long long) ChpeAutoNtInitiatePowerAction
347 stdcall NtIsProcessInJob(long long) ChpeAutoNtIsProcessInJob
348 stdcall NtIsSystemResumeAutomatic() ChpeAutoNtIsSystemResumeAutomatic
349 stdcall -version=0x600+ NtIsUILanguageComitted() ChpeStubNtIsUILanguageComitted
350 stdcall NtListenPort(ptr ptr) ChpeAutoNtListenPort
351 stdcall NtLoadDriver(ptr) ChpeAutoNtLoadDriver
352 stdcall NtLoadKey2(ptr ptr long) ChpeAutoNtLoadKey2
353 stdcall NtLoadKey(ptr ptr) ChpeAutoNtLoadKey
354 stdcall NtLoadKeyEx(ptr ptr long ptr ptr long ptr ptr) ChpeAutoNtLoadKeyEx
355 stdcall NtLockFile(long long ptr ptr ptr ptr ptr ptr long long) ChpeAutoNtLockFile
356 stdcall NtLockProductActivationKeys(ptr ptr) ChpeAutoNtLockProductActivationKeys
357 stdcall NtLockRegistryKey(ptr) ChpeAutoNtLockRegistryKey
358 stdcall NtLockVirtualMemory(long ptr ptr long) ChpeAutoNtLockVirtualMemory
359 stdcall NtMakePermanentObject(ptr) ChpeAutoNtMakePermanentObject
360 stdcall NtMakeTemporaryObject(long) ChpeAutoNtMakeTemporaryObject
361 stdcall -version=0x600+ NtMapCMFModule(long long ptr ptr ptr ptr) ChpeStubNtMapCMFModule
362 stdcall NtMapUserPhysicalPages(ptr ptr ptr) ChpeAutoNtMapUserPhysicalPages
363 stdcall NtMapUserPhysicalPagesScatter(ptr ptr ptr) ChpeAutoNtMapUserPhysicalPagesScatter
366 stdcall NtModifyBootEntry(ptr) ChpeAutoNtModifyBootEntry
367 stdcall NtModifyDriverEntry(ptr) ChpeAutoNtModifyDriverEntry
368 stdcall NtNotifyChangeDirectoryFile(long long ptr ptr ptr ptr long long long) ChpeAutoNtNotifyChangeDirectoryFile
369 stdcall NtNotifyChangeDirectoryFileEx(long long ptr ptr ptr ptr long long long long) ChpeAutoNtNotifyChangeDirectoryFileEx
372 stdcall NtOpenDirectoryObject(long long long) ChpeAutoNtOpenDirectoryObject
373 stdcall -version=0x600+ NtOpenEnlistment(ptr long ptr ptr ptr) ChpeAutoNtOpenEnlistment
374 stdcall NtOpenEvent(long long long) ChpeAutoNtOpenEvent
375 stdcall NtOpenEventPair(ptr long ptr) ChpeAutoNtOpenEventPair
377 stdcall NtOpenIoCompletion(ptr long ptr) ChpeAutoNtOpenIoCompletion
378 stdcall NtOpenJobObject(ptr long ptr) ChpeAutoNtOpenJobObject
380 stdcall -version=0x600+ NtOpenKeyTransacted(ptr long ptr ptr) ChpeAutoNtOpenKeyTransacted
381 stdcall -version=0x600+ NtOpenKeyTransactedEx(ptr long ptr long ptr) ChpeAutoNtOpenKeyTransactedEx
382 stdcall NtOpenKeyedEvent(ptr long ptr) ChpeAutoNtOpenKeyedEvent
383 stdcall NtOpenMutant(ptr long ptr) ChpeAutoNtOpenMutant
384 stdcall NtOpenObjectAuditAlarm(ptr ptr ptr ptr ptr ptr long long ptr long long ptr) ChpeAutoNtOpenObjectAuditAlarm
385 stdcall -version=0x600+ NtOpenPrivateNamespace(ptr long ptr ptr) ChpeAutoNtOpenPrivateNamespace
386 stdcall NtOpenProcess(ptr long ptr ptr) ChpeAutoNtOpenProcess
387 stdcall NtOpenProcessToken(long long ptr) ChpeAutoNtOpenProcessToken
388 stdcall NtOpenProcessTokenEx(long long long ptr) ChpeAutoNtOpenProcessTokenEx
389 stdcall -version=0x600+ NtOpenResourceManager(ptr long ptr ptr ptr) ChpeAutoNtOpenResourceManager
390 stdcall NtOpenSection(ptr long ptr) ChpeAutoNtOpenSection
391 stdcall NtOpenSemaphore(long long ptr) ChpeAutoNtOpenSemaphore
392 stdcall -version=0x600+ NtOpenSession(ptr long ptr) ChpeStubNtOpenSession
393 stdcall NtOpenSymbolicLinkObject(ptr long ptr) ChpeAutoNtOpenSymbolicLinkObject
394 stdcall NtOpenThread(ptr long ptr ptr) ChpeAutoNtOpenThread
395 stdcall NtOpenThreadToken(long long long ptr) ChpeAutoNtOpenThreadToken
396 stdcall NtOpenThreadTokenEx(long long long long ptr) ChpeAutoNtOpenThreadTokenEx
397 stdcall NtOpenTimer(ptr long ptr) ChpeAutoNtOpenTimer
398 stdcall -version=0x600+ NtOpenTransaction(ptr long ptr ptr ptr) ChpeAutoNtOpenTransaction
399 stdcall -version=0x600+ NtOpenTransactionManager(ptr long ptr ptr ptr long) ChpeAutoNtOpenTransactionManager
400 stdcall NtPlugPlayControl(ptr ptr long) ChpeAutoNtPlugPlayControl
401 stdcall NtPowerInformation(long ptr long ptr long) ChpeAutoNtPowerInformation
402 stdcall -version=0x600+ NtPrePrepareComplete(ptr ptr) ChpeAutoNtPrePrepareComplete
403 stdcall -version=0x600+ NtPrePrepareEnlistment(ptr ptr) ChpeAutoNtPrePrepareEnlistment
404 stdcall -version=0x600+ NtPrepareComplete(ptr ptr) ChpeAutoNtPrepareComplete
405 stdcall -version=0x600+ NtPrepareEnlistment(ptr ptr) ChpeAutoNtPrepareEnlistment
406 stdcall NtPrivilegeCheck(ptr ptr ptr) ChpeAutoNtPrivilegeCheck
407 stdcall NtPrivilegeObjectAuditAlarm(ptr ptr ptr long ptr long) ChpeAutoNtPrivilegeObjectAuditAlarm
408 stdcall NtPrivilegedServiceAuditAlarm(ptr ptr ptr ptr long) ChpeAutoNtPrivilegedServiceAuditAlarm
409 stdcall -version=0x600+ NtPropagationComplete(ptr long long ptr) ChpeAutoNtPropagationComplete
410 stdcall -version=0x600+ NtPropagationFailed(ptr long long) ChpeAutoNtPropagationFailed
412 stdcall NtPulseEvent(long ptr) ChpeAutoNtPulseEvent
413 stdcall NtQueryAttributesFile(ptr ptr) ChpeAutoNtQueryAttributesFile
414 stdcall NtQueryBootEntryOrder(ptr ptr) ChpeAutoNtQueryBootEntryOrder
415 stdcall NtQueryBootOptions(ptr ptr) ChpeAutoNtQueryBootOptions
416 stdcall NtQueryDebugFilterState(long long) ChpeAutoNtQueryDebugFilterState
417 stdcall NtQueryDefaultLocale(long ptr) ChpeAutoNtQueryDefaultLocale
418 stdcall NtQueryDefaultUILanguage(ptr) ChpeAutoNtQueryDefaultUILanguage
420 stdcall NtQueryDirectoryFileEx(long long ptr ptr ptr ptr long long long ptr) ChpeAutoNtQueryDirectoryFileEx
421 stdcall NtQueryDirectoryObject(long ptr long long long ptr ptr) ChpeAutoNtQueryDirectoryObject
422 stdcall NtQueryDriverEntryOrder(ptr ptr) ChpeAutoNtQueryDriverEntryOrder
423 stdcall NtQueryEaFile(long ptr ptr long long ptr long ptr long) ChpeAutoNtQueryEaFile
424 stdcall NtQueryEvent(long long ptr long ptr) ChpeAutoNtQueryEvent
425 stdcall NtQueryFullAttributesFile(ptr ptr) ChpeAutoNtQueryFullAttributesFile
426 stdcall NtQueryInformationAtom(long long ptr long ptr) ChpeAutoNtQueryInformationAtom
427 stdcall NtQueryInformationByName(ptr ptr ptr long long) ChpeAutoNtQueryInformationByName
428 stdcall -version=0x600+ NtQueryInformationEnlistment(ptr long ptr long ptr) ChpeAutoNtQueryInformationEnlistment
430 stdcall NtQueryInformationJobObject(ptr long ptr long ptr) ChpeAutoNtQueryInformationJobObject
431 stdcall NtQueryInformationPort(ptr long ptr long ptr) ChpeAutoNtQueryInformationPort
433 stdcall -version=0x600+ NtQueryInformationResourceManager(ptr long ptr long ptr) ChpeAutoNtQueryInformationResourceManager
435 stdcall NtQueryInformationToken(ptr long ptr long ptr) ChpeAutoNtQueryInformationToken
436 stdcall -version=0x600+ NtQueryInformationTransaction(ptr long ptr long ptr) ChpeAutoNtQueryInformationTransaction
437 stdcall -version=0x600+ NtQueryInformationTransactionManager(ptr long ptr long ptr) ChpeAutoNtQueryInformationTransactionManager
438 stdcall -version=0x600+ NtQueryInformationWorkerFactory(ptr long ptr long ptr) ChpeStubNtQueryInformationWorkerFactory
439 stdcall NtQueryInstallUILanguage(ptr) ChpeAutoNtQueryInstallUILanguage
440 stdcall NtQueryIntervalProfile(long ptr) ChpeAutoNtQueryIntervalProfile
441 stdcall NtQueryIoCompletion(long long ptr long ptr) ChpeAutoNtQueryIoCompletion
443 stdcall -version=0x600+ NtQueryLicenseValue(ptr ptr ptr long ptr) ChpeAutoNtQueryLicenseValue
444 stdcall NtQueryMultipleValueKey(long ptr long ptr long ptr) ChpeAutoNtQueryMultipleValueKey
445 stdcall NtQueryMutant(long long ptr long ptr) ChpeAutoNtQueryMutant
447 stdcall NtQueryOpenSubKeys(ptr ptr) ChpeAutoNtQueryOpenSubKeys
448 stdcall NtQueryOpenSubKeysEx(ptr long ptr ptr) ChpeAutoNtQueryOpenSubKeysEx
449 stdcall NtQueryPerformanceCounter(ptr ptr) ChpeAutoNtQueryPerformanceCounter
450 stdcall NtQueryPortInformationProcess() ChpeAutoNtQueryPortInformationProcess
451 stdcall NtQueryQuotaInformationFile(ptr ptr ptr long long ptr long ptr long) ChpeAutoNtQueryQuotaInformationFile
452 stdcall NtQuerySection(long long long long long) ChpeAutoNtQuerySection
453 stdcall NtQuerySecurityObject(long long long long long) ChpeAutoNtQuerySecurityObject
454 stdcall NtQuerySemaphore(long long ptr long ptr) ChpeAutoNtQuerySemaphore
455 stdcall NtQuerySymbolicLinkObject(long ptr ptr) ChpeAutoNtQuerySymbolicLinkObject
456 stdcall NtQuerySystemEnvironmentValue(ptr ptr long ptr) ChpeAutoNtQuerySystemEnvironmentValue
457 stdcall NtQuerySystemEnvironmentValueEx(ptr ptr ptr ptr ptr) ChpeAutoNtQuerySystemEnvironmentValueEx
459 stdcall -version=0x601+ NtQuerySystemInformationEx(long ptr long ptr long ptr) ChpeAutoNtQuerySystemInformationEx
460 stdcall NtQuerySystemTime(ptr) ChpeAutoNtQuerySystemTime
461 stdcall NtQueryTimer(ptr long ptr long ptr) ChpeAutoNtQueryTimer
462 stdcall NtQueryTimerResolution(long long long) ChpeAutoNtQueryTimerResolution
466 stdcall -version=0x602+ NtQueryWnfStateData(ptr ptr ptr ptr ptr ptr) ChpeAutoNtQueryWnfStateData
467 stdcall -version=0x602+ NtQueryWnfStateNameInformation(ptr long ptr ptr long) ChpeAutoNtQueryWnfStateNameInformation
468 stdcall NtQueueApcThread(long ptr long long long) ChpeAutoNtQueueApcThread
469 stdcall -version=0x601+ NtQueueApcThreadEx(long long ptr long long long) ChpeAutoNtQueueApcThreadEx
470 stdcall -version=0xA00+ NtQueueApcThreadEx2(long long long ptr long long long) ChpeAutoNtQueueApcThreadEx2
472 stdcall NtRaiseHardError(long long long ptr long ptr) ChpeAutoNtRaiseHardError
474 stdcall NtReadFileScatter(long long ptr ptr ptr ptr long ptr ptr) ChpeAutoNtReadFileScatter
475 stdcall -version=0x600+ NtReadOnlyEnlistment(ptr ptr) ChpeAutoNtReadOnlyEnlistment
476 stdcall NtReadRequestData(ptr ptr long ptr long ptr) ChpeAutoNtReadRequestData
478 stdcall -version=0x600+ NtRecoverEnlistment(ptr ptr) ChpeAutoNtRecoverEnlistment
479 stdcall -version=0x600+ NtRecoverResourceManager(ptr) ChpeAutoNtRecoverResourceManager
480 stdcall -version=0x600+ NtRecoverTransactionManager(ptr) ChpeAutoNtRecoverTransactionManager
481 stdcall -version=0x600+ NtRegisterProtocolAddressInformation(ptr ptr long ptr long) ChpeAutoNtRegisterProtocolAddressInformation
482 stdcall NtRegisterThreadTerminatePort(ptr) ChpeAutoNtRegisterThreadTerminatePort
483 stdcall -version=0x600+ NtReleaseCMFViewOwnership() ChpeStubNtReleaseCMFViewOwnership
484 stdcall NtReleaseKeyedEvent(ptr ptr long ptr) ChpeAutoNtReleaseKeyedEvent
485 stdcall NtReleaseMutant(long ptr) ChpeAutoNtReleaseMutant
486 stdcall NtReleaseSemaphore(long long ptr) ChpeAutoNtReleaseSemaphore
487 stdcall -version=0x600+ NtReleaseWorkerFactoryWorker(ptr) ChpeStubNtReleaseWorkerFactoryWorker
488 stdcall NtRemoveIoCompletion(ptr ptr ptr ptr ptr) ChpeAutoNtRemoveIoCompletion
489 stdcall -version=0x600+ NtRemoveIoCompletionEx(ptr ptr long ptr ptr long) ChpeAutoNtRemoveIoCompletionEx
490 stdcall NtRemoveProcessDebug(ptr ptr) ChpeAutoNtRemoveProcessDebug
491 stdcall NtRenameKey(ptr ptr) ChpeAutoNtRenameKey
492 stdcall -version=0x600+ NtRenameTransactionManager(ptr ptr) ChpeAutoNtRenameTransactionManager
493 stdcall NtReplaceKey(ptr long ptr) ChpeAutoNtReplaceKey
494 stdcall -version=0x600+ NtReplacePartitionUnit(wstr wstr long) ChpeStubNtReplacePartitionUnit
495 stdcall NtReplyPort(ptr ptr) ChpeAutoNtReplyPort
496 stdcall NtReplyWaitReceivePort(ptr ptr ptr ptr) ChpeAutoNtReplyWaitReceivePort
497 stdcall NtReplyWaitReceivePortEx(ptr ptr ptr ptr ptr) ChpeAutoNtReplyWaitReceivePortEx
498 stdcall NtReplyWaitReplyPort(ptr ptr) ChpeAutoNtReplyWaitReplyPort
499 stdcall NtRequestDeviceWakeup(ptr) ChpeAutoNtRequestDeviceWakeup
500 stdcall NtRequestPort(ptr ptr) ChpeAutoNtRequestPort
501 stdcall NtRequestWaitReplyPort(ptr ptr ptr) ChpeAutoNtRequestWaitReplyPort
502 stdcall NtRequestWakeupLatency(long) ChpeAutoNtRequestWakeupLatency
503 stdcall NtResetEvent(long ptr) ChpeAutoNtResetEvent
504 stdcall NtResetWriteWatch(long ptr long) ChpeAutoNtResetWriteWatch
505 stdcall NtRestoreKey(long long long) ChpeAutoNtRestoreKey
506 stdcall NtResumeProcess(ptr) ChpeAutoNtResumeProcess
508 stdcall -version=0x600+ NtRollbackComplete(ptr ptr) ChpeAutoNtRollbackComplete
509 stdcall -version=0x600+ NtRollbackEnlistment(ptr ptr) ChpeAutoNtRollbackEnlistment
510 stdcall -version=0x600+ NtRollbackTransaction(ptr long) ChpeAutoNtRollbackTransaction
511 stdcall -version=0x600+ NtRollforwardTransactionManager(ptr ptr) ChpeAutoNtRollforwardTransactionManager
512 stdcall NtSaveKey(long long) ChpeAutoNtSaveKey
513 stdcall NtSaveKeyEx(ptr ptr long) ChpeAutoNtSaveKeyEx
514 stdcall NtSaveMergedKeys(ptr ptr ptr) ChpeAutoNtSaveMergedKeys
515 stdcall NtSecureConnectPort(ptr ptr ptr ptr ptr ptr ptr ptr ptr) ChpeAutoNtSecureConnectPort
516 stdcall NtSetBootEntryOrder(ptr ptr) ChpeAutoNtSetBootEntryOrder
517 stdcall NtSetBootOptions(ptr long) ChpeAutoNtSetBootOptions
519 stdcall NtSetDebugFilterState(long long long) ChpeAutoNtSetDebugFilterState
520 stdcall NtSetDefaultHardErrorPort(ptr) ChpeAutoNtSetDefaultHardErrorPort
521 stdcall NtSetDefaultLocale(long long) ChpeAutoNtSetDefaultLocale
522 stdcall NtSetDefaultUILanguage(long) ChpeAutoNtSetDefaultUILanguage
523 stdcall NtSetDriverEntryOrder(ptr ptr) ChpeAutoNtSetDriverEntryOrder
524 stdcall NtSetEaFile(long ptr ptr long) ChpeAutoNtSetEaFile
525 stdcall NtSetEvent(long long) ChpeAutoNtSetEvent
526 stdcall NtSetEventBoostPriority(ptr) ChpeAutoNtSetEventBoostPriority
527 stdcall NtSetHighEventPair(ptr) ChpeAutoNtSetHighEventPair
528 stdcall NtSetHighWaitLowEventPair(ptr) ChpeAutoNtSetHighWaitLowEventPair
529 stdcall NtSetInformationDebugObject(ptr long ptr long ptr) ChpeAutoNtSetInformationDebugObject
530 stdcall -version=0x600+ NtSetInformationEnlistment(ptr long ptr long) ChpeAutoNtSetInformationEnlistment
531 stdcall NtSetInformationFile(ptr ptr ptr long long) ChpeAutoNtSetInformationFile
532 stdcall NtSetInformationJobObject(ptr long ptr long) ChpeAutoNtSetInformationJobObject
533 stdcall NtSetInformationKey(ptr long ptr long) ChpeAutoNtSetInformationKey
534 stdcall NtSetInformationObject(ptr long ptr long) ChpeAutoNtSetInformationObject
535 stdcall NtSetInformationProcess(ptr long ptr long) ChpeAutoNtSetInformationProcess
536 stdcall -version=0x600+ NtSetInformationResourceManager(ptr long ptr long) ChpeAutoNtSetInformationResourceManager
537 stdcall NtSetInformationThread(ptr long ptr long) ChpeAutoNtSetInformationThread
538 stdcall NtSetInformationVirtualMemory(ptr long ptr ptr ptr long) ChpeAutoNtSetInformationVirtualMemory
539 stdcall NtSetInformationToken(ptr long ptr long) ChpeAutoNtSetInformationToken
540 stdcall -version=0x600+ NtSetInformationTransaction(ptr long ptr long) ChpeAutoNtSetInformationTransaction
541 stdcall -version=0x600+ NtSetInformationTransactionManager(ptr long ptr long) ChpeAutoNtSetInformationTransactionManager
542 stdcall -version=0x600+ NtSetInformationWorkerFactory(ptr long ptr long) ChpeStubNtSetInformationWorkerFactory
543 stdcall NtSetIntervalProfile(long long) ChpeAutoNtSetIntervalProfile
544 stdcall NtSetIoCompletion(ptr long ptr long long) ChpeAutoNtSetIoCompletion
545 stdcall -version=0x600+ NtSetIoCompletionEx(ptr ptr long long long long) ChpeAutoNtSetIoCompletionEx
546 stdcall NtSetLdtEntries(long int64 long int64) ChpeAutoNtSetLdtEntries
547 stdcall NtSetLowEventPair(ptr) ChpeAutoNtSetLowEventPair
548 stdcall NtSetLowWaitHighEventPair(ptr) ChpeAutoNtSetLowWaitHighEventPair
549 stdcall NtSetQuotaInformationFile(ptr ptr ptr long) ChpeAutoNtSetQuotaInformationFile
550 stdcall NtSetSecurityObject(long long ptr) ChpeAutoNtSetSecurityObject
551 stdcall NtSetSystemEnvironmentValue(ptr ptr) ChpeAutoNtSetSystemEnvironmentValue
552 stdcall NtSetSystemEnvironmentValueEx(ptr ptr ptr ptr ptr) ChpeAutoNtSetSystemEnvironmentValueEx
553 stdcall NtSetSystemInformation(long ptr long) ChpeAutoNtSetSystemInformation
554 stdcall NtSetSystemPowerState(long long long) ChpeAutoNtSetSystemPowerState
555 stdcall NtSetSystemTime(ptr ptr) ChpeAutoNtSetSystemTime
556 stdcall NtSetThreadExecutionState(long ptr) ChpeAutoNtSetThreadExecutionState
558 stdcall NtSetTimerResolution(long long ptr) ChpeAutoNtSetTimerResolution
559 stdcall NtSetUuidSeed(ptr) ChpeAutoNtSetUuidSeed
561 stdcall NtSetVolumeInformationFile(long ptr ptr long long) ChpeAutoNtSetVolumeInformationFile
562 stdcall NtShutdownSystem(long) ChpeAutoNtShutdownSystem
563 stdcall -version=0x600+ NtShutdownWorkerFactory(ptr ptr) ChpeStubNtShutdownWorkerFactory
564 stdcall NtSignalAndWaitForSingleObject(long long long ptr) ChpeAutoNtSignalAndWaitForSingleObject
565 stdcall -version=0x600+ NtSinglePhaseReject(ptr ptr) ChpeAutoNtSinglePhaseReject
566 stdcall NtStartProfile(ptr) ChpeAutoNtStartProfile
567 stdcall NtStopProfile(ptr) ChpeAutoNtStopProfile
568 stdcall -version=0x602+ NtSubscribeWnfStateChange(ptr long long ptr) ChpeAutoNtSubscribeWnfStateChange
571 stdcall NtSystemDebugControl(long ptr long ptr long ptr) ChpeAutoNtSystemDebugControl
572 stdcall NtTerminateJobObject(ptr long) ChpeAutoNtTerminateJobObject
575 stdcall NtTestAlert() ChpeAutoNtTestAlert
576 stdcall -version=0x600+ NtThawRegistry() ChpeStubNtThawRegistry
577 stdcall -version=0x600+ NtThawTransactions() ChpeStubNtThawTransactions
578 stdcall -version=0x600+ NtTraceControl(long ptr long ptr long ptr) ChpeAutoNtTraceControl
579 stdcall NtTraceEvent(ptr long long ptr) ChpeAutoNtTraceEvent
580 stdcall NtTranslateFilePath(ptr long ptr long) ChpeAutoNtTranslateFilePath
581 stdcall NtUnloadDriver(ptr) ChpeAutoNtUnloadDriver
582 stdcall NtUnloadKey2(ptr long) ChpeAutoNtUnloadKey2
583 stdcall NtUnloadKey(long) ChpeAutoNtUnloadKey
584 stdcall NtUnloadKeyEx(ptr ptr) ChpeAutoNtUnloadKeyEx
585 stdcall NtUnlockFile(long ptr ptr ptr ptr) ChpeAutoNtUnlockFile
586 stdcall NtUnlockVirtualMemory(long ptr ptr long) ChpeAutoNtUnlockVirtualMemory
589 stdcall -version=0x602+ NtUnsubscribeWnfStateChange(ptr) ChpeAutoNtUnsubscribeWnfStateChange
590 stdcall -version=0x602+ NtUpdateWnfStateData(ptr ptr long ptr ptr long long) ChpeAutoNtUpdateWnfStateData
591 stdcall NtVdmControl(long ptr) ChpeAutoNtVdmControl
592 stdcall NtWaitForDebugEvent(ptr long ptr ptr) ChpeAutoNtWaitForDebugEvent
593 stdcall NtWaitForKeyedEvent(ptr ptr long ptr) ChpeAutoNtWaitForKeyedEvent
595 stdcall NtWaitForMultipleObjects32(long ptr long long ptr) ChpeAutoNtWaitForMultipleObjects32
596 stdcall NtWaitForMultipleObjects(long ptr long long ptr) ChpeAutoNtWaitForMultipleObjects
597 stdcall NtWaitForSingleObject(long long long) ChpeAutoNtWaitForSingleObject
598 stub -version=0x600+ NtWaitForWorkViaWorkerFactory
599 stdcall NtWaitHighEventPair(ptr) ChpeAutoNtWaitHighEventPair
600 stdcall NtWaitLowEventPair(ptr) ChpeAutoNtWaitLowEventPair
601 stdcall -version=0x600+ NtWorkerFactoryWorkerReady(long) ChpeStubNtWorkerFactoryWorkerReady
603 stdcall NtWriteFileGather(long long ptr ptr ptr ptr long ptr ptr) ChpeAutoNtWriteFileGather
604 stdcall NtWriteRequestData(ptr ptr long ptr long ptr) ChpeAutoNtWriteRequestData
606 stdcall NtYieldExecution() ChpeAutoNtYieldExecution
611 stdcall PfxFindPrefix(ptr ptr) ChpeAutoPfxFindPrefix
612 stdcall PfxInitialize(ptr) ChpeAutoPfxInitialize
613 stdcall PfxInsertPrefix(ptr ptr ptr) ChpeAutoPfxInsertPrefix
614 stdcall PfxRemovePrefix(ptr ptr) ChpeAutoPfxRemovePrefix
615 stdcall RtlAbortRXact(ptr) ChpeAutoRtlAbortRXact
616 stdcall RtlAbsoluteToSelfRelativeSD(ptr ptr ptr) ChpeAutoRtlAbsoluteToSelfRelativeSD
617 stdcall RtlAcquirePebLock() ChpeAutoRtlAcquirePebLock
619 stdcall RtlAcquireResourceExclusive(ptr long) ChpeAutoRtlAcquireResourceExclusive
620 stdcall RtlAcquireResourceShared(ptr long) ChpeAutoRtlAcquireResourceShared
623 stdcall RtlActivateActivationContext(long ptr ptr) ChpeAutoRtlActivateActivationContext
624 stdcall RtlActivateActivationContextEx(long ptr ptr ptr) ChpeAutoRtlActivateActivationContextEx
625 stdcall RtlActivateActivationContextUnsafeFast(ptr ptr) ChpeAutoRtlActivateActivationContextUnsafeFast
626 stdcall RtlAddAccessAllowedAce(ptr long long ptr) ChpeAutoRtlAddAccessAllowedAce
627 stdcall RtlAddAccessAllowedAceEx(ptr long long long ptr) ChpeAutoRtlAddAccessAllowedAceEx
628 stdcall RtlAddAccessAllowedObjectAce(ptr long long long ptr ptr ptr) ChpeAutoRtlAddAccessAllowedObjectAce
629 stdcall RtlAddAccessDeniedAce(ptr long long ptr) ChpeAutoRtlAddAccessDeniedAce
630 stdcall RtlAddAccessDeniedAceEx(ptr long long long ptr) ChpeAutoRtlAddAccessDeniedAceEx
631 stdcall RtlAddAccessDeniedObjectAce(ptr long long long ptr ptr ptr) ChpeAutoRtlAddAccessDeniedObjectAce
632 stdcall RtlAddAce(ptr long long ptr long) ChpeAutoRtlAddAce
633 stdcall RtlAddActionToRXact(ptr long ptr long ptr long) ChpeAutoRtlAddActionToRXact
634 stdcall RtlAddAtomToAtomTable(ptr wstr ptr) ChpeAutoRtlAddAtomToAtomTable
635 stdcall RtlAddAttributeActionToRXact(ptr long ptr ptr ptr long ptr long) ChpeAutoRtlAddAttributeActionToRXact
636 stdcall RtlAddAuditAccessAce(ptr long long ptr long long) ChpeAutoRtlAddAuditAccessAce
637 stdcall RtlAddAuditAccessAceEx(ptr long long long ptr long long) ChpeAutoRtlAddAuditAccessAceEx
638 stdcall RtlAddAuditAccessObjectAce(ptr long long long ptr ptr ptr long long) ChpeAutoRtlAddAuditAccessObjectAce
639 stdcall  RtlAddCompoundAce(ptr long long long ptr ptr) ChpeStubRtlAddCompoundAce
642 stdcall -version=0x600+ RtlAddMandatoryAce(ptr long long long long ptr) ChpeAutoRtlAddMandatoryAce
643 stdcall RtlAddRefActivationContext(ptr) ChpeAutoRtlAddRefActivationContext
644 stdcall RtlAddRefMemoryStream(ptr) ChpeAutoRtlAddRefMemoryStream
645 stdcall -version=0x600+ RtlAddSIDToBoundaryDescriptor(ptr ptr) ChpeAutoRtlAddSIDToBoundaryDescriptor
646 stdcall -version=0x600+ RtlAddIntegrityLabelToBoundaryDescriptor(ptr ptr) ChpeAutoRtlAddIntegrityLabelToBoundaryDescriptor
649 stdcall RtlAddressInSectionTable(ptr ptr long) ChpeAutoRtlAddressInSectionTable
650 stdcall RtlAdjustPrivilege(long long long ptr) ChpeAutoRtlAdjustPrivilege
651 stdcall RtlAllocateActivationContextStack(ptr) ChpeAutoRtlAllocateActivationContextStack
652 stdcall RtlAllocateAndInitializeSid(ptr long long long long long long long long long ptr) ChpeAutoRtlAllocateAndInitializeSid
653 stdcall RtlAllocateHandle(ptr ptr) ChpeAutoRtlAllocateHandle
655 stdcall -version=0x600+ RtlAllocateMemoryBlockLookaside(ptr long ptr) ChpeStubRtlAllocateMemoryBlockLookaside
656 stdcall -version=0x600+ RtlAllocateMemoryZone(long long ptr) ChpeStubRtlAllocateMemoryZone
657 stdcall RtlAnsiCharToUnicodeChar(ptr) ChpeAutoRtlAnsiCharToUnicodeChar
658 stdcall RtlAnsiStringToUnicodeSize(ptr) ChpeAutoRtlAnsiStringToUnicodeSize
659 stdcall RtlAnsiStringToUnicodeString(ptr ptr long) ChpeAutoRtlAnsiStringToUnicodeString
660 stdcall RtlAppendAsciizToString(ptr str) ChpeAutoRtlAppendAsciizToString
661 stdcall  RtlAppendPathElement(ptr ptr ptr) ChpeStubRtlAppendPathElement
662 stdcall RtlAppendStringToString(ptr ptr) ChpeAutoRtlAppendStringToString
663 stdcall RtlAppendUnicodeStringToString(ptr ptr) ChpeAutoRtlAppendUnicodeStringToString
664 stdcall RtlAppendUnicodeToString(ptr wstr) ChpeAutoRtlAppendUnicodeToString
665 stdcall RtlApplicationVerifierStop(ptr str ptr str ptr str ptr str ptr str) ChpeAutoRtlApplicationVerifierStop
666 stdcall RtlApplyRXact(ptr) ChpeAutoRtlApplyRXact
667 stdcall RtlApplyRXactNoFlush(ptr) ChpeAutoRtlApplyRXactNoFlush
668 stdcall RtlAreAllAccessesGranted(long long) ChpeAutoRtlAreAllAccessesGranted
669 stdcall RtlAreAnyAccessesGranted(long long) ChpeAutoRtlAreAnyAccessesGranted
670 stdcall RtlAreBitsClear(ptr long long) ChpeAutoRtlAreBitsClear
671 stdcall RtlAreBitsSet(ptr long long) ChpeAutoRtlAreBitsSet
672 stdcall RtlAssert(ptr ptr long ptr) ChpeAutoRtlAssert
673 stdcall RtlBarrier(ptr long) ChpeAutoRtlBarrier
674 stdcall -version=0x600+ RtlBarrierForDelete(long long) ChpeStubRtlBarrierForDelete
675 stdcall RtlCancelTimer(ptr ptr) ChpeAutoRtlCancelTimer
678 stdcall RtlCharToInteger(ptr long ptr) ChpeAutoRtlCharToInteger
679 stdcall RtlCheckForOrphanedCriticalSections(ptr) ChpeAutoRtlCheckForOrphanedCriticalSections
680 stdcall -version=0x602+ RtlCheckPortableOperatingSystem(ptr) ChpeAutoRtlCheckPortableOperatingSystem
681 stdcall RtlCheckRegistryKey(long ptr) ChpeAutoRtlCheckRegistryKey
682 stdcall -version=0x600+ RtlCleanUpTEBLangLists() ChpeStubRtlCleanUpTEBLangLists
683 stdcall RtlClearAllBits(ptr) ChpeAutoRtlClearAllBits
684 stdcall RtlClearBits(ptr long long) ChpeAutoRtlClearBits
685 stdcall RtlCloneMemoryStream(ptr ptr) ChpeAutoRtlCloneMemoryStream
686 stdcall -version=0x600+ RtlCloneUserProcess(long long long long long) ChpeStubRtlCloneUserProcess
687 stdcall -version=0x600+ RtlCmDecodeMemIoResource(ptr ptr) ChpeAutoRtlCmDecodeMemIoResource
688 stdcall -version=0x600+ RtlCmEncodeMemIoResource(ptr long int64 int64) ChpeAutoRtlCmEncodeMemIoResource
689 stdcall -version=0x600+ RtlCommitDebugInfo(ptr long) ChpeStubRtlCommitDebugInfo
690 stdcall RtlCommitMemoryStream(ptr long) ChpeAutoRtlCommitMemoryStream
691 stdcall RtlCompactHeap(long long) ChpeAutoRtlCompactHeap
692 stdcall -version=0x600+ RtlCompareAltitudes(ptr ptr) ChpeAutoRtlCompareAltitudes
694 stdcall RtlCompareMemoryUlong(ptr long long) ChpeAutoRtlCompareMemoryUlong
695 stdcall RtlCompareString(ptr ptr long) ChpeAutoRtlCompareString
697 stdcall -version=0x600+ RtlCompareUnicodeStrings(wstr long wstr long long) ChpeAutoRtlCompareUnicodeStrings
698 stdcall -version=0x600+ RtlCompleteProcessCloning(long) ChpeStubRtlCompleteProcessCloning
699 stdcall RtlCompressBuffer(long ptr long ptr long long ptr ptr) ChpeAutoRtlCompressBuffer
700 stdcall RtlComputeCrc32(long ptr long) ChpeAutoRtlComputeCrc32
701 stdcall RtlComputeImportTableHash(ptr ptr long) ChpeAutoRtlComputeImportTableHash
702 stdcall RtlComputePrivatizedDllName_U(ptr ptr ptr) ChpeAutoRtlComputePrivatizedDllName_U
703 stdcall -version=0x600+ RtlConnectToSm(ptr ptr long ptr) ChpeStubRtlConnectToSm
704 stdcall RtlConsoleMultiByteToUnicodeN(ptr long ptr ptr long ptr) ChpeAutoRtlConsoleMultiByteToUnicodeN
705 stdcall RtlConvertExclusiveToShared(ptr) ChpeAutoRtlConvertExclusiveToShared
706 stdcall -version=0x600+ RtlConvertLCIDToString(long long long ptr long) ChpeAutoRtlConvertLCIDToString
707 stdcall RtlConvertSharedToExclusive(ptr) ChpeAutoRtlConvertSharedToExclusive
708 stdcall RtlConvertSidToUnicodeString(ptr ptr long) ChpeAutoRtlConvertSidToUnicodeString
709 stdcall RtlConvertToAutoInheritSecurityObject(ptr ptr ptr ptr long ptr) ChpeAutoRtlConvertToAutoInheritSecurityObject
710 stdcall RtlConvertUiListToApiList(ptr ptr long) ChpeAutoRtlConvertUiListToApiList
711 stdcall RtlCopyLuid(ptr ptr) ChpeAutoRtlCopyLuid
712 stdcall RtlCopyLuidAndAttributesArray(long ptr ptr) ChpeAutoRtlCopyLuidAndAttributesArray
713 stdcall RtlCopyMappedMemory(ptr ptr long) ChpeAutoRtlCopyMappedMemory
714 stdcall -version=0xA00+ RtlCopyContext(ptr long ptr) ChpeAutoRtlCopyContext
715 stdcall -version=0x600+ RtlCopyExtendedContext(ptr long ptr) ChpeAutoRtlCopyExtendedContext
716 cdecl -version=0x600+ RtlCopyMemory(ptr ptr long) ChpeAutoRtlCopyMemory
718 stdcall RtlCopyMemoryStreamTo(ptr ptr int64 ptr ptr) ChpeAutoRtlCopyMemoryStreamTo
719 stdcall RtlCopyOutOfProcessMemoryStreamTo(ptr ptr int64 ptr ptr) ChpeAutoRtlCopyOutOfProcessMemoryStreamTo
720 stdcall RtlCopySecurityDescriptor(ptr ptr) ChpeAutoRtlCopySecurityDescriptor
721 stdcall RtlCopySid(long ptr ptr) ChpeAutoRtlCopySid
722 stdcall RtlCopySidAndAttributesArray(long ptr long ptr ptr ptr ptr) ChpeAutoRtlCopySidAndAttributesArray
723 stdcall RtlCopyString(ptr ptr) ChpeAutoRtlCopyString
725 stdcall RtlCreateAcl(ptr long long) ChpeAutoRtlCreateAcl
726 stdcall RtlCreateActivationContext(long ptr long ptr ptr ptr) ChpeAutoRtlCreateActivationContext
727 stdcall RtlCreateAndSetSD(ptr long ptr ptr ptr) ChpeAutoRtlCreateAndSetSD
728 stdcall RtlContractHashTable(ptr) ChpeAutoRtlContractHashTable
729 stdcall RtlCreateAtomTable(long ptr) ChpeAutoRtlCreateAtomTable
730 stdcall RtlCreateHashTable(ptr long long) ChpeAutoRtlCreateHashTable
731 stdcall RtlCreateBootStatusDataFile() ChpeAutoRtlCreateBootStatusDataFile
732 stdcall -version=0x600+ RtlCreateBoundaryDescriptor(ptr long) ChpeAutoRtlCreateBoundaryDescriptor
733 stdcall RtlCreateEnvironment(long ptr) ChpeAutoRtlCreateEnvironment
734 stdcall -version=0x600+ RtlCreateEnvironmentEx(ptr ptr long) ChpeStubRtlCreateEnvironmentEx
735 stdcall RtlCreateHeap(long ptr long long ptr ptr) ChpeAutoRtlCreateHeap
736 stdcall -version=0x600+ RtlCreateMemoryBlockLookaside(ptr long long long long) ChpeStubRtlCreateMemoryBlockLookaside
737 stdcall -version=0x600+ RtlCreateMemoryZone(ptr long long) ChpeStubRtlCreateMemoryZone
738 stdcall RtlCreateProcessParameters(ptr ptr ptr ptr ptr ptr ptr ptr ptr ptr) ChpeAutoRtlCreateProcessParameters
739 stdcall -version=0x600+ RtlCreateProcessParametersEx(ptr wstr ptr wstr wstr ptr wstr wstr ptr ptr long) ChpeAutoRtlCreateProcessParametersEx
740 stdcall RtlCreateQueryDebugBuffer(long long) ChpeAutoRtlCreateQueryDebugBuffer
741 stdcall RtlCreateRegistryKey(long wstr) ChpeAutoRtlCreateRegistryKey
742 stdcall RtlCreateSecurityDescriptor(ptr long) ChpeAutoRtlCreateSecurityDescriptor
743 stdcall RtlCreateServiceSid(ptr ptr ptr) ChpeAutoRtlCreateServiceSid
744 stdcall -version=0xA00+ RtlDeriveCapabilitySidsFromName(ptr ptr ptr) ChpeAutoRtlDeriveCapabilitySidsFromName
745 stdcall -version=0x602+ RtlIsCapabilitySid(ptr) ChpeAutoRtlIsCapabilitySid
746 stdcall -version=0x602+ RtlGetAppContainerSidType(ptr ptr) ChpeAutoRtlGetAppContainerSidType
747 stdcall -version=0x602+ RtlGetAppContainerParent(ptr ptr) ChpeAutoRtlGetAppContainerParent
748 stdcall -version=0x602+ RtlIsParentOfChildAppContainer(ptr ptr) ChpeAutoRtlIsParentOfChildAppContainer
749 stdcall -version=0x602+ RtlCheckTokenMembershipEx(ptr ptr long ptr) ChpeAutoRtlCheckTokenMembershipEx
750 stdcall RtlCreateSystemVolumeInformationFolder(ptr) ChpeAutoRtlCreateSystemVolumeInformationFolder
751 stdcall RtlCreateTagHeap(ptr long wstr wstr) ChpeAutoRtlCreateTagHeap
752 stdcall RtlCreateTimer(ptr ptr ptr ptr long long long) ChpeAutoRtlCreateTimer
753 stdcall RtlCreateTimerQueue(ptr) ChpeAutoRtlCreateTimerQueue
754 stdcall RtlCreateUnicodeString(ptr wstr) ChpeAutoRtlCreateUnicodeString
755 stdcall RtlCreateUnicodeStringFromAsciiz(ptr str) ChpeAutoRtlCreateUnicodeStringFromAsciiz
756 stdcall RtlCreateUserProcess(ptr long ptr ptr ptr ptr long ptr ptr ptr) ChpeAutoRtlCreateUserProcess
757 stdcall RtlCreateUserSecurityObject(ptr long ptr ptr long ptr ptr) ChpeAutoRtlCreateUserSecurityObject
758 stdcall -version=0x600+ RtlCreateUserStack(long long long long long ptr) ChpeAutoRtlCreateUserStack
760 stdcall -version=0x600+ RtlCultureNameToLCID(ptr ptr) ChpeAutoRtlCultureNameToLCID
761 stdcall RtlCustomCPToUnicodeN(ptr wstr long ptr str long) ChpeAutoRtlCustomCPToUnicodeN
762 stdcall RtlCutoverTimeToSystemTime(ptr ptr ptr long) ChpeAutoRtlCutoverTimeToSystemTime
763 stdcall -version=0x600+ RtlDeCommitDebugInfo(long long long) ChpeStubRtlDeCommitDebugInfo
764 stdcall RtlDeNormalizeProcessParams(ptr) ChpeAutoRtlDeNormalizeProcessParams
765 stdcall RtlDeactivateActivationContext(long long) ChpeAutoRtlDeactivateActivationContext
766 stdcall RtlDeactivateActivationContextUnsafeFast(ptr) ChpeAutoRtlDeactivateActivationContextUnsafeFast
767 stdcall  RtlDebugPrintTimes() ChpeStubRtlDebugPrintTimes
770 stdcall RtlDecompressBuffer(long ptr long ptr long ptr) ChpeAutoRtlDecompressBuffer
772 stdcall RtlDefaultNpAcl(ptr) ChpeAutoRtlDefaultNpAcl
773 stdcall RtlDelete(ptr) ChpeAutoRtlDelete
774 stdcall RtlDeleteAce(ptr long) ChpeAutoRtlDeleteAce
775 stdcall RtlDeleteAtomFromAtomTable(ptr long) ChpeAutoRtlDeleteAtomFromAtomTable
776 stdcall RtlDeleteHashTable(ptr) ChpeAutoRtlDeleteHashTable
777 stdcall RtlDeleteBarrier(ptr) ChpeAutoRtlDeleteBarrier
778 stdcall -version=0x600+ RtlDeleteBoundaryDescriptor(ptr) ChpeAutoRtlDeleteBoundaryDescriptor
780 stdcall RtlDeleteElementGenericTable(ptr ptr) ChpeAutoRtlDeleteElementGenericTable
781 stdcall RtlDeleteElementGenericTableAvl(ptr ptr) ChpeAutoRtlDeleteElementGenericTableAvl
784 stdcall RtlDeleteNoSplay(ptr ptr) ChpeAutoRtlDeleteNoSplay
785 stdcall RtlDeleteRegistryValue(long ptr ptr) ChpeAutoRtlDeleteRegistryValue
786 stdcall RtlDeleteResource(ptr) ChpeAutoRtlDeleteResource
787 stdcall RtlDeleteSecurityObject(ptr) ChpeAutoRtlDeleteSecurityObject
788 stdcall RtlDeleteTimer(ptr ptr ptr) ChpeAutoRtlDeleteTimer
789 stdcall RtlDeleteTimerQueue(ptr) ChpeAutoRtlDeleteTimerQueue
790 stdcall RtlDeleteTimerQueueEx(ptr ptr) ChpeAutoRtlDeleteTimerQueueEx
791 stdcall RtlDeregisterSecureMemoryCacheCallback(ptr) ChpeAutoRtlDeregisterSecureMemoryCacheCallback
792 stdcall RtlDeregisterWait(ptr) ChpeAutoRtlDeregisterWait
793 stdcall RtlDeregisterWaitEx(ptr ptr) ChpeAutoRtlDeregisterWaitEx
794 stdcall RtlDestroyAtomTable(ptr) ChpeAutoRtlDestroyAtomTable
795 stdcall RtlDestroyEnvironment(ptr) ChpeAutoRtlDestroyEnvironment
796 stdcall RtlDestroyHandleTable(ptr) ChpeAutoRtlDestroyHandleTable
797 stdcall RtlDestroyHeap(long) ChpeAutoRtlDestroyHeap
798 stdcall -version=0x600+ RtlDestroyMemoryBlockLookaside(long) ChpeStubRtlDestroyMemoryBlockLookaside
799 stdcall -version=0x600+ RtlDestroyMemoryZone(long) ChpeStubRtlDestroyMemoryZone
800 stdcall RtlDestroyProcessParameters(ptr) ChpeAutoRtlDestroyProcessParameters
801 stdcall RtlDestroyQueryDebugBuffer(ptr) ChpeAutoRtlDestroyQueryDebugBuffer
802 stdcall RtlDetermineDosPathNameType_U(wstr) ChpeAutoRtlDetermineDosPathNameType_U
803 stdcall RtlDllShutdownInProgress() ChpeAutoRtlDllShutdownInProgress
804 stdcall RtlDnsHostNameToComputerName(ptr ptr long) ChpeAutoRtlDnsHostNameToComputerName
805 stdcall RtlDoesFileExists_U(wstr) ChpeAutoRtlDoesFileExists_U
806 stdcall RtlDosApplyFileIsolationRedirection_Ustr(long ptr ptr ptr ptr ptr ptr ptr ptr) ChpeAutoRtlDosApplyFileIsolationRedirection_Ustr
807 stdcall RtlDosPathNameToNtPathName_U(wstr ptr ptr ptr) ChpeAutoRtlDosPathNameToNtPathName_U
808 stdcall RtlDosPathNameToNtPathName_U_WithStatus(wstr ptr ptr ptr) ChpeAutoRtlDosPathNameToNtPathName_U_WithStatus
809 stdcall RtlDosPathNameToRelativeNtPathName_U(ptr ptr ptr ptr) ChpeAutoRtlDosPathNameToRelativeNtPathName_U
810 stdcall RtlDosPathNameToRelativeNtPathName_U_WithStatus(wstr ptr ptr ptr) ChpeAutoRtlDosPathNameToRelativeNtPathName_U_WithStatus
811 stdcall RtlDosSearchPath_U(wstr wstr wstr long ptr ptr) ChpeAutoRtlDosSearchPath_U
812 stdcall RtlDosSearchPath_Ustr(long ptr ptr ptr ptr ptr ptr ptr ptr) ChpeAutoRtlDosSearchPath_Ustr
813 stdcall RtlDowncaseUnicodeChar(long) ChpeAutoRtlDowncaseUnicodeChar
814 stdcall RtlDowncaseUnicodeString(ptr ptr long) ChpeAutoRtlDowncaseUnicodeString
815 stdcall RtlDumpResource(ptr) ChpeAutoRtlDumpResource
816 stdcall RtlDuplicateUnicodeString(long ptr ptr) ChpeAutoRtlDuplicateUnicodeString
817 stdcall RtlEmptyAtomTable(ptr long) ChpeAutoRtlEmptyAtomTable
818 stdcall RtlEndEnumerationHashTable(ptr ptr) ChpeAutoRtlEndEnumerationHashTable
819 stdcall RtlEndWeakEnumerationHashTable(ptr ptr) ChpeAutoRtlEndWeakEnumerationHashTable
820 stdcall  RtlEnableEarlyCriticalSectionEventCreation() ChpeStubRtlEnableEarlyCriticalSectionEventCreation
824 stdcall RtlEnumProcessHeaps(ptr ptr) ChpeAutoRtlEnumProcessHeaps
825 stdcall RtlEnumerateEntryHashTable(ptr ptr) ChpeAutoRtlEnumerateEntryHashTable
826 stdcall RtlEnumerateGenericTable(ptr long) ChpeAutoRtlEnumerateGenericTable
827 stdcall RtlEnumerateGenericTableAvl(ptr long) ChpeAutoRtlEnumerateGenericTableAvl
828 stdcall RtlEnumerateGenericTableLikeADirectory(ptr ptr ptr long ptr ptr ptr) ChpeAutoRtlEnumerateGenericTableLikeADirectory
829 stdcall RtlEnumerateGenericTableWithoutSplaying(ptr ptr) ChpeAutoRtlEnumerateGenericTableWithoutSplaying
830 stdcall RtlEnumerateGenericTableWithoutSplayingAvl(ptr ptr) ChpeAutoRtlEnumerateGenericTableWithoutSplayingAvl
831 stdcall RtlEqualComputerName(ptr ptr) ChpeAutoRtlEqualComputerName
832 stdcall RtlEqualDomainName(ptr ptr) ChpeAutoRtlEqualDomainName
833 stdcall RtlEqualLuid(ptr ptr) ChpeAutoRtlEqualLuid
834 stdcall RtlExpandHashTable(ptr) ChpeAutoRtlExpandHashTable
835 stdcall RtlEqualPrefixSid(ptr ptr) ChpeAutoRtlEqualPrefixSid
836 stdcall RtlEqualSid(long long) ChpeAutoRtlEqualSid
837 stdcall RtlEqualString(ptr ptr long) ChpeAutoRtlEqualString
838 stdcall RtlEqualUnicodeString(ptr ptr long) ChpeAutoRtlEqualUnicodeString
839 stdcall RtlEraseUnicodeString(ptr) ChpeAutoRtlEraseUnicodeString
842 stdcall RtlWow64GetCurrentCpuArea(ptr ptr ptr) ChpeAutoRtlWow64GetCurrentCpuArea
844 stdcall ChpeMarkEcCodeRange(ptr long) ChpeAutoChpeMarkEcCodeRange
845 stdcall ChpeCanContinueToGuest() ChpeAutoChpeCanContinueToGuest
846 stdcall ChpeContinueToGuest(ptr) ChpeAutoChpeContinueToGuest
847 stdcall ChpeContinueToGuestEx(ptr long) ChpeAutoChpeContinueToGuestEx
848 stdcall ProcessPendingCrossProcessEmulatorWork() ChpeAutoProcessPendingCrossProcessEmulatorWork
849 stdcall ChpeIsProcessorFeaturePresent(long) ChpeAutoChpeIsProcessorFeaturePresent
850 stdcall RtlWow64PopAllCrossProcessWorkFromWorkList(ptr ptr) ChpeAutoRtlWow64PopAllCrossProcessWorkFromWorkList
851 stdcall RtlWow64PushCrossProcessWorkOntoFreeList(ptr ptr) ChpeAutoRtlWow64PushCrossProcessWorkOntoFreeList
854 stdcall -version=0x600+ RtlLocateLegacyContext(ptr ptr) ChpeAutoRtlLocateLegacyContext
857 stdcall -version=0x601+ RtlQueryUnbiasedInterruptTime(ptr) ChpeAutoRtlQueryUnbiasedInterruptTime
860 stdcall RtlExitUserProcess(long) ChpeAutoRtlExitUserProcess
862 stdcall -version=0x600+ RtlExpandEnvironmentStrings(long ptr long ptr long ptr) ChpeAutoRtlExpandEnvironmentStrings
863 stdcall RtlExpandEnvironmentStrings_U(ptr ptr ptr ptr) ChpeAutoRtlExpandEnvironmentStrings_U
864 stdcall -version=0x600+ RtlExtendMemoryBlockLookaside(long) ChpeStubRtlExtendMemoryBlockLookaside
865 stdcall -version=0x600+ RtlExtendMemoryZone(long long) ChpeStubRtlExtendMemoryZone
867 stdcall RtlFillMemoryUlong(ptr long long) ChpeAutoRtlFillMemoryUlong
868 stdcall RtlFinalReleaseOutOfProcessMemoryStream(ptr) ChpeAutoRtlFinalReleaseOutOfProcessMemoryStream
869 stdcall -version=0x600+ RtlFindAceByType(long long ptr) ChpeStubRtlFindAceByType
870 stdcall RtlFindActivationContextSectionGuid(long ptr long ptr ptr) ChpeAutoRtlFindActivationContextSectionGuid
871 stdcall RtlFindActivationContextSectionString(long ptr long ptr ptr) ChpeAutoRtlFindActivationContextSectionString
872 stdcall RtlFindCharInUnicodeString(long ptr ptr ptr) ChpeAutoRtlFindCharInUnicodeString
873 stdcall RtlFindClearBits(ptr long long) ChpeAutoRtlFindClearBits
874 stdcall RtlFindClearBitsAndSet(ptr long long) ChpeAutoRtlFindClearBitsAndSet
875 stdcall RtlFindClearRuns(ptr ptr long long) ChpeAutoRtlFindClearRuns
876 stdcall -version=0x600+ RtlFindClosestEncodableLength(long ptr) ChpeStubRtlFindClosestEncodableLength
878 stdcall RtlFindLastBackwardRunClear(ptr long ptr) ChpeAutoRtlFindLastBackwardRunClear
879 stdcall RtlFindLeastSignificantBit(double) ChpeAutoRtlFindLeastSignificantBit
880 stdcall RtlFindLongestRunClear(ptr long) ChpeAutoRtlFindLongestRunClear
881 stdcall RtlFindMessage(long long long long ptr) ChpeAutoRtlFindMessage
882 stdcall RtlFindMostSignificantBit(double) ChpeAutoRtlFindMostSignificantBit
883 stdcall RtlFindNextForwardRunClear(ptr long ptr) ChpeAutoRtlFindNextForwardRunClear
884 stdcall RtlFindSetBits(ptr long long) ChpeAutoRtlFindSetBits
885 stdcall RtlFindSetBitsAndClear(ptr long long) ChpeAutoRtlFindSetBitsAndClear
886 stdcall RtlFirstEntrySList(ptr) ChpeAutoRtlFirstEntrySList
887 stdcall RtlFirstFreeAce(ptr ptr) ChpeAutoRtlFirstFreeAce
892 stdcall RtlFlushSecureMemoryCache(ptr ptr) ChpeAutoRtlFlushSecureMemoryCache
894 stdcall RtlFormatMessage(ptr long long long long ptr ptr long ptr) ChpeAutoRtlFormatMessage
895 stdcall RtlFormatMessageEx(ptr long long long long ptr ptr long ptr long) ChpeAutoRtlFormatMessageEx
896 stdcall RtlFreeActivationContextStack(ptr) ChpeAutoRtlFreeActivationContextStack
897 stdcall RtlFreeAnsiString(long) ChpeAutoRtlFreeAnsiString
898 stdcall RtlFreeHandle(ptr ptr) ChpeAutoRtlFreeHandle
900 stdcall -version=0x600+ RtlFreeMemoryBlockLookaside(long long) ChpeStubRtlFreeMemoryBlockLookaside
901 stdcall RtlFreeOemString(ptr) ChpeAutoRtlFreeOemString
902 stdcall RtlFreeSid(long) ChpeAutoRtlFreeSid
903 stdcall RtlFreeThreadActivationContextStack() ChpeAutoRtlFreeThreadActivationContextStack
905 stdcall -version=0x600+ RtlFreeUserStack(ptr) ChpeAutoRtlFreeUserStack
906 stdcall RtlGUIDFromString(ptr ptr) ChpeAutoRtlGUIDFromString
908 stdcall RtlGetAce(ptr long ptr) ChpeAutoRtlGetAce
909 stdcall RtlGetActiveActivationContext(ptr) ChpeAutoRtlGetActiveActivationContext
910 stdcall RtlGetCallersAddress(ptr ptr) ChpeAutoRtlGetCallersAddress
911 stdcall RtlGetCompressionWorkSpaceSize(long ptr ptr) ChpeAutoRtlGetCompressionWorkSpaceSize
912 stdcall RtlGetControlSecurityDescriptor(ptr ptr ptr) ChpeAutoRtlGetControlSecurityDescriptor
913 stdcall RtlGetCriticalSectionRecursionCount(ptr) ChpeAutoRtlGetCriticalSectionRecursionCount
914 stdcall RtlGetCurrentDirectory_U(long ptr) ChpeAutoRtlGetCurrentDirectory_U
918 stdcall -version=0x600+ RtlGetCurrentTransaction() ChpeAutoRtlGetCurrentTransaction
919 stdcall RtlGetDaclSecurityDescriptor(ptr ptr ptr ptr) ChpeAutoRtlGetDaclSecurityDescriptor
920 stdcall -version=0xA00+ RtlGetDeviceFamilyInfoEnum(ptr ptr ptr) ChpeAutoRtlGetDeviceFamilyInfoEnum
921 stdcall -version=0x600+ RtlGetEnabledExtendedFeatures(int64) ChpeAutoRtlGetEnabledExtendedFeatures
922 stdcall RtlGetElementGenericTable(ptr long) ChpeAutoRtlGetElementGenericTable
923 stdcall RtlGetElementGenericTableAvl(ptr long) ChpeAutoRtlGetElementGenericTableAvl
924 stdcall -version=0x600+ RtlGetExtendedContextLength(long ptr) ChpeAutoRtlGetExtendedContextLength
925 stdcall -version=0x600+ RtlGetExtendedContextLength2(long ptr int64) ChpeAutoRtlGetExtendedContextLength2
926 stdcall -version=0x600+ -ret64 RtlGetExtendedFeaturesMask(ptr) ChpeAutoRtlGetExtendedFeaturesMask
927 stdcall -version=0x600+ RtlGetFileMUIPath(long long ptr ptr long long ptr) ChpeStubRtlGetFileMUIPath
928 stdcall RtlGetFrame() ChpeAutoRtlGetFrame
929 stdcall RtlGetFullPathName_U(wstr long ptr ptr) ChpeAutoRtlGetFullPathName_U
930 stdcall RtlGetFullPathName_UstrEx(ptr ptr ptr ptr ptr ptr ptr ptr) ChpeAutoRtlGetFullPathName_UstrEx
932 stdcall RtlGetGroupSecurityDescriptor(ptr ptr ptr) ChpeAutoRtlGetGroupSecurityDescriptor
933 stdcall -version=0x600+ RtlGetIntegerAtom(wstr ptr) ChpeAutoRtlGetIntegerAtom
936 stdcall -version=0x600+ RtlGetLocaleFileMappingAddress(ptr ptr ptr) ChpeAutoRtlGetLocaleFileMappingAddress
938 stdcall RtlGetLengthWithoutTrailingPathSeperators(long ptr ptr) ChpeAutoRtlGetLengthWithoutTrailingPathSeperators
939 stdcall RtlGetLongestNtPathLength() ChpeAutoRtlGetLongestNtPathLength
941 stdcall RtlGetNtGlobalFlags() ChpeAutoRtlGetNtGlobalFlags
942 stdcall RtlGetNtProductType(ptr) ChpeAutoRtlGetNtProductType
943 stdcall RtlGetNtVersionNumbers(ptr ptr ptr) ChpeAutoRtlGetNtVersionNumbers
944 stdcall RtlGetOwnerSecurityDescriptor(ptr ptr ptr) ChpeAutoRtlGetOwnerSecurityDescriptor
945 stdcall -version=0x600+ RtlGetParentLocaleName(wstr long long long) ChpeStubRtlGetParentLocaleName
946 stdcall -version=0x600+ RtlGetProcessPreferredUILanguages(long ptr ptr ptr) ChpeAutoRtlGetProcessPreferredUILanguages
949 stdcall RtlGetSaclSecurityDescriptor(ptr ptr ptr ptr) ChpeAutoRtlGetSaclSecurityDescriptor
950 stdcall RtlGetSecurityDescriptorRMControl(ptr ptr) ChpeAutoRtlGetSecurityDescriptorRMControl
951 stdcall RtlGetSetBootStatusData(ptr long long ptr long long) ChpeAutoRtlGetSetBootStatusData
952 stdcall -version=0x600+ RtlGetSystemPreferredUILanguages(long long ptr ptr ptr) ChpeAutoRtlGetSystemPreferredUILanguages
953 stdcall RtlGetThreadErrorMode() ChpeAutoRtlGetThreadErrorMode
954 stdcall -version=0x600+ RtlGetThreadLangIdByIndex(long long ptr ptr) ChpeStubRtlGetThreadLangIdByIndex
955 stdcall -version=0x600+ RtlGetThreadPreferredUILanguages(long ptr ptr ptr) ChpeAutoRtlGetThreadPreferredUILanguages
956 stdcall -version=0x600+ RtlGetUILanguageInfo(long ptr long ptr ptr) ChpeStubRtlGetUILanguageInfo
958 stdcall -version=0x600+ RtlGetUnloadEventTraceEx(ptr ptr ptr) ChpeAutoRtlGetUnloadEventTraceEx
959 stdcall RtlGetUserInfoHeap(ptr long ptr ptr ptr) ChpeAutoRtlGetUserInfoHeap
960 stdcall -version=0x600+ RtlGetUserPreferredUILanguages(long long ptr ptr ptr) ChpeAutoRtlGetUserPreferredUILanguages
962 stdcall RtlHashUnicodeString(ptr long long ptr) ChpeAutoRtlHashUnicodeString
963 stdcall -version=0x600+ RtlHeapTrkInitialize(ptr) ChpeStubRtlHeapTrkInitialize
964 stdcall RtlIdentifierAuthoritySid(ptr) ChpeAutoRtlIdentifierAuthoritySid
965 stdcall -version=0x600+ RtlIdnToAscii(long wstr long ptr ptr) ChpeAutoRtlIdnToAscii
966 stdcall -version=0x600+ RtlIdnToNameprepUnicode(long wstr long ptr ptr) ChpeAutoRtlIdnToNameprepUnicode
967 stdcall -version=0x600+ RtlIdnToUnicode(long wstr long ptr ptr) ChpeAutoRtlIdnToUnicode
968 stdcall RtlImageDirectoryEntryToData(ptr long long ptr) ChpeAutoRtlImageDirectoryEntryToData
969 stdcall RtlImageNtHeader(long) ChpeAutoRtlImageNtHeader
970 stdcall RtlImageNtHeaderEx(long ptr double ptr) ChpeAutoRtlImageNtHeaderEx
971 stdcall RtlImageRvaToSection(ptr long long) ChpeAutoRtlImageRvaToSection
972 stdcall RtlImageRvaToVa(ptr long long ptr) ChpeAutoRtlImageRvaToVa
973 stdcall RtlImpersonateSelf(long) ChpeAutoRtlImpersonateSelf
974 stdcall -version=0x600+ RtlImpersonateSelfEx(long long ptr) ChpeStubRtlImpersonateSelfEx
975 stdcall RtlInitAnsiString(ptr str) ChpeAutoRtlInitAnsiString
976 stdcall RtlInitAnsiStringEx(ptr str) ChpeAutoRtlInitAnsiStringEx
977 stdcall -version=0xA00+ RtlInitUTF8String(ptr str) ChpeAutoRtlInitUTF8String
978 stdcall RtlInitBarrier(ptr long long) ChpeAutoRtlInitBarrier
979 stdcall RtlInitCodePageTable(ptr ptr) ChpeAutoRtlInitCodePageTable
981 stdcall RtlInitNlsTables(ptr ptr ptr ptr) ChpeAutoRtlInitNlsTables
983 stdcall RtlInitString(ptr str) ChpeAutoRtlInitString
984 stdcall RtlGetNextEntryHashTable(ptr ptr) ChpeAutoRtlGetNextEntryHashTable
985 stdcall RtlInitEnumerationHashTable(ptr ptr) ChpeAutoRtlInitEnumerationHashTable
987 stdcall RtlInitWeakEnumerationHashTable(ptr ptr) ChpeAutoRtlInitWeakEnumerationHashTable
988 stdcall RtlInitUnicodeStringEx(ptr wstr) ChpeAutoRtlInitUnicodeStringEx
989 stdcall  RtlInitializeAtomPackage(ptr) ChpeStubRtlInitializeAtomPackage
990 stdcall RtlInitializeBitMap(ptr long long) ChpeAutoRtlInitializeBitMap
992 stdcall RtlInitializeContext(ptr ptr ptr ptr ptr) ChpeAutoRtlInitializeContext
994 stdcall RtlInitializeCriticalSectionAndSpinCount(ptr long) ChpeAutoRtlInitializeCriticalSectionAndSpinCount
995 stdcall -version=0x600+ RtlInitializeCriticalSectionEx(ptr long long) ChpeAutoRtlInitializeCriticalSectionEx
996 stdcall -version=0x600+ RtlInitializeExtendedContext(ptr long ptr) ChpeAutoRtlInitializeExtendedContext
997 stdcall -version=0x600+ RtlInitializeExtendedContext2(ptr long ptr int64) ChpeAutoRtlInitializeExtendedContext2
998 stdcall RtlInitializeGenericTable(ptr ptr ptr ptr ptr) ChpeAutoRtlInitializeGenericTable
999 stdcall RtlInitializeGenericTableAvl(ptr ptr ptr ptr ptr) ChpeAutoRtlInitializeGenericTableAvl
1000 stdcall RtlInitializeHandleTable(long long ptr) ChpeAutoRtlInitializeHandleTable
1001 stdcall -version=0x600+ RtlInitializeNtUserPfn(ptr long ptr long ptr long) ChpeAutoRtlInitializeNtUserPfn
1002 stdcall RtlInitializeRXact(ptr long ptr) ChpeAutoRtlInitializeRXact
1003 stdcall RtlInitializeResource(ptr) ChpeAutoRtlInitializeResource
1006 stdcall RtlInitializeSid(ptr ptr long) ChpeAutoRtlInitializeSid
1007 stdcall RtlInsertElementGenericTable(ptr ptr long ptr) ChpeAutoRtlInsertElementGenericTable
1008 stdcall RtlInsertEntryHashTable(ptr ptr long ptr) ChpeAutoRtlInsertEntryHashTable
1009 stdcall RtlInsertElementGenericTableAvl(ptr ptr long ptr) ChpeAutoRtlInsertElementGenericTableAvl
1010 stdcall RtlInsertElementGenericTableFull(ptr ptr long ptr ptr long) ChpeAutoRtlInsertElementGenericTableFull
1011 stdcall RtlInsertElementGenericTableFullAvl(ptr ptr long ptr ptr long) ChpeAutoRtlInsertElementGenericTableFullAvl
1014 stdcall RtlInt64ToUnicodeString(double long ptr) ChpeAutoRtlInt64ToUnicodeString
1015 stdcall RtlIntegerToChar(long long long ptr) ChpeAutoRtlIntegerToChar
1016 stdcall RtlIntegerToUnicodeString(long long ptr) ChpeAutoRtlIntegerToUnicodeString
1022 stdcall -version=0x600+ RtlIoDecodeMemIoResource(ptr ptr ptr ptr) ChpeAutoRtlIoDecodeMemIoResource
1023 stdcall -version=0x600+ RtlIoEncodeMemIoResource(ptr long int64 int64 int64 int64) ChpeAutoRtlIoEncodeMemIoResource
1024 stdcall RtlIpv4AddressToStringA(ptr ptr) ChpeAutoRtlIpv4AddressToStringA
1025 stdcall RtlIpv4AddressToStringExA(ptr long ptr ptr) ChpeAutoRtlIpv4AddressToStringExA
1026 stdcall RtlIpv4AddressToStringExW(ptr long ptr ptr) ChpeAutoRtlIpv4AddressToStringExW
1027 stdcall RtlIpv4AddressToStringW(ptr ptr) ChpeAutoRtlIpv4AddressToStringW
1028 stdcall RtlIpv4StringToAddressA(str long ptr ptr) ChpeAutoRtlIpv4StringToAddressA
1029 stdcall RtlIpv4StringToAddressExA(str long ptr ptr) ChpeAutoRtlIpv4StringToAddressExA
1030 stdcall RtlIpv4StringToAddressExW(wstr long ptr ptr) ChpeAutoRtlIpv4StringToAddressExW
1031 stdcall RtlIpv4StringToAddressW(wstr long ptr ptr) ChpeAutoRtlIpv4StringToAddressW
1032 stdcall RtlIpv6AddressToStringA(ptr ptr) ChpeAutoRtlIpv6AddressToStringA
1033 stdcall RtlIpv6AddressToStringExA(ptr long long ptr ptr) ChpeAutoRtlIpv6AddressToStringExA
1034 stdcall RtlIpv6AddressToStringExW(ptr long long ptr ptr) ChpeAutoRtlIpv6AddressToStringExW
1035 stdcall RtlIpv6AddressToStringW(ptr ptr) ChpeAutoRtlIpv6AddressToStringW
1036 stdcall RtlIpv6StringToAddressA(str ptr ptr) ChpeAutoRtlIpv6StringToAddressA
1037 stdcall RtlIpv6StringToAddressExA(str ptr ptr ptr) ChpeAutoRtlIpv6StringToAddressExA
1038 stdcall RtlIpv6StringToAddressExW(wstr ptr ptr ptr) ChpeAutoRtlIpv6StringToAddressExW
1039 stdcall RtlIpv6StringToAddressW(wstr ptr ptr) ChpeAutoRtlIpv6StringToAddressW
1040 stdcall RtlIsActivationContextActive(ptr) ChpeAutoRtlIsActivationContextActive
1041 stdcall RtlIsCriticalSectionLocked(ptr) ChpeAutoRtlIsCriticalSectionLocked
1042 stdcall RtlIsCriticalSectionLockedByThread(ptr) ChpeAutoRtlIsCriticalSectionLockedByThread
1043 stdcall -version=0x600+ RtlIsCurrentProcess(ptr) ChpeAutoRtlIsCurrentProcess
1044 stdcall -version=0x600+ RtlIsCurrentThreadAttachExempt() ChpeStubRtlIsCurrentThreadAttachExempt
1045 stdcall RtlIsDosDeviceName_U(wstr) ChpeAutoRtlIsDosDeviceName_U
1046 stdcall RtlIsGenericTableEmpty(ptr) ChpeAutoRtlIsGenericTableEmpty
1047 stdcall RtlIsGenericTableEmptyAvl(ptr) ChpeAutoRtlIsGenericTableEmptyAvl
1048 stdcall -version=0x601+ RtlIsNameInExpression(ptr ptr long ptr) ChpeAutoRtlIsNameInExpression
1049 stdcall RtlIsNameLegalDOS8Dot3(ptr ptr ptr) ChpeAutoRtlIsNameLegalDOS8Dot3
1050 stdcall -version=0x600+ RtlIsNormalizedString(long ptr long ptr) ChpeAutoRtlIsNormalizedString
1052 stdcall RtlIsTextUnicode(ptr long ptr) ChpeAutoRtlIsTextUnicode
1053 stdcall RtlIsThreadWithinLoaderCallout() ChpeAutoRtlIsThreadWithinLoaderCallout
1054 stdcall RtlIsValidHandle(ptr ptr) ChpeAutoRtlIsValidHandle
1055 stdcall RtlIsValidIndexHandle(ptr long ptr) ChpeAutoRtlIsValidIndexHandle
1056 stdcall -version=0x600+ RtlIsValidLocaleName(wstr long) ChpeAutoRtlIsValidLocaleName
1057 stdcall -version=0x600+ RtlLCIDToCultureName(long ptr) ChpeAutoRtlLCIDToCultureName
1058 stdcall RtlLargeIntegerToChar(ptr long long ptr) ChpeAutoRtlLargeIntegerToChar
1059 stdcall -version=0x600+ RtlLcidToLocaleName(long ptr long long) ChpeAutoRtlLcidToLocaleName
1061 stdcall RtlLengthRequiredSid(long) ChpeAutoRtlLengthRequiredSid
1062 stdcall RtlLengthSecurityDescriptor(ptr) ChpeAutoRtlLengthSecurityDescriptor
1063 stdcall RtlLengthSid(ptr) ChpeAutoRtlLengthSid
1064 stdcall RtlLocalTimeToSystemTime(ptr ptr) ChpeAutoRtlLocalTimeToSystemTime
1065 stdcall -version=0x600+ RtlLocaleNameToLcid(wstr ptr long) ChpeAutoRtlLocaleNameToLcid
1066 stdcall RtlLockBootStatusData(ptr) ChpeAutoRtlLockBootStatusData
1067 stdcall -version=0x600+ RtlLockCurrentThread() ChpeStubRtlLockCurrentThread
1068 stdcall RtlLockHeap(long) ChpeAutoRtlLockHeap
1069 stdcall -version=0x600+ RtlLockMemoryBlockLookaside(long) ChpeStubRtlLockMemoryBlockLookaside
1070 stdcall RtlLockMemoryStreamRegion(ptr int64 int64 long) ChpeAutoRtlLockMemoryStreamRegion
1071 stdcall -version=0x600+ RtlLockMemoryZone(long) ChpeStubRtlLockMemoryZone
1072 stdcall -version=0x600+ RtlLockModuleSection(long) ChpeStubRtlLockModuleSection
1074 stdcall  RtlLogStackBackTrace() ChpeStubRtlLogStackBackTrace
1075 stdcall RtlLookupAtomInAtomTable(ptr wstr ptr) ChpeAutoRtlLookupAtomInAtomTable
1076 stdcall RtlLookupElementGenericTable(ptr ptr) ChpeAutoRtlLookupElementGenericTable
1077 stdcall RtlLookupEntryHashTable(ptr long ptr) ChpeAutoRtlLookupEntryHashTable
1078 stdcall RtlRemoveEntryHashTable(ptr ptr ptr) ChpeAutoRtlRemoveEntryHashTable
1079 stdcall RtlLookupElementGenericTableAvl(ptr ptr) ChpeAutoRtlLookupElementGenericTableAvl
1080 stdcall RtlLookupElementGenericTableFull(ptr ptr ptr long) ChpeAutoRtlLookupElementGenericTableFull
1081 stdcall RtlLookupElementGenericTableFullAvl(ptr ptr ptr long) ChpeAutoRtlLookupElementGenericTableFullAvl
1084 stdcall RtlMakeSelfRelativeSD(ptr ptr ptr) ChpeAutoRtlMakeSelfRelativeSD
1085 stdcall RtlMapGenericMask(long ptr) ChpeAutoRtlMapGenericMask
1086 stdcall RtlMapSecurityErrorToNtStatus(long) ChpeAutoRtlMapSecurityErrorToNtStatus
1088 stdcall RtlMultiAppendUnicodeStringBuffer(ptr long ptr) ChpeAutoRtlMultiAppendUnicodeStringBuffer
1089 stdcall RtlMultiByteToUnicodeN(ptr long ptr ptr long) ChpeAutoRtlMultiByteToUnicodeN
1090 stdcall RtlMultiByteToUnicodeSize(ptr str long) ChpeAutoRtlMultiByteToUnicodeSize
1091 stdcall RtlMultipleAllocateHeap(ptr long ptr long ptr) ChpeAutoRtlMultipleAllocateHeap
1092 stdcall RtlMultipleFreeHeap(ptr long long ptr) ChpeAutoRtlMultipleFreeHeap
1093 stdcall RtlNewInstanceSecurityObject(long long ptr ptr ptr ptr ptr long ptr ptr) ChpeAutoRtlNewInstanceSecurityObject
1094 stdcall RtlNewSecurityGrantedAccess(long ptr ptr ptr ptr ptr) ChpeAutoRtlNewSecurityGrantedAccess
1095 stdcall RtlNewSecurityObject(ptr ptr ptr long ptr ptr) ChpeAutoRtlNewSecurityObject
1096 stdcall RtlNewSecurityObjectEx(ptr ptr ptr ptr long long ptr ptr) ChpeAutoRtlNewSecurityObjectEx
1097 stdcall RtlNewSecurityObjectWithMultipleInheritance(ptr ptr ptr ptr long long long ptr ptr) ChpeAutoRtlNewSecurityObjectWithMultipleInheritance
1098 stdcall RtlNormalizeProcessParams(ptr) ChpeAutoRtlNormalizeProcessParams
1099 stdcall -version=0x600+ RtlNormalizeString(long ptr long ptr ptr) ChpeAutoRtlNormalizeString
1100 stdcall RtlNtPathNameToDosPathName(long ptr ptr ptr) ChpeAutoRtlNtPathNameToDosPathName
1102 stdcall RtlNtStatusToDosErrorNoTeb(long) ChpeAutoRtlNtStatusToDosErrorNoTeb
1103 stub -version=0x600+ RtlNtdllName
1104 stdcall RtlNumberGenericTableElements(ptr) ChpeAutoRtlNumberGenericTableElements
1105 stdcall RtlNumberGenericTableElementsAvl(ptr) ChpeAutoRtlNumberGenericTableElementsAvl
1106 stdcall RtlNumberOfClearBits(ptr) ChpeAutoRtlNumberOfClearBits
1107 stdcall RtlNumberOfSetBits(ptr) ChpeAutoRtlNumberOfSetBits
1108 stdcall -version=0x600+ RtlNumberOfSetBitsUlongPtr(long) ChpeAutoRtlNumberOfSetBitsUlongPtr
1109 stdcall RtlOemStringToUnicodeSize(ptr) ChpeAutoRtlOemStringToUnicodeSize
1110 stdcall RtlOemStringToUnicodeString(ptr ptr long) ChpeAutoRtlOemStringToUnicodeString
1111 stdcall RtlOemToUnicodeN(ptr long ptr ptr long) ChpeAutoRtlOemToUnicodeN
1112 stdcall RtlOpenCurrentUser(long ptr) ChpeAutoRtlOpenCurrentUser
1113 stdcall -version=0x600+ RtlOwnerAcesPresent(long) ChpeStubRtlOwnerAcesPresent
1115 stdcall RtlPinAtomInAtomTable(ptr long) ChpeAutoRtlPinAtomInAtomTable
1116 stdcall RtlPopFrame(ptr) ChpeAutoRtlPopFrame
1117 stdcall RtlPrefixString(ptr ptr long) ChpeAutoRtlPrefixString
1118 stdcall RtlPrefixUnicodeString(ptr ptr long) ChpeAutoRtlPrefixUnicodeString
1119 stdcall -version=0x600+ RtlPrepareForProcessCloning() ChpeStubRtlPrepareForProcessCloning
1120 stdcall -version=0x600+ RtlProcessFlsData(ptr long) ChpeAutoRtlProcessFlsData
1121 stdcall RtlProtectHeap(ptr long) ChpeAutoRtlProtectHeap
1122 stdcall RtlPushFrame(ptr) ChpeAutoRtlPushFrame
1123 stdcall -version=0x600+ RtlQueryActivationContextApplicationSettings(long ptr wstr wstr ptr ptr ptr) ChpeAutoRtlQueryActivationContextApplicationSettings
1124 stdcall RtlQueryAtomInAtomTable(ptr long ptr ptr ptr ptr) ChpeAutoRtlQueryAtomInAtomTable
1125 stdcall -version=0x600+ RtlQueryCriticalSectionOwner(ptr) ChpeStubRtlQueryCriticalSectionOwner
1127 stdcall -version=0x600+ RtlQueryDynamicTimeZoneInformation(ptr) ChpeAutoRtlQueryDynamicTimeZoneInformation
1128 stdcall -version=0x600+ RtlQueryElevationFlags(ptr) ChpeStubRtlQueryElevationFlags
1130 stdcall RtlQueryEnvironmentVariable_U(ptr ptr ptr) ChpeAutoRtlQueryEnvironmentVariable_U
1131 stdcall RtlQueryHeapInformation(long long ptr long ptr) ChpeAutoRtlQueryHeapInformation
1132 stdcall RtlQueryInformationAcl(ptr ptr long long) ChpeAutoRtlQueryInformationAcl
1133 stdcall RtlQueryInformationActivationContext(long long ptr long ptr long ptr) ChpeAutoRtlQueryInformationActivationContext
1134 stdcall RtlQueryInformationActiveActivationContext(long ptr long ptr) ChpeAutoRtlQueryInformationActiveActivationContext
1135 stdcall RtlQueryInterfaceMemoryStream(ptr ptr ptr) ChpeAutoRtlQueryInterfaceMemoryStream
1136 stdcall -version=0x600+ RtlQueryModuleInformation(ptr long ptr) ChpeStubRtlQueryModuleInformation
1137 stdcall  RtlQueryProcessBackTraceInformation(ptr) ChpeStubRtlQueryProcessBackTraceInformation
1138 stdcall RtlQueryProcessDebugInformation(long long ptr) ChpeAutoRtlQueryProcessDebugInformation
1139 stdcall RtlQueryProcessHeapInformation(ptr) ChpeAutoRtlQueryProcessHeapInformation
1140 stdcall  RtlQueryProcessLockInformation(ptr) ChpeStubRtlQueryProcessLockInformation
1141 stdcall RtlQueryRegistryValues(long ptr ptr ptr ptr) ChpeAutoRtlQueryRegistryValues
1142 stdcall RtlQueryRegistryValuesEx(long ptr ptr ptr ptr) ChpeAutoRtlQueryRegistryValuesEx
1143 stdcall RtlQuerySecurityObject(ptr long ptr long ptr) ChpeAutoRtlQuerySecurityObject
1144 stdcall RtlQueryTagHeap(ptr long long long ptr) ChpeAutoRtlQueryTagHeap
1145 stdcall RtlQueryTimeZoneInformation(ptr) ChpeAutoRtlQueryTimeZoneInformation
1146 stdcall RtlQueueApcWow64Thread(ptr ptr ptr ptr ptr) ChpeAutoRtlQueueApcWow64Thread
1151 stdcall RtlRandomEx(ptr) ChpeAutoRtlRandomEx
1153 stdcall RtlReadMemoryStream(ptr ptr long ptr) ChpeAutoRtlReadMemoryStream
1154 stdcall RtlReadOutOfProcessMemoryStream(ptr ptr long ptr) ChpeAutoRtlReadOutOfProcessMemoryStream
1155 stdcall RtlRealPredecessor(ptr) ChpeAutoRtlRealPredecessor
1156 stdcall RtlRealSuccessor(ptr) ChpeAutoRtlRealSuccessor
1157 stdcall RtlRegisterSecureMemoryCacheCallback(ptr) ChpeAutoRtlRegisterSecureMemoryCacheCallback
1158 stdcall RtlRegisterCfgTargetRange(ptr long) ChpeAutoRtlRegisterCfgTargetRange
1159 stdcall -version=0x600+ RtlRegisterThreadWithCsrss() ChpeStubRtlRegisterThreadWithCsrss
1161 stdcall RtlReleaseActivationContext(ptr) ChpeAutoRtlReleaseActivationContext
1163 stdcall RtlReleasePebLock() ChpeAutoRtlReleasePebLock
1165 stdcall RtlReleaseRelativeName(ptr) ChpeAutoRtlReleaseRelativeName
1166 stdcall RtlReleaseResource(ptr) ChpeAutoRtlReleaseResource
1169 stdcall RtlRemoteCall(ptr ptr ptr long ptr long long) ChpeAutoRtlRemoteCall
1170 stdcall -version=0x600+ RtlRemovePrivileges(ptr ptr long) ChpeAutoRtlRemovePrivileges
1173 stdcall -version=0x600+ RtlReportException(long long long) ChpeStubRtlReportException
1174 stdcall -version=0x600+ RtlResetMemoryBlockLookaside(long) ChpeStubRtlResetMemoryBlockLookaside
1175 stdcall -version=0x600+ RtlResetMemoryZone(long) ChpeStubRtlResetMemoryZone
1176 stdcall -version=0x600+ RtlResetNtUserPfn() ChpeAutoRtlResetNtUserPfn
1177 stdcall RtlResetRtlTranslations(ptr) ChpeAutoRtlResetRtlTranslations
1180 stdcall -version=0x600+ RtlRetrieveNtUserPfn(ptr ptr ptr) ChpeAutoRtlRetrieveNtUserPfn
1181 stdcall RtlRevertMemoryStream(ptr) ChpeAutoRtlRevertMemoryStream
1182 stdcall RtlRunDecodeUnicodeString(long ptr) ChpeAutoRtlRunDecodeUnicodeString
1183 stdcall RtlRunEncodeUnicodeString(long ptr) ChpeAutoRtlRunEncodeUnicodeString
1184 stdcall -version=0x600+ RtlRunOnceBeginInitialize(ptr long ptr) ChpeAutoRtlRunOnceBeginInitialize
1185 stdcall -version=0x600+ RtlRunOnceComplete(ptr long ptr) ChpeAutoRtlRunOnceComplete
1188 stdcall RtlSecondsSince1970ToTime(long ptr) ChpeAutoRtlSecondsSince1970ToTime
1189 stdcall RtlSecondsSince1980ToTime(long ptr) ChpeAutoRtlSecondsSince1980ToTime
1190 stdcall RtlSeekMemoryStream(ptr int64 long ptr) ChpeAutoRtlSeekMemoryStream
1191 stdcall RtlSelfRelativeToAbsoluteSD2(ptr ptr) ChpeAutoRtlSelfRelativeToAbsoluteSD2
1192 stdcall RtlSelfRelativeToAbsoluteSD(ptr ptr ptr ptr ptr ptr ptr ptr ptr ptr ptr) ChpeAutoRtlSelfRelativeToAbsoluteSD
1193 stdcall -version=0x600+ RtlSendMsgToSm(ptr ptr) ChpeStubRtlSendMsgToSm
1194 stdcall RtlSetAllBits(ptr) ChpeAutoRtlSetAllBits
1195 stdcall RtlSetAttributesSecurityDescriptor(ptr long ptr) ChpeAutoRtlSetAttributesSecurityDescriptor
1196 stdcall RtlSetBits(ptr long long) ChpeAutoRtlSetBits
1197 stdcall RtlSetCfgTargetValidity(ptr long long ptr) ChpeAutoRtlSetCfgTargetValidity
1198 stdcall RtlSetControlSecurityDescriptor(ptr long long) ChpeAutoRtlSetControlSecurityDescriptor
1200 stdcall RtlSetCurrentDirectory_U(ptr) ChpeAutoRtlSetCurrentDirectory_U
1201 stdcall RtlSetCurrentEnvironment(wstr ptr) ChpeAutoRtlSetCurrentEnvironment
1202 stdcall -version=0x600+ RtlSetCurrentTransaction(ptr) ChpeAutoRtlSetCurrentTransaction
1203 stdcall RtlSetDaclSecurityDescriptor(ptr long ptr long) ChpeAutoRtlSetDaclSecurityDescriptor
1204 stdcall -version=0x600+ RtlSetDynamicTimeZoneInformation(long) ChpeStubRtlSetDynamicTimeZoneInformation
1205 stdcall RtlSetEnvironmentStrings(wstr long) ChpeAutoRtlSetEnvironmentStrings
1206 stdcall -version=0x600+ RtlSetEnvironmentVar(ptr ptr long ptr long) ChpeStubRtlSetEnvironmentVar
1207 stdcall RtlSetEnvironmentVariable(ptr ptr ptr) ChpeAutoRtlSetEnvironmentVariable
1208 stdcall -version=0x600+ RtlSetExtendedFeaturesMask(ptr int64) ChpeAutoRtlSetExtendedFeaturesMask
1209 stdcall RtlSetGroupSecurityDescriptor(ptr ptr long) ChpeAutoRtlSetGroupSecurityDescriptor
1210 stdcall RtlSetHeapInformation(ptr long ptr ptr) ChpeAutoRtlSetHeapInformation
1211 stdcall RtlSetInformationAcl(ptr ptr long long) ChpeAutoRtlSetInformationAcl
1212 stdcall RtlSetIoCompletionCallback(long ptr long) ChpeAutoRtlSetIoCompletionCallback
1214 stdcall RtlSetLastWin32ErrorAndNtStatusFromNtStatus(long) ChpeAutoRtlSetLastWin32ErrorAndNtStatusFromNtStatus
1215 stdcall RtlSetMemoryStreamSize(ptr int64) ChpeAutoRtlSetMemoryStreamSize
1216 stdcall RtlSetOwnerSecurityDescriptor(ptr ptr long) ChpeAutoRtlSetOwnerSecurityDescriptor
1217 stdcall -version=0x602+ RtlSetPortableOperatingSystem(long) ChpeAutoRtlSetPortableOperatingSystem
1218 stdcall -version=0x600+ RtlSetProcessDebugInformation(ptr long ptr) ChpeStubRtlSetProcessDebugInformation
1219 stdcall -version=0x600+ RtlSetProcessPreferredUILanguages(long ptr ptr) ChpeAutoRtlSetProcessPreferredUILanguages
1220 cdecl RtlSetProcessIsCritical(long ptr long) ChpeAutoRtlSetProcessIsCritical
1221 stdcall RtlSetSaclSecurityDescriptor(ptr long ptr long) ChpeAutoRtlSetSaclSecurityDescriptor
1222 stdcall RtlSetSecurityDescriptorRMControl(ptr ptr) ChpeAutoRtlSetSecurityDescriptorRMControl
1223 stdcall RtlSetSecurityObject(long ptr ptr ptr ptr) ChpeAutoRtlSetSecurityObject
1224 stdcall RtlSetSecurityObjectEx(long ptr ptr long ptr ptr) ChpeAutoRtlSetSecurityObjectEx
1225 stdcall RtlSetThreadErrorMode(long ptr) ChpeAutoRtlSetThreadErrorMode
1226 cdecl RtlSetThreadIsCritical(long ptr long) ChpeAutoRtlSetThreadIsCritical
1227 stdcall RtlSetThreadPoolStartFunc(ptr ptr) ChpeAutoRtlSetThreadPoolStartFunc
1228 stdcall -version=0x600+ RtlSetThreadPreferredUILanguages(long ptr ptr) ChpeAutoRtlSetThreadPreferredUILanguages
1229 stdcall RtlSetTimeZoneInformation(ptr) ChpeAutoRtlSetTimeZoneInformation
1230 stdcall RtlSetTimer(ptr ptr ptr ptr long long long) ChpeAutoRtlSetTimer
1231 stdcall RtlSetUnhandledExceptionFilter(ptr) ChpeAutoRtlSetUnhandledExceptionFilter
1232 stdcall RtlSetUserFlagsHeap(ptr long ptr long long) ChpeAutoRtlSetUserFlagsHeap
1233 stdcall RtlSetUserValueHeap(ptr long ptr ptr) ChpeAutoRtlSetUserValueHeap
1234 stdcall -version=0x600+ RtlSidDominates(long long ptr) ChpeStubRtlSidDominates
1235 stdcall -version=0x600+ RtlSidEqualLevel(long long ptr) ChpeStubRtlSidEqualLevel
1236 stdcall -version=0x600+ RtlSidHashInitialize(ptr long ptr) ChpeStubRtlSidHashInitialize
1237 stdcall -version=0x600+ RtlSidHashLookup(long ptr) ChpeStubRtlSidHashLookup
1238 stdcall -version=0x600+ RtlSidIsHigherLevel(long long ptr) ChpeStubRtlSidIsHigherLevel
1240 stdcall -version=0x600+ RtlSleepConditionVariableCS(ptr ptr ptr) ChpeAutoRtlSleepConditionVariableCS
1241 stdcall -version=0x600+ RtlSleepConditionVariableSRW(ptr ptr ptr long) ChpeAutoRtlSleepConditionVariableSRW
1242 stdcall RtlSplay(ptr) ChpeAutoRtlSplay
1243 stdcall RtlStartRXact(ptr) ChpeAutoRtlStartRXact
1244 stdcall RtlStatMemoryStream(ptr ptr long) ChpeAutoRtlStatMemoryStream
1245 stdcall RtlStringFromGUID(ptr ptr) ChpeAutoRtlStringFromGUID
1246 stdcall RtlSubAuthorityCountSid(ptr) ChpeAutoRtlSubAuthorityCountSid
1247 stdcall RtlSubAuthoritySid(ptr long) ChpeAutoRtlSubAuthoritySid
1248 stdcall RtlSubtreePredecessor(ptr) ChpeAutoRtlSubtreePredecessor
1249 stdcall RtlSubtreeSuccessor(ptr) ChpeAutoRtlSubtreeSuccessor
1250 stdcall RtlSystemTimeToLocalTime(ptr ptr) ChpeAutoRtlSystemTimeToLocalTime
1251 stdcall -version=0x600+ RtlTestBit(ptr long) ChpeAutoRtlTestBit
1252 stdcall RtlTimeFieldsToTime(ptr ptr) ChpeAutoRtlTimeFieldsToTime
1253 stdcall RtlTimeToElapsedTimeFields(long long) ChpeAutoRtlTimeToElapsedTimeFields
1254 stdcall RtlTimeToSecondsSince1970(ptr ptr) ChpeAutoRtlTimeToSecondsSince1970
1255 stdcall RtlTimeToSecondsSince1980(ptr ptr) ChpeAutoRtlTimeToSecondsSince1980
1256 stdcall RtlTimeToTimeFields(long long) ChpeAutoRtlTimeToTimeFields
1257 stdcall RtlTraceDatabaseAdd(ptr long ptr ptr) ChpeAutoRtlTraceDatabaseAdd
1258 stdcall RtlTraceDatabaseCreate(long ptr long long ptr) ChpeAutoRtlTraceDatabaseCreate
1259 stdcall RtlTraceDatabaseDestroy(ptr) ChpeAutoRtlTraceDatabaseDestroy
1260 stdcall RtlTraceDatabaseEnumerate(ptr ptr ptr) ChpeAutoRtlTraceDatabaseEnumerate
1261 stdcall RtlTraceDatabaseFind(ptr long ptr ptr) ChpeAutoRtlTraceDatabaseFind
1262 stdcall RtlTraceDatabaseLock(ptr) ChpeAutoRtlTraceDatabaseLock
1263 stdcall RtlTraceDatabaseUnlock(ptr) ChpeAutoRtlTraceDatabaseUnlock
1264 stdcall RtlTraceDatabaseValidate(ptr) ChpeAutoRtlTraceDatabaseValidate
1265 stdcall RtlTryAcquirePebLock() ChpeAutoRtlTryAcquirePebLock
1269 stdcall RtlUnhandledExceptionFilter2(ptr long) ChpeAutoRtlUnhandledExceptionFilter2
1270 stdcall RtlUnhandledExceptionFilter(ptr) ChpeAutoRtlUnhandledExceptionFilter
1271 stdcall RtlUnicodeStringToAnsiSize(ptr) ChpeAutoRtlUnicodeStringToAnsiSize
1272 stdcall -version=0x601+ RtlUnicodeToUTF8N(ptr long ptr wstr long) ChpeAutoRtlUnicodeToUTF8N
1273 stdcall -version=0xA00+ RtlUTF8StringToUnicodeString(ptr ptr long) ChpeAutoRtlUTF8StringToUnicodeString
1275 stdcall RtlUnicodeStringToAnsiString(ptr ptr long) ChpeAutoRtlUnicodeStringToAnsiString
1276 stdcall RtlUnicodeStringToCountedOemString(ptr ptr long) ChpeAutoRtlUnicodeStringToCountedOemString
1277 stdcall RtlUnicodeStringToInteger(ptr long ptr) ChpeAutoRtlUnicodeStringToInteger
1278 stdcall RtlUnicodeStringToOemSize(ptr) ChpeAutoRtlUnicodeStringToOemSize
1279 stdcall RtlUnicodeStringToOemString(ptr ptr long) ChpeAutoRtlUnicodeStringToOemString
1280 stdcall RtlUnicodeToCustomCPN(ptr ptr long ptr wstr long) ChpeAutoRtlUnicodeToCustomCPN
1281 stdcall RtlUnicodeToMultiByteN(ptr long ptr ptr long) ChpeAutoRtlUnicodeToMultiByteN
1282 stdcall RtlUnicodeToMultiByteSize(ptr ptr long) ChpeAutoRtlUnicodeToMultiByteSize
1283 stdcall RtlUnicodeToOemN(ptr long ptr ptr long) ChpeAutoRtlUnicodeToOemN
1284 stdcall RtlUniform(ptr) ChpeAutoRtlUniform
1285 stdcall RtlUnlockBootStatusData(ptr) ChpeAutoRtlUnlockBootStatusData
1286 stdcall -version=0x600+ RtlUnlockCurrentThread() ChpeStubRtlUnlockCurrentThread
1287 stdcall RtlUnlockHeap(long) ChpeAutoRtlUnlockHeap
1288 stdcall -version=0x600+ RtlUnlockMemoryBlockLookaside(long) ChpeStubRtlUnlockMemoryBlockLookaside
1289 stdcall RtlUnlockMemoryStreamRegion(ptr int64 int64 long) ChpeAutoRtlUnlockMemoryStreamRegion
1290 stdcall -version=0x600+ RtlUnlockMemoryZone(long) ChpeStubRtlUnlockMemoryZone
1291 stdcall -version=0x600+ RtlUnlockModuleSection(long) ChpeStubRtlUnlockModuleSection
1294 stdcall RtlUnregisterCfgTargetRange(ptr) ChpeAutoRtlUnregisterCfgTargetRange
1295 stdcall RtlUpcaseUnicodeChar(long) ChpeAutoRtlUpcaseUnicodeChar
1296 stdcall RtlUpcaseUnicodeString(ptr ptr long) ChpeAutoRtlUpcaseUnicodeString
1297 stdcall RtlUpcaseUnicodeStringToAnsiString(ptr ptr long) ChpeAutoRtlUpcaseUnicodeStringToAnsiString
1298 stdcall RtlUpcaseUnicodeStringToCountedOemString(ptr ptr long) ChpeAutoRtlUpcaseUnicodeStringToCountedOemString
1299 stdcall RtlUpcaseUnicodeStringToOemString(ptr ptr long) ChpeAutoRtlUpcaseUnicodeStringToOemString
1300 stdcall RtlUpcaseUnicodeToCustomCPN(ptr ptr long ptr wstr long) ChpeAutoRtlUpcaseUnicodeToCustomCPN
1301 stdcall RtlUpcaseUnicodeToMultiByteN(ptr long ptr ptr long) ChpeAutoRtlUpcaseUnicodeToMultiByteN
1302 stdcall RtlUpcaseUnicodeToOemN(ptr long ptr ptr long) ChpeAutoRtlUpcaseUnicodeToOemN
1303 stdcall -version=0x600+ RtlUpdateClonedCriticalSection(long) ChpeStubRtlUpdateClonedCriticalSection
1304 stdcall -version=0x600+ RtlUpdateClonedSRWLock(ptr long) ChpeStubRtlUpdateClonedSRWLock
1305 stdcall RtlUpdateTimer(ptr ptr long long) ChpeAutoRtlUpdateTimer
1306 stdcall RtlUpperChar(long) ChpeAutoRtlUpperChar
1307 stdcall RtlUpperString(ptr ptr) ChpeAutoRtlUpperString
1309 stdcall RtlValidAcl(ptr) ChpeAutoRtlValidAcl
1310 stdcall RtlValidRelativeSecurityDescriptor(ptr long long) ChpeAutoRtlValidRelativeSecurityDescriptor
1311 stdcall RtlValidSecurityDescriptor(ptr) ChpeAutoRtlValidSecurityDescriptor
1312 stdcall RtlValidSid(ptr) ChpeAutoRtlValidSid
1313 stdcall RtlValidateHeap(long long ptr) ChpeAutoRtlValidateHeap
1314 stdcall RtlValidateProcessHeaps() ChpeAutoRtlValidateProcessHeaps
1315 stdcall RtlValidateUnicodeString(long ptr) ChpeAutoRtlValidateUnicodeString
1316 stdcall RtlVerifyVersionInfo(ptr long double) ChpeAutoRtlVerifyVersionInfo
1318 stdcall RtlVirtualUnwind2(long int64 int64 ptr ptr ptr ptr ptr ptr ptr ptr ptr long) ChpeAutoRtlVirtualUnwind2
1324 stdcall RtlWalkFrameChain(ptr long long) ChpeAutoRtlWalkFrameChain
1325 stdcall RtlWeaklyEnumerateEntryHashTable(ptr ptr) ChpeAutoRtlWeaklyEnumerateEntryHashTable
1326 stdcall RtlWalkHeap(long ptr) ChpeAutoRtlWalkHeap
1327 stdcall -version=0x600+ RtlWerpReportException(long long ptr long long ptr) ChpeStubRtlWerpReportException
1328 stdcall -version=0x600+ RtlWow64CallFunction64() ChpeStubRtlWow64CallFunction64
1329 stdcall RtlWow64EnableFsRedirection(long) ChpeAutoRtlWow64EnableFsRedirection
1330 stdcall RtlWow64EnableFsRedirectionEx(long ptr) ChpeAutoRtlWow64EnableFsRedirectionEx
1331 stdcall -version=0x600+ RtlOpenCrossProcessEmulatorWorkConnection(ptr ptr ptr) ChpeAutoRtlOpenCrossProcessEmulatorWorkConnection
1332 stdcall -version=0x600+ RtlWow64GetCpuAreaInfo(ptr long ptr) ChpeAutoRtlWow64GetCpuAreaInfo
1333 stdcall -version=0x600+ RtlWow64GetCurrentMachine() ChpeAutoRtlWow64GetCurrentMachine
1334 stdcall -version=0x600+ RtlWow64GetProcessMachines(ptr ptr ptr) ChpeAutoRtlWow64GetProcessMachines
1335 stdcall -version=0x600+ RtlWow64GetSharedInfoProcess(ptr ptr ptr) ChpeAutoRtlWow64GetSharedInfoProcess
1336 stdcall -version=0x600+ RtlWow64GetThreadContext(ptr ptr) ChpeAutoRtlWow64GetThreadContext
1338 stdcall -version=0x600+ RtlWow64IsWowGuestMachineSupported(long ptr) ChpeAutoRtlWow64IsWowGuestMachineSupported
1339 stdcall -version=0x600+ RtlWow64LogMessageInEventLogger(long long long) ChpeStubRtlWow64LogMessageInEventLogger
1340 stdcall -version=0x600+ RtlWow64PopCrossProcessWorkFromFreeList(ptr) ChpeAutoRtlWow64PopCrossProcessWorkFromFreeList
1341 stdcall -version=0x600+ RtlWow64PushCrossProcessWorkOntoWorkList(ptr ptr ptr) ChpeAutoRtlWow64PushCrossProcessWorkOntoWorkList
1342 stdcall -version=0x600+ RtlWow64RequestCrossProcessHeavyFlush(ptr) ChpeAutoRtlWow64RequestCrossProcessHeavyFlush
1343 stdcall -version=0x600+ RtlWow64SetThreadContext(ptr ptr) ChpeAutoRtlWow64SetThreadContext
1344 stdcall -version=0x600+ RtlWow64SuspendThread(ptr ptr) ChpeAutoRtlWow64SuspendThread
1345 stdcall RtlWriteMemoryStream(ptr ptr long ptr) ChpeAutoRtlWriteMemoryStream
1346 stdcall RtlWriteRegistryValue(long ptr ptr long ptr long) ChpeAutoRtlWriteRegistryValue
1347 stdcall RtlZeroHeap(ptr long) ChpeAutoRtlZeroHeap
1349 stdcall RtlZombifyActivationContext(ptr) ChpeAutoRtlZombifyActivationContext
1351 stdcall -version=0x600+ RtlpCheckDynamicTimeZoneInformation(ptr long) ChpeStubRtlpCheckDynamicTimeZoneInformation
1352 stdcall -version=0x600+ RtlpCleanupRegistryKeys() ChpeStubRtlpCleanupRegistryKeys
1353 stdcall -version=0x600+ RtlpConvertCultureNamesToLCIDs(wstr ptr) ChpeStubRtlpConvertCultureNamesToLCIDs
1354 stdcall -version=0x600+ RtlpConvertLCIDsToCultureNames(wstr ptr) ChpeStubRtlpConvertLCIDsToCultureNames
1355 stdcall -version=0x600+ RtlpCreateProcessRegistryInfo(ptr) ChpeStubRtlpCreateProcessRegistryInfo
1356 stdcall RtlpEnsureBufferSize(long ptr long) ChpeAutoRtlpEnsureBufferSize
1357 stdcall -version=0x600+ RtlpGetLCIDFromLangInfoNode(long long ptr) ChpeStubRtlpGetLCIDFromLangInfoNode
1358 stdcall -version=0x600+ RtlpGetNameFromLangInfoNode(long long long) ChpeStubRtlpGetNameFromLangInfoNode
1359 stdcall -version=0x600+ RtlpGetSystemDefaultUILanguage(ptr long) ChpeStubRtlpGetSystemDefaultUILanguage
1360 stdcall -version=0x600+ RtlpGetUserOrMachineUILanguage4NLS(long long ptr) ChpeStubRtlpGetUserOrMachineUILanguage4NLS
1361 stdcall -version=0x600+ RtlpInitializeLangRegistryInfo(ptr) ChpeStubRtlpInitializeLangRegistryInfo
1362 stdcall -version=0x600+ RtlpIsQualifiedLanguage(long ptr long) ChpeStubRtlpIsQualifiedLanguage
1363 stdcall -version=0x600+ RtlpLoadMachineUIByPolicy(ptr long ptr) ChpeStubRtlpLoadMachineUIByPolicy
1364 stdcall -version=0x600+ RtlpLoadUserUIByPolicy(ptr long ptr) ChpeStubRtlpLoadUserUIByPolicy
1365 stdcall -version=0x600+ RtlpMuiFreeLangRegistryInfo(long) ChpeStubRtlpMuiFreeLangRegistryInfo
1366 stdcall -version=0x600+ RtlpMuiRegCreateRegistryInfo() ChpeStubRtlpMuiRegCreateRegistryInfo
1367 stdcall -version=0x600+ RtlpMuiRegFreeRegistryInfo(long long) ChpeStubRtlpMuiRegFreeRegistryInfo
1368 stdcall -version=0x600+ RtlpMuiRegLoadRegistryInfo(long long) ChpeStubRtlpMuiRegLoadRegistryInfo
1369 stdcall RtlpNotOwnerCriticalSection(ptr) ChpeAutoRtlpNotOwnerCriticalSection
1370 stdcall RtlpNtCreateKey(ptr long ptr long ptr ptr) ChpeAutoRtlpNtCreateKey
1371 stdcall RtlpNtEnumerateSubKey(ptr ptr long long) ChpeAutoRtlpNtEnumerateSubKey
1372 stdcall RtlpNtMakeTemporaryKey(ptr) ChpeAutoRtlpNtMakeTemporaryKey
1373 stdcall RtlpNtOpenKey(ptr long ptr long) ChpeAutoRtlpNtOpenKey
1374 stdcall RtlpNtQueryValueKey(ptr ptr ptr ptr long) ChpeAutoRtlpNtQueryValueKey
1375 stdcall RtlpNtSetValueKey(ptr long ptr long) ChpeAutoRtlpNtSetValueKey
1376 stdcall -version=0x600+ RtlpQueryDefaultUILanguage(ptr long) ChpeStubRtlpQueryDefaultUILanguage
1377 stdcall -version=0x600+ RtlpQueryProcessDebugInformationFromWow64(long ptr) ChpeStubRtlpQueryProcessDebugInformationFromWow64
1378 stdcall -version=0x600+ RtlpRefreshCachedUILanguage(wstr long) ChpeStubRtlpRefreshCachedUILanguage
1379 stdcall -version=0x600+ RtlpSetInstallLanguage(long ptr) ChpeStubRtlpSetInstallLanguage
1380 stdcall -version=0x600+ RtlpSetPreferredUILanguages(long ptr ptr) ChpeStubRtlpSetPreferredUILanguages
1381 stdcall RtlpUnWaitCriticalSection(ptr) ChpeAutoRtlpUnWaitCriticalSection
1382 stdcall -version=0x600+ RtlpVerifyAndCommitUILanguageSettings(long) ChpeStubRtlpVerifyAndCommitUILanguageSettings
1383 stdcall RtlpWaitForCriticalSection(ptr) ChpeAutoRtlpWaitForCriticalSection
1384 stdcall RtlxAnsiStringToUnicodeSize(ptr) ChpeAutoRtlxAnsiStringToUnicodeSize
1385 stdcall RtlxOemStringToUnicodeSize(ptr) ChpeAutoRtlxOemStringToUnicodeSize
1386 stdcall RtlxUnicodeStringToAnsiSize(ptr) ChpeAutoRtlxUnicodeStringToAnsiSize
1387 stdcall RtlxUnicodeStringToOemSize(ptr) ChpeAutoRtlxUnicodeStringToOemSize
1388 stdcall -version=0x600+ ShipAssert(long long) ChpeStubShipAssert
1389 stdcall -version=0x600+ ShipAssertGetBufferInfo(ptr ptr) ChpeStubShipAssertGetBufferInfo
1390 stdcall -version=0x600+ ShipAssertMsgA(long long) ChpeStubShipAssertMsgA
1391 stdcall -version=0x600+ ShipAssertMsgW(long long) ChpeStubShipAssertMsgW
1392 stdcall -version=0x600+ TpAllocAlpcCompletion(ptr ptr ptr ptr ptr) ChpeAutoTpAllocAlpcCompletion
1393 stdcall -version=0x600+ TpAllocAlpcCompletionEx(ptr ptr ptr ptr ptr) ChpeAutoTpAllocAlpcCompletionEx
1394 stdcall -version=0x600+ TpAllocCleanupGroup(ptr) ChpeAutoTpAllocCleanupGroup
1395 stdcall -version=0x600+ TpAllocIoCompletion(ptr ptr ptr ptr ptr) ChpeAutoTpAllocIoCompletion
1396 stdcall -version=0x600+ TpAllocPool(ptr ptr) ChpeAutoTpAllocPool
1397 stdcall -version=0x600+ TpAllocTimer(ptr ptr ptr ptr) ChpeAutoTpAllocTimer
1398 stdcall -version=0x600+ TpAllocWait(ptr ptr ptr ptr) ChpeAutoTpAllocWait
1399 stdcall -version=0x600+ TpAllocWork(ptr ptr ptr ptr) ChpeAutoTpAllocWork
1400 stdcall -version=0x600+ TpAlpcRegisterCompletionList(ptr) ChpeAutoTpAlpcRegisterCompletionList
1401 stdcall -version=0x600+ TpAlpcUnregisterCompletionList(ptr) ChpeAutoTpAlpcUnregisterCompletionList
1403 stdcall -version=0x600+ TpCallbackMayRunLong(ptr) ChpeAutoTpCallbackMayRunLong
1406 stdcall -version=0x600+ TpCallbackSendAlpcMessageOnCompletion(ptr ptr long ptr) ChpeAutoTpCallbackSendAlpcMessageOnCompletion
1407 stdcall -version=0x600+ TpCallbackSendPendingAlpcMessage(ptr) ChpeAutoTpCallbackSendPendingAlpcMessage
1411 stdcall -version=0x600+ TpCaptureCaller(long) ChpeStubTpCaptureCaller
1412 stdcall -version=0x600+ TpCheckTerminateWorker(ptr) ChpeStubTpCheckTerminateWorker
1413 stdcall -version=0x600+ TpDbgDumpHeapUsage(long ptr long) ChpeStubTpDbgDumpHeapUsage
1414 stdcall -version=0x600+ TpDbgSetLogRoutine() ChpeStubTpDbgSetLogRoutine
1418 stdcall -version=0x601+ TpQueryPoolStackInformation(ptr ptr) ChpeAutoTpQueryPoolStackInformation
1419 stdcall -version=0x600+ TpReleaseAlpcCompletion(ptr) ChpeAutoTpReleaseAlpcCompletion
1429 stdcall -version=0x601+ TpSetPoolStackInformation(ptr ptr) ChpeAutoTpSetPoolStackInformation
1434 stdcall -version=0x600+ TpSimpleTryPost(ptr ptr ptr) ChpeAutoTpSimpleTryPost
1436 stdcall -version=0x600+ TpWaitForAlpcCompletion(ptr) ChpeAutoTpWaitForAlpcCompletion
1442 stdcall -version=0x600+ WerCheckEventEscalation(long ptr) ChpeStubWerCheckEventEscalation
1443 stdcall -version=0x600+ WerReportSQMEvent(long long long) ChpeStubWerReportSQMEvent
1444 stdcall -version=0x600+ WerReportWatsonEvent(long long long long) ChpeStubWerReportWatsonEvent
1445 stdcall -version=0x600+ WinSqmAddToStream(ptr long long long) ChpeStubWinSqmAddToStream
1446 stdcall -version=0x600+ WinSqmAddToStreamEx(ptr long long ptr long) ChpeStubWinSqmAddToStreamEx
1447 stdcall -version=0x600+ WinSqmEndSession(ptr) ChpeAutoWinSqmEndSession
1448 stdcall -version=0x600+ WinSqmEventEnabled(long ptr) ChpeAutoWinSqmEventEnabled
1449 stdcall -version=0x600+ WinSqmEventWrite(long long long) ChpeStubWinSqmEventWrite
1450 stdcall -version=0x600+ WinSqmIncrementDWORD(long long long) ChpeAutoWinSqmIncrementDWORD
1451 stdcall -version=0x600+ WinSqmIsOptedIn() ChpeAutoWinSqmIsOptedIn
1452 stdcall -version=0x600+ WinSqmSetDWORD(ptr long long) ChpeAutoWinSqmSetDWORD
1453 stdcall -version=0x600+ WinSqmSetDWORD64(ptr long int64) ChpeAutoWinSqmSetDWORD64
1454 stdcall -version=0x600+ WinSqmSetString(ptr long ptr) ChpeStubWinSqmSetString
1456 stdcall ZwAcceptConnectPort(ptr long ptr long long ptr) ChpeAutoZwAcceptConnectPort
1457 stdcall ZwAccessCheck(ptr long long ptr ptr ptr ptr ptr) ChpeAutoZwAccessCheck
1458 stdcall ZwAccessCheckAndAuditAlarm(ptr long ptr ptr ptr long ptr long ptr ptr ptr) ChpeAutoZwAccessCheckAndAuditAlarm
1459 stdcall ZwAccessCheckByType(ptr ptr ptr long ptr long ptr ptr long ptr ptr) ChpeAutoZwAccessCheckByType
1460 stdcall ZwAccessCheckByTypeAndAuditAlarm(ptr ptr ptr ptr ptr ptr long long long ptr long ptr long ptr ptr ptr) ChpeAutoZwAccessCheckByTypeAndAuditAlarm
1461 stdcall ZwAccessCheckByTypeResultList(ptr ptr ptr long ptr long ptr ptr long ptr ptr) ChpeAutoZwAccessCheckByTypeResultList
1462 stdcall ZwAccessCheckByTypeResultListAndAuditAlarm(ptr ptr ptr ptr ptr ptr long long long ptr long ptr long ptr ptr ptr) ChpeAutoZwAccessCheckByTypeResultListAndAuditAlarm
1463 stdcall ZwAccessCheckByTypeResultListAndAuditAlarmByHandle(ptr ptr ptr ptr ptr ptr ptr long long long ptr long ptr long ptr ptr ptr) ChpeAutoZwAccessCheckByTypeResultListAndAuditAlarmByHandle
1464 stub -version=0x600+ ZwAcquireCMFViewOwnership
1465 stdcall ZwAddAtom(ptr long ptr) ChpeAutoZwAddAtom
1466 stdcall ZwAddBootEntry(ptr long) ChpeAutoZwAddBootEntry
1467 stdcall ZwAddDriverEntry(ptr long) ChpeAutoZwAddDriverEntry
1468 stdcall ZwAdjustGroupsToken(long long long long long long) ChpeAutoZwAdjustGroupsToken
1469 stdcall ZwAdjustPrivilegesToken(long long long long long long) ChpeAutoZwAdjustPrivilegesToken
1470 stdcall ZwAlertResumeThread(long ptr) ChpeAutoZwAlertResumeThread
1471 stdcall ZwAlertThread(long) ChpeAutoZwAlertThread
1472 stdcall ZwAlertThreadByThreadId(long) ChpeAutoZwAlertThreadByThreadId
1473 stdcall ZwAllocateLocallyUniqueId(ptr) ChpeAutoZwAllocateLocallyUniqueId
1474 stdcall ZwAllocateUserPhysicalPages(ptr ptr ptr) ChpeAutoZwAllocateUserPhysicalPages
1475 stdcall ZwAllocateUuids(ptr ptr ptr ptr) ChpeAutoZwAllocateUuids
1476 stdcall ZwAllocateVirtualMemory(long ptr ptr ptr long long) ChpeAutoZwAllocateVirtualMemory
1477 stdcall ZwAllocateVirtualMemoryEx(long ptr ptr long long ptr long) ChpeAutoZwAllocateVirtualMemoryEx
1478 stdcall -version=0x600+ ZwAlpcAcceptConnectPort(ptr ptr long ptr ptr ptr ptr ptr long) ChpeAutoZwAlpcAcceptConnectPort
1479 stdcall -version=0x600+ ZwAlpcCancelMessage(ptr long ptr) ChpeAutoZwAlpcCancelMessage
1480 stdcall -version=0x600+ ZwAlpcConnectPort(ptr ptr ptr ptr long ptr ptr ptr ptr ptr ptr) ChpeAutoZwAlpcConnectPort
1481 stdcall -version=0x602+ ZwAlpcConnectPortEx(ptr ptr ptr ptr long ptr ptr ptr ptr ptr ptr) ChpeAutoZwAlpcConnectPortEx
1482 stdcall -version=0x600+ ZwAlpcCreatePort(ptr ptr ptr) ChpeAutoZwAlpcCreatePort
1483 stdcall -version=0x600+ ZwAlpcCreatePortSection(ptr long ptr long ptr ptr) ChpeAutoZwAlpcCreatePortSection
1484 stdcall -version=0x600+ ZwAlpcCreateResourceReserve(ptr long long ptr) ChpeAutoZwAlpcCreateResourceReserve
1485 stdcall -version=0x600+ ZwAlpcCreateSectionView(ptr long ptr) ChpeAutoZwAlpcCreateSectionView
1486 stdcall -version=0x600+ ZwAlpcCreateSecurityContext(ptr long ptr) ChpeAutoZwAlpcCreateSecurityContext
1487 stdcall -version=0x600+ ZwAlpcDeletePortSection(ptr long ptr) ChpeAutoZwAlpcDeletePortSection
1488 stdcall -version=0x600+ ZwAlpcDeleteResourceReserve(ptr long long) ChpeAutoZwAlpcDeleteResourceReserve
1489 stdcall -version=0x600+ ZwAlpcDeleteSectionView(ptr long ptr) ChpeAutoZwAlpcDeleteSectionView
1490 stdcall -version=0x600+ ZwAlpcDeleteSecurityContext(ptr long ptr) ChpeAutoZwAlpcDeleteSecurityContext
1491 stdcall -version=0x600+ ZwAlpcDisconnectPort(ptr long) ChpeAutoZwAlpcDisconnectPort
1492 stdcall -version=0x600+ ZwAlpcImpersonateClientOfPort(ptr ptr ptr) ChpeAutoZwAlpcImpersonateClientOfPort
1493 stdcall -version=0xA00+ ZwAlpcImpersonateClientContainerOfPort(ptr ptr long) ChpeAutoZwAlpcImpersonateClientContainerOfPort
1494 stdcall -version=0x600+ ZwAlpcOpenSenderProcess(ptr ptr ptr long long ptr) ChpeAutoZwAlpcOpenSenderProcess
1495 stdcall -version=0x600+ ZwAlpcOpenSenderThread(ptr ptr ptr long long ptr) ChpeAutoZwAlpcOpenSenderThread
1496 stdcall -version=0x600+ ZwAlpcQueryInformation(ptr long ptr long ptr) ChpeAutoZwAlpcQueryInformation
1497 stdcall -version=0x600+ ZwAlpcQueryInformationMessage(ptr ptr long ptr long ptr) ChpeAutoZwAlpcQueryInformationMessage
1498 stdcall -version=0x600+ ZwAlpcRevokeSecurityContext(ptr long ptr) ChpeAutoZwAlpcRevokeSecurityContext
1499 stdcall -version=0x600+ ZwAlpcSendWaitReceivePort(ptr long ptr ptr ptr ptr ptr ptr) ChpeAutoZwAlpcSendWaitReceivePort
1500 stdcall -version=0x600+ ZwAlpcSetInformation(ptr long ptr long) ChpeAutoZwAlpcSetInformation
1501 stdcall ZwApphelpCacheControl(long ptr) ChpeAutoZwApphelpCacheControl
1502 stdcall ZwAreMappedFilesTheSame(ptr ptr) ChpeAutoZwAreMappedFilesTheSame
1503 stdcall ZwAssignProcessToJobObject(long long) ChpeAutoZwAssignProcessToJobObject
1504 stdcall ZwCallbackReturn(ptr long long) ChpeAutoZwCallbackReturn
1505 stdcall ZwCancelDeviceWakeupRequest(ptr) ChpeAutoZwCancelDeviceWakeupRequest
1506 stdcall ZwCancelIoFile(long ptr) ChpeAutoZwCancelIoFile
1507 stdcall -version=0x600+ ZwCancelIoFileEx(ptr ptr ptr) ChpeAutoZwCancelIoFileEx
1508 stdcall -version=0x600+ ZwCancelSynchronousIoFile(ptr ptr ptr) ChpeAutoZwCancelSynchronousIoFile
1509 stdcall ZwCancelTimer(long ptr) ChpeAutoZwCancelTimer
1510 stdcall ZwClearEvent(long) ChpeAutoZwClearEvent
1511 stdcall ZwClose(long) ChpeAutoZwClose
1512 stdcall ZwCloseObjectAuditAlarm(ptr ptr long) ChpeAutoZwCloseObjectAuditAlarm
1513 stdcall -version=0x600+ ZwCommitComplete(ptr ptr) ChpeAutoZwCommitComplete
1514 stdcall -version=0x600+ ZwCommitEnlistment(ptr ptr) ChpeAutoZwCommitEnlistment
1515 stdcall -version=0x600+ ZwCommitTransaction(ptr long) ChpeAutoZwCommitTransaction
1516 stdcall ZwCompactKeys(long ptr) ChpeAutoZwCompactKeys
1517 stdcall ZwCompareTokens(ptr ptr ptr) ChpeAutoZwCompareTokens
1518 stdcall ZwCompleteConnectPort(ptr) ChpeAutoZwCompleteConnectPort
1519 stdcall ZwCompressKey(ptr) ChpeAutoZwCompressKey
1520 stdcall ZwConnectPort(ptr ptr ptr ptr ptr ptr ptr ptr) ChpeAutoZwConnectPort
1521 stdcall ZwContinue(ptr long) ChpeAutoZwContinue
1522 stdcall ZwCreateDebugObject(ptr long ptr long) ChpeAutoZwCreateDebugObject
1523 stdcall ZwCreateDirectoryObject(long long long) ChpeAutoZwCreateDirectoryObject
1524 stdcall -version=0x600+ ZwCreateEnlistment(ptr long ptr ptr ptr long long ptr) ChpeAutoZwCreateEnlistment
1525 stdcall ZwCreateEvent(long long long long long) ChpeAutoZwCreateEvent
1526 stdcall ZwCreateEventPair(ptr long ptr) ChpeAutoZwCreateEventPair
1527 stdcall ZwCreateFile(ptr long ptr ptr long long long ptr long long ptr) ChpeAutoZwCreateFile
1528 stdcall ZwCreateIoCompletion(ptr long ptr long) ChpeAutoZwCreateIoCompletion
1529 stdcall -version=0x602+ ZwCreateWaitCompletionPacket(ptr long ptr) ChpeAutoZwCreateWaitCompletionPacket
1530 stdcall -version=0x602+ ZwAssociateWaitCompletionPacket(ptr ptr ptr ptr ptr long ptr ptr) ChpeAutoZwAssociateWaitCompletionPacket
1531 stdcall -version=0x602+ ZwCancelWaitCompletionPacket(ptr long) ChpeAutoZwCancelWaitCompletionPacket
1532 stdcall ZwCreateJobObject(ptr long ptr) ChpeAutoZwCreateJobObject
1533 stdcall ZwCreateJobSet(long ptr long) ChpeAutoZwCreateJobSet
1534 stdcall ZwCreateKey(ptr long ptr long ptr long long) ChpeAutoZwCreateKey
1535 stdcall -version=0x600+ ZwCreateKeyTransacted(ptr long ptr long ptr long ptr ptr) ChpeAutoZwCreateKeyTransacted
1536 stdcall ZwCreateKeyedEvent(ptr long ptr long) ChpeAutoZwCreateKeyedEvent
1537 stdcall ZwCreateMailslotFile(long long long long long long long long) ChpeAutoZwCreateMailslotFile
1538 stdcall ZwCreateMutant(ptr long ptr long) ChpeAutoZwCreateMutant
1539 stdcall ZwCreateNamedPipeFile(ptr long ptr ptr long long long long long long long long long ptr) ChpeAutoZwCreateNamedPipeFile
1540 stdcall ZwCreatePagingFile(ptr ptr ptr long) ChpeAutoZwCreatePagingFile
1541 stdcall ZwCreatePort(ptr ptr long long long) ChpeAutoZwCreatePort
1542 stdcall ZwCreateProcess(ptr long ptr ptr long ptr ptr ptr) ChpeAutoZwCreateProcess
1543 stdcall ZwCreateProcessEx(ptr long ptr ptr long ptr ptr ptr long) ChpeAutoZwCreateProcessEx
1544 stdcall ZwCreateProfile(ptr ptr ptr long long ptr long long long) ChpeAutoZwCreateProfile
1545 stdcall -version=0x600+ ZwCreateResourceManager(ptr long ptr ptr ptr long ptr) ChpeAutoZwCreateResourceManager
1546 stdcall ZwCreateSection(ptr long ptr ptr long long long) ChpeAutoZwCreateSection
1547 stdcall ZwCreateSemaphore(ptr long ptr long long) ChpeAutoZwCreateSemaphore
1548 stdcall ZwCreateSymbolicLinkObject(ptr long ptr ptr) ChpeAutoZwCreateSymbolicLinkObject
1549 stdcall ZwCreateThread(ptr long ptr ptr ptr ptr ptr long) ChpeAutoZwCreateThread
1550 stdcall -version=0x600+ ZwCreateThreadEx(ptr long ptr ptr ptr ptr long long long long ptr) ChpeAutoZwCreateThreadEx
1551 stdcall ZwCreateTimer(ptr long ptr long) ChpeAutoZwCreateTimer
1552 stdcall ZwCreateTimer2(ptr ptr ptr long long) ChpeAutoZwCreateTimer2
1553 stdcall ZwCreateToken(ptr long ptr long ptr ptr ptr ptr ptr ptr ptr ptr ptr) ChpeAutoZwCreateToken
1554 stdcall -version=0x600+ ZwCreateTransaction(ptr long ptr ptr ptr long long long ptr ptr) ChpeAutoZwCreateTransaction
1555 stdcall -version=0x600+ ZwCreateTransactionManager(ptr long ptr ptr long long) ChpeAutoZwCreateTransactionManager
1556 stdcall -version=0x600+ ZwCreateUserProcess(ptr ptr long long ptr ptr long long ptr ptr ptr) ChpeAutoZwCreateUserProcess
1557 stdcall ZwCreateWaitablePort(ptr ptr long long long) ChpeAutoZwCreateWaitablePort
1558 stdcall -version=0x602+ ZwCreateWnfStateName(ptr long long long ptr long ptr) ChpeAutoZwCreateWnfStateName
1559 stdcall -version=0x600+ ZwCreateWorkerFactory(ptr long ptr ptr ptr ptr ptr long long long) ChpeStubZwCreateWorkerFactory
1560 stdcall ZwDebugActiveProcess(ptr ptr) ChpeAutoZwDebugActiveProcess
1561 stdcall ZwDebugContinue(ptr ptr long) ChpeAutoZwDebugContinue
1562 stdcall ZwDelayExecution(long ptr) ChpeAutoZwDelayExecution
1563 stdcall ZwDeleteAtom(long) ChpeAutoZwDeleteAtom
1564 stdcall ZwDeleteBootEntry(long) ChpeAutoZwDeleteBootEntry
1565 stdcall ZwDeleteDriverEntry(long) ChpeAutoZwDeleteDriverEntry
1566 stdcall ZwDeleteFile(ptr) ChpeAutoZwDeleteFile
1567 stdcall ZwDeleteKey(long) ChpeAutoZwDeleteKey
1568 stdcall ZwDeleteObjectAuditAlarm(ptr ptr long) ChpeAutoZwDeleteObjectAuditAlarm
1569 stdcall -version=0x600+ ZwDeletePrivateNamespace(ptr) ChpeAutoZwDeletePrivateNamespace
1570 stdcall ZwDeleteValueKey(long ptr) ChpeAutoZwDeleteValueKey
1571 stdcall -version=0x602+ ZwDeleteWnfStateData(ptr ptr) ChpeAutoZwDeleteWnfStateData
1572 stdcall -version=0x602+ ZwDeleteWnfStateName(ptr) ChpeAutoZwDeleteWnfStateName
1573 stdcall ZwDeviceIoControlFile(long long long long long long long long long long) ChpeAutoZwDeviceIoControlFile
1574 stdcall ZwDisplayString(ptr) ChpeAutoZwDisplayString
1575 stdcall ZwDuplicateObject(long long long ptr long long long) ChpeAutoZwDuplicateObject
1576 stdcall ZwDuplicateToken(long long long long long long) ChpeAutoZwDuplicateToken
1577 stdcall ZwEnumerateBootEntries(ptr ptr) ChpeAutoZwEnumerateBootEntries
1578 stdcall ZwEnumerateDriverEntries(ptr ptr) ChpeAutoZwEnumerateDriverEntries
1579 stdcall ZwEnumerateKey(long long long ptr long ptr) ChpeAutoZwEnumerateKey
1580 stdcall ZwEnumerateSystemEnvironmentValuesEx(long ptr long) ChpeAutoZwEnumerateSystemEnvironmentValuesEx
1581 stdcall -version=0x600+ ZwEnumerateTransactionObject(ptr long ptr long ptr) ChpeAutoZwEnumerateTransactionObject
1582 stdcall ZwEnumerateValueKey(long long long ptr long ptr) ChpeAutoZwEnumerateValueKey
1583 stdcall ZwExtendSection(ptr ptr) ChpeAutoZwExtendSection
1584 stdcall ZwFilterToken(ptr long ptr ptr ptr ptr) ChpeAutoZwFilterToken
1585 stdcall -version=0x602+ ZwCreateLowBoxToken(ptr ptr long ptr ptr long ptr long ptr) ChpeAutoZwCreateLowBoxToken
1586 stdcall -version=0x600+ ZwCreatePrivateNamespace(ptr long ptr ptr) ChpeAutoZwCreatePrivateNamespace
1587 stdcall ZwFindAtom(ptr long ptr) ChpeAutoZwFindAtom
1588 stdcall ZwFlushBuffersFile(long ptr) ChpeAutoZwFlushBuffersFile
1589 stdcall ZwFlushBuffersFileEx(long long ptr long ptr) ChpeAutoZwFlushBuffersFileEx
1590 stdcall -version=0x600+ ZwFlushInstallUILanguage(long long) ChpeStubZwFlushInstallUILanguage
1591 stdcall ZwFlushInstructionCache(long ptr long) ChpeAutoZwFlushInstructionCache
1592 stdcall ZwFlushKey(long) ChpeAutoZwFlushKey
1593 stdcall -version=0x600+ ZwFlushProcessWriteBuffers() ChpeAutoZwFlushProcessWriteBuffers
1594 stdcall ZwFlushVirtualMemory(ptr ptr ptr ptr) ChpeAutoZwFlushVirtualMemory
1595 stdcall ZwFlushWriteBuffer() ChpeAutoZwFlushWriteBuffer
1596 stdcall ZwFreeUserPhysicalPages(ptr ptr ptr) ChpeAutoZwFreeUserPhysicalPages
1597 stdcall ZwFreeVirtualMemory(long ptr ptr long) ChpeAutoZwFreeVirtualMemory
1598 stdcall -version=0x600+ ZwFreezeRegistry(long) ChpeStubZwFreezeRegistry
1599 stdcall -version=0x600+ ZwFreezeTransactions(ptr ptr) ChpeStubZwFreezeTransactions
1600 stdcall ZwFsControlFile(long long long long long long long long long long) ChpeAutoZwFsControlFile
1601 stdcall ZwGetContextThread(long ptr) ChpeAutoZwGetContextThread
1602 stdcall ZwGetCurrentProcessorNumber() ChpeAutoZwGetCurrentProcessorNumber
1603 stdcall -version=0xA00+ ZwGetCurrentProcessorNumberEx(ptr) ChpeAutoZwGetCurrentProcessorNumberEx
1604 stdcall ZwGetDevicePowerState(ptr ptr) ChpeAutoZwGetDevicePowerState
1605 stdcall -version=0x600+ ZwGetMUIRegistryInfo(long ptr ptr) ChpeStubZwGetMUIRegistryInfo
1606 stdcall -version=0x600+ ZwGetNextProcess(ptr long long long ptr) ChpeStubZwGetNextProcess
1607 stdcall -version=0x600+ ZwGetNextThread(ptr ptr long long long ptr) ChpeAutoZwGetNextThread
1608 stdcall -version=0x600+ ZwGetNlsSectionPtr(long long ptr ptr ptr) ChpeAutoZwGetNlsSectionPtr
1609 stdcall -version=0x600+ ZwGetNotificationResourceManager(ptr ptr long ptr ptr long ptr) ChpeAutoZwGetNotificationResourceManager
1610 stdcall ZwGetPlugPlayEvent(long long ptr long) ChpeAutoZwGetPlugPlayEvent
1611 stdcall ZwGetWriteWatch(long long ptr long ptr ptr ptr) ChpeAutoZwGetWriteWatch
1612 stdcall ZwImpersonateAnonymousToken(ptr) ChpeAutoZwImpersonateAnonymousToken
1613 stdcall ZwImpersonateClientOfPort(ptr ptr) ChpeAutoZwImpersonateClientOfPort
1614 stdcall ZwImpersonateThread(ptr ptr ptr) ChpeAutoZwImpersonateThread
1615 stdcall -version=0x600+ ZwInitializeNlsFiles(ptr ptr ptr ptr) ChpeStubZwInitializeNlsFiles
1616 stdcall ZwInitializeRegistry(long) ChpeAutoZwInitializeRegistry
1617 stdcall ZwInitiatePowerAction(long long long long) ChpeAutoZwInitiatePowerAction
1618 stdcall ZwIsProcessInJob(long long) ChpeAutoZwIsProcessInJob
1619 stdcall ZwIsSystemResumeAutomatic() ChpeAutoZwIsSystemResumeAutomatic
1620 stdcall -version=0x600+ ZwIsUILanguageComitted() ChpeStubZwIsUILanguageComitted
1621 stdcall ZwListenPort(ptr ptr) ChpeAutoZwListenPort
1622 stdcall ZwLoadDriver(ptr) ChpeAutoZwLoadDriver
1623 stdcall ZwLoadKey2(ptr ptr long) ChpeAutoZwLoadKey2
1624 stdcall ZwLoadKey(ptr ptr) ChpeAutoZwLoadKey
1625 stdcall ZwLoadKeyEx(ptr ptr long ptr ptr long ptr ptr) ChpeAutoZwLoadKeyEx
1626 stdcall ZwLockFile(long long ptr ptr ptr ptr ptr ptr long long) ChpeAutoZwLockFile
1627 stdcall ZwLockProductActivationKeys(ptr ptr) ChpeAutoZwLockProductActivationKeys
1628 stdcall ZwLockRegistryKey(ptr) ChpeAutoZwLockRegistryKey
1629 stdcall ZwLockVirtualMemory(long ptr ptr long) ChpeAutoZwLockVirtualMemory
1630 stdcall ZwMakePermanentObject(ptr) ChpeAutoZwMakePermanentObject
1631 stdcall ZwMakeTemporaryObject(long) ChpeAutoZwMakeTemporaryObject
1632 stdcall -version=0x600+ ZwMapCMFModule(long long ptr ptr ptr) ChpeStubZwMapCMFModule
1633 stdcall ZwMapUserPhysicalPages(ptr ptr ptr) ChpeAutoZwMapUserPhysicalPages
1634 stdcall ZwMapUserPhysicalPagesScatter(ptr ptr ptr) ChpeAutoZwMapUserPhysicalPagesScatter
1635 stdcall ZwMapViewOfSection(long long ptr long long ptr ptr long long long) ChpeAutoZwMapViewOfSection
1636 stdcall ZwModifyBootEntry(ptr) ChpeAutoZwModifyBootEntry
1637 stdcall ZwModifyDriverEntry(ptr) ChpeAutoZwModifyDriverEntry
1638 stdcall ZwNotifyChangeDirectoryFile(long long ptr ptr ptr ptr long long long) ChpeAutoZwNotifyChangeDirectoryFile
1639 stdcall ZwNotifyChangeDirectoryFileEx(long long ptr ptr ptr ptr long long long long) ChpeAutoZwNotifyChangeDirectoryFileEx
1640 stdcall ZwNotifyChangeKey(long long ptr ptr ptr long long ptr long long) ChpeAutoZwNotifyChangeKey
1641 stdcall ZwNotifyChangeMultipleKeys(ptr long ptr ptr ptr ptr ptr long long ptr long long) ChpeAutoZwNotifyChangeMultipleKeys
1642 stdcall ZwOpenDirectoryObject(long long long) ChpeAutoZwOpenDirectoryObject
1643 stdcall -version=0x600+ ZwOpenEnlistment(ptr long ptr ptr ptr) ChpeAutoZwOpenEnlistment
1644 stdcall ZwOpenEvent(long long long) ChpeAutoZwOpenEvent
1645 stdcall ZwOpenEventPair(ptr long ptr) ChpeAutoZwOpenEventPair
1646 stdcall ZwOpenFile(ptr long ptr ptr long long) ChpeAutoZwOpenFile
1647 stdcall ZwOpenIoCompletion(ptr long ptr) ChpeAutoZwOpenIoCompletion
1648 stdcall ZwOpenJobObject(ptr long ptr) ChpeAutoZwOpenJobObject
1649 stdcall ZwOpenKey(ptr long ptr) ChpeAutoZwOpenKey
1650 stdcall ZwOpenKeyEx(ptr long ptr long) ChpeAutoZwOpenKeyEx
1651 stdcall -version=0x600+ ZwOpenKeyTransacted(ptr long ptr ptr) ChpeAutoZwOpenKeyTransacted
1652 stdcall ZwOpenKeyedEvent(ptr long ptr) ChpeAutoZwOpenKeyedEvent
1653 stdcall ZwOpenMutant(ptr long ptr) ChpeAutoZwOpenMutant
1654 stdcall ZwOpenObjectAuditAlarm(ptr ptr ptr ptr ptr ptr long long ptr long long ptr) ChpeAutoZwOpenObjectAuditAlarm
1655 stdcall -version=0x600+ ZwOpenPrivateNamespace(ptr long ptr ptr) ChpeAutoZwOpenPrivateNamespace
1656 stdcall ZwOpenProcess(ptr long ptr ptr) ChpeAutoZwOpenProcess
1657 stdcall ZwOpenProcessToken(long long ptr) ChpeAutoZwOpenProcessToken
1658 stdcall ZwOpenProcessTokenEx(long long long ptr) ChpeAutoZwOpenProcessTokenEx
1659 stdcall -version=0x600+ ZwOpenResourceManager(ptr long ptr ptr ptr) ChpeAutoZwOpenResourceManager
1660 stdcall ZwOpenSection(ptr long ptr) ChpeAutoZwOpenSection
1661 stdcall ZwOpenSemaphore(long long ptr) ChpeAutoZwOpenSemaphore
1662 stdcall -version=0x600+ ZwOpenSession(ptr long ptr) ChpeStubZwOpenSession
1663 stdcall ZwOpenSymbolicLinkObject(ptr long ptr) ChpeAutoZwOpenSymbolicLinkObject
1664 stdcall ZwOpenThread(ptr long ptr ptr) ChpeAutoZwOpenThread
1665 stdcall ZwOpenThreadToken(long long long ptr) ChpeAutoZwOpenThreadToken
1666 stdcall ZwOpenThreadTokenEx(long long long long ptr) ChpeAutoZwOpenThreadTokenEx
1667 stdcall ZwOpenTimer(ptr long ptr) ChpeAutoZwOpenTimer
1668 stdcall -version=0x600+ ZwOpenTransaction(ptr long ptr ptr ptr) ChpeAutoZwOpenTransaction
1669 stdcall -version=0x600+ ZwOpenTransactionManager(ptr long ptr ptr ptr long) ChpeAutoZwOpenTransactionManager
1670 stdcall ZwPlugPlayControl(ptr ptr long) ChpeAutoZwPlugPlayControl
1671 stdcall ZwPowerInformation(long ptr long ptr long) ChpeAutoZwPowerInformation
1672 stdcall -version=0x600+ ZwPrePrepareComplete(ptr ptr) ChpeAutoZwPrePrepareComplete
1673 stdcall -version=0x600+ ZwPrePrepareEnlistment(ptr ptr) ChpeAutoZwPrePrepareEnlistment
1674 stdcall -version=0x600+ ZwPrepareComplete(ptr ptr) ChpeAutoZwPrepareComplete
1675 stdcall -version=0x600+ ZwPrepareEnlistment(ptr ptr) ChpeAutoZwPrepareEnlistment
1676 stdcall ZwPrivilegeCheck(ptr ptr ptr) ChpeAutoZwPrivilegeCheck
1677 stdcall ZwPrivilegeObjectAuditAlarm(ptr ptr ptr long ptr long) ChpeAutoZwPrivilegeObjectAuditAlarm
1678 stdcall ZwPrivilegedServiceAuditAlarm(ptr ptr ptr ptr long) ChpeAutoZwPrivilegedServiceAuditAlarm
1679 stdcall -version=0x600+ ZwPropagationComplete(ptr long long ptr) ChpeAutoZwPropagationComplete
1680 stdcall -version=0x600+ ZwPropagationFailed(ptr long long) ChpeAutoZwPropagationFailed
1681 stdcall ZwProtectVirtualMemory(long ptr ptr long ptr) ChpeAutoZwProtectVirtualMemory
1682 stdcall ZwPulseEvent(long ptr) ChpeAutoZwPulseEvent
1683 stdcall ZwQueryAttributesFile(ptr ptr) ChpeAutoZwQueryAttributesFile
1684 stdcall ZwQueryBootEntryOrder(ptr ptr) ChpeAutoZwQueryBootEntryOrder
1685 stdcall ZwQueryBootOptions(ptr ptr) ChpeAutoZwQueryBootOptions
1686 stdcall ZwQueryDebugFilterState(long long) ChpeAutoZwQueryDebugFilterState
1687 stdcall ZwQueryDefaultLocale(long ptr) ChpeAutoZwQueryDefaultLocale
1688 stdcall ZwQueryDefaultUILanguage(ptr) ChpeAutoZwQueryDefaultUILanguage
1689 stdcall ZwQueryDirectoryFile(long long ptr ptr ptr ptr long long long ptr long) ChpeAutoZwQueryDirectoryFile
1690 stdcall ZwQueryDirectoryFileEx(long long ptr ptr ptr ptr long long long ptr) ChpeAutoZwQueryDirectoryFileEx
1691 stdcall ZwQueryDirectoryObject(long ptr long long long ptr ptr) ChpeAutoZwQueryDirectoryObject
1692 stdcall ZwQueryDriverEntryOrder(ptr ptr) ChpeAutoZwQueryDriverEntryOrder
1693 stdcall ZwQueryEaFile(long ptr ptr long long ptr long ptr long) ChpeAutoZwQueryEaFile
1694 stdcall ZwQueryEvent(long long ptr long ptr) ChpeAutoZwQueryEvent
1695 stdcall ZwQueryFullAttributesFile(ptr ptr) ChpeAutoZwQueryFullAttributesFile
1696 stdcall ZwQueryInformationAtom(long long ptr long ptr) ChpeAutoZwQueryInformationAtom
1697 stdcall ZwQueryInformationByName(ptr ptr ptr long long) ChpeAutoZwQueryInformationByName
1698 stdcall -version=0x600+ ZwQueryInformationEnlistment(ptr long ptr long ptr) ChpeAutoZwQueryInformationEnlistment
1699 stdcall ZwQueryInformationFile(long ptr ptr long long) ChpeAutoZwQueryInformationFile
1700 stdcall ZwQueryInformationJobObject(long long ptr long ptr) ChpeAutoZwQueryInformationJobObject
1701 stdcall ZwQueryInformationPort(ptr long ptr long ptr) ChpeAutoZwQueryInformationPort
1702 stdcall ZwQueryInformationProcess(long long ptr long ptr) ChpeAutoZwQueryInformationProcess
1703 stdcall -version=0x600+ ZwQueryInformationResourceManager(ptr long ptr long ptr) ChpeAutoZwQueryInformationResourceManager
1704 stdcall ZwQueryInformationThread(long long ptr long ptr) ChpeAutoZwQueryInformationThread
1705 stdcall ZwQueryInformationToken(long long ptr long ptr) ChpeAutoZwQueryInformationToken
1706 stdcall -version=0x600+ ZwQueryInformationTransaction(ptr long ptr long ptr) ChpeAutoZwQueryInformationTransaction
1707 stdcall -version=0x600+ ZwQueryInformationTransactionManager(ptr long ptr long ptr) ChpeAutoZwQueryInformationTransactionManager
1708 stdcall -version=0x600+ ZwQueryInformationWorkerFactory(ptr long ptr long ptr) ChpeStubZwQueryInformationWorkerFactory
1709 stdcall ZwQueryInstallUILanguage(ptr) ChpeAutoZwQueryInstallUILanguage
1710 stdcall ZwQueryIntervalProfile(long ptr) ChpeAutoZwQueryIntervalProfile
1711 stdcall ZwQueryIoCompletion(long long ptr long ptr) ChpeAutoZwQueryIoCompletion
1712 stdcall ZwQueryKey(long long ptr long ptr) ChpeAutoZwQueryKey
1713 stdcall -version=0x600+ ZwQueryLicenseValue(ptr ptr ptr long ptr) ChpeAutoZwQueryLicenseValue
1714 stdcall ZwQueryMultipleValueKey(long ptr long ptr long ptr) ChpeAutoZwQueryMultipleValueKey
1715 stdcall ZwQueryMutant(long long ptr long ptr) ChpeAutoZwQueryMutant
1716 stdcall ZwQueryObject(long long long long long) ChpeAutoZwQueryObject
1717 stdcall ZwQueryOpenSubKeys(ptr ptr) ChpeAutoZwQueryOpenSubKeys
1718 stdcall ZwQueryOpenSubKeysEx(ptr long ptr ptr) ChpeAutoZwQueryOpenSubKeysEx
1719 stdcall ZwQueryPerformanceCounter(long long) ChpeAutoZwQueryPerformanceCounter
1720 stdcall ZwQueryPortInformationProcess() ChpeAutoZwQueryPortInformationProcess
1721 stdcall ZwQueryQuotaInformationFile(ptr ptr ptr long long ptr long ptr long) ChpeAutoZwQueryQuotaInformationFile
1722 stdcall ZwQuerySection(long long long long long) ChpeAutoZwQuerySection
1723 stdcall ZwQuerySecurityObject(long long long long long) ChpeAutoZwQuerySecurityObject
1724 stdcall ZwQuerySemaphore(long long long long long) ChpeAutoZwQuerySemaphore
1725 stdcall ZwQuerySymbolicLinkObject(long ptr ptr) ChpeAutoZwQuerySymbolicLinkObject
1726 stdcall ZwQuerySystemEnvironmentValue(ptr ptr long ptr) ChpeAutoZwQuerySystemEnvironmentValue
1727 stdcall ZwQuerySystemEnvironmentValueEx(ptr ptr ptr ptr ptr) ChpeAutoZwQuerySystemEnvironmentValueEx
1728 stdcall ZwQuerySystemInformation(long long long long) ChpeAutoZwQuerySystemInformation
1729 stdcall -version=0x601+ ZwQuerySystemInformationEx(long ptr long ptr long ptr) ChpeAutoZwQuerySystemInformationEx
1730 stdcall ZwQuerySystemTime(ptr) ChpeAutoZwQuerySystemTime
1731 stdcall ZwQueryTimer(ptr long ptr long ptr) ChpeAutoZwQueryTimer
1732 stdcall ZwQueryTimerResolution(long long long) ChpeAutoZwQueryTimerResolution
1733 stdcall ZwQueryValueKey(long ptr long ptr long ptr) ChpeAutoZwQueryValueKey
1734 stdcall ZwQueryVirtualMemory(long ptr long ptr long ptr) ChpeAutoZwQueryVirtualMemory
1735 stdcall ZwQueryVolumeInformationFile(long ptr ptr long long) ChpeAutoZwQueryVolumeInformationFile
1736 stdcall -version=0x602+ ZwQueryWnfStateData(ptr ptr ptr ptr ptr ptr) ChpeAutoZwQueryWnfStateData
1737 stdcall -version=0x602+ ZwQueryWnfStateNameInformation(ptr long ptr ptr long) ChpeAutoZwQueryWnfStateNameInformation
1738 stdcall ZwQueueApcThread(long ptr long long long) ChpeAutoZwQueueApcThread
1739 stdcall ZwRaiseException(ptr ptr long) ChpeAutoZwRaiseException
1740 stdcall ZwRaiseHardError(long long long ptr long ptr) ChpeAutoZwRaiseHardError
1741 stdcall ZwReadFile(long long ptr ptr ptr ptr long ptr ptr) ChpeAutoZwReadFile
1742 stdcall ZwReadFileScatter(long long ptr ptr ptr ptr long ptr ptr) ChpeAutoZwReadFileScatter
1743 stdcall -version=0x600+ ZwReadOnlyEnlistment(ptr ptr) ChpeAutoZwReadOnlyEnlistment
1744 stdcall ZwReadRequestData(ptr ptr long ptr long ptr) ChpeAutoZwReadRequestData
1745 stdcall ZwReadVirtualMemory(long ptr ptr long ptr) ChpeAutoZwReadVirtualMemory
1746 stdcall -version=0x600+ ZwRecoverEnlistment(ptr ptr) ChpeAutoZwRecoverEnlistment
1747 stdcall -version=0x600+ ZwRecoverResourceManager(ptr) ChpeAutoZwRecoverResourceManager
1748 stdcall -version=0x600+ ZwRecoverTransactionManager(ptr) ChpeAutoZwRecoverTransactionManager
1749 stdcall -version=0x600+ ZwRegisterProtocolAddressInformation(ptr ptr long ptr long) ChpeAutoZwRegisterProtocolAddressInformation
1750 stdcall ZwRegisterThreadTerminatePort(ptr) ChpeAutoZwRegisterThreadTerminatePort
1751 stdcall -version=0x600+ ZwReleaseCMFViewOwnership() ChpeStubZwReleaseCMFViewOwnership
1752 stdcall ZwReleaseKeyedEvent(ptr ptr long ptr) ChpeAutoZwReleaseKeyedEvent
1753 stdcall ZwReleaseMutant(long ptr) ChpeAutoZwReleaseMutant
1754 stdcall ZwReleaseSemaphore(long long ptr) ChpeAutoZwReleaseSemaphore
1755 stdcall -version=0x600+ ZwReleaseWorkerFactoryWorker(ptr) ChpeStubZwReleaseWorkerFactoryWorker
1756 stdcall ZwRemoveIoCompletion(ptr ptr ptr ptr ptr) ChpeAutoZwRemoveIoCompletion
1757 stdcall -version=0x600+ ZwRemoveIoCompletionEx(ptr ptr long ptr ptr long) ChpeAutoZwRemoveIoCompletionEx
1758 stdcall ZwRemoveProcessDebug(ptr ptr) ChpeAutoZwRemoveProcessDebug
1759 stdcall ZwRenameKey(ptr ptr) ChpeAutoZwRenameKey
1760 stdcall -version=0x600+ ZwRenameTransactionManager(ptr ptr) ChpeAutoZwRenameTransactionManager
1761 stdcall ZwReplaceKey(ptr long ptr) ChpeAutoZwReplaceKey
1762 stdcall -version=0x600+ ZwReplacePartitionUnit(wstr wstr long) ChpeStubZwReplacePartitionUnit
1763 stdcall ZwReplyPort(ptr ptr) ChpeAutoZwReplyPort
1764 stdcall ZwReplyWaitReceivePort(ptr ptr ptr ptr) ChpeAutoZwReplyWaitReceivePort
1765 stdcall ZwReplyWaitReceivePortEx(ptr ptr ptr ptr ptr) ChpeAutoZwReplyWaitReceivePortEx
1766 stdcall ZwReplyWaitReplyPort(ptr ptr) ChpeAutoZwReplyWaitReplyPort
1767 stdcall ZwRequestDeviceWakeup(ptr) ChpeAutoZwRequestDeviceWakeup
1768 stdcall ZwRequestPort(ptr ptr) ChpeAutoZwRequestPort
1769 stdcall ZwRequestWaitReplyPort(ptr ptr ptr) ChpeAutoZwRequestWaitReplyPort
1770 stdcall ZwRequestWakeupLatency(long) ChpeAutoZwRequestWakeupLatency
1771 stdcall ZwResetEvent(long ptr) ChpeAutoZwResetEvent
1772 stdcall ZwResetWriteWatch(long ptr long) ChpeAutoZwResetWriteWatch
1773 stdcall ZwRestoreKey(long long long) ChpeAutoZwRestoreKey
1774 stdcall ZwResumeProcess(ptr) ChpeAutoZwResumeProcess
1775 stdcall ZwResumeThread(long long) ChpeAutoZwResumeThread
1776 stdcall -version=0x600+ ZwRollbackComplete(ptr ptr) ChpeAutoZwRollbackComplete
1777 stdcall -version=0x600+ ZwRollbackEnlistment(ptr ptr) ChpeAutoZwRollbackEnlistment
1778 stdcall -version=0x600+ ZwRollbackTransaction(ptr long) ChpeAutoZwRollbackTransaction
1779 stdcall -version=0x600+ ZwRollforwardTransactionManager(ptr ptr) ChpeAutoZwRollforwardTransactionManager
1780 stdcall ZwSaveKey(long long) ChpeAutoZwSaveKey
1781 stdcall ZwSaveKeyEx(ptr ptr long) ChpeAutoZwSaveKeyEx
1782 stdcall ZwSaveMergedKeys(ptr ptr ptr) ChpeAutoZwSaveMergedKeys
1783 stdcall ZwSecureConnectPort(ptr ptr ptr ptr ptr ptr ptr ptr ptr) ChpeAutoZwSecureConnectPort
1784 stdcall ZwSetBootEntryOrder(ptr ptr) ChpeAutoZwSetBootEntryOrder
1785 stdcall ZwSetBootOptions(ptr long) ChpeAutoZwSetBootOptions
1786 stdcall ZwSetContextThread(long ptr) ChpeAutoZwSetContextThread
1787 stdcall ZwSetDebugFilterState(long long long) ChpeAutoZwSetDebugFilterState
1788 stdcall ZwSetDefaultHardErrorPort(ptr) ChpeAutoZwSetDefaultHardErrorPort
1789 stdcall ZwSetDefaultLocale(long long) ChpeAutoZwSetDefaultLocale
1790 stdcall ZwSetDefaultUILanguage(long) ChpeAutoZwSetDefaultUILanguage
1791 stdcall ZwSetDriverEntryOrder(ptr ptr) ChpeAutoZwSetDriverEntryOrder
1792 stdcall ZwSetEaFile(long ptr ptr long) ChpeAutoZwSetEaFile
1793 stdcall ZwSetEvent(long long) ChpeAutoZwSetEvent
1794 stdcall ZwSetEventBoostPriority(ptr) ChpeAutoZwSetEventBoostPriority
1795 stdcall ZwSetHighEventPair(ptr) ChpeAutoZwSetHighEventPair
1796 stdcall ZwSetHighWaitLowEventPair(ptr) ChpeAutoZwSetHighWaitLowEventPair
1797 stdcall ZwSetInformationDebugObject(ptr long ptr long ptr) ChpeAutoZwSetInformationDebugObject
1798 stdcall -version=0x600+ ZwSetInformationEnlistment(ptr long ptr long) ChpeAutoZwSetInformationEnlistment
1799 stdcall ZwSetInformationFile(long long long long long) ChpeAutoZwSetInformationFile
1800 stdcall ZwSetInformationJobObject(long long ptr long) ChpeAutoZwSetInformationJobObject
1801 stdcall ZwSetInformationKey(long long ptr long) ChpeAutoZwSetInformationKey
1802 stdcall ZwSetInformationObject(long long ptr long) ChpeAutoZwSetInformationObject
1803 stdcall ZwSetInformationProcess(long long long long) ChpeAutoZwSetInformationProcess
1804 stdcall -version=0x600+ ZwSetInformationResourceManager(ptr long ptr long) ChpeAutoZwSetInformationResourceManager
1805 stdcall ZwSetInformationThread(long long ptr long) ChpeAutoZwSetInformationThread
1806 stdcall ZwSetInformationToken(long long ptr long) ChpeAutoZwSetInformationToken
1807 stdcall -version=0x600+ ZwSetInformationTransaction(ptr long ptr long) ChpeAutoZwSetInformationTransaction
1808 stdcall -version=0x600+ ZwSetInformationTransactionManager(ptr long ptr long) ChpeAutoZwSetInformationTransactionManager
1809 stdcall ZwSetInformationVirtualMemory(ptr long ptr ptr ptr long) ChpeAutoZwSetInformationVirtualMemory
1810 stdcall -version=0x600+ ZwSetInformationWorkerFactory(ptr long ptr long) ChpeStubZwSetInformationWorkerFactory
1811 stdcall ZwSetIntervalProfile(long long) ChpeAutoZwSetIntervalProfile
1812 stdcall ZwSetIoCompletion(ptr long ptr long long) ChpeAutoZwSetIoCompletion
1813 stdcall ZwSetLdtEntries(long int64 long int64) ChpeAutoZwSetLdtEntries
1814 stdcall ZwSetLowEventPair(ptr) ChpeAutoZwSetLowEventPair
1815 stdcall ZwSetLowWaitHighEventPair(ptr) ChpeAutoZwSetLowWaitHighEventPair
1816 stdcall ZwSetQuotaInformationFile(ptr ptr ptr long) ChpeAutoZwSetQuotaInformationFile
1817 stdcall ZwSetSecurityObject(long long ptr) ChpeAutoZwSetSecurityObject
1818 stdcall ZwSetSystemEnvironmentValue(ptr ptr) ChpeAutoZwSetSystemEnvironmentValue
1819 stdcall ZwSetSystemEnvironmentValueEx(ptr ptr ptr ptr ptr) ChpeAutoZwSetSystemEnvironmentValueEx
1820 stdcall ZwSetSystemInformation(long ptr long) ChpeAutoZwSetSystemInformation
1821 stdcall ZwSetSystemPowerState(long long long) ChpeAutoZwSetSystemPowerState
1822 stdcall ZwSetSystemTime(ptr ptr) ChpeAutoZwSetSystemTime
1823 stdcall ZwSetThreadExecutionState(long ptr) ChpeAutoZwSetThreadExecutionState
1824 stdcall ZwSetTimer(long ptr ptr ptr long long ptr) ChpeAutoZwSetTimer
1825 stdcall ZwSetTimerResolution(long long ptr) ChpeAutoZwSetTimerResolution
1826 stdcall ZwSetUuidSeed(ptr) ChpeAutoZwSetUuidSeed
1827 stdcall ZwSetValueKey(long long long long long long) ChpeAutoZwSetValueKey
1828 stdcall ZwSetVolumeInformationFile(long ptr ptr long long) ChpeAutoZwSetVolumeInformationFile
1829 stdcall ZwShutdownSystem(long) ChpeAutoZwShutdownSystem
1830 stdcall -version=0x600+ ZwShutdownWorkerFactory(ptr ptr) ChpeStubZwShutdownWorkerFactory
1831 stdcall ZwSignalAndWaitForSingleObject(long long long ptr) ChpeAutoZwSignalAndWaitForSingleObject
1832 stdcall -version=0x600+ ZwSinglePhaseReject(ptr ptr) ChpeAutoZwSinglePhaseReject
1833 stdcall ZwStartProfile(ptr) ChpeAutoZwStartProfile
1834 stdcall ZwStopProfile(ptr) ChpeAutoZwStopProfile
1835 stdcall -version=0x602+ ZwSubscribeWnfStateChange(ptr long long ptr) ChpeAutoZwSubscribeWnfStateChange
1836 stdcall ZwSuspendProcess(ptr) ChpeAutoZwSuspendProcess
1837 stdcall ZwSuspendThread(long ptr) ChpeAutoZwSuspendThread
1838 stdcall ZwSystemDebugControl(long ptr long ptr long ptr) ChpeAutoZwSystemDebugControl
1839 stdcall ZwTerminateJobObject(ptr long) ChpeAutoZwTerminateJobObject
1840 stdcall ZwTerminateProcess(ptr long) ChpeAutoZwTerminateProcess
1841 stdcall ZwTerminateThread(ptr long) ChpeAutoZwTerminateThread
1842 stdcall ZwTestAlert() ChpeAutoZwTestAlert
1843 stdcall -version=0x600+ ZwThawRegistry() ChpeStubZwThawRegistry
1844 stdcall -version=0x600+ ZwThawTransactions() ChpeStubZwThawTransactions
1845 stdcall -version=0x600+ ZwTraceControl(long ptr long ptr long ptr) ChpeAutoZwTraceControl
1846 stdcall ZwTraceEvent(ptr long long ptr) ChpeAutoZwTraceEvent
1847 stdcall ZwTranslateFilePath(ptr long ptr long) ChpeAutoZwTranslateFilePath
1848 stdcall ZwUnloadDriver(ptr) ChpeAutoZwUnloadDriver
1849 stdcall ZwUnloadKey2(ptr long) ChpeAutoZwUnloadKey2
1850 stdcall ZwUnloadKey(long) ChpeAutoZwUnloadKey
1851 stdcall ZwUnloadKeyEx(ptr ptr) ChpeAutoZwUnloadKeyEx
1852 stdcall ZwUnlockFile(long ptr ptr ptr ptr) ChpeAutoZwUnlockFile
1853 stdcall ZwUnlockVirtualMemory(long ptr ptr long) ChpeAutoZwUnlockVirtualMemory
1854 stdcall ZwUnmapViewOfSection(long ptr) ChpeAutoZwUnmapViewOfSection
1855 stdcall -version=0x602+ ZwUnsubscribeWnfStateChange(ptr) ChpeAutoZwUnsubscribeWnfStateChange
1856 stdcall -version=0x602+ ZwUpdateWnfStateData(ptr ptr long ptr ptr long long) ChpeAutoZwUpdateWnfStateData
1857 stdcall ZwVdmControl(long ptr) ChpeAutoZwVdmControl
1858 stdcall ZwWaitForDebugEvent(ptr long ptr ptr) ChpeAutoZwWaitForDebugEvent
1859 stdcall ZwWaitForKeyedEvent(ptr ptr long ptr) ChpeAutoZwWaitForKeyedEvent
1860 stdcall ZwWaitForAlertByThreadId(ptr ptr) ChpeAutoZwWaitForAlertByThreadId
1861 stdcall ZwWaitForMultipleObjects32(long ptr long long ptr) ChpeAutoZwWaitForMultipleObjects32
1862 stdcall ZwWaitForMultipleObjects(long ptr long long ptr) ChpeAutoZwWaitForMultipleObjects
1863 stdcall ZwWaitForSingleObject(long long long) ChpeAutoZwWaitForSingleObject
1864 stdcall -version=0x600+ ZwWaitForWorkViaWorkerFactory(ptr ptr long ptr ptr) ChpeStubZwWaitForWorkViaWorkerFactory
1865 stdcall ZwWaitHighEventPair(ptr) ChpeAutoZwWaitHighEventPair
1866 stdcall ZwWaitLowEventPair(ptr) ChpeAutoZwWaitLowEventPair
1867 stdcall -version=0x600+ ZwWorkerFactoryWorkerReady(ptr) ChpeStubZwWorkerFactoryWorkerReady
1868 stdcall ZwWriteFile(long long ptr ptr ptr ptr long ptr ptr) ChpeAutoZwWriteFile
1869 stdcall ZwWriteFileGather(long long ptr ptr ptr ptr long ptr ptr) ChpeAutoZwWriteFileGather
1870 stdcall ZwWriteRequestData(ptr ptr long ptr long ptr) ChpeAutoZwWriteRequestData
1871 stdcall ZwWriteVirtualMemory(long ptr ptr long ptr) ChpeAutoZwWriteVirtualMemory
1872 stdcall ZwYieldExecution() ChpeAutoZwYieldExecution
1875 cdecl __isascii(long) ChpeAuto__isascii
1876 cdecl __iscsym(long) ChpeAuto__iscsym
1877 cdecl __iscsymf(long) ChpeAuto__iscsymf
1878 cdecl -version=0x600+ __misaligned_access() ChpeStub__misaligned_access
1879 cdecl __toascii(long) ChpeAuto__toascii
1880 cdecl -ret64 _atoi64(str) ChpeAuto_atoi64
1882 extern _fltused ntdll._fltused
1883 cdecl _i64toa(double ptr long) ChpeAuto_i64toa
1884 cdecl _i64tow(double ptr long) ChpeAuto_i64tow
1885 cdecl _itoa(long ptr long) ChpeAuto_itoa
1886 cdecl _itow(long ptr long) ChpeAuto_itow
1889 cdecl _ltoa(long ptr long) ChpeAuto_ltoa
1890 cdecl _ltow(long ptr long) ChpeAuto_ltow
1891 cdecl _memccpy(ptr ptr long long) ChpeAuto_memccpy
1892 cdecl _memicmp(str str long) ChpeAuto_memicmp
1900 cdecl _splitpath(str ptr ptr ptr ptr) ChpeAuto_splitpath
1901 cdecl _strcmpi(str str) ChpeAuto_strcmpi
1902 cdecl _stricmp(str str) ChpeAuto_stricmp
1903 cdecl _strlwr(str) ChpeAuto_strlwr
1904 cdecl _strnicmp(str str long) ChpeAuto_strnicmp
1905 cdecl _strupr(str) ChpeAuto_strupr
1907 cdecl _ui64toa(double ptr long) ChpeAuto_ui64toa
1908 cdecl _ui64tow(double ptr long) ChpeAuto_ui64tow
1909 cdecl _ultoa(long ptr long) ChpeAuto_ultoa
1910 cdecl _ultow(long ptr long) ChpeAuto_ultow
1911 cdecl _vscwprintf(wstr ptr) ChpeAuto_vscwprintf
1912 cdecl _vsnprintf(ptr long str ptr) ChpeAuto_vsnprintf
1913 cdecl _vsnprintf_s(ptr long long str ptr) ChpeAuto_vsnprintf_s
1914 cdecl _vsnwprintf(ptr long wstr ptr) ChpeAuto_vsnwprintf
1915 cdecl _vsnwprintf_s(ptr long long wstr ptr) ChpeAuto_vsnwprintf_s
1916 cdecl -version=0x600+ _vswprintf(ptr wstr ptr) ChpeAuto_vswprintf
1917 cdecl _wcsicmp(wstr wstr) ChpeAuto_wcsicmp
1918 cdecl _wcslwr(wstr) ChpeAuto_wcslwr
1919 cdecl _wcsnicmp(wstr wstr long) ChpeAuto_wcsnicmp
1920 cdecl _wcstoui64(wstr ptr long) ChpeAuto_wcstoui64
1921 cdecl _wcsupr(wstr) ChpeAuto_wcsupr
1922 cdecl _wtoi(wstr) ChpeAuto_wtoi
1923 cdecl _wtoi64(wstr) ChpeAuto_wtoi64
1924 cdecl _wtol(wstr) ChpeAuto_wtol
1925 cdecl abs(long) ChpeAutoabs
1926 cdecl atan(double) ChpeAutoatan
1927 cdecl atan2(double double) ChpeAutoatan2
1928 cdecl atoi(str) ChpeAutoatoi
1929 cdecl atol(str) ChpeAutoatol
1931 cdecl ceil(double) ChpeAutoceil
1932 cdecl cos(double) ChpeAutocos
1933 cdecl fabs(double) ChpeAutofabs
1934 cdecl floor(double) ChpeAutofloor
1935 cdecl isalnum(long) ChpeAutoisalnum
1936 cdecl isalpha(long) ChpeAutoisalpha
1937 cdecl iscntrl(long) ChpeAutoiscntrl
1938 cdecl isdigit(long) ChpeAutoisdigit
1939 cdecl isgraph(long) ChpeAutoisgraph
1940 cdecl islower(long) ChpeAutoislower
1941 cdecl isprint(long) ChpeAutoisprint
1942 cdecl ispunct(long) ChpeAutoispunct
1943 cdecl isspace(long) ChpeAutoisspace
1944 cdecl isupper(long) ChpeAutoisupper
1945 cdecl iswalpha(long) ChpeAutoiswalpha
1946 cdecl iswctype(long long) ChpeAutoiswctype
1947 cdecl iswdigit(long) ChpeAutoiswdigit
1948 cdecl iswlower(long) ChpeAutoiswlower
1949 cdecl iswspace(long) ChpeAutoiswspace
1950 cdecl iswxdigit(long) ChpeAutoiswxdigit
1951 cdecl isxdigit(long) ChpeAutoisxdigit
1952 cdecl labs(long) ChpeAutolabs
1953 cdecl log(double) ChpeAutolog
1955 cdecl mbstowcs(ptr str long) ChpeAutombstowcs
1956 cdecl memchr(ptr long long) ChpeAutomemchr
1957 cdecl memcmp(ptr ptr long) ChpeAutomemcmp
1959 cdecl memmove(ptr ptr long) ChpeAutomemmove
1960 cdecl memset(ptr long long) ChpeAutomemset
1961 cdecl pow(double double) ChpeAutopow
1963 cdecl sin(double) ChpeAutosin
1965 cdecl sqrt(double) ChpeAutosqrt
1968 cdecl strcat(str str) ChpeAutostrcat
1969 cdecl strchr(str long) ChpeAutostrchr
1970 cdecl strcmp(str str) ChpeAutostrcmp
1971 cdecl strcpy(ptr str) ChpeAutostrcpy
1972 cdecl -version=0x600+ strcpy_s(ptr long str) ChpeAutostrcpy_s
1973 cdecl -version=0x600+ strcat_s(ptr long str) ChpeAutostrcat_s
1974 cdecl -version=0x600+ strncpy_s(ptr long str long) ChpeAutostrncpy_s
1975 cdecl strcspn(str str) ChpeAutostrcspn
1976 cdecl strlen(str) ChpeAutostrlen
1977 cdecl strncat(str str long) ChpeAutostrncat
1978 cdecl strncmp(str str long) ChpeAutostrncmp
1979 cdecl strncpy(ptr str long) ChpeAutostrncpy
1980 cdecl strpbrk(str str) ChpeAutostrpbrk
1981 cdecl strrchr(str long) ChpeAutostrrchr
1982 cdecl strspn(str str) ChpeAutostrspn
1983 cdecl strstr(str str) ChpeAutostrstr
1984 cdecl strtol(str ptr long) ChpeAutostrtol
1985 cdecl strtoul(str ptr long) ChpeAutostrtoul
1988 cdecl tan(double) ChpeAutotan
1989 cdecl tolower(long) ChpeAutotolower
1990 cdecl toupper(long) ChpeAutotoupper
1991 cdecl towlower(long) ChpeAutotowlower
1992 cdecl towupper(long) ChpeAutotowupper
1995 cdecl vsprintf(ptr str ptr) ChpeAutovsprintf
1996 cdecl wcscat(wstr wstr) ChpeAutowcscat
1997 cdecl wcschr(wstr long) ChpeAutowcschr
1998 cdecl wcscmp(wstr wstr) ChpeAutowcscmp
1999 cdecl wcscpy(ptr wstr) ChpeAutowcscpy
2000 cdecl wcscspn(wstr wstr) ChpeAutowcscspn
2001 cdecl wcslen(wstr) ChpeAutowcslen
2002 cdecl wcsncat(wstr wstr long) ChpeAutowcsncat
2003 cdecl wcsncmp(wstr wstr long) ChpeAutowcsncmp
2004 cdecl wcsncpy(ptr wstr long) ChpeAutowcsncpy
2005 cdecl wcsnlen(wstr long) ChpeAutowcsnlen
2006 cdecl wcspbrk(wstr wstr) ChpeAutowcspbrk
2007 cdecl wcsrchr(wstr long) ChpeAutowcsrchr
2008 cdecl wcsspn(wstr wstr) ChpeAutowcsspn
2009 cdecl wcsstr(wstr wstr) ChpeAutowcsstr
2010 cdecl wcstol(wstr ptr long) ChpeAutowcstol
2011 cdecl wcstombs(ptr ptr long) ChpeAutowcstombs
2012 cdecl wcstoul(wstr ptr long) ChpeAutowcstoul
2018 stdcall -version=0x602+ -ret64 RtlGetSystemTimePrecise() ChpeAutoRtlGetSystemTimePrecise
# END typed bridge wrappers
