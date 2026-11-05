/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     A fragment shader that reads only gl_FrontFacing draws and tells both faces apart
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <windows.h>
#include <GL/gl.h>
#include <stdlib.h>
#include <wine/test.h>

#define SIZE 64
#define TOLERANCE 8
#define GL_FRAGMENT_SHADER_ 0x8B30
#define GL_VERTEX_SHADER_ 0x8B31
#define GL_COMPILE_STATUS_ 0x8B81
#define GL_LINK_STATUS_ 0x8B82

typedef char GLchar_;
typedef GLuint (APIENTRY *PFN_CREATE_SHADER)(GLenum);
typedef void (APIENTRY *PFN_SHADER_SOURCE)(GLuint, GLsizei, const GLchar_ **, const GLint *);
typedef void (APIENTRY *PFN_COMPILE_SHADER)(GLuint);
typedef void (APIENTRY *PFN_GET_SHADERIV)(GLuint, GLenum, GLint *);
typedef GLuint (APIENTRY *PFN_CREATE_PROGRAM)(void);
typedef void (APIENTRY *PFN_ATTACH_SHADER)(GLuint, GLuint);
typedef void (APIENTRY *PFN_LINK_PROGRAM)(GLuint);
typedef void (APIENTRY *PFN_GET_PROGRAMIV)(GLuint, GLenum, GLint *);
typedef void (APIENTRY *PFN_USE_PROGRAM)(GLuint);
typedef void (APIENTRY *PFN_ENABLE_ATTRIB)(GLuint);
typedef void (APIENTRY *PFN_ATTRIB_POINTER)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void *);

static const char VertexSource[] =
    "#version 120\n"
    "void main() { gl_Position = gl_Vertex; }\n";
static const char FragmentSource[] =
    "#version 120\n"
    "void main() { gl_FragColor = gl_FrontFacing ? vec4(1.0, 0.0, 0.0, 1.0) : vec4(0.0, 1.0, 0.0, 1.0); }\n";
static const char Vertex150Source[] =
    "#version 150\n"
    "#extension GL_ARB_explicit_attrib_location : enable\n"
    "invariant gl_Position;\n"
    "layout(location = 0) in vec4 vs_in0;\n"
    "out shader_in_out { vec4 reg[10]; } shader_out;\n"
    "void main() { gl_Position = vs_in0; }\n";
static const char Fragment150Source[] =
    "#version 150\n"
    "#extension GL_ARB_explicit_attrib_location : enable\n"
    "layout(location = 0) out vec4 color_out0;\n"
    "void main() { color_out0 = (gl_FrontFacing ? 1.0 : -1.0) >= 0.0 ? vec4(1.0, 0.0, 0.0, 1.0) : vec4(0.0, 1.0, 0.0, 1.0); }\n";

static BOOL
BuildProgram(const char *VertexText, const char *FragmentText)
{
    PFN_CREATE_SHADER CreateShader = (PFN_CREATE_SHADER)wglGetProcAddress("glCreateShader");
    PFN_SHADER_SOURCE ShaderSource = (PFN_SHADER_SOURCE)wglGetProcAddress("glShaderSource");
    PFN_COMPILE_SHADER CompileShader = (PFN_COMPILE_SHADER)wglGetProcAddress("glCompileShader");
    PFN_GET_SHADERIV GetShaderiv = (PFN_GET_SHADERIV)wglGetProcAddress("glGetShaderiv");
    PFN_CREATE_PROGRAM CreateProgram = (PFN_CREATE_PROGRAM)wglGetProcAddress("glCreateProgram");
    PFN_ATTACH_SHADER AttachShader = (PFN_ATTACH_SHADER)wglGetProcAddress("glAttachShader");
    PFN_LINK_PROGRAM LinkProgram = (PFN_LINK_PROGRAM)wglGetProcAddress("glLinkProgram");
    PFN_GET_PROGRAMIV GetProgramiv = (PFN_GET_PROGRAMIV)wglGetProcAddress("glGetProgramiv");
    PFN_USE_PROGRAM UseProgram = (PFN_USE_PROGRAM)wglGetProcAddress("glUseProgram");
    const GLchar_ *Source;
    GLuint Vertex, Fragment, Program;
    GLint Status = 0;

    if (!CreateShader || !ShaderSource || !CompileShader || !GetShaderiv || !CreateProgram ||
        !AttachShader || !LinkProgram || !GetProgramiv || !UseProgram)
    {
        return FALSE;
    }
    Vertex = CreateShader(GL_VERTEX_SHADER_);
    Source = VertexText;
    ShaderSource(Vertex, 1, &Source, NULL);
    CompileShader(Vertex);
    GetShaderiv(Vertex, GL_COMPILE_STATUS_, &Status);
    ok(Status != 0, "The vertex shader did not compile\n");
    Fragment = CreateShader(GL_FRAGMENT_SHADER_);
    Source = FragmentText;
    ShaderSource(Fragment, 1, &Source, NULL);
    CompileShader(Fragment);
    GetShaderiv(Fragment, GL_COMPILE_STATUS_, &Status);
    ok(Status != 0, "The fragment shader did not compile\n");
    Program = CreateProgram();
    AttachShader(Program, Vertex);
    AttachShader(Program, Fragment);
    LinkProgram(Program);
    GetProgramiv(Program, GL_LINK_STATUS_, &Status);
    ok(Status != 0, "The program did not link\n");
    UseProgram(Program);
    return Status != 0;
}

