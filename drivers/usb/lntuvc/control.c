/*
 * PROJECT:     LiberNT USB Camera Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Video processing, camera and extension unit controls
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "lntuvc.h"
#include "uvcguid.h"

#define NDEBUG
#include <debug.h>

C_ASSERT(sizeof(KSIDENTIFIER) == 24);
C_ASSERT(sizeof(KSPROPERTY_VIDEOPROCAMP_S) == 40);
C_ASSERT(sizeof(KSPROPERTY_CAMERACONTROL_S) == 40);

#define UVC_VT_I4                           3
#define UVC_ARCSEC_PER_DEGREE               3600

#define UVC_CT_AE_MODE_MANUAL               0x01
#define UVC_CT_AE_MODE_AUTO                 0x02
#define UVC_CT_AE_MODE_SHUTTER_PRIORITY     0x04
#define UVC_CT_AE_MODE_APERTURE_PRIORITY    0x08

typedef enum _UVC_CONVERSION
{
    UvcConvertNone,
    UvcConvertExposure,
    UvcConvertPan,
    UvcConvertTilt,
    UvcConvertIris,
    UvcConvertRelative
} UVC_CONVERSION;

typedef enum _UVC_AUTO_KIND
{
    UvcAutoSwitch,
    UvcAutoExposure,
    UvcAutoIris
} UVC_AUTO_KIND;

typedef struct _UVC_CONTROL_MAP
{
    ULONG Id;
    ULONG Bit;
    UCHAR Selector;
    UCHAR Length;
    BOOLEAN Signed;
    UVC_CONVERSION Conversion;
    ULONG AutoBit;
    UCHAR AutoSelector;
    BOOLEAN NoFlags;
    UVC_AUTO_KIND AutoKind;
    UCHAR Direction;
    UCHAR Speed;
    BOOLEAN SignedRead;
} UVC_CONTROL_MAP;

typedef struct _UVC_CONTROL_DATA
{
    LONG Value;
    ULONG Flags;
    ULONG Capabilities;
} UVC_CONTROL_DATA, *PUVC_CONTROL_DATA;

#define UVC_NO_AUTO MAXULONG
#define UVC_IRIS_SCALE 10
#define UVC_RELATIVE_FORWARD 0x01
#define UVC_RELATIVE_BACKWARD 0xFF

static const UVC_CONTROL_MAP UvcProcAmpMap[] =
{
    { KSPROPERTY_VIDEOPROCAMP_BRIGHTNESS, 0, 0x02, 2, TRUE, UvcConvertNone, UVC_NO_AUTO, 0 },
    { KSPROPERTY_VIDEOPROCAMP_CONTRAST, 1, 0x03, 2, FALSE, UvcConvertNone, UVC_NO_AUTO, 0 },
    { KSPROPERTY_VIDEOPROCAMP_HUE, 2, 0x06, 2, TRUE, UvcConvertNone, 11, 0x10 },
    { KSPROPERTY_VIDEOPROCAMP_SATURATION, 3, 0x07, 2, FALSE, UvcConvertNone, UVC_NO_AUTO, 0 },
    { KSPROPERTY_VIDEOPROCAMP_SHARPNESS, 4, 0x08, 2, FALSE, UvcConvertNone, UVC_NO_AUTO, 0 },
    { KSPROPERTY_VIDEOPROCAMP_GAMMA, 5, 0x09, 2, FALSE, UvcConvertNone, UVC_NO_AUTO, 0 },
    { KSPROPERTY_VIDEOPROCAMP_WHITEBALANCE, 6, 0x0A, 2, FALSE, UvcConvertNone, 12, 0x0B },
    { KSPROPERTY_VIDEOPROCAMP_BACKLIGHT_COMPENSATION, 8, 0x01, 2, FALSE, UvcConvertNone, UVC_NO_AUTO, 0 },
    { KSPROPERTY_VIDEOPROCAMP_GAIN, 9, 0x04, 2, FALSE, UvcConvertNone, UVC_NO_AUTO, 0 },
    { KSPROPERTY_VIDEOPROCAMP_DIGITAL_MULTIPLIER, 14, 0x0E, 2, FALSE, UvcConvertNone, UVC_NO_AUTO, 0, TRUE },
    { KSPROPERTY_VIDEOPROCAMP_DIGITAL_MULTIPLIER_LIMIT, 15, 0x0F, 2, FALSE, UvcConvertNone, UVC_NO_AUTO, 0, TRUE },
    { KSPROPERTY_VIDEOPROCAMP_POWERLINE_FREQUENCY, 10, 0x05, 1, FALSE, UvcConvertNone, UVC_NO_AUTO, 0, TRUE },
    { KSPROPERTY_VIDEOPROCAMP_WHITEBALANCE_COMPONENT, 7, 0x0C, 4, FALSE, UvcConvertNone, 13, 0x0D },
};

static const UVC_CONTROL_MAP UvcCameraMap[] =
{
    { KSPROPERTY_CAMERACONTROL_PAN, 11, 0x0D, 8, TRUE, UvcConvertPan, UVC_NO_AUTO, 0 },
    { KSPROPERTY_CAMERACONTROL_TILT, 11, 0x0D, 8, TRUE, UvcConvertTilt, UVC_NO_AUTO, 0 },
    { KSPROPERTY_CAMERACONTROL_ROLL, 13, 0x0F, 2, TRUE, UvcConvertNone, UVC_NO_AUTO, 0 },
    { KSPROPERTY_CAMERACONTROL_ZOOM, 9, 0x0B, 2, FALSE, UvcConvertNone, UVC_NO_AUTO, 0 },
    { KSPROPERTY_CAMERACONTROL_EXPOSURE, 3, 0x04, 4, FALSE, UvcConvertExposure, 1, 0x02, FALSE, UvcAutoExposure },
    { KSPROPERTY_CAMERACONTROL_IRIS, 7, 0x09, 2, FALSE, UvcConvertIris, 1, 0x02, FALSE, UvcAutoIris },
    { KSPROPERTY_CAMERACONTROL_FOCUS, 5, 0x06, 2, FALSE, UvcConvertNone, 17, 0x08 },
    { KSPROPERTY_CAMERACONTROL_SCANMODE, 0, 0x01, 1, FALSE, UvcConvertNone, UVC_NO_AUTO, 0, TRUE },
    { KSPROPERTY_CAMERACONTROL_PRIVACY, 18, 0x11, 1, FALSE, UvcConvertNone, UVC_NO_AUTO, 0, TRUE },
    { KSPROPERTY_CAMERACONTROL_PAN_RELATIVE, 12, 0x0E, 4, FALSE, UvcConvertRelative, UVC_NO_AUTO, 0, TRUE, UvcAutoSwitch, 0, 1, FALSE },
    { KSPROPERTY_CAMERACONTROL_TILT_RELATIVE, 12, 0x0E, 4, FALSE, UvcConvertRelative, UVC_NO_AUTO, 0, TRUE, UvcAutoSwitch, 2, 3, FALSE },
    { KSPROPERTY_CAMERACONTROL_ROLL_RELATIVE, 14, 0x10, 2, FALSE, UvcConvertRelative, UVC_NO_AUTO, 0, TRUE, UvcAutoSwitch, 0, 1, FALSE },
    { KSPROPERTY_CAMERACONTROL_ZOOM_RELATIVE, 10, 0x0C, 3, FALSE, UvcConvertRelative, UVC_NO_AUTO, 0, FALSE, UvcAutoSwitch, 0, 2, TRUE },
    { KSPROPERTY_CAMERACONTROL_EXPOSURE_RELATIVE, 4, 0x05, 1, FALSE, UvcConvertNone, 1, 0x02, FALSE, UvcAutoExposure },
    { KSPROPERTY_CAMERACONTROL_IRIS_RELATIVE, 8, 0x0A, 1, FALSE, UvcConvertNone, 1, 0x02, FALSE, UvcAutoIris },
    { KSPROPERTY_CAMERACONTROL_FOCUS_RELATIVE, 6, 0x07, 2, FALSE, UvcConvertRelative, 17, 0x08, FALSE, UvcAutoSwitch, 0, 1, TRUE },
    { KSPROPERTY_CAMERACONTROL_AUTO_EXPOSURE_PRIORITY, 2, 0x03, 1, FALSE, UvcConvertNone, UVC_NO_AUTO, 0, TRUE },
};

static
NTSTATUS
UvcRawRequest(
    PUVC_DEVICE Device,
    PUVC_ENTITY Entity,
    UCHAR Request,
    UCHAR Selector,
    PVOID Data,
    ULONG Length)
{
    USHORT Index = (USHORT)((Entity->Id << 8) | Device->ControlInterface);
    USHORT Value = (USHORT)(Selector << 8);

    return UvcClassRequest(Device, Request < 0x80 ? FALSE : TRUE, Request, Value, Index, Data, Length, NULL);
}

static
LONG
UvcFromBytes(
    const UCHAR *Data,
    const UVC_CONTROL_MAP *Map)
{
    ULONG Value = 0;
    ULONG Offset = Map->Conversion == UvcConvertTilt ? 4 : 0;
    ULONG Length = Map->Length == 8 ? 4 : Map->Length;
    ULONG Index;

    if (Map->Conversion == UvcConvertRelative)
        return Data[Map->Speed];

    for (Index = 0; Index < Length; Index++)
        Value |= (ULONG)Data[Offset + Index] << (8 * Index);

    if (Map->Signed && Length == 2)
        return (SHORT)Value;
    if (Map->Signed && Length == 1)
        return (CHAR)Value;
    return (LONG)Value;
}

static
LONG
UvcExposureToLog2(
    LONG Raw)
{
    LONG Best = 0, Shift;
    LONG BestDistance = MAXLONG;

    for (Shift = 0; Shift < 31; Shift++)
    {
        LONG Candidate = 10000 >> Shift;
        LONG Distance = Candidate > Raw ? Candidate - Raw : Raw - Candidate;

        if (Distance < BestDistance)
        {
            BestDistance = Distance;
            Best = -Shift;
        }
        if (Candidate == 0)
            break;
    }

    return Best;
}

static
LONG
UvcToProperty(
    const UVC_CONTROL_MAP *Map,
    LONG Raw)
{
    switch (Map->Conversion)
    {
        case UvcConvertExposure:
            return UvcExposureToLog2(Raw);
        case UvcConvertPan:
        case UvcConvertTilt:
            return Raw / UVC_ARCSEC_PER_DEGREE;
        case UvcConvertIris:
            return Raw * UVC_IRIS_SCALE;
        default:
            return Raw;
    }
}

static
LONG
UvcToRaw(
    const UVC_CONTROL_MAP *Map,
    LONG Value)
{
    switch (Map->Conversion)
    {
        case UvcConvertExposure:
            return Value >= 0 ? (10000 << min(Value, 16)) : (10000 >> min(-Value, 30));
        case UvcConvertPan:
        case UvcConvertTilt:
            return Value * UVC_ARCSEC_PER_DEGREE;
        case UvcConvertIris:
            return Value / UVC_IRIS_SCALE;
        default:
            return Value;
    }
}

static
PUVC_CONTROL_DATA
UvcControlData(
    PKSIDENTIFIER Request,
    PVOID Data)
{
    return (PUVC_CONTROL_DATA)((PUCHAR)Data + ((Request->Flags & KSPROPERTY_TYPE_TOPOLOGY) ? sizeof(KSP_NODE) : sizeof(KSPROPERTY)));
}

static
ULONG
UvcControlSize(
    PKSIDENTIFIER Request)
{
    return (Request->Flags & KSPROPERTY_TYPE_TOPOLOGY) ? sizeof(KSPROPERTY_VIDEOPROCAMP_NODE_S) : sizeof(KSPROPERTY_VIDEOPROCAMP_S);
}

static
const UVC_CONTROL_MAP *
UvcLookup(
    PIRP Irp,
    PKSIDENTIFIER Request,
    PUVC_DEVICE *OutDevice,
    PUVC_ENTITY *OutEntity)
{
    PUVC_DEVICE Device = KsFilterGetDevice(KsGetFilterFromIrp(Irp))->Context;
    const UVC_CONTROL_MAP *Table;
    PUVC_ENTITY Entity;
    ULONG Count, Index;

    if (IsEqualGUIDAligned(&Request->Set, &UVC_PROPSETID_VIDCAP_VIDEOPROCAMP))
    {
        Table = UvcProcAmpMap;
        Count = RTL_NUMBER_OF(UvcProcAmpMap);
        Entity = UvcFindEntity(Device, UVC_VC_PROCESSING_UNIT, 0);
    }
    else
    {
        Table = UvcCameraMap;
        Count = RTL_NUMBER_OF(UvcCameraMap);
        Entity = UvcFindEntity(Device, UVC_VC_INPUT_TERMINAL, UVC_ITT_CAMERA);
    }

    for (Index = 0; Index < Count; Index++)
    {
        if (Table[Index].Id == Request->Id)
        {
            *OutDevice = Device;
            *OutEntity = Entity;
            return &Table[Index];
        }
    }

    return NULL;
}

static
NTSTATUS
UvcGetAutoFlags(
    PUVC_DEVICE Device,
    PUVC_ENTITY Entity,
    const UVC_CONTROL_MAP *Map,
    PULONG Flags,
    PULONG Capabilities)
{
    UCHAR Value = 0;
    NTSTATUS Status;

    *Flags = Map->NoFlags ? 0 : KSPROPERTY_VIDEOPROCAMP_FLAGS_MANUAL;
    *Capabilities = Map->NoFlags ? 0 : KSPROPERTY_VIDEOPROCAMP_FLAGS_MANUAL;

    if (Map->AutoBit == UVC_NO_AUTO || !UvcEntityHasControl(Entity, Map->AutoBit))
        return STATUS_SUCCESS;

    *Capabilities |= KSPROPERTY_VIDEOPROCAMP_FLAGS_AUTO;

    Status = UvcRawRequest(Device, Entity, UVC_GET_CUR, Map->AutoSelector, &Value, 1);
    if (!NT_SUCCESS(Status))
        return Status;

    if (Map->AutoKind == UvcAutoExposure)
    {
        if (Value & (UVC_CT_AE_MODE_AUTO | UVC_CT_AE_MODE_APERTURE_PRIORITY))
            *Flags = KSPROPERTY_VIDEOPROCAMP_FLAGS_AUTO;
    }
    else if (Map->AutoKind == UvcAutoIris)
    {
        if (Value & (UVC_CT_AE_MODE_AUTO | UVC_CT_AE_MODE_SHUTTER_PRIORITY))
            *Flags = KSPROPERTY_VIDEOPROCAMP_FLAGS_AUTO;
    }
    else if (Value)
    {
        *Flags = KSPROPERTY_VIDEOPROCAMP_FLAGS_AUTO;
    }

    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
UvcControlGet(
    PIRP Irp,
    PKSIDENTIFIER Request,
    PVOID Data)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    const UVC_CONTROL_MAP *Map;
    PUVC_CONTROL_DATA Reply;
    ULONG Size = UvcControlSize(Request);
    PUVC_DEVICE Device;
    PUVC_ENTITY Entity;
    UCHAR Raw[8] = { 0 };
    ULONG Flags, Capabilities;
    NTSTATUS Status;

    Map = UvcLookup(Irp, Request, &Device, &Entity);
    if (!Map || !UvcEntityHasControl(Entity, Map->Bit))
        return STATUS_NOT_FOUND;

    if (Stack->Parameters.DeviceIoControl.OutputBufferLength < Size || !Data)
        return STATUS_BUFFER_TOO_SMALL;

    Status = UvcRawRequest(Device, Entity, UVC_GET_CUR, Map->Selector, Raw, Map->Length);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = UvcGetAutoFlags(Device, Entity, Map, &Flags, &Capabilities);
    if (!NT_SUCCESS(Status))
        return Status;

    RtlZeroMemory(Data, Size);
    Reply = UvcControlData(Request, Data);
    Reply->Value = UvcToProperty(Map, UvcFromBytes(Raw, Map));
    if (Map->SignedRead && Raw[Map->Direction] == UVC_RELATIVE_BACKWARD)
        Reply->Value = -Reply->Value;
    Reply->Flags = Flags;
    Reply->Capabilities = Capabilities;
    Irp->IoStatus.Information = Size;
    return STATUS_SUCCESS;
}

static
NTSTATUS
UvcSetAuto(
    PUVC_DEVICE Device,
    PUVC_ENTITY Entity,
    const UVC_CONTROL_MAP *Map,
    BOOLEAN Automatic)
{
    UCHAR Value = Automatic;

    if (Map->AutoBit == UVC_NO_AUTO || !UvcEntityHasControl(Entity, Map->AutoBit))
        return Automatic ? STATUS_INVALID_PARAMETER : STATUS_SUCCESS;

    if (Map->AutoKind != UvcAutoSwitch)
    {
        UCHAR Current = 0;
        BOOLEAN ExposureAuto, IrisAuto;
        NTSTATUS Status = UvcRawRequest(Device, Entity, UVC_GET_CUR, Map->AutoSelector, &Current, 1);

        if (!NT_SUCCESS(Status))
            return Status;

        ExposureAuto = (Current & (UVC_CT_AE_MODE_AUTO | UVC_CT_AE_MODE_APERTURE_PRIORITY)) != 0;
        IrisAuto = (Current & (UVC_CT_AE_MODE_AUTO | UVC_CT_AE_MODE_SHUTTER_PRIORITY)) != 0;
        if (Map->AutoKind == UvcAutoExposure)
            ExposureAuto = Automatic;
        else
            IrisAuto = Automatic;

        if (ExposureAuto)
            Value = IrisAuto ? UVC_CT_AE_MODE_AUTO : UVC_CT_AE_MODE_APERTURE_PRIORITY;
        else
            Value = IrisAuto ? UVC_CT_AE_MODE_SHUTTER_PRIORITY : UVC_CT_AE_MODE_MANUAL;
    }

    return UvcRawRequest(Device, Entity, UVC_SET_CUR, Map->AutoSelector, &Value, 1);
}

static
NTSTATUS
NTAPI
UvcControlSet(
    PIRP Irp,
    PKSIDENTIFIER Request,
    PVOID Data)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PUVC_CONTROL_DATA Input;
    const UVC_CONTROL_MAP *Map;
    PUVC_DEVICE Device;
    PUVC_ENTITY Entity;
    UCHAR Raw[8] = { 0 };
    LONG Value;
    NTSTATUS Status;

    Map = UvcLookup(Irp, Request, &Device, &Entity);
    if (!Map || !UvcEntityHasControl(Entity, Map->Bit))
        return STATUS_NOT_FOUND;

    if (Stack->Parameters.DeviceIoControl.OutputBufferLength < UvcControlSize(Request) || !Data)
        return STATUS_BUFFER_TOO_SMALL;

    Input = UvcControlData(Request, Data);

    Status = UvcSetAuto(Device, Entity, Map, (Input->Flags & KSPROPERTY_VIDEOPROCAMP_FLAGS_AUTO) != 0);
    if (!NT_SUCCESS(Status))
        return Status;

    if (Input->Flags & KSPROPERTY_VIDEOPROCAMP_FLAGS_AUTO)
        return STATUS_SUCCESS;

    if (Map->Length == 8 || (Map->Conversion == UvcConvertRelative && Map->Length == 4))
    {
        Status = UvcRawRequest(Device, Entity, UVC_GET_CUR, Map->Selector, Raw, Map->Length);
        if (!NT_SUCCESS(Status))
            return Status;
    }

    Value = UvcToRaw(Map, Input->Value);
    if (Map->Conversion == UvcConvertRelative)
    {
        Raw[Map->Direction] = Value > 0 ? UVC_RELATIVE_FORWARD : (Value < 0 ? UVC_RELATIVE_BACKWARD : 0);
        Raw[Map->Speed] = (UCHAR)(Value < 0 ? -Value : Value);
    }
    else if (Map->Conversion == UvcConvertTilt)
        RtlCopyMemory(Raw + 4, &Value, 4);
    else
        RtlCopyMemory(Raw, &Value, min(Map->Length, 4));

    return UvcRawRequest(Device, Entity, UVC_SET_CUR, Map->Selector, Raw, Map->Length);
}

static
NTSTATUS
UvcWhiteBalanceRequest(
    PIRP Irp,
    PKSIDENTIFIER Request,
    const UVC_CONTROL_MAP **OutMap,
    PUVC_DEVICE *OutDevice,
    PUVC_ENTITY *OutEntity)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);

    if (Stack->Parameters.DeviceIoControl.InputBufferLength < sizeof(KSPROPERTY_VIDEOPROCAMP_NODE_S2) ||
        Stack->Parameters.DeviceIoControl.OutputBufferLength < sizeof(KSPROPERTY_VIDEOPROCAMP_NODE_S2))
    {
        return STATUS_BUFFER_TOO_SMALL;
    }
    if (!(Request->Flags & KSPROPERTY_TYPE_TOPOLOGY))
        return STATUS_INVALID_PARAMETER;

    *OutMap = UvcLookup(Irp, Request, OutDevice, OutEntity);
    if (!*OutMap || !UvcEntityHasControl(*OutEntity, (*OutMap)->Bit))
        return STATUS_NOT_FOUND;
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
UvcWhiteBalanceGet(
    PIRP Irp,
    PKSIDENTIFIER Request,
    PVOID Data)
{
    PKSPROPERTY_VIDEOPROCAMP_NODE_S2 Reply = Data;
    const UVC_CONTROL_MAP *Map;
    PUVC_DEVICE Device;
    PUVC_ENTITY Entity;
    USHORT Raw[2] = { 0 };
    ULONG Flags, Capabilities;
    NTSTATUS Status;

    Status = UvcWhiteBalanceRequest(Irp, Request, &Map, &Device, &Entity);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = UvcRawRequest(Device, Entity, UVC_GET_CUR, Map->Selector, Raw, sizeof(Raw));
    if (NT_SUCCESS(Status))
        Status = UvcGetAutoFlags(Device, Entity, Map, &Flags, &Capabilities);
    if (!NT_SUCCESS(Status))
        return Status;

    RtlZeroMemory(Reply, sizeof(*Reply));
    Reply->Value1 = Raw[0];
    Reply->Value2 = Raw[1];
    Reply->Flags = Flags;
    Reply->Capabilities = Capabilities;
    Irp->IoStatus.Information = sizeof(*Reply);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
UvcWhiteBalanceSet(
    PIRP Irp,
    PKSIDENTIFIER Request,
    PVOID Data)
{
    PKSPROPERTY_VIDEOPROCAMP_NODE_S2 Input = Data;
    const UVC_CONTROL_MAP *Map;
    PUVC_DEVICE Device;
    PUVC_ENTITY Entity;
    USHORT Raw[2];
    NTSTATUS Status;

    Status = UvcWhiteBalanceRequest(Irp, Request, &Map, &Device, &Entity);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = UvcSetAuto(Device, Entity, Map, (Input->Flags & KSPROPERTY_VIDEOPROCAMP_FLAGS_AUTO) != 0);
    if (!NT_SUCCESS(Status) || (Input->Flags & KSPROPERTY_VIDEOPROCAMP_FLAGS_AUTO))
        return Status;

    Raw[0] = (USHORT)Input->Value1;
    Raw[1] = (USHORT)Input->Value2;
    return UvcRawRequest(Device, Entity, UVC_SET_CUR, Map->Selector, Raw, sizeof(Raw));
}

static
NTSTATUS
UvcReadBound(
    PUVC_DEVICE Device,
    PUVC_ENTITY Entity,
    const UVC_CONTROL_MAP *Map,
    UCHAR Request,
    PLONG Value)
{
    UCHAR Raw[8] = { 0 };
    NTSTATUS Status;

    Status = UvcRawRequest(Device, Entity, Request, Map->Selector, Raw, Map->Length);
    if (NT_SUCCESS(Status))
        *Value = UvcFromBytes(Raw, Map);
    return Status;
}

static
NTSTATUS
NTAPI
UvcControlSupport(
    PIRP Irp,
    PKSIDENTIFIER Request,
    PVOID Data)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    ULONG Output = Stack->Parameters.DeviceIoControl.OutputBufferLength;
    PKSPROPERTY_DESCRIPTION Description = Data;
    PKSPROPERTY_MEMBERSHEADER Header;
    const UVC_CONTROL_MAP *Map;
    PUVC_DEVICE Device;
    PUVC_ENTITY Entity;
    BOOLEAN Defaults = (Request->Flags & KSPROPERTY_TYPE_DEFAULTVALUES) != 0;
    ULONG Size;
    NTSTATUS Status;

    Map = UvcLookup(Irp, Request, &Device, &Entity);
    if (!Map || !UvcEntityHasControl(Entity, Map->Bit))
        return STATUS_NOT_FOUND;

    if (Output == sizeof(ULONG))
    {
        *(PULONG)Data = KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_SET;
        Irp->IoStatus.Information = sizeof(ULONG);
        return STATUS_SUCCESS;
    }

    Size = sizeof(KSPROPERTY_DESCRIPTION) + sizeof(KSPROPERTY_MEMBERSHEADER) +
           (Defaults ? sizeof(LONG) : sizeof(KSPROPERTY_STEPPING_LONG));

    if (Output < Size && Output != sizeof(KSPROPERTY_DESCRIPTION))
        return STATUS_BUFFER_TOO_SMALL;

    Description->AccessFlags = KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_SET;
    Description->DescriptionSize = Size;
    Description->PropTypeSet.Set = UVC_KSPROPTYPESETID_General;
    Description->PropTypeSet.Id = UVC_VT_I4;
    Description->PropTypeSet.Flags = 0;
    Description->MembersListCount = 1;
    Description->Reserved = 0;
    Irp->IoStatus.Information = sizeof(KSPROPERTY_DESCRIPTION);

    if (Output < Size)
        return STATUS_SUCCESS;

    Header = (PKSPROPERTY_MEMBERSHEADER)(Description + 1);
    Header->MembersCount = 1;

    if (Defaults)
    {
        LONG Value;

        Status = UvcReadBound(Device, Entity, Map, UVC_GET_DEF, &Value);
        if (!NT_SUCCESS(Status))
            return Status;

        Header->MembersFlags = KSPROPERTY_MEMBER_VALUES;
        Header->MembersSize = sizeof(LONG);
        Header->Flags = KSPROPERTY_MEMBER_FLAG_DEFAULT;
        *(PLONG)(Header + 1) = UvcToProperty(Map, Value);
    }
    else
    {
        PKSPROPERTY_STEPPING_LONG Step = (PKSPROPERTY_STEPPING_LONG)(Header + 1);
        LONG Minimum, Maximum, Resolution;

        Status = UvcReadBound(Device, Entity, Map, UVC_GET_MIN, &Minimum);
        if (NT_SUCCESS(Status))
            Status = UvcReadBound(Device, Entity, Map, UVC_GET_MAX, &Maximum);
        if (NT_SUCCESS(Status))
            Status = UvcReadBound(Device, Entity, Map, UVC_GET_RES, &Resolution);
        if (!NT_SUCCESS(Status))
            return Status;

        Header->MembersFlags = KSPROPERTY_MEMBER_RANGES;
        Header->MembersSize = sizeof(KSPROPERTY_STEPPING_LONG);
        Header->Flags = 0;
        Step->SteppingDelta = Map->Conversion == UvcConvertExposure ? 1 : (ULONG)UvcToProperty(Map, Resolution);
        Step->Reserved = 0;
        Step->Bounds.SignedMinimum = UvcToProperty(Map, Minimum);
        Step->Bounds.SignedMaximum = UvcToProperty(Map, Maximum);
    }

    Irp->IoStatus.Information = Size;
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
UvcFocalLengthGet(
    PIRP Irp,
    PKSIDENTIFIER Request,
    PVOID Data)
{
    PUVC_DEVICE Device = KsFilterGetDevice(KsGetFilterFromIrp(Irp))->Context;
    PUVC_ENTITY Camera = UvcFindEntity(Device, UVC_VC_INPUT_TERMINAL, UVC_ITT_CAMERA);
    PLONG Reply = (PLONG)UvcControlData(Request, Data);
    ULONG Size = UvcControlSize(Request);

    if (!Camera)
        return STATUS_NOT_FOUND;

    RtlZeroMemory(Data, Size);
    Reply[0] = Camera->OcularFocal;
    Reply[1] = Camera->ObjectiveFocalMin;
    Reply[2] = Camera->ObjectiveFocalMax;
    Irp->IoStatus.Information = Size;
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
UvcPanTiltGet(
    PIRP Irp,
    PKSIDENTIFIER Request,
    PVOID Data)
{
    PUVC_DEVICE Device = KsFilterGetDevice(KsGetFilterFromIrp(Irp))->Context;
    PUVC_ENTITY Camera = UvcFindEntity(Device, UVC_VC_INPUT_TERMINAL, UVC_ITT_CAMERA);
    PLONG Reply = (PLONG)UvcControlData(Request, Data);
    ULONG Size = UvcControlSize(Request);
    LONG Raw[2];
    NTSTATUS Status;

    if (!UvcEntityHasControl(Camera, 11))
        return STATUS_NOT_FOUND;

    Status = UvcRawRequest(Device, Camera, UVC_GET_CUR, 0x0D, Raw, sizeof(Raw));
    if (!NT_SUCCESS(Status))
        return Status;

    RtlZeroMemory(Data, Size);
    Reply[0] = Raw[0] / UVC_ARCSEC_PER_DEGREE;
    Reply[1] = KSPROPERTY_CAMERACONTROL_FLAGS_MANUAL;
    Reply[2] = KSPROPERTY_CAMERACONTROL_FLAGS_MANUAL;
    Reply[3] = Raw[1] / UVC_ARCSEC_PER_DEGREE;
    Irp->IoStatus.Information = Size;
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
UvcPanTiltSet(
    PIRP Irp,
    PKSIDENTIFIER Request,
    PVOID Data)
{
    PUVC_DEVICE Device = KsFilterGetDevice(KsGetFilterFromIrp(Irp))->Context;
    PUVC_ENTITY Camera = UvcFindEntity(Device, UVC_VC_INPUT_TERMINAL, UVC_ITT_CAMERA);
    PLONG Input = (PLONG)UvcControlData(Request, Data);
    LONG Raw[2];

    if (!UvcEntityHasControl(Camera, 11))
        return STATUS_NOT_FOUND;

    Raw[0] = Input[0] * UVC_ARCSEC_PER_DEGREE;
    Raw[1] = Input[3] * UVC_ARCSEC_PER_DEGREE;
    return UvcRawRequest(Device, Camera, UVC_SET_CUR, 0x0D, Raw, sizeof(Raw));
}

static
PUVC_ENTITY
UvcExtensionFromNode(
    PIRP Irp,
    PKSP_NODE Node,
    PUVC_DEVICE *OutDevice)
{
    PUVC_DEVICE Device = KsFilterGetDevice(KsGetFilterFromIrp(Irp))->Context;

    *OutDevice = Device;
    if (Node->NodeId >= Device->EntityCount ||
        Device->Entities[Node->NodeId].Subtype != UVC_VC_EXTENSION_UNIT)
    {
        return NULL;
    }

    return &Device->Entities[Node->NodeId];
}

static
NTSTATUS
UvcExtensionLength(
    PUVC_DEVICE Device,
    PUVC_ENTITY Entity,
    UCHAR Selector,
    PULONG Length)
{
    USHORT Value = 0;
    NTSTATUS Status;

    Status = UvcRawRequest(Device, Entity, UVC_GET_LEN, Selector, &Value, sizeof(Value));
    if (NT_SUCCESS(Status))
        *Length = Value;
    return Status;
}

static
NTSTATUS
UvcExtensionTransfer(
    PIRP Irp,
    PKSIDENTIFIER Request,
    PVOID Data,
    BOOLEAN Get)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    ULONG Output = Stack->Parameters.DeviceIoControl.OutputBufferLength;
    PUVC_DEVICE Device;
    PUVC_ENTITY Entity;
    ULONG Length = 0;
    NTSTATUS Status;

    Entity = UvcExtensionFromNode(Irp, (PKSP_NODE)Request, &Device);
    if (!Entity)
        return STATUS_NOT_FOUND;

    if (Request->Id == 0 || Request->Id > 0xFF)
        return STATUS_NOT_FOUND;

    Status = UvcExtensionLength(Device, Entity, (UCHAR)Request->Id, &Length);
    if (!NT_SUCCESS(Status))
        return Status;

    if (Output == 0)
    {
        Irp->IoStatus.Information = Length;
        return STATUS_BUFFER_OVERFLOW;
    }

    if (Output < Length)
        return STATUS_BUFFER_TOO_SMALL;

    Status = UvcRawRequest(Device, Entity, Get ? UVC_GET_CUR : UVC_SET_CUR, (UCHAR)Request->Id, Data, Length);
    if (NT_SUCCESS(Status) && Get)
        Irp->IoStatus.Information = Length;
    return Status;
}

static
NTSTATUS
NTAPI
UvcExtensionGet(
    PIRP Irp,
    PKSIDENTIFIER Request,
    PVOID Data)
{
    return UvcExtensionTransfer(Irp, Request, Data, TRUE);
}

static
NTSTATUS
NTAPI
UvcExtensionSet(
    PIRP Irp,
    PKSIDENTIFIER Request,
    PVOID Data)
{
    return UvcExtensionTransfer(Irp, Request, Data, FALSE);
}

static
VOID
UvcFillItem(
    PKSPROPERTY_ITEM Item,
    ULONG Id,
    PFNKSHANDLER Get,
    PFNKSHANDLER Set,
    PFNKSHANDLER Support)
{
    Item->PropertyId = Id;
    Item->GetPropertyHandler = Get;
    Item->SetPropertyHandler = Set;
    Item->SupportHandler = Support;
    Item->MinProperty = sizeof(KSPROPERTY);
    Item->MinData = 0;
}

static
NTSTATUS
UvcBuildSet(
    PUVC_DEVICE Device,
    const GUID *Set,
    const UVC_CONTROL_MAP *Table,
    ULONG Count,
    PUVC_ENTITY Entity,
    BOOLEAN Camera,
    BOOLEAN Node,
    PKSPROPERTY_SET Output,
    PBOOLEAN Added)
{
    PKSPROPERTY_ITEM Items;
    ULONG Index, Used = 0;

    *Added = FALSE;
    if (!Entity)
        return STATUS_SUCCESS;

    Items = UvcAllocate(Device, (Count + 2) * sizeof(*Items));
    if (!Items)
        return STATUS_INSUFFICIENT_RESOURCES;

    for (Index = 0; Index < Count; Index++)
    {
        if (UvcEntityHasControl(Entity, Table[Index].Bit))
        {
            BOOLEAN Component = !Camera && Table[Index].Id == KSPROPERTY_VIDEOPROCAMP_WHITEBALANCE_COMPONENT;

            UvcFillItem(&Items[Used],
                        Table[Index].Id,
                        Component ? UvcWhiteBalanceGet : UvcControlGet,
                        Component ? UvcWhiteBalanceSet : UvcControlSet,
                        UvcControlSupport);
            if (Node)
                Items[Used].MinProperty = sizeof(KSP_NODE);
            Used++;
        }
    }

    if (Camera)
    {
        if (UvcEntityHasControl(Entity, 11))
        {
            UvcFillItem(&Items[Used], KSPROPERTY_CAMERACONTROL_PANTILT, UvcPanTiltGet, UvcPanTiltSet, NULL);
            Items[Used].MinData = Node ? sizeof(KSPROPERTY_CAMERACONTROL_NODE_S2) : sizeof(KSPROPERTY_CAMERACONTROL_S2);
            if (Node)
                Items[Used].MinProperty = sizeof(KSP_NODE);
            Used++;
        }
        UvcFillItem(&Items[Used], KSPROPERTY_CAMERACONTROL_FOCAL_LENGTH, UvcFocalLengthGet, NULL, NULL);
        Items[Used].MinData = Node ? sizeof(KSPROPERTY_CAMERACONTROL_NODE_S2) : sizeof(KSPROPERTY_CAMERACONTROL_FOCAL_LENGTH_S);
        if (Node)
            Items[Used].MinProperty = sizeof(KSP_NODE);
        Used++;
    }

    Output->Set = Set;
    Output->PropertiesCount = Used;
    Output->PropertyItem = Items;
    *Added = TRUE;
    return STATUS_SUCCESS;
}

static
NTSTATUS
UvcBuildExtensionTable(
    PUVC_DEVICE Device,
    PUVC_ENTITY Entity,
    PKSNODE_DESCRIPTOR Node)
{
    PKSAUTOMATION_TABLE Table;
    PKSPROPERTY_SET Set;
    PKSPROPERTY_ITEM Items;
    ULONG Bit, Used = 0;

    Table = UvcAllocate(Device, sizeof(*Table));
    Set = UvcAllocate(Device, sizeof(*Set));
    Items = UvcAllocate(Device, (Entity->ControlSize * 8 + 1) * sizeof(*Items));
    if (!Table || !Set || !Items)
        return STATUS_INSUFFICIENT_RESOURCES;

    for (Bit = 0; Bit < (ULONG)Entity->ControlSize * 8; Bit++)
    {
        if (UvcEntityHasControl(Entity, Bit))
        {
            UvcFillItem(&Items[Used], Bit + 1, UvcExtensionGet, UvcExtensionSet, NULL);
            Items[Used++].MinProperty = sizeof(KSP_NODE);
        }
    }

    Set->Set = &Entity->ExtensionCode;
    Set->PropertiesCount = Used;
    Set->PropertyItem = Items;
    Table->PropertySetsCount = 1;
    Table->PropertyItemSize = sizeof(KSPROPERTY_ITEM);
    Table->PropertySets = Set;
    Node->AutomationTable = Table;
    return STATUS_SUCCESS;
}

static
NTSTATUS
UvcBuildNodeTable(
    PUVC_DEVICE Device,
    const GUID *Set,
    const UVC_CONTROL_MAP *Map,
    ULONG Count,
    PUVC_ENTITY Entity,
    BOOLEAN Camera,
    PKSNODE_DESCRIPTOR Node)
{
    PKSAUTOMATION_TABLE Table = UvcAllocate(Device, sizeof(*Table));
    PKSPROPERTY_SET PropertySet = UvcAllocate(Device, sizeof(*PropertySet));
    BOOLEAN Added;
    NTSTATUS Status;

    if (!Table || !PropertySet)
        return STATUS_INSUFFICIENT_RESOURCES;

    Status = UvcBuildSet(Device, Set, Map, Count, Entity, Camera, TRUE, PropertySet, &Added);
    if (!NT_SUCCESS(Status) || !Added)
        return Status;

    Table->PropertySetsCount = 1;
    Table->PropertyItemSize = sizeof(KSPROPERTY_ITEM);
    Table->PropertySets = PropertySet;
    Node->AutomationTable = Table;
    return STATUS_SUCCESS;
}

static
PUVC_FRAME
UvcFrameFromRange(
    PUVC_DEVICE Device,
    ULONG RangeIndex)
{
    ULONG Frames = RangeIndex / 2, Index;

    for (Index = 0; Index < Device->FormatCount; Index++)
    {
        if (Frames < Device->Formats[Index].FrameCount)
            return &Device->Formats[Index].Frames[Frames];
        Frames -= Device->Formats[Index].FrameCount;
    }

    return NULL;
}

static
NTSTATUS
NTAPI
UvcVideoControlGet(
    PIRP Irp,
    PKSIDENTIFIER Request,
    PVOID Data)
{
    PKSFILTER Filter = KsGetFilterFromIrp(Irp);
    PKSPROPERTY_VIDEOCONTROL_MODE_S Input = (PKSPROPERTY_VIDEOCONTROL_MODE_S)Request;
    PKSPROPERTY_VIDEOCONTROL_MODE_S Reply = Data;

    if (Input->StreamIndex >= Filter->Descriptor->PinDescriptorsCount)
        return STATUS_INVALID_PARAMETER;

    Reply->StreamIndex = Input->StreamIndex;
    Reply->Mode = Input->StreamIndex == UVC_PIN_STILL ? (KS_VideoControlFlag_ExternalTriggerEnable | KS_VideoControlFlag_Trigger) : 0;
    Irp->IoStatus.Information = sizeof(*Reply);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
UvcVideoControlSetMode(
    PIRP Irp,
    PKSIDENTIFIER Request,
    PVOID Data)
{
    PKSFILTER Filter = KsGetFilterFromIrp(Irp);
    PKSPROPERTY_VIDEOCONTROL_MODE_S Input = Data;

    UNREFERENCED_PARAMETER(Request);

    if (Input->StreamIndex >= Filter->Descriptor->PinDescriptorsCount)
        return STATUS_INVALID_PARAMETER;

    if (Input->StreamIndex == UVC_PIN_STILL && (Input->Mode & KS_VideoControlFlag_Trigger))
        return UvcStillTrigger(KsFilterGetDevice(Filter)->Context);

    return STATUS_SUCCESS;
}

static
NTSTATUS
UvcVideoControlFrame(
    PIRP Irp,
    PKSPROPERTY_VIDEOCONTROL_FRAME_RATES_S Input,
    PUVC_FRAME *OutFrame)
{
    PUVC_DEVICE Device = KsFilterGetDevice(KsGetFilterFromIrp(Irp))->Context;

    if (Input->StreamIndex != UVC_PIN_CAPTURE)
        return STATUS_INVALID_PARAMETER;

    *OutFrame = UvcFrameFromRange(Device, Input->RangeIndex);
    return *OutFrame ? STATUS_SUCCESS : STATUS_INVALID_PARAMETER;
}

static
NTSTATUS
NTAPI
UvcActualFrameRateGet(
    PIRP Irp,
    PKSIDENTIFIER Request,
    PVOID Data)
{
    PKSPROPERTY_VIDEOCONTROL_ACTUAL_FRAME_RATE_S Reply = Data;
    PUVC_FRAME Frame;
    NTSTATUS Status;

    Status = UvcVideoControlFrame(Irp, (PKSPROPERTY_VIDEOCONTROL_FRAME_RATES_S)Request, &Frame);
    if (!NT_SUCCESS(Status))
        return Status;

    Reply->CurrentActualFrameRate = 0;
    Reply->CurrentMaxAvailableFrameRate = Frame->MaxInterval;
    Irp->IoStatus.Information = sizeof(*Reply);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
UvcFrameRatesGet(
    PIRP Irp,
    PKSIDENTIFIER Request,
    PVOID Data)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    ULONG Output = Stack->Parameters.DeviceIoControl.OutputBufferLength;
    PKSMULTIPLE_ITEM Items = Data;
    PLONGLONG Rates;
    PUVC_FRAME Frame;
    ULONG Count, Size, Index;
    NTSTATUS Status;

    Status = UvcVideoControlFrame(Irp, (PKSPROPERTY_VIDEOCONTROL_FRAME_RATES_S)Request, &Frame);
    if (!NT_SUCCESS(Status))
        return Status;

    if (Frame->IntervalCount)
        Count = Frame->IntervalCount;
    else if (Frame->IntervalStep)
        Count = (Frame->MaxInterval - Frame->MinInterval) / Frame->IntervalStep + 1;
    else
        Count = 1;
    if (Count > (MAXULONG - sizeof(KSMULTIPLE_ITEM)) / sizeof(LONGLONG))
        return STATUS_INTEGER_OVERFLOW;
    Size = sizeof(KSMULTIPLE_ITEM) + Count * sizeof(LONGLONG);
    Irp->IoStatus.Information = Size;
    if (Output == 0)
        return STATUS_BUFFER_OVERFLOW;
    if (Output < Size)
        return STATUS_BUFFER_TOO_SMALL;

    Items->Size = Size;
    Items->Count = Count;
    Rates = (PLONGLONG)(Items + 1);
    for (Index = 0; Index < Count; Index++)
    {
        if (Frame->IntervalCount)
            Rates[Index] = Frame->Intervals[Index];
        else
            Rates[Index] = (LONGLONG)Frame->MinInterval + (LONGLONG)Index * Frame->IntervalStep;
    }
    return STATUS_SUCCESS;
}

static
NTSTATUS
UvcBuildVideoControlSet(
    PUVC_DEVICE Device,
    PKSPROPERTY_SET Output)
{
    PKSPROPERTY_ITEM Items = UvcAllocate(Device, 4 * sizeof(*Items));

    if (!Items)
        return STATUS_INSUFFICIENT_RESOURCES;

    UvcFillItem(&Items[0], KSPROPERTY_VIDEOCONTROL_CAPS, UvcVideoControlGet, NULL, NULL);
    Items[0].MinProperty = sizeof(KSPROPERTY_VIDEOCONTROL_CAPS_S);
    Items[0].MinData = sizeof(KSPROPERTY_VIDEOCONTROL_CAPS_S);
    UvcFillItem(&Items[1], KSPROPERTY_VIDEOCONTROL_ACTUAL_FRAME_RATE, UvcActualFrameRateGet, NULL, NULL);
    Items[1].MinProperty = sizeof(KSPROPERTY_VIDEOCONTROL_ACTUAL_FRAME_RATE_S);
    Items[1].MinData = sizeof(KSPROPERTY_VIDEOCONTROL_ACTUAL_FRAME_RATE_S);
    UvcFillItem(&Items[2], KSPROPERTY_VIDEOCONTROL_FRAME_RATES, UvcFrameRatesGet, NULL, NULL);
    Items[2].MinProperty = sizeof(KSPROPERTY_VIDEOCONTROL_FRAME_RATES_S);
    UvcFillItem(&Items[3], KSPROPERTY_VIDEOCONTROL_MODE, UvcVideoControlGet, UvcVideoControlSetMode, NULL);
    Items[3].MinProperty = sizeof(KSPROPERTY_VIDEOCONTROL_MODE_S);
    Items[3].MinData = sizeof(KSPROPERTY_VIDEOCONTROL_MODE_S);

    Output->Set = &UVC_PROPSETID_VIDCAP_VIDEOCONTROL;
    Output->PropertiesCount = 4;
    Output->PropertyItem = Items;
    return STATUS_SUCCESS;
}

static
VOID
UvcFillDroppedFrames(
    PIRP Irp,
    PUVC_PIN Context,
    PKSPROPERTY_DROPPEDFRAMES_CURRENT_S Reply)
{
    Reply->PictureNumber = 0;
    Reply->DropCount = 0;
    Reply->AverageFrameSize = Context->ImageSize;
    Irp->IoStatus.Information = sizeof(*Reply);
}

static
NTSTATUS
NTAPI
UvcPinDroppedFramesGet(
    PIRP Irp,
    PKSIDENTIFIER Request,
    PVOID Data)
{
    PKSPIN Pin = KsGetPinFromIrp(Irp);

    UNREFERENCED_PARAMETER(Request);

    if (!Pin || !Pin->Context)
        return STATUS_INVALID_PARAMETER;

    UvcFillDroppedFrames(Irp, Pin->Context, Data);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
UvcFilterDroppedFramesGet(
    PIRP Irp,
    PKSIDENTIFIER Request,
    PVOID Data)
{
    PKSFILTER Filter = KsGetFilterFromIrp(Irp);
    NTSTATUS Status = STATUS_INVALID_PARAMETER;
    PKSPIN Pin;

    UNREFERENCED_PARAMETER(Request);

    KsFilterAcquireControl(Filter);
    Pin = KsFilterGetFirstChildPin(Filter, UVC_PIN_CAPTURE);
    if (Pin && Pin->Context)
    {
        UvcFillDroppedFrames(Irp, Pin->Context, Data);
        Status = STATUS_SUCCESS;
    }
    KsFilterReleaseControl(Filter);
    return Status;
}

static
NTSTATUS
NTAPI
UvcHeaderSizeGet(
    PIRP Irp,
    PKSIDENTIFIER Request,
    PVOID Data)
{
    UNREFERENCED_PARAMETER(Request);

    *(PULONG)Data = sizeof(KS_FRAME_INFO);
    Irp->IoStatus.Information = sizeof(ULONG);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
UvcSecureModeRequest(
    PIRP Irp,
    PKSIDENTIFIER Request,
    PVOID Data)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    ULONG Output = Stack->Parameters.DeviceIoControl.OutputBufferLength;
    ULONG Size = sizeof(KSCAMERA_EXTENDEDPROP_HEADER) + sizeof(KSCAMERA_EXTENDEDPROP_VALUE);

    UNREFERENCED_PARAMETER(Data);

    if (!Output && (Request->Flags & KSPROPERTY_TYPE_GET))
    {
        Irp->IoStatus.Information = Size;
        return STATUS_BUFFER_OVERFLOW;
    }
    if (Output < Size)
        return STATUS_BUFFER_TOO_SMALL;

    /* TODO: secure mode needs a camera that exposes the Microsoft UVC secure-mode extension control */
    return STATUS_INVALID_PARAMETER;
}

