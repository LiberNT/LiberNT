/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     How much pool and pagefile a process may use, and the block
 *              that keeps that account for a group of processes
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "pspriv.h"
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

EPROCESS_QUOTA_BLOCK PspDefaultQuotaBlock;

static LIST_ENTRY PspQuotaBlockList;
static KGUARDED_MUTEX PspQuotaLock;

/* PRIVATE FUNCTIONS **********************************************************/

/*
 * Charges one pool against a block. A block with no limit on that pool never
 * refuses, which is what the default block does: it keeps the account so a
 * caller can read it back, without standing in anybody's way.
 */
static
NTSTATUS
PspChargeQuotaBlock(
    _Inout_ PEPROCESS_QUOTA_BLOCK Block,
    _In_ PS_QUOTA_TYPE QuotaType,
    _In_ SIZE_T Amount)
{
    PEPROCESS_QUOTA_ENTRY Entry = &Block->QuotaEntry[QuotaType];
    SIZE_T New;

    KeAcquireGuardedMutex(&PspQuotaLock);

    New = Entry->Usage + Amount;

    /* An account that would wrap is as good as one that would overrun */
    if (New < Entry->Usage ||
        (Entry->Limit != 0 && New > Entry->Limit))
    {
        KeReleaseGuardedMutex(&PspQuotaLock);
        return STATUS_QUOTA_EXCEEDED;
    }

    Entry->Usage = New;
    if (New > Entry->Peak) Entry->Peak = New;

    KeReleaseGuardedMutex(&PspQuotaLock);
    return STATUS_SUCCESS;
}

static
VOID
PspReturnQuotaBlock(
    _Inout_ PEPROCESS_QUOTA_BLOCK Block,
    _In_ PS_QUOTA_TYPE QuotaType,
    _In_ SIZE_T Amount)
{
    PEPROCESS_QUOTA_ENTRY Entry = &Block->QuotaEntry[QuotaType];

    KeAcquireGuardedMutex(&PspQuotaLock);

    /*
     * Returning more than was charged would turn the account negative and
     * make every later charge succeed, so the account is emptied instead.
     */
    if (Amount > Entry->Usage) Entry->Usage = 0;
    else Entry->Usage -= Amount;

    KeReleaseGuardedMutex(&PspQuotaLock);
}

static
PS_QUOTA_TYPE
PspQuotaTypeFromPool(
    _In_ POOL_TYPE PoolType)
{
    /* Only the two pools reach a caller from outside; the pagefile account
     * is charged by the memory manager by name */
    return (PoolType == PagedPool) ? PsPagedPool : PsNonPagedPool;
}

/* PUBLIC FUNCTIONS ***********************************************************/

CODE_SEG("INIT")
VOID
NTAPI
PsInitializeQuotaSystem(VOID)
{
    PAGED_CODE();

    KeInitializeGuardedMutex(&PspQuotaLock);
    InitializeListHead(&PspQuotaBlockList);

    RtlZeroMemory(&PspDefaultQuotaBlock, sizeof(PspDefaultQuotaBlock));
    InitializeListHead(&PspDefaultQuotaBlock.QuotaList);

    /*
     * The block every process shares until somebody sets a limit keeps no
     * limit of its own: the system's own pool accounting is what bounds it.
     */
    PspDefaultQuotaBlock.ReferenceCount = 1;
    PspDefaultQuotaBlock.ProcessCount = 1;
}

VOID
NTAPI
PspInsertQuotaBlock(
    _In_ PEPROCESS_QUOTA_BLOCK QuotaBlock)
{
    PAGED_CODE();

    KeAcquireGuardedMutex(&PspQuotaLock);
    InsertTailList(&PspQuotaBlockList, &QuotaBlock->QuotaList);
    KeReleaseGuardedMutex(&PspQuotaLock);
}

/*
 * A new process shares its parent's block, so a limit set on a parent bounds
 * the whole tree below it. A process with no parent shares the default one.
 */
