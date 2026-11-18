/*
 * PROJECT:     LiberNT WMI MOF compiler
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     MOF text decoding, lexer and parser
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "mofd_private.h"
#include <wchar.h>
#include <stdlib.h>

typedef enum _MOF_TOKEN_KIND
{
    MofTokenEnd,
    MofTokenIdentifier,
    MofTokenAlias,
    MofTokenString,
    MofTokenChar,
    MofTokenInteger,
    MofTokenReal,
    MofTokenPunct,
    MofTokenPragma
} MOF_TOKEN_KIND;

typedef struct _MOF_TOKEN
{
    MOF_TOKEN_KIND Kind;
    WCHAR *Text;
    ULONGLONG Integer;
    BOOL Negative;
    double Real;
    WCHAR Punct;
    LONG Line;
} MOF_TOKEN;

typedef struct _MOF_PARSER
{
    MOF_UNIT *Unit;
    const WCHAR *Position;
    LONG Line;
    MOF_TOKEN Token;
    HRESULT Error;
    LONG ErrorLine;
    LONG ObjectNumber;
    LONG ObjectFirstLine;
} MOF_PARSER;

#define MOF_ARENA_BLOCK_SIZE 0x10000

void *
MofAlloc(MOF_UNIT *Unit, SIZE_T Size)
{
    MOF_ARENA_BLOCK *Block = Unit->Arena;
    BYTE *Memory;

    if (Size > MAXSIZE_T - MOF_ARENA_BLOCK_SIZE)
        return NULL;
    Size = (Size + 15) & ~(SIZE_T)15;
    if (!Block || Block->Size - Block->Used < Size)
    {
        SIZE_T BlockSize = max(Size + sizeof(*Block) + 16, MOF_ARENA_BLOCK_SIZE);

        Block = HeapAlloc(GetProcessHeap(), 0, BlockSize);
        if (!Block)
            return NULL;
        Block->Next = Unit->Arena;
        Block->Size = BlockSize;
        Block->Used = (sizeof(*Block) + 15) & ~(SIZE_T)15;
        Unit->Arena = Block;
    }
    Memory = (BYTE *)Block + Block->Used;
    Block->Used += Size;
    ZeroMemory(Memory, Size);
    return Memory;
}

void *
MofAllocArray(MOF_UNIT *Unit, SIZE_T Count, SIZE_T ElementSize)
{
    if (ElementSize && Count > MAXSIZE_T / ElementSize)
        return NULL;
    return MofAlloc(Unit, Count * ElementSize);
}

WCHAR *
MofStrDup(MOF_UNIT *Unit, const WCHAR *Text, SIZE_T Length)
{
    WCHAR *Copy = MofAllocArray(Unit, Length + 1, sizeof(WCHAR));

    if (!Copy)
        return NULL;
    memcpy(Copy, Text, Length * sizeof(WCHAR));
    Copy[Length] = 0;
    return Copy;
}

void
MofFreeUnit(MOF_UNIT *Unit)
{
    while (Unit->Arena)
    {
        MOF_ARENA_BLOCK *Next = Unit->Arena->Next;

        HeapFree(GetProcessHeap(), 0, Unit->Arena);
        Unit->Arena = Next;
    }
    ZeroMemory(Unit, sizeof(*Unit));
}

HRESULT
MofDecodeText(const BYTE *Data, SIZE_T Size, BOOL AcceptUnicode, WCHAR **Text)
{
    WCHAR *Buffer;
    int Count = 0;

    *Text = NULL;
    if (AcceptUnicode && Size >= 2 && Data[0] == 0xFF && Data[1] == 0xFE)
    {
        SIZE_T Chars = (Size - 2) / sizeof(WCHAR);

        Buffer = HeapAlloc(GetProcessHeap(), 0, (Chars + 1) * sizeof(WCHAR));
        if (!Buffer)
            return WBEM_E_OUT_OF_MEMORY;
        memcpy(Buffer, Data + 2, Chars * sizeof(WCHAR));
        Buffer[Chars] = 0;
        *Text = Buffer;
        return S_OK;
    }
    /* TODO: binary MOF input, which Windows compiles in CompileFile as well */
    if (Size > MAXLONG)
        return WBEMMOF_E_INVALID_FILE;
    if (Size)
    {
        Count = MultiByteToWideChar(CP_ACP, 0, (const char *)Data, (int)Size, NULL, 0);
        if (Count == 0)
            return WBEMMOF_E_INVALID_FILE;
    }
    if ((SIZE_T)Count >= MAXSIZE_T / sizeof(WCHAR))
        return WBEM_E_OUT_OF_MEMORY;
    Buffer = HeapAlloc(GetProcessHeap(), 0, ((SIZE_T)Count + 1) * sizeof(WCHAR));
    if (!Buffer)
        return WBEM_E_OUT_OF_MEMORY;
    if (Count)
        MultiByteToWideChar(CP_ACP, 0, (const char *)Data, (int)Size, Buffer, Count);
    Buffer[Count] = 0;
    *Text = Buffer;
    return S_OK;
}

static BOOL
MofFail(MOF_PARSER *Parser, HRESULT Error)
{
    if (SUCCEEDED(Parser->Error))
    {
        Parser->Error = Error;
        Parser->ErrorLine = Parser->Token.Line;
    }
    return FALSE;
}

static BOOL
MofIsIdentifierStart(WCHAR Char)
{
    return (Char >= L'a' && Char <= L'z') || (Char >= L'A' && Char <= L'Z') || Char == L'_' || Char >= 0x80;
}

static BOOL
MofIsIdentifierChar(WCHAR Char)
{
    return MofIsIdentifierStart(Char) || (Char >= L'0' && Char <= L'9');
}

