/*
 * PROJECT:     LiberNT FreeLoader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Resume from a hibernation image on BIOS systems
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <hiberfil.h>

#define WINLDR_HIBER_OPERATIONS_PER_PAGE 255

typedef struct _WINLDR_HIBER_OPERATION
{
    ULONG64 Destination;
    ULONG64 Source;
} WINLDR_HIBER_OPERATION, *PWINLDR_HIBER_OPERATION;

typedef struct _WINLDR_HIBER_OPERATION_PAGE
{
    ULONG64 Next;
    ULONG64 Count;
    WINLDR_HIBER_OPERATION Operation[WINLDR_HIBER_OPERATIONS_PER_PAGE];
} WINLDR_HIBER_OPERATION_PAGE, *PWINLDR_HIBER_OPERATION_PAGE;

C_ASSERT(sizeof(WINLDR_HIBER_OPERATION_PAGE) == PAGE_SIZE);

BOOLEAN
WinLdrHiberAllocatePage(
    _Out_ PULONG64 Page);

BOOLEAN
WinLdrArchPrepareResume(
    _In_ PLNT_HIBER_HEADER Header,
    _In_ ULONG64 HighestPage);

DECLSPEC_NORETURN
VOID
WinLdrArchResume(
    _In_ ULONG64 Operations);

VOID
WinLdrResumeFromHibernation(
    _In_ PCSTR BootPath);
