/*
 * PROJECT:     LiberNT NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/arch/amd64/tlb.c
 * PURPOSE:     AMD64 translation lookaside buffer maintenance
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifdef MM_HOST_TEST
#include <mmcc/nvs/amd64/tlbshim.h>
#else
#include <mm/nt/mint.h>
#endif
#include "hardware.h"
#include "tlb.h"
#include "tlbops.h"

#define MI_AMD64_TLB_WATCHDOG_SECONDS 10

/*
 * Reading the clock is itself a trap on a virtual machine, far longer than
 * the acknowledgement being waited for, so the watchdog only looks after it
 * has let the spin go round this many times.
 */
#define MI_AMD64_TLB_WATCHDOG_SAMPLES 1024

typedef struct MI_CACHE_ALIGNED _MI_AMD64_TLB_MESSAGE
{
    ULONG64 Address;
    ULONG64 Pages;
    ULONG64 Published;
    ULONG64 Completed;
} MI_AMD64_TLB_MESSAGE;

#define MI_AMD64_TLB_WORDS (KE_MAXIMUM_PROCESSORS / 64)

typedef ULONG64 MI_AMD64_TLB_SET[MI_AMD64_TLB_WORDS];

static MI_AMD64_TLB_MESSAGE *MiAmd64TlbRows[KE_MAXIMUM_PROCESSORS];
static ULONG64 MiAmd64TlbGeneration[KE_MAXIMUM_PROCESSORS];
static ULONG MiAmd64TlbFeatures[KE_MAXIMUM_PROCESSORS];
volatile LONG KiTbFlushTimeStamp;

static
MI_AMD64_TLB_MESSAGE *
MiAmd64TlbMessage(ULONG Target, ULONG Source)
{
    MI_AMD64_TLB_MESSAGE *Row;

    MI_ASSERT(Target < KE_MAXIMUM_PROCESSORS && Source < KE_MAXIMUM_PROCESSORS && Target != Source);
    Row = (MI_AMD64_TLB_MESSAGE *)MI_AMD64_READ_ACQUIRE64((ULONG64 *)&MiAmd64TlbRows[Target > Source ? Target : Source]);
    MI_ASSERT(Row != NULL);
    return Target > Source ? &Row[Source] : &Row[Source + Target];
}

BOOLEAN
MiAmd64PrepareProcessorTlb(ULONG Cpu)
{
    ULONG_PTR Row;

    MI_ASSERT(Cpu < KE_MAXIMUM_PROCESSORS);
    if (Cpu == 0 || MiAmd64TlbRows[Cpu] != NULL)
        return TRUE;
    Row = (ULONG_PTR)MiAmd64TlbAllocate(((SIZE_T)2 * Cpu + 1) * sizeof(MI_AMD64_TLB_MESSAGE));
    if (Row == 0)
        return FALSE;
    Row = (Row + sizeof(MI_AMD64_TLB_MESSAGE) - 1) & ~((ULONG_PTR)sizeof(MI_AMD64_TLB_MESSAGE) - 1);
    MI_AMD64_WRITE_RELEASE64((ULONG64 *)&MiAmd64TlbRows[Cpu], (ULONG64)Row);
    return TRUE;
}

typedef struct _MI_AMD64_TLB_WATCHDOG
{
    ULONG64 Baseline;
    ULONG64 Epoch;
    ULONG64 Remaining;
    ULONG Samples;
    BOOLEAN Valid;
} MI_AMD64_TLB_WATCHDOG;

static
BOOLEAN
MiAmd64TlbWatchdogExpired(MI_AMD64_TLB_WATCHDOG *Watchdog, ULONG64 Remaining,
                          PULONG64 Frequency, PULONG64 Now)
{
    ULONG64 Before, After;

    if (Watchdog->Remaining != Remaining)
    {
        Watchdog->Remaining = Remaining;
        Watchdog->Valid = FALSE;
    }

    if (!MiAmd64TlbWatchdogEnabled())
    {
        Watchdog->Valid = FALSE;
        Watchdog->Samples = 0;
        return FALSE;
    }

    if (++Watchdog->Samples < MI_AMD64_TLB_WATCHDOG_SAMPLES)
        return FALSE;
    Watchdog->Samples = 0;

    Before = MiAmd64TlbWatchdogEpoch();
    *Now = MiAmd64TlbWatchdogNow(Frequency);
    After = MiAmd64TlbWatchdogEpoch();
    if (Before != After || MiAmd64TlbWatchdogFreezeActive() ||
        !MiAmd64TlbWatchdogEnabled() || *Frequency == 0)
    {
        Watchdog->Valid = FALSE;
        return FALSE;
    }

    if (!Watchdog->Valid || Watchdog->Epoch != After || *Now < Watchdog->Baseline)
    {
        Watchdog->Baseline = *Now;
        Watchdog->Epoch = After;
        Watchdog->Valid = TRUE;
        return FALSE;
    }

    return (*Now - Watchdog->Baseline) / *Frequency >= MI_AMD64_TLB_WATCHDOG_SECONDS;
}

