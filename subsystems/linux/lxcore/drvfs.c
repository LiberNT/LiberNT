/*
 * PROJECT:     LiberNT Linux Subsystem
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     DrvFs mounts of Windows drives
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include "lxcore.h"

#define NDEBUG
#include <debug.h>

LONG
LxDrvfsParseSource(
    _In_ PCSTR Source,
    _Out_ PUNICODE_STRING NtRoot,
    _Out_ PSTR *Display)
{
    static const WCHAR Prefix[] = L"\\??\\";
    ULONG Length = (ULONG)strlen(Source), Needed = 0, Written = 0, Index;
    USHORT Out;
    PWCHAR Buffer;
    PSTR Shown;
    NTSTATUS Status;

    RtlZeroMemory(NtRoot, sizeof(*NtRoot));
    *Display = NULL;
    if ((Length < 2) || (Length > 1024) || (Source[1] != ':') ||
        !(((Source[0] >= 'A') && (Source[0] <= 'Z')) || ((Source[0] >= 'a') && (Source[0] <= 'z'))) ||
        ((Length > 2) && (Source[2] != '\\') && (Source[2] != '/')))
    {
        return -LX_EINVAL;
    }
    if (Length > 2)
    {
        Status = RtlUTF8ToUnicodeN(NULL, 0, &Needed, Source + 2, Length - 2);
        if (!NT_SUCCESS(Status))
            return -LX_EINVAL;
    }

    Buffer = LxAlloc(sizeof(Prefix) + 2 * sizeof(WCHAR) + Needed);
    Shown = LxAlloc(Length + 1);
    if ((Buffer == NULL) || (Shown == NULL))
    {
        LxFree(Buffer);
        LxFree(Shown);
        return -LX_ENOMEM;
    }

    Out = (USHORT)(RTL_NUMBER_OF(Prefix) - 1);
    RtlCopyMemory(Buffer, Prefix, Out * sizeof(WCHAR));
    Buffer[Out++] = (WCHAR)((Source[0] >= 'a') ? (Source[0] - 'a' + 'A') : Source[0]);
    Buffer[Out++] = L':';
    if (Length > 2)
    {
        PWCHAR Tail = LxAllocPaged(Needed);

        if (Tail == NULL)
        {
            LxFree(Buffer);
            LxFree(Shown);
            return -LX_ENOMEM;
        }
        Status = RtlUTF8ToUnicodeN(Tail, Needed, &Written, Source + 2, Length - 2);
        if (!NT_SUCCESS(Status))
            Written = 0;
        for (Index = 0; Index < Written / sizeof(WCHAR); Index++)
        {
            WCHAR Character = (Tail[Index] == L'/') ? L'\\' : Tail[Index];

            if ((Character != L'\\') || (Buffer[Out - 1] != L'\\'))
                Buffer[Out++] = Character;
        }
        LxFreePaged(Tail);
    }
    if ((Out > RTL_NUMBER_OF(Prefix) + 1) && (Buffer[Out - 1] == L'\\'))
        Out--;

    for (Index = 0; Index < Length; Index++)
        Shown[Index] = (Source[Index] == '/') ? '\\' : Source[Index];
    Shown[0] = (CHAR)Buffer[RTL_NUMBER_OF(Prefix) - 1];
    Shown[Length] = '\0';

    NtRoot->Buffer = Buffer;
    NtRoot->Length = Out * sizeof(WCHAR);
    NtRoot->MaximumLength = NtRoot->Length;
    *Display = Shown;
    return 0;
}

static
BOOLEAN
LxDrvfsOptionIs(
    _In_reads_(Length) PCSTR Key,
    _In_ SIZE_T Length,
    _In_z_ PCSTR Name)
{
    return (strlen(Name) == Length) && (RtlCompareMemory(Key, Name, Length) == Length);
}

static
BOOLEAN
LxDrvfsParseNumber(
    _In_ PCSTR Value,
    _In_ PCSTR End,
    _In_ ULONG Base,
    _In_ ULONG Limit,
    _Out_ PULONG Number)
{
    ULONG64 Result = 0;

    if (Value == End)
        return FALSE;
    for (; Value < End; Value++)
    {
        ULONG Digit;

        if ((*Value < '0') || (*Value > '9'))
            return FALSE;
        Digit = (ULONG)(*Value - '0');
        if (Digit >= Base)
            return FALSE;
        Result = Result * Base + Digit;
        if (Result > Limit)
            return FALSE;
    }
    *Number = (ULONG)Result;
    return TRUE;
}

LONG
LxDrvfsParseOptions(
    _In_ PLX_PROCESS Process,
    _In_opt_ PCSTR Data,
    _Out_ PLX_DRVFS_OPTIONS Options)
{
    PCSTR Cursor = Data;

    RtlZeroMemory(Options, sizeof(*Options));
    Options->Uid = Process->Instance->DrvfsDefaults ? Process->Instance->DrvfsUid : Process->Fsuid;
    Options->Gid = Process->Instance->DrvfsDefaults ? Process->Instance->DrvfsGid : Process->Fsgid;
    Options->Umask = 022;

    while ((Cursor != NULL) && (*Cursor != '\0'))
    {
        PCSTR Key = Cursor, Value = NULL, End;
        SIZE_T KeyLength;

        End = strchr(Cursor, ',');
        if (End == NULL)
            End = Cursor + strlen(Cursor);
        Cursor = (*End == ',') ? End + 1 : End;
        if (End == Key)
            continue;
        for (KeyLength = 0; (Key + KeyLength < End) && (Key[KeyLength] != '='); KeyLength++)
        {
        }
        if (Key + KeyLength < End)
            Value = Key + KeyLength + 1;

        if (LxDrvfsOptionIs(Key, KeyLength, "metadata") && (Value == NULL))
            Options->Metadata = TRUE;
        else if (Value == NULL)
            continue;
        else if (LxDrvfsOptionIs(Key, KeyLength, "uid"))
        {
            if (!LxDrvfsParseNumber(Value, End, 10, MAXULONG - 1, &Options->Uid))
                return -LX_EINVAL;
        }
        else if (LxDrvfsOptionIs(Key, KeyLength, "gid"))
        {
            if (!LxDrvfsParseNumber(Value, End, 10, MAXULONG - 1, &Options->Gid))
                return -LX_EINVAL;
        }
        else if (LxDrvfsOptionIs(Key, KeyLength, "umask"))
        {
            if (!LxDrvfsParseNumber(Value, End, 8, 0777, &Options->Umask))
                return -LX_EINVAL;
        }
        else if (LxDrvfsOptionIs(Key, KeyLength, "fmask"))
        {
            if (!LxDrvfsParseNumber(Value, End, 8, 0777, &Options->Fmask))
                return -LX_EINVAL;
        }
        else if (LxDrvfsOptionIs(Key, KeyLength, "dmask"))
        {
            if (!LxDrvfsParseNumber(Value, End, 8, 0777, &Options->Dmask))
                return -LX_EINVAL;
        }
        else if (LxDrvfsOptionIs(Key, KeyLength, "case"))
        {
            /* TODO: case=dir and case=force; only case=off is supported */
            if ((End - Value != 3) || (RtlCompareMemory(Value, "off", 3) != 3))
                return -LX_EINVAL;
        }
    }
    return 0;
}

