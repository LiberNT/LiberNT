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
#include <vkr_renderer.h>
#include "lxgpu.h"

#define LX_ATTACH_RETRIES 100
#define LX_ATTACH_RETRY_MS 100
#define LX_RECEIVE_BUFFER (288 * 1024)

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

static LONG LxEpoxyReady;

static
VOID
NTAPI
LxThreadNotify(PVOID Module, DWORD Reason, PVOID Reserved)
{
    if (((Reason == DLL_THREAD_ATTACH) || (Reason == DLL_THREAD_DETACH)) &&
        (InterlockedCompareExchange(&LxEpoxyReady, 0, 0) != 0))
    {
        epoxy_dll_main((HINSTANCE)Module, Reason, Reserved);
    }
}

/* the statically linked libepoxy gets the thread notifications its DllMain would get (GL on renderer threads) */
__attribute__((section(".CRT$XLF"), used)) static const PIMAGE_TLS_CALLBACK LxThreadCallback = LxThreadNotify;
void *lxgpu_vulkan_loader(void);

static HANDLE LxDevice;
static HDC LxDc;
static HGLRC LxRootContext;
static PFNWGLCREATECONTEXTATTRIBSARBPROC LxCreateContextAttribs;
static LX_RESOURCE *LxResources;
static LX_ATTACHMENT *LxAttachments;
static LONG LxPendingFences;
static HANDLE LxPollEvent;
static FILE *LxLog;
static BOOL LxVerbose;
static BOOL LxVenus;
static PUCHAR LxVenusContexts;
static ULONG LxVenusContextCount;

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
    if ((LxPollEvent == NULL) && (LxPendingFences > 0))
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
    static BOOL (WINAPI *MakeCurrent)(HDC, HGLRC);
    BOOL Ok;

    (void)Cookie;
    (void)Scanout;
    /* epoxy's own wrapper dispatches through the calling thread's table, still empty on a new thread */
    if (MakeCurrent == NULL)
        MakeCurrent = (BOOL (WINAPI *)(HDC, HGLRC))GetProcAddress(GetModuleHandleW(L"opengl32.dll"), "wglMakeCurrent");
    Ok = (MakeCurrent != NULL) && MakeCurrent(LxDc, (HGLRC)Context);
    epoxy_handle_external_wglMakeCurrent();
    return Ok ? 0 : -1;
}

static
void
LxVirglLog(enum virgl_log_level_flags Level, const char *Message, void *Data)
{
    (void)Data;
    LxTrace("virgl[%d]: %s", (int)Level, Message);
}

static
void
LxRetireFence(uint32_t CtxId, uint32_t RingIdx, uint64_t FenceId)
{
    LX_GPU_RING_DONE_MSG Done = { { LX_GPU_RING_DONE, sizeof(Done), 0, 0 }, CtxId, RingIdx, FenceId };

    if (LxVerbose)
        LxTrace("ring fence ctx %u ring %u id %llu done\n", CtxId, RingIdx, (unsigned long long)FenceId);
    LxSend(&Done, sizeof(Done));
}

static
BOOL
LxIsVenusContext(ULONG CtxId)
{
    return (CtxId < LxVenusContextCount) && LxVenusContexts[CtxId];
}

static
BOOL
LxSetVenusContext(ULONG CtxId, BOOL Venus)
{
    if (CtxId >= LxVenusContextCount)
    {
        ULONG Count = max(CtxId + 1, LxVenusContextCount * 2);
        PUCHAR Contexts = realloc(LxVenusContexts, Count);

        if (Contexts == NULL)
            return FALSE;
        memset(Contexts + LxVenusContextCount, 0, Count - LxVenusContextCount);
        LxVenusContexts = Contexts;
        LxVenusContextCount = Count;
    }
    LxVenusContexts[CtxId] = (UCHAR)Venus;
    return TRUE;
}

