/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Kernel debug output on the firmware framebuffer
 * COPYRIGHT:   Copyright 2026 Zhou Ji Yang <yinglanxing@gmail.com>
 *
 * A machine with no firmware serial port and no network stack has no debug
 * channel at all: DbgPrint output is dropped and, until the boot video driver
 * runs, the screen keeps showing the loader's last line.  This provider draws
 * the debug stream straight into the GOP framebuffer handed over by the
 * loader, so a boot that dies before the boot video driver exists still shows
 * how far it got.
 *
 * The loader maps that framebuffer in the KSEG0 window, which lives in the
 * kernel half of the address space and therefore survives the clearing of the
 * user half done by the memory manager.
 */

/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>
#include <bootfb.h>

#include "kd.h"

/* GLOBALS ********************************************************************/

static BOOTFB_CONSOLE KdpFbConsole;
static BOOLEAN KdpFbReady;
static BOOLEAN KdpFbStreamOn = TRUE;
static ULONG KdpFbReason;   /* Why the console is off, 0 when it is on */

/*
 * The boot logo owns most of the screen: its animation thread blits a black
 * canvas every frame, which repainted the whole console and left a black
 * screen with nothing but the last line.  Keep the console in a window at the
 * bottom, below the logo canvas, and never touch the rest of the screen.
 */
#define KDP_FB_WINDOW_ROWS 9

/* PERSISTENT BOOT LOG ********************************************************/
/*
 * The framebuffer console only lives as long as the screen does.  To keep the
 * log across a power cycle, the same text is mirrored into a UEFI variable:
 * the runtime services are the only storage that still works this early (the
 * disk stack, and with it lastboot.log, only appears in IoInitSystem, long
 * after the point where such a boot dies).
 *
 * Read it from Linux with:
 *     od -An -tx1 /sys/firmware/efi/efivars/LntBootLog-<guid>  | ...
 * The payload starts after a four byte attribute header written by the kernel:
 *   struct { ULONG Magic; ULONG Sequence; ULONG Length; ULONG Reserved; }
 * followed by Length bytes of text (the tail of the log is kept when full).
 */
#define KDP_BB_NAME         L"LntBootLog"
#define KDP_BB_CAPACITY     8192
#define KDP_BB_MAGIC        0x4C4E5442 /* 'LNTB' */
#define KDP_BB_MAX_WRITES   200
#define KDP_BB_ATTRIBUTES   (0x00000001 | 0x00000002 | 0x00000004)

typedef struct _KDP_BB_HEADER
{
    ULONG Magic;
    ULONG Sequence;
    ULONG Length;
    ULONG Reserved;
} KDP_BB_HEADER, *PKDP_BB_HEADER;

typedef UINT64 (NTAPI *PKDP_SET_VARIABLE)(PWSTR Name,
                                          GUID* Guid,
                                          ULONG Attributes,
                                          ULONG_PTR Size,
                                          PVOID Data);

static CHAR KdpBbBuffer[KDP_BB_CAPACITY];
static CHAR KdpBbWrite[sizeof(KDP_BB_HEADER) + KDP_BB_CAPACITY];
static KDP_BB_HEADER KdpBbHeader;
static ULONG KdpBbLength;
static ULONG KdpBbWrites;
static PKDP_SET_VARIABLE KdpBbSetVariable;
static BOOLEAN KdpBbReady;
static BOOLEAN KdpBbWarned;
static ULONG KdpBbPending;

/*
 * Only the tail of the log is written: a small variable is far less likely to
 * be refused once the firmware's variable store has churned, and the tail is
 * what names the stage a boot died in.  The header Reserved field carries how
 * many bytes were dropped in front of it.
 */
#define KDP_BB_TAIL        1024

static const GUID KdpBbGuid = { 0x4c6e7442, 0x6f6f, 0x7431,
                                { 0x9a, 0x3c, 0x4d, 0x5e, 0x6f, 0x70, 0x71, 0x72 } };

/* Keep the newest half of the buffer when it fills up */
static
VOID
KdpBbTrim(VOID)
{
    ULONG Keep = KDP_BB_CAPACITY / 2;

    RtlMoveMemory(KdpBbBuffer,
                  KdpBbBuffer + (KdpBbLength - Keep),
                  Keep);
    KdpBbLength = Keep;
}

static
VOID
KdpBbAppend(
    _In_reads_(Length) PCCH Text,
    _In_ ULONG Length)
{
    while (Length--)
    {
        if (KdpBbLength == KDP_BB_CAPACITY - 1)
            KdpBbTrim();

        KdpBbBuffer[KdpBbLength++] = *Text++;
    }
}

