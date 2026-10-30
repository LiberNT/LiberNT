/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Window, GDI and OpenGL program exercising a process that may run under an emulation host
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define COBJMACROS
#include <windows.h>
#include <d3d9.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <stdio.h>

#define TEST_CLASS       L"EmulationGuiTest"
#define TEST_CREATE_DATA ((PVOID)(ULONG_PTR)0x5EED1234)
#define WM_TEST_PING     (WM_APP + 1)
#define TIMER_TICK       1
#define TIMER_GUARD      2
#define TICK_COUNT       3
#define DIB_SIZE         64
#define DIB_RGB(Pixel)   ((Pixel) & 0x00FFFFFF)
#define GL_TEST_DEBUG_OUTPUT             0x92E0
#define GL_TEST_DEBUG_OUTPUT_SYNCHRONOUS 0x8242
#define GL_TEST_DEBUG_TYPE_ERROR         0x824C
#define GL_TEST_DEBUG_USER               ((const void *)(ULONG_PTR)0x5EED5678)
#define WGL_TEST_CONTEXT_MAJOR_VERSION   0x2091
#define WGL_TEST_CONTEXT_MINOR_VERSION   0x2092
#define WGL_TEST_CONTEXT_PROFILE_MASK    0x9126
#define WGL_TEST_CONTEXT_CORE_PROFILE    0x0001
#define GL_TEST_MAP_PERSISTENT_BIT       0x0040
#define GL_TEST_MAP_COHERENT_BIT         0x0080
#define GL_TEST_DYNAMIC_STORAGE_BIT      0x0100

typedef void (APIENTRY *PGL_TEST_DEBUG_PROC)(GLenum, GLenum, GLuint, GLenum, GLsizei, const GLchar *, const void *);
typedef void (APIENTRY *PGL_TEST_DEBUG_MESSAGE_CALLBACK)(PGL_TEST_DEBUG_PROC, const void *);
typedef HGLRC (WINAPI *PGL_TEST_CREATE_CONTEXT_ATTRIBS)(HDC, HGLRC, const int *);
typedef void (APIENTRY *PGL_TEST_BUFFER_STORAGE)(GLenum, GLsizeiptr, const void *, GLbitfield);
typedef IDirect3D9 *(WINAPI *PD3D_TEST_CREATE9)(UINT);

typedef struct _D3D_TEST_VERTEX
{
    FLOAT X, Y, Z, Rhw;
    FLOAT U, V;
} D3D_TEST_VERTEX;

typedef struct _D3D_TEST_COLOR_VERTEX
{
    FLOAT X, Y, Z, Rhw;
    D3DCOLOR Diffuse;
} D3D_TEST_COLOR_VERTEX;

static LONG Checks;
static LONG Failures;
static DWORD WindowThread;
static LONG NcCreateSeen;
static LONG CreateSeen;
static LONG PingSeen;
static LONG TimerSeen;
static LONG PaintSeen;
static LONG DestroySeen;
static LONG NcDestroySeen;
static LONG HookSeen;
static BOOL GuardFired;
static LONG DebugMessages;
static GLenum DebugType;
static const void *DebugUser;

#define CHECK(Expression) \
    do \
    { \
        InterlockedIncrement(&Checks); \
        if (!(Expression)) \
        { \
            InterlockedIncrement(&Failures); \
            printf("EMUGUI: FAIL %s:%d error=%lu\n", __FILE__, __LINE__, GetLastError()); \
        } \
    } while (0)

static
LRESULT
CALLBACK
WindowProc(
    _In_ HWND Window,
    _In_ UINT Message,
    _In_ WPARAM WParam,
    _In_ LPARAM LParam)
{
    PAINTSTRUCT Paint;
    HBRUSH Brush;
    HDC Dc;

    switch (Message)
    {
        case WM_NCCREATE:
            NcCreateSeen++;
            CHECK(((LPCREATESTRUCTW)LParam)->lpCreateParams == TEST_CREATE_DATA);
            break;

        case WM_CREATE:
            CreateSeen++;
            CHECK(((LPCREATESTRUCTW)LParam)->lpCreateParams == TEST_CREATE_DATA);
            return 0;

        case WM_TEST_PING:
            PingSeen++;
            CHECK(GetCurrentThreadId() == WindowThread);
            return WParam * 3 + LParam;

        case WM_TIMER:
            if (WParam == TIMER_TICK && ++TimerSeen == TICK_COUNT)
            {
                KillTimer(Window, TIMER_TICK);
                PostQuitMessage(0);
            }
            else if (WParam == TIMER_GUARD)
            {
                GuardFired = TRUE;
                PostQuitMessage(1);
            }
            return 0;

        case WM_PAINT:
            Dc = BeginPaint(Window, &Paint);
            CHECK(Dc != NULL);
            Brush = CreateSolidBrush(RGB(0, 0, 255));
            FillRect(Dc, &Paint.rcPaint, Brush);
            DeleteObject(Brush);
            EndPaint(Window, &Paint);
            PaintSeen++;
            return 0;

        case WM_DESTROY:
            DestroySeen++;
            return 0;

        case WM_NCDESTROY:
            NcDestroySeen++;
            break;
    }

    return DefWindowProcW(Window, Message, WParam, LParam);
}

static
LRESULT
CALLBACK
CallWndProcHook(
    _In_ int Code,
    _In_ WPARAM WParam,
    _In_ LPARAM LParam)
{
    if (Code == HC_ACTION && ((PCWPSTRUCT)LParam)->message == WM_TEST_PING) HookSeen++;
    return CallNextHookEx(NULL, Code, WParam, LParam);
}

static
DWORD
WINAPI
SenderThread(
    _In_ PVOID Parameter)
{
    DWORD_PTR Result = 0;

    CHECK(SendMessageTimeoutW((HWND)Parameter, WM_TEST_PING, 5, 2, SMTO_NORMAL, 15000, &Result) != 0);
    CHECK(Result == 17);
    return 0;
}

static
BOOL
CALLBACK
CountChild(
    _In_ HWND Window,
    _In_ LPARAM Count)
{
    (*(PLONG)Count)++;
    return TRUE;
}

