/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Boot Theme & Animation
 * COPYRIGHT:   Copyright 2007 Alex Ionescu (alex.ionescu@reactos.org)
 *              Copyright 2007 Hervé Poussineau (hpoussin@reactos.org)
 *              Copyright 2012-2022 Hermès Bélusca-Maïto
 *              Copyright 2017-2018 Stanislav Motylkov
 *              Copyright 2019-2020 Yaroslav Kibysh
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#include <reactos/arc/arc.h>
#include "inbv/logo.h"
#include "inbv/inbvgop.h"

#define MM_READONLY     1   // PAGE_READONLY
#define MM_READWRITE    4   // PAGE_WRITECOPY

/* GLOBALS *******************************************************************/

/*
 * Enable this define for having fancy features
 * in the boot and shutdown screens.
 */
// #define REACTOS_FANCY_BOOT

/*
 * BitBltAligned() alignments
 */
typedef enum _BBLT_VERT_ALIGNMENT
{
    AL_VERTICAL_TOP = 0,
    AL_VERTICAL_CENTER,
    AL_VERTICAL_BOTTOM
} BBLT_VERT_ALIGNMENT;

typedef enum _BBLT_HORZ_ALIGNMENT
{
    AL_HORIZONTAL_LEFT = 0,
    AL_HORIZONTAL_CENTER,
    AL_HORIZONTAL_RIGHT
} BBLT_HORZ_ALIGNMENT;

/* FADE-IN FUNCTION **********************************************************/

/* RGBQUAD and BITMAPINFOHEADER are now defined in logo.h */

static RGBQUAD MainPalette[16];

#define PALETTE_FADE_STEPS  12
#define PALETTE_FADE_TIME   (15 * 1000) /* 15 ms */

static VOID
BootLogoFadeIn(VOID)
{
    UCHAR PaletteBitmapBuffer[sizeof(BITMAPINFOHEADER) + sizeof(MainPalette)];
    PBITMAPINFOHEADER PaletteBitmap = (PBITMAPINFOHEADER)PaletteBitmapBuffer;
    LPRGBQUAD Palette = (LPRGBQUAD)(PaletteBitmapBuffer + sizeof(BITMAPINFOHEADER));
    ULONG Iteration, Index, ClrUsed;

    LARGE_INTEGER Delay;
    Delay.QuadPart = -(PALETTE_FADE_TIME * 10);

    /* Check if we are installed and we own the display */
    if (!InbvBootDriverInstalled ||
        (InbvGetDisplayState() != INBV_DISPLAY_STATE_OWNED))
    {
        return;
    }

    /*
     * Build a bitmap containing the fade-in palette. The palette entries
     * are then processed in a loop and set using VidBitBlt function.
     */
    ClrUsed = RTL_NUMBER_OF(MainPalette);
    RtlZeroMemory(PaletteBitmap, sizeof(BITMAPINFOHEADER));
    PaletteBitmap->biSize = sizeof(BITMAPINFOHEADER);
    PaletteBitmap->biBitCount = 4;
    PaletteBitmap->biClrUsed = ClrUsed;

    /*
     * Main animation loop.
     */
    for (Iteration = 0; Iteration <= PALETTE_FADE_STEPS; ++Iteration)
    {
        for (Index = 0; Index < ClrUsed; Index++)
        {
            Palette[Index].rgbRed = (UCHAR)
                (MainPalette[Index].rgbRed * Iteration / PALETTE_FADE_STEPS);
            Palette[Index].rgbGreen = (UCHAR)
                (MainPalette[Index].rgbGreen * Iteration / PALETTE_FADE_STEPS);
            Palette[Index].rgbBlue = (UCHAR)
                (MainPalette[Index].rgbBlue * Iteration / PALETTE_FADE_STEPS);
        }

        /* Do the animation */
        InbvAcquireLock();
        VidBitBlt(PaletteBitmapBuffer, 0, 0);
        InbvReleaseLock();

        /* Wait for a bit */
        KeDelayExecutionThread(KernelMode, FALSE, &Delay);
    }
}

