/*
 * PROJECT:     LiberNT Apple ISP Camera Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     KS filter and NV12 capture pin of the ISP camera
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "appleisp.h"
#include "ispguid.h"

#define ISP_FOURCC_NV12     0x3231564e
#define ISP_NV12_BITS       12

typedef struct _ISP_PIN
{
    PKSPIN Pin;
    PISP_DEVICE Isp;
    PISP_PRESET Preset;
    BOOLEAN Prepared;
    BOOLEAN Running;
} ISP_PIN, *PISP_PIN;

static const GUID IspFilterCategories[] =
{
    { 0x6994ad05L, 0x93ef, 0x11d0, { 0xa3, 0xcc, 0x00, 0xa0, 0xc9, 0x22, 0x31, 0x96 } },
    { 0x65e8773dL, 0x8f56, 0x11d0, { 0xa3, 0xb9, 0x00, 0xa0, 0xc9, 0x22, 0x31, 0x96 } },
    { 0xe5323777L, 0xf976, 0x4f5b, { 0x9b, 0x55, 0xb9, 0x46, 0x99, 0xc4, 0x6e, 0x44 } },
};

static const KSPIN_INTERFACE IspStandardInterface =
{
    { 0x1a8766a0L, 0x62ce, 0x11cf, { 0xa5, 0xd6, 0x28, 0xdb, 0x04, 0xc1, 0x00, 0x00 } },
    KSINTERFACE_STANDARD_STREAMING,
    0
};

static const KSPIN_MEDIUM IspStandardMedium =
{
    { 0x4747b320L, 0x62ce, 0x11cf, { 0xa5, 0xd6, 0x28, 0xdb, 0x04, 0xc1, 0x00, 0x00 } },
    KSMEDIUM_TYPE_ANYINSTANCE,
    0
};

static
PVOID
IspAllocateBagged(
    _In_ PISP_DEVICE Isp,
    _In_ SIZE_T Size)
{
    PVOID Memory = ExAllocatePoolZero(NonPagedPool, Size, ISP_TAG);

    if (!Memory)
        return NULL;
    if (!NT_SUCCESS(KsAddItemToObjectBag(Isp->KsDevice->Bag, Memory, ExFreePool)))
    {
        ExFreePoolWithTag(Memory, ISP_TAG);
        return NULL;
    }
    return Memory;
}

static
ULONG
IspBitRate(
    _In_ PISP_PRESET Preset)
{
    return Preset->ImageSize * 8 * ISP_FRAME_RATE;
}

static
VOID
IspFillCaps(
    _Out_ KS_VIDEO_STREAM_CONFIG_CAPS *Caps,
    _In_ const GUID *Specifier,
    _In_ PISP_PRESET Preset)
{
    Caps->guid = *Specifier;
    Caps->InputSize.cx = Preset->OutputWidth;
    Caps->InputSize.cy = Preset->OutputHeight;
    Caps->MinCroppingSize = Caps->InputSize;
    Caps->MaxCroppingSize = Caps->InputSize;
    Caps->CropGranularityX = 1;
    Caps->CropGranularityY = 1;
    Caps->CropAlignX = 1;
    Caps->CropAlignY = 1;
    Caps->MinOutputSize = Caps->InputSize;
    Caps->MaxOutputSize = Caps->InputSize;
    Caps->OutputGranularityX = 1;
    Caps->OutputGranularityY = 1;
    /* FIXME: only 30 fps is offered; the firmware runs its exposure between 15 and 30 fps */
    Caps->MinFrameInterval = ISP_FRAME_INTERVAL;
    Caps->MaxFrameInterval = ISP_FRAME_INTERVAL;
    Caps->MinBitsPerSecond = IspBitRate(Preset);
    Caps->MaxBitsPerSecond = IspBitRate(Preset);
}

static
VOID
IspFillBitmap(
    _Out_ KS_BITMAPINFOHEADER *Header,
    _In_ PISP_PRESET Preset)
{
    Header->biSize = sizeof(KS_BITMAPINFOHEADER);
    Header->biWidth = Preset->OutputWidth;
    Header->biHeight = Preset->OutputHeight;
    Header->biPlanes = 1;
    Header->biBitCount = ISP_NV12_BITS;
    Header->biCompression = ISP_FOURCC_NV12;
    Header->biSizeImage = Preset->ImageSize;
}

