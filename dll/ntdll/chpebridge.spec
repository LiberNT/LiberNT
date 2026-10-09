# PROJECT:     ReactOS ARM64EC runtime
# PURPOSE:     Native NTDLL bridge exports for emulated AMD64 imports
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>

38 stdcall DbgBreakPoint() ChpeDbgBreakPoint
39 varargs DbgPrint(str) ChpeDbgPrint
40 varargs DbgPrintEx(long long str) ChpeDbgPrintEx
59 stdcall -version=0x600+ EtwEventActivityIdControl(long ptr) ChpeEtwEventActivityIdControl
62 stdcall -version=0x600+ EtwEventRegister(ptr ptr ptr ptr) ChpeEtwEventRegister
64 stdcall -version=0x600+ EtwEventUnregister(int64) ChpeEtwEventUnregister
65 stdcall -version=0x600+ EtwEventWrite(int64 ptr long ptr) ChpeEtwEventWrite
71 stdcall -version=0x600+ EtwEventWriteTransfer(int64 ptr ptr ptr long ptr) ChpeEtwEventWriteTransfer
1861 cdecl __C_specific_handler(ptr long ptr ptr) ChpeCSpecificHandler
2000 stdcall ChpeDispatchExceptionNative(ptr ptr)
105 stdcall KiUserExceptionDispatcher(ptr ptr) ChpeKiUserExceptionDispatcher
108 stdcall LdrAccessResource(ptr ptr ptr ptr) ChpeLdrAccessResource
110 stdcall LdrAddRefDll(long ptr) ChpeLdrAddRefDll
112 stdcall LdrEnumResources(ptr ptr long ptr ptr) ChpeLdrEnumResources
114 stdcall LdrFindEntryForAddress(ptr ptr) ChpeLdrFindEntryForAddress
115 stdcall LdrFindResourceDirectory_U(ptr ptr long ptr) ChpeLdrFindResourceDirectory_U
117 stdcall LdrFindResource_U(ptr ptr long ptr) ChpeLdrFindResource_U
119 stdcall LdrGetDllHandle(wstr ptr ptr ptr) ChpeLdrGetDllHandle
120 stdcall LdrGetDllHandleEx(long wstr ptr ptr ptr) ChpeLdrGetDllHandleEx
124 stdcall LdrGetProcedureAddress(ptr ptr long ptr) ChpeLdrGetProcedureAddress
131 stdcall LdrLoadDll(wstr long ptr ptr) ChpeLdrLoadDll
140 stdcall LdrQueryProcessModuleInformation(ptr long ptr) ChpeLdrQueryProcessModuleInformation
143 stdcall -version=0x602+ LdrResolveDelayLoadedAPI(ptr ptr ptr ptr ptr long) ChpeLdrResolveDelayLoadedAPI
144 stdcall -version=0x602+ LdrResolveDelayLoadsFromDll(ptr str long) ChpeLdrResolveDelayLoadsFromDll
157 stdcall LdrUnloadDll(ptr) ChpeLdrUnloadDll
187 stdcall NtAdjustPrivilegesToken(long long ptr long ptr ptr) ChpeAutoNtAdjustPrivilegesToken
196 stdcall NtAllocateVirtualMemory(long ptr ptr ptr long long) ChpeAutoNtAllocateVirtualMemory
197 stdcall NtAllocateVirtualMemoryEx(long ptr ptr long long ptr long) ChpeAutoNtAllocateVirtualMemoryEx
191 stdcall NtAlertThreadByThreadId(long) ChpeAutoNtAlertThreadByThreadId
231 stdcall NtClose(long) ChpeAutoNtClose
242 stdcall NtContinue(ptr long) ChpeNtContinue
243 stdcall -version=0xA00+ NtContinueEx(ptr ptr) ChpeNtContinueEx
250 stdcall NtCreateFile(ptr long ptr ptr ptr long long long long ptr long) ChpeAutoNtCreateFile
262 stdcall NtCreateNamedPipeFile(ptr long ptr ptr long long long long long long long long long ptr) ChpeAutoNtCreateNamedPipeFile
297 stdcall NtDeviceIoControlFile(long long ptr ptr ptr long ptr long ptr long) ChpeAutoNtDeviceIoControlFile
316 stdcall -version=0x600+ NtFlushProcessWriteBuffers() ChpeAutoNtFlushProcessWriteBuffers
314 stdcall NtFlushInstructionCache(long ptr long) ChpeAutoNtFlushInstructionCache
320 stdcall NtFreeVirtualMemory(long ptr ptr long) ChpeAutoNtFreeVirtualMemory
324 stdcall NtGetContextThread(long ptr) ChpeNtGetContextThread
330 stdcall -version=0x600+ NtGetNextThread(ptr ptr long long long ptr) ChpeAutoNtGetNextThread
359 stdcall NtMapViewOfSection(long long ptr long ptr ptr ptr long long long) ChpeAutoNtMapViewOfSection
360 stdcall NtMapViewOfSectionEx(long long ptr ptr ptr long long ptr long) ChpeNtMapViewOfSectionEx
365 stdcall NtNotifyChangeKey(ptr ptr ptr ptr ptr long long ptr long long) ChpeAutoNtNotifyChangeKey
366 stdcall NtNotifyChangeMultipleKeys(ptr long ptr ptr ptr ptr ptr long long ptr long long) ChpeAutoNtNotifyChangeMultipleKeys
371 stdcall NtOpenFile(ptr long ptr ptr long long) ChpeAutoNtOpenFile
406 stdcall NtProtectVirtualMemory(long ptr ptr long ptr) ChpeAutoNtProtectVirtualMemory
414 stdcall NtQueryDirectoryFile(long long ptr ptr ptr ptr long long long ptr long) ChpeAutoNtQueryDirectoryFile
424 stdcall NtQueryInformationFile(long ptr ptr long long) ChpeAutoNtQueryInformationFile
437 stdcall NtQueryKey(long long ptr long ptr) ChpeAutoNtQueryKey
257 stdcall NtCreateKey(ptr long ptr long ptr long ptr) ChpeAutoNtCreateKey
429 stdcall NtQueryInformationThread(long long ptr long ptr) ChpeAutoNtQueryInformationThread
427 stdcall NtQueryInformationProcess(ptr long ptr long ptr) ChpeAutoNtQueryInformationProcess
887 stdcall RtlFormatCurrentUserKeyPath(ptr) ChpeRtlFormatCurrentUserKeyPath
898 stdcall RtlFreeUnicodeString(ptr) ChpeRtlFreeUnicodeString
374 stdcall NtOpenKey(ptr long ptr) ChpeAutoNtOpenKey
835 stdcall NtOpenKeyEx(ptr long ptr long) ChpeAutoNtOpenKeyEx
458 stdcall NtQueryValueKey(long ptr long ptr long ptr) ChpeAutoNtQueryValueKey
555 stdcall NtSetValueKey(long ptr long long ptr long) ChpeAutoNtSetValueKey
291 stdcall NtDeleteKey(long) ChpeAutoNtDeleteKey
294 stdcall NtDeleteValueKey(long ptr) ChpeAutoNtDeleteValueKey
303 stdcall NtEnumerateKey(long long long ptr long ptr) ChpeAutoNtEnumerateKey
306 stdcall NtEnumerateValueKey(long long long ptr long ptr) ChpeAutoNtEnumerateValueKey
315 stdcall NtFlushKey(long) ChpeAutoNtFlushKey
252 stdcall NtCreateWaitCompletionPacket(ptr long ptr) ChpeAutoNtCreateWaitCompletionPacket
253 stdcall NtAssociateWaitCompletionPacket(long long long ptr ptr long long ptr) ChpeAutoNtAssociateWaitCompletionPacket
254 stdcall NtCancelWaitCompletionPacket(long long) ChpeAutoNtCancelWaitCompletionPacket
460 stdcall NtQueryVolumeInformationFile(long ptr ptr long long) ChpeAutoNtQueryVolumeInformationFile
441 stdcall NtQueryObject(long long long long long) ChpeAutoNtQueryObject
453 stdcall NtQuerySystemInformation(long ptr long ptr) ChpeAutoNtQuerySystemInformation
934 stdcall RtlGetNativeSystemInformation(long ptr long ptr) ChpeRtlGetNativeSystemInformation
459 stdcall NtQueryVirtualMemory(long ptr long ptr long ptr) ChpeAutoNtQueryVirtualMemory
466 stdcall NtRaiseException(ptr ptr long) ChpeNtRaiseException
468 stdcall NtReadFile(long long ptr ptr ptr ptr long ptr ptr) ChpeAutoNtReadFile
472 stdcall NtReadVirtualMemory(long ptr ptr long ptr) ChpeAutoNtReadVirtualMemory
513 stdcall NtSetContextThread(long ptr) ChpeNtSetContextThread
564 stdcall NtSuspendProcess(ptr) ChpeAutoNtSuspendProcess
565 stdcall NtSuspendThread(ptr ptr) ChpeAutoNtSuspendThread
502 stdcall NtResumeThread(ptr ptr) ChpeAutoNtResumeThread
568 stdcall NtTerminateProcess(long long) ChpeAutoNtTerminateProcess
569 stdcall NtTerminateThread(long long) ChpeAutoNtTerminateThread
582 stdcall NtUnmapViewOfSection(long ptr) ChpeAutoNtUnmapViewOfSection
583 stdcall NtUnmapViewOfSectionEx(long ptr long) ChpeNtUnmapViewOfSectionEx
589 stdcall NtWaitForAlertByThreadId(ptr ptr) ChpeAutoNtWaitForAlertByThreadId
597 stdcall NtWriteFile(long long ptr ptr ptr ptr long ptr ptr) ChpeAutoNtWriteFile
600 stdcall NtWriteVirtualMemory(long ptr ptr long ptr) ChpeAutoNtWriteVirtualMemory
635 stdcall RtlAddFunctionTable(ptr long long) ChpeRtlAddFunctionTable
636 stdcall RtlAddGrowableFunctionTable(ptr ptr long long long long) ChpeRtlAddGrowableFunctionTable
642 stdcall RtlAddVectoredContinueHandler(long ptr) ChpeRtlAddVectoredContinueHandler
643 stdcall RtlAddVectoredExceptionHandler(long ptr) ChpeRtlAddVectoredExceptionHandler
649 stdcall RtlAllocateHeap(ptr long ptr) ChpeRtlAllocateHeap
909 stdcall RtlGetCurrentPeb() ChpeRtlGetCurrentPeb
941 stdcall RtlGetProcessHeaps(long ptr) ChpeRtlGetProcessHeaps
1122 stdcall RtlQueryEnvironmentVariable(ptr ptr long ptr long ptr) ChpeRtlQueryEnvironmentVariable
613 stdcall RtlAcquirePrivilege(ptr long long ptr) ChpeRtlAcquirePrivilege
616 stdcall RtlAcquireSRWLockExclusive(ptr) ChpeRtlAcquireSRWLockExclusive
617 stdcall RtlAcquireSRWLockShared(ptr) ChpeRtlAcquireSRWLockShared
671 stdcall RtlCaptureContext(ptr) ChpeRtlCaptureContextX64
672 stdcall RtlCaptureStackBackTrace(long long ptr ptr) ChpeRtlCaptureStackBackTrace
687 stdcall RtlCompareMemory(ptr ptr long) ChpeRtlCompareMemory
690 stdcall RtlCompareUnicodeString(ptr ptr long) ChpeRtlCompareUnicodeString
718 stdcall RtlCopyUnicodeString(ptr ptr) ChpeRtlCopyUnicodeString
762 stdcall RtlDecodePointer(ptr) ChpeRtlDecodePointer
763 stdcall RtlDecodeSystemPointer(ptr) ChpeRtlDecodeSystemPointer
773 stdcall RtlDeleteCriticalSection(ptr) ChpeRtlDeleteCriticalSection
776 cdecl RtlDeleteFunctionTable(ptr) ChpeRtlDeleteFunctionTable
777 stdcall RtlDeleteGrowableFunctionTable(ptr) ChpeRtlDeleteGrowableFunctionTable
817 stdcall RtlEnterCriticalSection(ptr) ChpeRtlEnterCriticalSection
815 stdcall RtlEncodePointer(ptr) ChpeRtlEncodePointer
816 stdcall RtlEncodeSystemPointer(ptr) ChpeRtlEncodeSystemPointer
855 stdcall RtlExitUserThread(long) ChpeRtlExitUserThread
860 stdcall RtlFillMemory(ptr long long) ChpeRtlFillMemory
882 stdcall RtlFlsAlloc(ptr ptr) ChpeRtlFlsAlloc
883 stdcall RtlFlsFree(long) ChpeRtlFlsFree
884 stdcall RtlFlsGetValue(long ptr) ChpeRtlFlsGetValue
885 stdcall RtlFlsSetValue(long ptr) ChpeRtlFlsSetValue
893 stdcall RtlFreeHeap(long long long) ChpeRtlFreeHeap
925 stdcall RtlGetFunctionTableListHead() ChpeRtlGetFunctionTableListHead
928 stdcall RtlGetLastNtStatus() ChpeRtlGetLastNtStatus
929 stdcall RtlGetLastWin32Error() ChpeRtlGetLastWin32Error
910 stdcall RtlGetCurrentProcessorNumber() ChpeRtlGetCurrentProcessorNumber
911 stdcall -version=0x601+ RtlGetCurrentProcessorNumberEx(ptr) ChpeRtlGetCurrentProcessorNumberEx
942 stdcall -version=0x600+ RtlGetProductInfo(long long long long ptr) ChpeRtlGetProductInfo
955 stdcall RtlGetVersion(ptr) ChpeRtlGetVersion
1007 stdcall RtlGrowFunctionTable(ptr long) ChpeRtlGrowFunctionTable
980 stdcall RtlInitUnicodeString(ptr wstr) ChpeRtlInitUnicodeString
985 stdcall -version=0x600+ RtlInitializeConditionVariable(ptr) ChpeRtlInitializeConditionVariable
987 stdcall RtlInitializeCriticalSection(ptr) ChpeRtlInitializeCriticalSection
998 stdcall RtlInitializeSListHead(ptr) ChpeRtlInitializeSListHead
999 stdcall -version=0x600+ RtlInitializeSRWLock(ptr) ChpeRtlInitializeSRWLock
1006 cdecl RtlInstallFunctionTableCallback(double double long ptr ptr ptr) ChpeRtlInstallFunctionTableCallback
1011 stdcall RtlInterlockedFlushSList(ptr) ChpeRtlInterlockedFlushSList
1012 stdcall RtlInterlockedPopEntrySList(ptr) ChpeRtlInterlockedPopEntrySList
1013 stdcall RtlInterlockedPushEntrySList(ptr ptr) ChpeRtlInterlockedPushEntrySList
1014 stdcall RtlInterlockedPushListSList(ptr ptr ptr long) ChpeRtlInterlockedPushListSList
1015 stdcall -version=0x602+ RtlInterlockedPushListSListEx(ptr ptr ptr long) ChpeRtlInterlockedPushListSListEx
1044 stdcall RtlIsProcessorFeaturePresent(long) ChpeRtlIsProcessorFeaturePresent
1053 stdcall RtlLeaveCriticalSection(ptr) ChpeRtlLeaveCriticalSection
1075 stdcall RtlLookupFunctionEntry(long ptr ptr) ChpeRtlLookupFunctionEntry
1076 stdcall RtlLookupFunctionTable(int64 ptr ptr) ChpeRtlLookupFunctionTable
1066 stdcall -version=0xA00+ RtlLogUnexpectedCodepath(ptr) ChpeRtlLogUnexpectedCodepath
1080 stdcall RtlMoveMemory(ptr ptr long) ChpeRtlMoveMemory
1094 stdcall RtlNtStatusToDosError(long) ChpeRtlNtStatusToDosError
1107 stdcall RtlPcToFileHeader(ptr ptr) ChpeRtlPcToFileHeader
1119 stdcall RtlQueryDepthSList(ptr) ChpeRtlQueryDepthSList
1141 stdcall -norelay RtlRaiseException(ptr) ChpeRtlRaiseException
1142 stdcall RtlRaiseStatus(long) ChpeRtlRaiseStatus
1143 stdcall RtlRandom(ptr) ChpeRtlRandom
1145 stdcall RtlReAllocateHeap(long long ptr long) ChpeRtlReAllocateHeap
1157 stdcall RtlReleasePrivilege(ptr) ChpeRtlReleasePrivilege
1160 stdcall RtlReleaseSRWLockExclusive(ptr) ChpeRtlReleaseSRWLockExclusive
1161 stdcall RtlReleaseSRWLockShared(ptr) ChpeRtlReleaseSRWLockShared
1164 stdcall RtlRemoveVectoredContinueHandler(ptr) ChpeRtlRemoveVectoredContinueHandler
1165 stdcall RtlRemoveVectoredExceptionHandler(ptr) ChpeRtlRemoveVectoredExceptionHandler
1171 stdcall RtlRestoreContext(ptr ptr) ChpeRtlRestoreContext
1172 stdcall RtlRestoreLastWin32Error(long) ChpeRtlRestoreLastWin32Error
1179 stdcall -version=0x600+ RtlRunOnceExecuteOnce(ptr ptr ptr ptr) ChpeRtlRunOnceExecuteOnce
1180 stdcall -version=0x600+ RtlRunOnceInitialize(ptr) ChpeRtlRunOnceInitialize
1231 stdcall RtlSizeHeap(long long ptr) ChpeRtlSizeHeap
1192 stdcall RtlSetCriticalSectionSpinCount(ptr long) ChpeRtlSetCriticalSectionSpinCount
1206 stdcall RtlSetLastWin32Error(long) ChpeRtlSetLastWin32Error
1258 stdcall RtlTryAcquireSRWLockExclusive(ptr) ChpeRtlTryAcquireSRWLockExclusive
1259 stdcall RtlTryAcquireSRWLockShared(ptr) ChpeRtlTryAcquireSRWLockShared
1260 stdcall RtlTryEnterCriticalSection(ptr) ChpeRtlTryEnterCriticalSection
1266 stdcall -version=0x601+ RtlUTF8ToUnicodeN(ptr long ptr str long) ChpeRtlUTF8ToUnicodeN
1284 stdcall RtlUnwind(ptr ptr ptr ptr) ChpeRtlUnwind
1285 stdcall RtlUnwindEx(ptr ptr ptr ptr ptr ptr) ChpeRtlUnwindEx
1309 stdcall RtlVirtualUnwind(long int64 int64 ptr ptr ptr ptr ptr) ChpeRtlVirtualUnwind
1311 stdcall -version=0x602+ RtlWaitOnAddress(ptr ptr long ptr) ChpeRtlWaitOnAddress
1313 stdcall -version=0x602+ RtlWakeAddressAll(ptr) ChpeRtlWakeAddressAll
1314 stdcall -version=0x602+ RtlWakeAddressSingle(ptr) ChpeRtlWakeAddressSingle
1312 stdcall RtlWakeAllConditionVariable(ptr) ChpeRtlWakeAllConditionVariable
1315 stdcall RtlWakeConditionVariable(ptr) ChpeRtlWakeConditionVariable
1338 stdcall RtlZeroMemory(ptr long) ChpeRtlZeroMemory
1392 stdcall -version=0x600+ TpCallbackLeaveCriticalSectionOnCompletion(ptr ptr) ChpeTpCallbackLeaveCriticalSectionOnCompletion
1394 stdcall -version=0x600+ TpCallbackReleaseMutexOnCompletion(ptr ptr) ChpeTpCallbackReleaseMutexOnCompletion
1395 stdcall -version=0x600+ TpCallbackReleaseSemaphoreOnCompletion(ptr ptr long) ChpeTpCallbackReleaseSemaphoreOnCompletion
1398 stdcall -version=0x600+ TpCallbackSetEventOnCompletion(ptr ptr) ChpeTpCallbackSetEventOnCompletion
1399 stdcall -version=0x600+ TpCallbackUnloadDllOnCompletion(ptr ptr) ChpeTpCallbackUnloadDllOnCompletion
1400 stdcall -version=0x600+ TpCancelAsyncIoOperation(ptr) ChpeTpCancelAsyncIoOperation
1405 stdcall -version=0x600+ TpDisassociateCallback(ptr) ChpeTpDisassociateCallback
1406 stdcall -version=0x600+ TpIsTimerSet(ptr) ChpeTpIsTimerSet
1407 stdcall -version=0x600+ TpPostWork(ptr) ChpeTpPostWork
1410 stdcall -version=0x600+ TpReleaseCleanupGroup(ptr) ChpeTpReleaseCleanupGroup
1411 stdcall -version=0x600+ TpReleaseCleanupGroupMembers(ptr long ptr) ChpeTpReleaseCleanupGroupMembers
1412 stdcall -version=0x600+ TpReleaseIoCompletion(ptr) ChpeTpReleaseIoCompletion
1413 stdcall -version=0x600+ TpReleasePool(ptr) ChpeTpReleasePool
1414 stdcall TpReleaseTimer(ptr) ChpeTpReleaseTimer
1415 stdcall TpReleaseWait(ptr) ChpeTpReleaseWait
1416 stdcall -version=0x600+ TpReleaseWork(ptr) ChpeTpReleaseWork
1417 stdcall -version=0x600+ TpSetPoolMaxThreads(ptr long) ChpeTpSetPoolMaxThreads
1418 stdcall -version=0x600+ TpSetPoolMinThreads(ptr long) ChpeTpSetPoolMinThreads
1420 stdcall TpSetTimer(ptr ptr long long) ChpeTpSetTimer
1421 stdcall -version=0x602+ TpSetTimerEx(ptr ptr long long) ChpeTpSetTimerEx
1422 stdcall TpSetWait(ptr long ptr) ChpeTpSetWait
1423 stdcall -version=0x602+ TpSetWaitEx(ptr long ptr ptr) ChpeTpSetWaitEx
1425 stdcall -version=0x600+ TpStartAsyncIoOperation(ptr) ChpeTpStartAsyncIoOperation
1427 stdcall -version=0x600+ TpWaitForIoCompletion(ptr long) ChpeTpWaitForIoCompletion
1428 stdcall TpWaitForTimer(ptr long) ChpeTpWaitForTimer
1429 stdcall -version=0x600+ TpWaitForWait(ptr long) ChpeTpWaitForWait
1430 stdcall -version=0x600+ TpWaitForWork(ptr long) ChpeTpWaitForWork
1431 stdcall -ret64 VerSetConditionMask(double long long) ChpeVerSetConditionMask
1883 varargs _snprintf(ptr long str) ChpeSnprintf
1885 varargs _snwprintf(ptr long wstr) ChpeSnwprintf
1893 varargs _swprintf(ptr wstr) ChpeSwprintf
1950 varargs sprintf(ptr str) ChpeSprintf
1971 varargs swprintf(ptr wstr) ChpeSwprintf
1862 cdecl __chkstk() ChpeChkStk
1876 cdecl _local_unwind(ptr ptr) ChpeLocalUnwind
1944 cdecl memcpy(ptr ptr long) ChpeMemcpy
2001 stdcall ChpeEmulationDispatch(ptr)
1328 stdcall RtlWow64GetThreadSelectorEntry(ptr ptr long ptr) ChpeRtlWow64GetThreadSelectorEntry

274 stdcall NtCreateThread(ptr long ptr long ptr ptr ptr long) ChpeNtCreateThread
323 stdcall NtFsControlFile(long long ptr ptr ptr long ptr long ptr long) ChpeAutoNtFsControlFile
552 stdcall NtSetTimer(long ptr ptr ptr long long ptr) ChpeAutoNtSetTimer
753 stdcall RtlCreateUserThread(long ptr long long ptr ptr ptr ptr ptr) ChpeRtlCreateUserThread
765 stdcall RtlDecompressFragment(long ptr long ptr long long ptr ptr) ChpeRtlDecompressFragment
901 stdcall RtlGenerate8dot3Name(ptr long ptr ptr) ChpeRtlGenerate8dot3Name
931 stdcall RtlGetLengthWithoutLastFullDosOrNtPathElement(long ptr ptr) ChpeRtlGetLengthWithoutLastFullDosOrNtPathElement
951 stdcall RtlGetUnloadEventTrace() ChpeRtlGetUnloadEventTrace
1140 stdcall RtlQueueWorkItem(ptr ptr long) ChpeRtlQueueWorkItem
1153 stdcall RtlRegisterWait(ptr ptr ptr ptr long long) ChpeRtlRegisterWait
1340 stdcall RtlpApplyLengthFunction(long long ptr ptr) ChpeRtlpApplyLengthFunction
1977 stdcall vDbgPrintEx(long long str ptr) ChpevDbgPrintEx
1978 stdcall vDbgPrintExWithPrefix(str long long str ptr) ChpevDbgPrintExWithPrefix
834 stdcall LdrGetDllFullName(ptr ptr) ChpeLdrGetDllFullName
871 stdcall RtlFindExportedRoutineByName(ptr str) ChpeRtlFindExportedRoutineByName
837 stdcall RtlIsEcCode(ptr) ChpeRtlIsEcCode
846 stdcall RtlLocateExtendedFeature(ptr long ptr) ChpeRtlLocateExtendedFeature
847 stdcall RtlLocateExtendedFeature2(ptr long ptr ptr) ChpeRtlLocateExtendedFeature2
849 stdcall RtlQueryPerformanceCounter(ptr) ChpeRtlQueryPerformanceCounter
850 stdcall RtlQueryPerformanceFrequency(ptr) ChpeRtlQueryPerformanceFrequency
852 stdcall RtlQuerySystemTime(ptr) ChpeRtlQuerySystemTime
853 stdcall RtlSystemTimeToTimeFields(ptr ptr) ChpeRtlSystemTimeToTimeFields
41 varargs DbgPrintReturnControlC(str) ChpeDbgPrintReturnControlC
55 stdcall DbgUserBreakPoint() ChpeDbgUserBreakPoint
87 varargs EtwTraceMessage(int64 long ptr long) ChpeEtwTraceMessage
1300 stdcall RtlUserThreadStart(long long) ChpeRtlUserThreadStart
1952 varargs sscanf(str str) ChpeSscanf
1997 cdecl ChpeVsscanf(str str ptr) ChpeAutoVsscanf
1998 stdcall ChpeVDbgPrintReturnControlC(str ptr) ChpeAutoVDbgPrintReturnControlC
1869 cdecl -private _errno() ChpeErrno
1875 cdecl _lfind(ptr ptr ptr long ptr) ChpeLfind
1916 cdecl bsearch(ptr ptr long long ptr) ChpeBsearch
1948 cdecl qsort(ptr long long ptr) ChpeQsort
1881 cdecl _setjmp(ptr ptr) ChpeSetJmpX64
1882 cdecl _setjmpex(ptr ptr) ChpeSetJmpX64
1940 cdecl longjmp(ptr long) ChpeLongJmp
974 stdcall RtlInitMemoryStream(ptr) ChpeRtlInitMemoryStream
976 stdcall RtlInitOutOfProcessMemoryStream(ptr) ChpeRtlInitOutOfProcessMemoryStream
1155 stdcall RtlReleaseMemoryStream(ptr) ChpeRtlReleaseMemoryStream
96 stdcall ExpInterlockedPopEntrySListEnd() ChpeUnsupportedKernelEntry
98 stdcall ExpInterlockedPopEntrySListFault() ChpeUnsupportedKernelEntry
100 stdcall ExpInterlockedPopEntrySListResume() ChpeUnsupportedKernelEntry
103 stdcall KiUserApcDispatcher(ptr ptr ptr ptr) ChpeUnsupportedApcEntry
107 stdcall KiUserEmulationDispatcher(ptr) ChpeUnsupportedEmulationEntry