VOID
NTAPI
PspInheritQuota(
    _In_ PEPROCESS Process,
    _In_opt_ PEPROCESS ParentProcess)
{
    PEPROCESS_QUOTA_BLOCK Block = &PspDefaultQuotaBlock;

    PAGED_CODE();

    if (ParentProcess != NULL && ParentProcess->QuotaBlock != NULL)
        Block = ParentProcess->QuotaBlock;

    KeAcquireGuardedMutex(&PspQuotaLock);
    Block->ReferenceCount++;
    Block->ProcessCount++;
    KeReleaseGuardedMutex(&PspQuotaLock);

    Process->QuotaBlock = Block;
}

/*
 * Gives a block back. The last process to let go of a block that is not the
 * default one frees it, because nothing else can reach it any more.
 */
VOID
NTAPI
PspDereferenceQuotaBlock(
    _In_opt_ PEPROCESS Process,
    _In_ PEPROCESS_QUOTA_BLOCK QuotaBlock)
{
    BOOLEAN Free = FALSE;

    PAGED_CODE();

    if (QuotaBlock == NULL) return;
    if (Process != NULL) Process->QuotaBlock = NULL;

    KeAcquireGuardedMutex(&PspQuotaLock);

    if (Process != NULL && QuotaBlock->ProcessCount != 0)
        QuotaBlock->ProcessCount--;

    if (QuotaBlock->ReferenceCount != 0) QuotaBlock->ReferenceCount--;

    if (QuotaBlock->ReferenceCount == 0 &&
        QuotaBlock != &PspDefaultQuotaBlock)
    {
        RemoveEntryList(&QuotaBlock->QuotaList);
        Free = TRUE;
    }

    KeReleaseGuardedMutex(&PspQuotaLock);

    if (Free) ExFreePoolWithTag(QuotaBlock, TAG_QUOTA_BLOCK);
}

/*
 * Everything the process still has charged is given back. This runs once,
 * when the process object is destroyed, so the block's account does not keep
 * what a process that is gone was using.
 */
VOID
NTAPI
PspReturnQuotasOnDestroy(
    _In_ PEPROCESS Process)
{
    PEPROCESS_QUOTA_BLOCK Block = Process->QuotaBlock;

    PAGED_CODE();

    if (Block == NULL) return;

    PspReturnQuotaBlock(Block, PsPagedPool, Process->ProcessQuotaUsage[1]);
    PspReturnQuotaBlock(Block, PsNonPagedPool, Process->ProcessQuotaUsage[0]);

    Process->ProcessQuotaUsage[0] = 0;
    Process->ProcessQuotaUsage[1] = 0;
}

/*
 * Hands back to the pool whatever the process holds above what it is using,
 * which is what the memory manager asks for when the system is short.
 */
VOID
NTAPI
PspReturnExcessQuotas(
    _In_ PEPROCESS Process)
{
    PAGED_CODE();

    UNREFERENCED_PARAMETER(Process);

    /* TODO: give back the charged-but-unused part of each pool account once
     * the pool tells the process manager how much of a charge it still holds */
}

/*
 * Charges a pool to a process, keeping both the process's own account and
 * its block's. The process account is what a caller reads back; the block's
 * is what the limit is checked against.
 */
NTSTATUS
NTAPI
PspChargeProcessQuotaSpecifiedPool(
    _In_ PEPROCESS Process,
    _In_ ULONG PoolIndex,
    _In_ SIZE_T Amount)
{
    PEPROCESS_QUOTA_BLOCK Block = Process->QuotaBlock;
    NTSTATUS Status;

    if (Block == NULL) return STATUS_SUCCESS;

    Status = PspChargeQuotaBlock(Block, (PS_QUOTA_TYPE)PoolIndex, Amount);
    if (!NT_SUCCESS(Status)) return Status;

    Process->ProcessQuotaUsage[PoolIndex] += Amount;
    if (Process->ProcessQuotaUsage[PoolIndex] >
        Process->ProcessQuotaPeak[PoolIndex])
    {
        Process->ProcessQuotaPeak[PoolIndex] =
            Process->ProcessQuotaUsage[PoolIndex];
    }

    return STATUS_SUCCESS;
}