static
VOID
TestWindow(VOID)
{
    WNDCLASSEXW Class = { sizeof(Class) };
    HANDLE Sender;
    HHOOK Hook;
    HWND Window, Button;
    WCHAR Text[32];
    LONG Children = 0;
    MSG Message;
    ATOM Atom;

    WindowThread = GetCurrentThreadId();
    Class.lpfnWndProc = WindowProc;
    Class.hInstance = GetModuleHandleW(NULL);
    Class.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    Class.lpszClassName = TEST_CLASS;
    Atom = RegisterClassExW(&Class);
    CHECK(Atom != 0);

    Window = CreateWindowExW(0, TEST_CLASS, L"Emulation GUI test", WS_OVERLAPPEDWINDOW,
                             40, 40, 320, 240, NULL, NULL, Class.hInstance, TEST_CREATE_DATA);
    CHECK(Window != NULL);
    if (!Window) return;
    CHECK(NcCreateSeen == 1 && CreateSeen == 1);
    CHECK(GetWindowTextW(Window, Text, RTL_NUMBER_OF(Text)) == 18 && !wcscmp(Text, L"Emulation GUI test"));

    Button = CreateWindowExW(0, L"BUTTON", L"Press", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                             8, 8, 80, 24, Window, (HMENU)100, Class.hInstance, NULL);
    CHECK(Button != NULL);
    CHECK(GetDlgItem(Window, 100) == Button);
    CHECK(GetWindowTextW(Button, Text, RTL_NUMBER_OF(Text)) == 5 && !wcscmp(Text, L"Press"));
    EnumChildWindows(Window, CountChild, (LPARAM)&Children);
    CHECK(Children == 1);

    ShowWindow(Window, SW_SHOW);
    CHECK(UpdateWindow(Window));
    CHECK(PaintSeen >= 1);

    CHECK(SendMessageW(Window, WM_TEST_PING, 1, 1) == 4);

    Hook = SetWindowsHookExW(WH_CALLWNDPROC, CallWndProcHook, NULL, WindowThread);
    CHECK(Hook != NULL);
    Sender = CreateThread(NULL, 0, SenderThread, Window, 0, NULL);
    CHECK(Sender != NULL);

    CHECK(SetTimer(Window, TIMER_TICK, 20, NULL) == TIMER_TICK);
    CHECK(SetTimer(Window, TIMER_GUARD, 20000, NULL) == TIMER_GUARD);
    while (GetMessageW(&Message, NULL, 0, 0) > 0)
    {
        TranslateMessage(&Message);
        DispatchMessageW(&Message);
    }
    KillTimer(Window, TIMER_GUARD);
    CHECK(!GuardFired);
    CHECK(Message.message == WM_QUIT && Message.wParam == 0);
    CHECK(TimerSeen == TICK_COUNT);

    if (Sender)
    {
        while (MsgWaitForMultipleObjects(1, &Sender, FALSE, 15000, QS_ALLINPUT) == WAIT_OBJECT_0 + 1)
        {
            while (PeekMessageW(&Message, NULL, 0, 0, PM_REMOVE)) DispatchMessageW(&Message);
        }
        CloseHandle(Sender);
    }
    CHECK(PingSeen == 2);
    CHECK(HookSeen >= 1);
    if (Hook) CHECK(UnhookWindowsHookEx(Hook));

    CHECK(DestroyWindow(Window));
    CHECK(DestroySeen == 1 && NcDestroySeen == 1);
    CHECK(!IsWindow(Window));
    CHECK(UnregisterClassW(TEST_CLASS, Class.hInstance));
    printf("EMUGUI: window created, %ld pings, %ld timer ticks, %ld paints, %ld hook calls\n",
           PingSeen, TimerSeen, PaintSeen, HookSeen);
}

static
HBITMAP
CreateTestDib(
    _In_ HDC Dc,
    _Out_ PULONG *Bits)
{
    BITMAPINFO Info = { { sizeof(BITMAPINFOHEADER) } };

    Info.bmiHeader.biWidth = DIB_SIZE;
    Info.bmiHeader.biHeight = -DIB_SIZE;
    Info.bmiHeader.biPlanes = 1;
    Info.bmiHeader.biBitCount = 32;
    Info.bmiHeader.biCompression = BI_RGB;
    return CreateDIBSection(Dc, &Info, DIB_RGB_COLORS, (PVOID *)Bits, NULL, 0);
}

static
VOID
TestGdi(VOID)
{
    RECT Red = { 10, 10, 20, 20 }, Box;
    HBITMAP Bitmap, CopyBitmap, OldBitmap, OldCopy;
    PULONG Bits = NULL, CopyBits = NULL;
    HDC Dc, CopyDc;
    HBRUSH Brush;
    HRGN Region, Other;
    ULONG Index, Dark = 0;

    Dc = CreateCompatibleDC(NULL);
    CopyDc = CreateCompatibleDC(NULL);
    CHECK(Dc != NULL && CopyDc != NULL);
    Bitmap = CreateTestDib(Dc, &Bits);
    CopyBitmap = CreateTestDib(CopyDc, &CopyBits);
    CHECK(Bitmap != NULL && Bits != NULL && CopyBitmap != NULL && CopyBits != NULL);
    if (!Bitmap || !CopyBitmap) return;

    OldBitmap = SelectObject(Dc, Bitmap);
    OldCopy = SelectObject(CopyDc, CopyBitmap);
    CHECK(PatBlt(Dc, 0, 0, DIB_SIZE, DIB_SIZE, WHITENESS));
    Brush = CreateSolidBrush(RGB(255, 0, 0));
    CHECK(FillRect(Dc, &Red, Brush));
    DeleteObject(Brush);
    CHECK(SetBkMode(Dc, TRANSPARENT) != 0);
    CHECK(SetTextColor(Dc, RGB(0, 0, 0)) != CLR_INVALID);
    CHECK(TextOutW(Dc, 24, 30, L"Hi", 2));
    CHECK(MoveToEx(Dc, 0, 60, NULL));
    CHECK(LineTo(Dc, DIB_SIZE, 60));
    GdiFlush();

    CHECK(DIB_RGB(Bits[15 * DIB_SIZE + 15]) == 0x00FF0000);
    CHECK(DIB_RGB(Bits[2 * DIB_SIZE + 2]) == 0x00FFFFFF);
    CHECK(GetPixel(Dc, 15, 15) == RGB(255, 0, 0));
    CHECK(DIB_RGB(Bits[60 * DIB_SIZE + 32]) == 0x00000000);
    for (Index = 24 * DIB_SIZE; Index < 56 * DIB_SIZE; Index++)
    {
        if ((Index % DIB_SIZE) >= 24 && DIB_RGB(Bits[Index]) != 0x00FFFFFF) Dark++;
    }
    CHECK(Dark > 0);

    CHECK(BitBlt(CopyDc, 0, 0, DIB_SIZE, DIB_SIZE, Dc, 0, 0, SRCCOPY));
    GdiFlush();
    CHECK(!memcmp(Bits, CopyBits, DIB_SIZE * DIB_SIZE * sizeof(ULONG)));
    CHECK(StretchBlt(CopyDc, 0, 0, DIB_SIZE / 2, DIB_SIZE / 2, Dc, 0, 0, DIB_SIZE, DIB_SIZE, SRCCOPY));
    GdiFlush();
    CHECK(DIB_RGB(CopyBits[7 * DIB_SIZE + 7]) == 0x00FF0000);

    Region = CreateRectRgn(0, 0, 10, 10);
    Other = CreateRectRgn(5, 5, 30, 20);
    CHECK(Region != NULL && Other != NULL);
    CHECK(CombineRgn(Region, Region, Other, RGN_OR) == COMPLEXREGION);
    CHECK(GetRgnBox(Region, &Box) == COMPLEXREGION);
    CHECK(Box.left == 0 && Box.top == 0 && Box.right == 30 && Box.bottom == 20);
    DeleteObject(Other);
    DeleteObject(Region);

    SelectObject(CopyDc, OldCopy);
    SelectObject(Dc, OldBitmap);
    DeleteObject(CopyBitmap);
    DeleteObject(Bitmap);
    DeleteDC(CopyDc);
    DeleteDC(Dc);
    printf("EMUGUI: GDI fill, text, line, blits and regions verified (%lu text pixels)\n", Dark);
}

