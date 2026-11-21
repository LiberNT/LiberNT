/*
 * PROJECT:     LiberNT NDK tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Windows 10 22H2 x86 layouts of NDK structures
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntifs.h>
#include <ndk/ntndk.h>

#define C_ASSERT_FIELD(Type, Offset, MemberName) \
    C_ASSERT(FIELD_OFFSET(Type, MemberName) == Offset);

/* KTHREAD */
/* FIXME: sizeof(KTHREAD) is 0x280 on Windows 11 */
C_ASSERT_FIELD(KTHREAD, 0x000, Header)
C_ASSERT_FIELD(KTHREAD, 0x010, SListFaultAddress)
C_ASSERT_FIELD(KTHREAD, 0x018, QuantumTarget)
C_ASSERT_FIELD(KTHREAD, 0x020, InitialStack)
C_ASSERT_FIELD(KTHREAD, 0x024, StackLimit)
C_ASSERT_FIELD(KTHREAD, 0x028, StackBase)
C_ASSERT_FIELD(KTHREAD, 0x02C, ThreadLock)
C_ASSERT_FIELD(KTHREAD, 0x030, CycleTime)
C_ASSERT_FIELD(KTHREAD, 0x038, HighCycleTime)
C_ASSERT_FIELD(KTHREAD, 0x03C, ServiceTable)
C_ASSERT_FIELD(KTHREAD, 0x040, CurrentRunTime)
C_ASSERT_FIELD(KTHREAD, 0x044, ExpectedRunTime)
C_ASSERT_FIELD(KTHREAD, 0x048, KernelStack)
C_ASSERT_FIELD(KTHREAD, 0x04C, StateSaveArea)
C_ASSERT_FIELD(KTHREAD, 0x050, SchedulingGroup)
C_ASSERT_FIELD(KTHREAD, 0x054, WaitRegister)
C_ASSERT_FIELD(KTHREAD, 0x055, Running)
C_ASSERT_FIELD(KTHREAD, 0x056, Alerted)
/* FIXME: KTHREAD.AutoBoostActive is at 0x058 on Windows 11 */
/* FIXME: KTHREAD.MiscFlags is at 0x058 on Windows 11 */
/* FIXME: KTHREAD.UmsDirectedSwitchEnable is at 0x059 on Windows 11 */
/* FIXME: KTHREAD.UmsPerformingSyscall is at 0x05A on Windows 11 */
/* FIXME: KTHREAD.ThreadFlagsSpare is at 0x05C on Windows 11 */
/* FIXME: KTHREAD.ThreadFlags is at 0x05C on Windows 11 */
/* FIXME: KTHREAD.DisableQuantum is at 0x05D on Windows 11 */
/* FIXME: KTHREAD.AutoBoostEntriesExhausted is at 0x05E on Windows 11 */
/* FIXME: KTHREAD.EtwStackTraceApcInserted is at 0x05F on Windows 11 */
C_ASSERT_FIELD(KTHREAD, 0x060, Tag)
C_ASSERT_FIELD(KTHREAD, 0x061, SystemHeteroCpuPolicy)
C_ASSERT_FIELD(KTHREAD, 0x063, Spare0)
C_ASSERT_FIELD(KTHREAD, 0x064, SystemCallNumber)
C_ASSERT_FIELD(KTHREAD, 0x068, FirstArgument)
C_ASSERT_FIELD(KTHREAD, 0x06C, TrapFrame)
/* FIXME: KTHREAD.ApcState is at 0x070 on Windows 11 */
/* FIXME: KTHREAD.ApcStateFill is at 0x070 on Windows 11 */
/* FIXME: KTHREAD.Priority is at 0x087 on Windows 11 */
/* FIXME: KTHREAD.UserIdealProcessor is at 0x088 on Windows 11 */
C_ASSERT_FIELD(KTHREAD, 0x08C, ContextSwitches)
C_ASSERT_FIELD(KTHREAD, 0x090, State)
C_ASSERT_FIELD(KTHREAD, 0x091, Spare12)
C_ASSERT_FIELD(KTHREAD, 0x092, WaitIrql)
C_ASSERT_FIELD(KTHREAD, 0x093, WaitMode)
C_ASSERT_FIELD(KTHREAD, 0x094, WaitStatus)
C_ASSERT_FIELD(KTHREAD, 0x098, WaitBlockList)
/* FIXME: KTHREAD.WaitListEntry is at 0x09C on Windows 11 */
/* FIXME: KTHREAD.SwapListEntry is at 0x09C on Windows 11 */
C_ASSERT_FIELD(KTHREAD, 0x0A4, Queue)
C_ASSERT_FIELD(KTHREAD, 0x0A8, Teb)
C_ASSERT_FIELD(KTHREAD, 0x0B0, RelativeTimerBias)
C_ASSERT_FIELD(KTHREAD, 0x0B8, Timer)
/* FIXME: KTHREAD.WaitBlock is at 0x0E0 on Windows 11 */
/* FIXME: KTHREAD.WaitBlockFill8 is at 0x0E0 on Windows 11 */
/* FIXME: KTHREAD.WaitBlockFill9 is at 0x0E0 on Windows 11 */
/* FIXME: KTHREAD.WaitBlockFill10 is at 0x0E0 on Windows 11 */
/* FIXME: KTHREAD.WaitBlockFill11 is at 0x0E0 on Windows 11 */
/* FIXME: KTHREAD.ThreadCounters is at 0x0F4 on Windows 11 */
/* FIXME: KTHREAD.XStateSave is at 0x10C on Windows 11 */
/* FIXME: KTHREAD.Win32Thread is at 0x124 on Windows 11 */
/* FIXME: KTHREAD.WaitTime is at 0x138 on Windows 11 */
/* FIXME: KTHREAD.KernelApcDisable is at 0x13C on Windows 11 */
/* FIXME: KTHREAD.CombinedApcDisable is at 0x13C on Windows 11 */
/* FIXME: KTHREAD.SpecialApcDisable is at 0x13E on Windows 11 */
C_ASSERT_FIELD(KTHREAD, 0x140, QueueListEntry)
/* FIXME: KTHREAD.NextProcessor is at 0x148 on Windows 11 */
/* FIXME: KTHREAD.NextProcessorNumber is at 0x148 on Windows 11 */
C_ASSERT_FIELD(KTHREAD, 0x14C, QueuePriority)
C_ASSERT_FIELD(KTHREAD, 0x150, Process)
/* FIXME: KTHREAD.UserAffinity is at 0x154 on Windows 11 */
/* FIXME: KTHREAD.UserAffinityFill is at 0x154 on Windows 11 */
/* FIXME: KTHREAD.PreviousMode is at 0x15A on Windows 11 */
/* FIXME: KTHREAD.BasePriority is at 0x15B on Windows 11 */
/* FIXME: KTHREAD.PriorityDecrement is at 0x15C on Windows 11 */
/* FIXME: KTHREAD.ForegroundBoost is at 0x15C on Windows 11 */
/* FIXME: KTHREAD.Preempted is at 0x15D on Windows 11 */
/* FIXME: KTHREAD.AdjustReason is at 0x15E on Windows 11 */
/* FIXME: KTHREAD.AdjustIncrement is at 0x15F on Windows 11 */
C_ASSERT_FIELD(KTHREAD, 0x160, AffinityVersion)
/* FIXME: KTHREAD.Affinity is at 0x164 on Windows 11 */
/* FIXME: KTHREAD.AffinityFill is at 0x164 on Windows 11 */
/* FIXME: KTHREAD.ApcStateIndex is at 0x16A on Windows 11 */
/* FIXME: KTHREAD.WaitBlockCount is at 0x16B on Windows 11 */
/* FIXME: KTHREAD.IdealProcessor is at 0x16C on Windows 11 */
/* FIXME: KTHREAD.ReadyTime is at 0x170 on Windows 11 */
/* FIXME: KTHREAD.SavedApcState is at 0x174 on Windows 11 */
/* FIXME: KTHREAD.SavedApcStateFill is at 0x174 on Windows 11 */
/* FIXME: KTHREAD.WaitReason is at 0x18B on Windows 11 */
/* FIXME: KTHREAD.SuspendCount is at 0x18C on Windows 11 */
/* FIXME: KTHREAD.Saturation is at 0x18D on Windows 11 */
/* FIXME: KTHREAD.SListFaultCount is at 0x18E on Windows 11 */
/* FIXME: KTHREAD.SchedulerApc is at 0x190 on Windows 11 */
/* FIXME: KTHREAD.SchedulerApcFill0 is at 0x190 on Windows 11 */
/* FIXME: KTHREAD.SchedulerApcFill1 is at 0x190 on Windows 11 */
/* FIXME: KTHREAD.SchedulerApcFill2 is at 0x190 on Windows 11 */
/* FIXME: KTHREAD.SchedulerApcFill3 is at 0x190 on Windows 11 */
/* FIXME: KTHREAD.SchedulerApcFill4 is at 0x190 on Windows 11 */
/* FIXME: KTHREAD.SchedulerApcFill5 is at 0x190 on Windows 11 */
/* FIXME: KTHREAD.ResourceIndex is at 0x191 on Windows 11 */
/* FIXME: KTHREAD.QuantumReset is at 0x193 on Windows 11 */
/* FIXME: KTHREAD.KernelTime is at 0x194 on Windows 11 */
/* FIXME: KTHREAD.WaitPrcb is at 0x1B4 on Windows 11 */
/* FIXME: KTHREAD.LegoData is at 0x1B8 on Windows 11 */
/* FIXME: KTHREAD.CallbackNestingLevel is at 0x1BF on Windows 11 */
/* FIXME: KTHREAD.UserTime is at 0x1C0 on Windows 11 */
/* FIXME: KTHREAD.SuspendEvent is at 0x1C4 on Windows 11 */
C_ASSERT_FIELD(KTHREAD, 0x1D4, ThreadListEntry)
C_ASSERT_FIELD(KTHREAD, 0x1DC, MutantListHead)
C_ASSERT_FIELD(KTHREAD, 0x1E4, AbEntrySummary)
C_ASSERT_FIELD(KTHREAD, 0x1E5, AbWaitEntryCount)
/* FIXME: KTHREAD.AbAllocationRegionCount is at 0x1E6 on Windows 11 */
/* FIXME: KTHREAD.SystemPriority is at 0x1E7 on Windows 11 */
C_ASSERT_FIELD(KTHREAD, 0x1E8, LockEntries)
/* FIXME: KTHREAD.PropagateBoostsEntry is at 0x1EC on Windows 11 */
/* FIXME: KTHREAD.IoSelfBoostsEntry is at 0x1F0 on Windows 11 */
/* FIXME: KTHREAD.PriorityFloorCounts is at 0x1F4 on Windows 11 */
/* FIXME: KTHREAD.PriorityFloorCountsReserved is at 0x204 on Windows 11 */
/* FIXME: KTHREAD.PriorityFloorSummary is at 0x214 on Windows 11 */
/* FIXME: KTHREAD.AbCompletedIoBoostCount is at 0x218 on Windows 11 */
/* FIXME: KTHREAD.AbCompletedIoQoSBoostCount is at 0x21C on Windows 11 */
/* FIXME: KTHREAD.KeReferenceCount is at 0x220 on Windows 11 */
/* FIXME: KTHREAD.AbOrphanedEntrySummary is at 0x222 on Windows 11 */
/* FIXME: KTHREAD.AbOwnedEntryCount is at 0x223 on Windows 11 */
/* FIXME: KTHREAD.ForegroundLossTime is at 0x224 on Windows 11 */
/* FIXME: KTHREAD.GlobalForegroundListEntry is at 0x228 on Windows 11 */
/* FIXME: KTHREAD.ForegroundDpcStackListEntry is at 0x228 on Windows 11 */
/* FIXME: KTHREAD.InGlobalForegroundList is at 0x22C on Windows 11 */
/* FIXME: KTHREAD.QueuedScb is at 0x230 on Windows 11 */
/* FIXME: KTHREAD.NpxState is at 0x238 on Windows 11 */
/* FIXME: KTHREAD.ThreadTimerDelay is at 0x240 on Windows 11 */
/* FIXME: KTHREAD.ThreadFlags3 is at 0x244 on Windows 11 */
/* FIXME: KTHREAD.BamQosLevel is at 0x244 on Windows 11 */
/* FIXME: KTHREAD.PpmPolicy is at 0x245 on Windows 11 */
/* FIXME: KTHREAD.AbWaitObject is at 0x248 on Windows 11 */
/* FIXME: KTHREAD.ReservedPreviousReadyTimeValue is at 0x24C on Windows 11 */
/* FIXME: KTHREAD.KernelWaitTime is at 0x250 on Windows 11 */
/* FIXME: KTHREAD.UserWaitTime is at 0x258 on Windows 11 */
/* FIXME: KTHREAD.Spare29 is at 0x260 on Windows 11 */
/* FIXME: KTHREAD.EndPadding is at 0x26C on Windows 11 */

