/*
 * PROJECT:         LiberNT kernel-mode tests
 * LICENSE:         GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:         Modern executive services used by WDDM class drivers
 * COPYRIGHT:       Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <reactos/drivers/acpi/acpi.h>

#define NDEBUG
#include <debug.h>

KMT_TESTFUNC Test_PoFxState;
KMT_TESTFUNC Test_MmWddmRotate;
KMT_TESTFUNC Test_MmWddmRotateValid;

typedef struct _TEST_TTM_DEVICE_INFORMATION
{
    PVOID Value[4];
} TEST_TTM_DEVICE_INFORMATION;

typedef struct _TEST_TTM_ARRIVAL_INFORMATION
{
    ULONGLONG Reserved;
    PUNICODE_STRING InstanceName;
} TEST_TTM_ARRIVAL_INFORMATION;

typedef struct _TEST_API_SET_NAMESPACE
{
    ULONG Version;
    ULONG Size;
    ULONG Flags;
    ULONG Count;
    ULONG EntryOffset;
    ULONG HashOffset;
    ULONG HashFactor;
} TEST_API_SET_NAMESPACE, *PTEST_API_SET_NAMESPACE;

typedef struct _TEST_API_SET_NAMESPACE_ENTRY
{
    ULONG Flags;
    ULONG NameOffset;
    ULONG NameLength;
    ULONG HashedLength;
    ULONG ValueOffset;
    ULONG ValueCount;
} TEST_API_SET_NAMESPACE_ENTRY, *PTEST_API_SET_NAMESPACE_ENTRY;

typedef struct _TEST_API_SET_VALUE_ENTRY
{
    ULONG Flags;
    ULONG NameOffset;
    ULONG NameLength;
    ULONG ValueOffset;
    ULONG ValueLength;
} TEST_API_SET_VALUE_ENTRY, *PTEST_API_SET_VALUE_ENTRY;

typedef struct _TEST_PROCESS_ENERGY_VALUES
{
    ULONGLONG Cycles[4][2];
    ULONGLONG DiskEnergy;
    ULONGLONG NetworkTailEnergy;
    ULONGLONG MbbTailEnergy;
    ULONGLONG NetworkTxRxBytes;
    ULONGLONG MbbTxRxBytes;
    UCHAR Remaining[432 - 104];
} TEST_PROCESS_ENERGY_VALUES, *PTEST_PROCESS_ENERGY_VALUES;

typedef struct _TEST_RTL_AVL_TREE
{
    PRTL_BALANCED_NODE Root;
} TEST_RTL_AVL_TREE, *PTEST_RTL_AVL_TREE;

typedef struct _TEST_RTL_AVL_ENTRY
{
    RTL_BALANCED_NODE Node;
    LONG Key;
} TEST_RTL_AVL_ENTRY, *PTEST_RTL_AVL_ENTRY;

typedef struct _TEST_RTL_MULTI_TIME_PRECISE
{
    ULONGLONG PerformanceCounter;
    ULONGLONG HostPerformanceCounter;
    ULONGLONG SystemTime;
} TEST_RTL_MULTI_TIME_PRECISE, *PTEST_RTL_MULTI_TIME_PRECISE;


NTSTATUS
NTAPI
DbgkLkmdRegisterCallback(
    _In_ PVOID Callback,
    _In_opt_ PVOID Context,
    _In_ ULONG Flags);

NTSTATUS
NTAPI
DbgkLkmdUnregisterCallback(
    _In_ PVOID Callback);

NTSTATUS
NTAPI
DifRegisterClassDriverPlugin(
    _In_ ULONG Version,
    _In_ PVOID Plugin,
    _In_ ULONG Flags,
    _In_opt_ PDRIVER_OBJECT DriverObject);

NTSTATUS
NTAPI
ZwManagePartition(
    _In_ HANDLE TargetHandle,
    _In_opt_ HANDLE SourceHandle,
    _In_ ULONG PartitionInformationClass,
    _Inout_updates_bytes_opt_(PartitionInformationLength) PVOID PartitionInformation,
    _In_ ULONG PartitionInformationLength);

NTSTATUS
NTAPI
ZwOpenPartition(
    _Out_ PHANDLE PartitionHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ POBJECT_ATTRIBUTES ObjectAttributes);

extern NTKERNELAPI POBJECT_TYPE PsPartitionType;

NTSTATUS
NTAPI
InbvSetVirtualFrameBuffer(
    _In_opt_ PVOID VirtualFrameBuffer);

VOID
NTAPI
PoLatencySensitivityHint(
    _In_ ULONG HintType);

VOID
NTAPI
PoNotifyVSyncChange(
    _In_ BOOLEAN Enable);

VOID
NTAPI
PoSetUserPresent(
    _In_ ULONG RequestReason);

VOID
NTAPI
PsSetProcessFaultInformation(
    _Inout_ PEPROCESS Process,
    _In_ PVOID FaultInformation);

NTSTATUS
NTAPI
PsSetProcessesWindowState(
    _In_ ULONG WindowState,
    _In_opt_ PVOID Context);

NTSTATUS
NTAPI
RtlIsApiSetImplemented(
    _In_ PCSTR ApiSetName);

NTSTATUS
NTAPI
ExShareAddressSpaceWithDevice(
    _In_ PDEVICE_OBJECT PhysicalDeviceObject,
    _Out_ PULONG ReturnedAsid);

NTSTATUS
NTAPI
PsTlsAlloc(
    _In_opt_ PVOID Callback,
    _In_ ULONG Flags,
    _Out_ PULONG TlsIndex);

VOID
NTAPI
PsTlsFree(
    _In_ ULONG TlsIndex);

NTSTATUS
NTAPI
PsTlsGetValue(
    _In_ ULONG TlsIndex,
    _Out_ PVOID *Value);

NTSTATUS
NTAPI
PsTlsSetValue(
    _In_ ULONG TlsIndex,
    _In_opt_ PVOID Value);

VOID
NTAPI
RtlAvlInsertNodeEx(
    _Inout_ PTEST_RTL_AVL_TREE Tree,
    _In_opt_ PRTL_BALANCED_NODE Parent,
    _In_ BOOLEAN Right,
    _Inout_ PRTL_BALANCED_NODE Node);

VOID
NTAPI
RtlAvlRemoveNode(
    _Inout_ PTEST_RTL_AVL_TREE Tree,
    _Inout_ PRTL_BALANCED_NODE Node);

PWCHAR
NTAPI
RtlFindUnicodeSubstring(
    _In_ PCUNICODE_STRING String,
    _In_ PCUNICODE_STRING SubString,
    _In_ BOOLEAN CaseInsensitive);

NTSTATUS
NTAPI
RtlGetMultiTimePrecise(
    _Out_ PTEST_RTL_MULTI_TIME_PRECISE TimeValues,
    _In_ ULONG RequestedValues,
    _Out_ PULONG ReturnedValues);

NTSTATUS
NTAPI
RtlConvertHostPerfCounterToPerfCounter(
    _In_ ULONGLONG HostPerformanceCounter,
    _In_ ULONGLONG MaximumError,
    _Out_ PULONGLONG PerformanceCounter);

NTSTATUS
NTAPI
RtlGenerateClass5Guid(
    _In_ REFGUID NamespaceGuid,
    _In_reads_bytes_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ GUID *Guid);

NTSTATUS
NTAPI
RtlGetSystemGlobalData(
    _In_ ULONG DataId,
    _Out_writes_bytes_(Size) PVOID Buffer,
    _In_ ULONG Size);

NTSTATUS
NTAPI
RtlGetThreadLangIdByIndex(
    _In_ ULONG Flags,
    _In_ ULONG Index,
    _Out_ PULONG LangId,
    _Out_opt_ PULONG LangIdCount);

BOOLEAN
NTAPI
RtlIsStateSeparationEnabled(VOID);

NTSTATUS
NTAPI
RtlQueryElevationFlags(
    _Out_ PULONG Flags);

NTSTATUS
NTAPI
RtlGetAcesBufferSize(
    _In_ PACL Acl,
    _Out_ PULONG AcesBufferSize);

NTSTATUS
NTAPI
RtlQueryPackageIdentity(
    _In_opt_ PVOID TokenObject,
    _Out_writes_bytes_to_opt_(*PackageSize, *PackageSize) PWSTR PackageFullName,
    _Inout_ PSIZE_T PackageSize,
    _Out_writes_bytes_to_opt_(*AppIdSize, *AppIdSize) PWSTR AppId,
    _Inout_opt_ PSIZE_T AppIdSize,
    _Out_opt_ PBOOLEAN Packaged);

NTSTATUS
NTAPI
SeConvertStringSecurityDescriptorToSecurityDescriptor(
    _In_ PCWSTR StringSecurityDescriptor,
    _In_ ULONG StringSecurityDescriptorRevision,
    _Outptr_ PSECURITY_DESCRIPTOR *SecurityDescriptor,
    _Out_opt_ PULONG SecurityDescriptorSize);

USHORT
NTAPI
PsGetProcessMachine(
    _In_ PEPROCESS Process);

USHORT
NTAPI
PsWow64GetProcessMachine(
    _In_ PEPROCESS Process);

PVOID
NTAPI
PsQueryCurrentApiSetSchema(VOID);

PPHYSICAL_MEMORY_RANGE
NTAPI
MmGetPhysicalMemoryRangesEx(
    _In_opt_ PVOID PartitionObject);

BOOLEAN
NTAPI
ExIsManufacturingModeEnabled(VOID);

BOOLEAN
NTAPI
ExIsSoftBoot(VOID);

BOOLEAN
NTAPI
ExQueryFastCacheDevLicense(VOID);

ULONGLONG
NTAPI
HvlGetHypervisorVendorId(VOID);

#ifndef MM_CURRENT_PROCESS_PARTITION_OBJECT
#define MM_CURRENT_PROCESS_PARTITION_OBJECT ((PVOID)MAXULONG_PTR)
#define MM_ALL_PARTITIONS_OBJECT ((PVOID)(MAXULONG_PTR - 1))
#endif

ULONG
NTAPI
EtwpDisableStackWalkApc(VOID);

VOID
NTAPI
EtwpReenableStackWalkApc(
    _In_ ULONG PreviousState);

NTSTATUS
NTAPI
TtmNotifyDeviceArrival(
    _In_ ULONG DeviceType,
    _In_opt_ PVOID DeviceId,
    _In_opt_ PVOID DeviceInformation,
    _In_ ULONG Flags,
    _In_opt_ PVOID ArrivalInformation);

VOID
NTAPI
TtmNotifyDeviceDeparture(
    _In_ ULONG DeviceType,
    _In_opt_ PVOID DeviceId);

VOID
NTAPI
TtmNotifyDeviceInput(
    _In_ ULONG DeviceType,
    _In_opt_ PVOID DeviceId,
    _In_ ULONG Flags);

#ifdef KMT_KSR_APISET
NTSTATUS
NTAPI
KsrClaimPersistedMemory(
    _In_ const GUID *MemoryId,
    _In_ ULONGLONG MemoryLength,
    _Out_writes_opt_(MemoryRunCount) PULONGLONG MemoryRuns,
    _In_ ULONG MemoryRunCount,
    _In_ BOOLEAN MapMemory,
    _Out_ PULONG ClaimedRunCount);

NTSTATUS
NTAPI
KsrQueryMetadata(
    _In_ const GUID *MemoryId,
    _In_ ULONGLONG MemoryLength,
    _Out_writes_bytes_opt_(MetadataLength) PVOID Metadata,
    _In_ ULONG MetadataLength,
    _Out_ PULONG RequiredLength);

VOID
NTAPI
KsrFreePersistedMemory(
    _In_ const GUID *MemoryId,
    _In_ BOOLEAN ReleaseMemory);

NTSTATUS
NTAPI
KsrPersistMemoryWithMetadata(
    _In_ const GUID *MemoryId,
    _In_reads_(MemoryRunCount) PULONGLONG MemoryRuns,
    _In_ ULONG MemoryRunCount,
    _In_reads_bytes_opt_(MetadataLength) PVOID Metadata,
    _In_ ULONG MetadataLength,
    _Out_ PULONGLONG PersistedMemoryLength);

NTSTATUS
NTAPI
KsrGetFirmwareInformation(
    _Out_ PVOID *FirmwareInformation);

NTSTATUS
NTAPI
KsrEnumeratePersistedMemory(
    _In_ const GUID *MemoryType,
    _In_ NTSTATUS (NTAPI *Callback)(const GUID *, ULONGLONG, PVOID),
    _In_opt_ PVOID Context);

NTSTATUS
NTAPI
KsrMdlToMemoryRuns(
    _In_ PMDL Mdl,
    _Out_writes_(MemoryRunCapacity) PULONGLONG MemoryRuns,
    _In_ ULONG MemoryRunCapacity,
    _Out_ PULONG MemoryRunCount);
#endif

#define DEFINE_LKMD_CALLBACK(Number) \
    static VOID NTAPI LkmdCallback##Number(VOID) { }

DEFINE_LKMD_CALLBACK(0)
DEFINE_LKMD_CALLBACK(1)
DEFINE_LKMD_CALLBACK(2)
DEFINE_LKMD_CALLBACK(3)
DEFINE_LKMD_CALLBACK(4)
DEFINE_LKMD_CALLBACK(5)
DEFINE_LKMD_CALLBACK(6)
DEFINE_LKMD_CALLBACK(7)

static PVOID LkmdCallbacks[] =
{
    LkmdCallback0,
    LkmdCallback1,
    LkmdCallback2,
    LkmdCallback3,
    LkmdCallback4,
    LkmdCallback5,
    LkmdCallback6,
    LkmdCallback7
};

static
VOID
TestFirstEntrySList(VOID)
{
    DECLSPEC_ALIGN(MEMORY_ALLOCATION_ALIGNMENT) SLIST_HEADER ListHead;
    DECLSPEC_ALIGN(MEMORY_ALLOCATION_ALIGNMENT) SLIST_ENTRY Entry;
    PSLIST_ENTRY PreviousEntry;

    InitializeSListHead(&ListHead);
    ok_eq_pointer(FirstEntrySList(&ListHead), NULL);
    PreviousEntry = ExInterlockedPushEntrySList(&ListHead, &Entry, NULL);
    ok_eq_pointer(PreviousEntry, NULL);
    ok_eq_pointer(FirstEntrySList(&ListHead), &Entry);
    PreviousEntry = ExInterlockedPopEntrySList(&ListHead, NULL);
    ok_eq_pointer(PreviousEntry, &Entry);
    ok_eq_pointer(FirstEntrySList(&ListHead), NULL);
}

static
VOID
TestExecutiveSpinLocks(VOID)
{
    EX_SPIN_LOCK SpinLock = 0;
    KIRQL OldIrql;

    ok_eq_uint(KeGetCurrentIrql(), PASSIVE_LEVEL);
    OldIrql = ExAcquireSpinLockShared(&SpinLock);
    ok_eq_uint(OldIrql, PASSIVE_LEVEL);
    ok_eq_uint(KeGetCurrentIrql(), DISPATCH_LEVEL);
    ExAcquireSpinLockSharedAtDpcLevel(&SpinLock);
    ExReleaseSpinLockSharedFromDpcLevel(&SpinLock);
    ExReleaseSpinLockShared(&SpinLock, OldIrql);
    ok_eq_uint(KeGetCurrentIrql(), PASSIVE_LEVEL);

    OldIrql = ExAcquireSpinLockExclusive(&SpinLock);
    ok_eq_uint(OldIrql, PASSIVE_LEVEL);
    ok_eq_uint(KeGetCurrentIrql(), DISPATCH_LEVEL);
    ExReleaseSpinLockExclusive(&SpinLock, OldIrql);
    ok_eq_uint(KeGetCurrentIrql(), PASSIVE_LEVEL);

    KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);
    ok_bool_true(ExTryAcquireSpinLockSharedAtDpcLevel(&SpinLock),
                 "try-acquire shared on free lock");
    ok_bool_true(ExTryConvertSharedSpinLockExclusive(&SpinLock),
                 "convert sole shared owner to exclusive");
    ok_bool_false(ExTryAcquireSpinLockSharedAtDpcLevel(&SpinLock),
                  "shared try-acquire while exclusive");
    ok_bool_false(ExTryAcquireSpinLockExclusiveAtDpcLevel(&SpinLock),
                  "exclusive try-acquire while exclusive");
    ExReleaseSpinLockExclusiveFromDpcLevel(&SpinLock);

    ok_bool_true(ExTryAcquireSpinLockExclusiveAtDpcLevel(&SpinLock),
                 "try-acquire exclusive on free lock");
    ExReleaseSpinLockExclusiveFromDpcLevel(&SpinLock);

    ExAcquireSpinLockExclusiveAtDpcLevel(&SpinLock);
    ExReleaseSpinLockExclusiveFromDpcLevel(&SpinLock);
    KeLowerIrql(OldIrql);
}

static
VOID
TestLkmdCallbacks(VOID)
{
    NTSTATUS Status;
    ULONG Index;
    ULONG Registered = 0;
    BOOLEAN NullRegistered = FALSE;

    Status = DbgkLkmdRegisterCallback(NULL, NULL, 0);
    ok_eq_hex(Status, STATUS_SUCCESS);
    NullRegistered = NT_SUCCESS(Status);
    Status = DbgkLkmdRegisterCallback(LkmdCallbacks[0], NULL, 3);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);

    for (Index = 0; Index < RTL_NUMBER_OF(LkmdCallbacks) - 1; Index++)
    {
        Status = DbgkLkmdRegisterCallback(LkmdCallbacks[Index],
                                          (PVOID)(ULONG_PTR)Index,
                                          Index & 1);
        ok(Status == STATUS_SUCCESS || Status == STATUS_IMPLEMENTATION_LIMIT,
           "unexpected register status 0x%08lx\n", Status);
        if (!NT_SUCCESS(Status))
            break;
        Registered++;
    }

    if (Registered != 0)
    {
        Status = DbgkLkmdRegisterCallback(LkmdCallbacks[0], NULL, 0);
        ok_eq_hex(Status, STATUS_ALREADY_REGISTERED);
    }

    if (NullRegistered && Registered == RTL_NUMBER_OF(LkmdCallbacks) - 1)
    {
        Status = DbgkLkmdRegisterCallback((PVOID)TestLkmdCallbacks, NULL, 0);
        ok_eq_hex(Status, STATUS_IMPLEMENTATION_LIMIT);
    }

    while (Registered != 0)
    {
        Registered--;
        Status = DbgkLkmdUnregisterCallback(LkmdCallbacks[Registered]);
        ok_eq_hex(Status, STATUS_SUCCESS);
    }

    Status = DbgkLkmdUnregisterCallback(LkmdCallbacks[0]);
    ok_eq_hex(Status, STATUS_NOT_FOUND);
    if (NullRegistered)
    {
        Status = DbgkLkmdUnregisterCallback(NULL);
        ok_eq_hex(Status, STATUS_SUCCESS);
    }
    Status = DbgkLkmdUnregisterCallback(NULL);
    ok_eq_hex(Status, STATUS_NOT_FOUND);
}

static
VOID
TestStackWalkApcState(VOID)
{
    ULONG InitialState;
    ULONG DisabledState;
    ULONG RestoredState;

    InitialState = EtwpDisableStackWalkApc();
    DisabledState = EtwpDisableStackWalkApc();
    ok_eq_hex(DisabledState, 0x1FF);

    EtwpReenableStackWalkApc(DisabledState);
    RestoredState = EtwpDisableStackWalkApc();
    ok_eq_hex(RestoredState, 0x1FF);
    EtwpReenableStackWalkApc(RestoredState);
    EtwpReenableStackWalkApc(InitialState);
}

static
VOID
TestTtmLifecycle(VOID)
{
    TEST_TTM_DEVICE_INFORMATION Information = {0};
    UNICODE_STRING InstanceName = RTL_CONSTANT_STRING(L"kmtest-ttm-device");
    TEST_TTM_ARRIVAL_INFORMATION ArrivalInformation = {0, &InstanceName};
    PVOID DeviceId = &Information;
    NTSTATUS Status;

    Status = TtmNotifyDeviceArrival(7, DeviceId, NULL, 0, NULL);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);

    Information.Value[0] = KmtDriverObject;
    Status = TtmNotifyDeviceArrival(7,
                                    DeviceId,
                                    &Information,
                                    0x1234,
                                    &ArrivalInformation);
    if (skip(Status != STATUS_NOT_SUPPORTED,
             "TTM provider is unavailable; skipping lifecycle checks\n"))
    {
        return;
    }
    ok_eq_hex(Status, STATUS_SUCCESS);

    Status = TtmNotifyDeviceArrival(7,
                                    DeviceId,
                                    &Information,
                                    0x1234,
                                    &ArrivalInformation);
    ok_eq_hex(Status, STATUS_DEVICE_ALREADY_ATTACHED);

    TtmNotifyDeviceInput(7, DeviceId, 0x5678);
    TtmNotifyDeviceDeparture(7, DeviceId);

    Status = TtmNotifyDeviceArrival(7,
                                    DeviceId,
                                    &Information,
                                    0x9ABC,
                                    &ArrivalInformation);
    ok_eq_hex(Status, STATUS_SUCCESS);
    TtmNotifyDeviceDeparture(7, DeviceId);
}

static
VOID
TestDeviceAddressSpace(VOID)
{
#ifdef _M_ARM64
    PDEVICE_OBJECT DeviceObject;
    ULONG FirstAsid = 0;
    ULONG SecondAsid = 0;
    NTSTATUS Status;

    if (skip(*(volatile ULONG *)(KI_USER_SHARED_DATA + PAGE_SIZE - sizeof(ULONG)) == 0x8eac705,
             "native address-space sharing requires a real display PDO\n"))
    {
        return;
    }

    DeviceObject = KmtDriverObject->DeviceObject;
    if (skip(DeviceObject != NULL,
             "kmtest driver has no device object for address-space sharing\n"))
    {
        return;
    }

    Status = ExShareAddressSpaceWithDevice(DeviceObject, &FirstAsid);
    if (skip(Status != STATUS_NOT_SUPPORTED,
             "device address-space sharing provider is unavailable\n"))
    {
        return;
    }
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok(FirstAsid != 0, "first ASID was zero\n");

    Status = ExShareAddressSpaceWithDevice(DeviceObject, &SecondAsid);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(SecondAsid, FirstAsid);
#else
    skip(TRUE, "ExShareAddressSpaceWithDevice is an ARM64 graphics dependency\n");
#endif
}

static
VOID
TestUnavailableFrameworks(VOID)
{
    DMA_IOMMU_INTERFACE LegacyInterface;
    DMA_IOMMU_INTERFACE_EX Interface;
    NTSTATUS Status;
    ULONG Index;
    ULONG Version;
    PIOMMU_DMA_DOMAIN Domain;
    IOMMU_DMA_DOMAIN_CREATION_FLAGS DomainFlags = {0};
    PVOID *Routines;

    RtlFillMemory(&LegacyInterface, sizeof(LegacyInterface), 0xA5);
    Status = IoGetIommuInterface(1, &LegacyInterface);
    if (Status == STATUS_NOT_SUPPORTED)
    {
        skip(FALSE, "legacy IOMMU interface provider is unavailable\n");
    }
    else
    {
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (NT_SUCCESS(Status))
        {
            ok_eq_ulong(LegacyInterface.Version, 1);
            Routines = (PVOID *)&LegacyInterface.CreateDomain;
            for (Index = 0; Index != 13; Index++)
                ok(Routines[Index] != NULL,
                   "legacy IOMMU routine[%lu] is NULL\n", Index);
        }

        RtlFillMemory(&LegacyInterface, sizeof(LegacyInterface), 0xA5);
        Status = IoGetIommuInterface(0, &LegacyInterface);
        ok_eq_hex(Status, STATUS_INVALID_PARAMETER_1);
        ok_eq_ulong(LegacyInterface.Version, 0xA5A5A5A5);
    }

    RtlFillMemory(&Interface, sizeof(Interface), 0xA5);
    Status = IoGetIommuInterfaceEx(1, 0, &Interface);
    trace("IoGetIommuInterfaceEx returned 0x%08lx\n", Status);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        ok_eq_size(Interface.Size, 0x78);
        ok_eq_ulong(Interface.Version, 1);
        Routines = (PVOID *)&Interface.V1;
        for (Index = 0; Index != 13; Index++)
            ok(Routines[Index] != NULL,
               "IOMMU routine[%lu] is NULL\n", Index);

        Domain = (PIOMMU_DMA_DOMAIN)(ULONG_PTR)0xA5A5A5A5;
        Status = Interface.V1.CreateDomain(TRUE, &Domain);
        trace("IOMMU CreateDomain(TRUE) returned 0x%08lx, domain %p\n",
              Status,
              Domain);
        ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
        ok_eq_pointer(Domain, (PIOMMU_DMA_DOMAIN)(ULONG_PTR)0xA5A5A5A5);
        if (NT_SUCCESS(Status))
        {
            Status = Interface.V1.DeleteDomain(Domain);
            ok_eq_hex(Status, STATUS_SUCCESS);
        }

        Domain = (PIOMMU_DMA_DOMAIN)(ULONG_PTR)0xA5A5A5A5;
        Status = Interface.V1.CreateDomain(FALSE, &Domain);
        trace("IOMMU CreateDomain(FALSE) returned 0x%08lx, domain %p\n",
              Status,
              Domain);
        ok_eq_hex(Status, STATUS_NOT_SUPPORTED);
        ok_eq_pointer(Domain, (PIOMMU_DMA_DOMAIN)(ULONG_PTR)0xA5A5A5A5);
        if (NT_SUCCESS(Status))
        {
            Status = Interface.V1.DeleteDomain(Domain);
            ok_eq_hex(Status, STATUS_SUCCESS);
        }
    }

    for (Version = 0; Version <= 4; Version++)
    {
        if (Version == 1)
            continue;

        RtlFillMemory(&Interface, sizeof(Interface), 0xA5);
        Status = IoGetIommuInterfaceEx(Version, 0, &Interface);
        trace("IoGetIommuInterfaceEx(%lu, 0) returned 0x%08lx, size 0x%Ix, version %lu\n",
              Version,
              Status,
              Interface.Size,
              Interface.Version);
        if ((Version == 0) || (Version == 4))
        {
            ok_eq_hex(Status, STATUS_INVALID_PARAMETER_1);
            ok_eq_size(Interface.Size, (SIZE_T)0xA5A5A5A5A5A5A5A5ULL);
        }
        else
        {
            ok_eq_hex(Status, STATUS_SUCCESS);
            ok_eq_size(Interface.Size, Version == 2 ? 0xC0 : 0xE8);
            ok_eq_ulong(Interface.Version, Version);

            Routines = Version == 2 ? (PVOID *)&Interface.V2 :
                                      (PVOID *)&Interface.V3;
            for (Index = 0; Index != (Version == 2 ? 22 : 27); Index++)
                ok(Routines[Index] != NULL,
                   "IOMMU V%lu routine[%lu] is NULL\n", Version, Index);

            Domain = (PIOMMU_DMA_DOMAIN)(ULONG_PTR)0xA5A5A5A5;
            if (Version == 2)
            {
                Status = Interface.V2.CreateDomainEx(DomainTypeTranslate,
                                                     DomainFlags,
                                                     NULL,
                                                     NULL,
                                                     &Domain);
            }
            else
            {
                Status = Interface.V3.CreateDomainEx(DomainTypeTranslate,
                                                     DomainFlags,
                                                     NULL,
                                                     NULL,
                                                     &Domain);
            }
            trace("IOMMU V%lu CreateDomainEx returned 0x%08lx, domain %p\n",
                  Version,
                  Status,
                  Domain);
            ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
            ok_eq_pointer(Domain,
                          (PIOMMU_DMA_DOMAIN)(ULONG_PTR)0xA5A5A5A5);

            Domain = (PIOMMU_DMA_DOMAIN)(ULONG_PTR)0xA5A5A5A5;
            if (Version == 2)
            {
                Status = Interface.V2.CreateDomainEx(DomainTypeUnmanaged,
                                                     DomainFlags,
                                                     NULL,
                                                     NULL,
                                                     &Domain);
            }
            else
            {
                Status = Interface.V3.CreateDomainEx(DomainTypeUnmanaged,
                                                     DomainFlags,
                                                     NULL,
                                                     NULL,
                                                     &Domain);
            }
            trace("IOMMU V%lu CreateDomainEx(unmanaged) returned 0x%08lx, domain %p\n",
                  Version,
                  Status,
                  Domain);
            ok_eq_hex(Status, STATUS_NOT_SUPPORTED);
            ok_eq_pointer(Domain,
                          (PIOMMU_DMA_DOMAIN)(ULONG_PTR)0xA5A5A5A5);
        }
    }

    RtlFillMemory(&Interface, sizeof(Interface), 0xA5);
    Status = IoGetIommuInterfaceEx(1, 1, &Interface);
    trace("IoGetIommuInterfaceEx(1, 1) returned 0x%08lx, size 0x%Ix, version %lu\n",
          Status,
          Interface.Size,
          Interface.Version);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER_3);
    ok_eq_size(Interface.Size, (SIZE_T)0xA5A5A5A5A5A5A5A5ULL);

    Status = DifRegisterClassDriverPlugin(0,
                                          (PVOID)TestUnavailableFrameworks,
                                          0,
                                          KmtDriverObject);
    ok_eq_hex(Status, STATUS_DIF_DRIVER_PLUGIN_MISMATCH);
}

static
VOID
TestKernelApiSets(VOID)
{
    static const PCSTR HostedContracts[] =
    {
        "ext-ms-win-core-win32k-dxgk-internal-l1-1-0",
        "ext-ms-win-core-win32k-dxgk-l1-1-0",
        "ext-ms-win-core-win32k-flipmgr-l1-1-1",
        "ext-ms-win-core-win32k-surfmgr-l1-1-1",
        "ext-ms-win-core-win32k-tokenmgr-l1-1-0",
        "ext-ms-win-ntos-werkernel-l1-1-1"
    };
    ULONG Index;
    NTSTATUS Status;

    for (Index = 0; Index < RTL_NUMBER_OF(HostedContracts); Index++)
    {
        Status = RtlIsApiSetImplemented(HostedContracts[Index]);
        ok(Status == STATUS_SUCCESS,
           "contract %s returned 0x%08lx\n",
           HostedContracts[Index],
           Status);
    }

    Status = RtlIsApiSetImplemented("ext-ms-win-ntos-ksr-l1-1-5");
    ok_eq_hex(Status, STATUS_NOT_FOUND);
    Status = RtlIsApiSetImplemented("ext-ms-win-core-win32k-dxgk-l1-1-0.dll");
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
}

#ifdef KMT_KSR_APISET
static const GUID TestKsrMemoryId =
    {0x6f0b02fe, 0xb60a, 0x46d0, {0xb8, 0x7f, 0x0d, 0xa7, 0x7f, 0x41, 0xf1, 0x64}};

static
NTSTATUS
NTAPI
TestKsrEnumerateCallback(
    _In_ const GUID *MemoryId,
    _In_ ULONGLONG MemoryLength,
    _In_opt_ PVOID Context)
{
    PULONG CallbackCount = Context;

    UNREFERENCED_PARAMETER(MemoryId);
    UNREFERENCED_PARAMETER(MemoryLength);
    (*CallbackCount)++;
    return STATUS_SUCCESS;
}

static
VOID
TestKsrClaimPersistedMemory(VOID)
{
    ULONGLONG MemoryRun;
    ULONG ClaimedRunCount;
    NTSTATUS Status;

    MemoryRun = 0xA5A5A5A5A5A5A5A5ULL;
    ClaimedRunCount = 0xA5A5A5A5;
    Status = KsrClaimPersistedMemory(&TestKsrMemoryId,
                                     PAGE_SIZE,
                                     &MemoryRun,
                                     1,
                                     FALSE,
                                     &ClaimedRunCount);
    ok_eq_hex(Status, STATUS_NOT_SUPPORTED);
    ok_eq_ulonglong(MemoryRun, 0xA5A5A5A5A5A5A5A5ULL);
    ok_eq_ulong(ClaimedRunCount, 0xA5A5A5A5);
}

static
VOID
TestKsrQueryMetadata(VOID)
{
    UCHAR Metadata = 0xA5;
    ULONG RequiredLength;
    NTSTATUS Status;

    RequiredLength = 0xA5A5A5A5;
    Status = KsrQueryMetadata(&TestKsrMemoryId,
                              PAGE_SIZE,
                              &Metadata,
                              sizeof(Metadata),
                              &RequiredLength);
    ok_eq_hex(Status, STATUS_NOT_SUPPORTED);
    ok_eq_uint(Metadata, 0xA5);
    ok_eq_ulong(RequiredLength, 0xA5A5A5A5);
}

static
VOID
TestKsrPersistMemoryWithMetadata(VOID)
{
    UCHAR Metadata = 0xA5;
    ULONGLONG MemoryRun = 0xA5A5A5A5A5A5A5A5ULL;
    ULONGLONG PersistedLength;
    NTSTATUS Status;

    PersistedLength = 0xA5A5A5A5A5A5A5A5ULL;
    Status = KsrPersistMemoryWithMetadata(&TestKsrMemoryId,
                                          &MemoryRun,
                                          1,
                                          &Metadata,
                                          sizeof(Metadata),
                                          &PersistedLength);
    ok_eq_hex(Status, STATUS_NOT_SUPPORTED);
    ok_eq_ulonglong(PersistedLength, 0xA5A5A5A5A5A5A5A5ULL);
}

static
VOID
TestKsrFirmwareInformation(VOID)
{
    PVOID FirmwareInformation;
    NTSTATUS Status;

    FirmwareInformation = (PVOID)(ULONG_PTR)0xA5A5A5A5;
    Status = KsrGetFirmwareInformation(&FirmwareInformation);
    ok_eq_hex(Status, STATUS_NOT_SUPPORTED);
    ok_eq_pointer(FirmwareInformation, (PVOID)(ULONG_PTR)0xA5A5A5A5);
}

static
VOID
TestKsrEnumeratePersistedMemory(VOID)
{
    ULONG CallbackCount;
    NTSTATUS Status;

    CallbackCount = 0;
    Status = KsrEnumeratePersistedMemory(&TestKsrMemoryId,
                                         TestKsrEnumerateCallback,
                                         &CallbackCount);
    ok_eq_hex(Status, STATUS_NOT_SUPPORTED);
    ok_eq_ulong(CallbackCount, 0);
}

static
VOID
TestKsrMdlToMemoryRuns(VOID)
{
    MDL Mdl;
    ULONGLONG MemoryRun = 0xA5A5A5A5A5A5A5A5ULL;
    ULONG MemoryRunCount;
    NTSTATUS Status;

    RtlZeroMemory(&Mdl, sizeof(Mdl));
    MemoryRunCount = 0xA5A5A5A5;
    Status = KsrMdlToMemoryRuns(&Mdl,
                                &MemoryRun,
                                1,
                                &MemoryRunCount);
    ok_eq_hex(Status, STATUS_NOT_SUPPORTED);
    ok_eq_ulonglong(MemoryRun, 0xA5A5A5A5A5A5A5A5ULL);
    ok_eq_ulong(MemoryRunCount, 0xA5A5A5A5);
}

static
VOID
TestKsrFreePersistedMemory(VOID)
{
    KsrFreePersistedMemory(&TestKsrMemoryId, FALSE);
    KsrFreePersistedMemory(&TestKsrMemoryId, TRUE);
    ok(TRUE, "KSR free no-provider calls completed\n");
}

static
VOID
TestKsrApiSetContract(VOID)
{
    BOOLEAN IsReactOS;

    IsReactOS = *(volatile ULONG *)(KI_USER_SHARED_DATA + PAGE_SIZE - sizeof(ULONG)) == 0x8eac705;
    if (skip(IsReactOS,
             "native KSR operations reboot an external caller; import binding is covered separately\n"))
    {
        return;
    }

    TestKsrClaimPersistedMemory();
    TestKsrQueryMetadata();
    TestKsrPersistMemoryWithMetadata();
    TestKsrFirmwareInformation();
    TestKsrEnumeratePersistedMemory();
    TestKsrMdlToMemoryRuns();
    TestKsrFreePersistedMemory();
}
#endif

static
VOID
CheckSecurityDescriptorConversion(
    _In_ PCWSTR Sddl,
    _In_ ULONG ExpectedSize,
    _In_ SECURITY_DESCRIPTOR_CONTROL ExpectedControl,
    _In_ ULONG ExpectedOwner,
    _In_ ULONG ExpectedGroup,
    _In_ ULONG ExpectedSacl,
    _In_ ULONG ExpectedDacl,
    _In_ ULONG ExpectedAceCount,
    _In_ ACCESS_MASK ExpectedMask,
    _In_opt_ PSID ExpectedAceSid)
{
    PSECURITY_DESCRIPTOR SecurityDescriptor = (PSECURITY_DESCRIPTOR)(ULONG_PTR)0xA5A5A5A5;
    PISECURITY_DESCRIPTOR_RELATIVE RelativeDescriptor;
    ULONG SecurityDescriptorSize = 0xA5A5A5A5;
    PACCESS_ALLOWED_ACE Ace;
    PACL Dacl;
    NTSTATUS Status;

    trace("Checking SDDL %S\n", Sddl);
    Status = SeConvertStringSecurityDescriptorToSecurityDescriptor(Sddl, 1, &SecurityDescriptor, &SecurityDescriptorSize);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    RelativeDescriptor = SecurityDescriptor;
    ok_eq_ulong(SecurityDescriptorSize, ExpectedSize);
    ok_bool_true(RtlValidSecurityDescriptor(SecurityDescriptor), "valid self-relative security descriptor");
    ok_eq_uint(RelativeDescriptor->Revision, SECURITY_DESCRIPTOR_REVISION);
    ok_eq_uint(RelativeDescriptor->Control, ExpectedControl);
    ok_eq_ulong(RelativeDescriptor->Owner, ExpectedOwner);
    ok_eq_ulong(RelativeDescriptor->Group, ExpectedGroup);
    ok_eq_ulong(RelativeDescriptor->Sacl, ExpectedSacl);
    ok_eq_ulong(RelativeDescriptor->Dacl, ExpectedDacl);
    if (ExpectedDacl != 0)
    {
        Dacl = (PACL)((PUCHAR)RelativeDescriptor + ExpectedDacl);
        ok_eq_uint(Dacl->AclRevision, ACL_REVISION);
        ok_eq_uint(Dacl->AceCount, ExpectedAceCount);
        if (ExpectedAceCount != 0)
        {
            Status = RtlGetAce(Dacl, 0, (PVOID *)&Ace);
            ok_eq_hex(Status, STATUS_SUCCESS);
            if (NT_SUCCESS(Status))
            {
                ok_eq_uint(Ace->Header.AceType, ACCESS_ALLOWED_ACE_TYPE);
                ok_eq_hex(Ace->Mask, ExpectedMask);
                ok_bool_true(RtlEqualSid((PSID)&Ace->SidStart, ExpectedAceSid), "expected SDDL ACE trustee");
            }
        }
    }
    if (ExpectedOwner != 0)
        ok_bool_true(RtlEqualSid((PSID)((PUCHAR)RelativeDescriptor + ExpectedOwner), SeExports->SeLocalSystemSid), "expected SDDL owner");
    if (ExpectedGroup != 0)
        ok_bool_true(RtlEqualSid((PSID)((PUCHAR)RelativeDescriptor + ExpectedGroup), SeExports->SeAliasAdminsSid), "expected SDDL group");
    ExFreePool(SecurityDescriptor);
}

static
VOID
TestSecurityDescriptorConversion(VOID)
{
    PSECURITY_DESCRIPTOR SecurityDescriptor;
    ULONG SecurityDescriptorSize;
    NTSTATUS Status;

    CheckSecurityDescriptorConversion(L"", 20, SE_SELF_RELATIVE, 0, 0, 0, 0, 0, 0, NULL);
    CheckSecurityDescriptorConversion(L"D:", 28, SE_SELF_RELATIVE | SE_DACL_PRESENT, 0, 0, 0, 20, 0, 0, NULL);
    CheckSecurityDescriptorConversion(L"D:P(A;;GA;;;SY)", 48, SE_SELF_RELATIVE | SE_DACL_PRESENT | SE_DACL_PROTECTED, 0, 0, 0, 20, 1, GENERIC_ALL, SeExports->SeLocalSystemSid);
    CheckSecurityDescriptorConversion(L"O:SYG:BAD:(A;;GR;;;WD)", 76, SE_SELF_RELATIVE | SE_DACL_PRESENT, 48, 60, 0, 20, 1, GENERIC_READ, SeExports->SeWorldSid);

    SecurityDescriptor = (PSECURITY_DESCRIPTOR)(ULONG_PTR)0xA5A5A5A5;
    SecurityDescriptorSize = 0xA5A5A5A5;
    Status = SeConvertStringSecurityDescriptorToSecurityDescriptor(L"X:SY", 1, &SecurityDescriptor, &SecurityDescriptorSize);
    ok_eq_hex(Status, NTSTATUS_FROM_WIN32(87));
    ok_eq_pointer(SecurityDescriptor, (PSECURITY_DESCRIPTOR)(ULONG_PTR)0xA5A5A5A5);
    ok_eq_ulong(SecurityDescriptorSize, 0);

    SecurityDescriptor = (PSECURITY_DESCRIPTOR)(ULONG_PTR)0xA5A5A5A5;
    SecurityDescriptorSize = 0xA5A5A5A5;
    Status = SeConvertStringSecurityDescriptorToSecurityDescriptor(L"D:P(A;;GA;;;SY)", 2, &SecurityDescriptor, &SecurityDescriptorSize);
    ok_eq_hex(Status, NTSTATUS_FROM_WIN32(1305));
    ok_eq_pointer(SecurityDescriptor, (PSECURITY_DESCRIPTOR)(ULONG_PTR)0xA5A5A5A5);
    ok_eq_ulong(SecurityDescriptorSize, 0xA5A5A5A5);
}

static
VOID
TestProcessMachine(VOID)
{
    PEPROCESS Process;
    USHORT ProcessMachine;
    USHORT Wow64Machine;

    Process = PsGetCurrentProcess();
    ProcessMachine = PsGetProcessMachine(Process);
    Wow64Machine = PsWow64GetProcessMachine(Process);
#if defined(_M_ARM64)
    ok_eq_uint(ProcessMachine, 0xAA64);
    ok_eq_uint(Wow64Machine, 0xAA64);
#elif defined(_M_AMD64)
    ok_eq_uint(ProcessMachine, IMAGE_FILE_MACHINE_AMD64);
    ok_eq_uint(Wow64Machine, IMAGE_FILE_MACHINE_AMD64);
#elif defined(_M_IX86)
    ok_eq_uint(ProcessMachine, IMAGE_FILE_MACHINE_I386);
    ok_eq_uint(Wow64Machine, IMAGE_FILE_MACHINE_I386);
#else
    ok_eq_uint(Wow64Machine, ProcessMachine);
#endif
    ok_eq_bool(PsIsProtectedProcess(Process), FALSE);
    ok_eq_bool(PsIsProtectedProcessLight(Process), FALSE);
}

#if defined(_M_ARM64)
static
VOID
TestHypervisorVendorId(VOID)
{
    SYSTEM_FIRMWARE_TABLE_INFORMATION Query = {0};
    PSYSTEM_FIRMWARE_TABLE_INFORMATION Table;
    PFADT Fadt;
    ULONGLONG VendorId, FirmwareId = 0;
    ULONG Length;
    NTSTATUS Status;

    VendorId = HvlGetHypervisorVendorId();
    trace("platform state: ARM64 hypervisor vendor 0x%I64x\n", VendorId);
    {
        ULONGLONG SecondVendorId = HvlGetHypervisorVendorId();
        ok_eq_ulonglong(SecondVendorId, VendorId);
    }

    Query.ProviderSignature = 'ACPI';
    Query.Action = SystemFirmwareTable_Get;
    Query.TableID = FADT_SIGNATURE;
    Status = ZwQuerySystemInformation(SystemFirmwareTableInformation, &Query, FIELD_OFFSET(SYSTEM_FIRMWARE_TABLE_INFORMATION, TableBuffer), &Length);
    ok_eq_hex(Status, STATUS_BUFFER_TOO_SMALL);
    if (Status != STATUS_BUFFER_TOO_SMALL)
        return;
    ok(Query.TableBufferLength >= sizeof(DESCRIPTION_HEADER) && Query.TableBufferLength <= 65536,
       "Invalid FADT length %lu\n", Query.TableBufferLength);
    if (Query.TableBufferLength < sizeof(DESCRIPTION_HEADER) || Query.TableBufferLength > 65536)
        return;

    Length = FIELD_OFFSET(SYSTEM_FIRMWARE_TABLE_INFORMATION, TableBuffer) + Query.TableBufferLength;
    Table = ExAllocatePoolWithTag(PagedPool, Length, 'tFvH');
    ok(Table != NULL, "Could not allocate the FADT query buffer\n");
    if (!Table)
        return;
    RtlCopyMemory(Table, &Query, FIELD_OFFSET(SYSTEM_FIRMWARE_TABLE_INFORMATION, TableBuffer));
    Status = ZwQuerySystemInformation(SystemFirmwareTableInformation, Table, Length, &Length);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        Fadt = (PFADT)Table->TableBuffer;
        ok_eq_ulong(Fadt->Header.Signature, FADT_SIGNATURE);
        ok(Fadt->Header.Length <= Table->TableBufferLength, "FADT extends past the query buffer\n");
        if (Fadt->Header.Length >= RTL_SIZEOF_THROUGH_FIELD(FADT, hypervisor_id) &&
            Table->TableBufferLength >= RTL_SIZEOF_THROUGH_FIELD(FADT, hypervisor_id))
            FirmwareId = Fadt->hypervisor_id;
        /* The native export excludes this exact Qualcomm identity. */
        if (FirmwareId == 0x4D4F4351ULL)
            FirmwareId = 0;
        ok_eq_ulonglong(VendorId, FirmwareId);
    }
    ExFreePoolWithTag(Table, 'tFvH');
}
#endif

