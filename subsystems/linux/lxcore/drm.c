/*
 * PROJECT:     LiberNT Linux Subsystem
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     virtio-gpu DRM render node backed by a Windows GPU renderer process
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "lxcore.h"
#include "../include/lxgpu.h"

#define LX_DRM_IOCTL_VERSION                0xC0406400
#define LX_DRM_IOCTL_GET_CAP                0xC010640C
#define LX_DRM_IOCTL_SET_CLIENT_CAP         0x4010640D
#define LX_DRM_IOCTL_GEM_CLOSE              0x40086409
#define LX_DRM_IOCTL_PRIME_HANDLE_TO_FD     0xC00C642D
#define LX_DRM_IOCTL_PRIME_FD_TO_HANDLE     0xC00C642E
#define LX_DMA_BUF_IOCTL_SYNC               0x40086200
#define LX_DRM_CAP_PRIME                    5
#define LX_DRM_PRIME_CAP_IMPORT             1
#define LX_DRM_PRIME_CAP_EXPORT             2
#define LX_DRM_CLOEXEC                      0x00080000
#define LX_DRM_RDWR                         0x00000002
#define LX_DRM_IOCTL_VIRTGPU_MAP            0xC0106441
#define LX_DRM_IOCTL_VIRTGPU_EXECBUFFER     0xC0406442
#define LX_DRM_IOCTL_VIRTGPU_GETPARAM       0xC0106443
#define LX_DRM_IOCTL_VIRTGPU_RESOURCE_CREATE 0xC0386444
#define LX_DRM_IOCTL_VIRTGPU_RESOURCE_INFO  0xC0106445
#define LX_DRM_IOCTL_VIRTGPU_FROM_HOST      0xC02C6446
#define LX_DRM_IOCTL_VIRTGPU_TO_HOST        0xC02C6447
#define LX_DRM_IOCTL_VIRTGPU_WAIT           0xC0086448
#define LX_DRM_IOCTL_VIRTGPU_GET_CAPS       0xC0186449
#define LX_DRM_IOCTL_VIRTGPU_RESOURCE_CREATE_BLOB 0xC030644A
#define LX_DRM_IOCTL_VIRTGPU_CONTEXT_INIT   0xC010644B

#define LX_VIRTGPU_PARAM_3D_FEATURES        1
#define LX_VIRTGPU_PARAM_CAPSET_QUERY_FIX   2
#define LX_VIRTGPU_PARAM_RESOURCE_BLOB      3
#define LX_VIRTGPU_PARAM_HOST_VISIBLE       4
#define LX_VIRTGPU_PARAM_CONTEXT_INIT       6
#define LX_VIRTGPU_PARAM_SUPPORTED_CAPSET_IDS 7
#define LX_VIRTGPU_EXECBUF_FENCE_FD_IN      0x01
#define LX_VIRTGPU_EXECBUF_FENCE_FD_OUT     0x02
#define LX_VIRTGPU_EXECBUF_RING_IDX         0x04
#define LX_VIRTGPU_CONTEXT_PARAM_CAPSET_ID  1
#define LX_VIRTGPU_CONTEXT_PARAM_NUM_RINGS  2
#define LX_VIRTGPU_CONTEXT_PARAM_POLL_RINGS_MASK 3
#define LX_VIRTGPU_CONTEXT_PARAM_DEBUG_NAME 4
#define LX_VIRTGPU_BLOB_MEM_HOST3D          2
#define LX_VIRTGPU_BLOB_FLAGS               0x7
#define LX_DRM_MAX_RINGS                    64
#define LX_DRM_MAX_CONTEXT_PARAMS           16
#define LX_VIRTGPU_PARAM_MAX                8
#define LX_VIRTGPU_WAIT_NOWAIT              1
#define LX_VIRTGPU_CAPSET_VIRGL             1
#define LX_VIRTGPU_CAPSET_VIRGL2            2
#define LX_VIRTGPU_CAPSET_VENUS             4
#define LX_DRM_MAX_COMMAND                  (8 * 1024 * 1024)
#define LX_DRM_MAX_BO_HANDLES               4096
#define LX_DRM_MAP_SHIFT                    32
#define LX_DRM_WAIT_TIMEOUT                 (-15LL * 10000000LL)
#define LX_DRM_REPLY_TIMEOUT                (-30LL * 10000000LL)
#define LX_GPU_MAX_QUEUED                   (32 * 1024 * 1024)
#define LX_GPU_DRAIN_POLL                   (-100LL * 10000LL)

typedef struct _LX_GPU_REQUEST
{
    LIST_ENTRY Link;
    LONG RefCount;
    BOOLEAN Sync;
    PVOID Section;
    KEVENT Done;
    LONG Status;
    PUCHAR Reply;
    ULONG ReplyLength;
    ULONG Size;
    LX_GPU_HEADER Message[ANYSIZE_ARRAY];
} LX_GPU_REQUEST, *PLX_GPU_REQUEST;

typedef struct _LX_GPU_CHANNEL
{
    LIST_ENTRY Link;
    LONG RefCount;
    UNICODE_STRING RootNt;
    KSPIN_LOCK Lock;
    LIST_ENTRY ToHost;
    LIST_ENTRY Pending;
    LIST_ENTRY Waiting;
    BOOLEAN HostAttached;
    ULONG Generation;
    ULONG64 QueuedBytes;
    KEVENT DrainEvent;
    ULONG NextSerial;
    volatile LONG Done;
    KEVENT DoneEvent;
    volatile LONG NextResId;
    volatile LONG NextCtxId;
    FAST_MUTEX BoLock;
    LIST_ENTRY Bos;
    LIST_ENTRY Fences;
    PUCHAR Caps;
    ULONG CapsLength;
    PUCHAR VenusCaps;
    ULONG VenusCapsLength;
    volatile LONG VenusState;
} LX_GPU_CHANNEL, *PLX_GPU_CHANNEL;

typedef struct _LX_GPU_FENCE
{
    LIST_ENTRY Link;
    PLX_GPU_CHANNEL Channel;
    ULONG CtxId;
    ULONG RingIdx;
    BOOLEAN Global;
    BOOLEAN Queued;
    volatile LONG Signaled;
    ULONG Serial;
    LX_POLL_HEAD Poll;
} LX_GPU_FENCE, *PLX_GPU_FENCE;

typedef struct _LX_DRM_EXECBUFFER_HEAD
{
    ULONG Flags;
    ULONG Size;
    ULONG64 Command;
    ULONG64 BoHandles;
    ULONG NumBoHandles;
    LONG FenceFd;
    ULONG Rest[8];
} LX_DRM_EXECBUFFER_HEAD, *PLX_DRM_EXECBUFFER_HEAD;

typedef struct _LX_GPU_HOST
{
    PLX_GPU_CHANNEL Channel;
} LX_GPU_HOST, *PLX_GPU_HOST;

typedef struct _LX_DRM_BO
{
    LIST_ENTRY Link;
    volatile LONG RefCount;
    PLX_GPU_CHANNEL Channel;
    ULONG ResId;
    ULONG64 Size;
    PVOID Section;
    volatile LONG Busy;
    ULONG CtxId;
    ULONG BlobMem;
} LX_DRM_BO, *PLX_DRM_BO;

typedef struct _LX_DRM_FILE
{
    PLX_GPU_CHANNEL Channel;
    FAST_MUTEX Lock;
    PLX_DRM_BO *Handles;
    ULONG Capacity;
    ULONG CtxId;
    ULONG Capset;
    ULONG NumRings;
    ULONG Generation;
} LX_DRM_FILE, *PLX_DRM_FILE;

static LIST_ENTRY LxGpuChannels;
static FAST_MUTEX LxGpuChannelsLock;

VOID
LxDrmInitialize(VOID)
{
    InitializeListHead(&LxGpuChannels);
    ExInitializeFastMutex(&LxGpuChannelsLock);
}

BOOLEAN
LxDrmPresent(VOID)
{
    PLIST_ENTRY Entry;
    BOOLEAN Present = FALSE;
    KIRQL Irql;

    ExAcquireFastMutex(&LxGpuChannelsLock);
    for (Entry = LxGpuChannels.Flink; (Entry != &LxGpuChannels) && !Present; Entry = Entry->Flink)
    {
        PLX_GPU_CHANNEL Channel = CONTAINING_RECORD(Entry, LX_GPU_CHANNEL, Link);

        KeAcquireSpinLock(&Channel->Lock, &Irql);
        Present = Channel->HostAttached;
        KeReleaseSpinLock(&Channel->Lock, Irql);
    }
    ExReleaseFastMutex(&LxGpuChannelsLock);
    return Present;
}

static
PLX_GPU_CHANNEL
LxGpuChannelGet(
    _In_ PCUNICODE_STRING RootNt)
{
    PLX_GPU_CHANNEL Channel;
    PLIST_ENTRY Entry;

    ExAcquireFastMutex(&LxGpuChannelsLock);
    for (Entry = LxGpuChannels.Flink; Entry != &LxGpuChannels; Entry = Entry->Flink)
    {
        Channel = CONTAINING_RECORD(Entry, LX_GPU_CHANNEL, Link);
        if (RtlEqualUnicodeString(&Channel->RootNt, RootNt, TRUE))
        {
            InterlockedIncrement(&Channel->RefCount);
            ExReleaseFastMutex(&LxGpuChannelsLock);
            return Channel;
        }
    }
    Channel = LxAlloc(sizeof(*Channel) + RootNt->Length);
    if (Channel != NULL)
    {
        RtlZeroMemory(Channel, sizeof(*Channel));
        Channel->RefCount = 1;
        Channel->RootNt.Buffer = (PWCHAR)(Channel + 1);
        Channel->RootNt.Length = Channel->RootNt.MaximumLength = RootNt->Length;
        RtlCopyMemory(Channel->RootNt.Buffer, RootNt->Buffer, RootNt->Length);
        KeInitializeSpinLock(&Channel->Lock);
        InitializeListHead(&Channel->ToHost);
        InitializeListHead(&Channel->Pending);
        InitializeListHead(&Channel->Waiting);
        InitializeListHead(&Channel->Bos);
        InitializeListHead(&Channel->Fences);
        KeInitializeEvent(&Channel->DoneEvent, NotificationEvent, FALSE);
        KeInitializeEvent(&Channel->DrainEvent, NotificationEvent, TRUE);
        ExInitializeFastMutex(&Channel->BoLock);
        InsertTailList(&LxGpuChannels, &Channel->Link);
    }
    ExReleaseFastMutex(&LxGpuChannelsLock);
    return Channel;
}

static
VOID
LxGpuRequestRelease(
    _In_ PLX_GPU_REQUEST Request)
{
    if (InterlockedDecrement(&Request->RefCount) != 0)
        return;
    if (Request->Section != NULL)
        ObDereferenceObject(Request->Section);
    if (Request->Reply != NULL)
        LxFree(Request->Reply);
    LxFree(Request);
}

static
VOID
LxGpuChannelRelease(
    _In_ PLX_GPU_CHANNEL Channel)
{
    ExAcquireFastMutex(&LxGpuChannelsLock);
    if (InterlockedDecrement(&Channel->RefCount) != 0)
    {
        ExReleaseFastMutex(&LxGpuChannelsLock);
        return;
    }
    RemoveEntryList(&Channel->Link);
    ExReleaseFastMutex(&LxGpuChannelsLock);
    if (Channel->Caps != NULL)
        LxFree(Channel->Caps);
    if (Channel->VenusCaps != NULL)
        LxFree(Channel->VenusCaps);
    LxFree(Channel);
}

static
PLX_GPU_REQUEST
LxGpuRequestAlloc(
    _In_ ULONG Type,
    _In_ ULONG Size)
{
    PLX_GPU_REQUEST Request = LxAlloc(FIELD_OFFSET(LX_GPU_REQUEST, Message) + Size);

    if (Request == NULL)
        return NULL;
    RtlZeroMemory(Request, FIELD_OFFSET(LX_GPU_REQUEST, Message) + Size);
    Request->RefCount = 1;
    Request->Size = Size;
    KeInitializeEvent(&Request->Done, NotificationEvent, FALSE);
    Request->Message->Type = Type;
    Request->Message->Size = Size;
    return Request;
}

static
VOID
NTAPI
LxGpuCancel(
    _Inout_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PLX_GPU_CHANNEL Channel = Irp->Tail.Overlay.DriverContext[0];
    KIRQL Irql;

    UNREFERENCED_PARAMETER(DeviceObject);
    IoReleaseCancelSpinLock(Irp->CancelIrql);
    KeAcquireSpinLock(&Channel->Lock, &Irql);
    RemoveEntryList(&Irp->Tail.Overlay.ListEntry);
    KeReleaseSpinLock(&Channel->Lock, Irql);
    Irp->IoStatus.Status = STATUS_CANCELLED;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
}

static
PIRP
LxGpuTakePending(
    _In_ PLX_GPU_CHANNEL Channel)
{
    while (!IsListEmpty(&Channel->Pending))
    {
        PIRP Irp = CONTAINING_RECORD(RemoveHeadList(&Channel->Pending), IRP, Tail.Overlay.ListEntry);

        if (IoSetCancelRoutine(Irp, NULL) != NULL)
            return Irp;
        InitializeListHead(&Irp->Tail.Overlay.ListEntry);
    }
    return NULL;
}

static
LONG
LxGpuSendFenced(
    _In_ PLX_GPU_CHANNEL Channel,
    _In_ PLX_GPU_REQUEST Request,
    _Inout_opt_ PLX_GPU_FENCE Fence)
{
    LARGE_INTEGER Timeout;
    PIRP Irp;
    KIRQL Irql;

    for (;;)
    {
        KeAcquireSpinLock(&Channel->Lock, &Irql);
        if (!Channel->HostAttached)
        {
            KeReleaseSpinLock(&Channel->Lock, Irql);
            return -LX_ENODEV;
        }
        if ((Channel->QueuedBytes == 0) || (Channel->QueuedBytes + Request->Size <= LX_GPU_MAX_QUEUED))
            break;
        KeClearEvent(&Channel->DrainEvent);
        KeReleaseSpinLock(&Channel->Lock, Irql);
        Timeout.QuadPart = LX_GPU_DRAIN_POLL;
        KeWaitForSingleObject(&Channel->DrainEvent, Executive, KernelMode, FALSE, &Timeout);
    }
    Channel->QueuedBytes += Request->Size;
    Request->Message->Serial = ++Channel->NextSerial;
    if (Fence != NULL)
    {
        Fence->Serial = Request->Message->Serial;
        Fence->Queued = TRUE;
        InsertTailList(&Channel->Fences, &Fence->Link);
    }
    InterlockedIncrement(&Request->RefCount);
    InsertTailList(&Channel->ToHost, &Request->Link);
    Irp = LxGpuTakePending(Channel);
    KeReleaseSpinLock(&Channel->Lock, Irql);
    if (Irp != NULL)
    {
        Irp->IoStatus.Status = STATUS_SUCCESS;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
    }
    return 0;
}

static
LONG
LxGpuSend(
    _In_ PLX_GPU_CHANNEL Channel,
    _In_ PLX_GPU_REQUEST Request)
{
    return LxGpuSendFenced(Channel, Request, NULL);
}

static
VOID
LxGpuSignalFences(
    _In_ PLX_GPU_CHANNEL Channel,
    _In_ BOOLEAN All,
    _In_ BOOLEAN Global,
    _In_ ULONG CtxId,
    _In_ ULONG RingIdx,
    _In_ ULONG Completed)
{
    PLIST_ENTRY Entry, Next;
    KIRQL Irql;

    KeAcquireSpinLock(&Channel->Lock, &Irql);
    for (Entry = Channel->Fences.Flink; Entry != &Channel->Fences; Entry = Next)
    {
        PLX_GPU_FENCE Fence = CONTAINING_RECORD(Entry, LX_GPU_FENCE, Link);

        Next = Entry->Flink;
        if (!All && ((Fence->Global != Global) || (!Global && ((Fence->CtxId != CtxId) || (Fence->RingIdx != RingIdx))) ||
                     ((LONG)(Completed - Fence->Serial) < 0)))
        {
            continue;
        }
        RemoveEntryList(&Fence->Link);
        Fence->Queued = FALSE;
        InterlockedExchange(&Fence->Signaled, 1);
        LxPollWake(&Fence->Poll, LX_POLLIN | LX_POLLRDNORM);
    }
    KeReleaseSpinLock(&Channel->Lock, Irql);
}

static
ULONG
LxGpuFencePoll(
    _In_ PLX_FILE File,
    _In_opt_ PLX_POLL_ENTRY Entry)
{
    PLX_GPU_FENCE Fence = File->Object;

    if (Entry != NULL)
        LxPollAdd(&Fence->Poll, Entry);
    return ReadNoFence(&Fence->Signaled) ? (LX_POLLIN | LX_POLLRDNORM) : 0;
}

static
VOID
LxGpuFenceRelease(
    _In_ PLX_FILE File)
{
    PLX_GPU_FENCE Fence = File->Object;
    KIRQL Irql;

    if (Fence == NULL)
        return;
    File->Object = NULL;
    KeAcquireSpinLock(&Fence->Channel->Lock, &Irql);
    if (Fence->Queued)
        RemoveEntryList(&Fence->Link);
    KeReleaseSpinLock(&Fence->Channel->Lock, Irql);
    LxGpuChannelRelease(Fence->Channel);
    LxFree(Fence);
}

static const LX_FILE_OPS LxGpuFenceOps =
{
    NULL, NULL, NULL, LxGpuFencePoll, LxAnonStat, NULL, NULL, NULL, LxGpuFenceRelease
};

static
LONG
LxGpuCall(
    _In_ PLX_GPU_CHANNEL Channel,
    _In_ PLX_GPU_REQUEST Request)
{
    LARGE_INTEGER Timeout;
    LONG Error;

    Request->Sync = TRUE;
    Error = LxGpuSend(Channel, Request);
    if (Error != 0)
        return Error;
    Timeout.QuadPart = LX_DRM_REPLY_TIMEOUT;
    if (KeWaitForSingleObject(&Request->Done, Executive, KernelMode, FALSE, &Timeout) != STATUS_SUCCESS)
        return -LX_ETIMEDOUT;
    return (Request->Status < 0) ? Request->Status : 0;
}

static
BOOLEAN
LxGpuWaitDone(
    _In_ PLX_GPU_CHANNEL Channel,
    _In_ LONG Serial,
    _In_ BOOLEAN NoWait)
{
    LARGE_INTEGER Timeout;
    LONG64 Deadline = (LONG64)KeQueryInterruptTime() - LX_DRM_WAIT_TIMEOUT;

    for (;;)
    {
        if ((Serial == 0) || ((LONG)(ReadNoFence(&Channel->Done) - Serial) >= 0))
            return TRUE;
        if (NoWait || ((LONG64)KeQueryInterruptTime() >= Deadline))
            return FALSE;
        KeClearEvent(&Channel->DoneEvent);
        if ((LONG)(ReadNoFence(&Channel->Done) - Serial) >= 0)
            return TRUE;
        Timeout.QuadPart = -10LL * 10000LL;
        KeWaitForSingleObject(&Channel->DoneEvent, UserRequest, UserMode, FALSE, &Timeout);
    }
}

static
VOID
LxDrmBoRelease(
    _In_ PLX_DRM_BO Bo)
{
    PLX_GPU_CHANNEL Channel = Bo->Channel;
    PLX_GPU_REQUEST Request;

    if (InterlockedDecrement(&Bo->RefCount) != 0)
        return;
    ExAcquireFastMutex(&Channel->BoLock);
    RemoveEntryList(&Bo->Link);
    ExReleaseFastMutex(&Channel->BoLock);
    Request = LxGpuRequestAlloc(LX_GPU_RES_UNREF, sizeof(LX_GPU_RES_MSG));
    if (Request != NULL)
    {
        ((PLX_GPU_RES_MSG)Request->Message)->ResId = Bo->ResId;
        ((PLX_GPU_RES_MSG)Request->Message)->CtxId = Bo->CtxId;
        LxGpuSend(Channel, Request);
        LxGpuRequestRelease(Request);
    }
    ObDereferenceObject(Bo->Section);
    LxGpuChannelRelease(Channel);
    LxFree(Bo);
}

static
PLX_DRM_BO
LxDrmBoLookup(
    _In_ PLX_DRM_FILE Drm,
    _In_ ULONG Handle)
{
    PLX_DRM_BO Bo = NULL;

    ExAcquireFastMutex(&Drm->Lock);
    if ((Handle != 0) && (Handle <= Drm->Capacity) && (Drm->Handles[Handle - 1] != NULL))
    {
        Bo = Drm->Handles[Handle - 1];
        InterlockedIncrement(&Bo->RefCount);
    }
    ExReleaseFastMutex(&Drm->Lock);
    return Bo;
}

static
LONG
LxDrmHandleAdd(
    _In_ PLX_DRM_FILE Drm,
    _In_ PLX_DRM_BO Bo,
    _Out_ PULONG Handle)
{
    ULONG Index;

    ExAcquireFastMutex(&Drm->Lock);
    for (Index = 0; Index < Drm->Capacity; Index++)
    {
        if (Drm->Handles[Index] == NULL)
            break;
    }
    if (Index == Drm->Capacity)
    {
        ULONG Capacity = max(Drm->Capacity * 2, 64);
        PLX_DRM_BO *Handles = LxAlloc(Capacity * sizeof(PLX_DRM_BO));

        if (Handles == NULL)
        {
            ExReleaseFastMutex(&Drm->Lock);
            return -LX_ENOMEM;
        }
        RtlZeroMemory(Handles, Capacity * sizeof(PLX_DRM_BO));
        if (Drm->Handles != NULL)
        {
            RtlCopyMemory(Handles, Drm->Handles, Drm->Capacity * sizeof(PLX_DRM_BO));
            LxFree(Drm->Handles);
        }
        Drm->Handles = Handles;
        Drm->Capacity = Capacity;
    }
    InterlockedIncrement(&Bo->RefCount);
    Drm->Handles[Index] = Bo;
    *Handle = Index + 1;
    ExReleaseFastMutex(&Drm->Lock);
    return 0;
}

static
BOOLEAN
LxDrmHasVenus(
    _In_ PLX_GPU_CHANNEL Channel);

static
LONG
LxDrmCreateContext(
    _In_ PLX_DRM_FILE Drm,
    _In_ ULONG Capset,
    _In_ ULONG NumRings,
    _In_ BOOLEAN Explicit)
{
    PLX_GPU_REQUEST Request;
    LONG Error;

    if (!Explicit && (ReadNoFence((PLONG)&Drm->CtxId) != 0))
        return 0;
    ExAcquireFastMutex(&Drm->Lock);
    if (Drm->CtxId != 0)
    {
        ExReleaseFastMutex(&Drm->Lock);
        return Explicit ? -LX_EEXIST : 0;
    }
    Request = LxGpuRequestAlloc(LX_GPU_CTX_CREATE, sizeof(LX_GPU_CTX_MSG));
    if (Request == NULL)
    {
        ExReleaseFastMutex(&Drm->Lock);
        return -LX_ENOMEM;
    }
    ((PLX_GPU_CTX_MSG)Request->Message)->CtxId = (ULONG)InterlockedIncrement(&Drm->Channel->NextCtxId);
    ((PLX_GPU_CTX_MSG)Request->Message)->Capset = Capset;
    Error = LxGpuCall(Drm->Channel, Request);
    if (Error == 0)
    {
        Drm->Capset = Capset;
        Drm->NumRings = NumRings;
        Drm->CtxId = ((PLX_GPU_CTX_MSG)Request->Message)->CtxId;
    }
    ExReleaseFastMutex(&Drm->Lock);
    LxGpuRequestRelease(Request);
    return Error;
}

static
LONG
LxDrmEnsureContext(
    _In_ PLX_DRM_FILE Drm)
{
    return LxDrmCreateContext(Drm, LX_VIRTGPU_CAPSET_VIRGL2, 0, FALSE);
}

static
LONG64
LxDrmContextInit(
    _In_ PLX_DRM_FILE Drm,
    _In_ ULONG64 Argument)
{
    struct
    {
        ULONG NumParams;
        ULONG Pad;
        ULONG64 Params;
    } Init;
    ULONG64 Param[2];
    ULONG Index, Capset = LX_VIRTGPU_CAPSET_VIRGL2, NumRings = 0;

    if (LxCopyFromUser(&Init, Argument, sizeof(Init)) != 0)
        return -LX_EFAULT;
    if (Init.NumParams > LX_DRM_MAX_CONTEXT_PARAMS)
        return -LX_EINVAL;
    for (Index = 0; Index < Init.NumParams; Index++)
    {
        if (LxCopyFromUser(Param, Init.Params + Index * sizeof(Param), sizeof(Param)) != 0)
            return -LX_EFAULT;
        switch (Param[0])
        {
            case LX_VIRTGPU_CONTEXT_PARAM_CAPSET_ID:
                Capset = (ULONG)Param[1];
                break;
            case LX_VIRTGPU_CONTEXT_PARAM_NUM_RINGS:
                if (Param[1] > LX_DRM_MAX_RINGS)
                    return -LX_EINVAL;
                NumRings = (ULONG)Param[1];
                break;
            case LX_VIRTGPU_CONTEXT_PARAM_POLL_RINGS_MASK:
            case LX_VIRTGPU_CONTEXT_PARAM_DEBUG_NAME:
                break;
            default:
                return -LX_EINVAL;
        }
    }
    if ((Capset != LX_VIRTGPU_CAPSET_VIRGL) && (Capset != LX_VIRTGPU_CAPSET_VIRGL2) &&
        ((Capset != LX_VIRTGPU_CAPSET_VENUS) || !LxDrmHasVenus(Drm->Channel)))
    {
        return -LX_EINVAL;
    }
    return LxDrmCreateContext(Drm, Capset, NumRings, TRUE);
}

static
LONG
LxDrmFetchCaps(
    _In_ PLX_GPU_CHANNEL Channel,
    _In_ ULONG Capset)
{
    PUCHAR *Slot = (Capset == LX_VIRTGPU_CAPSET_VENUS) ? &Channel->VenusCaps : &Channel->Caps;
    PULONG SlotLength = (Capset == LX_VIRTGPU_CAPSET_VENUS) ? &Channel->VenusCapsLength : &Channel->CapsLength;
    PLX_GPU_REQUEST Request;
    PUCHAR Caps;
    LONG Error;

    if (ReadPointerNoFence((PVOID *)Slot) != NULL)
        return 0;
    Request = LxGpuRequestAlloc(LX_GPU_GET_CAPS, sizeof(LX_GPU_CAPS_MSG));
    if (Request == NULL)
        return -LX_ENOMEM;
    ((PLX_GPU_CAPS_MSG)Request->Message)->Capset = Capset;
    Error = LxGpuCall(Channel, Request);
    if ((Error == 0) && (Request->ReplyLength != 0))
    {
        Caps = Request->Reply;
        Request->Reply = NULL;
        *SlotLength = Request->ReplyLength;
        if (InterlockedCompareExchangePointer((PVOID *)Slot, Caps, NULL) != NULL)
            LxFree(Caps);
    }
    else if (Error == 0)
    {
        Error = -LX_EINVAL;
    }
    LxGpuRequestRelease(Request);
    return Error;
}

static
BOOLEAN
LxDrmHasVenus(
    _In_ PLX_GPU_CHANNEL Channel)
{
    if (ReadNoFence(&Channel->VenusState) == 0)
        InterlockedExchange(&Channel->VenusState, (LxDrmFetchCaps(Channel, LX_VIRTGPU_CAPSET_VENUS) == 0) ? 1 : -1);
    return ReadNoFence(&Channel->VenusState) > 0;
}

static
LONG64
LxDrmResourceCreate(
    _In_ PLX_DRM_FILE Drm,
    _In_ ULONG64 Argument)
{
    ULONG Args[14];
    PLX_GPU_REQUEST Request;
    PLX_GPU_RES_MSG Message;
    PLX_DRM_BO Bo;
    ULONG Handle;
    ULONG64 Size;
    LONG Error;

    if (LxCopyFromUser(Args, Argument, sizeof(Args)) != 0)
        return -LX_EFAULT;
    Size = LX_ALIGN_UP((ULONG64)((Args[12] != 0) ? Args[12] : LX_PAGE_SIZE), LX_PAGE_SIZE);
    Bo = LxAlloc(sizeof(*Bo));
    if (Bo == NULL)
        return -LX_ENOMEM;
    RtlZeroMemory(Bo, sizeof(*Bo));
    if (!NT_SUCCESS(LxIpcCreateSection(Size, &Bo->Section)))
    {
        LxFree(Bo);
        return -LX_ENOMEM;
    }
    Bo->RefCount = 1;
    Bo->Size = Size;
    Bo->Channel = Drm->Channel;
    Bo->ResId = (ULONG)InterlockedIncrement(&Drm->Channel->NextResId);
    InterlockedIncrement(&Drm->Channel->RefCount);

    Request = LxGpuRequestAlloc(LX_GPU_RES_CREATE, sizeof(LX_GPU_RES_MSG));
    if (Request == NULL)
    {
        ObDereferenceObject(Bo->Section);
        LxGpuChannelRelease(Drm->Channel);
        LxFree(Bo);
        return -LX_ENOMEM;
    }
    Message = (PLX_GPU_RES_MSG)Request->Message;
    Message->ResId = Bo->ResId;
    RtlCopyMemory(&Message->Target, Args, 10 * sizeof(ULONG));
    Message->Size = Size;
    ObReferenceObject(Bo->Section);
    Request->Section = Bo->Section;
    Error = LxGpuCall(Drm->Channel, Request);
    LxGpuRequestRelease(Request);
    if (Error != 0)
    {
        ObDereferenceObject(Bo->Section);
        LxGpuChannelRelease(Drm->Channel);
        LxFree(Bo);
        return Error;
    }
    ExAcquireFastMutex(&Drm->Channel->BoLock);
    InsertTailList(&Drm->Channel->Bos, &Bo->Link);
    ExReleaseFastMutex(&Drm->Channel->BoLock);
    Error = LxDrmHandleAdd(Drm, Bo, &Handle);
    LxDrmBoRelease(Bo);
    if (Error != 0)
        return Error;
    Args[10] = Handle;
    Args[11] = Bo->ResId;
    if (LxCopyToUser(Argument, Args, sizeof(Args)) != 0)
        return -LX_EFAULT;
    return 0;
}

static
LONG64
LxDrmResourceCreateBlob(
    _In_ PLX_DRM_FILE Drm,
    _In_ ULONG64 Argument)
{
    struct
    {
        ULONG BlobMem;
        ULONG BlobFlags;
        ULONG BoHandle;
        ULONG ResHandle;
        ULONG64 Size;
        ULONG Pad;
        ULONG CmdSize;
        ULONG64 Cmd;
        ULONG64 BlobId;
    } Args;
    PLX_GPU_REQUEST Request;
    PLX_GPU_BLOB_MSG Message;
    PLX_DRM_BO Bo;
    ULONG Handle;
    LONG Error;

    if (LxCopyFromUser(&Args, Argument, sizeof(Args)) != 0)
        return -LX_EFAULT;
    /* TODO: guest blob memory and blob creation commands */
    if ((Args.BlobMem != LX_VIRTGPU_BLOB_MEM_HOST3D) || (Args.Size == 0) || (Args.CmdSize != 0) ||
        (Args.BlobFlags & ~LX_VIRTGPU_BLOB_FLAGS))
    {
        return -LX_EINVAL;
    }
    if ((ReadNoFence((PLONG)&Drm->CtxId) == 0) || (Drm->Capset != LX_VIRTGPU_CAPSET_VENUS))
        return -LX_EINVAL;
    Bo = LxAlloc(sizeof(*Bo));
    Request = LxGpuRequestAlloc(LX_GPU_BLOB_CREATE, sizeof(LX_GPU_BLOB_MSG));
    if ((Bo == NULL) || (Request == NULL))
    {
        if (Bo != NULL)
            LxFree(Bo);
        if (Request != NULL)
            LxGpuRequestRelease(Request);
        return -LX_ENOMEM;
    }
    RtlZeroMemory(Bo, sizeof(*Bo));
    Bo->RefCount = 1;
    Bo->Size = LX_ALIGN_UP(Args.Size, LX_PAGE_SIZE);
    Bo->Channel = Drm->Channel;
    Bo->CtxId = Drm->CtxId;
    Bo->BlobMem = Args.BlobMem;
    Bo->ResId = (ULONG)InterlockedIncrement(&Drm->Channel->NextResId);
    Message = (PLX_GPU_BLOB_MSG)Request->Message;
    Message->CtxId = Drm->CtxId;
    Message->ResId = Bo->ResId;
    Message->BlobMem = Args.BlobMem;
    Message->BlobFlags = Args.BlobFlags;
    Message->BlobId = Args.BlobId;
    Message->Size = Args.Size;
    Error = LxGpuCall(Drm->Channel, Request);
    if ((Error == 0) && (Request->Section == NULL))
        Error = -LX_EINVAL;
    if (Error != 0)
    {
        LxGpuRequestRelease(Request);
        LxFree(Bo);
        return Error;
    }
    Bo->Section = Request->Section;
    Request->Section = NULL;
    LxGpuRequestRelease(Request);
    InterlockedIncrement(&Drm->Channel->RefCount);
    ExAcquireFastMutex(&Drm->Channel->BoLock);
    InsertTailList(&Drm->Channel->Bos, &Bo->Link);
    ExReleaseFastMutex(&Drm->Channel->BoLock);
    Error = LxDrmHandleAdd(Drm, Bo, &Handle);
    LxDrmBoRelease(Bo);
    if (Error != 0)
        return Error;
    Args.BoHandle = Handle;
    Args.ResHandle = Bo->ResId;
    return (LxCopyToUser(Argument, &Args, sizeof(Args)) != 0) ? -LX_EFAULT : 0;
}