ULONG
MiAmd64TlbCapabilities(VOID)
{
    BOOLEAN Enabled = MiAmd64TlbDisableInterrupts();
    ULONG Cpu = MiAmd64TlbCpu();
    ULONG Features;
    int Registers[4];

    MI_ASSERT(Cpu < KE_MAXIMUM_PROCESSORS);
    Features = MI_AMD64_READ_ACQUIRE32(&MiAmd64TlbFeatures[Cpu]);
    if (Features == 0)
    {
        Features = 1;
        MiAmd64TlbCpuid(Registers, 0);
        if ((ULONG)Registers[0] >= 7)
        {
            MiAmd64TlbCpuid(Registers, 7);
            if (Registers[1] & (1 << 10))
                Features |= MI_AMD64_TLB_INVPCID;
        }
        MI_AMD64_WRITE_RELEASE32(&MiAmd64TlbFeatures[Cpu], Features);
    }
    MiAmd64TlbRestoreInterrupts(Enabled);
    return Features & ~1u;
}

static
VOID
MiAmd64FlushLocal(ULONG64 Address, ULONG64 Pages)
{
    BOOLEAN Enabled = MiAmd64TlbDisableInterrupts();

    if (Pages == MI_AMD64_TLB_NON_GLOBAL && (MiAmd64TlbCapabilities() & MI_AMD64_TLB_INVPCID))
    {
        MiAmd64TlbInvpcidNonGlobal();
    }
    else if (Pages == MI_AMD64_TLB_NON_GLOBAL && !(MiAmd64TlbReadCr4() & (1ULL << 17)))
    {
        MiAmd64TlbWriteCr3(MiAmd64TlbReadCr3() & ~(1ULL << 63));
    }
    else if (Pages != 0 && Pages != MI_AMD64_TLB_NON_GLOBAL)
    {
        do
        {
            MiAmd64TlbPage(Address);
            Address += PAGE_SIZE;
        } while (--Pages != 0);
    }
    else if (MiAmd64TlbCapabilities() & MI_AMD64_TLB_INVPCID)
    {
        MiAmd64TlbInvpcid();
    }
    else
    {
        ULONG64 Cr4 = MiAmd64TlbReadCr4();
        if (Cr4 & ((1ULL << 7) | (1ULL << 17)))
        {
            MiAmd64TlbWriteCr4(Cr4 ^ (1ULL << 7));
            MiAmd64TlbWriteCr4(Cr4);
        }
        else
        {
            MiAmd64TlbWriteCr3(MiAmd64TlbReadCr3() & ~(1ULL << 63));
        }
    }
    MiAmd64TlbRestoreInterrupts(Enabled);
}

VOID
MiAmd64ProcessTlbRequest(ULONG Source)
{
    BOOLEAN Enabled = MiAmd64TlbDisableInterrupts();
    ULONG Cpu = MiAmd64TlbCpu();
    MI_AMD64_TLB_MESSAGE *Message;
    ULONG64 Published, Completed, Address, Pages;

    Message = MiAmd64TlbMessage(Cpu, Source);
    Completed = MI_AMD64_READ_ACQUIRE64(&Message->Completed);
    Published = MI_AMD64_READ_ACQUIRE64(&Message->Published);
    if (Published != Completed)
    {
        Address = MI_AMD64_READ_NOFENCE64(&Message->Address);
        Pages = MI_AMD64_READ_NOFENCE64(&Message->Pages);
        if (MI_AMD64_READ_ACQUIRE64(&Message->Completed) == Completed &&
            MI_AMD64_READ_ACQUIRE64(&Message->Published) == Published)
        {
            MiAmd64FlushLocal(Address, Pages);
            (VOID)MI_AMD64_COMPARE_EXCHANGE64(&Message->Completed, Completed, Published);
        }
    }
    MiAmd64TlbRestoreInterrupts(Enabled);
}