VOID
LxDrvfsFormatOptions(
    _In_ PLX_MOUNT Mount,
    _Out_writes_(Size) PSTR Buffer,
    _In_ SIZE_T Size)
{
    const LX_DRVFS_OPTIONS *Options = &Mount->DrvfsOptions;

    if (Size != 0)
        Buffer[0] = '\0';
    if (!Mount->Drvfs)
        return;
    RtlStringCbPrintfA(Buffer, Size, ",uid=%lu,gid=%lu", Options->Uid, Options->Gid);
    if (Options->Umask != 022)
        RtlStringCbPrintfA(Buffer + strlen(Buffer), Size - strlen(Buffer), ",umask=%lo", Options->Umask);
    if (Options->Fmask != 0)
        RtlStringCbPrintfA(Buffer + strlen(Buffer), Size - strlen(Buffer), ",fmask=%lo", Options->Fmask);
    if (Options->Dmask != 0)
        RtlStringCbPrintfA(Buffer + strlen(Buffer), Size - strlen(Buffer), ",dmask=%lo", Options->Dmask);
    if (Options->Metadata)
        RtlStringCbCatA(Buffer, Size, ",metadata");
    RtlStringCbCatA(Buffer, Size, ",case=off");
}

static
ULONG
LxDrvfsGrantedAccess(
    _In_ HANDLE Handle)
{
    static const ACCESS_MASK Probes[3] = { FILE_READ_DATA, FILE_WRITE_DATA, FILE_EXECUTE };
    PUBLIC_OBJECT_BASIC_INFORMATION Basic;
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING Empty = { 0, 0, NULL };
    IO_STATUS_BLOCK Iosb;
    HANDLE Reopened;
    ACCESS_MASK Granted = 0;
    ULONG Index;

    InitializeObjectAttributes(&ObjectAttributes, &Empty, OBJ_KERNEL_HANDLE | OBJ_FORCE_ACCESS_CHECK, Handle, NULL);
    if (NT_SUCCESS(ZwCreateFile(&Reopened, MAXIMUM_ALLOWED, &ObjectAttributes, &Iosb, NULL, 0,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, FILE_OPEN,
                                FILE_OPEN_REPARSE_POINT, NULL, 0)))
    {
        if (NT_SUCCESS(ZwQueryObject(Reopened, ObjectBasicInformation, &Basic, sizeof(Basic), NULL)))
            Granted = Basic.GrantedAccess;
        ZwClose(Reopened);
        return Granted;
    }

    for (Index = 0; Index < RTL_NUMBER_OF(Probes); Index++)
    {
        if (NT_SUCCESS(ZwCreateFile(&Reopened, Probes[Index], &ObjectAttributes, &Iosb, NULL, 0,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, FILE_OPEN,
                                    FILE_OPEN_REPARSE_POINT, NULL, 0)))
        {
            Granted |= Probes[Index];
            ZwClose(Reopened);
        }
    }
    return Granted;
}