static
LONG
LxDrmCollectBos(
    _In_ PLX_DRM_FILE Drm,
    _In_ ULONG64 UserHandles,
    _In_ ULONG Count,
    _Out_writes_(Count) PLX_DRM_BO *Bos,
    _Out_writes_(Count) PULONG ResIds)
{
    PULONG Handles;
    ULONG Index;
    LONG Error = 0;

    RtlZeroMemory(Bos, Count * sizeof(PLX_DRM_BO));
    if (Count == 0)
        return 0;
    Handles = LxAlloc(Count * sizeof(ULONG));
    if (Handles == NULL)
        return -LX_ENOMEM;
    if (LxCopyFromUser(Handles, UserHandles, Count * sizeof(ULONG)) != 0)
        Error = -LX_EFAULT;
    for (Index = 0; (Error == 0) && (Index < Count); Index++)
    {
        Bos[Index] = LxDrmBoLookup(Drm, Handles[Index]);
        if (Bos[Index] == NULL)
            Error = -LX_ENOENT;
        else
            ResIds[Index] = Bos[Index]->ResId;
    }
    LxFree(Handles);
    return Error;
}

static
BOOLEAN
LxGpuFenceSignaled(
    _In_ PVOID Context)
{
    return ReadNoFence(&((PLX_GPU_FENCE)Context)->Signaled) != 0;
}

