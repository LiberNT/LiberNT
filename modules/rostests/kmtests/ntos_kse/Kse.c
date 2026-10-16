/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Kernel Shim Engine registration and device data tests
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

typedef struct _TEST_KSE_HOOK
{
    ULONG Type;
    PVOID Name;
    PVOID HookFunction;
    PVOID OriginalFunction;
} TEST_KSE_HOOK;

typedef struct _TEST_KSE_HOOK_COLLECTION
{
    ULONG Type;
    PCWSTR ExportDriverName;
    TEST_KSE_HOOK *HookArray;
} TEST_KSE_HOOK_COLLECTION;

typedef struct _TEST_KSE_SHIM
{
    SIZE_T Size;
    GUID *ShimGuid;
    PCWSTR ShimName;
    PVOID KseCallbackRoutines;
    PVOID ShimmedDriverTargetedNotification;
    PVOID ShimmedDriverUntargetedNotification;
    TEST_KSE_HOOK_COLLECTION *HookCollectionsArray;
} TEST_KSE_SHIM;

typedef NTSTATUS (NTAPI *PFN_KSE_REGISTER_SHIM)(TEST_KSE_SHIM *, PVOID, ULONG);
typedef NTSTATUS (NTAPI *PFN_KSE_REGISTER_SHIM_EX)(TEST_KSE_SHIM *, PVOID, ULONG, PVOID);
typedef NTSTATUS (NTAPI *PFN_KSE_UNREGISTER_SHIM)(TEST_KSE_SHIM *, PVOID, PVOID);
typedef NTSTATUS (NTAPI *PFN_KSE_QUERY_DEVICE_FLAGS)(PCWSTR, PCWSTR, PULONG64);
typedef NTSTATUS (NTAPI *PFN_KSE_SET_DEVICE_FLAGS)(PCWSTR, PCWSTR, ULONG64);
typedef NTSTATUS (NTAPI *PFN_KSE_QUERY_DEVICE_DATA)(PCWSTR, PCWSTR, PULONG, PULONG, PVOID);
typedef NTSTATUS (NTAPI *PFN_KSE_QUERY_DEVICE_DATA_LIST)(PCWSTR, PWSTR, ULONG, PULONG);

static GUID TestShimGuid = { 0x5a3c1f27, 0x8b4e, 0x4d6a, { 0x9e, 0x21, 0x7c, 0x55, 0x10, 0x3b, 0xa4, 0x6d } };

static
NTSTATUS
NTAPI
TestKseHookTarget(VOID)
{
    return STATUS_SUCCESS;
}

static TEST_KSE_HOOK TestHooks[] =
{
    { 0, "ExAllocatePoolWithTag", (PVOID)TestKseHookTarget, NULL },
    { 2, NULL, NULL, NULL },
};

static TEST_KSE_HOOK_COLLECTION TestCollections[] =
{
    { 0, L"ntoskrnl.exe", TestHooks },
    { 4, NULL, NULL },
};

static
PVOID
TestKseGetRoutine(
    _In_ PCWSTR Name)
{
    UNICODE_STRING RoutineName;

    RtlInitUnicodeString(&RoutineName, Name);
    return MmGetSystemRoutineAddress(&RoutineName);
}

static
VOID
TestKseInitShim(
    _Out_ TEST_KSE_SHIM *Shim)
{
    RtlZeroMemory(Shim, sizeof(*Shim));
    Shim->Size = sizeof(*Shim);
    Shim->ShimGuid = &TestShimGuid;
    Shim->ShimName = L"KmtestShim";
    Shim->HookCollectionsArray = TestCollections;
}

