/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Flat shading takes the provoking vertex colour, and the D3D12 driver runs on the virtio 3D GPU
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntstatus.h>
#define WIN32_NO_STATUS
#include <windows.h>
#include <d3dkmthk.h>
#include <GL/gl.h>
#include <string.h>
#include <wine/test.h>

#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif

#define SIZE 64
#define TOLERANCE 8

typedef struct _COLOR
{
    int Red;
    int Green;
    int Blue;
} COLOR;

static COLOR
ReadCenter(void)
{
    BYTE Pixel[4] = {0};
    COLOR Color;

    glFinish();
    glReadBuffer(GL_BACK);
    glReadPixels(SIZE / 2, SIZE / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, Pixel);
    Color.Red = Pixel[0];
    Color.Green = Pixel[1];
    Color.Blue = Pixel[2];
    return Color;
}

static BOOL
NearColor(COLOR Color, int Red, int Green, int Blue)
{
    return abs(Color.Red - Red) <= TOLERANCE && abs(Color.Green - Green) <= TOLERANCE &&
           abs(Color.Blue - Blue) <= TOLERANCE;
}

static void
DrawTriangle(void)
{
    glBegin(GL_TRIANGLES);
    glColor3ub(255, 0, 0);
    glVertex2f(-3.0f, -3.0f);
    glColor3ub(0, 255, 0);
    glVertex2f(3.0f, -3.0f);
    glColor3ub(0, 0, 255);
    glVertex2f(0.0f, 3.0f);
    glEnd();
}

static void
DrawQuad(void)
{
    glBegin(GL_QUADS);
    glColor3ub(255, 0, 0);
    glVertex2f(-1.0f, -1.0f);
    glColor3ub(0, 255, 0);
    glVertex2f(1.0f, -1.0f);
    glColor3ub(255, 255, 255);
    glVertex2f(1.0f, 1.0f);
    glColor3ub(0, 0, 255);
    glVertex2f(-1.0f, 1.0f);
    glEnd();
}

static void
TestFlatColors(void)
{
    COLOR Color;
    GLuint List;

    glViewport(0, 0, SIZE, SIZE);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    glShadeModel(GL_SMOOTH);
    glClear(GL_COLOR_BUFFER_BIT);
    DrawTriangle();
    Color = ReadCenter();
    ok(Color.Red > 0 && Color.Green > 0 && Color.Blue > 0,
       "smooth triangle gave %d,%d,%d\n", Color.Red, Color.Green, Color.Blue);

    glShadeModel(GL_FLAT);
    glClear(GL_COLOR_BUFFER_BIT);
    DrawTriangle();
    Color = ReadCenter();
    ok(NearColor(Color, 0, 0, 255), "flat triangle gave %d,%d,%d\n", Color.Red, Color.Green, Color.Blue);

    glClear(GL_COLOR_BUFFER_BIT);
    DrawQuad();
    Color = ReadCenter();
    ok(NearColor(Color, 0, 0, 255), "flat quad gave %d,%d,%d\n", Color.Red, Color.Green, Color.Blue);

    List = glGenLists(1);
    glNewList(List, GL_COMPILE);
    DrawQuad();
    glEndList();
    glClear(GL_COLOR_BUFFER_BIT);
    glCallList(List);
    Color = ReadCenter();
    ok(NearColor(Color, 0, 0, 255), "flat display list gave %d,%d,%d\n", Color.Red, Color.Green, Color.Blue);
    glDeleteLists(List, 1);
    glShadeModel(GL_SMOOTH);
}

