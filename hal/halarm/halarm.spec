@ fastcall ExAcquireFastMutex(ptr) ntoskrnl.ExAcquireFastMutex
@ fastcall ExReleaseFastMutex(ptr) ntoskrnl.ExReleaseFastMutex
@ fastcall ExTryToAcquireFastMutex(ptr) ntoskrnl.ExTryToAcquireFastMutex
@ stdcall HalAcquireDisplayOwnership(ptr) ntoskrnl.HalAcquireDisplayOwnership
@ stdcall HalAdjustResourceList(ptr) ntoskrnl.HalAdjustResourceList
@ stdcall HalAllProcessorsStarted() ntoskrnl.HalAllProcessorsStarted
@ stdcall HalAllocateAdapterChannel(ptr ptr long ptr) ntoskrnl.HalAllocateAdapterChannel
@ stdcall HalAllocateCommonBuffer(ptr long ptr long) ntoskrnl.HalAllocateCommonBuffer
@ stdcall HalAllocateCrashDumpRegisters(ptr ptr) ntoskrnl.HalAllocateCrashDumpRegisters
@ stdcall -version=0x0601+ HalAllocateHardwareCounters(ptr long ptr ptr) ntoskrnl.HalAllocateHardwareCounters
@ stdcall -version=0x0601+ HalFreeHardwareCounters(ptr) ntoskrnl.HalFreeHardwareCounters
@ stdcall -version=0x0601+ HalEnumerateEnvironmentVariablesEx(long ptr ptr) ntoskrnl.HalEnumerateEnvironmentVariablesEx
@ stdcall -version=0x0601+ HalSetEnvironmentVariableEx(ptr ptr ptr long long) ntoskrnl.HalSetEnvironmentVariableEx
@ stdcall -version=0x0601+ HalQueryEnvironmentVariableInfoEx(long ptr ptr ptr) ntoskrnl.HalQueryEnvironmentVariableInfoEx
@ stdcall -version=0x0601+ HalBugCheckSystem(ptr ptr) ntoskrnl.HalBugCheckSystem
@ stdcall HalAssignSlotResources(ptr ptr ptr ptr long long long ptr) ntoskrnl.HalAssignSlotResources
@ stdcall HalBeginSystemInterrupt(long long ptr) ntoskrnl.HalBeginSystemInterrupt
@ stdcall HalCalibratePerformanceCounter(ptr long long) ntoskrnl.HalCalibratePerformanceCounter
@ fastcall HalClearSoftwareInterrupt(long) ntoskrnl.HalClearSoftwareInterrupt
@ stdcall -version=0x0601+ HalDisableInterrupt(ptr) ntoskrnl.HalDisableInterrupt
@ stdcall HalDisableSystemInterrupt(long long) ntoskrnl.HalDisableSystemInterrupt
@ stdcall HalDisplayString(str) ntoskrnl.HalDisplayString
@ stdcall -version=0x0601+ HalEnableInterrupt(ptr) ntoskrnl.HalEnableInterrupt
@ stdcall HalEnableSystemInterrupt(long long long) ntoskrnl.HalEnableSystemInterrupt
@ stdcall HalEndSystemInterrupt(long long) ntoskrnl.HalEndSystemInterrupt
@ stdcall HalFlushCommonBuffer(long long long long long) ntoskrnl.HalFlushCommonBuffer
@ stdcall HalFreeCommonBuffer(ptr long long long ptr long) ntoskrnl.HalFreeCommonBuffer
@ stdcall HalGetAdapter(ptr ptr) ntoskrnl.HalGetAdapter
@ stdcall HalGetBusData(long long long ptr long) ntoskrnl.HalGetBusData
@ stdcall HalGetBusDataByOffset(long long long ptr long long) ntoskrnl.HalGetBusDataByOffset
@ stdcall -version=0x0601+ HalGetEnvironmentVariableEx(ptr ptr ptr ptr ptr) ntoskrnl.HalGetEnvironmentVariableEx
@ stdcall -version=0x0601+ HalGetInterruptTargetInformation(ptr) ntoskrnl.HalGetInterruptTargetInformation
@ stdcall HalGetEnvironmentVariable(str long str) ntoskrnl.HalGetEnvironmentVariable
@ fastcall HalGetInterruptSource() ntoskrnl.HalGetInterruptSource
@ stdcall HalGetInterruptVector(long long long long ptr ptr) ntoskrnl.HalGetInterruptVector
@ stdcall -version=0x0601+ HalGetVectorInput(long ptr ptr ptr ptr) ntoskrnl.HalGetVectorInput
@ stdcall -version=0x0601+ HalGetMemoryCachingRequirements(int64 long ptr) ntoskrnl.HalGetMemoryCachingRequirements
@ stdcall -version=0x0601+ HalGetMessageRoutingInfo(ptr) ntoskrnl.HalGetMessageRoutingInfo
@ stdcall -version=0x0601+ HalGetProcessorIdByNtNumber(long ptr) ntoskrnl.HalGetProcessorIdByNtNumber
@ stdcall HalInitSystem(long ptr) ntoskrnl.HalInitSystem
@ stdcall HalInitializeProcessor(long ptr) ntoskrnl.HalInitializeProcessor
@ stdcall HalMakeBeep(long) ntoskrnl.HalMakeBeep
@ stdcall HalProcessorIdle() ntoskrnl.HalProcessorIdle
@ stdcall HalQueryDisplayParameters(ptr ptr ptr ptr) ntoskrnl.HalQueryDisplayParameters
@ stdcall HalQueryRealTimeClock(ptr) ntoskrnl.HalQueryRealTimeClock
@ stdcall HalReadDmaCounter(ptr) ntoskrnl.HalReadDmaCounter
@ stdcall HalReportResourceUsage() ntoskrnl.HalReportResourceUsage
@ fastcall HalRequestSoftwareInterrupt(long) ntoskrnl.HalRequestSoftwareInterrupt
@ stdcall HalReturnToFirmware(long) ntoskrnl.HalReturnToFirmware
@ stdcall HalSetBusData(long long long ptr long) ntoskrnl.HalSetBusData
@ stdcall HalSetBusDataByOffset(long long long ptr long long) ntoskrnl.HalSetBusDataByOffset
@ stdcall HalSetDisplayParameters(long long) ntoskrnl.HalSetDisplayParameters
@ stdcall HalSetEnvironmentVariable(str str) ntoskrnl.HalSetEnvironmentVariable
@ stdcall HalSetProfileInterval(long) ntoskrnl.HalSetProfileInterval
@ stdcall HalSetRealTimeClock(ptr) ntoskrnl.HalSetRealTimeClock
@ stdcall HalSetTimeIncrement(long) ntoskrnl.HalSetTimeIncrement
@ stdcall HalStartNextProcessor(ptr ptr) ntoskrnl.HalStartNextProcessor
@ stdcall HalStartProfileInterrupt(long) ntoskrnl.HalStartProfileInterrupt
@ stdcall HalStopProfileInterrupt(long) ntoskrnl.HalStopProfileInterrupt
@ fastcall HalSweepIcache() ntoskrnl.HalSweepIcache
@ fastcall HalSweepDcache() ntoskrnl.HalSweepDcache
@ fastcall HalSystemVectorDispatchEntry(long long long) ntoskrnl.HalSystemVectorDispatchEntry
@ stdcall HalTranslateBusAddress(long long long long ptr ptr) ntoskrnl.HalTranslateBusAddress
@ stdcall -version=0x0601+ HalConvertDeviceIdtToIrql(long) ntoskrnl.HalConvertDeviceIdtToIrql
@ stdcall HalpConfigurePciRootBridge(ptr) ntoskrnl.HalpConfigurePciRootBridge
@ stdcall HalpAcpiQueryFaults(ptr long ptr) ntoskrnl.HalpAcpiQueryFaults
@ stdcall HalpRegisterPciRouteQuery(ptr) ntoskrnl.HalpRegisterPciRouteQuery
@ stdcall HalpSetPciRoutingMap(ptr long) ntoskrnl.HalpSetPciRoutingMap
@ stdcall HalpRecordPciMaxGsi(ptr) ntoskrnl.HalpRecordPciMaxGsi
@ stdcall HalpGetMessageRoutingInfo(ptr) ntoskrnl.HalpGetMessageRoutingInfo
@ stdcall IoFlushAdapterBuffers(ptr ptr ptr ptr long long) ntoskrnl.IoFlushAdapterBuffers
@ stdcall IoFreeAdapterChannel(ptr) ntoskrnl.IoFreeAdapterChannel
@ stdcall IoFreeMapRegisters(ptr ptr long) ntoskrnl.IoFreeMapRegisters
@ stdcall IoMapTransfer(ptr ptr ptr ptr ptr long) ntoskrnl.IoMapTransfer
@ extern -constant KdComPortInUse ntoskrnl.KdComPortInUse
@ fastcall KeAcquireInStackQueuedSpinLock(ptr ptr) ntoskrnl.KeAcquireInStackQueuedSpinLock
@ fastcall KeAcquireInStackQueuedSpinLockRaiseToSynch(ptr ptr) ntoskrnl.KeAcquireInStackQueuedSpinLockRaiseToSynch
@ fastcall KeAcquireQueuedSpinLock(ptr) ntoskrnl.KeAcquireQueuedSpinLock
@ fastcall KeAcquireQueuedSpinLockRaiseToSynch(ptr) ntoskrnl.KeAcquireQueuedSpinLockRaiseToSynch
@ stdcall KeAcquireSpinLock(ptr ptr) ntoskrnl.KeAcquireSpinLock
@ fastcall KeAcquireSpinLockRaiseToSynch(ptr) ntoskrnl.KeAcquireSpinLockRaiseToSynch
@ stdcall KeFlushWriteBuffer() ntoskrnl.KeFlushWriteBuffer
@ stdcall KeGetCurrentIrql() ntoskrnl.KeGetCurrentIrql
@ stdcall KeLowerIrql(long) ntoskrnl.KeLowerIrql
@ stdcall KeQueryPerformanceCounter(ptr) ntoskrnl.KeQueryPerformanceCounter
@ stdcall KeRaiseIrql(long ptr) ntoskrnl.KeRaiseIrql
@ stdcall KeRaiseIrqlToDpcLevel() ntoskrnl.KeRaiseIrqlToDpcLevel
@ stdcall KeRaiseIrqlToSynchLevel() ntoskrnl.KeRaiseIrqlToSynchLevel
@ fastcall KeReleaseInStackQueuedSpinLock(ptr) ntoskrnl.KeReleaseInStackQueuedSpinLock
@ fastcall KeReleaseQueuedSpinLock(ptr long) ntoskrnl.KeReleaseQueuedSpinLock
@ stdcall KeReleaseSpinLock(ptr long) ntoskrnl.KeReleaseSpinLock
@ stdcall KeStallExecutionProcessor(long) ntoskrnl.KeStallExecutionProcessor
@ fastcall KeTryToAcquireQueuedSpinLock(long ptr) ntoskrnl.KeTryToAcquireQueuedSpinLock
@ fastcall KeTryToAcquireQueuedSpinLockRaiseToSynch(long ptr) ntoskrnl.KeTryToAcquireQueuedSpinLockRaiseToSynch
@ fastcall KfAcquireSpinLock(ptr) ntoskrnl.KfAcquireSpinLock
@ fastcall KfLowerIrql(long) ntoskrnl.KfLowerIrql
@ fastcall KfRaiseIrql(long) ntoskrnl.KfRaiseIrql
@ fastcall KfReleaseSpinLock(ptr long) ntoskrnl.KfReleaseSpinLock
@ stdcall READ_PORT_BUFFER_UCHAR(ptr ptr long) ntoskrnl.READ_PORT_BUFFER_UCHAR
@ stdcall READ_PORT_BUFFER_ULONG(ptr ptr long) ntoskrnl.READ_PORT_BUFFER_ULONG
@ stdcall READ_PORT_BUFFER_USHORT(ptr ptr long) ntoskrnl.READ_PORT_BUFFER_USHORT
@ stdcall READ_PORT_UCHAR(ptr) ntoskrnl.READ_PORT_UCHAR
@ stdcall READ_PORT_ULONG(ptr) ntoskrnl.READ_PORT_ULONG
@ stdcall READ_PORT_USHORT(ptr) ntoskrnl.READ_PORT_USHORT
@ stdcall WRITE_PORT_BUFFER_UCHAR(ptr ptr long) ntoskrnl.WRITE_PORT_BUFFER_UCHAR
@ stdcall WRITE_PORT_BUFFER_ULONG(ptr ptr long) ntoskrnl.WRITE_PORT_BUFFER_ULONG
@ stdcall WRITE_PORT_BUFFER_USHORT(ptr ptr long) ntoskrnl.WRITE_PORT_BUFFER_USHORT
@ stdcall WRITE_PORT_UCHAR(ptr long) ntoskrnl.WRITE_PORT_UCHAR
@ stdcall WRITE_PORT_ULONG(ptr long) ntoskrnl.WRITE_PORT_ULONG
@ stdcall WRITE_PORT_USHORT(ptr long) ntoskrnl.WRITE_PORT_USHORT
@ extern -constant KdHvComPortInUse ntoskrnl.KdHvComPortInUse
