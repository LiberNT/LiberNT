# PROJECT:     ReactOS ARM64EC runtime
# PURPOSE:     Native NTDLL bridge exports for emulated AMD64 imports
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>

39 stdcall DbgBreakPoint() ChpeDbgBreakPoint
40 varargs DbgPrint(str) ChpeDbgPrint
41 varargs DbgPrintEx(long long str) ChpeDbgPrintEx
61 stdcall -version=0x600+ EtwEventActivityIdControl(long ptr) ChpeEtwEventActivityIdControl
64 stdcall -version=0x600+ EtwEventRegister(ptr ptr ptr ptr) ChpeEtwEventRegister
66 stdcall -version=0x600+ EtwEventUnregister(int64) ChpeEtwEventUnregister
67 stdcall -version=0x600+ EtwEventWrite(int64 ptr long ptr) ChpeEtwEventWrite
73 stdcall -version=0x600+ EtwEventWriteTransfer(int64 ptr ptr ptr long ptr) ChpeEtwEventWriteTransfer
1868 cdecl __C_specific_handler(ptr long ptr ptr) ChpeCSpecificHandler
2008 stdcall ChpeDispatchExceptionNative(ptr ptr)
107 stdcall KiUserExceptionDispatcher(ptr ptr) ChpeKiUserExceptionDispatcher
110 stdcall LdrAccessResource(ptr ptr ptr ptr) ChpeLdrAccessResource
112 stdcall LdrAddRefDll(long ptr) ChpeLdrAddRefDll
114 stdcall LdrEnumResources(ptr ptr long ptr ptr) ChpeLdrEnumResources
116 stdcall LdrFindEntryForAddress(ptr ptr) ChpeLdrFindEntryForAddress
117 stdcall LdrFindResourceDirectory_U(ptr ptr long ptr) ChpeLdrFindResourceDirectory_U
119 stdcall LdrFindResource_U(ptr ptr long ptr) ChpeLdrFindResource_U
121 stdcall LdrGetDllHandle(wstr ptr ptr ptr) ChpeLdrGetDllHandle
122 stdcall LdrGetDllHandleEx(long wstr ptr ptr ptr) ChpeLdrGetDllHandleEx
126 stdcall LdrGetProcedureAddress(ptr ptr long ptr) ChpeLdrGetProcedureAddress
133 stdcall LdrLoadDll(wstr long ptr ptr) ChpeLdrLoadDll
142 stdcall LdrQueryProcessModuleInformation(ptr long ptr) ChpeLdrQueryProcessModuleInformation
145 stdcall -version=0x602+ LdrResolveDelayLoadedAPI(ptr ptr ptr ptr ptr long) ChpeLdrResolveDelayLoadedAPI
146 stdcall -version=0x602+ LdrResolveDelayLoadsFromDll(ptr str long) ChpeLdrResolveDelayLoadsFromDll
159 stdcall LdrUnloadDll(ptr) ChpeLdrUnloadDll
189 stdcall NtAdjustPrivilegesToken(long long ptr long ptr ptr) ChpeAutoNtAdjustPrivilegesToken
198 stdcall NtAllocateVirtualMemory(long ptr ptr ptr long long) ChpeAutoNtAllocateVirtualMemory
199 stdcall NtAllocateVirtualMemoryEx(long ptr ptr long long ptr long) ChpeAutoNtAllocateVirtualMemoryEx
193 stdcall NtAlertThreadByThreadId(long) ChpeAutoNtAlertThreadByThreadId
233 stdcall NtClose(long) ChpeAutoNtClose
244 stdcall NtContinue(ptr long) ChpeNtContinue
245 stdcall -version=0xA00+ NtContinueEx(ptr ptr) ChpeNtContinueEx
252 stdcall NtCreateFile(ptr long ptr ptr ptr long long long long ptr long) ChpeAutoNtCreateFile
264 stdcall NtCreateNamedPipeFile(ptr long ptr ptr long long long long long long long long long ptr) ChpeAutoNtCreateNamedPipeFile
299 stdcall NtDeviceIoControlFile(long long ptr ptr ptr long ptr long ptr long) ChpeAutoNtDeviceIoControlFile
318 stdcall -version=0x600+ NtFlushProcessWriteBuffers() ChpeAutoNtFlushProcessWriteBuffers
316 stdcall NtFlushInstructionCache(long ptr long) ChpeAutoNtFlushInstructionCache
322 stdcall NtFreeVirtualMemory(long ptr ptr long) ChpeAutoNtFreeVirtualMemory
326 stdcall NtGetContextThread(long ptr) ChpeNtGetContextThread
332 stdcall -version=0x600+ NtGetNextThread(ptr ptr long long long ptr) ChpeAutoNtGetNextThread
361 stdcall NtMapViewOfSection(long long ptr long ptr ptr ptr long long long) ChpeAutoNtMapViewOfSection
362 stdcall NtMapViewOfSectionEx(long long ptr ptr ptr long long ptr long) ChpeNtMapViewOfSectionEx
367 stdcall NtNotifyChangeKey(ptr ptr ptr ptr ptr long long ptr long long) ChpeAutoNtNotifyChangeKey
368 stdcall NtNotifyChangeMultipleKeys(ptr long ptr ptr ptr ptr ptr long long ptr long long) ChpeAutoNtNotifyChangeMultipleKeys
373 stdcall NtOpenFile(ptr long ptr ptr long long) ChpeAutoNtOpenFile
408 stdcall NtProtectVirtualMemory(long ptr ptr long ptr) ChpeAutoNtProtectVirtualMemory
416 stdcall NtQueryDirectoryFile(long long ptr ptr ptr ptr long long long ptr long) ChpeAutoNtQueryDirectoryFile
426 stdcall NtQueryInformationFile(long ptr ptr long long) ChpeAutoNtQueryInformationFile
439 stdcall NtQueryKey(long long ptr long ptr) ChpeAutoNtQueryKey
259 stdcall NtCreateKey(ptr long ptr long ptr long ptr) ChpeAutoNtCreateKey
431 stdcall NtQueryInformationThread(long long ptr long ptr) ChpeAutoNtQueryInformationThread
429 stdcall NtQueryInformationProcess(ptr long ptr long ptr) ChpeAutoNtQueryInformationProcess
890 stdcall RtlFormatCurrentUserKeyPath(ptr) ChpeRtlFormatCurrentUserKeyPath
901 stdcall RtlFreeUnicodeString(ptr) ChpeRtlFreeUnicodeString
376 stdcall NtOpenKey(ptr long ptr) ChpeAutoNtOpenKey
838 stdcall NtOpenKeyEx(ptr long ptr long) ChpeAutoNtOpenKeyEx
460 stdcall NtQueryValueKey(long ptr long ptr long ptr) ChpeAutoNtQueryValueKey
557 stdcall NtSetValueKey(long ptr long long ptr long) ChpeAutoNtSetValueKey
293 stdcall NtDeleteKey(long) ChpeAutoNtDeleteKey
296 stdcall NtDeleteValueKey(long ptr) ChpeAutoNtDeleteValueKey
305 stdcall NtEnumerateKey(long long long ptr long ptr) ChpeAutoNtEnumerateKey
308 stdcall NtEnumerateValueKey(long long long ptr long ptr) ChpeAutoNtEnumerateValueKey
317 stdcall NtFlushKey(long) ChpeAutoNtFlushKey
254 stdcall NtCreateWaitCompletionPacket(ptr long ptr) ChpeAutoNtCreateWaitCompletionPacket
255 stdcall NtAssociateWaitCompletionPacket(long long long ptr ptr long long ptr) ChpeAutoNtAssociateWaitCompletionPacket
256 stdcall NtCancelWaitCompletionPacket(long long) ChpeAutoNtCancelWaitCompletionPacket
462 stdcall NtQueryVolumeInformationFile(long ptr ptr long long) ChpeAutoNtQueryVolumeInformationFile
443 stdcall NtQueryObject(long long long long long) ChpeAutoNtQueryObject
455 stdcall NtQuerySystemInformation(long ptr long ptr) ChpeAutoNtQuerySystemInformation
937 stdcall RtlGetNativeSystemInformation(long ptr long ptr) ChpeRtlGetNativeSystemInformation
461 stdcall NtQueryVirtualMemory(long ptr long ptr long ptr) ChpeAutoNtQueryVirtualMemory
468 stdcall NtRaiseException(ptr ptr long) ChpeNtRaiseException
470 stdcall NtReadFile(long long ptr ptr ptr ptr long ptr ptr) ChpeAutoNtReadFile
474 stdcall NtReadVirtualMemory(long ptr ptr long ptr) ChpeAutoNtReadVirtualMemory
515 stdcall NtSetContextThread(long ptr) ChpeNtSetContextThread
566 stdcall NtSuspendProcess(ptr) ChpeAutoNtSuspendProcess
567 stdcall NtSuspendThread(ptr ptr) ChpeAutoNtSuspendThread
504 stdcall NtResumeThread(ptr ptr) ChpeAutoNtResumeThread
570 stdcall NtTerminateProcess(long long) ChpeAutoNtTerminateProcess
571 stdcall NtTerminateThread(long long) ChpeAutoNtTerminateThread
584 stdcall NtUnmapViewOfSection(long ptr) ChpeAutoNtUnmapViewOfSection
585 stdcall NtUnmapViewOfSectionEx(long ptr long) ChpeNtUnmapViewOfSectionEx
591 stdcall NtWaitForAlertByThreadId(ptr ptr) ChpeAutoNtWaitForAlertByThreadId
599 stdcall NtWriteFile(long long ptr ptr ptr ptr long ptr ptr) ChpeAutoNtWriteFile
602 stdcall NtWriteVirtualMemory(long ptr ptr long ptr) ChpeAutoNtWriteVirtualMemory
637 stdcall RtlAddFunctionTable(ptr long long) ChpeRtlAddFunctionTable
638 stdcall RtlAddGrowableFunctionTable(ptr ptr long long long long) ChpeRtlAddGrowableFunctionTable
644 stdcall RtlAddVectoredContinueHandler(long ptr) ChpeRtlAddVectoredContinueHandler
645 stdcall RtlAddVectoredExceptionHandler(long ptr) ChpeRtlAddVectoredExceptionHandler
651 stdcall RtlAllocateHeap(ptr long ptr) ChpeRtlAllocateHeap
912 stdcall RtlGetCurrentPeb() ChpeRtlGetCurrentPeb
944 stdcall RtlGetProcessHeaps(long ptr) ChpeRtlGetProcessHeaps
1126 stdcall RtlQueryEnvironmentVariable(ptr ptr long ptr long ptr) ChpeRtlQueryEnvironmentVariable
615 stdcall RtlAcquirePrivilege(ptr long long ptr) ChpeRtlAcquirePrivilege
618 stdcall RtlAcquireSRWLockExclusive(ptr) ChpeRtlAcquireSRWLockExclusive
619 stdcall RtlAcquireSRWLockShared(ptr) ChpeRtlAcquireSRWLockShared
673 stdcall RtlCaptureContext(ptr) ChpeRtlCaptureContextX64
674 stdcall RtlCaptureStackBackTrace(long long ptr ptr) ChpeRtlCaptureStackBackTrace
690 stdcall RtlCompareMemory(ptr ptr long) ChpeRtlCompareMemory
693 stdcall RtlCompareUnicodeString(ptr ptr long) ChpeRtlCompareUnicodeString
721 stdcall RtlCopyUnicodeString(ptr ptr) ChpeRtlCopyUnicodeString
765 stdcall RtlDecodePointer(ptr) ChpeRtlDecodePointer
766 stdcall RtlDecodeSystemPointer(ptr) ChpeRtlDecodeSystemPointer
776 stdcall RtlDeleteCriticalSection(ptr) ChpeRtlDeleteCriticalSection
779 cdecl RtlDeleteFunctionTable(ptr) ChpeRtlDeleteFunctionTable
780 stdcall RtlDeleteGrowableFunctionTable(ptr) ChpeRtlDeleteGrowableFunctionTable
820 stdcall RtlEnterCriticalSection(ptr) ChpeRtlEnterCriticalSection
818 stdcall RtlEncodePointer(ptr) ChpeRtlEncodePointer
819 stdcall RtlEncodeSystemPointer(ptr) ChpeRtlEncodeSystemPointer
858 stdcall RtlExitUserThread(long) ChpeRtlExitUserThread
863 stdcall RtlFillMemory(ptr long long) ChpeRtlFillMemory
885 stdcall RtlFlsAlloc(ptr ptr) ChpeRtlFlsAlloc
886 stdcall RtlFlsFree(long) ChpeRtlFlsFree
887 stdcall RtlFlsGetValue(long ptr) ChpeRtlFlsGetValue
888 stdcall RtlFlsSetValue(long ptr) ChpeRtlFlsSetValue
896 stdcall RtlFreeHeap(long long long) ChpeRtlFreeHeap
928 stdcall RtlGetFunctionTableListHead() ChpeRtlGetFunctionTableListHead
931 stdcall RtlGetLastNtStatus() ChpeRtlGetLastNtStatus
932 stdcall RtlGetLastWin32Error() ChpeRtlGetLastWin32Error
913 stdcall RtlGetCurrentProcessorNumber() ChpeRtlGetCurrentProcessorNumber
914 stdcall -version=0x601+ RtlGetCurrentProcessorNumberEx(ptr) ChpeRtlGetCurrentProcessorNumberEx
945 stdcall -version=0x600+ RtlGetProductInfo(long long long long ptr) ChpeRtlGetProductInfo
958 stdcall RtlGetVersion(ptr) ChpeRtlGetVersion
1010 stdcall RtlGrowFunctionTable(ptr long) ChpeRtlGrowFunctionTable
983 stdcall RtlInitUnicodeString(ptr wstr) ChpeRtlInitUnicodeString
988 stdcall -version=0x600+ RtlInitializeConditionVariable(ptr) ChpeRtlInitializeConditionVariable
990 stdcall RtlInitializeCriticalSection(ptr) ChpeRtlInitializeCriticalSection
1001 stdcall RtlInitializeSListHead(ptr) ChpeRtlInitializeSListHead
1002 stdcall -version=0x600+ RtlInitializeSRWLock(ptr) ChpeRtlInitializeSRWLock
1009 cdecl RtlInstallFunctionTableCallback(double double long ptr ptr ptr) ChpeRtlInstallFunctionTableCallback
1014 stdcall RtlInterlockedFlushSList(ptr) ChpeRtlInterlockedFlushSList
1015 stdcall RtlInterlockedPopEntrySList(ptr) ChpeRtlInterlockedPopEntrySList
1016 stdcall RtlInterlockedPushEntrySList(ptr ptr) ChpeRtlInterlockedPushEntrySList
1017 stdcall RtlInterlockedPushListSList(ptr ptr ptr long) ChpeRtlInterlockedPushListSList
1018 stdcall -version=0x602+ RtlInterlockedPushListSListEx(ptr ptr ptr long) ChpeRtlInterlockedPushListSListEx
1048 stdcall RtlIsProcessorFeaturePresent(long) ChpeRtlIsProcessorFeaturePresent
1057 stdcall RtlLeaveCriticalSection(ptr) ChpeRtlLeaveCriticalSection
1079 stdcall RtlLookupFunctionEntry(long ptr ptr) ChpeRtlLookupFunctionEntry
1080 stdcall RtlLookupFunctionTable(int64 ptr ptr) ChpeRtlLookupFunctionTable
1070 stdcall -version=0xA00+ RtlLogUnexpectedCodepath(ptr) ChpeRtlLogUnexpectedCodepath
1084 stdcall RtlMoveMemory(ptr ptr long) ChpeRtlMoveMemory
1098 stdcall RtlNtStatusToDosError(long) ChpeRtlNtStatusToDosError
1111 stdcall RtlPcToFileHeader(ptr ptr) ChpeRtlPcToFileHeader
1123 stdcall RtlQueryDepthSList(ptr) ChpeRtlQueryDepthSList
1145 stdcall -norelay RtlRaiseException(ptr) ChpeRtlRaiseException
1146 stdcall RtlRaiseStatus(long) ChpeRtlRaiseStatus
1147 stdcall RtlRandom(ptr) ChpeRtlRandom
1149 stdcall RtlReAllocateHeap(long long ptr long) ChpeRtlReAllocateHeap
1161 stdcall RtlReleasePrivilege(ptr) ChpeRtlReleasePrivilege
1164 stdcall RtlReleaseSRWLockExclusive(ptr) ChpeRtlReleaseSRWLockExclusive
1165 stdcall RtlReleaseSRWLockShared(ptr) ChpeRtlReleaseSRWLockShared
1168 stdcall RtlRemoveVectoredContinueHandler(ptr) ChpeRtlRemoveVectoredContinueHandler
1169 stdcall RtlRemoveVectoredExceptionHandler(ptr) ChpeRtlRemoveVectoredExceptionHandler
1175 stdcall RtlRestoreContext(ptr ptr) ChpeRtlRestoreContext
1176 stdcall RtlRestoreLastWin32Error(long) ChpeRtlRestoreLastWin32Error
1183 stdcall -version=0x600+ RtlRunOnceExecuteOnce(ptr ptr ptr ptr) ChpeRtlRunOnceExecuteOnce
1184 stdcall -version=0x600+ RtlRunOnceInitialize(ptr) ChpeRtlRunOnceInitialize
1235 stdcall RtlSizeHeap(long long ptr) ChpeRtlSizeHeap
1196 stdcall RtlSetCriticalSectionSpinCount(ptr long) ChpeRtlSetCriticalSectionSpinCount
1210 stdcall RtlSetLastWin32Error(long) ChpeRtlSetLastWin32Error
1262 stdcall RtlTryAcquireSRWLockExclusive(ptr) ChpeRtlTryAcquireSRWLockExclusive
1263 stdcall RtlTryAcquireSRWLockShared(ptr) ChpeRtlTryAcquireSRWLockShared
1264 stdcall RtlTryEnterCriticalSection(ptr) ChpeRtlTryEnterCriticalSection
1270 stdcall -version=0x601+ RtlUTF8ToUnicodeN(ptr long ptr str long) ChpeRtlUTF8ToUnicodeN
1288 stdcall RtlUnwind(ptr ptr ptr ptr) ChpeRtlUnwind
1289 stdcall RtlUnwindEx(ptr ptr ptr ptr ptr ptr) ChpeRtlUnwindEx
1313 stdcall RtlVirtualUnwind(long int64 int64 ptr ptr ptr ptr ptr) ChpeRtlVirtualUnwind
1315 stdcall -version=0x602+ RtlWaitOnAddress(ptr ptr long ptr) ChpeRtlWaitOnAddress
1317 stdcall -version=0x602+ RtlWakeAddressAll(ptr) ChpeRtlWakeAddressAll
1318 stdcall -version=0x602+ RtlWakeAddressSingle(ptr) ChpeRtlWakeAddressSingle
1316 stdcall RtlWakeAllConditionVariable(ptr) ChpeRtlWakeAllConditionVariable
1319 stdcall RtlWakeConditionVariable(ptr) ChpeRtlWakeConditionVariable
1344 stdcall RtlZeroMemory(ptr long) ChpeRtlZeroMemory
1398 stdcall -version=0x600+ TpCallbackLeaveCriticalSectionOnCompletion(ptr ptr) ChpeTpCallbackLeaveCriticalSectionOnCompletion
1400 stdcall -version=0x600+ TpCallbackReleaseMutexOnCompletion(ptr ptr) ChpeTpCallbackReleaseMutexOnCompletion
1401 stdcall -version=0x600+ TpCallbackReleaseSemaphoreOnCompletion(ptr ptr long) ChpeTpCallbackReleaseSemaphoreOnCompletion
1404 stdcall -version=0x600+ TpCallbackSetEventOnCompletion(ptr ptr) ChpeTpCallbackSetEventOnCompletion
1405 stdcall -version=0x600+ TpCallbackUnloadDllOnCompletion(ptr ptr) ChpeTpCallbackUnloadDllOnCompletion
1406 stdcall -version=0x600+ TpCancelAsyncIoOperation(ptr) ChpeTpCancelAsyncIoOperation
1411 stdcall -version=0x600+ TpDisassociateCallback(ptr) ChpeTpDisassociateCallback
1412 stdcall -version=0x600+ TpIsTimerSet(ptr) ChpeTpIsTimerSet
1413 stdcall -version=0x600+ TpPostWork(ptr) ChpeTpPostWork
1416 stdcall -version=0x600+ TpReleaseCleanupGroup(ptr) ChpeTpReleaseCleanupGroup
1417 stdcall -version=0x600+ TpReleaseCleanupGroupMembers(ptr long ptr) ChpeTpReleaseCleanupGroupMembers
1418 stdcall -version=0x600+ TpReleaseIoCompletion(ptr) ChpeTpReleaseIoCompletion
1419 stdcall -version=0x600+ TpReleasePool(ptr) ChpeTpReleasePool
1420 stdcall TpReleaseTimer(ptr) ChpeTpReleaseTimer
1421 stdcall TpReleaseWait(ptr) ChpeTpReleaseWait
1422 stdcall -version=0x600+ TpReleaseWork(ptr) ChpeTpReleaseWork
1423 stdcall -version=0x600+ TpSetPoolMaxThreads(ptr long) ChpeTpSetPoolMaxThreads
1424 stdcall -version=0x600+ TpSetPoolMinThreads(ptr long) ChpeTpSetPoolMinThreads
1426 stdcall TpSetTimer(ptr ptr long long) ChpeTpSetTimer
1427 stdcall -version=0x602+ TpSetTimerEx(ptr ptr long long) ChpeTpSetTimerEx
1428 stdcall TpSetWait(ptr long ptr) ChpeTpSetWait
1429 stdcall -version=0x602+ TpSetWaitEx(ptr long ptr ptr) ChpeTpSetWaitEx
1431 stdcall -version=0x600+ TpStartAsyncIoOperation(ptr) ChpeTpStartAsyncIoOperation
1433 stdcall -version=0x600+ TpWaitForIoCompletion(ptr long) ChpeTpWaitForIoCompletion
1434 stdcall TpWaitForTimer(ptr long) ChpeTpWaitForTimer
1435 stdcall -version=0x600+ TpWaitForWait(ptr long) ChpeTpWaitForWait
1436 stdcall -version=0x600+ TpWaitForWork(ptr long) ChpeTpWaitForWork
1437 stdcall -ret64 VerSetConditionMask(double long long) ChpeVerSetConditionMask
1890 varargs _snprintf(ptr long str) ChpeSnprintf
1892 varargs _snwprintf(ptr long wstr) ChpeSnwprintf
1900 varargs _swprintf(ptr wstr) ChpeSwprintf
1958 varargs sprintf(ptr str) ChpeSprintf
1979 varargs swprintf(ptr wstr) ChpeSwprintf
1869 cdecl __chkstk() ChpeChkStk
1883 cdecl _local_unwind(ptr ptr) ChpeLocalUnwind
1952 cdecl memcpy(ptr ptr long) ChpeMemcpy
2009 stdcall ChpeEmulationDispatch(ptr)
1333 stdcall RtlWow64GetThreadSelectorEntry(ptr ptr long ptr) ChpeRtlWow64GetThreadSelectorEntry

276 stdcall NtCreateThread(ptr long ptr long ptr ptr ptr long) ChpeNtCreateThread
325 stdcall NtFsControlFile(long long ptr ptr ptr long ptr long ptr long) ChpeAutoNtFsControlFile
554 stdcall NtSetTimer(long ptr ptr ptr long long ptr) ChpeAutoNtSetTimer
756 stdcall RtlCreateUserThread(long ptr long long ptr ptr ptr ptr ptr) ChpeRtlCreateUserThread
768 stdcall RtlDecompressFragment(long ptr long ptr long long ptr ptr) ChpeRtlDecompressFragment
904 stdcall RtlGenerate8dot3Name(ptr long ptr ptr) ChpeRtlGenerate8dot3Name
934 stdcall RtlGetLengthWithoutLastFullDosOrNtPathElement(long ptr ptr) ChpeRtlGetLengthWithoutLastFullDosOrNtPathElement
954 stdcall RtlGetUnloadEventTrace() ChpeRtlGetUnloadEventTrace
1144 stdcall RtlQueueWorkItem(ptr ptr long) ChpeRtlQueueWorkItem
1157 stdcall RtlRegisterWait(ptr ptr ptr ptr long long) ChpeRtlRegisterWait
1346 stdcall RtlpApplyLengthFunction(long long ptr ptr) ChpeRtlpApplyLengthFunction
1985 stdcall vDbgPrintEx(long long str ptr) ChpevDbgPrintEx
1986 stdcall vDbgPrintExWithPrefix(str long long str ptr) ChpevDbgPrintExWithPrefix
837 stdcall LdrGetDllFullName(ptr ptr) ChpeLdrGetDllFullName
874 stdcall RtlFindExportedRoutineByName(ptr str) ChpeRtlFindExportedRoutineByName
840 stdcall RtlIsEcCode(ptr) ChpeRtlIsEcCode
849 stdcall RtlLocateExtendedFeature(ptr long ptr) ChpeRtlLocateExtendedFeature
850 stdcall RtlLocateExtendedFeature2(ptr long ptr ptr) ChpeRtlLocateExtendedFeature2
852 stdcall RtlQueryPerformanceCounter(ptr) ChpeRtlQueryPerformanceCounter
853 stdcall RtlQueryPerformanceFrequency(ptr) ChpeRtlQueryPerformanceFrequency
855 stdcall RtlQuerySystemTime(ptr) ChpeRtlQuerySystemTime
856 stdcall RtlSystemTimeToTimeFields(ptr ptr) ChpeRtlSystemTimeToTimeFields
42 varargs DbgPrintReturnControlC(str) ChpeDbgPrintReturnControlC
56 stdcall DbgUserBreakPoint() ChpeDbgUserBreakPoint
89 varargs EtwTraceMessage(int64 long ptr long) ChpeEtwTraceMessage
1304 stdcall RtlUserThreadStart(long long) ntdll.RtlUserThreadStart
1960 varargs sscanf(str str) ChpeSscanf
2005 cdecl ChpeVsscanf(str str ptr) ChpeAutoVsscanf
2006 stdcall ChpeVDbgPrintReturnControlC(str ptr) ChpeAutoVDbgPrintReturnControlC
1876 cdecl -private _errno() ChpeErrno
1882 cdecl _lfind(ptr ptr ptr long ptr) ChpeLfind
1924 cdecl bsearch(ptr ptr long long ptr) ChpeBsearch
1956 cdecl qsort(ptr long long ptr) ChpeQsort
1888 cdecl _setjmp(ptr ptr) ChpeSetJmpX64
1889 cdecl _setjmpex(ptr ptr) ChpeSetJmpX64
1948 cdecl longjmp(ptr long) ChpeLongJmp
977 stdcall RtlInitMemoryStream(ptr) ChpeRtlInitMemoryStream
979 stdcall RtlInitOutOfProcessMemoryStream(ptr) ChpeRtlInitOutOfProcessMemoryStream
1159 stdcall RtlReleaseMemoryStream(ptr) ChpeRtlReleaseMemoryStream
98 stdcall ExpInterlockedPopEntrySListEnd() ChpeUnsupportedKernelEntry
100 stdcall ExpInterlockedPopEntrySListFault() ChpeUnsupportedKernelEntry
102 stdcall ExpInterlockedPopEntrySListResume() ChpeUnsupportedKernelEntry
105 stdcall KiUserApcDispatcher(ptr ptr ptr ptr) ChpeUnsupportedApcEntry
109 stdcall KiUserEmulationDispatcher(ptr) ChpeUnsupportedEmulationEntry

