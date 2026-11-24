/*
 * LiberNT Calculator modern standard-mode UI.
 * Presentation only: arithmetic and keyboard commands remain in winmain.c.
 * LGPL-2.1-or-later, consistent with the existing calculator.
 */
#include "calc.h"
#include "modern_ui.h"
#include "resource.h"
#include <tchar.h>
#include <strsafe.h>

#define UI_SCI_BASE 30220
#define UI_SCI_ANGLE 30221
#define UI_SCI_INV 30222
#define UI_SCI_HYP 30223
#define UI_HISTORY_BUTTON 30110
#define UI_HISTORY_CLEAR 30111
#define UI_HISTORY_LIST 30112
#define UI_HISTORY_READY (WM_APP + 117)
#define UI_BG RGB(31,31,31)
#define UI_BUTTON RGB(57,57,57)
#define UI_NUMBER RGB(63,63,63)
#define UI_HOVER RGB(78,78,78)
#define UI_PRESSED RGB(91,91,91)
#define UI_ACCENT RGB(117,177,225)
#define UI_TEXT RGB(250,250,250)

static HFONT uiFont, uiDisplayFont, uiAccentFont;
static HBRUSH uiBackground;
static BOOL uiResizing;
static HWND uiExpression;
static HWND uiHistoryButton, uiHistoryPopup;
static TCHAR uiHistoryEntries[64][768];
static int uiHistoryCount;
static BOOL uiHistoryOpen;
static HFONT uiExpressionFont;
static HBRUSH uiControlBrush;
static HBRUSH uiHistoryBrush;
static int uiMode;
static int uiOriginalW, uiOriginalH;
static RECT uiOriginalRects[256];
static HWND uiOriginalControls[256];
static int uiOriginalCount;
static int uiHistoryWidth;
static HWND uiSciBase, uiSciAngle, uiSciInv, uiSciHyp;
static LRESULT CALLBACK UiHistoryProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
static void UiPositionHistory(HWND hwnd);


static const UINT uiGrid[6][4] = {
    {IDC_BUTTON_PERCENT, IDC_BUTTON_CE, IDC_BUTTON_CANC, IDC_BUTTON_BACK},
    {IDC_BUTTON_RX, IDC_BUTTON_Xe2, IDC_BUTTON_SQRT, IDC_BUTTON_DIV},
    {IDC_BUTTON_7, IDC_BUTTON_8, IDC_BUTTON_9, IDC_BUTTON_MULT},
    {IDC_BUTTON_4, IDC_BUTTON_5, IDC_BUTTON_6, IDC_BUTTON_SUB},
    {IDC_BUTTON_1, IDC_BUTTON_2, IDC_BUTTON_3, IDC_BUTTON_ADD},
    {IDC_BUTTON_SIGN, IDC_BUTTON_0, IDC_BUTTON_DOT, IDC_BUTTON_EQU}
};
static const UINT uiMemory[4] = {
    IDC_BUTTON_MC, IDC_BUTTON_MR, IDC_BUTTON_MS, IDC_BUTTON_MP
};

static BOOL UiIsNumber(UINT id)
{
    switch (id) {
    case IDC_BUTTON_0: case IDC_BUTTON_1: case IDC_BUTTON_2:
    case IDC_BUTTON_3: case IDC_BUTTON_4: case IDC_BUTTON_5:
    case IDC_BUTTON_6: case IDC_BUTTON_7: case IDC_BUTTON_8:
    case IDC_BUTTON_9: case IDC_BUTTON_DOT: case IDC_BUTTON_SIGN:
        return TRUE;
    default:
        return FALSE;
    }
}

static HFONT UiMakeFont(int points, int weight)
{
    HDC dc = GetDC(NULL);
    LOGFONT lf;
    int dpi = dc ? GetDeviceCaps(dc, LOGPIXELSY) : 96;
    if (dc) ReleaseDC(NULL, dc);
    ZeroMemory(&lf, sizeof(lf));
    lf.lfHeight = -MulDiv(points, dpi, 72);
    lf.lfWeight = weight;
    lf.lfCharSet = DEFAULT_CHARSET;
    _tcscpy(lf.lfFaceName, TEXT("Segoe UI"));
    return CreateFontIndirect(&lf);
}

/* The original radio/checkbox controls remain in the dialog and continue to
 * receive the exact commands expected by the calculator engine. The modern
 * toolbar only provides a more usable front end to those controls. */
static void UiScientificSync(HWND hwnd)
{
    TCHAR caption[80];
    UINT base = calc.base, angle = calc.degr;
    LPCTSTR baseText = base == IDC_RADIO_HEX ? TEXT("Hex") :
        base == IDC_RADIO_OCT ? TEXT("Oct") :
        base == IDC_RADIO_BIN ? TEXT("Bin") : TEXT("Dec");
    LPCTSTR angleText = angle == IDC_RADIO_RAD ? TEXT("Radianes") :
        angle == IDC_RADIO_GRAD ? TEXT("Gradianes") : TEXT("Grados");
    if (uiSciBase) {
        StringCchPrintf(caption, SIZEOF(caption), TEXT("Base: %s  \x25BE"), baseText);
        SetWindowText(uiSciBase, caption);
    }
    if (uiSciAngle) {
        StringCchPrintf(caption, SIZEOF(caption), TEXT("%s  \x25BE"), angleText);
        SetWindowText(uiSciAngle, caption);
    }
    if (uiSciInv) {
        SetWindowText(uiSciInv, SendMessage(GetDlgItem(hwnd, IDC_CHECK_INV), BM_GETCHECK, 0, 0) == BST_CHECKED ?
            TEXT("Inv: Sí") : TEXT("Inv: No"));
    }
    if (uiSciHyp) {
        SetWindowText(uiSciHyp, SendMessage(GetDlgItem(hwnd, IDC_CHECK_HYP), BM_GETCHECK, 0, 0) == BST_CHECKED ?
            TEXT("Hyp: Sí") : TEXT("Hyp: No"));
    }
}

