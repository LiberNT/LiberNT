/*
 * PROJECT:     LiberNT SpacemiT K1 display miniport
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     K1 display controller cursor and overlay planes
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "softgpu.h"
#include "k1xdpu.h"

static KSERVICE_ROUTINE K1xPlanesInterrupt;
static KDEFERRED_ROUTINE K1xPlanesDpc;
static WORKER_THREAD_ROUTINE K1xRetireWorker;
static KBUGCHECK_CALLBACK_ROUTINE K1xPlanesBugCheck;

static ULONG
K1xLineMemoryUnits(_In_ ULONG Width)
{
    ULONG Bytes = Width * sizeof(ULONG);

    Bytes = (Bytes + K1X_LINE_MEMORY_ALIGNMENT - 1) & ~(K1X_LINE_MEMORY_ALIGNMENT - 1);
    return Bytes / K1X_LINE_MEMORY_UNIT;
}

static VOID
K1xWriteSlot(
    _In_ PK1XDPU_CONTEXT Context,
    _In_ ULONG Slot,
    _In_ ULONG Channel,
    _In_ const RECT *Destination,
    _In_ BOOLEAN PixelAlpha)
{
    PUCHAR Base = Context->Dpu + K1X_COMPOSER_SLOT(Slot);

    K1xWrite(Base, K1X_SLOT_LEFT, (ULONG)Destination->left << K1X_SLOT_LEFT_SHIFT);
    K1xWrite(Base, K1X_SLOT_TOP_RIGHT,
             (ULONG)Destination->top | ((ULONG)(Destination->right - 1) << K1X_SLOT_RIGHT_SHIFT));
    K1xWrite(Base, K1X_SLOT_BOTTOM_BLEND,
             (ULONG)(Destination->bottom - 1) | (PixelAlpha ? K1X_SLOT_PIXEL_ALPHA : 0));
    K1xWrite(Base, K1X_SLOT_ALPHA, K1X_SLOT_ALPHA_OPAQUE);
    K1xWrite(Base, K1X_SLOT_CONTROL, K1X_SLOT_CONTROL_ENABLE | (Channel << K1X_SLOT_CONTROL_CHANNEL_SHIFT));
}

static VOID
K1xWriteChannel(
    _In_ PK1XDPU_CONTEXT Context,
    _In_ ULONG Channel,
    _In_ PHYSICAL_ADDRESS Address,
    _In_ ULONG Pitch,
    _In_ ULONG ImageWidth,
    _In_ ULONG ImageHeight,
    _In_ ULONG SourceX,
    _In_ ULONG SourceY,
    _In_ const RECT *Destination,
    _In_ ULONG Format,
    _In_ ULONG LineMemory)
{
    PUCHAR Base = Context->Dpu + K1X_DPU_CHANNEL(Channel);
    ULONG Width = (ULONG)(Destination->right - Destination->left);
    ULONG Height = (ULONG)(Destination->bottom - Destination->top);

    K1xWrite(Context->Dpu, K1X_DPU_TRANSLATION_CONTROL(Channel), 0);
    K1xWrite(Base, K1X_CHANNEL_CONTROL,
             K1X_CHANNEL_CONTROL_OUTSTANDING | K1X_CHANNEL_CONTROL_COMPOSER2 | K1X_CHANNEL_CONTROL_BURST);
    K1xWrite(Base, K1X_CHANNEL_COMPOSER_Y, (ULONG)Destination->top);
    K1xWrite(Base, K1X_CHANNEL_SCALE_RATIO_V, 0);
    K1xWrite(Base, K1X_CHANNEL_BASE_LOW, Address.LowPart);
    K1xWrite(Base, K1X_CHANNEL_BASE_HIGH, (ULONG)Address.HighPart & K1X_CHANNEL_BASE_HIGH_MASK);
    K1xWrite(Base, K1X_CHANNEL_STRIDE, Pitch);
    K1xWrite(Base, K1X_CHANNEL_IMAGE_SIZE, (ImageHeight << 16) | ImageWidth);
    K1xWrite(Base, K1X_CHANNEL_CROP_START, (SourceY << 16) | SourceX);
    K1xWrite(Base, K1X_CHANNEL_CROP_END, ((SourceY + Height - 1) << 16) | (SourceX + Width - 1));
    K1xWrite(Base, K1X_CHANNEL_ALPHA01, 0);
    K1xWrite(Base, K1X_CHANNEL_ALPHA23, 0);
    K1xWrite(Base, K1X_CHANNEL_LINE_MEMORY, LineMemory);
    K1xWrite(Base, K1X_CHANNEL_FORMAT, Format);
}

static VOID
K1xRetireLocked(
    _Inout_ PK1XDPU_CONTEXT Context,
    _In_opt_ PK1X_FLIP Flip)
{
    if (!Flip)
        return;
    InsertTailList(&Context->RetiredFlips, &Flip->Entry);
    if (InterlockedExchange(&Context->RetireQueued, 1) == 0)
    {
        KeClearEvent(&Context->RetireIdle);
        ExQueueWorkItem(&Context->RetireItem, DelayedWorkQueue);
    }
}

static VOID
K1xLatchLocked(_Inout_ PK1XDPU_CONTEXT Context)
{
    ULONG Raw;

    if (!Context->CommitPending ||
        (K1xRead(Context->Dpu, K1X_DPU_CTL2_COMMIT) & K1X_DPU_CTL2_COMMIT_READY))
    {
        return;
    }
    Context->CommitPending = FALSE;
    KeSetEvent(&Context->LatchEvent, IO_NO_INCREMENT, FALSE);
    if (Context->OverlayCommit == K1xOverlayCommitFlip)
    {
        K1xRetireLocked(Context, Context->OverlayFront);
        Context->OverlayFront = Context->OverlayPending;
        Context->OverlayPending = NULL;
    }
    else if (Context->OverlayCommit == K1xOverlayCommitOff)
    {
        K1xRetireLocked(Context, Context->OverlayFront);
        Context->OverlayFront = NULL;
    }
    Context->OverlayCommit = K1xOverlayCommitNone;

    Raw = K1xRead(Context->Dpu, K1X_DPU_INT_ONLINE2_RAW);
    if (Raw & K1X_DPU_INT_UNDERFLOW)
    {
        K1xWrite(Context->Dpu, K1X_DPU_INT_ONLINE2_STATUS, K1X_DPU_INT_UNDERFLOW);
        if (!Context->UnderflowReported)
        {
            Context->UnderflowReported = TRUE;
            DPRINT1("K1XDPU: display controller underflow, channels 0x%lx\n", Context->Channels);
        }
    }
}

static VOID
K1xApplyLocked(_Inout_ PK1XDPU_CONTEXT Context)
{
    BOOLEAN Changed = FALSE;
    PK1X_FLIP Flip;
    RECT Destination;
    ULONG Value;

    if (Context->CommitPending)
        return;

    if (Context->CursorDirty)
    {
        Context->CursorDirty = FALSE;
        if (Context->CursorVisible)
        {
            K1xWriteChannel(Context, K1X_CURSOR_CHANNEL, Context->CursorPhysical,
                            K1X_CURSOR_SIZE * sizeof(ULONG), K1X_CURSOR_SIZE, K1X_CURSOR_SIZE,
                            Context->CursorSourceX, Context->CursorSourceY, &Context->CursorDestination,
                            K1X_CHANNEL_FORMAT_ARGB8888,
                            K1X_LINE_MEMORY_MAPPED | K1xLineMemoryUnits(K1X_CURSOR_SIZE));
            K1xWriteSlot(Context, K1X_CURSOR_SLOT, K1X_CURSOR_CHANNEL, &Context->CursorDestination, TRUE);
            Context->Channels |= 1UL << K1X_CURSOR_CHANNEL;
            Changed = TRUE;
        }
        else if (Context->Channels & (1UL << K1X_CURSOR_CHANNEL))
        {
            K1xWrite(Context->Dpu + K1X_COMPOSER_SLOT(K1X_CURSOR_SLOT), K1X_SLOT_CONTROL, 0);
            Context->Channels &= ~(1UL << K1X_CURSOR_CHANNEL);
            Changed = TRUE;
        }
    }

    if (Context->OverlayHide)
    {
        Context->OverlayHide = FALSE;
        if (Context->OverlayShown)
        {
            K1xWrite(Context->Dpu + K1X_COMPOSER_SLOT(K1X_OVERLAY_SLOT), K1X_SLOT_CONTROL, 0);
            Context->Channels &= ~(1UL << K1X_OVERLAY_CHANNEL);
            Context->OverlayShown = FALSE;
            Context->OverlayCommit = K1xOverlayCommitOff;
            Changed = TRUE;
        }
    }
    else if (Context->OverlayNext && Context->OverlayNext->Ready)
    {
        ULONG CursorUnits = K1xLineMemoryUnits(K1X_CURSOR_SIZE);

        Flip = Context->OverlayNext;
        Context->OverlayNext = NULL;
        Destination.left = Flip->Left;
        Destination.top = Flip->Top;
        Destination.right = Flip->Left + (LONG)Flip->Width;
        Destination.bottom = Flip->Top + (LONG)Flip->Height;
        K1xWriteChannel(Context, K1X_OVERLAY_CHANNEL, Flip->Buffer->Physical, Flip->Pitch,
                        Flip->Width, Flip->Height, 0, 0, &Destination,
                        K1X_CHANNEL_FORMAT_XRGB8888,
                        (CursorUnits << K1X_LINE_MEMORY_START_SHIFT) |
                            (K1X_LINE_MEMORY_PAIR0_BYTES / K1X_LINE_MEMORY_UNIT - CursorUnits));
        K1xWriteSlot(Context, K1X_OVERLAY_SLOT, K1X_OVERLAY_CHANNEL, &Destination, FALSE);
        Context->Channels |= 1UL << K1X_OVERLAY_CHANNEL;
        Context->OverlayPending = Flip;
        Context->OverlayShown = TRUE;
        Context->OverlayCommit = K1xOverlayCommitFlip;
        Changed = TRUE;
    }

    if (!Changed)
        return;
    Value = K1xRead(Context->Dpu, K1X_DPU_CTL2_CHANNELS) & ~K1X_DPU_CTL2_CHANNEL_MASK;
    K1xWrite(Context->Dpu, K1X_DPU_CTL2_CHANNELS, Value | Context->Channels);
    KeMemoryBarrier();
    K1xWrite(Context->Dpu, K1X_DPU_CTL2_COMMIT, K1X_DPU_CTL2_COMMIT_READY);
    Context->CommitPending = TRUE;
}

VOID
K1xPlanesKick(_Inout_ PK1XDPU_CONTEXT Context)
{
    KIRQL OldIrql;

    if (!Context->PlanesReady)
        return;
    KeAcquireSpinLock(&Context->Lock, &OldIrql);
    K1xLatchLocked(Context);
    K1xApplyLocked(Context);
    KeReleaseSpinLock(&Context->Lock, OldIrql);
}

static BOOLEAN
NTAPI
K1xPlanesInterrupt(
    _In_ PKINTERRUPT Interrupt,
    _In_ PVOID ServiceContext)
{
    PK1XDPU_CONTEXT Context = ServiceContext;
    ULONG Bits;

    UNREFERENCED_PARAMETER(Interrupt);

    Bits = K1xRead(Context->Dpu, K1X_DPU_INT_ONLINE2_RAW) & Context->InterruptMask;
    if (!Bits)
        return FALSE;
    K1xWrite(Context->Dpu, K1X_DPU_INT_ONLINE2_STATUS, Bits);
    InterlockedOr(&Context->InterruptBits, (LONG)Bits);
    KeInsertQueueDpc(&Context->InterruptDpc, NULL, NULL);
    return TRUE;
}

static VOID
NTAPI
K1xPlanesDpc(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    PK1XDPU_CONTEXT Context = DeferredContext;
    ULONG Bits;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    Bits = (ULONG)InterlockedExchange(&Context->InterruptBits, 0);
    KeAcquireSpinLockAtDpcLevel(&Context->Lock);
    if (Bits & K1X_DPU_INT_VSYNC)
        KeSetEvent(&Context->VsyncEvent, IO_NO_INCREMENT, FALSE);
    K1xLatchLocked(Context);
    K1xApplyLocked(Context);
    KeReleaseSpinLockFromDpcLevel(&Context->Lock);
}

static VOID
K1xSetInterruptMaskLocked(_Inout_ PK1XDPU_CONTEXT Context)
{
    ULONG Mask = K1X_DPU_INT_COMMIT_TAKEN;

    if (Context->VsyncWaiters)
        Mask |= K1X_DPU_INT_VSYNC;
    if (Mask == Context->InterruptMask)
        return;
    if (Mask & ~Context->InterruptMask & K1X_DPU_INT_VSYNC)
        K1xWrite(Context->Dpu, K1X_DPU_INT_ONLINE2_STATUS, K1X_DPU_INT_VSYNC);
    Context->InterruptMask = Mask;
    K1xWrite(Context->Dpu, K1X_DPU_INT_ONLINE2_MASK, Mask);
}

BOOLEAN
K1xPlanesWaitVsync(_Inout_ PK1XDPU_CONTEXT Context)
{
    LARGE_INTEGER Timeout;
    NTSTATUS Status;
    KIRQL OldIrql;

    if (!Context->PlanesReady)
        return FALSE;
    KeAcquireSpinLock(&Context->Lock, &OldIrql);
    if (Context->VsyncWaiters++ == 0)
        KeClearEvent(&Context->VsyncEvent);
    K1xSetInterruptMaskLocked(Context);
    KeReleaseSpinLock(&Context->Lock, OldIrql);

    Timeout.QuadPart = -10000LL * K1X_VSYNC_TIMEOUT_MS;
    Status = KeWaitForSingleObject(&Context->VsyncEvent, Executive, KernelMode, FALSE, &Timeout);

    KeAcquireSpinLock(&Context->Lock, &OldIrql);
    Context->VsyncWaiters--;
    K1xSetInterruptMaskLocked(Context);
    KeReleaseSpinLock(&Context->Lock, OldIrql);
    return Status == STATUS_SUCCESS;
}

NTSTATUS
K1xPlanesUpdatePointer(_Inout_ PK1XDPU_CONTEXT Context)
{
    PSOFTGPU_DEVICE Device = Context->Device;
    LONG OriginX, OriginY, Left, Top, Right, Bottom;
    BOOLEAN Visible;
    KIRQL OldIrql;

    if (!Context->PlanesReady || !Context->CursorBuffer)
        return STATUS_DEVICE_NOT_READY;

    if (Device->PointerShapeValid && Context->CursorShapeGeneration != Device->PointerShapeGeneration)
    {
        RtlCopyMemory(Context->CursorBuffer, Device->PointerPixels, K1X_CURSOR_BYTES);
        KeMemoryBarrier();
        Context->CursorShapeGeneration = Device->PointerShapeGeneration;
    }

    OriginX = Device->PointerX - (LONG)Device->PointerHotX;
    OriginY = Device->PointerY - (LONG)Device->PointerHotY;
    Left = max(OriginX, 0);
    Top = max(OriginY, 0);
    Right = min(OriginX + (LONG)K1X_CURSOR_SIZE, (LONG)Device->Width);
    Bottom = min(OriginY + (LONG)K1X_CURSOR_SIZE, (LONG)Device->Height);
    Visible = Device->PointerShapeValid && Device->PointerVisible && Left < Right && Top < Bottom;

    KeAcquireSpinLock(&Context->Lock, &OldIrql);
    Context->CursorVisible = Visible;
    if (Visible)
    {
        Context->CursorDestination.left = Left;
        Context->CursorDestination.top = Top;
        Context->CursorDestination.right = Right;
        Context->CursorDestination.bottom = Bottom;
        Context->CursorSourceX = (ULONG)(Left - OriginX);
        Context->CursorSourceY = (ULONG)(Top - OriginY);
    }
    Context->CursorDirty = TRUE;
    K1xLatchLocked(Context);
    K1xApplyLocked(Context);
    KeReleaseSpinLock(&Context->Lock, OldIrql);
    return STATUS_SUCCESS;
}

VOID
K1xFlipDestroy(_In_ PK1X_FLIP Flip)
{
    PAGED_CODE();

    pvr_glue_fence_end(Flip->Fence, Flip->Wait);
    pvr_glue_fence_put(Flip->Fence);
    K1xScanoutDereference(Flip->Buffer);
    ExFreePoolWithTag(Flip, K1XDPU_TAG);
}

static VOID
NTAPI
K1xRetireWorker(_In_ PVOID Parameter)
{
    PK1XDPU_CONTEXT Context = Parameter;
    PLIST_ENTRY Entry;
    KIRQL OldIrql;

    for (;;)
    {
        KeAcquireSpinLock(&Context->Lock, &OldIrql);
        if (IsListEmpty(&Context->RetiredFlips))
        {
            InterlockedExchange(&Context->RetireQueued, 0);
            KeSetEvent(&Context->RetireIdle, IO_NO_INCREMENT, FALSE);
            KeReleaseSpinLock(&Context->Lock, OldIrql);
            return;
        }
        Entry = RemoveHeadList(&Context->RetiredFlips);
        KeReleaseSpinLock(&Context->Lock, OldIrql);
        K1xFlipDestroy(CONTAINING_RECORD(Entry, K1X_FLIP, Entry));
    }
}

static VOID
K1xFlipReady(_In_ PVOID Parameter)
{
    PK1X_FLIP Flip = Parameter;
    PK1XDPU_CONTEXT Context = Flip->Context;
    KIRQL OldIrql;

    KeAcquireSpinLock(&Context->Lock, &OldIrql);
    Flip->Ready = TRUE;
    if (Context->OverlayNext == Flip)
    {
        K1xLatchLocked(Context);
        K1xApplyLocked(Context);
    }
    KeReleaseSpinLock(&Context->Lock, OldIrql);
}

static BOOLEAN
K1xOverlayForeign(
    _In_opt_ const K1X_FLIP *Flip,
    _In_ PVOID Owner)
{
    return Flip && Flip->Owner != Owner;
}

BOOLEAN
K1xOverlayQueue(
    _Inout_ PK1XDPU_CONTEXT Context,
    _In_ PK1X_FLIP Flip)
{
    PK1X_FLIP Previous;
    BOOLEAN Foreign;
    KIRQL OldIrql;
    int Armed;

    PAGED_CODE();

    Flip->Context = Context;
    Flip->Ready = FALSE;
    ExAcquireFastMutex(&Context->OverlayMutex);
    KeAcquireSpinLock(&Context->Lock, &OldIrql);
    K1xLatchLocked(Context);
    Foreign = !Context->PlanesReady ||
              K1xOverlayForeign(Context->OverlayFront, Flip->Owner) ||
              K1xOverlayForeign(Context->OverlayPending, Flip->Owner) ||
              K1xOverlayForeign(Context->OverlayNext, Flip->Owner);
    if (Foreign)
    {
        KeReleaseSpinLock(&Context->Lock, OldIrql);
        ExReleaseFastMutex(&Context->OverlayMutex);
        return FALSE;
    }
    Previous = Context->OverlayNext;
    Context->OverlayNext = Flip;
    Context->OverlayHide = FALSE;
    KeReleaseSpinLock(&Context->Lock, OldIrql);
    if (Previous)
        K1xFlipDestroy(Previous);

    Armed = pvr_glue_fence_notify(Flip->Fence, K1xFlipReady, Flip, &Flip->Wait);
    if (Armed != 0)
        K1xFlipReady(Flip);
    ExReleaseFastMutex(&Context->OverlayMutex);
    return TRUE;
}

VOID
K1xOverlayHide(
    _Inout_ PK1XDPU_CONTEXT Context,
    _In_opt_ PVOID Owner)
{
    PK1X_FLIP Next = NULL;
    BOOLEAN Owned;
    KIRQL OldIrql;

    PAGED_CODE();

    if (!Context->PlanesReady)
        return;
    ExAcquireFastMutex(&Context->OverlayMutex);
    KeAcquireSpinLock(&Context->Lock, &OldIrql);
    Owned = !Owner ||
            (Context->OverlayNext && Context->OverlayNext->Owner == Owner) ||
            (Context->OverlayPending && Context->OverlayPending->Owner == Owner) ||
            (Context->OverlayFront && Context->OverlayFront->Owner == Owner);
    if (Owned)
    {
        Next = Context->OverlayNext;
        Context->OverlayNext = NULL;
        Context->OverlayHide = TRUE;
        K1xLatchLocked(Context);
        K1xApplyLocked(Context);
    }
    KeReleaseSpinLock(&Context->Lock, OldIrql);
    if (Next)
        K1xFlipDestroy(Next);
    ExReleaseFastMutex(&Context->OverlayMutex);
}

VOID
K1xOverlayBusy(
    _Inout_ PK1XDPU_CONTEXT Context,
    _In_ PVOID Owner,
    _Out_writes_(POWERVR_SCANOUT_BUSY_COUNT) PULONG Handles)
{
    PK1X_FLIP Flips[POWERVR_SCANOUT_BUSY_COUNT];
    KIRQL OldIrql;
    ULONG Index;

    KeAcquireSpinLock(&Context->Lock, &OldIrql);
    K1xLatchLocked(Context);
    Flips[0] = Context->OverlayFront;
    Flips[1] = Context->OverlayPending;
    Flips[2] = Context->OverlayNext;
    for (Index = 0; Index < POWERVR_SCANOUT_BUSY_COUNT; ++Index)
        Handles[Index] = (Flips[Index] && Flips[Index]->Owner == Owner) ? Flips[Index]->Handle : 0;
    KeReleaseSpinLock(&Context->Lock, OldIrql);
}

VOID
K1xOverlayWait(
    _Inout_ PK1XDPU_CONTEXT Context,
    _In_ const VOID *Flip)
{
    LARGE_INTEGER Timeout;
    BOOLEAN Waiting;
    KIRQL OldIrql;
    ULONG Round;

    PAGED_CODE();

    Timeout.QuadPart = -10000LL * K1X_VSYNC_TIMEOUT_MS;
    for (Round = 0; Round < K1X_OVERLAY_WAIT_ROUNDS; ++Round)
    {
        KeAcquireSpinLock(&Context->Lock, &OldIrql);
        K1xLatchLocked(Context);
        K1xApplyLocked(Context);
        Waiting = Context->OverlayNext == Flip || Context->OverlayPending == Flip;
        if (Waiting)
            KeClearEvent(&Context->LatchEvent);
        KeReleaseSpinLock(&Context->Lock, OldIrql);
        if (!Waiting)
            return;
        (VOID)KeWaitForSingleObject(&Context->LatchEvent, Executive, KernelMode, FALSE, &Timeout);
    }
}

VOID
K1xOverlayDrain(_Inout_ PK1XDPU_CONTEXT Context)
{
    LARGE_INTEGER Timeout;
    PK1X_FLIP Flips[3];
    KIRQL OldIrql;
    ULONG Elapsed, Index;

    PAGED_CODE();

    if (!Context->PlanesReady)
        return;
    K1xOverlayHide(Context, NULL);
    for (Elapsed = 0; Elapsed < K1X_COMMIT_TIMEOUT_US; Elapsed += K1X_COMMIT_POLL_US)
    {
        BOOLEAN Idle;

        KeAcquireSpinLock(&Context->Lock, &OldIrql);
        K1xLatchLocked(Context);
        K1xApplyLocked(Context);
        Idle = !Context->CommitPending && !Context->OverlayShown;
        KeReleaseSpinLock(&Context->Lock, OldIrql);
        if (Idle)
            break;
        KeStallExecutionProcessor(K1X_COMMIT_POLL_US);
    }

    ExAcquireFastMutex(&Context->OverlayMutex);
    KeAcquireSpinLock(&Context->Lock, &OldIrql);
    Flips[0] = Context->OverlayFront;
    Flips[1] = Context->OverlayPending;
    Flips[2] = Context->OverlayNext;
    Context->OverlayFront = NULL;
    Context->OverlayPending = NULL;
    Context->OverlayNext = NULL;
    Context->OverlayCommit = K1xOverlayCommitNone;
    Context->PlanesReady = FALSE;
    KeReleaseSpinLock(&Context->Lock, OldIrql);
    for (Index = 0; Index < RTL_NUMBER_OF(Flips); ++Index)
    {
        if (Flips[Index])
            K1xFlipDestroy(Flips[Index]);
    }
    ExReleaseFastMutex(&Context->OverlayMutex);
    Timeout.QuadPart = -10000LL * 1000;
    (VOID)KeWaitForSingleObject(&Context->RetireIdle, Executive, KernelMode, FALSE, &Timeout);
}

static BOOLEAN
K1xWaitCommitTaken(_In_ PK1XDPU_CONTEXT Context)
{
    ULONG Elapsed;

    for (Elapsed = 0; Elapsed < K1X_COMMIT_TIMEOUT_US; Elapsed += K1X_COMMIT_POLL_US)
    {
        if (!(K1xRead(Context->Dpu, K1X_DPU_CTL2_COMMIT) & K1X_DPU_CTL2_COMMIT_READY))
            return TRUE;
        KeStallExecutionProcessor(K1X_COMMIT_POLL_US);
    }
    return FALSE;
}

static VOID
K1xMovePrimary(
    _In_ PK1XDPU_CONTEXT Context,
    _In_ ULONG From,
    _In_ ULONG To)
{
    PUCHAR Source = Context->Dpu + K1X_COMPOSER_SLOT(From);
    PUCHAR Target = Context->Dpu + K1X_COMPOSER_SLOT(To);

    K1xWrite(Target, K1X_SLOT_LEFT, Context->FirmwareSlot[0]);
    K1xWrite(Target, K1X_SLOT_TOP_RIGHT, Context->FirmwareSlot[1]);
    K1xWrite(Target, K1X_SLOT_BOTTOM_BLEND, Context->FirmwareSlot[2]);
    K1xWrite(Target, K1X_SLOT_ALPHA, Context->FirmwareSlot[3]);
    K1xWrite(Target, K1X_SLOT_CONTROL,
             K1X_SLOT_CONTROL_ENABLE | (Context->PrimaryChannel << K1X_SLOT_CONTROL_CHANNEL_SHIFT));
    K1xWrite(Source, K1X_SLOT_CONTROL, 0);
}

static VOID
NTAPI
K1xPlanesBugCheck(
    _In_ PVOID Buffer,
    _In_ ULONG Length)
{
    PK1XDPU_CONTEXT Context = Buffer;
    ULONG Value;

    UNREFERENCED_PARAMETER(Length);

    if (!Context->Dpu)
        return;
    K1xWrite(Context->Dpu, K1X_DPU_INT_ONLINE2_MASK, 0);
    (VOID)K1xWaitCommitTaken(Context);
    K1xWrite(Context->Dpu + K1X_COMPOSER_SLOT(K1X_OVERLAY_SLOT), K1X_SLOT_CONTROL, 0);
    K1xMovePrimary(Context, K1X_PRIMARY_SLOT, K1X_FIRMWARE_SLOT);
    Value = K1xRead(Context->Dpu, K1X_DPU_CTL2_CHANNELS) & ~K1X_DPU_CTL2_CHANNEL_MASK;
    K1xWrite(Context->Dpu, K1X_DPU_CTL2_CHANNELS, Value | (1UL << Context->PrimaryChannel));
    K1xWrite(Context->Dpu, K1X_DPU_CTL2_COMMIT, K1X_DPU_CTL2_COMMIT_READY);
}

NTSTATUS
K1xPlanesStart(
    _Inout_ PK1XDPU_CONTEXT Context,
    _In_ const DXGK_DEVICE_INFO *Information,
    _In_z_ const CHAR *Output)
{
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Interrupt;
    PHYSICAL_ADDRESS Low, High, Skip;
    ULONG Ordinal, Control, Channels;
    PUCHAR Slot;
    NTSTATUS Status;

    PAGED_CODE();

    if (!Context->Dpu || Context->PlanesReady)
        return STATUS_DEVICE_NOT_READY;
    Ordinal = K1xFindName(Information->PhysicalDeviceObject, L"interrupt-names", Output);
    Interrupt = K1xFindResource(Information, CmResourceTypeInterrupt, Ordinal);
    if (!Interrupt)
    {
        DPRINT1("K1XDPU: no '%s' interrupt, planes disabled\n", Output);
        return STATUS_NOT_SUPPORTED;
    }

    Slot = Context->Dpu + K1X_COMPOSER_SLOT(K1X_FIRMWARE_SLOT);
    Control = K1xRead(Slot, K1X_SLOT_CONTROL);
    Channels = K1xRead(Context->Dpu, K1X_DPU_CTL2_CHANNELS) & K1X_DPU_CTL2_CHANNEL_MASK;
    Context->PrimaryChannel = (Control & K1X_SLOT_CONTROL_CHANNEL_MASK) >> K1X_SLOT_CONTROL_CHANNEL_SHIFT;
    if (!(Control & K1X_SLOT_CONTROL_ENABLE) || Context->PrimaryChannel >= K1X_DPU_CHANNEL_COUNT ||
        Context->PrimaryChannel == K1X_CURSOR_CHANNEL || Context->PrimaryChannel == K1X_OVERLAY_CHANNEL ||
        Channels != (1UL << Context->PrimaryChannel))
    {
        DPRINT1("K1XDPU: unexpected firmware layout (slot 0x%lx, channels 0x%lx), planes disabled\n",
                Control, Channels);
        return STATUS_NOT_SUPPORTED;
    }
    Context->FirmwareSlot[0] = K1xRead(Slot, K1X_SLOT_LEFT);
    Context->FirmwareSlot[1] = K1xRead(Slot, K1X_SLOT_TOP_RIGHT);
    Context->FirmwareSlot[2] = K1xRead(Slot, K1X_SLOT_BOTTOM_BLEND);
    Context->FirmwareSlot[3] = K1xRead(Slot, K1X_SLOT_ALPHA);

    Low.QuadPart = 0;
    High.QuadPart = K1X_SCANOUT_HIGHEST_ADDRESS;
    Skip.QuadPart = 0;
    Context->CursorBuffer = MmAllocateContiguousMemorySpecifyCache(K1X_CURSOR_BYTES, Low, High, Skip,
                                                                   MmWriteCombined);
    if (!Context->CursorBuffer)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(Context->CursorBuffer, K1X_CURSOR_BYTES);
    Context->CursorPhysical = MmGetPhysicalAddress(Context->CursorBuffer);

    KeInitializeSpinLock(&Context->Lock);
    ExInitializeFastMutex(&Context->OverlayMutex);
    KeInitializeEvent(&Context->VsyncEvent, NotificationEvent, FALSE);
    KeInitializeEvent(&Context->LatchEvent, NotificationEvent, FALSE);
    KeInitializeEvent(&Context->RetireIdle, NotificationEvent, TRUE);
    KeInitializeDpc(&Context->InterruptDpc, K1xPlanesDpc, Context);
    InitializeListHead(&Context->RetiredFlips);
    ExInitializeWorkItem(&Context->RetireItem, K1xRetireWorker, Context);
    Context->Channels = Channels;
    Context->InterruptMask = 0;
    K1xWrite(Context->Dpu, K1X_DPU_INT_ONLINE2_MASK, 0);
    K1xWrite(Context->Dpu, K1X_DPU_INT_ONLINE2_STATUS,
             K1X_DPU_INT_VSYNC | K1X_DPU_INT_COMMIT_TAKEN | K1X_DPU_INT_UNDERFLOW);

    Status = IoConnectInterrupt(&Context->Interrupt, K1xPlanesInterrupt, Context, NULL,
                                Interrupt->u.Interrupt.Vector, (KIRQL)Interrupt->u.Interrupt.Level,
                                (KIRQL)Interrupt->u.Interrupt.Level,
                                (Interrupt->Flags & CM_RESOURCE_INTERRUPT_LATCHED) ? Latched : LevelSensitive,
                                Interrupt->ShareDisposition == CmResourceShareShared,
                                Interrupt->u.Interrupt.Affinity, FALSE);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("K1XDPU: display interrupt %lu not connected (0x%08lx)\n",
                Interrupt->u.Interrupt.Vector, Status);
        MmFreeContiguousMemorySpecifyCache(Context->CursorBuffer, K1X_CURSOR_BYTES, MmWriteCombined);
        Context->CursorBuffer = NULL;
        return Status;
    }
    Context->InterruptConnected = TRUE;

    if (!K1xWaitCommitTaken(Context))
        DPRINT1("K1XDPU: firmware commit still pending\n");
    K1xMovePrimary(Context, K1X_FIRMWARE_SLOT, K1X_PRIMARY_SLOT);
    KeMemoryBarrier();
    K1xWrite(Context->Dpu, K1X_DPU_CTL2_COMMIT, K1X_DPU_CTL2_COMMIT_READY);
    if (!K1xWaitCommitTaken(Context))
    {
        DPRINT1("K1XDPU: plane layout was not taken, planes disabled\n");
        IoDisconnectInterrupt(Context->Interrupt);
        Context->InterruptConnected = FALSE;
        MmFreeContiguousMemorySpecifyCache(Context->CursorBuffer, K1X_CURSOR_BYTES, MmWriteCombined);
        Context->CursorBuffer = NULL;
        return STATUS_IO_TIMEOUT;
    }

    KeInitializeCallbackRecord(&Context->BugCheckRecord);
    Context->BugCheckRegistered = KeRegisterBugCheckCallback(&Context->BugCheckRecord, K1xPlanesBugCheck,
                                                             Context, sizeof(*Context), (PUCHAR)"K1XDPU");
    Context->InterruptMask = K1X_DPU_INT_COMMIT_TAKEN;
    K1xWrite(Context->Dpu, K1X_DPU_INT_ONLINE2_MASK, Context->InterruptMask);
    Context->PlanesReady = TRUE;
    DPRINT1("K1XDPU: planes ready on '%s', primary channel %lu, interrupt %lu\n",
            Output, Context->PrimaryChannel, Interrupt->u.Interrupt.Vector);
    return STATUS_SUCCESS;
}

VOID
K1xPlanesStop(_Inout_ PK1XDPU_CONTEXT Context)
{
    KIRQL OldIrql;
    ULONG Value;

    PAGED_CODE();

    if (!Context->PlanesReady)
        return;
    K1xOverlayDrain(Context);

    KeAcquireSpinLock(&Context->Lock, &OldIrql);
    Context->CursorVisible = FALSE;
    Context->CursorDirty = FALSE;
    Context->InterruptMask = 0;
    K1xWrite(Context->Dpu, K1X_DPU_INT_ONLINE2_MASK, 0);
    KeReleaseSpinLock(&Context->Lock, OldIrql);

    if (Context->BugCheckRegistered)
    {
        (VOID)KeDeregisterBugCheckCallback(&Context->BugCheckRecord);
        Context->BugCheckRegistered = FALSE;
    }
    if (Context->InterruptConnected)
    {
        IoDisconnectInterrupt(Context->Interrupt);
        Context->InterruptConnected = FALSE;
    }
    KeRemoveQueueDpc(&Context->InterruptDpc);
    KeFlushQueuedDpcs();

    (VOID)K1xWaitCommitTaken(Context);
    K1xWrite(Context->Dpu + K1X_COMPOSER_SLOT(K1X_OVERLAY_SLOT), K1X_SLOT_CONTROL, 0);
    K1xMovePrimary(Context, K1X_PRIMARY_SLOT, K1X_FIRMWARE_SLOT);
    Value = K1xRead(Context->Dpu, K1X_DPU_CTL2_CHANNELS) & ~K1X_DPU_CTL2_CHANNEL_MASK;
    K1xWrite(Context->Dpu, K1X_DPU_CTL2_CHANNELS, Value | (1UL << Context->PrimaryChannel));
    KeMemoryBarrier();
    K1xWrite(Context->Dpu, K1X_DPU_CTL2_COMMIT, K1X_DPU_CTL2_COMMIT_READY);
    (VOID)K1xWaitCommitTaken(Context);
    Context->CommitPending = FALSE;
    Context->Channels = 1UL << Context->PrimaryChannel;

    MmFreeContiguousMemorySpecifyCache(Context->CursorBuffer, K1X_CURSOR_BYTES, MmWriteCombined);
    Context->CursorBuffer = NULL;
}
