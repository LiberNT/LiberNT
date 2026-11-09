/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Hives: their files, their place in the namespace and their life
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

#include "cmpriv.h"

/* PRIVATE DATA ***************************************************************/

static UNICODE_STRING CmpHiveListKeyName = RTL_CONSTANT_STRING(
    L"\\REGISTRY\\MACHINE\\SYSTEM\\CurrentControlSet\\Control\\hivelist");

static UNICODE_STRING CmpLogExtension = RTL_CONSTANT_STRING(L".LOG");

/* PRIVATE FUNCTIONS **********************************************************/

HCELL_INDEX
NTAPI
CmpHiveSecurityAnchor(
    _In_ PHHIVE Hive)
{
    PCM_KEY_NODE Root;
    HCELL_INDEX Anchor;

    if (Hive->BaseBlock == NULL) return HCELL_NIL;

    /* A hive that has just been made has no root yet, so it anchors nothing */
    if (Hive->BaseBlock->RootCell == HCELL_NIL) return HCELL_NIL;

    Root = (PCM_KEY_NODE)HvGetCell(Hive, Hive->BaseBlock->RootCell);
    if (Root == NULL) return HCELL_NIL;

    Anchor = Root->Security;
    HvReleaseCell(Hive, Hive->BaseBlock->RootCell);
    return Anchor;
}

static
NTSTATUS
CmpOpenHiveListKey(
    _Out_ PHANDLE KeyHandle)
{
    OBJECT_ATTRIBUTES ObjectAttributes;

    InitializeObjectAttributes(&ObjectAttributes,
                               &CmpHiveListKeyName,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);

    return ZwOpenKey(KeyHandle, KEY_READ | KEY_WRITE, &ObjectAttributes);
}

static
NTSTATUS
CmpBuildHiveFileName(
    _In_ PCUNICODE_STRING BaseName,
    _In_opt_ PCUNICODE_STRING Extension,
    _Out_ PUNICODE_STRING Result)
{
    USHORT Size;

    Size = BaseName->Length;
    if (Extension != NULL) Size += Extension->Length;

    Result->Buffer = ExAllocatePoolZero(PagedPool,
                                        Size + sizeof(WCHAR),
                                        TAG_CM);
    if (Result->Buffer == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    Result->Length = 0;
    Result->MaximumLength = Size + sizeof(WCHAR);

    RtlAppendUnicodeStringToString(Result, BaseName);
    if (Extension != NULL) RtlAppendUnicodeStringToString(Result, Extension);

    return STATUS_SUCCESS;
}

static
NTSTATUS
CmpOpenOneHiveFile(
    _In_ PCUNICODE_STRING FileName,
    _In_ BOOLEAN CreateAllowed,
    _In_ BOOLEAN MarkAsSystemHive,
    _In_ BOOLEAN NoBuffering,
    _In_ BOOLEAN Shared,
    _Out_ PHANDLE Handle,
    _Out_ PULONG Disposition,
    _Out_opt_ PULONG ClusterSize)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    FILE_FS_SIZE_INFORMATION SizeInfo;
    IO_STATUS_BLOCK IoStatus;
    ULONG CreateOptions;
    NTSTATUS Status;
    ULONG Cluster;

    *Handle = NULL;
    *Disposition = 0;

    InitializeObjectAttributes(&ObjectAttributes,
                               (PUNICODE_STRING)FileName,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);

    CreateOptions = FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE;
    if (NoBuffering) CreateOptions |= FILE_NO_INTERMEDIATE_BUFFERING;

    Status = ZwCreateFile(Handle,
                          FILE_READ_DATA | FILE_WRITE_DATA | SYNCHRONIZE,
                          &ObjectAttributes,
                          &IoStatus,
                          NULL,
                          FILE_ATTRIBUTE_NORMAL | FILE_ATTRIBUTE_SYSTEM,
                          Shared ? FILE_SHARE_READ : 0,
                          CreateAllowed ? FILE_OPEN_IF : FILE_OPEN,
                          CreateOptions,
                          NULL,
                          0);
    if (!NT_SUCCESS(Status)) return Status;

    *Disposition = (ULONG)IoStatus.Information;

    if (MarkAsSystemHive)
    {
        ZwFsControlFile(*Handle,
                        NULL,
                        NULL,
                        NULL,
                        &IoStatus,
                        FSCTL_MARK_AS_SYSTEM_HIVE,
                        NULL,
                        0,
                        NULL,
                        0);
    }

    if (ClusterSize == NULL) return STATUS_SUCCESS;

    Status = ZwQueryVolumeInformationFile(*Handle,
                                          &IoStatus,
                                          &SizeInfo,
                                          sizeof(SizeInfo),
                                          FileFsSizeInformation);
    if (!NT_SUCCESS(Status))
    {
        *ClusterSize = 1;
        return STATUS_SUCCESS;
    }

    Cluster = SizeInfo.BytesPerSector / HSECTOR_SIZE;
    *ClusterSize = (Cluster < 1) ? 1 : Cluster;
    return STATUS_SUCCESS;
}

