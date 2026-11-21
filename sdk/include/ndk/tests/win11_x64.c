/*
 * PROJECT:     LiberNT NDK tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Windows 11 24H2 x64 layouts of NDK structures
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntifs.h>
#include <ndk/ntndk.h>

#define C_ASSERT_FIELD(Type, Offset, MemberName) \
    C_ASSERT(FIELD_OFFSET(Type, MemberName) == Offset);

/* KTHREAD */
C_ASSERT(sizeof(KTHREAD) == 0x4C0);
C_ASSERT_FIELD(KTHREAD, 0x000, Header)
C_ASSERT_FIELD(KTHREAD, 0x018, SListFaultAddress)
C_ASSERT_FIELD(KTHREAD, 0x020, QuantumTarget)
C_ASSERT_FIELD(KTHREAD, 0x028, InitialStack)
C_ASSERT_FIELD(KTHREAD, 0x030, StackLimit)
C_ASSERT_FIELD(KTHREAD, 0x038, StackBase)
C_ASSERT_FIELD(KTHREAD, 0x040, ThreadLock)
C_ASSERT_FIELD(KTHREAD, 0x048, CycleTime)
C_ASSERT_FIELD(KTHREAD, 0x050, CurrentRunTime)
C_ASSERT_FIELD(KTHREAD, 0x054, ExpectedRunTime)
C_ASSERT_FIELD(KTHREAD, 0x058, KernelStack)
C_ASSERT_FIELD(KTHREAD, 0x060, StateSaveArea)
C_ASSERT_FIELD(KTHREAD, 0x068, SchedulingGroup)
C_ASSERT_FIELD(KTHREAD, 0x070, WaitRegister)
C_ASSERT_FIELD(KTHREAD, 0x071, Running)
C_ASSERT_FIELD(KTHREAD, 0x072, Alerted)
/* FIXME: KTHREAD.AutoBoostActive is at 0x074 on Windows 11 */
/* FIXME: KTHREAD.MiscFlags is at 0x074 on Windows 11 */
/* FIXME: KTHREAD.SecureThread is at 0x075 on Windows 11 */
/* FIXME: KTHREAD.WaitNextClearWobPriorityFloor is at 0x076 on Windows 11 */
/* FIXME: KTHREAD.Reserved is at 0x077 on Windows 11 */
/* FIXME: KTHREAD.UserIdealProcessorFixed is at 0x078 on Windows 11 */
/* FIXME: KTHREAD.ThreadFlags is at 0x078 on Windows 11 */
/* FIXME: KTHREAD.DisableQuantum is at 0x079 on Windows 11 */
/* FIXME: KTHREAD.AutoBoostEntriesExhausted is at 0x07A on Windows 11 */
/* FIXME: KTHREAD.EtwStackTraceApcInserted is at 0x07B on Windows 11 */
C_ASSERT_FIELD(KTHREAD, 0x07C, Tag)
/* FIXME: KTHREAD.CalloutActive is at 0x07D on Windows 11 */
/* FIXME: KTHREAD.SameThreadTransientFlags is at 0x07D on Windows 11 */
/* FIXME: KTHREAD.RunningNonRetpolineCode is at 0x07E on Windows 11 */
/* FIXME: KTHREAD.SpecCtrl is at 0x07E on Windows 11 */
C_ASSERT_FIELD(KTHREAD, 0x080, SystemCallNumber)
C_ASSERT_FIELD(KTHREAD, 0x084, ReadyTime)
C_ASSERT_FIELD(KTHREAD, 0x088, FirstArgument)
C_ASSERT_FIELD(KTHREAD, 0x090, TrapFrame)
/* FIXME: KTHREAD.ApcState is at 0x098 on Windows 11 */
/* FIXME: KTHREAD.ApcStateFill is at 0x098 on Windows 11 */
/* FIXME: KTHREAD.Priority is at 0x0C3 on Windows 11 */
/* FIXME: KTHREAD.UserIdealProcessor is at 0x0C4 on Windows 11 */
C_ASSERT_FIELD(KTHREAD, 0x0C8, WaitStatus)
C_ASSERT_FIELD(KTHREAD, 0x0D0, WaitBlockList)
/* FIXME: KTHREAD.WaitListEntry is at 0x0D8 on Windows 11 */
/* FIXME: KTHREAD.SwapListEntry is at 0x0D8 on Windows 11 */
C_ASSERT_FIELD(KTHREAD, 0x0E8, Queue)
C_ASSERT_FIELD(KTHREAD, 0x0F0, Teb)
C_ASSERT_FIELD(KTHREAD, 0x0F8, RelativeTimerBias)
C_ASSERT_FIELD(KTHREAD, 0x100, Timer)
/* FIXME: KTHREAD.WaitBlock is at 0x140 on Windows 11 */
/* FIXME: KTHREAD.WaitBlockFill4 is at 0x140 on Windows 11 */
/* FIXME: KTHREAD.WaitBlockFill5 is at 0x140 on Windows 11 */
/* FIXME: KTHREAD.WaitBlockFill6 is at 0x140 on Windows 11 */
/* FIXME: KTHREAD.WaitBlockFill7 is at 0x140 on Windows 11 */
/* FIXME: KTHREAD.WaitBlockFill8 is at 0x140 on Windows 11 */
/* FIXME: KTHREAD.WaitBlockFill9 is at 0x140 on Windows 11 */
/* FIXME: KTHREAD.WaitBlockFill10 is at 0x140 on Windows 11 */
/* FIXME: KTHREAD.WaitBlockFill11 is at 0x140 on Windows 11 */
/* FIXME: KTHREAD.ContextSwitches is at 0x154 on Windows 11 */
/* FIXME: KTHREAD.ThreadCounters is at 0x168 on Windows 11 */
/* FIXME: KTHREAD.State is at 0x184 on Windows 11 */
/* FIXME: KTHREAD.Spare13 is at 0x185 on Windows 11 */
/* FIXME: KTHREAD.WaitIrql is at 0x186 on Windows 11 */
/* FIXME: KTHREAD.WaitMode is at 0x187 on Windows 11 */
/* FIXME: KTHREAD.XStateSave is at 0x198 on Windows 11 */
/* FIXME: KTHREAD.WaitTime is at 0x1B4 on Windows 11 */
/* FIXME: KTHREAD.Win32Thread is at 0x1C8 on Windows 11 */
/* FIXME: KTHREAD.KernelApcDisable is at 0x1E4 on Windows 11 */
/* FIXME: KTHREAD.CombinedApcDisable is at 0x1E4 on Windows 11 */
/* FIXME: KTHREAD.SpecialApcDisable is at 0x1E6 on Windows 11 */
/* FIXME: KTHREAD.Spare18 is at 0x1F0 on Windows 11 */
/* FIXME: KTHREAD.LastXStateSaveDebugInfo is at 0x1F8 on Windows 11 */
/* FIXME: KTHREAD.ThreadFlags2 is at 0x200 on Windows 11 */
/* FIXME: KTHREAD.DisableKasan is at 0x200 on Windows 11 */
C_ASSERT_FIELD(KTHREAD, 0x204, BamQosLevel)
C_ASSERT_FIELD(KTHREAD, 0x205, HardwareFeedbackClass)
/* FIXME: KTHREAD.PriorityDecrement is at 0x206 on Windows 11 */
/* FIXME: KTHREAD.ForegroundBoost is at 0x206 on Windows 11 */
C_ASSERT_FIELD(KTHREAD, 0x208, QueueListEntry)
/* FIXME: KTHREAD.NextProcessor is at 0x218 on Windows 11 */
/* FIXME: KTHREAD.NextProcessorNumber is at 0x218 on Windows 11 */
C_ASSERT_FIELD(KTHREAD, 0x21C, QueuePriority)
C_ASSERT_FIELD(KTHREAD, 0x220, Process)
C_ASSERT_FIELD(KTHREAD, 0x228, UserAffinity)
C_ASSERT_FIELD(KTHREAD, 0x230, UserAffinityPrimaryGroup)
C_ASSERT_FIELD(KTHREAD, 0x232, PreviousMode)
C_ASSERT_FIELD(KTHREAD, 0x233, BasePriority)
/* FIXME: KTHREAD.Spare24 is at 0x234 on Windows 11 */
C_ASSERT_FIELD(KTHREAD, 0x235, Preempted)
C_ASSERT_FIELD(KTHREAD, 0x236, AdjustReason)
C_ASSERT_FIELD(KTHREAD, 0x237, AdjustIncrement)
C_ASSERT_FIELD(KTHREAD, 0x238, AffinityVersion)
C_ASSERT_FIELD(KTHREAD, 0x240, Affinity)
C_ASSERT_FIELD(KTHREAD, 0x248, AffinityPrimaryGroup)
C_ASSERT_FIELD(KTHREAD, 0x24A, ApcStateIndex)
C_ASSERT_FIELD(KTHREAD, 0x24B, WaitBlockCount)
C_ASSERT_FIELD(KTHREAD, 0x24C, IdealProcessor)
C_ASSERT_FIELD(KTHREAD, 0x250, NpxState)
/* FIXME: KTHREAD.SavedApcState is at 0x258 on Windows 11 */
/* FIXME: KTHREAD.SavedApcStateFill is at 0x258 on Windows 11 */
/* FIXME: KTHREAD.WaitReason is at 0x283 on Windows 11 */
/* FIXME: KTHREAD.SuspendCount is at 0x284 on Windows 11 */
/* FIXME: KTHREAD.Saturation is at 0x285 on Windows 11 */
/* FIXME: KTHREAD.SListFaultCount is at 0x286 on Windows 11 */
/* FIXME: KTHREAD.SchedulerApc is at 0x288 on Windows 11 */
/* FIXME: KTHREAD.SchedulerApcFill1 is at 0x288 on Windows 11 */
/* FIXME: KTHREAD.SchedulerApcFill2 is at 0x288 on Windows 11 */
/* FIXME: KTHREAD.SchedulerApcFill3 is at 0x288 on Windows 11 */
/* FIXME: KTHREAD.SchedulerApcFill4 is at 0x288 on Windows 11 */
/* FIXME: KTHREAD.SchedulerApcFill5 is at 0x288 on Windows 11 */
/* FIXME: KTHREAD.QuantumReset is at 0x28B on Windows 11 */
/* FIXME: KTHREAD.KernelTime is at 0x28C on Windows 11 */
/* FIXME: KTHREAD.WaitPrcb is at 0x2C8 on Windows 11 */
/* FIXME: KTHREAD.LegoData is at 0x2D0 on Windows 11 */
/* FIXME: KTHREAD.CallbackNestingLevel is at 0x2DB on Windows 11 */
/* FIXME: KTHREAD.UserTime is at 0x2DC on Windows 11 */
/* FIXME: KTHREAD.SuspendEvent is at 0x2E0 on Windows 11 */
C_ASSERT_FIELD(KTHREAD, 0x2F8, ThreadListEntry)
C_ASSERT_FIELD(KTHREAD, 0x308, MutantListHead)
/* FIXME: KTHREAD.AbWaitEntryCount is at 0x318 on Windows 11 */
/* FIXME: KTHREAD.AbEntryCountValue is at 0x318 on Windows 11 */
/* FIXME: KTHREAD.AbOwnedEntryCount is at 0x319 on Windows 11 */
/* FIXME: KTHREAD.FreezeFlags is at 0x31A on Windows 11 */
/* FIXME: KTHREAD.FreezeCount2 is at 0x31A on Windows 11 */
C_ASSERT_FIELD(KTHREAD, 0x31B, WobPriority)
C_ASSERT_FIELD(KTHREAD, 0x31C, SecureThreadCookie)
C_ASSERT_FIELD(KTHREAD, 0x320, SchedulerSharedSystemSlot)
C_ASSERT_FIELD(KTHREAD, 0x328, PropagateBoostsEntry)
C_ASSERT_FIELD(KTHREAD, 0x330, IoSelfBoostsEntry)
C_ASSERT_FIELD(KTHREAD, 0x338, PriorityFloorCounts)
C_ASSERT_FIELD(KTHREAD, 0x358, PriorityFloorSummary)
C_ASSERT_FIELD(KTHREAD, 0x35C, AbCompletedIoBoostCount)
C_ASSERT_FIELD(KTHREAD, 0x360, AbCompletedIoQoSBoostCount)
C_ASSERT_FIELD(KTHREAD, 0x364, KeReferenceCount)
C_ASSERT_FIELD(KTHREAD, 0x366, DecayBoost)
/* FIXME: KTHREAD.Spare6 is at 0x367 on Windows 11 */
C_ASSERT_FIELD(KTHREAD, 0x368, ForegroundLossTime)
/* FIXME: KTHREAD.GlobalForegroundListEntry is at 0x370 on Windows 11 */
/* FIXME: KTHREAD.ForegroundDpcStackListEntry is at 0x370 on Windows 11 */
/* FIXME: KTHREAD.InGlobalForegroundList is at 0x378 on Windows 11 */
C_ASSERT_FIELD(KTHREAD, 0x380, ReadOperationCount)
C_ASSERT_FIELD(KTHREAD, 0x388, WriteOperationCount)
C_ASSERT_FIELD(KTHREAD, 0x390, OtherOperationCount)
C_ASSERT_FIELD(KTHREAD, 0x398, ReadTransferCount)
C_ASSERT_FIELD(KTHREAD, 0x3A0, WriteTransferCount)
C_ASSERT_FIELD(KTHREAD, 0x3A8, OtherTransferCount)
C_ASSERT_FIELD(KTHREAD, 0x3B0, QueuedScb)
C_ASSERT_FIELD(KTHREAD, 0x3B8, ThreadTimerDelay)
C_ASSERT_FIELD(KTHREAD, 0x3BC, Spare26)
C_ASSERT_FIELD(KTHREAD, 0x3BE, PpmPolicy)
C_ASSERT_FIELD(KTHREAD, 0x3BF, Spare27)
C_ASSERT_FIELD(KTHREAD, 0x3C0, TracingPrivate)
C_ASSERT_FIELD(KTHREAD, 0x3C8, SchedulerAssist)
C_ASSERT_FIELD(KTHREAD, 0x3D0, AbWaitObject)
C_ASSERT_FIELD(KTHREAD, 0x3D8, ReservedPreviousReadyTimeValue)
C_ASSERT_FIELD(KTHREAD, 0x3E0, KernelWaitTime)
C_ASSERT_FIELD(KTHREAD, 0x3E8, UserWaitTime)
/* FIXME: KTHREAD.GlobalUpdateVpThreadPriorityListEntry is at 0x3F0 on Windows 11 */
/* FIXME: KTHREAD.UpdateVpThreadPriorityDpcStackListEntry is at 0x3F0 on Windows 11 */
/* FIXME: KTHREAD.InGlobalUpdateVpThreadPriorityList is at 0x3F8 on Windows 11 */
C_ASSERT_FIELD(KTHREAD, 0x400, SchedulerAssistPriorityFloor)
C_ASSERT_FIELD(KTHREAD, 0x404, RealtimePriorityFloor)
C_ASSERT_FIELD(KTHREAD, 0x408, KernelShadowStack)
C_ASSERT_FIELD(KTHREAD, 0x410, KernelShadowStackInitial)
C_ASSERT_FIELD(KTHREAD, 0x418, KernelShadowStackBase)
C_ASSERT_FIELD(KTHREAD, 0x420, KernelShadowStackLimit)
C_ASSERT_FIELD(KTHREAD, 0x428, ExtendedFeatureDisableMask)
C_ASSERT_FIELD(KTHREAD, 0x430, HgsFeedbackStartTime)
C_ASSERT_FIELD(KTHREAD, 0x438, HgsFeedbackCycles)
C_ASSERT_FIELD(KTHREAD, 0x440, HgsInvalidFeedbackCount)
C_ASSERT_FIELD(KTHREAD, 0x444, HgsLowerPerfClassFeedbackCount)
C_ASSERT_FIELD(KTHREAD, 0x448, HgsHigherPerfClassFeedbackCount)
C_ASSERT_FIELD(KTHREAD, 0x44C, ModeHistory)
C_ASSERT_FIELD(KTHREAD, 0x450, SystemAffinityTokenListHead)
C_ASSERT_FIELD(KTHREAD, 0x458, IptSaveArea)
C_ASSERT_FIELD(KTHREAD, 0x460, ResourceIndex)
C_ASSERT_FIELD(KTHREAD, 0x461, CoreIsolationReasons)
C_ASSERT_FIELD(KTHREAD, 0x462, BamQosLevelFromAssistPage)
C_ASSERT_FIELD(KTHREAD, 0x463, SecureCallCoreIsolationCount)
C_ASSERT_FIELD(KTHREAD, 0x464, SchedulerSharedOffset)
C_ASSERT_FIELD(KTHREAD, 0x468, SchedulerSharedSwappablePage)
C_ASSERT_FIELD(KTHREAD, 0x470, KernelAbEntries)
C_ASSERT_FIELD(KTHREAD, 0x478, UserAbEntries)
C_ASSERT_FIELD(KTHREAD, 0x480, KcsanThread)
C_ASSERT_FIELD(KTHREAD, 0x488, SchedulerAssistYieldCounter)
C_ASSERT_FIELD(KTHREAD, 0x48C, SchedulerAssistYieldBoostCount)
C_ASSERT_FIELD(KTHREAD, 0x490, SchedulerAssistLastYieldBoostTime)
/* FIXME: KTHREAD.Padding is at 0x498 on Windows 11 */

