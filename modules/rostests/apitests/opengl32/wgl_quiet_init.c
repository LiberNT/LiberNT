/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Creating and using an OpenGL context writes nothing to the process's stderr
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <windows.h>
#include <GL/gl.h>
#include <stdio.h>
#include <string.h>
#include <wine/test.h>

#define WINDOW_CLASS "wgl_quiet_init"
#define CHILD_LIMIT_MS 60000

static void
RenderOnce(void)
{
    PIXELFORMATDESCRIPTOR Pfd = { sizeof(Pfd), 1 };
    WNDCLASSA Class = {0};
    HWND Window;
    HDC Dc;
    HGLRC Context;
    int Format;
    BOOL Rendered = FALSE;

    Class.lpfnWndProc = DefWindowProcA;
    Class.hInstance = GetModuleHandleA(NULL);
    Class.lpszClassName = WINDOW_CLASS;
    RegisterClassA(&Class);
    Window = CreateWindowA(WINDOW_CLASS, WINDOW_CLASS, WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, NULL, NULL,
                           Class.hInstance, NULL);
    Dc = Window ? GetDC(Window) : NULL;
    Pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    Pfd.iPixelType = PFD_TYPE_RGBA;
    Pfd.cColorBits = 24;
    Pfd.cAlphaBits = 8;
    Pfd.cDepthBits = 24;
    Format = Dc ? ChoosePixelFormat(Dc, &Pfd) : 0;
    if (Format && SetPixelFormat(Dc, Format, &Pfd))
    {
        Context = wglCreateContext(Dc);
        if (Context && wglMakeCurrent(Dc, Context))
        {
            Rendered = glGetString(GL_RENDERER) != NULL;
            glClearColor(0.2f, 0.4f, 0.6f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            glFinish();
            SwapBuffers(Dc);
            wglMakeCurrent(NULL, NULL);
        }
        if (Context)
            wglDeleteContext(Context);
    }
    if (Dc)
        ReleaseDC(Window, Dc);
    if (Window)
        DestroyWindow(Window);
    ExitProcess(Rendered ? 0 : 2);
}

START_TEST(wgl_quiet_init)
{
    SECURITY_ATTRIBUTES Inherit = { sizeof(Inherit), NULL, TRUE };
    STARTUPINFOA Startup = { sizeof(Startup) };
    PROCESS_INFORMATION Process;
    char CommandLine[MAX_PATH + 64];
    char Output[512];
    HANDLE Read, Write, Null;
    DWORD Total = 0, Got, Code = 1;
    char **Argv;
    int Argc = winetest_get_mainargs(&Argv);

    if (Argc >= 3 && strcmp(Argv[2], "child") == 0)
    {
        RenderOnce();
        return;
    }

    ok(CreatePipe(&Read, &Write, &Inherit, 0), "CreatePipe failed: %lu\n", GetLastError());
    Null = CreateFileA("NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &Inherit, OPEN_EXISTING, 0, NULL);
    ok(Null != INVALID_HANDLE_VALUE, "opening NUL failed: %lu\n", GetLastError());
    _snprintf(CommandLine, sizeof(CommandLine), "\"%s\" wgl_quiet_init child", Argv[0]);
    CommandLine[sizeof(CommandLine) - 1] = 0;
    Startup.dwFlags = STARTF_USESTDHANDLES;
    Startup.hStdInput = Null;
    Startup.hStdOutput = Null;
    Startup.hStdError = Write;
    ok(CreateProcessA(NULL, CommandLine, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &Startup, &Process),
       "CreateProcess failed: %lu\n", GetLastError());
    CloseHandle(Write);
    CloseHandle(Null);

    Output[0] = 0;
    while (ReadFile(Read, Output + Total, sizeof(Output) - 1 - Total, &Got, NULL) && Got != 0)
    {
        Total += Got;
        if (Total == sizeof(Output) - 1)
            break;
    }
    Output[Total] = 0;
    CloseHandle(Read);

    ok(WaitForSingleObject(Process.hProcess, CHILD_LIMIT_MS) == WAIT_OBJECT_0, "the child did not exit\n");
    GetExitCodeProcess(Process.hProcess, &Code);
    ok(Code == 0, "the child could not render, exit code %lu\n", Code);
    ok(Total == 0, "the child wrote %lu bytes to stderr: %s\n", Total, Output);
    CloseHandle(Process.hProcess);
    CloseHandle(Process.hThread);
}
