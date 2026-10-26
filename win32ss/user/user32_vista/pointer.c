/*
 * PROJECT:     ReactOS User32 - Vista+ APIs
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Pointer device, auto-rotation and window arrangement queries
 * COPYRIGHT:   Adapted from corresponding Wine user32 implementations
 */

#define WIN32_NO_STATUS
#define _INC_WINDOWS
#define COM_NO_WINDOWS_H
#include <windef.h>
#include <winbase.h>
#include <wingdi.h>
#include <winuser.h>

#if (WINVER < 0x0602)
typedef struct tagPOINTER_DEVICE_INFO POINTER_DEVICE_INFO;
typedef struct tagPOINTER_TOUCH_INFO POINTER_TOUCH_INFO;
typedef struct tagPOINTER_PEN_INFO POINTER_PEN_INFO;
#endif

#define NDEBUG
#include <debug.h>

BOOL APIENTRY
NtUserGetPointerDeviceRects(
    _In_ HANDLE device,
    _Out_ RECT *deviceRect,
    _Out_ RECT *displayRect);

BOOL APIENTRY
NtUserGetPointerInfoList(
    _In_ ULONG PointerId,
    _In_ ULONG PointerType,
    _In_ ULONG_PTR SourceDevice,
    _In_ ULONG_PTR hProcess,
    _In_ ULONG_PTR EntrySize,
    _Inout_ PULONG EntriesCount,
    _Inout_ PULONG PointerCount,
    _Out_opt_ PVOID PointerInfo);

static
BOOL
PointerQuery(
    _In_ UINT32 pointerId,
    _In_ ULONG pointerType,
    _In_ SIZE_T entrySize,
    _Out_ PVOID info)
{
    ULONG EntriesCount = 1, PointerCount = 1;

    if (info == NULL)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    return NtUserGetPointerInfoList(pointerId, pointerType, 0, 0, entrySize, &EntriesCount, &PointerCount, info);
}

static
BOOL
PointerQueryFrame(
    _In_ UINT32 pointerId,
    _In_ ULONG pointerType,
    _In_ SIZE_T entrySize,
    _Inout_ UINT32 *pointerCount,
    _Out_ PVOID info)
{
    POINTER_TOUCH_INFO Pair[2];
    ULONG EntriesCount = 1, Count;

    if (pointerCount == NULL || info == NULL)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    Count = (*pointerCount > 1) ? *pointerCount : RTL_NUMBER_OF(Pair);
    if (!NtUserGetPointerInfoList(pointerId, pointerType, 0, 0, entrySize, &EntriesCount, &Count,
                                  (*pointerCount > 1) ? info : (PVOID)Pair))
    {
        if (GetLastError() == ERROR_INSUFFICIENT_BUFFER)
            *pointerCount = Count;
        return FALSE;
    }

    if (*pointerCount <= 1)
    {
        if (Count > *pointerCount)
        {
            *pointerCount = Count;
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
            return FALSE;
        }
        RtlCopyMemory(info, Pair, entrySize);
    }

    *pointerCount = Count;
    return TRUE;
}

BOOL
WINAPI
GetPointerType(
    _In_ UINT32 pointerId,
    _Out_ POINTER_INPUT_TYPE *pointerType)
{
    POINTER_INFO Info;

    if (pointerId == 0 || pointerType == NULL)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    if (pointerId == 1)
    {
        *pointerType = PT_MOUSE;
        return TRUE;
    }

    if (!PointerQuery(pointerId, PT_POINTER, sizeof(Info), &Info))
        return FALSE;

    *pointerType = Info.pointerType;
    return TRUE;
}

BOOL
WINAPI
GetPointerInfo(
    _In_ UINT32 pointerId,
    _Out_ POINTER_INFO *pointerInfo)
{
    return PointerQuery(pointerId, PT_POINTER, sizeof(*pointerInfo), pointerInfo);
}

BOOL
WINAPI
GetPointerTouchInfo(
    _In_ UINT32 pointerId,
    _Out_ POINTER_TOUCH_INFO *touchInfo)
{
    return PointerQuery(pointerId, PT_TOUCH, sizeof(*touchInfo), touchInfo);
}