VOID
NTAPI
PspReturnProcessQuotaSpecifiedPool(
    _In_ PEPROCESS Process,
    _In_ ULONG PoolIndex,
    _In_ SIZE_T Amount)
{
    PEPROCESS_QUOTA_BLOCK Block = Process->QuotaBlock;

    if (Block == NULL) return;

    if (Amount > Process->ProcessQuotaUsage[PoolIndex])
        Amount = Process->ProcessQuotaUsage[PoolIndex];

    Process->ProcessQuotaUsage[PoolIndex] -= Amount;
    PspReturnQuotaBlock(Block, (PS_QUOTA_TYPE)PoolIndex, Amount);
}

NTSTATUS
NTAPI
PsChargeProcessPoolQuota(
    _In_ PEPROCESS Process,
    _In_ POOL_TYPE PoolType,
    _In_ SIZE_T Amount)
{
    return PspChargeProcessQuotaSpecifiedPool(Process,
                                              PspQuotaTypeFromPool(PoolType),
                                              Amount);
}

/*
 * The same charge, for a caller that would rather be unwound than told. The
 * pool allocator calls this, because an allocation that cannot be charged is
 * an allocation that cannot be made.
 */
VOID
NTAPI
PsChargePoolQuota(
    _In_ PEPROCESS Process,
    _In_ POOL_TYPE PoolType,
    _In_ SIZE_T Amount)
{
    NTSTATUS Status;

    Status = PsChargeProcessPoolQuota(Process, PoolType, Amount);
    if (!NT_SUCCESS(Status)) ExRaiseStatus(Status);
}

NTSTATUS
NTAPI
PsChargeProcessPagedPoolQuota(
    _In_ PEPROCESS Process,
    _In_ SIZE_T Amount)
{
    return PspChargeProcessQuotaSpecifiedPool(Process, PsPagedPool, Amount);
}

NTSTATUS
NTAPI
PsChargeProcessNonPagedPoolQuota(
    _In_ PEPROCESS Process,
    _In_ SIZE_T Amount)
{
    return PspChargeProcessQuotaSpecifiedPool(Process, PsNonPagedPool, Amount);
}

VOID
NTAPI
PsReturnPoolQuota(
    _In_ PEPROCESS Process,
    _In_ POOL_TYPE PoolType,
    _In_ SIZE_T Amount)
{
    PspReturnProcessQuotaSpecifiedPool(Process,
                                       PspQuotaTypeFromPool(PoolType),
                                       Amount);
}

VOID
NTAPI
PsReturnProcessPagedPoolQuota(
    _In_ PEPROCESS Process,
    _In_ SIZE_T Amount)
{
    PspReturnProcessQuotaSpecifiedPool(Process, PsPagedPool, Amount);
}

VOID
NTAPI
PsReturnProcessNonPagedPoolQuota(
    _In_ PEPROCESS Process,
    _In_ SIZE_T Amount)
{
    PspReturnProcessQuotaSpecifiedPool(Process, PsNonPagedPool, Amount);
}

NTSTATUS
NTAPI
PsChargeProcessPageFileQuota(
    _In_ PEPROCESS Process,
    _In_ SIZE_T Amount)
{
    PEPROCESS_QUOTA_BLOCK Block = Process->QuotaBlock;

    if (Block == NULL) return STATUS_SUCCESS;

    return PspChargeQuotaBlock(Block, PsPageFile, Amount);
}

