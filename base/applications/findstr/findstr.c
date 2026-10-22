/*
 * PROJECT:     LiberNT Applications
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Search for strings in files
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <io.h>
#include <fcntl.h>

#include <windef.h>
#include <winbase.h>
#include <wincon.h>
#include <winuser.h>

#include "resource.h"

typedef enum _SEARCH_TOKEN_TYPE
{
    TokenChar,
    TokenAny,
    TokenClass,
    TokenWordStart,
    TokenWordEnd
} SEARCH_TOKEN_TYPE;

typedef struct _SEARCH_TOKEN
{
    SEARCH_TOKEN_TYPE Type;
    BOOL Repeat;
    UCHAR Char;
    UCHAR Class[32];
} SEARCH_TOKEN, *PSEARCH_TOKEN;

typedef struct _SEARCH_PATTERN
{
    PSTR Text;
    SIZE_T Length;
    BOOL LiteralByDefault;
    BOOL Regex;
    BOOL AnchorStart;
    BOOL AnchorEnd;
    PSEARCH_TOKEN Tokens;
    SIZE_T TokenCount;
} SEARCH_PATTERN, *PSEARCH_PATTERN;

typedef struct _LINE_BUFFER
{
    PSTR Data;
    SIZE_T Size;
} LINE_BUFFER, *PLINE_BUFFER;

static BOOL MatchBegin, MatchEnd, ForceLiteral, ForceRegex, Recurse, IgnoreCase;
static BOOL ExactLine, Invert, PrintNumbers, NamesOnly, PrintOffsets, SkipBinary, IncludeOffline;
static BOOL UseNameColor, ShowNames, Found;
static WORD NameColor;
static PSEARCH_PATTERN Patterns;
static SIZE_T PatternCount, PatternCapacity;
static PSTR *Specs;
static SIZE_T SpecCount, SpecCapacity;
static LINE_BUFFER LineBuffer, FoldedBuffer;

static VOID
PrintResource(
    _In_ UINT Id,
    _In_ PCSTR Argument)
{
    CHAR Format[512];

    if (!LoadStringA(GetModuleHandleA(NULL), Id, Format, ARRAYSIZE(Format)))
        return;
    CharToOemA(Format, Format);
    fflush(stdout);
    fprintf(stderr, Format, Argument);
}

static VOID
Usage(VOID)
{
    CHAR Text[4096];

    if (!LoadStringA(GetModuleHandleA(NULL), IDS_USAGE, Text, ARRAYSIZE(Text)))
        return;
    CharToOemA(Text, Text);
    printf("%s\n", Text);
}

static BOOL
GrowBuffer(
    _Inout_ PLINE_BUFFER Buffer,
    _In_ SIZE_T Needed)
{
    SIZE_T Size = Buffer->Size ? Buffer->Size : 256;
    PSTR Data;

    while (Size < Needed)
        Size *= 2;
    if (Size == Buffer->Size)
        return TRUE;

    Data = realloc(Buffer->Data, Size);
    if (!Data)
        return FALSE;
    Buffer->Data = Data;
    Buffer->Size = Size;
    return TRUE;
}

static BOOL
ReadLine(
    _In_ FILE *File,
    _Inout_ PLINE_BUFFER Buffer,
    _Out_ PSIZE_T Length,
    _Out_ PSIZE_T Consumed)
{
    SIZE_T Used = 0;
    int c;

    while ((c = getc(File)) != EOF)
    {
        if (Used + 1 >= Buffer->Size && !GrowBuffer(Buffer, Used + 2))
            return FALSE;
        Buffer->Data[Used++] = (CHAR)c;
        if (c == '\n')
            break;
    }
    if (Used == 0)
        return FALSE;

    *Consumed = Used;
    if (Buffer->Data[Used - 1] == '\n')
        Used--;
    if (Used && Buffer->Data[Used - 1] == '\r')
        Used--;
    *Length = Used;
    return TRUE;
}

static UCHAR
FoldChar(
    _In_ UCHAR Char)
{
    return IgnoreCase ? (UCHAR)tolower(Char) : Char;
}

static BOOL
AddPattern(
    _In_reads_(Length) PCSTR Text,
    _In_ SIZE_T Length,
    _In_ BOOL LiteralByDefault)
{
    PSEARCH_PATTERN Pattern;

    if (Length == 0)
        return TRUE;

    if (PatternCount == PatternCapacity)
    {
        SIZE_T Capacity = PatternCapacity ? PatternCapacity * 2 : 8;
        PSEARCH_PATTERN NewPatterns = realloc(Patterns, Capacity * sizeof(SEARCH_PATTERN));

        if (!NewPatterns)
            return FALSE;
        Patterns = NewPatterns;
        PatternCapacity = Capacity;
    }

    Pattern = &Patterns[PatternCount];
    ZeroMemory(Pattern, sizeof(*Pattern));
    Pattern->Text = malloc(Length + 1);
    if (!Pattern->Text)
        return FALSE;
    memcpy(Pattern->Text, Text, Length);
    Pattern->Text[Length] = '\0';
    Pattern->Length = Length;
    Pattern->LiteralByDefault = LiteralByDefault;
    PatternCount++;
    return TRUE;
}

static BOOL
AddSpaceSeparatedPatterns(
    _In_ PCSTR Text)
{
    PCSTR Start;

    while (*Text)
    {
        while (*Text == ' ')
            Text++;
        Start = Text;
        while (*Text && *Text != ' ')
            Text++;
        if (!AddPattern(Start, (SIZE_T)(Text - Start), FALSE))
            return FALSE;
    }
    return TRUE;
}

static BOOL
AddSpec(
    _In_ PCSTR Spec)
{
    if (SpecCount == SpecCapacity)
    {
        SIZE_T Capacity = SpecCapacity ? SpecCapacity * 2 : 16;
        PSTR *NewSpecs = realloc(Specs, Capacity * sizeof(PSTR));

        if (!NewSpecs)
            return FALSE;
        Specs = NewSpecs;
        SpecCapacity = Capacity;
    }

    Specs[SpecCount] = _strdup(Spec);
    if (!Specs[SpecCount])
        return FALSE;
    SpecCount++;
    return TRUE;
}

static FILE *
OpenListFile(
    _In_ PCSTR Name)
{
    if (strcmp(Name, "/") == 0)
        return stdin;
    return fopen(Name, "rb");
}

static BOOL
ReadStringFile(
    _In_ PCSTR Name)
{
    LINE_BUFFER Buffer = { NULL, 0 };
    SIZE_T Length, Consumed;
    FILE *File;
    BOOL Result = TRUE;

    File = OpenListFile(Name);
    if (!File)
    {
        PrintResource(IDS_CANNOT_OPEN, Name);
        return FALSE;
    }

    while (Result && ReadLine(File, &Buffer, &Length, &Consumed))
        Result = AddPattern(Buffer.Data, Length, FALSE);

    if (File != stdin)
        fclose(File);
    free(Buffer.Data);
    return Result;
}

static BOOL
ReadFileList(
    _In_ PCSTR Name)
{
    LINE_BUFFER Buffer = { NULL, 0 };
    SIZE_T Length, Consumed;
    FILE *File;
    BOOL Result = TRUE;

    File = OpenListFile(Name);
    if (!File)
    {
        PrintResource(IDS_CANNOT_OPEN, Name);
        return FALSE;
    }

    while (Result && ReadLine(File, &Buffer, &Length, &Consumed))
    {
        PSTR Start = Buffer.Data;
        PSTR End = Buffer.Data + Length;

        while (Start < End && (*Start == ' ' || *Start == '\t' || *Start == '"'))
            Start++;
        while (End > Start && (End[-1] == ' ' || End[-1] == '\t' || End[-1] == '"'))
            End--;
        if (End == Start)
            continue;
        *End = '\0';
        Result = AddSpec(Start);
    }

    if (File != stdin)
        fclose(File);
    free(Buffer.Data);
    return Result;
}

static VOID
SetClassBit(
    _Inout_ PSEARCH_TOKEN Token,
    _In_ UINT Char)
{
    Token->Class[Char >> 3] |= (UCHAR)(1 << (Char & 7));
}

static BOOL
TestClassBit(
    _In_ const SEARCH_TOKEN *Token,
    _In_ UINT Char)
{
    return (Token->Class[Char >> 3] & (1 << (Char & 7))) != 0;
}

static PCSTR
ParseClass(
    _In_ PCSTR Text,
    _In_ PCSTR End,
    _Inout_ PSEARCH_TOKEN Token)
{
    BOOL Negate = FALSE;
    BOOL First = TRUE;
    UINT Char;

    Token->Type = TokenClass;
    if (Text < End && *Text == '^')
    {
        Negate = TRUE;
        Text++;
    }

    while (Text < End && (*Text != ']' || First))
    {
        UINT Low, High;

        First = FALSE;
        if (*Text == '\\' && Text + 1 < End)
            Text++;
        Low = (UCHAR)*Text++;
        High = Low;
        if (Text + 1 < End && *Text == '-' && Text[1] != ']')
        {
            Text++;
            if (*Text == '\\' && Text + 1 < End)
                Text++;
            High = (UCHAR)*Text++;
        }
        if (High < Low)
            return NULL;
        for (Char = Low; Char <= High; Char++)
            SetClassBit(Token, Char);
    }
    if (Text >= End)
        return NULL;

    if (IgnoreCase)
    {
        for (Char = 0; Char < 256; Char++)
        {
            if (TestClassBit(Token, Char))
            {
                SetClassBit(Token, (UCHAR)tolower(Char));
                SetClassBit(Token, (UCHAR)toupper(Char));
            }
        }
    }

    if (Negate)
    {
        for (Char = 0; Char < ARRAYSIZE(Token->Class); Char++)
            Token->Class[Char] = (UCHAR)~Token->Class[Char];
    }

    return Text + 1;
}

static BOOL
CompilePattern(
    _Inout_ PSEARCH_PATTERN Pattern)
{
    PCSTR Text = Pattern->Text;
    PCSTR End = Pattern->Text + Pattern->Length;
    SIZE_T Count = 0;
    PSEARCH_TOKEN Token;

    if (!Pattern->Regex)
    {
        SIZE_T Index;

        for (Index = 0; Index < Pattern->Length; Index++)
            Pattern->Text[Index] = (CHAR)FoldChar((UCHAR)Pattern->Text[Index]);
        return TRUE;
    }

    Pattern->Tokens = calloc(Pattern->Length + 1, sizeof(SEARCH_TOKEN));
    if (!Pattern->Tokens)
        return FALSE;

    if (*Text == '^')
    {
        Pattern->AnchorStart = TRUE;
        Text++;
    }

    while (Text < End)
    {
        UCHAR Char = (UCHAR)*Text;

        if (Char == '$' && Text + 1 == End)
        {
            Pattern->AnchorEnd = TRUE;
            Text++;
            continue;
        }

        if (Char == '*' && Count > 0 &&
            !Pattern->Tokens[Count - 1].Repeat &&
            Pattern->Tokens[Count - 1].Type != TokenWordStart &&
            Pattern->Tokens[Count - 1].Type != TokenWordEnd)
        {
            Pattern->Tokens[Count - 1].Repeat = TRUE;
            Text++;
            continue;
        }

        Token = &Pattern->Tokens[Count++];
        if (Char == '.')
        {
            Token->Type = TokenAny;
            Text++;
        }
        else if (Char == '[')
        {
            Text = ParseClass(Text + 1, End, Token);
            if (!Text)
                return FALSE;
        }
        else if (Char == '\\' && Text + 1 < End)
        {
            if (Text[1] == '<')
            {
                Token->Type = TokenWordStart;
            }
            else if (Text[1] == '>')
            {
                Token->Type = TokenWordEnd;
            }
            else
            {
                Token->Type = TokenChar;
                Token->Char = FoldChar((UCHAR)Text[1]);
            }
            Text += 2;
        }
        else
        {
            Token->Type = TokenChar;
            Token->Char = FoldChar(Char);
            Text++;
        }
    }

    Pattern->TokenCount = Count;
    return TRUE;
}

static BOOL
IsWordChar(
    _In_ UCHAR Char)
{
    return isalnum(Char) || Char == '_';
}

static BOOL
TokenMatchesChar(
    _In_ const SEARCH_TOKEN *Token,
    _In_ UCHAR Char)
{
    switch (Token->Type)
    {
        case TokenChar:
            return Char == Token->Char;
        case TokenAny:
            return TRUE;
        case TokenClass:
            return TestClassBit(Token, Char);
        default:
            return FALSE;
    }
}

static BOOL
MatchTokens(
    _In_ const SEARCH_PATTERN *Pattern,
    _In_ SIZE_T Index,
    _In_reads_(Length) PCSTR Line,
    _In_ SIZE_T Length,
    _In_ SIZE_T Position,
    _In_ BOOL AnchorEnd)
{
    const SEARCH_TOKEN *Token;
    SIZE_T Last;

    if (Index == Pattern->TokenCount)
        return !AnchorEnd || Position == Length;

    Token = &Pattern->Tokens[Index];
    if (Token->Type == TokenWordStart)
    {
        if (Position < Length && IsWordChar((UCHAR)Line[Position]) &&
            (Position == 0 || !IsWordChar((UCHAR)Line[Position - 1])))
        {
            return MatchTokens(Pattern, Index + 1, Line, Length, Position, AnchorEnd);
        }
        return FALSE;
    }
    if (Token->Type == TokenWordEnd)
    {
        if (Position > 0 && IsWordChar((UCHAR)Line[Position - 1]) &&
            (Position == Length || !IsWordChar((UCHAR)Line[Position])))
        {
            return MatchTokens(Pattern, Index + 1, Line, Length, Position, AnchorEnd);
        }
        return FALSE;
    }

    if (!Token->Repeat)
    {
        return Position < Length &&
               TokenMatchesChar(Token, (UCHAR)Line[Position]) &&
               MatchTokens(Pattern, Index + 1, Line, Length, Position + 1, AnchorEnd);
    }

    Last = Position;
    while (Last < Length && TokenMatchesChar(Token, (UCHAR)Line[Last]))
        Last++;
    for (;;)
    {
        if (MatchTokens(Pattern, Index + 1, Line, Length, Last, AnchorEnd))
            return TRUE;
        if (Last == Position)
            return FALSE;
        Last--;
    }
}

static BOOL
PatternMatches(
    _In_ const SEARCH_PATTERN *Pattern,
    _In_reads_(Length) PCSTR Line,
    _In_ SIZE_T Length)
{
    BOOL AnchorStart = MatchBegin || ExactLine;
    BOOL AnchorEnd = MatchEnd || ExactLine;
    SIZE_T Position;

    if (!Pattern->Regex)
    {
        if (Pattern->Length > Length)
            return FALSE;
        if (AnchorStart && AnchorEnd)
            return Pattern->Length == Length && memcmp(Line, Pattern->Text, Length) == 0;
        if (AnchorStart)
            return memcmp(Line, Pattern->Text, Pattern->Length) == 0;
        if (AnchorEnd)
            return memcmp(Line + Length - Pattern->Length, Pattern->Text, Pattern->Length) == 0;
        for (Position = 0; Position + Pattern->Length <= Length; Position++)
        {
            if (memcmp(Line + Position, Pattern->Text, Pattern->Length) == 0)
                return TRUE;
        }
        return FALSE;
    }

    AnchorStart = AnchorStart || Pattern->AnchorStart;
    AnchorEnd = AnchorEnd || Pattern->AnchorEnd;
    for (Position = 0; Position <= Length; Position++)
    {
        if (MatchTokens(Pattern, 0, Line, Length, Position, AnchorEnd))
            return TRUE;
        if (AnchorStart)
            break;
    }
    return FALSE;
}

static BOOL
LineMatches(
    _In_reads_(Length) PCSTR Line,
    _In_ SIZE_T Length)
{
    SIZE_T Index;

    if (IgnoreCase)
    {
        if (Length + 1 > FoldedBuffer.Size && !GrowBuffer(&FoldedBuffer, Length + 1))
            return FALSE;
        for (Index = 0; Index < Length; Index++)
            FoldedBuffer.Data[Index] = (CHAR)tolower((UCHAR)Line[Index]);
        Line = FoldedBuffer.Data;
    }

    for (Index = 0; Index < PatternCount; Index++)
    {
        if (PatternMatches(&Patterns[Index], Line, Length))
            return TRUE;
    }
    return FALSE;
}

static VOID
PrintName(
    _In_ PCSTR Name)
{
    HANDLE Output = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO Info;
    WORD Attributes = 0;
    BOOL Colored = FALSE;

    if (UseNameColor && GetConsoleScreenBufferInfo(Output, &Info))
    {
        Attributes = Info.wAttributes;
        fflush(stdout);
        SetConsoleTextAttribute(Output, NameColor);
        Colored = TRUE;
    }

    fputs(Name, stdout);

    if (Colored)
    {
        fflush(stdout);
        SetConsoleTextAttribute(Output, Attributes);
    }
}

static VOID
PrintNumber(
    _In_ ULONGLONG Number)
{
    CHAR Text[32];

    _ui64toa(Number, Text, 10);
    fputs(Text, stdout);
    fputc(':', stdout);
}

static VOID
SearchStream(
    _In_ FILE *File,
    _In_opt_ PCSTR Name)
{
    SIZE_T Length, Consumed;
    ULONGLONG Offset = 0;
    ULONGLONG Number = 0;

    while (ReadLine(File, &LineBuffer, &Length, &Consumed))
    {
        Number++;
        if (LineMatches(LineBuffer.Data, Length) != Invert)
        {
            Found = TRUE;
            if (NamesOnly)
            {
                if (Name)
                {
                    PrintName(Name);
                    fputs("\r\n", stdout);
                }
                return;
            }
            if (ShowNames && Name)
            {
                PrintName(Name);
                fputc(':', stdout);
            }
            if (PrintNumbers)
                PrintNumber(Number);
            if (PrintOffsets)
                PrintNumber(Offset);
            fwrite(LineBuffer.Data, 1, Length, stdout);
            fputs("\r\n", stdout);
        }
        Offset += Consumed;
    }
}

static BOOL
HasNonPrintable(
    _In_ FILE *File)
{
    BOOL Result = FALSE;
    int c;

    while ((c = getc(File)) != EOF)
    {
        if (c < 0x20 && c != '\t' && c != '\n' && c != '\v' && c != '\f' && c != '\r' && c != 0x1A)
        {
            Result = TRUE;
            break;
        }
    }
    rewind(File);
    return Result;
}

static VOID
SearchFile(
    _In_ PCSTR Path)
{
    DWORD Attributes = GetFileAttributesA(Path);
    FILE *File;

    if (Attributes != INVALID_FILE_ATTRIBUTES &&
        (Attributes & FILE_ATTRIBUTE_OFFLINE) && !IncludeOffline)
    {
        return;
    }

    File = fopen(Path, "rb");
    if (!File)
    {
        PrintResource(IDS_CANNOT_OPEN, Path);
        return;
    }

    if (!SkipBinary || !HasNonPrintable(File))
        SearchStream(File, Path);
    fclose(File);
}

static PSTR
JoinPath(
    _In_ PCSTR First,
    _In_ PCSTR Second,
    _In_ PCSTR Third)
{
    SIZE_T Length = strlen(First) + strlen(Second) + strlen(Third) + 1;
    PSTR Path = malloc(Length);

    if (Path)
    {
        strcpy(Path, First);
        strcat(Path, Second);
        strcat(Path, Third);
    }
    return Path;
}

static BOOL
SearchDirectory(
    _In_ PCSTR Directory,
    _In_ PCSTR Mask)
{
    WIN32_FIND_DATAA Data;
    HANDLE Find;
    PSTR Path;
    BOOL Any = FALSE;

    Path = JoinPath(Directory, Mask, "");
    if (!Path)
        return FALSE;
    Find = FindFirstFileA(Path, &Data);
    free(Path);
    if (Find != INVALID_HANDLE_VALUE)
    {
        do
        {
            if (Data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                continue;
            Path = JoinPath(Directory, Data.cFileName, "");
            if (Path)
            {
                SearchFile(Path);
                free(Path);
            }
            Any = TRUE;
        } while (FindNextFileA(Find, &Data));
        FindClose(Find);
    }

    if (!Recurse)
        return Any;

    Path = JoinPath(Directory, "*", "");
    if (!Path)
        return Any;
    Find = FindFirstFileA(Path, &Data);
    free(Path);
    if (Find == INVALID_HANDLE_VALUE)
        return Any;

    do
    {
        if (!(Data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
            (Data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
            strcmp(Data.cFileName, ".") == 0 || strcmp(Data.cFileName, "..") == 0)
        {
            continue;
        }
        Path = JoinPath(Directory, Data.cFileName, "\\");
        if (Path)
        {
            if (SearchDirectory(Path, Mask))
                Any = TRUE;
            free(Path);
        }
    } while (FindNextFileA(Find, &Data));
    FindClose(Find);
    return Any;
}

static VOID
SearchSpec(
    _In_ PCSTR Base,
    _In_ PCSTR Spec)
{
    PCSTR Name = Spec;
    PCSTR Char;
    PSTR Directory;
    SIZE_T BaseLength = strlen(Base);

    for (Char = Spec; *Char; Char++)
    {
        if (*Char == '\\' || *Char == '/' || *Char == ':')
            Name = Char + 1;
    }

    Directory = malloc(BaseLength + (SIZE_T)(Name - Spec) + 1);
    if (!Directory)
        return;
    memcpy(Directory, Base, BaseLength);
    memcpy(Directory + BaseLength, Spec, (SIZE_T)(Name - Spec));
    Directory[BaseLength + (Name - Spec)] = '\0';

    if (!SearchDirectory(Directory, *Name ? Name : "*"))
        PrintResource(IDS_CANNOT_OPEN, Spec);
    free(Directory);
}

static VOID
SearchSpecInDirectories(
    _In_ PCSTR Directories,
    _In_ PCSTR Spec)
{
    PCSTR Start = Directories;

    while (*Start)
    {
        PCSTR End = strchr(Start, ';');
        SIZE_T Length = End ? (SIZE_T)(End - Start) : strlen(Start);

        if (Length)
        {
            PSTR Base = malloc(Length + 2);

            if (Base)
            {
                memcpy(Base, Start, Length);
                Base[Length] = '\0';
                if (Base[Length - 1] != '\\' && Base[Length - 1] != '/' && Base[Length - 1] != ':')
                    strcat(Base, "\\");
                SearchSpec(Base, Spec);
                free(Base);
            }
        }
        if (!End)
            break;
        Start = End + 1;
    }
}

static BOOL
ParseColor(
    _In_ PCSTR Text)
{
    char *End;
    unsigned long Value = strtoul(Text, &End, 16);

    if (End == Text || *End || Value > 0xFF)
        return FALSE;
    NameColor = (WORD)Value;
    UseNameColor = TRUE;
    return TRUE;
}

static BOOL
ParseOption(
    _In_ PCSTR Option,
    _Inout_ PCSTR *StringFile,
    _Inout_ PCSTR *ListFile,
    _Inout_ PCSTR *Directories,
    _Inout_ PBOOL HaveStrings)
{
    if (Option[0] && Option[1] == ':')
    {
        switch (toupper((UCHAR)Option[0]))
        {
            case 'C':
                *HaveStrings = TRUE;
                return AddPattern(Option + 2, strlen(Option + 2), TRUE);
            case 'G':
                *HaveStrings = TRUE;
                *StringFile = Option + 2;
                return TRUE;
            case 'F':
                *ListFile = Option + 2;
                return TRUE;
            case 'D':
                *Directories = Option + 2;
                return TRUE;
            case 'A':
                return ParseColor(Option + 2);
            default:
                return FALSE;
        }
    }

    if (_stricmp(Option, "OFF") == 0 || _stricmp(Option, "OFFLINE") == 0)
    {
        IncludeOffline = TRUE;
        return TRUE;
    }

    if (*Option == '\0')
        return FALSE;

    for (; *Option; Option++)
    {
        switch (toupper((UCHAR)*Option))
        {
            case 'B': MatchBegin = TRUE; break;
            case 'E': MatchEnd = TRUE; break;
            case 'L': ForceLiteral = TRUE; break;
            case 'R': ForceRegex = TRUE; break;
            case 'S': Recurse = TRUE; break;
            case 'I': IgnoreCase = TRUE; break;
            case 'X': ExactLine = TRUE; break;
            case 'V': Invert = TRUE; break;
            case 'N': PrintNumbers = TRUE; break;
            case 'M': NamesOnly = TRUE; break;
            case 'O': PrintOffsets = TRUE; break;
            case 'P': SkipBinary = TRUE; break;
            default: return FALSE;
        }
    }
    return TRUE;
}

int
main(
    int argc,
    char **argv)
{
    PCSTR StringFile = NULL, ListFile = NULL, Directories = NULL;
    BOOL HaveStrings = FALSE, OptionsDone = FALSE, DefaultRegex = FALSE;
    SIZE_T Index;
    int Argument;

    for (Argument = 1; Argument < argc; Argument++)
    {
        PCSTR Text = argv[Argument];

        if (!OptionsDone && (Text[0] == '/' || Text[0] == '-') && Text[1])
        {
            if (strcmp(Text + 1, "?") == 0)
            {
                Usage();
                return 0;
            }
            if (!ParseOption(Text + 1, &StringFile, &ListFile, &Directories, &HaveStrings))
            {
                Usage();
                return 2;
            }
            continue;
        }

        OptionsDone = TRUE;
        if (!HaveStrings)
        {
            if (!AddSpaceSeparatedPatterns(Text))
                return 2;
            HaveStrings = TRUE;
        }
        else if (!AddSpec(Text))
        {
            return 2;
        }
    }

    if (StringFile && !ReadStringFile(StringFile))
        return 2;
    if (ListFile && !ReadFileList(ListFile))
        return 2;

    if (PatternCount == 0)
    {
        Usage();
        return 2;
    }

    for (Index = 0; Index < PatternCount; Index++)
    {
        if (!Patterns[Index].LiteralByDefault)
        {
            DefaultRegex = strpbrk(Patterns[Index].Text, ".*^$[\\") != NULL;
            break;
        }
    }

    for (Index = 0; Index < PatternCount; Index++)
    {
        PSEARCH_PATTERN Pattern = &Patterns[Index];

        if (ForceRegex)
            Pattern->Regex = TRUE;
        else if (ForceLiteral || Pattern->LiteralByDefault)
            Pattern->Regex = FALSE;
        else
            Pattern->Regex = DefaultRegex;

        if (!CompilePattern(Pattern))
        {
            Usage();
            return 2;
        }
    }

    ShowNames = SpecCount > 1 || ListFile || Directories || Recurse;
    for (Index = 0; Index < SpecCount && !ShowNames; Index++)
        ShowNames = strpbrk(Specs[Index], "*?") != NULL;

    _setmode(_fileno(stdout), _O_BINARY);

    if (SpecCount == 0)
    {
        _setmode(_fileno(stdin), _O_BINARY);
        SearchStream(stdin, NULL);
    }
    else
    {
        for (Index = 0; Index < SpecCount; Index++)
        {
            if (Directories)
                SearchSpecInDirectories(Directories, Specs[Index]);
            else
                SearchSpec("", Specs[Index]);
        }
    }

    fflush(stdout);
    return Found ? 0 : 1;
}