# BEGIN typed bridge wrappers (generate_chpe_bridge.py)
1 stdcall ApiSetQueryApiSetPresence(ptr ptr) ChpeAutoApiSetQueryApiSetPresence
2 stdcall -version=0x600+ RtlFlushHeaps() ChpeAutoRtlFlushHeaps
3 stdcall -version=0x602+ RtlQueryWnfStateData(ptr int64 ptr ptr ptr long) ChpeAutoRtlQueryWnfStateData
4 stdcall -version=0x602+ RtlSubscribeWnfStateChangeNotification(ptr int64 long ptr ptr ptr long long long) ChpeAutoRtlSubscribeWnfStateChangeNotification
5 stdcall -version=0x602+ RtlUnsubscribeWnfNotificationWaitForCompletion(ptr) ChpeAutoRtlUnsubscribeWnfNotificationWaitForCompletion
6 stdcall -version=0x600+ A_SHAFinal(ptr ptr) ChpeAutoA_SHAFinal
7 stdcall -version=0x600+ A_SHAInit(ptr) ChpeAutoA_SHAInit
8 stdcall -version=0x600+ A_SHAUpdate(ptr ptr long) ChpeAutoA_SHAUpdate
9 stdcall -version=0x600+ AlpcAdjustCompletionListConcurrencyCount(ptr long) ChpeAutoAlpcAdjustCompletionListConcurrencyCount
10 stdcall -version=0x600+ AlpcFreeCompletionListMessage(ptr ptr) ChpeAutoAlpcFreeCompletionListMessage
11 stdcall -version=0x600+ AlpcGetCompletionListLastMessageInformation(ptr ptr ptr) ChpeAutoAlpcGetCompletionListLastMessageInformation
12 stdcall -version=0x600+ AlpcGetCompletionListMessageAttributes(ptr ptr) ChpeAutoAlpcGetCompletionListMessageAttributes
13 stdcall -version=0x600+ AlpcGetHeaderSize(long) ChpeAutoAlpcGetHeaderSize
14 stdcall -version=0x600+ AlpcGetMessageAttribute(ptr long) ChpeAutoAlpcGetMessageAttribute
15 stdcall -version=0x600+ AlpcGetMessageFromCompletionList(ptr ptr) ChpeAutoAlpcGetMessageFromCompletionList
16 stdcall -version=0x600+ AlpcGetOutstandingCompletionListMessageCount(ptr) ChpeAutoAlpcGetOutstandingCompletionListMessageCount
17 stdcall -version=0x600+ AlpcInitializeMessageAttribute(long ptr long ptr) ChpeAutoAlpcInitializeMessageAttribute
18 stdcall -version=0x600+ AlpcMaxAllowedMessageLength() ChpeAutoAlpcMaxAllowedMessageLength
19 stdcall -version=0x600+ AlpcRegisterCompletionList(ptr ptr long long long) ChpeAutoAlpcRegisterCompletionList
20 stdcall -version=0x600+ AlpcRegisterCompletionListWorkerThread(ptr) ChpeAutoAlpcRegisterCompletionListWorkerThread
21 stdcall -version=0x600+ AlpcRundownCompletionList(ptr) ChpeAutoAlpcRundownCompletionList
22 stdcall -version=0x600+ AlpcUnregisterCompletionList(ptr) ChpeAutoAlpcUnregisterCompletionList
23 stdcall -version=0x600+ AlpcUnregisterCompletionListWorkerThread(ptr) ChpeAutoAlpcUnregisterCompletionListWorkerThread
24 stdcall CsrAllocateCaptureBuffer(long long) ChpeAutoCsrAllocateCaptureBuffer
25 stdcall CsrAllocateMessagePointer(ptr long ptr) ChpeAutoCsrAllocateMessagePointer
26 stdcall CsrCaptureMessageBuffer(ptr ptr long ptr) ChpeAutoCsrCaptureMessageBuffer
27 stdcall CsrCaptureMessageMultiUnicodeStringsInPlace(ptr long ptr) ChpeAutoCsrCaptureMessageMultiUnicodeStringsInPlace
28 stdcall CsrCaptureMessageString(ptr str long long ptr) ChpeAutoCsrCaptureMessageString
29 stdcall CsrCaptureTimeout(long ptr) ChpeAutoCsrCaptureTimeout
30 stdcall CsrClientCallServer(ptr ptr long long) ChpeAutoCsrClientCallServer
31 stdcall CsrClientConnectToServer(str long ptr ptr ptr) ChpeAutoCsrClientConnectToServer
32 stdcall CsrFreeCaptureBuffer(ptr) ChpeAutoCsrFreeCaptureBuffer
33 stdcall CsrGetProcessId() ChpeAutoCsrGetProcessId
34 stdcall CsrIdentifyAlertableThread() ChpeAutoCsrIdentifyAlertableThread
35 stdcall -version=0x502+ CsrNewThread() ChpeAutoCsrNewThread
36 stdcall CsrSetPriorityClass(ptr ptr) ChpeAutoCsrSetPriorityClass
37 stdcall -version=0x600+ CsrVerifyRegion(ptr long) ChpeStubCsrVerifyRegion
42 stdcall DbgPrompt(ptr ptr long) ChpeAutoDbgPrompt
43 stdcall DbgQueryDebugFilterState(long long) ChpeAutoDbgQueryDebugFilterState
44 stdcall DbgSetDebugFilterState(long long long) ChpeAutoDbgSetDebugFilterState
45 stdcall DbgUiConnectToDbg() ChpeAutoDbgUiConnectToDbg
46 stdcall DbgUiContinue(ptr long) ChpeAutoDbgUiContinue
47 stdcall DbgUiConvertStateChangeStructure(ptr ptr) ChpeAutoDbgUiConvertStateChangeStructure
48 stdcall DbgUiDebugActiveProcess(ptr) ChpeAutoDbgUiDebugActiveProcess
49 stdcall DbgUiGetThreadDebugObject() ChpeAutoDbgUiGetThreadDebugObject
50 stdcall DbgUiIssueRemoteBreakin(ptr) ChpeAutoDbgUiIssueRemoteBreakin
51 stdcall DbgUiRemoteBreakin() ChpeAutoDbgUiRemoteBreakin
52 stdcall DbgUiSetThreadDebugObject(ptr) ChpeAutoDbgUiSetThreadDebugObject
53 stdcall DbgUiStopDebugging(ptr) ChpeAutoDbgUiStopDebugging
54 stdcall DbgUiWaitStateChange(ptr ptr) ChpeAutoDbgUiWaitStateChange
56 stdcall  EtwCreateTraceInstanceId(ptr ptr) ChpeStubEtwCreateTraceInstanceId
57 stdcall -version=0x600+ EtwDeliverDataBlock(long) ChpeStubEtwDeliverDataBlock
58 stdcall -version=0x600+ EtwEnumerateProcessRegGuids(ptr long ptr) ChpeStubEtwEnumerateProcessRegGuids
60 stdcall -version=0x600+ EtwEventEnabled(int64 ptr) ChpeAutoEtwEventEnabled
61 stdcall -version=0x600+ EtwEventProviderEnabled(int64 long int64) ChpeAutoEtwEventProviderEnabled
63 stdcall -version=0x600+ EtwEventSetInformation(int64 long ptr long) ChpeAutoEtwEventSetInformation
66 stdcall -version=0x601+ EtwEventWriteEx(int64 ptr int64 long ptr ptr long ptr) ChpeAutoEtwEventWriteEx
67 stdcall -version=0x600+ EtwEventWriteEndScenario(long ptr long long) ChpeStubEtwEventWriteEndScenario
68 stdcall -version=0x600+ EtwEventWriteFull(long long long long long long long) ChpeStubEtwEventWriteFull
69 stdcall -version=0x600+ EtwEventWriteStartScenario(long ptr long long) ChpeStubEtwEventWriteStartScenario
70 stdcall -version=0x600+ EtwEventWriteString(int64 long int64 wstr) ChpeAutoEtwEventWriteString
72 stdcall EtwGetTraceEnableFlags(double) ChpeAutoEtwGetTraceEnableFlags
73 stdcall EtwGetTraceEnableLevel(double) ChpeAutoEtwGetTraceEnableLevel
74 stdcall EtwGetTraceLoggerHandle(ptr) ChpeAutoEtwGetTraceLoggerHandle
75 stdcall -version=0x600+ EtwLogTraceEvent(long long) ChpeStubEtwLogTraceEvent
76 stdcall -version=0x600+ EtwNotificationRegister(ptr long long long ptr) ChpeStubEtwNotificationRegister
77 stdcall -version=0x600+ EtwNotificationUnregister(long ptr) ChpeStubEtwNotificationUnregister
78 stdcall -version=0x600+ EtwProcessPrivateLoggerRequest(ptr) ChpeStubEtwProcessPrivateLoggerRequest
79 stdcall -version=0x600+ EtwRegister(ptr ptr ptr ptr) ChpeStubEtwRegister
80 stdcall -version=0x600+ EtwRegisterSecurityProvider() ChpeStubEtwRegisterSecurityProvider
81 stdcall EtwRegisterTraceGuidsA(ptr ptr ptr long ptr str str ptr) ChpeAutoEtwRegisterTraceGuidsA
82 stdcall EtwRegisterTraceGuidsW(ptr ptr ptr long ptr wstr wstr ptr) ChpeAutoEtwRegisterTraceGuidsW
83 stdcall -version=0x600+ EtwReplyNotification(long) ChpeStubEtwReplyNotification
84 stdcall -version=0x600+ EtwSendNotification(long long ptr long long) ChpeStubEtwSendNotification
85 stdcall -version=0x600+ EtwSetMark(long long long) ChpeStubEtwSetMark
86 stdcall  EtwTraceEventInstance(double ptr ptr ptr) ChpeStubEtwTraceEventInstance
88 stdcall  EtwTraceMessageVa(int64 long ptr long ptr) ChpeStubEtwTraceMessageVa
89 stdcall -version=0x600+ EtwUnregister(int64) ChpeStubEtwUnregister
90 stdcall EtwUnregisterTraceGuids(double) ChpeAutoEtwUnregisterTraceGuids
91 stdcall -version=0x600+ EtwWrite(int64 ptr ptr long ptr) ChpeStubEtwWrite
92 stdcall -version=0x600+ EtwWriteUMSecurityEvent(ptr long long long) ChpeStubEtwWriteUMSecurityEvent
93 stdcall -version=0x600+ EtwpCreateEtwThread(long long) ChpeStubEtwpCreateEtwThread
94 stdcall -version=0x600+ EtwpGetCpuSpeed(ptr) ChpeStubEtwpGetCpuSpeed
95 stdcall -version=0x600+ EtwpNotificationThread() ChpeStubEtwpNotificationThread
97 stub -version=0x600+ ExpInterlockedPopEntrySListEnd8
99 stub -version=0x600+ ExpInterlockedPopEntrySListFault8
101 stub -version=0x600+ ExpInterlockedPopEntrySListResume8
102 stdcall KiRaiseUserExceptionDispatcher() ChpeAutoKiRaiseUserExceptionDispatcher
104 stdcall KiUserCallbackDispatcher(ptr ptr long) ChpeAutoKiUserCallbackDispatcher
106 stdcall KiUserExceptionDispatcherWorker(ptr ptr) ChpeAutoKiUserExceptionDispatcherWorker
109 stdcall -version=0x600+ LdrAddLoadAsDataTable(ptr wstr long ptr) ChpeStubLdrAddLoadAsDataTable
111 stdcall LdrDisableThreadCalloutsForDll(ptr) ChpeAutoLdrDisableThreadCalloutsForDll
113 stdcall LdrEnumerateLoadedModules(long ptr ptr) ChpeAutoLdrEnumerateLoadedModules
116 stdcall  LdrFindResourceEx_U(ptr ptr ptr ptr ptr) ChpeStubLdrFindResourceEx_U
118 stdcall LdrFlushAlternateResourceModules() ChpeAutoLdrFlushAlternateResourceModules
121 stdcall -version=0x600+ LdrGetFailureData() ChpeStubLdrGetFailureData
122 stdcall -version=0x600+ LdrGetFileNameFromLoadAsDataTable(ptr ptr) ChpeStubLdrGetFileNameFromLoadAsDataTable
123 stdcall -version=0x600+ LdrGetKnownDllSectionHandle(wstr long ptr) ChpeStubLdrGetKnownDllSectionHandle
125 stdcall -version=0x600+ LdrGetProcedureAddressEx(ptr ptr long ptr long) ChpeAutoLdrGetProcedureAddressEx
126 stdcall  LdrHotPatchRoutine(ptr) ChpeStubLdrHotPatchRoutine
127 stdcall LdrInitShimEngineDynamic(ptr) ChpeAutoLdrInitShimEngineDynamic
128 stdcall LdrInitializeThunk(long long long long) ChpeAutoLdrInitializeThunk
129 stdcall LdrLoadAlternateResourceModule(ptr ptr) ChpeAutoLdrLoadAlternateResourceModule
130 stdcall -version=0x600+ LdrLoadAlternateResourceModuleEx(long long ptr ptr long) ChpeStubLdrLoadAlternateResourceModuleEx
132 stdcall LdrLockLoaderLock(long ptr ptr) ChpeAutoLdrLockLoaderLock
133 stdcall LdrOpenImageFileOptionsKey(ptr long ptr) ChpeAutoLdrOpenImageFileOptionsKey
134 stdcall -version=0x600+ LdrProcessInitializationComplete() ChpeStubLdrProcessInitializationComplete
135 stdcall LdrProcessRelocationBlock(ptr long ptr long) ChpeAutoLdrProcessRelocationBlock
136 stdcall LdrQueryImageFileExecutionOptions(ptr str long ptr long ptr) ChpeAutoLdrQueryImageFileExecutionOptions
137 stdcall LdrQueryImageFileExecutionOptionsEx(ptr ptr long ptr long ptr long) ChpeAutoLdrQueryImageFileExecutionOptionsEx
138 stdcall LdrQueryImageFileKeyOption(ptr ptr long ptr long ptr) ChpeAutoLdrQueryImageFileKeyOption
139 stdcall -version=0x600+ LdrQueryModuleServiceTags(ptr ptr ptr) ChpeStubLdrQueryModuleServiceTags
141 stdcall -version=0x600+ LdrRegisterDllNotification(long ptr ptr ptr) ChpeAutoLdrRegisterDllNotification
142 stdcall -version=0x600+ LdrRemoveLoadAsDataTable(ptr ptr ptr long) ChpeStubLdrRemoveLoadAsDataTable
145 stdcall -version=0x600+ LdrResFindResource(ptr long long long ptr ptr ptr ptr long) ChpeAutoLdrResFindResource
146 stdcall -version=0x600+ LdrResFindResourceDirectory(ptr long long ptr ptr ptr long ptr) ChpeAutoLdrResFindResourceDirectory
147 stdcall -version=0x600+ LdrResRelease(ptr ptr long long) ChpeStubLdrResRelease
148 stdcall -version=0x600+ LdrResSearchResource(wstr wstr long long long ptr long long) ChpeStubLdrResSearchResource
149 stdcall LdrSetAppCompatDllRedirectionCallback(long ptr ptr) ChpeAutoLdrSetAppCompatDllRedirectionCallback
150 stdcall LdrSetDllManifestProber(ptr) ChpeAutoLdrSetDllManifestProber
151 stdcall -version=0x600+ LdrSetMUICacheType(long) ChpeStubLdrSetMUICacheType
152 stdcall LdrShutdownProcess() ChpeAutoLdrShutdownProcess
153 stdcall LdrShutdownThread() ChpeAutoLdrShutdownThread
154 extern LdrSystemDllInitBlock ntdll.LdrSystemDllInitBlock
155 stdcall LdrUnloadAlternateResourceModule(ptr) ChpeAutoLdrUnloadAlternateResourceModule
156 stdcall -version=0x600+ LdrUnloadAlternateResourceModuleEx(long long) ChpeStubLdrUnloadAlternateResourceModuleEx
158 stdcall LdrUnlockLoaderLock(long ptr) ChpeAutoLdrUnlockLoaderLock
159 stdcall -version=0x600+ LdrUnregisterDllNotification(ptr) ChpeAutoLdrUnregisterDllNotification
160 stdcall LdrVerifyImageMatchesChecksum(ptr long long long) ChpeAutoLdrVerifyImageMatchesChecksum
161 stdcall -version=0x600+ LdrVerifyImageMatchesChecksumEx(ptr ptr) ChpeStubLdrVerifyImageMatchesChecksumEx
162 stdcall -version=0x600+ LdrpResGetMappingSize(long ptr long long) ChpeStubLdrpResGetMappingSize
163 stdcall -version=0x600+ LdrpResGetRCConfig(long long ptr long long) ChpeStubLdrpResGetRCConfig
164 stdcall -version=0x600+ LdrpResGetResourceDirectory(long long long ptr ptr) ChpeStubLdrpResGetResourceDirectory
165 stdcall -version=0x600+ MD4Final(ptr) ChpeAutoMD4Final
166 stdcall -version=0x600+ MD4Init(ptr) ChpeAutoMD4Init
167 stdcall -version=0x600+ MD4Update(ptr ptr long) ChpeAutoMD4Update
168 stdcall -version=0x600+ MD5Final(ptr) ChpeAutoMD5Final
169 stdcall -version=0x600+ MD5Init(ptr) ChpeAutoMD5Init
170 stdcall -version=0x600+ MD5Update(ptr ptr long) ChpeAutoMD5Update
171 extern NlsAnsiCodePage ntdll.NlsAnsiCodePage
172 extern NlsMbCodePageTag ntdll.NlsMbCodePageTag
173 extern NlsMbOemCodePageTag ntdll.NlsMbOemCodePageTag
174 stdcall NtAcceptConnectPort(ptr long ptr long long ptr) ChpeAutoNtAcceptConnectPort
175 stdcall NtAccessCheck(ptr long long ptr ptr ptr ptr ptr) ChpeAutoNtAccessCheck
176 stdcall NtAccessCheckAndAuditAlarm(ptr long ptr ptr ptr long ptr long ptr ptr ptr) ChpeAutoNtAccessCheckAndAuditAlarm
177 stdcall NtAccessCheckByType(ptr ptr ptr long ptr long ptr ptr long ptr ptr) ChpeAutoNtAccessCheckByType
178 stdcall NtAccessCheckByTypeAndAuditAlarm(ptr ptr ptr ptr ptr ptr long long long ptr long ptr long ptr ptr ptr) ChpeAutoNtAccessCheckByTypeAndAuditAlarm
179 stdcall NtAccessCheckByTypeResultList(ptr ptr ptr long ptr long ptr ptr long ptr ptr) ChpeAutoNtAccessCheckByTypeResultList
180 stdcall NtAccessCheckByTypeResultListAndAuditAlarm(ptr ptr ptr ptr ptr ptr long long long ptr long ptr long ptr ptr ptr) ChpeAutoNtAccessCheckByTypeResultListAndAuditAlarm
181 stdcall NtAccessCheckByTypeResultListAndAuditAlarmByHandle(ptr ptr ptr ptr ptr ptr ptr long long long ptr long ptr long ptr ptr ptr) ChpeAutoNtAccessCheckByTypeResultListAndAuditAlarmByHandle
182 stdcall -version=0x600+ NtAcquireCMFViewOwnership(ptr ptr long) ChpeStubNtAcquireCMFViewOwnership
183 stdcall NtAddAtom(ptr long ptr) ChpeAutoNtAddAtom
184 stdcall NtAddBootEntry(ptr long) ChpeAutoNtAddBootEntry
185 stdcall NtAddDriverEntry(ptr long) ChpeAutoNtAddDriverEntry
186 stdcall NtAdjustGroupsToken(long long ptr long ptr ptr) ChpeAutoNtAdjustGroupsToken
188 stdcall -version=0xA00+ NtAlertMultipleThreadByThreadId(ptr long ptr ptr) ChpeAutoNtAlertMultipleThreadByThreadId
189 stdcall NtAlertResumeThread(long ptr) ChpeAutoNtAlertResumeThread
190 stdcall NtAlertThread(long) ChpeAutoNtAlertThread
192 stdcall NtAllocateLocallyUniqueId(ptr) ChpeAutoNtAllocateLocallyUniqueId
193 stdcall -version=0x600+ NtAllocateReserveObject(ptr ptr long) ChpeAutoNtAllocateReserveObject
194 stdcall NtAllocateUserPhysicalPages(ptr ptr ptr) ChpeAutoNtAllocateUserPhysicalPages
195 stdcall NtAllocateUuids(ptr ptr ptr ptr) ChpeAutoNtAllocateUuids
198 stdcall -version=0x600+ NtAlpcAcceptConnectPort(ptr ptr long ptr ptr ptr ptr ptr long) ChpeAutoNtAlpcAcceptConnectPort
199 stdcall -version=0x600+ NtAlpcCancelMessage(ptr long ptr) ChpeAutoNtAlpcCancelMessage
200 stdcall -version=0x600+ NtAlpcConnectPort(ptr ptr ptr ptr long ptr ptr ptr ptr ptr ptr) ChpeAutoNtAlpcConnectPort
201 stdcall -version=0x602+ NtAlpcConnectPortEx(ptr ptr ptr ptr long ptr ptr ptr ptr ptr ptr) ChpeAutoNtAlpcConnectPortEx
202 stdcall -version=0x600+ NtAlpcCreatePort(ptr ptr ptr) ChpeAutoNtAlpcCreatePort
203 stdcall -version=0x600+ NtAlpcCreatePortSection(ptr long ptr long ptr ptr) ChpeAutoNtAlpcCreatePortSection
204 stdcall -version=0x600+ NtAlpcCreateResourceReserve(ptr long long ptr) ChpeAutoNtAlpcCreateResourceReserve
205 stdcall -version=0x600+ NtAlpcCreateSectionView(ptr long ptr) ChpeAutoNtAlpcCreateSectionView
206 stdcall -version=0x600+ NtAlpcCreateSecurityContext(ptr long ptr) ChpeAutoNtAlpcCreateSecurityContext
207 stdcall -version=0x600+ NtAlpcDeletePortSection(ptr long ptr) ChpeAutoNtAlpcDeletePortSection
208 stdcall -version=0x600+ NtAlpcDeleteResourceReserve(ptr long long) ChpeAutoNtAlpcDeleteResourceReserve
209 stdcall -version=0x600+ NtAlpcDeleteSectionView(ptr long ptr) ChpeAutoNtAlpcDeleteSectionView
210 stdcall -version=0x600+ NtAlpcDeleteSecurityContext(ptr long ptr) ChpeAutoNtAlpcDeleteSecurityContext
211 stdcall -version=0x600+ NtAlpcDisconnectPort(ptr long) ChpeAutoNtAlpcDisconnectPort
212 stdcall -version=0x600+ NtAlpcImpersonateClientOfPort(ptr ptr ptr) ChpeAutoNtAlpcImpersonateClientOfPort
213 stdcall -version=0xA00+ NtAlpcImpersonateClientContainerOfPort(ptr ptr long) ChpeAutoNtAlpcImpersonateClientContainerOfPort
214 stdcall -version=0x600+ NtAlpcOpenSenderProcess(ptr ptr ptr long long ptr) ChpeAutoNtAlpcOpenSenderProcess
215 stdcall -version=0x600+ NtAlpcOpenSenderThread(ptr ptr ptr long long ptr) ChpeAutoNtAlpcOpenSenderThread
216 stdcall -version=0x600+ NtAlpcQueryInformation(ptr long ptr long ptr) ChpeAutoNtAlpcQueryInformation
217 stdcall -version=0x600+ NtAlpcQueryInformationMessage(ptr ptr long ptr long ptr) ChpeAutoNtAlpcQueryInformationMessage
218 stdcall -version=0x600+ NtAlpcRevokeSecurityContext(ptr long ptr) ChpeAutoNtAlpcRevokeSecurityContext
219 stdcall -version=0x600+ NtAlpcSendWaitReceivePort(ptr long ptr ptr ptr ptr ptr ptr) ChpeAutoNtAlpcSendWaitReceivePort
220 stdcall -version=0x600+ NtAlpcSetInformation(ptr long ptr long) ChpeAutoNtAlpcSetInformation
221 stdcall NtApphelpCacheControl(long ptr) ChpeAutoNtApphelpCacheControl
222 stdcall NtAreMappedFilesTheSame(ptr ptr) ChpeAutoNtAreMappedFilesTheSame
223 stdcall NtAssignProcessToJobObject(long long) ChpeAutoNtAssignProcessToJobObject
224 stdcall NtCallbackReturn(ptr long long) ChpeAutoNtCallbackReturn
225 stdcall NtCancelDeviceWakeupRequest(ptr) ChpeAutoNtCancelDeviceWakeupRequest
226 stdcall NtCancelIoFile(long ptr) ChpeAutoNtCancelIoFile
227 stdcall -version=0x600+ NtCancelIoFileEx(long ptr ptr) ChpeAutoNtCancelIoFileEx
228 stdcall -version=0x600+ NtCancelSynchronousIoFile(long ptr ptr) ChpeAutoNtCancelSynchronousIoFile
229 stdcall NtCancelTimer(long ptr) ChpeAutoNtCancelTimer
230 stdcall NtClearEvent(long) ChpeAutoNtClearEvent
232 stdcall NtCloseObjectAuditAlarm(ptr ptr long) ChpeAutoNtCloseObjectAuditAlarm
233 stdcall -version=0x600+ NtCompareObjects(ptr ptr) ChpeAutoNtCompareObjects
234 stdcall -version=0x600+ NtCommitComplete(ptr ptr) ChpeAutoNtCommitComplete
235 stdcall -version=0x600+ NtCommitEnlistment(ptr ptr) ChpeAutoNtCommitEnlistment
236 stdcall -version=0x600+ NtCommitTransaction(ptr long) ChpeAutoNtCommitTransaction
237 stdcall NtCompactKeys(long ptr) ChpeAutoNtCompactKeys
238 stdcall NtCompareTokens(ptr ptr ptr) ChpeAutoNtCompareTokens
239 stdcall NtCompleteConnectPort(ptr) ChpeAutoNtCompleteConnectPort
240 stdcall NtCompressKey(ptr) ChpeAutoNtCompressKey
241 stdcall NtConnectPort(ptr ptr ptr ptr ptr ptr ptr ptr) ChpeAutoNtConnectPort
244 stdcall -version=0xA00+ NtConvertBetweenAuxiliaryCounterAndPerformanceCounter(long ptr ptr ptr) ChpeAutoNtConvertBetweenAuxiliaryCounterAndPerformanceCounter
245 stdcall NtCreateDebugObject(ptr long ptr long) ChpeAutoNtCreateDebugObject
246 stdcall NtCreateDirectoryObject(long long long) ChpeAutoNtCreateDirectoryObject
247 stdcall -version=0x600+ NtCreateEnlistment(ptr long ptr ptr ptr long long ptr) ChpeAutoNtCreateEnlistment
248 stdcall NtCreateEvent(long long long long long) ChpeAutoNtCreateEvent
249 stdcall NtCreateEventPair(ptr long ptr) ChpeAutoNtCreateEventPair
251 stdcall NtCreateIoCompletion(ptr long ptr long) ChpeAutoNtCreateIoCompletion
255 stdcall NtCreateJobObject(ptr long ptr) ChpeAutoNtCreateJobObject
256 stdcall NtCreateJobSet(long ptr long) ChpeAutoNtCreateJobSet
258 stdcall -version=0x600+ NtCreateKeyTransacted(ptr long ptr long ptr long ptr ptr) ChpeAutoNtCreateKeyTransacted
259 stdcall NtCreateKeyedEvent(ptr long ptr long) ChpeAutoNtCreateKeyedEvent
260 stdcall NtCreateMailslotFile(long long long long long long long long) ChpeAutoNtCreateMailslotFile
261 stdcall NtCreateMutant(ptr long ptr long) ChpeAutoNtCreateMutant
263 stdcall NtCreatePagingFile(ptr ptr ptr long) ChpeAutoNtCreatePagingFile
264 stdcall NtCreatePort(ptr ptr long long ptr) ChpeAutoNtCreatePort
265 stdcall -version=0x600+ NtCreatePrivateNamespace(ptr long ptr ptr) ChpeAutoNtCreatePrivateNamespace
266 stdcall NtCreateProcess(ptr long ptr ptr long ptr ptr ptr) ChpeAutoNtCreateProcess
267 stdcall NtCreateProcessEx(ptr long ptr ptr long ptr ptr ptr long) ChpeAutoNtCreateProcessEx
268 stdcall NtCreateProfile(ptr ptr ptr long long ptr long long long) ChpeAutoNtCreateProfile
269 stdcall -version=0x600+ NtCreateResourceManager(ptr long ptr ptr ptr long ptr) ChpeAutoNtCreateResourceManager
270 stdcall NtCreateSection(ptr long ptr ptr long long ptr) ChpeAutoNtCreateSection
271 stdcall NtCreateSectionEx(ptr long ptr ptr long long ptr ptr long) ChpeAutoNtCreateSectionEx
272 stdcall NtCreateSemaphore(ptr long ptr long long) ChpeAutoNtCreateSemaphore
273 stdcall NtCreateSymbolicLinkObject(ptr long ptr ptr) ChpeAutoNtCreateSymbolicLinkObject
275 stdcall -version=0x600+ NtCreateThreadEx(ptr long ptr ptr ptr ptr long long long long ptr) ChpeAutoNtCreateThreadEx
276 stdcall NtCreateTimer(ptr long ptr long) ChpeAutoNtCreateTimer
277 stdcall NtCreateToken(ptr long ptr long ptr ptr ptr ptr ptr ptr ptr ptr ptr) ChpeAutoNtCreateToken
278 stdcall -version=0x600+ NtCreateTransaction(ptr long ptr ptr ptr long long long ptr ptr) ChpeAutoNtCreateTransaction
279 stdcall -version=0x600+ NtCreateTransactionManager(ptr long ptr ptr long long) ChpeAutoNtCreateTransactionManager
280 stdcall -version=0x600+ NtCreateUserProcess(ptr ptr long long ptr ptr long long ptr ptr ptr) ChpeAutoNtCreateUserProcess
281 stdcall NtCreateWaitablePort(ptr ptr long long long) ChpeAutoNtCreateWaitablePort
282 stdcall -version=0x602+ NtCreateWnfStateName(ptr long long long ptr long ptr) ChpeAutoNtCreateWnfStateName
283 stdcall -version=0x600+ NtCreateWorkerFactory(ptr long long long long long long long long long) ChpeStubNtCreateWorkerFactory
284 stdcall NtDebugActiveProcess(ptr ptr) ChpeAutoNtDebugActiveProcess
285 stdcall NtDebugContinue(ptr ptr long) ChpeAutoNtDebugContinue
286 stdcall NtDelayExecution(long ptr) ChpeAutoNtDelayExecution
287 stdcall NtDeleteAtom(long) ChpeAutoNtDeleteAtom
288 stdcall NtDeleteBootEntry(long) ChpeAutoNtDeleteBootEntry
289 stdcall NtDeleteDriverEntry(long) ChpeAutoNtDeleteDriverEntry
290 stdcall NtDeleteFile(ptr) ChpeAutoNtDeleteFile
292 stdcall NtDeleteObjectAuditAlarm(ptr ptr long) ChpeAutoNtDeleteObjectAuditAlarm
293 stdcall -version=0x600+ NtDeletePrivateNamespace(ptr) ChpeAutoNtDeletePrivateNamespace
295 stdcall -version=0x602+ NtDeleteWnfStateData(ptr ptr) ChpeAutoNtDeleteWnfStateData
296 stdcall -version=0x602+ NtDeleteWnfStateName(ptr) ChpeAutoNtDeleteWnfStateName
298 stdcall NtDisplayString(ptr) ChpeAutoNtDisplayString
299 stdcall NtDuplicateObject(long long long ptr long long long) ChpeAutoNtDuplicateObject
300 stdcall NtDuplicateToken(long long long long long long) ChpeAutoNtDuplicateToken
301 stdcall NtEnumerateBootEntries(ptr ptr) ChpeAutoNtEnumerateBootEntries
302 stdcall NtEnumerateDriverEntries(ptr ptr) ChpeAutoNtEnumerateDriverEntries
304 stdcall NtEnumerateSystemEnvironmentValuesEx(long ptr long) ChpeAutoNtEnumerateSystemEnvironmentValuesEx
305 stdcall -version=0x600+ NtEnumerateTransactionObject(ptr long ptr long ptr) ChpeAutoNtEnumerateTransactionObject
307 stdcall NtExtendSection(ptr ptr) ChpeAutoNtExtendSection
308 stdcall NtFilterToken(ptr long ptr ptr ptr ptr) ChpeAutoNtFilterToken
309 stdcall -version=0x602+ NtCreateLowBoxToken(ptr ptr long ptr ptr long ptr long ptr) ChpeAutoNtCreateLowBoxToken
310 stdcall NtFindAtom(ptr long ptr) ChpeAutoNtFindAtom
311 stdcall NtFlushBuffersFile(long ptr) ChpeAutoNtFlushBuffersFile
312 stdcall NtFlushBuffersFileEx(long long ptr long ptr) ChpeAutoNtFlushBuffersFileEx
313 stdcall -version=0x600+ NtFlushInstallUILanguage(long long) ChpeStubNtFlushInstallUILanguage
317 stdcall NtFlushVirtualMemory(ptr ptr ptr ptr) ChpeAutoNtFlushVirtualMemory
318 stdcall NtFlushWriteBuffer() ChpeAutoNtFlushWriteBuffer
319 stdcall NtFreeUserPhysicalPages(ptr ptr ptr) ChpeAutoNtFreeUserPhysicalPages
321 stdcall -version=0x600+ NtFreezeRegistry(long) ChpeStubNtFreezeRegistry
322 stdcall -version=0x600+ NtFreezeTransactions(ptr ptr) ChpeStubNtFreezeTransactions
325 stdcall NtGetCurrentProcessorNumber() ChpeAutoNtGetCurrentProcessorNumber
326 stdcall -version=0xA00+ NtGetCurrentProcessorNumberEx(ptr) ChpeAutoNtGetCurrentProcessorNumberEx
327 stdcall NtGetDevicePowerState(ptr ptr) ChpeAutoNtGetDevicePowerState
328 stdcall -version=0x600+ NtGetMUIRegistryInfo(long ptr ptr) ChpeStubNtGetMUIRegistryInfo
329 stdcall -version=0x600+ NtGetNextProcess(long long long long ptr) ChpeStubNtGetNextProcess
331 stdcall -version=0x600+ NtGetNlsSectionPtr(long long ptr ptr ptr) ChpeAutoNtGetNlsSectionPtr
332 stdcall -version=0x600+ NtGetNotificationResourceManager(ptr ptr long ptr ptr long ptr) ChpeAutoNtGetNotificationResourceManager
333 stdcall NtGetPlugPlayEvent(long long ptr long) ChpeAutoNtGetPlugPlayEvent
334 stdcall NtGetTickCount() ChpeAutoNtGetTickCount
335 stdcall NtGetWriteWatch(long long ptr long ptr ptr ptr) ChpeAutoNtGetWriteWatch
336 stdcall NtImpersonateAnonymousToken(ptr) ChpeAutoNtImpersonateAnonymousToken
337 stdcall NtImpersonateClientOfPort(ptr ptr) ChpeAutoNtImpersonateClientOfPort
338 stdcall NtImpersonateThread(ptr ptr ptr) ChpeAutoNtImpersonateThread
339 stdcall -version=0x600+ NtInitializeNlsFiles(ptr ptr ptr) ChpeAutoNtInitializeNlsFiles
340 stdcall NtInitializeRegistry(long) ChpeAutoNtInitializeRegistry
341 stdcall NtInitiatePowerAction(long long long long) ChpeAutoNtInitiatePowerAction
342 stdcall NtIsProcessInJob(long long) ChpeAutoNtIsProcessInJob
343 stdcall NtIsSystemResumeAutomatic() ChpeAutoNtIsSystemResumeAutomatic
344 stdcall -version=0x600+ NtIsUILanguageComitted() ChpeStubNtIsUILanguageComitted
345 stdcall NtListenPort(ptr ptr) ChpeAutoNtListenPort
346 stdcall NtLoadDriver(ptr) ChpeAutoNtLoadDriver
347 stdcall NtLoadKey2(ptr ptr long) ChpeAutoNtLoadKey2
348 stdcall NtLoadKey(ptr ptr) ChpeAutoNtLoadKey
349 stdcall NtLoadKeyEx(ptr ptr long ptr ptr long ptr ptr) ChpeAutoNtLoadKeyEx
350 stdcall NtLockFile(long long ptr ptr ptr ptr ptr ptr long long) ChpeAutoNtLockFile
351 stdcall NtLockProductActivationKeys(ptr ptr) ChpeAutoNtLockProductActivationKeys
352 stdcall NtLockRegistryKey(ptr) ChpeAutoNtLockRegistryKey
353 stdcall NtLockVirtualMemory(long ptr ptr long) ChpeAutoNtLockVirtualMemory
354 stdcall NtMakePermanentObject(ptr) ChpeAutoNtMakePermanentObject
355 stdcall NtMakeTemporaryObject(long) ChpeAutoNtMakeTemporaryObject
356 stdcall -version=0x600+ NtMapCMFModule(long long ptr ptr ptr ptr) ChpeStubNtMapCMFModule
357 stdcall NtMapUserPhysicalPages(ptr ptr ptr) ChpeAutoNtMapUserPhysicalPages
358 stdcall NtMapUserPhysicalPagesScatter(ptr ptr ptr) ChpeAutoNtMapUserPhysicalPagesScatter
361 stdcall NtModifyBootEntry(ptr) ChpeAutoNtModifyBootEntry
362 stdcall NtModifyDriverEntry(ptr) ChpeAutoNtModifyDriverEntry
363 stdcall NtNotifyChangeDirectoryFile(long long ptr ptr ptr ptr long long long) ChpeAutoNtNotifyChangeDirectoryFile
364 stdcall NtNotifyChangeDirectoryFileEx(long long ptr ptr ptr ptr long long long long) ChpeAutoNtNotifyChangeDirectoryFileEx
367 stdcall NtOpenDirectoryObject(long long long) ChpeAutoNtOpenDirectoryObject
368 stdcall -version=0x600+ NtOpenEnlistment(ptr long ptr ptr ptr) ChpeAutoNtOpenEnlistment
369 stdcall NtOpenEvent(long long long) ChpeAutoNtOpenEvent
370 stdcall NtOpenEventPair(ptr long ptr) ChpeAutoNtOpenEventPair
372 stdcall NtOpenIoCompletion(ptr long ptr) ChpeAutoNtOpenIoCompletion
373 stdcall NtOpenJobObject(ptr long ptr) ChpeAutoNtOpenJobObject
375 stdcall -version=0x600+ NtOpenKeyTransacted(ptr long ptr ptr) ChpeAutoNtOpenKeyTransacted
376 stdcall -version=0x600+ NtOpenKeyTransactedEx(ptr long ptr long ptr) ChpeAutoNtOpenKeyTransactedEx
377 stdcall NtOpenKeyedEvent(ptr long ptr) ChpeAutoNtOpenKeyedEvent
378 stdcall NtOpenMutant(ptr long ptr) ChpeAutoNtOpenMutant
379 stdcall NtOpenObjectAuditAlarm(ptr ptr ptr ptr ptr ptr long long ptr long long ptr) ChpeAutoNtOpenObjectAuditAlarm
380 stdcall -version=0x600+ NtOpenPrivateNamespace(ptr long ptr ptr) ChpeAutoNtOpenPrivateNamespace
381 stdcall NtOpenProcess(ptr long ptr ptr) ChpeAutoNtOpenProcess
382 stdcall NtOpenProcessToken(long long ptr) ChpeAutoNtOpenProcessToken
383 stdcall NtOpenProcessTokenEx(long long long ptr) ChpeAutoNtOpenProcessTokenEx
384 stdcall -version=0x600+ NtOpenResourceManager(ptr long ptr ptr ptr) ChpeAutoNtOpenResourceManager
385 stdcall NtOpenSection(ptr long ptr) ChpeAutoNtOpenSection
386 stdcall NtOpenSemaphore(long long ptr) ChpeAutoNtOpenSemaphore
387 stdcall -version=0x600+ NtOpenSession(ptr long ptr) ChpeStubNtOpenSession
388 stdcall NtOpenSymbolicLinkObject(ptr long ptr) ChpeAutoNtOpenSymbolicLinkObject
389 stdcall NtOpenThread(ptr long ptr ptr) ChpeAutoNtOpenThread
390 stdcall NtOpenThreadToken(long long long ptr) ChpeAutoNtOpenThreadToken
391 stdcall NtOpenThreadTokenEx(long long long long ptr) ChpeAutoNtOpenThreadTokenEx
392 stdcall NtOpenTimer(ptr long ptr) ChpeAutoNtOpenTimer
393 stdcall -version=0x600+ NtOpenTransaction(ptr long ptr ptr ptr) ChpeAutoNtOpenTransaction
394 stdcall -version=0x600+ NtOpenTransactionManager(ptr long ptr ptr ptr long) ChpeAutoNtOpenTransactionManager
395 stdcall NtPlugPlayControl(ptr ptr long) ChpeAutoNtPlugPlayControl
396 stdcall NtPowerInformation(long ptr long ptr long) ChpeAutoNtPowerInformation
397 stdcall -version=0x600+ NtPrePrepareComplete(ptr ptr) ChpeAutoNtPrePrepareComplete
398 stdcall -version=0x600+ NtPrePrepareEnlistment(ptr ptr) ChpeAutoNtPrePrepareEnlistment
399 stdcall -version=0x600+ NtPrepareComplete(ptr ptr) ChpeAutoNtPrepareComplete
400 stdcall -version=0x600+ NtPrepareEnlistment(ptr ptr) ChpeAutoNtPrepareEnlistment
401 stdcall NtPrivilegeCheck(ptr ptr ptr) ChpeAutoNtPrivilegeCheck
402 stdcall NtPrivilegeObjectAuditAlarm(ptr ptr ptr long ptr long) ChpeAutoNtPrivilegeObjectAuditAlarm
403 stdcall NtPrivilegedServiceAuditAlarm(ptr ptr ptr ptr long) ChpeAutoNtPrivilegedServiceAuditAlarm
404 stdcall -version=0x600+ NtPropagationComplete(ptr long long ptr) ChpeAutoNtPropagationComplete
405 stdcall -version=0x600+ NtPropagationFailed(ptr long long) ChpeAutoNtPropagationFailed
407 stdcall NtPulseEvent(long ptr) ChpeAutoNtPulseEvent
408 stdcall NtQueryAttributesFile(ptr ptr) ChpeAutoNtQueryAttributesFile
409 stdcall NtQueryBootEntryOrder(ptr ptr) ChpeAutoNtQueryBootEntryOrder
410 stdcall NtQueryBootOptions(ptr ptr) ChpeAutoNtQueryBootOptions
411 stdcall NtQueryDebugFilterState(long long) ChpeAutoNtQueryDebugFilterState
412 stdcall NtQueryDefaultLocale(long ptr) ChpeAutoNtQueryDefaultLocale
413 stdcall NtQueryDefaultUILanguage(ptr) ChpeAutoNtQueryDefaultUILanguage
415 stdcall NtQueryDirectoryFileEx(long long ptr ptr ptr ptr long long long ptr) ChpeAutoNtQueryDirectoryFileEx
416 stdcall NtQueryDirectoryObject(long ptr long long long ptr ptr) ChpeAutoNtQueryDirectoryObject
417 stdcall NtQueryDriverEntryOrder(ptr ptr) ChpeAutoNtQueryDriverEntryOrder
418 stdcall NtQueryEaFile(long ptr ptr long long ptr long ptr long) ChpeAutoNtQueryEaFile
419 stdcall NtQueryEvent(long long ptr long ptr) ChpeAutoNtQueryEvent
420 stdcall NtQueryFullAttributesFile(ptr ptr) ChpeAutoNtQueryFullAttributesFile
421 stdcall NtQueryInformationAtom(long long ptr long ptr) ChpeAutoNtQueryInformationAtom
422 stdcall NtQueryInformationByName(ptr ptr ptr long long) ChpeAutoNtQueryInformationByName
423 stdcall -version=0x600+ NtQueryInformationEnlistment(ptr long ptr long ptr) ChpeAutoNtQueryInformationEnlistment
425 stdcall NtQueryInformationJobObject(ptr long ptr long ptr) ChpeAutoNtQueryInformationJobObject
426 stdcall NtQueryInformationPort(ptr long ptr long ptr) ChpeAutoNtQueryInformationPort
428 stdcall -version=0x600+ NtQueryInformationResourceManager(ptr long ptr long ptr) ChpeAutoNtQueryInformationResourceManager
430 stdcall NtQueryInformationToken(ptr long ptr long ptr) ChpeAutoNtQueryInformationToken
431 stdcall -version=0x600+ NtQueryInformationTransaction(ptr long ptr long ptr) ChpeAutoNtQueryInformationTransaction
432 stdcall -version=0x600+ NtQueryInformationTransactionManager(ptr long ptr long ptr) ChpeAutoNtQueryInformationTransactionManager
433 stdcall -version=0x600+ NtQueryInformationWorkerFactory(ptr long ptr long ptr) ChpeStubNtQueryInformationWorkerFactory
434 stdcall NtQueryInstallUILanguage(ptr) ChpeAutoNtQueryInstallUILanguage
435 stdcall NtQueryIntervalProfile(long ptr) ChpeAutoNtQueryIntervalProfile
436 stdcall NtQueryIoCompletion(long long ptr long ptr) ChpeAutoNtQueryIoCompletion
438 stdcall -version=0x600+ NtQueryLicenseValue(ptr ptr ptr long ptr) ChpeAutoNtQueryLicenseValue
439 stdcall NtQueryMultipleValueKey(long ptr long ptr long ptr) ChpeAutoNtQueryMultipleValueKey
440 stdcall NtQueryMutant(long long ptr long ptr) ChpeAutoNtQueryMutant
442 stdcall NtQueryOpenSubKeys(ptr ptr) ChpeAutoNtQueryOpenSubKeys
443 stdcall NtQueryOpenSubKeysEx(ptr long ptr ptr) ChpeAutoNtQueryOpenSubKeysEx
444 stdcall NtQueryPerformanceCounter(ptr ptr) ChpeAutoNtQueryPerformanceCounter
445 stdcall NtQueryPortInformationProcess() ChpeAutoNtQueryPortInformationProcess
446 stdcall NtQueryQuotaInformationFile(ptr ptr ptr long long ptr long ptr long) ChpeAutoNtQueryQuotaInformationFile
447 stdcall NtQuerySection(long long long long long) ChpeAutoNtQuerySection
448 stdcall NtQuerySecurityObject(long long long long long) ChpeAutoNtQuerySecurityObject
449 stdcall NtQuerySemaphore(long long ptr long ptr) ChpeAutoNtQuerySemaphore
450 stdcall NtQuerySymbolicLinkObject(long ptr ptr) ChpeAutoNtQuerySymbolicLinkObject
451 stdcall NtQuerySystemEnvironmentValue(ptr ptr long ptr) ChpeAutoNtQuerySystemEnvironmentValue
452 stdcall NtQuerySystemEnvironmentValueEx(ptr ptr ptr ptr ptr) ChpeAutoNtQuerySystemEnvironmentValueEx
454 stdcall -version=0x601+ NtQuerySystemInformationEx(long ptr long ptr long ptr) ChpeAutoNtQuerySystemInformationEx
455 stdcall NtQuerySystemTime(ptr) ChpeAutoNtQuerySystemTime
456 stdcall NtQueryTimer(ptr long ptr long ptr) ChpeAutoNtQueryTimer
457 stdcall NtQueryTimerResolution(long long long) ChpeAutoNtQueryTimerResolution
461 stdcall -version=0x602+ NtQueryWnfStateData(ptr ptr ptr ptr ptr ptr) ChpeAutoNtQueryWnfStateData
462 stdcall -version=0x602+ NtQueryWnfStateNameInformation(ptr long ptr ptr long) ChpeAutoNtQueryWnfStateNameInformation
463 stdcall NtQueueApcThread(long ptr long long long) ChpeAutoNtQueueApcThread
464 stdcall -version=0x601+ NtQueueApcThreadEx(long long ptr long long long) ChpeAutoNtQueueApcThreadEx
465 stdcall -version=0xA00+ NtQueueApcThreadEx2(long long long ptr long long long) ChpeAutoNtQueueApcThreadEx2
467 stdcall NtRaiseHardError(long long long ptr long ptr) ChpeAutoNtRaiseHardError
469 stdcall NtReadFileScatter(long long ptr ptr ptr ptr long ptr ptr) ChpeAutoNtReadFileScatter
470 stdcall -version=0x600+ NtReadOnlyEnlistment(ptr ptr) ChpeAutoNtReadOnlyEnlistment
471 stdcall NtReadRequestData(ptr ptr long ptr long ptr) ChpeAutoNtReadRequestData
473 stdcall -version=0x600+ NtRecoverEnlistment(ptr ptr) ChpeAutoNtRecoverEnlistment
474 stdcall -version=0x600+ NtRecoverResourceManager(ptr) ChpeAutoNtRecoverResourceManager
475 stdcall -version=0x600+ NtRecoverTransactionManager(ptr) ChpeAutoNtRecoverTransactionManager
476 stdcall -version=0x600+ NtRegisterProtocolAddressInformation(ptr ptr long ptr long) ChpeAutoNtRegisterProtocolAddressInformation
477 stdcall NtRegisterThreadTerminatePort(ptr) ChpeAutoNtRegisterThreadTerminatePort
478 stdcall -version=0x600+ NtReleaseCMFViewOwnership() ChpeStubNtReleaseCMFViewOwnership
479 stdcall NtReleaseKeyedEvent(ptr ptr long ptr) ChpeAutoNtReleaseKeyedEvent
480 stdcall NtReleaseMutant(long ptr) ChpeAutoNtReleaseMutant
481 stdcall NtReleaseSemaphore(long long ptr) ChpeAutoNtReleaseSemaphore
482 stdcall -version=0x600+ NtReleaseWorkerFactoryWorker(ptr) ChpeStubNtReleaseWorkerFactoryWorker
483 stdcall NtRemoveIoCompletion(ptr ptr ptr ptr ptr) ChpeAutoNtRemoveIoCompletion
484 stdcall -version=0x600+ NtRemoveIoCompletionEx(ptr ptr long ptr ptr long) ChpeAutoNtRemoveIoCompletionEx
485 stdcall NtRemoveProcessDebug(ptr ptr) ChpeAutoNtRemoveProcessDebug
486 stdcall NtRenameKey(ptr ptr) ChpeAutoNtRenameKey
487 stdcall -version=0x600+ NtRenameTransactionManager(ptr ptr) ChpeAutoNtRenameTransactionManager
488 stdcall NtReplaceKey(ptr long ptr) ChpeAutoNtReplaceKey
489 stdcall -version=0x600+ NtReplacePartitionUnit(wstr wstr long) ChpeStubNtReplacePartitionUnit
490 stdcall NtReplyPort(ptr ptr) ChpeAutoNtReplyPort
491 stdcall NtReplyWaitReceivePort(ptr ptr ptr ptr) ChpeAutoNtReplyWaitReceivePort
492 stdcall NtReplyWaitReceivePortEx(ptr ptr ptr ptr ptr) ChpeAutoNtReplyWaitReceivePortEx
493 stdcall NtReplyWaitReplyPort(ptr ptr) ChpeAutoNtReplyWaitReplyPort
494 stdcall NtRequestDeviceWakeup(ptr) ChpeAutoNtRequestDeviceWakeup
495 stdcall NtRequestPort(ptr ptr) ChpeAutoNtRequestPort
496 stdcall NtRequestWaitReplyPort(ptr ptr ptr) ChpeAutoNtRequestWaitReplyPort
497 stdcall NtRequestWakeupLatency(long) ChpeAutoNtRequestWakeupLatency
498 stdcall NtResetEvent(long ptr) ChpeAutoNtResetEvent
499 stdcall NtResetWriteWatch(long ptr long) ChpeAutoNtResetWriteWatch
500 stdcall NtRestoreKey(long long long) ChpeAutoNtRestoreKey
501 stdcall NtResumeProcess(ptr) ChpeAutoNtResumeProcess
503 stdcall -version=0x600+ NtRollbackComplete(ptr ptr) ChpeAutoNtRollbackComplete
504 stdcall -version=0x600+ NtRollbackEnlistment(ptr ptr) ChpeAutoNtRollbackEnlistment
505 stdcall -version=0x600+ NtRollbackTransaction(ptr long) ChpeAutoNtRollbackTransaction
506 stdcall -version=0x600+ NtRollforwardTransactionManager(ptr ptr) ChpeAutoNtRollforwardTransactionManager
507 stdcall NtSaveKey(long long) ChpeAutoNtSaveKey
508 stdcall NtSaveKeyEx(ptr ptr long) ChpeAutoNtSaveKeyEx
509 stdcall NtSaveMergedKeys(ptr ptr ptr) ChpeAutoNtSaveMergedKeys
510 stdcall NtSecureConnectPort(ptr ptr ptr ptr ptr ptr ptr ptr ptr) ChpeAutoNtSecureConnectPort
511 stdcall NtSetBootEntryOrder(ptr ptr) ChpeAutoNtSetBootEntryOrder
512 stdcall NtSetBootOptions(ptr long) ChpeAutoNtSetBootOptions
514 stdcall NtSetDebugFilterState(long long long) ChpeAutoNtSetDebugFilterState
515 stdcall NtSetDefaultHardErrorPort(ptr) ChpeAutoNtSetDefaultHardErrorPort
516 stdcall NtSetDefaultLocale(long long) ChpeAutoNtSetDefaultLocale
517 stdcall NtSetDefaultUILanguage(long) ChpeAutoNtSetDefaultUILanguage
518 stdcall NtSetDriverEntryOrder(ptr ptr) ChpeAutoNtSetDriverEntryOrder
519 stdcall NtSetEaFile(long ptr ptr long) ChpeAutoNtSetEaFile
520 stdcall NtSetEvent(long long) ChpeAutoNtSetEvent
521 stdcall NtSetEventBoostPriority(ptr) ChpeAutoNtSetEventBoostPriority
522 stdcall NtSetHighEventPair(ptr) ChpeAutoNtSetHighEventPair
523 stdcall NtSetHighWaitLowEventPair(ptr) ChpeAutoNtSetHighWaitLowEventPair
524 stdcall NtSetInformationDebugObject(ptr long ptr long ptr) ChpeAutoNtSetInformationDebugObject
525 stdcall -version=0x600+ NtSetInformationEnlistment(ptr long ptr long) ChpeAutoNtSetInformationEnlistment
526 stdcall NtSetInformationFile(ptr ptr ptr long long) ChpeAutoNtSetInformationFile
527 stdcall NtSetInformationJobObject(ptr long ptr long) ChpeAutoNtSetInformationJobObject
528 stdcall NtSetInformationKey(ptr long ptr long) ChpeAutoNtSetInformationKey
529 stdcall NtSetInformationObject(ptr long ptr long) ChpeAutoNtSetInformationObject
530 stdcall NtSetInformationProcess(ptr long ptr long) ChpeAutoNtSetInformationProcess
531 stdcall -version=0x600+ NtSetInformationResourceManager(ptr long ptr long) ChpeAutoNtSetInformationResourceManager
532 stdcall NtSetInformationThread(ptr long ptr long) ChpeAutoNtSetInformationThread
533 stdcall NtSetInformationVirtualMemory(ptr long ptr ptr ptr long) ChpeAutoNtSetInformationVirtualMemory
534 stdcall NtSetInformationToken(ptr long ptr long) ChpeAutoNtSetInformationToken
535 stdcall -version=0x600+ NtSetInformationTransaction(ptr long ptr long) ChpeAutoNtSetInformationTransaction
536 stdcall -version=0x600+ NtSetInformationTransactionManager(ptr long ptr long) ChpeAutoNtSetInformationTransactionManager
537 stdcall -version=0x600+ NtSetInformationWorkerFactory(ptr long ptr long) ChpeStubNtSetInformationWorkerFactory
538 stdcall NtSetIntervalProfile(long long) ChpeAutoNtSetIntervalProfile
539 stdcall NtSetIoCompletion(ptr long ptr long long) ChpeAutoNtSetIoCompletion
540 stdcall -version=0x600+ NtSetIoCompletionEx(ptr ptr long long long long) ChpeAutoNtSetIoCompletionEx
541 stdcall NtSetLdtEntries(long int64 long int64) ChpeAutoNtSetLdtEntries
542 stdcall NtSetLowEventPair(ptr) ChpeAutoNtSetLowEventPair
543 stdcall NtSetLowWaitHighEventPair(ptr) ChpeAutoNtSetLowWaitHighEventPair
544 stdcall NtSetQuotaInformationFile(ptr ptr ptr long) ChpeAutoNtSetQuotaInformationFile
545 stdcall NtSetSecurityObject(long long ptr) ChpeAutoNtSetSecurityObject
546 stdcall NtSetSystemEnvironmentValue(ptr ptr) ChpeAutoNtSetSystemEnvironmentValue
547 stdcall NtSetSystemEnvironmentValueEx(ptr ptr ptr ptr ptr) ChpeAutoNtSetSystemEnvironmentValueEx
548 stdcall NtSetSystemInformation(long ptr long) ChpeAutoNtSetSystemInformation
549 stdcall NtSetSystemPowerState(long long long) ChpeAutoNtSetSystemPowerState
550 stdcall NtSetSystemTime(ptr ptr) ChpeAutoNtSetSystemTime
551 stdcall NtSetThreadExecutionState(long ptr) ChpeAutoNtSetThreadExecutionState
553 stdcall NtSetTimerResolution(long long ptr) ChpeAutoNtSetTimerResolution
554 stdcall NtSetUuidSeed(ptr) ChpeAutoNtSetUuidSeed
556 stdcall NtSetVolumeInformationFile(long ptr ptr long long) ChpeAutoNtSetVolumeInformationFile
557 stdcall NtShutdownSystem(long) ChpeAutoNtShutdownSystem
558 stdcall -version=0x600+ NtShutdownWorkerFactory(ptr ptr) ChpeStubNtShutdownWorkerFactory
559 stdcall NtSignalAndWaitForSingleObject(long long long ptr) ChpeAutoNtSignalAndWaitForSingleObject
560 stdcall -version=0x600+ NtSinglePhaseReject(ptr ptr) ChpeAutoNtSinglePhaseReject
561 stdcall NtStartProfile(ptr) ChpeAutoNtStartProfile
562 stdcall NtStopProfile(ptr) ChpeAutoNtStopProfile
563 stdcall -version=0x602+ NtSubscribeWnfStateChange(ptr long long ptr) ChpeAutoNtSubscribeWnfStateChange
566 stdcall NtSystemDebugControl(long ptr long ptr long ptr) ChpeAutoNtSystemDebugControl
567 stdcall NtTerminateJobObject(ptr long) ChpeAutoNtTerminateJobObject
570 stdcall NtTestAlert() ChpeAutoNtTestAlert
571 stdcall -version=0x600+ NtThawRegistry() ChpeStubNtThawRegistry
572 stdcall -version=0x600+ NtThawTransactions() ChpeStubNtThawTransactions
573 stdcall -version=0x600+ NtTraceControl(long ptr long ptr long ptr) ChpeAutoNtTraceControl
574 stdcall NtTraceEvent(ptr long long ptr) ChpeAutoNtTraceEvent
575 stdcall NtTranslateFilePath(ptr long ptr long) ChpeAutoNtTranslateFilePath
576 stdcall NtUnloadDriver(ptr) ChpeAutoNtUnloadDriver
577 stdcall NtUnloadKey2(ptr long) ChpeAutoNtUnloadKey2
578 stdcall NtUnloadKey(long) ChpeAutoNtUnloadKey
579 stdcall NtUnloadKeyEx(ptr ptr) ChpeAutoNtUnloadKeyEx
580 stdcall NtUnlockFile(long ptr ptr ptr ptr) ChpeAutoNtUnlockFile
581 stdcall NtUnlockVirtualMemory(long ptr ptr long) ChpeAutoNtUnlockVirtualMemory
584 stdcall -version=0x602+ NtUnsubscribeWnfStateChange(ptr) ChpeAutoNtUnsubscribeWnfStateChange
585 stdcall -version=0x602+ NtUpdateWnfStateData(ptr ptr long ptr ptr long long) ChpeAutoNtUpdateWnfStateData
586 stdcall NtVdmControl(long ptr) ChpeAutoNtVdmControl
587 stdcall NtWaitForDebugEvent(ptr long ptr ptr) ChpeAutoNtWaitForDebugEvent
588 stdcall NtWaitForKeyedEvent(ptr ptr long ptr) ChpeAutoNtWaitForKeyedEvent
590 stdcall NtWaitForMultipleObjects32(long ptr long long ptr) ChpeAutoNtWaitForMultipleObjects32
591 stdcall NtWaitForMultipleObjects(long ptr long long ptr) ChpeAutoNtWaitForMultipleObjects
592 stdcall NtWaitForSingleObject(long long long) ChpeAutoNtWaitForSingleObject
593 stub -version=0x600+ NtWaitForWorkViaWorkerFactory
594 stdcall NtWaitHighEventPair(ptr) ChpeAutoNtWaitHighEventPair
595 stdcall NtWaitLowEventPair(ptr) ChpeAutoNtWaitLowEventPair
596 stdcall -version=0x600+ NtWorkerFactoryWorkerReady(long) ChpeStubNtWorkerFactoryWorkerReady
598 stdcall NtWriteFileGather(long long ptr ptr ptr ptr long ptr ptr) ChpeAutoNtWriteFileGather
599 stdcall NtWriteRequestData(ptr ptr long ptr long ptr) ChpeAutoNtWriteRequestData
601 stdcall NtYieldExecution() ChpeAutoNtYieldExecution
602 stdcall -version=0x600+ NtdllDefWindowProc_A(long long long long) ChpeStubNtdllDefWindowProc_A
603 stdcall -version=0x600+ NtdllDefWindowProc_W(long long long long) ChpeStubNtdllDefWindowProc_W
604 stdcall -version=0x600+ NtdllDialogWndProc_A(long long long long) ChpeStubNtdllDialogWndProc_A
605 stdcall -version=0x600+ NtdllDialogWndProc_W(long long long long) ChpeStubNtdllDialogWndProc_W
606 stdcall PfxFindPrefix(ptr ptr) ChpeAutoPfxFindPrefix
607 stdcall PfxInitialize(ptr) ChpeAutoPfxInitialize
608 stdcall PfxInsertPrefix(ptr ptr ptr) ChpeAutoPfxInsertPrefix
609 stdcall PfxRemovePrefix(ptr ptr) ChpeAutoPfxRemovePrefix
610 stdcall RtlAbortRXact(ptr) ChpeAutoRtlAbortRXact
611 stdcall RtlAbsoluteToSelfRelativeSD(ptr ptr ptr) ChpeAutoRtlAbsoluteToSelfRelativeSD
612 stdcall RtlAcquirePebLock() ChpeAutoRtlAcquirePebLock
614 stdcall RtlAcquireResourceExclusive(ptr long) ChpeAutoRtlAcquireResourceExclusive
615 stdcall RtlAcquireResourceShared(ptr long) ChpeAutoRtlAcquireResourceShared
618 stdcall RtlActivateActivationContext(long ptr ptr) ChpeAutoRtlActivateActivationContext
619 stdcall RtlActivateActivationContextEx(long ptr ptr ptr) ChpeAutoRtlActivateActivationContextEx
620 stdcall RtlActivateActivationContextUnsafeFast(ptr ptr) ChpeAutoRtlActivateActivationContextUnsafeFast
621 stdcall RtlAddAccessAllowedAce(ptr long long ptr) ChpeAutoRtlAddAccessAllowedAce
622 stdcall RtlAddAccessAllowedAceEx(ptr long long long ptr) ChpeAutoRtlAddAccessAllowedAceEx
623 stdcall RtlAddAccessAllowedObjectAce(ptr long long long ptr ptr ptr) ChpeAutoRtlAddAccessAllowedObjectAce
624 stdcall RtlAddAccessDeniedAce(ptr long long ptr) ChpeAutoRtlAddAccessDeniedAce
625 stdcall RtlAddAccessDeniedAceEx(ptr long long long ptr) ChpeAutoRtlAddAccessDeniedAceEx
626 stdcall RtlAddAccessDeniedObjectAce(ptr long long long ptr ptr ptr) ChpeAutoRtlAddAccessDeniedObjectAce
627 stdcall RtlAddAce(ptr long long ptr long) ChpeAutoRtlAddAce
628 stdcall RtlAddActionToRXact(ptr long ptr long ptr long) ChpeAutoRtlAddActionToRXact
629 stdcall RtlAddAtomToAtomTable(ptr wstr ptr) ChpeAutoRtlAddAtomToAtomTable
630 stdcall RtlAddAttributeActionToRXact(ptr long ptr ptr ptr long ptr long) ChpeAutoRtlAddAttributeActionToRXact
631 stdcall RtlAddAuditAccessAce(ptr long long ptr long long) ChpeAutoRtlAddAuditAccessAce
632 stdcall RtlAddAuditAccessAceEx(ptr long long long ptr long long) ChpeAutoRtlAddAuditAccessAceEx
633 stdcall RtlAddAuditAccessObjectAce(ptr long long long ptr ptr ptr long long) ChpeAutoRtlAddAuditAccessObjectAce
634 stdcall  RtlAddCompoundAce(ptr long long long ptr ptr) ChpeStubRtlAddCompoundAce
637 stdcall -version=0x600+ RtlAddMandatoryAce(ptr long long long long ptr) ChpeAutoRtlAddMandatoryAce
638 stdcall RtlAddRefActivationContext(ptr) ChpeAutoRtlAddRefActivationContext
639 stdcall RtlAddRefMemoryStream(ptr) ChpeAutoRtlAddRefMemoryStream
640 stdcall -version=0x600+ RtlAddSIDToBoundaryDescriptor(ptr ptr) ChpeAutoRtlAddSIDToBoundaryDescriptor
641 stdcall -version=0x600+ RtlAddIntegrityLabelToBoundaryDescriptor(ptr ptr) ChpeAutoRtlAddIntegrityLabelToBoundaryDescriptor
644 stdcall  RtlAddressInSectionTable(ptr ptr long) ChpeStubRtlAddressInSectionTable
645 stdcall RtlAdjustPrivilege(long long long ptr) ChpeAutoRtlAdjustPrivilege
646 stdcall RtlAllocateActivationContextStack(ptr) ChpeAutoRtlAllocateActivationContextStack
647 stdcall RtlAllocateAndInitializeSid(ptr long long long long long long long long long ptr) ChpeAutoRtlAllocateAndInitializeSid
648 stdcall RtlAllocateHandle(ptr ptr) ChpeAutoRtlAllocateHandle
650 stdcall -version=0x600+ RtlAllocateMemoryBlockLookaside(ptr long ptr) ChpeStubRtlAllocateMemoryBlockLookaside
651 stdcall -version=0x600+ RtlAllocateMemoryZone(long long ptr) ChpeStubRtlAllocateMemoryZone
652 stdcall RtlAnsiCharToUnicodeChar(ptr) ChpeAutoRtlAnsiCharToUnicodeChar
653 stdcall RtlAnsiStringToUnicodeSize(ptr) ChpeAutoRtlAnsiStringToUnicodeSize
654 stdcall RtlAnsiStringToUnicodeString(ptr ptr long) ChpeAutoRtlAnsiStringToUnicodeString
655 stdcall RtlAppendAsciizToString(ptr str) ChpeAutoRtlAppendAsciizToString
656 stdcall  RtlAppendPathElement(ptr ptr ptr) ChpeStubRtlAppendPathElement
657 stdcall RtlAppendStringToString(ptr ptr) ChpeAutoRtlAppendStringToString
658 stdcall RtlAppendUnicodeStringToString(ptr ptr) ChpeAutoRtlAppendUnicodeStringToString
659 stdcall RtlAppendUnicodeToString(ptr wstr) ChpeAutoRtlAppendUnicodeToString
660 stdcall RtlApplicationVerifierStop(ptr str ptr str ptr str ptr str ptr str) ChpeAutoRtlApplicationVerifierStop
661 stdcall RtlApplyRXact(ptr) ChpeAutoRtlApplyRXact
662 stdcall RtlApplyRXactNoFlush(ptr) ChpeAutoRtlApplyRXactNoFlush
663 stdcall RtlAreAllAccessesGranted(long long) ChpeAutoRtlAreAllAccessesGranted
664 stdcall RtlAreAnyAccessesGranted(long long) ChpeAutoRtlAreAnyAccessesGranted
665 stdcall RtlAreBitsClear(ptr long long) ChpeAutoRtlAreBitsClear
666 stdcall RtlAreBitsSet(ptr long long) ChpeAutoRtlAreBitsSet
667 stdcall RtlAssert(ptr ptr long ptr) ChpeAutoRtlAssert
668 stdcall -version=0x600+ RtlBarrier(ptr long) ChpeStubRtlBarrier
669 stdcall -version=0x600+ RtlBarrierForDelete(long long) ChpeStubRtlBarrierForDelete
670 stdcall RtlCancelTimer(ptr ptr) ChpeAutoRtlCancelTimer
673 stdcall RtlCharToInteger(ptr long ptr) ChpeAutoRtlCharToInteger
674 stdcall RtlCheckForOrphanedCriticalSections(ptr) ChpeAutoRtlCheckForOrphanedCriticalSections
675 stdcall RtlCheckRegistryKey(long ptr) ChpeAutoRtlCheckRegistryKey
676 stdcall -version=0x600+ RtlCleanUpTEBLangLists() ChpeStubRtlCleanUpTEBLangLists
677 stdcall RtlClearAllBits(ptr) ChpeAutoRtlClearAllBits
678 stdcall RtlClearBits(ptr long long) ChpeAutoRtlClearBits
679 stdcall RtlCloneMemoryStream(ptr ptr) ChpeAutoRtlCloneMemoryStream
680 stdcall -version=0x600+ RtlCloneUserProcess(long long long long long) ChpeStubRtlCloneUserProcess
681 stdcall -version=0x600+ RtlCmDecodeMemIoResource(ptr ptr) ChpeStubRtlCmDecodeMemIoResource
682 stdcall -version=0x600+ RtlCmEncodeMemIoResource(ptr long long long) ChpeStubRtlCmEncodeMemIoResource
683 stdcall -version=0x600+ RtlCommitDebugInfo(ptr long) ChpeStubRtlCommitDebugInfo
684 stdcall RtlCommitMemoryStream(ptr long) ChpeAutoRtlCommitMemoryStream
685 stdcall RtlCompactHeap(long long) ChpeAutoRtlCompactHeap
686 stdcall -version=0x600+ RtlCompareAltitudes(wstr wstr) ChpeStubRtlCompareAltitudes
688 stdcall RtlCompareMemoryUlong(ptr long long) ChpeAutoRtlCompareMemoryUlong
689 stdcall RtlCompareString(ptr ptr long) ChpeAutoRtlCompareString
691 stdcall -version=0x600+ RtlCompareUnicodeStrings(wstr long wstr long long) ChpeAutoRtlCompareUnicodeStrings
692 stdcall -version=0x600+ RtlCompleteProcessCloning(long) ChpeStubRtlCompleteProcessCloning
693 stdcall RtlCompressBuffer(long ptr long ptr long long ptr ptr) ChpeAutoRtlCompressBuffer
694 stdcall RtlComputeCrc32(long ptr long) ChpeAutoRtlComputeCrc32
695 stdcall RtlComputeImportTableHash(ptr ptr long) ChpeAutoRtlComputeImportTableHash
696 stdcall RtlComputePrivatizedDllName_U(ptr ptr ptr) ChpeAutoRtlComputePrivatizedDllName_U
697 stdcall -version=0x600+ RtlConnectToSm(ptr ptr long ptr) ChpeStubRtlConnectToSm
698 stdcall RtlConsoleMultiByteToUnicodeN(ptr long ptr ptr long ptr) ChpeAutoRtlConsoleMultiByteToUnicodeN
699 stdcall RtlConvertExclusiveToShared(ptr) ChpeAutoRtlConvertExclusiveToShared
700 stdcall -version=0x600+ RtlConvertLCIDToString(long long long ptr long) ChpeAutoRtlConvertLCIDToString
701 stdcall RtlConvertSharedToExclusive(ptr) ChpeAutoRtlConvertSharedToExclusive
702 stdcall RtlConvertSidToUnicodeString(ptr ptr long) ChpeAutoRtlConvertSidToUnicodeString
703 stdcall RtlConvertToAutoInheritSecurityObject(ptr ptr ptr ptr long ptr) ChpeAutoRtlConvertToAutoInheritSecurityObject
704 stdcall RtlConvertUiListToApiList(ptr ptr long) ChpeAutoRtlConvertUiListToApiList
705 stdcall RtlCopyLuid(ptr ptr) ChpeAutoRtlCopyLuid
706 stdcall RtlCopyLuidAndAttributesArray(long ptr ptr) ChpeAutoRtlCopyLuidAndAttributesArray
707 stdcall RtlCopyMappedMemory(ptr ptr long) ChpeAutoRtlCopyMappedMemory
708 stdcall -version=0xA00+ RtlCopyContext(ptr long ptr) ChpeAutoRtlCopyContext
709 stdcall -version=0x600+ RtlCopyExtendedContext(ptr long ptr) ChpeAutoRtlCopyExtendedContext
710 cdecl -version=0x600+ RtlCopyMemory(ptr ptr long) ChpeAutoRtlCopyMemory
711 stdcall -version=0x600+ RtlCopyMemoryNonTemporal(ptr ptr long) ChpeStubRtlCopyMemoryNonTemporal
712 stdcall RtlCopyMemoryStreamTo(ptr ptr int64 ptr ptr) ChpeAutoRtlCopyMemoryStreamTo
713 stdcall RtlCopyOutOfProcessMemoryStreamTo(ptr ptr int64 ptr ptr) ChpeAutoRtlCopyOutOfProcessMemoryStreamTo
714 stdcall RtlCopySecurityDescriptor(ptr ptr) ChpeAutoRtlCopySecurityDescriptor
715 stdcall RtlCopySid(long ptr ptr) ChpeAutoRtlCopySid
716 stdcall RtlCopySidAndAttributesArray(long ptr long ptr ptr ptr ptr) ChpeAutoRtlCopySidAndAttributesArray
717 stdcall RtlCopyString(ptr ptr) ChpeAutoRtlCopyString
719 stdcall RtlCreateAcl(ptr long long) ChpeAutoRtlCreateAcl
720 stdcall RtlCreateActivationContext(long ptr long ptr ptr ptr) ChpeAutoRtlCreateActivationContext
721 stdcall RtlCreateAndSetSD(ptr long ptr ptr ptr) ChpeAutoRtlCreateAndSetSD
722 stdcall RtlContractHashTable(ptr) ChpeAutoRtlContractHashTable
723 stdcall RtlCreateAtomTable(long ptr) ChpeAutoRtlCreateAtomTable
724 stdcall RtlCreateHashTable(ptr long long) ChpeAutoRtlCreateHashTable
725 stdcall RtlCreateBootStatusDataFile() ChpeAutoRtlCreateBootStatusDataFile
726 stdcall -version=0x600+ RtlCreateBoundaryDescriptor(ptr long) ChpeAutoRtlCreateBoundaryDescriptor
727 stdcall RtlCreateEnvironment(long ptr) ChpeAutoRtlCreateEnvironment
728 stdcall -version=0x600+ RtlCreateEnvironmentEx(ptr ptr long) ChpeStubRtlCreateEnvironmentEx
729 stdcall RtlCreateHeap(long ptr long long ptr ptr) ChpeAutoRtlCreateHeap
730 stdcall -version=0x600+ RtlCreateMemoryBlockLookaside(ptr long long long long) ChpeStubRtlCreateMemoryBlockLookaside
731 stdcall -version=0x600+ RtlCreateMemoryZone(ptr long long) ChpeStubRtlCreateMemoryZone
732 stdcall RtlCreateProcessParameters(ptr ptr ptr ptr ptr ptr ptr ptr ptr ptr) ChpeAutoRtlCreateProcessParameters
733 stdcall -version=0x600+ RtlCreateProcessParametersEx(ptr wstr ptr wstr wstr ptr wstr wstr ptr ptr long) ChpeAutoRtlCreateProcessParametersEx
734 stdcall RtlCreateQueryDebugBuffer(long long) ChpeAutoRtlCreateQueryDebugBuffer
735 stdcall RtlCreateRegistryKey(long wstr) ChpeAutoRtlCreateRegistryKey
736 stdcall RtlCreateSecurityDescriptor(ptr long) ChpeAutoRtlCreateSecurityDescriptor
737 stdcall RtlCreateServiceSid(ptr ptr ptr) ChpeAutoRtlCreateServiceSid
738 stdcall -version=0xA00+ RtlDeriveCapabilitySidsFromName(ptr ptr ptr) ChpeAutoRtlDeriveCapabilitySidsFromName
739 stdcall -version=0x602+ RtlIsCapabilitySid(ptr) ChpeAutoRtlIsCapabilitySid
740 stdcall -version=0x602+ RtlGetAppContainerSidType(ptr ptr) ChpeAutoRtlGetAppContainerSidType
741 stdcall -version=0x602+ RtlGetAppContainerParent(ptr ptr) ChpeAutoRtlGetAppContainerParent
742 stdcall -version=0x602+ RtlIsParentOfChildAppContainer(ptr ptr) ChpeAutoRtlIsParentOfChildAppContainer
743 stdcall -version=0x602+ RtlCheckTokenMembershipEx(ptr ptr long ptr) ChpeAutoRtlCheckTokenMembershipEx
744 stdcall RtlCreateSystemVolumeInformationFolder(ptr) ChpeAutoRtlCreateSystemVolumeInformationFolder
745 stdcall RtlCreateTagHeap(ptr long wstr wstr) ChpeAutoRtlCreateTagHeap
746 stdcall RtlCreateTimer(ptr ptr ptr ptr long long long) ChpeAutoRtlCreateTimer
747 stdcall RtlCreateTimerQueue(ptr) ChpeAutoRtlCreateTimerQueue
748 stdcall RtlCreateUnicodeString(ptr wstr) ChpeAutoRtlCreateUnicodeString
749 stdcall RtlCreateUnicodeStringFromAsciiz(ptr str) ChpeAutoRtlCreateUnicodeStringFromAsciiz
750 stdcall RtlCreateUserProcess(ptr long ptr ptr ptr ptr long ptr ptr ptr) ChpeAutoRtlCreateUserProcess
751 stdcall RtlCreateUserSecurityObject(ptr long ptr ptr long ptr ptr) ChpeAutoRtlCreateUserSecurityObject
752 stdcall -version=0x600+ RtlCreateUserStack(long long long long long ptr) ChpeAutoRtlCreateUserStack
754 stdcall -version=0x600+ RtlCultureNameToLCID(ptr ptr) ChpeAutoRtlCultureNameToLCID
755 stdcall RtlCustomCPToUnicodeN(ptr wstr long ptr str long) ChpeAutoRtlCustomCPToUnicodeN
756 stdcall RtlCutoverTimeToSystemTime(ptr ptr ptr long) ChpeAutoRtlCutoverTimeToSystemTime
757 stdcall -version=0x600+ RtlDeCommitDebugInfo(long long long) ChpeStubRtlDeCommitDebugInfo
758 stdcall RtlDeNormalizeProcessParams(ptr) ChpeAutoRtlDeNormalizeProcessParams
759 stdcall RtlDeactivateActivationContext(long long) ChpeAutoRtlDeactivateActivationContext
760 stdcall RtlDeactivateActivationContextUnsafeFast(ptr) ChpeAutoRtlDeactivateActivationContextUnsafeFast
761 stdcall  RtlDebugPrintTimes() ChpeStubRtlDebugPrintTimes
764 stdcall RtlDecompressBuffer(long ptr long ptr long ptr) ChpeAutoRtlDecompressBuffer
766 stdcall RtlDefaultNpAcl(ptr) ChpeAutoRtlDefaultNpAcl
767 stdcall RtlDelete(ptr) ChpeAutoRtlDelete
768 stdcall RtlDeleteAce(ptr long) ChpeAutoRtlDeleteAce
769 stdcall RtlDeleteAtomFromAtomTable(ptr long) ChpeAutoRtlDeleteAtomFromAtomTable
770 stdcall RtlDeleteHashTable(ptr) ChpeAutoRtlDeleteHashTable
771 stdcall -version=0x600+ RtlDeleteBarrier(long) ChpeStubRtlDeleteBarrier
772 stdcall -version=0x600+ RtlDeleteBoundaryDescriptor(ptr) ChpeAutoRtlDeleteBoundaryDescriptor
774 stdcall RtlDeleteElementGenericTable(ptr ptr) ChpeAutoRtlDeleteElementGenericTable
775 stdcall RtlDeleteElementGenericTableAvl(ptr ptr) ChpeAutoRtlDeleteElementGenericTableAvl
778 stdcall RtlDeleteNoSplay(ptr ptr) ChpeAutoRtlDeleteNoSplay
779 stdcall RtlDeleteRegistryValue(long ptr ptr) ChpeAutoRtlDeleteRegistryValue
780 stdcall RtlDeleteResource(ptr) ChpeAutoRtlDeleteResource
781 stdcall RtlDeleteSecurityObject(ptr) ChpeAutoRtlDeleteSecurityObject
782 stdcall RtlDeleteTimer(ptr ptr ptr) ChpeAutoRtlDeleteTimer
783 stdcall RtlDeleteTimerQueue(ptr) ChpeAutoRtlDeleteTimerQueue
784 stdcall RtlDeleteTimerQueueEx(ptr ptr) ChpeAutoRtlDeleteTimerQueueEx
785 stdcall -version=0x600+ RtlDeregisterSecureMemoryCacheCallback(ptr) ChpeStubRtlDeregisterSecureMemoryCacheCallback
786 stdcall RtlDeregisterWait(ptr) ChpeAutoRtlDeregisterWait
787 stdcall RtlDeregisterWaitEx(ptr ptr) ChpeAutoRtlDeregisterWaitEx
788 stdcall RtlDestroyAtomTable(ptr) ChpeAutoRtlDestroyAtomTable
789 stdcall RtlDestroyEnvironment(ptr) ChpeAutoRtlDestroyEnvironment
790 stdcall RtlDestroyHandleTable(ptr) ChpeAutoRtlDestroyHandleTable
791 stdcall RtlDestroyHeap(long) ChpeAutoRtlDestroyHeap
792 stdcall -version=0x600+ RtlDestroyMemoryBlockLookaside(long) ChpeStubRtlDestroyMemoryBlockLookaside
793 stdcall -version=0x600+ RtlDestroyMemoryZone(long) ChpeStubRtlDestroyMemoryZone
794 stdcall RtlDestroyProcessParameters(ptr) ChpeAutoRtlDestroyProcessParameters
795 stdcall RtlDestroyQueryDebugBuffer(ptr) ChpeAutoRtlDestroyQueryDebugBuffer
796 stdcall RtlDetermineDosPathNameType_U(wstr) ChpeAutoRtlDetermineDosPathNameType_U
797 stdcall RtlDllShutdownInProgress() ChpeAutoRtlDllShutdownInProgress
798 stdcall RtlDnsHostNameToComputerName(ptr ptr long) ChpeAutoRtlDnsHostNameToComputerName
799 stdcall RtlDoesFileExists_U(wstr) ChpeAutoRtlDoesFileExists_U
800 stdcall RtlDosApplyFileIsolationRedirection_Ustr(long ptr ptr ptr ptr ptr ptr ptr ptr) ChpeAutoRtlDosApplyFileIsolationRedirection_Ustr
801 stdcall RtlDosPathNameToNtPathName_U(wstr ptr ptr ptr) ChpeAutoRtlDosPathNameToNtPathName_U
802 stdcall RtlDosPathNameToNtPathName_U_WithStatus(wstr ptr ptr ptr) ChpeAutoRtlDosPathNameToNtPathName_U_WithStatus
803 stdcall RtlDosPathNameToRelativeNtPathName_U(ptr ptr ptr ptr) ChpeAutoRtlDosPathNameToRelativeNtPathName_U
804 stdcall RtlDosPathNameToRelativeNtPathName_U_WithStatus(wstr ptr ptr ptr) ChpeAutoRtlDosPathNameToRelativeNtPathName_U_WithStatus
805 stdcall RtlDosSearchPath_U(wstr wstr wstr long ptr ptr) ChpeAutoRtlDosSearchPath_U
806 stdcall RtlDosSearchPath_Ustr(long ptr ptr ptr ptr ptr ptr ptr ptr) ChpeAutoRtlDosSearchPath_Ustr
807 stdcall RtlDowncaseUnicodeChar(long) ChpeAutoRtlDowncaseUnicodeChar
808 stdcall RtlDowncaseUnicodeString(ptr ptr long) ChpeAutoRtlDowncaseUnicodeString
809 stdcall RtlDumpResource(ptr) ChpeAutoRtlDumpResource
810 stdcall RtlDuplicateUnicodeString(long ptr ptr) ChpeAutoRtlDuplicateUnicodeString
811 stdcall RtlEmptyAtomTable(ptr long) ChpeAutoRtlEmptyAtomTable
812 stdcall RtlEndEnumerationHashTable(ptr ptr) ChpeAutoRtlEndEnumerationHashTable
813 stdcall RtlEndWeakEnumerationHashTable(ptr ptr) ChpeAutoRtlEndWeakEnumerationHashTable
814 stdcall  RtlEnableEarlyCriticalSectionEventCreation() ChpeStubRtlEnableEarlyCriticalSectionEventCreation
818 stdcall RtlEnumProcessHeaps(ptr ptr) ChpeAutoRtlEnumProcessHeaps
819 stdcall RtlEnumerateEntryHashTable(ptr ptr) ChpeAutoRtlEnumerateEntryHashTable
820 stdcall RtlEnumerateGenericTable(ptr long) ChpeAutoRtlEnumerateGenericTable
821 stdcall RtlEnumerateGenericTableAvl(ptr long) ChpeAutoRtlEnumerateGenericTableAvl
822 stdcall RtlEnumerateGenericTableLikeADirectory(ptr ptr ptr long ptr ptr ptr) ChpeAutoRtlEnumerateGenericTableLikeADirectory
823 stdcall RtlEnumerateGenericTableWithoutSplaying(ptr ptr) ChpeAutoRtlEnumerateGenericTableWithoutSplaying
824 stdcall RtlEnumerateGenericTableWithoutSplayingAvl(ptr ptr) ChpeAutoRtlEnumerateGenericTableWithoutSplayingAvl
825 stdcall RtlEqualComputerName(ptr ptr) ChpeAutoRtlEqualComputerName
826 stdcall RtlEqualDomainName(ptr ptr) ChpeAutoRtlEqualDomainName
827 stdcall RtlEqualLuid(ptr ptr) ChpeAutoRtlEqualLuid
828 stdcall RtlExpandHashTable(ptr) ChpeAutoRtlExpandHashTable
829 stdcall RtlEqualPrefixSid(ptr ptr) ChpeAutoRtlEqualPrefixSid
830 stdcall RtlEqualSid(long long) ChpeAutoRtlEqualSid
831 stdcall RtlEqualString(ptr ptr long) ChpeAutoRtlEqualString
832 stdcall RtlEqualUnicodeString(ptr ptr long) ChpeAutoRtlEqualUnicodeString
833 stdcall RtlEraseUnicodeString(ptr) ChpeAutoRtlEraseUnicodeString
836 stdcall RtlWow64GetCurrentCpuArea(ptr ptr ptr) ChpeAutoRtlWow64GetCurrentCpuArea
838 stdcall ChpeMarkEcCodeRange(ptr long) ChpeAutoChpeMarkEcCodeRange
839 stdcall ChpeCanContinueToGuest() ChpeAutoChpeCanContinueToGuest
840 stdcall ChpeContinueToGuest(ptr) ChpeAutoChpeContinueToGuest
841 stdcall ChpeContinueToGuestEx(ptr long) ChpeAutoChpeContinueToGuestEx
842 stdcall ProcessPendingCrossProcessEmulatorWork() ChpeAutoProcessPendingCrossProcessEmulatorWork
843 stdcall ChpeIsProcessorFeaturePresent(long) ChpeAutoChpeIsProcessorFeaturePresent
844 stdcall RtlWow64PopAllCrossProcessWorkFromWorkList(ptr ptr) ChpeAutoRtlWow64PopAllCrossProcessWorkFromWorkList
845 stdcall RtlWow64PushCrossProcessWorkOntoFreeList(ptr ptr) ChpeAutoRtlWow64PushCrossProcessWorkOntoFreeList
848 stdcall -version=0x600+ RtlLocateLegacyContext(ptr ptr) ChpeAutoRtlLocateLegacyContext
851 stdcall -version=0x601+ RtlQueryUnbiasedInterruptTime(ptr) ChpeAutoRtlQueryUnbiasedInterruptTime
854 stdcall RtlExitUserProcess(long) ChpeAutoRtlExitUserProcess
856 stdcall -version=0x600+ RtlExpandEnvironmentStrings(long ptr long ptr long ptr) ChpeAutoRtlExpandEnvironmentStrings
857 stdcall RtlExpandEnvironmentStrings_U(ptr ptr ptr ptr) ChpeAutoRtlExpandEnvironmentStrings_U
858 stdcall -version=0x600+ RtlExtendMemoryBlockLookaside(long) ChpeStubRtlExtendMemoryBlockLookaside
859 stdcall -version=0x600+ RtlExtendMemoryZone(long long) ChpeStubRtlExtendMemoryZone
861 stdcall RtlFillMemoryUlong(ptr long long) ChpeAutoRtlFillMemoryUlong
862 stdcall RtlFinalReleaseOutOfProcessMemoryStream(ptr) ChpeAutoRtlFinalReleaseOutOfProcessMemoryStream
863 stdcall -version=0x600+ RtlFindAceByType(long long ptr) ChpeStubRtlFindAceByType
864 stdcall RtlFindActivationContextSectionGuid(long ptr long ptr ptr) ChpeAutoRtlFindActivationContextSectionGuid
865 stdcall RtlFindActivationContextSectionString(long ptr long ptr ptr) ChpeAutoRtlFindActivationContextSectionString
866 stdcall RtlFindCharInUnicodeString(long ptr ptr ptr) ChpeAutoRtlFindCharInUnicodeString
867 stdcall RtlFindClearBits(ptr long long) ChpeAutoRtlFindClearBits
868 stdcall RtlFindClearBitsAndSet(ptr long long) ChpeAutoRtlFindClearBitsAndSet
869 stdcall RtlFindClearRuns(ptr ptr long long) ChpeAutoRtlFindClearRuns
870 stdcall -version=0x600+ RtlFindClosestEncodableLength(long ptr) ChpeStubRtlFindClosestEncodableLength
872 stdcall RtlFindLastBackwardRunClear(ptr long ptr) ChpeAutoRtlFindLastBackwardRunClear
873 stdcall RtlFindLeastSignificantBit(double) ChpeAutoRtlFindLeastSignificantBit
874 stdcall RtlFindLongestRunClear(ptr long) ChpeAutoRtlFindLongestRunClear
875 stdcall RtlFindMessage(long long long long ptr) ChpeAutoRtlFindMessage
876 stdcall RtlFindMostSignificantBit(double) ChpeAutoRtlFindMostSignificantBit
877 stdcall RtlFindNextForwardRunClear(ptr long ptr) ChpeAutoRtlFindNextForwardRunClear
878 stdcall RtlFindSetBits(ptr long long) ChpeAutoRtlFindSetBits
879 stdcall RtlFindSetBitsAndClear(ptr long long) ChpeAutoRtlFindSetBitsAndClear
880 stdcall RtlFirstEntrySList(ptr) ChpeAutoRtlFirstEntrySList
881 stdcall RtlFirstFreeAce(ptr ptr) ChpeAutoRtlFirstFreeAce
886 stdcall RtlFlushSecureMemoryCache(ptr ptr) ChpeAutoRtlFlushSecureMemoryCache
888 stdcall RtlFormatMessage(ptr long long long long ptr ptr long ptr) ChpeAutoRtlFormatMessage
889 stdcall RtlFormatMessageEx(ptr long long long long ptr ptr long ptr long) ChpeAutoRtlFormatMessageEx
890 stdcall RtlFreeActivationContextStack(ptr) ChpeAutoRtlFreeActivationContextStack
891 stdcall RtlFreeAnsiString(long) ChpeAutoRtlFreeAnsiString
892 stdcall RtlFreeHandle(ptr ptr) ChpeAutoRtlFreeHandle
894 stdcall -version=0x600+ RtlFreeMemoryBlockLookaside(long long) ChpeStubRtlFreeMemoryBlockLookaside
895 stdcall RtlFreeOemString(ptr) ChpeAutoRtlFreeOemString
896 stdcall RtlFreeSid(long) ChpeAutoRtlFreeSid
897 stdcall RtlFreeThreadActivationContextStack() ChpeAutoRtlFreeThreadActivationContextStack
899 stdcall -version=0x600+ RtlFreeUserStack(ptr) ChpeAutoRtlFreeUserStack
900 stdcall RtlGUIDFromString(ptr ptr) ChpeAutoRtlGUIDFromString
902 stdcall RtlGetAce(ptr long ptr) ChpeAutoRtlGetAce
903 stdcall RtlGetActiveActivationContext(ptr) ChpeAutoRtlGetActiveActivationContext
904 stdcall RtlGetCallersAddress(ptr ptr) ChpeAutoRtlGetCallersAddress
905 stdcall RtlGetCompressionWorkSpaceSize(long ptr ptr) ChpeAutoRtlGetCompressionWorkSpaceSize
906 stdcall RtlGetControlSecurityDescriptor(ptr ptr ptr) ChpeAutoRtlGetControlSecurityDescriptor
907 stdcall RtlGetCriticalSectionRecursionCount(ptr) ChpeAutoRtlGetCriticalSectionRecursionCount
908 stdcall RtlGetCurrentDirectory_U(long ptr) ChpeAutoRtlGetCurrentDirectory_U
912 stdcall -version=0x600+ RtlGetCurrentTransaction() ChpeStubRtlGetCurrentTransaction
913 stdcall RtlGetDaclSecurityDescriptor(ptr ptr ptr ptr) ChpeAutoRtlGetDaclSecurityDescriptor
914 stdcall -version=0xA00+ RtlGetDeviceFamilyInfoEnum(ptr ptr ptr) ChpeAutoRtlGetDeviceFamilyInfoEnum
915 stdcall -version=0x600+ RtlGetEnabledExtendedFeatures(int64) ChpeAutoRtlGetEnabledExtendedFeatures
916 stdcall RtlGetElementGenericTable(ptr long) ChpeAutoRtlGetElementGenericTable
917 stdcall RtlGetElementGenericTableAvl(ptr long) ChpeAutoRtlGetElementGenericTableAvl
918 stdcall -version=0x600+ RtlGetExtendedContextLength(long ptr) ChpeAutoRtlGetExtendedContextLength
919 stdcall -version=0x600+ RtlGetExtendedContextLength2(long ptr int64) ChpeAutoRtlGetExtendedContextLength2
920 stdcall -version=0x600+ -ret64 RtlGetExtendedFeaturesMask(ptr) ChpeAutoRtlGetExtendedFeaturesMask
921 stdcall -version=0x600+ RtlGetFileMUIPath(long long ptr ptr long long ptr) ChpeStubRtlGetFileMUIPath
922 stdcall RtlGetFrame() ChpeAutoRtlGetFrame
923 stdcall RtlGetFullPathName_U(wstr long ptr ptr) ChpeAutoRtlGetFullPathName_U
924 stdcall RtlGetFullPathName_UstrEx(ptr ptr ptr ptr ptr ptr ptr ptr) ChpeAutoRtlGetFullPathName_UstrEx
926 stdcall RtlGetGroupSecurityDescriptor(ptr ptr ptr) ChpeAutoRtlGetGroupSecurityDescriptor
927 stdcall -version=0x600+ RtlGetIntegerAtom(wstr ptr) ChpeAutoRtlGetIntegerAtom
930 stdcall -version=0x600+ RtlGetLocaleFileMappingAddress(ptr ptr ptr) ChpeAutoRtlGetLocaleFileMappingAddress
932 stdcall RtlGetLengthWithoutTrailingPathSeperators(long ptr ptr) ChpeAutoRtlGetLengthWithoutTrailingPathSeperators
933 stdcall RtlGetLongestNtPathLength() ChpeAutoRtlGetLongestNtPathLength
935 stdcall RtlGetNtGlobalFlags() ChpeAutoRtlGetNtGlobalFlags
936 stdcall RtlGetNtProductType(ptr) ChpeAutoRtlGetNtProductType
937 stdcall RtlGetNtVersionNumbers(ptr ptr ptr) ChpeAutoRtlGetNtVersionNumbers
938 stdcall RtlGetOwnerSecurityDescriptor(ptr ptr ptr) ChpeAutoRtlGetOwnerSecurityDescriptor
939 stdcall -version=0x600+ RtlGetParentLocaleName(wstr long long long) ChpeStubRtlGetParentLocaleName
940 stdcall -version=0x600+ RtlGetProcessPreferredUILanguages(long ptr ptr ptr) ChpeAutoRtlGetProcessPreferredUILanguages
943 stdcall RtlGetSaclSecurityDescriptor(ptr ptr ptr ptr) ChpeAutoRtlGetSaclSecurityDescriptor
944 stdcall RtlGetSecurityDescriptorRMControl(ptr ptr) ChpeAutoRtlGetSecurityDescriptorRMControl
945 stdcall RtlGetSetBootStatusData(ptr long long ptr long long) ChpeAutoRtlGetSetBootStatusData
946 stdcall -version=0x600+ RtlGetSystemPreferredUILanguages(long long ptr ptr ptr) ChpeAutoRtlGetSystemPreferredUILanguages
947 stdcall RtlGetThreadErrorMode() ChpeAutoRtlGetThreadErrorMode
948 stdcall -version=0x600+ RtlGetThreadLangIdByIndex(long long ptr ptr) ChpeStubRtlGetThreadLangIdByIndex
949 stdcall -version=0x600+ RtlGetThreadPreferredUILanguages(long ptr ptr ptr) ChpeAutoRtlGetThreadPreferredUILanguages
950 stdcall -version=0x600+ RtlGetUILanguageInfo(long ptr long ptr ptr) ChpeStubRtlGetUILanguageInfo
952 stdcall -version=0x600+ RtlGetUnloadEventTraceEx(ptr ptr ptr) ChpeAutoRtlGetUnloadEventTraceEx
953 stdcall RtlGetUserInfoHeap(ptr long ptr ptr ptr) ChpeAutoRtlGetUserInfoHeap
954 stdcall -version=0x600+ RtlGetUserPreferredUILanguages(long long ptr ptr ptr) ChpeAutoRtlGetUserPreferredUILanguages
956 stdcall RtlHashUnicodeString(ptr long long ptr) ChpeAutoRtlHashUnicodeString
957 stdcall -version=0x600+ RtlHeapTrkInitialize(ptr) ChpeStubRtlHeapTrkInitialize
958 stdcall RtlIdentifierAuthoritySid(ptr) ChpeAutoRtlIdentifierAuthoritySid
959 stdcall -version=0x600+ RtlIdnToAscii(long wstr long ptr ptr) ChpeAutoRtlIdnToAscii
960 stdcall -version=0x600+ RtlIdnToNameprepUnicode(long wstr long ptr ptr) ChpeAutoRtlIdnToNameprepUnicode
961 stdcall -version=0x600+ RtlIdnToUnicode(long wstr long ptr ptr) ChpeAutoRtlIdnToUnicode
962 stdcall RtlImageDirectoryEntryToData(ptr long long ptr) ChpeAutoRtlImageDirectoryEntryToData
963 stdcall RtlImageNtHeader(long) ChpeAutoRtlImageNtHeader
964 stdcall RtlImageNtHeaderEx(long ptr double ptr) ChpeAutoRtlImageNtHeaderEx
965 stdcall RtlImageRvaToSection(ptr long long) ChpeAutoRtlImageRvaToSection
966 stdcall RtlImageRvaToVa(ptr long long ptr) ChpeAutoRtlImageRvaToVa
967 stdcall RtlImpersonateSelf(long) ChpeAutoRtlImpersonateSelf
968 stdcall -version=0x600+ RtlImpersonateSelfEx(long long ptr) ChpeStubRtlImpersonateSelfEx
969 stdcall RtlInitAnsiString(ptr str) ChpeAutoRtlInitAnsiString
970 stdcall RtlInitAnsiStringEx(ptr str) ChpeAutoRtlInitAnsiStringEx
971 stdcall -version=0xA00+ RtlInitUTF8String(ptr str) ChpeAutoRtlInitUTF8String
972 stdcall -version=0x600+ RtlInitBarrier(long long) ChpeStubRtlInitBarrier
973 stdcall RtlInitCodePageTable(ptr ptr) ChpeAutoRtlInitCodePageTable
975 stdcall RtlInitNlsTables(ptr ptr ptr ptr) ChpeAutoRtlInitNlsTables
977 stdcall RtlInitString(ptr str) ChpeAutoRtlInitString
978 stdcall RtlGetNextEntryHashTable(ptr ptr) ChpeAutoRtlGetNextEntryHashTable
979 stdcall RtlInitEnumerationHashTable(ptr ptr) ChpeAutoRtlInitEnumerationHashTable
981 stdcall RtlInitWeakEnumerationHashTable(ptr ptr) ChpeAutoRtlInitWeakEnumerationHashTable
982 stdcall RtlInitUnicodeStringEx(ptr wstr) ChpeAutoRtlInitUnicodeStringEx
983 stdcall  RtlInitializeAtomPackage(ptr) ChpeStubRtlInitializeAtomPackage
984 stdcall RtlInitializeBitMap(ptr long long) ChpeAutoRtlInitializeBitMap
986 stdcall RtlInitializeContext(ptr ptr ptr ptr ptr) ChpeAutoRtlInitializeContext
988 stdcall RtlInitializeCriticalSectionAndSpinCount(ptr long) ChpeAutoRtlInitializeCriticalSectionAndSpinCount
989 stdcall -version=0x600+ RtlInitializeCriticalSectionEx(ptr long long) ChpeAutoRtlInitializeCriticalSectionEx
990 stdcall -version=0x600+ RtlInitializeExtendedContext(ptr long ptr) ChpeAutoRtlInitializeExtendedContext
991 stdcall -version=0x600+ RtlInitializeExtendedContext2(ptr long ptr int64) ChpeAutoRtlInitializeExtendedContext2
992 stdcall RtlInitializeGenericTable(ptr ptr ptr ptr ptr) ChpeAutoRtlInitializeGenericTable
993 stdcall RtlInitializeGenericTableAvl(ptr ptr ptr ptr ptr) ChpeAutoRtlInitializeGenericTableAvl
994 stdcall RtlInitializeHandleTable(long long ptr) ChpeAutoRtlInitializeHandleTable
995 stdcall -version=0x600+ RtlInitializeNtUserPfn(ptr long ptr long ptr long) ChpeAutoRtlInitializeNtUserPfn
996 stdcall RtlInitializeRXact(ptr long ptr) ChpeAutoRtlInitializeRXact
997 stdcall RtlInitializeResource(ptr) ChpeAutoRtlInitializeResource
1000 stdcall RtlInitializeSid(ptr ptr long) ChpeAutoRtlInitializeSid
1001 stdcall RtlInsertElementGenericTable(ptr ptr long ptr) ChpeAutoRtlInsertElementGenericTable
1002 stdcall RtlInsertEntryHashTable(ptr ptr long ptr) ChpeAutoRtlInsertEntryHashTable
1003 stdcall RtlInsertElementGenericTableAvl(ptr ptr long ptr) ChpeAutoRtlInsertElementGenericTableAvl
1004 stdcall RtlInsertElementGenericTableFull(ptr ptr long ptr ptr long) ChpeAutoRtlInsertElementGenericTableFull
1005 stdcall RtlInsertElementGenericTableFullAvl(ptr ptr long ptr ptr long) ChpeAutoRtlInsertElementGenericTableFullAvl
1008 stdcall RtlInt64ToUnicodeString(double long ptr) ChpeAutoRtlInt64ToUnicodeString
1009 stdcall RtlIntegerToChar(long long long ptr) ChpeAutoRtlIntegerToChar
1010 stdcall RtlIntegerToUnicodeString(long long ptr) ChpeAutoRtlIntegerToUnicodeString
1016 stdcall -version=0x600+ RtlIoDecodeMemIoResource(ptr ptr ptr ptr) ChpeStubRtlIoDecodeMemIoResource
1017 stdcall -version=0x600+ RtlIoEncodeMemIoResource(ptr long long long long long) ChpeStubRtlIoEncodeMemIoResource
1018 stdcall RtlIpv4AddressToStringA(ptr ptr) ChpeAutoRtlIpv4AddressToStringA
1019 stdcall RtlIpv4AddressToStringExA(ptr long ptr ptr) ChpeAutoRtlIpv4AddressToStringExA
1020 stdcall RtlIpv4AddressToStringExW(ptr long ptr ptr) ChpeAutoRtlIpv4AddressToStringExW
1021 stdcall RtlIpv4AddressToStringW(ptr ptr) ChpeAutoRtlIpv4AddressToStringW
1022 stdcall RtlIpv4StringToAddressA(str long ptr ptr) ChpeAutoRtlIpv4StringToAddressA
1023 stdcall RtlIpv4StringToAddressExA(str long ptr ptr) ChpeAutoRtlIpv4StringToAddressExA
1024 stdcall RtlIpv4StringToAddressExW(wstr long ptr ptr) ChpeAutoRtlIpv4StringToAddressExW
1025 stdcall RtlIpv4StringToAddressW(wstr long ptr ptr) ChpeAutoRtlIpv4StringToAddressW
1026 stdcall RtlIpv6AddressToStringA(ptr ptr) ChpeAutoRtlIpv6AddressToStringA
1027 stdcall RtlIpv6AddressToStringExA(ptr long long ptr ptr) ChpeAutoRtlIpv6AddressToStringExA
1028 stdcall RtlIpv6AddressToStringExW(ptr long long ptr ptr) ChpeAutoRtlIpv6AddressToStringExW
1029 stdcall RtlIpv6AddressToStringW(ptr ptr) ChpeAutoRtlIpv6AddressToStringW
1030 stdcall RtlIpv6StringToAddressA(str ptr ptr) ChpeAutoRtlIpv6StringToAddressA
1031 stdcall RtlIpv6StringToAddressExA(str ptr ptr ptr) ChpeAutoRtlIpv6StringToAddressExA
1032 stdcall RtlIpv6StringToAddressExW(wstr ptr ptr ptr) ChpeAutoRtlIpv6StringToAddressExW
1033 stdcall RtlIpv6StringToAddressW(wstr ptr ptr) ChpeAutoRtlIpv6StringToAddressW
1034 stdcall RtlIsActivationContextActive(ptr) ChpeAutoRtlIsActivationContextActive
1035 stdcall RtlIsCriticalSectionLocked(ptr) ChpeAutoRtlIsCriticalSectionLocked
1036 stdcall RtlIsCriticalSectionLockedByThread(ptr) ChpeAutoRtlIsCriticalSectionLockedByThread
1037 stdcall -version=0x600+ RtlIsCurrentProcess(ptr) ChpeAutoRtlIsCurrentProcess
1038 stdcall -version=0x600+ RtlIsCurrentThreadAttachExempt() ChpeStubRtlIsCurrentThreadAttachExempt
1039 stdcall RtlIsDosDeviceName_U(wstr) ChpeAutoRtlIsDosDeviceName_U
1040 stdcall RtlIsGenericTableEmpty(ptr) ChpeAutoRtlIsGenericTableEmpty
1041 stdcall RtlIsGenericTableEmptyAvl(ptr) ChpeAutoRtlIsGenericTableEmptyAvl
1042 stdcall RtlIsNameLegalDOS8Dot3(ptr ptr ptr) ChpeAutoRtlIsNameLegalDOS8Dot3
1043 stdcall -version=0x600+ RtlIsNormalizedString(long ptr long ptr) ChpeAutoRtlIsNormalizedString
1045 stdcall RtlIsTextUnicode(ptr long ptr) ChpeAutoRtlIsTextUnicode
1046 stdcall RtlIsThreadWithinLoaderCallout() ChpeAutoRtlIsThreadWithinLoaderCallout
1047 stdcall RtlIsValidHandle(ptr ptr) ChpeAutoRtlIsValidHandle
1048 stdcall RtlIsValidIndexHandle(ptr long ptr) ChpeAutoRtlIsValidIndexHandle
1049 stdcall -version=0x600+ RtlIsValidLocaleName(wstr long) ChpeAutoRtlIsValidLocaleName
1050 stdcall -version=0x600+ RtlLCIDToCultureName(long ptr) ChpeAutoRtlLCIDToCultureName
1051 stdcall RtlLargeIntegerToChar(ptr long long ptr) ChpeAutoRtlLargeIntegerToChar
1052 stdcall -version=0x600+ RtlLcidToLocaleName(long ptr long long) ChpeAutoRtlLcidToLocaleName
1054 stdcall RtlLengthRequiredSid(long) ChpeAutoRtlLengthRequiredSid
1055 stdcall RtlLengthSecurityDescriptor(ptr) ChpeAutoRtlLengthSecurityDescriptor
1056 stdcall RtlLengthSid(ptr) ChpeAutoRtlLengthSid
1057 stdcall RtlLocalTimeToSystemTime(ptr ptr) ChpeAutoRtlLocalTimeToSystemTime
1058 stdcall -version=0x600+ RtlLocaleNameToLcid(wstr ptr long) ChpeAutoRtlLocaleNameToLcid
1059 stdcall RtlLockBootStatusData(ptr) ChpeAutoRtlLockBootStatusData
1060 stdcall -version=0x600+ RtlLockCurrentThread() ChpeStubRtlLockCurrentThread
1061 stdcall RtlLockHeap(long) ChpeAutoRtlLockHeap
1062 stdcall -version=0x600+ RtlLockMemoryBlockLookaside(long) ChpeStubRtlLockMemoryBlockLookaside
1063 stdcall RtlLockMemoryStreamRegion(ptr int64 int64 long) ChpeAutoRtlLockMemoryStreamRegion
1064 stdcall -version=0x600+ RtlLockMemoryZone(long) ChpeStubRtlLockMemoryZone
1065 stdcall -version=0x600+ RtlLockModuleSection(long) ChpeStubRtlLockModuleSection
1067 stdcall  RtlLogStackBackTrace() ChpeStubRtlLogStackBackTrace
1068 stdcall RtlLookupAtomInAtomTable(ptr wstr ptr) ChpeAutoRtlLookupAtomInAtomTable
1069 stdcall RtlLookupElementGenericTable(ptr ptr) ChpeAutoRtlLookupElementGenericTable
1070 stdcall RtlLookupEntryHashTable(ptr long ptr) ChpeAutoRtlLookupEntryHashTable
1071 stdcall RtlRemoveEntryHashTable(ptr ptr ptr) ChpeAutoRtlRemoveEntryHashTable
1072 stdcall RtlLookupElementGenericTableAvl(ptr ptr) ChpeAutoRtlLookupElementGenericTableAvl
1073 stdcall RtlLookupElementGenericTableFull(ptr ptr ptr long) ChpeAutoRtlLookupElementGenericTableFull
1074 stdcall RtlLookupElementGenericTableFullAvl(ptr ptr ptr long) ChpeAutoRtlLookupElementGenericTableFullAvl
1077 stdcall RtlMakeSelfRelativeSD(ptr ptr ptr) ChpeAutoRtlMakeSelfRelativeSD
1078 stdcall RtlMapGenericMask(long ptr) ChpeAutoRtlMapGenericMask
1079 stdcall RtlMapSecurityErrorToNtStatus(long) ChpeAutoRtlMapSecurityErrorToNtStatus
1081 stdcall RtlMultiAppendUnicodeStringBuffer(ptr long ptr) ChpeAutoRtlMultiAppendUnicodeStringBuffer
1082 stdcall RtlMultiByteToUnicodeN(ptr long ptr ptr long) ChpeAutoRtlMultiByteToUnicodeN
1083 stdcall RtlMultiByteToUnicodeSize(ptr str long) ChpeAutoRtlMultiByteToUnicodeSize
1084 stdcall RtlMultipleAllocateHeap(ptr long ptr long ptr) ChpeAutoRtlMultipleAllocateHeap
1085 stdcall RtlMultipleFreeHeap(ptr long long ptr) ChpeAutoRtlMultipleFreeHeap
1086 stdcall RtlNewInstanceSecurityObject(long long ptr ptr ptr ptr ptr long ptr ptr) ChpeAutoRtlNewInstanceSecurityObject
1087 stdcall RtlNewSecurityGrantedAccess(long ptr ptr ptr ptr ptr) ChpeAutoRtlNewSecurityGrantedAccess
1088 stdcall RtlNewSecurityObject(ptr ptr ptr long ptr ptr) ChpeAutoRtlNewSecurityObject
1089 stdcall RtlNewSecurityObjectEx(ptr ptr ptr ptr long long ptr ptr) ChpeAutoRtlNewSecurityObjectEx
1090 stdcall RtlNewSecurityObjectWithMultipleInheritance(ptr ptr ptr ptr long long long ptr ptr) ChpeAutoRtlNewSecurityObjectWithMultipleInheritance
1091 stdcall RtlNormalizeProcessParams(ptr) ChpeAutoRtlNormalizeProcessParams
1092 stdcall -version=0x600+ RtlNormalizeString(long ptr long ptr ptr) ChpeAutoRtlNormalizeString
1093 stdcall RtlNtPathNameToDosPathName(long ptr ptr ptr) ChpeAutoRtlNtPathNameToDosPathName
1095 stdcall RtlNtStatusToDosErrorNoTeb(long) ChpeAutoRtlNtStatusToDosErrorNoTeb
1096 stub -version=0x600+ RtlNtdllName
1097 stdcall RtlNumberGenericTableElements(ptr) ChpeAutoRtlNumberGenericTableElements
1098 stdcall RtlNumberGenericTableElementsAvl(ptr) ChpeAutoRtlNumberGenericTableElementsAvl
1099 stdcall RtlNumberOfClearBits(ptr) ChpeAutoRtlNumberOfClearBits
1100 stdcall RtlNumberOfSetBits(ptr) ChpeAutoRtlNumberOfSetBits
1101 stdcall -version=0x600+ RtlNumberOfSetBitsUlongPtr(long) ChpeAutoRtlNumberOfSetBitsUlongPtr
1102 stdcall RtlOemStringToUnicodeSize(ptr) ChpeAutoRtlOemStringToUnicodeSize
1103 stdcall RtlOemStringToUnicodeString(ptr ptr long) ChpeAutoRtlOemStringToUnicodeString
1104 stdcall RtlOemToUnicodeN(ptr long ptr ptr long) ChpeAutoRtlOemToUnicodeN
1105 stdcall RtlOpenCurrentUser(long ptr) ChpeAutoRtlOpenCurrentUser
1106 stdcall -version=0x600+ RtlOwnerAcesPresent(long) ChpeStubRtlOwnerAcesPresent
1108 stdcall RtlPinAtomInAtomTable(ptr long) ChpeAutoRtlPinAtomInAtomTable
1109 stdcall RtlPopFrame(ptr) ChpeAutoRtlPopFrame
1110 stdcall RtlPrefixString(ptr ptr long) ChpeAutoRtlPrefixString
1111 stdcall RtlPrefixUnicodeString(ptr ptr long) ChpeAutoRtlPrefixUnicodeString
1112 stdcall -version=0x600+ RtlPrepareForProcessCloning() ChpeStubRtlPrepareForProcessCloning
1113 stdcall -version=0x600+ RtlProcessFlsData(ptr long) ChpeAutoRtlProcessFlsData
1114 stdcall RtlProtectHeap(ptr long) ChpeAutoRtlProtectHeap
1115 stdcall RtlPushFrame(ptr) ChpeAutoRtlPushFrame
1116 stdcall -version=0x600+ RtlQueryActivationContextApplicationSettings(long ptr wstr wstr ptr ptr ptr) ChpeAutoRtlQueryActivationContextApplicationSettings
1117 stdcall RtlQueryAtomInAtomTable(ptr long ptr ptr ptr ptr) ChpeAutoRtlQueryAtomInAtomTable
1118 stdcall -version=0x600+ RtlQueryCriticalSectionOwner(ptr) ChpeStubRtlQueryCriticalSectionOwner
1120 stdcall -version=0x600+ RtlQueryDynamicTimeZoneInformation(ptr) ChpeAutoRtlQueryDynamicTimeZoneInformation
1121 stdcall -version=0x600+ RtlQueryElevationFlags(ptr) ChpeStubRtlQueryElevationFlags
1123 stdcall RtlQueryEnvironmentVariable_U(ptr ptr ptr) ChpeAutoRtlQueryEnvironmentVariable_U
1124 stdcall RtlQueryHeapInformation(long long ptr long ptr) ChpeAutoRtlQueryHeapInformation
1125 stdcall RtlQueryInformationAcl(ptr ptr long long) ChpeAutoRtlQueryInformationAcl
1126 stdcall RtlQueryInformationActivationContext(long long ptr long ptr long ptr) ChpeAutoRtlQueryInformationActivationContext
1127 stdcall RtlQueryInformationActiveActivationContext(long ptr long ptr) ChpeAutoRtlQueryInformationActiveActivationContext
1128 stdcall RtlQueryInterfaceMemoryStream(ptr ptr ptr) ChpeAutoRtlQueryInterfaceMemoryStream
1129 stdcall -version=0x600+ RtlQueryModuleInformation(ptr long ptr) ChpeStubRtlQueryModuleInformation
1130 stdcall  RtlQueryProcessBackTraceInformation(ptr) ChpeStubRtlQueryProcessBackTraceInformation
1131 stdcall RtlQueryProcessDebugInformation(long long ptr) ChpeAutoRtlQueryProcessDebugInformation
1132 stdcall RtlQueryProcessHeapInformation(ptr) ChpeAutoRtlQueryProcessHeapInformation
1133 stdcall  RtlQueryProcessLockInformation(ptr) ChpeStubRtlQueryProcessLockInformation
1134 stdcall RtlQueryRegistryValues(long ptr ptr ptr ptr) ChpeAutoRtlQueryRegistryValues
1135 stdcall RtlQueryRegistryValuesEx(long ptr ptr ptr ptr) ChpeAutoRtlQueryRegistryValuesEx
1136 stdcall RtlQuerySecurityObject(ptr long ptr long ptr) ChpeAutoRtlQuerySecurityObject
1137 stdcall RtlQueryTagHeap(ptr long long long ptr) ChpeAutoRtlQueryTagHeap
1138 stdcall RtlQueryTimeZoneInformation(ptr) ChpeAutoRtlQueryTimeZoneInformation
1139 stdcall RtlQueueApcWow64Thread(ptr ptr ptr ptr ptr) ChpeAutoRtlQueueApcWow64Thread
1144 stdcall RtlRandomEx(ptr) ChpeAutoRtlRandomEx
1146 stdcall RtlReadMemoryStream(ptr ptr long ptr) ChpeAutoRtlReadMemoryStream
1147 stdcall RtlReadOutOfProcessMemoryStream(ptr ptr long ptr) ChpeAutoRtlReadOutOfProcessMemoryStream
1148 stdcall RtlRealPredecessor(ptr) ChpeAutoRtlRealPredecessor
1149 stdcall RtlRealSuccessor(ptr) ChpeAutoRtlRealSuccessor
1150 stdcall RtlRegisterSecureMemoryCacheCallback(ptr) ChpeAutoRtlRegisterSecureMemoryCacheCallback
1151 stdcall RtlRegisterCfgTargetRange(ptr long) ChpeAutoRtlRegisterCfgTargetRange
1152 stdcall -version=0x600+ RtlRegisterThreadWithCsrss() ChpeStubRtlRegisterThreadWithCsrss
1154 stdcall RtlReleaseActivationContext(ptr) ChpeAutoRtlReleaseActivationContext
1156 stdcall RtlReleasePebLock() ChpeAutoRtlReleasePebLock
1158 stdcall RtlReleaseRelativeName(ptr) ChpeAutoRtlReleaseRelativeName
1159 stdcall RtlReleaseResource(ptr) ChpeAutoRtlReleaseResource
1162 stdcall RtlRemoteCall(ptr ptr ptr long ptr long long) ChpeAutoRtlRemoteCall
1163 stdcall -version=0x600+ RtlRemovePrivileges(ptr ptr long) ChpeAutoRtlRemovePrivileges
1166 stdcall -version=0x600+ RtlReportException(long long long) ChpeStubRtlReportException
1167 stdcall -version=0x600+ RtlResetMemoryBlockLookaside(long) ChpeStubRtlResetMemoryBlockLookaside
1168 stdcall -version=0x600+ RtlResetMemoryZone(long) ChpeStubRtlResetMemoryZone
1169 stdcall -version=0x600+ RtlResetNtUserPfn() ChpeAutoRtlResetNtUserPfn
1170 stdcall RtlResetRtlTranslations(ptr) ChpeAutoRtlResetRtlTranslations
1173 stdcall -version=0x600+ RtlRetrieveNtUserPfn(ptr ptr ptr) ChpeAutoRtlRetrieveNtUserPfn
1174 stdcall RtlRevertMemoryStream(ptr) ChpeAutoRtlRevertMemoryStream
1175 stdcall RtlRunDecodeUnicodeString(long ptr) ChpeAutoRtlRunDecodeUnicodeString
1176 stdcall RtlRunEncodeUnicodeString(long ptr) ChpeAutoRtlRunEncodeUnicodeString
1177 stdcall -version=0x600+ RtlRunOnceBeginInitialize(ptr long ptr) ChpeAutoRtlRunOnceBeginInitialize
1178 stdcall -version=0x600+ RtlRunOnceComplete(ptr long ptr) ChpeAutoRtlRunOnceComplete
1181 stdcall RtlSecondsSince1970ToTime(long ptr) ChpeAutoRtlSecondsSince1970ToTime
1182 stdcall RtlSecondsSince1980ToTime(long ptr) ChpeAutoRtlSecondsSince1980ToTime
1183 stdcall RtlSeekMemoryStream(ptr int64 long ptr) ChpeAutoRtlSeekMemoryStream
1184 stdcall RtlSelfRelativeToAbsoluteSD2(ptr ptr) ChpeAutoRtlSelfRelativeToAbsoluteSD2
1185 stdcall RtlSelfRelativeToAbsoluteSD(ptr ptr ptr ptr ptr ptr ptr ptr ptr ptr ptr) ChpeAutoRtlSelfRelativeToAbsoluteSD
1186 stdcall -version=0x600+ RtlSendMsgToSm(ptr ptr) ChpeStubRtlSendMsgToSm
1187 stdcall RtlSetAllBits(ptr) ChpeAutoRtlSetAllBits
1188 stdcall RtlSetAttributesSecurityDescriptor(ptr long ptr) ChpeAutoRtlSetAttributesSecurityDescriptor
1189 stdcall RtlSetBits(ptr long long) ChpeAutoRtlSetBits
1190 stdcall RtlSetCfgTargetValidity(ptr long long ptr) ChpeAutoRtlSetCfgTargetValidity
1191 stdcall RtlSetControlSecurityDescriptor(ptr long long) ChpeAutoRtlSetControlSecurityDescriptor
1193 stdcall RtlSetCurrentDirectory_U(ptr) ChpeAutoRtlSetCurrentDirectory_U
1194 stdcall RtlSetCurrentEnvironment(wstr ptr) ChpeAutoRtlSetCurrentEnvironment
1195 stdcall -version=0x600+ RtlSetCurrentTransaction(ptr) ChpeStubRtlSetCurrentTransaction
1196 stdcall RtlSetDaclSecurityDescriptor(ptr long ptr long) ChpeAutoRtlSetDaclSecurityDescriptor
1197 stdcall -version=0x600+ RtlSetDynamicTimeZoneInformation(long) ChpeStubRtlSetDynamicTimeZoneInformation
1198 stdcall RtlSetEnvironmentStrings(wstr long) ChpeAutoRtlSetEnvironmentStrings
1199 stdcall -version=0x600+ RtlSetEnvironmentVar(ptr ptr long ptr long) ChpeStubRtlSetEnvironmentVar
1200 stdcall RtlSetEnvironmentVariable(ptr ptr ptr) ChpeAutoRtlSetEnvironmentVariable
1201 stdcall -version=0x600+ RtlSetExtendedFeaturesMask(ptr int64) ChpeAutoRtlSetExtendedFeaturesMask
1202 stdcall RtlSetGroupSecurityDescriptor(ptr ptr long) ChpeAutoRtlSetGroupSecurityDescriptor
1203 stdcall RtlSetHeapInformation(ptr long ptr ptr) ChpeAutoRtlSetHeapInformation
1204 stdcall RtlSetInformationAcl(ptr ptr long long) ChpeAutoRtlSetInformationAcl
1205 stdcall RtlSetIoCompletionCallback(long ptr long) ChpeAutoRtlSetIoCompletionCallback
1207 stdcall RtlSetLastWin32ErrorAndNtStatusFromNtStatus(long) ChpeAutoRtlSetLastWin32ErrorAndNtStatusFromNtStatus
1208 stdcall RtlSetMemoryStreamSize(ptr int64) ChpeAutoRtlSetMemoryStreamSize
1209 stdcall RtlSetOwnerSecurityDescriptor(ptr ptr long) ChpeAutoRtlSetOwnerSecurityDescriptor
1210 stdcall -version=0x600+ RtlSetProcessDebugInformation(ptr long ptr) ChpeStubRtlSetProcessDebugInformation
1211 stdcall -version=0x600+ RtlSetProcessPreferredUILanguages(long ptr ptr) ChpeAutoRtlSetProcessPreferredUILanguages
1212 cdecl RtlSetProcessIsCritical(long ptr long) ChpeAutoRtlSetProcessIsCritical
1213 stdcall RtlSetSaclSecurityDescriptor(ptr long ptr long) ChpeAutoRtlSetSaclSecurityDescriptor
1214 stdcall RtlSetSecurityDescriptorRMControl(ptr ptr) ChpeAutoRtlSetSecurityDescriptorRMControl
1215 stdcall RtlSetSecurityObject(long ptr ptr ptr ptr) ChpeAutoRtlSetSecurityObject
1216 stdcall RtlSetSecurityObjectEx(long ptr ptr long ptr ptr) ChpeAutoRtlSetSecurityObjectEx
1217 stdcall RtlSetThreadErrorMode(long ptr) ChpeAutoRtlSetThreadErrorMode
1218 cdecl RtlSetThreadIsCritical(long ptr long) ChpeAutoRtlSetThreadIsCritical
1219 stdcall RtlSetThreadPoolStartFunc(ptr ptr) ChpeAutoRtlSetThreadPoolStartFunc
1220 stdcall -version=0x600+ RtlSetThreadPreferredUILanguages(long ptr ptr) ChpeStubRtlSetThreadPreferredUILanguages
1221 stdcall RtlSetTimeZoneInformation(ptr) ChpeAutoRtlSetTimeZoneInformation
1222 stdcall RtlSetTimer(ptr ptr ptr ptr long long long) ChpeAutoRtlSetTimer
1223 stdcall RtlSetUnhandledExceptionFilter(ptr) ChpeAutoRtlSetUnhandledExceptionFilter
1224 stdcall RtlSetUserFlagsHeap(ptr long ptr long long) ChpeAutoRtlSetUserFlagsHeap
1225 stdcall RtlSetUserValueHeap(ptr long ptr ptr) ChpeAutoRtlSetUserValueHeap
1226 stdcall -version=0x600+ RtlSidDominates(long long ptr) ChpeStubRtlSidDominates
1227 stdcall -version=0x600+ RtlSidEqualLevel(long long ptr) ChpeStubRtlSidEqualLevel
1228 stdcall -version=0x600+ RtlSidHashInitialize(ptr long ptr) ChpeStubRtlSidHashInitialize
1229 stdcall -version=0x600+ RtlSidHashLookup(long ptr) ChpeStubRtlSidHashLookup
1230 stdcall -version=0x600+ RtlSidIsHigherLevel(long long ptr) ChpeStubRtlSidIsHigherLevel
1232 stdcall -version=0x600+ RtlSleepConditionVariableCS(ptr ptr ptr) ChpeAutoRtlSleepConditionVariableCS
1233 stdcall -version=0x600+ RtlSleepConditionVariableSRW(ptr ptr ptr long) ChpeAutoRtlSleepConditionVariableSRW
1234 stdcall RtlSplay(ptr) ChpeAutoRtlSplay
1235 stdcall RtlStartRXact(ptr) ChpeAutoRtlStartRXact
1236 stdcall RtlStatMemoryStream(ptr ptr long) ChpeAutoRtlStatMemoryStream
1237 stdcall RtlStringFromGUID(ptr ptr) ChpeAutoRtlStringFromGUID
1238 stdcall RtlSubAuthorityCountSid(ptr) ChpeAutoRtlSubAuthorityCountSid
1239 stdcall RtlSubAuthoritySid(ptr long) ChpeAutoRtlSubAuthoritySid
1240 stdcall RtlSubtreePredecessor(ptr) ChpeAutoRtlSubtreePredecessor
1241 stdcall RtlSubtreeSuccessor(ptr) ChpeAutoRtlSubtreeSuccessor
1242 stdcall RtlSystemTimeToLocalTime(ptr ptr) ChpeAutoRtlSystemTimeToLocalTime
1243 stdcall -version=0x600+ RtlTestBit(ptr long) ChpeAutoRtlTestBit
1244 stdcall RtlTimeFieldsToTime(ptr ptr) ChpeAutoRtlTimeFieldsToTime
1245 stdcall RtlTimeToElapsedTimeFields(long long) ChpeAutoRtlTimeToElapsedTimeFields
1246 stdcall RtlTimeToSecondsSince1970(ptr ptr) ChpeAutoRtlTimeToSecondsSince1970
1247 stdcall RtlTimeToSecondsSince1980(ptr ptr) ChpeAutoRtlTimeToSecondsSince1980
1248 stdcall RtlTimeToTimeFields(long long) ChpeAutoRtlTimeToTimeFields
1249 stdcall RtlTraceDatabaseAdd(ptr long ptr ptr) ChpeAutoRtlTraceDatabaseAdd
1250 stdcall RtlTraceDatabaseCreate(long ptr long long ptr) ChpeAutoRtlTraceDatabaseCreate
1251 stdcall RtlTraceDatabaseDestroy(ptr) ChpeAutoRtlTraceDatabaseDestroy
1252 stdcall RtlTraceDatabaseEnumerate(ptr ptr ptr) ChpeAutoRtlTraceDatabaseEnumerate
1253 stdcall RtlTraceDatabaseFind(ptr long ptr ptr) ChpeAutoRtlTraceDatabaseFind
1254 stdcall RtlTraceDatabaseLock(ptr) ChpeAutoRtlTraceDatabaseLock
1255 stdcall RtlTraceDatabaseUnlock(ptr) ChpeAutoRtlTraceDatabaseUnlock
1256 stdcall RtlTraceDatabaseValidate(ptr) ChpeAutoRtlTraceDatabaseValidate
1257 stdcall -version=0x600+ RtlTryAcquirePebLock() ChpeStubRtlTryAcquirePebLock
1261 stdcall RtlUnhandledExceptionFilter2(ptr long) ChpeAutoRtlUnhandledExceptionFilter2
1262 stdcall RtlUnhandledExceptionFilter(ptr) ChpeAutoRtlUnhandledExceptionFilter
1263 stdcall RtlUnicodeStringToAnsiSize(ptr) ChpeAutoRtlUnicodeStringToAnsiSize
1264 stdcall -version=0x601+ RtlUnicodeToUTF8N(ptr long ptr wstr long) ChpeAutoRtlUnicodeToUTF8N
1265 stdcall -version=0xA00+ RtlUTF8StringToUnicodeString(ptr ptr long) ChpeAutoRtlUTF8StringToUnicodeString
1267 stdcall RtlUnicodeStringToAnsiString(ptr ptr long) ChpeAutoRtlUnicodeStringToAnsiString
1268 stdcall RtlUnicodeStringToCountedOemString(ptr ptr long) ChpeAutoRtlUnicodeStringToCountedOemString
1269 stdcall RtlUnicodeStringToInteger(ptr long ptr) ChpeAutoRtlUnicodeStringToInteger
1270 stdcall RtlUnicodeStringToOemSize(ptr) ChpeAutoRtlUnicodeStringToOemSize
1271 stdcall RtlUnicodeStringToOemString(ptr ptr long) ChpeAutoRtlUnicodeStringToOemString
1272 stdcall RtlUnicodeToCustomCPN(ptr ptr long ptr wstr long) ChpeAutoRtlUnicodeToCustomCPN
1273 stdcall RtlUnicodeToMultiByteN(ptr long ptr ptr long) ChpeAutoRtlUnicodeToMultiByteN
1274 stdcall RtlUnicodeToMultiByteSize(ptr ptr long) ChpeAutoRtlUnicodeToMultiByteSize
1275 stdcall RtlUnicodeToOemN(ptr long ptr ptr long) ChpeAutoRtlUnicodeToOemN
1276 stdcall RtlUniform(ptr) ChpeAutoRtlUniform
1277 stdcall RtlUnlockBootStatusData(ptr) ChpeAutoRtlUnlockBootStatusData
1278 stdcall -version=0x600+ RtlUnlockCurrentThread() ChpeStubRtlUnlockCurrentThread
1279 stdcall RtlUnlockHeap(long) ChpeAutoRtlUnlockHeap
1280 stdcall -version=0x600+ RtlUnlockMemoryBlockLookaside(long) ChpeStubRtlUnlockMemoryBlockLookaside
1281 stdcall RtlUnlockMemoryStreamRegion(ptr int64 int64 long) ChpeAutoRtlUnlockMemoryStreamRegion
1282 stdcall -version=0x600+ RtlUnlockMemoryZone(long) ChpeStubRtlUnlockMemoryZone
1283 stdcall -version=0x600+ RtlUnlockModuleSection(long) ChpeStubRtlUnlockModuleSection
1286 stdcall RtlUnregisterCfgTargetRange(ptr) ChpeAutoRtlUnregisterCfgTargetRange
1287 stdcall RtlUpcaseUnicodeChar(long) ChpeAutoRtlUpcaseUnicodeChar
1288 stdcall RtlUpcaseUnicodeString(ptr ptr long) ChpeAutoRtlUpcaseUnicodeString
1289 stdcall RtlUpcaseUnicodeStringToAnsiString(ptr ptr long) ChpeAutoRtlUpcaseUnicodeStringToAnsiString
1290 stdcall RtlUpcaseUnicodeStringToCountedOemString(ptr ptr long) ChpeAutoRtlUpcaseUnicodeStringToCountedOemString
1291 stdcall RtlUpcaseUnicodeStringToOemString(ptr ptr long) ChpeAutoRtlUpcaseUnicodeStringToOemString
1292 stdcall RtlUpcaseUnicodeToCustomCPN(ptr ptr long ptr wstr long) ChpeAutoRtlUpcaseUnicodeToCustomCPN
1293 stdcall RtlUpcaseUnicodeToMultiByteN(ptr long ptr ptr long) ChpeAutoRtlUpcaseUnicodeToMultiByteN
1294 stdcall RtlUpcaseUnicodeToOemN(ptr long ptr ptr long) ChpeAutoRtlUpcaseUnicodeToOemN
1295 stdcall -version=0x600+ RtlUpdateClonedCriticalSection(long) ChpeStubRtlUpdateClonedCriticalSection
1296 stdcall -version=0x600+ RtlUpdateClonedSRWLock(ptr long) ChpeStubRtlUpdateClonedSRWLock
1297 stdcall RtlUpdateTimer(ptr ptr long long) ChpeAutoRtlUpdateTimer
1298 stdcall RtlUpperChar(long) ChpeAutoRtlUpperChar
1299 stdcall RtlUpperString(ptr ptr) ChpeAutoRtlUpperString
1301 stdcall RtlValidAcl(ptr) ChpeAutoRtlValidAcl
1302 stdcall RtlValidRelativeSecurityDescriptor(ptr long long) ChpeAutoRtlValidRelativeSecurityDescriptor
1303 stdcall RtlValidSecurityDescriptor(ptr) ChpeAutoRtlValidSecurityDescriptor
1304 stdcall RtlValidSid(ptr) ChpeAutoRtlValidSid
1305 stdcall RtlValidateHeap(long long ptr) ChpeAutoRtlValidateHeap
1306 stdcall RtlValidateProcessHeaps() ChpeAutoRtlValidateProcessHeaps
1307 stdcall RtlValidateUnicodeString(long ptr) ChpeAutoRtlValidateUnicodeString
1308 stdcall RtlVerifyVersionInfo(ptr long double) ChpeAutoRtlVerifyVersionInfo
1310 stdcall RtlVirtualUnwind2(long int64 int64 ptr ptr ptr ptr ptr ptr ptr ptr ptr long) ChpeAutoRtlVirtualUnwind2
1316 stdcall RtlWalkFrameChain(ptr long long) ChpeAutoRtlWalkFrameChain
1317 stdcall RtlWeaklyEnumerateEntryHashTable(ptr ptr) ChpeAutoRtlWeaklyEnumerateEntryHashTable
1318 stdcall RtlWalkHeap(long ptr) ChpeAutoRtlWalkHeap
1319 stdcall -version=0x600+ RtlWerpReportException(long long ptr long long ptr) ChpeStubRtlWerpReportException
1320 stdcall -version=0x600+ RtlWow64CallFunction64() ChpeStubRtlWow64CallFunction64
1321 stdcall RtlWow64EnableFsRedirection(long) ChpeAutoRtlWow64EnableFsRedirection
1322 stdcall RtlWow64EnableFsRedirectionEx(long ptr) ChpeAutoRtlWow64EnableFsRedirectionEx
1323 stdcall -version=0x600+ RtlOpenCrossProcessEmulatorWorkConnection(ptr ptr ptr) ChpeAutoRtlOpenCrossProcessEmulatorWorkConnection
1324 stdcall -version=0x600+ RtlWow64GetCpuAreaInfo(ptr long ptr) ChpeAutoRtlWow64GetCpuAreaInfo
1325 stdcall -version=0x600+ RtlWow64GetCurrentMachine() ChpeAutoRtlWow64GetCurrentMachine
1326 stdcall -version=0x600+ RtlWow64GetProcessMachines(ptr ptr ptr) ChpeAutoRtlWow64GetProcessMachines
1327 stdcall -version=0x600+ RtlWow64GetThreadContext(ptr ptr) ChpeAutoRtlWow64GetThreadContext
1329 stdcall -version=0x600+ RtlWow64LogMessageInEventLogger(long long long) ChpeStubRtlWow64LogMessageInEventLogger
1330 stdcall -version=0x600+ RtlWow64PopCrossProcessWorkFromFreeList(ptr) ChpeAutoRtlWow64PopCrossProcessWorkFromFreeList
1331 stdcall -version=0x600+ RtlWow64PushCrossProcessWorkOntoWorkList(ptr ptr ptr) ChpeAutoRtlWow64PushCrossProcessWorkOntoWorkList
1332 stdcall -version=0x600+ RtlWow64RequestCrossProcessHeavyFlush(ptr) ChpeAutoRtlWow64RequestCrossProcessHeavyFlush
1333 stdcall -version=0x600+ RtlWow64SetThreadContext(ptr ptr) ChpeAutoRtlWow64SetThreadContext
1334 stdcall -version=0x600+ RtlWow64SuspendThread(ptr ptr) ChpeAutoRtlWow64SuspendThread
1335 stdcall RtlWriteMemoryStream(ptr ptr long ptr) ChpeAutoRtlWriteMemoryStream
1336 stdcall RtlWriteRegistryValue(long ptr ptr long ptr long) ChpeAutoRtlWriteRegistryValue
1337 stdcall RtlZeroHeap(ptr long) ChpeAutoRtlZeroHeap
1339 stdcall RtlZombifyActivationContext(ptr) ChpeAutoRtlZombifyActivationContext
1341 stdcall -version=0x600+ RtlpCheckDynamicTimeZoneInformation(ptr long) ChpeStubRtlpCheckDynamicTimeZoneInformation
1342 stdcall -version=0x600+ RtlpCleanupRegistryKeys() ChpeStubRtlpCleanupRegistryKeys
1343 stdcall -version=0x600+ RtlpConvertCultureNamesToLCIDs(wstr ptr) ChpeStubRtlpConvertCultureNamesToLCIDs
1344 stdcall -version=0x600+ RtlpConvertLCIDsToCultureNames(wstr ptr) ChpeStubRtlpConvertLCIDsToCultureNames
1345 stdcall -version=0x600+ RtlpCreateProcessRegistryInfo(ptr) ChpeStubRtlpCreateProcessRegistryInfo
1346 stdcall RtlpEnsureBufferSize(long ptr long) ChpeAutoRtlpEnsureBufferSize
1347 stdcall -version=0x600+ RtlpGetLCIDFromLangInfoNode(long long ptr) ChpeStubRtlpGetLCIDFromLangInfoNode
1348 stdcall -version=0x600+ RtlpGetNameFromLangInfoNode(long long long) ChpeStubRtlpGetNameFromLangInfoNode
1349 stdcall -version=0x600+ RtlpGetSystemDefaultUILanguage(ptr long) ChpeStubRtlpGetSystemDefaultUILanguage
1350 stdcall -version=0x600+ RtlpGetUserOrMachineUILanguage4NLS(long long ptr) ChpeStubRtlpGetUserOrMachineUILanguage4NLS
1351 stdcall -version=0x600+ RtlpInitializeLangRegistryInfo(ptr) ChpeStubRtlpInitializeLangRegistryInfo
1352 stdcall -version=0x600+ RtlpIsQualifiedLanguage(long ptr long) ChpeStubRtlpIsQualifiedLanguage
1353 stdcall -version=0x600+ RtlpLoadMachineUIByPolicy(ptr long ptr) ChpeStubRtlpLoadMachineUIByPolicy
1354 stdcall -version=0x600+ RtlpLoadUserUIByPolicy(ptr long ptr) ChpeStubRtlpLoadUserUIByPolicy
1355 stdcall -version=0x600+ RtlpMuiFreeLangRegistryInfo(long) ChpeStubRtlpMuiFreeLangRegistryInfo
1356 stdcall -version=0x600+ RtlpMuiRegCreateRegistryInfo() ChpeStubRtlpMuiRegCreateRegistryInfo
1357 stdcall -version=0x600+ RtlpMuiRegFreeRegistryInfo(long long) ChpeStubRtlpMuiRegFreeRegistryInfo
1358 stdcall -version=0x600+ RtlpMuiRegLoadRegistryInfo(long long) ChpeStubRtlpMuiRegLoadRegistryInfo
1359 stdcall RtlpNotOwnerCriticalSection(ptr) ChpeAutoRtlpNotOwnerCriticalSection
1360 stdcall RtlpNtCreateKey(ptr long ptr long ptr ptr) ChpeAutoRtlpNtCreateKey
1361 stdcall RtlpNtEnumerateSubKey(ptr ptr long long) ChpeAutoRtlpNtEnumerateSubKey
1362 stdcall RtlpNtMakeTemporaryKey(ptr) ChpeAutoRtlpNtMakeTemporaryKey
1363 stdcall RtlpNtOpenKey(ptr long ptr long) ChpeAutoRtlpNtOpenKey
1364 stdcall RtlpNtQueryValueKey(ptr ptr ptr ptr long) ChpeAutoRtlpNtQueryValueKey
1365 stdcall RtlpNtSetValueKey(ptr long ptr long) ChpeAutoRtlpNtSetValueKey
1366 stdcall -version=0x600+ RtlpQueryDefaultUILanguage(ptr long) ChpeStubRtlpQueryDefaultUILanguage
1367 stdcall -version=0x600+ RtlpQueryProcessDebugInformationFromWow64(long ptr) ChpeStubRtlpQueryProcessDebugInformationFromWow64
1368 stdcall -version=0x600+ RtlpRefreshCachedUILanguage(wstr long) ChpeStubRtlpRefreshCachedUILanguage
1369 stdcall -version=0x600+ RtlpSetInstallLanguage(long ptr) ChpeStubRtlpSetInstallLanguage
1370 stdcall -version=0x600+ RtlpSetPreferredUILanguages(long ptr ptr) ChpeStubRtlpSetPreferredUILanguages
1371 stdcall RtlpUnWaitCriticalSection(ptr) ChpeAutoRtlpUnWaitCriticalSection
1372 stdcall -version=0x600+ RtlpVerifyAndCommitUILanguageSettings(long) ChpeStubRtlpVerifyAndCommitUILanguageSettings
1373 stdcall RtlpWaitForCriticalSection(ptr) ChpeAutoRtlpWaitForCriticalSection
1374 stdcall RtlxAnsiStringToUnicodeSize(ptr) ChpeAutoRtlxAnsiStringToUnicodeSize
1375 stdcall RtlxOemStringToUnicodeSize(ptr) ChpeAutoRtlxOemStringToUnicodeSize
1376 stdcall RtlxUnicodeStringToAnsiSize(ptr) ChpeAutoRtlxUnicodeStringToAnsiSize
1377 stdcall RtlxUnicodeStringToOemSize(ptr) ChpeAutoRtlxUnicodeStringToOemSize
1378 stdcall -version=0x600+ ShipAssert(long long) ChpeStubShipAssert
1379 stdcall -version=0x600+ ShipAssertGetBufferInfo(ptr ptr) ChpeStubShipAssertGetBufferInfo
1380 stdcall -version=0x600+ ShipAssertMsgA(long long) ChpeStubShipAssertMsgA
1381 stdcall -version=0x600+ ShipAssertMsgW(long long) ChpeStubShipAssertMsgW
1382 stdcall -version=0x600+ TpAllocAlpcCompletion(ptr ptr ptr ptr ptr) ChpeAutoTpAllocAlpcCompletion
1383 stdcall -version=0x600+ TpAllocAlpcCompletionEx(ptr ptr ptr ptr ptr) ChpeAutoTpAllocAlpcCompletionEx
1384 stdcall -version=0x600+ TpAllocCleanupGroup(ptr) ChpeAutoTpAllocCleanupGroup
1385 stdcall -version=0x600+ TpAllocIoCompletion(ptr ptr ptr ptr ptr) ChpeAutoTpAllocIoCompletion
1386 stdcall -version=0x600+ TpAllocPool(ptr ptr) ChpeAutoTpAllocPool
1387 stdcall -version=0x600+ TpAllocTimer(ptr ptr ptr ptr) ChpeAutoTpAllocTimer
1388 stdcall -version=0x600+ TpAllocWait(ptr ptr ptr ptr) ChpeAutoTpAllocWait
1389 stdcall -version=0x600+ TpAllocWork(ptr ptr ptr ptr) ChpeAutoTpAllocWork
1390 stdcall -version=0x600+ TpAlpcRegisterCompletionList(ptr) ChpeAutoTpAlpcRegisterCompletionList
1391 stdcall -version=0x600+ TpAlpcUnregisterCompletionList(ptr) ChpeAutoTpAlpcUnregisterCompletionList
1393 stdcall -version=0x600+ TpCallbackMayRunLong(ptr) ChpeAutoTpCallbackMayRunLong
1396 stdcall -version=0x600+ TpCallbackSendAlpcMessageOnCompletion(ptr ptr long ptr) ChpeAutoTpCallbackSendAlpcMessageOnCompletion
1397 stdcall -version=0x600+ TpCallbackSendPendingAlpcMessage(ptr) ChpeAutoTpCallbackSendPendingAlpcMessage
1401 stdcall -version=0x600+ TpCaptureCaller(long) ChpeStubTpCaptureCaller
1402 stdcall -version=0x600+ TpCheckTerminateWorker(ptr) ChpeStubTpCheckTerminateWorker
1403 stdcall -version=0x600+ TpDbgDumpHeapUsage(long ptr long) ChpeStubTpDbgDumpHeapUsage
1404 stdcall -version=0x600+ TpDbgSetLogRoutine() ChpeStubTpDbgSetLogRoutine
1408 stdcall -version=0x601+ TpQueryPoolStackInformation(ptr ptr) ChpeAutoTpQueryPoolStackInformation
1409 stdcall -version=0x600+ TpReleaseAlpcCompletion(ptr) ChpeAutoTpReleaseAlpcCompletion
1419 stdcall -version=0x601+ TpSetPoolStackInformation(ptr ptr) ChpeAutoTpSetPoolStackInformation
1424 stdcall -version=0x600+ TpSimpleTryPost(ptr ptr ptr) ChpeAutoTpSimpleTryPost
1426 stdcall -version=0x600+ TpWaitForAlpcCompletion(ptr) ChpeAutoTpWaitForAlpcCompletion
1432 stdcall -version=0x600+ WerCheckEventEscalation(long ptr) ChpeStubWerCheckEventEscalation
1433 stdcall -version=0x600+ WerReportSQMEvent(long long long) ChpeStubWerReportSQMEvent
1434 stdcall -version=0x600+ WerReportWatsonEvent(long long long long) ChpeStubWerReportWatsonEvent
1435 stdcall -version=0x600+ WinSqmAddToStream(ptr long long long) ChpeStubWinSqmAddToStream
1436 stdcall -version=0x600+ WinSqmAddToStreamEx(ptr long long ptr long) ChpeStubWinSqmAddToStreamEx
1437 stdcall -version=0x600+ WinSqmEndSession(ptr) ChpeStubWinSqmEndSession
1438 stdcall -version=0x600+ WinSqmEventEnabled(long ptr) ChpeAutoWinSqmEventEnabled
1439 stdcall -version=0x600+ WinSqmEventWrite(long long long) ChpeStubWinSqmEventWrite
1440 stdcall -version=0x600+ WinSqmIncrementDWORD(long long long) ChpeAutoWinSqmIncrementDWORD
1441 stdcall -version=0x600+ WinSqmIsOptedIn() ChpeAutoWinSqmIsOptedIn
1442 stdcall -version=0x600+ WinSqmSetDWORD(ptr long long) ChpeStubWinSqmSetDWORD
1443 stdcall -version=0x600+ WinSqmSetString(ptr long ptr) ChpeStubWinSqmSetString
1444 stdcall -version=0x600+ WinSqmStartSession(ptr) ChpeStubWinSqmStartSession
1445 stdcall ZwAcceptConnectPort(ptr long ptr long long ptr) ChpeAutoZwAcceptConnectPort
1446 stdcall ZwAccessCheck(ptr long long ptr ptr ptr ptr ptr) ChpeAutoZwAccessCheck
1447 stdcall ZwAccessCheckAndAuditAlarm(ptr long ptr ptr ptr long ptr long ptr ptr ptr) ChpeAutoZwAccessCheckAndAuditAlarm
1448 stdcall ZwAccessCheckByType(ptr ptr ptr long ptr long ptr ptr long ptr ptr) ChpeAutoZwAccessCheckByType
1449 stdcall ZwAccessCheckByTypeAndAuditAlarm(ptr ptr ptr ptr ptr ptr long long long ptr long ptr long ptr ptr ptr) ChpeAutoZwAccessCheckByTypeAndAuditAlarm
1450 stdcall ZwAccessCheckByTypeResultList(ptr ptr ptr long ptr long ptr ptr long ptr ptr) ChpeAutoZwAccessCheckByTypeResultList
1451 stdcall ZwAccessCheckByTypeResultListAndAuditAlarm(ptr ptr ptr ptr ptr ptr long long long ptr long ptr long ptr ptr ptr) ChpeAutoZwAccessCheckByTypeResultListAndAuditAlarm
1452 stdcall ZwAccessCheckByTypeResultListAndAuditAlarmByHandle(ptr ptr ptr ptr ptr ptr ptr long long long ptr long ptr long ptr ptr ptr) ChpeAutoZwAccessCheckByTypeResultListAndAuditAlarmByHandle
1453 stub -version=0x600+ ZwAcquireCMFViewOwnership
1454 stdcall ZwAddAtom(ptr long ptr) ChpeAutoZwAddAtom
1455 stdcall ZwAddBootEntry(ptr long) ChpeAutoZwAddBootEntry
1456 stdcall ZwAddDriverEntry(ptr long) ChpeAutoZwAddDriverEntry
1457 stdcall ZwAdjustGroupsToken(long long long long long long) ChpeAutoZwAdjustGroupsToken
1458 stdcall ZwAdjustPrivilegesToken(long long long long long long) ChpeAutoZwAdjustPrivilegesToken
1459 stdcall ZwAlertResumeThread(long ptr) ChpeAutoZwAlertResumeThread
1460 stdcall ZwAlertThread(long) ChpeAutoZwAlertThread
1461 stdcall ZwAlertThreadByThreadId(long) ChpeAutoZwAlertThreadByThreadId
1462 stdcall ZwAllocateLocallyUniqueId(ptr) ChpeAutoZwAllocateLocallyUniqueId
1463 stdcall ZwAllocateUserPhysicalPages(ptr ptr ptr) ChpeAutoZwAllocateUserPhysicalPages
1464 stdcall ZwAllocateUuids(ptr ptr ptr ptr) ChpeAutoZwAllocateUuids
1465 stdcall ZwAllocateVirtualMemory(long ptr ptr ptr long long) ChpeAutoZwAllocateVirtualMemory
1466 stdcall ZwAllocateVirtualMemoryEx(long ptr ptr long long ptr long) ChpeAutoZwAllocateVirtualMemoryEx
1467 stdcall -version=0x600+ ZwAlpcAcceptConnectPort(ptr ptr long ptr ptr ptr ptr ptr long) ChpeAutoZwAlpcAcceptConnectPort
1468 stdcall -version=0x600+ ZwAlpcCancelMessage(ptr long ptr) ChpeAutoZwAlpcCancelMessage
1469 stdcall -version=0x600+ ZwAlpcConnectPort(ptr ptr ptr ptr long ptr ptr ptr ptr ptr ptr) ChpeAutoZwAlpcConnectPort
1470 stdcall -version=0x602+ ZwAlpcConnectPortEx(ptr ptr ptr ptr long ptr ptr ptr ptr ptr ptr) ChpeAutoZwAlpcConnectPortEx
1471 stdcall -version=0x600+ ZwAlpcCreatePort(ptr ptr ptr) ChpeAutoZwAlpcCreatePort
1472 stdcall -version=0x600+ ZwAlpcCreatePortSection(ptr long ptr long ptr ptr) ChpeAutoZwAlpcCreatePortSection
1473 stdcall -version=0x600+ ZwAlpcCreateResourceReserve(ptr long long ptr) ChpeAutoZwAlpcCreateResourceReserve
1474 stdcall -version=0x600+ ZwAlpcCreateSectionView(ptr long ptr) ChpeAutoZwAlpcCreateSectionView
1475 stdcall -version=0x600+ ZwAlpcCreateSecurityContext(ptr long ptr) ChpeAutoZwAlpcCreateSecurityContext
1476 stdcall -version=0x600+ ZwAlpcDeletePortSection(ptr long ptr) ChpeAutoZwAlpcDeletePortSection
1477 stdcall -version=0x600+ ZwAlpcDeleteResourceReserve(ptr long long) ChpeAutoZwAlpcDeleteResourceReserve
1478 stdcall -version=0x600+ ZwAlpcDeleteSectionView(ptr long ptr) ChpeAutoZwAlpcDeleteSectionView
1479 stdcall -version=0x600+ ZwAlpcDeleteSecurityContext(ptr long ptr) ChpeAutoZwAlpcDeleteSecurityContext
1480 stdcall -version=0x600+ ZwAlpcDisconnectPort(ptr long) ChpeAutoZwAlpcDisconnectPort
1481 stdcall -version=0x600+ ZwAlpcImpersonateClientOfPort(ptr ptr ptr) ChpeAutoZwAlpcImpersonateClientOfPort
1482 stdcall -version=0xA00+ ZwAlpcImpersonateClientContainerOfPort(ptr ptr long) ChpeAutoZwAlpcImpersonateClientContainerOfPort
1483 stdcall -version=0x600+ ZwAlpcOpenSenderProcess(ptr ptr ptr long long ptr) ChpeAutoZwAlpcOpenSenderProcess
1484 stdcall -version=0x600+ ZwAlpcOpenSenderThread(ptr ptr ptr long long ptr) ChpeAutoZwAlpcOpenSenderThread
1485 stdcall -version=0x600+ ZwAlpcQueryInformation(ptr long ptr long ptr) ChpeAutoZwAlpcQueryInformation
1486 stdcall -version=0x600+ ZwAlpcQueryInformationMessage(ptr ptr long ptr long ptr) ChpeAutoZwAlpcQueryInformationMessage
1487 stdcall -version=0x600+ ZwAlpcRevokeSecurityContext(ptr long ptr) ChpeAutoZwAlpcRevokeSecurityContext
1488 stdcall -version=0x600+ ZwAlpcSendWaitReceivePort(ptr long ptr ptr ptr ptr ptr ptr) ChpeAutoZwAlpcSendWaitReceivePort
1489 stdcall -version=0x600+ ZwAlpcSetInformation(ptr long ptr long) ChpeAutoZwAlpcSetInformation
1490 stdcall ZwApphelpCacheControl(long ptr) ChpeAutoZwApphelpCacheControl
1491 stdcall ZwAreMappedFilesTheSame(ptr ptr) ChpeAutoZwAreMappedFilesTheSame
1492 stdcall ZwAssignProcessToJobObject(long long) ChpeAutoZwAssignProcessToJobObject
1493 stdcall ZwCallbackReturn(ptr long long) ChpeAutoZwCallbackReturn
1494 stdcall ZwCancelDeviceWakeupRequest(ptr) ChpeAutoZwCancelDeviceWakeupRequest
1495 stdcall ZwCancelIoFile(long ptr) ChpeAutoZwCancelIoFile
1496 stdcall -version=0x600+ ZwCancelIoFileEx(ptr ptr ptr) ChpeAutoZwCancelIoFileEx
1497 stdcall -version=0x600+ ZwCancelSynchronousIoFile(ptr ptr ptr) ChpeAutoZwCancelSynchronousIoFile
1498 stdcall ZwCancelTimer(long ptr) ChpeAutoZwCancelTimer
1499 stdcall ZwClearEvent(long) ChpeAutoZwClearEvent
1500 stdcall ZwClose(long) ChpeAutoZwClose
1501 stdcall ZwCloseObjectAuditAlarm(ptr ptr long) ChpeAutoZwCloseObjectAuditAlarm
1502 stdcall -version=0x600+ ZwCommitComplete(ptr ptr) ChpeAutoZwCommitComplete
1503 stdcall -version=0x600+ ZwCommitEnlistment(ptr ptr) ChpeAutoZwCommitEnlistment
1504 stdcall -version=0x600+ ZwCommitTransaction(ptr long) ChpeAutoZwCommitTransaction
1505 stdcall ZwCompactKeys(long ptr) ChpeAutoZwCompactKeys
1506 stdcall ZwCompareTokens(ptr ptr ptr) ChpeAutoZwCompareTokens
1507 stdcall ZwCompleteConnectPort(ptr) ChpeAutoZwCompleteConnectPort
1508 stdcall ZwCompressKey(ptr) ChpeAutoZwCompressKey
1509 stdcall ZwConnectPort(ptr ptr ptr ptr ptr ptr ptr ptr) ChpeAutoZwConnectPort
1510 stdcall ZwContinue(ptr long) ChpeAutoZwContinue
1511 stdcall ZwCreateDebugObject(ptr long ptr long) ChpeAutoZwCreateDebugObject
1512 stdcall ZwCreateDirectoryObject(long long long) ChpeAutoZwCreateDirectoryObject
1513 stdcall -version=0x600+ ZwCreateEnlistment(ptr long ptr ptr ptr long long ptr) ChpeAutoZwCreateEnlistment
1514 stdcall ZwCreateEvent(long long long long long) ChpeAutoZwCreateEvent
1515 stdcall ZwCreateEventPair(ptr long ptr) ChpeAutoZwCreateEventPair
1516 stdcall ZwCreateFile(ptr long ptr ptr long long long ptr long long ptr) ChpeAutoZwCreateFile
1517 stdcall ZwCreateIoCompletion(ptr long ptr long) ChpeAutoZwCreateIoCompletion
1518 stdcall -version=0x602+ ZwCreateWaitCompletionPacket(ptr long ptr) ChpeAutoZwCreateWaitCompletionPacket
1519 stdcall -version=0x602+ ZwAssociateWaitCompletionPacket(ptr ptr ptr ptr ptr long ptr ptr) ChpeAutoZwAssociateWaitCompletionPacket
1520 stdcall -version=0x602+ ZwCancelWaitCompletionPacket(ptr long) ChpeAutoZwCancelWaitCompletionPacket
1521 stdcall ZwCreateJobObject(ptr long ptr) ChpeAutoZwCreateJobObject
1522 stdcall ZwCreateJobSet(long ptr long) ChpeAutoZwCreateJobSet
1523 stdcall ZwCreateKey(ptr long ptr long ptr long long) ChpeAutoZwCreateKey
1524 stdcall -version=0x600+ ZwCreateKeyTransacted(ptr long ptr long ptr long ptr ptr) ChpeAutoZwCreateKeyTransacted
1525 stdcall ZwCreateKeyedEvent(ptr long ptr long) ChpeAutoZwCreateKeyedEvent
1526 stdcall ZwCreateMailslotFile(long long long long long long long long) ChpeAutoZwCreateMailslotFile
1527 stdcall ZwCreateMutant(ptr long ptr long) ChpeAutoZwCreateMutant
1528 stdcall ZwCreateNamedPipeFile(ptr long ptr ptr long long long long long long long long long ptr) ChpeAutoZwCreateNamedPipeFile
1529 stdcall ZwCreatePagingFile(ptr ptr ptr long) ChpeAutoZwCreatePagingFile
1530 stdcall ZwCreatePort(ptr ptr long long long) ChpeAutoZwCreatePort
1531 stdcall ZwCreateProcess(ptr long ptr ptr long ptr ptr ptr) ChpeAutoZwCreateProcess
1532 stdcall ZwCreateProcessEx(ptr long ptr ptr long ptr ptr ptr long) ChpeAutoZwCreateProcessEx
1533 stdcall ZwCreateProfile(ptr ptr ptr long long ptr long long long) ChpeAutoZwCreateProfile
1534 stdcall -version=0x600+ ZwCreateResourceManager(ptr long ptr ptr ptr long ptr) ChpeAutoZwCreateResourceManager
1535 stdcall ZwCreateSection(ptr long ptr ptr long long long) ChpeAutoZwCreateSection
1536 stdcall ZwCreateSemaphore(ptr long ptr long long) ChpeAutoZwCreateSemaphore
1537 stdcall ZwCreateSymbolicLinkObject(ptr long ptr ptr) ChpeAutoZwCreateSymbolicLinkObject
1538 stdcall ZwCreateThread(ptr long ptr ptr ptr ptr ptr long) ChpeAutoZwCreateThread
1539 stdcall -version=0x600+ ZwCreateThreadEx(ptr long ptr ptr ptr ptr long long long long ptr) ChpeAutoZwCreateThreadEx
1540 stdcall ZwCreateTimer(ptr long ptr long) ChpeAutoZwCreateTimer
1541 stdcall ZwCreateToken(ptr long ptr long ptr ptr ptr ptr ptr ptr ptr ptr ptr) ChpeAutoZwCreateToken
1542 stdcall -version=0x600+ ZwCreateTransaction(ptr long ptr ptr ptr long long long ptr ptr) ChpeAutoZwCreateTransaction
1543 stdcall -version=0x600+ ZwCreateTransactionManager(ptr long ptr ptr long long) ChpeAutoZwCreateTransactionManager
1544 stdcall -version=0x600+ ZwCreateUserProcess(ptr ptr long long ptr ptr long long ptr ptr ptr) ChpeAutoZwCreateUserProcess
1545 stdcall ZwCreateWaitablePort(ptr ptr long long long) ChpeAutoZwCreateWaitablePort
1546 stdcall -version=0x602+ ZwCreateWnfStateName(ptr long long long ptr long ptr) ChpeAutoZwCreateWnfStateName
1547 stdcall -version=0x600+ ZwCreateWorkerFactory(ptr long ptr ptr ptr ptr ptr long long long) ChpeStubZwCreateWorkerFactory
1548 stdcall ZwDebugActiveProcess(ptr ptr) ChpeAutoZwDebugActiveProcess
1549 stdcall ZwDebugContinue(ptr ptr long) ChpeAutoZwDebugContinue
1550 stdcall ZwDelayExecution(long ptr) ChpeAutoZwDelayExecution
1551 stdcall ZwDeleteAtom(long) ChpeAutoZwDeleteAtom
1552 stdcall ZwDeleteBootEntry(long) ChpeAutoZwDeleteBootEntry
1553 stdcall ZwDeleteDriverEntry(long) ChpeAutoZwDeleteDriverEntry
1554 stdcall ZwDeleteFile(ptr) ChpeAutoZwDeleteFile
1555 stdcall ZwDeleteKey(long) ChpeAutoZwDeleteKey
1556 stdcall ZwDeleteObjectAuditAlarm(ptr ptr long) ChpeAutoZwDeleteObjectAuditAlarm
1557 stdcall -version=0x600+ ZwDeletePrivateNamespace(ptr) ChpeAutoZwDeletePrivateNamespace
1558 stdcall ZwDeleteValueKey(long ptr) ChpeAutoZwDeleteValueKey
1559 stdcall -version=0x602+ ZwDeleteWnfStateData(ptr ptr) ChpeAutoZwDeleteWnfStateData
1560 stdcall -version=0x602+ ZwDeleteWnfStateName(ptr) ChpeAutoZwDeleteWnfStateName
1561 stdcall ZwDeviceIoControlFile(long long long long long long long long long long) ChpeAutoZwDeviceIoControlFile
1562 stdcall ZwDisplayString(ptr) ChpeAutoZwDisplayString
1563 stdcall ZwDuplicateObject(long long long ptr long long long) ChpeAutoZwDuplicateObject
1564 stdcall ZwDuplicateToken(long long long long long long) ChpeAutoZwDuplicateToken
1565 stdcall ZwEnumerateBootEntries(ptr ptr) ChpeAutoZwEnumerateBootEntries
1566 stdcall ZwEnumerateDriverEntries(ptr ptr) ChpeAutoZwEnumerateDriverEntries
1567 stdcall ZwEnumerateKey(long long long ptr long ptr) ChpeAutoZwEnumerateKey
1568 stdcall ZwEnumerateSystemEnvironmentValuesEx(long ptr long) ChpeAutoZwEnumerateSystemEnvironmentValuesEx
1569 stdcall -version=0x600+ ZwEnumerateTransactionObject(ptr long ptr long ptr) ChpeAutoZwEnumerateTransactionObject
1570 stdcall ZwEnumerateValueKey(long long long ptr long ptr) ChpeAutoZwEnumerateValueKey
1571 stdcall ZwExtendSection(ptr ptr) ChpeAutoZwExtendSection
1572 stdcall ZwFilterToken(ptr long ptr ptr ptr ptr) ChpeAutoZwFilterToken
1573 stdcall -version=0x602+ ZwCreateLowBoxToken(ptr ptr long ptr ptr long ptr long ptr) ChpeAutoZwCreateLowBoxToken
1574 stdcall -version=0x600+ ZwCreatePrivateNamespace(ptr long ptr ptr) ChpeAutoZwCreatePrivateNamespace
1575 stdcall ZwFindAtom(ptr long ptr) ChpeAutoZwFindAtom
1576 stdcall ZwFlushBuffersFile(long ptr) ChpeAutoZwFlushBuffersFile
1577 stdcall ZwFlushBuffersFileEx(long long ptr long ptr) ChpeAutoZwFlushBuffersFileEx
1578 stdcall -version=0x600+ ZwFlushInstallUILanguage(long long) ChpeStubZwFlushInstallUILanguage
1579 stdcall ZwFlushInstructionCache(long ptr long) ChpeAutoZwFlushInstructionCache
1580 stdcall ZwFlushKey(long) ChpeAutoZwFlushKey
1581 stdcall -version=0x600+ ZwFlushProcessWriteBuffers() ChpeAutoZwFlushProcessWriteBuffers
1582 stdcall ZwFlushVirtualMemory(ptr ptr ptr ptr) ChpeAutoZwFlushVirtualMemory
1583 stdcall ZwFlushWriteBuffer() ChpeAutoZwFlushWriteBuffer
1584 stdcall ZwFreeUserPhysicalPages(ptr ptr ptr) ChpeAutoZwFreeUserPhysicalPages
1585 stdcall ZwFreeVirtualMemory(long ptr ptr long) ChpeAutoZwFreeVirtualMemory
1586 stdcall -version=0x600+ ZwFreezeRegistry(long) ChpeStubZwFreezeRegistry
1587 stdcall -version=0x600+ ZwFreezeTransactions(ptr ptr) ChpeStubZwFreezeTransactions
1588 stdcall ZwFsControlFile(long long long long long long long long long long) ChpeAutoZwFsControlFile
1589 stdcall ZwGetContextThread(long ptr) ChpeAutoZwGetContextThread
1590 stdcall ZwGetCurrentProcessorNumber() ChpeAutoZwGetCurrentProcessorNumber
1591 stdcall -version=0xA00+ ZwGetCurrentProcessorNumberEx(ptr) ChpeAutoZwGetCurrentProcessorNumberEx
1592 stdcall ZwGetDevicePowerState(ptr ptr) ChpeAutoZwGetDevicePowerState
1593 stdcall -version=0x600+ ZwGetMUIRegistryInfo(long ptr ptr) ChpeStubZwGetMUIRegistryInfo
1594 stdcall -version=0x600+ ZwGetNextProcess(ptr long long long ptr) ChpeStubZwGetNextProcess
1595 stdcall -version=0x600+ ZwGetNextThread(ptr ptr long long long ptr) ChpeAutoZwGetNextThread
1596 stdcall -version=0x600+ ZwGetNlsSectionPtr(long long ptr ptr ptr) ChpeAutoZwGetNlsSectionPtr
1597 stdcall -version=0x600+ ZwGetNotificationResourceManager(ptr ptr long ptr ptr long ptr) ChpeAutoZwGetNotificationResourceManager
1598 stdcall ZwGetPlugPlayEvent(long long ptr long) ChpeAutoZwGetPlugPlayEvent
1599 stdcall ZwGetWriteWatch(long long ptr long ptr ptr ptr) ChpeAutoZwGetWriteWatch
1600 stdcall ZwImpersonateAnonymousToken(ptr) ChpeAutoZwImpersonateAnonymousToken
1601 stdcall ZwImpersonateClientOfPort(ptr ptr) ChpeAutoZwImpersonateClientOfPort
1602 stdcall ZwImpersonateThread(ptr ptr ptr) ChpeAutoZwImpersonateThread
1603 stdcall -version=0x600+ ZwInitializeNlsFiles(ptr ptr ptr ptr) ChpeStubZwInitializeNlsFiles
1604 stdcall ZwInitializeRegistry(long) ChpeAutoZwInitializeRegistry
1605 stdcall ZwInitiatePowerAction(long long long long) ChpeAutoZwInitiatePowerAction
1606 stdcall ZwIsProcessInJob(long long) ChpeAutoZwIsProcessInJob
1607 stdcall ZwIsSystemResumeAutomatic() ChpeAutoZwIsSystemResumeAutomatic
1608 stdcall -version=0x600+ ZwIsUILanguageComitted() ChpeStubZwIsUILanguageComitted
1609 stdcall ZwListenPort(ptr ptr) ChpeAutoZwListenPort
1610 stdcall ZwLoadDriver(ptr) ChpeAutoZwLoadDriver
1611 stdcall ZwLoadKey2(ptr ptr long) ChpeAutoZwLoadKey2
1612 stdcall ZwLoadKey(ptr ptr) ChpeAutoZwLoadKey
1613 stdcall ZwLoadKeyEx(ptr ptr long ptr ptr long ptr ptr) ChpeAutoZwLoadKeyEx
1614 stdcall ZwLockFile(long long ptr ptr ptr ptr ptr ptr long long) ChpeAutoZwLockFile
1615 stdcall ZwLockProductActivationKeys(ptr ptr) ChpeAutoZwLockProductActivationKeys
1616 stdcall ZwLockRegistryKey(ptr) ChpeAutoZwLockRegistryKey
1617 stdcall ZwLockVirtualMemory(long ptr ptr long) ChpeAutoZwLockVirtualMemory
1618 stdcall ZwMakePermanentObject(ptr) ChpeAutoZwMakePermanentObject
1619 stdcall ZwMakeTemporaryObject(long) ChpeAutoZwMakeTemporaryObject
1620 stdcall -version=0x600+ ZwMapCMFModule(long long ptr ptr ptr) ChpeStubZwMapCMFModule
1621 stdcall ZwMapUserPhysicalPages(ptr ptr ptr) ChpeAutoZwMapUserPhysicalPages
1622 stdcall ZwMapUserPhysicalPagesScatter(ptr ptr ptr) ChpeAutoZwMapUserPhysicalPagesScatter
1623 stdcall ZwMapViewOfSection(long long ptr long long ptr ptr long long long) ChpeAutoZwMapViewOfSection
1624 stdcall ZwModifyBootEntry(ptr) ChpeAutoZwModifyBootEntry
1625 stdcall ZwModifyDriverEntry(ptr) ChpeAutoZwModifyDriverEntry
1626 stdcall ZwNotifyChangeDirectoryFile(long long ptr ptr ptr ptr long long long) ChpeAutoZwNotifyChangeDirectoryFile
1627 stdcall ZwNotifyChangeDirectoryFileEx(long long ptr ptr ptr ptr long long long long) ChpeAutoZwNotifyChangeDirectoryFileEx
1628 stdcall ZwNotifyChangeKey(long long ptr ptr ptr long long ptr long long) ChpeAutoZwNotifyChangeKey
1629 stdcall ZwNotifyChangeMultipleKeys(ptr long ptr ptr ptr ptr ptr long long ptr long long) ChpeAutoZwNotifyChangeMultipleKeys
1630 stdcall ZwOpenDirectoryObject(long long long) ChpeAutoZwOpenDirectoryObject
1631 stdcall -version=0x600+ ZwOpenEnlistment(ptr long ptr ptr ptr) ChpeAutoZwOpenEnlistment
1632 stdcall ZwOpenEvent(long long long) ChpeAutoZwOpenEvent
1633 stdcall ZwOpenEventPair(ptr long ptr) ChpeAutoZwOpenEventPair
1634 stdcall ZwOpenFile(ptr long ptr ptr long long) ChpeAutoZwOpenFile
1635 stdcall ZwOpenIoCompletion(ptr long ptr) ChpeAutoZwOpenIoCompletion
1636 stdcall ZwOpenJobObject(ptr long ptr) ChpeAutoZwOpenJobObject
1637 stdcall ZwOpenKey(ptr long ptr) ChpeAutoZwOpenKey
1638 stdcall ZwOpenKeyEx(ptr long ptr long) ChpeAutoZwOpenKeyEx
1639 stdcall -version=0x600+ ZwOpenKeyTransacted(ptr long ptr ptr) ChpeAutoZwOpenKeyTransacted
1640 stdcall ZwOpenKeyedEvent(ptr long ptr) ChpeAutoZwOpenKeyedEvent
1641 stdcall ZwOpenMutant(ptr long ptr) ChpeAutoZwOpenMutant
1642 stdcall ZwOpenObjectAuditAlarm(ptr ptr ptr ptr ptr ptr long long ptr long long ptr) ChpeAutoZwOpenObjectAuditAlarm
1643 stdcall -version=0x600+ ZwOpenPrivateNamespace(ptr long ptr ptr) ChpeAutoZwOpenPrivateNamespace
1644 stdcall ZwOpenProcess(ptr long ptr ptr) ChpeAutoZwOpenProcess
1645 stdcall ZwOpenProcessToken(long long ptr) ChpeAutoZwOpenProcessToken
1646 stdcall ZwOpenProcessTokenEx(long long long ptr) ChpeAutoZwOpenProcessTokenEx
1647 stdcall -version=0x600+ ZwOpenResourceManager(ptr long ptr ptr ptr) ChpeAutoZwOpenResourceManager
1648 stdcall ZwOpenSection(ptr long ptr) ChpeAutoZwOpenSection
1649 stdcall ZwOpenSemaphore(long long ptr) ChpeAutoZwOpenSemaphore
1650 stdcall -version=0x600+ ZwOpenSession(ptr long ptr) ChpeStubZwOpenSession
1651 stdcall ZwOpenSymbolicLinkObject(ptr long ptr) ChpeAutoZwOpenSymbolicLinkObject
1652 stdcall ZwOpenThread(ptr long ptr ptr) ChpeAutoZwOpenThread
1653 stdcall ZwOpenThreadToken(long long long ptr) ChpeAutoZwOpenThreadToken
1654 stdcall ZwOpenThreadTokenEx(long long long long ptr) ChpeAutoZwOpenThreadTokenEx
1655 stdcall ZwOpenTimer(ptr long ptr) ChpeAutoZwOpenTimer
1656 stdcall -version=0x600+ ZwOpenTransaction(ptr long ptr ptr ptr) ChpeAutoZwOpenTransaction
1657 stdcall -version=0x600+ ZwOpenTransactionManager(ptr long ptr ptr ptr long) ChpeAutoZwOpenTransactionManager
1658 stdcall ZwPlugPlayControl(ptr ptr long) ChpeAutoZwPlugPlayControl
1659 stdcall ZwPowerInformation(long ptr long ptr long) ChpeAutoZwPowerInformation
1660 stdcall -version=0x600+ ZwPrePrepareComplete(ptr ptr) ChpeAutoZwPrePrepareComplete
1661 stdcall -version=0x600+ ZwPrePrepareEnlistment(ptr ptr) ChpeAutoZwPrePrepareEnlistment
1662 stdcall -version=0x600+ ZwPrepareComplete(ptr ptr) ChpeAutoZwPrepareComplete
1663 stdcall -version=0x600+ ZwPrepareEnlistment(ptr ptr) ChpeAutoZwPrepareEnlistment
1664 stdcall ZwPrivilegeCheck(ptr ptr ptr) ChpeAutoZwPrivilegeCheck
1665 stdcall ZwPrivilegeObjectAuditAlarm(ptr ptr ptr long ptr long) ChpeAutoZwPrivilegeObjectAuditAlarm
1666 stdcall ZwPrivilegedServiceAuditAlarm(ptr ptr ptr ptr long) ChpeAutoZwPrivilegedServiceAuditAlarm
1667 stdcall -version=0x600+ ZwPropagationComplete(ptr long long ptr) ChpeAutoZwPropagationComplete
1668 stdcall -version=0x600+ ZwPropagationFailed(ptr long long) ChpeAutoZwPropagationFailed
1669 stdcall ZwProtectVirtualMemory(long ptr ptr long ptr) ChpeAutoZwProtectVirtualMemory
1670 stdcall ZwPulseEvent(long ptr) ChpeAutoZwPulseEvent
1671 stdcall ZwQueryAttributesFile(ptr ptr) ChpeAutoZwQueryAttributesFile
1672 stdcall ZwQueryBootEntryOrder(ptr ptr) ChpeAutoZwQueryBootEntryOrder
1673 stdcall ZwQueryBootOptions(ptr ptr) ChpeAutoZwQueryBootOptions
1674 stdcall ZwQueryDebugFilterState(long long) ChpeAutoZwQueryDebugFilterState
1675 stdcall ZwQueryDefaultLocale(long ptr) ChpeAutoZwQueryDefaultLocale
1676 stdcall ZwQueryDefaultUILanguage(ptr) ChpeAutoZwQueryDefaultUILanguage
1677 stdcall ZwQueryDirectoryFile(long long ptr ptr ptr ptr long long long ptr long) ChpeAutoZwQueryDirectoryFile
1678 stdcall ZwQueryDirectoryFileEx(long long ptr ptr ptr ptr long long long ptr) ChpeAutoZwQueryDirectoryFileEx
1679 stdcall ZwQueryDirectoryObject(long ptr long long long ptr ptr) ChpeAutoZwQueryDirectoryObject
1680 stdcall ZwQueryDriverEntryOrder(ptr ptr) ChpeAutoZwQueryDriverEntryOrder
1681 stdcall ZwQueryEaFile(long ptr ptr long long ptr long ptr long) ChpeAutoZwQueryEaFile
1682 stdcall ZwQueryEvent(long long ptr long ptr) ChpeAutoZwQueryEvent
1683 stdcall ZwQueryFullAttributesFile(ptr ptr) ChpeAutoZwQueryFullAttributesFile
1684 stdcall ZwQueryInformationAtom(long long ptr long ptr) ChpeAutoZwQueryInformationAtom
1685 stdcall ZwQueryInformationByName(ptr ptr ptr long long) ChpeAutoZwQueryInformationByName
1686 stdcall -version=0x600+ ZwQueryInformationEnlistment(ptr long ptr long ptr) ChpeAutoZwQueryInformationEnlistment
1687 stdcall ZwQueryInformationFile(long ptr ptr long long) ChpeAutoZwQueryInformationFile
1688 stdcall ZwQueryInformationJobObject(long long ptr long ptr) ChpeAutoZwQueryInformationJobObject
1689 stdcall ZwQueryInformationPort(ptr long ptr long ptr) ChpeAutoZwQueryInformationPort
1690 stdcall ZwQueryInformationProcess(long long ptr long ptr) ChpeAutoZwQueryInformationProcess
1691 stdcall -version=0x600+ ZwQueryInformationResourceManager(ptr long ptr long ptr) ChpeAutoZwQueryInformationResourceManager
1692 stdcall ZwQueryInformationThread(long long ptr long ptr) ChpeAutoZwQueryInformationThread
1693 stdcall ZwQueryInformationToken(long long ptr long ptr) ChpeAutoZwQueryInformationToken
1694 stdcall -version=0x600+ ZwQueryInformationTransaction(ptr long ptr long ptr) ChpeAutoZwQueryInformationTransaction
1695 stdcall -version=0x600+ ZwQueryInformationTransactionManager(ptr long ptr long ptr) ChpeAutoZwQueryInformationTransactionManager
1696 stdcall -version=0x600+ ZwQueryInformationWorkerFactory(ptr long ptr long ptr) ChpeStubZwQueryInformationWorkerFactory
1697 stdcall ZwQueryInstallUILanguage(ptr) ChpeAutoZwQueryInstallUILanguage
1698 stdcall ZwQueryIntervalProfile(long ptr) ChpeAutoZwQueryIntervalProfile
1699 stdcall ZwQueryIoCompletion(long long ptr long ptr) ChpeAutoZwQueryIoCompletion
1700 stdcall ZwQueryKey(long long ptr long ptr) ChpeAutoZwQueryKey
1701 stdcall -version=0x600+ ZwQueryLicenseValue(ptr ptr ptr long ptr) ChpeAutoZwQueryLicenseValue
1702 stdcall ZwQueryMultipleValueKey(long ptr long ptr long ptr) ChpeAutoZwQueryMultipleValueKey
1703 stdcall ZwQueryMutant(long long ptr long ptr) ChpeAutoZwQueryMutant
1704 stdcall ZwQueryObject(long long long long long) ChpeAutoZwQueryObject
1705 stdcall ZwQueryOpenSubKeys(ptr ptr) ChpeAutoZwQueryOpenSubKeys
1706 stdcall ZwQueryOpenSubKeysEx(ptr long ptr ptr) ChpeAutoZwQueryOpenSubKeysEx
1707 stdcall ZwQueryPerformanceCounter(long long) ChpeAutoZwQueryPerformanceCounter
1708 stdcall ZwQueryPortInformationProcess() ChpeAutoZwQueryPortInformationProcess
1709 stdcall ZwQueryQuotaInformationFile(ptr ptr ptr long long ptr long ptr long) ChpeAutoZwQueryQuotaInformationFile
1710 stdcall ZwQuerySection(long long long long long) ChpeAutoZwQuerySection
1711 stdcall ZwQuerySecurityObject(long long long long long) ChpeAutoZwQuerySecurityObject
1712 stdcall ZwQuerySemaphore(long long long long long) ChpeAutoZwQuerySemaphore
1713 stdcall ZwQuerySymbolicLinkObject(long ptr ptr) ChpeAutoZwQuerySymbolicLinkObject
1714 stdcall ZwQuerySystemEnvironmentValue(ptr ptr long ptr) ChpeAutoZwQuerySystemEnvironmentValue
1715 stdcall ZwQuerySystemEnvironmentValueEx(ptr ptr ptr ptr ptr) ChpeAutoZwQuerySystemEnvironmentValueEx
1716 stdcall ZwQuerySystemInformation(long long long long) ChpeAutoZwQuerySystemInformation
1717 stdcall -version=0x601+ ZwQuerySystemInformationEx(long ptr long ptr long ptr) ChpeAutoZwQuerySystemInformationEx
1718 stdcall ZwQuerySystemTime(ptr) ChpeAutoZwQuerySystemTime
1719 stdcall ZwQueryTimer(ptr long ptr long ptr) ChpeAutoZwQueryTimer
1720 stdcall ZwQueryTimerResolution(long long long) ChpeAutoZwQueryTimerResolution
1721 stdcall ZwQueryValueKey(long ptr long ptr long ptr) ChpeAutoZwQueryValueKey
1722 stdcall ZwQueryVirtualMemory(long ptr long ptr long ptr) ChpeAutoZwQueryVirtualMemory
1723 stdcall ZwQueryVolumeInformationFile(long ptr ptr long long) ChpeAutoZwQueryVolumeInformationFile
1724 stdcall -version=0x602+ ZwQueryWnfStateData(ptr ptr ptr ptr ptr ptr) ChpeAutoZwQueryWnfStateData
1725 stdcall -version=0x602+ ZwQueryWnfStateNameInformation(ptr long ptr ptr long) ChpeAutoZwQueryWnfStateNameInformation
1726 stdcall ZwQueueApcThread(long ptr long long long) ChpeAutoZwQueueApcThread
1727 stdcall ZwRaiseException(ptr ptr long) ChpeAutoZwRaiseException
1728 stdcall ZwRaiseHardError(long long long ptr long ptr) ChpeAutoZwRaiseHardError
1729 stdcall ZwReadFile(long long ptr ptr ptr ptr long ptr ptr) ChpeAutoZwReadFile
1730 stdcall ZwReadFileScatter(long long ptr ptr ptr ptr long ptr ptr) ChpeAutoZwReadFileScatter
1731 stdcall -version=0x600+ ZwReadOnlyEnlistment(ptr ptr) ChpeAutoZwReadOnlyEnlistment
1732 stdcall ZwReadRequestData(ptr ptr long ptr long ptr) ChpeAutoZwReadRequestData
1733 stdcall ZwReadVirtualMemory(long ptr ptr long ptr) ChpeAutoZwReadVirtualMemory
1734 stdcall -version=0x600+ ZwRecoverEnlistment(ptr ptr) ChpeAutoZwRecoverEnlistment
1735 stdcall -version=0x600+ ZwRecoverResourceManager(ptr) ChpeAutoZwRecoverResourceManager
1736 stdcall -version=0x600+ ZwRecoverTransactionManager(ptr) ChpeAutoZwRecoverTransactionManager
1737 stdcall -version=0x600+ ZwRegisterProtocolAddressInformation(ptr ptr long ptr long) ChpeAutoZwRegisterProtocolAddressInformation
1738 stdcall ZwRegisterThreadTerminatePort(ptr) ChpeAutoZwRegisterThreadTerminatePort
1739 stdcall -version=0x600+ ZwReleaseCMFViewOwnership() ChpeStubZwReleaseCMFViewOwnership
1740 stdcall ZwReleaseKeyedEvent(ptr ptr long ptr) ChpeAutoZwReleaseKeyedEvent
1741 stdcall ZwReleaseMutant(long ptr) ChpeAutoZwReleaseMutant
1742 stdcall ZwReleaseSemaphore(long long ptr) ChpeAutoZwReleaseSemaphore
1743 stdcall -version=0x600+ ZwReleaseWorkerFactoryWorker(ptr) ChpeStubZwReleaseWorkerFactoryWorker
1744 stdcall ZwRemoveIoCompletion(ptr ptr ptr ptr ptr) ChpeAutoZwRemoveIoCompletion
1745 stdcall -version=0x600+ ZwRemoveIoCompletionEx(ptr ptr long ptr ptr long) ChpeAutoZwRemoveIoCompletionEx
1746 stdcall ZwRemoveProcessDebug(ptr ptr) ChpeAutoZwRemoveProcessDebug
1747 stdcall ZwRenameKey(ptr ptr) ChpeAutoZwRenameKey
1748 stdcall -version=0x600+ ZwRenameTransactionManager(ptr ptr) ChpeAutoZwRenameTransactionManager
1749 stdcall ZwReplaceKey(ptr long ptr) ChpeAutoZwReplaceKey
1750 stdcall -version=0x600+ ZwReplacePartitionUnit(wstr wstr long) ChpeStubZwReplacePartitionUnit
1751 stdcall ZwReplyPort(ptr ptr) ChpeAutoZwReplyPort
1752 stdcall ZwReplyWaitReceivePort(ptr ptr ptr ptr) ChpeAutoZwReplyWaitReceivePort
1753 stdcall ZwReplyWaitReceivePortEx(ptr ptr ptr ptr ptr) ChpeAutoZwReplyWaitReceivePortEx
1754 stdcall ZwReplyWaitReplyPort(ptr ptr) ChpeAutoZwReplyWaitReplyPort
1755 stdcall ZwRequestDeviceWakeup(ptr) ChpeAutoZwRequestDeviceWakeup
1756 stdcall ZwRequestPort(ptr ptr) ChpeAutoZwRequestPort
1757 stdcall ZwRequestWaitReplyPort(ptr ptr ptr) ChpeAutoZwRequestWaitReplyPort
1758 stdcall ZwRequestWakeupLatency(long) ChpeAutoZwRequestWakeupLatency
1759 stdcall ZwResetEvent(long ptr) ChpeAutoZwResetEvent
1760 stdcall ZwResetWriteWatch(long ptr long) ChpeAutoZwResetWriteWatch
1761 stdcall ZwRestoreKey(long long long) ChpeAutoZwRestoreKey
1762 stdcall ZwResumeProcess(ptr) ChpeAutoZwResumeProcess
1763 stdcall ZwResumeThread(long long) ChpeAutoZwResumeThread
1764 stdcall -version=0x600+ ZwRollbackComplete(ptr ptr) ChpeAutoZwRollbackComplete
1765 stdcall -version=0x600+ ZwRollbackEnlistment(ptr ptr) ChpeAutoZwRollbackEnlistment
1766 stdcall -version=0x600+ ZwRollbackTransaction(ptr long) ChpeAutoZwRollbackTransaction
1767 stdcall -version=0x600+ ZwRollforwardTransactionManager(ptr ptr) ChpeAutoZwRollforwardTransactionManager
1768 stdcall ZwSaveKey(long long) ChpeAutoZwSaveKey
1769 stdcall ZwSaveKeyEx(ptr ptr long) ChpeAutoZwSaveKeyEx
1770 stdcall ZwSaveMergedKeys(ptr ptr ptr) ChpeAutoZwSaveMergedKeys
1771 stdcall ZwSecureConnectPort(ptr ptr ptr ptr ptr ptr ptr ptr ptr) ChpeAutoZwSecureConnectPort
1772 stdcall ZwSetBootEntryOrder(ptr ptr) ChpeAutoZwSetBootEntryOrder
1773 stdcall ZwSetBootOptions(ptr long) ChpeAutoZwSetBootOptions
1774 stdcall ZwSetContextThread(long ptr) ChpeAutoZwSetContextThread
1775 stdcall ZwSetDebugFilterState(long long long) ChpeAutoZwSetDebugFilterState
1776 stdcall ZwSetDefaultHardErrorPort(ptr) ChpeAutoZwSetDefaultHardErrorPort
1777 stdcall ZwSetDefaultLocale(long long) ChpeAutoZwSetDefaultLocale
1778 stdcall ZwSetDefaultUILanguage(long) ChpeAutoZwSetDefaultUILanguage
1779 stdcall ZwSetDriverEntryOrder(ptr ptr) ChpeAutoZwSetDriverEntryOrder
1780 stdcall ZwSetEaFile(long ptr ptr long) ChpeAutoZwSetEaFile
1781 stdcall ZwSetEvent(long long) ChpeAutoZwSetEvent
1782 stdcall ZwSetEventBoostPriority(ptr) ChpeAutoZwSetEventBoostPriority
1783 stdcall ZwSetHighEventPair(ptr) ChpeAutoZwSetHighEventPair
1784 stdcall ZwSetHighWaitLowEventPair(ptr) ChpeAutoZwSetHighWaitLowEventPair
1785 stdcall ZwSetInformationDebugObject(ptr long ptr long ptr) ChpeAutoZwSetInformationDebugObject
1786 stdcall -version=0x600+ ZwSetInformationEnlistment(ptr long ptr long) ChpeAutoZwSetInformationEnlistment
1787 stdcall ZwSetInformationFile(long long long long long) ChpeAutoZwSetInformationFile
1788 stdcall ZwSetInformationJobObject(long long ptr long) ChpeAutoZwSetInformationJobObject
1789 stdcall ZwSetInformationKey(long long ptr long) ChpeAutoZwSetInformationKey
1790 stdcall ZwSetInformationObject(long long ptr long) ChpeAutoZwSetInformationObject
1791 stdcall ZwSetInformationProcess(long long long long) ChpeAutoZwSetInformationProcess
1792 stdcall -version=0x600+ ZwSetInformationResourceManager(ptr long ptr long) ChpeAutoZwSetInformationResourceManager
1793 stdcall ZwSetInformationThread(long long ptr long) ChpeAutoZwSetInformationThread
1794 stdcall ZwSetInformationToken(long long ptr long) ChpeAutoZwSetInformationToken
1795 stdcall -version=0x600+ ZwSetInformationTransaction(ptr long ptr long) ChpeAutoZwSetInformationTransaction
1796 stdcall -version=0x600+ ZwSetInformationTransactionManager(ptr long ptr long) ChpeAutoZwSetInformationTransactionManager
1797 stdcall ZwSetInformationVirtualMemory(ptr long ptr ptr ptr long) ChpeAutoZwSetInformationVirtualMemory
1798 stdcall -version=0x600+ ZwSetInformationWorkerFactory(ptr long ptr long) ChpeStubZwSetInformationWorkerFactory
1799 stdcall ZwSetIntervalProfile(long long) ChpeAutoZwSetIntervalProfile
1800 stdcall ZwSetIoCompletion(ptr long ptr long long) ChpeAutoZwSetIoCompletion
1801 stdcall ZwSetLdtEntries(long int64 long int64) ChpeAutoZwSetLdtEntries
1802 stdcall ZwSetLowEventPair(ptr) ChpeAutoZwSetLowEventPair
1803 stdcall ZwSetLowWaitHighEventPair(ptr) ChpeAutoZwSetLowWaitHighEventPair
1804 stdcall ZwSetQuotaInformationFile(ptr ptr ptr long) ChpeAutoZwSetQuotaInformationFile
1805 stdcall ZwSetSecurityObject(long long ptr) ChpeAutoZwSetSecurityObject
1806 stdcall ZwSetSystemEnvironmentValue(ptr ptr) ChpeAutoZwSetSystemEnvironmentValue
1807 stdcall ZwSetSystemEnvironmentValueEx(ptr ptr ptr ptr ptr) ChpeAutoZwSetSystemEnvironmentValueEx
1808 stdcall ZwSetSystemInformation(long ptr long) ChpeAutoZwSetSystemInformation
1809 stdcall ZwSetSystemPowerState(long long long) ChpeAutoZwSetSystemPowerState
1810 stdcall ZwSetSystemTime(ptr ptr) ChpeAutoZwSetSystemTime
1811 stdcall ZwSetThreadExecutionState(long ptr) ChpeAutoZwSetThreadExecutionState
1812 stdcall ZwSetTimer(long ptr ptr ptr long long ptr) ChpeAutoZwSetTimer
1813 stdcall ZwSetTimerResolution(long long ptr) ChpeAutoZwSetTimerResolution
1814 stdcall ZwSetUuidSeed(ptr) ChpeAutoZwSetUuidSeed
1815 stdcall ZwSetValueKey(long long long long long long) ChpeAutoZwSetValueKey
1816 stdcall ZwSetVolumeInformationFile(long ptr ptr long long) ChpeAutoZwSetVolumeInformationFile
1817 stdcall ZwShutdownSystem(long) ChpeAutoZwShutdownSystem
1818 stdcall -version=0x600+ ZwShutdownWorkerFactory(ptr ptr) ChpeStubZwShutdownWorkerFactory
1819 stdcall ZwSignalAndWaitForSingleObject(long long long ptr) ChpeAutoZwSignalAndWaitForSingleObject
1820 stdcall -version=0x600+ ZwSinglePhaseReject(ptr ptr) ChpeAutoZwSinglePhaseReject
1821 stdcall ZwStartProfile(ptr) ChpeAutoZwStartProfile
1822 stdcall ZwStopProfile(ptr) ChpeAutoZwStopProfile
1823 stdcall -version=0x602+ ZwSubscribeWnfStateChange(ptr long long ptr) ChpeAutoZwSubscribeWnfStateChange
1824 stdcall ZwSuspendProcess(ptr) ChpeAutoZwSuspendProcess
1825 stdcall ZwSuspendThread(long ptr) ChpeAutoZwSuspendThread
1826 stdcall ZwSystemDebugControl(long ptr long ptr long ptr) ChpeAutoZwSystemDebugControl
1827 stdcall ZwTerminateJobObject(ptr long) ChpeAutoZwTerminateJobObject
1828 stdcall ZwTerminateProcess(ptr long) ChpeAutoZwTerminateProcess
1829 stdcall ZwTerminateThread(ptr long) ChpeAutoZwTerminateThread
1830 stdcall ZwTestAlert() ChpeAutoZwTestAlert
1831 stdcall -version=0x600+ ZwThawRegistry() ChpeStubZwThawRegistry
1832 stdcall -version=0x600+ ZwThawTransactions() ChpeStubZwThawTransactions
1833 stdcall -version=0x600+ ZwTraceControl(long ptr long ptr long ptr) ChpeAutoZwTraceControl
1834 stdcall ZwTraceEvent(ptr long long ptr) ChpeAutoZwTraceEvent
1835 stdcall ZwTranslateFilePath(ptr long ptr long) ChpeAutoZwTranslateFilePath
1836 stdcall ZwUnloadDriver(ptr) ChpeAutoZwUnloadDriver
1837 stdcall ZwUnloadKey2(ptr long) ChpeAutoZwUnloadKey2
1838 stdcall ZwUnloadKey(long) ChpeAutoZwUnloadKey
1839 stdcall ZwUnloadKeyEx(ptr ptr) ChpeAutoZwUnloadKeyEx
1840 stdcall ZwUnlockFile(long ptr ptr ptr ptr) ChpeAutoZwUnlockFile
1841 stdcall ZwUnlockVirtualMemory(long ptr ptr long) ChpeAutoZwUnlockVirtualMemory
1842 stdcall ZwUnmapViewOfSection(long ptr) ChpeAutoZwUnmapViewOfSection
1843 stdcall -version=0x602+ ZwUnsubscribeWnfStateChange(ptr) ChpeAutoZwUnsubscribeWnfStateChange
1844 stdcall -version=0x602+ ZwUpdateWnfStateData(ptr ptr long ptr ptr long long) ChpeAutoZwUpdateWnfStateData
1845 stdcall ZwVdmControl(long ptr) ChpeAutoZwVdmControl
1846 stdcall ZwWaitForDebugEvent(ptr long ptr ptr) ChpeAutoZwWaitForDebugEvent
1847 stdcall ZwWaitForKeyedEvent(ptr ptr long ptr) ChpeAutoZwWaitForKeyedEvent
1848 stdcall ZwWaitForAlertByThreadId(ptr ptr) ChpeAutoZwWaitForAlertByThreadId
1849 stdcall ZwWaitForMultipleObjects32(long ptr long long ptr) ChpeAutoZwWaitForMultipleObjects32
1850 stdcall ZwWaitForMultipleObjects(long ptr long long ptr) ChpeAutoZwWaitForMultipleObjects
1851 stdcall ZwWaitForSingleObject(long long long) ChpeAutoZwWaitForSingleObject
1852 stdcall -version=0x600+ ZwWaitForWorkViaWorkerFactory(ptr ptr long ptr ptr) ChpeStubZwWaitForWorkViaWorkerFactory
1853 stdcall ZwWaitHighEventPair(ptr) ChpeAutoZwWaitHighEventPair
1854 stdcall ZwWaitLowEventPair(ptr) ChpeAutoZwWaitLowEventPair
1855 stdcall -version=0x600+ ZwWorkerFactoryWorkerReady(ptr) ChpeStubZwWorkerFactoryWorkerReady
1856 stdcall ZwWriteFile(long long ptr ptr ptr ptr long ptr ptr) ChpeAutoZwWriteFile
1857 stdcall ZwWriteFileGather(long long ptr ptr ptr ptr long ptr ptr) ChpeAutoZwWriteFileGather
1858 stdcall ZwWriteRequestData(ptr ptr long ptr long ptr) ChpeAutoZwWriteRequestData
1859 stdcall ZwWriteVirtualMemory(long ptr ptr long ptr) ChpeAutoZwWriteVirtualMemory
1860 stdcall ZwYieldExecution() ChpeAutoZwYieldExecution
1863 cdecl __isascii(long) ChpeAuto__isascii
1864 cdecl __iscsym(long) ChpeAuto__iscsym
1865 cdecl __iscsymf(long) ChpeAuto__iscsymf
1866 cdecl -version=0x600+ __misaligned_access() ChpeStub__misaligned_access
1867 cdecl __toascii(long) ChpeAuto__toascii
1868 cdecl -ret64 _atoi64(str) ChpeAuto_atoi64
1870 extern _fltused ntdll._fltused
1871 cdecl _i64toa(double ptr long) ChpeAuto_i64toa
1872 cdecl _i64tow(double ptr long) ChpeAuto_i64tow
1873 cdecl _itoa(long ptr long) ChpeAuto_itoa
1874 cdecl _itow(long ptr long) ChpeAuto_itow
1877 cdecl _ltoa(long ptr long) ChpeAuto_ltoa
1878 cdecl _ltow(long ptr long) ChpeAuto_ltow
1879 cdecl _memccpy(ptr ptr long long) ChpeAuto_memccpy
1880 cdecl _memicmp(str str long) ChpeAuto_memicmp
1887 cdecl _splitpath(str ptr ptr ptr ptr) ChpeAuto_splitpath
1888 cdecl _strcmpi(str str) ChpeAuto_strcmpi
1889 cdecl _stricmp(str str) ChpeAuto_stricmp
1890 cdecl _strlwr(str) ChpeAuto_strlwr
1891 cdecl _strnicmp(str str long) ChpeAuto_strnicmp
1892 cdecl _strupr(str) ChpeAuto_strupr
1894 cdecl _ui64toa(double ptr long) ChpeAuto_ui64toa
1895 cdecl _ui64tow(double ptr long) ChpeAuto_ui64tow
1896 cdecl _ultoa(long ptr long) ChpeAuto_ultoa
1897 cdecl _ultow(long ptr long) ChpeAuto_ultow
1898 cdecl _vscwprintf(wstr ptr) ChpeAuto_vscwprintf
1899 cdecl _vsnprintf(ptr long str ptr) ChpeAuto_vsnprintf
1900 cdecl _vsnprintf_s(ptr long long str ptr) ChpeAuto_vsnprintf_s
1901 cdecl _vsnwprintf(ptr long wstr ptr) ChpeAuto_vsnwprintf
1902 cdecl _vsnwprintf_s(ptr long long wstr ptr) ChpeAuto_vsnwprintf_s
1903 cdecl -version=0x600+ _vswprintf(ptr wstr ptr) ChpeStub_vswprintf
1904 cdecl _wcsicmp(wstr wstr) ChpeAuto_wcsicmp
1905 cdecl _wcslwr(wstr) ChpeAuto_wcslwr
1906 cdecl _wcsnicmp(wstr wstr long) ChpeAuto_wcsnicmp
1907 cdecl _wcstoui64(wstr ptr long) ChpeAuto_wcstoui64
1908 cdecl _wcsupr(wstr) ChpeAuto_wcsupr
1909 cdecl _wtoi(wstr) ChpeAuto_wtoi
1910 cdecl _wtoi64(wstr) ChpeAuto_wtoi64
1911 cdecl _wtol(wstr) ChpeAuto_wtol
1912 cdecl abs(long) ChpeAutoabs
1913 cdecl atan(double) ChpeAutoatan
1914 cdecl atoi(str) ChpeAutoatoi
1915 cdecl atol(str) ChpeAutoatol
1917 cdecl ceil(double) ChpeAutoceil
1918 cdecl cos(double) ChpeAutocos
1919 cdecl fabs(double) ChpeAutofabs
1920 cdecl floor(double) ChpeAutofloor
1921 cdecl isalnum(long) ChpeAutoisalnum
1922 cdecl isalpha(long) ChpeAutoisalpha
1923 cdecl iscntrl(long) ChpeAutoiscntrl
1924 cdecl isdigit(long) ChpeAutoisdigit
1925 cdecl isgraph(long) ChpeAutoisgraph
1926 cdecl islower(long) ChpeAutoislower
1927 cdecl isprint(long) ChpeAutoisprint
1928 cdecl ispunct(long) ChpeAutoispunct
1929 cdecl isspace(long) ChpeAutoisspace
1930 cdecl isupper(long) ChpeAutoisupper
1931 cdecl iswalpha(long) ChpeAutoiswalpha
1932 cdecl iswctype(long long) ChpeAutoiswctype
1933 cdecl iswdigit(long) ChpeAutoiswdigit
1934 cdecl iswlower(long) ChpeAutoiswlower
1935 cdecl iswspace(long) ChpeAutoiswspace
1936 cdecl iswxdigit(long) ChpeAutoiswxdigit
1937 cdecl isxdigit(long) ChpeAutoisxdigit
1938 cdecl labs(long) ChpeAutolabs
1939 cdecl log(double) ChpeAutolog
1941 cdecl mbstowcs(ptr str long) ChpeAutombstowcs
1942 cdecl memchr(ptr long long) ChpeAutomemchr
1943 cdecl memcmp(ptr ptr long) ChpeAutomemcmp
1945 cdecl memmove(ptr ptr long) ChpeAutomemmove
1946 cdecl memset(ptr long long) ChpeAutomemset
1947 cdecl pow(double double) ChpeAutopow
1949 cdecl sin(double) ChpeAutosin
1951 cdecl sqrt(double) ChpeAutosqrt
1953 cdecl strcat(str str) ChpeAutostrcat
1954 cdecl strchr(str long) ChpeAutostrchr
1955 cdecl strcmp(str str) ChpeAutostrcmp
1956 cdecl strcpy(ptr str) ChpeAutostrcpy
1957 cdecl -version=0x600+ strcpy_s(ptr long str) ChpeAutostrcpy_s
1958 cdecl -version=0x600+ strcat_s(ptr long str) ChpeAutostrcat_s
1959 cdecl -version=0x600+ strncpy_s(ptr long str long) ChpeAutostrncpy_s
1960 cdecl strcspn(str str) ChpeAutostrcspn
1961 cdecl strlen(str) ChpeAutostrlen
1962 cdecl strncat(str str long) ChpeAutostrncat
1963 cdecl strncmp(str str long) ChpeAutostrncmp
1964 cdecl strncpy(ptr str long) ChpeAutostrncpy
1965 cdecl strpbrk(str str) ChpeAutostrpbrk
1966 cdecl strrchr(str long) ChpeAutostrrchr
1967 cdecl strspn(str str) ChpeAutostrspn
1968 cdecl strstr(str str) ChpeAutostrstr
1969 cdecl strtol(str ptr long) ChpeAutostrtol
1970 cdecl strtoul(str ptr long) ChpeAutostrtoul
1972 cdecl tan(double) ChpeAutotan
1973 cdecl tolower(long) ChpeAutotolower
1974 cdecl toupper(long) ChpeAutotoupper
1975 cdecl towlower(long) ChpeAutotowlower
1976 cdecl towupper(long) ChpeAutotowupper
1979 cdecl vsprintf(ptr str ptr) ChpeAutovsprintf
1980 cdecl wcscat(wstr wstr) ChpeAutowcscat
1981 cdecl wcschr(wstr long) ChpeAutowcschr
1982 cdecl wcscmp(wstr wstr) ChpeAutowcscmp
1983 cdecl wcscpy(ptr wstr) ChpeAutowcscpy
1984 cdecl wcscspn(wstr wstr) ChpeAutowcscspn
1985 cdecl wcslen(wstr) ChpeAutowcslen
1986 cdecl wcsncat(wstr wstr long) ChpeAutowcsncat
1987 cdecl wcsncmp(wstr wstr long) ChpeAutowcsncmp
1988 cdecl wcsncpy(ptr wstr long) ChpeAutowcsncpy
1989 cdecl wcsnlen(wstr long) ChpeAutowcsnlen
1990 cdecl wcspbrk(wstr wstr) ChpeAutowcspbrk
1991 cdecl wcsrchr(wstr long) ChpeAutowcsrchr
1992 cdecl wcsspn(wstr wstr) ChpeAutowcsspn
1993 cdecl wcsstr(wstr wstr) ChpeAutowcsstr
1994 cdecl wcstol(wstr ptr long) ChpeAutowcstol
1995 cdecl wcstombs(ptr ptr long) ChpeAutowcstombs
1996 cdecl wcstoul(wstr ptr long) ChpeAutowcstoul
1999 stdcall -version=0x602+ -ret64 RtlGetSystemTimePrecise() ChpeAutoRtlGetSystemTimePrecise
# END typed bridge wrappers
