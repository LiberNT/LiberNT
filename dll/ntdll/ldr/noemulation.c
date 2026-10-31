/*
 * PROJECT:     LiberNT NT Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Loader hooks for architectures without an emulation host
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntdll.h>

PCWSTR
LdrpArchGetEmulationHost(
    _In_ USHORT Machine)
{
    UNREFERENCED_PARAMETER(Machine);
    return NULL;
}

NTSTATUS
LdrpArchLoadEmulation(VOID)
{
    return STATUS_SUCCESS;
}

NTSTATUS
LdrpArchStartEmulation(VOID)
{
    return STATUS_SUCCESS;
}

VOID
LdrpArchDetachThread(VOID)
{
}

VOID
LdrpArchSnapExport(
    _In_ PVOID ExportBase,
    _In_opt_ PVOID ImportBase,
    _Inout_ PULONG_PTR Function)
{
    UNREFERENCED_PARAMETER(ExportBase);
    UNREFERENCED_PARAMETER(ImportBase);
    UNREFERENCED_PARAMETER(Function);
}

VOID
LdrpArchEnterEmulationHost(
    _Inout_ PCONTEXT Context,
    _In_ PTEB HostTeb,
    _In_ PVOID Stack,
    _In_ PVOID Entry,
    _In_ PVOID Argument)
{
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(HostTeb);
    UNREFERENCED_PARAMETER(Stack);
    UNREFERENCED_PARAMETER(Entry);
    UNREFERENCED_PARAMETER(Argument);
}
