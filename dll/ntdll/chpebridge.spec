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
1888 cdecl __C_specific_handler(ptr long ptr ptr) ChpeCSpecificHandler
2034 stdcall ChpeDispatchExceptionNative(ptr ptr)
108 stdcall KiUserExceptionDispatcher(ptr ptr) ChpeKiUserExceptionDispatcher
111 stdcall LdrAccessResource(ptr ptr ptr ptr) ChpeLdrAccessResource
114 stdcall LdrAddRefDll(long ptr) ChpeLdrAddRefDll
116 stdcall LdrEnumResources(ptr ptr long ptr ptr) ChpeLdrEnumResources
118 stdcall LdrFindEntryForAddress(ptr ptr) ChpeLdrFindEntryForAddress
119 stdcall LdrFindResourceDirectory_U(ptr ptr long ptr) ChpeLdrFindResourceDirectory_U
121 stdcall LdrFindResource_U(ptr ptr long ptr) ChpeLdrFindResource_U
124 stdcall LdrGetDllHandle(wstr ptr ptr ptr) ChpeLdrGetDllHandle
125 stdcall LdrGetDllHandleEx(long wstr ptr ptr ptr) ChpeLdrGetDllHandleEx
126 stdcall LdrGetDllHandleByName(ptr ptr ptr) ChpeLdrGetDllHandleByName
131 stdcall LdrGetProcedureAddress(ptr ptr long ptr) ChpeLdrGetProcedureAddress
138 stdcall LdrLoadDll(wstr long ptr ptr) ChpeLdrLoadDll
147 stdcall LdrQueryProcessModuleInformation(ptr long ptr) ChpeLdrQueryProcessModuleInformation
151 stdcall -version=0x602+ LdrResolveDelayLoadedAPI(ptr ptr ptr ptr ptr long) ChpeLdrResolveDelayLoadedAPI
152 stdcall -version=0x602+ LdrResolveDelayLoadsFromDll(ptr str long) ChpeLdrResolveDelayLoadsFromDll
167 stdcall LdrUnloadDll(ptr) ChpeLdrUnloadDll
197 stdcall NtAdjustPrivilegesToken(long long ptr long ptr ptr) ChpeAutoNtAdjustPrivilegesToken
206 stdcall NtAllocateVirtualMemory(long ptr ptr ptr long long) ChpeAutoNtAllocateVirtualMemory
207 stdcall NtAllocateVirtualMemoryEx(long ptr ptr long long ptr long) ChpeAutoNtAllocateVirtualMemoryEx
201 stdcall NtAlertThreadByThreadId(long) ChpeAutoNtAlertThreadByThreadId
241 stdcall NtClose(long) ChpeAutoNtClose
252 stdcall NtContinue(ptr long) ChpeNtContinue
253 stdcall -version=0xA00+ NtContinueEx(ptr ptr) ChpeNtContinueEx
260 stdcall NtCreateFile(ptr long ptr ptr ptr long long long long ptr long) ChpeAutoNtCreateFile
272 stdcall NtCreateNamedPipeFile(ptr long ptr ptr long long long long long long long long long ptr) ChpeAutoNtCreateNamedPipeFile
308 stdcall NtDeviceIoControlFile(long long ptr ptr ptr long ptr long ptr long) ChpeAutoNtDeviceIoControlFile
327 stdcall -version=0x600+ NtFlushProcessWriteBuffers() ChpeAutoNtFlushProcessWriteBuffers
325 stdcall NtFlushInstructionCache(long ptr long) ChpeAutoNtFlushInstructionCache
331 stdcall NtFreeVirtualMemory(long ptr ptr long) ChpeAutoNtFreeVirtualMemory
335 stdcall NtGetContextThread(long ptr) ChpeNtGetContextThread
341 stdcall -version=0x600+ NtGetNextThread(ptr ptr long long long ptr) ChpeAutoNtGetNextThread
370 stdcall NtMapViewOfSection(long long ptr long ptr ptr ptr long long long) ChpeAutoNtMapViewOfSection
371 stdcall NtMapViewOfSectionEx(long long ptr ptr ptr long long ptr long) ChpeNtMapViewOfSectionEx
376 stdcall NtNotifyChangeKey(ptr ptr ptr ptr ptr long long ptr long long) ChpeAutoNtNotifyChangeKey
377 stdcall NtNotifyChangeMultipleKeys(ptr long ptr ptr ptr ptr ptr long long ptr long long) ChpeAutoNtNotifyChangeMultipleKeys
382 stdcall NtOpenFile(ptr long ptr ptr long long) ChpeAutoNtOpenFile
417 stdcall NtProtectVirtualMemory(long ptr ptr long ptr) ChpeAutoNtProtectVirtualMemory
425 stdcall NtQueryDirectoryFile(long long ptr ptr ptr ptr long long long ptr long) ChpeAutoNtQueryDirectoryFile
435 stdcall NtQueryInformationFile(long ptr ptr long long) ChpeAutoNtQueryInformationFile
448 stdcall NtQueryKey(long long ptr long ptr) ChpeAutoNtQueryKey
267 stdcall NtCreateKey(ptr long ptr long ptr long ptr) ChpeAutoNtCreateKey
440 stdcall NtQueryInformationThread(long long ptr long ptr) ChpeAutoNtQueryInformationThread
438 stdcall NtQueryInformationProcess(ptr long ptr long ptr) ChpeAutoNtQueryInformationProcess
900 stdcall RtlFormatCurrentUserKeyPath(ptr) ChpeRtlFormatCurrentUserKeyPath
911 stdcall RtlFreeUnicodeString(ptr) ChpeRtlFreeUnicodeString
385 stdcall NtOpenKey(ptr long ptr) ChpeAutoNtOpenKey
848 stdcall NtOpenKeyEx(ptr long ptr long) ChpeAutoNtOpenKeyEx
469 stdcall NtQueryValueKey(long ptr long ptr long ptr) ChpeAutoNtQueryValueKey
566 stdcall NtSetValueKey(long ptr long long ptr long) ChpeAutoNtSetValueKey
302 stdcall NtDeleteKey(long) ChpeAutoNtDeleteKey
305 stdcall NtDeleteValueKey(long ptr) ChpeAutoNtDeleteValueKey
314 stdcall NtEnumerateKey(long long long ptr long ptr) ChpeAutoNtEnumerateKey
317 stdcall NtEnumerateValueKey(long long long ptr long ptr) ChpeAutoNtEnumerateValueKey
326 stdcall NtFlushKey(long) ChpeAutoNtFlushKey
262 stdcall NtCreateWaitCompletionPacket(ptr long ptr) ChpeAutoNtCreateWaitCompletionPacket
263 stdcall NtAssociateWaitCompletionPacket(long long long ptr ptr long long ptr) ChpeAutoNtAssociateWaitCompletionPacket
264 stdcall NtCancelWaitCompletionPacket(long long) ChpeAutoNtCancelWaitCompletionPacket
471 stdcall NtQueryVolumeInformationFile(long ptr ptr long long) ChpeAutoNtQueryVolumeInformationFile
452 stdcall NtQueryObject(long long long long long) ChpeAutoNtQueryObject
464 stdcall NtQuerySystemInformation(long ptr long ptr) ChpeAutoNtQuerySystemInformation
948 stdcall RtlGetNativeSystemInformation(long ptr long ptr) ChpeRtlGetNativeSystemInformation
470 stdcall NtQueryVirtualMemory(long ptr long ptr long ptr) ChpeAutoNtQueryVirtualMemory
477 stdcall NtRaiseException(ptr ptr long) ChpeNtRaiseException
479 stdcall NtReadFile(long long ptr ptr ptr ptr long ptr ptr) ChpeAutoNtReadFile
483 stdcall NtReadVirtualMemory(long ptr ptr long ptr) ChpeAutoNtReadVirtualMemory
524 stdcall NtSetContextThread(long ptr) ChpeNtSetContextThread
575 stdcall NtSuspendProcess(ptr) ChpeAutoNtSuspendProcess
576 stdcall NtSuspendThread(ptr ptr) ChpeAutoNtSuspendThread
513 stdcall NtResumeThread(ptr ptr) ChpeAutoNtResumeThread
579 stdcall NtTerminateProcess(long long) ChpeAutoNtTerminateProcess
580 stdcall NtTerminateThread(long long) ChpeAutoNtTerminateThread
593 stdcall NtUnmapViewOfSection(long ptr) ChpeAutoNtUnmapViewOfSection
594 stdcall NtUnmapViewOfSectionEx(long ptr long) ChpeNtUnmapViewOfSectionEx
600 stdcall NtWaitForAlertByThreadId(ptr ptr) ChpeAutoNtWaitForAlertByThreadId
608 stdcall NtWriteFile(long long ptr ptr ptr ptr long ptr ptr) ChpeAutoNtWriteFile
611 stdcall NtWriteVirtualMemory(long ptr ptr long ptr) ChpeAutoNtWriteVirtualMemory
646 stdcall RtlAddFunctionTable(ptr long long) ChpeRtlAddFunctionTable
647 stdcall RtlAddGrowableFunctionTable(ptr ptr long long long long) ChpeRtlAddGrowableFunctionTable
653 stdcall RtlAddVectoredContinueHandler(long ptr) ChpeRtlAddVectoredContinueHandler
654 stdcall RtlAddVectoredExceptionHandler(long ptr) ChpeRtlAddVectoredExceptionHandler
660 stdcall RtlAllocateHeap(ptr long ptr) ChpeRtlAllocateHeap
922 stdcall RtlGetCurrentPeb() ChpeRtlGetCurrentPeb
955 stdcall RtlGetProcessHeaps(long ptr) ChpeRtlGetProcessHeaps
1141 stdcall RtlQueryEnvironmentVariable(ptr ptr long ptr long ptr) ChpeRtlQueryEnvironmentVariable
624 stdcall RtlAcquirePrivilege(ptr long long ptr) ChpeRtlAcquirePrivilege
627 stdcall RtlAcquireSRWLockExclusive(ptr) ChpeRtlAcquireSRWLockExclusive
628 stdcall RtlAcquireSRWLockShared(ptr) ChpeRtlAcquireSRWLockShared
682 stdcall RtlCaptureContext(ptr) ChpeRtlCaptureContextX64
683 stdcall RtlCaptureStackBackTrace(long long ptr ptr) ChpeRtlCaptureStackBackTrace
699 stdcall RtlCompareMemory(ptr ptr long) ChpeRtlCompareMemory
702 stdcall RtlCompareUnicodeString(ptr ptr long) ChpeRtlCompareUnicodeString
731 stdcall RtlCopyUnicodeString(ptr ptr) ChpeRtlCopyUnicodeString
775 stdcall RtlDecodePointer(ptr) ChpeRtlDecodePointer
776 stdcall RtlDecodeSystemPointer(ptr) ChpeRtlDecodeSystemPointer
786 stdcall RtlDeleteCriticalSection(ptr) ChpeRtlDeleteCriticalSection
789 cdecl RtlDeleteFunctionTable(ptr) ChpeRtlDeleteFunctionTable
790 stdcall RtlDeleteGrowableFunctionTable(ptr) ChpeRtlDeleteGrowableFunctionTable
830 stdcall RtlEnterCriticalSection(ptr) ChpeRtlEnterCriticalSection
828 stdcall RtlEncodePointer(ptr) ChpeRtlEncodePointer
829 stdcall RtlEncodeSystemPointer(ptr) ChpeRtlEncodeSystemPointer
868 stdcall RtlExitUserThread(long) ChpeRtlExitUserThread
873 stdcall RtlFillMemory(ptr long long) ChpeRtlFillMemory
895 stdcall RtlFlsAlloc(ptr ptr) ChpeRtlFlsAlloc
896 stdcall RtlFlsFree(long) ChpeRtlFlsFree
897 stdcall RtlFlsGetValue(long ptr) ChpeRtlFlsGetValue
898 stdcall RtlFlsSetValue(long ptr) ChpeRtlFlsSetValue
906 stdcall RtlFreeHeap(long long long) ChpeRtlFreeHeap
939 stdcall RtlGetFunctionTableListHead() ChpeRtlGetFunctionTableListHead
942 stdcall RtlGetLastNtStatus() ChpeRtlGetLastNtStatus
943 stdcall RtlGetLastWin32Error() ChpeRtlGetLastWin32Error
923 stdcall RtlGetCurrentProcessorNumber() ChpeRtlGetCurrentProcessorNumber
924 stdcall -version=0x601+ RtlGetCurrentProcessorNumberEx(ptr) ChpeRtlGetCurrentProcessorNumberEx
956 stdcall -version=0x600+ RtlGetProductInfo(long long long long ptr) ChpeRtlGetProductInfo
970 stdcall RtlGetVersion(ptr) ChpeRtlGetVersion
1022 stdcall RtlGrowFunctionTable(ptr long) ChpeRtlGrowFunctionTable
995 stdcall RtlInitUnicodeString(ptr wstr) ChpeRtlInitUnicodeString
1000 stdcall -version=0x600+ RtlInitializeConditionVariable(ptr) ChpeRtlInitializeConditionVariable
1002 stdcall RtlInitializeCriticalSection(ptr) ChpeRtlInitializeCriticalSection
1013 stdcall RtlInitializeSListHead(ptr) ChpeRtlInitializeSListHead
1014 stdcall -version=0x600+ RtlInitializeSRWLock(ptr) ChpeRtlInitializeSRWLock
1021 cdecl RtlInstallFunctionTableCallback(double double long ptr ptr ptr) ChpeRtlInstallFunctionTableCallback
1026 stdcall RtlInterlockedFlushSList(ptr) ChpeRtlInterlockedFlushSList
1027 stdcall RtlInterlockedPopEntrySList(ptr) ChpeRtlInterlockedPopEntrySList
1028 stdcall RtlInterlockedPushEntrySList(ptr ptr) ChpeRtlInterlockedPushEntrySList
1029 stdcall RtlInterlockedPushListSList(ptr ptr ptr long) ChpeRtlInterlockedPushListSList
1030 stdcall -version=0x602+ RtlInterlockedPushListSListEx(ptr ptr ptr long) ChpeRtlInterlockedPushListSListEx
1062 stdcall RtlIsProcessorFeaturePresent(long) ChpeRtlIsProcessorFeaturePresent
1072 stdcall RtlLeaveCriticalSection(ptr) ChpeRtlLeaveCriticalSection
1094 stdcall RtlLookupFunctionEntry(long ptr ptr) ChpeRtlLookupFunctionEntry
1095 stdcall RtlLookupFunctionTable(int64 ptr ptr) ChpeRtlLookupFunctionTable
1085 stdcall -version=0xA00+ RtlLogUnexpectedCodepath(ptr) ChpeRtlLogUnexpectedCodepath
1099 stdcall RtlMoveMemory(ptr ptr long) ChpeRtlMoveMemory
1113 stdcall RtlNtStatusToDosError(long) ChpeRtlNtStatusToDosError
1126 stdcall RtlPcToFileHeader(ptr ptr) ChpeRtlPcToFileHeader
1138 stdcall RtlQueryDepthSList(ptr) ChpeRtlQueryDepthSList
1160 stdcall -norelay RtlRaiseException(ptr) ChpeRtlRaiseException
1161 stdcall RtlRaiseStatus(long) ChpeRtlRaiseStatus
1162 stdcall RtlRandom(ptr) ChpeRtlRandom
1166 stdcall RtlReAllocateHeap(long long ptr long) ChpeRtlReAllocateHeap
1179 stdcall RtlReleasePrivilege(ptr) ChpeRtlReleasePrivilege
1182 stdcall RtlReleaseSRWLockExclusive(ptr) ChpeRtlReleaseSRWLockExclusive
1183 stdcall RtlReleaseSRWLockShared(ptr) ChpeRtlReleaseSRWLockShared
1186 stdcall RtlRemoveVectoredContinueHandler(ptr) ChpeRtlRemoveVectoredContinueHandler
1187 stdcall RtlRemoveVectoredExceptionHandler(ptr) ChpeRtlRemoveVectoredExceptionHandler
1193 stdcall RtlRestoreContext(ptr ptr) ChpeRtlRestoreContext
1194 stdcall RtlRestoreLastWin32Error(long) ChpeRtlRestoreLastWin32Error
1201 stdcall -version=0x600+ RtlRunOnceExecuteOnce(ptr ptr ptr ptr) ChpeRtlRunOnceExecuteOnce
1202 stdcall -version=0x600+ RtlRunOnceInitialize(ptr) ChpeRtlRunOnceInitialize
1254 stdcall RtlSizeHeap(long long ptr) ChpeRtlSizeHeap
1214 stdcall RtlSetCriticalSectionSpinCount(ptr long) ChpeRtlSetCriticalSectionSpinCount
1228 stdcall RtlSetLastWin32Error(long) ChpeRtlSetLastWin32Error
1281 stdcall RtlTryAcquireSRWLockExclusive(ptr) ChpeRtlTryAcquireSRWLockExclusive
1282 stdcall RtlTryAcquireSRWLockShared(ptr) ChpeRtlTryAcquireSRWLockShared
1283 stdcall RtlTryEnterCriticalSection(ptr) ChpeRtlTryEnterCriticalSection
1289 stdcall -version=0x601+ RtlUTF8ToUnicodeN(ptr long ptr str long) ChpeRtlUTF8ToUnicodeN
1307 stdcall RtlUnwind(ptr ptr ptr ptr) ChpeRtlUnwind
1308 stdcall RtlUnwindEx(ptr ptr ptr ptr ptr ptr) ChpeRtlUnwindEx
1332 stdcall RtlVirtualUnwind(long int64 int64 ptr ptr ptr ptr ptr) ChpeRtlVirtualUnwind
1334 stdcall -version=0x602+ RtlWaitOnAddress(ptr ptr long ptr) ChpeRtlWaitOnAddress
1336 stdcall -version=0x602+ RtlWakeAddressAll(ptr) ChpeRtlWakeAddressAll
1337 stdcall -version=0x602+ RtlWakeAddressSingle(ptr) ChpeRtlWakeAddressSingle
1335 stdcall RtlWakeAllConditionVariable(ptr) ChpeRtlWakeAllConditionVariable
1338 stdcall RtlWakeConditionVariable(ptr) ChpeRtlWakeConditionVariable
1363 stdcall RtlZeroMemory(ptr long) ChpeRtlZeroMemory
1417 stdcall -version=0x600+ TpCallbackLeaveCriticalSectionOnCompletion(ptr ptr) ChpeTpCallbackLeaveCriticalSectionOnCompletion
1419 stdcall -version=0x600+ TpCallbackReleaseMutexOnCompletion(ptr ptr) ChpeTpCallbackReleaseMutexOnCompletion
1420 stdcall -version=0x600+ TpCallbackReleaseSemaphoreOnCompletion(ptr ptr long) ChpeTpCallbackReleaseSemaphoreOnCompletion
1423 stdcall -version=0x600+ TpCallbackSetEventOnCompletion(ptr ptr) ChpeTpCallbackSetEventOnCompletion
1424 stdcall -version=0x600+ TpCallbackUnloadDllOnCompletion(ptr ptr) ChpeTpCallbackUnloadDllOnCompletion
1425 stdcall -version=0x600+ TpCancelAsyncIoOperation(ptr) ChpeTpCancelAsyncIoOperation
1430 stdcall -version=0x600+ TpDisassociateCallback(ptr) ChpeTpDisassociateCallback
1431 stdcall -version=0x600+ TpIsTimerSet(ptr) ChpeTpIsTimerSet
1432 stdcall -version=0x600+ TpPostWork(ptr) ChpeTpPostWork
1435 stdcall -version=0x600+ TpReleaseCleanupGroup(ptr) ChpeTpReleaseCleanupGroup
1436 stdcall -version=0x600+ TpReleaseCleanupGroupMembers(ptr long ptr) ChpeTpReleaseCleanupGroupMembers
1437 stdcall -version=0x600+ TpReleaseIoCompletion(ptr) ChpeTpReleaseIoCompletion
1438 stdcall -version=0x600+ TpReleasePool(ptr) ChpeTpReleasePool
1439 stdcall TpReleaseTimer(ptr) ChpeTpReleaseTimer
1440 stdcall TpReleaseWait(ptr) ChpeTpReleaseWait
1441 stdcall -version=0x600+ TpReleaseWork(ptr) ChpeTpReleaseWork
1442 stdcall -version=0x600+ TpSetPoolMaxThreads(ptr long) ChpeTpSetPoolMaxThreads
1443 stdcall -version=0x600+ TpSetPoolMinThreads(ptr long) ChpeTpSetPoolMinThreads
1445 stdcall TpSetTimer(ptr ptr long long) ChpeTpSetTimer
1446 stdcall -version=0x602+ TpSetTimerEx(ptr ptr long long) ChpeTpSetTimerEx
1447 stdcall TpSetWait(ptr long ptr) ChpeTpSetWait
1448 stdcall -version=0x602+ TpSetWaitEx(ptr long ptr ptr) ChpeTpSetWaitEx
1450 stdcall -version=0x600+ TpStartAsyncIoOperation(ptr) ChpeTpStartAsyncIoOperation
1452 stdcall -version=0x600+ TpWaitForIoCompletion(ptr long) ChpeTpWaitForIoCompletion
1453 stdcall TpWaitForTimer(ptr long) ChpeTpWaitForTimer
1454 stdcall -version=0x600+ TpWaitForWait(ptr long) ChpeTpWaitForWait
1455 stdcall -version=0x600+ TpWaitForWork(ptr long) ChpeTpWaitForWork
1456 stdcall -ret64 VerSetConditionMask(double long long) ChpeVerSetConditionMask
1910 varargs _snprintf(ptr long str) ChpeSnprintf
1912 varargs _snwprintf(ptr long wstr) ChpeSnwprintf
1921 varargs _swprintf(ptr wstr) ChpeSwprintf
1979 varargs sprintf(ptr str) ChpeSprintf
2001 varargs swprintf(ptr wstr) ChpeSwprintf
1911 varargs _snprintf_s(ptr long long str) ChpeSnprintf_s
1913 varargs _snwprintf_s(ptr long long wstr) ChpeSnwprintf_s
1889 cdecl __chkstk() ChpeChkStk
1903 cdecl _local_unwind(ptr ptr) ChpeLocalUnwind
1973 cdecl memcpy(ptr ptr long) ChpeMemcpy
2035 stdcall ChpeEmulationDispatch(ptr)
1352 stdcall RtlWow64GetThreadSelectorEntry(ptr ptr long ptr) ChpeRtlWow64GetThreadSelectorEntry