static
NTSTATUS
UvcBuildDroppedFramesSet(
    PUVC_DEVICE Device,
    PFNKSHANDLER Handler,
    PKSPROPERTY_SET Output)
{
    PKSPROPERTY_ITEM Item = UvcAllocate(Device, sizeof(*Item));

    if (!Item)
        return STATUS_INSUFFICIENT_RESOURCES;

    UvcFillItem(Item, KSPROPERTY_DROPPEDFRAMES_CURRENT, Handler, NULL, NULL);
    Item->MinProperty = sizeof(KSPROPERTY);
    Item->MinData = sizeof(KSPROPERTY_DROPPEDFRAMES_CURRENT_S);
    Output->Set = &UVC_PROPSETID_VIDCAP_DROPPEDFRAMES;
    Output->PropertiesCount = 1;
    Output->PropertyItem = Item;
    return STATUS_SUCCESS;
}

static
NTSTATUS
UvcBuildSecureModeSet(
    PUVC_DEVICE Device,
    PKSPROPERTY_SET Output)
{
    PKSPROPERTY_ITEM Item = UvcAllocate(Device, sizeof(*Item));

    if (!Item)
        return STATUS_INSUFFICIENT_RESOURCES;

    UvcFillItem(Item, KSPROPERTY_CAMERACONTROL_EXTENDED_SECURE_MODE, UvcSecureModeRequest, UvcSecureModeRequest, NULL);
    Output->Set = &UVC_KSPROPERTYSETID_ExtendedCameraControl;
    Output->PropertiesCount = 1;
    Output->PropertyItem = Item;
    return STATUS_SUCCESS;
}

