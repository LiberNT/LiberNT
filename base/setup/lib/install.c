/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS Setup Library
 * FILE:            base/setup/lib/install.c
 * PURPOSE:         Installation functions
 * PROGRAMMERS:     Hervé Poussineau (hpoussin@reactos.org)
 *                  Hermes Belusca-Maito (hermes.belusca@sfr.fr)
 */

/* INCLUDES *****************************************************************/

#include "precomp.h"
#include "filesup.h"
#include "infsupp.h"

#include "setuplib.h" // HACK for USETUP_DATA

#include "install.h"

#define NDEBUG
#include <debug.h>


/* FUNCTIONS ****************************************************************/

static NTSTATUS
BuildFullDirectoryPath(
    IN PCWSTR RootPath,
    IN PCWSTR BasePath,
    IN PCWSTR RelativePath,
    OUT PWSTR FullPath,
    IN SIZE_T cchFullPathSize)
{
    NTSTATUS Status;

    if ((RelativePath[0] == UNICODE_NULL) || (RelativePath[0] == L'\\' && RelativePath[1] == UNICODE_NULL))
    {
        /* Installation path */
        DPRINT("InstallationPath: '%S'\n", RelativePath);

        Status = CombinePaths(FullPath, cchFullPathSize, 2,
                              RootPath, BasePath);

        DPRINT("InstallationPath(2): '%S'\n", FullPath);
    }
    else if (RelativePath[0] == L'\\')
    {
        /* Absolute path */
        DPRINT("AbsolutePath: '%S'\n", RelativePath);

        Status = CombinePaths(FullPath, cchFullPathSize, 2,
                              RootPath, RelativePath);

        DPRINT("AbsolutePath(2): '%S'\n", FullPath);
    }
    else // if (RelativePath[0] != L'\\')
    {
        /* Path relative to the installation path */
        DPRINT("RelativePath: '%S'\n", RelativePath);

        Status = CombinePaths(FullPath, cchFullPathSize, 3,
                              RootPath, BasePath, RelativePath);

        DPRINT("RelativePath(2): '%S'\n", FullPath);
    }

    return Status;
}


