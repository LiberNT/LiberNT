/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Executive address push-lock behavior
 */

#include <kmt_test.h>

#ifdef _M_ARM64

#define ADDRESS_PUSH_LOCK_WAKE_ROUNDS 16

typedef struct _ADDRESS_PUSH_LOCK_CONTEXT
{
    EX_PUSH_LOCK PushLock;
    KEVENT ReadyEvent;
    KEVENT DoneEvent;
    volatile LONGLONG Address;
    volatile LONG MainProcessor;
    volatile LONG WakerProcessor;
    ULONG Rounds;
    ULONG ForwardRemoteRounds;
    ULONG WaitFailures;
    ULONG WakerMigrations;
    KAFFINITY WakerAffinity;
    KAFFINITY WakerSeedAffinity;
    KAFFINITY WakerProcessorMask;
    KPRIORITY WakerPriorityMin;
    KPRIORITY WakerPriorityMax;
    BOOLEAN UseAddressWait;
} ADDRESS_PUSH_LOCK_CONTEXT, *PADDRESS_PUSH_LOCK_CONTEXT;

static
VOID
NTAPI
AddressPushLockWakeThread(
    _In_ PVOID Parameter)
{
    PADDRESS_PUSH_LOCK_CONTEXT Context = Parameter;
    KAFFINITY OldAffinity = 0;
    KPRIORITY Priority;
    NTSTATUS Status;
    ULONG PreviousProcessor = MAXULONG, Processor, Round;

    if (Context->WakerSeedAffinity)
    {
        OldAffinity = KeSetSystemAffinityThreadEx(Context->WakerSeedAffinity);
        KeRevertToUserAffinityThreadEx(OldAffinity);
        OldAffinity = 0;
    }
    if (Context->WakerAffinity) OldAffinity = KeSetSystemAffinityThreadEx(Context->WakerAffinity);
    for (Round = 0; Round < Context->Rounds; Round++)
    {
        Status = KeWaitForSingleObject(&Context->ReadyEvent, Executive, KernelMode, FALSE, NULL);
        if (!NT_SUCCESS(Status))
        {
            Context->WaitFailures++;
            break;
        }

        Processor = KeGetCurrentProcessorNumber();
        Priority = KeQueryPriorityThread(KeGetCurrentThread());
        if (Priority < Context->WakerPriorityMin) Context->WakerPriorityMin = Priority;
        if (Priority > Context->WakerPriorityMax) Context->WakerPriorityMax = Priority;
        if ((ULONG)Context->MainProcessor != Processor) Context->ForwardRemoteRounds++;
        if ((PreviousProcessor != MAXULONG) && (PreviousProcessor != Processor)) Context->WakerMigrations++;
        PreviousProcessor = Processor;
        if (Processor < sizeof(KAFFINITY) * CHAR_BIT) Context->WakerProcessorMask |= (KAFFINITY)1 << Processor;
        if (Context->UseAddressWait)
        {
            InterlockedExchange64(&Context->Address, 1);
            KeMemoryBarrier();
            if (Context->PushLock.Value) ExfUnblockPushLock(&Context->PushLock, NULL);
        }
        InterlockedExchange(&Context->WakerProcessor, Processor);
        KeSetEvent(&Context->DoneEvent, IO_NO_INCREMENT, FALSE);
    }
    if (Context->WakerAffinity) KeRevertToUserAffinityThreadEx(OldAffinity);
}

