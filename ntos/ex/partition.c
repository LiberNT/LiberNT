/*
 * PROJECT:         LiberNT Operating System
 * LICENSE:         GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:         Executive memory-partition object services
 * COPYRIGHT:       Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>

#define NDEBUG
#include <debug.h>

/* PRIVATE DEFINITIONS *******************************************************/

#define EXP_MEMORY_PARTITION_SIGNATURE 'traP'

typedef struct _EXP_MEMORY_PARTITION
{
    ULONG Signature;
    PEXP_MANAGE_MEMORY_PARTITION ManagePartition;
    PVOID ProviderContext;
} EXP_MEMORY_PARTITION, *PEXP_MEMORY_PARTITION;

typedef struct _EXP_MEMORY_PARTITION_INFORMATION
{
    ULONG Flags;
    ULONG NumaNode;
    ULONG Channel;
    ULONG NumberOfNumaNodes;
    ULONG64 ResidentAvailablePages;
    ULONG64 CommittedPages;
    ULONG64 CommitLimit;
    ULONG64 PeakCommitment;
    ULONG64 TotalNumberOfPages;
    ULONG64 AvailablePages;
    ULONG64 ZeroPages;
    ULONG64 FreePages;
    ULONG64 StandbyPages;
    ULONG64 Reserved[16];
    ULONG64 MaximumCommitLimit;
    ULONG64 Reserved2;
    ULONG PartitionId;
} EXP_MEMORY_PARTITION_INFORMATION, *PEXP_MEMORY_PARTITION_INFORMATION;

C_ASSERT(sizeof(EXP_MEMORY_PARTITION_INFORMATION) == 0xF0);
C_ASSERT(FIELD_OFFSET(EXP_MEMORY_PARTITION_INFORMATION, TotalNumberOfPages) == 0x30);
C_ASSERT(FIELD_OFFSET(EXP_MEMORY_PARTITION_INFORMATION, MaximumCommitLimit) == 0xD8);
C_ASSERT(FIELD_OFFSET(EXP_MEMORY_PARTITION_INFORMATION, PartitionId) == 0xE8);

/* GLOBALS *******************************************************************/

POBJECT_TYPE PsPartitionType;

static GENERIC_MAPPING ExpMemoryPartitionMapping =
{
    STANDARD_RIGHTS_READ | MEMORY_PARTITION_QUERY_ACCESS,
    STANDARD_RIGHTS_WRITE | MEMORY_PARTITION_MODIFY_ACCESS,
    STANDARD_RIGHTS_EXECUTE | SYNCHRONIZE,
    MEMORY_PARTITION_ALL_ACCESS
};

/* INITIALIZATION ************************************************************/

CODE_SEG("INIT")
BOOLEAN
NTAPI
ExpInitializePartitionImplementation(VOID)
{
    OBJECT_TYPE_INITIALIZER ObjectTypeInitializer;
    UNICODE_STRING TypeName;
    NTSTATUS Status;

    RtlZeroMemory(&ObjectTypeInitializer, sizeof(ObjectTypeInitializer));
    RtlInitUnicodeString(&TypeName, L"Partition");
    ObjectTypeInitializer.Length = sizeof(ObjectTypeInitializer);
    ObjectTypeInitializer.DefaultNonPagedPoolCharge =
        sizeof(EXP_MEMORY_PARTITION);
    ObjectTypeInitializer.GenericMapping = ExpMemoryPartitionMapping;
    ObjectTypeInitializer.PoolType = NonPagedPoolNx;
    ObjectTypeInitializer.ValidAccessMask = MEMORY_PARTITION_ALL_ACCESS;
    ObjectTypeInitializer.InvalidAttributes = OBJ_OPENLINK;

    Status = ObCreateObjectType(&TypeName,
                                &ObjectTypeInitializer,
                                NULL,
                                &PsPartitionType);
    return NT_SUCCESS(Status);
}

/* INTERNAL FUNCTIONS ********************************************************/

NTSTATUS
NTAPI
ExpCreateMemoryPartition(
    _In_opt_ POBJECT_ATTRIBUTES ObjectAttributes,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ PEXP_MANAGE_MEMORY_PARTITION ManagePartition,
    _In_opt_ PVOID ProviderContext,
    _Out_ PHANDLE PartitionHandle)
{
    PEXP_MEMORY_PARTITION Partition;
    NTSTATUS Status;

    PAGED_CODE();

    if (ManagePartition == NULL)
        return STATUS_INVALID_PARAMETER_3;
    if (PartitionHandle == NULL)
        return STATUS_INVALID_PARAMETER_5;

    Status = ObCreateObject(KernelMode,
                            PsPartitionType,
                            ObjectAttributes,
                            KernelMode,
                            NULL,
                            sizeof(*Partition),
                            0,
                            0,
                            (PVOID *)&Partition);
    if (!NT_SUCCESS(Status))
        return Status;

    Partition->Signature = EXP_MEMORY_PARTITION_SIGNATURE;
    Partition->ManagePartition = ManagePartition;
    Partition->ProviderContext = ProviderContext;

    return ObInsertObject(Partition,
                          NULL,
                          DesiredAccess,
                          0,
                          NULL,
                          PartitionHandle);
}