static
LONG
LxDrmWaitInFence(
    _In_ PLX_DRM_FILE Drm,
    _In_ LONG Fd,
    _In_ BOOLEAN Global,
    _In_ ULONG RingIdx)
{
    PLX_FILE File = LxFdGet(LxCurrentThread()->Process, Fd);
    PLX_GPU_FENCE Fence;
    LONG Error = 0;

    if (File == NULL)
        return -LX_EINVAL;
    if (File->Ops != &LxGpuFenceOps)
    {
        LxFileDereference(File);
        return -LX_EINVAL;
    }
    Fence = File->Object;
    if ((Fence->Channel != Drm->Channel) || (Fence->Global != Global) ||
        (!Global && ((Fence->CtxId != Drm->CtxId) || (Fence->RingIdx != RingIdx))))
    {
        while ((Error == 0) && !LxGpuFenceSignaled(Fence))
            Error = (LONG)LxWaitHead(&Fence->Poll, LxGpuFenceSignaled, Fence, NULL);
    }
    LxFileDereference(File);
    return Error;
}

static
LONG64
LxDrmExecbuffer(
    _In_ PLX_DRM_FILE Drm,
    _In_ ULONG64 Argument)
{
    LX_DRM_EXECBUFFER_HEAD Args;
    PLX_GPU_REQUEST Request;
    PLX_GPU_SUBMIT_MSG Message;
    PLX_GPU_FENCE Fence = NULL;
    PLX_FILE FenceFile = NULL;
    PLX_DRM_BO *Bos;
    PULONG ResIds;
    ULONG Index, RingIdx = 0;
    BOOLEAN Venus;
    LONG Error, Fd = -1;

    if (LxCopyFromUser(&Args, Argument, sizeof(Args)) != 0)
        return -LX_EFAULT;
    /* TODO: syncobjs */
    if (Args.Flags & ~(LX_VIRTGPU_EXECBUF_FENCE_FD_IN | LX_VIRTGPU_EXECBUF_FENCE_FD_OUT | LX_VIRTGPU_EXECBUF_RING_IDX))
        return -LX_EINVAL;
    Error = LxDrmEnsureContext(Drm);
    if (Error != 0)
        return Error;
    Venus = (Drm->Capset == LX_VIRTGPU_CAPSET_VENUS);
    if (((Args.Size == 0) && !Venus) || (Args.Size > LX_DRM_MAX_COMMAND) || (Args.Size & 3) ||
        (Args.NumBoHandles > LX_DRM_MAX_BO_HANDLES))
    {
        return -LX_EINVAL;
    }
    if (Args.Flags & LX_VIRTGPU_EXECBUF_RING_IDX)
    {
        RingIdx = Args.Rest[0];
        if (RingIdx >= max(Drm->NumRings, 1))
            return -LX_EINVAL;
    }
    if (Args.Flags & LX_VIRTGPU_EXECBUF_FENCE_FD_IN)
    {
        Error = LxDrmWaitInFence(Drm, Args.FenceFd, !Venus, RingIdx);
        if (Error != 0)
            return Error;
    }
    if (Args.Flags & LX_VIRTGPU_EXECBUF_FENCE_FD_OUT)
    {
        FenceFile = LxAnonFileCreate(&LxGpuFenceOps, LX_O_RDONLY, "sync_file", sizeof(LX_GPU_FENCE));
        if (FenceFile == NULL)
            return -LX_ENOMEM;
        Fence = FenceFile->Object;
        RtlZeroMemory(Fence, sizeof(*Fence));
        LxPollHeadInit(&Fence->Poll);
        Fence->Channel = Drm->Channel;
        Fence->CtxId = Drm->CtxId;
        Fence->RingIdx = RingIdx;
        Fence->Global = !Venus;
        InterlockedIncrement(&Drm->Channel->RefCount);
    }
    Request = LxGpuRequestAlloc(LX_GPU_SUBMIT, sizeof(LX_GPU_SUBMIT_MSG) + Args.NumBoHandles * sizeof(ULONG) + Args.Size);
    Bos = LxAlloc(max(Args.NumBoHandles, 1) * sizeof(PLX_DRM_BO));
    if ((Request == NULL) || (Bos == NULL))
    {
        if (Request != NULL)
            LxGpuRequestRelease(Request);
        if (Bos != NULL)
            LxFree(Bos);
        if (FenceFile != NULL)
            LxFileDereference(FenceFile);
        return -LX_ENOMEM;
    }
    Message = (PLX_GPU_SUBMIT_MSG)Request->Message;
    Message->CtxId = Drm->CtxId;
    Message->Length = Args.Size;
    Message->ResCount = Args.NumBoHandles;
    Message->RingIdx = RingIdx;
    ResIds = (PULONG)(Message + 1);
    Error = LxDrmCollectBos(Drm, Args.BoHandles, Args.NumBoHandles, Bos, ResIds);
    if ((Error == 0) && (Args.Size != 0) &&
        (LxCopyFromUser(ResIds + Args.NumBoHandles, Args.Command, Args.Size) != 0))
    {
        Error = -LX_EFAULT;
    }
    if ((Error == 0) && (FenceFile != NULL))
    {
        Fd = LxFdInstall(LxCurrentThread()->Process, FenceFile, 0, TRUE);
        if (Fd < 0)
            Error = Fd;
    }
    if (Error == 0)
        Error = LxGpuSendFenced(Drm->Channel, Request, Fence);
    for (Index = 0; Index < Args.NumBoHandles; Index++)
    {
        if (Bos[Index] == NULL)
            continue;
        if ((Error == 0) && !Venus)
            InterlockedExchange(&Bos[Index]->Busy, (LONG)Request->Message->Serial);
        LxDrmBoRelease(Bos[Index]);
    }
    LxFree(Bos);
    LxGpuRequestRelease(Request);
    if (FenceFile != NULL)
    {
        if ((Error != 0) && (Fd >= 0))
            LxFdClose(LxCurrentThread()->Process, Fd);
        LxFileDereference(FenceFile);
        if (Error == 0)
        {
            Args.FenceFd = Fd;
            if (LxCopyToUser(Argument, &Args, FIELD_OFFSET(LX_DRM_EXECBUFFER_HEAD, Rest)) != 0)
                Error = -LX_EFAULT;
        }
    }
    return Error;
}

