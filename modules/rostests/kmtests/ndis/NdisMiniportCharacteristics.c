/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     NdisMRegisterMiniportDriver characteristics and entry point validation
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#define NDIS_MINIPORT_DRIVER 1
#define NDIS680_MINIPORT 1
#include <kmt_test.h>
#include <ndis.h>
#include <drivers/ndis/ndiswdf.h>

static
VOID
NTAPI
NdisTestNeverCalled(VOID)
{
    ok(FALSE, "a miniport handler of a test registration was called\n");
}

#define NDIS_TEST_HANDLER(Type) ((Type *)(PVOID)NdisTestNeverCalled)

typedef struct _NDIS_TEST_DRIVER
{
    DRIVER_OBJECT DriverObject;
    DRIVER_EXTENSION DriverExtension;
} NDIS_TEST_DRIVER, *PNDIS_TEST_DRIVER;

static
VOID
NdisTestInitializeCharacteristics(
    _Out_ PNDIS_MINIPORT_DRIVER_CHARACTERISTICS Characteristics,
    _In_ UCHAR MinorVersion)
{
    RtlZeroMemory(Characteristics, sizeof(*Characteristics));
    Characteristics->Header.Type = NDIS_OBJECT_TYPE_MINIPORT_DRIVER_CHARACTERISTICS;
    if (MinorVersion >= 80)
    {
        Characteristics->Header.Revision = NDIS_MINIPORT_DRIVER_CHARACTERISTICS_REVISION_3;
        Characteristics->Header.Size = NDIS_SIZEOF_MINIPORT_DRIVER_CHARACTERISTICS_REVISION_3;
    }
    else
    {
        Characteristics->Header.Revision = NDIS_MINIPORT_DRIVER_CHARACTERISTICS_REVISION_2;
        Characteristics->Header.Size = NDIS_SIZEOF_MINIPORT_DRIVER_CHARACTERISTICS_REVISION_2;
    }
    Characteristics->MajorNdisVersion = 6;
    Characteristics->MinorNdisVersion = MinorVersion;
    Characteristics->InitializeHandlerEx = NDIS_TEST_HANDLER(MINIPORT_INITIALIZE);
    Characteristics->HaltHandlerEx = NDIS_TEST_HANDLER(MINIPORT_HALT);
    Characteristics->UnloadHandler = NDIS_TEST_HANDLER(MINIPORT_UNLOAD);
    Characteristics->PauseHandler = NDIS_TEST_HANDLER(MINIPORT_PAUSE);
    Characteristics->RestartHandler = NDIS_TEST_HANDLER(MINIPORT_RESTART);
    Characteristics->OidRequestHandler = NDIS_TEST_HANDLER(MINIPORT_OID_REQUEST);
    Characteristics->SendNetBufferListsHandler = NDIS_TEST_HANDLER(MINIPORT_SEND_NET_BUFFER_LISTS);
    Characteristics->ReturnNetBufferListsHandler = NDIS_TEST_HANDLER(MINIPORT_RETURN_NET_BUFFER_LISTS);
    Characteristics->CancelSendHandler = NDIS_TEST_HANDLER(MINIPORT_CANCEL_SEND);
    Characteristics->DevicePnPEventNotifyHandler = NDIS_TEST_HANDLER(MINIPORT_DEVICE_PNP_EVENT_NOTIFY);
    Characteristics->ShutdownHandlerEx = NDIS_TEST_HANDLER(MINIPORT_SHUTDOWN);
    Characteristics->CancelOidRequestHandler = NDIS_TEST_HANDLER(MINIPORT_CANCEL_OID_REQUEST);
}

