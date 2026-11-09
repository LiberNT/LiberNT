/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Counting where a program spends its time, from the interrupt that catches it
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "keprivate.h"
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

/* Profiles that watch the whole system rather than one process. */
LIST_ENTRY KiProfileListHead;

/* One record per profiling source that something has asked for, so that the
   platform is only told to start and stop a source once however many profiles
   are using it. */
LIST_ENTRY KiProfileSourceListHead;

/* Covers both lists and every profile on them. A sample is taken at the
   profiling level, so the lock has to be held at that level too. */
KSPIN_LOCK KiProfileLock;

/* The level the profile interrupt arrives at. Samples taken above it would be
   recorded against the profiler's own interrupt rather than against the code
   it is watching. */
KIRQL KiProfileIrql = PROFILE_LEVEL;

/* The period the two sources the kernel can drive itself are asked for, in
   hundreds of nanoseconds. */
ULONG KiProfileTimeInterval = 78125;
ULONG KiProfileAlignmentFixupInterval = 78125;

/* PRIVATE FUNCTIONS **********************************************************/

/*
 * The record that tracks how many profiles want one source. Answers NULL when
 * nothing wants it.
 *
 * Entered with the profile lock held.
 */
static
PKPROFILE_SOURCE_OBJECT
KiFindProfileSource(
    _In_ KPROFILE_SOURCE Source)
{
    PKPROFILE_SOURCE_OBJECT SourceObject;
    PLIST_ENTRY Entry;

    for (Entry = KiProfileSourceListHead.Flink;
         Entry != &KiProfileSourceListHead;
         Entry = Entry->Flink)
    {
        SourceObject = CONTAINING_RECORD(Entry,
                                         KPROFILE_SOURCE_OBJECT,
                                         ListEntry);
        if (SourceObject->Source == Source)
            return SourceObject;
    }

    return NULL;
}

/*
 * The period one source is sampled at. Only the two the kernel's own clock can
 * drive have a period it knows; anything else is the platform's business and
 * is reported as the default.
 */
static
PULONG
KiProfileIntervalSlot(
    _In_ KPROFILE_SOURCE Source)
{
    if (Source == ProfileTime)
        return &KiProfileTimeInterval;

    if (Source == ProfileAlignmentFixup)
        return &KiProfileAlignmentFixupInterval;

    return NULL;
}

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * Describes a region to be profiled and how finely. Nothing is counted yet:
 * the profile has no buffer until it is started, which is also when the
 * platform is asked to begin sampling.
 */
VOID
NTAPI
KeInitializeProfile(
    _Out_ PKPROFILE Profile,
    _In_opt_ PKPROCESS Process,
    _In_ PVOID ImageBase,
    _In_ SIZE_T ImageSize,
    _In_ ULONG BucketSize,
    _In_ KPROFILE_SOURCE ProfileSource,
    _In_ KAFFINITY Affinity)
{
    Profile->Type = ProfileObject;
    Profile->Size = sizeof(KPROFILE);

    InitializeListHead(&Profile->ProfileListEntry);

    Profile->Process = Process;
    Profile->RangeBase = ImageBase;
    Profile->RangeLimit = (PVOID)((ULONG_PTR)ImageBase + ImageSize);

    /*
     * The bucket size is given as a power of two, and a sample is placed by
     * shifting the offset into the region down by it. That is the same
     * arithmetic the caller used to size the buffer.
     */
    Profile->BucketShift = BucketSize;

    Profile->Buffer = NULL;
    Profile->Segment = 0;
    Profile->Affinity = Affinity;
    Profile->Source = ProfileSource;
    Profile->Started = FALSE;
}

/*
 * Starts counting into the caller's buffer. Answers FALSE where the profile
 * was already running, because a second start would leave the first buffer
 * written to by nobody and silently abandoned.
 */