static void
MofSkipBlank(MOF_PARSER *Parser)
{
    for (;;)
    {
        WCHAR Char = *Parser->Position;

        if (Char == L'\n')
        {
            Parser->Line++;
            Parser->Position++;
        }
        else if (Char == L' ' || Char == L'\t' || Char == L'\r' || Char == L'\f' || Char == L'\v')
        {
            Parser->Position++;
        }
        else if (Char == L'/' && Parser->Position[1] == L'/')
        {
            while (*Parser->Position && *Parser->Position != L'\n')
                Parser->Position++;
        }
        else if (Char == L'/' && Parser->Position[1] == L'*')
        {
            Parser->Position += 2;
            while (*Parser->Position && !(Parser->Position[0] == L'*' && Parser->Position[1] == L'/'))
            {
                if (*Parser->Position == L'\n')
                    Parser->Line++;
                Parser->Position++;
            }
            if (*Parser->Position)
                Parser->Position += 2;
        }
        else
        {
            return;
        }
    }
}

static int
MofHexValue(WCHAR Char)
{
    if (Char >= L'0' && Char <= L'9')
        return Char - L'0';
    if (Char >= L'a' && Char <= L'f')
        return Char - L'a' + 10;
    if (Char >= L'A' && Char <= L'F')
        return Char - L'A' + 10;
    return -1;
}

static BOOL
MofReadEscape(MOF_PARSER *Parser, WCHAR *Out)
{
    WCHAR Char = *Parser->Position++;

    switch (Char)
    {
    case L'b': *Out = L'\b'; return TRUE;
    case L't': *Out = L'\t'; return TRUE;
    case L'n': *Out = L'\n'; return TRUE;
    case L'f': *Out = L'\f'; return TRUE;
    case L'r': *Out = L'\r'; return TRUE;
    case L'"': *Out = L'"'; return TRUE;
    case L'\'': *Out = L'\''; return TRUE;
    case L'\\': *Out = L'\\'; return TRUE;
    case L'x':
    case L'X':
    {
        ULONG Value = 0;
        int Digits = 0;

        while (Digits < 4 && MofHexValue(*Parser->Position) >= 0)
        {
            Value = Value * 16 + MofHexValue(*Parser->Position++);
            Digits++;
        }
        if (Digits == 0)
            return FALSE;
        *Out = (WCHAR)Value;
        return TRUE;
    }
    default:
        return FALSE;
    }
}

static BOOL
MofReadString(MOF_PARSER *Parser, WCHAR Quote, WCHAR **Text)
{
    const WCHAR *Start = ++Parser->Position;
    SIZE_T Capacity = 0;
    SIZE_T Length = 0;
    WCHAR *Buffer;

    while (*Parser->Position && *Parser->Position != Quote)
    {
        if (*Parser->Position == L'\n')
            return MofFail(Parser, WBEMMOF_E_UNRECOGNIZED_TOKEN);
        if (*Parser->Position == L'\\' && Parser->Position[1])
            Parser->Position++;
        Parser->Position++;
        Capacity++;
    }
    if (*Parser->Position != Quote)
        return MofFail(Parser, WBEMMOF_E_UNRECOGNIZED_TOKEN);
    Buffer = MofAllocArray(Parser->Unit, Capacity + 1, sizeof(WCHAR));
    if (!Buffer)
        return MofFail(Parser, WBEM_E_OUT_OF_MEMORY);
    Parser->Position = Start;
    while (*Parser->Position != Quote)
    {
        if (*Parser->Position == L'\\')
        {
            Parser->Position++;
            if (!MofReadEscape(Parser, &Buffer[Length]))
                return MofFail(Parser, WBEMMOF_E_UNRECOGNIZED_TOKEN);
            Length++;
        }
        else
        {
            Buffer[Length++] = *Parser->Position++;
        }
    }
    Parser->Position++;
    Buffer[Length] = 0;
    *Text = Buffer;
    return TRUE;
}

static BOOL
MofReadNumber(MOF_PARSER *Parser)
{
    const WCHAR *Start = Parser->Position;
    MOF_TOKEN *Token = &Parser->Token;
    BOOL Real = FALSE;

    if (*Parser->Position == L'-' || *Parser->Position == L'+')
    {
        Token->Negative = *Parser->Position == L'-';
        Parser->Position++;
    }
    if (Parser->Position[0] == L'0' && (Parser->Position[1] == L'x' || Parser->Position[1] == L'X'))
    {
        ULONGLONG Value = 0;

        Parser->Position += 2;
        if (MofHexValue(*Parser->Position) < 0)
            return MofFail(Parser, WBEMMOF_E_ILLEGAL_CONSTANT_VALUE);
        while (MofHexValue(*Parser->Position) >= 0)
        {
            if (Value >> 60)
                return MofFail(Parser, WBEMMOF_E_OUT_OF_RANGE);
            Value = Value * 16 + MofHexValue(*Parser->Position++);
        }
        Token->Kind = MofTokenInteger;
        Token->Integer = Value;
        return TRUE;
    }
    while (*Parser->Position >= L'0' && *Parser->Position <= L'9')
        Parser->Position++;
    if (*Parser->Position == L'.' || *Parser->Position == L'e' || *Parser->Position == L'E')
    {
        WCHAR *End;

        Real = TRUE;
        Token->Real = wcstod(Start, &End);
        if (End == Start)
            return MofFail(Parser, WBEMMOF_E_ILLEGAL_CONSTANT_VALUE);
        Parser->Position = End;
        Token->Kind = MofTokenReal;
    }
    if (!Real)
    {
        const WCHAR *Digit = Start + ((*Start == L'-' || *Start == L'+') ? 1 : 0);
        ULONGLONG Value = 0;
        BOOL Octal = Digit[0] == L'0' && Parser->Position - Digit > 1;

        for (; Digit < Parser->Position; Digit++)
        {
            ULONG Base = Octal ? 8 : 10;
            ULONG Number = *Digit - L'0';

            if (Number >= Base)
                return MofFail(Parser, WBEMMOF_E_ILLEGAL_CONSTANT_VALUE);
            if (Value > (~0ULL - Number) / Base)
                return MofFail(Parser, WBEMMOF_E_OUT_OF_RANGE);
            Value = Value * Base + Number;
        }
        Token->Kind = MofTokenInteger;
        Token->Integer = Value;
    }
    if (MofIsIdentifierChar(*Parser->Position))
        return MofFail(Parser, WBEMMOF_E_ILLEGAL_CONSTANT_VALUE);
    return TRUE;
}