NTSTATUS
NTAPI
PsReturnProcessPageFileQuota(
    _In_ PEPROCESS Process,
    _In_ SIZE_T Amount)
{
    PEPROCESS_QUOTA_BLOCK Block = Process->QuotaBlock;

    if (Block == NULL) return STATUS_SUCCESS;

    PspReturnQuotaBlock(Block, PsPageFile, Amount);
    return STATUS_SUCCESS;
}

/*
 * Charges both pools at once against the process's block and hands the block
 * back, so a caller that holds something shared charges it once and gives it
 * back against the same block even if the process is gone by then.
 */
PEPROCESS_QUOTA_BLOCK
NTAPI
PsChargeSharedPoolQuota(
    _In_ PEPROCESS Process,
    _In_ SIZE_T AmountToChargePaged,
    _In_ SIZE_T AmountToChargeNonPaged)
{
    PEPROCESS_QUOTA_BLOCK Block = Process->QuotaBlock;

    if (Block == NULL) return NULL;

    if (AmountToChargePaged != 0 &&
        !NT_SUCCESS(PspChargeQuotaBlock(Block,
                                        PsPagedPool,
                                        AmountToChargePaged)))
    {
        return NULL;
    }

    if (AmountToChargeNonPaged != 0 &&
        !NT_SUCCESS(PspChargeQuotaBlock(Block,
                                        PsNonPagedPool,
                                        AmountToChargeNonPaged)))
    {
        if (AmountToChargePaged != 0)
            PspReturnQuotaBlock(Block, PsPagedPool, AmountToChargePaged);

        return NULL;
    }

    KeAcquireGuardedMutex(&PspQuotaLock);
    Block->ReferenceCount++;
    KeReleaseGuardedMutex(&PspQuotaLock);

    return Block;
}

VOID
NTAPI
PsReturnSharedPoolQuota(
    _In_ PEPROCESS_QUOTA_BLOCK QuotaBlock,
    _In_ SIZE_T AmountToReturnPaged,
    _In_ SIZE_T AmountToReturnNonPaged)
{
    if (QuotaBlock == NULL) return;

    if (AmountToReturnPaged != 0)
        PspReturnQuotaBlock(QuotaBlock, PsPagedPool, AmountToReturnPaged);

    if (AmountToReturnNonPaged != 0)
        PspReturnQuotaBlock(QuotaBlock, PsNonPagedPool, AmountToReturnNonPaged);

    PspDereferenceQuotaBlock(NULL, QuotaBlock);
}

/*
 * Reads the limits a process runs under. What is reported is the block's
 * limits and the process's own working-set bounds, because the first is
 * shared and the second is not.
 */
NTSTATUS
NTAPI
PspQueryQuotaLimits(
    _In_ PEPROCESS Process,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length,
    _Out_ PULONG ReturnLength)
{
    PEPROCESS_QUOTA_BLOCK Block = Process->QuotaBlock;
    QUOTA_LIMITS_EX Limits;

    PAGED_CODE();

    if (Length != sizeof(QUOTA_LIMITS) &&
        Length != sizeof(QUOTA_LIMITS_EX))
    {
        *ReturnLength = sizeof(QUOTA_LIMITS);
        return STATUS_INFO_LENGTH_MISMATCH;
    }

    RtlZeroMemory(&Limits, sizeof(Limits));

    if (Block != NULL)
    {
        Limits.PagedPoolLimit = Block->QuotaEntry[PsPagedPool].Limit;
        Limits.NonPagedPoolLimit = Block->QuotaEntry[PsNonPagedPool].Limit;
        Limits.PagefileLimit = Block->QuotaEntry[PsPageFile].Limit;
    }

    Limits.MinimumWorkingSetSize = Process->Vm.Instance.MinimumWorkingSetSize;
    Limits.MaximumWorkingSetSize = Process->Vm.Instance.MaximumWorkingSetSize;
    Limits.TimeLimit.QuadPart = -1LL;

    RtlCopyMemory(Buffer, &Limits, Length);
    *ReturnLength = Length;

    return STATUS_SUCCESS;
}

