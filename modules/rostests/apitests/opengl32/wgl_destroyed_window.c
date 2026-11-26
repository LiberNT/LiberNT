/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     A context current on another thread survives its window getting WM_DESTROY twice
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <windows.h>
#include <GL/gl.h>
#include <wine/test.h>

static HWND Parent;
static HWND Child;
static HDC ChildDC;
static HGLRC Context;
static LONG DestroyCount;
static HANDLE ParentReady;
static HANDLE DestroyParentNow;
static HANDLE ParentDestroyed;
static HANDLE ContextCurrent;
static HANDLE ReleaseContext;
static BOOL MadeCurrent;
static BOOL Released;
static BOOL Deleted;

static void
pump(DWORD Timeout)
{
    DWORD Deadline = GetTickCount() + Timeout;
    MSG Message;

    do
    {
        while (PeekMessageW(&Message, NULL, 0, 0, PM_REMOVE))
            DispatchMessageW(&Message);
        if ((LONG)(Deadline - GetTickCount()) <= 0)
            break;
        MsgWaitForMultipleObjects(0, NULL, FALSE, 10, QS_ALLINPUT);
    } while (TRUE);
}

static LRESULT CALLBACK
ChildProc(HWND Window, UINT Message, WPARAM wParam, LPARAM lParam)
{
    if (Message == WM_DESTROY && InterlockedIncrement(&DestroyCount) == 1)
    {
        MSG Msg;

        SetEvent(DestroyParentNow);
        while (MsgWaitForMultipleObjects(1, &ParentDestroyed, FALSE, 5000, QS_SENDMESSAGE) == WAIT_OBJECT_0 + 1)
            PeekMessageW(&Msg, NULL, 0, 0, PM_NOREMOVE | PM_QS_SENDMESSAGE);
    }
    return DefWindowProcW(Window, Message, wParam, lParam);
}

static DWORD WINAPI
ParentThread(PVOID Param)
{
    Parent = CreateWindowExW(0, L"DestroyedWindowContextParent", L"Parent", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                             100, 100, 300, 200, NULL, NULL, GetModuleHandleW(NULL), NULL);
    SetEvent(ParentReady);
    while (MsgWaitForMultipleObjects(1, &DestroyParentNow, FALSE, 10000, QS_ALLINPUT) == WAIT_OBJECT_0 + 1)
        pump(0);
    DestroyWindow(Parent);
    SetEvent(ParentDestroyed);
    pump(100);
    return 0;
}

static DWORD WINAPI
ContextThread(PVOID Param)
{
    MadeCurrent = wglMakeCurrent(ChildDC, Context);
    if (MadeCurrent)
        glClear(GL_COLOR_BUFFER_BIT);
    SetEvent(ContextCurrent);
    WaitForSingleObject(ReleaseContext, INFINITE);
    Released = wglMakeCurrent(NULL, NULL);
    Deleted = wglDeleteContext(Context);
    return 0;
}

START_TEST(wgl_destroyed_window)
{
    PIXELFORMATDESCRIPTOR Pfd = { sizeof(Pfd), 1 };
    WNDCLASSW Class = { 0 };
    HANDLE Threads[2];
    DWORD Wait;
    int Format;

    Class.lpfnWndProc = DefWindowProcW;
    Class.hInstance = GetModuleHandleW(NULL);
    Class.lpszClassName = L"DestroyedWindowContextParent";
    RegisterClassW(&Class);
    Class.lpfnWndProc = ChildProc;
    Class.style = CS_OWNDC;
    Class.lpszClassName = L"DestroyedWindowContextChild";
    RegisterClassW(&Class);

    ParentReady = CreateEventW(NULL, TRUE, FALSE, NULL);
    DestroyParentNow = CreateEventW(NULL, TRUE, FALSE, NULL);
    ParentDestroyed = CreateEventW(NULL, TRUE, FALSE, NULL);
    ContextCurrent = CreateEventW(NULL, TRUE, FALSE, NULL);
    ReleaseContext = CreateEventW(NULL, TRUE, FALSE, NULL);

    Threads[0] = CreateThread(NULL, 0, ParentThread, NULL, 0, NULL);
    WaitForSingleObject(ParentReady, 10000);
    Child = CreateWindowExW(0, L"DestroyedWindowContextChild", L"", WS_CHILD | WS_VISIBLE, 0, 0, 100, 100,
                            Parent, NULL, GetModuleHandleW(NULL), NULL);
    ok(Child != NULL, "CreateWindowExW failed, error %lu\n", GetLastError());
    if (!Child)
        goto Done;

    ChildDC = GetDC(Child);
    Pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    Pfd.iPixelType = PFD_TYPE_RGBA;
    Pfd.cColorBits = 32;
    Pfd.iLayerType = PFD_MAIN_PLANE;
    Format = ChoosePixelFormat(ChildDC, &Pfd);
    ok(Format != 0, "ChoosePixelFormat failed, error %lu\n", GetLastError());
    ok(SetPixelFormat(ChildDC, Format, &Pfd), "SetPixelFormat failed, error %lu\n", GetLastError());
    Context = wglCreateContext(ChildDC);
    ok(Context != NULL, "wglCreateContext failed, error %lu\n", GetLastError());
    if (!Context)
        goto Done;

    Threads[1] = CreateThread(NULL, 0, ContextThread, NULL, 0, NULL);
    WaitForSingleObject(ContextCurrent, 10000);
    ok(MadeCurrent, "wglMakeCurrent on the window failed\n");

    pump(50);
    DestroyWindow(Child);
    pump(100);
    WaitForSingleObject(Threads[0], 10000);
    ok(DestroyCount == 2, "the window got %ld WM_DESTROY, expected 2\n", DestroyCount);

    SetEvent(ReleaseContext);
    Wait = WaitForSingleObject(Threads[1], 5000);
    ok(Wait == WAIT_OBJECT_0, "releasing the context of the destroyed window did not return, wait %lu\n", Wait);
    if (Wait == WAIT_OBJECT_0)
    {
        ok(Released, "wglMakeCurrent(NULL, NULL) failed\n");
        ok(Deleted, "wglDeleteContext failed\n");
    }
    CloseHandle(Threads[1]);

Done:
    CloseHandle(Threads[0]);
    CloseHandle(ParentReady);
    CloseHandle(DestroyParentNow);
    CloseHandle(ParentDestroyed);
    CloseHandle(ContextCurrent);
    CloseHandle(ReleaseContext);
}
