/*
 * PROJECT:     LiberNT Native WiFi
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     WDI SAE authentication message encoding shared by nwifi and soft-MAC miniports
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <dot11wificxintf.h>
#include <dot11wificxtypes.hpp>

#define NWIFI_SAE_GROUP_P256            19
#define NWIFI_SAE_SCALAR_LENGTH         32
#define NWIFI_SAE_ELEMENT_LENGTH        64
#define NWIFI_SAE_CONFIRM_LENGTH        32
#define NWIFI_SAE_MAX_TOKEN             256
#define NWIFI_SAE_MAX_FRAME             512
#define NWIFI_SAE_MAX_MESSAGE           768
#define NWIFI_SAE_TLV_HEADER            4

typedef struct _NWIFI_SAE_PARAMS
{
    UCHAR Bssid[6];
    ULONG RequestType;
    BOOLEAN HaveCommit;
    USHORT Group;
    UCHAR Scalar[NWIFI_SAE_SCALAR_LENGTH];
    UCHAR Element[NWIFI_SAE_ELEMENT_LENGTH];
    ULONG TokenLength;
    UCHAR Token[NWIFI_SAE_MAX_TOKEN];
    BOOLEAN HaveConfirm;
    USHORT SendConfirm;
    UCHAR Confirm[NWIFI_SAE_CONFIRM_LENGTH];
    BOOLEAN HaveStatus;
    ULONG Status;
} NWIFI_SAE_PARAMS, *PNWIFI_SAE_PARAMS;

static __inline
BOOLEAN
NwifiSaePutTlv(
    _Inout_updates_bytes_(Capacity) PUCHAR Buffer,
    _In_ ULONG Capacity,
    _Inout_ PULONG Offset,
    _In_ USHORT Type,
    _In_reads_bytes_opt_(Length) const VOID *Data,
    _In_ ULONG Length)
{
    if (Length > 0xFFFF || *Offset + NWIFI_SAE_TLV_HEADER + Length > Capacity)
        return FALSE;

    Buffer[*Offset] = (UCHAR)Type;
    Buffer[*Offset + 1] = (UCHAR)(Type >> 8);
    Buffer[*Offset + 2] = (UCHAR)Length;
    Buffer[*Offset + 3] = (UCHAR)(Length >> 8);
    if (Length)
        RtlCopyMemory(Buffer + *Offset + NWIFI_SAE_TLV_HEADER, Data, Length);
    *Offset += NWIFI_SAE_TLV_HEADER + Length;
    return TRUE;
}

static __inline
BOOLEAN
NwifiSaeFindTlv(
    _In_reads_bytes_(Length) const UCHAR *Buffer,
    _In_ ULONG Length,
    _In_ USHORT Type,
    _Out_ const UCHAR **Value,
    _Out_ PULONG ValueLength)
{
    ULONG Offset = 0, Size;
    USHORT Id;

    *Value = NULL;
    *ValueLength = 0;
    while (Offset + NWIFI_SAE_TLV_HEADER <= Length)
    {
        Id = (USHORT)(Buffer[Offset] | (Buffer[Offset + 1] << 8));
        Size = Buffer[Offset + 2] | ((ULONG)Buffer[Offset + 3] << 8);
        if (Offset + NWIFI_SAE_TLV_HEADER + Size > Length)
            return FALSE;
        if (Id == Type)
        {
            *Value = Buffer + Offset + NWIFI_SAE_TLV_HEADER;
            *ValueLength = Size;
            return TRUE;
        }
        Offset += NWIFI_SAE_TLV_HEADER + Size;
    }
    return FALSE;
}

static __inline
ULONG
NwifiSaeReadUlong(
    _In_reads_bytes_(4) const UCHAR *Data)
{
    return Data[0] | ((ULONG)Data[1] << 8) | ((ULONG)Data[2] << 16) | ((ULONG)Data[3] << 24);
}

static __inline
BOOLEAN
NwifiSaePutUlong(
    _Inout_updates_bytes_(Capacity) PUCHAR Buffer,
    _In_ ULONG Capacity,
    _Inout_ PULONG Offset,
    _In_ USHORT Type,
    _In_ ULONG Value)
{
    UCHAR Data[4];

    Data[0] = (UCHAR)Value;
    Data[1] = (UCHAR)(Value >> 8);
    Data[2] = (UCHAR)(Value >> 16);
    Data[3] = (UCHAR)(Value >> 24);
    return NwifiSaePutTlv(Buffer, Capacity, Offset, Type, Data, sizeof(Data));
}

static __inline
BOOLEAN
NwifiSaePutUshort(
    _Inout_updates_bytes_(Capacity) PUCHAR Buffer,
    _In_ ULONG Capacity,
    _Inout_ PULONG Offset,
    _In_ USHORT Type,
    _In_ USHORT Value)
{
    UCHAR Data[2];

    Data[0] = (UCHAR)Value;
    Data[1] = (UCHAR)(Value >> 8);
    return NwifiSaePutTlv(Buffer, Capacity, Offset, Type, Data, sizeof(Data));
}

static __inline
ULONG
NwifiSaeBuildIndication(
    _Out_writes_bytes_(Capacity) PUCHAR Buffer,
    _In_ ULONG Capacity,
    _In_reads_bytes_(6) const UCHAR *Bssid,
    _In_ ULONG IndicationType,
    _In_reads_bytes_opt_(FrameLength) const UCHAR *Frame,
    _In_ ULONG FrameLength,
    _In_ BOOLEAN HaveStatus,
    _In_ ULONG Status)
{
    ULONG Offset = sizeof(WDI_MESSAGE_HEADER);
    USHORT FrameTlv;

    if (Capacity < Offset)
        return 0;
    RtlZeroMemory(Buffer, Offset);
    if (!NwifiSaePutTlv(Buffer, Capacity, &Offset, WDI_TLV_BSSID, Bssid, 6) ||
        !NwifiSaePutUlong(Buffer, Capacity, &Offset, WDI_TLV_SAE_INDICATION_TYPE, IndicationType))
    {
        return 0;
    }
    if (Frame && FrameLength)
    {
        FrameTlv = IndicationType == WDI_SAE_INDICATION_TYPE_CONFIRM_FRAME ?
                   WDI_TLV_SAE_CONFIRM_FRAME : WDI_TLV_SAE_COMMIT_FRAME;
        if (!NwifiSaePutTlv(Buffer, Capacity, &Offset, FrameTlv, Frame, FrameLength))
            return 0;
    }
    if (HaveStatus && !NwifiSaePutUlong(Buffer, Capacity, &Offset, WDI_TLV_SAE_STATUS, Status))
        return 0;
    return Offset;
}

static __inline
BOOLEAN
NwifiSaeParseIndication(
    _In_reads_bytes_(Length) const UCHAR *Buffer,
    _In_ ULONG Length,
    _Out_writes_bytes_(6) PUCHAR Bssid,
    _Out_ PULONG IndicationType,
    _Out_ const UCHAR **Frame,
    _Out_ PULONG FrameLength)
{
    const UCHAR *Value;
    ULONG ValueLength;

    *Frame = NULL;
    *FrameLength = 0;
    if (Length < sizeof(WDI_MESSAGE_HEADER))
        return FALSE;
    Buffer += sizeof(WDI_MESSAGE_HEADER);
    Length -= sizeof(WDI_MESSAGE_HEADER);

    if (!NwifiSaeFindTlv(Buffer, Length, WDI_TLV_BSSID, &Value, &ValueLength) || ValueLength != 6)
        return FALSE;
    RtlCopyMemory(Bssid, Value, 6);
    if (!NwifiSaeFindTlv(Buffer, Length, WDI_TLV_SAE_INDICATION_TYPE, &Value, &ValueLength) ||
        ValueLength != sizeof(ULONG))
    {
        return FALSE;
    }
    *IndicationType = NwifiSaeReadUlong(Value);
    if (NwifiSaeFindTlv(Buffer, Length, WDI_TLV_SAE_COMMIT_FRAME, &Value, &ValueLength) ||
        NwifiSaeFindTlv(Buffer, Length, WDI_TLV_SAE_CONFIRM_FRAME, &Value, &ValueLength))
    {
        *Frame = Value;
        *FrameLength = ValueLength;
    }
    return TRUE;
}

static __inline
ULONG
NwifiSaeBuildParams(
    _Out_writes_bytes_(Capacity) PUCHAR Buffer,
    _In_ ULONG Capacity,
    _In_ const NWIFI_SAE_PARAMS *Params)
{
    UCHAR Inner[NWIFI_SAE_TLV_HEADER * 4 + 2 + NWIFI_SAE_SCALAR_LENGTH + NWIFI_SAE_ELEMENT_LENGTH + NWIFI_SAE_MAX_TOKEN];
    ULONG Offset = sizeof(WDI_MESSAGE_HEADER), InnerLength;

    if (Capacity < Offset)
        return 0;
    RtlZeroMemory(Buffer, Offset);
    if (!NwifiSaePutTlv(Buffer, Capacity, &Offset, WDI_TLV_BSSID, Params->Bssid, 6) ||
        !NwifiSaePutUlong(Buffer, Capacity, &Offset, WDI_TLV_SAE_REQUEST_TYPE, Params->RequestType))
    {
        return 0;
    }

    if (Params->HaveCommit)
    {
        InnerLength = 0;
        if (!NwifiSaePutUshort(Inner, sizeof(Inner), &InnerLength, WDI_TLV_SAE_FINITE_CYCLIC_GROUP,
                               Params->Group) ||
            !NwifiSaePutTlv(Inner, sizeof(Inner), &InnerLength, WDI_TLV_SAE_SCALAR,
                            Params->Scalar, NWIFI_SAE_SCALAR_LENGTH) ||
            !NwifiSaePutTlv(Inner, sizeof(Inner), &InnerLength, WDI_TLV_SAE_ELEMENT,
                            Params->Element, NWIFI_SAE_ELEMENT_LENGTH) ||
            (Params->TokenLength &&
             !NwifiSaePutTlv(Inner, sizeof(Inner), &InnerLength, WDI_TLV_SAE_ANTI_CLOGGING_TOKEN,
                             Params->Token, Params->TokenLength)) ||
            !NwifiSaePutTlv(Buffer, Capacity, &Offset, WDI_TLV_SAE_COMMIT_PARAMS, Inner, InnerLength))
        {
            return 0;
        }
    }

    if (Params->HaveConfirm)
    {
        InnerLength = 0;
        if (!NwifiSaePutUshort(Inner, sizeof(Inner), &InnerLength, WDI_TLV_SAE_SEND_CONFIRM,
                               Params->SendConfirm) ||
            !NwifiSaePutTlv(Inner, sizeof(Inner), &InnerLength, WDI_TLV_SAE_CONFIRM,
                            Params->Confirm, NWIFI_SAE_CONFIRM_LENGTH) ||
            !NwifiSaePutTlv(Buffer, Capacity, &Offset, WDI_TLV_SAE_CONFIRM_PARAMS, Inner, InnerLength))
        {
            return 0;
        }
    }

    if (Params->HaveStatus &&
        !NwifiSaePutUlong(Buffer, Capacity, &Offset, WDI_TLV_SAE_STATUS, Params->Status))
    {
        return 0;
    }
    return Offset;
}

static __inline
BOOLEAN
NwifiSaeParseParams(
    _In_reads_bytes_(Length) const UCHAR *Buffer,
    _In_ ULONG Length,
    _Out_ PNWIFI_SAE_PARAMS Params)
{
    const UCHAR *Value, *Inner;
    ULONG ValueLength, InnerLength;

    RtlZeroMemory(Params, sizeof(*Params));
    if (Length < sizeof(WDI_MESSAGE_HEADER))
        return FALSE;
    Buffer += sizeof(WDI_MESSAGE_HEADER);
    Length -= sizeof(WDI_MESSAGE_HEADER);

    if (!NwifiSaeFindTlv(Buffer, Length, WDI_TLV_BSSID, &Value, &ValueLength) || ValueLength != 6)
        return FALSE;
    RtlCopyMemory(Params->Bssid, Value, 6);
    if (!NwifiSaeFindTlv(Buffer, Length, WDI_TLV_SAE_REQUEST_TYPE, &Value, &ValueLength) ||
        ValueLength != sizeof(ULONG))
    {
        return FALSE;
    }
    Params->RequestType = NwifiSaeReadUlong(Value);

    if (NwifiSaeFindTlv(Buffer, Length, WDI_TLV_SAE_COMMIT_PARAMS, &Inner, &InnerLength))
    {
        if (!NwifiSaeFindTlv(Inner, InnerLength, WDI_TLV_SAE_FINITE_CYCLIC_GROUP, &Value, &ValueLength) ||
            ValueLength != sizeof(USHORT))
        {
            return FALSE;
        }
        Params->Group = (USHORT)(Value[0] | (Value[1] << 8));
        if (!NwifiSaeFindTlv(Inner, InnerLength, WDI_TLV_SAE_SCALAR, &Value, &ValueLength) ||
            ValueLength != NWIFI_SAE_SCALAR_LENGTH)
        {
            return FALSE;
        }
        RtlCopyMemory(Params->Scalar, Value, NWIFI_SAE_SCALAR_LENGTH);
        if (!NwifiSaeFindTlv(Inner, InnerLength, WDI_TLV_SAE_ELEMENT, &Value, &ValueLength) ||
            ValueLength != NWIFI_SAE_ELEMENT_LENGTH)
        {
            return FALSE;
        }
        RtlCopyMemory(Params->Element, Value, NWIFI_SAE_ELEMENT_LENGTH);
        if (NwifiSaeFindTlv(Inner, InnerLength, WDI_TLV_SAE_ANTI_CLOGGING_TOKEN, &Value, &ValueLength))
        {
            if (ValueLength > NWIFI_SAE_MAX_TOKEN)
                return FALSE;
            RtlCopyMemory(Params->Token, Value, ValueLength);
            Params->TokenLength = ValueLength;
        }
        Params->HaveCommit = TRUE;
    }

    if (NwifiSaeFindTlv(Buffer, Length, WDI_TLV_SAE_CONFIRM_PARAMS, &Inner, &InnerLength))
    {
        if (!NwifiSaeFindTlv(Inner, InnerLength, WDI_TLV_SAE_SEND_CONFIRM, &Value, &ValueLength) ||
            ValueLength != sizeof(USHORT))
        {
            return FALSE;
        }
        Params->SendConfirm = (USHORT)(Value[0] | (Value[1] << 8));
        if (!NwifiSaeFindTlv(Inner, InnerLength, WDI_TLV_SAE_CONFIRM, &Value, &ValueLength) ||
            ValueLength != NWIFI_SAE_CONFIRM_LENGTH)
        {
            return FALSE;
        }
        RtlCopyMemory(Params->Confirm, Value, NWIFI_SAE_CONFIRM_LENGTH);
        Params->HaveConfirm = TRUE;
    }

    if (NwifiSaeFindTlv(Buffer, Length, WDI_TLV_SAE_STATUS, &Value, &ValueLength) && ValueLength == sizeof(ULONG))
    {
        Params->Status = NwifiSaeReadUlong(Value);
        Params->HaveStatus = TRUE;
    }
    return TRUE;
}