static BOOLEAN
AddTreeToCopyQueue(
    IN PUSETUP_DATA pSetupData,
    IN PCWSTR SourceDirectory,
    IN PCWSTR TargetDirectory,
    IN PCWSTR MediaPath)
{
    NTSTATUS Status;
    UNICODE_STRING Name;
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    HANDLE DirectoryHandle;
    INFCONTEXT Context;
    BOOLEAN RestartScan = TRUE;
    BOOLEAN Success = TRUE;
    BOOLEAN Excluded;
    PWCHAR Paths, SubSource, SubTarget, SubMedia, FileName;
    union
    {
        FILE_DIRECTORY_INFORMATION Info;
        UCHAR Buffer[sizeof(FILE_DIRECTORY_INFORMATION) + MAX_PATH * sizeof(WCHAR)];
    } Entry;

    Status = SetupCreateDirectory(TargetDirectory);
    if (!NT_SUCCESS(Status) && Status != STATUS_OBJECT_NAME_COLLISION)
    {
        DPRINT1("Creating directory '%S' failed: Status = 0x%08lx\n", TargetDirectory, Status);
        pSetupData->LastErrorNumber = ERROR_CREATE_DIR;
        if (pSetupData->ErrorRoutine)
            pSetupData->ErrorRoutine(pSetupData, TargetDirectory);
        return FALSE;
    }

    RtlInitUnicodeString(&Name, SourceDirectory);
    InitializeObjectAttributes(&ObjectAttributes,
                               &Name,
                               OBJ_CASE_INSENSITIVE,
                               NULL,
                               NULL);
    Status = NtOpenFile(&DirectoryHandle,
                        FILE_LIST_DIRECTORY | SYNCHRONIZE,
                        &ObjectAttributes,
                        &IoStatusBlock,
                        FILE_SHARE_READ | FILE_SHARE_WRITE,
                        FILE_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("NtOpenFile(%S) failed (Status 0x%08lx)\n", SourceDirectory, Status);
        return FALSE;
    }

    Paths = RtlAllocateHeap(ProcessHeap, 0, 4 * MAX_PATH * sizeof(WCHAR));
    if (!Paths)
    {
        NtClose(DirectoryHandle);
        return FALSE;
    }
    SubSource = Paths;
    SubTarget = SubSource + MAX_PATH;
    SubMedia = SubTarget + MAX_PATH;
    FileName = SubMedia + MAX_PATH;

    for (;;)
    {
        Status = NtQueryDirectoryFile(DirectoryHandle,
                                      NULL,
                                      NULL,
                                      NULL,
                                      &IoStatusBlock,
                                      &Entry,
                                      sizeof(Entry),
                                      FileDirectoryInformation,
                                      TRUE,
                                      NULL,
                                      RestartScan);
        if (Status == STATUS_NO_MORE_FILES)
            break;
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("NtQueryDirectoryFile(%S) failed (Status 0x%08lx)\n", SourceDirectory, Status);
            Success = FALSE;
            break;
        }
        RestartScan = FALSE;

        RtlStringCchCopyNW(FileName, MAX_PATH,
                           Entry.Info.FileName,
                           Entry.Info.FileNameLength / sizeof(WCHAR));
        if (!wcscmp(FileName, L".") || !wcscmp(FileName, L".."))
            continue;

        CombinePaths(SubMedia, MAX_PATH, 2, MediaPath, FileName);
        Excluded = SpInfFindFirstLine(pSetupData->SetupInf, L"SystemTrees.Exclude", SubMedia, &Context);

        if (Entry.Info.FileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            CombinePaths(SubSource, MAX_PATH, 2, SourceDirectory, FileName);
            CombinePaths(SubTarget, MAX_PATH, 2, TargetDirectory, FileName);
            if (Excluded)
            {
                SetupCreateDirectory(SubTarget);
                continue;
            }
            if (!AddTreeToCopyQueue(pSetupData, SubSource, SubTarget, SubMedia))
            {
                Success = FALSE;
                break;
            }
        }
        else if (Excluded)
        {
            continue;
        }
        else if (!SpFileQueueCopy((HSPFILEQ)pSetupData->SetupFileQueue,
                                  SourceDirectory,
                                  NULL,
                                  FileName,
                                  NULL,
                                  NULL,
                                  TargetDirectory,
                                  NULL,
                                  0))
        {
            DPRINT1("SpFileQueueCopy(%S\\%S) failed\n", SourceDirectory, FileName);
        }
    }

    RtlFreeHeap(ProcessHeap, 0, Paths);
    NtClose(DirectoryHandle);
    return Success;
}

static VOID
AddComputerFilesToCopyQueue(
    IN PUSETUP_DATA pSetupData,
    IN PCWSTR SectionName,
    IN PCWSTR SystemTreePath)
{
    INFCONTEXT Context;
    PCWSTR SourceFileName;
    PCWSTR Directory;
    PCWSTR TargetFileName;
    WCHAR SourceDirectory[MAX_PATH];
    WCHAR TargetDirectory[MAX_PATH];

    if (!SpInfFindFirstLine(pSetupData->SetupInf, SectionName, NULL, &Context))
        return;

    do
    {
        if (!INF_GetDataField(&Context, 0, &SourceFileName))
            break;

        if (!INF_GetDataField(&Context, 1, &Directory))
        {
            INF_FreeData(SourceFileName);
            break;
        }

        if (!INF_GetDataField(&Context, 2, &TargetFileName))
        {
            TargetFileName = NULL;
        }
        else if (!*TargetFileName)
        {
            INF_FreeData(TargetFileName);
            TargetFileName = NULL;
        }

        CombinePaths(SourceDirectory, ARRAYSIZE(SourceDirectory), 2,
                     SystemTreePath, Directory);
        CombinePaths(TargetDirectory, ARRAYSIZE(TargetDirectory), 2,
                     pSetupData->DestinationPath.Buffer, Directory);

        if (!SpFileQueueCopy((HSPFILEQ)pSetupData->SetupFileQueue,
                             SourceDirectory,
                             NULL,
                             SourceFileName,
                             NULL,
                             NULL,
                             TargetDirectory,
                             TargetFileName,
                             0))
        {
            DPRINT1("SpFileQueueCopy(%S\\%S) failed\n", SourceDirectory, SourceFileName);
        }

        INF_FreeData(TargetFileName);
        INF_FreeData(Directory);
        INF_FreeData(SourceFileName);

    } while (SpInfFindNextLine(&Context, &Context));
}

