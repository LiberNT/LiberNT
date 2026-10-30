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
#define HDGFX_DMA_GTT               4UL
#define HDGFX_DMA_USER              5UL
#define HDGFX_PATCH_GTT             0UL
#define HDGFX_PATCH_PHYSICAL        1UL
#define HDGFX_BATCH_LIMIT           0x2000UL
#define HDGFX_BLT_DWORDS            8UL
#define HDGFX_QUEUE_POLL            (-10000LL)
#define HDGFX_DEAD_CONTEXT_MAGIC    0xDEAD4346UL
#define HDGFX_APERTURE_CPU          (1ULL << 63)

typedef struct _HDGFX_DMA_RECORD
{
    ULONG Type;
    ULONG Size;
} HDGFX_DMA_RECORD, *PHDGFX_DMA_RECORD;

typedef struct _HDGFX_USER_BATCH
{
    ULONG BatchOffset;
    ULONG StateOffset;
} HDGFX_USER_BATCH, *PHDGFX_USER_BATCH;

typedef struct _HDGFX_GTT_UPDATE
{
    ULONG First;
    ULONG Count;
    ULONG Fill;
    ULONG Reserved;
} HDGFX_GTT_UPDATE, *PHDGFX_GTT_UPDATE;

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
    PHDGFX_OPEN Open;
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
    _Out_ PHDGFX_DEVICE *KmdDeviceOut,
    _Out_ PHDGFX_ADAPTER *AdapterOut)
{
    PHDGFX_CONTEXT Context = Handle;
    PHDGFX_DEVICE KmdDevice;
    PHDGFX_ADAPTER Adapter;

    *KmdDeviceOut = NULL;
    *AdapterOut = NULL;
    if (Handle == NULL)
        return STATUS_INVALID_HANDLE;
    if (Context->Magic == HDGFX_CONTEXT_MAGIC)
    {
        KmdDevice = Context->Device;
        if (KmdDevice == NULL || KmdDevice->Magic != HDGFX_DEVICE_MAGIC ||
            Context->Process == NULL || Context->Process != KmdDevice->Process)
        {
            return STATUS_INVALID_HANDLE;
        }
    }
    else
    {
        KmdDevice = Handle;
        if (KmdDevice->Magic != HDGFX_DEVICE_MAGIC)
            return STATUS_INVALID_HANDLE;
    }

    Adapter = KmdDevice->Adapter;
    if (Adapter == NULL || Adapter->Magic != HDGFX_ADAPTER_MAGIC ||
        KmdDevice->Process == NULL || KmdDevice->Process->Magic != HDGFX_PROCESS_MAGIC ||
        KmdDevice->Process->Adapter != Adapter)
    {
        return STATUS_INVALID_HANDLE;
    }
    *KmdDeviceOut = KmdDevice;
    *AdapterOut = Adapter;
    return STATUS_SUCCESS;
}

static NTSTATUS
HdgfxValidateOpen(
    _In_ PHDGFX_DEVICE KmdDevice,
    _In_ PHDGFX_OPEN Open)
{
    if (Open == NULL || Open->Magic != HDGFX_OPEN_MAGIC || Open->Device != KmdDevice)
        return STATUS_INVALID_HANDLE;
    if (Open->Size == 0 || Open->Size > HdgfxMaxAllocationSize(KmdDevice->Adapter, FALSE))
        return STATUS_INVALID_BUFFER_SIZE;
    return STATUS_SUCCESS;
}

static NTSTATUS
HdgfxValidateSurface(
    _In_ PHDGFX_DEVICE KmdDevice,
    _In_ PHDGFX_OPEN Open)
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
    _In_ PHDGFX_ADAPTER Adapter,
    _In_ CONST DXGK_ALLOCATIONLIST *Allocation,
    _In_ PHDGFX_OPEN Open,
    _In_ UINT AllocationOffset,
    _In_ UINT DriverId,
    _Out_ PULONGLONG Value)
{
    PHDGFX_SCANOUT Scanout = Adapter->Scanout;
    ULONGLONG Address;
    SIZE_T Offset;
    NTSTATUS Status;

    *Value = 0;
    if (AllocationOffset >= Open->Size)
        return STATUS_INVALID_PARAMETER;
    if (Allocation->SegmentId == 0)
        return STATUS_SUCCESS;
    if (Allocation->SegmentId == HDGFX_APERTURE_SEGMENT_ID && Adapter->ApertureSize != 0)
    {
        Address = (ULONGLONG)Allocation->PhysicalAddress.QuadPart + AllocationOffset;
        if (Address < Adapter->ApertureBase || Address - Adapter->ApertureBase >= Adapter->ApertureSize ||
            Open->Size - AllocationOffset > Adapter->ApertureSize - (Address - Adapter->ApertureBase))
        {
            return STATUS_INVALID_ADDRESS;
        }
        *Value = DriverId == HDGFX_PATCH_PHYSICAL ? Address | HDGFX_APERTURE_CPU : Address;
        return STATUS_SUCCESS;
    }
    if (Allocation->SegmentId != HDGFX_SLAB_SEGMENT_ID || Allocation->PhysicalAddress.QuadPart <= 0)
        return STATUS_INVALID_ADDRESS;

    Address = (ULONGLONG)Allocation->PhysicalAddress.QuadPart + AllocationOffset;
    Status = SoftGpu2dResolveSlabRange((ULONGLONG)Adapter->SlabPhysical.QuadPart, Adapter->SlabSize, Address,
                                       Open->Size - AllocationOffset, &Offset);
    if (!NT_SUCCESS(Status))
        return Status;
    if (DriverId == HDGFX_PATCH_PHYSICAL)
    {
        *Value = Address;
        return STATUS_SUCCESS;
    }
    if (Scanout == NULL || Scanout->SlabGtt == NULL || Scanout->SlabPhysical != (ULONGLONG)Adapter->SlabPhysical.QuadPart)
        return STATUS_INVALID_ADDRESS;
    *Value = Scanout->SlabAddress + (ULONGLONG)Offset;
    return STATUS_SUCCESS;
}

