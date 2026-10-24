/*
 * PROJECT:     LiberNT Linux Subsystem
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     PCI and USB devices in sysfs and /dev/bus/usb
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "lxcore.h"
#include <initguid.h>
#include <usbioctl.h>
#include <stdarg.h>

#define NDEBUG
#include <debug.h>

#define LX_SYSDEV_LIFETIME          (2 * 10000000LL)
#define LX_SYSDEV_PCI_CONFIG        256
#define LX_SYSDEV_PCI_PUBLIC        64
#define LX_SYSDEV_PCI_RESOURCES     13
#define LX_SYSDEV_PCI_BRIDGE_LINES  17
#define LX_SYSDEV_MAX_DRIVERS       64
#define LX_SYSDEV_MAX_RANGES        16
#define LX_SYSDEV_MAX_DEPTH         7
#define LX_SYSDEV_MAX_PORTS         31
#define LX_SYSDEV_MAX_INTERFACES    32
#define LX_SYSDEV_MAX_USB_DRIVERS   128

#define LX_IORESOURCE_IO            0x00000100
#define LX_IORESOURCE_MEM           0x00000200
#define LX_IORESOURCE_PREFETCH      0x00002000
#define LX_IORESOURCE_SIZEALIGN     0x00040000
#define LX_IORESOURCE_MEM_64        0x00100000

typedef struct _LX_SYSDEV_NODE
{
    PSTR Path;
    ULONG Type;
    ULONG Mode;
    PUCHAR Data;
    ULONG Length;
    ULONG Public;
} LX_SYSDEV_NODE, *PLX_SYSDEV_NODE;

typedef struct _LX_SYSDEV_USB
{
    ULONG Bus;
    ULONG Devnum;
    ULONG Port;
    UCHAR Speed;
    BOOLEAN RootHub;
    PWSTR ParentHub;
    PWSTR OwnHub;
    PUCHAR Descriptors;
    ULONG DescriptorLength;
    PSTR Strings[4];
    PSTR Drivers[LX_SYSDEV_MAX_INTERFACES];
} LX_SYSDEV_USB, *PLX_SYSDEV_USB;

typedef struct _LX_SYSDEV_USB_DRIVER
{
    WCHAR DriverKey[96];
    WCHAR Hardware[48];
    CHAR Service[32];
} LX_SYSDEV_USB_DRIVER, *PLX_SYSDEV_USB_DRIVER;

typedef struct _LX_SYSDEV_TREE
{
    volatile LONG References;
    LONG64 Built;
    ULONG Count;
    ULONG Capacity;
    PLX_SYSDEV_NODE Nodes;
    ULONG UsbCount;
    ULONG UsbCapacity;
    PLX_SYSDEV_USB Usb;
    ULONG UsbDriverCount;
    PLX_SYSDEV_USB_DRIVER UsbDrivers;
    ULONG NextBus;
    BOOLEAN Failed;
} LX_SYSDEV_TREE, *PLX_SYSDEV_TREE;

typedef struct _LX_SYSDEV_RANGE
{
    ULONG64 Start;
    ULONG64 Length;
    BOOLEAN Port;
} LX_SYSDEV_RANGE;

typedef struct _LX_SYSDEV_DRIVER
{
    ULONG Bus;
    ULONG Device;
    ULONG Function;
    USHORT VendorId;
    USHORT DeviceId;
    CHAR Service[64];
    ULONG RangeCount;
    LX_SYSDEV_RANGE Ranges[LX_SYSDEV_MAX_RANGES];
} LX_SYSDEV_DRIVER, *PLX_SYSDEV_DRIVER;

typedef struct _LX_SYSDEV_PCI
{
    ULONG DriverCount;
    LX_SYSDEV_DRIVER Drivers[LX_SYSDEV_MAX_DRIVERS];
} LX_SYSDEV_PCI, *PLX_SYSDEV_PCI;

typedef struct _LX_SYSDEV_HC
{
    PCSTR DriverName;
    PCSTR Product;
    PCSTR Serial;
    ULONG ProgIf;
} LX_SYSDEV_HC, *PLX_SYSDEV_HC;

typedef struct _LX_SYSDEV_BUS
{
    ULONG Number;
    ULONG NextDevnum;
    USHORT BcdUsb;
    PCSTR Path;
} LX_SYSDEV_BUS, *PLX_SYSDEV_BUS;

static FAST_MUTEX LxSysDevLock;
static PLX_SYSDEV_TREE LxSysDevCurrent;

static
int
LxSysDevCompare(
    _In_ PCSTR Left,
    _In_ PCSTR Right)
{
    for (;; Left++, Right++)
    {
        UCHAR A = (*Left == '/') ? 1 : (UCHAR)*Left;
        UCHAR B = (*Right == '/') ? 1 : (UCHAR)*Right;

        if (A != B)
            return (A < B) ? -1 : 1;
        if (A == 0)
            return 0;
    }
}

static
VOID
LxSysDevAdd(
    _Inout_ PLX_SYSDEV_TREE Tree,
    _In_ ULONG Type,
    _In_ ULONG Mode,
    _In_ PCSTR Path,
    _In_reads_bytes_opt_(Length) const VOID *Data,
    _In_ ULONG Length,
    _In_ ULONG Public)
{
    PLX_SYSDEV_NODE Node;

    if (Tree->Failed || (Path == NULL))
    {
        Tree->Failed = TRUE;
        return;
    }
    if (Tree->Count == Tree->Capacity)
    {
        ULONG Capacity = Tree->Capacity ? Tree->Capacity * 2 : 512;
        PLX_SYSDEV_NODE Nodes = LxAlloc(Capacity * sizeof(*Nodes));

        if (Nodes == NULL)
        {
            Tree->Failed = TRUE;
            return;
        }
        if (Tree->Nodes != NULL)
        {
            RtlCopyMemory(Nodes, Tree->Nodes, Tree->Count * sizeof(*Nodes));
            LxFree(Tree->Nodes);
        }
        Tree->Nodes = Nodes;
        Tree->Capacity = Capacity;
    }
    Node = &Tree->Nodes[Tree->Count];
    RtlZeroMemory(Node, sizeof(*Node));
    Node->Path = LxStrDup(Path);
    Node->Type = Type;
    Node->Mode = Mode;
    Node->Length = Length;
    Node->Public = Public;
    Node->Data = LxAlloc(Length + 1);
    if ((Node->Path == NULL) || (Node->Data == NULL))
    {
        LxFree(Node->Path);
        LxFree(Node->Data);
        Tree->Failed = TRUE;
        return;
    }
    if (Length != 0)
        RtlCopyMemory(Node->Data, Data, Length);
    Node->Data[Length] = 0;
    Tree->Count++;
}

static
VOID
LxSysDevDirectory(
    _Inout_ PLX_SYSDEV_TREE Tree,
    _In_ PCSTR Path)
{
    LxSysDevAdd(Tree, LX_S_IFDIR, 0755, Path, NULL, 0, MAXULONG);
}

static
VOID
LxSysDevBinary(
    _Inout_ PLX_SYSDEV_TREE Tree,
    _In_ PCSTR Directory,
    _In_ PCSTR Name,
    _In_ ULONG Mode,
    _In_reads_bytes_(Length) const VOID *Data,
    _In_ ULONG Length,
    _In_ ULONG Public)
{
    PSTR Path = LxStrPrintf("%s/%s", Directory, Name);

    LxSysDevAdd(Tree, LX_S_IFREG, Mode, Path, Data, Length, Public);
    LxFree(Path);
}

static
VOID
LxSysDevText(
    _Inout_ PLX_SYSDEV_TREE Tree,
    _In_ PCSTR Directory,
    _In_ PCSTR Name,
    _In_ PCSTR Format,
    ...)
{
    CHAR Text[512];
    va_list Arguments;

    va_start(Arguments, Format);
    if (!NT_SUCCESS(RtlStringCbVPrintfA(Text, sizeof(Text), Format, Arguments)))
        Tree->Failed = TRUE;
    va_end(Arguments);
    LxSysDevBinary(Tree, Directory, Name, 0444, Text, (ULONG)strlen(Text), MAXULONG);
}

static
VOID
LxSysDevLink(
    _Inout_ PLX_SYSDEV_TREE Tree,
    _In_ PCSTR Path,
    _In_ PCSTR Target)
{
    CHAR Relative[512] = "";
    PCSTR Slash;

    for (Slash = strchr(Path, '/'); Slash != NULL; Slash = strchr(Slash + 1, '/'))
        RtlStringCbCatA(Relative, sizeof(Relative), "../");
    if (!NT_SUCCESS(RtlStringCbCatA(Relative, sizeof(Relative), Target)))
    {
        Tree->Failed = TRUE;
        return;
    }
    LxSysDevAdd(Tree, LX_S_IFLNK, 0777, Path, Relative, (ULONG)strlen(Relative), MAXULONG);
}

static
VOID
LxSysDevLinkIn(
    _Inout_ PLX_SYSDEV_TREE Tree,
    _In_ PCSTR Directory,
    _In_ PCSTR Name,
    _In_ PCSTR Target)
{
    PSTR Path = LxStrPrintf("%s/%s", Directory, Name);

    if (Path == NULL)
    {
        Tree->Failed = TRUE;
        return;
    }
    LxSysDevLink(Tree, Path, Target);
    LxFree(Path);
}

static
VOID
LxSysDevFreeUsb(
    _Inout_ PLX_SYSDEV_USB Usb)
{
    ULONG Index;

    LxFree(Usb->ParentHub);
    LxFree(Usb->OwnHub);
    LxFree(Usb->Descriptors);
    for (Index = 0; Index < RTL_NUMBER_OF(Usb->Strings); Index++)
        LxFree(Usb->Strings[Index]);
    for (Index = 0; Index < LX_SYSDEV_MAX_INTERFACES; Index++)
        LxFree(Usb->Drivers[Index]);
    RtlZeroMemory(Usb, sizeof(*Usb));
}

static
VOID
LxSysDevFree(
    _In_ PLX_SYSDEV_TREE Tree)
{
    ULONG Index;

    for (Index = 0; Index < Tree->Count; Index++)
    {
        LxFree(Tree->Nodes[Index].Path);
        LxFree(Tree->Nodes[Index].Data);
    }
    for (Index = 0; Index < Tree->UsbCount; Index++)
        LxSysDevFreeUsb(&Tree->Usb[Index]);
    LxFree(Tree->Usb);
    if (Tree->UsbDrivers != NULL)
        LxFreePaged(Tree->UsbDrivers);
    LxFree(Tree->Nodes);
    LxFree(Tree);
}

static
PWSTR
LxSysDevWideDup(
    _In_opt_ PCWSTR Text)
{
    SIZE_T Bytes;
    PWSTR Copy;

    if (Text == NULL)
        return NULL;
    Bytes = (wcslen(Text) + 1) * sizeof(WCHAR);
    Copy = LxAlloc(Bytes);
    if (Copy != NULL)
        RtlCopyMemory(Copy, Text, Bytes);
    return Copy;
}

static
BOOLEAN
LxSysDevCopyUsb(
    _Out_ PLX_SYSDEV_USB Usb,
    _In_ PLX_SYSDEV_USB Source)
{
    ULONG Index;

    RtlZeroMemory(Usb, sizeof(*Usb));
    Usb->Bus = Source->Bus;
    Usb->Devnum = Source->Devnum;
    Usb->Port = Source->Port;
    Usb->Speed = Source->Speed;
    Usb->RootHub = Source->RootHub;
    Usb->ParentHub = LxSysDevWideDup(Source->ParentHub);
    Usb->OwnHub = LxSysDevWideDup(Source->OwnHub);
    Usb->DescriptorLength = Source->DescriptorLength;
    Usb->Descriptors = LxAlloc(Source->DescriptorLength + 1);
    if (Usb->Descriptors != NULL)
        RtlCopyMemory(Usb->Descriptors, Source->Descriptors, Source->DescriptorLength);
    for (Index = 0; Index < RTL_NUMBER_OF(Usb->Strings); Index++)
    {
        if (Source->Strings[Index] != NULL)
        {
            Usb->Strings[Index] = LxStrDup(Source->Strings[Index]);
            if (Usb->Strings[Index] == NULL)
                return FALSE;
        }
    }
    for (Index = 0; Index < LX_SYSDEV_MAX_INTERFACES; Index++)
    {
        if (Source->Drivers[Index] != NULL)
        {
            Usb->Drivers[Index] = LxStrDup(Source->Drivers[Index]);
            if (Usb->Drivers[Index] == NULL)
                return FALSE;
        }
    }
    return (Usb->Descriptors != NULL) && ((Source->ParentHub == NULL) || (Usb->ParentHub != NULL)) &&
           ((Source->OwnHub == NULL) || (Usb->OwnHub != NULL));
}

static
BOOLEAN
LxSysDevAddUsb(
    _Inout_ PLX_SYSDEV_TREE Tree,
    _Inout_ PLX_SYSDEV_USB Source,
    _In_reads_bytes_(Length) PUCHAR Descriptors,
    _In_ ULONG Length)
{
    PLX_SYSDEV_USB Usb;
    BOOLEAN Result;

    if (Tree->UsbCount == Tree->UsbCapacity)
    {
        ULONG Capacity = Tree->UsbCapacity ? Tree->UsbCapacity * 2 : 16;
        PLX_SYSDEV_USB Grown = LxAlloc(Capacity * sizeof(*Grown));

        if (Grown == NULL)
            return FALSE;
        RtlZeroMemory(Grown, Capacity * sizeof(*Grown));
        if (Tree->Usb != NULL)
        {
            RtlCopyMemory(Grown, Tree->Usb, Tree->UsbCount * sizeof(*Grown));
            LxFree(Tree->Usb);
        }
        Tree->Usb = Grown;
        Tree->UsbCapacity = Capacity;
    }
    Usb = &Tree->Usb[Tree->UsbCount++];
    Source->Descriptors = Descriptors;
    Source->DescriptorLength = Length;
    Result = LxSysDevCopyUsb(Usb, Source);
    Source->Descriptors = NULL;
    Source->DescriptorLength = 0;
    return Result;
}

static
VOID
LxSysDevSort(
    _Inout_ PLX_SYSDEV_TREE Tree)
{
    PLX_SYSDEV_NODE Scratch;
    ULONG Width, Left, Index;

    if (Tree->Count < 2)
        return;
    Scratch = LxAlloc(Tree->Count * sizeof(*Scratch));
    if (Scratch == NULL)
    {
        Tree->Failed = TRUE;
        return;
    }
    for (Width = 1; Width < Tree->Count; Width *= 2)
    {
        for (Left = 0; Left < Tree->Count; Left += 2 * Width)
        {
            ULONG Middle = min(Left + Width, Tree->Count);
            ULONG Right = min(Left + 2 * Width, Tree->Count);
            ULONG A = Left, B = Middle;

            for (Index = Left; Index < Right; Index++)
            {
                if ((A < Middle) && ((B >= Right) || (LxSysDevCompare(Tree->Nodes[A].Path, Tree->Nodes[B].Path) <= 0)))
                    Scratch[Index] = Tree->Nodes[A++];
                else
                    Scratch[Index] = Tree->Nodes[B++];
            }
        }
        RtlCopyMemory(Tree->Nodes, Scratch, Tree->Count * sizeof(*Scratch));
    }
    LxFree(Scratch);
}

static
ULONG
LxSysDevLowerBound(
    _In_ PLX_SYSDEV_TREE Tree,
    _In_ PCSTR Path)
{
    ULONG Low = 0, High = Tree->Count;

    while (Low < High)
    {
        ULONG Middle = Low + (High - Low) / 2;

        if (LxSysDevCompare(Tree->Nodes[Middle].Path, Path) < 0)
            Low = Middle + 1;
        else
            High = Middle;
    }
    return Low;
}

static
BOOLEAN
LxSysDevIsBelow(
    _In_ PCSTR Path,
    _In_ PCSTR Directory,
    _In_ SIZE_T Length)
{
    return (strncmp(Path, Directory, Length) == 0) && (Path[Length] == '/');
}

static
VOID
LxSysDevHex(
    _In_ PCWSTR Text,
    _In_ ULONG Digits,
    _Out_ PUSHORT Value)
{
    ULONG Index, Result = 0;

    for (Index = 0; Index < Digits; Index++)
    {
        WCHAR Char = Text[Index];

        Result <<= 4;
        if ((Char >= L'0') && (Char <= L'9'))
            Result |= Char - L'0';
        else if ((Char >= L'A') && (Char <= L'F'))
            Result |= Char - L'A' + 10;
        else if ((Char >= L'a') && (Char <= L'f'))
            Result |= Char - L'a' + 10;
        else
            Result = 0;
    }
    *Value = (USHORT)Result;
}

static
BOOLEAN
LxSysDevParseLocation(
    _In_reads_(Length) PCWSTR Text,
    _In_ ULONG Length,
    _Out_writes_(3) PULONG Numbers)
{
    ULONG Index, Found = 0;
    BOOLEAN Digit = FALSE;

    for (Index = 0; (Index < Length) && (Text[Index] != UNICODE_NULL) && (Found < 3); Index++)
    {
        if ((Text[Index] >= L'0') && (Text[Index] <= L'9'))
        {
            if (!Digit)
                Numbers[Found] = 0;
            Numbers[Found] = Numbers[Found] * 10 + (Text[Index] - L'0');
            Digit = TRUE;
        }
        else if (Digit)
        {
            Digit = FALSE;
            Found++;
        }
    }
    if (Digit)
        Found++;
    return Found >= 3;
}

static
PKEY_VALUE_PARTIAL_INFORMATION
LxSysDevQueryValue(
    _In_ HANDLE Key,
    _In_ PCWSTR Name)
{
    UNICODE_STRING ValueName;
    PKEY_VALUE_PARTIAL_INFORMATION Value;
    ULONG Length = 0;
    NTSTATUS Status;

    RtlInitUnicodeString(&ValueName, Name);
    Status = ZwQueryValueKey(Key, &ValueName, KeyValuePartialInformation, NULL, 0, &Length);
    if ((Status != STATUS_BUFFER_TOO_SMALL) && (Status != STATUS_BUFFER_OVERFLOW))
        return NULL;
    Value = LxAllocPaged(Length + sizeof(WCHAR));
    if (Value == NULL)
        return NULL;
    Status = ZwQueryValueKey(Key, &ValueName, KeyValuePartialInformation, Value, Length, &Length);
    if (!NT_SUCCESS(Status))
    {
        LxFreePaged(Value);
        return NULL;
    }
    return Value;
}

static
HANDLE
LxSysDevOpenKey(
    _In_opt_ HANDLE Root,
    _In_ PCWSTR Name,
    _In_ USHORT NameLength)
{
    UNICODE_STRING KeyName;
    OBJECT_ATTRIBUTES Attributes;
    HANDLE Key;

    KeyName.Buffer = (PWSTR)Name;
    KeyName.Length = NameLength;
    KeyName.MaximumLength = NameLength;
    InitializeObjectAttributes(&Attributes, &KeyName, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, Root, NULL);
    if (!NT_SUCCESS(ZwOpenKey(&Key, KEY_READ, &Attributes)))
        return NULL;
    return Key;
}

static
VOID
LxSysDevReadRanges(
    _In_ HANDLE Instance,
    _Inout_ PLX_SYSDEV_DRIVER Driver)
{
    static const WCHAR Control[] = L"Control";
    PKEY_VALUE_PARTIAL_INFORMATION Value;
    PCM_RESOURCE_LIST List;
    PUCHAR Cursor, End;
    HANDLE Key;
    ULONG Full, Partial;

    Key = LxSysDevOpenKey(Instance, Control, sizeof(Control) - sizeof(WCHAR));
    if (Key == NULL)
        return;
    Value = LxSysDevQueryValue(Key, L"AllocConfig");
    ZwClose(Key);
    if (Value == NULL)
        return;
    if ((Value->Type != REG_RESOURCE_LIST) || (Value->DataLength < sizeof(ULONG)))
    {
        LxFreePaged(Value);
        return;
    }
    List = (PCM_RESOURCE_LIST)Value->Data;
    Cursor = (PUCHAR)List->List;
    End = Value->Data + Value->DataLength;
    for (Full = 0; Full < List->Count; Full++)
    {
        PCM_FULL_RESOURCE_DESCRIPTOR Descriptor = (PCM_FULL_RESOURCE_DESCRIPTOR)Cursor;
        PCM_PARTIAL_RESOURCE_DESCRIPTOR Item;

        if (Cursor + FIELD_OFFSET(CM_FULL_RESOURCE_DESCRIPTOR, PartialResourceList.PartialDescriptors) > End)
            break;
        Item = Descriptor->PartialResourceList.PartialDescriptors;
        for (Partial = 0; Partial < Descriptor->PartialResourceList.Count; Partial++)
        {
            if ((PUCHAR)(Item + 1) > End)
                break;
            if (((Item->Type == CmResourceTypeMemory) || (Item->Type == CmResourceTypeMemoryLarge) ||
                 (Item->Type == CmResourceTypePort)) && (Driver->RangeCount < LX_SYSDEV_MAX_RANGES))
            {
                PHYSICAL_ADDRESS Start;
                ULONGLONG Length = RtlCmDecodeMemIoResource(Item, (PULONGLONG)&Start.QuadPart);

                Driver->Ranges[Driver->RangeCount].Start = Start.QuadPart;
                Driver->Ranges[Driver->RangeCount].Length = Length;
                Driver->Ranges[Driver->RangeCount].Port = (Item->Type == CmResourceTypePort);
                Driver->RangeCount++;
            }
            if (Item->Type == CmResourceTypeDeviceSpecific)
                Item = (PCM_PARTIAL_RESOURCE_DESCRIPTOR)((PUCHAR)(Item + 1) + Item->u.DeviceSpecificData.DataSize);
            else
                Item++;
        }
        Cursor = (PUCHAR)Item;
    }
    LxFreePaged(Value);
}

static
VOID
LxSysDevReadDrivers(
    _Out_ PLX_SYSDEV_PCI Pci)
{
    static const WCHAR EnumPci[] = L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Enum\\PCI";
    UCHAR Buffer[sizeof(KEY_BASIC_INFORMATION) + 256 * sizeof(WCHAR)];
    UCHAR InstanceBuffer[sizeof(KEY_BASIC_INFORMATION) + 256 * sizeof(WCHAR)];
    PKEY_BASIC_INFORMATION Hardware = (PKEY_BASIC_INFORMATION)Buffer;
    PKEY_BASIC_INFORMATION Instance = (PKEY_BASIC_INFORMATION)InstanceBuffer;
    HANDLE Root, HardwareKey, InstanceKey;
    ULONG HardwareIndex, InstanceIndex, Length;

    Pci->DriverCount = 0;
    Root = LxSysDevOpenKey(NULL, EnumPci, sizeof(EnumPci) - sizeof(WCHAR));
    if (Root == NULL)
        return;
    for (HardwareIndex = 0;
         NT_SUCCESS(ZwEnumerateKey(Root, HardwareIndex, KeyBasicInformation, Hardware, sizeof(Buffer) - sizeof(WCHAR), &Length));
         HardwareIndex++)
    {
        USHORT VendorId = 0, DeviceId = 0;

        if ((Hardware->NameLength >= 17 * sizeof(WCHAR)) && (_wcsnicmp(Hardware->Name, L"VEN_", 4) == 0) &&
            (_wcsnicmp(Hardware->Name + 8, L"&DEV_", 5) == 0))
        {
            LxSysDevHex(Hardware->Name + 4, 4, &VendorId);
            LxSysDevHex(Hardware->Name + 13, 4, &DeviceId);
        }
        HardwareKey = LxSysDevOpenKey(Root, Hardware->Name, (USHORT)Hardware->NameLength);
        if (HardwareKey == NULL)
            continue;
        for (InstanceIndex = 0;
             (Pci->DriverCount < LX_SYSDEV_MAX_DRIVERS) &&
             NT_SUCCESS(ZwEnumerateKey(HardwareKey, InstanceIndex, KeyBasicInformation, Instance,
                                       sizeof(InstanceBuffer) - sizeof(WCHAR), &Length));
             InstanceIndex++)
        {
            PLX_SYSDEV_DRIVER Driver = &Pci->Drivers[Pci->DriverCount];
            PKEY_VALUE_PARTIAL_INFORMATION Location, Service;
            ULONG Numbers[3];

            InstanceKey = LxSysDevOpenKey(HardwareKey, Instance->Name, (USHORT)Instance->NameLength);
            if (InstanceKey == NULL)
                continue;
            RtlZeroMemory(Driver, sizeof(*Driver));
            Location = LxSysDevQueryValue(InstanceKey, L"LocationInformation");
            Service = LxSysDevQueryValue(InstanceKey, L"Service");
            if ((Location != NULL) && (Location->Type == REG_SZ) &&
                LxSysDevParseLocation((PCWSTR)Location->Data, Location->DataLength / sizeof(WCHAR), Numbers))
            {
                Driver->Bus = Numbers[0];
                Driver->Device = Numbers[1];
                Driver->Function = Numbers[2];
                Driver->VendorId = VendorId;
                Driver->DeviceId = DeviceId;
                if ((Service != NULL) && (Service->Type == REG_SZ))
                {
                    ULONG Written = 0;

                    if (!NT_SUCCESS(RtlUnicodeToUTF8N(Driver->Service, sizeof(Driver->Service) - 1, &Written,
                                                      (PCWCH)Service->Data,
                                                      (ULONG)(wcsnlen((PCWSTR)Service->Data,
                                                                      Service->DataLength / sizeof(WCHAR)) *
                                                              sizeof(WCHAR)))))
                    {
                        Written = 0;
                    }
                    Driver->Service[min(Written, sizeof(Driver->Service) - 1)] = '\0';
                }
                LxSysDevReadRanges(InstanceKey, Driver);
                Pci->DriverCount++;
            }
            if (Location != NULL)
                LxFreePaged(Location);
            if (Service != NULL)
                LxFreePaged(Service);
            ZwClose(InstanceKey);
        }
        ZwClose(HardwareKey);
    }
    ZwClose(Root);
}

static
PLX_SYSDEV_DRIVER
LxSysDevFindDriver(
    _In_ PLX_SYSDEV_PCI Pci,
    _In_ ULONG Bus,
    _In_ ULONG Device,
    _In_ ULONG Function,
    _In_ USHORT VendorId,
    _In_ USHORT DeviceId)
{
    ULONG Index;

    for (Index = 0; Index < Pci->DriverCount; Index++)
    {
        PLX_SYSDEV_DRIVER Driver = &Pci->Drivers[Index];

        if ((Driver->Bus == Bus) && (Driver->Device == Device) && (Driver->Function == Function) &&
            (Driver->VendorId == VendorId) && (Driver->DeviceId == DeviceId))
        {
            return Driver;
        }
    }
    return NULL;
}

static
VOID
LxSysDevPciResources(
    _Inout_ PLX_SYSDEV_TREE Tree,
    _In_ PCSTR Directory,
    _In_reads_(LX_SYSDEV_PCI_CONFIG) PUCHAR Config,
    _In_opt_ PLX_SYSDEV_DRIVER Driver)
{
    ULONG Bars = ((Config[0x0E] & 0x7F) == 1) ? 2 : (((Config[0x0E] & 0x7F) == 0) ? 6 : 0);
    ULONG Lines = ((Config[0x0E] & 0x7F) == 1) ? LX_SYSDEV_PCI_BRIDGE_LINES : LX_SYSDEV_PCI_RESOURCES;
    CHAR Text[LX_SYSDEV_PCI_BRIDGE_LINES * 60];
    ULONG Index, Used = 0;

    Text[0] = '\0';
    for (Index = 0; Index < Lines; Index++)
    {
        ULONG64 Start = 0, End = 0, Flags = 0;
        BOOLEAN Wide = FALSE;

        if (Index < Bars)
        {
            ULONG Bar = *(PULONG)&Config[0x10 + Index * 4];

            if ((Bar & 1) != 0)
            {
                Start = Bar & ~3UL;
                Flags = LX_IORESOURCE_IO | LX_IORESOURCE_SIZEALIGN | (Bar & 3);
            }
            else if ((Bar & ~0xFUL) != 0 || (((Bar >> 1) & 3) == 2))
            {
                Start = Bar & ~0xFUL;
                Flags = LX_IORESOURCE_MEM | LX_IORESOURCE_SIZEALIGN | (Bar & 0xF);
                if (Bar & 8)
                    Flags |= LX_IORESOURCE_PREFETCH;
                if ((((Bar >> 1) & 3) == 2) && (Index + 1 < Bars))
                {
                    Start |= (ULONG64)*(PULONG)&Config[0x10 + (Index + 1) * 4] << 32;
                    Flags |= LX_IORESOURCE_MEM_64;
                    Wide = TRUE;
                }
            }
            if ((Start != 0) && (Driver != NULL))
            {
                ULONG Range;

                for (Range = 0; Range < Driver->RangeCount; Range++)
                {
                    if ((Driver->Ranges[Range].Start == Start) &&
                        (Driver->Ranges[Range].Port == ((Flags & LX_IORESOURCE_IO) != 0)) &&
                        (Driver->Ranges[Range].Length != 0))
                    {
                        End = Start + Driver->Ranges[Range].Length - 1;
                        break;
                    }
                }
            }
            if (End == 0)
                Start = Flags = 0;
            if (Wide)
            {
                RtlStringCbPrintfA(Text + Used, sizeof(Text) - Used, "0x%016llx 0x%016llx 0x%016llx\n", Start, End, Flags);
                Used += (ULONG)strlen(Text + Used);
                Index++;
                Start = End = Flags = 0;
                if (Index >= Lines)
                    break;
            }
        }
        RtlStringCbPrintfA(Text + Used, sizeof(Text) - Used, "0x%016llx 0x%016llx 0x%016llx\n", Start, End, Flags);
        Used += (ULONG)strlen(Text + Used);
    }
    LxSysDevBinary(Tree, Directory, "resource", 0444, Text, Used, MAXULONG);
}

static
VOID
LxSysDevPciDevice(
    _Inout_ PLX_SYSDEV_TREE Tree,
    _In_ PLX_SYSDEV_PCI Pci,
    _In_ PCSTR Directory,
    _In_ PCSTR Name,
    _In_ ULONG Bus,
    _In_ ULONG Device,
    _In_ ULONG Function,
    _In_reads_(LX_SYSDEV_PCI_CONFIG) PUCHAR Config)
{
    USHORT VendorId = *(PUSHORT)&Config[0x00], DeviceId = *(PUSHORT)&Config[0x02];
    USHORT SubVendor = 0, SubDevice = 0;
    ULONG Class = (Config[0x0B] << 16) | (Config[0x0A] << 8) | Config[0x09];
    PLX_SYSDEV_DRIVER Driver = LxSysDevFindDriver(Pci, Bus, Device, Function, VendorId, DeviceId);
    PSTR Link;

    if ((Config[0x0E] & 0x7F) == 0)
    {
        SubVendor = *(PUSHORT)&Config[0x2C];
        SubDevice = *(PUSHORT)&Config[0x2E];
    }
    LxSysDevDirectory(Tree, Directory);
    LxSysDevText(Tree, Directory, "vendor", "0x%04x\n", VendorId);
    LxSysDevText(Tree, Directory, "device", "0x%04x\n", DeviceId);
    LxSysDevText(Tree, Directory, "subsystem_vendor", "0x%04x\n", SubVendor);
    LxSysDevText(Tree, Directory, "subsystem_device", "0x%04x\n", SubDevice);
    LxSysDevText(Tree, Directory, "class", "0x%06x\n", Class);
    LxSysDevText(Tree, Directory, "revision", "0x%02x\n", Config[0x08]);
    LxSysDevText(Tree, Directory, "irq", "%u\n", (Config[0x3C] == 0xFF) ? 0 : Config[0x3C]);
    LxSysDevText(Tree, Directory, "enable", "%u\n", (Config[0x04] & 3) ? 1 : 0);
    LxSysDevText(Tree, Directory, "numa_node", "-1\n");
    LxSysDevText(Tree, Directory, "modalias", "pci:v%08Xd%08Xsv%08Xsd%08Xbc%02Xsc%02Xi%02X\n",
                 VendorId, DeviceId, SubVendor, SubDevice, Config[0x0B], Config[0x0A], Config[0x09]);
    LxSysDevText(Tree, Directory, "uevent",
                 "%s%s%sPCI_CLASS=%X\nPCI_ID=%04X:%04X\nPCI_SUBSYS_ID=%04X:%04X\nPCI_SLOT_NAME=%s\n"
                 "MODALIAS=pci:v%08Xd%08Xsv%08Xsd%08Xbc%02Xsc%02Xi%02X\n",
                 ((Driver != NULL) && (Driver->Service[0] != '\0')) ? "DRIVER=" : "",
                 ((Driver != NULL) && (Driver->Service[0] != '\0')) ? Driver->Service : "",
                 ((Driver != NULL) && (Driver->Service[0] != '\0')) ? "\n" : "",
                 Class, VendorId, DeviceId, SubVendor, SubDevice, Name,
                 VendorId, DeviceId, SubVendor, SubDevice, Config[0x0B], Config[0x0A], Config[0x09]);
    LxSysDevBinary(Tree, Directory, "config", 0444, Config, LX_SYSDEV_PCI_CONFIG, LX_SYSDEV_PCI_PUBLIC);
    LxSysDevPciResources(Tree, Directory, Config, Driver);
    LxSysDevLinkIn(Tree, Directory, "subsystem", "bus/pci");

    Link = LxStrPrintf("bus/pci/devices/%s", Name);
    if (Link != NULL)
    {
        LxSysDevLink(Tree, Link, Directory);
        LxFree(Link);
    }
    if ((Driver != NULL) && (Driver->Service[0] != '\0'))
    {
        PSTR Target = LxStrPrintf("bus/pci/drivers/%s", Driver->Service);

        if (Target != NULL)
        {
            LxSysDevLinkIn(Tree, Directory, "driver", Target);
            LxSysDevDirectory(Tree, Target);
            LxSysDevLinkIn(Tree, Target, Name, Directory);
            LxFree(Target);
        }
    }
}

static
BOOLEAN
LxSysDevReadConfig(
    _In_ ULONG Bus,
    _In_ ULONG Device,
    _In_ ULONG Function,
    _Out_writes_(LX_SYSDEV_PCI_CONFIG) PUCHAR Config)
{
    PCI_SLOT_NUMBER Slot;

    Slot.u.AsULONG = 0;
    Slot.u.bits.DeviceNumber = Device;
    Slot.u.bits.FunctionNumber = Function;
    if (HalGetBusDataByOffset(PCIConfiguration, Bus, Slot.u.AsULONG, Config, 0, sizeof(USHORT)) != sizeof(USHORT))
        return FALSE;
    if ((*(PUSHORT)Config == 0xFFFF) || (*(PUSHORT)Config == 0))
        return FALSE;
    RtlZeroMemory(Config, LX_SYSDEV_PCI_CONFIG);
    return HalGetBusDataByOffset(PCIConfiguration, Bus, Slot.u.AsULONG, Config, 0, LX_SYSDEV_PCI_CONFIG) != 0;
}

static
VOID
LxSysDevScanBus(
    _Inout_ PLX_SYSDEV_TREE Tree,
    _In_ PLX_SYSDEV_PCI Pci,
    _In_ ULONG Bus,
    _In_ PCSTR Parent,
    _In_ ULONG Depth,
    _Inout_ PULONG Seen)
{
    UCHAR Config[LX_SYSDEV_PCI_CONFIG];
    ULONG Device, Function;

    if ((Depth > 8) || (Seen[Bus / 32] & (1UL << (Bus % 32))))
        return;
    Seen[Bus / 32] |= 1UL << (Bus % 32);
    for (Device = 0; Device < PCI_MAX_DEVICES; Device++)
    {
        for (Function = 0; Function < PCI_MAX_FUNCTION; Function++)
        {
            CHAR Name[16];
            PSTR Directory;

            if (!LxSysDevReadConfig(Bus, Device, Function, Config))
            {
                if (Function == 0)
                    break;
                continue;
            }
            RtlStringCbPrintfA(Name, sizeof(Name), "0000:%02x:%02x.%u", Bus, Device, Function);
            Directory = LxStrPrintf("%s/%s", Parent, Name);
            if (Directory == NULL)
            {
                Tree->Failed = TRUE;
                return;
            }
            LxSysDevPciDevice(Tree, Pci, Directory, Name, Bus, Device, Function, Config);
            if (((Config[0x0E] & 0x7F) == 1) && (Config[0x19] > Bus))
                LxSysDevScanBus(Tree, Pci, Config[0x19], Directory, Depth + 1, Seen);
            LxFree(Directory);
            if ((Function == 0) && !(Config[0x0E] & 0x80))
                break;
        }
    }
}

static
NTSTATUS
LxSysDevIoctl(
    _In_ PDEVICE_OBJECT Device,
    _In_ PFILE_OBJECT File,
    _In_ ULONG Code,
    _Inout_updates_bytes_(Length) PVOID Buffer,
    _In_ ULONG InLength,
    _In_ ULONG Length,
    _Out_ PULONG Returned)
{
    IO_STATUS_BLOCK Iosb;
    KEVENT Event;
    PIRP Irp;
    NTSTATUS Status;

    *Returned = 0;
    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp = IoBuildDeviceIoControlRequest(Code, Device, Buffer, InLength, Buffer, Length, FALSE, &Event, &Iosb);
    if (Irp == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;
    IoGetNextIrpStackLocation(Irp)->FileObject = File;
    Status = IoCallDriver(Device, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = Iosb.Status;
    }
    *Returned = (ULONG)Iosb.Information;
    return Status;
}

static
NTSTATUS
LxSysDevOpen(
    _In_reads_(Length) PCWSTR Name,
    _In_ ULONG Length,
    _Out_ PFILE_OBJECT *File,
    _Out_ PDEVICE_OBJECT *Device)
{
    UNICODE_STRING Path;
    NTSTATUS Status;
    SIZE_T Bytes = (Length + 5) * sizeof(WCHAR);

    *File = NULL;
    *Device = NULL;
    Path.Buffer = LxAllocPaged(Bytes);
    if (Path.Buffer == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;
    Path.Length = 0;
    Path.MaximumLength = (USHORT)Bytes;
    if ((Length >= 4) && (Name[0] == L'\\') && ((Name[1] == L'?') || (Name[1] == L'\\')))
    {
        RtlAppendUnicodeToString(&Path, L"\\??\\");
        Name += 4;
        Length -= 4;
    }
    else
    {
        RtlAppendUnicodeToString(&Path, L"\\??\\");
    }
    RtlCopyMemory(Path.Buffer + Path.Length / sizeof(WCHAR), Name, Length * sizeof(WCHAR));
    Path.Length += (USHORT)(Length * sizeof(WCHAR));
    Status = IoGetDeviceObjectPointer(&Path, FILE_READ_ATTRIBUTES, File, Device);
    LxFreePaged(Path.Buffer);
    return Status;
}

static
ULONG
LxSysDevUtf8(
    _In_reads_bytes_(Length) const VOID *Utf16,
    _In_ ULONG Length,
    _Out_writes_z_(Size) PSTR Text,
    _In_ ULONG Size)
{
    ULONG Written = 0;

    if (!NT_SUCCESS(RtlUnicodeToUTF8N(Text, Size - 1, &Written, Utf16, Length & ~1UL)))
        Written = 0;
    Text[min(Written, Size - 1)] = '\0';
    return Written;
}

static
NTSTATUS
LxSysDevDescriptor(
    _In_ PDEVICE_OBJECT Device,
    _In_ PFILE_OBJECT File,
    _In_ ULONG Port,
    _In_ USHORT Value,
    _In_ USHORT Index,
    _Out_writes_bytes_(Size) PUCHAR Data,
    _In_ ULONG Size,
    _Out_ PULONG Length)
{
    ULONG Total = FIELD_OFFSET(USB_DESCRIPTOR_REQUEST, Data) + Size, Returned;
    PUSB_DESCRIPTOR_REQUEST Request = LxAllocPaged(Total);
    NTSTATUS Status;

    *Length = 0;
    if (Request == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(Request, Total);
    Request->ConnectionIndex = Port;
    Request->SetupPacket.bmRequest = 0x80;
    Request->SetupPacket.bRequest = USB_REQUEST_GET_DESCRIPTOR;
    Request->SetupPacket.wValue = Value;
    Request->SetupPacket.wIndex = Index;
    Request->SetupPacket.wLength = (USHORT)Size;
    Status = LxSysDevIoctl(Device, File, IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION, Request, Total, Total, &Returned);
    if (NT_SUCCESS(Status) && (Returned > FIELD_OFFSET(USB_DESCRIPTOR_REQUEST, Data)))
    {
        *Length = min(Returned - FIELD_OFFSET(USB_DESCRIPTOR_REQUEST, Data), Size);
        RtlCopyMemory(Data, Request->Data, *Length);
    }
    LxFreePaged(Request);
    return Status;
}

static
VOID
LxSysDevString(
    _In_ PDEVICE_OBJECT Device,
    _In_ PFILE_OBJECT File,
    _In_ ULONG Port,
    _In_ UCHAR StringIndex,
    _In_ USHORT Language,
    _Out_writes_z_(Size) PSTR Text,
    _In_ ULONG Size)
{
    UCHAR Data[256];
    ULONG Length;

    Text[0] = '\0';
    if ((StringIndex == 0) || (Language == 0))
        return;
    if (!NT_SUCCESS(LxSysDevDescriptor(Device, File, Port, (USB_STRING_DESCRIPTOR_TYPE << 8) | StringIndex, Language,
                                       Data, sizeof(Data), &Length)) ||
        (Length < 2) || (Data[1] != USB_STRING_DESCRIPTOR_TYPE))
    {
        return;
    }
    LxSysDevUtf8(Data + 2, min(Length, Data[0]) - 2, Text, Size);
}

static
VOID
LxSysDevUsbAttributes(
    _Inout_ PLX_SYSDEV_TREE Tree,
    _In_ PLX_SYSDEV_BUS Bus,
    _In_ PCSTR Directory,
    _In_ PCSTR DevPath,
    _In_ ULONG Devnum,
    _In_ PUSB_DEVICE_DESCRIPTOR Descriptor,
    _In_reads_bytes_(ConfigLength) PUCHAR Configs,
    _In_ ULONG ConfigLength,
    _In_ UCHAR Current,
    _In_ PCSTR Speed,
    _In_ ULONG MaxChild,
    _In_ PCSTR Manufacturer,
    _In_ PCSTR Product,
    _In_ PCSTR Serial,
    _Inout_ PLX_SYSDEV_USB Source)
{
    PUCHAR Descriptors = LxAlloc(sizeof(*Descriptor) + ConfigLength);
    PUCHAR Config = NULL, Cursor, Limit;
    ULONG Minor = (Bus->Number - 1) * 128 + (Devnum - 1), Offset;
    CHAR Name[64];
    ULONG Power = 0;

    for (Offset = 0; Offset + 9 <= ConfigLength;)
    {
        USHORT Total = *(PUSHORT)&Configs[Offset + 2];

        if ((Configs[Offset + 1] != USB_CONFIGURATION_DESCRIPTOR_TYPE) || (Total < 9))
            break;
        if ((Configs[Offset + 5] == Current) || (Config == NULL))
            Config = Configs + Offset;
        Offset += Total;
    }

    LxSysDevDirectory(Tree, Directory);
    if (Descriptors != NULL)
    {
        RtlCopyMemory(Descriptors, Descriptor, sizeof(*Descriptor));
        RtlCopyMemory(Descriptors + sizeof(*Descriptor), Configs, ConfigLength);
        LxSysDevBinary(Tree, Directory, "descriptors", 0444, Descriptors, sizeof(*Descriptor) + ConfigLength, MAXULONG);
        Source->Bus = Bus->Number;
        Source->Devnum = Devnum;
        if (!LxSysDevAddUsb(Tree, Source, Descriptors, sizeof(*Descriptor) + ConfigLength))
            Tree->Failed = TRUE;
        LxFree(Descriptors);
    }
    else
    {
        Tree->Failed = TRUE;
    }
    if (Config != NULL)
        Power = Config[8] * ((Descriptor->bcdUSB >= 0x0300) ? 8 : 2);
    LxSysDevText(Tree, Directory, "authorized", "1\n");
    LxSysDevText(Tree, Directory, "avoid_reset_quirk", "0\n");
    if (Current != 0)
        LxSysDevText(Tree, Directory, "bConfigurationValue", "%u\n", Current);
    else
        LxSysDevText(Tree, Directory, "bConfigurationValue", "\n");
    LxSysDevText(Tree, Directory, "bDeviceClass", "%02x\n", Descriptor->bDeviceClass);
    LxSysDevText(Tree, Directory, "bDeviceSubClass", "%02x\n", Descriptor->bDeviceSubClass);
    LxSysDevText(Tree, Directory, "bDeviceProtocol", "%02x\n", Descriptor->bDeviceProtocol);
    LxSysDevText(Tree, Directory, "bMaxPacketSize0", "%u\n",
                 (Descriptor->bcdUSB >= 0x0300) ? (1U << Descriptor->bMaxPacketSize0) : Descriptor->bMaxPacketSize0);
    LxSysDevText(Tree, Directory, "bMaxPower", "%umA\n", Power);
    LxSysDevText(Tree, Directory, "bNumConfigurations", "%u\n", Descriptor->bNumConfigurations);
    LxSysDevText(Tree, Directory, "bNumInterfaces", "%2u\n", (Config != NULL) ? Config[4] : 0);
    LxSysDevText(Tree, Directory, "bcdDevice", "%04x\n", Descriptor->bcdDevice);
    LxSysDevText(Tree, Directory, "bmAttributes", "%2x\n", (Config != NULL) ? Config[7] : 0);
    LxSysDevText(Tree, Directory, "busnum", "%u\n", Bus->Number);
    LxSysDevText(Tree, Directory, "devnum", "%u\n", Devnum);
    LxSysDevText(Tree, Directory, "devpath", "%s\n", DevPath);
    LxSysDevText(Tree, Directory, "dev", "%u:%u\n", LX_USB_DEVICE_MAJOR, Minor);
    LxSysDevText(Tree, Directory, "idVendor", "%04x\n", Descriptor->idVendor);
    LxSysDevText(Tree, Directory, "idProduct", "%04x\n", Descriptor->idProduct);
    LxSysDevText(Tree, Directory, "maxchild", "%u\n", MaxChild);
    LxSysDevText(Tree, Directory, "quirks", "0x0\n");
    LxSysDevText(Tree, Directory, "removable", "unknown\n");
    LxSysDevText(Tree, Directory, "rx_lanes", "1\n");
    LxSysDevText(Tree, Directory, "tx_lanes", "1\n");
    LxSysDevText(Tree, Directory, "configuration", "");
    LxSysDevText(Tree, Directory, "speed", "%s\n", Speed);
    LxSysDevText(Tree, Directory, "urbnum", "0\n");
    LxSysDevText(Tree, Directory, "version", "%2x.%02x\n", Descriptor->bcdUSB >> 8, Descriptor->bcdUSB & 0xFF);
    if (Manufacturer[0] != '\0')
        LxSysDevText(Tree, Directory, "manufacturer", "%s\n", Manufacturer);
    if (Product[0] != '\0')
        LxSysDevText(Tree, Directory, "product", "%s\n", Product);
    if (Serial[0] != '\0')
        LxSysDevText(Tree, Directory, "serial", "%s\n", Serial);
    LxSysDevText(Tree, Directory, "uevent",
                 "MAJOR=%u\nMINOR=%u\nDEVNAME=bus/usb/%03u/%03u\nDEVTYPE=usb_device\nDRIVER=usb\n"
                 "PRODUCT=%x/%x/%x\nTYPE=%u/%u/%u\nBUSNUM=%03u\nDEVNUM=%03u\n",
                 LX_USB_DEVICE_MAJOR, Minor, Bus->Number, Devnum, Descriptor->idVendor, Descriptor->idProduct,
                 Descriptor->bcdDevice, Descriptor->bDeviceClass, Descriptor->bDeviceSubClass,
                 Descriptor->bDeviceProtocol, Bus->Number, Devnum);
    LxSysDevLinkIn(Tree, Directory, "subsystem", "bus/usb");
    LxSysDevLinkIn(Tree, Directory, "driver", "bus/usb/drivers/usb");

    if (Config == NULL)
        return;
    Limit = min(Config + *(PUSHORT)&Config[2], Configs + ConfigLength);
    for (Cursor = Config + Config[0]; Cursor + 2 <= Limit; Cursor += Cursor[0])
    {
        PSTR Interface;
        PCSTR Driver;

        if (Cursor[0] < 2)
            break;
        if ((Cursor[1] != USB_INTERFACE_DESCRIPTOR_TYPE) || (Cursor[0] < 9) || (Cursor + 9 > Limit) || (Cursor[3] != 0))
            continue;
        RtlStringCbPrintfA(Name, sizeof(Name), "%s:%u.%u", strrchr(Directory, '/') + 1, Config[5], Cursor[2]);
        if (strncmp(Name, "usb", 3) == 0)
            RtlStringCbPrintfA(Name, sizeof(Name), "%u-0:%u.%u", Bus->Number, Config[5], Cursor[2]);
        Interface = LxStrPrintf("%s/%s", Directory, Name);
        if (Interface == NULL)
        {
            Tree->Failed = TRUE;
            return;
        }
        Driver = (Cursor[2] < LX_SYSDEV_MAX_INTERFACES) ? Source->Drivers[Cursor[2]] : NULL;
        LxSysDevDirectory(Tree, Interface);
        LxSysDevText(Tree, Interface, "authorized", "1\n");
        LxSysDevText(Tree, Interface, "bAlternateSetting", "%2u\n", Cursor[3]);
        LxSysDevText(Tree, Interface, "bInterfaceNumber", "%02x\n", Cursor[2]);
        LxSysDevText(Tree, Interface, "bNumEndpoints", "%02x\n", Cursor[4]);
        LxSysDevText(Tree, Interface, "bInterfaceClass", "%02x\n", Cursor[5]);
        LxSysDevText(Tree, Interface, "bInterfaceSubClass", "%02x\n", Cursor[6]);
        LxSysDevText(Tree, Interface, "bInterfaceProtocol", "%02x\n", Cursor[7]);
        LxSysDevText(Tree, Interface, "modalias", "usb:v%04Xp%04Xd%04Xdc%02Xdsc%02Xdp%02Xic%02Xisc%02Xip%02Xin%02X\n",
                     Descriptor->idVendor, Descriptor->idProduct, Descriptor->bcdDevice, Descriptor->bDeviceClass,
                     Descriptor->bDeviceSubClass, Descriptor->bDeviceProtocol, Cursor[5], Cursor[6], Cursor[7],
                     Cursor[2]);
        LxSysDevText(Tree, Interface, "uevent",
                     "DEVTYPE=usb_interface\n%s%s%sPRODUCT=%x/%x/%x\nTYPE=%u/%u/%u\nINTERFACE=%u/%u/%u\n"
                     "MODALIAS=usb:v%04Xp%04Xd%04Xdc%02Xdsc%02Xdp%02Xic%02Xisc%02Xip%02Xin%02X\n",
                     (Driver != NULL) ? "DRIVER=" : "", (Driver != NULL) ? Driver : "", (Driver != NULL) ? "\n" : "",
                     Descriptor->idVendor, Descriptor->idProduct, Descriptor->bcdDevice, Descriptor->bDeviceClass,
                     Descriptor->bDeviceSubClass, Descriptor->bDeviceProtocol, Cursor[5], Cursor[6], Cursor[7],
                     Descriptor->idVendor, Descriptor->idProduct, Descriptor->bcdDevice, Descriptor->bDeviceClass,
                     Descriptor->bDeviceSubClass, Descriptor->bDeviceProtocol, Cursor[5], Cursor[6], Cursor[7],
                     Cursor[2]);
        LxSysDevLinkIn(Tree, Interface, "subsystem", "bus/usb");
        if (Driver != NULL)
        {
            PSTR Target = LxStrPrintf("bus/usb/drivers/%s", Driver);

            if (Target != NULL)
            {
                LxSysDevLinkIn(Tree, Interface, "driver", Target);
                LxSysDevDirectory(Tree, Target);
                LxFree(Target);
            }
        }
        RtlStringCbPrintfA(Name, sizeof(Name), "bus/usb/devices/%s", strrchr(Interface, '/') + 1);
        LxSysDevLink(Tree, Name, Interface);
        LxFree(Interface);
    }
}

static
PCSTR
LxSysDevSpeed(
    _In_ UCHAR Speed,
    _In_ BOOLEAN SuperSpeed,
    _In_ BOOLEAN SuperSpeedPlus)
{
    if (SuperSpeedPlus)
        return "10000";
    if (SuperSpeed || (Speed == UsbSuperSpeed))
        return "5000";
    if (Speed == UsbHighSpeed)
        return "480";
    if (Speed == UsbFullSpeed)
        return "12";
    return "1.5";
}

static
VOID
LxSysDevReadUsbDrivers(
    _Inout_ PLX_SYSDEV_TREE Tree)
{
    static const WCHAR EnumUsb[] = L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Enum\\USB";
    UCHAR Buffer[sizeof(KEY_BASIC_INFORMATION) + 256 * sizeof(WCHAR)];
    UCHAR InstanceBuffer[sizeof(KEY_BASIC_INFORMATION) + 256 * sizeof(WCHAR)];
    PKEY_BASIC_INFORMATION Hardware = (PKEY_BASIC_INFORMATION)Buffer;
    PKEY_BASIC_INFORMATION Instance = (PKEY_BASIC_INFORMATION)InstanceBuffer;
    HANDLE Root, HardwareKey, InstanceKey;
    ULONG HardwareIndex, InstanceIndex, Length;

    Tree->UsbDrivers = LxAllocPaged(LX_SYSDEV_MAX_USB_DRIVERS * sizeof(LX_SYSDEV_USB_DRIVER));
    if (Tree->UsbDrivers == NULL)
        return;
    Root = LxSysDevOpenKey(NULL, EnumUsb, sizeof(EnumUsb) - sizeof(WCHAR));
    if (Root == NULL)
        return;
    for (HardwareIndex = 0;
         (Tree->UsbDriverCount < LX_SYSDEV_MAX_USB_DRIVERS) &&
         NT_SUCCESS(ZwEnumerateKey(Root, HardwareIndex, KeyBasicInformation, Hardware, sizeof(Buffer) - sizeof(WCHAR), &Length));
         HardwareIndex++)
    {
        HardwareKey = LxSysDevOpenKey(Root, Hardware->Name, (USHORT)Hardware->NameLength);
        if (HardwareKey == NULL)
            continue;
        for (InstanceIndex = 0;
             (Tree->UsbDriverCount < LX_SYSDEV_MAX_USB_DRIVERS) &&
             NT_SUCCESS(ZwEnumerateKey(HardwareKey, InstanceIndex, KeyBasicInformation, Instance,
                                       sizeof(InstanceBuffer) - sizeof(WCHAR), &Length));
             InstanceIndex++)
        {
            PLX_SYSDEV_USB_DRIVER Entry = &Tree->UsbDrivers[Tree->UsbDriverCount];
            PKEY_VALUE_PARTIAL_INFORMATION Driver, Service;

            InstanceKey = LxSysDevOpenKey(HardwareKey, Instance->Name, (USHORT)Instance->NameLength);
            if (InstanceKey == NULL)
                continue;
            Driver = LxSysDevQueryValue(InstanceKey, L"Driver");
            Service = LxSysDevQueryValue(InstanceKey, L"Service");
            if ((Driver != NULL) && (Driver->Type == REG_SZ) && (Service != NULL) && (Service->Type == REG_SZ))
            {
                RtlZeroMemory(Entry, sizeof(*Entry));
                RtlCopyMemory(Entry->DriverKey, Driver->Data,
                              min(Driver->DataLength, sizeof(Entry->DriverKey) - sizeof(WCHAR)));
                RtlCopyMemory(Entry->Hardware, Hardware->Name,
                              min(Hardware->NameLength, sizeof(Entry->Hardware) - sizeof(WCHAR)));
                LxSysDevUtf8(Service->Data,
                             (ULONG)(wcsnlen((PCWSTR)Service->Data, Service->DataLength / sizeof(WCHAR)) * sizeof(WCHAR)),
                             Entry->Service, sizeof(Entry->Service));
                Tree->UsbDriverCount++;
            }
            if (Driver != NULL)
                LxFreePaged(Driver);
            if (Service != NULL)
                LxFreePaged(Service);
            ZwClose(InstanceKey);
        }
        ZwClose(HardwareKey);
    }
    ZwClose(Root);
}

static
VOID
LxSysDevUsbDrivers(
    _In_ PLX_SYSDEV_TREE Tree,
    _In_ PDEVICE_OBJECT Hub,
    _In_ PFILE_OBJECT HubFile,
    _In_ ULONG Port,
    _In_ PUSB_DEVICE_DESCRIPTOR Descriptor,
    _Out_writes_(LX_SYSDEV_MAX_INTERFACES) PSTR *Drivers)
{
    ULONG Size = sizeof(USB_NODE_CONNECTION_DRIVERKEY_NAME) + 256 * sizeof(WCHAR), Returned, Index, Entry;
    PUSB_NODE_CONNECTION_DRIVERKEY_NAME Key;
    PSTR Service = NULL;

    RtlZeroMemory(Drivers, LX_SYSDEV_MAX_INTERFACES * sizeof(PSTR));
    if (Tree->UsbDrivers == NULL)
        return;
    Key = LxAllocPaged(Size);
    if (Key == NULL)
        return;
    RtlZeroMemory(Key, Size);
    Key->ConnectionIndex = Port;
    if (NT_SUCCESS(LxSysDevIoctl(Hub, HubFile, IOCTL_USB_GET_NODE_CONNECTION_DRIVERKEY_NAME, Key, Size - sizeof(WCHAR),
                                 Size - sizeof(WCHAR), &Returned)) &&
        (Key->DriverKeyName[0] != UNICODE_NULL))
    {
        for (Entry = 0; Entry < Tree->UsbDriverCount; Entry++)
        {
            if (_wcsicmp(Tree->UsbDrivers[Entry].DriverKey, Key->DriverKeyName) == 0)
            {
                Service = Tree->UsbDrivers[Entry].Service;
                break;
            }
        }
    }
    LxFreePaged(Key);
    if ((Service == NULL) || (Service[0] == '\0'))
        return;
    for (Index = 0; Index < LX_SYSDEV_MAX_INTERFACES; Index++)
    {
        WCHAR Hardware[48];

        Drivers[Index] = Service;
        if (_stricmp(Service, "usbccgp") != 0)
            continue;
        Drivers[Index] = NULL;
        RtlStringCbPrintfW(Hardware, sizeof(Hardware), L"VID_%04X&PID_%04X&MI_%02X", Descriptor->idVendor,
                           Descriptor->idProduct, Index);
        for (Entry = 0; Entry < Tree->UsbDriverCount; Entry++)
        {
            if (_wcsicmp(Tree->UsbDrivers[Entry].Hardware, Hardware) == 0)
            {
                Drivers[Index] = Tree->UsbDrivers[Entry].Service;
                break;
            }
        }
    }
}

static
ULONG
LxSysDevPortCount(
    _In_ PDEVICE_OBJECT Hub,
    _In_ PFILE_OBJECT HubFile)
{
    USB_NODE_INFORMATION Node;
    ULONG Returned;

    RtlZeroMemory(&Node, sizeof(Node));
    if (!NT_SUCCESS(LxSysDevIoctl(Hub, HubFile, IOCTL_USB_GET_NODE_INFORMATION, &Node, sizeof(Node), sizeof(Node),
                                  &Returned)) ||
        (Node.NodeType != UsbHub))
    {
        return 0;
    }
    return min(Node.u.HubInformation.HubDescriptor.bNumberOfPorts, LX_SYSDEV_MAX_PORTS);
}

static
VOID
LxSysDevUsbHub(
    _Inout_ PLX_SYSDEV_TREE Tree,
    _In_ PDEVICE_OBJECT Hub,
    _In_ PFILE_OBJECT HubFile,
    _In_ PCWSTR HubName,
    _In_ ULONG Ports,
    _In_ PLX_SYSDEV_BUS Bus2,
    _In_opt_ PLX_SYSDEV_BUS Bus3,
    _In_ PCSTR Parent2,
    _In_opt_ PCSTR Parent3,
    _In_ PCSTR Prefix,
    _In_ ULONG Depth);

static
VOID
LxSysDevUsbPort(
    _Inout_ PLX_SYSDEV_TREE Tree,
    _In_ PDEVICE_OBJECT Hub,
    _In_ PFILE_OBJECT HubFile,
    _In_ PCWSTR HubName,
    _In_ ULONG Port,
    _In_ PLX_SYSDEV_BUS Bus2,
    _In_opt_ PLX_SYSDEV_BUS Bus3,
    _In_ PCSTR Parent2,
    _In_opt_ PCSTR Parent3,
    _In_ PCSTR Prefix,
    _In_ ULONG Depth)
{
    ULONG Size = sizeof(USB_NODE_CONNECTION_INFORMATION_EX) + 32 * sizeof(USB_PIPE_INFO), Returned;
    PUSB_NODE_CONNECTION_INFORMATION_EX Info = LxAllocPaged(Size);
    USB_NODE_CONNECTION_INFORMATION_EX_V2 Info2;
    CHAR Manufacturer[128], Product[128], Serial[128], Name[64], DevPath[32];
    UCHAR Languages[8];
    PUCHAR Configs = NULL;
    ULONG ConfigLength = 0, Index, Length, Devnum;
    USHORT Language = 0;
    BOOLEAN Super = FALSE, SuperPlus = FALSE;
    PLX_SYSDEV_BUS Bus;
    PCSTR Parent;
    PSTR Directory = NULL, Link = NULL;
    PDEVICE_OBJECT Child = NULL;
    PFILE_OBJECT ChildFile = NULL;
    ULONG MaxChild = 0, NameSize = sizeof(USB_NODE_CONNECTION_NAME) + 512 * sizeof(WCHAR);
    PUSB_NODE_CONNECTION_NAME ChildName = NULL;
    LX_SYSDEV_USB Source;

    if (Info == NULL)
    {
        Tree->Failed = TRUE;
        return;
    }
    RtlZeroMemory(Info, Size);
    Info->ConnectionIndex = Port;
    if (!NT_SUCCESS(LxSysDevIoctl(Hub, HubFile, IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX, Info, Size, Size,
                                  &Returned)) ||
        (Info->ConnectionStatus != DeviceConnected) || (Info->DeviceDescriptor.bLength < sizeof(USB_DEVICE_DESCRIPTOR)))
    {
        LxFreePaged(Info);
        return;
    }
    RtlZeroMemory(&Info2, sizeof(Info2));
    Info2.ConnectionIndex = Port;
    Info2.Length = sizeof(Info2);
    Info2.SupportedUsbProtocols.Usb110 = 1;
    Info2.SupportedUsbProtocols.Usb200 = 1;
    Info2.SupportedUsbProtocols.Usb300 = 1;
    if (NT_SUCCESS(LxSysDevIoctl(Hub, HubFile, IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX_V2, &Info2, sizeof(Info2),
                                 sizeof(Info2), &Returned)))
    {
        Super = Info2.Flags.DeviceIsOperatingAtSuperSpeedOrHigher != 0;
        SuperPlus = Info2.Flags.DeviceIsOperatingAtSuperSpeedPlusOrHigher != 0;
    }
    if (Info->Speed == UsbSuperSpeed)
        Super = TRUE;

    Bus = ((Super || SuperPlus) && (Bus3 != NULL)) ? Bus3 : Bus2;
    Parent = (Bus == Bus3) ? Parent3 : Parent2;
    Devnum = Bus->NextDevnum++;

    for (Index = 0; Index < Info->DeviceDescriptor.bNumConfigurations; Index++)
    {
        UCHAR Header[9];
        PUCHAR Grown;
        USHORT Total;

        if (!NT_SUCCESS(LxSysDevDescriptor(Hub, HubFile, Port, (USB_CONFIGURATION_DESCRIPTOR_TYPE << 8) | Index, 0,
                                           Header, sizeof(Header), &Length)) ||
            (Length < sizeof(Header)))
        {
            break;
        }
        Total = *(PUSHORT)&Header[2];
        if (Total < sizeof(Header))
            break;
        Grown = LxAlloc(ConfigLength + Total);
        if (Grown == NULL)
            break;
        if (Configs != NULL)
        {
            RtlCopyMemory(Grown, Configs, ConfigLength);
            LxFree(Configs);
        }
        Configs = Grown;
        if (!NT_SUCCESS(LxSysDevDescriptor(Hub, HubFile, Port, (USB_CONFIGURATION_DESCRIPTOR_TYPE << 8) | Index, 0,
                                           Configs + ConfigLength, Total, &Length)))
        {
            break;
        }
        ConfigLength += Length;
    }

    if (NT_SUCCESS(LxSysDevDescriptor(Hub, HubFile, Port, USB_STRING_DESCRIPTOR_TYPE << 8, 0, Languages,
                                      sizeof(Languages), &Length)) &&
        (Length >= 4) && (Languages[1] == USB_STRING_DESCRIPTOR_TYPE))
    {
        Language = *(PUSHORT)&Languages[2];
    }
    LxSysDevString(Hub, HubFile, Port, Info->DeviceDescriptor.iManufacturer, Language, Manufacturer, sizeof(Manufacturer));
    LxSysDevString(Hub, HubFile, Port, Info->DeviceDescriptor.iProduct, Language, Product, sizeof(Product));
    LxSysDevString(Hub, HubFile, Port, Info->DeviceDescriptor.iSerialNumber, Language, Serial, sizeof(Serial));

    if (Prefix[0] == '\0')
    {
        RtlStringCbPrintfA(Name, sizeof(Name), "%u-%u", Bus->Number, Port);
        RtlStringCbPrintfA(DevPath, sizeof(DevPath), "%u", Port);
    }
    else
    {
        RtlStringCbPrintfA(Name, sizeof(Name), "%u-%s.%u", Bus->Number, Prefix, Port);
        RtlStringCbPrintfA(DevPath, sizeof(DevPath), "%s.%u", Prefix, Port);
    }
    if (Info->DeviceIsHub && (Depth < LX_SYSDEV_MAX_DEPTH))
    {
        ChildName = LxAllocPaged(NameSize);
        if (ChildName != NULL)
        {
            RtlZeroMemory(ChildName, NameSize);
            ChildName->ConnectionIndex = Port;
            if (NT_SUCCESS(LxSysDevIoctl(Hub, HubFile, IOCTL_USB_GET_NODE_CONNECTION_NAME, ChildName,
                                         NameSize - sizeof(WCHAR), NameSize - sizeof(WCHAR), &Returned)) &&
                (ChildName->NodeName[0] != UNICODE_NULL) &&
                NT_SUCCESS(LxSysDevOpen(ChildName->NodeName, (ULONG)wcsnlen(ChildName->NodeName, 512), &ChildFile,
                                        &Child)))
            {
                MaxChild = LxSysDevPortCount(Child, ChildFile);
            }
        }
    }

    RtlZeroMemory(&Source, sizeof(Source));
    Source.ParentHub = (PWSTR)HubName;
    Source.Port = Port;
    Source.OwnHub = (Child != NULL) ? ChildName->NodeName : NULL;
    if (SuperPlus)
        Source.Speed = LX_USB_SPEED_SUPER_PLUS;
    else if (Super)
        Source.Speed = LX_USB_SPEED_SUPER;
    else if (Info->Speed == UsbHighSpeed)
        Source.Speed = LX_USB_SPEED_HIGH;
    else if (Info->Speed == UsbFullSpeed)
        Source.Speed = LX_USB_SPEED_FULL;
    else
        Source.Speed = LX_USB_SPEED_LOW;
    LxSysDevUsbDrivers(Tree, Hub, HubFile, Port, &Info->DeviceDescriptor, Source.Drivers);

    Directory = LxStrPrintf("%s/%s", Parent, Name);
    Link = LxStrPrintf("bus/usb/devices/%s", Name);
    if ((Directory != NULL) && (Link != NULL))
    {
        LxSysDevUsbAttributes(Tree, Bus, Directory, DevPath, Devnum, &Info->DeviceDescriptor, Configs, ConfigLength,
                              Info->CurrentConfigurationValue, LxSysDevSpeed(Info->Speed, Super, SuperPlus), MaxChild,
                              Manufacturer, Product, Serial, &Source);
        LxSysDevLink(Tree, Link, Directory);
        if (Child != NULL)
            LxSysDevUsbHub(Tree, Child, ChildFile, ChildName->NodeName, MaxChild, Bus, NULL, Directory, NULL, DevPath,
                           Depth + 1);
    }
    else
    {
        Tree->Failed = TRUE;
    }
    if (ChildFile != NULL)
        ObDereferenceObject(ChildFile);
    if (ChildName != NULL)
        LxFreePaged(ChildName);
    LxFree(Link);
    LxFree(Configs);
    LxFree(Directory);
    LxFreePaged(Info);
}

static
VOID
LxSysDevUsbHub(
    _Inout_ PLX_SYSDEV_TREE Tree,
    _In_ PDEVICE_OBJECT Hub,
    _In_ PFILE_OBJECT HubFile,
    _In_ PCWSTR HubName,
    _In_ ULONG Ports,
    _In_ PLX_SYSDEV_BUS Bus2,
    _In_opt_ PLX_SYSDEV_BUS Bus3,
    _In_ PCSTR Parent2,
    _In_opt_ PCSTR Parent3,
    _In_ PCSTR Prefix,
    _In_ ULONG Depth)
{
    ULONG Port;

    for (Port = 1; (Port <= Ports) && !Tree->Failed; Port++)
        LxSysDevUsbPort(Tree, Hub, HubFile, HubName, Port, Bus2, Bus3, Parent2, Parent3, Prefix, Depth);
}

static
USHORT
LxSysDevKernelBcd(VOID)
{
    PCSTR Release = LX_KERNEL_RELEASE;
    ULONG Major = 0, Minor = 0;

    while ((*Release >= '0') && (*Release <= '9'))
        Major = Major * 10 + (*Release++ - '0');
    if (*Release == '.')
        Release++;
    while ((*Release >= '0') && (*Release <= '9'))
        Minor = Minor * 10 + (*Release++ - '0');
    return (USHORT)((((Major / 10) % 10) << 12) | ((Major % 10) << 8) | (((Minor / 10) % 10) << 4) | (Minor % 10));
}

static
VOID
LxSysDevRootHub(
    _Inout_ PLX_SYSDEV_TREE Tree,
    _In_ PLX_SYSDEV_BUS Bus,
    _In_ PLX_SYSDEV_HC Hc,
    _In_ PCWSTR HubName,
    _In_ ULONG Ports)
{
    USB_DEVICE_DESCRIPTOR Device;
    LX_SYSDEV_USB Source;
    UCHAR Config[31] =
    {
        9, USB_CONFIGURATION_DESCRIPTOR_TYPE, 25, 0, 1, 1, 0, 0xE0, 0,
        9, USB_INTERFACE_DESCRIPTOR_TYPE, 0, 0, 1, USB_DEVICE_CLASS_HUB, 0, 0, 0,
        7, USB_ENDPOINT_DESCRIPTOR_TYPE, 0x81, USB_ENDPOINT_TYPE_INTERRUPT, 4, 0, 12,
        6, 0x30, 0, 0, 2, 0
    };
    CHAR Manufacturer[96];
    PSTR Link;

    RtlZeroMemory(&Device, sizeof(Device));
    Device.bLength = sizeof(Device);
    Device.bDescriptorType = USB_DEVICE_DESCRIPTOR_TYPE;
    Device.bcdUSB = Bus->BcdUsb;
    Device.bDeviceClass = USB_DEVICE_CLASS_HUB;
    Device.idVendor = 0x1D6B;
    Device.bcdDevice = LxSysDevKernelBcd();
    Device.iManufacturer = 3;
    Device.iProduct = 2;
    Device.iSerialNumber = 1;
    Device.bNumConfigurations = 1;
    if (Bus->BcdUsb >= 0x0300)
    {
        Device.bDeviceProtocol = 3;
        Device.bMaxPacketSize0 = 9;
        Device.idProduct = 3;
        Config[2] = 31;
    }
    else if (Bus->BcdUsb >= 0x0200)
    {
        Device.bDeviceProtocol = (Hc->ProgIf == 0x30) ? 1 : 0;
        Device.bMaxPacketSize0 = 64;
        Device.idProduct = 2;
    }
    else
    {
        Device.bMaxPacketSize0 = 64;
        Device.idProduct = 1;
        Config[24] = 0xFF;
    }
    RtlStringCbPrintfA(Manufacturer, sizeof(Manufacturer), "Linux %s %s", LX_KERNEL_RELEASE, Hc->DriverName);
    RtlZeroMemory(&Source, sizeof(Source));
    Source.RootHub = TRUE;
    Source.OwnHub = (PWSTR)HubName;
    Source.Speed = (Bus->BcdUsb >= 0x0300) ? LX_USB_SPEED_SUPER :
                   ((Bus->BcdUsb >= 0x0200) ? LX_USB_SPEED_HIGH : LX_USB_SPEED_FULL);
    Source.Strings[1] = (PSTR)Hc->Serial;
    Source.Strings[2] = (PSTR)Hc->Product;
    Source.Strings[3] = Manufacturer;
    Source.Drivers[0] = "hub";
    LxSysDevUsbAttributes(Tree, Bus, Bus->Path, "0", Bus->NextDevnum++, &Device, Config, Config[2], 1,
                          (Bus->BcdUsb >= 0x0300) ? "5000" : ((Bus->BcdUsb >= 0x0200) ? "480" : "12"), Ports,
                          Manufacturer, Hc->Product, Hc->Serial, &Source);
    Link = LxStrPrintf("bus/usb/devices/usb%u", Bus->Number);
    if (Link == NULL)
    {
        Tree->Failed = TRUE;
        return;
    }
    LxSysDevLink(Tree, Link, Bus->Path);
    LxFree(Link);
}

static
PCSTR
LxSysDevFindPciDirectory(
    _In_ PLX_SYSDEV_TREE Tree,
    _In_ PCSTR Name)
{
    SIZE_T Length = strlen(Name);
    ULONG Index;

    for (Index = 0; Index < Tree->Count; Index++)
    {
        PCSTR Path = Tree->Nodes[Index].Path;
        SIZE_T PathLength = strlen(Path);

        if ((Tree->Nodes[Index].Type == LX_S_IFDIR) && (PathLength > Length) &&
            (strncmp(Path, "devices/", 8) == 0) && (Path[PathLength - Length - 1] == '/') &&
            (strcmp(Path + PathLength - Length, Name) == 0))
        {
            return Path;
        }
    }
    return NULL;
}

static
VOID
LxSysDevController(
    _Inout_ PLX_SYSDEV_TREE Tree,
    _In_ PDEVICE_OBJECT Device,
    _In_ PFILE_OBJECT File,
    _In_ ULONG Ordinal)
{
    ULONG RootSize = sizeof(USB_ROOT_HUB_NAME) + 512 * sizeof(WCHAR);
    PUSB_ROOT_HUB_NAME RootName;
    PDEVICE_OBJECT Pdo, Hub;
    PFILE_OBJECT HubFile;
    ULONG PciBus = 0, Address = 0, Length, Ports;
    CHAR Slot[16] = "", Path[256];
    PCSTR Parent = NULL;
    LX_SYSDEV_HC Hc;
    LX_SYSDEV_BUS Bus2, Bus3;
    UCHAR ProgIf = 0x20;
    BOOLEAN Pci = FALSE;

    Pdo = IoGetDeviceAttachmentBaseRef(Device);
    if (Pdo != NULL)
    {
        Pci = NT_SUCCESS(IoGetDeviceProperty(Pdo, DevicePropertyBusNumber, sizeof(PciBus), &PciBus, &Length)) &&
              NT_SUCCESS(IoGetDeviceProperty(Pdo, DevicePropertyAddress, sizeof(Address), &Address, &Length));
        ObDereferenceObject(Pdo);
    }
    if (Pci)
    {
        PCI_SLOT_NUMBER SlotNumber;

        SlotNumber.u.AsULONG = 0;
        SlotNumber.u.bits.DeviceNumber = Address >> 16;
        SlotNumber.u.bits.FunctionNumber = Address & 0xFFFF;
        if (HalGetBusDataByOffset(PCIConfiguration, PciBus, SlotNumber.u.AsULONG, &ProgIf, 9, 1) != 1)
            ProgIf = 0x20;
        RtlStringCbPrintfA(Slot, sizeof(Slot), "0000:%02x:%02x.%u", PciBus, Address >> 16, Address & 0xFFFF);
        Parent = LxSysDevFindPciDirectory(Tree, Slot);
    }
    if (Parent == NULL)
    {
        RtlStringCbPrintfA(Path, sizeof(Path), "devices/platform/usbhc.%u", Ordinal);
        LxSysDevDirectory(Tree, Path);
        Parent = Path;
        if (Slot[0] == '\0')
            RtlStringCbPrintfA(Slot, sizeof(Slot), "usbhc.%u", Ordinal);
    }

    RtlZeroMemory(&Hc, sizeof(Hc));
    Hc.ProgIf = ProgIf;
    Hc.Serial = Slot;
    switch (ProgIf)
    {
        case 0x30: Hc.DriverName = "xhci-hcd"; Hc.Product = "xHCI Host Controller"; break;
        case 0x10: Hc.DriverName = "ohci_hcd"; Hc.Product = "OHCI PCI host controller"; break;
        case 0x00: Hc.DriverName = "uhci_hcd"; Hc.Product = "UHCI Host Controller"; break;
        default: Hc.DriverName = "ehci_hcd"; Hc.Product = "EHCI Host Controller"; break;
    }

    RootName = LxAllocPaged(RootSize);
    if (RootName == NULL)
        return;
    RtlZeroMemory(RootName, RootSize);
    if (!NT_SUCCESS(LxSysDevIoctl(Device, File, IOCTL_USB_GET_ROOT_HUB_NAME, RootName, RootSize - sizeof(WCHAR),
                                  RootSize - sizeof(WCHAR), &Length)) ||
        (RootName->RootHubName[0] == UNICODE_NULL) ||
        !NT_SUCCESS(LxSysDevOpen(RootName->RootHubName, (ULONG)wcsnlen(RootName->RootHubName, 512), &HubFile, &Hub)))
    {
        LxFreePaged(RootName);
        return;
    }
    Ports = LxSysDevPortCount(Hub, HubFile);

    RtlZeroMemory(&Bus2, sizeof(Bus2));
    RtlZeroMemory(&Bus3, sizeof(Bus3));
    Bus2.Number = Tree->NextBus++;
    Bus2.NextDevnum = 1;
    Bus2.BcdUsb = (ProgIf >= 0x20) ? 0x0200 : 0x0110;
    Bus2.Path = LxStrPrintf("%s/usb%u", Parent, Bus2.Number);
    if (ProgIf == 0x30)
    {
        Bus3.Number = Tree->NextBus++;
        Bus3.NextDevnum = 1;
        Bus3.BcdUsb = 0x0300;
        Bus3.Path = LxStrPrintf("%s/usb%u", Parent, Bus3.Number);
    }
    if ((Bus2.Path != NULL) && ((ProgIf != 0x30) || (Bus3.Path != NULL)))
    {
        LxSysDevRootHub(Tree, &Bus2, &Hc, RootName->RootHubName, Ports);
        if (ProgIf == 0x30)
            LxSysDevRootHub(Tree, &Bus3, &Hc, RootName->RootHubName, Ports);
        LxSysDevUsbHub(Tree, Hub, HubFile, RootName->RootHubName, Ports, &Bus2, (ProgIf == 0x30) ? &Bus3 : NULL,
                       Bus2.Path, (ProgIf == 0x30) ? Bus3.Path : NULL, "", 0);
    }
    else
    {
        Tree->Failed = TRUE;
    }
    LxFree((PVOID)Bus2.Path);
    LxFree((PVOID)Bus3.Path);
    LxFreePaged(RootName);
    ObDereferenceObject(HubFile);
}

static
VOID
LxSysDevUsb(
    _Inout_ PLX_SYSDEV_TREE Tree)
{
    PWSTR List = NULL, Name;
    ULONG Ordinal = 0;

    if (!NT_SUCCESS(IoGetDeviceInterfaces(&GUID_DEVINTERFACE_USB_HOST_CONTROLLER, NULL, 0, &List)) || (List == NULL))
        return;
    for (Name = List; (*Name != UNICODE_NULL) && !Tree->Failed; Name += wcslen(Name) + 1)
    {
        PDEVICE_OBJECT Device;
        PFILE_OBJECT File;
        UNICODE_STRING Link;

        RtlInitUnicodeString(&Link, Name);
        if (!NT_SUCCESS(IoGetDeviceObjectPointer(&Link, FILE_READ_ATTRIBUTES, &File, &Device)))
            continue;
        LxSysDevController(Tree, Device, File, Ordinal++);
        ObDereferenceObject(File);
    }
    ExFreePool(List);
}

static
PLX_SYSDEV_TREE
LxSysDevBuild(VOID)
{
    static const PCSTR Directories[] =
    {
        "bus/pci", "bus/pci/devices", "bus/pci/drivers", "bus/pci/slots", "bus/usb", "bus/usb/devices",
        "bus/usb/drivers", "bus/usb/drivers/usb", "bus/usb/drivers/hub", "devices/pci0000:00"
    };
    PLX_SYSDEV_TREE Tree = LxAlloc(sizeof(*Tree));
    PLX_SYSDEV_PCI Pci;
    ULONG Seen[8] = { 0 }, Index;

    if (Tree == NULL)
        return NULL;
    RtlZeroMemory(Tree, sizeof(*Tree));
    Tree->References = 1;
    Tree->NextBus = 1;
    for (Index = 0; Index < RTL_NUMBER_OF(Directories); Index++)
        LxSysDevDirectory(Tree, Directories[Index]);
    LxSysDevText(Tree, "devices/pci0000:00", "uevent", "");

    Pci = LxAllocPaged(sizeof(*Pci));
    if (Pci != NULL)
    {
        LxSysDevReadDrivers(Pci);
        LxSysDevScanBus(Tree, Pci, 0, "devices/pci0000:00", 0, Seen);
        LxFreePaged(Pci);
    }
    LxSysDevReadUsbDrivers(Tree);
    LxSysDevUsb(Tree);
    LxSysDevSort(Tree);
    Tree->Built = (LONG64)KeQueryInterruptTime();
    if (Tree->Failed)
    {
        DPRINT1("The PCI and USB device list could not be built\n");
        LxSysDevFree(Tree);
        return NULL;
    }
    return Tree;
}

static
BOOLEAN
LxSysDevOwns(
    _In_ PCSTR Path,
    _In_ BOOLEAN Parents)
{
    static const PCSTR Roots[] = { "bus/pci", "bus/usb", "devices/pci0000:00", "devices/platform" };
    ULONG Index;

    if (Parents && ((strcmp(Path, "bus") == 0) || (strcmp(Path, "devices") == 0)))
        return TRUE;
    for (Index = 0; Index < RTL_NUMBER_OF(Roots); Index++)
    {
        SIZE_T Length = strlen(Roots[Index]);

        if ((strncmp(Path, Roots[Index], Length) == 0) && ((Path[Length] == '\0') || (Path[Length] == '/')))
            return TRUE;
    }
    return FALSE;
}

static
VOID
LxSysDevRelease(
    _In_ PLX_SYSDEV_TREE Tree)
{
    if (InterlockedDecrement(&Tree->References) == 0)
        LxSysDevFree(Tree);
}

static
PLX_SYSDEV_TREE
LxSysDevAcquire(VOID)
{
    PLX_SYSDEV_TREE Tree, Fresh = NULL, Old = NULL;
    BOOLEAN Stale;

    ExAcquireFastMutex(&LxSysDevLock);
    Tree = LxSysDevCurrent;
    Stale = (Tree == NULL) || ((LONG64)KeQueryInterruptTime() - Tree->Built > LX_SYSDEV_LIFETIME);
    if (!Stale)
        InterlockedIncrement(&Tree->References);
    ExReleaseFastMutex(&LxSysDevLock);
    if (!Stale)
        return Tree;

    Fresh = LxSysDevBuild();
    ExAcquireFastMutex(&LxSysDevLock);
    if (Fresh != NULL)
    {
        Old = LxSysDevCurrent;
        LxSysDevCurrent = Fresh;
    }
    Tree = LxSysDevCurrent;
    if (Tree != NULL)
        InterlockedIncrement(&Tree->References);
    ExReleaseFastMutex(&LxSysDevLock);
    if (Old != NULL)
        LxSysDevRelease(Old);
    return Tree;
}

LONG
LxSysDevQuery(
    _In_ PCSTR Path,
    _Out_ PULONG Type,
    _Out_ PULONG Mode,
    _Out_opt_ PSTR *LinkTarget)
{
    PLX_SYSDEV_TREE Tree;
    ULONG Index;
    LONG Error = -LX_ENOENT;

    *Type = 0;
    *Mode = 0;
    if (LinkTarget != NULL)
        *LinkTarget = NULL;
    if (!LxSysDevOwns(Path, FALSE))
        return -LX_ENOENT;
    Tree = LxSysDevAcquire();
    if (Tree == NULL)
        return -LX_ENOENT;
    Index = LxSysDevLowerBound(Tree, Path);
    if ((Index < Tree->Count) && (strcmp(Tree->Nodes[Index].Path, Path) == 0))
    {
        PLX_SYSDEV_NODE Node = &Tree->Nodes[Index];

        *Type = Node->Type;
        *Mode = Node->Mode;
        Error = 0;
        if ((Node->Type == LX_S_IFLNK) && (LinkTarget != NULL))
        {
            *LinkTarget = LxStrDup((PCSTR)Node->Data);
            if (*LinkTarget == NULL)
                Error = -LX_ENOMEM;
        }
    }
    else if ((Index < Tree->Count) && LxSysDevIsBelow(Tree->Nodes[Index].Path, Path, strlen(Path)))
    {
        *Type = LX_S_IFDIR;
        *Mode = 0755;
        Error = 0;
    }
    LxSysDevRelease(Tree);
    return Error;
}

VOID
LxSysDevList(
    _In_ PCSTR Path,
    _In_ PLX_SYSDEV_LIST_ROUTINE Routine,
    _In_ PVOID Context)
{
    PLX_SYSDEV_TREE Tree;
    SIZE_T Length = strlen(Path), LastLength = 0;
    PCSTR Last = NULL;
    ULONG Index;

    if (!LxSysDevOwns(Path, TRUE))
        return;
    Tree = LxSysDevAcquire();
    if (Tree == NULL)
        return;
    for (Index = LxSysDevLowerBound(Tree, Path); Index < Tree->Count; Index++)
    {
        PLX_SYSDEV_NODE Node = &Tree->Nodes[Index];
        PCSTR Rest, Slash;
        CHAR Name[128];
        SIZE_T NameLength;
        UCHAR Type;

        if (strcmp(Node->Path, Path) == 0)
            continue;
        if (!LxSysDevIsBelow(Node->Path, Path, Length))
            break;
        Rest = Node->Path + Length + 1;
        Slash = strchr(Rest, '/');
        NameLength = (Slash != NULL) ? (SIZE_T)(Slash - Rest) : strlen(Rest);
        if ((Last != NULL) && (NameLength == LastLength) && (strncmp(Rest, Last, NameLength) == 0))
            continue;
        if (NameLength >= sizeof(Name))
            continue;
        Last = Rest;
        LastLength = NameLength;
        RtlCopyMemory(Name, Rest, NameLength);
        Name[NameLength] = '\0';
        if ((Slash != NULL) || (Node->Type == LX_S_IFDIR))
            Type = LX_DT_DIR;
        else
            Type = (Node->Type == LX_S_IFLNK) ? LX_DT_LNK : LX_DT_REG;
        Routine(Context, Type, Name);
    }
    LxSysDevRelease(Tree);
}

LONG
LxSysDevRead(
    _In_ PLX_PROCESS Process,
    _In_ PCSTR Path,
    _Out_ PVOID *Data,
    _Out_ PULONG Length)
{
    PLX_SYSDEV_TREE Tree;
    ULONG64 Effective;
    ULONG Index;
    LONG Error = -LX_ENOENT;

    *Data = NULL;
    *Length = 0;
    if (!LxSysDevOwns(Path, FALSE))
        return -LX_ENOENT;
    Tree = LxSysDevAcquire();
    if (Tree == NULL)
        return -LX_ENOENT;
    Index = LxSysDevLowerBound(Tree, Path);
    if ((Index < Tree->Count) && (strcmp(Tree->Nodes[Index].Path, Path) == 0) &&
        (Tree->Nodes[Index].Type == LX_S_IFREG))
    {
        PLX_SYSDEV_NODE Node = &Tree->Nodes[Index];
        ULONG Size = Node->Length;

        Effective = (Process->Uid[1] == 0) ? Process->CapBounding : Process->CapAmbient;
        if (!(Effective & (1ULL << LX_CAP_SYS_ADMIN)))
            Size = min(Size, Node->Public);
        *Data = LxAlloc(Size + 1);
        if (*Data != NULL)
        {
            RtlCopyMemory(*Data, Node->Data, Size);
            *Length = Size;
            Error = 0;
        }
        else
        {
            Error = -LX_ENOMEM;
        }
    }
    LxSysDevRelease(Tree);
    return Error;
}

ULONG
LxSysDevUsbNodes(
    _Out_writes_(Count) PULONG Nodes,
    _In_ ULONG Count)
{
    PLX_SYSDEV_TREE Tree = LxSysDevAcquire();
    ULONG Copied, Index;

    if (Tree == NULL)
        return 0;
    Copied = min(Count, Tree->UsbCount);
    for (Index = 0; Index < Copied; Index++)
        Nodes[Index] = (Tree->Usb[Index].Bus << 16) | Tree->Usb[Index].Devnum;
    LxSysDevRelease(Tree);
    return Copied;
}

VOID
LxSysDevInitialize(VOID)
{
    ExInitializeFastMutex(&LxSysDevLock);
}