static void
CheckCenter(const char *What, int Red, int Green)
{
    BYTE Pixel[4] = {0};

    glFinish();
    glReadBuffer(GL_BACK);
    glReadPixels(SIZE / 2, SIZE / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, Pixel);
    ok(abs(Pixel[0] - Red) <= TOLERANCE && abs(Pixel[1] - Green) <= TOLERANCE && Pixel[2] <= TOLERANCE,
       "%s gave %u,%u,%u\n", What, Pixel[0], Pixel[1], Pixel[2]);
}

static void
DrawQuad(BOOL CounterClockwise)
{
    glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glBegin(GL_TRIANGLE_STRIP);
    if (CounterClockwise)
    {
        glVertex2f(-1.0f, -1.0f);
        glVertex2f(1.0f, -1.0f);
        glVertex2f(-1.0f, 1.0f);
        glVertex2f(1.0f, 1.0f);
    }
    else
    {
        glVertex2f(-1.0f, -1.0f);
        glVertex2f(-1.0f, 1.0f);
        glVertex2f(1.0f, -1.0f);
        glVertex2f(1.0f, 1.0f);
    }
    glEnd();
}

static void
DrawAttributeQuad(void)
{
    static const float Positions[] =
    {
        -1.0f, -1.0f, 0.1f,  1.0f, -1.0f, 0.1f,  -1.0f, 1.0f, 0.1f,
         1.0f, -1.0f, 0.1f,  1.0f,  1.0f, 0.1f,  -1.0f, 1.0f, 0.1f,
    };
    PFN_ENABLE_ATTRIB EnableAttrib = (PFN_ENABLE_ATTRIB)wglGetProcAddress("glEnableVertexAttribArray");
    PFN_ATTRIB_POINTER AttribPointer = (PFN_ATTRIB_POINTER)wglGetProcAddress("glVertexAttribPointer");

    ok(EnableAttrib != NULL && AttribPointer != NULL, "Vertex attribute functions are missing\n");
    if (EnableAttrib == NULL || AttribPointer == NULL)
        return;
    glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    EnableAttrib(0);
    AttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, Positions);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    CheckCenter("A GLSL 150 quad from a three component attribute", 255, 0);
}

START_TEST(wgl_front_facing)
{
    PIXELFORMATDESCRIPTOR Pfd = { sizeof(Pfd), 1 };
    HWND Window;
    HGLRC Context;
    HDC Dc;
    int Format;

    Window = CreateWindowA("static", "front facing", WS_POPUP | WS_VISIBLE, 0, 0, SIZE, SIZE, NULL, NULL, NULL, NULL);
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
            if (BuildProgram(VertexSource, FragmentSource))
            {
                glViewport(0, 0, SIZE, SIZE);
                DrawQuad(TRUE);
                CheckCenter("A counter-clockwise quad", 255, 0);
                DrawQuad(FALSE);
                CheckCenter("A clockwise quad", 0, 255);
                if (BuildProgram(Vertex150Source, Fragment150Source))
                    DrawAttributeQuad();
            }
            else
            {
                skip("This OpenGL driver has no shaders\n");
            }
            wglMakeCurrent(NULL, NULL);
        }
        if (Context != NULL)
            wglDeleteContext(Context);
    }
    ReleaseDC(Window, Dc);
    DestroyWindow(Window);
}
