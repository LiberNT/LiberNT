/*
 * PROJECT:     LiberNT NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/nt/ntci.c
 * PURPOSE:     Process image signing policy at image section creation
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <mm/nt/mint.h>
#include <libernt/ci.h>
#include "catalog.h"

#define TAG_CODE_INTEGRITY 'gImM'
#define MI_CI_CHUNK (64 * 1024)

typedef struct _MI_CI_FILE
{
    PFILE_OBJECT File;
    PUCHAR Bounce;
    NTSTATUS Status;
} MI_CI_FILE, *PMI_CI_FILE;

void *
CiAllocate(size_t Count, size_t Size)
{
    PVOID Buffer;

    if (!Count || !Size || Count > (SIZE_T)-1 / Size)
        return NULL;

    Buffer = ExAllocatePoolWithTag(PagedPool, Count * Size, TAG_CODE_INTEGRITY);
    if (Buffer)
        RtlZeroMemory(Buffer, Count * Size);

    return Buffer;
}

void
CiFree(void *Allocation)
{
    if (Allocation)
        ExFreePoolWithTag(Allocation, TAG_CODE_INTEGRITY);
}

static int
MiCiReadFile(void *Context, uint64_t Offset, void *Buffer, size_t Length)
{
    PMI_CI_FILE File = Context;
    PUCHAR Output = Buffer;

    if (Length > MAXULONG - 2 * PAGE_SIZE || Offset > MAXLONGLONG - Length)
    {
        File->Status = STATUS_INVALID_PARAMETER;
        return 0;
    }

    while (Length != 0)
    {
        ULONG Adjustment = (ULONG)(Offset & (PAGE_SIZE - 1));
        ULONG Chunk = (ULONG)min(Length, (size_t)(MI_CI_CHUNK - Adjustment));
        ULONG Transferred;

        File->Status = MiPagingIo(File->File, Offset - Adjustment, ROUND_TO_PAGES(Chunk + Adjustment), File->Bounce,
                                  FALSE, &Transferred);
        if (!NT_SUCCESS(File->Status))
            return 0;

        if (Transferred < Chunk + Adjustment)
        {
            File->Status = STATUS_END_OF_FILE;
            return 0;
        }

        RtlCopyMemory(Output, File->Bounce + Adjustment, Chunk);
        Output += Chunk;
        Offset += Chunk;
        Length -= Chunk;
    }

    return 1;
}

static
NTSTATUS
MiVerifyImageSigning(
    _In_ PFILE_OBJECT FileObject,
    _Out_ PBOOLEAN VerifiedOut)
{
    MI_CI_FILE File = { FileObject, NULL, STATUS_SUCCESS };
    SIZE_T Low = 0, High = RTL_NUMBER_OF(MiSystemImageCatalog), Middle;
    LARGE_INTEGER Size, Time;
    BOOLEAN Verified = FALSE;
    ULONGLONG Extent = 0;
    UCHAR Digest[32];
    NTSTATUS Status;

    PAGED_CODE();

    *VerifiedOut = FALSE;

    Status = FsRtlGetFileSize(FileObject, &Size);
    if (!NT_SUCCESS(Status))
        return Status;

    if (Size.QuadPart <= 0)
        return STATUS_INVALID_IMAGE_FORMAT;

    File.Bounce = ExAllocatePoolWithTag(NonPagedPool, MI_CI_CHUNK, TAG_CODE_INTEGRITY);
    if (File.Bounce == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    if (!CiCatalogImage(MiCiReadFile, &File, Size.QuadPart, &Extent, NULL))
    {
        if (!NT_SUCCESS(File.Status))
        {
            ExFreePoolWithTag(File.Bounce, TAG_CODE_INTEGRITY);
            return File.Status;
        }
        Extent = 0;
    }

    while (Low < High)
    {
        Middle = Low + (High - Low) / 2;
        if (MiSystemImageCatalog[Middle].Size < Extent)
            Low = Middle + 1;
        else
            High = Middle;
    }

    if (Extent != 0 && Low < RTL_NUMBER_OF(MiSystemImageCatalog) && MiSystemImageCatalog[Low].Size == Extent)
    {
        if (!CiCatalogImage(MiCiReadFile, &File, Size.QuadPart, &Extent, Digest))
        {
            ExFreePoolWithTag(File.Bounce, TAG_CODE_INTEGRITY);
            return NT_SUCCESS(File.Status) ? STATUS_INSUFFICIENT_RESOURCES : File.Status;
        }

        while (Low < RTL_NUMBER_OF(MiSystemImageCatalog) && MiSystemImageCatalog[Low].Size == Extent)
        {
            if (RtlCompareMemory(Digest, MiSystemImageCatalog[Low].Hash, sizeof(Digest)) == sizeof(Digest))
            {
                Verified = TRUE;
                break;
            }
            ++Low;
        }
    }

    if (!Verified)
    {
        KeQuerySystemTime(&Time);
        Verified = CiVerifyMicrosoftImage(MiCiReadFile, &File, Size.QuadPart,
                                          Time.QuadPart / 10000000ULL - 11644473600ULL);
    }

    ExFreePoolWithTag(File.Bounce, TAG_CODE_INTEGRITY);

    if (!NT_SUCCESS(File.Status))
        return File.Status;

    *VerifiedOut = Verified;
    return STATUS_SUCCESS;
}

NTSTATUS
MiValidateImageSigningPolicy(
    _In_ PFILE_OBJECT FileObject,
    _Inout_ PLONG SigningState)
{
    ULONG Policy = PsGetProcessMitigationPolicyFlags(PsGetCurrentProcess(), PSP_SIGNATURE_POLICY);
    BOOLEAN Verified;
    NTSTATUS Status;
    LONG State;

    PAGED_CODE();

    if (!(Policy & 9) || ExGetPreviousMode() != UserMode)
        return STATUS_SUCCESS;

    State = InterlockedCompareExchange(SigningState, MI_IMAGE_SIGNING_UNKNOWN, MI_IMAGE_SIGNING_UNKNOWN);
    if (State == MI_IMAGE_SIGNING_UNKNOWN)
    {
        Status = MiVerifyImageSigning(FileObject, &Verified);
        if (!NT_SUCCESS(Status))
            return Status;

        State = Verified ? MI_IMAGE_SIGNING_VERIFIED : MI_IMAGE_SIGNING_UNVERIFIED;
        InterlockedCompareExchange(SigningState, State, MI_IMAGE_SIGNING_UNKNOWN);
    }

    ASSERT(State == MI_IMAGE_SIGNING_VERIFIED || State == MI_IMAGE_SIGNING_UNVERIFIED);

    if (State == MI_IMAGE_SIGNING_VERIFIED)
        return STATUS_SUCCESS;

    return (Policy & 1) ? STATUS_INVALID_IMAGE_HASH : STATUS_SUCCESS;
}
