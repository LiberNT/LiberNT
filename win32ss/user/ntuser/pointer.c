/*
 * PROJECT:     LiberNT Win32k subsystem
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Touch pointers: identifiers, frames, pointer messages and mouse promotion
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <win32k.h>
#include <windowsx.h>

#define POINTER_FIRST_TOUCH_ID 2
#define POINTER_TOUCH_ID_COUNT MAX_TOUCH_COUNT
#define POINTER_FRAME_HISTORY 128
#define POINTER_TOUCH_EXTRA_INFO 0xFF515780
#define POINTER_COORDINATE_MAX 0xFFFF

typedef enum _POINTER_EVENT
{
    PointerEventNone,
    PointerEventDown,
    PointerEventUpdate,
    PointerEventUp
} POINTER_EVENT;

typedef struct _POINTER_TOUCH_SLOT
{
    BOOLEAN Active;
    BOOLEAN Primary;
    BOOLEAN Confidence;
    UCHAR Event;
    ULONG ContactId;
    UINT32 PointerId;
    POINT Pt;
    HWND hwndTopLevel;
} POINTER_TOUCH_SLOT, *PPOINTER_TOUCH_SLOT;

typedef struct _POINTER_TOUCH_SOURCE
{
    ULONG Capacity;
    POINTER_TOUCH_SLOT Slots[ANYSIZE_ARRAY];
} POINTER_TOUCH_SOURCE, *PPOINTER_TOUCH_SOURCE;

typedef struct _POINTER_FRAME
{
    UINT32 FrameId;
    ULONG Count;
    POINTER_TOUCH_INFO Info[ANYSIZE_ARRAY];
} POINTER_FRAME, *PPOINTER_FRAME;

typedef struct _POINTER_ROUTE
{
    HWND hwndTarget;
    USHORT HitTest;
    BOOLEAN Resolved;
    UINT PromotedMessage;
    UINT32 PromotedFrameId;
} POINTER_ROUTE, *PPOINTER_ROUTE;

PWND FASTCALL IntTopLevelWindowFromPoint(INT x, INT y);

static PPOINTER_FRAME gPointerFrames[POINTER_FRAME_HISTORY];
static UINT32 gPointerFrameId;
static BOOLEAN gTouchIdUsed[POINTER_TOUCH_ID_COUNT];
static POINTER_ROUTE gTouchRoutes[POINTER_TOUCH_ID_COUNT];
static ULONG gTouchNextId;
static ULONG gTouchContacts;
static BOOLEAN gTouchPrimaryClaimed;

static
PPOINTER_ROUTE
PointerTouchRoute(
    _In_ UINT32 PointerId)
{
    if (PointerId < POINTER_FIRST_TOUCH_ID || PointerId >= POINTER_FIRST_TOUCH_ID + POINTER_TOUCH_ID_COUNT)
        return NULL;
    return &gTouchRoutes[PointerId - POINTER_FIRST_TOUCH_ID];
}

static
BOOLEAN
PointerAllocateTouchId(
    _Out_ PUINT32 PointerId)
{
    ULONG Step, Index;

    for (Step = 0; Step < POINTER_TOUCH_ID_COUNT; Step++)
    {
        Index = (gTouchNextId + Step) % POINTER_TOUCH_ID_COUNT;
        if (gTouchIdUsed[Index])
            continue;
        gTouchIdUsed[Index] = TRUE;
        gTouchNextId = (Index + 1) % POINTER_TOUCH_ID_COUNT;
        *PointerId = POINTER_FIRST_TOUCH_ID + Index;
        return TRUE;
    }
    return FALSE;
}

static
PPOINTER_FRAME
PointerFindFrame(
    _In_ LONG_PTR FrameId)
{
    PPOINTER_FRAME Frame;

    if (FrameId <= 0 || (ULONG_PTR)FrameId != (UINT32)FrameId)
        return NULL;
    Frame = gPointerFrames[(ULONG)FrameId % POINTER_FRAME_HISTORY];
    return (Frame && Frame->FrameId == (UINT32)FrameId) ? Frame : NULL;
}

static
POINTER_TOUCH_INFO *
PointerFindInFrame(
    _In_ PPOINTER_FRAME Frame,
    _In_ UINT32 PointerId)
{
    ULONG Index;

    for (Index = 0; Index < Frame->Count; Index++)
    {
        if (Frame->Info[Index].pointerInfo.pointerId == PointerId)
            return &Frame->Info[Index];
    }
    return NULL;
}

static
VOID
PointerMapToScreen(
    _In_ const TOUCH_CONTACT *Contact,
    _Out_ PPOINT Pt)
{
    PMONITOR Monitor = UserGetPrimaryMonitor();
    LONG Width = 1, Height = 1, Left = 0, Top = 0;

    if (Monitor)
    {
        Left = Monitor->rcMonitor.left;
        Top = Monitor->rcMonitor.top;
        Width = max(Monitor->rcMonitor.right - Monitor->rcMonitor.left, 1);
        Height = max(Monitor->rcMonitor.bottom - Monitor->rcMonitor.top, 1);
    }
    Pt->x = Left + (LONG)(((ULONGLONG)min(Contact->X, POINTER_COORDINATE_MAX) * (ULONG)(Width - 1)) / POINTER_COORDINATE_MAX);
    Pt->y = Top + (LONG)(((ULONGLONG)min(Contact->Y, POINTER_COORDINATE_MAX) * (ULONG)(Height - 1)) / POINTER_COORDINATE_MAX);
}

static
VOID
PointerFillTouchInfo(
    _In_ PPOINTER_TOUCH_SLOT Slot,
    _In_ UINT32 FrameId,
    _In_ BOOLEAN Canceled,
    _In_ DWORD Time,
    _In_ ULONGLONG PerformanceCount,
    _Out_ POINTER_TOUCH_INFO *Info)
{
    POINTER_INFO *Common = &Info->pointerInfo;
    ULONG Dpi = (gpsi && gpsi->dmLogPixels) ? gpsi->dmLogPixels : 96;
    POINTER_FLAGS Flags;

    RtlZeroMemory(Info, sizeof(*Info));
    switch (Slot->Event)
    {
        case PointerEventDown:
            Flags = POINTER_FLAG_NEW | POINTER_FLAG_INRANGE | POINTER_FLAG_INCONTACT | POINTER_FLAG_FIRSTBUTTON | POINTER_FLAG_DOWN;
            Common->ButtonChangeType = POINTER_CHANGE_FIRSTBUTTON_DOWN;
            break;
        case PointerEventUpdate:
            Flags = POINTER_FLAG_INRANGE | POINTER_FLAG_INCONTACT | POINTER_FLAG_FIRSTBUTTON | POINTER_FLAG_UPDATE;
            Common->ButtonChangeType = POINTER_CHANGE_NONE;
            break;
        default:
            Flags = POINTER_FLAG_UP;
            Common->ButtonChangeType = POINTER_CHANGE_FIRSTBUTTON_UP;
            break;
    }
    if (Slot->Primary)
        Flags |= POINTER_FLAG_PRIMARY;
    if (Slot->Confidence)
        Flags |= POINTER_FLAG_CONFIDENCE;
    if (Canceled)
        Flags |= POINTER_FLAG_CANCELED;

    Common->pointerType = PT_TOUCH;
    Common->pointerId = Slot->PointerId;
    Common->frameId = FrameId;
    Common->pointerFlags = Flags;
    Common->sourceDevice = INVALID_HANDLE_VALUE;
    Common->hwndTarget = Slot->hwndTopLevel;
    Common->ptPixelLocation = Slot->Pt;
    Common->ptPixelLocationRaw = Slot->Pt;
    Common->ptHimetricLocation.x = (LONG)(((LONGLONG)Slot->Pt.x * 2540) / Dpi);
    Common->ptHimetricLocation.y = (LONG)(((LONGLONG)Slot->Pt.y * 2540) / Dpi);
    Common->ptHimetricLocationRaw = Common->ptHimetricLocation;
    Common->dwTime = Time;
    Common->historyCount = 1;
    Common->PerformanceCount = PerformanceCount;
    if (IS_KEY_DOWN(gafAsyncKeyState, VK_SHIFT))
        Common->dwKeyStates |= POINTER_MOD_SHIFT;
    if (IS_KEY_DOWN(gafAsyncKeyState, VK_CONTROL))
        Common->dwKeyStates |= POINTER_MOD_CTRL;

    Info->touchFlags = TOUCH_FLAG_NONE;
    Info->touchMask = TOUCH_MASK_NONE;
    Info->rcContact.left = Slot->Pt.x;
    Info->rcContact.top = Slot->Pt.y;
    Info->rcContact.right = Slot->Pt.x + 1;
    Info->rcContact.bottom = Slot->Pt.y + 1;
    Info->rcContactRaw = Info->rcContact;
}

static
VOID
PointerPost(
    _In_ PPOINTER_TOUCH_SLOT Slot,
    _In_ UINT Message,
    _In_ const POINTER_TOUCH_INFO *Info,
    _In_ DWORD Time)
{
    static const INPUT_MESSAGE_SOURCE Source = { IMDT_TOUCH, IMO_HARDWARE };
    PPOINTER_ROUTE Route = PointerTouchRoute(Slot->PointerId);
    HWND hwnd = (Route && Route->Resolved) ? Route->hwndTarget : Slot->hwndTopLevel;
    PWND pwnd = ValidateHwndNoErr(hwnd);
    MSG Msg;

    if (!pwnd || !pwnd->head.pti)
        return;

    Msg.hwnd = hwnd;
    Msg.message = Message;
    Msg.wParam = MAKEWPARAM(Slot->PointerId, LOWORD(Info->pointerInfo.pointerFlags));
    Msg.lParam = MAKELPARAM(Slot->Pt.x, Slot->Pt.y);
    Msg.time = Time;
    Msg.pt = Slot->Pt;
    MsqPostMessageEx(pwnd->head.pti, &Msg, FALSE, QS_POINTER, 0, Info->pointerInfo.frameId, &Source);
}

static
VOID
PointerCommitFrame(
    _Inout_ PPOINTER_TOUCH_SOURCE Source,
    _In_ BOOLEAN Canceled)
{
    PPOINTER_FRAME Frame = NULL, Old;
    PPOINTER_TOUCH_SLOT Slot;
    ULONGLONG PerformanceCount;
    ULONG Count = 0, Index, Entry = 0;
    UINT32 FrameId;
    DWORD Time;

    for (Index = 0; Index < Source->Capacity; Index++)
    {
        if (Source->Slots[Index].Event != PointerEventNone)
            Count++;
    }
    if (!Count)
        return;

    if (++gPointerFrameId == 0)
        gPointerFrameId = 1;
    FrameId = gPointerFrameId;
    Time = EngGetTickCount32();
    PerformanceCount = KeQueryPerformanceCounter(NULL).QuadPart;

    Frame = ExAllocatePoolWithTag(PagedPool, FIELD_OFFSET(POINTER_FRAME, Info[Count]), USERTAG_HIDDATA);
    if (Frame)
    {
        Frame->FrameId = FrameId;
        Frame->Count = Count;
        for (Index = 0; Index < Source->Capacity; Index++)
        {
            Slot = &Source->Slots[Index];
            if (Slot->Event != PointerEventNone)
                PointerFillTouchInfo(Slot, FrameId, Canceled, Time, PerformanceCount, &Frame->Info[Entry++]);
        }

        Old = gPointerFrames[FrameId % POINTER_FRAME_HISTORY];
        gPointerFrames[FrameId % POINTER_FRAME_HISTORY] = Frame;
        if (Old)
            ExFreePoolWithTag(Old, USERTAG_HIDDATA);

        Entry = 0;
        for (Index = 0; Index < Source->Capacity; Index++)
        {
            Slot = &Source->Slots[Index];
            switch (Slot->Event)
            {
                case PointerEventDown:
                    PointerPost(Slot, WM_POINTERDOWN, &Frame->Info[Entry], Time);
                    PointerPost(Slot, WM_POINTERENTER, &Frame->Info[Entry], Time);
                    break;
                case PointerEventUpdate:
                    PointerPost(Slot, WM_POINTERUPDATE, &Frame->Info[Entry], Time);
                    break;
                case PointerEventUp:
                    PointerPost(Slot, WM_POINTERUP, &Frame->Info[Entry], Time);
                    PointerPost(Slot, WM_POINTERLEAVE, &Frame->Info[Entry], Time);
                    break;
                default:
                    continue;
            }
            Entry++;
        }
    }

    for (Index = 0; Index < Source->Capacity; Index++)
    {
        Slot = &Source->Slots[Index];
        if (Slot->Event == PointerEventUp)
        {
            gTouchIdUsed[Slot->PointerId - POINTER_FIRST_TOUCH_ID] = FALSE;
            Slot->Active = FALSE;
            if (gTouchContacts)
                gTouchContacts--;
            if (!gTouchContacts)
                gTouchPrimaryClaimed = FALSE;
        }
        Slot->Event = PointerEventNone;
    }
}

PPOINTER_TOUCH_SOURCE
NTAPI
PointerCreateTouchSource(
    _In_ ULONG Capacity)
{
    PPOINTER_TOUCH_SOURCE Source;

    if (!Capacity)
        return NULL;
    Source = ExAllocatePoolZero(NonPagedPool, FIELD_OFFSET(POINTER_TOUCH_SOURCE, Slots[Capacity]), USERTAG_HIDDATA);
    if (Source)
        Source->Capacity = Capacity;
    return Source;
}

VOID
NTAPI
PointerDestroyTouchSource(
    _In_ PPOINTER_TOUCH_SOURCE Source)
{
    ULONG Index;

    for (Index = 0; Index < Source->Capacity; Index++)
    {
        if (Source->Slots[Index].Active)
            Source->Slots[Index].Event = PointerEventUp;
    }
    PointerCommitFrame(Source, TRUE);
    ExFreePoolWithTag(Source, USERTAG_HIDDATA);
}

VOID
NTAPI
PointerProcessTouchFrame(
    _Inout_ PPOINTER_TOUCH_SOURCE Source,
    _In_reads_(Count) const TOUCH_CONTACT *Contacts,
    _In_ ULONG Count)
{
    PPOINTER_TOUCH_SLOT Slot, Free;
    PPOINTER_ROUTE Route;
    ULONG Contact, Index;
    PWND pwnd;

    for (Contact = 0; Contact < Count; Contact++)
    {
        Slot = NULL;
        Free = NULL;
        for (Index = 0; Index < Source->Capacity; Index++)
        {
            if (Source->Slots[Index].Active && Source->Slots[Index].ContactId == Contacts[Contact].ContactId)
            {
                Slot = &Source->Slots[Index];
                break;
            }
            if (!Source->Slots[Index].Active && !Free)
                Free = &Source->Slots[Index];
        }

        if (Slot)
        {
            if (Slot->Event != PointerEventNone)
                continue;
            PointerMapToScreen(&Contacts[Contact], &Slot->Pt);
            Slot->Confidence = Contacts[Contact].Confidence;
            Slot->Event = Contacts[Contact].InContact ? PointerEventUpdate : PointerEventUp;
            continue;
        }

        if (!Contacts[Contact].InContact || !Free || !PointerAllocateTouchId(&Free->PointerId))
            continue;

        Free->Active = TRUE;
        Free->ContactId = Contacts[Contact].ContactId;
        Free->Confidence = Contacts[Contact].Confidence;
        Free->Primary = !gTouchPrimaryClaimed;
        Free->Event = PointerEventDown;
        PointerMapToScreen(&Contacts[Contact], &Free->Pt);
        pwnd = IntTopLevelWindowFromPoint(Free->Pt.x, Free->Pt.y);
        Free->hwndTopLevel = pwnd ? UserHMGetHandle(pwnd) : NULL;
        gTouchPrimaryClaimed = TRUE;
        gTouchContacts++;

        Route = PointerTouchRoute(Free->PointerId);
        RtlZeroMemory(Route, sizeof(*Route));
        Route->hwndTarget = Free->hwndTopLevel;
    }

    for (Index = 0; Index < Source->Capacity; Index++)
    {
        Slot = &Source->Slots[Index];
        if (Slot->Active && Slot->Event == PointerEventNone)
        {
            for (Contact = 0; Contact < Count && Contacts[Contact].ContactId != Slot->ContactId; Contact++)
                ;
            if (Contact == Count)
                Slot->Event = PointerEventUp;
        }
    }

    PointerCommitFrame(Source, FALSE);
}

VOID
FASTCALL
co_PointerRouteMessage(
    _In_ PTHREADINFO pti,
    _Inout_ MSG *Msg,
    _In_ LONG_PTR ExtraInfo)
{
    PPOINTER_ROUTE Route;
    USHORT HitTest = HTCLIENT;
    UINT32 PointerId;
    PWND pwnd;
    POINT Pt;

    if (Msg->message < WM_POINTERUPDATE || Msg->message > WM_POINTERLEAVE)
        return;
    PointerId = GET_POINTERID_WPARAM(Msg->wParam);
    Route = PointerTouchRoute(PointerId);
    if (!Route || !PointerFindFrame(ExtraInfo))
        return;

    if (Msg->message == WM_POINTERDOWN && !Route->Resolved)
    {
        Pt.x = GET_X_LPARAM(Msg->lParam);
        Pt.y = GET_Y_LPARAM(Msg->lParam);
        pwnd = co_WinPosWindowFromPoint(NULL, &Pt, &HitTest, FALSE);
        if (!pwnd || pwnd->head.pti->MessageQueue != pti->MessageQueue)
        {
            pwnd = ValidateHwndNoErr(Msg->hwnd);
            HitTest = HTCLIENT;
        }
        Route->hwndTarget = pwnd ? UserHMGetHandle(pwnd) : Msg->hwnd;
        Route->HitTest = HitTest;
        Route->Resolved = TRUE;
    }

    if (Route->Resolved)
    {
        Msg->hwnd = Route->hwndTarget;
        if (Route->HitTest != HTCLIENT)
        {
            switch (Msg->message)
            {
                case WM_POINTERDOWN:
                    Msg->message = WM_NCPOINTERDOWN;
                    Msg->wParam = MAKEWPARAM(PointerId, Route->HitTest);
                    break;
                case WM_POINTERUPDATE:
                    Msg->message = WM_NCPOINTERUPDATE;
                    Msg->wParam = MAKEWPARAM(PointerId, Route->HitTest);
                    break;
                case WM_POINTERUP:
                    Msg->message = WM_NCPOINTERUP;
                    Msg->wParam = MAKEWPARAM(PointerId, Route->HitTest);
                    break;
                default:
                    break;
            }
        }
    }

    pti->MessageQueue->ExtraInfo = ExtraInfo;
}

VOID
FASTCALL
PointerDefWindowProc(
    _In_ PWND Wnd,
    _In_ UINT Msg,
    _In_ WPARAM wParam)
{
    PTHREADINFO pti = PsGetCurrentThreadWin32Thread();
    POINTER_TOUCH_INFO *Info;
    PPOINTER_FRAME Frame;
    PPOINTER_ROUTE Route;
    MOUSEINPUT Input;
    LONG Width, Height;
    POINT Pt;

    UNREFERENCED_PARAMETER(Wnd);

    Route = PointerTouchRoute(GET_POINTERID_WPARAM(wParam));
    if (!Route || !pti || !pti->MessageQueue)
        return;
    Frame = PointerFindFrame(pti->MessageQueue->ExtraInfo);
    if (!Frame)
        return;
    Info = PointerFindInFrame(Frame, GET_POINTERID_WPARAM(wParam));
    if (!Info || !(Info->pointerInfo.pointerFlags & POINTER_FLAG_PRIMARY))
        return;
    if (Route->PromotedFrameId == Frame->FrameId && Route->PromotedMessage == Msg)
        return;
    Route->PromotedFrameId = Frame->FrameId;
    Route->PromotedMessage = Msg;

    Width = max(UserGetSystemMetrics(SM_CXSCREEN), 1);
    Height = max(UserGetSystemMetrics(SM_CYSCREEN), 1);
    Pt = Info->pointerInfo.ptPixelLocation;
    Pt.x = min(max(Pt.x, 0), Width - 1);
    Pt.y = min(max(Pt.y, 0), Height - 1);

    RtlZeroMemory(&Input, sizeof(Input));
    Input.dx = (LONG)((((LONGLONG)Pt.x << 16) + Width - 1) / Width);
    Input.dy = (LONG)((((LONGLONG)Pt.y << 16) + Height - 1) / Height);
    Input.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE;
    if (Msg == WM_POINTERDOWN || Msg == WM_NCPOINTERDOWN)
        Input.dwFlags |= MOUSEEVENTF_LEFTDOWN;
    else if (Msg == WM_POINTERUP || Msg == WM_NCPOINTERUP)
        Input.dwFlags |= MOUSEEVENTF_LEFTUP;
    Input.dwExtraInfo = POINTER_TOUCH_EXTRA_INFO;
    UserSendTouchMouseInput(&Input);
}

BOOL
FASTCALL
PointerGetTouchInfoList(
    _In_ UINT32 PointerId,
    _In_ ULONG PointerType,
    _In_ SIZE_T EntrySize,
    _Inout_ PULONG EntriesCount,
    _Inout_ PULONG PointerCount,
    _Out_ PVOID PointerInfo)
{
    PTHREADINFO pti = PsGetCurrentThreadWin32Thread();
    POINTER_TOUCH_INFO Entry;
    PPOINTER_FRAME Frame;
    PPOINTER_ROUTE Route;
    ULONG Capacity, Entries, Count, Index, Copied = 0;
    SIZE_T CopySize;

    CopySize = (PointerType == PT_TOUCH || PointerType == PT_TOUCHPAD) ? sizeof(POINTER_TOUCH_INFO) : sizeof(POINTER_INFO);
    CopySize = min(CopySize, EntrySize);

    Frame = (pti && pti->MessageQueue) ? PointerFindFrame(pti->MessageQueue->ExtraInfo) : NULL;
    if (!Frame || !PointerFindInFrame(Frame, PointerId))
    {
        EngSetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    _SEH2_TRY
    {
        Capacity = *(volatile ULONG *)PointerCount;
        Entries = *(volatile ULONG *)EntriesCount;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        EngSetLastError(ERROR_NOACCESS);
        _SEH2_YIELD(return FALSE);
    }
    _SEH2_END;

    if (!Entries || !Capacity)
    {
        EngSetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    Count = (Capacity > 1) ? Frame->Count : 1;
    if (Capacity < Count)
    {
        _SEH2_TRY
        {
            *PointerCount = Count;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
        }
        _SEH2_END;
        EngSetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }

    _SEH2_TRY
    {
        ProbeForWrite(PointerInfo, EntrySize * Count, 1);
        for (Index = 0; Index < Frame->Count && Copied < Count; Index++)
        {
            if (Count == 1 && Frame->Info[Index].pointerInfo.pointerId != PointerId)
                continue;
            Entry = Frame->Info[Index];
            Route = PointerTouchRoute(Entry.pointerInfo.pointerId);
            if (Route && Route->Resolved)
                Entry.pointerInfo.hwndTarget = Route->hwndTarget;
            RtlZeroMemory((PUCHAR)PointerInfo + Copied * EntrySize, EntrySize);
            RtlCopyMemory((PUCHAR)PointerInfo + Copied * EntrySize, &Entry, CopySize);
            Copied++;
        }
        *EntriesCount = 1;
        *PointerCount = Copied;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        EngSetLastError(ERROR_NOACCESS);
        _SEH2_YIELD(return FALSE);
    }
    _SEH2_END;

    return TRUE;
}
