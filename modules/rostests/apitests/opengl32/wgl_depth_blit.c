/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     glBlitFramebuffer copies depth and stencil between a window and a framebuffer object
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <windows.h>
#include <GL/gl.h>
#include <math.h>
#include <wine/test.h>

#define SIZE 64
#define GL_FRAMEBUFFER_ 0x8D40
#define GL_READ_FRAMEBUFFER_ 0x8CA8
#define GL_DRAW_FRAMEBUFFER_ 0x8CA9
#define GL_DEPTH_STENCIL_ATTACHMENT_ 0x821A
#define GL_FRAMEBUFFER_COMPLETE_ 0x8CD5
#define GL_DEPTH24_STENCIL8_ 0x88F0
#define GL_TEXTURE0_ 0x84C0
#define GL_DEPTH_STENCIL_ 0x84F9
#define GL_COLOR_ATTACHMENT0_ 0x8CE0
#define GL_UNSIGNED_INT_24_8_ 0x84FA

typedef void (APIENTRY *PFN_GEN_FRAMEBUFFERS)(GLsizei, GLuint *);
typedef void (APIENTRY *PFN_BIND_FRAMEBUFFER)(GLenum, GLuint);
typedef void (APIENTRY *PFN_FRAMEBUFFER_TEXTURE2D)(GLenum, GLenum, GLenum, GLuint, GLint);
typedef GLenum (APIENTRY *PFN_CHECK_FRAMEBUFFER)(GLenum);
typedef void (APIENTRY *PFN_BLIT_FRAMEBUFFER)(GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum);

static PFN_BIND_FRAMEBUFFER BindFramebuffer;
static PFN_BLIT_FRAMEBUFFER BlitFramebuffer;

static float
ReadDepth(GLuint Framebuffer)
{
    float Value = -1.0f;

    BindFramebuffer(GL_READ_FRAMEBUFFER_, Framebuffer);
    glReadPixels(SIZE / 2, SIZE / 2, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &Value);
    return Value;
}

static void
ClearDepth(GLuint Framebuffer, double Depth)
{
    BindFramebuffer(GL_DRAW_FRAMEBUFFER_, Framebuffer);
    glClearDepth(Depth);
    glDepthMask(GL_TRUE);
    glClear(GL_DEPTH_BUFFER_BIT);
}

static void
Copy(GLuint Source, GLuint Destination, GLbitfield Mask)
{
    BindFramebuffer(GL_READ_FRAMEBUFFER_, Source);
    BindFramebuffer(GL_DRAW_FRAMEBUFFER_, Destination);
    BlitFramebuffer(0, 0, SIZE, SIZE, 0, 0, SIZE, SIZE, Mask, GL_NEAREST);
}

static void
DrawDepthQuad(void)
{
    glBegin(GL_TRIANGLE_STRIP);
    glVertex3f(-1.0f, -1.0f, -0.8f);
    glVertex3f(1.0f, -1.0f, -0.8f);
    glVertex3f(-1.0f, 1.0f, -0.8f);
    glVertex3f(1.0f, 1.0f, -0.8f);
    glEnd();
}

