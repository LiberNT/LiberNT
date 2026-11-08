/*
 * PROJECT:     LiberNT Linux Subsystem
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Process credentials and capability sets
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "lxcore.h"

#define NDEBUG
#include <debug.h>

#define LX_SUID_DUMPABLE    2

static volatile LONG LxOverflowIds[2] = { 65534, 65534 };

PLX_NS
LxCredUserNs(
    _In_ PLX_PROCESS Process)
{
    return (Process->Ns[LxNsUser] != NULL) ? Process->Ns[LxNsUser] : Process->Instance->RootNs[LxNsUser];
}

PLX_ID_MAP
LxUserNsMap(
    _In_ PLX_NS UserNs,
    _In_ BOOLEAN Group)
{
    return (PLX_ID_MAP)ReadPointerAcquire((PVOID volatile *)&UserNs->User.Maps[Group ? 1 : 0]);
}

ULONG
LxIdToKernel(
    _In_opt_ PLX_NS UserNs,
    _In_ BOOLEAN Group,
    _In_ ULONG Id)
{
    PLX_ID_MAP Map;
    ULONG Index;

    if ((UserNs == NULL) || (UserNs->Parent == NULL))
        return Id;
    Map = LxUserNsMap(UserNs, Group);
    for (Index = 0; (Map != NULL) && (Index < Map->Count); Index++)
    {
        PLX_ID_EXTENT Extent = &Map->Extents[Index];

        if ((Id >= Extent->First) && (Id - Extent->First < Extent->Count))
            return Extent->LowerFirst + (Id - Extent->First);
    }
    return MAXULONG;
}

ULONG
LxIdRangeToKernel(
    _In_ PLX_NS UserNs,
    _In_ BOOLEAN Group,
    _In_ ULONG Id,
    _In_ ULONG Count)
{
    PLX_ID_MAP Map;
    ULONG Index;

    if (UserNs->Parent == NULL)
        return ((Id != MAXULONG) && (Count - 1 <= MAXULONG - 1 - Id)) ? Id : MAXULONG;
    Map = LxUserNsMap(UserNs, Group);
    for (Index = 0; (Map != NULL) && (Index < Map->Count); Index++)
    {
        PLX_ID_EXTENT Extent = &Map->Extents[Index];

        if ((Id >= Extent->First) && (Id - Extent->First < Extent->Count) &&
            (Count - 1 <= Extent->Count - 1 - (Id - Extent->First)))
        {
            return Extent->LowerFirst + (Id - Extent->First);
        }
    }
    return MAXULONG;
}

ULONG
LxIdFromKernel(
    _In_opt_ PLX_NS UserNs,
    _In_ BOOLEAN Group,
    _In_ ULONG Id)
{
    PLX_ID_MAP Map;
    ULONG Index;

    if ((UserNs == NULL) || (UserNs->Parent == NULL))
        return Id;
    Map = LxUserNsMap(UserNs, Group);
    for (Index = 0; (Map != NULL) && (Index < Map->Count); Index++)
    {
        PLX_ID_EXTENT Extent = &Map->Extents[Index];

        if ((Id >= Extent->LowerFirst) && (Id - Extent->LowerFirst < Extent->Count))
            return Extent->First + (Id - Extent->LowerFirst);
    }
    return MAXULONG;
}

ULONG
LxIdFromKernelMunged(
    _In_opt_ PLX_NS UserNs,
    _In_ BOOLEAN Group,
    _In_ ULONG Id)
{
    ULONG Value = LxIdFromKernel(UserNs, Group, Id);

    if ((Value == MAXULONG) && (UserNs != NULL) && (UserNs->Parent != NULL))
        Value = (ULONG)LxOverflowIds[Group ? 1 : 0];
    return Value;
}

ULONG
LxCredViewId(
    _In_ PLX_PROCESS Viewer,
    _In_ BOOLEAN Group,
    _In_ ULONG Id)
{
    return LxIdFromKernelMunged(LxCredUserNs(Viewer), Group, Id);
}

ULONG
LxCredViewIdFor(
    _In_ PLX_PROCESS Viewer,
    _In_ BOOLEAN Group,
    _In_ ULONG Id)
{
    PLX_NS UserNs = LxUserNsOf(Viewer);
    ULONG Value;

    Value = LxIdFromKernelMunged(UserNs, Group, Id);
    LxNsDereference(UserNs);
    return Value;
}

ULONG
LxCredOverflowId(
    _In_ BOOLEAN Group)
{
    return (ULONG)LxOverflowIds[Group ? 1 : 0];
}

LONG
LxCredSetOverflowId(
    _In_ PLX_PROCESS Process,
    _In_ BOOLEAN Group,
    _In_ LONG64 Value)
{
    if (!LxCapable(Process, LX_CAP_SYS_ADMIN))
        return -LX_EPERM;
    if ((Value < 0) || (Value > 65535))
        return -LX_EINVAL;
    InterlockedExchange(&LxOverflowIds[Group ? 1 : 0], (LONG)Value);
    return 0;
}

PLX_CRED
LxProcessCred(
    _In_ PLX_PROCESS Process)
{
    PLX_THREAD Thread = LxCurrentThread();

    if ((Thread != NULL) && (Thread->Process == Process) && (Thread->Cred != NULL))
        return Thread->Cred;
    return &Process->Cred;
}

BOOLEAN
LxNsCapable(
    _In_ PLX_PROCESS Process,
    _In_opt_ PLX_NS UserNs,
    _In_ ULONG Capability)
{
    PLX_CRED Cred = LxProcessCred(Process);
    PLX_NS Own = LxCredUserNs(Process), Ns = (UserNs != NULL) ? UserNs : Own;

    if (Capability > LX_CAP_LAST_CAP)
        return FALSE;
    for (;;)
    {
        if (Ns == Own)
            return ((Cred->CapEffective >> Capability) & 1) != 0;
        if ((Ns == NULL) || (Ns->Level <= Own->Level))
            return FALSE;
        if ((Ns->Parent == Own) && (Ns->User.OwnerUid == Cred->Uid[1]))
            return TRUE;
        Ns = Ns->Parent;
    }
}

BOOLEAN
LxCapable(
    _In_ PLX_PROCESS Process,
    _In_ ULONG Capability)
{
    return LxNsCapable(Process, Process->Instance->RootNs[LxNsUser], Capability);
}

BOOLEAN
LxMayMount(
    _In_ PLX_PROCESS Process)
{
    return LxNsCapable(Process, LxNsOwner(LxProcessNs(Process, LxNsMnt)), LX_CAP_SYS_ADMIN);
}

BOOLEAN
LxIdsMapped(
    _In_ PLX_PROCESS Process,
    _In_ ULONG Uid,
    _In_ ULONG Gid)
{
    PLX_NS Own = LxCredUserNs(Process);

    return (Own->Parent == NULL) ||
           ((LxIdFromKernel(Own, FALSE, Uid) != MAXULONG) && (LxIdFromKernel(Own, TRUE, Gid) != MAXULONG));
}

static
BOOLEAN
LxCapableWith(
    _In_ PLX_PROCESS Process,
    _In_ ULONG64 Effective,
    _In_ ULONG Uid,
    _In_ ULONG Gid,
    _In_ ULONG Capability)
{
    return ((Effective >> Capability) & 1) && LxIdsMapped(Process, Uid, Gid);
}

LONG
LxPermissionAs(
    _In_ PLX_PROCESS Process,
    _In_ ULONG Fsuid,
    _In_ ULONG Fsgid,
    _In_ ULONG64 Effective,
    _In_ ULONG Mode,
    _In_ ULONG Uid,
    _In_ ULONG Gid,
    _In_ ULONG Mask)
{
    PLX_CRED Cred = LxProcessCred(Process);
    ULONG Bits, Index;
    BOOLEAN Member = (Fsgid == Gid);

    Mask &= LX_MAY_READ | LX_MAY_WRITE | LX_MAY_EXEC;
    for (Index = 0; !Member && (Index < Cred->GroupCount); Index++)
        Member = (Cred->Groups[Index] == Gid);
    if (Fsuid == Uid)
        Bits = (Mode >> 6) & 7;
    else if (Member)
        Bits = (Mode >> 3) & 7;
    else
        Bits = Mode & 7;
    if ((Mask & ~Bits) == 0)
        return 0;
    if ((Mode & LX_S_IFMT) == LX_S_IFDIR)
    {
        if (!(Mask & LX_MAY_WRITE) && LxCapableWith(Process, Effective, Uid, Gid, LX_CAP_DAC_READ_SEARCH))
            return 0;
        return LxCapableWith(Process, Effective, Uid, Gid, LX_CAP_DAC_OVERRIDE) ? 0 : -LX_EACCES;
    }
    if ((Mask == LX_MAY_READ) && LxCapableWith(Process, Effective, Uid, Gid, LX_CAP_DAC_READ_SEARCH))
        return 0;
    if ((!(Mask & LX_MAY_EXEC) || (Mode & 0111)) && LxCapableWith(Process, Effective, Uid, Gid, LX_CAP_DAC_OVERRIDE))
        return 0;
    return -LX_EACCES;
}

LONG
LxPermission(
    _In_ PLX_PROCESS Process,
    _In_ ULONG Mode,
    _In_ ULONG Uid,
    _In_ ULONG Gid,
    _In_ ULONG Mask)
{
    PLX_CRED Cred = LxProcessCred(Process);

    return LxPermissionAs(Process, Cred->Fsuid, Cred->Fsgid, Cred->CapEffective, Mode, Uid, Gid, Mask);
}

BOOLEAN
LxOwnerOrCapable(
    _In_ PLX_PROCESS Process,
    _In_ ULONG Uid,
    _In_ ULONG Gid)
{
    return (LxProcessCred(Process)->Fsuid == Uid) || LxCapableWrtIds(Process, Uid, Gid, LX_CAP_FOWNER);
}

BOOLEAN
LxCapableWrtIds(
    _In_ PLX_PROCESS Process,
    _In_ ULONG Uid,
    _In_ ULONG Gid,
    _In_ ULONG Capability)
{
    PLX_NS Own = LxCredUserNs(Process);

    if (!LxNsCapable(Process, NULL, Capability))
        return FALSE;
    return (Own->Parent == NULL) ||
           ((LxIdFromKernel(Own, FALSE, Uid) != MAXULONG) && (LxIdFromKernel(Own, TRUE, Gid) != MAXULONG));
}

BOOLEAN
LxInGroup(
    _In_ PLX_PROCESS Process,
    _In_ ULONG Gid)
{
    PLX_CRED Cred = LxProcessCred(Process);
    ULONG Index;

    if (Cred->Fsgid == Gid)
        return TRUE;
    for (Index = 0; Index < Cred->GroupCount; Index++)
    {
        if (Cred->Groups[Index] == Gid)
            return TRUE;
    }
    return FALSE;
}

ULONG
LxCredMakeId(
    _In_ PLX_PROCESS Process,
    _In_ BOOLEAN Group,
    _In_ ULONG Id)
{
    return LxIdToKernel(LxCredUserNs(Process), Group, Id);
}

VOID
LxCredSave(
    _In_ PLX_PROCESS Process,
    _Out_ PLX_CRED_SAVE Save)
{
    PLX_CRED Cred = LxProcessCred(Process);

    RtlCopyMemory(Save->Uid, Cred->Uid, sizeof(Save->Uid));
    RtlCopyMemory(Save->Gid, Cred->Gid, sizeof(Save->Gid));
    Save->Fsuid = Cred->Fsuid;
    Save->Fsgid = Cred->Fsgid;
    Save->CapPermitted = Cred->CapPermitted;
}

VOID
LxCredFixSetuid(
    _Inout_ PLX_PROCESS Process,
    _In_ PLX_CRED_SAVE Old,
    _In_ BOOLEAN FileSystem)
{
    PLX_CRED Cred = LxProcessCred(Process);
    ULONG Root = LxCredMakeId(Process, FALSE, 0);
    BOOLEAN WasRoot, IsRoot;

    if (Cred->SecureBits & LX_SECBIT_NO_SETUID_FIXUP)
        return;
    if (FileSystem)
    {
        if ((Old->Fsuid == Root) && (Cred->Fsuid != Root))
            Cred->CapEffective &= ~LX_CAP_FS_SET;
        else if ((Old->Fsuid != Root) && (Cred->Fsuid == Root))
            Cred->CapEffective |= Cred->CapPermitted & LX_CAP_FS_SET;
        return;
    }
    WasRoot = (Old->Uid[0] == Root) || (Old->Uid[1] == Root) || (Old->Uid[2] == Root);
    IsRoot = (Cred->Uid[0] == Root) || (Cred->Uid[1] == Root) || (Cred->Uid[2] == Root);
    if (WasRoot && !IsRoot)
    {
        if (!(Cred->SecureBits & LX_SECBIT_KEEP_CAPS))
            Cred->CapPermitted = Cred->CapEffective = 0;
        Cred->CapAmbient = 0;
    }
    if ((Old->Uid[1] == Root) && (Cred->Uid[1] != Root))
        Cred->CapEffective = 0;
    else if ((Old->Uid[1] != Root) && (Cred->Uid[1] == Root))
        Cred->CapEffective = Cred->CapPermitted;
}

VOID
LxCredCommit(
    _Inout_ PLX_PROCESS Process,
    _Inout_ PLX_THREAD Thread,
    _In_ PLX_CRED_SAVE Old)
{
    PLX_CRED Cred = LxProcessCred(Process);

    if ((Cred->Uid[1] != Old->Uid[1]) || (Cred->Gid[1] != Old->Gid[1]) || (Cred->Fsuid != Old->Fsuid) ||
        (Cred->Fsgid != Old->Fsgid) || (Cred->CapPermitted & ~Old->CapPermitted))
    {
        Process->Dumpable = LX_SUID_DUMPABLE;
        Thread->PdeathSignal = 0;
    }
}

BOOLEAN
LxCredRootOwnsNs(
    _In_ PLX_PROCESS Process,
    _In_ ULONG RootId)
{
    PLX_NS Ns;

    if (RootId == MAXULONG)
        return FALSE;
    for (Ns = LxCredUserNs(Process); Ns != NULL; Ns = Ns->Parent)
    {
        if (LxIdFromKernel(Ns, FALSE, RootId) == 0)
            return TRUE;
    }
    return FALSE;
}

ULONG
LxCredExec(
    _Inout_ PLX_PROCESS Process,
    _Inout_ PLX_THREAD Thread,
    _In_ PLX_CRED_SAVE Old,
    _In_ PLX_FILE_CAPS FileCaps)
{
    PLX_CRED Cred = LxProcessCred(Process);
    ULONG Root = LxCredMakeId(Process, FALSE, 0);
    ULONG64 Permitted = 0;
    BOOLEAN Effective = FALSE, IdChanged, Gained;
    ULONG Result = 0;

    if (FileCaps->Present)
    {
        Permitted = (FileCaps->Permitted & Cred->CapBounding) | (FileCaps->Inheritable & Cred->CapInheritable);
        Effective = FileCaps->Effective;
    }
    if (!(Cred->SecureBits & LX_SECBIT_NOROOT) &&
        !(FileCaps->Present && (Cred->Uid[1] == Root) && (Cred->Uid[0] != Root)))
    {
        if ((Cred->Uid[1] == Root) || (Cred->Uid[0] == Root))
            Permitted = Cred->CapBounding | Cred->CapInheritable;
        if (Cred->Uid[1] == Root)
            Effective = TRUE;
    }
    Gained = (Permitted & ~Old->CapPermitted) != 0;
    if (Gained)
        Result |= LX_CRED_EXEC_GAINED;
    IdChanged = (Cred->Uid[1] != Old->Uid[1]) || !LxInGroup(Process, Cred->Gid[1]);
    if ((IdChanged || Gained) && Thread->NoNewPrivs)
        Permitted &= Old->CapPermitted;

    Cred->Uid[2] = Cred->Fsuid = Cred->Uid[1];
    Cred->Gid[2] = Cred->Fsgid = Cred->Gid[1];
    if (FileCaps->Present || IdChanged)
        Cred->CapAmbient = 0;
    Permitted |= Cred->CapAmbient;
    Cred->CapPermitted = Permitted;
    Cred->CapEffective = Effective ? Permitted : Cred->CapAmbient;
    Cred->SecureBits &= ~LX_SECBIT_KEEP_CAPS;
    if (IdChanged || (Cred->Uid[1] != Old->Uid[0]) || (Cred->Gid[1] != Old->Gid[0]) ||
        ((Cred->Uid[0] != Root) && (Effective || (Permitted & ~Cred->CapAmbient))))
    {
        Result |= LX_CRED_EXEC_SECURE;
    }
    Gained = (Permitted & ~Old->CapPermitted) != 0;
    if ((Result & LX_CRED_EXEC_SECURE) || Gained || (Cred->Uid[1] != Old->Uid[1]) ||
        (Cred->Gid[1] != Old->Gid[1]) || (Cred->Fsuid != Old->Fsuid) || (Cred->Fsgid != Old->Fsgid))
    {
        Thread->PdeathSignal = 0;
    }
    Process->Dumpable = ((Cred->Uid[1] == Cred->Uid[0]) && (Cred->Gid[1] == Cred->Gid[0]) && !Gained) ?
                        1 : LX_SUID_DUMPABLE;
    return Result;
}

LONG
LxCredSetCapabilities(
    _Inout_ PLX_PROCESS Process,
    _In_ ULONG64 Effective,
    _In_ ULONG64 Permitted,
    _In_ ULONG64 Inheritable)
{
    PLX_CRED Cred = LxProcessCred(Process);
    BOOLEAN Capped = !LxNsCapable(Process, NULL, LX_CAP_SETPCAP);
    LONG Error = 0;

    ExAcquireFastMutex(&Process->Lock);
    if (Capped && (Inheritable & ~(Cred->CapInheritable | Cred->CapPermitted)))
        Error = -LX_EPERM;
    else if (Inheritable & ~(Cred->CapInheritable | Cred->CapBounding))
        Error = -LX_EPERM;
    else if (Permitted & ~Cred->CapPermitted)
        Error = -LX_EPERM;
    else if (Effective & ~Permitted)
        Error = -LX_EPERM;
    if (Error == 0)
    {
        Cred->CapEffective = Effective;
        Cred->CapPermitted = Permitted;
        Cred->CapInheritable = Inheritable;
        Cred->CapAmbient &= Permitted & Inheritable;
    }
    ExReleaseFastMutex(&Process->Lock);
    return Error;
}

LONG
LxCredSetSecureBits(
    _Inout_ PLX_PROCESS Process,
    _In_ ULONG64 Bits)
{
    PLX_CRED Cred = LxProcessCred(Process);
    BOOLEAN Capable = LxNsCapable(Process, NULL, LX_CAP_SETPCAP);
    ULONG64 Old, Changed;
    LONG Error = 0;

    ExAcquireFastMutex(&Process->Lock);
    Old = Cred->SecureBits;
    Changed = Old ^ Bits;
    if ((((Old & LX_SECURE_ALL_LOCKS) >> 1) & Changed) || (Old & LX_SECURE_ALL_LOCKS & ~Bits) ||
        (Bits & ~(ULONG64)(LX_SECURE_ALL_LOCKS | LX_SECURE_ALL_BITS)))
    {
        Error = -LX_EPERM;
    }
    else if (!Capable &&
             ((Changed == 0) || (Changed & ~(ULONG64)(LX_SECURE_ALL_UNPRIVILEGED | (LX_SECURE_ALL_UNPRIVILEGED << 1)))))
    {
        Error = -LX_EPERM;
    }
    else
    {
        Cred->SecureBits = (ULONG)Bits;
    }
    ExReleaseFastMutex(&Process->Lock);
    return Error;
}