const KSAUTOMATION_TABLE *
UvcPinAutomationTable(
    PUVC_DEVICE Device)
{
    PKSAUTOMATION_TABLE Table = UvcAllocate(Device, sizeof(*Table));
    PKSPROPERTY_SET Sets = UvcAllocate(Device, 3 * sizeof(*Sets));
    PKSPROPERTY_ITEM HeaderSize = UvcAllocate(Device, sizeof(*HeaderSize));

    if (!Table || !Sets || !HeaderSize ||
        !NT_SUCCESS(UvcBuildVideoControlSet(Device, &Sets[0])) ||
        !NT_SUCCESS(UvcBuildDroppedFramesSet(Device, UvcPinDroppedFramesGet, &Sets[1])))
    {
        return NULL;
    }

    UvcFillItem(HeaderSize, KSPROPERTY_STREAMINTERFACE_HEADERSIZE, UvcHeaderSizeGet, NULL, NULL);
    HeaderSize->MinProperty = sizeof(KSPROPERTY);
    HeaderSize->MinData = sizeof(ULONG);
    Sets[2].Set = &UVC_KSPROPSETID_StreamInterface;
    Sets[2].PropertiesCount = 1;
    Sets[2].PropertyItem = HeaderSize;

    Table->PropertySetsCount = 3;
    Table->PropertyItemSize = sizeof(KSPROPERTY_ITEM);
    Table->PropertySets = Sets;
    return Table;
}