static
VOID
IspFillRange(
    _Out_ KS_DATARANGE_VIDEO *Range,
    _In_ PISP_PRESET Preset)
{
    Range->DataRange.FormatSize = sizeof(*Range);
    Range->DataRange.SampleSize = Preset->ImageSize;
    Range->DataRange.MajorFormat = ISP_KSDATAFORMAT_TYPE_VIDEO;
    Range->DataRange.SubFormat = ISP_KSDATAFORMAT_SUBTYPE_NV12;
    Range->DataRange.Specifier = ISP_KSDATAFORMAT_SPECIFIER_VIDEOINFO;
    Range->bFixedSizeSamples = TRUE;
    IspFillCaps(&Range->ConfigCaps, &ISP_KSDATAFORMAT_SPECIFIER_VIDEOINFO, Preset);
    Range->VideoInfoHeader.dwBitRate = IspBitRate(Preset);
    Range->VideoInfoHeader.AvgTimePerFrame = ISP_FRAME_INTERVAL;
    IspFillBitmap(&Range->VideoInfoHeader.bmiHeader, Preset);
}

static
VOID
IspFillRange2(
    _Out_ KS_DATARANGE_VIDEO2 *Range,
    _In_ PISP_PRESET Preset)
{
    ULONG A = Preset->OutputWidth, B = Preset->OutputHeight, T;

    while (B)
    {
        T = A % B;
        A = B;
        B = T;
    }
    Range->DataRange.FormatSize = sizeof(*Range);
    Range->DataRange.SampleSize = Preset->ImageSize;
    Range->DataRange.MajorFormat = ISP_KSDATAFORMAT_TYPE_VIDEO;
    Range->DataRange.SubFormat = ISP_KSDATAFORMAT_SUBTYPE_NV12;
    Range->DataRange.Specifier = ISP_KSDATAFORMAT_SPECIFIER_VIDEOINFO2;
    Range->bFixedSizeSamples = TRUE;
    IspFillCaps(&Range->ConfigCaps, &ISP_KSDATAFORMAT_SPECIFIER_VIDEOINFO2, Preset);
    Range->VideoInfoHeader.dwBitRate = IspBitRate(Preset);
    Range->VideoInfoHeader.AvgTimePerFrame = ISP_FRAME_INTERVAL;
    Range->VideoInfoHeader.dwPictAspectRatioX = A ? Preset->OutputWidth / A : 0;
    Range->VideoInfoHeader.dwPictAspectRatioY = A ? Preset->OutputHeight / A : 0;
    IspFillBitmap(&Range->VideoInfoHeader.bmiHeader, Preset);
}

static
const KS_BITMAPINFOHEADER *
IspFormatHeader(
    _In_ PKSDATAFORMAT DataFormat,
    _Out_ LONGLONG *Interval)
{
    *Interval = 0;
    if (IsEqualGUIDAligned(&DataFormat->Specifier, &ISP_KSDATAFORMAT_SPECIFIER_VIDEOINFO) &&
        DataFormat->FormatSize >= sizeof(KS_DATAFORMAT_VIDEOINFOHEADER))
    {
        *Interval = ((PKS_DATAFORMAT_VIDEOINFOHEADER)DataFormat)->VideoInfoHeader.AvgTimePerFrame;
        return &((PKS_DATAFORMAT_VIDEOINFOHEADER)DataFormat)->VideoInfoHeader.bmiHeader;
    }
    if (IsEqualGUIDAligned(&DataFormat->Specifier, &ISP_KSDATAFORMAT_SPECIFIER_VIDEOINFO2) &&
        DataFormat->FormatSize >= sizeof(KS_DATAFORMAT_VIDEOINFOHEADER2))
    {
        *Interval = ((PKS_DATAFORMAT_VIDEOINFOHEADER2)DataFormat)->VideoInfoHeader2.AvgTimePerFrame;
        return &((PKS_DATAFORMAT_VIDEOINFOHEADER2)DataFormat)->VideoInfoHeader2.bmiHeader;
    }
    return NULL;
}