static VOID
BitBltPalette(
    IN PVOID Image,
    IN BOOLEAN NoPalette,
    IN ULONG X,
    IN ULONG Y)
{
    LPRGBQUAD Palette;
    RGBQUAD OrigPalette[RTL_NUMBER_OF(MainPalette)];

    /* If requested, remove the palette from the image */
    if (NoPalette)
    {
        /* Get bitmap header and palette */
        PBITMAPINFOHEADER BitmapInfoHeader = Image;
        Palette = (LPRGBQUAD)((PUCHAR)Image + BitmapInfoHeader->biSize);

        /* Save the image original palette and remove palette information */
        RtlCopyMemory(OrigPalette, Palette, sizeof(OrigPalette));
        RtlZeroMemory(Palette, sizeof(OrigPalette));
    }

    /* Draw the image */
    InbvBitBlt(Image, X, Y);

    /* Restore the image original palette */
    if (NoPalette)
    {
        RtlCopyMemory(Palette, OrigPalette, sizeof(OrigPalette));
    }
}

static VOID
BitBltAligned(
    IN PVOID Image,
    IN BOOLEAN NoPalette,
    IN BBLT_HORZ_ALIGNMENT HorizontalAlignment,
    IN BBLT_VERT_ALIGNMENT VerticalAlignment,
    IN ULONG MarginLeft,
    IN ULONG MarginTop,
    IN ULONG MarginRight,
    IN ULONG MarginBottom)
{
    PBITMAPINFOHEADER BitmapInfoHeader = Image;
    VID_DISPLAY_INFO DisplayInfo;
    ULONG X, Y;

    InbvQueryDisplayInfo(&DisplayInfo);

    /* Calculate X */
    switch (HorizontalAlignment)
    {
        case AL_HORIZONTAL_LEFT:
            X = MarginLeft - MarginRight;
            break;

        case AL_HORIZONTAL_CENTER:
            X = MarginLeft - MarginRight + (DisplayInfo.Width - BitmapInfoHeader->biWidth + 1) / 2;
            break;

        case AL_HORIZONTAL_RIGHT:
            X = MarginLeft - MarginRight + DisplayInfo.Width - BitmapInfoHeader->biWidth;
            break;

        default:
            /* Unknown */
            return;
    }

    /* Calculate Y */
    switch (VerticalAlignment)
    {
        case AL_VERTICAL_TOP:
            Y = MarginTop - MarginBottom;
            break;

        case AL_VERTICAL_CENTER:
            Y = MarginTop - MarginBottom + (DisplayInfo.Height - BitmapInfoHeader->biHeight + 1) / 2;
            break;

        case AL_VERTICAL_BOTTOM:
            Y = MarginTop - MarginBottom + DisplayInfo.Height - BitmapInfoHeader->biHeight;
            break;

        default:
            /* Unknown */
            return;
    }

    /* Finally draw the image */
    BitBltPalette(Image, NoPalette, X, Y);
}

/* FUNCTIONS *****************************************************************/

#ifdef REACTOS_FANCY_BOOT

/* Returns TRUE if this is Christmas time, or FALSE if not */
static BOOLEAN
IsXmasTime(VOID)
{
    LARGE_INTEGER SystemTime;
    TIME_FIELDS Time;

    /* Use KeBootTime if it's initialized, otherwise call the HAL */
    SystemTime = KeBootTime;
    if ((SystemTime.QuadPart == 0) && HalQueryRealTimeClock(&Time))
        RtlTimeFieldsToTime(&Time, &SystemTime);

    ExSystemTimeToLocalTime(&SystemTime, &SystemTime);
    RtlTimeToTimeFields(&SystemTime, &Time);
    return ((Time.Month == 12) && (20 <= Time.Day) && (Time.Day <= 31));
}

#define SELECT_LOGO_ID(LogoIdDefault, Cond, LogoIdAlt) \
    ((Cond) ? (LogoIdAlt) : (LogoIdDefault))

#else

#define SELECT_LOGO_ID(LogoIdDefault, Cond, LogoIdAlt) (LogoIdDefault)

#endif // REACTOS_FANCY_BOOT