static
LONG64
LxDrmTransfer(
    _In_ PLX_DRM_FILE Drm,
    _In_ ULONG64 Argument,
    _In_ BOOLEAN ToHost)
{
    ULONG Args[11];
    PLX_GPU_REQUEST Request;
    PLX_GPU_TRANSFER_MSG Message;
    PLX_DRM_BO Bo;
    LONG Error;

    if (LxCopyFromUser(Args, Argument, sizeof(Args)) != 0)
        return -LX_EFAULT;
    Bo = LxDrmBoLookup(Drm, Args[0]);
    if (Bo == NULL)
        return -LX_ENOENT;
    Error = LxDrmEnsureContext(Drm);
    Request = (Error == 0) ? LxGpuRequestAlloc(LX_GPU_TRANSFER, sizeof(LX_GPU_TRANSFER_MSG)) : NULL;
    if ((Error == 0) && (Request == NULL))
        Error = -LX_ENOMEM;
    if (Error == 0)
    {
        Message = (PLX_GPU_TRANSFER_MSG)Request->Message;
        Message->CtxId = Drm->CtxId;
        Message->ResId = Bo->ResId;
        Message->ToHost = ToHost;
        RtlCopyMemory(Message->Box, &Args[1], sizeof(Message->Box));
        Message->Level = Args[7];
        Message->Offset = Args[8];
        Message->Stride = Args[9];
        Message->LayerStride = Args[10];
        Error = LxGpuSend(Drm->Channel, Request);
        if (Error == 0)
            InterlockedExchange(&Bo->Busy, (LONG)Request->Message->Serial);
        LxGpuRequestRelease(Request);
    }
    LxDrmBoRelease(Bo);
    return Error;
}

