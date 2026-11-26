/*
 * PROJECT:     LiberNT Captive Portal Sign-in
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     HTML page reader and form encoder
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "html.h"

#include <stdlib.h>
#include <string.h>

#define HTML_MAX_ATTRIBUTES 64
#define HTML_MAX_TAG_NAME 16
#define HTML_MAX_ENTITY 32

typedef struct _HTML_BUFFER
{
    char *Data;
    size_t Length;
    size_t Capacity;
} HTML_BUFFER;

typedef struct _HTML_ATTRIBUTE
{
    char Name[HTML_MAX_TAG_NAME * 2];
    char *Value;
} HTML_ATTRIBUTE;

typedef struct _HTML_TAG
{
    char Name[HTML_MAX_TAG_NAME];
    int Closing;
    HTML_ATTRIBUTE Attributes[HTML_MAX_ATTRIBUTES];
    size_t AttributeCount;
} HTML_TAG;

typedef struct _HTML_PARSER
{
    HTML_DOCUMENT *Document;
    HTML_BUFFER Text;
    HTML_BUFFER LinkText;
    HTML_BUFFER ButtonText;
    HTML_BUFFER OptionText;
    HTML_BUFFER LabelText;
    char *LinkHref;
    char *OptionValue;
    int Heading;
    int CurrentForm;
    int InHead;
    int InLink;
    int InButton;
    int InSelect;
    int InOption;
    int InLabel;
    int LinkInLabel;
    int HiddenDepth;
    char HiddenTag[HTML_MAX_TAG_NAME];
    int OptionSelected;
    int OptionDisabled;
    size_t ButtonItem;
    size_t SelectItem;
    long LabelTarget;
    int Failed;
} HTML_PARSER;

typedef struct _HTML_ENTITY
{
    const char *Name;
    unsigned int CodePoint;
} HTML_ENTITY;

static const HTML_ENTITY Entities[] =
{
    { "amp", '&' }, { "lt", '<' }, { "gt", '>' }, { "quot", '"' }, { "apos", '\'' }, { "nbsp", 0xA0 },
    { "copy", 0xA9 }, { "reg", 0xAE }, { "trade", 0x2122 }, { "hellip", 0x2026 }, { "mdash", 0x2014 },
    { "ndash", 0x2013 }, { "laquo", 0xAB }, { "raquo", 0xBB }, { "lsquo", 0x2018 }, { "rsquo", 0x2019 },
    { "ldquo", 0x201C }, { "rdquo", 0x201D }, { "bull", 0x2022 }, { "middot", 0xB7 }, { "euro", 0x20AC },
    { "pound", 0xA3 }, { "yen", 0xA5 }, { "cent", 0xA2 }, { "sect", 0xA7 }, { "deg", 0xB0 }, { "times", 0xD7 },
    { "divide", 0xF7 }, { "iexcl", 0xA1 }, { "iquest", 0xBF }, { "agrave", 0xE0 }, { "aacute", 0xE1 },
    { "acirc", 0xE2 }, { "atilde", 0xE3 }, { "auml", 0xE4 }, { "aring", 0xE5 }, { "aelig", 0xE6 },
    { "ccedil", 0xE7 }, { "egrave", 0xE8 }, { "eacute", 0xE9 }, { "ecirc", 0xEA }, { "euml", 0xEB },
    { "igrave", 0xEC }, { "iacute", 0xED }, { "icirc", 0xEE }, { "iuml", 0xEF }, { "ntilde", 0xF1 },
    { "ograve", 0xF2 }, { "oacute", 0xF3 }, { "ocirc", 0xF4 }, { "otilde", 0xF5 }, { "ouml", 0xF6 },
    { "oslash", 0xF8 }, { "ugrave", 0xF9 }, { "uacute", 0xFA }, { "ucirc", 0xFB }, { "uuml", 0xFC },
    { "yacute", 0xFD }, { "yuml", 0xFF }, { "szlig", 0xDF }, { "Agrave", 0xC0 }, { "Aacute", 0xC1 },
    { "Acirc", 0xC2 }, { "Auml", 0xC4 }, { "Ccedil", 0xC7 }, { "Egrave", 0xC8 }, { "Eacute", 0xC9 },
    { "Ecirc", 0xCA }, { "Ouml", 0xD6 }, { "Uuml", 0xDC }, { "Ntilde", 0xD1 },
};

static const char *const BlockTags[] =
{
    "address", "article", "aside", "blockquote", "body", "center", "dd", "details", "div", "dl", "dt",
    "fieldset", "figcaption", "figure", "footer", "header", "hr", "html", "legend", "li", "main", "nav",
    "ol", "p", "pre", "section", "summary", "table", "td", "th", "tr", "ul",
};

static const char *const VoidTags[] =
{
    "area", "base", "br", "col", "embed", "hr", "img", "input", "link", "meta", "param", "source", "track", "wbr",
};

static const char *const SkippedTags[] =
{
    "audio", "canvas", "iframe", "math", "object", "script", "style", "svg", "template", "video",
};

static int
HtmlLower(int Character)
{
    return (Character >= 'A' && Character <= 'Z') ? Character - 'A' + 'a' : Character;
}

static int
HtmlEqualNoCase(const char *Left, const char *Right)
{
    if (!Left || !Right)
        return 0;
    while (*Left && *Right)
    {
        if (HtmlLower((unsigned char)*Left) != HtmlLower((unsigned char)*Right))
            return 0;
        Left++;
        Right++;
    }
    return *Left == *Right;
}

static int
HtmlStartsWithNoCase(const char *Text, size_t Length, const char *Prefix)
{
    size_t Index;

    for (Index = 0; Prefix[Index]; Index++)
    {
        if (Index >= Length || HtmlLower((unsigned char)Text[Index]) != HtmlLower((unsigned char)Prefix[Index]))
            return 0;
    }
    return 1;
}

static int
HtmlIsSpace(char Character)
{
    return Character == ' ' || Character == '\t' || Character == '\r' || Character == '\n' || Character == '\f';
}

static char *
HtmlStrNDup(const char *Text, size_t Length)
{
    char *Copy = malloc(Length + 1);

    if (Copy)
    {
        memcpy(Copy, Text, Length);
        Copy[Length] = 0;
    }
    return Copy;
}

static char *
HtmlStrDup(const char *Text)
{
    return Text ? HtmlStrNDup(Text, strlen(Text)) : NULL;
}

static int
HtmlBufferReserve(HTML_BUFFER *Buffer, size_t Extra)
{
    size_t Capacity;
    char *Data;

    if (Extra > (size_t)-1 - Buffer->Length - 1)
        return 0;
    if (Buffer->Length + Extra + 1 <= Buffer->Capacity)
        return 1;
    Capacity = Buffer->Capacity ? Buffer->Capacity : 64;
    while (Capacity < Buffer->Length + Extra + 1)
    {
        if (Capacity > (size_t)-1 / 2)
            return 0;
        Capacity *= 2;
    }
    Data = realloc(Buffer->Data, Capacity);
    if (!Data)
        return 0;
    Buffer->Data = Data;
    Buffer->Capacity = Capacity;
    return 1;
}

static int
HtmlBufferAppend(HTML_BUFFER *Buffer, const char *Text, size_t Length)
{
    if (!HtmlBufferReserve(Buffer, Length))
        return 0;
    memcpy(Buffer->Data + Buffer->Length, Text, Length);
    Buffer->Length += Length;
    Buffer->Data[Buffer->Length] = 0;
    return 1;
}

static int
HtmlBufferAppendString(HTML_BUFFER *Buffer, const char *Text)
{
    return HtmlBufferAppend(Buffer, Text, strlen(Text));
}

static int
HtmlBufferAppendCodePoint(HTML_BUFFER *Buffer, unsigned int CodePoint)
{
    char Bytes[4];
    size_t Length;

    if (CodePoint == 0 || CodePoint > 0x10FFFF || (CodePoint >= 0xD800 && CodePoint <= 0xDFFF))
        CodePoint = 0xFFFD;
    if (CodePoint < 0x80)
    {
        Bytes[0] = (char)CodePoint;
        Length = 1;
    }
    else if (CodePoint < 0x800)
    {
        Bytes[0] = (char)(0xC0 | (CodePoint >> 6));
        Bytes[1] = (char)(0x80 | (CodePoint & 0x3F));
        Length = 2;
    }
    else if (CodePoint < 0x10000)
    {
        Bytes[0] = (char)(0xE0 | (CodePoint >> 12));
        Bytes[1] = (char)(0x80 | ((CodePoint >> 6) & 0x3F));
        Bytes[2] = (char)(0x80 | (CodePoint & 0x3F));
        Length = 3;
    }
    else
    {
        Bytes[0] = (char)(0xF0 | (CodePoint >> 18));
        Bytes[1] = (char)(0x80 | ((CodePoint >> 12) & 0x3F));
        Bytes[2] = (char)(0x80 | ((CodePoint >> 6) & 0x3F));
        Bytes[3] = (char)(0x80 | (CodePoint & 0x3F));
        Length = 4;
    }
    return HtmlBufferAppend(Buffer, Bytes, Length);
}

static void
HtmlBufferReset(HTML_BUFFER *Buffer)
{
    Buffer->Length = 0;
    if (Buffer->Data)
        Buffer->Data[0] = 0;
}

static void
HtmlBufferFree(HTML_BUFFER *Buffer)
{
    free(Buffer->Data);
    Buffer->Data = NULL;
    Buffer->Length = Buffer->Capacity = 0;
}

static size_t
HtmlDecodeEntity(const char *Text, size_t Length, unsigned int *CodePoint)
{
    char Name[HTML_MAX_ENTITY + 1];
    size_t End, Index;

    if (Length < 3 || Text[0] != '&')
        return 0;
    for (End = 1; End < Length && End <= HTML_MAX_ENTITY && Text[End] != ';'; End++)
    {
        if (HtmlIsSpace(Text[End]) || Text[End] == '&' || Text[End] == '<')
            return 0;
    }
    if (End >= Length || Text[End] != ';' || End == 1)
        return 0;

    if (Text[1] == '#')
    {
        unsigned long Value = 0;
        int Hex = (End > 2 && (Text[2] == 'x' || Text[2] == 'X'));
        size_t Start = Hex ? 3 : 2;

        if (Start >= End)
            return 0;
        for (Index = Start; Index < End; Index++)
        {
            int Digit;
            char Character = Text[Index];

            if (Character >= '0' && Character <= '9')
                Digit = Character - '0';
            else if (Hex && HtmlLower((unsigned char)Character) >= 'a' && HtmlLower((unsigned char)Character) <= 'f')
                Digit = HtmlLower((unsigned char)Character) - 'a' + 10;
            else
                return 0;
            Value = Value * (Hex ? 16 : 10) + (unsigned long)Digit;
            if (Value > 0x10FFFF)
                Value = 0x110000;
        }
        *CodePoint = (unsigned int)Value;
        return End + 1;
    }

    memcpy(Name, Text + 1, End - 1);
    Name[End - 1] = 0;
    for (Index = 0; Index < sizeof(Entities) / sizeof(Entities[0]); Index++)
    {
        if (strcmp(Entities[Index].Name, Name) == 0)
        {
            *CodePoint = Entities[Index].CodePoint;
            return End + 1;
        }
    }
    return 0;
}

static int
HtmlAppendDecoded(HTML_BUFFER *Buffer, const char *Text, size_t Length)
{
    size_t Index = 0, Start = 0;

    while (Index < Length)
    {
        unsigned int CodePoint;
        size_t Used;

        if (Text[Index] != '&')
        {
            Index++;
            continue;
        }
        Used = HtmlDecodeEntity(Text + Index, Length - Index, &CodePoint);
        if (Used == 0)
        {
            Index++;
            continue;
        }
        if (!HtmlBufferAppend(Buffer, Text + Start, Index - Start) || !HtmlBufferAppendCodePoint(Buffer, CodePoint))
            return 0;
        Index += Used;
        Start = Index;
    }
    return HtmlBufferAppend(Buffer, Text + Start, Length - Start);
}

static char *
HtmlDecodeDup(const char *Text, size_t Length)
{
    HTML_BUFFER Buffer = { 0 };

    if (!HtmlBufferReserve(&Buffer, Length) || !HtmlAppendDecoded(&Buffer, Text, Length))
    {
        HtmlBufferFree(&Buffer);
        return NULL;
    }
    return Buffer.Data;
}

static char *
HtmlCollapse(const char *Text, size_t Length)
{
    char *Result = malloc(Length + 1);
    size_t Index, Out = 0;
    int Space = 0;

    if (!Result)
        return NULL;
    for (Index = 0; Index < Length; Index++)
    {
        unsigned char Character = (unsigned char)Text[Index];

        if (HtmlIsSpace((char)Character) ||
            (Character == 0xC2 && Index + 1 < Length && (unsigned char)Text[Index + 1] == 0xA0))
        {
            if (Character == 0xC2)
                Index++;
            Space = 1;
            continue;
        }
        if (Space && Out != 0)
            Result[Out++] = ' ';
        Space = 0;
        Result[Out++] = (char)Character;
    }
    Result[Out] = 0;
    return Result;
}

static char *
HtmlTakeCollapsed(HTML_BUFFER *Buffer)
{
    char *Text = HtmlCollapse(Buffer->Data ? Buffer->Data : "", Buffer->Length);

    HtmlBufferReset(Buffer);
    if (Text && Text[0] == 0)
    {
        free(Text);
        return NULL;
    }
    return Text;
}

static const char *
HtmlAttribute(const HTML_TAG *Tag, const char *Name)
{
    size_t Index;

    for (Index = 0; Index < Tag->AttributeCount; Index++)
    {
        if (strcmp(Tag->Attributes[Index].Name, Name) == 0)
            return Tag->Attributes[Index].Value ? Tag->Attributes[Index].Value : "";
    }
    return NULL;
}

static char *
HtmlLowerDup(const char *Text, const char *Default)
{
    char *Copy = HtmlStrDup(Text ? Text : Default);
    size_t Index;

    for (Index = 0; Copy && Copy[Index]; Index++)
        Copy[Index] = (char)HtmlLower((unsigned char)Copy[Index]);
    return Copy;
}

static void
HtmlFreeTag(HTML_TAG *Tag)
{
    size_t Index;

    for (Index = 0; Index < Tag->AttributeCount; Index++)
        free(Tag->Attributes[Index].Value);
    Tag->AttributeCount = 0;
}

static size_t
HtmlReadTag(const char *Data, size_t Length, HTML_TAG *Tag)
{
    size_t Index = 1, NameLength = 0;

    memset(Tag, 0, sizeof(*Tag));
    if (Index < Length && Data[Index] == '/')
    {
        Tag->Closing = 1;
        Index++;
    }
    if (Index >= Length || !((Data[Index] >= 'a' && Data[Index] <= 'z') || (Data[Index] >= 'A' && Data[Index] <= 'Z')))
        return 0;
    while (Index < Length && !HtmlIsSpace(Data[Index]) && Data[Index] != '>' && Data[Index] != '/')
    {
        if (NameLength < sizeof(Tag->Name) - 1)
            Tag->Name[NameLength++] = (char)HtmlLower((unsigned char)Data[Index]);
        Index++;
    }

    for (;;)
    {
        size_t NameStart, NameEnd, ValueStart, ValueEnd;
        HTML_ATTRIBUTE *Attribute = NULL;

        while (Index < Length && (HtmlIsSpace(Data[Index]) || Data[Index] == '/'))
            Index++;
        if (Index >= Length)
            return Length;
        if (Data[Index] == '>')
            return Index + 1;

        NameStart = Index;
        while (Index < Length && !HtmlIsSpace(Data[Index]) && Data[Index] != '=' && Data[Index] != '>' &&
               !(Data[Index] == '/' && Index + 1 < Length && Data[Index + 1] == '>'))
        {
            Index++;
        }
        NameEnd = Index;
        if (NameEnd == NameStart)
        {
            Index++;
            continue;
        }
        if (Tag->AttributeCount < HTML_MAX_ATTRIBUTES)
        {
            size_t Copy = NameEnd - NameStart, Char;

            Attribute = &Tag->Attributes[Tag->AttributeCount++];
            if (Copy >= sizeof(Attribute->Name))
                Copy = sizeof(Attribute->Name) - 1;
            for (Char = 0; Char < Copy; Char++)
                Attribute->Name[Char] = (char)HtmlLower((unsigned char)Data[NameStart + Char]);
            Attribute->Name[Copy] = 0;
        }

        while (Index < Length && HtmlIsSpace(Data[Index]))
            Index++;
        if (Index >= Length || Data[Index] != '=')
            continue;
        Index++;
        while (Index < Length && HtmlIsSpace(Data[Index]))
            Index++;
        if (Index < Length && (Data[Index] == '"' || Data[Index] == '\''))
        {
            char Quote = Data[Index++];

            ValueStart = Index;
            while (Index < Length && Data[Index] != Quote)
                Index++;
            ValueEnd = Index;
            if (Index < Length)
                Index++;
        }
        else
        {
            ValueStart = Index;
            while (Index < Length && !HtmlIsSpace(Data[Index]) && Data[Index] != '>')
                Index++;
            ValueEnd = Index;
        }
        if (Attribute)
            Attribute->Value = HtmlDecodeDup(Data + ValueStart, ValueEnd - ValueStart);
    }
}

static size_t
HtmlFindClosing(const char *Data, size_t Length, size_t Start, const char *Name, size_t *ContentEnd)
{
    size_t Index, NameLength = strlen(Name);

    for (Index = Start; Index + 2 + NameLength <= Length; Index++)
    {
        if (Data[Index] == '<' && Data[Index + 1] == '/' &&
            HtmlStartsWithNoCase(Data + Index + 2, Length - Index - 2, Name) &&
            (Index + 2 + NameLength == Length || HtmlIsSpace(Data[Index + 2 + NameLength]) ||
             Data[Index + 2 + NameLength] == '>'))
        {
            *ContentEnd = Index;
            while (Index < Length && Data[Index] != '>')
                Index++;
            return Index < Length ? Index + 1 : Length;
        }
    }
    *ContentEnd = Length;
    return Length;
}

static int
HtmlIsOneOf(const char *Name, const char *const *List, size_t Count)
{
    size_t Index;

    for (Index = 0; Index < Count; Index++)
    {
        if (strcmp(Name, List[Index]) == 0)
            return 1;
    }
    return 0;
}

static HTML_ITEM *
HtmlAddItem(HTML_PARSER *Parser, HTML_ITEM_KIND Kind)
{
    HTML_DOCUMENT *Document = Parser->Document;
    HTML_ITEM *Item;

    if (Document->ItemCount == Document->ItemCapacity)
    {
        size_t Capacity = Document->ItemCapacity ? Document->ItemCapacity * 2 : 32;
        HTML_ITEM *Items = realloc(Document->Items, Capacity * sizeof(*Items));

        if (!Items)
        {
            Parser->Failed = 1;
            return NULL;
        }
        Document->Items = Items;
        Document->ItemCapacity = Capacity;
    }
    Item = &Document->Items[Document->ItemCount++];
    memset(Item, 0, sizeof(*Item));
    Item->Kind = Kind;
    Item->Form = Parser->CurrentForm;
    return Item;
}

static void
HtmlFlushText(HTML_PARSER *Parser)
{
    char *Text = HtmlTakeCollapsed(&Parser->Text);
    HTML_ITEM *Item;

    if (!Text)
        return;
    Item = HtmlAddItem(Parser, HtmlItemText);
    if (!Item)
    {
        free(Text);
        return;
    }
    Item->Heading = Parser->Heading;
    Item->Text = Text;
    Item->Form = -1;
}

static void
HtmlAppendText(HTML_PARSER *Parser, const char *Text, size_t Length)
{
    HTML_BUFFER *Target;

    if (Parser->InHead || Parser->HiddenDepth)
        return;
    if (Parser->InOption)
        Target = &Parser->OptionText;
    else if (Parser->InButton)
        Target = &Parser->ButtonText;
    else if (Parser->InLink)
    {
        Target = &Parser->LinkText;
        if (Parser->LinkInLabel && !HtmlAppendDecoded(&Parser->LabelText, Text, Length))
            Parser->Failed = 1;
    }
    else if (Parser->InLabel)
        Target = &Parser->LabelText;
    else if (Parser->InSelect)
        return;
    else
        Target = &Parser->Text;
    if (!HtmlAppendDecoded(Target, Text, Length))
        Parser->Failed = 1;
}

static void
HtmlAppendInline(HTML_PARSER *Parser, const char *Text)
{
    HtmlAppendText(Parser, Text, strlen(Text));
}

static void
HtmlFinishLink(HTML_PARSER *Parser)
{
    char *Text;
    HTML_ITEM *Item;

    if (!Parser->InLink)
        return;
    Parser->InLink = 0;
    Parser->LinkInLabel = 0;
    Text = HtmlTakeCollapsed(&Parser->LinkText);
    if (!Text)
    {
        free(Parser->LinkHref);
        Parser->LinkHref = NULL;
        return;
    }
    Item = HtmlAddItem(Parser, HtmlItemLink);
    if (!Item)
    {
        free(Text);
        free(Parser->LinkHref);
        Parser->LinkHref = NULL;
        return;
    }
    Item->Text = Text;
    Item->Href = Parser->LinkHref;
    Item->Form = -1;
    Parser->LinkHref = NULL;
}

static void
HtmlFinishButton(HTML_PARSER *Parser)
{
    if (!Parser->InButton)
        return;
    Parser->InButton = 0;
    if (Parser->ButtonItem < Parser->Document->ItemCount)
        Parser->Document->Items[Parser->ButtonItem].Text = HtmlTakeCollapsed(&Parser->ButtonText);
    else
        HtmlBufferReset(&Parser->ButtonText);
}

static void
HtmlFinishOption(HTML_PARSER *Parser)
{
    HTML_ITEM *Select;
    HTML_OPTION *Options;
    char *Text;

    if (!Parser->InOption)
        return;
    Parser->InOption = 0;
    Text = HtmlTakeCollapsed(&Parser->OptionText);
    if (Parser->SelectItem >= Parser->Document->ItemCount || Parser->OptionDisabled)
    {
        free(Text);
        free(Parser->OptionValue);
        Parser->OptionValue = NULL;
        return;
    }
    Select = &Parser->Document->Items[Parser->SelectItem];
    Options = realloc(Select->Options, (Select->OptionCount + 1) * sizeof(*Options));
    if (!Options)
    {
        Parser->Failed = 1;
        free(Text);
        free(Parser->OptionValue);
        Parser->OptionValue = NULL;
        return;
    }
    Select->Options = Options;
    Options[Select->OptionCount].Text = Text ? Text : HtmlStrDup("");
    Options[Select->OptionCount].Value = Parser->OptionValue ? Parser->OptionValue : HtmlStrDup(Text ? Text : "");
    Options[Select->OptionCount].Selected = Parser->OptionSelected;
    Select->OptionCount++;
    Parser->OptionValue = NULL;
}

static int
HtmlIsCheckable(const HTML_ITEM *Item)
{
    return Item->Kind == HtmlItemInput && Item->Type &&
           (strcmp(Item->Type, "checkbox") == 0 || strcmp(Item->Type, "radio") == 0);
}

static void
HtmlFinishLabel(HTML_PARSER *Parser)
{
    char *Text;

    if (!Parser->InLabel)
        return;
    Parser->InLabel = 0;
    Text = HtmlTakeCollapsed(&Parser->LabelText);
    if (!Text)
        return;
    if (Parser->LabelTarget >= 0 && (size_t)Parser->LabelTarget < Parser->Document->ItemCount &&
        !Parser->Document->Items[Parser->LabelTarget].Text)
    {
        Parser->Document->Items[Parser->LabelTarget].Text = Text;
        return;
    }
    if (!HtmlBufferAppendString(&Parser->Text, Text))
        Parser->Failed = 1;
    free(Text);
    HtmlFlushText(Parser);
}

static void
HtmlParseRefresh(HTML_DOCUMENT *Document, const char *Content)
{
    const char *Cursor = Content;
    int Seconds = 0;

    while (HtmlIsSpace(*Cursor))
        Cursor++;
    while (*Cursor >= '0' && *Cursor <= '9')
    {
        if (Seconds < 100000)
            Seconds = Seconds * 10 + (*Cursor - '0');
        Cursor++;
    }
    while (*Cursor && *Cursor != ';' && *Cursor != ',')
        Cursor++;
    Document->HasRefresh = 1;
    Document->RefreshSeconds = Seconds;
    if (!*Cursor)
        return;
    Cursor++;
    while (HtmlIsSpace(*Cursor))
        Cursor++;
    if (HtmlStartsWithNoCase(Cursor, strlen(Cursor), "url"))
    {
        const char *Equal = Cursor + 3;

        while (HtmlIsSpace(*Equal))
            Equal++;
        if (*Equal == '=')
        {
            Cursor = Equal + 1;
            while (HtmlIsSpace(*Cursor))
                Cursor++;
        }
    }
    if (*Cursor == '"' || *Cursor == '\'')
    {
        char Quote = *Cursor++;
        const char *End = strchr(Cursor, Quote);

        free(Document->RefreshUrl);
        Document->RefreshUrl = HtmlStrNDup(Cursor, End ? (size_t)(End - Cursor) : strlen(Cursor));
    }
    else if (*Cursor)
    {
        size_t Length = strlen(Cursor);

        while (Length && HtmlIsSpace(Cursor[Length - 1]))
            Length--;
        free(Document->RefreshUrl);
        Document->RefreshUrl = HtmlStrNDup(Cursor, Length);
    }
}

static void
HtmlStartInput(HTML_PARSER *Parser, const HTML_TAG *Tag)
{
    HTML_ITEM *Item;
    char *Type = HtmlLowerDup(HtmlAttribute(Tag, "type"), "text");
    int Visible = Type && strcmp(Type, "hidden") != 0;
    int Checkable = Type && (strcmp(Type, "checkbox") == 0 || strcmp(Type, "radio") == 0);

    if (!Type)
    {
        Parser->Failed = 1;
        return;
    }
    if (Visible && !Parser->InButton && !Parser->InLink && !Parser->HiddenDepth)
    {
        HtmlFlushText(Parser);
        if (Parser->InLabel && !Checkable)
        {
            char *Text = HtmlTakeCollapsed(&Parser->LabelText);

            if (Text)
            {
                if (!HtmlBufferAppendString(&Parser->Text, Text))
                    Parser->Failed = 1;
                free(Text);
                HtmlFlushText(Parser);
            }
        }
    }
    Item = HtmlAddItem(Parser, HtmlItemInput);
    if (!Item)
    {
        free(Type);
        return;
    }
    Item->Type = Type;
    Item->Name = HtmlStrDup(HtmlAttribute(Tag, "name"));
    Item->Value = HtmlStrDup(HtmlAttribute(Tag, "value"));
    Item->Id = HtmlStrDup(HtmlAttribute(Tag, "id"));
    Item->Placeholder = HtmlStrDup(HtmlAttribute(Tag, "placeholder"));
    Item->Hidden = Parser->HiddenDepth != 0 || HtmlAttribute(Tag, "hidden") != NULL;
    Item->Checked = HtmlAttribute(Tag, "checked") != NULL;
    Item->Disabled = HtmlAttribute(Tag, "disabled") != NULL;
    if (strcmp(Type, "image") == 0)
        Item->Text = HtmlStrDup(HtmlAttribute(Tag, "alt"));
    if (Checkable && Parser->InLabel && Parser->LabelTarget < 0 && !Parser->HiddenDepth)
        Parser->LabelTarget = (long)(Parser->Document->ItemCount - 1);
}

static void
HtmlStartTag(HTML_PARSER *Parser, const HTML_TAG *Tag)
{
    HTML_DOCUMENT *Document = Parser->Document;
    const char *Name = Tag->Name;

    if (strcmp(Name, "head") == 0)
    {
        Parser->InHead = 1;
        return;
    }
    if (strcmp(Name, "body") == 0)
        Parser->InHead = 0;

    if (strcmp(Name, "base") == 0)
    {
        if (!Document->BaseHref && HtmlAttribute(Tag, "href"))
            Document->BaseHref = HtmlStrDup(HtmlAttribute(Tag, "href"));
        return;
    }
    if (strcmp(Name, "meta") == 0)
    {
        if (HtmlEqualNoCase(HtmlAttribute(Tag, "http-equiv"), "refresh") && HtmlAttribute(Tag, "content"))
            HtmlParseRefresh(Document, HtmlAttribute(Tag, "content"));
        return;
    }
    if (strcmp(Name, "img") == 0)
    {
        const char *Alt = HtmlAttribute(Tag, "alt");

        if (Alt && (Parser->InLink || Parser->InButton))
        {
            HtmlAppendInline(Parser, " ");
            HtmlAppendInline(Parser, Alt);
            HtmlAppendInline(Parser, " ");
        }
        return;
    }
    if (strcmp(Name, "br") == 0)
    {
        if (Parser->InLink || Parser->InButton || Parser->InLabel || Parser->InOption)
            HtmlAppendInline(Parser, " ");
        else
            HtmlFlushText(Parser);
        return;
    }
    if (Name[0] == 'h' && Name[1] >= '1' && Name[1] <= '6' && Name[2] == 0)
    {
        HtmlFlushText(Parser);
        Parser->Heading = Name[1] - '0';
        return;
    }
    if (HtmlIsOneOf(Name, BlockTags, sizeof(BlockTags) / sizeof(BlockTags[0])))
    {
        if (!Parser->InLink && !Parser->InButton && !Parser->InLabel)
            HtmlFlushText(Parser);
        else
            HtmlAppendInline(Parser, " ");
        if (strcmp(Name, "li") == 0 && !Parser->InLink && !Parser->InButton && !Parser->InLabel)
            HtmlAppendInline(Parser, "\xE2\x80\xA2 ");
        return;
    }
    if (strcmp(Name, "a") == 0)
    {
        const char *Href = HtmlAttribute(Tag, "href");

        HtmlFinishLink(Parser);
        if (!Href || Parser->InButton)
            return;
        HtmlFlushText(Parser);
        Parser->InLink = 1;
        Parser->LinkInLabel = Parser->InLabel;
        Parser->LinkHref = HtmlStrDup(Href);
        return;
    }
    if (strcmp(Name, "form") == 0)
    {
        HTML_FORM *Forms;

        HtmlFlushText(Parser);
        if (Document->FormCount == Document->FormCapacity)
        {
            size_t Capacity = Document->FormCapacity ? Document->FormCapacity * 2 : 4;

            Forms = realloc(Document->Forms, Capacity * sizeof(*Forms));
            if (!Forms)
            {
                Parser->Failed = 1;
                return;
            }
            Document->Forms = Forms;
            Document->FormCapacity = Capacity;
        }
        Document->Forms[Document->FormCount].Action = HtmlStrDup(HtmlAttribute(Tag, "action"));
        Document->Forms[Document->FormCount].Method = HtmlLowerDup(HtmlAttribute(Tag, "method"), "get");
        Document->Forms[Document->FormCount].Enctype =
            HtmlLowerDup(HtmlAttribute(Tag, "enctype"), "application/x-www-form-urlencoded");
        Parser->CurrentForm = (int)Document->FormCount++;
        return;
    }
    if (strcmp(Name, "input") == 0)
    {
        HtmlStartInput(Parser, Tag);
        return;
    }
    if (strcmp(Name, "button") == 0)
    {
        HTML_ITEM *Item;

        HtmlFinishButton(Parser);
        HtmlFinishLink(Parser);
        HtmlFlushText(Parser);
        Item = HtmlAddItem(Parser, HtmlItemButton);
        if (!Item)
            return;
        Item->Type = HtmlLowerDup(HtmlAttribute(Tag, "type"), "submit");
        Item->Name = HtmlStrDup(HtmlAttribute(Tag, "name"));
        Item->Value = HtmlStrDup(HtmlAttribute(Tag, "value"));
        Item->Id = HtmlStrDup(HtmlAttribute(Tag, "id"));
        Item->Disabled = HtmlAttribute(Tag, "disabled") != NULL;
        Parser->InButton = 1;
        Parser->ButtonItem = Document->ItemCount - 1;
        return;
    }
    if (strcmp(Name, "select") == 0)
    {
        HTML_ITEM *Item;

        HtmlFlushText(Parser);
        Item = HtmlAddItem(Parser, HtmlItemSelect);
        if (!Item)
            return;
        Item->Name = HtmlStrDup(HtmlAttribute(Tag, "name"));
        Item->Id = HtmlStrDup(HtmlAttribute(Tag, "id"));
        Item->Disabled = HtmlAttribute(Tag, "disabled") != NULL;
        Parser->InSelect = 1;
        Parser->SelectItem = Document->ItemCount - 1;
        return;
    }
    if (strcmp(Name, "option") == 0)
    {
        if (!Parser->InSelect)
            return;
        HtmlFinishOption(Parser);
        Parser->InOption = 1;
        Parser->OptionValue = HtmlStrDup(HtmlAttribute(Tag, "value"));
        Parser->OptionSelected = HtmlAttribute(Tag, "selected") != NULL;
        Parser->OptionDisabled = HtmlAttribute(Tag, "disabled") != NULL;
        return;
    }
    if (strcmp(Name, "optgroup") == 0)
    {
        HtmlFinishOption(Parser);
        return;
    }
    if (strcmp(Name, "label") == 0)
    {
        const char *For = HtmlAttribute(Tag, "for");
        size_t Index;

        HtmlFinishLabel(Parser);
        HtmlFlushText(Parser);
        Parser->InLabel = 1;
        Parser->LabelTarget = -1;
        for (Index = 0; For && Index < Document->ItemCount; Index++)
        {
            if (HtmlIsCheckable(&Document->Items[Index]) && Document->Items[Index].Id &&
                strcmp(Document->Items[Index].Id, For) == 0)
            {
                Parser->LabelTarget = (long)Index;
                break;
            }
        }
        return;
    }
}

static void
HtmlEndTag(HTML_PARSER *Parser, const HTML_TAG *Tag)
{
    const char *Name = Tag->Name;

    if (strcmp(Name, "head") == 0)
    {
        Parser->InHead = 0;
        return;
    }
    if (Name[0] == 'h' && Name[1] >= '1' && Name[1] <= '6' && Name[2] == 0)
    {
        HtmlFlushText(Parser);
        Parser->Heading = 0;
        return;
    }
    if (strcmp(Name, "a") == 0)
    {
        HtmlFinishLink(Parser);
        return;
    }
    if (strcmp(Name, "button") == 0)
    {
        HtmlFinishButton(Parser);
        return;
    }
    if (strcmp(Name, "option") == 0 || strcmp(Name, "optgroup") == 0)
    {
        HtmlFinishOption(Parser);
        return;
    }
    if (strcmp(Name, "select") == 0)
    {
        HtmlFinishOption(Parser);
        Parser->InSelect = 0;
        return;
    }
    if (strcmp(Name, "label") == 0)
    {
        HtmlFinishLabel(Parser);
        return;
    }
    if (strcmp(Name, "form") == 0)
    {
        HtmlFinishLabel(Parser);
        HtmlFlushText(Parser);
        Parser->CurrentForm = -1;
        return;
    }
    if (HtmlIsOneOf(Name, BlockTags, sizeof(BlockTags) / sizeof(BlockTags[0])))
    {
        if (!Parser->InLink && !Parser->InButton && !Parser->InLabel)
            HtmlFlushText(Parser);
        else
            HtmlAppendInline(Parser, " ");
    }
}

int
HtmlParse(
    const char *Data,
    size_t Length,
    HTML_DOCUMENT *Document)
{
    HTML_PARSER Parser;
    size_t Index = 0;

    memset(Document, 0, sizeof(*Document));
    memset(&Parser, 0, sizeof(Parser));
    Parser.Document = Document;
    Parser.CurrentForm = -1;
    Parser.LabelTarget = -1;

    if (Length >= 3 && (unsigned char)Data[0] == 0xEF && (unsigned char)Data[1] == 0xBB && (unsigned char)Data[2] == 0xBF)
        Index = 3;

    while (Index < Length && !Parser.Failed)
    {
        HTML_TAG Tag;
        size_t Start, Used;

        if (Data[Index] != '<')
        {
            Start = Index;
            while (Index < Length && Data[Index] != '<')
                Index++;
            HtmlAppendText(&Parser, Data + Start, Index - Start);
            continue;
        }

        if (HtmlStartsWithNoCase(Data + Index, Length - Index, "<!--"))
        {
            const char *End = NULL;
            size_t Search;

            for (Search = Index + 4; Search + 3 <= Length; Search++)
            {
                if (Data[Search] == '-' && Data[Search + 1] == '-' && Data[Search + 2] == '>')
                {
                    End = Data + Search + 3;
                    break;
                }
            }
            Index = End ? (size_t)(End - Data) : Length;
            continue;
        }
        if (Index + 1 < Length && (Data[Index + 1] == '!' || Data[Index + 1] == '?'))
        {
            while (Index < Length && Data[Index] != '>')
                Index++;
            Index = Index < Length ? Index + 1 : Length;
            continue;
        }

        Used = HtmlReadTag(Data + Index, Length - Index, &Tag);
        if (Used == 0)
        {
            HtmlAppendText(&Parser, "<", 1);
            Index++;
            continue;
        }
        Index += Used;

        if (!Tag.Closing && strcmp(Tag.Name, "title") == 0)
        {
            size_t ContentEnd;
            size_t Next = HtmlFindClosing(Data, Length, Index, "title", &ContentEnd);

            if (!Document->Title)
            {
                char *Raw = HtmlDecodeDup(Data + Index, ContentEnd - Index);

                if (Raw)
                {
                    Document->Title = HtmlCollapse(Raw, strlen(Raw));
                    free(Raw);
                }
            }
            Index = Next;
        }
        else if (!Tag.Closing && strcmp(Tag.Name, "textarea") == 0)
        {
            size_t ContentEnd, Next = HtmlFindClosing(Data, Length, Index, "textarea", &ContentEnd);
            size_t ContentStart = Index;
            HTML_ITEM *Item;

            if (ContentStart < ContentEnd && Data[ContentStart] == '\r')
                ContentStart++;
            if (ContentStart < ContentEnd && Data[ContentStart] == '\n')
                ContentStart++;
            HtmlFlushText(&Parser);
            Item = HtmlAddItem(&Parser, HtmlItemTextArea);
            if (Item)
            {
                Item->Hidden = Parser.HiddenDepth != 0 || HtmlAttribute(&Tag, "hidden") != NULL;
                Item->Name = HtmlStrDup(HtmlAttribute(&Tag, "name"));
                Item->Id = HtmlStrDup(HtmlAttribute(&Tag, "id"));
                Item->Placeholder = HtmlStrDup(HtmlAttribute(&Tag, "placeholder"));
                Item->Disabled = HtmlAttribute(&Tag, "disabled") != NULL;
                Item->Value = HtmlDecodeDup(Data + ContentStart, ContentEnd - ContentStart);
            }
            Index = Next;
        }
        else if (!Tag.Closing && HtmlIsOneOf(Tag.Name, SkippedTags, sizeof(SkippedTags) / sizeof(SkippedTags[0])))
        {
            size_t ContentEnd;

            if (strcmp(Tag.Name, "script") == 0)
                Document->HasScript = 1;
            Index = HtmlFindClosing(Data, Length, Index, Tag.Name, &ContentEnd);
        }
        else if (Parser.HiddenDepth)
        {
            if (strcmp(Tag.Name, Parser.HiddenTag) == 0 &&
                !HtmlIsOneOf(Tag.Name, VoidTags, sizeof(VoidTags) / sizeof(VoidTags[0])))
            {
                Parser.HiddenDepth += Tag.Closing ? -1 : 1;
            }
            else if (!Tag.Closing && strcmp(Tag.Name, "input") == 0)
            {
                HtmlStartInput(&Parser, &Tag);
            }
        }
        else if (Tag.Closing)
        {
            HtmlEndTag(&Parser, &Tag);
        }
        else if (HtmlAttribute(&Tag, "hidden") &&
                 !HtmlIsOneOf(Tag.Name, VoidTags, sizeof(VoidTags) / sizeof(VoidTags[0])))
        {
            HtmlFlushText(&Parser);
            Parser.HiddenDepth = 1;
            memcpy(Parser.HiddenTag, Tag.Name, sizeof(Parser.HiddenTag));
        }
        else
        {
            HtmlStartTag(&Parser, &Tag);
        }
        HtmlFreeTag(&Tag);
    }

    HtmlFinishOption(&Parser);
    HtmlFinishButton(&Parser);
    HtmlFinishLink(&Parser);
    HtmlFinishLabel(&Parser);
    HtmlFlushText(&Parser);

    HtmlBufferFree(&Parser.Text);
    HtmlBufferFree(&Parser.LinkText);
    HtmlBufferFree(&Parser.ButtonText);
    HtmlBufferFree(&Parser.OptionText);
    HtmlBufferFree(&Parser.LabelText);
    free(Parser.LinkHref);
    free(Parser.OptionValue);

    if (Parser.Failed)
    {
        HtmlFree(Document);
        return 0;
    }
    return 1;
}

void
HtmlFree(
    HTML_DOCUMENT *Document)
{
    size_t Index, Option;

    for (Index = 0; Index < Document->ItemCount; Index++)
    {
        HTML_ITEM *Item = &Document->Items[Index];

        free(Item->Text);
        free(Item->Href);
        free(Item->Type);
        free(Item->Name);
        free(Item->Value);
        free(Item->Id);
        free(Item->Placeholder);
        for (Option = 0; Option < Item->OptionCount; Option++)
        {
            free(Item->Options[Option].Text);
            free(Item->Options[Option].Value);
        }
        free(Item->Options);
    }
    for (Index = 0; Index < Document->FormCount; Index++)
    {
        free(Document->Forms[Index].Action);
        free(Document->Forms[Index].Method);
        free(Document->Forms[Index].Enctype);
    }
    free(Document->Items);
    free(Document->Forms);
    free(Document->Title);
    free(Document->BaseHref);
    free(Document->RefreshUrl);
    memset(Document, 0, sizeof(*Document));
}

static int
HtmlTypeIs(const HTML_ITEM *Item, const char *Type)
{
    return Item->Type && strcmp(Item->Type, Type) == 0;
}

int
HtmlIsVisibleItem(
    const HTML_ITEM *Item)
{
    if (Item->Hidden)
        return 0;
    if (Item->Kind == HtmlItemInput)
        return !HtmlTypeIs(Item, "hidden") && !HtmlTypeIs(Item, "reset") && !HtmlTypeIs(Item, "file");
    if (Item->Kind == HtmlItemButton)
        return !HtmlTypeIs(Item, "reset");
    return 1;
}

int
HtmlIsSubmitItem(
    const HTML_ITEM *Item)
{
    if (Item->Kind == HtmlItemInput)
        return HtmlTypeIs(Item, "submit") || HtmlTypeIs(Item, "image");
    if (Item->Kind == HtmlItemButton)
        return HtmlTypeIs(Item, "submit");
    return 0;
}

int
HtmlSetValue(
    HTML_ITEM *Item,
    const char *Value)
{
    char *Copy = HtmlStrDup(Value ? Value : "");

    if (!Copy)
        return 0;
    free(Item->Value);
    Item->Value = Copy;
    return 1;
}

int
HtmlFormIsPost(
    const HTML_DOCUMENT *Document,
    size_t Form)
{
    return Form < Document->FormCount && HtmlEqualNoCase(Document->Forms[Form].Method, "post");
}

int
HtmlFormIsMultipart(
    const HTML_DOCUMENT *Document,
    size_t Form)
{
    return HtmlFormIsPost(Document, Form) && HtmlEqualNoCase(Document->Forms[Form].Enctype, "multipart/form-data");
}

static int
HtmlAppendUrlEncoded(HTML_BUFFER *Buffer, const char *Text)
{
    static const char Hex[] = "0123456789ABCDEF";
    const unsigned char *Cursor = (const unsigned char *)Text;

    for (; *Cursor; Cursor++)
    {
        char Escape[3];

        if ((*Cursor >= 'a' && *Cursor <= 'z') || (*Cursor >= 'A' && *Cursor <= 'Z') ||
            (*Cursor >= '0' && *Cursor <= '9') || *Cursor == '*' || *Cursor == '-' || *Cursor == '.' ||
            *Cursor == '_')
        {
            if (!HtmlBufferAppend(Buffer, (const char *)Cursor, 1))
                return 0;
            continue;
        }
        if (*Cursor == ' ')
        {
            if (!HtmlBufferAppend(Buffer, "+", 1))
                return 0;
            continue;
        }
        Escape[0] = '%';
        Escape[1] = Hex[*Cursor >> 4];
        Escape[2] = Hex[*Cursor & 0x0F];
        if (!HtmlBufferAppend(Buffer, Escape, 3))
            return 0;
    }
    return 1;
}

static int
HtmlAppendMultipartName(HTML_BUFFER *Buffer, const char *Text)
{
    for (; *Text; Text++)
    {
        const char *Escaped = NULL;

        if (*Text == '"')
            Escaped = "%22";
        else if (*Text == '\r')
            Escaped = "%0D";
        else if (*Text == '\n')
            Escaped = "%0A";
        if (Escaped ? !HtmlBufferAppendString(Buffer, Escaped) : !HtmlBufferAppend(Buffer, Text, 1))
            return 0;
    }
    return 1;
}

static int
HtmlAppendCrlf(HTML_BUFFER *Buffer, const char *Text)
{
    for (; *Text; Text++)
    {
        if (*Text == '\r' && Text[1] == '\n')
        {
            Text++;
            if (!HtmlBufferAppend(Buffer, "\r\n", 2))
                return 0;
        }
        else if (*Text == '\r' || *Text == '\n')
        {
            if (!HtmlBufferAppend(Buffer, "\r\n", 2))
                return 0;
        }
        else if (!HtmlBufferAppend(Buffer, Text, 1))
        {
            return 0;
        }
    }
    return 1;
}

static int
HtmlAppendPair(HTML_BUFFER *Buffer, const char *Boundary, int *First, const char *Name, const char *Value,
               int Multiline)
{
    if (Boundary)
    {
        HTML_BUFFER Normalized = { 0 };
        int Result;

        if (Multiline)
        {
            if (!HtmlBufferAppendString(&Normalized, "") || !HtmlAppendCrlf(&Normalized, Value))
            {
                HtmlBufferFree(&Normalized);
                return 0;
            }
            Value = Normalized.Data;
        }
        Result = HtmlBufferAppendString(Buffer, "--") && HtmlBufferAppendString(Buffer, Boundary) &&
                 HtmlBufferAppendString(Buffer, "\r\nContent-Disposition: form-data; name=\"") &&
                 HtmlAppendMultipartName(Buffer, Name) && HtmlBufferAppendString(Buffer, "\"\r\n\r\n") &&
                 HtmlBufferAppendString(Buffer, Value) && HtmlBufferAppendString(Buffer, "\r\n");
        HtmlBufferFree(&Normalized);
        return Result;
    }

    if (!*First && !HtmlBufferAppend(Buffer, "&", 1))
        return 0;
    *First = 0;
    if (!HtmlAppendUrlEncoded(Buffer, Name) || !HtmlBufferAppend(Buffer, "=", 1))
        return 0;
    if (Multiline)
    {
        HTML_BUFFER Normalized = { 0 };
        int Result = HtmlBufferAppendString(&Normalized, "") && HtmlAppendCrlf(&Normalized, Value) &&
                     HtmlAppendUrlEncoded(Buffer, Normalized.Data);

        HtmlBufferFree(&Normalized);
        return Result;
    }
    return HtmlAppendUrlEncoded(Buffer, Value);
}

char *
HtmlEncodeForm(
    const HTML_DOCUMENT *Document,
    size_t Form,
    size_t Submitter,
    const char *Boundary,
    size_t *Length)
{
    HTML_BUFFER Buffer = { 0 };
    size_t Index, Option;
    int First = 1, Ok = HtmlBufferAppendString(&Buffer, "");

    for (Index = 0; Ok && Index < Document->ItemCount; Index++)
    {
        const HTML_ITEM *Item = &Document->Items[Index];
        const char *Name = Item->Name ? Item->Name : "";

        if (Item->Form < 0 || (size_t)Item->Form != Form || Item->Disabled)
            continue;

        if (Item->Kind == HtmlItemInput && HtmlTypeIs(Item, "image"))
        {
            char *X, *Y;
            size_t NameLength = strlen(Name);

            if (Index != Submitter)
                continue;
            X = malloc(NameLength + 3);
            Y = malloc(NameLength + 3);
            if (!X || !Y)
            {
                free(X);
                free(Y);
                Ok = 0;
                break;
            }
            memcpy(X, Name, NameLength);
            memcpy(Y, Name, NameLength);
            memcpy(X + NameLength, NameLength ? ".x" : "x", NameLength ? 3 : 2);
            memcpy(Y + NameLength, NameLength ? ".y" : "y", NameLength ? 3 : 2);
            Ok = HtmlAppendPair(&Buffer, Boundary, &First, X, "0", 0) &&
                 HtmlAppendPair(&Buffer, Boundary, &First, Y, "0", 0);
            free(X);
            free(Y);
            continue;
        }

        if (!Item->Name || !Item->Name[0])
            continue;

        switch (Item->Kind)
        {
            case HtmlItemInput:
                if (HtmlTypeIs(Item, "checkbox") || HtmlTypeIs(Item, "radio"))
                {
                    if (Item->Checked)
                        Ok = HtmlAppendPair(&Buffer, Boundary, &First, Name, Item->Value ? Item->Value : "on", 0);
                }
                else if (HtmlTypeIs(Item, "submit") || HtmlTypeIs(Item, "button"))
                {
                    if (Index == Submitter && HtmlTypeIs(Item, "submit"))
                        Ok = HtmlAppendPair(&Buffer, Boundary, &First, Name, Item->Value ? Item->Value : "", 0);
                }
                else if (!HtmlTypeIs(Item, "reset") && !HtmlTypeIs(Item, "file"))
                {
                    Ok = HtmlAppendPair(&Buffer, Boundary, &First, Name, Item->Value ? Item->Value : "", 0);
                }
                break;

            case HtmlItemButton:
                if (Index == Submitter && HtmlTypeIs(Item, "submit"))
                    Ok = HtmlAppendPair(&Buffer, Boundary, &First, Name, Item->Value ? Item->Value : "", 0);
                break;

            case HtmlItemSelect:
            {
                int Any = 0;

                for (Option = 0; Ok && Option < Item->OptionCount; Option++)
                {
                    if (Item->Options[Option].Selected)
                    {
                        Ok = HtmlAppendPair(&Buffer, Boundary, &First, Name, Item->Options[Option].Value, 0);
                        Any = 1;
                    }
                }
                if (Ok && !Any && Item->OptionCount)
                    Ok = HtmlAppendPair(&Buffer, Boundary, &First, Name, Item->Options[0].Value, 0);
                break;
            }

            case HtmlItemTextArea:
                Ok = HtmlAppendPair(&Buffer, Boundary, &First, Name, Item->Value ? Item->Value : "", 1);
                break;

            default:
                break;
        }
    }

    if (Ok && Boundary)
        Ok = HtmlBufferAppendString(&Buffer, "--") && HtmlBufferAppendString(&Buffer, Boundary) &&
             HtmlBufferAppendString(&Buffer, "--\r\n");
    if (!Ok)
    {
        HtmlBufferFree(&Buffer);
        return NULL;
    }
    *Length = Buffer.Length;
    return Buffer.Data;
}

char *
HtmlFindCharset(
    const char *Data,
    size_t Length)
{
    size_t Index, Start;

    if (Length > 4096)
        Length = 4096;
    for (Index = 0; Index + 8 <= Length; Index++)
    {
        if (!HtmlStartsWithNoCase(Data + Index, Length - Index, "charset="))
            continue;
        Index += 8;
        while (Index < Length && (Data[Index] == '"' || Data[Index] == '\'' || HtmlIsSpace(Data[Index])))
            Index++;
        Start = Index;
        while (Index < Length && ((Data[Index] >= 'a' && Data[Index] <= 'z') || (Data[Index] >= 'A' && Data[Index] <= 'Z') ||
                                  (Data[Index] >= '0' && Data[Index] <= '9') || Data[Index] == '-' || Data[Index] == '_'))
        {
            Index++;
        }
        if (Index > Start)
        {
            char *Charset = HtmlStrNDup(Data + Start, Index - Start);
            size_t Char;

            for (Char = 0; Charset && Charset[Char]; Char++)
                Charset[Char] = (char)HtmlLower((unsigned char)Charset[Char]);
            return Charset;
        }
        return NULL;
    }
    return NULL;
}
