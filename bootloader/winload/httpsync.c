/*
 * PROJECT:     LiberNT Boot Loader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     httpsync: apply the archive of changed files to the operating system volume
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "winload.h"
#include <ntfsbl.h>

#define OSL_TAR_BLOCK          512
#define OSL_TAR_NAME_OFFSET    0
#define OSL_TAR_NAME_CHARS     100
#define OSL_TAR_SIZE_OFFSET    124
#define OSL_TAR_SIZE_CHARS     12
#define OSL_TAR_CHECKSUM_OFFSET 148
#define OSL_TAR_CHECKSUM_CHARS 8
#define OSL_TAR_TYPE_OFFSET    156
#define OSL_TAR_MAGIC_OFFSET   257
#define OSL_TAR_PREFIX_OFFSET  345
#define OSL_TAR_PREFIX_CHARS   155
#define OSL_SYNC_ID_FILE       "SYNC.ID"
#define OSL_SYNC_STATE_FILE    "LiberNT.sync"
#define OSL_SYNC_ID_CHARS      128
#define OSL_SYNC_NAME_CHARS    (OSL_TAR_PREFIX_CHARS + 1 + OSL_TAR_NAME_CHARS + 1)

typedef struct _OSL_TAR_ENTRY
{
    CHAR Name[OSL_SYNC_NAME_CHARS];
    CHAR Type;
    PUCHAR Data;
    ULONG Size;
} OSL_TAR_ENTRY, *POSL_TAR_ENTRY;

static
BOOLEAN
OslTarNumber(
    _In_reads_(Chars) const UCHAR* Field,
    _In_ ULONG Chars,
    _Out_ PULONGLONG Value)
{
    ULONG i = 0;

    *Value = 0;
    while (i < Chars && Field[i] == ' ')
        i++;
    for (; i < Chars && Field[i] >= '0' && Field[i] <= '7'; i++)
        *Value = (*Value << 3) | (Field[i] - '0');
    return i == Chars || Field[i] == ANSI_NULL || Field[i] == ' ';
}

static
BOOLEAN
OslTarPathValid(
    _In_z_ PCSTR Name)
{
    PCSTR Component = Name;
    SIZE_T Length;

    if (*Name == ANSI_NULL || *Name == '/')
        return FALSE;

    for (;;)
    {
        Length = strcspn(Component, "/");
        if ((Length == 1 && Component[0] == '.') ||
            (Length == 2 && Component[0] == '.' && Component[1] == '.') ||
            memchr(Component, '\\', Length) ||
            memchr(Component, ':', Length))
        {
            return FALSE;
        }
        if (Component[Length] == ANSI_NULL)
            return TRUE;
        Component += Length + 1;
    }
}

static
BOOLEAN
OslTarNext(
    _In_ PUCHAR Archive,
    _In_ ULONG Size,
    _Inout_ PULONG Offset,
    _Out_ POSL_TAR_ENTRY Entry,
    _Out_ PBOOLEAN End)
{
    PUCHAR Header = Archive + *Offset;
    ULONGLONG Value, Checksum = 0;
    ULONG i, Prefix;

    *End = FALSE;
    if (Size - *Offset < OSL_TAR_BLOCK)
        return FALSE;

    for (i = 0; i < OSL_TAR_BLOCK && Header[i] == 0; i++)
        ;
    if (i == OSL_TAR_BLOCK)
    {
        *End = TRUE;
        return TRUE;
    }

    for (i = 0; i < OSL_TAR_BLOCK; i++)
    {
        Checksum += (i >= OSL_TAR_CHECKSUM_OFFSET && i < OSL_TAR_CHECKSUM_OFFSET + OSL_TAR_CHECKSUM_CHARS)
                    ? ' ' : Header[i];
    }

    if (memcmp(Header + OSL_TAR_MAGIC_OFFSET, "ustar", 5) != 0 ||
        !OslTarNumber(Header + OSL_TAR_CHECKSUM_OFFSET, OSL_TAR_CHECKSUM_CHARS, &Value) ||
        Value != Checksum ||
        !OslTarNumber(Header + OSL_TAR_SIZE_OFFSET, OSL_TAR_SIZE_CHARS, &Value) ||
        Value > Size - *Offset - OSL_TAR_BLOCK)
    {
        return FALSE;
    }

    Prefix = (ULONG)strnlen((PCSTR)Header + OSL_TAR_PREFIX_OFFSET, OSL_TAR_PREFIX_CHARS);
    RtlZeroMemory(Entry->Name, sizeof(Entry->Name));
    if (Prefix != 0)
    {
        RtlCopyMemory(Entry->Name, Header + OSL_TAR_PREFIX_OFFSET, Prefix);
        Entry->Name[Prefix++] = '/';
    }
    RtlCopyMemory(Entry->Name + Prefix,
                  Header + OSL_TAR_NAME_OFFSET,
                  strnlen((PCSTR)Header + OSL_TAR_NAME_OFFSET, OSL_TAR_NAME_CHARS));

    Entry->Type = (CHAR)Header[OSL_TAR_TYPE_OFFSET];
    Entry->Data = Header + OSL_TAR_BLOCK;
    Entry->Size = (ULONG)Value;
    *Offset += OSL_TAR_BLOCK + (ULONG)ROUND_UP(Value, OSL_TAR_BLOCK);
    return *Offset <= Size;
}

static
VOID
OslSyncApply(
    _In_ PDK_DEVICE OsDevice,
    _In_ PUCHAR Archive,
    _In_ ULONG Size)
{
    CHAR Id[OSL_SYNC_ID_CHARS], Applied[OSL_SYNC_ID_CHARS];
    OSL_TAR_ENTRY Entry;
    NTSTATUS Status;
    ULONG Offset = 0, Files = 0, Read, IdLength;
    ULONGLONG Bytes = 0;
    BOOLEAN End;

    if (!OslTarNext(Archive, Size, &Offset, &Entry, &End) ||
        End ||
        strcmp(Entry.Name, OSL_SYNC_ID_FILE) != 0 ||
        Entry.Size == 0 ||
        Entry.Size >= sizeof(Id))
    {
        FwPrint(L"SYNC: the archive has no %hs entry.\r\n", OSL_SYNC_ID_FILE);
        return;
    }

    IdLength = Entry.Size;
    RtlCopyMemory(Id, Entry.Data, IdLength);
    Id[IdLength] = ANSI_NULL;

    Status = NtfsBlMount(OsDevice);
    if (!NT_SUCCESS(Status))
    {
        FwPrint(L"SYNC: the system volume could not be opened for writing (0x%08lx).\r\n", Status);
        return;
    }

    Status = NtfsBlReadFile(OSL_SYNC_STATE_FILE, Applied, sizeof(Applied) - 1, &Read);
    if (NT_SUCCESS(Status) && Read == IdLength && memcmp(Applied, Id, Read) == 0)
    {
        NtfsBlUnmount();
        FwPrint(L"SYNC: the system is up to date (%hs).\r\n", Id);
        return;
    }

    for (;;)
    {
        if (!OslTarNext(Archive, Size, &Offset, &Entry, &End))
        {
            FwPrint(L"SYNC: the archive is truncated or corrupt after %lu files.\r\n", Files);
            Status = STATUS_FILE_CORRUPT_ERROR;
            break;
        }
        if (End)
            break;

        if (!OslTarPathValid(Entry.Name))
        {
            FwPrint(L"SYNC: the archive path %hs is not valid.\r\n", Entry.Name);
            Status = STATUS_OBJECT_NAME_INVALID;
            break;
        }

        if (Entry.Type == '5')
        {
            Status = NtfsBlCreateDirectory(Entry.Name);
        }
        else if (Entry.Type == '0' || Entry.Type == ANSI_NULL)
        {
            Status = NtfsBlWriteFile(Entry.Name, Entry.Data, Entry.Size);
            Files++;
            Bytes += Entry.Size;
        }
        else
        {
            FwPrint(L"SYNC: %hs has the unsupported archive type %hc.\r\n", Entry.Name, Entry.Type);
            Status = STATUS_NOT_SUPPORTED;
        }

        if (!NT_SUCCESS(Status))
        {
            FwPrint(L"SYNC: %hs could not be written (0x%08lx).\r\n", Entry.Name, Status);
            break;
        }
    }

    if (NT_SUCCESS(Status))
        Status = NtfsBlWriteFile(OSL_SYNC_STATE_FILE, Id, IdLength);
    if (!NT_SUCCESS(NtfsBlUnmount()) && NT_SUCCESS(Status))
        Status = STATUS_UNEXPECTED_IO_ERROR;

    if (NT_SUCCESS(Status))
        FwPrint(L"SYNC: %lu files, %lu KiB written (%hs).\r\n", Files, (ULONG)(Bytes >> 10), Id);
    else
        FwPrint(L"SYNC: the update stopped (0x%08lx); the system may be partly updated.\r\n", Status);
}

VOID
OslHttpSync(
    _In_ PDK_DEVICE OsDevice,
    _In_ PCSTR Url,
    _In_opt_ PCSTR Address)
{
    PVOID Archive;
    ULONG Size;

    Archive = FwDownloadFile(Url, Address, FALSE, &Size);
    if (!Archive)
    {
        FwPrint(L"SYNC: %hs could not be downloaded; starting the installed system.\r\n", Url);
        return;
    }

    OslSyncApply(OsDevice, Archive, Size);
    FwFreePages(Archive, Size);
}
