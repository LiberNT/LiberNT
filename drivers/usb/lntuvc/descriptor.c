/*
 * PROJECT:     LiberNT USB Camera Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Parsing of the USB video class descriptors
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "lntuvc.h"
#include "uvcguid.h"

#define NDEBUG
#include <debug.h>

#define READ16(P) ((USHORT)((P)[0] | ((P)[1] << 8)))
#define READ32(P) ((ULONG)((P)[0] | ((P)[1] << 8) | ((P)[2] << 16) | ((ULONG)(P)[3] << 24)))

static
BOOLEAN
UvcNextDescriptor(
    PUCHAR End,
    PUCHAR *Position)
{
    PUCHAR Next = *Position + (*Position)[0];

    if ((*Position)[0] < 2 || Next + 2 > End || Next[0] < 2 || Next + Next[0] > End)
        return FALSE;

    *Position = Next;
    return TRUE;
}

static
VOID
UvcParseEntity(
    PUVC_ENTITY Entity,
    PUCHAR P)
{
    UCHAR Length = P[0];

    Entity->Subtype = P[2];
    Entity->Id = P[3];

    switch (P[2])
    {
        case UVC_VC_INPUT_TERMINAL:
            if (Length < 8)
                return;
            Entity->TerminalType = READ16(P + 4);
            if (Entity->TerminalType == UVC_ITT_CAMERA && Length >= 15)
            {
                Entity->ObjectiveFocalMin = READ16(P + 8);
                Entity->ObjectiveFocalMax = READ16(P + 10);
                Entity->OcularFocal = READ16(P + 12);
                Entity->ControlSize = P[14];
                if (15 + (ULONG)Entity->ControlSize <= Length)
                    Entity->Controls = P + 15;
                else
                    Entity->ControlSize = 0;
            }
            break;

        case UVC_VC_OUTPUT_TERMINAL:
            if (Length < 9)
                return;
            Entity->TerminalType = READ16(P + 4);
            Entity->SourceCount = 1;
            Entity->Sources = P + 7;
            break;

        case UVC_VC_SELECTOR_UNIT:
            if (Length < 5 || 5 + (ULONG)P[4] > Length)
                return;
            Entity->SourceCount = P[4];
            Entity->Sources = P + 5;
            break;

        case UVC_VC_PROCESSING_UNIT:
            if (Length < 8)
                return;
            Entity->SourceCount = 1;
            Entity->Sources = P + 4;
            Entity->ControlSize = P[7];
            if (8 + (ULONG)Entity->ControlSize <= Length)
                Entity->Controls = P + 8;
            else
                Entity->ControlSize = 0;
            break;

        case UVC_VC_EXTENSION_UNIT:
            if (Length < 24)
                return;
            RtlCopyMemory(&Entity->ExtensionCode, P + 4, sizeof(GUID));
            Entity->ExtensionControlCount = P[20];
            Entity->SourceCount = P[21];
            if (22 + (ULONG)Entity->SourceCount >= Length)
            {
                Entity->SourceCount = 0;
                return;
            }
            Entity->Sources = P + 22;
            Entity->ControlSize = P[22 + Entity->SourceCount];
            if (23 + (ULONG)Entity->SourceCount + Entity->ControlSize <= Length)
                Entity->Controls = P + 23 + Entity->SourceCount;
            else
                Entity->ControlSize = 0;
            break;
    }
}

static
NTSTATUS
UvcParseFrame(
    PUVC_DEVICE Device,
    PUVC_FORMAT Format,
    PUCHAR P)
{
    PUVC_FRAME Frame;
    ULONG Offset, Index, Type;

    if (Format->FrameCount == 0 || P[0] < 26)
        return STATUS_SUCCESS;

    for (Index = 0; Index < Format->FrameCount; Index++)
    {
        if (Format->Frames[Index].Index == 0)
            break;
    }
    if (Index == Format->FrameCount)
        return STATUS_SUCCESS;

    Frame = &Format->Frames[Index];
    Frame->Index = P[3];
    Frame->Width = READ16(P + 5);
    Frame->Height = READ16(P + 7);
    Frame->MinBitRate = READ32(P + 9);
    Frame->MaxBitRate = READ32(P + 13);

    if (P[2] == UVC_VS_FRAME_FRAME_BASED)
    {
        Frame->DefaultInterval = READ32(P + 17);
        Type = P[21];
        Offset = 26;
    }
    else
    {
        Frame->DefaultInterval = READ32(P + 21);
        Type = P[25];
        Offset = 26;
    }

    if (Type == 0)
    {
        if (Offset + 12 > P[0])
            return STATUS_SUCCESS;
        Frame->MinInterval = READ32(P + Offset);
        Frame->MaxInterval = READ32(P + Offset + 4);
        Frame->IntervalStep = READ32(P + Offset + 8);
    }
    else
    {
        if (Offset + Type * 4 > P[0])
            return STATUS_SUCCESS;
        Frame->Intervals = UvcAllocate(Device, Type * sizeof(ULONG));
        if (!Frame->Intervals)
            return STATUS_INSUFFICIENT_RESOURCES;
        Frame->IntervalCount = Type;
        Frame->MinInterval = MAXULONG;
        for (Index = 0; Index < Type; Index++)
        {
            Frame->Intervals[Index] = READ32(P + Offset + Index * 4);
            Frame->MinInterval = min(Frame->MinInterval, Frame->Intervals[Index]);
            Frame->MaxInterval = max(Frame->MaxInterval, Frame->Intervals[Index]);
        }
    }

    return STATUS_SUCCESS;
}

static
NTSTATUS
UvcParseFormat(
    PUVC_DEVICE Device,
    PUVC_FORMAT Format,
    PUCHAR P)
{
    Format->Subtype = P[2];
    Format->Index = P[3];
    Format->FrameCount = P[4];

    switch (P[2])
    {
        case UVC_VS_FORMAT_UNCOMPRESSED:
            if (P[0] < 27)
                return STATUS_DEVICE_DATA_ERROR;
            RtlCopyMemory(&Format->SubFormat, P + 5, sizeof(GUID));
            Format->BitCount = P[21];
            Format->Compression = Format->SubFormat.Data1;
            Format->FixedSize = TRUE;
            break;

        case UVC_VS_FORMAT_MJPEG:
            if (P[0] < 11)
                return STATUS_DEVICE_DATA_ERROR;
            Format->SubFormat = UVC_KSDATAFORMAT_SUBTYPE_MJPG;
            Format->BitCount = 24;
            Format->Compression = UVC_KSDATAFORMAT_SUBTYPE_MJPG.Data1;
            Format->FixedSize = FALSE;
            break;

        case UVC_VS_FORMAT_FRAME_BASED:
            if (P[0] < 28)
                return STATUS_DEVICE_DATA_ERROR;
            RtlCopyMemory(&Format->SubFormat, P + 5, sizeof(GUID));
            /* FIXME: the 24-bit fallback for bBitsPerPixel 0 is not checked against Windows 11 */
            Format->BitCount = P[21] ? P[21] : 24;
            Format->Compression = Format->SubFormat.Data1;
            Format->FixedSize = FALSE;
            break;
    }

    if (Format->FrameCount)
    {
        Format->Frames = UvcAllocate(Device, Format->FrameCount * sizeof(UVC_FRAME));
        if (!Format->Frames)
            return STATUS_INSUFFICIENT_RESOURCES;
    }

    return STATUS_SUCCESS;
}