static
BOOL
PixelIs(
    _In_ GLint X,
    _In_ GLint Y,
    _In_ UCHAR Red,
    _In_ UCHAR Green,
    _In_ UCHAR Blue)
{
    UCHAR Pixel[4] = { 0 };

    glReadPixels(X, Y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, Pixel);
    if (Pixel[0] == Red && Pixel[1] == Green && Pixel[2] == Blue) return TRUE;
    printf("EMUGUI: GL pixel %d,%d is %u,%u,%u,%u\n", X, Y, Pixel[0], Pixel[1], Pixel[2], Pixel[3]);
    return FALSE;
}

static
void
APIENTRY
DebugMessage(
    _In_ GLenum Source,
    _In_ GLenum Type,
    _In_ GLuint Id,
    _In_ GLenum Severity,
    _In_ GLsizei Length,
    _In_ const GLchar *Message,
    _In_ const void *User)
{
    if (Length != 0 && Message && Message[0]) InterlockedIncrement(&DebugMessages);
    DebugType = Type;
    DebugUser = User;
}

static
VOID
TestOpenGlCalls(VOID)
{
    static const UCHAR Texels[16] = { 255, 255, 0, 255, 255, 255, 0, 255, 255, 255, 0, 255, 255, 255, 0, 255 };
    static const GLfloat Vertices[6] = { 8.0f, 40.0f, 24.0f, 40.0f, 16.0f, 56.0f };
    PGL_TEST_DEBUG_MESSAGE_CALLBACK DebugMessageCallback;
    PFNGLGENBUFFERSPROC GenBuffers;
    PFNGLBINDBUFFERPROC BindBuffer;
    PFNGLBUFFERDATAPROC BufferData;
    PFNGLDELETEBUFFERSPROC DeleteBuffers;
    GLuint Texture, Buffer;

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, 64.0, 0.0, 64.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glColor3f(0.0f, 0.0f, 1.0f);
    glRectf(8.0f, 8.0f, 24.0f, 24.0f);
    glFinish();
    CHECK(PixelIs(16, 16, 0, 0, 255));
    CHECK(PixelIs(30, 16, 0, 0, 0));

    glGenTextures(1, &Texture);
    glBindTexture(GL_TEXTURE_2D, Texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, Texels);
    glEnable(GL_TEXTURE_2D);
    glColor3f(1.0f, 1.0f, 1.0f);
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f);
    glVertex2f(32.0f, 32.0f);
    glTexCoord2f(1.0f, 0.0f);
    glVertex2f(56.0f, 32.0f);
    glTexCoord2f(1.0f, 1.0f);
    glVertex2f(56.0f, 56.0f);
    glTexCoord2f(0.0f, 1.0f);
    glVertex2f(32.0f, 56.0f);
    glEnd();
    glDisable(GL_TEXTURE_2D);
    glFinish();
    CHECK(PixelIs(44, 44, 255, 255, 0));
    glDeleteTextures(1, &Texture);
    CHECK(glGetError() == GL_NO_ERROR);

    GenBuffers = (PFNGLGENBUFFERSPROC)wglGetProcAddress("glGenBuffers");
    BindBuffer = (PFNGLBINDBUFFERPROC)wglGetProcAddress("glBindBuffer");
    BufferData = (PFNGLBUFFERDATAPROC)wglGetProcAddress("glBufferData");
    DeleteBuffers = (PFNGLDELETEBUFFERSPROC)wglGetProcAddress("glDeleteBuffers");
    if (GenBuffers && BindBuffer && BufferData && DeleteBuffers)
    {
        GenBuffers(1, &Buffer);
        BindBuffer(GL_ARRAY_BUFFER, Buffer);
        BufferData(GL_ARRAY_BUFFER, sizeof(Vertices), Vertices, GL_STATIC_DRAW);
        glEnableClientState(GL_VERTEX_ARRAY);
        glVertexPointer(2, GL_FLOAT, 0, NULL);
        glColor3f(1.0f, 0.0f, 1.0f);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glDisableClientState(GL_VERTEX_ARRAY);
        BindBuffer(GL_ARRAY_BUFFER, 0);
        DeleteBuffers(1, &Buffer);
        glFinish();
        CHECK(PixelIs(16, 44, 255, 0, 255));
        CHECK(glGetError() == GL_NO_ERROR);
    }
    else
    {
        printf("EMUGUI: the OpenGL implementation has no buffer objects\n");
    }

    DebugMessageCallback = (PGL_TEST_DEBUG_MESSAGE_CALLBACK)wglGetProcAddress("glDebugMessageCallback");
    if (DebugMessageCallback)
    {
        glEnable(GL_TEST_DEBUG_OUTPUT_SYNCHRONOUS);
        glEnable(GL_TEST_DEBUG_OUTPUT);
        DebugMessageCallback(DebugMessage, GL_TEST_DEBUG_USER);
        glEnable(0xDEAD);
        CHECK(glGetError() == GL_INVALID_ENUM);
        CHECK(DebugMessages > 0);
        CHECK(DebugType == GL_TEST_DEBUG_TYPE_ERROR);
        CHECK(DebugUser == GL_TEST_DEBUG_USER);
        DebugMessageCallback(NULL, NULL);
        glDisable(GL_TEST_DEBUG_OUTPUT);
        CHECK(glGetError() == GL_NO_ERROR);
    }
    else
    {
        printf("EMUGUI: the OpenGL implementation has no debug output\n");
    }
    printf("EMUGUI: GL stack doubles, texture upload, buffer objects and debug callbacks verified\n");
}