static
LONG64
LxDmabufIoctl(
    _In_ PLX_FILE File,
    _In_ ULONG Request,
    _In_ ULONG64 Argument)
{
    UNREFERENCED_PARAMETER(File);
    UNREFERENCED_PARAMETER(Argument);
    return (Request == LX_DMA_BUF_IOCTL_SYNC) ? 0 : -LX_ENOTTY;
}

static
LONG
LxDmabufStat(
    _In_ PLX_FILE File,
    _Out_ PLX_STAT Stat)
{
    PLX_DRM_BO Bo = File->Object;

    RtlZeroMemory(Stat, sizeof(*Stat));
    Stat->Mode = LX_S_IFREG | 0600;
    Stat->Ino = Bo->ResId;
    Stat->Size = (LONG64)Bo->Size;
    Stat->Nlink = 1;
    return 0;
}

static
LONG64
LxDmabufSeek(
    _In_ PLX_FILE File,
    _In_ LONG64 Offset,
    _In_ ULONG Whence)
{
    PLX_DRM_BO Bo = File->Object;

    if (Offset != 0)
        return -LX_EINVAL;
    if (Whence == LX_SEEK_END)
        return (LONG64)Bo->Size;
    return (Whence == LX_SEEK_SET) ? 0 : -LX_EINVAL;
}

static
VOID
LxDmabufRelease(
    _In_ PLX_FILE File)
{
    PLX_DRM_BO Bo = File->Object;

    File->Object = NULL;
    if (Bo != NULL)
        LxDrmBoRelease(Bo);
}

static const LX_FILE_OPS LxDmabufOps =
{
    NULL, NULL, LxDmabufIoctl, NULL, LxDmabufStat, LxDmabufSeek, NULL, NULL, LxDmabufRelease
};

static
LONG64
LxDrmPrimeExport(
    _In_ PLX_DRM_FILE Drm,
    _In_ ULONG64 Argument)
{
    ULONG Prime[3];
    PLX_DRM_BO Bo;
    PLX_FILE File;
    LONG Fd;

    if (LxCopyFromUser(Prime, Argument, sizeof(Prime)) != 0)
        return -LX_EFAULT;
    if ((Prime[1] & ~(LX_DRM_CLOEXEC | LX_DRM_RDWR | LX_O_ACCMODE)) != 0)
        return -LX_EINVAL;
    Bo = LxDrmBoLookup(Drm, Prime[0]);
    if (Bo == NULL)
        return -LX_ENOENT;
    File = LxFileAlloc(&LxDmabufOps, (Prime[1] & LX_DRM_RDWR) ? LX_O_RDWR : LX_O_RDONLY, 0);
    if (File == NULL)
    {
        LxDrmBoRelease(Bo);
        return -LX_ENOMEM;
    }
    File->Path = LxStrDup("/dmabuf:");
    File->Object = Bo;
    Fd = LxFdInstall(LxCurrentThread()->Process, File, 0, (Prime[1] & LX_DRM_CLOEXEC) != 0);
    LxFileDereference(File);
    if (Fd < 0)
        return Fd;
    Prime[2] = (ULONG)Fd;
    return (LxCopyToUser(Argument, Prime, sizeof(Prime)) != 0) ? -LX_EFAULT : 0;
}