static
NTSTATUS
UvcParseStill(
    PUVC_DEVICE Device,
    PUVC_FORMAT Format,
    PUCHAR P)
{
    ULONG Count, Index;

    if (P[0] < 6)
        return STATUS_SUCCESS;

    Count = P[4];
    if (Count == 0 || P[0] < 6 + Count * 4)
        return STATUS_SUCCESS;

    Format->StillSizes = UvcAllocate(Device, Count * sizeof(UVC_STILL_SIZE));
    if (!Format->StillSizes)
        return STATUS_INSUFFICIENT_RESOURCES;

    for (Index = 0; Index < Count; Index++)
    {
        Format->StillSizes[Index].Width = READ16(P + 5 + Index * 4);
        Format->StillSizes[Index].Height = READ16(P + 7 + Index * 4);
    }
    Format->StillCount = Count;
    Format->StillCompressions = P[5 + Count * 4];
    return STATUS_SUCCESS;
}

static
BOOLEAN
UvcIsFormat(
    UCHAR Subtype)
{
    return Subtype == UVC_VS_FORMAT_UNCOMPRESSED ||
           Subtype == UVC_VS_FORMAT_MJPEG ||
           Subtype == UVC_VS_FORMAT_FRAME_BASED;
}

static
BOOLEAN
UvcIsFrame(
    UCHAR Subtype)
{
    return Subtype == UVC_VS_FRAME_UNCOMPRESSED ||
           Subtype == UVC_VS_FRAME_MJPEG ||
           Subtype == UVC_VS_FRAME_FRAME_BASED;
}