static
NTSTATUS
ExpQuerySystemPartitionInformation(
    _Inout_updates_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length)
{
    PEXP_MEMORY_PARTITION_INFORMATION Information;
    MM_PAGE_LIST_COUNTS Counts;
    SIZE_T PeakCommitment;

    PAGED_CODE();

    if (Length != sizeof(EXP_MEMORY_PARTITION_INFORMATION))
        return STATUS_INFO_LENGTH_MISMATCH;
    if (Buffer == NULL)
        return STATUS_INVALID_PARAMETER;

    Information = Buffer;
    if (Information->NumaNode != MAXULONG || Information->Channel != MAXULONG)
    {
        /* TODO: counts of one NUMA node or one channel */
        return STATUS_NOT_SUPPORTED;
    }

    MmQueryPageListCounts(&Counts);

    Information->NumberOfNumaNodes = KeNumberNodes;
    Information->ResidentAvailablePages = MmResidentAvailablePages;
    Information->CommittedPages = MmQuerySystemCommitCharge(&PeakCommitment);
    Information->CommitLimit = MmTotalCommitLimit;
    Information->PeakCommitment = PeakCommitment;
    Information->TotalNumberOfPages = MmNumberOfPhysicalPages;
    Information->AvailablePages = MmAvailablePages;
    Information->ZeroPages = Counts.Zeroed;
    Information->FreePages = Counts.Free;
    Information->StandbyPages = Counts.Standby;
    RtlZeroMemory(Information->Reserved, sizeof(Information->Reserved));
    Information->MaximumCommitLimit = MmTotalCommitLimitMaximum;
    Information->Reserved2 = 0;
    Information->PartitionId = 0;

    ASSERT(Information->NumberOfNumaNodes != 0);
    ASSERT(Information->TotalNumberOfPages != 0);
    ASSERT(Information->PeakCommitment >= Information->CommittedPages);

    return STATUS_SUCCESS;
}

static
NTSTATUS
ExpManageSystemPartition(
    _In_ ULONG PartitionInformationClass,
    _Inout_updates_bytes_opt_(PartitionInformationLength) PVOID PartitionInformation,
    _In_ ULONG PartitionInformationLength)
{
    PAGED_CODE();

    if (PartitionInformationClass != SystemMemoryPartitionInformation)
    {
        /* TODO: dedicated memory classes of the system partition */
        return STATUS_INVALID_INFO_CLASS;
    }

    return ExpQuerySystemPartitionInformation(PartitionInformation,
                                              PartitionInformationLength);
}

/* PUBLIC FUNCTIONS **********************************************************/

NTSTATUS
NTAPI
ZwOpenPartition(
    _Out_ PHANDLE PartitionHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ POBJECT_ATTRIBUTES ObjectAttributes)
{
    PAGED_CODE();

    return ObOpenObjectByName(ObjectAttributes,
                              PsPartitionType,
                              KernelMode,
                              NULL,
                              DesiredAccess,
                              NULL,
                              PartitionHandle);
}

NTSTATUS
NTAPI
ZwManagePartition(
    _In_ HANDLE TargetHandle,
    _In_opt_ HANDLE SourceHandle,
    _In_ ULONG PartitionInformationClass,
    _Inout_updates_bytes_opt_(PartitionInformationLength) PVOID PartitionInformation,
    _In_ ULONG PartitionInformationLength)
{
    PEXP_MEMORY_PARTITION Partition;
    ACCESS_MASK DesiredAccess;
    NTSTATUS Status;

    PAGED_CODE();

    if (TargetHandle == (HANDLE)(LONG_PTR)MEMORY_CURRENT_PARTITION_HANDLE ||
        TargetHandle == (HANDLE)(LONG_PTR)MEMORY_SYSTEM_PARTITION_HANDLE)
    {
        return ExpManageSystemPartition(PartitionInformationClass,
                                        PartitionInformation,
                                        PartitionInformationLength);
    }

    DesiredAccess = PartitionInformationClass == 0 ?
                        MEMORY_PARTITION_QUERY_ACCESS :
                        MEMORY_PARTITION_MODIFY_ACCESS;

    Status = ObReferenceObjectByHandle(TargetHandle,
                                       DesiredAccess,
                                       PsPartitionType,
                                       KernelMode,
                                       (PVOID *)&Partition,
                                       NULL);
    if (!NT_SUCCESS(Status))
        return Status;

    ASSERT(Partition->Signature == EXP_MEMORY_PARTITION_SIGNATURE);
    Status = Partition->ManagePartition(Partition->ProviderContext,
                                        SourceHandle,
                                        PartitionInformationClass,
                                        PartitionInformation,
                                        PartitionInformationLength);
    ObDereferenceObject(Partition);
    return Status;
}
