/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     glBlitFramebuffer copies color between RGBA and BGRA surfaces texel for texel
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <windows.h>
#include <GL/gl.h>
#include <wine/test.h>

#define SIZE 64
#define GL_BGRA_ 0x80E1
#define GL_FRAMEBUFFER_ 0x8D40
#define GL_READ_FRAMEBUFFER_ 0x8CA8
#define GL_DRAW_FRAMEBUFFER_ 0x8CA9
#define GL_COLOR_ATTACHMENT0_ 0x8CE0
#define GL_FRAMEBUFFER_COMPLETE_ 0x8CD5

typedef void (APIENTRY *PFN_GEN_FRAMEBUFFERS)(GLsizei, GLuint *);
typedef void (APIENTRY *PFN_DELETE_FRAMEBUFFERS)(GLsizei, const GLuint *);
typedef void (APIENTRY *PFN_BIND_FRAMEBUFFER)(GLenum, GLuint);
typedef void (APIENTRY *PFN_FRAMEBUFFER_TEXTURE2D)(GLenum, GLenum, GLenum, GLuint, GLint);
typedef GLenum (APIENTRY *PFN_CHECK_FRAMEBUFFER)(GLenum);
typedef void (APIENTRY *PFN_BLIT_FRAMEBUFFER)(GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum);

static PFN_GEN_FRAMEBUFFERS GenFramebuffers;
static PFN_DELETE_FRAMEBUFFERS DeleteFramebuffers;
static PFN_BIND_FRAMEBUFFER BindFramebuffer;
static PFN_FRAMEBUFFER_TEXTURE2D FramebufferTexture2D;
static PFN_CHECK_FRAMEBUFFER CheckFramebufferStatus;
static PFN_BLIT_FRAMEBUFFER BlitFramebuffer;

static BYTE Pattern[SIZE][SIZE][4];
static BYTE Pixels[SIZE][SIZE][4];

static PROC
GetProc(const char *Name, const char *ExtName)
{
    PROC Proc = wglGetProcAddress(Name);
    return Proc ? Proc : wglGetProcAddress(ExtName);
}

static GLuint
CreateTarget(GLenum UploadFormat, const void *Data, GLuint *Texture)
{
    GLuint Framebuffer = 0;

    glGenTextures(1, Texture);
    glBindTexture(GL_TEXTURE_2D, *Texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, SIZE, SIZE, 0, UploadFormat, GL_UNSIGNED_BYTE, Data);
    GenFramebuffers(1, &Framebuffer);
    BindFramebuffer(GL_FRAMEBUFFER_, Framebuffer);
    FramebufferTexture2D(GL_FRAMEBUFFER_, GL_COLOR_ATTACHMENT0_, GL_TEXTURE_2D, *Texture, 0);
    ok(CheckFramebufferStatus(GL_FRAMEBUFFER_) == GL_FRAMEBUFFER_COMPLETE_, "framebuffer %u is incomplete\n",
       Framebuffer);
    return Framebuffer;
}

static void
Clear(GLuint Framebuffer)
{
    BindFramebuffer(GL_DRAW_FRAMEBUFFER_, Framebuffer);
    glDrawBuffer(Framebuffer ? GL_COLOR_ATTACHMENT0_ : GL_BACK);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

static void
Blit(GLuint Source, GLuint Destination, GLint X0, GLint Y0, GLint X1, GLint Y1, GLint DX0, GLint DY0, GLint DX1,
     GLint DY1)
{
    BindFramebuffer(GL_READ_FRAMEBUFFER_, Source);
    glReadBuffer(GL_COLOR_ATTACHMENT0_);
    BindFramebuffer(GL_DRAW_FRAMEBUFFER_, Destination);
    glDrawBuffer(Destination ? GL_COLOR_ATTACHMENT0_ : GL_BACK);
    BlitFramebuffer(X0, Y0, X1, Y1, DX0, DY0, DX1, DY1, GL_COLOR_BUFFER_BIT, GL_NEAREST);
}

static void
Read(GLuint Framebuffer)
{
    memset(Pixels, 0xCD, sizeof(Pixels));
    BindFramebuffer(GL_READ_FRAMEBUFFER_, Framebuffer);
    glReadBuffer(Framebuffer ? GL_COLOR_ATTACHMENT0_ : GL_BACK);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, SIZE, SIZE, GL_RGBA, GL_UNSIGNED_BYTE, Pixels);
}