# BEGIN typed bridge wrappers (generate_chpe_bridge.py)
1 stdcall ApiSetQueryApiSetPresence(ptr ptr) ChpeAutoApiSetQueryApiSetPresence
2 stdcall -version=0x600+ RtlFlushHeaps() ChpeAutoRtlFlushHeaps
3 stdcall -version=0x602+ RtlQueryWnfStateData(ptr int64 ptr ptr ptr long) ChpeAutoRtlQueryWnfStateData
4 stdcall -version=0x602+ RtlSubscribeWnfStateChangeNotification(ptr int64 long ptr ptr ptr long long long) ChpeAutoRtlSubscribeWnfStateChangeNotification
5 stdcall -version=0x602+ RtlUnsubscribeWnfNotificationWaitForCompletion(ptr) ChpeAutoRtlUnsubscribeWnfNotificationWaitForCompletion
6 stdcall -version=0x602+ RtlUnsubscribeWnfStateChangeNotification(ptr) ChpeAutoRtlUnsubscribeWnfStateChangeNotification
7 stdcall -version=0x600+ A_SHAFinal(ptr ptr) ChpeAutoA_SHAFinal
8 stdcall -version=0x600+ A_SHAInit(ptr) ChpeAutoA_SHAInit
9 stdcall -version=0x600+ A_SHAUpdate(ptr ptr long) ChpeAutoA_SHAUpdate
10 stdcall -version=0x600+ AlpcAdjustCompletionListConcurrencyCount(ptr long) ChpeAutoAlpcAdjustCompletionListConcurrencyCount
11 stdcall -version=0x600+ AlpcFreeCompletionListMessage(ptr ptr) ChpeAutoAlpcFreeCompletionListMessage
12 stdcall -version=0x600+ AlpcGetCompletionListLastMessageInformation(ptr ptr ptr) ChpeAutoAlpcGetCompletionListLastMessageInformation
13 stdcall -version=0x600+ AlpcGetCompletionListMessageAttributes(ptr ptr) ChpeAutoAlpcGetCompletionListMessageAttributes
14 stdcall -version=0x600+ AlpcGetHeaderSize(long) ChpeAutoAlpcGetHeaderSize
15 stdcall -version=0x600+ AlpcGetMessageAttribute(ptr long) ChpeAutoAlpcGetMessageAttribute
16 stdcall -version=0x600+ AlpcGetMessageFromCompletionList(ptr ptr) ChpeAutoAlpcGetMessageFromCompletionList
17 stdcall -version=0x600+ AlpcGetOutstandingCompletionListMessageCount(ptr) ChpeAutoAlpcGetOutstandingCompletionListMessageCount
18 stdcall -version=0x600+ AlpcInitializeMessageAttribute(long ptr long ptr) ChpeAutoAlpcInitializeMessageAttribute
19 stdcall -version=0x600+ AlpcMaxAllowedMessageLength() ChpeAutoAlpcMaxAllowedMessageLength
20 stdcall -version=0x600+ AlpcRegisterCompletionList(ptr ptr long long long) ChpeAutoAlpcRegisterCompletionList
21 stdcall -version=0x600+ AlpcRegisterCompletionListWorkerThread(ptr) ChpeAutoAlpcRegisterCompletionListWorkerThread
22 stdcall -version=0x600+ AlpcRundownCompletionList(ptr) ChpeAutoAlpcRundownCompletionList
23 stdcall -version=0x600+ AlpcUnregisterCompletionList(ptr) ChpeAutoAlpcUnregisterCompletionList
24 stdcall -version=0x600+ AlpcUnregisterCompletionListWorkerThread(ptr) ChpeAutoAlpcUnregisterCompletionListWorkerThread
25 stdcall CsrAllocateCaptureBuffer(long long) ChpeAutoCsrAllocateCaptureBuffer
26 stdcall CsrAllocateMessagePointer(ptr long ptr) ChpeAutoCsrAllocateMessagePointer
27 stdcall CsrCaptureMessageBuffer(ptr ptr long ptr) ChpeAutoCsrCaptureMessageBuffer
28 stdcall CsrCaptureMessageMultiUnicodeStringsInPlace(ptr long ptr) ChpeAutoCsrCaptureMessageMultiUnicodeStringsInPlace
29 stdcall CsrCaptureMessageString(ptr str long long ptr) ChpeAutoCsrCaptureMessageString
30 stdcall CsrCaptureTimeout(long ptr) ChpeAutoCsrCaptureTimeout
31 stdcall CsrClientCallServer(ptr ptr long long) ChpeAutoCsrClientCallServer
32 stdcall CsrClientConnectToServer(str long ptr ptr ptr) ChpeAutoCsrClientConnectToServer
33 stdcall CsrFreeCaptureBuffer(ptr) ChpeAutoCsrFreeCaptureBuffer
34 stdcall CsrGetProcessId() ChpeAutoCsrGetProcessId
35 stdcall CsrIdentifyAlertableThread() ChpeAutoCsrIdentifyAlertableThread
36 stdcall -version=0x502+ CsrNewThread() ChpeAutoCsrNewThread
37 stdcall CsrSetPriorityClass(ptr ptr) ChpeAutoCsrSetPriorityClass
38 stdcall -version=0x600+ CsrVerifyRegion(ptr long) ChpeStubCsrVerifyRegion
43 stdcall DbgPrompt(ptr ptr long) ChpeAutoDbgPrompt
44 stdcall DbgQueryDebugFilterState(long long) ChpeAutoDbgQueryDebugFilterState
45 stdcall DbgSetDebugFilterState(long long long) ChpeAutoDbgSetDebugFilterState
46 stdcall DbgUiConnectToDbg() ChpeAutoDbgUiConnectToDbg
47 stdcall DbgUiContinue(ptr long) ChpeAutoDbgUiContinue
48 stdcall DbgUiConvertStateChangeStructure(ptr ptr) ChpeAutoDbgUiConvertStateChangeStructure
49 stdcall DbgUiDebugActiveProcess(ptr) ChpeAutoDbgUiDebugActiveProcess
50 stdcall DbgUiGetThreadDebugObject() ChpeAutoDbgUiGetThreadDebugObject
51 stdcall DbgUiIssueRemoteBreakin(ptr) ChpeAutoDbgUiIssueRemoteBreakin
52 stdcall DbgUiRemoteBreakin() ChpeAutoDbgUiRemoteBreakin
53 stdcall DbgUiSetThreadDebugObject(ptr) ChpeAutoDbgUiSetThreadDebugObject
54 stdcall DbgUiStopDebugging(ptr) ChpeAutoDbgUiStopDebugging
55 stdcall DbgUiWaitStateChange(ptr ptr) ChpeAutoDbgUiWaitStateChange
57 stdcall -version=0xA00+ EtwCheckCoverage(ptr) ChpeAutoEtwCheckCoverage
58 stdcall  EtwCreateTraceInstanceId(ptr ptr) ChpeStubEtwCreateTraceInstanceId
59 stdcall -version=0x600+ EtwDeliverDataBlock(long) ChpeStubEtwDeliverDataBlock
60 stdcall -version=0x600+ EtwEnumerateProcessRegGuids(ptr long ptr) ChpeStubEtwEnumerateProcessRegGuids
62 stdcall -version=0x600+ EtwEventEnabled(int64 ptr) ChpeAutoEtwEventEnabled
63 stdcall -version=0x600+ EtwEventProviderEnabled(int64 long int64) ChpeAutoEtwEventProviderEnabled
65 stdcall -version=0x600+ EtwEventSetInformation(int64 long ptr long) ChpeAutoEtwEventSetInformation
68 stdcall -version=0x601+ EtwEventWriteEx(int64 ptr int64 long ptr ptr long ptr) ChpeAutoEtwEventWriteEx
69 stdcall -version=0x600+ EtwEventWriteEndScenario(long ptr long long) ChpeStubEtwEventWriteEndScenario
70 stdcall -version=0x600+ EtwEventWriteFull(long long long long long long long) ChpeStubEtwEventWriteFull
71 stdcall -version=0x600+ EtwEventWriteStartScenario(long ptr long long) ChpeStubEtwEventWriteStartScenario
72 stdcall -version=0x600+ EtwEventWriteString(int64 long int64 wstr) ChpeAutoEtwEventWriteString
74 stdcall EtwGetTraceEnableFlags(double) ChpeAutoEtwGetTraceEnableFlags
75 stdcall EtwGetTraceEnableLevel(double) ChpeAutoEtwGetTraceEnableLevel
76 stdcall EtwGetTraceLoggerHandle(ptr) ChpeAutoEtwGetTraceLoggerHandle
77 stdcall -version=0x600+ EtwLogTraceEvent(long long) ChpeStubEtwLogTraceEvent
78 stdcall -version=0x600+ EtwNotificationRegister(ptr long long long ptr) ChpeStubEtwNotificationRegister
79 stdcall -version=0x600+ EtwNotificationUnregister(long ptr) ChpeStubEtwNotificationUnregister
80 stdcall -version=0x600+ EtwProcessPrivateLoggerRequest(ptr) ChpeStubEtwProcessPrivateLoggerRequest
81 stdcall -version=0x600+ EtwRegister(ptr ptr ptr ptr) ChpeStubEtwRegister
82 stdcall -version=0x600+ EtwRegisterSecurityProvider() ChpeStubEtwRegisterSecurityProvider
83 stdcall EtwRegisterTraceGuidsA(ptr ptr ptr long ptr str str ptr) ChpeAutoEtwRegisterTraceGuidsA
84 stdcall EtwRegisterTraceGuidsW(ptr ptr ptr long ptr wstr wstr ptr) ChpeAutoEtwRegisterTraceGuidsW
85 stdcall -version=0x600+ EtwReplyNotification(long) ChpeStubEtwReplyNotification
86 stdcall -version=0x600+ EtwSendNotification(long long ptr long long) ChpeStubEtwSendNotification
87 stdcall -version=0x600+ EtwSetMark(long long long) ChpeStubEtwSetMark
88 stdcall  EtwTraceEventInstance(double ptr ptr ptr) ChpeStubEtwTraceEventInstance
90 stdcall  EtwTraceMessageVa(int64 long ptr long ptr) ChpeStubEtwTraceMessageVa
91 stdcall -version=0x600+ EtwUnregister(int64) ChpeStubEtwUnregister
92 stdcall EtwUnregisterTraceGuids(double) ChpeAutoEtwUnregisterTraceGuids
93 stdcall -version=0x600+ EtwWrite(int64 ptr ptr long ptr) ChpeStubEtwWrite
94 stdcall -version=0x600+ EtwWriteUMSecurityEvent(ptr long long long) ChpeStubEtwWriteUMSecurityEvent
95 stdcall -version=0x600+ EtwpCreateEtwThread(long long) ChpeStubEtwpCreateEtwThread
96 stdcall -version=0x600+ EtwpGetCpuSpeed(ptr) ChpeStubEtwpGetCpuSpeed
97 stdcall -version=0x600+ EtwpNotificationThread() ChpeStubEtwpNotificationThread
99 stub -version=0x600+ ExpInterlockedPopEntrySListEnd8
101 stub -version=0x600+ ExpInterlockedPopEntrySListFault8
103 stub -version=0x600+ ExpInterlockedPopEntrySListResume8
104 stdcall KiRaiseUserExceptionDispatcher() ChpeAutoKiRaiseUserExceptionDispatcher
106 stdcall KiUserCallbackDispatcher(ptr ptr long) ChpeAutoKiUserCallbackDispatcher
108 stdcall KiUserExceptionDispatcherWorker(ptr ptr) ChpeAutoKiUserExceptionDispatcherWorker
111 stdcall -version=0x600+ LdrAddLoadAsDataTable(ptr wstr long ptr) ChpeStubLdrAddLoadAsDataTable
113 stdcall LdrDisableThreadCalloutsForDll(ptr) ChpeAutoLdrDisableThreadCalloutsForDll
115 stdcall LdrEnumerateLoadedModules(long ptr ptr) ChpeAutoLdrEnumerateLoadedModules
118 stdcall  LdrFindResourceEx_U(ptr ptr ptr ptr ptr) ChpeStubLdrFindResourceEx_U
120 stdcall LdrFlushAlternateResourceModules() ChpeAutoLdrFlushAlternateResourceModules
123 stdcall -version=0x600+ LdrGetFailureData() ChpeStubLdrGetFailureData
124 stdcall -version=0x600+ LdrGetFileNameFromLoadAsDataTable(ptr ptr) ChpeStubLdrGetFileNameFromLoadAsDataTable
125 stdcall -version=0x600+ LdrGetKnownDllSectionHandle(wstr long ptr) ChpeStubLdrGetKnownDllSectionHandle
127 stdcall -version=0x600+ LdrGetProcedureAddressEx(ptr ptr long ptr long) ChpeAutoLdrGetProcedureAddressEx
128 stdcall  LdrHotPatchRoutine(ptr) ChpeStubLdrHotPatchRoutine
129 stdcall LdrInitShimEngineDynamic(ptr) ChpeAutoLdrInitShimEngineDynamic
130 stdcall LdrInitializeThunk(long long long long) ChpeAutoLdrInitializeThunk
131 stdcall LdrLoadAlternateResourceModule(ptr ptr) ChpeAutoLdrLoadAlternateResourceModule
132 stdcall -version=0x600+ LdrLoadAlternateResourceModuleEx(long long ptr ptr long) ChpeStubLdrLoadAlternateResourceModuleEx
134 stdcall LdrLockLoaderLock(long ptr ptr) ChpeAutoLdrLockLoaderLock
135 stdcall LdrOpenImageFileOptionsKey(ptr long ptr) ChpeAutoLdrOpenImageFileOptionsKey
136 stdcall -version=0x600+ LdrProcessInitializationComplete() ChpeStubLdrProcessInitializationComplete
137 stdcall LdrProcessRelocationBlock(ptr long ptr long) ChpeAutoLdrProcessRelocationBlock
138 stdcall LdrQueryImageFileExecutionOptions(ptr str long ptr long ptr) ChpeAutoLdrQueryImageFileExecutionOptions
139 stdcall LdrQueryImageFileExecutionOptionsEx(ptr ptr long ptr long ptr long) ChpeAutoLdrQueryImageFileExecutionOptionsEx
140 stdcall LdrQueryImageFileKeyOption(ptr ptr long ptr long ptr) ChpeAutoLdrQueryImageFileKeyOption
141 stdcall -version=0x600+ LdrQueryModuleServiceTags(ptr ptr ptr) ChpeStubLdrQueryModuleServiceTags
143 stdcall -version=0x600+ LdrRegisterDllNotification(long ptr ptr ptr) ChpeAutoLdrRegisterDllNotification
144 stdcall -version=0x600+ LdrRemoveLoadAsDataTable(ptr ptr ptr long) ChpeStubLdrRemoveLoadAsDataTable
147 stdcall -version=0x600+ LdrResFindResource(ptr long long long ptr ptr ptr ptr long) ChpeAutoLdrResFindResource
148 stdcall -version=0x600+ LdrResFindResourceDirectory(ptr long long ptr ptr ptr long ptr) ChpeAutoLdrResFindResourceDirectory
149 stdcall -version=0x600+ LdrResRelease(ptr ptr long long) ChpeStubLdrResRelease
150 stdcall -version=0x600+ LdrResSearchResource(wstr wstr long long long ptr long long) ChpeStubLdrResSearchResource
151 stdcall LdrSetAppCompatDllRedirectionCallback(long ptr ptr) ChpeAutoLdrSetAppCompatDllRedirectionCallback
152 stdcall LdrSetDllManifestProber(ptr) ChpeAutoLdrSetDllManifestProber
153 stdcall -version=0x600+ LdrSetMUICacheType(long) ChpeStubLdrSetMUICacheType
154 stdcall LdrShutdownProcess() ChpeAutoLdrShutdownProcess
155 stdcall LdrShutdownThread() ChpeAutoLdrShutdownThread
156 extern LdrSystemDllInitBlock ntdll.LdrSystemDllInitBlock
157 stdcall LdrUnloadAlternateResourceModule(ptr) ChpeAutoLdrUnloadAlternateResourceModule
158 stdcall -version=0x600+ LdrUnloadAlternateResourceModuleEx(long long) ChpeStubLdrUnloadAlternateResourceModuleEx
160 stdcall LdrUnlockLoaderLock(long ptr) ChpeAutoLdrUnlockLoaderLock
161 stdcall -version=0x600+ LdrUnregisterDllNotification(ptr) ChpeAutoLdrUnregisterDllNotification
162 stdcall LdrVerifyImageMatchesChecksum(ptr long long long) ChpeAutoLdrVerifyImageMatchesChecksum
163 stdcall -version=0x600+ LdrVerifyImageMatchesChecksumEx(ptr ptr) ChpeStubLdrVerifyImageMatchesChecksumEx
164 stdcall -version=0x600+ LdrpResGetMappingSize(long ptr long long) ChpeStubLdrpResGetMappingSize
165 stdcall -version=0x600+ LdrpResGetRCConfig(long long ptr long long) ChpeStubLdrpResGetRCConfig
166 stdcall -version=0x600+ LdrpResGetResourceDirectory(long long long ptr ptr) ChpeStubLdrpResGetResourceDirectory
167 stdcall -version=0x600+ MD4Final(ptr) ChpeAutoMD4Final
168 stdcall -version=0x600+ MD4Init(ptr) ChpeAutoMD4Init
169 stdcall -version=0x600+ MD4Update(ptr ptr long) ChpeAutoMD4Update
170 stdcall -version=0x600+ MD5Final(ptr) ChpeAutoMD5Final
171 stdcall -version=0x600+ MD5Init(ptr) ChpeAutoMD5Init
172 stdcall -version=0x600+ MD5Update(ptr ptr long) ChpeAutoMD5Update
173 extern NlsAnsiCodePage ntdll.NlsAnsiCodePage
174 extern NlsMbCodePageTag ntdll.NlsMbCodePageTag
175 extern NlsMbOemCodePageTag ntdll.NlsMbOemCodePageTag
176 stdcall NtAcceptConnectPort(ptr long ptr long long ptr) ChpeAutoNtAcceptConnectPort
177 stdcall NtAccessCheck(ptr long long ptr ptr ptr ptr ptr) ChpeAutoNtAccessCheck
178 stdcall NtAccessCheckAndAuditAlarm(ptr long ptr ptr ptr long ptr long ptr ptr ptr) ChpeAutoNtAccessCheckAndAuditAlarm
179 stdcall NtAccessCheckByType(ptr ptr ptr long ptr long ptr ptr long ptr ptr) ChpeAutoNtAccessCheckByType
180 stdcall NtAccessCheckByTypeAndAuditAlarm(ptr ptr ptr ptr ptr ptr long long long ptr long ptr long ptr ptr ptr) ChpeAutoNtAccessCheckByTypeAndAuditAlarm
181 stdcall NtAccessCheckByTypeResultList(ptr ptr ptr long ptr long ptr ptr long ptr ptr) ChpeAutoNtAccessCheckByTypeResultList
182 stdcall NtAccessCheckByTypeResultListAndAuditAlarm(ptr ptr ptr ptr ptr ptr long long long ptr long ptr long ptr ptr ptr) ChpeAutoNtAccessCheckByTypeResultListAndAuditAlarm
183 stdcall NtAccessCheckByTypeResultListAndAuditAlarmByHandle(ptr ptr ptr ptr ptr ptr ptr long long long ptr long ptr long ptr ptr ptr) ChpeAutoNtAccessCheckByTypeResultListAndAuditAlarmByHandle
184 stdcall -version=0x600+ NtAcquireCMFViewOwnership(ptr ptr long) ChpeStubNtAcquireCMFViewOwnership
185 stdcall NtAddAtom(ptr long ptr) ChpeAutoNtAddAtom
186 stdcall NtAddBootEntry(ptr long) ChpeAutoNtAddBootEntry
187 stdcall NtAddDriverEntry(ptr long) ChpeAutoNtAddDriverEntry
188 stdcall NtAdjustGroupsToken(long long ptr long ptr ptr) ChpeAutoNtAdjustGroupsToken
190 stdcall -version=0xA00+ NtAlertMultipleThreadByThreadId(ptr long ptr ptr) ChpeAutoNtAlertMultipleThreadByThreadId
191 stdcall NtAlertResumeThread(long ptr) ChpeAutoNtAlertResumeThread
192 stdcall NtAlertThread(long) ChpeAutoNtAlertThread
194 stdcall NtAllocateLocallyUniqueId(ptr) ChpeAutoNtAllocateLocallyUniqueId
195 stdcall -version=0x600+ NtAllocateReserveObject(ptr ptr long) ChpeAutoNtAllocateReserveObject
196 stdcall NtAllocateUserPhysicalPages(ptr ptr ptr) ChpeAutoNtAllocateUserPhysicalPages
197 stdcall NtAllocateUuids(ptr ptr ptr ptr) ChpeAutoNtAllocateUuids
200 stdcall -version=0x600+ NtAlpcAcceptConnectPort(ptr ptr long ptr ptr ptr ptr ptr long) ChpeAutoNtAlpcAcceptConnectPort
201 stdcall -version=0x600+ NtAlpcCancelMessage(ptr long ptr) ChpeAutoNtAlpcCancelMessage
202 stdcall -version=0x600+ NtAlpcConnectPort(ptr ptr ptr ptr long ptr ptr ptr ptr ptr ptr) ChpeAutoNtAlpcConnectPort
203 stdcall -version=0x602+ NtAlpcConnectPortEx(ptr ptr ptr ptr long ptr ptr ptr ptr ptr ptr) ChpeAutoNtAlpcConnectPortEx
204 stdcall -version=0x600+ NtAlpcCreatePort(ptr ptr ptr) ChpeAutoNtAlpcCreatePort
205 stdcall -version=0x600+ NtAlpcCreatePortSection(ptr long ptr long ptr ptr) ChpeAutoNtAlpcCreatePortSection
206 stdcall -version=0x600+ NtAlpcCreateResourceReserve(ptr long long ptr) ChpeAutoNtAlpcCreateResourceReserve
207 stdcall -version=0x600+ NtAlpcCreateSectionView(ptr long ptr) ChpeAutoNtAlpcCreateSectionView
208 stdcall -version=0x600+ NtAlpcCreateSecurityContext(ptr long ptr) ChpeAutoNtAlpcCreateSecurityContext
209 stdcall -version=0x600+ NtAlpcDeletePortSection(ptr long ptr) ChpeAutoNtAlpcDeletePortSection
210 stdcall -version=0x600+ NtAlpcDeleteResourceReserve(ptr long long) ChpeAutoNtAlpcDeleteResourceReserve
211 stdcall -version=0x600+ NtAlpcDeleteSectionView(ptr long ptr) ChpeAutoNtAlpcDeleteSectionView
212 stdcall -version=0x600+ NtAlpcDeleteSecurityContext(ptr long ptr) ChpeAutoNtAlpcDeleteSecurityContext
213 stdcall -version=0x600+ NtAlpcDisconnectPort(ptr long) ChpeAutoNtAlpcDisconnectPort
214 stdcall -version=0x600+ NtAlpcImpersonateClientOfPort(ptr ptr ptr) ChpeAutoNtAlpcImpersonateClientOfPort
215 stdcall -version=0xA00+ NtAlpcImpersonateClientContainerOfPort(ptr ptr long) ChpeAutoNtAlpcImpersonateClientContainerOfPort
216 stdcall -version=0x600+ NtAlpcOpenSenderProcess(ptr ptr ptr long long ptr) ChpeAutoNtAlpcOpenSenderProcess
217 stdcall -version=0x600+ NtAlpcOpenSenderThread(ptr ptr ptr long long ptr) ChpeAutoNtAlpcOpenSenderThread
218 stdcall -version=0x600+ NtAlpcQueryInformation(ptr long ptr long ptr) ChpeAutoNtAlpcQueryInformation
219 stdcall -version=0x600+ NtAlpcQueryInformationMessage(ptr ptr long ptr long ptr) ChpeAutoNtAlpcQueryInformationMessage
220 stdcall -version=0x600+ NtAlpcRevokeSecurityContext(ptr long ptr) ChpeAutoNtAlpcRevokeSecurityContext
221 stdcall -version=0x600+ NtAlpcSendWaitReceivePort(ptr long ptr ptr ptr ptr ptr ptr) ChpeAutoNtAlpcSendWaitReceivePort
222 stdcall -version=0x600+ NtAlpcSetInformation(ptr long ptr long) ChpeAutoNtAlpcSetInformation
223 stdcall NtApphelpCacheControl(long ptr) ChpeAutoNtApphelpCacheControl
224 stdcall NtAreMappedFilesTheSame(ptr ptr) ChpeAutoNtAreMappedFilesTheSame
225 stdcall NtAssignProcessToJobObject(long long) ChpeAutoNtAssignProcessToJobObject
226 stdcall NtCallbackReturn(ptr long long) ChpeAutoNtCallbackReturn
227 stdcall NtCancelDeviceWakeupRequest(ptr) ChpeAutoNtCancelDeviceWakeupRequest
228 stdcall NtCancelIoFile(long ptr) ChpeAutoNtCancelIoFile
229 stdcall -version=0x600+ NtCancelIoFileEx(long ptr ptr) ChpeAutoNtCancelIoFileEx
230 stdcall -version=0x600+ NtCancelSynchronousIoFile(long ptr ptr) ChpeAutoNtCancelSynchronousIoFile
231 stdcall NtCancelTimer(long ptr) ChpeAutoNtCancelTimer
232 stdcall NtClearEvent(long) ChpeAutoNtClearEvent
234 stdcall NtCloseObjectAuditAlarm(ptr ptr long) ChpeAutoNtCloseObjectAuditAlarm
235 stdcall -version=0x600+ NtCompareObjects(ptr ptr) ChpeAutoNtCompareObjects
236 stdcall -version=0x600+ NtCommitComplete(ptr ptr) ChpeAutoNtCommitComplete
237 stdcall -version=0x600+ NtCommitEnlistment(ptr ptr) ChpeAutoNtCommitEnlistment
238 stdcall -version=0x600+ NtCommitTransaction(ptr long) ChpeAutoNtCommitTransaction
239 stdcall NtCompactKeys(long ptr) ChpeAutoNtCompactKeys
240 stdcall NtCompareTokens(ptr ptr ptr) ChpeAutoNtCompareTokens
241 stdcall NtCompleteConnectPort(ptr) ChpeAutoNtCompleteConnectPort
242 stdcall NtCompressKey(ptr) ChpeAutoNtCompressKey
243 stdcall NtConnectPort(ptr ptr ptr ptr ptr ptr ptr ptr) ChpeAutoNtConnectPort
246 stdcall -version=0xA00+ NtConvertBetweenAuxiliaryCounterAndPerformanceCounter(long ptr ptr ptr) ChpeAutoNtConvertBetweenAuxiliaryCounterAndPerformanceCounter
247 stdcall NtCreateDebugObject(ptr long ptr long) ChpeAutoNtCreateDebugObject
248 stdcall NtCreateDirectoryObject(long long long) ChpeAutoNtCreateDirectoryObject
249 stdcall -version=0x600+ NtCreateEnlistment(ptr long ptr ptr ptr long long ptr) ChpeAutoNtCreateEnlistment
250 stdcall NtCreateEvent(long long long long long) ChpeAutoNtCreateEvent
251 stdcall NtCreateEventPair(ptr long ptr) ChpeAutoNtCreateEventPair
253 stdcall NtCreateIoCompletion(ptr long ptr long) ChpeAutoNtCreateIoCompletion
257 stdcall NtCreateJobObject(ptr long ptr) ChpeAutoNtCreateJobObject
258 stdcall NtCreateJobSet(long ptr long) ChpeAutoNtCreateJobSet
260 stdcall -version=0x600+ NtCreateKeyTransacted(ptr long ptr long ptr long ptr ptr) ChpeAutoNtCreateKeyTransacted
261 stdcall NtCreateKeyedEvent(ptr long ptr long) ChpeAutoNtCreateKeyedEvent
262 stdcall NtCreateMailslotFile(long long long long long long long long) ChpeAutoNtCreateMailslotFile
263 stdcall NtCreateMutant(ptr long ptr long) ChpeAutoNtCreateMutant
265 stdcall NtCreatePagingFile(ptr ptr ptr long) ChpeAutoNtCreatePagingFile
266 stdcall NtCreatePort(ptr ptr long long ptr) ChpeAutoNtCreatePort
267 stdcall -version=0x600+ NtCreatePrivateNamespace(ptr long ptr ptr) ChpeAutoNtCreatePrivateNamespace
268 stdcall NtCreateProcess(ptr long ptr ptr long ptr ptr ptr) ChpeAutoNtCreateProcess
269 stdcall NtCreateProcessEx(ptr long ptr ptr long ptr ptr ptr long) ChpeAutoNtCreateProcessEx
270 stdcall NtCreateProfile(ptr ptr ptr long long ptr long long long) ChpeAutoNtCreateProfile
271 stdcall -version=0x600+ NtCreateResourceManager(ptr long ptr ptr ptr long ptr) ChpeAutoNtCreateResourceManager
272 stdcall NtCreateSection(ptr long ptr ptr long long ptr) ChpeAutoNtCreateSection
273 stdcall NtCreateSectionEx(ptr long ptr ptr long long ptr ptr long) ChpeAutoNtCreateSectionEx
274 stdcall NtCreateSemaphore(ptr long ptr long long) ChpeAutoNtCreateSemaphore
275 stdcall NtCreateSymbolicLinkObject(ptr long ptr ptr) ChpeAutoNtCreateSymbolicLinkObject
277 stdcall -version=0x600+ NtCreateThreadEx(ptr long ptr ptr ptr ptr long long long long ptr) ChpeAutoNtCreateThreadEx
278 stdcall NtCreateTimer(ptr long ptr long) ChpeAutoNtCreateTimer
279 stdcall NtCreateToken(ptr long ptr long ptr ptr ptr ptr ptr ptr ptr ptr ptr) ChpeAutoNtCreateToken
280 stdcall -version=0x600+ NtCreateTransaction(ptr long ptr ptr ptr long long long ptr ptr) ChpeAutoNtCreateTransaction
281 stdcall -version=0x600+ NtCreateTransactionManager(ptr long ptr ptr long long) ChpeAutoNtCreateTransactionManager
282 stdcall -version=0x600+ NtCreateUserProcess(ptr ptr long long ptr ptr long long ptr ptr ptr) ChpeAutoNtCreateUserProcess
283 stdcall NtCreateWaitablePort(ptr ptr long long long) ChpeAutoNtCreateWaitablePort
284 stdcall -version=0x602+ NtCreateWnfStateName(ptr long long long ptr long ptr) ChpeAutoNtCreateWnfStateName
285 stdcall -version=0x600+ NtCreateWorkerFactory(ptr long long long long long long long long long) ChpeStubNtCreateWorkerFactory
286 stdcall NtDebugActiveProcess(ptr ptr) ChpeAutoNtDebugActiveProcess
287 stdcall NtDebugContinue(ptr ptr long) ChpeAutoNtDebugContinue
288 stdcall NtDelayExecution(long ptr) ChpeAutoNtDelayExecution
289 stdcall NtDeleteAtom(long) ChpeAutoNtDeleteAtom
290 stdcall NtDeleteBootEntry(long) ChpeAutoNtDeleteBootEntry
291 stdcall NtDeleteDriverEntry(long) ChpeAutoNtDeleteDriverEntry
292 stdcall NtDeleteFile(ptr) ChpeAutoNtDeleteFile
294 stdcall NtDeleteObjectAuditAlarm(ptr ptr long) ChpeAutoNtDeleteObjectAuditAlarm
295 stdcall -version=0x600+ NtDeletePrivateNamespace(ptr) ChpeAutoNtDeletePrivateNamespace
297 stdcall -version=0x602+ NtDeleteWnfStateData(ptr ptr) ChpeAutoNtDeleteWnfStateData
298 stdcall -version=0x602+ NtDeleteWnfStateName(ptr) ChpeAutoNtDeleteWnfStateName
300 stdcall NtDisplayString(ptr) ChpeAutoNtDisplayString
301 stdcall NtDuplicateObject(long long long ptr long long long) ChpeAutoNtDuplicateObject
302 stdcall NtDuplicateToken(long long long long long long) ChpeAutoNtDuplicateToken
303 stdcall NtEnumerateBootEntries(ptr ptr) ChpeAutoNtEnumerateBootEntries
304 stdcall NtEnumerateDriverEntries(ptr ptr) ChpeAutoNtEnumerateDriverEntries
306 stdcall NtEnumerateSystemEnvironmentValuesEx(long ptr long) ChpeAutoNtEnumerateSystemEnvironmentValuesEx
307 stdcall -version=0x600+ NtEnumerateTransactionObject(ptr long ptr long ptr) ChpeAutoNtEnumerateTransactionObject
309 stdcall NtExtendSection(ptr ptr) ChpeAutoNtExtendSection
310 stdcall NtFilterToken(ptr long ptr ptr ptr ptr) ChpeAutoNtFilterToken
311 stdcall -version=0x602+ NtCreateLowBoxToken(ptr ptr long ptr ptr long ptr long ptr) ChpeAutoNtCreateLowBoxToken
312 stdcall NtFindAtom(ptr long ptr) ChpeAutoNtFindAtom
313 stdcall NtFlushBuffersFile(long ptr) ChpeAutoNtFlushBuffersFile
314 stdcall NtFlushBuffersFileEx(long long ptr long ptr) ChpeAutoNtFlushBuffersFileEx
315 stdcall -version=0x600+ NtFlushInstallUILanguage(long long) ChpeStubNtFlushInstallUILanguage
319 stdcall NtFlushVirtualMemory(ptr ptr ptr ptr) ChpeAutoNtFlushVirtualMemory
320 stdcall NtFlushWriteBuffer() ChpeAutoNtFlushWriteBuffer
321 stdcall NtFreeUserPhysicalPages(ptr ptr ptr) ChpeAutoNtFreeUserPhysicalPages
323 stdcall -version=0x600+ NtFreezeRegistry(long) ChpeStubNtFreezeRegistry
324 stdcall -version=0x600+ NtFreezeTransactions(ptr ptr) ChpeStubNtFreezeTransactions
327 stdcall NtGetCurrentProcessorNumber() ChpeAutoNtGetCurrentProcessorNumber
328 stdcall -version=0xA00+ NtGetCurrentProcessorNumberEx(ptr) ChpeAutoNtGetCurrentProcessorNumberEx
329 stdcall NtGetDevicePowerState(ptr ptr) ChpeAutoNtGetDevicePowerState
330 stdcall -version=0x600+ NtGetMUIRegistryInfo(long ptr ptr) ChpeStubNtGetMUIRegistryInfo
331 stdcall -version=0x600+ NtGetNextProcess(long long long long ptr) ChpeStubNtGetNextProcess
333 stdcall -version=0x600+ NtGetNlsSectionPtr(long long ptr ptr ptr) ChpeAutoNtGetNlsSectionPtr
334 stdcall -version=0x600+ NtGetNotificationResourceManager(ptr ptr long ptr ptr long ptr) ChpeAutoNtGetNotificationResourceManager
335 stdcall NtGetPlugPlayEvent(long long ptr long) ChpeAutoNtGetPlugPlayEvent
336 stdcall NtGetTickCount() ChpeAutoNtGetTickCount
337 stdcall NtGetWriteWatch(long long ptr long ptr ptr ptr) ChpeAutoNtGetWriteWatch
338 stdcall NtImpersonateAnonymousToken(ptr) ChpeAutoNtImpersonateAnonymousToken
339 stdcall NtImpersonateClientOfPort(ptr ptr) ChpeAutoNtImpersonateClientOfPort
340 stdcall NtImpersonateThread(ptr ptr ptr) ChpeAutoNtImpersonateThread
341 stdcall -version=0x600+ NtInitializeNlsFiles(ptr ptr ptr) ChpeAutoNtInitializeNlsFiles
342 stdcall NtInitializeRegistry(long) ChpeAutoNtInitializeRegistry
343 stdcall NtInitiatePowerAction(long long long long) ChpeAutoNtInitiatePowerAction
344 stdcall NtIsProcessInJob(long long) ChpeAutoNtIsProcessInJob
345 stdcall NtIsSystemResumeAutomatic() ChpeAutoNtIsSystemResumeAutomatic
346 stdcall -version=0x600+ NtIsUILanguageComitted() ChpeStubNtIsUILanguageComitted
347 stdcall NtListenPort(ptr ptr) ChpeAutoNtListenPort
348 stdcall NtLoadDriver(ptr) ChpeAutoNtLoadDriver
349 stdcall NtLoadKey2(ptr ptr long) ChpeAutoNtLoadKey2
350 stdcall NtLoadKey(ptr ptr) ChpeAutoNtLoadKey
351 stdcall NtLoadKeyEx(ptr ptr long ptr ptr long ptr ptr) ChpeAutoNtLoadKeyEx
352 stdcall NtLockFile(long long ptr ptr ptr ptr ptr ptr long long) ChpeAutoNtLockFile
353 stdcall NtLockProductActivationKeys(ptr ptr) ChpeAutoNtLockProductActivationKeys
354 stdcall NtLockRegistryKey(ptr) ChpeAutoNtLockRegistryKey
355 stdcall NtLockVirtualMemory(long ptr ptr long) ChpeAutoNtLockVirtualMemory
356 stdcall NtMakePermanentObject(ptr) ChpeAutoNtMakePermanentObject
357 stdcall NtMakeTemporaryObject(long) ChpeAutoNtMakeTemporaryObject
358 stdcall -version=0x600+ NtMapCMFModule(long long ptr ptr ptr ptr) ChpeStubNtMapCMFModule
359 stdcall NtMapUserPhysicalPages(ptr ptr ptr) ChpeAutoNtMapUserPhysicalPages
360 stdcall NtMapUserPhysicalPagesScatter(ptr ptr ptr) ChpeAutoNtMapUserPhysicalPagesScatter
363 stdcall NtModifyBootEntry(ptr) ChpeAutoNtModifyBootEntry
364 stdcall NtModifyDriverEntry(ptr) ChpeAutoNtModifyDriverEntry
365 stdcall NtNotifyChangeDirectoryFile(long long ptr ptr ptr ptr long long long) ChpeAutoNtNotifyChangeDirectoryFile
366 stdcall NtNotifyChangeDirectoryFileEx(long long ptr ptr ptr ptr long long long long) ChpeAutoNtNotifyChangeDirectoryFileEx
369 stdcall NtOpenDirectoryObject(long long long) ChpeAutoNtOpenDirectoryObject
370 stdcall -version=0x600+ NtOpenEnlistment(ptr long ptr ptr ptr) ChpeAutoNtOpenEnlistment
371 stdcall NtOpenEvent(long long long) ChpeAutoNtOpenEvent
372 stdcall NtOpenEventPair(ptr long ptr) ChpeAutoNtOpenEventPair
374 stdcall NtOpenIoCompletion(ptr long ptr) ChpeAutoNtOpenIoCompletion
375 stdcall NtOpenJobObject(ptr long ptr) ChpeAutoNtOpenJobObject
377 stdcall -version=0x600+ NtOpenKeyTransacted(ptr long ptr ptr) ChpeAutoNtOpenKeyTransacted
378 stdcall -version=0x600+ NtOpenKeyTransactedEx(ptr long ptr long ptr) ChpeAutoNtOpenKeyTransactedEx
379 stdcall NtOpenKeyedEvent(ptr long ptr) ChpeAutoNtOpenKeyedEvent
380 stdcall NtOpenMutant(ptr long ptr) ChpeAutoNtOpenMutant
381 stdcall NtOpenObjectAuditAlarm(ptr ptr ptr ptr ptr ptr long long ptr long long ptr) ChpeAutoNtOpenObjectAuditAlarm
382 stdcall -version=0x600+ NtOpenPrivateNamespace(ptr long ptr ptr) ChpeAutoNtOpenPrivateNamespace
383 stdcall NtOpenProcess(ptr long ptr ptr) ChpeAutoNtOpenProcess
384 stdcall NtOpenProcessToken(long long ptr) ChpeAutoNtOpenProcessToken
385 stdcall NtOpenProcessTokenEx(long long long ptr) ChpeAutoNtOpenProcessTokenEx
386 stdcall -version=0x600+ NtOpenResourceManager(ptr long ptr ptr ptr) ChpeAutoNtOpenResourceManager
387 stdcall NtOpenSection(ptr long ptr) ChpeAutoNtOpenSection
388 stdcall NtOpenSemaphore(long long ptr) ChpeAutoNtOpenSemaphore
389 stdcall -version=0x600+ NtOpenSession(ptr long ptr) ChpeStubNtOpenSession
390 stdcall NtOpenSymbolicLinkObject(ptr long ptr) ChpeAutoNtOpenSymbolicLinkObject
391 stdcall NtOpenThread(ptr long ptr ptr) ChpeAutoNtOpenThread
392 stdcall NtOpenThreadToken(long long long ptr) ChpeAutoNtOpenThreadToken
393 stdcall NtOpenThreadTokenEx(long long long long ptr) ChpeAutoNtOpenThreadTokenEx
394 stdcall NtOpenTimer(ptr long ptr) ChpeAutoNtOpenTimer
395 stdcall -version=0x600+ NtOpenTransaction(ptr long ptr ptr ptr) ChpeAutoNtOpenTransaction
396 stdcall -version=0x600+ NtOpenTransactionManager(ptr long ptr ptr ptr long) ChpeAutoNtOpenTransactionManager
397 stdcall NtPlugPlayControl(ptr ptr long) ChpeAutoNtPlugPlayControl
398 stdcall NtPowerInformation(long ptr long ptr long) ChpeAutoNtPowerInformation
399 stdcall -version=0x600+ NtPrePrepareComplete(ptr ptr) ChpeAutoNtPrePrepareComplete
400 stdcall -version=0x600+ NtPrePrepareEnlistment(ptr ptr) ChpeAutoNtPrePrepareEnlistment
401 stdcall -version=0x600+ NtPrepareComplete(ptr ptr) ChpeAutoNtPrepareComplete
402 stdcall -version=0x600+ NtPrepareEnlistment(ptr ptr) ChpeAutoNtPrepareEnlistment
403 stdcall NtPrivilegeCheck(ptr ptr ptr) ChpeAutoNtPrivilegeCheck
404 stdcall NtPrivilegeObjectAuditAlarm(ptr ptr ptr long ptr long) ChpeAutoNtPrivilegeObjectAuditAlarm
405 stdcall NtPrivilegedServiceAuditAlarm(ptr ptr ptr ptr long) ChpeAutoNtPrivilegedServiceAuditAlarm
406 stdcall -version=0x600+ NtPropagationComplete(ptr long long ptr) ChpeAutoNtPropagationComplete
407 stdcall -version=0x600+ NtPropagationFailed(ptr long long) ChpeAutoNtPropagationFailed
409 stdcall NtPulseEvent(long ptr) ChpeAutoNtPulseEvent
410 stdcall NtQueryAttributesFile(ptr ptr) ChpeAutoNtQueryAttributesFile
411 stdcall NtQueryBootEntryOrder(ptr ptr) ChpeAutoNtQueryBootEntryOrder
412 stdcall NtQueryBootOptions(ptr ptr) ChpeAutoNtQueryBootOptions
413 stdcall NtQueryDebugFilterState(long long) ChpeAutoNtQueryDebugFilterState
414 stdcall NtQueryDefaultLocale(long ptr) ChpeAutoNtQueryDefaultLocale
415 stdcall NtQueryDefaultUILanguage(ptr) ChpeAutoNtQueryDefaultUILanguage
417 stdcall NtQueryDirectoryFileEx(long long ptr ptr ptr ptr long long long ptr) ChpeAutoNtQueryDirectoryFileEx
418 stdcall NtQueryDirectoryObject(long ptr long long long ptr ptr) ChpeAutoNtQueryDirectoryObject
419 stdcall NtQueryDriverEntryOrder(ptr ptr) ChpeAutoNtQueryDriverEntryOrder
420 stdcall NtQueryEaFile(long ptr ptr long long ptr long ptr long) ChpeAutoNtQueryEaFile
421 stdcall NtQueryEvent(long long ptr long ptr) ChpeAutoNtQueryEvent
422 stdcall NtQueryFullAttributesFile(ptr ptr) ChpeAutoNtQueryFullAttributesFile
423 stdcall NtQueryInformationAtom(long long ptr long ptr) ChpeAutoNtQueryInformationAtom
424 stdcall NtQueryInformationByName(ptr ptr ptr long long) ChpeAutoNtQueryInformationByName
425 stdcall -version=0x600+ NtQueryInformationEnlistment(ptr long ptr long ptr) ChpeAutoNtQueryInformationEnlistment
427 stdcall NtQueryInformationJobObject(ptr long ptr long ptr) ChpeAutoNtQueryInformationJobObject
428 stdcall NtQueryInformationPort(ptr long ptr long ptr) ChpeAutoNtQueryInformationPort
430 stdcall -version=0x600+ NtQueryInformationResourceManager(ptr long ptr long ptr) ChpeAutoNtQueryInformationResourceManager
432 stdcall NtQueryInformationToken(ptr long ptr long ptr) ChpeAutoNtQueryInformationToken
433 stdcall -version=0x600+ NtQueryInformationTransaction(ptr long ptr long ptr) ChpeAutoNtQueryInformationTransaction
434 stdcall -version=0x600+ NtQueryInformationTransactionManager(ptr long ptr long ptr) ChpeAutoNtQueryInformationTransactionManager
435 stdcall -version=0x600+ NtQueryInformationWorkerFactory(ptr long ptr long ptr) ChpeStubNtQueryInformationWorkerFactory
436 stdcall NtQueryInstallUILanguage(ptr) ChpeAutoNtQueryInstallUILanguage
437 stdcall NtQueryIntervalProfile(long ptr) ChpeAutoNtQueryIntervalProfile
438 stdcall NtQueryIoCompletion(long long ptr long ptr) ChpeAutoNtQueryIoCompletion
440 stdcall -version=0x600+ NtQueryLicenseValue(ptr ptr ptr long ptr) ChpeAutoNtQueryLicenseValue
441 stdcall NtQueryMultipleValueKey(long ptr long ptr long ptr) ChpeAutoNtQueryMultipleValueKey
442 stdcall NtQueryMutant(long long ptr long ptr) ChpeAutoNtQueryMutant
444 stdcall NtQueryOpenSubKeys(ptr ptr) ChpeAutoNtQueryOpenSubKeys
445 stdcall NtQueryOpenSubKeysEx(ptr long ptr ptr) ChpeAutoNtQueryOpenSubKeysEx
446 stdcall NtQueryPerformanceCounter(ptr ptr) ChpeAutoNtQueryPerformanceCounter
447 stdcall NtQueryPortInformationProcess() ChpeAutoNtQueryPortInformationProcess
448 stdcall NtQueryQuotaInformationFile(ptr ptr ptr long long ptr long ptr long) ChpeAutoNtQueryQuotaInformationFile
449 stdcall NtQuerySection(long long long long long) ChpeAutoNtQuerySection
450 stdcall NtQuerySecurityObject(long long long long long) ChpeAutoNtQuerySecurityObject
451 stdcall NtQuerySemaphore(long long ptr long ptr) ChpeAutoNtQuerySemaphore
452 stdcall NtQuerySymbolicLinkObject(long ptr ptr) ChpeAutoNtQuerySymbolicLinkObject
453 stdcall NtQuerySystemEnvironmentValue(ptr ptr long ptr) ChpeAutoNtQuerySystemEnvironmentValue
454 stdcall NtQuerySystemEnvironmentValueEx(ptr ptr ptr ptr ptr) ChpeAutoNtQuerySystemEnvironmentValueEx
456 stdcall -version=0x601+ NtQuerySystemInformationEx(long ptr long ptr long ptr) ChpeAutoNtQuerySystemInformationEx
457 stdcall NtQuerySystemTime(ptr) ChpeAutoNtQuerySystemTime
458 stdcall NtQueryTimer(ptr long ptr long ptr) ChpeAutoNtQueryTimer
459 stdcall NtQueryTimerResolution(long long long) ChpeAutoNtQueryTimerResolution
463 stdcall -version=0x602+ NtQueryWnfStateData(ptr ptr ptr ptr ptr ptr) ChpeAutoNtQueryWnfStateData
464 stdcall -version=0x602+ NtQueryWnfStateNameInformation(ptr long ptr ptr long) ChpeAutoNtQueryWnfStateNameInformation
465 stdcall NtQueueApcThread(long ptr long long long) ChpeAutoNtQueueApcThread
466 stdcall -version=0x601+ NtQueueApcThreadEx(long long ptr long long long) ChpeAutoNtQueueApcThreadEx
467 stdcall -version=0xA00+ NtQueueApcThreadEx2(long long long ptr long long long) ChpeAutoNtQueueApcThreadEx2
469 stdcall NtRaiseHardError(long long long ptr long ptr) ChpeAutoNtRaiseHardError
471 stdcall NtReadFileScatter(long long ptr ptr ptr ptr long ptr ptr) ChpeAutoNtReadFileScatter
472 stdcall -version=0x600+ NtReadOnlyEnlistment(ptr ptr) ChpeAutoNtReadOnlyEnlistment
473 stdcall NtReadRequestData(ptr ptr long ptr long ptr) ChpeAutoNtReadRequestData
475 stdcall -version=0x600+ NtRecoverEnlistment(ptr ptr) ChpeAutoNtRecoverEnlistment
476 stdcall -version=0x600+ NtRecoverResourceManager(ptr) ChpeAutoNtRecoverResourceManager
477 stdcall -version=0x600+ NtRecoverTransactionManager(ptr) ChpeAutoNtRecoverTransactionManager
478 stdcall -version=0x600+ NtRegisterProtocolAddressInformation(ptr ptr long ptr long) ChpeAutoNtRegisterProtocolAddressInformation
479 stdcall NtRegisterThreadTerminatePort(ptr) ChpeAutoNtRegisterThreadTerminatePort
480 stdcall -version=0x600+ NtReleaseCMFViewOwnership() ChpeStubNtReleaseCMFViewOwnership
481 stdcall NtReleaseKeyedEvent(ptr ptr long ptr) ChpeAutoNtReleaseKeyedEvent
482 stdcall NtReleaseMutant(long ptr) ChpeAutoNtReleaseMutant
483 stdcall NtReleaseSemaphore(long long ptr) ChpeAutoNtReleaseSemaphore
484 stdcall -version=0x600+ NtReleaseWorkerFactoryWorker(ptr) ChpeStubNtReleaseWorkerFactoryWorker
485 stdcall NtRemoveIoCompletion(ptr ptr ptr ptr ptr) ChpeAutoNtRemoveIoCompletion
486 stdcall -version=0x600+ NtRemoveIoCompletionEx(ptr ptr long ptr ptr long) ChpeAutoNtRemoveIoCompletionEx
487 stdcall NtRemoveProcessDebug(ptr ptr) ChpeAutoNtRemoveProcessDebug
488 stdcall NtRenameKey(ptr ptr) ChpeAutoNtRenameKey
489 stdcall -version=0x600+ NtRenameTransactionManager(ptr ptr) ChpeAutoNtRenameTransactionManager
490 stdcall NtReplaceKey(ptr long ptr) ChpeAutoNtReplaceKey
491 stdcall -version=0x600+ NtReplacePartitionUnit(wstr wstr long) ChpeStubNtReplacePartitionUnit
492 stdcall NtReplyPort(ptr ptr) ChpeAutoNtReplyPort
493 stdcall NtReplyWaitReceivePort(ptr ptr ptr ptr) ChpeAutoNtReplyWaitReceivePort
494 stdcall NtReplyWaitReceivePortEx(ptr ptr ptr ptr ptr) ChpeAutoNtReplyWaitReceivePortEx
495 stdcall NtReplyWaitReplyPort(ptr ptr) ChpeAutoNtReplyWaitReplyPort
496 stdcall NtRequestDeviceWakeup(ptr) ChpeAutoNtRequestDeviceWakeup
497 stdcall NtRequestPort(ptr ptr) ChpeAutoNtRequestPort
498 stdcall NtRequestWaitReplyPort(ptr ptr ptr) ChpeAutoNtRequestWaitReplyPort
499 stdcall NtRequestWakeupLatency(long) ChpeAutoNtRequestWakeupLatency
500 stdcall NtResetEvent(long ptr) ChpeAutoNtResetEvent
501 stdcall NtResetWriteWatch(long ptr long) ChpeAutoNtResetWriteWatch
502 stdcall NtRestoreKey(long long long) ChpeAutoNtRestoreKey
503 stdcall NtResumeProcess(ptr) ChpeAutoNtResumeProcess
505 stdcall -version=0x600+ NtRollbackComplete(ptr ptr) ChpeAutoNtRollbackComplete
506 stdcall -version=0x600+ NtRollbackEnlistment(ptr ptr) ChpeAutoNtRollbackEnlistment
507 stdcall -version=0x600+ NtRollbackTransaction(ptr long) ChpeAutoNtRollbackTransaction
508 stdcall -version=0x600+ NtRollforwardTransactionManager(ptr ptr) ChpeAutoNtRollforwardTransactionManager
509 stdcall NtSaveKey(long long) ChpeAutoNtSaveKey
510 stdcall NtSaveKeyEx(ptr ptr long) ChpeAutoNtSaveKeyEx
511 stdcall NtSaveMergedKeys(ptr ptr ptr) ChpeAutoNtSaveMergedKeys
512 stdcall NtSecureConnectPort(ptr ptr ptr ptr ptr ptr ptr ptr ptr) ChpeAutoNtSecureConnectPort
513 stdcall NtSetBootEntryOrder(ptr ptr) ChpeAutoNtSetBootEntryOrder
514 stdcall NtSetBootOptions(ptr long) ChpeAutoNtSetBootOptions
516 stdcall NtSetDebugFilterState(long long long) ChpeAutoNtSetDebugFilterState
517 stdcall NtSetDefaultHardErrorPort(ptr) ChpeAutoNtSetDefaultHardErrorPort
518 stdcall NtSetDefaultLocale(long long) ChpeAutoNtSetDefaultLocale
519 stdcall NtSetDefaultUILanguage(long) ChpeAutoNtSetDefaultUILanguage
520 stdcall NtSetDriverEntryOrder(ptr ptr) ChpeAutoNtSetDriverEntryOrder
521 stdcall NtSetEaFile(long ptr ptr long) ChpeAutoNtSetEaFile
522 stdcall NtSetEvent(long long) ChpeAutoNtSetEvent
523 stdcall NtSetEventBoostPriority(ptr) ChpeAutoNtSetEventBoostPriority
524 stdcall NtSetHighEventPair(ptr) ChpeAutoNtSetHighEventPair
525 stdcall NtSetHighWaitLowEventPair(ptr) ChpeAutoNtSetHighWaitLowEventPair
526 stdcall NtSetInformationDebugObject(ptr long ptr long ptr) ChpeAutoNtSetInformationDebugObject
527 stdcall -version=0x600+ NtSetInformationEnlistment(ptr long ptr long) ChpeAutoNtSetInformationEnlistment
528 stdcall NtSetInformationFile(ptr ptr ptr long long) ChpeAutoNtSetInformationFile
529 stdcall NtSetInformationJobObject(ptr long ptr long) ChpeAutoNtSetInformationJobObject
530 stdcall NtSetInformationKey(ptr long ptr long) ChpeAutoNtSetInformationKey
531 stdcall NtSetInformationObject(ptr long ptr long) ChpeAutoNtSetInformationObject
532 stdcall NtSetInformationProcess(ptr long ptr long) ChpeAutoNtSetInformationProcess
533 stdcall -version=0x600+ NtSetInformationResourceManager(ptr long ptr long) ChpeAutoNtSetInformationResourceManager
534 stdcall NtSetInformationThread(ptr long ptr long) ChpeAutoNtSetInformationThread
535 stdcall NtSetInformationVirtualMemory(ptr long ptr ptr ptr long) ChpeAutoNtSetInformationVirtualMemory
536 stdcall NtSetInformationToken(ptr long ptr long) ChpeAutoNtSetInformationToken
537 stdcall -version=0x600+ NtSetInformationTransaction(ptr long ptr long) ChpeAutoNtSetInformationTransaction
538 stdcall -version=0x600+ NtSetInformationTransactionManager(ptr long ptr long) ChpeAutoNtSetInformationTransactionManager
539 stdcall -version=0x600+ NtSetInformationWorkerFactory(ptr long ptr long) ChpeStubNtSetInformationWorkerFactory
540 stdcall NtSetIntervalProfile(long long) ChpeAutoNtSetIntervalProfile
541 stdcall NtSetIoCompletion(ptr long ptr long long) ChpeAutoNtSetIoCompletion
542 stdcall -version=0x600+ NtSetIoCompletionEx(ptr ptr long long long long) ChpeAutoNtSetIoCompletionEx
543 stdcall NtSetLdtEntries(long int64 long int64) ChpeAutoNtSetLdtEntries
544 stdcall NtSetLowEventPair(ptr) ChpeAutoNtSetLowEventPair
545 stdcall NtSetLowWaitHighEventPair(ptr) ChpeAutoNtSetLowWaitHighEventPair
546 stdcall NtSetQuotaInformationFile(ptr ptr ptr long) ChpeAutoNtSetQuotaInformationFile
547 stdcall NtSetSecurityObject(long long ptr) ChpeAutoNtSetSecurityObject
548 stdcall NtSetSystemEnvironmentValue(ptr ptr) ChpeAutoNtSetSystemEnvironmentValue
549 stdcall NtSetSystemEnvironmentValueEx(ptr ptr ptr ptr ptr) ChpeAutoNtSetSystemEnvironmentValueEx
550 stdcall NtSetSystemInformation(long ptr long) ChpeAutoNtSetSystemInformation
551 stdcall NtSetSystemPowerState(long long long) ChpeAutoNtSetSystemPowerState
552 stdcall NtSetSystemTime(ptr ptr) ChpeAutoNtSetSystemTime
553 stdcall NtSetThreadExecutionState(long ptr) ChpeAutoNtSetThreadExecutionState
555 stdcall NtSetTimerResolution(long long ptr) ChpeAutoNtSetTimerResolution
556 stdcall NtSetUuidSeed(ptr) ChpeAutoNtSetUuidSeed
558 stdcall NtSetVolumeInformationFile(long ptr ptr long long) ChpeAutoNtSetVolumeInformationFile
559 stdcall NtShutdownSystem(long) ChpeAutoNtShutdownSystem
560 stdcall -version=0x600+ NtShutdownWorkerFactory(ptr ptr) ChpeStubNtShutdownWorkerFactory
561 stdcall NtSignalAndWaitForSingleObject(long long long ptr) ChpeAutoNtSignalAndWaitForSingleObject
562 stdcall -version=0x600+ NtSinglePhaseReject(ptr ptr) ChpeAutoNtSinglePhaseReject
563 stdcall NtStartProfile(ptr) ChpeAutoNtStartProfile
564 stdcall NtStopProfile(ptr) ChpeAutoNtStopProfile
565 stdcall -version=0x602+ NtSubscribeWnfStateChange(ptr long long ptr) ChpeAutoNtSubscribeWnfStateChange
568 stdcall NtSystemDebugControl(long ptr long ptr long ptr) ChpeAutoNtSystemDebugControl
569 stdcall NtTerminateJobObject(ptr long) ChpeAutoNtTerminateJobObject
572 stdcall NtTestAlert() ChpeAutoNtTestAlert
573 stdcall -version=0x600+ NtThawRegistry() ChpeStubNtThawRegistry
574 stdcall -version=0x600+ NtThawTransactions() ChpeStubNtThawTransactions
575 stdcall -version=0x600+ NtTraceControl(long ptr long ptr long ptr) ChpeAutoNtTraceControl
576 stdcall NtTraceEvent(ptr long long ptr) ChpeAutoNtTraceEvent
577 stdcall NtTranslateFilePath(ptr long ptr long) ChpeAutoNtTranslateFilePath
578 stdcall NtUnloadDriver(ptr) ChpeAutoNtUnloadDriver
579 stdcall NtUnloadKey2(ptr long) ChpeAutoNtUnloadKey2
580 stdcall NtUnloadKey(long) ChpeAutoNtUnloadKey
581 stdcall NtUnloadKeyEx(ptr ptr) ChpeAutoNtUnloadKeyEx
582 stdcall NtUnlockFile(long ptr ptr ptr ptr) ChpeAutoNtUnlockFile
583 stdcall NtUnlockVirtualMemory(long ptr ptr long) ChpeAutoNtUnlockVirtualMemory
586 stdcall -version=0x602+ NtUnsubscribeWnfStateChange(ptr) ChpeAutoNtUnsubscribeWnfStateChange
587 stdcall -version=0x602+ NtUpdateWnfStateData(ptr ptr long ptr ptr long long) ChpeAutoNtUpdateWnfStateData
588 stdcall NtVdmControl(long ptr) ChpeAutoNtVdmControl
589 stdcall NtWaitForDebugEvent(ptr long ptr ptr) ChpeAutoNtWaitForDebugEvent
590 stdcall NtWaitForKeyedEvent(ptr ptr long ptr) ChpeAutoNtWaitForKeyedEvent
592 stdcall NtWaitForMultipleObjects32(long ptr long long ptr) ChpeAutoNtWaitForMultipleObjects32
593 stdcall NtWaitForMultipleObjects(long ptr long long ptr) ChpeAutoNtWaitForMultipleObjects
594 stdcall NtWaitForSingleObject(long long long) ChpeAutoNtWaitForSingleObject
595 stub -version=0x600+ NtWaitForWorkViaWorkerFactory
596 stdcall NtWaitHighEventPair(ptr) ChpeAutoNtWaitHighEventPair
597 stdcall NtWaitLowEventPair(ptr) ChpeAutoNtWaitLowEventPair
598 stdcall -version=0x600+ NtWorkerFactoryWorkerReady(long) ChpeStubNtWorkerFactoryWorkerReady
600 stdcall NtWriteFileGather(long long ptr ptr ptr ptr long ptr ptr) ChpeAutoNtWriteFileGather
601 stdcall NtWriteRequestData(ptr ptr long ptr long ptr) ChpeAutoNtWriteRequestData
603 stdcall NtYieldExecution() ChpeAutoNtYieldExecution
604 stdcall -version=0x600+ NtdllDefWindowProc_A(long long long long) ChpeStubNtdllDefWindowProc_A
605 stdcall -version=0x600+ NtdllDefWindowProc_W(long long long long) ChpeStubNtdllDefWindowProc_W
606 stdcall -version=0x600+ NtdllDialogWndProc_A(long long long long) ChpeStubNtdllDialogWndProc_A
607 stdcall -version=0x600+ NtdllDialogWndProc_W(long long long long) ChpeStubNtdllDialogWndProc_W
608 stdcall PfxFindPrefix(ptr ptr) ChpeAutoPfxFindPrefix
609 stdcall PfxInitialize(ptr) ChpeAutoPfxInitialize
610 stdcall PfxInsertPrefix(ptr ptr ptr) ChpeAutoPfxInsertPrefix
611 stdcall PfxRemovePrefix(ptr ptr) ChpeAutoPfxRemovePrefix
612 stdcall RtlAbortRXact(ptr) ChpeAutoRtlAbortRXact
613 stdcall RtlAbsoluteToSelfRelativeSD(ptr ptr ptr) ChpeAutoRtlAbsoluteToSelfRelativeSD
614 stdcall RtlAcquirePebLock() ChpeAutoRtlAcquirePebLock
616 stdcall RtlAcquireResourceExclusive(ptr long) ChpeAutoRtlAcquireResourceExclusive
617 stdcall RtlAcquireResourceShared(ptr long) ChpeAutoRtlAcquireResourceShared
620 stdcall RtlActivateActivationContext(long ptr ptr) ChpeAutoRtlActivateActivationContext
621 stdcall RtlActivateActivationContextEx(long ptr ptr ptr) ChpeAutoRtlActivateActivationContextEx
622 stdcall RtlActivateActivationContextUnsafeFast(ptr ptr) ChpeAutoRtlActivateActivationContextUnsafeFast
623 stdcall RtlAddAccessAllowedAce(ptr long long ptr) ChpeAutoRtlAddAccessAllowedAce
624 stdcall RtlAddAccessAllowedAceEx(ptr long long long ptr) ChpeAutoRtlAddAccessAllowedAceEx
625 stdcall RtlAddAccessAllowedObjectAce(ptr long long long ptr ptr ptr) ChpeAutoRtlAddAccessAllowedObjectAce
626 stdcall RtlAddAccessDeniedAce(ptr long long ptr) ChpeAutoRtlAddAccessDeniedAce
627 stdcall RtlAddAccessDeniedAceEx(ptr long long long ptr) ChpeAutoRtlAddAccessDeniedAceEx
628 stdcall RtlAddAccessDeniedObjectAce(ptr long long long ptr ptr ptr) ChpeAutoRtlAddAccessDeniedObjectAce
629 stdcall RtlAddAce(ptr long long ptr long) ChpeAutoRtlAddAce
630 stdcall RtlAddActionToRXact(ptr long ptr long ptr long) ChpeAutoRtlAddActionToRXact
631 stdcall RtlAddAtomToAtomTable(ptr wstr ptr) ChpeAutoRtlAddAtomToAtomTable
632 stdcall RtlAddAttributeActionToRXact(ptr long ptr ptr ptr long ptr long) ChpeAutoRtlAddAttributeActionToRXact
633 stdcall RtlAddAuditAccessAce(ptr long long ptr long long) ChpeAutoRtlAddAuditAccessAce
634 stdcall RtlAddAuditAccessAceEx(ptr long long long ptr long long) ChpeAutoRtlAddAuditAccessAceEx
635 stdcall RtlAddAuditAccessObjectAce(ptr long long long ptr ptr ptr long long) ChpeAutoRtlAddAuditAccessObjectAce
636 stdcall  RtlAddCompoundAce(ptr long long long ptr ptr) ChpeStubRtlAddCompoundAce
639 stdcall -version=0x600+ RtlAddMandatoryAce(ptr long long long long ptr) ChpeAutoRtlAddMandatoryAce
640 stdcall RtlAddRefActivationContext(ptr) ChpeAutoRtlAddRefActivationContext
641 stdcall RtlAddRefMemoryStream(ptr) ChpeAutoRtlAddRefMemoryStream
642 stdcall -version=0x600+ RtlAddSIDToBoundaryDescriptor(ptr ptr) ChpeAutoRtlAddSIDToBoundaryDescriptor
643 stdcall -version=0x600+ RtlAddIntegrityLabelToBoundaryDescriptor(ptr ptr) ChpeAutoRtlAddIntegrityLabelToBoundaryDescriptor
646 stdcall  RtlAddressInSectionTable(ptr ptr long) ChpeStubRtlAddressInSectionTable
647 stdcall RtlAdjustPrivilege(long long long ptr) ChpeAutoRtlAdjustPrivilege
648 stdcall RtlAllocateActivationContextStack(ptr) ChpeAutoRtlAllocateActivationContextStack
649 stdcall RtlAllocateAndInitializeSid(ptr long long long long long long long long long ptr) ChpeAutoRtlAllocateAndInitializeSid
650 stdcall RtlAllocateHandle(ptr ptr) ChpeAutoRtlAllocateHandle
652 stdcall -version=0x600+ RtlAllocateMemoryBlockLookaside(ptr long ptr) ChpeStubRtlAllocateMemoryBlockLookaside
653 stdcall -version=0x600+ RtlAllocateMemoryZone(long long ptr) ChpeStubRtlAllocateMemoryZone
654 stdcall RtlAnsiCharToUnicodeChar(ptr) ChpeAutoRtlAnsiCharToUnicodeChar
655 stdcall RtlAnsiStringToUnicodeSize(ptr) ChpeAutoRtlAnsiStringToUnicodeSize
656 stdcall RtlAnsiStringToUnicodeString(ptr ptr long) ChpeAutoRtlAnsiStringToUnicodeString
657 stdcall RtlAppendAsciizToString(ptr str) ChpeAutoRtlAppendAsciizToString
658 stdcall  RtlAppendPathElement(ptr ptr ptr) ChpeStubRtlAppendPathElement
659 stdcall RtlAppendStringToString(ptr ptr) ChpeAutoRtlAppendStringToString
660 stdcall RtlAppendUnicodeStringToString(ptr ptr) ChpeAutoRtlAppendUnicodeStringToString
661 stdcall RtlAppendUnicodeToString(ptr wstr) ChpeAutoRtlAppendUnicodeToString
662 stdcall RtlApplicationVerifierStop(ptr str ptr str ptr str ptr str ptr str) ChpeAutoRtlApplicationVerifierStop
663 stdcall RtlApplyRXact(ptr) ChpeAutoRtlApplyRXact
664 stdcall RtlApplyRXactNoFlush(ptr) ChpeAutoRtlApplyRXactNoFlush
665 stdcall RtlAreAllAccessesGranted(long long) ChpeAutoRtlAreAllAccessesGranted
666 stdcall RtlAreAnyAccessesGranted(long long) ChpeAutoRtlAreAnyAccessesGranted
667 stdcall RtlAreBitsClear(ptr long long) ChpeAutoRtlAreBitsClear
668 stdcall RtlAreBitsSet(ptr long long) ChpeAutoRtlAreBitsSet
669 stdcall RtlAssert(ptr ptr long ptr) ChpeAutoRtlAssert
670 stdcall -version=0x600+ RtlBarrier(ptr long) ChpeStubRtlBarrier
671 stdcall -version=0x600+ RtlBarrierForDelete(long long) ChpeStubRtlBarrierForDelete
672 stdcall RtlCancelTimer(ptr ptr) ChpeAutoRtlCancelTimer
675 stdcall RtlCharToInteger(ptr long ptr) ChpeAutoRtlCharToInteger
676 stdcall RtlCheckForOrphanedCriticalSections(ptr) ChpeAutoRtlCheckForOrphanedCriticalSections
677 stdcall -version=0x602+ RtlCheckPortableOperatingSystem(ptr) ChpeAutoRtlCheckPortableOperatingSystem
678 stdcall RtlCheckRegistryKey(long ptr) ChpeAutoRtlCheckRegistryKey
679 stdcall -version=0x600+ RtlCleanUpTEBLangLists() ChpeStubRtlCleanUpTEBLangLists
680 stdcall RtlClearAllBits(ptr) ChpeAutoRtlClearAllBits
681 stdcall RtlClearBits(ptr long long) ChpeAutoRtlClearBits
682 stdcall RtlCloneMemoryStream(ptr ptr) ChpeAutoRtlCloneMemoryStream
683 stdcall -version=0x600+ RtlCloneUserProcess(long long long long long) ChpeStubRtlCloneUserProcess
684 stdcall -version=0x600+ RtlCmDecodeMemIoResource(ptr ptr) ChpeStubRtlCmDecodeMemIoResource
685 stdcall -version=0x600+ RtlCmEncodeMemIoResource(ptr long long long) ChpeStubRtlCmEncodeMemIoResource
686 stdcall -version=0x600+ RtlCommitDebugInfo(ptr long) ChpeStubRtlCommitDebugInfo
687 stdcall RtlCommitMemoryStream(ptr long) ChpeAutoRtlCommitMemoryStream
688 stdcall RtlCompactHeap(long long) ChpeAutoRtlCompactHeap
689 stdcall -version=0x600+ RtlCompareAltitudes(wstr wstr) ChpeStubRtlCompareAltitudes
691 stdcall RtlCompareMemoryUlong(ptr long long) ChpeAutoRtlCompareMemoryUlong
692 stdcall RtlCompareString(ptr ptr long) ChpeAutoRtlCompareString
694 stdcall -version=0x600+ RtlCompareUnicodeStrings(wstr long wstr long long) ChpeAutoRtlCompareUnicodeStrings
695 stdcall -version=0x600+ RtlCompleteProcessCloning(long) ChpeStubRtlCompleteProcessCloning
696 stdcall RtlCompressBuffer(long ptr long ptr long long ptr ptr) ChpeAutoRtlCompressBuffer
697 stdcall RtlComputeCrc32(long ptr long) ChpeAutoRtlComputeCrc32
698 stdcall RtlComputeImportTableHash(ptr ptr long) ChpeAutoRtlComputeImportTableHash
699 stdcall RtlComputePrivatizedDllName_U(ptr ptr ptr) ChpeAutoRtlComputePrivatizedDllName_U
700 stdcall -version=0x600+ RtlConnectToSm(ptr ptr long ptr) ChpeStubRtlConnectToSm
701 stdcall RtlConsoleMultiByteToUnicodeN(ptr long ptr ptr long ptr) ChpeAutoRtlConsoleMultiByteToUnicodeN
702 stdcall RtlConvertExclusiveToShared(ptr) ChpeAutoRtlConvertExclusiveToShared
703 stdcall -version=0x600+ RtlConvertLCIDToString(long long long ptr long) ChpeAutoRtlConvertLCIDToString
704 stdcall RtlConvertSharedToExclusive(ptr) ChpeAutoRtlConvertSharedToExclusive
705 stdcall RtlConvertSidToUnicodeString(ptr ptr long) ChpeAutoRtlConvertSidToUnicodeString
706 stdcall RtlConvertToAutoInheritSecurityObject(ptr ptr ptr ptr long ptr) ChpeAutoRtlConvertToAutoInheritSecurityObject
707 stdcall RtlConvertUiListToApiList(ptr ptr long) ChpeAutoRtlConvertUiListToApiList
708 stdcall RtlCopyLuid(ptr ptr) ChpeAutoRtlCopyLuid
709 stdcall RtlCopyLuidAndAttributesArray(long ptr ptr) ChpeAutoRtlCopyLuidAndAttributesArray
710 stdcall RtlCopyMappedMemory(ptr ptr long) ChpeAutoRtlCopyMappedMemory
711 stdcall -version=0xA00+ RtlCopyContext(ptr long ptr) ChpeAutoRtlCopyContext
712 stdcall -version=0x600+ RtlCopyExtendedContext(ptr long ptr) ChpeAutoRtlCopyExtendedContext
713 cdecl -version=0x600+ RtlCopyMemory(ptr ptr long) ChpeAutoRtlCopyMemory
714 stdcall -version=0x600+ RtlCopyMemoryNonTemporal(ptr ptr long) ChpeStubRtlCopyMemoryNonTemporal
715 stdcall RtlCopyMemoryStreamTo(ptr ptr int64 ptr ptr) ChpeAutoRtlCopyMemoryStreamTo
716 stdcall RtlCopyOutOfProcessMemoryStreamTo(ptr ptr int64 ptr ptr) ChpeAutoRtlCopyOutOfProcessMemoryStreamTo
717 stdcall RtlCopySecurityDescriptor(ptr ptr) ChpeAutoRtlCopySecurityDescriptor
718 stdcall RtlCopySid(long ptr ptr) ChpeAutoRtlCopySid
719 stdcall RtlCopySidAndAttributesArray(long ptr long ptr ptr ptr ptr) ChpeAutoRtlCopySidAndAttributesArray
720 stdcall RtlCopyString(ptr ptr) ChpeAutoRtlCopyString
722 stdcall RtlCreateAcl(ptr long long) ChpeAutoRtlCreateAcl
723 stdcall RtlCreateActivationContext(long ptr long ptr ptr ptr) ChpeAutoRtlCreateActivationContext
724 stdcall RtlCreateAndSetSD(ptr long ptr ptr ptr) ChpeAutoRtlCreateAndSetSD
725 stdcall RtlContractHashTable(ptr) ChpeAutoRtlContractHashTable
726 stdcall RtlCreateAtomTable(long ptr) ChpeAutoRtlCreateAtomTable
727 stdcall RtlCreateHashTable(ptr long long) ChpeAutoRtlCreateHashTable
728 stdcall RtlCreateBootStatusDataFile() ChpeAutoRtlCreateBootStatusDataFile
729 stdcall -version=0x600+ RtlCreateBoundaryDescriptor(ptr long) ChpeAutoRtlCreateBoundaryDescriptor
730 stdcall RtlCreateEnvironment(long ptr) ChpeAutoRtlCreateEnvironment
731 stdcall -version=0x600+ RtlCreateEnvironmentEx(ptr ptr long) ChpeStubRtlCreateEnvironmentEx
732 stdcall RtlCreateHeap(long ptr long long ptr ptr) ChpeAutoRtlCreateHeap
733 stdcall -version=0x600+ RtlCreateMemoryBlockLookaside(ptr long long long long) ChpeStubRtlCreateMemoryBlockLookaside
734 stdcall -version=0x600+ RtlCreateMemoryZone(ptr long long) ChpeStubRtlCreateMemoryZone
735 stdcall RtlCreateProcessParameters(ptr ptr ptr ptr ptr ptr ptr ptr ptr ptr) ChpeAutoRtlCreateProcessParameters
736 stdcall -version=0x600+ RtlCreateProcessParametersEx(ptr wstr ptr wstr wstr ptr wstr wstr ptr ptr long) ChpeAutoRtlCreateProcessParametersEx
737 stdcall RtlCreateQueryDebugBuffer(long long) ChpeAutoRtlCreateQueryDebugBuffer
738 stdcall RtlCreateRegistryKey(long wstr) ChpeAutoRtlCreateRegistryKey
739 stdcall RtlCreateSecurityDescriptor(ptr long) ChpeAutoRtlCreateSecurityDescriptor
740 stdcall RtlCreateServiceSid(ptr ptr ptr) ChpeAutoRtlCreateServiceSid
741 stdcall -version=0xA00+ RtlDeriveCapabilitySidsFromName(ptr ptr ptr) ChpeAutoRtlDeriveCapabilitySidsFromName
742 stdcall -version=0x602+ RtlIsCapabilitySid(ptr) ChpeAutoRtlIsCapabilitySid
743 stdcall -version=0x602+ RtlGetAppContainerSidType(ptr ptr) ChpeAutoRtlGetAppContainerSidType
744 stdcall -version=0x602+ RtlGetAppContainerParent(ptr ptr) ChpeAutoRtlGetAppContainerParent
745 stdcall -version=0x602+ RtlIsParentOfChildAppContainer(ptr ptr) ChpeAutoRtlIsParentOfChildAppContainer
746 stdcall -version=0x602+ RtlCheckTokenMembershipEx(ptr ptr long ptr) ChpeAutoRtlCheckTokenMembershipEx
747 stdcall RtlCreateSystemVolumeInformationFolder(ptr) ChpeAutoRtlCreateSystemVolumeInformationFolder
748 stdcall RtlCreateTagHeap(ptr long wstr wstr) ChpeAutoRtlCreateTagHeap
749 stdcall RtlCreateTimer(ptr ptr ptr ptr long long long) ChpeAutoRtlCreateTimer
750 stdcall RtlCreateTimerQueue(ptr) ChpeAutoRtlCreateTimerQueue
751 stdcall RtlCreateUnicodeString(ptr wstr) ChpeAutoRtlCreateUnicodeString
752 stdcall RtlCreateUnicodeStringFromAsciiz(ptr str) ChpeAutoRtlCreateUnicodeStringFromAsciiz
753 stdcall RtlCreateUserProcess(ptr long ptr ptr ptr ptr long ptr ptr ptr) ChpeAutoRtlCreateUserProcess
754 stdcall RtlCreateUserSecurityObject(ptr long ptr ptr long ptr ptr) ChpeAutoRtlCreateUserSecurityObject
755 stdcall -version=0x600+ RtlCreateUserStack(long long long long long ptr) ChpeAutoRtlCreateUserStack
757 stdcall -version=0x600+ RtlCultureNameToLCID(ptr ptr) ChpeAutoRtlCultureNameToLCID
758 stdcall RtlCustomCPToUnicodeN(ptr wstr long ptr str long) ChpeAutoRtlCustomCPToUnicodeN
759 stdcall RtlCutoverTimeToSystemTime(ptr ptr ptr long) ChpeAutoRtlCutoverTimeToSystemTime
760 stdcall -version=0x600+ RtlDeCommitDebugInfo(long long long) ChpeStubRtlDeCommitDebugInfo
761 stdcall RtlDeNormalizeProcessParams(ptr) ChpeAutoRtlDeNormalizeProcessParams
762 stdcall RtlDeactivateActivationContext(long long) ChpeAutoRtlDeactivateActivationContext
763 stdcall RtlDeactivateActivationContextUnsafeFast(ptr) ChpeAutoRtlDeactivateActivationContextUnsafeFast
764 stdcall  RtlDebugPrintTimes() ChpeStubRtlDebugPrintTimes
767 stdcall RtlDecompressBuffer(long ptr long ptr long ptr) ChpeAutoRtlDecompressBuffer
769 stdcall RtlDefaultNpAcl(ptr) ChpeAutoRtlDefaultNpAcl
770 stdcall RtlDelete(ptr) ChpeAutoRtlDelete
771 stdcall RtlDeleteAce(ptr long) ChpeAutoRtlDeleteAce
772 stdcall RtlDeleteAtomFromAtomTable(ptr long) ChpeAutoRtlDeleteAtomFromAtomTable
773 stdcall RtlDeleteHashTable(ptr) ChpeAutoRtlDeleteHashTable
774 stdcall -version=0x600+ RtlDeleteBarrier(long) ChpeStubRtlDeleteBarrier
775 stdcall -version=0x600+ RtlDeleteBoundaryDescriptor(ptr) ChpeAutoRtlDeleteBoundaryDescriptor
777 stdcall RtlDeleteElementGenericTable(ptr ptr) ChpeAutoRtlDeleteElementGenericTable
778 stdcall RtlDeleteElementGenericTableAvl(ptr ptr) ChpeAutoRtlDeleteElementGenericTableAvl
781 stdcall RtlDeleteNoSplay(ptr ptr) ChpeAutoRtlDeleteNoSplay
782 stdcall RtlDeleteRegistryValue(long ptr ptr) ChpeAutoRtlDeleteRegistryValue
783 stdcall RtlDeleteResource(ptr) ChpeAutoRtlDeleteResource
784 stdcall RtlDeleteSecurityObject(ptr) ChpeAutoRtlDeleteSecurityObject
785 stdcall RtlDeleteTimer(ptr ptr ptr) ChpeAutoRtlDeleteTimer
786 stdcall RtlDeleteTimerQueue(ptr) ChpeAutoRtlDeleteTimerQueue
787 stdcall RtlDeleteTimerQueueEx(ptr ptr) ChpeAutoRtlDeleteTimerQueueEx
788 stdcall -version=0x600+ RtlDeregisterSecureMemoryCacheCallback(ptr) ChpeStubRtlDeregisterSecureMemoryCacheCallback
789 stdcall RtlDeregisterWait(ptr) ChpeAutoRtlDeregisterWait
790 stdcall RtlDeregisterWaitEx(ptr ptr) ChpeAutoRtlDeregisterWaitEx
791 stdcall RtlDestroyAtomTable(ptr) ChpeAutoRtlDestroyAtomTable
792 stdcall RtlDestroyEnvironment(ptr) ChpeAutoRtlDestroyEnvironment
793 stdcall RtlDestroyHandleTable(ptr) ChpeAutoRtlDestroyHandleTable
794 stdcall RtlDestroyHeap(long) ChpeAutoRtlDestroyHeap
795 stdcall -version=0x600+ RtlDestroyMemoryBlockLookaside(long) ChpeStubRtlDestroyMemoryBlockLookaside
796 stdcall -version=0x600+ RtlDestroyMemoryZone(long) ChpeStubRtlDestroyMemoryZone
797 stdcall RtlDestroyProcessParameters(ptr) ChpeAutoRtlDestroyProcessParameters
798 stdcall RtlDestroyQueryDebugBuffer(ptr) ChpeAutoRtlDestroyQueryDebugBuffer
799 stdcall RtlDetermineDosPathNameType_U(wstr) ChpeAutoRtlDetermineDosPathNameType_U
800 stdcall RtlDllShutdownInProgress() ChpeAutoRtlDllShutdownInProgress
801 stdcall RtlDnsHostNameToComputerName(ptr ptr long) ChpeAutoRtlDnsHostNameToComputerName
802 stdcall RtlDoesFileExists_U(wstr) ChpeAutoRtlDoesFileExists_U
803 stdcall RtlDosApplyFileIsolationRedirection_Ustr(long ptr ptr ptr ptr ptr ptr ptr ptr) ChpeAutoRtlDosApplyFileIsolationRedirection_Ustr
804 stdcall RtlDosPathNameToNtPathName_U(wstr ptr ptr ptr) ChpeAutoRtlDosPathNameToNtPathName_U
805 stdcall RtlDosPathNameToNtPathName_U_WithStatus(wstr ptr ptr ptr) ChpeAutoRtlDosPathNameToNtPathName_U_WithStatus
806 stdcall RtlDosPathNameToRelativeNtPathName_U(ptr ptr ptr ptr) ChpeAutoRtlDosPathNameToRelativeNtPathName_U
807 stdcall RtlDosPathNameToRelativeNtPathName_U_WithStatus(wstr ptr ptr ptr) ChpeAutoRtlDosPathNameToRelativeNtPathName_U_WithStatus
808 stdcall RtlDosSearchPath_U(wstr wstr wstr long ptr ptr) ChpeAutoRtlDosSearchPath_U
809 stdcall RtlDosSearchPath_Ustr(long ptr ptr ptr ptr ptr ptr ptr ptr) ChpeAutoRtlDosSearchPath_Ustr
810 stdcall RtlDowncaseUnicodeChar(long) ChpeAutoRtlDowncaseUnicodeChar
811 stdcall RtlDowncaseUnicodeString(ptr ptr long) ChpeAutoRtlDowncaseUnicodeString
812 stdcall RtlDumpResource(ptr) ChpeAutoRtlDumpResource
813 stdcall RtlDuplicateUnicodeString(long ptr ptr) ChpeAutoRtlDuplicateUnicodeString
814 stdcall RtlEmptyAtomTable(ptr long) ChpeAutoRtlEmptyAtomTable
815 stdcall RtlEndEnumerationHashTable(ptr ptr) ChpeAutoRtlEndEnumerationHashTable
816 stdcall RtlEndWeakEnumerationHashTable(ptr ptr) ChpeAutoRtlEndWeakEnumerationHashTable
817 stdcall  RtlEnableEarlyCriticalSectionEventCreation() ChpeStubRtlEnableEarlyCriticalSectionEventCreation
821 stdcall RtlEnumProcessHeaps(ptr ptr) ChpeAutoRtlEnumProcessHeaps
822 stdcall RtlEnumerateEntryHashTable(ptr ptr) ChpeAutoRtlEnumerateEntryHashTable
823 stdcall RtlEnumerateGenericTable(ptr long) ChpeAutoRtlEnumerateGenericTable
824 stdcall RtlEnumerateGenericTableAvl(ptr long) ChpeAutoRtlEnumerateGenericTableAvl
825 stdcall RtlEnumerateGenericTableLikeADirectory(ptr ptr ptr long ptr ptr ptr) ChpeAutoRtlEnumerateGenericTableLikeADirectory
826 stdcall RtlEnumerateGenericTableWithoutSplaying(ptr ptr) ChpeAutoRtlEnumerateGenericTableWithoutSplaying
827 stdcall RtlEnumerateGenericTableWithoutSplayingAvl(ptr ptr) ChpeAutoRtlEnumerateGenericTableWithoutSplayingAvl
828 stdcall RtlEqualComputerName(ptr ptr) ChpeAutoRtlEqualComputerName
829 stdcall RtlEqualDomainName(ptr ptr) ChpeAutoRtlEqualDomainName
830 stdcall RtlEqualLuid(ptr ptr) ChpeAutoRtlEqualLuid
831 stdcall RtlExpandHashTable(ptr) ChpeAutoRtlExpandHashTable
832 stdcall RtlEqualPrefixSid(ptr ptr) ChpeAutoRtlEqualPrefixSid
833 stdcall RtlEqualSid(long long) ChpeAutoRtlEqualSid
834 stdcall RtlEqualString(ptr ptr long) ChpeAutoRtlEqualString
835 stdcall RtlEqualUnicodeString(ptr ptr long) ChpeAutoRtlEqualUnicodeString
836 stdcall RtlEraseUnicodeString(ptr) ChpeAutoRtlEraseUnicodeString
839 stdcall RtlWow64GetCurrentCpuArea(ptr ptr ptr) ChpeAutoRtlWow64GetCurrentCpuArea
841 stdcall ChpeMarkEcCodeRange(ptr long) ChpeAutoChpeMarkEcCodeRange
842 stdcall ChpeCanContinueToGuest() ChpeAutoChpeCanContinueToGuest
843 stdcall ChpeContinueToGuest(ptr) ChpeAutoChpeContinueToGuest
844 stdcall ChpeContinueToGuestEx(ptr long) ChpeAutoChpeContinueToGuestEx
845 stdcall ProcessPendingCrossProcessEmulatorWork() ChpeAutoProcessPendingCrossProcessEmulatorWork
846 stdcall ChpeIsProcessorFeaturePresent(long) ChpeAutoChpeIsProcessorFeaturePresent
847 stdcall RtlWow64PopAllCrossProcessWorkFromWorkList(ptr ptr) ChpeAutoRtlWow64PopAllCrossProcessWorkFromWorkList
848 stdcall RtlWow64PushCrossProcessWorkOntoFreeList(ptr ptr) ChpeAutoRtlWow64PushCrossProcessWorkOntoFreeList
851 stdcall -version=0x600+ RtlLocateLegacyContext(ptr ptr) ChpeAutoRtlLocateLegacyContext
854 stdcall -version=0x601+ RtlQueryUnbiasedInterruptTime(ptr) ChpeAutoRtlQueryUnbiasedInterruptTime
857 stdcall RtlExitUserProcess(long) ChpeAutoRtlExitUserProcess
859 stdcall -version=0x600+ RtlExpandEnvironmentStrings(long ptr long ptr long ptr) ChpeAutoRtlExpandEnvironmentStrings
860 stdcall RtlExpandEnvironmentStrings_U(ptr ptr ptr ptr) ChpeAutoRtlExpandEnvironmentStrings_U
861 stdcall -version=0x600+ RtlExtendMemoryBlockLookaside(long) ChpeStubRtlExtendMemoryBlockLookaside
862 stdcall -version=0x600+ RtlExtendMemoryZone(long long) ChpeStubRtlExtendMemoryZone
864 stdcall RtlFillMemoryUlong(ptr long long) ChpeAutoRtlFillMemoryUlong
865 stdcall RtlFinalReleaseOutOfProcessMemoryStream(ptr) ChpeAutoRtlFinalReleaseOutOfProcessMemoryStream
866 stdcall -version=0x600+ RtlFindAceByType(long long ptr) ChpeStubRtlFindAceByType
867 stdcall RtlFindActivationContextSectionGuid(long ptr long ptr ptr) ChpeAutoRtlFindActivationContextSectionGuid
868 stdcall RtlFindActivationContextSectionString(long ptr long ptr ptr) ChpeAutoRtlFindActivationContextSectionString
869 stdcall RtlFindCharInUnicodeString(long ptr ptr ptr) ChpeAutoRtlFindCharInUnicodeString
870 stdcall RtlFindClearBits(ptr long long) ChpeAutoRtlFindClearBits
871 stdcall RtlFindClearBitsAndSet(ptr long long) ChpeAutoRtlFindClearBitsAndSet
872 stdcall RtlFindClearRuns(ptr ptr long long) ChpeAutoRtlFindClearRuns
873 stdcall -version=0x600+ RtlFindClosestEncodableLength(long ptr) ChpeStubRtlFindClosestEncodableLength
875 stdcall RtlFindLastBackwardRunClear(ptr long ptr) ChpeAutoRtlFindLastBackwardRunClear
876 stdcall RtlFindLeastSignificantBit(double) ChpeAutoRtlFindLeastSignificantBit
877 stdcall RtlFindLongestRunClear(ptr long) ChpeAutoRtlFindLongestRunClear
878 stdcall RtlFindMessage(long long long long ptr) ChpeAutoRtlFindMessage
879 stdcall RtlFindMostSignificantBit(double) ChpeAutoRtlFindMostSignificantBit
880 stdcall RtlFindNextForwardRunClear(ptr long ptr) ChpeAutoRtlFindNextForwardRunClear
881 stdcall RtlFindSetBits(ptr long long) ChpeAutoRtlFindSetBits
882 stdcall RtlFindSetBitsAndClear(ptr long long) ChpeAutoRtlFindSetBitsAndClear
883 stdcall RtlFirstEntrySList(ptr) ChpeAutoRtlFirstEntrySList
884 stdcall RtlFirstFreeAce(ptr ptr) ChpeAutoRtlFirstFreeAce
889 stdcall RtlFlushSecureMemoryCache(ptr ptr) ChpeAutoRtlFlushSecureMemoryCache
891 stdcall RtlFormatMessage(ptr long long long long ptr ptr long ptr) ChpeAutoRtlFormatMessage
892 stdcall RtlFormatMessageEx(ptr long long long long ptr ptr long ptr long) ChpeAutoRtlFormatMessageEx
893 stdcall RtlFreeActivationContextStack(ptr) ChpeAutoRtlFreeActivationContextStack
894 stdcall RtlFreeAnsiString(long) ChpeAutoRtlFreeAnsiString
895 stdcall RtlFreeHandle(ptr ptr) ChpeAutoRtlFreeHandle
897 stdcall -version=0x600+ RtlFreeMemoryBlockLookaside(long long) ChpeStubRtlFreeMemoryBlockLookaside
898 stdcall RtlFreeOemString(ptr) ChpeAutoRtlFreeOemString
899 stdcall RtlFreeSid(long) ChpeAutoRtlFreeSid
900 stdcall RtlFreeThreadActivationContextStack() ChpeAutoRtlFreeThreadActivationContextStack
902 stdcall -version=0x600+ RtlFreeUserStack(ptr) ChpeAutoRtlFreeUserStack
903 stdcall RtlGUIDFromString(ptr ptr) ChpeAutoRtlGUIDFromString
905 stdcall RtlGetAce(ptr long ptr) ChpeAutoRtlGetAce
906 stdcall RtlGetActiveActivationContext(ptr) ChpeAutoRtlGetActiveActivationContext
907 stdcall RtlGetCallersAddress(ptr ptr) ChpeAutoRtlGetCallersAddress
908 stdcall RtlGetCompressionWorkSpaceSize(long ptr ptr) ChpeAutoRtlGetCompressionWorkSpaceSize
909 stdcall RtlGetControlSecurityDescriptor(ptr ptr ptr) ChpeAutoRtlGetControlSecurityDescriptor
910 stdcall RtlGetCriticalSectionRecursionCount(ptr) ChpeAutoRtlGetCriticalSectionRecursionCount
911 stdcall RtlGetCurrentDirectory_U(long ptr) ChpeAutoRtlGetCurrentDirectory_U
915 stdcall -version=0x600+ RtlGetCurrentTransaction() ChpeStubRtlGetCurrentTransaction
916 stdcall RtlGetDaclSecurityDescriptor(ptr ptr ptr ptr) ChpeAutoRtlGetDaclSecurityDescriptor
917 stdcall -version=0xA00+ RtlGetDeviceFamilyInfoEnum(ptr ptr ptr) ChpeAutoRtlGetDeviceFamilyInfoEnum
918 stdcall -version=0x600+ RtlGetEnabledExtendedFeatures(int64) ChpeAutoRtlGetEnabledExtendedFeatures
919 stdcall RtlGetElementGenericTable(ptr long) ChpeAutoRtlGetElementGenericTable
920 stdcall RtlGetElementGenericTableAvl(ptr long) ChpeAutoRtlGetElementGenericTableAvl
921 stdcall -version=0x600+ RtlGetExtendedContextLength(long ptr) ChpeAutoRtlGetExtendedContextLength
922 stdcall -version=0x600+ RtlGetExtendedContextLength2(long ptr int64) ChpeAutoRtlGetExtendedContextLength2
923 stdcall -version=0x600+ -ret64 RtlGetExtendedFeaturesMask(ptr) ChpeAutoRtlGetExtendedFeaturesMask
924 stdcall -version=0x600+ RtlGetFileMUIPath(long long ptr ptr long long ptr) ChpeStubRtlGetFileMUIPath
925 stdcall RtlGetFrame() ChpeAutoRtlGetFrame
926 stdcall RtlGetFullPathName_U(wstr long ptr ptr) ChpeAutoRtlGetFullPathName_U
927 stdcall RtlGetFullPathName_UstrEx(ptr ptr ptr ptr ptr ptr ptr ptr) ChpeAutoRtlGetFullPathName_UstrEx
929 stdcall RtlGetGroupSecurityDescriptor(ptr ptr ptr) ChpeAutoRtlGetGroupSecurityDescriptor
930 stdcall -version=0x600+ RtlGetIntegerAtom(wstr ptr) ChpeAutoRtlGetIntegerAtom
933 stdcall -version=0x600+ RtlGetLocaleFileMappingAddress(ptr ptr ptr) ChpeAutoRtlGetLocaleFileMappingAddress
935 stdcall RtlGetLengthWithoutTrailingPathSeperators(long ptr ptr) ChpeAutoRtlGetLengthWithoutTrailingPathSeperators
936 stdcall RtlGetLongestNtPathLength() ChpeAutoRtlGetLongestNtPathLength
938 stdcall RtlGetNtGlobalFlags() ChpeAutoRtlGetNtGlobalFlags
939 stdcall RtlGetNtProductType(ptr) ChpeAutoRtlGetNtProductType
940 stdcall RtlGetNtVersionNumbers(ptr ptr ptr) ChpeAutoRtlGetNtVersionNumbers
941 stdcall RtlGetOwnerSecurityDescriptor(ptr ptr ptr) ChpeAutoRtlGetOwnerSecurityDescriptor
942 stdcall -version=0x600+ RtlGetParentLocaleName(wstr long long long) ChpeStubRtlGetParentLocaleName
943 stdcall -version=0x600+ RtlGetProcessPreferredUILanguages(long ptr ptr ptr) ChpeAutoRtlGetProcessPreferredUILanguages
946 stdcall RtlGetSaclSecurityDescriptor(ptr ptr ptr ptr) ChpeAutoRtlGetSaclSecurityDescriptor
947 stdcall RtlGetSecurityDescriptorRMControl(ptr ptr) ChpeAutoRtlGetSecurityDescriptorRMControl
948 stdcall RtlGetSetBootStatusData(ptr long long ptr long long) ChpeAutoRtlGetSetBootStatusData
949 stdcall -version=0x600+ RtlGetSystemPreferredUILanguages(long long ptr ptr ptr) ChpeAutoRtlGetSystemPreferredUILanguages
950 stdcall RtlGetThreadErrorMode() ChpeAutoRtlGetThreadErrorMode
951 stdcall -version=0x600+ RtlGetThreadLangIdByIndex(long long ptr ptr) ChpeStubRtlGetThreadLangIdByIndex
952 stdcall -version=0x600+ RtlGetThreadPreferredUILanguages(long ptr ptr ptr) ChpeAutoRtlGetThreadPreferredUILanguages
953 stdcall -version=0x600+ RtlGetUILanguageInfo(long ptr long ptr ptr) ChpeStubRtlGetUILanguageInfo
955 stdcall -version=0x600+ RtlGetUnloadEventTraceEx(ptr ptr ptr) ChpeAutoRtlGetUnloadEventTraceEx
956 stdcall RtlGetUserInfoHeap(ptr long ptr ptr ptr) ChpeAutoRtlGetUserInfoHeap
957 stdcall -version=0x600+ RtlGetUserPreferredUILanguages(long long ptr ptr ptr) ChpeAutoRtlGetUserPreferredUILanguages
959 stdcall RtlHashUnicodeString(ptr long long ptr) ChpeAutoRtlHashUnicodeString
960 stdcall -version=0x600+ RtlHeapTrkInitialize(ptr) ChpeStubRtlHeapTrkInitialize
961 stdcall RtlIdentifierAuthoritySid(ptr) ChpeAutoRtlIdentifierAuthoritySid
962 stdcall -version=0x600+ RtlIdnToAscii(long wstr long ptr ptr) ChpeAutoRtlIdnToAscii
963 stdcall -version=0x600+ RtlIdnToNameprepUnicode(long wstr long ptr ptr) ChpeAutoRtlIdnToNameprepUnicode
964 stdcall -version=0x600+ RtlIdnToUnicode(long wstr long ptr ptr) ChpeAutoRtlIdnToUnicode
965 stdcall RtlImageDirectoryEntryToData(ptr long long ptr) ChpeAutoRtlImageDirectoryEntryToData
966 stdcall RtlImageNtHeader(long) ChpeAutoRtlImageNtHeader
967 stdcall RtlImageNtHeaderEx(long ptr double ptr) ChpeAutoRtlImageNtHeaderEx
968 stdcall RtlImageRvaToSection(ptr long long) ChpeAutoRtlImageRvaToSection
969 stdcall RtlImageRvaToVa(ptr long long ptr) ChpeAutoRtlImageRvaToVa
970 stdcall RtlImpersonateSelf(long) ChpeAutoRtlImpersonateSelf
971 stdcall -version=0x600+ RtlImpersonateSelfEx(long long ptr) ChpeStubRtlImpersonateSelfEx
972 stdcall RtlInitAnsiString(ptr str) ChpeAutoRtlInitAnsiString
973 stdcall RtlInitAnsiStringEx(ptr str) ChpeAutoRtlInitAnsiStringEx
974 stdcall -version=0xA00+ RtlInitUTF8String(ptr str) ChpeAutoRtlInitUTF8String
975 stdcall -version=0x600+ RtlInitBarrier(long long) ChpeStubRtlInitBarrier
976 stdcall RtlInitCodePageTable(ptr ptr) ChpeAutoRtlInitCodePageTable
978 stdcall RtlInitNlsTables(ptr ptr ptr ptr) ChpeAutoRtlInitNlsTables
980 stdcall RtlInitString(ptr str) ChpeAutoRtlInitString
981 stdcall RtlGetNextEntryHashTable(ptr ptr) ChpeAutoRtlGetNextEntryHashTable
982 stdcall RtlInitEnumerationHashTable(ptr ptr) ChpeAutoRtlInitEnumerationHashTable
984 stdcall RtlInitWeakEnumerationHashTable(ptr ptr) ChpeAutoRtlInitWeakEnumerationHashTable
985 stdcall RtlInitUnicodeStringEx(ptr wstr) ChpeAutoRtlInitUnicodeStringEx
986 stdcall  RtlInitializeAtomPackage(ptr) ChpeStubRtlInitializeAtomPackage
987 stdcall RtlInitializeBitMap(ptr long long) ChpeAutoRtlInitializeBitMap
989 stdcall RtlInitializeContext(ptr ptr ptr ptr ptr) ChpeAutoRtlInitializeContext
991 stdcall RtlInitializeCriticalSectionAndSpinCount(ptr long) ChpeAutoRtlInitializeCriticalSectionAndSpinCount
992 stdcall -version=0x600+ RtlInitializeCriticalSectionEx(ptr long long) ChpeAutoRtlInitializeCriticalSectionEx
993 stdcall -version=0x600+ RtlInitializeExtendedContext(ptr long ptr) ChpeAutoRtlInitializeExtendedContext
994 stdcall -version=0x600+ RtlInitializeExtendedContext2(ptr long ptr int64) ChpeAutoRtlInitializeExtendedContext2
995 stdcall RtlInitializeGenericTable(ptr ptr ptr ptr ptr) ChpeAutoRtlInitializeGenericTable
996 stdcall RtlInitializeGenericTableAvl(ptr ptr ptr ptr ptr) ChpeAutoRtlInitializeGenericTableAvl
997 stdcall RtlInitializeHandleTable(long long ptr) ChpeAutoRtlInitializeHandleTable
998 stdcall -version=0x600+ RtlInitializeNtUserPfn(ptr long ptr long ptr long) ChpeAutoRtlInitializeNtUserPfn
999 stdcall RtlInitializeRXact(ptr long ptr) ChpeAutoRtlInitializeRXact
1000 stdcall RtlInitializeResource(ptr) ChpeAutoRtlInitializeResource
1003 stdcall RtlInitializeSid(ptr ptr long) ChpeAutoRtlInitializeSid
1004 stdcall RtlInsertElementGenericTable(ptr ptr long ptr) ChpeAutoRtlInsertElementGenericTable
1005 stdcall RtlInsertEntryHashTable(ptr ptr long ptr) ChpeAutoRtlInsertEntryHashTable
1006 stdcall RtlInsertElementGenericTableAvl(ptr ptr long ptr) ChpeAutoRtlInsertElementGenericTableAvl
1007 stdcall RtlInsertElementGenericTableFull(ptr ptr long ptr ptr long) ChpeAutoRtlInsertElementGenericTableFull
1008 stdcall RtlInsertElementGenericTableFullAvl(ptr ptr long ptr ptr long) ChpeAutoRtlInsertElementGenericTableFullAvl
1011 stdcall RtlInt64ToUnicodeString(double long ptr) ChpeAutoRtlInt64ToUnicodeString
1012 stdcall RtlIntegerToChar(long long long ptr) ChpeAutoRtlIntegerToChar
1013 stdcall RtlIntegerToUnicodeString(long long ptr) ChpeAutoRtlIntegerToUnicodeString
1019 stdcall -version=0x600+ RtlIoDecodeMemIoResource(ptr ptr ptr ptr) ChpeStubRtlIoDecodeMemIoResource
1020 stdcall -version=0x600+ RtlIoEncodeMemIoResource(ptr long long long long long) ChpeStubRtlIoEncodeMemIoResource
1021 stdcall RtlIpv4AddressToStringA(ptr ptr) ChpeAutoRtlIpv4AddressToStringA
1022 stdcall RtlIpv4AddressToStringExA(ptr long ptr ptr) ChpeAutoRtlIpv4AddressToStringExA
1023 stdcall RtlIpv4AddressToStringExW(ptr long ptr ptr) ChpeAutoRtlIpv4AddressToStringExW
1024 stdcall RtlIpv4AddressToStringW(ptr ptr) ChpeAutoRtlIpv4AddressToStringW
1025 stdcall RtlIpv4StringToAddressA(str long ptr ptr) ChpeAutoRtlIpv4StringToAddressA
1026 stdcall RtlIpv4StringToAddressExA(str long ptr ptr) ChpeAutoRtlIpv4StringToAddressExA
1027 stdcall RtlIpv4StringToAddressExW(wstr long ptr ptr) ChpeAutoRtlIpv4StringToAddressExW
1028 stdcall RtlIpv4StringToAddressW(wstr long ptr ptr) ChpeAutoRtlIpv4StringToAddressW
1029 stdcall RtlIpv6AddressToStringA(ptr ptr) ChpeAutoRtlIpv6AddressToStringA
1030 stdcall RtlIpv6AddressToStringExA(ptr long long ptr ptr) ChpeAutoRtlIpv6AddressToStringExA
1031 stdcall RtlIpv6AddressToStringExW(ptr long long ptr ptr) ChpeAutoRtlIpv6AddressToStringExW
1032 stdcall RtlIpv6AddressToStringW(ptr ptr) ChpeAutoRtlIpv6AddressToStringW
1033 stdcall RtlIpv6StringToAddressA(str ptr ptr) ChpeAutoRtlIpv6StringToAddressA
1034 stdcall RtlIpv6StringToAddressExA(str ptr ptr ptr) ChpeAutoRtlIpv6StringToAddressExA
1035 stdcall RtlIpv6StringToAddressExW(wstr ptr ptr ptr) ChpeAutoRtlIpv6StringToAddressExW
1036 stdcall RtlIpv6StringToAddressW(wstr ptr ptr) ChpeAutoRtlIpv6StringToAddressW
1037 stdcall RtlIsActivationContextActive(ptr) ChpeAutoRtlIsActivationContextActive
1038 stdcall RtlIsCriticalSectionLocked(ptr) ChpeAutoRtlIsCriticalSectionLocked
1039 stdcall RtlIsCriticalSectionLockedByThread(ptr) ChpeAutoRtlIsCriticalSectionLockedByThread
1040 stdcall -version=0x600+ RtlIsCurrentProcess(ptr) ChpeAutoRtlIsCurrentProcess
1041 stdcall -version=0x600+ RtlIsCurrentThreadAttachExempt() ChpeStubRtlIsCurrentThreadAttachExempt
1042 stdcall RtlIsDosDeviceName_U(wstr) ChpeAutoRtlIsDosDeviceName_U
1043 stdcall RtlIsGenericTableEmpty(ptr) ChpeAutoRtlIsGenericTableEmpty
1044 stdcall RtlIsGenericTableEmptyAvl(ptr) ChpeAutoRtlIsGenericTableEmptyAvl
1045 stdcall -version=0x601+ RtlIsNameInExpression(ptr ptr long ptr) ChpeAutoRtlIsNameInExpression
1046 stdcall RtlIsNameLegalDOS8Dot3(ptr ptr ptr) ChpeAutoRtlIsNameLegalDOS8Dot3
1047 stdcall -version=0x600+ RtlIsNormalizedString(long ptr long ptr) ChpeAutoRtlIsNormalizedString
1049 stdcall RtlIsTextUnicode(ptr long ptr) ChpeAutoRtlIsTextUnicode
1050 stdcall RtlIsThreadWithinLoaderCallout() ChpeAutoRtlIsThreadWithinLoaderCallout
1051 stdcall RtlIsValidHandle(ptr ptr) ChpeAutoRtlIsValidHandle
1052 stdcall RtlIsValidIndexHandle(ptr long ptr) ChpeAutoRtlIsValidIndexHandle
1053 stdcall -version=0x600+ RtlIsValidLocaleName(wstr long) ChpeAutoRtlIsValidLocaleName
1054 stdcall -version=0x600+ RtlLCIDToCultureName(long ptr) ChpeAutoRtlLCIDToCultureName
1055 stdcall RtlLargeIntegerToChar(ptr long long ptr) ChpeAutoRtlLargeIntegerToChar
1056 stdcall -version=0x600+ RtlLcidToLocaleName(long ptr long long) ChpeAutoRtlLcidToLocaleName
1058 stdcall RtlLengthRequiredSid(long) ChpeAutoRtlLengthRequiredSid
1059 stdcall RtlLengthSecurityDescriptor(ptr) ChpeAutoRtlLengthSecurityDescriptor
1060 stdcall RtlLengthSid(ptr) ChpeAutoRtlLengthSid
1061 stdcall RtlLocalTimeToSystemTime(ptr ptr) ChpeAutoRtlLocalTimeToSystemTime
1062 stdcall -version=0x600+ RtlLocaleNameToLcid(wstr ptr long) ChpeAutoRtlLocaleNameToLcid
1063 stdcall RtlLockBootStatusData(ptr) ChpeAutoRtlLockBootStatusData
1064 stdcall -version=0x600+ RtlLockCurrentThread() ChpeStubRtlLockCurrentThread
1065 stdcall RtlLockHeap(long) ChpeAutoRtlLockHeap
1066 stdcall -version=0x600+ RtlLockMemoryBlockLookaside(long) ChpeStubRtlLockMemoryBlockLookaside
1067 stdcall RtlLockMemoryStreamRegion(ptr int64 int64 long) ChpeAutoRtlLockMemoryStreamRegion
1068 stdcall -version=0x600+ RtlLockMemoryZone(long) ChpeStubRtlLockMemoryZone
1069 stdcall -version=0x600+ RtlLockModuleSection(long) ChpeStubRtlLockModuleSection
1071 stdcall  RtlLogStackBackTrace() ChpeStubRtlLogStackBackTrace
1072 stdcall RtlLookupAtomInAtomTable(ptr wstr ptr) ChpeAutoRtlLookupAtomInAtomTable
1073 stdcall RtlLookupElementGenericTable(ptr ptr) ChpeAutoRtlLookupElementGenericTable
1074 stdcall RtlLookupEntryHashTable(ptr long ptr) ChpeAutoRtlLookupEntryHashTable
1075 stdcall RtlRemoveEntryHashTable(ptr ptr ptr) ChpeAutoRtlRemoveEntryHashTable
1076 stdcall RtlLookupElementGenericTableAvl(ptr ptr) ChpeAutoRtlLookupElementGenericTableAvl
1077 stdcall RtlLookupElementGenericTableFull(ptr ptr ptr long) ChpeAutoRtlLookupElementGenericTableFull
1078 stdcall RtlLookupElementGenericTableFullAvl(ptr ptr ptr long) ChpeAutoRtlLookupElementGenericTableFullAvl
1081 stdcall RtlMakeSelfRelativeSD(ptr ptr ptr) ChpeAutoRtlMakeSelfRelativeSD
1082 stdcall RtlMapGenericMask(long ptr) ChpeAutoRtlMapGenericMask
1083 stdcall RtlMapSecurityErrorToNtStatus(long) ChpeAutoRtlMapSecurityErrorToNtStatus
1085 stdcall RtlMultiAppendUnicodeStringBuffer(ptr long ptr) ChpeAutoRtlMultiAppendUnicodeStringBuffer
1086 stdcall RtlMultiByteToUnicodeN(ptr long ptr ptr long) ChpeAutoRtlMultiByteToUnicodeN
1087 stdcall RtlMultiByteToUnicodeSize(ptr str long) ChpeAutoRtlMultiByteToUnicodeSize
1088 stdcall RtlMultipleAllocateHeap(ptr long ptr long ptr) ChpeAutoRtlMultipleAllocateHeap
1089 stdcall RtlMultipleFreeHeap(ptr long long ptr) ChpeAutoRtlMultipleFreeHeap
1090 stdcall RtlNewInstanceSecurityObject(long long ptr ptr ptr ptr ptr long ptr ptr) ChpeAutoRtlNewInstanceSecurityObject
1091 stdcall RtlNewSecurityGrantedAccess(long ptr ptr ptr ptr ptr) ChpeAutoRtlNewSecurityGrantedAccess
1092 stdcall RtlNewSecurityObject(ptr ptr ptr long ptr ptr) ChpeAutoRtlNewSecurityObject
1093 stdcall RtlNewSecurityObjectEx(ptr ptr ptr ptr long long ptr ptr) ChpeAutoRtlNewSecurityObjectEx
1094 stdcall RtlNewSecurityObjectWithMultipleInheritance(ptr ptr ptr ptr long long long ptr ptr) ChpeAutoRtlNewSecurityObjectWithMultipleInheritance
1095 stdcall RtlNormalizeProcessParams(ptr) ChpeAutoRtlNormalizeProcessParams
1096 stdcall -version=0x600+ RtlNormalizeString(long ptr long ptr ptr) ChpeAutoRtlNormalizeString
1097 stdcall RtlNtPathNameToDosPathName(long ptr ptr ptr) ChpeAutoRtlNtPathNameToDosPathName
1099 stdcall RtlNtStatusToDosErrorNoTeb(long) ChpeAutoRtlNtStatusToDosErrorNoTeb
1100 stub -version=0x600+ RtlNtdllName
1101 stdcall RtlNumberGenericTableElements(ptr) ChpeAutoRtlNumberGenericTableElements
1102 stdcall RtlNumberGenericTableElementsAvl(ptr) ChpeAutoRtlNumberGenericTableElementsAvl
1103 stdcall RtlNumberOfClearBits(ptr) ChpeAutoRtlNumberOfClearBits
1104 stdcall RtlNumberOfSetBits(ptr) ChpeAutoRtlNumberOfSetBits
1105 stdcall -version=0x600+ RtlNumberOfSetBitsUlongPtr(long) ChpeAutoRtlNumberOfSetBitsUlongPtr
1106 stdcall RtlOemStringToUnicodeSize(ptr) ChpeAutoRtlOemStringToUnicodeSize
1107 stdcall RtlOemStringToUnicodeString(ptr ptr long) ChpeAutoRtlOemStringToUnicodeString
1108 stdcall RtlOemToUnicodeN(ptr long ptr ptr long) ChpeAutoRtlOemToUnicodeN
1109 stdcall RtlOpenCurrentUser(long ptr) ChpeAutoRtlOpenCurrentUser
1110 stdcall -version=0x600+ RtlOwnerAcesPresent(long) ChpeStubRtlOwnerAcesPresent
1112 stdcall RtlPinAtomInAtomTable(ptr long) ChpeAutoRtlPinAtomInAtomTable
1113 stdcall RtlPopFrame(ptr) ChpeAutoRtlPopFrame
1114 stdcall RtlPrefixString(ptr ptr long) ChpeAutoRtlPrefixString
1115 stdcall RtlPrefixUnicodeString(ptr ptr long) ChpeAutoRtlPrefixUnicodeString
1116 stdcall -version=0x600+ RtlPrepareForProcessCloning() ChpeStubRtlPrepareForProcessCloning
1117 stdcall -version=0x600+ RtlProcessFlsData(ptr long) ChpeAutoRtlProcessFlsData
1118 stdcall RtlProtectHeap(ptr long) ChpeAutoRtlProtectHeap
1119 stdcall RtlPushFrame(ptr) ChpeAutoRtlPushFrame
1120 stdcall -version=0x600+ RtlQueryActivationContextApplicationSettings(long ptr wstr wstr ptr ptr ptr) ChpeAutoRtlQueryActivationContextApplicationSettings
1121 stdcall RtlQueryAtomInAtomTable(ptr long ptr ptr ptr ptr) ChpeAutoRtlQueryAtomInAtomTable
1122 stdcall -version=0x600+ RtlQueryCriticalSectionOwner(ptr) ChpeStubRtlQueryCriticalSectionOwner
1124 stdcall -version=0x600+ RtlQueryDynamicTimeZoneInformation(ptr) ChpeAutoRtlQueryDynamicTimeZoneInformation
1125 stdcall -version=0x600+ RtlQueryElevationFlags(ptr) ChpeStubRtlQueryElevationFlags
1127 stdcall RtlQueryEnvironmentVariable_U(ptr ptr ptr) ChpeAutoRtlQueryEnvironmentVariable_U
1128 stdcall RtlQueryHeapInformation(long long ptr long ptr) ChpeAutoRtlQueryHeapInformation
1129 stdcall RtlQueryInformationAcl(ptr ptr long long) ChpeAutoRtlQueryInformationAcl
1130 stdcall RtlQueryInformationActivationContext(long long ptr long ptr long ptr) ChpeAutoRtlQueryInformationActivationContext
1131 stdcall RtlQueryInformationActiveActivationContext(long ptr long ptr) ChpeAutoRtlQueryInformationActiveActivationContext
1132 stdcall RtlQueryInterfaceMemoryStream(ptr ptr ptr) ChpeAutoRtlQueryInterfaceMemoryStream
1133 stdcall -version=0x600+ RtlQueryModuleInformation(ptr long ptr) ChpeStubRtlQueryModuleInformation
1134 stdcall  RtlQueryProcessBackTraceInformation(ptr) ChpeStubRtlQueryProcessBackTraceInformation
1135 stdcall RtlQueryProcessDebugInformation(long long ptr) ChpeAutoRtlQueryProcessDebugInformation
1136 stdcall RtlQueryProcessHeapInformation(ptr) ChpeAutoRtlQueryProcessHeapInformation
1137 stdcall  RtlQueryProcessLockInformation(ptr) ChpeStubRtlQueryProcessLockInformation
1138 stdcall RtlQueryRegistryValues(long ptr ptr ptr ptr) ChpeAutoRtlQueryRegistryValues
1139 stdcall RtlQueryRegistryValuesEx(long ptr ptr ptr ptr) ChpeAutoRtlQueryRegistryValuesEx
1140 stdcall RtlQuerySecurityObject(ptr long ptr long ptr) ChpeAutoRtlQuerySecurityObject
1141 stdcall RtlQueryTagHeap(ptr long long long ptr) ChpeAutoRtlQueryTagHeap
1142 stdcall RtlQueryTimeZoneInformation(ptr) ChpeAutoRtlQueryTimeZoneInformation
1143 stdcall RtlQueueApcWow64Thread(ptr ptr ptr ptr ptr) ChpeAutoRtlQueueApcWow64Thread
1148 stdcall RtlRandomEx(ptr) ChpeAutoRtlRandomEx
1150 stdcall RtlReadMemoryStream(ptr ptr long ptr) ChpeAutoRtlReadMemoryStream
1151 stdcall RtlReadOutOfProcessMemoryStream(ptr ptr long ptr) ChpeAutoRtlReadOutOfProcessMemoryStream
1152 stdcall RtlRealPredecessor(ptr) ChpeAutoRtlRealPredecessor
1153 stdcall RtlRealSuccessor(ptr) ChpeAutoRtlRealSuccessor
1154 stdcall RtlRegisterSecureMemoryCacheCallback(ptr) ChpeAutoRtlRegisterSecureMemoryCacheCallback
1155 stdcall RtlRegisterCfgTargetRange(ptr long) ChpeAutoRtlRegisterCfgTargetRange
1156 stdcall -version=0x600+ RtlRegisterThreadWithCsrss() ChpeStubRtlRegisterThreadWithCsrss
1158 stdcall RtlReleaseActivationContext(ptr) ChpeAutoRtlReleaseActivationContext
1160 stdcall RtlReleasePebLock() ChpeAutoRtlReleasePebLock
1162 stdcall RtlReleaseRelativeName(ptr) ChpeAutoRtlReleaseRelativeName
1163 stdcall RtlReleaseResource(ptr) ChpeAutoRtlReleaseResource
1166 stdcall RtlRemoteCall(ptr ptr ptr long ptr long long) ChpeAutoRtlRemoteCall
1167 stdcall -version=0x600+ RtlRemovePrivileges(ptr ptr long) ChpeAutoRtlRemovePrivileges
1170 stdcall -version=0x600+ RtlReportException(long long long) ChpeStubRtlReportException
1171 stdcall -version=0x600+ RtlResetMemoryBlockLookaside(long) ChpeStubRtlResetMemoryBlockLookaside
1172 stdcall -version=0x600+ RtlResetMemoryZone(long) ChpeStubRtlResetMemoryZone
1173 stdcall -version=0x600+ RtlResetNtUserPfn() ChpeAutoRtlResetNtUserPfn
1174 stdcall RtlResetRtlTranslations(ptr) ChpeAutoRtlResetRtlTranslations
1177 stdcall -version=0x600+ RtlRetrieveNtUserPfn(ptr ptr ptr) ChpeAutoRtlRetrieveNtUserPfn
1178 stdcall RtlRevertMemoryStream(ptr) ChpeAutoRtlRevertMemoryStream
1179 stdcall RtlRunDecodeUnicodeString(long ptr) ChpeAutoRtlRunDecodeUnicodeString
1180 stdcall RtlRunEncodeUnicodeString(long ptr) ChpeAutoRtlRunEncodeUnicodeString
1181 stdcall -version=0x600+ RtlRunOnceBeginInitialize(ptr long ptr) ChpeAutoRtlRunOnceBeginInitialize
1182 stdcall -version=0x600+ RtlRunOnceComplete(ptr long ptr) ChpeAutoRtlRunOnceComplete
1185 stdcall RtlSecondsSince1970ToTime(long ptr) ChpeAutoRtlSecondsSince1970ToTime
1186 stdcall RtlSecondsSince1980ToTime(long ptr) ChpeAutoRtlSecondsSince1980ToTime
1187 stdcall RtlSeekMemoryStream(ptr int64 long ptr) ChpeAutoRtlSeekMemoryStream
1188 stdcall RtlSelfRelativeToAbsoluteSD2(ptr ptr) ChpeAutoRtlSelfRelativeToAbsoluteSD2
1189 stdcall RtlSelfRelativeToAbsoluteSD(ptr ptr ptr ptr ptr ptr ptr ptr ptr ptr ptr) ChpeAutoRtlSelfRelativeToAbsoluteSD
1190 stdcall -version=0x600+ RtlSendMsgToSm(ptr ptr) ChpeStubRtlSendMsgToSm
1191 stdcall RtlSetAllBits(ptr) ChpeAutoRtlSetAllBits
1192 stdcall RtlSetAttributesSecurityDescriptor(ptr long ptr) ChpeAutoRtlSetAttributesSecurityDescriptor
1193 stdcall RtlSetBits(ptr long long) ChpeAutoRtlSetBits
1194 stdcall RtlSetCfgTargetValidity(ptr long long ptr) ChpeAutoRtlSetCfgTargetValidity
1195 stdcall RtlSetControlSecurityDescriptor(ptr long long) ChpeAutoRtlSetControlSecurityDescriptor
1197 stdcall RtlSetCurrentDirectory_U(ptr) ChpeAutoRtlSetCurrentDirectory_U
1198 stdcall RtlSetCurrentEnvironment(wstr ptr) ChpeAutoRtlSetCurrentEnvironment
1199 stdcall -version=0x600+ RtlSetCurrentTransaction(ptr) ChpeStubRtlSetCurrentTransaction
1200 stdcall RtlSetDaclSecurityDescriptor(ptr long ptr long) ChpeAutoRtlSetDaclSecurityDescriptor
1201 stdcall -version=0x600+ RtlSetDynamicTimeZoneInformation(long) ChpeStubRtlSetDynamicTimeZoneInformation
1202 stdcall RtlSetEnvironmentStrings(wstr long) ChpeAutoRtlSetEnvironmentStrings
1203 stdcall -version=0x600+ RtlSetEnvironmentVar(ptr ptr long ptr long) ChpeStubRtlSetEnvironmentVar
1204 stdcall RtlSetEnvironmentVariable(ptr ptr ptr) ChpeAutoRtlSetEnvironmentVariable
1205 stdcall -version=0x600+ RtlSetExtendedFeaturesMask(ptr int64) ChpeAutoRtlSetExtendedFeaturesMask
1206 stdcall RtlSetGroupSecurityDescriptor(ptr ptr long) ChpeAutoRtlSetGroupSecurityDescriptor
1207 stdcall RtlSetHeapInformation(ptr long ptr ptr) ChpeAutoRtlSetHeapInformation
1208 stdcall RtlSetInformationAcl(ptr ptr long long) ChpeAutoRtlSetInformationAcl
1209 stdcall RtlSetIoCompletionCallback(long ptr long) ChpeAutoRtlSetIoCompletionCallback
1211 stdcall RtlSetLastWin32ErrorAndNtStatusFromNtStatus(long) ChpeAutoRtlSetLastWin32ErrorAndNtStatusFromNtStatus
1212 stdcall RtlSetMemoryStreamSize(ptr int64) ChpeAutoRtlSetMemoryStreamSize
1213 stdcall RtlSetOwnerSecurityDescriptor(ptr ptr long) ChpeAutoRtlSetOwnerSecurityDescriptor
1214 stdcall -version=0x600+ RtlSetProcessDebugInformation(ptr long ptr) ChpeStubRtlSetProcessDebugInformation
1215 stdcall -version=0x600+ RtlSetProcessPreferredUILanguages(long ptr ptr) ChpeAutoRtlSetProcessPreferredUILanguages
1216 cdecl RtlSetProcessIsCritical(long ptr long) ChpeAutoRtlSetProcessIsCritical
1217 stdcall RtlSetSaclSecurityDescriptor(ptr long ptr long) ChpeAutoRtlSetSaclSecurityDescriptor
1218 stdcall RtlSetSecurityDescriptorRMControl(ptr ptr) ChpeAutoRtlSetSecurityDescriptorRMControl
1219 stdcall RtlSetSecurityObject(long ptr ptr ptr ptr) ChpeAutoRtlSetSecurityObject
1220 stdcall RtlSetSecurityObjectEx(long ptr ptr long ptr ptr) ChpeAutoRtlSetSecurityObjectEx
1221 stdcall RtlSetThreadErrorMode(long ptr) ChpeAutoRtlSetThreadErrorMode
1222 cdecl RtlSetThreadIsCritical(long ptr long) ChpeAutoRtlSetThreadIsCritical
1223 stdcall RtlSetThreadPoolStartFunc(ptr ptr) ChpeAutoRtlSetThreadPoolStartFunc
1224 stdcall -version=0x600+ RtlSetThreadPreferredUILanguages(long ptr ptr) ChpeStubRtlSetThreadPreferredUILanguages
1225 stdcall RtlSetTimeZoneInformation(ptr) ChpeAutoRtlSetTimeZoneInformation
1226 stdcall RtlSetTimer(ptr ptr ptr ptr long long long) ChpeAutoRtlSetTimer
1227 stdcall RtlSetUnhandledExceptionFilter(ptr) ChpeAutoRtlSetUnhandledExceptionFilter
1228 stdcall RtlSetUserFlagsHeap(ptr long ptr long long) ChpeAutoRtlSetUserFlagsHeap
1229 stdcall RtlSetUserValueHeap(ptr long ptr ptr) ChpeAutoRtlSetUserValueHeap
1230 stdcall -version=0x600+ RtlSidDominates(long long ptr) ChpeStubRtlSidDominates
1231 stdcall -version=0x600+ RtlSidEqualLevel(long long ptr) ChpeStubRtlSidEqualLevel
1232 stdcall -version=0x600+ RtlSidHashInitialize(ptr long ptr) ChpeStubRtlSidHashInitialize
1233 stdcall -version=0x600+ RtlSidHashLookup(long ptr) ChpeStubRtlSidHashLookup
1234 stdcall -version=0x600+ RtlSidIsHigherLevel(long long ptr) ChpeStubRtlSidIsHigherLevel
1236 stdcall -version=0x600+ RtlSleepConditionVariableCS(ptr ptr ptr) ChpeAutoRtlSleepConditionVariableCS
1237 stdcall -version=0x600+ RtlSleepConditionVariableSRW(ptr ptr ptr long) ChpeAutoRtlSleepConditionVariableSRW
1238 stdcall RtlSplay(ptr) ChpeAutoRtlSplay
1239 stdcall RtlStartRXact(ptr) ChpeAutoRtlStartRXact
1240 stdcall RtlStatMemoryStream(ptr ptr long) ChpeAutoRtlStatMemoryStream
1241 stdcall RtlStringFromGUID(ptr ptr) ChpeAutoRtlStringFromGUID
1242 stdcall RtlSubAuthorityCountSid(ptr) ChpeAutoRtlSubAuthorityCountSid
1243 stdcall RtlSubAuthoritySid(ptr long) ChpeAutoRtlSubAuthoritySid
1244 stdcall RtlSubtreePredecessor(ptr) ChpeAutoRtlSubtreePredecessor
1245 stdcall RtlSubtreeSuccessor(ptr) ChpeAutoRtlSubtreeSuccessor
1246 stdcall RtlSystemTimeToLocalTime(ptr ptr) ChpeAutoRtlSystemTimeToLocalTime
1247 stdcall -version=0x600+ RtlTestBit(ptr long) ChpeAutoRtlTestBit
1248 stdcall RtlTimeFieldsToTime(ptr ptr) ChpeAutoRtlTimeFieldsToTime
1249 stdcall RtlTimeToElapsedTimeFields(long long) ChpeAutoRtlTimeToElapsedTimeFields
1250 stdcall RtlTimeToSecondsSince1970(ptr ptr) ChpeAutoRtlTimeToSecondsSince1970
1251 stdcall RtlTimeToSecondsSince1980(ptr ptr) ChpeAutoRtlTimeToSecondsSince1980
1252 stdcall RtlTimeToTimeFields(long long) ChpeAutoRtlTimeToTimeFields
1253 stdcall RtlTraceDatabaseAdd(ptr long ptr ptr) ChpeAutoRtlTraceDatabaseAdd
1254 stdcall RtlTraceDatabaseCreate(long ptr long long ptr) ChpeAutoRtlTraceDatabaseCreate
1255 stdcall RtlTraceDatabaseDestroy(ptr) ChpeAutoRtlTraceDatabaseDestroy
1256 stdcall RtlTraceDatabaseEnumerate(ptr ptr ptr) ChpeAutoRtlTraceDatabaseEnumerate
1257 stdcall RtlTraceDatabaseFind(ptr long ptr ptr) ChpeAutoRtlTraceDatabaseFind
1258 stdcall RtlTraceDatabaseLock(ptr) ChpeAutoRtlTraceDatabaseLock
1259 stdcall RtlTraceDatabaseUnlock(ptr) ChpeAutoRtlTraceDatabaseUnlock
1260 stdcall RtlTraceDatabaseValidate(ptr) ChpeAutoRtlTraceDatabaseValidate
1261 stdcall -version=0x600+ RtlTryAcquirePebLock() ChpeStubRtlTryAcquirePebLock
1265 stdcall RtlUnhandledExceptionFilter2(ptr long) ChpeAutoRtlUnhandledExceptionFilter2
1266 stdcall RtlUnhandledExceptionFilter(ptr) ChpeAutoRtlUnhandledExceptionFilter
1267 stdcall RtlUnicodeStringToAnsiSize(ptr) ChpeAutoRtlUnicodeStringToAnsiSize
1268 stdcall -version=0x601+ RtlUnicodeToUTF8N(ptr long ptr wstr long) ChpeAutoRtlUnicodeToUTF8N
1269 stdcall -version=0xA00+ RtlUTF8StringToUnicodeString(ptr ptr long) ChpeAutoRtlUTF8StringToUnicodeString
1271 stdcall RtlUnicodeStringToAnsiString(ptr ptr long) ChpeAutoRtlUnicodeStringToAnsiString
1272 stdcall RtlUnicodeStringToCountedOemString(ptr ptr long) ChpeAutoRtlUnicodeStringToCountedOemString
1273 stdcall RtlUnicodeStringToInteger(ptr long ptr) ChpeAutoRtlUnicodeStringToInteger
1274 stdcall RtlUnicodeStringToOemSize(ptr) ChpeAutoRtlUnicodeStringToOemSize
1275 stdcall RtlUnicodeStringToOemString(ptr ptr long) ChpeAutoRtlUnicodeStringToOemString
1276 stdcall RtlUnicodeToCustomCPN(ptr ptr long ptr wstr long) ChpeAutoRtlUnicodeToCustomCPN
1277 stdcall RtlUnicodeToMultiByteN(ptr long ptr ptr long) ChpeAutoRtlUnicodeToMultiByteN
1278 stdcall RtlUnicodeToMultiByteSize(ptr ptr long) ChpeAutoRtlUnicodeToMultiByteSize
1279 stdcall RtlUnicodeToOemN(ptr long ptr ptr long) ChpeAutoRtlUnicodeToOemN
1280 stdcall RtlUniform(ptr) ChpeAutoRtlUniform
1281 stdcall RtlUnlockBootStatusData(ptr) ChpeAutoRtlUnlockBootStatusData
1282 stdcall -version=0x600+ RtlUnlockCurrentThread() ChpeStubRtlUnlockCurrentThread
1283 stdcall RtlUnlockHeap(long) ChpeAutoRtlUnlockHeap
1284 stdcall -version=0x600+ RtlUnlockMemoryBlockLookaside(long) ChpeStubRtlUnlockMemoryBlockLookaside
1285 stdcall RtlUnlockMemoryStreamRegion(ptr int64 int64 long) ChpeAutoRtlUnlockMemoryStreamRegion
1286 stdcall -version=0x600+ RtlUnlockMemoryZone(long) ChpeStubRtlUnlockMemoryZone
1287 stdcall -version=0x600+ RtlUnlockModuleSection(long) ChpeStubRtlUnlockModuleSection
1290 stdcall RtlUnregisterCfgTargetRange(ptr) ChpeAutoRtlUnregisterCfgTargetRange
1291 stdcall RtlUpcaseUnicodeChar(long) ChpeAutoRtlUpcaseUnicodeChar
1292 stdcall RtlUpcaseUnicodeString(ptr ptr long) ChpeAutoRtlUpcaseUnicodeString
1293 stdcall RtlUpcaseUnicodeStringToAnsiString(ptr ptr long) ChpeAutoRtlUpcaseUnicodeStringToAnsiString
1294 stdcall RtlUpcaseUnicodeStringToCountedOemString(ptr ptr long) ChpeAutoRtlUpcaseUnicodeStringToCountedOemString
1295 stdcall RtlUpcaseUnicodeStringToOemString(ptr ptr long) ChpeAutoRtlUpcaseUnicodeStringToOemString
1296 stdcall RtlUpcaseUnicodeToCustomCPN(ptr ptr long ptr wstr long) ChpeAutoRtlUpcaseUnicodeToCustomCPN
1297 stdcall RtlUpcaseUnicodeToMultiByteN(ptr long ptr ptr long) ChpeAutoRtlUpcaseUnicodeToMultiByteN
1298 stdcall RtlUpcaseUnicodeToOemN(ptr long ptr ptr long) ChpeAutoRtlUpcaseUnicodeToOemN
1299 stdcall -version=0x600+ RtlUpdateClonedCriticalSection(long) ChpeStubRtlUpdateClonedCriticalSection
1300 stdcall -version=0x600+ RtlUpdateClonedSRWLock(ptr long) ChpeStubRtlUpdateClonedSRWLock
1301 stdcall RtlUpdateTimer(ptr ptr long long) ChpeAutoRtlUpdateTimer
1302 stdcall RtlUpperChar(long) ChpeAutoRtlUpperChar
1303 stdcall RtlUpperString(ptr ptr) ChpeAutoRtlUpperString
1305 stdcall RtlValidAcl(ptr) ChpeAutoRtlValidAcl
1306 stdcall RtlValidRelativeSecurityDescriptor(ptr long long) ChpeAutoRtlValidRelativeSecurityDescriptor
1307 stdcall RtlValidSecurityDescriptor(ptr) ChpeAutoRtlValidSecurityDescriptor
1308 stdcall RtlValidSid(ptr) ChpeAutoRtlValidSid
1309 stdcall RtlValidateHeap(long long ptr) ChpeAutoRtlValidateHeap
1310 stdcall RtlValidateProcessHeaps() ChpeAutoRtlValidateProcessHeaps
1311 stdcall RtlValidateUnicodeString(long ptr) ChpeAutoRtlValidateUnicodeString
1312 stdcall RtlVerifyVersionInfo(ptr long double) ChpeAutoRtlVerifyVersionInfo
1314 stdcall RtlVirtualUnwind2(long int64 int64 ptr ptr ptr ptr ptr ptr ptr ptr ptr long) ChpeAutoRtlVirtualUnwind2
1320 stdcall RtlWalkFrameChain(ptr long long) ChpeAutoRtlWalkFrameChain
1321 stdcall RtlWeaklyEnumerateEntryHashTable(ptr ptr) ChpeAutoRtlWeaklyEnumerateEntryHashTable
1322 stdcall RtlWalkHeap(long ptr) ChpeAutoRtlWalkHeap
1323 stdcall -version=0x600+ RtlWerpReportException(long long ptr long long ptr) ChpeStubRtlWerpReportException
1324 stdcall -version=0x600+ RtlWow64CallFunction64() ChpeStubRtlWow64CallFunction64
1325 stdcall RtlWow64EnableFsRedirection(long) ChpeAutoRtlWow64EnableFsRedirection
1326 stdcall RtlWow64EnableFsRedirectionEx(long ptr) ChpeAutoRtlWow64EnableFsRedirectionEx
1327 stdcall -version=0x600+ RtlOpenCrossProcessEmulatorWorkConnection(ptr ptr ptr) ChpeAutoRtlOpenCrossProcessEmulatorWorkConnection
1328 stdcall -version=0x600+ RtlWow64GetCpuAreaInfo(ptr long ptr) ChpeAutoRtlWow64GetCpuAreaInfo
1329 stdcall -version=0x600+ RtlWow64GetCurrentMachine() ChpeAutoRtlWow64GetCurrentMachine
1330 stdcall -version=0x600+ RtlWow64GetProcessMachines(ptr ptr ptr) ChpeAutoRtlWow64GetProcessMachines
1331 stdcall -version=0x600+ RtlWow64GetSharedInfoProcess(ptr ptr ptr) ChpeAutoRtlWow64GetSharedInfoProcess
1332 stdcall -version=0x600+ RtlWow64GetThreadContext(ptr ptr) ChpeAutoRtlWow64GetThreadContext
1334 stdcall -version=0x600+ RtlWow64IsWowGuestMachineSupported(long ptr) ChpeAutoRtlWow64IsWowGuestMachineSupported
1335 stdcall -version=0x600+ RtlWow64LogMessageInEventLogger(long long long) ChpeStubRtlWow64LogMessageInEventLogger
1336 stdcall -version=0x600+ RtlWow64PopCrossProcessWorkFromFreeList(ptr) ChpeAutoRtlWow64PopCrossProcessWorkFromFreeList
1337 stdcall -version=0x600+ RtlWow64PushCrossProcessWorkOntoWorkList(ptr ptr ptr) ChpeAutoRtlWow64PushCrossProcessWorkOntoWorkList
1338 stdcall -version=0x600+ RtlWow64RequestCrossProcessHeavyFlush(ptr) ChpeAutoRtlWow64RequestCrossProcessHeavyFlush
1339 stdcall -version=0x600+ RtlWow64SetThreadContext(ptr ptr) ChpeAutoRtlWow64SetThreadContext
1340 stdcall -version=0x600+ RtlWow64SuspendThread(ptr ptr) ChpeAutoRtlWow64SuspendThread
1341 stdcall RtlWriteMemoryStream(ptr ptr long ptr) ChpeAutoRtlWriteMemoryStream
1342 stdcall RtlWriteRegistryValue(long ptr ptr long ptr long) ChpeAutoRtlWriteRegistryValue
1343 stdcall RtlZeroHeap(ptr long) ChpeAutoRtlZeroHeap
1345 stdcall RtlZombifyActivationContext(ptr) ChpeAutoRtlZombifyActivationContext
1347 stdcall -version=0x600+ RtlpCheckDynamicTimeZoneInformation(ptr long) ChpeStubRtlpCheckDynamicTimeZoneInformation
1348 stdcall -version=0x600+ RtlpCleanupRegistryKeys() ChpeStubRtlpCleanupRegistryKeys
1349 stdcall -version=0x600+ RtlpConvertCultureNamesToLCIDs(wstr ptr) ChpeStubRtlpConvertCultureNamesToLCIDs
1350 stdcall -version=0x600+ RtlpConvertLCIDsToCultureNames(wstr ptr) ChpeStubRtlpConvertLCIDsToCultureNames
1351 stdcall -version=0x600+ RtlpCreateProcessRegistryInfo(ptr) ChpeStubRtlpCreateProcessRegistryInfo
1352 stdcall RtlpEnsureBufferSize(long ptr long) ChpeAutoRtlpEnsureBufferSize
1353 stdcall -version=0x600+ RtlpGetLCIDFromLangInfoNode(long long ptr) ChpeStubRtlpGetLCIDFromLangInfoNode
1354 stdcall -version=0x600+ RtlpGetNameFromLangInfoNode(long long long) ChpeStubRtlpGetNameFromLangInfoNode
1355 stdcall -version=0x600+ RtlpGetSystemDefaultUILanguage(ptr long) ChpeStubRtlpGetSystemDefaultUILanguage
1356 stdcall -version=0x600+ RtlpGetUserOrMachineUILanguage4NLS(long long ptr) ChpeStubRtlpGetUserOrMachineUILanguage4NLS
1357 stdcall -version=0x600+ RtlpInitializeLangRegistryInfo(ptr) ChpeStubRtlpInitializeLangRegistryInfo
1358 stdcall -version=0x600+ RtlpIsQualifiedLanguage(long ptr long) ChpeStubRtlpIsQualifiedLanguage
1359 stdcall -version=0x600+ RtlpLoadMachineUIByPolicy(ptr long ptr) ChpeStubRtlpLoadMachineUIByPolicy
1360 stdcall -version=0x600+ RtlpLoadUserUIByPolicy(ptr long ptr) ChpeStubRtlpLoadUserUIByPolicy
1361 stdcall -version=0x600+ RtlpMuiFreeLangRegistryInfo(long) ChpeStubRtlpMuiFreeLangRegistryInfo
1362 stdcall -version=0x600+ RtlpMuiRegCreateRegistryInfo() ChpeStubRtlpMuiRegCreateRegistryInfo
1363 stdcall -version=0x600+ RtlpMuiRegFreeRegistryInfo(long long) ChpeStubRtlpMuiRegFreeRegistryInfo
1364 stdcall -version=0x600+ RtlpMuiRegLoadRegistryInfo(long long) ChpeStubRtlpMuiRegLoadRegistryInfo
1365 stdcall RtlpNotOwnerCriticalSection(ptr) ChpeAutoRtlpNotOwnerCriticalSection
1366 stdcall RtlpNtCreateKey(ptr long ptr long ptr ptr) ChpeAutoRtlpNtCreateKey
1367 stdcall RtlpNtEnumerateSubKey(ptr ptr long long) ChpeAutoRtlpNtEnumerateSubKey
1368 stdcall RtlpNtMakeTemporaryKey(ptr) ChpeAutoRtlpNtMakeTemporaryKey
1369 stdcall RtlpNtOpenKey(ptr long ptr long) ChpeAutoRtlpNtOpenKey
1370 stdcall RtlpNtQueryValueKey(ptr ptr ptr ptr long) ChpeAutoRtlpNtQueryValueKey
1371 stdcall RtlpNtSetValueKey(ptr long ptr long) ChpeAutoRtlpNtSetValueKey
1372 stdcall -version=0x600+ RtlpQueryDefaultUILanguage(ptr long) ChpeStubRtlpQueryDefaultUILanguage
1373 stdcall -version=0x600+ RtlpQueryProcessDebugInformationFromWow64(long ptr) ChpeStubRtlpQueryProcessDebugInformationFromWow64
1374 stdcall -version=0x600+ RtlpRefreshCachedUILanguage(wstr long) ChpeStubRtlpRefreshCachedUILanguage
1375 stdcall -version=0x600+ RtlpSetInstallLanguage(long ptr) ChpeStubRtlpSetInstallLanguage
1376 stdcall -version=0x600+ RtlpSetPreferredUILanguages(long ptr ptr) ChpeStubRtlpSetPreferredUILanguages
1377 stdcall RtlpUnWaitCriticalSection(ptr) ChpeAutoRtlpUnWaitCriticalSection
1378 stdcall -version=0x600+ RtlpVerifyAndCommitUILanguageSettings(long) ChpeStubRtlpVerifyAndCommitUILanguageSettings
1379 stdcall RtlpWaitForCriticalSection(ptr) ChpeAutoRtlpWaitForCriticalSection
1380 stdcall RtlxAnsiStringToUnicodeSize(ptr) ChpeAutoRtlxAnsiStringToUnicodeSize
1381 stdcall RtlxOemStringToUnicodeSize(ptr) ChpeAutoRtlxOemStringToUnicodeSize
1382 stdcall RtlxUnicodeStringToAnsiSize(ptr) ChpeAutoRtlxUnicodeStringToAnsiSize
1383 stdcall RtlxUnicodeStringToOemSize(ptr) ChpeAutoRtlxUnicodeStringToOemSize
1384 stdcall -version=0x600+ ShipAssert(long long) ChpeStubShipAssert
1385 stdcall -version=0x600+ ShipAssertGetBufferInfo(ptr ptr) ChpeStubShipAssertGetBufferInfo
1386 stdcall -version=0x600+ ShipAssertMsgA(long long) ChpeStubShipAssertMsgA
1387 stdcall -version=0x600+ ShipAssertMsgW(long long) ChpeStubShipAssertMsgW
1388 stdcall -version=0x600+ TpAllocAlpcCompletion(ptr ptr ptr ptr ptr) ChpeAutoTpAllocAlpcCompletion
1389 stdcall -version=0x600+ TpAllocAlpcCompletionEx(ptr ptr ptr ptr ptr) ChpeAutoTpAllocAlpcCompletionEx
1390 stdcall -version=0x600+ TpAllocCleanupGroup(ptr) ChpeAutoTpAllocCleanupGroup
1391 stdcall -version=0x600+ TpAllocIoCompletion(ptr ptr ptr ptr ptr) ChpeAutoTpAllocIoCompletion
1392 stdcall -version=0x600+ TpAllocPool(ptr ptr) ChpeAutoTpAllocPool
1393 stdcall -version=0x600+ TpAllocTimer(ptr ptr ptr ptr) ChpeAutoTpAllocTimer
1394 stdcall -version=0x600+ TpAllocWait(ptr ptr ptr ptr) ChpeAutoTpAllocWait
1395 stdcall -version=0x600+ TpAllocWork(ptr ptr ptr ptr) ChpeAutoTpAllocWork
1396 stdcall -version=0x600+ TpAlpcRegisterCompletionList(ptr) ChpeAutoTpAlpcRegisterCompletionList
1397 stdcall -version=0x600+ TpAlpcUnregisterCompletionList(ptr) ChpeAutoTpAlpcUnregisterCompletionList
1399 stdcall -version=0x600+ TpCallbackMayRunLong(ptr) ChpeAutoTpCallbackMayRunLong
1402 stdcall -version=0x600+ TpCallbackSendAlpcMessageOnCompletion(ptr ptr long ptr) ChpeAutoTpCallbackSendAlpcMessageOnCompletion
1403 stdcall -version=0x600+ TpCallbackSendPendingAlpcMessage(ptr) ChpeAutoTpCallbackSendPendingAlpcMessage
1407 stdcall -version=0x600+ TpCaptureCaller(long) ChpeStubTpCaptureCaller
1408 stdcall -version=0x600+ TpCheckTerminateWorker(ptr) ChpeStubTpCheckTerminateWorker
1409 stdcall -version=0x600+ TpDbgDumpHeapUsage(long ptr long) ChpeStubTpDbgDumpHeapUsage
1410 stdcall -version=0x600+ TpDbgSetLogRoutine() ChpeStubTpDbgSetLogRoutine
1414 stdcall -version=0x601+ TpQueryPoolStackInformation(ptr ptr) ChpeAutoTpQueryPoolStackInformation
1415 stdcall -version=0x600+ TpReleaseAlpcCompletion(ptr) ChpeAutoTpReleaseAlpcCompletion
1425 stdcall -version=0x601+ TpSetPoolStackInformation(ptr ptr) ChpeAutoTpSetPoolStackInformation
1430 stdcall -version=0x600+ TpSimpleTryPost(ptr ptr ptr) ChpeAutoTpSimpleTryPost
1432 stdcall -version=0x600+ TpWaitForAlpcCompletion(ptr) ChpeAutoTpWaitForAlpcCompletion
1438 stdcall -version=0x600+ WerCheckEventEscalation(long ptr) ChpeStubWerCheckEventEscalation
1439 stdcall -version=0x600+ WerReportSQMEvent(long long long) ChpeStubWerReportSQMEvent
1440 stdcall -version=0x600+ WerReportWatsonEvent(long long long long) ChpeStubWerReportWatsonEvent
1441 stdcall -version=0x600+ WinSqmAddToStream(ptr long long long) ChpeStubWinSqmAddToStream
1442 stdcall -version=0x600+ WinSqmAddToStreamEx(ptr long long ptr long) ChpeStubWinSqmAddToStreamEx
1443 stdcall -version=0x600+ WinSqmEndSession(ptr) ChpeStubWinSqmEndSession
1444 stdcall -version=0x600+ WinSqmEventEnabled(long ptr) ChpeAutoWinSqmEventEnabled
1445 stdcall -version=0x600+ WinSqmEventWrite(long long long) ChpeStubWinSqmEventWrite
1446 stdcall -version=0x600+ WinSqmIncrementDWORD(long long long) ChpeAutoWinSqmIncrementDWORD
1447 stdcall -version=0x600+ WinSqmIsOptedIn() ChpeAutoWinSqmIsOptedIn
1448 stdcall -version=0x600+ WinSqmSetDWORD(ptr long long) ChpeAutoWinSqmSetDWORD
1449 stdcall -version=0x600+ WinSqmSetDWORD64(ptr long int64) ChpeAutoWinSqmSetDWORD64
1450 stdcall -version=0x600+ WinSqmSetString(ptr long ptr) ChpeStubWinSqmSetString
1451 stdcall -version=0x600+ WinSqmStartSession(ptr) ChpeStubWinSqmStartSession
1452 stdcall ZwAcceptConnectPort(ptr long ptr long long ptr) ChpeAutoZwAcceptConnectPort
1453 stdcall ZwAccessCheck(ptr long long ptr ptr ptr ptr ptr) ChpeAutoZwAccessCheck
1454 stdcall ZwAccessCheckAndAuditAlarm(ptr long ptr ptr ptr long ptr long ptr ptr ptr) ChpeAutoZwAccessCheckAndAuditAlarm
1455 stdcall ZwAccessCheckByType(ptr ptr ptr long ptr long ptr ptr long ptr ptr) ChpeAutoZwAccessCheckByType
1456 stdcall ZwAccessCheckByTypeAndAuditAlarm(ptr ptr ptr ptr ptr ptr long long long ptr long ptr long ptr ptr ptr) ChpeAutoZwAccessCheckByTypeAndAuditAlarm
1457 stdcall ZwAccessCheckByTypeResultList(ptr ptr ptr long ptr long ptr ptr long ptr ptr) ChpeAutoZwAccessCheckByTypeResultList
1458 stdcall ZwAccessCheckByTypeResultListAndAuditAlarm(ptr ptr ptr ptr ptr ptr long long long ptr long ptr long ptr ptr ptr) ChpeAutoZwAccessCheckByTypeResultListAndAuditAlarm
1459 stdcall ZwAccessCheckByTypeResultListAndAuditAlarmByHandle(ptr ptr ptr ptr ptr ptr ptr long long long ptr long ptr long ptr ptr ptr) ChpeAutoZwAccessCheckByTypeResultListAndAuditAlarmByHandle
1460 stub -version=0x600+ ZwAcquireCMFViewOwnership
1461 stdcall ZwAddAtom(ptr long ptr) ChpeAutoZwAddAtom
1462 stdcall ZwAddBootEntry(ptr long) ChpeAutoZwAddBootEntry
1463 stdcall ZwAddDriverEntry(ptr long) ChpeAutoZwAddDriverEntry
1464 stdcall ZwAdjustGroupsToken(long long long long long long) ChpeAutoZwAdjustGroupsToken
1465 stdcall ZwAdjustPrivilegesToken(long long long long long long) ChpeAutoZwAdjustPrivilegesToken
1466 stdcall ZwAlertResumeThread(long ptr) ChpeAutoZwAlertResumeThread
1467 stdcall ZwAlertThread(long) ChpeAutoZwAlertThread
1468 stdcall ZwAlertThreadByThreadId(long) ChpeAutoZwAlertThreadByThreadId
1469 stdcall ZwAllocateLocallyUniqueId(ptr) ChpeAutoZwAllocateLocallyUniqueId
1470 stdcall ZwAllocateUserPhysicalPages(ptr ptr ptr) ChpeAutoZwAllocateUserPhysicalPages
1471 stdcall ZwAllocateUuids(ptr ptr ptr ptr) ChpeAutoZwAllocateUuids
1472 stdcall ZwAllocateVirtualMemory(long ptr ptr ptr long long) ChpeAutoZwAllocateVirtualMemory
1473 stdcall ZwAllocateVirtualMemoryEx(long ptr ptr long long ptr long) ChpeAutoZwAllocateVirtualMemoryEx
1474 stdcall -version=0x600+ ZwAlpcAcceptConnectPort(ptr ptr long ptr ptr ptr ptr ptr long) ChpeAutoZwAlpcAcceptConnectPort
1475 stdcall -version=0x600+ ZwAlpcCancelMessage(ptr long ptr) ChpeAutoZwAlpcCancelMessage
1476 stdcall -version=0x600+ ZwAlpcConnectPort(ptr ptr ptr ptr long ptr ptr ptr ptr ptr ptr) ChpeAutoZwAlpcConnectPort
1477 stdcall -version=0x602+ ZwAlpcConnectPortEx(ptr ptr ptr ptr long ptr ptr ptr ptr ptr ptr) ChpeAutoZwAlpcConnectPortEx
1478 stdcall -version=0x600+ ZwAlpcCreatePort(ptr ptr ptr) ChpeAutoZwAlpcCreatePort
1479 stdcall -version=0x600+ ZwAlpcCreatePortSection(ptr long ptr long ptr ptr) ChpeAutoZwAlpcCreatePortSection
1480 stdcall -version=0x600+ ZwAlpcCreateResourceReserve(ptr long long ptr) ChpeAutoZwAlpcCreateResourceReserve
1481 stdcall -version=0x600+ ZwAlpcCreateSectionView(ptr long ptr) ChpeAutoZwAlpcCreateSectionView
1482 stdcall -version=0x600+ ZwAlpcCreateSecurityContext(ptr long ptr) ChpeAutoZwAlpcCreateSecurityContext
1483 stdcall -version=0x600+ ZwAlpcDeletePortSection(ptr long ptr) ChpeAutoZwAlpcDeletePortSection
1484 stdcall -version=0x600+ ZwAlpcDeleteResourceReserve(ptr long long) ChpeAutoZwAlpcDeleteResourceReserve
1485 stdcall -version=0x600+ ZwAlpcDeleteSectionView(ptr long ptr) ChpeAutoZwAlpcDeleteSectionView
1486 stdcall -version=0x600+ ZwAlpcDeleteSecurityContext(ptr long ptr) ChpeAutoZwAlpcDeleteSecurityContext
1487 stdcall -version=0x600+ ZwAlpcDisconnectPort(ptr long) ChpeAutoZwAlpcDisconnectPort
1488 stdcall -version=0x600+ ZwAlpcImpersonateClientOfPort(ptr ptr ptr) ChpeAutoZwAlpcImpersonateClientOfPort
1489 stdcall -version=0xA00+ ZwAlpcImpersonateClientContainerOfPort(ptr ptr long) ChpeAutoZwAlpcImpersonateClientContainerOfPort
1490 stdcall -version=0x600+ ZwAlpcOpenSenderProcess(ptr ptr ptr long long ptr) ChpeAutoZwAlpcOpenSenderProcess
1491 stdcall -version=0x600+ ZwAlpcOpenSenderThread(ptr ptr ptr long long ptr) ChpeAutoZwAlpcOpenSenderThread
1492 stdcall -version=0x600+ ZwAlpcQueryInformation(ptr long ptr long ptr) ChpeAutoZwAlpcQueryInformation
1493 stdcall -version=0x600+ ZwAlpcQueryInformationMessage(ptr ptr long ptr long ptr) ChpeAutoZwAlpcQueryInformationMessage
1494 stdcall -version=0x600+ ZwAlpcRevokeSecurityContext(ptr long ptr) ChpeAutoZwAlpcRevokeSecurityContext
1495 stdcall -version=0x600+ ZwAlpcSendWaitReceivePort(ptr long ptr ptr ptr ptr ptr ptr) ChpeAutoZwAlpcSendWaitReceivePort
1496 stdcall -version=0x600+ ZwAlpcSetInformation(ptr long ptr long) ChpeAutoZwAlpcSetInformation
1497 stdcall ZwApphelpCacheControl(long ptr) ChpeAutoZwApphelpCacheControl
1498 stdcall ZwAreMappedFilesTheSame(ptr ptr) ChpeAutoZwAreMappedFilesTheSame
1499 stdcall ZwAssignProcessToJobObject(long long) ChpeAutoZwAssignProcessToJobObject
1500 stdcall ZwCallbackReturn(ptr long long) ChpeAutoZwCallbackReturn
1501 stdcall ZwCancelDeviceWakeupRequest(ptr) ChpeAutoZwCancelDeviceWakeupRequest
1502 stdcall ZwCancelIoFile(long ptr) ChpeAutoZwCancelIoFile
1503 stdcall -version=0x600+ ZwCancelIoFileEx(ptr ptr ptr) ChpeAutoZwCancelIoFileEx
1504 stdcall -version=0x600+ ZwCancelSynchronousIoFile(ptr ptr ptr) ChpeAutoZwCancelSynchronousIoFile
1505 stdcall ZwCancelTimer(long ptr) ChpeAutoZwCancelTimer
1506 stdcall ZwClearEvent(long) ChpeAutoZwClearEvent
1507 stdcall ZwClose(long) ChpeAutoZwClose
1508 stdcall ZwCloseObjectAuditAlarm(ptr ptr long) ChpeAutoZwCloseObjectAuditAlarm
1509 stdcall -version=0x600+ ZwCommitComplete(ptr ptr) ChpeAutoZwCommitComplete
1510 stdcall -version=0x600+ ZwCommitEnlistment(ptr ptr) ChpeAutoZwCommitEnlistment
1511 stdcall -version=0x600+ ZwCommitTransaction(ptr long) ChpeAutoZwCommitTransaction
1512 stdcall ZwCompactKeys(long ptr) ChpeAutoZwCompactKeys
1513 stdcall ZwCompareTokens(ptr ptr ptr) ChpeAutoZwCompareTokens
1514 stdcall ZwCompleteConnectPort(ptr) ChpeAutoZwCompleteConnectPort
1515 stdcall ZwCompressKey(ptr) ChpeAutoZwCompressKey
1516 stdcall ZwConnectPort(ptr ptr ptr ptr ptr ptr ptr ptr) ChpeAutoZwConnectPort
1517 stdcall ZwContinue(ptr long) ChpeAutoZwContinue
1518 stdcall ZwCreateDebugObject(ptr long ptr long) ChpeAutoZwCreateDebugObject
1519 stdcall ZwCreateDirectoryObject(long long long) ChpeAutoZwCreateDirectoryObject
1520 stdcall -version=0x600+ ZwCreateEnlistment(ptr long ptr ptr ptr long long ptr) ChpeAutoZwCreateEnlistment
1521 stdcall ZwCreateEvent(long long long long long) ChpeAutoZwCreateEvent
1522 stdcall ZwCreateEventPair(ptr long ptr) ChpeAutoZwCreateEventPair
1523 stdcall ZwCreateFile(ptr long ptr ptr long long long ptr long long ptr) ChpeAutoZwCreateFile
1524 stdcall ZwCreateIoCompletion(ptr long ptr long) ChpeAutoZwCreateIoCompletion
1525 stdcall -version=0x602+ ZwCreateWaitCompletionPacket(ptr long ptr) ChpeAutoZwCreateWaitCompletionPacket
1526 stdcall -version=0x602+ ZwAssociateWaitCompletionPacket(ptr ptr ptr ptr ptr long ptr ptr) ChpeAutoZwAssociateWaitCompletionPacket
1527 stdcall -version=0x602+ ZwCancelWaitCompletionPacket(ptr long) ChpeAutoZwCancelWaitCompletionPacket
1528 stdcall ZwCreateJobObject(ptr long ptr) ChpeAutoZwCreateJobObject
1529 stdcall ZwCreateJobSet(long ptr long) ChpeAutoZwCreateJobSet
1530 stdcall ZwCreateKey(ptr long ptr long ptr long long) ChpeAutoZwCreateKey
1531 stdcall -version=0x600+ ZwCreateKeyTransacted(ptr long ptr long ptr long ptr ptr) ChpeAutoZwCreateKeyTransacted
1532 stdcall ZwCreateKeyedEvent(ptr long ptr long) ChpeAutoZwCreateKeyedEvent
1533 stdcall ZwCreateMailslotFile(long long long long long long long long) ChpeAutoZwCreateMailslotFile
1534 stdcall ZwCreateMutant(ptr long ptr long) ChpeAutoZwCreateMutant
1535 stdcall ZwCreateNamedPipeFile(ptr long ptr ptr long long long long long long long long long ptr) ChpeAutoZwCreateNamedPipeFile
1536 stdcall ZwCreatePagingFile(ptr ptr ptr long) ChpeAutoZwCreatePagingFile
1537 stdcall ZwCreatePort(ptr ptr long long long) ChpeAutoZwCreatePort
1538 stdcall ZwCreateProcess(ptr long ptr ptr long ptr ptr ptr) ChpeAutoZwCreateProcess
1539 stdcall ZwCreateProcessEx(ptr long ptr ptr long ptr ptr ptr long) ChpeAutoZwCreateProcessEx
1540 stdcall ZwCreateProfile(ptr ptr ptr long long ptr long long long) ChpeAutoZwCreateProfile
1541 stdcall -version=0x600+ ZwCreateResourceManager(ptr long ptr ptr ptr long ptr) ChpeAutoZwCreateResourceManager
1542 stdcall ZwCreateSection(ptr long ptr ptr long long long) ChpeAutoZwCreateSection
1543 stdcall ZwCreateSemaphore(ptr long ptr long long) ChpeAutoZwCreateSemaphore
1544 stdcall ZwCreateSymbolicLinkObject(ptr long ptr ptr) ChpeAutoZwCreateSymbolicLinkObject
1545 stdcall ZwCreateThread(ptr long ptr ptr ptr ptr ptr long) ChpeAutoZwCreateThread
1546 stdcall -version=0x600+ ZwCreateThreadEx(ptr long ptr ptr ptr ptr long long long long ptr) ChpeAutoZwCreateThreadEx
1547 stdcall ZwCreateTimer(ptr long ptr long) ChpeAutoZwCreateTimer
1548 stdcall ZwCreateToken(ptr long ptr long ptr ptr ptr ptr ptr ptr ptr ptr ptr) ChpeAutoZwCreateToken
1549 stdcall -version=0x600+ ZwCreateTransaction(ptr long ptr ptr ptr long long long ptr ptr) ChpeAutoZwCreateTransaction
1550 stdcall -version=0x600+ ZwCreateTransactionManager(ptr long ptr ptr long long) ChpeAutoZwCreateTransactionManager
1551 stdcall -version=0x600+ ZwCreateUserProcess(ptr ptr long long ptr ptr long long ptr ptr ptr) ChpeAutoZwCreateUserProcess
1552 stdcall ZwCreateWaitablePort(ptr ptr long long long) ChpeAutoZwCreateWaitablePort
1553 stdcall -version=0x602+ ZwCreateWnfStateName(ptr long long long ptr long ptr) ChpeAutoZwCreateWnfStateName
1554 stdcall -version=0x600+ ZwCreateWorkerFactory(ptr long ptr ptr ptr ptr ptr long long long) ChpeStubZwCreateWorkerFactory
1555 stdcall ZwDebugActiveProcess(ptr ptr) ChpeAutoZwDebugActiveProcess
1556 stdcall ZwDebugContinue(ptr ptr long) ChpeAutoZwDebugContinue
1557 stdcall ZwDelayExecution(long ptr) ChpeAutoZwDelayExecution
1558 stdcall ZwDeleteAtom(long) ChpeAutoZwDeleteAtom
1559 stdcall ZwDeleteBootEntry(long) ChpeAutoZwDeleteBootEntry
1560 stdcall ZwDeleteDriverEntry(long) ChpeAutoZwDeleteDriverEntry
1561 stdcall ZwDeleteFile(ptr) ChpeAutoZwDeleteFile
1562 stdcall ZwDeleteKey(long) ChpeAutoZwDeleteKey
1563 stdcall ZwDeleteObjectAuditAlarm(ptr ptr long) ChpeAutoZwDeleteObjectAuditAlarm
1564 stdcall -version=0x600+ ZwDeletePrivateNamespace(ptr) ChpeAutoZwDeletePrivateNamespace
1565 stdcall ZwDeleteValueKey(long ptr) ChpeAutoZwDeleteValueKey
1566 stdcall -version=0x602+ ZwDeleteWnfStateData(ptr ptr) ChpeAutoZwDeleteWnfStateData
1567 stdcall -version=0x602+ ZwDeleteWnfStateName(ptr) ChpeAutoZwDeleteWnfStateName
1568 stdcall ZwDeviceIoControlFile(long long long long long long long long long long) ChpeAutoZwDeviceIoControlFile
1569 stdcall ZwDisplayString(ptr) ChpeAutoZwDisplayString
1570 stdcall ZwDuplicateObject(long long long ptr long long long) ChpeAutoZwDuplicateObject
1571 stdcall ZwDuplicateToken(long long long long long long) ChpeAutoZwDuplicateToken
1572 stdcall ZwEnumerateBootEntries(ptr ptr) ChpeAutoZwEnumerateBootEntries
1573 stdcall ZwEnumerateDriverEntries(ptr ptr) ChpeAutoZwEnumerateDriverEntries
1574 stdcall ZwEnumerateKey(long long long ptr long ptr) ChpeAutoZwEnumerateKey
1575 stdcall ZwEnumerateSystemEnvironmentValuesEx(long ptr long) ChpeAutoZwEnumerateSystemEnvironmentValuesEx
1576 stdcall -version=0x600+ ZwEnumerateTransactionObject(ptr long ptr long ptr) ChpeAutoZwEnumerateTransactionObject
1577 stdcall ZwEnumerateValueKey(long long long ptr long ptr) ChpeAutoZwEnumerateValueKey
1578 stdcall ZwExtendSection(ptr ptr) ChpeAutoZwExtendSection
1579 stdcall ZwFilterToken(ptr long ptr ptr ptr ptr) ChpeAutoZwFilterToken
1580 stdcall -version=0x602+ ZwCreateLowBoxToken(ptr ptr long ptr ptr long ptr long ptr) ChpeAutoZwCreateLowBoxToken
1581 stdcall -version=0x600+ ZwCreatePrivateNamespace(ptr long ptr ptr) ChpeAutoZwCreatePrivateNamespace
1582 stdcall ZwFindAtom(ptr long ptr) ChpeAutoZwFindAtom
1583 stdcall ZwFlushBuffersFile(long ptr) ChpeAutoZwFlushBuffersFile
1584 stdcall ZwFlushBuffersFileEx(long long ptr long ptr) ChpeAutoZwFlushBuffersFileEx
1585 stdcall -version=0x600+ ZwFlushInstallUILanguage(long long) ChpeStubZwFlushInstallUILanguage
1586 stdcall ZwFlushInstructionCache(long ptr long) ChpeAutoZwFlushInstructionCache
1587 stdcall ZwFlushKey(long) ChpeAutoZwFlushKey
1588 stdcall -version=0x600+ ZwFlushProcessWriteBuffers() ChpeAutoZwFlushProcessWriteBuffers
1589 stdcall ZwFlushVirtualMemory(ptr ptr ptr ptr) ChpeAutoZwFlushVirtualMemory
1590 stdcall ZwFlushWriteBuffer() ChpeAutoZwFlushWriteBuffer
1591 stdcall ZwFreeUserPhysicalPages(ptr ptr ptr) ChpeAutoZwFreeUserPhysicalPages
1592 stdcall ZwFreeVirtualMemory(long ptr ptr long) ChpeAutoZwFreeVirtualMemory
1593 stdcall -version=0x600+ ZwFreezeRegistry(long) ChpeStubZwFreezeRegistry
1594 stdcall -version=0x600+ ZwFreezeTransactions(ptr ptr) ChpeStubZwFreezeTransactions
1595 stdcall ZwFsControlFile(long long long long long long long long long long) ChpeAutoZwFsControlFile
1596 stdcall ZwGetContextThread(long ptr) ChpeAutoZwGetContextThread
1597 stdcall ZwGetCurrentProcessorNumber() ChpeAutoZwGetCurrentProcessorNumber
1598 stdcall -version=0xA00+ ZwGetCurrentProcessorNumberEx(ptr) ChpeAutoZwGetCurrentProcessorNumberEx
1599 stdcall ZwGetDevicePowerState(ptr ptr) ChpeAutoZwGetDevicePowerState
1600 stdcall -version=0x600+ ZwGetMUIRegistryInfo(long ptr ptr) ChpeStubZwGetMUIRegistryInfo
1601 stdcall -version=0x600+ ZwGetNextProcess(ptr long long long ptr) ChpeStubZwGetNextProcess
1602 stdcall -version=0x600+ ZwGetNextThread(ptr ptr long long long ptr) ChpeAutoZwGetNextThread
1603 stdcall -version=0x600+ ZwGetNlsSectionPtr(long long ptr ptr ptr) ChpeAutoZwGetNlsSectionPtr
1604 stdcall -version=0x600+ ZwGetNotificationResourceManager(ptr ptr long ptr ptr long ptr) ChpeAutoZwGetNotificationResourceManager
1605 stdcall ZwGetPlugPlayEvent(long long ptr long) ChpeAutoZwGetPlugPlayEvent
1606 stdcall ZwGetWriteWatch(long long ptr long ptr ptr ptr) ChpeAutoZwGetWriteWatch
1607 stdcall ZwImpersonateAnonymousToken(ptr) ChpeAutoZwImpersonateAnonymousToken
1608 stdcall ZwImpersonateClientOfPort(ptr ptr) ChpeAutoZwImpersonateClientOfPort
1609 stdcall ZwImpersonateThread(ptr ptr ptr) ChpeAutoZwImpersonateThread
1610 stdcall -version=0x600+ ZwInitializeNlsFiles(ptr ptr ptr ptr) ChpeStubZwInitializeNlsFiles
1611 stdcall ZwInitializeRegistry(long) ChpeAutoZwInitializeRegistry
1612 stdcall ZwInitiatePowerAction(long long long long) ChpeAutoZwInitiatePowerAction
1613 stdcall ZwIsProcessInJob(long long) ChpeAutoZwIsProcessInJob
1614 stdcall ZwIsSystemResumeAutomatic() ChpeAutoZwIsSystemResumeAutomatic
1615 stdcall -version=0x600+ ZwIsUILanguageComitted() ChpeStubZwIsUILanguageComitted
1616 stdcall ZwListenPort(ptr ptr) ChpeAutoZwListenPort
1617 stdcall ZwLoadDriver(ptr) ChpeAutoZwLoadDriver
1618 stdcall ZwLoadKey2(ptr ptr long) ChpeAutoZwLoadKey2
1619 stdcall ZwLoadKey(ptr ptr) ChpeAutoZwLoadKey
1620 stdcall ZwLoadKeyEx(ptr ptr long ptr ptr long ptr ptr) ChpeAutoZwLoadKeyEx
1621 stdcall ZwLockFile(long long ptr ptr ptr ptr ptr ptr long long) ChpeAutoZwLockFile
1622 stdcall ZwLockProductActivationKeys(ptr ptr) ChpeAutoZwLockProductActivationKeys
1623 stdcall ZwLockRegistryKey(ptr) ChpeAutoZwLockRegistryKey
1624 stdcall ZwLockVirtualMemory(long ptr ptr long) ChpeAutoZwLockVirtualMemory
1625 stdcall ZwMakePermanentObject(ptr) ChpeAutoZwMakePermanentObject
1626 stdcall ZwMakeTemporaryObject(long) ChpeAutoZwMakeTemporaryObject
1627 stdcall -version=0x600+ ZwMapCMFModule(long long ptr ptr ptr) ChpeStubZwMapCMFModule
1628 stdcall ZwMapUserPhysicalPages(ptr ptr ptr) ChpeAutoZwMapUserPhysicalPages
1629 stdcall ZwMapUserPhysicalPagesScatter(ptr ptr ptr) ChpeAutoZwMapUserPhysicalPagesScatter
1630 stdcall ZwMapViewOfSection(long long ptr long long ptr ptr long long long) ChpeAutoZwMapViewOfSection
1631 stdcall ZwModifyBootEntry(ptr) ChpeAutoZwModifyBootEntry
1632 stdcall ZwModifyDriverEntry(ptr) ChpeAutoZwModifyDriverEntry
1633 stdcall ZwNotifyChangeDirectoryFile(long long ptr ptr ptr ptr long long long) ChpeAutoZwNotifyChangeDirectoryFile
1634 stdcall ZwNotifyChangeDirectoryFileEx(long long ptr ptr ptr ptr long long long long) ChpeAutoZwNotifyChangeDirectoryFileEx
1635 stdcall ZwNotifyChangeKey(long long ptr ptr ptr long long ptr long long) ChpeAutoZwNotifyChangeKey
1636 stdcall ZwNotifyChangeMultipleKeys(ptr long ptr ptr ptr ptr ptr long long ptr long long) ChpeAutoZwNotifyChangeMultipleKeys
1637 stdcall ZwOpenDirectoryObject(long long long) ChpeAutoZwOpenDirectoryObject
1638 stdcall -version=0x600+ ZwOpenEnlistment(ptr long ptr ptr ptr) ChpeAutoZwOpenEnlistment
1639 stdcall ZwOpenEvent(long long long) ChpeAutoZwOpenEvent
1640 stdcall ZwOpenEventPair(ptr long ptr) ChpeAutoZwOpenEventPair
1641 stdcall ZwOpenFile(ptr long ptr ptr long long) ChpeAutoZwOpenFile
1642 stdcall ZwOpenIoCompletion(ptr long ptr) ChpeAutoZwOpenIoCompletion
1643 stdcall ZwOpenJobObject(ptr long ptr) ChpeAutoZwOpenJobObject
1644 stdcall ZwOpenKey(ptr long ptr) ChpeAutoZwOpenKey
1645 stdcall ZwOpenKeyEx(ptr long ptr long) ChpeAutoZwOpenKeyEx
1646 stdcall -version=0x600+ ZwOpenKeyTransacted(ptr long ptr ptr) ChpeAutoZwOpenKeyTransacted
1647 stdcall ZwOpenKeyedEvent(ptr long ptr) ChpeAutoZwOpenKeyedEvent
1648 stdcall ZwOpenMutant(ptr long ptr) ChpeAutoZwOpenMutant
1649 stdcall ZwOpenObjectAuditAlarm(ptr ptr ptr ptr ptr ptr long long ptr long long ptr) ChpeAutoZwOpenObjectAuditAlarm
1650 stdcall -version=0x600+ ZwOpenPrivateNamespace(ptr long ptr ptr) ChpeAutoZwOpenPrivateNamespace
1651 stdcall ZwOpenProcess(ptr long ptr ptr) ChpeAutoZwOpenProcess
1652 stdcall ZwOpenProcessToken(long long ptr) ChpeAutoZwOpenProcessToken
1653 stdcall ZwOpenProcessTokenEx(long long long ptr) ChpeAutoZwOpenProcessTokenEx
1654 stdcall -version=0x600+ ZwOpenResourceManager(ptr long ptr ptr ptr) ChpeAutoZwOpenResourceManager
1655 stdcall ZwOpenSection(ptr long ptr) ChpeAutoZwOpenSection
1656 stdcall ZwOpenSemaphore(long long ptr) ChpeAutoZwOpenSemaphore
1657 stdcall -version=0x600+ ZwOpenSession(ptr long ptr) ChpeStubZwOpenSession
1658 stdcall ZwOpenSymbolicLinkObject(ptr long ptr) ChpeAutoZwOpenSymbolicLinkObject
1659 stdcall ZwOpenThread(ptr long ptr ptr) ChpeAutoZwOpenThread
1660 stdcall ZwOpenThreadToken(long long long ptr) ChpeAutoZwOpenThreadToken
1661 stdcall ZwOpenThreadTokenEx(long long long long ptr) ChpeAutoZwOpenThreadTokenEx
1662 stdcall ZwOpenTimer(ptr long ptr) ChpeAutoZwOpenTimer
1663 stdcall -version=0x600+ ZwOpenTransaction(ptr long ptr ptr ptr) ChpeAutoZwOpenTransaction
1664 stdcall -version=0x600+ ZwOpenTransactionManager(ptr long ptr ptr ptr long) ChpeAutoZwOpenTransactionManager
1665 stdcall ZwPlugPlayControl(ptr ptr long) ChpeAutoZwPlugPlayControl
1666 stdcall ZwPowerInformation(long ptr long ptr long) ChpeAutoZwPowerInformation
1667 stdcall -version=0x600+ ZwPrePrepareComplete(ptr ptr) ChpeAutoZwPrePrepareComplete
1668 stdcall -version=0x600+ ZwPrePrepareEnlistment(ptr ptr) ChpeAutoZwPrePrepareEnlistment
1669 stdcall -version=0x600+ ZwPrepareComplete(ptr ptr) ChpeAutoZwPrepareComplete
1670 stdcall -version=0x600+ ZwPrepareEnlistment(ptr ptr) ChpeAutoZwPrepareEnlistment
1671 stdcall ZwPrivilegeCheck(ptr ptr ptr) ChpeAutoZwPrivilegeCheck
1672 stdcall ZwPrivilegeObjectAuditAlarm(ptr ptr ptr long ptr long) ChpeAutoZwPrivilegeObjectAuditAlarm
1673 stdcall ZwPrivilegedServiceAuditAlarm(ptr ptr ptr ptr long) ChpeAutoZwPrivilegedServiceAuditAlarm
1674 stdcall -version=0x600+ ZwPropagationComplete(ptr long long ptr) ChpeAutoZwPropagationComplete
1675 stdcall -version=0x600+ ZwPropagationFailed(ptr long long) ChpeAutoZwPropagationFailed
1676 stdcall ZwProtectVirtualMemory(long ptr ptr long ptr) ChpeAutoZwProtectVirtualMemory
1677 stdcall ZwPulseEvent(long ptr) ChpeAutoZwPulseEvent
1678 stdcall ZwQueryAttributesFile(ptr ptr) ChpeAutoZwQueryAttributesFile
1679 stdcall ZwQueryBootEntryOrder(ptr ptr) ChpeAutoZwQueryBootEntryOrder
1680 stdcall ZwQueryBootOptions(ptr ptr) ChpeAutoZwQueryBootOptions
1681 stdcall ZwQueryDebugFilterState(long long) ChpeAutoZwQueryDebugFilterState
1682 stdcall ZwQueryDefaultLocale(long ptr) ChpeAutoZwQueryDefaultLocale
1683 stdcall ZwQueryDefaultUILanguage(ptr) ChpeAutoZwQueryDefaultUILanguage
1684 stdcall ZwQueryDirectoryFile(long long ptr ptr ptr ptr long long long ptr long) ChpeAutoZwQueryDirectoryFile
1685 stdcall ZwQueryDirectoryFileEx(long long ptr ptr ptr ptr long long long ptr) ChpeAutoZwQueryDirectoryFileEx
1686 stdcall ZwQueryDirectoryObject(long ptr long long long ptr ptr) ChpeAutoZwQueryDirectoryObject
1687 stdcall ZwQueryDriverEntryOrder(ptr ptr) ChpeAutoZwQueryDriverEntryOrder
1688 stdcall ZwQueryEaFile(long ptr ptr long long ptr long ptr long) ChpeAutoZwQueryEaFile
1689 stdcall ZwQueryEvent(long long ptr long ptr) ChpeAutoZwQueryEvent
1690 stdcall ZwQueryFullAttributesFile(ptr ptr) ChpeAutoZwQueryFullAttributesFile
1691 stdcall ZwQueryInformationAtom(long long ptr long ptr) ChpeAutoZwQueryInformationAtom
1692 stdcall ZwQueryInformationByName(ptr ptr ptr long long) ChpeAutoZwQueryInformationByName
1693 stdcall -version=0x600+ ZwQueryInformationEnlistment(ptr long ptr long ptr) ChpeAutoZwQueryInformationEnlistment
1694 stdcall ZwQueryInformationFile(long ptr ptr long long) ChpeAutoZwQueryInformationFile
1695 stdcall ZwQueryInformationJobObject(long long ptr long ptr) ChpeAutoZwQueryInformationJobObject
1696 stdcall ZwQueryInformationPort(ptr long ptr long ptr) ChpeAutoZwQueryInformationPort
1697 stdcall ZwQueryInformationProcess(long long ptr long ptr) ChpeAutoZwQueryInformationProcess
1698 stdcall -version=0x600+ ZwQueryInformationResourceManager(ptr long ptr long ptr) ChpeAutoZwQueryInformationResourceManager
1699 stdcall ZwQueryInformationThread(long long ptr long ptr) ChpeAutoZwQueryInformationThread
1700 stdcall ZwQueryInformationToken(long long ptr long ptr) ChpeAutoZwQueryInformationToken
1701 stdcall -version=0x600+ ZwQueryInformationTransaction(ptr long ptr long ptr) ChpeAutoZwQueryInformationTransaction
1702 stdcall -version=0x600+ ZwQueryInformationTransactionManager(ptr long ptr long ptr) ChpeAutoZwQueryInformationTransactionManager
1703 stdcall -version=0x600+ ZwQueryInformationWorkerFactory(ptr long ptr long ptr) ChpeStubZwQueryInformationWorkerFactory
1704 stdcall ZwQueryInstallUILanguage(ptr) ChpeAutoZwQueryInstallUILanguage
1705 stdcall ZwQueryIntervalProfile(long ptr) ChpeAutoZwQueryIntervalProfile
1706 stdcall ZwQueryIoCompletion(long long ptr long ptr) ChpeAutoZwQueryIoCompletion
1707 stdcall ZwQueryKey(long long ptr long ptr) ChpeAutoZwQueryKey
1708 stdcall -version=0x600+ ZwQueryLicenseValue(ptr ptr ptr long ptr) ChpeAutoZwQueryLicenseValue
1709 stdcall ZwQueryMultipleValueKey(long ptr long ptr long ptr) ChpeAutoZwQueryMultipleValueKey
1710 stdcall ZwQueryMutant(long long ptr long ptr) ChpeAutoZwQueryMutant
1711 stdcall ZwQueryObject(long long long long long) ChpeAutoZwQueryObject
1712 stdcall ZwQueryOpenSubKeys(ptr ptr) ChpeAutoZwQueryOpenSubKeys
1713 stdcall ZwQueryOpenSubKeysEx(ptr long ptr ptr) ChpeAutoZwQueryOpenSubKeysEx
1714 stdcall ZwQueryPerformanceCounter(long long) ChpeAutoZwQueryPerformanceCounter
1715 stdcall ZwQueryPortInformationProcess() ChpeAutoZwQueryPortInformationProcess
1716 stdcall ZwQueryQuotaInformationFile(ptr ptr ptr long long ptr long ptr long) ChpeAutoZwQueryQuotaInformationFile
1717 stdcall ZwQuerySection(long long long long long) ChpeAutoZwQuerySection
1718 stdcall ZwQuerySecurityObject(long long long long long) ChpeAutoZwQuerySecurityObject
1719 stdcall ZwQuerySemaphore(long long long long long) ChpeAutoZwQuerySemaphore
1720 stdcall ZwQuerySymbolicLinkObject(long ptr ptr) ChpeAutoZwQuerySymbolicLinkObject
1721 stdcall ZwQuerySystemEnvironmentValue(ptr ptr long ptr) ChpeAutoZwQuerySystemEnvironmentValue
1722 stdcall ZwQuerySystemEnvironmentValueEx(ptr ptr ptr ptr ptr) ChpeAutoZwQuerySystemEnvironmentValueEx
1723 stdcall ZwQuerySystemInformation(long long long long) ChpeAutoZwQuerySystemInformation
1724 stdcall -version=0x601+ ZwQuerySystemInformationEx(long ptr long ptr long ptr) ChpeAutoZwQuerySystemInformationEx
1725 stdcall ZwQuerySystemTime(ptr) ChpeAutoZwQuerySystemTime
1726 stdcall ZwQueryTimer(ptr long ptr long ptr) ChpeAutoZwQueryTimer
1727 stdcall ZwQueryTimerResolution(long long long) ChpeAutoZwQueryTimerResolution
1728 stdcall ZwQueryValueKey(long ptr long ptr long ptr) ChpeAutoZwQueryValueKey
1729 stdcall ZwQueryVirtualMemory(long ptr long ptr long ptr) ChpeAutoZwQueryVirtualMemory
1730 stdcall ZwQueryVolumeInformationFile(long ptr ptr long long) ChpeAutoZwQueryVolumeInformationFile
1731 stdcall -version=0x602+ ZwQueryWnfStateData(ptr ptr ptr ptr ptr ptr) ChpeAutoZwQueryWnfStateData
1732 stdcall -version=0x602+ ZwQueryWnfStateNameInformation(ptr long ptr ptr long) ChpeAutoZwQueryWnfStateNameInformation
1733 stdcall ZwQueueApcThread(long ptr long long long) ChpeAutoZwQueueApcThread
1734 stdcall ZwRaiseException(ptr ptr long) ChpeAutoZwRaiseException
1735 stdcall ZwRaiseHardError(long long long ptr long ptr) ChpeAutoZwRaiseHardError
1736 stdcall ZwReadFile(long long ptr ptr ptr ptr long ptr ptr) ChpeAutoZwReadFile
1737 stdcall ZwReadFileScatter(long long ptr ptr ptr ptr long ptr ptr) ChpeAutoZwReadFileScatter
1738 stdcall -version=0x600+ ZwReadOnlyEnlistment(ptr ptr) ChpeAutoZwReadOnlyEnlistment
1739 stdcall ZwReadRequestData(ptr ptr long ptr long ptr) ChpeAutoZwReadRequestData
1740 stdcall ZwReadVirtualMemory(long ptr ptr long ptr) ChpeAutoZwReadVirtualMemory
1741 stdcall -version=0x600+ ZwRecoverEnlistment(ptr ptr) ChpeAutoZwRecoverEnlistment
1742 stdcall -version=0x600+ ZwRecoverResourceManager(ptr) ChpeAutoZwRecoverResourceManager
1743 stdcall -version=0x600+ ZwRecoverTransactionManager(ptr) ChpeAutoZwRecoverTransactionManager
1744 stdcall -version=0x600+ ZwRegisterProtocolAddressInformation(ptr ptr long ptr long) ChpeAutoZwRegisterProtocolAddressInformation
1745 stdcall ZwRegisterThreadTerminatePort(ptr) ChpeAutoZwRegisterThreadTerminatePort
1746 stdcall -version=0x600+ ZwReleaseCMFViewOwnership() ChpeStubZwReleaseCMFViewOwnership
1747 stdcall ZwReleaseKeyedEvent(ptr ptr long ptr) ChpeAutoZwReleaseKeyedEvent
1748 stdcall ZwReleaseMutant(long ptr) ChpeAutoZwReleaseMutant
1749 stdcall ZwReleaseSemaphore(long long ptr) ChpeAutoZwReleaseSemaphore
1750 stdcall -version=0x600+ ZwReleaseWorkerFactoryWorker(ptr) ChpeStubZwReleaseWorkerFactoryWorker
1751 stdcall ZwRemoveIoCompletion(ptr ptr ptr ptr ptr) ChpeAutoZwRemoveIoCompletion
1752 stdcall -version=0x600+ ZwRemoveIoCompletionEx(ptr ptr long ptr ptr long) ChpeAutoZwRemoveIoCompletionEx
1753 stdcall ZwRemoveProcessDebug(ptr ptr) ChpeAutoZwRemoveProcessDebug
1754 stdcall ZwRenameKey(ptr ptr) ChpeAutoZwRenameKey
1755 stdcall -version=0x600+ ZwRenameTransactionManager(ptr ptr) ChpeAutoZwRenameTransactionManager
1756 stdcall ZwReplaceKey(ptr long ptr) ChpeAutoZwReplaceKey
1757 stdcall -version=0x600+ ZwReplacePartitionUnit(wstr wstr long) ChpeStubZwReplacePartitionUnit
1758 stdcall ZwReplyPort(ptr ptr) ChpeAutoZwReplyPort
1759 stdcall ZwReplyWaitReceivePort(ptr ptr ptr ptr) ChpeAutoZwReplyWaitReceivePort
1760 stdcall ZwReplyWaitReceivePortEx(ptr ptr ptr ptr ptr) ChpeAutoZwReplyWaitReceivePortEx
1761 stdcall ZwReplyWaitReplyPort(ptr ptr) ChpeAutoZwReplyWaitReplyPort
1762 stdcall ZwRequestDeviceWakeup(ptr) ChpeAutoZwRequestDeviceWakeup
1763 stdcall ZwRequestPort(ptr ptr) ChpeAutoZwRequestPort
1764 stdcall ZwRequestWaitReplyPort(ptr ptr ptr) ChpeAutoZwRequestWaitReplyPort
1765 stdcall ZwRequestWakeupLatency(long) ChpeAutoZwRequestWakeupLatency
1766 stdcall ZwResetEvent(long ptr) ChpeAutoZwResetEvent
1767 stdcall ZwResetWriteWatch(long ptr long) ChpeAutoZwResetWriteWatch
1768 stdcall ZwRestoreKey(long long long) ChpeAutoZwRestoreKey
1769 stdcall ZwResumeProcess(ptr) ChpeAutoZwResumeProcess
1770 stdcall ZwResumeThread(long long) ChpeAutoZwResumeThread
1771 stdcall -version=0x600+ ZwRollbackComplete(ptr ptr) ChpeAutoZwRollbackComplete
1772 stdcall -version=0x600+ ZwRollbackEnlistment(ptr ptr) ChpeAutoZwRollbackEnlistment
1773 stdcall -version=0x600+ ZwRollbackTransaction(ptr long) ChpeAutoZwRollbackTransaction
1774 stdcall -version=0x600+ ZwRollforwardTransactionManager(ptr ptr) ChpeAutoZwRollforwardTransactionManager
1775 stdcall ZwSaveKey(long long) ChpeAutoZwSaveKey
1776 stdcall ZwSaveKeyEx(ptr ptr long) ChpeAutoZwSaveKeyEx
1777 stdcall ZwSaveMergedKeys(ptr ptr ptr) ChpeAutoZwSaveMergedKeys
1778 stdcall ZwSecureConnectPort(ptr ptr ptr ptr ptr ptr ptr ptr ptr) ChpeAutoZwSecureConnectPort
1779 stdcall ZwSetBootEntryOrder(ptr ptr) ChpeAutoZwSetBootEntryOrder
1780 stdcall ZwSetBootOptions(ptr long) ChpeAutoZwSetBootOptions
1781 stdcall ZwSetContextThread(long ptr) ChpeAutoZwSetContextThread
1782 stdcall ZwSetDebugFilterState(long long long) ChpeAutoZwSetDebugFilterState
1783 stdcall ZwSetDefaultHardErrorPort(ptr) ChpeAutoZwSetDefaultHardErrorPort
1784 stdcall ZwSetDefaultLocale(long long) ChpeAutoZwSetDefaultLocale
1785 stdcall ZwSetDefaultUILanguage(long) ChpeAutoZwSetDefaultUILanguage
1786 stdcall ZwSetDriverEntryOrder(ptr ptr) ChpeAutoZwSetDriverEntryOrder
1787 stdcall ZwSetEaFile(long ptr ptr long) ChpeAutoZwSetEaFile
1788 stdcall ZwSetEvent(long long) ChpeAutoZwSetEvent
1789 stdcall ZwSetEventBoostPriority(ptr) ChpeAutoZwSetEventBoostPriority
1790 stdcall ZwSetHighEventPair(ptr) ChpeAutoZwSetHighEventPair
1791 stdcall ZwSetHighWaitLowEventPair(ptr) ChpeAutoZwSetHighWaitLowEventPair
1792 stdcall ZwSetInformationDebugObject(ptr long ptr long ptr) ChpeAutoZwSetInformationDebugObject
1793 stdcall -version=0x600+ ZwSetInformationEnlistment(ptr long ptr long) ChpeAutoZwSetInformationEnlistment
1794 stdcall ZwSetInformationFile(long long long long long) ChpeAutoZwSetInformationFile
1795 stdcall ZwSetInformationJobObject(long long ptr long) ChpeAutoZwSetInformationJobObject
1796 stdcall ZwSetInformationKey(long long ptr long) ChpeAutoZwSetInformationKey
1797 stdcall ZwSetInformationObject(long long ptr long) ChpeAutoZwSetInformationObject
1798 stdcall ZwSetInformationProcess(long long long long) ChpeAutoZwSetInformationProcess
1799 stdcall -version=0x600+ ZwSetInformationResourceManager(ptr long ptr long) ChpeAutoZwSetInformationResourceManager
1800 stdcall ZwSetInformationThread(long long ptr long) ChpeAutoZwSetInformationThread
1801 stdcall ZwSetInformationToken(long long ptr long) ChpeAutoZwSetInformationToken
1802 stdcall -version=0x600+ ZwSetInformationTransaction(ptr long ptr long) ChpeAutoZwSetInformationTransaction
1803 stdcall -version=0x600+ ZwSetInformationTransactionManager(ptr long ptr long) ChpeAutoZwSetInformationTransactionManager
1804 stdcall ZwSetInformationVirtualMemory(ptr long ptr ptr ptr long) ChpeAutoZwSetInformationVirtualMemory
1805 stdcall -version=0x600+ ZwSetInformationWorkerFactory(ptr long ptr long) ChpeStubZwSetInformationWorkerFactory
1806 stdcall ZwSetIntervalProfile(long long) ChpeAutoZwSetIntervalProfile
1807 stdcall ZwSetIoCompletion(ptr long ptr long long) ChpeAutoZwSetIoCompletion
1808 stdcall ZwSetLdtEntries(long int64 long int64) ChpeAutoZwSetLdtEntries
1809 stdcall ZwSetLowEventPair(ptr) ChpeAutoZwSetLowEventPair
1810 stdcall ZwSetLowWaitHighEventPair(ptr) ChpeAutoZwSetLowWaitHighEventPair
1811 stdcall ZwSetQuotaInformationFile(ptr ptr ptr long) ChpeAutoZwSetQuotaInformationFile
1812 stdcall ZwSetSecurityObject(long long ptr) ChpeAutoZwSetSecurityObject
1813 stdcall ZwSetSystemEnvironmentValue(ptr ptr) ChpeAutoZwSetSystemEnvironmentValue
1814 stdcall ZwSetSystemEnvironmentValueEx(ptr ptr ptr ptr ptr) ChpeAutoZwSetSystemEnvironmentValueEx
1815 stdcall ZwSetSystemInformation(long ptr long) ChpeAutoZwSetSystemInformation
1816 stdcall ZwSetSystemPowerState(long long long) ChpeAutoZwSetSystemPowerState
1817 stdcall ZwSetSystemTime(ptr ptr) ChpeAutoZwSetSystemTime
1818 stdcall ZwSetThreadExecutionState(long ptr) ChpeAutoZwSetThreadExecutionState
1819 stdcall ZwSetTimer(long ptr ptr ptr long long ptr) ChpeAutoZwSetTimer
1820 stdcall ZwSetTimerResolution(long long ptr) ChpeAutoZwSetTimerResolution
1821 stdcall ZwSetUuidSeed(ptr) ChpeAutoZwSetUuidSeed
1822 stdcall ZwSetValueKey(long long long long long long) ChpeAutoZwSetValueKey
1823 stdcall ZwSetVolumeInformationFile(long ptr ptr long long) ChpeAutoZwSetVolumeInformationFile
1824 stdcall ZwShutdownSystem(long) ChpeAutoZwShutdownSystem
1825 stdcall -version=0x600+ ZwShutdownWorkerFactory(ptr ptr) ChpeStubZwShutdownWorkerFactory
1826 stdcall ZwSignalAndWaitForSingleObject(long long long ptr) ChpeAutoZwSignalAndWaitForSingleObject
1827 stdcall -version=0x600+ ZwSinglePhaseReject(ptr ptr) ChpeAutoZwSinglePhaseReject
1828 stdcall ZwStartProfile(ptr) ChpeAutoZwStartProfile
1829 stdcall ZwStopProfile(ptr) ChpeAutoZwStopProfile
1830 stdcall -version=0x602+ ZwSubscribeWnfStateChange(ptr long long ptr) ChpeAutoZwSubscribeWnfStateChange
1831 stdcall ZwSuspendProcess(ptr) ChpeAutoZwSuspendProcess
1832 stdcall ZwSuspendThread(long ptr) ChpeAutoZwSuspendThread
1833 stdcall ZwSystemDebugControl(long ptr long ptr long ptr) ChpeAutoZwSystemDebugControl
1834 stdcall ZwTerminateJobObject(ptr long) ChpeAutoZwTerminateJobObject
1835 stdcall ZwTerminateProcess(ptr long) ChpeAutoZwTerminateProcess
1836 stdcall ZwTerminateThread(ptr long) ChpeAutoZwTerminateThread
1837 stdcall ZwTestAlert() ChpeAutoZwTestAlert
1838 stdcall -version=0x600+ ZwThawRegistry() ChpeStubZwThawRegistry
1839 stdcall -version=0x600+ ZwThawTransactions() ChpeStubZwThawTransactions
1840 stdcall -version=0x600+ ZwTraceControl(long ptr long ptr long ptr) ChpeAutoZwTraceControl
1841 stdcall ZwTraceEvent(ptr long long ptr) ChpeAutoZwTraceEvent
1842 stdcall ZwTranslateFilePath(ptr long ptr long) ChpeAutoZwTranslateFilePath
1843 stdcall ZwUnloadDriver(ptr) ChpeAutoZwUnloadDriver
1844 stdcall ZwUnloadKey2(ptr long) ChpeAutoZwUnloadKey2
1845 stdcall ZwUnloadKey(long) ChpeAutoZwUnloadKey
1846 stdcall ZwUnloadKeyEx(ptr ptr) ChpeAutoZwUnloadKeyEx
1847 stdcall ZwUnlockFile(long ptr ptr ptr ptr) ChpeAutoZwUnlockFile
1848 stdcall ZwUnlockVirtualMemory(long ptr ptr long) ChpeAutoZwUnlockVirtualMemory
1849 stdcall ZwUnmapViewOfSection(long ptr) ChpeAutoZwUnmapViewOfSection
1850 stdcall -version=0x602+ ZwUnsubscribeWnfStateChange(ptr) ChpeAutoZwUnsubscribeWnfStateChange
1851 stdcall -version=0x602+ ZwUpdateWnfStateData(ptr ptr long ptr ptr long long) ChpeAutoZwUpdateWnfStateData
1852 stdcall ZwVdmControl(long ptr) ChpeAutoZwVdmControl
1853 stdcall ZwWaitForDebugEvent(ptr long ptr ptr) ChpeAutoZwWaitForDebugEvent
1854 stdcall ZwWaitForKeyedEvent(ptr ptr long ptr) ChpeAutoZwWaitForKeyedEvent
1855 stdcall ZwWaitForAlertByThreadId(ptr ptr) ChpeAutoZwWaitForAlertByThreadId
1856 stdcall ZwWaitForMultipleObjects32(long ptr long long ptr) ChpeAutoZwWaitForMultipleObjects32
1857 stdcall ZwWaitForMultipleObjects(long ptr long long ptr) ChpeAutoZwWaitForMultipleObjects
1858 stdcall ZwWaitForSingleObject(long long long) ChpeAutoZwWaitForSingleObject
1859 stdcall -version=0x600+ ZwWaitForWorkViaWorkerFactory(ptr ptr long ptr ptr) ChpeStubZwWaitForWorkViaWorkerFactory
1860 stdcall ZwWaitHighEventPair(ptr) ChpeAutoZwWaitHighEventPair
1861 stdcall ZwWaitLowEventPair(ptr) ChpeAutoZwWaitLowEventPair
1862 stdcall -version=0x600+ ZwWorkerFactoryWorkerReady(ptr) ChpeStubZwWorkerFactoryWorkerReady
1863 stdcall ZwWriteFile(long long ptr ptr ptr ptr long ptr ptr) ChpeAutoZwWriteFile
1864 stdcall ZwWriteFileGather(long long ptr ptr ptr ptr long ptr ptr) ChpeAutoZwWriteFileGather
1865 stdcall ZwWriteRequestData(ptr ptr long ptr long ptr) ChpeAutoZwWriteRequestData
1866 stdcall ZwWriteVirtualMemory(long ptr ptr long ptr) ChpeAutoZwWriteVirtualMemory
1867 stdcall ZwYieldExecution() ChpeAutoZwYieldExecution
1870 cdecl __isascii(long) ChpeAuto__isascii
1871 cdecl __iscsym(long) ChpeAuto__iscsym
1872 cdecl __iscsymf(long) ChpeAuto__iscsymf
1873 cdecl -version=0x600+ __misaligned_access() ChpeStub__misaligned_access
1874 cdecl __toascii(long) ChpeAuto__toascii
1875 cdecl -ret64 _atoi64(str) ChpeAuto_atoi64
1877 extern _fltused ntdll._fltused
1878 cdecl _i64toa(double ptr long) ChpeAuto_i64toa
1879 cdecl _i64tow(double ptr long) ChpeAuto_i64tow
1880 cdecl _itoa(long ptr long) ChpeAuto_itoa
1881 cdecl _itow(long ptr long) ChpeAuto_itow
1884 cdecl _ltoa(long ptr long) ChpeAuto_ltoa
1885 cdecl _ltow(long ptr long) ChpeAuto_ltow
1886 cdecl _memccpy(ptr ptr long long) ChpeAuto_memccpy
1887 cdecl _memicmp(str str long) ChpeAuto_memicmp
1894 cdecl _splitpath(str ptr ptr ptr ptr) ChpeAuto_splitpath
1895 cdecl _strcmpi(str str) ChpeAuto_strcmpi
1896 cdecl _stricmp(str str) ChpeAuto_stricmp
1897 cdecl _strlwr(str) ChpeAuto_strlwr
1898 cdecl _strnicmp(str str long) ChpeAuto_strnicmp
1899 cdecl _strupr(str) ChpeAuto_strupr
1901 cdecl _ui64toa(double ptr long) ChpeAuto_ui64toa
1902 cdecl _ui64tow(double ptr long) ChpeAuto_ui64tow
1903 cdecl _ultoa(long ptr long) ChpeAuto_ultoa
1904 cdecl _ultow(long ptr long) ChpeAuto_ultow
1905 cdecl _vscwprintf(wstr ptr) ChpeAuto_vscwprintf
1906 cdecl _vsnprintf(ptr long str ptr) ChpeAuto_vsnprintf
1907 cdecl _vsnprintf_s(ptr long long str ptr) ChpeAuto_vsnprintf_s
1908 cdecl _vsnwprintf(ptr long wstr ptr) ChpeAuto_vsnwprintf
1909 cdecl _vsnwprintf_s(ptr long long wstr ptr) ChpeAuto_vsnwprintf_s
1910 cdecl -version=0x600+ _vswprintf(ptr wstr ptr) ChpeStub_vswprintf
1911 cdecl _wcsicmp(wstr wstr) ChpeAuto_wcsicmp
1912 cdecl _wcslwr(wstr) ChpeAuto_wcslwr
1913 cdecl _wcsnicmp(wstr wstr long) ChpeAuto_wcsnicmp
1914 cdecl _wcstoui64(wstr ptr long) ChpeAuto_wcstoui64
1915 cdecl _wcsupr(wstr) ChpeAuto_wcsupr
1916 cdecl _wtoi(wstr) ChpeAuto_wtoi
1917 cdecl _wtoi64(wstr) ChpeAuto_wtoi64
1918 cdecl _wtol(wstr) ChpeAuto_wtol
1919 cdecl abs(long) ChpeAutoabs
1920 cdecl atan(double) ChpeAutoatan
1921 cdecl atan2(double double) ChpeAutoatan2
1922 cdecl atoi(str) ChpeAutoatoi
1923 cdecl atol(str) ChpeAutoatol
1925 cdecl ceil(double) ChpeAutoceil
1926 cdecl cos(double) ChpeAutocos
1927 cdecl fabs(double) ChpeAutofabs
1928 cdecl floor(double) ChpeAutofloor
1929 cdecl isalnum(long) ChpeAutoisalnum
1930 cdecl isalpha(long) ChpeAutoisalpha
1931 cdecl iscntrl(long) ChpeAutoiscntrl
1932 cdecl isdigit(long) ChpeAutoisdigit
1933 cdecl isgraph(long) ChpeAutoisgraph
1934 cdecl islower(long) ChpeAutoislower
1935 cdecl isprint(long) ChpeAutoisprint
1936 cdecl ispunct(long) ChpeAutoispunct
1937 cdecl isspace(long) ChpeAutoisspace
1938 cdecl isupper(long) ChpeAutoisupper
1939 cdecl iswalpha(long) ChpeAutoiswalpha
1940 cdecl iswctype(long long) ChpeAutoiswctype
1941 cdecl iswdigit(long) ChpeAutoiswdigit
1942 cdecl iswlower(long) ChpeAutoiswlower
1943 cdecl iswspace(long) ChpeAutoiswspace
1944 cdecl iswxdigit(long) ChpeAutoiswxdigit
1945 cdecl isxdigit(long) ChpeAutoisxdigit
1946 cdecl labs(long) ChpeAutolabs
1947 cdecl log(double) ChpeAutolog
1949 cdecl mbstowcs(ptr str long) ChpeAutombstowcs
1950 cdecl memchr(ptr long long) ChpeAutomemchr
1951 cdecl memcmp(ptr ptr long) ChpeAutomemcmp
1953 cdecl memmove(ptr ptr long) ChpeAutomemmove
1954 cdecl memset(ptr long long) ChpeAutomemset
1955 cdecl pow(double double) ChpeAutopow
1957 cdecl sin(double) ChpeAutosin
1959 cdecl sqrt(double) ChpeAutosqrt
1961 cdecl strcat(str str) ChpeAutostrcat
1962 cdecl strchr(str long) ChpeAutostrchr
1963 cdecl strcmp(str str) ChpeAutostrcmp
1964 cdecl strcpy(ptr str) ChpeAutostrcpy
1965 cdecl -version=0x600+ strcpy_s(ptr long str) ChpeAutostrcpy_s
1966 cdecl -version=0x600+ strcat_s(ptr long str) ChpeAutostrcat_s
1967 cdecl -version=0x600+ strncpy_s(ptr long str long) ChpeAutostrncpy_s
1968 cdecl strcspn(str str) ChpeAutostrcspn
1969 cdecl strlen(str) ChpeAutostrlen
1970 cdecl strncat(str str long) ChpeAutostrncat
1971 cdecl strncmp(str str long) ChpeAutostrncmp
1972 cdecl strncpy(ptr str long) ChpeAutostrncpy
1973 cdecl strpbrk(str str) ChpeAutostrpbrk
1974 cdecl strrchr(str long) ChpeAutostrrchr
1975 cdecl strspn(str str) ChpeAutostrspn
1976 cdecl strstr(str str) ChpeAutostrstr
1977 cdecl strtol(str ptr long) ChpeAutostrtol
1978 cdecl strtoul(str ptr long) ChpeAutostrtoul
1980 cdecl tan(double) ChpeAutotan
1981 cdecl tolower(long) ChpeAutotolower
1982 cdecl toupper(long) ChpeAutotoupper
1983 cdecl towlower(long) ChpeAutotowlower
1984 cdecl towupper(long) ChpeAutotowupper
1987 cdecl vsprintf(ptr str ptr) ChpeAutovsprintf
1988 cdecl wcscat(wstr wstr) ChpeAutowcscat
1989 cdecl wcschr(wstr long) ChpeAutowcschr
1990 cdecl wcscmp(wstr wstr) ChpeAutowcscmp
1991 cdecl wcscpy(ptr wstr) ChpeAutowcscpy
1992 cdecl wcscspn(wstr wstr) ChpeAutowcscspn
1993 cdecl wcslen(wstr) ChpeAutowcslen
1994 cdecl wcsncat(wstr wstr long) ChpeAutowcsncat
1995 cdecl wcsncmp(wstr wstr long) ChpeAutowcsncmp
1996 cdecl wcsncpy(ptr wstr long) ChpeAutowcsncpy
1997 cdecl wcsnlen(wstr long) ChpeAutowcsnlen
1998 cdecl wcspbrk(wstr wstr) ChpeAutowcspbrk
1999 cdecl wcsrchr(wstr long) ChpeAutowcsrchr
2000 cdecl wcsspn(wstr wstr) ChpeAutowcsspn
2001 cdecl wcsstr(wstr wstr) ChpeAutowcsstr
2002 cdecl wcstol(wstr ptr long) ChpeAutowcstol
2003 cdecl wcstombs(ptr ptr long) ChpeAutowcstombs
2004 cdecl wcstoul(wstr ptr long) ChpeAutowcstoul
2007 stdcall -version=0x602+ -ret64 RtlGetSystemTimePrecise() ChpeAutoRtlGetSystemTimePrecise
# END typed bridge wrappers
