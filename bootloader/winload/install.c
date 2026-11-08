/*
 * PROJECT:     LiberNT Boot Loader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Installs a staged LiberNT image over the operating system volume
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "winload.h"
#include <ntfsbl.h>
#include <sha256.h>

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
#define OSL_INSTALL_STATUS     "install.log"
#define OSL_INSTALL_CHUNK      0x100000
#define OSL_INSTALL_ATTEMPTS   2
#define OSL_INSTALL_MARGIN     (64ULL * 1024 * 1024)
#define OSL_INSTALL_RECORD     4096
#define OSL_INSTALL_UNDO_DIR   "Rollback\\"
#define OSL_INSTALL_UNDO_LIST  "Rollback\\rollback.lst"
#define OSL_INSTALL_UNDO_HEAD  "; LiberNT rollback "
#define OSL_INSTALL_UNDO_END   "; end"
#define OSL_INSTALL_UNDO_LINE  (OSL_INSTALL_PATH_CHARS + 16)
#define OSL_INSTALL_UNDO_GROW  0x10000
#define OSL_INSTALL_UNDONE     "rolledback"
#define OSL_INSTALL_STAGE_KEY  "Stage="

typedef enum _OSL_INSTALL_PASS
{
    OslPassPlan,
    OslPassBackup,
    OslPassWrite
} OSL_INSTALL_PASS;

static const PCSTR OslInstallHives[] =
{
    "system32\\config\\SYSTEM",
    "system32\\config\\SOFTWARE",
    "system32\\config\\DEFAULT",
    "system32\\config\\SAM",
    "system32\\config\\SECURITY"
};

static const PCSTR OslInstallHiveLogs[] = { "", ".LOG1", ".LOG2" };

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
    ULONGLONG ImageSize;
    UCHAR Hash[BL_SHA256_SIZE];
    PUCHAR Buffer;
    PCSTR Stage;
    NTSTATUS Result;
    ULONG ClusterSize;
    LONGLONG Growth;
    ULONG Missing;
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
    PCWSTR Activity;
    ULONG Written;
    ULONG Removed;
    ULONG Failed;
    CHAR StageRoot[OSL_INSTALL_PATH_CHARS];
    OSL_NAME_SET Backups;
    PSTR Undo;
    ULONG UndoLength;
    ULONG UndoCapacity;
    ULONG BackupCount;
    ULONGLONG BackupBytes;
    BOOLEAN DropObsolete;
    BOOLEAN UndoReady;
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
        FwPrint(L"\r%s... %lu%%", Install->Activity, Percent);
    }
}

static
NTSTATUS
OslVerifyVolumeFile(
    _Inout_ POSL_INSTALL Install,
    _In_z_ PCSTR Target,
    _In_reads_bytes_(Size) const UCHAR* Data,
    _In_ ULONG Size)
{
    ULONGLONG FileSize;
    BOOLEAN IsDirectory;
    PVOID File;
    ULONG Offset, Chunk, Read;
    NTSTATUS Status;

    Status = NtfsBlOpenFile(Target, &File, &FileSize, &IsDirectory);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = (IsDirectory || FileSize != Size) ? STATUS_FILE_CORRUPT_ERROR : STATUS_SUCCESS;
    for (Offset = 0; NT_SUCCESS(Status) && Offset < Size; Offset += Chunk)
    {
        Chunk = min(Size - Offset, OSL_INSTALL_CHUNK);
        Status = NtfsBlReadFileAt(File, Offset, Install->Buffer, Chunk, &Read);
        if (NT_SUCCESS(Status) && (Read != Chunk || !RtlEqualMemory(Install->Buffer, Data + Offset, Chunk)))
            Status = STATUS_FILE_CORRUPT_ERROR;
    }
    NtfsBlCloseFile(File);
    return Status;
}

static
NTSTATUS
OslWriteVerified(
    _Inout_ POSL_INSTALL Install,
    _In_z_ PCSTR Target,
    _In_reads_bytes_(Size) const UCHAR* Data,
    _In_ ULONG Size)
{
    NTSTATUS Status = STATUS_UNSUCCESSFUL;
    ULONG Attempt;

    for (Attempt = 0; Attempt < OSL_INSTALL_ATTEMPTS; Attempt++)
    {
        Status = NtfsBlWriteFile(Target, Data, Size);
        if (NT_SUCCESS(Status))
            Status = OslVerifyVolumeFile(Install, Target, Data, Size);
        if (NT_SUCCESS(Status))
            break;
    }
    return Status;
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
            Status = OslWriteVerified(Install, Target, Data, File->Size);
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
    if (NT_SUCCESS(Install->Result))
        Install->Result = Status;
}

static
BOOLEAN
OslFail(
    _Inout_ POSL_INSTALL Install,
    _In_z_ PCSTR Stage,
    _In_ NTSTATUS Status)
{
    Install->Stage = Stage;
    Install->Result = Status;
    return FALSE;
}

static
BOOLEAN
OslIsAbsent(
    _In_ NTSTATUS Status)
{
    return Status == STATUS_NOT_FOUND || Status == STATUS_OBJECT_NAME_NOT_FOUND ||
           Status == STATUS_OBJECT_PATH_NOT_FOUND;
}

static
NTSTATUS
OslLoadVolumeFile(
    _In_z_ PCSTR Path,
    _Outptr_result_maybenull_ PUCHAR* Data,
    _Out_ PULONG Size)
{
    ULONGLONG FileSize;
    BOOLEAN IsDirectory;
    PVOID File;
    ULONG Offset, Chunk, Read;
    NTSTATUS Status;

    *Data = NULL;
    *Size = 0;
    Status = NtfsBlOpenFile(Path, &File, &FileSize, &IsDirectory);
    if (!NT_SUCCESS(Status))
        return Status;

    if (IsDirectory)
        Status = STATUS_FILE_IS_A_DIRECTORY;
    else if (FileSize > MAXULONG)
        Status = STATUS_FILE_TOO_LARGE;
    else if (!(*Data = FwAllocatePages(max((ULONG)FileSize, 1))))
        Status = STATUS_NO_MEMORY;

    for (Offset = 0; NT_SUCCESS(Status) && Offset < (ULONG)FileSize; Offset += Chunk)
    {
        Chunk = min((ULONG)FileSize - Offset, OSL_INSTALL_CHUNK);
        Status = NtfsBlReadFileAt(File, Offset, *Data + Offset, Chunk, &Read);
        if (NT_SUCCESS(Status) && Read != Chunk)
            Status = STATUS_END_OF_FILE;
    }
    NtfsBlCloseFile(File);

    if (!NT_SUCCESS(Status) && *Data)
    {
        FwFreePages(*Data, max((ULONG)FileSize, 1));
        *Data = NULL;
    }
    if (NT_SUCCESS(Status))
        *Size = (ULONG)FileSize;
    return Status;
}

static
BOOLEAN
OslStagePath(
    _In_ POSL_INSTALL Install,
    _In_z_ PCSTR Relative,
    _Out_writes_z_(OSL_INSTALL_PATH_CHARS) PSTR Path)
{
    return NT_SUCCESS(RtlStringCchPrintfA(Path, OSL_INSTALL_PATH_CHARS, "%s%s", Install->StageRoot, Relative));
}

static
BOOLEAN
OslBackupPath(
    _In_ POSL_INSTALL Install,
    _In_ ULONG Index,
    _Out_writes_z_(OSL_INSTALL_PATH_CHARS) PSTR Path)
{
    return NT_SUCCESS(RtlStringCchPrintfA(Path, OSL_INSTALL_PATH_CHARS, "%s%s%08lu",
                                          Install->StageRoot, OSL_INSTALL_UNDO_DIR, Index));
}

static
VOID
OslUndoHead(
    _In_ POSL_INSTALL Install,
    _Out_writes_z_(OSL_INSTALL_UNDO_LINE) PSTR Head)
{
    static const CHAR Digits[] = "0123456789abcdef";
    SIZE_T Length;
    ULONG i;

    RtlStringCchCopyA(Head, OSL_INSTALL_UNDO_LINE, OSL_INSTALL_UNDO_HEAD);
    Length = strlen(Head);
    for (i = 0; i < BL_SHA256_SIZE; i++)
    {
        Head[Length++] = Digits[Install->Hash[i] >> 4];
        Head[Length++] = Digits[Install->Hash[i] & 0xF];
    }
    Head[Length] = ANSI_NULL;
}

static
BOOLEAN
OslAppendUndo(
    _Inout_ POSL_INSTALL Install,
    _In_z_ PCSTR Line)
{
    ULONG Length = (ULONG)strlen(Line), Needed, Capacity;
    PSTR Text;

    Needed = Install->UndoLength + Length + 3;
    if (Needed > Install->UndoCapacity)
    {
        Capacity = max(max(Install->UndoCapacity * 2, Needed), OSL_INSTALL_UNDO_GROW);
        Text = FwAllocate(Capacity);
        if (!Text)
            return FALSE;
        if (Install->Undo)
        {
            RtlCopyMemory(Text, Install->Undo, Install->UndoLength);
            FwFree(Install->Undo);
        }
        Install->Undo = Text;
        Install->UndoCapacity = Capacity;
    }

    RtlCopyMemory(Install->Undo + Install->UndoLength, Line, Length);
    Install->UndoLength += Length;
    Install->Undo[Install->UndoLength++] = '\r';
    Install->Undo[Install->UndoLength++] = '\n';
    Install->Undo[Install->UndoLength] = ANSI_NULL;
    return TRUE;
}

static
BOOLEAN
OslRecordBackup(
    _Inout_ POSL_INSTALL Install,
    _In_z_ PCSTR Target,
    _In_ ULONG Index)
{
    CHAR Line[OSL_INSTALL_UNDO_LINE];
    POSL_NAME Name;
    NTSTATUS Status;

    if (Index)
        Status = RtlStringCbPrintfA(Line, sizeof(Line), "R %08lu %s", Index, Target);
    else
        Status = RtlStringCbPrintfA(Line, sizeof(Line), "D %s", Target);
    if (!NT_SUCCESS(Status))
        return FALSE;

    Name = OslAddName(&Install->Backups, Target);
    if (!Name)
        return FALSE;
    Name->Block = Index;
    return OslAppendUndo(Install, Line);
}

static
BOOLEAN
OslParseUndoLine(
    _In_z_ PCSTR Line,
    _Out_ PULONG Index,
    _Out_ PCSTR* Target)
{
    ULONG i, Value = 0;

    if (Line[0] == 'D' && Line[1] == ' ' && Line[2] != ANSI_NULL)
    {
        *Index = 0;
        *Target = Line + 2;
        return TRUE;
    }
    if (Line[0] != 'R' || Line[1] != ' ')
        return FALSE;

    for (i = 2; i < 10; i++)
    {
        if (Line[i] < '0' || Line[i] > '9')
            return FALSE;
        Value = Value * 10 + (Line[i] - '0');
    }
    if (Value == 0 || Line[10] != ' ' || Line[11] == ANSI_NULL)
        return FALSE;

    *Index = Value;
    *Target = Line + 11;
    return TRUE;
}

static
BOOLEAN
OslLoadUndo(
    _Inout_ POSL_INSTALL Install)
{
    CHAR Path[OSL_INSTALL_PATH_CHARS], Line[OSL_INSTALL_UNDO_LINE], Head[OSL_INSTALL_UNDO_LINE];
    BOOLEAN Valid, Ended = FALSE;
    PCSTR Cursor, Target;
    POSL_NAME Name;
    ULONG Index, Size;
    PSTR Text;

    if (!OslStagePath(Install, OSL_INSTALL_UNDO_LIST, Path))
        return FALSE;
    Text = OslReadVolumeFile(Path, &Size);
    if (!Text)
        return FALSE;

    OslUndoHead(Install, Head);
    Cursor = Text;
    Valid = OslReadLine(&Cursor, Line, sizeof(Line)) && strcmp(Line, Head) == 0;
    while (Valid && !Ended && OslReadLine(&Cursor, Line, sizeof(Line)))
    {
        if (strcmp(Line, OSL_INSTALL_UNDO_END) == 0)
        {
            Ended = TRUE;
        }
        else if (!OslParseUndoLine(Line, &Index, &Target) || !(Name = OslAddName(&Install->Backups, Target)))
        {
            Valid = FALSE;
        }
        else
        {
            Name->Block = Index;
            Install->BackupCount = max(Install->BackupCount, Index);
        }
    }

    if (!Valid || !Ended)
    {
        FwFree(Text);
        OslFreeNames(&Install->Backups);
        Install->BackupCount = 0;
        return FALSE;
    }

    if (Install->Undo)
        FwFree(Install->Undo);
    Install->Undo = Text;
    Install->UndoLength = Size;
    Install->UndoCapacity = Size + 1;
    return TRUE;
}

static
VOID
OslBackupTarget(
    _Inout_ POSL_INSTALL Install,
    _In_z_ PCSTR Target)
{
    CHAR Path[OSL_INSTALL_PATH_CHARS];
    PUCHAR Data;
    ULONG Size;
    NTSTATUS Status;

    if (OslFindName(&Install->Backups, Target))
        return;

    Status = OslLoadVolumeFile(Target, &Data, &Size);
    if (OslIsAbsent(Status))
    {
        Status = OslRecordBackup(Install, Target, 0) ? STATUS_SUCCESS : STATUS_NO_MEMORY;
    }
    else if (Status == STATUS_FILE_IS_A_DIRECTORY)
    {
        return;
    }
    else if (NT_SUCCESS(Status))
    {
        if (!OslBackupPath(Install, Install->BackupCount + 1, Path))
            Status = STATUS_NAME_TOO_LONG;
        else
            Status = OslWriteVerified(Install, Path, Data, Size);
        if (NT_SUCCESS(Status) && !OslRecordBackup(Install, Target, ++Install->BackupCount))
            Status = STATUS_NO_MEMORY;
        FwFreePages(Data, max(Size, 1));
        OslAdvance(Install, Size);
    }

    if (!NT_SUCCESS(Status))
    {
        FwPrint(L"\r\nINSTALL: %hs could not be saved (0x%08lx).\r\n", Target, Status);
        Install->Failed++;
        if (NT_SUCCESS(Install->Result))
            Install->Result = Status;
    }
}

static
BOOLEAN
OslHivePath(
    _In_ POSL_INSTALL Install,
    _In_ ULONG Index,
    _Out_writes_z_(OSL_INSTALL_PATH_CHARS) PSTR Path)
{
    return NT_SUCCESS(RtlStringCchPrintfA(Path, OSL_INSTALL_PATH_CHARS, "%s%s%s", Install->SystemRoot,
                                          OslInstallHives[Index / RTL_NUMBER_OF(OslInstallHiveLogs)],
                                          OslInstallHiveLogs[Index % RTL_NUMBER_OF(OslInstallHiveLogs)]));
}

static
BOOLEAN
OslOriginalPath(
    _In_ POSL_INSTALL Install,
    _In_z_ PCSTR Path,
    _Out_writes_z_(OSL_INSTALL_PATH_CHARS) PSTR Original)
{
    POSL_NAME Name = OslFindName(&Install->Backups, Path);

    if (!Name)
        return NT_SUCCESS(RtlStringCchCopyA(Original, OSL_INSTALL_PATH_CHARS, Path));
    return Name->Block != 0 && OslBackupPath(Install, Name->Block, Original);
}

static
BOOLEAN
OslSameOriginalFiles(
    _In_ POSL_INSTALL Install,
    _In_z_ PCSTR First,
    _In_z_ PCSTR Second)
{
    CHAR A[OSL_INSTALL_PATH_CHARS], B[OSL_INSTALL_PATH_CHARS];

    return OslOriginalPath(Install, First, A) && OslOriginalPath(Install, Second, B) && OslSameVolumeFiles(A, B);
}

static
ULONGLONG
OslClusters(
    _In_ POSL_INSTALL Install,
    _In_ ULONGLONG Size)
{
    ULONG Cluster = Install->ClusterSize ? Install->ClusterSize : 4096;

    return (Size + Cluster - 1) / Cluster * Cluster;
}

static
VOID
OslPlanFile(
    _Inout_ POSL_INSTALL Install,
    _In_ POSL_NAME File,
    _In_z_ PCSTR Target)
{
    ULONGLONG Existing;
    BOOLEAN IsDirectory;
    PVOID Handle;

    Install->TotalBytes += File->Size;
    Install->Growth += (LONGLONG)OslClusters(Install, File->Size);
    if (NT_SUCCESS(NtfsBlOpenFile(Target, &Handle, &Existing, &IsDirectory)))
    {
        if (!IsDirectory)
        {
            Install->Growth -= (LONGLONG)OslClusters(Install, Existing);
            if (!Install->UndoReady)
            {
                Install->TotalBytes += Existing;
                Install->BackupBytes += OslClusters(Install, Existing) + OSL_INSTALL_RECORD;
            }
        }
        NtfsBlCloseFile(Handle);
    }
    else
    {
        Install->Growth += OSL_INSTALL_RECORD;
    }
}

static
VOID
OslPlanBackup(
    _Inout_ POSL_INSTALL Install,
    _In_z_ PCSTR Target)
{
    ULONGLONG Existing;
    BOOLEAN IsDirectory;
    PVOID Handle;

    if (Install->UndoReady || !NT_SUCCESS(NtfsBlOpenFile(Target, &Handle, &Existing, &IsDirectory)))
        return;
    if (!IsDirectory)
    {
        Install->TotalBytes += Existing;
        Install->BackupBytes += OslClusters(Install, Existing) + OSL_INSTALL_RECORD;
    }
    NtfsBlCloseFile(Handle);
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
    _Inout_ POSL_INSTALL Install,
    _In_ OSL_INSTALL_PASS Pass)
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

        if (Pass == OslPassPlan)
        {
            if (!Directory)
                OslPlanBackup(Install, Target);
            continue;
        }
        if (Pass == OslPassBackup)
        {
            if (!Directory)
                OslBackupTarget(Install, Target);
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
    _In_ OSL_INSTALL_PASS Pass)
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
            /* TODO: directories the update creates are kept when the update is rolled back */
            Status = Pass == OslPassWrite ? NtfsBlCreateDirectory(Target) : STATUS_SUCCESS;
            if (!NT_SUCCESS(Status))
            {
                FwPrint(L"\r\nINSTALL: %hs could not be created (0x%08lx).\r\n", Target, Status);
                Install->Failed++;
                if (NT_SUCCESS(Install->Result))
                    Install->Result = Status;
            }
            else if (Pass == OslPassPlan)
            {
                Install->Growth += OSL_INSTALL_RECORD;
            }
            continue;
        }

        if (OslFindName(&Install->Preserve, Name) && OslVolumeFileExists(Target))
            continue;

        File = OslFindName(&Install->Files, Name);
        if (!File || File->Directory)
        {
            FwPrint(L"\r\nINSTALL: %hs is listed but missing from the image.\r\n", Name);
            Install->Missing++;
            continue;
        }

        if (Pass == OslPassWrite)
            OslWriteFromImage(Install, File, Target);
        else if (Pass == OslPassBackup)
            OslBackupTarget(Install, Target);
        else
            OslPlanFile(Install, File, Target);
    }
}