/* KPCR */
/* FIXME: sizeof(KPCR) is 0xD080 on Windows 11 */
/* FIXME: KPCR.NtTib is at 0x000 on Windows 11 */
/* FIXME: KPCR.GdtBase is at 0x000 on Windows 11 */
/* FIXME: KPCR.TssBase is at 0x008 on Windows 11 */
/* FIXME: KPCR.UserRsp is at 0x010 on Windows 11 */
/* FIXME: KPCR.Self is at 0x018 on Windows 11 */
/* FIXME: KPCR.CurrentPrcb is at 0x020 on Windows 11 */
/* FIXME: KPCR.LockArray is at 0x028 on Windows 11 */
/* FIXME: KPCR.Used_Self is at 0x030 on Windows 11 */
C_ASSERT_FIELD(KPCR, 0x038, IdtBase)
C_ASSERT_FIELD(KPCR, 0x040, Unused)
C_ASSERT_FIELD(KPCR, 0x050, Irql)
C_ASSERT_FIELD(KPCR, 0x051, SecondLevelCacheAssociativity)
C_ASSERT_FIELD(KPCR, 0x052, ObsoleteNumber)
C_ASSERT_FIELD(KPCR, 0x053, Fill0)
C_ASSERT_FIELD(KPCR, 0x054, Unused0)
C_ASSERT_FIELD(KPCR, 0x060, MajorVersion)
C_ASSERT_FIELD(KPCR, 0x062, MinorVersion)
C_ASSERT_FIELD(KPCR, 0x064, StallScaleFactor)
C_ASSERT_FIELD(KPCR, 0x068, Unused1)
C_ASSERT_FIELD(KPCR, 0x080, KernelReserved)
C_ASSERT_FIELD(KPCR, 0x0BC, SecondLevelCacheSize)
C_ASSERT_FIELD(KPCR, 0x0C0, HalReserved)
C_ASSERT_FIELD(KPCR, 0x100, Unused2)
C_ASSERT_FIELD(KPCR, 0x108, KdVersionBlock)
C_ASSERT_FIELD(KPCR, 0x110, Unused3)
C_ASSERT_FIELD(KPCR, 0x118, PcrAlign1)
/* FIXME: KPCR.Prcb is at 0x180 on Windows 11 */