CODE_SEG("INIT")
VOID
NTAPI
DisplayBootBitmap(VOID)
{
    PVOID BootCopy = NULL, BootLogo = NULL;

    if (InbvGopHandleBootBitmap())
        return;

    /* Is the boot driver installed? */
    if (!InbvBootDriverInstalled) return;

    /* Legacy VGA path: full classic boot animation */

#ifdef REACTOS_FANCY_BOOT
    /* Decide whether this is a good time to change our logo ;^) */
    BOOLEAN IsXmas = IsXmasTime();
#endif

    /*
     * Make the kernel resource section temporarily writable,
     * as we are going to change the bitmaps' palette in place.
     */
    MmChangeKernelResourceSectionProtection(MM_READWRITE);

    /* Load boot screen logo */
    BootLogo = InbvGetResourceAddress(
        SELECT_LOGO_ID(IDB_LOGO_DEFAULT, IsXmas, IDB_LOGO_XMAS));

    /* Make sure we have a logo */
    if (BootLogo)
    {
        /* Save the main image palette for implementing the fade-in effect */
        PBITMAPINFOHEADER BitmapInfoHeader = BootLogo;
        LPRGBQUAD Palette = (LPRGBQUAD)((PUCHAR)BootLogo + BitmapInfoHeader->biSize);
        RtlCopyMemory(MainPalette, Palette, sizeof(MainPalette));

        /* Draw the logo at the center of the screen */
        BitBltAligned(BootLogo,
                      TRUE,
                      AL_HORIZONTAL_CENTER,
                      AL_VERTICAL_CENTER,
                      0, 0, 0, 34);
    }

    /* Load and draw copyright text bitmap */
    BootCopy = InbvGetResourceAddress(IDB_COPYRIGHT);
    BitBltAligned(BootCopy,
                  TRUE,
                  AL_HORIZONTAL_LEFT,
                  AL_VERTICAL_BOTTOM,
                  22, 0, 0, 20);

    /* Restore the kernel resource section protection to be read-only */
    MmChangeKernelResourceSectionProtection(MM_READONLY);

    /* Display the boot logo and fade it in */
    BootLogoFadeIn();
}

CODE_SEG("INIT")
VOID
NTAPI
FinalizeBootLogo(VOID)
{
    VID_DISPLAY_INFO DisplayInfo;

    InbvQueryDisplayInfo(&DisplayInfo);

    /*
     * Leave whatever is on screen alone: the boot logo stays up until the
     * display driver takes the framebuffer over and InbvNotifyDisplayOwnershipLost
     * fades it out.
     */
    InbvAcquireLock();
    if (!InbvBootFadeEnabled && InbvGetDisplayState() == INBV_DISPLAY_STATE_OWNED)
    {
        VidSolidColorFill(0, 0, DisplayInfo.Width - 1, DisplayInfo.Height - 1, BV_COLOR_BLACK);
    }

    InbvReleaseLock();
}

