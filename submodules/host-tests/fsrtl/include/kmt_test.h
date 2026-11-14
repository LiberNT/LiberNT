/*
 * PROJECT:     LiberNT host-native tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Host stand-in for kmt_test.h so kernel-mode FsRtl tests run on the host
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <stdio.h>
#include "ntoskrnl.h"

#define _WIN32_WINNT_VISTA 0x0600

extern unsigned HostTestsRun;
extern unsigned HostTestsFailed;
extern BOOLEAN KmtIsCheckedBuild;

#define GetNTVersion() 0x0A00
#define skip(Condition, ...) (!(Condition))
#define ok(Condition, ...) \
    do { \
        HostTestsRun++; \
        if (!(Condition)) { HostTestsFailed++; printf("%s:%d: test failed\n", __FILE__, __LINE__); } \
    } while (0)
#define START_TEST(Name) void Test_##Name(void)

BOOLEAN NTAPI FsRtlIsNameInExpression(PUNICODE_STRING Expression, PUNICODE_STRING Name, BOOLEAN IgnoreCase, PWCHAR UpcaseTable);
BOOLEAN NTAPI FsRtlIsDbcsInExpression(PANSI_STRING Expression, PANSI_STRING Name);

static inline VOID RtlInitUnicodeString(PUNICODE_STRING String, PCWSTR Source)
{
    USHORT Length = 0;

    while (Source != NULL && Source[Length] != 0)
        Length++;
    String->Buffer = (PWSTR)Source;
    String->Length = (USHORT)(Length * sizeof(WCHAR));
    String->MaximumLength = (USHORT)(String->Length + sizeof(WCHAR));
}

static inline NTSTATUS RtlUnicodeStringToAnsiString(PANSI_STRING Ansi, PUNICODE_STRING Unicode, BOOLEAN Allocate)
{
    USHORT Index;

    (void)Allocate;
    Ansi->Length = (USHORT)(Unicode->Length / sizeof(WCHAR));
    Ansi->MaximumLength = (USHORT)(Ansi->Length + 1);
    Ansi->Buffer = malloc(Ansi->MaximumLength);
    for (Index = 0; Index < Ansi->Length; Index++)
        Ansi->Buffer[Index] = (CHAR)Unicode->Buffer[Index];
    Ansi->Buffer[Ansi->Length] = 0;
    return 0;
}

static inline VOID RtlFreeAnsiString(PANSI_STRING Ansi)
{
    free(Ansi->Buffer);
    Ansi->Buffer = NULL;
}