static BOOL
MofNext(MOF_PARSER *Parser)
{
    MOF_TOKEN *Token = &Parser->Token;
    WCHAR Char;

    if (FAILED(Parser->Error))
        return FALSE;
    MofSkipBlank(Parser);
    ZeroMemory(Token, sizeof(*Token));
    Token->Line = Parser->Line;
    Char = *Parser->Position;
    if (!Char)
    {
        Token->Kind = MofTokenEnd;
        return TRUE;
    }
    if (Char == L'#')
    {
        const WCHAR *Start = ++Parser->Position;

        while (MofIsIdentifierChar(*Parser->Position))
            Parser->Position++;
        if (Parser->Position - Start != 6 || _wcsnicmp(Start, L"pragma", 6))
            return MofFail(Parser, WBEMMOF_E_INVALID_PRAGMA);
        Token->Kind = MofTokenPragma;
        return TRUE;
    }
    if (Char == L'"')
    {
        Token->Kind = MofTokenString;
        return MofReadString(Parser, L'"', &Token->Text);
    }
    if (Char == L'\'')
    {
        Token->Kind = MofTokenChar;
        if (!MofReadString(Parser, L'\'', &Token->Text))
            return FALSE;
        if (wcslen(Token->Text) != 1)
            return MofFail(Parser, WBEMMOF_E_ILLEGAL_CONSTANT_VALUE);
        return TRUE;
    }
    if ((Char >= L'0' && Char <= L'9') ||
        ((Char == L'-' || Char == L'+') && Parser->Position[1] >= L'0' && Parser->Position[1] <= L'9') ||
        (Char == L'.' && Parser->Position[1] >= L'0' && Parser->Position[1] <= L'9'))
    {
        return MofReadNumber(Parser);
    }
    if (Char == L'$' || MofIsIdentifierStart(Char))
    {
        const WCHAR *Start = Parser->Position;

        if (Char == L'$')
        {
            Parser->Position++;
            if (!MofIsIdentifierStart(*Parser->Position))
                return MofFail(Parser, WBEMMOF_E_EXPECTED_ALIAS_NAME);
            Start = Parser->Position;
            Token->Kind = MofTokenAlias;
        }
        else
        {
            Token->Kind = MofTokenIdentifier;
        }
        while (MofIsIdentifierChar(*Parser->Position))
            Parser->Position++;
        Token->Text = MofStrDup(Parser->Unit, Start, Parser->Position - Start);
        if (!Token->Text)
            return MofFail(Parser, WBEM_E_OUT_OF_MEMORY);
        return TRUE;
    }
    if (wcschr(L"{}[]();,:=", Char))
    {
        Token->Kind = MofTokenPunct;
        Token->Punct = Char;
        Parser->Position++;
        return TRUE;
    }
    return MofFail(Parser, WBEMMOF_E_UNRECOGNIZED_TOKEN);
}

static BOOL
MofIsPunct(MOF_PARSER *Parser, WCHAR Punct)
{
    return Parser->Token.Kind == MofTokenPunct && Parser->Token.Punct == Punct;
}

static BOOL
MofIsKeyword(MOF_PARSER *Parser, const WCHAR *Keyword)
{
    return Parser->Token.Kind == MofTokenIdentifier && !_wcsicmp(Parser->Token.Text, Keyword);
}

static BOOL
MofExpectPunct(MOF_PARSER *Parser, WCHAR Punct, HRESULT Error)
{
    if (!MofIsPunct(Parser, Punct))
        return MofFail(Parser, Error);
    return MofNext(Parser);
}

static BOOL
MofTakeIdentifier(MOF_PARSER *Parser, WCHAR **Name, HRESULT Error)
{
    if (Parser->Token.Kind != MofTokenIdentifier)
        return MofFail(Parser, Error);
    *Name = Parser->Token.Text;
    return MofNext(Parser);
}

static BOOL
MofTakeString(MOF_PARSER *Parser, WCHAR **Text, HRESULT Error)
{
    WCHAR *Joined;

    if (Parser->Token.Kind != MofTokenString)
        return MofFail(Parser, Error);
    Joined = Parser->Token.Text;
    if (!MofNext(Parser))
        return FALSE;
    while (Parser->Token.Kind == MofTokenString)
    {
        SIZE_T Left = wcslen(Joined), Right = wcslen(Parser->Token.Text);
        WCHAR *Both = MofAllocArray(Parser->Unit, Left + Right + 1, sizeof(WCHAR));

        if (!Both)
            return MofFail(Parser, WBEM_E_OUT_OF_MEMORY);
        memcpy(Both, Joined, Left * sizeof(WCHAR));
        memcpy(Both + Left, Parser->Token.Text, (Right + 1) * sizeof(WCHAR));
        Joined = Both;
        if (!MofNext(Parser))
            return FALSE;
    }
    *Text = Joined;
    return TRUE;
}

static BOOL
MofParseConstant(MOF_PARSER *Parser, MOF_VALUE *Value)
{
    MOF_TOKEN *Token = &Parser->Token;

    ZeroMemory(Value, sizeof(*Value));
    switch (Token->Kind)
    {
    case MofTokenString:
        Value->Kind = MofValueString;
        return MofTakeString(Parser, &Value->String, WBEMMOF_E_ILLEGAL_CONSTANT_VALUE);
    case MofTokenChar:
        Value->Kind = MofValueChar;
        Value->Integer = Token->Text[0];
        return MofNext(Parser);
    case MofTokenInteger:
        Value->Kind = MofValueInteger;
        Value->Integer = Token->Integer;
        Value->Negative = Token->Negative;
        return MofNext(Parser);
    case MofTokenReal:
        Value->Kind = MofValueReal;
        Value->Real = Token->Real;
        return MofNext(Parser);
    case MofTokenAlias:
        Value->Kind = MofValueAlias;
        Value->String = Token->Text;
        return MofNext(Parser);
    case MofTokenIdentifier:
        if (!_wcsicmp(Token->Text, L"TRUE") || !_wcsicmp(Token->Text, L"FALSE"))
        {
            Value->Kind = MofValueBoolean;
            Value->Boolean = !_wcsicmp(Token->Text, L"TRUE");
            return MofNext(Parser);
        }
        if (!_wcsicmp(Token->Text, L"NULL"))
        {
            Value->Kind = MofValueNull;
            return MofNext(Parser);
        }
        /* TODO: embedded "instance of" values */
        return MofFail(Parser, WBEMMOF_E_ILLEGAL_CONSTANT_VALUE);
    default:
        return MofFail(Parser, WBEMMOF_E_ILLEGAL_CONSTANT_VALUE);
    }
}