BOOL
WINAPI
GetPointerTouchInfoHistory(
    _In_ UINT32 pointerId,
    _Inout_ UINT32 *entriesCount,
    _Out_writes_(*entriesCount) POINTER_TOUCH_INFO *touchInfo)
{
    if (entriesCount == NULL || *entriesCount == 0)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    if (!PointerQuery(pointerId, PT_TOUCH, sizeof(*touchInfo), touchInfo))
        return FALSE;

    *entriesCount = 1;
    return TRUE;
}

BOOL
WINAPI
GetPointerFrameTouchInfo(
    _In_ UINT32 pointerId,
    _Inout_ UINT32 *pointerCount,
    _Out_writes_opt_(*pointerCount) POINTER_TOUCH_INFO *touchInfo)
{
    return PointerQueryFrame(pointerId, PT_TOUCH, sizeof(*touchInfo), pointerCount, touchInfo);
}

BOOL
WINAPI
GetAutoRotationState(
    _Out_ PAR_STATE pState)
{
    if (pState == NULL)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    *pState = AR_NOT_SUPPORTED;
    return TRUE;
}

BOOL
WINAPI
GetPointerDevice(
    _In_ HANDLE device,
    _Out_ POINTER_DEVICE_INFO *pointerDevice)
{
    UNREFERENCED_PARAMETER(device);
    UNREFERENCED_PARAMETER(pointerDevice);

    SetLastError(ERROR_INVALID_HANDLE);
    return FALSE;
}

BOOL
WINAPI
GetPointerDeviceRects(
    _In_ HANDLE device,
    _Out_ RECT *deviceRect,
    _Out_ RECT *displayRect)
{
    if (device == NULL || deviceRect == NULL || displayRect == NULL)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    return NtUserGetPointerDeviceRects(device, deviceRect, displayRect);
}

BOOL
WINAPI
GetPointerFrameTouchInfoHistory(
    _In_ UINT32 pointerId,
    _Inout_ UINT32 *entriesCount,
    _Inout_ UINT32 *pointerCount,
    _Out_writes_(*entriesCount * *pointerCount) POINTER_TOUCH_INFO *touchInfo)
{
    if (entriesCount == NULL || *entriesCount == 0)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    if (!PointerQueryFrame(pointerId, PT_TOUCH, sizeof(*touchInfo), pointerCount, touchInfo))
        return FALSE;

    *entriesCount = 1;
    return TRUE;
}

BOOL
WINAPI
GetPointerPenInfo(
    _In_ UINT32 pointerId,
    _Out_ POINTER_PEN_INFO *penInfo)
{
    UNREFERENCED_PARAMETER(pointerId);
    UNREFERENCED_PARAMETER(penInfo);

    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
}

BOOL
WINAPI
GetPointerPenInfoHistory(
    _In_ UINT32 pointerId,
    _Inout_ UINT32 *entriesCount,
    _Out_writes_(*entriesCount) POINTER_PEN_INFO *penInfo)
{
    UNREFERENCED_PARAMETER(pointerId);
    UNREFERENCED_PARAMETER(entriesCount);
    UNREFERENCED_PARAMETER(penInfo);

    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
}

BOOL
WINAPI
InitializeTouchInjection(
    _In_ UINT32 maxCount,
    _In_ DWORD dwMode)
{
    if (maxCount == 0 || maxCount > MAX_TOUCH_COUNT ||
        (dwMode != TOUCH_FEEDBACK_DEFAULT && dwMode != TOUCH_FEEDBACK_INDIRECT && dwMode != TOUCH_FEEDBACK_NONE))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    return TRUE;
}

BOOL
WINAPI
InjectTouchInput(
    _In_ UINT32 count,
    _In_reads_(count) const POINTER_TOUCH_INFO *contacts)
{
    if (count == 0 || count > MAX_TOUCH_COUNT || contacts == NULL)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL
WINAPI
SkipPointerFrameMessages(
    _In_ UINT32 pointerId)
{
    UNREFERENCED_PARAMETER(pointerId);

    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
}

BOOL
WINAPI
IsWindowArranged(
    _In_ HWND hwnd)
{
    if (!IsWindow(hwnd))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    return FALSE;
}