static
VOID
NdisTestRegister(
    _In_ PCSTR What,
    _In_ PNDIS_MINIPORT_DRIVER_CHARACTERISTICS Characteristics,
    _In_ NDIS_STATUS Expected)
{
    static UNICODE_STRING RegistryPath = RTL_CONSTANT_STRING(L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\NdisCharacteristicsTest");
    PNDIS_TEST_DRIVER Driver;
    NDIS_HANDLE Handle = (NDIS_HANDLE)(ULONG_PTR)1;
    NDIS_STATUS Status;

    Driver = ExAllocatePoolZero(NonPagedPool, sizeof(*Driver), 'tsdN');
    if (!skip(Driver != NULL, "no memory for the test driver object\n"))
    {
        Driver->DriverObject.Type = IO_TYPE_DRIVER;
        Driver->DriverObject.Size = sizeof(Driver->DriverObject);
        Driver->DriverObject.DriverExtension = &Driver->DriverExtension;
        Driver->DriverExtension.DriverObject = &Driver->DriverObject;
        Status = NdisMRegisterMiniportDriver(&Driver->DriverObject, &RegistryPath, NULL, Characteristics, &Handle);
        ok(Status == Expected, "%s returned 0x%08lx, expected 0x%08lx\n", What, Status, Expected);
        if (Status == NDIS_STATUS_SUCCESS)
            NdisMDeregisterMiniportDriver(Handle);
        else
            ok(Handle == NULL, "%s left handle %p\n", What, Handle);
        ExFreePoolWithTag(Driver, 'tsdN');
    }
}

static
VOID
NdisTestHeaderAndVersion(VOID)
{
    NDIS_MINIPORT_DRIVER_CHARACTERISTICS Characteristics;

    NdisTestInitializeCharacteristics(&Characteristics, 20);
    NdisTestRegister("an NDIS 6.20 miniport", &Characteristics, NDIS_STATUS_SUCCESS);

    NdisTestInitializeCharacteristics(&Characteristics, 0);
    Characteristics.Header.Type = 0;
    Characteristics.Header.Revision = 0;
    Characteristics.Header.Size = NDIS_SIZEOF_MINIPORT_DRIVER_CHARACTERISTICS_REVISION_1;
    NdisTestRegister("an NDIS 6.0 miniport without a typed header", &Characteristics, NDIS_STATUS_SUCCESS);
    Characteristics.Header.Size = NDIS_SIZEOF_MINIPORT_DRIVER_CHARACTERISTICS_REVISION_1 - 1;
    NdisTestRegister("an NDIS 6.0 miniport with a short header", &Characteristics, NDIS_STATUS_BAD_CHARACTERISTICS);

    NdisTestInitializeCharacteristics(&Characteristics, 20);
    Characteristics.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    NdisTestRegister("an NDIS 6.20 miniport with a default header type", &Characteristics, NDIS_STATUS_BAD_CHARACTERISTICS);

    NdisTestInitializeCharacteristics(&Characteristics, 20);
    Characteristics.Header.Revision = NDIS_MINIPORT_DRIVER_CHARACTERISTICS_REVISION_1;
    NdisTestRegister("an NDIS 6.20 miniport with a revision 1 header", &Characteristics, NDIS_STATUS_BAD_CHARACTERISTICS);

    NdisTestInitializeCharacteristics(&Characteristics, 20);
    Characteristics.Header.Revision = NDIS_MINIPORT_DRIVER_CHARACTERISTICS_REVISION_3 + 1;
    Characteristics.Header.Size = sizeof(Characteristics);
    NdisTestRegister("an NDIS 6.20 miniport with a newer header revision", &Characteristics, NDIS_STATUS_SUCCESS);

    NdisTestInitializeCharacteristics(&Characteristics, 80);
    Characteristics.Header.Size = NDIS_SIZEOF_MINIPORT_DRIVER_CHARACTERISTICS_REVISION_2;
    NdisTestRegister("an NDIS 6.80 miniport with a revision 2 size", &Characteristics, NDIS_STATUS_BAD_CHARACTERISTICS);

    NdisTestInitializeCharacteristics(&Characteristics, 20);
    Characteristics.MajorNdisVersion = 5;
    NdisTestRegister("an NDIS 5.20 miniport", &Characteristics, NDIS_STATUS_BAD_VERSION);

    NdisTestInitializeCharacteristics(&Characteristics, 2);
    NdisTestRegister("an NDIS 6.2 miniport", &Characteristics, NDIS_STATUS_BAD_VERSION);
}

static
VOID
NdisTestEntryPoints(VOID)
{
    static const struct
    {
        PCSTR Name;
        ULONG Offset;
    } Required[] =
    {
        { "InitializeHandlerEx", FIELD_OFFSET(NDIS_MINIPORT_DRIVER_CHARACTERISTICS, InitializeHandlerEx) },
        { "HaltHandlerEx", FIELD_OFFSET(NDIS_MINIPORT_DRIVER_CHARACTERISTICS, HaltHandlerEx) },
        { "PauseHandler", FIELD_OFFSET(NDIS_MINIPORT_DRIVER_CHARACTERISTICS, PauseHandler) },
        { "RestartHandler", FIELD_OFFSET(NDIS_MINIPORT_DRIVER_CHARACTERISTICS, RestartHandler) },
        { "SendNetBufferListsHandler", FIELD_OFFSET(NDIS_MINIPORT_DRIVER_CHARACTERISTICS, SendNetBufferListsHandler) },
        { "ReturnNetBufferListsHandler", FIELD_OFFSET(NDIS_MINIPORT_DRIVER_CHARACTERISTICS, ReturnNetBufferListsHandler) },
        { "CancelSendHandler", FIELD_OFFSET(NDIS_MINIPORT_DRIVER_CHARACTERISTICS, CancelSendHandler) },
        { "DevicePnPEventNotifyHandler", FIELD_OFFSET(NDIS_MINIPORT_DRIVER_CHARACTERISTICS, DevicePnPEventNotifyHandler) },
        { "ShutdownHandlerEx", FIELD_OFFSET(NDIS_MINIPORT_DRIVER_CHARACTERISTICS, ShutdownHandlerEx) },
        { "CancelOidRequestHandler", FIELD_OFFSET(NDIS_MINIPORT_DRIVER_CHARACTERISTICS, CancelOidRequestHandler) },
        { "UnloadHandler", FIELD_OFFSET(NDIS_MINIPORT_DRIVER_CHARACTERISTICS, UnloadHandler) },
    };
    NDIS_MINIPORT_DRIVER_CHARACTERISTICS Characteristics;
    CHAR What[96];
    ULONG Index;

    for (Index = 0; Index < RTL_NUMBER_OF(Required); ++Index)
    {
        NdisTestInitializeCharacteristics(&Characteristics, 20);
        *(PVOID *)((PUCHAR)&Characteristics + Required[Index].Offset) = NULL;
        RtlStringCbPrintfA(What, sizeof(What), "a miniport without %s", Required[Index].Name);
        NdisTestRegister(What, &Characteristics, NDIS_STATUS_BAD_CHARACTERISTICS);
    }

    NdisTestInitializeCharacteristics(&Characteristics, 20);
    Characteristics.UnloadHandler = NULL;
    Characteristics.Flags = NDIS_NETCX_DRIVER;
    NdisTestRegister("a NetAdapterCx miniport without UnloadHandler", &Characteristics, NDIS_STATUS_SUCCESS);

    NdisTestInitializeCharacteristics(&Characteristics, 20);
    Characteristics.DirectOidRequestHandler = NDIS_TEST_HANDLER(MINIPORT_DIRECT_OID_REQUEST);
    NdisTestRegister("an NDIS 6.20 miniport with a direct OID handler alone", &Characteristics, NDIS_STATUS_BAD_CHARACTERISTICS);
    Characteristics.CancelDirectOidRequestHandler = NDIS_TEST_HANDLER(MINIPORT_CANCEL_DIRECT_OID_REQUEST);
    NdisTestRegister("an NDIS 6.20 miniport with both direct OID handlers", &Characteristics, NDIS_STATUS_SUCCESS);

    NdisTestInitializeCharacteristics(&Characteristics, 0);
    Characteristics.Header.Revision = NDIS_MINIPORT_DRIVER_CHARACTERISTICS_REVISION_1;
    Characteristics.Header.Size = NDIS_SIZEOF_MINIPORT_DRIVER_CHARACTERISTICS_REVISION_1;
    Characteristics.CancelDirectOidRequestHandler = NDIS_TEST_HANDLER(MINIPORT_CANCEL_DIRECT_OID_REQUEST);
    NdisTestRegister("an NDIS 6.0 miniport with a cancel direct OID handler alone", &Characteristics, NDIS_STATUS_SUCCESS);
}

START_TEST(NdisMiniportCharacteristics)
{
    NdisTestHeaderAndVersion();
    NdisTestEntryPoints();
}
