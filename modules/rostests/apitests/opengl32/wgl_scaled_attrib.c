/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Non-normalized integer vertex attributes reach a float shader input as their integer values
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <windows.h>
#include <GL/gl.h>
#include <stdlib.h>
#include <string.h>
#include <wine/test.h>

#define SIZE 64
#define TOLERANCE 8
#define GL_FRAGMENT_SHADER_ 0x8B30
#define GL_VERTEX_SHADER_ 0x8B31
#define GL_LINK_STATUS_ 0x8B82
#define GL_BGRA_ 0x80E1
#define GL_UINT_2_10_10_10_ 0x8368
#define GL_INT_2_10_10_10_ 0x8D9F
#define PACK_1010102(R, G, B, A) \
    (((DWORD)(R) & 0x3FF) | (((DWORD)(G) & 0x3FF) << 10) | (((DWORD)(B) & 0x3FF) << 20) | ((DWORD)(A) << 30))

typedef char GLchar_;
typedef GLuint (APIENTRY *PFN_CREATE_SHADER)(GLenum);
typedef void (APIENTRY *PFN_SHADER_SOURCE)(GLuint, GLsizei, const GLchar_ **, const GLint *);
typedef void (APIENTRY *PFN_COMPILE_SHADER)(GLuint);
typedef GLuint (APIENTRY *PFN_CREATE_PROGRAM)(void);
typedef void (APIENTRY *PFN_ATTACH_SHADER)(GLuint, GLuint);
typedef void (APIENTRY *PFN_LINK_PROGRAM)(GLuint);
typedef void (APIENTRY *PFN_GET_PROGRAMIV)(GLuint, GLenum, GLint *);
typedef void (APIENTRY *PFN_USE_PROGRAM)(GLuint);
typedef GLint (APIENTRY *PFN_GET_LOCATION)(GLuint, const GLchar_ *);
typedef void (APIENTRY *PFN_UNIFORM4F)(GLint, GLfloat, GLfloat, GLfloat, GLfloat);
typedef void (APIENTRY *PFN_ENABLE_ATTRIB)(GLuint);
typedef void (APIENTRY *PFN_ATTRIB_POINTER)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void *);

typedef struct _ATTRIB_CASE
{
    const char *Name;
    GLint Size;
    GLenum Type;
    GLboolean Normalized;
    unsigned Stride;
    BYTE Data[8];
    float Scale;
    DWORD Packed;
} ATTRIB_CASE;

static const char VertexSource[] =
    "#version 120\n"
    "attribute vec4 Value;\n"
    "uniform vec4 Scale;\n"
    "varying vec4 Color;\n"
    "void main() { gl_Position = gl_Vertex; Color = Value * Scale; }\n";
static const char FragmentSource[] =
    "#version 120\n"
    "varying vec4 Color;\n"
    "void main() { gl_FragColor = vec4(Color.xyz, 1.0); }\n";

static const ATTRIB_CASE Cases[] =
{
    { "4 unsigned bytes", 4, GL_UNSIGNED_BYTE, GL_FALSE, 4, { 255, 128, 64, 255 }, 1.0f / 255.0f, 0 },
    { "4 signed bytes", 4, GL_BYTE, GL_FALSE, 4, { (BYTE)-127, (BYTE)-64, (BYTE)-32, 127 }, -1.0f / 127.0f, 0 },
    { "4 unsigned shorts", 4, GL_UNSIGNED_SHORT, GL_FALSE, 8, { 0xE8, 0x03, 0xF4, 0x01, 0xFA, 0x00, 0xE8, 0x03 }, 1.0f / 1000.0f, 0 },
    { "4 signed shorts", 4, GL_SHORT, GL_FALSE, 8, { 0x18, 0xFC, 0x0C, 0xFE, 0x06, 0xFF, 0xE8, 0x03 }, -1.0f / 1000.0f, 0 },
    { "unsigned 10 10 10 2", 4, GL_UINT_2_10_10_10_, GL_FALSE, 4, { 0 }, 1.0f / 1023.0f, PACK_1010102(1023, 512, 256, 3) },
    { "signed 10 10 10 2", 4, GL_INT_2_10_10_10_, GL_FALSE, 4, { 0 }, -1.0f / 511.0f, PACK_1010102(-511, -256, -128, 1) },
    { "normalized signed 10 10 10 2", 4, GL_INT_2_10_10_10_, GL_TRUE, 4, { 0 }, -1.0f, PACK_1010102(-511, -256, -128, 1) },
    { "normalized unsigned bytes", 4, GL_UNSIGNED_BYTE, GL_TRUE, 4, { 255, 128, 64, 255 }, 1.0f, 0 },
    { "normalized BGRA bytes", GL_BGRA_, GL_UNSIGNED_BYTE, GL_TRUE, 4, { 64, 128, 255, 255 }, 1.0f, 0 },
};

static GLuint
Compile(GLenum Stage, const char *Text)
{
    PFN_CREATE_SHADER CreateShader = (PFN_CREATE_SHADER)wglGetProcAddress("glCreateShader");
    PFN_SHADER_SOURCE ShaderSource = (PFN_SHADER_SOURCE)wglGetProcAddress("glShaderSource");
    PFN_COMPILE_SHADER CompileShader = (PFN_COMPILE_SHADER)wglGetProcAddress("glCompileShader");
    GLuint Shader = CreateShader(Stage);
    const GLchar_ *Source = Text;

    ShaderSource(Shader, 1, &Source, NULL);
    CompileShader(Shader);
    return Shader;
}