/* KPRCB */
/* FIXME: sizeof(KPRCB) is 0xCF00 on Windows 11 */
C_ASSERT_FIELD(KPRCB, 0x000, MxCsr)
C_ASSERT_FIELD(KPRCB, 0x004, LegacyNumber)
C_ASSERT_FIELD(KPRCB, 0x005, ReservedMustBeZero)
C_ASSERT_FIELD(KPRCB, 0x006, InterruptRequest)
C_ASSERT_FIELD(KPRCB, 0x007, IdleHalt)
C_ASSERT_FIELD(KPRCB, 0x008, CurrentThread)
C_ASSERT_FIELD(KPRCB, 0x010, NextThread)
C_ASSERT_FIELD(KPRCB, 0x018, IdleThread)
C_ASSERT_FIELD(KPRCB, 0x020, NestingLevel)
C_ASSERT_FIELD(KPRCB, 0x021, ClockOwner)
C_ASSERT_FIELD(KPRCB, 0x022, PendingTickFlags)
/* FIXME: KPRCB.PendingTick is at 0x022 on Windows 11 */
C_ASSERT_FIELD(KPRCB, 0x023, IdleState)
C_ASSERT_FIELD(KPRCB, 0x024, Number)
C_ASSERT_FIELD(KPRCB, 0x028, RspBase)
C_ASSERT_FIELD(KPRCB, 0x030, PrcbLock)
C_ASSERT_FIELD(KPRCB, 0x038, PriorityState)
/* FIXME: KPRCB.CpuType is at 0x040 on Windows 11 */
/* FIXME: KPRCB.CpuID is at 0x041 on Windows 11 */
/* FIXME: KPRCB.CpuStep is at 0x042 on Windows 11 */
/* FIXME: KPRCB.CpuStepping is at 0x042 on Windows 11 */
/* FIXME: KPRCB.CpuModel is at 0x043 on Windows 11 */
/* FIXME: KPRCB.MHz is at 0x044 on Windows 11 */
/* FIXME: KPRCB.HalReserved is at 0x048 on Windows 11 */
/* FIXME: KPRCB.MinorVersion is at 0x088 on Windows 11 */
/* FIXME: KPRCB.MajorVersion is at 0x08A on Windows 11 */
/* FIXME: KPRCB.BuildType is at 0x08C on Windows 11 */
/* FIXME: KPRCB.CpuVendor is at 0x08D on Windows 11 */
/* FIXME: KPRCB.LegacyCoresPerPhysicalProcessor is at 0x08E on Windows 11 */
/* FIXME: KPRCB.LegacyLogicalProcessorsPerCore is at 0x08F on Windows 11 */
/* FIXME: KPRCB.TscFrequency is at 0x090 on Windows 11 */
/* FIXME: KPRCB.TracepointLog is at 0x098 on Windows 11 */
/* FIXME: KPRCB.CoresPerPhysicalProcessor is at 0x0A0 on Windows 11 */
/* FIXME: KPRCB.LogicalProcessorsPerCore is at 0x0A4 on Windows 11 */
/* FIXME: KPRCB.SelfIpiRequestSummary is at 0x0A8 on Windows 11 */
/* FIXME: KPRCB.QpcToTscIncrementShift is at 0x0AC on Windows 11 */
/* FIXME: KPRCB.PrcbPad03 is at 0x0AD on Windows 11 */
/* FIXME: KPRCB.QpcToTscIncrement is at 0x0B0 on Windows 11 */
/* FIXME: KPRCB.PrcbPad04 is at 0x0B8 on Windows 11 */
/* FIXME: KPRCB.SchedulerSubNode is at 0x0C0 on Windows 11 */
/* FIXME: KPRCB.GroupSetMember is at 0x0C8 on Windows 11 */
/* FIXME: KPRCB.Group is at 0x0D0 on Windows 11 */
/* FIXME: KPRCB.GroupIndex is at 0x0D1 on Windows 11 */
/* FIXME: KPRCB.PrcbPad05 is at 0x0D2 on Windows 11 */
/* FIXME: KPRCB.InitialApicId is at 0x0D4 on Windows 11 */
/* FIXME: KPRCB.ScbOffset is at 0x0D8 on Windows 11 */
/* FIXME: KPRCB.ApicMask is at 0x0DC on Windows 11 */
/* FIXME: KPRCB.AcpiReserved is at 0x0E0 on Windows 11 */
/* FIXME: KPRCB.CFlushSize is at 0x0E8 on Windows 11 */
/* FIXME: KPRCB.PrcbFlags is at 0x0EC on Windows 11 */
/* FIXME: KPRCB.PrcbFlagsReserved is at 0x0EC on Windows 11 */
/* FIXME: KPRCB.PrcbPad11 is at 0x0F0 on Windows 11 */
/* FIXME: KPRCB.ProcessorState is at 0x100 on Windows 11 */
/* FIXME: KPRCB.ExtendedSupervisorState is at 0x6C0 on Windows 11 */
/* FIXME: KPRCB.ProcessorSignature is at 0x6C8 on Windows 11 */
/* FIXME: KPRCB.ProcessorFlags is at 0x6CC on Windows 11 */
/* FIXME: KPRCB.TrappedSecurityDomain is at 0x6D0 on Windows 11 */
/* FIXME: KPRCB.PrcbPad12a is at 0x6D0 on Windows 11 */
/* FIXME: KPRCB.BpbState is at 0x6D8 on Windows 11 */
/* FIXME: KPRCB.BpbTrappedBpbState is at 0x6DA on Windows 11 */
/* FIXME: KPRCB.BpbFeatures is at 0x6DC on Windows 11 */
/* FIXME: KPRCB.PrcbPad12b is at 0x6DD on Windows 11 */
/* FIXME: KPRCB.BpbRetpolineState is at 0x6DE on Windows 11 */
/* FIXME: KPRCB.BpbRetpolineStateOverflow is at 0x6DF on Windows 11 */
/* FIXME: KPRCB.PairRegister is at 0x6E0 on Windows 11 */
/* FIXME: KPRCB.BpbRetpolineExitSpecCtrl is at 0x6E2 on Windows 11 */
/* FIXME: KPRCB.BpbTrappedRetpolineExitSpecCtrl is at 0x6E4 on Windows 11 */
/* FIXME: KPRCB.BpbCurrentSpecCtrl is at 0x6E6 on Windows 11 */
/* FIXME: KPRCB.BpbKernelSpecCtrl is at 0x6E8 on Windows 11 */
/* FIXME: KPRCB.BpbNmiSpecCtrl is at 0x6EA on Windows 11 */
/* FIXME: KPRCB.BpbUserSpecCtrl is at 0x6EC on Windows 11 */
/* FIXME: KPRCB.PrcbPad12c is at 0x6EE on Windows 11 */
/* FIXME: KPRCB.LockQueue is at 0x6F0 on Windows 11 */
/* FIXME: KPRCB.PPLookasideList is at 0x800 on Windows 11 */
/* FIXME: KPRCB.PPNxPagedLookasideList is at 0x900 on Windows 11 */
/* FIXME: KPRCB.PPNPagedLookasideList is at 0x1500 on Windows 11 */
/* FIXME: KPRCB.PPPagedLookasideList is at 0x2100 on Windows 11 */
/* FIXME: KPRCB.MsrIa32TsxCtrl is at 0x2D00 on Windows 11 */
/* FIXME: KPRCB.DeferredReadyListHead is at 0x2D08 on Windows 11 */
/* FIXME: KPRCB.MmPageFaultCount is at 0x2D10 on Windows 11 */
/* FIXME: KPRCB.MmCopyOnWriteCount is at 0x2D14 on Windows 11 */
/* FIXME: KPRCB.MmTransitionCount is at 0x2D18 on Windows 11 */
/* FIXME: KPRCB.MmDemandZeroCount is at 0x2D1C on Windows 11 */
/* FIXME: KPRCB.MmPageReadCount is at 0x2D20 on Windows 11 */
/* FIXME: KPRCB.MmPageReadIoCount is at 0x2D24 on Windows 11 */
/* FIXME: KPRCB.MmDirtyPagesWriteCount is at 0x2D28 on Windows 11 */
/* FIXME: KPRCB.MmDirtyWriteIoCount is at 0x2D2C on Windows 11 */
/* FIXME: KPRCB.MmMappedPagesWriteCount is at 0x2D30 on Windows 11 */
/* FIXME: KPRCB.MmMappedWriteIoCount is at 0x2D34 on Windows 11 */
/* FIXME: KPRCB.KeSystemCalls is at 0x2D38 on Windows 11 */
/* FIXME: KPRCB.KeContextSwitches is at 0x2D3C on Windows 11 */
/* FIXME: KPRCB.PrcbPad40 is at 0x2D40 on Windows 11 */
/* FIXME: KPRCB.CcFastReadNoWait is at 0x2D44 on Windows 11 */
/* FIXME: KPRCB.CcFastReadWait is at 0x2D48 on Windows 11 */
/* FIXME: KPRCB.CcFastReadNotPossible is at 0x2D4C on Windows 11 */
/* FIXME: KPRCB.CcCopyReadNoWait is at 0x2D50 on Windows 11 */
/* FIXME: KPRCB.CcCopyReadWait is at 0x2D54 on Windows 11 */
/* FIXME: KPRCB.CcCopyReadNoWaitMiss is at 0x2D58 on Windows 11 */
/* FIXME: KPRCB.IoReadOperationCount is at 0x2D5C on Windows 11 */
/* FIXME: KPRCB.IoWriteOperationCount is at 0x2D60 on Windows 11 */
/* FIXME: KPRCB.IoOtherOperationCount is at 0x2D64 on Windows 11 */
/* FIXME: KPRCB.IoReadTransferCount is at 0x2D68 on Windows 11 */
/* FIXME: KPRCB.IoWriteTransferCount is at 0x2D70 on Windows 11 */
/* FIXME: KPRCB.IoOtherTransferCount is at 0x2D78 on Windows 11 */
/* FIXME: KPRCB.PacketBarrier is at 0x2D80 on Windows 11 */
/* FIXME: KPRCB.TargetCount is at 0x2D84 on Windows 11 */
/* FIXME: KPRCB.IpiFrozen is at 0x2D88 on Windows 11 */
/* FIXME: KPRCB.PrcbPad30 is at 0x2D8C on Windows 11 */
/* FIXME: KPRCB.IsrDpcStats is at 0x2D90 on Windows 11 */
/* FIXME: KPRCB.DeviceInterrupts is at 0x2D98 on Windows 11 */
/* FIXME: KPRCB.LookasideIrpFloat is at 0x2D9C on Windows 11 */
/* FIXME: KPRCB.InterruptLastCount is at 0x2DA0 on Windows 11 */
/* FIXME: KPRCB.InterruptRate is at 0x2DA4 on Windows 11 */
/* FIXME: KPRCB.PrcbPad31 is at 0x2DA8 on Windows 11 */
/* FIXME: KPRCB.PairPrcb is at 0x2DB0 on Windows 11 */
/* FIXME: KPRCB.StaticAffinity is at 0x2DB8 on Windows 11 */
/* FIXME: KPRCB.DeferredDispatchInterrupts is at 0x35D8 on Windows 11 */
/* FIXME: KPRCB.StaticRescheduleContext is at 0x37E8 on Windows 11 */
/* FIXME: KPRCB.CpuPartition is at 0x37F0 on Windows 11 */
/* FIXME: KPRCB.PrcbPad35 is at 0x37F8 on Windows 11 */
/* FIXME: KPRCB.InterruptObjectPool is at 0x3800 on Windows 11 */
/* FIXME: KPRCB.DpcRuntimeHistoryHashTable is at 0x3810 on Windows 11 */
/* FIXME: KPRCB.DpcRuntimeHistoryHashTableCleanupDpc is at 0x3818 on Windows 11 */
/* FIXME: KPRCB.CurrentDpcRoutine is at 0x3820 on Windows 11 */
/* FIXME: KPRCB.CurrentDpcRuntimeHistoryCached is at 0x3828 on Windows 11 */
/* FIXME: KPRCB.CurrentDpcStartTime is at 0x3830 on Windows 11 */
/* FIXME: KPRCB.DpcDelegateThread is at 0x3838 on Windows 11 */
/* FIXME: KPRCB.DpcData is at 0x3840 on Windows 11 */
/* FIXME: KPRCB.DpcStack is at 0x38A0 on Windows 11 */
/* FIXME: KPRCB.MaximumDpcQueueDepth is at 0x38A8 on Windows 11 */
/* FIXME: KPRCB.DpcRequestRate is at 0x38AC on Windows 11 */
/* FIXME: KPRCB.MinimumDpcRate is at 0x38B0 on Windows 11 */
/* FIXME: KPRCB.DpcLastCount is at 0x38B4 on Windows 11 */
/* FIXME: KPRCB.ThreadDpcEnable is at 0x38B8 on Windows 11 */
/* FIXME: KPRCB.QuantumEnd is at 0x38B9 on Windows 11 */
/* FIXME: KPRCB.DpcRoutineActive is at 0x38BA on Windows 11 */
/* FIXME: KPRCB.IdleSchedule is at 0x38BB on Windows 11 */
/* FIXME: KPRCB.DpcRequestSummary is at 0x38BC on Windows 11 */
/* FIXME: KPRCB.DpcRequestSlot is at 0x38BC on Windows 11 */
/* FIXME: KPRCB.NormalDpcState is at 0x38BC on Windows 11 */
/* FIXME: KPRCB.DpcNormalProcessingActive is at 0x38BC on Windows 11 */
/* FIXME: KPRCB.DpcNormalSpare is at 0x38BD on Windows 11 */
/* FIXME: KPRCB.ThreadDpcState is at 0x38BE on Windows 11 */
/* FIXME: KPRCB.DpcThreadActive is at 0x38BE on Windows 11 */
/* FIXME: KPRCB.LastTick is at 0x38C0 on Windows 11 */
/* FIXME: KPRCB.ClockInterrupts is at 0x38C4 on Windows 11 */
/* FIXME: KPRCB.ReadyScanTick is at 0x38C8 on Windows 11 */
/* FIXME: KPRCB.SingleDpcSoftTimeLimitTicks is at 0x38CC on Windows 11 */
/* FIXME: KPRCB.SingleDpcSoftTimeoutEventInfo is at 0x38D0 on Windows 11 */
/* FIXME: KPRCB.CumulativeDpcSoftTimeLimitTicks is at 0x38D8 on Windows 11 */
/* FIXME: KPRCB.DpcWatchdogProfileBufferSize is at 0x38DC on Windows 11 */
/* FIXME: KPRCB.RcuData is at 0x38E0 on Windows 11 */
/* FIXME: KPRCB.InterruptObject is at 0x3900 on Windows 11 */
/* FIXME: KPRCB.TimerTable is at 0x4100 on Windows 11 */
/* FIXME: KPRCB.DpcLog is at 0x8318 on Windows 11 */
/* FIXME: KPRCB.DpcLogIndex is at 0x8320 on Windows 11 */
/* FIXME: KPRCB.DpcLogBufferSize is at 0x8324 on Windows 11 */
/* FIXME: KPRCB.PrcbPad92 is at 0x8328 on Windows 11 */
/* FIXME: KPRCB.DpcGate is at 0x8340 on Windows 11 */
/* FIXME: KPRCB.PrcbPad52 is at 0x8358 on Windows 11 */
/* FIXME: KPRCB.CallDpc is at 0x8360 on Windows 11 */
/* FIXME: KPRCB.ClockKeepAlive is at 0x83A0 on Windows 11 */
/* FIXME: KPRCB.PrcbPad60 is at 0x83A4 on Windows 11 */
/* FIXME: KPRCB.NmiActive is at 0x83A6 on Windows 11 */
/* FIXME: KPRCB.CombinedNmiMceActive is at 0x83A6 on Windows 11 */
/* FIXME: KPRCB.MceActive is at 0x83A7 on Windows 11 */
/* FIXME: KPRCB.DpcWatchdogPeriodTicks is at 0x83A8 on Windows 11 */
/* FIXME: KPRCB.DpcWatchdogCount is at 0x83AC on Windows 11 */
/* FIXME: KPRCB.KeSpinLockOrdering is at 0x83B0 on Windows 11 */
/* FIXME: KPRCB.DpcWatchdogProfileCumulativeDpcThresholdTicks is at 0x83B4 on Windows 11 */
/* FIXME: KPRCB.CachedPtes is at 0x83B8 on Windows 11 */
/* FIXME: KPRCB.WaitListHead is at 0x83C0 on Windows 11 */
/* FIXME: KPRCB.WaitLock is at 0x83D0 on Windows 11 */
/* FIXME: KPRCB.ReadySummary is at 0x83D8 on Windows 11 */
/* FIXME: KPRCB.AffinitizedSelectionMask is at 0x83DC on Windows 11 */
/* FIXME: KPRCB.QueueIndex is at 0x83E0 on Windows 11 */
/* FIXME: KPRCB.NormalPriorityQueueIndex is at 0x83E4 on Windows 11 */
/* FIXME: KPRCB.NormalPriorityReadyScanTick is at 0x83E8 on Windows 11 */
/* FIXME: KPRCB.DpcWatchdogSequenceNumber is at 0x83EC on Windows 11 */
/* FIXME: KPRCB.TimerExpirationDpc is at 0x83F0 on Windows 11 */
/* FIXME: KPRCB.ScbQueue is at 0x8430 on Windows 11 */
/* FIXME: KPRCB.DispatcherReadyListHead is at 0x8440 on Windows 11 */
/* FIXME: KPRCB.InterruptCount is at 0x8640 on Windows 11 */
/* FIXME: KPRCB.KernelTime is at 0x8644 on Windows 11 */
/* FIXME: KPRCB.UserTime is at 0x8648 on Windows 11 */
/* FIXME: KPRCB.DpcTime is at 0x864C on Windows 11 */
/* FIXME: KPRCB.InterruptTime is at 0x8650 on Windows 11 */
/* FIXME: KPRCB.AdjustDpcThreshold is at 0x8654 on Windows 11 */
/* FIXME: KPRCB.DebuggerSavedIRQL is at 0x8658 on Windows 11 */
/* FIXME: KPRCB.GroupSchedulingOverQuota is at 0x8659 on Windows 11 */
/* FIXME: KPRCB.DeepSleep is at 0x865A on Windows 11 */
/* FIXME: KPRCB.PrcbPad80 is at 0x865B on Windows 11 */
/* FIXME: KPRCB.DpcTimeCount is at 0x865C on Windows 11 */
/* FIXME: KPRCB.DpcTimeLimitTicks is at 0x8660 on Windows 11 */
/* FIXME: KPRCB.PeriodicCount is at 0x8664 on Windows 11 */
/* FIXME: KPRCB.PeriodicBias is at 0x8668 on Windows 11 */
/* FIXME: KPRCB.AvailableTime is at 0x866C on Windows 11 */
/* FIXME: KPRCB.KeExceptionDispatchCount is at 0x8670 on Windows 11 */
/* FIXME: KPRCB.ReadyThreadCount is at 0x8674 on Windows 11 */
/* FIXME: KPRCB.ReadyQueueExpectedRunTime is at 0x8678 on Windows 11 */
/* FIXME: KPRCB.StartCycles is at 0x8680 on Windows 11 */
/* FIXME: KPRCB.TaggedCycles is at 0x8688 on Windows 11 */
/* FIXME: KPRCB.AffinitizedCycles is at 0x86A8 on Windows 11 */
/* FIXME: KPRCB.CyclesByThreadType is at 0x86B0 on Windows 11 */
/* FIXME: KPRCB.CpuCycleScalingFactor is at 0x86B8 on Windows 11 */
/* FIXME: KPRCB.PerformanceScoreByClass is at 0x86BC on Windows 11 */
/* FIXME: KPRCB.EfficiencyScoreByClass is at 0x86CC on Windows 11 */
/* FIXME: KPRCB.CycleAccumulationInitialized is at 0x86DC on Windows 11 */
/* FIXME: KPRCB.PrcbPad83 is at 0x86DD on Windows 11 */
/* FIXME: KPRCB.MceRecoveryContext is at 0x86E0 on Windows 11 */
/* FIXME: KPRCB.ForceParkDutyCycleData is at 0x86F0 on Windows 11 */
/* FIXME: KPRCB.StartCyclesQpc is at 0x86F8 on Windows 11 */
/* FIXME: KPRCB.CycleTimeQpc is at 0x8700 on Windows 11 */
/* FIXME: KPRCB.NumberOfSecureFaults is at 0x8708 on Windows 11 */
/* FIXME: KPRCB.PrcbPad84 is at 0x8710 on Windows 11 */
/* FIXME: KPRCB.CachedStacks is at 0x8730 on Windows 11 */
/* FIXME: KPRCB.DpcWatchdogProfileSingleDpcThresholdTicks is at 0x8740 on Windows 11 */
/* FIXME: KPRCB.PrcbPad82 is at 0x8744 on Windows 11 */
/* FIXME: KPRCB.PageColor is at 0x8750 on Windows 11 */
/* FIXME: KPRCB.NodeColor is at 0x8754 on Windows 11 */
/* FIXME: KPRCB.PrcbPad87 is at 0x8758 on Windows 11 */
/* FIXME: KPRCB.PrcbPad81 is at 0x8760 on Windows 11 */
/* FIXME: KPRCB.SystemWorkKickInProgress is at 0x8765 on Windows 11 */
/* FIXME: KPRCB.ExceptionStackActive is at 0x8766 on Windows 11 */
/* FIXME: KPRCB.TbFlushListActive is at 0x8767 on Windows 11 */
/* FIXME: KPRCB.ExceptionStack is at 0x8768 on Windows 11 */
/* FIXME: KPRCB.MmSpinLockOrdering is at 0x8770 on Windows 11 */
/* FIXME: KPRCB.CycleTime is at 0x8778 on Windows 11 */
/* FIXME: KPRCB.Cycles is at 0x8780 on Windows 11 */
/* FIXME: KPRCB.CcFastMdlReadNoWait is at 0x87C0 on Windows 11 */
/* FIXME: KPRCB.CcFastMdlReadWait is at 0x87C4 on Windows 11 */
/* FIXME: KPRCB.CcFastMdlReadNotPossible is at 0x87C8 on Windows 11 */
/* FIXME: KPRCB.CcMapDataNoWait is at 0x87CC on Windows 11 */
/* FIXME: KPRCB.CcMapDataWait is at 0x87D0 on Windows 11 */
/* FIXME: KPRCB.CcPinMappedDataCount is at 0x87D4 on Windows 11 */
/* FIXME: KPRCB.CcPinReadNoWait is at 0x87D8 on Windows 11 */
/* FIXME: KPRCB.CcPinReadWait is at 0x87DC on Windows 11 */
/* FIXME: KPRCB.CcMdlReadNoWait is at 0x87E0 on Windows 11 */
/* FIXME: KPRCB.CcMdlReadWait is at 0x87E4 on Windows 11 */
/* FIXME: KPRCB.CcLazyWriteHotSpots is at 0x87E8 on Windows 11 */
/* FIXME: KPRCB.CcLazyWriteIos is at 0x87EC on Windows 11 */
/* FIXME: KPRCB.CcLazyWritePages is at 0x87F0 on Windows 11 */
/* FIXME: KPRCB.CcDataFlushes is at 0x87F4 on Windows 11 */
/* FIXME: KPRCB.CcDataPages is at 0x87F8 on Windows 11 */
/* FIXME: KPRCB.CcLostDelayedWrites is at 0x87FC on Windows 11 */
/* FIXME: KPRCB.CcFastReadResourceMiss is at 0x8800 on Windows 11 */
/* FIXME: KPRCB.CcCopyReadWaitMiss is at 0x8804 on Windows 11 */
/* FIXME: KPRCB.CcFastMdlReadResourceMiss is at 0x8808 on Windows 11 */
/* FIXME: KPRCB.CcMapDataNoWaitMiss is at 0x880C on Windows 11 */
/* FIXME: KPRCB.CcMapDataWaitMiss is at 0x8810 on Windows 11 */
/* FIXME: KPRCB.CcPinReadNoWaitMiss is at 0x8814 on Windows 11 */
/* FIXME: KPRCB.CcPinReadWaitMiss is at 0x8818 on Windows 11 */
/* FIXME: KPRCB.CcMdlReadNoWaitMiss is at 0x881C on Windows 11 */
/* FIXME: KPRCB.CcMdlReadWaitMiss is at 0x8820 on Windows 11 */
/* FIXME: KPRCB.CcReadAheadIos is at 0x8824 on Windows 11 */
/* FIXME: KPRCB.MmCacheTransitionCount is at 0x8828 on Windows 11 */
/* FIXME: KPRCB.MmCacheReadCount is at 0x882C on Windows 11 */
/* FIXME: KPRCB.MmCacheIoCount is at 0x8830 on Windows 11 */
/* FIXME: KPRCB.PrcbPad91 is at 0x8834 on Windows 11 */
/* FIXME: KPRCB.MmInternal is at 0x8838 on Windows 11 */
/* FIXME: KPRCB.PowerState is at 0x8840 on Windows 11 */
/* FIXME: KPRCB.PrcbPad96 is at 0x8A78 on Windows 11 */
/* FIXME: KPRCB.PrcbPad90 is at 0x8A80 on Windows 11 */
/* FIXME: KPRCB.ScbList is at 0x8A88 on Windows 11 */
/* FIXME: KPRCB.ForceIdleDpc is at 0x8A98 on Windows 11 */
/* FIXME: KPRCB.DpcWatchdogDpc is at 0x8AD8 on Windows 11 */
/* FIXME: KPRCB.PrcbPad98 is at 0x8B18 on Windows 11 */
/* FIXME: KPRCB.VmInternal is at 0x8B50 on Windows 11 */
/* FIXME: KPRCB.Cache is at 0x8B58 on Windows 11 */
/* FIXME: KPRCB.CacheCount is at 0x8B94 on Windows 11 */
/* FIXME: KPRCB.CachedCommit is at 0x8B98 on Windows 11 */
/* FIXME: KPRCB.CachedResidentAvailable is at 0x8B9C on Windows 11 */
/* FIXME: KPRCB.WheaInfo is at 0x8BA0 on Windows 11 */
/* FIXME: KPRCB.EtwSupport is at 0x8BA8 on Windows 11 */
/* FIXME: KPRCB.ExSaPageArray is at 0x8BB0 on Windows 11 */
/* FIXME: KPRCB.KeAlignmentFixupCount is at 0x8BB8 on Windows 11 */
/* FIXME: KPRCB.PrcbPad95 is at 0x8BBC on Windows 11 */
/* FIXME: KPRCB.HypercallPageList is at 0x8BC0 on Windows 11 */
/* FIXME: KPRCB.StatisticsPage is at 0x8BD0 on Windows 11 */
/* FIXME: KPRCB.GenerationTarget is at 0x8BD8 on Windows 11 */
/* FIXME: KPRCB.VirtualApicAssistPage is at 0x8BE0 on Windows 11 */
/* FIXME: KPRCB.PrcbPad85 is at 0x8BE8 on Windows 11 */
/* FIXME: KPRCB.HypercallCachedPages is at 0x8C00 on Windows 11 */
/* FIXME: KPRCB.VirtualApicAssist is at 0x8C08 on Windows 11 */
/* FIXME: KPRCB.PackageProcessorSet is at 0x8C10 on Windows 11 */
/* FIXME: KPRCB.PrcbPad86 is at 0x8D18 on Windows 11 */
/* FIXME: KPRCB.ProcessorId is at 0x8E18 on Windows 11 */
/* FIXME: KPRCB.TopologyId is at 0x8E18 on Windows 11 */
/* FIXME: KPRCB.CoreId is at 0x8E1C on Windows 11 */
/* FIXME: KPRCB.ModuleId is at 0x8E20 on Windows 11 */
/* FIXME: KPRCB.DieId is at 0x8E24 on Windows 11 */
/* FIXME: KPRCB.PackageId is at 0x8E28 on Windows 11 */
/* FIXME: KPRCB.ComplexId is at 0x8E2C on Windows 11 */
/* FIXME: KPRCB.PrcbPad101 is at 0x8E30 on Windows 11 */
/* FIXME: KPRCB.SharedReadyQueueMask is at 0x8E40 on Windows 11 */
/* FIXME: KPRCB.SharedReadyQueue is at 0x8E48 on Windows 11 */
/* FIXME: KPRCB.SharedQueueScanOwner is at 0x8E50 on Windows 11 */
/* FIXME: KPRCB.ScanSiblingIndex is at 0x8E54 on Windows 11 */
/* FIXME: KPRCB.CoreControlBlock is at 0x8E58 on Windows 11 */
/* FIXME: KPRCB.CoreProcessorSet is at 0x8E60 on Windows 11 */
/* FIXME: KPRCB.ScanSiblingMask is at 0x8E68 on Windows 11 */
/* FIXME: KPRCB.LLCMask is at 0x8E70 on Windows 11 */
/* FIXME: KPRCB.GroupModuleProcessorSet is at 0x8E78 on Windows 11 */
/* FIXME: KPRCB.SmtIsolationThread is at 0x8E80 on Windows 11 */
/* FIXME: KPRCB.PrcbPad97 is at 0x8E88 on Windows 11 */
/* FIXME: KPRCB.ProcessorProfileControlArea is at 0x8E98 on Windows 11 */
/* FIXME: KPRCB.ProfileEventIndexAddress is at 0x8EA0 on Windows 11 */
/* FIXME: KPRCB.DpcWatchdogProfile is at 0x8EA8 on Windows 11 */
/* FIXME: KPRCB.DpcWatchdogProfileCurrentEmptyCapture is at 0x8EB0 on Windows 11 */
/* FIXME: KPRCB.SchedulerAssist is at 0x8EB8 on Windows 11 */
/* FIXME: KPRCB.SynchCounters is at 0x8EC0 on Windows 11 */
/* FIXME: KPRCB.PrcbPad94 is at 0x8F78 on Windows 11 */
/* FIXME: KPRCB.FsCounters is at 0x8F80 on Windows 11 */
/* FIXME: KPRCB.VendorString is at 0x8F90 on Windows 11 */
/* FIXME: KPRCB.PrcbPad100 is at 0x8F9D on Windows 11 */
/* FIXME: KPRCB.FeatureBits is at 0x8FA0 on Windows 11 */
/* FIXME: KPRCB.UpdateSignature is at 0x8FA8 on Windows 11 */
/* FIXME: KPRCB.PteBitCache is at 0x8FB0 on Windows 11 */
/* FIXME: KPRCB.PteBitOffset is at 0x8FB8 on Windows 11 */
/* FIXME: KPRCB.PrcbPad105 is at 0x8FBC on Windows 11 */
/* FIXME: KPRCB.Context is at 0x8FC0 on Windows 11 */
/* FIXME: KPRCB.ContextFlagsInit is at 0x8FC8 on Windows 11 */
/* FIXME: KPRCB.PrcbPad115 is at 0x8FCC on Windows 11 */
/* FIXME: KPRCB.ExtendedState is at 0x8FD0 on Windows 11 */
/* FIXME: KPRCB.IsrStack is at 0x8FD8 on Windows 11 */
/* FIXME: KPRCB.EntropyTimingState is at 0x8FE0 on Windows 11 */
/* FIXME: KPRCB.StibpPairingTrace is at 0x9138 on Windows 11 */
/* FIXME: KPRCB.AbSelfIoBoostsList is at 0x9170 on Windows 11 */
/* FIXME: KPRCB.AbPropagateBoostsList is at 0x9178 on Windows 11 */
/* FIXME: KPRCB.AbDpc is at 0x9180 on Windows 11 */
/* FIXME: KPRCB.SymCryptEntropyAccumulatorState is at 0x91C0 on Windows 11 */
/* FIXME: KPRCB.IoIrpStackProfilerCurrent is at 0x9340 on Windows 11 */
/* FIXME: KPRCB.IoIrpStackProfilerPrevious is at 0x9394 on Windows 11 */
/* FIXME: KPRCB.SecureFault is at 0x93E8 on Windows 11 */
/* FIXME: KPRCB.LocalSharedReadyQueue is at 0x9400 on Windows 11 */
/* FIXME: KPRCB.LocalSearchContexts is at 0x9408 on Windows 11 */
/* FIXME: KPRCB.SearchContexts is at 0x9418 on Windows 11 */
/* FIXME: KPRCB.SearchGenerations is at 0x9428 on Windows 11 */
/* FIXME: KPRCB.PrcbPad125 is at 0x9438 on Windows 11 */
/* FIXME: KPRCB.TimerExpirationTraceCount is at 0x9440 on Windows 11 */
/* FIXME: KPRCB.PrcbPad127 is at 0x9444 on Windows 11 */
/* FIXME: KPRCB.TimerExpirationTrace is at 0x9448 on Windows 11 */
/* FIXME: KPRCB.PrcbPad128 is at 0x9548 on Windows 11 */
/* FIXME: KPRCB.ClockTimerState is at 0x9580 on Windows 11 */
/* FIXME: KPRCB.IpiFrame is at 0x9A98 on Windows 11 */
/* FIXME: KPRCB.PrcbPad129 is at 0x9AA0 on Windows 11 */
/* FIXME: KPRCB.Mailbox is at 0x9AC0 on Windows 11 */
/* FIXME: KPRCB.PrcbPad130 is at 0x9AC8 on Windows 11 */
/* FIXME: KPRCB.McheckContext is at 0x9B00 on Windows 11 */
/* FIXME: KPRCB.TransitionShadowStack is at 0x9BA0 on Windows 11 */
/* FIXME: KPRCB.KernelShadowStackInitial is at 0x9BA8 on Windows 11 */
/* FIXME: KPRCB.IstShadowStacksTable is at 0x9BB0 on Windows 11 */
/* FIXME: KPRCB.HiberSwapShadowStacks is at 0x9BB8 on Windows 11 */
/* FIXME: KPRCB.HiberSwapShadowStackBases is at 0x9BE0 on Windows 11 */
/* FIXME: KPRCB.MmFaultCompletionInfo is at 0x9C08 on Windows 11 */
/* FIXME: KPRCB.PrcbPad132 is at 0x9C10 on Windows 11 */
/* FIXME: KPRCB.SelfmapLockHandle is at 0x9C40 on Windows 11 */
/* FIXME: KPRCB.PrcbPad134b is at 0x9CA0 on Windows 11 */
/* FIXME: KPRCB.DieProcessorSet is at 0x9CC0 on Windows 11 */
/* FIXME: KPRCB.PrcbPad135 is at 0x9DC8 on Windows 11 */
/* FIXME: KPRCB.CoresPerPhysicalDie is at 0x9EC8 on Windows 11 */
/* FIXME: KPRCB.LogicalProcessorsPerModule is at 0x9ECC on Windows 11 */
/* FIXME: KPRCB.FreezeForRecoveryDpc is at 0x9ED0 on Windows 11 */
/* FIXME: KPRCB.ModuleProcessorSet is at 0x9F10 on Windows 11 */
/* FIXME: KPRCB.PrcbPad136 is at 0xA018 on Windows 11 */
/* FIXME: KPRCB.LocalCoreControlBlock is at 0xA118 on Windows 11 */
/* FIXME: KPRCB.PrcbPad137a is at 0xA148 on Windows 11 */
/* FIXME: KPRCB.KstackFreeDpc is at 0xA150 on Windows 11 */
/* FIXME: KPRCB.SlistRollbackDpc is at 0xA190 on Windows 11 */
/* FIXME: KPRCB.KstackFreeList is at 0xA1D0 on Windows 11 */
/* FIXME: KPRCB.IsrStackNesting is at 0xA1E0 on Windows 11 */
/* FIXME: KPRCB.PrcbPad138 is at 0xA1E4 on Windows 11 */
/* FIXME: KPRCB.KernelDirectoryTableBase is at 0xAE80 on Windows 11 */
/* FIXME: KPRCB.RspBaseShadow is at 0xAE88 on Windows 11 */
/* FIXME: KPRCB.UserRspShadow is at 0xAE90 on Windows 11 */
/* FIXME: KPRCB.ShadowFlags is at 0xAE98 on Windows 11 */
/* FIXME: KPRCB.PrcbPad138b is at 0xAE9C on Windows 11 */
/* FIXME: KPRCB.PrcbPad138c is at 0xAEA0 on Windows 11 */
/* FIXME: KPRCB.PrcbPad138d is at 0xAEA8 on Windows 11 */
/* FIXME: KPRCB.VerwSelector is at 0xAEAA on Windows 11 */
/* FIXME: KPRCB.DbgMceNestingLevel is at 0xAEAC on Windows 11 */
/* FIXME: KPRCB.DbgMceFlags is at 0xAEB0 on Windows 11 */
/* FIXME: KPRCB.CoreControlBlockIndex is at 0xAEB4 on Windows 11 */
/* FIXME: KPRCB.CoreControlBlockShadow is at 0xAEB8 on Windows 11 */
/* FIXME: KPRCB.LocalCoreControlBlockShadow is at 0xAEC0 on Windows 11 */
/* FIXME: KPRCB.CacheProcessorSet is at 0xAF00 on Windows 11 */
/* FIXME: KPRCB.PrcbPad139 is at 0xB428 on Windows 11 */
/* FIXME: KPRCB.NodeRelativeTopologyIndex is at 0xB928 on Windows 11 */
/* FIXME: KPRCB.ComplexProcessorSet is at 0xB940 on Windows 11 */
/* FIXME: KPRCB.PrcbPad139b is at 0xBA48 on Windows 11 */
/* FIXME: KPRCB.LogicalProcessorsPerComplex is at 0xBB48 on Windows 11 */
/* FIXME: KPRCB.PrcbPad139c is at 0xBB4C on Windows 11 */
/* FIXME: KPRCB.PrcbPad140 is at 0xBB50 on Windows 11 */
/* FIXME: KPRCB.PrcbPad140a is at 0xBE80 on Windows 11 */
/* FIXME: KPRCB.PrcbPad141 is at 0xBEC0 on Windows 11 */
/* FIXME: KPRCB.RequestMailbox is at 0xCEC0 on Windows 11 */

