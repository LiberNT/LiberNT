# PROJECT:     ReactOS ARM64EC runtime
# PURPOSE:     Native NTDLL bridge exports for emulated AMD64 imports
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>

37 stdcall DbgBreakPoint() ChpeDbgBreakPoint
38 varargs DbgPrint(str) ChpeDbgPrint
39 varargs DbgPrintEx(long long str) ChpeDbgPrintEx
58 stdcall -version=0x600+ EtwEventActivityIdControl(long ptr) ChpeEtwEventActivityIdControl
61 stdcall -version=0x600+ EtwEventRegister(ptr ptr ptr ptr) ChpeEtwEventRegister
63 stdcall -version=0x600+ EtwEventUnregister(int64) ChpeEtwEventUnregister
64 stdcall -version=0x600+ EtwEventWrite(int64 ptr long ptr) ChpeEtwEventWrite
70 stdcall -version=0x600+ EtwEventWriteTransfer(int64 ptr ptr ptr long ptr) ChpeEtwEventWriteTransfer
1857 cdecl __C_specific_handler(ptr long ptr ptr) ChpeCSpecificHandler
1996 stdcall ChpeDispatchExceptionNative(ptr ptr)
104 stdcall KiUserExceptionDispatcher(ptr ptr) ChpeKiUserExceptionDispatcher
107 stdcall LdrAccessResource(ptr ptr ptr ptr) ChpeLdrAccessResource
109 stdcall LdrAddRefDll(long ptr) ChpeLdrAddRefDll
111 stdcall LdrEnumResources(ptr ptr long ptr ptr) ChpeLdrEnumResources
113 stdcall LdrFindEntryForAddress(ptr ptr) ChpeLdrFindEntryForAddress
114 stdcall LdrFindResourceDirectory_U(ptr ptr long ptr) ChpeLdrFindResourceDirectory_U
116 stdcall LdrFindResource_U(ptr ptr long ptr) ChpeLdrFindResource_U
118 stdcall LdrGetDllHandle(wstr ptr ptr ptr) ChpeLdrGetDllHandle
119 stdcall LdrGetDllHandleEx(long wstr ptr ptr ptr) ChpeLdrGetDllHandleEx
123 stdcall LdrGetProcedureAddress(ptr ptr long ptr) ChpeLdrGetProcedureAddress
130 stdcall LdrLoadDll(wstr long ptr ptr) ChpeLdrLoadDll
139 stdcall LdrQueryProcessModuleInformation(ptr long ptr) ChpeLdrQueryProcessModuleInformation
142 stdcall -version=0x602+ LdrResolveDelayLoadedAPI(ptr ptr ptr ptr ptr long) ChpeLdrResolveDelayLoadedAPI
143 stdcall -version=0x602+ LdrResolveDelayLoadsFromDll(ptr str long) ChpeLdrResolveDelayLoadsFromDll
156 stdcall LdrUnloadDll(ptr) ChpeLdrUnloadDll
186 stdcall NtAdjustPrivilegesToken(long long ptr long ptr ptr) ChpeAutoNtAdjustPrivilegesToken
195 stdcall NtAllocateVirtualMemory(long ptr ptr ptr long long) ChpeAutoNtAllocateVirtualMemory
196 stdcall NtAllocateVirtualMemoryEx(long ptr ptr long long ptr long) ChpeAutoNtAllocateVirtualMemoryEx
190 stdcall NtAlertThreadByThreadId(long) ChpeAutoNtAlertThreadByThreadId
230 stdcall NtClose(long) ChpeAutoNtClose
241 stdcall NtContinue(ptr long) ChpeNtContinue
242 stdcall -version=0xA00+ NtContinueEx(ptr ptr) ChpeNtContinueEx
249 stdcall NtCreateFile(ptr long ptr ptr ptr long long long long ptr long) ChpeAutoNtCreateFile
261 stdcall NtCreateNamedPipeFile(ptr long ptr ptr long long long long long long long long long ptr) ChpeAutoNtCreateNamedPipeFile
296 stdcall NtDeviceIoControlFile(long long ptr ptr ptr long ptr long ptr long) ChpeAutoNtDeviceIoControlFile
315 stdcall -version=0x600+ NtFlushProcessWriteBuffers() ChpeAutoNtFlushProcessWriteBuffers
313 stdcall NtFlushInstructionCache(long ptr long) ChpeAutoNtFlushInstructionCache
319 stdcall NtFreeVirtualMemory(long ptr ptr long) ChpeAutoNtFreeVirtualMemory
323 stdcall NtGetContextThread(long ptr) ChpeNtGetContextThread
329 stdcall -version=0x600+ NtGetNextThread(ptr ptr long long long ptr) ChpeAutoNtGetNextThread
358 stdcall NtMapViewOfSection(long long ptr long ptr ptr ptr long long long) ChpeAutoNtMapViewOfSection
359 stdcall NtMapViewOfSectionEx(long long ptr ptr ptr long long ptr long) ChpeNtMapViewOfSectionEx
364 stdcall NtNotifyChangeKey(ptr ptr ptr ptr ptr long long ptr long long) ChpeAutoNtNotifyChangeKey
365 stdcall NtNotifyChangeMultipleKeys(ptr long ptr ptr ptr ptr ptr long long ptr long long) ChpeAutoNtNotifyChangeMultipleKeys
370 stdcall NtOpenFile(ptr long ptr ptr long long) ChpeAutoNtOpenFile
405 stdcall NtProtectVirtualMemory(long ptr ptr long ptr) ChpeAutoNtProtectVirtualMemory
413 stdcall NtQueryDirectoryFile(long long ptr ptr ptr ptr long long long ptr long) ChpeAutoNtQueryDirectoryFile
423 stdcall NtQueryInformationFile(long ptr ptr long long) ChpeAutoNtQueryInformationFile
436 stdcall NtQueryKey(long long ptr long ptr) ChpeAutoNtQueryKey
256 stdcall NtCreateKey(ptr long ptr long ptr long ptr) ChpeAutoNtCreateKey
428 stdcall NtQueryInformationThread(long long ptr long ptr) ChpeAutoNtQueryInformationThread
426 stdcall NtQueryInformationProcess(ptr long ptr long ptr) ChpeAutoNtQueryInformationProcess
886 stdcall RtlFormatCurrentUserKeyPath(ptr) ChpeRtlFormatCurrentUserKeyPath
897 stdcall RtlFreeUnicodeString(ptr) ChpeRtlFreeUnicodeString
373 stdcall NtOpenKey(ptr long ptr) ChpeAutoNtOpenKey
834 stdcall NtOpenKeyEx(ptr long ptr long) ChpeAutoNtOpenKeyEx
457 stdcall NtQueryValueKey(long ptr long ptr long ptr) ChpeAutoNtQueryValueKey
554 stdcall NtSetValueKey(long ptr long long ptr long) ChpeAutoNtSetValueKey
290 stdcall NtDeleteKey(long) ChpeAutoNtDeleteKey
293 stdcall NtDeleteValueKey(long ptr) ChpeAutoNtDeleteValueKey
302 stdcall NtEnumerateKey(long long long ptr long ptr) ChpeAutoNtEnumerateKey
305 stdcall NtEnumerateValueKey(long long long ptr long ptr) ChpeAutoNtEnumerateValueKey
314 stdcall NtFlushKey(long) ChpeAutoNtFlushKey
251 stdcall NtCreateWaitCompletionPacket(ptr long ptr) ChpeAutoNtCreateWaitCompletionPacket
252 stdcall NtAssociateWaitCompletionPacket(long long long ptr ptr long long ptr) ChpeAutoNtAssociateWaitCompletionPacket
253 stdcall NtCancelWaitCompletionPacket(long long) ChpeAutoNtCancelWaitCompletionPacket
459 stdcall NtQueryVolumeInformationFile(long ptr ptr long long) ChpeAutoNtQueryVolumeInformationFile
440 stdcall NtQueryObject(long long long long long) ChpeAutoNtQueryObject
452 stdcall NtQuerySystemInformation(long ptr long ptr) ChpeAutoNtQuerySystemInformation
933 stdcall RtlGetNativeSystemInformation(long ptr long ptr) ChpeRtlGetNativeSystemInformation
458 stdcall NtQueryVirtualMemory(long ptr long ptr long ptr) ChpeAutoNtQueryVirtualMemory
465 stdcall NtRaiseException(ptr ptr long) ChpeNtRaiseException
467 stdcall NtReadFile(long long ptr ptr ptr ptr long ptr ptr) ChpeAutoNtReadFile
471 stdcall NtReadVirtualMemory(long ptr ptr long ptr) ChpeAutoNtReadVirtualMemory
512 stdcall NtSetContextThread(long ptr) ChpeNtSetContextThread
563 stdcall NtSuspendProcess(ptr) ChpeAutoNtSuspendProcess
564 stdcall NtSuspendThread(ptr ptr) ChpeAutoNtSuspendThread
501 stdcall NtResumeThread(ptr ptr) ChpeAutoNtResumeThread
567 stdcall NtTerminateProcess(long long) ChpeAutoNtTerminateProcess
568 stdcall NtTerminateThread(long long) ChpeAutoNtTerminateThread
581 stdcall NtUnmapViewOfSection(long ptr) ChpeAutoNtUnmapViewOfSection
582 stdcall NtUnmapViewOfSectionEx(long ptr long) ChpeNtUnmapViewOfSectionEx
588 stdcall NtWaitForAlertByThreadId(ptr ptr) ChpeAutoNtWaitForAlertByThreadId
596 stdcall NtWriteFile(long long ptr ptr ptr ptr long ptr ptr) ChpeAutoNtWriteFile
599 stdcall NtWriteVirtualMemory(long ptr ptr long ptr) ChpeAutoNtWriteVirtualMemory
634 stdcall RtlAddFunctionTable(ptr long long) ChpeRtlAddFunctionTable
635 stdcall RtlAddGrowableFunctionTable(ptr ptr long long long long) ChpeRtlAddGrowableFunctionTable
641 stdcall RtlAddVectoredContinueHandler(long ptr) ChpeRtlAddVectoredContinueHandler
642 stdcall RtlAddVectoredExceptionHandler(long ptr) ChpeRtlAddVectoredExceptionHandler
648 stdcall RtlAllocateHeap(ptr long ptr) ChpeRtlAllocateHeap
908 stdcall RtlGetCurrentPeb() ChpeRtlGetCurrentPeb
940 stdcall RtlGetProcessHeaps(long ptr) ChpeRtlGetProcessHeaps
1120 stdcall RtlQueryEnvironmentVariable(ptr ptr long ptr long ptr) ChpeRtlQueryEnvironmentVariable
612 stdcall RtlAcquirePrivilege(ptr long long ptr) ChpeRtlAcquirePrivilege
615 stdcall RtlAcquireSRWLockExclusive(ptr) ChpeRtlAcquireSRWLockExclusive
616 stdcall RtlAcquireSRWLockShared(ptr) ChpeRtlAcquireSRWLockShared
670 stdcall RtlCaptureContext(ptr) ChpeRtlCaptureContextX64
671 stdcall RtlCaptureStackBackTrace(long long ptr ptr) ChpeRtlCaptureStackBackTrace
686 stdcall RtlCompareMemory(ptr ptr long) ChpeRtlCompareMemory
689 stdcall RtlCompareUnicodeString(ptr ptr long) ChpeRtlCompareUnicodeString
717 stdcall RtlCopyUnicodeString(ptr ptr) ChpeRtlCopyUnicodeString
761 stdcall RtlDecodePointer(ptr) ChpeRtlDecodePointer
762 stdcall RtlDecodeSystemPointer(ptr) ChpeRtlDecodeSystemPointer
772 stdcall RtlDeleteCriticalSection(ptr) ChpeRtlDeleteCriticalSection
775 cdecl RtlDeleteFunctionTable(ptr) ChpeRtlDeleteFunctionTable
776 stdcall RtlDeleteGrowableFunctionTable(ptr) ChpeRtlDeleteGrowableFunctionTable
816 stdcall RtlEnterCriticalSection(ptr) ChpeRtlEnterCriticalSection
814 stdcall RtlEncodePointer(ptr) ChpeRtlEncodePointer
815 stdcall RtlEncodeSystemPointer(ptr) ChpeRtlEncodeSystemPointer
854 stdcall RtlExitUserThread(long) ChpeRtlExitUserThread
859 stdcall RtlFillMemory(ptr long long) ChpeRtlFillMemory
881 stdcall RtlFlsAlloc(ptr ptr) ChpeRtlFlsAlloc
882 stdcall RtlFlsFree(long) ChpeRtlFlsFree
883 stdcall RtlFlsGetValue(long ptr) ChpeRtlFlsGetValue
884 stdcall RtlFlsSetValue(long ptr) ChpeRtlFlsSetValue
892 stdcall RtlFreeHeap(long long long) ChpeRtlFreeHeap
924 stdcall RtlGetFunctionTableListHead() ChpeRtlGetFunctionTableListHead
927 stdcall RtlGetLastNtStatus() ChpeRtlGetLastNtStatus
928 stdcall RtlGetLastWin32Error() ChpeRtlGetLastWin32Error
909 stdcall RtlGetCurrentProcessorNumber() ChpeRtlGetCurrentProcessorNumber
910 stdcall -version=0x601+ RtlGetCurrentProcessorNumberEx(ptr) ChpeRtlGetCurrentProcessorNumberEx
941 stdcall -version=0x600+ RtlGetProductInfo(long long long long ptr) ChpeRtlGetProductInfo
954 stdcall RtlGetVersion(ptr) ChpeRtlGetVersion
1005 stdcall RtlGrowFunctionTable(ptr long) ChpeRtlGrowFunctionTable
978 stdcall RtlInitUnicodeString(ptr wstr) ChpeRtlInitUnicodeString
983 stdcall -version=0x600+ RtlInitializeConditionVariable(ptr) ChpeRtlInitializeConditionVariable
985 stdcall RtlInitializeCriticalSection(ptr) ChpeRtlInitializeCriticalSection
996 stdcall RtlInitializeSListHead(ptr) ChpeRtlInitializeSListHead
997 stdcall -version=0x600+ RtlInitializeSRWLock(ptr) ChpeRtlInitializeSRWLock
1004 cdecl RtlInstallFunctionTableCallback(double double long ptr ptr ptr) ChpeRtlInstallFunctionTableCallback
1009 stdcall RtlInterlockedFlushSList(ptr) ChpeRtlInterlockedFlushSList
1010 stdcall RtlInterlockedPopEntrySList(ptr) ChpeRtlInterlockedPopEntrySList
1011 stdcall RtlInterlockedPushEntrySList(ptr ptr) ChpeRtlInterlockedPushEntrySList
1012 stdcall RtlInterlockedPushListSList(ptr ptr ptr long) ChpeRtlInterlockedPushListSList
1013 stdcall -version=0x602+ RtlInterlockedPushListSListEx(ptr ptr ptr long) ChpeRtlInterlockedPushListSListEx
1042 stdcall RtlIsProcessorFeaturePresent(long) ChpeRtlIsProcessorFeaturePresent
1051 stdcall RtlLeaveCriticalSection(ptr) ChpeRtlLeaveCriticalSection
1073 stdcall RtlLookupFunctionEntry(long ptr ptr) ChpeRtlLookupFunctionEntry
1074 stdcall RtlLookupFunctionTable(int64 ptr ptr) ChpeRtlLookupFunctionTable
1064 stdcall -version=0xA00+ RtlLogUnexpectedCodepath(ptr) ChpeRtlLogUnexpectedCodepath
1078 stdcall RtlMoveMemory(ptr ptr long) ChpeRtlMoveMemory
1092 stdcall RtlNtStatusToDosError(long) ChpeRtlNtStatusToDosError
1105 stdcall RtlPcToFileHeader(ptr ptr) ChpeRtlPcToFileHeader
1117 stdcall RtlQueryDepthSList(ptr) ChpeRtlQueryDepthSList
1139 stdcall -norelay RtlRaiseException(ptr) ChpeRtlRaiseException
1140 stdcall RtlRaiseStatus(long) ChpeRtlRaiseStatus
1141 stdcall RtlRandom(ptr) ChpeRtlRandom
1143 stdcall RtlReAllocateHeap(long long ptr long) ChpeRtlReAllocateHeap
1155 stdcall RtlReleasePrivilege(ptr) ChpeRtlReleasePrivilege
1158 stdcall RtlReleaseSRWLockExclusive(ptr) ChpeRtlReleaseSRWLockExclusive
1159 stdcall RtlReleaseSRWLockShared(ptr) ChpeRtlReleaseSRWLockShared
1162 stdcall RtlRemoveVectoredContinueHandler(ptr) ChpeRtlRemoveVectoredContinueHandler
1163 stdcall RtlRemoveVectoredExceptionHandler(ptr) ChpeRtlRemoveVectoredExceptionHandler
1169 stdcall RtlRestoreContext(ptr ptr) ChpeRtlRestoreContext
1170 stdcall RtlRestoreLastWin32Error(long) ChpeRtlRestoreLastWin32Error
1177 stdcall -version=0x600+ RtlRunOnceExecuteOnce(ptr ptr ptr ptr) ChpeRtlRunOnceExecuteOnce
1178 stdcall -version=0x600+ RtlRunOnceInitialize(ptr) ChpeRtlRunOnceInitialize
1229 stdcall RtlSizeHeap(long long ptr) ChpeRtlSizeHeap
1190 stdcall RtlSetCriticalSectionSpinCount(ptr long) ChpeRtlSetCriticalSectionSpinCount
1204 stdcall RtlSetLastWin32Error(long) ChpeRtlSetLastWin32Error
1256 stdcall RtlTryAcquireSRWLockExclusive(ptr) ChpeRtlTryAcquireSRWLockExclusive
1257 stdcall RtlTryAcquireSRWLockShared(ptr) ChpeRtlTryAcquireSRWLockShared
1258 stdcall RtlTryEnterCriticalSection(ptr) ChpeRtlTryEnterCriticalSection
1263 stdcall -version=0x601+ RtlUTF8ToUnicodeN(ptr long ptr str long) ChpeRtlUTF8ToUnicodeN
1281 stdcall RtlUnwind(ptr ptr ptr ptr) ChpeRtlUnwind
1282 stdcall RtlUnwindEx(ptr ptr ptr ptr ptr ptr) ChpeRtlUnwindEx
1306 stdcall RtlVirtualUnwind(long int64 int64 ptr ptr ptr ptr ptr) ChpeRtlVirtualUnwind
1308 stdcall -version=0x602+ RtlWaitOnAddress(ptr ptr long ptr) ChpeRtlWaitOnAddress
1310 stdcall -version=0x602+ RtlWakeAddressAll(ptr) ChpeRtlWakeAddressAll
1311 stdcall -version=0x602+ RtlWakeAddressSingle(ptr) ChpeRtlWakeAddressSingle
1309 stdcall RtlWakeAllConditionVariable(ptr) ChpeRtlWakeAllConditionVariable
1312 stdcall RtlWakeConditionVariable(ptr) ChpeRtlWakeConditionVariable
1335 stdcall RtlZeroMemory(ptr long) ChpeRtlZeroMemory
1389 stdcall -version=0x600+ TpCallbackLeaveCriticalSectionOnCompletion(ptr ptr) ChpeTpCallbackLeaveCriticalSectionOnCompletion
1391 stdcall -version=0x600+ TpCallbackReleaseMutexOnCompletion(ptr ptr) ChpeTpCallbackReleaseMutexOnCompletion
1392 stdcall -version=0x600+ TpCallbackReleaseSemaphoreOnCompletion(ptr ptr long) ChpeTpCallbackReleaseSemaphoreOnCompletion
1395 stdcall -version=0x600+ TpCallbackSetEventOnCompletion(ptr ptr) ChpeTpCallbackSetEventOnCompletion
1396 stdcall -version=0x600+ TpCallbackUnloadDllOnCompletion(ptr ptr) ChpeTpCallbackUnloadDllOnCompletion
1397 stdcall -version=0x600+ TpCancelAsyncIoOperation(ptr) ChpeTpCancelAsyncIoOperation
1402 stdcall -version=0x600+ TpDisassociateCallback(ptr) ChpeTpDisassociateCallback
1403 stdcall -version=0x600+ TpIsTimerSet(ptr) ChpeTpIsTimerSet
1404 stdcall -version=0x600+ TpPostWork(ptr) ChpeTpPostWork
1407 stdcall -version=0x600+ TpReleaseCleanupGroup(ptr) ChpeTpReleaseCleanupGroup
1408 stdcall -version=0x600+ TpReleaseCleanupGroupMembers(ptr long ptr) ChpeTpReleaseCleanupGroupMembers
1409 stdcall -version=0x600+ TpReleaseIoCompletion(ptr) ChpeTpReleaseIoCompletion
1410 stdcall -version=0x600+ TpReleasePool(ptr) ChpeTpReleasePool
1411 stdcall TpReleaseTimer(ptr) ChpeTpReleaseTimer
1412 stdcall TpReleaseWait(ptr) ChpeTpReleaseWait
1413 stdcall -version=0x600+ TpReleaseWork(ptr) ChpeTpReleaseWork
1414 stdcall -version=0x600+ TpSetPoolMaxThreads(ptr long) ChpeTpSetPoolMaxThreads
1415 stdcall -version=0x600+ TpSetPoolMinThreads(ptr long) ChpeTpSetPoolMinThreads
1417 stdcall TpSetTimer(ptr ptr long long) ChpeTpSetTimer
1418 stdcall -version=0x602+ TpSetTimerEx(ptr ptr long long) ChpeTpSetTimerEx
1419 stdcall TpSetWait(ptr long ptr) ChpeTpSetWait
1420 stdcall -version=0x602+ TpSetWaitEx(ptr long ptr ptr) ChpeTpSetWaitEx
1422 stdcall -version=0x600+ TpStartAsyncIoOperation(ptr) ChpeTpStartAsyncIoOperation
1424 stdcall -version=0x600+ TpWaitForIoCompletion(ptr long) ChpeTpWaitForIoCompletion
1425 stdcall TpWaitForTimer(ptr long) ChpeTpWaitForTimer
1426 stdcall -version=0x600+ TpWaitForWait(ptr long) ChpeTpWaitForWait
1427 stdcall -version=0x600+ TpWaitForWork(ptr long) ChpeTpWaitForWork
1428 stdcall -ret64 VerSetConditionMask(double long long) ChpeVerSetConditionMask
1879 varargs _snprintf(ptr long str) ChpeSnprintf
1881 varargs _snwprintf(ptr long wstr) ChpeSnwprintf
1889 varargs _swprintf(ptr wstr) ChpeSwprintf
1946 varargs sprintf(ptr str) ChpeSprintf
1967 varargs swprintf(ptr wstr) ChpeSwprintf
1858 cdecl __chkstk() ChpeChkStk
1872 cdecl _local_unwind(ptr ptr) ChpeLocalUnwind
1940 cdecl memcpy(ptr ptr long) ChpeMemcpy
1997 stdcall ChpeEmulationDispatch(ptr)
1325 stdcall RtlWow64GetThreadSelectorEntry(ptr ptr long ptr) ChpeRtlWow64GetThreadSelectorEntry

273 stdcall NtCreateThread(ptr long ptr long ptr ptr ptr long) ChpeNtCreateThread
322 stdcall NtFsControlFile(long long ptr ptr ptr long ptr long ptr long) ChpeAutoNtFsControlFile
551 stdcall NtSetTimer(long ptr ptr ptr long long ptr) ChpeAutoNtSetTimer
752 stdcall RtlCreateUserThread(long ptr long long ptr ptr ptr ptr ptr) ChpeRtlCreateUserThread
764 stdcall RtlDecompressFragment(long ptr long ptr long long ptr ptr) ChpeRtlDecompressFragment
900 stdcall RtlGenerate8dot3Name(ptr long ptr ptr) ChpeRtlGenerate8dot3Name
930 stdcall RtlGetLengthWithoutLastFullDosOrNtPathElement(long ptr ptr) ChpeRtlGetLengthWithoutLastFullDosOrNtPathElement
950 stdcall RtlGetUnloadEventTrace() ChpeRtlGetUnloadEventTrace
1138 stdcall RtlQueueWorkItem(ptr ptr long) ChpeRtlQueueWorkItem
1151 stdcall RtlRegisterWait(ptr ptr ptr ptr long long) ChpeRtlRegisterWait
1337 stdcall RtlpApplyLengthFunction(long long ptr ptr) ChpeRtlpApplyLengthFunction
1973 stdcall vDbgPrintEx(long long str ptr) ChpevDbgPrintEx
1974 stdcall vDbgPrintExWithPrefix(str long long str ptr) ChpevDbgPrintExWithPrefix
833 stdcall LdrGetDllFullName(ptr ptr) ChpeLdrGetDllFullName
870 stdcall RtlFindExportedRoutineByName(ptr str) ChpeRtlFindExportedRoutineByName
836 stdcall RtlIsEcCode(ptr) ChpeRtlIsEcCode
845 stdcall RtlLocateExtendedFeature(ptr long ptr) ChpeRtlLocateExtendedFeature
846 stdcall RtlLocateExtendedFeature2(ptr long ptr ptr) ChpeRtlLocateExtendedFeature2
848 stdcall RtlQueryPerformanceCounter(ptr) ChpeRtlQueryPerformanceCounter
849 stdcall RtlQueryPerformanceFrequency(ptr) ChpeRtlQueryPerformanceFrequency
851 stdcall RtlQuerySystemTime(ptr) ChpeRtlQuerySystemTime
852 stdcall RtlSystemTimeToTimeFields(ptr ptr) ChpeRtlSystemTimeToTimeFields
40 varargs DbgPrintReturnControlC(str) ChpeDbgPrintReturnControlC
54 stdcall DbgUserBreakPoint() ChpeDbgUserBreakPoint
86 varargs EtwTraceMessage(int64 long ptr long) ChpeEtwTraceMessage
1297 stdcall RtlUserThreadStart(long long) ChpeRtlUserThreadStart
1948 varargs sscanf(str str) ChpeSscanf
1993 cdecl ChpeVsscanf(str str ptr) ChpeAutoVsscanf
1994 stdcall ChpeVDbgPrintReturnControlC(str ptr) ChpeAutoVDbgPrintReturnControlC
1865 cdecl -private _errno() ChpeErrno
1871 cdecl _lfind(ptr ptr ptr long ptr) ChpeLfind
1912 cdecl bsearch(ptr ptr long long ptr) ChpeBsearch
1944 cdecl qsort(ptr long long ptr) ChpeQsort
1877 cdecl _setjmp(ptr ptr) ChpeSetJmpX64
1878 cdecl _setjmpex(ptr ptr) ChpeSetJmpX64
1936 cdecl longjmp(ptr long) ChpeLongJmp
972 stdcall RtlInitMemoryStream(ptr) ChpeRtlInitMemoryStream
974 stdcall RtlInitOutOfProcessMemoryStream(ptr) ChpeRtlInitOutOfProcessMemoryStream
1153 stdcall RtlReleaseMemoryStream(ptr) ChpeRtlReleaseMemoryStream
95 stdcall ExpInterlockedPopEntrySListEnd() ChpeUnsupportedKernelEntry
97 stdcall ExpInterlockedPopEntrySListFault() ChpeUnsupportedKernelEntry
99 stdcall ExpInterlockedPopEntrySListResume() ChpeUnsupportedKernelEntry
102 stdcall KiUserApcDispatcher(ptr ptr ptr ptr) ChpeUnsupportedApcEntry
106 stdcall KiUserEmulationDispatcher(ptr) ChpeUnsupportedEmulationEntry