static
PISP_PRESET
IspFindPreset(
    _In_ PISP_DEVICE Isp,
    _In_ PKSDATAFORMAT DataFormat)
{
    const KS_BITMAPINFOHEADER *Header;
    LONGLONG Interval;
    LONG Height;
    ULONG Index;

    if (!IsEqualGUIDAligned(&DataFormat->MajorFormat, &ISP_KSDATAFORMAT_TYPE_VIDEO) ||
        !IsEqualGUIDAligned(&DataFormat->SubFormat, &ISP_KSDATAFORMAT_SUBTYPE_NV12))
    {
        return NULL;
    }
    Header = IspFormatHeader(DataFormat, &Interval);
    if (!Header || Header->biCompression != ISP_FOURCC_NV12 || Header->biBitCount != ISP_NV12_BITS ||
        (Interval != 0 && (Interval < ISP_FRAME_INTERVAL || Interval > ISP_FRAME_INTERVAL + 1)))
    {
        return NULL;
    }
    Height = Header->biHeight < 0 ? -Header->biHeight : Header->biHeight;
    for (Index = 0; Index < Isp->PresetCount; ++Index)
    {
        if ((ULONG)Header->biWidth == Isp->Presets[Index].OutputWidth &&
            (ULONG)Height == Isp->Presets[Index].OutputHeight &&
            Header->biSizeImage == Isp->Presets[Index].ImageSize)
        {
            return &Isp->Presets[Index];
        }
    }
    return NULL;
}

static
NTSTATUS
NTAPI
IspPinIntersect(
    _In_ PVOID Context,
    _In_ PIRP Irp,
    _In_ PKSP_PIN PinInstance,
    _In_ PKSDATARANGE CallerRange,
    _In_ PKSDATARANGE DescriptorRange,
    _In_ ULONG BufferSize,
    _Out_opt_ PVOID Data,
    _Out_ PULONG DataSize)
{
    const KS_BITMAPINFOHEADER *Ours, *Theirs = NULL;
    PUCHAR Output = Data;
    BOOLEAN Two;
    ULONG Size;

    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(Irp);
    UNREFERENCED_PARAMETER(PinInstance);
    if (!IsEqualGUIDAligned(&CallerRange->MajorFormat, &DescriptorRange->MajorFormat) ||
        !IsEqualGUIDAligned(&CallerRange->SubFormat, &DescriptorRange->SubFormat) ||
        !IsEqualGUIDAligned(&CallerRange->Specifier, &DescriptorRange->Specifier))
    {
        return STATUS_NO_MATCH;
    }
    Two = IsEqualGUIDAligned(&DescriptorRange->Specifier, &ISP_KSDATAFORMAT_SPECIFIER_VIDEOINFO2);
    if (Two)
    {
        Ours = &((KS_DATARANGE_VIDEO2 *)DescriptorRange)->VideoInfoHeader.bmiHeader;
        if (CallerRange->FormatSize >= sizeof(KS_DATARANGE_VIDEO2))
            Theirs = &((KS_DATARANGE_VIDEO2 *)CallerRange)->VideoInfoHeader.bmiHeader;
        Size = sizeof(KS_DATAFORMAT_VIDEOINFOHEADER2);
    }
    else if (IsEqualGUIDAligned(&DescriptorRange->Specifier, &ISP_KSDATAFORMAT_SPECIFIER_VIDEOINFO))
    {
        Ours = &((KS_DATARANGE_VIDEO *)DescriptorRange)->VideoInfoHeader.bmiHeader;
        if (CallerRange->FormatSize >= sizeof(KS_DATARANGE_VIDEO))
            Theirs = &((KS_DATARANGE_VIDEO *)CallerRange)->VideoInfoHeader.bmiHeader;
        Size = sizeof(KS_DATAFORMAT_VIDEOINFOHEADER);
    }
    else
    {
        return STATUS_NO_MATCH;
    }
    if (Theirs && (Theirs->biWidth != Ours->biWidth || Theirs->biHeight != Ours->biHeight))
        return STATUS_NO_MATCH;
    *DataSize = Size;
    if (BufferSize == 0)
        return STATUS_BUFFER_OVERFLOW;
    if (BufferSize < Size || !Output)
        return STATUS_BUFFER_TOO_SMALL;
    RtlCopyMemory(Output, DescriptorRange, sizeof(KSDATARANGE));
    ((PKSDATAFORMAT)Output)->FormatSize = Size;
    if (Two)
    {
        RtlCopyMemory(&((PKS_DATAFORMAT_VIDEOINFOHEADER2)Output)->VideoInfoHeader2,
                      &((KS_DATARANGE_VIDEO2 *)DescriptorRange)->VideoInfoHeader, sizeof(KS_VIDEOINFOHEADER2));
    }
    else
    {
        RtlCopyMemory(&((PKS_DATAFORMAT_VIDEOINFOHEADER)Output)->VideoInfoHeader,
                      &((KS_DATARANGE_VIDEO *)DescriptorRange)->VideoInfoHeader, sizeof(KS_VIDEOINFOHEADER));
    }
    return STATUS_SUCCESS;
}