static void UiScientificMenu(HWND hwnd, HWND trigger, BOOL angle)
{
    HMENU menu = CreatePopupMenu();
    RECT rect;
    UINT chosen;
    if (!menu) return;
    if (angle) {
        AppendMenu(menu, MF_STRING, IDC_RADIO_DEG, TEXT("Grados"));
        AppendMenu(menu, MF_STRING, IDC_RADIO_RAD, TEXT("Radianes"));
        AppendMenu(menu, MF_STRING, IDC_RADIO_GRAD, TEXT("Gradianes"));
        CheckMenuRadioItem(menu, IDC_RADIO_DEG, IDC_RADIO_GRAD, calc.degr, MF_BYCOMMAND);
    } else {
        AppendMenu(menu, MF_STRING, IDC_RADIO_HEX, TEXT("Hexadecimal"));
        AppendMenu(menu, MF_STRING, IDC_RADIO_DEC, TEXT("Decimal"));
        AppendMenu(menu, MF_STRING, IDC_RADIO_OCT, TEXT("Octal"));
        AppendMenu(menu, MF_STRING, IDC_RADIO_BIN, TEXT("Binario"));
        CheckMenuRadioItem(menu, IDC_RADIO_HEX, IDC_RADIO_BIN, calc.base, MF_BYCOMMAND);
    }
    GetWindowRect(trigger, &rect);
    chosen = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN,
                            rect.left, rect.bottom, 0, hwnd, NULL);
    DestroyMenu(menu);
    if (chosen) {
        SendMessage(hwnd, WM_COMMAND, MAKEWPARAM(chosen, BN_CLICKED),
                    (LPARAM)GetDlgItem(hwnd, chosen));
        UiScientificSync(hwnd);
    }
}

BOOL ModernCalcScientificCommand(HWND hwnd, UINT id)
{
    if (uiMode != CALC_LAYOUT_SCIENTIFIC + 1) return FALSE;
    switch (id) {
    case UI_SCI_BASE: UiScientificMenu(hwnd, uiSciBase, FALSE); return TRUE;
    case UI_SCI_ANGLE: UiScientificMenu(hwnd, uiSciAngle, TRUE); return TRUE;
    case UI_SCI_INV:
        CheckDlgButton(hwnd, IDC_CHECK_INV,
            IsDlgButtonChecked(hwnd, IDC_CHECK_INV) == BST_CHECKED ? BST_UNCHECKED : BST_CHECKED);
        SendMessage(hwnd, WM_COMMAND, MAKEWPARAM(IDC_CHECK_INV, BN_CLICKED),
            (LPARAM)GetDlgItem(hwnd, IDC_CHECK_INV));
        UiScientificSync(hwnd); return TRUE;
    case UI_SCI_HYP:
        CheckDlgButton(hwnd, IDC_CHECK_HYP,
            IsDlgButtonChecked(hwnd, IDC_CHECK_HYP) == BST_CHECKED ? BST_UNCHECKED : BST_CHECKED);
        SendMessage(hwnd, WM_COMMAND, MAKEWPARAM(IDC_CHECK_HYP, BN_CLICKED),
            (LPARAM)GetDlgItem(hwnd, IDC_CHECK_HYP));
        UiScientificSync(hwnd); return TRUE;
    default: return FALSE;
    }
}

void ModernCalcScientificRefresh(HWND hwnd)
{
    if (uiMode == CALC_LAYOUT_SCIENTIFIC + 1) UiScientificSync(hwnd);
}

