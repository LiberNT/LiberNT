/*
 * PROJECT:     LiberNT Driver Database Generator
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Builds the in-box HKLM\SYSTEM\DriverDatabase registry INF from driver INF files
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>

#include <typedefs.h>
#include <infhost.h>

#define DATABASE_KEY "SYSTEM\\DriverDatabase"
#define SIGNERSCORE_INBOX 0x0D000003
#define SPSVCINST_ASSOCSERVICE 0x00000002
#define FLG_ADDREG_BINVALUETYPE 0x00000001
#define FLG_ADDREG_DELVAL 0x00000004
#define FLG_ADDREG_KEYONLY 0x00000010
#define FLG_ADDREG_DELREG_BIT 0x00008000
#define CONFIGFLAG_FINISH_INSTALL 0x00000400
#define MAX_SET_INFS 16

typedef struct _INF_FILE
{
    char *Path;
    char *Name;
    HINF Inf;
} INF_FILE;

typedef struct _INF_SET
{
    HINF Infs[MAX_SET_INFS];
    unsigned Count;
} INF_SET;

typedef struct _DEVICE_ID
{
    char *Id;
    unsigned char Flags;
    unsigned char FeatureScore;
    unsigned short Position;
} DEVICE_ID;

typedef struct _STRING_LIST
{
    char **Items;
    size_t Count;
} STRING_LIST;

static INF_FILE *Files;
static size_t FileCount;
static const char *Arch;
static unsigned ArchNumber;
static FILE *Out;

static void *
Allocate(size_t Size)
{
    void *Memory = calloc(1, Size ? Size : 1);

    if (!Memory)
    {
        fprintf(stderr, "mkdrvdb: out of memory\n");
        exit(1);
    }
    return Memory;
}

static char *
Duplicate(const char *String)
{
    char *Copy = Allocate(strlen(String) + 1);

    strcpy(Copy, String);
    return Copy;
}

static char *
Format(const char *FormatString, ...)
{
    va_list Arguments;
    char *Buffer;
    int Length;

    va_start(Arguments, FormatString);
    Length = vsnprintf(NULL, 0, FormatString, Arguments);
    va_end(Arguments);
    Buffer = Allocate((size_t)Length + 1);
    va_start(Arguments, FormatString);
    vsnprintf(Buffer, (size_t)Length + 1, FormatString, Arguments);
    va_end(Arguments);
    return Buffer;
}

static int
Equal(const char *First, const char *Second)
{
    while (*First && *Second)
    {
        if (tolower((unsigned char)*First) != tolower((unsigned char)*Second))
            return 0;
        First++;
        Second++;
    }
    return *First == *Second;
}

static int
EqualPrefix(const char *String, const char *Prefix, size_t Length)
{
    size_t Index;

    for (Index = 0; Index < Length; Index++)
    {
        if (!String[Index] || tolower((unsigned char)String[Index]) != tolower((unsigned char)Prefix[Index]))
            return 0;
    }
    return 1;
}

static void
Lower(char *String)
{
    for (; *String; String++)
        *String = (char)tolower((unsigned char)*String);
}

static char *
Utf8(const WCHAR *Wide)
{
    size_t Length = 0, Index, Position = 0;
    unsigned long Code;
    char *Buffer;

    while (Wide[Length])
        Length++;
    Buffer = Allocate(Length * 3 + 1);

    for (Index = 0; Index < Length; Index++)
    {
        Code = Wide[Index];
        if (Code >= 0xD800 && Code <= 0xDBFF && Index + 1 < Length &&
            Wide[Index + 1] >= 0xDC00 && Wide[Index + 1] <= 0xDFFF)
        {
            Code = 0x10000 + ((Code - 0xD800) << 10) + (Wide[Index + 1] - 0xDC00);
            Index++;
        }

        if (Code < 0x80)
        {
            Buffer[Position++] = (char)Code;
        }
        else if (Code < 0x800)
        {
            Buffer[Position++] = (char)(0xC0 | (Code >> 6));
            Buffer[Position++] = (char)(0x80 | (Code & 0x3F));
        }
        else if (Code < 0x10000)
        {
            Buffer[Position++] = (char)(0xE0 | (Code >> 12));
            Buffer[Position++] = (char)(0x80 | ((Code >> 6) & 0x3F));
            Buffer[Position++] = (char)(0x80 | (Code & 0x3F));
        }
        else
        {
            Buffer[Position++] = (char)(0xF0 | (Code >> 18));
            Buffer[Position++] = (char)(0x80 | ((Code >> 12) & 0x3F));
            Buffer[Position++] = (char)(0x80 | ((Code >> 6) & 0x3F));
            Buffer[Position++] = (char)(0x80 | (Code & 0x3F));
        }
    }
    Buffer[Position] = 0;
    return Buffer;
}

static WCHAR *
Utf16(const char *String)
{
    size_t Length = strlen(String), Position = 0;
    const unsigned char *Byte = (const unsigned char *)String;
    unsigned long Code;
    WCHAR *Buffer = Allocate((Length + 1) * 2 * sizeof(WCHAR));

    while (*Byte)
    {
        if (*Byte < 0x80)
        {
            Code = *Byte++;
        }
        else if ((*Byte & 0xE0) == 0xC0 && Byte[1])
        {
            Code = ((Byte[0] & 0x1F) << 6) | (Byte[1] & 0x3F);
            Byte += 2;
        }
        else if ((*Byte & 0xF0) == 0xE0 && Byte[1] && Byte[2])
        {
            Code = ((Byte[0] & 0x0F) << 12) | ((Byte[1] & 0x3F) << 6) | (Byte[2] & 0x3F);
            Byte += 3;
        }
        else if ((*Byte & 0xF8) == 0xF0 && Byte[1] && Byte[2] && Byte[3])
        {
            Code = ((unsigned long)(Byte[0] & 0x07) << 18) | ((Byte[1] & 0x3F) << 12) |
                   ((Byte[2] & 0x3F) << 6) | (Byte[3] & 0x3F);
            Byte += 4;
        }
        else
        {
            Code = *Byte++;
        }

        if (Code >= 0x10000)
        {
            Code -= 0x10000;
            Buffer[Position++] = (WCHAR)(0xD800 + (Code >> 10));
            Buffer[Position++] = (WCHAR)(0xDC00 + (Code & 0x3FF));
        }
        else
        {
            Buffer[Position++] = (WCHAR)Code;
        }
    }
    Buffer[Position] = 0;
    return Buffer;
}

static int
FindFirst(HINF Inf, const char *Section, const char *Key, PINFCONTEXT *Context)
{
    WCHAR *SectionW = Utf16(Section), *KeyW = Key ? Utf16(Key) : NULL;
    int Found = InfHostFindFirstLine(Inf, SectionW, KeyW, Context) == 0;

    free(SectionW);
    free(KeyW);
    return Found;
}

static int
FindNextMatch(PINFCONTEXT Context, const char *Key)
{
    WCHAR *KeyW;
    int Found;

    if (InfHostFindNextLine(Context, Context) != 0)
        return 0;

    KeyW = Utf16(Key);
    Found = InfHostFindNextMatchLine(Context, KeyW, Context) == 0;
    free(KeyW);
    return Found;
}

static int
SectionExists(HINF Inf, const char *Section)
{
    WCHAR *SectionW = Utf16(Section);
    LONG Count = InfHostGetLineCount(Inf, SectionW);

    free(SectionW);
    return Count >= 0;
}

static char *
Field(PINFCONTEXT Context, ULONG Index)
{
    WCHAR *Raw = NULL, *Buffer;
    ULONG Required = 0;
    char *Result;

    if (Index > (ULONG)InfHostGetFieldCount(Context) ||
        InfHostGetDataField(Context, Index, &Raw) != 0 || !Raw)
    {
        return NULL;
    }
    if (InfHostGetStringField(Context, Index, NULL, 0, &Required) != 0 || !Required)
        return NULL;

    Buffer = Allocate(Required * sizeof(WCHAR));
    if (InfHostGetStringField(Context, Index, Buffer, Required, NULL) != 0)
    {
        free(Buffer);
        return NULL;
    }
    Result = Utf8(Buffer);
    free(Buffer);
    return Result;
}

static char *
RawField(PINFCONTEXT Context, ULONG Index)
{
    WCHAR *Raw = NULL;

    if (Index > (ULONG)InfHostGetFieldCount(Context) ||
        InfHostGetDataField(Context, Index, &Raw) != 0 || !Raw)
    {
        return NULL;
    }
    return Utf8(Raw);
}

static int
IntField(PINFCONTEXT Context, ULONG Index, long *Value)
{
    char *Text = Field(Context, Index);
    char *End;

    if (!Text || !*Text)
    {
        free(Text);
        return 0;
    }
    *Value = (long)strtoul(Text, &End, 0);
    free(Text);
    return 1;
}

static char *
LineField(HINF Inf, const char *Section, const char *Key, ULONG Index)
{
    PINFCONTEXT Context;
    char *Value = NULL;

    if (FindFirst(Inf, Section, Key, &Context))
    {
        Value = Field(Context, Index);
        InfHostFreeContext(Context);
    }
    return Value;
}

static INF_FILE *
FindFile(const char *Name)
{
    size_t Index;

    for (Index = FileCount; Index-- > 0;)
    {
        if (Equal(Files[Index].Name, Name))
            return &Files[Index];
    }
    return NULL;
}

static void
SetAdd(INF_SET *Set, HINF Inf)
{
    unsigned Index;

    for (Index = 0; Index < Set->Count; Index++)
    {
        if (Set->Infs[Index] == Inf)
            return;
    }
    if (Set->Count < MAX_SET_INFS)
        Set->Infs[Set->Count++] = Inf;
}

static int
SetFindFirst(INF_SET *Set, const char *Section, const char *Key, PINFCONTEXT *Context)
{
    unsigned Index;

    for (Index = 0; Index < Set->Count; Index++)
    {
        if (FindFirst(Set->Infs[Index], Section, Key, Context))
            return 1;
    }
    return 0;
}

static void
ListAdd(STRING_LIST *List, const char *Item)
{
    size_t Index;

    for (Index = 0; Index < List->Count; Index++)
    {
        if (Equal(List->Items[Index], Item))
            return;
    }
    List->Items = realloc(List->Items, (List->Count + 1) * sizeof(char *));
    if (!List->Items)
    {
        fprintf(stderr, "mkdrvdb: out of memory\n");
        exit(1);
    }
    List->Items[List->Count++] = Duplicate(Item);
}

static int
ListContains(STRING_LIST *List, const char *Item)
{
    size_t Index;

    for (Index = 0; Index < List->Count; Index++)
    {
        if (Equal(List->Items[Index], Item))
            return 1;
    }
    return 0;
}

static void
ListFree(STRING_LIST *List)
{
    size_t Index;

    for (Index = 0; Index < List->Count; Index++)
        free(List->Items[Index]);
    free(List->Items);
    List->Items = NULL;
    List->Count = 0;
}

static void
PutString(const char *String)
{
    fputc('"', Out);
    for (; *String; String++)
    {
        if (*String == '"')
            fputs("\"\"", Out);
        else if (*String == '%')
            fputs("%%", Out);
        else
            fputc(*String, Out);
    }
    fputc('"', Out);
}

static void
PutValue(const char *Key, const char *Name, unsigned long Flags)
{
    fputs("HKLM,", Out);
    PutString(Key);
    fputc(',', Out);
    if (Name && *Name)
        PutString(Name);
    fprintf(Out, ",0x%08lX", Flags);
}

static void
PutSz(const char *Key, const char *Name, const char *Value)
{
    PutValue(Key, Name, 0x00000000);
    fputc(',', Out);
    PutString(Value);
    fputc('\n', Out);
}

static void
PutDword(const char *Key, const char *Name, unsigned long Value)
{
    PutValue(Key, Name, 0x00010001);
    fprintf(Out, ",0x%08lX\n", Value);
}

static void
PutBinary(const char *Key, const char *Name, const unsigned char *Data, size_t Size)
{
    size_t Index;

    PutValue(Key, Name, 0x00000001);
    for (Index = 0; Index < Size; Index++)
        fprintf(Out, ",%02x", Data[Index]);
    fputc('\n', Out);
}

static void
PutMultiSz(const char *Key, const char *Name, STRING_LIST *List)
{
    size_t Index;

    PutValue(Key, Name, 0x00010000);
    for (Index = 0; Index < List->Count; Index++)
    {
        fputc(',', Out);
        PutString(List->Items[Index]);
    }
    fputc('\n', Out);
}

static int
DecorationCompatible(const char *Decoration)
{
    const char *Part = Decoration;
    unsigned long Major, Minor;
    char *End;
    size_t Length;

    if (EqualPrefix(Part, "NT", 2))
        Part += 2;
    else if (EqualPrefix(Part, "Win", 3))
        return 0;

    Length = strcspn(Part, ".");
    if (Length && !(Length == strlen(Arch) && EqualPrefix(Part, Arch, Length)))
        return 0;
    Part += Length;

    if (*Part == '.' && Part[1] && Part[1] != '.')
    {
        Major = strtoul(Part + 1, &End, 10);
        if (Major > 10)
            return 0;
        if (*End == '.' && End[1] && End[1] != '.')
        {
            Minor = strtoul(End + 1, &End, 10);
            if (Major == 10 && Minor > 0)
                return 0;
        }
    }
    return 1;
}

static unsigned long long
DecorationRank(const char *Decoration)
{
    const char *Part = strchr(Decoration, '.');
    unsigned long long Rank = 0;
    unsigned Index;

    for (Index = 0; Part && *Part == '.' && Index < 5; Index++)
    {
        unsigned long Value = strtoul(Part + 1, NULL, 10);

        if (Index == 0)
            Rank |= (unsigned long long)(Value & 0xFF) << 48;
        else if (Index == 1)
            Rank |= (unsigned long long)(Value & 0xFF) << 40;
        else if (Index == 4)
            Rank |= Value & 0xFFFFFFFFULL;
        Part = strchr(Part + 1, '.');
    }
    return Rank;
}

static char *
ActualSection(HINF Inf, const char *Base)
{
    char *Candidate;

    Candidate = Format("%s.NT%s", Base, Arch);
    if (SectionExists(Inf, Candidate))
        return Candidate;
    free(Candidate);

    Candidate = Format("%s.NT", Base);
    if (SectionExists(Inf, Candidate))
        return Candidate;
    free(Candidate);

    return SectionExists(Inf, Base) ? Duplicate(Base) : NULL;
}

static void
NormalizeKeyPath(char *Path)
{
    char *Read = Path, *Write = Path;

    while (*Read == '\\')
        Read++;
    while (*Read)
    {
        if (*Read == '\\' && (Read[1] == '\\' || !Read[1]))
        {
            Read++;
            continue;
        }
        *Write++ = *Read++;
    }
    *Write = 0;
}

static int
HasDirectoryId(const char *Text)
{
    const char *Digit;

    for (; (Text = strchr(Text, '%')) != NULL; Text++)
    {
        if (Text[1] == '%')
        {
            Text++;
            continue;
        }
        for (Digit = Text + 1; *Digit >= '0' && *Digit <= '9'; Digit++)
            ;
        if (Digit > Text + 1 && *Digit == '%')
            return 1;
    }
    return 0;
}

static void
EmitAddRegSection(INF_SET *Set, const char *Section, const char *Prefix)
{
    PINFCONTEXT Context;
    char *Root, *SubKey, *Name, *Data, *Key;
    long Flags, Count, Index;
    int Found;

    if (!SetFindFirst(Set, Section, NULL, &Context))
        return;

    for (Found = 1; Found; Found = InfHostFindNextLine(Context, Context) == 0)
    {
        Root = Field(Context, 1);
        if (!Root || !Equal(Root, "HKR"))
        {
            free(Root);
            continue;
        }
        free(Root);

        Count = InfHostGetFieldCount(Context);
        if (!IntField(Context, 4, &Flags))
            Flags = 0;
        if (Flags & (FLG_ADDREG_DELREG_BIT | FLG_ADDREG_DELVAL))
            continue;

        SubKey = Field(Context, 2);
        if (SubKey)
            NormalizeKeyPath(SubKey);
        Key = (SubKey && *SubKey) ? Format("%s\\%s", Prefix, SubKey) : Duplicate(Prefix);
        free(SubKey);

        if (Count < 3)
        {
            PutValue(Key, NULL, FLG_ADDREG_KEYONLY);
            fputc('\n', Out);
            free(Key);
            continue;
        }

        Name = Field(Context, 3);
        PutValue(Key, Name, (unsigned long)Flags);
        for (Index = 5; Index <= Count; Index++)
        {
            Data = Field(Context, (ULONG)Index);
            fputc(',', Out);
            if (Data && (Flags & FLG_ADDREG_BINVALUETYPE))
                fputs(Data, Out);
            else if (Data)
                PutString(Data);
            free(Data);
            Data = RawField(Context, (ULONG)Index);
            if (Data && HasDirectoryId(Data))
                fprintf(stderr, "mkdrvdb: warning: directory id left in [%s] value %s\n", Section, Data);
            free(Data);
        }
        fputc('\n', Out);
        free(Name);
        free(Key);
    }
    InfHostFreeContext(Context);
}

static void
EmitAddRegList(INF_SET *Set, const char *Section, const char *Prefix)
{
    PINFCONTEXT Context;
    long Count, Index;
    char *Name;
    int Found;

    if (!SetFindFirst(Set, Section, "AddReg", &Context))
        return;

    for (Found = 1; Found; Found = FindNextMatch(Context, "AddReg"))
    {
        Count = InfHostGetFieldCount(Context);
        for (Index = 1; Index <= Count; Index++)
        {
            Name = Field(Context, (ULONG)Index);
            if (Name && *Name)
                EmitAddRegSection(Set, Name, Prefix);
            free(Name);
        }
    }
    InfHostFreeContext(Context);
}

static void
AddIncludes(INF_SET *Set, HINF Inf, const char *Section, STRING_LIST *Names)
{
    PINFCONTEXT Context;
    INF_FILE *File;
    long Count, Index;
    char *Name;
    int Found;

    if (!FindFirst(Inf, Section, "Include", &Context))
        return;

    for (Found = 1; Found; Found = FindNextMatch(Context, "Include"))
    {
        Count = InfHostGetFieldCount(Context);
        for (Index = 1; Index <= Count; Index++)
        {
            Name = Field(Context, (ULONG)Index);
            if (Name && *Name)
            {
                if (Names)
                    ListAdd(Names, Name);
                File = FindFile(Name);
                if (File)
                    SetAdd(Set, File->Inf);
            }
            free(Name);
        }
    }
    InfHostFreeContext(Context);
}

static void
EmitRelativeRegistry(INF_SET *Set, const char *Section, const char *Prefix)
{
    PINFCONTEXT Context;
    long Count, Index;
    char *Name;
    int Found;

    AddIncludes(Set, Set->Infs[0], Section, NULL);

    if (SetFindFirst(Set, Section, "Needs", &Context))
    {
        for (Found = 1; Found; Found = FindNextMatch(Context, "Needs"))
        {
            Count = InfHostGetFieldCount(Context);
            for (Index = 1; Index <= Count; Index++)
            {
                Name = Field(Context, (ULONG)Index);
                if (Name && *Name)
                    EmitAddRegList(Set, Name, Prefix);
                free(Name);
            }
        }
        InfHostFreeContext(Context);
    }

    EmitAddRegList(Set, Section, Prefix);
}

static void
EmitFilter(HINF Inf, const char *ConfigurationKey, PINFCONTEXT AddFilter)
{
    char *Name = Field(AddFilter, 1), *Section = Field(AddFilter, 3), *Level = NULL, *Key = NULL;

    if (Name && *Name && Section && *Section)
    {
        Level = LineField(Inf, Section, "FilterLevel", 1);
        if (Level && *Level)
        {
            Key = Format("%s\\Filters\\%s", ConfigurationKey, Level);
        }
        else
        {
            free(Level);
            Level = LineField(Inf, Section, "FilterPosition", 1);
            if (Level && (Equal(Level, "Upper") || Equal(Level, "Lower")))
                Key = Format("%s\\Filters\\*%s", ConfigurationKey, Equal(Level, "Upper") ? "Upper" : "Lower");
        }
    }

    if (Key)
    {
        PutValue(Key, Name, 0x00020001);
        fputc('\n', Out);
        free(Key);
    }
    free(Level);
    free(Section);
    free(Name);
}

static void
EmitConfiguration(INF_FILE *File, const char *PackageKey, const char *Section)
{
    STRING_LIST Included = { NULL, 0 };
    INF_SET Set = { { File->Inf }, 1 };
    PINFCONTEXT Context;
    char *Key, *Path, *Service, *ServiceSection, *Prefix;
    int Found, Associated = 0;
    long Flags;

    Key = Format("%s\\Configurations\\%s", PackageKey, Section);
    PutDword(Key, "ConfigScope", 0xF7F);
    Path = Format("%s.CoInstallers", Section);
    Found = FindFirst(File->Inf, Path, NULL, &Context);
    if (Found)
        InfHostFreeContext(Context);
    PutDword(Key, "ConfigFlags", Found ? CONFIGFLAG_FINISH_INSTALL : 0);
    free(Path);

    AddIncludes(&Set, File->Inf, Section, &Included);
    if (Included.Count)
        PutMultiSz(Key, "IncludedInfs", &Included);
    ListFree(&Included);

    Path = Format("%s.Services", Section);
    if (FindFirst(File->Inf, Path, "AddService", &Context))
    {
        for (Found = 1; Found; Found = FindNextMatch(Context, "AddService"))
        {
            Service = Field(Context, 1);
            if (!Service || !*Service)
            {
                free(Service);
                continue;
            }

            if (!Associated && IntField(Context, 2, &Flags) && (Flags & SPSVCINST_ASSOCSERVICE))
            {
                PutSz(Key, "Service", Service);
                Associated = 1;
            }

            ServiceSection = Field(Context, 3);
            if (ServiceSection && *ServiceSection)
            {
                Prefix = Format("%s\\Services\\%s", Key, Service);
                EmitRelativeRegistry(&Set, ServiceSection, Prefix);
                free(Prefix);
            }
            free(ServiceSection);
            free(Service);
        }
        InfHostFreeContext(Context);
    }
    free(Path);

    Path = Format("%s.Filters", Section);
    if (FindFirst(File->Inf, Path, "AddFilter", &Context))
    {
        for (Found = 1; Found; Found = FindNextMatch(Context, "AddFilter"))
            EmitFilter(File->Inf, Key, Context);
        InfHostFreeContext(Context);
    }
    free(Path);

    Path = Format("%s.HW", Section);
    if (SetFindFirst(&Set, Path, NULL, &Context))
    {
        InfHostFreeContext(Context);
        Prefix = Format("%s\\Device", Key);
        EmitRelativeRegistry(&Set, Path, Prefix);
        free(Prefix);
    }
    free(Path);

    Prefix = Format("%s\\Driver", Key);
    EmitRelativeRegistry(&Set, Section, Prefix);
    free(Prefix);
    free(Key);
}

static unsigned long long
HashFile(const char *Path, int *Ok)
{
    unsigned long long Value = 0xcbf29ce484222325ULL;
    unsigned char Buffer[4096];
    size_t Read, Index;
    FILE *File = fopen(Path, "rb");

    *Ok = File != NULL;
    if (!File)
        return 0;
    while ((Read = fread(Buffer, 1, sizeof(Buffer), File)) != 0)
    {
        for (Index = 0; Index < Read; Index++)
        {
            Value ^= Buffer[Index];
            Value *= 0x100000001b3ULL;
        }
    }
    fclose(File);
    return Value;
}

static long long
DaysFromCivil(long Year, unsigned Month, unsigned Day)
{
    long Era, YearOfEra, DayOfYear, DayOfEra;

    Year -= Month <= 2;
    Era = (Year >= 0 ? Year : Year - 399) / 400;
    YearOfEra = Year - Era * 400;
    DayOfYear = (153 * (Month + (Month > 2 ? -3 : 9)) + 2) / 5 + Day - 1;
    DayOfEra = YearOfEra * 365 + YearOfEra / 4 - YearOfEra / 100 + DayOfYear;
    return (long long)Era * 146097 + DayOfEra;
}

static unsigned long long
ParseDriverDate(const char *Text)
{
    unsigned Month, Day, Year;

    if (!Text || strlen(Text) != 10 ||
        (Text[2] != '-' && Text[2] != '/') || (Text[5] != '-' && Text[5] != '/'))
    {
        return 0;
    }
    Month = (Text[0] - '0') * 10 + (Text[1] - '0');
    Day = (Text[3] - '0') * 10 + (Text[4] - '0');
    Year = (Text[6] - '0') * 1000 + (Text[7] - '0') * 100 + (Text[8] - '0') * 10 + (Text[9] - '0');
    if (Month < 1 || Month > 12 || Day < 1 || Day > 31 || Year < 1601)
        return 0;
    return (unsigned long long)(DaysFromCivil(Year, Month, Day) - DaysFromCivil(1601, 1, 1)) *
           86400ULL * 10000000ULL;
}

static unsigned long long
ParseDriverVersion(const char *Text)
{
    unsigned long Parts[4] = { 0, 0, 0, 0 };
    unsigned Index;

    if (!Text)
        return 0;
    for (Index = 0; Index < 4; Index++)
    {
        Parts[Index] = strtoul(Text, NULL, 10) & 0xFFFF;
        Text = strchr(Text, '.');
        if (!Text)
            break;
        Text++;
    }
    return ((unsigned long long)Parts[0] << 48) | ((unsigned long long)Parts[1] << 32) |
           ((unsigned long long)Parts[2] << 16) | Parts[3];
}

static int
ParseGuid(const char *Text, unsigned char *Guid)
{
    unsigned long Data1;
    unsigned Data2, Data3, Bytes[8];
    unsigned Index;

    if (!Text || sscanf(Text, "{%8lx-%4x-%4x-%2x%2x-%2x%2x%2x%2x%2x%2x}",
                        &Data1, &Data2, &Data3, &Bytes[0], &Bytes[1], &Bytes[2], &Bytes[3],
                        &Bytes[4], &Bytes[5], &Bytes[6], &Bytes[7]) != 11)
    {
        return 0;
    }
    for (Index = 0; Index < 4; Index++)
        Guid[Index] = (unsigned char)(Data1 >> (Index * 8));
    Guid[4] = (unsigned char)Data2;
    Guid[5] = (unsigned char)(Data2 >> 8);
    Guid[6] = (unsigned char)Data3;
    Guid[7] = (unsigned char)(Data3 >> 8);
    for (Index = 0; Index < 8; Index++)
        Guid[8 + Index] = (unsigned char)Bytes[Index];
    return 1;
}

static void
PutLittleEndian(unsigned char *Data, unsigned long long Value, unsigned Size)
{
    unsigned Index;

    for (Index = 0; Index < Size; Index++)
        Data[Index] = (unsigned char)(Value >> (Index * 8));
}

static void
EmitText(const char *DescriptorKey, const char *PackageKey, const char *ValueName,
         PINFCONTEXT Context, STRING_LIST *Tokens)
{
    char *Raw = RawField(Context, 0), *Value, *Token, *Key;
    size_t Length;

    if (!Raw || !*Raw)
    {
        free(Raw);
        return;
    }
    Length = strlen(Raw);
    if (Length > 2 && Raw[0] == '%' && Raw[Length - 1] == '%')
        Lower(Raw);
    PutSz(DescriptorKey, ValueName, Raw);

    if (Length > 2 && Raw[0] == '%' && Raw[Length - 1] == '%')
    {
        Token = Duplicate(Raw + 1);
        Token[Length - 2] = 0;
        if (!ListContains(Tokens, Token))
        {
            Value = Field(Context, 0);
            if (Value)
            {
                Key = Format("%s\\Strings", PackageKey);
                PutSz(Key, Token, Value);
                free(Key);
                ListAdd(Tokens, Token);
            }
            free(Value);
        }
        free(Token);
    }
    free(Raw);
}

static DEVICE_ID *
FindDeviceId(DEVICE_ID *Ids, size_t Count, const char *Id)
{
    size_t Index;

    for (Index = 0; Index < Count; Index++)
    {
        if (Equal(Ids[Index].Id, Id))
            return &Ids[Index];
    }
    return NULL;
}

static unsigned
FeatureScore(HINF Inf, const char *Section)
{
    PINFCONTEXT Context;
    long Value = 0xFF;

    if (FindFirst(Inf, Section, "FeatureScore", &Context))
    {
        if (!IntField(Context, 1, &Value) || Value < 0 || Value > 0xFF)
            Value = 0xFF;
        InfHostFreeContext(Context);
    }
    return (unsigned)Value;
}

static void
ProcessFile(INF_FILE *File)
{
    STRING_LIST Configurations = { NULL, 0 }, Tokens = { NULL, 0 }, Packages = { NULL, 0 };
    PINFCONTEXT Manufacturer, Model;
    DEVICE_ID *Ids = NULL, *Existing;
    size_t IdCount = 0, Index;
    unsigned char Version[48], Guid[16], Data[4];
    unsigned long long Hash;
    char *ClassGuid, *Provider, *Date, *DriverVersion, *PackageId, *PackageKey;
    char *ModelsBase, *Models, *Decoration, *Decorated, *InstallBase, *Install, *Id, *Key;
    unsigned long long BestRank;
    long Count, FieldIndex, DecorationIndex;
    unsigned Score;
    int Ok, Found, ModelFound, Compatible;

    ClassGuid = LineField(File->Inf, "Version", "ClassGUID", 1);
    if (!ParseGuid(ClassGuid, Guid))
    {
        free(ClassGuid);
        return;
    }
    free(ClassGuid);

    Hash = HashFile(File->Path, &Ok);
    if (!Ok)
    {
        fprintf(stderr, "mkdrvdb: cannot read %s\n", File->Path);
        exit(1);
    }

    PackageId = Format("%s_%s_%016llx", File->Name, Arch, Hash);
    Lower(PackageId);
    PackageKey = Format(DATABASE_KEY "\\DriverPackages\\%s", PackageId);

    Provider = LineField(File->Inf, "Version", "Provider", 1);
    Date = LineField(File->Inf, "Version", "DriverVer", 1);
    DriverVersion = LineField(File->Inf, "Version", "DriverVer", 2);

    memset(Version, 0, sizeof(Version));
    Version[0] = 0xFF;
    Version[1] = 0xFF;
    PutLittleEndian(Version + 2, ArchNumber, 2);
    memcpy(Version + 8, Guid, sizeof(Guid));
    PutLittleEndian(Version + 24, ParseDriverDate(Date), 8);
    PutLittleEndian(Version + 32, ParseDriverVersion(DriverVersion), 8);

    PutSz(PackageKey, NULL, File->Name);
    PutBinary(PackageKey, "Version", Version, sizeof(Version));
    if (Provider)
        PutSz(PackageKey, "Provider", Provider);
    PutDword(PackageKey, "SignerScore", SIGNERSCORE_INBOX);
    free(Provider);
    free(Date);
    free(DriverVersion);

    for (Found = FindFirst(File->Inf, "Manufacturer", NULL, &Manufacturer);
         Found;
         Found = InfHostFindNextLine(Manufacturer, Manufacturer) == 0)
    {
        ModelsBase = Field(Manufacturer, 1);
        if (!ModelsBase || !*ModelsBase)
        {
            free(ModelsBase);
            continue;
        }

        Count = InfHostGetFieldCount(Manufacturer);
        Compatible = Count < 2;
        Decorated = NULL;
        BestRank = 0;
        for (DecorationIndex = 2; DecorationIndex <= Count; DecorationIndex++)
        {
            Decoration = Field(Manufacturer, (ULONG)DecorationIndex);
            if (Decoration && DecorationCompatible(Decoration))
            {
                char *Candidate = Format("%s.%s", ModelsBase, Decoration);
                unsigned long long Rank = DecorationRank(Decoration);

                Compatible = 1;
                if (Rank != 0 && SectionExists(File->Inf, Candidate) && (!Decorated || Rank > BestRank))
                {
                    free(Decorated);
                    Decorated = Candidate;
                    BestRank = Rank;
                }
                else
                {
                    free(Candidate);
                }
            }
            free(Decoration);
        }

        Models = Decorated ? Decorated : (Compatible ? ActualSection(File->Inf, ModelsBase) : NULL);
        free(ModelsBase);
        if (!Models)
            continue;

        for (ModelFound = FindFirst(File->Inf, Models, NULL, &Model);
             ModelFound;
             ModelFound = InfHostFindNextLine(Model, Model) == 0)
        {
            InstallBase = Field(Model, 1);
            Install = (InstallBase && *InstallBase) ? ActualSection(File->Inf, InstallBase) : NULL;
            free(InstallBase);
            if (!Install)
                continue;

            if (!ListContains(&Configurations, Install))
            {
                ListAdd(&Configurations, Install);
                EmitConfiguration(File, PackageKey, Install);
            }

            Score = FeatureScore(File->Inf, Install);
            Count = InfHostGetFieldCount(Model);
            for (FieldIndex = 2; FieldIndex <= Count && FieldIndex <= 0x102; FieldIndex++)
            {
                Id = Field(Model, (ULONG)FieldIndex);
                if (!Id || !*Id)
                {
                    free(Id);
                    continue;
                }

                Existing = FindDeviceId(Ids, IdCount, Id);
                if (!Existing)
                {
                    Ids = realloc(Ids, (IdCount + 1) * sizeof(DEVICE_ID));
                    if (!Ids)
                    {
                        fprintf(stderr, "mkdrvdb: out of memory\n");
                        exit(1);
                    }
                    Existing = &Ids[IdCount++];
                    Existing->Id = Duplicate(Id);
                    Existing->Flags = 0;
                    Existing->FeatureScore = (unsigned char)Score;
                    Existing->Position = FieldIndex == 2 ? 0 : (unsigned short)(FieldIndex - 3);

                    Key = Format("%s\\Descriptors\\%s", PackageKey, Id);
                    PutSz(Key, "Configuration", Install);
                    EmitText(Key, PackageKey, "Manufacturer", Manufacturer, &Tokens);
                    EmitText(Key, PackageKey, "Description", Model, &Tokens);
                    free(Key);
                }

                if (FieldIndex == 2)
                {
                    Existing->Flags |= 1;
                }
                else
                {
                    if (!(Existing->Flags & 2) || (unsigned short)(FieldIndex - 3) < Existing->Position)
                        Existing->Position = (unsigned short)(FieldIndex - 3);
                    Existing->Flags |= 2;
                }
                free(Id);
            }
            free(Install);
        }
        free(Models);
    }

    for (Index = 0; Index < IdCount; Index++)
    {
        Data[0] = Ids[Index].Flags;
        Data[1] = Ids[Index].FeatureScore;
        PutLittleEndian(Data + 2, (Ids[Index].Flags & 2) ? Ids[Index].Position : 0, 2);
        Key = Format(DATABASE_KEY "\\DeviceIds\\%s", Ids[Index].Id);
        PutBinary(Key, File->Name, Data, sizeof(Data));
        free(Key);
        free(Ids[Index].Id);
    }
    free(Ids);

    Key = Format(DATABASE_KEY "\\DriverInfFiles\\%s", File->Name);
    ListAdd(&Packages, PackageId);
    PutMultiSz(Key, NULL, &Packages);
    PutSz(Key, "Active", PackageId);
    if (Configurations.Count)
        PutMultiSz(Key, "Configurations", &Configurations);
    free(Key);

    ListFree(&Packages);
    ListFree(&Configurations);
    ListFree(&Tokens);
    free(PackageKey);
    free(PackageId);
}

static unsigned
ArchitectureNumber(const char *Name)
{
    if (Equal(Name, "x86"))
        return 0;
    if (Equal(Name, "ppc"))
        return 3;
    if (Equal(Name, "arm"))
        return 5;
    if (Equal(Name, "ia64"))
        return 6;
    if (Equal(Name, "amd64"))
        return 9;
    if (Equal(Name, "arm64"))
        return 12;
    if (Equal(Name, "riscv64"))
        return 15;
    fprintf(stderr, "mkdrvdb: unknown architecture %s\n", Name);
    exit(1);
}

int
main(int argc, char **argv)
{
    char Line[4096], *Name, *End;
    ULONG ErrorLine;
    FILE *List;
    size_t Index;

    if (argc != 4)
    {
        fprintf(stderr, "usage: mkdrvdb <architecture> <output.inf> <inf-list.txt>\n");
        return 1;
    }

    Arch = argv[1];
    ArchNumber = ArchitectureNumber(Arch);

    List = fopen(argv[3], "r");
    if (!List)
    {
        fprintf(stderr, "mkdrvdb: cannot open %s\n", argv[3]);
        return 1;
    }
    while (fgets(Line, sizeof(Line), List))
    {
        End = Line + strlen(Line);
        while (End > Line && (End[-1] == '\n' || End[-1] == '\r' || End[-1] == ' '))
            *--End = 0;
        if (!Line[0])
            continue;

        Files = realloc(Files, (FileCount + 1) * sizeof(INF_FILE));
        if (!Files)
        {
            fprintf(stderr, "mkdrvdb: out of memory\n");
            return 1;
        }
        Files[FileCount].Path = Duplicate(Line);
        Name = strrchr(Line, '/');
        if (!Name)
            Name = strrchr(Line, '\\');
        Files[FileCount].Name = Duplicate(Name ? Name + 1 : Line);
        if (InfHostOpenFile(&Files[FileCount].Inf, Line, 0, &ErrorLine) != 0)
        {
            fprintf(stderr, "mkdrvdb: cannot parse %s (line %lu)\n", Line, (unsigned long)ErrorLine);
            return 1;
        }
        FileCount++;
    }
    fclose(List);

    Out = fopen(argv[2], "w");
    if (!Out)
    {
        fprintf(stderr, "mkdrvdb: cannot create %s\n", argv[2]);
        return 1;
    }

    fputs("[Version]\nSignature = \"$Windows NT$\"\n\n[AddReg]\n\n", Out);
    PutDword(DATABASE_KEY, "Version", 0x0A000000);
    PutDword(DATABASE_KEY, "SchemaVersion", 0x00010000);
    PutDword(DATABASE_KEY, "ConfigOptions", 0x00000100);
    PutDword(DATABASE_KEY, "Architecture", ArchNumber);
    PutDword(DATABASE_KEY, "SetupStatus", 0);

    for (Index = 0; Index < FileCount; Index++)
    {
        if (FindFile(Files[Index].Name) == &Files[Index])
            ProcessFile(&Files[Index]);
    }

    if (fclose(Out) != 0)
    {
        fprintf(stderr, "mkdrvdb: cannot write %s\n", argv[2]);
        return 1;
    }

    for (Index = 0; Index < FileCount; Index++)
        InfHostCloseFile(Files[Index].Inf);
    return 0;
}