static
BOOLEAN
UvcIsEntity(
    UCHAR Subtype)
{
    return Subtype >= UVC_VC_INPUT_TERMINAL && Subtype <= UVC_VC_EXTENSION_UNIT;
}

static
VOID
UvcParseEndpoint(
    PUVC_DEVICE Device,
    PUCHAR P,
    PUCHAR End,
    UCHAR Alternate)
{
    PUVC_ALT Alt;
    PUCHAR Next = P;
    USHORT Packet;
    UCHAR Type;

    if (P[0] < 7 || !(P[2] & 0x80) || Device->StreamAltCount >= UVC_MAX_ALTS)
        return;

    Type = P[3] & USB_ENDPOINT_TYPE_MASK;
    if (Type != USB_ENDPOINT_TYPE_ISOCHRONOUS && Type != USB_ENDPOINT_TYPE_BULK)
        return;

    Packet = READ16(P + 4);
    Alt = &Device->Alts[Device->StreamAltCount++];
    Alt->AlternateSetting = Alternate;
    Alt->EndpointAddress = P[2];
    Alt->Attributes = P[3];
    Alt->MaxPacketSize = Packet;
    Alt->Bytes = (Packet & 0x7FF) * (1 + ((Packet >> 11) & 3));

    if (UvcNextDescriptor(End, &Next) && Next[1] == USB_SUPERSPEED_ENDPOINT_COMPANION_DESCRIPTOR_TYPE && Next[0] >= 6)
    {
        USHORT Burst = READ16(Next + 4);

        if (Burst)
            Alt->Bytes = Burst;
    }

    if (Type == USB_ENDPOINT_TYPE_BULK)
        Device->Bulk = TRUE;
}

static
VOID
UvcLogTopology(
    PUVC_DEVICE Device)
{
    ULONG I, J;

    DPRINT1("USB video function: UVC %x, %lu entities, %lu formats, %u streaming endpoints (%s)\n",
            Device->UvcVersion,
            Device->EntityCount,
            Device->FormatCount,
            Device->StreamAltCount,
            Device->Bulk ? "bulk" : "isochronous");

    for (I = 0; I < Device->FormatCount; I++)
    {
        PUVC_FORMAT Format = &Device->Formats[I];

        for (J = 0; J < Format->FrameCount; J++)
        {
            PUVC_FRAME Frame = &Format->Frames[J];

            DPRINT1("USB video format %u subtype %u frame %u: %ux%u interval %lu..%lu\n",
                    Format->Index,
                    Format->Subtype,
                    Frame->Index,
                    Frame->Width,
                    Frame->Height,
                    Frame->MinInterval,
                    Frame->MaxInterval);
        }
    }
}