static
VOID
KdpBbFlush(VOID)
{
    UINT64 Status;
    ULONG Offset;

    if (!KdpBbReady || !KdpBbSetVariable || KdpBbLength == 0)
        return;

    if (KdpBbWrites >= KDP_BB_MAX_WRITES)
        return;

    Offset = (KdpBbLength > KDP_BB_TAIL) ? KdpBbLength - KDP_BB_TAIL : 0;

    KdpBbHeader.Magic = KDP_BB_MAGIC;
    KdpBbHeader.Sequence++;
    KdpBbHeader.Length = KdpBbLength - Offset;
    KdpBbHeader.Reserved = Offset;

    RtlCopyMemory(KdpBbWrite, &KdpBbHeader, sizeof(KDP_BB_HEADER));
    RtlCopyMemory(KdpBbWrite + sizeof(KDP_BB_HEADER), KdpBbBuffer + Offset, KdpBbHeader.Length);

    Status = KdpBbSetVariable(KDP_BB_NAME,
                              (GUID*)&KdpBbGuid,
                              KDP_BB_ATTRIBUTES,
                              sizeof(KDP_BB_HEADER) + KdpBbHeader.Length,
                              KdpBbWrite);

    if (Status != 0)
    {
        /* The firmware refuses the variable: stop bothering it, but say so on
         * the screen, so a log that ends early is not taken for a hang. */
        KdpBbSetVariable = NULL;
        if (!KdpBbWarned)
        {
            KdpBbWarned = TRUE;
            BootFbPrint(&KdpFbConsole, "LiberNT: firmware rejected the boot log variable\r\n");
        }
        return;
    }

    KdpBbWrites++;
}

static
VOID
KdpBbEnable(
    _In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PEFI_FIRMWARE_INFORMATION Efi;

    if (KdpBbReady || !LoaderBlock || !LoaderBlock->Extension)
        return;

    if (!LoaderBlock->FirmwareInformation.FirmwareTypeEfi)
        return;

    Efi = &LoaderBlock->FirmwareInformation.u.EfiInformation;
    if (!Efi->VirtualEfiRuntimeServices || !NT_SUCCESS(Efi->SetVirtualAddressMapStatus))
        return;

    KdpBbSetVariable = (PKDP_SET_VARIABLE)(ULONG_PTR)Efi->VirtualEfiRuntimeServices->SetVariable;
    KdpBbReady = (KdpBbSetVariable != NULL);
}

/* FUNCTIONS ******************************************************************/

VOID
NTAPI
KdpFbEarlyInit(
    _In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PLOADER_PARAMETER_EXTENSION Extension;
    PLOADER_PARAMETER_EARLY_CONSOLE Console;
    ULONG Scale;

    if (KdpFbReady)
        return;

    if (!LoaderBlock || !LoaderBlock->Extension)
    {
        KdpFbReason = 1;
        return;
    }

    Extension = LoaderBlock->Extension;
    if (Extension->Size < RTL_SIZEOF_THROUGH_FIELD(LOADER_PARAMETER_EXTENSION, EarlyConsole))
    {
        KdpFbReason = 2;
        return;
    }

    Console = &Extension->EarlyConsole;
    if (Console->Magic != LOADER_EARLY_CONSOLE_MAGIC ||
        Console->Size != sizeof(*Console) ||
        Console->Base.QuadPart == 0 ||
        Console->BitsPerPixel != 32 ||
        Console->Width == 0 ||
        Console->Height == 0 ||
        Console->Pitch < Console->Width)
    {
        KdpFbReason = 3;
        return;
    }

    Scale = (Console->Height >= 1000) ? 2 : 1;

    KdpFbConsole.Base = (PUCHAR)(ULONG_PTR)Console->Base.QuadPart;
    KdpFbConsole.Width = Console->Width;
    KdpFbConsole.Height = Console->Height;
    KdpFbConsole.Pitch = Console->Pitch;
    KdpFbConsole.LineBytes = Console->Pitch * 4;
    KdpFbConsole.Scale = Scale;
    KdpFbConsole.CellWidth = Scale * BOOTFB_FONT_WIDTH;
    KdpFbConsole.CellHeight = Scale * BOOTFB_FONT_HEIGHT;
    KdpFbConsole.Columns = Console->Width / KdpFbConsole.CellWidth;
    KdpFbConsole.Rows = Console->Height / KdpFbConsole.CellHeight;
    if (KdpFbConsole.Columns == 0 || KdpFbConsole.Rows <= KDP_FB_WINDOW_ROWS + 1)
    {
        KdpFbReason = 4;
        return;
    }

    KdpFbConsole.CursorX = 0;
    KdpFbConsole.CursorY = KdpFbConsole.Rows - KDP_FB_WINDOW_ROWS - 1;

    /* Clear only our own window; the rest of the screen belongs to the loader
     * handoff text and to the boot logo. */
    BootFbFill(&KdpFbConsole,
               0,
               KdpFbConsole.CursorY * KdpFbConsole.CellHeight,
               KdpFbConsole.Width,
               KDP_FB_WINDOW_ROWS * KdpFbConsole.CellHeight,
               0);

    KdpFbConsole.Enabled = TRUE;
    KdpFbReady = TRUE;

}