/* TEB */
C_ASSERT(sizeof(TEB) == 0x1878);
C_ASSERT_FIELD(TEB, 0x000, NtTib)
C_ASSERT_FIELD(TEB, 0x038, EnvironmentPointer)
C_ASSERT_FIELD(TEB, 0x040, ClientId)
C_ASSERT_FIELD(TEB, 0x050, ActiveRpcHandle)
C_ASSERT_FIELD(TEB, 0x058, ThreadLocalStoragePointer)
C_ASSERT_FIELD(TEB, 0x060, ProcessEnvironmentBlock)
C_ASSERT_FIELD(TEB, 0x068, LastErrorValue)
C_ASSERT_FIELD(TEB, 0x06C, CountOfOwnedCriticalSections)
C_ASSERT_FIELD(TEB, 0x070, CsrClientThread)
C_ASSERT_FIELD(TEB, 0x078, Win32ThreadInfo)
C_ASSERT_FIELD(TEB, 0x080, User32Reserved)
C_ASSERT_FIELD(TEB, 0x0E8, UserReserved)
C_ASSERT_FIELD(TEB, 0x100, WOW32Reserved)
C_ASSERT_FIELD(TEB, 0x108, CurrentLocale)
C_ASSERT_FIELD(TEB, 0x10C, FpSoftwareStatusRegister)
C_ASSERT_FIELD(TEB, 0x110, ReservedForDebuggerInstrumentation)
C_ASSERT_FIELD(TEB, 0x190, SystemReserved1)
/* FIXME: TEB.HeapFlsData is at 0x258 on Windows 11 */
/* FIXME: TEB.RngState is at 0x260 on Windows 11 */
/* FIXME: TEB.PlaceholderCompatibilityMode is at 0x280 on Windows 11 */
/* FIXME: TEB.PlaceholderHydrationAlwaysExplicit is at 0x281 on Windows 11 */
/* FIXME: TEB.PlaceholderReserved is at 0x282 on Windows 11 */
/* FIXME: TEB.ProxiedProcessId is at 0x28C on Windows 11 */
/* FIXME: TEB._ActivationStack is at 0x290 on Windows 11 */
/* FIXME: TEB.WorkingOnBehalfTicket is at 0x2B8 on Windows 11 */
C_ASSERT_FIELD(TEB, 0x2C0, ExceptionCode)
C_ASSERT_FIELD(TEB, 0x2C4, Padding0)
C_ASSERT_FIELD(TEB, 0x2C8, ActivationContextStackPointer)
C_ASSERT_FIELD(TEB, 0x2D0, InstrumentationCallbackSp)
C_ASSERT_FIELD(TEB, 0x2D8, InstrumentationCallbackPreviousPc)
C_ASSERT_FIELD(TEB, 0x2E0, InstrumentationCallbackPreviousSp)
C_ASSERT_FIELD(TEB, 0x2E8, TxFsContext)
C_ASSERT_FIELD(TEB, 0x2EC, InstrumentationCallbackDisabled)
C_ASSERT_FIELD(TEB, 0x2ED, UnalignedLoadStoreExceptions)
C_ASSERT_FIELD(TEB, 0x2EE, Padding1)
C_ASSERT_FIELD(TEB, 0x2F0, GdiTebBatch)
C_ASSERT_FIELD(TEB, 0x7D8, RealClientId)
C_ASSERT_FIELD(TEB, 0x7E8, GdiCachedProcessHandle)
C_ASSERT_FIELD(TEB, 0x7F0, GdiClientPID)
C_ASSERT_FIELD(TEB, 0x7F4, GdiClientTID)
C_ASSERT_FIELD(TEB, 0x7F8, GdiThreadLocalInfo)
C_ASSERT_FIELD(TEB, 0x800, Win32ClientInfo)
C_ASSERT_FIELD(TEB, 0x9F0, glDispatchTable)
C_ASSERT_FIELD(TEB, 0x1138, glReserved1)
C_ASSERT_FIELD(TEB, 0x1220, glReserved2)
C_ASSERT_FIELD(TEB, 0x1228, glSectionInfo)
C_ASSERT_FIELD(TEB, 0x1230, glSection)
C_ASSERT_FIELD(TEB, 0x1238, glTable)
C_ASSERT_FIELD(TEB, 0x1240, glCurrentRC)
C_ASSERT_FIELD(TEB, 0x1248, glContext)
C_ASSERT_FIELD(TEB, 0x1250, LastStatusValue)
C_ASSERT_FIELD(TEB, 0x1254, Padding2)
C_ASSERT_FIELD(TEB, 0x1258, StaticUnicodeString)
C_ASSERT_FIELD(TEB, 0x1268, StaticUnicodeBuffer)
C_ASSERT_FIELD(TEB, 0x1472, Padding3)
C_ASSERT_FIELD(TEB, 0x1478, DeallocationStack)
C_ASSERT_FIELD(TEB, 0x1480, TlsSlots)
C_ASSERT_FIELD(TEB, 0x1680, TlsLinks)
C_ASSERT_FIELD(TEB, 0x1690, Vdm)
C_ASSERT_FIELD(TEB, 0x1698, ReservedForNtRpc)
C_ASSERT_FIELD(TEB, 0x16A0, DbgSsReserved)
C_ASSERT_FIELD(TEB, 0x16B0, HardErrorMode)
C_ASSERT_FIELD(TEB, 0x16B4, Padding4)
C_ASSERT_FIELD(TEB, 0x16B8, Instrumentation)
C_ASSERT_FIELD(TEB, 0x1710, ActivityId)
C_ASSERT_FIELD(TEB, 0x1720, SubProcessTag)
C_ASSERT_FIELD(TEB, 0x1728, PerflibData)
C_ASSERT_FIELD(TEB, 0x1730, EtwTraceData)
C_ASSERT_FIELD(TEB, 0x1738, WinSockData)
C_ASSERT_FIELD(TEB, 0x1740, GdiBatchCount)
/* FIXME: TEB.CurrentIdealProcessor is at 0x1744 on Windows 11 */
/* FIXME: TEB.IdealProcessorValue is at 0x1744 on Windows 11 */
/* FIXME: TEB.ReservedPad0 is at 0x1744 on Windows 11 */
/* FIXME: TEB.ReservedPad1 is at 0x1745 on Windows 11 */
/* FIXME: TEB.ReservedPad2 is at 0x1746 on Windows 11 */
/* FIXME: TEB.IdealProcessor is at 0x1747 on Windows 11 */
C_ASSERT_FIELD(TEB, 0x1748, GuaranteedStackBytes)
C_ASSERT_FIELD(TEB, 0x174C, Padding5)
C_ASSERT_FIELD(TEB, 0x1750, ReservedForPerf)
C_ASSERT_FIELD(TEB, 0x1758, ReservedForOle)
C_ASSERT_FIELD(TEB, 0x1760, WaitingOnLoaderLock)
C_ASSERT_FIELD(TEB, 0x1764, Padding6)
C_ASSERT_FIELD(TEB, 0x1768, SavedPriorityState)
C_ASSERT_FIELD(TEB, 0x1770, ReservedForCodeCoverage)
C_ASSERT_FIELD(TEB, 0x1778, ThreadPoolData)
C_ASSERT_FIELD(TEB, 0x1780, TlsExpansionSlots)
/* FIXME: TEB.ChpeV2CpuAreaInfo is at 0x1788 on Windows 11 */
/* FIXME: TEB.Unused is at 0x1790 on Windows 11 */
C_ASSERT_FIELD(TEB, 0x1798, MuiGeneration)
C_ASSERT_FIELD(TEB, 0x179C, IsImpersonating)
C_ASSERT_FIELD(TEB, 0x17A0, NlsCache)
C_ASSERT_FIELD(TEB, 0x17A8, pShimData)
/* FIXME: TEB.HeapData is at 0x17B0 on Windows 11 */
C_ASSERT_FIELD(TEB, 0x17B4, Padding7)
C_ASSERT_FIELD(TEB, 0x17B8, CurrentTransactionHandle)
C_ASSERT_FIELD(TEB, 0x17C0, ActiveFrame)
C_ASSERT_FIELD(TEB, 0x17C8, FlsData)
C_ASSERT_FIELD(TEB, 0x17D0, PreferredLanguages)
C_ASSERT_FIELD(TEB, 0x17D8, UserPrefLanguages)
C_ASSERT_FIELD(TEB, 0x17E0, MergedPrefLanguages)
C_ASSERT_FIELD(TEB, 0x17E8, MuiImpersonation)
/* FIXME: TEB.CrossTebFlags is at 0x17EC on Windows 11 */
/* FIXME: TEB.SpareCrossTebBits is at 0x17EC on Windows 11 */
/* FIXME: TEB.SameTebFlags is at 0x17EE on Windows 11 */
/* FIXME: TEB.SafeThunkCall is at 0x17EE on Windows 11 */
/* FIXME: TEB.DisableUserStackWalk is at 0x17EF on Windows 11 */
C_ASSERT_FIELD(TEB, 0x17F0, TxnScopeEnterCallback)
C_ASSERT_FIELD(TEB, 0x17F8, TxnScopeExitCallback)
C_ASSERT_FIELD(TEB, 0x1800, TxnScopeContext)
C_ASSERT_FIELD(TEB, 0x1808, LockCount)
C_ASSERT_FIELD(TEB, 0x180C, WowTebOffset)
C_ASSERT_FIELD(TEB, 0x1810, ResourceRetValue)
C_ASSERT_FIELD(TEB, 0x1818, ReservedForWdf)
C_ASSERT_FIELD(TEB, 0x1820, ReservedForCrt)
C_ASSERT_FIELD(TEB, 0x1828, EffectiveContainerId)
C_ASSERT_FIELD(TEB, 0x1838, LastSleepCounter)
C_ASSERT_FIELD(TEB, 0x1840, SpinCallCount)
/* FIXME: TEB.Padding8 is at 0x1844 on Windows 11 */
C_ASSERT_FIELD(TEB, 0x1848, ExtendedFeatureDisableMask)
C_ASSERT_FIELD(TEB, 0x1850, SchedulerSharedDataSlot)
C_ASSERT_FIELD(TEB, 0x1858, HeapWalkContext)
C_ASSERT_FIELD(TEB, 0x1860, PrimaryGroupAffinity)
C_ASSERT_FIELD(TEB, 0x1870, Rcu)

