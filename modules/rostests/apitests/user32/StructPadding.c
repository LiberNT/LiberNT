/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that the bytes between the fields of ICONINFO and MENUBARINFO come back as zeroes
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#define ROUNDS 64

/* The bytes of Structure from From up to To, ORed together */
static ULONG
Residue(_In_ const VOID *Structure, _In_ SIZE_T From, _In_ SIZE_T To)
{
    const UCHAR *Bytes = Structure;
    ULONG Seen = 0;

    while (From < To)
        Seen |= Bytes[From++];
    return Seen;
}

static void
Test_IconInfo(void)
{
    static const BYTE Bits[4] = { 0x55, 0xAA, 0x55, 0xAA };
    HCURSOR Cursor;
    ICONINFO Info;
    ULONG Round, Seen = 0;

    Cursor = CreateCursor(NULL, 1, 2, 4, 4, Bits, Bits);
    ok(Cursor != NULL, "CreateCursor failed, error %lu\n", GetLastError());
    if (Cursor == NULL)
        return;

    for (Round = 0; Round < ROUNDS; Round++)
    {
        memset(&Info, 0xA5, sizeof(Info));
        if (!GetIconInfo(Cursor, &Info))
        {
            ok(FALSE, "Round %lu: GetIconInfo failed, error %lu\n", Round, GetLastError());
            break;
        }
        ok(Info.xHotspot == 1 && Info.yHotspot == 2, "Round %lu: hotspot (%lu, %lu)\n", Round, Info.xHotspot, Info.yHotspot);
        Seen |= Residue(&Info, FIELD_OFFSET(ICONINFO, yHotspot) + sizeof(Info.yHotspot), FIELD_OFFSET(ICONINFO, hbmMask));
        DeleteObject(Info.hbmMask);
        DeleteObject(Info.hbmColor);
        /* Something else runs on the kernel stack between two rounds */
        (void)GetDesktopWindow();
        (void)GetTickCount();
    }
    ok(Seen == 0, "ICONINFO carried 0x%lx between yHotspot and hbmMask\n", Seen);
    if (FIELD_OFFSET(ICONINFO, hbmMask) == FIELD_OFFSET(ICONINFO, yHotspot) + sizeof(Info.yHotspot))
        skip("ICONINFO has no padding on this architecture\n");

    DestroyCursor(Cursor);
}

static void
Test_MenuBarInfo(void)
{
    HWND Window;
    HMENU Menu, Popup;
    MENUBARINFO Info;
    SIZE_T Flags = FIELD_OFFSET(MENUBARINFO, hwndMenu) + sizeof(Info.hwndMenu);
    ULONG Round, Seen = 0, Bits = 0, Word;

    Menu = CreateMenu();
    Popup = CreatePopupMenu();
    ok(Menu != NULL && Popup != NULL, "CreateMenu failed, error %lu\n", GetLastError());
    AppendMenuW(Popup, MF_STRING, 100, L"Item");
    AppendMenuW(Menu, MF_POPUP, (UINT_PTR)Popup, L"Menu");
    Window = CreateWindowExW(0, L"BUTTON", L"StructPadding", WS_OVERLAPPEDWINDOW,
                             10, 10, 200, 100, NULL, Menu, NULL, NULL);
    ok(Window != NULL, "CreateWindowExW failed, error %lu\n", GetLastError());
    if (Window == NULL)
    {
        DestroyMenu(Menu);
        return;
    }

    for (Round = 0; Round < ROUNDS; Round++)
    {
        memset(&Info, 0xA5, sizeof(Info));
        Info.cbSize = sizeof(Info);
        if (!GetMenuBarInfo(Window, OBJID_MENU, 0, &Info))
        {
            ok(FALSE, "Round %lu: GetMenuBarInfo failed, error %lu\n", Round, GetLastError());
            break;
        }
        ok(Info.hMenu == Menu, "Round %lu: hMenu %p, expected %p\n", Round, Info.hMenu, Menu);
        Seen |= Residue(&Info, FIELD_OFFSET(MENUBARINFO, rcBar) + sizeof(Info.rcBar), FIELD_OFFSET(MENUBARINFO, hMenu));
        Seen |= Residue(&Info, Flags + sizeof(ULONG), sizeof(Info));
        /* fBarFocused and fFocused are the two low bits of the word after hwndMenu; the other thirty are nobody's */
        memcpy(&Word, (const UCHAR *)&Info + Flags, sizeof(Word));
        Bits |= Word & ~3UL;
        (void)GetDesktopWindow();
        (void)GetTickCount();
    }
    ok(Seen == 0, "MENUBARINFO carried 0x%lx in its padding\n", Seen);
    ok(Bits == 0, "MENUBARINFO carried 0x%lx beside its two flags\n", Bits);

    DestroyWindow(Window);
}

START_TEST(StructPadding)
{
    Test_IconInfo();
    Test_MenuBarInfo();
}
