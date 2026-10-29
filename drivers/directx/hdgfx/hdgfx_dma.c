/*
 * PROJECT:     LiberNT Intel HD Graphics Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     DMA buffers as render ring batches, submission and fence completion
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include "hdgfx.h"
#include "softgpu_2d_core.h"

#define HDGFX_DMA_BATCH             1UL
#define HDGFX_DMA_CPU               2UL
#define HDGFX_DMA_NOP               3UL
#define HDGFX_PATCH_GTT             0UL
#define HDGFX_PATCH_PHYSICAL        1UL
#define HDGFX_BATCH_LIMIT           0x2000UL
#define HDGFX_BLT_DWORDS            8UL
#define HDGFX_QUEUE_POLL            (-10000LL)

typedef struct _HDGFX_DMA_RECORD
{
    ULONG Type;
    ULONG Size;
} HDGFX_DMA_RECORD, *PHDGFX_DMA_RECORD;

typedef struct _HDGFX_DMA_WRITER
{
    PUCHAR Base;
    PUCHAR Cursor;
    PUCHAR End;
    PHDGFX_DMA_RECORD Batch;
    D3DDDI_PATCHLOCATIONLIST *Patch;
    D3DDDI_PATCHLOCATIONLIST *PatchEnd;
} HDGFX_DMA_WRITER, *PHDGFX_DMA_WRITER;

typedef struct _HDGFX_DMA_SURFACE
{
    CONST DXGK_ALLOCATIONLIST *Allocation;
    PSOFTGPU_OPENALLOC Open;
    UINT AllocationIndex;
} HDGFX_DMA_SURFACE, *PHDGFX_DMA_SURFACE;

typedef enum _HDGFX_QUEUE_STATE
{
    HdgfxQueueDone,
    HdgfxQueueRing,
    HdgfxQueueWait
} HDGFX_QUEUE_STATE;

static NTSTATUS
HdgfxResolveOwner(
    _In_ PVOID Handle,
    _Out_ PSOFTGPU_KMD_DEVICE *KmdDeviceOut,
    _Out_ PSOFTGPU_DEVICE *DeviceOut)
{
    PSOFTGPU_CONTEXT Context = Handle;
    PSOFTGPU_KMD_DEVICE KmdDevice;
    PSOFTGPU_DEVICE Device;

    *KmdDeviceOut = NULL;
    *DeviceOut = NULL;
    if (Handle == NULL)
        return STATUS_INVALID_HANDLE;
    if (Context->Magic == SOFTGPU_CONTEXT_MAGIC)
    {
        KmdDevice = Context->Device;
        if (KmdDevice == NULL || KmdDevice->Magic != SOFTGPU_KMD_DEVICE_MAGIC ||
            Context->Process == NULL || Context->Process != KmdDevice->Process)
        {
            return STATUS_INVALID_HANDLE;
        }
    }
    else
    {
        KmdDevice = Handle;
        if (KmdDevice->Magic != SOFTGPU_KMD_DEVICE_MAGIC)
            return STATUS_INVALID_HANDLE;
    }

    Device = KmdDevice->Adapter;
    if (Device == NULL || Device->Magic != SOFTGPU_DEVICE_MAGIC ||
        KmdDevice->Process == NULL || KmdDevice->Process->Magic != SOFTGPU_PROCESS_MAGIC ||
        KmdDevice->Process->Adapter != Device)
    {
        return STATUS_INVALID_HANDLE;
    }
    *KmdDeviceOut = KmdDevice;
    *DeviceOut = Device;
    return STATUS_SUCCESS;
}

static NTSTATUS
HdgfxValidateOpen(
    _In_ PSOFTGPU_KMD_DEVICE KmdDevice,
    _In_ PSOFTGPU_OPENALLOC Open)
{
    if (Open == NULL || Open->Magic != SOFTGPU_OPENALLOC_MAGIC || Open->Device != KmdDevice)
        return STATUS_INVALID_HANDLE;
    if (Open->Size == 0 || Open->Size > KmdDevice->Adapter->FrameBufferSize)
        return STATUS_INVALID_BUFFER_SIZE;
    return STATUS_SUCCESS;
}

static NTSTATUS
HdgfxValidateSurface(
    _In_ PSOFTGPU_KMD_DEVICE KmdDevice,
    _In_ PSOFTGPU_OPENALLOC Open)
{
    NTSTATUS Status = HdgfxValidateOpen(KmdDevice, Open);

    if (!NT_SUCCESS(Status))
        return Status;
    if (Open->Width == 0 || Open->Height == 0 ||
        Open->Width > SOFTGPU_MAX_DISPLAY_WIDTH || Open->Height > SOFTGPU_MAX_DISPLAY_HEIGHT ||
        Open->Pitch < Open->Width * SOFTGPU_DISPLAY_BYTES_PER_PIXEL || Open->Pitch > SOFTGPU_MAX_DISPLAY_PITCH ||
        Open->Pitch % SOFTGPU_DISPLAY_BYTES_PER_PIXEL != 0 ||
        (Open->Format != D3DDDIFMT_X8R8G8B8 && Open->Format != D3DDDIFMT_A8R8G8B8))
    {
        return STATUS_NOT_SUPPORTED;
    }
    if (Open->Size != (SIZE_T)Open->Pitch * Open->Height)
        return STATUS_INVALID_BUFFER_SIZE;
    return STATUS_SUCCESS;
}

static NTSTATUS
HdgfxPlacement(
    _In_ PSOFTGPU_DEVICE Device,
    _In_ CONST DXGK_ALLOCATIONLIST *Allocation,
    _In_ PSOFTGPU_OPENALLOC Open,
    _In_ UINT AllocationOffset,
    _In_ UINT DriverId,
    _Out_ PULONGLONG Value)
{
    PHDGFX_SCANOUT Scanout = Device->PlatformContext;
    ULONGLONG Address;
    SIZE_T Offset;
    NTSTATUS Status;

    *Value = 0;
    if (AllocationOffset >= Open->Size)
        return STATUS_INVALID_PARAMETER;
    if (Allocation->SegmentId == 0)
        return STATUS_SUCCESS;
    if (Allocation->SegmentId != SOFTGPU_SEGMENT_ID || Allocation->PhysicalAddress.QuadPart <= 0)
        return STATUS_INVALID_ADDRESS;

    Address = (ULONGLONG)Allocation->PhysicalAddress.QuadPart + AllocationOffset;
    Status = SoftGpu2dResolveSlabRange((ULONGLONG)Device->FrameBufferPhys.QuadPart, Device->FrameBufferSize, Address,
                                       Open->Size - AllocationOffset, &Offset);
    if (!NT_SUCCESS(Status))
        return Status;
    if (DriverId == HDGFX_PATCH_PHYSICAL)
    {
        *Value = Address;
        return STATUS_SUCCESS;
    }
    if (Scanout == NULL || Scanout->SlabGtt == NULL || Scanout->SlabPhysical != (ULONGLONG)Device->FrameBufferPhys.QuadPart)
        return STATUS_INVALID_ADDRESS;
    *Value = Scanout->SlabAddress + (ULONGLONG)Offset;
    return STATUS_SUCCESS;
}

static BOOLEAN
HdgfxBlitterUsable(
    _In_ PSOFTGPU_DEVICE Device)
{
    PHDGFX_ADAPTER Adapter = Device->PlatformServicesContext;
    PHDGFX_SCANOUT Scanout = Device->PlatformContext;

    return Adapter != NULL && Scanout != NULL && Adapter->RingStarted && Adapter->RingInterrupt &&
           Scanout->SlabGtt != NULL;
}

static VOID
HdgfxWriterInit(
    _Out_ PHDGFX_DMA_WRITER Writer,
    _In_ PVOID DmaBuffer,
    _In_ UINT DmaSize,
    _In_opt_ D3DDDI_PATCHLOCATIONLIST *Patch,
    _In_ UINT PatchCount)
{
    Writer->Base = DmaBuffer;
    Writer->Cursor = DmaBuffer;
    Writer->End = Writer->Base + DmaSize;
    Writer->Batch = NULL;
    Writer->Patch = Patch;
    Writer->PatchEnd = Patch == NULL ? NULL : Patch + PatchCount;
}

static VOID
HdgfxWriterClose(
    _Inout_ PHDGFX_DMA_WRITER Writer)
{
    if (Writer->Batch == NULL)
        return;
    if ((Writer->Cursor - (PUCHAR)Writer->Batch) % 8 != 0)
    {
        *(PULONG)Writer->Cursor = 0;
        Writer->Cursor += sizeof(ULONG);
    }
    Writer->Batch->Size = (ULONG)(Writer->Cursor - (PUCHAR)Writer->Batch);
    Writer->Batch = NULL;
}

static BOOLEAN
HdgfxWriterFits(
    _In_ PHDGFX_DMA_WRITER Writer,
    _In_ SIZE_T Bytes,
    _In_ UINT Patches)
{
    return (SIZE_T)(Writer->End - Writer->Cursor) >= sizeof(HDGFX_DMA_RECORD) + Bytes + sizeof(ULONG) &&
           (Patches == 0 || (Writer->Patch != NULL && (SIZE_T)(Writer->PatchEnd - Writer->Patch) >= Patches));
}

static PULONG
HdgfxWriterBatch(
    _Inout_ PHDGFX_DMA_WRITER Writer,
    _In_ ULONG Dwords)
{
    PULONG Commands;

    if (Writer->Batch != NULL &&
        (SIZE_T)(Writer->Cursor - (PUCHAR)Writer->Batch) + Dwords * sizeof(ULONG) > HDGFX_BATCH_LIMIT)
    {
        HdgfxWriterClose(Writer);
    }
    if (Writer->Batch == NULL)
    {
        Writer->Batch = (PHDGFX_DMA_RECORD)Writer->Cursor;
        Writer->Batch->Type = HDGFX_DMA_BATCH;
        Writer->Batch->Size = 0;
        Writer->Cursor += sizeof(HDGFX_DMA_RECORD);
    }
    Commands = (PULONG)Writer->Cursor;
    Writer->Cursor += Dwords * sizeof(ULONG);
    return Commands;
}

static PVOID
HdgfxWriterRecord(
    _Inout_ PHDGFX_DMA_WRITER Writer,
    _In_ ULONG Type,
    _In_ ULONG Bytes)
{
    PHDGFX_DMA_RECORD Record;

    HdgfxWriterClose(Writer);
    Record = (PHDGFX_DMA_RECORD)Writer->Cursor;
    Record->Type = Type;
    Record->Size = sizeof(*Record) + Bytes;
    Writer->Cursor += Record->Size;
    RtlZeroMemory(Record + 1, Bytes);
    return Record + 1;
}

static VOID
HdgfxWriterPatch(
    _Inout_ PHDGFX_DMA_WRITER Writer,
    _In_ CONST VOID *Location,
    _In_ CONST HDGFX_DMA_SURFACE *Surface,
    _In_ UINT DriverId)
{
    RtlZeroMemory(Writer->Patch, sizeof(*Writer->Patch));
    Writer->Patch->AllocationIndex = Surface->AllocationIndex;
    Writer->Patch->DriverId = DriverId;
    Writer->Patch->PatchOffset = (UINT)((CONST UCHAR *)Location - Writer->Base);
    Writer->Patch++;
}

static BOOLEAN
HdgfxRectsOverlap(
    _In_ CONST SOFTGPU_CMD *Command,
    _In_ PSOFTGPU_OPENALLOC Open)
{
    SIZE_T SourceOffset;
    SIZE_T DestinationOffset;
    SIZE_T RowBytes;
    SIZE_T SourceSpan;
    SIZE_T DestinationSpan;

    if (!NT_SUCCESS(SoftGpu2dRectRange(Open->Size, Command->SrcPitch, &Command->SrcRect, &SourceOffset, &RowBytes,
                                       &SourceSpan)) ||
        !NT_SUCCESS(SoftGpu2dRectRange(Open->Size, Command->DstPitch, &Command->DstRect, &DestinationOffset,
                                       &RowBytes, &DestinationSpan)))
    {
        return TRUE;
    }
    return SourceOffset < DestinationOffset + DestinationSpan && DestinationOffset < SourceOffset + SourceSpan;
}

static NTSTATUS
HdgfxEmitCommand(
    _Inout_ PHDGFX_DMA_WRITER Writer,
    _In_ PSOFTGPU_DEVICE Device,
    _In_ CONST SOFTGPU_CMD *Command,
    _In_opt_ CONST HDGFX_DMA_SURFACE *Source,
    _In_ CONST HDGFX_DMA_SURFACE *Destination)
{
    HDGFX_BLT_SURFACE SourceSurface;
    HDGFX_BLT_SURFACE DestinationSurface;
    HDGFX_BLT_RECT Rect;
    PSOFTGPU_CMD Cpu;
    ULONGLONG Value;
    uint32_t Dwords[HDGFX_BLT_DWORDS];
    PULONG Batch;
    ULONG Count = 0;
    UINT Patches = Source != NULL ? 2 : 1;
    NTSTATUS Status;

    if ((Command->Op == SOFTGPU_CMD_OP_FILL || Command->Op == SOFTGPU_CMD_OP_BLT) && HdgfxBlitterUsable(Device) &&
        !(Source != NULL && Source->Open == Destination->Open && HdgfxRectsOverlap(Command, Source->Open) &&
          Source->Open->Pitch % HDGFX_BLT_OVERLAP_PITCH != 0))
    {
        Rect.Left = (uint32_t)Command->DstRect.left;
        Rect.Top = (uint32_t)Command->DstRect.top;
        Rect.Right = (uint32_t)Command->DstRect.right;
        Rect.Bottom = (uint32_t)Command->DstRect.bottom;
        Status = HdgfxPlacement(Device, Destination->Allocation, Destination->Open, 0, HDGFX_PATCH_GTT, &Value);
        if (!NT_SUCCESS(Status))
            return Status;
        DestinationSurface.Address = (uint32_t)Value;
        DestinationSurface.Pitch = Command->DstPitch;
        if (Source == NULL)
        {
            Count = HdgfxBltFill(Dwords, &DestinationSurface, &Rect, Command->Color);
        }
        else
        {
            Status = HdgfxPlacement(Device, Source->Allocation, Source->Open, 0, HDGFX_PATCH_GTT, &Value);
            if (!NT_SUCCESS(Status))
                return Status;
            SourceSurface.Address = (uint32_t)Value;
            SourceSurface.Pitch = Command->SrcPitch;
            Count = HdgfxBltCopy(Dwords, &DestinationSurface, &Rect, &SourceSurface,
                                 (uint32_t)Command->SrcRect.left, (uint32_t)Command->SrcRect.top);
        }
    }

    if (Count != 0)
    {
        if (!HdgfxWriterFits(Writer, Count * sizeof(ULONG), Patches))
            return STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER;
        Batch = HdgfxWriterBatch(Writer, Count);
        RtlCopyMemory(Batch, Dwords, Count * sizeof(ULONG));
        if (Source != NULL)
            HdgfxWriterPatch(Writer, &Batch[7], Source, HDGFX_PATCH_GTT);
        HdgfxWriterPatch(Writer, &Batch[4], Destination, HDGFX_PATCH_GTT);
        return STATUS_SUCCESS;
    }

    if (!HdgfxWriterFits(Writer, sizeof(SOFTGPU_CMD), Patches))
        return STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER;
    Cpu = HdgfxWriterRecord(Writer, HDGFX_DMA_CPU, sizeof(SOFTGPU_CMD));
    *Cpu = *Command;
    if (Source != NULL)
    {
        Status = HdgfxPlacement(Device, Source->Allocation, Source->Open, 0, HDGFX_PATCH_PHYSICAL, &Cpu->SrcAddress);
        if (!NT_SUCCESS(Status))
            return Status;
        HdgfxWriterPatch(Writer, &Cpu->SrcAddress, Source, HDGFX_PATCH_PHYSICAL);
    }
    Status = HdgfxPlacement(Device, Destination->Allocation, Destination->Open, 0, HDGFX_PATCH_PHYSICAL,
                            &Cpu->DstAddress);
    if (!NT_SUCCESS(Status))
        return Status;
    HdgfxWriterPatch(Writer, &Cpu->DstAddress, Destination, HDGFX_PATCH_PHYSICAL);
    return STATUS_SUCCESS;
}

static NTSTATUS
HdgfxRenderSurface(
    _In_ PSOFTGPU_KMD_DEVICE KmdDevice,
    _In_ CONST DXGKARG_RENDER *Render,
    _In_ UINT PatchOffset,
    _Out_ PHDGFX_DMA_SURFACE Surface)
{
    CONST D3DDDI_PATCHLOCATIONLIST *Patch = NULL;
    UINT Index;

    for (Index = 0; Index < Render->PatchLocationListInSize; Index++)
    {
        if (Render->pPatchLocationListIn[Index].PatchOffset != PatchOffset)
            continue;
        if (Patch != NULL)
            return STATUS_INVALID_PARAMETER;
        Patch = &Render->pPatchLocationListIn[Index];
    }
    if (Patch == NULL || Patch->AllocationIndex >= Render->AllocationListSize || Patch->AllocationOffset != 0)
        return STATUS_INVALID_PARAMETER;
    Surface->AllocationIndex = Patch->AllocationIndex;
    Surface->Allocation = &Render->pAllocationList[Patch->AllocationIndex];
    Surface->Open = (PSOFTGPU_OPENALLOC)Surface->Allocation->hDeviceSpecificAllocation;
    return HdgfxValidateOpen(KmdDevice, Surface->Open);
}

static NTSTATUS
HdgfxRenderRect(
    _In_ PSOFTGPU_KMD_DEVICE KmdDevice,
    _In_ CONST HDGFX_DMA_SURFACE *Surface,
    _In_ CONST RECT *Rect,
    _In_ ULONG Pitch,
    _In_ BOOLEAN WriteOperation)
{
    SIZE_T Offset;
    SIZE_T RowBytes;
    SIZE_T Span;
    NTSTATUS Status = HdgfxValidateSurface(KmdDevice, Surface->Open);

    if (!NT_SUCCESS(Status))
        return Status;
    if ((WriteOperation && !Surface->Allocation->WriteOperation) ||
        Rect->left < 0 || Rect->top < 0 ||
        Rect->right > (LONG)Surface->Open->Width || Rect->bottom > (LONG)Surface->Open->Height ||
        Pitch != Surface->Open->Pitch)
    {
        return STATUS_INVALID_PARAMETER;
    }
    return SoftGpu2dRectRange(Surface->Open->Size, Pitch, Rect, &Offset, &RowBytes, &Span);
}

static NTSTATUS
HdgfxRenderCommand(
    _In_ PSOFTGPU_KMD_DEVICE KmdDevice,
    _In_ CONST DXGKARG_RENDER *Render,
    _In_ UINT Offset,
    _Inout_opt_ PHDGFX_DMA_WRITER Writer,
    _Inout_ PUINT Patches)
{
    CONST SOFTGPU_CMD *Command = (CONST SOFTGPU_CMD *)((CONST UCHAR *)Render->pCommand + Offset);
    HDGFX_DMA_SURFACE Source;
    HDGFX_DMA_SURFACE Destination;
    UINT SourceOffset = Offset + FIELD_OFFSET(SOFTGPU_CMD, SrcAddress);
    UINT DestinationOffset = Offset + FIELD_OFFSET(SOFTGPU_CMD, DstAddress);
    UINT Index;
    NTSTATUS Status;

    if (Command->Magic != SOFTGPU_CMD_MAGIC || Command->Size != sizeof(SOFTGPU_CMD))
        return STATUS_INVALID_PARAMETER;

    switch (Command->Op)
    {
        case SOFTGPU_CMD_OP_NOP:
            if (Writer != NULL)
                return STATUS_SUCCESS;
            for (Index = 0; Index < Render->PatchLocationListInSize; Index++)
            {
                UINT PatchOffset = Render->pPatchLocationListIn[Index].PatchOffset;
                UINT Other;

                if (PatchOffset != SourceOffset && PatchOffset != DestinationOffset)
                    continue;
                for (Other = 0; Other < Index; Other++)
                {
                    if (Render->pPatchLocationListIn[Other].PatchOffset == PatchOffset)
                        return STATUS_INVALID_PARAMETER;
                }
                (*Patches)++;
            }
            return STATUS_SUCCESS;

        case SOFTGPU_CMD_OP_BLT:
            Status = HdgfxRenderSurface(KmdDevice, Render, SourceOffset, &Source);
            if (NT_SUCCESS(Status))
                Status = HdgfxRenderRect(KmdDevice, &Source, &Command->SrcRect, Command->SrcPitch, FALSE);
            if (NT_SUCCESS(Status))
                Status = HdgfxRenderSurface(KmdDevice, Render, DestinationOffset, &Destination);
            if (NT_SUCCESS(Status))
                Status = HdgfxRenderRect(KmdDevice, &Destination, &Command->DstRect, Command->DstPitch, TRUE);
            if (!NT_SUCCESS(Status))
                return Status;
            if (Command->SrcRect.right - Command->SrcRect.left != Command->DstRect.right - Command->DstRect.left ||
                Command->SrcRect.bottom - Command->SrcRect.top != Command->DstRect.bottom - Command->DstRect.top)
            {
                return STATUS_INVALID_PARAMETER;
            }
            *Patches += 2;
            return Writer != NULL ? HdgfxEmitCommand(Writer, KmdDevice->Adapter, Command, &Source, &Destination) :
                                    STATUS_SUCCESS;

        case SOFTGPU_CMD_OP_FILL:
            Status = HdgfxRenderSurface(KmdDevice, Render, DestinationOffset, &Destination);
            if (NT_SUCCESS(Status))
                Status = HdgfxRenderRect(KmdDevice, &Destination, &Command->DstRect, Command->DstPitch, TRUE);
            if (!NT_SUCCESS(Status))
                return Status;
            (*Patches)++;
            return Writer != NULL ? HdgfxEmitCommand(Writer, KmdDevice->Adapter, Command, NULL, &Destination) :
                                    STATUS_SUCCESS;

        case SOFTGPU_CMD_OP_SIGNAL_FENCE:
        case SOFTGPU_CMD_OP_WAIT_FENCE:
            if (Command->FenceGpuVa != 0)
                return STATUS_INVALID_PARAMETER;
            Status = HdgfxRenderSurface(KmdDevice, Render, DestinationOffset, &Destination);
            if (!NT_SUCCESS(Status))
                return Status;
            if (Destination.Open->Size < sizeof(UINT64) ||
                (Command->Op == SOFTGPU_CMD_OP_SIGNAL_FENCE && !Destination.Allocation->WriteOperation))
            {
                return STATUS_INVALID_PARAMETER;
            }
            (*Patches)++;
            return Writer != NULL ? HdgfxEmitCommand(Writer, KmdDevice->Adapter, Command, NULL, &Destination) :
                                    STATUS_SUCCESS;

        default:
            return STATUS_INVALID_PARAMETER;
    }
}

NTSTATUS
APIENTRY
HdgfxDdiRender(
    _In_ PVOID hContext,
    _Inout_ DXGKARG_RENDER *pRender)
{
    PSOFTGPU_KMD_DEVICE KmdDevice;
    PSOFTGPU_DEVICE Device;
    HDGFX_DMA_WRITER Writer;
    UINT Patches = 0;
    UINT Offset;
    UINT Index;
    NTSTATUS Status;

    Status = HdgfxResolveOwner(hContext, &KmdDevice, &Device);
    if (!NT_SUCCESS(Status))
        return Status;
    if (pRender == NULL || pRender->pCommand == NULL || pRender->pDmaBuffer == NULL)
        return STATUS_INVALID_PARAMETER;
    if (pRender->CommandLength == 0 || pRender->CommandLength % sizeof(SOFTGPU_CMD) != 0 ||
        pRender->MultipassOffset >= pRender->CommandLength || pRender->MultipassOffset % sizeof(SOFTGPU_CMD) != 0)
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (pRender->PatchLocationListInSize > pRender->PatchLocationListOutSize)
        return STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER;
    if (pRender->PatchLocationListInSize != 0 &&
        (pRender->pPatchLocationListIn == NULL || pRender->pPatchLocationListOut == NULL ||
         pRender->pAllocationList == NULL || pRender->AllocationListSize == 0))
    {
        return STATUS_INVALID_PARAMETER;
    }

    for (Index = 0; Index < pRender->PatchLocationListInSize; Index++)
    {
        CONST D3DDDI_PATCHLOCATIONLIST *Patch = &pRender->pPatchLocationListIn[Index];
        CONST DXGK_ALLOCATIONLIST *Allocation;
        UINT RecordOffset;
        ULONGLONG Value;

        if (Patch->PatchOffset > pRender->CommandLength ||
            sizeof(ULONGLONG) > pRender->CommandLength - Patch->PatchOffset)
        {
            return STATUS_INVALID_USER_BUFFER;
        }
        RecordOffset = Patch->PatchOffset % sizeof(SOFTGPU_CMD);
        if ((RecordOffset != FIELD_OFFSET(SOFTGPU_CMD, SrcAddress) &&
             RecordOffset != FIELD_OFFSET(SOFTGPU_CMD, DstAddress)) ||
            Patch->AllocationIndex >= pRender->AllocationListSize || Patch->AllocationOffset != 0)
        {
            return STATUS_INVALID_PARAMETER;
        }
        Allocation = &pRender->pAllocationList[Patch->AllocationIndex];
        Status = HdgfxValidateOpen(KmdDevice, (PSOFTGPU_OPENALLOC)Allocation->hDeviceSpecificAllocation);
        if (NT_SUCCESS(Status))
        {
            Status = HdgfxPlacement(Device, Allocation, (PSOFTGPU_OPENALLOC)Allocation->hDeviceSpecificAllocation, 0,
                                    HDGFX_PATCH_PHYSICAL, &Value);
        }
        if (!NT_SUCCESS(Status))
            return Status;
    }
    for (Offset = 0; Offset < pRender->CommandLength; Offset += sizeof(SOFTGPU_CMD))
    {
        Status = HdgfxRenderCommand(KmdDevice, pRender, Offset, NULL, &Patches);
        if (!NT_SUCCESS(Status))
            return Status;
    }
    if (Patches != pRender->PatchLocationListInSize)
        return STATUS_INVALID_PARAMETER;

    HdgfxWriterInit(&Writer, pRender->pDmaBuffer, pRender->DmaSize, pRender->pPatchLocationListOut,
                    pRender->PatchLocationListOutSize);
    Status = STATUS_SUCCESS;
    for (Offset = pRender->MultipassOffset; Offset < pRender->CommandLength; Offset += sizeof(SOFTGPU_CMD))
    {
        Status = HdgfxRenderCommand(KmdDevice, pRender, Offset, &Writer, &Patches);
        if (!NT_SUCCESS(Status))
            break;
    }
    if (Status == STATUS_SUCCESS && Writer.Cursor == Writer.Base)
    {
        if (!HdgfxWriterFits(&Writer, 0, 0))
            return STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER;
        HdgfxWriterRecord(&Writer, HDGFX_DMA_NOP, 0);
    }
    if (!NT_SUCCESS(Status) && Status != STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER)
        return Status;
    HdgfxWriterClose(&Writer);

    pRender->pDmaBuffer = Writer.Cursor;
    pRender->pPatchLocationListOut = Writer.Patch;
    pRender->MultipassOffset = Status == STATUS_SUCCESS ? 0 : Offset;
    return Status;
}

static NTSTATUS
HdgfxPresentSurface(
    _In_ PSOFTGPU_KMD_DEVICE KmdDevice,
    _In_ CONST DXGKARG_PRESENT *Present,
    _In_ UINT AllocationIndex,
    _In_ CONST RECT *Rect,
    _Out_ PHDGFX_DMA_SURFACE Surface)
{
    NTSTATUS Status;

    Surface->AllocationIndex = AllocationIndex;
    Surface->Allocation = &Present->pAllocationList[AllocationIndex];
    Surface->Open = (PSOFTGPU_OPENALLOC)Surface->Allocation->hDeviceSpecificAllocation;
    Status = HdgfxValidateSurface(KmdDevice, Surface->Open);
    if (!NT_SUCCESS(Status))
        return Status;
    if (Rect->left < 0 || Rect->top < 0 || Rect->left >= Rect->right || Rect->top >= Rect->bottom ||
        Rect->right > (LONG)Surface->Open->Width || Rect->bottom > (LONG)Surface->Open->Height)
    {
        return STATUS_INVALID_PARAMETER;
    }
    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
HdgfxDdiPresent(
    _In_ PVOID hContext,
    _Inout_ DXGKARG_PRESENT *pPresent)
{
    PSOFTGPU_KMD_DEVICE KmdDevice;
    PSOFTGPU_DEVICE Device;
    SOFTGPU_2D_PRESENT_POLICY Policy;
    HDGFX_DMA_SURFACE Source;
    HDGFX_DMA_SURFACE Destination;
    HDGFX_DMA_WRITER Writer;
    SOFTGPU_CMD Command;
    UINT Index;
    NTSTATUS Status;

    Status = HdgfxResolveOwner(hContext, &KmdDevice, &Device);
    if (!NT_SUCCESS(Status))
        return Status;
    if (Device->Stopped)
        return STATUS_DELETE_PENDING;
    if (pPresent == NULL || pPresent->pDmaBuffer == NULL)
        return STATUS_INVALID_PARAMETER;
    if (pPresent->Flags.Flip && pPresent->FlipInterval > D3DDDI_FLIPINTERVAL_FOUR)
        return STATUS_INVALID_PARAMETER;
    Policy = SoftGpu2dPresentEvaluate(pPresent->Flags.Flip, pPresent->Flags.Blt, pPresent->Flags.ColorFill,
                                      pPresent->NumSrcAllocations, pPresent->NumDstAllocations);
    if (Policy == SoftGpu2dPresentInvalid ||
        ((pPresent->NumSrcAllocations != 0 || pPresent->NumDstAllocations != 0) && pPresent->pAllocationList == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }

    HdgfxWriterInit(&Writer, pPresent->pDmaBuffer, pPresent->DmaSize, pPresent->pPatchLocationListOut,
                    pPresent->PatchLocationListOutSize);
    if (Policy == SoftGpu2dPresentNoOp)
    {
        if (pPresent->NumSrcAllocations == 1)
        {
            Status = HdgfxValidateSurface(KmdDevice, (PSOFTGPU_OPENALLOC)
                         pPresent->pAllocationList[DXGK_PRESENT_SOURCE_INDEX].hDeviceSpecificAllocation);
            if (!NT_SUCCESS(Status))
                return Status;
        }
        if (!HdgfxWriterFits(&Writer, 0, 0))
            return STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER;
        HdgfxWriterRecord(&Writer, HDGFX_DMA_NOP, 0);
        pPresent->pDmaBuffer = Writer.Cursor;
        return STATUS_SUCCESS;
    }

    if (pPresent->SubRectCnt == 0 || pPresent->pDstSubRects == NULL)
        return STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER;
    if (pPresent->DstRect.left < 0 || pPresent->DstRect.top < 0 ||
        pPresent->DstRect.left >= pPresent->DstRect.right || pPresent->DstRect.top >= pPresent->DstRect.bottom ||
        (pPresent->Flags.Blt &&
         (pPresent->SrcRect.left < 0 || pPresent->SrcRect.top < 0 ||
          pPresent->SrcRect.left >= pPresent->SrcRect.right || pPresent->SrcRect.top >= pPresent->SrcRect.bottom)))
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (pPresent->Flags.Blt &&
        (pPresent->SrcRect.right - pPresent->SrcRect.left != pPresent->DstRect.right - pPresent->DstRect.left ||
         pPresent->SrcRect.bottom - pPresent->SrcRect.top != pPresent->DstRect.bottom - pPresent->DstRect.top))
    {
        return STATUS_NOT_SUPPORTED;
    }

    for (Index = 0; Index < pPresent->SubRectCnt; Index++)
    {
        CONST RECT *Rect = &pPresent->pDstSubRects[Index];

        if (Rect->left < pPresent->DstRect.left || Rect->top < pPresent->DstRect.top ||
            Rect->right > pPresent->DstRect.right || Rect->bottom > pPresent->DstRect.bottom)
        {
            return STATUS_INVALID_PARAMETER;
        }
        RtlZeroMemory(&Command, sizeof(Command));
        Command.Magic = SOFTGPU_CMD_MAGIC;
        Command.Size = sizeof(Command);
        Command.Color = pPresent->Color;
        Command.DstRect = *Rect;
        Status = HdgfxPresentSurface(KmdDevice, pPresent, DXGK_PRESENT_DESTINATION_INDEX, Rect, &Destination);
        if (!NT_SUCCESS(Status))
            return Status;
        Command.DstPitch = Destination.Open->Pitch;
        if (pPresent->Flags.Blt)
        {
            Command.Op = SOFTGPU_CMD_OP_BLT;
            Command.SrcRect.left = pPresent->SrcRect.left + (Rect->left - pPresent->DstRect.left);
            Command.SrcRect.top = pPresent->SrcRect.top + (Rect->top - pPresent->DstRect.top);
            Command.SrcRect.right = Command.SrcRect.left + (Rect->right - Rect->left);
            Command.SrcRect.bottom = Command.SrcRect.top + (Rect->bottom - Rect->top);
            Status = HdgfxPresentSurface(KmdDevice, pPresent, DXGK_PRESENT_SOURCE_INDEX, &Command.SrcRect, &Source);
            if (!NT_SUCCESS(Status))
                return Status;
            Command.SrcPitch = Source.Open->Pitch;
            Status = HdgfxEmitCommand(&Writer, Device, &Command, &Source, &Destination);
        }
        else
        {
            Command.Op = SOFTGPU_CMD_OP_FILL;
            Status = HdgfxEmitCommand(&Writer, Device, &Command, NULL, &Destination);
        }
        if (!NT_SUCCESS(Status))
            return Status;
    }
    HdgfxWriterClose(&Writer);
    pPresent->pDmaBuffer = Writer.Cursor;
    pPresent->pPatchLocationListOut = Writer.Patch;
    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
HdgfxDdiBuildPagingBuffer(
    _In_ PVOID MiniportDeviceContext,
    _Inout_ PDXGKARG_BUILDPAGINGBUFFER BuildPagingBuffer)
{
    PSOFTGPU_DEVICE Device = MiniportDeviceContext;
    ULONGLONG SlabBase;
    ULONGLONG SlabOffset;
    HDGFX_DMA_WRITER Writer;
    PSOFTGPU_CMD Command;
    PVOID SystemVa;
    PMDL Mdl;
    BOOLEAN ToSlab;

    if (Device == NULL || Device->Magic != SOFTGPU_DEVICE_MAGIC || BuildPagingBuffer == NULL ||
        BuildPagingBuffer->pDmaBuffer == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    SlabBase = (ULONGLONG)Device->FrameBufferPhys.QuadPart;
    HdgfxWriterInit(&Writer, BuildPagingBuffer->pDmaBuffer, BuildPagingBuffer->DmaSize, NULL, 0);
    if (!HdgfxWriterFits(&Writer, sizeof(SOFTGPU_CMD), 0))
        return STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER;

    switch (BuildPagingBuffer->Operation)
    {
        case DXGK_OPERATION_TRANSFER:
        {
            CONST DXGK_BUILDPAGINGBUFFER_TRANSFER *Transfer = &BuildPagingBuffer->Transfer;

            if (Transfer->TransferSize == 0)
                return STATUS_INVALID_PARAMETER;
            if (Transfer->Source.SegmentId == 0 && Transfer->Destination.SegmentId == SOFTGPU_SEGMENT_ID)
            {
                ToSlab = TRUE;
                Mdl = Transfer->Source.pMdl;
                SlabOffset = (ULONGLONG)Transfer->Destination.SegmentAddress.QuadPart;
            }
            else if (Transfer->Source.SegmentId == SOFTGPU_SEGMENT_ID && Transfer->Destination.SegmentId == 0)
            {
                ToSlab = FALSE;
                Mdl = Transfer->Destination.pMdl;
                SlabOffset = (ULONGLONG)Transfer->Source.SegmentAddress.QuadPart;
            }
            else
            {
                return STATUS_NOT_SUPPORTED;
            }
            if (Mdl == NULL || SlabOffset > Device->FrameBufferSize ||
                Transfer->TransferSize > Device->FrameBufferSize - SlabOffset)
            {
                return STATUS_INVALID_PARAMETER;
            }
            SystemVa = MmGetSystemAddressForMdlSafe(Mdl, NormalPagePriority);
            if (SystemVa == NULL)
                return STATUS_INSUFFICIENT_RESOURCES;
            Command = HdgfxWriterRecord(&Writer, HDGFX_DMA_CPU, sizeof(SOFTGPU_CMD));
            Command->Op = SOFTGPU_CMD_OP_PAGE;
            Command->Flags = ToSlab ? SOFTGPU_CMD_FLAG_TO_SLAB : 0;
            Command->SlabAddress = SlabBase + SlabOffset;
            Command->SystemAddress = (ULONGLONG)(ULONG_PTR)SystemVa + Transfer->MdlOffset;
            Command->ByteCount = Transfer->TransferSize;
            break;
        }

        case DXGK_OPERATION_FILL:
        {
            CONST DXGK_BUILDPAGINGBUFFER_FILL *Fill = &BuildPagingBuffer->Fill;

            if (Fill->Destination.SegmentId != SOFTGPU_SEGMENT_ID || Fill->FillSize == 0)
                return STATUS_NOT_SUPPORTED;
            SlabOffset = (ULONGLONG)Fill->Destination.SegmentAddress.QuadPart;
            if (SlabOffset > Device->FrameBufferSize || Fill->FillSize > Device->FrameBufferSize - SlabOffset)
                return STATUS_INVALID_PARAMETER;
            Command = HdgfxWriterRecord(&Writer, HDGFX_DMA_CPU, sizeof(SOFTGPU_CMD));
            Command->Op = SOFTGPU_CMD_OP_FILL_LINEAR;
            Command->SlabAddress = SlabBase + SlabOffset;
            Command->ByteCount = Fill->FillSize;
            Command->Color = Fill->FillPattern;
            break;
        }

        case DXGK_OPERATION_DISCARD_CONTENT:
        case DXGK_OPERATION_NOTIFY_RESIDENCY:
            HdgfxWriterRecord(&Writer, HDGFX_DMA_NOP, 0);
            break;

        default:
            return STATUS_NOT_SUPPORTED;
    }
    BuildPagingBuffer->pDmaBuffer = Writer.Cursor;
    BuildPagingBuffer->MultipassOffset = 0;
    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
HdgfxDdiPatch(
    _In_ PVOID MiniportDeviceContext,
    _In_ CONST DXGKARG_PATCH *Patch)
{
    PSOFTGPU_DEVICE Device = MiniportDeviceContext;
    PSOFTGPU_KMD_DEVICE KmdDevice;
    UINT Index;

    if (Device == NULL || Device->Magic != SOFTGPU_DEVICE_MAGIC || Patch == NULL)
        return STATUS_INVALID_PARAMETER;
    KmdDevice = (PSOFTGPU_KMD_DEVICE)Patch->hDevice;
    if (KmdDevice == NULL || KmdDevice->Magic != SOFTGPU_KMD_DEVICE_MAGIC || KmdDevice->Adapter != Device ||
        KmdDevice->Process == NULL || KmdDevice->Process->Magic != SOFTGPU_PROCESS_MAGIC ||
        KmdDevice->Process->Adapter != Device)
    {
        return STATUS_INVALID_HANDLE;
    }
    if (Patch->PatchLocationListSubmissionLength == 0)
        return STATUS_SUCCESS;
    if (Patch->pPatchLocationList == NULL || Patch->pDmaBuffer == NULL || Patch->pAllocationList == NULL ||
        Patch->DmaBufferSubmissionStartOffset > Patch->DmaBufferSubmissionEndOffset ||
        Patch->DmaBufferSubmissionEndOffset > Patch->DmaBufferSize ||
        Patch->PatchLocationListSubmissionStart > Patch->PatchLocationListSize ||
        Patch->PatchLocationListSubmissionLength >
            Patch->PatchLocationListSize - Patch->PatchLocationListSubmissionStart)
    {
        return STATUS_INVALID_PARAMETER;
    }

    for (Index = Patch->PatchLocationListSubmissionStart;
         Index < Patch->PatchLocationListSubmissionStart + Patch->PatchLocationListSubmissionLength;
         Index++)
    {
        CONST D3DDDI_PATCHLOCATIONLIST *Entry = &Patch->pPatchLocationList[Index];
        CONST DXGK_ALLOCATIONLIST *Allocation;
        PSOFTGPU_OPENALLOC Open;
        PUCHAR Location;
        ULONG Width = Entry->DriverId == HDGFX_PATCH_PHYSICAL ? sizeof(ULONGLONG) : sizeof(ULONG);
        ULONGLONG Value;
        NTSTATUS Status;

        if (Entry->AllocationIndex >= Patch->AllocationListSize ||
            (Entry->DriverId != HDGFX_PATCH_GTT && Entry->DriverId != HDGFX_PATCH_PHYSICAL) ||
            Entry->PatchOffset < Patch->DmaBufferSubmissionStartOffset ||
            Entry->PatchOffset > Patch->DmaBufferSubmissionEndOffset ||
            Width > Patch->DmaBufferSubmissionEndOffset - Entry->PatchOffset)
        {
            return STATUS_INVALID_PARAMETER;
        }
        Allocation = &Patch->pAllocationList[Entry->AllocationIndex];
        Open = (PSOFTGPU_OPENALLOC)Allocation->hDeviceSpecificAllocation;
        Status = HdgfxValidateOpen(KmdDevice, Open);
        if (!NT_SUCCESS(Status))
            return Status;
        if (Allocation->SegmentId == 0)
            return STATUS_GRAPHICS_ALLOCATION_BUSY;
        Status = HdgfxPlacement(Device, Allocation, Open, Entry->AllocationOffset, Entry->DriverId, &Value);
        if (!NT_SUCCESS(Status))
            return Status;
        Location = (PUCHAR)Patch->pDmaBuffer + Entry->PatchOffset;
        if (Width == sizeof(ULONGLONG))
            *(ULONGLONG UNALIGNED *)Location = Value;
        else
            *(ULONG UNALIGNED *)Location = (ULONG)Value;
    }
    return STATUS_SUCCESS;
}

static volatile LONG64 *
HdgfxSlabFence(
    _In_ PSOFTGPU_DEVICE Device,
    _In_ ULONGLONG Address)
{
    SIZE_T Offset;

    if (Address % sizeof(UINT64) != 0 ||
        !NT_SUCCESS(SoftGpu2dResolveSlabRange((ULONGLONG)Device->FrameBufferPhys.QuadPart, Device->FrameBufferSize,
                                              Address, sizeof(UINT64), &Offset)))
    {
        return NULL;
    }
    return (volatile LONG64 *)((PUCHAR)Device->FrameBuffer + Offset);
}

static PUCHAR
HdgfxSlabRange(
    _In_ PSOFTGPU_DEVICE Device,
    _In_ ULONGLONG Address,
    _In_ ULONGLONG Length,
    _Out_ PSIZE_T Remaining)
{
    SIZE_T Offset;

    *Remaining = 0;
    if (!NT_SUCCESS(SoftGpu2dResolveSlabRange((ULONGLONG)Device->FrameBufferPhys.QuadPart, Device->FrameBufferSize,
                                              Address, Length, &Offset)))
    {
        return NULL;
    }
    *Remaining = Device->FrameBufferSize - Offset;
    return (PUCHAR)Device->FrameBuffer + Offset;
}

static BOOLEAN
HdgfxRingIdle(
    _Inout_ PHDGFX_ADAPTER Adapter)
{
    HDGFX_MMIO Mmio;
    BOOLEAN Idle;

    KeAcquireSpinLockAtDpcLevel(&Adapter->RingLock);
    if (Adapter->RingUnfenced && !Adapter->RingResetting)
    {
        HdgfxMmioInit(Adapter->Registers, &Mmio);
        HdgfxRingFence(Adapter, &Mmio, HdgfxIlkRingHead(&Mmio));
    }
    Idle = !Adapter->RingStarted ||
           (!Adapter->RingUnfenced &&
            ((volatile ULONG *)Adapter->StatusPage)[HDGFX_RING_SEQNO_INDEX] == Adapter->Seqno);
    KeReleaseSpinLockFromDpcLevel(&Adapter->RingLock);
    return Idle;
}

static HDGFX_QUEUE_STATE
HdgfxRunCpu(
    _Inout_ PHDGFX_ADAPTER Adapter,
    _In_ CONST SOFTGPU_CMD *Command)
{
    PSOFTGPU_DEVICE Device = Adapter->Device;
    volatile LONG64 *Fence;
    PUCHAR Source;
    PUCHAR Destination;
    SIZE_T SourceSize;
    SIZE_T DestinationSize;
    SIZE_T Index;
    NTSTATUS Status = STATUS_SUCCESS;

    if (!HdgfxRingIdle(Adapter))
        return HdgfxQueueRing;

    switch (Command->Op)
    {
        case SOFTGPU_CMD_OP_WAIT_FENCE:
            Fence = HdgfxSlabFence(Device, Command->DstAddress);
            if (Fence == NULL)
                Status = STATUS_INVALID_ADDRESS;
            else if ((ULONGLONG)ReadAcquire64(Fence) < Command->FenceValue)
                return HdgfxQueueWait;
            break;

        case SOFTGPU_CMD_OP_SIGNAL_FENCE:
            Fence = HdgfxSlabFence(Device, Command->DstAddress);
            if (Fence == NULL)
                Status = STATUS_INVALID_ADDRESS;
            else
                WriteRelease64(Fence, (LONG64)Command->FenceValue);
            break;

        case SOFTGPU_CMD_OP_BLT:
            Source = HdgfxSlabRange(Device, Command->SrcAddress, 1, &SourceSize);
            Destination = HdgfxSlabRange(Device, Command->DstAddress, 1, &DestinationSize);
            Status = Source == NULL || Destination == NULL ? STATUS_INVALID_ADDRESS :
                     SoftGpu2dCopyRect(Source, SourceSize, Command->SrcPitch, &Command->SrcRect, Destination,
                                       DestinationSize, Command->DstPitch, &Command->DstRect);
            break;

        case SOFTGPU_CMD_OP_FILL:
            Destination = HdgfxSlabRange(Device, Command->DstAddress, 1, &DestinationSize);
            Status = Destination == NULL ? STATUS_INVALID_ADDRESS :
                     SoftGpu2dFillRect(Destination, DestinationSize, Command->DstPitch, &Command->DstRect,
                                       Command->Color);
            break;

        case SOFTGPU_CMD_OP_PAGE:
            Destination = HdgfxSlabRange(Device, Command->SlabAddress, Command->ByteCount, &DestinationSize);
            if (Destination == NULL || Command->SystemAddress == 0)
                Status = STATUS_INVALID_ADDRESS;
            else if (Command->Flags & SOFTGPU_CMD_FLAG_TO_SLAB)
                RtlCopyMemory(Destination, (PVOID)(ULONG_PTR)Command->SystemAddress, (SIZE_T)Command->ByteCount);
            else
                RtlCopyMemory((PVOID)(ULONG_PTR)Command->SystemAddress, Destination, (SIZE_T)Command->ByteCount);
            break;

        case SOFTGPU_CMD_OP_FILL_LINEAR:
            Destination = HdgfxSlabRange(Device, Command->SlabAddress, Command->ByteCount, &DestinationSize);
            if (Destination == NULL || Command->ByteCount % sizeof(ULONG) != 0)
            {
                Status = STATUS_INVALID_ADDRESS;
                break;
            }
            for (Index = 0; Index < (SIZE_T)(Command->ByteCount / sizeof(ULONG)); Index++)
                ((PULONG)Destination)[Index] = Command->Color;
            break;

        default:
            Status = STATUS_ILLEGAL_INSTRUCTION;
            break;
    }
    if (!NT_SUCCESS(Status))
        DPRINT1("CPU operation %lu failed 0x%08lx, skipped\n", Command->Op, Status);
    KeMemoryBarrier();
    return HdgfxQueueDone;
}

static HDGFX_QUEUE_STATE
HdgfxRunBatch(
    _Inout_ PHDGFX_ADAPTER Adapter,
    _In_reads_(Count) CONST ULONG *Dwords,
    _In_ ULONG Count)
{
    HDGFX_QUEUE_STATE State = HdgfxQueueDone;
    HDGFX_MMIO Mmio;
    ULONG Bytes = Count * sizeof(ULONG);
    ULONG Padding;
    ULONG Head;

    if (Count == 0)
        return HdgfxQueueDone;
    KeAcquireSpinLockAtDpcLevel(&Adapter->RingLock);
    if (!Adapter->RingStarted || Adapter->RingResetting)
    {
        KeReleaseSpinLockFromDpcLevel(&Adapter->RingLock);
        DPRINT1("Render ring stopped, dropping a %lu-dword batch\n", Count);
        return HdgfxQueueDone;
    }
    HdgfxMmioInit(Adapter->Registers, &Mmio);
    Head = HdgfxIlkRingHead(&Mmio);
    Padding = Adapter->Ring.Size - Adapter->Ring.Tail < Bytes ? Adapter->Ring.Size - Adapter->Ring.Tail : 0;
    if (HdgfxRingFree(&Adapter->Ring, Head) < HDGFX_RING_RESERVE + Padding + Bytes)
    {
        if (Adapter->RingUnfenced)
            HdgfxRingFence(Adapter, &Mmio, Head);
        State = HdgfxQueueRing;
    }
    else if (HdgfxRingEmit(&Adapter->Ring, Head, (const uint32_t *)Dwords, Count))
    {
        Adapter->RingUnfenced = TRUE;
        HdgfxIlkAdvanceRing(&Mmio, &Adapter->Ring);
    }
    else
    {
        DPRINT1("Render ring refused a %lu-dword batch, dropping it\n", Count);
    }
    KeReleaseSpinLockFromDpcLevel(&Adapter->RingLock);
    return State;
}

static HDGFX_QUEUE_STATE
HdgfxRunFence(
    _Inout_ PHDGFX_ADAPTER Adapter,
    _In_ ULONG Fence,
    _Out_ PBOOLEAN Software)
{
    uint32_t Flush[2] = { 0, 0 };
    HDGFX_MMIO Mmio;
    ULONG Head;

    *Software = FALSE;
    if (!Adapter->RingStarted || !Adapter->RingInterrupt)
    {
        if (!HdgfxRingIdle(Adapter))
            return HdgfxQueueRing;
        Adapter->SoftwareFence = Fence;
        *Software = TRUE;
        return HdgfxQueueDone;
    }
    KeAcquireSpinLockAtDpcLevel(&Adapter->RingLock);
    if (Adapter->RingResetting)
    {
        KeReleaseSpinLockFromDpcLevel(&Adapter->RingLock);
        return HdgfxQueueRing;
    }
    HdgfxMmioInit(Adapter->Registers, &Mmio);
    Head = HdgfxIlkRingHead(&Mmio);
    if (HdgfxRingFree(&Adapter->Ring, Head) < HDGFX_RING_RESERVE)
    {
        KeReleaseSpinLockFromDpcLevel(&Adapter->RingLock);
        return HdgfxQueueRing;
    }
    HdgfxMiFlush(Flush);
    if (!HdgfxRingEmit(&Adapter->Ring, Head, Flush, RTL_NUMBER_OF(Flush)) ||
        !HdgfxRingEmitBreadcrumb(&Adapter->Ring, Head, HDGFX_RING_FENCE_INDEX, Fence) ||
        !HdgfxRingFence(Adapter, &Mmio, Head))
    {
        KeReleaseSpinLockFromDpcLevel(&Adapter->RingLock);
        return HdgfxQueueRing;
    }
    KeReleaseSpinLockFromDpcLevel(&Adapter->RingLock);
    return HdgfxQueueDone;
}

static HDGFX_QUEUE_STATE
HdgfxRunSubmit(
    _Inout_ PHDGFX_ADAPTER Adapter,
    _Inout_ PHDGFX_SUBMIT Submit,
    _Out_ PBOOLEAN Software)
{
    HDGFX_QUEUE_STATE State = HdgfxQueueDone;
    PHDGFX_DMA_RECORD Record;
    PUCHAR Map = NULL;

    *Software = FALSE;
    if (Submit->Offset < Submit->EndOffset)
    {
        Map = MmMapIoSpace(Submit->DmaPhysical, Submit->EndOffset, MmCached);
        if (Map == NULL)
            return HdgfxQueueWait;
    }
    while (Submit->Offset < Submit->EndOffset)
    {
        Record = (PHDGFX_DMA_RECORD)(Map + Submit->Offset);
        if (Submit->EndOffset - Submit->Offset < sizeof(*Record) || Record->Size < sizeof(*Record) ||
            Record->Size > Submit->EndOffset - Submit->Offset || Record->Size % 8 != 0 ||
            (Record->Type == HDGFX_DMA_CPU && Record->Size != sizeof(*Record) + sizeof(SOFTGPU_CMD)))
        {
            DPRINT1("Malformed DMA record at 0x%lx of fence %lu, skipping the rest\n", Submit->Offset, Submit->Fence);
            Submit->Offset = Submit->EndOffset;
            break;
        }
        if (Record->Type == HDGFX_DMA_BATCH)
            State = HdgfxRunBatch(Adapter, (PULONG)(Record + 1), (Record->Size - sizeof(*Record)) / sizeof(ULONG));
        else if (Record->Type == HDGFX_DMA_CPU)
            State = HdgfxRunCpu(Adapter, (PSOFTGPU_CMD)(Record + 1));
        if (State != HdgfxQueueDone)
            break;
        Submit->Offset += Record->Size;
    }
    if (Map != NULL)
        MmUnmapIoSpace(Map, Submit->EndOffset);
    if (State != HdgfxQueueDone)
        return State;
    return HdgfxRunFence(Adapter, Submit->Fence, Software);
}

static BOOLEAN
NTAPI
HdgfxCompleteSoftware(
    _In_opt_ PVOID Context)
{
    PHDGFX_ADAPTER Adapter = Context;
    PSOFTGPU_DEVICE Device = Adapter->Device;
    DXGKARGCB_NOTIFY_INTERRUPT_DATA Notify;

    if (Adapter->QueueResetting || (LONG)(Adapter->SoftwareFence - Adapter->CompletedFence) <= 0)
        return FALSE;
    Adapter->CompletedFence = Adapter->SoftwareFence;
    RtlZeroMemory(&Notify, sizeof(Notify));
    Notify.InterruptType = DXGK_INTERRUPT_TYPE_DMA_COMPLETED;
    Notify.DmaCompleted.SubmissionFenceId = Adapter->SoftwareFence;
    Device->DxgkInterface.DxgkCbNotifyInterrupt(Device->DxgkInterface.DeviceHandle, &Notify);
    return TRUE;
}

static VOID
NTAPI
HdgfxQueueRoutine(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    PHDGFX_ADAPTER Adapter = DeferredContext;
    PSOFTGPU_DEVICE Device = Adapter->Device;
    HDGFX_QUEUE_STATE State = HdgfxQueueDone;
    LARGE_INTEGER Due;
    BOOLEAN Software = FALSE;
    BOOLEAN Notified = FALSE;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    if (!ExAcquireRundownProtection(&Adapter->Rundown))
        return;
    KeAcquireSpinLockAtDpcLevel(&Adapter->QueueLock);
    while (!Adapter->QueueStopped && !Adapter->QueueResetting && Adapter->QueueHead != Adapter->QueueTail)
    {
        BOOLEAN Completed;

        State = HdgfxRunSubmit(Adapter, &Adapter->Queue[Adapter->QueueHead % HDGFX_QUEUE_SIZE], &Completed);
        if (State != HdgfxQueueDone)
            break;
        Software |= Completed;
        Adapter->QueueHead++;
    }
    if (State != HdgfxQueueDone)
    {
        Due.QuadPart = HDGFX_QUEUE_POLL;
        KeSetTimer(&Adapter->QueueTimer, Due, &Adapter->QueueDpc);
    }
    KeReleaseSpinLockFromDpcLevel(&Adapter->QueueLock);

    if (Software && Device->DxgkInterface.DxgkCbSynchronizeExecution != NULL)
    {
        Device->DxgkInterface.DxgkCbSynchronizeExecution(Device->DxgkInterface.DeviceHandle, HdgfxCompleteSoftware,
                                                         Adapter, 0, &Notified);
        if (Notified)
            Device->DxgkInterface.DxgkCbNotifyDpc(Device->DxgkInterface.DeviceHandle);
    }
    ExReleaseRundownProtection(&Adapter->Rundown);
}

VOID
HdgfxQueueStart(
    _Inout_ PHDGFX_ADAPTER Adapter)
{
    KeInitializeDpc(&Adapter->QueueDpc, HdgfxQueueRoutine, Adapter);
    KeInitializeTimer(&Adapter->QueueTimer);
    Adapter->QueueHead = 0;
    Adapter->QueueTail = 0;
    Adapter->CompletedFence = Adapter->SubmittedFence;
    Adapter->QueueResetting = FALSE;
    Adapter->QueueStopped = FALSE;
}

VOID
HdgfxQueueStop(
    _Inout_ PHDGFX_ADAPTER Adapter)
{
    KIRQL OldIrql;

    KeAcquireSpinLock(&Adapter->QueueLock, &OldIrql);
    Adapter->QueueStopped = TRUE;
    Adapter->QueueHead = Adapter->QueueTail;
    KeReleaseSpinLock(&Adapter->QueueLock, OldIrql);
    KeCancelTimer(&Adapter->QueueTimer);
    KeRemoveQueueDpc(&Adapter->QueueDpc);
    KeFlushQueuedDpcs();
}

VOID
HdgfxQueueInterrupt(
    _Inout_ PHDGFX_ADAPTER Adapter)
{
    PSOFTGPU_DEVICE Device = Adapter->Device;
    DXGKARGCB_NOTIFY_INTERRUPT_DATA Notify;
    ULONG Fence = ((volatile ULONG *)Adapter->StatusPage)[HDGFX_RING_FENCE_INDEX];

    if (Adapter->QueueResetting || (LONG)(Fence - Adapter->CompletedFence) <= 0)
        return;
    Adapter->CompletedFence = Fence;
    RtlZeroMemory(&Notify, sizeof(Notify));
    Notify.InterruptType = DXGK_INTERRUPT_TYPE_DMA_COMPLETED;
    Notify.DmaCompleted.SubmissionFenceId = Fence;
    Device->DxgkInterface.DxgkCbNotifyInterrupt(Device->DxgkInterface.DeviceHandle, &Notify);
    InterlockedExchange(&Adapter->FenceNotified, 1);
}

VOID
HdgfxQueueDpc(
    _Inout_ PHDGFX_ADAPTER Adapter)
{
    PSOFTGPU_DEVICE Device = Adapter->Device;

    KeInsertQueueDpc(&Adapter->QueueDpc, NULL, NULL);
    if (InterlockedExchange(&Adapter->FenceNotified, 0) != 0)
        Device->DxgkInterface.DxgkCbNotifyDpc(Device->DxgkInterface.DeviceHandle);
}

NTSTATUS
APIENTRY
HdgfxDdiSubmitCommand(
    _In_ PVOID MiniportDeviceContext,
    _In_ CONST DXGKARG_SUBMITCOMMAND *SubmitCommand)
{
    PSOFTGPU_DEVICE Device = MiniportDeviceContext;
    PHDGFX_ADAPTER Adapter;
    PSOFTGPU_KMD_DEVICE KmdDevice;
    PHDGFX_SUBMIT Submit;
    NTSTATUS Status = STATUS_SUCCESS;
    KIRQL OldIrql;

    if (Device == NULL || Device->Magic != SOFTGPU_DEVICE_MAGIC || SubmitCommand == NULL ||
        SubmitCommand->NodeOrdinal != 0 || SubmitCommand->EngineOrdinal != 0)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Adapter = Device->PlatformServicesContext;
    if (Adapter == NULL)
        return STATUS_DEVICE_NOT_READY;
    KmdDevice = (PSOFTGPU_KMD_DEVICE)SubmitCommand->hDevice;
    if (KmdDevice == NULL || KmdDevice->Magic != SOFTGPU_KMD_DEVICE_MAGIC || KmdDevice->Adapter != Device ||
        KmdDevice->Process == NULL || KmdDevice->Process->Magic != SOFTGPU_PROCESS_MAGIC ||
        KmdDevice->Process->Adapter != Device)
    {
        return STATUS_INVALID_HANDLE;
    }
    if (!SubmitCommand->Flags.NullRendering &&
        (SubmitCommand->DmaBufferPhysicalAddress.QuadPart == 0 ||
         SubmitCommand->DmaBufferSubmissionEndOffset <= SubmitCommand->DmaBufferSubmissionStartOffset))
    {
        return STATUS_INVALID_PARAMETER;
    }

    KeAcquireSpinLock(&Adapter->QueueLock, &OldIrql);
    if (Device->Stopped || Adapter->QueueStopped)
    {
        Status = STATUS_DELETE_PENDING;
    }
    else if (Adapter->QueueTail - Adapter->QueueHead >= HDGFX_QUEUE_SIZE)
    {
        Status = STATUS_DEVICE_BUSY;
    }
    else
    {
        Submit = &Adapter->Queue[Adapter->QueueTail % HDGFX_QUEUE_SIZE];
        Submit->DmaPhysical = SubmitCommand->DmaBufferPhysicalAddress;
        Submit->EndOffset = SubmitCommand->DmaBufferSubmissionEndOffset;
        Submit->Offset = SubmitCommand->Flags.NullRendering ? Submit->EndOffset :
                         SubmitCommand->DmaBufferSubmissionStartOffset;
        Submit->Fence = SubmitCommand->SubmissionFenceId;
        Adapter->SubmittedFence = Submit->Fence;
        Adapter->QueueTail++;
        KeInsertQueueDpc(&Adapter->QueueDpc, NULL, NULL);
    }
    KeReleaseSpinLock(&Adapter->QueueLock, OldIrql);
    return Status;
}

NTSTATUS
APIENTRY
HdgfxDdiQueryCurrentFence(
    _In_ PVOID MiniportDeviceContext,
    _Inout_ PDXGKARG_QUERYCURRENTFENCE pCurrentFence)
{
    PSOFTGPU_DEVICE Device = MiniportDeviceContext;
    PHDGFX_ADAPTER Adapter;

    if (Device == NULL || Device->Magic != SOFTGPU_DEVICE_MAGIC || pCurrentFence == NULL ||
        pCurrentFence->NodeOrdinal != 0 || pCurrentFence->EngineOrdinal != 0)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Adapter = Device->PlatformServicesContext;
    pCurrentFence->CurrentFence = Adapter != NULL ? Adapter->CompletedFence : 0;
    return STATUS_SUCCESS;
}

static BOOLEAN
NTAPI
HdgfxResetFences(
    _In_opt_ PVOID Context)
{
    PHDGFX_ADAPTER Adapter = Context;

    Adapter->CompletedFence = Adapter->SubmittedFence;
    return TRUE;
}

static BOOLEAN
NTAPI
HdgfxRestartFences(
    _In_opt_ PVOID Context)
{
    PHDGFX_ADAPTER Adapter = Context;

    if (Adapter->StatusPage != NULL)
        ((volatile ULONG *)Adapter->StatusPage)[HDGFX_RING_FENCE_INDEX] = Adapter->CompletedFence;
    Adapter->QueueResetting = FALSE;
    return TRUE;
}

NTSTATUS
APIENTRY
HdgfxDdiResetFromTimeout(
    _In_ PVOID MiniportDeviceContext)
{
    PSOFTGPU_DEVICE Device = MiniportDeviceContext;
    PHDGFX_ADAPTER Adapter;
    BOOLEAN Ignored;
    KIRQL OldIrql;

    PAGED_CODE();

    if (Device == NULL || Device->Magic != SOFTGPU_DEVICE_MAGIC)
        return STATUS_INVALID_PARAMETER;
    Adapter = Device->PlatformServicesContext;
    if (Adapter == NULL)
        return STATUS_SUCCESS;

    KeAcquireSpinLock(&Adapter->QueueLock, &OldIrql);
    Adapter->QueueResetting = TRUE;
    Adapter->QueueHead = Adapter->QueueTail;
    KeReleaseSpinLock(&Adapter->QueueLock, OldIrql);
    KeCancelTimer(&Adapter->QueueTimer);
    KeRemoveQueueDpc(&Adapter->QueueDpc);
    KeFlushQueuedDpcs();
    Device->DxgkInterface.DxgkCbSynchronizeExecution(Device->DxgkInterface.DeviceHandle, HdgfxResetFences, Adapter, 0,
                                                     &Ignored);
    DPRINT1("Timeout reset dropped the queue at fence %lu\n", Adapter->CompletedFence);
    return HdgfxResetRender(Adapter);
}

NTSTATUS
APIENTRY
HdgfxDdiRestartFromTimeout(
    _In_ PVOID MiniportDeviceContext)
{
    PSOFTGPU_DEVICE Device = MiniportDeviceContext;
    PHDGFX_ADAPTER Adapter;
    BOOLEAN Ignored;

    PAGED_CODE();

    if (Device == NULL || Device->Magic != SOFTGPU_DEVICE_MAGIC)
        return STATUS_INVALID_PARAMETER;
    Adapter = Device->PlatformServicesContext;
    if (Adapter == NULL)
        return STATUS_SUCCESS;

    HdgfxRestartRender(Adapter);
    Device->DxgkInterface.DxgkCbSynchronizeExecution(Device->DxgkInterface.DeviceHandle, HdgfxRestartFences, Adapter,
                                                     0, &Ignored);
    KeInsertQueueDpc(&Adapter->QueueDpc, NULL, NULL);
    return STATUS_SUCCESS;
}
