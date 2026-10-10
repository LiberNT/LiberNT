/*
 * PROJECT:     LiberNT AMD64 emulation host
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Native image the loader starts beside an AMD64 process image
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define WIN32_NO_STATUS
#define NTOS_MODE_USER
#include <windef.h>
#include <winbase.h>
#undef WIN32_NO_STATUS
#include <ntstatus.h>
#include <ndk/ntndk.h>
#include <reactos/emulation.h>

VOID
NTAPI
EmuThreadStart(
    _In_ PEMULATION_THREAD Host);

VOID
NTAPI
EmuHostEntry(
    _In_ PEMULATION_THREAD Host)
{
    EmuThreadStart(Host);
    NtTerminateProcess(NtCurrentProcess(), STATUS_UNSUCCESSFUL);
}