static
VOID
TestAddressPushLockImmediateBehavior(VOID)
{
    static const SIZE_T AddressSizes[] = { sizeof(UCHAR), sizeof(USHORT), sizeof(ULONG), sizeof(ULONGLONG) };
    EX_PUSH_LOCK PushLock;
    LARGE_INTEGER ZeroTimeout;
    ULONGLONG Address, Compare;
    NTSTATUS Status;
    ULONG i;

    Address = 0x0102030405060708ULL;
    Compare = 0x1112131415161718ULL;
    for (i = 0; i < RTL_NUMBER_OF(AddressSizes); i++)
    {
        PushLock.Value = 0;
        Status = ExBlockOnAddressPushLock(&PushLock, &Address, &Compare, AddressSizes[i], NULL);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok_eq_pointer(PushLock.Ptr, NULL);
    }

    PushLock.Value = 0;
    Status = ExBlockOnAddressPushLock(&PushLock, &Address, &Compare, 3, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_pointer(PushLock.Ptr, NULL);

    Compare = Address;
    ZeroTimeout.QuadPart = 0;
    PushLock.Value = 0;
    Status = ExBlockOnAddressPushLock(&PushLock, &Address, &Compare, sizeof(Address), &ZeroTimeout);
    ok_eq_hex(Status, STATUS_TIMEOUT);
    ok_eq_pointer(PushLock.Ptr, NULL);
}

static
ULONGLONG
TestAddressPushLockRoundTrips(
    _In_ BOOLEAN UseAddressWait,
    _In_ KAFFINITY MainAffinity,
    _In_ KAFFINITY WakerAffinity,
    _In_ KAFFINITY WakerSeedAffinity,
    _In_ ULONG Rounds,
    _In_z_ PCSTR Label)
{
    ADDRESS_PUSH_LOCK_CONTEXT Context;
    LARGE_INTEGER Frequency, StartTime, EndTime, Timeout;
    LONGLONG Compare;
    KAFFINITY MainProcessorMask = 0, OldAffinity = 0;
    PKTHREAD WakerThread;
    NTSTATUS Status;
    ULONG MainMigrations = 0, MainProcessor, PreviousProcessor = MAXULONG, ReturnRemoteRounds = 0, Round, StatusFailures = 0, Timeouts = 0;
    ULONGLONG ElapsedMicroseconds;
    KPRIORITY MainPriority, MainPriorityMin = HIGH_PRIORITY, MainPriorityMax = 0;

    RtlZeroMemory(&Context, sizeof(Context));
    KeInitializeEvent(&Context.ReadyEvent, SynchronizationEvent, FALSE);
    KeInitializeEvent(&Context.DoneEvent, SynchronizationEvent, FALSE);
    Context.Rounds = Rounds;
    Context.WakerAffinity = WakerAffinity;
    Context.WakerSeedAffinity = WakerSeedAffinity;
    Context.UseAddressWait = UseAddressWait;
    Context.WakerPriorityMin = HIGH_PRIORITY;
    Compare = 0;
    Timeout.QuadPart = -1000LL * 10 * 1000;
    WakerThread = KmtStartThread(AddressPushLockWakeThread, &Context);
    if (MainAffinity) OldAffinity = KeSetSystemAffinityThreadEx(MainAffinity);
    StartTime = KeQueryPerformanceCounter(&Frequency);

    for (Round = 0; Round < Context.Rounds; Round++)
    {
        InterlockedExchange64(&Context.Address, 0);
        MainProcessor = KeGetCurrentProcessorNumber();
        if ((PreviousProcessor != MAXULONG) && (PreviousProcessor != MainProcessor)) MainMigrations++;
        PreviousProcessor = MainProcessor;
        if (MainProcessor < sizeof(KAFFINITY) * CHAR_BIT) MainProcessorMask |= (KAFFINITY)1 << MainProcessor;
        InterlockedExchange(&Context.MainProcessor, MainProcessor);
        KeSetEvent(&Context.ReadyEvent, IO_NO_INCREMENT, FALSE);
        if (UseAddressWait)
        {
            Status = ExBlockOnAddressPushLock(&Context.PushLock, &Context.Address, &Compare, sizeof(Context.Address), &Timeout);
            if (Status == STATUS_TIMEOUT) Timeouts++;
            else if (!NT_SUCCESS(Status)) StatusFailures++;
        }
        Status = KeWaitForSingleObject(&Context.DoneEvent, Executive, KernelMode, FALSE, NULL);
        if (!NT_SUCCESS(Status)) StatusFailures++;
        MainPriority = KeQueryPriorityThread(KeGetCurrentThread());
        if (MainPriority < MainPriorityMin) MainPriorityMin = MainPriority;
        if (MainPriority > MainPriorityMax) MainPriorityMax = MainPriority;
        MainProcessor = KeGetCurrentProcessorNumber();
        if ((ULONG)Context.WakerProcessor != MainProcessor) ReturnRemoteRounds++;
    }

    KmtFinishThread(WakerThread, NULL);
    EndTime = KeQueryPerformanceCounter(NULL);
    if (MainAffinity) KeRevertToUserAffinityThreadEx(OldAffinity);
    ElapsedMicroseconds = Frequency.QuadPart > 0 ? (ULONGLONG)(EndTime.QuadPart - StartTime.QuadPart) * 1000000 / Frequency.QuadPart : 0;
    trace("address push-lock %s: rounds=%lu elapsed-us=%I64u main-mask=%Ix waker-mask=%Ix forward-remote=%lu return-remote=%lu main-migrations=%lu waker-migrations=%lu main-prio=%ld-%ld waker-prio=%ld-%ld\n", Label, Context.Rounds, ElapsedMicroseconds, MainProcessorMask, Context.WakerProcessorMask, Context.ForwardRemoteRounds, ReturnRemoteRounds, MainMigrations, Context.WakerMigrations, MainPriorityMin, MainPriorityMax, Context.WakerPriorityMin, Context.WakerPriorityMax);
    if (MainAffinity && WakerAffinity)
    {
        ok_eq_ulong(Context.ForwardRemoteRounds, MainAffinity == WakerAffinity ? 0 : Context.Rounds);
        ok_eq_ulong(ReturnRemoteRounds, MainAffinity == WakerAffinity ? 0 : Context.Rounds);
    }
    else
    {
        /* Unpinned wakeups may select any allowed CPU, including an idle CPU
         * other than the signaling thread's CPU. Locality is a benchmark,
         * not an address-wait correctness guarantee. */
        KAFFINITY ActiveProcessors = KeQueryActiveProcessors();
        ok((MainProcessorMask & ~(MainAffinity ? MainAffinity : ActiveProcessors)) == 0,
           "main thread ran outside its affinity: %Ix\n", MainProcessorMask);
        ok((Context.WakerProcessorMask & ~(WakerAffinity ? WakerAffinity : ActiveProcessors)) == 0,
           "waker ran outside its affinity: %Ix\n", Context.WakerProcessorMask);
    }
    ok_eq_ulong(Context.WaitFailures, 0);
    ok_eq_ulong(StatusFailures, 0);
    ok_eq_ulong(Timeouts, 0);
    ok_eq_longlong(Context.Address, UseAddressWait ? 1 : 0);
    ok_eq_pointer(Context.PushLock.Ptr, NULL);
    return ElapsedMicroseconds;
}

static
VOID
TestAddressPushLockWake(VOID)
{
    TestAddressPushLockRoundTrips(TRUE, 0, 0, 0, ADDRESS_PUSH_LOCK_WAKE_ROUNDS, "wake");
}

#endif

START_TEST(ExPushLock)
{
#ifdef _M_ARM64
    TestAddressPushLockImmediateBehavior();
    TestAddressPushLockWake();
#else
    skip(TRUE, "ExBlockOnAddressPushLock is currently exposed for ARM64\n");
#endif
}
