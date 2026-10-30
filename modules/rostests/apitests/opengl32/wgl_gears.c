/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Animated lit gears reach the screen; reports their frame rate
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include <windows.h>
#include <GL/gl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <wine/test.h>

#ifndef GL_BGRA_EXT
#define GL_BGRA_EXT 0x80E1
#endif

#define PI 3.14159265f
#define GEAR_COUNT 3
#define RATE_MS 3000
#define SCREEN_POLL_MS 1000
#define SCREEN_COLOR 0x2060C0
#define SCREEN_RGB RGB(SCREEN_COLOR >> 16, (SCREEN_COLOR >> 8) & 0xFF, SCREEN_COLOR & 0xFF)
#define RESIZE_STEP 80

typedef BOOL (WINAPI *PFN_SWAP_INTERVAL)(int);

static const struct
{
    float inner, outer, width, x, y, ratio, phase;
    int teeth;
    float color[4];
}
gears[GEAR_COUNT] =
{
    {1.0f, 4.0f, 1.0f, -3.0f, -2.0f, 1.0f, 0.0f, 20, {0.8f, 0.1f, 0.0f, 1.0f}},
    {0.5f, 2.0f, 2.0f, 3.1f, -2.0f, -2.0f, -9.0f, 10, {0.0f, 0.8f, 0.2f, 1.0f}},
    {1.3f, 2.0f, 0.5f, -3.1f, 4.2f, -2.0f, -25.0f, 10, {0.2f, 0.2f, 1.0f, 1.0f}},
};

static GLuint lists[GEAR_COUNT];

static void Vertex(float radius, float angle, float z)
{
    glVertex3f(radius * cosf(angle), radius * sinf(angle), z);
}

static void Quad(float r0, float a0, float z0, float r1, float a1, float z1)
{
    float dx = r1 * cosf(a1) - r0 * cosf(a0);
    float dy = r1 * sinf(a1) - r0 * sinf(a0);
    float length = sqrtf(dx * dx + dy * dy);

    if (length > 0.0f)
        glNormal3f(dy / length, -dx / length, 0.0f);
    Vertex(r0, a0, z0);
    Vertex(r1, a1, z1);
    Vertex(r1, a1, -z1);
    Vertex(r0, a0, -z0);
}

static void BuildGear(int index)
{
    float inner = gears[index].inner, outer = gears[index].outer + 0.5f, root = gears[index].outer - 0.5f;
    float half = gears[index].width * 0.5f, step = 2.0f * PI / gears[index].teeth;
    int tooth, side;

    glNewList(lists[index], GL_COMPILE);
    glMaterialfv(GL_FRONT, GL_AMBIENT_AND_DIFFUSE, gears[index].color);
    glBegin(GL_QUADS);
    for (tooth = 0; tooth < gears[index].teeth; tooth++)
    {
        float a0 = tooth * step, a1 = a0 + step / 4.0f, a2 = a0 + step / 2.0f, a3 = a0 + 3.0f * step / 4.0f;
        float a4 = a0 + step;

        for (side = -1; side <= 1; side += 2)
        {
            float z = side * half;

            glNormal3f(0.0f, 0.0f, (float)side);
            Vertex(inner, a0, z);
            Vertex(root, a0, z);
            Vertex(root, a4, z);
            Vertex(inner, a4, z);

            Vertex(root, a0, z);
            Vertex(outer, a1, z);
            Vertex(outer, a2, z);
            Vertex(root, a3, z);
        }
        Quad(root, a0, half, outer, a1, half);
        Quad(outer, a1, half, outer, a2, half);
        Quad(outer, a2, half, root, a3, half);
        Quad(root, a3, half, root, a4, half);
        Quad(inner, a4, half, inner, a0, half);
    }
    glEnd();
    glEndList();
}

static void SetupScene(int width, int height)
{
    static const float light[4] = {5.0f, 5.0f, 10.0f, 0.0f};
    float aspect = (float)height / (float)width;
    int index;

    glViewport(0, 0, width, height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-1.0, 1.0, -aspect, aspect, 5.0, 60.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0.0f, 0.0f, -40.0f);
    glLightfv(GL_LIGHT0, GL_POSITION, light);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_NORMALIZE);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glDrawBuffer(GL_BACK);
    glReadBuffer(GL_BACK);

    for (index = 0; index < GEAR_COUNT; index++)
    {
        lists[index] = glGenLists(1);
        BuildGear(index);
    }
    ok(glGetError() == GL_NO_ERROR, "GL error building the gears\n");
}

static void DrawScene(float angle)
{
    int index;

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    for (index = 0; index < GEAR_COUNT; index++)
    {
        glPushMatrix();
        glRotatef(20.0f, 1.0f, 0.0f, 0.0f);
        glRotatef(30.0f, 0.0f, 1.0f, 0.0f);
        glTranslatef(gears[index].x, gears[index].y, 0.0f);
        glRotatef(angle * gears[index].ratio + gears[index].phase, 0.0f, 0.0f, 1.0f);
        glCallList(lists[index]);
        glPopMatrix();
    }
}

static void Pump(void)
{
    MSG message;

    while (PeekMessageA(&message, NULL, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&message);
        DispatchMessageA(&message);
    }
}