static
BOOL
LxInitializeVenus(void)
{
    static const struct vkr_renderer_callbacks Callbacks = { LxVirglLog, LxRetireFence };

    if (lxgpu_vulkan_loader() == NULL)
    {
        LxTrace("no Vulkan loader, venus disabled\n");
        return FALSE;
    }
    return vkr_renderer_init(VKR_RENDERER_THREAD_SYNC | VKR_RENDERER_ASYNC_FENCE_CB, &Callbacks);
}

static
void
LxCreateBlob(PLX_GPU_BLOB_MSG Message)
{
    LX_GPU_BLOB_REPLY Blob = { 0 };
    enum virgl_resource_fd_type FdType;
    struct virgl_resource_vulkan_info VulkanInfo;
    uint32_t MapInfo = 0;
    int Fd = -1;
    BOOL Created = FALSE;

    if (LxIsVenusContext(Message->CtxId))
    {
        Created = vkr_renderer_create_resource(Message->CtxId, Message->ResId, Message->BlobId, Message->Size,
                                               Message->BlobFlags, &FdType, &Fd, &MapInfo, &VulkanInfo);
    }
    if (Created && (Fd < 0))
    {
        vkr_renderer_destroy_resource(Message->CtxId, Message->ResId);
        Created = FALSE;
    }
    if (LxVerbose || !Created)
    {
        LxTrace("blob ctx %u res %u id %llu size %llu flags %x -> %d\n", Message->CtxId, Message->ResId,
                (unsigned long long)Message->BlobId, (unsigned long long)Message->Size, Message->BlobFlags, Created);
    }
    if (!Created)
    {
        LxReply(Message->Header.Serial, -22, NULL, 0);
        return;
    }
    Blob.Section = (ULONG64)(LONG_PTR)Fd;
    Blob.MapInfo = MapInfo;
    LxReply(Message->Header.Serial, 0, &Blob, sizeof(Blob));
    CloseHandle((HANDLE)(LONG_PTR)Fd);
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
    int PollFd;

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
    if (virgl_renderer_init(&LxDc, VIRGL_RENDERER_THREAD_SYNC | VIRGL_RENDERER_ASYNC_FENCE_CB, &Callbacks) != 0)
        return FALSE;
    PollFd = virgl_renderer_get_poll_fd();
    LxPollEvent = (PollFd >= 0) ? (HANDLE)(intptr_t)PollFd : NULL;
    LxTrace("fence sync thread %s\n", (LxPollEvent != NULL) ? "enabled" : "disabled");
    LxVenus = LxInitializeVenus();
    LxTrace("venus %s\n", LxVenus ? "enabled" : "disabled");
    return TRUE;
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

            if (Message->Capset == LX_GPU_CAPSET_VENUS)
            {
                MaxSize = LxVenus ? (uint32_t)vkr_get_capset(NULL, 0) : 0;
                Caps = (MaxSize != 0) ? calloc(1, MaxSize) : NULL;
                if (Caps != NULL)
                    vkr_get_capset(Caps, 0);
                LxReply(Header->Serial, (Caps != NULL) ? 0 : -22, Caps, (Caps != NULL) ? MaxSize : 0);
                free(Caps);
                break;
            }
            virgl_renderer_get_cap_set(Message->Capset, &MaxVersion, &MaxSize);
            Caps = (MaxSize != 0) ? calloc(1, MaxSize) : NULL;
            if (Caps == NULL)
            {
                LxReply(Header->Serial, -22, NULL, 0);
                break;
            }
            virgl_renderer_fill_caps(Message->Capset, MaxVersion, Caps);
            /* FIXME: coherent persistent GL buffers need host blob mappings the Linux side cannot map yet */
            if ((Message->Capset == LX_GPU_CAPSET_VIRGL2) && (MaxSize >= sizeof(struct virgl_caps_v2)))
                ((union virgl_caps *)Caps)->v2.capability_bits &= ~VIRGL_CAP_ARB_BUFFER_STORAGE;
            LxReply(Header->Serial, 0, Caps, MaxSize);
            free(Caps);
            break;
        }

        case LX_GPU_CTX_CREATE:
        {
            PLX_GPU_CTX_MSG Message = (PLX_GPU_CTX_MSG)Header;
            int Result = -1;

            if (Message->Capset == LX_GPU_CAPSET_VENUS)
            {
                if (LxVenus && LxSetVenusContext(Message->CtxId, TRUE) &&
                    vkr_renderer_create_context(Message->CtxId, LX_GPU_CAPSET_VENUS, 5, "lxgpu"))
                {
                    Result = 0;
                }
                else
                {
                    LxSetVenusContext(Message->CtxId, FALSE);
                }
            }
            else
            {
                Result = virgl_renderer_context_create(Message->CtxId, 5, "lxgpu");
            }

            if (LxVerbose || (Result != 0))
                LxTrace("context %u -> %d\n", Message->CtxId, Result);
            LxReply(Header->Serial, (Result == 0) ? 0 : -22, NULL, 0);
            break;
        }

        case LX_GPU_CTX_DESTROY:
            if (LxIsVenusContext(((PLX_GPU_CTX_MSG)Header)->CtxId))
            {
                vkr_renderer_destroy_context(((PLX_GPU_CTX_MSG)Header)->CtxId);
                LxSetVenusContext(((PLX_GPU_CTX_MSG)Header)->CtxId, FALSE);
                break;
            }
            LxForget(((PLX_GPU_CTX_MSG)Header)->CtxId, 0);
            virgl_renderer_context_destroy(((PLX_GPU_CTX_MSG)Header)->CtxId);
            break;

        case LX_GPU_RES_CREATE:
            LxCreateResource((PLX_GPU_RES_MSG)Header);
            break;

        case LX_GPU_RES_UNREF:
            if (LxIsVenusContext(((PLX_GPU_RES_MSG)Header)->CtxId))
                vkr_renderer_destroy_resource(((PLX_GPU_RES_MSG)Header)->CtxId, ((PLX_GPU_RES_MSG)Header)->ResId);
            else
                LxDestroyResource(((PLX_GPU_RES_MSG)Header)->ResId);
            break;

        case LX_GPU_BLOB_CREATE:
            LxCreateBlob((PLX_GPU_BLOB_MSG)Header);
            break;

        case LX_GPU_SUBMIT:
        {
            PLX_GPU_SUBMIT_MSG Message = (PLX_GPU_SUBMIT_MSG)Header;
            PULONG ResIds = (PULONG)(Message + 1);
            ULONG Index;
            int Result;

            if (LxIsVenusContext(Message->CtxId))
            {
                BOOL Submitted = (Message->Length == 0) ||
                                 vkr_renderer_submit_cmd(Message->CtxId, ResIds + Message->ResCount, Message->Length);

                if (LxVerbose || !Submitted)
                {
                    LxTrace("venus submit ctx %u ring %u %u bytes -> %d\n", Message->CtxId, Message->RingIdx,
                            Message->Length, Submitted);
                }
                if (!vkr_renderer_submit_fence(Message->CtxId, 0, Message->RingIdx, Header->Serial))
                    LxRetireFence(Message->CtxId, Message->RingIdx, Header->Serial);
                break;
            }
            for (Index = 0; Index < Message->ResCount; Index++)
                LxAttach(Message->CtxId, ResIds[Index]);
            Result = virgl_renderer_submit_cmd(ResIds + Message->ResCount, (int)Message->CtxId,
                                               (int)(Message->Length / sizeof(ULONG)));
            if (LxVerbose || (Result != 0))
                LxTrace("submit ctx %u %u dwords -> %d\n", Message->CtxId, Message->Length / 4, Result);
            if (virgl_renderer_create_fence((int)Header->Serial, Message->CtxId) == 0)
                LxPendingFences += (LxPollEvent == NULL);
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
                LxPendingFences += (LxPollEvent == NULL);
            break;
        }
    }
}