static BOOLEAN
HdgfxBlitterUsable(
    _In_ PHDGFX_ADAPTER Adapter)
{
    PHDGFX_SCANOUT Scanout = Adapter->Scanout;

    return Scanout != NULL && Adapter->RingStarted && Adapter->RingInterrupt &&
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
    Writer->Cursor += HdgfxMiBatchEnd((uint32_t *)Writer->Cursor) * sizeof(ULONG);
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
    return (SIZE_T)(Writer->End - Writer->Cursor) >= sizeof(HDGFX_DMA_RECORD) + Bytes + 2 * sizeof(ULONG) &&
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
    _In_ PHDGFX_OPEN Open)
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
    _In_ PHDGFX_ADAPTER Adapter,
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

    if ((Command->Op == SOFTGPU_CMD_OP_FILL || Command->Op == SOFTGPU_CMD_OP_BLT) && HdgfxBlitterUsable(Adapter) &&
        !(Source != NULL && Source->Open == Destination->Open && HdgfxRectsOverlap(Command, Source->Open) &&
          Source->Open->Pitch % HDGFX_BLT_OVERLAP_PITCH != 0))
    {
        Rect.Left = (uint32_t)Command->DstRect.left;
        Rect.Top = (uint32_t)Command->DstRect.top;
        Rect.Right = (uint32_t)Command->DstRect.right;
        Rect.Bottom = (uint32_t)Command->DstRect.bottom;
        Status = HdgfxPlacement(Adapter, Destination->Allocation, Destination->Open, 0, HDGFX_PATCH_GTT, &Value);
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
            Status = HdgfxPlacement(Adapter, Source->Allocation, Source->Open, 0, HDGFX_PATCH_GTT, &Value);
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
        Status = HdgfxPlacement(Adapter, Source->Allocation, Source->Open, 0, HDGFX_PATCH_PHYSICAL, &Cpu->SrcAddress);
        if (!NT_SUCCESS(Status))
            return Status;
        HdgfxWriterPatch(Writer, &Cpu->SrcAddress, Source, HDGFX_PATCH_PHYSICAL);
    }
    Status = HdgfxPlacement(Adapter, Destination->Allocation, Destination->Open, 0, HDGFX_PATCH_PHYSICAL,
                            &Cpu->DstAddress);
    if (!NT_SUCCESS(Status))
        return Status;
    HdgfxWriterPatch(Writer, &Cpu->DstAddress, Destination, HDGFX_PATCH_PHYSICAL);
    return STATUS_SUCCESS;
}

static NTSTATUS
HdgfxRenderSurface(
    _In_ PHDGFX_DEVICE KmdDevice,
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
    Surface->Open = (PHDGFX_OPEN)Surface->Allocation->hDeviceSpecificAllocation;
    return HdgfxValidateOpen(KmdDevice, Surface->Open);
}