void ModernCalcLayout(HWND hwnd)
{
    RECT client;
    int w, h, margin = 12, gap = 4;
    int top, gridHeight, cellW, cellH, row, col;
    HDWP hdwp;
    HWND control;

    if (uiResizing || !GetClientRect(hwnd, &client)) return;
    w = client.right - client.left;
    h = client.bottom - client.top;
    if (uiMode != 0) {
        /* Scientific controls are laid out once, from their actual child
         * windows. No second standard-mode grid is placed over them. */
        int i, j, count = 0, gridTop = 190;
        HWND buttons[256], swap;
        RECT positions[256], tmp;
        BOOL scientific = uiMode == CALC_LAYOUT_SCIENTIFIC + 1;
        int gridLeft = scientific ? margin : w * 43 / 100;
        int gridRight = w - margin;
        int actionTop = 119;
        if (w < (scientific ? 690 : 620) || h < 420) return;
        hdwp = BeginDeferWindowPos(uiOriginalCount + 8);
        if (!hdwp) return;
#define UI_OTHER_MOVE(id, xx, yy, ww, hh) do { \
    control = GetDlgItem(hwnd, (id)); \
    if (control && hdwp) hdwp = DeferWindowPos(hdwp, control, NULL, \
        (xx), (yy), (ww), (hh), SWP_NOZORDER | SWP_NOACTIVATE); \
} while (0)
        UI_OTHER_MOVE(IDC_TEXT_OUTPUT, margin, 120, w - 2*margin, 52);
        if (scientific) {
            for (i = 0; i < uiOriginalCount; ++i) {
                HWND child = uiOriginalControls[i];
                RECT r = uiOriginalRects[i];
                TCHAR cls[20];
                LONG_PTR bs;
                int id = GetDlgCtrlID(child);
                GetClassName(child, cls, 20);
                if (lstrcmpi(cls, TEXT("Button"))) continue;
                if (id == IDC_BUTTON_FOCUS || !(GetWindowLongPtr(child,GWL_STYLE)&WS_VISIBLE)) continue;
                bs = GetWindowLongPtr(child,GWL_STYLE);
                if ((bs & BS_TYPEMASK) != BS_OWNERDRAW) {
                    /* Radio buttons and checkboxes stay in the toolbar. */
                    continue;
                }
                buttons[count] = child;
                positions[count++] = r;
            }
            /* Preserve the original reading order, top-to-bottom then left-to-right. */
            for (i=0;i<count;i++) for(j=i+1;j<count;j++) {
                if (positions[j].top < positions[i].top - 3 ||
                    (abs(positions[j].top-positions[i].top)<=3 && positions[j].left<positions[i].left)) {
                    swap=buttons[i];buttons[i]=buttons[j];buttons[j]=swap;
                    tmp=positions[i];positions[i]=positions[j];positions[j]=tmp;
                }
            }
            /* Scale the original scientific keypad geometry as one group.
             * This preserves the numerical keypad, operator and scientific
             * function columns rather than flattening them into a new order. */
            if (count) {
                int minX=positions[0].left, minY=positions[0].top;
                int maxX=positions[0].right, maxY=positions[0].bottom;
                for (i=1;i<count;i++) {
                    if (positions[i].left < minX) minX=positions[i].left;
                    if (positions[i].top < minY) minY=positions[i].top;
                    if (positions[i].right > maxX) maxX=positions[i].right;
                    if (positions[i].bottom > maxY) maxY=positions[i].bottom;
                }
                for (i=0;i<count;i++) {
                    RECT r=positions[i];
                    int spanX=maxX-minX, spanY=maxY-minY;
                    int x=margin+(r.left-minX)*(w-2*margin)/(spanX?spanX:1);
                    int y=gridTop+(r.top-minY)*(h-gridTop-margin)/(spanY?spanY:1);
                    int bw=(r.right-r.left)*(w-2*margin)/(spanX?spanX:1)-gap;
                    int bh=(r.bottom-r.top)*(h-gridTop-margin)/(spanY?spanY:1)-gap;
                    if (bw<24) bw=24;
                    if (bh<24) bh=24;
                    if (hdwp) hdwp=DeferWindowPos(hdwp,buttons[i],NULL,x,y,bw,bh,
                        SWP_NOZORDER|SWP_NOACTIVATE);
                }
            }
            /* Only the new toolbar is visible. Original radio/check controls
             * are retained, hidden, and receive their normal WM_COMMANDs. */
            if (uiSciBase && hdwp) hdwp = DeferWindowPos(hdwp, uiSciBase, NULL,
                margin, 12, (w-2*margin-3*gap)/4, 38, SWP_NOZORDER|SWP_NOACTIVATE);
            if (uiSciAngle && hdwp) hdwp = DeferWindowPos(hdwp, uiSciAngle, NULL,
                margin+(w-2*margin-3*gap)/4+gap, 12, (w-2*margin-3*gap)/4, 38, SWP_NOZORDER|SWP_NOACTIVATE);
            if (uiSciInv && hdwp) hdwp = DeferWindowPos(hdwp, uiSciInv, NULL,
                margin+2*((w-2*margin-3*gap)/4+gap), 12, (w-2*margin-3*gap)/4, 38, SWP_NOZORDER|SWP_NOACTIVATE);
            if (uiSciHyp && hdwp) hdwp = DeferWindowPos(hdwp, uiSciHyp, NULL,
                margin+3*((w-2*margin-3*gap)/4+gap), 12, (w-2*margin-3*gap)/4, 38, SWP_NOZORDER|SWP_NOACTIVATE);

        } else {
            int leftW = gridLeft - 2 * margin - 8;
            int gx, gy, gw, gh, rows=4, columns=5;
            int gridTop=176, gridBottom=h-margin;
            static const UINT conversionKeys[4][5] = {
                {IDC_BUTTON_7,IDC_BUTTON_8,IDC_BUTTON_9,IDC_BUTTON_DIV,IDC_BUTTON_SQRT},
                {IDC_BUTTON_4,IDC_BUTTON_5,IDC_BUTTON_6,IDC_BUTTON_MULT,IDC_BUTTON_PERCENT},
                {IDC_BUTTON_1,IDC_BUTTON_2,IDC_BUTTON_3,IDC_BUTTON_SUB,IDC_BUTTON_RX},
                {IDC_BUTTON_0,IDC_BUTTON_SIGN,IDC_BUTTON_DOT,IDC_BUTTON_ADD,IDC_BUTTON_EQU}
            };
            gw=(gridRight-gridLeft-(columns-1)*gap)/columns;
            gh=(gridBottom-gridTop-(rows-1)*gap)/rows;
            /* Conversion inputs get a dedicated left pane. Combo-box
             * dropdown height must remain generous for the popup list. */
            UI_OTHER_MOVE(IDC_COMBO_CATEGORY, margin, 195, leftW, 180);
            UI_OTHER_MOVE(IDC_COMBO_FROM, margin, 285, leftW, 180);
            UI_OTHER_MOVE(IDC_COMBO_TO, margin, 375, leftW, 180);
            UI_OTHER_MOVE(IDC_BUTTON_CONVERT, margin, h - 65, leftW, 48);
            /* Text labels are positioned from their original vertical order.
             * Their shared IDC_STATIC identifier prevents GetDlgItem use. */
            for (i = 0; i < uiOriginalCount; ++i) {
                HWND child = uiOriginalControls[i];
                TCHAR cls[16];
                RECT *r = &uiOriginalRects[i];
                GetClassName(child, cls, 16);
                if (lstrcmpi(cls, TEXT("Static")) == 0 &&
                    GetDlgCtrlID(child) == IDC_STATIC) {
                    int labelY = (r->top < uiOriginalH * 44 / 130) ? 169 :
                                 (r->top < uiOriginalH * 70 / 130) ? 259 : 349;
                    if (hdwp) hdwp = DeferWindowPos(hdwp, child, NULL,
                        margin, labelY, leftW, 22, SWP_NOZORDER | SWP_NOACTIVATE);
                }
            }
            /* Any remaining conversion-specific buttons are placed in the
             * left pane, rather than left at their original dialog positions. */
            for (i = 0; i < uiOriginalCount; ++i) {
                HWND child = uiOriginalControls[i];
                TCHAR cls[20];
                RECT r = uiOriginalRects[i];
                int id = GetDlgCtrlID(child), k, used = 0;
                if (id == IDC_BUTTON_FOCUS || id == IDC_TEXT_OUTPUT) continue;
                GetClassName(child, cls, 20);
                if (lstrcmpi(cls, TEXT("Button"))) continue;
                for (k = 0; k < 4; ++k)
                    if (uiMemory[k] == (UINT)id) used = 1;
                for (gy = 0; gy < 4; ++gy)
                    for (gx = 0; gx < 5; ++gx)
                        if (conversionKeys[gy][gx] == (UINT)id) used = 1;
                if (id == IDC_BUTTON_BACK || id == IDC_BUTTON_CE || id == IDC_BUTTON_CANC ||
                    id == IDC_BUTTON_CONVERT) used = 1;
                if (!used && (GetWindowLongPtr(child,GWL_STYLE) & WS_VISIBLE) && hdwp)
                    hdwp = DeferWindowPos(hdwp, child, NULL,
                    margin + (r.left * leftW) / (uiOriginalW ? uiOriginalW : 1),
                    110 + (r.top * (h - 250)) / (uiOriginalH ? uiOriginalH : 1),
                    (r.right-r.left) * leftW / (uiOriginalW ? uiOriginalW : 1),
                    28, SWP_NOZORDER | SWP_NOACTIVATE);
            }
            UI_OTHER_MOVE(IDC_BUTTON_BACK, gridLeft, actionTop, gw, 42);
            UI_OTHER_MOVE(IDC_BUTTON_CE, gridLeft + gw + gap, actionTop, gw, 42);
            UI_OTHER_MOVE(IDC_BUTTON_CANC, gridLeft + 2*(gw+gap), actionTop, gw, 42);
            for (gx = 0; gx < 4; ++gx)
                UI_OTHER_MOVE(uiMemory[gx], gridLeft + gx*(gw+gap), 116-42, gw, 38);
            for (gy = 0; gy < rows; ++gy)
                for (gx = 0; gx < columns; ++gx)
                    UI_OTHER_MOVE(conversionKeys[gy][gx],
                        gridLeft + gx*(gw+gap), gridTop + gy*(gh+gap), gw, gh);
        }
        if (hdwp) EndDeferWindowPos(hdwp);
#undef UI_OTHER_MOVE
        if (uiExpression) SetWindowPos(uiExpression, NULL, margin, 40,
                                       w - 2 * margin, 26, SWP_NOZORDER | SWP_NOACTIVATE);
        return;
    }
    if (w < 280 || h < 380) return;

    top = 172;
    gridHeight = h - top - margin;
    cellW = (w - margin * 2 - gap * 3) / 4;
    cellH = (gridHeight - gap * 5) / 6;
    hdwp = BeginDeferWindowPos(30);
    if (!hdwp) return;