static
VOID
TestPlatformState(VOID)
{
    static const UNICODE_STRING ExpectedName = RTL_CONSTANT_STRING(L"ext-ms-win-core-win32k-dxgk-l1-1-0");
    static const UNICODE_STRING ExpectedTarget = RTL_CONSTANT_STRING(L"dxgkrnl.sys");
    PTEST_API_SET_NAMESPACE_ENTRY NamespaceEntry;
    PTEST_API_SET_VALUE_ENTRY ValueEntry;
    UNICODE_STRING Name;
    UNICODE_STRING Target;
    PESILO HostSilo;
    PVOID ApiSetSchema;
    PTEST_API_SET_NAMESPACE Namespace;
    BOOLEAN Found = FALSE;
    ULONG Index;

    HostSilo = PsGetHostSilo();
    ok_eq_pointer(HostSilo, NULL);

#if defined(_M_ARM64)
    TestHypervisorVendorId();
#endif

    ApiSetSchema = PsQueryCurrentApiSetSchema();
    ok(ApiSetSchema != NULL, "API-set schema was NULL\n");
    if (ApiSetSchema != NULL)
    {
        Namespace = ApiSetSchema;
        trace("platform state: API-set schema %p, version %lu, size %lu, flags 0x%lx, count %lu, entries %lu, hashes %lu, factor %lu\n", ApiSetSchema, Namespace->Version, Namespace->Size, Namespace->Flags, Namespace->Count, Namespace->EntryOffset, Namespace->HashOffset, Namespace->HashFactor);
        ok_eq_ulong(Namespace->Version, 6);
        ok(Namespace->Size >= sizeof(*Namespace), "API-set schema size %lu was too small\n", Namespace->Size);
        ok(Namespace->Count != 0, "API-set schema was empty\n");
        ok_eq_ulong(Namespace->EntryOffset, sizeof(*Namespace));
        ok(Namespace->HashOffset < Namespace->Size, "API-set hash offset %lu exceeded size %lu\n", Namespace->HashOffset, Namespace->Size);
        ok(Namespace->HashFactor != 0, "API-set hash factor was zero\n");

        NamespaceEntry = (PVOID)((PUCHAR)Namespace + Namespace->EntryOffset);
        for (Index = 0; Index != Namespace->Count; ++Index)
        {
            Name.Buffer = (PVOID)((PUCHAR)Namespace + NamespaceEntry[Index].NameOffset);
            Name.Length = (USHORT)NamespaceEntry[Index].NameLength;
            Name.MaximumLength = Name.Length;
            if (!RtlEqualUnicodeString(&Name, &ExpectedName, TRUE))
                continue;

            Found = TRUE;
            ok_eq_ulong(NamespaceEntry[Index].ValueCount, 1);
            if (NamespaceEntry[Index].ValueCount != 0)
            {
                ValueEntry = (PVOID)((PUCHAR)Namespace + NamespaceEntry[Index].ValueOffset);
                Target.Buffer = (PVOID)((PUCHAR)Namespace + ValueEntry->ValueOffset);
                Target.Length = (USHORT)ValueEntry->ValueLength;
                Target.MaximumLength = Target.Length;
                ok_bool_true(RtlEqualUnicodeString(&Target, &ExpectedTarget, TRUE), "expected dxgkrnl API-set host");
            }
            break;
        }
        ok_bool_true(Found, "expected WDDM API-set contract");
    }
}