static
NTSTATUS
IspStreamPrepare(
    _Inout_ PISP_PIN Context)
{
    PISP_DEVICE Isp = Context->Isp;
    ULONG Index;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    if (Context->Prepared)
        return STATUS_SUCCESS;
    ASSERT(!Isp->Streaming && !Isp->StreamPreset);
    for (Index = 0; Index < ISP_FRAME_COUNT; ++Index)
    {
        ASSERT(!Isp->Frames[Index].Surface);
        Isp->Frames[Index].Surface = IspAllocateSurface(Isp, Context->Preset->FrameSize, 0);
        Isp->Frames[Index].State = IspFrameFree;
        if (!Isp->Frames[Index].Surface)
            goto Fail;
    }
    for (Index = 0; Index < ISP_META_COUNT; ++Index)
    {
        ASSERT(!Isp->Meta[Index].Surface);
        Isp->Meta[Index].Surface = IspAllocateSurface(Isp, Isp->Hardware->MetaSize, 0);
        Isp->Meta[Index].Submitted = FALSE;
        if (!Isp->Meta[Index].Surface)
            goto Fail;
    }
    Isp->StreamPreset = Context->Preset;
    Context->Prepared = TRUE;
    DbgPrint("appleisp: %lu frame buffers of %lx bytes ready for %lux%lu\n", ISP_FRAME_COUNT, Context->Preset->FrameSize,
             Context->Preset->OutputWidth, Context->Preset->OutputHeight);
    return STATUS_SUCCESS;

Fail:
    for (Index = 0; Index < ISP_FRAME_COUNT; ++Index)
    {
        IspFreeSurface(Isp, Isp->Frames[Index].Surface);
        Isp->Frames[Index].Surface = NULL;
    }
    for (Index = 0; Index < ISP_META_COUNT; ++Index)
    {
        IspFreeSurface(Isp, Isp->Meta[Index].Surface);
        Isp->Meta[Index].Surface = NULL;
    }
    return STATUS_INSUFFICIENT_RESOURCES;
}

static
VOID
IspStreamUnprepare(
    _Inout_ PISP_PIN Context)
{
    PISP_DEVICE Isp = Context->Isp;
    ULONG Index;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    if (!Context->Prepared)
        return;
    ASSERT(!Context->Running && !Isp->Streaming);
    KsPinAcquireProcessingMutex(Context->Pin);
    for (Index = 0; Index < ISP_FRAME_COUNT; ++Index)
    {
        ASSERT(Isp->Frames[Index].State == IspFrameFree);
        IspFreeSurface(Isp, Isp->Frames[Index].Surface);
        Isp->Frames[Index].Surface = NULL;
    }
    for (Index = 0; Index < ISP_META_COUNT; ++Index)
    {
        IspFreeSurface(Isp, Isp->Meta[Index].Surface);
        Isp->Meta[Index].Surface = NULL;
        Isp->Meta[Index].Submitted = FALSE;
    }
    Isp->StreamPreset = NULL;
    Context->Prepared = FALSE;
    KsPinReleaseProcessingMutex(Context->Pin);
}

static
VOID
IspResetFrames(
    _In_ PISP_DEVICE Isp)
{
    ULONG Index;
    KIRQL Irql;

    KeAcquireSpinLock(&Isp->FrameLock, &Irql);
    for (Index = 0; Index < ISP_FRAME_COUNT; ++Index)
    {
        if (Isp->Frames[Index].State != IspFrameCopying)
            Isp->Frames[Index].State = IspFrameFree;
    }
    for (Index = 0; Index < ISP_META_COUNT; ++Index)
        Isp->Meta[Index].Submitted = FALSE;
    KeReleaseSpinLock(&Isp->FrameLock, Irql);
}

