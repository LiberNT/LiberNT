/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     A new file object carries no per-file-object context of one that was deleted before it
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

#define ATTEMPTS 64

static
NTSTATUS
OpenFileObject(
    _Out_ PHANDLE Handle,
    _Out_ PFILE_OBJECT *FileObject)
{
    UNICODE_STRING Name = RTL_CONSTANT_STRING(L"\\SystemRoot\\system32\\ntoskrnl.exe");
    OBJECT_ATTRIBUTES Attributes;
    IO_STATUS_BLOCK IoStatus;
    NTSTATUS Status;

    *FileObject = NULL;

    InitializeObjectAttributes(&Attributes, &Name, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = ZwOpenFile(Handle,
                        FILE_READ_ATTRIBUTES | SYNCHRONIZE,
                        &Attributes,
                        &IoStatus,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        FILE_SYNCHRONOUS_IO_NONALERT);
    if (!NT_SUCCESS(Status)) return Status;

    Status = ObReferenceObjectByHandle(*Handle, 0, *IoFileObjectType, KernelMode, (PVOID *)FileObject, NULL);
    if (!NT_SUCCESS(Status)) ZwClose(*Handle);

    return Status;
}

START_TEST(FsRtlPerFileObjectContext)
{
    PFSRTL_PER_FILEOBJECT_CONTEXT Context, Stale;
    HANDLE Handles[ATTEMPTS];
    PFILE_OBJECT Objects[ATTEMPTS];
    PFILE_OBJECT FileObject, Deleted;
    PFILE_OBJECT Reused = NULL;
    KAFFINITY OldAffinity;
    ULONG Count, Index;
    HANDLE Handle;
    NTSTATUS Status;

    Context = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Context), 'CFsT');
    ok(Context != NULL, "out of memory\n");
    if (Context == NULL) return;

    /* The freed file object is handed out again soonest to an allocation on the processor that freed it */
    OldAffinity = KeSetSystemAffinityThreadEx(1);

    Status = OpenFileObject(&Handle, &FileObject);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
    {
        KeRevertToUserAffinityThreadEx(OldAffinity);
        ExFreePoolWithTag(Context, 'CFsT');
        return;
    }

    FsRtlInitPerFileObjectContext(Context, Context, NULL);
    Status = FsRtlInsertPerFileObjectContext(FileObject, Context);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_pointer(FsRtlLookupPerFileObjectContext(FileObject, Context, NULL), Context);

    Deleted = FileObject;
    ObDereferenceObject(FileObject);
    ZwClose(Handle);

    for (Count = 0; Count < ATTEMPTS && Reused == NULL; Count++)
    {
        Status = OpenFileObject(&Handles[Count], &Objects[Count]);
        if (!NT_SUCCESS(Status)) break;

        if (Objects[Count] == Deleted) Reused = Objects[Count];
    }

    if (!skip(Reused != NULL, "No new file object took the address of the deleted one\n"))
    {
        ok_eq_pointer(FsRtlLookupPerFileObjectContext(Reused, Context, NULL), NULL);
        Stale = FsRtlRemovePerFileObjectContext(Reused, Context, NULL);
        ok_eq_pointer(Stale, NULL);
    }

    for (Index = 0; Index < Count; Index++)
    {
        if (Objects[Index] == NULL) continue;
        ObDereferenceObject(Objects[Index]);
        ZwClose(Handles[Index]);
    }

    KeRevertToUserAffinityThreadEx(OldAffinity);

    if (Reused != NULL) ExFreePoolWithTag(Context, 'CFsT');
}