static
ULONG64
MiAmd64TlbPending(ULONG Source, ULONG64 *Remaining, ULONG64 Generation)
{
    ULONG64 Remote, Count = 0;
    ULONG Target, Word, Words = MiAmd64TlbWords();

    for (Word = 0; Word < Words; Word++)
    {
        for (Remote = Remaining[Word]; Remote != 0; Remote &= Remote - 1)
        {
            MI_AMD64_TLB_MESSAGE *Message;
            Target = Word * 64 + MI_AMD64_CTZ64(Remote);
            Message = MiAmd64TlbMessage(Target, Source);
            if ((LONG64)(MI_AMD64_READ_ACQUIRE64(&Message->Completed) - Generation) >= 0)
                Remaining[Word] &= ~(1ULL << (Target % 64));
            else
                Count++;
        }
    }
    return Count;
}

static
VOID
MiAmd64TlbWaitCompleted(ULONG Source, ULONG64 *Targets, ULONG64 Generation)
{
    ULONG64 Frequency = 0, Now = 0, Remaining;
    MI_AMD64_TLB_WATCHDOG Watchdog = {0};

    MI_ASSERT(Source < KE_MAXIMUM_PROCESSORS);
    for (;;)
    {
        Remaining = MiAmd64TlbPending(Source, Targets, Generation);
        if (Remaining == 0)
            break;
        if (MiAmd64TlbWatchdogExpired(&Watchdog, Remaining, &Frequency, &Now))
        {
            Remaining = MiAmd64TlbPending(Source, Targets, Generation);
            if (Remaining == Watchdog.Remaining &&
                MiAmd64TlbWatchdogEnabled() &&
                !MiAmd64TlbWatchdogFreezeActive() &&
                MiAmd64TlbWatchdogEpoch() == Watchdog.Epoch)
                MiAmd64TlbWatchdogBugcheck(Frequency, Now, Watchdog.Baseline);
            else
                Watchdog.Valid = FALSE;
        }
        MiAmd64TlbPoll();
        MiAmd64TlbPause();
    }
}

static
VOID
MiAmd64ShootDown(const ULONG64 *Targets, BOOLEAN AllActive, BOOLEAN IncludeSelf, ULONG64 Address, ULONG64 Pages)
{
    KIRQL Previous = MiAmd64TlbRaiseIrql();
    ULONG Cpu = MiAmd64TlbCpu(), Target, Word, Words = MiAmd64TlbWords();
    ULONG64 Remaining, Generation = 0;
    MI_AMD64_TLB_SET Remote, Single;
    BOOLEAN AnyRemote = FALSE;

    MI_ASSERT(Cpu < KE_MAXIMUM_PROCESSORS && Words != 0 && Words <= MI_AMD64_TLB_WORDS);
    for (Word = 0; Word < Words; Word++)
    {
        Remote[Word] = MiAmd64TlbActiveCpus(Word);
        if (!AllActive)
            Remote[Word] &= Targets != NULL ? Targets[Word] : 0;
        if (Word == Cpu / 64)
        {
            if (!AllActive && Targets != NULL && (Targets[Word] & (1ULL << (Cpu % 64))))
                IncludeSelf = TRUE;
            Remote[Word] &= ~(1ULL << (Cpu % 64));
        }
        if (Remote[Word] != 0)
            AnyRemote = TRUE;
        Single[Word] = 0;
    }
    Address &= ~((ULONG64)PAGE_SIZE - 1);
    if (Pages > MI_AMD64_TLB_USER_RANGE_LIMIT && Address < MI_AMD64_USER_END &&
        Pages - 1 < ((MI_AMD64_USER_END - Address) >> PAGE_SHIFT))
        Pages = MI_AMD64_TLB_NON_GLOBAL;
    else if (Pages > MI_AMD64_TLB_RANGE_LIMIT ||
             !MiAmd64CanonicalAddress(Address) ||
             (Pages != 0 && (Pages - 1 > (~Address >> PAGE_SHIFT) ||
                            !MiAmd64CanonicalAddress(Address + ((Pages - 1) << PAGE_SHIFT)))))
        Pages = 0;
    if (AnyRemote)
    {
        Generation = MiAmd64TlbGeneration[Cpu] + 1;
        MI_ASSERT(Generation != 0);
        MiAmd64TlbGeneration[Cpu] = Generation;
    }
    for (Word = 0; Word < Words; Word++)
    {
        for (Remaining = Remote[Word]; Remaining != 0; Remaining &= Remaining - 1)
        {
            MI_AMD64_TLB_MESSAGE *Message;
            ULONG64 Published;
            Target = Word * 64 + MI_AMD64_CTZ64(Remaining);
            Message = MiAmd64TlbMessage(Target, Cpu);
            Published = MI_AMD64_READ_NOFENCE64(&Message->Published);
            Single[Word] = 1ULL << (Target % 64);
            MiAmd64TlbWaitCompleted(Cpu, Single, Published);
            MI_ASSERT(Single[Word] == 0);
            MI_ASSERT(Published == MI_AMD64_READ_ACQUIRE64(&Message->Completed));
            MI_ASSERT((LONG64)(Generation - Published) > 0);
            MI_AMD64_WRITE_RELEASE64(&Message->Address, Address);
            MI_AMD64_WRITE_RELEASE64(&Message->Pages, Pages);
            MI_AMD64_WRITE_RELEASE64(&Message->Published, Generation);
        }
    }
    for (Word = 0; Word < Words; Word++)
    {
        if (Remote[Word] != 0)
            KiSendMemoryIpi(Word, Remote[Word]);
    }
    if (IncludeSelf)
        MiAmd64FlushLocal(Address, Pages);
    if (AnyRemote && MiAmd64TlbPreemptibleWait(Previous))
        MiAmd64TlbLowerForWait();
    MiAmd64TlbWaitCompleted(Cpu, Remote, Generation);
    MiAmd64TlbLowerIrql(Previous);
}

