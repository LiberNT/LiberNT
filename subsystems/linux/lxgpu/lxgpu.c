/*
 * PROJECT:     LiberNT Linux Subsystem
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Windows renderer of the Linux virtio-gpu device: virglrenderer on WGL
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <windows.h>
#include <winioctl.h>
#include <shellapi.h>
#include <epoxy/gl.h>
#include <epoxy/wgl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <virglrenderer.h>
#include <virgl_hw.h>
#include "lxgpu.h"

struct iovec
{
    void *iov_base;
    size_t iov_len;
};

typedef struct _LX_RESOURCE
{
    ULONG Id;
    PVOID View;
    SIZE_T Size;
    struct iovec Iov;
    struct _LX_RESOURCE *Next;
} LX_RESOURCE;

typedef struct _LX_ATTACHMENT
{
    ULONG CtxId;
    ULONG ResId;
    struct _LX_ATTACHMENT *Next;
} LX_ATTACHMENT;

BOOL WINAPI epoxy_dll_main(HINSTANCE Instance, DWORD Reason, LPVOID Reserved);

static HANDLE LxDevice;
static HDC LxDc;
static HGLRC LxRootContext;
static PFNWGLCREATECONTEXTATTRIBSARBPROC LxCreateContextAttribs;
static LX_RESOURCE *LxResources;
static LX_ATTACHMENT *LxAttachments;
static LONG LxPendingFences;
static FILE *LxLog;
static BOOL LxVerbose;

static
void
LxTrace(const char *Format, ...)
{
    va_list Args;

    if (LxLog == NULL)
        return;
    va_start(Args, Format);
    vfprintf(LxLog, Format, Args);
    va_end(Args);
    fflush(LxLog);
}

static
BOOL
LxSend(const void *Buffer, DWORD Length)
{
    OVERLAPPED Overlapped = { 0 };
    DWORD Bytes = 0;
    BOOL Ok;

    Overlapped.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (Overlapped.hEvent == NULL)
        return FALSE;
    Ok = DeviceIoControl(LxDevice, IOCTL_LX_GPU_SEND, (PVOID)Buffer, Length, NULL, 0, &Bytes, &Overlapped);
    if (!Ok && (GetLastError() == ERROR_IO_PENDING))
        Ok = GetOverlappedResult(LxDevice, &Overlapped, &Bytes, TRUE);
    CloseHandle(Overlapped.hEvent);
    return Ok;
}

static
void
LxReply(ULONG Serial, LONG Status, const void *Data, ULONG Length)
{
    PLX_GPU_HEADER Header = malloc(sizeof(*Header) + Length);

    if (Header == NULL)
        return;
    Header->Type = LX_GPU_REPLY;
    Header->Size = sizeof(*Header) + Length;
    Header->Serial = Serial;
    Header->Status = Status;
    if (Length != 0)
        memcpy(Header + 1, Data, Length);
    LxSend(Header, Header->Size);
    free(Header);
}

static
void
LxWriteFence(void *Cookie, uint32_t Fence)
{
    LX_GPU_DONE_MSG Done = { { LX_GPU_DONE, sizeof(Done), 0, 0 }, Fence, 0 };

    (void)Cookie;
    if (LxPendingFences > 0)
        LxPendingFences--;
    if (LxVerbose)
        LxTrace("fence %u done\n", Fence);
    LxSend(&Done, sizeof(Done));
}

static
virgl_renderer_gl_context
LxCreateContext(void *Cookie, int Scanout, struct virgl_renderer_gl_ctx_param *Param)
{
    int Attribs[] = { WGL_CONTEXT_MAJOR_VERSION_ARB, Param->major_ver, WGL_CONTEXT_MINOR_VERSION_ARB, Param->minor_ver,
                      WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB, 0 };

    (void)Cookie;
    (void)Scanout;
    return (virgl_renderer_gl_context)LxCreateContextAttribs(LxDc, LxRootContext, Attribs);
}

static
void
LxDestroyContext(void *Cookie, virgl_renderer_gl_context Context)
{
    (void)Cookie;
    wglDeleteContext((HGLRC)Context);
}

static
int
LxMakeCurrent(void *Cookie, int Scanout, virgl_renderer_gl_context Context)
{
    (void)Cookie;
    (void)Scanout;
    return wglMakeCurrent(LxDc, (HGLRC)Context) ? 0 : -1;
}

static
void
LxVirglLog(enum virgl_log_level_flags Level, const char *Message, void *Data)
{
    (void)Data;
    LxTrace("virgl[%d]: %s", (int)Level, Message);
}

static
BOOL
LxInitializeRenderer(void)
{
    static struct virgl_renderer_callbacks Callbacks =
    {
        1, LxWriteFence, LxCreateContext, LxDestroyContext, LxMakeCurrent
    };
    PIXELFORMATDESCRIPTOR Pfd = { sizeof(Pfd), 1, PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER,
                                  PFD_TYPE_RGBA, 32, 0, 0, 0, 0, 0, 0, 8, 0, 0, 0, 0, 0, 0, 24, 8 };
    int Attribs[] = { WGL_CONTEXT_MAJOR_VERSION_ARB, 4, WGL_CONTEXT_MINOR_VERSION_ARB, 3, WGL_CONTEXT_PROFILE_MASK_ARB,
                      WGL_CONTEXT_CORE_PROFILE_BIT_ARB, 0 };
    WNDCLASSW Class = { 0 };
    HGLRC Legacy;
    HWND Window;

    Class.lpfnWndProc = DefWindowProcW;
    Class.hInstance = GetModuleHandleW(NULL);
    Class.lpszClassName = L"lxgpu";
    RegisterClassW(&Class);
    Window = CreateWindowW(L"lxgpu", L"lxgpu", WS_OVERLAPPEDWINDOW, 0, 0, 16, 16, NULL, NULL, Class.hInstance, NULL);
    if (Window == NULL)
        return FALSE;
    LxDc = GetDC(Window);
    if (!SetPixelFormat(LxDc, ChoosePixelFormat(LxDc, &Pfd), &Pfd))
        return FALSE;
    Legacy = wglCreateContext(LxDc);
    if ((Legacy == NULL) || !wglMakeCurrent(LxDc, Legacy))
        return FALSE;
    LxCreateContextAttribs = (PFNWGLCREATECONTEXTATTRIBSARBPROC)wglGetProcAddress("wglCreateContextAttribsARB");
    if (LxCreateContextAttribs == NULL)
        return FALSE;
    LxRootContext = LxCreateContextAttribs(LxDc, NULL, Attribs);
    if (LxRootContext == NULL)
        return FALSE;
    wglMakeCurrent(LxDc, LxRootContext);
    LxTrace("GL %s / %s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION));
    virgl_set_log_callback(LxVirglLog, NULL, NULL);
    return virgl_renderer_init(&LxDc, 0, &Callbacks) == 0;
}

static
void
LxAttach(ULONG CtxId, ULONG ResId)
{
    LX_ATTACHMENT *Entry;

    for (Entry = LxAttachments; Entry != NULL; Entry = Entry->Next)
    {
        if ((Entry->CtxId == CtxId) && (Entry->ResId == ResId))
            return;
    }
    Entry = malloc(sizeof(*Entry));
    if (Entry == NULL)
        return;
    Entry->CtxId = CtxId;
    Entry->ResId = ResId;
    Entry->Next = LxAttachments;
    LxAttachments = Entry;
    virgl_renderer_ctx_attach_resource((int)CtxId, (int)ResId);
}

static
void
LxForget(ULONG CtxId, ULONG ResId)
{
    LX_ATTACHMENT **Link = &LxAttachments;

    while (*Link != NULL)
    {
        LX_ATTACHMENT *Entry = *Link;

        if (((CtxId != 0) && (Entry->CtxId == CtxId)) || ((ResId != 0) && (Entry->ResId == ResId)))
        {
            *Link = Entry->Next;
            free(Entry);
        }
        else
        {
            Link = &Entry->Next;
        }
    }
}

static
void
LxCreateResource(PLX_GPU_RES_MSG Message)
{
    struct virgl_renderer_resource_create_args Args;
    LX_RESOURCE *Resource = calloc(1, sizeof(*Resource));
    HANDLE Section = (HANDLE)(ULONG_PTR)Message->Section;
    int Result = -12;

    if ((Resource != NULL) && (Section != NULL))
        Resource->View = MapViewOfFile(Section, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, (SIZE_T)Message->Size);
    if (Section != NULL)
        CloseHandle(Section);
    if ((Resource != NULL) && (Resource->View != NULL))
    {
        Args.handle = Message->ResId;
        Args.target = Message->Target;
        Args.format = Message->Format;
        Args.bind = Message->Bind;
        Args.width = Message->Width;
        Args.height = Message->Height;
        Args.depth = Message->Depth;
        Args.array_size = Message->ArraySize;
        Args.last_level = Message->LastLevel;
        Args.nr_samples = Message->NrSamples;
        Args.flags = Message->Flags;
        Resource->Id = Message->ResId;
        Resource->Size = (SIZE_T)Message->Size;
        Resource->Iov.iov_base = Resource->View;
        Resource->Iov.iov_len = Resource->Size;
        Result = virgl_renderer_resource_create(&Args, NULL, 0);
        if (Result == 0)
        {
            Result = virgl_renderer_resource_attach_iov((int)Message->ResId, (struct iovec *)&Resource->Iov, 1);
            if (Result != 0)
                virgl_renderer_resource_unref(Message->ResId);
        }
    }
    if (Result == 0)
    {
        Resource->Next = LxResources;
        LxResources = Resource;
    }
    else if (Resource != NULL)
    {
        if (Resource->View != NULL)
            UnmapViewOfFile(Resource->View);
        free(Resource);
    }
    if (LxVerbose || (Result != 0))
    {
        LxTrace("resource %u target %u format %u %ux%u size %llu -> %d\n", Message->ResId, Message->Target,
                Message->Format, Message->Width, Message->Height, (unsigned long long)Message->Size, Result);
    }
    LxReply(Message->Header.Serial, (Result == 0) ? 0 : -22, NULL, 0);
}

static
void
LxDestroyResource(ULONG ResId)
{
    LX_RESOURCE **Link = &LxResources;

    LxForget(0, ResId);
    virgl_renderer_resource_unref(ResId);
    while (*Link != NULL)
    {
        if ((*Link)->Id == ResId)
        {
            LX_RESOURCE *Resource = *Link;

            *Link = Resource->Next;
            UnmapViewOfFile(Resource->View);
            free(Resource);
            return;
        }
        Link = &(*Link)->Next;
    }
}

static
void
LxDispatch(PLX_GPU_HEADER Header)
{
    switch (Header->Type)
    {
        case LX_GPU_GET_CAPS:
        {
            PLX_GPU_CAPS_MSG Message = (PLX_GPU_CAPS_MSG)Header;
            uint32_t MaxVersion = 0, MaxSize = 0;
            void *Caps;

            virgl_renderer_get_cap_set(Message->Capset, &MaxVersion, &MaxSize);
            Caps = (MaxSize != 0) ? calloc(1, MaxSize) : NULL;
            if (Caps == NULL)
            {
                LxReply(Header->Serial, -22, NULL, 0);
                break;
            }
            virgl_renderer_fill_caps(Message->Capset, MaxVersion, Caps);
            LxReply(Header->Serial, 0, Caps, MaxSize);
            free(Caps);
            break;
        }

        case LX_GPU_CTX_CREATE:
        {
            PLX_GPU_CTX_MSG Message = (PLX_GPU_CTX_MSG)Header;
            int Result = virgl_renderer_context_create(Message->CtxId, 5, "lxgpu");

            if (LxVerbose || (Result != 0))
                LxTrace("context %u -> %d\n", Message->CtxId, Result);
            LxReply(Header->Serial, (Result == 0) ? 0 : -22, NULL, 0);
            break;
        }

        case LX_GPU_CTX_DESTROY:
            LxForget(((PLX_GPU_CTX_MSG)Header)->CtxId, 0);
            virgl_renderer_context_destroy(((PLX_GPU_CTX_MSG)Header)->CtxId);
            break;

        case LX_GPU_RES_CREATE:
            LxCreateResource((PLX_GPU_RES_MSG)Header);
            break;

        case LX_GPU_RES_UNREF:
            LxDestroyResource(((PLX_GPU_RES_MSG)Header)->ResId);
            break;

        case LX_GPU_SUBMIT:
        {
            PLX_GPU_SUBMIT_MSG Message = (PLX_GPU_SUBMIT_MSG)Header;
            PULONG ResIds = (PULONG)(Message + 1);
            ULONG Index;
            int Result;

            for (Index = 0; Index < Message->ResCount; Index++)
                LxAttach(Message->CtxId, ResIds[Index]);
            Result = virgl_renderer_submit_cmd(ResIds + Message->ResCount, (int)Message->CtxId,
                                               (int)(Message->Length / sizeof(ULONG)));
            if (LxVerbose || (Result != 0))
                LxTrace("submit ctx %u %u dwords -> %d\n", Message->CtxId, Message->Length / 4, Result);
            if (virgl_renderer_create_fence((int)Header->Serial, Message->CtxId) == 0)
                LxPendingFences++;
            break;
        }

        case LX_GPU_TRANSFER:
        {
            PLX_GPU_TRANSFER_MSG Message = (PLX_GPU_TRANSFER_MSG)Header;
            struct virgl_box Box;
            int Result;

            memcpy(&Box, Message->Box, sizeof(Box));
            LxAttach(Message->CtxId, Message->ResId);
            if (Message->ToHost)
            {
                Result = virgl_renderer_transfer_write_iov(Message->ResId, Message->CtxId, (int)Message->Level,
                                                           Message->Stride, Message->LayerStride, &Box, Message->Offset,
                                                           NULL, 0);
            }
            else
            {
                Result = virgl_renderer_transfer_read_iov(Message->ResId, Message->CtxId, Message->Level,
                                                          Message->Stride, Message->LayerStride, &Box, Message->Offset,
                                                          NULL, 0);
            }
            if (LxVerbose || (Result != 0))
            {
                LxTrace("transfer %s res %u ctx %u box %ux%u -> %d\n", Message->ToHost ? "to" : "from",
                        Message->ResId, Message->CtxId, Box.w, Box.h, Result);
            }
            if (virgl_renderer_create_fence((int)Header->Serial, Message->CtxId) == 0)
                LxPendingFences++;
            break;
        }
    }
}

static
int
LxServe(PCWSTR Root, HANDLE Parent)
{
    WCHAR Full[MAX_PATH], Nt[MAX_PATH + 8];
    OVERLAPPED Overlapped = { 0 };
    PUCHAR Buffer;
    HANDLE Waits[2];
    DWORD Bytes, WaitCount = (Parent != NULL) ? 2 : 1;
    BOOL Pending = FALSE;
    int Result = -1;

    if (!GetFullPathNameW(Root, MAX_PATH, Full, NULL))
        return 1;
    _snwprintf(Nt, MAX_PATH + 8, L"\\??\\%s", Full);
    Nt[MAX_PATH + 7] = UNICODE_NULL;
    if (!LxInitializeRenderer())
    {
        LxTrace("renderer init failed\n");
        return 2;
    }
    LxDevice = CreateFileW(L"\\\\.\\lxcore" LX_GPU_FILE_NAME, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
                           FILE_FLAG_OVERLAPPED, NULL);
    if (LxDevice == INVALID_HANDLE_VALUE)
    {
        LxTrace("open lxcore gpu failed %lu\n", GetLastError());
        return 3;
    }
    Overlapped.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if ((Overlapped.hEvent == NULL) ||
        (!DeviceIoControl(LxDevice, IOCTL_LX_GPU_ATTACH, Nt, (DWORD)(wcslen(Nt) * sizeof(WCHAR)), NULL, 0, &Bytes,
                          &Overlapped) &&
         !((GetLastError() == ERROR_IO_PENDING) && GetOverlappedResult(LxDevice, &Overlapped, &Bytes, TRUE))))
    {
        LxTrace("attach failed %lu\n", GetLastError());
        return 4;
    }
    LxTrace("attached %ls\n", Nt);
    Buffer = malloc(LX_GPU_MAX_MESSAGE);
    if (Buffer == NULL)
        return 1;
    Waits[0] = Overlapped.hEvent;
    Waits[1] = Parent;
    timeBeginPeriod(1);
    while (Result < 0)
    {
        ResetEvent(Overlapped.hEvent);
        if (!DeviceIoControl(LxDevice, IOCTL_LX_GPU_RECEIVE, NULL, 0, Buffer, LX_GPU_MAX_MESSAGE, &Bytes, &Overlapped))
        {
            if (GetLastError() != ERROR_IO_PENDING)
            {
                LxTrace("receive failed %lu\n", GetLastError());
                Result = 5;
                break;
            }
            Pending = TRUE;
        }
        while (Pending && (Result < 0))
        {
            DWORD Wait = WaitForMultipleObjects(WaitCount, Waits, FALSE, (LxPendingFences > 0) ? 1 : INFINITE);

            if (Wait == WAIT_OBJECT_0 + 1)
            {
                LxTrace("window host exited\n");
                Result = 0;
            }
            else if (Wait == WAIT_OBJECT_0)
            {
                if (!GetOverlappedResult(LxDevice, &Overlapped, &Bytes, FALSE))
                {
                    LxTrace("receive completion failed %lu\n", GetLastError());
                    Result = 6;
                }
                Pending = FALSE;
            }
            else
            {
                virgl_renderer_poll();
            }
        }
        if ((Result < 0) && (Bytes >= sizeof(LX_GPU_HEADER)))
            LxDispatch((PLX_GPU_HEADER)Buffer);
        virgl_renderer_poll();
    }
    if (Pending && (CancelIoEx(LxDevice, &Overlapped) || (GetLastError() != ERROR_NOT_FOUND)))
        GetOverlappedResult(LxDevice, &Overlapped, &Bytes, TRUE);
    free(Buffer);
    return Result;
}

int
main(void)
{
    PWSTR *Arguments;
    PCWSTR Root = NULL;
    HANDLE Parent = NULL;
    int Count, Index;

    /* libepoxy is linked statically: its DllMain allocates the TLS slots of the per-thread dispatch tables */
    if (!epoxy_dll_main(GetModuleHandleW(NULL), DLL_PROCESS_ATTACH, NULL))
        return 1;
    Arguments = CommandLineToArgvW(GetCommandLineW(), &Count);
    if (Arguments == NULL)
        return 1;
    for (Index = 1; Index < Count; Index++)
    {
        if ((wcscmp(Arguments[Index], L"-r") == 0) && (Index + 1 < Count))
        {
            Root = Arguments[++Index];
        }
        else if ((wcscmp(Arguments[Index], L"-l") == 0) && (Index + 1 < Count))
        {
            LxLog = _wfopen(Arguments[++Index], L"w");
        }
        else if (wcscmp(Arguments[Index], L"-v") == 0)
        {
            LxVerbose = TRUE;
        }
        else if ((wcscmp(Arguments[Index], L"-p") == 0) && (Index + 1 < Count))
        {
            Parent = OpenProcess(SYNCHRONIZE, FALSE, (DWORD)wcstoul(Arguments[++Index], NULL, 10));
            if (Parent == NULL)
                return 1;
        }
    }
    if (Root == NULL)
        return 1;
    return LxServe(Root, Parent);
}
