/*
 * PROJECT:     LiberNT D3DKMT API Tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     gdi32 exports the D3DKMT swap chain and bundle object entry points
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 */

#include "precomp.h"

static const PCSTR Gdi32D3dkmtNames[] =
{
    "D3DKMTAbandonSwapChain",
    "D3DKMTAcquireSwapChain",
    "D3DKMTAddSurfaceToSwapChain",
    "D3DKMTCreateBundleObject",
    "D3DKMTCreateSwapChain",
    "D3DKMTExtractBundleObject",
    "D3DKMTGetSetSwapChainMetadata",
    "D3DKMTOpenSwapChain",
    "D3DKMTReleaseSwapChain",
    "D3DKMTRemoveSurfaceFromSwapChain",
    "D3DKMTSetDodIndirectSwapchain",
    "D3DKMTUnOrderedPresentSwapChain",
};

/* Look the name up in the export table itself: on Windows these entries forward to an API set that may be absent */
static BOOLEAN
Gdi32HasExport(
    _In_ HMODULE Module,
    _In_ PCSTR Name)
{
    PIMAGE_DOS_HEADER Dos = (PIMAGE_DOS_HEADER)Module;
    PIMAGE_NT_HEADERS Nt = (PIMAGE_NT_HEADERS)((PBYTE)Module + Dos->e_lfanew);
    PIMAGE_DATA_DIRECTORY Directory = &Nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    PIMAGE_EXPORT_DIRECTORY Exports;
    PULONG Names;
    ULONG Index;

    if (Directory->VirtualAddress == 0 || Directory->Size < sizeof(*Exports))
        return FALSE;
    Exports = (PIMAGE_EXPORT_DIRECTORY)((PBYTE)Module + Directory->VirtualAddress);
    Names = (PULONG)((PBYTE)Module + Exports->AddressOfNames);
    for (Index = 0; Index < Exports->NumberOfNames; ++Index)
    {
        if (lstrcmpA((PCSTR)((PBYTE)Module + Names[Index]), Name) == 0)
            return TRUE;
    }
    return FALSE;
}

START_TEST(gdi32names)
{
    HMODULE Gdi32;
    ULONG Index;

    Gdi32 = LoadLibraryA("gdi32.dll");
    ok(Gdi32 != NULL, "gdi32.dll did not load (%lu)\n", GetLastError());
    if (Gdi32 == NULL)
        return;
    for (Index = 0; Index < ARRAYSIZE(Gdi32D3dkmtNames); ++Index)
        ok(Gdi32HasExport(Gdi32, Gdi32D3dkmtNames[Index]), "gdi32 does not export %s\n", Gdi32D3dkmtNames[Index]);
}
