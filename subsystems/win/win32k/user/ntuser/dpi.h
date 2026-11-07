#pragma once

typedef union _USER_DPI_MESSAGE_DATA
{
    RECT Rect;
    WINDOWPOS WinPos;
    MINMAXINFO MinMax;
    CREATESTRUCTW Create;
    struct
    {
        NCCALCSIZE_PARAMS Params;
        WINDOWPOS Pos;
    } NcCalc;
} USER_DPI_MESSAGE_DATA;

typedef struct _USER_DPI_MESSAGE
{
    BOOL Active;
    BOOL ToLogical;
    BOOL NcParams;
    UINT Msg;
    POINT Origin;
    POINT TargetOrigin;
    PVOID Source;
    USER_DPI_MESSAGE_DATA Data;
    USER_DPI_MESSAGE_DATA Initial;
} USER_DPI_MESSAGE, *PUSER_DPI_MESSAGE;

VOID UserDpiUpdateVirtualization(VOID);
BOOL UserDpiIsVirtualizationActive(VOID);
ULONG UserDpiGetThreadContext(_In_opt_ PTHREADINFO pti);
BOOL UserDpiIsVirtualThread(_In_opt_ PTHREADINFO pti);
BOOL UserDpiIsVirtualCaller(VOID);
BOOL UserDpiIsVirtualWindow(_In_opt_ PWND Wnd);
LONG UserDpiToLogical(_In_ LONG Value);
LONG UserDpiToPhysical(_In_ LONG Value);
VOID UserDpiPointToLogical(_Inout_ PPOINT Point);
VOID UserDpiPointToPhysical(_Inout_ PPOINT Point);
VOID UserDpiRectToLogical(_Inout_ PRECT Rect);
VOID UserDpiRectToPhysical(_Inout_ PRECT Rect);
PREGION UserDpiCreateLogicalRegion(_In_ PREGION Source);
PREGION UserDpiCreatePhysicalRegion(_In_ PREGION Source);
BOOL UserDpiBeginMessage(_In_opt_ PWND Wnd, _In_ UINT Msg, _Inout_ WPARAM *wParam, _Inout_ LPARAM *lParam,
                         _Out_ PUSER_DPI_MESSAGE Conv, _In_ BOOL ToLogical);
VOID UserDpiEndMessage(_In_ PUSER_DPI_MESSAGE Conv);
VOID UserDpiVirtualizeMessage(_Inout_ PMSG Msg);
VOID UserDpiWindowPosToPhysical(_In_ PWND Wnd, _Inout_ PINT X, _Inout_ PINT Y, _Inout_ PINT Cx, _Inout_ PINT Cy, _In_ UINT Flags);
VOID UserDpiCreateStructToPhysical(_Inout_ CREATESTRUCTW *Cs);
VOID UserDpiClientRectToLogical(_In_ PWND Wnd, _Inout_ PRECT Rect);
VOID UserDpiClientRectToPhysical(_In_ PWND Wnd, _Inout_ PRECT Rect);
VOID UserDpiGetLogicalClientRect(_In_ PWND Wnd, _Out_ PRECTL Rect);
PREGION UserDpiCreateClientRegion(_In_ PWND Wnd, _In_ PREGION Source, _In_ BOOL ToLogical);
LONG UserDpiRelativeClientToPhysical(_In_ PWND Wnd, _In_ LONG Value, _In_ BOOL Horizontal);
INT UserDpiBeginPhysicalDrawing(_In_opt_ PWND Wnd, _In_opt_ HDC hDC);
INT UserDpiBeginLogicalDrawing(_In_opt_ PWND DcWindow, _In_ HDC hDC);
VOID UserDpiEndDrawing(_In_opt_ HDC hDC, _In_ INT Level);