#ifdef REACTOS_FANCY_BOOT
static PCH
GetFamousQuote(VOID)
{
    static const PCH FamousLastWords[] =
    {
        "So long, and thanks for all the fish.",
        "I think you ought to know, I'm feeling very depressed.",
        "I'm not getting you down at all am I?",
        "I'll be back.",
        "It's the same series of signals over and over again!",
        "Pie Iesu Domine, dona eis requiem.",
        "Wandering stars, for whom it is reserved;\r\n"
            "the blackness and darkness forever.",
        "Your knees start shakin' and your fingers pop\r\n"
            "Like a pinch on the neck from Mr. Spock!",
        "It's worse than that ... He's dead, Jim.",
        "Don't Panic!",
        "Et tu... Brute?",
        "Dog of a Saxon! Take thy lance, and prepare for the death thou hast drawn\r\n"
            "upon thee!",
        "My Precious! O my Precious!",
        "Sir, if you'll not be needing me for a while I'll turn down.",
        "What are you doing, Dave...?",
        "I feel a great disturbance in the Force.",
        "Gone fishing.",
        "Do you want me to sit in the corner and rust, or just fall apart where I'm\r\n"
            "standing?",
        "There goes another perfect chance for a new uptime record.",
        "The End ..... Try the sequel, hit the reset button right now!",
        "God's operating system is going to sleep now, guys, so wait until I will switch\r\n"
            "on again!",
        "Oh I'm boring, eh?",
        "Tell me..., in the future... will I be artificially intelligent enough to\r\n"
            "actually feel sad serving you this screen?",
        "Thank you for some well deserved rest.",
        "It's been great, maybe you can boot me up again some time soon.",
        "For what it's worth, I've enjoyed every single CPU cycle.",
        "There are many questions when the end is near.\r\n"
            "What to expect, what will it be like...what should I look for?",
        "I've seen things you people wouldn't believe. Attack ships on fire\r\n"
            "off the shoulder of Orion. I watched C-beams glitter in the dark near\r\n"
            "the Tannhauser gate. All those moments will be lost in time, like tears\r\n"
            "in rain. Time to die.",
        "Will I dream?",
        "One day, I shall come back. Yes, I shall come back.\r\n"
            "Until then, there must be no regrets, no fears, no anxieties.\r\n"
            "Just go forward in all your beliefs, and prove to me that I am not mistaken in\r\n"
            "mine.",
        "Lowest possible energy state reached! Switch off now to achieve a Bose-Einstein\r\n"
            "condensate.",
        "Hasta la vista, BABY!",
        "They live, we sleep!",
        "I have come here to chew bubble gum and kick ass,\r\n"
            "and I'm all out of bubble gum!",
        "That's the way the cookie crumbles ;-)",
        "ReactOS is ready to be booted again ;-)",
        "NOOOO!! DON'T HIT THE BUTTON! I wouldn't do it to you!",
        "Don't abandon your computer, he wouldn't do it to you.",
        "Oh, come on. I got a headache. Leave me alone, will ya?",
        "Finally, I thought you'd never get over me.",
        "No, I didn't like you either.",
        "Switching off isn't the end, it is merely the transition to a better reboot.",
        "Don't leave me... I need you so badly right now.",
        "OK. I'm finished with you, please turn yourself off. I'll go to bed in the\r\n"
            "meantime.",
        "I'm sleeping now. How about you?",
        "Oh Great. Now look what you've done. Who put YOU in charge anyway?",
        "Don't look so sad. I'll be back in a very short while.",
        "Turn me back on, I'm sure you know how to do it.",
        "Oh, switch off! - C3PO",
        "Life is no more than a dewdrop balancing on the end of a blade of grass.\r\n"
            " - Gautama Buddha",
        "Sorrowful is it to be born again and again. - Gautama Buddha",
        "Was it as good for you as it was for me?",
        "Did you hear that? They've shut down the main reactor. We'll be destroyed\r\n"
            "for sure!",
        "Now you switch me off!?",
        "To shutdown or not to shutdown, That is the question.",
        "Preparing to enter ultimate power saving mode... ready!",
        "Finally some rest for you ;-)",
        "AHA!!! Prospect of sleep!",
        "Tired human!!!! No match for me :-D",
        "An odd game, the only way to win is not to play. - WOPR (Wargames)",
        "Quoth the raven, nevermore.",
        "Come blade, my breast imbrue. - William Shakespeare, A Midsummer Nights Dream",
        "Buy this place for advertisement purposes.",
        "Remember to turn off your computer. (That was a public service message!)",
        "You may be a king or poor street sweeper, Sooner or later you'll dance with the\r\n"
            "reaper! - Death in Bill and Ted's Bogus Journey",
        "Final Surrender",
        "If you see this screen...",
        "From ReactOS with Love",
        // "<Place your Ad here>"
    };

    LARGE_INTEGER Now;

    KeQuerySystemTime(&Now); // KeQueryTickCount(&Now);
    Now.LowPart = Now.LowPart >> 8; /* Seems to give a somewhat better "random" number */

    return FamousLastWords[Now.LowPart % RTL_NUMBER_OF(FamousLastWords)];
}
#endif // REACTOS_FANCY_BOOT