/* TEB32 */
C_ASSERT(sizeof(TEB32) == 0x1038);
C_ASSERT_FIELD(TEB32, 0x000, NtTib)
C_ASSERT_FIELD(TEB32, 0x01C, EnvironmentPointer)
C_ASSERT_FIELD(TEB32, 0x020, ClientId)
C_ASSERT_FIELD(TEB32, 0x028, ActiveRpcHandle)
C_ASSERT_FIELD(TEB32, 0x02C, ThreadLocalStoragePointer)
C_ASSERT_FIELD(TEB32, 0x030, ProcessEnvironmentBlock)
C_ASSERT_FIELD(TEB32, 0x034, LastErrorValue)
C_ASSERT_FIELD(TEB32, 0x038, CountOfOwnedCriticalSections)
C_ASSERT_FIELD(TEB32, 0x03C, CsrClientThread)
C_ASSERT_FIELD(TEB32, 0x040, Win32ThreadInfo)
C_ASSERT_FIELD(TEB32, 0x044, User32Reserved)
C_ASSERT_FIELD(TEB32, 0x0AC, UserReserved)
C_ASSERT_FIELD(TEB32, 0x0C0, WOW32Reserved)
C_ASSERT_FIELD(TEB32, 0x0C4, CurrentLocale)
C_ASSERT_FIELD(TEB32, 0x0C8, FpSoftwareStatusRegister)
C_ASSERT_FIELD(TEB32, 0x0CC, ReservedForDebuggerInstrumentation)
C_ASSERT_FIELD(TEB32, 0x10C, SystemReserved1)
/* FIXME: TEB32.HeapFlsData is at 0x160 on Windows 11 */
/* FIXME: TEB32.RngState is at 0x164 on Windows 11 */
/* FIXME: TEB32.PlaceholderCompatibilityMode is at 0x174 on Windows 11 */
/* FIXME: TEB32.PlaceholderHydrationAlwaysExplicit is at 0x175 on Windows 11 */
/* FIXME: TEB32.PlaceholderReserved is at 0x176 on Windows 11 */
/* FIXME: TEB32.ProxiedProcessId is at 0x180 on Windows 11 */
/* FIXME: TEB32._ActivationStack is at 0x184 on Windows 11 */
/* FIXME: TEB32.WorkingOnBehalfTicket is at 0x19C on Windows 11 */
C_ASSERT_FIELD(TEB32, 0x1A4, ExceptionCode)
C_ASSERT_FIELD(TEB32, 0x1A8, ActivationContextStackPointer)
C_ASSERT_FIELD(TEB32, 0x1AC, InstrumentationCallbackSp)
C_ASSERT_FIELD(TEB32, 0x1B0, InstrumentationCallbackPreviousPc)
C_ASSERT_FIELD(TEB32, 0x1B4, InstrumentationCallbackPreviousSp)
C_ASSERT_FIELD(TEB32, 0x1B8, InstrumentationCallbackDisabled)
C_ASSERT_FIELD(TEB32, 0x1B9, SpareBytes)
C_ASSERT_FIELD(TEB32, 0x1D0, TxFsContext)
C_ASSERT_FIELD(TEB32, 0x1D4, GdiTebBatch)
C_ASSERT_FIELD(TEB32, 0x6B4, RealClientId)
C_ASSERT_FIELD(TEB32, 0x6BC, GdiCachedProcessHandle)
C_ASSERT_FIELD(TEB32, 0x6C0, GdiClientPID)
C_ASSERT_FIELD(TEB32, 0x6C4, GdiClientTID)
C_ASSERT_FIELD(TEB32, 0x6C8, GdiThreadLocalInfo)
C_ASSERT_FIELD(TEB32, 0x6CC, Win32ClientInfo)
C_ASSERT_FIELD(TEB32, 0x7C4, glDispatchTable)
C_ASSERT_FIELD(TEB32, 0xB68, glReserved1)
C_ASSERT_FIELD(TEB32, 0xBDC, glReserved2)
C_ASSERT_FIELD(TEB32, 0xBE0, glSectionInfo)
C_ASSERT_FIELD(TEB32, 0xBE4, glSection)
C_ASSERT_FIELD(TEB32, 0xBE8, glTable)
C_ASSERT_FIELD(TEB32, 0xBEC, glCurrentRC)
C_ASSERT_FIELD(TEB32, 0xBF0, glContext)
C_ASSERT_FIELD(TEB32, 0xBF4, LastStatusValue)
C_ASSERT_FIELD(TEB32, 0xBF8, StaticUnicodeString)
C_ASSERT_FIELD(TEB32, 0xC00, StaticUnicodeBuffer)
C_ASSERT_FIELD(TEB32, 0xE0C, DeallocationStack)
C_ASSERT_FIELD(TEB32, 0xE10, TlsSlots)
C_ASSERT_FIELD(TEB32, 0xF10, TlsLinks)
C_ASSERT_FIELD(TEB32, 0xF18, Vdm)
C_ASSERT_FIELD(TEB32, 0xF1C, ReservedForNtRpc)
C_ASSERT_FIELD(TEB32, 0xF20, DbgSsReserved)
C_ASSERT_FIELD(TEB32, 0xF28, HardErrorMode)
C_ASSERT_FIELD(TEB32, 0xF2C, Instrumentation)
C_ASSERT_FIELD(TEB32, 0xF50, ActivityId)
C_ASSERT_FIELD(TEB32, 0xF60, SubProcessTag)
C_ASSERT_FIELD(TEB32, 0xF64, PerflibData)
C_ASSERT_FIELD(TEB32, 0xF68, EtwTraceData)
C_ASSERT_FIELD(TEB32, 0xF6C, WinSockData)
C_ASSERT_FIELD(TEB32, 0xF70, GdiBatchCount)
/* FIXME: TEB32.CurrentIdealProcessor is at 0xF74 on Windows 11 */
/* FIXME: TEB32.IdealProcessorValue is at 0xF74 on Windows 11 */
/* FIXME: TEB32.ReservedPad0 is at 0xF74 on Windows 11 */
/* FIXME: TEB32.ReservedPad1 is at 0xF75 on Windows 11 */
/* FIXME: TEB32.ReservedPad2 is at 0xF76 on Windows 11 */
/* FIXME: TEB32.IdealProcessor is at 0xF77 on Windows 11 */
C_ASSERT_FIELD(TEB32, 0xF78, GuaranteedStackBytes)
C_ASSERT_FIELD(TEB32, 0xF7C, ReservedForPerf)
C_ASSERT_FIELD(TEB32, 0xF80, ReservedForOle)
C_ASSERT_FIELD(TEB32, 0xF84, WaitingOnLoaderLock)
C_ASSERT_FIELD(TEB32, 0xF88, SavedPriorityState)
C_ASSERT_FIELD(TEB32, 0xF8C, ReservedForCodeCoverage)
C_ASSERT_FIELD(TEB32, 0xF90, ThreadPoolData)
C_ASSERT_FIELD(TEB32, 0xF94, TlsExpansionSlots)
C_ASSERT_FIELD(TEB32, 0xF98, MuiGeneration)
C_ASSERT_FIELD(TEB32, 0xF9C, IsImpersonating)
C_ASSERT_FIELD(TEB32, 0xFA0, NlsCache)
C_ASSERT_FIELD(TEB32, 0xFA4, pShimData)
/* FIXME: TEB32.HeapData is at 0xFA8 on Windows 11 */
C_ASSERT_FIELD(TEB32, 0xFAC, CurrentTransactionHandle)
C_ASSERT_FIELD(TEB32, 0xFB0, ActiveFrame)
C_ASSERT_FIELD(TEB32, 0xFB4, FlsData)
C_ASSERT_FIELD(TEB32, 0xFB8, PreferredLanguages)
C_ASSERT_FIELD(TEB32, 0xFBC, UserPrefLanguages)
C_ASSERT_FIELD(TEB32, 0xFC0, MergedPrefLanguages)
C_ASSERT_FIELD(TEB32, 0xFC4, MuiImpersonation)
/* FIXME: TEB32.CrossTebFlags is at 0xFC8 on Windows 11 */
/* FIXME: TEB32.SpareCrossTebBits is at 0xFC8 on Windows 11 */
/* FIXME: TEB32.SameTebFlags is at 0xFCA on Windows 11 */
/* FIXME: TEB32.SafeThunkCall is at 0xFCA on Windows 11 */
/* FIXME: TEB32.DisableUserStackWalk is at 0xFCB on Windows 11 */
C_ASSERT_FIELD(TEB32, 0xFCC, TxnScopeEnterCallback)
C_ASSERT_FIELD(TEB32, 0xFD0, TxnScopeExitCallback)
C_ASSERT_FIELD(TEB32, 0xFD4, TxnScopeContext)
C_ASSERT_FIELD(TEB32, 0xFD8, LockCount)
C_ASSERT_FIELD(TEB32, 0xFDC, WowTebOffset)
C_ASSERT_FIELD(TEB32, 0xFE0, ResourceRetValue)
C_ASSERT_FIELD(TEB32, 0xFE4, ReservedForWdf)
C_ASSERT_FIELD(TEB32, 0xFE8, ReservedForCrt)
C_ASSERT_FIELD(TEB32, 0xFF0, EffectiveContainerId)
C_ASSERT_FIELD(TEB32, 0x1000, LastSleepCounter)
C_ASSERT_FIELD(TEB32, 0x1008, SpinCallCount)
C_ASSERT_FIELD(TEB32, 0x1010, ExtendedFeatureDisableMask)
C_ASSERT_FIELD(TEB32, 0x1018, SchedulerSharedDataSlot)
C_ASSERT_FIELD(TEB32, 0x101C, HeapWalkContext)
C_ASSERT_FIELD(TEB32, 0x1020, PrimaryGroupAffinity)
C_ASSERT_FIELD(TEB32, 0x102C, Rcu)

