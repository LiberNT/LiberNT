/*
 * PROJECT:     LiberNT Network Location Awareness Service
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Internet probe request and response handling
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "probe.h"

#include <string.h>

#define NLA_HTTP_FORBIDDEN 403
#define NLA_HTTP_OK 200

static int
NlaLower(int Character)
{
    return (Character >= 'A' && Character <= 'Z') ? Character - 'A' + 'a' : Character;
}

static int
NlaStartsWithNoCase(const char *Text, size_t Length, const char *Prefix)
{
    size_t Index;

    for (Index = 0; Prefix[Index] != 0; Index++)
    {
        if (Index >= Length || NlaLower((unsigned char)Text[Index]) != NlaLower((unsigned char)Prefix[Index]))
            return 0;
    }
    return 1;
}

static int
NlaIsSpace(char Character)
{
    return Character == ' ' || Character == '\t' || Character == '\r' || Character == '\n';
}

static int
NlaHexValue(char Character)
{
    if (Character >= '0' && Character <= '9')
        return Character - '0';
    Character = (char)NlaLower((unsigned char)Character);
    if (Character >= 'a' && Character <= 'f')
        return Character - 'a' + 10;
    return -1;
}

static size_t
NlaAppend(char *Buffer, size_t BufferSize, size_t Offset, const char *Text)
{
    size_t Length = strlen(Text);

    if (Offset >= BufferSize || Length >= BufferSize - Offset)
        return BufferSize;
    memcpy(Buffer + Offset, Text, Length);
    Buffer[Offset + Length] = 0;
    return Offset + Length;
}

size_t
NlaBuildProbeRequest(
    char *Buffer,
    size_t BufferSize,
    const char *Host,
    const char *Path)
{
    size_t Offset;

    if (BufferSize == 0)
        return 0;
    Buffer[0] = 0;
    Offset = NlaAppend(Buffer, BufferSize, 0, "GET ");
    if (Path[0] != '/')
        Offset = NlaAppend(Buffer, BufferSize, Offset, "/");
    Offset = NlaAppend(Buffer, BufferSize, Offset, Path);
    Offset = NlaAppend(Buffer, BufferSize, Offset, " HTTP/1.0\r\nHost: ");
    Offset = NlaAppend(Buffer, BufferSize, Offset, Host);
    Offset = NlaAppend(Buffer, BufferSize, Offset, "\r\nConnection: close\r\n\r\n");
    if (Offset >= BufferSize)
    {
        Buffer[0] = 0;
        return 0;
    }
    return Offset;
}

static int
NlaParseStatus(const char *Response, size_t Length)
{
    size_t Index = 5;
    int Status = 0, Digits = 0;

    if (!NlaStartsWithNoCase(Response, Length, "HTTP/"))
        return -1;
    while (Index < Length && Response[Index] != ' ')
    {
        if (Response[Index] == '\r' || Response[Index] == '\n')
            return -1;
        Index++;
    }
    while (Index < Length && Response[Index] == ' ')
        Index++;
    while (Index < Length && Digits < 3 && Response[Index] >= '0' && Response[Index] <= '9')
    {
        Status = Status * 10 + (Response[Index] - '0');
        Index++;
        Digits++;
    }
    return Digits == 3 ? Status : -1;
}

static const char *
NlaFindHeaderEnd(const char *Response, size_t Length, size_t *BodyOffset)
{
    size_t Index;

    for (Index = 0; Index < Length; Index++)
    {
        if (Response[Index] != '\n')
            continue;
        if (Index + 1 < Length && Response[Index + 1] == '\n')
        {
            *BodyOffset = Index + 2;
            return Response + Index + 2;
        }
        if (Index + 2 < Length && Response[Index + 1] == '\r' && Response[Index + 2] == '\n')
        {
            *BodyOffset = Index + 3;
            return Response + Index + 3;
        }
    }
    return NULL;
}

static int
NlaHeaderValue(const char *Headers, size_t Length, const char *Name, const char **Value, size_t *ValueLength)
{
    size_t Line = 0, NameLength = strlen(Name);

    while (Line < Length)
    {
        size_t End = Line;

        while (End < Length && Headers[End] != '\n')
            End++;
        if (End - Line > NameLength && NlaStartsWithNoCase(Headers + Line, End - Line, Name) &&
            Headers[Line + NameLength] == ':')
        {
            size_t Start = Line + NameLength + 1, Stop = End;

            while (Start < Stop && NlaIsSpace(Headers[Start]))
                Start++;
            while (Stop > Start && NlaIsSpace(Headers[Stop - 1]))
                Stop--;
            *Value = Headers + Start;
            *ValueLength = Stop - Start;
            return 1;
        }
        Line = End + 1;
    }
    return 0;
}

typedef struct _NLA_BODY_MATCH
{
    const char *Expected;
    size_t ExpectedLength;
    size_t Matched;
    int Mismatch;
} NLA_BODY_MATCH;

static void
NlaMatchBody(NLA_BODY_MATCH *Match, const char *Data, size_t Length)
{
    size_t Index;

    for (Index = 0; Index < Length && !Match->Mismatch; Index++)
    {
        if (Match->Matched < Match->ExpectedLength)
        {
            if (Data[Index] != Match->Expected[Match->Matched])
                Match->Mismatch = 1;
            else
                Match->Matched++;
        }
        else if (!NlaIsSpace(Data[Index]))
        {
            Match->Mismatch = 1;
        }
    }
}

static int
NlaMatchChunkedBody(NLA_BODY_MATCH *Match, const char *Body, size_t Length)
{
    size_t Offset = 0;

    while (Offset < Length)
    {
        size_t ChunkSize = 0;
        int Digit, Digits = 0;

        while (Offset < Length && (Digit = NlaHexValue(Body[Offset])) >= 0)
        {
            if (ChunkSize > ((size_t)-1 >> 4))
                return 0;
            ChunkSize = (ChunkSize << 4) | (size_t)Digit;
            Offset++;
            Digits++;
        }
        if (Digits == 0)
            return 0;
        while (Offset < Length && Body[Offset] != '\n')
            Offset++;
        if (Offset >= Length)
            return 0;
        Offset++;
        if (ChunkSize == 0)
            return 1;
        if (ChunkSize > Length - Offset)
            return 0;
        NlaMatchBody(Match, Body + Offset, ChunkSize);
        Offset += ChunkSize;
        if (Offset < Length && Body[Offset] == '\r')
            Offset++;
        if (Offset < Length && Body[Offset] == '\n')
            Offset++;
    }
    return 0;
}

NLA_PROBE_RESULT
NlaClassifyProbeResponse(
    const char *Response,
    size_t Length,
    const char *ExpectedContent)
{
    NLA_BODY_MATCH Match;
    const char *Body, *Value;
    size_t BodyOffset = 0, BodyLength, ValueLength;
    int Status;

    if (Length == 0)
        return NlaProbeNoInternet;

    Status = NlaParseStatus(Response, Length);
    if (Status == NLA_HTTP_FORBIDDEN)
        return NlaProbeNoInternet;
    if (Status != NLA_HTTP_OK)
        return NlaProbeCaptivePortal;

    Body = NlaFindHeaderEnd(Response, Length, &BodyOffset);
    if (!Body)
        return NlaProbeCaptivePortal;
    BodyLength = Length - BodyOffset;

    memset(&Match, 0, sizeof(Match));
    Match.Expected = ExpectedContent;
    Match.ExpectedLength = strlen(ExpectedContent);

    if (NlaHeaderValue(Response, BodyOffset, "Transfer-Encoding", &Value, &ValueLength) &&
        ValueLength >= 7 && NlaStartsWithNoCase(Value + ValueLength - 7, 7, "chunked"))
    {
        if (!NlaMatchChunkedBody(&Match, Body, BodyLength))
            return NlaProbeCaptivePortal;
    }
    else
    {
        if (NlaHeaderValue(Response, BodyOffset, "Content-Length", &Value, &ValueLength))
        {
            size_t Declared = 0, Index;

            for (Index = 0; Index < ValueLength; Index++)
            {
                if (Value[Index] < '0' || Value[Index] > '9' || Declared > ((size_t)-1 - 9) / 10)
                    return NlaProbeCaptivePortal;
                Declared = Declared * 10 + (size_t)(Value[Index] - '0');
            }
            if (Declared < BodyLength)
                BodyLength = Declared;
        }
        NlaMatchBody(&Match, Body, BodyLength);
    }

    if (Match.Mismatch || Match.Matched != Match.ExpectedLength)
        return NlaProbeCaptivePortal;
    return NlaProbeInternet;
}
