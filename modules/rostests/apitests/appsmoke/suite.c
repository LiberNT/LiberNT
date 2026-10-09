/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Runs every application smoke test in a child process under a time budget
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "appsmoke.h"

static const struct
{
    const char *Name;
    DWORD BudgetMs;
} Cases[] =
{
    { "os_version", 10000 },
    { "dll_directory", 15000 },
    { "child_start", 40000 },
    { "alert_wait", 20000 },
    { "yield_fairness", 20000 },
    { "sockets", 20000 },
    { "d3dkmt", 15000 },
    { "saved_bitmap", 10000 },
    { "window_dc", 15000 },
    { "display_mode", 30000 },
    { "swap_chain", 30000 },
    { "d3d12_device", 30000 },
    { "opengl", 20000 },
    { "opengl_mode_change", 30000 },
    { "opencl", 60000 },
};

static void Report(const char *Format, ...)
{
    char Line[256];
    va_list Args;

    va_start(Args, Format);
    _vsnprintf(Line, sizeof(Line) - 1, Format, Args);
    va_end(Args);
    Line[sizeof(Line) - 1] = 0;

    printf("%s", Line);
    fflush(stdout);
    OutputDebugStringA(Line);
}

BOOL AppSmokeIsRole(const char *Role)
{
    char **Arguments;
    int Count = winetest_get_mainargs(&Arguments);

    return Count >= 3 && !strcmp(Arguments[2], Role);
}

BOOL AppSmokeRunSelf(const char *Test, const char *Role, DWORD BudgetMs, DWORD *ExitCode, DWORD *ElapsedMs)
{
    char Path[MAX_PATH], Command[MAX_PATH + 128];
    PROCESS_INFORMATION Process;
    STARTUPINFOA Startup;
    DWORD Start, Wait;
    BOOL InTime = FALSE;

    *ExitCode = ~0u;
    *ElapsedMs = 0;
    if (!GetModuleFileNameA(NULL, Path, sizeof(Path)))
        return FALSE;

    _snprintf(Command, sizeof(Command) - 1, "\"%s\" %s%s%s", Path, Test, Role ? " " : "", Role ? Role : "");
    Command[sizeof(Command) - 1] = 0;

    memset(&Startup, 0, sizeof(Startup));
    Startup.cb = sizeof(Startup);
    fflush(stdout);
    Start = GetTickCount();
    if (!CreateProcessA(NULL, Command, NULL, NULL, TRUE, 0, NULL, NULL, &Startup, &Process))
    {
        *ExitCode = GetLastError();
        return FALSE;
    }

    Wait = WaitForSingleObject(Process.hProcess, BudgetMs);
    if (Wait == WAIT_OBJECT_0)
    {
        InTime = GetExitCodeProcess(Process.hProcess, ExitCode);
    }
    else
    {
        TerminateProcess(Process.hProcess, WAIT_TIMEOUT);
        WaitForSingleObject(Process.hProcess, 2000);
        *ExitCode = WAIT_TIMEOUT;
    }

    *ElapsedMs = GetTickCount() - Start;
    CloseHandle(Process.hThread);
    CloseHandle(Process.hProcess);
    return InTime;
}

static LRESULT CALLBACK WindowProc(HWND Window, UINT Message, WPARAM WParam, LPARAM LParam)
{
    return DefWindowProcW(Window, Message, WParam, LParam);
}

void AppSmokePumpMessages(void)
{
    MSG Message;

    while (PeekMessageW(&Message, NULL, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&Message);
        DispatchMessageW(&Message);
    }
}

HWND AppSmokeCreateWindow(int Width, int Height)
{
    WNDCLASSW Class;
    RECT Rect;
    HWND Window;

    memset(&Class, 0, sizeof(Class));
    Class.style = CS_OWNDC;
    Class.lpfnWndProc = WindowProc;
    Class.hInstance = GetModuleHandleW(NULL);
    Class.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    Class.lpszClassName = L"AppSmokeWindow";
    RegisterClassW(&Class);

    SetRect(&Rect, 0, 0, Width, Height);
    AdjustWindowRect(&Rect, WS_OVERLAPPEDWINDOW, FALSE);
    Window = CreateWindowExW(0, Class.lpszClassName, L"appsmoke", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                             48, 48, Rect.right - Rect.left, Rect.bottom - Rect.top,
                             NULL, NULL, Class.hInstance, NULL);
    if (Window)
    {
        SetForegroundWindow(Window);
        AppSmokePumpMessages();
    }
    return Window;
}

START_TEST(suite)
{
    DWORD ExitCode, Elapsed, Total = GetTickCount();
    UINT Index, Failed = 0;
    BOOL InTime, Wow64 = FALSE;
    const char *Verdict;

    IsWow64Process(GetCurrentProcess(), &Wow64);
    for (Index = 0; Index < ARRAYSIZE(Cases); ++Index)
    {
        InTime = AppSmokeRunSelf(Cases[Index].Name, NULL, Cases[Index].BudgetMs, &ExitCode, &Elapsed);
        if (InTime && !ExitCode)
            Verdict = "PASS";
        else if (ExitCode == WAIT_TIMEOUT)
            Verdict = "TIMEOUT";
        else
            Verdict = "FAIL";

        ok(InTime && !ExitCode, "%s: exit code %#lx after %lu ms (budget %lu ms)\n",
           Cases[Index].Name, ExitCode, Elapsed, Cases[Index].BudgetMs);
        if (!InTime || ExitCode)
            ++Failed;
        Report("APPSMOKE %u-bit%s %s %s exit=%#lx ms=%lu\n", (UINT)sizeof(void *) * 8,
               Wow64 ? " wow64" : "", Cases[Index].Name, Verdict, ExitCode, Elapsed);
    }

    Report("APPSMOKE_DONE %u-bit%s cases=%u failed=%u ms=%lu\n", (UINT)sizeof(void *) * 8,
           Wow64 ? " wow64" : "", (UINT)ARRAYSIZE(Cases), Failed, GetTickCount() - Total);
}