VOID
MiAmd64FlushTargets(ULONG64 Targets, ULONG64 Address, ULONG64 Pages)
{
    MI_AMD64_TLB_SET Set = {0};

    Set[0] = Targets;
    MiAmd64ShootDown(Set, FALSE, FALSE, Address, Pages);
}

VOID
MiArchInvalidateTlbRange(PVOID BaseAddress, SIZE_T Size, MI_TLB_SCOPE Scope)
{
    ULONG64 Address = (ULONG_PTR)BaseAddress;
    ULONG64 Pages;

    if (Size == 0)
        return;
    Pages = ((Size - 1) >> PAGE_SHIFT) + 1;
    Pages += (((Size - 1) & (PAGE_SIZE - 1)) + (Address & (PAGE_SIZE - 1))) >> PAGE_SHIFT;
    MiAmd64ShootDown(NULL, Scope == MiTlbAllProcessors, TRUE, Address, Pages);
}

VOID
MiArchInvalidateTlbSingle(PVOID Address, MI_TLB_SCOPE Scope)
{
    MiArchInvalidateTlbRange(Address, 1, Scope);
}

VOID
MiArchInvalidateTlbAll(MI_TLB_SCOPE Scope)
{
    MiAmd64ShootDown(NULL, Scope == MiTlbAllProcessors, TRUE, 0, 0);
}

VOID
NTAPI
KeFlushCurrentTb(VOID)
{
    MiAmd64FlushLocal(0, 0);
}

VOID
NTAPI
KeFlushEntireTb(BOOLEAN Invalid, BOOLEAN AllProcessors)
{
    UNREFERENCED_PARAMETER(Invalid);
    MiArchInvalidateTlbAll(AllProcessors ? MiTlbAllProcessors : MiTlbLocal);
    MI_AMD64_INCREMENT_NOFENCE(&KiTbFlushTimeStamp);
}

VOID
MiArchTlbInvalidate(ULONG64 VirtualAddress, ULONG64 PageCount, BOOLEAN AllProcessors)
{
    MI_TLB_SCOPE Scope = AllProcessors ? MiTlbAllProcessors : MiTlbLocal;
    if (PageCount > ((SIZE_T)-1 >> PAGE_SHIFT))
        MiArchInvalidateTlbAll(Scope);
    else
        MiArchInvalidateTlbRange((PVOID)(ULONG_PTR)VirtualAddress,
                                 (SIZE_T)(PageCount << PAGE_SHIFT), Scope);
}

VOID
MiArchTlbInvalidateProcessors(ULONG64 VirtualAddress, ULONG64 PageCount, ULONG64 Processors)
{
    MI_AMD64_TLB_SET Set = {0};

    if (PageCount == 0)
        return;
    if (PageCount > ((SIZE_T)-1 >> PAGE_SHIFT))
        PageCount = 0;
    Set[0] = Processors;
    /* FIXME: an owner's processors past the first 64 are not passed down; with more than 64 every processor is flushed */
    MiAmd64ShootDown(Set, MiAmd64TlbWords() > 1, TRUE, VirtualAddress, PageCount);
}

VOID
MiArchTlbInvalidateAll(BOOLEAN AllProcessors)
{
    MiArchInvalidateTlbAll(AllProcessors ? MiTlbAllProcessors : MiTlbLocal);
}