static void
DrawCase(const ATTRIB_CASE *Case, GLint Value, GLint Scale)
{
    static const float Positions[] = { -1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f };
    PFN_UNIFORM4F Uniform4f = (PFN_UNIFORM4F)wglGetProcAddress("glUniform4f");
    PFN_ENABLE_ATTRIB EnableAttrib = (PFN_ENABLE_ATTRIB)wglGetProcAddress("glEnableVertexAttribArray");
    PFN_ATTRIB_POINTER AttribPointer = (PFN_ATTRIB_POINTER)wglGetProcAddress("glVertexAttribPointer");
    BYTE Vertices[4 * 8];
    BYTE Pixel[4] = {0};
    unsigned Index;

    for (Index = 0; Index < 4; ++Index)
    {
        if (Case->Stride == 4 && Case->Type != GL_UNSIGNED_BYTE && Case->Type != GL_BYTE)
            memcpy(Vertices + Index * Case->Stride, &Case->Packed, 4);
        else
            memcpy(Vertices + Index * Case->Stride, Case->Data, Case->Stride);
    }
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    Uniform4f(Scale, Case->Scale, Case->Scale, Case->Scale, Case->Scale);
    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(2, GL_FLOAT, 0, Positions);
    EnableAttrib(Value);
    AttribPointer(Value, Case->Size, Case->Type, Case->Normalized, Case->Stride, Vertices);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glFinish();
    glReadBuffer(GL_BACK);
    glReadPixels(SIZE / 2, SIZE / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, Pixel);
    ok(abs(Pixel[0] - 255) <= TOLERANCE && abs(Pixel[1] - 128) <= TOLERANCE && abs(Pixel[2] - 64) <= TOLERANCE,
       "%s gave %u,%u,%u\n", Case->Name, Pixel[0], Pixel[1], Pixel[2]);
    ok(glGetError() == GL_NO_ERROR, "%s raised a GL error\n", Case->Name);
}

static void
RunCases(void)
{
    PFN_CREATE_PROGRAM CreateProgram = (PFN_CREATE_PROGRAM)wglGetProcAddress("glCreateProgram");
    PFN_ATTACH_SHADER AttachShader = (PFN_ATTACH_SHADER)wglGetProcAddress("glAttachShader");
    PFN_LINK_PROGRAM LinkProgram = (PFN_LINK_PROGRAM)wglGetProcAddress("glLinkProgram");
    PFN_GET_PROGRAMIV GetProgramiv = (PFN_GET_PROGRAMIV)wglGetProcAddress("glGetProgramiv");
    PFN_USE_PROGRAM UseProgram = (PFN_USE_PROGRAM)wglGetProcAddress("glUseProgram");
    PFN_GET_LOCATION GetAttribLocation = (PFN_GET_LOCATION)wglGetProcAddress("glGetAttribLocation");
    PFN_GET_LOCATION GetUniformLocation = (PFN_GET_LOCATION)wglGetProcAddress("glGetUniformLocation");
    GLuint Program;
    GLint Status = 0, Value, Scale;
    unsigned Index;

    if (!CreateProgram || !AttachShader || !LinkProgram || !GetProgramiv || !UseProgram ||
        !GetAttribLocation || !GetUniformLocation)
    {
        skip("This OpenGL driver has no shaders\n");
        return;
    }
    Program = CreateProgram();
    AttachShader(Program, Compile(GL_VERTEX_SHADER_, VertexSource));
    AttachShader(Program, Compile(GL_FRAGMENT_SHADER_, FragmentSource));
    LinkProgram(Program);
    GetProgramiv(Program, GL_LINK_STATUS_, &Status);
    ok(Status != 0, "The program did not link\n");
    if (Status == 0)
        return;
    UseProgram(Program);
    Value = GetAttribLocation(Program, "Value");
    Scale = GetUniformLocation(Program, "Scale");
    ok(Value >= 0 && Scale >= 0, "Got locations %d and %d\n", Value, Scale);
    if (Value < 0 || Scale < 0)
        return;
    glViewport(0, 0, SIZE, SIZE);
    for (Index = 0; Index < ARRAYSIZE(Cases); ++Index)
        DrawCase(&Cases[Index], Value, Scale);
}

START_TEST(wgl_scaled_attrib)
{
    PIXELFORMATDESCRIPTOR Pfd = { sizeof(Pfd), 1 };
    HWND Window;
    HGLRC Context;
    HDC Dc;
    int Format;

    Window = CreateWindowA("static", "scaled attributes", WS_POPUP | WS_VISIBLE, 0, 0, SIZE, SIZE, NULL, NULL, NULL, NULL);
    ok(Window != NULL, "CreateWindow failed: %lu\n", GetLastError());
    if (Window == NULL)
        return;
    Dc = GetDC(Window);
    Pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    Pfd.iPixelType = PFD_TYPE_RGBA;
    Pfd.cColorBits = 24;
    Format = ChoosePixelFormat(Dc, &Pfd);
    ok(Format != 0, "ChoosePixelFormat failed: %lu\n", GetLastError());
    if (Format != 0 && SetPixelFormat(Dc, Format, &Pfd))
    {
        Context = wglCreateContext(Dc);
        ok(Context != NULL, "wglCreateContext failed: %lu\n", GetLastError());
        if (Context != NULL && wglMakeCurrent(Dc, Context))
        {
            trace("renderer '%s'\n", glGetString(GL_RENDERER));
            RunCases();
            wglMakeCurrent(NULL, NULL);
        }
        if (Context != NULL)
            wglDeleteContext(Context);
    }
    ReleaseDC(Window, Dc);
    DestroyWindow(Window);
}