/*
 * One console, several processors: DbgPrint can arrive from any CPU, and the
 * cursor and the cell fills are not atomic.  The lock is taken with a try, not
 * a spin: a diagnostic path that blocks is a diagnostic path that turns one
 * wedged processor into a silent machine, which is exactly what this console
 * exists to avoid.  Losing a line under contention is acceptable; freezing is
 * not.
 */
static volatile LONG KdpFbConsoleLock;

/* Last lines, kept so the console can be repainted after the boot logo and its
 * animation thread are done: the logo blits a black canvas over the whole
 * screen every frame and erases anything drawn underneath it. */
#define KDP_FB_LINE_CHARS 96
static CHAR KdpFbLines[KDP_FB_WINDOW_ROWS][KDP_FB_LINE_CHARS];
static ULONG KdpFbLineNext;
static ULONG KdpFbLineLength;
static BOOLEAN KdpFbLineWrap;

static
VOID
KdpFbRecordChar(
    _In_ UCHAR Character)
{
    if (Character == '\r')
        return;

    if (Character == '\n')
    {
        KdpFbLines[KdpFbLineNext][KdpFbLineLength] = ANSI_NULL;
        KdpFbLineNext = (KdpFbLineNext + 1) % KDP_FB_WINDOW_ROWS;
        KdpFbLineLength = 0;
        KdpFbLineWrap = TRUE;
        return;
    }

    if (KdpFbLineLength + 1 < KDP_FB_LINE_CHARS)
        KdpFbLines[KdpFbLineNext][KdpFbLineLength++] = (CHAR)Character;
}

static
BOOLEAN
KdpFbTryLockConsole(VOID)
{
    return InterlockedCompareExchange(&KdpFbConsoleLock, 1, 0) == 0;
}

static
VOID
KdpFbUnlockConsole(VOID)
{
    InterlockedExchange(&KdpFbConsoleLock, 0);
}

/*
 * Draw text without ever scrolling: a scroll reads the framebuffer back, and
 * on real hardware that read path is the one thing that can stall for minutes
 * (or forever, when the mapping is write-combined).  When the last row is
 * reached the cursor wraps to the first console row, so the newest lines are
 * always on screen and the console never touches video memory as a source.
 */
static
VOID
KdpFbDraw(
    _In_ PCSTR Text)
{
    ULONG Columns = KdpFbConsole.Columns;

    while (*Text)
    {
        UCHAR Character = (UCHAR)*Text++;

        if (Character == '\r')
        {
            KdpFbConsole.CursorX = 0;
            continue;
        }

        if (Character == '\n')
        {
            ULONG TailX = KdpFbConsole.CursorX * KdpFbConsole.CellWidth;

            /* Hand the line to the recorder so it can be repainted later */
            KdpFbRecordChar(Character);

            /* Clear only the remainder of the row: clearing the whole row here
             * erased the line that had just been drawn, which made every
             * screenshot look like the log stopped earlier than it did. */
            if (TailX < KdpFbConsole.Width)
            {
                BootFbFill(&KdpFbConsole,
                           TailX,
                           KdpFbConsole.CursorY * KdpFbConsole.CellHeight,
                           KdpFbConsole.Width - TailX,
                           KdpFbConsole.CellHeight,
                           0);
            }

            KdpFbConsole.CursorX = 0;
            if (++KdpFbConsole.CursorY > KdpFbConsole.Rows - 2)
                KdpFbConsole.CursorY = KdpFbConsole.Rows - KDP_FB_WINDOW_ROWS - 1;
            continue;
        }

        if (Character < 0x20)
            continue;

        if (KdpFbConsole.CursorX + 1 >= Columns)
        {
            KdpFbConsole.CursorX = 0;
            if (++KdpFbConsole.CursorY > KdpFbConsole.Rows - 2)
                KdpFbConsole.CursorY = KdpFbConsole.Rows - KDP_FB_WINDOW_ROWS - 1;
        }

        KdpFbRecordChar(Character);

        /* Clear the row before its first character: with wrap around drawing
         * a shorter line over a longer one from the previous pass left the old
         * tail visible, which made a screenshot read like two messages. */
        if (KdpFbConsole.CursorX == 0)
        {
            BootFbFill(&KdpFbConsole,
                       0,
                       KdpFbConsole.CursorY * KdpFbConsole.CellHeight,
                       KdpFbConsole.Width,
                       KdpFbConsole.CellHeight,
                       0);
        }

        BootFbPutChar(&KdpFbConsole, Character);
    }
}

