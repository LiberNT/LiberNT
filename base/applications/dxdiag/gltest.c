/*
 * PROJECT:     LiberNT DirectX Diagnostic Tool
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     OpenGL 1.1 and OpenGL 3.3 core profile rendering benchmarks
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#include <GL/gl.h>
#include <GL/glext.h>

/* WGL_ARB_create_context and WGL_ARB_create_context_profile, Khronos OpenGL registry */
#define WGL_CONTEXT_MAJOR_VERSION_ARB 0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB 0x2092
#define WGL_CONTEXT_PROFILE_MASK_ARB 0x9126
#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB 0x00000001

typedef HGLRC (WINAPI *PFN_WGL_CREATE_CONTEXT_ATTRIBS)(HDC hDC, HGLRC hShareContext, const int *Attributes);
typedef BOOL (WINAPI *PFN_WGL_SWAP_INTERVAL)(int Interval);

typedef struct
{
    HWND hWnd;
    HDC hDC;
    HGLRC Context;
    UINT Width;
    UINT Height;
} GL_WINDOW;

typedef struct
{
    PFNGLGENVERTEXARRAYSPROC GenVertexArrays;
    PFNGLBINDVERTEXARRAYPROC BindVertexArray;
    PFNGLDELETEVERTEXARRAYSPROC DeleteVertexArrays;
    PFNGLGENBUFFERSPROC GenBuffers;
    PFNGLBINDBUFFERPROC BindBuffer;
    PFNGLBUFFERDATAPROC BufferData;
    PFNGLDELETEBUFFERSPROC DeleteBuffers;
    PFNGLCREATESHADERPROC CreateShader;
    PFNGLSHADERSOURCEPROC ShaderSource;
    PFNGLCOMPILESHADERPROC CompileShader;
    PFNGLGETSHADERIVPROC GetShaderiv;
    PFNGLDELETESHADERPROC DeleteShader;
    PFNGLCREATEPROGRAMPROC CreateProgram;
    PFNGLATTACHSHADERPROC AttachShader;
    PFNGLLINKPROGRAMPROC LinkProgram;
    PFNGLGETPROGRAMIVPROC GetProgramiv;
    PFNGLUSEPROGRAMPROC UseProgram;
    PFNGLDELETEPROGRAMPROC DeleteProgram;
    PFNGLVERTEXATTRIBPOINTERPROC VertexAttribPointer;
    PFNGLENABLEVERTEXATTRIBARRAYPROC EnableVertexAttribArray;
} GL3_FUNCTIONS;

typedef struct
{
    GL3_FUNCTIONS Functions;
    BOOL Loaded;
    GLuint Program;
    GLuint Array;
    GLuint Buffer;
} GL3_STATE;

static const CHAR GL3VertexSource[] =
    "#version 330 core\n"
    "layout(location = 0) in vec4 Position;\n"
    "layout(location = 1) in vec4 Color;\n"
    "out vec4 VertexColor;\n"
    "void main() { gl_Position = Position; VertexColor = Color; }\n";

static const CHAR GL3FragmentSource[] =
    "#version 330 core\n"
    "in vec4 VertexColor;\n"
    "out vec4 FragmentColor;\n"
    "void main() { FragmentColor = VertexColor; }\n";

static BOOL GLCheck(PD3DTEST_RESULT Result, PCSTR Api, PCSTR Stage)
{
    GLenum Error = glGetError();

    if (Error == GL_NO_ERROR)
        return TRUE;
    return D3DTestFail(Result, Api, Stage, MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, Error));
}

static BOOL GLOpen(GL_WINDOW *Window, HWND hWnd, PCSTR Api, PD3DTEST_RESULT Result)
{
    PIXELFORMATDESCRIPTOR Format = { sizeof(Format), 1, PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER, PFD_TYPE_RGBA, 32 };
    RECT ClientRect;
    int Index;

    GetClientRect(hWnd, &ClientRect);
    Window->Width = ClientRect.right - ClientRect.left;
    Window->Height = ClientRect.bottom - ClientRect.top;
    Window->hWnd = hWnd;
    Window->hDC = GetDC(hWnd);
    if (!Window->hDC)
        return D3DTestFail(Result, Api, "get-dc", E_FAIL);
    Index = ChoosePixelFormat(Window->hDC, &Format);
    if (!Index)
        return D3DTestFail(Result, Api, "choose-pixel-format", HRESULT_FROM_WIN32(GetLastError()));
    if (!SetPixelFormat(Window->hDC, Index, &Format))
        return D3DTestFail(Result, Api, "set-pixel-format", HRESULT_FROM_WIN32(GetLastError()));
    Window->Context = wglCreateContext(Window->hDC);
    if (!Window->Context)
        return D3DTestFail(Result, Api, "create-context", HRESULT_FROM_WIN32(GetLastError()));
    if (!wglMakeCurrent(Window->hDC, Window->Context))
        return D3DTestFail(Result, Api, "make-current", HRESULT_FROM_WIN32(GetLastError()));
    return TRUE;
}