284 stdcall NtCreateThread(ptr long ptr long ptr ptr ptr long) ChpeNtCreateThread
334 stdcall NtFsControlFile(long long ptr ptr ptr long ptr long ptr long) ChpeAutoNtFsControlFile
563 stdcall NtSetTimer(long ptr ptr ptr long long ptr) ChpeAutoNtSetTimer
766 stdcall RtlCreateUserThread(long ptr long long ptr ptr ptr ptr ptr) ChpeRtlCreateUserThread
778 stdcall RtlDecompressFragment(long ptr long ptr long long ptr ptr) ChpeRtlDecompressFragment
914 stdcall RtlGenerate8dot3Name(ptr long ptr ptr) ChpeRtlGenerate8dot3Name
945 stdcall RtlGetLengthWithoutLastFullDosOrNtPathElement(long ptr ptr) ChpeRtlGetLengthWithoutLastFullDosOrNtPathElement
966 stdcall RtlGetUnloadEventTrace() ChpeRtlGetUnloadEventTrace
1159 stdcall RtlQueueWorkItem(ptr ptr long) ChpeRtlQueueWorkItem
1174 stdcall RtlRegisterWait(ptr ptr ptr ptr long long) ChpeRtlRegisterWait
1365 stdcall RtlpApplyLengthFunction(long long ptr ptr) ChpeRtlpApplyLengthFunction
2008 stdcall vDbgPrintEx(long long str ptr) ChpevDbgPrintEx
2009 stdcall vDbgPrintExWithPrefix(str long long str ptr) ChpevDbgPrintExWithPrefix
847 stdcall LdrGetDllFullName(ptr ptr) ChpeLdrGetDllFullName
884 stdcall RtlFindExportedRoutineByName(ptr str) ChpeRtlFindExportedRoutineByName
850 stdcall RtlIsEcCode(ptr) ChpeRtlIsEcCode
928 stdcall -version=0x600+ RtlGetEnabledExtendedFeatures(int64) ChpeRtlGetEnabledExtendedFeatures
859 stdcall RtlLocateExtendedFeature(ptr long ptr) ChpeRtlLocateExtendedFeature
860 stdcall RtlLocateExtendedFeature2(ptr long ptr ptr) ChpeRtlLocateExtendedFeature2
862 stdcall RtlQueryPerformanceCounter(ptr) ChpeRtlQueryPerformanceCounter
863 stdcall RtlQueryPerformanceFrequency(ptr) ChpeRtlQueryPerformanceFrequency
865 stdcall RtlQuerySystemTime(ptr) ChpeRtlQuerySystemTime
866 stdcall RtlSystemTimeToTimeFields(ptr ptr) ChpeRtlSystemTimeToTimeFields
43 varargs DbgPrintReturnControlC(str) ChpeDbgPrintReturnControlC
57 stdcall DbgUserBreakPoint() ChpeDbgUserBreakPoint
90 varargs EtwTraceMessage(int64 long ptr long) ChpeEtwTraceMessage
1323 stdcall RtlUserThreadStart(long long) ntdll.RtlUserThreadStart
1981 varargs sscanf(str str) ChpeSscanf
2028 cdecl ChpeVsscanf(str str ptr) ChpeAutoVsscanf
1982 varargs sscanf_s(str str) ChpeSscanf_s
2002 varargs swscanf_s(wstr wstr) ChpeSwscanf_s
1914 varargs _snwscanf_s(wstr long wstr) ChpeSnwscanf_s
2029 cdecl ChpeVsscanf_s(str str ptr) ChpeAutoVsscanf_s
2030 cdecl ChpeVswscanf_s(wstr wstr ptr) ChpeAutoVswscanf_s
2031 cdecl ChpeVsnwscanf_s(wstr long wstr ptr) ChpeAutoVsnwscanf_s
2032 stdcall ChpeVDbgPrintReturnControlC(str ptr) ChpeAutoVDbgPrintReturnControlC
1896 cdecl -private _errno() ChpeErrno
1902 cdecl _lfind(ptr ptr ptr long ptr) ChpeLfind
1945 cdecl bsearch(ptr ptr long long ptr) ChpeBsearch
1977 cdecl qsort(ptr long long ptr) ChpeQsort
1908 cdecl _setjmp(ptr ptr) ChpeSetJmpX64
1909 cdecl _setjmpex(ptr ptr) ChpeSetJmpX64
1969 cdecl longjmp(ptr long) ChpeLongJmp
989 stdcall RtlInitMemoryStream(ptr) ChpeRtlInitMemoryStream
991 stdcall RtlInitOutOfProcessMemoryStream(ptr) ChpeRtlInitOutOfProcessMemoryStream
1176 stdcall RtlReleaseMemoryStream(ptr) ChpeRtlReleaseMemoryStream
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
112 stdcall -version=0x600+ LdrAddDllDirectory(ptr ptr) ChpeAutoLdrAddDllDirectory
113 stdcall -version=0x600+ LdrAddLoadAsDataTable(ptr wstr long ptr) ChpeStubLdrAddLoadAsDataTable
115 stdcall LdrDisableThreadCalloutsForDll(ptr) ChpeAutoLdrDisableThreadCalloutsForDll
117 stdcall LdrEnumerateLoadedModules(long ptr ptr) ChpeAutoLdrEnumerateLoadedModules
120 stdcall  LdrFindResourceEx_U(ptr ptr ptr ptr ptr) ChpeStubLdrFindResourceEx_U
122 stdcall LdrFlushAlternateResourceModules() ChpeAutoLdrFlushAlternateResourceModules
123 stdcall -version=0x600+ LdrGetDllDirectory(ptr) ChpeAutoLdrGetDllDirectory
127 stdcall -version=0x600+ LdrGetDllPath(wstr long ptr ptr) ChpeAutoLdrGetDllPath
128 stdcall -version=0x600+ LdrGetFailureData() ChpeStubLdrGetFailureData
129 stdcall -version=0x600+ LdrGetFileNameFromLoadAsDataTable(ptr ptr) ChpeStubLdrGetFileNameFromLoadAsDataTable
130 stdcall -version=0x600+ LdrGetKnownDllSectionHandle(wstr long ptr) ChpeStubLdrGetKnownDllSectionHandle
132 stdcall -version=0x600+ LdrGetProcedureAddressEx(ptr ptr long ptr long) ChpeAutoLdrGetProcedureAddressEx
133 stdcall  LdrHotPatchRoutine(ptr) ChpeStubLdrHotPatchRoutine
134 stdcall LdrInitShimEngineDynamic(ptr) ChpeAutoLdrInitShimEngineDynamic
135 stdcall LdrInitializeThunk(long long long long) ChpeAutoLdrInitializeThunk
136 stdcall LdrLoadAlternateResourceModule(ptr ptr) ChpeAutoLdrLoadAlternateResourceModule
137 stdcall -version=0x600+ LdrLoadAlternateResourceModuleEx(long long ptr ptr long) ChpeStubLdrLoadAlternateResourceModuleEx
139 stdcall LdrLockLoaderLock(long ptr ptr) ChpeAutoLdrLockLoaderLock
140 stdcall LdrOpenImageFileOptionsKey(ptr long ptr) ChpeAutoLdrOpenImageFileOptionsKey
141 stdcall -version=0x600+ LdrProcessInitializationComplete() ChpeStubLdrProcessInitializationComplete
142 stdcall LdrProcessRelocationBlock(ptr long ptr long) ChpeAutoLdrProcessRelocationBlock
143 stdcall LdrQueryImageFileExecutionOptions(ptr str long ptr long ptr) ChpeAutoLdrQueryImageFileExecutionOptions
144 stdcall LdrQueryImageFileExecutionOptionsEx(ptr ptr long ptr long ptr long) ChpeAutoLdrQueryImageFileExecutionOptionsEx
145 stdcall LdrQueryImageFileKeyOption(ptr ptr long ptr long ptr) ChpeAutoLdrQueryImageFileKeyOption
146 stdcall -version=0x600+ LdrQueryModuleServiceTags(ptr ptr ptr) ChpeStubLdrQueryModuleServiceTags
148 stdcall -version=0x600+ LdrRegisterDllNotification(long ptr ptr ptr) ChpeAutoLdrRegisterDllNotification
149 stdcall -version=0x600+ LdrRemoveDllDirectory(ptr) ChpeAutoLdrRemoveDllDirectory
150 stdcall -version=0x600+ LdrRemoveLoadAsDataTable(ptr ptr ptr long) ChpeStubLdrRemoveLoadAsDataTable
153 stdcall -version=0x600+ LdrResFindResource(ptr long long long ptr ptr ptr ptr long) ChpeAutoLdrResFindResource
154 stdcall -version=0x600+ LdrResFindResourceDirectory(ptr long long ptr ptr ptr long ptr) ChpeAutoLdrResFindResourceDirectory
155 stdcall -version=0x600+ LdrResRelease(ptr ptr long long) ChpeStubLdrResRelease
156 stdcall -version=0x600+ LdrResSearchResource(wstr wstr long long long ptr long long) ChpeStubLdrResSearchResource
157 stdcall LdrSetAppCompatDllRedirectionCallback(long ptr ptr) ChpeAutoLdrSetAppCompatDllRedirectionCallback
158 stdcall -version=0x600+ LdrSetDefaultDllDirectories(long) ChpeAutoLdrSetDefaultDllDirectories
159 stdcall -version=0x600+ LdrSetDllDirectory(ptr) ChpeAutoLdrSetDllDirectory
160 stdcall LdrSetDllManifestProber(ptr) ChpeAutoLdrSetDllManifestProber
161 stdcall -version=0x600+ LdrSetMUICacheType(long) ChpeStubLdrSetMUICacheType
162 stdcall LdrShutdownProcess() ChpeAutoLdrShutdownProcess
163 stdcall LdrShutdownThread() ChpeAutoLdrShutdownThread
164 extern LdrSystemDllInitBlock ntdll.LdrSystemDllInitBlock
165 stdcall LdrUnloadAlternateResourceModule(ptr) ChpeAutoLdrUnloadAlternateResourceModule
166 stdcall -version=0x600+ LdrUnloadAlternateResourceModuleEx(long long) ChpeStubLdrUnloadAlternateResourceModuleEx
168 stdcall LdrUnlockLoaderLock(long ptr) ChpeAutoLdrUnlockLoaderLock
169 stdcall -version=0x600+ LdrUnregisterDllNotification(ptr) ChpeAutoLdrUnregisterDllNotification
170 stdcall LdrVerifyImageMatchesChecksum(ptr long long long) ChpeAutoLdrVerifyImageMatchesChecksum
171 stdcall -version=0x600+ LdrVerifyImageMatchesChecksumEx(ptr ptr) ChpeStubLdrVerifyImageMatchesChecksumEx
172 stdcall -version=0x600+ LdrpResGetMappingSize(long ptr long long) ChpeStubLdrpResGetMappingSize
173 stdcall -version=0x600+ LdrpResGetRCConfig(long long ptr long long) ChpeStubLdrpResGetRCConfig
174 stdcall -version=0x600+ LdrpResGetResourceDirectory(long long long ptr ptr) ChpeStubLdrpResGetResourceDirectory
175 stdcall -version=0x600+ MD4Final(ptr) ChpeAutoMD4Final
176 stdcall -version=0x600+ MD4Init(ptr) ChpeAutoMD4Init
177 stdcall -version=0x600+ MD4Update(ptr ptr long) ChpeAutoMD4Update
178 stdcall -version=0x600+ MD5Final(ptr) ChpeAutoMD5Final
179 stdcall -version=0x600+ MD5Init(ptr) ChpeAutoMD5Init
180 stdcall -version=0x600+ MD5Update(ptr ptr long) ChpeAutoMD5Update
181 extern NlsAnsiCodePage ntdll.NlsAnsiCodePage
182 extern NlsMbCodePageTag ntdll.NlsMbCodePageTag
183 extern NlsMbOemCodePageTag ntdll.NlsMbOemCodePageTag
184 stdcall NtAcceptConnectPort(ptr long ptr long long ptr) ChpeAutoNtAcceptConnectPort
185 stdcall NtAccessCheck(ptr long long ptr ptr ptr ptr ptr) ChpeAutoNtAccessCheck
186 stdcall NtAccessCheckAndAuditAlarm(ptr long ptr ptr ptr long ptr long ptr ptr ptr) ChpeAutoNtAccessCheckAndAuditAlarm
187 stdcall NtAccessCheckByType(ptr ptr ptr long ptr long ptr ptr long ptr ptr) ChpeAutoNtAccessCheckByType
188 stdcall NtAccessCheckByTypeAndAuditAlarm(ptr ptr ptr ptr ptr ptr long long long ptr long ptr long ptr ptr ptr) ChpeAutoNtAccessCheckByTypeAndAuditAlarm
189 stdcall NtAccessCheckByTypeResultList(ptr ptr ptr long ptr long ptr ptr long ptr ptr) ChpeAutoNtAccessCheckByTypeResultList
190 stdcall NtAccessCheckByTypeResultListAndAuditAlarm(ptr ptr ptr ptr ptr ptr long long long ptr long ptr long ptr ptr ptr) ChpeAutoNtAccessCheckByTypeResultListAndAuditAlarm
191 stdcall NtAccessCheckByTypeResultListAndAuditAlarmByHandle(ptr ptr ptr ptr ptr ptr ptr long long long ptr long ptr long ptr ptr ptr) ChpeAutoNtAccessCheckByTypeResultListAndAuditAlarmByHandle
192 stdcall -version=0x600+ NtAcquireCMFViewOwnership(ptr ptr long) ChpeStubNtAcquireCMFViewOwnership
193 stdcall NtAddAtom(ptr long ptr) ChpeAutoNtAddAtom
194 stdcall NtAddBootEntry(ptr long) ChpeAutoNtAddBootEntry
195 stdcall NtAddDriverEntry(ptr long) ChpeAutoNtAddDriverEntry
196 stdcall NtAdjustGroupsToken(long long ptr long ptr ptr) ChpeAutoNtAdjustGroupsToken
198 stdcall -version=0xA00+ NtAlertMultipleThreadByThreadId(ptr long ptr ptr) ChpeAutoNtAlertMultipleThreadByThreadId
199 stdcall NtAlertResumeThread(long ptr) ChpeAutoNtAlertResumeThread
200 stdcall NtAlertThread(long) ChpeAutoNtAlertThread
202 stdcall NtAllocateLocallyUniqueId(ptr) ChpeAutoNtAllocateLocallyUniqueId
203 stdcall -version=0x600+ NtAllocateReserveObject(ptr ptr long) ChpeAutoNtAllocateReserveObject
204 stdcall NtAllocateUserPhysicalPages(ptr ptr ptr) ChpeAutoNtAllocateUserPhysicalPages
205 stdcall NtAllocateUuids(ptr ptr ptr ptr) ChpeAutoNtAllocateUuids
208 stdcall -version=0x600+ NtAlpcAcceptConnectPort(ptr ptr long ptr ptr ptr ptr ptr long) ChpeAutoNtAlpcAcceptConnectPort
209 stdcall -version=0x600+ NtAlpcCancelMessage(ptr long ptr) ChpeAutoNtAlpcCancelMessage
210 stdcall -version=0x600+ NtAlpcConnectPort(ptr ptr ptr ptr long ptr ptr ptr ptr ptr ptr) ChpeAutoNtAlpcConnectPort
211 stdcall -version=0x602+ NtAlpcConnectPortEx(ptr ptr ptr ptr long ptr ptr ptr ptr ptr ptr) ChpeAutoNtAlpcConnectPortEx
212 stdcall -version=0x600+ NtAlpcCreatePort(ptr ptr ptr) ChpeAutoNtAlpcCreatePort
213 stdcall -version=0x600+ NtAlpcCreatePortSection(ptr long ptr long ptr ptr) ChpeAutoNtAlpcCreatePortSection
214 stdcall -version=0x600+ NtAlpcCreateResourceReserve(ptr long long ptr) ChpeAutoNtAlpcCreateResourceReserve
215 stdcall -version=0x600+ NtAlpcCreateSectionView(ptr long ptr) ChpeAutoNtAlpcCreateSectionView
216 stdcall -version=0x600+ NtAlpcCreateSecurityContext(ptr long ptr) ChpeAutoNtAlpcCreateSecurityContext
217 stdcall -version=0x600+ NtAlpcDeletePortSection(ptr long ptr) ChpeAutoNtAlpcDeletePortSection
218 stdcall -version=0x600+ NtAlpcDeleteResourceReserve(ptr long long) ChpeAutoNtAlpcDeleteResourceReserve
219 stdcall -version=0x600+ NtAlpcDeleteSectionView(ptr long ptr) ChpeAutoNtAlpcDeleteSectionView
220 stdcall -version=0x600+ NtAlpcDeleteSecurityContext(ptr long ptr) ChpeAutoNtAlpcDeleteSecurityContext
221 stdcall -version=0x600+ NtAlpcDisconnectPort(ptr long) ChpeAutoNtAlpcDisconnectPort
222 stdcall -version=0x600+ NtAlpcImpersonateClientOfPort(ptr ptr ptr) ChpeAutoNtAlpcImpersonateClientOfPort
223 stdcall -version=0xA00+ NtAlpcImpersonateClientContainerOfPort(ptr ptr long) ChpeAutoNtAlpcImpersonateClientContainerOfPort
224 stdcall -version=0x600+ NtAlpcOpenSenderProcess(ptr ptr ptr long long ptr) ChpeAutoNtAlpcOpenSenderProcess
225 stdcall -version=0x600+ NtAlpcOpenSenderThread(ptr ptr ptr long long ptr) ChpeAutoNtAlpcOpenSenderThread
226 stdcall -version=0x600+ NtAlpcQueryInformation(ptr long ptr long ptr) ChpeAutoNtAlpcQueryInformation
227 stdcall -version=0x600+ NtAlpcQueryInformationMessage(ptr ptr long ptr long ptr) ChpeAutoNtAlpcQueryInformationMessage
228 stdcall -version=0x600+ NtAlpcRevokeSecurityContext(ptr long ptr) ChpeAutoNtAlpcRevokeSecurityContext
229 stdcall -version=0x600+ NtAlpcSendWaitReceivePort(ptr long ptr ptr ptr ptr ptr ptr) ChpeAutoNtAlpcSendWaitReceivePort
230 stdcall -version=0x600+ NtAlpcSetInformation(ptr long ptr long) ChpeAutoNtAlpcSetInformation
231 stdcall NtApphelpCacheControl(long ptr) ChpeAutoNtApphelpCacheControl
232 stdcall NtAreMappedFilesTheSame(ptr ptr) ChpeAutoNtAreMappedFilesTheSame
233 stdcall NtAssignProcessToJobObject(long long) ChpeAutoNtAssignProcessToJobObject
234 stdcall NtCallbackReturn(ptr long long) ChpeAutoNtCallbackReturn
235 stdcall NtCancelDeviceWakeupRequest(ptr) ChpeAutoNtCancelDeviceWakeupRequest
236 stdcall NtCancelIoFile(long ptr) ChpeAutoNtCancelIoFile
237 stdcall -version=0x600+ NtCancelIoFileEx(long ptr ptr) ChpeAutoNtCancelIoFileEx
238 stdcall -version=0x600+ NtCancelSynchronousIoFile(long ptr ptr) ChpeAutoNtCancelSynchronousIoFile
239 stdcall NtCancelTimer(long ptr) ChpeAutoNtCancelTimer
240 stdcall NtClearEvent(long) ChpeAutoNtClearEvent
242 stdcall NtCloseObjectAuditAlarm(ptr ptr long) ChpeAutoNtCloseObjectAuditAlarm
243 stdcall -version=0x600+ NtCompareObjects(ptr ptr) ChpeAutoNtCompareObjects
244 stdcall -version=0x600+ NtCommitComplete(ptr ptr) ChpeAutoNtCommitComplete
245 stdcall -version=0x600+ NtCommitEnlistment(ptr ptr) ChpeAutoNtCommitEnlistment
246 stdcall -version=0x600+ NtCommitTransaction(ptr long) ChpeAutoNtCommitTransaction
247 stdcall NtCompactKeys(long ptr) ChpeAutoNtCompactKeys
248 stdcall NtCompareTokens(ptr ptr ptr) ChpeAutoNtCompareTokens
249 stdcall NtCompleteConnectPort(ptr) ChpeAutoNtCompleteConnectPort
250 stdcall NtCompressKey(ptr) ChpeAutoNtCompressKey
251 stdcall NtConnectPort(ptr ptr ptr ptr ptr ptr ptr ptr) ChpeAutoNtConnectPort
254 stdcall -version=0xA00+ NtConvertBetweenAuxiliaryCounterAndPerformanceCounter(long ptr ptr ptr) ChpeAutoNtConvertBetweenAuxiliaryCounterAndPerformanceCounter
255 stdcall NtCreateDebugObject(ptr long ptr long) ChpeAutoNtCreateDebugObject
256 stdcall NtCreateDirectoryObject(long long long) ChpeAutoNtCreateDirectoryObject
257 stdcall -version=0x600+ NtCreateEnlistment(ptr long ptr ptr ptr long long ptr) ChpeAutoNtCreateEnlistment
258 stdcall NtCreateEvent(long long long long long) ChpeAutoNtCreateEvent
259 stdcall NtCreateEventPair(ptr long ptr) ChpeAutoNtCreateEventPair
261 stdcall NtCreateIoCompletion(ptr long ptr long) ChpeAutoNtCreateIoCompletion
265 stdcall NtCreateJobObject(ptr long ptr) ChpeAutoNtCreateJobObject
266 stdcall NtCreateJobSet(long ptr long) ChpeAutoNtCreateJobSet
268 stdcall -version=0x600+ NtCreateKeyTransacted(ptr long ptr long ptr long ptr ptr) ChpeAutoNtCreateKeyTransacted
269 stdcall NtCreateKeyedEvent(ptr long ptr long) ChpeAutoNtCreateKeyedEvent
270 stdcall NtCreateMailslotFile(long long long long long long long long) ChpeAutoNtCreateMailslotFile
271 stdcall NtCreateMutant(ptr long ptr long) ChpeAutoNtCreateMutant
273 stdcall NtCreatePagingFile(ptr ptr ptr long) ChpeAutoNtCreatePagingFile
274 stdcall NtCreatePort(ptr ptr long long ptr) ChpeAutoNtCreatePort
275 stdcall -version=0x600+ NtCreatePrivateNamespace(ptr long ptr ptr) ChpeAutoNtCreatePrivateNamespace
276 stdcall NtCreateProcess(ptr long ptr ptr long ptr ptr ptr) ChpeAutoNtCreateProcess
277 stdcall NtCreateProcessEx(ptr long ptr ptr long ptr ptr ptr long) ChpeAutoNtCreateProcessEx
278 stdcall NtCreateProfile(ptr ptr ptr long long ptr long long long) ChpeAutoNtCreateProfile
279 stdcall -version=0x600+ NtCreateResourceManager(ptr long ptr ptr ptr long ptr) ChpeAutoNtCreateResourceManager
280 stdcall NtCreateSection(ptr long ptr ptr long long ptr) ChpeAutoNtCreateSection
281 stdcall NtCreateSectionEx(ptr long ptr ptr long long ptr ptr long) ChpeAutoNtCreateSectionEx
282 stdcall NtCreateSemaphore(ptr long ptr long long) ChpeAutoNtCreateSemaphore
283 stdcall NtCreateSymbolicLinkObject(ptr long ptr ptr) ChpeAutoNtCreateSymbolicLinkObject
285 stdcall -version=0x600+ NtCreateThreadEx(ptr long ptr ptr ptr ptr long long long long ptr) ChpeAutoNtCreateThreadEx
286 stdcall NtCreateTimer(ptr long ptr long) ChpeAutoNtCreateTimer
287 stdcall NtCreateTimer2(ptr ptr ptr long long) ChpeAutoNtCreateTimer2
288 stdcall NtCreateToken(ptr long ptr long ptr ptr ptr ptr ptr ptr ptr ptr ptr) ChpeAutoNtCreateToken
289 stdcall -version=0x600+ NtCreateTransaction(ptr long ptr ptr ptr long long long ptr ptr) ChpeAutoNtCreateTransaction
290 stdcall -version=0x600+ NtCreateTransactionManager(ptr long ptr ptr long long) ChpeAutoNtCreateTransactionManager
291 stdcall -version=0x600+ NtCreateUserProcess(ptr ptr long long ptr ptr long long ptr ptr ptr) ChpeAutoNtCreateUserProcess
292 stdcall NtCreateWaitablePort(ptr ptr long long long) ChpeAutoNtCreateWaitablePort
293 stdcall -version=0x602+ NtCreateWnfStateName(ptr long long long ptr long ptr) ChpeAutoNtCreateWnfStateName
294 stdcall -version=0x600+ NtCreateWorkerFactory(ptr long long long long long long long long long) ChpeStubNtCreateWorkerFactory
295 stdcall NtDebugActiveProcess(ptr ptr) ChpeAutoNtDebugActiveProcess
296 stdcall NtDebugContinue(ptr ptr long) ChpeAutoNtDebugContinue
297 stdcall NtDelayExecution(long ptr) ChpeAutoNtDelayExecution
298 stdcall NtDeleteAtom(long) ChpeAutoNtDeleteAtom
299 stdcall NtDeleteBootEntry(long) ChpeAutoNtDeleteBootEntry
300 stdcall NtDeleteDriverEntry(long) ChpeAutoNtDeleteDriverEntry
301 stdcall NtDeleteFile(ptr) ChpeAutoNtDeleteFile
303 stdcall NtDeleteObjectAuditAlarm(ptr ptr long) ChpeAutoNtDeleteObjectAuditAlarm
304 stdcall -version=0x600+ NtDeletePrivateNamespace(ptr) ChpeAutoNtDeletePrivateNamespace
306 stdcall -version=0x602+ NtDeleteWnfStateData(ptr ptr) ChpeAutoNtDeleteWnfStateData
307 stdcall -version=0x602+ NtDeleteWnfStateName(ptr) ChpeAutoNtDeleteWnfStateName
309 stdcall NtDisplayString(ptr) ChpeAutoNtDisplayString
310 stdcall NtDuplicateObject(long long long ptr long long long) ChpeAutoNtDuplicateObject
311 stdcall NtDuplicateToken(long long long long long long) ChpeAutoNtDuplicateToken
312 stdcall NtEnumerateBootEntries(ptr ptr) ChpeAutoNtEnumerateBootEntries
313 stdcall NtEnumerateDriverEntries(ptr ptr) ChpeAutoNtEnumerateDriverEntries
315 stdcall NtEnumerateSystemEnvironmentValuesEx(long ptr long) ChpeAutoNtEnumerateSystemEnvironmentValuesEx
316 stdcall -version=0x600+ NtEnumerateTransactionObject(ptr long ptr long ptr) ChpeAutoNtEnumerateTransactionObject
318 stdcall NtExtendSection(ptr ptr) ChpeAutoNtExtendSection
319 stdcall NtFilterToken(ptr long ptr ptr ptr ptr) ChpeAutoNtFilterToken
320 stdcall -version=0x602+ NtCreateLowBoxToken(ptr ptr long ptr ptr long ptr long ptr) ChpeAutoNtCreateLowBoxToken
321 stdcall NtFindAtom(ptr long ptr) ChpeAutoNtFindAtom
322 stdcall NtFlushBuffersFile(long ptr) ChpeAutoNtFlushBuffersFile
323 stdcall NtFlushBuffersFileEx(long long ptr long ptr) ChpeAutoNtFlushBuffersFileEx
324 stdcall -version=0x600+ NtFlushInstallUILanguage(long long) ChpeStubNtFlushInstallUILanguage
328 stdcall NtFlushVirtualMemory(ptr ptr ptr ptr) ChpeAutoNtFlushVirtualMemory
329 stdcall NtFlushWriteBuffer() ChpeAutoNtFlushWriteBuffer
330 stdcall NtFreeUserPhysicalPages(ptr ptr ptr) ChpeAutoNtFreeUserPhysicalPages
332 stdcall -version=0x600+ NtFreezeRegistry(long) ChpeStubNtFreezeRegistry
333 stdcall -version=0x600+ NtFreezeTransactions(ptr ptr) ChpeStubNtFreezeTransactions
336 stdcall NtGetCurrentProcessorNumber() ChpeAutoNtGetCurrentProcessorNumber
337 stdcall -version=0xA00+ NtGetCurrentProcessorNumberEx(ptr) ChpeAutoNtGetCurrentProcessorNumberEx
338 stdcall NtGetDevicePowerState(ptr ptr) ChpeAutoNtGetDevicePowerState
339 stdcall -version=0x600+ NtGetMUIRegistryInfo(long ptr ptr) ChpeStubNtGetMUIRegistryInfo
340 stdcall -version=0x600+ NtGetNextProcess(long long long long ptr) ChpeStubNtGetNextProcess
342 stdcall -version=0x600+ NtGetNlsSectionPtr(long long ptr ptr ptr) ChpeAutoNtGetNlsSectionPtr
343 stdcall -version=0x600+ NtGetNotificationResourceManager(ptr ptr long ptr ptr long ptr) ChpeAutoNtGetNotificationResourceManager
344 stdcall NtGetPlugPlayEvent(long long ptr long) ChpeAutoNtGetPlugPlayEvent
345 stdcall NtGetTickCount() ChpeAutoNtGetTickCount
346 stdcall NtGetWriteWatch(long long ptr long ptr ptr ptr) ChpeAutoNtGetWriteWatch
347 stdcall NtImpersonateAnonymousToken(ptr) ChpeAutoNtImpersonateAnonymousToken
348 stdcall NtImpersonateClientOfPort(ptr ptr) ChpeAutoNtImpersonateClientOfPort
349 stdcall NtImpersonateThread(ptr ptr ptr) ChpeAutoNtImpersonateThread
350 stdcall -version=0x600+ NtInitializeNlsFiles(ptr ptr ptr) ChpeAutoNtInitializeNlsFiles
351 stdcall NtInitializeRegistry(long) ChpeAutoNtInitializeRegistry
352 stdcall NtInitiatePowerAction(long long long long) ChpeAutoNtInitiatePowerAction
353 stdcall NtIsProcessInJob(long long) ChpeAutoNtIsProcessInJob
354 stdcall NtIsSystemResumeAutomatic() ChpeAutoNtIsSystemResumeAutomatic
355 stdcall -version=0x600+ NtIsUILanguageComitted() ChpeStubNtIsUILanguageComitted
356 stdcall NtListenPort(ptr ptr) ChpeAutoNtListenPort
357 stdcall NtLoadDriver(ptr) ChpeAutoNtLoadDriver
358 stdcall NtLoadKey2(ptr ptr long) ChpeAutoNtLoadKey2
359 stdcall NtLoadKey(ptr ptr) ChpeAutoNtLoadKey
360 stdcall NtLoadKeyEx(ptr ptr long ptr ptr long ptr ptr) ChpeAutoNtLoadKeyEx
361 stdcall NtLockFile(long long ptr ptr ptr ptr ptr ptr long long) ChpeAutoNtLockFile
362 stdcall NtLockProductActivationKeys(ptr ptr) ChpeAutoNtLockProductActivationKeys
363 stdcall NtLockRegistryKey(ptr) ChpeAutoNtLockRegistryKey
364 stdcall NtLockVirtualMemory(long ptr ptr long) ChpeAutoNtLockVirtualMemory
365 stdcall NtMakePermanentObject(ptr) ChpeAutoNtMakePermanentObject
366 stdcall NtMakeTemporaryObject(long) ChpeAutoNtMakeTemporaryObject
367 stdcall -version=0x600+ NtMapCMFModule(long long ptr ptr ptr ptr) ChpeStubNtMapCMFModule
368 stdcall NtMapUserPhysicalPages(ptr ptr ptr) ChpeAutoNtMapUserPhysicalPages
369 stdcall NtMapUserPhysicalPagesScatter(ptr ptr ptr) ChpeAutoNtMapUserPhysicalPagesScatter
372 stdcall NtModifyBootEntry(ptr) ChpeAutoNtModifyBootEntry
373 stdcall NtModifyDriverEntry(ptr) ChpeAutoNtModifyDriverEntry
374 stdcall NtNotifyChangeDirectoryFile(long long ptr ptr ptr ptr long long long) ChpeAutoNtNotifyChangeDirectoryFile
375 stdcall NtNotifyChangeDirectoryFileEx(long long ptr ptr ptr ptr long long long long) ChpeAutoNtNotifyChangeDirectoryFileEx
378 stdcall NtOpenDirectoryObject(long long long) ChpeAutoNtOpenDirectoryObject
379 stdcall -version=0x600+ NtOpenEnlistment(ptr long ptr ptr ptr) ChpeAutoNtOpenEnlistment
380 stdcall NtOpenEvent(long long long) ChpeAutoNtOpenEvent
381 stdcall NtOpenEventPair(ptr long ptr) ChpeAutoNtOpenEventPair
383 stdcall NtOpenIoCompletion(ptr long ptr) ChpeAutoNtOpenIoCompletion
384 stdcall NtOpenJobObject(ptr long ptr) ChpeAutoNtOpenJobObject
386 stdcall -version=0x600+ NtOpenKeyTransacted(ptr long ptr ptr) ChpeAutoNtOpenKeyTransacted
387 stdcall -version=0x600+ NtOpenKeyTransactedEx(ptr long ptr long ptr) ChpeAutoNtOpenKeyTransactedEx
388 stdcall NtOpenKeyedEvent(ptr long ptr) ChpeAutoNtOpenKeyedEvent
389 stdcall NtOpenMutant(ptr long ptr) ChpeAutoNtOpenMutant
390 stdcall NtOpenObjectAuditAlarm(ptr ptr ptr ptr ptr ptr long long ptr long long ptr) ChpeAutoNtOpenObjectAuditAlarm
391 stdcall -version=0x600+ NtOpenPrivateNamespace(ptr long ptr ptr) ChpeAutoNtOpenPrivateNamespace
392 stdcall NtOpenProcess(ptr long ptr ptr) ChpeAutoNtOpenProcess
393 stdcall NtOpenProcessToken(long long ptr) ChpeAutoNtOpenProcessToken
394 stdcall NtOpenProcessTokenEx(long long long ptr) ChpeAutoNtOpenProcessTokenEx
395 stdcall -version=0x600+ NtOpenResourceManager(ptr long ptr ptr ptr) ChpeAutoNtOpenResourceManager
396 stdcall NtOpenSection(ptr long ptr) ChpeAutoNtOpenSection
397 stdcall NtOpenSemaphore(long long ptr) ChpeAutoNtOpenSemaphore
398 stdcall -version=0x600+ NtOpenSession(ptr long ptr) ChpeStubNtOpenSession
399 stdcall NtOpenSymbolicLinkObject(ptr long ptr) ChpeAutoNtOpenSymbolicLinkObject
400 stdcall NtOpenThread(ptr long ptr ptr) ChpeAutoNtOpenThread
401 stdcall NtOpenThreadToken(long long long ptr) ChpeAutoNtOpenThreadToken
402 stdcall NtOpenThreadTokenEx(long long long long ptr) ChpeAutoNtOpenThreadTokenEx
403 stdcall NtOpenTimer(ptr long ptr) ChpeAutoNtOpenTimer
404 stdcall -version=0x600+ NtOpenTransaction(ptr long ptr ptr ptr) ChpeAutoNtOpenTransaction
405 stdcall -version=0x600+ NtOpenTransactionManager(ptr long ptr ptr ptr long) ChpeAutoNtOpenTransactionManager
406 stdcall NtPlugPlayControl(ptr ptr long) ChpeAutoNtPlugPlayControl
407 stdcall NtPowerInformation(long ptr long ptr long) ChpeAutoNtPowerInformation
408 stdcall -version=0x600+ NtPrePrepareComplete(ptr ptr) ChpeAutoNtPrePrepareComplete
409 stdcall -version=0x600+ NtPrePrepareEnlistment(ptr ptr) ChpeAutoNtPrePrepareEnlistment
410 stdcall -version=0x600+ NtPrepareComplete(ptr ptr) ChpeAutoNtPrepareComplete
411 stdcall -version=0x600+ NtPrepareEnlistment(ptr ptr) ChpeAutoNtPrepareEnlistment
412 stdcall NtPrivilegeCheck(ptr ptr ptr) ChpeAutoNtPrivilegeCheck
413 stdcall NtPrivilegeObjectAuditAlarm(ptr ptr ptr long ptr long) ChpeAutoNtPrivilegeObjectAuditAlarm
414 stdcall NtPrivilegedServiceAuditAlarm(ptr ptr ptr ptr long) ChpeAutoNtPrivilegedServiceAuditAlarm
415 stdcall -version=0x600+ NtPropagationComplete(ptr long long ptr) ChpeAutoNtPropagationComplete
416 stdcall -version=0x600+ NtPropagationFailed(ptr long long) ChpeAutoNtPropagationFailed
418 stdcall NtPulseEvent(long ptr) ChpeAutoNtPulseEvent
419 stdcall NtQueryAttributesFile(ptr ptr) ChpeAutoNtQueryAttributesFile
420 stdcall NtQueryBootEntryOrder(ptr ptr) ChpeAutoNtQueryBootEntryOrder
421 stdcall NtQueryBootOptions(ptr ptr) ChpeAutoNtQueryBootOptions
422 stdcall NtQueryDebugFilterState(long long) ChpeAutoNtQueryDebugFilterState
423 stdcall NtQueryDefaultLocale(long ptr) ChpeAutoNtQueryDefaultLocale
424 stdcall NtQueryDefaultUILanguage(ptr) ChpeAutoNtQueryDefaultUILanguage
426 stdcall NtQueryDirectoryFileEx(long long ptr ptr ptr ptr long long long ptr) ChpeAutoNtQueryDirectoryFileEx
427 stdcall NtQueryDirectoryObject(long ptr long long long ptr ptr) ChpeAutoNtQueryDirectoryObject
428 stdcall NtQueryDriverEntryOrder(ptr ptr) ChpeAutoNtQueryDriverEntryOrder
429 stdcall NtQueryEaFile(long ptr ptr long long ptr long ptr long) ChpeAutoNtQueryEaFile
430 stdcall NtQueryEvent(long long ptr long ptr) ChpeAutoNtQueryEvent
431 stdcall NtQueryFullAttributesFile(ptr ptr) ChpeAutoNtQueryFullAttributesFile
432 stdcall NtQueryInformationAtom(long long ptr long ptr) ChpeAutoNtQueryInformationAtom
433 stdcall NtQueryInformationByName(ptr ptr ptr long long) ChpeAutoNtQueryInformationByName
434 stdcall -version=0x600+ NtQueryInformationEnlistment(ptr long ptr long ptr) ChpeAutoNtQueryInformationEnlistment
436 stdcall NtQueryInformationJobObject(ptr long ptr long ptr) ChpeAutoNtQueryInformationJobObject
437 stdcall NtQueryInformationPort(ptr long ptr long ptr) ChpeAutoNtQueryInformationPort
439 stdcall -version=0x600+ NtQueryInformationResourceManager(ptr long ptr long ptr) ChpeAutoNtQueryInformationResourceManager
441 stdcall NtQueryInformationToken(ptr long ptr long ptr) ChpeAutoNtQueryInformationToken
442 stdcall -version=0x600+ NtQueryInformationTransaction(ptr long ptr long ptr) ChpeAutoNtQueryInformationTransaction
443 stdcall -version=0x600+ NtQueryInformationTransactionManager(ptr long ptr long ptr) ChpeAutoNtQueryInformationTransactionManager
444 stdcall -version=0x600+ NtQueryInformationWorkerFactory(ptr long ptr long ptr) ChpeStubNtQueryInformationWorkerFactory
445 stdcall NtQueryInstallUILanguage(ptr) ChpeAutoNtQueryInstallUILanguage
446 stdcall NtQueryIntervalProfile(long ptr) ChpeAutoNtQueryIntervalProfile
447 stdcall NtQueryIoCompletion(long long ptr long ptr) ChpeAutoNtQueryIoCompletion
449 stdcall -version=0x600+ NtQueryLicenseValue(ptr ptr ptr long ptr) ChpeAutoNtQueryLicenseValue
450 stdcall NtQueryMultipleValueKey(long ptr long ptr long ptr) ChpeAutoNtQueryMultipleValueKey
451 stdcall NtQueryMutant(long long ptr long ptr) ChpeAutoNtQueryMutant
453 stdcall NtQueryOpenSubKeys(ptr ptr) ChpeAutoNtQueryOpenSubKeys
454 stdcall NtQueryOpenSubKeysEx(ptr long ptr ptr) ChpeAutoNtQueryOpenSubKeysEx
455 stdcall NtQueryPerformanceCounter(ptr ptr) ChpeAutoNtQueryPerformanceCounter
456 stdcall NtQueryPortInformationProcess() ChpeAutoNtQueryPortInformationProcess
457 stdcall NtQueryQuotaInformationFile(ptr ptr ptr long long ptr long ptr long) ChpeAutoNtQueryQuotaInformationFile
458 stdcall NtQuerySection(long long long long long) ChpeAutoNtQuerySection
459 stdcall NtQuerySecurityObject(long long long long long) ChpeAutoNtQuerySecurityObject
460 stdcall NtQuerySemaphore(long long ptr long ptr) ChpeAutoNtQuerySemaphore
461 stdcall NtQuerySymbolicLinkObject(long ptr ptr) ChpeAutoNtQuerySymbolicLinkObject
462 stdcall NtQuerySystemEnvironmentValue(ptr ptr long ptr) ChpeAutoNtQuerySystemEnvironmentValue
463 stdcall NtQuerySystemEnvironmentValueEx(ptr ptr ptr ptr ptr) ChpeAutoNtQuerySystemEnvironmentValueEx
465 stdcall -version=0x601+ NtQuerySystemInformationEx(long ptr long ptr long ptr) ChpeAutoNtQuerySystemInformationEx
466 stdcall NtQuerySystemTime(ptr) ChpeAutoNtQuerySystemTime
467 stdcall NtQueryTimer(ptr long ptr long ptr) ChpeAutoNtQueryTimer
468 stdcall NtQueryTimerResolution(long long long) ChpeAutoNtQueryTimerResolution
472 stdcall -version=0x602+ NtQueryWnfStateData(ptr ptr ptr ptr ptr ptr) ChpeAutoNtQueryWnfStateData
473 stdcall -version=0x602+ NtQueryWnfStateNameInformation(ptr long ptr ptr long) ChpeAutoNtQueryWnfStateNameInformation
474 stdcall NtQueueApcThread(long ptr long long long) ChpeAutoNtQueueApcThread
475 stdcall -version=0x601+ NtQueueApcThreadEx(long long ptr long long long) ChpeAutoNtQueueApcThreadEx
476 stdcall -version=0xA00+ NtQueueApcThreadEx2(long long long ptr long long long) ChpeAutoNtQueueApcThreadEx2
478 stdcall NtRaiseHardError(long long long ptr long ptr) ChpeAutoNtRaiseHardError
480 stdcall NtReadFileScatter(long long ptr ptr ptr ptr long ptr ptr) ChpeAutoNtReadFileScatter
481 stdcall -version=0x600+ NtReadOnlyEnlistment(ptr ptr) ChpeAutoNtReadOnlyEnlistment
482 stdcall NtReadRequestData(ptr ptr long ptr long ptr) ChpeAutoNtReadRequestData
484 stdcall -version=0x600+ NtRecoverEnlistment(ptr ptr) ChpeAutoNtRecoverEnlistment
485 stdcall -version=0x600+ NtRecoverResourceManager(ptr) ChpeAutoNtRecoverResourceManager
486 stdcall -version=0x600+ NtRecoverTransactionManager(ptr) ChpeAutoNtRecoverTransactionManager
487 stdcall -version=0x600+ NtRegisterProtocolAddressInformation(ptr ptr long ptr long) ChpeAutoNtRegisterProtocolAddressInformation
488 stdcall NtRegisterThreadTerminatePort(ptr) ChpeAutoNtRegisterThreadTerminatePort
489 stdcall -version=0x600+ NtReleaseCMFViewOwnership() ChpeStubNtReleaseCMFViewOwnership
490 stdcall NtReleaseKeyedEvent(ptr ptr long ptr) ChpeAutoNtReleaseKeyedEvent
491 stdcall NtReleaseMutant(long ptr) ChpeAutoNtReleaseMutant
492 stdcall NtReleaseSemaphore(long long ptr) ChpeAutoNtReleaseSemaphore
493 stdcall -version=0x600+ NtReleaseWorkerFactoryWorker(ptr) ChpeStubNtReleaseWorkerFactoryWorker
494 stdcall NtRemoveIoCompletion(ptr ptr ptr ptr ptr) ChpeAutoNtRemoveIoCompletion
495 stdcall -version=0x600+ NtRemoveIoCompletionEx(ptr ptr long ptr ptr long) ChpeAutoNtRemoveIoCompletionEx
496 stdcall NtRemoveProcessDebug(ptr ptr) ChpeAutoNtRemoveProcessDebug
497 stdcall NtRenameKey(ptr ptr) ChpeAutoNtRenameKey
498 stdcall -version=0x600+ NtRenameTransactionManager(ptr ptr) ChpeAutoNtRenameTransactionManager
499 stdcall NtReplaceKey(ptr long ptr) ChpeAutoNtReplaceKey
500 stdcall -version=0x600+ NtReplacePartitionUnit(wstr wstr long) ChpeStubNtReplacePartitionUnit
501 stdcall NtReplyPort(ptr ptr) ChpeAutoNtReplyPort
502 stdcall NtReplyWaitReceivePort(ptr ptr ptr ptr) ChpeAutoNtReplyWaitReceivePort
503 stdcall NtReplyWaitReceivePortEx(ptr ptr ptr ptr ptr) ChpeAutoNtReplyWaitReceivePortEx
504 stdcall NtReplyWaitReplyPort(ptr ptr) ChpeAutoNtReplyWaitReplyPort
505 stdcall NtRequestDeviceWakeup(ptr) ChpeAutoNtRequestDeviceWakeup
506 stdcall NtRequestPort(ptr ptr) ChpeAutoNtRequestPort
507 stdcall NtRequestWaitReplyPort(ptr ptr ptr) ChpeAutoNtRequestWaitReplyPort
508 stdcall NtRequestWakeupLatency(long) ChpeAutoNtRequestWakeupLatency
509 stdcall NtResetEvent(long ptr) ChpeAutoNtResetEvent
510 stdcall NtResetWriteWatch(long ptr long) ChpeAutoNtResetWriteWatch
511 stdcall NtRestoreKey(long long long) ChpeAutoNtRestoreKey
512 stdcall NtResumeProcess(ptr) ChpeAutoNtResumeProcess
514 stdcall -version=0x600+ NtRollbackComplete(ptr ptr) ChpeAutoNtRollbackComplete
515 stdcall -version=0x600+ NtRollbackEnlistment(ptr ptr) ChpeAutoNtRollbackEnlistment
516 stdcall -version=0x600+ NtRollbackTransaction(ptr long) ChpeAutoNtRollbackTransaction
517 stdcall -version=0x600+ NtRollforwardTransactionManager(ptr ptr) ChpeAutoNtRollforwardTransactionManager
518 stdcall NtSaveKey(long long) ChpeAutoNtSaveKey
519 stdcall NtSaveKeyEx(ptr ptr long) ChpeAutoNtSaveKeyEx
520 stdcall NtSaveMergedKeys(ptr ptr ptr) ChpeAutoNtSaveMergedKeys
521 stdcall NtSecureConnectPort(ptr ptr ptr ptr ptr ptr ptr ptr ptr) ChpeAutoNtSecureConnectPort
522 stdcall NtSetBootEntryOrder(ptr ptr) ChpeAutoNtSetBootEntryOrder
523 stdcall NtSetBootOptions(ptr long) ChpeAutoNtSetBootOptions
525 stdcall NtSetDebugFilterState(long long long) ChpeAutoNtSetDebugFilterState
526 stdcall NtSetDefaultHardErrorPort(ptr) ChpeAutoNtSetDefaultHardErrorPort
527 stdcall NtSetDefaultLocale(long long) ChpeAutoNtSetDefaultLocale
528 stdcall NtSetDefaultUILanguage(long) ChpeAutoNtSetDefaultUILanguage
529 stdcall NtSetDriverEntryOrder(ptr ptr) ChpeAutoNtSetDriverEntryOrder
530 stdcall NtSetEaFile(long ptr ptr long) ChpeAutoNtSetEaFile
531 stdcall NtSetEvent(long long) ChpeAutoNtSetEvent
532 stdcall NtSetEventBoostPriority(ptr) ChpeAutoNtSetEventBoostPriority
533 stdcall NtSetHighEventPair(ptr) ChpeAutoNtSetHighEventPair
534 stdcall NtSetHighWaitLowEventPair(ptr) ChpeAutoNtSetHighWaitLowEventPair
535 stdcall NtSetInformationDebugObject(ptr long ptr long ptr) ChpeAutoNtSetInformationDebugObject
536 stdcall -version=0x600+ NtSetInformationEnlistment(ptr long ptr long) ChpeAutoNtSetInformationEnlistment
537 stdcall NtSetInformationFile(ptr ptr ptr long long) ChpeAutoNtSetInformationFile
538 stdcall NtSetInformationJobObject(ptr long ptr long) ChpeAutoNtSetInformationJobObject
539 stdcall NtSetInformationKey(ptr long ptr long) ChpeAutoNtSetInformationKey
540 stdcall NtSetInformationObject(ptr long ptr long) ChpeAutoNtSetInformationObject
541 stdcall NtSetInformationProcess(ptr long ptr long) ChpeAutoNtSetInformationProcess
542 stdcall -version=0x600+ NtSetInformationResourceManager(ptr long ptr long) ChpeAutoNtSetInformationResourceManager
543 stdcall NtSetInformationThread(ptr long ptr long) ChpeAutoNtSetInformationThread
544 stdcall NtSetInformationVirtualMemory(ptr long ptr ptr ptr long) ChpeAutoNtSetInformationVirtualMemory
545 stdcall NtSetInformationToken(ptr long ptr long) ChpeAutoNtSetInformationToken
546 stdcall -version=0x600+ NtSetInformationTransaction(ptr long ptr long) ChpeAutoNtSetInformationTransaction
547 stdcall -version=0x600+ NtSetInformationTransactionManager(ptr long ptr long) ChpeAutoNtSetInformationTransactionManager
548 stdcall -version=0x600+ NtSetInformationWorkerFactory(ptr long ptr long) ChpeStubNtSetInformationWorkerFactory
549 stdcall NtSetIntervalProfile(long long) ChpeAutoNtSetIntervalProfile
550 stdcall NtSetIoCompletion(ptr long ptr long long) ChpeAutoNtSetIoCompletion
551 stdcall -version=0x600+ NtSetIoCompletionEx(ptr ptr long long long long) ChpeAutoNtSetIoCompletionEx
552 stdcall NtSetLdtEntries(long int64 long int64) ChpeAutoNtSetLdtEntries
553 stdcall NtSetLowEventPair(ptr) ChpeAutoNtSetLowEventPair
554 stdcall NtSetLowWaitHighEventPair(ptr) ChpeAutoNtSetLowWaitHighEventPair
555 stdcall NtSetQuotaInformationFile(ptr ptr ptr long) ChpeAutoNtSetQuotaInformationFile
556 stdcall NtSetSecurityObject(long long ptr) ChpeAutoNtSetSecurityObject
557 stdcall NtSetSystemEnvironmentValue(ptr ptr) ChpeAutoNtSetSystemEnvironmentValue
558 stdcall NtSetSystemEnvironmentValueEx(ptr ptr ptr ptr ptr) ChpeAutoNtSetSystemEnvironmentValueEx
559 stdcall NtSetSystemInformation(long ptr long) ChpeAutoNtSetSystemInformation
560 stdcall NtSetSystemPowerState(long long long) ChpeAutoNtSetSystemPowerState
561 stdcall NtSetSystemTime(ptr ptr) ChpeAutoNtSetSystemTime
562 stdcall NtSetThreadExecutionState(long ptr) ChpeAutoNtSetThreadExecutionState
564 stdcall NtSetTimerResolution(long long ptr) ChpeAutoNtSetTimerResolution
565 stdcall NtSetUuidSeed(ptr) ChpeAutoNtSetUuidSeed
567 stdcall NtSetVolumeInformationFile(long ptr ptr long long) ChpeAutoNtSetVolumeInformationFile
568 stdcall NtShutdownSystem(long) ChpeAutoNtShutdownSystem
569 stdcall -version=0x600+ NtShutdownWorkerFactory(ptr ptr) ChpeStubNtShutdownWorkerFactory
570 stdcall NtSignalAndWaitForSingleObject(long long long ptr) ChpeAutoNtSignalAndWaitForSingleObject
571 stdcall -version=0x600+ NtSinglePhaseReject(ptr ptr) ChpeAutoNtSinglePhaseReject
572 stdcall NtStartProfile(ptr) ChpeAutoNtStartProfile
573 stdcall NtStopProfile(ptr) ChpeAutoNtStopProfile
574 stdcall -version=0x602+ NtSubscribeWnfStateChange(ptr long long ptr) ChpeAutoNtSubscribeWnfStateChange
577 stdcall NtSystemDebugControl(long ptr long ptr long ptr) ChpeAutoNtSystemDebugControl
578 stdcall NtTerminateJobObject(ptr long) ChpeAutoNtTerminateJobObject
581 stdcall NtTestAlert() ChpeAutoNtTestAlert
582 stdcall -version=0x600+ NtThawRegistry() ChpeStubNtThawRegistry
583 stdcall -version=0x600+ NtThawTransactions() ChpeStubNtThawTransactions
584 stdcall -version=0x600+ NtTraceControl(long ptr long ptr long ptr) ChpeAutoNtTraceControl
585 stdcall NtTraceEvent(ptr long long ptr) ChpeAutoNtTraceEvent
586 stdcall NtTranslateFilePath(ptr long ptr long) ChpeAutoNtTranslateFilePath
587 stdcall NtUnloadDriver(ptr) ChpeAutoNtUnloadDriver
588 stdcall NtUnloadKey2(ptr long) ChpeAutoNtUnloadKey2
589 stdcall NtUnloadKey(long) ChpeAutoNtUnloadKey
590 stdcall NtUnloadKeyEx(ptr ptr) ChpeAutoNtUnloadKeyEx
591 stdcall NtUnlockFile(long ptr ptr ptr ptr) ChpeAutoNtUnlockFile
592 stdcall NtUnlockVirtualMemory(long ptr ptr long) ChpeAutoNtUnlockVirtualMemory
595 stdcall -version=0x602+ NtUnsubscribeWnfStateChange(ptr) ChpeAutoNtUnsubscribeWnfStateChange
596 stdcall -version=0x602+ NtUpdateWnfStateData(ptr ptr long ptr ptr long long) ChpeAutoNtUpdateWnfStateData
597 stdcall NtVdmControl(long ptr) ChpeAutoNtVdmControl
598 stdcall NtWaitForDebugEvent(ptr long ptr ptr) ChpeAutoNtWaitForDebugEvent
599 stdcall NtWaitForKeyedEvent(ptr ptr long ptr) ChpeAutoNtWaitForKeyedEvent
601 stdcall NtWaitForMultipleObjects32(long ptr long long ptr) ChpeAutoNtWaitForMultipleObjects32
602 stdcall NtWaitForMultipleObjects(long ptr long long ptr) ChpeAutoNtWaitForMultipleObjects
603 stdcall NtWaitForSingleObject(long long long) ChpeAutoNtWaitForSingleObject
604 stub -version=0x600+ NtWaitForWorkViaWorkerFactory
605 stdcall NtWaitHighEventPair(ptr) ChpeAutoNtWaitHighEventPair
606 stdcall NtWaitLowEventPair(ptr) ChpeAutoNtWaitLowEventPair
607 stdcall -version=0x600+ NtWorkerFactoryWorkerReady(long) ChpeStubNtWorkerFactoryWorkerReady
609 stdcall NtWriteFileGather(long long ptr ptr ptr ptr long ptr ptr) ChpeAutoNtWriteFileGather
610 stdcall NtWriteRequestData(ptr ptr long ptr long ptr) ChpeAutoNtWriteRequestData
612 stdcall NtYieldExecution() ChpeAutoNtYieldExecution
617 stdcall PfxFindPrefix(ptr ptr) ChpeAutoPfxFindPrefix
618 stdcall PfxInitialize(ptr) ChpeAutoPfxInitialize
619 stdcall PfxInsertPrefix(ptr ptr ptr) ChpeAutoPfxInsertPrefix
620 stdcall PfxRemovePrefix(ptr ptr) ChpeAutoPfxRemovePrefix
621 stdcall RtlAbortRXact(ptr) ChpeAutoRtlAbortRXact
622 stdcall RtlAbsoluteToSelfRelativeSD(ptr ptr ptr) ChpeAutoRtlAbsoluteToSelfRelativeSD
623 stdcall RtlAcquirePebLock() ChpeAutoRtlAcquirePebLock
625 stdcall RtlAcquireResourceExclusive(ptr long) ChpeAutoRtlAcquireResourceExclusive
626 stdcall RtlAcquireResourceShared(ptr long) ChpeAutoRtlAcquireResourceShared
629 stdcall RtlActivateActivationContext(long ptr ptr) ChpeAutoRtlActivateActivationContext
630 stdcall RtlActivateActivationContextEx(long ptr ptr ptr) ChpeAutoRtlActivateActivationContextEx
631 stdcall RtlActivateActivationContextUnsafeFast(ptr ptr) ChpeAutoRtlActivateActivationContextUnsafeFast
632 stdcall RtlAddAccessAllowedAce(ptr long long ptr) ChpeAutoRtlAddAccessAllowedAce
633 stdcall RtlAddAccessAllowedAceEx(ptr long long long ptr) ChpeAutoRtlAddAccessAllowedAceEx
634 stdcall RtlAddAccessAllowedObjectAce(ptr long long long ptr ptr ptr) ChpeAutoRtlAddAccessAllowedObjectAce
635 stdcall RtlAddAccessDeniedAce(ptr long long ptr) ChpeAutoRtlAddAccessDeniedAce
636 stdcall RtlAddAccessDeniedAceEx(ptr long long long ptr) ChpeAutoRtlAddAccessDeniedAceEx
637 stdcall RtlAddAccessDeniedObjectAce(ptr long long long ptr ptr ptr) ChpeAutoRtlAddAccessDeniedObjectAce
638 stdcall RtlAddAce(ptr long long ptr long) ChpeAutoRtlAddAce
639 stdcall RtlAddActionToRXact(ptr long ptr long ptr long) ChpeAutoRtlAddActionToRXact
640 stdcall RtlAddAtomToAtomTable(ptr wstr ptr) ChpeAutoRtlAddAtomToAtomTable
641 stdcall RtlAddAttributeActionToRXact(ptr long ptr ptr ptr long ptr long) ChpeAutoRtlAddAttributeActionToRXact
642 stdcall RtlAddAuditAccessAce(ptr long long ptr long long) ChpeAutoRtlAddAuditAccessAce
643 stdcall RtlAddAuditAccessAceEx(ptr long long long ptr long long) ChpeAutoRtlAddAuditAccessAceEx
644 stdcall RtlAddAuditAccessObjectAce(ptr long long long ptr ptr ptr long long) ChpeAutoRtlAddAuditAccessObjectAce
645 stdcall  RtlAddCompoundAce(ptr long long long ptr ptr) ChpeStubRtlAddCompoundAce
648 stdcall -version=0x600+ RtlAddMandatoryAce(ptr long long long long ptr) ChpeAutoRtlAddMandatoryAce
649 stdcall RtlAddRefActivationContext(ptr) ChpeAutoRtlAddRefActivationContext
650 stdcall RtlAddRefMemoryStream(ptr) ChpeAutoRtlAddRefMemoryStream
651 stdcall -version=0x600+ RtlAddSIDToBoundaryDescriptor(ptr ptr) ChpeAutoRtlAddSIDToBoundaryDescriptor
652 stdcall -version=0x600+ RtlAddIntegrityLabelToBoundaryDescriptor(ptr ptr) ChpeAutoRtlAddIntegrityLabelToBoundaryDescriptor
655 stdcall RtlAddressInSectionTable(ptr ptr long) ChpeAutoRtlAddressInSectionTable
656 stdcall RtlAdjustPrivilege(long long long ptr) ChpeAutoRtlAdjustPrivilege
657 stdcall RtlAllocateActivationContextStack(ptr) ChpeAutoRtlAllocateActivationContextStack
658 stdcall RtlAllocateAndInitializeSid(ptr long long long long long long long long long ptr) ChpeAutoRtlAllocateAndInitializeSid
659 stdcall RtlAllocateHandle(ptr ptr) ChpeAutoRtlAllocateHandle
661 stdcall -version=0x600+ RtlAllocateMemoryBlockLookaside(ptr long ptr) ChpeStubRtlAllocateMemoryBlockLookaside
662 stdcall -version=0x600+ RtlAllocateMemoryZone(long long ptr) ChpeStubRtlAllocateMemoryZone
663 stdcall RtlAnsiCharToUnicodeChar(ptr) ChpeAutoRtlAnsiCharToUnicodeChar
664 stdcall RtlAnsiStringToUnicodeSize(ptr) ChpeAutoRtlAnsiStringToUnicodeSize
665 stdcall RtlAnsiStringToUnicodeString(ptr ptr long) ChpeAutoRtlAnsiStringToUnicodeString
666 stdcall RtlAppendAsciizToString(ptr str) ChpeAutoRtlAppendAsciizToString
667 stdcall  RtlAppendPathElement(ptr ptr ptr) ChpeStubRtlAppendPathElement
668 stdcall RtlAppendStringToString(ptr ptr) ChpeAutoRtlAppendStringToString
669 stdcall RtlAppendUnicodeStringToString(ptr ptr) ChpeAutoRtlAppendUnicodeStringToString
670 stdcall RtlAppendUnicodeToString(ptr wstr) ChpeAutoRtlAppendUnicodeToString
671 stdcall RtlApplicationVerifierStop(ptr str ptr str ptr str ptr str ptr str) ChpeAutoRtlApplicationVerifierStop
672 stdcall RtlApplyRXact(ptr) ChpeAutoRtlApplyRXact
673 stdcall RtlApplyRXactNoFlush(ptr) ChpeAutoRtlApplyRXactNoFlush
674 stdcall RtlAreAllAccessesGranted(long long) ChpeAutoRtlAreAllAccessesGranted
675 stdcall RtlAreAnyAccessesGranted(long long) ChpeAutoRtlAreAnyAccessesGranted
676 stdcall RtlAreBitsClear(ptr long long) ChpeAutoRtlAreBitsClear
677 stdcall RtlAreBitsSet(ptr long long) ChpeAutoRtlAreBitsSet
678 stdcall RtlAssert(ptr ptr long ptr) ChpeAutoRtlAssert
679 stdcall RtlBarrier(ptr long) ChpeAutoRtlBarrier
680 stdcall -version=0x600+ RtlBarrierForDelete(long long) ChpeStubRtlBarrierForDelete
681 stdcall RtlCancelTimer(ptr ptr) ChpeAutoRtlCancelTimer
684 stdcall RtlCharToInteger(ptr long ptr) ChpeAutoRtlCharToInteger
685 stdcall RtlCheckForOrphanedCriticalSections(ptr) ChpeAutoRtlCheckForOrphanedCriticalSections
686 stdcall -version=0x602+ RtlCheckPortableOperatingSystem(ptr) ChpeAutoRtlCheckPortableOperatingSystem
687 stdcall RtlCheckRegistryKey(long ptr) ChpeAutoRtlCheckRegistryKey
688 stdcall -version=0x600+ RtlCleanUpTEBLangLists() ChpeStubRtlCleanUpTEBLangLists
689 stdcall RtlClearAllBits(ptr) ChpeAutoRtlClearAllBits
690 stdcall RtlClearBits(ptr long long) ChpeAutoRtlClearBits
691 stdcall RtlCloneMemoryStream(ptr ptr) ChpeAutoRtlCloneMemoryStream
692 stdcall -version=0x600+ RtlCloneUserProcess(long long long long long) ChpeStubRtlCloneUserProcess
693 stdcall -version=0x600+ RtlCmDecodeMemIoResource(ptr ptr) ChpeAutoRtlCmDecodeMemIoResource
694 stdcall -version=0x600+ RtlCmEncodeMemIoResource(ptr long int64 int64) ChpeAutoRtlCmEncodeMemIoResource
695 stdcall -version=0x600+ RtlCommitDebugInfo(ptr long) ChpeStubRtlCommitDebugInfo
696 stdcall RtlCommitMemoryStream(ptr long) ChpeAutoRtlCommitMemoryStream
697 stdcall RtlCompactHeap(long long) ChpeAutoRtlCompactHeap
698 stdcall -version=0x600+ RtlCompareAltitudes(ptr ptr) ChpeAutoRtlCompareAltitudes
700 stdcall RtlCompareMemoryUlong(ptr long long) ChpeAutoRtlCompareMemoryUlong
701 stdcall RtlCompareString(ptr ptr long) ChpeAutoRtlCompareString
703 stdcall -version=0x600+ RtlCompareUnicodeStrings(wstr long wstr long long) ChpeAutoRtlCompareUnicodeStrings
704 stdcall -version=0x600+ RtlCompleteProcessCloning(long) ChpeStubRtlCompleteProcessCloning
705 stdcall RtlCompressBuffer(long ptr long ptr long long ptr ptr) ChpeAutoRtlCompressBuffer
706 stdcall RtlComputeCrc32(long ptr long) ChpeAutoRtlComputeCrc32
707 stdcall RtlComputeImportTableHash(ptr ptr long) ChpeAutoRtlComputeImportTableHash
708 stdcall RtlComputePrivatizedDllName_U(ptr ptr ptr) ChpeAutoRtlComputePrivatizedDllName_U
709 stdcall -version=0x600+ RtlConnectToSm(ptr ptr long ptr) ChpeStubRtlConnectToSm
710 stdcall RtlConsoleMultiByteToUnicodeN(ptr long ptr ptr long ptr) ChpeAutoRtlConsoleMultiByteToUnicodeN
711 stdcall -version=0xA00+ RtlConvertDeviceFamilyInfoToString(ptr ptr ptr ptr) ChpeAutoRtlConvertDeviceFamilyInfoToString
712 stdcall RtlConvertExclusiveToShared(ptr) ChpeAutoRtlConvertExclusiveToShared
713 stdcall -version=0x600+ RtlConvertLCIDToString(long long long ptr long) ChpeAutoRtlConvertLCIDToString
714 stdcall RtlConvertSharedToExclusive(ptr) ChpeAutoRtlConvertSharedToExclusive
715 stdcall RtlConvertSidToUnicodeString(ptr ptr long) ChpeAutoRtlConvertSidToUnicodeString
716 stdcall RtlConvertToAutoInheritSecurityObject(ptr ptr ptr ptr long ptr) ChpeAutoRtlConvertToAutoInheritSecurityObject
717 stdcall RtlConvertUiListToApiList(ptr ptr long) ChpeAutoRtlConvertUiListToApiList
718 stdcall RtlCopyLuid(ptr ptr) ChpeAutoRtlCopyLuid
719 stdcall RtlCopyLuidAndAttributesArray(long ptr ptr) ChpeAutoRtlCopyLuidAndAttributesArray
720 stdcall RtlCopyMappedMemory(ptr ptr long) ChpeAutoRtlCopyMappedMemory
721 stdcall -version=0xA00+ RtlCopyContext(ptr long ptr) ChpeAutoRtlCopyContext
722 stdcall -version=0x600+ RtlCopyExtendedContext(ptr long ptr) ChpeAutoRtlCopyExtendedContext
723 cdecl -version=0x600+ RtlCopyMemory(ptr ptr long) ChpeAutoRtlCopyMemory
725 stdcall RtlCopyMemoryStreamTo(ptr ptr int64 ptr ptr) ChpeAutoRtlCopyMemoryStreamTo
726 stdcall RtlCopyOutOfProcessMemoryStreamTo(ptr ptr int64 ptr ptr) ChpeAutoRtlCopyOutOfProcessMemoryStreamTo
727 stdcall RtlCopySecurityDescriptor(ptr ptr) ChpeAutoRtlCopySecurityDescriptor
728 stdcall RtlCopySid(long ptr ptr) ChpeAutoRtlCopySid
729 stdcall RtlCopySidAndAttributesArray(long ptr long ptr ptr ptr ptr) ChpeAutoRtlCopySidAndAttributesArray
730 stdcall RtlCopyString(ptr ptr) ChpeAutoRtlCopyString
732 stdcall RtlCreateAcl(ptr long long) ChpeAutoRtlCreateAcl
733 stdcall RtlCreateActivationContext(long ptr long ptr ptr ptr) ChpeAutoRtlCreateActivationContext
734 stdcall RtlCreateAndSetSD(ptr long ptr ptr ptr) ChpeAutoRtlCreateAndSetSD
735 stdcall RtlContractHashTable(ptr) ChpeAutoRtlContractHashTable
736 stdcall RtlCreateAtomTable(long ptr) ChpeAutoRtlCreateAtomTable
737 stdcall RtlCreateHashTable(ptr long long) ChpeAutoRtlCreateHashTable
738 stdcall RtlCreateBootStatusDataFile() ChpeAutoRtlCreateBootStatusDataFile
739 stdcall -version=0x600+ RtlCreateBoundaryDescriptor(ptr long) ChpeAutoRtlCreateBoundaryDescriptor
740 stdcall RtlCreateEnvironment(long ptr) ChpeAutoRtlCreateEnvironment
741 stdcall -version=0x600+ RtlCreateEnvironmentEx(ptr ptr long) ChpeStubRtlCreateEnvironmentEx
742 stdcall RtlCreateHeap(long ptr long long ptr ptr) ChpeAutoRtlCreateHeap
743 stdcall -version=0x600+ RtlCreateMemoryBlockLookaside(ptr long long long long) ChpeStubRtlCreateMemoryBlockLookaside
744 stdcall -version=0x600+ RtlCreateMemoryZone(ptr long long) ChpeStubRtlCreateMemoryZone
745 stdcall RtlCreateProcessParameters(ptr ptr ptr ptr ptr ptr ptr ptr ptr ptr) ChpeAutoRtlCreateProcessParameters
746 stdcall -version=0x600+ RtlCreateProcessParametersEx(ptr wstr ptr wstr wstr ptr wstr wstr ptr ptr long) ChpeAutoRtlCreateProcessParametersEx
747 stdcall RtlCreateQueryDebugBuffer(long long) ChpeAutoRtlCreateQueryDebugBuffer
748 stdcall RtlCreateRegistryKey(long wstr) ChpeAutoRtlCreateRegistryKey
749 stdcall RtlCreateSecurityDescriptor(ptr long) ChpeAutoRtlCreateSecurityDescriptor
750 stdcall RtlCreateServiceSid(ptr ptr ptr) ChpeAutoRtlCreateServiceSid
751 stdcall -version=0xA00+ RtlDeriveCapabilitySidsFromName(ptr ptr ptr) ChpeAutoRtlDeriveCapabilitySidsFromName
752 stdcall -version=0x602+ RtlIsCapabilitySid(ptr) ChpeAutoRtlIsCapabilitySid
753 stdcall -version=0x602+ RtlGetAppContainerSidType(ptr ptr) ChpeAutoRtlGetAppContainerSidType
754 stdcall -version=0x602+ RtlGetAppContainerParent(ptr ptr) ChpeAutoRtlGetAppContainerParent
755 stdcall -version=0x602+ RtlIsParentOfChildAppContainer(ptr ptr) ChpeAutoRtlIsParentOfChildAppContainer
756 stdcall -version=0x602+ RtlCheckTokenMembershipEx(ptr ptr long ptr) ChpeAutoRtlCheckTokenMembershipEx
757 stdcall RtlCreateSystemVolumeInformationFolder(ptr) ChpeAutoRtlCreateSystemVolumeInformationFolder
758 stdcall RtlCreateTagHeap(ptr long wstr wstr) ChpeAutoRtlCreateTagHeap
759 stdcall RtlCreateTimer(ptr ptr ptr ptr long long long) ChpeAutoRtlCreateTimer
760 stdcall RtlCreateTimerQueue(ptr) ChpeAutoRtlCreateTimerQueue
761 stdcall RtlCreateUnicodeString(ptr wstr) ChpeAutoRtlCreateUnicodeString
762 stdcall RtlCreateUnicodeStringFromAsciiz(ptr str) ChpeAutoRtlCreateUnicodeStringFromAsciiz
763 stdcall RtlCreateUserProcess(ptr long ptr ptr ptr ptr long ptr ptr ptr) ChpeAutoRtlCreateUserProcess
764 stdcall RtlCreateUserSecurityObject(ptr long ptr ptr long ptr ptr) ChpeAutoRtlCreateUserSecurityObject
765 stdcall -version=0x600+ RtlCreateUserStack(long long long long long ptr) ChpeAutoRtlCreateUserStack
767 stdcall -version=0x600+ RtlCultureNameToLCID(ptr ptr) ChpeAutoRtlCultureNameToLCID
768 stdcall RtlCustomCPToUnicodeN(ptr wstr long ptr str long) ChpeAutoRtlCustomCPToUnicodeN
769 stdcall RtlCutoverTimeToSystemTime(ptr ptr ptr long) ChpeAutoRtlCutoverTimeToSystemTime
770 stdcall -version=0x600+ RtlDeCommitDebugInfo(long long long) ChpeStubRtlDeCommitDebugInfo
771 stdcall RtlDeNormalizeProcessParams(ptr) ChpeAutoRtlDeNormalizeProcessParams
772 stdcall RtlDeactivateActivationContext(long long) ChpeAutoRtlDeactivateActivationContext
773 stdcall RtlDeactivateActivationContextUnsafeFast(ptr) ChpeAutoRtlDeactivateActivationContextUnsafeFast
774 stdcall  RtlDebugPrintTimes() ChpeStubRtlDebugPrintTimes
777 stdcall RtlDecompressBuffer(long ptr long ptr long ptr) ChpeAutoRtlDecompressBuffer
779 stdcall RtlDefaultNpAcl(ptr) ChpeAutoRtlDefaultNpAcl
780 stdcall RtlDelete(ptr) ChpeAutoRtlDelete
781 stdcall RtlDeleteAce(ptr long) ChpeAutoRtlDeleteAce
782 stdcall RtlDeleteAtomFromAtomTable(ptr long) ChpeAutoRtlDeleteAtomFromAtomTable
783 stdcall RtlDeleteHashTable(ptr) ChpeAutoRtlDeleteHashTable
784 stdcall RtlDeleteBarrier(ptr) ChpeAutoRtlDeleteBarrier
785 stdcall -version=0x600+ RtlDeleteBoundaryDescriptor(ptr) ChpeAutoRtlDeleteBoundaryDescriptor
787 stdcall RtlDeleteElementGenericTable(ptr ptr) ChpeAutoRtlDeleteElementGenericTable
788 stdcall RtlDeleteElementGenericTableAvl(ptr ptr) ChpeAutoRtlDeleteElementGenericTableAvl
791 stdcall RtlDeleteNoSplay(ptr ptr) ChpeAutoRtlDeleteNoSplay
792 stdcall RtlDeleteRegistryValue(long ptr ptr) ChpeAutoRtlDeleteRegistryValue
793 stdcall RtlDeleteResource(ptr) ChpeAutoRtlDeleteResource
794 stdcall RtlDeleteSecurityObject(ptr) ChpeAutoRtlDeleteSecurityObject
795 stdcall RtlDeleteTimer(ptr ptr ptr) ChpeAutoRtlDeleteTimer
796 stdcall RtlDeleteTimerQueue(ptr) ChpeAutoRtlDeleteTimerQueue
797 stdcall RtlDeleteTimerQueueEx(ptr ptr) ChpeAutoRtlDeleteTimerQueueEx
798 stdcall RtlDeregisterSecureMemoryCacheCallback(ptr) ChpeAutoRtlDeregisterSecureMemoryCacheCallback
799 stdcall RtlDeregisterWait(ptr) ChpeAutoRtlDeregisterWait
800 stdcall RtlDeregisterWaitEx(ptr ptr) ChpeAutoRtlDeregisterWaitEx
801 stdcall RtlDestroyAtomTable(ptr) ChpeAutoRtlDestroyAtomTable
802 stdcall RtlDestroyEnvironment(ptr) ChpeAutoRtlDestroyEnvironment
803 stdcall RtlDestroyHandleTable(ptr) ChpeAutoRtlDestroyHandleTable
804 stdcall RtlDestroyHeap(long) ChpeAutoRtlDestroyHeap
805 stdcall -version=0x600+ RtlDestroyMemoryBlockLookaside(long) ChpeStubRtlDestroyMemoryBlockLookaside
806 stdcall -version=0x600+ RtlDestroyMemoryZone(long) ChpeStubRtlDestroyMemoryZone
807 stdcall RtlDestroyProcessParameters(ptr) ChpeAutoRtlDestroyProcessParameters
808 stdcall RtlDestroyQueryDebugBuffer(ptr) ChpeAutoRtlDestroyQueryDebugBuffer
809 stdcall RtlDetermineDosPathNameType_U(wstr) ChpeAutoRtlDetermineDosPathNameType_U
810 stdcall RtlDllShutdownInProgress() ChpeAutoRtlDllShutdownInProgress
811 stdcall RtlDnsHostNameToComputerName(ptr ptr long) ChpeAutoRtlDnsHostNameToComputerName
812 stdcall RtlDoesFileExists_U(wstr) ChpeAutoRtlDoesFileExists_U
813 stdcall RtlDosApplyFileIsolationRedirection_Ustr(long ptr ptr ptr ptr ptr ptr ptr ptr) ChpeAutoRtlDosApplyFileIsolationRedirection_Ustr
814 stdcall RtlDosPathNameToNtPathName_U(wstr ptr ptr ptr) ChpeAutoRtlDosPathNameToNtPathName_U
815 stdcall RtlDosPathNameToNtPathName_U_WithStatus(wstr ptr ptr ptr) ChpeAutoRtlDosPathNameToNtPathName_U_WithStatus
816 stdcall RtlDosPathNameToRelativeNtPathName_U(ptr ptr ptr ptr) ChpeAutoRtlDosPathNameToRelativeNtPathName_U
817 stdcall RtlDosPathNameToRelativeNtPathName_U_WithStatus(wstr ptr ptr ptr) ChpeAutoRtlDosPathNameToRelativeNtPathName_U_WithStatus
818 stdcall RtlDosSearchPath_U(wstr wstr wstr long ptr ptr) ChpeAutoRtlDosSearchPath_U
819 stdcall RtlDosSearchPath_Ustr(long ptr ptr ptr ptr ptr ptr ptr ptr) ChpeAutoRtlDosSearchPath_Ustr
820 stdcall RtlDowncaseUnicodeChar(long) ChpeAutoRtlDowncaseUnicodeChar
821 stdcall RtlDowncaseUnicodeString(ptr ptr long) ChpeAutoRtlDowncaseUnicodeString
822 stdcall RtlDumpResource(ptr) ChpeAutoRtlDumpResource
823 stdcall RtlDuplicateUnicodeString(long ptr ptr) ChpeAutoRtlDuplicateUnicodeString
824 stdcall RtlEmptyAtomTable(ptr long) ChpeAutoRtlEmptyAtomTable
825 stdcall RtlEndEnumerationHashTable(ptr ptr) ChpeAutoRtlEndEnumerationHashTable
826 stdcall RtlEndWeakEnumerationHashTable(ptr ptr) ChpeAutoRtlEndWeakEnumerationHashTable
827 stdcall  RtlEnableEarlyCriticalSectionEventCreation() ChpeStubRtlEnableEarlyCriticalSectionEventCreation
831 stdcall RtlEnumProcessHeaps(ptr ptr) ChpeAutoRtlEnumProcessHeaps
832 stdcall RtlEnumerateEntryHashTable(ptr ptr) ChpeAutoRtlEnumerateEntryHashTable
833 stdcall RtlEnumerateGenericTable(ptr long) ChpeAutoRtlEnumerateGenericTable
834 stdcall RtlEnumerateGenericTableAvl(ptr long) ChpeAutoRtlEnumerateGenericTableAvl
835 stdcall RtlEnumerateGenericTableLikeADirectory(ptr ptr ptr long ptr ptr ptr) ChpeAutoRtlEnumerateGenericTableLikeADirectory
836 stdcall RtlEnumerateGenericTableWithoutSplaying(ptr ptr) ChpeAutoRtlEnumerateGenericTableWithoutSplaying
837 stdcall RtlEnumerateGenericTableWithoutSplayingAvl(ptr ptr) ChpeAutoRtlEnumerateGenericTableWithoutSplayingAvl
838 stdcall RtlEqualComputerName(ptr ptr) ChpeAutoRtlEqualComputerName
839 stdcall RtlEqualDomainName(ptr ptr) ChpeAutoRtlEqualDomainName
840 stdcall RtlEqualLuid(ptr ptr) ChpeAutoRtlEqualLuid
841 stdcall RtlExpandHashTable(ptr) ChpeAutoRtlExpandHashTable
842 stdcall RtlEqualPrefixSid(ptr ptr) ChpeAutoRtlEqualPrefixSid
843 stdcall RtlEqualSid(long long) ChpeAutoRtlEqualSid
844 stdcall RtlEqualString(ptr ptr long) ChpeAutoRtlEqualString
845 stdcall RtlEqualUnicodeString(ptr ptr long) ChpeAutoRtlEqualUnicodeString
846 stdcall RtlEraseUnicodeString(ptr) ChpeAutoRtlEraseUnicodeString
849 stdcall RtlWow64GetCurrentCpuArea(ptr ptr ptr) ChpeAutoRtlWow64GetCurrentCpuArea
851 stdcall ChpeMarkEcCodeRange(ptr long) ChpeAutoChpeMarkEcCodeRange
852 stdcall ChpeCanContinueToGuest() ChpeAutoChpeCanContinueToGuest
853 stdcall ChpeContinueToGuest(ptr) ChpeAutoChpeContinueToGuest
854 stdcall ChpeContinueToGuestEx(ptr long) ChpeAutoChpeContinueToGuestEx
855 stdcall ProcessPendingCrossProcessEmulatorWork() ChpeAutoProcessPendingCrossProcessEmulatorWork
856 stdcall ChpeIsProcessorFeaturePresent(long) ChpeAutoChpeIsProcessorFeaturePresent
857 stdcall RtlWow64PopAllCrossProcessWorkFromWorkList(ptr ptr) ChpeAutoRtlWow64PopAllCrossProcessWorkFromWorkList
858 stdcall RtlWow64PushCrossProcessWorkOntoFreeList(ptr ptr) ChpeAutoRtlWow64PushCrossProcessWorkOntoFreeList
861 stdcall -version=0x600+ RtlLocateLegacyContext(ptr ptr) ChpeAutoRtlLocateLegacyContext
864 stdcall -version=0x601+ RtlQueryUnbiasedInterruptTime(ptr) ChpeAutoRtlQueryUnbiasedInterruptTime
867 stdcall RtlExitUserProcess(long) ChpeAutoRtlExitUserProcess
869 stdcall -version=0x600+ RtlExpandEnvironmentStrings(long ptr long ptr long ptr) ChpeAutoRtlExpandEnvironmentStrings
870 stdcall RtlExpandEnvironmentStrings_U(ptr ptr ptr ptr) ChpeAutoRtlExpandEnvironmentStrings_U
871 stdcall -version=0x600+ RtlExtendMemoryBlockLookaside(long) ChpeStubRtlExtendMemoryBlockLookaside
872 stdcall -version=0x600+ RtlExtendMemoryZone(long long) ChpeStubRtlExtendMemoryZone
874 stdcall RtlFillMemoryUlong(ptr long long) ChpeAutoRtlFillMemoryUlong
875 stdcall RtlFinalReleaseOutOfProcessMemoryStream(ptr) ChpeAutoRtlFinalReleaseOutOfProcessMemoryStream
876 stdcall -version=0x600+ RtlFindAceByType(long long ptr) ChpeStubRtlFindAceByType
877 stdcall RtlFindActivationContextSectionGuid(long ptr long ptr ptr) ChpeAutoRtlFindActivationContextSectionGuid
878 stdcall RtlFindActivationContextSectionString(long ptr long ptr ptr) ChpeAutoRtlFindActivationContextSectionString
879 stdcall RtlFindCharInUnicodeString(long ptr ptr ptr) ChpeAutoRtlFindCharInUnicodeString
880 stdcall RtlFindClearBits(ptr long long) ChpeAutoRtlFindClearBits
881 stdcall RtlFindClearBitsAndSet(ptr long long) ChpeAutoRtlFindClearBitsAndSet
882 stdcall RtlFindClearRuns(ptr ptr long long) ChpeAutoRtlFindClearRuns
883 stdcall -version=0x600+ RtlFindClosestEncodableLength(long ptr) ChpeStubRtlFindClosestEncodableLength
885 stdcall RtlFindLastBackwardRunClear(ptr long ptr) ChpeAutoRtlFindLastBackwardRunClear
886 stdcall RtlFindLeastSignificantBit(double) ChpeAutoRtlFindLeastSignificantBit
887 stdcall RtlFindLongestRunClear(ptr long) ChpeAutoRtlFindLongestRunClear
888 stdcall RtlFindMessage(long long long long ptr) ChpeAutoRtlFindMessage
889 stdcall RtlFindMostSignificantBit(double) ChpeAutoRtlFindMostSignificantBit
890 stdcall RtlFindNextForwardRunClear(ptr long ptr) ChpeAutoRtlFindNextForwardRunClear
891 stdcall RtlFindSetBits(ptr long long) ChpeAutoRtlFindSetBits
892 stdcall RtlFindSetBitsAndClear(ptr long long) ChpeAutoRtlFindSetBitsAndClear
893 stdcall RtlFirstEntrySList(ptr) ChpeAutoRtlFirstEntrySList
894 stdcall RtlFirstFreeAce(ptr ptr) ChpeAutoRtlFirstFreeAce
899 stdcall RtlFlushSecureMemoryCache(ptr ptr) ChpeAutoRtlFlushSecureMemoryCache
901 stdcall RtlFormatMessage(ptr long long long long ptr ptr long ptr) ChpeAutoRtlFormatMessage
902 stdcall RtlFormatMessageEx(ptr long long long long ptr ptr long ptr long) ChpeAutoRtlFormatMessageEx
903 stdcall RtlFreeActivationContextStack(ptr) ChpeAutoRtlFreeActivationContextStack
904 stdcall RtlFreeAnsiString(long) ChpeAutoRtlFreeAnsiString
905 stdcall RtlFreeHandle(ptr ptr) ChpeAutoRtlFreeHandle
907 stdcall -version=0x600+ RtlFreeMemoryBlockLookaside(long long) ChpeStubRtlFreeMemoryBlockLookaside
908 stdcall RtlFreeOemString(ptr) ChpeAutoRtlFreeOemString
909 stdcall RtlFreeSid(long) ChpeAutoRtlFreeSid
910 stdcall RtlFreeThreadActivationContextStack() ChpeAutoRtlFreeThreadActivationContextStack
912 stdcall -version=0x600+ RtlFreeUserStack(ptr) ChpeAutoRtlFreeUserStack
913 stdcall RtlGUIDFromString(ptr ptr) ChpeAutoRtlGUIDFromString
915 stdcall RtlGetAce(ptr long ptr) ChpeAutoRtlGetAce
916 stdcall RtlGetActiveActivationContext(ptr) ChpeAutoRtlGetActiveActivationContext
917 stdcall RtlGetCallersAddress(ptr ptr) ChpeAutoRtlGetCallersAddress
918 stdcall RtlGetCompressionWorkSpaceSize(long ptr ptr) ChpeAutoRtlGetCompressionWorkSpaceSize
919 stdcall RtlGetControlSecurityDescriptor(ptr ptr ptr) ChpeAutoRtlGetControlSecurityDescriptor
920 stdcall RtlGetCriticalSectionRecursionCount(ptr) ChpeAutoRtlGetCriticalSectionRecursionCount
921 stdcall RtlGetCurrentDirectory_U(long ptr) ChpeAutoRtlGetCurrentDirectory_U
925 stdcall -version=0x600+ RtlGetCurrentTransaction() ChpeAutoRtlGetCurrentTransaction
926 stdcall RtlGetDaclSecurityDescriptor(ptr ptr ptr ptr) ChpeAutoRtlGetDaclSecurityDescriptor
927 stdcall -version=0xA00+ RtlGetDeviceFamilyInfoEnum(ptr ptr ptr) ChpeAutoRtlGetDeviceFamilyInfoEnum
929 stdcall RtlGetElementGenericTable(ptr long) ChpeAutoRtlGetElementGenericTable
930 stdcall RtlGetElementGenericTableAvl(ptr long) ChpeAutoRtlGetElementGenericTableAvl
931 stdcall -version=0x600+ RtlGetExtendedContextLength(long ptr) ChpeAutoRtlGetExtendedContextLength
932 stdcall -version=0x600+ RtlGetExePath(wstr ptr) ChpeAutoRtlGetExePath
933 stdcall -version=0x600+ RtlGetExtendedContextLength2(long ptr int64) ChpeAutoRtlGetExtendedContextLength2
934 stdcall -version=0x600+ -ret64 RtlGetExtendedFeaturesMask(ptr) ChpeAutoRtlGetExtendedFeaturesMask
935 stdcall -version=0x600+ RtlGetFileMUIPath(long long ptr ptr long long ptr) ChpeStubRtlGetFileMUIPath
936 stdcall RtlGetFrame() ChpeAutoRtlGetFrame
937 stdcall RtlGetFullPathName_U(wstr long ptr ptr) ChpeAutoRtlGetFullPathName_U
938 stdcall RtlGetFullPathName_UstrEx(ptr ptr ptr ptr ptr ptr ptr ptr) ChpeAutoRtlGetFullPathName_UstrEx
940 stdcall RtlGetGroupSecurityDescriptor(ptr ptr ptr) ChpeAutoRtlGetGroupSecurityDescriptor
941 stdcall -version=0x600+ RtlGetIntegerAtom(wstr ptr) ChpeAutoRtlGetIntegerAtom
944 stdcall -version=0x600+ RtlGetLocaleFileMappingAddress(ptr ptr ptr) ChpeAutoRtlGetLocaleFileMappingAddress
946 stdcall RtlGetLengthWithoutTrailingPathSeperators(long ptr ptr) ChpeAutoRtlGetLengthWithoutTrailingPathSeperators
947 stdcall RtlGetLongestNtPathLength() ChpeAutoRtlGetLongestNtPathLength
949 stdcall RtlGetNtGlobalFlags() ChpeAutoRtlGetNtGlobalFlags
950 stdcall RtlGetNtProductType(ptr) ChpeAutoRtlGetNtProductType
951 stdcall RtlGetNtVersionNumbers(ptr ptr ptr) ChpeAutoRtlGetNtVersionNumbers
952 stdcall RtlGetOwnerSecurityDescriptor(ptr ptr ptr) ChpeAutoRtlGetOwnerSecurityDescriptor
953 stdcall -version=0x600+ RtlGetParentLocaleName(wstr long long long) ChpeStubRtlGetParentLocaleName
954 stdcall -version=0x600+ RtlGetProcessPreferredUILanguages(long ptr ptr ptr) ChpeAutoRtlGetProcessPreferredUILanguages
957 stdcall RtlGetSaclSecurityDescriptor(ptr ptr ptr ptr) ChpeAutoRtlGetSaclSecurityDescriptor
958 stdcall -version=0x600+ RtlGetSearchPath(ptr) ChpeAutoRtlGetSearchPath
959 stdcall RtlGetSecurityDescriptorRMControl(ptr ptr) ChpeAutoRtlGetSecurityDescriptorRMControl
960 stdcall RtlGetSetBootStatusData(ptr long long ptr long long) ChpeAutoRtlGetSetBootStatusData
961 stdcall -version=0x600+ RtlGetSystemPreferredUILanguages(long long ptr ptr ptr) ChpeAutoRtlGetSystemPreferredUILanguages
962 stdcall RtlGetThreadErrorMode() ChpeAutoRtlGetThreadErrorMode
963 stdcall -version=0x600+ RtlGetThreadLangIdByIndex(long long ptr ptr) ChpeStubRtlGetThreadLangIdByIndex
964 stdcall -version=0x600+ RtlGetThreadPreferredUILanguages(long ptr ptr ptr) ChpeAutoRtlGetThreadPreferredUILanguages
965 stdcall -version=0x600+ RtlGetUILanguageInfo(long ptr long ptr ptr) ChpeStubRtlGetUILanguageInfo
967 stdcall -version=0x600+ RtlGetUnloadEventTraceEx(ptr ptr ptr) ChpeAutoRtlGetUnloadEventTraceEx
968 stdcall RtlGetUserInfoHeap(ptr long ptr ptr ptr) ChpeAutoRtlGetUserInfoHeap
969 stdcall -version=0x600+ RtlGetUserPreferredUILanguages(long long ptr ptr ptr) ChpeAutoRtlGetUserPreferredUILanguages
971 stdcall RtlHashUnicodeString(ptr long long ptr) ChpeAutoRtlHashUnicodeString
972 stdcall -version=0x600+ RtlHeapTrkInitialize(ptr) ChpeStubRtlHeapTrkInitialize
973 stdcall RtlIdentifierAuthoritySid(ptr) ChpeAutoRtlIdentifierAuthoritySid
974 stdcall -version=0x600+ RtlIdnToAscii(long wstr long ptr ptr) ChpeAutoRtlIdnToAscii
975 stdcall -version=0x600+ RtlIdnToNameprepUnicode(long wstr long ptr ptr) ChpeAutoRtlIdnToNameprepUnicode
976 stdcall -version=0x600+ RtlIdnToUnicode(long wstr long ptr ptr) ChpeAutoRtlIdnToUnicode
977 stdcall RtlImageDirectoryEntryToData(ptr long long ptr) ChpeAutoRtlImageDirectoryEntryToData
978 stdcall RtlImageNtHeader(long) ChpeAutoRtlImageNtHeader
979 stdcall RtlImageNtHeaderEx(long ptr double ptr) ChpeAutoRtlImageNtHeaderEx
980 stdcall RtlImageRvaToSection(ptr long long) ChpeAutoRtlImageRvaToSection
981 stdcall RtlImageRvaToVa(ptr long long ptr) ChpeAutoRtlImageRvaToVa
982 stdcall RtlImpersonateSelf(long) ChpeAutoRtlImpersonateSelf
983 stdcall -version=0x600+ RtlImpersonateSelfEx(long long ptr) ChpeStubRtlImpersonateSelfEx
984 stdcall RtlInitAnsiString(ptr str) ChpeAutoRtlInitAnsiString
985 stdcall RtlInitAnsiStringEx(ptr str) ChpeAutoRtlInitAnsiStringEx
986 stdcall -version=0xA00+ RtlInitUTF8String(ptr str) ChpeAutoRtlInitUTF8String
987 stdcall RtlInitBarrier(ptr long long) ChpeAutoRtlInitBarrier
988 stdcall RtlInitCodePageTable(ptr ptr) ChpeAutoRtlInitCodePageTable
990 stdcall RtlInitNlsTables(ptr ptr ptr ptr) ChpeAutoRtlInitNlsTables
992 stdcall RtlInitString(ptr str) ChpeAutoRtlInitString
993 stdcall RtlGetNextEntryHashTable(ptr ptr) ChpeAutoRtlGetNextEntryHashTable
994 stdcall RtlInitEnumerationHashTable(ptr ptr) ChpeAutoRtlInitEnumerationHashTable
996 stdcall RtlInitWeakEnumerationHashTable(ptr ptr) ChpeAutoRtlInitWeakEnumerationHashTable
997 stdcall RtlInitUnicodeStringEx(ptr wstr) ChpeAutoRtlInitUnicodeStringEx
998 stdcall  RtlInitializeAtomPackage(ptr) ChpeStubRtlInitializeAtomPackage
999 stdcall RtlInitializeBitMap(ptr long long) ChpeAutoRtlInitializeBitMap
1001 stdcall RtlInitializeContext(ptr ptr ptr ptr ptr) ChpeAutoRtlInitializeContext
1003 stdcall RtlInitializeCriticalSectionAndSpinCount(ptr long) ChpeAutoRtlInitializeCriticalSectionAndSpinCount
1004 stdcall -version=0x600+ RtlInitializeCriticalSectionEx(ptr long long) ChpeAutoRtlInitializeCriticalSectionEx
1005 stdcall -version=0x600+ RtlInitializeExtendedContext(ptr long ptr) ChpeAutoRtlInitializeExtendedContext
1006 stdcall -version=0x600+ RtlInitializeExtendedContext2(ptr long ptr int64) ChpeAutoRtlInitializeExtendedContext2
1007 stdcall RtlInitializeGenericTable(ptr ptr ptr ptr ptr) ChpeAutoRtlInitializeGenericTable
1008 stdcall RtlInitializeGenericTableAvl(ptr ptr ptr ptr ptr) ChpeAutoRtlInitializeGenericTableAvl
1009 stdcall RtlInitializeHandleTable(long long ptr) ChpeAutoRtlInitializeHandleTable
1010 stdcall -version=0x600+ RtlInitializeNtUserPfn(ptr long ptr long ptr long) ChpeAutoRtlInitializeNtUserPfn
1011 stdcall RtlInitializeRXact(ptr long ptr) ChpeAutoRtlInitializeRXact
1012 stdcall RtlInitializeResource(ptr) ChpeAutoRtlInitializeResource
1015 stdcall RtlInitializeSid(ptr ptr long) ChpeAutoRtlInitializeSid
1016 stdcall RtlInsertElementGenericTable(ptr ptr long ptr) ChpeAutoRtlInsertElementGenericTable
1017 stdcall RtlInsertEntryHashTable(ptr ptr long ptr) ChpeAutoRtlInsertEntryHashTable
1018 stdcall RtlInsertElementGenericTableAvl(ptr ptr long ptr) ChpeAutoRtlInsertElementGenericTableAvl
1019 stdcall RtlInsertElementGenericTableFull(ptr ptr long ptr ptr long) ChpeAutoRtlInsertElementGenericTableFull
1020 stdcall RtlInsertElementGenericTableFullAvl(ptr ptr long ptr ptr long) ChpeAutoRtlInsertElementGenericTableFullAvl
1023 stdcall RtlInt64ToUnicodeString(double long ptr) ChpeAutoRtlInt64ToUnicodeString
1024 stdcall RtlIntegerToChar(long long long ptr) ChpeAutoRtlIntegerToChar
1025 stdcall RtlIntegerToUnicodeString(long long ptr) ChpeAutoRtlIntegerToUnicodeString
1031 stdcall -version=0x600+ RtlIoDecodeMemIoResource(ptr ptr ptr ptr) ChpeAutoRtlIoDecodeMemIoResource
1032 stdcall -version=0x600+ RtlIoEncodeMemIoResource(ptr long int64 int64 int64 int64) ChpeAutoRtlIoEncodeMemIoResource
1033 stdcall RtlIpv4AddressToStringA(ptr ptr) ChpeAutoRtlIpv4AddressToStringA
1034 stdcall RtlIpv4AddressToStringExA(ptr long ptr ptr) ChpeAutoRtlIpv4AddressToStringExA
1035 stdcall RtlIpv4AddressToStringExW(ptr long ptr ptr) ChpeAutoRtlIpv4AddressToStringExW
1036 stdcall RtlIpv4AddressToStringW(ptr ptr) ChpeAutoRtlIpv4AddressToStringW
1037 stdcall RtlIpv4StringToAddressA(str long ptr ptr) ChpeAutoRtlIpv4StringToAddressA
1038 stdcall RtlIpv4StringToAddressExA(str long ptr ptr) ChpeAutoRtlIpv4StringToAddressExA
1039 stdcall RtlIpv4StringToAddressExW(wstr long ptr ptr) ChpeAutoRtlIpv4StringToAddressExW
1040 stdcall RtlIpv4StringToAddressW(wstr long ptr ptr) ChpeAutoRtlIpv4StringToAddressW
1041 stdcall RtlIpv6AddressToStringA(ptr ptr) ChpeAutoRtlIpv6AddressToStringA
1042 stdcall RtlIpv6AddressToStringExA(ptr long long ptr ptr) ChpeAutoRtlIpv6AddressToStringExA
1043 stdcall RtlIpv6AddressToStringExW(ptr long long ptr ptr) ChpeAutoRtlIpv6AddressToStringExW
1044 stdcall RtlIpv6AddressToStringW(ptr ptr) ChpeAutoRtlIpv6AddressToStringW
1045 stdcall RtlIpv6StringToAddressA(str ptr ptr) ChpeAutoRtlIpv6StringToAddressA
1046 stdcall RtlIpv6StringToAddressExA(str ptr ptr ptr) ChpeAutoRtlIpv6StringToAddressExA
1047 stdcall RtlIpv6StringToAddressExW(wstr ptr ptr ptr) ChpeAutoRtlIpv6StringToAddressExW
1048 stdcall RtlIpv6StringToAddressW(wstr ptr ptr) ChpeAutoRtlIpv6StringToAddressW
1049 stdcall RtlIsActivationContextActive(ptr) ChpeAutoRtlIsActivationContextActive
1050 stdcall RtlIsCriticalSectionLocked(ptr) ChpeAutoRtlIsCriticalSectionLocked
1051 stdcall RtlIsCriticalSectionLockedByThread(ptr) ChpeAutoRtlIsCriticalSectionLockedByThread
1052 stdcall -version=0x600+ RtlIsCurrentProcess(ptr) ChpeAutoRtlIsCurrentProcess
1053 stdcall -version=0x600+ RtlIsCurrentThreadAttachExempt() ChpeStubRtlIsCurrentThreadAttachExempt
1054 stdcall RtlIsDosDeviceName_U(wstr) ChpeAutoRtlIsDosDeviceName_U
1055 stdcall RtlIsGenericTableEmpty(ptr) ChpeAutoRtlIsGenericTableEmpty
1056 stdcall RtlIsGenericTableEmptyAvl(ptr) ChpeAutoRtlIsGenericTableEmptyAvl
1057 stdcall -version=0x601+ RtlIsNameInExpression(ptr ptr long ptr) ChpeAutoRtlIsNameInExpression
1058 stdcall RtlIsMultiSessionSku() ChpeAutoRtlIsMultiSessionSku
1059 stdcall RtlIsMultiUsersInSessionSku() ChpeAutoRtlIsMultiUsersInSessionSku
1060 stdcall RtlIsNameLegalDOS8Dot3(ptr ptr ptr) ChpeAutoRtlIsNameLegalDOS8Dot3
1061 stdcall -version=0x600+ RtlIsNormalizedString(long ptr long ptr) ChpeAutoRtlIsNormalizedString
1063 stdcall RtlIsStateSeparationEnabled() ChpeAutoRtlIsStateSeparationEnabled
1064 stdcall RtlIsTextUnicode(ptr long ptr) ChpeAutoRtlIsTextUnicode
1065 stdcall RtlIsThreadWithinLoaderCallout() ChpeAutoRtlIsThreadWithinLoaderCallout
1066 stdcall RtlIsValidHandle(ptr ptr) ChpeAutoRtlIsValidHandle
1067 stdcall RtlIsValidIndexHandle(ptr long ptr) ChpeAutoRtlIsValidIndexHandle
1068 stdcall -version=0x600+ RtlIsValidLocaleName(wstr long) ChpeAutoRtlIsValidLocaleName
1069 stdcall -version=0x600+ RtlLCIDToCultureName(long ptr) ChpeAutoRtlLCIDToCultureName
1070 stdcall RtlLargeIntegerToChar(ptr long long ptr) ChpeAutoRtlLargeIntegerToChar
1071 stdcall -version=0x600+ RtlLcidToLocaleName(long ptr long long) ChpeAutoRtlLcidToLocaleName
1073 stdcall RtlLengthRequiredSid(long) ChpeAutoRtlLengthRequiredSid
1074 stdcall RtlLengthSecurityDescriptor(ptr) ChpeAutoRtlLengthSecurityDescriptor
1075 stdcall RtlLengthSid(ptr) ChpeAutoRtlLengthSid
1076 stdcall RtlLocalTimeToSystemTime(ptr ptr) ChpeAutoRtlLocalTimeToSystemTime
1077 stdcall -version=0x600+ RtlLocaleNameToLcid(wstr ptr long) ChpeAutoRtlLocaleNameToLcid
1078 stdcall RtlLockBootStatusData(ptr) ChpeAutoRtlLockBootStatusData
1079 stdcall -version=0x600+ RtlLockCurrentThread() ChpeStubRtlLockCurrentThread
1080 stdcall RtlLockHeap(long) ChpeAutoRtlLockHeap
1081 stdcall -version=0x600+ RtlLockMemoryBlockLookaside(long) ChpeStubRtlLockMemoryBlockLookaside
1082 stdcall RtlLockMemoryStreamRegion(ptr int64 int64 long) ChpeAutoRtlLockMemoryStreamRegion
1083 stdcall -version=0x600+ RtlLockMemoryZone(long) ChpeStubRtlLockMemoryZone
1084 stdcall -version=0x600+ RtlLockModuleSection(long) ChpeStubRtlLockModuleSection
1086 stdcall  RtlLogStackBackTrace() ChpeStubRtlLogStackBackTrace
1087 stdcall RtlLookupAtomInAtomTable(ptr wstr ptr) ChpeAutoRtlLookupAtomInAtomTable
1088 stdcall RtlLookupElementGenericTable(ptr ptr) ChpeAutoRtlLookupElementGenericTable
1089 stdcall RtlLookupEntryHashTable(ptr long ptr) ChpeAutoRtlLookupEntryHashTable
1090 stdcall RtlRemoveEntryHashTable(ptr ptr ptr) ChpeAutoRtlRemoveEntryHashTable
1091 stdcall RtlLookupElementGenericTableAvl(ptr ptr) ChpeAutoRtlLookupElementGenericTableAvl
1092 stdcall RtlLookupElementGenericTableFull(ptr ptr ptr long) ChpeAutoRtlLookupElementGenericTableFull
1093 stdcall RtlLookupElementGenericTableFullAvl(ptr ptr ptr long) ChpeAutoRtlLookupElementGenericTableFullAvl
1096 stdcall RtlMakeSelfRelativeSD(ptr ptr ptr) ChpeAutoRtlMakeSelfRelativeSD
1097 stdcall RtlMapGenericMask(long ptr) ChpeAutoRtlMapGenericMask
1098 stdcall RtlMapSecurityErrorToNtStatus(long) ChpeAutoRtlMapSecurityErrorToNtStatus
1100 stdcall RtlMultiAppendUnicodeStringBuffer(ptr long ptr) ChpeAutoRtlMultiAppendUnicodeStringBuffer
1101 stdcall RtlMultiByteToUnicodeN(ptr long ptr ptr long) ChpeAutoRtlMultiByteToUnicodeN
1102 stdcall RtlMultiByteToUnicodeSize(ptr str long) ChpeAutoRtlMultiByteToUnicodeSize
1103 stdcall RtlMultipleAllocateHeap(ptr long ptr long ptr) ChpeAutoRtlMultipleAllocateHeap
1104 stdcall RtlMultipleFreeHeap(ptr long long ptr) ChpeAutoRtlMultipleFreeHeap
1105 stdcall RtlNewInstanceSecurityObject(long long ptr ptr ptr ptr ptr long ptr ptr) ChpeAutoRtlNewInstanceSecurityObject
1106 stdcall RtlNewSecurityGrantedAccess(long ptr ptr ptr ptr ptr) ChpeAutoRtlNewSecurityGrantedAccess
1107 stdcall RtlNewSecurityObject(ptr ptr ptr long ptr ptr) ChpeAutoRtlNewSecurityObject
1108 stdcall RtlNewSecurityObjectEx(ptr ptr ptr ptr long long ptr ptr) ChpeAutoRtlNewSecurityObjectEx
1109 stdcall RtlNewSecurityObjectWithMultipleInheritance(ptr ptr ptr ptr long long long ptr ptr) ChpeAutoRtlNewSecurityObjectWithMultipleInheritance
1110 stdcall RtlNormalizeProcessParams(ptr) ChpeAutoRtlNormalizeProcessParams
1111 stdcall -version=0x600+ RtlNormalizeString(long ptr long ptr ptr) ChpeAutoRtlNormalizeString
1112 stdcall RtlNtPathNameToDosPathName(long ptr ptr ptr) ChpeAutoRtlNtPathNameToDosPathName
1114 stdcall RtlNtStatusToDosErrorNoTeb(long) ChpeAutoRtlNtStatusToDosErrorNoTeb
1115 stub -version=0x600+ RtlNtdllName
1116 stdcall RtlNumberGenericTableElements(ptr) ChpeAutoRtlNumberGenericTableElements
1117 stdcall RtlNumberGenericTableElementsAvl(ptr) ChpeAutoRtlNumberGenericTableElementsAvl
1118 stdcall RtlNumberOfClearBits(ptr) ChpeAutoRtlNumberOfClearBits
1119 stdcall RtlNumberOfSetBits(ptr) ChpeAutoRtlNumberOfSetBits
1120 stdcall -version=0x600+ RtlNumberOfSetBitsUlongPtr(long) ChpeAutoRtlNumberOfSetBitsUlongPtr
1121 stdcall RtlOemStringToUnicodeSize(ptr) ChpeAutoRtlOemStringToUnicodeSize
1122 stdcall RtlOemStringToUnicodeString(ptr ptr long) ChpeAutoRtlOemStringToUnicodeString
1123 stdcall RtlOemToUnicodeN(ptr long ptr ptr long) ChpeAutoRtlOemToUnicodeN
1124 stdcall RtlOpenCurrentUser(long ptr) ChpeAutoRtlOpenCurrentUser
1125 stdcall -version=0x600+ RtlOwnerAcesPresent(long) ChpeStubRtlOwnerAcesPresent
1127 stdcall RtlPinAtomInAtomTable(ptr long) ChpeAutoRtlPinAtomInAtomTable
1128 stdcall RtlPopFrame(ptr) ChpeAutoRtlPopFrame
1129 stdcall RtlPrefixString(ptr ptr long) ChpeAutoRtlPrefixString
1130 stdcall RtlPrefixUnicodeString(ptr ptr long) ChpeAutoRtlPrefixUnicodeString
1131 stdcall -version=0x600+ RtlPrepareForProcessCloning() ChpeStubRtlPrepareForProcessCloning
1132 stdcall -version=0x600+ RtlProcessFlsData(ptr long) ChpeAutoRtlProcessFlsData
1133 stdcall RtlProtectHeap(ptr long) ChpeAutoRtlProtectHeap
1134 stdcall RtlPushFrame(ptr) ChpeAutoRtlPushFrame
1135 stdcall -version=0x600+ RtlQueryActivationContextApplicationSettings(long ptr wstr wstr ptr ptr ptr) ChpeAutoRtlQueryActivationContextApplicationSettings
1136 stdcall RtlQueryAtomInAtomTable(ptr long ptr ptr ptr ptr) ChpeAutoRtlQueryAtomInAtomTable
1137 stdcall -version=0x600+ RtlQueryCriticalSectionOwner(ptr) ChpeStubRtlQueryCriticalSectionOwner
1139 stdcall -version=0x600+ RtlQueryDynamicTimeZoneInformation(ptr) ChpeAutoRtlQueryDynamicTimeZoneInformation
1140 stdcall -version=0x600+ RtlQueryElevationFlags(ptr) ChpeStubRtlQueryElevationFlags
1142 stdcall RtlQueryEnvironmentVariable_U(ptr ptr ptr) ChpeAutoRtlQueryEnvironmentVariable_U
1143 stdcall RtlQueryHeapInformation(long long ptr long ptr) ChpeAutoRtlQueryHeapInformation
1144 stdcall RtlQueryInformationAcl(ptr ptr long long) ChpeAutoRtlQueryInformationAcl
1145 stdcall RtlQueryInformationActivationContext(long long ptr long ptr long ptr) ChpeAutoRtlQueryInformationActivationContext
1146 stdcall RtlQueryInformationActiveActivationContext(long ptr long ptr) ChpeAutoRtlQueryInformationActiveActivationContext
1147 stdcall RtlQueryInterfaceMemoryStream(ptr ptr ptr) ChpeAutoRtlQueryInterfaceMemoryStream
1148 stdcall -version=0x600+ RtlQueryModuleInformation(ptr long ptr) ChpeStubRtlQueryModuleInformation
1149 stdcall  RtlQueryProcessBackTraceInformation(ptr) ChpeStubRtlQueryProcessBackTraceInformation
1150 stdcall RtlQueryProcessDebugInformation(long long ptr) ChpeAutoRtlQueryProcessDebugInformation
1151 stdcall RtlQueryProcessHeapInformation(ptr) ChpeAutoRtlQueryProcessHeapInformation
1152 stdcall  RtlQueryProcessLockInformation(ptr) ChpeStubRtlQueryProcessLockInformation
1153 stdcall RtlQueryRegistryValues(long ptr ptr ptr ptr) ChpeAutoRtlQueryRegistryValues
1154 stdcall RtlQueryRegistryValuesEx(long ptr ptr ptr ptr) ChpeAutoRtlQueryRegistryValuesEx
1155 stdcall RtlQuerySecurityObject(ptr long ptr long ptr) ChpeAutoRtlQuerySecurityObject
1156 stdcall RtlQueryTagHeap(ptr long long long ptr) ChpeAutoRtlQueryTagHeap
1157 stdcall RtlQueryTimeZoneInformation(ptr) ChpeAutoRtlQueryTimeZoneInformation
1158 stdcall RtlQueueApcWow64Thread(ptr ptr ptr ptr ptr) ChpeAutoRtlQueueApcWow64Thread
1163 stdcall RtlRandomEx(ptr) ChpeAutoRtlRandomEx
1164 stdcall -version=0x602+ RtlRbInsertNodeEx(ptr ptr long ptr) ChpeAutoRtlRbInsertNodeEx
1165 stdcall -version=0x602+ RtlRbRemoveNode(ptr ptr) ChpeAutoRtlRbRemoveNode
1167 stdcall RtlReadMemoryStream(ptr ptr long ptr) ChpeAutoRtlReadMemoryStream
1168 stdcall RtlReadOutOfProcessMemoryStream(ptr ptr long ptr) ChpeAutoRtlReadOutOfProcessMemoryStream
1169 stdcall RtlRealPredecessor(ptr) ChpeAutoRtlRealPredecessor
1170 stdcall RtlRealSuccessor(ptr) ChpeAutoRtlRealSuccessor
1171 stdcall RtlRegisterSecureMemoryCacheCallback(ptr) ChpeAutoRtlRegisterSecureMemoryCacheCallback
1172 stdcall RtlRegisterCfgTargetRange(ptr long) ChpeAutoRtlRegisterCfgTargetRange
1173 stdcall -version=0x600+ RtlRegisterThreadWithCsrss() ChpeStubRtlRegisterThreadWithCsrss
1175 stdcall RtlReleaseActivationContext(ptr) ChpeAutoRtlReleaseActivationContext
1177 stdcall -version=0x600+ RtlReleasePath(ptr) ChpeAutoRtlReleasePath
1178 stdcall RtlReleasePebLock() ChpeAutoRtlReleasePebLock
1180 stdcall RtlReleaseRelativeName(ptr) ChpeAutoRtlReleaseRelativeName
1181 stdcall RtlReleaseResource(ptr) ChpeAutoRtlReleaseResource
1184 stdcall RtlRemoteCall(ptr ptr ptr long ptr long long) ChpeAutoRtlRemoteCall
1185 stdcall -version=0x600+ RtlRemovePrivileges(ptr ptr long) ChpeAutoRtlRemovePrivileges
1188 stdcall -version=0x600+ RtlReportException(long long long) ChpeStubRtlReportException
1189 stdcall -version=0x600+ RtlResetMemoryBlockLookaside(long) ChpeStubRtlResetMemoryBlockLookaside
1190 stdcall -version=0x600+ RtlResetMemoryZone(long) ChpeStubRtlResetMemoryZone
1191 stdcall -version=0x600+ RtlResetNtUserPfn() ChpeAutoRtlResetNtUserPfn
1192 stdcall RtlResetRtlTranslations(ptr) ChpeAutoRtlResetRtlTranslations
1195 stdcall -version=0x600+ RtlRetrieveNtUserPfn(ptr ptr ptr) ChpeAutoRtlRetrieveNtUserPfn
1196 stdcall RtlRevertMemoryStream(ptr) ChpeAutoRtlRevertMemoryStream
1197 stdcall RtlRunDecodeUnicodeString(long ptr) ChpeAutoRtlRunDecodeUnicodeString
1198 stdcall RtlRunEncodeUnicodeString(long ptr) ChpeAutoRtlRunEncodeUnicodeString
1199 stdcall -version=0x600+ RtlRunOnceBeginInitialize(ptr long ptr) ChpeAutoRtlRunOnceBeginInitialize
1200 stdcall -version=0x600+ RtlRunOnceComplete(ptr long ptr) ChpeAutoRtlRunOnceComplete
1203 stdcall RtlSecondsSince1970ToTime(long ptr) ChpeAutoRtlSecondsSince1970ToTime
1204 stdcall RtlSecondsSince1980ToTime(long ptr) ChpeAutoRtlSecondsSince1980ToTime
1205 stdcall RtlSeekMemoryStream(ptr int64 long ptr) ChpeAutoRtlSeekMemoryStream
1206 stdcall RtlSelfRelativeToAbsoluteSD2(ptr ptr) ChpeAutoRtlSelfRelativeToAbsoluteSD2
1207 stdcall RtlSelfRelativeToAbsoluteSD(ptr ptr ptr ptr ptr ptr ptr ptr ptr ptr ptr) ChpeAutoRtlSelfRelativeToAbsoluteSD
1208 stdcall -version=0x600+ RtlSendMsgToSm(ptr ptr) ChpeStubRtlSendMsgToSm
1209 stdcall RtlSetAllBits(ptr) ChpeAutoRtlSetAllBits
1210 stdcall RtlSetAttributesSecurityDescriptor(ptr long ptr) ChpeAutoRtlSetAttributesSecurityDescriptor
1211 stdcall RtlSetBits(ptr long long) ChpeAutoRtlSetBits
1212 stdcall RtlSetCfgTargetValidity(ptr long long ptr) ChpeAutoRtlSetCfgTargetValidity
1213 stdcall RtlSetControlSecurityDescriptor(ptr long long) ChpeAutoRtlSetControlSecurityDescriptor
1215 stdcall RtlSetCurrentDirectory_U(ptr) ChpeAutoRtlSetCurrentDirectory_U
1216 stdcall RtlSetCurrentEnvironment(wstr ptr) ChpeAutoRtlSetCurrentEnvironment
1217 stdcall -version=0x600+ RtlSetCurrentTransaction(ptr) ChpeAutoRtlSetCurrentTransaction
1218 stdcall RtlSetDaclSecurityDescriptor(ptr long ptr long) ChpeAutoRtlSetDaclSecurityDescriptor
1219 stdcall -version=0x600+ RtlSetDynamicTimeZoneInformation(long) ChpeStubRtlSetDynamicTimeZoneInformation
1220 stdcall RtlSetEnvironmentStrings(wstr long) ChpeAutoRtlSetEnvironmentStrings
1221 stdcall -version=0x600+ RtlSetEnvironmentVar(ptr ptr long ptr long) ChpeStubRtlSetEnvironmentVar
1222 stdcall RtlSetEnvironmentVariable(ptr ptr ptr) ChpeAutoRtlSetEnvironmentVariable
1223 stdcall -version=0x600+ RtlSetExtendedFeaturesMask(ptr int64) ChpeAutoRtlSetExtendedFeaturesMask
1224 stdcall RtlSetGroupSecurityDescriptor(ptr ptr long) ChpeAutoRtlSetGroupSecurityDescriptor
1225 stdcall RtlSetHeapInformation(ptr long ptr ptr) ChpeAutoRtlSetHeapInformation
1226 stdcall RtlSetInformationAcl(ptr ptr long long) ChpeAutoRtlSetInformationAcl
1227 stdcall RtlSetIoCompletionCallback(long ptr long) ChpeAutoRtlSetIoCompletionCallback
1229 stdcall RtlSetLastWin32ErrorAndNtStatusFromNtStatus(long) ChpeAutoRtlSetLastWin32ErrorAndNtStatusFromNtStatus
1230 stdcall RtlSetMemoryStreamSize(ptr int64) ChpeAutoRtlSetMemoryStreamSize
1231 stdcall RtlSetOwnerSecurityDescriptor(ptr ptr long) ChpeAutoRtlSetOwnerSecurityDescriptor
1232 stdcall -version=0x602+ RtlSetPortableOperatingSystem(long) ChpeAutoRtlSetPortableOperatingSystem
1233 stdcall -version=0x600+ RtlSetProcessDebugInformation(ptr long ptr) ChpeStubRtlSetProcessDebugInformation
1234 stdcall -version=0x600+ RtlSetProcessPreferredUILanguages(long ptr ptr) ChpeAutoRtlSetProcessPreferredUILanguages
1235 cdecl RtlSetProcessIsCritical(long ptr long) ChpeAutoRtlSetProcessIsCritical
1236 stdcall RtlSetSaclSecurityDescriptor(ptr long ptr long) ChpeAutoRtlSetSaclSecurityDescriptor
1237 stdcall RtlSetSecurityDescriptorRMControl(ptr ptr) ChpeAutoRtlSetSecurityDescriptorRMControl
1238 stdcall RtlSetSecurityObject(long ptr ptr ptr ptr) ChpeAutoRtlSetSecurityObject
1239 stdcall RtlSetSecurityObjectEx(long ptr ptr long ptr ptr) ChpeAutoRtlSetSecurityObjectEx
1240 stdcall RtlSetThreadErrorMode(long ptr) ChpeAutoRtlSetThreadErrorMode
1241 cdecl RtlSetThreadIsCritical(long ptr long) ChpeAutoRtlSetThreadIsCritical
1242 stdcall RtlSetThreadPoolStartFunc(ptr ptr) ChpeAutoRtlSetThreadPoolStartFunc
1243 stdcall -version=0x600+ RtlSetThreadPreferredUILanguages(long ptr ptr) ChpeAutoRtlSetThreadPreferredUILanguages
1244 stdcall RtlSetTimeZoneInformation(ptr) ChpeAutoRtlSetTimeZoneInformation
1245 stdcall RtlSetTimer(ptr ptr ptr ptr long long long) ChpeAutoRtlSetTimer
1246 stdcall RtlSetUnhandledExceptionFilter(ptr) ChpeAutoRtlSetUnhandledExceptionFilter
1247 stdcall RtlSetUserFlagsHeap(ptr long ptr long long) ChpeAutoRtlSetUserFlagsHeap
1248 stdcall RtlSetUserValueHeap(ptr long ptr ptr) ChpeAutoRtlSetUserValueHeap
1249 stdcall -version=0x600+ RtlSidDominates(long long ptr) ChpeStubRtlSidDominates
1250 stdcall -version=0x600+ RtlSidEqualLevel(long long ptr) ChpeStubRtlSidEqualLevel
1251 stdcall -version=0x600+ RtlSidHashInitialize(ptr long ptr) ChpeStubRtlSidHashInitialize
1252 stdcall -version=0x600+ RtlSidHashLookup(long ptr) ChpeStubRtlSidHashLookup
1253 stdcall -version=0x600+ RtlSidIsHigherLevel(long long ptr) ChpeStubRtlSidIsHigherLevel
1255 stdcall -version=0x600+ RtlSleepConditionVariableCS(ptr ptr ptr) ChpeAutoRtlSleepConditionVariableCS
1256 stdcall -version=0x600+ RtlSleepConditionVariableSRW(ptr ptr ptr long) ChpeAutoRtlSleepConditionVariableSRW
1257 stdcall RtlSplay(ptr) ChpeAutoRtlSplay
1258 stdcall RtlStartRXact(ptr) ChpeAutoRtlStartRXact
1259 stdcall RtlStatMemoryStream(ptr ptr long) ChpeAutoRtlStatMemoryStream
1260 stdcall RtlStringFromGUID(ptr ptr) ChpeAutoRtlStringFromGUID
1261 stdcall RtlSubAuthorityCountSid(ptr) ChpeAutoRtlSubAuthorityCountSid
1262 stdcall RtlSubAuthoritySid(ptr long) ChpeAutoRtlSubAuthoritySid
1263 stdcall RtlSubtreePredecessor(ptr) ChpeAutoRtlSubtreePredecessor
1264 stdcall RtlSubtreeSuccessor(ptr) ChpeAutoRtlSubtreeSuccessor
1265 stdcall RtlSystemTimeToLocalTime(ptr ptr) ChpeAutoRtlSystemTimeToLocalTime
1266 stdcall -version=0x600+ RtlTestBit(ptr long) ChpeAutoRtlTestBit
1267 stdcall RtlTimeFieldsToTime(ptr ptr) ChpeAutoRtlTimeFieldsToTime
1268 stdcall RtlTimeToElapsedTimeFields(long long) ChpeAutoRtlTimeToElapsedTimeFields
1269 stdcall RtlTimeToSecondsSince1970(ptr ptr) ChpeAutoRtlTimeToSecondsSince1970
1270 stdcall RtlTimeToSecondsSince1980(ptr ptr) ChpeAutoRtlTimeToSecondsSince1980
1271 stdcall RtlTimeToTimeFields(long long) ChpeAutoRtlTimeToTimeFields
1272 stdcall RtlTraceDatabaseAdd(ptr long ptr ptr) ChpeAutoRtlTraceDatabaseAdd
1273 stdcall RtlTraceDatabaseCreate(long ptr long long ptr) ChpeAutoRtlTraceDatabaseCreate
1274 stdcall RtlTraceDatabaseDestroy(ptr) ChpeAutoRtlTraceDatabaseDestroy
1275 stdcall RtlTraceDatabaseEnumerate(ptr ptr ptr) ChpeAutoRtlTraceDatabaseEnumerate
1276 stdcall RtlTraceDatabaseFind(ptr long ptr ptr) ChpeAutoRtlTraceDatabaseFind
1277 stdcall RtlTraceDatabaseLock(ptr) ChpeAutoRtlTraceDatabaseLock
1278 stdcall RtlTraceDatabaseUnlock(ptr) ChpeAutoRtlTraceDatabaseUnlock
1279 stdcall RtlTraceDatabaseValidate(ptr) ChpeAutoRtlTraceDatabaseValidate
1280 stdcall RtlTryAcquirePebLock() ChpeAutoRtlTryAcquirePebLock
1284 stdcall RtlUnhandledExceptionFilter2(ptr long) ChpeAutoRtlUnhandledExceptionFilter2
1285 stdcall RtlUnhandledExceptionFilter(ptr) ChpeAutoRtlUnhandledExceptionFilter
1286 stdcall RtlUnicodeStringToAnsiSize(ptr) ChpeAutoRtlUnicodeStringToAnsiSize
1287 stdcall -version=0x601+ RtlUnicodeToUTF8N(ptr long ptr wstr long) ChpeAutoRtlUnicodeToUTF8N
1288 stdcall -version=0xA00+ RtlUTF8StringToUnicodeString(ptr ptr long) ChpeAutoRtlUTF8StringToUnicodeString
1290 stdcall RtlUnicodeStringToAnsiString(ptr ptr long) ChpeAutoRtlUnicodeStringToAnsiString
1291 stdcall RtlUnicodeStringToCountedOemString(ptr ptr long) ChpeAutoRtlUnicodeStringToCountedOemString
1292 stdcall RtlUnicodeStringToInteger(ptr long ptr) ChpeAutoRtlUnicodeStringToInteger
1293 stdcall RtlUnicodeStringToOemSize(ptr) ChpeAutoRtlUnicodeStringToOemSize
1294 stdcall RtlUnicodeStringToOemString(ptr ptr long) ChpeAutoRtlUnicodeStringToOemString
1295 stdcall RtlUnicodeToCustomCPN(ptr ptr long ptr wstr long) ChpeAutoRtlUnicodeToCustomCPN
1296 stdcall RtlUnicodeToMultiByteN(ptr long ptr ptr long) ChpeAutoRtlUnicodeToMultiByteN
1297 stdcall RtlUnicodeToMultiByteSize(ptr ptr long) ChpeAutoRtlUnicodeToMultiByteSize
1298 stdcall RtlUnicodeToOemN(ptr long ptr ptr long) ChpeAutoRtlUnicodeToOemN
1299 stdcall RtlUniform(ptr) ChpeAutoRtlUniform
1300 stdcall RtlUnlockBootStatusData(ptr) ChpeAutoRtlUnlockBootStatusData
1301 stdcall -version=0x600+ RtlUnlockCurrentThread() ChpeStubRtlUnlockCurrentThread
1302 stdcall RtlUnlockHeap(long) ChpeAutoRtlUnlockHeap
1303 stdcall -version=0x600+ RtlUnlockMemoryBlockLookaside(long) ChpeStubRtlUnlockMemoryBlockLookaside
1304 stdcall RtlUnlockMemoryStreamRegion(ptr int64 int64 long) ChpeAutoRtlUnlockMemoryStreamRegion
1305 stdcall -version=0x600+ RtlUnlockMemoryZone(long) ChpeStubRtlUnlockMemoryZone
1306 stdcall -version=0x600+ RtlUnlockModuleSection(long) ChpeStubRtlUnlockModuleSection
1309 stdcall RtlUnregisterCfgTargetRange(ptr) ChpeAutoRtlUnregisterCfgTargetRange
1310 stdcall RtlUpcaseUnicodeChar(long) ChpeAutoRtlUpcaseUnicodeChar
1311 stdcall RtlUpcaseUnicodeString(ptr ptr long) ChpeAutoRtlUpcaseUnicodeString
1312 stdcall RtlUpcaseUnicodeStringToAnsiString(ptr ptr long) ChpeAutoRtlUpcaseUnicodeStringToAnsiString
1313 stdcall RtlUpcaseUnicodeStringToCountedOemString(ptr ptr long) ChpeAutoRtlUpcaseUnicodeStringToCountedOemString
1314 stdcall RtlUpcaseUnicodeStringToOemString(ptr ptr long) ChpeAutoRtlUpcaseUnicodeStringToOemString
1315 stdcall RtlUpcaseUnicodeToCustomCPN(ptr ptr long ptr wstr long) ChpeAutoRtlUpcaseUnicodeToCustomCPN
1316 stdcall RtlUpcaseUnicodeToMultiByteN(ptr long ptr ptr long) ChpeAutoRtlUpcaseUnicodeToMultiByteN
1317 stdcall RtlUpcaseUnicodeToOemN(ptr long ptr ptr long) ChpeAutoRtlUpcaseUnicodeToOemN
1318 stdcall -version=0x600+ RtlUpdateClonedCriticalSection(long) ChpeStubRtlUpdateClonedCriticalSection
1319 stdcall -version=0x600+ RtlUpdateClonedSRWLock(ptr long) ChpeStubRtlUpdateClonedSRWLock
1320 stdcall RtlUpdateTimer(ptr ptr long long) ChpeAutoRtlUpdateTimer
1321 stdcall RtlUpperChar(long) ChpeAutoRtlUpperChar
1322 stdcall RtlUpperString(ptr ptr) ChpeAutoRtlUpperString
1324 stdcall RtlValidAcl(ptr) ChpeAutoRtlValidAcl
1325 stdcall RtlValidRelativeSecurityDescriptor(ptr long long) ChpeAutoRtlValidRelativeSecurityDescriptor
1326 stdcall RtlValidSecurityDescriptor(ptr) ChpeAutoRtlValidSecurityDescriptor
1327 stdcall RtlValidSid(ptr) ChpeAutoRtlValidSid
1328 stdcall RtlValidateHeap(long long ptr) ChpeAutoRtlValidateHeap
1329 stdcall RtlValidateProcessHeaps() ChpeAutoRtlValidateProcessHeaps
1330 stdcall RtlValidateUnicodeString(long ptr) ChpeAutoRtlValidateUnicodeString
1331 stdcall RtlVerifyVersionInfo(ptr long double) ChpeAutoRtlVerifyVersionInfo
1333 stdcall RtlVirtualUnwind2(long int64 int64 ptr ptr ptr ptr ptr ptr ptr ptr ptr long) ChpeAutoRtlVirtualUnwind2
1339 stdcall RtlWalkFrameChain(ptr long long) ChpeAutoRtlWalkFrameChain
1340 stdcall RtlWeaklyEnumerateEntryHashTable(ptr ptr) ChpeAutoRtlWeaklyEnumerateEntryHashTable
1341 stdcall RtlWalkHeap(long ptr) ChpeAutoRtlWalkHeap
1342 stdcall -version=0x600+ RtlWerpReportException(long long ptr long long ptr) ChpeStubRtlWerpReportException
1343 stdcall -version=0x600+ RtlWow64CallFunction64() ChpeStubRtlWow64CallFunction64
1344 stdcall RtlWow64EnableFsRedirection(long) ChpeAutoRtlWow64EnableFsRedirection
1345 stdcall RtlWow64EnableFsRedirectionEx(long ptr) ChpeAutoRtlWow64EnableFsRedirectionEx
1346 stdcall -version=0x600+ RtlOpenCrossProcessEmulatorWorkConnection(ptr ptr ptr) ChpeAutoRtlOpenCrossProcessEmulatorWorkConnection
1347 stdcall -version=0x600+ RtlWow64GetCpuAreaInfo(ptr long ptr) ChpeAutoRtlWow64GetCpuAreaInfo
1348 stdcall -version=0x600+ RtlWow64GetCurrentMachine() ChpeAutoRtlWow64GetCurrentMachine
1349 stdcall -version=0x600+ RtlWow64GetProcessMachines(ptr ptr ptr) ChpeAutoRtlWow64GetProcessMachines
1350 stdcall -version=0x600+ RtlWow64GetSharedInfoProcess(ptr ptr ptr) ChpeAutoRtlWow64GetSharedInfoProcess
1351 stdcall -version=0x600+ RtlWow64GetThreadContext(ptr ptr) ChpeAutoRtlWow64GetThreadContext
1353 stdcall -version=0x600+ RtlWow64IsWowGuestMachineSupported(long ptr) ChpeAutoRtlWow64IsWowGuestMachineSupported
1354 stdcall -version=0x600+ RtlWow64LogMessageInEventLogger(long long long) ChpeStubRtlWow64LogMessageInEventLogger
1355 stdcall -version=0x600+ RtlWow64PopCrossProcessWorkFromFreeList(ptr) ChpeAutoRtlWow64PopCrossProcessWorkFromFreeList
1356 stdcall -version=0x600+ RtlWow64PushCrossProcessWorkOntoWorkList(ptr ptr ptr) ChpeAutoRtlWow64PushCrossProcessWorkOntoWorkList
1357 stdcall -version=0x600+ RtlWow64RequestCrossProcessHeavyFlush(ptr) ChpeAutoRtlWow64RequestCrossProcessHeavyFlush
1358 stdcall -version=0x600+ RtlWow64SetThreadContext(ptr ptr) ChpeAutoRtlWow64SetThreadContext
1359 stdcall -version=0x600+ RtlWow64SuspendThread(ptr ptr) ChpeAutoRtlWow64SuspendThread
1360 stdcall RtlWriteMemoryStream(ptr ptr long ptr) ChpeAutoRtlWriteMemoryStream
1361 stdcall RtlWriteRegistryValue(long ptr ptr long ptr long) ChpeAutoRtlWriteRegistryValue
1362 stdcall RtlZeroHeap(ptr long) ChpeAutoRtlZeroHeap
1364 stdcall RtlZombifyActivationContext(ptr) ChpeAutoRtlZombifyActivationContext
1366 stdcall -version=0x600+ RtlpCheckDynamicTimeZoneInformation(ptr long) ChpeStubRtlpCheckDynamicTimeZoneInformation
1367 stdcall -version=0x600+ RtlpCleanupRegistryKeys() ChpeStubRtlpCleanupRegistryKeys
1368 stdcall -version=0x600+ RtlpConvertCultureNamesToLCIDs(wstr ptr) ChpeStubRtlpConvertCultureNamesToLCIDs
1369 stdcall -version=0x600+ RtlpConvertLCIDsToCultureNames(wstr ptr) ChpeStubRtlpConvertLCIDsToCultureNames
1370 stdcall -version=0x600+ RtlpCreateProcessRegistryInfo(ptr) ChpeStubRtlpCreateProcessRegistryInfo
1371 stdcall RtlpEnsureBufferSize(long ptr long) ChpeAutoRtlpEnsureBufferSize
1372 stdcall -version=0x600+ RtlpGetLCIDFromLangInfoNode(long long ptr) ChpeStubRtlpGetLCIDFromLangInfoNode
1373 stdcall -version=0x600+ RtlpGetNameFromLangInfoNode(long long long) ChpeStubRtlpGetNameFromLangInfoNode
1374 stdcall -version=0x600+ RtlpGetSystemDefaultUILanguage(ptr long) ChpeStubRtlpGetSystemDefaultUILanguage
1375 stdcall -version=0x600+ RtlpGetUserOrMachineUILanguage4NLS(long long ptr) ChpeStubRtlpGetUserOrMachineUILanguage4NLS
1376 stdcall -version=0x600+ RtlpInitializeLangRegistryInfo(ptr) ChpeStubRtlpInitializeLangRegistryInfo
1377 stdcall -version=0x600+ RtlpIsQualifiedLanguage(long ptr long) ChpeStubRtlpIsQualifiedLanguage
1378 stdcall -version=0x600+ RtlpLoadMachineUIByPolicy(ptr long ptr) ChpeStubRtlpLoadMachineUIByPolicy
1379 stdcall -version=0x600+ RtlpLoadUserUIByPolicy(ptr long ptr) ChpeStubRtlpLoadUserUIByPolicy
1380 stdcall -version=0x600+ RtlpMuiFreeLangRegistryInfo(long) ChpeStubRtlpMuiFreeLangRegistryInfo
1381 stdcall -version=0x600+ RtlpMuiRegCreateRegistryInfo() ChpeStubRtlpMuiRegCreateRegistryInfo
1382 stdcall -version=0x600+ RtlpMuiRegFreeRegistryInfo(long long) ChpeStubRtlpMuiRegFreeRegistryInfo
1383 stdcall -version=0x600+ RtlpMuiRegLoadRegistryInfo(long long) ChpeStubRtlpMuiRegLoadRegistryInfo
1384 stdcall RtlpNotOwnerCriticalSection(ptr) ChpeAutoRtlpNotOwnerCriticalSection
1385 stdcall RtlpNtCreateKey(ptr long ptr long ptr ptr) ChpeAutoRtlpNtCreateKey
1386 stdcall RtlpNtEnumerateSubKey(ptr ptr long long) ChpeAutoRtlpNtEnumerateSubKey
1387 stdcall RtlpNtMakeTemporaryKey(ptr) ChpeAutoRtlpNtMakeTemporaryKey
1388 stdcall RtlpNtOpenKey(ptr long ptr long) ChpeAutoRtlpNtOpenKey
1389 stdcall RtlpNtQueryValueKey(ptr ptr ptr ptr long) ChpeAutoRtlpNtQueryValueKey
1390 stdcall RtlpNtSetValueKey(ptr long ptr long) ChpeAutoRtlpNtSetValueKey
1391 stdcall -version=0x600+ RtlpQueryDefaultUILanguage(ptr long) ChpeStubRtlpQueryDefaultUILanguage
1392 stdcall -version=0x600+ RtlpQueryProcessDebugInformationFromWow64(long ptr) ChpeStubRtlpQueryProcessDebugInformationFromWow64
1393 stdcall -version=0x600+ RtlpRefreshCachedUILanguage(wstr long) ChpeStubRtlpRefreshCachedUILanguage
1394 stdcall -version=0x600+ RtlpSetInstallLanguage(long ptr) ChpeStubRtlpSetInstallLanguage
1395 stdcall -version=0x600+ RtlpSetPreferredUILanguages(long ptr ptr) ChpeStubRtlpSetPreferredUILanguages
1396 stdcall RtlpUnWaitCriticalSection(ptr) ChpeAutoRtlpUnWaitCriticalSection
1397 stdcall -version=0x600+ RtlpVerifyAndCommitUILanguageSettings(long) ChpeStubRtlpVerifyAndCommitUILanguageSettings
1398 stdcall RtlpWaitForCriticalSection(ptr) ChpeAutoRtlpWaitForCriticalSection
1399 stdcall RtlxAnsiStringToUnicodeSize(ptr) ChpeAutoRtlxAnsiStringToUnicodeSize
1400 stdcall RtlxOemStringToUnicodeSize(ptr) ChpeAutoRtlxOemStringToUnicodeSize
1401 stdcall RtlxUnicodeStringToAnsiSize(ptr) ChpeAutoRtlxUnicodeStringToAnsiSize
1402 stdcall RtlxUnicodeStringToOemSize(ptr) ChpeAutoRtlxUnicodeStringToOemSize
1403 stdcall -version=0x600+ ShipAssert(long long) ChpeStubShipAssert
1404 stdcall -version=0x600+ ShipAssertGetBufferInfo(ptr ptr) ChpeStubShipAssertGetBufferInfo
1405 stdcall -version=0x600+ ShipAssertMsgA(long long) ChpeStubShipAssertMsgA
1406 stdcall -version=0x600+ ShipAssertMsgW(long long) ChpeStubShipAssertMsgW
1407 stdcall -version=0x600+ TpAllocAlpcCompletion(ptr ptr ptr ptr ptr) ChpeAutoTpAllocAlpcCompletion
1408 stdcall -version=0x600+ TpAllocAlpcCompletionEx(ptr ptr ptr ptr ptr) ChpeAutoTpAllocAlpcCompletionEx
1409 stdcall -version=0x600+ TpAllocCleanupGroup(ptr) ChpeAutoTpAllocCleanupGroup
1410 stdcall -version=0x600+ TpAllocIoCompletion(ptr ptr ptr ptr ptr) ChpeAutoTpAllocIoCompletion
1411 stdcall -version=0x600+ TpAllocPool(ptr ptr) ChpeAutoTpAllocPool
1412 stdcall -version=0x600+ TpAllocTimer(ptr ptr ptr ptr) ChpeAutoTpAllocTimer
1413 stdcall -version=0x600+ TpAllocWait(ptr ptr ptr ptr) ChpeAutoTpAllocWait
1414 stdcall -version=0x600+ TpAllocWork(ptr ptr ptr ptr) ChpeAutoTpAllocWork
1415 stdcall -version=0x600+ TpAlpcRegisterCompletionList(ptr) ChpeAutoTpAlpcRegisterCompletionList
1416 stdcall -version=0x600+ TpAlpcUnregisterCompletionList(ptr) ChpeAutoTpAlpcUnregisterCompletionList
1418 stdcall -version=0x600+ TpCallbackMayRunLong(ptr) ChpeAutoTpCallbackMayRunLong
1421 stdcall -version=0x600+ TpCallbackSendAlpcMessageOnCompletion(ptr ptr long ptr) ChpeAutoTpCallbackSendAlpcMessageOnCompletion
1422 stdcall -version=0x600+ TpCallbackSendPendingAlpcMessage(ptr) ChpeAutoTpCallbackSendPendingAlpcMessage
1426 stdcall -version=0x600+ TpCaptureCaller(long) ChpeStubTpCaptureCaller
1427 stdcall -version=0x600+ TpCheckTerminateWorker(ptr) ChpeStubTpCheckTerminateWorker
1428 stdcall -version=0x600+ TpDbgDumpHeapUsage(long ptr long) ChpeStubTpDbgDumpHeapUsage
1429 stdcall -version=0x600+ TpDbgSetLogRoutine() ChpeStubTpDbgSetLogRoutine
1433 stdcall -version=0x601+ TpQueryPoolStackInformation(ptr ptr) ChpeAutoTpQueryPoolStackInformation
1434 stdcall -version=0x600+ TpReleaseAlpcCompletion(ptr) ChpeAutoTpReleaseAlpcCompletion
1444 stdcall -version=0x601+ TpSetPoolStackInformation(ptr ptr) ChpeAutoTpSetPoolStackInformation
1449 stdcall -version=0x600+ TpSimpleTryPost(ptr ptr ptr) ChpeAutoTpSimpleTryPost
1451 stdcall -version=0x600+ TpWaitForAlpcCompletion(ptr) ChpeAutoTpWaitForAlpcCompletion
1457 stdcall -version=0x600+ WerCheckEventEscalation(long ptr) ChpeStubWerCheckEventEscalation
1458 stdcall -version=0x600+ WerReportSQMEvent(long long long) ChpeStubWerReportSQMEvent
1459 stdcall -version=0x600+ WerReportWatsonEvent(long long long long) ChpeStubWerReportWatsonEvent
1460 stdcall -version=0x600+ WinSqmAddToStream(ptr long long long) ChpeStubWinSqmAddToStream
1461 stdcall -version=0x600+ WinSqmAddToStreamEx(ptr long long ptr long) ChpeStubWinSqmAddToStreamEx
1462 stdcall -version=0x600+ WinSqmEndSession(ptr) ChpeAutoWinSqmEndSession
1463 stdcall -version=0x600+ WinSqmEventEnabled(long ptr) ChpeAutoWinSqmEventEnabled
1464 stdcall -version=0x600+ WinSqmEventWrite(long long long) ChpeStubWinSqmEventWrite
1465 stdcall -version=0x600+ WinSqmIncrementDWORD(long long long) ChpeAutoWinSqmIncrementDWORD
1466 stdcall -version=0x600+ WinSqmIsOptedIn() ChpeAutoWinSqmIsOptedIn
1467 stdcall -version=0x600+ WinSqmSetDWORD(ptr long long) ChpeAutoWinSqmSetDWORD
1468 stdcall -version=0x600+ WinSqmSetDWORD64(ptr long int64) ChpeAutoWinSqmSetDWORD64
1469 stdcall -version=0x600+ WinSqmSetString(ptr long ptr) ChpeStubWinSqmSetString
1471 stdcall ZwAcceptConnectPort(ptr long ptr long long ptr) ChpeAutoZwAcceptConnectPort
1472 stdcall ZwAccessCheck(ptr long long ptr ptr ptr ptr ptr) ChpeAutoZwAccessCheck
1473 stdcall ZwAccessCheckAndAuditAlarm(ptr long ptr ptr ptr long ptr long ptr ptr ptr) ChpeAutoZwAccessCheckAndAuditAlarm
1474 stdcall ZwAccessCheckByType(ptr ptr ptr long ptr long ptr ptr long ptr ptr) ChpeAutoZwAccessCheckByType
1475 stdcall ZwAccessCheckByTypeAndAuditAlarm(ptr ptr ptr ptr ptr ptr long long long ptr long ptr long ptr ptr ptr) ChpeAutoZwAccessCheckByTypeAndAuditAlarm
1476 stdcall ZwAccessCheckByTypeResultList(ptr ptr ptr long ptr long ptr ptr long ptr ptr) ChpeAutoZwAccessCheckByTypeResultList
1477 stdcall ZwAccessCheckByTypeResultListAndAuditAlarm(ptr ptr ptr ptr ptr ptr long long long ptr long ptr long ptr ptr ptr) ChpeAutoZwAccessCheckByTypeResultListAndAuditAlarm
1478 stdcall ZwAccessCheckByTypeResultListAndAuditAlarmByHandle(ptr ptr ptr ptr ptr ptr ptr long long long ptr long ptr long ptr ptr ptr) ChpeAutoZwAccessCheckByTypeResultListAndAuditAlarmByHandle
1479 stub -version=0x600+ ZwAcquireCMFViewOwnership
1480 stdcall ZwAddAtom(ptr long ptr) ChpeAutoZwAddAtom
1481 stdcall ZwAddBootEntry(ptr long) ChpeAutoZwAddBootEntry
1482 stdcall ZwAddDriverEntry(ptr long) ChpeAutoZwAddDriverEntry
1483 stdcall ZwAdjustGroupsToken(long long long long long long) ChpeAutoZwAdjustGroupsToken
1484 stdcall ZwAdjustPrivilegesToken(long long long long long long) ChpeAutoZwAdjustPrivilegesToken
1485 stdcall ZwAlertResumeThread(long ptr) ChpeAutoZwAlertResumeThread
1486 stdcall ZwAlertThread(long) ChpeAutoZwAlertThread
1487 stdcall ZwAlertThreadByThreadId(long) ChpeAutoZwAlertThreadByThreadId
1488 stdcall ZwAllocateLocallyUniqueId(ptr) ChpeAutoZwAllocateLocallyUniqueId
1489 stdcall ZwAllocateUserPhysicalPages(ptr ptr ptr) ChpeAutoZwAllocateUserPhysicalPages
1490 stdcall ZwAllocateUuids(ptr ptr ptr ptr) ChpeAutoZwAllocateUuids
1491 stdcall ZwAllocateVirtualMemory(long ptr ptr ptr long long) ChpeAutoZwAllocateVirtualMemory
1492 stdcall ZwAllocateVirtualMemoryEx(long ptr ptr long long ptr long) ChpeAutoZwAllocateVirtualMemoryEx
1493 stdcall -version=0x600+ ZwAlpcAcceptConnectPort(ptr ptr long ptr ptr ptr ptr ptr long) ChpeAutoZwAlpcAcceptConnectPort
1494 stdcall -version=0x600+ ZwAlpcCancelMessage(ptr long ptr) ChpeAutoZwAlpcCancelMessage
1495 stdcall -version=0x600+ ZwAlpcConnectPort(ptr ptr ptr ptr long ptr ptr ptr ptr ptr ptr) ChpeAutoZwAlpcConnectPort
1496 stdcall -version=0x602+ ZwAlpcConnectPortEx(ptr ptr ptr ptr long ptr ptr ptr ptr ptr ptr) ChpeAutoZwAlpcConnectPortEx
1497 stdcall -version=0x600+ ZwAlpcCreatePort(ptr ptr ptr) ChpeAutoZwAlpcCreatePort
1498 stdcall -version=0x600+ ZwAlpcCreatePortSection(ptr long ptr long ptr ptr) ChpeAutoZwAlpcCreatePortSection
1499 stdcall -version=0x600+ ZwAlpcCreateResourceReserve(ptr long long ptr) ChpeAutoZwAlpcCreateResourceReserve
1500 stdcall -version=0x600+ ZwAlpcCreateSectionView(ptr long ptr) ChpeAutoZwAlpcCreateSectionView
1501 stdcall -version=0x600+ ZwAlpcCreateSecurityContext(ptr long ptr) ChpeAutoZwAlpcCreateSecurityContext
1502 stdcall -version=0x600+ ZwAlpcDeletePortSection(ptr long ptr) ChpeAutoZwAlpcDeletePortSection
1503 stdcall -version=0x600+ ZwAlpcDeleteResourceReserve(ptr long long) ChpeAutoZwAlpcDeleteResourceReserve
1504 stdcall -version=0x600+ ZwAlpcDeleteSectionView(ptr long ptr) ChpeAutoZwAlpcDeleteSectionView
1505 stdcall -version=0x600+ ZwAlpcDeleteSecurityContext(ptr long ptr) ChpeAutoZwAlpcDeleteSecurityContext
1506 stdcall -version=0x600+ ZwAlpcDisconnectPort(ptr long) ChpeAutoZwAlpcDisconnectPort
1507 stdcall -version=0x600+ ZwAlpcImpersonateClientOfPort(ptr ptr ptr) ChpeAutoZwAlpcImpersonateClientOfPort
1508 stdcall -version=0xA00+ ZwAlpcImpersonateClientContainerOfPort(ptr ptr long) ChpeAutoZwAlpcImpersonateClientContainerOfPort
1509 stdcall -version=0x600+ ZwAlpcOpenSenderProcess(ptr ptr ptr long long ptr) ChpeAutoZwAlpcOpenSenderProcess
1510 stdcall -version=0x600+ ZwAlpcOpenSenderThread(ptr ptr ptr long long ptr) ChpeAutoZwAlpcOpenSenderThread
1511 stdcall -version=0x600+ ZwAlpcQueryInformation(ptr long ptr long ptr) ChpeAutoZwAlpcQueryInformation
1512 stdcall -version=0x600+ ZwAlpcQueryInformationMessage(ptr ptr long ptr long ptr) ChpeAutoZwAlpcQueryInformationMessage
1513 stdcall -version=0x600+ ZwAlpcRevokeSecurityContext(ptr long ptr) ChpeAutoZwAlpcRevokeSecurityContext
1514 stdcall -version=0x600+ ZwAlpcSendWaitReceivePort(ptr long ptr ptr ptr ptr ptr ptr) ChpeAutoZwAlpcSendWaitReceivePort
1515 stdcall -version=0x600+ ZwAlpcSetInformation(ptr long ptr long) ChpeAutoZwAlpcSetInformation
1516 stdcall ZwApphelpCacheControl(long ptr) ChpeAutoZwApphelpCacheControl
1517 stdcall ZwAreMappedFilesTheSame(ptr ptr) ChpeAutoZwAreMappedFilesTheSame
1518 stdcall ZwAssignProcessToJobObject(long long) ChpeAutoZwAssignProcessToJobObject
1519 stdcall ZwCallbackReturn(ptr long long) ChpeAutoZwCallbackReturn
1520 stdcall ZwCancelDeviceWakeupRequest(ptr) ChpeAutoZwCancelDeviceWakeupRequest
1521 stdcall ZwCancelIoFile(long ptr) ChpeAutoZwCancelIoFile
1522 stdcall -version=0x600+ ZwCancelIoFileEx(ptr ptr ptr) ChpeAutoZwCancelIoFileEx
1523 stdcall -version=0x600+ ZwCancelSynchronousIoFile(ptr ptr ptr) ChpeAutoZwCancelSynchronousIoFile
1524 stdcall ZwCancelTimer(long ptr) ChpeAutoZwCancelTimer
1525 stdcall ZwClearEvent(long) ChpeAutoZwClearEvent
1526 stdcall ZwClose(long) ChpeAutoZwClose
1527 stdcall ZwCloseObjectAuditAlarm(ptr ptr long) ChpeAutoZwCloseObjectAuditAlarm
1528 stdcall -version=0x600+ ZwCommitComplete(ptr ptr) ChpeAutoZwCommitComplete
1529 stdcall -version=0x600+ ZwCommitEnlistment(ptr ptr) ChpeAutoZwCommitEnlistment
1530 stdcall -version=0x600+ ZwCommitTransaction(ptr long) ChpeAutoZwCommitTransaction
1531 stdcall ZwCompactKeys(long ptr) ChpeAutoZwCompactKeys
1532 stdcall ZwCompareTokens(ptr ptr ptr) ChpeAutoZwCompareTokens
1533 stdcall ZwCompleteConnectPort(ptr) ChpeAutoZwCompleteConnectPort
1534 stdcall ZwCompressKey(ptr) ChpeAutoZwCompressKey
1535 stdcall ZwConnectPort(ptr ptr ptr ptr ptr ptr ptr ptr) ChpeAutoZwConnectPort
1536 stdcall ZwContinue(ptr long) ChpeAutoZwContinue
1537 stdcall ZwCreateDebugObject(ptr long ptr long) ChpeAutoZwCreateDebugObject
1538 stdcall ZwCreateDirectoryObject(long long long) ChpeAutoZwCreateDirectoryObject
1539 stdcall -version=0x600+ ZwCreateEnlistment(ptr long ptr ptr ptr long long ptr) ChpeAutoZwCreateEnlistment
1540 stdcall ZwCreateEvent(long long long long long) ChpeAutoZwCreateEvent
1541 stdcall ZwCreateEventPair(ptr long ptr) ChpeAutoZwCreateEventPair
1542 stdcall ZwCreateFile(ptr long ptr ptr long long long ptr long long ptr) ChpeAutoZwCreateFile
1543 stdcall ZwCreateIoCompletion(ptr long ptr long) ChpeAutoZwCreateIoCompletion
1544 stdcall -version=0x602+ ZwCreateWaitCompletionPacket(ptr long ptr) ChpeAutoZwCreateWaitCompletionPacket
1545 stdcall -version=0x602+ ZwAssociateWaitCompletionPacket(ptr ptr ptr ptr ptr long ptr ptr) ChpeAutoZwAssociateWaitCompletionPacket
1546 stdcall -version=0x602+ ZwCancelWaitCompletionPacket(ptr long) ChpeAutoZwCancelWaitCompletionPacket
1547 stdcall ZwCreateJobObject(ptr long ptr) ChpeAutoZwCreateJobObject
1548 stdcall ZwCreateJobSet(long ptr long) ChpeAutoZwCreateJobSet
1549 stdcall ZwCreateKey(ptr long ptr long ptr long long) ChpeAutoZwCreateKey
1550 stdcall -version=0x600+ ZwCreateKeyTransacted(ptr long ptr long ptr long ptr ptr) ChpeAutoZwCreateKeyTransacted
1551 stdcall ZwCreateKeyedEvent(ptr long ptr long) ChpeAutoZwCreateKeyedEvent
1552 stdcall ZwCreateMailslotFile(long long long long long long long long) ChpeAutoZwCreateMailslotFile
1553 stdcall ZwCreateMutant(ptr long ptr long) ChpeAutoZwCreateMutant
1554 stdcall ZwCreateNamedPipeFile(ptr long ptr ptr long long long long long long long long long ptr) ChpeAutoZwCreateNamedPipeFile
1555 stdcall ZwCreatePagingFile(ptr ptr ptr long) ChpeAutoZwCreatePagingFile
1556 stdcall ZwCreatePort(ptr ptr long long long) ChpeAutoZwCreatePort
1557 stdcall ZwCreateProcess(ptr long ptr ptr long ptr ptr ptr) ChpeAutoZwCreateProcess
1558 stdcall ZwCreateProcessEx(ptr long ptr ptr long ptr ptr ptr long) ChpeAutoZwCreateProcessEx
1559 stdcall ZwCreateProfile(ptr ptr ptr long long ptr long long long) ChpeAutoZwCreateProfile
1560 stdcall -version=0x600+ ZwCreateResourceManager(ptr long ptr ptr ptr long ptr) ChpeAutoZwCreateResourceManager
1561 stdcall ZwCreateSection(ptr long ptr ptr long long long) ChpeAutoZwCreateSection
1562 stdcall ZwCreateSemaphore(ptr long ptr long long) ChpeAutoZwCreateSemaphore
1563 stdcall ZwCreateSymbolicLinkObject(ptr long ptr ptr) ChpeAutoZwCreateSymbolicLinkObject
1564 stdcall ZwCreateThread(ptr long ptr ptr ptr ptr ptr long) ChpeAutoZwCreateThread
1565 stdcall -version=0x600+ ZwCreateThreadEx(ptr long ptr ptr ptr ptr long long long long ptr) ChpeAutoZwCreateThreadEx
1566 stdcall ZwCreateTimer(ptr long ptr long) ChpeAutoZwCreateTimer
1567 stdcall ZwCreateTimer2(ptr ptr ptr long long) ChpeAutoZwCreateTimer2
1568 stdcall ZwCreateToken(ptr long ptr long ptr ptr ptr ptr ptr ptr ptr ptr ptr) ChpeAutoZwCreateToken
1569 stdcall -version=0x600+ ZwCreateTransaction(ptr long ptr ptr ptr long long long ptr ptr) ChpeAutoZwCreateTransaction
1570 stdcall -version=0x600+ ZwCreateTransactionManager(ptr long ptr ptr long long) ChpeAutoZwCreateTransactionManager
1571 stdcall -version=0x600+ ZwCreateUserProcess(ptr ptr long long ptr ptr long long ptr ptr ptr) ChpeAutoZwCreateUserProcess
1572 stdcall ZwCreateWaitablePort(ptr ptr long long long) ChpeAutoZwCreateWaitablePort
1573 stdcall -version=0x602+ ZwCreateWnfStateName(ptr long long long ptr long ptr) ChpeAutoZwCreateWnfStateName
1574 stdcall -version=0x600+ ZwCreateWorkerFactory(ptr long ptr ptr ptr ptr ptr long long long) ChpeStubZwCreateWorkerFactory
1575 stdcall ZwDebugActiveProcess(ptr ptr) ChpeAutoZwDebugActiveProcess
1576 stdcall ZwDebugContinue(ptr ptr long) ChpeAutoZwDebugContinue
1577 stdcall ZwDelayExecution(long ptr) ChpeAutoZwDelayExecution
1578 stdcall ZwDeleteAtom(long) ChpeAutoZwDeleteAtom
1579 stdcall ZwDeleteBootEntry(long) ChpeAutoZwDeleteBootEntry
1580 stdcall ZwDeleteDriverEntry(long) ChpeAutoZwDeleteDriverEntry
1581 stdcall ZwDeleteFile(ptr) ChpeAutoZwDeleteFile
1582 stdcall ZwDeleteKey(long) ChpeAutoZwDeleteKey
1583 stdcall ZwDeleteObjectAuditAlarm(ptr ptr long) ChpeAutoZwDeleteObjectAuditAlarm
1584 stdcall -version=0x600+ ZwDeletePrivateNamespace(ptr) ChpeAutoZwDeletePrivateNamespace
1585 stdcall ZwDeleteValueKey(long ptr) ChpeAutoZwDeleteValueKey
1586 stdcall -version=0x602+ ZwDeleteWnfStateData(ptr ptr) ChpeAutoZwDeleteWnfStateData
1587 stdcall -version=0x602+ ZwDeleteWnfStateName(ptr) ChpeAutoZwDeleteWnfStateName
1588 stdcall ZwDeviceIoControlFile(long long long long long long long long long long) ChpeAutoZwDeviceIoControlFile
1589 stdcall ZwDisplayString(ptr) ChpeAutoZwDisplayString
1590 stdcall ZwDuplicateObject(long long long ptr long long long) ChpeAutoZwDuplicateObject
1591 stdcall ZwDuplicateToken(long long long long long long) ChpeAutoZwDuplicateToken
1592 stdcall ZwEnumerateBootEntries(ptr ptr) ChpeAutoZwEnumerateBootEntries
1593 stdcall ZwEnumerateDriverEntries(ptr ptr) ChpeAutoZwEnumerateDriverEntries
1594 stdcall ZwEnumerateKey(long long long ptr long ptr) ChpeAutoZwEnumerateKey
1595 stdcall ZwEnumerateSystemEnvironmentValuesEx(long ptr long) ChpeAutoZwEnumerateSystemEnvironmentValuesEx
1596 stdcall -version=0x600+ ZwEnumerateTransactionObject(ptr long ptr long ptr) ChpeAutoZwEnumerateTransactionObject
1597 stdcall ZwEnumerateValueKey(long long long ptr long ptr) ChpeAutoZwEnumerateValueKey
1598 stdcall ZwExtendSection(ptr ptr) ChpeAutoZwExtendSection
1599 stdcall ZwFilterToken(ptr long ptr ptr ptr ptr) ChpeAutoZwFilterToken
1600 stdcall -version=0x602+ ZwCreateLowBoxToken(ptr ptr long ptr ptr long ptr long ptr) ChpeAutoZwCreateLowBoxToken
1601 stdcall -version=0x600+ ZwCreatePrivateNamespace(ptr long ptr ptr) ChpeAutoZwCreatePrivateNamespace
1602 stdcall ZwFindAtom(ptr long ptr) ChpeAutoZwFindAtom
1603 stdcall ZwFlushBuffersFile(long ptr) ChpeAutoZwFlushBuffersFile
1604 stdcall ZwFlushBuffersFileEx(long long ptr long ptr) ChpeAutoZwFlushBuffersFileEx
1605 stdcall -version=0x600+ ZwFlushInstallUILanguage(long long) ChpeStubZwFlushInstallUILanguage
1606 stdcall ZwFlushInstructionCache(long ptr long) ChpeAutoZwFlushInstructionCache
1607 stdcall ZwFlushKey(long) ChpeAutoZwFlushKey
1608 stdcall -version=0x600+ ZwFlushProcessWriteBuffers() ChpeAutoZwFlushProcessWriteBuffers
1609 stdcall ZwFlushVirtualMemory(ptr ptr ptr ptr) ChpeAutoZwFlushVirtualMemory
1610 stdcall ZwFlushWriteBuffer() ChpeAutoZwFlushWriteBuffer
1611 stdcall ZwFreeUserPhysicalPages(ptr ptr ptr) ChpeAutoZwFreeUserPhysicalPages
1612 stdcall ZwFreeVirtualMemory(long ptr ptr long) ChpeAutoZwFreeVirtualMemory
1613 stdcall -version=0x600+ ZwFreezeRegistry(long) ChpeStubZwFreezeRegistry
1614 stdcall -version=0x600+ ZwFreezeTransactions(ptr ptr) ChpeStubZwFreezeTransactions
1615 stdcall ZwFsControlFile(long long long long long long long long long long) ChpeAutoZwFsControlFile
1616 stdcall ZwGetContextThread(long ptr) ChpeAutoZwGetContextThread
1617 stdcall ZwGetCurrentProcessorNumber() ChpeAutoZwGetCurrentProcessorNumber
1618 stdcall -version=0xA00+ ZwGetCurrentProcessorNumberEx(ptr) ChpeAutoZwGetCurrentProcessorNumberEx
1619 stdcall ZwGetDevicePowerState(ptr ptr) ChpeAutoZwGetDevicePowerState
1620 stdcall -version=0x600+ ZwGetMUIRegistryInfo(long ptr ptr) ChpeStubZwGetMUIRegistryInfo
1621 stdcall -version=0x600+ ZwGetNextProcess(ptr long long long ptr) ChpeStubZwGetNextProcess
1622 stdcall -version=0x600+ ZwGetNextThread(ptr ptr long long long ptr) ChpeAutoZwGetNextThread
1623 stdcall -version=0x600+ ZwGetNlsSectionPtr(long long ptr ptr ptr) ChpeAutoZwGetNlsSectionPtr
1624 stdcall -version=0x600+ ZwGetNotificationResourceManager(ptr ptr long ptr ptr long ptr) ChpeAutoZwGetNotificationResourceManager
1625 stdcall ZwGetPlugPlayEvent(long long ptr long) ChpeAutoZwGetPlugPlayEvent
1626 stdcall ZwGetWriteWatch(long long ptr long ptr ptr ptr) ChpeAutoZwGetWriteWatch
1627 stdcall ZwImpersonateAnonymousToken(ptr) ChpeAutoZwImpersonateAnonymousToken
1628 stdcall ZwImpersonateClientOfPort(ptr ptr) ChpeAutoZwImpersonateClientOfPort
1629 stdcall ZwImpersonateThread(ptr ptr ptr) ChpeAutoZwImpersonateThread
1630 stdcall -version=0x600+ ZwInitializeNlsFiles(ptr ptr ptr ptr) ChpeStubZwInitializeNlsFiles
1631 stdcall ZwInitializeRegistry(long) ChpeAutoZwInitializeRegistry
1632 stdcall ZwInitiatePowerAction(long long long long) ChpeAutoZwInitiatePowerAction
1633 stdcall ZwIsProcessInJob(long long) ChpeAutoZwIsProcessInJob
1634 stdcall ZwIsSystemResumeAutomatic() ChpeAutoZwIsSystemResumeAutomatic
1635 stdcall -version=0x600+ ZwIsUILanguageComitted() ChpeStubZwIsUILanguageComitted
1636 stdcall ZwListenPort(ptr ptr) ChpeAutoZwListenPort
1637 stdcall ZwLoadDriver(ptr) ChpeAutoZwLoadDriver
1638 stdcall ZwLoadKey2(ptr ptr long) ChpeAutoZwLoadKey2
1639 stdcall ZwLoadKey(ptr ptr) ChpeAutoZwLoadKey
1640 stdcall ZwLoadKeyEx(ptr ptr long ptr ptr long ptr ptr) ChpeAutoZwLoadKeyEx
1641 stdcall ZwLockFile(long long ptr ptr ptr ptr ptr ptr long long) ChpeAutoZwLockFile
1642 stdcall ZwLockProductActivationKeys(ptr ptr) ChpeAutoZwLockProductActivationKeys
1643 stdcall ZwLockRegistryKey(ptr) ChpeAutoZwLockRegistryKey
1644 stdcall ZwLockVirtualMemory(long ptr ptr long) ChpeAutoZwLockVirtualMemory
1645 stdcall ZwMakePermanentObject(ptr) ChpeAutoZwMakePermanentObject
1646 stdcall ZwMakeTemporaryObject(long) ChpeAutoZwMakeTemporaryObject
1647 stdcall -version=0x600+ ZwMapCMFModule(long long ptr ptr ptr) ChpeStubZwMapCMFModule
1648 stdcall ZwMapUserPhysicalPages(ptr ptr ptr) ChpeAutoZwMapUserPhysicalPages
1649 stdcall ZwMapUserPhysicalPagesScatter(ptr ptr ptr) ChpeAutoZwMapUserPhysicalPagesScatter
1650 stdcall ZwMapViewOfSection(long long ptr long long ptr ptr long long long) ChpeAutoZwMapViewOfSection
1651 stdcall ZwModifyBootEntry(ptr) ChpeAutoZwModifyBootEntry
1652 stdcall ZwModifyDriverEntry(ptr) ChpeAutoZwModifyDriverEntry
1653 stdcall ZwNotifyChangeDirectoryFile(long long ptr ptr ptr ptr long long long) ChpeAutoZwNotifyChangeDirectoryFile
1654 stdcall ZwNotifyChangeDirectoryFileEx(long long ptr ptr ptr ptr long long long long) ChpeAutoZwNotifyChangeDirectoryFileEx
1655 stdcall ZwNotifyChangeKey(long long ptr ptr ptr long long ptr long long) ChpeAutoZwNotifyChangeKey
1656 stdcall ZwNotifyChangeMultipleKeys(ptr long ptr ptr ptr ptr ptr long long ptr long long) ChpeAutoZwNotifyChangeMultipleKeys
1657 stdcall ZwOpenDirectoryObject(long long long) ChpeAutoZwOpenDirectoryObject
1658 stdcall -version=0x600+ ZwOpenEnlistment(ptr long ptr ptr ptr) ChpeAutoZwOpenEnlistment
1659 stdcall ZwOpenEvent(long long long) ChpeAutoZwOpenEvent
1660 stdcall ZwOpenEventPair(ptr long ptr) ChpeAutoZwOpenEventPair
1661 stdcall ZwOpenFile(ptr long ptr ptr long long) ChpeAutoZwOpenFile
1662 stdcall ZwOpenIoCompletion(ptr long ptr) ChpeAutoZwOpenIoCompletion
1663 stdcall ZwOpenJobObject(ptr long ptr) ChpeAutoZwOpenJobObject
1664 stdcall ZwOpenKey(ptr long ptr) ChpeAutoZwOpenKey
1665 stdcall ZwOpenKeyEx(ptr long ptr long) ChpeAutoZwOpenKeyEx
1666 stdcall -version=0x600+ ZwOpenKeyTransacted(ptr long ptr ptr) ChpeAutoZwOpenKeyTransacted
1667 stdcall ZwOpenKeyedEvent(ptr long ptr) ChpeAutoZwOpenKeyedEvent
1668 stdcall ZwOpenMutant(ptr long ptr) ChpeAutoZwOpenMutant
1669 stdcall ZwOpenObjectAuditAlarm(ptr ptr ptr ptr ptr ptr long long ptr long long ptr) ChpeAutoZwOpenObjectAuditAlarm
1670 stdcall -version=0x600+ ZwOpenPrivateNamespace(ptr long ptr ptr) ChpeAutoZwOpenPrivateNamespace
1671 stdcall ZwOpenProcess(ptr long ptr ptr) ChpeAutoZwOpenProcess
1672 stdcall ZwOpenProcessToken(long long ptr) ChpeAutoZwOpenProcessToken
1673 stdcall ZwOpenProcessTokenEx(long long long ptr) ChpeAutoZwOpenProcessTokenEx
1674 stdcall -version=0x600+ ZwOpenResourceManager(ptr long ptr ptr ptr) ChpeAutoZwOpenResourceManager
1675 stdcall ZwOpenSection(ptr long ptr) ChpeAutoZwOpenSection
1676 stdcall ZwOpenSemaphore(long long ptr) ChpeAutoZwOpenSemaphore
1677 stdcall -version=0x600+ ZwOpenSession(ptr long ptr) ChpeStubZwOpenSession
1678 stdcall ZwOpenSymbolicLinkObject(ptr long ptr) ChpeAutoZwOpenSymbolicLinkObject
1679 stdcall ZwOpenThread(ptr long ptr ptr) ChpeAutoZwOpenThread
1680 stdcall ZwOpenThreadToken(long long long ptr) ChpeAutoZwOpenThreadToken
1681 stdcall ZwOpenThreadTokenEx(long long long long ptr) ChpeAutoZwOpenThreadTokenEx
1682 stdcall ZwOpenTimer(ptr long ptr) ChpeAutoZwOpenTimer
1683 stdcall -version=0x600+ ZwOpenTransaction(ptr long ptr ptr ptr) ChpeAutoZwOpenTransaction
1684 stdcall -version=0x600+ ZwOpenTransactionManager(ptr long ptr ptr ptr long) ChpeAutoZwOpenTransactionManager
1685 stdcall ZwPlugPlayControl(ptr ptr long) ChpeAutoZwPlugPlayControl
1686 stdcall ZwPowerInformation(long ptr long ptr long) ChpeAutoZwPowerInformation
1687 stdcall -version=0x600+ ZwPrePrepareComplete(ptr ptr) ChpeAutoZwPrePrepareComplete
1688 stdcall -version=0x600+ ZwPrePrepareEnlistment(ptr ptr) ChpeAutoZwPrePrepareEnlistment
1689 stdcall -version=0x600+ ZwPrepareComplete(ptr ptr) ChpeAutoZwPrepareComplete
1690 stdcall -version=0x600+ ZwPrepareEnlistment(ptr ptr) ChpeAutoZwPrepareEnlistment
1691 stdcall ZwPrivilegeCheck(ptr ptr ptr) ChpeAutoZwPrivilegeCheck
1692 stdcall ZwPrivilegeObjectAuditAlarm(ptr ptr ptr long ptr long) ChpeAutoZwPrivilegeObjectAuditAlarm
1693 stdcall ZwPrivilegedServiceAuditAlarm(ptr ptr ptr ptr long) ChpeAutoZwPrivilegedServiceAuditAlarm
1694 stdcall -version=0x600+ ZwPropagationComplete(ptr long long ptr) ChpeAutoZwPropagationComplete
1695 stdcall -version=0x600+ ZwPropagationFailed(ptr long long) ChpeAutoZwPropagationFailed
1696 stdcall ZwProtectVirtualMemory(long ptr ptr long ptr) ChpeAutoZwProtectVirtualMemory
1697 stdcall ZwPulseEvent(long ptr) ChpeAutoZwPulseEvent
1698 stdcall ZwQueryAttributesFile(ptr ptr) ChpeAutoZwQueryAttributesFile
1699 stdcall ZwQueryBootEntryOrder(ptr ptr) ChpeAutoZwQueryBootEntryOrder
1700 stdcall ZwQueryBootOptions(ptr ptr) ChpeAutoZwQueryBootOptions
1701 stdcall ZwQueryDebugFilterState(long long) ChpeAutoZwQueryDebugFilterState
1702 stdcall ZwQueryDefaultLocale(long ptr) ChpeAutoZwQueryDefaultLocale
1703 stdcall ZwQueryDefaultUILanguage(ptr) ChpeAutoZwQueryDefaultUILanguage
1704 stdcall ZwQueryDirectoryFile(long long ptr ptr ptr ptr long long long ptr long) ChpeAutoZwQueryDirectoryFile
1705 stdcall ZwQueryDirectoryFileEx(long long ptr ptr ptr ptr long long long ptr) ChpeAutoZwQueryDirectoryFileEx
1706 stdcall ZwQueryDirectoryObject(long ptr long long long ptr ptr) ChpeAutoZwQueryDirectoryObject
1707 stdcall ZwQueryDriverEntryOrder(ptr ptr) ChpeAutoZwQueryDriverEntryOrder
1708 stdcall ZwQueryEaFile(long ptr ptr long long ptr long ptr long) ChpeAutoZwQueryEaFile
1709 stdcall ZwQueryEvent(long long ptr long ptr) ChpeAutoZwQueryEvent
1710 stdcall ZwQueryFullAttributesFile(ptr ptr) ChpeAutoZwQueryFullAttributesFile
1711 stdcall ZwQueryInformationAtom(long long ptr long ptr) ChpeAutoZwQueryInformationAtom
1712 stdcall ZwQueryInformationByName(ptr ptr ptr long long) ChpeAutoZwQueryInformationByName
1713 stdcall -version=0x600+ ZwQueryInformationEnlistment(ptr long ptr long ptr) ChpeAutoZwQueryInformationEnlistment
1714 stdcall ZwQueryInformationFile(long ptr ptr long long) ChpeAutoZwQueryInformationFile
1715 stdcall ZwQueryInformationJobObject(long long ptr long ptr) ChpeAutoZwQueryInformationJobObject
1716 stdcall ZwQueryInformationPort(ptr long ptr long ptr) ChpeAutoZwQueryInformationPort
1717 stdcall ZwQueryInformationProcess(long long ptr long ptr) ChpeAutoZwQueryInformationProcess
1718 stdcall -version=0x600+ ZwQueryInformationResourceManager(ptr long ptr long ptr) ChpeAutoZwQueryInformationResourceManager
1719 stdcall ZwQueryInformationThread(long long ptr long ptr) ChpeAutoZwQueryInformationThread
1720 stdcall ZwQueryInformationToken(long long ptr long ptr) ChpeAutoZwQueryInformationToken
1721 stdcall -version=0x600+ ZwQueryInformationTransaction(ptr long ptr long ptr) ChpeAutoZwQueryInformationTransaction
1722 stdcall -version=0x600+ ZwQueryInformationTransactionManager(ptr long ptr long ptr) ChpeAutoZwQueryInformationTransactionManager
1723 stdcall -version=0x600+ ZwQueryInformationWorkerFactory(ptr long ptr long ptr) ChpeStubZwQueryInformationWorkerFactory
1724 stdcall ZwQueryInstallUILanguage(ptr) ChpeAutoZwQueryInstallUILanguage
1725 stdcall ZwQueryIntervalProfile(long ptr) ChpeAutoZwQueryIntervalProfile
1726 stdcall ZwQueryIoCompletion(long long ptr long ptr) ChpeAutoZwQueryIoCompletion
1727 stdcall ZwQueryKey(long long ptr long ptr) ChpeAutoZwQueryKey
1728 stdcall -version=0x600+ ZwQueryLicenseValue(ptr ptr ptr long ptr) ChpeAutoZwQueryLicenseValue
1729 stdcall ZwQueryMultipleValueKey(long ptr long ptr long ptr) ChpeAutoZwQueryMultipleValueKey
1730 stdcall ZwQueryMutant(long long ptr long ptr) ChpeAutoZwQueryMutant
1731 stdcall ZwQueryObject(long long long long long) ChpeAutoZwQueryObject
1732 stdcall ZwQueryOpenSubKeys(ptr ptr) ChpeAutoZwQueryOpenSubKeys
1733 stdcall ZwQueryOpenSubKeysEx(ptr long ptr ptr) ChpeAutoZwQueryOpenSubKeysEx
1734 stdcall ZwQueryPerformanceCounter(long long) ChpeAutoZwQueryPerformanceCounter
1735 stdcall ZwQueryPortInformationProcess() ChpeAutoZwQueryPortInformationProcess
1736 stdcall ZwQueryQuotaInformationFile(ptr ptr ptr long long ptr long ptr long) ChpeAutoZwQueryQuotaInformationFile
1737 stdcall ZwQuerySection(long long long long long) ChpeAutoZwQuerySection
1738 stdcall ZwQuerySecurityObject(long long long long long) ChpeAutoZwQuerySecurityObject
1739 stdcall ZwQuerySemaphore(long long long long long) ChpeAutoZwQuerySemaphore
1740 stdcall ZwQuerySymbolicLinkObject(long ptr ptr) ChpeAutoZwQuerySymbolicLinkObject
1741 stdcall ZwQuerySystemEnvironmentValue(ptr ptr long ptr) ChpeAutoZwQuerySystemEnvironmentValue
1742 stdcall ZwQuerySystemEnvironmentValueEx(ptr ptr ptr ptr ptr) ChpeAutoZwQuerySystemEnvironmentValueEx
1743 stdcall ZwQuerySystemInformation(long long long long) ChpeAutoZwQuerySystemInformation
1744 stdcall -version=0x601+ ZwQuerySystemInformationEx(long ptr long ptr long ptr) ChpeAutoZwQuerySystemInformationEx
1745 stdcall ZwQuerySystemTime(ptr) ChpeAutoZwQuerySystemTime
1746 stdcall ZwQueryTimer(ptr long ptr long ptr) ChpeAutoZwQueryTimer
1747 stdcall ZwQueryTimerResolution(long long long) ChpeAutoZwQueryTimerResolution
1748 stdcall ZwQueryValueKey(long ptr long ptr long ptr) ChpeAutoZwQueryValueKey
1749 stdcall ZwQueryVirtualMemory(long ptr long ptr long ptr) ChpeAutoZwQueryVirtualMemory
1750 stdcall ZwQueryVolumeInformationFile(long ptr ptr long long) ChpeAutoZwQueryVolumeInformationFile
1751 stdcall -version=0x602+ ZwQueryWnfStateData(ptr ptr ptr ptr ptr ptr) ChpeAutoZwQueryWnfStateData
1752 stdcall -version=0x602+ ZwQueryWnfStateNameInformation(ptr long ptr ptr long) ChpeAutoZwQueryWnfStateNameInformation
1753 stdcall ZwQueueApcThread(long ptr long long long) ChpeAutoZwQueueApcThread
1754 stdcall ZwRaiseException(ptr ptr long) ChpeAutoZwRaiseException
1755 stdcall ZwRaiseHardError(long long long ptr long ptr) ChpeAutoZwRaiseHardError
1756 stdcall ZwReadFile(long long ptr ptr ptr ptr long ptr ptr) ChpeAutoZwReadFile
1757 stdcall ZwReadFileScatter(long long ptr ptr ptr ptr long ptr ptr) ChpeAutoZwReadFileScatter
1758 stdcall -version=0x600+ ZwReadOnlyEnlistment(ptr ptr) ChpeAutoZwReadOnlyEnlistment
1759 stdcall ZwReadRequestData(ptr ptr long ptr long ptr) ChpeAutoZwReadRequestData
1760 stdcall ZwReadVirtualMemory(long ptr ptr long ptr) ChpeAutoZwReadVirtualMemory
1761 stdcall -version=0x600+ ZwRecoverEnlistment(ptr ptr) ChpeAutoZwRecoverEnlistment
1762 stdcall -version=0x600+ ZwRecoverResourceManager(ptr) ChpeAutoZwRecoverResourceManager
1763 stdcall -version=0x600+ ZwRecoverTransactionManager(ptr) ChpeAutoZwRecoverTransactionManager
1764 stdcall -version=0x600+ ZwRegisterProtocolAddressInformation(ptr ptr long ptr long) ChpeAutoZwRegisterProtocolAddressInformation
1765 stdcall ZwRegisterThreadTerminatePort(ptr) ChpeAutoZwRegisterThreadTerminatePort
1766 stdcall -version=0x600+ ZwReleaseCMFViewOwnership() ChpeStubZwReleaseCMFViewOwnership
1767 stdcall ZwReleaseKeyedEvent(ptr ptr long ptr) ChpeAutoZwReleaseKeyedEvent
1768 stdcall ZwReleaseMutant(long ptr) ChpeAutoZwReleaseMutant
1769 stdcall ZwReleaseSemaphore(long long ptr) ChpeAutoZwReleaseSemaphore
1770 stdcall -version=0x600+ ZwReleaseWorkerFactoryWorker(ptr) ChpeStubZwReleaseWorkerFactoryWorker
1771 stdcall ZwRemoveIoCompletion(ptr ptr ptr ptr ptr) ChpeAutoZwRemoveIoCompletion
1772 stdcall -version=0x600+ ZwRemoveIoCompletionEx(ptr ptr long ptr ptr long) ChpeAutoZwRemoveIoCompletionEx
1773 stdcall ZwRemoveProcessDebug(ptr ptr) ChpeAutoZwRemoveProcessDebug
1774 stdcall ZwRenameKey(ptr ptr) ChpeAutoZwRenameKey
1775 stdcall -version=0x600+ ZwRenameTransactionManager(ptr ptr) ChpeAutoZwRenameTransactionManager
1776 stdcall ZwReplaceKey(ptr long ptr) ChpeAutoZwReplaceKey
1777 stdcall -version=0x600+ ZwReplacePartitionUnit(wstr wstr long) ChpeStubZwReplacePartitionUnit
1778 stdcall ZwReplyPort(ptr ptr) ChpeAutoZwReplyPort
1779 stdcall ZwReplyWaitReceivePort(ptr ptr ptr ptr) ChpeAutoZwReplyWaitReceivePort
1780 stdcall ZwReplyWaitReceivePortEx(ptr ptr ptr ptr ptr) ChpeAutoZwReplyWaitReceivePortEx
1781 stdcall ZwReplyWaitReplyPort(ptr ptr) ChpeAutoZwReplyWaitReplyPort
1782 stdcall ZwRequestDeviceWakeup(ptr) ChpeAutoZwRequestDeviceWakeup
1783 stdcall ZwRequestPort(ptr ptr) ChpeAutoZwRequestPort
1784 stdcall ZwRequestWaitReplyPort(ptr ptr ptr) ChpeAutoZwRequestWaitReplyPort
1785 stdcall ZwRequestWakeupLatency(long) ChpeAutoZwRequestWakeupLatency
1786 stdcall ZwResetEvent(long ptr) ChpeAutoZwResetEvent
1787 stdcall ZwResetWriteWatch(long ptr long) ChpeAutoZwResetWriteWatch
1788 stdcall ZwRestoreKey(long long long) ChpeAutoZwRestoreKey
1789 stdcall ZwResumeProcess(ptr) ChpeAutoZwResumeProcess
1790 stdcall ZwResumeThread(long long) ChpeAutoZwResumeThread
1791 stdcall -version=0x600+ ZwRollbackComplete(ptr ptr) ChpeAutoZwRollbackComplete
1792 stdcall -version=0x600+ ZwRollbackEnlistment(ptr ptr) ChpeAutoZwRollbackEnlistment
1793 stdcall -version=0x600+ ZwRollbackTransaction(ptr long) ChpeAutoZwRollbackTransaction
1794 stdcall -version=0x600+ ZwRollforwardTransactionManager(ptr ptr) ChpeAutoZwRollforwardTransactionManager
1795 stdcall ZwSaveKey(long long) ChpeAutoZwSaveKey
1796 stdcall ZwSaveKeyEx(ptr ptr long) ChpeAutoZwSaveKeyEx
1797 stdcall ZwSaveMergedKeys(ptr ptr ptr) ChpeAutoZwSaveMergedKeys
1798 stdcall ZwSecureConnectPort(ptr ptr ptr ptr ptr ptr ptr ptr ptr) ChpeAutoZwSecureConnectPort
1799 stdcall ZwSetBootEntryOrder(ptr ptr) ChpeAutoZwSetBootEntryOrder
1800 stdcall ZwSetBootOptions(ptr long) ChpeAutoZwSetBootOptions
1801 stdcall ZwSetContextThread(long ptr) ChpeAutoZwSetContextThread
1802 stdcall ZwSetDebugFilterState(long long long) ChpeAutoZwSetDebugFilterState
1803 stdcall ZwSetDefaultHardErrorPort(ptr) ChpeAutoZwSetDefaultHardErrorPort
1804 stdcall ZwSetDefaultLocale(long long) ChpeAutoZwSetDefaultLocale
1805 stdcall ZwSetDefaultUILanguage(long) ChpeAutoZwSetDefaultUILanguage
1806 stdcall ZwSetDriverEntryOrder(ptr ptr) ChpeAutoZwSetDriverEntryOrder
1807 stdcall ZwSetEaFile(long ptr ptr long) ChpeAutoZwSetEaFile
1808 stdcall ZwSetEvent(long long) ChpeAutoZwSetEvent
1809 stdcall ZwSetEventBoostPriority(ptr) ChpeAutoZwSetEventBoostPriority
1810 stdcall ZwSetHighEventPair(ptr) ChpeAutoZwSetHighEventPair
1811 stdcall ZwSetHighWaitLowEventPair(ptr) ChpeAutoZwSetHighWaitLowEventPair
1812 stdcall ZwSetInformationDebugObject(ptr long ptr long ptr) ChpeAutoZwSetInformationDebugObject
1813 stdcall -version=0x600+ ZwSetInformationEnlistment(ptr long ptr long) ChpeAutoZwSetInformationEnlistment
1814 stdcall ZwSetInformationFile(long long long long long) ChpeAutoZwSetInformationFile
1815 stdcall ZwSetInformationJobObject(long long ptr long) ChpeAutoZwSetInformationJobObject
1816 stdcall ZwSetInformationKey(long long ptr long) ChpeAutoZwSetInformationKey
1817 stdcall ZwSetInformationObject(long long ptr long) ChpeAutoZwSetInformationObject
1818 stdcall ZwSetInformationProcess(long long long long) ChpeAutoZwSetInformationProcess
1819 stdcall -version=0x600+ ZwSetInformationResourceManager(ptr long ptr long) ChpeAutoZwSetInformationResourceManager
1820 stdcall ZwSetInformationThread(long long ptr long) ChpeAutoZwSetInformationThread
1821 stdcall ZwSetInformationToken(long long ptr long) ChpeAutoZwSetInformationToken
1822 stdcall -version=0x600+ ZwSetInformationTransaction(ptr long ptr long) ChpeAutoZwSetInformationTransaction
1823 stdcall -version=0x600+ ZwSetInformationTransactionManager(ptr long ptr long) ChpeAutoZwSetInformationTransactionManager
1824 stdcall ZwSetInformationVirtualMemory(ptr long ptr ptr ptr long) ChpeAutoZwSetInformationVirtualMemory
1825 stdcall -version=0x600+ ZwSetInformationWorkerFactory(ptr long ptr long) ChpeStubZwSetInformationWorkerFactory
1826 stdcall ZwSetIntervalProfile(long long) ChpeAutoZwSetIntervalProfile
1827 stdcall ZwSetIoCompletion(ptr long ptr long long) ChpeAutoZwSetIoCompletion
1828 stdcall ZwSetLdtEntries(long int64 long int64) ChpeAutoZwSetLdtEntries
1829 stdcall ZwSetLowEventPair(ptr) ChpeAutoZwSetLowEventPair
1830 stdcall ZwSetLowWaitHighEventPair(ptr) ChpeAutoZwSetLowWaitHighEventPair
1831 stdcall ZwSetQuotaInformationFile(ptr ptr ptr long) ChpeAutoZwSetQuotaInformationFile
1832 stdcall ZwSetSecurityObject(long long ptr) ChpeAutoZwSetSecurityObject
1833 stdcall ZwSetSystemEnvironmentValue(ptr ptr) ChpeAutoZwSetSystemEnvironmentValue
1834 stdcall ZwSetSystemEnvironmentValueEx(ptr ptr ptr ptr ptr) ChpeAutoZwSetSystemEnvironmentValueEx
1835 stdcall ZwSetSystemInformation(long ptr long) ChpeAutoZwSetSystemInformation
1836 stdcall ZwSetSystemPowerState(long long long) ChpeAutoZwSetSystemPowerState
1837 stdcall ZwSetSystemTime(ptr ptr) ChpeAutoZwSetSystemTime
1838 stdcall ZwSetThreadExecutionState(long ptr) ChpeAutoZwSetThreadExecutionState
1839 stdcall ZwSetTimer(long ptr ptr ptr long long ptr) ChpeAutoZwSetTimer
1840 stdcall ZwSetTimerResolution(long long ptr) ChpeAutoZwSetTimerResolution
1841 stdcall ZwSetUuidSeed(ptr) ChpeAutoZwSetUuidSeed
1842 stdcall ZwSetValueKey(long long long long long long) ChpeAutoZwSetValueKey
1843 stdcall ZwSetVolumeInformationFile(long ptr ptr long long) ChpeAutoZwSetVolumeInformationFile
1844 stdcall ZwShutdownSystem(long) ChpeAutoZwShutdownSystem
1845 stdcall -version=0x600+ ZwShutdownWorkerFactory(ptr ptr) ChpeStubZwShutdownWorkerFactory
1846 stdcall ZwSignalAndWaitForSingleObject(long long long ptr) ChpeAutoZwSignalAndWaitForSingleObject
1847 stdcall -version=0x600+ ZwSinglePhaseReject(ptr ptr) ChpeAutoZwSinglePhaseReject
1848 stdcall ZwStartProfile(ptr) ChpeAutoZwStartProfile
1849 stdcall ZwStopProfile(ptr) ChpeAutoZwStopProfile
1850 stdcall -version=0x602+ ZwSubscribeWnfStateChange(ptr long long ptr) ChpeAutoZwSubscribeWnfStateChange
1851 stdcall ZwSuspendProcess(ptr) ChpeAutoZwSuspendProcess
1852 stdcall ZwSuspendThread(long ptr) ChpeAutoZwSuspendThread
1853 stdcall ZwSystemDebugControl(long ptr long ptr long ptr) ChpeAutoZwSystemDebugControl
1854 stdcall ZwTerminateJobObject(ptr long) ChpeAutoZwTerminateJobObject
1855 stdcall ZwTerminateProcess(ptr long) ChpeAutoZwTerminateProcess
1856 stdcall ZwTerminateThread(ptr long) ChpeAutoZwTerminateThread
1857 stdcall ZwTestAlert() ChpeAutoZwTestAlert
1858 stdcall -version=0x600+ ZwThawRegistry() ChpeStubZwThawRegistry
1859 stdcall -version=0x600+ ZwThawTransactions() ChpeStubZwThawTransactions
1860 stdcall -version=0x600+ ZwTraceControl(long ptr long ptr long ptr) ChpeAutoZwTraceControl
1861 stdcall ZwTraceEvent(ptr long long ptr) ChpeAutoZwTraceEvent
1862 stdcall ZwTranslateFilePath(ptr long ptr long) ChpeAutoZwTranslateFilePath
1863 stdcall ZwUnloadDriver(ptr) ChpeAutoZwUnloadDriver
1864 stdcall ZwUnloadKey2(ptr long) ChpeAutoZwUnloadKey2
1865 stdcall ZwUnloadKey(long) ChpeAutoZwUnloadKey
1866 stdcall ZwUnloadKeyEx(ptr ptr) ChpeAutoZwUnloadKeyEx
1867 stdcall ZwUnlockFile(long ptr ptr ptr ptr) ChpeAutoZwUnlockFile
1868 stdcall ZwUnlockVirtualMemory(long ptr ptr long) ChpeAutoZwUnlockVirtualMemory
1869 stdcall ZwUnmapViewOfSection(long ptr) ChpeAutoZwUnmapViewOfSection
1870 stdcall -version=0x602+ ZwUnsubscribeWnfStateChange(ptr) ChpeAutoZwUnsubscribeWnfStateChange
1871 stdcall -version=0x602+ ZwUpdateWnfStateData(ptr ptr long ptr ptr long long) ChpeAutoZwUpdateWnfStateData
1872 stdcall ZwVdmControl(long ptr) ChpeAutoZwVdmControl
1873 stdcall ZwWaitForDebugEvent(ptr long ptr ptr) ChpeAutoZwWaitForDebugEvent
1874 stdcall ZwWaitForKeyedEvent(ptr ptr long ptr) ChpeAutoZwWaitForKeyedEvent
1875 stdcall ZwWaitForAlertByThreadId(ptr ptr) ChpeAutoZwWaitForAlertByThreadId
1876 stdcall ZwWaitForMultipleObjects32(long ptr long long ptr) ChpeAutoZwWaitForMultipleObjects32
1877 stdcall ZwWaitForMultipleObjects(long ptr long long ptr) ChpeAutoZwWaitForMultipleObjects
1878 stdcall ZwWaitForSingleObject(long long long) ChpeAutoZwWaitForSingleObject
1879 stdcall -version=0x600+ ZwWaitForWorkViaWorkerFactory(ptr ptr long ptr ptr) ChpeStubZwWaitForWorkViaWorkerFactory
1880 stdcall ZwWaitHighEventPair(ptr) ChpeAutoZwWaitHighEventPair
1881 stdcall ZwWaitLowEventPair(ptr) ChpeAutoZwWaitLowEventPair
1882 stdcall -version=0x600+ ZwWorkerFactoryWorkerReady(ptr) ChpeStubZwWorkerFactoryWorkerReady
1883 stdcall ZwWriteFile(long long ptr ptr ptr ptr long ptr ptr) ChpeAutoZwWriteFile
1884 stdcall ZwWriteFileGather(long long ptr ptr ptr ptr long ptr ptr) ChpeAutoZwWriteFileGather
1885 stdcall ZwWriteRequestData(ptr ptr long ptr long ptr) ChpeAutoZwWriteRequestData
1886 stdcall ZwWriteVirtualMemory(long ptr ptr long ptr) ChpeAutoZwWriteVirtualMemory
1887 stdcall ZwYieldExecution() ChpeAutoZwYieldExecution
1890 cdecl __isascii(long) ChpeAuto__isascii
1891 cdecl __iscsym(long) ChpeAuto__iscsym
1892 cdecl __iscsymf(long) ChpeAuto__iscsymf
1893 cdecl -version=0x600+ __misaligned_access() ChpeStub__misaligned_access
1894 cdecl __toascii(long) ChpeAuto__toascii
1895 cdecl -ret64 _atoi64(str) ChpeAuto_atoi64
1897 extern _fltused ntdll._fltused
1898 cdecl _i64toa(double ptr long) ChpeAuto_i64toa
1899 cdecl _i64tow(double ptr long) ChpeAuto_i64tow
1900 cdecl _itoa(long ptr long) ChpeAuto_itoa
1901 cdecl _itow(long ptr long) ChpeAuto_itow
1904 cdecl _ltoa(long ptr long) ChpeAuto_ltoa
1905 cdecl _ltow(long ptr long) ChpeAuto_ltow
1906 cdecl _memccpy(ptr ptr long long) ChpeAuto_memccpy
1907 cdecl _memicmp(str str long) ChpeAuto_memicmp
1915 cdecl _splitpath(str ptr ptr ptr ptr) ChpeAuto_splitpath
1916 cdecl _strcmpi(str str) ChpeAuto_strcmpi
1917 cdecl _stricmp(str str) ChpeAuto_stricmp
1918 cdecl _strlwr(str) ChpeAuto_strlwr
1919 cdecl _strnicmp(str str long) ChpeAuto_strnicmp
1920 cdecl _strupr(str) ChpeAuto_strupr
1922 cdecl _ui64toa(double ptr long) ChpeAuto_ui64toa
1923 cdecl _ui64tow(double ptr long) ChpeAuto_ui64tow
1924 cdecl _ultoa(long ptr long) ChpeAuto_ultoa
1925 cdecl _ultow(long ptr long) ChpeAuto_ultow
1926 cdecl _vscwprintf(wstr ptr) ChpeAuto_vscwprintf
1927 cdecl _vsnprintf(ptr long str ptr) ChpeAuto_vsnprintf
1928 cdecl _vsnprintf_s(ptr long long str ptr) ChpeAuto_vsnprintf_s
1929 cdecl _vsnwprintf(ptr long wstr ptr) ChpeAuto_vsnwprintf
1930 cdecl _vsnwprintf_s(ptr long long wstr ptr) ChpeAuto_vsnwprintf_s
1931 cdecl -version=0x600+ _vswprintf(ptr wstr ptr) ChpeAuto_vswprintf
1932 cdecl _wcsicmp(wstr wstr) ChpeAuto_wcsicmp
1933 cdecl _wcslwr(wstr) ChpeAuto_wcslwr
1934 cdecl _wcsnicmp(wstr wstr long) ChpeAuto_wcsnicmp
1935 cdecl _wcstoui64(wstr ptr long) ChpeAuto_wcstoui64
1936 cdecl _wcsupr(wstr) ChpeAuto_wcsupr
1937 cdecl _wtoi(wstr) ChpeAuto_wtoi
1938 cdecl _wtoi64(wstr) ChpeAuto_wtoi64
1939 cdecl _wtol(wstr) ChpeAuto_wtol
1940 cdecl abs(long) ChpeAutoabs
1941 cdecl atan(double) ChpeAutoatan
1942 cdecl atan2(double double) ChpeAutoatan2
1943 cdecl atoi(str) ChpeAutoatoi
1944 cdecl atol(str) ChpeAutoatol
1946 cdecl ceil(double) ChpeAutoceil
1947 cdecl cos(double) ChpeAutocos
1948 cdecl fabs(double) ChpeAutofabs
1949 cdecl floor(double) ChpeAutofloor
1950 cdecl isalnum(long) ChpeAutoisalnum
1951 cdecl isalpha(long) ChpeAutoisalpha
1952 cdecl iscntrl(long) ChpeAutoiscntrl
1953 cdecl isdigit(long) ChpeAutoisdigit
1954 cdecl isgraph(long) ChpeAutoisgraph
1955 cdecl islower(long) ChpeAutoislower
1956 cdecl isprint(long) ChpeAutoisprint
1957 cdecl ispunct(long) ChpeAutoispunct
1958 cdecl isspace(long) ChpeAutoisspace
1959 cdecl isupper(long) ChpeAutoisupper
1960 cdecl iswalpha(long) ChpeAutoiswalpha
1961 cdecl iswctype(long long) ChpeAutoiswctype
1962 cdecl iswdigit(long) ChpeAutoiswdigit
1963 cdecl iswlower(long) ChpeAutoiswlower
1964 cdecl iswspace(long) ChpeAutoiswspace
1965 cdecl iswxdigit(long) ChpeAutoiswxdigit
1966 cdecl isxdigit(long) ChpeAutoisxdigit
1967 cdecl labs(long) ChpeAutolabs
1968 cdecl log(double) ChpeAutolog
1970 cdecl mbstowcs(ptr str long) ChpeAutombstowcs
1971 cdecl memchr(ptr long long) ChpeAutomemchr
1972 cdecl memcmp(ptr ptr long) ChpeAutomemcmp
1974 cdecl memmove(ptr ptr long) ChpeAutomemmove
1975 cdecl memset(ptr long long) ChpeAutomemset
1976 cdecl pow(double double) ChpeAutopow
1978 cdecl sin(double) ChpeAutosin
1980 cdecl sqrt(double) ChpeAutosqrt
1983 cdecl strcat(str str) ChpeAutostrcat
1984 cdecl strchr(str long) ChpeAutostrchr
1985 cdecl strcmp(str str) ChpeAutostrcmp
1986 cdecl strcpy(ptr str) ChpeAutostrcpy
1987 cdecl -version=0x600+ strcpy_s(ptr long str) ChpeAutostrcpy_s
1988 cdecl -version=0x600+ strcat_s(ptr long str) ChpeAutostrcat_s
1989 cdecl -version=0x600+ strncpy_s(ptr long str long) ChpeAutostrncpy_s
1990 cdecl strcspn(str str) ChpeAutostrcspn
1991 cdecl strlen(str) ChpeAutostrlen
1992 cdecl strncat(str str long) ChpeAutostrncat
1993 cdecl strncmp(str str long) ChpeAutostrncmp
1994 cdecl strncpy(ptr str long) ChpeAutostrncpy
1995 cdecl strpbrk(str str) ChpeAutostrpbrk
1996 cdecl strrchr(str long) ChpeAutostrrchr
1997 cdecl strspn(str str) ChpeAutostrspn
1998 cdecl strstr(str str) ChpeAutostrstr
1999 cdecl strtol(str ptr long) ChpeAutostrtol
2000 cdecl strtoul(str ptr long) ChpeAutostrtoul
2003 cdecl tan(double) ChpeAutotan
2004 cdecl tolower(long) ChpeAutotolower
2005 cdecl toupper(long) ChpeAutotoupper
2006 cdecl towlower(long) ChpeAutotowlower
2007 cdecl towupper(long) ChpeAutotowupper
2010 cdecl vsprintf(ptr str ptr) ChpeAutovsprintf
2011 cdecl wcscat(wstr wstr) ChpeAutowcscat
2012 cdecl wcschr(wstr long) ChpeAutowcschr
2013 cdecl wcscmp(wstr wstr) ChpeAutowcscmp
2014 cdecl wcscpy(ptr wstr) ChpeAutowcscpy
2015 cdecl wcscspn(wstr wstr) ChpeAutowcscspn
2016 cdecl wcslen(wstr) ChpeAutowcslen
2017 cdecl wcsncat(wstr wstr long) ChpeAutowcsncat
2018 cdecl wcsncmp(wstr wstr long) ChpeAutowcsncmp
2019 cdecl wcsncpy(ptr wstr long) ChpeAutowcsncpy
2020 cdecl wcsnlen(wstr long) ChpeAutowcsnlen
2021 cdecl wcspbrk(wstr wstr) ChpeAutowcspbrk
2022 cdecl wcsrchr(wstr long) ChpeAutowcsrchr
2023 cdecl wcsspn(wstr wstr) ChpeAutowcsspn
2024 cdecl wcsstr(wstr wstr) ChpeAutowcsstr
2025 cdecl wcstol(wstr ptr long) ChpeAutowcstol
2026 cdecl wcstombs(ptr ptr long) ChpeAutowcstombs
2027 cdecl wcstoul(wstr ptr long) ChpeAutowcstoul
2033 stdcall -version=0x602+ -ret64 RtlGetSystemTimePrecise() ChpeAutoRtlGetSystemTimePrecise
# END typed bridge wrappers