static
VOID
TestPhysicalMemoryRanges(VOID)
{
    PPHYSICAL_MEMORY_RANGE SystemRanges;

    SystemRanges = MmGetPhysicalMemoryRangesEx(NULL);
    trace("physical ranges: system %p\n", SystemRanges);
    ok(SystemRanges != NULL, "system physical ranges were NULL\n");
    if (SystemRanges != NULL)
        ExFreePool(SystemRanges);
}

static
VOID
TestModernProcessEnergyState(VOID)
{
    TEST_PROCESS_ENERGY_VALUES Before;
    TEST_PROCESS_ENERGY_VALUES After;
    ULONG ReturnLength;
    NTSTATUS Status;

    RtlFillMemory(&Before, sizeof(Before), 0xA5);
    ReturnLength = 0xA5A5A5A5;
    Status = ZwQueryInformationProcess(NtCurrentProcess(), ProcessEnergyValues, &Before, sizeof(Before), &ReturnLength);
    trace("ProcessEnergyValues returned 0x%08lx, length %lu, disk %I64u, network %I64u/%I64u, MBB %I64u/%I64u\n", Status, ReturnLength, Before.DiskEnergy, Before.NetworkTailEnergy, Before.NetworkTxRxBytes, Before.MbbTailEnergy, Before.MbbTxRxBytes);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(ReturnLength, sizeof(Before));
    if (NT_SUCCESS(Status))
    {
        PsUpdateComponentPower(PsGetCurrentProcess(), 1, 0x101);
        PsUpdateComponentPower(PsGetCurrentProcess(), 2, 0x0000010200000103ULL);
        PsUpdateComponentPower(PsGetCurrentProcess(), 3, 0x0000010400000105ULL);
        RtlFillMemory(&After, sizeof(After), 0xA5);
        ReturnLength = 0xA5A5A5A5;
        Status = ZwQueryInformationProcess(NtCurrentProcess(), ProcessEnergyValues, &After, sizeof(After), &ReturnLength);
        trace("ProcessEnergyValues after update returned 0x%08lx, length %lu, disk %I64u, network %I64u/%I64u, MBB %I64u/%I64u\n", Status, ReturnLength, After.DiskEnergy, After.NetworkTailEnergy, After.NetworkTxRxBytes, After.MbbTailEnergy, After.MbbTxRxBytes);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok_eq_ulong(ReturnLength, sizeof(After));
        ok_eq_ulonglong(After.DiskEnergy - Before.DiskEnergy, 0x101);
        ok_eq_ulonglong(After.NetworkTailEnergy - Before.NetworkTailEnergy, 0x102);
        ok_eq_ulonglong(After.NetworkTxRxBytes - Before.NetworkTxRxBytes, 0x103);
        ok_eq_ulonglong(After.MbbTailEnergy - Before.MbbTailEnergy, 0x104);
        ok_eq_ulonglong(After.MbbTxRxBytes - Before.MbbTxRxBytes, 0x105);
    }
}