static
LONG64
LxDrmPrimeImport(
    _In_ PLX_DRM_FILE Drm,
    _In_ ULONG64 Argument)
{
    ULONG Prime[3], Index;
    PLX_FILE File;
    PLX_DRM_BO Bo;
    LONG Error = 0;

    if (LxCopyFromUser(Prime, Argument, sizeof(Prime)) != 0)
        return -LX_EFAULT;
    File = LxFdGet(LxCurrentThread()->Process, (LONG)Prime[2]);
    if (File == NULL)
        return -LX_EBADF;
    Bo = (File->Ops == &LxDmabufOps) ? File->Object : NULL;
    if ((Bo == NULL) || (Bo->Channel != Drm->Channel))
    {
        LxFileDereference(File);
        return -LX_EINVAL;
    }
    ExAcquireFastMutex(&Drm->Lock);
    for (Index = 0; Index < Drm->Capacity; Index++)
    {
        if (Drm->Handles[Index] == Bo)
            break;
    }
    ExReleaseFastMutex(&Drm->Lock);
    if (Index < Drm->Capacity)
        Prime[0] = Index + 1;
    else
        Error = LxDrmHandleAdd(Drm, Bo, &Prime[0]);
    LxFileDereference(File);
    if (Error != 0)
        return Error;
    return (LxCopyToUser(Argument, Prime, sizeof(Prime)) != 0) ? -LX_EFAULT : 0;
}

static
LONG64
LxDrmIoctl(
    _In_ PLX_FILE File,
    _In_ ULONG Request,
    _In_ ULONG64 Argument)
{
    PLX_DRM_FILE Drm = File->Object;
    ULONG64 Pair[2];
    ULONG Small[4];
    PLX_DRM_BO Bo;
    BOOLEAN Current;
    LONG Error;
    KIRQL Irql;

    KeAcquireSpinLock(&Drm->Channel->Lock, &Irql);
    Current = Drm->Channel->HostAttached && (Drm->Channel->Generation == Drm->Generation);
    KeReleaseSpinLock(&Drm->Channel->Lock, Irql);
    if (!Current)
        return -LX_ENODEV;

    switch (Request)
    {
        case LX_DRM_IOCTL_VERSION:
        {
            static const char Name[] = "virtio_gpu", Date[] = "0", Desc[] = "virtio GPU";
            struct
            {
                LONG Major, Minor, Patch, Pad;
                ULONG64 NameLength, Name, DateLength, Date, DescLength, Desc;
            } Version;

            if (LxCopyFromUser(&Version, Argument, sizeof(Version)) != 0)
                return -LX_EFAULT;
            if ((Version.Name != 0) &&
                (LxCopyToUser(Version.Name, Name, min(Version.NameLength, sizeof(Name) - 1)) != 0))
            {
                return -LX_EFAULT;
            }
            if ((Version.Date != 0) &&
                (LxCopyToUser(Version.Date, Date, min(Version.DateLength, sizeof(Date) - 1)) != 0))
            {
                return -LX_EFAULT;
            }
            if ((Version.Desc != 0) &&
                (LxCopyToUser(Version.Desc, Desc, min(Version.DescLength, sizeof(Desc) - 1)) != 0))
            {
                return -LX_EFAULT;
            }
            Version.Major = 0;
            Version.Minor = 1;
            Version.Patch = 0;
            Version.NameLength = sizeof(Name) - 1;
            Version.DateLength = sizeof(Date) - 1;
            Version.DescLength = sizeof(Desc) - 1;
            return (LxCopyToUser(Argument, &Version, sizeof(Version)) != 0) ? -LX_EFAULT : 0;
        }

        case LX_DRM_IOCTL_GET_CAP:
            if (LxCopyFromUser(Pair, Argument, sizeof(Pair)) != 0)
                return -LX_EFAULT;
            Pair[1] = (Pair[0] == LX_DRM_CAP_PRIME) ? (LX_DRM_PRIME_CAP_IMPORT | LX_DRM_PRIME_CAP_EXPORT) : 0;
            return (LxCopyToUser(Argument, Pair, sizeof(Pair)) != 0) ? -LX_EFAULT : 0;

        case LX_DRM_IOCTL_SET_CLIENT_CAP:
            return -LX_EINVAL;

        case LX_DRM_IOCTL_GEM_CLOSE:
            if (LxCopyFromUser(Small, Argument, 2 * sizeof(ULONG)) != 0)
                return -LX_EFAULT;
            ExAcquireFastMutex(&Drm->Lock);
            Bo = ((Small[0] != 0) && (Small[0] <= Drm->Capacity)) ? Drm->Handles[Small[0] - 1] : NULL;
            if (Bo != NULL)
                Drm->Handles[Small[0] - 1] = NULL;
            ExReleaseFastMutex(&Drm->Lock);
            if (Bo == NULL)
                return -LX_EINVAL;
            LxDrmBoRelease(Bo);
            return 0;

        case LX_DRM_IOCTL_PRIME_HANDLE_TO_FD:
            return LxDrmPrimeExport(Drm, Argument);

        case LX_DRM_IOCTL_PRIME_FD_TO_HANDLE:
            return LxDrmPrimeImport(Drm, Argument);

        case LX_DRM_IOCTL_VIRTGPU_GETPARAM:
        {
            LONG Value;

            if (LxCopyFromUser(Pair, Argument, sizeof(Pair)) != 0)
                return -LX_EFAULT;
            if ((Pair[0] == 0) || (Pair[0] > LX_VIRTGPU_PARAM_MAX))
                return -LX_EINVAL;
            switch (Pair[0])
            {
                case LX_VIRTGPU_PARAM_3D_FEATURES:
                case LX_VIRTGPU_PARAM_CAPSET_QUERY_FIX:
                    Value = 1;
                    break;
                case LX_VIRTGPU_PARAM_RESOURCE_BLOB:
                case LX_VIRTGPU_PARAM_HOST_VISIBLE:
                case LX_VIRTGPU_PARAM_CONTEXT_INIT:
                    Value = LxDrmHasVenus(Drm->Channel) ? 1 : 0;
                    break;
                case LX_VIRTGPU_PARAM_SUPPORTED_CAPSET_IDS:
                    Value = (1 << LX_VIRTGPU_CAPSET_VIRGL) | (1 << LX_VIRTGPU_CAPSET_VIRGL2) |
                            (LxDrmHasVenus(Drm->Channel) ? (1 << LX_VIRTGPU_CAPSET_VENUS) : 0);
                    break;
                default:
                    Value = 0;
                    break;
            }
            return (LxCopyToUser(Pair[1], &Value, sizeof(Value)) != 0) ? -LX_EFAULT : 0;
        }

        case LX_DRM_IOCTL_VIRTGPU_GET_CAPS:
        {
            struct
            {
                ULONG CapSetId;
                ULONG CapSetVer;
                ULONG64 Address;
                ULONG Size;
                ULONG Pad;
            } Caps;

            if (LxCopyFromUser(&Caps, Argument, sizeof(Caps)) != 0)
                return -LX_EFAULT;
            if (Caps.CapSetId == LX_VIRTGPU_CAPSET_VENUS)
            {
                if (!LxDrmHasVenus(Drm->Channel))
                    return -LX_EINVAL;
                return (LxCopyToUser(Caps.Address, Drm->Channel->VenusCaps,
                                     min(Caps.Size, Drm->Channel->VenusCapsLength)) != 0) ? -LX_EFAULT : 0;
            }
            if ((Caps.CapSetId != LX_VIRTGPU_CAPSET_VIRGL) && (Caps.CapSetId != LX_VIRTGPU_CAPSET_VIRGL2))
                return -LX_EINVAL;
            Error = LxDrmFetchCaps(Drm->Channel, LX_VIRTGPU_CAPSET_VIRGL2);
            if (Error != 0)
                return Error;
            return (LxCopyToUser(Caps.Address, Drm->Channel->Caps, min(Caps.Size, Drm->Channel->CapsLength)) != 0) ?
                   -LX_EFAULT : 0;
        }

        case LX_DRM_IOCTL_VIRTGPU_RESOURCE_CREATE:
            return LxDrmResourceCreate(Drm, Argument);

        case LX_DRM_IOCTL_VIRTGPU_RESOURCE_CREATE_BLOB:
            return LxDrmResourceCreateBlob(Drm, Argument);

        case LX_DRM_IOCTL_VIRTGPU_CONTEXT_INIT:
            return LxDrmContextInit(Drm, Argument);

        case LX_DRM_IOCTL_VIRTGPU_RESOURCE_INFO:
            if (LxCopyFromUser(Small, Argument, sizeof(Small)) != 0)
                return -LX_EFAULT;
            Bo = LxDrmBoLookup(Drm, Small[0]);
            if (Bo == NULL)
                return -LX_ENOENT;
            Small[1] = Bo->ResId;
            Small[2] = (ULONG)Bo->Size;
            Small[3] = Bo->BlobMem;
            LxDrmBoRelease(Bo);
            return (LxCopyToUser(Argument, Small, sizeof(Small)) != 0) ? -LX_EFAULT : 0;

        case LX_DRM_IOCTL_VIRTGPU_MAP:
            if (LxCopyFromUser(Pair, Argument, sizeof(Pair)) != 0)
                return -LX_EFAULT;
            Bo = LxDrmBoLookup(Drm, (ULONG)Pair[1]);
            if (Bo == NULL)
                return -LX_ENOENT;
            Pair[0] = (ULONG64)Bo->ResId << LX_DRM_MAP_SHIFT;
            LxDrmBoRelease(Bo);
            return (LxCopyToUser(Argument, Pair, sizeof(Pair)) != 0) ? -LX_EFAULT : 0;

        case LX_DRM_IOCTL_VIRTGPU_EXECBUFFER:
            return LxDrmExecbuffer(Drm, Argument);

        case LX_DRM_IOCTL_VIRTGPU_TO_HOST:
        case LX_DRM_IOCTL_VIRTGPU_FROM_HOST:
            return LxDrmTransfer(Drm, Argument, Request == LX_DRM_IOCTL_VIRTGPU_TO_HOST);

        case LX_DRM_IOCTL_VIRTGPU_WAIT:
        {
            BOOLEAN Done;

            if (LxCopyFromUser(Small, Argument, 2 * sizeof(ULONG)) != 0)
                return -LX_EFAULT;
            Bo = LxDrmBoLookup(Drm, Small[0]);
            if (Bo == NULL)
                return -LX_ENOENT;
            Done = LxGpuWaitDone(Drm->Channel, ReadNoFence(&Bo->Busy), (Small[1] & LX_VIRTGPU_WAIT_NOWAIT) != 0);
            LxDrmBoRelease(Bo);
            return Done ? 0 : -LX_EBUSY;
        }
    }
    return -LX_ENOTTY;
}

