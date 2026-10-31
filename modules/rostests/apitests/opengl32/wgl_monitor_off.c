/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     OpenGL keeps rendering while the monitor is turned off and on
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include <windows.h>
#include <GL/gl.h>
#include <stdlib.h>
#include <wine/test.h>

#ifndef GL_BGRA_EXT
#define GL_BGRA_EXT 0x80E1
#endif

#define SIZE 64
#define TEX_SIZE 16
#define CYCLES 2
#define TOLERANCE 8

static DWORD texels[TEX_SIZE * TEX_SIZE];
static DWORD first_wrong;
static volatile LONG stop_rendering;
static volatile LONG frames;
static volatile LONG wrong_frames;
static volatile LONG gl_errors;
static BOOL context_ready;

static BOOL ColorNear(DWORD value, DWORD expected)
{
    int shift;

    for (shift = 0; shift < 24; shift += 8)
    {
        if (abs((int)((value >> shift) & 0xFF) - (int)((expected >> shift) & 0xFF)) > TOLERANCE)
            return FALSE;
    }
    return TRUE;
}

static void RenderFrame(HDC dc, DWORD color)
{
    GLuint texture;
    DWORD value = 0;
    unsigned int i;

    for (i = 0; i < TEX_SIZE * TEX_SIZE; i++)
        texels[i] = 0xFF000000 | color;

    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, TEX_SIZE, TEX_SIZE, 0, GL_BGRA_EXT, GL_UNSIGNED_BYTE, texels);
    glEnable(GL_TEXTURE_2D);
    glClear(GL_COLOR_BUFFER_BIT);
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f); glVertex2f(0.0f, 0.0f);
    glTexCoord2f(1.0f, 0.0f); glVertex2f(1.0f, 0.0f);
    glTexCoord2f(1.0f, 1.0f); glVertex2f(1.0f, 1.0f);
    glTexCoord2f(0.0f, 1.0f); glVertex2f(0.0f, 1.0f);
    glEnd();
    glFinish();
    glReadPixels(SIZE / 2, SIZE / 2, 1, 1, GL_BGRA_EXT, GL_UNSIGNED_BYTE, &value);
    glDeleteTextures(1, &texture);
    SwapBuffers(dc);

    if (!ColorNear(value, color))
    {
        if (InterlockedIncrement(&wrong_frames) == 1)
            first_wrong = value;
    }
    if (glGetError() != GL_NO_ERROR)
        InterlockedIncrement(&gl_errors);
    InterlockedIncrement(&frames);
}

static DWORD WINAPI RenderThread(LPVOID parameter)
{
    PIXELFORMATDESCRIPTOR pfd = {sizeof(pfd), 1};
    HANDLE ready = parameter;
    HWND window;
    HGLRC context = NULL;
    HDC dc = NULL;
    DWORD frame = 0;
    int format;

    window = CreateWindowA("static", "wgl_monitor_off", WS_POPUP | WS_VISIBLE, 0, 0, SIZE, SIZE, NULL, NULL, NULL, NULL);
    if (window)
        dc = GetDC(window);
    if (dc)
    {
        pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
        pfd.iPixelType = PFD_TYPE_RGBA;
        pfd.cColorBits = 32;
        format = ChoosePixelFormat(dc, &pfd);
        if (format && SetPixelFormat(dc, format, &pfd))
            context = wglCreateContext(dc);
    }
    context_ready = context && wglMakeCurrent(dc, context);
    SetEvent(ready);

    if (context_ready)
    {
        glViewport(0, 0, SIZE, SIZE);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(0.0, 1.0, 0.0, 1.0, -1.0, 1.0);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        glDrawBuffer(GL_BACK);
        glReadBuffer(GL_BACK);

        while (!stop_rendering)
            RenderFrame(dc, 0x00402060 + ((frame++ & 0x3F) << 8));
        wglMakeCurrent(NULL, NULL);
    }

    if (context)
        wglDeleteContext(context);
    if (dc)
        ReleaseDC(window, dc);
    if (window)
        DestroyWindow(window);
    return 0;
}

START_TEST(wgl_monitor_off)
{
    HWND control;
    HANDLE ready;
    HANDLE thread;
    LONG before;
    int cycle;

    control = CreateWindowA("static", "wgl_monitor_off control", WS_POPUP, 0, 0, 1, 1, NULL, NULL, NULL, NULL);
    ok(control != NULL, "CreateWindow failed: %lu\n", GetLastError());
    ready = CreateEventA(NULL, TRUE, FALSE, NULL);
    thread = CreateThread(NULL, 0, RenderThread, ready, 0, NULL);
    ok(thread != NULL, "CreateThread failed: %lu\n", GetLastError());
    if (!control || !thread)
        return;

    WaitForSingleObject(ready, 30000);
    ok(context_ready, "No OpenGL context\n");
    if (context_ready)
    {
        for (cycle = 0; cycle < CYCLES; cycle++)
        {
            before = frames;
            SendMessageA(control, WM_SYSCOMMAND, SC_MONITORPOWER, 2);
            Sleep(2500);
            ok(frames > before, "Cycle %d: no frames while the monitor is off\n", cycle);

            before = frames;
            SendMessageA(control, WM_SYSCOMMAND, SC_MONITORPOWER, -1);
            mouse_event(MOUSEEVENTF_MOVE, 0, 0, 0, 0);
            Sleep(2500);
            ok(frames > before, "Cycle %d: no frames after the monitor came back\n", cycle);
        }
    }

    InterlockedExchange(&stop_rendering, 1);
    ok(WaitForSingleObject(thread, 30000) == WAIT_OBJECT_0, "Render thread did not stop\n");
    ok(wrong_frames == 0, "%ld of %ld frames read back the wrong color, first 0x%08lx\n", wrong_frames, frames, first_wrong);
    ok(gl_errors == 0, "%ld of %ld frames raised a GL error\n", gl_errors, frames);
    trace("%ld frames\n", frames);

    CloseHandle(thread);
    CloseHandle(ready);
    DestroyWindow(control);
}