static
UCHAR
ProcessFaultCounts(
    _In_ PEPROCESS Process)
{
    return (UCHAR)(Process->HangCount | (Process->GhostCount << 3) | (Process->PrefilterException << 6));
}

static
VOID
TestModernProcessFaultState(VOID)
{
    BOOLEAN IsReactOS;
    UCHAR BeforeCounts = 0;
    UCHAR ExpectedCounts;
    ULONG BeforeFlags = 0;
    ULONG FaultInformation;
    ULONG FaultQuery;
    ULONG ReturnLength;
    NTSTATUS Status;

    IsReactOS = *(volatile ULONG *)(KI_USER_SHARED_DATA + PAGE_SIZE - sizeof(ULONG)) == 0x8eac705;
    if (IsReactOS)
    {
        BeforeCounts = ProcessFaultCounts(PsGetCurrentProcess());
        BeforeFlags = PsGetCurrentProcess()->Flags3;
    }
    FaultQuery = 0xA5A5A5A5;
    ReturnLength = 0xA5A5A5A5;
    Status = ZwQueryInformationProcess(NtCurrentProcess(), ProcessFaultInformation, &FaultQuery, sizeof(FaultQuery), &ReturnLength);
    trace("ProcessFaultInformation before returned 0x%08lx, length %lu, value 0x%08lx\n", Status, ReturnLength, FaultQuery);
    ok_eq_hex(Status, STATUS_INVALID_INFO_CLASS);
    FaultInformation = 0xF;
    PsSetProcessFaultInformation(PsGetCurrentProcess(), &FaultInformation);
    FaultQuery = 0xA5A5A5A5;
    ReturnLength = 0xA5A5A5A5;
    Status = ZwQueryInformationProcess(NtCurrentProcess(), ProcessFaultInformation, &FaultQuery, sizeof(FaultQuery), &ReturnLength);
    trace("ProcessFaultInformation after returned 0x%08lx, length %lu, value 0x%08lx\n", Status, ReturnLength, FaultQuery);
    ok_eq_hex(Status, STATUS_INVALID_INFO_CLASS);

    if (skip(IsReactOS, "native EPROCESS fields are intentionally opaque\n"))
        return;

    ExpectedCounts = BeforeCounts | 0x40;
    if ((BeforeCounts & 0x7) != 0x7)
        ExpectedCounts = (ExpectedCounts & ~0x7) | ((BeforeCounts & 0x7) + 1);
    if (((BeforeCounts >> 3) & 0x7) != 0x7)
        ExpectedCounts = (ExpectedCounts & ~0x38) | ((((BeforeCounts >> 3) & 0x7) + 1) << 3);
    ok_eq_uint(ProcessFaultCounts(PsGetCurrentProcess()), ExpectedCounts);
    ok_eq_hex(PsGetCurrentProcess()->Flags3, BeforeFlags | 0x4);
    FaultInformation = 0x6;
    for (ReturnLength = 0; ReturnLength != 8; ReturnLength++)
        PsSetProcessFaultInformation(PsGetCurrentProcess(), &FaultInformation);
    ok_eq_uint(ProcessFaultCounts(PsGetCurrentProcess()) & 0x7F, 0x7F);
}