/* PEB */
C_ASSERT(sizeof(PEB) == 0x7D0);
C_ASSERT_FIELD(PEB, 0x000, InheritedAddressSpace)
C_ASSERT_FIELD(PEB, 0x001, ReadImageFileExecOptions)
C_ASSERT_FIELD(PEB, 0x002, BeingDebugged)
/* FIXME: PEB.BitField is at 0x003 on Windows 11 */
/* FIXME: PEB.ImageUsesLargePages is at 0x003 on Windows 11 */
/* FIXME: PEB.Padding0 is at 0x004 on Windows 11 */
C_ASSERT_FIELD(PEB, 0x008, Mutant)
C_ASSERT_FIELD(PEB, 0x010, ImageBaseAddress)
C_ASSERT_FIELD(PEB, 0x018, Ldr)
C_ASSERT_FIELD(PEB, 0x020, ProcessParameters)
C_ASSERT_FIELD(PEB, 0x028, SubSystemData)
C_ASSERT_FIELD(PEB, 0x030, ProcessHeap)
C_ASSERT_FIELD(PEB, 0x038, FastPebLock)
/* FIXME: PEB.AtlThunkSListPtr is at 0x040 on Windows 11 */
C_ASSERT_FIELD(PEB, 0x048, IFEOKey)
/* FIXME: PEB.CrossProcessFlags is at 0x050 on Windows 11 */
/* FIXME: PEB.ProcessInJob is at 0x050 on Windows 11 */
/* FIXME: PEB.ReservedBits0 is at 0x051 on Windows 11 */
/* FIXME: PEB.Padding1 is at 0x054 on Windows 11 */
/* FIXME: PEB.KernelCallbackTable is at 0x058 on Windows 11 */
/* FIXME: PEB.UserSharedInfoPtr is at 0x058 on Windows 11 */
C_ASSERT_FIELD(PEB, 0x060, SystemReserved)
/* FIXME: PEB.AtlThunkSListPtr32 is at 0x064 on Windows 11 */
C_ASSERT_FIELD(PEB, 0x068, ApiSetMap)
C_ASSERT_FIELD(PEB, 0x070, TlsExpansionCounter)
/* FIXME: PEB.Padding2 is at 0x074 on Windows 11 */
C_ASSERT_FIELD(PEB, 0x078, TlsBitmap)
C_ASSERT_FIELD(PEB, 0x080, TlsBitmapBits)
C_ASSERT_FIELD(PEB, 0x088, ReadOnlySharedMemoryBase)
/* FIXME: PEB.SharedData is at 0x090 on Windows 11 */
C_ASSERT_FIELD(PEB, 0x098, ReadOnlyStaticServerData)
C_ASSERT_FIELD(PEB, 0x0A0, AnsiCodePageData)
C_ASSERT_FIELD(PEB, 0x0A8, OemCodePageData)
C_ASSERT_FIELD(PEB, 0x0B0, UnicodeCaseTableData)
C_ASSERT_FIELD(PEB, 0x0B8, NumberOfProcessors)
C_ASSERT_FIELD(PEB, 0x0BC, NtGlobalFlag)
C_ASSERT_FIELD(PEB, 0x0C0, CriticalSectionTimeout)
C_ASSERT_FIELD(PEB, 0x0C8, HeapSegmentReserve)
C_ASSERT_FIELD(PEB, 0x0D0, HeapSegmentCommit)
C_ASSERT_FIELD(PEB, 0x0D8, HeapDeCommitTotalFreeThreshold)
C_ASSERT_FIELD(PEB, 0x0E0, HeapDeCommitFreeBlockThreshold)
C_ASSERT_FIELD(PEB, 0x0E8, NumberOfHeaps)
C_ASSERT_FIELD(PEB, 0x0EC, MaximumNumberOfHeaps)
C_ASSERT_FIELD(PEB, 0x0F0, ProcessHeaps)
C_ASSERT_FIELD(PEB, 0x0F8, GdiSharedHandleTable)
C_ASSERT_FIELD(PEB, 0x100, ProcessStarterHelper)
C_ASSERT_FIELD(PEB, 0x108, GdiDCAttributeList)
/* FIXME: PEB.Padding3 is at 0x10C on Windows 11 */
C_ASSERT_FIELD(PEB, 0x110, LoaderLock)
C_ASSERT_FIELD(PEB, 0x118, OSMajorVersion)
C_ASSERT_FIELD(PEB, 0x11C, OSMinorVersion)
C_ASSERT_FIELD(PEB, 0x120, OSBuildNumber)
C_ASSERT_FIELD(PEB, 0x122, OSCSDVersion)
C_ASSERT_FIELD(PEB, 0x124, OSPlatformId)
C_ASSERT_FIELD(PEB, 0x128, ImageSubsystem)
C_ASSERT_FIELD(PEB, 0x12C, ImageSubsystemMajorVersion)
C_ASSERT_FIELD(PEB, 0x130, ImageSubsystemMinorVersion)
/* FIXME: PEB.Padding4 is at 0x134 on Windows 11 */
/* FIXME: PEB.ActiveProcessAffinityMask is at 0x138 on Windows 11 */
C_ASSERT_FIELD(PEB, 0x140, GdiHandleBuffer)
C_ASSERT_FIELD(PEB, 0x230, PostProcessInitRoutine)
C_ASSERT_FIELD(PEB, 0x238, TlsExpansionBitmap)
C_ASSERT_FIELD(PEB, 0x240, TlsExpansionBitmapBits)
C_ASSERT_FIELD(PEB, 0x2C0, SessionId)
/* FIXME: PEB.Padding5 is at 0x2C4 on Windows 11 */
C_ASSERT_FIELD(PEB, 0x2C8, AppCompatFlags)
C_ASSERT_FIELD(PEB, 0x2D0, AppCompatFlagsUser)
C_ASSERT_FIELD(PEB, 0x2D8, pShimData)
C_ASSERT_FIELD(PEB, 0x2E0, AppCompatInfo)
C_ASSERT_FIELD(PEB, 0x2E8, CSDVersion)
C_ASSERT_FIELD(PEB, 0x2F8, ActivationContextData)
C_ASSERT_FIELD(PEB, 0x300, ProcessAssemblyStorageMap)
C_ASSERT_FIELD(PEB, 0x308, SystemDefaultActivationContextData)
C_ASSERT_FIELD(PEB, 0x310, SystemAssemblyStorageMap)
C_ASSERT_FIELD(PEB, 0x318, MinimumStackCommit)
/* FIXME: PEB.SparePointers is at 0x320 on Windows 11 */
/* FIXME: PEB.PatchLoaderData is at 0x330 on Windows 11 */
/* FIXME: PEB.ChpeV2ProcessInfo is at 0x338 on Windows 11 */
/* FIXME: PEB.AppModelFeatureState is at 0x340 on Windows 11 */
/* FIXME: PEB.SpareUlongs is at 0x344 on Windows 11 */
/* FIXME: PEB.ActiveCodePage is at 0x34C on Windows 11 */
/* FIXME: PEB.OemCodePage is at 0x34E on Windows 11 */
/* FIXME: PEB.UseCaseMapping is at 0x350 on Windows 11 */
/* FIXME: PEB.UnusedNlsField is at 0x352 on Windows 11 */
C_ASSERT_FIELD(PEB, 0x358, WerRegistrationData)
C_ASSERT_FIELD(PEB, 0x360, WerShipAssertPtr)
C_ASSERT_FIELD(PEB, 0x368, EcCodeBitMap)
C_ASSERT_FIELD(PEB, 0x370, pImageHeaderHash)
/* FIXME: PEB.TracingFlags is at 0x378 on Windows 11 */
/* FIXME: PEB.HeapTracingEnabled is at 0x378 on Windows 11 */
/* FIXME: PEB.Padding6 is at 0x37C on Windows 11 */
C_ASSERT_FIELD(PEB, 0x380, CsrServerReadOnlySharedMemoryBase)
C_ASSERT_FIELD(PEB, 0x388, TppWorkerpListLock)
C_ASSERT_FIELD(PEB, 0x390, TppWorkerpList)
C_ASSERT_FIELD(PEB, 0x3A0, WaitOnAddressHashTable)
C_ASSERT_FIELD(PEB, 0x7A0, TelemetryCoverageHeader)
C_ASSERT_FIELD(PEB, 0x7A8, CloudFileFlags)
C_ASSERT_FIELD(PEB, 0x7AC, CloudFileDiagFlags)
C_ASSERT_FIELD(PEB, 0x7B0, PlaceholderCompatibilityMode)
C_ASSERT_FIELD(PEB, 0x7B1, PlaceholderCompatibilityModeReserved)
C_ASSERT_FIELD(PEB, 0x7B8, LeapSecondData)
/* FIXME: PEB.LeapSecondFlags is at 0x7C0 on Windows 11 */
/* FIXME: PEB.SixtySecondEnabled is at 0x7C0 on Windows 11 */
C_ASSERT_FIELD(PEB, 0x7C4, NtGlobalFlag2)
C_ASSERT_FIELD(PEB, 0x7C8, ExtendedFeatureDisableMask)

