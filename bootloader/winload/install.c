/*
 * PROJECT:     LiberNT Boot Loader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Installs a staged LiberNT image over the operating system volume
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "winload.h"
#include <ntfsbl.h>

#define ISO_BLOCK_SIZE        2048
#define ISO_DESCRIPTOR_BLOCK  16
#define ISO_PRIMARY           1
#define ISO_ROOT_RECORD       156
#define ISO_RECORD_EXTENT     2
#define ISO_RECORD_LENGTH     10
#define ISO_RECORD_FLAGS      25
#define ISO_RECORD_NAME_SIZE  32
#define ISO_RECORD_NAME       33
#define ISO_FLAG_DIRECTORY    0x02
#define ISO_DIRECTORY_MAX     (16 * 1024 * 1024)
#define ISO_DEPTH_MAX         32

#define OSL_INSTALL_PATH_CHARS 520
#define OSL_INSTALL_NAME_CHARS 256
#define OSL_INSTALL_BUCKETS    8192
#define OSL_INSTALL_TREES      8
#define OSL_INSTALL_COMPUTER   32
#define OSL_INSTALL_FIELDS     3
#define OSL_INSTALL_INF_MAX    (4 * 1024 * 1024)
#define OSL_INSTALL_INVENTORY  "servicing\\LiberNT.files"
#define OSL_INSTALL_SETUP_INF  "txtsetup.sif"
#define OSL_INSTALL_IMAGE_LINE "; Image "

typedef struct _OSL_NAME
{
    struct _OSL_NAME* Next;
    ULONG Block;
    ULONG Size;
    BOOLEAN Directory;
    CHAR Path[ANYSIZE_ARRAY];
} OSL_NAME, *POSL_NAME;

typedef struct _OSL_NAME_SET
{
    POSL_NAME Buckets[OSL_INSTALL_BUCKETS];
    ULONG Count;
} OSL_NAME_SET, *POSL_NAME_SET;

typedef struct _OSL_TREE
{
    CHAR Media[OSL_INSTALL_NAME_CHARS];
    CHAR Target[OSL_INSTALL_PATH_CHARS];
} OSL_TREE, *POSL_TREE;

typedef struct _OSL_COMPUTER_FILE
{
    CHAR Source[OSL_INSTALL_PATH_CHARS];
    CHAR Target[OSL_INSTALL_PATH_CHARS];
    CHAR Media[OSL_INSTALL_PATH_CHARS];
    BOOLEAN Installed;
} OSL_COMPUTER_FILE, *POSL_COMPUTER_FILE;

typedef struct _OSL_INSTALL
{
    PVOID Image;
    CHAR SystemRoot[OSL_INSTALL_PATH_CHARS];
    CHAR InventoryEntry[OSL_INSTALL_PATH_CHARS];
    CHAR InventoryPath[OSL_INSTALL_PATH_CHARS];
    PSTR SetupText;
    PSTR NewText;
    PSTR OldText;
    ULONG SetupSize;
    ULONG NewSize;
    ULONG OldSize;
    OSL_NAME_SET Files;
    OSL_NAME_SET Inventory;
    OSL_NAME_SET Exclude;
    OSL_NAME_SET Preserve;
    OSL_NAME_SET Computers;
    OSL_TREE Trees[OSL_INSTALL_TREES];
    ULONG TreeCount;
    ULONG SystemTree;
    OSL_COMPUTER_FILE ComputerFiles[OSL_INSTALL_COMPUTER];
    ULONG ComputerCount;
    ULONGLONG TotalBytes;
    ULONGLONG DoneBytes;
    ULONG Percent;
    ULONG Written;
    ULONG Removed;
    ULONG Failed;
} OSL_INSTALL, *POSL_INSTALL;

static
ULONG
OslNameHash(
    _In_z_ PCSTR Path)
{
    ULONG Hash = 2166136261u;
    UCHAR Char;

    while (*Path)
    {
        Char = (UCHAR)*Path++;
        if (Char == '/')
            Char = '\\';
        Hash = (Hash ^ (UCHAR)toupper(Char)) * 16777619u;
    }
    return Hash & (OSL_INSTALL_BUCKETS - 1);
}

static
BOOLEAN
OslSamePath(
    _In_z_ PCSTR First,
    _In_z_ PCSTR Second)
{
    UCHAR A, B;

    do
    {
        A = (UCHAR)*First++;
        B = (UCHAR)*Second++;
        if (A == '/')
            A = '\\';
        if (B == '/')
            B = '\\';
        if (toupper(A) != toupper(B))
            return FALSE;
    } while (A != ANSI_NULL);

    return TRUE;
}

static
POSL_NAME
OslFindName(
    _In_ POSL_NAME_SET Set,
    _In_z_ PCSTR Path)
{
    POSL_NAME Name;

    for (Name = Set->Buckets[OslNameHash(Path)]; Name; Name = Name->Next)
    {
        if (OslSamePath(Name->Path, Path))
            return Name;
    }
    return NULL;
}

static
POSL_NAME
OslAddName(
    _Inout_ POSL_NAME_SET Set,
    _In_z_ PCSTR Path)
{
    SIZE_T Length = strlen(Path);
    ULONG Bucket = OslNameHash(Path);
    POSL_NAME Name;

    Name = OslFindName(Set, Path);
    if (Name)
        return Name;

    Name = FwAllocate(FIELD_OFFSET(OSL_NAME, Path) + Length + 1);
    if (!Name)
        return NULL;

    RtlZeroMemory(Name, FIELD_OFFSET(OSL_NAME, Path));
    RtlCopyMemory(Name->Path, Path, Length + 1);
    Name->Next = Set->Buckets[Bucket];
    Set->Buckets[Bucket] = Name;
    Set->Count++;
    return Name;
}

static
VOID
OslFreeNames(
    _Inout_ POSL_NAME_SET Set)
{
    POSL_NAME Name, Next;
    ULONG i;

    for (i = 0; i < OSL_INSTALL_BUCKETS; i++)
    {
        for (Name = Set->Buckets[i]; Name; Name = Next)
        {
            Next = Name->Next;
            FwFree(Name);
        }
        Set->Buckets[i] = NULL;
    }
    Set->Count = 0;
}

static
BOOLEAN
OslReadImage(
    _In_ POSL_INSTALL Install,
    _In_ ULONGLONG Offset,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length)
{
    ULONG Read;

    return NT_SUCCESS(NtfsBlReadFileAt(Install->Image, Offset, Buffer, Length, &Read)) && Read == Length;
}

static
BOOLEAN
OslIsoName(
    _In_ const UCHAR* Record,
    _Out_writes_z_(OSL_INSTALL_NAME_CHARS) PSTR Name)
{
    ULONG Length = Record[ISO_RECORD_NAME_SIZE];
    ULONG i;

    if (Length == 1 && Record[ISO_RECORD_NAME] <= 1)
        return FALSE;

    for (i = 0; i < Length && i < OSL_INSTALL_NAME_CHARS - 1 && Record[ISO_RECORD_NAME + i] != ';'; i++)
        Name[i] = Record[ISO_RECORD_NAME + i];

    while (i != 0 && Name[i - 1] == '.')
        i--;

    Name[i] = ANSI_NULL;
    return i != 0;
}

static
BOOLEAN
OslIndexDirectory(
    _Inout_ POSL_INSTALL Install,
    _In_ ULONG Block,
    _In_ ULONG Length,
    _In_z_ PCSTR Path,
    _In_ ULONG Depth);

static
BOOLEAN
OslIndexRecord(
    _Inout_ POSL_INSTALL Install,
    _In_ const UCHAR* Record,
    _In_z_ PCSTR Path,
    _In_ ULONG Depth)
{
    CHAR Name[OSL_INSTALL_NAME_CHARS];
    CHAR Child[OSL_INSTALL_PATH_CHARS];
    POSL_NAME Entry;

    if (!OslIsoName(Record, Name))
        return TRUE;
    if (!NT_SUCCESS(RtlStringCbPrintfA(Child, sizeof(Child), Path[0] ? "%s\\%s" : "%s%s", Path, Name)))
        return FALSE;

    Entry = OslAddName(&Install->Files, Child);
    if (!Entry)
        return FALSE;
    Entry->Block = LeUlong(Record + ISO_RECORD_EXTENT);
    Entry->Size = LeUlong(Record + ISO_RECORD_LENGTH);
    Entry->Directory = (Record[ISO_RECORD_FLAGS] & ISO_FLAG_DIRECTORY) != 0;

    return !Entry->Directory || OslIndexDirectory(Install, Entry->Block, Entry->Size, Child, Depth + 1);
}

static
BOOLEAN
OslIndexDirectory(
    _Inout_ POSL_INSTALL Install,
    _In_ ULONG Block,
    _In_ ULONG Length,
    _In_z_ PCSTR Path,
    _In_ ULONG Depth)
{
    PUCHAR Directory, Record;
    ULONG Offset = 0;
    BOOLEAN Success;

    if (Depth > ISO_DEPTH_MAX || Length == 0 || Length > ISO_DIRECTORY_MAX)
        return FALSE;

    Directory = FwAllocate(Length);
    if (!Directory)
        return FALSE;

    Success = OslReadImage(Install, (ULONGLONG)Block * ISO_BLOCK_SIZE, Directory, Length);
    while (Success && Offset + ISO_RECORD_NAME < Length)
    {
        Record = Directory + Offset;
        if (Record[0] == 0)
        {
            Offset = (Offset / ISO_BLOCK_SIZE + 1) * ISO_BLOCK_SIZE;
            continue;
        }

        Success = Record[0] > ISO_RECORD_NAME &&
                  Record[0] <= Length - Offset &&
                  Record[ISO_RECORD_NAME_SIZE] <= Record[0] - ISO_RECORD_NAME;
        if (!Success)
            break;
        Offset += Record[0];
        Success = OslIndexRecord(Install, Record, Path, Depth);
    }

    FwFree(Directory);
    return Success;
}

static
BOOLEAN
OslIndexImage(
    _Inout_ POSL_INSTALL Install)
{
    UCHAR Descriptor[ISO_BLOCK_SIZE];

    if (!OslReadImage(Install, (ULONGLONG)ISO_DESCRIPTOR_BLOCK * ISO_BLOCK_SIZE, Descriptor, sizeof(Descriptor)) ||
        Descriptor[0] != ISO_PRIMARY ||
        !RtlEqualMemory(Descriptor + 1, "CD001", 5))
    {
        return FALSE;
    }

    return OslIndexDirectory(Install,
                             LeUlong(Descriptor + ISO_ROOT_RECORD + ISO_RECORD_EXTENT),
                             LeUlong(Descriptor + ISO_ROOT_RECORD + ISO_RECORD_LENGTH),
                             "",
                             0);
}

static
PSTR
OslReadVolumeFile(
    _In_z_ PCSTR Path,
    _Out_ PULONG Size)
{
    ULONGLONG FileSize;
    PVOID File;
    PSTR Text;
    ULONG Read;

    *Size = 0;
    if (!NT_SUCCESS(NtfsBlOpenFile(Path, &File, &FileSize, NULL)))
        return NULL;

    Text = FileSize < OSL_INSTALL_INF_MAX ? FwAllocate((SIZE_T)FileSize + 1) : NULL;
    if (Text && FileSize != 0 &&
        (!NT_SUCCESS(NtfsBlReadFileAt(File, 0, Text, (ULONG)FileSize, &Read)) || Read != FileSize))
    {
        FwFree(Text);
        Text = NULL;
    }
    NtfsBlCloseFile(File);

    if (Text)
    {
        Text[FileSize] = ANSI_NULL;
        *Size = (ULONG)FileSize;
    }
    return Text;
}

static
PSTR
OslReadImageText(
    _In_ POSL_INSTALL Install,
    _In_z_ PCSTR Path,
    _Out_ PULONG Size)
{
    POSL_NAME File = OslFindName(&Install->Files, Path);
    PSTR Text;

    *Size = 0;
    if (!File || File->Directory || File->Size >= OSL_INSTALL_INF_MAX)
        return NULL;

    Text = FwAllocate(File->Size + 1);
    if (!Text)
        return NULL;
    if (File->Size != 0 && !OslReadImage(Install, (ULONGLONG)File->Block * ISO_BLOCK_SIZE, Text, File->Size))
    {
        FwFree(Text);
        return NULL;
    }
    Text[File->Size] = ANSI_NULL;
    *Size = File->Size;
    return Text;
}

static
BOOLEAN
OslReadLine(
    _Inout_ PCSTR* Cursor,
    _Out_writes_z_(Size) PSTR Line,
    _In_ SIZE_T Size)
{
    PCSTR Text = *Cursor;
    SIZE_T Length = 0, Start = 0;

    if (*Text == ANSI_NULL)
        return FALSE;

    while (*Text != ANSI_NULL && *Text != '\n')
    {
        if (Length + 1 < Size)
            Line[Length++] = *Text;
        Text++;
    }
    if (*Text == '\n')
        Text++;
    *Cursor = Text;

    while (Length && (Line[Length - 1] == '\r' || Line[Length - 1] == ' ' || Line[Length - 1] == '\t'))
        Length--;
    while (Start < Length && (Line[Start] == ' ' || Line[Start] == '\t'))
        Start++;
    RtlMoveMemory(Line, Line + Start, Length - Start);
    Line[Length - Start] = ANSI_NULL;
    return TRUE;
}

static
VOID
OslInfToken(
    _Inout_ PCSTR* Cursor,
    _In_ CHAR Stop,
    _Out_writes_z_(Size) PSTR Token,
    _In_ SIZE_T Size)
{
    PCSTR Text = *Cursor;
    SIZE_T Length = 0;
    BOOLEAN Quoted = FALSE;

    while (*Text == ' ' || *Text == '\t')
        Text++;

    while (*Text != ANSI_NULL && (Quoted || (*Text != Stop && *Text != ';')))
    {
        if (*Text == '"')
            Quoted = !Quoted;
        else if (Length + 1 < Size)
            Token[Length++] = *Text;
        Text++;
    }

    while (Length && (Token[Length - 1] == ' ' || Token[Length - 1] == '\t'))
        Length--;
    Token[Length] = ANSI_NULL;

    if (*Text == Stop)
        Text++;
    *Cursor = Text;
}

static
VOID
OslParseSetupLine(
    _Inout_ POSL_INSTALL Install,
    _In_z_ PCSTR Section,
    _In_z_ PCSTR Line,
    _In_ BOOLEAN ComputerFiles)
{
    CHAR Key[OSL_INSTALL_PATH_CHARS];
    CHAR Fields[OSL_INSTALL_FIELDS][OSL_INSTALL_PATH_CHARS];
    POSL_COMPUTER_FILE File;
    POSL_TREE Tree;
    PCSTR Value = Line;
    ULONG i;

    OslInfToken(&Value, '=', Key, sizeof(Key));
    for (i = 0; i < OSL_INSTALL_FIELDS; i++)
        OslInfToken(&Value, ',', Fields[i], sizeof(Fields[i]));
    if (Key[0] == ANSI_NULL)
        return;

    if (ComputerFiles)
    {
        if (_strnicmp(Section, "Files.", 6) != 0 || !OslFindName(&Install->Computers, Section + 6) ||
            Fields[0][0] == ANSI_NULL || Fields[1][0] == ANSI_NULL || Install->ComputerCount >= OSL_INSTALL_COMPUTER)
        {
            return;
        }
        File = &Install->ComputerFiles[Install->ComputerCount++];
        RtlStringCbPrintfA(File->Media, sizeof(File->Media), "%s\\%s\\%s",
                           Install->Trees[Install->SystemTree].Media, Fields[0], Key);
        RtlStringCbPrintfA(File->Source, sizeof(File->Source), "%s%s\\%s", Install->SystemRoot, Fields[0], Key);
        RtlStringCbPrintfA(File->Target, sizeof(File->Target), "%s%s\\%s", Install->SystemRoot, Fields[0], Fields[1]);
        return;
    }

    if (_stricmp(Section, "SystemTrees") == 0 && Install->TreeCount < OSL_INSTALL_TREES)
    {
        Tree = &Install->Trees[Install->TreeCount++];
        RtlStringCbCopyA(Tree->Media, sizeof(Tree->Media), Key);
        RtlStringCbCopyA(Tree->Target, sizeof(Tree->Target), Fields[0]);
    }
    else if (_stricmp(Section, "SystemTrees.Exclude") == 0)
    {
        OslAddName(&Install->Exclude, Key);
    }
    else if (_stricmp(Section, "SystemTrees.Preserve") == 0)
    {
        OslAddName(&Install->Preserve, Key);
    }
    else if (_strnicmp(Section, "Computer.", 9) == 0)
    {
        OslAddName(&Install->Computers, Key);
    }
}

static
VOID
OslParseSetupInf(
    _Inout_ POSL_INSTALL Install,
    _In_ BOOLEAN ComputerFiles)
{
    CHAR Section[OSL_INSTALL_NAME_CHARS] = "";
    CHAR Line[OSL_INSTALL_PATH_CHARS];
    PCSTR Cursor = Install->SetupText;
    PSTR End;

    while (OslReadLine(&Cursor, Line, sizeof(Line)))
    {
        if (Line[0] == ANSI_NULL || Line[0] == ';')
            continue;

        if (Line[0] == '[')
        {
            End = strchr(Line, ']');
            if (End)
                *End = ANSI_NULL;
            RtlStringCbCopyA(Section, sizeof(Section), Line + 1);
            continue;
        }

        OslParseSetupLine(Install, Section, Line, ComputerFiles);
    }
}

static
BOOLEAN
OslMapEntry(
    _In_ POSL_INSTALL Install,
    _In_z_ PCSTR Entry,
    _Out_writes_z_(OSL_INSTALL_PATH_CHARS) PSTR Name,
    _Out_writes_z_(OSL_INSTALL_PATH_CHARS) PSTR Target,
    _Out_ PBOOLEAN Directory)
{
    POSL_TREE Tree = NULL;
    SIZE_T Length, MediaLength = 0;
    PCSTR TreeTarget;
    PSTR Separator;
    CHAR Saved;
    BOOLEAN Excluded;
    ULONG i;

    *Directory = FALSE;
    if (!NT_SUCCESS(RtlStringCchCopyA(Name, OSL_INSTALL_PATH_CHARS, Entry)))
        return FALSE;

    Length = strlen(Name);
    if (Length && Name[Length - 1] == '\\')
    {
        *Directory = TRUE;
        Name[--Length] = ANSI_NULL;
    }
    if (Length == 0)
        return FALSE;

    for (i = 0; i < Install->TreeCount && !Tree; i++)
    {
        MediaLength = strlen(Install->Trees[i].Media);
        if (_strnicmp(Name, Install->Trees[i].Media, MediaLength) == 0 &&
            (Name[MediaLength] == '\\' || Name[MediaLength] == ANSI_NULL))
        {
            Tree = &Install->Trees[i];
        }
    }
    if (!Tree)
        return FALSE;

    for (Separator = strchr(Name, '\\'); ; Separator = strchr(Separator + 1, '\\'))
    {
        Saved = Separator ? *Separator : ANSI_NULL;
        if (Separator)
            *Separator = ANSI_NULL;
        Excluded = OslFindName(&Install->Exclude, Name) != NULL;
        if (Separator)
            *Separator = Saved;
        if (Excluded)
            return FALSE;
        if (!Separator)
            break;
    }

    TreeTarget = Tree->Target;
    if (TreeTarget[0] == ANSI_NULL || (TreeTarget[0] == '\\' && TreeTarget[1] == ANSI_NULL))
    {
        Length = strlen(Install->SystemRoot);
        return NT_SUCCESS(RtlStringCchPrintfA(Target, OSL_INSTALL_PATH_CHARS, "%.*s%s",
                                              (int)(Length ? Length - 1 : 0), Install->SystemRoot,
                                              Name + MediaLength));
    }
    if (TreeTarget[0] == '\\')
        return NT_SUCCESS(RtlStringCchPrintfA(Target, OSL_INSTALL_PATH_CHARS, "%s%s", TreeTarget + 1, Name + MediaLength));
    return NT_SUCCESS(RtlStringCchPrintfA(Target, OSL_INSTALL_PATH_CHARS, "%s%s%s",
                                          Install->SystemRoot, TreeTarget, Name + MediaLength));
}

static
BOOLEAN
OslVolumeFileExists(
    _In_z_ PCSTR Path)
{
    PVOID File;

    if (!NT_SUCCESS(NtfsBlOpenFile(Path, &File, NULL, NULL)))
        return FALSE;
    NtfsBlCloseFile(File);
    return TRUE;
}

static
BOOLEAN
OslSameVolumeFiles(
    _In_z_ PCSTR First,
    _In_z_ PCSTR Second)
{
    PSTR A, B = NULL;
    ULONG SizeA, SizeB = 0;
    BOOLEAN Same;

    A = OslReadVolumeFile(First, &SizeA);
    if (A)
        B = OslReadVolumeFile(Second, &SizeB);
    Same = A && B && SizeA == SizeB && RtlEqualMemory(A, B, SizeA);
    if (A)
        FwFree(A);
    if (B)
        FwFree(B);
    return Same;
}

static
VOID
OslAdvance(
    _Inout_ POSL_INSTALL Install,
    _In_ ULONG Bytes)
{
    ULONG Percent;

    Install->DoneBytes += Bytes;
    Percent = Install->TotalBytes ? (ULONG)(min(Install->DoneBytes, Install->TotalBytes) * 100 / Install->TotalBytes)
                                  : 100;
    if (Percent != Install->Percent)
    {
        Install->Percent = Percent;
        FwPrint(L"\rInstalling LiberNT... %lu%%", Percent);
    }
}

static
VOID
OslWriteFromImage(
    _Inout_ POSL_INSTALL Install,
    _In_ POSL_NAME File,
    _In_z_ PCSTR Target)
{
    NTSTATUS Status = STATUS_NO_MEMORY;
    PUCHAR Data;

    Data = FwAllocatePages(max(File->Size, 1));
    if (Data)
    {
        if (File->Size != 0 && !OslReadImage(Install, (ULONGLONG)File->Block * ISO_BLOCK_SIZE, Data, File->Size))
            Status = STATUS_UNEXPECTED_IO_ERROR;
        else
            Status = NtfsBlWriteFile(Target, Data, File->Size);
        FwFreePages(Data, max(File->Size, 1));
    }

    OslAdvance(Install, File->Size);
    if (NT_SUCCESS(Status))
    {
        Install->Written++;
        return;
    }
    FwPrint(L"\r\nINSTALL: %hs could not be written (0x%08lx).\r\n", Target, Status);
    Install->Failed++;
}

static
ULONG
OslCountLines(
    _In_z_ PCSTR Text)
{
    ULONG Count = 1;

    for (; *Text; Text++)
    {
        if (*Text == '\n')
            Count++;
    }
    return Count;
}

static
VOID
OslRemoveObsolete(
    _Inout_ POSL_INSTALL Install)
{
    CHAR Line[OSL_INSTALL_PATH_CHARS], Name[OSL_INSTALL_PATH_CHARS], Target[OSL_INSTALL_PATH_CHARS];
    PCSTR Cursor = Install->OldText, Start, *Lines;
    ULONG Count = 0, i;
    BOOLEAN Directory;
    NTSTATUS Status;

    Lines = FwAllocate(OslCountLines(Install->OldText) * sizeof(PCSTR));
    if (!Lines)
        return;

    for (Start = Cursor; OslReadLine(&Cursor, Line, sizeof(Line)); Start = Cursor)
        Lines[Count++] = Start;

    for (i = Count; i > 0; i--)
    {
        Cursor = Lines[i - 1];
        OslReadLine(&Cursor, Line, sizeof(Line));
        if (Line[0] == ANSI_NULL || Line[0] == ';' ||
            OslFindName(&Install->Inventory, Line) ||
            !OslMapEntry(Install, Line, Name, Target, &Directory) ||
            OslFindName(&Install->Preserve, Name))
        {
            continue;
        }

        Status = NtfsBlDeleteFile(Target, Directory);
        if (NT_SUCCESS(Status))
            Install->Removed++;
        FwPrint(L"INSTALL: drops %hs (0x%08lx)\r\n", Target, Status);
    }

    FwFree((PVOID)Lines);
}

static
VOID
OslInstallFiles(
    _Inout_ POSL_INSTALL Install,
    _In_ BOOLEAN Write)
{
    CHAR Line[OSL_INSTALL_PATH_CHARS], Name[OSL_INSTALL_PATH_CHARS], Target[OSL_INSTALL_PATH_CHARS];
    PCSTR Cursor = Install->NewText;
    POSL_NAME File;
    BOOLEAN Directory;
    NTSTATUS Status;

    while (OslReadLine(&Cursor, Line, sizeof(Line)))
    {
        if (Line[0] == ANSI_NULL || Line[0] == ';' || OslSamePath(Line, Install->InventoryEntry) ||
            !OslMapEntry(Install, Line, Name, Target, &Directory))
        {
            continue;
        }

        if (Directory)
        {
            Status = Write ? NtfsBlCreateDirectory(Target) : STATUS_SUCCESS;
            if (!NT_SUCCESS(Status))
                FwPrint(L"\r\nINSTALL: %hs could not be created (0x%08lx).\r\n", Target, Status);
            continue;
        }

        if (OslFindName(&Install->Preserve, Name) && OslVolumeFileExists(Target))
            continue;

        File = OslFindName(&Install->Files, Name);
        if (!File || File->Directory)
        {
            if (Write)
                FwPrint(L"\r\nINSTALL: %hs is listed but missing from the image.\r\n", Name);
            continue;
        }

        if (Write)
            OslWriteFromImage(Install, File, Target);
        else
            Install->TotalBytes += File->Size;
    }
}

static
VOID
OslInstallComputerFiles(
    _Inout_ POSL_INSTALL Install)
{
    POSL_COMPUTER_FILE Computer;
    POSL_NAME File;
    ULONG i;

    for (i = 0; i < Install->ComputerCount; i++)
    {
        Computer = &Install->ComputerFiles[i];
        File = Computer->Installed ? OslFindName(&Install->Files, Computer->Media) : NULL;
        if (!File || File->Directory)
            continue;
        FwPrint(L"\r\nINSTALL: %hs from %hs\r\n", Computer->Target, Computer->Media);
        OslWriteFromImage(Install, File, Computer->Target);
    }
}

static
POSL_NAME
OslFindSetupInf(
    _In_ POSL_INSTALL Install)
{
    POSL_NAME File;
    PCSTR Separator;
    ULONG i;

    for (i = 0; i < OSL_INSTALL_BUCKETS; i++)
    {
        for (File = Install->Files.Buckets[i]; File; File = File->Next)
        {
            Separator = strchr(File->Path, '\\');
            if (!File->Directory && Separator && !strchr(Separator + 1, '\\') &&
                _stricmp(Separator + 1, OSL_INSTALL_SETUP_INF) == 0)
            {
                return File;
            }
        }
    }
    return NULL;
}

static
BOOLEAN
OslLoadPolicy(
    _Inout_ POSL_INSTALL Install)
{
    POSL_NAME SetupInf = OslFindSetupInf(Install);
    ULONG i;

    Install->SetupText = SetupInf ? OslReadImageText(Install, SetupInf->Path, &Install->SetupSize) : NULL;
    if (!Install->SetupText)
    {
        FwPrint(L"INSTALL: the image has no %hs; starting the installed system.\r\n", OSL_INSTALL_SETUP_INF);
        return FALSE;
    }

    OslParseSetupInf(Install, FALSE);
    for (i = 0; i < Install->TreeCount; i++)
    {
        if (Install->Trees[i].Target[0] == ANSI_NULL ||
            (Install->Trees[i].Target[0] == '\\' && Install->Trees[i].Target[1] == ANSI_NULL))
        {
            Install->SystemTree = i;
            OslParseSetupInf(Install, TRUE);
            return TRUE;
        }
    }

    FwPrint(L"INSTALL: the image names no system tree; starting the installed system.\r\n");
    return FALSE;
}

static
BOOLEAN
OslLoadInventories(
    _Inout_ POSL_INSTALL Install)
{
    CHAR Line[OSL_INSTALL_PATH_CHARS];
    PCSTR Cursor;

    RtlStringCbPrintfA(Install->InventoryEntry, sizeof(Install->InventoryEntry), "%s\\%s",
                       Install->Trees[Install->SystemTree].Media, OSL_INSTALL_INVENTORY);
    RtlStringCbPrintfA(Install->InventoryPath, sizeof(Install->InventoryPath), "%s%s",
                       Install->SystemRoot, OSL_INSTALL_INVENTORY);

    Install->NewText = OslReadImageText(Install, Install->InventoryEntry, &Install->NewSize);
    if (!Install->NewText)
    {
        FwPrint(L"INSTALL: the image has no file inventory; starting the installed system.\r\n");
        return FALSE;
    }

    Install->OldText = OslReadVolumeFile(Install->InventoryPath, &Install->OldSize);
    if (Install->OldText && Install->OldSize == Install->NewSize &&
        RtlEqualMemory(Install->OldText, Install->NewText, Install->NewSize))
    {
        FwPrint(L"INSTALL: the system already runs this image.\r\n");
        return FALSE;
    }

    for (Cursor = Install->NewText; OslReadLine(&Cursor, Line, sizeof(Line)); )
    {
        if (Line[0] != ANSI_NULL && Line[0] != ';' && !OslAddName(&Install->Inventory, Line))
            return FALSE;
    }
    return TRUE;
}

static
VOID
OslInventoryImage(
    _In_opt_z_ PCSTR Text,
    _Out_writes_z_(Size) PSTR Image,
    _In_ SIZE_T Size)
{
    CHAR Line[OSL_INSTALL_PATH_CHARS];
    PCSTR Cursor = Text;

    Image[0] = ANSI_NULL;
    while (Cursor && OslReadLine(&Cursor, Line, sizeof(Line)) && Line[0] == ';')
    {
        if (_strnicmp(Line, OSL_INSTALL_IMAGE_LINE, sizeof(OSL_INSTALL_IMAGE_LINE) - 1) == 0)
        {
            RtlStringCbCopyA(Image, Size, Line + sizeof(OSL_INSTALL_IMAGE_LINE) - 1);
            return;
        }
    }
}

static
VOID
OslInstallOpenedImage(
    _Inout_ POSL_INSTALL Install,
    _In_z_ PCSTR ImagePath)
{
    CHAR OldImage[OSL_INSTALL_NAME_CHARS], NewImage[OSL_INSTALL_NAME_CHARS];
    NTSTATUS Status;
    ULONG i;

    if (!OslIndexImage(Install))
    {
        FwPrint(L"INSTALL: %hs is not a LiberNT image; starting the installed system.\r\n", ImagePath);
        return;
    }
    if (!OslLoadPolicy(Install) || !OslLoadInventories(Install))
        return;

    for (i = 0; i < Install->ComputerCount; i++)
    {
        Install->ComputerFiles[i].Installed = OslSameVolumeFiles(Install->ComputerFiles[i].Source,
                                                                 Install->ComputerFiles[i].Target);
    }

    FwPrint(L"INSTALL: %hs over %hs, %lu image entries, installed inventory %hs.\r\n",
            ImagePath, Install->SystemRoot, Install->Inventory.Count, Install->OldText ? "found" : "missing");

    OslInventoryImage(Install->OldText, OldImage, sizeof(OldImage));
    OslInventoryImage(Install->NewText, NewImage, sizeof(NewImage));
    if (OldImage[0] && _stricmp(OldImage, NewImage) == 0)
        OslRemoveObsolete(Install);
    else if (Install->OldText)
        FwPrint(L"INSTALL: the system was installed from image %hs, this is image %hs; nothing is dropped.\r\n",
                OldImage[0] ? OldImage : "unknown", NewImage[0] ? NewImage : "unknown");
    OslInstallFiles(Install, FALSE);
    OslInstallFiles(Install, TRUE);
    OslInstallComputerFiles(Install);

    if (Install->Failed == 0)
    {
        Status = NtfsBlWriteFile(Install->InventoryPath, Install->NewText, Install->NewSize);
        if (!NT_SUCCESS(Status))
        {
            FwPrint(L"\r\nINSTALL: the file inventory could not be written (0x%08lx).\r\n", Status);
            Install->Failed++;
        }
    }

    if (Install->Failed == 0)
        FwPrint(L"\r\nINSTALL: %lu files written, %lu removed.\r\n", Install->Written, Install->Removed);
    else
        FwPrint(L"\r\nINSTALL: %lu files could not be written.\r\n", Install->Failed);
}

static
VOID
OslFreeInstall(
    _In_ _Post_invalid_ POSL_INSTALL Install)
{
    if (Install->SetupText)
        FwFree(Install->SetupText);
    if (Install->NewText)
        FwFree(Install->NewText);
    if (Install->OldText)
        FwFree(Install->OldText);
    OslFreeNames(&Install->Files);
    OslFreeNames(&Install->Inventory);
    OslFreeNames(&Install->Exclude);
    OslFreeNames(&Install->Preserve);
    OslFreeNames(&Install->Computers);
    FwFree(Install);
}

VOID
OslInstallImage(
    _In_ PDK_DEVICE OsDevice,
    _In_z_ PCSTR SystemRoot,
    _In_z_ PCSTR ImagePath)
{
    POSL_INSTALL Install;
    NTSTATUS Status;

    while (*ImagePath == '\\')
        ImagePath++;
    while (*SystemRoot == '\\')
        SystemRoot++;

    Install = FwAllocate(sizeof(*Install));
    if (!Install)
        return;
    RtlZeroMemory(Install, sizeof(*Install));
    RtlStringCbCopyA(Install->SystemRoot, sizeof(Install->SystemRoot), SystemRoot);
    Install->Percent = MAXULONG;

    Status = NtfsBlMount(OsDevice);
    if (!NT_SUCCESS(Status))
    {
        FwPrint(L"INSTALL: the system volume could not be opened for writing (0x%08lx).\r\n", Status);
        OslFreeInstall(Install);
        return;
    }

    Status = NtfsBlOpenFile(ImagePath, &Install->Image, NULL, NULL);
    if (NT_SUCCESS(Status))
    {
        OslInstallOpenedImage(Install, ImagePath);
        NtfsBlCloseFile(Install->Image);
    }
    else
    {
        FwPrint(L"INSTALL: %hs could not be opened (0x%08lx); starting the installed system.\r\n", ImagePath, Status);
    }

    if (!NT_SUCCESS(NtfsBlUnmount()))
        FwPrint(L"INSTALL: the system volume could not be flushed.\r\n");
    OslFreeInstall(Install);
}
