/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests which system parameter and cursor calls a window station handle's attribute access allows
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

typedef struct _STATION_CONTEXT
{
    PCSTR Name;
    BOOL Interactive;
    ACCESS_MASK Access;
} STATION_CONTEXT;

static NONCLIENTMETRICSW InitialMetrics;
static RECT InitialWorkArea;
static UINT InitialKeyboardDelay;
static UINT InitialMouseHoverTime;
static UINT InitialDoubleClickTime;
static BOOL InitialBeep;
static BOOL InitialDragFullWindows;
static BOOL InitialMenuAnimation;
static int InitialMouseSpeed;

static
VOID
ExpectResult(
    _In_ PCSTR Context,
    _In_ PCSTR Call,
    _In_ BOOL Result,
    _In_ DWORD Error,
    _In_ DWORD ExpectedError)
{
    if (ExpectedError == ERROR_SUCCESS)
    {
        ok(Result, "%s: %s failed, error %lu\n", Context, Call, Error);
    }
    else
    {
        ok(!Result && Error == ExpectedError,
           "%s: %s returned %d with error %lu, expected a failure with error %lu\n",
           Context, Call, Result, Error, ExpectedError);
    }
}

#define EXPECT_CALL(Call, Expression, ExpectedError) \
    do \
    { \
        BOOL CallResult; \
        DWORD CallError; \
        SetLastError(0xdeadbeef); \
        CallResult = (Expression); \
        CallError = GetLastError(); \
        ExpectResult(Context->Name, Call, CallResult, CallError, ExpectedError); \
    } while (0)

static
VOID
TestCalls(
    _In_ const STATION_CONTEXT *Context)
{
    BOOL Read = (Context->Access & WINSTA_READATTRIBUTES) != 0;
    BOOL Write = (Context->Access & WINSTA_WRITEATTRIBUTES) != 0;
    DWORD ReadError = Read ? ERROR_SUCCESS : ERROR_ACCESS_DENIED;
    DWORD ClipError = (Context->Interactive && Write) ? ERROR_SUCCESS : ERROR_ACCESS_DENIED;
    DWORD CursorInfoError = (Context->Interactive && Read) ? ERROR_SUCCESS : ERROR_ACCESS_DENIED;
    DWORD SetError;
    NONCLIENTMETRICSW Metrics;
    CURSORINFO CursorInfo;
    RECT Rect;
    int Speed;

    if (!Context->Interactive)
        SetError = ERROR_REQUIRES_INTERACTIVE_WINDOWSTATION;
    else
        SetError = Write ? ERROR_SUCCESS : ERROR_ACCESS_DENIED;

    Metrics.cbSize = sizeof(Metrics);
    EXPECT_CALL("SPI_GETNONCLIENTMETRICS", SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(Metrics), &Metrics, 0), ReadError);
    EXPECT_CALL("SPI_GETWORKAREA", SystemParametersInfoW(SPI_GETWORKAREA, 0, &Rect, 0), ReadError);
    EXPECT_CALL("SPI_GETMOUSESPEED", SystemParametersInfoW(SPI_GETMOUSESPEED, 0, &Speed, 0), ReadError);
    EXPECT_CALL("SPI_SETMOUSESPEED", SystemParametersInfoW(SPI_SETMOUSESPEED, 0, (PVOID)(INT_PTR)InitialMouseSpeed, 0), ReadError);
    EXPECT_CALL("GetClipCursor", GetClipCursor(&Rect), ReadError);
    EXPECT_CALL("ClipCursor", ClipCursor(NULL), ClipError);
    CursorInfo.cbSize = sizeof(CursorInfo);
    EXPECT_CALL("GetCursorInfo", GetCursorInfo(&CursorInfo), CursorInfoError);

    EXPECT_CALL("SPI_SETKEYBOARDDELAY", SystemParametersInfoW(SPI_SETKEYBOARDDELAY, InitialKeyboardDelay, NULL, 0), SetError);
    EXPECT_CALL("SPI_SETDOUBLECLICKTIME", SystemParametersInfoW(SPI_SETDOUBLECLICKTIME, InitialDoubleClickTime, NULL, 0), SetError);
    EXPECT_CALL("SPI_SETMOUSEHOVERTIME", SystemParametersInfoW(SPI_SETMOUSEHOVERTIME, InitialMouseHoverTime, NULL, 0), SetError);
    EXPECT_CALL("SPI_SETBEEP", SystemParametersInfoW(SPI_SETBEEP, InitialBeep, NULL, 0), SetError);
    EXPECT_CALL("SPI_SETDRAGFULLWINDOWS", SystemParametersInfoW(SPI_SETDRAGFULLWINDOWS, InitialDragFullWindows, NULL, 0), SetError);
    EXPECT_CALL("SPI_SETMENUANIMATION", SystemParametersInfoW(SPI_SETMENUANIMATION, 0, (PVOID)(INT_PTR)InitialMenuAnimation, 0), SetError);
    EXPECT_CALL("SPI_SETWORKAREA", SystemParametersInfoW(SPI_SETWORKAREA, 0, &InitialWorkArea, 0), SetError);
    EXPECT_CALL("SPI_SETNONCLIENTMETRICS", SystemParametersInfoW(SPI_SETNONCLIENTMETRICS, sizeof(InitialMetrics), &InitialMetrics, 0), SetError);
}

