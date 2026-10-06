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
    PWCHAR AdditionalSectionName = NULL;
    PGENERIC_LIST_ENTRY Entry;
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

    Entry = GetCurrentListEntry(pSetupData->ComputerList);
    if (Entry && SystemTreePath[0] != UNICODE_NULL)
    {
        pSetupData->ComputerType = ((PGENENTRY)GetListEntryData(Entry))->Id;
        if (pSetupData->ComputerType &&
            ProcessComputerFiles(pSetupData->SetupInf, pSetupData->ComputerType, &AdditionalSectionName) &&
            AdditionalSectionName)
        {
            AddComputerFilesToCopyQueue(pSetupData, AdditionalSectionName, SystemTreePath);
        }
    }

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
