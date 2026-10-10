/*
 * PROJECT:     LiberNT
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Contract between the loader and a native emulation host
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef _REACTOS_EMULATION_H_
#define _REACTOS_EMULATION_H_

#define EMULATION_HOST_IMAGE_AMD64 L"amd64host.exe"
#define EMULATION_SYSTEM_DIRECTORY_AMD64 L"SystemAMD64"

typedef struct _EMULATION_THREAD
{
    CONTEXT StartContext;
    PTEB GuestTeb;
    PPEB GuestPeb;
    PVOID StackBase;
    PVOID StackLimit;
    PVOID AllocationBase;
    PVOID HostData;
} EMULATION_THREAD, *PEMULATION_THREAD;

#define EmulationCurrentThread() ((PEMULATION_THREAD)NtCurrentTeb() - 1)

#endif