static BOOL
MofParseInitializer(MOF_PARSER *Parser, MOF_VALUE *Value)
{
    MOF_VALUE Items[256];
    MOF_VALUE *Heap = Items;
    ULONG Capacity = ARRAYSIZE(Items);
    ULONG Count = 0;

    if (!MofIsPunct(Parser, L'{'))
        return MofParseConstant(Parser, Value);
    if (!MofNext(Parser))
        return FALSE;
    while (!MofIsPunct(Parser, L'}'))
    {
        if (Count == Capacity)
        {
            MOF_VALUE *Grown = MofAllocArray(Parser->Unit, (SIZE_T)Capacity * 2, sizeof(*Grown));

            if (!Grown)
                return MofFail(Parser, WBEM_E_OUT_OF_MEMORY);
            memcpy(Grown, Heap, Count * sizeof(*Grown));
            Heap = Grown;
            Capacity *= 2;
        }
        if (!MofParseConstant(Parser, &Heap[Count]))
            return FALSE;
        Count++;
        if (MofIsPunct(Parser, L','))
        {
            if (!MofNext(Parser))
                return FALSE;
        }
        else if (!MofIsPunct(Parser, L'}'))
        {
            return MofFail(Parser, WBEMMOF_E_EXPECTED_CLOSE_BRACE);
        }
    }
    ZeroMemory(Value, sizeof(*Value));
    Value->Kind = MofValueArray;
    Value->Count = Count;
    Value->Items = MofAllocArray(Parser->Unit, max(Count, 1), sizeof(*Value->Items));
    if (!Value->Items)
        return MofFail(Parser, WBEM_E_OUT_OF_MEMORY);
    memcpy(Value->Items, Heap, Count * sizeof(*Value->Items));
    return MofNext(Parser);
}

static BOOL
MofParseFlavors(MOF_PARSER *Parser, LONG *Flavor)
{
    *Flavor = 0;
    if (!MofNext(Parser))
        return FALSE;
    if (Parser->Token.Kind != MofTokenIdentifier)
        return MofFail(Parser, WBEMMOF_E_EXPECTED_FLAVOR_TYPE);
    while (Parser->Token.Kind == MofTokenIdentifier)
    {
        if (MofIsKeyword(Parser, L"ToInstance"))
            *Flavor |= WBEM_FLAVOR_FLAG_PROPAGATE_TO_INSTANCE;
        else if (MofIsKeyword(Parser, L"ToSubclass"))
            *Flavor |= WBEM_FLAVOR_FLAG_PROPAGATE_TO_DERIVED_CLASS;
        else if (MofIsKeyword(Parser, L"DisableOverride"))
            *Flavor |= WBEM_FLAVOR_NOT_OVERRIDABLE;
        else if (MofIsKeyword(Parser, L"Amended"))
            *Flavor |= WBEM_FLAVOR_AMENDED;
        else if (!MofIsKeyword(Parser, L"EnableOverride") && !MofIsKeyword(Parser, L"Restricted") &&
                 !MofIsKeyword(Parser, L"NotToInstance") && !MofIsKeyword(Parser, L"NotToSubclass") &&
                 !MofIsKeyword(Parser, L"Translatable"))
        {
            return MofFail(Parser, WBEMMOF_E_EXPECTED_FLAVOR_TYPE);
        }
        if (!MofNext(Parser))
            return FALSE;
    }
    return TRUE;
}

static BOOL
MofParseQualifiers(MOF_PARSER *Parser, MOF_QUALIFIER_LIST *List)
{
    MOF_QUALIFIER Items[64];
    MOF_QUALIFIER *Heap = Items;
    ULONG Capacity = ARRAYSIZE(Items);
    ULONG Count = 0;

    ZeroMemory(List, sizeof(*List));
    if (!MofIsPunct(Parser, L'['))
        return TRUE;
    if (!MofNext(Parser))
        return FALSE;
    for (;;)
    {
        MOF_QUALIFIER *Qualifier;
        ULONG Index;

        if (Count == Capacity)
        {
            MOF_QUALIFIER *Grown = MofAllocArray(Parser->Unit, (SIZE_T)Capacity * 2, sizeof(*Grown));

            if (!Grown)
                return MofFail(Parser, WBEM_E_OUT_OF_MEMORY);
            memcpy(Grown, Heap, Count * sizeof(*Grown));
            Heap = Grown;
            Capacity *= 2;
        }
        Qualifier = &Heap[Count];
        ZeroMemory(Qualifier, sizeof(*Qualifier));
        if (!MofTakeIdentifier(Parser, &Qualifier->Name, WBEMMOF_E_EXPECTED_QUALIFIER_NAME))
            return FALSE;
        for (Index = 0; Index < Count; Index++)
        {
            if (!_wcsicmp(Heap[Index].Name, Qualifier->Name))
                return MofFail(Parser, WBEMMOF_E_DUPLICATE_QUALIFIER);
        }
        if (MofIsPunct(Parser, L'('))
        {
            if (!MofNext(Parser) || !MofParseConstant(Parser, &Qualifier->Value) ||
                !MofExpectPunct(Parser, L')', WBEMMOF_E_EXPECTED_CLOSE_PAREN))
                return FALSE;
        }
        else if (MofIsPunct(Parser, L'{'))
        {
            if (!MofParseInitializer(Parser, &Qualifier->Value))
                return FALSE;
        }
        else
        {
            Qualifier->Value.Kind = MofValueBoolean;
            Qualifier->Value.Boolean = TRUE;
        }
        if (MofIsPunct(Parser, L':') && !MofParseFlavors(Parser, &Qualifier->Flavor))
            return FALSE;
        Count++;
        if (MofIsPunct(Parser, L']'))
            break;
        if (!MofExpectPunct(Parser, L',', WBEMMOF_E_EXPECTED_CLOSE_BRACKET))
            return FALSE;
    }
    List->Count = Count;
    List->Items = MofAllocArray(Parser->Unit, Count, sizeof(*List->Items));
    if (!List->Items)
        return MofFail(Parser, WBEM_E_OUT_OF_MEMORY);
    memcpy(List->Items, Heap, Count * sizeof(*List->Items));
    return MofNext(Parser);
}