static
VOID
TestThroughHandle(
    _In_ PCWSTR StationName,
    _In_ HWINSTA ReturnTo,
    _In_ const STATION_CONTEXT *Context)
{
    HWINSTA Station;

    Station = OpenWindowStationW(StationName, FALSE, Context->Access | WINSTA_ENUMDESKTOPS);
    ok(Station != NULL, "%s: OpenWindowStationW failed, error %lu\n", Context->Name, GetLastError());
    if (Station == NULL)
        return;

    if (SetProcessWindowStation(Station))
    {
        TestCalls(Context);
        ok(SetProcessWindowStation(ReturnTo), "%s: could not return to the previous window station, error %lu\n", Context->Name, GetLastError());
    }
    else
    {
        ok(FALSE, "%s: SetProcessWindowStation failed, error %lu\n", Context->Name, GetLastError());
    }

    ok(CloseWindowStation(Station), "%s: CloseWindowStation failed, error %lu\n", Context->Name, GetLastError());
}

START_TEST(WindowStationAttributes)
{
    static const STATION_CONTEXT Inherited = { "interactive, inherited", TRUE, WINSTA_READATTRIBUTES | WINSTA_WRITEATTRIBUTES };
    static const STATION_CONTEXT Interactive[] =
    {
        { "interactive, read", TRUE, WINSTA_READATTRIBUTES },
        { "interactive, write", TRUE, WINSTA_WRITEATTRIBUTES },
        { "interactive, neither", TRUE, 0 },
    };
    static const STATION_CONTEXT OtherAll = { "other, all", FALSE, WINSTA_READATTRIBUTES | WINSTA_WRITEATTRIBUTES };
    static const STATION_CONTEXT Other[] =
    {
        { "other, read", FALSE, WINSTA_READATTRIBUTES },
        { "other, write", FALSE, WINSTA_WRITEATTRIBUTES },
        { "other, neither", FALSE, 0 },
    };
    static const WCHAR OtherStationName[] = L"LntAttributeTestStation";
    HWINSTA Original = GetProcessWindowStation();
    HDESK OriginalDesktop = GetThreadDesktop(GetCurrentThreadId());
    WCHAR Name[64];
    HWINSTA OtherStation;
    HDESK OtherDesktop;
    DWORD Needed;
    ULONG Index;

    Name[0] = UNICODE_NULL;
    if (!GetUserObjectInformationW(Original, UOI_NAME, Name, sizeof(Name), &Needed) || _wcsicmp(Name, L"WinSta0") != 0)
    {
        skip("The process does not start on the interactive window station (%ls)\n", Name);
        return;
    }

    InitialMetrics.cbSize = sizeof(InitialMetrics);
    ok(SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(InitialMetrics), &InitialMetrics, 0), "SPI_GETNONCLIENTMETRICS failed, error %lu\n", GetLastError());
    ok(SystemParametersInfoW(SPI_GETWORKAREA, 0, &InitialWorkArea, 0), "SPI_GETWORKAREA failed, error %lu\n", GetLastError());
    ok(SystemParametersInfoW(SPI_GETKEYBOARDDELAY, 0, &InitialKeyboardDelay, 0), "SPI_GETKEYBOARDDELAY failed, error %lu\n", GetLastError());
    ok(SystemParametersInfoW(SPI_GETMOUSEHOVERTIME, 0, &InitialMouseHoverTime, 0), "SPI_GETMOUSEHOVERTIME failed, error %lu\n", GetLastError());
    ok(SystemParametersInfoW(SPI_GETBEEP, 0, &InitialBeep, 0), "SPI_GETBEEP failed, error %lu\n", GetLastError());
    ok(SystemParametersInfoW(SPI_GETDRAGFULLWINDOWS, 0, &InitialDragFullWindows, 0), "SPI_GETDRAGFULLWINDOWS failed, error %lu\n", GetLastError());
    ok(SystemParametersInfoW(SPI_GETMENUANIMATION, 0, &InitialMenuAnimation, 0), "SPI_GETMENUANIMATION failed, error %lu\n", GetLastError());
    ok(SystemParametersInfoW(SPI_GETMOUSESPEED, 0, &InitialMouseSpeed, 0), "SPI_GETMOUSESPEED failed, error %lu\n", GetLastError());
    InitialDoubleClickTime = GetDoubleClickTime();

    TestCalls(&Inherited);
    for (Index = 0; Index < RTL_NUMBER_OF(Interactive); Index++)
        TestThroughHandle(L"WinSta0", Original, &Interactive[Index]);

    OtherStation = CreateWindowStationW(OtherStationName, 0, WINSTA_ALL_ACCESS, NULL);
    if (OtherStation == NULL)
    {
        skip("CreateWindowStationW failed, error %lu\n", GetLastError());
        return;
    }

    ok(SetProcessWindowStation(OtherStation), "SetProcessWindowStation failed, error %lu\n", GetLastError());
    OtherDesktop = CreateDesktopW(L"LntAttributeTestDesktop", NULL, NULL, 0, GENERIC_ALL, NULL);
    ok(OtherDesktop != NULL, "CreateDesktopW failed, error %lu\n", GetLastError());
    if (OtherDesktop != NULL)
        ok(SetThreadDesktop(OtherDesktop), "SetThreadDesktop failed, error %lu\n", GetLastError());

    TestCalls(&OtherAll);
    for (Index = 0; Index < RTL_NUMBER_OF(Other); Index++)
        TestThroughHandle(OtherStationName, OtherStation, &Other[Index]);

    ok(SetProcessWindowStation(Original), "Could not return to the interactive window station, error %lu\n", GetLastError());
    ok(SetThreadDesktop(OriginalDesktop), "Could not return to the original desktop, error %lu\n", GetLastError());
    if (OtherDesktop != NULL)
        ok(CloseDesktop(OtherDesktop), "CloseDesktop failed, error %lu\n", GetLastError());
    ok(CloseWindowStation(OtherStation), "CloseWindowStation failed, error %lu\n", GetLastError());

    TestCalls(&Inherited);
}
