/*
 * PROJECT:     LiberNT Kernel Debugger
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Terminal channel a debugger transport module offers to the built-in debugger
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define KD_TERMINAL_INTERFACE_VERSION 2

#define KD_TERMINAL_INFORMATION_SYSTEM  1
#define KD_TERMINAL_INFORMATION_PCI     2
#define KD_TERMINAL_INFORMATION_ACPI    3
#define KD_TERMINAL_INFORMATION_MODULES 4

#define KD_TERMINAL_STATE_CRASHED       0x0001

struct _LOADER_PARAMETER_BLOCK;

typedef VOID
(NTAPI *PKD_TERMINAL_CONFIGURE)(
    _Inout_opt_z_ PCHAR LoadOptions);

typedef NTSTATUS
(NTAPI *PKD_TERMINAL_INITIALIZE)(
    _In_ struct _LOADER_PARAMETER_BLOCK *LoaderBlock);

typedef VOID
(NTAPI *PKD_TERMINAL_WRITE)(
    _In_reads_bytes_(Length) const CHAR *Buffer,
    _In_ ULONG Length);

typedef BOOLEAN
(NTAPI *PKD_TERMINAL_READ)(
    _Out_ PUCHAR Byte);

typedef BOOLEAN
(NTAPI *PKD_TERMINAL_POLL)(VOID);

typedef BOOLEAN
(NTAPI *PKD_TERMINAL_QUERY_ATTACHED)(
    _Out_opt_ PULONG Columns,
    _Out_opt_ PULONG Rows);

typedef ULONG
(NTAPI *PKD_TERMINAL_QUERY_STATUS)(
    _Out_writes_z_(Size) PCHAR Buffer,
    _In_ ULONG Size);

typedef ULONG
(NTAPI *PKD_TERMINAL_QUERY_KEY)(
    _Out_writes_z_(Size) PCHAR Buffer,
    _In_ ULONG Size);

typedef ULONG
(NTAPI *PKD_TERMINAL_HOST_INFORMATION)(
    _In_ ULONG Class,
    _In_ ULONG Offset,
    _Out_writes_bytes_(Size) PUCHAR Buffer,
    _In_ ULONG Size,
    _Out_ PULONG Total);

typedef ULONG
(NTAPI *PKD_TERMINAL_HOST_STATE)(VOID);

typedef struct _KD_TERMINAL_HOST
{
    PKD_TERMINAL_HOST_INFORMATION QueryInformation;
    PKD_TERMINAL_HOST_STATE QueryState;
} KD_TERMINAL_HOST, *PKD_TERMINAL_HOST;

typedef VOID
(NTAPI *PKD_TERMINAL_SET_HOST)(
    _In_ const KD_TERMINAL_HOST *Host);

typedef struct _KD_TERMINAL_INTERFACE
{
    ULONG Version;
    PKD_TERMINAL_CONFIGURE Configure;
    PKD_TERMINAL_INITIALIZE Initialize;
    PKD_TERMINAL_WRITE Write;
    PKD_TERMINAL_READ Read;
    PKD_TERMINAL_POLL Poll;
    PKD_TERMINAL_QUERY_ATTACHED QueryAttached;
    PKD_TERMINAL_QUERY_STATUS QueryStatus;
    PKD_TERMINAL_QUERY_KEY QueryKey;
    PKD_TERMINAL_SET_HOST SetHost;
} KD_TERMINAL_INTERFACE, *PKD_TERMINAL_INTERFACE;

NTSTATUS
NTAPI
KdQueryTerminalInterface(
    _Out_ PKD_TERMINAL_INTERFACE Interface);
