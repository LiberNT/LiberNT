/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Where a create request carries its extra parameters
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "ecpp.h"
#define NDEBUG
#include <debug.h>

/* PRIVATE FUNCTIONS **********************************************************/

/*
 * The I/O manager points the create request's user buffer at the create
 * context's parameter list, so that slot is where the list lives for as long
 * as the create runs.
 */
static
PECP_LIST *
FsRtlpEcpSlotOfIrp(
    _In_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);

    if (Stack->MajorFunction != IRP_MJ_CREATE) return NULL;

    if (Irp->UserBuffer == NULL) return NULL;

    return (PECP_LIST *)Irp->UserBuffer;
}

/* PUBLIC FUNCTIONS ***********************************************************/

NTSTATUS
NTAPI
FsRtlGetEcpListFromIrp(
    _In_ PIRP Irp,
    _Outptr_result_maybenull_ PECP_LIST *EcpList)
{
    PECP_LIST *Slot;

    PAGED_CODE();

    if ((Irp == NULL) || (EcpList == NULL)) return STATUS_INVALID_PARAMETER;

    *EcpList = NULL;

    Slot = FsRtlpEcpSlotOfIrp(Irp);
    if (Slot == NULL) return STATUS_INVALID_PARAMETER;

    *EcpList = *Slot;

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
FsRtlSetEcpListIntoIrp(
    _Inout_ PIRP Irp,
    _In_ PECP_LIST EcpList)
{
    PECP_LIST *Slot;

    PAGED_CODE();

    if (Irp == NULL) return STATUS_INVALID_PARAMETER;

    Slot = FsRtlpEcpSlotOfIrp(Irp);
    if (Slot == NULL) return STATUS_INVALID_PARAMETER;

    *Slot = EcpList;

    return STATUS_SUCCESS;
}

/* EOF */