START_TEST(KseShim)
{
    PFN_KSE_REGISTER_SHIM pKseRegisterShim = TestKseGetRoutine(L"KseRegisterShim");
    PFN_KSE_REGISTER_SHIM_EX pKseRegisterShimEx = TestKseGetRoutine(L"KseRegisterShimEx");
    PFN_KSE_UNREGISTER_SHIM pKseUnregisterShim = TestKseGetRoutine(L"KseUnregisterShim");
    TEST_KSE_SHIM Shim, Duplicate, Broken;
    NTSTATUS Status;

    if (!pKseRegisterShim || !pKseRegisterShimEx || !pKseUnregisterShim)
    {
        skip(FALSE, "Kernel Shim Engine exports not available\n");
        return;
    }

    Status = pKseRegisterShim(NULL, NULL, 0);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);

    Status = pKseUnregisterShim(NULL, NULL, NULL);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);

    TestKseInitShim(&Shim);
    Status = pKseUnregisterShim(&Shim, NULL, NULL);
    ok_eq_hex(Status, STATUS_OBJECT_NAME_NOT_FOUND);

    TestKseInitShim(&Broken);
    Broken.HookCollectionsArray = NULL;
    Status = pKseRegisterShim(&Broken, NULL, 0);
    ok_eq_hex(Status, STATUS_UNSUCCESSFUL);
    ok_eq_pointer(Broken.KseCallbackRoutines, NULL);

    Status = pKseRegisterShim(&Shim, NULL, 0);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok(Shim.KseCallbackRoutines != NULL, "KseCallbackRoutines not set\n");

    TestKseInitShim(&Duplicate);
    Status = pKseRegisterShim(&Duplicate, NULL, 0);
    ok_eq_hex(Status, STATUS_OBJECT_NAME_COLLISION);
    ok_eq_pointer(Duplicate.KseCallbackRoutines, NULL);

    Status = pKseRegisterShimEx(&Duplicate, NULL, 0, &Duplicate);
    ok_eq_hex(Status, STATUS_OBJECT_NAME_COLLISION);

    Status = pKseUnregisterShim(&Duplicate, NULL, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);

    Status = pKseUnregisterShim(&Shim, NULL, NULL);
    ok_eq_hex(Status, STATUS_OBJECT_NAME_NOT_FOUND);

    Status = pKseRegisterShimEx(&Shim, NULL, 0, &Shim);
    ok_eq_hex(Status, STATUS_SUCCESS);

    Status = pKseUnregisterShim(&Shim, NULL, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
}

#define TEST_KSE_DEVICE   L"KMTEST\\KSEDEVICE\\0001"
#define TEST_KSE_PROVIDER L"KmtestProvider"
#define TEST_KSE_KEY      L"\\Registry\\Machine\\System\\CurrentControlSet\\Control\\Compatibility\\Device\\KMTEST!KSEDEVICE!0001"