static void
Check(const char *What, BOOL Alpha, int X0, int Y0, int X1, int Y1, int SourceX, int SourceY, BOOL FlipY)
{
    int Bad = 0, FirstX = -1, FirstY = -1, X, Y, Channel;

    for (Y = 0; Y < SIZE; Y++)
    {
        for (X = 0; X < SIZE; X++)
        {
            BOOL Inside = X >= X0 && X < X1 && Y >= Y0 && Y < Y1;
            const BYTE *Expected = NULL;
            BYTE Zero[4] = {0};

            if (Inside)
                Expected = Pattern[FlipY ? SourceY + (Y1 - 1 - Y) : SourceY + (Y - Y0)][SourceX + (X - X0)];
            else
                Expected = Zero;
            for (Channel = 0; Channel < (Alpha ? 4 : 3); Channel++)
            {
                if (Pixels[Y][X][Channel] != Expected[Channel])
                {
                    if (!Bad++)
                    {
                        FirstX = X;
                        FirstY = Y;
                    }
                    break;
                }
            }
        }
    }
    ok(Bad == 0, "%s: %d texels differ, first at %d,%d: %02x%02x%02x%02x\n", What, Bad, FirstX, FirstY,
       Bad ? Pixels[FirstY][FirstX][0] : 0, Bad ? Pixels[FirstY][FirstX][1] : 0,
       Bad ? Pixels[FirstY][FirstX][2] : 0, Bad ? Pixels[FirstY][FirstX][3] : 0);
}

static void
RunBlits(void)
{
    BYTE Swapped[SIZE][SIZE][4];
    GLuint RgbaTexture = 0, BgraTexture = 0, Rgba, Bgra;
    int X, Y;

    for (Y = 0; Y < SIZE; Y++)
    {
        for (X = 0; X < SIZE; X++)
        {
            Pattern[Y][X][0] = (BYTE)(X * 4 + 1);
            Pattern[Y][X][1] = (BYTE)(Y * 4 + 2);
            Pattern[Y][X][2] = (BYTE)((X ^ Y) * 3);
            Pattern[Y][X][3] = (BYTE)(0x80 + ((X + Y) & 0x3F));
            Swapped[Y][X][0] = Pattern[Y][X][2];
            Swapped[Y][X][1] = Pattern[Y][X][1];
            Swapped[Y][X][2] = Pattern[Y][X][0];
            Swapped[Y][X][3] = Pattern[Y][X][3];
        }
    }

    Rgba = CreateTarget(GL_RGBA, Pattern, &RgbaTexture);
    Bgra = CreateTarget(GL_BGRA_, NULL, &BgraTexture);

    Clear(Bgra);
    Blit(Rgba, Bgra, 0, 0, SIZE, SIZE, 0, 0, SIZE, SIZE);
    Read(Bgra);
    Check("RGBA to BGRA", TRUE, 0, 0, SIZE, SIZE, 0, 0, FALSE);

    Clear(Bgra);
    Blit(Rgba, Bgra, 8, 4, 40, 36, 16, 20, 48, 52);
    Read(Bgra);
    Check("RGBA to BGRA, part", TRUE, 16, 20, 48, 52, 8, 4, FALSE);

    glBindTexture(GL_TEXTURE_2D, BgraTexture);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, SIZE, SIZE, GL_BGRA_, GL_UNSIGNED_BYTE, Swapped);
    Clear(Rgba);
    Blit(Bgra, Rgba, 0, 0, SIZE, SIZE, 0, 0, SIZE, SIZE);
    Read(Rgba);
    Check("BGRA to RGBA", TRUE, 0, 0, SIZE, SIZE, 0, 0, FALSE);

    glBindTexture(GL_TEXTURE_2D, RgbaTexture);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, SIZE, SIZE, GL_RGBA, GL_UNSIGNED_BYTE, Pattern);
    Clear(0);
    Blit(Rgba, 0, 0, 0, SIZE, SIZE, 0, 0, SIZE, SIZE);
    Read(0);
    Check("RGBA to the window", FALSE, 0, 0, SIZE, SIZE, 0, 0, FALSE);

    Clear(Bgra);
    glEnable(GL_SCISSOR_TEST);
    glScissor(10, 12, 20, 24);
    Blit(Rgba, Bgra, 0, 0, SIZE, SIZE, 0, 0, SIZE, SIZE);
    glDisable(GL_SCISSOR_TEST);
    Read(Bgra);
    Check("RGBA to BGRA, scissored", TRUE, 10, 12, 30, 36, 10, 12, FALSE);

    Clear(Bgra);
    Blit(Rgba, Bgra, 0, 0, SIZE, SIZE, 0, SIZE, SIZE, 0);
    Read(Bgra);
    Check("RGBA to BGRA, flipped", TRUE, 0, 0, SIZE, SIZE, 0, 0, TRUE);

    BindFramebuffer(GL_FRAMEBUFFER_, 0);
    DeleteFramebuffers(1, &Rgba);
    DeleteFramebuffers(1, &Bgra);
    glDeleteTextures(1, &RgbaTexture);
    glDeleteTextures(1, &BgraTexture);
}

