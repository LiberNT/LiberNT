/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     A driver object names its entry in the loaded module list
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <ndk/ldrtypes.h>

static
BOOLEAN
IsLoadedModuleEntry(
    _In_ PLIST_ENTRY LoadedModules,
    _In_ PERESOURCE LoadedModulesLock,
    _In_ PVOID Section)
{
    PLIST_ENTRY Link;
    BOOLEAN Found = FALSE;

    KeEnterCriticalRegion();
    ExAcquireResourceSharedLite(LoadedModulesLock, TRUE);
    for (Link = LoadedModules->Flink; Link != LoadedModules; Link = Link->Flink)
    {
        if (CONTAINING_RECORD(Link, LDR_DATA_TABLE_ENTRY, InLoadOrderLinks) == Section)
        {
            Found = TRUE;
            break;
        }
    }
    ExReleaseResourceLite(LoadedModulesLock);
    KeLeaveCriticalRegion();

    return Found;
}

START_TEST(IoDriverSection)
{
    static PCWSTR const Names[] =
    {
        L"\\Driver\\ACPI",
        L"\\Driver\\PCI",
        L"\\Driver\\Disk",
        L"\\Driver\\partmgr",
        L"\\Driver\\volmgr",
        L"\\FileSystem\\Ntfs",
    };
    UNICODE_STRING ListName = RTL_CONSTANT_STRING(L"PsLoadedModuleList");
    UNICODE_STRING LockName = RTL_CONSTANT_STRING(L"PsLoadedModuleResource");
    PLIST_ENTRY LoadedModules;
    PERESOURCE LoadedModulesLock;
    PDRIVER_OBJECT Driver;
    UNICODE_STRING Name;
    ULONG Index, Checked = 0;
    NTSTATUS Status;

    LoadedModules = MmGetSystemRoutineAddress(&ListName);
    LoadedModulesLock = MmGetSystemRoutineAddress(&LockName);
    if (skip(LoadedModules != NULL && LoadedModulesLock != NULL, "The loaded module list is not exported\n"))
        return;

    for (Index = 0; Index < RTL_NUMBER_OF(Names); Index++)
    {
        RtlInitUnicodeString(&Name, Names[Index]);
        Status = ObReferenceObjectByName(&Name, OBJ_CASE_INSENSITIVE, NULL, 0, IoDriverObjectType, KernelMode,
                                         NULL, (PVOID *)&Driver);
        if (!NT_SUCCESS(Status))
            continue;

        ok(Driver->DriverSection != NULL, "%wZ has no module entry\n", &Name);
        if (Driver->DriverSection != NULL)
        {
            ok(IsLoadedModuleEntry(LoadedModules, LoadedModulesLock, Driver->DriverSection),
               "%wZ names a module entry %p outside the loaded module list\n", &Name, Driver->DriverSection);
            Checked++;
        }
        ObDereferenceObject(Driver);
    }

    trace("%lu driver objects checked\n", Checked);
    ok(Checked != 0, "No boot driver object found\n");
}