static
VOID
OslInstallComputerFiles(
    _Inout_ POSL_INSTALL Install,
    _In_ OSL_INSTALL_PASS Pass)
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
        if (Pass == OslPassPlan)
        {
            OslPlanFile(Install, File, Computer->Target);
            continue;
        }
        if (Pass == OslPassBackup)
        {
            OslBackupTarget(Install, Computer->Target);
            continue;
        }
        FwPrint(L"\r\nINSTALL: %hs from %hs\r\n", Computer->Target, Computer->Media);
        OslWriteFromImage(Install, File, Computer->Target);
    }
}

static
VOID
OslBackupSystem(
    _Inout_ POSL_INSTALL Install,
    _In_ OSL_INSTALL_PASS Pass)
{
    CHAR Path[OSL_INSTALL_PATH_CHARS];
    ULONG i;

    if (Install->DropObsolete)
        OslRemoveObsolete(Install, Pass);
    OslInstallFiles(Install, Pass);
    OslInstallComputerFiles(Install, Pass);

    for (i = 0; i < RTL_NUMBER_OF(OslInstallHives) * RTL_NUMBER_OF(OslInstallHiveLogs); i++)
    {
        if (!OslHivePath(Install, i, Path))
            continue;
        if (Pass == OslPassPlan)
            OslPlanBackup(Install, Path);
        else
            OslBackupTarget(Install, Path);
    }
    if (Pass == OslPassPlan)
        OslPlanBackup(Install, Install->InventoryPath);
    else
        OslBackupTarget(Install, Install->InventoryPath);
}