static
VOID
TestOpenGlSharedContext(
    _In_ HDC Dc,
    _In_ HGLRC Context)
{
    static const UCHAR Texels[16] = { 0, 255, 255, 255, 0, 255, 255, 255, 0, 255, 255, 255, 0, 255, 255, 255 };
    static const int Attributes[1] = { 0 };
    PGL_TEST_CREATE_CONTEXT_ATTRIBS CreateContextAttribs;
    PFNGLGENFRAMEBUFFERSPROC GenFramebuffers;
    PFNGLBINDFRAMEBUFFERPROC BindFramebuffer;
    PFNGLFRAMEBUFFERTEXTURE2DPROC FramebufferTexture2D;
    PFNGLCHECKFRAMEBUFFERSTATUSPROC CheckFramebufferStatus;
    PFNGLBLITFRAMEBUFFERPROC BlitFramebuffer;
    PFNGLDELETEFRAMEBUFFERSPROC DeleteFramebuffers;
    GLuint Texture, Framebuffer;
    HGLRC Shared;

    CreateContextAttribs = (PGL_TEST_CREATE_CONTEXT_ATTRIBS)wglGetProcAddress("wglCreateContextAttribsARB");
    if (!CreateContextAttribs)
    {
        printf("EMUGUI: the OpenGL implementation has no wglCreateContextAttribsARB\n");
        return;
    }

    glGenTextures(1, &Texture);
    glBindTexture(GL_TEXTURE_2D, Texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, Texels);
    glFinish();

    Shared = CreateContextAttribs(Dc, Context, Attributes);
    CHECK(Shared != NULL);
    if (!Shared)
    {
        glDeleteTextures(1, &Texture);
        return;
    }
    CHECK(wglMakeCurrent(Dc, Shared));
    CHECK(glIsTexture(Texture) == GL_TRUE);

    GenFramebuffers = (PFNGLGENFRAMEBUFFERSPROC)wglGetProcAddress("glGenFramebuffers");
    BindFramebuffer = (PFNGLBINDFRAMEBUFFERPROC)wglGetProcAddress("glBindFramebuffer");
    FramebufferTexture2D = (PFNGLFRAMEBUFFERTEXTURE2DPROC)wglGetProcAddress("glFramebufferTexture2D");
    CheckFramebufferStatus = (PFNGLCHECKFRAMEBUFFERSTATUSPROC)wglGetProcAddress("glCheckFramebufferStatus");
    BlitFramebuffer = (PFNGLBLITFRAMEBUFFERPROC)wglGetProcAddress("glBlitFramebuffer");
    DeleteFramebuffers = (PFNGLDELETEFRAMEBUFFERSPROC)wglGetProcAddress("glDeleteFramebuffers");
    if (GenFramebuffers && BindFramebuffer && FramebufferTexture2D && CheckFramebufferStatus && BlitFramebuffer &&
        DeleteFramebuffers)
    {
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        GenFramebuffers(1, &Framebuffer);
        BindFramebuffer(GL_READ_FRAMEBUFFER, Framebuffer);
        FramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, Texture, 0);
        CHECK(CheckFramebufferStatus(GL_READ_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
        BindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        BlitFramebuffer(0, 0, 2, 2, 8, 8, 24, 24, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        BindFramebuffer(GL_FRAMEBUFFER, 0);
        DeleteFramebuffers(1, &Framebuffer);
        glFinish();
        CHECK(PixelIs(16, 16, 0, 255, 255));
        CHECK(PixelIs(30, 30, 0, 0, 0));
    }
    else
    {
        printf("EMUGUI: the OpenGL implementation has no framebuffer blits\n");
    }
    CHECK(glGetError() == GL_NO_ERROR);

    CHECK(wglMakeCurrent(Dc, Context));
    CHECK(wglDeleteContext(Shared));
    glDeleteTextures(1, &Texture);
    CHECK(glGetError() == GL_NO_ERROR);
    printf("EMUGUI: shared context texture and framebuffer blit verified\n");
}

static
VOID
TestOpenGlCoreProfile(
    _In_ HDC Dc,
    _In_ HGLRC Context)
{
    static const int Attributes[] =
    {
        WGL_TEST_CONTEXT_MAJOR_VERSION, 3, WGL_TEST_CONTEXT_MINOR_VERSION, 2,
        WGL_TEST_CONTEXT_PROFILE_MASK, WGL_TEST_CONTEXT_CORE_PROFILE, 0
    };
    static const GLfloat Triangle[6] = { -1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f };
    static const GLchar *VertexSource =
        "#version 150\nin vec2 position;\nvoid main() { gl_Position = vec4(position, 0.5, 1.0); }\n";
    static const GLchar *FragmentSource =
        "#version 150\nuniform vec4 color;\nout vec4 result;\nvoid main() { result = color; }\n";
    PGL_TEST_CREATE_CONTEXT_ATTRIBS CreateContextAttribs;
    PFNGLCREATESHADERPROC CreateShader;
    PFNGLSHADERSOURCEPROC ShaderSource;
    PFNGLCOMPILESHADERPROC CompileShader;
    PFNGLGETSHADERIVPROC GetShaderiv;
    PFNGLCREATEPROGRAMPROC CreateProgram;
    PFNGLATTACHSHADERPROC AttachShader;
    PFNGLBINDATTRIBLOCATIONPROC BindAttribLocation;
    PFNGLLINKPROGRAMPROC LinkProgram;
    PFNGLGETPROGRAMIVPROC GetProgramiv;
    PFNGLUSEPROGRAMPROC UseProgram;
    PFNGLGETUNIFORMLOCATIONPROC GetUniformLocation;
    PFNGLUNIFORM4FPROC Uniform4f;
    PFNGLGENVERTEXARRAYSPROC GenVertexArrays;
    PFNGLBINDVERTEXARRAYPROC BindVertexArray;
    PFNGLGENBUFFERSPROC GenBuffers;
    PFNGLBINDBUFFERPROC BindBuffer;
    PFNGLBUFFERDATAPROC BufferData;
    PFNGLVERTEXATTRIBPOINTERPROC VertexAttribPointer;
    PFNGLENABLEVERTEXATTRIBARRAYPROC EnableVertexAttribArray;
    PFNGLDELETEBUFFERSPROC DeleteBuffers;
    PFNGLDELETEVERTEXARRAYSPROC DeleteVertexArrays;
    PFNGLDELETEPROGRAMPROC DeleteProgram;
    PFNGLDELETESHADERPROC DeleteShader;
    PFNGLGETVERTEXATTRIBIVPROC GetVertexAttribiv;
    PFNGLGETVERTEXATTRIBPOINTERVPROC GetVertexAttribPointerv;
    PFNGLGETBUFFERSUBDATAPROC GetBufferSubData;
    PFNGLGETUNIFORMFVPROC GetUniformfv;
    GLfloat Readback[6];
    GLint Value;
    PVOID Pointer;
    PGL_TEST_BUFFER_STORAGE BufferStorage;
    PFNGLMAPBUFFERRANGEPROC MapBufferRange;
    PFNGLFLUSHMAPPEDBUFFERRANGEPROC FlushMappedBufferRange;
    GLuint VertexShader, FragmentShader, Program, VertexArray, Buffer, Persistent;
    GLint Status, Color;
    GLfloat *Mapped;
    HGLRC Core;

    CreateContextAttribs = (PGL_TEST_CREATE_CONTEXT_ATTRIBS)wglGetProcAddress("wglCreateContextAttribsARB");
    if (!CreateContextAttribs)
    {
        printf("EMUGUI: the OpenGL implementation has no wglCreateContextAttribsARB\n");
        return;
    }
    Core = CreateContextAttribs(Dc, NULL, Attributes);
    if (!Core)
    {
        int Major = 0, Minor = 0;
        const char *Version = (const char *)glGetString(GL_VERSION);

        if (Version && sscanf(Version, "%d.%d", &Major, &Minor) == 2 && (Major < 3 || (Major == 3 && Minor < 2)))
        {
            printf("EMUGUI: the OpenGL implementation has no 3.2 core profile (version %s)\n", Version);
            return;
        }
    }
    CHECK(Core != NULL);
    if (!Core) return;
    CHECK(wglMakeCurrent(Dc, Core));

    CreateShader = (PFNGLCREATESHADERPROC)wglGetProcAddress("glCreateShader");
    ShaderSource = (PFNGLSHADERSOURCEPROC)wglGetProcAddress("glShaderSource");
    CompileShader = (PFNGLCOMPILESHADERPROC)wglGetProcAddress("glCompileShader");
    GetShaderiv = (PFNGLGETSHADERIVPROC)wglGetProcAddress("glGetShaderiv");
    CreateProgram = (PFNGLCREATEPROGRAMPROC)wglGetProcAddress("glCreateProgram");
    AttachShader = (PFNGLATTACHSHADERPROC)wglGetProcAddress("glAttachShader");
    BindAttribLocation = (PFNGLBINDATTRIBLOCATIONPROC)wglGetProcAddress("glBindAttribLocation");
    LinkProgram = (PFNGLLINKPROGRAMPROC)wglGetProcAddress("glLinkProgram");
    GetProgramiv = (PFNGLGETPROGRAMIVPROC)wglGetProcAddress("glGetProgramiv");
    UseProgram = (PFNGLUSEPROGRAMPROC)wglGetProcAddress("glUseProgram");
    GetUniformLocation = (PFNGLGETUNIFORMLOCATIONPROC)wglGetProcAddress("glGetUniformLocation");
    Uniform4f = (PFNGLUNIFORM4FPROC)wglGetProcAddress("glUniform4f");
    GenVertexArrays = (PFNGLGENVERTEXARRAYSPROC)wglGetProcAddress("glGenVertexArrays");
    BindVertexArray = (PFNGLBINDVERTEXARRAYPROC)wglGetProcAddress("glBindVertexArray");
    GenBuffers = (PFNGLGENBUFFERSPROC)wglGetProcAddress("glGenBuffers");
    BindBuffer = (PFNGLBINDBUFFERPROC)wglGetProcAddress("glBindBuffer");
    BufferData = (PFNGLBUFFERDATAPROC)wglGetProcAddress("glBufferData");
    VertexAttribPointer = (PFNGLVERTEXATTRIBPOINTERPROC)wglGetProcAddress("glVertexAttribPointer");
    EnableVertexAttribArray = (PFNGLENABLEVERTEXATTRIBARRAYPROC)wglGetProcAddress("glEnableVertexAttribArray");
    DeleteBuffers = (PFNGLDELETEBUFFERSPROC)wglGetProcAddress("glDeleteBuffers");
    DeleteVertexArrays = (PFNGLDELETEVERTEXARRAYSPROC)wglGetProcAddress("glDeleteVertexArrays");
    DeleteProgram = (PFNGLDELETEPROGRAMPROC)wglGetProcAddress("glDeleteProgram");
    DeleteShader = (PFNGLDELETESHADERPROC)wglGetProcAddress("glDeleteShader");
    CHECK(CreateShader && ShaderSource && CompileShader && GetShaderiv && CreateProgram && AttachShader &&
          BindAttribLocation && LinkProgram && GetProgramiv && UseProgram && GetUniformLocation && Uniform4f &&
          GenVertexArrays && BindVertexArray && GenBuffers && BindBuffer && BufferData && VertexAttribPointer &&
          EnableVertexAttribArray && DeleteBuffers && DeleteVertexArrays && DeleteProgram && DeleteShader);
    if (CreateShader && ShaderSource && CompileShader && GetShaderiv && CreateProgram && AttachShader &&
        BindAttribLocation && LinkProgram && GetProgramiv && UseProgram && GetUniformLocation && Uniform4f &&
        GenVertexArrays && BindVertexArray && GenBuffers && BindBuffer && BufferData && VertexAttribPointer &&
        EnableVertexAttribArray && DeleteBuffers && DeleteVertexArrays && DeleteProgram && DeleteShader)
    {
        VertexShader = CreateShader(GL_VERTEX_SHADER);
        ShaderSource(VertexShader, 1, &VertexSource, NULL);
        CompileShader(VertexShader);
        GetShaderiv(VertexShader, GL_COMPILE_STATUS, &Status);
        CHECK(Status == GL_TRUE);
        FragmentShader = CreateShader(GL_FRAGMENT_SHADER);
        ShaderSource(FragmentShader, 1, &FragmentSource, NULL);
        CompileShader(FragmentShader);
        GetShaderiv(FragmentShader, GL_COMPILE_STATUS, &Status);
        CHECK(Status == GL_TRUE);
        Program = CreateProgram();
        AttachShader(Program, VertexShader);
        AttachShader(Program, FragmentShader);
        BindAttribLocation(Program, 0, "position");
        LinkProgram(Program);
        GetProgramiv(Program, GL_LINK_STATUS, &Status);
        CHECK(Status == GL_TRUE);

        GenVertexArrays(1, &VertexArray);
        BindVertexArray(VertexArray);
        GenBuffers(1, &Buffer);
        BindBuffer(GL_ARRAY_BUFFER, Buffer);
        BufferData(GL_ARRAY_BUFFER, sizeof(Triangle), Triangle, GL_STATIC_DRAW);
        VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(GLfloat), NULL);
        EnableVertexAttribArray(0);

        glViewport(0, 0, 64, 64);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        GetVertexAttribiv = (PFNGLGETVERTEXATTRIBIVPROC)wglGetProcAddress("glGetVertexAttribiv");
        GetVertexAttribPointerv = (PFNGLGETVERTEXATTRIBPOINTERVPROC)wglGetProcAddress("glGetVertexAttribPointerv");
        GetBufferSubData = (PFNGLGETBUFFERSUBDATAPROC)wglGetProcAddress("glGetBufferSubData");
        GetUniformfv = (PFNGLGETUNIFORMFVPROC)wglGetProcAddress("glGetUniformfv");
        CHECK(GetVertexAttribiv && GetVertexAttribPointerv && GetBufferSubData && GetUniformfv);
        if (GetVertexAttribiv && GetVertexAttribPointerv && GetBufferSubData)
        {
            Value = -1;
            GetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &Value);
            CHECK(Value == GL_TRUE);
            Value = -1;
            GetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_SIZE, &Value);
            CHECK(Value == 2);
            Value = -1;
            GetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_STRIDE, &Value);
            CHECK(Value == 2 * sizeof(GLfloat));
            Value = -1;
            GetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_TYPE, &Value);
            CHECK(Value == GL_FLOAT);
            Value = -1;
            GetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_NORMALIZED, &Value);
            CHECK(Value == GL_FALSE);
            Value = -1;
            GetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING, &Value);
            CHECK(Value == (GLint)Buffer);
            Pointer = (PVOID)(ULONG_PTR)1;
            GetVertexAttribPointerv(0, GL_VERTEX_ATTRIB_ARRAY_POINTER, &Pointer);
            CHECK(Pointer == NULL);
            RtlZeroMemory(Readback, sizeof(Readback));
            GetBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(Readback), Readback);
            CHECK(!memcmp(Readback, Triangle, sizeof(Triangle)));
        }

        UseProgram(Program);
        {
            GLint State[4] = { -1, -1, -1, -1 };
            GLboolean Mask[4] = { 0 };

            printf("EMUGUI: core context version '%s'\n", (const char *)glGetString(GL_VERSION));
            glGetIntegerv(GL_CURRENT_PROGRAM, State);
            CHECK(State[0] == (GLint)Program);
            glGetIntegerv(GL_VIEWPORT, State);
            CHECK(State[0] == 0 && State[1] == 0 && State[2] == 64 && State[3] == 64);
            State[0] = -1;
            glGetIntegerv(GL_DRAW_BUFFER, State);
            CHECK(State[0] == GL_BACK);
            glGetBooleanv(GL_COLOR_WRITEMASK, Mask);
            CHECK(Mask[0] && Mask[1] && Mask[2] && Mask[3]);
            CHECK(!glIsEnabled(GL_RASTERIZER_DISCARD));
            State[0] = -1;
            glGetIntegerv(GL_VERTEX_ARRAY_BINDING, State);
            CHECK(State[0] == (GLint)VertexArray);
        }
        Color = GetUniformLocation(Program, "color");
        CHECK(Color >= 0);
        Uniform4f(Color, 1.0f, 0.5f, 0.0f, 1.0f);
        if (GetUniformfv)
        {
            RtlZeroMemory(Readback, sizeof(Readback));
            GetUniformfv(Program, Color, Readback);
            CHECK(Readback[0] == 1.0f && Readback[1] == 0.5f && Readback[2] == 0.0f && Readback[3] == 1.0f);
            if (Readback[0] != 1.0f || Readback[1] != 0.5f || Readback[3] != 1.0f)
                printf("EMUGUI: uniform color reads back %f %f %f %f\n", Readback[0], Readback[1], Readback[2], Readback[3]);
        }
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glFinish();
        CHECK(PixelIs(8, 8, 255, 128, 0) || PixelIs(8, 8, 255, 127, 0));
        CHECK(PixelIs(60, 60, 0, 0, 0));
        CHECK(glGetError() == GL_NO_ERROR);

        BufferStorage = (PGL_TEST_BUFFER_STORAGE)wglGetProcAddress("glBufferStorage");
        MapBufferRange = (PFNGLMAPBUFFERRANGEPROC)wglGetProcAddress("glMapBufferRange");
        FlushMappedBufferRange = (PFNGLFLUSHMAPPEDBUFFERRANGEPROC)wglGetProcAddress("glFlushMappedBufferRange");
        if (BufferStorage && MapBufferRange && FlushMappedBufferRange)
        {
            static const GLfloat Upper[6] = { 1.0f, 1.0f, -1.0f, 1.0f, 1.0f, -1.0f };

            GenBuffers(1, &Persistent);
            BindBuffer(GL_ARRAY_BUFFER, Persistent);
            BufferStorage(GL_ARRAY_BUFFER, 4096, NULL,
                          GL_MAP_WRITE_BIT | GL_TEST_MAP_PERSISTENT_BIT | GL_TEST_MAP_COHERENT_BIT |
                          GL_TEST_DYNAMIC_STORAGE_BIT);
            Mapped = (GLfloat *)MapBufferRange(GL_ARRAY_BUFFER, 0, 4096,
                                               GL_MAP_WRITE_BIT | GL_TEST_MAP_PERSISTENT_BIT |
                                               GL_TEST_MAP_COHERENT_BIT | GL_MAP_UNSYNCHRONIZED_BIT |
                                               GL_MAP_FLUSH_EXPLICIT_BIT);
            CHECK(Mapped != NULL);
            if (Mapped)
            {
                memcpy(Mapped + 16, Upper, sizeof(Upper));
                FlushMappedBufferRange(GL_ARRAY_BUFFER, 16 * sizeof(GLfloat), sizeof(Upper));
                VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(GLfloat), (const void *)(16 * sizeof(GLfloat)));
                Uniform4f(Color, 0.0f, 0.0f, 1.0f, 1.0f);
                glDrawArrays(GL_TRIANGLES, 0, 3);
                glFinish();
                CHECK(PixelIs(56, 56, 0, 0, 255));
                CHECK(PixelIs(8, 8, 255, 128, 0) || PixelIs(8, 8, 255, 127, 0));
            }
            CHECK(glGetError() == GL_NO_ERROR);
            BindBuffer(GL_ARRAY_BUFFER, 0);
            DeleteBuffers(1, &Persistent);
        }
        else
        {
            printf("EMUGUI: the OpenGL implementation has no persistent buffer mappings\n");
        }

        UseProgram(0);
        BindVertexArray(0);
        DeleteVertexArrays(1, &VertexArray);
        BindBuffer(GL_ARRAY_BUFFER, 0);
        DeleteBuffers(1, &Buffer);
        DeleteProgram(Program);
        DeleteShader(VertexShader);
        DeleteShader(FragmentShader);
    }

    CHECK(wglMakeCurrent(Dc, Context));
    CHECK(wglDeleteContext(Core));
    printf("EMUGUI: core profile shaders, vertex arrays, uniforms and persistent buffers verified\n");
}