static VOID
QueueComputerFiles(
    _Inout_ PUSETUP_DATA pSetupData,
    _In_ PCWSTR SystemTreePath)
{
    PWCHAR AdditionalSectionName = NULL;
    PGENERIC_LIST_ENTRY Entry;

    Entry = GetCurrentListEntry(pSetupData->ComputerList);
    if (!Entry || SystemTreePath[0] == UNICODE_NULL)
        return;

    pSetupData->ComputerType = ((PGENENTRY)GetListEntryData(Entry))->Id;
    if (pSetupData->ComputerType &&
        ProcessComputerFiles(pSetupData->SetupInf, pSetupData->ComputerType, &AdditionalSectionName) &&
        AdditionalSectionName)
    {
        AddComputerFilesToCopyQueue(pSetupData, AdditionalSectionName, SystemTreePath);
    }
}

BOOLEAN // ERROR_NUMBER
NTAPI
PrepareFileCopy(
    IN OUT PUSETUP_DATA pSetupData,
    IN PFILE_COPY_STATUS_ROUTINE StatusRoutine OPTIONAL)
{
    NTSTATUS Status;
    INFCONTEXT TreesContext;
    PCWSTR MediaDirectory;
    PCWSTR TargetDirectory;
    WCHAR SourcePath[MAX_PATH];
    WCHAR TargetPath[MAX_PATH];
    WCHAR SystemTreePath[MAX_PATH];

    /* Create the file queue */
    pSetupData->SetupFileQueue = (PVOID)SpFileQueueOpen();
    if (pSetupData->SetupFileQueue == NULL)
    {
        pSetupData->LastErrorNumber = ERROR_COPY_QUEUE;
        if (pSetupData->ErrorRoutine)
            pSetupData->ErrorRoutine(pSetupData);
        return FALSE;
    }

    if (!SpInfFindFirstLine(pSetupData->SetupInf, L"SystemTrees", NULL, &TreesContext))
    {
        pSetupData->LastErrorNumber = ERROR_TXTSETUP_SECTION;
        if (pSetupData->ErrorRoutine)
            pSetupData->ErrorRoutine(pSetupData, L"SystemTrees");
        return FALSE;
    }

    SystemTreePath[0] = UNICODE_NULL;
    do
    {
        if (!INF_GetData(&TreesContext, &MediaDirectory, &TargetDirectory))
            break;

        CombinePaths(SourcePath, ARRAYSIZE(SourcePath), 2,
                     pSetupData->SourceRootPath.Buffer, MediaDirectory);
        Status = BuildFullDirectoryPath(pSetupData->DestinationRootPath.Buffer,
                                        pSetupData->InstallPath.Buffer,
                                        TargetDirectory,
                                        TargetPath,
                                        ARRAYSIZE(TargetPath));
        if (NT_SUCCESS(Status) &&
            ((TargetDirectory[0] == UNICODE_NULL) ||
             (TargetDirectory[0] == L'\\' && TargetDirectory[1] == UNICODE_NULL)))
        {
            RtlStringCchCopyW(SystemTreePath, ARRAYSIZE(SystemTreePath), SourcePath);
        }

        if (!NT_SUCCESS(Status) ||
            !AddTreeToCopyQueue(pSetupData, SourcePath, TargetPath, MediaDirectory))
        {
            if (pSetupData->LastErrorNumber != ERROR_CREATE_DIR)
            {
                pSetupData->LastErrorNumber = ERROR_TXTSETUP_SECTION;
                if (pSetupData->ErrorRoutine)
                    pSetupData->ErrorRoutine(pSetupData, MediaDirectory);
            }
            INF_FreeData(TargetDirectory);
            INF_FreeData(MediaDirectory);
            return FALSE;
        }

        INF_FreeData(TargetDirectory);
        INF_FreeData(MediaDirectory);

    } while (SpInfFindNextLine(&TreesContext, &TreesContext));

    QueueComputerFiles(pSetupData, SystemTreePath);
    return TRUE;
}