/* KPCR */
/* FIXME: sizeof(KPCR) is 0x6020 on Windows 11 */
/* FIXME: KPCR.NtTib is at 0x000 on Windows 11 */
/* FIXME: KPCR.Used_ExceptionList is at 0x000 on Windows 11 */
/* FIXME: KPCR.Used_StackBase is at 0x004 on Windows 11 */
/* FIXME: KPCR.MxCsr is at 0x008 on Windows 11 */
/* FIXME: KPCR.TssCopy is at 0x00C on Windows 11 */
/* FIXME: KPCR.ContextSwitches is at 0x010 on Windows 11 */
/* FIXME: KPCR.SetMemberCopy is at 0x014 on Windows 11 */
/* FIXME: KPCR.Used_Self is at 0x018 on Windows 11 */
C_ASSERT_FIELD(KPCR, 0x01C, SelfPcr)
C_ASSERT_FIELD(KPCR, 0x020, Prcb)
C_ASSERT_FIELD(KPCR, 0x024, Irql)
C_ASSERT_FIELD(KPCR, 0x028, IRR)
C_ASSERT_FIELD(KPCR, 0x02C, IrrActive)
C_ASSERT_FIELD(KPCR, 0x030, IDR)
C_ASSERT_FIELD(KPCR, 0x034, KdVersionBlock)
C_ASSERT_FIELD(KPCR, 0x038, IDT)
C_ASSERT_FIELD(KPCR, 0x03C, GDT)
C_ASSERT_FIELD(KPCR, 0x040, TSS)
C_ASSERT_FIELD(KPCR, 0x044, MajorVersion)
C_ASSERT_FIELD(KPCR, 0x046, MinorVersion)
C_ASSERT_FIELD(KPCR, 0x048, SetMember)
C_ASSERT_FIELD(KPCR, 0x04C, StallScaleFactor)
C_ASSERT_FIELD(KPCR, 0x050, SpareUnused)
C_ASSERT_FIELD(KPCR, 0x051, Number)
C_ASSERT_FIELD(KPCR, 0x052, Spare0)
C_ASSERT_FIELD(KPCR, 0x053, SecondLevelCacheAssociativity)
C_ASSERT_FIELD(KPCR, 0x054, VdmAlert)
C_ASSERT_FIELD(KPCR, 0x058, KernelReserved)
C_ASSERT_FIELD(KPCR, 0x090, SecondLevelCacheSize)
C_ASSERT_FIELD(KPCR, 0x094, HalReserved)
/* FIXME: KPCR.InterruptMode is at 0x0D4 on Windows 11 */
/* FIXME: KPCR.Spare1 is at 0x0D8 on Windows 11 */
/* FIXME: KPCR.KernelReserved2 is at 0x0DC on Windows 11 */
/* FIXME: KPCR.PrcbData is at 0x120 on Windows 11 */

