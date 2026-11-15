/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Process manager inline helpers: flags, notify routines, locks
 * COPYRIGHT:   Copyright Alex Ionescu <alex.ionescu@reactos.org>
 *              Copyright Thomas Weidenmueller <w3seek@reactos.org>
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define PspPrioritySeparationFromMask(Mask) ((Mask) & 0x03)
#define PspQuantumTypeFromMask(Mask) ((Mask) & 0x0C)
#define PspQuantumLengthFromMask(Mask) ((Mask) & 0x30)

#define PspSetCrossThreadFlag(Thread, Flag) InterlockedOr((PLONG)&(Thread)->CrossThreadFlags, (Flag))
#define PspClearCrossThreadFlag(Thread, Flag) InterlockedAnd((PLONG)&(Thread)->CrossThreadFlags, ~(Flag))
#define PspSetProcessFlag(Process, Flag) InterlockedOr((PLONG)&(Process)->Flags, (Flag))
#define PspClearProcessFlag(Process, Flag) InterlockedAnd((PLONG)&(Process)->Flags, ~(Flag))

FORCEINLINE
PEPROCESS
_PsGetCurrentProcess(VOID)
{
    return (PEPROCESS)KeGetCurrentThread()->ApcState.Process;
}

FORCEINLINE
VOID
PspRunCreateThreadNotifyRoutines(
    _In_ PETHREAD Thread,
    _In_ BOOLEAN Create)
{
    ULONG Slot;

    if (PspThreadNotifyRoutineCount == 0)
        return;

    for (Slot = 0; Slot < PSP_MAX_CREATE_THREAD_NOTIFY; Slot++)
    {
        ExDoCallBack(&PspThreadNotifyRoutine[Slot], Thread->Cid.UniqueProcess, Thread->Cid.UniqueThread,
                     (PVOID)(ULONG_PTR)Create);
    }
}

FORCEINLINE
VOID
PspRunCreateProcessNotifyRoutines(
    _In_ PEPROCESS Process,
    _In_ BOOLEAN Create)
{
    PS_CREATE_NOTIFY_INFO CreateInfo;
    PPS_CREATE_NOTIFY_INFO Info = NULL;
    PEX_CALLBACK_ROUTINE_BLOCK Block;
    PVOID Routine;
    ULONG Slot;

    if (PspProcessNotifyRoutineCount == 0)
        return;

    /* Only the extended routines get it, and only for a process being created */
    if (Create)
    {
        RtlZeroMemory(&CreateInfo, sizeof(CreateInfo));
        CreateInfo.Size = sizeof(CreateInfo);
        CreateInfo.ParentProcessId = Process->InheritedFromUniqueProcessId;
        CreateInfo.CreatingThreadId = PsGetCurrentThread()->Cid;
        CreateInfo.CreationStatus = STATUS_SUCCESS;
        if (Process->SeAuditProcessCreationInfo.ImageFileName != NULL)
        {
            CreateInfo.ImageFileName = &Process->SeAuditProcessCreationInfo.ImageFileName->Name;
            CreateInfo.FileOpenNameAvailable = TRUE;
        }
        Info = &CreateInfo;
    }

    for (Slot = 0; Slot < PSP_MAX_CREATE_PROCESS_NOTIFY; Slot++)
    {
        Block = ExReferenceCallBackBlock(&PspProcessNotifyRoutine[Slot]);
        if (Block == NULL)
            continue;

        Routine = (PVOID)ExGetCallBackBlockRoutine(Block);
        if (ExGetCallBackBlockContext(Block) != NULL)
            ((PCREATE_PROCESS_NOTIFY_ROUTINE_EX)Routine)(Process, Process->UniqueProcessId, Info);
        else
            ((PCREATE_PROCESS_NOTIFY_ROUTINE)Routine)(Process->InheritedFromUniqueProcessId, Process->UniqueProcessId, Create);

        ExDereferenceCallBackBlock(&PspProcessNotifyRoutine[Slot], Block);
    }
}

FORCEINLINE
VOID
PspRunLoadImageNotifyRoutines(
    _In_opt_ PUNICODE_STRING FullImageName,
    _In_ HANDLE ProcessId,
    _In_ PIMAGE_INFO ImageInfo)
{
    ULONG Slot;

    for (Slot = 0; Slot < PSP_MAX_LOAD_IMAGE_NOTIFY; Slot++)
        ExDoCallBack(&PspLoadImageNotifyRoutine[Slot], FullImageName, ProcessId, ImageInfo);
}

FORCEINLINE
VOID
PspRunLegoRoutine(
    _In_ PKTHREAD Thread)
{
    if (PspLegoNotifyRoutine != NULL)
        PspLegoNotifyRoutine(Thread);
}

FORCEINLINE
VOID
PspLockSecurity(
    _Inout_ PEX_PUSH_LOCK Lock,
    _In_ BOOLEAN Exclusive)
{
    KeEnterCriticalRegion();
    if (Exclusive)
        ExAcquirePushLockExclusive(Lock);
    else
        ExAcquirePushLockShared(Lock);
}

FORCEINLINE
VOID
PspUnlockSecurity(
    _Inout_ PEX_PUSH_LOCK Lock,
    _In_ BOOLEAN Exclusive)
{
    if (Exclusive)
        ExReleasePushLockExclusive(Lock);
    else
        ExReleasePushLockShared(Lock);
    KeLeaveCriticalRegion();
}

#define PspLockProcessSecurityShared(Process) PspLockSecurity(&(Process)->ProcessLock, FALSE)
#define PspUnlockProcessSecurityShared(Process) PspUnlockSecurity(&(Process)->ProcessLock, FALSE)
#define PspLockProcessSecurityExclusive(Process) PspLockSecurity(&(Process)->ProcessLock, TRUE)
#define PspUnlockProcessSecurityExclusive(Process) PspUnlockSecurity(&(Process)->ProcessLock, TRUE)
#define PspLockThreadSecurityShared(Thread) PspLockSecurity(&(Thread)->ThreadLock, FALSE)
#define PspUnlockThreadSecurityShared(Thread) PspUnlockSecurity(&(Thread)->ThreadLock, FALSE)
#define PspLockThreadSecurityExclusive(Thread) PspLockSecurity(&(Thread)->ThreadLock, TRUE)
#define PspUnlockThreadSecurityExclusive(Thread) PspUnlockSecurity(&(Thread)->ThreadLock, TRUE)
