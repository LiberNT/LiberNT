/*
 * PROJECT:     LiberNT LoongArch64 HAL
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     16550-compatible console UART
 *
 * The board wires four byte-wide 16550 register files at 0x1FE0_01E0 plus
 * 0x100 per instance (QEMU include/hw/loongarch/virt.h VIRT_UART_BASE/SIZE,
 * serial_mm_init with regshift 0). 7A1000 chapter 9.1 defines the same
 * register file: byte accesses only, UART number in bits 9:8, register in
 * bits 7:0.
 *
 * The HAL uses UART 0 only to report a platform that is not the machine this
 * build targets, before any driver or debugger exists. It deliberately does
 * not program the divisor latch: the UART's input clock is not described by
 * the manual's register chapter and the firmware has already selected
 * 115200 8N1, so writing a divisor computed from a guessed clock would break
 * the console the loader left behind. Baud programming belongs to the driver
 * that owns the device (serial.sys) once it knows the clock.
 *
 * TODO: the UART input clock for the 7A1000 bridge is not in the local
 * manual; add the divisor programming when a source for it exists.
 */

#include <ntifs.h>
#include <ioaccess.h>
#include <ndk/halfuncs.h>
#include "halp.h"

#define HALP_LA64_UART_RBR 0x00
#define HALP_LA64_UART_THR 0x00
#define HALP_LA64_UART_IER 0x01
#define HALP_LA64_UART_FCR 0x02
#define HALP_LA64_UART_LCR 0x03
#define HALP_LA64_UART_MCR 0x04
#define HALP_LA64_UART_LSR 0x05
#define HALP_LA64_UART_SCR 0x07

#define HALP_LA64_UART_LSR_THRE 0x20
#define HALP_LA64_UART_LSR_TEMT 0x40
#define HALP_LA64_UART_FCR_ENABLE 0x01
#define HALP_LA64_UART_FCR_CLEAR 0x06
#define HALP_LA64_UART_LCR_8N1 0x03
#define HALP_LA64_UART_MCR_DTR_RTS 0x03
#define HALP_LA64_UART_POLL_LIMIT 100000UL

static PUCHAR HalpLa64Uart;

static
UCHAR
HalpLa64UartRead(
    _In_ ULONG Register)
{
    return READ_REGISTER_UCHAR((volatile UCHAR *)(HalpLa64Uart + Register));
}

static
VOID
HalpLa64UartWriteRegister(
    _In_ ULONG Register,
    _In_ UCHAR Value)
{
    WRITE_REGISTER_UCHAR((volatile UCHAR *)(HalpLa64Uart + Register), Value);
}

BOOLEAN
HalpLa64InitializeUart(VOID)
{
    if (!HalpLa64Platform.UartBase || HalpLa64Platform.UartLength < 8 ||
        !HalpLa64Platform.UartCount)
        return FALSE;
    return TRUE;
}

BOOLEAN
HalpLa64MapUart(VOID)
{
    PHYSICAL_ADDRESS Address;

    if (!HalpLa64Platform.UartBase)
        return FALSE;
    Address.QuadPart = (LONGLONG)HalpLa64Platform.UartBase;
    HalpLa64Uart = MmMapIoSpace(Address, PAGE_SIZE, MmNonCached);
    if (!HalpLa64Uart)
        return FALSE;

    /* 8N1, FIFOs on; the divisor latch is left as the firmware set it. */
    HalpLa64UartWriteRegister(HALP_LA64_UART_LCR, HALP_LA64_UART_LCR_8N1);
    HalpLa64UartWriteRegister(HALP_LA64_UART_FCR,
                              HALP_LA64_UART_FCR_ENABLE | HALP_LA64_UART_FCR_CLEAR);
    HalpLa64UartWriteRegister(HALP_LA64_UART_MCR, HALP_LA64_UART_MCR_DTR_RTS);
    return TRUE;
}

VOID
HalpLa64UartWrite(
    _In_reads_(Length) const CHAR *String,
    _In_ SIZE_T Length)
{
    SIZE_T Index;

    if (!HalpLa64Uart)
        return;
    for (Index = 0; Index < Length; ++Index)
    {
        ULONG Spin;

        for (Spin = 0; Spin < HALP_LA64_UART_POLL_LIMIT; ++Spin)
        {
            if (HalpLa64UartRead(HALP_LA64_UART_LSR) & HALP_LA64_UART_LSR_THRE)
                break;
            __asm__ __volatile__("nop");
        }
        HalpLa64UartWriteRegister(HALP_LA64_UART_THR, (UCHAR)String[Index]);
    }
}

/* Report the UART's register window and line to the PnP platform bus. The
 * interrupt is the PCH-PIC input, which is also the EIOINTC source. */
BOOLEAN
HalpLa64GetUartResource(
    _In_ ULONG Index,
    _Out_ PCM_PARTIAL_RESOURCE_DESCRIPTOR Memory,
    _Out_ PCM_PARTIAL_RESOURCE_DESCRIPTOR Interrupt)
{
    if (!HalpLa64Platform.UartBase || Index >= HalpLa64Platform.UartCount ||
        !Memory || !Interrupt)
        return FALSE;

    RtlZeroMemory(Memory, sizeof(*Memory));
    Memory->Type = CmResourceTypeMemory;
    Memory->ShareDisposition = CmResourceShareDeviceExclusive;
    Memory->Flags = CM_RESOURCE_MEMORY_READ_WRITE;
    Memory->u.Memory.Start.QuadPart =
        (LONGLONG)(HalpLa64Platform.UartBase + Index * 0x100);
    Memory->u.Memory.Length = 8;

    RtlZeroMemory(Interrupt, sizeof(*Interrupt));
    Interrupt->Type = CmResourceTypeInterrupt;
    Interrupt->ShareDisposition = CmResourceShareShared;
    Interrupt->Flags = CM_RESOURCE_INTERRUPT_LEVEL_SENSITIVE;
    Interrupt->u.Interrupt.Level = HalpLa64Platform.UartFirstIrq + Index;
    Interrupt->u.Interrupt.Vector = HalpLa64Platform.UartFirstIrq + Index;
    Interrupt->u.Interrupt.Affinity = 1;
    return TRUE;
}
