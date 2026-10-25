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

BOOLEAN
LxNsCapable(
    _In_ PLX_PROCESS Process,
    _In_opt_ PLX_NS UserNs,
    _In_ ULONG Capability)
{
    PLX_NS Own = LxCredUserNs(Process), Ns = (UserNs != NULL) ? UserNs : Own;

    if (Capability > LX_CAP_LAST_CAP)
        return FALSE;
    for (;;)
    {
        if (Ns == Own)
            return ((Process->CapEffective >> Capability) & 1) != 0;
        if ((Ns == NULL) || (Ns->Level <= Own->Level))
            return FALSE;
        if ((Ns->Parent == Own) && (Ns->User.OwnerUid == Process->Uid[1]))
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
    ULONG Bits, Index;
    BOOLEAN Member = (Fsgid == Gid);

    Mask &= LX_MAY_READ | LX_MAY_WRITE | LX_MAY_EXEC;
    for (Index = 0; !Member && (Index < Process->GroupCount); Index++)
        Member = (Process->Groups[Index] == Gid);
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
    return LxPermissionAs(Process, Process->Fsuid, Process->Fsgid, Process->CapEffective, Mode, Uid, Gid, Mask);
}

BOOLEAN
LxOwnerOrCapable(
    _In_ PLX_PROCESS Process,
    _In_ ULONG Uid,
    _In_ ULONG Gid)
{
    return (Process->Fsuid == Uid) || LxCapableWrtIds(Process, Uid, Gid, LX_CAP_FOWNER);
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
    ULONG Index;

    if (Process->Fsgid == Gid)
        return TRUE;
    for (Index = 0; Index < Process->GroupCount; Index++)
    {
        if (Process->Groups[Index] == Gid)
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
    RtlCopyMemory(Save->Uid, Process->Uid, sizeof(Save->Uid));
    RtlCopyMemory(Save->Gid, Process->Gid, sizeof(Save->Gid));
    Save->Fsuid = Process->Fsuid;
    Save->Fsgid = Process->Fsgid;
    Save->CapPermitted = Process->CapPermitted;
}

VOID
LxCredFixSetuid(
    _Inout_ PLX_PROCESS Process,
    _In_ PLX_CRED_SAVE Old,
    _In_ BOOLEAN FileSystem)
{
    ULONG Root = LxCredMakeId(Process, FALSE, 0);
    BOOLEAN WasRoot, IsRoot;

    if (Process->SecureBits & LX_SECBIT_NO_SETUID_FIXUP)
        return;
    if (FileSystem)
    {
        if ((Old->Fsuid == Root) && (Process->Fsuid != Root))
            Process->CapEffective &= ~LX_CAP_FS_SET;
        else if ((Old->Fsuid != Root) && (Process->Fsuid == Root))
            Process->CapEffective |= Process->CapPermitted & LX_CAP_FS_SET;
        return;
    }
    WasRoot = (Old->Uid[0] == Root) || (Old->Uid[1] == Root) || (Old->Uid[2] == Root);
    IsRoot = (Process->Uid[0] == Root) || (Process->Uid[1] == Root) || (Process->Uid[2] == Root);
    if (WasRoot && !IsRoot)
    {
        if (!(Process->SecureBits & LX_SECBIT_KEEP_CAPS))
            Process->CapPermitted = Process->CapEffective = 0;
        Process->CapAmbient = 0;
    }
    if ((Old->Uid[1] == Root) && (Process->Uid[1] != Root))
        Process->CapEffective = 0;
    else if ((Old->Uid[1] != Root) && (Process->Uid[1] == Root))
        Process->CapEffective = Process->CapPermitted;
}

VOID
LxCredCommit(
    _Inout_ PLX_PROCESS Process,
    _In_ PLX_CRED_SAVE Old)
{
    if ((Process->Uid[1] != Old->Uid[1]) || (Process->Gid[1] != Old->Gid[1]) || (Process->Fsuid != Old->Fsuid) ||
        (Process->Fsgid != Old->Fsgid) || (Process->CapPermitted & ~Old->CapPermitted))
    {
        Process->Dumpable = LX_SUID_DUMPABLE;
        Process->PdeathSignal = 0;
    }
}

VOID
LxCredExec(
    _Inout_ PLX_PROCESS Process,
    _In_ PLX_CRED_SAVE Old)
{
    ULONG Root = LxCredMakeId(Process, FALSE, 0);
    ULONG64 Permitted = 0;
    BOOLEAN Effective = FALSE, IdChanged;

    if (!(Process->SecureBits & LX_SECBIT_NOROOT))
    {
        if ((Process->Uid[1] == Root) || (Process->Uid[0] == Root))
            Permitted = Process->CapBounding | Process->CapInheritable;
        Effective = (Process->Uid[1] == Root);
    }
    IdChanged = (Process->Uid[1] != Old->Uid[1]) || !LxInGroup(Process, Process->Gid[1]);
    if ((IdChanged || (Permitted & ~Old->CapPermitted)) && Process->NoNewPrivs)
        Permitted &= Old->CapPermitted;

    Process->Uid[2] = Process->Fsuid = Process->Uid[1];
    Process->Gid[2] = Process->Fsgid = Process->Gid[1];
    if (IdChanged)
        Process->CapAmbient = 0;
    Permitted |= Process->CapAmbient;
    Process->CapPermitted = Permitted;
    Process->CapEffective = Effective ? Permitted : Process->CapAmbient;
    Process->SecureBits &= ~LX_SECBIT_KEEP_CAPS;
    if (IdChanged || (Process->Uid[1] != Process->Uid[0]) || (Process->Gid[1] != Process->Gid[0]))
        Process->PdeathSignal = 0;
    Process->Dumpable = ((Process->Uid[1] == Process->Uid[0]) && (Process->Gid[1] == Process->Gid[0])) ? 1 : LX_SUID_DUMPABLE;
}

LONG
LxCredSetCapabilities(
    _Inout_ PLX_PROCESS Process,
    _In_ ULONG64 Effective,
    _In_ ULONG64 Permitted,
    _In_ ULONG64 Inheritable)
{
    BOOLEAN Capped = !LxNsCapable(Process, NULL, LX_CAP_SETPCAP);
    LONG Error = 0;

    ExAcquireFastMutex(&Process->Lock);
    if (Capped && (Inheritable & ~(Process->CapInheritable | Process->CapPermitted)))
        Error = -LX_EPERM;
    else if (Inheritable & ~(Process->CapInheritable | Process->CapBounding))
        Error = -LX_EPERM;
    else if (Permitted & ~Process->CapPermitted)
        Error = -LX_EPERM;
    else if (Effective & ~Permitted)
        Error = -LX_EPERM;
    if (Error == 0)
    {
        Process->CapEffective = Effective;
        Process->CapPermitted = Permitted;
        Process->CapInheritable = Inheritable;
        Process->CapAmbient &= Permitted & Inheritable;
    }
    ExReleaseFastMutex(&Process->Lock);
    return Error;
}

LONG
LxCredSetSecureBits(
    _Inout_ PLX_PROCESS Process,
    _In_ ULONG64 Bits)
{
    BOOLEAN Capable = LxNsCapable(Process, NULL, LX_CAP_SETPCAP);
    ULONG64 Old, Changed;
    LONG Error = 0;

    ExAcquireFastMutex(&Process->Lock);
    Old = Process->SecureBits;
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
        Process->SecureBits = (ULONG)Bits;
    }
    ExReleaseFastMutex(&Process->Lock);
    return Error;
}
