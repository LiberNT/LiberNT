/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Every PCI function in configuration space has a PnP device, whatever its device number
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <ntstrsafe.h>

#define NDEBUG
#include <debug.h>

#define PCI_ENUM_ROOT L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Enum\\PCI"
#define PCI_SCAN_MAX_DEPTH 16

typedef struct _PCI_SCAN_CONTEXT
{
    BOOLEAN SeenBus[PCI_MAX_BRIDGE_NUMBER + 1];
    ULONG Functions;
    ULONG FunctionsAfterDeviceZeroGap;
} PCI_SCAN_CONTEXT, *PPCI_SCAN_CONTEXT;

static
HANDLE
OpenKey(
    _In_opt_ HANDLE Parent,
    _In_ PUNICODE_STRING Name)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    HANDLE Key = NULL;

    InitializeObjectAttributes(&ObjectAttributes, Name, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, Parent, NULL);
    if (!NT_SUCCESS(ZwOpenKey(&Key, KEY_READ, &ObjectAttributes)))
        return NULL;
    return Key;
}

static
BOOLEAN
InstanceHasAddress(
    _In_ HANDLE DeviceKey,
    _In_ ULONG Address)
{
    static const UNICODE_STRING ValueName = RTL_CONSTANT_STRING(L"Address");
    UCHAR Buffer[sizeof(KEY_BASIC_INFORMATION) + 256 * sizeof(WCHAR)];
    PKEY_BASIC_INFORMATION Basic = (PKEY_BASIC_INFORMATION)Buffer;
    UCHAR ValueBuffer[sizeof(KEY_VALUE_PARTIAL_INFORMATION) + sizeof(ULONG)];
    PKEY_VALUE_PARTIAL_INFORMATION Value = (PKEY_VALUE_PARTIAL_INFORMATION)ValueBuffer;
    ULONG Index;
    ULONG Length;

    for (Index = 0; ; Index++)
    {
        UNICODE_STRING Name;
        HANDLE Instance;
        BOOLEAN Match = FALSE;

        if (!NT_SUCCESS(ZwEnumerateKey(DeviceKey, Index, KeyBasicInformation, Basic, sizeof(Buffer) - sizeof(WCHAR), &Length)))
            return FALSE;

        Name.Buffer = Basic->Name;
        Name.Length = Name.MaximumLength = (USHORT)Basic->NameLength;
        Instance = OpenKey(DeviceKey, &Name);
        if (!Instance)
            continue;

        if (NT_SUCCESS(ZwQueryValueKey(Instance, (PUNICODE_STRING)&ValueName, KeyValuePartialInformation,
                                       Value, sizeof(ValueBuffer), &Length)) &&
            Value->DataLength == sizeof(ULONG) &&
            *(PULONG)Value->Data == Address)
        {
            Match = TRUE;
        }
        ZwClose(Instance);
        if (Match)
            return TRUE;
    }
}

static
BOOLEAN
FunctionHasDevice(
    _In_ USHORT VendorId,
    _In_ USHORT DeviceId,
    _In_ ULONG Address)
{
    UNICODE_STRING RootName = RTL_CONSTANT_STRING(PCI_ENUM_ROOT);
    UCHAR Buffer[sizeof(KEY_BASIC_INFORMATION) + 256 * sizeof(WCHAR)];
    PKEY_BASIC_INFORMATION Basic = (PKEY_BASIC_INFORMATION)Buffer;
    WCHAR Prefix[32];
    SIZE_T PrefixLength;
    HANDLE Root;
    ULONG Index;
    ULONG Length;
    BOOLEAN Found = FALSE;

    Root = OpenKey(NULL, &RootName);
    if (!Root)
        return FALSE;

    RtlStringCbPrintfW(Prefix, sizeof(Prefix), L"VEN_%04X&DEV_%04X&", VendorId, DeviceId);
    PrefixLength = wcslen(Prefix) * sizeof(WCHAR);

    for (Index = 0; !Found; Index++)
    {
        UNICODE_STRING Name;
        HANDLE DeviceKey;

        if (!NT_SUCCESS(ZwEnumerateKey(Root, Index, KeyBasicInformation, Basic, sizeof(Buffer) - sizeof(WCHAR), &Length)))
            break;
        if (Basic->NameLength < PrefixLength ||
            _wcsnicmp(Basic->Name, Prefix, PrefixLength / sizeof(WCHAR)) != 0)
        {
            continue;
        }

        Name.Buffer = Basic->Name;
        Name.Length = Name.MaximumLength = (USHORT)Basic->NameLength;
        DeviceKey = OpenKey(Root, &Name);
        if (DeviceKey)
        {
            Found = InstanceHasAddress(DeviceKey, Address);
            ZwClose(DeviceKey);
        }
    }

    ZwClose(Root);
    return Found;
}

static
VOID
ScanBus(
    _Inout_ PPCI_SCAN_CONTEXT Context,
    _In_ ULONG Bus,
    _In_ ULONG Depth)
{
    PCI_COMMON_HEADER Header;
    PCI_SLOT_NUMBER Slot;
    ULONG Device;
    ULONG Function;
    BOOLEAN DeviceZeroPresent = FALSE;

    if (Bus > PCI_MAX_BRIDGE_NUMBER || Context->SeenBus[Bus] || Depth > PCI_SCAN_MAX_DEPTH)
        return;
    Context->SeenBus[Bus] = TRUE;

    Slot.u.AsULONG = 0;
    for (Device = 0; Device < PCI_MAX_DEVICES; Device++)
    {
        Slot.u.bits.DeviceNumber = Device;
        for (Function = 0; Function < PCI_MAX_FUNCTION; Function++)
        {
            Slot.u.bits.FunctionNumber = Function;
            RtlZeroMemory(&Header, sizeof(Header));
            HalGetBusDataByOffset(PCIConfiguration, Bus, Slot.u.AsULONG, &Header, 0, sizeof(Header));
            if (Header.VendorID == PCI_INVALID_VENDORID || Header.VendorID == 0)
            {
                if (Function == 0)
                    break;
                continue;
            }

            if (Device == 0)
                DeviceZeroPresent = TRUE;
            Context->Functions++;
            if (Bus != 0 && !DeviceZeroPresent)
                Context->FunctionsAfterDeviceZeroGap++;

            ok(FunctionHasDevice(Header.VendorID, Header.DeviceID, (Device << 16) | Function),
               "%02lx:%02lx.%lx %04x:%04x has no PnP device\n",
               Bus, Device, Function, Header.VendorID, Header.DeviceID);

            if (PCI_CONFIGURATION_TYPE(&Header) == PCI_BRIDGE_TYPE &&
                Header.u.type1.SecondaryBus > Bus)
            {
                ScanBus(Context, Header.u.type1.SecondaryBus, Depth + 1);
            }

            if (Function == 0 && !PCI_MULTIFUNCTION_DEVICE(&Header))
                break;
        }
    }
}

START_TEST(IoPciEnumeration)
{
    PPCI_SCAN_CONTEXT Context;

    Context = ExAllocatePoolZero(NonPagedPool, sizeof(*Context), 'tPmK');
    if (!Context)
    {
        skip(FALSE, "no memory for the scan context\n");
        return;
    }

    ScanBus(Context, 0, 0);
    ok(Context->Functions != 0, "no PCI function found on bus 0\n");
    trace("%lu PCI functions, %lu on a secondary bus without a device 0\n",
          Context->Functions, Context->FunctionsAfterDeviceZeroGap);

    ExFreePoolWithTag(Context, 'tPmK');
}
