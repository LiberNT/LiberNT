/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests that a thread running a window procedure called back from the kernel reports the callback's context
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <winuser.h>

#define CALLBACK_CLASS L"ThreadContextInCallback"

static volatile LONG spin_stop;
static volatile ULONG_PTR spin_frame;
static HANDLE spin_entered, window_ready;
static HWND spin_window;

static LRESULT CALLBACK spin_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    volatile LONG marker = 0;

    if (msg != WM_APP)
        return DefWindowProcW(hwnd, msg, wparam, lparam);

    spin_frame = (ULONG_PTR)&marker;
    SetEvent(spin_entered);
    while (!InterlockedCompareExchange(&spin_stop, 0, 0))
        marker++;
    return 0;
}

static DWORD WINAPI window_thread(void *arg)
{
    MSG msg;

    spin_window = CreateWindowExW(0, CALLBACK_CLASS, NULL, 0, 0, 0, 0, 0,
                                  HWND_MESSAGE, NULL, GetModuleHandleW(NULL), NULL);
    SetEvent(window_ready);
    if (spin_window == NULL)
        return 1;

    while (GetMessageW(&msg, NULL, 0, 0) > 0)
        DispatchMessageW(&msg);

    DestroyWindow(spin_window);
    return 2;
}

START_TEST(ThreadContextInCallback)
{
#ifdef _M_AMD64
    WNDCLASSW wc = { 0 };
    PIMAGE_NT_HEADERS nt;
    ULONG_PTR image_base;
    CONTEXT ctx;
    HANDLE thread;
    DWORD tid, code = 0;

    wc.lpfnWndProc = spin_proc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = CALLBACK_CLASS;
    ok(RegisterClassW(&wc) != 0, "RegisterClassW failed: %lu\n", GetLastError());

    image_base = (ULONG_PTR)GetModuleHandleW(NULL);
    nt = (PIMAGE_NT_HEADERS)(image_base + ((PIMAGE_DOS_HEADER)image_base)->e_lfanew);

    spin_entered = CreateEventW(NULL, TRUE, FALSE, NULL);
    window_ready = CreateEventW(NULL, TRUE, FALSE, NULL);
    thread = CreateThread(NULL, 0, window_thread, NULL, 0, &tid);
    ok(thread != NULL, "CreateThread failed: %lu\n", GetLastError());
    if (thread == NULL)
        return;

    ok(WaitForSingleObject(window_ready, 5000) == WAIT_OBJECT_0, "window thread did not start\n");
    ok(spin_window != NULL, "CreateWindowExW failed\n");
    if (spin_window == NULL)
    {
        WaitForSingleObject(thread, 5000);
        CloseHandle(thread);
        return;
    }

    /* A message sent from another thread runs as a kernel callback inside GetMessageW */
    ok(SendNotifyMessageW(spin_window, WM_APP, 0, 0), "SendNotifyMessageW failed: %lu\n", GetLastError());
    ok(WaitForSingleObject(spin_entered, 5000) == WAIT_OBJECT_0, "window procedure was not called\n");

    ok(SuspendThread(thread) == 0, "SuspendThread failed: %lu\n", GetLastError());
    memset(&ctx, 0, sizeof(ctx));
    ctx.ContextFlags = CONTEXT_FULL;
    ok(GetThreadContext(thread, &ctx), "GetThreadContext failed: %lu\n", GetLastError());
    ok(ctx.Rip >= image_base && ctx.Rip < image_base + nt->OptionalHeader.SizeOfImage,
       "Rip %p is outside the window procedure's image %p\n", (void *)(ULONG_PTR)ctx.Rip, (void *)image_base);
    ok(ctx.Rsp < spin_frame && spin_frame - ctx.Rsp < 0x1000,
       "Rsp %p is not in the window procedure's frame %p\n", (void *)(ULONG_PTR)ctx.Rsp, (void *)spin_frame);

    InterlockedExchange(&spin_stop, 1);
    ok(ResumeThread(thread) == 1, "ResumeThread failed: %lu\n", GetLastError());
    ok(PostThreadMessageW(tid, WM_QUIT, 0, 0), "PostThreadMessageW failed: %lu\n", GetLastError());
    ok(WaitForSingleObject(thread, 5000) == WAIT_OBJECT_0, "window thread did not return from the callback\n");
    ok(GetExitCodeThread(thread, &code) && code == 2, "window thread exit code %lu\n", code);

    CloseHandle(thread);
    CloseHandle(spin_entered);
    CloseHandle(window_ready);
    UnregisterClassW(CALLBACK_CLASS, GetModuleHandleW(NULL));
#else
    skip("The context checked is the x64 one\n");
#endif
}
