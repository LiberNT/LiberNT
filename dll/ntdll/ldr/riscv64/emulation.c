/*
 * PROJECT:     LiberNT NT Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     RISC-V64 hooks for processes that run under an emulation host
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntdll.h>
#include <reactos/emulation.h>

PCWSTR
LdrpArchGetEmulationHost(
    _In_ USHORT Machine)
{
    return Machine == IMAGE_FILE_MACHINE_AMD64 ? EMULATION_HOST_IMAGE_AMD64 : NULL;
}

VOID
LdrpArchEnterEmulationHost(
    _Inout_ PCONTEXT Context,
    _In_ PTEB HostTeb,
    _In_ PVOID Stack,
    _In_ PVOID Entry,
    _In_ PVOID Argument)
{
    Context->Pc = (ULONG_PTR)Entry;
    Context->Ra = 0;
    Context->Sp = (ULONG_PTR)Stack;
    Context->Tp = (ULONG_PTR)HostTeb;
    Context->A0 = (ULONG_PTR)Argument;
}