static
NTSTATUS
IspStreamStart(
    _Inout_ PISP_PIN Context)
{
    PISP_DEVICE Isp = Context->Isp;
    NTSTATUS Status;

    ASSERT(Context->Prepared && !Context->Running && Isp->StreamPreset == Context->Preset);
    Status = IspFirmwareBoot(Isp);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = IspConfigureCapture(Isp, Context->Preset);
    if (NT_SUCCESS(Status))
    {
        IspResetFrames(Isp);
        Isp->FramesDelivered = 0;
        Isp->FramesDropped = 0;
        Isp->Discontinuity = FALSE;
        InterlockedExchange(&Isp->Streaming, 1);
        Status = IspStartCapture(Isp);
    }
    if (!NT_SUCCESS(Status))
    {
        InterlockedExchange(&Isp->Streaming, 0);
        IspFirmwareShutdown(Isp);
        IspResetFrames(Isp);
        return Status;
    }
    Context->Running = TRUE;
    return STATUS_SUCCESS;
}

static
VOID
IspStreamStop(
    _Inout_ PISP_PIN Context)
{
    PISP_DEVICE Isp = Context->Isp;

    if (!Context->Running)
        return;
    InterlockedExchange(&Isp->Streaming, 0);
    IspStopCapture(Isp);
    IspFirmwareShutdown(Isp);
    IspResetFrames(Isp);
    Context->Running = FALSE;
    DbgPrint("appleisp: capture stopped after %lu frames, %lu dropped\n", Isp->FramesDelivered, Isp->FramesDropped);
}

static
NTSTATUS
NTAPI
IspPinSetDeviceState(
    _In_ PKSPIN Pin,
    _In_ KSSTATE To,
    _In_ KSSTATE From)
{
    PISP_PIN Context = Pin->Context;
    NTSTATUS Status;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    if (To == From)
        return STATUS_SUCCESS;
    if (From == KSSTATE_RUN)
        IspStreamStop(Context);
    if (From == KSSTATE_STOP)
    {
        Status = IspStreamPrepare(Context);
        if (!NT_SUCCESS(Status))
            return Status;
    }
    if (To == KSSTATE_RUN)
    {
        Status = IspStreamStart(Context);
        if (!NT_SUCCESS(Status))
        {
            DbgPrint("appleisp: starting the capture failed: %lx\n", Status);
            return Status;
        }
    }
    if (To == KSSTATE_STOP)
        IspStreamUnprepare(Context);
    return STATUS_SUCCESS;
}

static
VOID
IspCopyNv12(
    _Out_ PUCHAR Destination,
    _In_ const UCHAR *Source,
    _In_ PISP_PRESET Preset)
{
    ULONG Row, Width = Preset->OutputWidth, Height = Preset->OutputHeight;

    if (Preset->Stride == Width)
    {
        RtlCopyMemory(Destination, Source, Preset->ImageSize);
        return;
    }
    for (Row = 0; Row < Height; ++Row)
        RtlCopyMemory(Destination + Row * Width, Source + Row * Preset->Stride, Width);
    Destination += Width * Height;
    Source += Preset->LumaSize;
    for (Row = 0; Row < Height / 2; ++Row)
        RtlCopyMemory(Destination + Row * Width, Source + Row * Preset->Stride, Width);
}

