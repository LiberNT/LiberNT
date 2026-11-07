/*
 * PROJECT:     LiberNT Win32k
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     DPI virtualization of DPI-unaware windows
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <win32k.h>

BOOL
UserDpiIsVirtualizationActive(VOID)
{
    return gbCompositionEnabled && UserGetSystemDpi() != USER_SYSTEM_DPI_DEFAULT;
}

VOID
UserDpiUpdateVirtualization(VOID)
{
    if (!gpsi)
        return;
    if (UserDpiIsVirtualizationActive())
        gpsi->dwSRVIFlags |= SRVINFO_DPIVIRTUALIZATION;
    else
        gpsi->dwSRVIFlags &= ~SRVINFO_DPIVIRTUALIZATION;
}

ULONG
UserDpiGetThreadContext(_In_opt_ PTHREADINFO pti)
{
    if (pti == NULL)
        return NTUSER_DPI_PER_MONITOR_AWARE;
    if (pti->DpiContext != 0)
        return pti->DpiContext;
    if (pti->ppi != NULL && pti->ppi->DpiContext != 0)
        return pti->ppi->DpiContext;
    return NTUSER_DPI_UNAWARE;
}

BOOL
UserDpiIsVirtualThread(_In_opt_ PTHREADINFO pti)
{
    return pti != NULL &&
           UserDpiIsVirtualizationActive() &&
           NTUSER_DPI_CONTEXT_GET_AWARENESS(UserDpiGetThreadContext(pti)) == DPI_AWARENESS_UNAWARE;
}

BOOL
UserDpiIsVirtualCaller(VOID)
{
    return UserDpiIsVirtualThread(PsGetCurrentThreadWin32Thread());
}

BOOL
UserDpiIsVirtualWindow(_In_opt_ PWND Wnd)
{
    return Wnd != NULL &&
           UserDpiIsVirtualizationActive() &&
           NTUSER_DPI_CONTEXT_GET_AWARENESS(Wnd->DpiContext) == DPI_AWARENESS_UNAWARE;
}

static LONG
UserDpiMulDiv(_In_ LONG Value, _In_ LONG Numerator, _In_ LONG Denominator)
{
    LONGLONG Product = (LONGLONG)Value * Numerator;

    if (Product >= 0)
        return (LONG)((Product + Denominator / 2) / Denominator);
    return -(LONG)((-Product + Denominator / 2) / Denominator);
}

LONG
UserDpiToLogical(_In_ LONG Value)
{
    return UserDpiMulDiv(Value, USER_SYSTEM_DPI_DEFAULT, (LONG)UserGetSystemDpi());
}

LONG
UserDpiToPhysical(_In_ LONG Value)
{
    return UserDpiMulDiv(Value, (LONG)UserGetSystemDpi(), USER_SYSTEM_DPI_DEFAULT);
}

VOID
UserDpiPointToLogical(_Inout_ PPOINT Point)
{
    Point->x = UserDpiToLogical(Point->x);
    Point->y = UserDpiToLogical(Point->y);
}

VOID
UserDpiPointToPhysical(_Inout_ PPOINT Point)
{
    Point->x = UserDpiToPhysical(Point->x);
    Point->y = UserDpiToPhysical(Point->y);
}

VOID
UserDpiRectToLogical(_Inout_ PRECT Rect)
{
    Rect->left = UserDpiToLogical(Rect->left);
    Rect->top = UserDpiToLogical(Rect->top);
    Rect->right = UserDpiToLogical(Rect->right);
    Rect->bottom = UserDpiToLogical(Rect->bottom);
}

VOID
UserDpiRectToPhysical(_Inout_ PRECT Rect)
{
    Rect->left = UserDpiToPhysical(Rect->left);
    Rect->top = UserDpiToPhysical(Rect->top);
    Rect->right = UserDpiToPhysical(Rect->right);
    Rect->bottom = UserDpiToPhysical(Rect->bottom);
}

static PREGION
UserDpiCreateMappedRegion(_In_ PREGION Source, _In_ BOOL Logical)
{
    PREGION Region = IntSysCreateRectpRgn(0, 0, 0, 0);
    ULONG Index;
    RECT Rect;

    if (Region == NULL)
        return NULL;

    for (Index = 0; Index < Source->rdh.nCount; Index++)
    {
        Rect = *(PRECT)&Source->Buffer[Index];
        if (Logical)
            UserDpiRectToLogical(&Rect);
        else
            UserDpiRectToPhysical(&Rect);
        if (Rect.left < Rect.right && Rect.top < Rect.bottom &&
            !REGION_UnionRectWithRgn(Region, (PRECTL)&Rect))
        {
            REGION_Delete(Region);
            return NULL;
        }
    }
    return Region;
}

PREGION
UserDpiCreateLogicalRegion(_In_ PREGION Source)
{
    return UserDpiCreateMappedRegion(Source, TRUE);
}

PREGION
UserDpiCreatePhysicalRegion(_In_ PREGION Source)
{
    return UserDpiCreateMappedRegion(Source, FALSE);
}

static LONG
UserDpiConvert(_In_ PUSER_DPI_MESSAGE Conv, _In_ LONG Value)
{
    return Conv->ToLogical ? UserDpiToLogical(Value) : UserDpiToPhysical(Value);
}

static LONG
UserDpiUnconvert(_In_ PUSER_DPI_MESSAGE Conv, _In_ LONG Value)
{
    return Conv->ToLogical ? UserDpiToPhysical(Value) : UserDpiToLogical(Value);
}

static VOID
UserDpiConvertRelativeRect(_In_ PUSER_DPI_MESSAGE Conv, _Inout_ PRECT Rect)
{
    Rect->left = UserDpiConvert(Conv, Conv->Origin.x + Rect->left) - Conv->TargetOrigin.x;
    Rect->top = UserDpiConvert(Conv, Conv->Origin.y + Rect->top) - Conv->TargetOrigin.y;
    Rect->right = UserDpiConvert(Conv, Conv->Origin.x + Rect->right) - Conv->TargetOrigin.x;
    Rect->bottom = UserDpiConvert(Conv, Conv->Origin.y + Rect->bottom) - Conv->TargetOrigin.y;
}

static LONG
UserDpiUnconvertEdge(_In_ PUSER_DPI_MESSAGE Conv, _In_ LONG Value, _In_ LONG Initial,
                     _In_ LONG Original, _In_ LONG TargetOrigin, _In_ LONG Origin)
{
    if (Value == Initial)
        return Original;
    return UserDpiUnconvert(Conv, TargetOrigin + Value) - Origin;
}

static VOID
UserDpiUnconvertRelativeRect(_In_ PUSER_DPI_MESSAGE Conv, _In_ const RECT *Value,
                             _In_ const RECT *Initial, _Inout_ PRECT Original)
{
    Original->left = UserDpiUnconvertEdge(Conv, Value->left, Initial->left, Original->left,
                                          Conv->TargetOrigin.x, Conv->Origin.x);
    Original->top = UserDpiUnconvertEdge(Conv, Value->top, Initial->top, Original->top,
                                         Conv->TargetOrigin.y, Conv->Origin.y);
    Original->right = UserDpiUnconvertEdge(Conv, Value->right, Initial->right, Original->right,
                                           Conv->TargetOrigin.x, Conv->Origin.x);
    Original->bottom = UserDpiUnconvertEdge(Conv, Value->bottom, Initial->bottom, Original->bottom,
                                            Conv->TargetOrigin.y, Conv->Origin.y);
}

static VOID
UserDpiWindowPosToRect(_In_ const WINDOWPOS *Pos, _Out_ PRECT Rect)
{
    Rect->left = Pos->x;
    Rect->top = Pos->y;
    Rect->right = Pos->x + Pos->cx;
    Rect->bottom = Pos->y + Pos->cy;
}

static VOID
UserDpiRectToWindowPos(_In_ const RECT *Rect, _Inout_ WINDOWPOS *Pos)
{
    Pos->x = Rect->left;
    Pos->y = Rect->top;
    Pos->cx = Rect->right - Rect->left;
    Pos->cy = Rect->bottom - Rect->top;
}

static VOID
UserDpiConvertWindowPos(_In_ PUSER_DPI_MESSAGE Conv, _Inout_ WINDOWPOS *Pos)
{
    RECT Rect;

    UserDpiWindowPosToRect(Pos, &Rect);
    UserDpiConvertRelativeRect(Conv, &Rect);
    UserDpiRectToWindowPos(&Rect, Pos);
}

static VOID
UserDpiUnconvertWindowPos(_In_ PUSER_DPI_MESSAGE Conv, _In_ const WINDOWPOS *Value,
                          _In_ const WINDOWPOS *Initial, _Inout_ WINDOWPOS *Original)
{
    RECT ValueRect, InitialRect, OriginalRect;

    UserDpiWindowPosToRect(Value, &ValueRect);
    UserDpiWindowPosToRect(Initial, &InitialRect);
    UserDpiWindowPosToRect(Original, &OriginalRect);
    UserDpiUnconvertRelativeRect(Conv, &ValueRect, &InitialRect, &OriginalRect);
    UserDpiRectToWindowPos(&OriginalRect, Original);
    Original->flags = Value->flags;
    Original->hwndInsertAfter = Value->hwndInsertAfter;
}

static VOID
UserDpiConvertPointValue(_In_ PUSER_DPI_MESSAGE Conv, _Inout_ PPOINT Point)
{
    Point->x = UserDpiConvert(Conv, Point->x);
    Point->y = UserDpiConvert(Conv, Point->y);
}

static VOID
UserDpiUnconvertPointValue(_In_ PUSER_DPI_MESSAGE Conv, _In_ const POINT *Value,
                           _In_ const POINT *Initial, _Inout_ PPOINT Original)
{
    if (Value->x != Initial->x)
        Original->x = UserDpiUnconvert(Conv, Value->x);
    if (Value->y != Initial->y)
        Original->y = UserDpiUnconvert(Conv, Value->y);
}

static LPARAM
UserDpiConvertScreenPoint(_In_ PUSER_DPI_MESSAGE Conv, _In_ LPARAM lParam)
{
    return MAKELPARAM(UserDpiConvert(Conv, (SHORT)LOWORD(lParam)),
                      UserDpiConvert(Conv, (SHORT)HIWORD(lParam)));
}

static BOOL
UserDpiIsMouseSysCommand(_In_ WPARAM wParam)
{
    switch (wParam & 0xFFF0)
    {
        case SC_SIZE:
        case SC_MOVE:
        case SC_MINIMIZE:
        case SC_MAXIMIZE:
        case SC_CLOSE:
        case SC_RESTORE:
        case SC_MOUSEMENU:
        case SC_CONTEXTHELP:
        case SC_DEFAULT:
        case SC_HSCROLL:
        case SC_VSCROLL:
            return TRUE;
    }
    return FALSE;
}

BOOL
UserDpiBeginMessage(
    _In_opt_ PWND Wnd,
    _In_ UINT Msg,
    _Inout_ WPARAM *wParam,
    _Inout_ LPARAM *lParam,
    _Out_ PUSER_DPI_MESSAGE Conv,
    _In_ BOOL ToLogical)
{
    POINT ParentOrigin = {0, 0};

    Conv->Active = FALSE;
    if (Wnd == NULL || !UserDpiIsVirtualizationActive())
        return FALSE;
    if (ToLogical && !UserDpiIsVirtualWindow(Wnd))
        return FALSE;

    if ((Wnd->style & WS_CHILD) && Wnd->spwndParent != NULL)
    {
        ParentOrigin.x = Wnd->spwndParent->rcClient.left;
        ParentOrigin.y = Wnd->spwndParent->rcClient.top;
    }
    Conv->ToLogical = ToLogical;
    Conv->Msg = Msg;
    if (ToLogical)
    {
        Conv->Origin = ParentOrigin;
        Conv->TargetOrigin = ParentOrigin;
        UserDpiPointToLogical(&Conv->TargetOrigin);
    }
    else
    {
        Conv->TargetOrigin = ParentOrigin;
        Conv->Origin = ParentOrigin;
        UserDpiPointToLogical(&Conv->Origin);
    }
    Conv->Source = (PVOID)*lParam;

    if (!ToLogical && (Msg == WM_NCCREATE || Msg == WM_CREATE || Msg == WM_SIZE || Msg == WM_MOVE))
        return FALSE;

    switch (Msg)
    {
        case WM_MOUSEMOVE:
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_LBUTTONDBLCLK:
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
        case WM_RBUTTONDBLCLK:
        case WM_MBUTTONDOWN:
        case WM_MBUTTONUP:
        case WM_MBUTTONDBLCLK:
        case WM_XBUTTONDOWN:
        case WM_XBUTTONUP:
        case WM_XBUTTONDBLCLK:
        case WM_MOUSEHOVER:
        {
            RECT Rect = {(SHORT)LOWORD(*lParam), (SHORT)HIWORD(*lParam), (SHORT)LOWORD(*lParam), (SHORT)HIWORD(*lParam)};

            Conv->Origin.x = Wnd->rcClient.left;
            Conv->Origin.y = Wnd->rcClient.top;
            Conv->TargetOrigin = Conv->Origin;
            UserDpiPointToLogical(ToLogical ? &Conv->TargetOrigin : &Conv->Origin);
            UserDpiConvertRelativeRect(Conv, &Rect);
            *lParam = MAKELPARAM(Rect.left, Rect.top);
            return TRUE;
        }

        case WM_MOUSEWHEEL:
        case WM_MOUSEHWHEEL:
        case WM_NCHITTEST:
        case WM_NCMOUSEMOVE:
        case WM_NCLBUTTONDOWN:
        case WM_NCLBUTTONUP:
        case WM_NCLBUTTONDBLCLK:
        case WM_NCRBUTTONDOWN:
        case WM_NCRBUTTONUP:
        case WM_NCRBUTTONDBLCLK:
        case WM_NCMBUTTONDOWN:
        case WM_NCMBUTTONUP:
        case WM_NCMBUTTONDBLCLK:
        case WM_NCXBUTTONDOWN:
        case WM_NCXBUTTONUP:
        case WM_NCXBUTTONDBLCLK:
        case WM_NCMOUSEHOVER:
            *lParam = UserDpiConvertScreenPoint(Conv, *lParam);
            return TRUE;

        case WM_CONTEXTMENU:
            if (*lParam != -1)
                *lParam = UserDpiConvertScreenPoint(Conv, *lParam);
            return TRUE;

        case WM_SYSCOMMAND:
            if (!UserDpiIsMouseSysCommand(*wParam))
                return FALSE;
            *lParam = UserDpiConvertScreenPoint(Conv, *lParam);
            return TRUE;

        case WM_NCPAINT:
            if (!ToLogical || *wParam <= 1)
                return FALSE;
            *wParam = 1;
            return TRUE;

        case WM_MOVE:
        {
            RECT Rect = {(SHORT)LOWORD(*lParam), (SHORT)HIWORD(*lParam), (SHORT)LOWORD(*lParam), (SHORT)HIWORD(*lParam)};

            UserDpiConvertRelativeRect(Conv, &Rect);
            *lParam = MAKELPARAM(Rect.left, Rect.top);
            return TRUE;
        }

        case WM_SIZE:
        {
            RECT Client = Wnd->rcClient;

            if (*wParam == SIZE_MINIMIZED)
                return FALSE;
            if (ToLogical &&
                LOWORD(*lParam) == Client.right - Client.left &&
                HIWORD(*lParam) == Client.bottom - Client.top)
            {
                UserDpiRectToLogical(&Client);
                *lParam = MAKELPARAM(Client.right - Client.left, Client.bottom - Client.top);
            }
            else
            {
                *lParam = MAKELPARAM(UserDpiConvert(Conv, LOWORD(*lParam)),
                                     UserDpiConvert(Conv, HIWORD(*lParam)));
            }
            return TRUE;
        }

        case WM_WINDOWPOSCHANGING:
        case WM_WINDOWPOSCHANGED:
            if (Conv->Source == NULL)
                return FALSE;
            Conv->Data.WinPos = *(WINDOWPOS *)Conv->Source;
            UserDpiConvertWindowPos(Conv, &Conv->Data.WinPos);
            Conv->Initial.WinPos = Conv->Data.WinPos;
            *lParam = (LPARAM)&Conv->Data.WinPos;
            Conv->Active = TRUE;
            return TRUE;

        case WM_GETMINMAXINFO:
            if (Conv->Source == NULL)
                return FALSE;
            Conv->Data.MinMax = *(MINMAXINFO *)Conv->Source;
            UserDpiConvertPointValue(Conv, &Conv->Data.MinMax.ptMaxSize);
            UserDpiConvertPointValue(Conv, &Conv->Data.MinMax.ptMaxPosition);
            UserDpiConvertPointValue(Conv, &Conv->Data.MinMax.ptMinTrackSize);
            UserDpiConvertPointValue(Conv, &Conv->Data.MinMax.ptMaxTrackSize);
            Conv->Initial.MinMax = Conv->Data.MinMax;
            *lParam = (LPARAM)&Conv->Data.MinMax;
            Conv->Active = TRUE;
            return TRUE;

        case WM_SIZING:
        case WM_MOVING:
            if (Conv->Source == NULL)
                return FALSE;
            Conv->Origin.x = Conv->Origin.y = 0;
            Conv->TargetOrigin.x = Conv->TargetOrigin.y = 0;
            Conv->Data.Rect = *(PRECT)Conv->Source;
            UserDpiConvertRelativeRect(Conv, &Conv->Data.Rect);
            Conv->Initial.Rect = Conv->Data.Rect;
            *lParam = (LPARAM)&Conv->Data.Rect;
            Conv->Active = TRUE;
            return TRUE;

        case WM_NCCALCSIZE:
            if (Conv->Source == NULL)
                return FALSE;
            Conv->NcParams = *wParam != 0;
            if (*wParam)
            {
                NCCALCSIZE_PARAMS *Params = (NCCALCSIZE_PARAMS *)Conv->Source;

                Conv->Data.NcCalc.Params = *Params;
                UserDpiConvertRelativeRect(Conv, &Conv->Data.NcCalc.Params.rgrc[0]);
                UserDpiConvertRelativeRect(Conv, &Conv->Data.NcCalc.Params.rgrc[1]);
                UserDpiConvertRelativeRect(Conv, &Conv->Data.NcCalc.Params.rgrc[2]);
                if (Params->lppos != NULL)
                {
                    Conv->Data.NcCalc.Pos = *Params->lppos;
                    UserDpiConvertWindowPos(Conv, &Conv->Data.NcCalc.Pos);
                    Conv->Data.NcCalc.Params.lppos = &Conv->Data.NcCalc.Pos;
                }
                Conv->Initial.NcCalc = Conv->Data.NcCalc;
                *lParam = (LPARAM)&Conv->Data.NcCalc.Params;
            }
            else
            {
                Conv->Data.Rect = *(PRECT)Conv->Source;
                UserDpiConvertRelativeRect(Conv, &Conv->Data.Rect);
                Conv->Initial.Rect = Conv->Data.Rect;
                *lParam = (LPARAM)&Conv->Data.Rect;
            }
            Conv->Active = TRUE;
            return TRUE;

        case WM_NCCREATE:
        case WM_CREATE:
        {
            RECT Rect;

            if (Conv->Source == NULL)
                return FALSE;
            Conv->Data.Create = *(CREATESTRUCTW *)Conv->Source;
            if (Conv->Data.Create.x != CW_USEDEFAULT && Conv->Data.Create.cx != CW_USEDEFAULT)
            {
                Rect.left = Conv->Data.Create.x;
                Rect.top = Conv->Data.Create.y;
                Rect.right = Conv->Data.Create.x + Conv->Data.Create.cx;
                Rect.bottom = Conv->Data.Create.y + Conv->Data.Create.cy;
                UserDpiConvertRelativeRect(Conv, &Rect);
                Conv->Data.Create.x = Rect.left;
                Conv->Data.Create.y = Rect.top;
                Conv->Data.Create.cx = Rect.right - Rect.left;
                Conv->Data.Create.cy = Rect.bottom - Rect.top;
            }
            *lParam = (LPARAM)&Conv->Data.Create;
            return TRUE;
        }
    }
    return FALSE;
}

VOID
UserDpiEndMessage(_In_ PUSER_DPI_MESSAGE Conv)
{
    if (!Conv->Active)
        return;
    Conv->Active = FALSE;

    switch (Conv->Msg)
    {
        case WM_WINDOWPOSCHANGING:
        case WM_WINDOWPOSCHANGED:
            UserDpiUnconvertWindowPos(Conv, &Conv->Data.WinPos, &Conv->Initial.WinPos,
                                      (WINDOWPOS *)Conv->Source);
            break;

        case WM_GETMINMAXINFO:
        {
            MINMAXINFO *MinMax = (MINMAXINFO *)Conv->Source;

            UserDpiUnconvertPointValue(Conv, &Conv->Data.MinMax.ptMaxSize,
                                       &Conv->Initial.MinMax.ptMaxSize, &MinMax->ptMaxSize);
            UserDpiUnconvertPointValue(Conv, &Conv->Data.MinMax.ptMaxPosition,
                                       &Conv->Initial.MinMax.ptMaxPosition, &MinMax->ptMaxPosition);
            UserDpiUnconvertPointValue(Conv, &Conv->Data.MinMax.ptMinTrackSize,
                                       &Conv->Initial.MinMax.ptMinTrackSize, &MinMax->ptMinTrackSize);
            UserDpiUnconvertPointValue(Conv, &Conv->Data.MinMax.ptMaxTrackSize,
                                       &Conv->Initial.MinMax.ptMaxTrackSize, &MinMax->ptMaxTrackSize);
            break;
        }

        case WM_SIZING:
        case WM_MOVING:
            UserDpiUnconvertRelativeRect(Conv, &Conv->Data.Rect, &Conv->Initial.Rect, (PRECT)Conv->Source);
            break;

        case WM_NCCALCSIZE:
            if (Conv->NcParams)
            {
                NCCALCSIZE_PARAMS *Params = (NCCALCSIZE_PARAMS *)Conv->Source;
                ULONG Index;

                for (Index = 0; Index < 3; Index++)
                {
                    UserDpiUnconvertRelativeRect(Conv, &Conv->Data.NcCalc.Params.rgrc[Index],
                                                 &Conv->Initial.NcCalc.Params.rgrc[Index],
                                                 &Params->rgrc[Index]);
                }
            }
            else
            {
                UserDpiUnconvertRelativeRect(Conv, &Conv->Data.Rect, &Conv->Initial.Rect, (PRECT)Conv->Source);
            }
            break;
    }
}

VOID
UserDpiVirtualizeMessage(_Inout_ PMSG Msg)
{
    USER_DPI_MESSAGE Conv;
    WPARAM wParam = Msg->wParam;
    LPARAM lParam = Msg->lParam;
    PWND Wnd;

    if (!UserDpiIsVirtualCaller())
        return;
    UserDpiPointToLogical(&Msg->pt);

    switch (Msg->message)
    {
        case WM_MOUSEMOVE:
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_LBUTTONDBLCLK:
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
        case WM_RBUTTONDBLCLK:
        case WM_MBUTTONDOWN:
        case WM_MBUTTONUP:
        case WM_MBUTTONDBLCLK:
        case WM_XBUTTONDOWN:
        case WM_XBUTTONUP:
        case WM_XBUTTONDBLCLK:
        case WM_MOUSEHOVER:
        case WM_MOUSEWHEEL:
        case WM_MOUSEHWHEEL:
        case WM_NCMOUSEMOVE:
        case WM_NCLBUTTONDOWN:
        case WM_NCLBUTTONUP:
        case WM_NCLBUTTONDBLCLK:
        case WM_NCRBUTTONDOWN:
        case WM_NCRBUTTONUP:
        case WM_NCRBUTTONDBLCLK:
        case WM_NCMBUTTONDOWN:
        case WM_NCMBUTTONUP:
        case WM_NCMBUTTONDBLCLK:
        case WM_NCXBUTTONDOWN:
        case WM_NCXBUTTONUP:
        case WM_NCXBUTTONDBLCLK:
        case WM_NCMOUSEHOVER:
        case WM_CONTEXTMENU:
        case WM_SYSCOMMAND:
            break;
        default:
            return;
    }

    Wnd = Msg->hwnd ? ValidateHwndNoErr(Msg->hwnd) : NULL;
    if (Wnd != NULL && UserDpiBeginMessage(Wnd, Msg->message, &wParam, &lParam, &Conv, TRUE))
    {
        Msg->wParam = wParam;
        Msg->lParam = lParam;
    }
}

static POINT
UserDpiParentClientOrigin(_In_opt_ PWND Parent, _In_ DWORD Style)
{
    POINT Origin = {0, 0};

    if ((Style & WS_CHILD) && Parent != NULL && !UserIsDesktopWindow(Parent))
    {
        Origin.x = Parent->rcClient.left;
        Origin.y = Parent->rcClient.top;
    }
    return Origin;
}

static LONG
UserDpiRelativeToPhysical(_In_ LONG Value, _In_ LONG Origin)
{
    return UserDpiToPhysical(UserDpiToLogical(Origin) + Value) - Origin;
}

VOID
UserDpiWindowPosToPhysical(
    _In_ PWND Wnd,
    _Inout_ PINT X,
    _Inout_ PINT Y,
    _Inout_ PINT Cx,
    _Inout_ PINT Cy,
    _In_ UINT Flags)
{
    POINT Origin = UserDpiParentClientOrigin(Wnd->spwndParent, Wnd->style);
    LONG Left, Top;

    if (!(Flags & SWP_NOMOVE))
    {
        Left = UserDpiRelativeToPhysical(*X, Origin.x);
        Top = UserDpiRelativeToPhysical(*Y, Origin.y);
        if (!(Flags & SWP_NOSIZE))
        {
            *Cx = UserDpiRelativeToPhysical(*X + *Cx, Origin.x) - Left;
            *Cy = UserDpiRelativeToPhysical(*Y + *Cy, Origin.y) - Top;
        }
        *X = Left;
        *Y = Top;
    }
    else if (!(Flags & SWP_NOSIZE))
    {
        *Cx = UserDpiToPhysical(UserDpiToLogical(Wnd->rcWindow.left) + *Cx) - Wnd->rcWindow.left;
        *Cy = UserDpiToPhysical(UserDpiToLogical(Wnd->rcWindow.top) + *Cy) - Wnd->rcWindow.top;
    }
}

VOID
UserDpiCreateStructToPhysical(_Inout_ CREATESTRUCTW *Cs)
{
    POINT Origin = UserDpiParentClientOrigin(Cs->hwndParent ? ValidateHwndNoErr(Cs->hwndParent) : NULL, Cs->style);
    LONG Left = Cs->x, Top = Cs->y;

    if (Cs->x != CW_USEDEFAULT)
    {
        Left = UserDpiRelativeToPhysical(Cs->x, Origin.x);
        Top = UserDpiRelativeToPhysical(Cs->y, Origin.y);
    }
    if (Cs->cx != CW_USEDEFAULT)
    {
        if (Cs->x != CW_USEDEFAULT)
        {
            Cs->cx = UserDpiRelativeToPhysical(Cs->x + Cs->cx, Origin.x) - Left;
            Cs->cy = UserDpiRelativeToPhysical(Cs->y + Cs->cy, Origin.y) - Top;
        }
        else
        {
            Cs->cx = UserDpiToPhysical(Cs->cx);
            Cs->cy = UserDpiToPhysical(Cs->cy);
        }
    }
    Cs->x = Left;
    Cs->y = Top;
}

LONG
UserDpiRelativeClientToPhysical(_In_ PWND Wnd, _In_ LONG Value, _In_ BOOL Horizontal)
{
    return UserDpiRelativeToPhysical(Value, Horizontal ? Wnd->rcClient.left : Wnd->rcClient.top);
}

static POINT
UserDpiClientOrigin(_In_ PWND Wnd)
{
    POINT Origin = {0, 0};

    if (!UserIsDesktopWindow(Wnd))
    {
        Origin.x = Wnd->rcClient.left;
        Origin.y = Wnd->rcClient.top;
    }
    return Origin;
}

VOID
UserDpiClientRectToLogical(_In_ PWND Wnd, _Inout_ PRECT Rect)
{
    POINT Origin = UserDpiClientOrigin(Wnd), Logical = Origin;

    UserDpiPointToLogical(&Logical);
    Rect->left = UserDpiToLogical(Origin.x + Rect->left) - Logical.x;
    Rect->top = UserDpiToLogical(Origin.y + Rect->top) - Logical.y;
    Rect->right = UserDpiToLogical(Origin.x + Rect->right) - Logical.x;
    Rect->bottom = UserDpiToLogical(Origin.y + Rect->bottom) - Logical.y;
}

VOID
UserDpiClientRectToPhysical(_In_ PWND Wnd, _Inout_ PRECT Rect)
{
    POINT Origin = UserDpiClientOrigin(Wnd), Logical = Origin;

    UserDpiPointToLogical(&Logical);
    Rect->left = UserDpiToPhysical(Logical.x + Rect->left) - Origin.x;
    Rect->top = UserDpiToPhysical(Logical.y + Rect->top) - Origin.y;
    Rect->right = UserDpiToPhysical(Logical.x + Rect->right) - Origin.x;
    Rect->bottom = UserDpiToPhysical(Logical.y + Rect->bottom) - Origin.y;
}

VOID
UserDpiGetLogicalClientRect(_In_ PWND Wnd, _Out_ PRECTL Rect)
{
    RECT Client = Wnd->rcClient;

    UserDpiRectToLogical(&Client);
    Rect->left = Rect->top = 0;
    Rect->right = Client.right - Client.left;
    Rect->bottom = Client.bottom - Client.top;
}

PREGION
UserDpiCreateClientRegion(_In_ PWND Wnd, _In_ PREGION Source, _In_ BOOL ToLogical)
{
    POINT Origin = UserDpiClientOrigin(Wnd), Logical = Origin;
    PREGION Copy, Region;

    UserDpiPointToLogical(&Logical);
    Copy = IntSysCreateRectpRgn(0, 0, 0, 0);
    if (Copy == NULL)
        return NULL;
    IntGdiCombineRgn(Copy, Source, NULL, RGN_COPY);
    if (ToLogical)
    {
        REGION_bOffsetRgn(Copy, Origin.x, Origin.y);
        Region = UserDpiCreateLogicalRegion(Copy);
        if (Region != NULL)
            REGION_bOffsetRgn(Region, -Logical.x, -Logical.y);
    }
    else
    {
        REGION_bOffsetRgn(Copy, Logical.x, Logical.y);
        Region = UserDpiCreatePhysicalRegion(Copy);
        if (Region != NULL)
            REGION_bOffsetRgn(Region, -Origin.x, -Origin.y);
    }
    REGION_Delete(Copy);
    return Region;
}

static VOID
UserDpiSetScale(_In_ HDC hDC, _In_ LONG WindowExtent, _In_ LONG ViewportExtent)
{
    PDC pdc = DC_LockDc(hDC);

    if (pdc == NULL)
        return;
    IntGdiSetMapMode(pdc, MM_ANISOTROPIC);
    pdc->pdcattr->szlWindowExt.cx = WindowExtent;
    pdc->pdcattr->szlWindowExt.cy = WindowExtent;
    pdc->pdcattr->szlViewportExt.cx = ViewportExtent;
    pdc->pdcattr->szlViewportExt.cy = ViewportExtent;
    pdc->pdcattr->flXform |= PAGE_EXTENTS_CHANGED | INVALIDATE_ATTRIBUTES | DEVICE_TO_WORLD_INVALID;
    DC_UnlockDc(pdc);
}

INT
UserDpiBeginPhysicalDrawing(_In_opt_ PWND Wnd, _In_opt_ HDC hDC)
{
    INT Level;

    if (hDC == NULL || !UserDpiIsVirtualWindow(Wnd))
        return 0;
    Level = NtGdiSaveDC(hDC);
    if (Level > 0)
        UserDpiSetScale(hDC, (LONG)UserGetSystemDpi(), USER_SYSTEM_DPI_DEFAULT);
    return Level;
}

INT
UserDpiBeginLogicalDrawing(_In_opt_ PWND DcWindow, _In_ HDC hDC)
{
    INT Level = NtGdiSaveDC(hDC);

    if (Level <= 0)
        return 0;
    if (UserDpiIsVirtualWindow(DcWindow))
        UserDpiSetScale(hDC, 1, 1);
    else
        UserDpiSetScale(hDC, USER_SYSTEM_DPI_DEFAULT, (LONG)UserGetSystemDpi());
    return Level;
}

VOID
UserDpiEndDrawing(_In_opt_ HDC hDC, _In_ INT Level)
{
    if (hDC != NULL && Level > 0)
        NtGdiRestoreDC(hDC, Level);
}