static
LONG
LxDrmStat(
    _In_ PLX_FILE File,
    _Out_ PLX_STAT Stat)
{
    RtlZeroMemory(Stat, sizeof(*Stat));
    Stat->Mode = LX_S_IFCHR | 0666;
    Stat->Rdev = File->Rdev;
    Stat->Nlink = 1;
    return 0;
}

static
VOID
LxDrmRelease(
    _In_ PLX_FILE File)
{
    PLX_DRM_FILE Drm = File->Object;
    PLX_GPU_REQUEST Request;
    ULONG Index;

    if (Drm == NULL)
        return;
    File->Object = NULL;
    for (Index = 0; Index < Drm->Capacity; Index++)
    {
        if (Drm->Handles[Index] != NULL)
            LxDrmBoRelease(Drm->Handles[Index]);
    }
    if (Drm->Handles != NULL)
        LxFree(Drm->Handles);
    if ((Drm->CtxId != 0) && ((Request = LxGpuRequestAlloc(LX_GPU_CTX_DESTROY, sizeof(LX_GPU_CTX_MSG))) != NULL))
    {
        ((PLX_GPU_CTX_MSG)Request->Message)->CtxId = Drm->CtxId;
        LxGpuSend(Drm->Channel, Request);
        LxGpuRequestRelease(Request);
    }
    LxGpuChannelRelease(Drm->Channel);
    LxFree(Drm);
}

static const LX_FILE_OPS LxDrmOps =
{
    NULL, NULL, LxDrmIoctl, NULL, LxDrmStat, NULL, NULL, NULL, LxDrmRelease
};

BOOLEAN
LxDrmIsFile(
    _In_ PLX_FILE File)
{
    return (File->Ops == &LxDrmOps) || (File->Ops == &LxDmabufOps);
}

PLX_FILE
LxDrmOpen(
    _In_ PLX_PROCESS Process,
    _In_ ULONG Minor,
    _In_ ULONG Flags,
    _In_ PCSTR Path)
{
    PLX_GPU_CHANNEL Channel;
    PLX_DRM_FILE Drm;
    PLX_FILE File;
    BOOLEAN Attached;
    ULONG Generation;
    KIRQL Irql;

    if ((Minor != 0) && (Minor != 128))
        return NULL;
    Channel = LxGpuChannelGet(&Process->Instance->RootNt);
    if (Channel == NULL)
        return NULL;
    KeAcquireSpinLock(&Channel->Lock, &Irql);
    Attached = Channel->HostAttached;
    Generation = Channel->Generation;
    KeReleaseSpinLock(&Channel->Lock, Irql);
    Drm = Attached ? LxAlloc(sizeof(*Drm)) : NULL;
    File = (Drm != NULL) ? LxFileAlloc(&LxDrmOps, Flags & (LX_O_ACCMODE | LX_O_NONBLOCK), LX_S_IFCHR) : NULL;
    if (File == NULL)
    {
        if (Drm != NULL)
            LxFree(Drm);
        LxGpuChannelRelease(Channel);
        return NULL;
    }
    RtlZeroMemory(Drm, sizeof(*Drm));
    Drm->Channel = Channel;
    Drm->Generation = Generation;
    ExInitializeFastMutex(&Drm->Lock);
    File->Object = Drm;
    File->Path = LxStrDup(Path);
    File->Rdev = ((ULONG64)LX_DRM_MAJOR << 8) | Minor;
    return File;
}

LONG64
LxDrmMmap(
    _In_ PLX_PROCESS Process,
    _In_ PLX_FILE File,
    _In_ ULONG64 Address,
    _In_ ULONG64 Length,
    _In_ ULONG Prot,
    _In_ ULONG Flags,
    _In_ ULONG64 Offset)
{
    PLX_DRM_FILE Drm = File->Object;
    PLX_DRM_BO Bo = NULL;
    PLIST_ENTRY Entry;
    LONG64 Result;

    if (File->Ops == &LxDmabufOps)
    {
        Bo = File->Object;
        /* FIXME: dmabuf mappings at a non-zero offset are refused */
        if ((Offset != 0) || (Length == 0) || (Length > Bo->Size))
            return -LX_EINVAL;
        return LxMmMapSection(Process, Bo->Section, (Flags & LX_MAP_FIXED) ? Address : 0, Length, Prot,
                              (Flags & LX_MAP_FIXED) != 0);
    }
    ExAcquireFastMutex(&Drm->Channel->BoLock);
    for (Entry = Drm->Channel->Bos.Flink; Entry != &Drm->Channel->Bos; Entry = Entry->Flink)
    {
        PLX_DRM_BO Candidate = CONTAINING_RECORD(Entry, LX_DRM_BO, Link);

        if (((ULONG64)Candidate->ResId << LX_DRM_MAP_SHIFT) == Offset)
        {
            Bo = Candidate;
            InterlockedIncrement(&Bo->RefCount);
            break;
        }
    }
    ExReleaseFastMutex(&Drm->Channel->BoLock);
    if (Bo == NULL)
        return -LX_EINVAL;
    if ((Length == 0) || (Length > Bo->Size))
    {
        LxDrmBoRelease(Bo);
        return -LX_EINVAL;
    }
    Result = LxMmMapSection(Process, Bo->Section, (Flags & LX_MAP_FIXED) ? Address : 0, Length, Prot,
                            (Flags & LX_MAP_FIXED) != 0);
    LxDrmBoRelease(Bo);
    return Result;
}

