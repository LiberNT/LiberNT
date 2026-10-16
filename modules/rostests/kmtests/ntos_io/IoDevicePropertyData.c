/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Device property query parity tests
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <initguid.h>
#include <devpkey.h>
#include <ntddstor.h>
#include <usbiodef.h>
#include <cfg.h>

typedef struct _TEST_DEVPROP
{
    PCSTR Name;
    const DEVPROPKEY *Key;
    DEVPROPTYPE Type;
} TEST_DEVPROP;

#define TEST_KEY(Name) { #Name, &DEVPKEY_Device_##Name, DEVPROP_TYPE_EMPTY }
#define TEST_TYPED(Name, Type) { #Name, &DEVPKEY_Device_##Name, Type }

static const TEST_DEVPROP TestDiskKeys[] =
{
    TEST_TYPED(InstanceId, DEVPROP_TYPE_STRING), TEST_TYPED(Parent, DEVPROP_TYPE_STRING),
    TEST_TYPED(HardwareIds, DEVPROP_TYPE_STRING_LIST), TEST_TYPED(Service, DEVPROP_TYPE_STRING),
    TEST_TYPED(Class, DEVPROP_TYPE_STRING), TEST_TYPED(ClassGuid, DEVPROP_TYPE_GUID),
    TEST_TYPED(Driver, DEVPROP_TYPE_STRING), TEST_TYPED(ConfigFlags, DEVPROP_TYPE_UINT32),
    TEST_TYPED(Capabilities, DEVPROP_TYPE_UINT32), TEST_TYPED(ContainerId, DEVPROP_TYPE_GUID),
    TEST_TYPED(DriverDate, DEVPROP_TYPE_FILETIME), TEST_TYPED(DriverVersion, DEVPROP_TYPE_STRING),
    TEST_TYPED(DriverDesc, DEVPROP_TYPE_STRING), TEST_TYPED(DriverInfPath, DEVPROP_TYPE_STRING),
    TEST_TYPED(DriverInfSection, DEVPROP_TYPE_STRING), TEST_TYPED(MatchingDeviceId, DEVPROP_TYPE_STRING),
    TEST_TYPED(DriverProvider, DEVPROP_TYPE_STRING), TEST_TYPED(ProblemCode, DEVPROP_TYPE_UINT32),
    TEST_TYPED(Stack, DEVPROP_TYPE_STRING_LIST), TEST_TYPED(IsPresent, DEVPROP_TYPE_BOOLEAN),
    TEST_TYPED(HasProblem, DEVPROP_TYPE_BOOLEAN), TEST_TYPED(DevNodeStatus, DEVPROP_TYPE_UINT32),
};

static const TEST_DEVPROP TestKeys[] =
{
    TEST_KEY(InstanceId), TEST_KEY(Parent), TEST_KEY(DeviceDesc), TEST_KEY(HardwareIds),
    TEST_KEY(CompatibleIds), TEST_KEY(Service), TEST_KEY(Class), TEST_KEY(ClassGuid),
    TEST_KEY(Driver), TEST_KEY(ConfigFlags), TEST_KEY(Manufacturer), TEST_KEY(FriendlyName),
    TEST_KEY(LocationInfo), TEST_KEY(PDOName), TEST_KEY(Capabilities), TEST_KEY(UINumber),
    TEST_KEY(UpperFilters), TEST_KEY(LowerFilters), TEST_KEY(BusTypeGuid), TEST_KEY(LegacyBusType),
    TEST_KEY(BusNumber), TEST_KEY(EnumeratorName), TEST_KEY(Address), TEST_KEY(RemovalPolicy),
    TEST_KEY(InstallState), TEST_KEY(ContainerId), TEST_KEY(DriverDate), TEST_KEY(DriverVersion),
    TEST_KEY(DriverDesc), TEST_KEY(DriverInfPath), TEST_KEY(DriverInfSection), TEST_KEY(DriverInfSectionExt),
    TEST_KEY(MatchingDeviceId), TEST_KEY(DriverProvider), TEST_KEY(DriverRank), TEST_KEY(ConfigurationId),
    TEST_KEY(InstallDate), TEST_KEY(FirstInstallDate), TEST_KEY(LastArrivalDate), TEST_KEY(SessionId),
    TEST_KEY(InLocalMachineContainer), TEST_KEY(DevNodeStatus), TEST_KEY(ProblemCode), TEST_KEY(BusReportedDeviceDesc),
    TEST_KEY(Stack), TEST_KEY(IsPresent), TEST_KEY(HasProblem), TEST_KEY(Siblings),
    TEST_KEY(Children), TEST_KEY(BaseContainerId), TEST_KEY(ModelId), TEST_KEY(LocationPaths),
    TEST_KEY(PowerData), TEST_KEY(Security), TEST_KEY(SecuritySDS), TEST_KEY(DevType),
    TEST_KEY(Exclusive), TEST_KEY(Characteristics), TEST_KEY(EjectionRelations), TEST_KEY(RemovalRelations),
    TEST_KEY(PowerRelations), TEST_KEY(BusRelations), TEST_KEY(TransportRelations), TEST_KEY(ProblemStatus),
    TEST_KEY(DHP_Rebalance_Policy), TEST_KEY(Numa_Node), TEST_KEY(ReportedDeviceIdsHash), TEST_KEY(DriverLogoLevel),
    TEST_KEY(GenericDriverInstalled),
};