static VOID GLClose(GL_WINDOW *Window)
{
    wglMakeCurrent(NULL, NULL);
    if (Window->Context)
        wglDeleteContext(Window->Context);
    if (Window->hDC)
        ReleaseDC(Window->hWnd, Window->hDC);
}

static VOID GLDisableSwapInterval(VOID)
{
    PFN_WGL_SWAP_INTERVAL SwapInterval = (PFN_WGL_SWAP_INTERVAL)wglGetProcAddress("wglSwapIntervalEXT");

    if (SwapInterval)
        SwapInterval(0);
}

static BOOL GLPresent(GL_WINDOW *Window, PCSTR Api, PD3DTEST_RESULT Result)
{
    if (!GLCheck(Result, Api, "draw"))
        return FALSE;
    if (!SwapBuffers(Window->hDC))
        return D3DTestFail(Result, Api, "present", HRESULT_FROM_WIN32(GetLastError()));
    Result->Frames++;
    return TRUE;
}

static BOOL GL1Render(GL_WINDOW *Window, PD3DTEST_RESULT Result)
{
    D3DTEST_VERTEX Vertices[D3DTEST_MAX_VERTEX_COUNT];
    D3DTEST_CLIP_VERTEX Clip[D3DTEST_MAX_VERTEX_COUNT];
    DWORD Start, Elapsed;
    UINT Count;

    GLDisableSwapInterval();
    glViewport(0, 0, Window->Width, Window->Height);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glVertexPointer(4, GL_FLOAT, sizeof(Clip[0]), Clip[0].Position);
    glColorPointer(4, GL_FLOAT, sizeof(Clip[0]), Clip[0].Color);
    if (!GLCheck(Result, "GL1", "setup"))
        return FALSE;

    Start = GetTickCount();
    while (D3DTestContinue(Result, Start, &Elapsed))
    {
        Count = D3DTestBuildScene(Vertices, Elapsed, Window->Width, Window->Height, Result);
        D3DTestToClipSpace(Clip, Vertices, Count, Window->Width, Window->Height);
        glClear(GL_COLOR_BUFFER_BIT);
        glDrawArrays(GL_TRIANGLES, 0, Count);
        if (!GLPresent(Window, "GL1", Result))
            return FALSE;
    }
    return !Result->Stage;
}

BOOL GL1Test(GUID *lpDevice, HWND hWnd, BOOL BootMode, PD3DTEST_RESULT Result)
{
    GL_WINDOW Window = {0};
    BOOL Passed;

    UNREFERENCED_PARAMETER(lpDevice);
    UNREFERENCED_PARAMETER(BootMode);

    Passed = GLOpen(&Window, hWnd, "GL1", Result) && GL1Render(&Window, Result);
    GLClose(&Window);
    return Passed;
}

static BOOL GL3OpenCore(GL_WINDOW *Window, HWND hWnd, PD3DTEST_RESULT Result)
{
    static const int Attributes[] =
    {
        WGL_CONTEXT_MAJOR_VERSION_ARB, 3,
        WGL_CONTEXT_MINOR_VERSION_ARB, 3,
        WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
        0
    };
    PFN_WGL_CREATE_CONTEXT_ATTRIBS CreateContextAttribs;
    HGLRC Context;

    if (!GLOpen(Window, hWnd, "GL3", Result))
        return FALSE;
    CreateContextAttribs = (PFN_WGL_CREATE_CONTEXT_ATTRIBS)wglGetProcAddress("wglCreateContextAttribsARB");
    if (!CreateContextAttribs)
        return D3DTestFail(Result, "GL3", "create-context-attribs", E_NOTIMPL);
    Context = CreateContextAttribs(Window->hDC, NULL, Attributes);
    if (!Context)
        return D3DTestFail(Result, "GL3", "create-core-context", HRESULT_FROM_WIN32(GetLastError()));

    wglMakeCurrent(NULL, NULL);
    wglDeleteContext(Window->Context);
    Window->Context = Context;
    if (!wglMakeCurrent(Window->hDC, Context))
        return D3DTestFail(Result, "GL3", "make-current", HRESULT_FROM_WIN32(GetLastError()));
    return TRUE;
}

