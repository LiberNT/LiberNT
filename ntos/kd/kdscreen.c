/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Putting the debug output on the screen
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include <drivers/bootvid/display.h>
#include "kd.h"
#include "kdterminal.h"
#define NDEBUG
#include <debug.h>

/* PRIVATE DATA ***************************************************************/

/*
 * Whether the screen provider is allowed to write at all. Between the moment
 * a graphics driver takes the display and the moment the machine stops, the
 * boot video driver must not touch it.
 */
static BOOLEAN KdpScreenInitialised = FALSE;

/* PRIVATE FUNCTIONS **********************************************************/

/*
 * Takes the display and makes it fit to read text on. Returns TRUE when it had
 * to be taken from somebody, which is what KdpScreenRelease() needs to know.
 */
BOOLEAN
KdpScreenAcquire(VOID)
{
    if (!InbvIsBootDriverInstalled())
        return FALSE;

    /* Already ours: nothing was taken, so nothing has to be given back */
    if (InbvCheckDisplayOwnership())
        return FALSE;

    InbvAcquireDisplayOwnership();
    InbvResetDisplay();
    InbvSolidColorFill(0, 0, SCREEN_WIDTH - 1, SCREEN_HEIGHT - 1, BV_COLOR_BLACK);
    InbvSetTextColor(BV_COLOR_WHITE);
    InbvInstallDisplayStringFilter(NULL);
    InbvEnableDisplayString(TRUE);
    InbvSetScrollRegion(0, 0, SCREEN_WIDTH - 1, SCREEN_HEIGHT - 1);

    return TRUE;
}

/*
 * The machine has stopped and something wants the screen to show why. The
 * display is taken whether or not the screen was one of the places the output
 * was going, because a stopped machine has nowhere else to say it.
 */
BOOLEAN
KdpScreenEnterStop(
    _In_ BOOLEAN Requested)
{
    if (!Requested && !KdpDebugMode.Screen)
        return FALSE;

    return KdpScreenAcquire();
}

/*
 * Gives the display back to whoever had it.
 */
VOID
KdpScreenRelease(
    _In_ BOOLEAN DisplayAcquired)
{
    if (!DisplayAcquired)
        return;

    InbvNotifyDisplayOwnershipLost(NULL);
}

/*
 * One string on the screen. The boot video driver draws it directly, so this
 * works with the machine stopped and with interrupts off.
 */
static
VOID
NTAPI
KdpScreenPrint(
    _In_ PCCH String,
    _In_ ULONG Length)
{
    CHAR Line[81];
    ULONG Offset = 0;

    if (!KdpScreenInitialised)
        return;

    if (!InbvIsBootDriverInstalled() || !InbvCheckDisplayOwnership())
        return;

    /*
     * What the caller handed over is not necessarily terminated, and the boot
     * video driver only draws terminated strings, so it goes out in pieces
     * that fit one line.
     */
    while (Offset < Length)
    {
        ULONG Chunk = Length - Offset;
        ULONG Index;

        if (Chunk > sizeof(Line) - 1)
            Chunk = sizeof(Line) - 1;

        for (Index = 0; Index < Chunk; Index++)
            Line[Index] = String[Offset + Index];

        Line[Chunk] = ANSI_NULL;
        InbvDisplayString(Line);

        Offset += Chunk;
    }
}

/* PUBLIC FUNCTIONS ***********************************************************/

NTSTATUS
NTAPI
KdpScreenInit(
    _In_ PKD_DISPATCH_TABLE DispatchTable,
    _In_ ULONG BootPhase)
{
    if (!KdpDebugMode.Screen)
        return STATUS_PORT_DISCONNECTED;

    if (BootPhase == 0)
    {
        /*
         * The boot video driver is not there yet. Saying so keeps this
         * provider out of the list until it can actually draw.
         */
        DispatchTable->KdpPrintRoutine = KdpScreenPrint;
        return STATUS_SUCCESS;
    }

    if (BootPhase == 1)
    {
        if (!InbvIsBootDriverInstalled())
            return STATUS_PORT_DISCONNECTED;

        KdpScreenAcquire();
        KdpScreenInitialised = TRUE;

        InsertTailList(&KdProviders, &DispatchTable->KdProvidersList);
    }

    return STATUS_SUCCESS;
}

/* EOF */