/* PUBLIC FUNCTIONS ***********************************************************/

VOID
NTAPI
CmpInitHiveViewList(
    _In_ PCMHIVE Hive)
{
    RtlZeroMemory(&Hive->Hive.ViewMap, sizeof(HVP_VIEW_MAP));
}

VOID
NTAPI
CmpDestroyHiveViewList(
    _In_ PCMHIVE Hive)
{
    /* TODO: a hive is never mapped as views, so there is no view list to undo */
    RtlZeroMemory(&Hive->Hive.ViewMap, sizeof(HVP_VIEW_MAP));
}

NTSTATUS
NTAPI
CmpOpenHiveFiles(
    _In_ PCUNICODE_STRING BaseName,
    _In_opt_ PCWSTR Extension,
    _Out_ PHANDLE Primary,
    _Out_ PHANDLE Log,
    _Out_ PULONG PrimaryDisposition,
    _Out_ PULONG LogDisposition,
    _In_ BOOLEAN CreateAllowed,
    _In_ BOOLEAN MarkAsSystemHive,
    _In_ BOOLEAN NoBuffering,
    _Out_opt_ PULONG ClusterSize)
{
    UNICODE_STRING ExtensionName;
    UNICODE_STRING FileName;
    NTSTATUS Status;

    *Primary = NULL;
    *Log = NULL;
    *PrimaryDisposition = 0;
    *LogDisposition = 0;

    if (Extension != NULL)
    {
        RtlInitUnicodeString(&ExtensionName, Extension);
        Status = CmpBuildHiveFileName(BaseName, &ExtensionName, &FileName);
    }
    else
    {
        Status = CmpBuildHiveFileName(BaseName, NULL, &FileName);
    }

    if (!NT_SUCCESS(Status)) return Status;

    Status = CmpOpenOneHiveFile(&FileName,
                                CreateAllowed,
                                MarkAsSystemHive,
                                NoBuffering,
                                FALSE,
                                Primary,
                                PrimaryDisposition,
                                ClusterSize);
    ExFreePool(FileName.Buffer);

    if (!NT_SUCCESS(Status)) return Status;

    Status = CmpBuildHiveFileName(BaseName, &CmpLogExtension, &FileName);
    if (!NT_SUCCESS(Status)) return STATUS_SUCCESS;

    CmpOpenOneHiveFile(&FileName,
                       CreateAllowed,
                       MarkAsSystemHive,
                       NoBuffering,
                       FALSE,
                       Log,
                       LogDisposition,
                       NULL);
    ExFreePool(FileName.Buffer);

    return STATUS_SUCCESS;
}

VOID
NTAPI
CmpCloseHiveFiles(
    _In_ PCMHIVE Hive)
{
    ULONG i;

    for (i = 0; i < RTL_NUMBER_OF(Hive->FileHandles); i++)
    {
        if (Hive->FileHandles[i] == NULL) continue;

        ZwClose(Hive->FileHandles[i]);
        Hive->FileHandles[i] = NULL;
    }
}

/*
 * The root of a hive is the one key that has no parent inside its own hive:
 * it is marked so that nothing can delete it and so that a lookup crossing
 * into the hive knows it has arrived.
 */
