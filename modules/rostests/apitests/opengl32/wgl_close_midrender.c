/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     A process closed or killed while rendering exits promptly and leaves the GPU usable
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <windows.h>
#include <GL/gl.h>
#include <stdio.h>
#include <string.h>
#include <wine/test.h>

#define WINDOW_CLASS "wgl_close_midrender"
#define READY_FRAMES 60
#define FLUSHES_PER_FRAME 8
#define RENDER_BEFORE_CLOSE_MS 1500
#define READY_LIMIT_MS 60000
#define EXIT_LIMIT_MS 10000
#define FOLLOWUP_FRAMES 30
#define FOLLOWUP_LIMIT_MS 30000
#define ROUNDS 4
#define BENCHMARK_RUN_MS 8000

static LRESULT CALLBACK
WindowProc(HWND Window, UINT Message, WPARAM WParam, LPARAM LParam)
{
    if (Message == WM_DESTROY)
    {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(Window, Message, WParam, LParam);
}

static void
DrawFrame(ULONG Frame)
{
    ULONG Flush;

    glClearColor((Frame & 1) ? 0.2f : 0.1f, 0.1f, 0.3f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    for (Flush = 0; Flush < FLUSHES_PER_FRAME; ++Flush)
    {
        glBegin(GL_TRIANGLES);
        glColor3f(1.0f, 0.0f, 0.0f);
        glVertex2f(-1.0f, -1.0f);
        glColor3f(0.0f, 1.0f, 0.0f);
        glVertex2f(1.0f, -1.0f);
        glColor3f(0.0f, 0.0f, 1.0f);
        glVertex2f(0.0f, 1.0f);
        glEnd();
        glFlush();
    }
}

static BOOL
CreateRenderWindow(HWND *Window, HDC *Dc)
{
    PIXELFORMATDESCRIPTOR Pfd = { sizeof(Pfd), 1 };
    WNDCLASSA Class = {0};
    HGLRC Context;
    int Format;

    Class.lpfnWndProc = WindowProc;
    Class.hInstance = GetModuleHandleA(NULL);
    Class.lpszClassName = WINDOW_CLASS;
    RegisterClassA(&Class);
    *Window = CreateWindowA(WINDOW_CLASS, WINDOW_CLASS, WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                            40, 40, 320, 240, NULL, NULL, Class.hInstance, NULL);
    if (*Window == NULL)
        return FALSE;
    *Dc = GetDC(*Window);
    Pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    Pfd.iPixelType = PFD_TYPE_RGBA;
    Pfd.cColorBits = 24;
    Format = ChoosePixelFormat(*Dc, &Pfd);
    if (Format == 0 || !SetPixelFormat(*Dc, Format, &Pfd))
        return FALSE;
    Context = wglCreateContext(*Dc);
    return Context != NULL && wglMakeCurrent(*Dc, Context);
}

static void
RenderUntilClosed(const char *ReadyName)
{
    HANDLE Ready = OpenEventA(EVENT_MODIFY_STATE, FALSE, ReadyName);
    HWND Window;
    HDC Dc;
    ULONG Frame = 0;

    if (Ready == NULL || !CreateRenderWindow(&Window, &Dc))
        ExitProcess(2);
    for (;;)
    {
        MSG Message;

        while (PeekMessageA(&Message, NULL, 0, 0, PM_REMOVE))
        {
            if (Message.message == WM_QUIT)
                ExitProcess(0);
            TranslateMessage(&Message);
            DispatchMessageA(&Message);
        }
        DrawFrame(Frame);
        SwapBuffers(Dc);
        if (++Frame == READY_FRAMES)
            SetEvent(Ready);
    }
}

static void
RenderFewFrames(void)
{
    HWND Window;
    HDC Dc;
    ULONG Frame;

    if (!CreateRenderWindow(&Window, &Dc))
        ExitProcess(2);
    for (Frame = 0; Frame < FOLLOWUP_FRAMES; ++Frame)
    {
        DrawFrame(Frame);
        SwapBuffers(Dc);
    }
    glFinish();
    ExitProcess(0);
}

static BOOL
StartChild(const char *Arguments, PROCESS_INFORMATION *Process)
{
    char **Argv;
    char CommandLine[MAX_PATH + 128];
    STARTUPINFOA Startup = { sizeof(Startup) };

    winetest_get_mainargs(&Argv);
    _snprintf(CommandLine, sizeof(CommandLine), "\"%s\" wgl_close_midrender %s", Argv[0], Arguments);
    CommandLine[sizeof(CommandLine) - 1] = 0;
    return CreateProcessA(NULL, CommandLine, NULL, NULL, FALSE, 0, NULL, NULL, &Startup, Process);
}

static BOOL CALLBACK
PostCloseToWindow(HWND Window, LPARAM Param)
{
    UNREFERENCED_PARAMETER(Param);
    PostMessageA(Window, WM_CLOSE, 0, 0);
    return TRUE;
}

static void
CheckGpuStillRenders(ULONG Round)
{
    PROCESS_INFORMATION Process;
    DWORD Start, Wait, Code = 1;

    ok(StartChild("frames", &Process), "round %lu: follow-up child failed to start: %lu\n", Round, GetLastError());
    if (Process.hProcess == NULL)
        return;
    Start = GetTickCount();
    Wait = WaitForSingleObject(Process.hProcess, FOLLOWUP_LIMIT_MS);
    trace("round %lu: follow-up %lu frames took %lu ms\n", Round, (ULONG)FOLLOWUP_FRAMES, GetTickCount() - Start);
    ok(Wait == WAIT_OBJECT_0, "round %lu: rendering after the close did not finish: %lu\n", Round, Wait);
    if (Wait != WAIT_OBJECT_0)
        TerminateProcess(Process.hProcess, 3);
    else
    {
        GetExitCodeProcess(Process.hProcess, &Code);
        ok(Code == 0, "round %lu: follow-up child exited with %lu\n", Round, Code);
    }
    CloseHandle(Process.hThread);
    CloseHandle(Process.hProcess);
}

static void
RunRound(ULONG Round, BOOL Kill)
{
    PROCESS_INFORMATION Process = {0};
    HANDLE Ready;
    char Name[64], Arguments[96];
    DWORD Wait, Start;

    _snprintf(Name, sizeof(Name), "wgl_close_midrender_%lu_%lu", GetCurrentProcessId(), Round);
    Name[sizeof(Name) - 1] = 0;
    _snprintf(Arguments, sizeof(Arguments), "render %s", Name);
    Arguments[sizeof(Arguments) - 1] = 0;
    Ready = CreateEventA(NULL, TRUE, FALSE, Name);
    ok(StartChild(Arguments, &Process), "round %lu: child failed to start: %lu\n", Round, GetLastError());
    if (Process.hProcess == NULL)
    {
        CloseHandle(Ready);
        return;
    }
    {
        HANDLE Objects[2] = { Ready, Process.hProcess };

        Wait = WaitForMultipleObjects(2, Objects, FALSE, READY_LIMIT_MS);
    }
    ok(Wait == WAIT_OBJECT_0, "round %lu: child did not start rendering: %lu\n", Round, Wait);
    if (Wait == WAIT_OBJECT_0)
    {
        Sleep(RENDER_BEFORE_CLOSE_MS);
        Start = GetTickCount();
        if (Kill)
            TerminateProcess(Process.hProcess, 7);
        else
            EnumThreadWindows(Process.dwThreadId, PostCloseToWindow, 0);
        Wait = WaitForSingleObject(Process.hProcess, EXIT_LIMIT_MS);
        trace("round %lu: %s took %lu ms to exit\n", Round, Kill ? "kill" : "close", GetTickCount() - Start);
        ok(Wait == WAIT_OBJECT_0, "round %lu: process did not exit after %s: %lu\n",
           Round, Kill ? "kill" : "close", Wait);
    }
    if (Wait != WAIT_OBJECT_0)
        TerminateProcess(Process.hProcess, 3);
    CloseHandle(Process.hThread);
    CloseHandle(Process.hProcess);
    CloseHandle(Ready);
    CheckGpuStillRenders(Round);
}

static void
RunBenchmarkRound(void)
{
    PROCESS_INFORMATION Process = {0};
    STARTUPINFOA Startup = { sizeof(Startup) };
    char Path[MAX_PATH];
    DWORD Wait, Start;

    GetSystemDirectoryA(Path, sizeof(Path));
    strncat(Path, "\\glmark2.exe", sizeof(Path) - strlen(Path) - 1);
    if (GetFileAttributesA(Path) == INVALID_FILE_ATTRIBUTES)
    {
        skip("glmark2 is not installed\n");
        return;
    }
    ok(CreateProcessA(Path, NULL, NULL, NULL, FALSE, 0, NULL, NULL, &Startup, &Process),
       "glmark2 failed to start: %lu\n", GetLastError());
    if (Process.hProcess == NULL)
        return;
    Sleep(BENCHMARK_RUN_MS);
    Start = GetTickCount();
    EnumThreadWindows(Process.dwThreadId, PostCloseToWindow, 0);
    Wait = WaitForSingleObject(Process.hProcess, EXIT_LIMIT_MS);
    trace("glmark2 took %lu ms to exit\n", GetTickCount() - Start);
    ok(Wait == WAIT_OBJECT_0, "glmark2 did not exit after close: %lu\n", Wait);
    if (Wait != WAIT_OBJECT_0)
        TerminateProcess(Process.hProcess, 3);
    CloseHandle(Process.hThread);
    CloseHandle(Process.hProcess);
    CheckGpuStillRenders(ROUNDS);
}

START_TEST(wgl_close_midrender)
{
    char **Argv;
    int Argc = winetest_get_mainargs(&Argv);
    ULONG Round;

    if (Argc >= 4 && strcmp(Argv[2], "render") == 0)
    {
        RenderUntilClosed(Argv[3]);
        return;
    }
    if (Argc >= 3 && strcmp(Argv[2], "frames") == 0)
    {
        RenderFewFrames();
        return;
    }
    for (Round = 0; Round < ROUNDS; ++Round)
        RunRound(Round, (Round & 1) != 0);
    RunBenchmarkRound();
}
