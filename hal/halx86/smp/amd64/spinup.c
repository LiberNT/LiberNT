/*
 * PROJECT:     ReactOS HAL
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     AMD64 Application Processor (AP) spinup setup
 * COPYRIGHT:   Copyright 2023 Justin Miller <justin.miller@reactos.org>
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

/* INCLUDES ******************************************************************/

#include <hal.h>
#include <smp.h>
#include <bootfb.h>

#define NDEBUG
#include <debug.h>

/* GLOBALS *******************************************************************/

extern BOOLEAN HalpOnlyBootProcessor;
extern PPROCESSOR_IDENTITY HalpProcessorIdentity;
extern PHYSICAL_ADDRESS HalpLowStubPhysicalAddress;
extern PVOID HalpLowStub;
extern HALP_APIC_INFO_TABLE HalpApicInfoTable;

/* Trampoline symbol (hal/halx86/smp/amd64/apentry.S) */
extern UCHAR HalpAPEntry64[];

ULONG HalpStartedProcessorCount = 1;

/* FUNCTIONS *****************************************************************/

/* Fixed bottom row: never scrolls, always shows the last step reached */
static BOOTFB_CONSOLE HalpApConsole;
static BOOLEAN HalpApConsoleReady;
static PLOADER_PARAMETER_BLOCK HalpApLoaderBlock;

VOID
HalpApTrace(
    _In_ PCSTR Text)
{
    PLOADER_PARAMETER_EXTENSION Extension;
    PLOADER_PARAMETER_EARLY_CONSOLE Console;
    PCSTR Cursor;
    CHAR Limited[96];
    ULONG Row, Length;

    if (!HalpApConsoleReady)
    {
        if (HalpApLoaderBlock == NULL || HalpApLoaderBlock->Extension == NULL)
            return;

        Extension = HalpApLoaderBlock->Extension;
        if (Extension->Size < RTL_SIZEOF_THROUGH_FIELD(LOADER_PARAMETER_EXTENSION, EarlyConsole))
            return;

        Console = &Extension->EarlyConsole;
        if (Console->Magic != LOADER_EARLY_CONSOLE_MAGIC ||
            Console->Size != sizeof(*Console) ||
            Console->Base.QuadPart == 0 ||
            Console->BitsPerPixel != 32 ||
            Console->Width == 0 ||
            Console->Height == 0 ||
            Console->Pitch < Console->Width)
            return;

        HalpApConsole.Base = (PUCHAR)(ULONG_PTR)Console->Base.QuadPart;
        HalpApConsole.Width = Console->Width;
        HalpApConsole.Height = Console->Height;
        HalpApConsole.Pitch = Console->Pitch;
        HalpApConsole.LineBytes = Console->Pitch * 4;
        HalpApConsole.Scale = 1;
        HalpApConsole.CellWidth = BOOTFB_FONT_WIDTH;
        HalpApConsole.CellHeight = BOOTFB_FONT_HEIGHT;
        HalpApConsole.Columns = Console->Width / HalpApConsole.CellWidth;
        HalpApConsole.Rows = Console->Height / HalpApConsole.CellHeight;
        if (HalpApConsole.Columns == 0 || HalpApConsole.Rows == 0)
            return;

        HalpApConsole.Enabled = TRUE;
        HalpApConsoleReady = TRUE;
    }

    Row = HalpApConsole.Rows - 1;
    BootFbFill(&HalpApConsole, 0, Row * HalpApConsole.CellHeight,
               HalpApConsole.Width, HalpApConsole.CellHeight, 0);
    HalpApConsole.CursorX = 0;
    HalpApConsole.CursorY = Row;

    /* Truncate: a wrap scrolls, and a scroll reads the framebuffer back */
    Limited[0] = ANSI_NULL;
    for (Cursor = Text, Length = 0; *Cursor && Length + 1 < sizeof(Limited); Cursor++)
    {
        if ((UCHAR)*Cursor >= 0x20)
            Limited[Length++] = *Cursor;
    }
    Limited[Length] = ANSI_NULL;

    BootFbPrint(&HalpApConsole, Limited);
}

BOOLEAN
NTAPI
HalStartNextProcessor(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _In_ PKPROCESSOR_STATE ProcessorState)
{
    UNREFERENCED_PARAMETER(LoaderBlock);

    /* Bail out if we only use the boot CPU */
    if (HalpOnlyBootProcessor)
        return FALSE;

    /* Bail out if we have started all available CPUs */
    if (HalpStartedProcessorCount == HalpApicInfoTable.ProcessorCount)
        return FALSE;

    /* No low (<1MB) trampoline stub (e.g. UEFI left no low memory): stay UP */
    if (HalpLowStub == NULL)
    {
        DPRINT1("HalStartNextProcessor: no low-memory AP stub, continuing as UP\n");
        return FALSE;
    }

    HalpApLoaderBlock = LoaderBlock;
    HalpApTrace("AP: preparing the low stub");

    /* Copy the trampoline into low memory and point it at this processor */
    if (!HalpPrepareLowStub(ProcessorState, HalpAPEntry64))
        return FALSE;

    HalpApTrace("AP: low stub ready");

    /* Armed either way: the caller must wait for it and must not free it */
    if (!ApicStartApplicationProcessor(HalpStartedProcessorCount, HalpLowStubPhysicalAddress))
        HalpApTrace("AP: IPI refused, processor armed anyway");
    else
        HalpApTrace("AP: INIT and STARTUP sent");

    HalpStartedProcessorCount++;

    return TRUE;
}

BOOLEAN
HalpRestartProcessor(
    _In_ ULONG NTProcessorNumber,
    _In_ PKPROCESSOR_STATE ProcessorState,
    _In_ PVOID Entry)
{
    if (!HalpPrepareLowStub(ProcessorState, Entry))
        return FALSE;

    ApicStartApplicationProcessor(NTProcessorNumber, HalpLowStubPhysicalAddress);
    return TRUE;
}