static GLuint GL3CompileShader(const GL3_FUNCTIONS *Functions, GLenum Type, const GLchar *Source)
{
    GLuint Shader = Functions->CreateShader(Type);
    GLint Compiled = GL_FALSE;

    if (!Shader)
        return 0;
    Functions->ShaderSource(Shader, 1, &Source, NULL);
    Functions->CompileShader(Shader);
    Functions->GetShaderiv(Shader, GL_COMPILE_STATUS, &Compiled);
    if (Compiled)
        return Shader;
    Functions->DeleteShader(Shader);
    return 0;
}

static BOOL GL3CreateProgram(GL3_STATE *State, PD3DTEST_RESULT Result)
{
    const GL3_FUNCTIONS *Functions = &State->Functions;
    GLuint VertexShader, FragmentShader;
    GLint Linked = GL_FALSE;

    VertexShader = GL3CompileShader(Functions, GL_VERTEX_SHADER, GL3VertexSource);
    if (!VertexShader)
        return D3DTestFail(Result, "GL3", "compile-vertex-shader", E_FAIL);
    FragmentShader = GL3CompileShader(Functions, GL_FRAGMENT_SHADER, GL3FragmentSource);
    if (!FragmentShader)
    {
        Functions->DeleteShader(VertexShader);
        return D3DTestFail(Result, "GL3", "compile-fragment-shader", E_FAIL);
    }

    State->Program = Functions->CreateProgram();
    if (State->Program)
    {
        Functions->AttachShader(State->Program, VertexShader);
        Functions->AttachShader(State->Program, FragmentShader);
        Functions->LinkProgram(State->Program);
        Functions->GetProgramiv(State->Program, GL_LINK_STATUS, &Linked);
    }
    Functions->DeleteShader(FragmentShader);
    Functions->DeleteShader(VertexShader);
    if (!Linked)
        return D3DTestFail(Result, "GL3", "link-program", E_FAIL);
    return TRUE;
}

static PROC GL3Load(PCSTR Name, BOOL *Loaded)
{
    PROC Function = wglGetProcAddress(Name);

    if (!Function)
        *Loaded = FALSE;
    return Function;
}

static BOOL GL3LoadFunctions(GL3_FUNCTIONS *Functions)
{
    BOOL Loaded = TRUE;

    Functions->GenVertexArrays = (PFNGLGENVERTEXARRAYSPROC)GL3Load("glGenVertexArrays", &Loaded);
    Functions->BindVertexArray = (PFNGLBINDVERTEXARRAYPROC)GL3Load("glBindVertexArray", &Loaded);
    Functions->DeleteVertexArrays = (PFNGLDELETEVERTEXARRAYSPROC)GL3Load("glDeleteVertexArrays", &Loaded);
    Functions->GenBuffers = (PFNGLGENBUFFERSPROC)GL3Load("glGenBuffers", &Loaded);
    Functions->BindBuffer = (PFNGLBINDBUFFERPROC)GL3Load("glBindBuffer", &Loaded);
    Functions->BufferData = (PFNGLBUFFERDATAPROC)GL3Load("glBufferData", &Loaded);
    Functions->DeleteBuffers = (PFNGLDELETEBUFFERSPROC)GL3Load("glDeleteBuffers", &Loaded);
    Functions->CreateShader = (PFNGLCREATESHADERPROC)GL3Load("glCreateShader", &Loaded);
    Functions->ShaderSource = (PFNGLSHADERSOURCEPROC)GL3Load("glShaderSource", &Loaded);
    Functions->CompileShader = (PFNGLCOMPILESHADERPROC)GL3Load("glCompileShader", &Loaded);
    Functions->GetShaderiv = (PFNGLGETSHADERIVPROC)GL3Load("glGetShaderiv", &Loaded);
    Functions->DeleteShader = (PFNGLDELETESHADERPROC)GL3Load("glDeleteShader", &Loaded);
    Functions->CreateProgram = (PFNGLCREATEPROGRAMPROC)GL3Load("glCreateProgram", &Loaded);
    Functions->AttachShader = (PFNGLATTACHSHADERPROC)GL3Load("glAttachShader", &Loaded);
    Functions->LinkProgram = (PFNGLLINKPROGRAMPROC)GL3Load("glLinkProgram", &Loaded);
    Functions->GetProgramiv = (PFNGLGETPROGRAMIVPROC)GL3Load("glGetProgramiv", &Loaded);
    Functions->UseProgram = (PFNGLUSEPROGRAMPROC)GL3Load("glUseProgram", &Loaded);
    Functions->DeleteProgram = (PFNGLDELETEPROGRAMPROC)GL3Load("glDeleteProgram", &Loaded);
    Functions->VertexAttribPointer = (PFNGLVERTEXATTRIBPOINTERPROC)GL3Load("glVertexAttribPointer", &Loaded);
    Functions->EnableVertexAttribArray = (PFNGLENABLEVERTEXATTRIBARRAYPROC)GL3Load("glEnableVertexAttribArray", &Loaded);
    return Loaded;
}

