/*
 * PROJECT:     LiberNT Linux Subsystem
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Instances, mounts, path lookup and NT backed file system nodes
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "lxcore.h"
#include <ntstrsafe.h>
#include <stdarg.h>

#define NDEBUG
#include <debug.h>

#define LX_MAGIC_NTFS       0x5346544EULL
#define LX_MAGIC_PROC       0x00009FA0ULL
#define LX_MAGIC_SYSFS      0x62656572ULL
#define LX_MAGIC_TMPFS      0x01021994ULL
#define LX_MAGIC_CGROUP2    0x63677270ULL
#define LX_MAGIC_DEVPTS     0x00001CD1ULL

LIST_ENTRY LxInstanceList;
FAST_MUTEX LxInstanceListLock;
KEVENT LxInstanceCreateLock;
static volatile LONG LxTrashCounter;
static volatile LONG LxInstanceCounter;

PSTR
LxStrPrintf(
    _In_ PCSTR Format,
    ...)
{
    va_list Arguments;
    PSTR Buffer;
    SIZE_T Size = 256;

    for (;;)
    {
        NTSTATUS Status;

        Buffer = LxAlloc(Size);
        if (Buffer == NULL)
            return NULL;
        va_start(Arguments, Format);
        Status = RtlStringCbVPrintfA(Buffer, Size, Format, Arguments);
        va_end(Arguments);
        if (NT_SUCCESS(Status))
            return Buffer;
        LxFree(Buffer);
        if ((Status != STATUS_BUFFER_OVERFLOW) || (Size >= 65536))
            return NULL;
        Size *= 4;
    }
}

static
LONG
LxUnicodeAppend(
    _Inout_ PUNICODE_STRING Target,
    _In_ PCWSTR Data,
    _In_ ULONG Bytes)
{
    if ((ULONG)Target->Length + Bytes > Target->MaximumLength)
        return -LX_ENAMETOOLONG;
    RtlCopyMemory((PUCHAR)Target->Buffer + Target->Length, Data, Bytes);
    Target->Length += (USHORT)Bytes;
    return 0;
}

LONG
LxNtPath(
    _In_ PLX_MOUNT Mount,
    _In_ PCSTR Relative,
    _Out_ PUNICODE_STRING NtPath)
{
    ULONG Utf8Length = (ULONG)strlen(Relative);
    ULONG Needed = 0, Written = 0, Index;
    PWCHAR Tail;
    NTSTATUS Status;
    BOOLEAN DriveRoot;

    RtlZeroMemory(NtPath, sizeof(*NtPath));
    if (Utf8Length != 0)
    {
        Status = RtlUTF8ToUnicodeN(NULL, 0, &Needed, Relative, Utf8Length);
        if (!NT_SUCCESS(Status) && (Status != STATUS_SOME_NOT_MAPPED))
            return -LX_EINVAL;
    }

    if ((ULONG)Mount->NtRoot.Length + sizeof(WCHAR) + Needed + sizeof(WCHAR) > MAXUSHORT)
        return -LX_ENAMETOOLONG;

    NtPath->MaximumLength = (USHORT)(Mount->NtRoot.Length + sizeof(WCHAR) + Needed + sizeof(WCHAR));
    NtPath->Buffer = LxAllocPaged(NtPath->MaximumLength);
    if (NtPath->Buffer == NULL)
        return -LX_ENOMEM;

    LxUnicodeAppend(NtPath, Mount->NtRoot.Buffer, Mount->NtRoot.Length);
    DriveRoot = (Mount->NtRoot.Length >= sizeof(WCHAR)) &&
                (Mount->NtRoot.Buffer[Mount->NtRoot.Length / sizeof(WCHAR) - 1] == L':');
    if ((Utf8Length != 0) || DriveRoot)
        LxUnicodeAppend(NtPath, L"\\", sizeof(WCHAR));
    if (Utf8Length == 0)
        return 0;

    Tail = (PWCHAR)((PUCHAR)NtPath->Buffer + NtPath->Length);
    Status = RtlUTF8ToUnicodeN(Tail, Needed, &Written, Relative, Utf8Length);
    if (!NT_SUCCESS(Status) && (Status != STATUS_SOME_NOT_MAPPED))
    {
        LxFreePaged(NtPath->Buffer);
        RtlZeroMemory(NtPath, sizeof(*NtPath));
        return -LX_EINVAL;
    }
    for (Index = 0; Index < Written / sizeof(WCHAR); Index++)
    {
        WCHAR Character = Tail[Index];

        if (Character == L'/')
            Tail[Index] = L'\\';
        else if ((Character < 0x20) || (Character == L':') || (Character == L'*') || (Character == L'?') ||
                 (Character == L'"') || (Character == L'<') || (Character == L'>') || (Character == L'|') ||
                 (Character == L'\\'))
        {
            Tail[Index] = 0xF000 + Character;
        }
    }
    NtPath->Length += (USHORT)Written;
    return 0;
}

LONG
LxPathNormalize(
    _In_ PCSTR Input,
    _Out_writes_(LX_PATH_MAX) PSTR Output)
{
    SIZE_T Used = 0;
    PCSTR Cursor = Input;

    Output[0] = '\0';
    while (*Cursor != '\0')
    {
        PCSTR Start;
        SIZE_T Length;

        while (*Cursor == '/')
            Cursor++;
        if (*Cursor == '\0')
            break;

        Start = Cursor;
        while ((*Cursor != '\0') && (*Cursor != '/'))
            Cursor++;
        Length = Cursor - Start;

        if ((Length == 1) && (Start[0] == '.'))
            continue;

        if ((Length == 2) && (Start[0] == '.') && (Start[1] == '.'))
        {
            while ((Used > 0) && (Output[Used - 1] != '/'))
                Used--;
            if (Used > 0)
                Used--;
            Output[Used] = '\0';
            continue;
        }

        if (Used + 1 + Length >= LX_PATH_MAX)
            return -LX_ENAMETOOLONG;

        Output[Used++] = '/';
        RtlCopyMemory(Output + Used, Start, Length);
        Used += Length;
        Output[Used] = '\0';
    }

    if (Used == 0)
    {
        Output[0] = '/';
        Output[1] = '\0';
    }
    return 0;
}

static
PSTR
LxPathJoin(
    _In_ PCSTR Base,
    _In_ PCSTR Name)
{
    SIZE_T BaseLength = strlen(Base), NameLength = strlen(Name);
    PSTR Result;

    if (BaseLength + 1 + NameLength >= LX_PATH_MAX)
        return NULL;
    Result = LxAlloc(BaseLength + NameLength + 2);
    if (Result == NULL)
        return NULL;
    RtlCopyMemory(Result, Base, BaseLength);
    if ((BaseLength == 0) || (Base[BaseLength - 1] != '/'))
        Result[BaseLength++] = '/';
    RtlCopyMemory(Result + BaseLength, Name, NameLength + 1);
    return Result;
}

static
LONG
LxNtOpenHandle(
    _In_ PCUNICODE_STRING NtPath,
    _In_ ACCESS_MASK Access,
    _In_ ULONG Disposition,
    _In_ ULONG Options,
    _In_ ULONG Attributes,
    _Out_ PHANDLE Handle)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK Iosb;
    NTSTATUS Status;

    InitializeObjectAttributes(&ObjectAttributes, (PUNICODE_STRING)NtPath,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = ZwCreateFile(Handle, Access | SYNCHRONIZE, &ObjectAttributes, &Iosb, NULL, Attributes,
                          FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, Disposition,
                          Options | FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_REPARSE_POINT, NULL, 0);
    if (!NT_SUCCESS(Status))
    {
        *Handle = NULL;
        if (Status == STATUS_FILE_IS_A_DIRECTORY)
            return -LX_EISDIR;
        if (Status == STATUS_NOT_A_DIRECTORY)
            return -LX_ENOTDIR;
        return -LxStatusToErrno(Status);
    }
    return 0;
}

LONG
LxNtReadLxAttributes(
    _In_ HANDLE Handle,
    _Out_ PULONG Mode,
    _Out_ PULONG Uid,
    _Out_ PULONG Gid,
    _Out_ PULONG64 Device)
{
    static const PCSTR Names[4] = { "$LXUID", "$LXGID", "$LXMOD", "$LXDEV" };
    UCHAR ListBuffer[96];
    UCHAR Output[256];
    PFILE_FULL_EA_INFORMATION Entry;
    IO_STATUS_BLOCK Iosb;
    ULONG Offset = 0, Index, ListLength = 0;
    NTSTATUS Status;

    *Mode = *Uid = *Gid = MAXULONG;
    *Device = MAXULONG64;

    RtlZeroMemory(ListBuffer, sizeof(ListBuffer));
    for (Index = 0; Index < 4; Index++)
    {
        PFILE_GET_EA_INFORMATION Get = (PFILE_GET_EA_INFORMATION)(ListBuffer + ListLength);
        ULONG NameLength = (ULONG)strlen(Names[Index]);
        ULONG Size = (ULONG)LX_ALIGN_UP(FIELD_OFFSET(FILE_GET_EA_INFORMATION, EaName) + NameLength + 1, 4);

        Get->EaNameLength = (UCHAR)NameLength;
        RtlCopyMemory(Get->EaName, Names[Index], NameLength);
        Get->NextEntryOffset = (Index < 3) ? Size : 0;
        ListLength += Size;
    }

    Status = ZwQueryEaFile(Handle, &Iosb, Output, sizeof(Output), FALSE, ListBuffer, ListLength, NULL, TRUE);
    if (!NT_SUCCESS(Status))
        return -LX_ENODATA;

    for (;;)
    {
        Entry = (PFILE_FULL_EA_INFORMATION)(Output + Offset);
        if (Entry->EaValueLength != 0)
        {
            PUCHAR Value = (PUCHAR)Entry->EaName + Entry->EaNameLength + 1;

            for (Index = 0; Index < 4; Index++)
            {
                if ((Entry->EaNameLength == strlen(Names[Index])) &&
                    (_strnicmp(Entry->EaName, Names[Index], Entry->EaNameLength) == 0))
                {
                    break;
                }
            }

            if ((Index < 3) && (Entry->EaValueLength >= sizeof(ULONG)))
            {
                ULONG Value32;

                RtlCopyMemory(&Value32, Value, sizeof(Value32));
                if (Index == 0)
                    *Uid = Value32;
                else if (Index == 1)
                    *Gid = Value32;
                else
                    *Mode = Value32;
            }
            else if ((Index == 3) && (Entry->EaValueLength >= 2 * sizeof(ULONG)))
            {
                ULONG Pair[2];

                RtlCopyMemory(Pair, Value, sizeof(Pair));
                *Device = ((ULONG64)Pair[0] << 32) | Pair[1];
            }
        }
        if (Entry->NextEntryOffset == 0)
            break;
        Offset += Entry->NextEntryOffset;
        if (Offset >= sizeof(Output))
            break;
    }
    return 0;
}

LONG
LxNtWriteLxAttributes(
    _In_ HANDLE Handle,
    _In_ ULONG Mode,
    _In_ ULONG Uid,
    _In_ ULONG Gid,
    _In_ ULONG64 Device,
    _In_ BOOLEAN WriteDevice)
{
    static const PCSTR Names[4] = { "$LXUID", "$LXGID", "$LXMOD", "$LXDEV" };
    UCHAR Buffer[160];
    ULONG Length = 0, Index, Count = WriteDevice ? 4 : 3;
    PFILE_FULL_EA_INFORMATION Previous = NULL;
    IO_STATUS_BLOCK Iosb;
    NTSTATUS Status;

    RtlZeroMemory(Buffer, sizeof(Buffer));
    for (Index = 0; Index < Count; Index++)
    {
        PFILE_FULL_EA_INFORMATION Entry = (PFILE_FULL_EA_INFORMATION)(Buffer + Length);
        ULONG NameLength = (ULONG)strlen(Names[Index]);
        ULONG ValueLength = (Index == 3) ? 8 : 4;
        PUCHAR Value;

        Entry->Flags = 0;
        Entry->EaNameLength = (UCHAR)NameLength;
        Entry->EaValueLength = (USHORT)ValueLength;
        RtlCopyMemory(Entry->EaName, Names[Index], NameLength);
        Value = (PUCHAR)Entry->EaName + NameLength + 1;
        if (Index == 0)
            RtlCopyMemory(Value, &Uid, 4);
        else if (Index == 1)
            RtlCopyMemory(Value, &Gid, 4);
        else if (Index == 2)
            RtlCopyMemory(Value, &Mode, 4);
        else
        {
            ULONG Pair[2] = { (ULONG)(Device >> 32), (ULONG)Device };

            RtlCopyMemory(Value, Pair, 8);
        }

        if (Previous != NULL)
            Previous->NextEntryOffset = (ULONG)((PUCHAR)Entry - (PUCHAR)Previous);
        Previous = Entry;
        Length += (ULONG)LX_ALIGN_UP(FIELD_OFFSET(FILE_FULL_EA_INFORMATION, EaName) + NameLength + 1 + ValueLength, 4);
    }

    Status = ZwSetEaFile(Handle, &Iosb, Buffer, Length);
    return NT_SUCCESS(Status) ? 0 : -LxStatusToErrno(Status);
}

static
LONG
LxNtSetSpecialAttribute(
    _In_ HANDLE Handle)
{
    FILE_BASIC_INFORMATION Basic;
    IO_STATUS_BLOCK Iosb;
    NTSTATUS Status;

    RtlZeroMemory(&Basic, sizeof(Basic));
    Basic.FileAttributes = FILE_ATTRIBUTE_SYSTEM;
    Status = ZwSetInformationFile(Handle, &Iosb, &Basic, sizeof(Basic), FileBasicInformation);
    return NT_SUCCESS(Status) ? 0 : -LxStatusToErrno(Status);
}

LONG
LxNtCreateDirectory(
    _In_ PCUNICODE_STRING NtPath,
    _In_ ULONG Mode)
{
    HANDLE Handle;
    LONG Error;

    Error = LxNtOpenHandle(NtPath, FILE_LIST_DIRECTORY | FILE_WRITE_EA | FILE_READ_EA, FILE_OPEN_IF,
                           FILE_DIRECTORY_FILE, FILE_ATTRIBUTE_NORMAL, &Handle);
    if (Error != 0)
        return Error;
    LxNtWriteLxAttributes(Handle, LX_S_IFDIR | (Mode & 07777), 0, 0, 0, FALSE);
    ZwClose(Handle);
    return 0;
}

static
LONG
LxNtDeleteTreeHandle(
    _In_ HANDLE Directory)
{
    PUCHAR Buffer = LxAllocPaged(LX_IO_CHUNK);
    BOOLEAN Restart = TRUE;

    if (Buffer == NULL)
        return -LX_ENOMEM;

    for (;;)
    {
        IO_STATUS_BLOCK Iosb;
        PFILE_DIRECTORY_INFORMATION Entry;
        NTSTATUS Status;
        ULONG Offset = 0;

        Status = ZwQueryDirectoryFile(Directory, NULL, NULL, NULL, &Iosb, Buffer, LX_IO_CHUNK,
                                      FileDirectoryInformation, FALSE, NULL, Restart);
        Restart = FALSE;
        if (!NT_SUCCESS(Status))
            break;

        for (;;)
        {
            OBJECT_ATTRIBUTES ObjectAttributes;
            UNICODE_STRING Name;
            HANDLE Child;

            Entry = (PFILE_DIRECTORY_INFORMATION)(Buffer + Offset);
            Name.Buffer = Entry->FileName;
            Name.Length = Name.MaximumLength = (USHORT)Entry->FileNameLength;
            if (!((Name.Length == 2) && (Name.Buffer[0] == L'.')) &&
                !((Name.Length == 4) && (Name.Buffer[0] == L'.') && (Name.Buffer[1] == L'.')))
            {
                InitializeObjectAttributes(&ObjectAttributes, &Name, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, Directory, NULL);
                if (NT_SUCCESS(ZwCreateFile(&Child, DELETE | FILE_LIST_DIRECTORY | FILE_WRITE_ATTRIBUTES | SYNCHRONIZE,
                                            &ObjectAttributes, &Iosb, NULL, 0,
                                            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, FILE_OPEN,
                                            FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_REPARSE_POINT, NULL, 0)))
                {
                    FILE_DISPOSITION_INFORMATION Disposition = { TRUE };
                    FILE_BASIC_INFORMATION Basic;

                    if (Entry->FileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                        LxNtDeleteTreeHandle(Child);
                    RtlZeroMemory(&Basic, sizeof(Basic));
                    Basic.FileAttributes = FILE_ATTRIBUTE_NORMAL;
                    ZwSetInformationFile(Child, &Iosb, &Basic, sizeof(Basic), FileBasicInformation);
                    ZwSetInformationFile(Child, &Iosb, &Disposition, sizeof(Disposition), FileDispositionInformation);
                    ZwClose(Child);
                }
            }
            if (Entry->NextEntryOffset == 0)
                break;
            Offset += Entry->NextEntryOffset;
        }
    }

    LxFreePaged(Buffer);
    return 0;
}

LONG
LxNtDeleteTree(
    _In_ PCUNICODE_STRING NtPath)
{
    FILE_DISPOSITION_INFORMATION Disposition = { TRUE };
    IO_STATUS_BLOCK Iosb;
    HANDLE Handle;
    LONG Error;

    Error = LxNtOpenHandle(NtPath, DELETE | FILE_LIST_DIRECTORY, FILE_OPEN, FILE_DIRECTORY_FILE, 0, &Handle);
    if (Error != 0)
        return Error;
    LxNtDeleteTreeHandle(Handle);
    ZwSetInformationFile(Handle, &Iosb, &Disposition, sizeof(Disposition), FileDispositionInformation);
    ZwClose(Handle);
    return 0;
}

static
LONG
LxNtCreateNode(
    _In_ PCUNICODE_STRING NtPath,
    _In_ ULONG Mode,
    _In_ ULONG64 Device,
    _In_opt_ PCSTR LinkTarget)
{
    IO_STATUS_BLOCK Iosb;
    HANDLE Handle;
    LONG Error;

    Error = LxNtOpenHandle(NtPath, FILE_GENERIC_WRITE | FILE_READ_EA, FILE_CREATE, FILE_NON_DIRECTORY_FILE,
                           FILE_ATTRIBUTE_NORMAL, &Handle);
    if (Error != 0)
        return Error;

    Error = LxNtWriteLxAttributes(Handle, Mode, 0, 0, Device,
                                  ((Mode & LX_S_IFMT) == LX_S_IFCHR) || ((Mode & LX_S_IFMT) == 0060000));
    if ((Error == 0) && (LinkTarget != NULL))
    {
        NTSTATUS Status = ZwWriteFile(Handle, NULL, NULL, NULL, &Iosb, (PVOID)LinkTarget,
                                      (ULONG)strlen(LinkTarget), NULL, NULL);
        if (!NT_SUCCESS(Status))
            Error = -LxStatusToErrno(Status);
    }
    if ((Error == 0) && ((Mode & LX_S_IFMT) != LX_S_IFREG))
        Error = LxNtSetSpecialAttribute(Handle);
    if (Error != 0)
    {
        FILE_DISPOSITION_INFORMATION Disposition = { TRUE };

        ZwSetInformationFile(Handle, &Iosb, &Disposition, sizeof(Disposition), FileDispositionInformation);
    }
    ZwClose(Handle);
    return Error;
}

static
LONG
LxInstanceMountNt(
    _In_ PLX_INSTANCE Instance,
    _In_ PCSTR Target,
    _In_ PCSTR Source,
    _In_ PCSTR FsType,
    _In_ ULONG Flags,
    _In_ PCUNICODE_STRING NtRoot,
    _In_ ULONG64 Magic,
    _Out_opt_ PLX_MOUNT *Out)
{
    PLX_MOUNT Mount = LxAlloc(sizeof(*Mount));

    if (Mount == NULL)
        return -LX_ENOMEM;

    Mount->Kind = LxFsNt;
    Mount->Instance = Instance;
    Mount->Target = LxStrDup(Target);
    Mount->Source = LxStrDup(Source);
    Mount->FsType = LxStrDup(FsType);
    Mount->Root = LxStrDup("/");
    Mount->Flags = Flags;
    Mount->Magic = Magic;
    Mount->NtRoot.MaximumLength = NtRoot->Length + sizeof(WCHAR);
    Mount->NtRoot.Buffer = LxAlloc(Mount->NtRoot.MaximumLength);
    if ((Mount->Target == NULL) || (Mount->Source == NULL) || (Mount->FsType == NULL) ||
        (Mount->Root == NULL) || (Mount->NtRoot.Buffer == NULL))
    {
        LxFree(Mount->Target);
        LxFree(Mount->Source);
        LxFree(Mount->FsType);
        LxFree(Mount->Root);
        LxFree(Mount->NtRoot.Buffer);
        LxFree(Mount);
        return -LX_ENOMEM;
    }
    RtlCopyMemory(Mount->NtRoot.Buffer, NtRoot->Buffer, NtRoot->Length);
    Mount->NtRoot.Length = NtRoot->Length;

    ExAcquireFastMutex(&Instance->Lock);
    Mount->Id = Instance->NextMountId++;
    Mount->ParentId = (Mount->Id > 20) ? 20 : 1;
    Mount->Dev = (Mount->Id == 20) ? 0x820 : (0x10 + Mount->Id);
    InsertTailList(&Instance->Mounts, &Mount->Link);
    ExReleaseFastMutex(&Instance->Lock);
    LxEventSourceSignal(&Instance->MountEvents);
    LxVcacheInvalidate();
    if (Out != NULL)
        *Out = Mount;
    return 0;
}

static
LONG
LxInstanceMountSynthetic(
    _In_ PLX_INSTANCE Instance,
    _In_ PCSTR Target,
    _In_ PCSTR Source,
    _In_ PCSTR FsType,
    _In_ ULONG Flags,
    _In_ LX_FS_KIND Kind,
    _In_ ULONG64 Magic,
    _Out_opt_ PLX_MOUNT *Out)
{
    PLX_MOUNT Mount = LxAlloc(sizeof(*Mount));

    if (Mount == NULL)
        return -LX_ENOMEM;

    Mount->Kind = Kind;
    Mount->Instance = Instance;
    Mount->Target = LxStrDup(Target);
    Mount->Source = LxStrDup(Source);
    Mount->FsType = LxStrDup(FsType);
    Mount->Root = LxStrDup("/");
    Mount->Flags = Flags;
    Mount->Magic = Magic;

    ExAcquireFastMutex(&Instance->Lock);
    Mount->Id = Instance->NextMountId++;
    Mount->ParentId = 20;
    Mount->Dev = 0x10 + Mount->Id;
    InsertTailList(&Instance->Mounts, &Mount->Link);
    ExReleaseFastMutex(&Instance->Lock);
    LxEventSourceSignal(&Instance->MountEvents);
    LxVcacheInvalidate();
    if (Out != NULL)
        *Out = Mount;
    return 0;
}

static
LONG
LxInstanceTmpfs(
    _In_ PLX_INSTANCE Instance,
    _In_ PCSTR Target,
    _In_ PCSTR FsType,
    _In_ ULONG Flags,
    _In_ ULONG Mode,
    _Out_opt_ PUNICODE_STRING Root,
    _Out_opt_ PLX_MOUNT *Out)
{
    UNICODE_STRING NtRoot;
    WCHAR Suffix[16];
    LONG Error;
    ULONG Id;

    ExAcquireFastMutex(&Instance->Lock);
    Id = Instance->NextTmpId++;
    ExReleaseFastMutex(&Instance->Lock);

    RtlStringCbPrintfW(Suffix, sizeof(Suffix), L"\\%lu", Id);
    NtRoot.MaximumLength = Instance->TmpNt.Length + (USHORT)(wcslen(Suffix) * sizeof(WCHAR)) + sizeof(WCHAR);
    NtRoot.Length = 0;
    NtRoot.Buffer = LxAlloc(NtRoot.MaximumLength);
    if (NtRoot.Buffer == NULL)
        return -LX_ENOMEM;
    LxUnicodeAppend(&NtRoot, Instance->TmpNt.Buffer, Instance->TmpNt.Length);
    LxUnicodeAppend(&NtRoot, Suffix, (ULONG)(wcslen(Suffix) * sizeof(WCHAR)));

    Error = LxNtCreateDirectory(&NtRoot, Mode);
    if (Error == 0)
        Error = LxInstanceMountNt(Instance, Target, FsType, FsType, Flags, &NtRoot, LX_MAGIC_TMPFS, Out);
    if ((Error == 0) && (Root != NULL))
    {
        *Root = NtRoot;
        return 0;
    }
    LxFree(NtRoot.Buffer);
    return Error;
}

static
VOID
LxPopulateDev(
    _In_ PCUNICODE_STRING DevRoot)
{
    static const struct
    {
        PCSTR Name;
        ULONG Mode;
        ULONG Major;
        ULONG Minor;
        PCSTR Link;
    } Nodes[] =
    {
        { "null", LX_S_IFCHR | 0666, 1, 3, NULL },
        { "zero", LX_S_IFCHR | 0666, 1, 5, NULL },
        { "full", LX_S_IFCHR | 0666, 1, 7, NULL },
        { "random", LX_S_IFCHR | 0666, 1, 8, NULL },
        { "urandom", LX_S_IFCHR | 0666, 1, 9, NULL },
        { "kmsg", LX_S_IFCHR | 0644, 1, 11, NULL },
        { "tty", LX_S_IFCHR | 0666, 5, 0, NULL },
        { "console", LX_S_IFCHR | 0600, 5, 1, NULL },
        { "ptmx", LX_S_IFCHR | 0666, 5, 2, NULL },
        { "lxwin", LX_S_IFCHR | 0666, LX_WIN_DEVICE_MAJOR, LX_WIN_DEVICE_MINOR, NULL },
        { "fd", LX_S_IFLNK | 0777, 0, 0, "/proc/self/fd" },
        { "stdin", LX_S_IFLNK | 0777, 0, 0, "/proc/self/fd/0" },
        { "stdout", LX_S_IFLNK | 0777, 0, 0, "/proc/self/fd/1" },
        { "stderr", LX_S_IFLNK | 0777, 0, 0, "/proc/self/fd/2" },
        { "core", LX_S_IFLNK | 0777, 0, 0, "/proc/kcore" },
    };
    static const PCSTR Directories[] = { "pts", "shm", "mqueue", "hugepages" };
    ULONG Index;

    for (Index = 0; Index < RTL_NUMBER_OF(Nodes); Index++)
    {
        LX_MOUNT Mount;
        UNICODE_STRING Path;

        RtlZeroMemory(&Mount, sizeof(Mount));
        Mount.NtRoot = *DevRoot;
        if (LxNtPath(&Mount, Nodes[Index].Name, &Path) == 0)
        {
            LxNtCreateNode(&Path, Nodes[Index].Mode, ((ULONG64)Nodes[Index].Major << 32) | Nodes[Index].Minor,
                           Nodes[Index].Link);
            LxFreePaged(Path.Buffer);
        }
    }

    for (Index = 0; Index < RTL_NUMBER_OF(Directories); Index++)
    {
        LX_MOUNT Mount;
        UNICODE_STRING Path;

        RtlZeroMemory(&Mount, sizeof(Mount));
        Mount.NtRoot = *DevRoot;
        if (LxNtPath(&Mount, Directories[Index], &Path) == 0)
        {
            LxNtCreateDirectory(&Path, (Index == 1) ? 01777 : 0755);
            LxFreePaged(Path.Buffer);
        }
    }
}

PLX_INSTANCE
LxInstanceCreate(
    _In_ PCUNICODE_STRING RootNt,
    _In_ BOOLEAN Boot)
{
    static const WCHAR TmpSuffix[] = L".lxtmp";
    PLX_INSTANCE Instance;
    UNICODE_STRING DevRoot;
    UCHAR Random[16];
    ULONG Index;

    Instance = LxAlloc(sizeof(*Instance));
    if (Instance == NULL)
        return NULL;

    Instance->RefCount = 1;
    ExInitializeFastMutex(&Instance->Lock);
    InitializeListHead(&Instance->Mounts);
    InitializeListHead(&Instance->Ptys);
    LxEventSourceInit(&Instance->MountEvents);
    Instance->NextMountId = 20;
    Instance->NextPid = 1;
    Instance->Booted = Boot;
    RtlCopyMemory(Instance->Hostname, "localhost", sizeof("localhost"));
    RtlCopyMemory(Instance->DomainName, "(none)", sizeof("(none)"));
    Instance->BootTime = (LONG64)KeQueryInterruptTime();

    Instance->RootNt.MaximumLength = RootNt->Length + sizeof(WCHAR);
    Instance->RootNt.Buffer = LxAlloc(Instance->RootNt.MaximumLength);
    Instance->TmpNt.MaximumLength = RootNt->Length + sizeof(TmpSuffix) + 16 * sizeof(WCHAR);
    Instance->TmpNt.Buffer = LxAlloc(Instance->TmpNt.MaximumLength);
    Instance->RootCgroup = LxCgroupCreateRoot();
    KeInitializeEvent(&Instance->InitExited, NotificationEvent, FALSE);
    if (Instance->RootCgroup != NULL)
        Instance->RootCgroup->Instance = Instance;
    if ((Instance->RootNt.Buffer == NULL) || (Instance->TmpNt.Buffer == NULL) || (Instance->RootCgroup == NULL))
    {
        LxFree(Instance->RootNt.Buffer);
        LxFree(Instance->TmpNt.Buffer);
        if (Instance->RootCgroup != NULL)
            LxCgroupFreeTree(Instance->RootCgroup);
        LxFree(Instance);
        return NULL;
    }
    LxUnicodeAppend(&Instance->RootNt, RootNt->Buffer, RootNt->Length);
    LxUnicodeAppend(&Instance->TmpNt, RootNt->Buffer, RootNt->Length);
    LxUnicodeAppend(&Instance->TmpNt, TmpSuffix, sizeof(TmpSuffix) - sizeof(WCHAR));

    if (NT_SUCCESS(LxFillRandom(Random, sizeof(Random))))
    {
        static const CHAR Hex[] = "0123456789abcdef";
        ULONG Out = 0;

        Random[6] = (Random[6] & 0x0F) | 0x40;
        Random[8] = (Random[8] & 0x3F) | 0x80;
        for (Index = 0; Index < 16; Index++)
        {
            if ((Index == 4) || (Index == 6) || (Index == 8) || (Index == 10))
                Instance->BootId[Out++] = '-';
            Instance->BootId[Out++] = Hex[Random[Index] >> 4];
            Instance->BootId[Out++] = Hex[Random[Index] & 15];
        }
        Instance->BootId[Out] = '\0';
    }

    {
        PLX_INSTANCE Other = LxInstanceFind(RootNt);
        WCHAR Id[16];

        if (Other != NULL)
            LxInstanceDereference(Other);
        else
            LxNtDeleteTree(&Instance->TmpNt);
        LxNtCreateDirectory(&Instance->TmpNt, 0755);
        RtlStringCbPrintfW(Id, sizeof(Id), L"\\%lu", (ULONG)InterlockedIncrement(&LxInstanceCounter));
        LxUnicodeAppend(&Instance->TmpNt, Id, (ULONG)(wcslen(Id) * sizeof(WCHAR)));
        LxNtCreateDirectory(&Instance->TmpNt, 0755);
    }

    LxInstanceMountNt(Instance, "/", "/dev/root", "lxfs", 0, &Instance->RootNt, LX_MAGIC_NTFS, NULL);
    LxInstanceMountSynthetic(Instance, "/proc", "proc", "proc", LX_MS_NOSUID | LX_MS_NODEV | LX_MS_NOEXEC,
                             LxFsProc, LX_MAGIC_PROC, NULL);
    LxInstanceMountSynthetic(Instance, "/sys", "sysfs", "sysfs", LX_MS_NOSUID | LX_MS_NODEV | LX_MS_NOEXEC,
                             LxFsSys, LX_MAGIC_SYSFS, NULL);
    LxInstanceMountSynthetic(Instance, "/sys/fs/cgroup", "cgroup2", "cgroup2",
                             LX_MS_NOSUID | LX_MS_NODEV | LX_MS_NOEXEC, LxFsCgroup, LX_MAGIC_CGROUP2, NULL);
    if (LxInstanceTmpfs(Instance, "/dev", "devtmpfs", LX_MS_NOSUID, 0755, &DevRoot, NULL) == 0)
    {
        LxPopulateDev(&DevRoot);
        LxFree(DevRoot.Buffer);
    }
    LxInstanceMountSynthetic(Instance, "/dev/pts", "devpts", "devpts", LX_MS_NOSUID | LX_MS_NOEXEC,
                             LxFsDevpts, LX_MAGIC_DEVPTS, NULL);
    LxInstanceTmpfs(Instance, "/dev/shm", "tmpfs", LX_MS_NOSUID | LX_MS_NODEV, 01777, NULL, NULL);
    LxInstanceTmpfs(Instance, "/run", "tmpfs", LX_MS_NOSUID | LX_MS_NODEV, 0755, NULL, NULL);

    ExAcquireFastMutex(&LxInstanceListLock);
    InsertTailList(&LxInstanceList, &Instance->Link);
    ExReleaseFastMutex(&LxInstanceListLock);
    return Instance;
}

PLX_INSTANCE
LxInstanceFind(
    _In_ PCUNICODE_STRING RootNt)
{
    PLIST_ENTRY Entry;
    PLX_INSTANCE Found = NULL;

    ExAcquireFastMutex(&LxInstanceListLock);
    for (Entry = LxInstanceList.Flink; Entry != &LxInstanceList; Entry = Entry->Flink)
    {
        PLX_INSTANCE Instance = CONTAINING_RECORD(Entry, LX_INSTANCE, Link);

        if (RtlEqualUnicodeString(&Instance->RootNt, RootNt, TRUE))
        {
            LxInstanceReference(Instance);
            Found = Instance;
            break;
        }
    }
    ExReleaseFastMutex(&LxInstanceListLock);
    return Found;
}

VOID
LxInstanceReference(
    _In_ PLX_INSTANCE Instance)
{
    InterlockedIncrement(&Instance->RefCount);
}

VOID
LxInstanceDereference(
    _In_ PLX_INSTANCE Instance)
{
    if (InterlockedDecrement(&Instance->RefCount) != 0)
        return;

    ExAcquireFastMutex(&LxInstanceListLock);
    RemoveEntryList(&Instance->Link);
    ExReleaseFastMutex(&LxInstanceListLock);

    while (!IsListEmpty(&Instance->Mounts))
    {
        PLX_MOUNT Mount = CONTAINING_RECORD(RemoveHeadList(&Instance->Mounts), LX_MOUNT, Link);

        LxFree(Mount->Target);
        LxFree(Mount->Source);
        LxFree(Mount->FsType);
        LxFree(Mount->Root);
        LxFree(Mount->NtRoot.Buffer);
        LxFree(Mount);
    }
    LxVcachePurgeInstance(Instance);
    LxIpcInstanceCleanup(Instance);
    if (Instance->Console != NULL)
        LxTtyDereference(Instance->Console);
    LxCgroupFreeTree(Instance->RootCgroup);
    LxNtDeleteTree(&Instance->TmpNt);
    LxFree(Instance->RootNt.Buffer);
    LxFree(Instance->TmpNt.Buffer);
    LxFree(Instance);
}

ULONG
LxInstanceAllocatePid(
    _In_ PLX_INSTANCE Instance)
{
    ULONG Pid;

    for (;;)
    {
        PLX_PROCESS Existing;
        PLX_PROCESS Owner = NULL;
        PLX_THREAD Thread;

        ExAcquireFastMutex(&Instance->Lock);
        Pid = Instance->NextPid++;
        if (Instance->NextPid > 4194304)
            Instance->NextPid = 300;
        ExReleaseFastMutex(&Instance->Lock);

        Existing = LxProcessLookup(Instance, Pid);
        if (Existing != NULL)
        {
            LxProcessDereference(Existing);
            continue;
        }
        Thread = LxThreadLookupReferenced(Instance, Pid, &Owner);
        if (Owner != NULL)
        {
            LxProcessDereference(Owner);
            continue;
        }
        UNREFERENCED_PARAMETER(Thread);
        return Pid;
    }
}

PLX_MOUNT
LxMountFind(
    _In_ PLX_INSTANCE Instance,
    _In_ PCSTR Path,
    _Out_opt_ PCSTR *Relative)
{
    PLIST_ENTRY Entry;
    PLX_MOUNT Best = NULL;
    SIZE_T BestLength = 0;

    ExAcquireFastMutex(&Instance->Lock);
    for (Entry = Instance->Mounts.Flink; Entry != &Instance->Mounts; Entry = Entry->Flink)
    {
        PLX_MOUNT Mount = CONTAINING_RECORD(Entry, LX_MOUNT, Link);
        SIZE_T Length = strlen(Mount->Target);
        BOOLEAN Match;

        if (strcmp(Mount->Target, "/") == 0)
            Match = TRUE;
        else
            Match = (strncmp(Path, Mount->Target, Length) == 0) && ((Path[Length] == '\0') || (Path[Length] == '/'));
        if (!Match && (Mount->Alias != NULL))
        {
            Length = strlen(Mount->Alias);
            Match = (strncmp(Path, Mount->Alias, Length) == 0) && ((Path[Length] == '\0') || (Path[Length] == '/'));
        }

        if (Match && ((Best == NULL) || (Length >= BestLength)))
        {
            Best = Mount;
            BestLength = (strcmp(Mount->Target, "/") == 0) ? 0 : Length;
        }
    }
    ExReleaseFastMutex(&Instance->Lock);

    if ((Best != NULL) && (Relative != NULL))
    {
        PCSTR Rest = Path + BestLength;

        while (*Rest == '/')
            Rest++;
        *Relative = Rest;
    }
    return Best;
}

LONG
LxMountAdd(
    _In_ PLX_INSTANCE Instance,
    _In_ PCSTR Target,
    _In_ PCSTR Source,
    _In_ PCSTR FsType,
    _In_ ULONG Flags,
    _In_opt_ PCSTR Data)
{
    PCSTR Relative;
    PLX_MOUNT Existing;

    UNREFERENCED_PARAMETER(Data);

    Existing = LxMountFind(Instance, Target, &Relative);
    if ((Existing != NULL) && (Relative[0] == '\0') && (strcmp(Existing->FsType, FsType) == 0))
        return 0;

    if ((strcmp(FsType, "tmpfs") == 0) || (strcmp(FsType, "devtmpfs") == 0) || (strcmp(FsType, "mqueue") == 0) ||
        (strcmp(FsType, "ramfs") == 0) || (strcmp(FsType, "hugetlbfs") == 0) || (strcmp(FsType, "configfs") == 0) ||
        (strcmp(FsType, "fusectl") == 0) || (strcmp(FsType, "tracefs") == 0) || (strcmp(FsType, "debugfs") == 0) ||
        (strcmp(FsType, "securityfs") == 0) || (strcmp(FsType, "pstore") == 0) || (strcmp(FsType, "bpf") == 0) ||
        (strcmp(FsType, "efivarfs") == 0) || (strcmp(FsType, "binfmt_misc") == 0) || (strcmp(FsType, "autofs") == 0))
    {
        return LxInstanceTmpfs(Instance, Target, FsType, Flags, 01777, NULL, NULL);
    }
    if (strcmp(FsType, "proc") == 0)
        return LxInstanceMountSynthetic(Instance, Target, Source, "proc", Flags, LxFsProc, LX_MAGIC_PROC, NULL);
    if (strcmp(FsType, "sysfs") == 0)
        return LxInstanceMountSynthetic(Instance, Target, Source, "sysfs", Flags, LxFsSys, LX_MAGIC_SYSFS, NULL);
    if (strcmp(FsType, "cgroup2") == 0)
        return LxInstanceMountSynthetic(Instance, Target, Source, "cgroup2", Flags, LxFsCgroup, LX_MAGIC_CGROUP2, NULL);
    if (strcmp(FsType, "devpts") == 0)
        return LxInstanceMountSynthetic(Instance, Target, Source, "devpts", Flags, LxFsDevpts, LX_MAGIC_DEVPTS, NULL);
    return -LX_ENODEV;
}

LONG
LxMountRemove(
    _In_ PLX_INSTANCE Instance,
    _In_ PCSTR Target)
{
    PLIST_ENTRY Entry;
    PLX_MOUNT Found = NULL;

    ExAcquireFastMutex(&Instance->Lock);
    for (Entry = Instance->Mounts.Blink; Entry != &Instance->Mounts; Entry = Entry->Blink)
    {
        PLX_MOUNT Mount = CONTAINING_RECORD(Entry, LX_MOUNT, Link);

        if ((strcmp(Mount->Target, Target) == 0) && (strcmp(Target, "/") != 0))
        {
            Found = Mount;
            RemoveEntryList(&Mount->Link);
            InitializeListHead(&Mount->Link);
            break;
        }
    }
    ExReleaseFastMutex(&Instance->Lock);
    if (Found != NULL)
    {
        LxEventSourceSignal(&Instance->MountEvents);
        LxVcacheInvalidate();
    }
    return (Found != NULL) ? 0 : -LX_EINVAL;
}

static
PSTR
LxMountDetachedTarget(
    _In_ PLX_INSTANCE Instance)
{
    ULONG Id;

    ExAcquireFastMutex(&Instance->Lock);
    Id = ++Instance->NextDetachedId;
    ExReleaseFastMutex(&Instance->Lock);
    return LxStrPrintf("/.lxcore-detached/%lu", Id);
}

static
VOID
LxMountMarkDetached(
    _In_ PLX_INSTANCE Instance,
    _In_ PLX_MOUNT Mount)
{
    ExAcquireFastMutex(&Instance->Lock);
    Mount->Detached = TRUE;
    ExReleaseFastMutex(&Instance->Lock);
}

LONG
LxMountCreateDetached(
    _In_ PLX_INSTANCE Instance,
    _In_ PCSTR FsType,
    _In_ PCSTR Source,
    _In_ ULONG Flags,
    _In_ ULONG Mode,
    _Out_ PLX_MOUNT *Mount)
{
    PSTR Target = LxMountDetachedTarget(Instance);
    LONG Error;

    *Mount = NULL;
    if (Target == NULL)
        return -LX_ENOMEM;

    if ((strcmp(FsType, "proc") == 0))
        Error = LxInstanceMountSynthetic(Instance, Target, Source, "proc", Flags, LxFsProc, LX_MAGIC_PROC, Mount);
    else if (strcmp(FsType, "sysfs") == 0)
        Error = LxInstanceMountSynthetic(Instance, Target, Source, "sysfs", Flags, LxFsSys, LX_MAGIC_SYSFS, Mount);
    else if (strcmp(FsType, "cgroup2") == 0)
        Error = LxInstanceMountSynthetic(Instance, Target, Source, "cgroup2", Flags, LxFsCgroup, LX_MAGIC_CGROUP2, Mount);
    else if (strcmp(FsType, "devpts") == 0)
        Error = LxInstanceMountSynthetic(Instance, Target, Source, "devpts", Flags, LxFsDevpts, LX_MAGIC_DEVPTS, Mount);
    else if ((strcmp(FsType, "tmpfs") == 0) || (strcmp(FsType, "ramfs") == 0) || (strcmp(FsType, "devtmpfs") == 0) ||
             (strcmp(FsType, "mqueue") == 0))
        Error = LxInstanceTmpfs(Instance, Target, FsType, Flags, Mode, NULL, Mount);
    else
        Error = -LX_ENODEV;

    if (Error == 0)
        LxMountMarkDetached(Instance, *Mount);
    LxFree(Target);
    return Error;
}

LONG
LxMountBind(
    _In_ PLX_INSTANCE Instance,
    _In_ PLX_NODE Source,
    _In_ PCSTR Target,
    _In_ ULONG Flags)
{
    PLX_MOUNT Mount;
    LONG Error;

    if ((Source->Mount == NULL) || (Source->Mount->Kind != LxFsNt))
        return -LX_EINVAL;
    Error = LxInstanceMountNt(Instance, Target, Source->Mount->Source, Source->Mount->FsType,
                              (Source->Mount->Flags & ~LX_MS_RDONLY) | (Flags & LX_MS_RDONLY),
                              &Source->NtPath, Source->Mount->Magic, &Mount);
    if (Error == 0)
    {
        LxFree(Mount->Root);
        Mount->Root = LxStrPrintf("/%s", Source->Relative);
        if (Mount->Root == NULL)
            Mount->Root = LxStrDup("/");
        Mount->Dev = Source->Mount->Dev;
    }
    return Error;
}

LONG
LxMountBindDetached(
    _In_ PLX_INSTANCE Instance,
    _In_ PLX_NODE Source,
    _Out_ PLX_MOUNT *Mount)
{
    PSTR Target = LxMountDetachedTarget(Instance);
    LONG Error;

    *Mount = NULL;
    if (Target == NULL)
        return -LX_ENOMEM;
    if ((Source->Mount == NULL) || (Source->Mount->Kind != LxFsNt))
    {
        LxFree(Target);
        return -LX_EINVAL;
    }
    Error = LxInstanceMountNt(Instance, Target, Source->Mount->Source, Source->Mount->FsType, Source->Mount->Flags,
                              &Source->NtPath, Source->Mount->Magic, Mount);
    if (Error == 0)
    {
        (*Mount)->Dev = Source->Mount->Dev;
        LxMountMarkDetached(Instance, *Mount);
    }
    LxFree(Target);
    return Error;
}

LONG
LxMountMove(
    _In_ PLX_INSTANCE Instance,
    _In_ PLX_MOUNT Mount,
    _In_ PCSTR Target)
{
    PSTR NewTarget = LxStrDup(Target), OldTarget;

    if (NewTarget == NULL)
        return -LX_ENOMEM;
    if (strcmp(Mount->Target, "/") == 0)
    {
        LxFree(NewTarget);
        return -LX_EINVAL;
    }

    ExAcquireFastMutex(&Instance->Lock);
    OldTarget = Mount->Target;
    Mount->Target = NewTarget;
    if (Mount->Detached && (Mount->Alias == NULL))
    {
        Mount->Alias = OldTarget;
        OldTarget = NULL;
    }
    Mount->Detached = FALSE;
    RemoveEntryList(&Mount->Link);
    InsertTailList(&Instance->Mounts, &Mount->Link);
    ExReleaseFastMutex(&Instance->Lock);
    LxFree(OldTarget);
    LxEventSourceSignal(&Instance->MountEvents);
    LxVcacheInvalidate();
    return 0;
}

VOID
LxMountSetFlags(
    _In_ PLX_INSTANCE Instance,
    _In_ PLX_MOUNT Mount,
    _In_ ULONG Set,
    _In_ ULONG Clear)
{
    ExAcquireFastMutex(&Instance->Lock);
    Mount->Flags = (Mount->Flags & ~Clear) | Set;
    ExReleaseFastMutex(&Instance->Lock);
    LxEventSourceSignal(&Instance->MountEvents);
    LxVcacheInvalidate();
}

static
VOID
LxMountOptions(
    _In_ PLX_MOUNT Mount,
    _Out_writes_(Size) PSTR Buffer,
    _In_ SIZE_T Size)
{
    RtlStringCbPrintfA(Buffer, Size, "%s%s%s%s%s",
                       (Mount->Flags & LX_MS_RDONLY) ? "ro" : "rw",
                       (Mount->Flags & LX_MS_NOSUID) ? ",nosuid" : "",
                       (Mount->Flags & LX_MS_NODEV) ? ",nodev" : "",
                       (Mount->Flags & LX_MS_NOEXEC) ? ",noexec" : "",
                       ",relatime");
}

PSTR
LxMountInfo(
    _In_ PLX_PROCESS Process,
    _In_ BOOLEAN Full,
    _Out_ PULONG Length)
{
    PLX_INSTANCE Instance = Process->Instance;
    PLIST_ENTRY Entry;
    SIZE_T Size = 16384, Used = 0;
    PSTR Buffer = LxAlloc(Size);

    *Length = 0;
    if (Buffer == NULL)
        return NULL;

    ExAcquireFastMutex(&Instance->Lock);
    for (Entry = Instance->Mounts.Flink; Entry != &Instance->Mounts; Entry = Entry->Flink)
    {
        PLX_MOUNT Mount = CONTAINING_RECORD(Entry, LX_MOUNT, Link);
        CHAR Options[64];
        size_t Remaining = Size - Used;
        PSTR End;
        NTSTATUS Status;

        if (Mount->Detached)
            continue;
        LxMountOptions(Mount, Options, sizeof(Options));
        if (Full)
        {
            Status = RtlStringCbPrintfExA(Buffer + Used, Remaining, &End, NULL, 0,
                                          "%lu %lu 0:%lu %s %s %s shared:%lu - %s %s %s\n",
                                          Mount->Id, (Mount->Id == 20) ? 1 : Mount->ParentId, Mount->Dev & 0xFF,
                                          Mount->Root, Mount->Target, Options, Mount->Id,
                                          Mount->FsType, Mount->Source,
                                          (Mount->Flags & LX_MS_RDONLY) ? "ro" : "rw");
        }
        else
        {
            Status = RtlStringCbPrintfExA(Buffer + Used, Remaining, &End, NULL, 0, "%s %s %s %s 0 0\n",
                                          Mount->Source, Mount->Target, Mount->FsType, Options);
        }
        if (!NT_SUCCESS(Status))
            break;
        Used = End - Buffer;
    }
    ExReleaseFastMutex(&Instance->Lock);

    *Length = (ULONG)Used;
    return Buffer;
}

LONG
LxMountStatfs(
    _In_ PLX_MOUNT Mount,
    _Out_writes_bytes_(120) PVOID Statfs)
{
    struct
    {
        ULONG64 Type;
        ULONG64 Bsize;
        ULONG64 Blocks;
        ULONG64 Bfree;
        ULONG64 Bavail;
        ULONG64 Files;
        ULONG64 Ffree;
        ULONG Fsid[2];
        ULONG64 Namelen;
        ULONG64 Frsize;
        ULONG64 Flags;
        ULONG64 Spare[4];
    } *Result = Statfs;

    C_ASSERT(sizeof(*Result) == 120);

    RtlZeroMemory(Result, sizeof(*Result));
    Result->Type = Mount->Magic;
    Result->Bsize = 4096;
    Result->Frsize = 4096;
    Result->Namelen = 255;
    Result->Fsid[0] = Mount->Dev;
    Result->Flags = 0x20 | ((Mount->Flags & LX_MS_RDONLY) ? 1 : 0) | ((Mount->Flags & LX_MS_NOSUID) ? 2 : 0) |
                    ((Mount->Flags & LX_MS_NODEV) ? 4 : 0) | ((Mount->Flags & LX_MS_NOEXEC) ? 8 : 0);
    Result->Files = 1000000;
    Result->Ffree = 900000;

    if (Mount->Kind == LxFsNt)
    {
        FILE_FS_FULL_SIZE_INFORMATION Size;
        IO_STATUS_BLOCK Iosb;
        UNICODE_STRING Root;
        HANDLE Handle;

        if ((LxNtPath(Mount, "", &Root) == 0) &&
            (LxNtOpenHandle(&Root, FILE_READ_ATTRIBUTES, FILE_OPEN, 0, 0, &Handle) == 0))
        {
            if (NT_SUCCESS(ZwQueryVolumeInformationFile(Handle, &Iosb, &Size, sizeof(Size), FileFsFullSizeInformation)))
            {
                ULONG64 Unit = (ULONG64)Size.SectorsPerAllocationUnit * Size.BytesPerSector;

                Result->Bsize = Result->Frsize = Unit;
                Result->Blocks = (ULONG64)Size.TotalAllocationUnits.QuadPart;
                Result->Bfree = (ULONG64)Size.ActualAvailableAllocationUnits.QuadPart;
                Result->Bavail = (ULONG64)Size.CallerAvailableAllocationUnits.QuadPart;
            }
            ZwClose(Handle);
        }
        LxFreePaged(Root.Buffer);
    }
    return 0;
}

VOID
LxNodeFree(
    _Inout_ PLX_NODE Node)
{
    LxFree(Node->Path);
    LxFreePaged(Node->NtPath.Buffer);
    LxFree(Node->LinkTarget);
    RtlZeroMemory(Node, sizeof(*Node));
}

static
LONG
LxNtReadLink(
    _In_ PCUNICODE_STRING NtPath,
    _Out_ PSTR *Target)
{
    IO_STATUS_BLOCK Iosb;
    HANDLE Handle;
    PSTR Buffer;
    NTSTATUS Status;
    LONG Error;

    *Target = NULL;
    Error = LxNtOpenHandle(NtPath, FILE_READ_DATA, FILE_OPEN, FILE_NON_DIRECTORY_FILE, 0, &Handle);
    if (Error != 0)
        return Error;

    Buffer = LxAlloc(LX_PATH_MAX);
    if (Buffer == NULL)
    {
        ZwClose(Handle);
        return -LX_ENOMEM;
    }
    Status = ZwReadFile(Handle, NULL, NULL, NULL, &Iosb, Buffer, LX_PATH_MAX - 1, NULL, NULL);
    ZwClose(Handle);
    if (!NT_SUCCESS(Status) && (Status != STATUS_END_OF_FILE))
    {
        LxFree(Buffer);
        return -LxStatusToErrno(Status);
    }
    Buffer[NT_SUCCESS(Status) ? Iosb.Information : 0] = '\0';
    *Target = Buffer;
    return 0;
}

static
LONG
LxNodeQuery(
    _In_ PLX_PROCESS Process,
    _In_ PCSTR Path,
    _Out_ PLX_MOUNT *MountOut,
    _Out_ PCSTR *RelativeOut,
    _Out_ PULONG Type,
    _Out_opt_ PSTR *LinkTarget)
{
    FILE_NETWORK_OPEN_INFORMATION Information;
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING NtPath;
    PLX_MOUNT Mount;
    PCSTR Relative;
    NTSTATUS Status;
    LONG Error;

    if (LinkTarget != NULL)
        *LinkTarget = NULL;
    *Type = 0;

    Mount = LxMountFind(Process->Instance, Path, &Relative);
    *MountOut = Mount;
    *RelativeOut = Relative;
    if (Mount == NULL)
        return -LX_ENOENT;

    if (Mount->Kind != LxFsNt)
        return LxSynthQuery(Process, Mount, Relative, Type, LinkTarget);

    Error = LxNtPath(Mount, Relative, &NtPath);
    if (Error != 0)
        return Error;

    InitializeObjectAttributes(&ObjectAttributes, &NtPath, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = ZwQueryFullAttributesFile(&ObjectAttributes, &Information);
    if (!NT_SUCCESS(Status))
    {
        LxFreePaged(NtPath.Buffer);
        if (Status == STATUS_NOT_A_DIRECTORY)
            return -LX_ENOTDIR;
        Error = -LxStatusToErrno(Status);
        return (Error == -LX_ENOTDIR) ? -LX_ENOENT : Error;
    }

    if (Information.FileAttributes & FILE_ATTRIBUTE_DIRECTORY)
    {
        *Type = LX_S_IFDIR;
    }
    else if (Information.FileAttributes & FILE_ATTRIBUTE_SYSTEM)
    {
        HANDLE Handle;
        ULONG Mode, Uid, Gid;
        ULONG64 Device;

        *Type = LX_S_IFREG;
        if (LxNtOpenHandle(&NtPath, FILE_READ_EA, FILE_OPEN, 0, 0, &Handle) == 0)
        {
            if ((LxNtReadLxAttributes(Handle, &Mode, &Uid, &Gid, &Device) == 0) && (Mode != MAXULONG) &&
                ((Mode & LX_S_IFMT) != 0))
            {
                *Type = Mode & LX_S_IFMT;
            }
            ZwClose(Handle);
        }
        if ((*Type == LX_S_IFLNK) && (LinkTarget != NULL))
        {
            Error = LxNtReadLink(&NtPath, LinkTarget);
            if (Error != 0)
            {
                LxFreePaged(NtPath.Buffer);
                return Error;
            }
        }
    }
    else
    {
        *Type = LX_S_IFREG;
    }

    LxFreePaged(NtPath.Buffer);
    return 0;
}

static
LONG
LxLookupFinish(
    _In_ PLX_PROCESS Process,
    _Inout_ PLX_NODE Node,
    _In_ PSTR Path,
    _In_ BOOLEAN Exists,
    _In_ ULONG Type,
    _In_opt_ PSTR LinkTarget)
{
    PCSTR Relative;
    LONG Error;

    Node->Path = Path;
    Node->Exists = Exists;
    Node->Type = Type;
    Node->LinkTarget = LinkTarget;
    Node->Mount = LxMountFind(Process->Instance, Path, &Relative);
    Node->Relative = Relative;
    if ((Node->Mount != NULL) && (Node->Mount->Kind == LxFsNt))
    {
        Error = LxNtPath(Node->Mount, Relative, &Node->NtPath);
        if (Error != 0)
        {
            LxNodeFree(Node);
            return Error;
        }
    }
    return 0;
}

static
LONG
LxLookupWalk(
    _In_ PLX_PROCESS Process,
    _In_ LONG DirFd,
    _In_ PCSTR Path,
    _In_ ULONG Flags,
    _Out_ PLX_NODE Node,
    _Out_ PBOOLEAN Synthetic)
{
    PSTR Current = NULL, Rest = NULL, Candidate = NULL, Normalized = NULL, Root = NULL;
    SIZE_T PathLength = strlen(Path);
    ULONG Links = 0, Type = 0;
    PSTR LinkTarget = NULL;
    PLX_MOUNT Mount = NULL, StartMount = NULL;
    PCSTR Relative;
    LONG Error = 0;

    *Synthetic = FALSE;
    RtlZeroMemory(Node, sizeof(*Node));
    if (PathLength == 0)
        return -LX_ENOENT;
    if (PathLength >= LX_PATH_MAX)
        return -LX_ENAMETOOLONG;

    if ((PathLength > 1) && (Path[PathLength - 1] == '/'))
        Flags |= LX_LOOKUP_DIRECTORY | LX_LOOKUP_FOLLOW;

    if ((Path[0] == '/') && (Flags & LX_LOOKUP_BENEATH))
        return -LX_EXDEV;

    if ((Path[0] == '/') && !(Flags & LX_LOOKUP_IN_ROOT))
    {
        Current = LxStrDup("/");
    }
    else if (DirFd == LX_AT_FDCWD)
    {
        ExAcquireFastMutex(&Process->Lock);
        Current = LxStrDup(Process->Cwd);
        ExReleaseFastMutex(&Process->Lock);
    }
    else
    {
        PLX_FILE File = LxFdGet(Process, DirFd);

        if (File == NULL)
            return -LX_EBADF;
        if ((File->Type != LX_S_IFDIR) || (File->Path == NULL))
        {
            LxFileDereference(File);
            return -LX_ENOTDIR;
        }
        Current = LxStrDup(File->Path);
        LxFileDereference(File);
    }
    if (Current == NULL)
        return -LX_ENOMEM;

    Normalized = LxAlloc(LX_PATH_MAX);
    if ((Normalized != NULL) && (Flags & (LX_LOOKUP_BENEATH | LX_LOOKUP_IN_ROOT)))
    {
        Root = LxStrDup(Current);
        if (Root == NULL)
        {
            LxFree(Normalized);
            Normalized = NULL;
        }
    }
    if (Normalized == NULL)
    {
        LxFree(Current);
        return -LX_ENOMEM;
    }
    if (Flags & LX_LOOKUP_NO_XDEV)
        StartMount = LxMountFind(Process->Instance, Current, &Relative);

    if ((strstr(Path, "..") == NULL) && !(Flags & LX_LOOKUP_RESOLVE))
    {
        PSTR Joined = (Path[0] == '/') ? LxStrDup(Path) : LxPathJoin(Current, Path);

        if ((Joined != NULL) && (LxPathNormalize(Joined, Normalized) == 0))
        {
            Error = LxNodeQuery(Process, Normalized, &Mount, &Relative, &Type, &LinkTarget);
            if ((Mount != NULL) && (Mount->Kind != LxFsNt))
                *Synthetic = TRUE;
            if ((Error == 0) &&
                ((Type != LX_S_IFLNK) || !(Flags & LX_LOOKUP_FOLLOW) ||
                 ((LinkTarget == NULL) && (Mount->Kind != LxFsNt))))
            {
                LxFree(Joined);
                LxFree(Current);
                LxFree(Root);
                if ((Flags & LX_LOOKUP_DIRECTORY) && (Type != LX_S_IFDIR) && (Type != LX_S_IFLNK))
                {
                    LxFree(LinkTarget);
                    LxFree(Normalized);
                    return -LX_ENOTDIR;
                }
                return LxLookupFinish(Process, Node, Normalized, TRUE, Type, LinkTarget);
            }
            LxFree(LinkTarget);
            LinkTarget = NULL;
        }
        LxFree(Joined);
    }

    Rest = LxStrDup(Path);
    if (Rest == NULL)
    {
        LxFree(Current);
        LxFree(Normalized);
        LxFree(Root);
        return -LX_ENOMEM;
    }
    if (Rest[0] == '/')
    {
        LxFree(Current);
        Current = LxStrDup((Root != NULL) ? Root : "/");
    }

    for (;;)
    {
        PSTR Cursor = Rest, Component, NextRest;
        BOOLEAN Last;

        while (*Cursor == '/')
            Cursor++;
        if (*Cursor == '\0')
        {
            Error = LxNodeQuery(Process, Current, &Mount, &Relative, &Type, NULL);
            if ((Mount != NULL) && (Mount->Kind != LxFsNt))
                *Synthetic = TRUE;
            if (Error == 0)
            {
                if ((Flags & LX_LOOKUP_DIRECTORY) && (Type != LX_S_IFDIR))
                    Error = -LX_ENOTDIR;
                else
                {
                    LxFree(Rest);
                    LxFree(Normalized);
                    LxFree(Root);
                    return LxLookupFinish(Process, Node, Current, TRUE, Type, NULL);
                }
            }
            break;
        }

        Component = Cursor;
        while ((*Cursor != '\0') && (*Cursor != '/'))
            Cursor++;
        NextRest = Cursor;
        if (*Cursor == '/')
            *Cursor++ = '\0';
        while (*Cursor == '/')
            Cursor++;
        Last = (*Cursor == '\0');
        NextRest = Cursor;

        if (strcmp(Component, ".") == 0)
        {
            RtlMoveMemory(Rest, NextRest, strlen(NextRest) + 1);
            continue;
        }
        if (strcmp(Component, "..") == 0)
        {
            PSTR Slash = strrchr(Current, '/');

            if ((Root != NULL) && (strcmp(Current, Root) == 0))
            {
                if (Flags & LX_LOOKUP_BENEATH)
                {
                    Error = -LX_EXDEV;
                    break;
                }
                RtlMoveMemory(Rest, NextRest, strlen(NextRest) + 1);
                continue;
            }
            if ((Slash != NULL) && (Slash != Current))
                *Slash = '\0';
            else
                Current[1] = '\0';
            if ((StartMount != NULL) && (LxMountFind(Process->Instance, Current, &Relative) != StartMount))
            {
                Error = -LX_EXDEV;
                break;
            }
            RtlMoveMemory(Rest, NextRest, strlen(NextRest) + 1);
            continue;
        }
        if (strlen(Component) > 255)
        {
            Error = -LX_ENAMETOOLONG;
            break;
        }

        Candidate = LxPathJoin(Current, Component);
        if (Candidate == NULL)
        {
            Error = -LX_ENAMETOOLONG;
            break;
        }

        Error = LxNodeQuery(Process, Candidate, &Mount, &Relative, &Type, &LinkTarget);
        if ((Mount != NULL) && (Mount->Kind != LxFsNt))
            *Synthetic = TRUE;
        if (Error == -LX_ENOENT)
        {
            if (Last && (Flags & LX_LOOKUP_CREATE))
            {
                LxFree(Current);
                LxFree(Rest);
                LxFree(Normalized);
                LxFree(Root);
                return LxLookupFinish(Process, Node, Candidate, FALSE, 0, NULL);
            }
            break;
        }
        if (Error != 0)
            break;
        if ((StartMount != NULL) && (Mount != StartMount))
        {
            Error = -LX_EXDEV;
            break;
        }

        if ((Type == LX_S_IFLNK) && (LinkTarget == NULL) && (Mount->Kind != LxFsNt))
        {
            if ((Flags & (LX_LOOKUP_NO_MAGICLINKS | LX_LOOKUP_NO_SYMLINKS | LX_LOOKUP_BENEATH | LX_LOOKUP_IN_ROOT)) &&
                (!Last || (Flags & LX_LOOKUP_FOLLOW)))
            {
                Error = -LX_ELOOP;
                break;
            }
            if (!Last)
            {
                Error = -LX_ENOTDIR;
                break;
            }
        }
        else if ((Type == LX_S_IFLNK) && (!Last || (Flags & LX_LOOKUP_FOLLOW)))
        {
            PSTR NewRest;
            SIZE_T TargetLength, RestLength;

            if (Flags & LX_LOOKUP_NO_SYMLINKS)
            {
                Error = -LX_ELOOP;
                break;
            }
            if ((++Links > LX_MAX_SYMLINKS) || (LinkTarget == NULL) || (LinkTarget[0] == '\0'))
            {
                Error = (Links > LX_MAX_SYMLINKS) ? -LX_ELOOP : -LX_ENOENT;
                break;
            }
            if ((LinkTarget[0] == '/') && (Flags & LX_LOOKUP_BENEATH))
            {
                Error = -LX_EXDEV;
                break;
            }

            TargetLength = strlen(LinkTarget);
            RestLength = strlen(NextRest);
            if (TargetLength + 1 + RestLength >= LX_PATH_MAX)
            {
                Error = -LX_ENAMETOOLONG;
                break;
            }
            NewRest = LxAlloc(TargetLength + RestLength + 2);
            if (NewRest == NULL)
            {
                Error = -LX_ENOMEM;
                break;
            }
            RtlCopyMemory(NewRest, LinkTarget, TargetLength);
            if (RestLength != 0)
            {
                NewRest[TargetLength] = '/';
                RtlCopyMemory(NewRest + TargetLength + 1, NextRest, RestLength + 1);
            }
            else
            {
                NewRest[TargetLength] = '\0';
            }

            if (LinkTarget[0] == '/')
            {
                LxFree(Current);
                Current = LxStrDup((Root != NULL) ? Root : "/");
            }
            LxFree(Rest);
            Rest = NewRest;
            LxFree(LinkTarget);
            LinkTarget = NULL;
            LxFree(Candidate);
            Candidate = NULL;
            if (Current == NULL)
            {
                Error = -LX_ENOMEM;
                break;
            }
            continue;
        }

        if (Last)
        {
            LxFree(Current);
            LxFree(Rest);
            LxFree(Normalized);
            LxFree(Root);
            if ((Flags & LX_LOOKUP_DIRECTORY) && (Type != LX_S_IFDIR))
            {
                LxFree(LinkTarget);
                LxFree(Candidate);
                return -LX_ENOTDIR;
            }
            return LxLookupFinish(Process, Node, Candidate, TRUE, Type, LinkTarget);
        }

        if (Type != LX_S_IFDIR)
        {
            Error = -LX_ENOTDIR;
            break;
        }

        LxFree(LinkTarget);
        LinkTarget = NULL;
        LxFree(Current);
        Current = Candidate;
        Candidate = NULL;
        RtlMoveMemory(Rest, NextRest, strlen(NextRest) + 1);
    }

    LxFree(LinkTarget);
    LxFree(Candidate);
    LxFree(Current);
    LxFree(Rest);
    LxFree(Normalized);
    LxFree(Root);
    return (Error != 0) ? Error : -LX_ENOENT;
}

#define LX_VCACHE_BUCKETS   1024
#define LX_VCACHE_MAX       8192
#define LX_VCACHE_STAT_TTL  10000000LL
#define LX_VCACHE_LOOKUP    1
#define LX_VCACHE_STAT      2

typedef struct _LX_VCACHE_ENTRY
{
    LIST_ENTRY Link;
    LIST_ENTRY Lru;
    PLX_INSTANCE Instance;
    ULONG Kind;
    ULONG Flags;
    ULONG Hash;
    LONG64 Generation;
    LONG64 Time;
    LONG Error;
    BOOLEAN Exists;
    ULONG Type;
    PSTR Key;
    PSTR Resolved;
    PSTR LinkTarget;
    LX_STAT Stat;
} LX_VCACHE_ENTRY, *PLX_VCACHE_ENTRY;

static LIST_ENTRY LxVcacheBuckets[LX_VCACHE_BUCKETS];
static LIST_ENTRY LxVcacheLru;
static FAST_MUTEX LxVcacheLock;
static ULONG LxVcacheCount;
static volatile LONG64 LxVcacheGeneration = 1;

VOID
LxVcacheInitialize(VOID)
{
    ULONG Index;

    for (Index = 0; Index < LX_VCACHE_BUCKETS; Index++)
        InitializeListHead(&LxVcacheBuckets[Index]);
    InitializeListHead(&LxVcacheLru);
    ExInitializeFastMutex(&LxVcacheLock);
}

VOID
LxVcacheInvalidate(VOID)
{
    InterlockedIncrement64(&LxVcacheGeneration);
}

static
ULONG
LxVcacheHash(
    _In_ PLX_INSTANCE Instance,
    _In_ ULONG Kind,
    _In_ PCSTR Key)
{
    ULONG Hash = 2166136261UL ^ (ULONG)(ULONG_PTR)Instance ^ (Kind * 16777619UL);

    while (*Key != '\0')
    {
        Hash ^= (UCHAR)*Key++;
        Hash *= 16777619UL;
    }
    return Hash;
}

static
VOID
LxVcacheFreeEntry(
    _In_ PLX_VCACHE_ENTRY Entry)
{
    RemoveEntryList(&Entry->Link);
    RemoveEntryList(&Entry->Lru);
    LxVcacheCount--;
    LxFree(Entry->Key);
    LxFree(Entry->Resolved);
    LxFree(Entry->LinkTarget);
    LxFree(Entry);
}

static
PLX_VCACHE_ENTRY
LxVcacheFind(
    _In_ PLX_INSTANCE Instance,
    _In_ ULONG Kind,
    _In_ ULONG Flags,
    _In_ PCSTR Key,
    _In_ ULONG Hash)
{
    PLIST_ENTRY Bucket = &LxVcacheBuckets[Hash % LX_VCACHE_BUCKETS], Link;

    for (Link = Bucket->Flink; Link != Bucket; Link = Link->Flink)
    {
        PLX_VCACHE_ENTRY Entry = CONTAINING_RECORD(Link, LX_VCACHE_ENTRY, Link);

        if ((Entry->Hash == Hash) && (Entry->Instance == Instance) && (Entry->Kind == Kind) &&
            (Entry->Flags == Flags) && (strcmp(Entry->Key, Key) == 0))
        {
            return Entry;
        }
    }
    return NULL;
}

static
VOID
LxVcachePut(
    _In_ PLX_VCACHE_ENTRY New)
{
    PLX_VCACHE_ENTRY Old;

    ExAcquireFastMutex(&LxVcacheLock);
    Old = LxVcacheFind(New->Instance, New->Kind, New->Flags, New->Key, New->Hash);
    if (Old != NULL)
        LxVcacheFreeEntry(Old);
    while ((LxVcacheCount >= LX_VCACHE_MAX) && !IsListEmpty(&LxVcacheLru))
        LxVcacheFreeEntry(CONTAINING_RECORD(LxVcacheLru.Blink, LX_VCACHE_ENTRY, Lru));
    InsertHeadList(&LxVcacheBuckets[New->Hash % LX_VCACHE_BUCKETS], &New->Link);
    InsertHeadList(&LxVcacheLru, &New->Lru);
    LxVcacheCount++;
    ExReleaseFastMutex(&LxVcacheLock);
}

VOID
LxVcachePurgeInstance(
    _In_ PLX_INSTANCE Instance)
{
    PLIST_ENTRY Link, Next;

    ExAcquireFastMutex(&LxVcacheLock);
    for (Link = LxVcacheLru.Flink; Link != &LxVcacheLru; Link = Next)
    {
        PLX_VCACHE_ENTRY Entry = CONTAINING_RECORD(Link, LX_VCACHE_ENTRY, Lru);

        Next = Link->Flink;
        if (Entry->Instance == Instance)
            LxVcacheFreeEntry(Entry);
    }
    ExReleaseFastMutex(&LxVcacheLock);
}

VOID
LxVcacheInvalidatePath(
    _In_ PLX_INSTANCE Instance,
    _In_ PCSTR Path)
{
    ULONG Hash = LxVcacheHash(Instance, LX_VCACHE_STAT, Path);
    PLX_VCACHE_ENTRY Entry;

    ExAcquireFastMutex(&LxVcacheLock);
    Entry = LxVcacheFind(Instance, LX_VCACHE_STAT, 0, Path, Hash);
    if (Entry != NULL)
        LxVcacheFreeEntry(Entry);
    ExReleaseFastMutex(&LxVcacheLock);
}

static
PSTR
LxLookupCacheKey(
    _In_ PLX_PROCESS Process,
    _In_ LONG DirFd,
    _In_ PCSTR Path)
{
    PSTR Base, Joined, Key;

    if ((Path[0] == '\0') || (strstr(Path, "..") != NULL))
        return NULL;
    if (Path[0] == '/')
    {
        Joined = LxStrDup(Path);
    }
    else
    {
        if (DirFd == LX_AT_FDCWD)
        {
            ExAcquireFastMutex(&Process->Lock);
            Base = LxStrDup(Process->Cwd);
            ExReleaseFastMutex(&Process->Lock);
        }
        else
        {
            PLX_FILE File = LxFdGet(Process, DirFd);

            if (File == NULL)
                return NULL;
            Base = ((File->Type == LX_S_IFDIR) && (File->Path != NULL) && (File->Path[0] == '/')) ?
                   LxStrDup(File->Path) : NULL;
            LxFileDereference(File);
        }
        if (Base == NULL)
            return NULL;
        Joined = LxPathJoin(Base, Path);
        LxFree(Base);
    }
    if (Joined == NULL)
        return NULL;
    Key = LxAlloc(LX_PATH_MAX);
    if ((Key != NULL) && (LxPathNormalize(Joined, Key) != 0))
    {
        LxFree(Key);
        Key = NULL;
    }
    LxFree(Joined);
    return Key;
}

LONG
LxLookup(
    _In_ PLX_PROCESS Process,
    _In_ LONG DirFd,
    _In_ PCSTR Path,
    _In_ ULONG Flags,
    _Out_ PLX_NODE Node)
{
    PLX_INSTANCE Instance = Process->Instance;
    SIZE_T Length = strlen(Path);
    ULONG KeyFlags = Flags;
    LONG64 Generation = LxVcacheGeneration;
    PLX_VCACHE_ENTRY Entry;
    BOOLEAN Synthetic;
    PSTR Key;
    LONG Error;

    if ((Length > 1) && (Path[Length - 1] == '/'))
        KeyFlags |= 0x80000000;
    Key = LxLookupCacheKey(Process, DirFd, Path);
    if (Key != NULL)
    {
        ULONG Hash = LxVcacheHash(Instance, LX_VCACHE_LOOKUP, Key);
        PSTR Resolved = NULL, LinkTarget = NULL;
        BOOLEAN Hit = FALSE, Exists = FALSE;
        ULONG Type = 0;

        ExAcquireFastMutex(&LxVcacheLock);
        Entry = LxVcacheFind(Instance, LX_VCACHE_LOOKUP, KeyFlags, Key, Hash);
        if ((Entry != NULL) && (Entry->Generation == Generation))
        {
            Hit = TRUE;
            Error = Entry->Error;
            Exists = Entry->Exists;
            Type = Entry->Type;
            if (Error == 0)
            {
                Resolved = LxStrDup(Entry->Resolved);
                LinkTarget = (Entry->LinkTarget != NULL) ? LxStrDup(Entry->LinkTarget) : NULL;
                if ((Resolved == NULL) || ((Entry->LinkTarget != NULL) && (LinkTarget == NULL)))
                    Hit = FALSE;
            }
            if (Hit)
            {
                RemoveEntryList(&Entry->Lru);
                InsertHeadList(&LxVcacheLru, &Entry->Lru);
            }
        }
        ExReleaseFastMutex(&LxVcacheLock);

        if (Hit)
        {
            LxFree(Key);
            RtlZeroMemory(Node, sizeof(*Node));
            if (Error != 0)
                return Error;
            return LxLookupFinish(Process, Node, Resolved, Exists, Type, LinkTarget);
        }
        LxFree(Resolved);
        LxFree(LinkTarget);

        Error = LxLookupWalk(Process, DirFd, Path, Flags, Node, &Synthetic);
        if (!Synthetic && ((Error == 0) || (Error == -LX_ENOENT) || (Error == -LX_ENOTDIR)) &&
            ((Error != 0) || ((Node->Mount != NULL) && (Node->Mount->Kind == LxFsNt))))
        {
            Entry = LxAlloc(sizeof(*Entry));
            if (Entry != NULL)
            {
                Entry->Instance = Instance;
                Entry->Kind = LX_VCACHE_LOOKUP;
                Entry->Flags = KeyFlags;
                Entry->Hash = Hash;
                Entry->Generation = Generation;
                Entry->Error = Error;
                Entry->Key = Key;
                Key = NULL;
                if (Error == 0)
                {
                    Entry->Exists = Node->Exists;
                    Entry->Type = Node->Type;
                    Entry->Resolved = LxStrDup(Node->Path);
                    Entry->LinkTarget = (Node->LinkTarget != NULL) ? LxStrDup(Node->LinkTarget) : NULL;
                }
                if ((Error == 0) && ((Entry->Resolved == NULL) || ((Node->LinkTarget != NULL) && (Entry->LinkTarget == NULL))))
                {
                    LxFree(Entry->Resolved);
                    LxFree(Entry->LinkTarget);
                    LxFree(Entry->Key);
                    LxFree(Entry);
                }
                else
                {
                    LxVcachePut(Entry);
                }
            }
        }
        LxFree(Key);
        return Error;
    }

    return LxLookupWalk(Process, DirFd, Path, Flags, Node, &Synthetic);
}

LONG
LxVfsOpenNode(
    _In_ PLX_PROCESS Process,
    _Inout_ PLX_NODE Node,
    _In_ ULONG Flags,
    _In_ ULONG Mode,
    _Out_ PLX_FILE *Result)
{
    ULONG Disposition, Options = 0;
    ACCESS_MASK Access;
    HANDLE Handle;
    PLX_FILE File;
    LONG Error;

    *Result = NULL;

    if (Node->Mount == NULL)
        return -LX_ENOENT;

    if (Node->Mount->Kind != LxFsNt)
    {
        if (!Node->Exists)
            return (Flags & LX_O_CREAT) ? -LX_EACCES : -LX_ENOENT;
        return LxSynthOpen(Process, Node, Flags, Result);
    }

    if (Node->Exists)
    {
        if ((Flags & LX_O_CREAT) && (Flags & LX_O_EXCL))
            return -LX_EEXIST;

        switch (Node->Type)
        {
            case LX_S_IFLNK:
                if (!(Flags & LX_O_PATH))
                    return -LX_ELOOP;
                break;

            case LX_S_IFDIR:
                if ((Flags & LX_O_ACCMODE) != LX_O_RDONLY)
                    return -LX_EISDIR;
                break;

            case LX_S_IFCHR:
            case 0060000:
                if (!(Flags & LX_O_PATH))
                {
                    FILE_INTERNAL_INFORMATION Internal;
                    IO_STATUS_BLOCK Iosb;
                    ULONG ModeValue, Uid, Gid;
                    ULONG64 Device = MAXULONG64;

                    Internal.IndexNumber.QuadPart = 0;
                    if (LxNtOpenHandle(&Node->NtPath, FILE_READ_EA | FILE_READ_ATTRIBUTES, FILE_OPEN, 0, 0, &Handle) == 0)
                    {
                        LxNtReadLxAttributes(Handle, &ModeValue, &Uid, &Gid, &Device);
                        if (!NT_SUCCESS(ZwQueryInformationFile(Handle, &Iosb, &Internal, sizeof(Internal),
                                                               FileInternalInformation)))
                        {
                            Internal.IndexNumber.QuadPart = 0;
                        }
                        ZwClose(Handle);
                    }
                    File = LxDeviceOpen(Process, Device, Flags, Node->Path);
                    if (File == NULL)
                        return -LX_ENXIO;
                    File->Mount = Node->Mount;
                    if (Internal.IndexNumber.QuadPart != 0)
                        File->Ino = (ULONG64)Internal.IndexNumber.QuadPart;
                    *Result = File;
                    return 0;
                }
                break;

            case LX_S_IFIFO:
                if (!(Flags & LX_O_PATH))
                    return LxFifoOpen(Process, Node, Flags, Result);
                break;

            case 0140000:
                if (!(Flags & LX_O_PATH))
                    return -LX_ENXIO;
                break;

            default:
                break;
        }
    }
    else
    {
        if (!(Flags & LX_O_CREAT))
            return -LX_ENOENT;
        if (Node->Mount->Flags & LX_MS_RDONLY)
            return -LX_EROFS;
    }

    if (Flags & LX_O_PATH)
    {
        Access = FILE_READ_ATTRIBUTES | FILE_READ_EA;
    }
    else
    {
        switch (Flags & LX_O_ACCMODE)
        {
            case LX_O_WRONLY:
                Access = FILE_GENERIC_WRITE | FILE_READ_ATTRIBUTES | FILE_READ_EA;
                break;
            case LX_O_RDWR:
                Access = FILE_GENERIC_READ | FILE_GENERIC_WRITE;
                break;
            default:
                Access = FILE_GENERIC_READ | FILE_WRITE_EA | FILE_WRITE_ATTRIBUTES;
                break;
        }
    }

    if (!Node->Exists)
        Disposition = FILE_CREATE;
    else if ((Flags & LX_O_TRUNC) && ((Flags & LX_O_ACCMODE) != LX_O_RDONLY) && (Node->Type == LX_S_IFREG))
        Disposition = FILE_OVERWRITE;
    else
        Disposition = FILE_OPEN;

    if (!Node->Exists)
        Options |= FILE_NON_DIRECTORY_FILE;
    if (Flags & LX_O_DIRECTORY)
        Options |= FILE_DIRECTORY_FILE;

    Error = LxNtOpenHandle(&Node->NtPath, Access, Disposition, Options, FILE_ATTRIBUTE_NORMAL, &Handle);
    if ((Error == -LX_EACCES) && !(Flags & LX_O_PATH) && Node->Exists)
    {
        Access &= ~(FILE_WRITE_EA | FILE_WRITE_ATTRIBUTES);
        Error = LxNtOpenHandle(&Node->NtPath, Access, Disposition, Options, FILE_ATTRIBUTE_NORMAL, &Handle);
    }
    if (Error != 0)
        return Error;

    if (!Node->Exists)
    {
        LxNtWriteLxAttributes(Handle, LX_S_IFREG | (Mode & ~Process->Umask & 07777), 0, 0, 0, FALSE);
        LxVcacheInvalidate();
        LxInotifyEvent(Process, Node->Path, 0x00000100, FALSE);
    }

    File = LxFileFromNtHandle(Handle, Flags & (LX_O_ACCMODE | LX_O_APPEND | LX_O_NONBLOCK | LX_O_PATH));
    if (File == NULL)
    {
        ZwClose(Handle);
        return -LX_ENOMEM;
    }
    File->Path = LxStrDup(Node->Path);
    File->Mount = Node->Mount;
    if (Node->Type == LX_S_IFLNK)
        File->Type = LX_S_IFLNK;

    if ((Flags & LX_O_DIRECTORY) && !File->Directory)
    {
        LxFileDereference(File);
        return -LX_ENOTDIR;
    }

    *Result = File;
    return 0;
}

LONG
LxVfsOpen(
    _In_ PLX_PROCESS Process,
    _In_ LONG DirFd,
    _In_ PCSTR Path,
    _In_ ULONG Flags,
    _In_ ULONG Mode,
    _Out_ PLX_FILE *File)
{
    return LxVfsOpenResolve(Process, DirFd, Path, Flags, Mode, 0, File);
}

LONG
LxVfsOpenResolve(
    _In_ PLX_PROCESS Process,
    _In_ LONG DirFd,
    _In_ PCSTR Path,
    _In_ ULONG Flags,
    _In_ ULONG Mode,
    _In_ ULONG Resolve,
    _Out_ PLX_FILE *File)
{
    LX_NODE Node;
    ULONG LookupFlags = Resolve & LX_LOOKUP_RESOLVE;
    LONG Error;

    if (!(Flags & LX_O_NOFOLLOW))
        LookupFlags |= LX_LOOKUP_FOLLOW;
    if (Flags & LX_O_CREAT)
        LookupFlags |= LX_LOOKUP_CREATE;
    if (Flags & LX_O_DIRECTORY)
        LookupFlags |= LX_LOOKUP_DIRECTORY;

    Error = LxLookup(Process, DirFd, Path, LookupFlags, &Node);
    if (Error != 0)
        return Error;

    Error = LxVfsOpenNode(Process, &Node, Flags, Mode, File);
    LxNodeFree(&Node);
    return Error;
}

LONG
LxVfsStatNode(
    _In_ PLX_PROCESS Process,
    _In_ PLX_NODE Node,
    _Out_ PLX_STAT Stat)
{
    PLX_FILE File;
    HANDLE Handle;
    LONG Error;

    PLX_VCACHE_ENTRY Entry;
    LONG64 Generation = LxVcacheGeneration, Now = (LONG64)KeQueryInterruptTime();
    ULONG Hash;

    if (Node->Mount->Kind != LxFsNt)
        return LxSynthStat(Process, Node, Stat);

    Hash = LxVcacheHash(Process->Instance, LX_VCACHE_STAT, Node->Path);
    ExAcquireFastMutex(&LxVcacheLock);
    Entry = LxVcacheFind(Process->Instance, LX_VCACHE_STAT, 0, Node->Path, Hash);
    if ((Entry != NULL) && (Entry->Generation == Generation) && (Now - Entry->Time < LX_VCACHE_STAT_TTL))
    {
        *Stat = Entry->Stat;
        ExReleaseFastMutex(&LxVcacheLock);
        return 0;
    }
    ExReleaseFastMutex(&LxVcacheLock);

    Error = LxNtOpenHandle(&Node->NtPath, FILE_READ_ATTRIBUTES | FILE_READ_EA, FILE_OPEN, 0, 0, &Handle);
    if (Error != 0)
        return Error;

    File = LxFileFromNtHandle(Handle, LX_O_PATH);
    if (File == NULL)
    {
        ZwClose(Handle);
        return -LX_ENOMEM;
    }
    File->Mount = Node->Mount;
    Error = LxFileStat(File, Stat);
    LxFileDereference(File);

    if (Error == 0)
    {
        Entry = LxAlloc(sizeof(*Entry));
        if (Entry != NULL)
        {
            Entry->Key = LxStrDup(Node->Path);
            if (Entry->Key == NULL)
            {
                LxFree(Entry);
            }
            else
            {
                Entry->Instance = Process->Instance;
                Entry->Kind = LX_VCACHE_STAT;
                Entry->Hash = Hash;
                Entry->Generation = Generation;
                Entry->Time = Now;
                Entry->Stat = *Stat;
                LxVcachePut(Entry);
            }
        }
    }
    return Error;
}

LONG
LxVfsMkdir(
    _In_ PLX_PROCESS Process,
    _In_ PLX_NODE Node,
    _In_ ULONG Mode)
{
    HANDLE Handle;
    LONG Error;

    if (Node->Exists)
        return -LX_EEXIST;
    if (Node->Mount->Kind != LxFsNt)
        return LxSynthMkdir(Process, Node);
    if (Node->Mount->Flags & LX_MS_RDONLY)
        return -LX_EROFS;

    Error = LxNtOpenHandle(&Node->NtPath, FILE_LIST_DIRECTORY | FILE_WRITE_EA | FILE_READ_EA, FILE_CREATE,
                           FILE_DIRECTORY_FILE, FILE_ATTRIBUTE_NORMAL, &Handle);
    if (Error != 0)
        return Error;
    LxNtWriteLxAttributes(Handle, LX_S_IFDIR | (Mode & ~Process->Umask & 07777), 0, 0, 0, FALSE);
    ZwClose(Handle);
    LxVcacheInvalidate();
    LxInotifyEvent(Process, Node->Path, 0x00000100 | 0x40000000, FALSE);
    return 0;
}

LONG
LxVfsMknod(
    _In_ PLX_PROCESS Process,
    _In_ PLX_NODE Node,
    _In_ ULONG Mode,
    _In_ ULONG64 Device)
{
    ULONG Type = Mode & LX_S_IFMT;
    LONG Error;

    if (Node->Exists)
        return -LX_EEXIST;
    if (Node->Mount->Kind != LxFsNt)
        return -LX_EPERM;
    if (Type == 0)
        Type = LX_S_IFREG;

    Error = LxNtCreateNode(&Node->NtPath, Type | (Mode & ~Process->Umask & 07777), Device, NULL);
    LxVcacheInvalidate();
    if (Error == 0)
        LxInotifyEvent(Process, Node->Path, 0x00000100, FALSE);
    return Error;
}

LONG
LxVfsSymlink(
    _In_ PLX_PROCESS Process,
    _In_ PLX_NODE Node,
    _In_ PCSTR Target)
{
    LONG Error;

    if (Node->Exists)
        return -LX_EEXIST;
    if (Node->Mount->Kind != LxFsNt)
        return -LX_EPERM;

    Error = LxNtCreateNode(&Node->NtPath, LX_S_IFLNK | 0777, 0, Target);
    LxVcacheInvalidate();
    if (Error == 0)
        LxInotifyEvent(Process, Node->Path, 0x00000100, FALSE);
    return Error;
}

static
LONG
LxNtMoveToTrash(
    _In_ PLX_PROCESS Process,
    _In_ HANDLE Handle)
{
    PFILE_RENAME_INFORMATION Rename;
    IO_STATUS_BLOCK Iosb;
    WCHAR Name[48];
    ULONG Size, NameBytes;
    NTSTATUS Status;
    PCUNICODE_STRING Trash = &Process->Instance->TmpNt;

    RtlStringCbPrintfW(Name, sizeof(Name), L"\\.deleted.%lu", (ULONG)InterlockedIncrement(&LxTrashCounter));
    NameBytes = (ULONG)(wcslen(Name) * sizeof(WCHAR));
    Size = FIELD_OFFSET(FILE_RENAME_INFORMATION, FileName) + Trash->Length + NameBytes;
    Rename = LxAllocPaged(Size);
    if (Rename == NULL)
        return -LX_ENOMEM;

    Rename->ReplaceIfExists = TRUE;
    Rename->RootDirectory = NULL;
    Rename->FileNameLength = Trash->Length + NameBytes;
    RtlCopyMemory(Rename->FileName, Trash->Buffer, Trash->Length);
    RtlCopyMemory((PUCHAR)Rename->FileName + Trash->Length, Name, NameBytes);
    Status = ZwSetInformationFile(Handle, &Iosb, Rename, Size, FileRenameInformation);
    LxFreePaged(Rename);
    return NT_SUCCESS(Status) ? 0 : -LxStatusToErrno(Status);
}

static
BOOLEAN
LxNtDirectoryEmpty(
    _In_ HANDLE Handle)
{
    UCHAR Buffer[1024];
    IO_STATUS_BLOCK Iosb;
    BOOLEAN Restart = TRUE;

    for (;;)
    {
        PFILE_DIRECTORY_INFORMATION Entry;
        ULONG Offset = 0;
        NTSTATUS Status;

        Status = ZwQueryDirectoryFile(Handle, NULL, NULL, NULL, &Iosb, Buffer, sizeof(Buffer),
                                      FileDirectoryInformation, FALSE, NULL, Restart);
        Restart = FALSE;
        if (!NT_SUCCESS(Status))
            return TRUE;
        for (;;)
        {
            Entry = (PFILE_DIRECTORY_INFORMATION)(Buffer + Offset);
            if (!((Entry->FileNameLength == 2) && (Entry->FileName[0] == L'.')) &&
                !((Entry->FileNameLength == 4) && (Entry->FileName[0] == L'.') && (Entry->FileName[1] == L'.')))
            {
                return FALSE;
            }
            if (Entry->NextEntryOffset == 0)
                break;
            Offset += Entry->NextEntryOffset;
        }
    }
}

LONG
LxVfsUnlink(
    _In_ PLX_PROCESS Process,
    _In_ PLX_NODE Node,
    _In_ BOOLEAN Directory)
{
    FILE_DISPOSITION_INFORMATION Disposition = { TRUE };
    FILE_BASIC_INFORMATION Basic;
    IO_STATUS_BLOCK Iosb;
    HANDLE Handle;
    NTSTATUS Status;
    LONG Error;

    if (!Node->Exists)
        return -LX_ENOENT;
    if (Node->Mount->Kind != LxFsNt)
        return Directory ? LxSynthRmdir(Process, Node) : -LX_EPERM;
    if (Node->Relative[0] == '\0')
        return -LX_EBUSY;
    if (Directory && (Node->Type != LX_S_IFDIR))
        return -LX_ENOTDIR;
    if (!Directory && (Node->Type == LX_S_IFDIR))
        return -LX_EISDIR;

    Error = LxNtOpenHandle(&Node->NtPath, DELETE | FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES | FILE_LIST_DIRECTORY,
                           FILE_OPEN, Directory ? FILE_DIRECTORY_FILE : FILE_NON_DIRECTORY_FILE, 0, &Handle);
    if (Error != 0)
        return Error;

    if (Directory && !LxNtDirectoryEmpty(Handle))
    {
        ZwClose(Handle);
        return -LX_ENOTEMPTY;
    }

    RtlZeroMemory(&Basic, sizeof(Basic));
    if (NT_SUCCESS(ZwQueryInformationFile(Handle, &Iosb, &Basic, sizeof(Basic), FileBasicInformation)) &&
        (Basic.FileAttributes & FILE_ATTRIBUTE_READONLY))
    {
        Basic.FileAttributes &= ~FILE_ATTRIBUTE_READONLY;
        ZwSetInformationFile(Handle, &Iosb, &Basic, sizeof(Basic), FileBasicInformation);
    }

    LxNtMoveToTrash(Process, Handle);
    Status = ZwSetInformationFile(Handle, &Iosb, &Disposition, sizeof(Disposition), FileDispositionInformation);
    ZwClose(Handle);
    LxVcacheInvalidate();
    if (!NT_SUCCESS(Status))
        return -LxStatusToErrno(Status);
    LxInotifyEvent(Process, Node->Path, 0x00000200 | (Directory ? 0x40000000 : 0), TRUE);
    return 0;
}

LONG
LxVfsRename(
    _In_ PLX_PROCESS Process,
    _In_ PLX_NODE From,
    _In_ PLX_NODE To,
    _In_ ULONG Flags)
{
    PFILE_RENAME_INFORMATION Rename;
    IO_STATUS_BLOCK Iosb;
    HANDLE Handle;
    NTSTATUS Status;
    ULONG Size;
    LONG Error;

    if (!From->Exists)
        return -LX_ENOENT;
    if ((From->Mount != To->Mount) || (From->Mount->Kind != LxFsNt))
        return -LX_EXDEV;
    if (Flags & ~1UL)
        return -LX_EINVAL;
    if (strcmp(From->Path, To->Path) == 0)
        return 0;

    if (To->Exists)
    {
        if (Flags & 1)
            return -LX_EEXIST;
        if ((From->Type == LX_S_IFDIR) && (To->Type != LX_S_IFDIR))
            return -LX_ENOTDIR;
        if ((From->Type != LX_S_IFDIR) && (To->Type == LX_S_IFDIR))
            return -LX_EISDIR;
        Error = LxVfsUnlink(Process, To, To->Type == LX_S_IFDIR);
        if (Error != 0)
            return Error;
    }

    Error = LxNtOpenHandle(&From->NtPath, DELETE | FILE_READ_ATTRIBUTES, FILE_OPEN, 0, 0, &Handle);
    if (Error != 0)
        return Error;

    Size = FIELD_OFFSET(FILE_RENAME_INFORMATION, FileName) + To->NtPath.Length;
    Rename = LxAllocPaged(Size);
    if (Rename == NULL)
    {
        ZwClose(Handle);
        return -LX_ENOMEM;
    }
    Rename->ReplaceIfExists = TRUE;
    Rename->RootDirectory = NULL;
    Rename->FileNameLength = To->NtPath.Length;
    RtlCopyMemory(Rename->FileName, To->NtPath.Buffer, To->NtPath.Length);
    Status = ZwSetInformationFile(Handle, &Iosb, Rename, Size, FileRenameInformation);
    LxFreePaged(Rename);
    ZwClose(Handle);
    LxVcacheInvalidate();
    if (!NT_SUCCESS(Status))
        return -LxStatusToErrno(Status);

    LxInotifyEvent(Process, From->Path, 0x00000040, TRUE);
    LxInotifyEvent(Process, To->Path, 0x00000080, FALSE);
    return 0;
}

LONG
LxVfsLink(
    _In_ PLX_PROCESS Process,
    _In_ PLX_NODE From,
    _In_ PLX_NODE To)
{
    PFILE_LINK_INFORMATION Link;
    IO_STATUS_BLOCK Iosb;
    HANDLE Handle;
    NTSTATUS Status;
    ULONG Size;
    LONG Error;

    if (!From->Exists)
        return -LX_ENOENT;
    if (To->Exists)
        return -LX_EEXIST;
    if (From->Type == LX_S_IFDIR)
        return -LX_EPERM;
    if ((From->Mount != To->Mount) || (From->Mount->Kind != LxFsNt))
        return -LX_EXDEV;

    Error = LxNtOpenHandle(&From->NtPath, FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES, FILE_OPEN, 0, 0, &Handle);
    if (Error != 0)
        return Error;

    Size = FIELD_OFFSET(FILE_LINK_INFORMATION, FileName) + To->NtPath.Length;
    Link = LxAllocPaged(Size);
    if (Link == NULL)
    {
        ZwClose(Handle);
        return -LX_ENOMEM;
    }
    Link->ReplaceIfExists = FALSE;
    Link->RootDirectory = NULL;
    Link->FileNameLength = To->NtPath.Length;
    RtlCopyMemory(Link->FileName, To->NtPath.Buffer, To->NtPath.Length);
    Status = ZwSetInformationFile(Handle, &Iosb, Link, Size, FileLinkInformation);
    LxFreePaged(Link);
    ZwClose(Handle);
    LxVcacheInvalidate();
    if (!NT_SUCCESS(Status))
        return -LxStatusToErrno(Status);
    LxInotifyEvent(Process, To->Path, 0x00000100, FALSE);
    return 0;
}

LONG
LxVfsSetAttributes(
    _In_ PLX_PROCESS Process,
    _In_ PLX_NODE Node,
    _In_opt_ PLX_FILE File,
    _In_ ULONG Mode,
    _In_ ULONG Uid,
    _In_ ULONG Gid)
{
    HANDLE Handle = NULL;
    ULONG OldMode, OldUid, OldGid;
    ULONG64 Device;
    LX_STAT Stat;
    BOOLEAN Close = FALSE;
    LONG Error;

    if ((File != NULL) && (File->Handle != NULL))
    {
        Handle = File->Handle;
    }
    else if ((Node != NULL) && (Node->Mount != NULL) && (Node->Mount->Kind == LxFsNt))
    {
        Error = LxNtOpenHandle(&Node->NtPath, FILE_READ_EA | FILE_WRITE_EA | FILE_READ_ATTRIBUTES, FILE_OPEN, 0, 0, &Handle);
        if (Error != 0)
            return Error;
        Close = TRUE;
    }
    else if ((Node != NULL) && (Node->Mount != NULL))
    {
        return 0;
    }
    else
    {
        return 0;
    }

    if ((LxNtReadLxAttributes(Handle, &OldMode, &OldUid, &OldGid, &Device) != 0) || (OldMode == MAXULONG))
    {
        PLX_FILE Temporary = LxFileFromNtHandle(Handle, LX_O_PATH);

        OldMode = 0;
        OldUid = OldGid = 0;
        if (Temporary != NULL)
        {
            Temporary->Handle = NULL;
            if (LxNtStatHandleType(Handle, &Stat) == 0)
                OldMode = Stat.Mode;
            LxFileDereference(Temporary);
        }
    }
    if (OldUid == MAXULONG)
        OldUid = 0;
    if (OldGid == MAXULONG)
        OldGid = 0;

    if (Mode != MAXULONG)
        OldMode = (OldMode & LX_S_IFMT) | (Mode & 07777);
    if (Uid != MAXULONG)
        OldUid = Uid;
    if (Gid != MAXULONG)
        OldGid = Gid;

    Error = LxNtWriteLxAttributes(Handle, OldMode, OldUid, OldGid, Device,
                                  (Device != MAXULONG64) &&
                                  (((OldMode & LX_S_IFMT) == LX_S_IFCHR) || ((OldMode & LX_S_IFMT) == 0060000)));
    if (Close)
        ZwClose(Handle);
    if ((Node != NULL) && (Node->Path != NULL))
        LxVcacheInvalidatePath(Process->Instance, Node->Path);
    if ((File != NULL) && (File->Path != NULL) && (File->Mount != NULL))
        LxVcacheInvalidatePath(File->Mount->Instance, File->Path);
    if ((Error == 0) && (Node != NULL))
        LxInotifyEvent(Process, Node->Path, 0x00000004, FALSE);
    return Error;
}

LONG
LxNtStatHandleType(
    _In_ HANDLE Handle,
    _Out_ PLX_STAT Stat)
{
    FILE_STANDARD_INFORMATION Standard;
    IO_STATUS_BLOCK Iosb;
    NTSTATUS Status;

    RtlZeroMemory(Stat, sizeof(*Stat));
    Status = ZwQueryInformationFile(Handle, &Iosb, &Standard, sizeof(Standard), FileStandardInformation);
    if (!NT_SUCCESS(Status))
        return -LxStatusToErrno(Status);
    Stat->Mode = Standard.Directory ? (LX_S_IFDIR | 0755) : (LX_S_IFREG | 0755);
    return 0;
}

LONG
LxVfsSetTimes(
    _In_opt_ PLX_NODE Node,
    _In_opt_ PLX_FILE File,
    _In_ PLX_TIMESPEC Times)
{
    FILE_BASIC_INFORMATION Basic;
    IO_STATUS_BLOCK Iosb;
    LARGE_INTEGER Now;
    HANDLE Handle = NULL;
    BOOLEAN Close = FALSE;
    NTSTATUS Status;
    ULONG Index;

    if ((File != NULL) && (File->Handle != NULL))
    {
        Handle = File->Handle;
    }
    else if ((Node != NULL) && (Node->Mount->Kind == LxFsNt))
    {
        LONG Error = LxNtOpenHandle(&Node->NtPath, FILE_WRITE_ATTRIBUTES, FILE_OPEN, 0, 0, &Handle);

        if (Error != 0)
            return Error;
        Close = TRUE;
    }
    else
    {
        return 0;
    }

    RtlZeroMemory(&Basic, sizeof(Basic));
    KeQuerySystemTime(&Now);
    for (Index = 0; Index < 2; Index++)
    {
        PLARGE_INTEGER Target = (Index == 0) ? &Basic.LastAccessTime : &Basic.LastWriteTime;

        if (Times[Index].Nanoseconds == ((1LL << 30) - 2))
            continue;
        Target->QuadPart = (Times[Index].Nanoseconds == ((1LL << 30) - 1)) ? Now.QuadPart : LxTimespecToNtTime(&Times[Index]);
    }
    Basic.ChangeTime.QuadPart = Now.QuadPart;

    Status = ZwSetInformationFile(Handle, &Iosb, &Basic, sizeof(Basic), FileBasicInformation);
    if (Close)
        ZwClose(Handle);
    if ((Node != NULL) && (Node->Path != NULL) && (Node->Mount != NULL))
        LxVcacheInvalidatePath(Node->Mount->Instance, Node->Path);
    if ((File != NULL) && (File->Path != NULL) && (File->Mount != NULL))
        LxVcacheInvalidatePath(File->Mount->Instance, File->Path);
    return NT_SUCCESS(Status) ? 0 : -LxStatusToErrno(Status);
}

LONG
LxVfsTruncate(
    _In_ PLX_NODE Node,
    _In_ LONG64 Length)
{
    FILE_END_OF_FILE_INFORMATION EndOfFile;
    IO_STATUS_BLOCK Iosb;
    HANDLE Handle;
    NTSTATUS Status;
    LONG Error;

    if (Node->Mount->Kind != LxFsNt)
        return -LX_EINVAL;
    if (Node->Type == LX_S_IFDIR)
        return -LX_EISDIR;

    Error = LxNtOpenHandle(&Node->NtPath, FILE_WRITE_DATA, FILE_OPEN, FILE_NON_DIRECTORY_FILE, 0, &Handle);
    if (Error != 0)
        return Error;
    EndOfFile.EndOfFile.QuadPart = Length;
    Status = ZwSetInformationFile(Handle, &Iosb, &EndOfFile, sizeof(EndOfFile), FileEndOfFileInformation);
    ZwClose(Handle);
    LxVcacheInvalidatePath(Node->Mount->Instance, Node->Path);
    return NT_SUCCESS(Status) ? 0 : -LxStatusToErrno(Status);
}

LONG
LxVfsReadlink(
    _In_ PLX_PROCESS Process,
    _In_ PLX_NODE Node,
    _Out_ PSTR *Target)
{
    *Target = NULL;
    if (Node->Type != LX_S_IFLNK)
        return -LX_EINVAL;
    if (Node->Mount->Kind != LxFsNt)
        return LxSynthReadlink(Process, Node, Target);
    if (Node->LinkTarget != NULL)
    {
        *Target = LxStrDup(Node->LinkTarget);
        return (*Target != NULL) ? 0 : -LX_ENOMEM;
    }
    if (Node->Mount->Kind == LxFsNt)
        return LxNtReadLink(&Node->NtPath, Target);
    return -LX_EINVAL;
}

#define LX_XATTR_EA_NAME "$LXXATTR"

LONG
LxXattrOpenNode(
    _In_ PLX_NODE Node,
    _In_ BOOLEAN Write,
    _Out_ PHANDLE Handle)
{
    *Handle = NULL;
    if (!Node->Exists)
        return -LX_ENOENT;
    if ((Node->Mount == NULL) || (Node->Mount->Kind != LxFsNt))
        return -LX_EOPNOTSUPP;
    return LxNtOpenHandle(&Node->NtPath, FILE_READ_EA | FILE_READ_ATTRIBUTES | (Write ? FILE_WRITE_EA : 0),
                          FILE_OPEN, 0, 0, Handle);
}

LONG
LxXattrRead(
    _In_ HANDLE Handle,
    _Out_writes_bytes_to_(Capacity, *Length) PUCHAR Buffer,
    _In_ ULONG Capacity,
    _Out_ PULONG Length)
{
    ULONG NameLength = sizeof(LX_XATTR_EA_NAME) - 1;
    ULONG ListLength = (ULONG)LX_ALIGN_UP(FIELD_OFFSET(FILE_GET_EA_INFORMATION, EaName) + NameLength + 1, 4);
    ULONG OutputLength = FIELD_OFFSET(FILE_FULL_EA_INFORMATION, EaName) + NameLength + 1 + MAXUSHORT + 8;
    PFILE_GET_EA_INFORMATION Get;
    PFILE_FULL_EA_INFORMATION Entry;
    IO_STATUS_BLOCK Iosb;
    UCHAR List[32];
    NTSTATUS Status;

    *Length = 0;
    RtlZeroMemory(List, sizeof(List));
    Get = (PFILE_GET_EA_INFORMATION)List;
    Get->EaNameLength = (UCHAR)NameLength;
    RtlCopyMemory(Get->EaName, LX_XATTR_EA_NAME, NameLength);

    Entry = LxAlloc(OutputLength);
    if (Entry == NULL)
        return -LX_ENOMEM;
    Status = ZwQueryEaFile(Handle, &Iosb, Entry, OutputLength, TRUE, List, ListLength, NULL, TRUE);
    if (NT_SUCCESS(Status) && (Entry->EaValueLength != 0))
    {
        *Length = min((ULONG)Entry->EaValueLength, Capacity);
        RtlCopyMemory(Buffer, (PUCHAR)Entry->EaName + Entry->EaNameLength + 1, *Length);
    }
    else if (!NT_SUCCESS(Status) && (Status != STATUS_NO_EAS_ON_FILE) && (Status != STATUS_NONEXISTENT_EA_ENTRY) &&
             (Status != STATUS_EAS_NOT_SUPPORTED))
    {
        LxFree(Entry);
        return -LxStatusToErrno(Status);
    }
    LxFree(Entry);
    return 0;
}

LONG
LxXattrWrite(
    _In_ HANDLE Handle,
    _In_reads_bytes_(Length) PUCHAR Blob,
    _In_ ULONG Length)
{
    ULONG NameLength = sizeof(LX_XATTR_EA_NAME) - 1;
    ULONG Size = (ULONG)LX_ALIGN_UP(FIELD_OFFSET(FILE_FULL_EA_INFORMATION, EaName) + NameLength + 1 + Length, 4);
    PFILE_FULL_EA_INFORMATION Entry;
    IO_STATUS_BLOCK Iosb;
    NTSTATUS Status;

    if (Length > MAXUSHORT)
        return -LX_ENOSPC;
    Entry = LxAlloc(Size);
    if (Entry == NULL)
        return -LX_ENOMEM;
    Entry->EaNameLength = (UCHAR)NameLength;
    Entry->EaValueLength = (USHORT)Length;
    RtlCopyMemory(Entry->EaName, LX_XATTR_EA_NAME, NameLength);
    RtlCopyMemory((PUCHAR)Entry->EaName + NameLength + 1, Blob, Length);
    Status = ZwSetEaFile(Handle, &Iosb, Entry, Size);
    LxFree(Entry);
    if (Status == STATUS_EA_TOO_LARGE)
        return -LX_ENOSPC;
    if (Status == STATUS_EAS_NOT_SUPPORTED)
        return -LX_EOPNOTSUPP;
    return NT_SUCCESS(Status) ? 0 : -LxStatusToErrno(Status);
}

LONG
LxXattrOpenFile(
    _In_ PLX_FILE File,
    _In_ BOOLEAN Write,
    _Out_ PHANDLE Handle,
    _Out_ PBOOLEAN Close)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING Empty = { 0, 0, NULL };
    IO_STATUS_BLOCK Iosb;

    *Handle = NULL;
    *Close = FALSE;
    if ((File->Handle == NULL) || (File->NtPipe) || (File->Mount == NULL) || (File->Mount->Kind != LxFsNt))
        return -LX_EOPNOTSUPP;
    if (Write)
    {
        InitializeObjectAttributes(&ObjectAttributes, &Empty, OBJ_KERNEL_HANDLE, File->Handle, NULL);
        if (NT_SUCCESS(ZwCreateFile(Handle, FILE_READ_EA | FILE_WRITE_EA | FILE_READ_ATTRIBUTES | SYNCHRONIZE,
                                    &ObjectAttributes, &Iosb, NULL, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                    FILE_OPEN, FILE_SYNCHRONOUS_IO_NONALERT, NULL, 0)))
        {
            *Close = TRUE;
            return 0;
        }
    }
    *Handle = File->Handle;
    return 0;
}