static void
CheckDepthDraw(const char *What, GLuint Framebuffer)
{
    BYTE Pixel[4] = {0};

    BindFramebuffer(GL_DRAW_FRAMEBUFFER_, Framebuffer);
    BindFramebuffer(GL_READ_FRAMEBUFFER_, Framebuffer);
    glDrawBuffer(Framebuffer ? GL_COLOR_ATTACHMENT0_ : GL_BACK);
    glReadBuffer(Framebuffer ? GL_COLOR_ATTACHMENT0_ : GL_BACK);
    glViewport(0, 0, SIZE, SIZE);
    glClearColor(0.0f, 1.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glColor4f(1.0f, 0.0f, 0.0f, 1.0f);
    DrawDepthQuad();
    glDisable(GL_DEPTH_TEST);
    glFinish();
    glReadPixels(SIZE / 2, SIZE / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, Pixel);
    ok(Pixel[0] > 200 && Pixel[1] < 50, "%s: the depth test rejected a near quad, got %u,%u,%u\n", What, Pixel[0],
       Pixel[1], Pixel[2]);
}

static void
RunSmallTargetCases(GLuint DepthObject, GLuint DepthTexture, PFN_GEN_FRAMEBUFFERS GenFramebuffers,
                    PFN_FRAMEBUFFER_TEXTURE2D FramebufferTexture2D)
{
    GLuint Small = 0, SmallObject = 0, Large = 0, LargeObject = 0;

    BindFramebuffer(GL_DRAW_FRAMEBUFFER_, 0);
    glDrawBuffer(GL_BACK);
    ClearDepth(0, 1.0);
    CheckDepthDraw("A cleared window depth", 0);

    glGenTextures(1, &Small);
    glBindTexture(GL_TEXTURE_2D, Small);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, SIZE / 4, SIZE / 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    GenFramebuffers(1, &SmallObject);
    BindFramebuffer(GL_FRAMEBUFFER_, SmallObject);
    FramebufferTexture2D(GL_FRAMEBUFFER_, GL_COLOR_ATTACHMENT0_, GL_TEXTURE_2D, Small, 0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0_);

    ClearDepth(0, 1.0);
    BindFramebuffer(GL_DRAW_FRAMEBUFFER_, SmallObject);
    glClearColor(1.0f, 0.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glFinish();
    CheckDepthDraw("A small color target alone", 0);

    ClearDepth(0, 1.0);
    ClearDepth(DepthObject, 1.0);
    BindFramebuffer(GL_DRAW_FRAMEBUFFER_, SmallObject);
    FramebufferTexture2D(GL_FRAMEBUFFER_, GL_DEPTH_STENCIL_ATTACHMENT_, GL_TEXTURE_2D, DepthTexture, 0);
    glClearColor(1.0f, 0.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glFinish();
    CheckDepthDraw("A small color target with a larger depth buffer", 0);

    glGenTextures(1, &Large);
    glBindTexture(GL_TEXTURE_2D, Large);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, SIZE, SIZE, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    GenFramebuffers(1, &LargeObject);
    BindFramebuffer(GL_FRAMEBUFFER_, LargeObject);
    FramebufferTexture2D(GL_FRAMEBUFFER_, GL_COLOR_ATTACHMENT0_, GL_TEXTURE_2D, Large, 0);
    FramebufferTexture2D(GL_FRAMEBUFFER_, GL_DEPTH_STENCIL_ATTACHMENT_, GL_TEXTURE_2D, DepthTexture, 0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0_);
    glReadBuffer(GL_COLOR_ATTACHMENT0_);
    glClearColor(0.0f, 1.0f, 1.0f, 1.0f);
    glClearDepth(1.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    BindFramebuffer(GL_DRAW_FRAMEBUFFER_, SmallObject);
    glClearColor(1.0f, 0.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    CheckDepthDraw("A depth texture shared with a small target, no flush", LargeObject);

    BindFramebuffer(GL_FRAMEBUFFER_, LargeObject);
    glClearDepth(1.0);
    glClear(GL_DEPTH_BUFFER_BIT);
    BindFramebuffer(GL_DRAW_FRAMEBUFFER_, SmallObject);
    glClearColor(1.0f, 0.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glFinish();
    BindFramebuffer(GL_DRAW_FRAMEBUFFER_, SmallObject);
    FramebufferTexture2D(GL_DRAW_FRAMEBUFFER_, GL_DEPTH_STENCIL_ATTACHMENT_, GL_TEXTURE_2D, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    glFinish();
    CheckDepthDraw("A depth texture detached from a small target", LargeObject);

    FramebufferTexture2D(GL_DRAW_FRAMEBUFFER_, GL_DEPTH_STENCIL_ATTACHMENT_, GL_TEXTURE_2D, DepthTexture, 0);
    BindFramebuffer(GL_DRAW_FRAMEBUFFER_, LargeObject);
    glClearDepth(1.0);
    glClear(GL_DEPTH_BUFFER_BIT);
    BindFramebuffer(GL_DRAW_FRAMEBUFFER_, SmallObject);
    glEnable(GL_SCISSOR_TEST);
    glScissor(1, 1, 4, 4);
    glClearColor(1.0f, 1.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_SCISSOR_TEST);
    glFinish();
    CheckDepthDraw("A scissored color clear on a small target", LargeObject);

    BindFramebuffer(GL_DRAW_FRAMEBUFFER_, LargeObject);
    glClearDepth(1.0);
    glClear(GL_DEPTH_BUFFER_BIT);
    BindFramebuffer(GL_DRAW_FRAMEBUFFER_, SmallObject);
    glViewport(0, 0, SIZE / 4, SIZE / 4);
    glDepthMask(GL_FALSE);
    glDisable(GL_DEPTH_TEST);
    glColor4f(0.0f, 1.0f, 0.0f, 1.0f);
    DrawDepthQuad();
    glDepthMask(GL_TRUE);
    glFinish();
    CheckDepthDraw("A draw without depth on a small target", LargeObject);
    ok(glGetError() == GL_NO_ERROR, "The small target cases raised a GL error\n");
}

static void
RunCopies(void)
{
    PFN_GEN_FRAMEBUFFERS GenFramebuffers = (PFN_GEN_FRAMEBUFFERS)wglGetProcAddress("glGenFramebuffers");
    PFN_FRAMEBUFFER_TEXTURE2D FramebufferTexture2D = (PFN_FRAMEBUFFER_TEXTURE2D)wglGetProcAddress("glFramebufferTexture2D");
    PFN_CHECK_FRAMEBUFFER CheckFramebuffer = (PFN_CHECK_FRAMEBUFFER)wglGetProcAddress("glCheckFramebufferStatus");
    GLuint Texture = 0, Framebuffer = 0;
    GLint DepthBits = 0, StencilBits = 0;
    float Value;

    BindFramebuffer = (PFN_BIND_FRAMEBUFFER)wglGetProcAddress("glBindFramebuffer");
    BlitFramebuffer = (PFN_BLIT_FRAMEBUFFER)wglGetProcAddress("glBlitFramebuffer");
    if (!GenFramebuffers || !FramebufferTexture2D || !CheckFramebuffer || !BindFramebuffer || !BlitFramebuffer)
    {
        skip("This OpenGL driver has no framebuffer objects\n");
        return;
    }
    glGetIntegerv(GL_DEPTH_BITS, &DepthBits);
    glGetIntegerv(GL_STENCIL_BITS, &StencilBits);
    trace("The window has %d depth and %d stencil bits\n", DepthBits, StencilBits);
    ok(DepthBits == 24 && StencilBits == 8, "Got %d depth and %d stencil bits\n", DepthBits, StencilBits);

    glGenTextures(1, &Texture);
    glBindTexture(GL_TEXTURE_2D, Texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8_, SIZE, SIZE, 0, GL_DEPTH_STENCIL_, GL_UNSIGNED_INT_24_8_, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    GenFramebuffers(1, &Framebuffer);
    BindFramebuffer(GL_FRAMEBUFFER_, Framebuffer);
    FramebufferTexture2D(GL_FRAMEBUFFER_, GL_DEPTH_STENCIL_ATTACHMENT_, GL_TEXTURE_2D, Texture, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    ok(CheckFramebuffer(GL_FRAMEBUFFER_) == GL_FRAMEBUFFER_COMPLETE_, "The framebuffer object is incomplete\n");
    ok(glGetError() == GL_NO_ERROR, "The setup raised a GL error\n");

    ClearDepth(0, 0.25);
    Value = ReadDepth(0);
    ok(fabsf(Value - 0.25f) < 0.01f, "The window depth reads %f after a clear\n", Value);

    ClearDepth(Framebuffer, 0.75);
    Value = ReadDepth(Framebuffer);
    ok(fabsf(Value - 0.75f) < 0.01f, "The object depth reads %f after a clear\n", Value);

    Copy(0, Framebuffer, GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    ok(glGetError() == GL_NO_ERROR, "The copy from the window raised a GL error\n");
    glFinish();
    Value = ReadDepth(Framebuffer);
    ok(fabsf(Value - 0.25f) < 0.01f, "The object depth reads %f after the copy from the window\n", Value);

    ClearDepth(Framebuffer, 1.0);
    ClearDepth(0, 0.5);
    Copy(Framebuffer, 0, GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    ok(glGetError() == GL_NO_ERROR, "The copy to the window raised a GL error\n");
    glFinish();
    Value = ReadDepth(0);
    ok(fabsf(Value - 1.0f) < 0.01f, "The window depth reads %f after the copy to the window\n", Value);

    RunSmallTargetCases(Framebuffer, Texture, GenFramebuffers, FramebufferTexture2D);
}

START_TEST(wgl_depth_blit)
{
    PIXELFORMATDESCRIPTOR Pfd = { sizeof(Pfd), 1 };
    HWND Window;
    HGLRC Context;
    HDC Dc;
    int Format;

    Window = CreateWindowA("static", "depth blit", WS_POPUP | WS_VISIBLE, 0, 0, SIZE, SIZE, NULL, NULL, NULL, NULL);
    ok(Window != NULL, "CreateWindow failed: %lu\n", GetLastError());
    if (Window == NULL)
        return;
    Dc = GetDC(Window);
    Pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    Pfd.iPixelType = PFD_TYPE_RGBA;
    Pfd.cColorBits = 24;
    Pfd.cDepthBits = 24;
    Pfd.cStencilBits = 8;
    Format = ChoosePixelFormat(Dc, &Pfd);
    ok(Format != 0, "ChoosePixelFormat failed: %lu\n", GetLastError());
    if (Format != 0 && SetPixelFormat(Dc, Format, &Pfd))
    {
        Context = wglCreateContext(Dc);
        ok(Context != NULL, "wglCreateContext failed: %lu\n", GetLastError());
        if (Context != NULL && wglMakeCurrent(Dc, Context))
        {
            trace("renderer '%s'\n", glGetString(GL_RENDERER));
            RunCopies();
            wglMakeCurrent(NULL, NULL);
        }
        if (Context != NULL)
            wglDeleteContext(Context);
    }
    ReleaseDC(Window, Dc);
    DestroyWindow(Window);
}
