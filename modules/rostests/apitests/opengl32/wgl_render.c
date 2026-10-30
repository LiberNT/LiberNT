/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     OpenGL 1.1 clears, triangles, textures and depth read back exactly
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include <windows.h>
#include <GL/gl.h>
#include <stdlib.h>
#include <wine/test.h>

#ifndef GL_BGRA_EXT
#define GL_BGRA_EXT 0x80E1
#endif

#define SIZE 128
#define TEX_SIZE 64

static DWORD pixels[SIZE * SIZE];

static DWORD ReadPixel(int x, int y)
{
    DWORD value = 0;

    glReadPixels(x, y, 1, 1, GL_BGRA_EXT, GL_UNSIGNED_BYTE, &value);
    return value & 0x00FFFFFF;
}

static BOOL ColorNear(DWORD value, DWORD expected, int tolerance)
{
    int shift;

    for (shift = 0; shift < 24; shift += 8)
    {
        if (abs((int)((value >> shift) & 0xFF) - (int)((expected >> shift) & 0xFF)) > tolerance)
            return FALSE;
    }
    return TRUE;
}

static unsigned int CountPixelsNear(DWORD expected, int tolerance)
{
    unsigned int count = 0;
    unsigned int i;

    glReadPixels(0, 0, SIZE, SIZE, GL_BGRA_EXT, GL_UNSIGNED_BYTE, pixels);
    for (i = 0; i < SIZE * SIZE; i++)
        count += ColorNear(pixels[i], expected, tolerance);
    return count;
}

static unsigned int CountPixels(DWORD expected)
{
    return CountPixelsNear(expected, 0);
}

static void Test_Clear(void)
{
    glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ok(CountPixels(0xFF0000) == SIZE * SIZE, "red clear: pixel 0 is 0x%06lx\n", pixels[0] & 0x00FFFFFF);

    glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ok(CountPixels(0x0000FF) == SIZE * SIZE, "blue clear: pixel 0 is 0x%06lx\n", pixels[0] & 0x00FFFFFF);
}

static void Test_Triangle(void)
{
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glColor3f(0.0f, 1.0f, 0.0f);
    glBegin(GL_TRIANGLES);
    glVertex2f(0.0f, 0.0f);
    glVertex2f(1.0f, 0.0f);
    glVertex2f(0.0f, 1.0f);
    glEnd();

    ok(ReadPixel(8, 8) == 0x00FF00, "inside the triangle: 0x%06lx\n", ReadPixel(8, 8));
    ok(ReadPixel(SIZE - 8, SIZE - 8) == 0x000000, "outside the triangle: 0x%06lx\n",
       ReadPixel(SIZE - 8, SIZE - 8));
    ok(CountPixels(0x00FF00) > SIZE * SIZE * 45 / 100 && CountPixels(0x00FF00) < SIZE * SIZE * 55 / 100,
       "triangle covers %u of %u pixels\n", CountPixels(0x00FF00), SIZE * SIZE);
}

static void Test_Texture(void)
{
    static DWORD texels[TEX_SIZE * TEX_SIZE];
    GLuint texture = 0;
    int x, y;

    for (y = 0; y < TEX_SIZE; y++)
        for (x = 0; x < TEX_SIZE; x++)
            texels[y * TEX_SIZE + x] = y < TEX_SIZE / 2 ? (x < TEX_SIZE / 2 ? 0xFFFF0000 : 0xFF00FF00)
                                                        : (x < TEX_SIZE / 2 ? 0xFF0000FF : 0xFFFFFFFF);

    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, TEX_SIZE, TEX_SIZE, 0, GL_BGRA_EXT, GL_UNSIGNED_BYTE, texels);
    ok(glGetError() == GL_NO_ERROR, "glTexImage2D failed\n");

    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glEnable(GL_TEXTURE_2D);
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f); glVertex2f(0.0f, 0.0f);
    glTexCoord2f(1.0f, 0.0f); glVertex2f(1.0f, 0.0f);
    glTexCoord2f(1.0f, 1.0f); glVertex2f(1.0f, 1.0f);
    glTexCoord2f(0.0f, 1.0f); glVertex2f(0.0f, 1.0f);
    glEnd();
    glDisable(GL_TEXTURE_2D);

    ok(CountPixelsNear(0xFF0000, 1) == SIZE * SIZE / 4, "red quadrant has %u pixels\n", CountPixelsNear(0xFF0000, 1));
    ok(CountPixelsNear(0x00FF00, 1) == SIZE * SIZE / 4, "green quadrant has %u pixels\n", CountPixelsNear(0x00FF00, 1));
    ok(CountPixelsNear(0x0000FF, 1) == SIZE * SIZE / 4, "blue quadrant has %u pixels\n", CountPixelsNear(0x0000FF, 1));
    ok(CountPixelsNear(0xFFFFFF, 1) == SIZE * SIZE / 4, "white quadrant has %u pixels\n", CountPixelsNear(0xFFFFFF, 1));
    ok(ColorNear(ReadPixel(4, 4), 0xFF0000, 1) && ColorNear(ReadPixel(SIZE - 4, 4), 0x00FF00, 1) &&
       ColorNear(ReadPixel(4, SIZE - 4), 0x0000FF, 1) && ColorNear(ReadPixel(SIZE - 4, SIZE - 4), 0xFFFFFF, 1),
       "quadrants 0x%06lx 0x%06lx 0x%06lx 0x%06lx\n", ReadPixel(4, 4), ReadPixel(SIZE - 4, 4),
       ReadPixel(4, SIZE - 4), ReadPixel(SIZE - 4, SIZE - 4));

    glDeleteTextures(1, &texture);
}