static
VOID
TestOpenGl(VOID)
{
    PIXELFORMATDESCRIPTOR Format = { sizeof(Format), 1 };
    WNDCLASSW Class = { 0 };
    UCHAR Pixel[4];
    HGLRC Context;
    HWND Window;
    HDC Dc;
    int Index;

    Class.lpfnWndProc = DefWindowProcW;
    Class.hInstance = GetModuleHandleW(NULL);
    Class.lpszClassName = L"EmulationGlTest";
    Class.style = CS_OWNDC;
    CHECK(RegisterClassW(&Class) != 0);
    Window = CreateWindowExW(0, Class.lpszClassName, L"Emulation GL test", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                             380, 40, 128, 128, NULL, NULL, Class.hInstance, NULL);
    CHECK(Window != NULL);
    if (!Window) return;
    Dc = GetDC(Window);
    CHECK(Dc != NULL);

    Format.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    Format.iPixelType = PFD_TYPE_RGBA;
    Format.cColorBits = 32;
    Format.cDepthBits = 24;
    Format.iLayerType = PFD_MAIN_PLANE;
    Index = ChoosePixelFormat(Dc, &Format);
    CHECK(Index > 0);
    CHECK(SetPixelFormat(Dc, Index, &Format));
    Context = wglCreateContext(Dc);
    CHECK(Context != NULL);
    if (!Context)
    {
        ReleaseDC(Window, Dc);
        DestroyWindow(Window);
        return;
    }
    CHECK(wglMakeCurrent(Dc, Context));

    CHECK(glGetString(GL_VENDOR) != NULL && glGetString(GL_RENDERER) != NULL && glGetString(GL_VERSION) != NULL);
    printf("EMUGUI: GL vendor '%s' renderer '%s' version '%s'\n",
           (const char *)glGetString(GL_VENDOR),
           (const char *)glGetString(GL_RENDERER),
           (const char *)glGetString(GL_VERSION));

    glViewport(0, 0, 64, 64);
    glClearColor(0.0f, 1.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glFinish();
    glReadPixels(2, 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, Pixel);
    CHECK(Pixel[0] == 0 && Pixel[1] == 255 && Pixel[2] == 0);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glColor3f(1.0f, 0.0f, 0.0f);
    glBegin(GL_TRIANGLES);
    glVertex2f(-0.9f, -0.9f);
    glVertex2f(0.9f, -0.9f);
    glVertex2f(0.0f, 0.9f);
    glEnd();
    glFinish();
    glReadPixels(32, 28, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, Pixel);
    CHECK(Pixel[0] == 255 && Pixel[1] == 0 && Pixel[2] == 0);
    glReadPixels(2, 62, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, Pixel);
    CHECK(Pixel[0] == 0 && Pixel[1] == 255 && Pixel[2] == 0);
    CHECK(glGetError() == GL_NO_ERROR);
    TestOpenGlCalls();
    TestOpenGlSharedContext(Dc, Context);
    TestOpenGlCoreProfile(Dc, Context);
    CHECK(SwapBuffers(Dc));

    CHECK(wglMakeCurrent(NULL, NULL));
    CHECK(wglDeleteContext(Context));
    ReleaseDC(Window, Dc);
    CHECK(DestroyWindow(Window));
    CHECK(UnregisterClassW(Class.lpszClassName, Class.hInstance));
    printf("EMUGUI: GL clear, triangle and swap verified\n");
}

static
BOOL
SurfacePixelIs(
    _In_ IDirect3DDevice9 *Device,
    _In_ IDirect3DSurface9 *Target,
    _In_ IDirect3DSurface9 *Copy,
    _In_ UINT X,
    _In_ UINT Y,
    _In_ D3DCOLOR Expected)
{
    D3DLOCKED_RECT Locked;
    D3DCOLOR Pixel;

    if (FAILED(IDirect3DDevice9_GetRenderTargetData(Device, Target, Copy))) return FALSE;
    if (FAILED(IDirect3DSurface9_LockRect(Copy, &Locked, NULL, D3DLOCK_READONLY))) return FALSE;
    Pixel = ((const D3DCOLOR *)((const BYTE *)Locked.pBits + Y * Locked.Pitch))[X];
    IDirect3DSurface9_UnlockRect(Copy);
    if ((Pixel & 0x00FFFFFF) == (Expected & 0x00FFFFFF)) return TRUE;
    printf("EMUGUI: pixel %u,%u is %08lx, expected %08lx\n", X, Y, (ULONG)Pixel, (ULONG)Expected);
    return FALSE;
}

static
VOID
TestDirect3D9(VOID)
{
    static const D3D_TEST_VERTEX Quad[4] =
    {
        { 8.0f, 8.0f, 0.5f, 1.0f, 0.0f, 0.0f },
        { 24.0f, 8.0f, 0.5f, 1.0f, 1.0f, 0.0f },
        { 8.0f, 24.0f, 0.5f, 1.0f, 0.0f, 1.0f },
        { 24.0f, 24.0f, 0.5f, 1.0f, 1.0f, 1.0f },
    };
    static const D3D_TEST_COLOR_VERTEX ColorQuad[4] =
    {
        { 40.0f, 8.0f, 0.5f, 1.0f, D3DCOLOR_XRGB(0, 255, 0) },
        { 56.0f, 8.0f, 0.5f, 1.0f, D3DCOLOR_XRGB(0, 255, 0) },
        { 40.0f, 24.0f, 0.5f, 1.0f, D3DCOLOR_XRGB(0, 255, 0) },
        { 56.0f, 24.0f, 0.5f, 1.0f, D3DCOLOR_XRGB(0, 255, 0) },
    };
    D3DPRESENT_PARAMETERS Parameters = { 0 };
    IDirect3DSurface9 *Target = NULL, *Copy = NULL;
    IDirect3DTexture9 *Texture = NULL;
    IDirect3DDevice9 *Device = NULL;
    PD3D_TEST_CREATE9 Create9;
    D3DLOCKED_RECT Locked;
    IDirect3D9 *Direct3D;
    UINT Row, Column;
    HMODULE Module;
    HWND Window;

    Module = LoadLibraryW(L"d3d9.dll");
    CHECK(Module != NULL);
    if (!Module) return;
    Create9 = (PD3D_TEST_CREATE9)GetProcAddress(Module, "Direct3DCreate9");
    CHECK(Create9 != NULL);
    Direct3D = Create9 ? Create9(D3D_SDK_VERSION) : NULL;
    CHECK(Direct3D != NULL);
    if (!Direct3D)
    {
        FreeLibrary(Module);
        return;
    }

    Window = CreateWindowExW(0, L"STATIC", L"Emulation D3D9 test", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                             520, 40, 128, 128, NULL, NULL, GetModuleHandleW(NULL), NULL);
    CHECK(Window != NULL);
    Parameters.Windowed = TRUE;
    Parameters.SwapEffect = D3DSWAPEFFECT_DISCARD;
    Parameters.BackBufferFormat = D3DFMT_X8R8G8B8;
    Parameters.BackBufferWidth = 64;
    Parameters.BackBufferHeight = 64;
    Parameters.BackBufferCount = 1;
    Parameters.hDeviceWindow = Window;
    CHECK(SUCCEEDED(IDirect3D9_CreateDevice(Direct3D, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, Window,
                                            D3DCREATE_SOFTWARE_VERTEXPROCESSING, &Parameters, &Device)));
    if (Device)
    {
        CHECK(SUCCEEDED(IDirect3DDevice9_GetRenderTarget(Device, 0, &Target)));
        CHECK(SUCCEEDED(IDirect3DDevice9_CreateOffscreenPlainSurface(Device, 64, 64, D3DFMT_X8R8G8B8,
                                                                    D3DPOOL_SYSTEMMEM, &Copy, NULL)));
        CHECK(SUCCEEDED(IDirect3DDevice9_Clear(Device, 0, NULL, D3DCLEAR_TARGET, D3DCOLOR_XRGB(255, 0, 0), 1.0f, 0)));
        CHECK(Target && Copy && SurfacePixelIs(Device, Target, Copy, 32, 32, D3DCOLOR_XRGB(255, 0, 0)));

        CHECK(SUCCEEDED(IDirect3DDevice9_CreateTexture(Device, 4, 4, 1, D3DUSAGE_DYNAMIC, D3DFMT_X8R8G8B8,
                                                       D3DPOOL_DEFAULT, &Texture, NULL)));
        if (Texture && SUCCEEDED(IDirect3DTexture9_LockRect(Texture, 0, &Locked, NULL, D3DLOCK_DISCARD)))
        {
            for (Row = 0; Row < 4; Row++)
            {
                for (Column = 0; Column < 4; Column++)
                    ((D3DCOLOR *)((BYTE *)Locked.pBits + Row * Locked.Pitch))[Column] = D3DCOLOR_XRGB(255, 255, 0);
            }
            CHECK(SUCCEEDED(IDirect3DTexture9_UnlockRect(Texture, 0)));
        }
        CHECK(SUCCEEDED(IDirect3DDevice9_BeginScene(Device)));
        CHECK(SUCCEEDED(IDirect3DDevice9_SetRenderState(Device, D3DRS_LIGHTING, FALSE)));
        CHECK(SUCCEEDED(IDirect3DDevice9_SetRenderState(Device, D3DRS_CULLMODE, D3DCULL_NONE)));
        CHECK(SUCCEEDED(IDirect3DDevice9_SetFVF(Device, D3DFVF_XYZRHW | D3DFVF_DIFFUSE)));
        CHECK(SUCCEEDED(IDirect3DDevice9_DrawPrimitiveUP(Device, D3DPT_TRIANGLESTRIP, 2, ColorQuad, sizeof(ColorQuad[0]))));
        CHECK(SUCCEEDED(IDirect3DDevice9_SetTexture(Device, 0, (IDirect3DBaseTexture9 *)Texture)));
        CHECK(SUCCEEDED(IDirect3DDevice9_SetFVF(Device, D3DFVF_XYZRHW | D3DFVF_TEX1)));
        CHECK(SUCCEEDED(IDirect3DDevice9_DrawPrimitiveUP(Device, D3DPT_TRIANGLESTRIP, 2, Quad, sizeof(Quad[0]))));
        CHECK(SUCCEEDED(IDirect3DDevice9_EndScene(Device)));
        CHECK(Target && Copy && SurfacePixelIs(Device, Target, Copy, 48, 16, D3DCOLOR_XRGB(0, 255, 0)));
        CHECK(Target && Copy && SurfacePixelIs(Device, Target, Copy, 16, 16, D3DCOLOR_XRGB(255, 255, 0)));
        CHECK(Target && Copy && SurfacePixelIs(Device, Target, Copy, 40, 40, D3DCOLOR_XRGB(255, 0, 0)));
        CHECK(SUCCEEDED(IDirect3DDevice9_Present(Device, NULL, NULL, NULL, NULL)));

        if (Texture) IDirect3DTexture9_Release(Texture);
        if (Copy) IDirect3DSurface9_Release(Copy);
        if (Target) IDirect3DSurface9_Release(Target);
        IDirect3DDevice9_Release(Device);
    }
    IDirect3D9_Release(Direct3D);
    if (Window) DestroyWindow(Window);
    FreeLibrary(Module);
    printf("EMUGUI: Direct3D 9 clear, dynamic texture and textured quad verified\n");
}

int
main(VOID)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    TestWindow();
    TestGdi();
    TestOpenGl();
    TestDirect3D9();

    printf("EMUGUI: END checks=%ld failures=%ld\n", Checks, Failures);
    return Failures ? 1 : 0;
}