# BEGIN typed bridge wrappers (generate_chpe_bridge.py)
1 stdcall -version=0x600+ RtlFlushHeaps() ChpeAutoRtlFlushHeaps
2 stdcall -version=0x602+ RtlQueryWnfStateData(ptr int64 ptr ptr ptr long) ChpeAutoRtlQueryWnfStateData
3 stdcall -version=0x602+ RtlSubscribeWnfStateChangeNotification(ptr int64 long ptr ptr ptr long long long) ChpeAutoRtlSubscribeWnfStateChangeNotification
4 stdcall -version=0x602+ RtlUnsubscribeWnfNotificationWaitForCompletion(ptr) ChpeAutoRtlUnsubscribeWnfNotificationWaitForCompletion
5 stdcall -version=0x600+ A_SHAFinal(ptr ptr) ChpeAutoA_SHAFinal
6 stdcall -version=0x600+ A_SHAInit(ptr) ChpeAutoA_SHAInit
7 stdcall -version=0x600+ A_SHAUpdate(ptr ptr long) ChpeAutoA_SHAUpdate
8 stdcall -version=0x600+ AlpcAdjustCompletionListConcurrencyCount(ptr long) ChpeAutoAlpcAdjustCompletionListConcurrencyCount
9 stdcall -version=0x600+ AlpcFreeCompletionListMessage(ptr ptr) ChpeAutoAlpcFreeCompletionListMessage
10 stdcall -version=0x600+ AlpcGetCompletionListLastMessageInformation(ptr ptr ptr) ChpeAutoAlpcGetCompletionListLastMessageInformation
11 stdcall -version=0x600+ AlpcGetCompletionListMessageAttributes(ptr ptr) ChpeAutoAlpcGetCompletionListMessageAttributes
12 stdcall -version=0x600+ AlpcGetHeaderSize(long) ChpeAutoAlpcGetHeaderSize
13 stdcall -version=0x600+ AlpcGetMessageAttribute(ptr long) ChpeAutoAlpcGetMessageAttribute
14 stdcall -version=0x600+ AlpcGetMessageFromCompletionList(ptr ptr) ChpeAutoAlpcGetMessageFromCompletionList
15 stdcall -version=0x600+ AlpcGetOutstandingCompletionListMessageCount(ptr) ChpeAutoAlpcGetOutstandingCompletionListMessageCount
16 stdcall -version=0x600+ AlpcInitializeMessageAttribute(long ptr long ptr) ChpeAutoAlpcInitializeMessageAttribute
17 stdcall -version=0x600+ AlpcMaxAllowedMessageLength() ChpeAutoAlpcMaxAllowedMessageLength
18 stdcall -version=0x600+ AlpcRegisterCompletionList(ptr ptr long long long) ChpeAutoAlpcRegisterCompletionList
19 stdcall -version=0x600+ AlpcRegisterCompletionListWorkerThread(ptr) ChpeAutoAlpcRegisterCompletionListWorkerThread
20 stdcall -version=0x600+ AlpcRundownCompletionList(ptr) ChpeAutoAlpcRundownCompletionList
21 stdcall -version=0x600+ AlpcUnregisterCompletionList(ptr) ChpeAutoAlpcUnregisterCompletionList
22 stdcall -version=0x600+ AlpcUnregisterCompletionListWorkerThread(ptr) ChpeAutoAlpcUnregisterCompletionListWorkerThread
23 stdcall CsrAllocateCaptureBuffer(long long) ChpeAutoCsrAllocateCaptureBuffer
24 stdcall CsrAllocateMessagePointer(ptr long ptr) ChpeAutoCsrAllocateMessagePointer
25 stdcall CsrCaptureMessageBuffer(ptr ptr long ptr) ChpeAutoCsrCaptureMessageBuffer
26 stdcall CsrCaptureMessageMultiUnicodeStringsInPlace(ptr long ptr) ChpeAutoCsrCaptureMessageMultiUnicodeStringsInPlace
27 stdcall CsrCaptureMessageString(ptr str long long ptr) ChpeAutoCsrCaptureMessageString
28 stdcall CsrCaptureTimeout(long ptr) ChpeAutoCsrCaptureTimeout
29 stdcall CsrClientCallServer(ptr ptr long long) ChpeAutoCsrClientCallServer
30 stdcall CsrClientConnectToServer(str long ptr ptr ptr) ChpeAutoCsrClientConnectToServer
31 stdcall CsrFreeCaptureBuffer(ptr) ChpeAutoCsrFreeCaptureBuffer
32 stdcall CsrGetProcessId() ChpeAutoCsrGetProcessId
33 stdcall CsrIdentifyAlertableThread() ChpeAutoCsrIdentifyAlertableThread
34 stdcall -version=0x502+ CsrNewThread() ChpeAutoCsrNewThread
35 stdcall CsrSetPriorityClass(ptr ptr) ChpeAutoCsrSetPriorityClass
36 stdcall -version=0x600+ CsrVerifyRegion(ptr long) ChpeStubCsrVerifyRegion
41 stdcall DbgPrompt(ptr ptr long) ChpeAutoDbgPrompt
42 stdcall DbgQueryDebugFilterState(long long) ChpeAutoDbgQueryDebugFilterState
43 stdcall DbgSetDebugFilterState(long long long) ChpeAutoDbgSetDebugFilterState
44 stdcall DbgUiConnectToDbg() ChpeAutoDbgUiConnectToDbg
45 stdcall DbgUiContinue(ptr long) ChpeAutoDbgUiContinue
46 stdcall DbgUiConvertStateChangeStructure(ptr ptr) ChpeAutoDbgUiConvertStateChangeStructure
47 stdcall DbgUiDebugActiveProcess(ptr) ChpeAutoDbgUiDebugActiveProcess
48 stdcall DbgUiGetThreadDebugObject() ChpeAutoDbgUiGetThreadDebugObject
49 stdcall DbgUiIssueRemoteBreakin(ptr) ChpeAutoDbgUiIssueRemoteBreakin
50 stdcall DbgUiRemoteBreakin() ChpeAutoDbgUiRemoteBreakin
51 stdcall DbgUiSetThreadDebugObject(ptr) ChpeAutoDbgUiSetThreadDebugObject
52 stdcall DbgUiStopDebugging(ptr) ChpeAutoDbgUiStopDebugging
53 stdcall DbgUiWaitStateChange(ptr ptr) ChpeAutoDbgUiWaitStateChange
55 stdcall  EtwCreateTraceInstanceId(ptr ptr) ChpeStubEtwCreateTraceInstanceId
56 stdcall -version=0x600+ EtwDeliverDataBlock(long) ChpeStubEtwDeliverDataBlock
57 stdcall -version=0x600+ EtwEnumerateProcessRegGuids(ptr long ptr) ChpeStubEtwEnumerateProcessRegGuids
59 stdcall -version=0x600+ EtwEventEnabled(int64 ptr) ChpeAutoEtwEventEnabled
60 stdcall -version=0x600+ EtwEventProviderEnabled(int64 long int64) ChpeAutoEtwEventProviderEnabled
62 stdcall -version=0x600+ EtwEventSetInformation(int64 long ptr long) ChpeAutoEtwEventSetInformation
65 stdcall -version=0x601+ EtwEventWriteEx(int64 ptr int64 long ptr ptr long ptr) ChpeAutoEtwEventWriteEx
66 stdcall -version=0x600+ EtwEventWriteEndScenario(long ptr long long) ChpeStubEtwEventWriteEndScenario
67 stdcall -version=0x600+ EtwEventWriteFull(long long long long long long long) ChpeStubEtwEventWriteFull
68 stdcall -version=0x600+ EtwEventWriteStartScenario(long ptr long long) ChpeStubEtwEventWriteStartScenario
69 stdcall -version=0x600+ EtwEventWriteString(int64 long int64 wstr) ChpeAutoEtwEventWriteString
71 stdcall EtwGetTraceEnableFlags(double) ChpeAutoEtwGetTraceEnableFlags
72 stdcall EtwGetTraceEnableLevel(double) ChpeAutoEtwGetTraceEnableLevel
73 stdcall EtwGetTraceLoggerHandle(ptr) ChpeAutoEtwGetTraceLoggerHandle
74 stdcall -version=0x600+ EtwLogTraceEvent(long long) ChpeStubEtwLogTraceEvent
75 stdcall -version=0x600+ EtwNotificationRegister(ptr long long long ptr) ChpeStubEtwNotificationRegister
76 stdcall -version=0x600+ EtwNotificationUnregister(long ptr) ChpeStubEtwNotificationUnregister
77 stdcall -version=0x600+ EtwProcessPrivateLoggerRequest(ptr) ChpeStubEtwProcessPrivateLoggerRequest
78 stdcall -version=0x600+ EtwRegister(ptr ptr ptr ptr) ChpeStubEtwRegister
79 stdcall -version=0x600+ EtwRegisterSecurityProvider() ChpeStubEtwRegisterSecurityProvider
80 stdcall EtwRegisterTraceGuidsA(ptr ptr ptr long ptr str str ptr) ChpeAutoEtwRegisterTraceGuidsA
81 stdcall EtwRegisterTraceGuidsW(ptr ptr ptr long ptr wstr wstr ptr) ChpeAutoEtwRegisterTraceGuidsW
82 stdcall -version=0x600+ EtwReplyNotification(long) ChpeStubEtwReplyNotification
83 stdcall -version=0x600+ EtwSendNotification(long long ptr long long) ChpeStubEtwSendNotification
84 stdcall -version=0x600+ EtwSetMark(long long long) ChpeStubEtwSetMark
85 stdcall  EtwTraceEventInstance(double ptr ptr ptr) ChpeStubEtwTraceEventInstance
87 stdcall  EtwTraceMessageVa(int64 long ptr long ptr) ChpeStubEtwTraceMessageVa
88 stdcall -version=0x600+ EtwUnregister(int64) ChpeStubEtwUnregister
89 stdcall EtwUnregisterTraceGuids(double) ChpeAutoEtwUnregisterTraceGuids
90 stdcall -version=0x600+ EtwWrite(int64 ptr ptr long ptr) ChpeStubEtwWrite
91 stdcall -version=0x600+ EtwWriteUMSecurityEvent(ptr long long long) ChpeStubEtwWriteUMSecurityEvent
92 stdcall -version=0x600+ EtwpCreateEtwThread(long long) ChpeStubEtwpCreateEtwThread
93 stdcall -version=0x600+ EtwpGetCpuSpeed(ptr) ChpeStubEtwpGetCpuSpeed
94 stdcall -version=0x600+ EtwpNotificationThread() ChpeStubEtwpNotificationThread
96 stub -version=0x600+ ExpInterlockedPopEntrySListEnd8
98 stub -version=0x600+ ExpInterlockedPopEntrySListFault8
100 stub -version=0x600+ ExpInterlockedPopEntrySListResume8
101 stdcall KiRaiseUserExceptionDispatcher() ChpeAutoKiRaiseUserExceptionDispatcher
103 stdcall KiUserCallbackDispatcher(ptr ptr long) ChpeAutoKiUserCallbackDispatcher
105 stdcall KiUserExceptionDispatcherWorker(ptr ptr) ChpeAutoKiUserExceptionDispatcherWorker
108 stdcall -version=0x600+ LdrAddLoadAsDataTable(ptr wstr long ptr) ChpeStubLdrAddLoadAsDataTable
110 stdcall LdrDisableThreadCalloutsForDll(ptr) ChpeAutoLdrDisableThreadCalloutsForDll
112 stdcall LdrEnumerateLoadedModules(long ptr ptr) ChpeAutoLdrEnumerateLoadedModules
115 stdcall  LdrFindResourceEx_U(ptr ptr ptr ptr ptr) ChpeStubLdrFindResourceEx_U
117 stdcall LdrFlushAlternateResourceModules() ChpeAutoLdrFlushAlternateResourceModules
120 stdcall -version=0x600+ LdrGetFailureData() ChpeStubLdrGetFailureData
121 stdcall -version=0x600+ LdrGetFileNameFromLoadAsDataTable(ptr ptr) ChpeStubLdrGetFileNameFromLoadAsDataTable
122 stdcall -version=0x600+ LdrGetKnownDllSectionHandle(wstr long ptr) ChpeStubLdrGetKnownDllSectionHandle
124 stdcall -version=0x600+ LdrGetProcedureAddressEx(ptr ptr long ptr long) ChpeAutoLdrGetProcedureAddressEx
125 stdcall  LdrHotPatchRoutine(ptr) ChpeStubLdrHotPatchRoutine
126 stdcall LdrInitShimEngineDynamic(ptr) ChpeAutoLdrInitShimEngineDynamic
127 stdcall LdrInitializeThunk(long long long long) ChpeAutoLdrInitializeThunk
128 stdcall LdrLoadAlternateResourceModule(ptr ptr) ChpeAutoLdrLoadAlternateResourceModule
129 stdcall -version=0x600+ LdrLoadAlternateResourceModuleEx(long long ptr ptr long) ChpeStubLdrLoadAlternateResourceModuleEx
131 stdcall LdrLockLoaderLock(long ptr ptr) ChpeAutoLdrLockLoaderLock
132 stdcall LdrOpenImageFileOptionsKey(ptr long ptr) ChpeAutoLdrOpenImageFileOptionsKey
133 stdcall -version=0x600+ LdrProcessInitializationComplete() ChpeStubLdrProcessInitializationComplete
134 stdcall LdrProcessRelocationBlock(ptr long ptr long) ChpeAutoLdrProcessRelocationBlock
135 stdcall LdrQueryImageFileExecutionOptions(ptr str long ptr long ptr) ChpeAutoLdrQueryImageFileExecutionOptions
136 stdcall LdrQueryImageFileExecutionOptionsEx(ptr ptr long ptr long ptr long) ChpeAutoLdrQueryImageFileExecutionOptionsEx
137 stdcall LdrQueryImageFileKeyOption(ptr ptr long ptr long ptr) ChpeAutoLdrQueryImageFileKeyOption
138 stdcall -version=0x600+ LdrQueryModuleServiceTags(ptr ptr ptr) ChpeStubLdrQueryModuleServiceTags
140 stdcall -version=0x600+ LdrRegisterDllNotification(long ptr ptr ptr) ChpeAutoLdrRegisterDllNotification
141 stdcall -version=0x600+ LdrRemoveLoadAsDataTable(ptr ptr ptr long) ChpeStubLdrRemoveLoadAsDataTable
144 stdcall -version=0x600+ LdrResFindResource(ptr long long long ptr ptr ptr ptr long) ChpeAutoLdrResFindResource
145 stdcall -version=0x600+ LdrResFindResourceDirectory(ptr long long ptr ptr ptr long ptr) ChpeAutoLdrResFindResourceDirectory
146 stdcall -version=0x600+ LdrResRelease(ptr ptr long long) ChpeStubLdrResRelease
147 stdcall -version=0x600+ LdrResSearchResource(wstr wstr long long long ptr long long) ChpeStubLdrResSearchResource
148 stdcall LdrSetAppCompatDllRedirectionCallback(long ptr ptr) ChpeAutoLdrSetAppCompatDllRedirectionCallback
149 stdcall LdrSetDllManifestProber(ptr) ChpeAutoLdrSetDllManifestProber
150 stdcall -version=0x600+ LdrSetMUICacheType(long) ChpeStubLdrSetMUICacheType
151 stdcall LdrShutdownProcess() ChpeAutoLdrShutdownProcess
152 stdcall LdrShutdownThread() ChpeAutoLdrShutdownThread
153 extern LdrSystemDllInitBlock ntdll.LdrSystemDllInitBlock
154 stdcall LdrUnloadAlternateResourceModule(ptr) ChpeAutoLdrUnloadAlternateResourceModule
155 stdcall -version=0x600+ LdrUnloadAlternateResourceModuleEx(long long) ChpeStubLdrUnloadAlternateResourceModuleEx
157 stdcall LdrUnlockLoaderLock(long ptr) ChpeAutoLdrUnlockLoaderLock
158 stdcall -version=0x600+ LdrUnregisterDllNotification(ptr) ChpeAutoLdrUnregisterDllNotification
159 stdcall LdrVerifyImageMatchesChecksum(ptr long long long) ChpeAutoLdrVerifyImageMatchesChecksum
160 stdcall -version=0x600+ LdrVerifyImageMatchesChecksumEx(ptr ptr) ChpeStubLdrVerifyImageMatchesChecksumEx
161 stdcall -version=0x600+ LdrpResGetMappingSize(long ptr long long) ChpeStubLdrpResGetMappingSize
162 stdcall -version=0x600+ LdrpResGetRCConfig(long long ptr long long) ChpeStubLdrpResGetRCConfig
163 stdcall -version=0x600+ LdrpResGetResourceDirectory(long long long ptr ptr) ChpeStubLdrpResGetResourceDirectory
164 stdcall -version=0x600+ MD4Final(ptr) ChpeAutoMD4Final
165 stdcall -version=0x600+ MD4Init(ptr) ChpeAutoMD4Init
166 stdcall -version=0x600+ MD4Update(ptr ptr long) ChpeAutoMD4Update
167 stdcall -version=0x600+ MD5Final(ptr) ChpeAutoMD5Final
168 stdcall -version=0x600+ MD5Init(ptr) ChpeAutoMD5Init
169 stdcall -version=0x600+ MD5Update(ptr ptr long) ChpeAutoMD5Update
170 extern NlsAnsiCodePage ntdll.NlsAnsiCodePage
171 extern NlsMbCodePageTag ntdll.NlsMbCodePageTag
172 extern NlsMbOemCodePageTag ntdll.NlsMbOemCodePageTag
173 stdcall NtAcceptConnectPort(ptr long ptr long long ptr) ChpeAutoNtAcceptConnectPort
174 stdcall NtAccessCheck(ptr long long ptr ptr ptr ptr ptr) ChpeAutoNtAccessCheck
175 stdcall NtAccessCheckAndAuditAlarm(ptr long ptr ptr ptr long ptr long ptr ptr ptr) ChpeAutoNtAccessCheckAndAuditAlarm
176 stdcall NtAccessCheckByType(ptr ptr ptr long ptr long ptr ptr long ptr ptr) ChpeAutoNtAccessCheckByType
177 stdcall NtAccessCheckByTypeAndAuditAlarm(ptr ptr ptr ptr ptr ptr long long long ptr long ptr long ptr ptr ptr) ChpeAutoNtAccessCheckByTypeAndAuditAlarm
178 stdcall NtAccessCheckByTypeResultList(ptr ptr ptr long ptr long ptr ptr long ptr ptr) ChpeAutoNtAccessCheckByTypeResultList
179 stdcall NtAccessCheckByTypeResultListAndAuditAlarm(ptr ptr ptr ptr ptr ptr long long long ptr long ptr long ptr ptr ptr) ChpeAutoNtAccessCheckByTypeResultListAndAuditAlarm
180 stdcall NtAccessCheckByTypeResultListAndAuditAlarmByHandle(ptr ptr ptr ptr ptr ptr ptr long long long ptr long ptr long ptr ptr ptr) ChpeAutoNtAccessCheckByTypeResultListAndAuditAlarmByHandle
181 stdcall -version=0x600+ NtAcquireCMFViewOwnership(ptr ptr long) ChpeStubNtAcquireCMFViewOwnership
182 stdcall NtAddAtom(ptr long ptr) ChpeAutoNtAddAtom
183 stdcall NtAddBootEntry(ptr long) ChpeAutoNtAddBootEntry
184 stdcall NtAddDriverEntry(ptr long) ChpeAutoNtAddDriverEntry
185 stdcall NtAdjustGroupsToken(long long ptr long ptr ptr) ChpeAutoNtAdjustGroupsToken
187 stdcall -version=0xA00+ NtAlertMultipleThreadByThreadId(ptr long ptr ptr) ChpeAutoNtAlertMultipleThreadByThreadId
188 stdcall NtAlertResumeThread(long ptr) ChpeAutoNtAlertResumeThread
189 stdcall NtAlertThread(long) ChpeAutoNtAlertThread
191 stdcall NtAllocateLocallyUniqueId(ptr) ChpeAutoNtAllocateLocallyUniqueId
192 stdcall -version=0x600+ NtAllocateReserveObject(ptr ptr long) ChpeAutoNtAllocateReserveObject
193 stdcall NtAllocateUserPhysicalPages(ptr ptr ptr) ChpeAutoNtAllocateUserPhysicalPages
194 stdcall NtAllocateUuids(ptr ptr ptr ptr) ChpeAutoNtAllocateUuids
197 stdcall -version=0x600+ NtAlpcAcceptConnectPort(ptr ptr long ptr ptr ptr ptr ptr long) ChpeAutoNtAlpcAcceptConnectPort
198 stdcall -version=0x600+ NtAlpcCancelMessage(ptr long ptr) ChpeAutoNtAlpcCancelMessage
199 stdcall -version=0x600+ NtAlpcConnectPort(ptr ptr ptr ptr long ptr ptr ptr ptr ptr ptr) ChpeAutoNtAlpcConnectPort
200 stdcall -version=0x602+ NtAlpcConnectPortEx(ptr ptr ptr ptr long ptr ptr ptr ptr ptr ptr) ChpeAutoNtAlpcConnectPortEx
201 stdcall -version=0x600+ NtAlpcCreatePort(ptr ptr ptr) ChpeAutoNtAlpcCreatePort
202 stdcall -version=0x600+ NtAlpcCreatePortSection(ptr long ptr long ptr ptr) ChpeAutoNtAlpcCreatePortSection
203 stdcall -version=0x600+ NtAlpcCreateResourceReserve(ptr long long ptr) ChpeAutoNtAlpcCreateResourceReserve
204 stdcall -version=0x600+ NtAlpcCreateSectionView(ptr long ptr) ChpeAutoNtAlpcCreateSectionView
205 stdcall -version=0x600+ NtAlpcCreateSecurityContext(ptr long ptr) ChpeAutoNtAlpcCreateSecurityContext
206 stdcall -version=0x600+ NtAlpcDeletePortSection(ptr long ptr) ChpeAutoNtAlpcDeletePortSection
207 stdcall -version=0x600+ NtAlpcDeleteResourceReserve(ptr long long) ChpeAutoNtAlpcDeleteResourceReserve
208 stdcall -version=0x600+ NtAlpcDeleteSectionView(ptr long ptr) ChpeAutoNtAlpcDeleteSectionView
209 stdcall -version=0x600+ NtAlpcDeleteSecurityContext(ptr long ptr) ChpeAutoNtAlpcDeleteSecurityContext
210 stdcall -version=0x600+ NtAlpcDisconnectPort(ptr long) ChpeAutoNtAlpcDisconnectPort
211 stdcall -version=0x600+ NtAlpcImpersonateClientOfPort(ptr ptr ptr) ChpeAutoNtAlpcImpersonateClientOfPort
212 stdcall -version=0xA00+ NtAlpcImpersonateClientContainerOfPort(ptr ptr long) ChpeAutoNtAlpcImpersonateClientContainerOfPort
213 stdcall -version=0x600+ NtAlpcOpenSenderProcess(ptr ptr ptr long long ptr) ChpeAutoNtAlpcOpenSenderProcess
214 stdcall -version=0x600+ NtAlpcOpenSenderThread(ptr ptr ptr long long ptr) ChpeAutoNtAlpcOpenSenderThread
215 stdcall -version=0x600+ NtAlpcQueryInformation(ptr long ptr long ptr) ChpeAutoNtAlpcQueryInformation
216 stdcall -version=0x600+ NtAlpcQueryInformationMessage(ptr ptr long ptr long ptr) ChpeAutoNtAlpcQueryInformationMessage
217 stdcall -version=0x600+ NtAlpcRevokeSecurityContext(ptr long ptr) ChpeAutoNtAlpcRevokeSecurityContext
218 stdcall -version=0x600+ NtAlpcSendWaitReceivePort(ptr long ptr ptr ptr ptr ptr ptr) ChpeAutoNtAlpcSendWaitReceivePort
219 stdcall -version=0x600+ NtAlpcSetInformation(ptr long ptr long) ChpeAutoNtAlpcSetInformation
220 stdcall NtApphelpCacheControl(long ptr) ChpeAutoNtApphelpCacheControl
221 stdcall NtAreMappedFilesTheSame(ptr ptr) ChpeAutoNtAreMappedFilesTheSame
222 stdcall NtAssignProcessToJobObject(long long) ChpeAutoNtAssignProcessToJobObject
223 stdcall NtCallbackReturn(ptr long long) ChpeAutoNtCallbackReturn
224 stdcall NtCancelDeviceWakeupRequest(ptr) ChpeAutoNtCancelDeviceWakeupRequest
225 stdcall NtCancelIoFile(long ptr) ChpeAutoNtCancelIoFile
226 stdcall -version=0x600+ NtCancelIoFileEx(long ptr ptr) ChpeAutoNtCancelIoFileEx
227 stdcall -version=0x600+ NtCancelSynchronousIoFile(long ptr ptr) ChpeAutoNtCancelSynchronousIoFile
228 stdcall NtCancelTimer(long ptr) ChpeAutoNtCancelTimer
229 stdcall NtClearEvent(long) ChpeAutoNtClearEvent
231 stdcall NtCloseObjectAuditAlarm(ptr ptr long) ChpeAutoNtCloseObjectAuditAlarm
232 stdcall -version=0x600+ NtCompareObjects(ptr ptr) ChpeAutoNtCompareObjects
233 stdcall -version=0x600+ NtCommitComplete(ptr ptr) ChpeAutoNtCommitComplete
234 stdcall -version=0x600+ NtCommitEnlistment(ptr ptr) ChpeAutoNtCommitEnlistment
235 stdcall -version=0x600+ NtCommitTransaction(ptr long) ChpeAutoNtCommitTransaction
236 stdcall NtCompactKeys(long ptr) ChpeAutoNtCompactKeys
237 stdcall NtCompareTokens(ptr ptr ptr) ChpeAutoNtCompareTokens
238 stdcall NtCompleteConnectPort(ptr) ChpeAutoNtCompleteConnectPort
239 stdcall NtCompressKey(ptr) ChpeAutoNtCompressKey
240 stdcall NtConnectPort(ptr ptr ptr ptr ptr ptr ptr ptr) ChpeAutoNtConnectPort
243 stdcall -version=0xA00+ NtConvertBetweenAuxiliaryCounterAndPerformanceCounter(long ptr ptr ptr) ChpeAutoNtConvertBetweenAuxiliaryCounterAndPerformanceCounter
244 stdcall NtCreateDebugObject(ptr long ptr long) ChpeAutoNtCreateDebugObject
245 stdcall NtCreateDirectoryObject(long long long) ChpeAutoNtCreateDirectoryObject
246 stdcall -version=0x600+ NtCreateEnlistment(ptr long ptr ptr ptr long long ptr) ChpeAutoNtCreateEnlistment
247 stdcall NtCreateEvent(long long long long long) ChpeAutoNtCreateEvent
248 stdcall NtCreateEventPair(ptr long ptr) ChpeAutoNtCreateEventPair
250 stdcall NtCreateIoCompletion(ptr long ptr long) ChpeAutoNtCreateIoCompletion
254 stdcall NtCreateJobObject(ptr long ptr) ChpeAutoNtCreateJobObject
255 stdcall NtCreateJobSet(long ptr long) ChpeAutoNtCreateJobSet
257 stdcall -version=0x600+ NtCreateKeyTransacted(ptr long ptr long ptr long ptr ptr) ChpeAutoNtCreateKeyTransacted
258 stdcall NtCreateKeyedEvent(ptr long ptr long) ChpeAutoNtCreateKeyedEvent
259 stdcall NtCreateMailslotFile(long long long long long long long long) ChpeAutoNtCreateMailslotFile
260 stdcall NtCreateMutant(ptr long ptr long) ChpeAutoNtCreateMutant
262 stdcall NtCreatePagingFile(ptr ptr ptr long) ChpeAutoNtCreatePagingFile
263 stdcall NtCreatePort(ptr ptr long long ptr) ChpeAutoNtCreatePort
264 stdcall -version=0x600+ NtCreatePrivateNamespace(ptr long ptr ptr) ChpeAutoNtCreatePrivateNamespace
265 stdcall NtCreateProcess(ptr long ptr ptr long ptr ptr ptr) ChpeAutoNtCreateProcess
266 stdcall NtCreateProcessEx(ptr long ptr ptr long ptr ptr ptr long) ChpeAutoNtCreateProcessEx
267 stdcall NtCreateProfile(ptr ptr ptr long long ptr long long long) ChpeAutoNtCreateProfile
268 stdcall -version=0x600+ NtCreateResourceManager(ptr long ptr ptr ptr long ptr) ChpeAutoNtCreateResourceManager
269 stdcall NtCreateSection(ptr long ptr ptr long long ptr) ChpeAutoNtCreateSection
270 stdcall NtCreateSectionEx(ptr long ptr ptr long long ptr ptr long) ChpeAutoNtCreateSectionEx
271 stdcall NtCreateSemaphore(ptr long ptr long long) ChpeAutoNtCreateSemaphore
272 stdcall NtCreateSymbolicLinkObject(ptr long ptr ptr) ChpeAutoNtCreateSymbolicLinkObject
274 stdcall -version=0x600+ NtCreateThreadEx(ptr long ptr ptr ptr ptr long long long long ptr) ChpeAutoNtCreateThreadEx
275 stdcall NtCreateTimer(ptr long ptr long) ChpeAutoNtCreateTimer
276 stdcall NtCreateToken(ptr long ptr long ptr ptr ptr ptr ptr ptr ptr ptr ptr) ChpeAutoNtCreateToken
277 stdcall -version=0x600+ NtCreateTransaction(ptr long ptr ptr ptr long long long ptr ptr) ChpeAutoNtCreateTransaction
278 stdcall -version=0x600+ NtCreateTransactionManager(ptr long ptr ptr long long) ChpeAutoNtCreateTransactionManager
279 stdcall -version=0x600+ NtCreateUserProcess(ptr ptr long long ptr ptr long long ptr ptr ptr) ChpeAutoNtCreateUserProcess
280 stdcall NtCreateWaitablePort(ptr ptr long long long) ChpeAutoNtCreateWaitablePort
281 stdcall -version=0x602+ NtCreateWnfStateName(ptr long long long ptr long ptr) ChpeAutoNtCreateWnfStateName
282 stdcall -version=0x600+ NtCreateWorkerFactory(ptr long long long long long long long long long) ChpeStubNtCreateWorkerFactory
283 stdcall NtDebugActiveProcess(ptr ptr) ChpeAutoNtDebugActiveProcess
284 stdcall NtDebugContinue(ptr ptr long) ChpeAutoNtDebugContinue
285 stdcall NtDelayExecution(long ptr) ChpeAutoNtDelayExecution
286 stdcall NtDeleteAtom(long) ChpeAutoNtDeleteAtom
287 stdcall NtDeleteBootEntry(long) ChpeAutoNtDeleteBootEntry
288 stdcall NtDeleteDriverEntry(long) ChpeAutoNtDeleteDriverEntry
289 stdcall NtDeleteFile(ptr) ChpeAutoNtDeleteFile
291 stdcall NtDeleteObjectAuditAlarm(ptr ptr long) ChpeAutoNtDeleteObjectAuditAlarm
292 stdcall -version=0x600+ NtDeletePrivateNamespace(ptr) ChpeAutoNtDeletePrivateNamespace
294 stdcall -version=0x602+ NtDeleteWnfStateData(ptr ptr) ChpeAutoNtDeleteWnfStateData
295 stdcall -version=0x602+ NtDeleteWnfStateName(ptr) ChpeAutoNtDeleteWnfStateName
297 stdcall NtDisplayString(ptr) ChpeAutoNtDisplayString
298 stdcall NtDuplicateObject(long long long ptr long long long) ChpeAutoNtDuplicateObject
299 stdcall NtDuplicateToken(long long long long long long) ChpeAutoNtDuplicateToken
300 stdcall NtEnumerateBootEntries(ptr ptr) ChpeAutoNtEnumerateBootEntries
301 stdcall NtEnumerateDriverEntries(ptr ptr) ChpeAutoNtEnumerateDriverEntries
303 stdcall NtEnumerateSystemEnvironmentValuesEx(long ptr long) ChpeAutoNtEnumerateSystemEnvironmentValuesEx
304 stdcall -version=0x600+ NtEnumerateTransactionObject(ptr long ptr long ptr) ChpeAutoNtEnumerateTransactionObject
306 stdcall NtExtendSection(ptr ptr) ChpeAutoNtExtendSection
307 stdcall NtFilterToken(ptr long ptr ptr ptr ptr) ChpeAutoNtFilterToken
308 stdcall -version=0x602+ NtCreateLowBoxToken(ptr ptr long ptr ptr long ptr long ptr) ChpeAutoNtCreateLowBoxToken
309 stdcall NtFindAtom(ptr long ptr) ChpeAutoNtFindAtom
310 stdcall NtFlushBuffersFile(long ptr) ChpeAutoNtFlushBuffersFile
311 stdcall NtFlushBuffersFileEx(long long ptr long ptr) ChpeAutoNtFlushBuffersFileEx
312 stdcall -version=0x600+ NtFlushInstallUILanguage(long long) ChpeStubNtFlushInstallUILanguage
316 stdcall NtFlushVirtualMemory(ptr ptr ptr ptr) ChpeAutoNtFlushVirtualMemory
317 stdcall NtFlushWriteBuffer() ChpeAutoNtFlushWriteBuffer
318 stdcall NtFreeUserPhysicalPages(ptr ptr ptr) ChpeAutoNtFreeUserPhysicalPages
320 stdcall -version=0x600+ NtFreezeRegistry(long) ChpeStubNtFreezeRegistry
321 stdcall -version=0x600+ NtFreezeTransactions(ptr ptr) ChpeStubNtFreezeTransactions
324 stdcall NtGetCurrentProcessorNumber() ChpeAutoNtGetCurrentProcessorNumber
325 stdcall -version=0xA00+ NtGetCurrentProcessorNumberEx(ptr) ChpeAutoNtGetCurrentProcessorNumberEx
326 stdcall NtGetDevicePowerState(ptr ptr) ChpeAutoNtGetDevicePowerState
327 stdcall -version=0x600+ NtGetMUIRegistryInfo(long ptr ptr) ChpeStubNtGetMUIRegistryInfo
328 stdcall -version=0x600+ NtGetNextProcess(long long long long ptr) ChpeStubNtGetNextProcess
330 stdcall -version=0x600+ NtGetNlsSectionPtr(long long ptr ptr ptr) ChpeAutoNtGetNlsSectionPtr
331 stdcall -version=0x600+ NtGetNotificationResourceManager(ptr ptr long ptr ptr long ptr) ChpeAutoNtGetNotificationResourceManager
332 stdcall NtGetPlugPlayEvent(long long ptr long) ChpeAutoNtGetPlugPlayEvent
333 stdcall NtGetTickCount() ChpeAutoNtGetTickCount
334 stdcall NtGetWriteWatch(long long ptr long ptr ptr ptr) ChpeAutoNtGetWriteWatch
335 stdcall NtImpersonateAnonymousToken(ptr) ChpeAutoNtImpersonateAnonymousToken
336 stdcall NtImpersonateClientOfPort(ptr ptr) ChpeAutoNtImpersonateClientOfPort
337 stdcall NtImpersonateThread(ptr ptr ptr) ChpeAutoNtImpersonateThread
338 stdcall -version=0x600+ NtInitializeNlsFiles(ptr ptr ptr) ChpeAutoNtInitializeNlsFiles
339 stdcall NtInitializeRegistry(long) ChpeAutoNtInitializeRegistry
340 stdcall NtInitiatePowerAction(long long long long) ChpeAutoNtInitiatePowerAction
341 stdcall NtIsProcessInJob(long long) ChpeAutoNtIsProcessInJob
342 stdcall NtIsSystemResumeAutomatic() ChpeAutoNtIsSystemResumeAutomatic
343 stdcall -version=0x600+ NtIsUILanguageComitted() ChpeStubNtIsUILanguageComitted
344 stdcall NtListenPort(ptr ptr) ChpeAutoNtListenPort
345 stdcall NtLoadDriver(ptr) ChpeAutoNtLoadDriver
346 stdcall NtLoadKey2(ptr ptr long) ChpeAutoNtLoadKey2
347 stdcall NtLoadKey(ptr ptr) ChpeAutoNtLoadKey
348 stdcall NtLoadKeyEx(ptr ptr long ptr ptr long ptr ptr) ChpeAutoNtLoadKeyEx
349 stdcall NtLockFile(long long ptr ptr ptr ptr ptr ptr long long) ChpeAutoNtLockFile
350 stdcall NtLockProductActivationKeys(ptr ptr) ChpeAutoNtLockProductActivationKeys
351 stdcall NtLockRegistryKey(ptr) ChpeAutoNtLockRegistryKey
352 stdcall NtLockVirtualMemory(long ptr ptr long) ChpeAutoNtLockVirtualMemory
353 stdcall NtMakePermanentObject(ptr) ChpeAutoNtMakePermanentObject
354 stdcall NtMakeTemporaryObject(long) ChpeAutoNtMakeTemporaryObject
355 stdcall -version=0x600+ NtMapCMFModule(long long ptr ptr ptr ptr) ChpeStubNtMapCMFModule
356 stdcall NtMapUserPhysicalPages(ptr ptr ptr) ChpeAutoNtMapUserPhysicalPages
357 stdcall NtMapUserPhysicalPagesScatter(ptr ptr ptr) ChpeAutoNtMapUserPhysicalPagesScatter
360 stdcall NtModifyBootEntry(ptr) ChpeAutoNtModifyBootEntry
361 stdcall NtModifyDriverEntry(ptr) ChpeAutoNtModifyDriverEntry
362 stdcall NtNotifyChangeDirectoryFile(long long ptr ptr ptr ptr long long long) ChpeAutoNtNotifyChangeDirectoryFile
363 stdcall NtNotifyChangeDirectoryFileEx(long long ptr ptr ptr ptr long long long long) ChpeAutoNtNotifyChangeDirectoryFileEx
366 stdcall NtOpenDirectoryObject(long long long) ChpeAutoNtOpenDirectoryObject
367 stdcall -version=0x600+ NtOpenEnlistment(ptr long ptr ptr ptr) ChpeAutoNtOpenEnlistment
368 stdcall NtOpenEvent(long long long) ChpeAutoNtOpenEvent
369 stdcall NtOpenEventPair(ptr long ptr) ChpeAutoNtOpenEventPair
371 stdcall NtOpenIoCompletion(ptr long ptr) ChpeAutoNtOpenIoCompletion
372 stdcall NtOpenJobObject(ptr long ptr) ChpeAutoNtOpenJobObject
374 stdcall -version=0x600+ NtOpenKeyTransacted(ptr long ptr ptr) ChpeAutoNtOpenKeyTransacted
375 stdcall -version=0x600+ NtOpenKeyTransactedEx(ptr long ptr long ptr) ChpeAutoNtOpenKeyTransactedEx
376 stdcall NtOpenKeyedEvent(ptr long ptr) ChpeAutoNtOpenKeyedEvent
377 stdcall NtOpenMutant(ptr long ptr) ChpeAutoNtOpenMutant
378 stdcall NtOpenObjectAuditAlarm(ptr ptr ptr ptr ptr ptr long long ptr long long ptr) ChpeAutoNtOpenObjectAuditAlarm
379 stdcall -version=0x600+ NtOpenPrivateNamespace(ptr long ptr ptr) ChpeAutoNtOpenPrivateNamespace
380 stdcall NtOpenProcess(ptr long ptr ptr) ChpeAutoNtOpenProcess
381 stdcall NtOpenProcessToken(long long ptr) ChpeAutoNtOpenProcessToken
382 stdcall NtOpenProcessTokenEx(long long long ptr) ChpeAutoNtOpenProcessTokenEx
383 stdcall -version=0x600+ NtOpenResourceManager(ptr long ptr ptr ptr) ChpeAutoNtOpenResourceManager
384 stdcall NtOpenSection(ptr long ptr) ChpeAutoNtOpenSection
385 stdcall NtOpenSemaphore(long long ptr) ChpeAutoNtOpenSemaphore
386 stdcall -version=0x600+ NtOpenSession(ptr long ptr) ChpeStubNtOpenSession
387 stdcall NtOpenSymbolicLinkObject(ptr long ptr) ChpeAutoNtOpenSymbolicLinkObject
388 stdcall NtOpenThread(ptr long ptr ptr) ChpeAutoNtOpenThread
389 stdcall NtOpenThreadToken(long long long ptr) ChpeAutoNtOpenThreadToken
390 stdcall NtOpenThreadTokenEx(long long long long ptr) ChpeAutoNtOpenThreadTokenEx
391 stdcall NtOpenTimer(ptr long ptr) ChpeAutoNtOpenTimer
392 stdcall -version=0x600+ NtOpenTransaction(ptr long ptr ptr ptr) ChpeAutoNtOpenTransaction
393 stdcall -version=0x600+ NtOpenTransactionManager(ptr long ptr ptr ptr long) ChpeAutoNtOpenTransactionManager
394 stdcall NtPlugPlayControl(ptr ptr long) ChpeAutoNtPlugPlayControl
395 stdcall NtPowerInformation(long ptr long ptr long) ChpeAutoNtPowerInformation
396 stdcall -version=0x600+ NtPrePrepareComplete(ptr ptr) ChpeAutoNtPrePrepareComplete
397 stdcall -version=0x600+ NtPrePrepareEnlistment(ptr ptr) ChpeAutoNtPrePrepareEnlistment
398 stdcall -version=0x600+ NtPrepareComplete(ptr ptr) ChpeAutoNtPrepareComplete
399 stdcall -version=0x600+ NtPrepareEnlistment(ptr ptr) ChpeAutoNtPrepareEnlistment
400 stdcall NtPrivilegeCheck(ptr ptr ptr) ChpeAutoNtPrivilegeCheck
401 stdcall NtPrivilegeObjectAuditAlarm(ptr ptr ptr long ptr long) ChpeAutoNtPrivilegeObjectAuditAlarm
402 stdcall NtPrivilegedServiceAuditAlarm(ptr ptr ptr ptr long) ChpeAutoNtPrivilegedServiceAuditAlarm
403 stdcall -version=0x600+ NtPropagationComplete(ptr long long ptr) ChpeAutoNtPropagationComplete
404 stdcall -version=0x600+ NtPropagationFailed(ptr long long) ChpeAutoNtPropagationFailed
406 stdcall NtPulseEvent(long ptr) ChpeAutoNtPulseEvent
407 stdcall NtQueryAttributesFile(ptr ptr) ChpeAutoNtQueryAttributesFile
408 stdcall NtQueryBootEntryOrder(ptr ptr) ChpeAutoNtQueryBootEntryOrder
409 stdcall NtQueryBootOptions(ptr ptr) ChpeAutoNtQueryBootOptions
410 stdcall NtQueryDebugFilterState(long long) ChpeAutoNtQueryDebugFilterState
411 stdcall NtQueryDefaultLocale(long ptr) ChpeAutoNtQueryDefaultLocale
412 stdcall NtQueryDefaultUILanguage(ptr) ChpeAutoNtQueryDefaultUILanguage
414 stdcall NtQueryDirectoryFileEx(long long ptr ptr ptr ptr long long long ptr) ChpeAutoNtQueryDirectoryFileEx
415 stdcall NtQueryDirectoryObject(long ptr long long long ptr ptr) ChpeAutoNtQueryDirectoryObject
416 stdcall NtQueryDriverEntryOrder(ptr ptr) ChpeAutoNtQueryDriverEntryOrder
417 stdcall NtQueryEaFile(long ptr ptr long long ptr long ptr long) ChpeAutoNtQueryEaFile
418 stdcall NtQueryEvent(long long ptr long ptr) ChpeAutoNtQueryEvent
419 stdcall NtQueryFullAttributesFile(ptr ptr) ChpeAutoNtQueryFullAttributesFile
420 stdcall NtQueryInformationAtom(long long ptr long ptr) ChpeAutoNtQueryInformationAtom
421 stdcall NtQueryInformationByName(ptr ptr ptr long long) ChpeAutoNtQueryInformationByName
422 stdcall -version=0x600+ NtQueryInformationEnlistment(ptr long ptr long ptr) ChpeAutoNtQueryInformationEnlistment
424 stdcall NtQueryInformationJobObject(ptr long ptr long ptr) ChpeAutoNtQueryInformationJobObject
425 stdcall NtQueryInformationPort(ptr long ptr long ptr) ChpeAutoNtQueryInformationPort
427 stdcall -version=0x600+ NtQueryInformationResourceManager(ptr long ptr long ptr) ChpeAutoNtQueryInformationResourceManager
429 stdcall NtQueryInformationToken(ptr long ptr long ptr) ChpeAutoNtQueryInformationToken
430 stdcall -version=0x600+ NtQueryInformationTransaction(ptr long ptr long ptr) ChpeAutoNtQueryInformationTransaction
431 stdcall -version=0x600+ NtQueryInformationTransactionManager(ptr long ptr long ptr) ChpeAutoNtQueryInformationTransactionManager
432 stdcall -version=0x600+ NtQueryInformationWorkerFactory(ptr long ptr long ptr) ChpeStubNtQueryInformationWorkerFactory
433 stdcall NtQueryInstallUILanguage(ptr) ChpeAutoNtQueryInstallUILanguage
434 stdcall NtQueryIntervalProfile(long ptr) ChpeAutoNtQueryIntervalProfile
435 stdcall NtQueryIoCompletion(long long ptr long ptr) ChpeAutoNtQueryIoCompletion
437 stdcall -version=0x600+ NtQueryLicenseValue(ptr ptr ptr long ptr) ChpeAutoNtQueryLicenseValue
438 stdcall NtQueryMultipleValueKey(long ptr long ptr long ptr) ChpeAutoNtQueryMultipleValueKey
439 stdcall NtQueryMutant(long long ptr long ptr) ChpeAutoNtQueryMutant
441 stdcall NtQueryOpenSubKeys(ptr ptr) ChpeAutoNtQueryOpenSubKeys
442 stdcall NtQueryOpenSubKeysEx(ptr long ptr ptr) ChpeAutoNtQueryOpenSubKeysEx
443 stdcall NtQueryPerformanceCounter(ptr ptr) ChpeAutoNtQueryPerformanceCounter
444 stdcall NtQueryPortInformationProcess() ChpeAutoNtQueryPortInformationProcess
445 stdcall NtQueryQuotaInformationFile(ptr ptr ptr long long ptr long ptr long) ChpeAutoNtQueryQuotaInformationFile
446 stdcall NtQuerySection(long long long long long) ChpeAutoNtQuerySection
447 stdcall NtQuerySecurityObject(long long long long long) ChpeAutoNtQuerySecurityObject
448 stdcall NtQuerySemaphore(long long ptr long ptr) ChpeAutoNtQuerySemaphore
449 stdcall NtQuerySymbolicLinkObject(long ptr ptr) ChpeAutoNtQuerySymbolicLinkObject
450 stdcall NtQuerySystemEnvironmentValue(ptr ptr long ptr) ChpeAutoNtQuerySystemEnvironmentValue
451 stdcall NtQuerySystemEnvironmentValueEx(ptr ptr ptr ptr ptr) ChpeAutoNtQuerySystemEnvironmentValueEx
453 stdcall -version=0x601+ NtQuerySystemInformationEx(long ptr long ptr long ptr) ChpeAutoNtQuerySystemInformationEx
454 stdcall NtQuerySystemTime(ptr) ChpeAutoNtQuerySystemTime
455 stdcall NtQueryTimer(ptr long ptr long ptr) ChpeAutoNtQueryTimer
456 stdcall NtQueryTimerResolution(long long long) ChpeAutoNtQueryTimerResolution
460 stdcall -version=0x602+ NtQueryWnfStateData(ptr ptr ptr ptr ptr ptr) ChpeAutoNtQueryWnfStateData
461 stdcall -version=0x602+ NtQueryWnfStateNameInformation(ptr long ptr ptr long) ChpeAutoNtQueryWnfStateNameInformation
462 stdcall NtQueueApcThread(long ptr long long long) ChpeAutoNtQueueApcThread
463 stdcall -version=0x601+ NtQueueApcThreadEx(long long ptr long long long) ChpeAutoNtQueueApcThreadEx
464 stdcall -version=0xA00+ NtQueueApcThreadEx2(long long long ptr long long long) ChpeAutoNtQueueApcThreadEx2
466 stdcall NtRaiseHardError(long long long ptr long ptr) ChpeAutoNtRaiseHardError
468 stdcall NtReadFileScatter(long long ptr ptr ptr ptr long ptr ptr) ChpeAutoNtReadFileScatter
469 stdcall -version=0x600+ NtReadOnlyEnlistment(ptr ptr) ChpeAutoNtReadOnlyEnlistment
470 stdcall NtReadRequestData(ptr ptr long ptr long ptr) ChpeAutoNtReadRequestData
472 stdcall -version=0x600+ NtRecoverEnlistment(ptr ptr) ChpeAutoNtRecoverEnlistment
473 stdcall -version=0x600+ NtRecoverResourceManager(ptr) ChpeAutoNtRecoverResourceManager
474 stdcall -version=0x600+ NtRecoverTransactionManager(ptr) ChpeAutoNtRecoverTransactionManager
475 stdcall -version=0x600+ NtRegisterProtocolAddressInformation(ptr ptr long ptr long) ChpeAutoNtRegisterProtocolAddressInformation
476 stdcall NtRegisterThreadTerminatePort(ptr) ChpeAutoNtRegisterThreadTerminatePort
477 stdcall -version=0x600+ NtReleaseCMFViewOwnership() ChpeStubNtReleaseCMFViewOwnership
478 stdcall NtReleaseKeyedEvent(ptr ptr long ptr) ChpeAutoNtReleaseKeyedEvent
479 stdcall NtReleaseMutant(long ptr) ChpeAutoNtReleaseMutant
480 stdcall NtReleaseSemaphore(long long ptr) ChpeAutoNtReleaseSemaphore
481 stdcall -version=0x600+ NtReleaseWorkerFactoryWorker(ptr) ChpeStubNtReleaseWorkerFactoryWorker
482 stdcall NtRemoveIoCompletion(ptr ptr ptr ptr ptr) ChpeAutoNtRemoveIoCompletion
483 stdcall -version=0x600+ NtRemoveIoCompletionEx(ptr ptr long ptr ptr long) ChpeAutoNtRemoveIoCompletionEx
484 stdcall NtRemoveProcessDebug(ptr ptr) ChpeAutoNtRemoveProcessDebug
485 stdcall NtRenameKey(ptr ptr) ChpeAutoNtRenameKey
486 stdcall -version=0x600+ NtRenameTransactionManager(ptr ptr) ChpeAutoNtRenameTransactionManager
487 stdcall NtReplaceKey(ptr long ptr) ChpeAutoNtReplaceKey
488 stdcall -version=0x600+ NtReplacePartitionUnit(wstr wstr long) ChpeStubNtReplacePartitionUnit
489 stdcall NtReplyPort(ptr ptr) ChpeAutoNtReplyPort
490 stdcall NtReplyWaitReceivePort(ptr ptr ptr ptr) ChpeAutoNtReplyWaitReceivePort
491 stdcall NtReplyWaitReceivePortEx(ptr ptr ptr ptr ptr) ChpeAutoNtReplyWaitReceivePortEx
492 stdcall NtReplyWaitReplyPort(ptr ptr) ChpeAutoNtReplyWaitReplyPort
493 stdcall NtRequestDeviceWakeup(ptr) ChpeAutoNtRequestDeviceWakeup
494 stdcall NtRequestPort(ptr ptr) ChpeAutoNtRequestPort
495 stdcall NtRequestWaitReplyPort(ptr ptr ptr) ChpeAutoNtRequestWaitReplyPort
496 stdcall NtRequestWakeupLatency(long) ChpeAutoNtRequestWakeupLatency
497 stdcall NtResetEvent(long ptr) ChpeAutoNtResetEvent
498 stdcall NtResetWriteWatch(long ptr long) ChpeAutoNtResetWriteWatch
499 stdcall NtRestoreKey(long long long) ChpeAutoNtRestoreKey
500 stdcall NtResumeProcess(ptr) ChpeAutoNtResumeProcess
502 stdcall -version=0x600+ NtRollbackComplete(ptr ptr) ChpeAutoNtRollbackComplete
503 stdcall -version=0x600+ NtRollbackEnlistment(ptr ptr) ChpeAutoNtRollbackEnlistment
504 stdcall -version=0x600+ NtRollbackTransaction(ptr long) ChpeAutoNtRollbackTransaction
505 stdcall -version=0x600+ NtRollforwardTransactionManager(ptr ptr) ChpeAutoNtRollforwardTransactionManager
506 stdcall NtSaveKey(long long) ChpeAutoNtSaveKey
507 stdcall NtSaveKeyEx(ptr ptr long) ChpeAutoNtSaveKeyEx
508 stdcall NtSaveMergedKeys(ptr ptr ptr) ChpeAutoNtSaveMergedKeys
509 stdcall NtSecureConnectPort(ptr ptr ptr ptr ptr ptr ptr ptr ptr) ChpeAutoNtSecureConnectPort
510 stdcall NtSetBootEntryOrder(ptr ptr) ChpeAutoNtSetBootEntryOrder
511 stdcall NtSetBootOptions(ptr long) ChpeAutoNtSetBootOptions
513 stdcall NtSetDebugFilterState(long long long) ChpeAutoNtSetDebugFilterState
514 stdcall NtSetDefaultHardErrorPort(ptr) ChpeAutoNtSetDefaultHardErrorPort
515 stdcall NtSetDefaultLocale(long long) ChpeAutoNtSetDefaultLocale
516 stdcall NtSetDefaultUILanguage(long) ChpeAutoNtSetDefaultUILanguage
517 stdcall NtSetDriverEntryOrder(ptr ptr) ChpeAutoNtSetDriverEntryOrder
518 stdcall NtSetEaFile(long ptr ptr long) ChpeAutoNtSetEaFile
519 stdcall NtSetEvent(long long) ChpeAutoNtSetEvent
520 stdcall NtSetEventBoostPriority(ptr) ChpeAutoNtSetEventBoostPriority
521 stdcall NtSetHighEventPair(ptr) ChpeAutoNtSetHighEventPair
522 stdcall NtSetHighWaitLowEventPair(ptr) ChpeAutoNtSetHighWaitLowEventPair
523 stdcall NtSetInformationDebugObject(ptr long ptr long ptr) ChpeAutoNtSetInformationDebugObject
524 stdcall -version=0x600+ NtSetInformationEnlistment(ptr long ptr long) ChpeAutoNtSetInformationEnlistment
525 stdcall NtSetInformationFile(ptr ptr ptr long long) ChpeAutoNtSetInformationFile
526 stdcall NtSetInformationJobObject(ptr long ptr long) ChpeAutoNtSetInformationJobObject
527 stdcall NtSetInformationKey(ptr long ptr long) ChpeAutoNtSetInformationKey
528 stdcall NtSetInformationObject(ptr long ptr long) ChpeAutoNtSetInformationObject
529 stdcall NtSetInformationProcess(ptr long ptr long) ChpeAutoNtSetInformationProcess
530 stdcall -version=0x600+ NtSetInformationResourceManager(ptr long ptr long) ChpeAutoNtSetInformationResourceManager
531 stdcall NtSetInformationThread(ptr long ptr long) ChpeAutoNtSetInformationThread
532 stdcall NtSetInformationVirtualMemory(ptr long ptr ptr ptr long) ChpeAutoNtSetInformationVirtualMemory
533 stdcall NtSetInformationToken(ptr long ptr long) ChpeAutoNtSetInformationToken
534 stdcall -version=0x600+ NtSetInformationTransaction(ptr long ptr long) ChpeAutoNtSetInformationTransaction
535 stdcall -version=0x600+ NtSetInformationTransactionManager(ptr long ptr long) ChpeAutoNtSetInformationTransactionManager
536 stdcall -version=0x600+ NtSetInformationWorkerFactory(ptr long ptr long) ChpeStubNtSetInformationWorkerFactory
537 stdcall NtSetIntervalProfile(long long) ChpeAutoNtSetIntervalProfile
538 stdcall NtSetIoCompletion(ptr long ptr long long) ChpeAutoNtSetIoCompletion
539 stdcall -version=0x600+ NtSetIoCompletionEx(ptr ptr long long long long) ChpeAutoNtSetIoCompletionEx
540 stdcall NtSetLdtEntries(long int64 long int64) ChpeAutoNtSetLdtEntries
541 stdcall NtSetLowEventPair(ptr) ChpeAutoNtSetLowEventPair
542 stdcall NtSetLowWaitHighEventPair(ptr) ChpeAutoNtSetLowWaitHighEventPair
543 stdcall NtSetQuotaInformationFile(ptr ptr ptr long) ChpeAutoNtSetQuotaInformationFile
544 stdcall NtSetSecurityObject(long long ptr) ChpeAutoNtSetSecurityObject
545 stdcall NtSetSystemEnvironmentValue(ptr ptr) ChpeAutoNtSetSystemEnvironmentValue
546 stdcall NtSetSystemEnvironmentValueEx(ptr ptr ptr ptr ptr) ChpeAutoNtSetSystemEnvironmentValueEx
547 stdcall NtSetSystemInformation(long ptr long) ChpeAutoNtSetSystemInformation
548 stdcall NtSetSystemPowerState(long long long) ChpeAutoNtSetSystemPowerState
549 stdcall NtSetSystemTime(ptr ptr) ChpeAutoNtSetSystemTime
550 stdcall NtSetThreadExecutionState(long ptr) ChpeAutoNtSetThreadExecutionState
552 stdcall NtSetTimerResolution(long long ptr) ChpeAutoNtSetTimerResolution
553 stdcall NtSetUuidSeed(ptr) ChpeAutoNtSetUuidSeed
555 stdcall NtSetVolumeInformationFile(long ptr ptr long long) ChpeAutoNtSetVolumeInformationFile
556 stdcall NtShutdownSystem(long) ChpeAutoNtShutdownSystem
557 stdcall -version=0x600+ NtShutdownWorkerFactory(ptr ptr) ChpeStubNtShutdownWorkerFactory
558 stdcall NtSignalAndWaitForSingleObject(long long long ptr) ChpeAutoNtSignalAndWaitForSingleObject
559 stdcall -version=0x600+ NtSinglePhaseReject(ptr ptr) ChpeAutoNtSinglePhaseReject
560 stdcall NtStartProfile(ptr) ChpeAutoNtStartProfile
561 stdcall NtStopProfile(ptr) ChpeAutoNtStopProfile
562 stdcall -version=0x602+ NtSubscribeWnfStateChange(ptr long long ptr) ChpeAutoNtSubscribeWnfStateChange
565 stdcall NtSystemDebugControl(long ptr long ptr long ptr) ChpeAutoNtSystemDebugControl
566 stdcall NtTerminateJobObject(ptr long) ChpeAutoNtTerminateJobObject
569 stdcall NtTestAlert() ChpeAutoNtTestAlert
570 stdcall -version=0x600+ NtThawRegistry() ChpeStubNtThawRegistry
571 stdcall -version=0x600+ NtThawTransactions() ChpeStubNtThawTransactions
572 stdcall -version=0x600+ NtTraceControl(long ptr long ptr long ptr) ChpeAutoNtTraceControl
573 stdcall NtTraceEvent(ptr long long ptr) ChpeAutoNtTraceEvent
574 stdcall NtTranslateFilePath(ptr long ptr long) ChpeAutoNtTranslateFilePath
575 stdcall NtUnloadDriver(ptr) ChpeAutoNtUnloadDriver
576 stdcall NtUnloadKey2(ptr long) ChpeAutoNtUnloadKey2
577 stdcall NtUnloadKey(long) ChpeAutoNtUnloadKey
578 stdcall NtUnloadKeyEx(ptr ptr) ChpeAutoNtUnloadKeyEx
579 stdcall NtUnlockFile(long ptr ptr ptr ptr) ChpeAutoNtUnlockFile
580 stdcall NtUnlockVirtualMemory(long ptr ptr long) ChpeAutoNtUnlockVirtualMemory
583 stdcall -version=0x602+ NtUnsubscribeWnfStateChange(ptr) ChpeAutoNtUnsubscribeWnfStateChange
584 stdcall -version=0x602+ NtUpdateWnfStateData(ptr ptr long ptr ptr long long) ChpeAutoNtUpdateWnfStateData
585 stdcall NtVdmControl(long ptr) ChpeAutoNtVdmControl
586 stdcall NtWaitForDebugEvent(ptr long ptr ptr) ChpeAutoNtWaitForDebugEvent
587 stdcall NtWaitForKeyedEvent(ptr ptr long ptr) ChpeAutoNtWaitForKeyedEvent
589 stdcall NtWaitForMultipleObjects32(long ptr long long ptr) ChpeAutoNtWaitForMultipleObjects32
590 stdcall NtWaitForMultipleObjects(long ptr long long ptr) ChpeAutoNtWaitForMultipleObjects
591 stdcall NtWaitForSingleObject(long long long) ChpeAutoNtWaitForSingleObject
592 stub -version=0x600+ NtWaitForWorkViaWorkerFactory
593 stdcall NtWaitHighEventPair(ptr) ChpeAutoNtWaitHighEventPair
594 stdcall NtWaitLowEventPair(ptr) ChpeAutoNtWaitLowEventPair
595 stdcall -version=0x600+ NtWorkerFactoryWorkerReady(long) ChpeStubNtWorkerFactoryWorkerReady
597 stdcall NtWriteFileGather(long long ptr ptr ptr ptr long ptr ptr) ChpeAutoNtWriteFileGather
598 stdcall NtWriteRequestData(ptr ptr long ptr long ptr) ChpeAutoNtWriteRequestData
600 stdcall NtYieldExecution() ChpeAutoNtYieldExecution
601 stdcall -version=0x600+ NtdllDefWindowProc_A(long long long long) ChpeStubNtdllDefWindowProc_A
602 stdcall -version=0x600+ NtdllDefWindowProc_W(long long long long) ChpeStubNtdllDefWindowProc_W
603 stdcall -version=0x600+ NtdllDialogWndProc_A(long long long long) ChpeStubNtdllDialogWndProc_A
604 stdcall -version=0x600+ NtdllDialogWndProc_W(long long long long) ChpeStubNtdllDialogWndProc_W
605 stdcall PfxFindPrefix(ptr ptr) ChpeAutoPfxFindPrefix
606 stdcall PfxInitialize(ptr) ChpeAutoPfxInitialize
607 stdcall PfxInsertPrefix(ptr ptr ptr) ChpeAutoPfxInsertPrefix
608 stdcall PfxRemovePrefix(ptr ptr) ChpeAutoPfxRemovePrefix
609 stdcall RtlAbortRXact(ptr) ChpeAutoRtlAbortRXact
610 stdcall RtlAbsoluteToSelfRelativeSD(ptr ptr ptr) ChpeAutoRtlAbsoluteToSelfRelativeSD
611 stdcall RtlAcquirePebLock() ChpeAutoRtlAcquirePebLock
613 stdcall RtlAcquireResourceExclusive(ptr long) ChpeAutoRtlAcquireResourceExclusive
614 stdcall RtlAcquireResourceShared(ptr long) ChpeAutoRtlAcquireResourceShared
617 stdcall RtlActivateActivationContext(long ptr ptr) ChpeAutoRtlActivateActivationContext
618 stdcall RtlActivateActivationContextEx(long ptr ptr ptr) ChpeAutoRtlActivateActivationContextEx
619 stdcall RtlActivateActivationContextUnsafeFast(ptr ptr) ChpeAutoRtlActivateActivationContextUnsafeFast
620 stdcall RtlAddAccessAllowedAce(ptr long long ptr) ChpeAutoRtlAddAccessAllowedAce
621 stdcall RtlAddAccessAllowedAceEx(ptr long long long ptr) ChpeAutoRtlAddAccessAllowedAceEx
622 stdcall RtlAddAccessAllowedObjectAce(ptr long long long ptr ptr ptr) ChpeAutoRtlAddAccessAllowedObjectAce
623 stdcall RtlAddAccessDeniedAce(ptr long long ptr) ChpeAutoRtlAddAccessDeniedAce
624 stdcall RtlAddAccessDeniedAceEx(ptr long long long ptr) ChpeAutoRtlAddAccessDeniedAceEx
625 stdcall RtlAddAccessDeniedObjectAce(ptr long long long ptr ptr ptr) ChpeAutoRtlAddAccessDeniedObjectAce
626 stdcall RtlAddAce(ptr long long ptr long) ChpeAutoRtlAddAce
627 stdcall RtlAddActionToRXact(ptr long ptr long ptr long) ChpeAutoRtlAddActionToRXact
628 stdcall RtlAddAtomToAtomTable(ptr wstr ptr) ChpeAutoRtlAddAtomToAtomTable
629 stdcall RtlAddAttributeActionToRXact(ptr long ptr ptr ptr long ptr long) ChpeAutoRtlAddAttributeActionToRXact
630 stdcall RtlAddAuditAccessAce(ptr long long ptr long long) ChpeAutoRtlAddAuditAccessAce
631 stdcall RtlAddAuditAccessAceEx(ptr long long long ptr long long) ChpeAutoRtlAddAuditAccessAceEx
632 stdcall RtlAddAuditAccessObjectAce(ptr long long long ptr ptr ptr long long) ChpeAutoRtlAddAuditAccessObjectAce
633 stdcall  RtlAddCompoundAce(ptr long long long ptr ptr) ChpeStubRtlAddCompoundAce
636 stdcall -version=0x600+ RtlAddMandatoryAce(ptr long long long long ptr) ChpeAutoRtlAddMandatoryAce
637 stdcall RtlAddRefActivationContext(ptr) ChpeAutoRtlAddRefActivationContext
638 stdcall RtlAddRefMemoryStream(ptr) ChpeAutoRtlAddRefMemoryStream
639 stdcall -version=0x600+ RtlAddSIDToBoundaryDescriptor(ptr ptr) ChpeAutoRtlAddSIDToBoundaryDescriptor
640 stdcall -version=0x600+ RtlAddIntegrityLabelToBoundaryDescriptor(ptr ptr) ChpeAutoRtlAddIntegrityLabelToBoundaryDescriptor
643 stdcall  RtlAddressInSectionTable(ptr ptr long) ChpeStubRtlAddressInSectionTable
644 stdcall RtlAdjustPrivilege(long long long ptr) ChpeAutoRtlAdjustPrivilege
645 stdcall RtlAllocateActivationContextStack(ptr) ChpeAutoRtlAllocateActivationContextStack
646 stdcall RtlAllocateAndInitializeSid(ptr long long long long long long long long long ptr) ChpeAutoRtlAllocateAndInitializeSid
647 stdcall RtlAllocateHandle(ptr ptr) ChpeAutoRtlAllocateHandle
649 stdcall -version=0x600+ RtlAllocateMemoryBlockLookaside(ptr long ptr) ChpeStubRtlAllocateMemoryBlockLookaside
650 stdcall -version=0x600+ RtlAllocateMemoryZone(long long ptr) ChpeStubRtlAllocateMemoryZone
651 stdcall RtlAnsiCharToUnicodeChar(ptr) ChpeAutoRtlAnsiCharToUnicodeChar
652 stdcall RtlAnsiStringToUnicodeSize(ptr) ChpeAutoRtlAnsiStringToUnicodeSize
653 stdcall RtlAnsiStringToUnicodeString(ptr ptr long) ChpeAutoRtlAnsiStringToUnicodeString
654 stdcall RtlAppendAsciizToString(ptr str) ChpeAutoRtlAppendAsciizToString
655 stdcall  RtlAppendPathElement(ptr ptr ptr) ChpeStubRtlAppendPathElement
656 stdcall RtlAppendStringToString(ptr ptr) ChpeAutoRtlAppendStringToString
657 stdcall RtlAppendUnicodeStringToString(ptr ptr) ChpeAutoRtlAppendUnicodeStringToString
658 stdcall RtlAppendUnicodeToString(ptr wstr) ChpeAutoRtlAppendUnicodeToString
659 stdcall RtlApplicationVerifierStop(ptr str ptr str ptr str ptr str ptr str) ChpeAutoRtlApplicationVerifierStop
660 stdcall RtlApplyRXact(ptr) ChpeAutoRtlApplyRXact
661 stdcall RtlApplyRXactNoFlush(ptr) ChpeAutoRtlApplyRXactNoFlush
662 stdcall RtlAreAllAccessesGranted(long long) ChpeAutoRtlAreAllAccessesGranted
663 stdcall RtlAreAnyAccessesGranted(long long) ChpeAutoRtlAreAnyAccessesGranted
664 stdcall RtlAreBitsClear(ptr long long) ChpeAutoRtlAreBitsClear
665 stdcall RtlAreBitsSet(ptr long long) ChpeAutoRtlAreBitsSet
666 stdcall RtlAssert(ptr ptr long ptr) ChpeAutoRtlAssert
667 stdcall -version=0x600+ RtlBarrier(ptr long) ChpeStubRtlBarrier
668 stdcall -version=0x600+ RtlBarrierForDelete(long long) ChpeStubRtlBarrierForDelete
669 stdcall RtlCancelTimer(ptr ptr) ChpeAutoRtlCancelTimer
672 stdcall RtlCharToInteger(ptr long ptr) ChpeAutoRtlCharToInteger
673 stdcall RtlCheckForOrphanedCriticalSections(ptr) ChpeAutoRtlCheckForOrphanedCriticalSections
674 stdcall RtlCheckRegistryKey(long ptr) ChpeAutoRtlCheckRegistryKey
675 stdcall -version=0x600+ RtlCleanUpTEBLangLists() ChpeStubRtlCleanUpTEBLangLists
676 stdcall RtlClearAllBits(ptr) ChpeAutoRtlClearAllBits
677 stdcall RtlClearBits(ptr long long) ChpeAutoRtlClearBits
678 stdcall RtlCloneMemoryStream(ptr ptr) ChpeAutoRtlCloneMemoryStream
679 stdcall -version=0x600+ RtlCloneUserProcess(long long long long long) ChpeStubRtlCloneUserProcess
680 stdcall -version=0x600+ RtlCmDecodeMemIoResource(ptr ptr) ChpeStubRtlCmDecodeMemIoResource
681 stdcall -version=0x600+ RtlCmEncodeMemIoResource(ptr long long long) ChpeStubRtlCmEncodeMemIoResource
682 stdcall -version=0x600+ RtlCommitDebugInfo(ptr long) ChpeStubRtlCommitDebugInfo
683 stdcall RtlCommitMemoryStream(ptr long) ChpeAutoRtlCommitMemoryStream
684 stdcall RtlCompactHeap(long long) ChpeAutoRtlCompactHeap
685 stdcall -version=0x600+ RtlCompareAltitudes(wstr wstr) ChpeStubRtlCompareAltitudes
687 stdcall RtlCompareMemoryUlong(ptr long long) ChpeAutoRtlCompareMemoryUlong
688 stdcall RtlCompareString(ptr ptr long) ChpeAutoRtlCompareString
690 stdcall -version=0x600+ RtlCompareUnicodeStrings(wstr long wstr long long) ChpeAutoRtlCompareUnicodeStrings
691 stdcall -version=0x600+ RtlCompleteProcessCloning(long) ChpeStubRtlCompleteProcessCloning
692 stdcall RtlCompressBuffer(long ptr long ptr long long ptr ptr) ChpeAutoRtlCompressBuffer
693 stdcall RtlComputeCrc32(long ptr long) ChpeAutoRtlComputeCrc32
694 stdcall RtlComputeImportTableHash(ptr ptr long) ChpeAutoRtlComputeImportTableHash
695 stdcall RtlComputePrivatizedDllName_U(ptr ptr ptr) ChpeAutoRtlComputePrivatizedDllName_U
696 stdcall -version=0x600+ RtlConnectToSm(ptr ptr long ptr) ChpeStubRtlConnectToSm
697 stdcall RtlConsoleMultiByteToUnicodeN(ptr long ptr ptr long ptr) ChpeAutoRtlConsoleMultiByteToUnicodeN
698 stdcall RtlConvertExclusiveToShared(ptr) ChpeAutoRtlConvertExclusiveToShared
699 stdcall -version=0x600+ RtlConvertLCIDToString(long long long ptr long) ChpeAutoRtlConvertLCIDToString
700 stdcall RtlConvertSharedToExclusive(ptr) ChpeAutoRtlConvertSharedToExclusive
701 stdcall RtlConvertSidToUnicodeString(ptr ptr long) ChpeAutoRtlConvertSidToUnicodeString
702 stdcall RtlConvertToAutoInheritSecurityObject(ptr ptr ptr ptr long ptr) ChpeAutoRtlConvertToAutoInheritSecurityObject
703 stdcall RtlConvertUiListToApiList(ptr ptr long) ChpeAutoRtlConvertUiListToApiList
704 stdcall RtlCopyLuid(ptr ptr) ChpeAutoRtlCopyLuid
705 stdcall RtlCopyLuidAndAttributesArray(long ptr ptr) ChpeAutoRtlCopyLuidAndAttributesArray
706 stdcall RtlCopyMappedMemory(ptr ptr long) ChpeAutoRtlCopyMappedMemory
707 stdcall -version=0xA00+ RtlCopyContext(ptr long ptr) ChpeAutoRtlCopyContext
708 stdcall -version=0x600+ RtlCopyExtendedContext(ptr long ptr) ChpeAutoRtlCopyExtendedContext
709 cdecl -version=0x600+ RtlCopyMemory(ptr ptr long) ChpeAutoRtlCopyMemory
710 stdcall -version=0x600+ RtlCopyMemoryNonTemporal(ptr ptr long) ChpeStubRtlCopyMemoryNonTemporal
711 stdcall RtlCopyMemoryStreamTo(ptr ptr int64 ptr ptr) ChpeAutoRtlCopyMemoryStreamTo
712 stdcall RtlCopyOutOfProcessMemoryStreamTo(ptr ptr int64 ptr ptr) ChpeAutoRtlCopyOutOfProcessMemoryStreamTo
713 stdcall RtlCopySecurityDescriptor(ptr ptr) ChpeAutoRtlCopySecurityDescriptor
714 stdcall RtlCopySid(long ptr ptr) ChpeAutoRtlCopySid
715 stdcall RtlCopySidAndAttributesArray(long ptr long ptr ptr ptr ptr) ChpeAutoRtlCopySidAndAttributesArray
716 stdcall RtlCopyString(ptr ptr) ChpeAutoRtlCopyString
718 stdcall RtlCreateAcl(ptr long long) ChpeAutoRtlCreateAcl
719 stdcall RtlCreateActivationContext(long ptr long ptr ptr ptr) ChpeAutoRtlCreateActivationContext
720 stdcall RtlCreateAndSetSD(ptr long ptr ptr ptr) ChpeAutoRtlCreateAndSetSD
721 stdcall RtlContractHashTable(ptr) ChpeAutoRtlContractHashTable
722 stdcall RtlCreateAtomTable(long ptr) ChpeAutoRtlCreateAtomTable
723 stdcall RtlCreateHashTable(ptr long long) ChpeAutoRtlCreateHashTable
724 stdcall RtlCreateBootStatusDataFile() ChpeAutoRtlCreateBootStatusDataFile
725 stdcall -version=0x600+ RtlCreateBoundaryDescriptor(ptr long) ChpeAutoRtlCreateBoundaryDescriptor
726 stdcall RtlCreateEnvironment(long ptr) ChpeAutoRtlCreateEnvironment
727 stdcall -version=0x600+ RtlCreateEnvironmentEx(ptr ptr long) ChpeStubRtlCreateEnvironmentEx
728 stdcall RtlCreateHeap(long ptr long long ptr ptr) ChpeAutoRtlCreateHeap
729 stdcall -version=0x600+ RtlCreateMemoryBlockLookaside(ptr long long long long) ChpeStubRtlCreateMemoryBlockLookaside
730 stdcall -version=0x600+ RtlCreateMemoryZone(ptr long long) ChpeStubRtlCreateMemoryZone
731 stdcall RtlCreateProcessParameters(ptr ptr ptr ptr ptr ptr ptr ptr ptr ptr) ChpeAutoRtlCreateProcessParameters
732 stdcall -version=0x600+ RtlCreateProcessParametersEx(ptr wstr ptr wstr wstr ptr wstr wstr ptr ptr long) ChpeAutoRtlCreateProcessParametersEx
733 stdcall RtlCreateQueryDebugBuffer(long long) ChpeAutoRtlCreateQueryDebugBuffer
734 stdcall RtlCreateRegistryKey(long wstr) ChpeAutoRtlCreateRegistryKey
735 stdcall RtlCreateSecurityDescriptor(ptr long) ChpeAutoRtlCreateSecurityDescriptor
736 stdcall RtlCreateServiceSid(ptr ptr ptr) ChpeAutoRtlCreateServiceSid
737 stdcall -version=0xA00+ RtlDeriveCapabilitySidsFromName(ptr ptr ptr) ChpeAutoRtlDeriveCapabilitySidsFromName
738 stdcall -version=0x602+ RtlIsCapabilitySid(ptr) ChpeAutoRtlIsCapabilitySid
739 stdcall -version=0x602+ RtlGetAppContainerSidType(ptr ptr) ChpeAutoRtlGetAppContainerSidType
740 stdcall -version=0x602+ RtlGetAppContainerParent(ptr ptr) ChpeAutoRtlGetAppContainerParent
741 stdcall -version=0x602+ RtlIsParentOfChildAppContainer(ptr ptr) ChpeAutoRtlIsParentOfChildAppContainer
742 stdcall -version=0x602+ RtlCheckTokenMembershipEx(ptr ptr long ptr) ChpeAutoRtlCheckTokenMembershipEx
743 stdcall RtlCreateSystemVolumeInformationFolder(ptr) ChpeAutoRtlCreateSystemVolumeInformationFolder
744 stdcall RtlCreateTagHeap(ptr long wstr wstr) ChpeAutoRtlCreateTagHeap
745 stdcall RtlCreateTimer(ptr ptr ptr ptr long long long) ChpeAutoRtlCreateTimer
746 stdcall RtlCreateTimerQueue(ptr) ChpeAutoRtlCreateTimerQueue
747 stdcall RtlCreateUnicodeString(ptr wstr) ChpeAutoRtlCreateUnicodeString
748 stdcall RtlCreateUnicodeStringFromAsciiz(ptr str) ChpeAutoRtlCreateUnicodeStringFromAsciiz
749 stdcall RtlCreateUserProcess(ptr long ptr ptr ptr ptr long ptr ptr ptr) ChpeAutoRtlCreateUserProcess
750 stdcall RtlCreateUserSecurityObject(ptr long ptr ptr long ptr ptr) ChpeAutoRtlCreateUserSecurityObject
751 stdcall -version=0x600+ RtlCreateUserStack(long long long long long ptr) ChpeAutoRtlCreateUserStack
753 stdcall -version=0x600+ RtlCultureNameToLCID(ptr ptr) ChpeAutoRtlCultureNameToLCID
754 stdcall RtlCustomCPToUnicodeN(ptr wstr long ptr str long) ChpeAutoRtlCustomCPToUnicodeN
755 stdcall RtlCutoverTimeToSystemTime(ptr ptr ptr long) ChpeAutoRtlCutoverTimeToSystemTime
756 stdcall -version=0x600+ RtlDeCommitDebugInfo(long long long) ChpeStubRtlDeCommitDebugInfo
757 stdcall RtlDeNormalizeProcessParams(ptr) ChpeAutoRtlDeNormalizeProcessParams
758 stdcall RtlDeactivateActivationContext(long long) ChpeAutoRtlDeactivateActivationContext
759 stdcall RtlDeactivateActivationContextUnsafeFast(ptr) ChpeAutoRtlDeactivateActivationContextUnsafeFast
760 stdcall  RtlDebugPrintTimes() ChpeStubRtlDebugPrintTimes
763 stdcall RtlDecompressBuffer(long ptr long ptr long ptr) ChpeAutoRtlDecompressBuffer
765 stdcall RtlDefaultNpAcl(ptr) ChpeAutoRtlDefaultNpAcl
766 stdcall RtlDelete(ptr) ChpeAutoRtlDelete
767 stdcall RtlDeleteAce(ptr long) ChpeAutoRtlDeleteAce
768 stdcall RtlDeleteAtomFromAtomTable(ptr long) ChpeAutoRtlDeleteAtomFromAtomTable
769 stdcall RtlDeleteHashTable(ptr) ChpeAutoRtlDeleteHashTable
770 stdcall -version=0x600+ RtlDeleteBarrier(long) ChpeStubRtlDeleteBarrier
771 stdcall -version=0x600+ RtlDeleteBoundaryDescriptor(ptr) ChpeAutoRtlDeleteBoundaryDescriptor
773 stdcall RtlDeleteElementGenericTable(ptr ptr) ChpeAutoRtlDeleteElementGenericTable
774 stdcall RtlDeleteElementGenericTableAvl(ptr ptr) ChpeAutoRtlDeleteElementGenericTableAvl
777 stdcall RtlDeleteNoSplay(ptr ptr) ChpeAutoRtlDeleteNoSplay
778 stdcall RtlDeleteRegistryValue(long ptr ptr) ChpeAutoRtlDeleteRegistryValue
779 stdcall RtlDeleteResource(ptr) ChpeAutoRtlDeleteResource
780 stdcall RtlDeleteSecurityObject(ptr) ChpeAutoRtlDeleteSecurityObject
781 stdcall RtlDeleteTimer(ptr ptr ptr) ChpeAutoRtlDeleteTimer
782 stdcall RtlDeleteTimerQueue(ptr) ChpeAutoRtlDeleteTimerQueue
783 stdcall RtlDeleteTimerQueueEx(ptr ptr) ChpeAutoRtlDeleteTimerQueueEx
784 stdcall -version=0x600+ RtlDeregisterSecureMemoryCacheCallback(ptr) ChpeStubRtlDeregisterSecureMemoryCacheCallback
785 stdcall RtlDeregisterWait(ptr) ChpeAutoRtlDeregisterWait
786 stdcall RtlDeregisterWaitEx(ptr ptr) ChpeAutoRtlDeregisterWaitEx
787 stdcall RtlDestroyAtomTable(ptr) ChpeAutoRtlDestroyAtomTable
788 stdcall RtlDestroyEnvironment(ptr) ChpeAutoRtlDestroyEnvironment
789 stdcall RtlDestroyHandleTable(ptr) ChpeAutoRtlDestroyHandleTable
790 stdcall RtlDestroyHeap(long) ChpeAutoRtlDestroyHeap
791 stdcall -version=0x600+ RtlDestroyMemoryBlockLookaside(long) ChpeStubRtlDestroyMemoryBlockLookaside
792 stdcall -version=0x600+ RtlDestroyMemoryZone(long) ChpeStubRtlDestroyMemoryZone
793 stdcall RtlDestroyProcessParameters(ptr) ChpeAutoRtlDestroyProcessParameters
794 stdcall RtlDestroyQueryDebugBuffer(ptr) ChpeAutoRtlDestroyQueryDebugBuffer
795 stdcall RtlDetermineDosPathNameType_U(wstr) ChpeAutoRtlDetermineDosPathNameType_U
796 stdcall RtlDllShutdownInProgress() ChpeAutoRtlDllShutdownInProgress
797 stdcall RtlDnsHostNameToComputerName(ptr ptr long) ChpeAutoRtlDnsHostNameToComputerName
798 stdcall RtlDoesFileExists_U(wstr) ChpeAutoRtlDoesFileExists_U
799 stdcall RtlDosApplyFileIsolationRedirection_Ustr(long ptr ptr ptr ptr ptr ptr ptr ptr) ChpeAutoRtlDosApplyFileIsolationRedirection_Ustr
800 stdcall RtlDosPathNameToNtPathName_U(wstr ptr ptr ptr) ChpeAutoRtlDosPathNameToNtPathName_U
801 stdcall RtlDosPathNameToNtPathName_U_WithStatus(wstr ptr ptr ptr) ChpeAutoRtlDosPathNameToNtPathName_U_WithStatus
802 stdcall RtlDosPathNameToRelativeNtPathName_U(ptr ptr ptr ptr) ChpeAutoRtlDosPathNameToRelativeNtPathName_U
803 stdcall RtlDosPathNameToRelativeNtPathName_U_WithStatus(wstr ptr ptr ptr) ChpeAutoRtlDosPathNameToRelativeNtPathName_U_WithStatus
804 stdcall RtlDosSearchPath_U(wstr wstr wstr long ptr ptr) ChpeAutoRtlDosSearchPath_U
805 stdcall RtlDosSearchPath_Ustr(long ptr ptr ptr ptr ptr ptr ptr ptr) ChpeAutoRtlDosSearchPath_Ustr
806 stdcall RtlDowncaseUnicodeChar(long) ChpeAutoRtlDowncaseUnicodeChar
807 stdcall RtlDowncaseUnicodeString(ptr ptr long) ChpeAutoRtlDowncaseUnicodeString
808 stdcall RtlDumpResource(ptr) ChpeAutoRtlDumpResource
809 stdcall RtlDuplicateUnicodeString(long ptr ptr) ChpeAutoRtlDuplicateUnicodeString
810 stdcall RtlEmptyAtomTable(ptr long) ChpeAutoRtlEmptyAtomTable
811 stdcall RtlEndEnumerationHashTable(ptr ptr) ChpeAutoRtlEndEnumerationHashTable
812 stdcall RtlEndWeakEnumerationHashTable(ptr ptr) ChpeAutoRtlEndWeakEnumerationHashTable
813 stdcall  RtlEnableEarlyCriticalSectionEventCreation() ChpeStubRtlEnableEarlyCriticalSectionEventCreation
817 stdcall RtlEnumProcessHeaps(ptr ptr) ChpeAutoRtlEnumProcessHeaps
818 stdcall RtlEnumerateEntryHashTable(ptr ptr) ChpeAutoRtlEnumerateEntryHashTable
819 stdcall RtlEnumerateGenericTable(ptr long) ChpeAutoRtlEnumerateGenericTable
820 stdcall RtlEnumerateGenericTableAvl(ptr long) ChpeAutoRtlEnumerateGenericTableAvl
821 stdcall RtlEnumerateGenericTableLikeADirectory(ptr ptr ptr long ptr ptr ptr) ChpeAutoRtlEnumerateGenericTableLikeADirectory
822 stdcall RtlEnumerateGenericTableWithoutSplaying(ptr ptr) ChpeAutoRtlEnumerateGenericTableWithoutSplaying
823 stdcall RtlEnumerateGenericTableWithoutSplayingAvl(ptr ptr) ChpeAutoRtlEnumerateGenericTableWithoutSplayingAvl
824 stdcall RtlEqualComputerName(ptr ptr) ChpeAutoRtlEqualComputerName
825 stdcall RtlEqualDomainName(ptr ptr) ChpeAutoRtlEqualDomainName
826 stdcall RtlEqualLuid(ptr ptr) ChpeAutoRtlEqualLuid
827 stdcall RtlExpandHashTable(ptr) ChpeAutoRtlExpandHashTable
828 stdcall RtlEqualPrefixSid(ptr ptr) ChpeAutoRtlEqualPrefixSid
829 stdcall RtlEqualSid(long long) ChpeAutoRtlEqualSid
830 stdcall RtlEqualString(ptr ptr long) ChpeAutoRtlEqualString
831 stdcall RtlEqualUnicodeString(ptr ptr long) ChpeAutoRtlEqualUnicodeString
832 stdcall RtlEraseUnicodeString(ptr) ChpeAutoRtlEraseUnicodeString
835 stdcall RtlWow64GetCurrentCpuArea(ptr ptr ptr) ChpeAutoRtlWow64GetCurrentCpuArea
837 stdcall ChpeMarkEcCodeRange(ptr long) ChpeAutoChpeMarkEcCodeRange
838 stdcall ChpeCanContinueToGuest() ChpeAutoChpeCanContinueToGuest
839 stdcall ChpeContinueToGuest(ptr) ChpeAutoChpeContinueToGuest
840 stdcall ChpeContinueToGuestEx(ptr long) ChpeAutoChpeContinueToGuestEx
841 stdcall ProcessPendingCrossProcessEmulatorWork() ChpeAutoProcessPendingCrossProcessEmulatorWork
842 stdcall ChpeIsProcessorFeaturePresent(long) ChpeAutoChpeIsProcessorFeaturePresent
843 stdcall RtlWow64PopAllCrossProcessWorkFromWorkList(ptr ptr) ChpeAutoRtlWow64PopAllCrossProcessWorkFromWorkList
844 stdcall RtlWow64PushCrossProcessWorkOntoFreeList(ptr ptr) ChpeAutoRtlWow64PushCrossProcessWorkOntoFreeList
847 stdcall -version=0x600+ RtlLocateLegacyContext(ptr ptr) ChpeAutoRtlLocateLegacyContext
850 stdcall -version=0x601+ RtlQueryUnbiasedInterruptTime(ptr) ChpeAutoRtlQueryUnbiasedInterruptTime
853 stdcall RtlExitUserProcess(long) ChpeAutoRtlExitUserProcess
855 stdcall -version=0x600+ RtlExpandEnvironmentStrings(long ptr long ptr long ptr) ChpeAutoRtlExpandEnvironmentStrings
856 stdcall RtlExpandEnvironmentStrings_U(ptr ptr ptr ptr) ChpeAutoRtlExpandEnvironmentStrings_U
857 stdcall -version=0x600+ RtlExtendMemoryBlockLookaside(long) ChpeStubRtlExtendMemoryBlockLookaside
858 stdcall -version=0x600+ RtlExtendMemoryZone(long long) ChpeStubRtlExtendMemoryZone
860 stdcall RtlFillMemoryUlong(ptr long long) ChpeAutoRtlFillMemoryUlong
861 stdcall RtlFinalReleaseOutOfProcessMemoryStream(ptr) ChpeAutoRtlFinalReleaseOutOfProcessMemoryStream
862 stdcall -version=0x600+ RtlFindAceByType(long long ptr) ChpeStubRtlFindAceByType
863 stdcall RtlFindActivationContextSectionGuid(long ptr long ptr ptr) ChpeAutoRtlFindActivationContextSectionGuid
864 stdcall RtlFindActivationContextSectionString(long ptr long ptr ptr) ChpeAutoRtlFindActivationContextSectionString
865 stdcall RtlFindCharInUnicodeString(long ptr ptr ptr) ChpeAutoRtlFindCharInUnicodeString
866 stdcall RtlFindClearBits(ptr long long) ChpeAutoRtlFindClearBits
867 stdcall RtlFindClearBitsAndSet(ptr long long) ChpeAutoRtlFindClearBitsAndSet
868 stdcall RtlFindClearRuns(ptr ptr long long) ChpeAutoRtlFindClearRuns
869 stdcall -version=0x600+ RtlFindClosestEncodableLength(long ptr) ChpeStubRtlFindClosestEncodableLength
871 stdcall RtlFindLastBackwardRunClear(ptr long ptr) ChpeAutoRtlFindLastBackwardRunClear
872 stdcall RtlFindLeastSignificantBit(double) ChpeAutoRtlFindLeastSignificantBit
873 stdcall RtlFindLongestRunClear(ptr long) ChpeAutoRtlFindLongestRunClear
874 stdcall RtlFindMessage(long long long long ptr) ChpeAutoRtlFindMessage
875 stdcall RtlFindMostSignificantBit(double) ChpeAutoRtlFindMostSignificantBit
876 stdcall RtlFindNextForwardRunClear(ptr long ptr) ChpeAutoRtlFindNextForwardRunClear
877 stdcall RtlFindSetBits(ptr long long) ChpeAutoRtlFindSetBits
878 stdcall RtlFindSetBitsAndClear(ptr long long) ChpeAutoRtlFindSetBitsAndClear
879 stdcall RtlFirstEntrySList(ptr) ChpeAutoRtlFirstEntrySList
880 stdcall RtlFirstFreeAce(ptr ptr) ChpeAutoRtlFirstFreeAce
885 stdcall RtlFlushSecureMemoryCache(ptr ptr) ChpeAutoRtlFlushSecureMemoryCache
887 stdcall RtlFormatMessage(ptr long long long long ptr ptr long ptr) ChpeAutoRtlFormatMessage
888 stdcall RtlFormatMessageEx(ptr long long long long ptr ptr long ptr long) ChpeAutoRtlFormatMessageEx
889 stdcall RtlFreeActivationContextStack(ptr) ChpeAutoRtlFreeActivationContextStack
890 stdcall RtlFreeAnsiString(long) ChpeAutoRtlFreeAnsiString
891 stdcall RtlFreeHandle(ptr ptr) ChpeAutoRtlFreeHandle
893 stdcall -version=0x600+ RtlFreeMemoryBlockLookaside(long long) ChpeStubRtlFreeMemoryBlockLookaside
894 stdcall RtlFreeOemString(ptr) ChpeAutoRtlFreeOemString
895 stdcall RtlFreeSid(long) ChpeAutoRtlFreeSid
896 stdcall RtlFreeThreadActivationContextStack() ChpeAutoRtlFreeThreadActivationContextStack
898 stdcall -version=0x600+ RtlFreeUserStack(ptr) ChpeAutoRtlFreeUserStack
899 stdcall RtlGUIDFromString(ptr ptr) ChpeAutoRtlGUIDFromString
901 stdcall RtlGetAce(ptr long ptr) ChpeAutoRtlGetAce
902 stdcall RtlGetActiveActivationContext(ptr) ChpeAutoRtlGetActiveActivationContext
903 stdcall RtlGetCallersAddress(ptr ptr) ChpeAutoRtlGetCallersAddress
904 stdcall RtlGetCompressionWorkSpaceSize(long ptr ptr) ChpeAutoRtlGetCompressionWorkSpaceSize
905 stdcall RtlGetControlSecurityDescriptor(ptr ptr ptr) ChpeAutoRtlGetControlSecurityDescriptor
906 stdcall RtlGetCriticalSectionRecursionCount(ptr) ChpeAutoRtlGetCriticalSectionRecursionCount
907 stdcall RtlGetCurrentDirectory_U(long ptr) ChpeAutoRtlGetCurrentDirectory_U
911 stdcall -version=0x600+ RtlGetCurrentTransaction() ChpeStubRtlGetCurrentTransaction
912 stdcall RtlGetDaclSecurityDescriptor(ptr ptr ptr ptr) ChpeAutoRtlGetDaclSecurityDescriptor
913 stdcall -version=0xA00+ RtlGetDeviceFamilyInfoEnum(ptr ptr ptr) ChpeAutoRtlGetDeviceFamilyInfoEnum
914 stdcall -version=0x600+ RtlGetEnabledExtendedFeatures(int64) ChpeAutoRtlGetEnabledExtendedFeatures
915 stdcall RtlGetElementGenericTable(ptr long) ChpeAutoRtlGetElementGenericTable
916 stdcall RtlGetElementGenericTableAvl(ptr long) ChpeAutoRtlGetElementGenericTableAvl
917 stdcall -version=0x600+ RtlGetExtendedContextLength(long ptr) ChpeAutoRtlGetExtendedContextLength
918 stdcall -version=0x600+ RtlGetExtendedContextLength2(long ptr int64) ChpeAutoRtlGetExtendedContextLength2
919 stdcall -version=0x600+ -ret64 RtlGetExtendedFeaturesMask(ptr) ChpeAutoRtlGetExtendedFeaturesMask
920 stdcall -version=0x600+ RtlGetFileMUIPath(long long ptr ptr long long ptr) ChpeStubRtlGetFileMUIPath
921 stdcall RtlGetFrame() ChpeAutoRtlGetFrame
922 stdcall RtlGetFullPathName_U(wstr long ptr ptr) ChpeAutoRtlGetFullPathName_U
923 stdcall RtlGetFullPathName_UstrEx(ptr ptr ptr ptr ptr ptr ptr ptr) ChpeAutoRtlGetFullPathName_UstrEx
925 stdcall RtlGetGroupSecurityDescriptor(ptr ptr ptr) ChpeAutoRtlGetGroupSecurityDescriptor
926 stdcall -version=0x600+ RtlGetIntegerAtom(wstr ptr) ChpeAutoRtlGetIntegerAtom
929 stdcall -version=0x600+ RtlGetLocaleFileMappingAddress(ptr ptr ptr) ChpeAutoRtlGetLocaleFileMappingAddress
931 stdcall RtlGetLengthWithoutTrailingPathSeperators(long ptr ptr) ChpeAutoRtlGetLengthWithoutTrailingPathSeperators
932 stdcall RtlGetLongestNtPathLength() ChpeAutoRtlGetLongestNtPathLength
934 stdcall RtlGetNtGlobalFlags() ChpeAutoRtlGetNtGlobalFlags
935 stdcall RtlGetNtProductType(ptr) ChpeAutoRtlGetNtProductType
936 stdcall RtlGetNtVersionNumbers(ptr ptr ptr) ChpeAutoRtlGetNtVersionNumbers
937 stdcall RtlGetOwnerSecurityDescriptor(ptr ptr ptr) ChpeAutoRtlGetOwnerSecurityDescriptor
938 stdcall -version=0x600+ RtlGetParentLocaleName(wstr long long long) ChpeStubRtlGetParentLocaleName
939 stdcall -version=0x600+ RtlGetProcessPreferredUILanguages(long ptr ptr ptr) ChpeAutoRtlGetProcessPreferredUILanguages
942 stdcall RtlGetSaclSecurityDescriptor(ptr ptr ptr ptr) ChpeAutoRtlGetSaclSecurityDescriptor
943 stdcall RtlGetSecurityDescriptorRMControl(ptr ptr) ChpeAutoRtlGetSecurityDescriptorRMControl
944 stdcall RtlGetSetBootStatusData(ptr long long ptr long long) ChpeAutoRtlGetSetBootStatusData
945 stdcall -version=0x600+ RtlGetSystemPreferredUILanguages(long long ptr ptr ptr) ChpeAutoRtlGetSystemPreferredUILanguages
946 stdcall RtlGetThreadErrorMode() ChpeAutoRtlGetThreadErrorMode
947 stdcall -version=0x600+ RtlGetThreadLangIdByIndex(long long ptr ptr) ChpeStubRtlGetThreadLangIdByIndex
948 stdcall -version=0x600+ RtlGetThreadPreferredUILanguages(long ptr ptr ptr) ChpeAutoRtlGetThreadPreferredUILanguages
949 stdcall -version=0x600+ RtlGetUILanguageInfo(long ptr long ptr ptr) ChpeStubRtlGetUILanguageInfo
951 stdcall -version=0x600+ RtlGetUnloadEventTraceEx(ptr ptr ptr) ChpeAutoRtlGetUnloadEventTraceEx
952 stdcall RtlGetUserInfoHeap(ptr long ptr ptr ptr) ChpeAutoRtlGetUserInfoHeap
953 stdcall -version=0x600+ RtlGetUserPreferredUILanguages(long long ptr ptr ptr) ChpeAutoRtlGetUserPreferredUILanguages
955 stdcall RtlHashUnicodeString(ptr long long ptr) ChpeAutoRtlHashUnicodeString
956 stdcall -version=0x600+ RtlHeapTrkInitialize(ptr) ChpeStubRtlHeapTrkInitialize
957 stdcall RtlIdentifierAuthoritySid(ptr) ChpeAutoRtlIdentifierAuthoritySid
958 stdcall -version=0x600+ RtlIdnToAscii(long wstr long ptr ptr) ChpeAutoRtlIdnToAscii
959 stdcall -version=0x600+ RtlIdnToNameprepUnicode(long wstr long ptr ptr) ChpeAutoRtlIdnToNameprepUnicode
960 stdcall -version=0x600+ RtlIdnToUnicode(long wstr long ptr ptr) ChpeAutoRtlIdnToUnicode
961 stdcall RtlImageDirectoryEntryToData(ptr long long ptr) ChpeAutoRtlImageDirectoryEntryToData
962 stdcall RtlImageNtHeader(long) ChpeAutoRtlImageNtHeader
963 stdcall RtlImageNtHeaderEx(long ptr double ptr) ChpeAutoRtlImageNtHeaderEx
964 stdcall RtlImageRvaToSection(ptr long long) ChpeAutoRtlImageRvaToSection
965 stdcall RtlImageRvaToVa(ptr long long ptr) ChpeAutoRtlImageRvaToVa
966 stdcall RtlImpersonateSelf(long) ChpeAutoRtlImpersonateSelf
967 stdcall -version=0x600+ RtlImpersonateSelfEx(long long ptr) ChpeStubRtlImpersonateSelfEx
968 stdcall RtlInitAnsiString(ptr str) ChpeAutoRtlInitAnsiString
969 stdcall RtlInitAnsiStringEx(ptr str) ChpeAutoRtlInitAnsiStringEx
970 stdcall -version=0x600+ RtlInitBarrier(long long) ChpeStubRtlInitBarrier
971 stdcall RtlInitCodePageTable(ptr ptr) ChpeAutoRtlInitCodePageTable
973 stdcall RtlInitNlsTables(ptr ptr ptr ptr) ChpeAutoRtlInitNlsTables
975 stdcall RtlInitString(ptr str) ChpeAutoRtlInitString
976 stdcall RtlGetNextEntryHashTable(ptr ptr) ChpeAutoRtlGetNextEntryHashTable
977 stdcall RtlInitEnumerationHashTable(ptr ptr) ChpeAutoRtlInitEnumerationHashTable
979 stdcall RtlInitWeakEnumerationHashTable(ptr ptr) ChpeAutoRtlInitWeakEnumerationHashTable
980 stdcall RtlInitUnicodeStringEx(ptr wstr) ChpeAutoRtlInitUnicodeStringEx
981 stdcall  RtlInitializeAtomPackage(ptr) ChpeStubRtlInitializeAtomPackage
982 stdcall RtlInitializeBitMap(ptr long long) ChpeAutoRtlInitializeBitMap
984 stdcall RtlInitializeContext(ptr ptr ptr ptr ptr) ChpeAutoRtlInitializeContext
986 stdcall RtlInitializeCriticalSectionAndSpinCount(ptr long) ChpeAutoRtlInitializeCriticalSectionAndSpinCount
987 stdcall -version=0x600+ RtlInitializeCriticalSectionEx(ptr long long) ChpeAutoRtlInitializeCriticalSectionEx
988 stdcall -version=0x600+ RtlInitializeExtendedContext(ptr long ptr) ChpeAutoRtlInitializeExtendedContext
989 stdcall -version=0x600+ RtlInitializeExtendedContext2(ptr long ptr int64) ChpeAutoRtlInitializeExtendedContext2
990 stdcall RtlInitializeGenericTable(ptr ptr ptr ptr ptr) ChpeAutoRtlInitializeGenericTable
991 stdcall RtlInitializeGenericTableAvl(ptr ptr ptr ptr ptr) ChpeAutoRtlInitializeGenericTableAvl
992 stdcall RtlInitializeHandleTable(long long ptr) ChpeAutoRtlInitializeHandleTable
993 stdcall -version=0x600+ RtlInitializeNtUserPfn(ptr long ptr long ptr long) ChpeAutoRtlInitializeNtUserPfn
994 stdcall RtlInitializeRXact(ptr long ptr) ChpeAutoRtlInitializeRXact
995 stdcall RtlInitializeResource(ptr) ChpeAutoRtlInitializeResource
998 stdcall RtlInitializeSid(ptr ptr long) ChpeAutoRtlInitializeSid
999 stdcall RtlInsertElementGenericTable(ptr ptr long ptr) ChpeAutoRtlInsertElementGenericTable
1000 stdcall RtlInsertEntryHashTable(ptr ptr long ptr) ChpeAutoRtlInsertEntryHashTable
1001 stdcall RtlInsertElementGenericTableAvl(ptr ptr long ptr) ChpeAutoRtlInsertElementGenericTableAvl
1002 stdcall RtlInsertElementGenericTableFull(ptr ptr long ptr ptr long) ChpeAutoRtlInsertElementGenericTableFull
1003 stdcall RtlInsertElementGenericTableFullAvl(ptr ptr long ptr ptr long) ChpeAutoRtlInsertElementGenericTableFullAvl
1006 stdcall RtlInt64ToUnicodeString(double long ptr) ChpeAutoRtlInt64ToUnicodeString
1007 stdcall RtlIntegerToChar(long long long ptr) ChpeAutoRtlIntegerToChar
1008 stdcall RtlIntegerToUnicodeString(long long ptr) ChpeAutoRtlIntegerToUnicodeString
1014 stdcall -version=0x600+ RtlIoDecodeMemIoResource(ptr ptr ptr ptr) ChpeStubRtlIoDecodeMemIoResource
1015 stdcall -version=0x600+ RtlIoEncodeMemIoResource(ptr long long long long long) ChpeStubRtlIoEncodeMemIoResource
1016 stdcall RtlIpv4AddressToStringA(ptr ptr) ChpeAutoRtlIpv4AddressToStringA
1017 stdcall RtlIpv4AddressToStringExA(ptr long ptr ptr) ChpeAutoRtlIpv4AddressToStringExA
1018 stdcall RtlIpv4AddressToStringExW(ptr long ptr ptr) ChpeAutoRtlIpv4AddressToStringExW
1019 stdcall RtlIpv4AddressToStringW(ptr ptr) ChpeAutoRtlIpv4AddressToStringW
1020 stdcall RtlIpv4StringToAddressA(str long ptr ptr) ChpeAutoRtlIpv4StringToAddressA
1021 stdcall RtlIpv4StringToAddressExA(str long ptr ptr) ChpeAutoRtlIpv4StringToAddressExA
1022 stdcall RtlIpv4StringToAddressExW(wstr long ptr ptr) ChpeAutoRtlIpv4StringToAddressExW
1023 stdcall RtlIpv4StringToAddressW(wstr long ptr ptr) ChpeAutoRtlIpv4StringToAddressW
1024 stdcall RtlIpv6AddressToStringA(ptr ptr) ChpeAutoRtlIpv6AddressToStringA
1025 stdcall RtlIpv6AddressToStringExA(ptr long long ptr ptr) ChpeAutoRtlIpv6AddressToStringExA
1026 stdcall RtlIpv6AddressToStringExW(ptr long long ptr ptr) ChpeAutoRtlIpv6AddressToStringExW
1027 stdcall RtlIpv6AddressToStringW(ptr ptr) ChpeAutoRtlIpv6AddressToStringW
1028 stdcall RtlIpv6StringToAddressA(str ptr ptr) ChpeAutoRtlIpv6StringToAddressA
1029 stdcall RtlIpv6StringToAddressExA(str ptr ptr ptr) ChpeAutoRtlIpv6StringToAddressExA
1030 stdcall RtlIpv6StringToAddressExW(wstr ptr ptr ptr) ChpeAutoRtlIpv6StringToAddressExW
1031 stdcall RtlIpv6StringToAddressW(wstr ptr ptr) ChpeAutoRtlIpv6StringToAddressW
1032 stdcall RtlIsActivationContextActive(ptr) ChpeAutoRtlIsActivationContextActive
1033 stdcall RtlIsCriticalSectionLocked(ptr) ChpeAutoRtlIsCriticalSectionLocked
1034 stdcall RtlIsCriticalSectionLockedByThread(ptr) ChpeAutoRtlIsCriticalSectionLockedByThread
1035 stdcall -version=0x600+ RtlIsCurrentProcess(ptr) ChpeAutoRtlIsCurrentProcess
1036 stdcall -version=0x600+ RtlIsCurrentThreadAttachExempt() ChpeStubRtlIsCurrentThreadAttachExempt
1037 stdcall RtlIsDosDeviceName_U(wstr) ChpeAutoRtlIsDosDeviceName_U
1038 stdcall RtlIsGenericTableEmpty(ptr) ChpeAutoRtlIsGenericTableEmpty
1039 stdcall RtlIsGenericTableEmptyAvl(ptr) ChpeAutoRtlIsGenericTableEmptyAvl
1040 stdcall RtlIsNameLegalDOS8Dot3(ptr ptr ptr) ChpeAutoRtlIsNameLegalDOS8Dot3
1041 stdcall -version=0x600+ RtlIsNormalizedString(long ptr long ptr) ChpeAutoRtlIsNormalizedString
1043 stdcall RtlIsTextUnicode(ptr long ptr) ChpeAutoRtlIsTextUnicode
1044 stdcall RtlIsThreadWithinLoaderCallout() ChpeAutoRtlIsThreadWithinLoaderCallout
1045 stdcall RtlIsValidHandle(ptr ptr) ChpeAutoRtlIsValidHandle
1046 stdcall RtlIsValidIndexHandle(ptr long ptr) ChpeAutoRtlIsValidIndexHandle
1047 stdcall -version=0x600+ RtlIsValidLocaleName(wstr long) ChpeAutoRtlIsValidLocaleName
1048 stdcall -version=0x600+ RtlLCIDToCultureName(long ptr) ChpeAutoRtlLCIDToCultureName
1049 stdcall RtlLargeIntegerToChar(ptr long long ptr) ChpeAutoRtlLargeIntegerToChar
1050 stdcall -version=0x600+ RtlLcidToLocaleName(long ptr long long) ChpeAutoRtlLcidToLocaleName
1052 stdcall RtlLengthRequiredSid(long) ChpeAutoRtlLengthRequiredSid
1053 stdcall RtlLengthSecurityDescriptor(ptr) ChpeAutoRtlLengthSecurityDescriptor
1054 stdcall RtlLengthSid(ptr) ChpeAutoRtlLengthSid
1055 stdcall RtlLocalTimeToSystemTime(ptr ptr) ChpeAutoRtlLocalTimeToSystemTime
1056 stdcall -version=0x600+ RtlLocaleNameToLcid(wstr ptr long) ChpeAutoRtlLocaleNameToLcid
1057 stdcall RtlLockBootStatusData(ptr) ChpeAutoRtlLockBootStatusData
1058 stdcall -version=0x600+ RtlLockCurrentThread() ChpeStubRtlLockCurrentThread
1059 stdcall RtlLockHeap(long) ChpeAutoRtlLockHeap
1060 stdcall -version=0x600+ RtlLockMemoryBlockLookaside(long) ChpeStubRtlLockMemoryBlockLookaside
1061 stdcall RtlLockMemoryStreamRegion(ptr int64 int64 long) ChpeAutoRtlLockMemoryStreamRegion
1062 stdcall -version=0x600+ RtlLockMemoryZone(long) ChpeStubRtlLockMemoryZone
1063 stdcall -version=0x600+ RtlLockModuleSection(long) ChpeStubRtlLockModuleSection
1065 stdcall  RtlLogStackBackTrace() ChpeStubRtlLogStackBackTrace
1066 stdcall RtlLookupAtomInAtomTable(ptr wstr ptr) ChpeAutoRtlLookupAtomInAtomTable
1067 stdcall RtlLookupElementGenericTable(ptr ptr) ChpeAutoRtlLookupElementGenericTable
1068 stdcall RtlLookupEntryHashTable(ptr long ptr) ChpeAutoRtlLookupEntryHashTable
1069 stdcall RtlRemoveEntryHashTable(ptr ptr ptr) ChpeAutoRtlRemoveEntryHashTable
1070 stdcall RtlLookupElementGenericTableAvl(ptr ptr) ChpeAutoRtlLookupElementGenericTableAvl
1071 stdcall RtlLookupElementGenericTableFull(ptr ptr ptr long) ChpeAutoRtlLookupElementGenericTableFull
1072 stdcall RtlLookupElementGenericTableFullAvl(ptr ptr ptr long) ChpeAutoRtlLookupElementGenericTableFullAvl
1075 stdcall RtlMakeSelfRelativeSD(ptr ptr ptr) ChpeAutoRtlMakeSelfRelativeSD
1076 stdcall RtlMapGenericMask(long ptr) ChpeAutoRtlMapGenericMask
1077 stdcall RtlMapSecurityErrorToNtStatus(long) ChpeAutoRtlMapSecurityErrorToNtStatus
1079 stdcall RtlMultiAppendUnicodeStringBuffer(ptr long ptr) ChpeAutoRtlMultiAppendUnicodeStringBuffer
1080 stdcall RtlMultiByteToUnicodeN(ptr long ptr ptr long) ChpeAutoRtlMultiByteToUnicodeN
1081 stdcall RtlMultiByteToUnicodeSize(ptr str long) ChpeAutoRtlMultiByteToUnicodeSize
1082 stdcall RtlMultipleAllocateHeap(ptr long ptr long ptr) ChpeAutoRtlMultipleAllocateHeap
1083 stdcall RtlMultipleFreeHeap(ptr long long ptr) ChpeAutoRtlMultipleFreeHeap
1084 stdcall RtlNewInstanceSecurityObject(long long ptr ptr ptr ptr ptr long ptr ptr) ChpeAutoRtlNewInstanceSecurityObject
1085 stdcall RtlNewSecurityGrantedAccess(long ptr ptr ptr ptr ptr) ChpeAutoRtlNewSecurityGrantedAccess
1086 stdcall RtlNewSecurityObject(ptr ptr ptr long ptr ptr) ChpeAutoRtlNewSecurityObject
1087 stdcall RtlNewSecurityObjectEx(ptr ptr ptr ptr long long ptr ptr) ChpeAutoRtlNewSecurityObjectEx
1088 stdcall RtlNewSecurityObjectWithMultipleInheritance(ptr ptr ptr ptr long long long ptr ptr) ChpeAutoRtlNewSecurityObjectWithMultipleInheritance
1089 stdcall RtlNormalizeProcessParams(ptr) ChpeAutoRtlNormalizeProcessParams
1090 stdcall -version=0x600+ RtlNormalizeString(long ptr long ptr ptr) ChpeAutoRtlNormalizeString
1091 stdcall RtlNtPathNameToDosPathName(long ptr ptr ptr) ChpeAutoRtlNtPathNameToDosPathName
1093 stdcall RtlNtStatusToDosErrorNoTeb(long) ChpeAutoRtlNtStatusToDosErrorNoTeb
1094 stub -version=0x600+ RtlNtdllName
1095 stdcall RtlNumberGenericTableElements(ptr) ChpeAutoRtlNumberGenericTableElements
1096 stdcall RtlNumberGenericTableElementsAvl(ptr) ChpeAutoRtlNumberGenericTableElementsAvl
1097 stdcall RtlNumberOfClearBits(ptr) ChpeAutoRtlNumberOfClearBits
1098 stdcall RtlNumberOfSetBits(ptr) ChpeAutoRtlNumberOfSetBits
1099 stdcall -version=0x600+ RtlNumberOfSetBitsUlongPtr(long) ChpeAutoRtlNumberOfSetBitsUlongPtr
1100 stdcall RtlOemStringToUnicodeSize(ptr) ChpeAutoRtlOemStringToUnicodeSize
1101 stdcall RtlOemStringToUnicodeString(ptr ptr long) ChpeAutoRtlOemStringToUnicodeString
1102 stdcall RtlOemToUnicodeN(ptr long ptr ptr long) ChpeAutoRtlOemToUnicodeN
1103 stdcall RtlOpenCurrentUser(long ptr) ChpeAutoRtlOpenCurrentUser
1104 stdcall -version=0x600+ RtlOwnerAcesPresent(long) ChpeStubRtlOwnerAcesPresent
1106 stdcall RtlPinAtomInAtomTable(ptr long) ChpeAutoRtlPinAtomInAtomTable
1107 stdcall RtlPopFrame(ptr) ChpeAutoRtlPopFrame
1108 stdcall RtlPrefixString(ptr ptr long) ChpeAutoRtlPrefixString
1109 stdcall RtlPrefixUnicodeString(ptr ptr long) ChpeAutoRtlPrefixUnicodeString
1110 stdcall -version=0x600+ RtlPrepareForProcessCloning() ChpeStubRtlPrepareForProcessCloning
1111 stdcall -version=0x600+ RtlProcessFlsData(ptr long) ChpeAutoRtlProcessFlsData
1112 stdcall RtlProtectHeap(ptr long) ChpeAutoRtlProtectHeap
1113 stdcall RtlPushFrame(ptr) ChpeAutoRtlPushFrame
1114 stdcall -version=0x600+ RtlQueryActivationContextApplicationSettings(long ptr wstr wstr ptr ptr ptr) ChpeAutoRtlQueryActivationContextApplicationSettings
1115 stdcall RtlQueryAtomInAtomTable(ptr long ptr ptr ptr ptr) ChpeAutoRtlQueryAtomInAtomTable
1116 stdcall -version=0x600+ RtlQueryCriticalSectionOwner(ptr) ChpeStubRtlQueryCriticalSectionOwner
1118 stdcall -version=0x600+ RtlQueryDynamicTimeZoneInformation(ptr) ChpeAutoRtlQueryDynamicTimeZoneInformation
1119 stdcall -version=0x600+ RtlQueryElevationFlags(ptr) ChpeStubRtlQueryElevationFlags
1121 stdcall RtlQueryEnvironmentVariable_U(ptr ptr ptr) ChpeAutoRtlQueryEnvironmentVariable_U
1122 stdcall RtlQueryHeapInformation(long long ptr long ptr) ChpeAutoRtlQueryHeapInformation
1123 stdcall RtlQueryInformationAcl(ptr ptr long long) ChpeAutoRtlQueryInformationAcl
1124 stdcall RtlQueryInformationActivationContext(long long ptr long ptr long ptr) ChpeAutoRtlQueryInformationActivationContext
1125 stdcall RtlQueryInformationActiveActivationContext(long ptr long ptr) ChpeAutoRtlQueryInformationActiveActivationContext
1126 stdcall RtlQueryInterfaceMemoryStream(ptr ptr ptr) ChpeAutoRtlQueryInterfaceMemoryStream
1127 stdcall -version=0x600+ RtlQueryModuleInformation(ptr long ptr) ChpeStubRtlQueryModuleInformation
1128 stdcall  RtlQueryProcessBackTraceInformation(ptr) ChpeStubRtlQueryProcessBackTraceInformation
1129 stdcall RtlQueryProcessDebugInformation(long long ptr) ChpeAutoRtlQueryProcessDebugInformation
1130 stdcall RtlQueryProcessHeapInformation(ptr) ChpeAutoRtlQueryProcessHeapInformation
1131 stdcall  RtlQueryProcessLockInformation(ptr) ChpeStubRtlQueryProcessLockInformation
1132 stdcall RtlQueryRegistryValues(long ptr ptr ptr ptr) ChpeAutoRtlQueryRegistryValues
1133 stdcall RtlQueryRegistryValuesEx(long ptr ptr ptr ptr) ChpeAutoRtlQueryRegistryValuesEx
1134 stdcall RtlQuerySecurityObject(ptr long ptr long ptr) ChpeAutoRtlQuerySecurityObject
1135 stdcall RtlQueryTagHeap(ptr long long long ptr) ChpeAutoRtlQueryTagHeap
1136 stdcall RtlQueryTimeZoneInformation(ptr) ChpeAutoRtlQueryTimeZoneInformation
1137 stdcall RtlQueueApcWow64Thread(ptr ptr ptr ptr ptr) ChpeAutoRtlQueueApcWow64Thread
1142 stdcall RtlRandomEx(ptr) ChpeAutoRtlRandomEx
1144 stdcall RtlReadMemoryStream(ptr ptr long ptr) ChpeAutoRtlReadMemoryStream
1145 stdcall RtlReadOutOfProcessMemoryStream(ptr ptr long ptr) ChpeAutoRtlReadOutOfProcessMemoryStream
1146 stdcall RtlRealPredecessor(ptr) ChpeAutoRtlRealPredecessor
1147 stdcall RtlRealSuccessor(ptr) ChpeAutoRtlRealSuccessor
1148 stdcall RtlRegisterSecureMemoryCacheCallback(ptr) ChpeAutoRtlRegisterSecureMemoryCacheCallback
1149 stdcall RtlRegisterCfgTargetRange(ptr long) ChpeAutoRtlRegisterCfgTargetRange
1150 stdcall -version=0x600+ RtlRegisterThreadWithCsrss() ChpeStubRtlRegisterThreadWithCsrss
1152 stdcall RtlReleaseActivationContext(ptr) ChpeAutoRtlReleaseActivationContext
1154 stdcall RtlReleasePebLock() ChpeAutoRtlReleasePebLock
1156 stdcall RtlReleaseRelativeName(ptr) ChpeAutoRtlReleaseRelativeName
1157 stdcall RtlReleaseResource(ptr) ChpeAutoRtlReleaseResource
1160 stdcall RtlRemoteCall(ptr ptr ptr long ptr long long) ChpeAutoRtlRemoteCall
1161 stdcall -version=0x600+ RtlRemovePrivileges(ptr ptr long) ChpeAutoRtlRemovePrivileges
1164 stdcall -version=0x600+ RtlReportException(long long long) ChpeStubRtlReportException
1165 stdcall -version=0x600+ RtlResetMemoryBlockLookaside(long) ChpeStubRtlResetMemoryBlockLookaside
1166 stdcall -version=0x600+ RtlResetMemoryZone(long) ChpeStubRtlResetMemoryZone
1167 stdcall -version=0x600+ RtlResetNtUserPfn() ChpeAutoRtlResetNtUserPfn
1168 stdcall RtlResetRtlTranslations(ptr) ChpeAutoRtlResetRtlTranslations
1171 stdcall -version=0x600+ RtlRetrieveNtUserPfn(ptr ptr ptr) ChpeAutoRtlRetrieveNtUserPfn
1172 stdcall RtlRevertMemoryStream(ptr) ChpeAutoRtlRevertMemoryStream
1173 stdcall RtlRunDecodeUnicodeString(long ptr) ChpeAutoRtlRunDecodeUnicodeString
1174 stdcall RtlRunEncodeUnicodeString(long ptr) ChpeAutoRtlRunEncodeUnicodeString
1175 stdcall -version=0x600+ RtlRunOnceBeginInitialize(ptr long ptr) ChpeAutoRtlRunOnceBeginInitialize
1176 stdcall -version=0x600+ RtlRunOnceComplete(ptr long ptr) ChpeAutoRtlRunOnceComplete
1179 stdcall RtlSecondsSince1970ToTime(long ptr) ChpeAutoRtlSecondsSince1970ToTime
1180 stdcall RtlSecondsSince1980ToTime(long ptr) ChpeAutoRtlSecondsSince1980ToTime
1181 stdcall RtlSeekMemoryStream(ptr int64 long ptr) ChpeAutoRtlSeekMemoryStream
1182 stdcall RtlSelfRelativeToAbsoluteSD2(ptr ptr) ChpeAutoRtlSelfRelativeToAbsoluteSD2
1183 stdcall RtlSelfRelativeToAbsoluteSD(ptr ptr ptr ptr ptr ptr ptr ptr ptr ptr ptr) ChpeAutoRtlSelfRelativeToAbsoluteSD
1184 stdcall -version=0x600+ RtlSendMsgToSm(ptr ptr) ChpeStubRtlSendMsgToSm
1185 stdcall RtlSetAllBits(ptr) ChpeAutoRtlSetAllBits
1186 stdcall RtlSetAttributesSecurityDescriptor(ptr long ptr) ChpeAutoRtlSetAttributesSecurityDescriptor
1187 stdcall RtlSetBits(ptr long long) ChpeAutoRtlSetBits
1188 stdcall RtlSetCfgTargetValidity(ptr long long ptr) ChpeAutoRtlSetCfgTargetValidity
1189 stdcall RtlSetControlSecurityDescriptor(ptr long long) ChpeAutoRtlSetControlSecurityDescriptor
1191 stdcall RtlSetCurrentDirectory_U(ptr) ChpeAutoRtlSetCurrentDirectory_U
1192 stdcall RtlSetCurrentEnvironment(wstr ptr) ChpeAutoRtlSetCurrentEnvironment
1193 stdcall -version=0x600+ RtlSetCurrentTransaction(ptr) ChpeStubRtlSetCurrentTransaction
1194 stdcall RtlSetDaclSecurityDescriptor(ptr long ptr long) ChpeAutoRtlSetDaclSecurityDescriptor
1195 stdcall -version=0x600+ RtlSetDynamicTimeZoneInformation(long) ChpeStubRtlSetDynamicTimeZoneInformation
1196 stdcall RtlSetEnvironmentStrings(wstr long) ChpeAutoRtlSetEnvironmentStrings
1197 stdcall -version=0x600+ RtlSetEnvironmentVar(ptr ptr long ptr long) ChpeStubRtlSetEnvironmentVar
1198 stdcall RtlSetEnvironmentVariable(ptr ptr ptr) ChpeAutoRtlSetEnvironmentVariable
1199 stdcall -version=0x600+ RtlSetExtendedFeaturesMask(ptr int64) ChpeAutoRtlSetExtendedFeaturesMask
1200 stdcall RtlSetGroupSecurityDescriptor(ptr ptr long) ChpeAutoRtlSetGroupSecurityDescriptor
1201 stdcall RtlSetHeapInformation(ptr long ptr ptr) ChpeAutoRtlSetHeapInformation
1202 stdcall RtlSetInformationAcl(ptr ptr long long) ChpeAutoRtlSetInformationAcl
1203 stdcall RtlSetIoCompletionCallback(long ptr long) ChpeAutoRtlSetIoCompletionCallback
1205 stdcall RtlSetLastWin32ErrorAndNtStatusFromNtStatus(long) ChpeAutoRtlSetLastWin32ErrorAndNtStatusFromNtStatus
1206 stdcall RtlSetMemoryStreamSize(ptr int64) ChpeAutoRtlSetMemoryStreamSize
1207 stdcall RtlSetOwnerSecurityDescriptor(ptr ptr long) ChpeAutoRtlSetOwnerSecurityDescriptor
1208 stdcall -version=0x600+ RtlSetProcessDebugInformation(ptr long ptr) ChpeStubRtlSetProcessDebugInformation
1209 stdcall -version=0x600+ RtlSetProcessPreferredUILanguages(long ptr ptr) ChpeAutoRtlSetProcessPreferredUILanguages
1210 cdecl RtlSetProcessIsCritical(long ptr long) ChpeAutoRtlSetProcessIsCritical
1211 stdcall RtlSetSaclSecurityDescriptor(ptr long ptr long) ChpeAutoRtlSetSaclSecurityDescriptor
1212 stdcall RtlSetSecurityDescriptorRMControl(ptr ptr) ChpeAutoRtlSetSecurityDescriptorRMControl
1213 stdcall RtlSetSecurityObject(long ptr ptr ptr ptr) ChpeAutoRtlSetSecurityObject
1214 stdcall RtlSetSecurityObjectEx(long ptr ptr long ptr ptr) ChpeAutoRtlSetSecurityObjectEx
1215 stdcall RtlSetThreadErrorMode(long ptr) ChpeAutoRtlSetThreadErrorMode
1216 cdecl RtlSetThreadIsCritical(long ptr long) ChpeAutoRtlSetThreadIsCritical
1217 stdcall RtlSetThreadPoolStartFunc(ptr ptr) ChpeAutoRtlSetThreadPoolStartFunc
1218 stdcall -version=0x600+ RtlSetThreadPreferredUILanguages(long ptr ptr) ChpeStubRtlSetThreadPreferredUILanguages
1219 stdcall RtlSetTimeZoneInformation(ptr) ChpeAutoRtlSetTimeZoneInformation
1220 stdcall RtlSetTimer(ptr ptr ptr ptr long long long) ChpeAutoRtlSetTimer
1221 stdcall RtlSetUnhandledExceptionFilter(ptr) ChpeAutoRtlSetUnhandledExceptionFilter
1222 stdcall RtlSetUserFlagsHeap(ptr long ptr long long) ChpeAutoRtlSetUserFlagsHeap
1223 stdcall RtlSetUserValueHeap(ptr long ptr ptr) ChpeAutoRtlSetUserValueHeap
1224 stdcall -version=0x600+ RtlSidDominates(long long ptr) ChpeStubRtlSidDominates
1225 stdcall -version=0x600+ RtlSidEqualLevel(long long ptr) ChpeStubRtlSidEqualLevel
1226 stdcall -version=0x600+ RtlSidHashInitialize(ptr long ptr) ChpeStubRtlSidHashInitialize
1227 stdcall -version=0x600+ RtlSidHashLookup(long ptr) ChpeStubRtlSidHashLookup
1228 stdcall -version=0x600+ RtlSidIsHigherLevel(long long ptr) ChpeStubRtlSidIsHigherLevel
1230 stdcall -version=0x600+ RtlSleepConditionVariableCS(ptr ptr ptr) ChpeAutoRtlSleepConditionVariableCS
1231 stdcall -version=0x600+ RtlSleepConditionVariableSRW(ptr ptr ptr long) ChpeAutoRtlSleepConditionVariableSRW
1232 stdcall RtlSplay(ptr) ChpeAutoRtlSplay
1233 stdcall RtlStartRXact(ptr) ChpeAutoRtlStartRXact
1234 stdcall RtlStatMemoryStream(ptr ptr long) ChpeAutoRtlStatMemoryStream
1235 stdcall RtlStringFromGUID(ptr ptr) ChpeAutoRtlStringFromGUID
1236 stdcall RtlSubAuthorityCountSid(ptr) ChpeAutoRtlSubAuthorityCountSid
1237 stdcall RtlSubAuthoritySid(ptr long) ChpeAutoRtlSubAuthoritySid
1238 stdcall RtlSubtreePredecessor(ptr) ChpeAutoRtlSubtreePredecessor
1239 stdcall RtlSubtreeSuccessor(ptr) ChpeAutoRtlSubtreeSuccessor
1240 stdcall RtlSystemTimeToLocalTime(ptr ptr) ChpeAutoRtlSystemTimeToLocalTime
1241 stdcall -version=0x600+ RtlTestBit(ptr long) ChpeAutoRtlTestBit
1242 stdcall RtlTimeFieldsToTime(ptr ptr) ChpeAutoRtlTimeFieldsToTime
1243 stdcall RtlTimeToElapsedTimeFields(long long) ChpeAutoRtlTimeToElapsedTimeFields
1244 stdcall RtlTimeToSecondsSince1970(ptr ptr) ChpeAutoRtlTimeToSecondsSince1970
1245 stdcall RtlTimeToSecondsSince1980(ptr ptr) ChpeAutoRtlTimeToSecondsSince1980
1246 stdcall RtlTimeToTimeFields(long long) ChpeAutoRtlTimeToTimeFields
1247 stdcall RtlTraceDatabaseAdd(ptr long ptr ptr) ChpeAutoRtlTraceDatabaseAdd
1248 stdcall RtlTraceDatabaseCreate(long ptr long long ptr) ChpeAutoRtlTraceDatabaseCreate
1249 stdcall RtlTraceDatabaseDestroy(ptr) ChpeAutoRtlTraceDatabaseDestroy
1250 stdcall RtlTraceDatabaseEnumerate(ptr ptr ptr) ChpeAutoRtlTraceDatabaseEnumerate
1251 stdcall RtlTraceDatabaseFind(ptr long ptr ptr) ChpeAutoRtlTraceDatabaseFind
1252 stdcall RtlTraceDatabaseLock(ptr) ChpeAutoRtlTraceDatabaseLock
1253 stdcall RtlTraceDatabaseUnlock(ptr) ChpeAutoRtlTraceDatabaseUnlock
1254 stdcall RtlTraceDatabaseValidate(ptr) ChpeAutoRtlTraceDatabaseValidate
1255 stdcall -version=0x600+ RtlTryAcquirePebLock() ChpeStubRtlTryAcquirePebLock
1259 stdcall RtlUnhandledExceptionFilter2(ptr long) ChpeAutoRtlUnhandledExceptionFilter2
1260 stdcall RtlUnhandledExceptionFilter(ptr) ChpeAutoRtlUnhandledExceptionFilter
1261 stdcall RtlUnicodeStringToAnsiSize(ptr) ChpeAutoRtlUnicodeStringToAnsiSize
1262 stdcall -version=0x601+ RtlUnicodeToUTF8N(ptr long ptr wstr long) ChpeAutoRtlUnicodeToUTF8N
1264 stdcall RtlUnicodeStringToAnsiString(ptr ptr long) ChpeAutoRtlUnicodeStringToAnsiString
1265 stdcall RtlUnicodeStringToCountedOemString(ptr ptr long) ChpeAutoRtlUnicodeStringToCountedOemString
1266 stdcall RtlUnicodeStringToInteger(ptr long ptr) ChpeAutoRtlUnicodeStringToInteger
1267 stdcall RtlUnicodeStringToOemSize(ptr) ChpeAutoRtlUnicodeStringToOemSize
1268 stdcall RtlUnicodeStringToOemString(ptr ptr long) ChpeAutoRtlUnicodeStringToOemString
1269 stdcall RtlUnicodeToCustomCPN(ptr ptr long ptr wstr long) ChpeAutoRtlUnicodeToCustomCPN
1270 stdcall RtlUnicodeToMultiByteN(ptr long ptr ptr long) ChpeAutoRtlUnicodeToMultiByteN
1271 stdcall RtlUnicodeToMultiByteSize(ptr ptr long) ChpeAutoRtlUnicodeToMultiByteSize
1272 stdcall RtlUnicodeToOemN(ptr long ptr ptr long) ChpeAutoRtlUnicodeToOemN
1273 stdcall RtlUniform(ptr) ChpeAutoRtlUniform
1274 stdcall RtlUnlockBootStatusData(ptr) ChpeAutoRtlUnlockBootStatusData
1275 stdcall -version=0x600+ RtlUnlockCurrentThread() ChpeStubRtlUnlockCurrentThread
1276 stdcall RtlUnlockHeap(long) ChpeAutoRtlUnlockHeap
1277 stdcall -version=0x600+ RtlUnlockMemoryBlockLookaside(long) ChpeStubRtlUnlockMemoryBlockLookaside
1278 stdcall RtlUnlockMemoryStreamRegion(ptr int64 int64 long) ChpeAutoRtlUnlockMemoryStreamRegion
1279 stdcall -version=0x600+ RtlUnlockMemoryZone(long) ChpeStubRtlUnlockMemoryZone
1280 stdcall -version=0x600+ RtlUnlockModuleSection(long) ChpeStubRtlUnlockModuleSection
1283 stdcall RtlUnregisterCfgTargetRange(ptr) ChpeAutoRtlUnregisterCfgTargetRange
1284 stdcall RtlUpcaseUnicodeChar(long) ChpeAutoRtlUpcaseUnicodeChar
1285 stdcall RtlUpcaseUnicodeString(ptr ptr long) ChpeAutoRtlUpcaseUnicodeString
1286 stdcall RtlUpcaseUnicodeStringToAnsiString(ptr ptr long) ChpeAutoRtlUpcaseUnicodeStringToAnsiString
1287 stdcall RtlUpcaseUnicodeStringToCountedOemString(ptr ptr long) ChpeAutoRtlUpcaseUnicodeStringToCountedOemString
1288 stdcall RtlUpcaseUnicodeStringToOemString(ptr ptr long) ChpeAutoRtlUpcaseUnicodeStringToOemString
1289 stdcall RtlUpcaseUnicodeToCustomCPN(ptr ptr long ptr wstr long) ChpeAutoRtlUpcaseUnicodeToCustomCPN
1290 stdcall RtlUpcaseUnicodeToMultiByteN(ptr long ptr ptr long) ChpeAutoRtlUpcaseUnicodeToMultiByteN
1291 stdcall RtlUpcaseUnicodeToOemN(ptr long ptr ptr long) ChpeAutoRtlUpcaseUnicodeToOemN
1292 stdcall -version=0x600+ RtlUpdateClonedCriticalSection(long) ChpeStubRtlUpdateClonedCriticalSection
1293 stdcall -version=0x600+ RtlUpdateClonedSRWLock(ptr long) ChpeStubRtlUpdateClonedSRWLock
1294 stdcall RtlUpdateTimer(ptr ptr long long) ChpeAutoRtlUpdateTimer
1295 stdcall RtlUpperChar(long) ChpeAutoRtlUpperChar
1296 stdcall RtlUpperString(ptr ptr) ChpeAutoRtlUpperString
1298 stdcall RtlValidAcl(ptr) ChpeAutoRtlValidAcl
1299 stdcall RtlValidRelativeSecurityDescriptor(ptr long long) ChpeAutoRtlValidRelativeSecurityDescriptor
1300 stdcall RtlValidSecurityDescriptor(ptr) ChpeAutoRtlValidSecurityDescriptor
1301 stdcall RtlValidSid(ptr) ChpeAutoRtlValidSid
1302 stdcall RtlValidateHeap(long long ptr) ChpeAutoRtlValidateHeap
1303 stdcall RtlValidateProcessHeaps() ChpeAutoRtlValidateProcessHeaps
1304 stdcall RtlValidateUnicodeString(long ptr) ChpeAutoRtlValidateUnicodeString
1305 stdcall RtlVerifyVersionInfo(ptr long double) ChpeAutoRtlVerifyVersionInfo
1307 stdcall RtlVirtualUnwind2(long int64 int64 ptr ptr ptr ptr ptr ptr ptr ptr ptr long) ChpeAutoRtlVirtualUnwind2
1313 stdcall RtlWalkFrameChain(ptr long long) ChpeAutoRtlWalkFrameChain
1314 stdcall RtlWeaklyEnumerateEntryHashTable(ptr ptr) ChpeAutoRtlWeaklyEnumerateEntryHashTable
1315 stdcall RtlWalkHeap(long ptr) ChpeAutoRtlWalkHeap
1316 stdcall -version=0x600+ RtlWerpReportException(long long ptr long long ptr) ChpeStubRtlWerpReportException
1317 stdcall -version=0x600+ RtlWow64CallFunction64() ChpeStubRtlWow64CallFunction64
1318 stdcall RtlWow64EnableFsRedirection(long) ChpeAutoRtlWow64EnableFsRedirection
1319 stdcall RtlWow64EnableFsRedirectionEx(long ptr) ChpeAutoRtlWow64EnableFsRedirectionEx
1320 stdcall -version=0x600+ RtlOpenCrossProcessEmulatorWorkConnection(ptr ptr ptr) ChpeAutoRtlOpenCrossProcessEmulatorWorkConnection
1321 stdcall -version=0x600+ RtlWow64GetCpuAreaInfo(ptr long ptr) ChpeAutoRtlWow64GetCpuAreaInfo
1322 stdcall -version=0x600+ RtlWow64GetCurrentMachine() ChpeAutoRtlWow64GetCurrentMachine
1323 stdcall -version=0x600+ RtlWow64GetProcessMachines(ptr ptr ptr) ChpeAutoRtlWow64GetProcessMachines
1324 stdcall -version=0x600+ RtlWow64GetThreadContext(ptr ptr) ChpeAutoRtlWow64GetThreadContext
1326 stdcall -version=0x600+ RtlWow64LogMessageInEventLogger(long long long) ChpeStubRtlWow64LogMessageInEventLogger
1327 stdcall -version=0x600+ RtlWow64PopCrossProcessWorkFromFreeList(ptr) ChpeAutoRtlWow64PopCrossProcessWorkFromFreeList
1328 stdcall -version=0x600+ RtlWow64PushCrossProcessWorkOntoWorkList(ptr ptr ptr) ChpeAutoRtlWow64PushCrossProcessWorkOntoWorkList
1329 stdcall -version=0x600+ RtlWow64RequestCrossProcessHeavyFlush(ptr) ChpeAutoRtlWow64RequestCrossProcessHeavyFlush
1330 stdcall -version=0x600+ RtlWow64SetThreadContext(ptr ptr) ChpeAutoRtlWow64SetThreadContext
1331 stdcall -version=0x600+ RtlWow64SuspendThread(ptr ptr) ChpeAutoRtlWow64SuspendThread
1332 stdcall RtlWriteMemoryStream(ptr ptr long ptr) ChpeAutoRtlWriteMemoryStream
1333 stdcall RtlWriteRegistryValue(long ptr ptr long ptr long) ChpeAutoRtlWriteRegistryValue
1334 stdcall RtlZeroHeap(ptr long) ChpeAutoRtlZeroHeap
1336 stdcall RtlZombifyActivationContext(ptr) ChpeAutoRtlZombifyActivationContext
1338 stdcall -version=0x600+ RtlpCheckDynamicTimeZoneInformation(ptr long) ChpeStubRtlpCheckDynamicTimeZoneInformation
1339 stdcall -version=0x600+ RtlpCleanupRegistryKeys() ChpeStubRtlpCleanupRegistryKeys
1340 stdcall -version=0x600+ RtlpConvertCultureNamesToLCIDs(wstr ptr) ChpeStubRtlpConvertCultureNamesToLCIDs
1341 stdcall -version=0x600+ RtlpConvertLCIDsToCultureNames(wstr ptr) ChpeStubRtlpConvertLCIDsToCultureNames
1342 stdcall -version=0x600+ RtlpCreateProcessRegistryInfo(ptr) ChpeStubRtlpCreateProcessRegistryInfo
1343 stdcall RtlpEnsureBufferSize(long ptr long) ChpeAutoRtlpEnsureBufferSize
1344 stdcall -version=0x600+ RtlpGetLCIDFromLangInfoNode(long long ptr) ChpeStubRtlpGetLCIDFromLangInfoNode
1345 stdcall -version=0x600+ RtlpGetNameFromLangInfoNode(long long long) ChpeStubRtlpGetNameFromLangInfoNode
1346 stdcall -version=0x600+ RtlpGetSystemDefaultUILanguage(ptr long) ChpeStubRtlpGetSystemDefaultUILanguage
1347 stdcall -version=0x600+ RtlpGetUserOrMachineUILanguage4NLS(long long ptr) ChpeStubRtlpGetUserOrMachineUILanguage4NLS
1348 stdcall -version=0x600+ RtlpInitializeLangRegistryInfo(ptr) ChpeStubRtlpInitializeLangRegistryInfo
1349 stdcall -version=0x600+ RtlpIsQualifiedLanguage(long ptr long) ChpeStubRtlpIsQualifiedLanguage
1350 stdcall -version=0x600+ RtlpLoadMachineUIByPolicy(ptr long ptr) ChpeStubRtlpLoadMachineUIByPolicy
1351 stdcall -version=0x600+ RtlpLoadUserUIByPolicy(ptr long ptr) ChpeStubRtlpLoadUserUIByPolicy
1352 stdcall -version=0x600+ RtlpMuiFreeLangRegistryInfo(long) ChpeStubRtlpMuiFreeLangRegistryInfo
1353 stdcall -version=0x600+ RtlpMuiRegCreateRegistryInfo() ChpeStubRtlpMuiRegCreateRegistryInfo
1354 stdcall -version=0x600+ RtlpMuiRegFreeRegistryInfo(long long) ChpeStubRtlpMuiRegFreeRegistryInfo
1355 stdcall -version=0x600+ RtlpMuiRegLoadRegistryInfo(long long) ChpeStubRtlpMuiRegLoadRegistryInfo
1356 stdcall RtlpNotOwnerCriticalSection(ptr) ChpeAutoRtlpNotOwnerCriticalSection
1357 stdcall RtlpNtCreateKey(ptr long ptr long ptr ptr) ChpeAutoRtlpNtCreateKey
1358 stdcall RtlpNtEnumerateSubKey(ptr ptr long long) ChpeAutoRtlpNtEnumerateSubKey
1359 stdcall RtlpNtMakeTemporaryKey(ptr) ChpeAutoRtlpNtMakeTemporaryKey
1360 stdcall RtlpNtOpenKey(ptr long ptr long) ChpeAutoRtlpNtOpenKey
1361 stdcall RtlpNtQueryValueKey(ptr ptr ptr ptr long) ChpeAutoRtlpNtQueryValueKey
1362 stdcall RtlpNtSetValueKey(ptr long ptr long) ChpeAutoRtlpNtSetValueKey
1363 stdcall -version=0x600+ RtlpQueryDefaultUILanguage(ptr long) ChpeStubRtlpQueryDefaultUILanguage
1364 stdcall -version=0x600+ RtlpQueryProcessDebugInformationFromWow64(long ptr) ChpeStubRtlpQueryProcessDebugInformationFromWow64
1365 stdcall -version=0x600+ RtlpRefreshCachedUILanguage(wstr long) ChpeStubRtlpRefreshCachedUILanguage
1366 stdcall -version=0x600+ RtlpSetInstallLanguage(long ptr) ChpeStubRtlpSetInstallLanguage
1367 stdcall -version=0x600+ RtlpSetPreferredUILanguages(long ptr ptr) ChpeStubRtlpSetPreferredUILanguages
1368 stdcall RtlpUnWaitCriticalSection(ptr) ChpeAutoRtlpUnWaitCriticalSection
1369 stdcall -version=0x600+ RtlpVerifyAndCommitUILanguageSettings(long) ChpeStubRtlpVerifyAndCommitUILanguageSettings
1370 stdcall RtlpWaitForCriticalSection(ptr) ChpeAutoRtlpWaitForCriticalSection
1371 stdcall RtlxAnsiStringToUnicodeSize(ptr) ChpeAutoRtlxAnsiStringToUnicodeSize
1372 stdcall RtlxOemStringToUnicodeSize(ptr) ChpeAutoRtlxOemStringToUnicodeSize
1373 stdcall RtlxUnicodeStringToAnsiSize(ptr) ChpeAutoRtlxUnicodeStringToAnsiSize
1374 stdcall RtlxUnicodeStringToOemSize(ptr) ChpeAutoRtlxUnicodeStringToOemSize
1375 stdcall -version=0x600+ ShipAssert(long long) ChpeStubShipAssert
1376 stdcall -version=0x600+ ShipAssertGetBufferInfo(ptr ptr) ChpeStubShipAssertGetBufferInfo
1377 stdcall -version=0x600+ ShipAssertMsgA(long long) ChpeStubShipAssertMsgA
1378 stdcall -version=0x600+ ShipAssertMsgW(long long) ChpeStubShipAssertMsgW
1379 stdcall -version=0x600+ TpAllocAlpcCompletion(ptr ptr ptr ptr ptr) ChpeAutoTpAllocAlpcCompletion
1380 stdcall -version=0x600+ TpAllocAlpcCompletionEx(ptr ptr ptr ptr ptr) ChpeAutoTpAllocAlpcCompletionEx
1381 stdcall -version=0x600+ TpAllocCleanupGroup(ptr) ChpeAutoTpAllocCleanupGroup
1382 stdcall -version=0x600+ TpAllocIoCompletion(ptr ptr ptr ptr ptr) ChpeAutoTpAllocIoCompletion
1383 stdcall -version=0x600+ TpAllocPool(ptr ptr) ChpeAutoTpAllocPool
1384 stdcall -version=0x600+ TpAllocTimer(ptr ptr ptr ptr) ChpeAutoTpAllocTimer
1385 stdcall -version=0x600+ TpAllocWait(ptr ptr ptr ptr) ChpeAutoTpAllocWait
1386 stdcall -version=0x600+ TpAllocWork(ptr ptr ptr ptr) ChpeAutoTpAllocWork
1387 stdcall -version=0x600+ TpAlpcRegisterCompletionList(ptr) ChpeAutoTpAlpcRegisterCompletionList
1388 stdcall -version=0x600+ TpAlpcUnregisterCompletionList(ptr) ChpeAutoTpAlpcUnregisterCompletionList
1390 stdcall -version=0x600+ TpCallbackMayRunLong(ptr) ChpeAutoTpCallbackMayRunLong
1393 stdcall -version=0x600+ TpCallbackSendAlpcMessageOnCompletion(ptr ptr long ptr) ChpeAutoTpCallbackSendAlpcMessageOnCompletion
1394 stdcall -version=0x600+ TpCallbackSendPendingAlpcMessage(ptr) ChpeAutoTpCallbackSendPendingAlpcMessage
1398 stdcall -version=0x600+ TpCaptureCaller(long) ChpeStubTpCaptureCaller
1399 stdcall -version=0x600+ TpCheckTerminateWorker(ptr) ChpeStubTpCheckTerminateWorker
1400 stdcall -version=0x600+ TpDbgDumpHeapUsage(long ptr long) ChpeStubTpDbgDumpHeapUsage
1401 stdcall -version=0x600+ TpDbgSetLogRoutine() ChpeStubTpDbgSetLogRoutine
1405 stdcall -version=0x601+ TpQueryPoolStackInformation(ptr ptr) ChpeAutoTpQueryPoolStackInformation
1406 stdcall -version=0x600+ TpReleaseAlpcCompletion(ptr) ChpeAutoTpReleaseAlpcCompletion
1416 stdcall -version=0x601+ TpSetPoolStackInformation(ptr ptr) ChpeAutoTpSetPoolStackInformation
1421 stdcall -version=0x600+ TpSimpleTryPost(ptr ptr ptr) ChpeAutoTpSimpleTryPost
1423 stdcall -version=0x600+ TpWaitForAlpcCompletion(ptr) ChpeAutoTpWaitForAlpcCompletion
1429 stdcall -version=0x600+ WerCheckEventEscalation(long ptr) ChpeStubWerCheckEventEscalation
1430 stdcall -version=0x600+ WerReportSQMEvent(long long long) ChpeStubWerReportSQMEvent
1431 stdcall -version=0x600+ WerReportWatsonEvent(long long long long) ChpeStubWerReportWatsonEvent
1432 stdcall -version=0x600+ WinSqmAddToStream(ptr long long long) ChpeStubWinSqmAddToStream
1433 stdcall -version=0x600+ WinSqmAddToStreamEx(ptr long long ptr long) ChpeStubWinSqmAddToStreamEx
1434 stdcall -version=0x600+ WinSqmEndSession(ptr) ChpeStubWinSqmEndSession
1435 stdcall -version=0x600+ WinSqmEventEnabled(long ptr) ChpeAutoWinSqmEventEnabled
1436 stdcall -version=0x600+ WinSqmEventWrite(long long long) ChpeStubWinSqmEventWrite
1437 stdcall -version=0x600+ WinSqmIsOptedIn() ChpeAutoWinSqmIsOptedIn
1438 stdcall -version=0x600+ WinSqmSetDWORD(ptr long long) ChpeStubWinSqmSetDWORD
1439 stdcall -version=0x600+ WinSqmSetString(ptr long ptr) ChpeStubWinSqmSetString
1440 stdcall -version=0x600+ WinSqmStartSession(ptr) ChpeStubWinSqmStartSession
1441 stdcall ZwAcceptConnectPort(ptr long ptr long long ptr) ChpeAutoZwAcceptConnectPort
1442 stdcall ZwAccessCheck(ptr long long ptr ptr ptr ptr ptr) ChpeAutoZwAccessCheck
1443 stdcall ZwAccessCheckAndAuditAlarm(ptr long ptr ptr ptr long ptr long ptr ptr ptr) ChpeAutoZwAccessCheckAndAuditAlarm
1444 stdcall ZwAccessCheckByType(ptr ptr ptr long ptr long ptr ptr long ptr ptr) ChpeAutoZwAccessCheckByType
1445 stdcall ZwAccessCheckByTypeAndAuditAlarm(ptr ptr ptr ptr ptr ptr long long long ptr long ptr long ptr ptr ptr) ChpeAutoZwAccessCheckByTypeAndAuditAlarm
1446 stdcall ZwAccessCheckByTypeResultList(ptr ptr ptr long ptr long ptr ptr long ptr ptr) ChpeAutoZwAccessCheckByTypeResultList
1447 stdcall ZwAccessCheckByTypeResultListAndAuditAlarm(ptr ptr ptr ptr ptr ptr long long long ptr long ptr long ptr ptr ptr) ChpeAutoZwAccessCheckByTypeResultListAndAuditAlarm
1448 stdcall ZwAccessCheckByTypeResultListAndAuditAlarmByHandle(ptr ptr ptr ptr ptr ptr ptr long long long ptr long ptr long ptr ptr ptr) ChpeAutoZwAccessCheckByTypeResultListAndAuditAlarmByHandle
1449 stub -version=0x600+ ZwAcquireCMFViewOwnership
1450 stdcall ZwAddAtom(ptr long ptr) ChpeAutoZwAddAtom
1451 stdcall ZwAddBootEntry(ptr long) ChpeAutoZwAddBootEntry
1452 stdcall ZwAddDriverEntry(ptr long) ChpeAutoZwAddDriverEntry
1453 stdcall ZwAdjustGroupsToken(long long long long long long) ChpeAutoZwAdjustGroupsToken
1454 stdcall ZwAdjustPrivilegesToken(long long long long long long) ChpeAutoZwAdjustPrivilegesToken
1455 stdcall ZwAlertResumeThread(long ptr) ChpeAutoZwAlertResumeThread
1456 stdcall ZwAlertThread(long) ChpeAutoZwAlertThread
1457 stdcall ZwAlertThreadByThreadId(long) ChpeAutoZwAlertThreadByThreadId
1458 stdcall ZwAllocateLocallyUniqueId(ptr) ChpeAutoZwAllocateLocallyUniqueId
1459 stdcall ZwAllocateUserPhysicalPages(ptr ptr ptr) ChpeAutoZwAllocateUserPhysicalPages
1460 stdcall ZwAllocateUuids(ptr ptr ptr ptr) ChpeAutoZwAllocateUuids
1461 stdcall ZwAllocateVirtualMemory(long ptr ptr ptr long long) ChpeAutoZwAllocateVirtualMemory
1462 stdcall ZwAllocateVirtualMemoryEx(long ptr ptr long long ptr long) ChpeAutoZwAllocateVirtualMemoryEx
1463 stdcall -version=0x600+ ZwAlpcAcceptConnectPort(ptr ptr long ptr ptr ptr ptr ptr long) ChpeAutoZwAlpcAcceptConnectPort
1464 stdcall -version=0x600+ ZwAlpcCancelMessage(ptr long ptr) ChpeAutoZwAlpcCancelMessage
1465 stdcall -version=0x600+ ZwAlpcConnectPort(ptr ptr ptr ptr long ptr ptr ptr ptr ptr ptr) ChpeAutoZwAlpcConnectPort
1466 stdcall -version=0x602+ ZwAlpcConnectPortEx(ptr ptr ptr ptr long ptr ptr ptr ptr ptr ptr) ChpeAutoZwAlpcConnectPortEx
1467 stdcall -version=0x600+ ZwAlpcCreatePort(ptr ptr ptr) ChpeAutoZwAlpcCreatePort
1468 stdcall -version=0x600+ ZwAlpcCreatePortSection(ptr long ptr long ptr ptr) ChpeAutoZwAlpcCreatePortSection
1469 stdcall -version=0x600+ ZwAlpcCreateResourceReserve(ptr long long ptr) ChpeAutoZwAlpcCreateResourceReserve
1470 stdcall -version=0x600+ ZwAlpcCreateSectionView(ptr long ptr) ChpeAutoZwAlpcCreateSectionView
1471 stdcall -version=0x600+ ZwAlpcCreateSecurityContext(ptr long ptr) ChpeAutoZwAlpcCreateSecurityContext
1472 stdcall -version=0x600+ ZwAlpcDeletePortSection(ptr long ptr) ChpeAutoZwAlpcDeletePortSection
1473 stdcall -version=0x600+ ZwAlpcDeleteResourceReserve(ptr long long) ChpeAutoZwAlpcDeleteResourceReserve
1474 stdcall -version=0x600+ ZwAlpcDeleteSectionView(ptr long ptr) ChpeAutoZwAlpcDeleteSectionView
1475 stdcall -version=0x600+ ZwAlpcDeleteSecurityContext(ptr long ptr) ChpeAutoZwAlpcDeleteSecurityContext
1476 stdcall -version=0x600+ ZwAlpcDisconnectPort(ptr long) ChpeAutoZwAlpcDisconnectPort
1477 stdcall -version=0x600+ ZwAlpcImpersonateClientOfPort(ptr ptr ptr) ChpeAutoZwAlpcImpersonateClientOfPort
1478 stdcall -version=0xA00+ ZwAlpcImpersonateClientContainerOfPort(ptr ptr long) ChpeAutoZwAlpcImpersonateClientContainerOfPort
1479 stdcall -version=0x600+ ZwAlpcOpenSenderProcess(ptr ptr ptr long long ptr) ChpeAutoZwAlpcOpenSenderProcess
1480 stdcall -version=0x600+ ZwAlpcOpenSenderThread(ptr ptr ptr long long ptr) ChpeAutoZwAlpcOpenSenderThread
1481 stdcall -version=0x600+ ZwAlpcQueryInformation(ptr long ptr long ptr) ChpeAutoZwAlpcQueryInformation
1482 stdcall -version=0x600+ ZwAlpcQueryInformationMessage(ptr ptr long ptr long ptr) ChpeAutoZwAlpcQueryInformationMessage
1483 stdcall -version=0x600+ ZwAlpcRevokeSecurityContext(ptr long ptr) ChpeAutoZwAlpcRevokeSecurityContext
1484 stdcall -version=0x600+ ZwAlpcSendWaitReceivePort(ptr long ptr ptr ptr ptr ptr ptr) ChpeAutoZwAlpcSendWaitReceivePort
1485 stdcall -version=0x600+ ZwAlpcSetInformation(ptr long ptr long) ChpeAutoZwAlpcSetInformation
1486 stdcall ZwApphelpCacheControl(long ptr) ChpeAutoZwApphelpCacheControl
1487 stdcall ZwAreMappedFilesTheSame(ptr ptr) ChpeAutoZwAreMappedFilesTheSame
1488 stdcall ZwAssignProcessToJobObject(long long) ChpeAutoZwAssignProcessToJobObject
1489 stdcall ZwCallbackReturn(ptr long long) ChpeAutoZwCallbackReturn
1490 stdcall ZwCancelDeviceWakeupRequest(ptr) ChpeAutoZwCancelDeviceWakeupRequest
1491 stdcall ZwCancelIoFile(long ptr) ChpeAutoZwCancelIoFile
1492 stdcall -version=0x600+ ZwCancelIoFileEx(ptr ptr ptr) ChpeAutoZwCancelIoFileEx
1493 stdcall -version=0x600+ ZwCancelSynchronousIoFile(ptr ptr ptr) ChpeAutoZwCancelSynchronousIoFile
1494 stdcall ZwCancelTimer(long ptr) ChpeAutoZwCancelTimer
1495 stdcall ZwClearEvent(long) ChpeAutoZwClearEvent
1496 stdcall ZwClose(long) ChpeAutoZwClose
1497 stdcall ZwCloseObjectAuditAlarm(ptr ptr long) ChpeAutoZwCloseObjectAuditAlarm
1498 stdcall -version=0x600+ ZwCommitComplete(ptr ptr) ChpeAutoZwCommitComplete
1499 stdcall -version=0x600+ ZwCommitEnlistment(ptr ptr) ChpeAutoZwCommitEnlistment
1500 stdcall -version=0x600+ ZwCommitTransaction(ptr long) ChpeAutoZwCommitTransaction
1501 stdcall ZwCompactKeys(long ptr) ChpeAutoZwCompactKeys
1502 stdcall ZwCompareTokens(ptr ptr ptr) ChpeAutoZwCompareTokens
1503 stdcall ZwCompleteConnectPort(ptr) ChpeAutoZwCompleteConnectPort
1504 stdcall ZwCompressKey(ptr) ChpeAutoZwCompressKey
1505 stdcall ZwConnectPort(ptr ptr ptr ptr ptr ptr ptr ptr) ChpeAutoZwConnectPort
1506 stdcall ZwContinue(ptr long) ChpeAutoZwContinue
1507 stdcall ZwCreateDebugObject(ptr long ptr long) ChpeAutoZwCreateDebugObject
1508 stdcall ZwCreateDirectoryObject(long long long) ChpeAutoZwCreateDirectoryObject
1509 stdcall -version=0x600+ ZwCreateEnlistment(ptr long ptr ptr ptr long long ptr) ChpeAutoZwCreateEnlistment
1510 stdcall ZwCreateEvent(long long long long long) ChpeAutoZwCreateEvent
1511 stdcall ZwCreateEventPair(ptr long ptr) ChpeAutoZwCreateEventPair
1512 stdcall ZwCreateFile(ptr long ptr ptr long long long ptr long long ptr) ChpeAutoZwCreateFile
1513 stdcall ZwCreateIoCompletion(ptr long ptr long) ChpeAutoZwCreateIoCompletion
1514 stdcall -version=0x602+ ZwCreateWaitCompletionPacket(ptr long ptr) ChpeAutoZwCreateWaitCompletionPacket
1515 stdcall -version=0x602+ ZwAssociateWaitCompletionPacket(ptr ptr ptr ptr ptr long ptr ptr) ChpeAutoZwAssociateWaitCompletionPacket
1516 stdcall -version=0x602+ ZwCancelWaitCompletionPacket(ptr long) ChpeAutoZwCancelWaitCompletionPacket
1517 stdcall ZwCreateJobObject(ptr long ptr) ChpeAutoZwCreateJobObject
1518 stdcall ZwCreateJobSet(long ptr long) ChpeAutoZwCreateJobSet
1519 stdcall ZwCreateKey(ptr long ptr long ptr long long) ChpeAutoZwCreateKey
1520 stdcall -version=0x600+ ZwCreateKeyTransacted(ptr long ptr long ptr long ptr ptr) ChpeAutoZwCreateKeyTransacted
1521 stdcall ZwCreateKeyedEvent(ptr long ptr long) ChpeAutoZwCreateKeyedEvent
1522 stdcall ZwCreateMailslotFile(long long long long long long long long) ChpeAutoZwCreateMailslotFile
1523 stdcall ZwCreateMutant(ptr long ptr long) ChpeAutoZwCreateMutant
1524 stdcall ZwCreateNamedPipeFile(ptr long ptr ptr long long long long long long long long long ptr) ChpeAutoZwCreateNamedPipeFile
1525 stdcall ZwCreatePagingFile(ptr ptr ptr long) ChpeAutoZwCreatePagingFile
1526 stdcall ZwCreatePort(ptr ptr long long long) ChpeAutoZwCreatePort
1527 stdcall ZwCreateProcess(ptr long ptr ptr long ptr ptr ptr) ChpeAutoZwCreateProcess
1528 stdcall ZwCreateProcessEx(ptr long ptr ptr long ptr ptr ptr long) ChpeAutoZwCreateProcessEx
1529 stdcall ZwCreateProfile(ptr ptr ptr long long ptr long long long) ChpeAutoZwCreateProfile
1530 stdcall -version=0x600+ ZwCreateResourceManager(ptr long ptr ptr ptr long ptr) ChpeAutoZwCreateResourceManager
1531 stdcall ZwCreateSection(ptr long ptr ptr long long long) ChpeAutoZwCreateSection
1532 stdcall ZwCreateSemaphore(ptr long ptr long long) ChpeAutoZwCreateSemaphore
1533 stdcall ZwCreateSymbolicLinkObject(ptr long ptr ptr) ChpeAutoZwCreateSymbolicLinkObject
1534 stdcall ZwCreateThread(ptr long ptr ptr ptr ptr ptr long) ChpeAutoZwCreateThread
1535 stdcall -version=0x600+ ZwCreateThreadEx(ptr long ptr ptr ptr ptr long long long long ptr) ChpeAutoZwCreateThreadEx
1536 stdcall ZwCreateTimer(ptr long ptr long) ChpeAutoZwCreateTimer
1537 stdcall ZwCreateToken(ptr long ptr long ptr ptr ptr ptr ptr ptr ptr ptr ptr) ChpeAutoZwCreateToken
1538 stdcall -version=0x600+ ZwCreateTransaction(ptr long ptr ptr ptr long long long ptr ptr) ChpeAutoZwCreateTransaction
1539 stdcall -version=0x600+ ZwCreateTransactionManager(ptr long ptr ptr long long) ChpeAutoZwCreateTransactionManager
1540 stdcall -version=0x600+ ZwCreateUserProcess(ptr ptr long long ptr ptr long long ptr ptr ptr) ChpeAutoZwCreateUserProcess
1541 stdcall ZwCreateWaitablePort(ptr ptr long long long) ChpeAutoZwCreateWaitablePort
1542 stdcall -version=0x602+ ZwCreateWnfStateName(ptr long long long ptr long ptr) ChpeAutoZwCreateWnfStateName
1543 stdcall -version=0x600+ ZwCreateWorkerFactory(ptr long ptr ptr ptr ptr ptr long long long) ChpeStubZwCreateWorkerFactory
1544 stdcall ZwDebugActiveProcess(ptr ptr) ChpeAutoZwDebugActiveProcess
1545 stdcall ZwDebugContinue(ptr ptr long) ChpeAutoZwDebugContinue
1546 stdcall ZwDelayExecution(long ptr) ChpeAutoZwDelayExecution
1547 stdcall ZwDeleteAtom(long) ChpeAutoZwDeleteAtom
1548 stdcall ZwDeleteBootEntry(long) ChpeAutoZwDeleteBootEntry
1549 stdcall ZwDeleteDriverEntry(long) ChpeAutoZwDeleteDriverEntry
1550 stdcall ZwDeleteFile(ptr) ChpeAutoZwDeleteFile
1551 stdcall ZwDeleteKey(long) ChpeAutoZwDeleteKey
1552 stdcall ZwDeleteObjectAuditAlarm(ptr ptr long) ChpeAutoZwDeleteObjectAuditAlarm
1553 stdcall -version=0x600+ ZwDeletePrivateNamespace(ptr) ChpeAutoZwDeletePrivateNamespace
1554 stdcall ZwDeleteValueKey(long ptr) ChpeAutoZwDeleteValueKey
1555 stdcall -version=0x602+ ZwDeleteWnfStateData(ptr ptr) ChpeAutoZwDeleteWnfStateData
1556 stdcall -version=0x602+ ZwDeleteWnfStateName(ptr) ChpeAutoZwDeleteWnfStateName
1557 stdcall ZwDeviceIoControlFile(long long long long long long long long long long) ChpeAutoZwDeviceIoControlFile
1558 stdcall ZwDisplayString(ptr) ChpeAutoZwDisplayString
1559 stdcall ZwDuplicateObject(long long long ptr long long long) ChpeAutoZwDuplicateObject
1560 stdcall ZwDuplicateToken(long long long long long long) ChpeAutoZwDuplicateToken
1561 stdcall ZwEnumerateBootEntries(ptr ptr) ChpeAutoZwEnumerateBootEntries
1562 stdcall ZwEnumerateDriverEntries(ptr ptr) ChpeAutoZwEnumerateDriverEntries
1563 stdcall ZwEnumerateKey(long long long ptr long ptr) ChpeAutoZwEnumerateKey
1564 stdcall ZwEnumerateSystemEnvironmentValuesEx(long ptr long) ChpeAutoZwEnumerateSystemEnvironmentValuesEx
1565 stdcall -version=0x600+ ZwEnumerateTransactionObject(ptr long ptr long ptr) ChpeAutoZwEnumerateTransactionObject
1566 stdcall ZwEnumerateValueKey(long long long ptr long ptr) ChpeAutoZwEnumerateValueKey
1567 stdcall ZwExtendSection(ptr ptr) ChpeAutoZwExtendSection
1568 stdcall ZwFilterToken(ptr long ptr ptr ptr ptr) ChpeAutoZwFilterToken
1569 stdcall -version=0x602+ ZwCreateLowBoxToken(ptr ptr long ptr ptr long ptr long ptr) ChpeAutoZwCreateLowBoxToken
1570 stdcall -version=0x600+ ZwCreatePrivateNamespace(ptr long ptr ptr) ChpeAutoZwCreatePrivateNamespace
1571 stdcall ZwFindAtom(ptr long ptr) ChpeAutoZwFindAtom
1572 stdcall ZwFlushBuffersFile(long ptr) ChpeAutoZwFlushBuffersFile
1573 stdcall ZwFlushBuffersFileEx(long long ptr long ptr) ChpeAutoZwFlushBuffersFileEx
1574 stdcall -version=0x600+ ZwFlushInstallUILanguage(long long) ChpeStubZwFlushInstallUILanguage
1575 stdcall ZwFlushInstructionCache(long ptr long) ChpeAutoZwFlushInstructionCache
1576 stdcall ZwFlushKey(long) ChpeAutoZwFlushKey
1577 stdcall -version=0x600+ ZwFlushProcessWriteBuffers() ChpeAutoZwFlushProcessWriteBuffers
1578 stdcall ZwFlushVirtualMemory(ptr ptr ptr ptr) ChpeAutoZwFlushVirtualMemory
1579 stdcall ZwFlushWriteBuffer() ChpeAutoZwFlushWriteBuffer
1580 stdcall ZwFreeUserPhysicalPages(ptr ptr ptr) ChpeAutoZwFreeUserPhysicalPages
1581 stdcall ZwFreeVirtualMemory(long ptr ptr long) ChpeAutoZwFreeVirtualMemory
1582 stdcall -version=0x600+ ZwFreezeRegistry(long) ChpeStubZwFreezeRegistry
1583 stdcall -version=0x600+ ZwFreezeTransactions(ptr ptr) ChpeStubZwFreezeTransactions
1584 stdcall ZwFsControlFile(long long long long long long long long long long) ChpeAutoZwFsControlFile
1585 stdcall ZwGetContextThread(long ptr) ChpeAutoZwGetContextThread
1586 stdcall ZwGetCurrentProcessorNumber() ChpeAutoZwGetCurrentProcessorNumber
1587 stdcall -version=0xA00+ ZwGetCurrentProcessorNumberEx(ptr) ChpeAutoZwGetCurrentProcessorNumberEx
1588 stdcall ZwGetDevicePowerState(ptr ptr) ChpeAutoZwGetDevicePowerState
1589 stdcall -version=0x600+ ZwGetMUIRegistryInfo(long ptr ptr) ChpeStubZwGetMUIRegistryInfo
1590 stdcall -version=0x600+ ZwGetNextProcess(ptr long long long ptr) ChpeStubZwGetNextProcess
1591 stdcall -version=0x600+ ZwGetNextThread(ptr ptr long long long ptr) ChpeAutoZwGetNextThread
1592 stdcall -version=0x600+ ZwGetNlsSectionPtr(long long ptr ptr ptr) ChpeAutoZwGetNlsSectionPtr
1593 stdcall -version=0x600+ ZwGetNotificationResourceManager(ptr ptr long ptr ptr long ptr) ChpeAutoZwGetNotificationResourceManager
1594 stdcall ZwGetPlugPlayEvent(long long ptr long) ChpeAutoZwGetPlugPlayEvent
1595 stdcall ZwGetWriteWatch(long long ptr long ptr ptr ptr) ChpeAutoZwGetWriteWatch
1596 stdcall ZwImpersonateAnonymousToken(ptr) ChpeAutoZwImpersonateAnonymousToken
1597 stdcall ZwImpersonateClientOfPort(ptr ptr) ChpeAutoZwImpersonateClientOfPort
1598 stdcall ZwImpersonateThread(ptr ptr ptr) ChpeAutoZwImpersonateThread
1599 stdcall -version=0x600+ ZwInitializeNlsFiles(ptr ptr ptr ptr) ChpeStubZwInitializeNlsFiles
1600 stdcall ZwInitializeRegistry(long) ChpeAutoZwInitializeRegistry
1601 stdcall ZwInitiatePowerAction(long long long long) ChpeAutoZwInitiatePowerAction
1602 stdcall ZwIsProcessInJob(long long) ChpeAutoZwIsProcessInJob
1603 stdcall ZwIsSystemResumeAutomatic() ChpeAutoZwIsSystemResumeAutomatic
1604 stdcall -version=0x600+ ZwIsUILanguageComitted() ChpeStubZwIsUILanguageComitted
1605 stdcall ZwListenPort(ptr ptr) ChpeAutoZwListenPort
1606 stdcall ZwLoadDriver(ptr) ChpeAutoZwLoadDriver
1607 stdcall ZwLoadKey2(ptr ptr long) ChpeAutoZwLoadKey2
1608 stdcall ZwLoadKey(ptr ptr) ChpeAutoZwLoadKey
1609 stdcall ZwLoadKeyEx(ptr ptr long ptr ptr long ptr ptr) ChpeAutoZwLoadKeyEx
1610 stdcall ZwLockFile(long long ptr ptr ptr ptr ptr ptr long long) ChpeAutoZwLockFile
1611 stdcall ZwLockProductActivationKeys(ptr ptr) ChpeAutoZwLockProductActivationKeys
1612 stdcall ZwLockRegistryKey(ptr) ChpeAutoZwLockRegistryKey
1613 stdcall ZwLockVirtualMemory(long ptr ptr long) ChpeAutoZwLockVirtualMemory
1614 stdcall ZwMakePermanentObject(ptr) ChpeAutoZwMakePermanentObject
1615 stdcall ZwMakeTemporaryObject(long) ChpeAutoZwMakeTemporaryObject
1616 stdcall -version=0x600+ ZwMapCMFModule(long long ptr ptr ptr) ChpeStubZwMapCMFModule
1617 stdcall ZwMapUserPhysicalPages(ptr ptr ptr) ChpeAutoZwMapUserPhysicalPages
1618 stdcall ZwMapUserPhysicalPagesScatter(ptr ptr ptr) ChpeAutoZwMapUserPhysicalPagesScatter
1619 stdcall ZwMapViewOfSection(long long ptr long long ptr ptr long long long) ChpeAutoZwMapViewOfSection
1620 stdcall ZwModifyBootEntry(ptr) ChpeAutoZwModifyBootEntry
1621 stdcall ZwModifyDriverEntry(ptr) ChpeAutoZwModifyDriverEntry
1622 stdcall ZwNotifyChangeDirectoryFile(long long ptr ptr ptr ptr long long long) ChpeAutoZwNotifyChangeDirectoryFile
1623 stdcall ZwNotifyChangeDirectoryFileEx(long long ptr ptr ptr ptr long long long long) ChpeAutoZwNotifyChangeDirectoryFileEx
1624 stdcall ZwNotifyChangeKey(long long ptr ptr ptr long long ptr long long) ChpeAutoZwNotifyChangeKey
1625 stdcall ZwNotifyChangeMultipleKeys(ptr long ptr ptr ptr ptr ptr long long ptr long long) ChpeAutoZwNotifyChangeMultipleKeys
1626 stdcall ZwOpenDirectoryObject(long long long) ChpeAutoZwOpenDirectoryObject
1627 stdcall -version=0x600+ ZwOpenEnlistment(ptr long ptr ptr ptr) ChpeAutoZwOpenEnlistment
1628 stdcall ZwOpenEvent(long long long) ChpeAutoZwOpenEvent
1629 stdcall ZwOpenEventPair(ptr long ptr) ChpeAutoZwOpenEventPair
1630 stdcall ZwOpenFile(ptr long ptr ptr long long) ChpeAutoZwOpenFile
1631 stdcall ZwOpenIoCompletion(ptr long ptr) ChpeAutoZwOpenIoCompletion
1632 stdcall ZwOpenJobObject(ptr long ptr) ChpeAutoZwOpenJobObject
1633 stdcall ZwOpenKey(ptr long ptr) ChpeAutoZwOpenKey
1634 stdcall ZwOpenKeyEx(ptr long ptr long) ChpeAutoZwOpenKeyEx
1635 stdcall -version=0x600+ ZwOpenKeyTransacted(ptr long ptr ptr) ChpeAutoZwOpenKeyTransacted
1636 stdcall ZwOpenKeyedEvent(ptr long ptr) ChpeAutoZwOpenKeyedEvent
1637 stdcall ZwOpenMutant(ptr long ptr) ChpeAutoZwOpenMutant
1638 stdcall ZwOpenObjectAuditAlarm(ptr ptr ptr ptr ptr ptr long long ptr long long ptr) ChpeAutoZwOpenObjectAuditAlarm
1639 stdcall -version=0x600+ ZwOpenPrivateNamespace(ptr long ptr ptr) ChpeAutoZwOpenPrivateNamespace
1640 stdcall ZwOpenProcess(ptr long ptr ptr) ChpeAutoZwOpenProcess
1641 stdcall ZwOpenProcessToken(long long ptr) ChpeAutoZwOpenProcessToken
1642 stdcall ZwOpenProcessTokenEx(long long long ptr) ChpeAutoZwOpenProcessTokenEx
1643 stdcall -version=0x600+ ZwOpenResourceManager(ptr long ptr ptr ptr) ChpeAutoZwOpenResourceManager
1644 stdcall ZwOpenSection(ptr long ptr) ChpeAutoZwOpenSection
1645 stdcall ZwOpenSemaphore(long long ptr) ChpeAutoZwOpenSemaphore
1646 stdcall -version=0x600+ ZwOpenSession(ptr long ptr) ChpeStubZwOpenSession
1647 stdcall ZwOpenSymbolicLinkObject(ptr long ptr) ChpeAutoZwOpenSymbolicLinkObject
1648 stdcall ZwOpenThread(ptr long ptr ptr) ChpeAutoZwOpenThread
1649 stdcall ZwOpenThreadToken(long long long ptr) ChpeAutoZwOpenThreadToken
1650 stdcall ZwOpenThreadTokenEx(long long long long ptr) ChpeAutoZwOpenThreadTokenEx
1651 stdcall ZwOpenTimer(ptr long ptr) ChpeAutoZwOpenTimer
1652 stdcall -version=0x600+ ZwOpenTransaction(ptr long ptr ptr ptr) ChpeAutoZwOpenTransaction
1653 stdcall -version=0x600+ ZwOpenTransactionManager(ptr long ptr ptr ptr long) ChpeAutoZwOpenTransactionManager
1654 stdcall ZwPlugPlayControl(ptr ptr long) ChpeAutoZwPlugPlayControl
1655 stdcall ZwPowerInformation(long ptr long ptr long) ChpeAutoZwPowerInformation
1656 stdcall -version=0x600+ ZwPrePrepareComplete(ptr ptr) ChpeAutoZwPrePrepareComplete
1657 stdcall -version=0x600+ ZwPrePrepareEnlistment(ptr ptr) ChpeAutoZwPrePrepareEnlistment
1658 stdcall -version=0x600+ ZwPrepareComplete(ptr ptr) ChpeAutoZwPrepareComplete
1659 stdcall -version=0x600+ ZwPrepareEnlistment(ptr ptr) ChpeAutoZwPrepareEnlistment
1660 stdcall ZwPrivilegeCheck(ptr ptr ptr) ChpeAutoZwPrivilegeCheck
1661 stdcall ZwPrivilegeObjectAuditAlarm(ptr ptr ptr long ptr long) ChpeAutoZwPrivilegeObjectAuditAlarm
1662 stdcall ZwPrivilegedServiceAuditAlarm(ptr ptr ptr ptr long) ChpeAutoZwPrivilegedServiceAuditAlarm
1663 stdcall -version=0x600+ ZwPropagationComplete(ptr long long ptr) ChpeAutoZwPropagationComplete
1664 stdcall -version=0x600+ ZwPropagationFailed(ptr long long) ChpeAutoZwPropagationFailed
1665 stdcall ZwProtectVirtualMemory(long ptr ptr long ptr) ChpeAutoZwProtectVirtualMemory
1666 stdcall ZwPulseEvent(long ptr) ChpeAutoZwPulseEvent
1667 stdcall ZwQueryAttributesFile(ptr ptr) ChpeAutoZwQueryAttributesFile
1668 stdcall ZwQueryBootEntryOrder(ptr ptr) ChpeAutoZwQueryBootEntryOrder
1669 stdcall ZwQueryBootOptions(ptr ptr) ChpeAutoZwQueryBootOptions
1670 stdcall ZwQueryDebugFilterState(long long) ChpeAutoZwQueryDebugFilterState
1671 stdcall ZwQueryDefaultLocale(long ptr) ChpeAutoZwQueryDefaultLocale
1672 stdcall ZwQueryDefaultUILanguage(ptr) ChpeAutoZwQueryDefaultUILanguage
1673 stdcall ZwQueryDirectoryFile(long long ptr ptr ptr ptr long long long ptr long) ChpeAutoZwQueryDirectoryFile
1674 stdcall ZwQueryDirectoryFileEx(long long ptr ptr ptr ptr long long long ptr) ChpeAutoZwQueryDirectoryFileEx
1675 stdcall ZwQueryDirectoryObject(long ptr long long long ptr ptr) ChpeAutoZwQueryDirectoryObject
1676 stdcall ZwQueryDriverEntryOrder(ptr ptr) ChpeAutoZwQueryDriverEntryOrder
1677 stdcall ZwQueryEaFile(long ptr ptr long long ptr long ptr long) ChpeAutoZwQueryEaFile
1678 stdcall ZwQueryEvent(long long ptr long ptr) ChpeAutoZwQueryEvent
1679 stdcall ZwQueryFullAttributesFile(ptr ptr) ChpeAutoZwQueryFullAttributesFile
1680 stdcall ZwQueryInformationAtom(long long ptr long ptr) ChpeAutoZwQueryInformationAtom
1681 stdcall ZwQueryInformationByName(ptr ptr ptr long long) ChpeAutoZwQueryInformationByName
1682 stdcall -version=0x600+ ZwQueryInformationEnlistment(ptr long ptr long ptr) ChpeAutoZwQueryInformationEnlistment
1683 stdcall ZwQueryInformationFile(long ptr ptr long long) ChpeAutoZwQueryInformationFile
1684 stdcall ZwQueryInformationJobObject(long long ptr long ptr) ChpeAutoZwQueryInformationJobObject
1685 stdcall ZwQueryInformationPort(ptr long ptr long ptr) ChpeAutoZwQueryInformationPort
1686 stdcall ZwQueryInformationProcess(long long ptr long ptr) ChpeAutoZwQueryInformationProcess
1687 stdcall -version=0x600+ ZwQueryInformationResourceManager(ptr long ptr long ptr) ChpeAutoZwQueryInformationResourceManager
1688 stdcall ZwQueryInformationThread(long long ptr long ptr) ChpeAutoZwQueryInformationThread
1689 stdcall ZwQueryInformationToken(long long ptr long ptr) ChpeAutoZwQueryInformationToken
1690 stdcall -version=0x600+ ZwQueryInformationTransaction(ptr long ptr long ptr) ChpeAutoZwQueryInformationTransaction
1691 stdcall -version=0x600+ ZwQueryInformationTransactionManager(ptr long ptr long ptr) ChpeAutoZwQueryInformationTransactionManager
1692 stdcall -version=0x600+ ZwQueryInformationWorkerFactory(ptr long ptr long ptr) ChpeStubZwQueryInformationWorkerFactory
1693 stdcall ZwQueryInstallUILanguage(ptr) ChpeAutoZwQueryInstallUILanguage
1694 stdcall ZwQueryIntervalProfile(long ptr) ChpeAutoZwQueryIntervalProfile
1695 stdcall ZwQueryIoCompletion(long long ptr long ptr) ChpeAutoZwQueryIoCompletion
1696 stdcall ZwQueryKey(long long ptr long ptr) ChpeAutoZwQueryKey
1697 stdcall -version=0x600+ ZwQueryLicenseValue(ptr ptr ptr long ptr) ChpeAutoZwQueryLicenseValue
1698 stdcall ZwQueryMultipleValueKey(long ptr long ptr long ptr) ChpeAutoZwQueryMultipleValueKey
1699 stdcall ZwQueryMutant(long long ptr long ptr) ChpeAutoZwQueryMutant
1700 stdcall ZwQueryObject(long long long long long) ChpeAutoZwQueryObject
1701 stdcall ZwQueryOpenSubKeys(ptr ptr) ChpeAutoZwQueryOpenSubKeys
1702 stdcall ZwQueryOpenSubKeysEx(ptr long ptr ptr) ChpeAutoZwQueryOpenSubKeysEx
1703 stdcall ZwQueryPerformanceCounter(long long) ChpeAutoZwQueryPerformanceCounter
1704 stdcall ZwQueryPortInformationProcess() ChpeAutoZwQueryPortInformationProcess
1705 stdcall ZwQueryQuotaInformationFile(ptr ptr ptr long long ptr long ptr long) ChpeAutoZwQueryQuotaInformationFile
1706 stdcall ZwQuerySection(long long long long long) ChpeAutoZwQuerySection
1707 stdcall ZwQuerySecurityObject(long long long long long) ChpeAutoZwQuerySecurityObject
1708 stdcall ZwQuerySemaphore(long long long long long) ChpeAutoZwQuerySemaphore
1709 stdcall ZwQuerySymbolicLinkObject(long ptr ptr) ChpeAutoZwQuerySymbolicLinkObject
1710 stdcall ZwQuerySystemEnvironmentValue(ptr ptr long ptr) ChpeAutoZwQuerySystemEnvironmentValue
1711 stdcall ZwQuerySystemEnvironmentValueEx(ptr ptr ptr ptr ptr) ChpeAutoZwQuerySystemEnvironmentValueEx
1712 stdcall ZwQuerySystemInformation(long long long long) ChpeAutoZwQuerySystemInformation
1713 stdcall -version=0x601+ ZwQuerySystemInformationEx(long ptr long ptr long ptr) ChpeAutoZwQuerySystemInformationEx
1714 stdcall ZwQuerySystemTime(ptr) ChpeAutoZwQuerySystemTime
1715 stdcall ZwQueryTimer(ptr long ptr long ptr) ChpeAutoZwQueryTimer
1716 stdcall ZwQueryTimerResolution(long long long) ChpeAutoZwQueryTimerResolution
1717 stdcall ZwQueryValueKey(long ptr long ptr long ptr) ChpeAutoZwQueryValueKey
1718 stdcall ZwQueryVirtualMemory(long ptr long ptr long ptr) ChpeAutoZwQueryVirtualMemory
1719 stdcall ZwQueryVolumeInformationFile(long ptr ptr long long) ChpeAutoZwQueryVolumeInformationFile
1720 stdcall -version=0x602+ ZwQueryWnfStateData(ptr ptr ptr ptr ptr ptr) ChpeAutoZwQueryWnfStateData
1721 stdcall -version=0x602+ ZwQueryWnfStateNameInformation(ptr long ptr ptr long) ChpeAutoZwQueryWnfStateNameInformation
1722 stdcall ZwQueueApcThread(long ptr long long long) ChpeAutoZwQueueApcThread
1723 stdcall ZwRaiseException(ptr ptr long) ChpeAutoZwRaiseException
1724 stdcall ZwRaiseHardError(long long long ptr long ptr) ChpeAutoZwRaiseHardError
1725 stdcall ZwReadFile(long long ptr ptr ptr ptr long ptr ptr) ChpeAutoZwReadFile
1726 stdcall ZwReadFileScatter(long long ptr ptr ptr ptr long ptr ptr) ChpeAutoZwReadFileScatter
1727 stdcall -version=0x600+ ZwReadOnlyEnlistment(ptr ptr) ChpeAutoZwReadOnlyEnlistment
1728 stdcall ZwReadRequestData(ptr ptr long ptr long ptr) ChpeAutoZwReadRequestData
1729 stdcall ZwReadVirtualMemory(long ptr ptr long ptr) ChpeAutoZwReadVirtualMemory
1730 stdcall -version=0x600+ ZwRecoverEnlistment(ptr ptr) ChpeAutoZwRecoverEnlistment
1731 stdcall -version=0x600+ ZwRecoverResourceManager(ptr) ChpeAutoZwRecoverResourceManager
1732 stdcall -version=0x600+ ZwRecoverTransactionManager(ptr) ChpeAutoZwRecoverTransactionManager
1733 stdcall -version=0x600+ ZwRegisterProtocolAddressInformation(ptr ptr long ptr long) ChpeAutoZwRegisterProtocolAddressInformation
1734 stdcall ZwRegisterThreadTerminatePort(ptr) ChpeAutoZwRegisterThreadTerminatePort
1735 stdcall -version=0x600+ ZwReleaseCMFViewOwnership() ChpeStubZwReleaseCMFViewOwnership
1736 stdcall ZwReleaseKeyedEvent(ptr ptr long ptr) ChpeAutoZwReleaseKeyedEvent
1737 stdcall ZwReleaseMutant(long ptr) ChpeAutoZwReleaseMutant
1738 stdcall ZwReleaseSemaphore(long long ptr) ChpeAutoZwReleaseSemaphore
1739 stdcall -version=0x600+ ZwReleaseWorkerFactoryWorker(ptr) ChpeStubZwReleaseWorkerFactoryWorker
1740 stdcall ZwRemoveIoCompletion(ptr ptr ptr ptr ptr) ChpeAutoZwRemoveIoCompletion
1741 stdcall -version=0x600+ ZwRemoveIoCompletionEx(ptr ptr long ptr ptr long) ChpeAutoZwRemoveIoCompletionEx
1742 stdcall ZwRemoveProcessDebug(ptr ptr) ChpeAutoZwRemoveProcessDebug
1743 stdcall ZwRenameKey(ptr ptr) ChpeAutoZwRenameKey
1744 stdcall -version=0x600+ ZwRenameTransactionManager(ptr ptr) ChpeAutoZwRenameTransactionManager
1745 stdcall ZwReplaceKey(ptr long ptr) ChpeAutoZwReplaceKey
1746 stdcall -version=0x600+ ZwReplacePartitionUnit(wstr wstr long) ChpeStubZwReplacePartitionUnit
1747 stdcall ZwReplyPort(ptr ptr) ChpeAutoZwReplyPort
1748 stdcall ZwReplyWaitReceivePort(ptr ptr ptr ptr) ChpeAutoZwReplyWaitReceivePort
1749 stdcall ZwReplyWaitReceivePortEx(ptr ptr ptr ptr ptr) ChpeAutoZwReplyWaitReceivePortEx
1750 stdcall ZwReplyWaitReplyPort(ptr ptr) ChpeAutoZwReplyWaitReplyPort
1751 stdcall ZwRequestDeviceWakeup(ptr) ChpeAutoZwRequestDeviceWakeup
1752 stdcall ZwRequestPort(ptr ptr) ChpeAutoZwRequestPort
1753 stdcall ZwRequestWaitReplyPort(ptr ptr ptr) ChpeAutoZwRequestWaitReplyPort
1754 stdcall ZwRequestWakeupLatency(long) ChpeAutoZwRequestWakeupLatency
1755 stdcall ZwResetEvent(long ptr) ChpeAutoZwResetEvent
1756 stdcall ZwResetWriteWatch(long ptr long) ChpeAutoZwResetWriteWatch
1757 stdcall ZwRestoreKey(long long long) ChpeAutoZwRestoreKey
1758 stdcall ZwResumeProcess(ptr) ChpeAutoZwResumeProcess
1759 stdcall ZwResumeThread(long long) ChpeAutoZwResumeThread
1760 stdcall -version=0x600+ ZwRollbackComplete(ptr ptr) ChpeAutoZwRollbackComplete
1761 stdcall -version=0x600+ ZwRollbackEnlistment(ptr ptr) ChpeAutoZwRollbackEnlistment
1762 stdcall -version=0x600+ ZwRollbackTransaction(ptr long) ChpeAutoZwRollbackTransaction
1763 stdcall -version=0x600+ ZwRollforwardTransactionManager(ptr ptr) ChpeAutoZwRollforwardTransactionManager
1764 stdcall ZwSaveKey(long long) ChpeAutoZwSaveKey
1765 stdcall ZwSaveKeyEx(ptr ptr long) ChpeAutoZwSaveKeyEx
1766 stdcall ZwSaveMergedKeys(ptr ptr ptr) ChpeAutoZwSaveMergedKeys
1767 stdcall ZwSecureConnectPort(ptr ptr ptr ptr ptr ptr ptr ptr ptr) ChpeAutoZwSecureConnectPort
1768 stdcall ZwSetBootEntryOrder(ptr ptr) ChpeAutoZwSetBootEntryOrder
1769 stdcall ZwSetBootOptions(ptr long) ChpeAutoZwSetBootOptions
1770 stdcall ZwSetContextThread(long ptr) ChpeAutoZwSetContextThread
1771 stdcall ZwSetDebugFilterState(long long long) ChpeAutoZwSetDebugFilterState
1772 stdcall ZwSetDefaultHardErrorPort(ptr) ChpeAutoZwSetDefaultHardErrorPort
1773 stdcall ZwSetDefaultLocale(long long) ChpeAutoZwSetDefaultLocale
1774 stdcall ZwSetDefaultUILanguage(long) ChpeAutoZwSetDefaultUILanguage
1775 stdcall ZwSetDriverEntryOrder(ptr ptr) ChpeAutoZwSetDriverEntryOrder
1776 stdcall ZwSetEaFile(long ptr ptr long) ChpeAutoZwSetEaFile
1777 stdcall ZwSetEvent(long long) ChpeAutoZwSetEvent
1778 stdcall ZwSetEventBoostPriority(ptr) ChpeAutoZwSetEventBoostPriority
1779 stdcall ZwSetHighEventPair(ptr) ChpeAutoZwSetHighEventPair
1780 stdcall ZwSetHighWaitLowEventPair(ptr) ChpeAutoZwSetHighWaitLowEventPair
1781 stdcall ZwSetInformationDebugObject(ptr long ptr long ptr) ChpeAutoZwSetInformationDebugObject
1782 stdcall -version=0x600+ ZwSetInformationEnlistment(ptr long ptr long) ChpeAutoZwSetInformationEnlistment
1783 stdcall ZwSetInformationFile(long long long long long) ChpeAutoZwSetInformationFile
1784 stdcall ZwSetInformationJobObject(long long ptr long) ChpeAutoZwSetInformationJobObject
1785 stdcall ZwSetInformationKey(long long ptr long) ChpeAutoZwSetInformationKey
1786 stdcall ZwSetInformationObject(long long ptr long) ChpeAutoZwSetInformationObject
1787 stdcall ZwSetInformationProcess(long long long long) ChpeAutoZwSetInformationProcess
1788 stdcall -version=0x600+ ZwSetInformationResourceManager(ptr long ptr long) ChpeAutoZwSetInformationResourceManager
1789 stdcall ZwSetInformationThread(long long ptr long) ChpeAutoZwSetInformationThread
1790 stdcall ZwSetInformationToken(long long ptr long) ChpeAutoZwSetInformationToken
1791 stdcall -version=0x600+ ZwSetInformationTransaction(ptr long ptr long) ChpeAutoZwSetInformationTransaction
1792 stdcall -version=0x600+ ZwSetInformationTransactionManager(ptr long ptr long) ChpeAutoZwSetInformationTransactionManager
1793 stdcall ZwSetInformationVirtualMemory(ptr long ptr ptr ptr long) ChpeAutoZwSetInformationVirtualMemory
1794 stdcall -version=0x600+ ZwSetInformationWorkerFactory(ptr long ptr long) ChpeStubZwSetInformationWorkerFactory
1795 stdcall ZwSetIntervalProfile(long long) ChpeAutoZwSetIntervalProfile
1796 stdcall ZwSetIoCompletion(ptr long ptr long long) ChpeAutoZwSetIoCompletion
1797 stdcall ZwSetLdtEntries(long int64 long int64) ChpeAutoZwSetLdtEntries
1798 stdcall ZwSetLowEventPair(ptr) ChpeAutoZwSetLowEventPair
1799 stdcall ZwSetLowWaitHighEventPair(ptr) ChpeAutoZwSetLowWaitHighEventPair
1800 stdcall ZwSetQuotaInformationFile(ptr ptr ptr long) ChpeAutoZwSetQuotaInformationFile
1801 stdcall ZwSetSecurityObject(long long ptr) ChpeAutoZwSetSecurityObject
1802 stdcall ZwSetSystemEnvironmentValue(ptr ptr) ChpeAutoZwSetSystemEnvironmentValue
1803 stdcall ZwSetSystemEnvironmentValueEx(ptr ptr ptr ptr ptr) ChpeAutoZwSetSystemEnvironmentValueEx
1804 stdcall ZwSetSystemInformation(long ptr long) ChpeAutoZwSetSystemInformation
1805 stdcall ZwSetSystemPowerState(long long long) ChpeAutoZwSetSystemPowerState
1806 stdcall ZwSetSystemTime(ptr ptr) ChpeAutoZwSetSystemTime
1807 stdcall ZwSetThreadExecutionState(long ptr) ChpeAutoZwSetThreadExecutionState
1808 stdcall ZwSetTimer(long ptr ptr ptr long long ptr) ChpeAutoZwSetTimer
1809 stdcall ZwSetTimerResolution(long long ptr) ChpeAutoZwSetTimerResolution
1810 stdcall ZwSetUuidSeed(ptr) ChpeAutoZwSetUuidSeed
1811 stdcall ZwSetValueKey(long long long long long long) ChpeAutoZwSetValueKey
1812 stdcall ZwSetVolumeInformationFile(long ptr ptr long long) ChpeAutoZwSetVolumeInformationFile
1813 stdcall ZwShutdownSystem(long) ChpeAutoZwShutdownSystem
1814 stdcall -version=0x600+ ZwShutdownWorkerFactory(ptr ptr) ChpeStubZwShutdownWorkerFactory
1815 stdcall ZwSignalAndWaitForSingleObject(long long long ptr) ChpeAutoZwSignalAndWaitForSingleObject
1816 stdcall -version=0x600+ ZwSinglePhaseReject(ptr ptr) ChpeAutoZwSinglePhaseReject
1817 stdcall ZwStartProfile(ptr) ChpeAutoZwStartProfile
1818 stdcall ZwStopProfile(ptr) ChpeAutoZwStopProfile
1819 stdcall -version=0x602+ ZwSubscribeWnfStateChange(ptr long long ptr) ChpeAutoZwSubscribeWnfStateChange
1820 stdcall ZwSuspendProcess(ptr) ChpeAutoZwSuspendProcess
1821 stdcall ZwSuspendThread(long ptr) ChpeAutoZwSuspendThread
1822 stdcall ZwSystemDebugControl(long ptr long ptr long ptr) ChpeAutoZwSystemDebugControl
1823 stdcall ZwTerminateJobObject(ptr long) ChpeAutoZwTerminateJobObject
1824 stdcall ZwTerminateProcess(ptr long) ChpeAutoZwTerminateProcess
1825 stdcall ZwTerminateThread(ptr long) ChpeAutoZwTerminateThread
1826 stdcall ZwTestAlert() ChpeAutoZwTestAlert
1827 stdcall -version=0x600+ ZwThawRegistry() ChpeStubZwThawRegistry
1828 stdcall -version=0x600+ ZwThawTransactions() ChpeStubZwThawTransactions
1829 stdcall -version=0x600+ ZwTraceControl(long ptr long ptr long ptr) ChpeAutoZwTraceControl
1830 stdcall ZwTraceEvent(ptr long long ptr) ChpeAutoZwTraceEvent
1831 stdcall ZwTranslateFilePath(ptr long ptr long) ChpeAutoZwTranslateFilePath
1832 stdcall ZwUnloadDriver(ptr) ChpeAutoZwUnloadDriver
1833 stdcall ZwUnloadKey2(ptr long) ChpeAutoZwUnloadKey2
1834 stdcall ZwUnloadKey(long) ChpeAutoZwUnloadKey
1835 stdcall ZwUnloadKeyEx(ptr ptr) ChpeAutoZwUnloadKeyEx
1836 stdcall ZwUnlockFile(long ptr ptr ptr ptr) ChpeAutoZwUnlockFile
1837 stdcall ZwUnlockVirtualMemory(long ptr ptr long) ChpeAutoZwUnlockVirtualMemory
1838 stdcall ZwUnmapViewOfSection(long ptr) ChpeAutoZwUnmapViewOfSection
1839 stdcall -version=0x602+ ZwUnsubscribeWnfStateChange(ptr) ChpeAutoZwUnsubscribeWnfStateChange
1840 stdcall -version=0x602+ ZwUpdateWnfStateData(ptr ptr long ptr ptr long long) ChpeAutoZwUpdateWnfStateData
1841 stdcall ZwVdmControl(long ptr) ChpeAutoZwVdmControl
1842 stdcall ZwWaitForDebugEvent(ptr long ptr ptr) ChpeAutoZwWaitForDebugEvent
1843 stdcall ZwWaitForKeyedEvent(ptr ptr long ptr) ChpeAutoZwWaitForKeyedEvent
1844 stdcall ZwWaitForAlertByThreadId(ptr ptr) ChpeAutoZwWaitForAlertByThreadId
1845 stdcall ZwWaitForMultipleObjects32(long ptr long long ptr) ChpeAutoZwWaitForMultipleObjects32
1846 stdcall ZwWaitForMultipleObjects(long ptr long long ptr) ChpeAutoZwWaitForMultipleObjects
1847 stdcall ZwWaitForSingleObject(long long long) ChpeAutoZwWaitForSingleObject
1848 stdcall -version=0x600+ ZwWaitForWorkViaWorkerFactory(ptr ptr long ptr ptr) ChpeStubZwWaitForWorkViaWorkerFactory
1849 stdcall ZwWaitHighEventPair(ptr) ChpeAutoZwWaitHighEventPair
1850 stdcall ZwWaitLowEventPair(ptr) ChpeAutoZwWaitLowEventPair
1851 stdcall -version=0x600+ ZwWorkerFactoryWorkerReady(ptr) ChpeStubZwWorkerFactoryWorkerReady
1852 stdcall ZwWriteFile(long long ptr ptr ptr ptr long ptr ptr) ChpeAutoZwWriteFile
1853 stdcall ZwWriteFileGather(long long ptr ptr ptr ptr long ptr ptr) ChpeAutoZwWriteFileGather
1854 stdcall ZwWriteRequestData(ptr ptr long ptr long ptr) ChpeAutoZwWriteRequestData
1855 stdcall ZwWriteVirtualMemory(long ptr ptr long ptr) ChpeAutoZwWriteVirtualMemory
1856 stdcall ZwYieldExecution() ChpeAutoZwYieldExecution
1859 cdecl __isascii(long) ChpeAuto__isascii
1860 cdecl __iscsym(long) ChpeAuto__iscsym
1861 cdecl __iscsymf(long) ChpeAuto__iscsymf
1862 cdecl -version=0x600+ __misaligned_access() ChpeStub__misaligned_access
1863 cdecl __toascii(long) ChpeAuto__toascii
1864 cdecl -ret64 _atoi64(str) ChpeAuto_atoi64
1866 extern _fltused ntdll._fltused
1867 cdecl _i64toa(double ptr long) ChpeAuto_i64toa
1868 cdecl _i64tow(double ptr long) ChpeAuto_i64tow
1869 cdecl _itoa(long ptr long) ChpeAuto_itoa
1870 cdecl _itow(long ptr long) ChpeAuto_itow
1873 cdecl _ltoa(long ptr long) ChpeAuto_ltoa
1874 cdecl _ltow(long ptr long) ChpeAuto_ltow
1875 cdecl _memccpy(ptr ptr long long) ChpeAuto_memccpy
1876 cdecl _memicmp(str str long) ChpeAuto_memicmp
1883 cdecl _splitpath(str ptr ptr ptr ptr) ChpeAuto_splitpath
1884 cdecl _strcmpi(str str) ChpeAuto_strcmpi
1885 cdecl _stricmp(str str) ChpeAuto_stricmp
1886 cdecl _strlwr(str) ChpeAuto_strlwr
1887 cdecl _strnicmp(str str long) ChpeAuto_strnicmp
1888 cdecl _strupr(str) ChpeAuto_strupr
1890 cdecl _ui64toa(double ptr long) ChpeAuto_ui64toa
1891 cdecl _ui64tow(double ptr long) ChpeAuto_ui64tow
1892 cdecl _ultoa(long ptr long) ChpeAuto_ultoa
1893 cdecl _ultow(long ptr long) ChpeAuto_ultow
1894 cdecl _vscwprintf(wstr ptr) ChpeAuto_vscwprintf
1895 cdecl _vsnprintf(ptr long str ptr) ChpeAuto_vsnprintf
1896 cdecl _vsnprintf_s(ptr long long str ptr) ChpeAuto_vsnprintf_s
1897 cdecl _vsnwprintf(ptr long wstr ptr) ChpeAuto_vsnwprintf
1898 cdecl _vsnwprintf_s(ptr long long wstr ptr) ChpeAuto_vsnwprintf_s
1899 cdecl -version=0x600+ _vswprintf(ptr wstr ptr) ChpeStub_vswprintf
1900 cdecl _wcsicmp(wstr wstr) ChpeAuto_wcsicmp
1901 cdecl _wcslwr(wstr) ChpeAuto_wcslwr
1902 cdecl _wcsnicmp(wstr wstr long) ChpeAuto_wcsnicmp
1903 cdecl _wcstoui64(wstr ptr long) ChpeAuto_wcstoui64
1904 cdecl _wcsupr(wstr) ChpeAuto_wcsupr
1905 cdecl _wtoi(wstr) ChpeAuto_wtoi
1906 cdecl _wtoi64(wstr) ChpeAuto_wtoi64
1907 cdecl _wtol(wstr) ChpeAuto_wtol
1908 cdecl abs(long) ChpeAutoabs
1909 cdecl atan(double) ChpeAutoatan
1910 cdecl atoi(str) ChpeAutoatoi
1911 cdecl atol(str) ChpeAutoatol
1913 cdecl ceil(double) ChpeAutoceil
1914 cdecl cos(double) ChpeAutocos
1915 cdecl fabs(double) ChpeAutofabs
1916 cdecl floor(double) ChpeAutofloor
1917 cdecl isalnum(long) ChpeAutoisalnum
1918 cdecl isalpha(long) ChpeAutoisalpha
1919 cdecl iscntrl(long) ChpeAutoiscntrl
1920 cdecl isdigit(long) ChpeAutoisdigit
1921 cdecl isgraph(long) ChpeAutoisgraph
1922 cdecl islower(long) ChpeAutoislower
1923 cdecl isprint(long) ChpeAutoisprint
1924 cdecl ispunct(long) ChpeAutoispunct
1925 cdecl isspace(long) ChpeAutoisspace
1926 cdecl isupper(long) ChpeAutoisupper
1927 cdecl iswalpha(long) ChpeAutoiswalpha
1928 cdecl iswctype(long long) ChpeAutoiswctype
1929 cdecl iswdigit(long) ChpeAutoiswdigit
1930 cdecl iswlower(long) ChpeAutoiswlower
1931 cdecl iswspace(long) ChpeAutoiswspace
1932 cdecl iswxdigit(long) ChpeAutoiswxdigit
1933 cdecl isxdigit(long) ChpeAutoisxdigit
1934 cdecl labs(long) ChpeAutolabs
1935 cdecl log(double) ChpeAutolog
1937 cdecl mbstowcs(ptr str long) ChpeAutombstowcs
1938 cdecl memchr(ptr long long) ChpeAutomemchr
1939 cdecl memcmp(ptr ptr long) ChpeAutomemcmp
1941 cdecl memmove(ptr ptr long) ChpeAutomemmove
1942 cdecl memset(ptr long long) ChpeAutomemset
1943 cdecl pow(double double) ChpeAutopow
1945 cdecl sin(double) ChpeAutosin
1947 cdecl sqrt(double) ChpeAutosqrt
1949 cdecl strcat(str str) ChpeAutostrcat
1950 cdecl strchr(str long) ChpeAutostrchr
1951 cdecl strcmp(str str) ChpeAutostrcmp
1952 cdecl strcpy(ptr str) ChpeAutostrcpy
1953 cdecl -version=0x600+ strcpy_s(ptr long str) ChpeAutostrcpy_s
1954 cdecl -version=0x600+ strcat_s(ptr long str) ChpeAutostrcat_s
1955 cdecl -version=0x600+ strncpy_s(ptr long str long) ChpeAutostrncpy_s
1956 cdecl strcspn(str str) ChpeAutostrcspn
1957 cdecl strlen(str) ChpeAutostrlen
1958 cdecl strncat(str str long) ChpeAutostrncat
1959 cdecl strncmp(str str long) ChpeAutostrncmp
1960 cdecl strncpy(ptr str long) ChpeAutostrncpy
1961 cdecl strpbrk(str str) ChpeAutostrpbrk
1962 cdecl strrchr(str long) ChpeAutostrrchr
1963 cdecl strspn(str str) ChpeAutostrspn
1964 cdecl strstr(str str) ChpeAutostrstr
1965 cdecl strtol(str ptr long) ChpeAutostrtol
1966 cdecl strtoul(str ptr long) ChpeAutostrtoul
1968 cdecl tan(double) ChpeAutotan
1969 cdecl tolower(long) ChpeAutotolower
1970 cdecl toupper(long) ChpeAutotoupper
1971 cdecl towlower(long) ChpeAutotowlower
1972 cdecl towupper(long) ChpeAutotowupper
1975 cdecl vsprintf(ptr str ptr) ChpeAutovsprintf
1976 cdecl wcscat(wstr wstr) ChpeAutowcscat
1977 cdecl wcschr(wstr long) ChpeAutowcschr
1978 cdecl wcscmp(wstr wstr) ChpeAutowcscmp
1979 cdecl wcscpy(ptr wstr) ChpeAutowcscpy
1980 cdecl wcscspn(wstr wstr) ChpeAutowcscspn
1981 cdecl wcslen(wstr) ChpeAutowcslen
1982 cdecl wcsncat(wstr wstr long) ChpeAutowcsncat
1983 cdecl wcsncmp(wstr wstr long) ChpeAutowcsncmp
1984 cdecl wcsncpy(ptr wstr long) ChpeAutowcsncpy
1985 cdecl wcsnlen(wstr long) ChpeAutowcsnlen
1986 cdecl wcspbrk(wstr wstr) ChpeAutowcspbrk
1987 cdecl wcsrchr(wstr long) ChpeAutowcsrchr
1988 cdecl wcsspn(wstr wstr) ChpeAutowcsspn
1989 cdecl wcsstr(wstr wstr) ChpeAutowcsstr
1990 cdecl wcstol(wstr ptr long) ChpeAutowcstol
1991 cdecl wcstombs(ptr ptr long) ChpeAutowcstombs
1992 cdecl wcstoul(wstr ptr long) ChpeAutowcstoul
1995 stdcall -version=0x602+ -ret64 RtlGetSystemTimePrecise() ChpeAutoRtlGetSystemTimePrecise
# END typed bridge wrappers
