/*
 * PROJECT:     LiberNT Generic Framebuffer Boot Video Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Framebuffer write visibility on cache-coherent scanout
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

VOID
VidpInitializeFrameBufferFlush(
    _In_ PHYSICAL_ADDRESS PhysicalAddress,
    _In_ PVOID VirtualAddress,
    _In_ ULONG Size)
{
    UNREFERENCED_PARAMETER(PhysicalAddress);
    UNREFERENCED_PARAMETER(VirtualAddress);
    UNREFERENCED_PARAMETER(Size);
}

VOID
VidpFlushFrameBuffer(
    _In_ PVOID Start,
    _In_ ULONG Width,
    _In_ ULONG Height,
    _In_ ULONG Stride)
{
    UNREFERENCED_PARAMETER(Start);
    UNREFERENCED_PARAMETER(Width);
    UNREFERENCED_PARAMETER(Height);
    UNREFERENCED_PARAMETER(Stride);
}