#define INVENTORY_IMAGE_LINE L"; Image "

typedef struct _INVENTORY
{
    PWSTR Text;
    PWSTR* Entries;
    ULONG Count;
    PCWSTR Image;
} INVENTORY, *PINVENTORY;

#define UPGRADE_MAX_TREES   8

typedef struct _UPGRADE_TREE
{
    WCHAR Media[MAX_PATH];
    WCHAR Target[MAX_PATH];
} UPGRADE_TREE, *PUPGRADE_TREE;

typedef struct _UPGRADE_PLAN
{
    PUSETUP_DATA SetupData;
    UPGRADE_TREE Trees[UPGRADE_MAX_TREES];
    ULONG TreeCount;
    ULONG SystemTree;
    WCHAR Name[MAX_PATH];
    WCHAR Source[MAX_PATH];
    WCHAR Target[MAX_PATH];
    WCHAR Parent[MAX_PATH];
} UPGRADE_PLAN, *PUPGRADE_PLAN;

static int
__cdecl
CompareInventoryEntries(
    const void* First,
    const void* Second)
{
    return _wcsicmp(*(PCWSTR const*)First, *(PCWSTR const*)Second);
}

static VOID
FreeInventory(
    _Inout_ PINVENTORY Inventory)
{
    if (Inventory->Entries)
        RtlFreeHeap(ProcessHeap, 0, Inventory->Entries);
    if (Inventory->Text)
        RtlFreeHeap(ProcessHeap, 0, Inventory->Text);
    RtlZeroMemory(Inventory, sizeof(*Inventory));
}

static NTSTATUS
LoadInventory(
    _In_ PCWSTR Path,
    _Out_ PINVENTORY Inventory)
{
    HANDLE FileHandle, SectionHandle;
    PVOID View;
    ULONG FileSize, cbText = 0, Lines = 1;
    PWSTR Line, Next;
    SIZE_T Length;
    NTSTATUS Status;

    RtlZeroMemory(Inventory, sizeof(*Inventory));
    Status = OpenAndMapFile(NULL, Path, &FileHandle, &FileSize, &SectionHandle, &View, FALSE);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = RtlUTF8ToUnicodeN(NULL, 0, &cbText, View, FileSize);
    if (NT_SUCCESS(Status))
    {
        Inventory->Text = RtlAllocateHeap(ProcessHeap, 0, cbText + sizeof(WCHAR));
        if (!Inventory->Text)
            Status = STATUS_NO_MEMORY;
    }
    if (NT_SUCCESS(Status))
        Status = RtlUTF8ToUnicodeN(Inventory->Text, cbText, &cbText, View, FileSize);
    UnMapAndCloseFile(FileHandle, SectionHandle, View);
    if (!NT_SUCCESS(Status))
    {
        FreeInventory(Inventory);
        return Status;
    }
    Inventory->Text[cbText / sizeof(WCHAR)] = UNICODE_NULL;

    for (Line = Inventory->Text; *Line; Line++)
    {
        if (*Line == L'\n')
            Lines++;
    }

    Inventory->Entries = RtlAllocateHeap(ProcessHeap, 0, Lines * sizeof(PWSTR));
    if (!Inventory->Entries)
    {
        FreeInventory(Inventory);
        return STATUS_NO_MEMORY;
    }

    for (Line = Inventory->Text; Line && *Line; Line = Next)
    {
        Next = wcschr(Line, L'\n');
        if (Next)
            *Next++ = UNICODE_NULL;
        Length = wcslen(Line);
        if (Length && Line[Length - 1] == L'\r')
            Line[--Length] = UNICODE_NULL;
        if (Length && *Line != L';')
            Inventory->Entries[Inventory->Count++] = Line;
        else if (!Inventory->Image && _wcsnicmp(Line, INVENTORY_IMAGE_LINE, ARRAYSIZE(INVENTORY_IMAGE_LINE) - 1) == 0)
            Inventory->Image = Line + ARRAYSIZE(INVENTORY_IMAGE_LINE) - 1;
    }

    qsort(Inventory->Entries, Inventory->Count, sizeof(PWSTR), CompareInventoryEntries);
    return STATUS_SUCCESS;
}