NTSTATUS
UvcParseDescriptors(
    PUVC_DEVICE Device)
{
    PUSB_CONFIGURATION_DESCRIPTOR Config = Device->Config;
    PUCHAR Start = (PUCHAR)Config;
    PUCHAR End = Start + Config->wTotalLength;
    PUCHAR P;
    ULONG Entities = 0, Formats = 0;
    ULONG EntityIndex = 0, FormatIndex = 0;
    UCHAR Subclass, Interface, Alternate;
    BOOLEAN HaveControl = FALSE;
    BOOLEAN InFormat = FALSE;
    NTSTATUS Status;

    P = Start;
    Subclass = 0;
    Alternate = 0;
    Interface = 0;
    do
    {
        if (P[1] == USB_INTERFACE_DESCRIPTOR_TYPE && P[0] >= 9)
        {
            Interface = P[2];
            Alternate = P[3];
            Subclass = P[5] == USB_DEVICE_CLASS_VIDEO ? P[6] : 0;

            if (Subclass == UVC_SC_VIDEOCONTROL && !HaveControl)
            {
                HaveControl = TRUE;
                Device->ControlInterface = Interface;
            }
            if (Subclass == UVC_SC_VIDEOSTREAMING && !Device->HasStream)
            {
                Device->HasStream = TRUE;
                Device->StreamInterface = Interface;
            }
        }
        else if (P[1] == UVC_CS_INTERFACE && P[0] >= 3)
        {
            if (Subclass == UVC_SC_VIDEOCONTROL && UvcIsEntity(P[2]))
                Entities++;
            else if (Subclass == UVC_SC_VIDEOSTREAMING && Interface == Device->StreamInterface && Alternate == 0 && UvcIsFormat(P[2]))
                Formats++;
        }
    } while (UvcNextDescriptor(End, &P));

    if (!HaveControl || !Device->HasStream || Entities == 0 || Formats == 0)
    {
        DPRINT1("USB video function without a usable control and streaming interface\n");
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    Device->Entities = UvcAllocate(Device, Entities * sizeof(UVC_ENTITY));
    Device->Formats = UvcAllocate(Device, Formats * sizeof(UVC_FORMAT));
    if (!Device->Entities || !Device->Formats)
        return STATUS_INSUFFICIENT_RESOURCES;

    P = Start;
    Subclass = 0;
    Alternate = 0;
    Interface = 0;
    do
    {
        if (P[1] == USB_INTERFACE_DESCRIPTOR_TYPE && P[0] >= 9)
        {
            Interface = P[2];
            Alternate = P[3];
            Subclass = P[5] == USB_DEVICE_CLASS_VIDEO ? P[6] : 0;
        }
        else if (P[1] == USB_ENDPOINT_DESCRIPTOR_TYPE)
        {
            if (Subclass == UVC_SC_VIDEOSTREAMING && Interface == Device->StreamInterface)
                UvcParseEndpoint(Device, P, End, Alternate);
        }
        else if (P[1] == UVC_CS_INTERFACE && P[0] >= 3)
        {
            if (Subclass == UVC_SC_VIDEOCONTROL)
            {
                if (P[2] == UVC_VC_HEADER && P[0] >= 11)
                {
                    Device->UvcVersion = READ16(P + 3);
                    Device->ClockFrequency = READ32(P + 7);
                }
                else if (UvcIsEntity(P[2]) && P[0] >= 4)
                {
                    UvcParseEntity(&Device->Entities[EntityIndex], P);
                    EntityIndex++;
                }
            }
            else if (Subclass == UVC_SC_VIDEOSTREAMING && Interface == Device->StreamInterface && Alternate == 0)
            {
                if (P[2] == UVC_VS_INPUT_HEADER && P[0] >= 10)
                {
                    Device->TerminalLink = P[8];
                    Device->StillCaptureMethod = P[9];
                }
                else if (UvcIsFormat(P[2]) && P[0] >= 5)
                {
                    Status = UvcParseFormat(Device, &Device->Formats[FormatIndex], P);
                    if (!NT_SUCCESS(Status))
                        return Status;
                    FormatIndex++;
                    InFormat = TRUE;
                }
                else if (UvcIsFrame(P[2]) && InFormat)
                {
                    Status = UvcParseFrame(Device, &Device->Formats[FormatIndex - 1], P);
                    if (!NT_SUCCESS(Status))
                        return Status;
                }
                else if (P[2] == UVC_VS_STILL_IMAGE_FRAME)
                {
                    if (InFormat)
                    {
                        Status = UvcParseStill(Device, &Device->Formats[FormatIndex - 1], P);
                        if (!NT_SUCCESS(Status))
                            return Status;
                    }
                }
                else if (P[2] == UVC_VS_COLOR_FORMAT)
                {
                    if (InFormat && P[0] >= 6)
                    {
                        Device->Formats[FormatIndex - 1].HasColor = TRUE;
                        Device->Formats[FormatIndex - 1].ColorPrimaries = P[3];
                        Device->Formats[FormatIndex - 1].ColorMatrix = P[5];
                    }
                }
                else
                {
                    DPRINT1("USB video function: skipping streaming descriptor subtype %u\n", P[2]);
                    InFormat = FALSE;
                }
            }
        }
    } while (UvcNextDescriptor(End, &P));

    Device->EntityCount = EntityIndex;
    Device->FormatCount = FormatIndex;

    for (FormatIndex = Device->FormatCount; FormatIndex > 1; FormatIndex--)
    {
        PUVC_FORMAT Later = &Device->Formats[FormatIndex - 1];
        PUVC_FORMAT Earlier = &Device->Formats[FormatIndex - 2];

        if (!Earlier->HasColor && Later->HasColor)
        {
            Earlier->HasColor = TRUE;
            Earlier->ColorPrimaries = Later->ColorPrimaries;
            Earlier->ColorMatrix = Later->ColorMatrix;
        }
    }

    for (FormatIndex = 0; FormatIndex < Device->FormatCount; FormatIndex++)
    {
        PUVC_FORMAT Format = &Device->Formats[FormatIndex];
        ULONG Filled = 0;

        while (Filled < Format->FrameCount && Format->Frames[Filled].Index != 0)
            Filled++;
        Format->FrameCount = Filled;
    }

    if (Device->StreamAltCount == 0)
    {
        DPRINT1("USB video function without a streaming endpoint\n");
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    UvcLogTopology(Device);
    return STATUS_SUCCESS;
}

PUVC_ENTITY
UvcFindEntity(
    PUVC_DEVICE Device,
    UCHAR Subtype,
    USHORT TerminalType)
{
    ULONG Index;

    for (Index = 0; Index < Device->EntityCount; Index++)
    {
        PUVC_ENTITY Entity = &Device->Entities[Index];

        if (Entity->Subtype == Subtype && (!TerminalType || Entity->TerminalType == TerminalType))
            return Entity;
    }

    return NULL;
}

BOOLEAN
UvcEntityHasControl(
    PUVC_ENTITY Entity,
    ULONG Bit)
{
    if (!Entity || !Entity->Controls || Bit / 8 >= Entity->ControlSize)
        return FALSE;

    return (Entity->Controls[Bit / 8] & (1 << (Bit % 8))) != 0;
}

USHORT
UvcImageBits(
    PUVC_FORMAT Format)
{
    return Format->BitCount;
}

ULONG
UvcImageSize(
    PUVC_FORMAT Format,
    PUVC_FRAME Frame)
{
    return (ULONG)Frame->Width * Frame->Height * Format->BitCount / 8;
}