VOID
NTAPI
KdpFbPrintString(
    _In_ PCSTR Text)
{
    if (!KdpFbReady)
        return;

    /* Explicit progress markers are drawn even after the boot video driver
     * took the display over: they exist exactly for the window in which
     * nothing else reports anything any more.  Only the debug stream itself
     * is turned off at that point, so the desktop is not painted over. */
    /*
     * Drawn only, never persisted: a firmware that stops answering
     * SetVariable() after a few dozen calls once boot services are gone
     * turns a diagnostic marker into the very hang it was added to find.
     * The screen is what a person in front of the machine reads anyway.
     */
    /*
     * Markers are screen only.  They used to be mirrored into the KD stream
     * as well, which puts them behind the serial provider: on a machine with
     * no working COM1 a marker that is already on the screen then blocks on a
     * transmitter that never reports ready, and the next marker never comes.
     */
    if (KdpFbTryLockConsole())
    {
        KdpFbDraw(Text);
        KdpFbUnlockConsole();
    }
}

/*
 * Repaint the window from the recorded lines.  Called once the boot logo has
 * finished: everything the console drew while the logo animation was running
 * was painted over.
 */
VOID
NTAPI
KdpFbStreamStop(VOID)
{
    KdpFbStreamOn = FALSE;
}

VOID
NTAPI
KdpFbRedraw(VOID)
{
    ULONG Index, Line, Top;

    if (!KdpFbReady || !KdpFbLineWrap)
        return;

    if (!KdpFbTryLockConsole())
        return;

    Top = KdpFbConsole.Rows - KDP_FB_WINDOW_ROWS - 1;

    BootFbFill(&KdpFbConsole,
               0,
               Top * KdpFbConsole.CellHeight,
               KdpFbConsole.Width,
               KDP_FB_WINDOW_ROWS * KdpFbConsole.CellHeight,
               0);

    KdpFbConsole.CursorX = 0;
    KdpFbConsole.CursorY = Top;

    for (Index = 0; Index < KDP_FB_WINDOW_ROWS; Index++)
    {
        Line = (KdpFbLineNext + Index) % KDP_FB_WINDOW_ROWS;
        if (KdpFbLines[Line][0] == ANSI_NULL)
            continue;

        BootFbPrint(&KdpFbConsole, KdpFbLines[Line]);
        BootFbPrint(&KdpFbConsole, "\r\n");
    }

    KdpFbUnlockConsole();
}

/* Per processor startup stages, one cache line each */
typedef struct _KDP_STAGE_SLOT
{
    volatile ULONG Cpu;
    volatile ULONG Stage;
    volatile ULONG64 Tsc;
    UCHAR Pad[64 - 2 * sizeof(ULONG) - sizeof(ULONG64)];
} KDP_STAGE_SLOT, *PKDP_STAGE_SLOT;

static KDP_STAGE_SLOT KdpStageSlots[MAXIMUM_PROCESSORS];

VOID
NTAPI
KdpStageSet(
    _In_ ULONG Cpu,
    _In_ ULONG Stage)
{
    if (Cpu >= MAXIMUM_PROCESSORS)
        return;

    KdpStageSlots[Cpu].Cpu = Cpu;
#if defined(_M_IX86) || defined(_M_AMD64)
    KdpStageSlots[Cpu].Tsc = __rdtsc();
#else
    KdpStageSlots[Cpu].Tsc = 0;
#endif
    KeMemoryBarrier();
    KdpStageSlots[Cpu].Stage = Stage;
}

ULONG
NTAPI
KdpStageQuery(
    _In_ ULONG Cpu)
{
    if (Cpu >= MAXIMUM_PROCESSORS)
        return KDP_STAGE_NONE;

    return KdpStageSlots[Cpu].Stage;
}

