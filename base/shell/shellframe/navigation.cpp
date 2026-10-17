/*
 * PROJECT:     LiberNT Shell Frame
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Back and forward buttons for window captions
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "shellframe.h"

namespace ShellFrame
{

static HIMAGELIST
CreateArrowList(int size, COLORREF color)
{
    static const LPCWSTR Arrows[] = { L"arrow_left_16_regular", L"arrow_right_16_regular" };
    HIMAGELIST list = ImageList_Create(size, size, ILC_COLOR32, (int)_countof(Arrows), 0);

    for (size_t i = 0; list && i < _countof(Arrows); i++)
    {
        HICON icon = CreateGlyphIcon(Arrows[i], size, color);

        ImageList_AddIcon(list, icon);
        if (icon)
            DestroyIcon(icon);
    }
    return list;
}

NavigationButtons::NavigationButtons() :
    m_hwnd(NULL),
    m_Images(NULL),
    m_Disabled(NULL),
    m_BackId(0),
    m_ForwardId(0),
    m_Button(0),
    m_Glyph(0),
    m_PadX(0),
    m_PadY(0),
    m_Radius(0.0f)
{
}

NavigationButtons::~NavigationButtons()
{
    Destroy();
}

BOOL
NavigationButtons::Create(HWND parent, const Theme &theme, UINT backId, UINT forwardId, LPCWSTR backTip,
                          LPCWSTR forwardTip, COLORREF glyph, COLORREF disabled)
{
    TBBUTTON buttons[2] = { };

    m_BackId = backId;
    m_ForwardId = forwardId;
    m_Button = theme.Scale(32);
    m_Glyph = theme.Scale(16);
    m_PadX = theme.Scale(8);
    m_PadY = theme.Scale(6);
    m_Radius = theme.ScaleF(4.0f);
    m_hwnd = CreateWindowExW(0, TOOLBARCLASSNAMEW, L"", WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT | TBSTYLE_TRANSPARENT |
                             TBSTYLE_TOOLTIPS | CCS_NODIVIDER | CCS_NOPARENTALIGN | CCS_NORESIZE, 0, 0, 0, 0, parent, NULL,
                             (HINSTANCE)GetWindowLongPtrW(parent, GWLP_HINSTANCE), NULL);
    if (!m_hwnd)
        return FALSE;
    m_Images = CreateArrowList(m_Glyph, glyph);
    m_Disabled = CreateArrowList(m_Glyph, disabled);
    SendMessageW(m_hwnd, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
    SendMessageW(m_hwnd, TB_SETEXTENDEDSTYLE, 0, TBSTYLE_EX_MIXEDBUTTONS);
    SendMessageW(m_hwnd, TB_SETIMAGELIST, 0, (LPARAM)m_Images);
    SendMessageW(m_hwnd, TB_SETDISABLEDIMAGELIST, 0, (LPARAM)m_Disabled);
    SendMessageW(m_hwnd, TB_SETBUTTONSIZE, 0, MAKELPARAM(m_Button, m_Button));
    buttons[0].iBitmap = 0;
    buttons[0].idCommand = backId;
    buttons[0].fsStyle = BTNS_BUTTON;
    buttons[0].iString = (INT_PTR)backTip;
    buttons[1].iBitmap = 1;
    buttons[1].idCommand = forwardId;
    buttons[1].fsStyle = BTNS_BUTTON;
    buttons[1].iString = (INT_PTR)forwardTip;
    SendMessageW(m_hwnd, TB_ADDBUTTONSW, 2, (LPARAM)buttons);
    SendMessageW(m_hwnd, TB_SETBUTTONSIZE, 0, MAKELPARAM(m_Button, m_Button));
    SendMessageW(m_hwnd, TB_AUTOSIZE, 0, 0);
    return TRUE;
}

void
NavigationButtons::Destroy()
{
    if (m_hwnd)
        DestroyWindow(m_hwnd);
    if (m_Images)
        ImageList_Destroy(m_Images);
    if (m_Disabled)
        ImageList_Destroy(m_Disabled);
    m_hwnd = NULL;
    m_Images = m_Disabled = NULL;
}

void
NavigationButtons::Place(int left, int centerY)
{
    if (!m_hwnd)
        return;
    SetWindowPos(m_hwnd, NULL, left, centerY - m_Button / 2, m_Button * 2, m_Button,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
    InvalidateRect(m_hwnd, NULL, TRUE);
}

void
NavigationButtons::Enable(BOOL back, BOOL forward)
{
    if (!m_hwnd)
        return;
    SendMessageW(m_hwnd, TB_ENABLEBUTTON, m_BackId, MAKELONG(back, 0));
    SendMessageW(m_hwnd, TB_ENABLEBUTTON, m_ForwardId, MAKELONG(forward, 0));
}

BOOL
NavigationButtons::OnNotify(const NMHDR *header, LRESULT *result, COLORREF background)
{
    const NMTBCUSTOMDRAW *draw = (const NMTBCUSTOMDRAW *)header;

    if (!m_hwnd || header->hwndFrom != m_hwnd || header->code != NM_CUSTOMDRAW)
        return FALSE;
    *result = CDRF_DODEFAULT;
    if (draw->nmcd.dwDrawStage == CDDS_PREPAINT)
    {
        *result = CDRF_NOTIFYITEMDRAW;
    }
    else if (draw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT)
    {
        HDC hdc = draw->nmcd.hdc;
        RECT rc = draw->nmcd.rc;
        UINT state = draw->nmcd.uItemState;
        int index = (int)SendMessageW(m_hwnd, TB_COMMANDTOINDEX, draw->nmcd.dwItemSpec, 0);
        int x = (rc.left + rc.right) / 2, y = (rc.top + rc.bottom) / 2;
        HBRUSH brush = CreateSolidBrush(background);

        FillRect(hdc, &rc, brush);
        DeleteObject(brush);
        if (!(state & CDIS_DISABLED) && (state & (CDIS_HOT | CDIS_SELECTED)))
        {
            RECT pill = { x - m_Glyph / 2 - m_PadX, y - m_Glyph / 2 - m_PadY, x + m_Glyph / 2 + m_PadX,
                          y + m_Glyph / 2 + m_PadY };

            FillRoundGlass(hdc, pill, m_Radius, RGB(255, 255, 255), (state & CDIS_SELECTED) ? 26 : 13);
        }
        if (index >= 0)
            ImageList_Draw((state & CDIS_DISABLED) ? m_Disabled : m_Images, index, hdc, x - m_Glyph / 2,
                           y - m_Glyph / 2, ILD_TRANSPARENT);
        *result = CDRF_SKIPDEFAULT;
    }
    return TRUE;
}

}