/* PEB32 */
C_ASSERT(sizeof(PEB32) == 0x488);
C_ASSERT_FIELD(PEB32, 0x000, InheritedAddressSpace)
C_ASSERT_FIELD(PEB32, 0x001, ReadImageFileExecOptions)
C_ASSERT_FIELD(PEB32, 0x002, BeingDebugged)
/* FIXME: PEB32.BitField is at 0x003 on Windows 11 */
/* FIXME: PEB32.ImageUsesLargePages is at 0x003 on Windows 11 */
C_ASSERT_FIELD(PEB32, 0x004, Mutant)
C_ASSERT_FIELD(PEB32, 0x008, ImageBaseAddress)
C_ASSERT_FIELD(PEB32, 0x00C, Ldr)
C_ASSERT_FIELD(PEB32, 0x010, ProcessParameters)
C_ASSERT_FIELD(PEB32, 0x014, SubSystemData)
C_ASSERT_FIELD(PEB32, 0x018, ProcessHeap)
C_ASSERT_FIELD(PEB32, 0x01C, FastPebLock)
/* FIXME: PEB32.AtlThunkSListPtr is at 0x020 on Windows 11 */
C_ASSERT_FIELD(PEB32, 0x024, IFEOKey)
/* FIXME: PEB32.CrossProcessFlags is at 0x028 on Windows 11 */
/* FIXME: PEB32.ProcessInJob is at 0x028 on Windows 11 */
/* FIXME: PEB32.ReservedBits0 is at 0x029 on Windows 11 */
/* FIXME: PEB32.KernelCallbackTable is at 0x02C on Windows 11 */
/* FIXME: PEB32.UserSharedInfoPtr is at 0x02C on Windows 11 */
C_ASSERT_FIELD(PEB32, 0x030, SystemReserved)
/* FIXME: PEB32.AtlThunkSListPtr32 is at 0x034 on Windows 11 */
C_ASSERT_FIELD(PEB32, 0x038, ApiSetMap)
C_ASSERT_FIELD(PEB32, 0x03C, TlsExpansionCounter)
C_ASSERT_FIELD(PEB32, 0x040, TlsBitmap)
C_ASSERT_FIELD(PEB32, 0x044, TlsBitmapBits)
C_ASSERT_FIELD(PEB32, 0x04C, ReadOnlySharedMemoryBase)
/* FIXME: PEB32.SharedData is at 0x050 on Windows 11 */
C_ASSERT_FIELD(PEB32, 0x054, ReadOnlyStaticServerData)
C_ASSERT_FIELD(PEB32, 0x058, AnsiCodePageData)
C_ASSERT_FIELD(PEB32, 0x05C, OemCodePageData)
C_ASSERT_FIELD(PEB32, 0x060, UnicodeCaseTableData)
C_ASSERT_FIELD(PEB32, 0x064, NumberOfProcessors)
C_ASSERT_FIELD(PEB32, 0x068, NtGlobalFlag)
C_ASSERT_FIELD(PEB32, 0x070, CriticalSectionTimeout)
C_ASSERT_FIELD(PEB32, 0x078, HeapSegmentReserve)
C_ASSERT_FIELD(PEB32, 0x07C, HeapSegmentCommit)
C_ASSERT_FIELD(PEB32, 0x080, HeapDeCommitTotalFreeThreshold)
C_ASSERT_FIELD(PEB32, 0x084, HeapDeCommitFreeBlockThreshold)
C_ASSERT_FIELD(PEB32, 0x088, NumberOfHeaps)
C_ASSERT_FIELD(PEB32, 0x08C, MaximumNumberOfHeaps)
C_ASSERT_FIELD(PEB32, 0x090, ProcessHeaps)
C_ASSERT_FIELD(PEB32, 0x094, GdiSharedHandleTable)
C_ASSERT_FIELD(PEB32, 0x098, ProcessStarterHelper)
C_ASSERT_FIELD(PEB32, 0x09C, GdiDCAttributeList)
C_ASSERT_FIELD(PEB32, 0x0A0, LoaderLock)
C_ASSERT_FIELD(PEB32, 0x0A4, OSMajorVersion)
C_ASSERT_FIELD(PEB32, 0x0A8, OSMinorVersion)
C_ASSERT_FIELD(PEB32, 0x0AC, OSBuildNumber)
C_ASSERT_FIELD(PEB32, 0x0AE, OSCSDVersion)
C_ASSERT_FIELD(PEB32, 0x0B0, OSPlatformId)
C_ASSERT_FIELD(PEB32, 0x0B4, ImageSubsystem)
C_ASSERT_FIELD(PEB32, 0x0B8, ImageSubsystemMajorVersion)
C_ASSERT_FIELD(PEB32, 0x0BC, ImageSubsystemMinorVersion)
/* FIXME: PEB32.ActiveProcessAffinityMask is at 0x0C0 on Windows 11 */
C_ASSERT_FIELD(PEB32, 0x0C4, GdiHandleBuffer)
C_ASSERT_FIELD(PEB32, 0x14C, PostProcessInitRoutine)
C_ASSERT_FIELD(PEB32, 0x150, TlsExpansionBitmap)
C_ASSERT_FIELD(PEB32, 0x154, TlsExpansionBitmapBits)
C_ASSERT_FIELD(PEB32, 0x1D4, SessionId)
C_ASSERT_FIELD(PEB32, 0x1D8, AppCompatFlags)
C_ASSERT_FIELD(PEB32, 0x1E0, AppCompatFlagsUser)
C_ASSERT_FIELD(PEB32, 0x1E8, pShimData)
C_ASSERT_FIELD(PEB32, 0x1EC, AppCompatInfo)
C_ASSERT_FIELD(PEB32, 0x1F0, CSDVersion)
C_ASSERT_FIELD(PEB32, 0x1F8, ActivationContextData)
C_ASSERT_FIELD(PEB32, 0x1FC, ProcessAssemblyStorageMap)
C_ASSERT_FIELD(PEB32, 0x200, SystemDefaultActivationContextData)
C_ASSERT_FIELD(PEB32, 0x204, SystemAssemblyStorageMap)
C_ASSERT_FIELD(PEB32, 0x208, MinimumStackCommit)
/* FIXME: PEB32.SparePointers is at 0x20C on Windows 11 */
/* FIXME: PEB32.PatchLoaderData is at 0x214 on Windows 11 */
/* FIXME: PEB32.ChpeV2ProcessInfo is at 0x218 on Windows 11 */
/* FIXME: PEB32.AppModelFeatureState is at 0x21C on Windows 11 */
/* FIXME: PEB32.SpareUlongs is at 0x220 on Windows 11 */
/* FIXME: PEB32.ActiveCodePage is at 0x228 on Windows 11 */
/* FIXME: PEB32.OemCodePage is at 0x22A on Windows 11 */
/* FIXME: PEB32.UseCaseMapping is at 0x22C on Windows 11 */
/* FIXME: PEB32.UnusedNlsField is at 0x22E on Windows 11 */
C_ASSERT_FIELD(PEB32, 0x230, WerRegistrationData)
C_ASSERT_FIELD(PEB32, 0x234, WerShipAssertPtr)
/* FIXME: PEB32.Spare is at 0x238 on Windows 11 */
C_ASSERT_FIELD(PEB32, 0x23C, pImageHeaderHash)
/* FIXME: PEB32.TracingFlags is at 0x240 on Windows 11 */
/* FIXME: PEB32.HeapTracingEnabled is at 0x240 on Windows 11 */
C_ASSERT_FIELD(PEB32, 0x248, CsrServerReadOnlySharedMemoryBase)
C_ASSERT_FIELD(PEB32, 0x250, TppWorkerpListLock)
C_ASSERT_FIELD(PEB32, 0x254, TppWorkerpList)
C_ASSERT_FIELD(PEB32, 0x25C, WaitOnAddressHashTable)
C_ASSERT_FIELD(PEB32, 0x45C, TelemetryCoverageHeader)
C_ASSERT_FIELD(PEB32, 0x460, CloudFileFlags)
C_ASSERT_FIELD(PEB32, 0x464, CloudFileDiagFlags)
C_ASSERT_FIELD(PEB32, 0x468, PlaceholderCompatibilityMode)
C_ASSERT_FIELD(PEB32, 0x469, PlaceholderCompatibilityModeReserved)
C_ASSERT_FIELD(PEB32, 0x470, LeapSecondData)
/* FIXME: PEB32.LeapSecondFlags is at 0x474 on Windows 11 */
/* FIXME: PEB32.SixtySecondEnabled is at 0x474 on Windows 11 */
C_ASSERT_FIELD(PEB32, 0x478, NtGlobalFlag2)
C_ASSERT_FIELD(PEB32, 0x480, ExtendedFeatureDisableMask)

