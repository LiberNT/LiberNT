/*
 * PROJECT:     LiberNT Network Kernel Debugger Transport
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Host side of the KDNET extensibility interface and debug device selection
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "kdnet.h"

typedef NTSTATUS
(NTAPI *PKDNET_INITIALIZE_LIBRARY)(
    _In_ PKDNET_EXTENSIBILITY_IMPORTS ImportTable,
    _In_opt_ PCHAR LoaderOptions,
    _Inout_ PDEBUG_DEVICE_DESCRIPTOR Device);

UCHAR KdNicAddress[MAC_ADDRESS_SIZE];
UCHAR KdNicLinkState;
DEBUG_DEVICE_DESCRIPTOR KdNicDevice;

static CHAR KdpNicPath[KDNET_PATH_SIZE];
static PHYSICAL_ADDRESS KdpNicBase;
static CHAR KdpSavedPath[KDNET_PATH_SIZE];
static PHYSICAL_ADDRESS KdpSavedBase;

static KDNET_EXTENSIBILITY_EXPORTS KdpExports;
static KDNET_EXTENSIBILITY_IMPORTS KdpImports;
static KDNET_SHARED_DATA KdpShared;
static KDNET_EXTENSIBILITY_EXPORTS KdpSavedExports;
static KDNET_SHARED_DATA KdpSavedShared;
static DEBUG_DEVICE_DESCRIPTOR KdpSavedDevice;
static UCHAR KdpSavedAddress[MAC_ADDRESS_SIZE];
static BOOLEAN KdpSavedValid;
static NTSTATUS KdpErrorStatus;
static PWCHAR KdpErrorString;
static ULONG KdpHardwareId;
static BOOLEAN KdpNicStarted;

static
ULONG
NTAPI
KdpGetPciData(
    _In_ ULONG BusNumber,
    _In_ ULONG SlotNumber,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Offset,
    _In_ ULONG Length)
{
    return HalGetBusDataByOffset(PCIConfiguration, BusNumber, SlotNumber, Buffer, Offset, Length);
}

static
ULONG
NTAPI
KdpSetPciData(
    _In_ ULONG BusNumber,
    _In_ ULONG SlotNumber,
    _In_reads_bytes_(Length) PVOID Buffer,
    _In_ ULONG Offset,
    _In_ ULONG Length)
{
    return HalSetBusDataByOffset(PCIConfiguration, BusNumber, SlotNumber, Buffer, Offset, Length);
}

static
PHYSICAL_ADDRESS
NTAPI
KdpGetPhysicalAddress(
    _In_ PVOID Address)
{
    return MmGetPhysicalAddress(Address);
}

static
VOID
NTAPI
KdpStall(
    ULONG Microseconds)
{
    KeStallExecutionProcessor(Microseconds);
}

static UCHAR NTAPI KdpReadRegisterUchar(_In_ PUCHAR Register) { return READ_REGISTER_UCHAR(Register); }
static USHORT NTAPI KdpReadRegisterUshort(_In_ PUSHORT Register) { return READ_REGISTER_USHORT(Register); }
static ULONG NTAPI KdpReadRegisterUlong(_In_ PULONG Register) { return READ_REGISTER_ULONG(Register); }
static VOID NTAPI KdpWriteRegisterUchar(_In_ PUCHAR Register, _In_ UCHAR Value) { WRITE_REGISTER_UCHAR(Register, Value); }
static VOID NTAPI KdpWriteRegisterUshort(_In_ PUSHORT Register, _In_ USHORT Value) { WRITE_REGISTER_USHORT(Register, Value); }
static VOID NTAPI KdpWriteRegisterUlong(_In_ PULONG Register, _In_ ULONG Value) { WRITE_REGISTER_ULONG(Register, Value); }
static UCHAR NTAPI KdpReadPortUchar(_In_ PUCHAR Port) { return READ_PORT_UCHAR(Port); }
static USHORT NTAPI KdpReadPortUshort(_In_ PUSHORT Port) { return READ_PORT_USHORT(Port); }
static ULONG NTAPI KdpReadPortUlong(_In_ PULONG Port) { return READ_PORT_ULONG(Port); }
static VOID NTAPI KdpWritePortUchar(_In_ PUCHAR Port, _In_ UCHAR Value) { WRITE_PORT_UCHAR(Port, Value); }
static VOID NTAPI KdpWritePortUshort(_In_ PUSHORT Port, _In_ USHORT Value) { WRITE_PORT_USHORT(Port, Value); }
static VOID NTAPI KdpWritePortUlong(_In_ PULONG Port, _In_ ULONG Value) { WRITE_PORT_ULONG(Port, Value); }

static
ULONG64
NTAPI
KdpReadRegisterUlong64(
    _In_ ULONG64 *Register)
{
    return READ_REGISTER_ULONG((PULONG)Register) |
           ((ULONG64)READ_REGISTER_ULONG((PULONG)Register + 1) << 32);
}

static
VOID
NTAPI
KdpWriteRegisterUlong64(
    _In_ ULONG64 *Register,
    _In_ ULONG64 Value)
{
    WRITE_REGISTER_ULONG((PULONG)Register, (ULONG)Value);
    WRITE_REGISTER_ULONG((PULONG)Register + 1, (ULONG)(Value >> 32));
}

static
VOID
NTAPI
KdpSetHiberRange(
    _In_opt_ PVOID MemoryMap,
    _In_ ULONG Flags,
    _In_ PVOID Address,
    _In_ ULONG_PTR Length,
    _In_ ULONG Tag)
{
    UNREFERENCED_PARAMETER(MemoryMap);
    UNREFERENCED_PARAMETER(Flags);
    UNREFERENCED_PARAMETER(Address);
    UNREFERENCED_PARAMETER(Length);
    UNREFERENCED_PARAMETER(Tag);
}

static
VOID
NTAPI
KdpBugCheck(
    _In_ ULONG BugCheckCode,
    _In_ ULONG_PTR BugCheckParameter1,
    _In_ ULONG_PTR BugCheckParameter2,
    _In_ ULONG_PTR BugCheckParameter3,
    _In_ ULONG_PTR BugCheckParameter4)
{
    KeBugCheckEx(BugCheckCode, BugCheckParameter1, BugCheckParameter2,
                 BugCheckParameter3, BugCheckParameter4);
}

static
PVOID
NTAPI
KdpMapPhysicalMemory(
    _In_ PHYSICAL_ADDRESS PhysicalAddress,
    _In_ ULONG NumberPages,
    _In_ BOOLEAN FlushCurrentTLB)
{
    UNREFERENCED_PARAMETER(FlushCurrentTLB);

    return MmMapIoSpace(PhysicalAddress, (SIZE_T)NumberPages * PAGE_SIZE, MmNonCached);
}

static
VOID
NTAPI
KdpUnmapVirtualAddress(
    _In_ PVOID VirtualAddress,
    _In_ ULONG NumberPages,
    _In_ BOOLEAN FlushCurrentTLB)
{
    UNREFERENCED_PARAMETER(FlushCurrentTLB);

    MmUnmapIoSpace(VirtualAddress, (SIZE_T)NumberPages * PAGE_SIZE);
}

static
ULONG64
NTAPI
KdpReadCycleCounter(
    _Out_opt_ ULONG64 *Frequency)
{
    LARGE_INTEGER Counter, Rate;

    Counter = KeQueryPerformanceCounter(&Rate);
    if (Frequency)
        *Frequency = Rate.QuadPart;

    return Counter.QuadPart;
}

static
VOID
__cdecl
KdpExtensionPrint(
    _In_ PCHAR Format,
    ...)
{
    va_list Arguments;

    if (KdNetReady)
        return;

    va_start(Arguments, Format);
    vDbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, Format, Arguments);
    va_end(Arguments);
}

static
VOID
KdpBuildImports(VOID)
{
    RtlZeroMemory(&KdpExports, sizeof(KdpExports));
    KdpExports.FunctionCount = KDNET_EXT_EXPORTS;

    RtlZeroMemory(&KdpImports, sizeof(KdpImports));
    KdpImports.FunctionCount = KDNET_EXT_IMPORTS;
    KdpImports.Exports = &KdpExports;
    KdpImports.GetPciDataByOffset = KdpGetPciData;
    KdpImports.SetPciDataByOffset = KdpSetPciData;
    KdpImports.GetPhysicalAddress = KdpGetPhysicalAddress;
    KdpImports.StallExecutionProcessor = KdpStall;
    KdpImports.ReadRegisterUChar = KdpReadRegisterUchar;
    KdpImports.ReadRegisterUShort = KdpReadRegisterUshort;
    KdpImports.ReadRegisterULong = KdpReadRegisterUlong;
    KdpImports.ReadRegisterULong64 = KdpReadRegisterUlong64;
    KdpImports.WriteRegisterUChar = KdpWriteRegisterUchar;
    KdpImports.WriteRegisterUShort = KdpWriteRegisterUshort;
    KdpImports.WriteRegisterULong = KdpWriteRegisterUlong;
    KdpImports.WriteRegisterULong64 = KdpWriteRegisterUlong64;
    KdpImports.ReadPortUChar = KdpReadPortUchar;
    KdpImports.ReadPortUShort = KdpReadPortUshort;
    KdpImports.ReadPortULong = KdpReadPortUlong;
    KdpImports.WritePortUChar = KdpWritePortUchar;
    KdpImports.WritePortUShort = KdpWritePortUshort;
    KdpImports.WritePortULong = KdpWritePortUlong;
    KdpImports.SetHiberRange = KdpSetHiberRange;
    KdpImports.BugCheckEx = KdpBugCheck;
    KdpImports.MapPhysicalMemory64 = KdpMapPhysicalMemory;
    KdpImports.UnmapVirtualAddress = KdpUnmapVirtualAddress;
    KdpImports.ReadCycleCounter = KdpReadCycleCounter;
    KdpImports.KdNetDbgPrintf = KdpExtensionPrint;
    KdpImports.ExecutionEnvironment = KD_ENV_KERNEL;
    KdpImports.KdNetErrorStatus = &KdpErrorStatus;
    KdpImports.KdNetErrorString = &KdpErrorString;
    KdpImports.KdNetHardwareID = &KdpHardwareId;
}

static
PKDNET_INITIALIZE_LIBRARY
KdpFindExtension(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _In_ PCWSTR FileName)
{
    UNICODE_STRING Name;
    PLIST_ENTRY Entry;
    PLDR_DATA_TABLE_ENTRY Module;
    PIMAGE_EXPORT_DIRECTORY Exports;
    PULONG Names, Functions;
    PUSHORT Ordinals;
    ULONG Size, i;

    RtlInitUnicodeString(&Name, FileName);

    for (Entry = LoaderBlock->LoadOrderListHead.Flink;
         Entry != &LoaderBlock->LoadOrderListHead;
         Entry = Entry->Flink)
    {
        Module = CONTAINING_RECORD(Entry, LDR_DATA_TABLE_ENTRY, InLoadOrderLinks);
        if (!RtlEqualUnicodeString(&Module->BaseDllName, &Name, TRUE))
            continue;

        Exports = RtlImageDirectoryEntryToData(Module->DllBase, TRUE,
                                               IMAGE_DIRECTORY_ENTRY_EXPORT, &Size);
        if (!Exports)
            return NULL;

        Names = (PULONG)((PUCHAR)Module->DllBase + Exports->AddressOfNames);
        Ordinals = (PUSHORT)((PUCHAR)Module->DllBase + Exports->AddressOfNameOrdinals);
        Functions = (PULONG)((PUCHAR)Module->DllBase + Exports->AddressOfFunctions);
        for (i = 0; i < Exports->NumberOfNames; i++)
        {
            if (strcmp((PCHAR)Module->DllBase + Names[i], "KdInitializeLibrary") == 0 &&
                Ordinals[i] < Exports->NumberOfFunctions)
            {
                return (PKDNET_INITIALIZE_LIBRARY)((PUCHAR)Module->DllBase + Functions[Ordinals[i]]);
            }
        }

        return NULL;
    }

    return NULL;
}

static
VOID
KdpReleaseDevice(
    _Inout_ PDEBUG_DEVICE_DESCRIPTOR Device)
{
    ULONG i;

    for (i = 0; i < MAXIMUM_DEBUG_BARS; i++)
    {
        if (Device->BaseAddress[i].Valid && Device->BaseAddress[i].Type == CmResourceTypeMemory)
            MmUnmapIoSpace(Device->BaseAddress[i].TranslatedAddress, Device->BaseAddress[i].Length);

        Device->BaseAddress[i].Valid = FALSE;
    }

    if (Device->Memory.VirtualAddress)
    {
        MmFreeContiguousMemorySpecifyCache(Device->Memory.VirtualAddress, Device->Memory.Length,
                                           Device->Memory.Cached ? MmCached : MmNonCached);
        Device->Memory.VirtualAddress = NULL;
    }
}

static
VOID
KdpMapDevice(
    _Inout_ PDEBUG_DEVICE_DESCRIPTOR Device,
    _In_ PPCI_COMMON_HEADER Config)
{
    PHYSICAL_ADDRESS BusAddress, Translated;
    ULONG AddressSpace, Bar, Probe, High, i;
    ULONG64 Base, Length;
    USHORT Command, Disabled;
    BOOLEAN Wide;

    Command = Config->Command;
    Disabled = Command & ~(PCI_ENABLE_IO_SPACE | PCI_ENABLE_MEMORY_SPACE);
    KdpSetPciData(Device->Bus, Device->Slot, &Disabled, FIELD_OFFSET(PCI_COMMON_HEADER, Command), sizeof(Disabled));

    for (i = 0; i < PCI_TYPE0_ADDRESSES; i++)
    {
        Bar = Config->u.type0.BaseAddresses[i];
        Probe = 0xFFFFFFFF;
        KdpSetPciData(Device->Bus, Device->Slot, &Probe,
                      FIELD_OFFSET(PCI_COMMON_HEADER, u.type0.BaseAddresses[i]), sizeof(Probe));
        KdpGetPciData(Device->Bus, Device->Slot, &Probe,
                      FIELD_OFFSET(PCI_COMMON_HEADER, u.type0.BaseAddresses[i]), sizeof(Probe));
        KdpSetPciData(Device->Bus, Device->Slot, &Bar,
                      FIELD_OFFSET(PCI_COMMON_HEADER, u.type0.BaseAddresses[i]), sizeof(Bar));
        if (!Probe || Probe == 0xFFFFFFFF)
            continue;

        if (Bar & PCI_ADDRESS_IO_SPACE)
        {
            Base = Bar & PCI_ADDRESS_IO_ADDRESS_MASK;
            Length = (~(ULONG64)(Probe & PCI_ADDRESS_IO_ADDRESS_MASK) + 1) & 0xFFFF;
            AddressSpace = 1;
            Wide = FALSE;
        }
        else
        {
            Wide = (Bar & PCI_ADDRESS_MEMORY_TYPE_MASK) == PCI_TYPE_64BIT && i + 1 < PCI_TYPE0_ADDRESSES;
            Base = Bar & PCI_ADDRESS_MEMORY_ADDRESS_MASK;
            Length = (~(ULONG64)(Probe & PCI_ADDRESS_MEMORY_ADDRESS_MASK) + 1) & 0xFFFFFFFF;
            if (Wide)
            {
                High = Config->u.type0.BaseAddresses[i + 1];
                Base |= (ULONG64)High << 32;
            }

            AddressSpace = 0;
        }

        if (Base && Length)
        {
            BusAddress.QuadPart = Base;
            if (HalTranslateBusAddress(PCIBus, Device->Bus, BusAddress, &AddressSpace, &Translated))
            {
                Device->BaseAddress[i].Length = (ULONG)Length;
                if (AddressSpace)
                {
                    Device->BaseAddress[i].Type = CmResourceTypePort;
                    Device->BaseAddress[i].TranslatedAddress = (PUCHAR)(ULONG_PTR)Translated.QuadPart;
                    Device->BaseAddress[i].Valid = TRUE;
                }
                else
                {
                    Device->BaseAddress[i].Type = CmResourceTypeMemory;
                    Device->BaseAddress[i].TranslatedAddress = MmMapIoSpace(Translated, (SIZE_T)Length, MmNonCached);
                    Device->BaseAddress[i].Valid = Device->BaseAddress[i].TranslatedAddress != NULL;
                }
            }
        }

        if (Wide)
            i++;
    }

    Command |= PCI_ENABLE_MEMORY_SPACE | PCI_ENABLE_BUS_MASTER;
    KdpSetPciData(Device->Bus, Device->Slot, &Command, FIELD_OFFSET(PCI_COMMON_HEADER, Command), sizeof(Command));
}

static
NTSTATUS
KdpPrepareDevice(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _In_ PCWSTR ModuleName)
{
    PKDNET_INITIALIZE_LIBRARY InitializeLibrary;
    PHYSICAL_ADDRESS Lowest, Highest, Boundary;
    NTSTATUS Status;

    InitializeLibrary = KdpFindExtension(LoaderBlock, ModuleName);
    if (!InitializeLibrary)
        return STATUS_NOT_SUPPORTED;

    KdNicDevice.Memory.Cached = TRUE;
    KdpBuildImports();
    Status = InitializeLibrary(&KdpImports, LoaderBlock->LoadOptions, &KdNicDevice);
    if (!NT_SUCCESS(Status))
        return Status;

    if (!KdpExports.KdInitializeController || !KdpExports.KdGetRxPacket ||
        !KdpExports.KdReleaseRxPacket || !KdpExports.KdGetTxPacket ||
        !KdpExports.KdSendTxPacket || !KdpExports.KdGetPacketAddress ||
        !KdNicDevice.Memory.Length || KdNicDevice.Memory.Length > MAX_HARDWARE_CONTEXT_SIZE)
    {
        return STATUS_NOT_SUPPORTED;
    }

    Lowest.QuadPart = 0;
    Boundary.QuadPart = 0;
    Highest.QuadPart = KdNicDevice.Memory.MaxEnd.QuadPart ? KdNicDevice.Memory.MaxEnd.QuadPart : 0xFFFFFFFF;
    KdNicDevice.Memory.VirtualAddress =
        MmAllocateContiguousMemorySpecifyCache(KdNicDevice.Memory.Length, Lowest, Highest, Boundary,
                                               KdNicDevice.Memory.Cached ? MmCached : MmNonCached);
    if (!KdNicDevice.Memory.VirtualAddress)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(KdNicDevice.Memory.VirtualAddress, KdNicDevice.Memory.Length);
    KdNicDevice.Memory.Start = MmGetPhysicalAddress(KdNicDevice.Memory.VirtualAddress);
    KdNicDevice.Memory.MaxEnd = Highest;
    KdNicDevice.Memory.Aligned = TRUE;
    return STATUS_SUCCESS;
}

static
NTSTATUS
KdpStartController(VOID)
{
    NTSTATUS Status;

    KdNicDevice.Flags = DBG_DEVICE_FLAG_BARS_MAPPED | DBG_DEVICE_FLAG_SCRATCH_ALLOCATED;
    if (!KdNicDevice.Memory.Cached)
        KdNicDevice.Flags |= DBG_DEVICE_FLAG_UNCACHED_MEMORY;

    KdNicDevice.Configured = TRUE;

    RtlZeroMemory(&KdpShared, sizeof(KdpShared));
    KdpShared.Hardware = KdNicDevice.Memory.VirtualAddress;
    KdpShared.Device = &KdNicDevice;
    KdpShared.TargetMacAddress = KdNicAddress;
    KdpShared.LinkState = &KdNicLinkState;

    Status = KdpExports.KdInitializeController(&KdpShared);
    if (!NT_SUCCESS(Status))
    {
        KdpReleaseDevice(&KdNicDevice);
        return Status;
    }

    KdNicDevice.Initialized = TRUE;
    return STATUS_SUCCESS;
}

static
NTSTATUS
KdpStartDevice(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _In_ ULONG Bus,
    _In_ PCI_SLOT_NUMBER Slot,
    _In_ PPCI_COMMON_HEADER Config)
{
    WCHAR Name[KDNET_MODULE_NAME_SIZE];
    NTSTATUS Status;

    RtlZeroMemory(&KdNicDevice, sizeof(KdNicDevice));
    KdNicDevice.Bus = Bus;
    KdNicDevice.Slot = Slot.u.AsULONG;
    KdNicDevice.VendorID = Config->VendorID;
    KdNicDevice.DeviceID = Config->DeviceID;
    KdNicDevice.BaseClass = Config->BaseClass;
    KdNicDevice.SubClass = Config->SubClass;
    KdNicDevice.ProgIf = Config->ProgIf;
    KdNicDevice.NameSpace = KdNameSpacePCI;

    _snwprintf(Name, RTL_NUMBER_OF(Name), L"kd_%02x_%04x.dll", Config->BaseClass, Config->VendorID);
    Name[RTL_NUMBER_OF(Name) - 1] = UNICODE_NULL;
    Status = KdpPrepareDevice(LoaderBlock, Name);
    if (!NT_SUCCESS(Status))
        return Status;

    KdpMapDevice(&KdNicDevice, Config);
    return KdpStartController();
}

static
VOID
KdpStopDevice(VOID)
{
    if (KdpExports.KdShutdownController)
        KdpExports.KdShutdownController(KdpShared.Hardware);

    KdpReleaseDevice(&KdNicDevice);
}

static
VOID
KdpSaveDevice(VOID)
{
    KdpSavedExports = KdpExports;
    KdpSavedShared = KdpShared;
    KdpSavedDevice = KdNicDevice;
    RtlCopyMemory(KdpSavedAddress, KdNicAddress, sizeof(KdpSavedAddress));
    RtlZeroMemory(KdpSavedPath, sizeof(KdpSavedPath));
    KdpSavedBase.QuadPart = 0;
    KdpSavedValid = TRUE;
}

static
VOID
KdpRestoreDevice(VOID)
{
    KdpExports = KdpSavedExports;
    KdpShared = KdpSavedShared;
    KdNicDevice = KdpSavedDevice;
    KdpShared.Device = &KdNicDevice;
    RtlCopyMemory(KdNicAddress, KdpSavedAddress, sizeof(KdNicAddress));
    RtlCopyMemory(KdpNicPath, KdpSavedPath, sizeof(KdpNicPath));
    KdpNicBase = KdpSavedBase;
    KdpSavedValid = FALSE;
}

static
VOID
KdpDropSavedDevice(VOID)
{
    KDNET_EXTENSIBILITY_EXPORTS Exports = KdpExports;
    KDNET_SHARED_DATA Shared = KdpShared;
    DEBUG_DEVICE_DESCRIPTOR Descriptor = KdNicDevice;
    UCHAR Address[MAC_ADDRESS_SIZE];

    if (!KdpSavedValid)
        return;

    RtlCopyMemory(Address, KdNicAddress, sizeof(Address));
    KdpRestoreDevice();
    KdpStopDevice();
    KdpExports = Exports;
    KdpShared = Shared;
    KdNicDevice = Descriptor;
    KdpShared.Device = &KdNicDevice;
    RtlCopyMemory(KdNicAddress, Address, sizeof(KdNicAddress));
    KdNicLinkState = 1;
}

static
BOOLEAN
KdpStartPlatformDevice(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _In_ PKDNET_PLATFORM_DEVICE Platform)
{
    PUCHAR Registers;

    RtlZeroMemory(&KdNicDevice, sizeof(KdNicDevice));
    KdNicDevice.Bus = MAXULONG;
    KdNicDevice.Slot = MAXULONG;
    KdNicDevice.VendorID = PCI_INVALID_VENDORID;
    KdNicDevice.DeviceID = 0xFFFF;
    KdNicDevice.BaseClass = PCI_CLASS_NETWORK_CTLR;
    KdNicDevice.NameSpace = Platform->NameSpace;
    KdNicDevice.PortType = Platform->PortType;
    KdNicDevice.PortSubtype = Platform->PortSubtype;
    KdNicDevice.OemData = Platform->OemData;
    KdNicDevice.OemDataLength = Platform->OemDataLength;
    if (!NT_SUCCESS(KdpPrepareDevice(LoaderBlock, Platform->ModuleName)))
        return FALSE;

    Registers = MmMapIoSpace(Platform->Base, Platform->Length, MmNonCached);
    if (!Registers)
    {
        KdpReleaseDevice(&KdNicDevice);
        return FALSE;
    }

    KdNicDevice.BaseAddress[0].Type = CmResourceTypeMemory;
    KdNicDevice.BaseAddress[0].Valid = TRUE;
    KdNicDevice.BaseAddress[0].TranslatedAddress = Registers;
    KdNicDevice.BaseAddress[0].Length = Platform->Length;
    if (!NT_SUCCESS(KdpStartController()))
        return FALSE;

    if (KdNicLinkState)
    {
        KdpDropSavedDevice();
        RtlCopyMemory(KdpNicPath, Platform->Path, sizeof(KdpNicPath));
        KdpNicBase = Platform->Base;
        return TRUE;
    }

    if (KdpSavedValid)
    {
        KdpStopDevice();
        return FALSE;
    }

    KdpSaveDevice();
    RtlCopyMemory(KdpSavedPath, Platform->Path, sizeof(KdpSavedPath));
    KdpSavedBase = Platform->Base;
    return FALSE;
}

static
VOID
KdpReserveDevice(VOID)
{
    UNICODE_STRING Services = RTL_CONSTANT_STRING(
        L"\\Registry\\Machine\\System\\CurrentControlSet\\Services");
    PCWSTR Names[3] = { L"FDT", L"Debug", L"0" };
    UNICODE_STRING Name;
    WCHAR Path[RTL_NUMBER_OF(KdpNicPath)];
    OBJECT_ATTRIBUTES ObjectAttributes;
    HANDLE Parent, Key;
    NTSTATUS Status;
    ULONG Value, i;

    if (KdNicDevice.NameSpace == KdNameSpacePCI)
        Names[0] = L"PCI";
    else if (KdNicDevice.NameSpace == KdNameSpaceACPI && KdpNicPath[0])
        Names[0] = L"ACPI";
    else if (KdNicDevice.NameSpace != KdNameSpaceNone || !KdpNicBase.QuadPart)
        return;

    InitializeObjectAttributes(&ObjectAttributes, &Services,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    if (!NT_SUCCESS(ZwOpenKey(&Parent, KEY_ALL_ACCESS, &ObjectAttributes)))
        return;

    for (i = 0; i < RTL_NUMBER_OF(Names); i++)
    {
        RtlInitUnicodeString(&Name, Names[i]);
        InitializeObjectAttributes(&ObjectAttributes, &Name,
                                   OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, Parent, NULL);
        Status = ZwCreateKey(&Key, KEY_ALL_ACCESS, &ObjectAttributes, 0, NULL, REG_OPTION_VOLATILE, NULL);
        ZwClose(Parent);
        if (!NT_SUCCESS(Status))
            return;

        Parent = Key;
    }

    if (KdNicDevice.NameSpace == KdNameSpacePCI)
    {
        Value = KdNicDevice.Bus;
        RtlInitUnicodeString(&Name, L"Bus");
        ZwSetValueKey(Key, &Name, 0, REG_DWORD, &Value, sizeof(Value));
        Value = KdNicDevice.Slot;
        RtlInitUnicodeString(&Name, L"Slot");
        ZwSetValueKey(Key, &Name, 0, REG_DWORD, &Value, sizeof(Value));
    }
    else if (KdNicDevice.NameSpace == KdNameSpaceACPI)
    {
        for (Value = 0; KdpNicPath[Value]; Value++)
            Path[Value] = (UCHAR)KdpNicPath[Value];

        Path[Value] = UNICODE_NULL;
        RtlInitUnicodeString(&Name, L"Path");
        ZwSetValueKey(Key, &Name, 0, REG_SZ, Path, (Value + 1) * sizeof(WCHAR));
    }
    else
    {
        Value = KdpNicBase.LowPart;
        RtlInitUnicodeString(&Name, L"BaseLow");
        ZwSetValueKey(Key, &Name, 0, REG_DWORD, &Value, sizeof(Value));
        Value = KdpNicBase.HighPart;
        RtlInitUnicodeString(&Name, L"BaseHigh");
        ZwSetValueKey(Key, &Name, 0, REG_DWORD, &Value, sizeof(Value));
    }

    ZwClose(Key);
}

NTSTATUS
KdNicInitialize(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PCI_COMMON_HEADER Config;
    PCI_SLOT_NUMBER Slot;
    NTSTATUS Status = STATUS_NO_SUCH_DEVICE, Result;
    ULONG Bus, Device, Function, Length;

    for (Bus = 0; Bus < PCI_MAX_BRIDGE_NUMBER + 1; Bus++)
    {
        for (Device = 0; Device < PCI_MAX_DEVICES; Device++)
        {
            for (Function = 0; Function < PCI_MAX_FUNCTION; Function++)
            {
                Slot.u.AsULONG = 0;
                Slot.u.bits.DeviceNumber = Device;
                Slot.u.bits.FunctionNumber = Function;
                Length = KdpGetPciData(Bus, Slot.u.AsULONG, &Config, 0, PCI_COMMON_HDR_LENGTH);
                if (!Length)
                    goto NextBus;

                if (Length < PCI_COMMON_HDR_LENGTH || Config.VendorID == PCI_INVALID_VENDORID ||
                    Config.VendorID == 0)
                {
                    if (!Function)
                        break;

                    continue;
                }

                if (PCI_CONFIGURATION_TYPE(&Config) == PCI_DEVICE_TYPE &&
                    Config.BaseClass == PCI_CLASS_NETWORK_CTLR &&
                    (!KdNetOptions.HaveLocation ||
                     (KdNetOptions.Bus == Bus && KdNetOptions.Device == Device &&
                      KdNetOptions.Function == Function)))
                {
                    Result = KdpStartDevice(LoaderBlock, Bus, Slot, &Config);
                    if (NT_SUCCESS(Result) && KdNicLinkState)
                    {
                        KdpDropSavedDevice();
                        KdpNicStarted = TRUE;
                        KdpReserveDevice();
                        return STATUS_SUCCESS;
                    }

                    if (NT_SUCCESS(Result))
                    {
                        if (KdpSavedValid)
                            KdpStopDevice();
                        else
                            KdpSaveDevice();
                    }
                    else
                    {
                        Status = Result;
                    }
                }

                if (!Function && !PCI_MULTIFUNCTION_DEVICE(&Config))
                    break;
            }
        }
NextBus:
        NOTHING;
    }

    if (KdpSavedValid)
    {
        KdpRestoreDevice();
        KdNicLinkState = 0;
        KdpNicStarted = TRUE;
        KdpReserveDevice();
        return STATUS_SUCCESS;
    }

    if (!KdNetOptions.HaveLocation &&
        (KdPlatformEnumerate(LoaderBlock, KdpStartPlatformDevice) || KdpSavedValid))
    {
        if (KdpSavedValid)
        {
            KdpRestoreDevice();
            KdNicLinkState = 0;
        }

        KdpNicStarted = TRUE;
        KdpReserveDevice();
        return STATUS_SUCCESS;
    }

    return Status;
}

PUCHAR
KdNicGetTxBuffer(
    _Out_ PULONG Handle)
{
    if (!KdpNicStarted || !NT_SUCCESS(KdpExports.KdGetTxPacket(KdpShared.Hardware, Handle)))
        return NULL;

    return KdpExports.KdGetPacketAddress(KdpShared.Hardware, *Handle);
}

PUCHAR
KdNicGetTxBufferAddress(
    _In_ ULONG Handle)
{
    return KdpExports.KdGetPacketAddress(KdpShared.Hardware, Handle);
}

VOID
KdNicSend(
    _In_ ULONG Handle,
    _In_ ULONG Length)
{
    KdpExports.KdSendTxPacket(KdpShared.Hardware, Handle, Length);
}

BOOLEAN
KdNicReceive(
    _Out_ PUCHAR *Frame,
    _Out_ PULONG Length,
    _Out_ PULONG Handle)
{
    PVOID Packet;

    if (!KdpNicStarted ||
        !NT_SUCCESS(KdpExports.KdGetRxPacket(KdpShared.Hardware, Handle, &Packet, Length)))
    {
        return FALSE;
    }

    *Frame = Packet;
    return TRUE;
}

VOID
KdNicRelease(
    _In_ ULONG Handle)
{
    KdpExports.KdReleaseRxPacket(KdpShared.Hardware, Handle);
}