static
BOOLEAN
OslSaveUndo(
    _Inout_ POSL_INSTALL Install)
{
    CHAR Path[OSL_INSTALL_PATH_CHARS], Head[OSL_INSTALL_UNDO_LINE];
    NTSTATUS Status;

    OslFreeNames(&Install->Backups);
    Install->BackupCount = 0;
    Install->UndoLength = 0;
    Install->Stage = "backup";
    OslUndoHead(Install, Head);
    if (!OslStagePath(Install, OSL_INSTALL_UNDO_LIST, Path) || !OslAppendUndo(Install, Head))
        return OslFail(Install, "backup", STATUS_NO_MEMORY);

    OslBackupSystem(Install, OslPassBackup);
    if (Install->Failed)
    {
        FwPrint(L"\r\nINSTALL: %lu files of the installed system could not be saved; starting the installed system.\r\n",
                Install->Failed);
        return FALSE;
    }

    if (!OslAppendUndo(Install, OSL_INSTALL_UNDO_END))
        return OslFail(Install, "backup", STATUS_NO_MEMORY);
    Status = OslWriteVerified(Install, Path, (const UCHAR*)Install->Undo, Install->UndoLength);
    if (!NT_SUCCESS(Status))
    {
        FwPrint(L"\r\nINSTALL: the list of saved files could not be written (0x%08lx); starting the installed system.\r\n",
                Status);
        return OslFail(Install, "backup", Status);
    }

    Install->UndoReady = TRUE;
    FwPrint(L"\r\nINSTALL: %lu files of the installed system saved for a rollback.\r\n", Install->BackupCount);
    return TRUE;
}