static void
TestFlatLighting(void)
{
    static const GLfloat Direction[4] = {0.0f, 0.0f, 1.0f, 0.0f};
    static const GLfloat White[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    static const GLfloat Black[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    static const GLfloat Green[4] = {0.0f, 1.0f, 0.0f, 1.0f};
    COLOR Color;

    glLightfv(GL_LIGHT0, GL_POSITION, Direction);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, White);
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, Black);
    glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, Black);
    glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, Green);
    glEnable(GL_LIGHT0);
    glEnable(GL_LIGHTING);
    glShadeModel(GL_FLAT);
    glClear(GL_COLOR_BUFFER_BIT);
    glNormal3f(0.0f, 0.0f, 1.0f);
    glBegin(GL_QUADS);
    glVertex2f(-1.0f, -1.0f);
    glVertex2f(1.0f, -1.0f);
    glVertex2f(1.0f, 1.0f);
    glVertex2f(-1.0f, 1.0f);
    glEnd();
    Color = ReadCenter();
    ok(NearColor(Color, 0, 255, 0), "flat lit quad gave %d,%d,%d\n", Color.Red, Color.Green, Color.Blue);
    glDisable(GL_LIGHTING);
    glDisable(GL_LIGHT0);
    glShadeModel(GL_SMOOTH);
}

static BOOL
WindowIsOnVirtio3d(HDC hdc)
{
    D3DKMT_OPENADAPTERFROMHDC Open;
    D3DKMT_QUERYADAPTERINFO Query;
    D3DKMT_QUERY_DEVICE_IDS Ids;
    D3DKMT_CLOSEADAPTER Close;
    BOOL Virtio = FALSE;

    memset(&Open, 0, sizeof(Open));
    Open.hDc = hdc;
    if (!NT_SUCCESS(D3DKMTOpenAdapterFromHdc(&Open)) || Open.hAdapter == 0)
        return FALSE;
    memset(&Ids, 0, sizeof(Ids));
    memset(&Query, 0, sizeof(Query));
    Query.hAdapter = Open.hAdapter;
    Query.Type = KMTQAITYPE_PHYSICALADAPTERDEVICEIDS;
    Query.pPrivateDriverData = &Ids;
    Query.PrivateDriverDataSize = sizeof(Ids);
    if (NT_SUCCESS(D3DKMTQueryAdapterInfo(&Query)))
        Virtio = Ids.DeviceIds.VendorID == 0x1af4 && Ids.DeviceIds.DeviceID == 0x1050;
    Close.hAdapter = Open.hAdapter;
    D3DKMTCloseAdapter(&Close);
    return Virtio;
}

static void
TestRenderer(HDC hdc)
{
    const char *Renderer = (const char *)glGetString(GL_RENDERER);

    trace("renderer '%s' version '%s'\n", Renderer ? Renderer : "(null)", glGetString(GL_VERSION));
    ok(Renderer != NULL, "GL_RENDERER is NULL\n");
    if (Renderer == NULL)
        return;
    if (GetModuleHandleA("mesa_gallium.dll") == NULL || !WindowIsOnVirtio3d(hdc))
    {
        skip("Not the Mesa driver on the virtio 3D GPU\n");
        return;
    }
    ok(strncmp(Renderer, "D3D12 (", 7) == 0, "OpenGL runs on '%s', not on the GPU\n", Renderer);
}

START_TEST(wgl_flat_shading)
{
    PIXELFORMATDESCRIPTOR Pfd = { sizeof(Pfd), 1 };
    HWND Window;
    HGLRC Context;
    HDC hdc;
    int Format;

    Window = CreateWindowA("static", "flat shading", WS_POPUP | WS_VISIBLE, 0, 0, SIZE, SIZE, NULL, NULL, NULL, NULL);
    ok(Window != NULL, "CreateWindow failed: %lu\n", GetLastError());
    if (Window == NULL)
        return;
    hdc = GetDC(Window);
    Pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    Pfd.iPixelType = PFD_TYPE_RGBA;
    Pfd.cColorBits = 24;
    Format = ChoosePixelFormat(hdc, &Pfd);
    ok(Format != 0, "ChoosePixelFormat failed: %lu\n", GetLastError());
    if (Format != 0 && SetPixelFormat(hdc, Format, &Pfd))
    {
        Context = wglCreateContext(hdc);
        ok(Context != NULL, "wglCreateContext failed: %lu\n", GetLastError());
        if (Context != NULL && wglMakeCurrent(hdc, Context))
        {
            TestRenderer(hdc);
            TestFlatColors();
            TestFlatLighting();
            wglMakeCurrent(NULL, NULL);
        }
        if (Context != NULL)
            wglDeleteContext(Context);
    }
    ReleaseDC(Window, hdc);
    DestroyWindow(Window);
}
