/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     KDBG RISC-V instruction decoder interface
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

typedef struct _KDB_RISCV_DECODE_STATE
{
    ULONG64 Value[32];
    ULONG Known;
} KDB_RISCV_DECODE_STATE, *PKDB_RISCV_DECODE_STATE;

typedef struct _KDB_RISCV_DECODED
{
    CHAR Text[96];
    BOOLEAN HasTarget;
    BOOLEAN HasAddress;
    ULONG64 Target;
    ULONG64 Address;
} KDB_RISCV_DECODED, *PKDB_RISCV_DECODED;

VOID
KdbpRiscvDecodeReset(
    _Out_ PKDB_RISCV_DECODE_STATE State);

VOID
KdbpRiscvDecode(
    _In_ ULONG Instruction,
    _In_ ULONG Length,
    _In_ ULONG64 Pc,
    _Inout_ PKDB_RISCV_DECODE_STATE State,
    _In_ BOOLEAN KeepStaleValues,
    _Out_ PKDB_RISCV_DECODED Decoded);

BOOLEAN
KdbpRiscvEmulateRelative(
    _Inout_ PCONTEXT Context,
    _In_ ULONG Inst,
    _In_ LONG Length);
