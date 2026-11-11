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

static BOOLEAN KdpScreenJoinedForStop = FALSE;

/* Between KdpScreenEnterStop() and KdpScreenRelease(): what is printed is drawn at once */
static BOOLEAN KdpScreenStopped = FALSE;

#define KDP_SCREEN_TEXT_SIZE 8192

/*
 * What the running machine printed. Drawing it where it is printed would keep
 * the processor at HIGH_LEVEL with interrupts off for a whole repaint, so a DPC
 * draws it instead, all that has piled up in one go.
 */
static KSPIN_LOCK KdpScreenQueueLock;
static CHAR KdpScreenQueue[KDP_SCREEN_TEXT_SIZE];
static ULONG KdpScreenQueued;
static KDPC KdpScreenDpc;
static BOOLEAN KdpScreenDpcReady = FALSE;
static LONG KdpScreenDrawing;
static CHAR KdpScreenDpcText[KDP_SCREEN_TEXT_SIZE + 1];

/* What a stopped machine draws, collected while the debugger runs a command */
static CHAR KdpScreenStopText[KDP_SCREEN_TEXT_SIZE + 1];
static ULONG KdpScreenStopLength;
static BOOLEAN KdpScreenHeld = FALSE;
static LARGE_INTEGER KdpScreenHeldSince;

#define KDP_SCREEN_HOLD_MS 50

/* PRIVATE FUNCTIONS **********************************************************/

static
VOID
NTAPI
KdpScreenPrint(
    _In_ PCCH String,
    _In_ ULONG Length);

static
VOID
KdpScreenFlushStop(VOID);

static
VOID
KdpScreenSetConsole(
    _In_ BOOLEAN Clear)
{
    VID_DISPLAY_INFO DisplayInfo;
    ULONG Width = SCREEN_WIDTH;
    ULONG Height = SCREEN_HEIGHT;
    ULONG Right = SCREEN_WIDTH - 1;

    InbvUseCompactFont();

    if (InbvQueryDisplayInfo(&DisplayInfo) &&
        (DisplayInfo.Width >= SCREEN_WIDTH) &&
        (DisplayInfo.Height >= SCREEN_HEIGHT) &&
        (DisplayInfo.CharacterWidth != 0))
    {
        Width = DisplayInfo.Width;
        Height = DisplayInfo.Height;
        Right = Width - (Width % DisplayInfo.CharacterWidth) - 1;
    }

    if (Clear)
    {
        InbvResetDisplay();
        InbvSolidColorFill(0, 0, Width - 1, Height - 1, BV_COLOR_BLACK);
    }

    InbvSetTextColor(BV_COLOR_WHITE);
    InbvInstallDisplayStringFilter(NULL);
    InbvEnableDisplayString(TRUE);
    InbvSetScrollRegion(0, 0, Right, Height - 1);
}

static
VOID
KdpScreenJoin(VOID)
{
    PKD_DISPATCH_TABLE Table = &DispatchTable[KdScreen];

    if (KdpScreenInitialised)
        return;

    Table->KdpPrintRoutine = KdpScreenPrint;
    InsertTailList(&KdProviders, &Table->KdProvidersList);
    KdpScreenInitialised = TRUE;
}

static
VOID
KdpScreenPrintCallback(
    _In_ PSTRING Output,
    _In_ ULONG ComponentId,
    _In_ ULONG Level)
{
    UNREFERENCED_PARAMETER(ComponentId);
    UNREFERENCED_PARAMETER(Level);

    KdpScreenPrint(Output->Buffer, Output->Length);
}

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
    KdpScreenSetConsole(TRUE);

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
    BOOLEAN Acquired;

    if (!Requested && !KdpDebugMode.Screen)
        return FALSE;

    KdpScreenStopped = TRUE;
    Acquired = KdpScreenAcquire();

    if (!KdpScreenInitialised && InbvIsBootDriverInstalled())
    {
        KdpScreenJoin();
        KdpScreenJoinedForStop = TRUE;
    }

    return Acquired;
}

/*
 * Gives the display back to whoever had it.
 */