NTSTATUS
UvcBuildControlTables(
    PUVC_DEVICE Device,
    PKSFILTER_DESCRIPTOR Descriptor,
    PKSNODE_DESCRIPTOR Nodes)
{
    static const ULONG StiEvents[] = { KSEVENT_VIDCAPTOSTI_EXT_TRIGGER, KSEVENT_VIDCAP_AUTO_UPDATE, KSEVENT_VIDCAP_SEARCH };
    static const ULONG DeviceEvents[] = { KSEVENT_DEVICE_LOST, KSEVENT_DEVICE_PREEMPTED, KSEVENT_DEVICE_THERMAL_HIGH, KSEVENT_DEVICE_THERMAL_LOW };
    PKSAUTOMATION_TABLE Table;
    PKSPROPERTY_SET Sets;
    PKSEVENT_SET Events;
    PKSEVENT_ITEM EventItems;
    BOOLEAN Added;
    ULONG Used = 0, Index;
    NTSTATUS Status;

    Table = UvcAllocate(Device, sizeof(*Table));
    Sets = UvcAllocate(Device, 5 * sizeof(*Sets));
    Events = UvcAllocate(Device, 2 * sizeof(*Events));
    EventItems = UvcAllocate(Device, (RTL_NUMBER_OF(StiEvents) + RTL_NUMBER_OF(DeviceEvents)) * sizeof(*EventItems));
    if (!Table || !Sets || !Events || !EventItems)
        return STATUS_INSUFFICIENT_RESOURCES;

    Status = UvcBuildSet(Device,
                         &UVC_PROPSETID_VIDCAP_VIDEOPROCAMP,
                         UvcProcAmpMap,
                         RTL_NUMBER_OF(UvcProcAmpMap),
                         UvcFindEntity(Device, UVC_VC_PROCESSING_UNIT, 0),
                         FALSE,
                         FALSE,
                         &Sets[Used],
                         &Added);
    if (!NT_SUCCESS(Status))
        return Status;
    if (Added)
        Used++;

    Status = UvcBuildSet(Device,
                         &UVC_PROPSETID_VIDCAP_CAMERACONTROL,
                         UvcCameraMap,
                         RTL_NUMBER_OF(UvcCameraMap),
                         UvcFindEntity(Device, UVC_VC_INPUT_TERMINAL, UVC_ITT_CAMERA),
                         TRUE,
                         FALSE,
                         &Sets[Used],
                         &Added);
    if (!NT_SUCCESS(Status))
        return Status;
    if (Added)
        Used++;

    Status = UvcBuildVideoControlSet(Device, &Sets[Used]);
    if (!NT_SUCCESS(Status))
        return Status;
    Used++;

    Status = UvcBuildDroppedFramesSet(Device, UvcFilterDroppedFramesGet, &Sets[Used]);
    if (!NT_SUCCESS(Status))
        return Status;
    Used++;

    Status = UvcBuildSecureModeSet(Device, &Sets[Used]);
    if (!NT_SUCCESS(Status))
        return Status;
    Used++;

    for (Index = 0; Index < RTL_NUMBER_OF(StiEvents) + RTL_NUMBER_OF(DeviceEvents); Index++)
    {
        EventItems[Index].EventId = Index < RTL_NUMBER_OF(StiEvents) ? StiEvents[Index] : DeviceEvents[Index - RTL_NUMBER_OF(StiEvents)];
        EventItems[Index].DataInput = sizeof(KSEVENTDATA);
    }
    Events[0].Set = &UVC_KSEVENTSETID_VIDCAPTOSTI;
    Events[0].EventsCount = RTL_NUMBER_OF(StiEvents);
    Events[0].EventItem = EventItems;
    Events[1].Set = &UVC_KSEVENTSETID_Device;
    Events[1].EventsCount = RTL_NUMBER_OF(DeviceEvents);
    Events[1].EventItem = EventItems + RTL_NUMBER_OF(StiEvents);

    Table->PropertySetsCount = Used;
    Table->PropertyItemSize = sizeof(KSPROPERTY_ITEM);
    Table->PropertySets = Sets;
    Table->EventSetsCount = 2;
    Table->EventItemSize = sizeof(KSEVENT_ITEM);
    Table->EventSets = Events;
    Descriptor->AutomationTable = Table;

    for (Index = 0; Index < Device->EntityCount; Index++)
    {
        PUVC_ENTITY Entity = &Device->Entities[Index];

        if (Entity->Subtype == UVC_VC_EXTENSION_UNIT)
        {
            Status = UvcBuildExtensionTable(Device, Entity, &Nodes[Index]);
        }
        else if (Entity->Subtype == UVC_VC_PROCESSING_UNIT)
        {
            Status = UvcBuildNodeTable(Device, &UVC_PROPSETID_VIDCAP_VIDEOPROCAMP, UvcProcAmpMap, RTL_NUMBER_OF(UvcProcAmpMap), Entity, FALSE, &Nodes[Index]);
        }
        else if (Entity->Subtype == UVC_VC_INPUT_TERMINAL && Entity->TerminalType == UVC_ITT_CAMERA)
        {
            Status = UvcBuildNodeTable(Device, &UVC_PROPSETID_VIDCAP_CAMERACONTROL, UvcCameraMap, RTL_NUMBER_OF(UvcCameraMap), Entity, TRUE, &Nodes[Index]);
        }
        if (!NT_SUCCESS(Status))
            return Status;
    }

    return STATUS_SUCCESS;
}
