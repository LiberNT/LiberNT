/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Pin power tray icon ownership and notification failure state
 * COPYRIGHT:   Copyright 2026 orhunnx <orhuntopcux@gmail.com>
 */

#include "../../../../dll/shellext/stobject/precomp.h"
#include <stdio.h>
#include <powrprof.h>

static unsigned Failures, Created, Destroyed, Live, Peak, Adds, Modifies, Deletes;
static HICON Icons[5010];
static SYSTEM_POWER_STATUS Status;
static BOOL AllocationFailure;
static HRESULT NextResult = S_OK;
static void (*NotifyHook)(void);
HINSTANCE g_hInstance;

static void Check(const char *Name, BOOL Passed)
{
    printf("%s %s\n", Passed ? "PASS" : "FAIL", Name);
    if (!Passed) ++Failures;
}

static HICON TestLoad(HINSTANCE Instance, UINT Id)
{
    UNREFERENCED_PARAMETER(Id);
    if (AllocationFailure) return NULL;
    HICON Icon = (HICON)LoadImageW(Instance, MAKEINTRESOURCEW(101), IMAGE_ICON, 18, 18, 0);
    if (Icon)
    {
        Icons[Created++] = Icon;
        if (++Live > Peak) Peak = Live;
    }
    return Icon;
}

static BOOL TestDestroy(HICON Icon)
{
    for (unsigned Index = 0; Index < Created; ++Index)
    {
        if (Icons[Index] == Icon)
        {
            BOOL Result = DestroyIcon(Icon);
            Icons[Index] = NULL;
            ++Destroyed;
            --Live;
            return Result;
        }
    }
    Check("power.destroy_owned_icon_once", FALSE);
    return FALSE;
}

static BOOL WINAPI TestStatus(LPSYSTEM_POWER_STATUS Output)
{
    *Output = Status;
    return TRUE;
}

class TestSysTray
{
public:
    HRESULT NotifyIcon(INT Code, UINT Id, HICON Icon, LPCWSTR Tip, DWORD State = 0)
    {
        UNREFERENCED_PARAMETER(Id);
        UNREFERENCED_PARAMETER(Icon);
        UNREFERENCED_PARAMETER(Tip);
        UNREFERENCED_PARAMETER(State);
        if (Code == NIM_ADD) ++Adds;
        if (Code == NIM_MODIFY) ++Modifies;
        if (Code == NIM_DELETE) ++Deletes;
        HRESULT Result = NextResult;
        NextResult = S_OK;
        if (NotifyHook)
        {
            void (*Hook)(void) = NotifyHook;
            NotifyHook = NULL;
            Hook();
        }
        return Result;
    }
    HWND GetHWnd() { return NULL; }
    VOID EnableService(DWORD, BOOL) {}
    BOOL IsServiceEnabled(DWORD) { return TRUE; }
    static VOID RunDll(PCSTR, PCSTR) {}
};

#define CSysTray TestSysTray
#define StoLoadTrayIcon TestLoad
#define GetSystemPowerStatus TestStatus
#define DestroyIcon TestDestroy
#include "../../../../dll/shellext/stobject/power.cpp"
#undef DestroyIcon
#undef GetSystemPowerStatus
#undef StoLoadTrayIcon
#undef CSysTray

static TestSysTray Tray;

static void Reset(void)
{
    for (unsigned Index = 0; Index < Created; ++Index)
        if (Icons[Index]) TestDestroy(Icons[Index]);
    Created = Destroyed = Live = Peak = Adds = Modifies = Deletes = 0;
    AllocationFailure = FALSE;
    NextResult = S_OK;
    NotifyHook = NULL;
    ZeroMemory(&Status, sizeof(Status));
    Status.BatteryLifePercent = 50;
    Status.BatteryLifeTime = 3600;
    g_bPowerIconShown = FALSE;
}

static void NestedShutdown(void)
{
    Power_Shutdown(&Tray);
}

static void NestedInit(void)
{
    Power_Init(&Tray);
}

int RunPowerLifetimeTests(void)
{
    g_hInstance = GetModuleHandleW(NULL);
    Reset();
    printf("CASE power_5000_polls_remain_bounded\n");
    Check("power.init", SUCCEEDED(Power_Init(&Tray)));
    for (unsigned Index = 0; Index < 5000; ++Index) Power_Update(&Tray);
    Check("power.single_add", Adds == 1 && Modifies == 5000);
    Check("power.bounded_owned_handles", Peak <= 1);
    Power_Shutdown(&Tray);
    Check("power.shutdown_releases_all", Live == 0 && Created == Destroyed);
    Check("power.single_delete", Deletes == 1);
    printf("POWER_COUNTS created=%u destroyed=%u peak=%u live=%u\n", Created, Destroyed, Peak, Live);
    Reset();

    printf("CASE failed_add_is_retried\n");
    NextResult = E_FAIL;
    Check("power.failed_add_status", FAILED(Power_Init(&Tray)));
    Check("power.failed_add_not_shown", !g_bPowerIconShown);
    Power_Update(&Tray);
    Check("power.failed_add_retried_as_add", Adds == 2 && Modifies == 0);
    Power_Shutdown(&Tray);
    Check("power.failed_add_releases_all", Live == 0);
    Reset();

    printf("CASE icon_load_failure_does_not_publish_null\n");
    AllocationFailure = TRUE;
    Check("power.load_failure_status", FAILED(Power_Init(&Tray)));
    Check("power.load_failure_no_notify", Adds == 0 && Modifies == 0);
    Check("power.load_failure_not_shown", !g_bPowerIconShown);
    Reset();

    printf("CASE shutdown_during_add\n");
    NotifyHook = NestedShutdown;
    Power_Init(&Tray);
    Check("power.nested_shutdown_visibility", !g_bPowerIconShown && Deletes == 1);
    Check("power.nested_shutdown_releases_all", Live == 0);
    Reset();

    printf("CASE failed_outer_add_preserves_nested_success\n");
    NextResult = E_FAIL;
    NotifyHook = NestedInit;
    Check("power.outer_failure_status", FAILED(Power_Init(&Tray)));
    Check("power.nested_success_remains_shown", g_bPowerIconShown && Adds == 2);
    Power_Shutdown(&Tray);
    Check("power.nested_init_releases_all", Live == 0);
    Reset();

    printf("CASE no_battery_hides_without_loading\n");
    Power_Init(&Tray);
    unsigned Before = Created;
    Status.BatteryFlag = BATTERY_FLAG_NO_BATTERY;
    Power_Update(&Tray);
    Power_Update(&Tray);
    Power_Shutdown(&Tray);
    Check("power.no_battery_no_load", Created == Before);
    Check("power.no_battery_single_delete", Deletes == 1 && !g_bPowerIconShown);
    Check("power.no_battery_releases_all", Live == 0);
    Reset();
    printf("POWER_RESULT failures=%u\n", Failures);
    return Failures;
}