VOID
KdpScreenRelease(
    _In_ BOOLEAN DisplayAcquired)
{
    KdpScreenHeld = FALSE;
    KdpScreenFlushStop();
    KdpScreenStopped = FALSE;

    if (KdpScreenJoinedForStop)
    {
        RemoveEntryList(&DispatchTable[KdScreen].KdProvidersList);
        KdpScreenInitialised = FALSE;
        KdpScreenJoinedForStop = FALSE;
    }

    if (!DisplayAcquired)
        return;

    InbvNotifyDisplayOwnershipLost(NULL);
}

VOID
KdpScreenEnterBugCheck(
    _In_ BOOLEAN FollowPrints)
{
    if (!InbvIsBootDriverInstalled())
        return;

    KdpScreenStopped = TRUE;

    if (!KdpScreenInitialised ||
        (InbvGetDisplayState() != INBV_DISPLAY_STATE_OWNED))
    {
        InbvAcquireDisplayOwnership();
        KdpScreenSetConsole(TRUE);
    }
    else
    {
        KdpScreenSetConsole(FALSE);
    }

    KdpNetDisplayStatus();

    KdpScreenJoin();
    KdpScreenJoinedForStop = FALSE;

    if (FollowPrints)
        DbgSetDebugPrintCallback(KdpScreenPrintCallback, TRUE);
}

static
BOOLEAN
KdpScreenMachineStopped(VOID)
{
    return KdpScreenStopped || KdEnteredDebugger || KeBugCheckActive;
}

static
BOOLEAN
KdpScreenCanDraw(VOID)
{
    return KdpScreenInitialised && InbvIsBootDriverInstalled() && InbvCheckDisplayOwnership();
}

static
BOOLEAN
KdpScreenLockQueue(
    _Out_ PKIRQL OldIrql)
{
    KeRaiseIrql(HIGH_LEVEL, OldIrql);

    while (!KeTryToAcquireSpinLockAtDpcLevel(&KdpScreenQueueLock))
    {
        /* A stopped machine may have frozen the processor that holds it */
        if (KdpScreenMachineStopped())
        {
            KeLowerIrql(*OldIrql);
            return FALSE;
        }

        YieldProcessor();
    }

    return TRUE;
}

static
VOID
KdpScreenUnlockQueue(
    _In_ KIRQL OldIrql)
{
    KeReleaseSpinLockFromDpcLevel(&KdpScreenQueueLock);
    KeLowerIrql(OldIrql);
}

static
VOID
KdpScreenQueueText(
    _In_reads_bytes_(Length) PCCH String,
    _In_ ULONG Length)
{
    KIRQL OldIrql;

    if (!KdpScreenLockQueue(&OldIrql))
        return;

    /* A screen shows the end of the log, so the oldest text is what gives way */
    if (Length >= sizeof(KdpScreenQueue))
    {
        String += Length - sizeof(KdpScreenQueue);
        Length = sizeof(KdpScreenQueue);
        KdpScreenQueued = 0;
    }
    else if (KdpScreenQueued + Length > sizeof(KdpScreenQueue))
    {
        ULONG Drop = KdpScreenQueued + Length - sizeof(KdpScreenQueue);

        RtlMoveMemory(KdpScreenQueue, KdpScreenQueue + Drop, KdpScreenQueued - Drop);
        KdpScreenQueued -= Drop;
    }

    RtlCopyMemory(KdpScreenQueue + KdpScreenQueued, String, Length);
    KdpScreenQueued += Length;

    KdpScreenUnlockQueue(OldIrql);
}

static
ULONG
KdpScreenTakeQueued(
    _Out_writes_bytes_(KDP_SCREEN_TEXT_SIZE) PCHAR Text)
{
    KIRQL OldIrql;
    ULONG Length;

    if (!KdpScreenLockQueue(&OldIrql))
        return 0;

    Length = KdpScreenQueued;
    RtlCopyMemory(Text, KdpScreenQueue, Length);
    KdpScreenQueued = 0;

    KdpScreenUnlockQueue(OldIrql);
    return Length;
}

static
BOOLEAN
KdpScreenHasQueued(VOID)
{
    KIRQL OldIrql;
    BOOLEAN Queued;

    if (!KdpScreenLockQueue(&OldIrql))
        return FALSE;

    Queued = (KdpScreenQueued != 0);

    KdpScreenUnlockQueue(OldIrql);
    return Queued;
}

