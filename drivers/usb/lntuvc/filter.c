/*
 * PROJECT:     LiberNT USB Camera Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     KS filter description of a USB video function
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "lntuvc.h"
#include "uvcguid.h"

#define NDEBUG
#include <debug.h>


C_ASSERT(sizeof(KS_DATARANGE_VIDEO) == 296);
C_ASSERT(sizeof(KS_DATARANGE_VIDEO2) == 320);

static const GUID UvcFilterCategories[] =
{
    { 0x6994ad05L, 0x93ef, 0x11d0, { 0xa3, 0xcc, 0x00, 0xa0, 0xc9, 0x22, 0x31, 0x96 } },
    { 0x65e8773dL, 0x8f56, 0x11d0, { 0xa3, 0xb9, 0x00, 0xa0, 0xc9, 0x22, 0x31, 0x96 } },
    { 0xe5323777L, 0xf976, 0x4f5b, { 0x9b, 0x55, 0xb9, 0x46, 0x99, 0xc4, 0x6e, 0x44 } },
};

static const KSPIN_INTERFACE UvcStandardInterface =
{
    { 0x1a8766a0L, 0x62ce, 0x11cf, { 0xa5, 0xd6, 0x28, 0xdb, 0x04, 0xc1, 0x00, 0x00 } },
    KSINTERFACE_STANDARD_STREAMING,
    0
};

static const KSPIN_MEDIUM UvcStandardMedium =
{
    { 0x4747b320L, 0x62ce, 0x11cf, { 0xa5, 0xd6, 0x28, 0xdb, 0x04, 0xc1, 0x00, 0x00 } },
    KSMEDIUM_TYPE_ANYINSTANCE,
    0
};

static
VOID
UvcFillConfigCaps(
    KS_VIDEO_STREAM_CONFIG_CAPS *Caps,
    const GUID *Specifier,
    PUVC_FRAME Frame)
{
    Caps->guid = *Specifier;
    Caps->InputSize.cx = Frame->Width;
    Caps->InputSize.cy = Frame->Height;
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
    Caps->MinFrameInterval = Frame->MinInterval;
    Caps->MaxFrameInterval = Frame->MaxInterval;
    Caps->MinBitsPerSecond = Frame->MinBitRate;
    Caps->MaxBitsPerSecond = Frame->MaxBitRate;
}

static
VOID
UvcFillBitmapHeader(
    KS_BITMAPINFOHEADER *Header,
    PUVC_FORMAT Format,
    PUVC_FRAME Frame)
{
    Header->biSize = sizeof(KS_BITMAPINFOHEADER);
    Header->biWidth = Frame->Width;
    Header->biHeight = Frame->Height;
    Header->biPlanes = 1;
    Header->biBitCount = UvcImageBits(Format);
    Header->biCompression = Format->Compression;
    Header->biSizeImage = UvcImageSize(Format, Frame);
}

static
ULONG
UvcBitRate(
    PUVC_FORMAT Format,
    PUVC_FRAME Frame,
    ULONG Interval)
{
    return UvcImageSize(Format, Frame) * 8 * (Interval ? 10000000 / Interval : 0);
}

static
VOID
UvcFillRange(
    KS_DATARANGE_VIDEO *Range,
    PUVC_FORMAT Format,
    PUVC_FRAME Frame)
{
    Range->DataRange.FormatSize = sizeof(*Range);
    Range->DataRange.SampleSize = UvcImageSize(Format, Frame);
    Range->DataRange.MajorFormat = UVC_KSDATAFORMAT_TYPE_VIDEO;
    Range->DataRange.SubFormat = Format->SubFormat;
    Range->DataRange.Specifier = UVC_KSDATAFORMAT_SPECIFIER_VIDEOINFO;
    Range->bFixedSizeSamples = Format->FixedSize;
    UvcFillConfigCaps(&Range->ConfigCaps, &UVC_KSDATAFORMAT_SPECIFIER_VIDEOINFO, Frame);
    Range->VideoInfoHeader.dwBitRate = UvcBitRate(Format, Frame, Frame->DefaultInterval);
    Range->VideoInfoHeader.AvgTimePerFrame = Frame->DefaultInterval;
    UvcFillBitmapHeader(&Range->VideoInfoHeader.bmiHeader, Format, Frame);
}

#define UVC_AMCONTROL_FLAGS 0x81
#define UVC_STILL_INTERVAL 10000000
#define UVC_LIGHTING_DIM 3

static
ULONG
UvcColorFlags(
    PUVC_FORMAT Format)
{
    BOOLEAN Jpeg = Format->Subtype == UVC_VS_FORMAT_MJPEG;
    UCHAR Chroma = Jpeg ? 1 : 6;
    UCHAR Nominal = Jpeg ? 1 : 2;
    UCHAR Matrix = 2;
    UCHAR Primaries = 2;

    if (Format->HasColor)
    {
        if (Format->ColorMatrix == 1)
            Matrix = 1;
        else if (Format->ColorMatrix == 5)
            Matrix = 3;
        if (Format->ColorPrimaries >= 1 && Format->ColorPrimaries <= 5)
            Primaries = Format->ColorPrimaries + 1;
    }

    return UVC_AMCONTROL_FLAGS | ((ULONG)Chroma << 8) | ((ULONG)Nominal << 12) |
           ((ULONG)Matrix << 15) | ((ULONG)UVC_LIGHTING_DIM << 18) | ((ULONG)Primaries << 22);
}

static
VOID
UvcFillRange2(
    KS_DATARANGE_VIDEO2 *Range,
    PUVC_FORMAT Format,
    PUVC_FRAME Frame)
{
    ULONG X = Frame->Width, Y = Frame->Height, A = X, B = Y;

    while (B)
    {
        ULONG T = A % B;

        A = B;
        B = T;
    }

    Range->DataRange.FormatSize = sizeof(*Range);
    Range->DataRange.SampleSize = UvcImageSize(Format, Frame);
    Range->DataRange.MajorFormat = UVC_KSDATAFORMAT_TYPE_VIDEO;
    Range->DataRange.SubFormat = Format->SubFormat;
    Range->DataRange.Specifier = UVC_KSDATAFORMAT_SPECIFIER_VIDEOINFO2;
    Range->bFixedSizeSamples = Format->FixedSize;
    UvcFillConfigCaps(&Range->ConfigCaps, &UVC_KSDATAFORMAT_SPECIFIER_VIDEOINFO2, Frame);
    Range->VideoInfoHeader.dwBitRate = UvcBitRate(Format, Frame, Frame->DefaultInterval);
    Range->VideoInfoHeader.AvgTimePerFrame = Frame->DefaultInterval;
    Range->VideoInfoHeader.dwPictAspectRatioX = A ? X / A : 0;
    Range->VideoInfoHeader.dwPictAspectRatioY = A ? Y / A : 0;
    Range->VideoInfoHeader.dwControlFlags = UvcColorFlags(Format);
    UvcFillBitmapHeader(&Range->VideoInfoHeader.bmiHeader, Format, Frame);
}

static
NTSTATUS
UvcBuildStillRanges(
    PUVC_DEVICE Device,
    const KSDATARANGE ***OutRanges,
    PULONG OutCount)
{
    const KSDATARANGE **Ranges;
    ULONG Count = 0, Index = 0, F, I;

    for (F = 0; F < Device->FormatCount; F++)
        Count += Device->Formats[F].StillCount;

    Ranges = UvcAllocate(Device, Count * sizeof(*Ranges));
    if (!Ranges)
        return STATUS_INSUFFICIENT_RESOURCES;

    for (F = 0; F < Device->FormatCount; F++)
    {
        PUVC_FORMAT Format = &Device->Formats[F];

        for (I = 0; I < Format->StillCount; I++)
        {
            KS_DATARANGE_VIDEO *Range = UvcAllocate(Device, sizeof(*Range));
            UVC_FRAME Frame;

            if (!Range)
                return STATUS_INSUFFICIENT_RESOURCES;

            RtlZeroMemory(&Frame, sizeof(Frame));
            Frame.Width = Format->StillSizes[I].Width;
            Frame.Height = Format->StillSizes[I].Height;
            Frame.DefaultInterval = UVC_STILL_INTERVAL;
            Frame.MinInterval = UVC_STILL_INTERVAL;
            Frame.MaxInterval = UVC_STILL_INTERVAL;
            Frame.MinBitRate = UvcBitRate(Format, &Frame, UVC_STILL_INTERVAL);
            Frame.MaxBitRate = Frame.MinBitRate;
            UvcFillRange(Range, Format, &Frame);
            Ranges[Index++] = (const KSDATARANGE *)Range;
        }
    }

    *OutRanges = Ranges;
    *OutCount = Index;
    return STATUS_SUCCESS;
}

static
NTSTATUS
UvcBuildRanges(
    PUVC_DEVICE Device,
    const KSDATARANGE ***OutRanges,
    PULONG OutCount)
{
    const KSDATARANGE **Ranges;
    ULONG Count = 0, Index = 0, F, I;

    for (F = 0; F < Device->FormatCount; F++)
        Count += Device->Formats[F].FrameCount * 2;

    Ranges = UvcAllocate(Device, Count * sizeof(*Ranges));
    if (!Ranges)
        return STATUS_INSUFFICIENT_RESOURCES;

    for (F = 0; F < Device->FormatCount; F++)
    {
        PUVC_FORMAT Format = &Device->Formats[F];

        for (I = 0; I < Format->FrameCount; I++)
        {
            KS_DATARANGE_VIDEO *Range = UvcAllocate(Device, sizeof(*Range));
            KS_DATARANGE_VIDEO2 *Range2 = UvcAllocate(Device, sizeof(*Range2));

            if (!Range || !Range2)
                return STATUS_INSUFFICIENT_RESOURCES;

            UvcFillRange(Range, Format, &Format->Frames[I]);
            UvcFillRange2(Range2, Format, &Format->Frames[I]);
            Ranges[Index++] = (const KSDATARANGE *)Range;
            Ranges[Index++] = (const KSDATARANGE *)Range2;
        }
    }

    *OutRanges = Ranges;
    *OutCount = Index;
    return STATUS_SUCCESS;
}

static
const GUID *
UvcNodeType(
    PUVC_ENTITY Entity)
{
    switch (Entity->Subtype)
    {
        case UVC_VC_INPUT_TERMINAL:
            return Entity->TerminalType == UVC_ITT_CAMERA ? &UVC_KSNODETYPE_VIDEO_CAMERA_TERMINAL : &UVC_KSNODETYPE_VIDEO_INPUT_TERMINAL;
        case UVC_VC_OUTPUT_TERMINAL:
            return Entity->TerminalType == UVC_TT_STREAMING ? &UVC_KSNODETYPE_VIDEO_STREAMING : &UVC_KSNODETYPE_VIDEO_OUTPUT_TERMINAL;
        case UVC_VC_SELECTOR_UNIT:
            return &UVC_KSNODETYPE_VIDEO_SELECTOR;
        case UVC_VC_PROCESSING_UNIT:
            return &UVC_KSNODETYPE_VIDEO_PROCESSING;
        default:
            return &UVC_KSNODETYPE_DEV_SPECIFIC;
    }
}

static
PUVC_ENTITY
UvcEntityById(
    PUVC_DEVICE Device,
    UCHAR Id)
{
    ULONG Index;

    for (Index = 0; Index < Device->EntityCount; Index++)
    {
        if (Device->Entities[Index].Id == Id)
            return &Device->Entities[Index];
    }

    return NULL;
}

static
BOOLEAN
UvcHasStillPin(
    PUVC_DEVICE Device)
{
    ULONG Index;

    if (Device->StillCaptureMethod != UVC_STILL_METHOD_TRANSMIT)
        return FALSE;

    for (Index = 0; Index < Device->FormatCount; Index++)
    {
        if (Device->Formats[Index].StillCount)
            return TRUE;
    }

    return FALSE;
}

static
NTSTATUS
UvcBuildTopology(
    PUVC_DEVICE Device,
    PKSFILTER_DESCRIPTOR Descriptor)
{
    PKSNODE_DESCRIPTOR Nodes;
    PKSTOPOLOGY_CONNECTION Connections;
    ULONG Count = 0, Index, Source, Used = 0;
    NTSTATUS Status;

    for (Index = 0; Index < Device->EntityCount; Index++)
    {
        Device->Entities[Index].NodeIndex = Index;
        Count += max(Device->Entities[Index].SourceCount, 1);
    }
    Count += 3;

    Nodes = UvcAllocate(Device, Device->EntityCount * sizeof(KSNODE_DESCRIPTOR));
    Connections = UvcAllocate(Device, Count * sizeof(KSTOPOLOGY_CONNECTION));
    if (!Nodes || !Connections)
        return STATUS_INSUFFICIENT_RESOURCES;

    for (Index = 0; Index < Device->EntityCount; Index++)
    {
        PUVC_ENTITY Entity = &Device->Entities[Index];

        Nodes[Index].Type = UvcNodeType(Entity);

        if (Entity->Subtype == UVC_VC_INPUT_TERMINAL)
        {
            Connections[Used].FromNode = KSFILTER_NODE;
            Connections[Used].FromNodePin = UVC_PIN_BRIDGE;
            Connections[Used].ToNode = Index;
            Connections[Used].ToNodePin = 1;
            Used++;
        }

        for (Source = 0; Source < Entity->SourceCount; Source++)
        {
            PUVC_ENTITY From = UvcEntityById(Device, Entity->Sources[Source]);

            if (!From)
                continue;

            Connections[Used].FromNode = From - Device->Entities;
            Connections[Used].FromNodePin = 0;
            Connections[Used].ToNode = Index;
            Connections[Used].ToNodePin = 1 + Source;
            Used++;
        }

        if (Entity->Subtype == UVC_VC_OUTPUT_TERMINAL && Entity->Id == Device->TerminalLink)
        {
            Connections[Used].FromNode = Index;
            Connections[Used].FromNodePin = 0;
            Connections[Used].ToNode = KSFILTER_NODE;
            Connections[Used].ToNodePin = UVC_PIN_CAPTURE;
            Used++;
            if (UvcHasStillPin(Device))
            {
                Connections[Used].FromNode = Index;
                Connections[Used].FromNodePin = 1;
                Connections[Used].ToNode = KSFILTER_NODE;
                Connections[Used].ToNodePin = UVC_PIN_STILL;
                Used++;
            }
        }
    }

    Status = UvcBuildControlTables(Device, Descriptor, Nodes);
    if (!NT_SUCCESS(Status))
        return Status;

    Descriptor->NodeDescriptorsCount = Device->EntityCount;
    Descriptor->NodeDescriptorSize = sizeof(KSNODE_DESCRIPTOR);
    Descriptor->NodeDescriptors = Nodes;
    Descriptor->ConnectionsCount = Used;
    Descriptor->Connections = Connections;
    return STATUS_SUCCESS;
}

NTSTATUS
UvcBuildFilter(
    PUVC_DEVICE Device)
{
    PKSFILTER_DESCRIPTOR Descriptor;
    PKSPIN_DESCRIPTOR_EX Pins;
    KSCOMPONENTID *Component;
    NTSTATUS Status;

    Descriptor = UvcAllocate(Device, sizeof(*Descriptor));
    Pins = UvcAllocate(Device, 3 * sizeof(*Pins));
    Component = UvcAllocate(Device, sizeof(*Component));
    if (!Descriptor || !Pins || !Component)
        return STATUS_INSUFFICIENT_RESOURCES;

    Pins[UVC_PIN_CAPTURE].Dispatch = &UvcPinDispatch;
    Pins[UVC_PIN_CAPTURE].PinDescriptor.InterfacesCount = 1;
    Pins[UVC_PIN_CAPTURE].PinDescriptor.Interfaces = &UvcStandardInterface;
    Pins[UVC_PIN_CAPTURE].PinDescriptor.MediumsCount = 1;
    Pins[UVC_PIN_CAPTURE].PinDescriptor.Mediums = &UvcStandardMedium;
    Pins[UVC_PIN_CAPTURE].PinDescriptor.DataFlow = KSPIN_DATAFLOW_OUT;
    Pins[UVC_PIN_CAPTURE].PinDescriptor.Communication = KSPIN_COMMUNICATION_BOTH;
    Pins[UVC_PIN_CAPTURE].PinDescriptor.Category = &UVC_PINNAME_VIDEO_CAPTURE;
    Pins[UVC_PIN_CAPTURE].PinDescriptor.Name = &UVC_PINNAME_VIDEO_CAPTURE;
    Pins[UVC_PIN_CAPTURE].Flags = KSPIN_FLAG_PROCESS_IN_RUN_STATE_ONLY;
    Pins[UVC_PIN_CAPTURE].InstancesPossible = 1;
    Pins[UVC_PIN_CAPTURE].AutomationTable = UvcPinAutomationTable(Device);
    Pins[UVC_PIN_CAPTURE].IntersectHandler = UvcPinIntersect;
    Status = UvcBuildRanges(Device,
                            (const KSDATARANGE ***)&Pins[UVC_PIN_CAPTURE].PinDescriptor.DataRanges,
                            &Pins[UVC_PIN_CAPTURE].PinDescriptor.DataRangesCount);
    if (!NT_SUCCESS(Status))
        return Status;

    Pins[UVC_PIN_BRIDGE].PinDescriptor.InterfacesCount = 1;
    Pins[UVC_PIN_BRIDGE].PinDescriptor.Interfaces = &UvcStandardInterface;
    Pins[UVC_PIN_BRIDGE].PinDescriptor.MediumsCount = 1;
    Pins[UVC_PIN_BRIDGE].PinDescriptor.Mediums = &UvcStandardMedium;
    Pins[UVC_PIN_BRIDGE].PinDescriptor.DataFlow = KSPIN_DATAFLOW_IN;
    Pins[UVC_PIN_BRIDGE].PinDescriptor.Communication = KSPIN_COMMUNICATION_BRIDGE;
    Pins[UVC_PIN_BRIDGE].PinDescriptor.Category = &UVC_PINNAME_BRIDGE;

    if (UvcHasStillPin(Device))
    {
        Pins[UVC_PIN_STILL].Dispatch = &UvcStillDispatch;
        Pins[UVC_PIN_STILL].PinDescriptor.InterfacesCount = 1;
        Pins[UVC_PIN_STILL].PinDescriptor.Interfaces = &UvcStandardInterface;
        Pins[UVC_PIN_STILL].PinDescriptor.MediumsCount = 1;
        Pins[UVC_PIN_STILL].PinDescriptor.Mediums = &UvcStandardMedium;
        Pins[UVC_PIN_STILL].PinDescriptor.DataFlow = KSPIN_DATAFLOW_OUT;
        Pins[UVC_PIN_STILL].PinDescriptor.Communication = KSPIN_COMMUNICATION_BOTH;
        Pins[UVC_PIN_STILL].PinDescriptor.Category = &UVC_PINNAME_VIDEO_STILL;
        Pins[UVC_PIN_STILL].PinDescriptor.Name = &UVC_PINNAME_VIDEO_STILL;
        Pins[UVC_PIN_STILL].Flags = KSPIN_FLAG_PROCESS_IN_RUN_STATE_ONLY;
        Pins[UVC_PIN_STILL].InstancesPossible = 1;
        Pins[UVC_PIN_STILL].IntersectHandler = UvcPinIntersect;
        Status = UvcBuildStillRanges(Device,
                                     (const KSDATARANGE ***)&Pins[UVC_PIN_STILL].PinDescriptor.DataRanges,
                                     &Pins[UVC_PIN_STILL].PinDescriptor.DataRangesCount);
        if (!NT_SUCCESS(Status))
            return Status;
    }

    Component->Manufacturer = UVC_COMPONENT_MANUFACTURER;
    Component->Manufacturer.Data1 += Device->DeviceDescriptor.idVendor;
    Component->Product = UVC_COMPONENT_PRODUCT;
    Component->Product.Data1 += Device->DeviceDescriptor.idProduct;
    Component->Component = UVC_KSCOMPONENTID_CAMERA;
    Component->Version = 1;

    Descriptor->Version = KSFILTER_DESCRIPTOR_VERSION;
    Descriptor->ReferenceGuid = &UVC_KSNAME_Filter;
    Descriptor->PinDescriptorsCount = UvcHasStillPin(Device) ? 3 : 2;
    Descriptor->PinDescriptorSize = sizeof(KSPIN_DESCRIPTOR_EX);
    Descriptor->PinDescriptors = Pins;
    Descriptor->CategoriesCount = RTL_NUMBER_OF(UvcFilterCategories);
    Descriptor->Categories = UvcFilterCategories;
    Descriptor->ComponentId = Component;

    Status = UvcBuildTopology(Device, Descriptor);
    if (!NT_SUCCESS(Status))
        return Status;

    Device->FilterDescriptor = Descriptor;
    return STATUS_SUCCESS;
}

static
const KS_BITMAPINFOHEADER *
UvcFormatHeader(
    PKSDATAFORMAT DataFormat,
    LONGLONG *Interval)
{
    if (IsEqualGUIDAligned(&DataFormat->Specifier, &UVC_KSDATAFORMAT_SPECIFIER_VIDEOINFO) &&
        DataFormat->FormatSize >= sizeof(KS_DATAFORMAT_VIDEOINFOHEADER))
    {
        const KS_DATAFORMAT_VIDEOINFOHEADER *Video = (const KS_DATAFORMAT_VIDEOINFOHEADER *)DataFormat;

        *Interval = Video->VideoInfoHeader.AvgTimePerFrame;
        return &Video->VideoInfoHeader.bmiHeader;
    }

    if (IsEqualGUIDAligned(&DataFormat->Specifier, &UVC_KSDATAFORMAT_SPECIFIER_VIDEOINFO2) &&
        DataFormat->FormatSize >= sizeof(KS_DATAFORMAT_VIDEOINFOHEADER2))
    {
        const KS_DATAFORMAT_VIDEOINFOHEADER2 *Video = (const KS_DATAFORMAT_VIDEOINFOHEADER2 *)DataFormat;

        *Interval = Video->VideoInfoHeader2.AvgTimePerFrame;
        return &Video->VideoInfoHeader2.bmiHeader;
    }

    return NULL;
}

static
ULONG
UvcNearestInterval(
    PUVC_FRAME Frame,
    ULONG Requested)
{
    ULONG Index, Best = Frame->DefaultInterval;
    ULONG BestDistance = MAXULONG;

    if (Frame->IntervalCount == 0)
    {
        if (Frame->IntervalStep)
        {
            ULONG Steps = (Requested - Frame->MinInterval + Frame->IntervalStep / 2) / Frame->IntervalStep;

            return min(Frame->MinInterval + Steps * Frame->IntervalStep, Frame->MaxInterval);
        }
        return Requested;
    }

    for (Index = 0; Index < Frame->IntervalCount; Index++)
    {
        ULONG Value = Frame->Intervals[Index];
        ULONG Distance = Value > Requested ? Value - Requested : Requested - Value;

        if (Distance < BestDistance)
        {
            BestDistance = Distance;
            Best = Value;
        }
    }

    return Best;
}

NTSTATUS
UvcFindFormat(
    PUVC_DEVICE Device,
    PKSDATAFORMAT DataFormat,
    PUVC_FORMAT *OutFormat,
    PUVC_FRAME *OutFrame,
    PULONG OutInterval)
{
    const KS_BITMAPINFOHEADER *Header;
    LONGLONG Interval = 0;
    ULONG F, I;

    if (!IsEqualGUIDAligned(&DataFormat->MajorFormat, &UVC_KSDATAFORMAT_TYPE_VIDEO))
        return STATUS_NO_MATCH;

    Header = UvcFormatHeader(DataFormat, &Interval);
    if (!Header)
        return STATUS_NO_MATCH;

    for (F = 0; F < Device->FormatCount; F++)
    {
        PUVC_FORMAT Format = &Device->Formats[F];

        if (!IsEqualGUIDAligned(&DataFormat->SubFormat, &Format->SubFormat) ||
            Header->biCompression != Format->Compression ||
            Header->biBitCount != UvcImageBits(Format))
        {
            continue;
        }

        for (I = 0; I < Format->FrameCount; I++)
        {
            PUVC_FRAME Frame = &Format->Frames[I];
            LONG Height = Header->biHeight < 0 ? -Header->biHeight : Header->biHeight;

            if (Header->biWidth != Frame->Width || Height != Frame->Height ||
                Header->biSizeImage != UvcImageSize(Format, Frame))
            {
                continue;
            }

            if (Interval == 0)
                Interval = Frame->DefaultInterval;
            if (Interval < Frame->MinInterval || Interval > Frame->MaxInterval)
                continue;

            *OutFormat = Format;
            *OutFrame = Frame;
            *OutInterval = UvcNearestInterval(Frame, (ULONG)Interval);
            return STATUS_SUCCESS;
        }
    }

    return STATUS_NO_MATCH;
}

NTSTATUS
NTAPI
UvcPinIntersect(
    PVOID Context,
    PIRP Irp,
    PKSP_PIN PinInstance,
    PKSDATARANGE CallerRange,
    PKSDATARANGE DescriptorRange,
    ULONG BufferSize,
    PVOID Data,
    PULONG DataSize)
{
    ULONG Size;
    PUCHAR Output = Data;

    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(Irp);
    UNREFERENCED_PARAMETER(PinInstance);

    if (!IsEqualGUIDAligned(&CallerRange->MajorFormat, &DescriptorRange->MajorFormat) ||
        !IsEqualGUIDAligned(&CallerRange->SubFormat, &DescriptorRange->SubFormat) ||
        !IsEqualGUIDAligned(&CallerRange->Specifier, &DescriptorRange->Specifier))
    {
        return STATUS_NO_MATCH;
    }

    if (IsEqualGUIDAligned(&DescriptorRange->Specifier, &UVC_KSDATAFORMAT_SPECIFIER_VIDEOINFO))
    {
        KS_DATARANGE_VIDEO *Range = (KS_DATARANGE_VIDEO *)DescriptorRange;

        if (CallerRange->FormatSize >= sizeof(KS_DATARANGE_VIDEO))
        {
            const KS_DATARANGE_VIDEO *Caller = (const KS_DATARANGE_VIDEO *)CallerRange;

            if (Caller->VideoInfoHeader.bmiHeader.biWidth != Range->VideoInfoHeader.bmiHeader.biWidth ||
                Caller->VideoInfoHeader.bmiHeader.biHeight != Range->VideoInfoHeader.bmiHeader.biHeight)
            {
                return STATUS_NO_MATCH;
            }
        }

        Size = sizeof(KS_DATAFORMAT_VIDEOINFOHEADER);
        *DataSize = Size;
        if (BufferSize == 0)
            return STATUS_BUFFER_OVERFLOW;
        if (BufferSize < Size)
            return STATUS_BUFFER_TOO_SMALL;

        RtlCopyMemory(Output, &Range->DataRange, sizeof(KSDATARANGE));
        ((PKSDATAFORMAT)Output)->FormatSize = Size;
        RtlCopyMemory(&((KS_DATAFORMAT_VIDEOINFOHEADER *)Output)->VideoInfoHeader,
                      &Range->VideoInfoHeader,
                      sizeof(KS_VIDEOINFOHEADER));
        return STATUS_SUCCESS;
    }

    if (IsEqualGUIDAligned(&DescriptorRange->Specifier, &UVC_KSDATAFORMAT_SPECIFIER_VIDEOINFO2))
    {
        KS_DATARANGE_VIDEO2 *Range = (KS_DATARANGE_VIDEO2 *)DescriptorRange;

        if (CallerRange->FormatSize >= sizeof(KS_DATARANGE_VIDEO2))
        {
            const KS_DATARANGE_VIDEO2 *Caller = (const KS_DATARANGE_VIDEO2 *)CallerRange;

            if (Caller->VideoInfoHeader.bmiHeader.biWidth != Range->VideoInfoHeader.bmiHeader.biWidth ||
                Caller->VideoInfoHeader.bmiHeader.biHeight != Range->VideoInfoHeader.bmiHeader.biHeight)
            {
                return STATUS_NO_MATCH;
            }
        }

        Size = sizeof(KS_DATAFORMAT_VIDEOINFOHEADER2);
        *DataSize = Size;
        if (BufferSize == 0)
            return STATUS_BUFFER_OVERFLOW;
        if (BufferSize < Size)
            return STATUS_BUFFER_TOO_SMALL;

        RtlCopyMemory(Output, &Range->DataRange, sizeof(KSDATARANGE));
        ((PKSDATAFORMAT)Output)->FormatSize = Size;
        RtlCopyMemory(&((KS_DATAFORMAT_VIDEOINFOHEADER2 *)Output)->VideoInfoHeader2,
                      &Range->VideoInfoHeader,
                      sizeof(KS_VIDEOINFOHEADER2));
        return STATUS_SUCCESS;
    }

    return STATUS_NO_MATCH;
}
