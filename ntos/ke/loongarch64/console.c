/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     LoongArch64 early console (loader-described MMIO 16550)
 *
 * After ExitBootServices nothing may call firmware, so the only console that
 * can exist before the HAL is the one the loader mapped into a direct-map
 * window and described in its payload (handoff decision D-24). The device is
 * reached through DMW0, whose MAT is strongly-ordered uncached, never through
 * the cached window.
 */

#include <ntoskrnl.h>
#include "la64ke.h"

/* 16550 register indices; the byte offset is Index << RegisterShift. */
#define KI_UART_RBR            0
#define KI_UART_THR            0
#define KI_UART_LSR            5
#define KI_UART_LSR_DR         0x01
#define KI_UART_LSR_THRE       0x20
#define KI_UART_REGISTER_COUNT 8
#define KI_UART_TX_SPIN_LIMIT  1000000

static ULONG KiLa64ConsoleType = LOONGARCH64_EARLY_CONSOLE_NONE;
static BOOLEAN KiLa64ConsoleIsReady;
static volatile UCHAR *KiLa64ConsoleBase;
static ULONG KiLa64ConsoleShift;
static ULONG KiLa64ConsoleWidth;

static
ULONG
KiLa64ConsoleRead(
    _In_ ULONG Index)
{
    volatile UCHAR *Register = KiLa64ConsoleBase + ((ULONG_PTR)Index << KiLa64ConsoleShift);
    ULONG Value;

    /* The window is uncached, so no cache maintenance is needed; dbar 0 keeps
     * the compiler and the core from moving the access across a later one. */
    KiLa64MemoryBarrier();
    switch (KiLa64ConsoleWidth)
    {
        case 4:
            Value = *(volatile ULONG *)Register;
            break;
        case 2:
            Value = *(volatile USHORT *)Register;
            break;
        default:
            Value = *Register;
            break;
    }
    KiLa64MemoryBarrier();
    return Value;
}

static
VOID
KiLa64ConsoleWriteRegister(
    _In_ ULONG Index,
    _In_ ULONG Value)
{
    volatile UCHAR *Register = KiLa64ConsoleBase + ((ULONG_PTR)Index << KiLa64ConsoleShift);

    KiLa64MemoryBarrier();
    switch (KiLa64ConsoleWidth)
    {
        case 4:
            *(volatile ULONG *)Register = Value;
            break;
        case 2:
            *(volatile USHORT *)Register = (USHORT)Value;
            break;
        default:
            *Register = (UCHAR)Value;
            break;
    }
    KiLa64MemoryBarrier();
}

static
BOOLEAN
KiLa64ConsoleInitializeNs16550(
    _In_ PLOONGARCH64_LOADER_BLOCK LaBlock)
{
    ULONG Shift = LaBlock->EarlyConsoleRegisterShift;
    ULONG Width = LaBlock->EarlyConsoleRegisterWidth;
    ULONGLONG Address = LaBlock->EarlyConsoleAddress;
    ULONGLONG Length = LaBlock->EarlyConsoleLength;
    ULONGLONG Window;

    if (Width == 0)
        Width = 1;
    if ((Width != 1) && (Width != 2) && (Width != 4))
        return FALSE;
    if (Shift > 4)
        return FALSE;
    if ((Address == 0) || (Address & (Width - 1)))
        return FALSE;

    /* The loader mapped this window into the uncached direct map (DMW0). */
    Window = (ULONGLONG)KI_UART_REGISTER_COUNT << Shift;
    if ((Length < Window) ||
        (Address >= LaBlock->PhysicalLimit) ||
        (Length > LaBlock->PhysicalLimit - Address))
    {
        return FALSE;
    }

    KiLa64ConsoleBase = (volatile UCHAR *)(ULONG_PTR)(LaBlock->UncachedBase + Address);
    KiLa64ConsoleShift = Shift;
    KiLa64ConsoleWidth = Width;
    return TRUE;
}

VOID
NTAPI
KiLa64ConsoleInitialize(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PLOONGARCH64_LOADER_BLOCK LaBlock = &LoaderBlock->u.LoongArch64;
    BOOLEAN Described;

    KiLa64ConsoleIsReady = FALSE;
    KiLa64ConsoleType = LOONGARCH64_EARLY_CONSOLE_NONE;
    Described = (LaBlock->Flags & LOONGARCH64_LOADER_FLAG_EARLY_CONSOLE) != 0;

    if (Described &&
        (LaBlock->EarlyConsoleInterface == LOONGARCH64_EARLY_CONSOLE_NS16550) &&
        KiLa64ConsoleInitializeNs16550(LaBlock))
    {
        KiLa64ConsoleType = LOONGARCH64_EARLY_CONSOLE_NS16550;
        KiLa64ConsoleIsReady = TRUE;
    }
}

BOOLEAN
NTAPI
KiLa64ConsoleReady(VOID)
{
    return KiLa64ConsoleIsReady;
}

ULONG
NTAPI
KiLa64ConsoleInterface(VOID)
{
    return KiLa64ConsoleType;
}

VOID
NTAPI
KiLa64ConsolePutByte(
    _In_ UCHAR Byte)
{
    ULONG Spin;

    if (!KiLa64ConsoleIsReady)
        return;

    /* Bounded wait for transmitter-holding-register empty; a wedged line must
     * not hang the kernel, so the byte is written after the limit. */
    for (Spin = 0; Spin < KI_UART_TX_SPIN_LIMIT; Spin++)
    {
        if (KiLa64ConsoleRead(KI_UART_LSR) & KI_UART_LSR_THRE)
            break;
    }
    KiLa64ConsoleWriteRegister(KI_UART_THR, Byte);
}

VOID
NTAPI
KiLa64ConsoleWrite(
    _In_reads_bytes_(Length) const CHAR *Buffer,
    _In_ SIZE_T Length)
{
    SIZE_T Index;

    for (Index = 0; Index < Length; Index++)
        KiLa64ConsolePutByte((UCHAR)Buffer[Index]);
}

BOOLEAN
NTAPI
KiLa64ConsoleGetByte(
    _Out_ PUCHAR Byte)
{
    /* Leave *Byte untouched when nothing is available: KD callers use a
     * preset sentinel to detect "no data". */
    if (!KiLa64ConsoleIsReady)
        return FALSE;

    if (!(KiLa64ConsoleRead(KI_UART_LSR) & KI_UART_LSR_DR))
        return FALSE;
    *Byte = (UCHAR)KiLa64ConsoleRead(KI_UART_RBR);
    return TRUE;
}