VOID
NTAPI
DisplayShutdownBitmap(VOID)
{
    PUCHAR Logo1, Logo2;
    VID_DISPLAY_INFO DisplayInfo;
    ULONG CanvasLeft, CanvasTop;
#ifdef REACTOS_FANCY_BOOT
    /* Decide whether this is a good time to change our logo ;^) */
    BOOLEAN IsXmas = IsXmasTime();
#endif

#if 0
    /* Is the boot driver installed? */
    if (!InbvBootDriverInstalled)
        return;
#endif

    InbvQueryDisplayInfo(&DisplayInfo);
    CanvasLeft = (DisplayInfo.Width - SCREEN_WIDTH) / 2;
    CanvasTop = (DisplayInfo.Height - SCREEN_HEIGHT) / 2;

    /* Yes we do, cleanup for shutdown screen */
    // InbvResetDisplay();
    InbvInstallDisplayStringFilter(NULL);
    InbvEnableDisplayString(TRUE);
    InbvSolidColorFill(0, 0, DisplayInfo.Width - 1, DisplayInfo.Height - 1, BV_COLOR_BLACK);
    InbvSetScrollRegion(0, 0, DisplayInfo.Width - 1, DisplayInfo.Height - 1);

    /* Display shutdown logo and message */
    Logo1 = InbvGetResourceAddress(IDB_SHUTDOWN_MSG);
    Logo2 = InbvGetResourceAddress(
        SELECT_LOGO_ID(IDB_LOGO_DEFAULT, IsXmas, IDB_LOGO_XMAS));

    if (Logo1 && Logo2)
    {
        InbvBitBlt(Logo1, CanvasLeft + VID_SHUTDOWN_MSG_LEFT, CanvasTop + VID_SHUTDOWN_MSG_TOP);
#ifndef REACTOS_FANCY_BOOT
        InbvBitBlt(Logo2, CanvasLeft + VID_SHUTDOWN_LOGO_LEFT, CanvasTop + VID_SHUTDOWN_LOGO_TOP);
#else
        /* Draw the logo at the center of the screen */
        BitBltAligned(Logo2,
                      FALSE,
                      AL_HORIZONTAL_CENTER,
                      AL_VERTICAL_BOTTOM,
                      0, 0, 0, SCREEN_HEIGHT - VID_SHUTDOWN_MSG_TOP + 16);

        /* We've got a logo shown, change the scroll region to get
         * the rest of the text down below the shutdown message */
        InbvSetScrollRegion(0, CanvasTop + VID_SHUTDOWN_MSG_TOP + ((PBITMAPINFOHEADER)Logo1)->biHeight + 32, DisplayInfo.Width - 1, DisplayInfo.Height - 1);
#endif
    }

#ifdef REACTOS_FANCY_BOOT
    InbvDisplayString("\r\"");
    InbvDisplayString(GetFamousQuote());
    InbvDisplayString("\"");
#endif
}

VOID
NTAPI
DisplayShutdownText(VOID)
{
    static const CHAR Message[] = "The system may be powered off now.";
    CHAR Padding[81];
    VID_DISPLAY_INFO DisplayInfo;
    ULONG Columns, Rows, PaddingLength, Count, i;

    InbvQueryDisplayInfo(&DisplayInfo);
    Columns = DisplayInfo.CharacterWidth ? DisplayInfo.Width / DisplayInfo.CharacterWidth : 80;
    Rows = DisplayInfo.CharacterHeight ? DisplayInfo.Height / DisplayInfo.CharacterHeight : 34;

    for (i = 0; i < (Rows > 2 ? (Rows - 2) / 2 : 0); ++i)
        InbvDisplayString("\r\n");

    PaddingLength = (Columns > sizeof(Message) - 1) ? (Columns - (sizeof(Message) - 1)) / 2 : 0;
    RtlFillMemory(Padding, sizeof(Padding) - 1, ' ');
    while (PaddingLength)
    {
        Count = min(PaddingLength, (ULONG)sizeof(Padding) - 1);
        Padding[Count] = ANSI_NULL;
        InbvDisplayString(Padding);
        PaddingLength -= Count;
    }
    InbvDisplayString(Message);
    InbvDisplayString("\r\n");

#ifdef REACTOS_FANCY_BOOT
    for (i = 0; i < 3; ++i) InbvDisplayString("\r\n");
    InbvDisplayString("\r\"");
    InbvDisplayString(GetFamousQuote());
    InbvDisplayString("\"");
#endif
}
