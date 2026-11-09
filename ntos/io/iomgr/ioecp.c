/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The extra create parameters a create request carries along
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "../ioint.h"
#define NDEBUG
#include <debug.h>

/* PUBLIC FUNCTIONS ***********************************************************/

NTSTATUS
NTAPI
IoSetIrpExtraCreateParameter(
    _Inout_ PIRP Irp,
    _In_ struct _ECP_LIST *ExtraCreateParameter)
{
    PAGED_CODE();

    if (ExtraCreateParameter == NULL) return STATUS_INVALID_PARAMETER;

    return FsRtlSetEcpListIntoIrp(Irp, ExtraCreateParameter);
}

NTSTATUS
NTAPI
IoGetIrpExtraCreateParameter(
    _In_ PIRP Irp,
    _Outptr_result_maybenull_ struct _ECP_LIST **ExtraCreateParameter)
{
    PAGED_CODE();

    return FsRtlGetEcpListFromIrp(Irp, ExtraCreateParameter);
}

VOID
NTAPI
IoClearIrpExtraCreateParameter(
    _Inout_ PIRP Irp)
{
    PAGED_CODE();

    (VOID)FsRtlSetEcpListIntoIrp(Irp, NULL);
}
