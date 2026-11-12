/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Holding on to what a crash dump will need
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "../ioint.h"
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

ULONG IopNumTriageDumpDataBlocks = 0;
PVOID IopTriageDumpDataBlocks[64];

/* PRIVATE FUNCTIONS **********************************************************/

static
VOID
IopReadCrashControl(VOID)
{
    RTL_QUERY_REGISTRY_TABLE QueryTable[2];
    ULONG DefaultAutoReboot = 0;

    RtlZeroMemory(QueryTable, sizeof(QueryTable));
    QueryTable[0].Flags = RTL_QUERY_REGISTRY_DIRECT;
    QueryTable[0].Name = L"AutoReboot";
    QueryTable[0].EntryContext = &IopAutoReboot;
    QueryTable[0].DefaultType = REG_DWORD;
    QueryTable[0].DefaultData = &DefaultAutoReboot;
    QueryTable[0].DefaultLength = sizeof(DefaultAutoReboot);
    RtlQueryRegistryValues(RTL_REGISTRY_CONTROL, L"CrashControl", QueryTable, NULL, NULL);
}

/* PUBLIC FUNCTIONS ***********************************************************/

BOOLEAN
NTAPI
IoInitializeCrashDumpCore(
    IN PLOADER_PARAMETER_BLOCK LoaderBlock OPTIONAL)
{
    IopReadCrashControl();

    IopNumTriageDumpDataBlocks = 0;
    RtlZeroMemory(IopTriageDumpDataBlocks, sizeof(IopTriageDumpDataBlocks));

    return KdpInitializeCrashDumpCore(LoaderBlock);
}

BOOLEAN
NTAPI
IoInitializeCrashDump(
    IN HANDLE DumpFileHandle)
{
    PAGED_CODE();

    return KdpInitializeCrashDump(DumpFileHandle);
}

BOOLEAN
NTAPI
IoInitializeDedicatedCrashDump(
    IN PFILE_OBJECT BootFileObject)
{
    PAGED_CODE();

    return KdpInitializeDedicatedCrashDump(BootFileObject);
}

BOOLEAN
NTAPI
IoInitializeBootPageFileCrashDump(
    IN PCUNICODE_STRING BootDeviceName)
{
    PAGED_CODE();

    return KdpInitializeBootPageFileCrashDump(BootDeviceName);
}

BOOLEAN
NTAPI
IoReleaseCrashDumpFile(VOID)
{
    PAGED_CODE();

    return KdpReleaseCrashDumpFile();
}

VOID
NTAPI
IoPublishCrashDumpArtifacts(VOID)
{
    PAGED_CODE();

    KdpPublishCrashDumpArtifacts();
}

/* EOF */