static
VOID
NTAPI
KdpScreenDpcRoutine(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    ULONG Length;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(DeferredContext);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    /* One processor draws; one that finds it busy leaves its text to it */
    while (InterlockedExchange(&KdpScreenDrawing, 1) == 0)
    {
        Length = KdpScreenTakeQueued(KdpScreenDpcText);

        /* The boot video driver only draws terminated strings */
        if ((Length != 0) && KdpScreenCanDraw())
        {
            KdpScreenDpcText[Length] = ANSI_NULL;
            InbvDisplayString(KdpScreenDpcText);
        }

        InterlockedExchange(&KdpScreenDrawing, 0);

        if (!KdpScreenHasQueued())
            break;
    }
}

static
VOID
KdpScreenFlushStop(VOID)
{
    if (KdpScreenStopLength == 0)
        return;

    KdpScreenStopText[KdpScreenStopLength] = ANSI_NULL;
    KdpScreenStopLength = 0;

    if (KdpScreenCanDraw())
        InbvDisplayString(KdpScreenStopText);
}

static
VOID
KdpScreenStartStopText(VOID)
{
    /* What the running machine left for the DPC comes first */
    KdpScreenStopLength = KdpScreenTakeQueued(KdpScreenStopText);
    KdpScreenHeldSince = KeQueryPerformanceCounter(NULL);
}

static
VOID
KdpScreenAddStop(
    _In_reads_bytes_(Length) PCCH String,
    _In_ ULONG Length)
{
    LARGE_INTEGER Frequency;

    if (KdpScreenStopLength == 0)
        KdpScreenStartStopText();

    while (Length != 0)
    {
        ULONG Chunk = KDP_SCREEN_TEXT_SIZE - KdpScreenStopLength;

        if (Chunk == 0)
        {
            KdpScreenFlushStop();
            KdpScreenStartStopText();
            continue;
        }

        if (Chunk > Length)
            Chunk = Length;

        RtlCopyMemory(KdpScreenStopText + KdpScreenStopLength, String, Chunk);
        KdpScreenStopLength += Chunk;
        String += Chunk;
        Length -= Chunk;
    }

    if (!KdpScreenHeld)
    {
        KdpScreenFlushStop();
        return;
    }

    /* A long command still shows how far it has got */
    if ((KeQueryPerformanceCounter(&Frequency).QuadPart - KdpScreenHeldSince.QuadPart) * 1000 >=
        Frequency.QuadPart * KDP_SCREEN_HOLD_MS)
    {
        KdpScreenFlushStop();
    }
}

/*
 * One string on the screen. A stopped machine draws it here, since nothing
 * else will run to do it; a running one leaves it to the DPC.
 */
static
VOID
NTAPI
KdpScreenPrint(
    _In_ PCCH String,
    _In_ ULONG Length)
{
    if (!KdpScreenCanDraw() || (Length == 0))
        return;

    if (KdpScreenMachineStopped() || !KdpScreenDpcReady)
    {
        KdpScreenAddStop(String, Length);
        return;
    }

    KdpScreenQueueText(String, Length);
    KeInsertQueueDpc(&KdpScreenDpc, NULL, NULL);
}

/*
 * The debugger holds what it prints while a command runs, so a listing is
 * drawn in one go instead of one repaint per line. Letting go draws it.
 */
VOID
KdpScreenHoldOutput(
    _In_ BOOLEAN Hold)
{
    KdpScreenHeld = Hold;

    if (!Hold)
        KdpScreenFlushStop();
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

        KeInitializeSpinLock(&KdpScreenQueueLock);
        KeInitializeDpc(&KdpScreenDpc, KdpScreenDpcRoutine, NULL);
        KdpScreenDpcReady = TRUE;

        if (!KdpScreenAcquire())
            KdpScreenSetConsole(TRUE);
        KdpScreenInitialised = TRUE;

        InsertTailList(&KdProviders, &DispatchTable->KdProvidersList);
    }

    return STATUS_SUCCESS;
}

/* EOF */