static BOOLEAN
IsInInventory(
    _In_ PINVENTORY Inventory,
    _In_ PCWSTR Entry)
{
    return Inventory->Count != 0 &&
           bsearch(&Entry, Inventory->Entries, Inventory->Count, sizeof(PWSTR), CompareInventoryEntries) != NULL;
}

static BOOLEAN
InitUpgradePlan(
    _In_ PUSETUP_DATA pSetupData,
    _Out_ PUPGRADE_PLAN Plan)
{
    INFCONTEXT Context;
    PCWSTR Media, Target;
    PUPGRADE_TREE Tree;

    RtlZeroMemory(Plan, sizeof(*Plan));
    Plan->SetupData = pSetupData;
    Plan->SystemTree = UPGRADE_MAX_TREES;

    if (!SpInfFindFirstLine(pSetupData->SetupInf, L"SystemTrees", NULL, &Context))
        return FALSE;

    do
    {
        if (Plan->TreeCount >= UPGRADE_MAX_TREES || !INF_GetData(&Context, &Media, &Target))
            break;

        Tree = &Plan->Trees[Plan->TreeCount];
        if (NT_SUCCESS(RtlStringCchCopyW(Tree->Media, ARRAYSIZE(Tree->Media), Media)) &&
            NT_SUCCESS(BuildFullDirectoryPath(pSetupData->DestinationRootPath.Buffer,
                                              pSetupData->InstallPath.Buffer,
                                              Target,
                                              Tree->Target,
                                              ARRAYSIZE(Tree->Target))))
        {
            if (Target[0] == UNICODE_NULL || (Target[0] == L'\\' && Target[1] == UNICODE_NULL))
                Plan->SystemTree = Plan->TreeCount;
            Plan->TreeCount++;
        }

        INF_FreeData(Target);
        INF_FreeData(Media);
    } while (SpInfFindNextLine(&Context, &Context));

    return Plan->SystemTree < Plan->TreeCount;
}

static BOOLEAN
MapUpgradeEntry(
    _Inout_ PUPGRADE_PLAN Plan,
    _In_ PCWSTR Entry,
    _Out_ PBOOLEAN Directory)
{
    INFCONTEXT Context;
    PWCHAR Separator;
    SIZE_T Length, MediaLength = 0;
    BOOLEAN Excluded;
    WCHAR Saved;
    ULONG i;

    *Directory = FALSE;
    if (!NT_SUCCESS(RtlStringCchCopyW(Plan->Name, ARRAYSIZE(Plan->Name), Entry)))
        return FALSE;

    Length = wcslen(Plan->Name);
    if (Length && Plan->Name[Length - 1] == L'\\')
    {
        *Directory = TRUE;
        Plan->Name[--Length] = UNICODE_NULL;
    }
    if (Length == 0)
        return FALSE;

    for (i = 0; i < Plan->TreeCount; i++)
    {
        MediaLength = wcslen(Plan->Trees[i].Media);
        if (_wcsnicmp(Plan->Name, Plan->Trees[i].Media, MediaLength) == 0 &&
            (Plan->Name[MediaLength] == L'\\' || Plan->Name[MediaLength] == UNICODE_NULL))
        {
            break;
        }
    }
    if (i == Plan->TreeCount)
        return FALSE;

    for (Separator = wcschr(Plan->Name, L'\\'); ; Separator = wcschr(Separator + 1, L'\\'))
    {
        Saved = Separator ? *Separator : UNICODE_NULL;
        if (Separator)
            *Separator = UNICODE_NULL;
        Excluded = SpInfFindFirstLine(Plan->SetupData->SetupInf, L"SystemTrees.Exclude", Plan->Name, &Context);
        if (Separator)
            *Separator = Saved;
        if (Excluded)
            return FALSE;
        if (!Separator)
            break;
    }

    return NT_SUCCESS(CombinePaths(Plan->Source, ARRAYSIZE(Plan->Source), 2,
                                   Plan->SetupData->SourceRootPath.Buffer, Plan->Name)) &&
           NT_SUCCESS(CombinePaths(Plan->Target, ARRAYSIZE(Plan->Target), 2,
                                   Plan->Trees[i].Target, Plan->Name + MediaLength));
}

