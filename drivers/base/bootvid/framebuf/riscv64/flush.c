/*
 * PROJECT:     LiberNT Generic Framebuffer Boot Video Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Make framebuffer writes visible to a non-coherent scanout engine on RISC-V64
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#define TAG_BOOTVID_FLUSH 'lfiV'

static PMDL VidpFrameBufferMdl;

VOID
VidpInitializeFrameBufferFlush(
    _In_ PHYSICAL_ADDRESS PhysicalAddress,
    _In_ PVOID VirtualAddress,
    _In_ ULONG Size)
{
    PPFN_NUMBER Pages;
    PMDL Mdl;
    ULONG Index, Count;

    Mdl = ExAllocatePoolWithTag(NonPagedPool, MmSizeOfMdl(VirtualAddress, Size), TAG_BOOTVID_FLUSH);
    if (!Mdl)
        return;
    MmInitializeMdl(Mdl, VirtualAddress, Size);
    Pages = MmGetMdlPfnArray(Mdl);
    Count = ADDRESS_AND_SIZE_TO_SPAN_PAGES(VirtualAddress, Size);
    for (Index = 0; Index < Count; ++Index)
        Pages[Index] = (PFN_NUMBER)(PhysicalAddress.QuadPart >> PAGE_SHIFT) + Index;
    VidpFrameBufferMdl = Mdl;
    KeFlushIoRectangle(Mdl, VirtualAddress, Size, 1, Size, TRUE);
}

VOID
VidpFlushFrameBuffer(
    _In_ PVOID Start,
    _In_ ULONG Width,
    _In_ ULONG Height,
    _In_ ULONG Stride)
{
    if (VidpFrameBufferMdl)
        KeFlushIoRectangle(VidpFrameBufferMdl, Start, Width, Height, Stride, FALSE);
}