static
VOID
TestModernProcessWindowState(VOID)
{
    PVOID Context;
    NTSTATUS Status;

    if (skip(*(volatile ULONG *)(KI_USER_SHARED_DATA + PAGE_SIZE - sizeof(ULONG)) == 0x8eac705, "native window-state provider can block an isolated caller\n"))
        return;

    Status = PsSetProcessesWindowState(0, NULL);
    trace("PsSetProcessesWindowState(0) returned 0x%08lx\n", Status);
    ok_eq_hex(Status, STATUS_SUCCESS);

    Context = (PVOID)(ULONG_PTR)0x12345678;
    Status = PsSetProcessesWindowState(0xA5, Context);
    trace("PsSetProcessesWindowState(0xA5) returned 0x%08lx\n", Status);
    ok_eq_hex(Status, STATUS_SUCCESS);
}

static
VOID
TestModernPowerState(VOID)
{
    PoLatencySensitivityHint(0);
    PoLatencySensitivityHint(4);
    PoNotifyVSyncChange(FALSE);
    PoNotifyVSyncChange(TRUE);
    PoSetUserPresent(0);
    ok(TRUE, "power-state notification calls completed\n");
}

static
VOID
TestModernInbvState(VOID)
{
    NTSTATUS Status;

    Status = InbvSetVirtualFrameBuffer(NULL);
    trace("InbvSetVirtualFrameBuffer(NULL) returned 0x%08lx\n", Status);
    ok_eq_hex(Status, STATUS_SUCCESS);
}