BOOLEAN
NTAPI
KeStartProfile(
    _Inout_ PKPROFILE Profile,
    _In_ PVOID Buffer)
{
    PKPROFILE_SOURCE_OBJECT SourceObject, Allocated;
    PLIST_ENTRY ListHead;
    KIRQL OldIrql;
    BOOLEAN Started = FALSE;
    BOOLEAN FirstForSource = FALSE;

    ASSERT(Profile->Type == ProfileObject);

    /*
     * The source record is taken before the lock: the lock is held at the
     * profiling level, where nothing may be allocated.
     */
    Allocated = ExAllocatePoolWithTag(NonPagedPool,
                                      sizeof(KPROFILE_SOURCE_OBJECT),
                                      TAG_PROFILE);

    /*
     * The lock has to be taken at the level the profile interrupt arrives
     * at, or a sample taken on this processor could walk a list that is
     * half edited.
     */
    OldIrql = KfRaiseIrql(KiProfileIrql);
    KeAcquireSpinLockAtDpcLevel(&KiProfileLock);

    if (!Profile->Started)
    {
        Profile->Buffer = Buffer;
        Profile->Started = TRUE;

        ListHead = (Profile->Process != NULL)
                       ? &Profile->Process->ProfileListHead
                       : &KiProfileListHead;

        InsertTailList(ListHead, &Profile->ProfileListEntry);

        SourceObject = KiFindProfileSource(Profile->Source);
        if (SourceObject != NULL)
        {
            SourceObject->ReferenceCount++;
        }
        else if (Allocated != NULL)
        {
            Allocated->Source = Profile->Source;
            Allocated->ReferenceCount = 1;
            InsertTailList(&KiProfileSourceListHead, &Allocated->ListEntry);
            Allocated = NULL;
            FirstForSource = TRUE;
        }

        Started = TRUE;
    }

    KeReleaseSpinLockFromDpcLevel(&KiProfileLock);
    KeLowerIrql(OldIrql);

    if (Allocated != NULL)
        ExFreePoolWithTag(Allocated, TAG_PROFILE);

    /* The platform only has to be told when nothing was using the source */
    if (FirstForSource)
    {
        HalStartProfileInterrupt(Profile->Source);
    }

    return Started;
}

/*
 * Stops counting. Answers whether the profile had been running, which tells a
 * caller whether the buffer it is about to read was ever being written.
 */
BOOLEAN
NTAPI
KeStopProfile(
    _Inout_ PKPROFILE Profile)
{
    PKPROFILE_SOURCE_OBJECT SourceObject = NULL;
    PKPROFILE_SOURCE_OBJECT Release = NULL;
    KPROFILE_SOURCE Source = Profile->Source;
    KIRQL OldIrql;
    BOOLEAN Stopped = FALSE;

    ASSERT(Profile->Type == ProfileObject);

    OldIrql = KfRaiseIrql(KiProfileIrql);
    KeAcquireSpinLockAtDpcLevel(&KiProfileLock);

    if (Profile->Started)
    {
        RemoveEntryList(&Profile->ProfileListEntry);
        InitializeListHead(&Profile->ProfileListEntry);

        Profile->Started = FALSE;
        Profile->Buffer = NULL;

        SourceObject = KiFindProfileSource(Source);
        if (SourceObject != NULL)
        {
            SourceObject->ReferenceCount--;
            if (SourceObject->ReferenceCount == 0)
            {
                RemoveEntryList(&SourceObject->ListEntry);
                Release = SourceObject;
            }
        }

        Stopped = TRUE;
    }

    KeReleaseSpinLockFromDpcLevel(&KiProfileLock);
    KeLowerIrql(OldIrql);

    if (Release != NULL)
    {
        HalStopProfileInterrupt(Source);
        ExFreePoolWithTag(Release, TAG_PROFILE);
    }

    return Stopped;
}

/*
 * The period a source is sampled at. A source the kernel does not drive itself
 * reports the default rather than nothing, because a caller asking is choosing
 * a number to pass back and has to get a usable one.
 */
ULONG
NTAPI
KeQueryIntervalProfile(
    _In_ KPROFILE_SOURCE ProfileSource)
{
    PULONG Slot = KiProfileIntervalSlot(ProfileSource);

    if (Slot != NULL)
        return *Slot;

    return HalSetProfileInterval(0);
}

/*
 * Asks for a sampling period. The platform may not be able to give exactly
 * what was asked for, and what it settles on is what is recorded, so that a
 * later query answers what is happening rather than what was wanted.
 */
VOID
NTAPI
KeSetIntervalProfile(
    _In_ ULONG Interval,
    _In_ KPROFILE_SOURCE ProfileSource)
{
    PULONG Slot = KiProfileIntervalSlot(ProfileSource);

    if (Slot == NULL)
        return;

    *Slot = HalSetProfileInterval(Interval);
}