static
PDEVICE_OBJECT
TestGetInterfacePdo(
    _In_ const GUID *InterfaceClass)
{
    PDEVICE_OBJECT DeviceObject, Pdo;
    UNICODE_STRING Name;
    PFILE_OBJECT FileObject;
    PWSTR List;
    NTSTATUS Status;

    Status = IoGetDeviceInterfaces(InterfaceClass, NULL, 0, &List);
    if (!NT_SUCCESS(Status) || !List[0])
    {
        trace("interfaces -> %lx\n", Status);
        if (NT_SUCCESS(Status))
            ExFreePool(List);
        return NULL;
    }

    RtlInitUnicodeString(&Name, List);
    Status = IoGetDeviceObjectPointer(&Name, FILE_READ_ATTRIBUTES, &FileObject, &DeviceObject);
    ExFreePool(List);
    if (!NT_SUCCESS(Status))
    {
        trace("object -> %lx\n", Status);
        return NULL;
    }

    Pdo = IoGetDeviceAttachmentBaseRef(DeviceObject);
    ObDereferenceObject(FileObject);
    return Pdo;
}

static
VOID
TestDumpValue(
    _In_ PCSTR Tag,
    _In_ PCSTR Name,
    _In_ DEVPROPTYPE Type,
    _In_reads_bytes_(Size) PUCHAR Data,
    _In_ ULONG Size)
{
    CHAR Hex[3 * 64 + 1];
    PWCHAR String;
    ULONG i;

    if (Type == DEVPROP_TYPE_STRING || Type == DEVPROP_TYPE_STRING_INDIRECT)
    {
        trace("VAL %s %s \"%.*ls\"\n", Tag, Name, (int)(Size / sizeof(WCHAR)), (PWCHAR)Data);
        return;
    }
    if (Type == DEVPROP_TYPE_STRING_LIST)
    {
        for (String = (PWCHAR)Data; *String && (PUCHAR)String < Data + Size; String += wcslen(String) + 1)
            trace("VAL %s %s [\"%ls\"]\n", Tag, Name, String);
        return;
    }
    for (i = 0; i < Size && i < 64; i++)
        RtlStringCbPrintfA(Hex + 3 * i, sizeof(Hex) - 3 * i, "%02x ", Data[i]);
    Hex[3 * min(Size, 64)] = '\0';
    trace("VAL %s %s %s\n", Tag, Name, Hex);
}