static
BOOL
LxAttachChannel(PCWSTR Nt, HANDLE Event, HANDLE Parent)
{
    OVERLAPPED Overlapped = { 0 };
    DWORD Bytes, Attempt;

    Overlapped.hEvent = Event;
    for (Attempt = 0; Attempt < LX_ATTACH_RETRIES; Attempt++)
    {
        if (DeviceIoControl(LxDevice, IOCTL_LX_GPU_ATTACH, (PVOID)Nt, (DWORD)(wcslen(Nt) * sizeof(WCHAR)), NULL, 0,
                            &Bytes, &Overlapped) ||
            ((GetLastError() == ERROR_IO_PENDING) && GetOverlappedResult(LxDevice, &Overlapped, &Bytes, TRUE)))
        {
            return TRUE;
        }
        if (GetLastError() != ERROR_BUSY)
            return FALSE;
        if (Parent == NULL)
            Sleep(LX_ATTACH_RETRY_MS);
        else if (WaitForSingleObject(Parent, LX_ATTACH_RETRY_MS) == WAIT_OBJECT_0)
            return FALSE;
    }
    return FALSE;
}

static
int
LxServe(PCWSTR Root, HANDLE Parent)
{
    WCHAR Full[MAX_PATH], Nt[MAX_PATH + 8];
    CHAR Utf8[3 * (MAX_PATH + 8)];
    OVERLAPPED Overlapped = { 0 };
    PUCHAR Buffer;
    HANDLE Waits[3];
    DWORD Bytes, WaitCount = 1, ParentIndex = MAXDWORD, PollIndex = MAXDWORD;
    BOOL Pending = FALSE;
    ULONG Capacity = LX_RECEIVE_BUFFER;
    int Result = -1;

    if (!GetFullPathNameW(Root, MAX_PATH, Full, NULL) || (wcslen(Full) + 4 >= ARRAYSIZE(Nt)))
        return 1;
    wcscpy(Nt, L"\\??\\");
    wcscat(Nt, Full);
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
    if ((Overlapped.hEvent == NULL) || !LxAttachChannel(Nt, Overlapped.hEvent, Parent))
    {
        LxTrace("attach failed %lu\n", GetLastError());
        return 4;
    }
    if (!WideCharToMultiByte(CP_UTF8, 0, Nt, -1, Utf8, sizeof(Utf8), NULL, NULL))
        Utf8[0] = '\0';
    LxTrace("attached %s\n", Utf8);
    Buffer = malloc(Capacity);
    if (Buffer == NULL)
        return 1;
    Waits[0] = Overlapped.hEvent;
    if (Parent != NULL)
    {
        ParentIndex = WaitCount;
        Waits[WaitCount++] = Parent;
    }
    if (LxPollEvent != NULL)
    {
        PollIndex = WaitCount;
        Waits[WaitCount++] = LxPollEvent;
    }
    timeBeginPeriod(1);
    while (Result < 0)
    {
        ResetEvent(Overlapped.hEvent);
        if (!DeviceIoControl(LxDevice, IOCTL_LX_GPU_RECEIVE, NULL, 0, Buffer, Capacity, &Bytes, &Overlapped))
        {
            if (GetLastError() == ERROR_MORE_DATA)
            {
                PUCHAR Grown = NULL;

                if (!GetOverlappedResult(LxDevice, &Overlapped, &Bytes, FALSE) && (GetLastError() == ERROR_MORE_DATA) &&
                    (Bytes > Capacity) && (Bytes <= LX_GPU_MAX_MESSAGE))
                {
                    Grown = realloc(Buffer, Bytes);
                }
                if (Grown == NULL)
                {
                    LxTrace("receive buffer of %lu bytes failed\n", Bytes);
                    Result = 5;
                    break;
                }
                Buffer = Grown;
                Capacity = Bytes;
                continue;
            }
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

            if (Wait == WAIT_OBJECT_0 + ParentIndex)
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
        if (Capacity > LX_RECEIVE_BUFFER)
        {
            PUCHAR Shrunk = realloc(Buffer, LX_RECEIVE_BUFFER);

            if (Shrunk != NULL)
            {
                Buffer = Shrunk;
                Capacity = LX_RECEIVE_BUFFER;
            }
        }
    }
    CloseHandle(LxDevice);
    LxDevice = NULL;
    if (Pending)
        WaitForSingleObject(Overlapped.hEvent, INFINITE);
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
    InterlockedExchange(&LxEpoxyReady, 1);
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