VOID
LxDrvfsAdjustStat(
    _In_ PLX_MOUNT Mount,
    _In_ HANDLE Handle,
    _In_ ULONG LxFlags,
    _In_ ULONG FileAttributes,
    _Inout_ PLX_STAT Stat)
{
    const LX_DRVFS_OPTIONS *Options = &Mount->DrvfsOptions;
    BOOLEAN Directory = (Stat->Mode & LX_S_IFMT) == LX_S_IFDIR;
    ACCESS_MASK Granted;
    ULONG Bits = 0;

    if (!Mount->Drvfs)
        return;
    if (!(LxFlags & LX_FILE_METADATA_HAS_UID))
        Stat->Uid = Options->Uid;
    if (!(LxFlags & LX_FILE_METADATA_HAS_GID))
        Stat->Gid = Options->Gid;
    if (LxFlags & LX_FILE_METADATA_HAS_MODE)
        return;

    Granted = LxDrvfsGrantedAccess(Handle);
    if (Granted & FILE_READ_DATA)
        Bits |= 4;
    if ((Granted & FILE_WRITE_DATA) && (Directory || !(FileAttributes & FILE_ATTRIBUTE_READONLY)))
        Bits |= 2;
    if (Granted & FILE_EXECUTE)
        Bits |= 1;
    Bits = (Bits << 6) | (Bits << 3) | Bits;
    if (Options->Metadata)
        Bits &= ~(Options->Umask | (Directory ? Options->Dmask : Options->Fmask));
    Stat->Mode = (Stat->Mode & LX_S_IFMT) | Bits;
}

LONG
LxDrvfsSetMode(
    _In_ HANDLE Handle,
    _In_ ULONG Mode)
{
    FILE_BASIC_INFORMATION Basic;
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING Empty = { 0, 0, NULL };
    IO_STATUS_BLOCK Iosb;
    HANDLE Writable;
    ULONG Attributes;
    NTSTATUS Status;

    InitializeObjectAttributes(&ObjectAttributes, &Empty, OBJ_KERNEL_HANDLE | OBJ_FORCE_ACCESS_CHECK, Handle, NULL);
    Status = ZwCreateFile(&Writable, FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES | SYNCHRONIZE, &ObjectAttributes, &Iosb,
                          NULL, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, FILE_OPEN,
                          FILE_OPEN_REPARSE_POINT | FILE_SYNCHRONOUS_IO_NONALERT, NULL, 0);
    if (!NT_SUCCESS(Status))
        return -LxStatusToErrno(Status);

    Status = ZwQueryInformationFile(Writable, &Iosb, &Basic, sizeof(Basic), FileBasicInformation);
    if (NT_SUCCESS(Status) && !(Basic.FileAttributes & FILE_ATTRIBUTE_DIRECTORY))
    {
        Attributes = Basic.FileAttributes;
        if ((Mode & 0222) == 0)
            Attributes |= FILE_ATTRIBUTE_READONLY;
        else
            Attributes &= ~FILE_ATTRIBUTE_READONLY;
        if (Attributes != Basic.FileAttributes)
        {
            RtlZeroMemory(&Basic, sizeof(Basic));
            Basic.FileAttributes = (Attributes != 0) ? Attributes : FILE_ATTRIBUTE_NORMAL;
            Status = ZwSetInformationFile(Writable, &Iosb, &Basic, sizeof(Basic), FileBasicInformation);
        }
    }
    ZwClose(Writable);
    return NT_SUCCESS(Status) ? 0 : -LxStatusToErrno(Status);
}