static
VOID
IspFillHeader(
    _In_ PKSSTREAM_POINTER Leading,
    _In_ PISP_PRESET Preset,
    _In_ ULONGLONG Time,
    _In_ BOOLEAN Discontinuity)
{
    PKSSTREAM_HEADER Header = Leading->StreamHeader;

    Header->OptionsFlags = KSSTREAM_HEADER_OPTIONSF_SPLICEPOINT | KSSTREAM_HEADER_OPTIONSF_TIMEVALID |
                           KSSTREAM_HEADER_OPTIONSF_DURATIONVALID |
                           (Discontinuity ? KSSTREAM_HEADER_OPTIONSF_DATADISCONTINUITY : 0);
    Header->DataUsed = Preset->ImageSize;
    Header->Duration = ISP_FRAME_INTERVAL;
    Header->PresentationTime.Time = (LONGLONG)Time;
    Header->PresentationTime.Numerator = 0;
    Header->PresentationTime.Denominator = 0;
    if (Header->Size >= sizeof(KSSTREAM_HEADER) + sizeof(KS_FRAME_INFO))
    {
        PKS_FRAME_INFO Info = (PKS_FRAME_INFO)(Header + 1);

        RtlZeroMemory(Info, sizeof(*Info));
        Info->ExtendedHeaderSize = sizeof(KS_FRAME_INFO);
    }
}

static
NTSTATUS
NTAPI
IspPinProcess(
    _In_ PKSPIN Pin)
{
    PISP_PIN Context = Pin->Context;
    PISP_DEVICE Isp = Context->Isp;
    PISP_PRESET Preset = Context->Preset;
    PKSSTREAM_POINTER Leading;
    PISP_FRAME Frame = NULL;
    BOOLEAN Discontinuity, Newer = FALSE;
    ULONG Index;
    KIRQL Irql;

    if (!Context->Prepared)
        return STATUS_SUCCESS;
    KeAcquireSpinLock(&Isp->FrameLock, &Irql);
    for (Index = 0; Index < ISP_FRAME_COUNT && !Frame; ++Index)
    {
        if (Isp->Frames[Index].State == IspFrameReady)
        {
            Frame = &Isp->Frames[Index];
            Frame->State = IspFrameCopying;
        }
    }
    Discontinuity = Isp->Discontinuity && Isp->FramesDelivered;
    KeReleaseSpinLock(&Isp->FrameLock, Irql);
    if (!Frame)
        return STATUS_SUCCESS;

    Leading = KsPinGetLeadingEdgeStreamPointer(Pin, KSSTREAM_POINTER_STATE_LOCKED);
    if (!Leading || Leading->OffsetOut.Remaining < Preset->ImageSize)
    {
        KeAcquireSpinLock(&Isp->FrameLock, &Irql);
        for (Index = 0; Index < ISP_FRAME_COUNT; ++Index)
            Newer |= Isp->Frames[Index].State == IspFrameReady;
        if (Leading || Newer)
        {
            Frame->State = IspFrameFree;
            Isp->FramesDropped++;
            Isp->Discontinuity = TRUE;
        }
        else
        {
            Frame->State = IspFrameReady;
        }
        KeReleaseSpinLock(&Isp->FrameLock, Irql);
        KeSetEvent(&Isp->WorkEvent, IO_NO_INCREMENT, FALSE);
        if (!Leading)
            return STATUS_PENDING;
        KsStreamPointerUnlock(Leading, FALSE);
        return STATUS_SUCCESS;
    }

    /* TODO: map the client buffers into the ISP address space instead of copying each frame */
    IspCopyNv12(Leading->OffsetOut.Data, Frame->Surface->Virtual, Preset);
    IspFillHeader(Leading, Preset, Frame->Time, Discontinuity);
    KsStreamPointerAdvanceOffsetsAndUnlock(Leading, 0, Preset->ImageSize, TRUE);

    KeAcquireSpinLock(&Isp->FrameLock, &Irql);
    ASSERT(Frame->State == IspFrameCopying);
    Frame->State = IspFrameFree;
    Isp->FramesDelivered++;
    Isp->Discontinuity = FALSE;
    KeReleaseSpinLock(&Isp->FrameLock, Irql);
    KeSetEvent(&Isp->WorkEvent, IO_NO_INCREMENT, FALSE);
    if (Isp->FramesDelivered == 1)
        DbgPrint("appleisp: first %lux%lu frame delivered\n", Preset->OutputWidth, Preset->OutputHeight);
    return STATUS_SUCCESS;
}

