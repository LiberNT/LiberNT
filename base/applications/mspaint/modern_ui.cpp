/*
 * LiberNT Paint modern ribbon - phase 3
 * Original UI implementation. Existing ReactOS Paint artwork is reused
 * through the application's IDB_TOOLBARICONS resource, not duplicated.
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
#include "precomp.h"
#include "modern_ui.h"
#include "modern_layers.h"

namespace
{
    const WCHAR kRibbonClass[] = L"LiberNTPaintRibbon";
    // Paint modern ribbon v3 - GDI glyphs and compact palette layout.
    const COLORREF kSurface = RGB(248, 249, 251);
    const COLORREF kText = RGB(37, 43, 52);
    const COLORREF kMuted = RGB(99, 105, 113);
    const COLORREF kBorder = RGB(217, 222, 228);
    const COLORREF kSelected = RGB(219, 233, 250);
    const int kHeight = 155;
    const int kTop = 50;
    const int kRowStep = 36;
    const int kColorStep = 19;
    const UINT kSizeCommandBase = 49500;
    const UINT kWandCommand = 49601;
    const UINT kWandIconResource = 52001;

    struct Action
    {
        UINT command;
        const WCHAR *label;
        int bitmapIndex; // -1 for text-only action
        RECT rc;
        BOOL visible;
    };

    // Each tool gets exactly one visual action, using its existing bitmap index.
    Action kActions[] =
    {
        { IDM_EDITPASTE, L"Paste", -1 },
        { IDM_EDITCUT, L"Cut", -1 },
        { IDM_EDITCOPY, L"Copy", -1 },
        { ID_RECTSEL, L"Select", 1 },
        { IDM_IMAGECROP, L"Crop", -1 },
        { IDM_IMAGESTRETCHSKEW, L"Resize", -1 },
        { IDM_IMAGEROTATEMIRROR, L"Rotate", -1 },
        { ID_PEN, L"Pencil", 6 },
        { ID_FILL, L"Fill", 3 },
        { ID_TEXT, L"Text", 9 },
        { ID_RUBBER, L"Eraser", 2 },
        { ID_COLOR, L"Picker", 4 },
        { ID_ZOOM, L"Magnifier", 5 },
        { kWandCommand, L"Magic Wand", -1 },
        { ID_BRUSH, L"Brush", 7 },
        { ID_AIRBRUSH, L"Airbrush", 8 },
        { ID_LINE, L"Line", 10 },
        { ID_BEZIER, L"Curve", 11 },
        { ID_RECT, L"Rectangle", 12 },
        { ID_SHAPE, L"Polygon", 13 },
        { ID_ELLIPSE, L"Ellipse", 14 },
        { ID_RRECT, L"Rounded rect", 15 },
        { kSizeCommandBase, L"Size", -1 },
        { IDM_VIEWZOOM50, L"50%", -1 },
        { IDM_VIEWZOOM100, L"100%", -1 },
        { IDM_VIEWZOOM200, L"200%", -1 },
        { IDM_VIEWSHOWGRID, L"Grid", -1 },
        { IDM_VIEWSTATUSBAR, L"Status bar", -1 },
        { IDM_VIEWFULLSCREEN, L"Full screen", -1 }
    };
    enum { ACTION_COUNT = sizeof(kActions) / sizeof(kActions[0]) };
    enum { HOME_ACTIONS = 23 };

    struct RibbonState
    {
        BOOL home;
        int hover;
        HIMAGELIST icons;
        HICON wandIcon;
        HFONT font;
        HFONT smallFont;
        RECT tabHome;
        RECT tabView;
        RECT colorBoxes[NUM_COLORS];
        RECT foreground;
        RECT background;
        RECT editColors;
        RECT quickSave;
        RECT quickUndo;
        RECT quickRedo;
        RECT quickLayers;
        BOOL compact;
        BOOL paletteVisible;
    };

    RECT MakeRect(int x, int y, int width, int height)
    {
        RECT rc = { x, y, x + width, y + height };
        return rc;
    }

    BOOL Contains(const RECT& rc, POINT pt)
    {
        return PtInRect(&rc, pt);
    }

    void PaintText(HDC dc, const WCHAR *str, const RECT& rc, COLORREF color,
                   UINT align = DT_CENTER | DT_VCENTER | DT_SINGLELINE)
    {
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, color);
        RECT r = rc;
        DrawTextW(dc, str, -1, &r, align);
    }

    void Fill(HDC dc, RECT rc, COLORREF color)
    {
        HBRUSH brush = CreateSolidBrush(color);
        FillRect(dc, &rc, brush);
        DeleteObject(brush);
    }

    void Frame(HDC dc, RECT rc, COLORREF color)
    {
        HBRUSH brush = CreateSolidBrush(color);
        FrameRect(dc, &rc, brush);
        DeleteObject(brush);
    }

    void Label(HDC dc, const WCHAR* label, int left, int right)
    {
        PaintText(dc, label, MakeRect(left, 125, right - left, 22), kMuted);
    }

    void Separator(HDC dc, int x)
    {
        Fill(dc, MakeRect(x, 45, 1, 99), kBorder);
    }

    // Preserve a stable layout at normal desktop widths; compact controls
    // use smaller text-only action rectangles at narrower widths.
    void Layout(HWND hwnd, RibbonState *state)
    {
        RECT client;
        GetClientRect(hwnd, &client);
        int width = client.right - client.left;
        state->compact = width < 645;
        state->paletteVisible = width >= 970;
        state->tabHome = MakeRect(12, 5, 75, 30);
        state->tabView = MakeRect(92, 5, 75, 30);
        state->quickSave = MakeRect(185, 5, 34, 29);
        state->quickUndo = MakeRect(225, 5, 34, 29);
        state->quickRedo = MakeRect(265, 5, 34, 29);
        state->quickLayers = MakeRect((width > 426 ? width - 101 : 325), 5, 94, 29);
        for (int i = 0; i < ACTION_COUNT; ++i)
        {
            kActions[i].rc = MakeRect(0, 0, 0, 0);
            kActions[i].visible = FALSE;
        }
        // Compact icon-only groups, with a single control per action.
        // Clipboard: large paste control and two small actions beside it.
        const int groupX[] = { 12, 102, 243, 357, 425, 565 };
        kActions[0].rc = MakeRect(12, 52, 36, 66);
        kActions[1].rc = MakeRect(53, 53, 33, 30);
        kActions[2].rc = MakeRect(53, 88, 33, 30);
        // Image options: two by two, with short captions.
        for (int i = 3; i <= 6; ++i)
            kActions[i].rc = MakeRect(groupX[1] + (i - 3) % 2 * 66,
                                      kTop + (i - 3) / 2 * 35, 62, 31);
        // Seven tools: compact 4 x 2 grid, with Magic Wand at the end.
        for (int i = 7; i <= 13; ++i)
            kActions[i].rc = MakeRect(groupX[2] + (i - 7) % 4 * 27,
                                      kTop + (i - 7) / 4 * kRowStep, 26, 32);
        // Two brushes.
        for (int i = 14; i <= 15; ++i)
            kActions[i].rc = MakeRect(groupX[3] + (i - 14) * 31, kTop + 14, 30, 35);
        // Six shapes.
        for (int i = 16; i <= 21; ++i)
            kActions[i].rc = MakeRect(groupX[4] + (i - 16) % 3 * 34,
                                      kTop + (i - 16) / 3 * kRowStep, 32, 32);
        kActions[22].rc = MakeRect(groupX[5], kTop + 14, 53, 36);
        if (state->compact)
        {
            // On narrow windows, keep the input region inside the client width.
            // Only presentation changes; all core tool commands remain reachable
            // from the original menu and keyboard shortcuts.
            int limit = width - 15;
            for (int i = 0; i <= 22; ++i)
                kActions[i].visible = kActions[i].rc.right <= limit;
        }
        else
        {
            for (int i = 0; i <= 22; ++i)
                kActions[i].visible = TRUE;
        }
        for (int i = 23; i < ACTION_COUNT; ++i)
        {
            int col = (i - 23) % 3;
            int row = (i - 23) / 3;
            kActions[i].rc = MakeRect(15 + col * 125, 52 + row * 34, 116, 30);
            kActions[i].visible = TRUE;
        }
        const int paletteX = 672;
        state->foreground = MakeRect(632, 53, 29, 29);
        state->background = MakeRect(632, 89, 29, 29);
        for (int i = 0; i < NUM_COLORS; ++i)
        {
            state->colorBoxes[i] = MakeRect(paletteX + (i % 14) * kColorStep,
                                             55 + (i / 14) * 37, 16, 16);
        }
        state->editColors = MakeRect(947, 71, 25, 32);
    }

    // Small vector icons for document actions absent from the original toolbar.
    // Drawn with Win32 GDI; no external fonts or proprietary bitmaps.
    void DrawGlyph(HDC dc, UINT cmd, RECT rect)
    {
        int x = (rect.left + rect.right - 18) / 2;
        int y = (rect.top + rect.bottom - 18) / 2;
        HPEN pen = CreatePen(PS_SOLID, 2, RGB(69, 79, 91));
        HPEN previous = (HPEN)SelectObject(dc, pen);
        HBRUSH previousBrush = (HBRUSH)SelectObject(dc, GetStockObject(NULL_BRUSH));
        switch (cmd)
        {
        case IDM_EDITPASTE:
            Rectangle(dc, x + 3, y + 4, x + 16, y + 18);
            Rectangle(dc, x + 6, y + 1, x + 13, y + 6);
            break;
        case IDM_EDITCUT:
            Ellipse(dc, x + 1, y + 10, x + 7, y + 16);
            Ellipse(dc, x + 11, y + 10, x + 17, y + 16);
            MoveToEx(dc, x + 4, y + 11, NULL); LineTo(dc, x + 15, y + 2);
            MoveToEx(dc, x + 14, y + 11, NULL); LineTo(dc, x + 4, y + 2);
            break;
        case IDM_EDITCOPY:
            Rectangle(dc, x + 5, y + 2, x + 17, y + 14);
            MoveToEx(dc, x + 2, y + 5, NULL); LineTo(dc, x + 2, y + 17);
            LineTo(dc, x + 14, y + 17);
            break;
        case IDM_IMAGECROP:
            MoveToEx(dc, x + 5, y + 1, NULL); LineTo(dc, x + 5, y + 13);
            LineTo(dc, x + 17, y + 13);
            MoveToEx(dc, x + 1, y + 5, NULL); LineTo(dc, x + 13, y + 5);
            LineTo(dc, x + 13, y + 17);
            break;
        case IDM_IMAGESTRETCHSKEW:
            Rectangle(dc, x + 4, y + 4, x + 14, y + 14);
            MoveToEx(dc, x + 2, y + 2, NULL); LineTo(dc, x + 7, y + 2);
            MoveToEx(dc, x + 2, y + 2, NULL); LineTo(dc, x + 2, y + 7);
            MoveToEx(dc, x + 16, y + 16, NULL); LineTo(dc, x + 11, y + 16);
            MoveToEx(dc, x + 16, y + 16, NULL); LineTo(dc, x + 16, y + 11);
            break;
        case IDM_IMAGEROTATEMIRROR:
            Arc(dc, x + 2, y + 2, x + 16, y + 16, x + 15, y + 5, x + 5, y + 15);
            MoveToEx(dc, x + 15, y + 5, NULL); LineTo(dc, x + 15, y + 11);
            LineTo(dc, x + 10, y + 9);
            break;
        case IDM_COLORSEDITPALETTE:
        {
            HBRUSH red = CreateSolidBrush(RGB(226, 75, 88));
            HBRUSH green = CreateSolidBrush(RGB(70, 170, 116));
            HBRUSH blue = CreateSolidBrush(RGB(63, 138, 219));
            RECT a = MakeRect(x + 2, y + 2, 8, 8);
            RECT b = MakeRect(x + 9, y + 2, 8, 8);
            RECT c = MakeRect(x + 5, y + 9, 8, 8);
            FillRect(dc, &a, red); FillRect(dc, &b, green); FillRect(dc, &c, blue);
            DeleteObject(red); DeleteObject(green); DeleteObject(blue);
            break;
        }
        case kSizeCommandBase:
            for (int j = 0; j < 3; ++j)
            {
                MoveToEx(dc, x + 2, y + 4 + j * 5, NULL);
                LineTo(dc, x + 16, y + 4 + j * 5);
            }
            break;
        case IDM_FILESAVE:
            Rectangle(dc, x + 2, y + 1, x + 16, y + 17);
            Rectangle(dc, x + 5, y + 2, x + 13, y + 7);
            Rectangle(dc, x + 5, y + 11, x + 13, y + 16);
            break;
        case IDM_EDITUNDO:
        case IDM_EDITREDO:
        {
            int dir = cmd == IDM_EDITUNDO ? 1 : -1;
            int p = cmd == IDM_EDITUNDO ? x + 3 : x + 15;
            MoveToEx(dc, p + dir * 5, y + 3, NULL); LineTo(dc, p, y + 8);
            LineTo(dc, p + dir * 5, y + 12);
            MoveToEx(dc, p, y + 8, NULL); LineTo(dc, p + dir * 11, y + 8);
            LineTo(dc, p + dir * 11, y + 15);
            break;
        }
        default:
            break;
        }
        SelectObject(dc, previousBrush);
        SelectObject(dc, previous);
        DeleteObject(pen);
    }

    void DrawAction(HDC dc, RibbonState *state, int i)
    {
        Action& action = kActions[i];
        if (!action.visible || action.rc.right <= action.rc.left)
            return;
        BOOL selected = (action.command == kWandCommand && ModernLayersWandMode()) ||
                        (action.command >= ID_FREESEL && action.command <= ID_RRECT &&
                         !ModernLayersWandMode() &&
                         toolsModel.GetActiveTool() == (TOOLTYPE)(action.command - ID_FREESEL + 1));
        if (selected || state->hover == i)
            Fill(dc, action.rc, selected ? kSelected : RGB(233, 238, 245));
        if (selected)
            Frame(dc, action.rc, RGB(105, 158, 218));
        if (action.command == kWandCommand && state->wandIcon)
        {
            int x = (action.rc.left + action.rc.right - 20) / 2;
            int y = (action.rc.top + action.rc.bottom - 20) / 2;
            DrawIconEx(dc, x, y, state->wandIcon, 20, 20, 0, NULL, DI_NORMAL);
        }
        else if (action.bitmapIndex >= 0 && state->icons)
        {
            int x = (action.rc.left + action.rc.right - 16) / 2;
            int y = (action.rc.top + action.rc.bottom - 16) / 2;
            ImageList_Draw(state->icons, action.bitmapIndex, dc, x, y, ILD_TRANSPARENT);
        }
        else if (i < HOME_ACTIONS)
        {
            DrawGlyph(dc, action.command, action.rc);
        }
        else
        {
            HFONT old = (HFONT)SelectObject(dc, state->smallFont);
            PaintText(dc, action.label, action.rc, kText);
            SelectObject(dc, old);
        }
    }

    void PaintRibbon(HWND hwnd, RibbonState *state, HDC dc)
    {
        RECT rc;
        GetClientRect(hwnd, &rc);
        Fill(dc, rc, kSurface);
        Fill(dc, MakeRect(0, 36, rc.right, 1), kBorder);
        Fill(dc, MakeRect(0, kHeight - 1, rc.right, 1), kBorder);
        HFONT original = (HFONT)SelectObject(dc, state->font);
        RECT tab = state->home ? state->tabHome : state->tabView;
        Fill(dc, tab, RGB(255, 255, 255));
        Fill(dc, MakeRect(tab.left, tab.bottom - 2, tab.right - tab.left, 2), RGB(42, 117, 198));
        PaintText(dc, L"Home", state->tabHome, kText);
        PaintText(dc, L"View", state->tabView, kText);
        DrawGlyph(dc, IDM_FILESAVE, state->quickSave);
        DrawGlyph(dc, IDM_EDITUNDO, state->quickUndo);
        DrawGlyph(dc, IDM_EDITREDO, state->quickRedo);
        Frame(dc, state->quickLayers, kBorder);
        PaintText(dc, L"Layers", state->quickLayers, kText);
        if (state->home)
        {
            for (int i = 0; i < HOME_ACTIONS; ++i)
                DrawAction(dc, state, i);
            Separator(dc, 94); Separator(dc, 236); Separator(dc, 350);
            Separator(dc, 422); Separator(dc, 557); Separator(dc, 625);
            Label(dc, L"Clipboard", 3, 94);
            Label(dc, L"Image", 97, 236);
            Label(dc, L"Tools", 237, 350);
            Label(dc, L"Brushes", 351, 422);
            Label(dc, L"Shapes", 423, 557);
            Label(dc, L"Size", 558, 625);
            Label(dc, L"Colors", 626, 980);
            if (state->paletteVisible)
            {
                Fill(dc, state->foreground, paletteModel.GetFgColor());
                Fill(dc, state->background, paletteModel.GetBgColor());
                Frame(dc, state->foreground, kText);
                Frame(dc, state->background, kBorder);
                for (int i = 0; i < NUM_COLORS; ++i)
                {
                    RECT box = state->colorBoxes[i];
                    HBRUSH color = CreateSolidBrush(paletteModel.GetColor(i));
                    HBRUSH oldBrush = (HBRUSH)SelectObject(dc, color);
                    HPEN pen = CreatePen(PS_SOLID, 1, kBorder);
                    HPEN oldPen = (HPEN)SelectObject(dc, pen);
                    Ellipse(dc, box.left, box.top, box.right, box.bottom);
                    COLORREF chosen = i < NUM_COLORS ? paletteModel.GetColor(i) : RGB(0, 0, 0);
                    if (chosen == paletteModel.GetFgColor() || chosen == paletteModel.GetBgColor())
                    {
                        RECT highlight = MakeRect(box.left - 2, box.top - 2, 20, 20);
                        Frame(dc, highlight, chosen == paletteModel.GetFgColor() ?
                                      RGB(37, 108, 193) : RGB(135, 142, 152));
                    }
                    SelectObject(dc, oldPen);
                    SelectObject(dc, oldBrush);
                    DeleteObject(pen);
                    DeleteObject(color);
                }
                DrawGlyph(dc, IDM_COLORSEDITPALETTE, state->editColors);
                Frame(dc, state->editColors, kBorder);
            }
        }
        else
        {
            for (int i = HOME_ACTIONS; i < ACTION_COUNT; ++i)
                DrawAction(dc, state, i);
            Label(dc, L"Zoom", 10, 385);
        }
        SelectObject(dc, original);
    }

    void ChooseSize(HWND hwnd)
    {
        HMENU popup = CreatePopupMenu();
        if (!popup)
            return;
        for (int size = 1; size <= 5; ++size)
        {
            WCHAR label[40];
            wsprintfW(label, L"%d px", size);
            AppendMenuW(popup, MF_STRING, kSizeCommandBase + size, label);
        }
        POINT pt = { kActions[22].rc.left, kActions[22].rc.bottom };
        ClientToScreen(hwnd, &pt);
        int choice = TrackPopupMenu(popup, TPM_RETURNCMD | TPM_NONOTIFY | TPM_LEFTALIGN,
                                    pt.x, pt.y, 0, hwnd, NULL);
        DestroyMenu(popup);
        if (choice > (int)kSizeCommandBase && choice <= (int)kSizeCommandBase + 5)
        {
            int width = choice - kSizeCommandBase;
            toolsModel.SetLineWidth(width);
            toolsModel.SetPenWidth(width);
            toolsModel.SetBrushWidth(width);
            toolsModel.NotifyToolSettingsChanged();
        }
    }

    void Execute(HWND hwnd, UINT command)
    {
        if (command == kWandCommand)
        {
            if (!ModernLayersHasActiveOverlay())
            {
                MessageBoxW(GetParent(hwnd),
                    L"Create or select a layer above Background before using Magic Wand.",
                    L"Magic Wand", MB_OK | MB_ICONINFORMATION);
                return;
            }
            ModernLayersSetWandMode(!ModernLayersWandMode());
        }
        else if (command == kSizeCommandBase)
            ChooseSize(hwnd);
        else if (command >= ID_FREESEL && command <= ID_RRECT)
        {
            ModernLayersSetWandMode(false);
            // Reuse original tool mapping and selection notifications.
            if (IsWindow(toolBoxContainer.m_hWnd))
                SendMessageW(toolBoxContainer.m_hWnd, WM_COMMAND, MAKEWPARAM(command, 0), 0);
        }
        else
            SendMessageW(GetParent(hwnd), WM_COMMAND, MAKEWPARAM(command, 0), 0);
        InvalidateRect(hwnd, NULL, FALSE);
    }

    LRESULT CALLBACK RibbonProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
    {
        RibbonState *state = (RibbonState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        switch (msg)
        {
            case WM_CREATE:
            {
                state = new RibbonState();
                ZeroMemory(state, sizeof(*state));
                state->home = TRUE;
                state->hover = -1;
                state->font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
                state->smallFont = state->font;
                state->wandIcon = (HICON)LoadImageW(g_hinstExe,
                    MAKEINTRESOURCEW(kWandIconResource), IMAGE_ICON, 20, 20, LR_DEFAULTCOLOR);
                state->icons = ImageList_Create(16, 16, ILC_COLOR24 | ILC_MASK, 16, 1);
                if (state->icons)
                {
                    HBITMAP bitmap = (HBITMAP)LoadImageW(g_hinstExe,
                        MAKEINTRESOURCEW(IDB_TOOLBARICONS), IMAGE_BITMAP, 256, 16, 0);
                    if (bitmap)
                    {
                        ImageList_AddMasked(state->icons, bitmap, RGB(255, 0, 255));
                        DeleteObject(bitmap);
                    }
                }
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)state);
                Layout(hwnd, state);
                return 0;
            }
            case WM_SIZE:
                if (state) { Layout(hwnd, state); InvalidateRect(hwnd, NULL, FALSE); }
                return 0;
            case WM_ERASEBKGND:
                return 1;
            case WM_PAINT:
            {
                PAINTSTRUCT ps;
                HDC dc = BeginPaint(hwnd, &ps);
                if (state) PaintRibbon(hwnd, state, dc);
                EndPaint(hwnd, &ps);
                return 0;
            }
            case WM_LBUTTONDOWN:
            case WM_RBUTTONDOWN:
            case WM_LBUTTONDBLCLK:
            {
                if (!state) return 0;
                POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
                if (msg != WM_RBUTTONDOWN && Contains(state->tabHome, pt))
                    state->home = TRUE;
                else if (msg != WM_RBUTTONDOWN && Contains(state->tabView, pt))
                    state->home = FALSE;
                else if (msg != WM_RBUTTONDOWN && Contains(state->quickSave, pt))
                    Execute(hwnd, IDM_FILESAVE);
                else if (msg != WM_RBUTTONDOWN && Contains(state->quickUndo, pt))
                    Execute(hwnd, IDM_EDITUNDO);
                else if (msg != WM_RBUTTONDOWN && Contains(state->quickRedo, pt))
                    Execute(hwnd, IDM_EDITREDO);
                else if (msg != WM_RBUTTONDOWN && Contains(state->quickLayers, pt))
                    ModernLayersToggle(GetParent(hwnd));
                else if (state->home)
                {
                    BOOL handled = FALSE;
                    if (state->paletteVisible)
                    {
                        for (int i = 0; i < NUM_COLORS; ++i)
                            if (Contains(state->colorBoxes[i], pt))
                            {
                                COLORREF color = paletteModel.GetColor(i);
                                if (msg == WM_LBUTTONDBLCLK)
                                {
                                    if (mainWindow.ChooseColor(&color))
                                        paletteModel.SetColor(i, color);
                                }
                                if (msg == WM_RBUTTONDOWN)
                                    paletteModel.SetBgColor(color);
                                else
                                    paletteModel.SetFgColor(color);
                                handled = TRUE;
                                break;
                            }
                        if (!handled && Contains(state->editColors, pt) && msg != WM_RBUTTONDOWN)
                        {
                            Execute(hwnd, IDM_COLORSEDITPALETTE);
                            handled = TRUE;
                        }
                    }
                    if (!handled && msg != WM_RBUTTONDOWN)
                        for (int i = 0; i < HOME_ACTIONS; ++i)
                            if (kActions[i].visible && Contains(kActions[i].rc, pt))
                            {
                                Execute(hwnd, kActions[i].command);
                                break;
                            }
                }
                else if (msg != WM_RBUTTONDOWN)
                {
                    for (int i = HOME_ACTIONS; i < ACTION_COUNT; ++i)
                        if (kActions[i].visible && Contains(kActions[i].rc, pt))
                        { Execute(hwnd, kActions[i].command); break; }
                }
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }
            case WM_MOUSEMOVE:
            {
                if (!state) return 0;
                POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
                int hover = -1;
                for (int i = state->home ? 0 : HOME_ACTIONS;
                     i < (state->home ? (INT)HOME_ACTIONS : (INT)ACTION_COUNT); ++i)
                    if (kActions[i].visible && Contains(kActions[i].rc, pt))
                        { hover = i; break; }
                if (hover != state->hover)
                {
                    state->hover = hover;
                    InvalidateRect(hwnd, NULL, FALSE);
                }
                TRACKMOUSEEVENT track = { sizeof(track), TME_LEAVE, hwnd, 0 };
                TrackMouseEvent(&track);
                return 0;
            }
            case WM_MOUSELEAVE:
                if (state) { state->hover = -1; InvalidateRect(hwnd, NULL, FALSE); }
                return 0;
            case WM_NCDESTROY:
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
                if (state)
                {
                    if (state->icons) ImageList_Destroy(state->icons);
                    if (state->wandIcon) DestroyIcon(state->wandIcon);
                    delete state;
                }
                break;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

INT ModernPaintRibbonHeight(INT clientWidth)
{
    (void)clientWidth;
    return kHeight;
}

HWND ModernPaintCreateRibbon(HWND parent)
{
    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc = RibbonProc;
    wc.hInstance = g_hinstExe;
    wc.lpszClassName = kRibbonClass;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    wc.style = CS_DBLCLKS;
    if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return NULL;
    return CreateWindowExW(0, kRibbonClass, NULL, WS_CHILD | WS_VISIBLE,
                           0, 0, 0, 0, parent, NULL, g_hinstExe, NULL);
}