BOOLEAN
NTAPI
CmpCreateRootNode(
    _In_ PHHIVE Hive,
    _In_ PCWSTR Name,
    _Out_ PHCELL_INDEX Index)
{
    UNICODE_STRING KeyName;
    PCM_KEY_NODE Node;
    USHORT NameSize;

    RtlInitUnicodeString(&KeyName, Name);
    NameSize = CmpNameSize(Hive, &KeyName);

    *Index = HvAllocateCell(Hive,
                            FIELD_OFFSET(CM_KEY_NODE, Name) + NameSize,
                            Stable,
                            HCELL_NIL);
    if (*Index == HCELL_NIL) return FALSE;

    Node = (PCM_KEY_NODE)HvGetCell(Hive, *Index);
    if (Node == NULL) return FALSE;

    RtlZeroMemory(Node, FIELD_OFFSET(CM_KEY_NODE, Name));

    Node->Signature = CM_KEY_NODE_SIGNATURE;
    Node->Flags = KEY_HIVE_ENTRY | KEY_NO_DELETE;
    Node->Parent = HCELL_NIL;
    Node->Security = HCELL_NIL;
    Node->Class = HCELL_NIL;
    Node->SubKeyLists[Stable] = HCELL_NIL;
    Node->SubKeyLists[Volatile] = HCELL_NIL;
    Node->ValueList.List = HCELL_NIL;
    KeQuerySystemTime(&Node->LastWriteTime);

    Node->NameLength = CmpCopyName(Hive, Node->Name, &KeyName);
    if (Node->NameLength < KeyName.Length) Node->Flags |= KEY_COMP_NAME;

    HvMarkCellDirty(Hive, *Index, FALSE);
    HvReleaseCell(Hive, *Index);

    Hive->BaseBlock->RootCell = *Index;
    return TRUE;
}

/*
 * Takes the cluster the volume reports, so that a hive on a disk whose
 * sectors are larger than the hive's own writes in whole sectors.
 */