static
NTSTATUS
IspApplyFraming(
    _In_ PKSPIN Pin,
    _In_ PISP_PRESET Preset)
{
    PKSALLOCATOR_FRAMING_EX Framing;
    NTSTATUS Status;

    Status = KsEdit(Pin, &Pin->Descriptor, ISP_TAG);
    if (!NT_SUCCESS(Status))
        return Status;
    Framing = ExAllocatePoolZero(NonPagedPool, sizeof(*Framing), ISP_TAG);
    if (!Framing)
        return STATUS_INSUFFICIENT_RESOURCES;
    Status = KsAddItemToObjectBag(Pin->Bag, Framing, ExFreePool);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Framing, ISP_TAG);
        return Status;
    }
    Framing->CountItems = 1;
    Framing->OutputCompression.RatioNumerator = 1;
    Framing->OutputCompression.RatioDenominator = 1;
    Framing->FramingItem[0].MemoryType = ISP_KSMEMORY_TYPE_KERNEL_NONPAGED;
    Framing->FramingItem[0].Flags = KSALLOCATOR_REQUIREMENTF_SYSTEM_MEMORY | KSALLOCATOR_REQUIREMENTF_PREFERENCES_ONLY;
    Framing->FramingItem[0].Frames = ISP_FRAME_COUNT;
    Framing->FramingItem[0].PhysicalRange.MinFrameSize = Preset->ImageSize;
    Framing->FramingItem[0].PhysicalRange.MaxFrameSize = Preset->ImageSize;
    Framing->FramingItem[0].FramingRange.Range.MinFrameSize = Preset->ImageSize;
    Framing->FramingItem[0].FramingRange.Range.MaxFrameSize = Preset->ImageSize;
    ((PKSPIN_DESCRIPTOR_EX)Pin->Descriptor)->AllocatorFraming = Framing;
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
IspPinCreate(
    _In_ PKSPIN Pin,
    _In_ PIRP Irp)
{
    PISP_DEVICE Isp = KsFilterGetDevice(KsPinGetParentFilter(Pin))->Context;
    PISP_PIN Context;
    KIRQL Irql;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(Irp);
    ASSERT(Isp);
    Context = ExAllocatePoolZero(NonPagedPool, sizeof(*Context), ISP_TAG);
    if (!Context)
        return STATUS_INSUFFICIENT_RESOURCES;
    Status = KsAddItemToObjectBag(Pin->Bag, Context, ExFreePool);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Context, ISP_TAG);
        return Status;
    }
    Context->Pin = Pin;
    Context->Isp = Isp;
    Context->Preset = IspFindPreset(Isp, Pin->ConnectionFormat);
    if (!Context->Preset)
        return STATUS_NO_MATCH;
    Status = IspApplyFraming(Pin, Context->Preset);
    if (!NT_SUCCESS(Status))
        return Status;
    KeAcquireSpinLock(&Isp->FrameLock, &Irql);
    if (Isp->Pin)
        Status = STATUS_DEVICE_BUSY;
    else
        Isp->Pin = Pin;
    KeReleaseSpinLock(&Isp->FrameLock, Irql);
    if (!NT_SUCCESS(Status))
        return Status;
    Pin->Context = Context;
    DbgPrint("appleisp: capture pin opened for %lux%lu NV12\n", Context->Preset->OutputWidth,
             Context->Preset->OutputHeight);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