static
VOID
TestQueryPdo(
    _In_ PCSTR Tag,
    _In_ PDEVICE_OBJECT Pdo)
{
    UCHAR Buffer[512];
    DEVPROPTYPE Type;
    ULONG Required;
    NTSTATUS Status, Probe;
    ULONG i;

    for (i = 0; i < RTL_NUMBER_OF(TestKeys); i++)
    {
        Required = 0xCCCCCCCC;
        Type = 0xCCCCCCCC;
        Probe = IoGetDevicePropertyData(Pdo, TestKeys[i].Key, LOCALE_NEUTRAL, 0, 0, NULL, &Required, &Type);
        Status = STATUS_NOT_FOUND;
        if (Probe == STATUS_BUFFER_TOO_SMALL && Required <= sizeof(Buffer))
            Status = IoGetDevicePropertyData(Pdo, TestKeys[i].Key, LOCALE_NEUTRAL, 0, sizeof(Buffer), Buffer, &Required, &Type);
        trace("PROP %s %s probe %lx size %lu type %lx get %lx\n", Tag, TestKeys[i].Name, Probe, Required, Type, Status);
        if (NT_SUCCESS(Status))
            TestDumpValue(Tag, TestKeys[i].Name, Type, Buffer, Required);
    }

    for (i = 0; i <= (ULONG)(DevicePropertyContainerID & 0xFF); i++)
    {
        Required = 0xCCCCCCCC;
        Probe = IoGetDeviceProperty(Pdo, (DEVICE_REGISTRY_PROPERTY)i, 0, NULL, &Required);
        Status = STATUS_NOT_FOUND;
        if (Probe == STATUS_BUFFER_TOO_SMALL && Required <= sizeof(Buffer))
            Status = IoGetDeviceProperty(Pdo, (DEVICE_REGISTRY_PROPERTY)i, sizeof(Buffer), Buffer, &Required);
        trace("LEGACY %s %lu probe %lx size %lu get %lx\n", Tag, i, Probe, Required, Status);
    }
}

static
VOID
TestCheckDiskPdo(
    _In_ PDEVICE_OBJECT Pdo)
{
    DEVPROPTYPE Type;
    ULONG Required, i;
    ULONG DevNodeStatus = 0;
    NTSTATUS Status;

    for (i = 0; i < RTL_NUMBER_OF(TestDiskKeys); i++)
    {
        Required = 0;
        Type = DEVPROP_TYPE_EMPTY;
        Status = IoGetDevicePropertyData(Pdo, TestDiskKeys[i].Key, LOCALE_NEUTRAL, 0, 0, NULL, &Required, &Type);
        ok(Status == STATUS_BUFFER_TOO_SMALL, "%s: status %lx\n", TestDiskKeys[i].Name, Status);
        ok(Type == TestDiskKeys[i].Type, "%s: type %lx, expected %lx\n", TestDiskKeys[i].Name, Type, TestDiskKeys[i].Type);
        ok(Required != 0, "%s: no size\n", TestDiskKeys[i].Name);
    }

    Status = IoGetDevicePropertyData(Pdo, &DEVPKEY_Device_DevNodeStatus, LOCALE_NEUTRAL, 0, sizeof(DevNodeStatus), &DevNodeStatus, &Required, &Type);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_hex(DevNodeStatus & (DN_DRIVER_LOADED | DN_STARTED | DN_NT_ENUMERATOR | DN_NT_DRIVER | DN_HAS_PROBLEM),
              DN_DRIVER_LOADED | DN_STARTED | DN_NT_ENUMERATOR | DN_NT_DRIVER);

    Status = IoGetDevicePropertyData(Pdo, &DEVPKEY_Device_SessionId, LOCALE_NEUTRAL, 0, 0, NULL, &Required, &Type);
    ok_eq_hex(Status, STATUS_OBJECT_NAME_NOT_FOUND);

    Required = 0xCCCCCCCC;
    Status = IoGetDeviceProperty(Pdo, DevicePropertyResourceRequirements, 0, NULL, &Required);
    ok(Status == STATUS_SUCCESS || Status == STATUS_BUFFER_TOO_SMALL, "ResourceRequirements: status %lx\n", Status);
    Required = 0xCCCCCCCC;
    Status = IoGetDeviceProperty(Pdo, DevicePropertyAllocatedResources, 0, NULL, &Required);
    ok(Status == STATUS_SUCCESS || Status == STATUS_BUFFER_TOO_SMALL, "AllocatedResources: status %lx\n", Status);
}

START_TEST(IoDevicePropertyData)
{
    PDEVICE_OBJECT Pdo;

    Pdo = TestGetInterfacePdo(&GUID_DEVINTERFACE_DISK);
    ok(Pdo != NULL, "no disk PDO\n");
    if (Pdo)
    {
        TestCheckDiskPdo(Pdo);
        TestQueryPdo("disk", Pdo);
        ObDereferenceObject(Pdo);
    }

    Pdo = TestGetInterfacePdo(&GUID_DEVINTERFACE_USB_HOST_CONTROLLER);
    if (Pdo)
    {
        TestQueryPdo("xhci", Pdo);
        ObDereferenceObject(Pdo);
    }
}
