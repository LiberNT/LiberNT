/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Keeping track of who may share an open file with whom
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "../ioint.h"
#define NDEBUG
#include <debug.h>

/* PRIVATE FUNCTIONS **********************************************************/

static
VOID
IopReadAccessFlags(
    _In_ PFILE_OBJECT FileObject,
    _Out_ PBOOLEAN Read,
    _Out_ PBOOLEAN Write,
    _Out_ PBOOLEAN Delete)
{
    *Read = FileObject->ReadAccess;
    *Write = FileObject->WriteAccess;
    *Delete = FileObject->DeleteAccess;
}

/* PUBLIC FUNCTIONS ***********************************************************/

VOID
NTAPI
IoSetShareAccess(
    _In_ ACCESS_MASK DesiredAccess,
    _In_ ULONG DesiredShareAccess,
    _Inout_ PFILE_OBJECT FileObject,
    _Out_ PSHARE_ACCESS ShareAccess)
{
    BOOLEAN Read;
    BOOLEAN Write;
    BOOLEAN Delete;

    PAGED_CODE();

    Read = (DesiredAccess & (FILE_READ_DATA | FILE_EXECUTE)) != 0;
    Write = (DesiredAccess & (FILE_WRITE_DATA | FILE_APPEND_DATA)) != 0;
    Delete = (DesiredAccess & DELETE) != 0;

    FileObject->ReadAccess = Read;
    FileObject->WriteAccess = Write;
    FileObject->DeleteAccess = Delete;

    if (!Read && !Write && !Delete)
    {
        ShareAccess->OpenCount = 0;
        ShareAccess->Readers = 0;
        ShareAccess->Writers = 0;
        ShareAccess->Deleters = 0;
        ShareAccess->SharedRead = 0;
        ShareAccess->SharedWrite = 0;
        ShareAccess->SharedDelete = 0;
        return;
    }

    FileObject->SharedRead = (DesiredShareAccess & FILE_SHARE_READ) != 0;
    FileObject->SharedWrite = (DesiredShareAccess & FILE_SHARE_WRITE) != 0;
    FileObject->SharedDelete = (DesiredShareAccess & FILE_SHARE_DELETE) != 0;

    ShareAccess->OpenCount = 1;
    ShareAccess->Readers = Read ? 1 : 0;
    ShareAccess->Writers = Write ? 1 : 0;
    ShareAccess->Deleters = Delete ? 1 : 0;
    ShareAccess->SharedRead = FileObject->SharedRead ? 1 : 0;
    ShareAccess->SharedWrite = FileObject->SharedWrite ? 1 : 0;
    ShareAccess->SharedDelete = FileObject->SharedDelete ? 1 : 0;
}

NTSTATUS
NTAPI
IoCheckShareAccess(
    _In_ ACCESS_MASK DesiredAccess,
    _In_ ULONG DesiredShareAccess,
    _Inout_ PFILE_OBJECT FileObject,
    _Inout_ PSHARE_ACCESS ShareAccess,
    _In_ BOOLEAN Update)
{
    BOOLEAN Read;
    BOOLEAN Write;
    BOOLEAN Delete;
    BOOLEAN SharedRead;
    BOOLEAN SharedWrite;
    BOOLEAN SharedDelete;

    PAGED_CODE();

    Read = (DesiredAccess & (FILE_READ_DATA | FILE_EXECUTE)) != 0;
    Write = (DesiredAccess & (FILE_WRITE_DATA | FILE_APPEND_DATA)) != 0;
    Delete = (DesiredAccess & DELETE) != 0;

    FileObject->ReadAccess = Read;
    FileObject->WriteAccess = Write;
    FileObject->DeleteAccess = Delete;

    if (!Read && !Write && !Delete) return STATUS_SUCCESS;

    SharedRead = (DesiredShareAccess & FILE_SHARE_READ) != 0;
    SharedWrite = (DesiredShareAccess & FILE_SHARE_WRITE) != 0;
    SharedDelete = (DesiredShareAccess & FILE_SHARE_DELETE) != 0;

    if ((Read && ShareAccess->SharedRead < ShareAccess->OpenCount) ||
        (Write && ShareAccess->SharedWrite < ShareAccess->OpenCount) ||
        (Delete && ShareAccess->SharedDelete < ShareAccess->OpenCount) ||
        (ShareAccess->Readers != 0 && !SharedRead) ||
        (ShareAccess->Writers != 0 && !SharedWrite) ||
        (ShareAccess->Deleters != 0 && !SharedDelete))
    {
        return STATUS_SHARING_VIOLATION;
    }

    if (!Update) return STATUS_SUCCESS;

    FileObject->SharedRead = SharedRead;
    FileObject->SharedWrite = SharedWrite;
    FileObject->SharedDelete = SharedDelete;

    ShareAccess->OpenCount++;

    if (Read) ShareAccess->Readers++;
    if (Write) ShareAccess->Writers++;
    if (Delete) ShareAccess->Deleters++;

    if (SharedRead) ShareAccess->SharedRead++;
    if (SharedWrite) ShareAccess->SharedWrite++;
    if (SharedDelete) ShareAccess->SharedDelete++;

    return STATUS_SUCCESS;
}

VOID
NTAPI
IoUpdateShareAccess(
    _Inout_ PFILE_OBJECT FileObject,
    _Inout_ PSHARE_ACCESS ShareAccess)
{
    BOOLEAN Read;
    BOOLEAN Write;
    BOOLEAN Delete;

    PAGED_CODE();

    IopReadAccessFlags(FileObject, &Read, &Write, &Delete);

    if (!Read && !Write && !Delete) return;

    ShareAccess->OpenCount++;

    if (Read) ShareAccess->Readers++;
    if (Write) ShareAccess->Writers++;
    if (Delete) ShareAccess->Deleters++;

    if (FileObject->SharedRead) ShareAccess->SharedRead++;
    if (FileObject->SharedWrite) ShareAccess->SharedWrite++;
    if (FileObject->SharedDelete) ShareAccess->SharedDelete++;
}

VOID
NTAPI
IoRemoveShareAccess(
    _In_ PFILE_OBJECT FileObject,
    _Inout_ PSHARE_ACCESS ShareAccess)
{
    BOOLEAN Read;
    BOOLEAN Write;
    BOOLEAN Delete;

    PAGED_CODE();

    IopReadAccessFlags(FileObject, &Read, &Write, &Delete);

    if (!Read && !Write && !Delete) return;

    ASSERT(ShareAccess->OpenCount > 0);
    ShareAccess->OpenCount--;

    if (Read) ShareAccess->Readers--;
    if (Write) ShareAccess->Writers--;
    if (Delete) ShareAccess->Deleters--;

    if (FileObject->SharedRead) ShareAccess->SharedRead--;
    if (FileObject->SharedWrite) ShareAccess->SharedWrite--;
    if (FileObject->SharedDelete) ShareAccess->SharedDelete--;
}