/* KUSER_SHARED_DATA */
C_ASSERT(sizeof(KUSER_SHARED_DATA) == 0xA80);
C_ASSERT_FIELD(KUSER_SHARED_DATA, 0x000, TickCountLowDeprecated)
C_ASSERT_FIELD(KUSER_SHARED_DATA, 0x004, TickCountMultiplier)
C_ASSERT_FIELD(KUSER_SHARED_DATA, 0x008, InterruptTime)
C_ASSERT_FIELD(KUSER_SHARED_DATA, 0x014, SystemTime)
C_ASSERT_FIELD(KUSER_SHARED_DATA, 0x020, TimeZoneBias)
C_ASSERT_FIELD(KUSER_SHARED_DATA, 0x02C, ImageNumberLow)
C_ASSERT_FIELD(KUSER_SHARED_DATA, 0x02E, ImageNumberHigh)
C_ASSERT_FIELD(KUSER_SHARED_DATA, 0x030, NtSystemRoot)
C_ASSERT_FIELD(KUSER_SHARED_DATA, 0x238, MaxStackTraceDepth)
C_ASSERT_FIELD(KUSER_SHARED_DATA, 0x23C, CryptoExponent)
C_ASSERT_FIELD(KUSER_SHARED_DATA, 0x240, TimeZoneId)
C_ASSERT_FIELD(KUSER_SHARED_DATA, 0x244, LargePageMinimum)
C_ASSERT_FIELD(KUSER_SHARED_DATA, 0x248, AitSamplingValue)
C_ASSERT_FIELD(KUSER_SHARED_DATA, 0x24C, AppCompatFlag)
C_ASSERT_FIELD(KUSER_SHARED_DATA, 0x250, RNGSeedVersion)
C_ASSERT_FIELD(KUSER_SHARED_DATA, 0x258, GlobalValidationRunlevel)
C_ASSERT_FIELD(KUSER_SHARED_DATA, 0x25C, TimeZoneBiasStamp)
C_ASSERT_FIELD(KUSER_SHARED_DATA, 0x260, NtBuildNumber)
C_ASSERT_FIELD(KUSER_SHARED_DATA, 0x264, NtProductType)
C_ASSERT_FIELD(KUSER_SHARED_DATA, 0x268, ProductTypeIsValid)
C_ASSERT_FIELD(KUSER_SHARED_DATA, 0x269, Reserved0)
C_ASSERT_FIELD(KUSER_SHARED_DATA, 0x26A, NativeProcessorArchitecture)
C_ASSERT_FIELD(KUSER_SHARED_DATA, 0x26C, NtMajorVersion)
C_ASSERT_FIELD(KUSER_SHARED_DATA, 0x270, NtMinorVersion)
C_ASSERT_FIELD(KUSER_SHARED_DATA, 0x274, ProcessorFeatures)
C_ASSERT_FIELD(KUSER_SHARED_DATA, 0x2B4, Reserved1)
C_ASSERT_FIELD(KUSER_SHARED_DATA, 0x2B8, Reserved3)
C_ASSERT_FIELD(KUSER_SHARED_DATA, 0x2BC, TimeSlip)
C_ASSERT_FIELD(KUSER_SHARED_DATA, 0x2C0, AlternativeArchitecture)
C_ASSERT_FIELD(KUSER_SHARED_DATA, 0x2C4, BootId)
C_ASSERT_FIELD(KUSER_SHARED_DATA, 0x2C8, SystemExpirationDate)
C_ASSERT_FIELD(KUSER_SHARED_DATA, 0x2D0, SuiteMask)
C_ASSERT_FIELD(KUSER_SHARED_DATA, 0x2D4, KdDebuggerEnabled)
/* FIXME: KUSER_SHARED_DATA.MitigationPolicies is at 0x2D5 on Windows 11 */
/* FIXME: KUSER_SHARED_DATA.NXSupportPolicy is at 0x2D5 on Windows 11 */
C_ASSERT_FIELD(KUSER_SHARED_DATA, 0x2D6, CyclesPerYield)
C_ASSERT_FIELD(KUSER_SHARED_DATA, 0x2D8, ActiveConsoleId)
C_ASSERT_FIELD(KUSER_SHARED_DATA, 0x2DC, DismountCount)
C_ASSERT_FIELD(KUSER_SHARED_DATA, 0x2E0, ComPlusPackage)
C_ASSERT_FIELD(KUSER_SHARED_DATA, 0x2E4, LastSystemRITEventTickCount)
C_ASSERT_FIELD(KUSER_SHARED_DATA, 0x2E8, NumberOfPhysicalPages)
C_ASSERT_FIELD(KUSER_SHARED_DATA, 0x2EC, SafeBootMode)
/* FIXME: KUSER_SHARED_DATA.VirtualizationFlags is at 0x2ED on Windows 11 */
C_ASSERT_FIELD(KUSER_SHARED_DATA, 0x2EE, Reserved12)
/* FIXME: KUSER_SHARED_DATA.SharedDataFlags is at 0x2F0 on Windows 11 */
/* FIXME: KUSER_SHARED_DATA.DbgErrorPortPresent is at 0x2F0 on Windows 11 */
/* FIXME: KUSER_SHARED_DATA.DbgMultiSessionSku is at 0x2F1 on Windows 11 */
C_ASSERT_FIELD(KUSER_SHARED_DATA, 0x2F4, DataFlagsPad)
C_ASSERT_FIELD(KUSER_SHARED_DATA, 0x2F8, TestRetInstruction)
C_ASSERT_FIELD(KUSER_SHARED_DATA, 0x300, QpcFrequency)
C_ASSERT_FIELD(KUSER_SHARED_DATA, 0x308, SystemCall)
C_ASSERT_FIELD(KUSER_SHARED_DATA, 0x30C, Reserved2)
C_ASSERT_FIELD(KUSER_SHARED_DATA, 0x310, FullNumberOfPhysicalPages)
C_ASSERT_FIELD(KUSER_SHARED_DATA, 0x318, SystemCallPad)
/* FIXME: KUSER_SHARED_DATA.TickCount is at 0x320 on Windows 11 */
/* FIXME: KUSER_SHARED_DATA.TickCountQuad is at 0x320 on Windows 11 */
/* FIXME: KUSER_SHARED_DATA.ReservedTickCountOverlay is at 0x320 on Windows 11 */
/* FIXME: KUSER_SHARED_DATA.TickCountPad is at 0x32C on Windows 11 */
C_ASSERT_FIELD(KUSER_SHARED_DATA, 0x330, Cookie)
C_ASSERT_FIELD(KUSER_SHARED_DATA, 0x334, CookiePad)
C_ASSERT_FIELD(KUSER_SHARED_DATA, 0x338, ConsoleSessionForegroundProcessId)
C_ASSERT_FIELD(KUSER_SHARED_DATA, 0x340, TimeUpdateLock)
C_ASSERT_FIELD(KUSER_SHARED_DATA, 0x348, BaselineSystemTimeQpc)
C_ASSERT_FIELD(KUSER_SHARED_DATA, 0x350, BaselineInterruptTimeQpc)
C_ASSERT_FIELD(KUSER_SHARED_DATA, 0x358, QpcSystemTimeIncrement)
C_ASSERT_FIELD(KUSER_SHARED_DATA, 0x360, QpcInterruptTimeIncrement)
C_ASSERT_FIELD(KUSER_SHARED_DATA, 0x368, QpcSystemTimeIncrementShift)
C_ASSERT_FIELD(KUSER_SHARED_DATA, 0x369, QpcInterruptTimeIncrementShift)
C_ASSERT_FIELD(KUSER_SHARED_DATA, 0x36A, UnparkedProcessorCount)
C_ASSERT_FIELD(KUSER_SHARED_DATA, 0x36C, EnclaveFeatureMask)
C_ASSERT_FIELD(KUSER_SHARED_DATA, 0x37C, TelemetryCoverageRound)
C_ASSERT_FIELD(KUSER_SHARED_DATA, 0x380, UserModeGlobalLogger)
C_ASSERT_FIELD(KUSER_SHARED_DATA, 0x3A0, ImageFileExecutionOptions)
C_ASSERT_FIELD(KUSER_SHARED_DATA, 0x3A4, LangGenerationCount)
C_ASSERT_FIELD(KUSER_SHARED_DATA, 0x3A8, Reserved4)
C_ASSERT_FIELD(KUSER_SHARED_DATA, 0x3B0, InterruptTimeBias)
C_ASSERT_FIELD(KUSER_SHARED_DATA, 0x3B8, QpcBias)
C_ASSERT_FIELD(KUSER_SHARED_DATA, 0x3C0, ActiveProcessorCount)
C_ASSERT_FIELD(KUSER_SHARED_DATA, 0x3C4, ActiveGroupCount)
C_ASSERT_FIELD(KUSER_SHARED_DATA, 0x3C5, Reserved9)
/* FIXME: KUSER_SHARED_DATA.QpcData is at 0x3C6 on Windows 11 */
/* FIXME: KUSER_SHARED_DATA.QpcBypassEnabled is at 0x3C6 on Windows 11 */
/* FIXME: KUSER_SHARED_DATA.QpcReserved is at 0x3C7 on Windows 11 */
C_ASSERT_FIELD(KUSER_SHARED_DATA, 0x3C8, TimeZoneBiasEffectiveStart)
C_ASSERT_FIELD(KUSER_SHARED_DATA, 0x3D0, TimeZoneBiasEffectiveEnd)
C_ASSERT_FIELD(KUSER_SHARED_DATA, 0x3D8, XState)
C_ASSERT_FIELD(KUSER_SHARED_DATA, 0x720, FeatureConfigurationChangeStamp)
C_ASSERT_FIELD(KUSER_SHARED_DATA, 0x72C, Spare)
C_ASSERT_FIELD(KUSER_SHARED_DATA, 0x730, UserPointerAuthMask)
C_ASSERT_FIELD(KUSER_SHARED_DATA, 0x738, Reserved10)