START_TEST(wgl_color_blit)
{
    PIXELFORMATDESCRIPTOR Pfd = { sizeof(Pfd), 1 };
    HWND Window;
    HGLRC Context;
    HDC Dc;
    int Format;

    Window = CreateWindowA("static", "color blit", WS_POPUP | WS_VISIBLE, 0, 0, SIZE, SIZE, NULL, NULL, NULL, NULL);
    ok(Window != NULL, "CreateWindow failed: %lu\n", GetLastError());
    if (Window == NULL)
        return;
    Dc = GetDC(Window);
    Pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    Pfd.iPixelType = PFD_TYPE_RGBA;
    Pfd.cColorBits = 32;
    Format = ChoosePixelFormat(Dc, &Pfd);
    ok(Format != 0, "ChoosePixelFormat failed: %lu\n", GetLastError());
    if (Format != 0 && SetPixelFormat(Dc, Format, &Pfd))
    {
        Context = wglCreateContext(Dc);
        ok(Context != NULL, "wglCreateContext failed: %lu\n", GetLastError());
        if (Context != NULL && wglMakeCurrent(Dc, Context))
        {
            trace("renderer '%s'\n", glGetString(GL_RENDERER));
            GenFramebuffers = (PFN_GEN_FRAMEBUFFERS)GetProc("glGenFramebuffers", "glGenFramebuffersEXT");
            DeleteFramebuffers = (PFN_DELETE_FRAMEBUFFERS)GetProc("glDeleteFramebuffers", "glDeleteFramebuffersEXT");
            BindFramebuffer = (PFN_BIND_FRAMEBUFFER)GetProc("glBindFramebuffer", "glBindFramebufferEXT");
            FramebufferTexture2D = (PFN_FRAMEBUFFER_TEXTURE2D)GetProc("glFramebufferTexture2D",
                                                                       "glFramebufferTexture2DEXT");
            CheckFramebufferStatus = (PFN_CHECK_FRAMEBUFFER)GetProc("glCheckFramebufferStatus",
                                                                     "glCheckFramebufferStatusEXT");
            BlitFramebuffer = (PFN_BLIT_FRAMEBUFFER)GetProc("glBlitFramebuffer", "glBlitFramebufferEXT");
            if (GenFramebuffers && DeleteFramebuffers && BindFramebuffer && FramebufferTexture2D &&
                CheckFramebufferStatus && BlitFramebuffer)
                RunBlits();
            else
                skip("the renderer has no framebuffer blits\n");
            wglMakeCurrent(NULL, NULL);
        }
        if (Context != NULL)
            wglDeleteContext(Context);
    }
    ReleaseDC(Window, Dc);
    DestroyWindow(Window);
}