static BOOLEAN
IsPreservedEntry(
    _In_ PUPGRADE_PLAN Plan)
{
    INFCONTEXT Context;

    return SpInfFindFirstLine(Plan->SetupData->SetupInf, L"SystemTrees.Preserve", Plan->Name, &Context);
}

static NTSTATUS
RemoveEmptyDirectory(
    _In_ PCWSTR Path)
{
    FILE_DISPOSITION_INFORMATION Disposition = { TRUE };
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    UNICODE_STRING Name;
    HANDLE Handle;
    NTSTATUS Status;

    RtlInitUnicodeString(&Name, Path);
    InitializeObjectAttributes(&ObjectAttributes, &Name, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtOpenFile(&Handle,
                        DELETE | SYNCHRONIZE,
                        &ObjectAttributes,
                        &IoStatusBlock,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        FILE_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_FOR_BACKUP_INTENT);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = NtSetInformationFile(Handle, &IoStatusBlock, &Disposition, sizeof(Disposition),
                                  FileDispositionInformation);
    NtClose(Handle);
    return Status;
}

static VOID
DropObsoleteFiles(
    _Inout_ PUPGRADE_PLAN Plan,
    _In_ PINVENTORY Old,
    _In_ PINVENTORY New)
{
    BOOLEAN Directory;
    NTSTATUS Status;
    ULONG i;

    for (i = Old->Count; i > 0; i--)
    {
        if (IsInInventory(New, Old->Entries[i - 1]) ||
            !MapUpgradeEntry(Plan, Old->Entries[i - 1], &Directory) ||
            IsPreservedEntry(Plan))
        {
            continue;
        }

        Status = Directory ? RemoveEmptyDirectory(Plan->Target) : SetupDeleteFile(Plan->Target, TRUE);
        DPRINT1("Upgrade drops '%S' (Status 0x%08lx)\n", Plan->Target, Status);
    }
}

static VOID
QueueInventoryFiles(
    _Inout_ PUPGRADE_PLAN Plan,
    _In_ PINVENTORY New)
{
    PUSETUP_DATA pSetupData = Plan->SetupData;
    PWCHAR SourceName, TargetName;
    BOOLEAN Directory;
    NTSTATUS Status;
    ULONG i;

    for (i = 0; i < New->Count; i++)
    {
        if (!MapUpgradeEntry(Plan, New->Entries[i], &Directory))
            continue;

        if (Directory)
        {
            Status = SetupCreateDirectory(Plan->Target);
            if (!NT_SUCCESS(Status) && Status != STATUS_OBJECT_NAME_COLLISION)
                DPRINT1("Creating '%S' failed (Status 0x%08lx)\n", Plan->Target, Status);
            continue;
        }

        if (IsPreservedEntry(Plan) && DoesFileExist(NULL, Plan->Target))
            continue;
        if (!DoesFileExist(NULL, Plan->Source))
        {
            DPRINT1("'%S' is listed but missing from the source\n", Plan->Source);
            continue;
        }

        SourceName = wcsrchr(Plan->Source, L'\\');
        TargetName = wcsrchr(Plan->Target, L'\\');
        if (!SourceName || !TargetName)
            continue;
        *SourceName++ = UNICODE_NULL;
        *TargetName = UNICODE_NULL;

        if (_wcsicmp(Plan->Parent, Plan->Target) != 0)
        {
            Status = SetupCreateDirectory(Plan->Target);
            if (!NT_SUCCESS(Status) && Status != STATUS_OBJECT_NAME_COLLISION)
                DPRINT1("Creating '%S' failed (Status 0x%08lx)\n", Plan->Target, Status);
            RtlStringCchCopyW(Plan->Parent, ARRAYSIZE(Plan->Parent), Plan->Target);
        }

        if (!SpFileQueueCopy((HSPFILEQ)pSetupData->SetupFileQueue,
                             Plan->Source,
                             NULL,
                             SourceName,
                             NULL,
                             NULL,
                             Plan->Target,
                             NULL,
                             0))
        {
            DPRINT1("SpFileQueueCopy(%S\\%S) failed\n", Plan->Source, SourceName);
        }
    }
}

BOOLEAN
NTAPI
PrepareUpgradeFileCopy(
    _Inout_ PUSETUP_DATA pSetupData,
    _In_opt_ PFILE_COPY_STATUS_ROUTINE StatusRoutine)
{
    PUPGRADE_PLAN Plan;
    INVENTORY New, Old;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(StatusRoutine);

    pSetupData->SetupFileQueue = (PVOID)SpFileQueueOpen();
    if (pSetupData->SetupFileQueue == NULL)
    {
        pSetupData->LastErrorNumber = ERROR_COPY_QUEUE;
        if (pSetupData->ErrorRoutine)
            pSetupData->ErrorRoutine(pSetupData);
        return FALSE;
    }

    Plan = RtlAllocateHeap(ProcessHeap, 0, sizeof(*Plan));
    if (!Plan)
        return FALSE;

    if (!InitUpgradePlan(pSetupData, Plan))
    {
        pSetupData->LastErrorNumber = ERROR_TXTSETUP_SECTION;
        if (pSetupData->ErrorRoutine)
            pSetupData->ErrorRoutine(pSetupData, L"SystemTrees");
        RtlFreeHeap(ProcessHeap, 0, Plan);
        return FALSE;
    }

    CombinePaths(Plan->Source, ARRAYSIZE(Plan->Source), 3, pSetupData->SourceRootPath.Buffer,
                 Plan->Trees[Plan->SystemTree].Media, UPGRADE_INVENTORY_PATH);
    Status = LoadInventory(Plan->Source, &New);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Cannot read the file inventory '%S' (Status 0x%08lx)\n", Plan->Source, Status);
        RtlFreeHeap(ProcessHeap, 0, Plan);
        return FALSE;
    }

    CombinePaths(Plan->Target, ARRAYSIZE(Plan->Target), 2,
                 Plan->Trees[Plan->SystemTree].Target, UPGRADE_INVENTORY_PATH);
    Status = LoadInventory(Plan->Target, &Old);
    DPRINT1("Upgrade from %lu to %lu inventory entries (Status 0x%08lx)\n", Old.Count, New.Count, Status);

    if (Old.Image && New.Image && _wcsicmp(Old.Image, New.Image) == 0)
        DropObsoleteFiles(Plan, &Old, &New);
    else if (Old.Count)
        DPRINT1("The system was installed from image '%S', this is image '%S'; nothing is dropped\n",
                Old.Image ? Old.Image : L"unknown", New.Image ? New.Image : L"unknown");
    QueueInventoryFiles(Plan, &New);

    CombinePaths(Plan->Source, ARRAYSIZE(Plan->Source), 2, pSetupData->SourceRootPath.Buffer,
                 Plan->Trees[Plan->SystemTree].Media);
    QueueComputerFiles(pSetupData, Plan->Source);

    FreeInventory(&Old);
    FreeInventory(&New);
    RtlFreeHeap(ProcessHeap, 0, Plan);
    return TRUE;
}

BOOLEAN
NTAPI
DoFileCopy(
    IN OUT PUSETUP_DATA pSetupData,
    IN PSP_FILE_CALLBACK_W MsgHandler,
    IN PVOID Context OPTIONAL)
{
    BOOLEAN Success;

    Success = SpFileQueueCommit(NULL,
                                (HSPFILEQ)pSetupData->SetupFileQueue,
                                MsgHandler,
                                Context);

    SpFileQueueClose((HSPFILEQ)pSetupData->SetupFileQueue);
    pSetupData->SetupFileQueue = NULL;

    return Success;
}

/* EOF */