VOID
NTAPI
KdpStageDump(
    _In_ PCSTR Why)
{
    ULONG Cpu, Stage;
    CHAR Line[96];

    ULONG Persisted = 0;

    for (Cpu = 0; Cpu < MAXIMUM_PROCESSORS; Cpu++)
    {
        Stage = KdpStageSlots[Cpu].Stage;
        if (Stage == KDP_STAGE_NONE)
            continue;

        RtlStringCbPrintfA(Line,
                           sizeof(Line),
                           "LiberNT: %hs CPU %lu stage %lu\r\n",
                           Why,
                           Cpu,
                           Stage);

        /* Persist a bounded prefix: the variable budget is small, but the
         * first processors are enough to tell where startup stopped. */
        if (Persisted < 8)
        {
            KdpFbCheckpoint(Line);
            Persisted++;
        }
        else
        {
            KdpFbPrintString(Line);
        }
    }
}

/*
 * Phase checkpoint: drawn on the screen and, within a small budget, persisted
 * into the boot log variable.  The screen can be taken away at any moment (the
 * boot video driver and the logo own it after a while), and on a machine with
 * no serial port the variable is then the only record of how far the boot got.
 * The firmware stops answering SetVariable() after a few dozen calls, so the
 * budget is deliberately small.
 */
#define KDP_BB_CHECKPOINT_BUDGET 20
static ULONG KdpBbCheckpoints;

VOID
NTAPI
KdpFbCheckpoint(
    _In_ PCSTR Text)
{
    if (!KdpFbReady)
        return;

    if (KeGetCurrentIrql() == PASSIVE_LEVEL && KdpBbCheckpoints < KDP_BB_CHECKPOINT_BUDGET)
    {
        KdpBbCheckpoints++;
        KdpBbAppend(Text, (ULONG)strlen(Text));
        KdpBbFlush();
    }

    if (KdpFbTryLockConsole())
    {
        KdpFbDraw(Text);
        KdpFbUnlockConsole();
    }
}

/* KDP_PRINT_ROUTINE */
static
VOID
NTAPI
KdpFbPrint(
    _In_ PCCH String,
    _In_ ULONG Length)
{
    ULONG i, Chunk = 0;

    if (!KdpFbReady)
        return;

    /*
     * Keep mirroring after the boot video driver is installed: that is where an
     * assertion, a bugcheck or a driver message would otherwise be printed to a
     * serial port nobody reads.  Turned off when the boot reaches the end of
     * phase 1 so the desktop is not painted over.
     */
    if (!KdpFbStreamOn)
        return;

    if (!KdpFbTryLockConsole())
        return;

    for (i = 0; i < Length; i += Chunk)
    {
        CHAR Piece[64];
        ULONG Count = min(Length - i, sizeof(Piece) - 1);

        RtlCopyMemory(Piece, String + i, Count);
        Piece[Count] = ANSI_NULL;
        KdpFbDraw(Piece);
        Chunk = Count;
    }

    KdpFbUnlockConsole();

    /* Mirror the stream and checkpoint it every four kilobytes: each
     * checkpoint is a firmware call, and the firmware stops answering after
     * a few dozen of them once boot services are gone. */
    KdpBbAppend(String, Length);
    KdpBbPending += Length;
    if (KdpBbPending >= 4096)
    {
        KdpBbFlush();
        KdpBbPending = 0;
    }
}

/* KDP_INIT_ROUTINE */
NTSTATUS
NTAPI
KdpFbInit(
    _In_ PKD_DISPATCH_TABLE DispatchTable,
    _In_ ULONG BootPhase)
{
    if (BootPhase != 0)
        return STATUS_SUCCESS;

    if (!KdpFbReady)
        KdpFbEarlyInit(KeLoaderBlock);

    DbgPrint("KDFB: provider init, ready %u, reason %lu, magic 0x%08lx\n",
            KdpFbReady,
            KdpFbReason,
            (KeLoaderBlock && KeLoaderBlock->Extension) ?
                KeLoaderBlock->Extension->EarlyConsole.Magic : 0);

    if (!KdpFbReady)
        return STATUS_PORT_DISCONNECTED;

    KdpBbEnable(KeLoaderBlock);

    DispatchTable->KdpPrintRoutine = KdpFbPrint;
    DispatchTable->KdpInitRoutine = KdpFbInit;
    InsertTailList(&KdProviders, &DispatchTable->KdProvidersList);
    return STATUS_SUCCESS;
}