static NTSTATUS
HdgfxRenderRect(
    _In_ PHDGFX_DEVICE KmdDevice,
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
    _In_ PHDGFX_DEVICE KmdDevice,
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

static BOOLEAN
HdgfxGenSection(
    _In_ ULONG Offset,
    _In_ ULONG Length,
    _In_ ULONG Alignment,
    _In_ ULONG Limit,
    _In_ UINT CommandLength)
{
    if (Length == 0)
        return TRUE;
    return Length <= Limit && Length % Alignment == 0 && Offset % Alignment == 0 &&
           Offset >= sizeof(HDGFX_GEN_SUBMIT) && Offset <= CommandLength && Length <= CommandLength - Offset;
}

static NTSTATUS
HdgfxRenderGen(
    _In_ PHDGFX_DEVICE KmdDevice,
    _In_ PHDGFX_ADAPTER Adapter,
    _Inout_ DXGKARG_RENDER *pRender)
{
    HDGFX_GEN_SUBMIT Submit;
    ULONGLONG DmaAddress = (ULONGLONG)pRender->DmaBufferPhysicalAddress.QuadPart;
    PUCHAR Base = pRender->pDmaBuffer;
    D3DDDI_PATCHLOCATIONLIST *Out = pRender->pPatchLocationListOut;
    PHDGFX_DMA_RECORD Record;
    PHDGFX_USER_BATCH User;
    PULONG End;
    ULONG StateOffset;
    ULONG BatchOffset;
    ULONG Size;
    UINT Index;
    NTSTATUS Status;

    if (pRender->CommandLength < sizeof(Submit))
        return STATUS_INVALID_PARAMETER;
    RtlCopyMemory(&Submit, pRender->pCommand, sizeof(Submit));
    if (Submit.Magic != HDGFX_GEN_SUBMIT_MAGIC ||
        Submit.Version != HDGFX_GEN_SUBMIT_VERSION || Submit.Flags != 0 || Submit.Reserved != 0 ||
        pRender->MultipassOffset != 0 || Submit.BatchLength == 0 ||
        !HdgfxGenSection(Submit.BatchOffset, Submit.BatchLength, 8, HDGFX_GEN_MAX_BATCH_SIZE,
                         pRender->CommandLength) ||
        !HdgfxGenSection(Submit.StateOffset, Submit.StateLength, 4, HDGFX_GEN_MAX_STATE_SIZE,
                         pRender->CommandLength) ||
        (Submit.StateLength != 0 && Submit.StateOffset < Submit.BatchOffset + Submit.BatchLength &&
         Submit.BatchOffset < Submit.StateOffset + Submit.StateLength))
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (pRender->DmaBufferSegmentId != HDGFX_DMA_SEGMENT_ID || DmaAddress < Adapter->DmaBase ||
        DmaAddress - Adapter->DmaBase >= HDGFX_DMA_SIZE)
    {
        return STATUS_INVALID_PARAMETER;
    }

    StateOffset = (ULONG)(ROUND_TO_PAGES(DmaAddress + sizeof(*Record) + sizeof(*User)) - DmaAddress);
    BatchOffset = ALIGN_UP_BY(StateOffset + Submit.StateLength, 64);
    Size = BatchOffset + Submit.BatchLength + 2 * sizeof(ULONG);
    if (Size > pRender->DmaSize)
        return STATUS_INVALID_PARAMETER;

    for (Index = 0; Index < pRender->PatchLocationListInSize; Index++)
    {
        CONST D3DDDI_PATCHLOCATIONLIST *Patch = &pRender->pPatchLocationListIn[Index];
        CONST DXGK_ALLOCATIONLIST *Allocation;
        ULONGLONG Value;

        if (Patch->PatchOffset % sizeof(ULONG) != 0 || Patch->AllocationIndex >= pRender->AllocationListSize)
            return STATUS_INVALID_PARAMETER;
        if (!(Patch->PatchOffset >= Submit.BatchOffset &&
              Patch->PatchOffset - Submit.BatchOffset < Submit.BatchLength) &&
            !(Submit.StateLength != 0 && Patch->PatchOffset >= Submit.StateOffset &&
              Patch->PatchOffset - Submit.StateOffset < Submit.StateLength))
        {
            return STATUS_INVALID_USER_BUFFER;
        }
        if (Patch->DriverId == HDGFX_GEN_PATCH_STATE)
        {
            if (Patch->AllocationOffset >= Submit.StateLength)
                return STATUS_INVALID_PARAMETER;
            continue;
        }
        if (Patch->DriverId != HDGFX_GEN_PATCH_ALLOCATION)
            return STATUS_INVALID_PARAMETER;
        Allocation = &pRender->pAllocationList[Patch->AllocationIndex];
        Status = HdgfxValidateOpen(KmdDevice, (PHDGFX_OPEN)Allocation->hDeviceSpecificAllocation);
        if (NT_SUCCESS(Status))
        {
            Status = HdgfxPlacement(Adapter, Allocation, (PHDGFX_OPEN)Allocation->hDeviceSpecificAllocation,
                                    Patch->AllocationOffset, HDGFX_PATCH_GTT, &Value);
        }
        if (!NT_SUCCESS(Status))
            return Status;
    }

    RtlZeroMemory(Base, BatchOffset);
    Record = (PHDGFX_DMA_RECORD)Base;
    Record->Type = HDGFX_DMA_USER;
    Record->Size = Size;
    User = (PHDGFX_USER_BATCH)(Record + 1);
    User->BatchOffset = BatchOffset;
    User->StateOffset = StateOffset;
    if (Submit.StateLength != 0)
        RtlCopyMemory(Base + StateOffset, (CONST UCHAR *)pRender->pCommand + Submit.StateOffset, Submit.StateLength);
    RtlCopyMemory(Base + BatchOffset, (CONST UCHAR *)pRender->pCommand + Submit.BatchOffset, Submit.BatchLength);
    End = (PULONG)(Base + BatchOffset + Submit.BatchLength);
    HdgfxMiBatchEnd((uint32_t *)End);
    End[1] = 0;

    for (Index = 0; Index < pRender->PatchLocationListInSize; Index++)
    {
        CONST D3DDDI_PATCHLOCATIONLIST *Patch = &pRender->pPatchLocationListIn[Index];
        ULONG Location;

        if (Patch->PatchOffset - Submit.BatchOffset < Submit.BatchLength)
            Location = BatchOffset + (Patch->PatchOffset - Submit.BatchOffset);
        else
            Location = StateOffset + (Patch->PatchOffset - Submit.StateOffset);
        if (Patch->DriverId == HDGFX_GEN_PATCH_STATE)
        {
            *(PULONG)(Base + Location) = (ULONG)(DmaAddress + StateOffset + Patch->AllocationOffset);
            continue;
        }
        RtlZeroMemory(Out, sizeof(*Out));
        Out->AllocationIndex = Patch->AllocationIndex;
        Out->AllocationOffset = Patch->AllocationOffset;
        Out->DriverId = HDGFX_PATCH_GTT;
        Out->PatchOffset = Location;
        Out++;
    }

    pRender->pDmaBuffer = Base + Size;
    pRender->pPatchLocationListOut = Out;
    pRender->MultipassOffset = 0;
    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
HdgfxDdiRender(
    _In_ PVOID hContext,
    _Inout_ DXGKARG_RENDER *pRender)
{
    PHDGFX_DEVICE KmdDevice;
    PHDGFX_ADAPTER Adapter;
    HDGFX_DMA_WRITER Writer;
    UINT Patches = 0;
    UINT Offset;
    UINT Index;
    NTSTATUS Status;

    Status = HdgfxResolveOwner(hContext, &KmdDevice, &Adapter);
    if (!NT_SUCCESS(Status))
        return Status;
    if (pRender == NULL || pRender->pCommand == NULL || pRender->pDmaBuffer == NULL)
        return STATUS_INVALID_PARAMETER;
    if (pRender->PatchLocationListInSize != 0 &&
        (pRender->pPatchLocationListIn == NULL || pRender->pPatchLocationListOut == NULL ||
         pRender->pAllocationList == NULL || pRender->AllocationListSize == 0))
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (((PHDGFX_CONTEXT)hContext)->Magic == HDGFX_CONTEXT_MAGIC && ((PHDGFX_CONTEXT)hContext)->Gen)
    {
        if (pRender->PatchLocationListInSize > pRender->PatchLocationListOutSize)
            return STATUS_INVALID_PARAMETER;
        return HdgfxRenderGen(KmdDevice, Adapter, pRender);
    }
    if (pRender->CommandLength == 0 || pRender->CommandLength % sizeof(SOFTGPU_CMD) != 0 ||
        pRender->MultipassOffset >= pRender->CommandLength || pRender->MultipassOffset % sizeof(SOFTGPU_CMD) != 0)
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (pRender->PatchLocationListInSize > pRender->PatchLocationListOutSize)
        return STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER;

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
        Status = HdgfxValidateOpen(KmdDevice, (PHDGFX_OPEN)Allocation->hDeviceSpecificAllocation);
        if (NT_SUCCESS(Status))
        {
            Status = HdgfxPlacement(Adapter, Allocation, (PHDGFX_OPEN)Allocation->hDeviceSpecificAllocation, 0,
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
    _In_ PHDGFX_DEVICE KmdDevice,
    _In_ CONST DXGKARG_PRESENT *Present,
    _In_ UINT AllocationIndex,
    _In_ CONST RECT *Rect,
    _Out_ PHDGFX_DMA_SURFACE Surface)
{
    NTSTATUS Status;

    Surface->AllocationIndex = AllocationIndex;
    Surface->Allocation = &Present->pAllocationList[AllocationIndex];
    Surface->Open = (PHDGFX_OPEN)Surface->Allocation->hDeviceSpecificAllocation;
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
    PHDGFX_DEVICE KmdDevice;
    PHDGFX_ADAPTER Adapter;
    SOFTGPU_2D_PRESENT_POLICY Policy;
    HDGFX_DMA_SURFACE Source;
    HDGFX_DMA_SURFACE Destination;
    HDGFX_DMA_WRITER Writer;
    SOFTGPU_CMD Command;
    UINT Index;
    NTSTATUS Status;

    Status = HdgfxResolveOwner(hContext, &KmdDevice, &Adapter);
    if (!NT_SUCCESS(Status))
        return Status;
    if (Adapter->Stopped)
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
            Status = HdgfxValidateSurface(KmdDevice, (PHDGFX_OPEN)
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
            Status = HdgfxEmitCommand(&Writer, Adapter, &Command, &Source, &Destination);
        }
        else
        {
            Command.Op = SOFTGPU_CMD_OP_FILL;
            Status = HdgfxEmitCommand(&Writer, Adapter, &Command, NULL, &Destination);
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
    PHDGFX_ADAPTER Adapter = MiniportDeviceContext;
    ULONGLONG SlabBase;
    ULONGLONG SlabOffset;
    HDGFX_DMA_WRITER Writer;
    PHDGFX_GTT_UPDATE Update;
    PSOFTGPU_CMD Command;
    PPFN_NUMBER Pfns;
    PVOID SystemVa;
    SIZE_T Done;
    SIZE_T Count;
    SIZE_T Index;
    ULONGLONG Dummy;
    PMDL Mdl;
    BOOLEAN ToSlab;

    if (Adapter == NULL || Adapter->Magic != HDGFX_ADAPTER_MAGIC || BuildPagingBuffer == NULL ||
        BuildPagingBuffer->pDmaBuffer == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    SlabBase = (ULONGLONG)Adapter->SlabPhysical.QuadPart;
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
            if (Transfer->Source.SegmentId == 0 && Transfer->Destination.SegmentId == HDGFX_SLAB_SEGMENT_ID)
            {
                ToSlab = TRUE;
                Mdl = Transfer->Source.pMdl;
                SlabOffset = (ULONGLONG)Transfer->Destination.SegmentAddress.QuadPart;
            }
            else if (Transfer->Source.SegmentId == HDGFX_SLAB_SEGMENT_ID && Transfer->Destination.SegmentId == 0)
            {
                ToSlab = FALSE;
                Mdl = Transfer->Destination.pMdl;
                SlabOffset = (ULONGLONG)Transfer->Source.SegmentAddress.QuadPart;
            }
            else
            {
                return STATUS_NOT_SUPPORTED;
            }
            if (Mdl == NULL || SlabOffset > Adapter->SlabSize ||
                Transfer->TransferSize > Adapter->SlabSize - SlabOffset)
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

            if (Fill->Destination.SegmentId != HDGFX_SLAB_SEGMENT_ID || Fill->FillSize == 0)
                return STATUS_NOT_SUPPORTED;
            SlabOffset = (ULONGLONG)Fill->Destination.SegmentAddress.QuadPart;
            if (SlabOffset > Adapter->SlabSize || Fill->FillSize > Adapter->SlabSize - SlabOffset)
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

        case DXGK_OPERATION_MAP_APERTURE_SEGMENT:
        {
            CONST DXGK_BUILDPAGINGBUFFER_MAP_APERTURE *Map = &BuildPagingBuffer->MapApertureSegment;

            Done = BuildPagingBuffer->MultipassOffset;
            if (Map->SegmentId != HDGFX_APERTURE_SEGMENT_ID || Adapter->ApertureSize == 0 || Map->pMdl == NULL ||
                Map->NumberOfPages == 0 || Done >= Map->NumberOfPages ||
                Map->OffsetInPages > Adapter->ApertureSize / PAGE_SIZE ||
                Map->NumberOfPages > Adapter->ApertureSize / PAGE_SIZE - Map->OffsetInPages ||
                Map->MdlOffset > ADDRESS_AND_SIZE_TO_SPAN_PAGES(MmGetMdlVirtualAddress(Map->pMdl),
                                                                MmGetMdlByteCount(Map->pMdl)) ||
                Map->NumberOfPages > ADDRESS_AND_SIZE_TO_SPAN_PAGES(MmGetMdlVirtualAddress(Map->pMdl),
                                                                    MmGetMdlByteCount(Map->pMdl)) - Map->MdlOffset)
            {
                return STATUS_INVALID_PARAMETER;
            }
            Count = ((SIZE_T)(Writer.End - Writer.Cursor) - sizeof(HDGFX_DMA_RECORD) - sizeof(*Update) -
                     2 * sizeof(ULONG)) / sizeof(ULONG);
            if (Count == 0)
                return STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER;
            if (Count > Map->NumberOfPages - Done)
                Count = Map->NumberOfPages - Done;
            Update = HdgfxWriterRecord(&Writer, HDGFX_DMA_GTT,
                                       (ULONG)ALIGN_UP_BY(sizeof(*Update) + Count * sizeof(ULONG), 8));
            Update->First = (ULONG)(Adapter->ApertureBase / PAGE_SIZE + Map->OffsetInPages + Done);
            Update->Count = (ULONG)Count;
            Pfns = MmGetMdlPfnArray(Map->pMdl) + Map->MdlOffset + Done;
            for (Index = 0; Index < Count; Index++)
            {
                if (Pfns[Index] > (Adapter->Generation->GttPhysicalLimit >> PAGE_SHIFT))
                    return STATUS_INVALID_PARAMETER;
                ((PULONG)(Update + 1))[Index] = Adapter->Generation->GttEntry((ULONGLONG)Pfns[Index] << PAGE_SHIFT);
            }
            Done += Count;
            if (Done < Map->NumberOfPages)
            {
                BuildPagingBuffer->pDmaBuffer = Writer.Cursor;
                BuildPagingBuffer->MultipassOffset = (UINT)Done;
                return STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER;
            }
            break;
        }

        case DXGK_OPERATION_UNMAP_APERTURE_SEGMENT:
        {
            CONST DXGK_BUILDPAGINGBUFFER_UNMAP_APERTURE *Unmap = &BuildPagingBuffer->UnmapApertureSegment;

            if (Unmap->SegmentId != HDGFX_APERTURE_SEGMENT_ID || Adapter->ApertureSize == 0 ||
                Unmap->NumberOfPages == 0 || Unmap->OffsetInPages > Adapter->ApertureSize / PAGE_SIZE ||
                Unmap->NumberOfPages > Adapter->ApertureSize / PAGE_SIZE - Unmap->OffsetInPages)
            {
                return STATUS_INVALID_PARAMETER;
            }
            Dummy = (ULONGLONG)Unmap->DummyPage.QuadPart;
            if (Dummy == 0 || Dummy % PAGE_SIZE != 0 || Dummy > Adapter->Generation->GttPhysicalLimit)
                Dummy = (ULONGLONG)MmGetPhysicalAddress(Adapter->ScratchPage).QuadPart;
            Update = HdgfxWriterRecord(&Writer, HDGFX_DMA_GTT, sizeof(*Update));
            Update->First = (ULONG)(Adapter->ApertureBase / PAGE_SIZE + Unmap->OffsetInPages);
            Update->Count = (ULONG)Unmap->NumberOfPages;
            Update->Fill = Adapter->Generation->GttEntry(Dummy);
            break;
        }

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
    PHDGFX_ADAPTER Adapter = MiniportDeviceContext;
    PHDGFX_DEVICE KmdDevice;
    UINT Index;

    if (Adapter == NULL || Adapter->Magic != HDGFX_ADAPTER_MAGIC || Patch == NULL)
        return STATUS_INVALID_PARAMETER;
    KmdDevice = (PHDGFX_DEVICE)Patch->hDevice;
    if (KmdDevice == NULL || KmdDevice->Magic != HDGFX_DEVICE_MAGIC || KmdDevice->Adapter != Adapter ||
        KmdDevice->Process == NULL || KmdDevice->Process->Magic != HDGFX_PROCESS_MAGIC ||
        KmdDevice->Process->Adapter != Adapter)
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
        PHDGFX_OPEN Open;
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
        Open = (PHDGFX_OPEN)Allocation->hDeviceSpecificAllocation;
        Status = HdgfxValidateOpen(KmdDevice, Open);
        if (!NT_SUCCESS(Status))
            return Status;
        if (Allocation->SegmentId == 0)
            return STATUS_GRAPHICS_ALLOCATION_BUSY;
        Status = HdgfxPlacement(Adapter, Allocation, Open, Entry->AllocationOffset, Entry->DriverId, &Value);
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

static PUCHAR
HdgfxCpuRange(
    _In_ PHDGFX_ADAPTER Adapter,
    _In_ ULONGLONG Address,
    _In_ ULONGLONG Length,
    _Out_ PSIZE_T Remaining)
{
    SIZE_T Offset;

    *Remaining = 0;
    if ((Address & HDGFX_APERTURE_CPU) != 0)
    {
        Address &= ~HDGFX_APERTURE_CPU;
        if (Adapter->ApertureCpu == NULL || Address < Adapter->ApertureBase ||
            Address - Adapter->ApertureBase >= Adapter->ApertureSize ||
            Length > Adapter->ApertureSize - (Address - Adapter->ApertureBase))
        {
            return NULL;
        }
        Offset = (SIZE_T)(Address - Adapter->ApertureBase);
        *Remaining = Adapter->ApertureSize - Offset;
        return Adapter->ApertureCpu + Offset;
    }
    if (!NT_SUCCESS(SoftGpu2dResolveSlabRange((ULONGLONG)Adapter->SlabPhysical.QuadPart, Adapter->SlabSize,
                                              Address, Length, &Offset)))
    {
        return NULL;
    }
    *Remaining = Adapter->SlabSize - Offset;
    return (PUCHAR)Adapter->Slab + Offset;
}

static volatile LONG64 *
HdgfxCpuFence(
    _In_ PHDGFX_ADAPTER Adapter,
    _In_ ULONGLONG Address)
{
    SIZE_T Remaining;

    if (Address % sizeof(UINT64) != 0)
        return NULL;
    return (volatile LONG64 *)HdgfxCpuRange(Adapter, Address, sizeof(UINT64), &Remaining);
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
            Fence = HdgfxCpuFence(Adapter, Command->DstAddress);
            if (Fence == NULL)
                Status = STATUS_INVALID_ADDRESS;
            else if ((ULONGLONG)ReadAcquire64(Fence) < Command->FenceValue)
                return HdgfxQueueWait;
            break;

        case SOFTGPU_CMD_OP_SIGNAL_FENCE:
            Fence = HdgfxCpuFence(Adapter, Command->DstAddress);
            if (Fence == NULL)
                Status = STATUS_INVALID_ADDRESS;
            else
                WriteRelease64(Fence, (LONG64)Command->FenceValue);
            break;

        case SOFTGPU_CMD_OP_BLT:
            Source = HdgfxCpuRange(Adapter, Command->SrcAddress, 1, &SourceSize);
            Destination = HdgfxCpuRange(Adapter, Command->DstAddress, 1, &DestinationSize);
            Status = Source == NULL || Destination == NULL ? STATUS_INVALID_ADDRESS :
                     SoftGpu2dCopyRect(Source, SourceSize, Command->SrcPitch, &Command->SrcRect, Destination,
                                       DestinationSize, Command->DstPitch, &Command->DstRect);
            break;

        case SOFTGPU_CMD_OP_FILL:
            Destination = HdgfxCpuRange(Adapter, Command->DstAddress, 1, &DestinationSize);
            Status = Destination == NULL ? STATUS_INVALID_ADDRESS :
                     SoftGpu2dFillRect(Destination, DestinationSize, Command->DstPitch, &Command->DstRect,
                                       Command->Color);
            break;

        case SOFTGPU_CMD_OP_PAGE:
            Destination = HdgfxCpuRange(Adapter, Command->SlabAddress, Command->ByteCount, &DestinationSize);
            if (Destination == NULL || Command->SystemAddress == 0)
                Status = STATUS_INVALID_ADDRESS;
            else if (Command->Flags & SOFTGPU_CMD_FLAG_TO_SLAB)
                RtlCopyMemory(Destination, (PVOID)(ULONG_PTR)Command->SystemAddress, (SIZE_T)Command->ByteCount);
            else
                RtlCopyMemory((PVOID)(ULONG_PTR)Command->SystemAddress, Destination, (SIZE_T)Command->ByteCount);
            break;

        case SOFTGPU_CMD_OP_FILL_LINEAR:
            Destination = HdgfxCpuRange(Adapter, Command->SlabAddress, Command->ByteCount, &DestinationSize);
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
HdgfxRunGtt(
    _Inout_ PHDGFX_ADAPTER Adapter,
    _In_ CONST HDGFX_GTT_UPDATE *Update)
{
    ULONG First = Adapter->ApertureBase / PAGE_SIZE;
    ULONG Index;

    if (!HdgfxRingIdle(Adapter))
        return HdgfxQueueRing;
    if (Adapter->ApertureGtt == NULL || Update->Count == 0 || Update->First < First ||
        Update->First - First >= Adapter->ApertureSize / PAGE_SIZE ||
        Update->Count > Adapter->ApertureSize / PAGE_SIZE - (Update->First - First))
    {
        DPRINT1("GTT update of %lu pages at %lu is outside the aperture, skipped\n", Update->Count, Update->First);
        return HdgfxQueueDone;
    }
    for (Index = 0; Index < Update->Count; Index++)
    {
        WRITE_REGISTER_ULONG(&Adapter->ApertureGtt[Update->First - First + Index],
                             Update->Fill != 0 ? Update->Fill : ((CONST ULONG *)(Update + 1))[Index]);
    }
    READ_REGISTER_ULONG(&Adapter->ApertureGtt[Update->First - First + Update->Count - 1]);
    HdgfxGttFlush(Adapter->Registers, Adapter->Generation);
    return HdgfxQueueDone;
}

static HDGFX_QUEUE_STATE
HdgfxRunBatch(
    _Inout_ PHDGFX_ADAPTER Adapter,
    _In_ ULONG Address,
    _In_ BOOLEAN Secure)
{
    HDGFX_QUEUE_STATE State = HdgfxQueueDone;
    HDGFX_MMIO Mmio;
    uint32_t Dwords[4];
    ULONG Count = Secure ? HdgfxMiBatchStart(Dwords, Address) : HdgfxMiUserBatchStart(Dwords, Address);
    ULONG Bytes = Count * sizeof(ULONG);
    ULONG Padding;
    ULONG Head;

    if (Count == 0)
    {
        DPRINT1("Batch at 0x%lx is not qword aligned, dropping it\n", Address);
        return HdgfxQueueDone;
    }
    KeAcquireSpinLockAtDpcLevel(&Adapter->RingLock);
    if (!Adapter->RingStarted || Adapter->RingResetting)
    {
        KeReleaseSpinLockFromDpcLevel(&Adapter->RingLock);
        DPRINT1("Render ring stopped, dropping the batch at 0x%lx\n", Address);
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
    else if (HdgfxRingEmit(&Adapter->Ring, Head, Dwords, Count))
    {
        Adapter->RingUnfenced = TRUE;
        HdgfxIlkAdvanceRing(&Mmio, &Adapter->Ring);
    }
    else
    {
        DPRINT1("Render ring refused the batch at 0x%lx, dropping it\n", Address);
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
    PUCHAR Mapped = NULL;
    ULONG Segment = 0;

    *Software = FALSE;
    if (Submit->Offset < Submit->EndOffset && Submit->SegmentId == HDGFX_DMA_SEGMENT_ID)
    {
        Segment = (ULONG)((ULONGLONG)Submit->DmaPhysical.QuadPart - Adapter->DmaBase);
        Map = Adapter->DmaMemory + Segment;
    }
    else if (Submit->Offset < Submit->EndOffset)
    {
        Map = Mapped = MmMapIoSpace(Submit->DmaPhysical, Submit->EndOffset, MmCached);
        if (Map == NULL)
            return HdgfxQueueWait;
    }
    while (Submit->Offset < Submit->EndOffset)
    {
        Record = (PHDGFX_DMA_RECORD)(Map + Submit->Offset);
        if (Submit->EndOffset - Submit->Offset < sizeof(*Record) || Record->Size < sizeof(*Record) ||
            Record->Size > Submit->EndOffset - Submit->Offset || Record->Size % 8 != 0 ||
            (Record->Type == HDGFX_DMA_CPU && Record->Size != sizeof(*Record) + sizeof(SOFTGPU_CMD)) ||
            (Record->Type == HDGFX_DMA_GTT &&
             (Record->Size < sizeof(*Record) + sizeof(HDGFX_GTT_UPDATE) ||
              (((PHDGFX_GTT_UPDATE)(Record + 1))->Fill == 0 &&
               ((PHDGFX_GTT_UPDATE)(Record + 1))->Count >
                   (Record->Size - sizeof(*Record) - sizeof(HDGFX_GTT_UPDATE)) / sizeof(ULONG)))) ||
            (Record->Type == HDGFX_DMA_USER &&
             (Record->Size < sizeof(*Record) + sizeof(HDGFX_USER_BATCH) ||
              ((PHDGFX_USER_BATCH)(Record + 1))->BatchOffset % 8 != 0 ||
              ((PHDGFX_USER_BATCH)(Record + 1))->BatchOffset >= Record->Size)))
        {
            DPRINT1("Malformed DMA record at 0x%lx of fence %lu, skipping the rest\n", Submit->Offset, Submit->Fence);
            Submit->Offset = Submit->EndOffset;
            break;
        }
        if ((Record->Type == HDGFX_DMA_BATCH || Record->Type == HDGFX_DMA_USER) && Mapped != NULL)
            DPRINT1("Batch at 0x%lx of fence %lu is outside the DMA segment, skipped\n", Submit->Offset, Submit->Fence);
        else if (Record->Type == HDGFX_DMA_BATCH)
            State = HdgfxRunBatch(Adapter, (ULONG)Adapter->DmaBase + Segment + Submit->Offset + sizeof(*Record), TRUE);
        else if (Record->Type == HDGFX_DMA_USER)
            State = HdgfxRunBatch(Adapter,
                                  (ULONG)Adapter->DmaBase + Segment + Submit->Offset +
                                      ((PHDGFX_USER_BATCH)(Record + 1))->BatchOffset,
                                  FALSE);
        else if (Record->Type == HDGFX_DMA_CPU)
            State = HdgfxRunCpu(Adapter, (PSOFTGPU_CMD)(Record + 1));
        else if (Record->Type == HDGFX_DMA_GTT)
            State = HdgfxRunGtt(Adapter, (PHDGFX_GTT_UPDATE)(Record + 1));
        if (State != HdgfxQueueDone)
            break;
        Submit->Offset += Record->Size;
    }
    if (Mapped != NULL)
        MmUnmapIoSpace(Mapped, Submit->EndOffset);
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
    DXGKARGCB_NOTIFY_INTERRUPT_DATA Notify;

    if (Adapter->QueueResetting || (LONG)(Adapter->SoftwareFence - Adapter->CompletedFence) <= 0)
        return FALSE;
    Adapter->CompletedFence = Adapter->SoftwareFence;
    RtlZeroMemory(&Notify, sizeof(Notify));
    Notify.InterruptType = DXGK_INTERRUPT_TYPE_DMA_COMPLETED;
    Notify.DmaCompleted.SubmissionFenceId = Adapter->SoftwareFence;
    Adapter->DxgkInterface.DxgkCbNotifyInterrupt(Adapter->DxgkInterface.DeviceHandle, &Notify);
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

    if (Software && Adapter->DxgkInterface.DxgkCbSynchronizeExecution != NULL)
    {
        Adapter->DxgkInterface.DxgkCbSynchronizeExecution(Adapter->DxgkInterface.DeviceHandle, HdgfxCompleteSoftware,
                                                          Adapter, 0, &Notified);
        if (Notified)
            Adapter->DxgkInterface.DxgkCbNotifyDpc(Adapter->DxgkInterface.DeviceHandle);
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
    DXGKARGCB_NOTIFY_INTERRUPT_DATA Notify;
    ULONG Fence = ((volatile ULONG *)Adapter->StatusPage)[HDGFX_RING_FENCE_INDEX];

    if (Adapter->QueueResetting || (LONG)(Fence - Adapter->CompletedFence) <= 0)
        return;
    Adapter->CompletedFence = Fence;
    RtlZeroMemory(&Notify, sizeof(Notify));
    Notify.InterruptType = DXGK_INTERRUPT_TYPE_DMA_COMPLETED;
    Notify.DmaCompleted.SubmissionFenceId = Fence;
    Adapter->DxgkInterface.DxgkCbNotifyInterrupt(Adapter->DxgkInterface.DeviceHandle, &Notify);
    InterlockedExchange(&Adapter->FenceNotified, 1);
}

VOID
HdgfxQueueDpc(
    _Inout_ PHDGFX_ADAPTER Adapter)
{

    KeInsertQueueDpc(&Adapter->QueueDpc, NULL, NULL);
    if (InterlockedExchange(&Adapter->FenceNotified, 0) != 0)
        Adapter->DxgkInterface.DxgkCbNotifyDpc(Adapter->DxgkInterface.DeviceHandle);
}

NTSTATUS
APIENTRY
HdgfxDdiSubmitCommand(
    _In_ PVOID MiniportDeviceContext,
    _In_ CONST DXGKARG_SUBMITCOMMAND *SubmitCommand)
{
    PHDGFX_ADAPTER Adapter = MiniportDeviceContext;
    PHDGFX_DEVICE KmdDevice;
    PHDGFX_SUBMIT Submit;
    NTSTATUS Status = STATUS_SUCCESS;
    KIRQL OldIrql;

    if (Adapter == NULL || Adapter->Magic != HDGFX_ADAPTER_MAGIC || SubmitCommand == NULL ||
        SubmitCommand->NodeOrdinal != 0 || SubmitCommand->EngineOrdinal != 0)
    {
        return STATUS_INVALID_PARAMETER;
    }
    KmdDevice = (PHDGFX_DEVICE)SubmitCommand->hDevice;
    if (KmdDevice == NULL || KmdDevice->Magic != HDGFX_DEVICE_MAGIC || KmdDevice->Adapter != Adapter ||
        KmdDevice->Process == NULL || KmdDevice->Process->Magic != HDGFX_PROCESS_MAGIC ||
        KmdDevice->Process->Adapter != Adapter)
    {
        return STATUS_INVALID_HANDLE;
    }
    if (!SubmitCommand->Flags.NullRendering &&
        (SubmitCommand->DmaBufferPhysicalAddress.QuadPart == 0 ||
         SubmitCommand->DmaBufferSubmissionEndOffset <= SubmitCommand->DmaBufferSubmissionStartOffset ||
         (SubmitCommand->DmaBufferSegmentId != 0 &&
          (SubmitCommand->DmaBufferSegmentId != HDGFX_DMA_SEGMENT_ID || Adapter->DmaMemory == NULL ||
           (ULONGLONG)SubmitCommand->DmaBufferPhysicalAddress.QuadPart < Adapter->DmaBase ||
           (ULONGLONG)SubmitCommand->DmaBufferPhysicalAddress.QuadPart - Adapter->DmaBase >= HDGFX_DMA_SIZE ||
           SubmitCommand->DmaBufferSubmissionEndOffset >
               HDGFX_DMA_SIZE - ((ULONGLONG)SubmitCommand->DmaBufferPhysicalAddress.QuadPart - Adapter->DmaBase)))))
    {
        return STATUS_INVALID_PARAMETER;
    }

    KeAcquireSpinLock(&Adapter->QueueLock, &OldIrql);
    if (Adapter->Stopped || Adapter->QueueStopped)
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
        Submit->SegmentId = SubmitCommand->DmaBufferSegmentId;
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
    PHDGFX_ADAPTER Adapter = MiniportDeviceContext;

    if (Adapter == NULL || Adapter->Magic != HDGFX_ADAPTER_MAGIC || pCurrentFence == NULL ||
        pCurrentFence->NodeOrdinal != 0 || pCurrentFence->EngineOrdinal != 0)
    {
        return STATUS_INVALID_PARAMETER;
    }
    pCurrentFence->CurrentFence = Adapter->CompletedFence;
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
    PHDGFX_ADAPTER Adapter = MiniportDeviceContext;
    BOOLEAN Ignored;
    NTSTATUS Status;
    KIRQL OldIrql;

    PAGED_CODE();

    if (Adapter == NULL || Adapter->Magic != HDGFX_ADAPTER_MAGIC)
        return STATUS_INVALID_PARAMETER;

    KeAcquireSpinLock(&Adapter->QueueLock, &OldIrql);
    Adapter->QueueResetting = TRUE;
    Adapter->QueueHead = Adapter->QueueTail;
    KeReleaseSpinLock(&Adapter->QueueLock, OldIrql);
    KeCancelTimer(&Adapter->QueueTimer);
    KeRemoveQueueDpc(&Adapter->QueueDpc);
    KeFlushQueuedDpcs();
    Adapter->DxgkInterface.DxgkCbSynchronizeExecution(Adapter->DxgkInterface.DeviceHandle, HdgfxResetFences, Adapter, 0,
                                                      &Ignored);
    DPRINT1("Timeout reset dropped the queue at fence %lu\n", Adapter->CompletedFence);
    Status = HdgfxResetRender(Adapter);
    HdgfxApertureClear(Adapter);
    return Status;
}

NTSTATUS
APIENTRY
HdgfxDdiRestartFromTimeout(
    _In_ PVOID MiniportDeviceContext)
{
    PHDGFX_ADAPTER Adapter = MiniportDeviceContext;
    BOOLEAN Ignored;

    PAGED_CODE();

    if (Adapter == NULL || Adapter->Magic != HDGFX_ADAPTER_MAGIC)
        return STATUS_INVALID_PARAMETER;

    HdgfxRestartRender(Adapter);
    Adapter->DxgkInterface.DxgkCbSynchronizeExecution(Adapter->DxgkInterface.DeviceHandle, HdgfxRestartFences, Adapter,
                                                      0, &Ignored);
    KeInsertQueueDpc(&Adapter->QueueDpc, NULL, NULL);
    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
HdgfxDdiCreateContext(
    _In_ PVOID hDevice,
    _Inout_ PDXGKARG_CREATECONTEXT CreateContext)
{
    PHDGFX_DEVICE KmdDevice = hDevice;
    PHDGFX_PROCESS Process;
    PHDGFX_CONTEXT Context;
    HDGFX_GEN_CONTEXT_DATA GenData;
    BOOLEAN Gen = FALSE;

    if (CreateContext == NULL || KmdDevice == NULL || KmdDevice->Magic != HDGFX_DEVICE_MAGIC ||
        KmdDevice->Adapter == NULL || KmdDevice->Adapter->Magic != HDGFX_ADAPTER_MAGIC)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Process = KmdDevice->Process;
    if ((!CreateContext->Flags.SystemContext && Process == NULL) ||
        (Process != NULL && (Process->Magic != HDGFX_PROCESS_MAGIC || Process->Adapter != KmdDevice->Adapter)) ||
        CreateContext->NodeOrdinal != 0)
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (CreateContext->PrivateDriverDataSize == sizeof(GenData) && CreateContext->pPrivateDriverData != NULL)
    {
        RtlCopyMemory(&GenData, CreateContext->pPrivateDriverData, sizeof(GenData));
        if (GenData.Magic != HDGFX_GEN_CONTEXT_MAGIC || GenData.Version != HDGFX_GEN_CONTEXT_VERSION ||
            CreateContext->Flags.SystemContext)
        {
            return STATUS_INVALID_PARAMETER;
        }
        Gen = TRUE;
    }

    Context = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*Context), HDGFX_TAG);
    if (Context == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;
    Context->Magic = HDGFX_CONTEXT_MAGIC;
    Context->NodeOrdinal = CreateContext->NodeOrdinal;
    Context->Gen = Gen;
    Context->Device = KmdDevice;
    Context->Process = Process;

    CreateContext->ContextInfo.DmaBufferSize = Gen ? HDGFX_GEN_COMMAND_BUFFER_SIZE : 64 * 1024;
    CreateContext->ContextInfo.DmaBufferSegmentSet = 1UL << (HDGFX_DMA_SEGMENT_ID - 1);
    CreateContext->ContextInfo.DmaBufferPrivateDataSize = 0;
    CreateContext->ContextInfo.AllocationListSize = Gen ? HDGFX_GEN_ALLOCATION_LIST_SIZE : 256;
    CreateContext->ContextInfo.PatchLocationListSize = Gen ? HDGFX_GEN_PATCH_LIST_SIZE : 256;
    CreateContext->ContextInfo.Caps.Value = 0;
    CreateContext->hContext = Context;
    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
HdgfxDdiDestroyContext(
    _In_ PVOID hContext)
{
    PHDGFX_CONTEXT Context = hContext;

    if (Context == NULL)
        return STATUS_SUCCESS;
    if (Context->Magic != HDGFX_CONTEXT_MAGIC)
        return STATUS_INVALID_PARAMETER;
    Context->Magic = HDGFX_DEAD_CONTEXT_MAGIC;
    Context->Device = NULL;
    Context->Process = NULL;
    ExFreePoolWithTag(Context, HDGFX_TAG);
    return STATUS_SUCCESS;
}