/* KPRCB */
/* FIXME: sizeof(KPRCB) is 0x5F00 on Windows 11 */
C_ASSERT_FIELD(KPRCB, 0x000, MinorVersion)
C_ASSERT_FIELD(KPRCB, 0x002, MajorVersion)
C_ASSERT_FIELD(KPRCB, 0x004, CurrentThread)
C_ASSERT_FIELD(KPRCB, 0x008, NextThread)
C_ASSERT_FIELD(KPRCB, 0x00C, IdleThread)
/* FIXME: KPRCB.LegacyNumber is at 0x010 on Windows 11 */
/* FIXME: KPRCB.NestingLevel is at 0x011 on Windows 11 */
C_ASSERT_FIELD(KPRCB, 0x012, BuildType)
/* FIXME: KPRCB.CpuType is at 0x014 on Windows 11 */
/* FIXME: KPRCB.CpuID is at 0x015 on Windows 11 */
/* FIXME: KPRCB.CpuStep is at 0x016 on Windows 11 */
/* FIXME: KPRCB.CpuStepping is at 0x016 on Windows 11 */
/* FIXME: KPRCB.CpuModel is at 0x017 on Windows 11 */
/* FIXME: KPRCB.ProcessorState is at 0x018 on Windows 11 */
/* FIXME: KPRCB.ParentNode is at 0x338 on Windows 11 */
/* FIXME: KPRCB.PriorityState is at 0x33C on Windows 11 */
/* FIXME: KPRCB.KernelReserved is at 0x340 on Windows 11 */
/* FIXME: KPRCB.HalReserved is at 0x378 on Windows 11 */
/* FIXME: KPRCB.CFlushSize is at 0x3B8 on Windows 11 */
/* FIXME: KPRCB.CoresPerPhysicalProcessor is at 0x3BC on Windows 11 */
/* FIXME: KPRCB.LogicalProcessorsPerCore is at 0x3BD on Windows 11 */
/* FIXME: KPRCB.CpuVendor is at 0x3BE on Windows 11 */
/* FIXME: KPRCB.PrcbPad0 is at 0x3BF on Windows 11 */
/* FIXME: KPRCB.MHz is at 0x3C0 on Windows 11 */
/* FIXME: KPRCB.GroupIndex is at 0x3C4 on Windows 11 */
/* FIXME: KPRCB.Group is at 0x3C5 on Windows 11 */
/* FIXME: KPRCB.PrcbPad05 is at 0x3C6 on Windows 11 */
/* FIXME: KPRCB.GroupSetMember is at 0x3C8 on Windows 11 */
/* FIXME: KPRCB.Number is at 0x3CC on Windows 11 */
/* FIXME: KPRCB.ClockOwner is at 0x3D0 on Windows 11 */
/* FIXME: KPRCB.PendingTickFlags is at 0x3D1 on Windows 11 */
/* FIXME: KPRCB.PendingTick is at 0x3D1 on Windows 11 */
/* FIXME: KPRCB.PrcbPad10 is at 0x3D2 on Windows 11 */
C_ASSERT_FIELD(KPRCB, 0x418, LockQueue)
/* FIXME: KPRCB.InterruptCount is at 0x4A0 on Windows 11 */
/* FIXME: KPRCB.KernelTime is at 0x4A4 on Windows 11 */
/* FIXME: KPRCB.UserTime is at 0x4A8 on Windows 11 */
/* FIXME: KPRCB.DpcTime is at 0x4AC on Windows 11 */
/* FIXME: KPRCB.DpcTimeCount is at 0x4B0 on Windows 11 */
/* FIXME: KPRCB.InterruptTime is at 0x4B4 on Windows 11 */
/* FIXME: KPRCB.AdjustDpcThreshold is at 0x4B8 on Windows 11 */
/* FIXME: KPRCB.PageColor is at 0x4BC on Windows 11 */
/* FIXME: KPRCB.DebuggerSavedIRQL is at 0x4C0 on Windows 11 */
/* FIXME: KPRCB.NodeColor is at 0x4C1 on Windows 11 */
/* FIXME: KPRCB.DeepSleep is at 0x4C2 on Windows 11 */
/* FIXME: KPRCB.TbFlushListActive is at 0x4C3 on Windows 11 */
/* FIXME: KPRCB.CachedStack is at 0x4C4 on Windows 11 */
/* FIXME: KPRCB.NodeShiftedColor is at 0x4C8 on Windows 11 */
/* FIXME: KPRCB.SecondaryColorMask is at 0x4CC on Windows 11 */
/* FIXME: KPRCB.DpcTimeLimit is at 0x4D0 on Windows 11 */
/* FIXME: KPRCB.MmInternal is at 0x4D4 on Windows 11 */
/* FIXME: KPRCB.PrcbFlags is at 0x4D8 on Windows 11 */
/* FIXME: KPRCB.SchedulerAssist is at 0x4DC on Windows 11 */
/* FIXME: KPRCB.CcFastReadNoWait is at 0x4E0 on Windows 11 */
/* FIXME: KPRCB.CcFastReadWait is at 0x4E4 on Windows 11 */
/* FIXME: KPRCB.CcFastReadNotPossible is at 0x4E8 on Windows 11 */
/* FIXME: KPRCB.CcCopyReadNoWait is at 0x4EC on Windows 11 */
/* FIXME: KPRCB.CcCopyReadWait is at 0x4F0 on Windows 11 */
/* FIXME: KPRCB.CcCopyReadNoWaitMiss is at 0x4F4 on Windows 11 */
/* FIXME: KPRCB.MmSpinLockOrdering is at 0x4F8 on Windows 11 */
/* FIXME: KPRCB.IoReadOperationCount is at 0x4FC on Windows 11 */
/* FIXME: KPRCB.IoWriteOperationCount is at 0x500 on Windows 11 */
/* FIXME: KPRCB.IoOtherOperationCount is at 0x504 on Windows 11 */
/* FIXME: KPRCB.IoReadTransferCount is at 0x508 on Windows 11 */
/* FIXME: KPRCB.IoWriteTransferCount is at 0x510 on Windows 11 */
/* FIXME: KPRCB.IoOtherTransferCount is at 0x518 on Windows 11 */
/* FIXME: KPRCB.CcFastMdlReadNoWait is at 0x520 on Windows 11 */
/* FIXME: KPRCB.CcFastMdlReadWait is at 0x524 on Windows 11 */
/* FIXME: KPRCB.CcFastMdlReadNotPossible is at 0x528 on Windows 11 */
/* FIXME: KPRCB.CcMapDataNoWait is at 0x52C on Windows 11 */
/* FIXME: KPRCB.CcMapDataWait is at 0x530 on Windows 11 */
/* FIXME: KPRCB.CcPinMappedDataCount is at 0x534 on Windows 11 */
/* FIXME: KPRCB.CcPinReadNoWait is at 0x538 on Windows 11 */
/* FIXME: KPRCB.CcPinReadWait is at 0x53C on Windows 11 */
/* FIXME: KPRCB.CcMdlReadNoWait is at 0x540 on Windows 11 */
/* FIXME: KPRCB.CcMdlReadWait is at 0x544 on Windows 11 */
/* FIXME: KPRCB.CcLazyWriteHotSpots is at 0x548 on Windows 11 */
/* FIXME: KPRCB.CcLazyWriteIos is at 0x54C on Windows 11 */
/* FIXME: KPRCB.CcLazyWritePages is at 0x550 on Windows 11 */
/* FIXME: KPRCB.CcDataFlushes is at 0x554 on Windows 11 */
/* FIXME: KPRCB.CcDataPages is at 0x558 on Windows 11 */
/* FIXME: KPRCB.CcLostDelayedWrites is at 0x55C on Windows 11 */
/* FIXME: KPRCB.CcFastReadResourceMiss is at 0x560 on Windows 11 */
/* FIXME: KPRCB.CcCopyReadWaitMiss is at 0x564 on Windows 11 */
/* FIXME: KPRCB.CcFastMdlReadResourceMiss is at 0x568 on Windows 11 */
/* FIXME: KPRCB.CcMapDataNoWaitMiss is at 0x56C on Windows 11 */
/* FIXME: KPRCB.CcMapDataWaitMiss is at 0x570 on Windows 11 */
/* FIXME: KPRCB.CcPinReadNoWaitMiss is at 0x574 on Windows 11 */
/* FIXME: KPRCB.CcPinReadWaitMiss is at 0x578 on Windows 11 */
/* FIXME: KPRCB.CcMdlReadNoWaitMiss is at 0x57C on Windows 11 */
/* FIXME: KPRCB.CcMdlReadWaitMiss is at 0x580 on Windows 11 */
/* FIXME: KPRCB.CcReadAheadIos is at 0x584 on Windows 11 */
/* FIXME: KPRCB.KeAlignmentFixupCount is at 0x588 on Windows 11 */
/* FIXME: KPRCB.KeExceptionDispatchCount is at 0x58C on Windows 11 */
/* FIXME: KPRCB.KeSystemCalls is at 0x590 on Windows 11 */
/* FIXME: KPRCB.AvailableTime is at 0x594 on Windows 11 */
/* FIXME: KPRCB.PrcbPad22 is at 0x598 on Windows 11 */
/* FIXME: KPRCB.PPLookasideList is at 0x5A0 on Windows 11 */
/* FIXME: KPRCB.PPNxPagedLookasideList is at 0x620 on Windows 11 */
/* FIXME: KPRCB.PPNPagedLookasideList is at 0xF20 on Windows 11 */
/* FIXME: KPRCB.PPPagedLookasideList is at 0x1820 on Windows 11 */
/* FIXME: KPRCB.PacketBarrier is at 0x2120 on Windows 11 */
/* FIXME: KPRCB.ReverseStall is at 0x2124 on Windows 11 */
/* FIXME: KPRCB.IpiFrame is at 0x2128 on Windows 11 */
/* FIXME: KPRCB.PrcbPad3 is at 0x212C on Windows 11 */
/* FIXME: KPRCB.CurrentPacket is at 0x2160 on Windows 11 */
/* FIXME: KPRCB.TargetSet is at 0x216C on Windows 11 */
/* FIXME: KPRCB.WorkerRoutine is at 0x2170 on Windows 11 */
/* FIXME: KPRCB.IpiFrozen is at 0x2174 on Windows 11 */
/* FIXME: KPRCB.PrcbPad4 is at 0x2178 on Windows 11 */
/* FIXME: KPRCB.RequestSummary is at 0x21A0 on Windows 11 */
/* FIXME: KPRCB.TargetCount is at 0x21A4 on Windows 11 */
/* FIXME: KPRCB.PrcbPad94 is at 0x21A8 on Windows 11 */
/* FIXME: KPRCB.TrappedSecurityDomain is at 0x21B0 on Windows 11 */
/* FIXME: KPRCB.BpbState is at 0x21B8 on Windows 11 */
/* FIXME: KPRCB.BpbCpuIdle is at 0x21B8 on Windows 11 */
/* FIXME: KPRCB.BpbFeatures is at 0x21B9 on Windows 11 */
/* FIXME: KPRCB.BpbClearOnIdle is at 0x21B9 on Windows 11 */
/* FIXME: KPRCB.BpbCurrentSpecCtrl is at 0x21BA on Windows 11 */
/* FIXME: KPRCB.BpbKernelSpecCtrl is at 0x21BB on Windows 11 */
/* FIXME: KPRCB.BpbNmiSpecCtrl is at 0x21BC on Windows 11 */
/* FIXME: KPRCB.BpbUserSpecCtrl is at 0x21BD on Windows 11 */
/* FIXME: KPRCB.PrcbPad49 is at 0x21BE on Windows 11 */
/* FIXME: KPRCB.ProcessorSignature is at 0x21C0 on Windows 11 */
/* FIXME: KPRCB.ProcessorFlags is at 0x21C4 on Windows 11 */
/* FIXME: KPRCB.PrcbPad50 is at 0x21C8 on Windows 11 */
/* FIXME: KPRCB.InterruptLastCount is at 0x21D0 on Windows 11 */
/* FIXME: KPRCB.InterruptRate is at 0x21D4 on Windows 11 */
/* FIXME: KPRCB.DeviceInterrupts is at 0x21D8 on Windows 11 */
/* FIXME: KPRCB.IsrDpcStats is at 0x21DC on Windows 11 */
/* FIXME: KPRCB.DpcData is at 0x21E0 on Windows 11 */
/* FIXME: KPRCB.DpcStack is at 0x2210 on Windows 11 */
/* FIXME: KPRCB.MaximumDpcQueueDepth is at 0x2214 on Windows 11 */
/* FIXME: KPRCB.DpcRequestRate is at 0x2218 on Windows 11 */
/* FIXME: KPRCB.MinimumDpcRate is at 0x221C on Windows 11 */
/* FIXME: KPRCB.DpcLastCount is at 0x2220 on Windows 11 */
/* FIXME: KPRCB.PrcbLock is at 0x2224 on Windows 11 */
/* FIXME: KPRCB.DpcGate is at 0x2228 on Windows 11 */
/* FIXME: KPRCB.IdleState is at 0x2238 on Windows 11 */
/* FIXME: KPRCB.QuantumEnd is at 0x2239 on Windows 11 */
/* FIXME: KPRCB.DpcRoutineActive is at 0x223A on Windows 11 */
/* FIXME: KPRCB.IdleSchedule is at 0x223B on Windows 11 */
/* FIXME: KPRCB.DpcRequestSummary is at 0x223C on Windows 11 */
/* FIXME: KPRCB.DpcRequestSlot is at 0x223C on Windows 11 */
/* FIXME: KPRCB.NormalDpcState is at 0x223C on Windows 11 */
/* FIXME: KPRCB.DpcNormalProcessingActive is at 0x223C on Windows 11 */
/* FIXME: KPRCB.ThreadDpcState is at 0x223E on Windows 11 */
/* FIXME: KPRCB.DpcThreadActive is at 0x223E on Windows 11 */
/* FIXME: KPRCB.LastTick is at 0x2240 on Windows 11 */
/* FIXME: KPRCB.PeriodicCount is at 0x2244 on Windows 11 */
/* FIXME: KPRCB.PeriodicBias is at 0x2248 on Windows 11 */
/* FIXME: KPRCB.ClockInterrupts is at 0x224C on Windows 11 */
/* FIXME: KPRCB.ReadyScanTick is at 0x2250 on Windows 11 */
/* FIXME: KPRCB.GroupSchedulingOverQuota is at 0x2254 on Windows 11 */
/* FIXME: KPRCB.ThreadDpcEnable is at 0x2255 on Windows 11 */
/* FIXME: KPRCB.PrcbPad41 is at 0x2256 on Windows 11 */
/* FIXME: KPRCB.TimerTable is at 0x2260 on Windows 11 */
/* FIXME: KPRCB.PrcbPad92 is at 0x3AB0 on Windows 11 */
/* FIXME: KPRCB.CallDpc is at 0x3AE0 on Windows 11 */
/* FIXME: KPRCB.ClockKeepAlive is at 0x3B00 on Windows 11 */
/* FIXME: KPRCB.PrcbPad6 is at 0x3B04 on Windows 11 */
/* FIXME: KPRCB.DpcWatchdogPeriod is at 0x3B08 on Windows 11 */
/* FIXME: KPRCB.DpcWatchdogCount is at 0x3B0C on Windows 11 */
/* FIXME: KPRCB.KeSpinLockOrdering is at 0x3B10 on Windows 11 */
/* FIXME: KPRCB.DpcWatchdogProfileCumulativeDpcThreshold is at 0x3B14 on Windows 11 */
/* FIXME: KPRCB.QueueIndex is at 0x3B18 on Windows 11 */
/* FIXME: KPRCB.DeferredReadyListHead is at 0x3B1C on Windows 11 */
/* FIXME: KPRCB.ReadySummary is at 0x3B20 on Windows 11 */
/* FIXME: KPRCB.AffinitizedSelectionMask is at 0x3B24 on Windows 11 */
/* FIXME: KPRCB.WaitLock is at 0x3B28 on Windows 11 */
/* FIXME: KPRCB.WaitListHead is at 0x3B2C on Windows 11 */
/* FIXME: KPRCB.ScbOffset is at 0x3B34 on Windows 11 */
/* FIXME: KPRCB.ReadyThreadCount is at 0x3B38 on Windows 11 */
/* FIXME: KPRCB.StartCycles is at 0x3B40 on Windows 11 */
/* FIXME: KPRCB.TaggedCyclesStart is at 0x3B48 on Windows 11 */
/* FIXME: KPRCB.TaggedCycles is at 0x3B50 on Windows 11 */
/* FIXME: KPRCB.CycleTime is at 0x3B68 on Windows 11 */
/* FIXME: KPRCB.AffinitizedCycles is at 0x3B70 on Windows 11 */
/* FIXME: KPRCB.ImportantCycles is at 0x3B78 on Windows 11 */
/* FIXME: KPRCB.UnimportantCycles is at 0x3B80 on Windows 11 */
/* FIXME: KPRCB.ReadyQueueExpectedRunTime is at 0x3B88 on Windows 11 */
/* FIXME: KPRCB.HighCycleTime is at 0x3B90 on Windows 11 */
/* FIXME: KPRCB.Cycles is at 0x3B98 on Windows 11 */
/* FIXME: KPRCB.PrcbPad71 is at 0x3BD8 on Windows 11 */
/* FIXME: KPRCB.DpcWatchdogSequenceNumber is at 0x3BDC on Windows 11 */
/* FIXME: KPRCB.DispatcherReadyListHead is at 0x3BE0 on Windows 11 */
/* FIXME: KPRCB.ChainedInterruptList is at 0x3CE0 on Windows 11 */
/* FIXME: KPRCB.LookasideIrpFloat is at 0x3CE4 on Windows 11 */
/* FIXME: KPRCB.ScbQueue is at 0x3CE8 on Windows 11 */
/* FIXME: KPRCB.ScbList is at 0x3CF0 on Windows 11 */
/* FIXME: KPRCB.MmPageFaultCount is at 0x3CF8 on Windows 11 */
/* FIXME: KPRCB.MmCopyOnWriteCount is at 0x3CFC on Windows 11 */
/* FIXME: KPRCB.MmTransitionCount is at 0x3D00 on Windows 11 */
/* FIXME: KPRCB.MmCacheTransitionCount is at 0x3D04 on Windows 11 */
/* FIXME: KPRCB.MmDemandZeroCount is at 0x3D08 on Windows 11 */
/* FIXME: KPRCB.MmPageReadCount is at 0x3D0C on Windows 11 */
/* FIXME: KPRCB.MmPageReadIoCount is at 0x3D10 on Windows 11 */
/* FIXME: KPRCB.MmCacheReadCount is at 0x3D14 on Windows 11 */
/* FIXME: KPRCB.MmCacheIoCount is at 0x3D18 on Windows 11 */
/* FIXME: KPRCB.MmDirtyPagesWriteCount is at 0x3D1C on Windows 11 */
/* FIXME: KPRCB.MmDirtyWriteIoCount is at 0x3D20 on Windows 11 */
/* FIXME: KPRCB.MmMappedPagesWriteCount is at 0x3D24 on Windows 11 */
/* FIXME: KPRCB.MmMappedWriteIoCount is at 0x3D28 on Windows 11 */
/* FIXME: KPRCB.CachedCommit is at 0x3D2C on Windows 11 */
/* FIXME: KPRCB.CachedResidentAvailable is at 0x3D30 on Windows 11 */
/* FIXME: KPRCB.HyperPte is at 0x3D34 on Windows 11 */
/* FIXME: KPRCB.PrcbPad8 is at 0x3D38 on Windows 11 */
/* FIXME: KPRCB.VendorString is at 0x3D3C on Windows 11 */
/* FIXME: KPRCB.InitialApicId is at 0x3D49 on Windows 11 */
/* FIXME: KPRCB.LogicalProcessorsPerPhysicalProcessor is at 0x3D4A on Windows 11 */
/* FIXME: KPRCB.PrcbPad9 is at 0x3D4B on Windows 11 */
/* FIXME: KPRCB.FeatureBits is at 0x3D50 on Windows 11 */
/* FIXME: KPRCB.UpdateSignature is at 0x3D58 on Windows 11 */
/* FIXME: KPRCB.IsrTime is at 0x3D60 on Windows 11 */
/* FIXME: KPRCB.GenerationTarget is at 0x3D68 on Windows 11 */
/* FIXME: KPRCB.PowerState is at 0x3D70 on Windows 11 */
/* FIXME: KPRCB.ForceIdleDpc is at 0x3F18 on Windows 11 */
/* FIXME: KPRCB.MsrIa32TsxCtrl is at 0x3F38 on Windows 11 */
/* FIXME: KPRCB.PrcbPad91 is at 0x3F40 on Windows 11 */
/* FIXME: KPRCB.DpcRuntimeHistoryHashTable is at 0x3F58 on Windows 11 */
/* FIXME: KPRCB.DpcRuntimeHistoryHashTableCleanupDpc is at 0x3F5C on Windows 11 */
/* FIXME: KPRCB.CurrentDpcRuntimeHistoryCached is at 0x3F60 on Windows 11 */
/* FIXME: KPRCB.CurrentDpcStartTime is at 0x3F68 on Windows 11 */
/* FIXME: KPRCB.CurrentDpcRoutine is at 0x3F70 on Windows 11 */
/* FIXME: KPRCB.DpcWatchdogProfileSingleDpcThreshold is at 0x3F74 on Windows 11 */
/* FIXME: KPRCB.DpcWatchdogDpc is at 0x3F78 on Windows 11 */
/* FIXME: KPRCB.DpcWatchdogTimer is at 0x3F98 on Windows 11 */
/* FIXME: KPRCB.HypercallPageList is at 0x3FC0 on Windows 11 */
/* FIXME: KPRCB.HypercallCachedPages is at 0x3FC8 on Windows 11 */
/* FIXME: KPRCB.VirtualApicAssist is at 0x3FCC on Windows 11 */
/* FIXME: KPRCB.StatisticsPage is at 0x3FD0 on Windows 11 */
/* FIXME: KPRCB.Cache is at 0x3FD4 on Windows 11 */
/* FIXME: KPRCB.CacheCount is at 0x4010 on Windows 11 */
/* FIXME: KPRCB.PackageProcessorSet is at 0x4014 on Windows 11 */
/* FIXME: KPRCB.SharedReadyQueueMask is at 0x4020 on Windows 11 */
/* FIXME: KPRCB.SharedReadyQueue is at 0x4024 on Windows 11 */
/* FIXME: KPRCB.SharedQueueScanOwner is at 0x4028 on Windows 11 */
/* FIXME: KPRCB.CoreProcessorSet is at 0x402C on Windows 11 */
/* FIXME: KPRCB.ScanSiblingMask is at 0x4030 on Windows 11 */
/* FIXME: KPRCB.LLCMask is at 0x4034 on Windows 11 */
/* FIXME: KPRCB.CacheProcessorMask is at 0x4038 on Windows 11 */
/* FIXME: KPRCB.ScanSiblingIndex is at 0x404C on Windows 11 */
/* FIXME: KPRCB.WheaInfo is at 0x4050 on Windows 11 */
/* FIXME: KPRCB.EtwSupport is at 0x4054 on Windows 11 */
/* FIXME: KPRCB.InterruptObjectPool is at 0x4058 on Windows 11 */
/* FIXME: KPRCB.DpcWatchdogProfile is at 0x4060 on Windows 11 */
/* FIXME: KPRCB.DpcWatchdogProfileCurrentEmptyCapture is at 0x4064 on Windows 11 */
/* FIXME: KPRCB.PackageId is at 0x4068 on Windows 11 */
/* FIXME: KPRCB.PteBitCache is at 0x406C on Windows 11 */
/* FIXME: KPRCB.PteBitOffset is at 0x4070 on Windows 11 */
/* FIXME: KPRCB.PrcbPad93 is at 0x4074 on Windows 11 */
/* FIXME: KPRCB.ProcessorProfileControlArea is at 0x4078 on Windows 11 */
/* FIXME: KPRCB.ProfileEventIndexAddress is at 0x407C on Windows 11 */
/* FIXME: KPRCB.TimerExpirationDpc is at 0x4080 on Windows 11 */
/* FIXME: KPRCB.SynchCounters is at 0x40A0 on Windows 11 */
/* FIXME: KPRCB.FsCounters is at 0x4158 on Windows 11 */
/* FIXME: KPRCB.Context is at 0x4168 on Windows 11 */
/* FIXME: KPRCB.ContextFlagsInit is at 0x416C on Windows 11 */
/* FIXME: KPRCB.ExtendedState is at 0x4170 on Windows 11 */
/* FIXME: KPRCB.EntropyTimingState is at 0x4174 on Windows 11 */
/* FIXME: KPRCB.IsrStack is at 0x429C on Windows 11 */
/* FIXME: KPRCB.VectorToInterruptObject is at 0x42A0 on Windows 11 */
/* FIXME: KPRCB.AbSelfIoBoostsList is at 0x45E0 on Windows 11 */
/* FIXME: KPRCB.AbPropagateBoostsList is at 0x45E4 on Windows 11 */
/* FIXME: KPRCB.AbDpc is at 0x45E8 on Windows 11 */
/* FIXME: KPRCB.IoIrpStackProfilerCurrent is at 0x4608 on Windows 11 */
/* FIXME: KPRCB.IoIrpStackProfilerPrevious is at 0x465C on Windows 11 */
/* FIXME: KPRCB.TimerExpirationTrace is at 0x46B0 on Windows 11 */
/* FIXME: KPRCB.TimerExpirationTraceCount is at 0x47B0 on Windows 11 */
/* FIXME: KPRCB.ExSaPageArray is at 0x47B4 on Windows 11 */
/* FIXME: KPRCB.ExtendedSupervisorState is at 0x47B8 on Windows 11 */
/* FIXME: KPRCB.PrcbPad100 is at 0x47BC on Windows 11 */
/* FIXME: KPRCB.LocalSharedReadyQueue is at 0x47E0 on Windows 11 */
/* FIXME: KPRCB.Mailbox is at 0x4920 on Windows 11 */
/* FIXME: KPRCB.PrcbPad is at 0x4924 on Windows 11 */
/* FIXME: KPRCB.KernelDirectoryTableBase is at 0x4EE0 on Windows 11 */
/* FIXME: KPRCB.EspBaseShadow is at 0x4EE4 on Windows 11 */
/* FIXME: KPRCB.UserEspShadow is at 0x4EE8 on Windows 11 */
/* FIXME: KPRCB.ShadowFlags is at 0x4EEC on Windows 11 */
/* FIXME: KPRCB.UserDS is at 0x4EF0 on Windows 11 */
/* FIXME: KPRCB.UserES is at 0x4EF4 on Windows 11 */
/* FIXME: KPRCB.UserFS is at 0x4EF8 on Windows 11 */
/* FIXME: KPRCB.EspIretd is at 0x4EFC on Windows 11 */
/* FIXME: KPRCB.RestoreSegOption is at 0x4F00 on Windows 11 */
/* FIXME: KPRCB.SavedEsi is at 0x4F04 on Windows 11 */
/* FIXME: KPRCB.VerwSelector is at 0x4F08 on Windows 11 */
/* FIXME: KPRCB.PrcbShadowPad is at 0x4F0A on Windows 11 */
/* FIXME: KPRCB.TaskSwitchCount is at 0x4F0C on Windows 11 */
/* FIXME: KPRCB.DbgLogs is at 0x4F10 on Windows 11 */
/* FIXME: KPRCB.DbgCount is at 0x5710 on Windows 11 */
/* FIXME: KPRCB.PrcbPadRemainingPage is at 0x5714 on Windows 11 */
/* FIXME: KPRCB.RequestMailbox is at 0x5EE0 on Windows 11 */