static
VOID
OslRestoreFiles(
    _Inout_ POSL_INSTALL Install)
{
    CHAR Line[OSL_INSTALL_UNDO_LINE], Path[OSL_INSTALL_PATH_CHARS];
    PCSTR Cursor, Target;
    PUCHAR Data;
    ULONG Index, Size;
    NTSTATUS Status;

    for (Cursor = Install->Undo; OslReadLine(&Cursor, Line, sizeof(Line)); )
    {
        if (!OslParseUndoLine(Line, &Index, &Target))
            continue;

        if (Index == 0)
        {
            Status = NtfsBlDeleteFile(Target, FALSE);
            if (NT_SUCCESS(Status))
                Install->Removed++;
            else if (OslIsAbsent(Status))
                Status = STATUS_SUCCESS;
        }
        else if (!OslBackupPath(Install, Index, Path))
        {
            Status = STATUS_NAME_TOO_LONG;
        }
        else
        {
            Status = OslLoadVolumeFile(Path, &Data, &Size);
            if (NT_SUCCESS(Status))
            {
                Status = OslWriteVerified(Install, Target, Data, Size);
                FwFreePages(Data, max(Size, 1));
            }
            if (NT_SUCCESS(Status))
                Install->Written++;
        }

        OslAdvance(Install, 1);
        if (!NT_SUCCESS(Status))
        {
            FwPrint(L"\r\nINSTALL: %hs could not be restored (0x%08lx).\r\n", Target, Status);
            Install->Failed++;
            if (NT_SUCCESS(Install->Result))
                Install->Result = Status;
        }
    }
}

