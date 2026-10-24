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

BOOLEAN
LxCapable(
    _In_ PLX_PROCESS Process,
    _In_ ULONG Capability)
{
    return (Capability <= LX_CAP_LAST_CAP) && (((Process->CapEffective >> Capability) & 1) != 0);
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
    UNREFERENCED_PARAMETER(Process);
    UNREFERENCED_PARAMETER(Group);
    return Id;
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
    BOOLEAN Capped = !LxCapable(Process, LX_CAP_SETPCAP);
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
    BOOLEAN Capable = LxCapable(Process, LX_CAP_SETPCAP);
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