static
BOOLEAN
LxDrvfsFixedDrive(
    _In_ WCHAR Letter)
{
    WCHAR Path[] = L"\\??\\A:\\";
    FILE_FS_DEVICE_INFORMATION Device;
    UNICODE_STRING Name;
    IO_STATUS_BLOCK Iosb;
    HANDLE Handle;
    NTSTATUS Status;

    Path[4] = Letter;
    RtlInitUnicodeString(&Name, Path);
    if (LxNtOpenHandle(&Name, FILE_READ_ATTRIBUTES, FILE_OPEN, FILE_DIRECTORY_FILE, 0, &Handle) != 0)
        return FALSE;
    Status = ZwQueryVolumeInformationFile(Handle, &Iosb, &Device, sizeof(Device), FileFsDeviceInformation);
    ZwClose(Handle);
    return NT_SUCCESS(Status) && (Device.DeviceType == FILE_DEVICE_DISK) &&
           !(Device.Characteristics & (FILE_REMOVABLE_MEDIA | FILE_REMOTE_DEVICE | FILE_FLOPPY_DISKETTE));
}

static
VOID
LxDrvfsMakeDirectory(
    _In_ PLX_PROCESS Process,
    _In_ PCSTR Path)
{
    LX_NODE Node;

    if (LxLookup(Process, LX_AT_FDCWD, Path, LX_LOOKUP_CREATE, &Node) != 0)
        return;
    if (!Node.Exists)
        LxVfsMkdir(Process, &Node, 0755);
    LxNodeFree(&Node);
}

VOID
LxDrvfsAutomount(
    _In_ PLX_PROCESS Process,
    _In_ PCSTR Root,
    _In_opt_ PCSTR Options)
{
    CHAR Target[LX_PATH_MAX];
    CHAR Source[4] = "A:\\";
    SIZE_T Length = strlen(Root), Index;
    LX_DRVFS_OPTIONS Defaults;
    WCHAR Letter;

    if ((Root[0] != '/') || (Length + 2 >= sizeof(Target)))
        return;
    if (LxDrvfsParseOptions(Process, Options, &Defaults) == 0)
    {
        Process->Instance->DrvfsUid = Defaults.Uid;
        Process->Instance->DrvfsGid = Defaults.Gid;
        Process->Instance->DrvfsDefaults = TRUE;
    }
    for (Index = 1; Index <= Length; Index++)
    {
        if ((Index == Length) || (Root[Index] == '/'))
        {
            RtlCopyMemory(Target, Root, Index);
            Target[Index] = '\0';
            if (Index > 1)
                LxDrvfsMakeDirectory(Process, Target);
        }
    }

    RtlCopyMemory(Target, Root, Length + 1);
    if (Target[Length - 1] != '/')
        Target[Length++] = '/';
    for (Letter = L'A'; Letter <= L'Z'; Letter++)
    {
        if (!LxDrvfsFixedDrive(Letter))
            continue;
        Target[Length] = (CHAR)(Letter - L'A' + 'a');
        Target[Length + 1] = '\0';
        Source[0] = (CHAR)Letter;
        LxDrvfsMakeDirectory(Process, Target);
        if (LxMountAdd(Process, Target, Source, "drvfs", LX_MS_NOATIME, Options) != 0)
            DPRINT1("lxcore: automount of %s on %s failed\n", Source, Target);
    }
}