static
NTSTATUS
CmpInitializeHiveWithCluster(
    _Out_ PCMHIVE *CmHive,
    _In_ ULONG OperationType,
    _In_ ULONG HiveFlags,
    _In_ ULONG FileType,
    _In_opt_ PVOID HiveData,
    _In_ HANDLE Primary,
    _In_ HANDLE Log,
    _In_ HANDLE External,
    _In_ HANDLE Alternate,
    _In_opt_ PCUNICODE_STRING FileName,
    _In_ ULONG CheckFlags,
    _In_ ULONG Cluster)
{
    CM_CHECK_REGISTRY_STATUS CheckStatus;
    PCMHIVE Hive;
    NTSTATUS Status;

    *CmHive = NULL;

    if (OperationType == HINIT_FILE && Primary == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Hive = ExAllocatePoolZero(NonPagedPool, sizeof(CMHIVE), TAG_CMHIVE);
    if (Hive == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    InitializeListHead(&Hive->NotifyList);
    InitializeListHead(&Hive->HiveList);
    InitializeListHead(&Hive->PreloadedHiveList);
    InitializeListHead(&Hive->TrustClassEntry);

    ExInitializeRundownProtection(&Hive->HiveRundown);
    ExInitializePushLock(&Hive->HandleClosePendingEvent);
    ExInitializePushLock(&Hive->LateUnloadFinishedEvent);

    CmpInitSecurityCache(Hive);
    CmpInitHiveViewList(Hive);

    Hive->FileHandles[HFILE_TYPE_PRIMARY] = Primary;
    Hive->FileHandles[HFILE_TYPE_LOG] = Log;
    Hive->FileHandles[HFILE_TYPE_EXTERNAL] = External;
    Hive->FileHandles[HFILE_TYPE_ALTERNATE] = Alternate;

    Hive->ReferenceCount = 1;
    Hive->CreatorOwner = KeGetCurrentThread();
    Hive->UnloadHistoryIndex = 0;
    Hive->SecurityHitHint = -1;

    Status = HvInitialize(&Hive->Hive,
                          OperationType,
                          HiveFlags,
                          FileType,
                          HiveData,
                          CmpAllocate,
                          CmpFree,
                          CmpFileWrite,
                          CmpFileRead,
                          Cluster,
                          FileName);
    if (!NT_SUCCESS(Status))
    {
        Hive->CreatorOwner = NULL;
        CmpDestroySecurityCache(Hive);
        ExFreePool(Hive);
        return Status;
    }

    if (CheckFlags != 0)
    {
        CheckStatus = CmpSelfHealHive(Hive, CheckFlags);
        if (!CM_CHECK_REGISTRY_SUCCESS(CheckStatus))
        {
            Hive->CreatorOwner = NULL;
            HvFree(&Hive->Hive);
            CmpDestroySecurityCache(Hive);
            ExFreePool(Hive);
            return STATUS_REGISTRY_CORRUPT;
        }
    }

    Hive->CreatorOwner = NULL;

    if (Hive->Hive.BaseBlock != NULL)
    {
        CmpPopulateSecurityCache(Hive, CmpHiveSecurityAnchor(&Hive->Hive));
    }

    ExAcquirePushLockExclusive(&CmpHiveListHeadLock);
    InsertTailList(&CmpHiveListHead, &Hive->HiveList);
    ExReleasePushLockExclusive(&CmpHiveListHeadLock);

    *CmHive = Hive;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
CmpInitializeHive(
    _Out_ PCMHIVE *CmHive,
    _In_ ULONG OperationType,
    _In_ ULONG HiveFlags,
    _In_ ULONG FileType,
    _In_opt_ PVOID HiveData,
    _In_ HANDLE Primary,
    _In_ HANDLE Log,
    _In_ HANDLE External,
    _In_ HANDLE Alternate,
    _In_opt_ PCUNICODE_STRING FileName,
    _In_ ULONG CheckFlags)
{
    return CmpInitializeHiveWithCluster(CmHive,
                                        OperationType,
                                        HiveFlags,
                                        FileType,
                                        HiveData,
                                        Primary,
                                        Log,
                                        External,
                                        Alternate,
                                        FileName,
                                        CheckFlags,
                                        0);
}

NTSTATUS
NTAPI
CmpDestroyHive(
    _In_ PCMHIVE CmHive)
{
    ExAcquirePushLockExclusive(&CmpHiveListHeadLock);
    if (!IsListEmpty(&CmHive->HiveList)) RemoveEntryList(&CmHive->HiveList);
    InitializeListHead(&CmHive->HiveList);
    ExReleasePushLockExclusive(&CmpHiveListHeadLock);

    CmpRemoveFromSelfHealQueue(CmHive);
    CmpRemoveFromHiveFileList(CmHive);
    CmpDestroyHiveViewList(CmHive);
    CmpDestroySecurityCache(CmHive);

    HvFree(&CmHive->Hive);
    CmpCloseHiveFiles(CmHive);

    if (CmHive->FileFullPath.Buffer != NULL)
    {
        ExFreePool(CmHive->FileFullPath.Buffer);
    }

    if (CmHive->FileUserName.Buffer != NULL)
    {
        ExFreePool(CmHive->FileUserName.Buffer);
    }

    if (CmHive->HiveRootPath.Buffer != NULL)
    {
        ExFreePool(CmHive->HiveRootPath.Buffer);
    }

    if (CmHive->UnloadEventArray != NULL) ExFreePool(CmHive->UnloadEventArray);

    ExFreePool(CmHive);
    return STATUS_SUCCESS;
}

VOID
NTAPI
CmpReferenceHive(
    _In_ PCMHIVE Hive)
{
    InterlockedIncrement(&Hive->ReferenceCount);
}

VOID
NTAPI
CmpDereferenceHive(
    _In_ PCMHIVE Hive)
{
    if (InterlockedDecrement(&Hive->ReferenceCount) != 0) return;

    CmpDestroyHive(Hive);
}

NTSTATUS
NTAPI
CmpInitHiveFromFile(
    _In_ PCUNICODE_STRING HiveName,
    _In_ ULONG HiveFlags,
    _Out_ PCMHIVE *Hive,
    _Inout_ PBOOLEAN New,
    _In_ ULONG CheckFlags)
{
    ULONG PrimaryDisposition = 0;
    ULONG LogDisposition = 0;
    ULONG ClusterSize = 1;
    HANDLE Primary = NULL;
    HANDLE Log = NULL;
    HCELL_INDEX RootCell;
    ULONG Operation;
    PCMHIVE CmHive;
    NTSTATUS Status;

    *Hive = NULL;

    Status = CmpOpenHiveFiles(HiveName,
                              NULL,
                              &Primary,
                              &Log,
                              &PrimaryDisposition,
                              &LogDisposition,
                              TRUE,
                              FALSE,
                              TRUE,
                              &ClusterSize);
    if (!NT_SUCCESS(Status)) return Status;

    if (Primary == NULL) return STATUS_OBJECT_NAME_NOT_FOUND;

    *New = (PrimaryDisposition == FILE_CREATED);
    Operation = *New ? HINIT_CREATE : HINIT_FILE;

    Status = CmpInitializeHiveWithCluster(&CmHive,
                                          Operation,
                                          HiveFlags,
                                          HFILE_TYPE_PRIMARY,
                                          NULL,
                                          Primary,
                                          Log,
                                          NULL,
                                          NULL,
                                          HiveName,
                                          *New ? 0 : CheckFlags,
                                          ClusterSize);
    if (!NT_SUCCESS(Status))
    {
        if (Log != NULL) ZwClose(Log);
        ZwClose(Primary);
        return Status;
    }

    if (*New && !CmpCreateRootNode(&CmHive->Hive, L"", &RootCell))
    {
        CmpDestroyHive(CmHive);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    *Hive = CmHive;
    return STATUS_SUCCESS;
}

/*
 * Hangs a loaded hive under a name in the namespace by making a key in the
 * volatile master hive that stands for the loaded hive's own root.
 */
NTSTATUS
NTAPI
CmpLinkHiveToMaster(
    _In_ PUNICODE_STRING LinkName,
    _In_opt_ HANDLE RootDirectory,
    _In_ PCMHIVE CmHive,
    _In_ BOOLEAN Allocate,
    _In_opt_ PSECURITY_DESCRIPTOR SecurityDescriptor)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    CM_PARSE_CONTEXT ParseContext;
    PCM_KEY_BODY KeyBody;
    HANDLE KeyHandle;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(Allocate);

    RtlZeroMemory(&ParseContext, sizeof(ParseContext));

    ParseContext.CreateLink = TRUE;
    ParseContext.CreateOperation = TRUE;
    ParseContext.CreateOptions = REG_OPTION_VOLATILE;
    ParseContext.ChildHive.KeyHive = &CmHive->Hive;
    ParseContext.ChildHive.KeyCell = CmHive->Hive.BaseBlock->RootCell;
    ParseContext.OriginatingPoint = CmHive;

    InitializeObjectAttributes(&ObjectAttributes,
                               LinkName,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               RootDirectory,
                               SecurityDescriptor);

    Status = ObOpenObjectByName(&ObjectAttributes,
                                CmpKeyObjectType,
                                KernelMode,
                                NULL,
                                KEY_READ | KEY_WRITE,
                                &ParseContext,
                                &KeyHandle);
    if (!NT_SUCCESS(Status)) return Status;

    Status = ObReferenceObjectByHandle(KeyHandle,
                                       0,
                                       CmpKeyObjectType,
                                       KernelMode,
                                       (PVOID*)&KeyBody,
                                       NULL);
    if (NT_SUCCESS(Status))
    {
        if (CmpReferenceKeyControlBlock(KeyBody->KeyControlBlock))
        {
            CmHive->RootKcb = KeyBody->KeyControlBlock;
        }

        ObDereferenceObject(KeyBody);
    }

    ZwClose(KeyHandle);
    return Status;
}

/*
 * The hive list under the control set is how the rest of the system learns
 * which file a loaded hive came from.
 */
NTSTATUS
NTAPI
CmpAddToHiveFileList(
    _Inout_ PCMHIVE Hive)
{
    UNICODE_STRING ValueName;
    HANDLE KeyHandle;
    NTSTATUS Status;

    if (Hive->HiveRootPath.Length == 0) return STATUS_SUCCESS;

    Status = CmpOpenHiveListKey(&KeyHandle);
    if (!NT_SUCCESS(Status)) return Status;

    ValueName = Hive->HiveRootPath;

    Status = ZwSetValueKey(KeyHandle,
                           &ValueName,
                           0,
                           REG_SZ,
                           Hive->FileFullPath.Buffer,
                           Hive->FileFullPath.Length + sizeof(WCHAR));

    ZwClose(KeyHandle);
    return Status;
}

VOID
NTAPI
CmpRemoveFromHiveFileList(
    _Inout_ PCMHIVE Hive)
{
    UNICODE_STRING ValueName;
    HANDLE KeyHandle;
    NTSTATUS Status;

    if (Hive->HiveRootPath.Length == 0) return;

    Status = CmpOpenHiveListKey(&KeyHandle);
    if (!NT_SUCCESS(Status)) return;

    ValueName = Hive->HiveRootPath;
    ZwDeleteValueKey(KeyHandle, &ValueName);
    ZwClose(KeyHandle);
}

NTSTATUS
NTAPI
CmpFlushHiveToDisk(
    _In_ PCMHIVE Hive,
    _In_ BOOLEAN Force)
{
    BOOLEAN Written;

    if (CmpNoWrite) return STATUS_SUCCESS;
    if (Hive->Hive.HiveFlags & HIVE_VOLATILE) return STATUS_SUCCESS;
    if (Hive->Hive.HiveFlags & HIVE_NOLAZYFLUSH) return STATUS_SUCCESS;
    if (!Force && Hive->Hive.DirtyCount == 0) return STATUS_SUCCESS;

    CmpLockHiveFlusherExclusive(Hive);
    Written = HvSyncHive(&Hive->Hive);
    CmpUnlockHiveFlusher(Hive);

    if (!Written) return STATUS_REGISTRY_IO_FAILED;

    Hive->FinalFlushSucceeded = TRUE;
    return STATUS_SUCCESS;
}