static BOOL
MofLookupType(const WCHAR *Name, CIMTYPE *Type)
{
    static const struct { const WCHAR *Name; CIMTYPE Type; } Types[] =
    {
        { L"sint8", CIM_SINT8 }, { L"uint8", CIM_UINT8 }, { L"sint16", CIM_SINT16 }, { L"uint16", CIM_UINT16 },
        { L"sint32", CIM_SINT32 }, { L"uint32", CIM_UINT32 }, { L"sint64", CIM_SINT64 }, { L"uint64", CIM_UINT64 },
        { L"real32", CIM_REAL32 }, { L"real64", CIM_REAL64 }, { L"boolean", CIM_BOOLEAN }, { L"string", CIM_STRING },
        { L"datetime", CIM_DATETIME }, { L"char16", CIM_CHAR16 }, { L"object", CIM_OBJECT }, { L"void", CIM_EMPTY },
    };
    ULONG Index;

    for (Index = 0; Index < ARRAYSIZE(Types); Index++)
    {
        if (!_wcsicmp(Name, Types[Index].Name))
        {
            *Type = Types[Index].Type;
            return TRUE;
        }
    }
    return FALSE;
}

static BOOL
MofParseArraySuffix(MOF_PARSER *Parser, CIMTYPE *Type)
{
    if (!MofIsPunct(Parser, L'['))
        return TRUE;
    if (!MofNext(Parser))
        return FALSE;
    if (Parser->Token.Kind == MofTokenInteger && !MofNext(Parser))
        return FALSE;
    if (!MofExpectPunct(Parser, L']', WBEMMOF_E_EXPECTED_CLOSE_BRACKET))
        return FALSE;
    *Type |= CIM_FLAG_ARRAY;
    return TRUE;
}

static BOOL
MofParseTypedName(MOF_PARSER *Parser, CIMTYPE *Type, WCHAR **ReferenceClass, WCHAR **ObjectClass, WCHAR **Name,
                  HRESULT EndError)
{
    WCHAR *TypeName;

    *ReferenceClass = NULL;
    *ObjectClass = NULL;
    if (Parser->Token.Kind == MofTokenEnd)
        return MofFail(Parser, EndError);
    if (!MofTakeIdentifier(Parser, &TypeName, WBEMMOF_E_EXPECTED_TYPE_IDENTIFIER))
        return FALSE;
    if (MofIsKeyword(Parser, L"ref"))
    {
        *Type = CIM_REFERENCE;
        *ReferenceClass = TypeName;
        if (!MofNext(Parser))
            return FALSE;
    }
    else if (!MofLookupType(TypeName, Type))
    {
        *Type = CIM_OBJECT;
        *ObjectClass = TypeName;
    }
    return MofTakeIdentifier(Parser, Name, WBEMMOF_E_EXPECTED_PROPERTY_NAME);
}

static BOOL
MofParseMethod(MOF_PARSER *Parser, MOF_METHOD *Method)
{
    MOF_PROPERTY Items[64];
    MOF_PROPERTY *Heap = Items;
    ULONG Capacity = ARRAYSIZE(Items);
    ULONG Count = 0;

    if (!MofNext(Parser))
        return FALSE;
    while (!MofIsPunct(Parser, L')'))
    {
        MOF_PROPERTY *Parameter;

        if (Count == Capacity)
        {
            MOF_PROPERTY *Grown = MofAllocArray(Parser->Unit, (SIZE_T)Capacity * 2, sizeof(*Grown));

            if (!Grown)
                return MofFail(Parser, WBEM_E_OUT_OF_MEMORY);
            memcpy(Grown, Heap, Count * sizeof(*Grown));
            Heap = Grown;
            Capacity *= 2;
        }
        Parameter = &Heap[Count];
        ZeroMemory(Parameter, sizeof(*Parameter));
        if (!MofParseQualifiers(Parser, &Parameter->Qualifiers) ||
            !MofParseTypedName(Parser, &Parameter->Type, &Parameter->ReferenceClass, &Parameter->ObjectClass,
                               &Parameter->Name, WBEMMOF_E_EXPECTED_CLOSE_PAREN) ||
            !MofParseArraySuffix(Parser, &Parameter->Type))
            return FALSE;
        if (Parameter->Type == CIM_EMPTY)
            return MofFail(Parser, WBEMMOF_E_EXPECTED_TYPE_IDENTIFIER);
        if (MofIsPunct(Parser, L'='))
        {
            if (!MofNext(Parser) || !MofParseInitializer(Parser, &Parameter->Value))
                return FALSE;
        }
        Count++;
        if (MofIsPunct(Parser, L','))
        {
            if (!MofNext(Parser))
                return FALSE;
        }
        else if (!MofIsPunct(Parser, L')'))
        {
            return MofFail(Parser, WBEMMOF_E_EXPECTED_CLOSE_PAREN);
        }
    }
    Method->ParameterCount = Count;
    Method->Parameters = MofAllocArray(Parser->Unit, max(Count, 1), sizeof(*Method->Parameters));
    if (!Method->Parameters)
        return MofFail(Parser, WBEM_E_OUT_OF_MEMORY);
    memcpy(Method->Parameters, Heap, Count * sizeof(*Method->Parameters));
    return MofNext(Parser);
}

