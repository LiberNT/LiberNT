/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ARMv7 PL011 console the loader mapped for the kernel
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>

/* PrimeCell UART (PL011) TRM DDI 0183: data, flag and interrupt registers. */
#define PL011_DR   0x000
#define PL011_FR   0x018
#define PL011_IMSC 0x038
#define PL011_FR_RXFE 0x10
#define PL011_FR_TXFF 0x20
#define PL011_FR_BUSY 0x08

static volatile PUCHAR KiArmConsoleBase;
static ULONG KiArmConsoleKind;

VOID
NTAPI
KiArmConsoleInitialize(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PARM_LOADER_BLOCK ArmBlock = &LoaderBlock->u.Arm;

    if ((ArmBlock->EarlyUartInterface != ARM_EARLY_UART_PL011) || (ArmBlock->EarlyUartAddress == 0))
        return;

    KiArmConsoleKind = ArmBlock->EarlyUartInterface;
    KiArmConsoleBase = (PUCHAR)(ULONG_PTR)ArmBlock->EarlyUartAddress;
}

BOOLEAN
NTAPI
KiArmConsoleReady(VOID)
{
    return KiArmConsoleBase != NULL;
}

ULONG
NTAPI
KiArmConsoleInterface(VOID)
{
    return KiArmConsoleKind;
}

VOID
NTAPI
KiArmConsolePutByte(
    _In_ UCHAR Byte)
{
    if (!KiArmConsoleBase)
        return;

    while (READ_REGISTER_ULONG((PULONG)(KiArmConsoleBase + PL011_FR)) & PL011_FR_TXFF)
        YieldProcessor();
    WRITE_REGISTER_ULONG((PULONG)(KiArmConsoleBase + PL011_DR), Byte);
}

VOID
NTAPI
KiArmConsoleWrite(
    _In_reads_bytes_(Length) PCCH Buffer,
    _In_ SIZE_T Length)
{
    SIZE_T Index;

    for (Index = 0; Index < Length; Index++)
    {
        if (Buffer[Index] == '\n')
            KiArmConsolePutByte('\r');
        KiArmConsolePutByte((UCHAR)Buffer[Index]);
    }
}

BOOLEAN
NTAPI
KiArmConsoleGetByte(
    _Out_ PUCHAR Byte)
{
    if (!KiArmConsoleBase ||
        (READ_REGISTER_ULONG((PULONG)(KiArmConsoleBase + PL011_FR)) & PL011_FR_RXFE))
    {
        return FALSE;
    }

    *Byte = (UCHAR)READ_REGISTER_ULONG((PULONG)(KiArmConsoleBase + PL011_DR));
    return TRUE;
}