static
VOID
OslUndoInstall(
    _Inout_ POSL_INSTALL Install)
{
    NTSTATUS Status;

    if (!OslLoadUndo(Install))
    {
        FwPrint(L"INSTALL: the update changed no file of the installed system; starting it.\r\n");
        Install->Stage = OSL_INSTALL_UNDONE;
        return;
    }

    Status = NtfsBlSetDirty(TRUE);
    if (!NT_SUCCESS(Status))
    {
        FwPrint(L"INSTALL: the system volume could not be marked for checking (0x%08lx).\r\n", Status);
        OslFail(Install, "rollback", Status);
        return;
    }

    FwPrint(L"INSTALL: the update did not finish; restoring the %lu files it changed.\r\n", Install->Backups.Count);
    Install->Stage = "rollback";
    Install->Activity = L"Restoring the previous LiberNT";
    Install->TotalBytes = Install->Backups.Count;
    OslRestoreFiles(Install);

    if (Install->Failed == 0)
    {
        Status = NtfsBlSetDirty(FALSE);
        if (!NT_SUCCESS(Status))
            FwPrint(L"\r\nINSTALL: the system volume stays marked for checking (0x%08lx).\r\n", Status);
        Install->Stage = OSL_INSTALL_UNDONE;
        FwPrint(L"\r\nINSTALL: the previous system was restored, %lu files written, %lu removed.\r\n",
                Install->Written, Install->Removed);
    }
    else
    {
        FwPrint(L"\r\nINSTALL: %lu files could not be restored; the next start tries again.\r\n", Install->Failed);
    }
}

