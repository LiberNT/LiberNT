/*
 * PROJECT:     LiberNT Win32k
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     StickyKeys, FilterKeys, ToggleKeys and MouseKeys for hardware input
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <win32k.h>

typedef struct _ACCESS_MODIFIER
{
    WORD Vk;
    WORD Scan;
    BOOL Extended;
    DWORD Latched;
    DWORD Locked;
} ACCESS_MODIFIER;

static const ACCESS_MODIFIER AccessModifiers[] =
{
    { VK_LSHIFT, 0x2A, FALSE, SKF_LSHIFTLATCHED, SKF_LSHIFTLOCKED },
    { VK_RSHIFT, 0x36, TRUE, SKF_RSHIFTLATCHED, SKF_RSHIFTLOCKED },
    { VK_LCONTROL, 0x1D, FALSE, SKF_LCTLLATCHED, SKF_LCTLLOCKED },
    { VK_RCONTROL, 0x1D, TRUE, SKF_RCTLLATCHED, SKF_RCTLLOCKED },
    { VK_LMENU, 0x38, FALSE, SKF_LALTLATCHED, SKF_LALTLOCKED },
    { VK_RMENU, 0x38, TRUE, SKF_RALTLATCHED, SKF_RALTLOCKED },
    { VK_LWIN, 0x5B, TRUE, SKF_LWINLATCHED, SKF_LWINLOCKED },
    { VK_RWIN, 0x5C, TRUE, SKF_RWINLATCHED, SKF_RWINLOCKED },
};

#define ACCESS_KEY_INDEX(Input) \
    (((Input)->wScan & 0x7F) | (((Input)->dwFlags & KEYEVENTF_EXTENDEDKEY) ? 0x80 : 0))

static BOOL gAccessKeyDown[256];
static BOOL gAccessKeyDelivered[256];
static BOOL gStickyPressed[_countof(AccessModifiers)];
static BOOL gStickyChord[_countof(AccessModifiers)];
static BOOL gStickyCycled[_countof(AccessModifiers)];

static UINT gFilterLastUpKey;
static DWORD gFilterLastUpTime;
static DWORD gFilterDownTime[256];
static DWORD gFilterRepeatTime[256];
static BOOL gFilterPending;
static KEYBDINPUT gFilterPendingInput;
static UINT_PTR gFilterTimer;

static DWORD gMouseKeysStartTime;
static DWORD gMouseKeysLastTime;

static VOID AccessDeliver(KEYBDINPUT *pKbdInput);

static
VOID
AccessSound(UINT uType)
{
    if (hwndSAS)
        UserPostMessage(hwndSAS, WM_LOGONNOTIFY, LN_MESSAGE_BEEP, uType);
}

static
VOID
StickySendModifier(UINT Index, BOOL bUp)
{
    KEYBDINPUT KbdInput;

    RtlZeroMemory(&KbdInput, sizeof(KbdInput));
    KbdInput.wVk = AccessModifiers[Index].Vk;
    KbdInput.wScan = AccessModifiers[Index].Scan;
    KbdInput.dwFlags = (AccessModifiers[Index].Extended ? KEYEVENTF_EXTENDEDKEY : 0) | (bUp ? KEYEVENTF_KEYUP : 0);
    UserSendKeyboardInput(&KbdInput, FALSE);
}

static
INT
StickyFindModifier(WORD wVk)
{
    UINT i;

    for (i = 0; i < _countof(AccessModifiers); i++)
    {
        if (AccessModifiers[i].Vk == wVk)
            return (INT)i;
    }
    return -1;
}

static
VOID
StickyRelease(BOOL bLocked)
{
    UINT i;

    for (i = 0; i < _countof(AccessModifiers); i++)
    {
        DWORD State = gspv.stickykeys.dwFlags & (AccessModifiers[i].Latched | AccessModifiers[i].Locked);

        if (!State || (!bLocked && (State & AccessModifiers[i].Locked)))
            continue;
        gspv.stickykeys.dwFlags &= ~State;
        if (!gStickyPressed[i])
            StickySendModifier(i, TRUE);
    }
}

static
VOID
StickyProcess(KEYBDINPUT *pKbdInput)
{
    BOOL bUp = (pKbdInput->dwFlags & KEYEVENTF_KEYUP) != 0;
    INT Index = StickyFindModifier(pKbdInput->wVk);
    DWORD Latched, Locked;
    UINT i;

    if (Index < 0)
    {
        UserSendKeyboardInput(pKbdInput, FALSE);
        if (!bUp)
        {
            for (i = 0; i < _countof(AccessModifiers); i++)
            {
                if (gStickyPressed[i])
                    gStickyChord[i] = TRUE;
            }
            StickyRelease(FALSE);
        }
        return;
    }

    Latched = AccessModifiers[Index].Latched;
    Locked = AccessModifiers[Index].Locked;
    if (!bUp)
    {
        if (gStickyPressed[Index])
        {
            if (!(gspv.stickykeys.dwFlags & (Latched | Locked)) && !gStickyCycled[Index])
                UserSendKeyboardInput(pKbdInput, FALSE);
            return;
        }
        gStickyPressed[Index] = TRUE;
        gStickyChord[Index] = FALSE;
        gStickyCycled[Index] = FALSE;
        for (i = 0; i < _countof(AccessModifiers); i++)
        {
            if (i != (UINT)Index && gStickyPressed[i])
            {
                gStickyChord[i] = TRUE;
                gStickyChord[Index] = TRUE;
            }
        }
        if (gspv.stickykeys.dwFlags & Latched)
        {
            gStickyCycled[Index] = TRUE;
            gspv.stickykeys.dwFlags &= ~Latched;
            if (gspv.stickykeys.dwFlags & SKF_TRISTATE)
            {
                gspv.stickykeys.dwFlags |= Locked;
            }
            else
            {
                StickySendModifier((UINT)Index, TRUE);
            }
            if (gspv.stickykeys.dwFlags & SKF_AUDIBLEFEEDBACK)
                AccessSound((gspv.stickykeys.dwFlags & Locked) ? MB_ICONASTERISK : MB_OK);
            return;
        }
        if (gspv.stickykeys.dwFlags & Locked)
        {
            gStickyCycled[Index] = TRUE;
            gspv.stickykeys.dwFlags &= ~Locked;
            StickySendModifier((UINT)Index, TRUE);
            if (gspv.stickykeys.dwFlags & SKF_AUDIBLEFEEDBACK)
                AccessSound(MB_OK);
            return;
        }
        UserSendKeyboardInput(pKbdInput, FALSE);
        return;
    }

    gStickyPressed[Index] = FALSE;
    if (gStickyCycled[Index])
        return;
    if (gStickyChord[Index])
    {
        UserSendKeyboardInput(pKbdInput, FALSE);
        if (gspv.stickykeys.dwFlags & SKF_TWOKEYSOFF)
        {
            StickyRelease(TRUE);
            gspv.stickykeys.dwFlags &= ~SKF_STICKYKEYSON;
        }
        return;
    }
    gspv.stickykeys.dwFlags |= Latched;
    if (gspv.stickykeys.dwFlags & SKF_AUDIBLEFEEDBACK)
        AccessSound(MB_OK);
}

static
VOID
MouseKeysSend(DWORD dwFlags, LONG dx, LONG dy)
{
    MOUSEINPUT mi;

    RtlZeroMemory(&mi, sizeof(mi));
    mi.dx = dx;
    mi.dy = dy;
    mi.dwFlags = dwFlags;
    UserSendMouseInput(&mi, FALSE);
}

static
BOOL
MouseKeysButton(UINT Key, BOOL bUp, BOOL bRepeat)
{
    DWORD Selected, Down = 0, Up = 0;

    switch (Key)
    {
        case 0x80 | 0x35:
        case 0x37:
        case 0x4A:
            if (!bUp)
            {
                Selected = Key == 0x4A ? MKF_RIGHTBUTTONSEL :
                           Key == 0x37 ? MKF_LEFTBUTTONSEL | MKF_RIGHTBUTTONSEL : MKF_LEFTBUTTONSEL;
                gspv.mousekeys.dwFlags = (gspv.mousekeys.dwFlags & ~(MKF_LEFTBUTTONSEL | MKF_RIGHTBUTTONSEL)) | Selected;
            }
            return TRUE;
        case 0x4C:
        case 0x4E:
        case 0x52:
        case 0x53:
            break;
        default:
            return FALSE;
    }
    if (bUp || bRepeat)
        return TRUE;

    Selected = gspv.mousekeys.dwFlags & (MKF_LEFTBUTTONSEL | MKF_RIGHTBUTTONSEL);
    if (!Selected)
        Selected = MKF_LEFTBUTTONSEL;
    if (Selected & MKF_LEFTBUTTONSEL)
    {
        Down |= MOUSEEVENTF_LEFTDOWN;
        Up |= MOUSEEVENTF_LEFTUP;
    }
    if (Selected & MKF_RIGHTBUTTONSEL)
    {
        Down |= MOUSEEVENTF_RIGHTDOWN;
        Up |= MOUSEEVENTF_RIGHTUP;
    }

    switch (Key)
    {
        case 0x4C:
            MouseKeysSend(Down, 0, 0);
            MouseKeysSend(Up, 0, 0);
            break;
        case 0x4E:
            MouseKeysSend(Down, 0, 0);
            MouseKeysSend(Up, 0, 0);
            MouseKeysSend(Down, 0, 0);
            MouseKeysSend(Up, 0, 0);
            break;
        case 0x52:
            MouseKeysSend(Down, 0, 0);
            gspv.mousekeys.dwFlags |= ((Selected & MKF_LEFTBUTTONSEL) ? MKF_LEFTBUTTONDOWN : 0) |
                                      ((Selected & MKF_RIGHTBUTTONSEL) ? MKF_RIGHTBUTTONDOWN : 0);
            break;
        case 0x53:
            Up = ((gspv.mousekeys.dwFlags & MKF_LEFTBUTTONDOWN) ? MOUSEEVENTF_LEFTUP : 0) |
                 ((gspv.mousekeys.dwFlags & MKF_RIGHTBUTTONDOWN) ? MOUSEEVENTF_RIGHTUP : 0);
            if (Up)
                MouseKeysSend(Up, 0, 0);
            gspv.mousekeys.dwFlags &= ~(MKF_LEFTBUTTONDOWN | MKF_RIGHTBUTTONDOWN);
            break;
    }
    return TRUE;
}

static
BOOL
MouseKeysProcess(KEYBDINPUT *pKbdInput, UINT Key, BOOL bRepeat)
{
    static const struct { UINT Key; SHORT Dx; SHORT Dy; } Moves[] =
    {
        { 0x47, -1, -1 }, { 0x48, 0, -1 }, { 0x49, 1, -1 }, { 0x4B, -1, 0 },
        { 0x4D, 1, 0 }, { 0x4F, -1, 1 }, { 0x50, 0, 1 }, { 0x51, 1, 1 },
    };
    BOOL bUp = (pKbdInput->dwFlags & KEYEVENTF_KEYUP) != 0;
    DWORD Now = EngGetTickCount32(), TimeToMax, Elapsed, Step = 1;
    ULONG Speed;
    UINT i;

    if (!(gspv.mousekeys.dwFlags & MKF_MOUSEKEYSON))
        return FALSE;
    if (((gspv.mousekeys.dwFlags & MKF_REPLACENUMBERS) != 0) != IS_KEY_LOCKED(gafAsyncKeyState, VK_NUMLOCK))
    {
        gspv.mousekeys.dwFlags &= ~MKF_MOUSEMODE;
        return FALSE;
    }
    gspv.mousekeys.dwFlags |= MKF_MOUSEMODE;
    if (MouseKeysButton(Key, bUp, bRepeat))
        return TRUE;

    for (i = 0; i < _countof(Moves); i++)
    {
        if (Moves[i].Key == Key)
            break;
    }
    if (i == _countof(Moves))
        return FALSE;
    if (bUp)
        return TRUE;

    if (!bRepeat)
    {
        gMouseKeysStartTime = Now;
    }
    else
    {
        TimeToMax = max(gspv.mousekeys.iTimeToMaxSpeed, 1);
        Elapsed = min(Now - gMouseKeysStartTime, TimeToMax);
        Speed = 10 + (ULONG)(((ULONGLONG)(max(gspv.mousekeys.iMaxSpeed, 10) - 10) * Elapsed) / TimeToMax);
        if ((gspv.mousekeys.dwFlags & MKF_MODIFIERS) && IS_KEY_DOWN(gafAsyncKeyState, VK_CONTROL))
            Speed *= max(gspv.mousekeys.iCtrlSpeed, 1);
        Step = max(1, (Speed * min(Now - gMouseKeysLastTime, 100)) / 1000);
        if ((gspv.mousekeys.dwFlags & MKF_MODIFIERS) && IS_KEY_DOWN(gafAsyncKeyState, VK_SHIFT))
            Step = 1;
    }
    gMouseKeysLastTime = Now;
    MouseKeysSend(MOUSEEVENTF_MOVE, Moves[i].Dx * (LONG)Step, Moves[i].Dy * (LONG)Step);
    return TRUE;
}

static
VOID
CALLBACK
FilterSlowKeysTimer(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime)
{
    KEYBDINPUT KbdInput;
    UINT Key;

    if (!gFilterPending)
        return;
    gFilterPending = FALSE;
    KbdInput = gFilterPendingInput;
    Key = ACCESS_KEY_INDEX(&KbdInput);
    if (!(gspv.filterkeys.dwFlags & FKF_FILTERKEYSON) || !gAccessKeyDown[Key])
        return;
    gFilterDownTime[Key] = EngGetTickCount32();
    gFilterRepeatTime[Key] = gFilterDownTime[Key];
    if (gspv.filterkeys.dwFlags & FKF_CLICKON)
        AccessSound(MB_OK);
    AccessDeliver(&KbdInput);
}

static
BOOL
FilterProcess(KEYBDINPUT *pKbdInput, UINT Key, BOOL bRepeat)
{
    BOOL bUp = (pKbdInput->dwFlags & KEYEVENTF_KEYUP) != 0;
    DWORD Now = EngGetTickCount32();

    if (!(gspv.filterkeys.dwFlags & FKF_FILTERKEYSON))
        return FALSE;

    if (gFilterPending && ACCESS_KEY_INDEX(&gFilterPendingInput) == Key)
    {
        if (bUp)
            gFilterPending = FALSE;
        return TRUE;
    }

    if (bUp)
    {
        gFilterLastUpKey = Key;
        gFilterLastUpTime = Now;
        return FALSE;
    }

    if (!bRepeat)
    {
        if (gspv.filterkeys.iBounceMSec)
            return Key == gFilterLastUpKey && Now - gFilterLastUpTime < gspv.filterkeys.iBounceMSec;
        if (gspv.filterkeys.dwFlags & FKF_CLICKON)
            AccessSound(MB_OK);
        if (gspv.filterkeys.iWaitMSec)
        {
            gFilterPendingInput = *pKbdInput;
            gFilterTimer = IntSetTimer(NULL, gFilterTimer, gspv.filterkeys.iWaitMSec, FilterSlowKeysTimer,
                                       TMRF_RIT | TMRF_ONESHOT);
            gFilterPending = gFilterTimer != 0;
            return gFilterPending;
        }
        gFilterDownTime[Key] = Now;
        gFilterRepeatTime[Key] = Now;
        return FALSE;
    }

    if (gspv.filterkeys.iBounceMSec || !gspv.filterkeys.iRepeatMSec)
        return FALSE;
    if (Now - gFilterDownTime[Key] < gspv.filterkeys.iDelayMSec ||
        Now - gFilterRepeatTime[Key] < gspv.filterkeys.iRepeatMSec)
    {
        return TRUE;
    }
    gFilterRepeatTime[Key] = Now;
    return FALSE;
}

static
VOID
AccessDeliver(KEYBDINPUT *pKbdInput)
{
    BOOL bUp = (pKbdInput->dwFlags & KEYEVENTF_KEYUP) != 0;
    WORD wVk = pKbdInput->wVk;
    UINT Key = ACCESS_KEY_INDEX(pKbdInput);

    if (bUp && !gAccessKeyDelivered[Key])
        return;
    gAccessKeyDelivered[Key] = !bUp;
    if (gspv.stickykeys.dwFlags & SKF_STICKYKEYSON)
        StickyProcess(pKbdInput);
    else
        UserSendKeyboardInput(pKbdInput, FALSE);

    if (!bUp && (gspv.togglekeys.dwFlags & TKF_TOGGLEKEYSON) &&
        (wVk == VK_CAPITAL || wVk == VK_NUMLOCK || wVk == VK_SCROLL))
    {
        AccessSound(IS_KEY_LOCKED(gafAsyncKeyState, wVk) ? MB_ICONASTERISK : MB_OK);
    }
}

VOID
NTAPI
UserAccessKeyboardInput(KEYBDINPUT *pKbdInput)
{
    UINT Key = ACCESS_KEY_INDEX(pKbdInput);
    BOOL bUp = (pKbdInput->dwFlags & KEYEVENTF_KEYUP) != 0;
    BOOL bRepeat = !bUp && gAccessKeyDown[Key];
    BOOL bConsumed;

    bConsumed = MouseKeysProcess(pKbdInput, Key, bRepeat);
    if (!bConsumed)
        bConsumed = FilterProcess(pKbdInput, Key, bRepeat);
    gAccessKeyDown[Key] = !bUp;
    if (!bConsumed)
        AccessDeliver(pKbdInput);
}

VOID
NTAPI
UserAccessMouseButtonUp(VOID)
{
    if (gspv.stickykeys.dwFlags & SKF_STICKYKEYSON)
        StickyRelease(FALSE);
}

VOID
NTAPI
UserAccessSettingsChanged(VOID)
{
    if (!(gspv.stickykeys.dwFlags & SKF_STICKYKEYSON))
        StickyRelease(TRUE);
    if (!(gspv.filterkeys.dwFlags & FKF_FILTERKEYSON))
        gFilterPending = FALSE;
    if (!(gspv.mousekeys.dwFlags & MKF_MOUSEKEYSON))
        gspv.mousekeys.dwFlags &= ~(MKF_LEFTBUTTONDOWN | MKF_RIGHTBUTTONDOWN | MKF_MOUSEMODE);
}
