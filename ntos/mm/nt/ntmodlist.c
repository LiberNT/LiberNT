/*
 * PROJECT:     LiberNT NT Virtual Memory Subsystem
 * LICENSE:     GPL-3.0-only (https://spdx.org/licenses/GPL-3.0-only)
 * PURPOSE:     Loaded module list, import reference lists and boot images
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <mm/nt/ntsysimg.h>
#define NDEBUG
#include <debug.h>

LIST_ENTRY PsLoadedModuleList;
LIST_ENTRY MmLoadedUserImageList;
KSPIN_LOCK PsLoadedModuleSpinLock;
ERESOURCE PsLoadedModuleResource;
ULONG_PTR PsNtosImageBase;
KMUTANT MmSystemLoadLock;
PVOID MmUnloadedDrivers;
PVOID MmLastUnloadedDrivers;

static
PLDR_DATA_TABLE_ENTRY
MiModuleFromLink(
    _In_ PLIST_ENTRY Link)
{
    return CONTAINING_RECORD(Link, LDR_DATA_TABLE_ENTRY, InLoadOrderLinks);
}

#if DBG
static
BOOLEAN
MiOwnsSystemImages(VOID)
{
    return (BOOLEAN)(MmSystemLoadLock.OwnerThread == KeGetCurrentThread());
}
#endif

VOID
MiLockSystemImages(VOID)
{
    PAGED_CODE();

    KeEnterCriticalRegion();
    KeWaitForSingleObject(&MmSystemLoadLock, WrVirtualMemory, KernelMode, FALSE, NULL);
    ASSERT(MiOwnsSystemImages());
}

VOID
MiUnlockSystemImages(VOID)
{
    ASSERT(MiOwnsSystemImages());

    KeReleaseMutant(&MmSystemLoadLock, MUTANT_INCREMENT, FALSE, FALSE);
    KeLeaveCriticalRegion();
}

PLDR_DATA_TABLE_ENTRY
MiFindSystemImage(
    _In_ PCUNICODE_STRING Name,
    _In_ BOOLEAN MatchFullName)
{
    PLDR_DATA_TABLE_ENTRY Entry;
    PLIST_ENTRY Link;

    ASSERT(MiOwnsSystemImages());

    for (Link = PsLoadedModuleList.Flink; Link != &PsLoadedModuleList; Link = Link->Flink)
    {
        Entry = MiModuleFromLink(Link);
        if (RtlEqualUnicodeString(Name, MatchFullName ? &Entry->FullDllName : &Entry->BaseDllName, TRUE))
            return Entry;
    }

    return NULL;
}

static
VOID
MiEditModuleList(
    _In_ PLDR_DATA_TABLE_ENTRY Entry,
    _In_ BOOLEAN Publish)
{
    KIRQL OldIrql;

    ASSERT(MiOwnsSystemImages());
    ASSERT(KeAreApcsDisabled());

    ExAcquireResourceExclusiveLite(&PsLoadedModuleResource, TRUE);
    OldIrql = KeAcquireSpinLockRaiseToSynch(&PsLoadedModuleSpinLock);

    if (Publish)
    {
        InsertTailList(&PsLoadedModuleList, &Entry->InLoadOrderLinks);
    }
    else
    {
        RemoveEntryList(&Entry->InLoadOrderLinks);
        Entry->InLoadOrderLinks.Flink = NULL;
        Entry->InLoadOrderLinks.Blink = NULL;
    }

    KeReleaseSpinLock(&PsLoadedModuleSpinLock, OldIrql);
    ExReleaseResourceLite(&PsLoadedModuleResource);
}

VOID
MiPublishSystemImage(
    _In_ PLDR_DATA_TABLE_ENTRY Entry)
{
    ASSERT(Entry->InLoadOrderLinks.Flink == NULL);
    MiEditModuleList(Entry, TRUE);
}

VOID
MiWithdrawSystemImage(
    _In_ PLDR_DATA_TABLE_ENTRY Entry)
{
    ASSERT(Entry->InLoadOrderLinks.Flink != NULL);
    MiEditModuleList(Entry, FALSE);
}

PLDR_DATA_TABLE_ENTRY
NTAPI
MiLookupDataTableEntry(
    _In_ PVOID Address)
{
    PLDR_DATA_TABLE_ENTRY Entry;
    PLIST_ENTRY Link;

    for (Link = PsLoadedModuleList.Flink; Link != &PsLoadedModuleList; Link = Link->Flink)
    {
        Entry = MiModuleFromLink(Link);
        if ((ULONG_PTR)Address - (ULONG_PTR)Entry->DllBase < Entry->SizeOfImage)
            return Entry;
    }

    return NULL;
}

static
BOOLEAN
MiImportListIsTable(
    _In_opt_ PVOID ImportList)
{
    return (BOOLEAN)(ImportList != NULL &&
                     ImportList != MM_SYSLDR_NO_IMPORTS &&
                     ImportList != MM_SYSLDR_BOOT_LOADED &&
                     !((ULONG_PTR)ImportList & MM_SYSLDR_SINGLE_ENTRY));
}

ULONG
MiImportListCount(
    _In_opt_ PVOID ImportList)
{
    if (ImportList == NULL || ImportList == MM_SYSLDR_NO_IMPORTS || ImportList == MM_SYSLDR_BOOT_LOADED)
        return 0;

    if ((ULONG_PTR)ImportList & MM_SYSLDR_SINGLE_ENTRY)
        return 1;

    return (ULONG)((PLOAD_IMPORTS)ImportList)->Count;
}

PLDR_DATA_TABLE_ENTRY
MiImportListEntry(
    _In_ PVOID ImportList,
    _In_ ULONG Index)
{
    ASSERT(Index < MiImportListCount(ImportList));

    if ((ULONG_PTR)ImportList & MM_SYSLDR_SINGLE_ENTRY)
        return (PLDR_DATA_TABLE_ENTRY)((ULONG_PTR)ImportList & ~(ULONG_PTR)MM_SYSLDR_SINGLE_ENTRY);

    return ((PLOAD_IMPORTS)ImportList)->Entry[Index];
}

VOID
MiFreeImportList(
    _In_opt_ PVOID ImportList)
{
    if (MiImportListIsTable(ImportList))
        ExFreePoolWithTag(ImportList, TAG_LDR_IMPORTS);
}

PVOID
MiCreateImportList(
    _In_reads_(Count) PLDR_DATA_TABLE_ENTRY *Modules,
    _In_ ULONG Count)
{
    PLOAD_IMPORTS Table;
    ULONG Index;

    if (Count == 0)
        return MM_SYSLDR_NO_IMPORTS;

    if (Count == 1)
    {
        ASSERT(((ULONG_PTR)Modules[0] & MM_SYSLDR_SINGLE_ENTRY) == 0);
        return (PVOID)((ULONG_PTR)Modules[0] | MM_SYSLDR_SINGLE_ENTRY);
    }

    Table = ExAllocatePoolWithTag(PagedPool, FIELD_OFFSET(LOAD_IMPORTS, Entry[Count]), TAG_LDR_IMPORTS);
    if (Table == NULL)
        return NULL;

    Table->Count = Count;
    for (Index = 0; Index < Count; Index++)
    {
        ASSERT(Modules[Index] != NULL);
        Table->Entry[Index] = Modules[Index];
    }

    ASSERT(MiImportListIsTable(Table));
    return Table;
}

BOOLEAN
NTAPI
MmIsSystemImageImportingModule(
    _In_ PVOID ImageBase,
    _In_ PCUNICODE_STRING ModuleName)
{
    PLDR_DATA_TABLE_ENTRY Entry;
    BOOLEAN Found = FALSE;
    ULONG Index, Count;

    PAGED_CODE();

    if (!ImageBase || !ModuleName || !ModuleName->Buffer)
        return FALSE;

    if (!PsLoadedModuleList.Flink || !PsLoadedModuleList.Blink)
        return FALSE;

    KeEnterCriticalRegion();
    ExAcquireResourceSharedLite(&PsLoadedModuleResource, TRUE);

    Entry = MiLookupDataTableEntry(ImageBase);
    Count = Entry ? MiImportListCount(Entry->LoadedImports) : 0;

    for (Index = 0; Index < Count && !Found; Index++)
        Found = RtlEqualUnicodeString(&MiImportListEntry(Entry->LoadedImports, Index)->BaseDllName, ModuleName, TRUE);

    ExReleaseResourceLite(&PsLoadedModuleResource);
    KeLeaveCriticalRegion();
    return Found;
}

CODE_SEG("INIT")
static
PLDR_DATA_TABLE_ENTRY
MiCopyBootImageEntry(
    _In_ PLDR_DATA_TABLE_ENTRY Source)
{
    PLDR_DATA_TABLE_ENTRY Copy;
    USHORT BaseBytes = Source->BaseDllName.Length;
    USHORT FullBytes = Source->FullDllName.Length;

    Copy = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Copy) + BaseBytes + sizeof(UNICODE_NULL), TAG_MODULE_OBJECT);
    if (Copy == NULL)
        return NULL;

    *Copy = *Source;
    Copy->InLoadOrderLinks.Flink = NULL;
    Copy->InLoadOrderLinks.Blink = NULL;
    InitializeListHead(&Copy->InMemoryOrderLinks);
    InitializeListHead(&Copy->InInitializationOrderLinks);

    Copy->BaseDllName.Buffer = (PWCH)(Copy + 1);
    Copy->BaseDllName.Length = BaseBytes;
    Copy->BaseDllName.MaximumLength = BaseBytes + sizeof(UNICODE_NULL);
    RtlCopyMemory(Copy->BaseDllName.Buffer, Source->BaseDllName.Buffer, BaseBytes);
    Copy->BaseDllName.Buffer[BaseBytes / sizeof(WCHAR)] = UNICODE_NULL;

    Copy->FullDllName.Buffer = ExAllocatePoolWithTag(PagedPool, FullBytes + sizeof(UNICODE_NULL), TAG_LDR_WSTR);
    if (Copy->FullDllName.Buffer == NULL)
    {
        ExFreePoolWithTag(Copy, TAG_MODULE_OBJECT);
        return NULL;
    }

    Copy->FullDllName.Length = FullBytes;
    Copy->FullDllName.MaximumLength = FullBytes + sizeof(UNICODE_NULL);
    RtlCopyMemory(Copy->FullDllName.Buffer, Source->FullDllName.Buffer, FullBytes);
    Copy->FullDllName.Buffer[FullBytes / sizeof(WCHAR)] = UNICODE_NULL;

    return Copy;
}

CODE_SEG("INIT")
static
VOID
MiReplaceBootDriverEntry(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _In_ PLDR_DATA_TABLE_ENTRY LoaderEntry,
    _In_ PLDR_DATA_TABLE_ENTRY Entry)
{
    PBOOT_DRIVER_LIST_ENTRY BootEntry;
    PLIST_ENTRY Link;

    for (Link = LoaderBlock->BootDriverListHead.Flink;
         Link != &LoaderBlock->BootDriverListHead;
         Link = Link->Flink)
    {
        BootEntry = CONTAINING_RECORD(Link, BOOT_DRIVER_LIST_ENTRY, Link);
        if (BootEntry->LdrEntry == LoaderEntry)
            BootEntry->LdrEntry = Entry;
    }
}

CODE_SEG("INIT")
static
ULONG
MiCollectBootBindings(
    _In_ PLDR_DATA_TABLE_ENTRY Importer,
    _Out_writes_(Capacity) PLDR_DATA_TABLE_ENTRY *Targets,
    _In_ ULONG Capacity)
{
    PIMAGE_IMPORT_DESCRIPTOR Descriptor;
    PLDR_DATA_TABLE_ENTRY Current = NULL;
    PULONG_PTR Slot;
    ULONG Size, Found = 0, Index;

    Descriptor = RtlImageDirectoryEntryToData(Importer->DllBase, TRUE, IMAGE_DIRECTORY_ENTRY_IMPORT, &Size);
    if (Descriptor == NULL)
        return 0;

    for (; Descriptor->Name != 0 && Descriptor->OriginalFirstThunk != 0; Descriptor++)
    {
        for (Slot = (PULONG_PTR)((ULONG_PTR)Importer->DllBase + Descriptor->FirstThunk); *Slot != 0; Slot++)
        {
            if (Current != NULL && *Slot - (ULONG_PTR)Current->DllBase < Current->SizeOfImage)
                continue;

            Current = MiLookupDataTableEntry((PVOID)*Slot);
            ASSERTMSG("A boot image is bound to an address outside every loaded image\n", Current != NULL);
            if (Current == NULL)
                continue;

            for (Index = 0; Index < Found && Targets[Index] != Current; Index++)
                NOTHING;

            if (Index == Found)
            {
                ASSERT(Found < Capacity);
                Targets[Found++] = Current;
            }
        }
    }

    return Found;
}

CODE_SEG("INIT")
static
BOOLEAN
MiLinkBootImageImports(
    _In_ PLDR_DATA_TABLE_ENTRY Kernel,
    _In_ PLDR_DATA_TABLE_ENTRY Hal,
    _In_ ULONG ModuleCount)
{
    PLDR_DATA_TABLE_ENTRY *Targets, Importer;
    PLIST_ENTRY Link;
    ULONG Found, Index, Kept;
    PVOID List;

    Targets = ExAllocatePoolWithTag(PagedPool, ModuleCount * sizeof(*Targets), TAG_LDR_IMPORTS);
    if (Targets == NULL)
        return FALSE;

    for (Link = PsLoadedModuleList.Flink; Link != &PsLoadedModuleList; Link = Link->Flink)
    {
        Importer = MiModuleFromLink(Link);
        Found = MiCollectBootBindings(Importer, Targets, ModuleCount);

        for (Index = 0, Kept = 0; Index < Found; Index++)
        {
            if (Targets[Index] == Kernel || Targets[Index] == Hal)
                continue;

            Targets[Index]->LoadCount++;
            Targets[Kept++] = Targets[Index];
        }

        if (Importer == Kernel || Importer == Hal)
            continue;

        List = MiCreateImportList(Targets, Kept);
        if (List == NULL)
        {
            ExFreePoolWithTag(Targets, TAG_LDR_IMPORTS);
            return FALSE;
        }

        Importer->LoadedImports = List;
    }

    ExFreePoolWithTag(Targets, TAG_LDR_IMPORTS);
    return TRUE;
}

CODE_SEG("INIT")
BOOLEAN
NTAPI
MiInitializeLoadedModuleList(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    UNICODE_STRING KernelName = RTL_CONSTANT_STRING(L"ntoskrnl.exe");
    UNICODE_STRING HalName = RTL_CONSTANT_STRING(L"hal.dll");
    PLDR_DATA_TABLE_ENTRY Source, Copy, Kernel = NULL, Hal = NULL;
    PLIST_ENTRY Link;
    ULONG Count = 0;

    ExInitializeResourceLite(&PsLoadedModuleResource);
    KeInitializeSpinLock(&PsLoadedModuleSpinLock);
    InitializeListHead(&PsLoadedModuleList);
    InitializeListHead(&MmLoadedUserImageList);

    ASSERT(!IsListEmpty(&LoaderBlock->LoadOrderListHead));
    PsNtosImageBase = (ULONG_PTR)MiModuleFromLink(LoaderBlock->LoadOrderListHead.Flink)->DllBase;

    for (Link = LoaderBlock->LoadOrderListHead.Flink; Link != &LoaderBlock->LoadOrderListHead; Link = Link->Flink)
    {
        Source = MiModuleFromLink(Link);
        if (RtlImageNtHeader(Source->DllBase) == NULL)
            continue;

        Copy = MiCopyBootImageEntry(Source);
        if (Copy == NULL)
            return FALSE;

        Copy->LoadedImports = MM_SYSLDR_BOOT_LOADED;
        InsertTailList(&PsLoadedModuleList, &Copy->InLoadOrderLinks);
        MiReplaceBootDriverEntry(LoaderBlock, Source, Copy);

        if (Kernel == NULL && RtlEqualUnicodeString(&Copy->BaseDllName, &KernelName, TRUE))
            Kernel = Copy;
        else if (Hal == NULL && RtlEqualUnicodeString(&Copy->BaseDllName, &HalName, TRUE))
            Hal = Copy;

        Count++;
    }

    ASSERT(Kernel != NULL && Hal != NULL);
    if (Kernel == NULL || Hal == NULL)
        return TRUE;

    for (Link = PsLoadedModuleList.Flink; Link != &PsLoadedModuleList; Link = Link->Flink)
    {
        Copy = MiModuleFromLink(Link);
        Copy->LoadCount = ((Copy->Flags & LDRP_DRIVER_DEPENDENT_DLL) && Copy != Kernel && Copy != Hal) ? 0 : 1;
        if (Copy != Kernel && Copy != Hal)
            Copy->LoadedImports = MM_SYSLDR_NO_IMPORTS;
    }

    return MiLinkBootImageImports(Kernel, Hal, Count);
}