static
BOOLEAN
OslWasUndone(
    _In_ POSL_INSTALL Install)
{
    CHAR Path[OSL_INSTALL_PATH_CHARS], Line[OSL_INSTALL_NAME_CHARS];
    BOOLEAN Undone = FALSE;
    PCSTR Cursor;
    PSTR Text;
    ULONG Size;

    if (!OslStagePath(Install, OSL_INSTALL_STATUS, Path))
        return FALSE;
    Text = OslReadVolumeFile(Path, &Size);
    for (Cursor = Text; Cursor && OslReadLine(&Cursor, Line, sizeof(Line)); )
    {
        if (_stricmp(Line, OSL_INSTALL_STAGE_KEY OSL_INSTALL_UNDONE) == 0)
            Undone = TRUE;
    }
    if (Text)
        FwFree(Text);
    return Undone;
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
        Install->Stage = "current";
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
BOOLEAN
OslParseHash(
    _In_z_ PCSTR Text,
    _Out_writes_bytes_(BL_SHA256_SIZE) PUCHAR Hash)
{
    ULONG i, Digit;
    CHAR Char;

    for (i = 0; i < BL_SHA256_SIZE * 2; i++)
    {
        Char = Text[i];
        if (Char >= '0' && Char <= '9')
            Digit = Char - '0';
        else if (Char >= 'a' && Char <= 'f')
            Digit = Char - 'a' + 10;
        else if (Char >= 'A' && Char <= 'F')
            Digit = Char - 'A' + 10;
        else
            return FALSE;
        if (i % 2 == 0)
            Hash[i / 2] = (UCHAR)(Digit << 4);
        else
            Hash[i / 2] |= (UCHAR)Digit;
    }
    return Text[i] == ANSI_NULL || Text[i] == ' ';
}

static
BOOLEAN
OslVerifyImage(
    _Inout_ POSL_INSTALL Install)
{
    UCHAR Digest[BL_SHA256_SIZE];
    BL_SHA256 Context;
    ULONGLONG Offset;
    ULONG Chunk, Percent, Shown = MAXULONG;

    BlSha256Init(&Context);
    for (Offset = 0; Offset < Install->ImageSize; Offset += Chunk)
    {
        Chunk = (ULONG)min(Install->ImageSize - Offset, (ULONGLONG)OSL_INSTALL_CHUNK);
        if (!OslReadImage(Install, Offset, Install->Buffer, Chunk))
            return FALSE;
        BlSha256Update(&Context, Install->Buffer, Chunk);
        Percent = (ULONG)(Offset * 100 / Install->ImageSize);
        if (Percent != Shown)
        {
            Shown = Percent;
            FwPrint(L"\rChecking the LiberNT update... %lu%%", Percent);
        }
    }
    BlSha256Final(&Context, Digest);
    FwPrint(L"\r\n");
    return RtlEqualMemory(Digest, Install->Hash, sizeof(Digest));
}

static
BOOLEAN
OslPreflight(
    _Inout_ POSL_INSTALL Install)
{
    ULONGLONG FreeBytes, Needed;
    NTSTATUS Status;

    if (!OslVerifyImage(Install))
    {
        FwPrint(L"INSTALL: the image does not match its checksum; starting the installed system.\r\n");
        return OslFail(Install, "checksum", STATUS_FILE_CORRUPT_ERROR);
    }

    Status = NtfsBlQuerySpace(&FreeBytes, &Install->ClusterSize);
    if (!NT_SUCCESS(Status))
        return OslFail(Install, "space", Status);

    OslBackupSystem(Install, OslPassPlan);
    if (Install->Missing)
    {
        FwPrint(L"INSTALL: %lu files of the inventory are missing from the image; starting the installed system.\r\n",
                Install->Missing);
        return OslFail(Install, "missing", STATUS_OBJECT_NAME_NOT_FOUND);
    }

    Needed = (Install->Growth > 0 ? (ULONGLONG)Install->Growth : 0) + Install->BackupBytes + OSL_INSTALL_MARGIN;
    FwPrint(L"INSTALL: %I64u MB to write, %I64u MB needed, %I64u MB free.\r\n",
            Install->TotalBytes >> 20, Needed >> 20, FreeBytes >> 20);
    if (FreeBytes < Needed)
    {
        FwPrint(L"INSTALL: not enough free space; starting the installed system.\r\n");
        return OslFail(Install, "space", STATUS_DISK_FULL);
    }
    return TRUE;
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
        OslFail(Install, "image", STATUS_FILE_CORRUPT_ERROR);
        return;
    }
    if (!OslLoadPolicy(Install))
    {
        OslFail(Install, "policy", STATUS_FILE_CORRUPT_ERROR);
        return;
    }
    if (!OslLoadInventories(Install))
    {
        if (!Install->Stage)
            OslFail(Install, "inventory", STATUS_FILE_CORRUPT_ERROR);
        return;
    }

    Install->UndoReady = OslLoadUndo(Install);
    for (i = 0; i < Install->ComputerCount; i++)
    {
        Install->ComputerFiles[i].Installed = OslSameOriginalFiles(Install,
                                                                   Install->ComputerFiles[i].Source,
                                                                   Install->ComputerFiles[i].Target);
    }

    OslInventoryImage(Install->OldText, OldImage, sizeof(OldImage));
    OslInventoryImage(Install->NewText, NewImage, sizeof(NewImage));
    Install->DropObsolete = OldImage[0] && _stricmp(OldImage, NewImage) == 0;

    FwPrint(L"INSTALL: %hs over %hs, %lu image entries, installed inventory %hs, saved files %hs.\r\n",
            ImagePath, Install->SystemRoot, Install->Inventory.Count, Install->OldText ? "found" : "missing",
            Install->UndoReady ? "found" : "missing");

    if (!OslPreflight(Install))
        return;

    Status = NtfsBlSetDirty(TRUE);
    if (!NT_SUCCESS(Status))
    {
        FwPrint(L"INSTALL: the system volume could not be marked for checking (0x%08lx); starting the installed system.\r\n",
                Status);
        OslFail(Install, "volume", Status);
        return;
    }

    if (!Install->UndoReady && !OslSaveUndo(Install))
        return;

    Install->Stage = "write";
    if (Install->DropObsolete)
        OslRemoveObsolete(Install, OslPassWrite);
    else if (Install->OldText)
        FwPrint(L"INSTALL: the system was installed from image %hs, this is image %hs; nothing is dropped.\r\n",
                OldImage[0] ? OldImage : "unknown", NewImage[0] ? NewImage : "unknown");
    OslInstallFiles(Install, OslPassWrite);
    OslInstallComputerFiles(Install, OslPassWrite);

    if (Install->Failed == 0)
    {
        Status = OslWriteVerified(Install, Install->InventoryPath, (const UCHAR*)Install->NewText, Install->NewSize);
        if (!NT_SUCCESS(Status))
        {
            FwPrint(L"\r\nINSTALL: the file inventory could not be written (0x%08lx).\r\n", Status);
            Install->Failed++;
            Install->Result = Status;
        }
    }

    if (Install->Failed == 0)
    {
        Status = NtfsBlSetDirty(FALSE);
        if (!NT_SUCCESS(Status))
            FwPrint(L"\r\nINSTALL: the system volume stays marked for checking (0x%08lx).\r\n", Status);
        Install->Stage = "done";
        /* FIXME: this start still uses the SYSTEM hive of the previous build; an update that needs a boot driver the previous build lacks is rolled back */
        FwPrint(L"\r\nINSTALL: %lu files written, %lu removed.\r\n", Install->Written, Install->Removed);
    }
    else
    {
        FwPrint(L"\r\nINSTALL: %lu files could not be written; the next start tries again.\r\n", Install->Failed);
    }
}