static MOF_OBJECT *
MofAddObject(MOF_PARSER *Parser, MOF_OBJECT_KIND Kind, LONG FirstLine)
{
    MOF_OBJECT *Object = MofAlloc(Parser->Unit, sizeof(*Object));

    if (!Object)
    {
        MofFail(Parser, WBEM_E_OUT_OF_MEMORY);
        return NULL;
    }
    Object->Kind = Kind;
    Object->FirstLine = FirstLine;
    if (Kind != MofObjectPragma)
        Object->Number = ++Parser->Unit->ObjectCount;
    if (Parser->Unit->Last)
        Parser->Unit->Last->Next = Object;
    else
        Parser->Unit->First = Object;
    Parser->Unit->Last = Object;
    return Object;
}

static BOOL
MofParseClass(MOF_PARSER *Parser, MOF_QUALIFIER_LIST *Qualifiers, LONG FirstLine)
{
    MOF_PROPERTY *Properties = NULL;
    MOF_METHOD *Methods = NULL;
    ULONG PropertyCapacity = 0, MethodCapacity = 0;
    MOF_OBJECT *Object;

    Object = MofAddObject(Parser, MofObjectClass, FirstLine);
    if (!Object || !MofNext(Parser))
        return FALSE;
    Object->Qualifiers = *Qualifiers;
    if (!MofTakeIdentifier(Parser, &Object->Name, WBEMMOF_E_EXPECTED_CLASS_NAME))
        return FALSE;
    if (MofIsKeyword(Parser, L"as"))
    {
        if (!MofNext(Parser) || Parser->Token.Kind != MofTokenAlias)
            return MofFail(Parser, WBEMMOF_E_EXPECTED_ALIAS_NAME);
        Object->Alias = Parser->Token.Text;
        if (!MofNext(Parser))
            return FALSE;
    }
    if (MofIsPunct(Parser, L':'))
    {
        if (!MofNext(Parser) || !MofTakeIdentifier(Parser, &Object->Super, WBEMMOF_E_EXPECTED_CLASS_NAME))
            return FALSE;
    }
    if (!MofExpectPunct(Parser, L'{', WBEMMOF_E_EXPECTED_OPEN_BRACE))
        return FALSE;
    while (!MofIsPunct(Parser, L'}'))
    {
        MOF_QUALIFIER_LIST FeatureQualifiers;
        CIMTYPE Type;
        WCHAR *ReferenceClass, *ObjectClass, *Name;

        if (!MofParseQualifiers(Parser, &FeatureQualifiers) ||
            !MofParseTypedName(Parser, &Type, &ReferenceClass, &ObjectClass, &Name,
                               WBEMMOF_E_EXPECTED_BRACE_OR_BAD_TYPE))
            return FALSE;
        if (MofIsPunct(Parser, L'('))
        {
            MOF_METHOD *Method;

            if (Object->MethodCount == MethodCapacity)
            {
                MOF_METHOD *Grown;

                MethodCapacity = MethodCapacity ? MethodCapacity * 2 : 16;
                Grown = MofAllocArray(Parser->Unit, MethodCapacity, sizeof(*Grown));
                if (!Grown)
                    return MofFail(Parser, WBEM_E_OUT_OF_MEMORY);
                if (Methods)
                    memcpy(Grown, Methods, Object->MethodCount * sizeof(*Grown));
                Methods = Grown;
            }
            Method = &Methods[Object->MethodCount];
            Method->Qualifiers = FeatureQualifiers;
            Method->ReturnType = Type;
            Method->Name = Name;
            if (!MofParseMethod(Parser, Method))
                return FALSE;
            Object->MethodCount++;
        }
        else
        {
            MOF_PROPERTY *Property;
            ULONG Index;

            if (Type == CIM_EMPTY)
                return MofFail(Parser, WBEMMOF_E_EXPECTED_TYPE_IDENTIFIER);
            for (Index = 0; Index < Object->PropertyCount; Index++)
            {
                if (!_wcsicmp(Properties[Index].Name, Name))
                    return MofFail(Parser, WBEMMOF_E_DUPLICATE_PROPERTY);
            }
            if (Object->PropertyCount == PropertyCapacity)
            {
                MOF_PROPERTY *Grown;

                PropertyCapacity = PropertyCapacity ? PropertyCapacity * 2 : 16;
                Grown = MofAllocArray(Parser->Unit, PropertyCapacity, sizeof(*Grown));
                if (!Grown)
                    return MofFail(Parser, WBEM_E_OUT_OF_MEMORY);
                if (Properties)
                    memcpy(Grown, Properties, Object->PropertyCount * sizeof(*Grown));
                Properties = Grown;
            }
            Property = &Properties[Object->PropertyCount];
            Property->Qualifiers = FeatureQualifiers;
            Property->Type = Type;
            Property->ReferenceClass = ReferenceClass;
            Property->ObjectClass = ObjectClass;
            Property->Name = Name;
            if (!MofParseArraySuffix(Parser, &Property->Type))
                return FALSE;
            if (MofIsPunct(Parser, L'='))
            {
                if (!MofNext(Parser) || !MofParseInitializer(Parser, &Property->Value))
                    return FALSE;
            }
            Object->PropertyCount++;
        }
        if (!MofExpectPunct(Parser, L';', WBEMMOF_E_EXPECTED_SEMI))
            return FALSE;
    }
    Object->Properties = Properties;
    Object->Methods = Methods;
    Object->LastLine = Parser->Token.Line;
    if (!MofNext(Parser))
        return FALSE;
    return MofExpectPunct(Parser, L';', WBEMMOF_E_EXPECTED_SEMI);
}