static void Test_Scene(int width, int height)
{
    DWORD *first = malloc(width * height * sizeof(DWORD));
    DWORD *second = malloc(width * height * sizeof(DWORD));
    int count = width * height, covered = 0, moved = 0, index;

    if (!first || !second)
    {
        skip("no memory for the pixels\n");
        free(first);
        free(second);
        return;
    }

    DrawScene(0.0f);
    glReadPixels(0, 0, width, height, GL_BGRA_EXT, GL_UNSIGNED_BYTE, first);
    DrawScene(7.0f);
    glReadPixels(0, 0, width, height, GL_BGRA_EXT, GL_UNSIGNED_BYTE, second);
    for (index = 0; index < count; index++)
    {
        covered += (first[index] & 0x00FFFFFF) != 0;
        moved += first[index] != second[index];
    }
    ok(covered > count / 50 && covered < count / 2, "gears cover %d of %d pixels\n", covered, count);
    ok(moved > count / 500, "rotating 7 degrees changed %d of %d pixels\n", moved, count);
    free(first);
    free(second);
}

static void Test_Screen(HDC dc, int x, int y)
{
    HDC screen = GetDC(NULL);
    DWORD start = GetTickCount();
    COLORREF pixel;
    BOOL ret;

    glClearColor(((SCREEN_COLOR >> 16) & 0xFF) / 255.0f, ((SCREEN_COLOR >> 8) & 0xFF) / 255.0f,
                 (SCREEN_COLOR & 0xFF) / 255.0f, 1.0f);
    do
    {
        glClear(GL_COLOR_BUFFER_BIT);
        ret = SwapBuffers(dc);
        Pump();
        pixel = GetPixel(screen, x, y);
    } while (ret && pixel != SCREEN_RGB && GetTickCount() - start < SCREEN_POLL_MS);
    ok(ret, "SwapBuffers failed: %lu\n", GetLastError());
    ok(pixel == SCREEN_RGB, "screen pixel is 0x%06lx\n", pixel);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    ReleaseDC(NULL, screen);
}

static void Test_Rate(HDC dc, const char *name, int width, int height)
{
    LARGE_INTEGER frequency, start, now;
    unsigned int frames = 0;
    float angle = 0.0f;
    double seconds;
    char line[128];
    BOOL ret = TRUE;

    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&start);
    do
    {
        DrawScene(angle);
        ret = SwapBuffers(dc);
        Pump();
        angle += 2.0f;
        frames++;
        QueryPerformanceCounter(&now);
        seconds = (double)(now.QuadPart - start.QuadPart) / frequency.QuadPart;
    } while (ret && seconds * 1000.0 < RATE_MS);
    ok(ret, "SwapBuffers failed at frame %u: %lu\n", frames, GetLastError());
    glFinish();
    ok(glGetError() == GL_NO_ERROR, "GL error after the frame loop\n");

    sprintf(line, "WGL_GEARS %s %dx%d: %u frames in %.2f s = %.1f FPS\n", name, width, height,
            frames, seconds, frames / seconds);
    trace("%s", line);
    OutputDebugStringA(line);
}

static void Test_Resize(HWND window, HDC dc)
{
    POINT origin = {0, 0};
    RECT outer, client;

    GetWindowRect(window, &outer);
    SetWindowPos(window, NULL, 0, 0, outer.right - outer.left - RESIZE_STEP, outer.bottom - outer.top - RESIZE_STEP,
                 SWP_NOMOVE | SWP_NOZORDER);
    Pump();
    GetClientRect(window, &client);
    glViewport(0, 0, client.right, client.bottom);
    ClientToScreen(window, &origin);
    Test_Screen(dc, origin.x + client.right / 2, origin.y + client.bottom / 2);
}

static void RunWindow(const char *name, DWORD style, int x, int y, int width, int height)
{
    PIXELFORMATDESCRIPTOR pfd = {sizeof(pfd), 1};
    RECT rect = {0, 0, width, height};
    POINT origin = {0, 0};
    HWND window;
    HDC dc;
    HGLRC context = NULL;
    PFN_SWAP_INTERVAL swap_interval;
    int format;

    AdjustWindowRectEx(&rect, style, FALSE, WS_EX_TOPMOST);
    window = CreateWindowExA(WS_EX_TOPMOST, "static", name, style, x, y, rect.right - rect.left,
                             rect.bottom - rect.top, NULL, NULL, NULL, NULL);
    ok(window != NULL, "CreateWindow failed: %lu\n", GetLastError());
    if (!window)
        return;
    Pump();
    dc = GetDC(window);
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    format = ChoosePixelFormat(dc, &pfd);
    ok(format != 0, "ChoosePixelFormat failed: %lu\n", GetLastError());
    if (!format || !SetPixelFormat(dc, format, &pfd))
        goto cleanup;
    context = wglCreateContext(dc);
    ok(context != NULL, "wglCreateContext failed: %lu\n", GetLastError());
    if (!context || !wglMakeCurrent(dc, context))
        goto cleanup;
    trace("%s: %s %s\n", name, glGetString(GL_RENDERER), glGetString(GL_VERSION));

    SetupScene(width, height);
    Test_Scene(width, height);
    ClientToScreen(window, &origin);
    Test_Screen(dc, origin.x + width / 2, origin.y + height / 2);
    Test_Rate(dc, name, width, height);
    swap_interval = (PFN_SWAP_INTERVAL)wglGetProcAddress("wglSwapIntervalEXT");
    if (swap_interval && swap_interval(0))
        Test_Rate(dc, "uncapped", width, height);
    Test_Resize(window, dc);

cleanup:
    if (context)
    {
        wglMakeCurrent(NULL, NULL);
        wglDeleteContext(context);
    }
    ReleaseDC(window, dc);
    DestroyWindow(window);
    Pump();
}

START_TEST(wgl_gears)
{
    RunWindow("framed", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 64, 64, 300, 300);
    RunWindow("borderless", WS_POPUP | WS_VISIBLE, 64, 64, 300, 300);
    RunWindow("fullscreen", WS_POPUP | WS_VISIBLE, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
}