/*
 * The level profile interrupts arrive at. A driver that services an interrupt
 * above it will not be seen by the profiler at all, which is why the level is
 * worth being able to move.
 */
VOID
NTAPI
KeSetProfileIrql(
    _In_ KIRQL ProfileIrql)
{
    KiProfileIrql = ProfileIrql;
}

/*
 * Records one sample against every profile on a list that is watching the
 * address and this processor.
 *
 * Entered with the profile lock held at the profiling level.
 */
VOID
NTAPI
KiParseProfileList(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ KPROFILE_SOURCE Source,
    _In_ PLIST_ENTRY ListHead)
{
    PKPROFILE Profile;
    PLIST_ENTRY Entry;
    ULONG_PTR Pc = KeGetTrapFramePc(TrapFrame);
    KAFFINITY Current = AFFINITY_MASK(KeGetCurrentPrcb()->Number);
    PULONG Bucket;
    SIZE_T Offset;

    for (Entry = ListHead->Flink; Entry != ListHead; Entry = Entry->Flink)
    {
        Profile = CONTAINING_RECORD(Entry, KPROFILE, ProfileListEntry);

        if (Profile->Source != Source)
            continue;

        if ((Profile->Affinity & Current) == 0)
            continue;

        if ((Pc < (ULONG_PTR)Profile->RangeBase) ||
            (Pc >= (ULONG_PTR)Profile->RangeLimit))
        {
            continue;
        }

        if (Profile->Buffer == NULL)
            continue;

        Offset = (SIZE_T)(Pc - (ULONG_PTR)Profile->RangeBase);
        Bucket = (PULONG)Profile->Buffer + (Offset >> Profile->BucketShift);

        /*
         * The buffer belongs to the profiling process and is locked down for
         * the life of the profile, so it can be written from here; a fault
         * would be a defect in whoever locked it rather than something to
         * recover from at this level.
         */
        (*Bucket)++;
    }
}

/*
 * The profile interrupt. Whatever was running on this processor when it
 * arrived is the sample, which is why the level the interrupt comes in at
 * decides what the profiler can see.
 */
VOID
NTAPI
KeProfileInterruptWithSource(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ KPROFILE_SOURCE Source)
{
    PKPROCESS Process;
    /* Already above the dispatcher: the lock is taken without raising */
    ASSERT(KeGetCurrentIrql() >= DISPATCH_LEVEL);
    KeAcquireSpinLockAtDpcLevel(&KiProfileLock);

    /* The profiles watching this process, and then those watching everything */
    Process = KeGetCurrentThread()->ApcState.Process;
    if (Process != NULL)
        KiParseProfileList(TrapFrame, Source, &Process->ProfileListHead);

    KiParseProfileList(TrapFrame, Source, &KiProfileListHead);

    KeReleaseSpinLockFromDpcLevel(&KiProfileLock);
}

VOID
NTAPI
KeProfileInterrupt(
    _In_ PKTRAP_FRAME TrapFrame)
{
    KeProfileInterruptWithSource(TrapFrame, ProfileTime);
}

/* PUBLIC FUNCTIONS: HARDWARE COUNTERS ****************************************/

/*
 * Hardware performance counters are a property of the processor and are
 * programmed through the platform. Nothing here drives them, so the documented
 * answer for a platform without them is given rather than a made-up success.
 */
NTSTATUS
NTAPI
KeSetHardwareCounterConfiguration(
    _In_ PHARDWARE_COUNTER CounterArray,
    _In_ ULONG Count)
{
    UNREFERENCED_PARAMETER(CounterArray);
    UNREFERENCED_PARAMETER(Count);

    /* TODO: no per-thread hardware counter configuration is driven here */
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
KeQueryHardwareCounterConfiguration(
    _Out_ PHARDWARE_COUNTER CounterArray,
    _In_ ULONG MaximumCount,
    _Out_ PULONG Count)
{
    UNREFERENCED_PARAMETER(CounterArray);
    UNREFERENCED_PARAMETER(MaximumCount);

    *Count = 0;
    return STATUS_NOT_SUPPORTED;
}

/* EOF */