static
VOID
OslWriteStatus(
    _In_ POSL_INSTALL Install)
{
    CHAR Path[OSL_INSTALL_PATH_CHARS], Text[256];
    NTSTATUS Status;

    if (!Install->StageRoot[0] || !OslStagePath(Install, OSL_INSTALL_STATUS, Path))
        return;

    if (!NT_SUCCESS(RtlStringCbPrintfA(Text, sizeof(Text),
                                       "[Install]\r\nStage=%s\r\nResult=0x%08lx\r\nWritten=%lu\r\nRemoved=%lu\r\nFailed=%lu\r\n",
                                       Install->Stage ? Install->Stage : "open", Install->Result, Install->Written,
                                       Install->Removed, Install->Failed)))
    {
        return;
    }
    Status = NtfsBlWriteFile(Path, Text, (ULONG)strlen(Text));
    if (!NT_SUCCESS(Status))
        FwPrint(L"INSTALL: the install status could not be written (0x%08lx).\r\n", Status);
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
    if (Install->Buffer)
        FwFreePages(Install->Buffer, OSL_INSTALL_CHUNK);
    OslFreeNames(&Install->Files);
    OslFreeNames(&Install->Inventory);
    OslFreeNames(&Install->Exclude);
    OslFreeNames(&Install->Preserve);
    OslFreeNames(&Install->Computers);
    OslFreeNames(&Install->Backups);
    if (Install->Undo)
        FwFree(Install->Undo);
    FwFree(Install);
}