static
VOID
TestKseDeleteDeviceKey(VOID)
{
    UNICODE_STRING KeyName = RTL_CONSTANT_STRING(TEST_KSE_KEY);
    OBJECT_ATTRIBUTES ObjectAttributes;
    HANDLE KeyHandle;

    InitializeObjectAttributes(&ObjectAttributes, &KeyName, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    if (NT_SUCCESS(ZwOpenKey(&KeyHandle, DELETE, &ObjectAttributes)))
    {
        ZwDeleteKey(KeyHandle);
        ZwClose(KeyHandle);
    }
}

START_TEST(KseDeviceData)
{
    PFN_KSE_QUERY_DEVICE_FLAGS pKseQueryDeviceFlags = TestKseGetRoutine(L"KseQueryDeviceFlags");
    PFN_KSE_SET_DEVICE_FLAGS pKseSetDeviceFlags = TestKseGetRoutine(L"KseSetDeviceFlags");
    PFN_KSE_QUERY_DEVICE_DATA pKseQueryDeviceData = TestKseGetRoutine(L"KseQueryDeviceData");
    PFN_KSE_QUERY_DEVICE_DATA_LIST pKseQueryDeviceDataList = TestKseGetRoutine(L"KseQueryDeviceDataList");
    ULONG64 Flags, Data;
    ULONG Type, Size, Required;
    WCHAR List[64];
    NTSTATUS Status;

    if (!pKseQueryDeviceFlags || !pKseSetDeviceFlags || !pKseQueryDeviceData || !pKseQueryDeviceDataList)
    {
        skip(FALSE, "Kernel Shim Engine device exports not available\n");
        return;
    }

    TestKseDeleteDeviceKey();

    Status = pKseQueryDeviceFlags(TEST_KSE_DEVICE, TEST_KSE_PROVIDER, NULL);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
    Status = pKseQueryDeviceFlags(NULL, TEST_KSE_PROVIDER, &Flags);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);

    Flags = 0xCCCCCCCCCCCCCCCCULL;
    Status = pKseQueryDeviceFlags(TEST_KSE_DEVICE, TEST_KSE_PROVIDER, &Flags);
    ok_eq_hex(Status, STATUS_NOT_FOUND);
    ok_eq_ulonglong(Flags, 0ULL);

    Required = 0xCCCCCCCC;
    Status = pKseQueryDeviceDataList(TEST_KSE_DEVICE, List, sizeof(List), &Required);
    ok_eq_hex(Status, STATUS_NOT_FOUND);

    Status = pKseSetDeviceFlags(NULL, TEST_KSE_PROVIDER, 1);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
    Status = pKseSetDeviceFlags(TEST_KSE_DEVICE, NULL, 1);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);

    Status = pKseSetDeviceFlags(TEST_KSE_DEVICE, TEST_KSE_PROVIDER, 0x0123456789ABCDEFULL);
    ok_eq_hex(Status, STATUS_SUCCESS);

    Flags = 0;
    Status = pKseQueryDeviceFlags(TEST_KSE_DEVICE, TEST_KSE_PROVIDER, &Flags);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulonglong(Flags, 0x0123456789ABCDEFULL);

    Flags = 0xCCCCCCCCCCCCCCCCULL;
    Status = pKseQueryDeviceFlags(TEST_KSE_DEVICE, L"OtherProvider", &Flags);
    ok_eq_hex(Status, STATUS_NOT_FOUND);
    ok_eq_ulonglong(Flags, 0ULL);

    Type = REG_QWORD;
    Size = sizeof(Data);
    Data = 0;
    Status = pKseQueryDeviceData(TEST_KSE_DEVICE, TEST_KSE_PROVIDER, &Type, &Size, &Data);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_hex(Type, 0x1000000BUL);
    ok_eq_ulong(Size, (ULONG)sizeof(Data));
    ok_eq_ulonglong(Data, 0x0123456789ABCDEFULL);

    Type = REG_QWORD;
    Size = 4;
    Status = pKseQueryDeviceData(TEST_KSE_DEVICE, TEST_KSE_PROVIDER, &Type, &Size, &Data);
    ok_eq_hex(Status, STATUS_BUFFER_TOO_SMALL);
    ok_eq_ulong(Size, (ULONG)sizeof(Data));

    Status = pKseQueryDeviceData(TEST_KSE_DEVICE, TEST_KSE_PROVIDER, NULL, &Size, &Data);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);

    Type = 0x20000000 | REG_QWORD;
    Size = sizeof(Data);
    Status = pKseQueryDeviceData(TEST_KSE_DEVICE, TEST_KSE_PROVIDER, &Type, &Size, &Data);
    ok_eq_hex(Status, STATUS_NOT_FOUND);

    Required = 0;
    Status = pKseQueryDeviceDataList(TEST_KSE_DEVICE, NULL, 0, &Required);
    ok_eq_hex(Status, STATUS_BUFFER_TOO_SMALL);
    ok_eq_ulong(Required, (ULONG)sizeof(TEST_KSE_PROVIDER) + sizeof(WCHAR));

    RtlFillMemory(List, sizeof(List), 0xCC);
    Required = 0;
    Status = pKseQueryDeviceDataList(TEST_KSE_DEVICE, List, sizeof(List), &Required);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(Required, (ULONG)sizeof(TEST_KSE_PROVIDER) + sizeof(WCHAR));
    ok(wcscmp(List, TEST_KSE_PROVIDER) == 0, "List[0] = %ls\n", List);
    ok_eq_hex(List[RTL_NUMBER_OF(TEST_KSE_PROVIDER)], UNICODE_NULL);

    TestKseDeleteDeviceKey();
}