#define UI_MOVE(id, xx, yy, ww, hh) do { \
    control = GetDlgItem(hwnd, (id)); \
    if (control && hdwp) hdwp = DeferWindowPos(hdwp, control, NULL, \
        (xx), (yy), (ww), (hh), SWP_NOZORDER | SWP_NOACTIVATE); \
} while (0)

    UI_MOVE(IDC_TEXT_OUTPUT, margin, 72, w - margin * 2, 58);
    if (uiExpression) SetWindowPos(uiExpression, NULL, margin, 40,
                                  w - margin * 2 - 42, 30, SWP_NOZORDER | SWP_NOACTIVATE);
    if (uiHistoryButton) SetWindowPos(uiHistoryButton,NULL,w-margin-36,12,36,30,SWP_NOZORDER|SWP_NOACTIVATE);
    uiHistoryWidth = (w > 540) ? 300 : (w > 310 ? w - 24 : w - 12);
    UiPositionHistory(hwnd);
    for (col = 0; col < 4; ++col)
        UI_MOVE(uiMemory[col], margin + col * (cellW + gap),
                134, cellW, 32);
    for (row = 0; row < 6; ++row)
        for (col = 0; col < 4; ++col)
            UI_MOVE(uiGrid[row][col], margin + col * (cellW + gap),
                    top + row * (cellH + gap), cellW, cellH);
#undef UI_MOVE
    if (hdwp) EndDeferWindowPos(hdwp);
}