VOID
OslInstallImage(
    _In_ PDK_DEVICE OsDevice,
    _In_z_ PCSTR SystemRoot,
    _In_z_ PCSTR ImagePath,
    _In_opt_z_ PCSTR ImageHash,
    _In_ BOOLEAN Undo)
{
    POSL_INSTALL Install;
    PCSTR Name;
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
    Name = strrchr(ImagePath, '\\');
    if (Name)
        RtlStringCbCopyNA(Install->StageRoot, sizeof(Install->StageRoot), ImagePath, Name + 1 - ImagePath);
    Install->Percent = MAXULONG;
    Install->Activity = L"Installing LiberNT";
    Install->Buffer = FwAllocatePages(OSL_INSTALL_CHUNK);

    Status = NtfsBlMount(OsDevice);
    if (!NT_SUCCESS(Status))
    {
        FwPrint(L"INSTALL: the system volume could not be opened for writing (0x%08lx).\r\n", Status);
        OslFreeInstall(Install);
        return;
    }

    if (!Install->Buffer)
    {
        OslFail(Install, "open", STATUS_NO_MEMORY);
    }
    else if (!Install->StageRoot[0])
    {
        FwPrint(L"INSTALL: %hs is not in a staging folder; starting the installed system.\r\n", ImagePath);
        OslFail(Install, "open", STATUS_OBJECT_PATH_INVALID);
    }
    else if (!ImageHash || !OslParseHash(ImageHash, Install->Hash))
    {
        FwPrint(L"INSTALL: the update has no valid checksum; starting the installed system.\r\n");
        OslFail(Install, "checksum", STATUS_INVALID_PARAMETER);
    }
    else if (OslWasUndone(Install))
    {
        FwPrint(L"INSTALL: the update was rolled back at an earlier start; starting the installed system.\r\n");
        Install->Stage = OSL_INSTALL_UNDONE;
    }
    else if (Undo)
    {
        OslUndoInstall(Install);
    }
    else
    {
        Status = NtfsBlOpenFile(ImagePath, &Install->Image, &Install->ImageSize, NULL);
        if (NT_SUCCESS(Status))
        {
            OslInstallOpenedImage(Install, ImagePath);
            NtfsBlCloseFile(Install->Image);
        }
        else
        {
            FwPrint(L"INSTALL: %hs could not be opened (0x%08lx); starting the installed system.\r\n", ImagePath, Status);
            OslFail(Install, "open", Status);
        }
    }

    OslWriteStatus(Install);
    if (!NT_SUCCESS(NtfsBlUnmount()))
        FwPrint(L"INSTALL: the system volume could not be flushed.\r\n");
    OslFreeInstall(Install);
}
