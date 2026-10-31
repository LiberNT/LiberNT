/*
 * PROJECT:     LiberNT Windows Firewall service
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Parse bounded Windows Firewall policy rule strings
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include "mpssvc_policy.h"

static BOOL
MpsSliceEquals(
    _In_reads_(Length) PCWSTR Text,
    _In_ SIZE_T Length,
    _In_ PCWSTR Expected)
{
    SIZE_T Index;

    for (Index = 0; Index < Length && Expected[Index] != UNICODE_NULL; Index++)
    {
        if (Text[Index] != Expected[Index])
            return FALSE;
    }

    return Index == Length && Expected[Index] == UNICODE_NULL;
}

static DWORD
MpsParseNumber(
    _In_reads_(Length) PCWSTR Text,
    _In_ SIZE_T Length,
    _In_ UINT32 Maximum,
    _Out_ UINT32 *Number)
{
    UINT32 Value = 0;
    SIZE_T Index;

    if (Length == 0)
        return ERROR_INVALID_DATA;

    for (Index = 0; Index < Length; Index++)
    {
        UINT32 Digit;

        if (Text[Index] < L'0' || Text[Index] > L'9')
            return ERROR_INVALID_DATA;

        Digit = Text[Index] - L'0';
        if (Value > (Maximum - Digit) / 10)
            return ERROR_INVALID_DATA;

        Value = Value * 10 + Digit;
    }

    *Number = Value;
    return ERROR_SUCCESS;
}

static DWORD
MpsParsePortNumber(
    _In_reads_(Length) PCWSTR Text,
    _In_ SIZE_T Length,
    _Out_ USHORT *Port)
{
    UINT32 Value;

    if (Length > 5)
        return ERROR_INVALID_DATA;

    DWORD Error = MpsParseNumber(Text, Length, 65535, &Value);

    if (Error == ERROR_SUCCESS)
        *Port = (USHORT)Value;

    return Error;
}

static DWORD
MpsParseIpv4DottedQuad(
    _In_reads_(Length) PCWSTR Text,
    _In_ SIZE_T Length,
    _Out_ UINT32 *Address)
{
    UINT32 Octets[4];
    SIZE_T Start = 0, End, Index;
    DWORD Error;

    *Address = 0;
    for (Index = 0; Index < 4; Index++)
    {
        End = Start;
        while (End < Length && Text[End] != L'.')
            End++;

        if (End == Start || End - Start > 3)
            return ERROR_INVALID_DATA;

        Error = MpsParseNumber(Text + Start, End - Start, 255, &Octets[Index]);
        if (Error != ERROR_SUCCESS)
            return Error;

        if (Index < 3)
        {
            if (End == Length || Text[End] != L'.')
                return ERROR_INVALID_DATA;
            Start = End + 1;
        }
        else
        {
            if (End != Length)
                return ERROR_INVALID_DATA;
        }
    }

    *Address = (Octets[0] << 24) | (Octets[1] << 16) | (Octets[2] << 8) | Octets[3];
    return ERROR_SUCCESS;
}

static DWORD
MpsParseIpv4AddressAndMask(
    _In_reads_(Length) PCWSTR Text,
    _In_ SIZE_T Length,
    _Out_ UINT32 *Address,
    _Out_ UINT32 *Mask,
    _Out_ BOOL *Unsupported)
{
    SIZE_T Slash = 0;
    SIZE_T Index;
    DWORD Error;

    *Unsupported = FALSE;

    if (MpsSliceEquals(Text, Length, L"LocalSubnet") ||
        MpsSliceEquals(Text, Length, L"DNS") ||
        MpsSliceEquals(Text, Length, L"DHCP") ||
        MpsSliceEquals(Text, Length, L"WINS") ||
        MpsSliceEquals(Text, Length, L"DefaultGateway") ||
        MpsSliceEquals(Text, Length, L"Intranet"))
    {
        *Unsupported = TRUE;
        return ERROR_SUCCESS;
    }

    for (Index = 0; Index < Length; Index++)
    {
        if (Text[Index] == L',' || Text[Index] == L'-')
        {
            *Unsupported = TRUE;
            return ERROR_SUCCESS;
        }
    }

    while (Slash < Length && Text[Slash] != L'/')
        Slash++;

    if (Slash == Length)
    {
        Error = MpsParseIpv4DottedQuad(Text, Length, Address);
        if (Error != ERROR_SUCCESS)
            return Error;
        *Mask = 0xFFFFFFFF;
        return ERROR_SUCCESS;
    }

    Error = MpsParseIpv4DottedQuad(Text, Slash, Address);
    if (Error != ERROR_SUCCESS)
        return Error;

    PCWSTR MaskText = Text + Slash + 1;
    SIZE_T MaskLength = Length - Slash - 1;
    UINT32 Prefix = 0;

    if (MaskLength == 0)
        return ERROR_INVALID_DATA;

    if (MpsParseNumber(MaskText, MaskLength, 32, &Prefix) == ERROR_SUCCESS)
    {
        *Mask = Prefix == 0 ? 0 : (0xFFFFFFFFU << (32 - Prefix));
        *Address &= *Mask;
        return ERROR_SUCCESS;
    }

    Error = MpsParseIpv4DottedQuad(MaskText, MaskLength, Mask);
    if (Error == ERROR_SUCCESS)
        *Address &= *Mask;

    return Error;
}

static DWORD
MpsAddAddress(
    _In_reads_(Length) PCWSTR Text,
    _In_ SIZE_T Length,
    _In_ BOOL Local,
    _Inout_ MPS_FIREWALL_RULE *Rule)
{
    MPS_V4_SUBNET *Addresses = Local ? Rule->LocalAddresses : Rule->RemoteAddresses;
    UINT32 *Count = Local ? &Rule->LocalAddressCount : &Rule->RemoteAddressCount;
    UINT32 Address, Mask;
    BOOL Unsupported;
    DWORD Error;

    Error = MpsParseIpv4AddressAndMask(Text, Length, &Address, &Mask, &Unsupported);
    if (Error != ERROR_SUCCESS)
        return Error;

    if (Unsupported)
    {
        Rule->Unsupported = TRUE;
        return ERROR_SUCCESS;
    }

    if (*Count == MPS_MAX_ADDRESSES)
        return ERROR_BUFFER_OVERFLOW;

    Addresses[*Count].Address = Address;
    Addresses[*Count].Mask = Mask;
    (*Count)++;
    return ERROR_SUCCESS;
}

static BOOL
MpsIsPortKeyword(
    _In_reads_(Length) PCWSTR Text,
    _In_ SIZE_T Length,
    _In_ BOOL Local,
    _In_ UINT32 PortVersion)
{
    if (Local)
    {
        if (PortVersion == 0 &&
            (MpsSliceEquals(Text, Length, L"RPC") ||
             MpsSliceEquals(Text, Length, L"RPC-EPMap") ||
             MpsSliceEquals(Text, Length, L"Teredo")))
            return TRUE;

        if (PortVersion == 1 &&
            (MpsSliceEquals(Text, Length, L"IPTLSIn") ||
             MpsSliceEquals(Text, Length, L"IPHTTPSIn")))
            return TRUE;

        if (PortVersion == 2 &&
            (MpsSliceEquals(Text, Length, L"DHCP") ||
             MpsSliceEquals(Text, Length, L"Ply2Disc") ||
             MpsSliceEquals(Text, Length, L"mDNS") ||
             MpsSliceEquals(Text, Length, L"TcpCDPSvc")))
            return TRUE;
    }
    else
    {
        if (PortVersion == 1 &&
            (MpsSliceEquals(Text, Length, L"IPTLSOut") ||
             MpsSliceEquals(Text, Length, L"IPHTTPSOut")))
            return TRUE;

        if (PortVersion == 2 && MpsSliceEquals(Text, Length, L"CortanaOut"))
            return TRUE;
    }

    return FALSE;
}

static DWORD
MpsAddSinglePortRange(
    _In_reads_(Length) PCWSTR Text,
    _In_ SIZE_T Length,
    _In_ BOOL Local,
    _In_ UINT32 PortVersion,
    _Inout_ MPS_FIREWALL_RULE *Rule)
{
    SIZE_T Index;
    USHORT First, Last;
    DWORD Error;
    MPS_PORT_RANGE *Ports;
    UINT32 *Count;

    if (Length == 0)
        return ERROR_INVALID_DATA;

    if (MpsIsPortKeyword(Text, Length, Local, PortVersion))
    {
        Rule->Unsupported = TRUE;
        return ERROR_SUCCESS;
    }

    for (Index = 0; Index < Length; Index++)
    {
        if (Text[Index] == L'-')
            break;
    }

    if (Index == Length)
    {
        Error = MpsParsePortNumber(Text, Length, &First);
        if (Error == ERROR_SUCCESS)
            Last = First;
    }
    else
    {
        Error = MpsParsePortNumber(Text, Index, &First);
        if (Error == ERROR_SUCCESS)
            Error = MpsParsePortNumber(Text + Index + 1, Length - Index - 1, &Last);
        if (Error == ERROR_SUCCESS && First > Last)
            Error = ERROR_INVALID_DATA;
    }

    if (Error != ERROR_SUCCESS)
        return Error;

    if (Local)
    {
        Ports = Rule->LocalPorts;
        Count = &Rule->LocalPortCount;
    }
    else
    {
        Ports = Rule->RemotePorts;
        Count = &Rule->RemotePortCount;
    }

    if (*Count == MPS_MAX_PORTS)
        return ERROR_BUFFER_OVERFLOW;

    Ports[*Count].First = First;
    Ports[*Count].Last = Last;
    (*Count)++;
    return ERROR_SUCCESS;
}

static DWORD
MpsAddPorts(
    _In_reads_(Length) PCWSTR Text,
    _In_ SIZE_T Length,
    _In_ BOOL Local,
    _In_ UINT32 PortVersion,
    _Inout_ MPS_FIREWALL_RULE *Rule)
{
    SIZE_T Start = 0;
    SIZE_T End;
    DWORD Error;

    if (Length == 0 || Text[0] == L',' || Text[Length - 1] == L',')
        return ERROR_INVALID_DATA;

    while (Start < Length)
    {
        End = Start;
        while (End < Length && Text[End] != L',')
            End++;

        if (End == Start)
            return ERROR_INVALID_DATA;

        Error = MpsAddSinglePortRange(Text + Start, End - Start, Local, PortVersion, Rule);
        if (Error != ERROR_SUCCESS)
            return Error;

        if (End < Length && Text[End] == L',')
            Start = End + 1;
        else
            Start = End;
    }

    return ERROR_SUCCESS;
}

static DWORD
MpsCopySlice(
    _Out_writes_(Capacity) PWSTR Destination,
    _In_ SIZE_T Capacity,
    _In_reads_(Length) PCWSTR Source,
    _In_ SIZE_T Length)
{
    if (Length >= Capacity)
        return ERROR_BUFFER_OVERFLOW;

    CopyMemory(Destination, Source, Length * sizeof(WCHAR));
    Destination[Length] = UNICODE_NULL;
    return ERROR_SUCCESS;
}

static DWORD
MpsParseVersion(
    _In_reads_(Length) PCWSTR Text,
    _In_ SIZE_T Length,
    _Out_ SIZE_T *Consumed)
{
    SIZE_T Separator = 0;
    SIZE_T End = 0;
    UINT32 Major, Minor;
    DWORD Error;

    if (Length < 5 || Text[0] != L'v')
        return ERROR_INVALID_DATA;

    while (Separator < Length && Text[Separator] != L'.')
        Separator++;

    if (Separator <= 1 || Separator >= Length || Separator - 1 > 3)
        return ERROR_INVALID_DATA;

    Error = MpsParseNumber(Text + 1, Separator - 1, 255, &Major);
    if (Error != ERROR_SUCCESS)
        return Error;

    End = Separator + 1;
    while (End < Length && Text[End] != L'|')
        End++;

    if (End == Length || End == Separator + 1 || End - Separator - 1 > 3)
        return ERROR_INVALID_DATA;

    Error = MpsParseNumber(Text + Separator + 1, End - Separator - 1, 255, &Minor);
    if (Error != ERROR_SUCCESS || Major != 2)
        return ERROR_INVALID_DATA;
    UNREFERENCED_PARAMETER(Minor);

    *Consumed = End + 1;
    return ERROR_SUCCESS;
}

static DWORD
MpsParseField(
    _In_reads_(FieldLength) PCWSTR Field,
    _In_ SIZE_T FieldLength,
    _Inout_ MPS_FIREWALL_RULE *Rule)
{
    SIZE_T Equals = 0;
    SIZE_T ValueLength;
    PCWSTR Value;
    UINT32 Number;
    DWORD Error;

    while (Equals < FieldLength && Field[Equals] != L'=')
        Equals++;

    if (Equals == 0 || Equals == FieldLength)
        return ERROR_INVALID_DATA;

    Value = Field + Equals + 1;
    ValueLength = FieldLength - Equals - 1;

    if (ValueLength == 0)
    {
        if (MpsSliceEquals(Field, Equals, L"Desc") ||
            MpsSliceEquals(Field, Equals, L"EmbedCtxt"))
            return ERROR_SUCCESS;

        return ERROR_INVALID_DATA;
    }

    if (MpsSliceEquals(Field, Equals, L"Action"))
    {
        if (Rule->HasAction)
            return ERROR_INVALID_DATA;
        Rule->HasAction = TRUE;

        if (MpsSliceEquals(Value, ValueLength, L"Allow"))
            Rule->Action = MpsRuleActionAllow;
        else if (MpsSliceEquals(Value, ValueLength, L"Block"))
            Rule->Action = MpsRuleActionBlock;
        else if (MpsSliceEquals(Value, ValueLength, L"ByPass"))
        {
            Rule->Action = MpsRuleActionBypass;
            Rule->Unsupported = TRUE;
        }
        else
            return ERROR_INVALID_DATA;
        return ERROR_SUCCESS;
    }

    if (MpsSliceEquals(Field, Equals, L"Dir"))
    {
        if (Rule->HasDirection)
            return ERROR_INVALID_DATA;
        Rule->HasDirection = TRUE;
        if (MpsSliceEquals(Value, ValueLength, L"In"))
            Rule->Inbound = TRUE;
        else if (MpsSliceEquals(Value, ValueLength, L"Out"))
            Rule->Inbound = FALSE;
        else
            return ERROR_INVALID_DATA;
        return ERROR_SUCCESS;
    }

    if (MpsSliceEquals(Field, Equals, L"Active"))
    {
        if (Rule->HasActive)
            return ERROR_INVALID_DATA;
        Rule->HasActive = TRUE;
        if (MpsSliceEquals(Value, ValueLength, L"TRUE"))
            Rule->Active = TRUE;
        else if (!MpsSliceEquals(Value, ValueLength, L"FALSE"))
            return ERROR_INVALID_DATA;
        return ERROR_SUCCESS;
    }

    if (MpsSliceEquals(Field, Equals, L"Protocol"))
    {
        if (Rule->HasProtocol || ValueLength > 3)
            return ERROR_INVALID_DATA;
        Error = MpsParseNumber(Value, ValueLength, 256, &Number);
        if (Error != ERROR_SUCCESS)
            return Error;
        Rule->Protocol = Number;
        Rule->HasProtocol = TRUE;
        return ERROR_SUCCESS;
    }

    if (MpsSliceEquals(Field, Equals, L"LPort") ||
        MpsSliceEquals(Field, Equals, L"LPort2_10"))
    {
        if (!Rule->HasProtocol || (Rule->Protocol != 6 && Rule->Protocol != 17))
            return ERROR_INVALID_DATA;
        return MpsAddPorts(Value,
                          ValueLength,
                          TRUE,
                          MpsSliceEquals(Field, Equals, L"LPort2_10") ? 1 : 0,
                          Rule);
    }

    if (MpsSliceEquals(Field, Equals, L"RPort") ||
        MpsSliceEquals(Field, Equals, L"RPort2_10"))
    {
        if (!Rule->HasProtocol || (Rule->Protocol != 6 && Rule->Protocol != 17))
            return ERROR_INVALID_DATA;
        return MpsAddPorts(Value,
                          ValueLength,
                          FALSE,
                          MpsSliceEquals(Field, Equals, L"RPort2_10") ? 1 : 0,
                          Rule);
    }

    if (MpsSliceEquals(Field, Equals, L"LPort2_20"))
    {
        if (!Rule->HasProtocol || (Rule->Protocol != 6 && Rule->Protocol != 17))
            return ERROR_INVALID_DATA;
        if (!MpsIsPortKeyword(Value, ValueLength, TRUE, 2))
            return ERROR_INVALID_DATA;
        Rule->Unsupported = TRUE;
        return ERROR_SUCCESS;
    }

    if (MpsSliceEquals(Field, Equals, L"Profile"))
    {
        Rule->HasProfile = TRUE;
        if (MpsSliceEquals(Value, ValueLength, L"Domain"))
            Rule->ProfileMask |= 1;
        else if (MpsSliceEquals(Value, ValueLength, L"Private"))
            Rule->ProfileMask |= 2;
        else if (MpsSliceEquals(Value, ValueLength, L"Public"))
            Rule->ProfileMask |= 4;
        else
            return ERROR_INVALID_DATA;
        return ERROR_SUCCESS;
    }

    if (MpsSliceEquals(Field, Equals, L"LA4"))
        return MpsAddAddress(Value, ValueLength, TRUE, Rule);

    if (MpsSliceEquals(Field, Equals, L"RA4"))
        return MpsAddAddress(Value, ValueLength, FALSE, Rule);

    if (MpsSliceEquals(Field, Equals, L"App"))
    {
        if (Rule->HasApplication)
            return ERROR_INVALID_DATA;
        Error = MpsCopySlice(Rule->Application,
                             ARRAYSIZE(Rule->Application),
                             Value,
                             ValueLength);
        if (Error == ERROR_SUCCESS)
            Rule->HasApplication = TRUE;
        return Error;
    }

    if (MpsSliceEquals(Field, Equals, L"Svc"))
    {
        if (Rule->HasService)
            return ERROR_INVALID_DATA;
        Rule->HasService = TRUE;
        if (!MpsSliceEquals(Value, ValueLength, L"*"))
            Rule->Unsupported = TRUE;
        return ERROR_SUCCESS;
    }

    if (MpsSliceEquals(Field, Equals, L"Name") ||
        MpsSliceEquals(Field, Equals, L"Desc") ||
        MpsSliceEquals(Field, Equals, L"EmbedCtxt"))
        return ERROR_SUCCESS;

    Rule->Unsupported = TRUE;
    return ERROR_SUCCESS;
}

DWORD
MpsParseFirewallRule(
    _In_reads_(TextLength) PCWSTR Text,
    _In_ SIZE_T TextLength,
    _Out_ MPS_FIREWALL_RULE *Rule)
{
    SIZE_T Position, End;
    DWORD Error;

    if (Text == NULL || Rule == NULL)
        return ERROR_INVALID_PARAMETER;
    if (TextLength > MPS_MAX_RULE_CHARS)
        return ERROR_BUFFER_OVERFLOW;

    ZeroMemory(Rule, sizeof(*Rule));
    Rule->Protocol = 256;

    Error = MpsParseVersion(Text, TextLength, &Position);
    if (Error != ERROR_SUCCESS)
        return Error;

    while (Position < TextLength)
    {
        End = Position;
        while (End < TextLength && Text[End] != L'|')
            End++;

        if (End == TextLength || End == Position)
            return ERROR_INVALID_DATA;

        Error = MpsParseField(Text + Position, End - Position, Rule);
        if (Error != ERROR_SUCCESS)
            return Error;

        Position = End + 1;
    }

    if (!Rule->HasAction || !Rule->HasDirection)
        return ERROR_INVALID_DATA;

    if (!Rule->HasProfile)
        Rule->ProfileMask = 7;

    if (Rule->ProfileMask != 7)
        Rule->Unsupported = TRUE;

    return ERROR_SUCCESS;
}