static BOOL
MofParseInstance(MOF_PARSER *Parser, MOF_QUALIFIER_LIST *Qualifiers, LONG FirstLine)
{
    MOF_PROPERTY *Values = NULL;
    ULONG Capacity = 0;
    MOF_OBJECT *Object;

    Object = MofAddObject(Parser, MofObjectInstance, FirstLine);
    if (!Object || !MofNext(Parser))
        return FALSE;
    if (!MofIsKeyword(Parser, L"of"))
        return MofFail(Parser, WBEMMOF_E_INVALID_INSTANCE_DECLARATION);
    if (!MofNext(Parser))
        return FALSE;
    Object->Qualifiers = *Qualifiers;
    if (!MofTakeIdentifier(Parser, &Object->Name, WBEMMOF_E_EXPECTED_CLASS_NAME))
        return FALSE;
    if (MofIsKeyword(Parser, L"as"))
    {
        if (!MofNext(Parser) || Parser->Token.Kind != MofTokenAlias)
            return MofFail(Parser, WBEMMOF_E_EXPECTED_ALIAS_NAME);
        Object->Alias = Parser->Token.Text;
        if (!MofNext(Parser))
            return FALSE;
    }
    if (!MofExpectPunct(Parser, L'{', WBEMMOF_E_EXPECTED_OPEN_BRACE))
        return FALSE;
    while (!MofIsPunct(Parser, L'}'))
    {
        MOF_PROPERTY *Value;
        ULONG Index;

        if (Parser->Token.Kind == MofTokenEnd)
            return MofFail(Parser, WBEMMOF_E_EXPECTED_CLOSE_BRACE);
        if (Object->PropertyCount == Capacity)
        {
            MOF_PROPERTY *Grown;

            Capacity = Capacity ? Capacity * 2 : 16;
            Grown = MofAllocArray(Parser->Unit, Capacity, sizeof(*Grown));
            if (!Grown)
                return MofFail(Parser, WBEM_E_OUT_OF_MEMORY);
            if (Values)
                memcpy(Grown, Values, Object->PropertyCount * sizeof(*Grown));
            Values = Grown;
        }
        Value = &Values[Object->PropertyCount];
        ZeroMemory(Value, sizeof(*Value));
        if (!MofParseQualifiers(Parser, &Value->Qualifiers) ||
            !MofTakeIdentifier(Parser, &Value->Name, WBEMMOF_E_EXPECTED_PROPERTY_NAME))
            return FALSE;
        for (Index = 0; Index < Object->PropertyCount; Index++)
        {
            if (!_wcsicmp(Values[Index].Name, Value->Name))
                return MofFail(Parser, WBEMMOF_E_DUPLICATE_PROPERTY);
        }
        if (!MofExpectPunct(Parser, L'=', WBEMMOF_E_INVALID_INSTANCE_DECLARATION) ||
            !MofParseInitializer(Parser, &Value->Value) ||
            !MofExpectPunct(Parser, L';', WBEMMOF_E_EXPECTED_SEMI))
            return FALSE;
        Object->PropertyCount++;
    }
    Object->Properties = Values;
    Object->LastLine = Parser->Token.Line;
    if (!MofNext(Parser))
        return FALSE;
    return MofExpectPunct(Parser, L';', WBEMMOF_E_EXPECTED_SEMI);
}

static BOOL
MofSkipParenthesized(MOF_PARSER *Parser)
{
    if (!MofExpectPunct(Parser, L'(', WBEMMOF_E_EXPECTED_OPEN_PAREN))
        return FALSE;
    while (!MofIsPunct(Parser, L')'))
    {
        if (Parser->Token.Kind == MofTokenEnd)
            return MofFail(Parser, WBEMMOF_E_EXPECTED_CLOSE_PAREN);
        if (!MofNext(Parser))
            return FALSE;
    }
    return MofNext(Parser);
}

/* WMI keeps no qualifier type declarations: they are checked for syntax and dropped */
static BOOL
MofParseQualifierDeclaration(MOF_PARSER *Parser)
{
    CIMTYPE Type;
    WCHAR *Name, *TypeName;
    MOF_VALUE Value;

    if (!MofNext(Parser) || !MofTakeIdentifier(Parser, &Name, WBEMMOF_E_EXPECTED_QUALIFIER_NAME) ||
        !MofExpectPunct(Parser, L':', WBEMMOF_E_INVALID_QUALIFIER_SYNTAX) ||
        !MofTakeIdentifier(Parser, &TypeName, WBEMMOF_E_EXPECTED_TYPE_IDENTIFIER))
        return FALSE;
    if (!MofLookupType(TypeName, &Type) || Type == CIM_EMPTY)
        return MofFail(Parser, WBEMMOF_E_UNRECOGNIZED_TYPE);
    if (!MofParseArraySuffix(Parser, &Type))
        return FALSE;
    if (MofIsPunct(Parser, L'=') && (!MofNext(Parser) || !MofParseInitializer(Parser, &Value)))
        return FALSE;
    while (MofIsPunct(Parser, L','))
    {
        if (!MofNext(Parser))
            return FALSE;
        if (!MofIsKeyword(Parser, L"scope") && !MofIsKeyword(Parser, L"flavor"))
            return MofFail(Parser, WBEMMOF_E_INVALID_QUALIFIER_SYNTAX);
        if (!MofNext(Parser) || !MofSkipParenthesized(Parser))
            return FALSE;
    }
    return MofExpectPunct(Parser, L';', WBEMMOF_E_EXPECTED_SEMI);
}

HRESULT
MofParseFlags(const WCHAR *Text, BOOL Instance, LONG *Flags)
{
    const WCHAR *Position = Text;

    *Flags = 0;
    while (*Position)
    {
        const WCHAR *Start;
        SIZE_T Length;

        while (*Position == L' ' || *Position == L'\t' || *Position == L',')
            Position++;
        Start = Position;
        while (*Position && *Position != L' ' && *Position != L'\t' && *Position != L',')
            Position++;
        Length = Position - Start;
        if (Length == 0)
            break;
        if (Length == 10 && !_wcsnicmp(Start, L"updateonly", Length))
            *Flags |= WBEM_FLAG_UPDATE_ONLY;
        else if (Length == 10 && !_wcsnicmp(Start, L"createonly", Length))
            *Flags |= WBEM_FLAG_CREATE_ONLY;
        else if (!Instance && Length == 10 && !_wcsnicmp(Start, L"safeupdate", Length))
            *Flags |= WBEM_FLAG_UPDATE_SAFE_MODE;
        else if (!Instance && Length == 11 && !_wcsnicmp(Start, L"forceupdate", Length))
            *Flags |= WBEM_FLAG_UPDATE_FORCE_MODE;
        else
            return WBEMMOF_E_INVALID_FLAGS_SYNTAX;
    }
    return S_OK;
}

