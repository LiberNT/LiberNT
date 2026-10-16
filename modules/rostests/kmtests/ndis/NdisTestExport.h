/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Runtime lookup of ndis.sys exports that not every architecture provides
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

typedef PVOID (NTAPI *PFN_NDIS_TEST_PC_TO_FILE_HEADER)(PVOID, PVOID *);
typedef PVOID (NTAPI *PFN_NDIS_TEST_DIRECTORY_ENTRY_TO_DATA)(PVOID, BOOLEAN, USHORT, PULONG);

static
PVOID
NdisTestFindExport(
    _In_ PCSTR Name)
{
    PFN_NDIS_TEST_PC_TO_FILE_HEADER pPcToFileHeader;
    PFN_NDIS_TEST_DIRECTORY_ENTRY_TO_DATA pDirectoryEntryToData;
    PIMAGE_EXPORT_DIRECTORY ExportDirectory;
    UNICODE_STRING RoutineName;
    PUSHORT OrdinalTable;
    PULONG FunctionTable;
    PULONG NameTable;
    PVOID ImageBase = NULL;
    ULONG DirectorySize;
    ULONG FunctionIndex;
    ULONG Index;

    RtlInitUnicodeString(&RoutineName, L"RtlPcToFileHeader");
    pPcToFileHeader = MmGetSystemRoutineAddress(&RoutineName);
    RtlInitUnicodeString(&RoutineName, L"RtlImageDirectoryEntryToData");
    pDirectoryEntryToData = MmGetSystemRoutineAddress(&RoutineName);
    if (!pPcToFileHeader || !pDirectoryEntryToData)
        return NULL;

    if (!pPcToFileHeader((PVOID)NdisGetVersion, &ImageBase) || !ImageBase)
        return NULL;

    ExportDirectory = pDirectoryEntryToData(ImageBase, TRUE, IMAGE_DIRECTORY_ENTRY_EXPORT, &DirectorySize);
    if (!ExportDirectory || DirectorySize < sizeof(*ExportDirectory))
        return NULL;

    NameTable = (PULONG)((PUCHAR)ImageBase + ExportDirectory->AddressOfNames);
    OrdinalTable = (PUSHORT)((PUCHAR)ImageBase + ExportDirectory->AddressOfNameOrdinals);
    FunctionTable = (PULONG)((PUCHAR)ImageBase + ExportDirectory->AddressOfFunctions);

    for (Index = 0; Index < ExportDirectory->NumberOfNames; Index++)
    {
        if (strcmp((PCSTR)((PUCHAR)ImageBase + NameTable[Index]), Name) != 0)
            continue;

        FunctionIndex = OrdinalTable[Index];
        if (FunctionIndex >= ExportDirectory->NumberOfFunctions || !FunctionTable[FunctionIndex])
            return NULL;
        return (PUCHAR)ImageBase + FunctionTable[FunctionIndex];
    }

    return NULL;
}