IspPinClose(
    _In_ PKSPIN Pin,
    _In_ PIRP Irp)
{
    PISP_PIN Context = Pin->Context;
    KIRQL Irql;

    UNREFERENCED_PARAMETER(Irp);
    if (!Context)
        return STATUS_SUCCESS;
    IspStreamStop(Context);
    IspStreamUnprepare(Context);
    KeAcquireSpinLock(&Context->Isp->FrameLock, &Irql);
    ASSERT(Context->Isp->Pin == Pin);
    Context->Isp->Pin = NULL;
    KeReleaseSpinLock(&Context->Isp->FrameLock, Irql);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
IspPinSetDataFormat(
    _In_ PKSPIN Pin,
    _In_opt_ PKSDATAFORMAT OldFormat,
    _In_opt_ PKSMULTIPLE_ITEM OldAttributes,
    _In_ const KSDATARANGE *DataRange,
    _In_opt_ const KSATTRIBUTE_LIST *AttributeRange)
{
    PISP_DEVICE Isp = KsFilterGetDevice(KsPinGetParentFilter(Pin))->Context;
    PISP_PIN Context = Pin->Context == (PVOID)Isp ? NULL : Pin->Context;
    PISP_PRESET Preset;

    UNREFERENCED_PARAMETER(OldFormat);
    UNREFERENCED_PARAMETER(OldAttributes);
    UNREFERENCED_PARAMETER(DataRange);
    UNREFERENCED_PARAMETER(AttributeRange);
    Preset = IspFindPreset(Isp, Pin->ConnectionFormat);
    if (!Preset)
        return STATUS_NO_MATCH;
    if (Context && Preset != Context->Preset)
    {
        if (Context->Prepared)
            return STATUS_INVALID_DEVICE_STATE;
        Context->Preset = Preset;
    }
    return STATUS_SUCCESS;
}

static const KSPIN_DISPATCH IspPinDispatch =
{
    IspPinCreate,
    IspPinClose,
    IspPinProcess,
    NULL,
    IspPinSetDataFormat,
    IspPinSetDeviceState,
    NULL,
    NULL,
    NULL,
    NULL
};

NTSTATUS
IspBuildFilter(
    _In_ PISP_DEVICE Isp)
{
    PKSFILTER_DESCRIPTOR Descriptor;
    PKSPIN_DESCRIPTOR_EX Pin;
    PKSDATARANGE *Ranges;
    KS_DATARANGE_VIDEO *Range;
    KS_DATARANGE_VIDEO2 *Range2;
    ULONG Index;

    ASSERT(Isp->PresetCount && Isp->PresetCount <= ISP_MAX_PRESETS);
    Descriptor = IspAllocateBagged(Isp, sizeof(*Descriptor));
    Pin = IspAllocateBagged(Isp, sizeof(*Pin));
    Ranges = IspAllocateBagged(Isp, 2 * Isp->PresetCount * sizeof(*Ranges));
    if (!Descriptor || !Pin || !Ranges)
        return STATUS_INSUFFICIENT_RESOURCES;
    for (Index = 0; Index < Isp->PresetCount; ++Index)
    {
        Range = IspAllocateBagged(Isp, sizeof(*Range));
        Range2 = IspAllocateBagged(Isp, sizeof(*Range2));
        if (!Range || !Range2)
            return STATUS_INSUFFICIENT_RESOURCES;
        IspFillRange(Range, &Isp->Presets[Index]);
        IspFillRange2(Range2, &Isp->Presets[Index]);
        Ranges[2 * Index] = (PKSDATARANGE)Range;
        Ranges[2 * Index + 1] = (PKSDATARANGE)Range2;
    }
    Pin->Dispatch = &IspPinDispatch;
    Pin->Flags = KSPIN_FLAG_PROCESS_IN_RUN_STATE_ONLY;
    Pin->InstancesPossible = 1;
    Pin->InstancesNecessary = 0;
    Pin->IntersectHandler = IspPinIntersect;
    Pin->PinDescriptor.InterfacesCount = 1;
    Pin->PinDescriptor.Interfaces = &IspStandardInterface;
    Pin->PinDescriptor.MediumsCount = 1;
    Pin->PinDescriptor.Mediums = &IspStandardMedium;
    Pin->PinDescriptor.DataRangesCount = 2 * Isp->PresetCount;
    Pin->PinDescriptor.DataRanges = Ranges;
    Pin->PinDescriptor.DataFlow = KSPIN_DATAFLOW_OUT;
    Pin->PinDescriptor.Communication = KSPIN_COMMUNICATION_BOTH;
    Pin->PinDescriptor.Category = &ISP_PINNAME_VIDEO_CAPTURE;
    Pin->PinDescriptor.Name = &ISP_PINNAME_VIDEO_CAPTURE;

    /* TODO: camera controls, dropped-frame property, still pin and panel location are not exposed */
    Descriptor->Version = KSFILTER_DESCRIPTOR_VERSION;
    Descriptor->ReferenceGuid = &ISP_KSNAME_Filter;
    Descriptor->PinDescriptorsCount = 1;
    Descriptor->PinDescriptorSize = sizeof(KSPIN_DESCRIPTOR_EX);
    Descriptor->PinDescriptors = Pin;
    Descriptor->CategoriesCount = RTL_NUMBER_OF(IspFilterCategories);
    Descriptor->Categories = IspFilterCategories;
    Isp->FilterDescriptor = Descriptor;
    return STATUS_SUCCESS;
}