NTSTATUS
LxGpuHostCreate(
    _Inout_ PFILE_OBJECT FileObject)
{
    PLX_GPU_HOST Host = LxAlloc(sizeof(*Host));

    if (Host == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;
    Host->Channel = NULL;
    FileObject->FsContext2 = Host;
    return STATUS_SUCCESS;
}

VOID
LxGpuHostCleanup(
    _Inout_ PFILE_OBJECT FileObject)
{
    PLX_GPU_HOST Host = FileObject->FsContext2;
    PLX_GPU_CHANNEL Channel = (Host != NULL) ? Host->Channel : NULL;
    LIST_ENTRY Purge;
    KIRQL Irql;
    PIRP Irp;

    if (Channel == NULL)
        return;
    Host->Channel = NULL;
    InitializeListHead(&Purge);
    KeAcquireSpinLock(&Channel->Lock, &Irql);
    Channel->HostAttached = FALSE;
    while (!IsListEmpty(&Channel->ToHost))
        InsertTailList(&Purge, RemoveHeadList(&Channel->ToHost));
    Channel->QueuedBytes = 0;
    KeSetEvent(&Channel->DrainEvent, IO_NO_INCREMENT, FALSE);
    while (!IsListEmpty(&Channel->Waiting))
        InsertTailList(&Purge, RemoveHeadList(&Channel->Waiting));
    while ((Irp = LxGpuTakePending(Channel)) != NULL)
    {
        KeReleaseSpinLock(&Channel->Lock, Irql);
        Irp->IoStatus.Status = STATUS_CANCELLED;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        KeAcquireSpinLock(&Channel->Lock, &Irql);
    }
    InterlockedExchange(&Channel->Done, (LONG)Channel->NextSerial);
    KeReleaseSpinLock(&Channel->Lock, Irql);
    KeSetEvent(&Channel->DoneEvent, IO_NO_INCREMENT, FALSE);
    LxGpuSignalFences(Channel, TRUE, FALSE, 0, 0, 0);
    while (!IsListEmpty(&Purge))
    {
        PLX_GPU_REQUEST Request = CONTAINING_RECORD(RemoveHeadList(&Purge), LX_GPU_REQUEST, Link);

        Request->Status = -LX_ENODEV;
        KeSetEvent(&Request->Done, IO_NO_INCREMENT, FALSE);
        LxGpuRequestRelease(Request);
    }
    LxGpuChannelRelease(Channel);
}

VOID
LxGpuHostClose(
    _Inout_ PFILE_OBJECT FileObject)
{
    PLX_GPU_HOST Host = FileObject->FsContext2;

    if (Host == NULL)
        return;
    FileObject->FsContext2 = NULL;
    if (Host->Channel != NULL)
        LxGpuHostCleanup(FileObject);
    LxFree(Host);
}

static
NTSTATUS
LxGpuHostAttach(
    _In_ PLX_GPU_HOST Host,
    _In_ PIRP Irp,
    _In_ PIO_STACK_LOCATION Stack)
{
    ULONG Length = Stack->Parameters.DeviceIoControl.InputBufferLength;
    UNICODE_STRING RootNt;
    PLX_GPU_CHANNEL Channel;
    BOOLEAN Busy;
    KIRQL Irql;

    if (Host->Channel != NULL)
        return STATUS_DEVICE_BUSY;
    if ((Length < 2 * sizeof(WCHAR)) || (Length & 1) || (Length > MAXUSHORT))
        return STATUS_INVALID_PARAMETER;
    RootNt.Buffer = Irp->AssociatedIrp.SystemBuffer;
    RootNt.Length = (USHORT)Length;
    while ((RootNt.Length > 2 * sizeof(WCHAR)) &&
           ((RootNt.Buffer[RootNt.Length / sizeof(WCHAR) - 1] == L'\\') ||
            (RootNt.Buffer[RootNt.Length / sizeof(WCHAR) - 1] == UNICODE_NULL)))
    {
        RootNt.Length -= sizeof(WCHAR);
    }
    RootNt.MaximumLength = RootNt.Length;
    Channel = LxGpuChannelGet(&RootNt);
    if (Channel == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;
    KeAcquireSpinLock(&Channel->Lock, &Irql);
    Busy = Channel->HostAttached;
    if (!Busy)
    {
        Channel->HostAttached = TRUE;
        Channel->Generation++;
    }
    KeReleaseSpinLock(&Channel->Lock, Irql);
    if (Busy)
    {
        LxGpuChannelRelease(Channel);
        return STATUS_DEVICE_BUSY;
    }
    Host->Channel = Channel;
    return STATUS_SUCCESS;
}

static
NTSTATUS
LxGpuHostReceive(
    _In_ PLX_GPU_HOST Host,
    _Inout_ PIRP Irp,
    _In_ PIO_STACK_LOCATION Stack)
{
    PLX_GPU_CHANNEL Channel = Host->Channel;
    ULONG Space = Stack->Parameters.DeviceIoControl.OutputBufferLength;
    PLX_GPU_REQUEST Request = NULL;
    PVOID Buffer;
    HANDLE Handle;
    KIRQL Irql;

    if (Channel == NULL)
        return STATUS_INVALID_DEVICE_STATE;
    KeAcquireSpinLock(&Channel->Lock, &Irql);
    if (!IsListEmpty(&Channel->ToHost))
    {
        Request = CONTAINING_RECORD(Channel->ToHost.Flink, LX_GPU_REQUEST, Link);
        if (Space < Request->Size)
        {
            KeReleaseSpinLock(&Channel->Lock, Irql);
            Irp->IoStatus.Information = Request->Size;
            return STATUS_BUFFER_OVERFLOW;
        }
        RemoveEntryList(&Request->Link);
        Channel->QueuedBytes -= Request->Size;
        KeSetEvent(&Channel->DrainEvent, IO_NO_INCREMENT, FALSE);
        if (Request->Sync)
            InsertTailList(&Channel->Waiting, &Request->Link);
        else
            InitializeListHead(&Request->Link);
    }
    else
    {
        Irp->Tail.Overlay.DriverContext[0] = Channel;
        IoSetCancelRoutine(Irp, LxGpuCancel);
        if (Irp->Cancel && (IoSetCancelRoutine(Irp, NULL) != NULL))
        {
            KeReleaseSpinLock(&Channel->Lock, Irql);
            return STATUS_CANCELLED;
        }
        IoMarkIrpPending(Irp);
        InsertTailList(&Channel->Pending, &Irp->Tail.Overlay.ListEntry);
        KeReleaseSpinLock(&Channel->Lock, Irql);
        return STATUS_PENDING;
    }
    KeReleaseSpinLock(&Channel->Lock, Irql);

    if ((Request->Section != NULL) &&
        NT_SUCCESS(ObOpenObjectByPointer(Request->Section, 0, NULL, SECTION_MAP_READ | SECTION_MAP_WRITE | SECTION_QUERY,
                                         NULL, KernelMode, &Handle)))
    {
        ((PLX_GPU_RES_MSG)Request->Message)->Section = (ULONG64)(ULONG_PTR)Handle;
    }
    Buffer = MmGetSystemAddressForMdlSafe(Irp->MdlAddress, NormalPagePriority);
    if (Buffer != NULL)
        RtlCopyMemory(Buffer, Request->Message, Request->Size);
    Irp->IoStatus.Information = (Buffer != NULL) ? Request->Size : 0;
    if (!Request->Sync)
        LxGpuRequestRelease(Request);
    return (Buffer != NULL) ? STATUS_SUCCESS : STATUS_INSUFFICIENT_RESOURCES;
}

static
NTSTATUS
LxGpuHostSend(
    _In_ PLX_GPU_HOST Host,
    _In_ PIRP Irp,
    _In_ PIO_STACK_LOCATION Stack)
{
    PLX_GPU_CHANNEL Channel = Host->Channel;
    ULONG Length = Stack->Parameters.DeviceIoControl.InputBufferLength;
    PLX_GPU_HEADER Header = Irp->AssociatedIrp.SystemBuffer;
    PLX_GPU_REQUEST Request = NULL;
    PLIST_ENTRY Entry;
    KIRQL Irql;

    if (Channel == NULL)
        return STATUS_INVALID_DEVICE_STATE;
    if ((Length < sizeof(*Header)) || (Length > LX_GPU_MAX_MESSAGE))
        return STATUS_INVALID_PARAMETER;
    if (Header->Type == LX_GPU_DONE)
    {
        if (Length < sizeof(LX_GPU_DONE_MSG))
            return STATUS_INVALID_PARAMETER;
        InterlockedExchange(&Channel->Done, (LONG)((PLX_GPU_DONE_MSG)Header)->Completed);
        KeSetEvent(&Channel->DoneEvent, IO_NO_INCREMENT, FALSE);
        LxGpuSignalFences(Channel, FALSE, TRUE, 0, 0, ((PLX_GPU_DONE_MSG)Header)->Completed);
        return STATUS_SUCCESS;
    }
    if (Header->Type == LX_GPU_RING_DONE)
    {
        PLX_GPU_RING_DONE_MSG Done = (PLX_GPU_RING_DONE_MSG)Header;

        if (Length < sizeof(LX_GPU_RING_DONE_MSG))
            return STATUS_INVALID_PARAMETER;
        LxGpuSignalFences(Channel, FALSE, FALSE, Done->CtxId, Done->RingIdx, (ULONG)Done->FenceId);
        return STATUS_SUCCESS;
    }
    if (Header->Type != LX_GPU_REPLY)
        return STATUS_INVALID_PARAMETER;

    KeAcquireSpinLock(&Channel->Lock, &Irql);
    for (Entry = Channel->Waiting.Flink; Entry != &Channel->Waiting; Entry = Entry->Flink)
    {
        PLX_GPU_REQUEST Candidate = CONTAINING_RECORD(Entry, LX_GPU_REQUEST, Link);

        if (Candidate->Message->Serial == Header->Serial)
        {
            Request = Candidate;
            RemoveEntryList(&Request->Link);
            InitializeListHead(&Request->Link);
            break;
        }
    }
    KeReleaseSpinLock(&Channel->Lock, Irql);
    if (Request == NULL)
        return STATUS_NOT_FOUND;
    Request->Status = Header->Status;
    if ((Request->Message->Type == LX_GPU_BLOB_CREATE) && (Header->Status == 0))
    {
        PVOID Section = NULL;

        if ((Length < sizeof(*Header) + sizeof(LX_GPU_BLOB_REPLY)) ||
            !NT_SUCCESS(ObReferenceObjectByHandle(
                (HANDLE)(ULONG_PTR)((PLX_GPU_BLOB_REPLY)(Header + 1))->Section, SECTION_MAP_READ | SECTION_MAP_WRITE,
                MmSectionObjectType, UserMode, &Section, NULL)))
        {
            Request->Status = -LX_EINVAL;
        }
        else
        {
            Request->Section = Section;
        }
    }
    if (Length > sizeof(*Header))
    {
        Request->Reply = LxAlloc(Length - sizeof(*Header));
        if (Request->Reply != NULL)
        {
            RtlCopyMemory(Request->Reply, Header + 1, Length - sizeof(*Header));
            Request->ReplyLength = Length - sizeof(*Header);
        }
    }
    KeSetEvent(&Request->Done, IO_NO_INCREMENT, FALSE);
    LxGpuRequestRelease(Request);
    return STATUS_SUCCESS;
}

BOOLEAN
LxGpuHostIoctl(
    _In_ PFILE_OBJECT FileObject,
    _Inout_ PIRP Irp,
    _In_ PIO_STACK_LOCATION Stack,
    _Out_ PNTSTATUS Status)
{
    PLX_GPU_HOST Host = FileObject->FsContext2;
    ULONG Code = Stack->Parameters.DeviceIoControl.IoControlCode;

    if ((Code != IOCTL_LX_GPU_ATTACH) && (Code != IOCTL_LX_GPU_RECEIVE) && (Code != IOCTL_LX_GPU_SEND))
        return FALSE;
    if (Host == NULL)
        *Status = STATUS_INVALID_DEVICE_REQUEST;
    else if (Code == IOCTL_LX_GPU_ATTACH)
        *Status = LxGpuHostAttach(Host, Irp, Stack);
    else if (Code == IOCTL_LX_GPU_RECEIVE)
        *Status = LxGpuHostReceive(Host, Irp, Stack);
    else
        *Status = LxGpuHostSend(Host, Irp, Stack);
    if (*Status == STATUS_PENDING)
        return TRUE;
    Irp->IoStatus.Status = *Status;
    if (Code != IOCTL_LX_GPU_RECEIVE)
        Irp->IoStatus.Information = 0;
    else if (!NT_SUCCESS(*Status) && (*Status != STATUS_BUFFER_OVERFLOW))
        Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return TRUE;
}
