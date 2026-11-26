/*
 * PROJECT:     LiberNT Web Browser
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Address, character set, title and generated page helpers
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "browser.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BROWSER_META_SCAN 4096

static int
StartsWithNoCase(const char *Text, const char *Prefix)
{
    for (; *Prefix; Text++, Prefix++)
    {
        if (tolower((unsigned char)*Text) != tolower((unsigned char)*Prefix))
            return 0;
    }
    return 1;
}

static const char *
FindNoCase(const char *Text, size_t Length, const char *Needle)
{
    size_t NeedleLength = strlen(Needle);

    for (size_t Index = 0; NeedleLength <= Length && Index <= Length - NeedleLength; Index++)
    {
        size_t Match = 0;
        while (Match < NeedleLength &&
               tolower((unsigned char)Text[Index + Match]) == tolower((unsigned char)Needle[Match]))
        {
            Match++;
        }
        if (Match == NeedleLength)
            return Text + Index;
    }
    return NULL;
}

static int
Copy(char *Target, size_t Size, const char *Source, size_t Length)
{
    if (!Size || Length >= Size)
        return 0;
    memcpy(Target, Source, Length);
    Target[Length] = 0;
    return 1;
}

static int
Compose(char *Url, size_t Size, const char *Scheme, const char *Host, const char *End)
{
    size_t SchemeLength = strlen(Scheme), Length = (size_t)(End - Host), Authority = 0;
    int Slash;

    while (Authority < Length && Host[Authority] != '/' && Host[Authority] != '?' && Host[Authority] != '#')
        Authority++;
    Slash = Authority == Length || Host[Authority] != '/';
    if (!Authority || SchemeLength + Length + (size_t)Slash + 1 > Size)
        return 0;
    memcpy(Url, Scheme, SchemeLength);
    memcpy(Url + SchemeLength, Host, Authority);
    if (Slash)
        Url[SchemeLength + Authority] = '/';
    memcpy(Url + SchemeLength + Authority + Slash, Host + Authority, Length - Authority);
    Url[SchemeLength + Length + Slash] = 0;
    return 1;
}

int
BrowserNormalizeUrl(const char *Input, char *Url, size_t Size)
{
    const char *Start = Input, *End;
    size_t Length;

    if (!Input || !Url || !Size)
        return 0;
    Url[0] = 0;
    while (*Start && isspace((unsigned char)*Start))
        Start++;
    End = Start + strlen(Start);
    while (End > Start && isspace((unsigned char)End[-1]))
        End--;
    Length = (size_t)(End - Start);
    if (!Length)
        return 0;
    for (const char *Scan = Start; Scan < End; Scan++)
    {
        if (isspace((unsigned char)*Scan))
            return 0;
    }

    if (Length == 11 && StartsWithNoCase(Start, "about:blank"))
        return Copy(Url, Size, "about:blank", 11);
    if (StartsWithNoCase(Start, "http://") || StartsWithNoCase(Start, "https://"))
    {
        size_t SchemeLength = tolower((unsigned char)Start[4]) == 's' ? 8 : 7;
        char Scheme[9];
        memcpy(Scheme, Start, SchemeLength);
        Scheme[SchemeLength] = 0;
        return Compose(Url, Size, Scheme, Start + SchemeLength, End);
    }

    {
        const char *Colon = memchr(Start, ':', Length);
        const char *Slash = memchr(Start, '/', Length);
        if (Colon && (!Slash || Colon < Slash))
        {
            const char *Port = Colon + 1;
            const char *PortEnd = Slash ? Slash : End;
            if (Port == PortEnd)
                return 0;
            for (; Port < PortEnd; Port++)
            {
                if (!isdigit((unsigned char)*Port))
                    return 0;
            }
        }
        if (*Start == '/' || *Start == '.')
            return 0;
        return Compose(Url, Size, "http://", Start, End);
    }
}

static void
MediaType(const char *ContentType, char *Type, size_t Size)
{
    size_t Length = 0;

    if (!Size)
        return;
    while (ContentType && *ContentType && isspace((unsigned char)*ContentType))
        ContentType++;
    while (ContentType && ContentType[Length] && ContentType[Length] != ';' && Length + 1 < Size)
    {
        Type[Length] = (char)tolower((unsigned char)ContentType[Length]);
        Length++;
    }
    while (Length && isspace((unsigned char)Type[Length - 1]))
        Length--;
    Type[Length] = 0;
}

BROWSER_CONTENT
BrowserContentKind(const char *ContentType)
{
    static const char *const Html[] = { "text/html", "application/xhtml+xml" };
    static const char *const Text[] = { "text/plain", "text/css", "text/javascript", "text/xml", "text/csv",
                                        "application/javascript", "application/json", "application/xml" };
    static const char *const Images[] = { "image/png", "image/jpeg", "image/gif", "image/bmp", "image/svg+xml",
                                          "image/x-icon", "image/vnd.microsoft.icon" };
    char Type[128];

    MediaType(ContentType, Type, sizeof(Type));
    if (!Type[0])
        return BrowserContentHtml;
    for (size_t Index = 0; Index < sizeof(Html) / sizeof(Html[0]); Index++)
    {
        if (!strcmp(Type, Html[Index]))
            return BrowserContentHtml;
    }
    for (size_t Index = 0; Index < sizeof(Text) / sizeof(Text[0]); Index++)
    {
        if (!strcmp(Type, Text[Index]))
            return BrowserContentText;
    }
    for (size_t Index = 0; Index < sizeof(Images) / sizeof(Images[0]); Index++)
    {
        if (!strcmp(Type, Images[Index]))
            return BrowserContentImage;
    }
    return BrowserContentDownload;
}

static int
TakeValue(const char *Value, const char *End, char *Charset, size_t Size)
{
    size_t Length = 0;

    while (Value < End && (isspace((unsigned char)*Value) || *Value == '"' || *Value == '\''))
        Value++;
    while (Value + Length < End &&
           (isalnum((unsigned char)Value[Length]) || (Value[Length] && strchr("-_.:", Value[Length]))))
        Length++;
    return Length && Copy(Charset, Size, Value, Length);
}

int
BrowserCharset(const char *ContentType, const char *Body, size_t Length, char *Charset, size_t Size)
{
    const char *Found;
    size_t Scan = Length < BROWSER_META_SCAN ? Length : BROWSER_META_SCAN;

    if (!Size)
        return 0;
    Charset[0] = 0;
    if (ContentType && (Found = FindNoCase(ContentType, strlen(ContentType), "charset=")))
        return TakeValue(Found + 8, ContentType + strlen(ContentType), Charset, Size);
    if (Body && Length >= 3 && (unsigned char)Body[0] == 0xEF && (unsigned char)Body[1] == 0xBB &&
        (unsigned char)Body[2] == 0xBF)
    {
        return Copy(Charset, Size, "utf-8", 5);
    }
    for (const char *Meta = Body; Body && (Meta = FindNoCase(Meta, Scan - (size_t)(Meta - Body), "<meta"));)
    {
        const char *Close = memchr(Meta, '>', Scan - (size_t)(Meta - Body));
        const char *End = Close ? Close : Body + Scan;
        if ((Found = FindNoCase(Meta, (size_t)(End - Meta), "charset=")))
            return TakeValue(Found + 8, End, Charset, Size);
        Meta += 5;
    }
    return 0;
}

unsigned
BrowserCodePage(const char *Charset)
{
    static const struct
    {
        const char *Name;
        unsigned CodePage;
    } Names[] = {
        { "utf-8", 0 },           { "utf8", 0 },          { "us-ascii", 1252 },      { "ascii", 1252 },
        { "iso-8859-1", 1252 },   { "iso_8859-1", 1252 }, { "latin1", 1252 },        { "l1", 1252 },
        { "cp1252", 1252 },       { "iso-8859-13", 28603 }, { "iso-8859-15", 28605 }, { "shift_jis", 932 },
        { "shift-jis", 932 },     { "sjis", 932 },        { "windows-31j", 932 },    { "euc-jp", 20932 },
        { "gb2312", 936 },        { "gbk", 936 },         { "gb18030", 54936 },      { "big5", 950 },
        { "euc-kr", 949 },        { "ks_c_5601-1987", 949 }, { "koi8-r", 20866 },    { "koi8-u", 21866 },
    };
    char Lower[32];
    size_t Length = 0;

    if (!Charset)
        return 0;
    while (Charset[Length] && Length + 1 < sizeof(Lower))
    {
        Lower[Length] = (char)tolower((unsigned char)Charset[Length]);
        Length++;
    }
    Lower[Length] = 0;
    for (size_t Index = 0; Index < sizeof(Names) / sizeof(Names[0]); Index++)
    {
        if (!strcmp(Lower, Names[Index].Name))
            return Names[Index].CodePage;
    }
    if (!strncmp(Lower, "windows-125", 11) && Lower[11] >= '0' && Lower[11] <= '8' && !Lower[12])
        return 1250 + (unsigned)(Lower[11] - '0');
    if (!strncmp(Lower, "iso-8859-", 9) && Lower[9] >= '2' && Lower[9] <= '9' && !Lower[10])
        return 28590 + (unsigned)(Lower[9] - '0');
    return 0;
}

static size_t
PutUtf8(char *Target, unsigned long Code)
{
    if (Code < 0x80)
    {
        Target[0] = (char)Code;
        return 1;
    }
    if (Code < 0x800)
    {
        Target[0] = (char)(0xC0 | (Code >> 6));
        Target[1] = (char)(0x80 | (Code & 0x3F));
        return 2;
    }
    if (Code < 0x10000)
    {
        Target[0] = (char)(0xE0 | (Code >> 12));
        Target[1] = (char)(0x80 | ((Code >> 6) & 0x3F));
        Target[2] = (char)(0x80 | (Code & 0x3F));
        return 3;
    }
    Target[0] = (char)(0xF0 | (Code >> 18));
    Target[1] = (char)(0x80 | ((Code >> 12) & 0x3F));
    Target[2] = (char)(0x80 | ((Code >> 6) & 0x3F));
    Target[3] = (char)(0x80 | (Code & 0x3F));
    return 4;
}

size_t
BrowserTitle(const char *Html, size_t Length, char *Title, size_t Size)
{
    static const struct
    {
        const char *Name;
        unsigned long Code;
    } Entities[] = { { "amp;", '&' }, { "lt;", '<' }, { "gt;", '>' }, { "quot;", '"' }, { "apos;", '\'' },
                     { "nbsp;", ' ' } };
    const char *Open, *Start, *Stop;
    size_t Out = 0;
    int Space = 0, Truncated = 0;

    if (!Size)
        return 0;
    Title[0] = 0;
    if (!Html || !(Open = FindNoCase(Html, Length, "<title")))
        return 0;
    Start = memchr(Open, '>', Length - (size_t)(Open - Html));
    if (!Start)
        return 0;
    Start++;
    Stop = FindNoCase(Start, Length - (size_t)(Start - Html), "</title");
    if (!Stop)
        return 0;

    for (const char *Scan = Start; Scan < Stop;)
    {
        char Encoded[4];
        size_t Bytes = 1;
        unsigned long Code = (unsigned char)*Scan;
        size_t Advance = 1;

        if (*Scan == '&')
        {
            size_t Left = (size_t)(Stop - Scan - 1);
            if (Left > 1 && Scan[1] == '#')
            {
                char *Next;
                int Hex = Left > 2 && (Scan[2] == 'x' || Scan[2] == 'X');
                unsigned long Value = strtoul(Scan + (Hex ? 3 : 2), &Next, Hex ? 16 : 10);
                if (Next < Stop && *Next == ';' && Value && Value <= 0x10FFFF)
                {
                    Code = Value;
                    Advance = (size_t)(Next - Scan) + 1;
                }
            }
            else
            {
                for (size_t Index = 0; Index < sizeof(Entities) / sizeof(Entities[0]); Index++)
                {
                    size_t NameLength = strlen(Entities[Index].Name);
                    if (NameLength <= Left && !strncmp(Scan + 1, Entities[Index].Name, NameLength))
                    {
                        Code = Entities[Index].Code;
                        Advance = NameLength + 1;
                        break;
                    }
                }
            }
            Bytes = PutUtf8(Encoded, Code);
        }
        else
        {
            Encoded[0] = *Scan;
        }
        Scan += Advance;

        if (Bytes == 1 && isspace((unsigned char)Encoded[0]))
        {
            Space = Out != 0;
            continue;
        }
        if (Out + Space + Bytes >= Size)
        {
            Truncated = 1;
            break;
        }
        if (Space)
        {
            Title[Out++] = ' ';
            Space = 0;
        }
        memcpy(Title + Out, Encoded, Bytes);
        Out += Bytes;
    }
    if (Truncated && Out)
    {
        size_t Lead = Out - 1, Expected;
        while (Lead && ((unsigned char)Title[Lead] & 0xC0) == 0x80)
            Lead--;
        unsigned char First = (unsigned char)Title[Lead];
        Expected = First < 0x80 ? 1 : First >= 0xF0 ? 4 : First >= 0xE0 ? 3 : 2;
        if (Out - Lead < Expected)
            Out = Lead;
    }
    Title[Out] = 0;
    return Out;
}

static size_t
PercentDecode(const char *Text, size_t Length, char *Target, size_t Size)
{
    size_t Out = 0;

    for (size_t Index = 0; Index < Length && Out + 1 < Size; Index++)
    {
        if (Text[Index] == '%' && Index + 2 < Length && isxdigit((unsigned char)Text[Index + 1]) &&
            isxdigit((unsigned char)Text[Index + 2]))
        {
            char Hex[3] = { Text[Index + 1], Text[Index + 2], 0 };
            Target[Out++] = (char)strtoul(Hex, NULL, 16);
            Index += 2;
        }
        else
        {
            Target[Out++] = Text[Index];
        }
    }
    Target[Out] = 0;
    return Out;
}

void
BrowserFileName(const char *Disposition, const char *Url, char *Name, size_t Size)
{
    const char *Found;
    size_t Length = 0;

    if (!Size)
        return;
    Name[0] = 0;
    if (Disposition && (Found = FindNoCase(Disposition, strlen(Disposition), "filename*=")))
    {
        const char *Value = strstr(Found, "''");
        if (Value)
        {
            Value += 2;
            Length = PercentDecode(Value, strcspn(Value, "; \t"), Name, Size);
        }
    }
    if (!Length && Disposition && (Found = FindNoCase(Disposition, strlen(Disposition), "filename=")))
    {
        const char *Value = Found + 9;
        size_t ValueLength;
        if (*Value == '"')
        {
            Value++;
            ValueLength = strcspn(Value, "\"");
        }
        else
        {
            ValueLength = strcspn(Value, "; \t");
        }
        Length = ValueLength < Size ? ValueLength : Size - 1;
        memcpy(Name, Value, Length);
        Name[Length] = 0;
    }
    if (!Length && Url)
    {
        const char *Path = strstr(Url, "://");
        const char *Stop, *Slash;
        Path = Path ? strchr(Path + 3, '/') : NULL;
        if (Path)
        {
            Stop = Path + strcspn(Path, "?#");
            Slash = Stop;
            while (Slash > Path && Slash[-1] != '/')
                Slash--;
            Length = PercentDecode(Slash, (size_t)(Stop - Slash), Name, Size);
        }
    }

    for (size_t Index = 0; Index < Length; Index++)
    {
        unsigned char Character = (unsigned char)Name[Index];
        if (Character < 0x20 || strchr("\\/:*?\"<>|", Character))
            Name[Index] = '_';
    }
    while (Length && (Name[Length - 1] == '.' || Name[Length - 1] == ' '))
        Name[--Length] = 0;
    if (!Length)
        Copy(Name, Size, "download", 8);
}

static char *
Escape(const char *Text, size_t Length, size_t *Escaped)
{
    char *Result = Length < ((size_t)-1 - 1) / 6 ? malloc(Length * 6 + 1) : NULL;
    size_t Out = 0;

    if (!Result)
        return NULL;
    for (size_t Index = 0; Index < Length; Index++)
    {
        const char *Entity = NULL;
        switch (Text[Index])
        {
            case '&': Entity = "&amp;"; break;
            case '<': Entity = "&lt;"; break;
            case '>': Entity = "&gt;"; break;
            case '"': Entity = "&quot;"; break;
        }
        if (Entity)
        {
            memcpy(Result + Out, Entity, strlen(Entity));
            Out += strlen(Entity);
        }
        else
        {
            Result[Out++] = Text[Index];
        }
    }
    Result[Out] = 0;
    *Escaped = Out;
    return Result;
}

static char *
Wrap(const char *Before, const char *Text, size_t Length, const char *After, size_t *PageLength)
{
    size_t Escaped = 0, Total;
    char *Inner = Escape(Text, Length, &Escaped), *Page;

    if (!Inner)
        return NULL;
    Total = strlen(Before) + Escaped + strlen(After);
    Page = malloc(Total + 1);
    if (Page)
    {
        size_t BeforeLength = strlen(Before);
        memcpy(Page, Before, BeforeLength);
        memcpy(Page + BeforeLength, Inner, Escaped);
        memcpy(Page + BeforeLength + Escaped, After, strlen(After) + 1);
        *PageLength = Total;
    }
    free(Inner);
    return Page;
}

char *
BrowserTextPage(const char *Text, size_t Length, size_t *PageLength)
{
    return Wrap("<!DOCTYPE html><html><head><meta charset=\"utf-8\"></head><body style=\"margin:8px\">"
                "<pre style=\"white-space:pre-wrap\">",
                Text, Length, "</pre></body></html>", PageLength);
}

char *
BrowserImagePage(const char *Url, size_t *PageLength)
{
    return Wrap("<!DOCTYPE html><html><body style=\"margin:0;background-color:#0e0e0e;text-align:center\"><img src=\"",
                Url, strlen(Url), "\" style=\"max-width:100%\"></body></html>", PageLength);
}

char *
BrowserMessagePage(const char *Heading, const char *Detail, size_t *PageLength)
{
    size_t HeadingLength = 0, DetailLength = 0;
    char *EscapedHeading = Escape(Heading, strlen(Heading), &HeadingLength);
    char *EscapedDetail = Escape(Detail, strlen(Detail), &DetailLength);
    char *Page = NULL;
    static const char Format[] = "<!DOCTYPE html><html><head><meta charset=\"utf-8\"></head>"
                                 "<body style=\"font-family:sans-serif;margin:32px;color:#202020\">"
                                 "<h2 style=\"font-weight:normal\">%s</h2><p style=\"color:#606060\">%s</p></body></html>";

    if (EscapedHeading && EscapedDetail)
    {
        size_t Size = sizeof(Format) + HeadingLength + DetailLength;
        Page = malloc(Size);
        if (Page)
        {
            int Written = snprintf(Page, Size, Format, EscapedHeading, EscapedDetail);
            *PageLength = Written > 0 ? (size_t)Written : 0;
        }
    }
    free(EscapedHeading);
    free(EscapedDetail);
    return Page;
}