/*
 * Sets the limits a process runs under. Raising a limit needs the privilege
 * for it; lowering one does not. A caller that passes only the working-set
 * bounds is asking the memory manager, not the quota accounting.
 */
NTSTATUS
NTAPI
PspSetQuotaLimits(
    _In_ PEPROCESS Process,
    _In_ ULONG Unused,
    _In_ PVOID QuotaLimits,
    _In_ ULONG QuotaLimitsLength,
    _In_ KPROCESSOR_MODE PreviousMode)
{
    PEPROCESS_QUOTA_BLOCK Block;
    QUOTA_LIMITS_EX Limits;
    BOOLEAN Privileged;

    PAGED_CODE();

    UNREFERENCED_PARAMETER(Unused);

    if (QuotaLimitsLength != sizeof(QUOTA_LIMITS) &&
        QuotaLimitsLength != sizeof(QUOTA_LIMITS_EX))
    {
        return STATUS_INFO_LENGTH_MISMATCH;
    }

    RtlZeroMemory(&Limits, sizeof(Limits));

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForRead(QuotaLimits, QuotaLimitsLength, sizeof(ULONG_PTR));
            RtlCopyMemory(&Limits, QuotaLimits, QuotaLimitsLength);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }
    else
    {
        RtlCopyMemory(&Limits, QuotaLimits, QuotaLimitsLength);
    }

    /*
     * Only the working-set bounds were given, so this is the documented way
     * of asking the memory manager to keep a process in memory, and the
     * limits that are shared are left alone.
     */
    if (Limits.PagedPoolLimit == 0 &&
        Limits.NonPagedPoolLimit == 0 &&
        Limits.PagefileLimit == 0)
    {
        if (Limits.MinimumWorkingSetSize == 0 &&
            Limits.MaximumWorkingSetSize == 0)
        {
            return STATUS_INVALID_PARAMETER;
        }

        return MmAdjustWorkingSetSize(Limits.MinimumWorkingSetSize,
                                      Limits.MaximumWorkingSetSize,
                                      0,
                                      FALSE);
    }

    Privileged = SeSinglePrivilegeCheck(SeIncreaseQuotaPrivilege, PreviousMode);

    Block = Process->QuotaBlock;
    if (Block == NULL) return STATUS_SUCCESS;

    KeAcquireGuardedMutex(&PspQuotaLock);

    /*
     * A block that is still the one every process shares cannot be given
     * limits, because that would bound the whole system.
     */
    if (Block == &PspDefaultQuotaBlock)
    {
        KeReleaseGuardedMutex(&PspQuotaLock);
        return STATUS_ACCESS_DENIED;
    }

    if (!Privileged)
    {
        if ((Limits.PagedPoolLimit != 0 &&
             Limits.PagedPoolLimit > Block->QuotaEntry[PsPagedPool].Limit) ||
            (Limits.NonPagedPoolLimit != 0 &&
             Limits.NonPagedPoolLimit > Block->QuotaEntry[PsNonPagedPool].Limit) ||
            (Limits.PagefileLimit != 0 &&
             Limits.PagefileLimit > Block->QuotaEntry[PsPageFile].Limit))
        {
            KeReleaseGuardedMutex(&PspQuotaLock);
            return STATUS_PRIVILEGE_NOT_HELD;
        }
    }

    if (Limits.PagedPoolLimit != 0)
        Block->QuotaEntry[PsPagedPool].Limit = Limits.PagedPoolLimit;

    if (Limits.NonPagedPoolLimit != 0)
        Block->QuotaEntry[PsNonPagedPool].Limit = Limits.NonPagedPoolLimit;

    if (Limits.PagefileLimit != 0)
        Block->QuotaEntry[PsPageFile].Limit = Limits.PagefileLimit;

    KeReleaseGuardedMutex(&PspQuotaLock);

    return STATUS_SUCCESS;
}