static BOOL GL3Setup(GL3_STATE *State, PD3DTEST_RESULT Result)
{
    const GL3_FUNCTIONS *Functions = &State->Functions;

    if (!GL3LoadFunctions(&State->Functions))
        return D3DTestFail(Result, "GL3", "load-functions", E_NOTIMPL);
    State->Loaded = TRUE;
    if (!GL3CreateProgram(State, Result))
        return FALSE;

    Functions->GenVertexArrays(1, &State->Array);
    Functions->BindVertexArray(State->Array);
    Functions->GenBuffers(1, &State->Buffer);
    Functions->BindBuffer(GL_ARRAY_BUFFER, State->Buffer);
    Functions->BufferData(GL_ARRAY_BUFFER, D3DTEST_MAX_VERTEX_COUNT * sizeof(D3DTEST_CLIP_VERTEX), NULL, GL_STREAM_DRAW);
    Functions->VertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(D3DTEST_CLIP_VERTEX), (const GLvoid *)(ULONG_PTR)FIELD_OFFSET(D3DTEST_CLIP_VERTEX, Position));
    Functions->EnableVertexAttribArray(0);
    Functions->VertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(D3DTEST_CLIP_VERTEX), (const GLvoid *)(ULONG_PTR)FIELD_OFFSET(D3DTEST_CLIP_VERTEX, Color));
    Functions->EnableVertexAttribArray(1);
    Functions->UseProgram(State->Program);
    return GLCheck(Result, "GL3", "setup");
}

static BOOL GL3Render(GL_WINDOW *Window, GL3_STATE *State, PD3DTEST_RESULT Result)
{
    D3DTEST_VERTEX Vertices[D3DTEST_MAX_VERTEX_COUNT];
    D3DTEST_CLIP_VERTEX Clip[D3DTEST_MAX_VERTEX_COUNT];
    DWORD Start, Elapsed;
    UINT Count;

    if (!GL3Setup(State, Result))
        return FALSE;
    GLDisableSwapInterval();
    glViewport(0, 0, Window->Width, Window->Height);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    Start = GetTickCount();
    while (D3DTestContinue(Result, Start, &Elapsed))
    {
        Count = D3DTestBuildScene(Vertices, Elapsed, Window->Width, Window->Height, Result);
        D3DTestToClipSpace(Clip, Vertices, Count, Window->Width, Window->Height);
        State->Functions.BufferData(GL_ARRAY_BUFFER, Count * sizeof(Clip[0]), Clip, GL_STREAM_DRAW);
        glClear(GL_COLOR_BUFFER_BIT);
        glDrawArrays(GL_TRIANGLES, 0, Count);
        if (!GLPresent(Window, "GL3", Result))
            return FALSE;
    }
    return !Result->Stage;
}

static VOID GL3Release(GL3_STATE *State)
{
    if (!State->Loaded)
        return;
    if (State->Buffer)
        State->Functions.DeleteBuffers(1, &State->Buffer);
    if (State->Array)
        State->Functions.DeleteVertexArrays(1, &State->Array);
    if (State->Program)
        State->Functions.DeleteProgram(State->Program);
}

BOOL GL3Test(GUID *lpDevice, HWND hWnd, BOOL BootMode, PD3DTEST_RESULT Result)
{
    GL_WINDOW Window = {0};
    GL3_STATE State = {0};
    BOOL Passed = FALSE;

    UNREFERENCED_PARAMETER(lpDevice);
    UNREFERENCED_PARAMETER(BootMode);

    if (GL3OpenCore(&Window, hWnd, Result))
    {
        Passed = GL3Render(&Window, &State, Result);
        GL3Release(&State);
    }
    GLClose(&Window);
    return Passed;
}