void ModernCalcInit(HWND hwnd)
{
    LONG_PTR style;
    RECT windowRect = {0,0,344,550};
    HWND button;
    int i;

    ModernCalcCleanup();
    uiMode = 0;
    uiBackground = CreateSolidBrush(UI_BG);
    uiFont = UiMakeFont(13, FW_NORMAL);
    uiAccentFont = UiMakeFont(13, FW_BOLD);
    uiDisplayFont = UiMakeFont(30, FW_SEMIBOLD);
    uiExpressionFont = UiMakeFont(12, FW_NORMAL);
    uiControlBrush = CreateSolidBrush(UI_BUTTON);
    uiHistoryBrush = CreateSolidBrush(RGB(39,39,39));
    uiHistoryOpen = FALSE;
    uiHistoryCount = 0;
    uiHistoryButton = CreateWindowEx(0,TEXT("BUTTON"),TEXT("Historial"),
        WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,0,0,80,28,hwnd,(HMENU)(UINT_PTR)UI_HISTORY_BUTTON,
        (HINSTANCE)GetWindowLongPtr(hwnd,GWLP_HINSTANCE),NULL);
    {
        WNDCLASS wc;
        ZeroMemory(&wc, sizeof(wc));
        wc.lpfnWndProc = UiHistoryProc;
        wc.hInstance = (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE);
        wc.lpszClassName = TEXT("LiberNTCalcHistoryPanel");
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = uiHistoryBrush;
        RegisterClass(&wc);
        uiHistoryPopup = CreateWindowEx(WS_EX_TOOLWINDOW, wc.lpszClassName,
            TEXT(""), WS_POPUP | WS_BORDER, 0, 0, 300, 300,
            hwnd, NULL, wc.hInstance, NULL);
    }
    if (uiFont && uiHistoryButton)
        SendMessage(uiHistoryButton, WM_SETFONT, (WPARAM)uiFont, TRUE);
    uiExpression = CreateWindowEx(0, TEXT("STATIC"), TEXT(""),
        WS_CHILD | WS_VISIBLE | SS_RIGHT | SS_CENTERIMAGE,
        0, 0, 100, 30, hwnd, NULL,
        (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), NULL);
    if (uiExpression && uiExpressionFont)
        SendMessage(uiExpression, WM_SETFONT, (WPARAM)uiExpressionFont, TRUE);

    button = CreateWindowEx(0, TEXT("BUTTON"), TEXT("x²"),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW | BS_NOTIFY,
        0,0,20,20, hwnd, (HMENU)(UINT_PTR)IDC_BUTTON_Xe2,
        (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), NULL);
    if (button && uiFont) SendMessage(button, WM_SETFONT, (WPARAM)uiFont, TRUE);

    for (i = 0; i < 4; ++i) {
        button = GetDlgItem(hwnd, uiMemory[i]);
        if (button && uiFont) SendMessage(button, WM_SETFONT, (WPARAM)uiFont, TRUE);
    }
    for (i = 0; i < 24; ++i) {
        button = GetDlgItem(hwnd, uiGrid[i/4][i%4]);
        if (button && uiFont) SendMessage(button, WM_SETFONT, (WPARAM)uiFont, TRUE);
    }
    button = GetDlgItem(hwnd, IDC_TEXT_OUTPUT);
    if (button) {
        if (uiDisplayFont) SendMessage(button, WM_SETFONT, (WPARAM)uiDisplayFont, TRUE);
        SetWindowLongPtr(button, GWL_EXSTYLE,
            GetWindowLongPtr(button, GWL_EXSTYLE) & ~WS_EX_CLIENTEDGE);
    }
    ShowWindow(GetDlgItem(hwnd, IDC_TEXT_MEMORY), SW_HIDE);

    SetDlgItemText(hwnd, IDC_BUTTON_BACK, TEXT("Back"));
    SetDlgItemText(hwnd, IDC_BUTTON_SQRT, TEXT("√x"));
    SetDlgItemText(hwnd, IDC_BUTTON_RX, TEXT("1/x"));
    SetDlgItemText(hwnd, IDC_BUTTON_MULT, TEXT("×"));
    SetDlgItemText(hwnd, IDC_BUTTON_DIV, TEXT("÷"));
    SetDlgItemText(hwnd, IDC_BUTTON_SIGN, TEXT("±"));

    style = GetWindowLongPtr(hwnd, GWL_STYLE);
    SetWindowLongPtr(hwnd, GWL_STYLE, style | WS_THICKFRAME | WS_MAXIMIZEBOX);
    AdjustWindowRectEx(&windowRect, (DWORD)(style | WS_THICKFRAME | WS_MAXIMIZEBOX),
                       GetMenu(hwnd) != NULL, (DWORD)GetWindowLongPtr(hwnd, GWL_EXSTYLE));
    uiResizing = TRUE;
    SetWindowPos(hwnd, NULL, 0, 0, windowRect.right - windowRect.left,
                 windowRect.bottom - windowRect.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    uiResizing = FALSE;
    ModernCalcLayout(hwnd);
    InvalidateRect(hwnd, NULL, TRUE);
}

void ModernCalcCleanup(void)
{
    uiExpression = NULL;
    if (uiHistoryPopup && IsWindow(uiHistoryPopup)) DestroyWindow(uiHistoryPopup);
    uiHistoryPopup = NULL;
    uiHistoryButton = NULL;
    uiHistoryOpen=FALSE;
    uiOriginalCount = 0;
    uiSciBase = uiSciAngle = uiSciInv = uiSciHyp = NULL;
    uiMode = 0;
    if (uiExpressionFont) { DeleteObject(uiExpressionFont); uiExpressionFont = NULL; }
    if (uiControlBrush) { DeleteObject(uiControlBrush); uiControlBrush = NULL; }
    if (uiHistoryBrush) { DeleteObject(uiHistoryBrush); uiHistoryBrush = NULL; }
    if (uiFont) { DeleteObject(uiFont); uiFont = NULL; }
    if (uiAccentFont) { DeleteObject(uiAccentFont); uiAccentFont = NULL; }
    if (uiDisplayFont) { DeleteObject(uiDisplayFont); uiDisplayFont = NULL; }
    if (uiBackground) { DeleteObject(uiBackground); uiBackground = NULL; }
}

INT_PTR ModernCalcErase(HDC dc, HWND hwnd)
{
    RECT rc;
    GetClientRect(hwnd, &rc);
    FillRect(dc, &rc, uiBackground ? uiBackground : (HBRUSH)GetStockObject(BLACK_BRUSH));
    return TRUE;
}

INT_PTR ModernCalcStaticColor(HDC dc, HWND control, HWND hwnd)
{
    (void)hwnd;
    if (uiMode && control != GetDlgItem(hwnd, IDC_TEXT_OUTPUT) &&
        control != uiExpression) {
        SetBkColor(dc, UI_BUTTON);
        SetTextColor(dc, UI_TEXT);
        return (INT_PTR)(uiControlBrush ? uiControlBrush : GetStockObject(BLACK_BRUSH));
    }
    SetBkColor(dc, UI_BG);
    SetTextColor(dc, UI_TEXT);
    SetBkMode(dc, TRANSPARENT);
    return (INT_PTR)(uiBackground ? uiBackground : GetStockObject(BLACK_BRUSH));
}

INT_PTR ModernCalcDrawButton(const DRAWITEMSTRUCT *dis)
{
    HDC memory;
    HBITMAP bitmap, previousBitmap;
    HBRUSH brush, oldBrush;
    HPEN pen, oldPen;
    RECT rc, textRect;
    TCHAR label[64];
    COLORREF color;
    int width, height, radius = 10;
    HFONT oldFont;

    if (!dis || dis->CtlType != ODT_BUTTON) return FALSE;
    width = dis->rcItem.right - dis->rcItem.left;
    height = dis->rcItem.bottom - dis->rcItem.top;
    if (width <= 0 || height <= 0) return TRUE;

    memory = CreateCompatibleDC(dis->hDC);
    if (!memory) return FALSE;
    bitmap = CreateCompatibleBitmap(dis->hDC, width, height);
    if (!bitmap) { DeleteDC(memory); return FALSE; }
    previousBitmap = (HBITMAP)SelectObject(memory, bitmap);
    rc.left = rc.top = 0;
    rc.right = width; rc.bottom = height;
    FillRect(memory, &rc, uiBackground ? uiBackground : (HBRUSH)GetStockObject(BLACK_BRUSH));

    {
        POINT pt;
        RECT screen;
        BOOL hovered;
        BOOL pressed;
        BOOL accent = dis->CtlID == IDC_BUTTON_EQU;
        GetCursorPos(&pt);
        GetWindowRect(dis->hwndItem, &screen);
        hovered = PtInRect(&screen, pt);
        /* ODS_SELECTED may remain set due to keyboard focus or Wine's
         * owner-draw behavior. Only draw a pressed state during an actual
         * left-button press over the control. */
        pressed = hovered && (GetKeyState(VK_LBUTTON) & 0x8000) != 0;
        if (accent)
            color = pressed ? RGB(72, 137, 190) :
                    hovered ? RGB(146, 197, 237) : UI_ACCENT;
        else
            color = pressed ? UI_PRESSED :
                    hovered ? UI_HOVER :
                    UiIsNumber(dis->CtlID) ? UI_NUMBER : UI_BUTTON;
    }
    brush = CreateSolidBrush(color);
    pen = CreatePen(PS_SOLID, 1, color);
    oldBrush = brush ? (HBRUSH)SelectObject(memory, brush) : NULL;
    oldPen = pen ? (HPEN)SelectObject(memory, pen) : NULL;
    if (brush && pen)
        RoundRect(memory, 1, 1, width - 1, height - 1, radius, radius);
    if (oldPen) SelectObject(memory, oldPen);
    if (oldBrush) SelectObject(memory, oldBrush);
    if (pen) DeleteObject(pen);
    if (brush) DeleteObject(brush);

    if (dis->CtlID == UI_HISTORY_BUTTON) {
        HPEN clockPen = CreatePen(PS_SOLID,2,UI_TEXT);
        HPEN prevPen = (HPEN)SelectObject(memory,clockPen);
        int cx=width/2, cy=height/2;
        SelectObject(memory,GetStockObject(NULL_BRUSH));
        Ellipse(memory,cx-9,cy-9,cx+9,cy+9);
        MoveToEx(memory,cx,cy-6,NULL);
        LineTo(memory,cx,cy);
        LineTo(memory,cx+5,cy+3);
        SelectObject(memory,prevPen);
        DeleteObject(clockPen);
        BitBlt(dis->hDC,dis->rcItem.left,dis->rcItem.top,width,height,memory,0,0,SRCCOPY);
        SelectObject(memory,previousBitmap);
        DeleteObject(bitmap);
        DeleteDC(memory);
        return TRUE;
    }
    GetWindowText(dis->hwndItem, label, sizeof(label)/sizeof(label[0]));
    textRect = rc;
    oldFont = (dis->CtlID == IDC_BUTTON_EQU && uiAccentFont) ?
        (HFONT)SelectObject(memory, uiAccentFont) :
        (uiFont ? (HFONT)SelectObject(memory, uiFont) : NULL);
    SetBkMode(memory, TRANSPARENT);
    SetTextColor(memory, (dis->CtlID == IDC_BUTTON_EQU) ? RGB(10,20,28) : UI_TEXT);
    DrawText(memory, label, -1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    if (oldFont) SelectObject(memory, oldFont);
    BitBlt(dis->hDC, dis->rcItem.left, dis->rcItem.top, width, height,
           memory, 0, 0, SRCCOPY);
    SelectObject(memory, previousBitmap);
    DeleteObject(bitmap);
    DeleteDC(memory);
    return TRUE;
}

/* Preserve the existing localized author line and append the translated
 * redesign credit. ShellAbout displays the supplied text on separate lines. */
void ModernCalcAppendCredit(TCHAR *buffer, size_t capacity)
{
    LPCTSTR credit = TEXT("Redesigned by Nyven");
    switch (PRIMARYLANGID(GetUserDefaultUILanguage())) {
    case LANG_SPANISH: credit = TEXT("Rediseñada por Nyven"); break;
    case LANG_PORTUGUESE: credit = TEXT("Redesenhada por Nyven"); break;
    case LANG_FRENCH: credit = TEXT("Redessinée par Nyven"); break;
    case LANG_GERMAN: credit = TEXT("Neugestaltet von Nyven"); break;
    case LANG_ITALIAN: credit = TEXT("Riprogettata da Nyven"); break;
    case LANG_DUTCH: credit = TEXT("Opnieuw ontworpen door Nyven"); break;
    case LANG_POLISH: credit = TEXT("Przeprojektowana przez Nyven"); break;
    case LANG_RUSSIAN: credit = TEXT("Переработано Nyven"); break;
    case LANG_JAPANESE: credit = TEXT("Nyvenによる再設計"); break;
    case LANG_KOREAN: credit = TEXT("Nyven이 재설계"); break;
    case LANG_CHINESE: credit = TEXT("由 Nyven 重新设计"); break;
    case LANG_CZECH: credit = TEXT("Přepracoval Nyven"); break;
    case LANG_HUNGARIAN: credit = TEXT("Újratervezte Nyven"); break;
    case LANG_GREEK: credit = TEXT("Επανασχεδιασμός από Nyven"); break;
    case LANG_HEBREW: credit = TEXT("עיצוב מחדש על ידי Nyven"); break;
    case LANG_BULGARIAN: credit = TEXT("Преработена от Nyven"); break;
    case LANG_INDONESIAN: credit = TEXT("Didesain ulang oleh Nyven"); break;
    default: break;
    }
    StringCchCat(buffer, capacity, TEXT("\n"));
    StringCchCat(buffer, capacity, credit);
}

/* Keep the legacy scientific/conversion controls and their command IDs.
 * Their initial dialog-unit layout is captured before we resize the window. */
void ModernCalcInitOther(HWND hwnd, int mode)
{
    HWND child;
    RECT parent, r;
    LONG_PTR style;
    int count = 0;
    ModernCalcCleanup();
    uiMode = mode + 1;
    uiBackground = CreateSolidBrush(UI_BG);
    uiControlBrush = CreateSolidBrush(UI_BUTTON);
    uiHistoryBrush = CreateSolidBrush(RGB(39,39,39));
    uiFont = UiMakeFont(12, FW_NORMAL);
    uiAccentFont = UiMakeFont(12, FW_BOLD);
    uiDisplayFont = UiMakeFont(26, FW_SEMIBOLD);
    uiExpressionFont = UiMakeFont(12, FW_NORMAL);
    GetClientRect(hwnd, &parent);
    uiOriginalW = parent.right;
    uiOriginalH = parent.bottom;
    for (child = GetWindow(hwnd, GW_CHILD); child && count < 256;
         child = GetWindow(child, GW_HWNDNEXT)) {
        GetWindowRect(child, &r);
        MapWindowPoints(NULL, hwnd, (POINT *)&r, 2);
        uiOriginalControls[count] = child;
        uiOriginalRects[count++] = r;
        {
            TCHAR cls[16];
            GetClassName(child, cls, 16);
            if (lstrcmpi(cls, TEXT("Button")) == 0) {
                LONG_PTR buttonStyle = GetWindowLongPtr(child, GWL_STYLE);
                LONG_PTR buttonType = buttonStyle & BS_TYPEMASK;
                TCHAR caption[4];
                /* Group frames are decorative and overlap the toolbar after
                 * resizing. Keep their radio buttons, not the frames. */
                if (buttonType == BS_GROUPBOX) {
                    ShowWindow(child, SW_HIDE);
                    continue;
                }
                /* The legacy dialogs also have blank pushbutton placeholders.
                 * Do not expose them as empty squares in the modern layout. */
                if ((buttonType == BS_PUSHBUTTON || buttonType == BS_DEFPUSHBUTTON ||
                     buttonType == BS_OWNERDRAW) &&
                    GetWindowText(child, caption, SIZEOF(caption)) == 0 &&
                    GetDlgCtrlID(child) != IDC_BUTTON_FOCUS) {
                    ShowWindow(child, SW_HIDE);
                    continue;
                }
                if ((buttonStyle & WS_VISIBLE) && GetDlgCtrlID(child) != IDC_BUTTON_FOCUS &&
                    ((buttonStyle & BS_TYPEMASK) == BS_PUSHBUTTON ||
                     (buttonStyle & BS_TYPEMASK) == BS_DEFPUSHBUTTON ||
                     (buttonStyle & BS_TYPEMASK) == BS_OWNERDRAW)) {
                    SetWindowLongPtr(child, GWL_STYLE,
                        (buttonStyle & ~BS_TYPEMASK) | BS_OWNERDRAW);
                    if (uiFont) SendMessage(child, WM_SETFONT, (WPARAM)uiFont, FALSE);
                }
            }
        }
    }
    /* Legacy memory/parent markers have an empty caption and a raised
     * client edge. They are status placeholders, not interactive buttons. */
    child = GetDlgItem(hwnd, IDC_TEXT_PARENT);
    if (child) ShowWindow(child, SW_HIDE);
    child = GetDlgItem(hwnd, IDC_TEXT_MEMORY);
    if (child) ShowWindow(child, SW_HIDE);
    if (mode == CALC_LAYOUT_SCIENTIFIC) {
        static const UINT hiddenIds[] = {
            IDC_RADIO_HEX, IDC_RADIO_DEC, IDC_RADIO_OCT, IDC_RADIO_BIN,
            IDC_RADIO_DEG, IDC_RADIO_RAD, IDC_RADIO_GRAD,
            IDC_CHECK_INV, IDC_CHECK_HYP
        };
        int n;
        for (n=0; n<(int)(sizeof(hiddenIds)/sizeof(hiddenIds[0])); ++n) {
            child=GetDlgItem(hwnd,hiddenIds[n]);
            if (child) ShowWindow(child,SW_HIDE);
        }
        uiSciBase=CreateWindowEx(0,TEXT("BUTTON"),TEXT("Base: Dec"),
            WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,0,0,80,35,
            hwnd,(HMENU)(UINT_PTR)UI_SCI_BASE,calc.hInstance,NULL);
        uiSciAngle=CreateWindowEx(0,TEXT("BUTTON"),TEXT("Grados"),
            WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,0,0,80,35,
            hwnd,(HMENU)(UINT_PTR)UI_SCI_ANGLE,calc.hInstance,NULL);
        uiSciInv=CreateWindowEx(0,TEXT("BUTTON"),TEXT("Inv: No"),
            WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,0,0,80,35,
            hwnd,(HMENU)(UINT_PTR)UI_SCI_INV,calc.hInstance,NULL);
        uiSciHyp=CreateWindowEx(0,TEXT("BUTTON"),TEXT("Hyp: No"),
            WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,0,0,80,35,
            hwnd,(HMENU)(UINT_PTR)UI_SCI_HYP,calc.hInstance,NULL);
        UiScientificSync(hwnd);
    }
    uiOriginalCount = count;
    child = GetDlgItem(hwnd, IDC_BUTTON_FOCUS);
    if (child) ShowWindow(child, SW_HIDE);
    child = GetDlgItem(hwnd, IDC_TEXT_OUTPUT);
    if (child && uiDisplayFont)
        SendMessage(child, WM_SETFONT, (WPARAM)uiDisplayFont, TRUE);
    style = GetWindowLongPtr(hwnd, GWL_STYLE);
    SetWindowLongPtr(hwnd, GWL_STYLE, style | WS_THICKFRAME | WS_MAXIMIZEBOX);
    uiResizing = TRUE;
    SetWindowPos(hwnd, NULL, 0, 0,
                 mode == CALC_LAYOUT_SCIENTIFIC ? 920 : 800, 610,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    uiResizing = FALSE;
    ModernCalcLayout(hwnd);
    InvalidateRect(hwnd, NULL, TRUE);
}

void ModernCalcSetExpression(HWND hwnd, LPCTSTR value)
{
    (void)hwnd;
    if (uiExpression) SetWindowText(uiExpression, value ? value : TEXT(""));
}

static void UiPositionHistory(HWND hwnd)
{
    RECT rc;
    POINT pt;
    int w, h;
    if (!uiHistoryPopup || !IsWindow(uiHistoryPopup)) return;
    GetClientRect(hwnd, &rc);
    w = rc.right - rc.left;
    h = rc.bottom - rc.top;
    uiHistoryWidth = w > 560 ? 320 : w - 24;
    if (uiHistoryWidth < 180) uiHistoryWidth = 180;
    pt.x = w - uiHistoryWidth - 12;
    pt.y = 48;
    ClientToScreen(hwnd, &pt);
    SetWindowPos(uiHistoryPopup, HWND_TOP, pt.x, pt.y,
        uiHistoryWidth, h > 180 ? h - 65 : 120,
        SWP_NOACTIVATE | (uiHistoryOpen ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
}

static LRESULT CALLBACK UiHistoryProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        RECT rc, item;
        int i, y;
        HFONT oldFont = NULL;
        GetClientRect(hwnd, &rc);
        FillRect(dc, &rc, uiHistoryBrush ? uiHistoryBrush : (HBRUSH)GetStockObject(BLACK_BRUSH));
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, UI_TEXT);
        if (uiFont) oldFont = (HFONT)SelectObject(dc, uiFont);
        item = rc; item.left += 14; item.right -= 14;
        item.top = 14; item.bottom = 42;
        DrawText(dc, TEXT("Historial"), -1, &item, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        y = 52;
        if (!uiHistoryCount) {
            item.top = y; item.bottom = y + 45;
            SetTextColor(dc, RGB(185,185,185));
            DrawText(dc, TEXT("Sin operaciones"), -1, &item, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        }
        for (i = 0; i < uiHistoryCount && y + 42 < rc.bottom - 48; ++i) {
            item.top = y; item.bottom = y + 42;
            SetTextColor(dc, UI_TEXT);
            DrawText(dc, uiHistoryEntries[i], -1, &item,
                DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            y += 48;
        }
        item.left = 1; item.right = rc.right - 1;
        item.top = rc.bottom - 44; item.bottom = rc.bottom - 1;
        FillRect(dc, &item, uiControlBrush ? uiControlBrush : (HBRUSH)GetStockObject(GRAY_BRUSH));
        DrawText(dc, TEXT("Vaciar historial"), -1, &item,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        if (oldFont) SelectObject(dc, oldFont);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_LBUTTONUP:
    {
        RECT rc;
        GetClientRect(hwnd, &rc);
        if (HIWORD(lp) >= rc.bottom - 44) {
            uiHistoryCount = 0;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) {
            uiHistoryOpen = FALSE;
            ShowWindow(hwnd, SW_HIDE);
            return 0;
        }
        break;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

BOOL ModernCalcHistoryCommand(HWND hwnd, UINT id)
{
    if (id == UI_HISTORY_BUTTON) {
        uiHistoryOpen = !uiHistoryOpen;
        UiPositionHistory(hwnd);
        if (uiHistoryPopup) InvalidateRect(uiHistoryPopup, NULL, TRUE);
        return TRUE;
    }
    return FALSE;
}

void ModernCalcHistoryAdd(LPCTSTR expression, LPCTSTR result)
{
    int i;
    if (!expression || !expression[0]) return;
    if (uiHistoryCount == 64) uiHistoryCount = 63;
    for (i = uiHistoryCount; i > 0; --i)
        StringCchCopy(uiHistoryEntries[i], SIZEOF(uiHistoryEntries[i]), uiHistoryEntries[i-1]);
    StringCchPrintf(uiHistoryEntries[0], SIZEOF(uiHistoryEntries[0]),
                    TEXT("%s %s"), expression, result ? result : TEXT(""));
    ++uiHistoryCount;
    if (uiHistoryPopup) InvalidateRect(uiHistoryPopup, NULL, FALSE);
}

INT_PTR ModernCalcHistoryColor(HDC dc)
{
    SetTextColor(dc, UI_TEXT);
    SetBkColor(dc, RGB(39,39,39));
    return (INT_PTR)(uiHistoryBrush ? uiHistoryBrush : GetStockObject(BLACK_BRUSH));
}

/* A dedicated modal dialog avoids ShellAbout truncating the redesign credit. */
static INT_PTR CALLBACK UiAboutProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    static HBRUSH aboutBrush;
    switch (msg) {
    case WM_INITDIALOG:
    {
        LPCTSTR *strings = (LPCTSTR *)lp;
        HWND control;
        aboutBrush = CreateSolidBrush(UI_BG);
        SetWindowText(hwnd, strings[0]);
        control = CreateWindowEx(0, TEXT("STATIC"), strings[0],
            WS_CHILD | WS_VISIBLE | SS_LEFT, 20, 22, 330, 30, hwnd,
            NULL, (HINSTANCE)GetWindowLongPtr(hwnd,GWLP_HINSTANCE), NULL);
        if (control && uiFont) SendMessage(control,WM_SETFONT,(WPARAM)uiFont,TRUE);
        control = CreateWindowEx(0, TEXT("STATIC"), strings[1],
            WS_CHILD | WS_VISIBLE | SS_LEFT, 20, 60, 330, 70, hwnd,
            NULL, (HINSTANCE)GetWindowLongPtr(hwnd,GWLP_HINSTANCE), NULL);
        if (control && uiFont) SendMessage(control,WM_SETFONT,(WPARAM)uiFont,TRUE);
        control = CreateWindowEx(0, TEXT("BUTTON"), TEXT("Aceptar"),
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 250, 140, 100, 34,
            hwnd, (HMENU)(UINT_PTR)IDOK,
            (HINSTANCE)GetWindowLongPtr(hwnd,GWLP_HINSTANCE), NULL);
        if (control && uiFont) SendMessage(control,WM_SETFONT,(WPARAM)uiFont,TRUE);
        return TRUE;
    }
    case WM_ERASEBKGND:
        return ModernCalcErase((HDC)wp, hwnd);
    case WM_CTLCOLORSTATIC:
        SetBkColor((HDC)wp, UI_BG);
        SetTextColor((HDC)wp, UI_TEXT);
        return (INT_PTR)(aboutBrush ? aboutBrush : GetStockObject(BLACK_BRUSH));
    case WM_DRAWITEM:
        return ModernCalcDrawButton((const DRAWITEMSTRUCT *)lp);
    case WM_COMMAND:
        if (LOWORD(wp) == IDOK || LOWORD(wp) == IDCANCEL) {
            EndDialog(hwnd, LOWORD(wp));
            return TRUE;
        }
        break;
    case WM_CLOSE:
        EndDialog(hwnd, IDCANCEL);
        return TRUE;
    case WM_DESTROY:
        if (aboutBrush) { DeleteObject(aboutBrush); aboutBrush = NULL; }
        break;
    }
    return FALSE;
}

void ModernCalcShowAbout(HWND owner, LPCTSTR title, LPCTSTR credit)
{
    /* DLGTEMPLATE with zero menu, class and title; dimensions in dialog units. */
    struct { DLGTEMPLATE dlg; WORD menu, cls, title; } templateData;
    LPCTSTR strings[2];
    ZeroMemory(&templateData, sizeof(templateData));
    templateData.dlg.style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME;
    templateData.dlg.cx = 250;
    templateData.dlg.cy = 130;
    strings[0] = title;
    strings[1] = credit;
    DialogBoxIndirectParam((HINSTANCE)GetWindowLongPtr(owner,GWLP_HINSTANCE),
        &templateData.dlg, owner, UiAboutProc, (LPARAM)strings);
}