static
VOID
TestModernProviderState(VOID)
{
    static const WCHAR ExpectedLicenseData[] = L"EMPTY";
    static UNICODE_STRING LicenseName = RTL_CONSTANT_STRING(L"Kernel-MUI-Language-Allowed");
    static UNICODE_STRING MissingLicenseName = RTL_CONSTANT_STRING(L"ReactOS-Missing-License-Value");
    static const UNICODE_STRING PartitionName = RTL_CONSTANT_STRING(L"\\??\\MemoryPartitionGraphics");
    UCHAR PartitionInformation[0xF0];
    OBJECT_ATTRIBUTES ObjectAttributes;
    WCHAR LicenseData[16];
    ULONG ResultDataSize;
    ULONG Type;
    HANDLE PartitionHandle;
    NTSTATUS Status;

    Type = 0xA5A5A5A5;
    ResultDataSize = 0xA5A5A5A5;
    Status = ZwQueryLicenseValue(&LicenseName, &Type, LicenseData, 0, &ResultDataSize);
    trace("ZwQueryLicenseValue(size 0) returned 0x%08lx, type %lu, size %lu\n", Status, Type, ResultDataSize);
    ok_eq_hex(Status, STATUS_BUFFER_TOO_SMALL);
    ok_eq_ulong(Type, REG_SZ);
    ok_eq_ulong(ResultDataSize, sizeof(ExpectedLicenseData));

    RtlFillMemory(LicenseData, sizeof(LicenseData), 0xA5);
    Status = ZwQueryLicenseValue(&LicenseName, &Type, LicenseData, sizeof(LicenseData), &ResultDataSize);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(Type, REG_SZ);
    ok_eq_ulong(ResultDataSize, sizeof(ExpectedLicenseData));
    ok(RtlEqualMemory(LicenseData, ExpectedLicenseData, sizeof(ExpectedLicenseData)), "unexpected MUI license data\n");

    Type = 0xA5A5A5A5;
    ResultDataSize = 0xA5A5A5A5;
    Status = ZwQueryLicenseValue(&MissingLicenseName, &Type, LicenseData, sizeof(LicenseData), &ResultDataSize);
    ok_eq_hex(Status, STATUS_OBJECT_NAME_NOT_FOUND);
    ok_eq_ulong(Type, 0xA5A5A5A5);
    ok_eq_ulong(ResultDataSize, 0xA5A5A5A5);

    ok(PsPartitionType != NULL, "PsPartitionType was NULL\n");
    InitializeObjectAttributes(&ObjectAttributes, (PUNICODE_STRING)&PartitionName, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    PartitionHandle = NULL;
    Status = ZwOpenPartition(&PartitionHandle, 0x001F0003, &ObjectAttributes);
    trace("ZwOpenPartition(MemoryPartitionGraphics) returned 0x%08lx, handle %p\n", Status, PartitionHandle);
    if (!NT_SUCCESS(Status))
    {
        ok_eq_hex(Status, STATUS_OBJECT_NAME_NOT_FOUND);
        skip(FALSE, "graphics partition provider is unavailable\n");
    }
    else
    {
        RtlFillMemory(PartitionInformation, sizeof(PartitionInformation), 0xA5);
        Status = ZwManagePartition(PartitionHandle, NULL, 0, PartitionInformation, sizeof(PartitionInformation));
        trace("ZwManagePartition(graphics, class 0) returned 0x%08lx, pages %Iu, id %lu\n", Status, *(PSIZE_T)&PartitionInformation[0x30], *(PULONG)&PartitionInformation[0xE8]);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok(*(PSIZE_T)&PartitionInformation[0x30] != 0, "partition page count was zero\n");
        ZwClose(PartitionHandle);
    }

    Status = DifRegisterClassDriverPlugin(0, NULL, 0, KmtDriverObject);
    ok_eq_hex(Status, STATUS_DIF_DRIVER_PLUGIN_MISMATCH);
}

typedef struct _TEST_TLS_WORKER_CONTEXT
{
    KEVENT ReadyEvent;
    KEVENT ReleaseEvent;
    ULONG TlsIndex;
    PVOID Value;
    NTSTATUS SetStatus;
    BOOLEAN WaitForRelease;
} TEST_TLS_WORKER_CONTEXT, *PTEST_TLS_WORKER_CONTEXT;

static volatile LONG TestTlsCallbackCount;
static PVOID TestTlsCallbackValues[4];
static KIRQL TestTlsCallbackIrql;

static
VOID
NTAPI
TestTlsCallback(
    _In_opt_ PVOID Value)
{
    LONG Index;

    Index = InterlockedIncrement(&TestTlsCallbackCount) - 1;
    if ((ULONG)Index < RTL_NUMBER_OF(TestTlsCallbackValues))
        TestTlsCallbackValues[Index] = Value;
    TestTlsCallbackIrql = KeGetCurrentIrql();
}

static
VOID
NTAPI
TestTlsWorker(
    _In_ PVOID Parameter)
{
    PTEST_TLS_WORKER_CONTEXT Context = Parameter;

    Context->SetStatus = PsTlsSetValue(Context->TlsIndex, Context->Value);
    KeSetEvent(&Context->ReadyEvent, IO_NO_INCREMENT, FALSE);
    if (Context->WaitForRelease)
        KeWaitForSingleObject(&Context->ReleaseEvent, Executive, KernelMode, FALSE, NULL);
    PsTerminateSystemThread(STATUS_SUCCESS);
}

static
BOOLEAN
TestTlsSawCallbackValue(
    _In_ PVOID Value)
{
    ULONG Index;

    for (Index = 0; Index < RTL_NUMBER_OF(TestTlsCallbackValues); Index++)
    {
        if (TestTlsCallbackValues[Index] == Value)
            return TRUE;
    }
    return FALSE;
}

static
VOID
TestModernTlsState(VOID)
{
    const PVOID TestValue = (PVOID)(ULONG_PTR)0x12345678;
    const PVOID MainValue = (PVOID)(ULONG_PTR)0x11111111;
    const PVOID WorkerValue = (PVOID)(ULONG_PTR)0x22222222;
    TEST_TLS_WORKER_CONTEXT WorkerContext;
    HANDLE ThreadHandle;
    PVOID Value;
    ULONG CallbackTlsIndex;
    ULONG TlsIndex;
    NTSTATUS Status;

    Value = TestValue;
    Status = PsTlsGetValue(0, &Value);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
    ok_eq_pointer(Value, TestValue);
    Status = PsTlsSetValue(MAXULONG, TestValue);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);

    TlsIndex = MAXULONG;
    Status = PsTlsAlloc(NULL, 1, &TlsIndex);
    trace("PsTlsAlloc(flags 1) returned 0x%08lx, index %lu\n", Status, TlsIndex);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
    ok_eq_ulong(TlsIndex, MAXULONG);

    TlsIndex = MAXULONG;
    Status = PsTlsAlloc(NULL, 0, &TlsIndex);
    trace("PsTlsAlloc returned 0x%08lx, index %lu\n", Status, TlsIndex);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    Value = TestValue;
    Status = PsTlsGetValue(TlsIndex, &Value);
    trace("PsTlsGetValue(initial) returned 0x%08lx, value %p\n", Status, Value);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_pointer(Value, NULL);

    Status = PsTlsSetValue(TlsIndex, TestValue);
    trace("PsTlsSetValue returned 0x%08lx\n", Status);
    ok_eq_hex(Status, STATUS_SUCCESS);

    Value = NULL;
    Status = PsTlsGetValue(TlsIndex, &Value);
    trace("PsTlsGetValue(stored) returned 0x%08lx, value %p\n", Status, Value);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_pointer(Value, TestValue);

    PsTlsFree(TlsIndex);

    Value = TestValue;
    Status = PsTlsGetValue(TlsIndex, &Value);
    trace("PsTlsGetValue(freed) returned 0x%08lx, value %p\n", Status, Value);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_pointer(Value, NULL);

    Status = PsTlsSetValue(TlsIndex, TestValue);
    trace("PsTlsSetValue(freed) returned 0x%08lx\n", Status);
    ok_eq_hex(Status, STATUS_SUCCESS);

    Value = NULL;
    Status = PsTlsGetValue(TlsIndex, &Value);
    trace("PsTlsGetValue(unallocated stored) returned 0x%08lx, value %p\n", Status, Value);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_pointer(Value, TestValue);

    Status = PsTlsSetValue(TlsIndex, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);

    RtlZeroMemory(TestTlsCallbackValues, sizeof(TestTlsCallbackValues));
    TestTlsCallbackCount = 0;
    TestTlsCallbackIrql = HIGH_LEVEL;
    CallbackTlsIndex = MAXULONG;
    Status = PsTlsAlloc(TestTlsCallback, 0, &CallbackTlsIndex);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    Status = PsTlsSetValue(CallbackTlsIndex, (PVOID)MainValue);
    ok_eq_hex(Status, STATUS_SUCCESS);

    RtlZeroMemory(&WorkerContext, sizeof(WorkerContext));
    KeInitializeEvent(&WorkerContext.ReadyEvent, NotificationEvent, FALSE);
    KeInitializeEvent(&WorkerContext.ReleaseEvent, NotificationEvent, FALSE);
    WorkerContext.TlsIndex = CallbackTlsIndex;
    WorkerContext.Value = (PVOID)WorkerValue;
    WorkerContext.SetStatus = STATUS_UNSUCCESSFUL;
    WorkerContext.WaitForRelease = TRUE;
    ThreadHandle = NULL;
    Status = PsCreateSystemThread(&ThreadHandle, THREAD_ALL_ACCESS, NULL, NULL, NULL, TestTlsWorker, &WorkerContext);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
    {
        PsTlsFree(CallbackTlsIndex);
        return;
    }

    Status = KeWaitForSingleObject(&WorkerContext.ReadyEvent, Executive, KernelMode, FALSE, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_hex(WorkerContext.SetStatus, STATUS_SUCCESS);
    Value = NULL;
    Status = PsTlsGetValue(CallbackTlsIndex, &Value);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_pointer(Value, MainValue);

    PsTlsFree(CallbackTlsIndex);
    ok_eq_long(TestTlsCallbackCount, 2);
    ok_bool_true(TestTlsSawCallbackValue((PVOID)MainValue), "main-thread TLS callback value");
    ok_bool_true(TestTlsSawCallbackValue((PVOID)WorkerValue), "worker-thread TLS callback value");
    ok(TestTlsCallbackIrql <= APC_LEVEL, "TLS callback ran at IRQL %u\n", TestTlsCallbackIrql);
    Value = TestValue;
    Status = PsTlsGetValue(CallbackTlsIndex, &Value);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_pointer(Value, NULL);

    KeSetEvent(&WorkerContext.ReleaseEvent, IO_NO_INCREMENT, FALSE);
    Status = ZwWaitForSingleObject(ThreadHandle, FALSE, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ZwClose(ThreadHandle);
    ok_eq_long(TestTlsCallbackCount, 2);

    RtlZeroMemory(TestTlsCallbackValues, sizeof(TestTlsCallbackValues));
    TestTlsCallbackCount = 0;
    TestTlsCallbackIrql = HIGH_LEVEL;
    CallbackTlsIndex = MAXULONG;
    Status = PsTlsAlloc(TestTlsCallback, 0, &CallbackTlsIndex);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    RtlZeroMemory(&WorkerContext, sizeof(WorkerContext));
    KeInitializeEvent(&WorkerContext.ReadyEvent, NotificationEvent, FALSE);
    KeInitializeEvent(&WorkerContext.ReleaseEvent, NotificationEvent, FALSE);
    WorkerContext.TlsIndex = CallbackTlsIndex;
    WorkerContext.Value = (PVOID)WorkerValue;
    WorkerContext.SetStatus = STATUS_UNSUCCESSFUL;
    ThreadHandle = NULL;
    Status = PsCreateSystemThread(&ThreadHandle, THREAD_ALL_ACCESS, NULL, NULL, NULL, TestTlsWorker, &WorkerContext);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
    {
        PsTlsFree(CallbackTlsIndex);
        return;
    }

    Status = ZwWaitForSingleObject(ThreadHandle, FALSE, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ZwClose(ThreadHandle);
    ok_eq_hex(WorkerContext.SetStatus, STATUS_SUCCESS);
    ok_eq_long(TestTlsCallbackCount, 1);
    ok_eq_pointer(TestTlsCallbackValues[0], WorkerValue);
    ok(TestTlsCallbackIrql <= APC_LEVEL, "thread-exit TLS callback ran at IRQL %u\n", TestTlsCallbackIrql);
    PsTlsFree(CallbackTlsIndex);
    ok_eq_long(TestTlsCallbackCount, 1);
}

START_TEST(ExWddm)
{
    TestFirstEntrySList();
    TestExecutiveSpinLocks();
    TestLkmdCallbacks();
    TestStackWalkApcState();
    TestTtmLifecycle();
    TestDeviceAddressSpace();
    TestUnavailableFrameworks();
    TestKernelApiSets();
#ifdef KMT_KSR_APISET
    TestKsrApiSetContract();
#endif
    TestSecurityDescriptorConversion();
    TestProcessMachine();
    TestPlatformState();
    TestPhysicalMemoryRanges();
}

START_TEST(ExWddmEnergy)
{
    TestModernProcessEnergyState();
}

START_TEST(ExWddmPlatform)
{
    TestProcessMachine();
    TestPlatformState();
    TestPhysicalMemoryRanges();
}

START_TEST(ExWddmFault)
{
    TestModernProcessFaultState();
}

#ifdef KMT_KSR_APISET
START_TEST(ExWddmKsrLoad)
{
    ok(TRUE, "KSR API-set import table resolved\n");
}

START_TEST(ExWddmKsr)
{
    TestKsrApiSetContract();
}
#endif

START_TEST(ExWddmWindow)
{
    TestModernProcessWindowState();
}

START_TEST(ExWddmPower)
{
    TestModernPowerState();
}

START_TEST(ExWddmPoFx)
{
    Test_PoFxState();
}

START_TEST(ExWddmInbv)
{
    TestModernInbvState();
}

START_TEST(ExWddmRotate)
{
    Test_MmWddmRotate();
}

START_TEST(ExWddmRotateValid)
{
    Test_MmWddmRotateValid();
}

START_TEST(ExWddmProviders)
{
    TestModernProviderState();
}

START_TEST(ExWddmTls)
{
    TestModernTlsState();
}