static BOOL
MofParsePragma(MOF_PARSER *Parser)
{
    MOF_OBJECT *Object;
    WCHAR *Name;

    Object = MofAddObject(Parser, MofObjectPragma, Parser->Token.Line);
    if (!Object || !MofNext(Parser))
        return FALSE;
    if (!MofTakeIdentifier(Parser, &Name, WBEMMOF_E_INVALID_PRAGMA))
        return FALSE;
    Object->Name = Name;
    if (!_wcsicmp(Name, L"autorecover"))
    {
        Object->Pragma = MofPragmaAutoRecover;
        Parser->Unit->HasAutoRecover = TRUE;
        return TRUE;
    }
    if (!_wcsicmp(Name, L"namespace"))
        Object->Pragma = MofPragmaNamespace;
    else if (!_wcsicmp(Name, L"deleteinstance"))
        Object->Pragma = MofPragmaDeleteInstance;
    else if (!_wcsicmp(Name, L"deleteclass"))
        Object->Pragma = MofPragmaDeleteClass;
    else if (!_wcsicmp(Name, L"classflags"))
        Object->Pragma = MofPragmaClassFlags;
    else if (!_wcsicmp(Name, L"instanceflags"))
        Object->Pragma = MofPragmaInstanceFlags;
    else if (!_wcsicmp(Name, L"locale") || !_wcsicmp(Name, L"instancelocale") || !_wcsicmp(Name, L"amendment") ||
             !_wcsicmp(Name, L"nonlocal") || !_wcsicmp(Name, L"nonlocaltype") || !_wcsicmp(Name, L"source") ||
             !_wcsicmp(Name, L"sourcetype"))
        Object->Pragma = MofPragmaOther;
    else
        /* TODO: #pragma include, refused for now like an include whose file cannot be read */
        return MofFail(Parser, WBEMMOF_E_INVALID_PRAGMA);
    if (!MofExpectPunct(Parser, L'(', WBEMMOF_E_EXPECTED_OPEN_PAREN))
        return FALSE;
    if (Parser->Token.Kind == MofTokenString)
    {
        if (!MofTakeString(Parser, &Object->PragmaArgument, WBEMMOF_E_INVALID_PRAGMA))
            return FALSE;
    }
    else if (Parser->Token.Kind == MofTokenIdentifier)
    {
        Object->PragmaArgument = Parser->Token.Text;
        if (!MofNext(Parser))
            return FALSE;
    }
    else if (Object->Pragma != MofPragmaOther)
    {
        return MofFail(Parser, WBEMMOF_E_INVALID_PRAGMA);
    }
    if (MofIsPunct(Parser, L','))
    {
        if (!MofNext(Parser))
            return FALSE;
        if (MofIsKeyword(Parser, L"FAIL"))
            Object->PragmaFail = TRUE;
        else if (!MofIsKeyword(Parser, L"NOFAIL"))
            return MofFail(Parser, Object->Pragma == MofPragmaDeleteClass ? WBEMMOF_E_INVALID_DELETECLASS_SYNTAX :
                                                                             WBEMMOF_E_INVALID_DELETEINSTANCE_SYNTAX);
        if (!MofNext(Parser))
            return FALSE;
    }
    else if (Object->Pragma == MofPragmaDeleteInstance || Object->Pragma == MofPragmaDeleteClass)
    {
        return MofFail(Parser, Object->Pragma == MofPragmaDeleteClass ? WBEMMOF_E_INVALID_DELETECLASS_SYNTAX :
                                                                         WBEMMOF_E_INVALID_DELETEINSTANCE_SYNTAX);
    }
    if (Object->Pragma == MofPragmaClassFlags || Object->Pragma == MofPragmaInstanceFlags)
    {
        HRESULT Result = MofParseFlags(Object->PragmaArgument ? Object->PragmaArgument : L"",
                                       Object->Pragma == MofPragmaInstanceFlags, &Object->PragmaFlags);

        if (FAILED(Result))
            return MofFail(Parser, Result);
    }
    /* TODO: #pragma locale, instancelocale, amendment, nonlocal, nonlocaltype, source and sourcetype are not applied */
    return MofExpectPunct(Parser, L')', WBEMMOF_E_EXPECTED_CLOSE_PAREN);
}

HRESULT
MofParse(MOF_UNIT *Unit, const WCHAR *Text, WBEM_COMPILE_STATUS_INFO *Info)
{
    MOF_PARSER Parser;

    ZeroMemory(&Parser, sizeof(Parser));
    Parser.Unit = Unit;
    Parser.Position = Text;
    Parser.Line = 1;
    Parser.Error = S_OK;
    if (MofNext(&Parser))
    {
        while (Parser.Token.Kind != MofTokenEnd)
        {
            MOF_QUALIFIER_LIST Qualifiers;
            LONG FirstLine = Parser.Token.Line;

            if (Parser.Token.Kind == MofTokenPragma)
            {
                if (!MofParsePragma(&Parser))
                    break;
                continue;
            }
            if (!MofParseQualifiers(&Parser, &Qualifiers))
                break;
            if (MofIsKeyword(&Parser, L"class"))
            {
                if (!MofParseClass(&Parser, &Qualifiers, FirstLine))
                    break;
            }
            else if (MofIsKeyword(&Parser, L"instance"))
            {
                if (!MofParseInstance(&Parser, &Qualifiers, FirstLine))
                    break;
            }
            else if (MofIsKeyword(&Parser, L"qualifier") && !Qualifiers.Count)
            {
                if (!MofParseQualifierDeclaration(&Parser))
                    break;
            }
            else
            {
                MofFail(&Parser, MofIsKeyword(&Parser, L"typedef") ? WBEMMOF_E_TYPEDEF_NOT_SUPPORTED :
                                                                     WBEMMOF_E_UNRECOGNIZED_TOKEN);
                break;
            }
        }
    }
    if (FAILED(Parser.Error))
    {
        Info->lPhaseError = 2;
        Info->hRes = Parser.Error;
    }
    return Parser.Error;
}
