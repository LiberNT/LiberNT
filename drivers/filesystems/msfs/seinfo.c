/*
 * PROJECT:     LiberNT Mailslot FileSystem
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Mailslot security descriptor query and set
 */

/* INCLUDES ******************************************************************/

#include "msfs.h"

#define NDEBUG
#include <debug.h>

/* FUNCTIONS *****************************************************************/

NTSTATUS DEFAULTAPI
MsfsQuerySecurity(PDEVICE_OBJECT DeviceObject,
                  PIRP Irp)
{
    PIO_STACK_LOCATION IoStack;
    PMSFS_DEVICE_EXTENSION DeviceExtension;
    PMSFS_FCB Fcb;
    NTSTATUS Status;

    PAGED_CODE();

    IoStack = IoGetCurrentIrpStackLocation(Irp);
    DeviceExtension = DeviceObject->DeviceExtension;
    Fcb = (PMSFS_FCB)IoStack->FileObject->FsContext;

    if (Fcb == NULL)
    {
        Irp->IoStatus.Status = STATUS_INVALID_PARAMETER;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_INVALID_PARAMETER;
    }

    KeLockMutex(&DeviceExtension->FcbListLock);

    if (IoStack->FileObject->FsContext2 != Fcb->ServerCcb)
    {
        Status = STATUS_END_OF_FILE;
        Irp->IoStatus.Information = 0;
    }
    else
    {
        Status = SeQuerySecurityDescriptorInfo(&IoStack->Parameters.QuerySecurity.SecurityInformation,
                                               Irp->UserBuffer,
                                               &IoStack->Parameters.QuerySecurity.Length,
                                               &Fcb->SecurityDescriptor);
        if (Status == STATUS_BUFFER_TOO_SMALL)
        {
            Irp->IoStatus.Information = IoStack->Parameters.QuerySecurity.Length;
            Status = STATUS_BUFFER_OVERFLOW;
        }
        else
        {
            Irp->IoStatus.Information = 0;
        }
    }

    KeUnlockMutex(&DeviceExtension->FcbListLock);

    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

NTSTATUS DEFAULTAPI
MsfsSetSecurity(PDEVICE_OBJECT DeviceObject,
                PIRP Irp)
{
    PIO_STACK_LOCATION IoStack;
    PMSFS_DEVICE_EXTENSION DeviceExtension;
    PMSFS_FCB Fcb;
    PSECURITY_DESCRIPTOR OldSecurityDescriptor;
    PSECURITY_DESCRIPTOR TempSecurityDescriptor;
    PSECURITY_DESCRIPTOR NewSecurityDescriptor;
    NTSTATUS Status;

    PAGED_CODE();

    IoStack = IoGetCurrentIrpStackLocation(Irp);
    DeviceExtension = DeviceObject->DeviceExtension;
    Fcb = (PMSFS_FCB)IoStack->FileObject->FsContext;

    if (Fcb == NULL)
    {
        Irp->IoStatus.Status = STATUS_INVALID_PARAMETER;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_INVALID_PARAMETER;
    }

    KeLockMutex(&DeviceExtension->FcbListLock);

    OldSecurityDescriptor = TempSecurityDescriptor = Fcb->SecurityDescriptor;
    Status = SeSetSecurityDescriptorInfo(NULL,
                                         &IoStack->Parameters.SetSecurity.SecurityInformation,
                                         IoStack->Parameters.SetSecurity.SecurityDescriptor,
                                         &TempSecurityDescriptor,
                                         PagedPool,
                                         IoGetFileObjectGenericMapping());
    if (NT_SUCCESS(Status))
    {
        Status = ObLogSecurityDescriptor(TempSecurityDescriptor, &NewSecurityDescriptor, 1);
        ExFreePoolWithTag(TempSecurityDescriptor, 0);
        if (NT_SUCCESS(Status))
        {
            Fcb->SecurityDescriptor = NewSecurityDescriptor;
            ObDereferenceSecurityDescriptor(OldSecurityDescriptor, 1);
        }
    }

    KeUnlockMutex(&DeviceExtension->FcbListLock);

    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

/* EOF */
