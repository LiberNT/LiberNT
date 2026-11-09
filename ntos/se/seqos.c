/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Security quality of service capture
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* PRIVATE FUNCTIONS **********************************************************/

/*
 * The quality of service a caller asks for travels in its object attributes,
 * so it has to be taken out of the caller's memory before anything acts on it.
 * A kernel caller is trusted with its own structure unless it asks for a copy.
 */
NTSTATUS
NTAPI
SepCaptureSecurityQualityOfService(
    _In_opt_ POBJECT_ATTRIBUTES ObjectAttributes,
    _In_ KPROCESSOR_MODE AccessMode,
    _In_ POOL_TYPE PoolType,
    _In_ BOOLEAN CaptureIfKernel,
    _Out_ PSECURITY_QUALITY_OF_SERVICE *CapturedSecurityQualityOfService,
    _Out_ PBOOLEAN Present)
{
    PSECURITY_QUALITY_OF_SERVICE Requested = NULL;
    PSECURITY_QUALITY_OF_SERVICE Copy;
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();

    *CapturedSecurityQualityOfService = NULL;
    *Present = FALSE;

    if (ObjectAttributes == NULL) return STATUS_SUCCESS;

    if (AccessMode != KernelMode)
    {
        /* Reading the attributes is itself a read of the caller's memory */
        _SEH2_TRY
        {
            ProbeForRead(ObjectAttributes,
                         sizeof(OBJECT_ATTRIBUTES),
                         TYPE_ALIGNMENT(OBJECT_ATTRIBUTES));

            Requested = ObjectAttributes->SecurityQualityOfService;
            if (Requested != NULL)
            {
                ProbeForRead(Requested,
                             sizeof(SECURITY_QUALITY_OF_SERVICE),
                             TYPE_ALIGNMENT(SECURITY_QUALITY_OF_SERVICE));

                if (Requested->Length != sizeof(SECURITY_QUALITY_OF_SERVICE))
                    Status = STATUS_INVALID_PARAMETER;
            }
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;

        if (!NT_SUCCESS(Status)) return Status;
        if (Requested == NULL) return STATUS_SUCCESS;

        Copy = ExAllocatePoolWithTag(PoolType,
                                     sizeof(SECURITY_QUALITY_OF_SERVICE),
                                     TAG_QOS);
        if (Copy == NULL) return STATUS_INSUFFICIENT_RESOURCES;

        /* The caller can still unmap it between the probe and the copy */
        _SEH2_TRY
        {
            *Copy = *Requested;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;

        if (!NT_SUCCESS(Status))
        {
            ExFreePoolWithTag(Copy, TAG_QOS);
            return Status;
        }

        *CapturedSecurityQualityOfService = Copy;
        *Present = TRUE;
        return STATUS_SUCCESS;
    }

    Requested = ObjectAttributes->SecurityQualityOfService;
    if (Requested == NULL) return STATUS_SUCCESS;

    if (!CaptureIfKernel)
    {
        /* Nothing is copied, so what the caller supplied has to outlive the
         * operation; releasing it later frees nothing */
        *CapturedSecurityQualityOfService = Requested;
        *Present = TRUE;
        return STATUS_SUCCESS;
    }

    if (Requested->Length != sizeof(SECURITY_QUALITY_OF_SERVICE))
        return STATUS_INVALID_PARAMETER;

    Copy = ExAllocatePoolWithTag(PoolType,
                                 sizeof(SECURITY_QUALITY_OF_SERVICE),
                                 TAG_QOS);
    if (Copy == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    *Copy = *Requested;
    *CapturedSecurityQualityOfService = Copy;
    *Present = TRUE;

    return STATUS_SUCCESS;
}

/*
 * Only a copy is given back; a kernel caller that was trusted with its own
 * structure has nothing to release.
 */
VOID
NTAPI
SepReleaseSecurityQualityOfService(
    _In_opt_ PSECURITY_QUALITY_OF_SERVICE CapturedSecurityQualityOfService,
    _In_ KPROCESSOR_MODE AccessMode,
    _In_ BOOLEAN CaptureIfKernel)
{
    PAGED_CODE();

    if (CapturedSecurityQualityOfService == NULL) return;
    if (AccessMode == KernelMode && !CaptureIfKernel) return;

    ExFreePoolWithTag(CapturedSecurityQualityOfService, TAG_QOS);
}

/* EOF */