static void Test_Depth(void)
{
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glClearDepth(1.0);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glColor3f(1.0f, 0.0f, 0.0f);
    glBegin(GL_QUADS);
    glVertex3f(0.0f, 0.0f, 0.5f);
    glVertex3f(0.75f, 0.0f, 0.5f);
    glVertex3f(0.75f, 0.75f, 0.5f);
    glVertex3f(0.0f, 0.75f, 0.5f);
    glEnd();
    glColor3f(0.0f, 0.0f, 1.0f);
    glBegin(GL_QUADS);
    glVertex3f(0.25f, 0.25f, -0.5f);
    glVertex3f(1.0f, 0.25f, -0.5f);
    glVertex3f(1.0f, 1.0f, -0.5f);
    glVertex3f(0.25f, 1.0f, -0.5f);
    glEnd();
    glDisable(GL_DEPTH_TEST);

    ok(ReadPixel(SIZE / 2, SIZE / 2) == 0xFF0000, "nearer quad wins the overlap: 0x%06lx\n",
       ReadPixel(SIZE / 2, SIZE / 2));
    ok(ReadPixel(SIZE - 8, SIZE - 8) == 0x0000FF, "farther quad outside the overlap: 0x%06lx\n",
       ReadPixel(SIZE - 8, SIZE - 8));
    ok(ReadPixel(8, SIZE - 8) == 0x000000, "uncovered corner: 0x%06lx\n", ReadPixel(8, SIZE - 8));
}

static void Test_Frames(HDC dc)
{
    unsigned int frame;
    BOOL ret = TRUE;

    for (frame = 0; frame < 120 && ret; frame++)
    {
        glClearColor((frame & 1) ? 1.0f : 0.0f, 0.5f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glColor3f(1.0f, 1.0f, 1.0f);
        glBegin(GL_TRIANGLES);
        glVertex2f(0.5f, 0.9f);
        glVertex2f(0.1f, 0.1f);
        glVertex2f(0.9f, 0.1f);
        glEnd();
        ret = SwapBuffers(dc);
    }
    ok(ret, "SwapBuffers failed at frame %u: %lu\n", frame, GetLastError());
    glFinish();
    ok(glGetError() == GL_NO_ERROR, "GL error after the frame loop\n");
}

static void DrawTriangle(void)
{
    glBegin(GL_TRIANGLES);
    glVertex2f(0.0f, 0.0f);
    glVertex2f(1.0f, 0.0f);
    glVertex2f(0.0f, 1.0f);
    glEnd();
}

static void Test_TwoContexts(HDC dc, HGLRC context)
{
    HGLRC other = wglCreateContext(dc);

    ok(other != NULL, "wglCreateContext failed: %lu\n", GetLastError());
    if (!other)
        return;

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_TEXTURE_2D);
    glColor3f(1.0f, 0.0f, 0.0f);
    DrawTriangle();
    glFinish();

    ok(wglMakeCurrent(dc, other), "wglMakeCurrent failed: %lu\n", GetLastError());
    glViewport(0, 0, SIZE, SIZE);
    glMatrixMode(GL_PROJECTION);
    glOrtho(0.0, 1.0, 0.0, 1.0, -1.0, 1.0);
    glDrawBuffer(GL_BACK);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_FRONT);
    glColor3f(0.0f, 1.0f, 0.0f);
    DrawTriangle();
    glFinish();

    ok(wglMakeCurrent(dc, context), "wglMakeCurrent failed: %lu\n", GetLastError());
    glColor3f(0.0f, 0.0f, 1.0f);
    DrawTriangle();
    glFinish();
    ok(ColorNear(ReadPixel(SIZE / 4, SIZE / 4), 0x0000FF, 2), "Pixel after the other context culled: 0x%06lx\n",
       ReadPixel(SIZE / 4, SIZE / 4));

    wglDeleteContext(other);
}

START_TEST(wgl_render)
{
    PIXELFORMATDESCRIPTOR pfd = {sizeof(pfd), 1};
    HWND window = NULL;
    HDC dc = NULL;
    HGLRC context = NULL;
    int format;

    window = CreateWindowA("static", "wgl_render", WS_POPUP | WS_VISIBLE, 0, 0, SIZE, SIZE, NULL, NULL, NULL, NULL);
    ok(window != NULL, "CreateWindow failed: %lu\n", GetLastError());
    if (!window)
        return;
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
    trace("Renderer: %s; version: %s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION));

    glViewport(0, 0, SIZE, SIZE);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, 1.0, 0.0, 1.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glDrawBuffer(GL_BACK);
    glReadBuffer(GL_BACK);

    Test_Clear();
    Test_Triangle();
    Test_Texture();
    Test_Depth();
    Test_Frames(dc);
    Test_TwoContexts(dc, context);
    ok(glGetError() == GL_NO_ERROR, "GL error at the end\n");

cleanup:
    if (context)
    {
        wglMakeCurrent(NULL, NULL);
        wglDeleteContext(context);
    }
    if (dc)
        ReleaseDC(window, dc);
    DestroyWindow(window);
}
