/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Unit tests for the Apple silicon PMGR, DART and SPMI helpers
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>

#define WIN32_NO_STATUS
#include <ndk/ntndk.h>
#include <winreg.h>
#include <reactos/riscv64/fdtlib.h>
#include <reactos/drivers/applepmp.h>

#define UNIT_TEST

typedef struct _DEVICE_OBJECT DEVICE_OBJECT, *PDEVICE_OBJECT;

typedef enum _MEMORY_CACHING_TYPE
{
    MmNonCached = 0,
    MmCached = 1
} MEMORY_CACHING_TYPE;

typedef struct _PHYSICAL_MEMORY_RANGE
{
    PHYSICAL_ADDRESS BaseAddress;
    LARGE_INTEGER NumberOfBytes;
} PHYSICAL_MEMORY_RANGE, *PPHYSICAL_MEMORY_RANGE;

#define NonPagedPool 0
#define PagedPool 1
#define PLUGPLAY_REGKEY_DEVICE 1

static
PVOID
ExAllocatePoolWithTag(ULONG PoolType, SIZE_T NumberOfBytes, ULONG Tag)
{
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, NumberOfBytes);
}

static
VOID
ExFreePoolWithTag(PVOID Memory, ULONG Tag)
{
    HeapFree(GetProcessHeap(), 0, Memory);
}

static
VOID
ExFreePool(PVOID Memory)
{
    HeapFree(GetProcessHeap(), 0, Memory);
}

typedef struct _SIM_ALLOCATION
{
    PUCHAR Virtual;
    ULONG64 Physical;
    SIZE_T Size;
} SIM_ALLOCATION;

static SIM_ALLOCATION SimAllocations[2048];
static ULONG SimAllocationCount;
static PHYSICAL_MEMORY_RANGE SimRam[16];

static
PVOID
MmAllocateContiguousMemorySpecifyCache(SIZE_T Size, PHYSICAL_ADDRESS Low, PHYSICAL_ADDRESS High,
                                       PHYSICAL_ADDRESS Boundary, MEMORY_CACHING_TYPE Type)
{
    SIM_ALLOCATION *Allocation;

    if (SimAllocationCount == RTL_NUMBER_OF(SimAllocations))
        return NULL;
    Allocation = &SimAllocations[SimAllocationCount];
    Allocation->Virtual = HeapAlloc(GetProcessHeap(), 0, Size);
    if (!Allocation->Virtual)
        return NULL;
    Allocation->Physical = 0x2000000000ULL + ((ULONG64)SimAllocationCount << 24) + 0x1000;
    if (Boundary.QuadPart && ((Allocation->Physical ^ (Allocation->Physical + Size - 1)) & ~(ULONG64)(Boundary.QuadPart - 1)))
        Allocation->Physical &= ~(ULONG64)(Boundary.QuadPart - 1);
    Allocation->Size = Size;
    SimAllocationCount++;
    return Allocation->Virtual;
}

static
VOID
MmFreeContiguousMemorySpecifyCache(PVOID Virtual, SIZE_T Size, MEMORY_CACHING_TYPE Type)
{
    ULONG Index;

    for (Index = 0; Index < SimAllocationCount; ++Index)
    {
        if (SimAllocations[Index].Virtual == (PUCHAR)Virtual)
        {
            HeapFree(GetProcessHeap(), 0, SimAllocations[Index].Virtual);
            SimAllocations[Index].Virtual = NULL;
            SimAllocations[Index].Size = 0;
            return;
        }
    }
}

static
PHYSICAL_ADDRESS
MmGetPhysicalAddress(PVOID Virtual)
{
    PHYSICAL_ADDRESS Address;
    ULONG Index;

    Address.QuadPart = 0;
    for (Index = 0; Index < SimAllocationCount; ++Index)
    {
        if ((PUCHAR)Virtual >= SimAllocations[Index].Virtual &&
            (PUCHAR)Virtual < SimAllocations[Index].Virtual + SimAllocations[Index].Size)
        {
            Address.QuadPart = (LONGLONG)(SimAllocations[Index].Physical +
                                          ((PUCHAR)Virtual - SimAllocations[Index].Virtual));
        }
    }
    return Address;
}

static
PULONG64
SimVirtualForPhysical(ULONG64 Physical)
{
    ULONG Index;

    for (Index = 0; Index < SimAllocationCount; ++Index)
    {
        if (Physical >= SimAllocations[Index].Physical &&
            Physical < SimAllocations[Index].Physical + SimAllocations[Index].Size)
            return (PULONG64)(SimAllocations[Index].Virtual + (Physical - SimAllocations[Index].Physical));
    }
    return NULL;
}

static
PPHYSICAL_MEMORY_RANGE
MmGetPhysicalMemoryRanges(VOID)
{
    PPHYSICAL_MEMORY_RANGE Copy = HeapAlloc(GetProcessHeap(), 0, sizeof(SimRam));

    if (Copy)
        memcpy(Copy, SimRam, sizeof(SimRam));
    return Copy;
}

static
NTSTATUS
MockZwOpenKey(PHANDLE Key, ACCESS_MASK Access, POBJECT_ATTRIBUTES Attributes)
{
    return STATUS_OBJECT_NAME_NOT_FOUND;
}

static
NTSTATUS
MockZwQueryValueKey(HANDLE Key, PUNICODE_STRING Name, KEY_VALUE_INFORMATION_CLASS Class, PVOID Information,
                    ULONG Length, PULONG ResultLength)
{
    return STATUS_OBJECT_NAME_NOT_FOUND;
}

static
NTSTATUS
MockZwClose(HANDLE Handle)
{
    return STATUS_SUCCESS;
}

static
NTSTATUS
MockIoOpenDeviceRegistryKey(PDEVICE_OBJECT DeviceObject, ULONG KeyType, ACCESS_MASK Access, PHANDLE Key)
{
    return STATUS_NOT_IMPLEMENTED;
}

#define ZwOpenKey MockZwOpenKey
#define ZwQueryValueKey MockZwQueryValueKey
#define ZwClose MockZwClose
#define IoOpenDeviceRegistryKey MockIoOpenDeviceRegistryKey

#define SIM_PMGR_BASE       0x292280000ULL
#define SIM_DART_BASE       0x37a808000ULL
#define SIM_SPMI_BASE       0x364714000ULL
#define SIM_REGION_SIZE     0x4000
#define SIM_ATC_CORE_BASE   0x703000000ULL
#define SIM_ATC_CORE_SIZE   0x4c000
#define SIM_ATC_AXI2AF_BASE 0x700000000ULL
#define SIM_ATC_AXI2AF_SIZE 0x8000
#define SIM_ATC_USB2_BASE   0x702a90000ULL
#define SIM_ATC_PIPE_BASE   0x702a84000ULL
#define SIM_PCIE_PORT0_BASE 0x594008000ULL
#define SIM_PCIE_PHY0_BASE  0x59e020000ULL
#define SIM_PINCTRL_BASE    0x347100000ULL
#define SIM_PCIE_CFG_BASE   0x580100000ULL
#define SIM_I2C_BASE        0x289018000ULL
#define SIM_NCO_BASE        0x350044000ULL
#define SIM_NCO_SIZE        0x14000
#define SIM_ADMAC_BASE      0x28b200000ULL
#define SIM_ADMAC_SIZE      0x34000
#define SIM_MCA_BASE        0x28b400000ULL
#define SIM_MCA_SIZE        0x10000
#define SIM_MCA_SWITCH_BASE 0x28b300000ULL
#define SIM_MCA_SWITCH_SIZE 0x20000
#define SIM_I2C_INDEX       11
#define SIM_NCO_INDEX       12
#define SIM_ADMAC_INDEX     13
#define SIM_MCA_INDEX       14
#define SIM_SWITCH_INDEX    15

typedef struct _SIM_REGION
{
    ULONG64 Physical;
    ULONG Size;
    ULONG *Data;
} SIM_REGION;

static ULONG SimPmgrData[SIM_REGION_SIZE / sizeof(ULONG)];
static ULONG SimDartData[SIM_REGION_SIZE / sizeof(ULONG)];
static ULONG SimSpmiData[SIM_REGION_SIZE / sizeof(ULONG)];
static ULONG SimAtcCoreData[SIM_ATC_CORE_SIZE / sizeof(ULONG)];
static ULONG SimAtcAxi2afData[SIM_ATC_AXI2AF_SIZE / sizeof(ULONG)];
static ULONG SimAtcUsb2Data[SIM_REGION_SIZE / sizeof(ULONG)];
static ULONG SimAtcPipeData[SIM_REGION_SIZE / sizeof(ULONG)];
static ULONG SimPciePortData[SIM_REGION_SIZE / sizeof(ULONG)];
static ULONG SimPciePhyData[SIM_REGION_SIZE / sizeof(ULONG)];
static ULONG SimPinctrlData[SIM_REGION_SIZE / sizeof(ULONG)];
static ULONG SimPcieConfigData[SIM_REGION_SIZE / sizeof(ULONG)];
static ULONG SimI2cData[SIM_REGION_SIZE / sizeof(ULONG)];
static ULONG SimNcoData[SIM_NCO_SIZE / sizeof(ULONG)];
static ULONG SimAdmacData[SIM_ADMAC_SIZE / sizeof(ULONG)];
static ULONG SimMcaData[SIM_MCA_SIZE / sizeof(ULONG)];
static ULONG SimMcaSwitchData[SIM_MCA_SWITCH_SIZE / sizeof(ULONG)];

typedef struct _SIM_WRITE
{
    ULONG64 Physical;
    ULONG Value;
} SIM_WRITE;

static SIM_REGION SimRegions[] =
{
    { SIM_PMGR_BASE, sizeof(SimPmgrData), SimPmgrData },
    { SIM_DART_BASE, sizeof(SimDartData), SimDartData },
    { SIM_SPMI_BASE, sizeof(SimSpmiData), SimSpmiData },
    { SIM_ATC_CORE_BASE, sizeof(SimAtcCoreData), SimAtcCoreData },
    { SIM_ATC_AXI2AF_BASE, sizeof(SimAtcAxi2afData), SimAtcAxi2afData },
    { SIM_ATC_USB2_BASE, sizeof(SimAtcUsb2Data), SimAtcUsb2Data },
    { SIM_ATC_PIPE_BASE, sizeof(SimAtcPipeData), SimAtcPipeData },
    { SIM_PCIE_PORT0_BASE, sizeof(SimPciePortData), SimPciePortData },
    { SIM_PCIE_PHY0_BASE, sizeof(SimPciePhyData), SimPciePhyData },
    { SIM_PINCTRL_BASE, sizeof(SimPinctrlData), SimPinctrlData },
    { SIM_PCIE_CFG_BASE, sizeof(SimPcieConfigData), SimPcieConfigData },
    { SIM_I2C_BASE, sizeof(SimI2cData), SimI2cData },
    { SIM_NCO_BASE, sizeof(SimNcoData), SimNcoData },
    { SIM_ADMAC_BASE, sizeof(SimAdmacData), SimAdmacData },
    { SIM_MCA_BASE, sizeof(SimMcaData), SimMcaData },
    { SIM_MCA_SWITCH_BASE, sizeof(SimMcaSwitchData), SimMcaSwitchData },
};
static SIM_WRITE SimWrites[256];
static BOOLEAN SimAtcStuck;
static BOOLEAN SimPcieNoLink;
static ULONG SimSleptMs;
static ULONG SimWriteCount;
static ULONG SimMapCount;
static BOOLEAN SimPmgrStuck;
static UCHAR SimNvmem[0x10000];
static ULONG SimFifo[32];
static ULONG SimFifoHead;
static ULONG SimFifoTail;
static BOOLEAN SimSpmiSilent;
static BOOLEAN SimSpmiBadParity;
static ULONG SimSpmiCommands[16];
static ULONG SimSpmiCommandCount;
static BOOLEAN SimSpmiNoAck;
static ULONG SimSpmiWritePending;
static ULONG SimSpmiWriteAddress;
static ULONG SimSpmiWriteLength;
static ULONG SimSpmiWriteDone;
static ULONG SimSpmiWriteCommand;
static BOOLEAN SimAceEnabled;
#define SIM_ACE_WINDOW_SIZE 0x40
#define SIM_ACE_READ_WINDOW 0x20
#define SIM_ACE_WRITE_WINDOW 0xa0

static UCHAR SimAceRegs[256][SIM_ACE_WINDOW_SIZE];
static UCHAR SimAceLength[256];
static UCHAR SimAceSelected;
static UCHAR SimAceRegister0;
static ULONG SimAceBusyReads;
static BOOLEAN SimAceWrongEcho;
static BOOLEAN SimAceRejectCommand;
static BOOLEAN SimAceNoWake;
static ULONG SimAceWakeInterrupt;
static UCHAR SimAceWriteBuffer[SIM_ACE_WINDOW_SIZE];

#define SIM_CODEC_ADDRESS   0x4b

typedef struct _SIM_ADMAC_CHANNEL
{
    ULONG Staging[4];
    ULONG StagingCount;
    ULONG Write;
    ULONG Read;
    ULONG Count;
    ULONG ReportWrite;
    ULONG ReportRead;
    ULONG ReportCount;
    ULONG ReportWord;
} SIM_ADMAC_CHANNEL;

static UCHAR SimCodec[0x8000];
static ULONG SimI2cRx[16];
static ULONG SimI2cRxHead;
static ULONG SimI2cRxTail;
static ULONG SimI2cBytes;
static ULONG SimI2cPointer;
static BOOLEAN SimI2cNack;
static ULONG SimI2cTx[64];
static ULONG SimI2cTxCount;
static SIM_ADMAC_CHANNEL SimAdmacChannels[8];
static ULONG SimAdmacPushed[16][4];
static ULONG SimAdmacPushedCount;
static BOOLEAN SimMcaStuck;

static
ULONG
SimI2cRead(ULONG Offset)
{
    ULONG *Data = SimRegions[SIM_I2C_INDEX].Data;

    if (Offset == 0x04)
        return SimI2cRxHead == SimI2cRxTail ? 0x100 : SimI2cRx[SimI2cRxHead++ % RTL_NUMBER_OF(SimI2cRx)];
    if (Offset == 0x14)
        return Data[0x14 / sizeof(ULONG)] | 0x10000 | (SimI2cRxHead != SimI2cRxTail ? 0x80000 : 0);
    return Data[Offset / sizeof(ULONG)];
}

static
VOID
SimI2cWrite(ULONG Offset, ULONG Value)
{
    ULONG *Data = SimRegions[SIM_I2C_INDEX].Data, Index;

    if (Offset == 0x14)
    {
        Data[0x14 / sizeof(ULONG)] &= ~Value;
        return;
    }
    if (Offset == 0x1c && (Value & 0x600))
        SimI2cRxHead = SimI2cRxTail = 0;
    if (Offset != 0x00)
    {
        Data[Offset / sizeof(ULONG)] = Value;
        return;
    }
    if (SimI2cTxCount < RTL_NUMBER_OF(SimI2cTx))
        SimI2cTx[SimI2cTxCount++] = Value;
    if (Value & 0x100)
    {
        SimI2cNack = ((Value & 0xff) >> 1) != SIM_CODEC_ADDRESS;
        SimI2cBytes = 0;
        return;
    }
    if (Value & 0x400)
    {
        for (Index = 0; !SimI2cNack && Index < (Value & 0xff); ++Index)
            SimI2cRx[SimI2cRxTail++ % RTL_NUMBER_OF(SimI2cRx)] = SimCodec[SimI2cPointer++ % sizeof(SimCodec)];
        Data[0x14 / sizeof(ULONG)] |= 0x08000000 | (SimI2cNack ? 0x200000 : 0);
        return;
    }
    if (!SimI2cNack)
    {
        if (SimI2cBytes == 0)
            SimI2cPointer = (Value & 0xff) << 8;
        else if (SimI2cBytes == 1)
            SimI2cPointer |= Value & 0xff;
        else
            SimCodec[SimI2cPointer++ % sizeof(SimCodec)] = (UCHAR)Value;
        SimI2cBytes++;
    }
    if (Value & 0x200)
        Data[0x14 / sizeof(ULONG)] |= 0x08000000 | (SimI2cNack ? 0x200000 : 0);
}

static
ULONG
SimAdmacRing(ULONG Write, ULONG Read, ULONG Count)
{
    return (Write & 3) | ((Read & 3) << 4) | (Count == 0 ? 0x100 : 0) | (Count == 4 ? 0x200 : 0);
}

static
ULONG
SimAdmacChannelOf(ULONG Offset, ULONG Base)
{
    return ((Offset & ~0x4000) - Base) / 4 * 2 + ((Offset & 0x4000) ? 1 : 0);
}

static
ULONG
SimAdmacRead(ULONG Offset)
{
    ULONG *Data = SimRegions[SIM_ADMAC_INDEX].Data, Channel, Register, Value = 0;
    SIM_ADMAC_CHANNEL *Sim;

    if (Offset >= 0x8000 && Offset < 0x8000 + 8 * 0x200)
    {
        Channel = (Offset - 0x8000) / 0x200;
        Register = (Offset - 0x8000) % 0x200;
        Sim = &SimAdmacChannels[Channel];
        if (Register == 0x70)
            return SimAdmacRing(Sim->Write, Sim->Read, Sim->Count);
        if (Register == 0x74)
            return SimAdmacRing(Sim->ReportWrite, Sim->ReportRead, Sim->ReportCount);
    }
    if ((Offset & ~0x4000) >= 0x10100 && (Offset & ~0x4000) < 0x10110)
    {
        Sim = &SimAdmacChannels[SimAdmacChannelOf(Offset, 0x10100)];
        if (!Sim->ReportCount)
            return 0;
        if (++Sim->ReportWord == 4)
        {
            Sim->ReportWord = 0;
            Sim->ReportRead++;
            Sim->ReportCount--;
        }
        return 0x1000 + Sim->ReportWord;
    }
    if (Offset >= 0x30 && Offset < 0x40)
    {
        for (Channel = 0; Channel < 8; Channel += 2)
        {
            Register = 0x8000 + Channel * 0x200 + (Offset - 0x30);
            if (Data[(Register + 0x10) / sizeof(ULONG)] & Data[(Register + 0x20) / sizeof(ULONG)])
                Value |= 1UL << (Channel / 2);
        }
        return Value;
    }
    return Data[Offset / sizeof(ULONG)];
}

static
VOID
SimAdmacWrite(ULONG Offset, ULONG Value)
{
    ULONG *Data = SimRegions[SIM_ADMAC_INDEX].Data, Channel, Register;
    SIM_ADMAC_CHANNEL *Sim;

    if ((Offset & ~0x4000) >= 0x10000 && (Offset & ~0x4000) < 0x10010)
    {
        Sim = &SimAdmacChannels[SimAdmacChannelOf(Offset, 0x10000)];
        Sim->Staging[Sim->StagingCount++] = Value;
        if (Sim->StagingCount == 4)
        {
            Sim->StagingCount = 0;
            if (Sim->Count < 4)
            {
                Sim->Write++;
                Sim->Count++;
                if (SimAdmacPushedCount < RTL_NUMBER_OF(SimAdmacPushed))
                    memcpy(SimAdmacPushed[SimAdmacPushedCount++], Sim->Staging, sizeof(Sim->Staging));
            }
        }
        return;
    }
    if (Offset >= 0x8000 && Offset < 0x8000 + 8 * 0x200)
    {
        Channel = (Offset - 0x8000) / 0x200;
        Register = (Offset - 0x8000) % 0x200;
        if (Register == 0x00 && (Value & 1))
            memset(&SimAdmacChannels[Channel], 0, sizeof(SimAdmacChannels[Channel]));
        if (Register >= 0x10 && Register < 0x20)
        {
            Data[Offset / sizeof(ULONG)] &= ~Value;
            return;
        }
    }
    if (Offset >= 0x50 && Offset < 0x60)
    {
        Data[Offset / sizeof(ULONG)] &= ~Value;
        return;
    }
    Data[Offset / sizeof(ULONG)] = Value;
}

static
VOID
SimAdmacComplete(ULONG Channel, ULONG Count)
{
    SIM_ADMAC_CHANNEL *Sim = &SimAdmacChannels[Channel];

    while (Count-- && Sim->Count)
    {
        Sim->Read++;
        Sim->Count--;
        if (Sim->ReportCount < 4)
        {
            Sim->ReportWrite++;
            Sim->ReportCount++;
        }
        SimRegions[SIM_ADMAC_INDEX].Data[(0x8000 + Channel * 0x200 + 0x14) / sizeof(ULONG)] |= 1;
    }
}

static
SIM_REGION *
SimFind(ULONG64 Physical, PULONG Offset)
{
    ULONG Index;

    for (Index = 0; Index < RTL_NUMBER_OF(SimRegions); ++Index)
    {
        if (Physical >= SimRegions[Index].Physical && Physical - SimRegions[Index].Physical < SimRegions[Index].Size)
        {
            *Offset = (ULONG)(Physical - SimRegions[Index].Physical);
            return &SimRegions[Index];
        }
    }
    return NULL;
}

static
SIM_REGION *
SimFromVirtual(volatile ULONG *Address, PULONG Offset)
{
    ULONG Index;
    PUCHAR Pointer = (PUCHAR)Address;

    for (Index = 0; Index < RTL_NUMBER_OF(SimRegions); ++Index)
    {
        PUCHAR Base = (PUCHAR)SimRegions[Index].Data;

        if (Pointer >= Base && Pointer < Base + SimRegions[Index].Size)
        {
            *Offset = (ULONG)(Pointer - Base);
            return &SimRegions[Index];
        }
    }
    return NULL;
}

static
PVOID
MmMapIoSpace(PHYSICAL_ADDRESS Physical, SIZE_T Size, MEMORY_CACHING_TYPE Type)
{
    ULONG Offset;
    SIM_REGION *Region = SimFind((ULONG64)Physical.QuadPart, &Offset);
    PVOID Allocated;

    if (!Region || Offset + Size > Region->Size)
    {
        /* Memory a coprocessor owns is mapped the same way as a register window. */
        Allocated = SimVirtualForPhysical((ULONG64)Physical.QuadPart);
        if (!Allocated)
            return NULL;
        SimMapCount++;
        return Allocated;
    }
    SimMapCount++;
    return (PUCHAR)Region->Data + Offset;
}

static
VOID
MmUnmapIoSpace(PVOID Address, SIZE_T Size)
{
    SimMapCount--;
}

static
VOID
SimPush(ULONG Value)
{
    SimFifo[SimFifoTail++ % RTL_NUMBER_OF(SimFifo)] = Value;
}

static
ULONG
SimRead(volatile ULONG *Address)
{
    ULONG Offset;
    SIM_REGION *Region = SimFromVirtual(Address, &Offset);

    if (!Region)
        return 0xdeadbeef;
    if (Region == &SimRegions[SIM_I2C_INDEX])
        return SimI2cRead(Offset);
    if (Region == &SimRegions[SIM_ADMAC_INDEX])
        return SimAdmacRead(Offset);
    if (Region == &SimRegions[2])
    {
        if (Offset == 0x00)
            return SimFifoHead == SimFifoTail ? 0x01000000 : 0;
        if (Offset == 0x08)
            return SimFifoHead == SimFifoTail ? 0 : SimFifo[SimFifoHead++ % RTL_NUMBER_OF(SimFifo)];
    }
    return Region->Data[Offset / sizeof(ULONG)];
}

static
VOID
SimAceCommit(ULONG Length)
{
    ULONG Index;

    if (SimAceSelected == 0x08 && Length == 4 && !memcmp(SimAceWriteBuffer, "SSPS", 4))
    {
        if (SimAceRejectCommand)
        {
            memcpy(SimAceRegs[0x08], "!CMD", 4);
            return;
        }
        SimAceRegs[0x20][0] = SimAceRegs[0x09][0];
        memset(SimAceRegs[0x08], 0, 4);
        SimAceRegs[0x09][0] = 0;
        return;
    }
    if (SimAceSelected == 0x18)
    {
        for (Index = 0; Index < Length && Index < 8; ++Index)
            SimAceRegs[0x14][Index] &= ~SimAceWriteBuffer[Index];
        return;
    }
    memcpy(SimAceRegs[SimAceSelected], SimAceWriteBuffer, Length);
    SimAceLength[SimAceSelected] = (UCHAR)Length;
}

static
BOOLEAN
SimAceCommand(ULONG Command)
{
    ULONG Opcode = Command & 0xff, Address = Command >> 16, Length, Index, Word;

    if (!(Command & 0x8000))
    {
        if (SimSpmiCommandCount < RTL_NUMBER_OF(SimSpmiCommands))
            SimSpmiCommands[SimSpmiCommandCount++] = Command;
        return TRUE;
    }
    if (Opcode == 0x13)
    {
        if (SimSpmiCommandCount < RTL_NUMBER_OF(SimSpmiCommands))
            SimSpmiCommands[SimSpmiCommandCount++] = Command;
        if (!SimAceNoWake)
            SimSpmiData[(0x60 + (SimAceWakeInterrupt / 32) * 4) / sizeof(ULONG)] |= 1UL << (SimAceWakeInterrupt % 32);
        SimPush(0x8000 | (Command & 0x7fff));
        return TRUE;
    }
    if (Opcode & 0x80)
    {
        if (SimSpmiCommandCount < RTL_NUMBER_OF(SimSpmiCommands))
            SimSpmiCommands[SimSpmiCommandCount++] = Command;
        if (Address != ((Opcode & 0x7f) << 8))
            return TRUE;
        SimAceSelected = (UCHAR)(Opcode & 0x7f);
        SimAceRegister0 = SimAceWrongEcho ? (UCHAR)(SimAceSelected ^ 1) : SimAceSelected;
        SimPush(0x8000 | (Command & 0x7fff));
        return TRUE;
    }
    if ((Opcode & 0xe0) == 0x60)
    {
        if (SimSpmiCommandCount < RTL_NUMBER_OF(SimSpmiCommands))
            SimSpmiCommands[SimSpmiCommandCount++] = Command;
        if (Address != (Opcode & 0x1f))
            return TRUE;
        SimPush(0xffff8000 | Opcode);
        SimPush((Address == 0 && SimAceBusyReads) ? (SimAceBusyReads--, SimAceRegister0 | 0x80) : SimAceRegister0);
        return TRUE;
    }
    Length = (Opcode & 7) + 1;
    if ((Opcode & 0xf8) == 0x38 && (Command & 0x8000) && Address >= SIM_ACE_READ_WINDOW &&
        Address + Length <= SIM_ACE_READ_WINDOW + SIM_ACE_WINDOW_SIZE)
    {
        if (SimSpmiCommandCount < RTL_NUMBER_OF(SimSpmiCommands))
            SimSpmiCommands[SimSpmiCommandCount++] = Command;
        SimPush(0xffff8000 | Opcode);
        for (Index = 0; Index < Length; Index += 4)
        {
            Word = 0;
            memcpy(&Word, &SimAceRegs[SimAceSelected][Address - SIM_ACE_READ_WINDOW + Index], min(4, Length - Index));
            SimPush(Word);
        }
        return TRUE;
    }
    return FALSE;
}

static
VOID
SimSpmiCommand(ULONG Command)
{
    ULONG Opcode = Command & 0xff, Sid = (Command >> 8) & 0x7f, Address = Command >> 16, Length, Index, Word;

    if (SimAceEnabled && !SimSpmiWritePending && SimAceCommand(Command))
        return;
    if (SimSpmiWritePending)
    {
        for (Index = 0; Index < 4 && SimSpmiWriteDone < SimSpmiWriteLength; ++Index)
        {
            UCHAR Byte = (UCHAR)(Command >> (8 * Index));
            ULONG Target = SimSpmiWriteAddress + SimSpmiWriteDone++;

            if (SimAceEnabled && Target >= SIM_ACE_WRITE_WINDOW && Target < SIM_ACE_WRITE_WINDOW + SIM_ACE_WINDOW_SIZE)
                SimAceWriteBuffer[Target - SIM_ACE_WRITE_WINDOW] = Byte;
            else
                SimNvmem[Target] = Byte;
        }
        if (--SimSpmiWritePending == 0)
        {
            if (SimAceEnabled && SimSpmiWriteAddress >= SIM_ACE_WRITE_WINDOW &&
                SimSpmiWriteAddress + SimSpmiWriteLength <= SIM_ACE_WRITE_WINDOW + SIM_ACE_WINDOW_SIZE)
                SimAceCommit(SimSpmiWriteAddress + SimSpmiWriteLength - SIM_ACE_WRITE_WINDOW);
            if (!SimSpmiSilent)
                SimPush((SimSpmiNoAck ? 0 : 0x8000) | (SimSpmiWriteCommand & 0x7fff));
        }
        return;
    }
    if (SimSpmiCommandCount < RTL_NUMBER_OF(SimSpmiCommands))
        SimSpmiCommands[SimSpmiCommandCount++] = Command;
    if ((Opcode & 0xf8) == 0x30 && (Command & 0x8000))
    {
        SimSpmiWriteCommand = Command;
        SimSpmiWriteAddress = Address;
        SimSpmiWriteLength = (Opcode & 7) + 1;
        SimSpmiWriteDone = 0;
        SimSpmiWritePending = (SimSpmiWriteLength + 3) / 4;
        return;
    }
    if (SimSpmiSilent || (Opcode & 0xf8) != 0x38 || !(Command & 0x8000))
        return;
    Length = (Opcode & 7) + 1;
    SimPush(((SimSpmiBadParity ? 0xfffe : 0xffff) << 16) | 0x8000 | (Sid << 8) | Opcode);
    for (Index = 0; Index < Length; Index += 4)
    {
        Word = 0;
        memcpy(&Word, &SimNvmem[Address + Index], min(4, Length - Index));
        SimPush(Word);
    }
}

static
VOID
SimWrite(volatile ULONG *Address, ULONG Value)
{
    ULONG Offset;
    SIM_REGION *Region = SimFromVirtual(Address, &Offset);

    if (!Region)
        return;
    if (SimWriteCount < RTL_NUMBER_OF(SimWrites))
    {
        SimWrites[SimWriteCount].Physical = Region->Physical + Offset;
        SimWrites[SimWriteCount++].Value = Value;
    }
    if (Region == &SimRegions[SIM_I2C_INDEX])
    {
        SimI2cWrite(Offset, Value);
        return;
    }
    if (Region == &SimRegions[SIM_ADMAC_INDEX])
    {
        SimAdmacWrite(Offset, Value);
        return;
    }
    if (Region == &SimRegions[SIM_MCA_INDEX] && ((Offset % 0x4000) == 0x300 || (Offset % 0x4000) == 0x400) &&
        (Value & 2) && !SimMcaStuck)
        Value &= ~2;
    if (Region == &SimRegions[0])
    {
        if (!SimPmgrStuck)
            Value = (Value & ~0xf0) | ((Value & 0xf) << 4);
    }
    else if (Region == &SimRegions[1])
    {
        if (Offset == 0x80)
            Value &= ~0x80000000;
    }
    else if (Region == &SimRegions[2] && Offset == 0x04)
    {
        SimSpmiCommand(Value);
        return;
    }
    else if (Region == &SimRegions[2] && Offset >= 0x60 && Offset < 0xa0)
    {
        Region->Data[Offset / sizeof(ULONG)] &= ~Value;
        return;
    }
    else if (Region == &SimRegions[3] && Offset == 0x20000 && !SimAtcStuck)
    {
        Region->Data[0x20004 / sizeof(ULONG)] = Value & 3;
    }
    else if (Region == &SimRegions[6] && Offset == 0x10)
    {
        Region->Data[0x14 / sizeof(ULONG)] = Value & 1;
    }
    else if (Region == &SimRegions[7])
    {
        if (Offset == 0x82c && (Value & 1))
            Region->Data[0x804 / sizeof(ULONG)] |= 1;
        else if (Offset == 0x80 && (Value & 1) && !SimPcieNoLink)
            Region->Data[0x208 / sizeof(ULONG)] |= 1;
        else if (Offset >= 0x3000 + 16 * 4 && Offset < 0x3800)
            return;
    }
    else if (Region == &SimRegions[8] && Offset == 0)
    {
        Value |= (Value & 3) << 2;
    }
    Region->Data[Offset / sizeof(ULONG)] = Value;
}

static ULONG SimStalledMicroseconds;

#define READ_REGISTER_ULONG(Address) SimRead(Address)
#define WRITE_REGISTER_ULONG(Address, Value) SimWrite(Address, Value)
#define KeStallExecutionProcessor(Microseconds) (SimStalledMicroseconds += (Microseconds))
#define KeMemoryBarrier() ((void)0)
#define KeQueryInterruptTime() ((ULONG64)(SimSleptMs * 10000ULL + SimStalledMicroseconds * 10ULL))

static
VOID
PcieSleepMs(ULONG Milliseconds)
{
    SimSleptMs += Milliseconds;
}

static
VOID
UatSleepMs(ULONG Milliseconds)
{
    SimSleptMs += Milliseconds;
}

#include "../../../../sdk/lib/drivers/fdtdrv/fdtdrv.c"
#include "../../../../sdk/lib/drivers/applertkit/pmgr.c"
#include "../../../../sdk/lib/drivers/applertkit/dart.c"
#include "../../../../sdk/lib/drivers/applertkit/uat.c"
#include "../../../../sdk/lib/drivers/applertkit/spmi.c"
#include "../../../../sdk/lib/drivers/applertkit/atcphy.c"
#include "../../../../sdk/lib/drivers/applertkit/pcie.c"
#include "../../../../sdk/lib/drivers/applertkit/gpio.c"
#include "../../../../sdk/lib/drivers/applertkit/i2c.c"
#include "../../../../sdk/lib/drivers/applertkit/nco.c"
#include "../../../../sdk/lib/drivers/applertkit/admac.c"
#include "../../../../sdk/lib/drivers/applertkit/mca.c"

static
VOID
AcePdSleepMs(ULONG Milliseconds)
{
    SimSleptMs += Milliseconds;
}

#include "../../../../sdk/lib/drivers/applertkit/acepd.c"
#include "../hal/fdtbuild.h"

#define PHANDLE_LW31        0x51
#define PHANDLE_ANS         0x58
#define PHANDLE_APCIE       0x82
#define PHANDLE_DART0       0x8f
#define PHANDLE_DART1       0x90
#define PHANDLE_RTC_OFFSET  0x75
#define PHANDLE_BOOT_ERRORS 0x73
#define PHANDLE_PANICS      0x74
#define PHANDLE_ATCPHY      0x92
#define PHANDLE_PINCTRL     0x17
#define PHANDLE_PCIE_PINS   0x89
#define PHANDLE_SMC_GPIO    0x8b

static
VOID
BuildSocTree(VOID)
{
    memset(&Builder, 0, sizeof(Builder));
    FdtBeginNode("");
    FdtCells("#address-cells", 1, 2);
    FdtCells("#size-cells", 1, 2);
    FdtBeginNode("soc");
    FdtStrings("compatible", LIST("simple-bus"));
    FdtCells("#address-cells", 1, 2);
    FdtCells("#size-cells", 1, 2);
    FdtProperty("ranges", NULL, 0);

    FdtBeginNode("power-management@292280000");
    FdtStrings("compatible", LIST("apple,t6030-pmgr\0apple,t8103-pmgr\0syscon\0simple-mfd"));
    FdtCells("#address-cells", 1, 1);
    FdtCells("#size-cells", 1, 1);
    FdtCells("reg", 4, 2, 0x92280000, 0, 0x4000);
    FdtBeginNode("power-controller@4d8");
    FdtStrings("compatible", LIST("apple,t6030-pmgr-pwrstate\0apple,t8103-pmgr-pwrstate"));
    FdtCells("reg", 2, 0x4d8, 4);
    FdtStrings("label", LIST("lw31"));
    FdtCells("phandle", 1, PHANDLE_LW31);
    FdtEndNode();
    FdtBeginNode("power-controller@510");
    FdtStrings("compatible", LIST("apple,t6030-pmgr-pwrstate\0apple,t8103-pmgr-pwrstate"));
    FdtCells("reg", 2, 0x510, 4);
    FdtStrings("label", LIST("ans"));
    FdtCells("power-domains", 1, PHANDLE_LW31);
    FdtCells("phandle", 1, PHANDLE_ANS);
    FdtEndNode();
    FdtBeginNode("power-controller@5b0");
    FdtStrings("compatible", LIST("apple,t6030-pmgr-pwrstate\0apple,t8103-pmgr-pwrstate"));
    FdtCells("reg", 2, 0x5b0, 4);
    FdtStrings("label", LIST("apcie_sys_st"));
    FdtCells("power-domains", 1, PHANDLE_ANS);
    FdtCells("phandle", 1, PHANDLE_APCIE);
    FdtEndNode();
    FdtEndNode();

    FdtBeginNode("nvme@38dcc0000");
    FdtStrings("compatible", LIST("apple,t6030-nvme-ans3\0apple,t8103-nvme-ans2"));
    FdtCells("reg", 4, 3, 0x8dcc0000, 0, 0x60000);
    FdtCells("power-domains", 2, PHANDLE_ANS, PHANDLE_APCIE);
    FdtCells("resets", 1, PHANDLE_ANS);
    FdtEndNode();

    FdtBeginNode("iommu@37a808000");
    FdtStrings("compatible", LIST("apple,t6030-dart\0apple,t8110-dart"));
    FdtCells("reg", 4, 3, 0x7a808000, 0, 0x4000);
    FdtCells("#iommu-cells", 1, 1);
    FdtCells("phandle", 1, PHANDLE_DART0);
    FdtEndNode();

    FdtBeginNode("usb@702280000");
    FdtStrings("compatible", LIST("apple,t6030-dwc3\0apple,t8103-dwc3"));
    FdtCells("iommus", 4, PHANDLE_DART0, 0, PHANDLE_DART0, 1);
    FdtCells("phys", 4, PHANDLE_ATCPHY, 3, PHANDLE_ATCPHY, 4);
    FdtEndNode();

    FdtBeginNode("phy@703000000");
    FdtStrings("compatible", LIST("apple,t6030-atcphy\0apple,t8122-atcphy"));
    FdtCells("reg", 20, 7, 0x3000000, 0, 0x4c000, 7, 0x3050000, 0, 0x8000, 7, 0, 0, 0x8000,
             7, 0x2a90000, 0, 0x4000, 7, 0x2a84000, 0, 0x4000);
    FdtStrings("reg-names", LIST("core\0lpdptx\0axi2af\0usb2phy\0pipehandler"));
    FdtCells("apple,tunable-common-a", 6, 0x20, 0xff, 0x12, 0x2a04, 0x7c, 0x28);
    FdtCells("apple,tunable-axi2af", 3, 0x100, 0xf0, 0x30);
    FdtCells("apple,tunable-common-b", 6, 0x24, 0xff, 0x34, 0x4c000, 0xff, 0x1);
    FdtCells("phandle", 1, PHANDLE_ATCPHY);
    FdtEndNode();

    FdtBeginNode("pinctrl@347100000");
    FdtStrings("compatible", LIST("apple,t6030-pinctrl\0apple,t8103-pinctrl"));
    FdtCells("reg", 4, 3, 0x47100000, 0, 0x4000);
    FdtCells("phandle", 1, PHANDLE_PINCTRL);
    FdtBeginNode("pcie-pins");
    FdtCells("pinmux", 4, 0x100a3, 0x100a4, 0x100a5, 0x100a6);
    FdtCells("phandle", 1, PHANDLE_PCIE_PINS);
    FdtEndNode();
    FdtEndNode();

    FdtBeginNode("smc@36c400000");
    FdtStrings("compatible", LIST("apple,t6030-smc\0apple,t8103-smc"));
    FdtBeginNode("gpio@67500000");
    FdtStrings("compatible", LIST("apple,smc-gpio"));
    FdtCells("phandle", 1, PHANDLE_SMC_GPIO);
    FdtEndNode();
    FdtEndNode();

    FdtBeginNode("pcie@580000000");
    FdtStrings("compatible", LIST("apple,t6030-pcie\0apple,t6020-pcie"));
    FdtStrings("device_type", LIST("pci"));
    FdtCells("reg", 16, 5, 0x80000000, 0, 0x1000000, 5, 0x91000000, 0, 0x4000, 5, 0x94008000, 0, 0x4000,
             5, 0x9e020000, 0, 0x4000);
    FdtStrings("reg-names", LIST("config\0rc\0port0\0phy0"));
    FdtCells("msi-ranges", 5, 0x13, 0, 0x53e, 1, 0x20);
    FdtCells("bus-range", 2, 0, 4);
    FdtCells("ranges", 14, 0x43000000, 5, 0xa0000000, 5, 0xa0000000, 0, 0x20000000,
             0x02000000, 0, 0xc0000000, 5, 0xc0000000, 0, 0x40000000);
    FdtCells("iommu-map", 8, 0x100, PHANDLE_DART0, 1, 1, 0x200, PHANDLE_DART0, 2, 1);
    FdtCells("iommu-map-mask", 1, 0xff00);
    FdtCells("power-domains", 1, PHANDLE_APCIE);
    FdtCells("pinctrl-0", 1, PHANDLE_PCIE_PINS);
    FdtCells("#address-cells", 1, 3);
    FdtCells("#size-cells", 1, 2);
    FdtBeginNode("pci@0,0");
    FdtStrings("device_type", LIST("pci"));
    FdtCells("reg", 5, 0, 0, 0, 0, 0);
    FdtCells("reset-gpios", 3, PHANDLE_PINCTRL, 0xa7, 1);
    FdtCells("pwren-gpios", 3, PHANDLE_SMC_GPIO, 0x13, 0);
    FdtCells("bus-range", 2, 1, 1);
    FdtEndNode();
    FdtBeginNode("pci@1,0");
    FdtStrings("device_type", LIST("pci"));
    FdtCells("reg", 5, 0x800, 0, 0, 0, 0);
    FdtCells("reset-gpios", 3, PHANDLE_PINCTRL, 0xa8, 1);
    FdtCells("bus-range", 2, 2, 2);
    FdtStrings("status", LIST("disabled"));
    FdtEndNode();
    FdtEndNode();

    FdtBeginNode("spmi@364714000");
    FdtStrings("compatible", LIST("apple,t6030-spmi\0apple,t8103-spmi"));
    FdtCells("reg", 4, 3, 0x64714000, 0, 0x100);
    FdtCells("#address-cells", 1, 2);
    FdtCells("#size-cells", 1, 0);
    FdtBeginNode("pmic@b");
    FdtStrings("compatible", LIST("apple,chapel-pmic\0apple,spmi-nvmem"));
    FdtCells("reg", 2, 0xb, 0);
    FdtBeginNode("nvmem-layout");
    FdtStrings("compatible", LIST("fixed-layout"));
    FdtCells("#address-cells", 1, 1);
    FdtCells("#size-cells", 1, 1);
    FdtBeginNode("rtc-offset@2100");
    FdtCells("reg", 2, 0x2100, 6);
    FdtCells("phandle", 1, PHANDLE_RTC_OFFSET);
    FdtEndNode();
    FdtBeginNode("boot-error-count@f802,0");
    FdtCells("reg", 2, 0xf802, 1);
    FdtCells("bits", 2, 0, 4);
    FdtCells("phandle", 1, PHANDLE_BOOT_ERRORS);
    FdtEndNode();
    FdtBeginNode("panic-count@f802,4");
    FdtCells("reg", 2, 0xf802, 1);
    FdtCells("bits", 2, 4, 4);
    FdtCells("phandle", 1, PHANDLE_PANICS);
    FdtEndNode();
    FdtEndNode();
    FdtEndNode();
    FdtEndNode();

    FdtEndNode();
    FdtEndNode();
    FdtFinish();
}

static
VOID
ResetSimulation(PFDT_DRIVER_TREE Tree)
{
    ULONG Index;

    for (Index = 0; Index < RTL_NUMBER_OF(SimRegions); ++Index)
        memset(SimRegions[Index].Data, 0, SimRegions[Index].Size);
    SimAtcStuck = FALSE;
    SimPcieNoLink = FALSE;
    SimSleptMs = 0;
    SimWriteCount = 0;
    SimMapCount = 0;
    SimPmgrStuck = FALSE;
    SimFifoHead = SimFifoTail = 0;
    SimSpmiSilent = FALSE;
    SimSpmiBadParity = FALSE;
    SimSpmiCommandCount = 0;
    SimSpmiNoAck = FALSE;
    SimSpmiWritePending = 0;
    SimAceEnabled = FALSE;
    memset(SimAceRegs, 0, sizeof(SimAceRegs));
    memset(SimAceLength, 0, sizeof(SimAceLength));
    SimAceSelected = SimAceRegister0 = 0;
    SimAceBusyReads = 0;
    SimAceWrongEcho = SimAceRejectCommand = SimAceNoWake = FALSE;
    SimAceWakeInterrupt = 0;
    SimStalledMicroseconds = 0;
    BuildSocTree();
    memset(Tree, 0, sizeof(*Tree));
    Tree->Blob = Builder.Blob;
    ok(RiscvFdtOpen(Builder.Blob, Builder.BlobSize, &Tree->Fdt), "The test tree did not open\n");
}

static
ULONG
FindNode(PFDT_DRIVER_TREE Tree, const CHAR *Compatible)
{
    ULONG Node = RiscvFdtRootNode(&Tree->Fdt), Length;
    const VOID *Property;

    while (Node != RISCV_FDT_NO_NODE)
    {
        if (RiscvFdtToken(&Tree->Fdt, Node) == RISCV_FDT_BEGIN_NODE)
        {
            Property = RiscvFdtGetProperty(&Tree->Fdt, Node, "compatible", &Length);
            if (RiscvFdtStringListContains(Property, Length, Compatible))
                return Node;
        }
        Node = RiscvFdtSkipToken(&Tree->Fdt, Node);
    }
    return RISCV_FDT_NO_NODE;
}

#define PMGR(Offset) SimRegions[0].Data[(Offset) / sizeof(ULONG)]
#define DART(Offset) SimRegions[1].Data[(Offset) / sizeof(ULONG)]
#define SPMI(Offset) SimRegions[2].Data[(Offset) / sizeof(ULONG)]
#define ATC(Offset) SimRegions[3].Data[(Offset) / sizeof(ULONG)]
#define AXI2AF(Offset) SimRegions[4].Data[(Offset) / sizeof(ULONG)]
#define USB2PHY(Offset) SimRegions[5].Data[(Offset) / sizeof(ULONG)]
#define PIPE(Offset) SimRegions[6].Data[(Offset) / sizeof(ULONG)]
#define PCIEPORT(Offset) SimRegions[7].Data[(Offset) / sizeof(ULONG)]
#define PCIEPHY(Offset) SimRegions[8].Data[(Offset) / sizeof(ULONG)]
#define PINCTRL(Offset) SimRegions[9].Data[(Offset) / sizeof(ULONG)]
#define PCIECFG(Offset) SimRegions[10].Data[(Offset) / sizeof(ULONG)]

static
VOID
TestPmgr(VOID)
{
    FDT_DRIVER_TREE Tree;
    ULONG Nvme;
    NTSTATUS Status;

    ResetSimulation(&Tree);
    Nvme = FindNode(&Tree, "apple,t8103-nvme-ans2");
    PMGR(0x4d8) = 0xff;
    PMGR(0x510) = 0x00000700;
    PMGR(0x5b0) = 0x00000000;
    Status = ApplePmgrPowerOnDevice(&Tree, Nvme);
    ok_hex(Status, STATUS_SUCCESS);
    ok_int(SimWriteCount, 4);
    ok(SimWrites[0].Physical == SIM_PMGR_BASE + 0x510, "First write went to %I64x\n", SimWrites[0].Physical);
    ok_hex(SimWrites[0].Value, 0x0000000f);
    ok(SimWrites[1].Physical == SIM_PMGR_BASE + 0x510, "Second write went to %I64x\n", SimWrites[1].Physical);
    ok_hex(SimWrites[1].Value, 0x1000000f);
    ok(SimWrites[2].Physical == SIM_PMGR_BASE + 0x5b0, "Third write went to %I64x\n", SimWrites[2].Physical);
    ok_hex(SimWrites[2].Value, 0x0000000f);
    ok_hex(SimWrites[3].Value, 0x1000000f);
    ok_hex(PMGR(0x4d8), 0xff);
    ok_hex(PMGR(0x510), 0x100000ff);
    ok_int(SimMapCount, 0);

    SimWriteCount = 0;
    Status = ApplePmgrPowerOnDevice(&Tree, Nvme);
    ok_hex(Status, STATUS_SUCCESS);
    ok_int(SimWriteCount, 0);

    PMGR(0x510) = 0x100003ff;
    SimWriteCount = 0;
    Status = ApplePmgrResetDevice(&Tree, Nvme);
    ok_hex(Status, STATUS_SUCCESS);
    ok_int(SimWriteCount, 4);
    ok_hex(SimWrites[0].Value, 0x100004ff);
    ok_hex(SimWrites[1].Value, 0x900004ff);
    ok_hex(SimWrites[2].Value, 0x100004ff);
    ok_hex(SimWrites[3].Value, 0x100000ff);

    PMGR(0x510) = 0;
    SimWriteCount = 0;
    Status = ApplePmgrResetDevice(&Tree, Nvme);
    ok_hex(Status, STATUS_DEVICE_NOT_READY);
    ok_int(SimWriteCount, 0);

    ResetSimulation(&Tree);
    SimPmgrStuck = TRUE;
    PMGR(0x4d8) = 0xff;
    Status = ApplePmgrPowerOnDevice(&Tree, FindNode(&Tree, "apple,t8103-nvme-ans2"));
    ok_hex(Status, STATUS_IO_TIMEOUT);
    ok(SimStalledMicroseconds >= 100, "Waited %lu us for the power state\n", SimStalledMicroseconds);
    ok_int(SimMapCount, 0);
}

static
ULONG64
WalkDart(ULONG64 TopPhysical, ULONG Levels, ULONG64 Iova)
{
    PULONG64 Table = SimVirtualForPhysical(TopPhysical);
    ULONG64 Entry = 0;
    ULONG Level;

    for (Level = Levels; Table && Level >= 1; --Level)
    {
        Entry = Table[(Iova >> (14 + (Level - 1) * 11)) & 2047];
        if (!(Entry & 1))
            return 0;
        if (Level > 1)
            Table = SimVirtualForPhysical((Entry & 0x0000003ffffffc00ULL) << 4);
    }
    return Table ? (Entry & 0x0000003ffffffc00ULL) << 4 : 0;
}

static
VOID
TestDartIdentity(PFDT_DRIVER_TREE Tree, ULONG Usb, ULONG InputBits, ULONG Levels, ULONG Tcr)
{
    ULONG64 Mask = (1ULL << InputBits) - 1, Top;
    NTSTATUS Status;

    ResetSimulation(Tree);
    memset(&AppleDartIdentity, 0, sizeof(AppleDartIdentity));
    SimRam[0].BaseAddress.QuadPart = 0x10000000000LL;
    SimRam[0].NumberOfBytes.QuadPart = 0x200000;
    SimRam[1].BaseAddress.QuadPart = 0x10002000000LL;
    SimRam[1].NumberOfBytes.QuadPart = 0x6000;
    SimRam[2].NumberOfBytes.QuadPart = 0;
    DART(0x04) = 0;
    DART(0x08) = (42 << 24) | (InputBits << 16);
    DART(0x0c) = 16;
    DART(0x1000) = 6;
    Status = AppleDartMapPhysical(Tree, Usb, "test");
    ok_hex(Status, STATUS_SUCCESS);
    ok_int(AppleDartIdentity.Levels, Levels);
    ok_hex(DART(0x1000), Tcr);
    ok_hex(DART(0x1004), Tcr);
    ok_hex(DART(0x1400) & 1, 1);
    ok_hex(DART(0x1400), DART(0x1404));
    ok_hex(DART(0xc00), 3);
    ok_hex(DART(0x80), 0x101);
    Top = ((ULONG64)(DART(0x1400) >> 2)) << 14;
    ok(Top == AppleDartIdentity.TopPhysical, "TTBR points at %I64x, tables at %I64x\n", Top, AppleDartIdentity.TopPhysical);
    ok(WalkDart(Top, Levels, 0x10000000000ULL & Mask) == 0x10000000000ULL, "First page maps to %I64x\n",
       WalkDart(Top, Levels, 0x10000000000ULL & Mask));
    ok(WalkDart(Top, Levels, 0x100001fc000ULL & Mask) == 0x100001fc000ULL, "Last page maps to %I64x\n",
       WalkDart(Top, Levels, 0x100001fc000ULL & Mask));
    ok(WalkDart(Top, Levels, 0x10002004000ULL & Mask) == 0x10002004000ULL, "Second range maps to %I64x\n",
       WalkDart(Top, Levels, 0x10002004000ULL & Mask));
    ok(WalkDart(Top, Levels, 0x10000200000ULL & Mask) == 0, "A hole is mapped to %I64x\n",
       WalkDart(Top, Levels, 0x10000200000ULL & Mask));
    ok_int(SimMapCount, 0);
}

static
VOID
TestDart(VOID)
{
    FDT_DRIVER_TREE Tree;
    ULONG Usb;
    NTSTATUS Status;

    ResetSimulation(&Tree);
    Usb = FindNode(&Tree, "apple,t8103-dwc3");
    DART(0x04) = 1;
    DART(0x0c) = 16;
    DART(0x1000) = 0;
    DART(0x1004) = 1;
    DART(0x1400) = 0x1234;
    Status = AppleDartMapPhysical(&Tree, Usb, "test");
    ok_hex(Status, STATUS_SUCCESS);
    ok_hex(DART(0x1000), 6);
    ok_hex(DART(0x1004), 6);
    ok_hex(DART(0x1400), 0);
    ok_hex(DART(0xc00), 3);
    ok_hex(DART(0x80), 0x101);
    ok_int(SimMapCount, 0);

    ResetSimulation(&Tree);
    DART(0x04) = 0;
    DART(0x0c) = 16;
    Status = AppleDartMapPhysical(&Tree, Usb, "test");
    ok_hex(Status, STATUS_NOT_SUPPORTED);
    ok_int(SimWriteCount, 0);

    TestDartIdentity(&Tree, Usb, 36, 2, 1);
    TestDartIdentity(&Tree, Usb, 42, 3, 9);

    ResetSimulation(&Tree);
    memset(&AppleDartIdentity, 0, sizeof(AppleDartIdentity));
    SimRam[0].BaseAddress.QuadPart = 0x10000000000LL;
    SimRam[0].NumberOfBytes.QuadPart = 0x200000;
    SimRam[1].BaseAddress.QuadPart = 0x11000000000LL;
    SimRam[1].NumberOfBytes.QuadPart = 0x4000;
    SimRam[2].NumberOfBytes.QuadPart = 0;
    DART(0x08) = (42 << 24) | (36 << 16);
    DART(0x0c) = 16;
    Status = AppleDartMapPhysical(&Tree, Usb, "test");
    ok_hex(Status, STATUS_NOT_SUPPORTED);
    ok_hex(DART(0x1000), 0);

    ResetSimulation(&Tree);
    DART(0x04) = 1;
    DART(0x0c) = 16;
    DART(0x200) = 1;
    Status = AppleDartMapPhysical(&Tree, Usb, "test");
    ok_hex(Status, STATUS_ACCESS_DENIED);
    ok_int(SimWriteCount, 0);

    ResetSimulation(&Tree);
    DART(0x04) = 1;
    DART(0x0c) = 1;
    Status = AppleDartMapPhysical(&Tree, Usb, "test");
    ok_hex(Status, STATUS_DEVICE_CONFIGURATION_ERROR);
}

static
VOID
TestDartLowWindowM3Memory(VOID)
{
    static const ULONG64 Runs[][2] =
    {
        { 0x10001fa8000ULL, 0x10001faffffULL }, { 0x10001fb2000ULL, 0x10001fbffffULL },
        { 0x10001fc3000ULL, 0x100037a7fffULL }, { 0x10003b48000ULL, 0x10006713fffULL },
        { 0x1000673e000ULL, 0x1083bbfffffULL }, { 0x1083bc01000ULL, 0x108d4892fffULL },
        { 0x108d4894000ULL, 0x108d4897fffULL }, { 0x108d48ba000ULL, 0x108d48c1fffULL },
        { 0x108d48c3000ULL, 0x108d48c3fffULL }, { 0x108d48e6000ULL, 0x108d595dfffULL },
        { 0x108d595f000ULL, 0x108d992ffffULL }, { 0x108d9940000ULL, 0x108d9a0ffffULL },
        { 0x108d9a14000ULL, 0x108d9a17fffULL },
    };
    FDT_DRIVER_TREE Tree;
    ULONG64 Iova, Top;
    ULONG Dart, Index;
    NTSTATUS Status;

    ResetSimulation(&Tree);
    for (Index = 0; Index < SimAllocationCount; Index++)
        HeapFree(GetProcessHeap(), 0, SimAllocations[Index].Virtual);
    SimAllocationCount = 0;
    memset(&AppleDartIdentity, 0, sizeof(AppleDartIdentity));
    AppleDartLowNext = DART_LOW_WINDOW_BASE;
    memset(SimRam, 0, sizeof(SimRam));
    for (Index = 0; Index < RTL_NUMBER_OF(Runs); Index++)
    {
        SimRam[Index].BaseAddress.QuadPart = (LONGLONG)Runs[Index][0];
        SimRam[Index].NumberOfBytes.QuadPart = (LONGLONG)(Runs[Index][1] + 1 - Runs[Index][0]);
    }
    Dart = FindNode(&Tree, "apple,t8110-dart");
    DART(0x04) = 0x30110023;
    DART(0x08) = 0x2a2a0200;
    DART(0x0c) = 16;
    DART(0x1004) = 6;

    Status = AppleDartMapLow(&Tree, Dart, 1, 0x1000673e000ULL, 0x7a000, &Iova, "test");
    ok_hex(Status, STATUS_SUCCESS);
    ok(Iova == 0x01002000ULL, "Window mapping at %I64x\n", Iova);
    ok_int(AppleDartIdentity.Levels, 3);
    ok_hex(DART(0x1004), 9);
    Top = ((ULONG64)(DART(0x1404) >> 2)) << 14;
    for (Index = 0; Index < RTL_NUMBER_OF(Runs); Index++)
    {
        ULONG64 Page = Runs[Index][0] & ~0x3fffULL;

        if (WalkDart(Top, 3, Page) != Page)
        {
            ok(FALSE, "Run %lu page %I64x maps to %I64x\n", Index, Page, WalkDart(Top, 3, Page));
            break;
        }
    }
    ok(WalkDart(Top, 3, 0x108d9a14000ULL) == 0x108d9a14000ULL, "Last RAM page maps to %I64x\n",
       WalkDart(Top, 3, 0x108d9a14000ULL));
    ok(WalkDart(Top, 3, 0x01000000ULL) == 0x1000673c000ULL, "Window page maps to %I64x\n", WalkDart(Top, 3, 0x01000000ULL));
    ok(WalkDart(Top, 3, 0x01078000ULL) == 0x100067b4000ULL, "Last window page maps to %I64x\n",
       WalkDart(Top, 3, 0x01078000ULL));
    memset(SimRam, 0, sizeof(SimRam));
}

static
VOID
TestDartLowWindow(VOID)
{
    FDT_DRIVER_TREE Tree;
    ULONG Dart, Pcie, Phandle, Sid;
    ULONG64 Iova, Top;
    NTSTATUS Status;

    ResetSimulation(&Tree);
    memset(&AppleDartIdentity, 0, sizeof(AppleDartIdentity));
    AppleDartLowNext = DART_LOW_WINDOW_BASE;
    SimRam[0].BaseAddress.QuadPart = 0x10000000000LL;
    SimRam[0].NumberOfBytes.QuadPart = 0x200000;
    SimRam[1].NumberOfBytes.QuadPart = 0;
    Dart = FindNode(&Tree, "apple,t8110-dart");
    Pcie = FindNode(&Tree, "apple,t6020-pcie");
    DART(0x04) = 1;
    DART(0x08) = (42 << 24) | (42 << 16);
    DART(0x0c) = 16;
    DART(0x1000) = 6;
    DART(0x1004) = 6;

    ok(ApplePcieFindStream(&Tree, Pcie, 0x101, &Phandle, &Sid), "RID 0x101 has no stream\n");
    ok_hex(Phandle, PHANDLE_DART0);
    ok_int(Sid, 1);
    ok(ApplePcieFindStream(&Tree, Pcie, 0x200, &Phandle, &Sid), "RID 0x200 has no stream\n");
    ok_int(Sid, 2);
    ok(!ApplePcieFindStream(&Tree, Pcie, 0x300, &Phandle, &Sid), "RID 0x300 must not resolve\n");

    Status = AppleDartMapLow(&Tree, Dart, 1, 0x10000006000ULL, 0x100, &Iova, "test");
    ok_hex(Status, STATUS_SUCCESS);
    ok(Iova == 0x01002000ULL, "First window mapping at %I64x\n", Iova);
    ok_int(AppleDartIdentity.Levels, 3);
    ok_hex(DART(0x1004), 9);
    ok_hex(DART(0x1000), 6);
    ok_hex(DART(0x1404) & 1, 1);
    ok_hex(DART(0xc00) & 2, 2);
    ok_hex(DART(0x80), 0x101);
    Top = ((ULONG64)(DART(0x1404) >> 2)) << 14;
    ok(WalkDart(Top, 3, 0x01000000ULL) == 0x10000004000ULL, "Window page maps to %I64x\n", WalkDart(Top, 3, 0x01000000ULL));
    ok(WalkDart(Top, 3, 0x10000000000ULL) == 0x10000000000ULL, "Identity lost: %I64x\n", WalkDart(Top, 3, 0x10000000000ULL));
    ok(WalkDart(Top, 3, 0x01004000ULL) == 0, "Unused window page maps to %I64x\n", WalkDart(Top, 3, 0x01004000ULL));

    SimWriteCount = 0;
    Status = AppleDartMapLow(&Tree, Dart, 1, 0x10000007f00ULL, 0x200, &Iova, "test");
    ok_hex(Status, STATUS_SUCCESS);
    ok(Iova == 0x01007f00ULL, "Second window mapping at %I64x\n", Iova);
    ok(WalkDart(Top, 3, 0x01004000ULL) == 0x10000004000ULL, "Second mapping first page %I64x\n", WalkDart(Top, 3, 0x01004000ULL));
    ok(WalkDart(Top, 3, 0x01008000ULL) == 0x10000008000ULL, "Second mapping last page %I64x\n", WalkDart(Top, 3, 0x01008000ULL));
    ok_int(SimWriteCount, 1);
    ok(SimWrites[0].Physical == SIM_DART_BASE + 0x80, "A translating stream only needs a flush, wrote %I64x\n", SimWrites[0].Physical);

    SimWriteCount = 0;
    ok_hex(AppleDartUnmapLow(&Tree, Dart, 1, Iova, 0x200, "test"), STATUS_SUCCESS);
    ok(WalkDart(Top, 3, 0x01004000ULL) == 0, "Unmapped first page still maps to %I64x\n", WalkDart(Top, 3, 0x01004000ULL));
    ok(WalkDart(Top, 3, 0x01008000ULL) == 0, "Unmapped last page still maps to %I64x\n", WalkDart(Top, 3, 0x01008000ULL));
    ok(WalkDart(Top, 3, 0x01000000ULL) == 0x10000004000ULL, "Unmap touched the first mapping: %I64x\n", WalkDart(Top, 3, 0x01000000ULL));
    ok(WalkDart(Top, 3, 0x10000000000ULL) == 0x10000000000ULL, "Unmap touched the identity: %I64x\n", WalkDart(Top, 3, 0x10000000000ULL));
    ok_int(SimWriteCount, 1);
    ok(SimWrites[0].Physical == SIM_DART_BASE + 0x80 && SimWrites[0].Value == 0x101, "Unmap flushed %I64x = %lx\n",
       SimWrites[0].Physical, SimWrites[0].Value);
    ok_hex(AppleDartUnmapLow(&Tree, Dart, 1, Iova, 0x200, "test"), STATUS_NOT_FOUND);
    ok_hex(AppleDartUnmapLow(&Tree, Dart, 1, 0x10000006000ULL, 0x100, "test"), STATUS_INVALID_PARAMETER);
    ok_hex(AppleDartUnmapLow(&Tree, Dart, 1, DART_LOW_WINDOW_END - 0x100, 0x200, "test"), STATUS_INVALID_PARAMETER);
    ok_hex(AppleDartUnmapLow(&Tree, Dart, 16, 0x01000000ULL, 0x100, "test"), STATUS_INVALID_PARAMETER);
    ok_hex(AppleDartUnmapLow(&Tree, Dart, 1, 0x01000000ULL, 0, "test"), STATUS_INVALID_PARAMETER);

    ok_hex(AppleDartMapLow(&Tree, Dart, 16, 0x10000006000ULL, 0x100, &Iova, "test"), STATUS_INVALID_PARAMETER);
    ok_hex(AppleDartMapLow(&Tree, Dart, 1, 0x10000006000ULL, 0, &Iova, "test"), STATUS_INVALID_PARAMETER);
    DART(0x200) = 1;
    ok_hex(AppleDartMapLow(&Tree, Dart, 1, 0x10000006000ULL, 0x100, &Iova, "test"), STATUS_ACCESS_DENIED);
    DART(0x200) = 0;
    AppleDartLowNext = DART_LOW_WINDOW_END - 0x4000;
    ok_hex(AppleDartMapLow(&Tree, Dart, 1, 0x10000006000ULL, 0x4000, &Iova, "test"), STATUS_INSUFFICIENT_RESOURCES);
    ok(AppleDartLowNext == DART_LOW_WINDOW_END - 0x4000, "A failed mapping moved the window to %I64x\n", AppleDartLowNext);
    ok_int(SimMapCount, 0);
}


#define DART_ADOPT_SID      5
#define DART_ADOPT_PAGE     0x4000ULL

static
PULONG64
SimFirmwareTable(ULONG64 *Physical)
{
    PHYSICAL_ADDRESS Low, High, Boundary;
    PULONG64 Table;

    Low.QuadPart = 0;
    High.QuadPart = -1;
    Boundary.QuadPart = (LONGLONG)DART_ADOPT_PAGE;
    Table = MmAllocateContiguousMemorySpecifyCache((SIZE_T)DART_ADOPT_PAGE, Low, High, Boundary, MmCached);
    if (Table)
    {
        memset(Table, 0, (SIZE_T)DART_ADOPT_PAGE);
        *Physical = (ULONG64)MmGetPhysicalAddress(Table).QuadPart;
    }
    return Table;
}

/* Build the two-level tables a running coprocessor would already have, with one page mapped. */
static
ULONG64
SimFirmwareTables(ULONG64 Iova, ULONG64 Physical)
{
    ULONG64 TopPhysical = 0, LeafPhysical = 0;
    PULONG64 Top = SimFirmwareTable(&TopPhysical);
    PULONG64 Leaf = SimFirmwareTable(&LeafPhysical);

    if (!Top || !Leaf)
        return 0;
    Top[(Iova >> 25) & 2047] = ((LeafPhysical >> 4) & 0x0000003ffffffc00ULL) | 1;
    Leaf[(Iova >> 14) & 2047] = ((Physical >> 4) & 0x0000003ffffffc00ULL) | (0xfffULL << 40) | 1;
    return TopPhysical;
}

static
VOID
SimDartAdoptRegisters(ULONG64 TopPhysical, ULONG Tcr)
{
    DART(0x08) = (36 << 24) | (36 << 16);
    DART(0x0c) = 16;
    DART(0x200) = 0;
    DART(0x1000 + 4 * DART_ADOPT_SID) = Tcr;
    DART(0x1400 + 4 * DART_ADOPT_SID) = (ULONG)(((TopPhysical >> 14) << 2) | 1);
}

static
VOID
TestDartAdopt(VOID)
{
    const ULONG64 Mapped = 0x00200000ULL, Phys = 0x10000040000ULL;
    APPLE_DART_ADOPTED Adopted;
    FDT_DRIVER_TREE Tree;
    ULONG64 Top, Found, Iova;
    ULONG Dart;
    NTSTATUS Status;

    ResetSimulation(&Tree);
    Dart = FindNode(&Tree, "apple,t8110-dart");
    Top = SimFirmwareTables(Mapped, Phys);
    ok(Top != 0, "The firmware tables were not built\n");
    SimDartAdoptRegisters(Top, 1);

    Status = AppleDartAdopt(&Tree, Dart, DART_ADOPT_SID, 0x10000000ULL, 0x20000000ULL, "test", &Adopted);
    ok_hex(Status, STATUS_SUCCESS);
    ok_int(Adopted.Levels, 2);
    ok_int(Adopted.Sid, DART_ADOPT_SID);
    ok(Adopted.TopPhysical == Top, "Adopted the tables at %I64x, not %I64x\n", Adopted.TopPhysical, Top);

    /* The firmware's own mapping is readable and is never handed out again. */
    ok(AppleDartAdoptedTranslate(&Adopted, Mapped, &Found), "The firmware mapping was not found\n");
    ok(Found == Phys, "The firmware mapping resolves to %I64x, not %I64x\n", Found, Phys);
    ok(AppleDartAdoptedTranslate(&Adopted, Mapped + 0x40, &Found) && Found == Phys + 0x40,
       "An offset inside the page resolves to %I64x\n", Found);
    ok(!AppleDartAdoptedTranslate(&Adopted, Mapped + DART_ADOPT_PAGE, &Found), "An unmapped page resolved\n");
    ok(!AppleDartAdoptedRangeFree(&Adopted, Mapped, 1), "The firmware page is reported free\n");
    ok(AppleDartAdoptedRangeFree(&Adopted, Mapped + DART_ADOPT_PAGE, 1), "A free page is reported taken\n");

    /* A mapping at a chosen address, and the refusal to overwrite one that exists. */
    Status = AppleDartAdoptedMapAt(&Adopted, 0x10000000ULL, 0x10000080000ULL, 0x1000);
    ok_hex(Status, STATUS_SUCCESS);
    ok(AppleDartAdoptedTranslate(&Adopted, 0x10000000ULL, &Found) && Found == 0x10000080000ULL,
       "The new mapping resolves to %I64x\n", Found);
    ok_hex(AppleDartAdoptedMapAt(&Adopted, 0x10000000ULL, 0x100000c0000ULL, 0x1000), STATUS_CONFLICTING_ADDRESSES);
    ok(AppleDartAdoptedTranslate(&Adopted, 0x10000000ULL, &Found) && Found == 0x10000080000ULL,
       "The refused mapping changed the address to %I64x\n", Found);
    ok_hex(AppleDartAdoptedMapAt(&Adopted, 0x10004000ULL, 0x10000080040ULL, 0x1000), STATUS_INVALID_PARAMETER);

    /* The window allocator skips what is taken and keeps the offset inside the page. */
    Status = AppleDartAdoptedMap(&Adopted, 0x10000100040ULL, 0x80, &Iova);
    ok_hex(Status, STATUS_SUCCESS);
    ok((Iova & (DART_ADOPT_PAGE - 1)) == 0x40, "The mapping lost its page offset: %I64x\n", Iova);
    ok(Iova >= 0x10004000ULL, "The allocator reused the taken page at %I64x\n", Iova);
    ok(AppleDartAdoptedTranslate(&Adopted, Iova, &Found) && Found == 0x10000100040ULL,
       "The allocated mapping resolves to %I64x\n", Found);

    /* A window the tables cannot reach, and a stream the firmware did not translate. */
    AppleDartAdoptedRelease(&Adopted);
    ok_hex(AppleDartAdopt(&Tree, Dart, DART_ADOPT_SID, 0x10000000ULL, 0x1000000000ULL, "test", &Adopted),
           STATUS_SUCCESS);
    AppleDartAdoptedRelease(&Adopted);
    ok_hex(AppleDartAdopt(&Tree, Dart, DART_ADOPT_SID, 0x10000000ULL, 0x1000004000ULL, "test", &Adopted),
           STATUS_NOT_SUPPORTED);
    SimDartAdoptRegisters(Top, 6);
    ok_hex(AppleDartAdopt(&Tree, Dart, DART_ADOPT_SID, 0x10000000ULL, 0x20000000ULL, "test", &Adopted),
           STATUS_DEVICE_NOT_READY);
    SimDartAdoptRegisters(Top, 1);
    DART(0x1400 + 4 * DART_ADOPT_SID) = 0;
    ok_hex(AppleDartAdopt(&Tree, Dart, DART_ADOPT_SID, 0x10000000ULL, 0x20000000ULL, "test", &Adopted),
           STATUS_DEVICE_NOT_READY);
    SimDartAdoptRegisters(Top, 1);
    ok_hex(AppleDartAdopt(&Tree, Dart, 64, 0x10000000ULL, 0x20000000ULL, "test", &Adopted),
           STATUS_DEVICE_NOT_READY);
    ok_hex(AppleDartAdopt(&Tree, Dart, DART_ADOPT_SID, 0x20000000ULL, 0x10000000ULL, "test", &Adopted),
           STATUS_INVALID_PARAMETER);

    Status = AppleDartAdopt(&Tree, Dart, DART_ADOPT_SID, 0x10000000ULL, 0x20000000ULL, "test", &Adopted);
    ok_hex(Status, STATUS_SUCCESS);
    AppleDartAdoptedRelease(&Adopted);
    ok_int(SimMapCount, 0);
}

static
VOID
TestPmpPower(VOID)
{
    /* The power processor's observed wire values: the graphics vote and its acknowledgement. */
    ok(ApplePmpPowerMessage(APPLE_PMP_SET_POWER, APPLE_PMP_DEVICE_GRAPHICS, TRUE) == 0x0020f50100000000ULL,
       "The graphics vote encodes as %I64x\n", ApplePmpPowerMessage(APPLE_PMP_SET_POWER, APPLE_PMP_DEVICE_GRAPHICS, TRUE));
    ok(ApplePmpPowerReply(APPLE_PMP_SET_POWER) == 0x0021000000000000ULL, "The acknowledgement is %I64x\n",
       ApplePmpPowerReply(APPLE_PMP_SET_POWER));
    ok(ApplePmpPowerMessage(APPLE_PMP_SET_POWER, APPLE_PMP_DEVICE_GRAPHICS, FALSE) == 0x0020f50000000000ULL,
       "Releasing the graphics vote encodes as %I64x\n",
       ApplePmpPowerMessage(APPLE_PMP_SET_POWER, APPLE_PMP_DEVICE_GRAPHICS, FALSE));
    ok(ApplePmpPowerMessage(APPLE_PMP_SET_POWER, 8, TRUE) == 0x0020f80100000000ULL, "Device 8 encodes as %I64x\n",
       ApplePmpPowerMessage(APPLE_PMP_SET_POWER, 8, TRUE));
    /* Every field sits in its own bits, so no device or opcode can forge another's reply. */
    ok((ApplePmpPowerMessage(APPLE_PMP_SET_POWER, APPLE_PMP_DEVICE_GRAPHICS, TRUE) & APPLE_PMP_COMMAND_MARKER) != 0,
       "The vote lost its marker\n");
    ok(ApplePmpPowerMessage(APPLE_PMP_SET_POWER, APPLE_PMP_DEVICE_GRAPHICS, TRUE) !=
       ApplePmpPowerReply(APPLE_PMP_SET_POWER), "A request cannot equal its own acknowledgement\n");
    ok(ApplePmpPowerMessage(0x0e, APPLE_PMP_DEVICE_GRAPHICS, TRUE) !=
       ApplePmpPowerMessage(APPLE_PMP_SET_POWER, APPLE_PMP_DEVICE_GRAPHICS, TRUE), "Two opcodes encode alike\n");
    ok(ApplePmpPowerReply(0x0e) != ApplePmpPowerReply(APPLE_PMP_SET_POWER), "Two opcodes acknowledge alike\n");
}

static
VOID
TestUat(VOID)
{
    const APPLE_UAT_PROTECTION Shared = { APPLE_UAT_ACCESS_BOTH, APPLE_UAT_MEMORY_UNCACHED, TRUE, TRUE };
    const APPLE_UAT_PROTECTION Private = { APPLE_UAT_ACCESS_FIRMWARE, APPLE_UAT_MEMORY_CACHED, FALSE, FALSE };
    const APPLE_UAT_PROTECTION Device = { APPLE_UAT_ACCESS_GPU, APPLE_UAT_MEMORY_DEVICE, TRUE, FALSE };
    APPLE_UAT_PROTECTION Read;
    APPLE_UAT uat;
    ULONG64 Physical, Entry;
    ULONG Access, Memory, Index;
    NTSTATUS Status;

    ok_hex(AppleUatCreate(&uat, TRUE, "test"), STATUS_SUCCESS);
    ok_int(uat.Tables, 1);
    ok(uat.TopPhysical != 0, "The table root has no address\n");

    /* A leaf carries the address, the owner, the caching and the execute rules, and nothing else. */
    Entry = AppleUatEntry(0x80000000ULL, &Shared, TRUE);
    ok((Entry & 0x3) == 3, "A leaf must be a page entry: %I64x\n", Entry);
    ok((Entry & APPLE_UAT_OUTPUT_MASK) == 0x80000000ULL, "A leaf lost its address: %I64x\n", Entry);
    ok(APPLE_UAT_OUTPUT_MASK == 0x3ffffffc000ULL, "A leaf addresses %I64x\n", (ULONG64)APPLE_UAT_OUTPUT_MASK);
    ok_int(APPLE_UAT_ROOT_ENTRIES, 64);
    ok((Entry & (1ULL << 10)) != 0, "A leaf must be marked accessed: %I64x\n", Entry);
    ok((Entry & (1ULL << 11)) == 0, "A global leaf must not be marked non-global: %I64x\n", Entry);
    ok((AppleUatEntry(0x80000000ULL, &Shared, FALSE) & (1ULL << 11)) != 0,
       "A non-global leaf must be marked so\n");
    ok((Entry >> 52) & 0x8, "A leaf must allow the processor to reach it: %I64x\n", Entry >> 52);

    /* Every owner and caching combination survives the round trip through a leaf. */
    for (Access = 0; Access <= 3; ++Access)
    {
        for (Memory = 0; Memory <= 7; ++Memory)
        {
            APPLE_UAT_PROTECTION Any;

            Any.Access = (UCHAR)Access;
            Any.Memory = (UCHAR)Memory;
            Any.Read = (Access & 1) != 0;
            Any.Write = (Memory & 1) != 0;
            ok_hex(AppleUatMap(&uat, 0x100000ULL + (ULONG64)(Access * 8 + Memory) * APPLE_UAT_PAGE_SIZE,
                               0x40000000ULL, APPLE_UAT_PAGE_SIZE, &Any), STATUS_SUCCESS);
            ok(AppleUatTranslate(&uat, 0x100000ULL + (ULONG64)(Access * 8 + Memory) * APPLE_UAT_PAGE_SIZE,
                                 &Physical, &Read), "A mapping was lost\n");
            ok(Read.Access == Any.Access && Read.Memory == Any.Memory && Read.Read == Any.Read &&
               Read.Write == Any.Write, "Access %lu memory %lu came back as %u/%u/%u/%u\n", Access, Memory,
               Read.Access, Read.Memory, Read.Read, Read.Write);
        }
    }

    /* A range spanning several leaf tables, its offsets, and the refusal to overwrite. */
    Status = AppleUatMap(&uat, 0x2000000ULL, 0x50000000ULL, 4 * APPLE_UAT_PAGE_SIZE, &Private);
    ok_hex(Status, STATUS_SUCCESS);
    for (Index = 0; Index < 4; ++Index)
    {
        ok(AppleUatTranslate(&uat, 0x2000000ULL + (ULONG64)Index * APPLE_UAT_PAGE_SIZE, &Physical, NULL) &&
           Physical == 0x50000000ULL + (ULONG64)Index * APPLE_UAT_PAGE_SIZE, "Page %lu maps to %I64x\n", Index,
           Physical);
    }
    ok(AppleUatTranslate(&uat, 0x2000000ULL + 0x40, &Physical, NULL) && Physical == 0x50000040ULL,
       "An offset inside a page resolves to %I64x\n", Physical);
    ok(!AppleUatTranslate(&uat, 0x2000000ULL + 4 * APPLE_UAT_PAGE_SIZE, &Physical, NULL),
       "The page after the range is mapped\n");
    ok_hex(AppleUatMap(&uat, 0x2000000ULL, 0x60000000ULL, APPLE_UAT_PAGE_SIZE, &Private),
           STATUS_CONFLICTING_ADDRESSES);
    ok(AppleUatTranslate(&uat, 0x2000000ULL, &Physical, NULL) && Physical == 0x50000000ULL,
       "The refused mapping changed the address to %I64x\n", Physical);

    /* A top level boundary, so the walk has to build a second branch. */
    ok_hex(AppleUatMap(&uat, 0x1000000000ULL, 0x70000000ULL, APPLE_UAT_PAGE_SIZE, &Device), STATUS_SUCCESS);
    ok(AppleUatTranslate(&uat, 0x1000000000ULL, &Physical, &Read) && Physical == 0x70000000ULL,
       "The far mapping resolves to %I64x\n", Physical);
    ok_int(Read.Memory, APPLE_UAT_MEMORY_DEVICE);

    /* The firmware's own addresses carry a tag above the translated bits, as the hardware ignores it. */
    ok_hex(AppleUatMap(&uat, 0xfffffc2040000000ULL, 0x78000000ULL, APPLE_UAT_PAGE_SIZE, &Shared), STATUS_SUCCESS);
    ok(AppleUatTranslate(&uat, 0xfffffc2040000000ULL, &Physical, NULL) && Physical == 0x78000000ULL,
       "A firmware address resolves to %I64x\n", Physical);
    ok(AppleUatTranslate(&uat, 0x2040000000ULL, &Physical, NULL) && Physical == 0x78000000ULL,
       "The tag changed the translation to %I64x\n", Physical);
    ok_hex(AppleUatMap(&uat, 0x2040000000ULL, 0x79000000ULL, APPLE_UAT_PAGE_SIZE, &Shared),
           STATUS_CONFLICTING_ADDRESSES);

    /* The last page the input range can name, the page past it, and an address the output cannot hold. */
    ok_hex(AppleUatMap(&uat, APPLE_UAT_ADDRESS_MASK + 1 - APPLE_UAT_PAGE_SIZE, 0x7a000000ULL, APPLE_UAT_PAGE_SIZE,
                       &Shared), STATUS_SUCCESS);
    ok_hex(AppleUatMap(&uat, APPLE_UAT_ADDRESS_MASK + 1 - APPLE_UAT_PAGE_SIZE, 0x7a000000ULL,
                       2 * APPLE_UAT_PAGE_SIZE, &Shared), STATUS_INVALID_PARAMETER);
    ok_hex(AppleUatMap(&uat, 0x4000000ULL, 1ULL << APPLE_UAT_OUTPUT_BITS, APPLE_UAT_PAGE_SIZE, &Shared),
           STATUS_INVALID_PARAMETER);

    /* Misaligned requests. */
    ok_hex(AppleUatMap(&uat, 0x3000ULL, 0x70000000ULL, APPLE_UAT_PAGE_SIZE, &Device), STATUS_INVALID_PARAMETER);
    ok_hex(AppleUatMap(&uat, 0x3000000ULL, 0x70000040ULL, APPLE_UAT_PAGE_SIZE, &Device), STATUS_INVALID_PARAMETER);
    ok_hex(AppleUatMap(&uat, 0x3000000ULL, 0x70000000ULL, 0x40, &Device), STATUS_INVALID_PARAMETER);
    ok_hex(AppleUatMap(&uat, 0x3000000ULL, 0x70000000ULL, 0, &Device), STATUS_INVALID_PARAMETER);

    /* Unmapping frees the address again and leaves its neighbours alone. */
    ok_hex(AppleUatUnmap(&uat, 0x2000000ULL, APPLE_UAT_PAGE_SIZE), STATUS_SUCCESS);
    ok(!AppleUatTranslate(&uat, 0x2000000ULL, &Physical, NULL), "An unmapped page still resolves\n");
    ok(AppleUatTranslate(&uat, 0x2000000ULL + APPLE_UAT_PAGE_SIZE, &Physical, NULL),
       "Unmapping took its neighbour with it\n");
    ok_hex(AppleUatMap(&uat, 0x2000000ULL, 0x60000000ULL, APPLE_UAT_PAGE_SIZE, &Private), STATUS_SUCCESS);

    AppleUatDestroy(&uat);
    ok_int(uat.Tables, 0);
    ok(uat.Top == NULL, "The table root survived destruction\n");
}

static
PULONG64
UatFirmwarePage(
    _Out_ PULONG64 Physical)
{
    PHYSICAL_ADDRESS Low, High, Boundary;
    PULONG64 Page;

    Low.QuadPart = 0;
    High.QuadPart = -1;
    Boundary.QuadPart = APPLE_UAT_PAGE_SIZE;
    Page = MmAllocateContiguousMemorySpecifyCache(APPLE_UAT_PAGE_SIZE, Low, High, Boundary, MmNonCached);
    if (!Page)
    {
        *Physical = 0;
        return NULL;
    }
    RtlZeroMemory(Page, APPLE_UAT_PAGE_SIZE);
    *Physical = (ULONG64)MmGetPhysicalAddress(Page).QuadPart;
    return Page;
}

static
VOID
TestUatAdopt(VOID)
{
    const APPLE_UAT_PROTECTION Shared = { APPLE_UAT_ACCESS_BOTH, APPLE_UAT_MEMORY_UNCACHED, TRUE, TRUE };
    ULONG64 HandoffPhysical, ContextsPhysical, RootPhysical, LevelPhysical, Physical;
    PULONG64 Handoff, Contexts, Root, Level;
    APPLE_UAT_ADOPTED Adopted;
    ULONG Index, Set;

    Handoff = UatFirmwarePage(&HandoffPhysical);
    Contexts = UatFirmwarePage(&ContextsPhysical);
    Root = UatFirmwarePage(&RootPhysical);
    Level = UatFirmwarePage(&LevelPhysical);
    ok(Handoff && Contexts && Root && Level, "The firmware pages were not simulated\n");
    if (!Handoff || !Contexts || !Root || !Level)
        return;

    /* The firmware owns the first two entries of its root and the host must not disturb them. */
    Root[0] = 0xfeed000000000003ULL;
    Root[1] = 0xfeed000000004003ULL;
    for (Index = 2; Index < APPLE_UAT_ROOT_ENTRIES; ++Index)
        Root[Index] = 0xdead000000000003ULL;
    for (Index = 0; Index < APPLE_UAT_TABLE_ENTRIES; ++Index)
        Level[Index] = 0xdead000000000003ULL;
    for (Index = 0; Index < 2 * APPLE_UAT_CONTEXTS; ++Index)
        Contexts[Index] = 0xdead000000000001ULL;

    ok_hex(AppleUatAdopt(&Adopted, HandoffPhysical + 1, ContextsPhysical, RootPhysical, LevelPhysical, "test"),
           STATUS_INVALID_PARAMETER);
    ok_hex(AppleUatAdopt(&Adopted, 0, ContextsPhysical, RootPhysical, LevelPhysical, "test"),
           STATUS_INVALID_PARAMETER);
    ok_hex(AppleUatAdopt(&Adopted, HandoffPhysical, ContextsPhysical, RootPhysical, LevelPhysical, "test"),
           STATUS_SUCCESS);
    ok(Adopted.Lower.TopPhysical != 0 && Adopted.Host.TopPhysical != 0, "The host tables have no root\n");

    /* A silent firmware is reported rather than waited for for ever. */
    SimSleptMs = 0;
    ok_hex(AppleUatAdoptedHandshake(&Adopted), STATUS_DEVICE_NOT_READY);
    ok(SimSleptMs >= 1000, "Waited %lu ms for the firmware\n", SimSleptMs);
    ok(Handoff[0] == 0x4b1d000000000002ULL, "The host magic is %I64x\n", Handoff[0]);
    ok(*(PULONG)((PUCHAR)Handoff + 0x18) == 0xffffffffUL, "The slot is %08lx\n",
       *(PULONG)((PUCHAR)Handoff + 0x18));

    /* Once the firmware answers, the flush requests are cleared and nothing past them is touched. */
    Handoff[1] = 0x4b1d000000000002ULL;
    for (Index = 0; Index < 0x618 / sizeof(ULONG64); ++Index)
        Handoff[0x20 / sizeof(ULONG64) + Index] = 0xdeadbeefdeadbeefULL;
    ((PUCHAR)Handoff)[0x638] = 0x5a;
    ok_hex(AppleUatAdoptedHandshake(&Adopted), STATUS_SUCCESS);
    for (Index = 0, Set = 0; Index < 0x618 / sizeof(ULONG64); ++Index)
    {
        if (Handoff[0x20 / sizeof(ULONG64) + Index])
            ++Set;
    }
    ok_int(Set, 0);
    ok_int(((PUCHAR)Handoff)[0x638], 0x5a);

    /* The install leaves the firmware's own entries alone and publishes the host window. */
    ok_hex(AppleUatAdoptedInstall(&Adopted), STATUS_SUCCESS);
    ok(Root[0] == 0xfeed000000000003ULL && Root[1] == 0xfeed000000004003ULL,
       "The install changed the firmware entries to %I64x %I64x\n", Root[0], Root[1]);
    ok(Root[2] == ((LevelPhysical & APPLE_UAT_OUTPUT_MASK) | 3), "The window entry is %I64x\n", Root[2]);
    for (Index = 3, Set = 0; Index < APPLE_UAT_ROOT_ENTRIES; ++Index)
    {
        if (Root[Index])
            ++Set;
    }
    ok_int(Set, 0);
    ok(Contexts[0] == ((Adopted.Lower.TopPhysical & APPLE_UAT_OUTPUT_MASK) | 1),
       "The lower half of context 0 is %I64x\n", Contexts[0]);
    ok(Contexts[1] == ((RootPhysical & APPLE_UAT_OUTPUT_MASK) | 1), "The upper half of context 0 is %I64x\n",
       Contexts[1]);
    for (Index = 2, Set = 0; Index < 2 * APPLE_UAT_CONTEXTS; ++Index)
    {
        if (Contexts[Index])
            ++Set;
    }
    ok_int(Set, 0);
    for (Index = 0, Set = 0; Index < APPLE_UAT_TABLE_ENTRIES; ++Index)
    {
        if (Level[Index])
            ++Set;
    }
    ok_int(Set, 0);

    /* A mapping in the window appears in the level the firmware walks, at the slot of its address. */
    ok_hex(AppleUatAdoptedMap(&Adopted, APPLE_UAT_HOST_BASE + 0x8000000ULL, 0x30000000ULL,
                              2 * APPLE_UAT_PAGE_SIZE, &Shared), STATUS_SUCCESS);
    ok(AppleUatAdoptedTranslate(&Adopted, APPLE_UAT_HOST_BASE + 0x8000000ULL, &Physical) &&
       Physical == 0x30000000ULL, "The window mapping resolves to %I64x\n", Physical);
    ok(Level[4] != 0, "The window level has no entry for the mapping\n");
    for (Index = 0, Set = 0; Index < APPLE_UAT_TABLE_ENTRIES; ++Index)
    {
        if (Level[Index])
            ++Set;
    }
    ok_int(Set, 1);

    /* Addresses outside the window belong to the firmware and are refused. */
    ok_hex(AppleUatAdoptedMap(&Adopted, APPLE_UAT_HOST_BASE - APPLE_UAT_PAGE_SIZE, 0x30000000ULL,
                              APPLE_UAT_PAGE_SIZE, &Shared), STATUS_INVALID_PARAMETER);
    ok_hex(AppleUatAdoptedMap(&Adopted, APPLE_UAT_HOST_BASE + APPLE_UAT_HOST_SIZE, 0x30000000ULL,
                              APPLE_UAT_PAGE_SIZE, &Shared), STATUS_INVALID_PARAMETER);
    ok_hex(AppleUatAdoptedMap(&Adopted, APPLE_UAT_HOST_BASE + APPLE_UAT_HOST_SIZE - APPLE_UAT_PAGE_SIZE,
                              0x30000000ULL, 2 * APPLE_UAT_PAGE_SIZE, &Shared), STATUS_INVALID_PARAMETER);

    AppleUatAdoptedRelease(&Adopted);
    ok(Adopted.Handoff == NULL && Adopted.Contexts == NULL && Adopted.KernelRoot == NULL &&
       Adopted.HostLevel == NULL, "The firmware pages are still mapped\n");
    ok(Adopted.Lower.Top == NULL && Adopted.Host.Top == NULL, "The host tables survived the release\n");
}

static
VOID
TestSpmi(VOID)
{
    static const UCHAR Offset[6] = { 0x11, 0x22, 0x33, 0x44, 0x55, 0x66 };
    FDT_DRIVER_TREE Tree;
    APPLE_NVMEM_CELL Cell;
    APPLE_SPMI Spmi;
    UCHAR Buffer[12];
    NTSTATUS Status;

    ResetSimulation(&Tree);
    memcpy(&SimNvmem[0x2100], Offset, sizeof(Offset));
    SimNvmem[0x2106] = 0x77;
    SimNvmem[0x2107] = 0x88;
    SimNvmem[0x2108] = 0x99;
    SimNvmem[0xf802] = 0x53;

    Status = AppleNvmemReadCell(&Tree, PHANDLE_RTC_OFFSET, &Cell);
    ok_hex(Status, STATUS_SUCCESS);
    ok_int(Cell.Length, 6);
    ok_hex(Cell.Address, 0x2100);
    ok_int(Cell.BitOffset, 0);
    ok_int(Cell.BitCount, 48);
    ok(!memcmp(Cell.Data, Offset, sizeof(Offset)), "Cell data is %02x %02x ... %02x\n", Cell.Data[0], Cell.Data[1], Cell.Data[5]);
    ok_int(SimSpmiCommandCount, 1);
    ok_hex(SimSpmiCommands[0], 0x21008b3d);
    ok_int(SimMapCount, 0);

    Status = AppleNvmemReadCell(&Tree, PHANDLE_BOOT_ERRORS, &Cell);
    ok_hex(Status, STATUS_SUCCESS);
    ok_hex(Cell.Data[0], 0x53);
    ok_int(Cell.BitOffset, 0);
    ok_int(Cell.BitCount, 4);
    Status = AppleNvmemReadCell(&Tree, PHANDLE_PANICS, &Cell);
    ok_hex(Status, STATUS_SUCCESS);
    ok_int(Cell.BitOffset, 4);
    ok_int(Cell.BitCount, 4);

    Status = AppleNvmemReadCell(&Tree, 0x1234, &Cell);
    ok_hex(Status, STATUS_NOT_FOUND);

    Status = AppleSpmiOpen(&Tree, FindNode(&Tree, "apple,t8103-spmi"), &Spmi);
    ok_hex(Status, STATUS_SUCCESS);
    SimSpmiCommandCount = 0;
    Status = AppleSpmiRead(&Spmi, 0xb, 0x2100, Buffer, sizeof(Buffer));
    ok_hex(Status, STATUS_SUCCESS);
    ok_int(SimSpmiCommandCount, 2);
    ok_hex(SimSpmiCommands[0], 0x21008b3f);
    ok_hex(SimSpmiCommands[1], 0x21088b3b);
    ok_hex(Buffer[8], 0x99);

    SimSpmiBadParity = TRUE;
    SimWriteCount = 0;
    Status = AppleSpmiRead(&Spmi, 0xb, 0x2100, Buffer, 6);
    ok_hex(Status, STATUS_DEVICE_DATA_ERROR);
    SimSpmiBadParity = FALSE;

    SimSpmiSilent = TRUE;
    SimStalledMicroseconds = 0;
    Status = AppleSpmiRead(&Spmi, 0xb, 0x2100, Buffer, 6);
    ok_hex(Status, STATUS_IO_TIMEOUT);
    ok(SimStalledMicroseconds >= 50000, "Waited %lu us for the reply\n", SimStalledMicroseconds);
    ok(Spmi.Failed, "The failure was not remembered\n");

    SimSpmiSilent = FALSE;
    SimWriteCount = 0;
    Status = AppleSpmiRead(&Spmi, 0xb, 0xf802, Buffer, 1);
    ok_hex(Status, STATUS_SUCCESS);
    ok(SimWriteCount >= 3, "%lu writes\n", SimWriteCount);
    ok(SimWrites[0].Physical == SIM_SPMI_BASE + 0xa4 && SimWrites[0].Value == 1, "First write %I64x = %lx\n",
       SimWrites[0].Physical, SimWrites[0].Value);
    ok(SimWrites[1].Physical == SIM_SPMI_BASE + 0x80 && SimWrites[1].Value == 1, "Second write %I64x = %lx\n",
       SimWrites[1].Physical, SimWrites[1].Value);
    ok_hex(Buffer[0], 0x53);
    AppleSpmiClose(&Spmi);
    ok_int(SimMapCount, 0);

    SimSpmiCommandCount = 0;
    Status = AppleNvmemWriteCell(&Tree, PHANDLE_PANICS, 0);
    ok_hex(Status, STATUS_SUCCESS);
    ok_hex(SimNvmem[0xf802], 0x03);
    ok_int(SimSpmiCommandCount, 2);
    ok_hex(SimSpmiCommands[0], 0xf8028b38);
    ok_hex(SimSpmiCommands[1], 0xf8028b30);
    Status = AppleNvmemWriteCell(&Tree, PHANDLE_BOOT_ERRORS, 0);
    ok_hex(Status, STATUS_SUCCESS);
    ok_hex(SimNvmem[0xf802], 0x00);
    Status = AppleNvmemWriteCell(&Tree, PHANDLE_PANICS, 5);
    ok_hex(Status, STATUS_SUCCESS);
    ok_hex(SimNvmem[0xf802], 0x50);
    Status = AppleNvmemReadCell(&Tree, PHANDLE_PANICS, &Cell);
    ok_hex(Status, STATUS_SUCCESS);
    ok_int(AppleNvmemCellValue(&Cell), 5);
    Status = AppleNvmemWriteCell(&Tree, PHANDLE_PANICS, 16);
    ok_hex(Status, STATUS_INVALID_PARAMETER);
    ok_hex(SimNvmem[0xf802], 0x50);

    Status = AppleNvmemWriteCell(&Tree, PHANDLE_RTC_OFFSET, 0x123456);
    ok_hex(Status, STATUS_INVALID_PARAMETER);
    ok_hex(SimNvmem[0x2100], 0x11);

    SimSpmiNoAck = TRUE;
    Status = AppleNvmemWriteCell(&Tree, PHANDLE_PANICS, 0);
    ok_hex(Status, STATUS_DEVICE_PROTOCOL_ERROR);
    SimSpmiNoAck = FALSE;
    ok_int(SimMapCount, 0);
}

static
ULONG
FindWrite(ULONG64 Physical, ULONG Mask, ULONG Value, ULONG Start)
{
    ULONG Index;

    for (Index = Start; Index < SimWriteCount; ++Index)
    {
        if (SimWrites[Index].Physical == Physical && (SimWrites[Index].Value & Mask) == Value)
            return Index;
    }
    return MAXULONG;
}

static
VOID
TestAtcphy(VOID)
{
    FDT_DRIVER_TREE Tree;
    APPLE_ATCPHY Phy;
    ULONG Usb, ResetAsserted, PowerOn, HostMode, ResetReleased;
    NTSTATUS Status;

    ResetSimulation(&Tree);
    Status = AppleAtcphyOpen(&Tree, FindNode(&Tree, "apple,t8103-nvme-ans2"), &Phy);
    ok_hex(Status, STATUS_NOT_FOUND);
    ok_int(SimMapCount, 0);

    Usb = FindNode(&Tree, "apple,t8103-dwc3");
    Status = AppleAtcphyOpen(&Tree, Usb, &Phy);
    ok_hex(Status, STATUS_SUCCESS);
    ok(Phy.Node == FindNode(&Tree, "apple,t8122-atcphy"), "The PHY node is %lx\n", Phy.Node);
    ok(!Phy.HasAusPllTop, "A t8122 PHY was given the t8103 AUSPLL block\n");
    ok_int(SimMapCount, 4);
    ok_int(Phy.Core.Size, SIM_ATC_CORE_SIZE);
    ok_int(Phy.Axi2af.Size, SIM_ATC_AXI2AF_SIZE);
    ok_int(Phy.Usb2Phy.Size, SIM_REGION_SIZE);
    ok_int(Phy.PipeHandler.Size, SIM_REGION_SIZE);

    USB2PHY(0x04) = 0x0000000b;
    USB2PHY(0x1c) = 0x60000000;
    ATC(0x20000) = 0x0000001f;
    ATC(0x20004) = 0x00000003;
    ATC(0x20008) = 0x00000005;
    ATC(0x48) = 0x00000000;
    ATC(0x4c) = 0x0003ffff;
    ATC(0x9010) = 0x00000201;
    ATC(0x10010) = 0x00000201;
    ATC(0x20) = 0xabcdefff;
    ATC(0x24) = 0x00000000;
    ATC(0x2a04) = 0x00000003;
    AXI2AF(0x100) = 0x000000ff;
    PIPE(0x1c) = 0x00000001;
    PIPE(0x0c) = 0x00000009;
    SimWriteCount = 0;
    SimStalledMicroseconds = 0;
    Status = AppleAtcphyStartUsb2Host(&Tree, &Phy);
    ok_hex(Status, STATUS_SUCCESS);
    ok(SimWriteCount < RTL_NUMBER_OF(SimWrites), "The write log overflowed\n");

    ResetAsserted = FindWrite(SIM_ATC_PIPE_BASE + 0x1c, 0x11, 0x10, 0);
    PowerOn = FindWrite(SIM_ATC_USB2_BASE + 0x00, MAXULONG, 2, 0);
    HostMode = FindWrite(SIM_ATC_USB2_BASE + 0x08, 0x7000, 0x7000, 0);
    ResetReleased = FindWrite(SIM_ATC_PIPE_BASE + 0x1c, 0x11, 0x01, 0);
    ok(ResetAsserted != MAXULONG && PowerOn != MAXULONG && ResetAsserted < PowerOn,
       "The dwc3 reset was not held before the PHY came up (%lu, %lu)\n", ResetAsserted, PowerOn);
    ok(HostMode != MAXULONG && ResetReleased != MAXULONG && HostMode < ResetReleased,
       "The USB2 PHY was not in host mode before the dwc3 reset was released (%lu, %lu)\n", HostMode, ResetReleased);
    ok(FindWrite(SIM_ATC_USB2_BASE + 0x00, MAXULONG, 4, 0) < PowerOn, "The USB2 PHY was not isolated first\n");

    ok_hex(USB2PHY(0x00), 0x00000002);
    ok_hex(USB2PHY(0x04), 0x00000004);
    ok_hex(USB2PHY(0x08), 0x0000700f);
    ok_hex(USB2PHY(0x1c), 0x00000000);
    ok_hex(ATC(0x20000), 0x0000001b);
    ok_hex(ATC(0x20008), 0x00000001);
    ok_hex(ATC(0x48), 0x000006db);
    ok_hex(ATC(0x4c), 0x0000000a);
    ok_hex(ATC(0x9010), 0x00000200);
    ok_hex(ATC(0x10010), 0x00000200);
    ok_hex(ATC(0x08), 0x0003332a);
    ok_hex(ATC(0x1b0), 0x00000ccc);
    ok_hex(ATC(0x1014), 0x00000000);
    ok_hex(ATC(0x2000), 0x00000000);
    ok_hex(ATC(0x2a00), 0x00000022);
    ok_int(Phy.TunableEntries[0], 2);
    ok_int(Phy.TunableEntries[1], 1);
    ok_int(Phy.TunableEntries[2], 0);
    ok_hex(ATC(0x20), 0xabcdef12);
    ok_hex(ATC(0x2a04), 0x0000002b);
    ok_hex(ATC(0x24), 0x00000000);
    ok_hex(AXI2AF(0x100), 0x0000003f);
    ok_hex(PIPE(0x1c), 0x00000001);
    ok_hex(PIPE(0x0c), 0x00000022);
    ok(SimStalledMicroseconds < 2000, "The bring-up stalled for %lu us\n", SimStalledMicroseconds);

    PIPE(0x00) = 0x00000000;
    PIPE(0x04) = 0x00000017;
    PIPE(0x0c) = 0x00000009;
    PIPE(0x20) = 0x0000000f;
    Status = AppleAtcphySelectDummyPipe(&Phy);
    ok_hex(Status, STATUS_SUCCESS);
    ok_hex(PIPE(0x00), 0x00000005);
    ok_hex(PIPE(0x04), 0x00000011);
    ok_hex(PIPE(0x0c), 0x00000022);
    ok_hex(PIPE(0x10), 0x00000000);
    ok_hex(PIPE(0x14), 0x00000000);
    ok_hex(PIPE(0x20), 0x00009002);

    AppleAtcphyStopUsb2Host(&Phy);
    ok_hex(PIPE(0x1c) & 0x11, 0x10);
    ok_hex(PIPE(0x0c), 0x00000022);
    ok_hex(USB2PHY(0x00), 0x00000004);
    ok_hex(USB2PHY(0x04) & 0x0f, 0x0000000b);
    ok_hex(USB2PHY(0x1c) & 0x60000000, 0x60000000);

    SimAtcStuck = TRUE;
    ATC(0x20004) = 0;
    SimStalledMicroseconds = 0;
    Status = AppleAtcphyStartUsb2Host(&Tree, &Phy);
    ok_hex(Status, STATUS_IO_TIMEOUT);
    ok(SimStalledMicroseconds >= 100000 && SimStalledMicroseconds < 102000,
       "A stuck PHY stalled for %lu us\n", SimStalledMicroseconds);
    SimAtcStuck = FALSE;

    AppleAtcphyClose(&Phy);
    ok_int(SimMapCount, 0);
    ok(Phy.Core.Base == NULL && Phy.PipeHandler.Base == NULL, "The PHY regions are still mapped\n");
}

static ULONG TestSmcGpioCalls;
static BOOLEAN TestSmcGpioLowBank;
static UCHAR TestSmcGpioPin;
static BOOLEAN TestSmcGpioValue;

static
NTSTATUS
NTAPI
TestSetSmcGpio(PVOID Context, BOOLEAN LowBank, UCHAR Pin, BOOLEAN Value)
{
    TestSmcGpioCalls++;
    TestSmcGpioLowBank = LowBank;
    TestSmcGpioPin = Pin;
    TestSmcGpioValue = Value;
    return (Context == (PVOID)1) ? STATUS_DEVICE_NOT_READY : STATUS_SUCCESS;
}

static
VOID
TestPcie(VOID)
{
    FDT_DRIVER_TREE Tree;
    APPLE_PCIE Pcie;
    PAPPLE_PCIE_PORT Port;
    ULONG Node, ResetLow, PerstOff, ResetHigh;
    NTSTATUS Status;

    ResetSimulation(&Tree);
    Node = FindNode(&Tree, "apple,t6020-pcie");
    PMGR(0x5b0) = 0;
    Status = ApplePcieOpen(&Tree, Node, &Pcie);
    ok_hex(Status, STATUS_SUCCESS);
    ok_int(Pcie.PortCount, 1);
    ok(Pcie.ConfigBase == 0x580000000ULL && Pcie.ConfigSize == 0x1000000ULL, "Config space %I64x+%I64x\n",
       Pcie.ConfigBase, Pcie.ConfigSize);
    ok_int(Pcie.MsiVectors, 32);
    ok_int(Pcie.FirstBus, 0);
    ok_int(Pcie.LastBus, 4);
    ok_int(Pcie.WindowCount, 2);
    ok(Pcie.Windows[0].BusBase == 0x5a0000000ULL && Pcie.Windows[0].Size == 0x20000000 && Pcie.Windows[0].Prefetchable,
       "Window 0 %I64x+%lx prefetch %u\n", Pcie.Windows[0].BusBase, Pcie.Windows[0].Size, Pcie.Windows[0].Prefetchable);
    ok(Pcie.Windows[1].BusBase == 0xc0000000ULL && Pcie.Windows[1].Size == 0x40000000 && !Pcie.Windows[1].Prefetchable,
       "Window 1 %I64x+%lx prefetch %u\n", Pcie.Windows[1].BusBase, Pcie.Windows[1].Size, Pcie.Windows[1].Prefetchable);
    ok(Pcie.Pinctrl.Base != NULL, "The pin controller is not mapped\n");
    ok_hex(PMGR(0x5b0) & 0xff, 0xff);
    ok_hex(PINCTRL(0xa3 * 4), 0x220);
    ok_hex(PINCTRL(0xa6 * 4), 0x220);
    ok_hex(PINCTRL(0xa7 * 4), 0);
    Port = &Pcie.Ports[0];
    ok_int(Port->Index, 0);
    ok_int(Port->Bus, 1);

    Pcie.SetSmcGpio = TestSetSmcGpio;
    TestSmcGpioCalls = 0;
    SimWriteCount = 0;
    SimSleptMs = 0;
    for (Node = 16; Node < 20; ++Node)
        PCIEPORT(0x3000 + 4 * Node) = 0;
    PINCTRL(0xa7 * 4) = 0x40;
    Status = ApplePcieStartPort(&Tree, &Pcie, Port);
    ok_hex(Status, STATUS_SUCCESS);
    ok(SimWriteCount < RTL_NUMBER_OF(SimWrites), "The write log overflowed\n");
    ok(!Port->WasUp && Port->LinkUp, "Link state %u/%u\n", Port->WasUp, Port->LinkUp);
    ok_int(TestSmcGpioCalls, 1);
    ok(!TestSmcGpioLowBank && TestSmcGpioPin == 0x13 && TestSmcGpioValue, "SMC GPIO %u/%u/%u\n", TestSmcGpioLowBank,
       TestSmcGpioPin, TestSmcGpioValue);
    ResetLow = FindWrite(SIM_PINCTRL_BASE + 0xa7 * 4, 0x6f, 0x02, 0);
    PerstOff = FindWrite(SIM_PCIE_PORT0_BASE + 0x82c, 1, 1, 0);
    ResetHigh = FindWrite(SIM_PINCTRL_BASE + 0xa7 * 4, 0x6f, 0x03, 0);
    ok(ResetLow != MAXULONG && PerstOff != MAXULONG && ResetHigh != MAXULONG && ResetLow < PerstOff && PerstOff < ResetHigh,
       "PERST# order %lu %lu %lu\n", ResetLow, PerstOff, ResetHigh);
    ok(FindWrite(SIM_PCIE_PHY0_BASE, 0x600, 0x600, 0) < PerstOff, "The reference clock was not enabled before PERST#\n");
    ok_hex(PINCTRL(0xa7 * 4), 0x03);
    ok_hex(PCIEPHY(0), 0xc000060f);
    ok_hex(PCIEPORT(0x800), 0x00000001);
    ok_hex(PCIEPORT(0x104), 0xffffffff);
    ok_hex(PCIEPORT(0x16c), 0xfffff000);
    ok_hex(PCIEPORT(0x170), 0);
    ok_hex(PCIEPORT(0x3800), 0x80000000);
    ok_hex(PCIEPORT(0x3800 + 31 * 4), 0x8000001f);
    ok_hex(PCIEPORT(0x3800 + 32 * 4), 0);
    ok_hex(PCIEPORT(0x124), 1);
    ok_int(Port->Rid2SidEntries, 16);
    ok_hex(PCIEPORT(0x3000), 0);
    ok_hex(PCIEPORT(0x80), 1);
    ok(SimSleptMs >= 200 && SimSleptMs < 300, "Bring-up slept %lu ms\n", SimSleptMs);

    TestSmcGpioCalls = 0;
    SimWriteCount = 0;
    Status = ApplePcieStartPort(&Tree, &Pcie, Port);
    ok_hex(Status, STATUS_SUCCESS);
    ok(Port->WasUp && Port->LinkUp, "Kept link state %u/%u\n", Port->WasUp, Port->LinkUp);
    ok_int(TestSmcGpioCalls, 0);
    ok(FindWrite(SIM_PCIE_PORT0_BASE + 0x82c, 0, 0, 0) == MAXULONG, "PERST# was touched on a live link\n");
    ok(FindWrite(SIM_PCIE_PORT0_BASE + 0x80, 0, 0, 0) == MAXULONG, "Link training restarted on a live link\n");

    PCIEPORT(0x208) = 0;
    PCIEPORT(0x804) = 0;
    SimPcieNoLink = TRUE;
    Status = ApplePcieStartPort(&Tree, &Pcie, Port);
    ok_hex(Status, STATUS_DEVICE_NOT_CONNECTED);
    ok(!Port->LinkUp && Port->LinkWaitMs == 500, "No link: %u after %lu ms\n", Port->LinkUp, Port->LinkWaitMs);
    SimPcieNoLink = FALSE;

    PCIEPORT(0x208) = 0;
    PCIEPORT(0x804) = 0;
    Pcie.SmcGpioContext = (PVOID)1;
    Status = ApplePcieStartPort(&Tree, &Pcie, Port);
    ok_hex(Status, STATUS_DEVICE_NOT_READY);
    Pcie.SmcGpioContext = NULL;

    PCIECFG(0x0000) = 0x443314e4;
    PCIECFG(0x1000) = 0x5f7114e4;
    PCIECFG(0x2000) = 0xffffffff;
    ok_hex(ApplePcieReadConfig(&Pcie, 1, 0, 0, 0), 0x443314e4);
    ok_hex(ApplePcieReadConfig(&Pcie, 1, 0, 1, 0), 0x5f7114e4);
    ok_hex(ApplePcieReadConfig(&Pcie, 1, 0, 2, 0), 0xffffffff);
    ok_hex(ApplePcieReadConfig(&Pcie, 16, 0, 0, 0), 0xffffffff);

    PCIEPORT(0x208) = 0;
    PCIEPORT(0x804) = 0;
    Status = ApplePcieStartPort(&Tree, &Pcie, Port);
    ok_hex(Status, STATUS_SUCCESS);
    DART(0x04) = 1;
    DART(0x0c) = 16;
    DART(0x1004) = 0;
    DART(0x1008) = 0;
    ok_int(ApplePcieMapPortDevices(&Tree, &Pcie, Port), 2);
    ok_int(Port->Rid2SidUsed, 2);
    ok_hex(PCIEPORT(0x3000), 0x80010100);
    ok_hex(PCIEPORT(0x3004), 0x80010101);
    ok_hex(PCIEPORT(0x3008), 0);
    ok_hex(DART(0x1004), 6);
    ok_hex(DART(0x1008), 0);

    Status = ApplePcieStartPort(&Tree, &Pcie, Port);
    ok_hex(Status, STATUS_SUCCESS);
    ok_int(Port->Rid2SidUsed, 0);
    ok_hex(PCIEPORT(0x3000), 0);
    ok_int(ApplePcieMapPortDevices(&Tree, &Pcie, Port), 2);
    ok_hex(PCIEPORT(0x3000), 0x80010100);
    ok_hex(PCIEPORT(0x3008), 0);

    Port->LinkUp = FALSE;
    ok_int(ApplePcieMapPortDevices(&Tree, &Pcie, Port), 0);

    ApplePcieClose(&Pcie);
    ok_int(SimMapCount, 0);
}

#define PHANDLE_AIC         0x13
#define PHANDLE_CLKREF      0x14
#define PHANDLE_I2C_PINS    0x18
#define PHANDLE_I2C2_PD     0x19
#define PHANDLE_SIO_ADMA    0x29
#define PHANDLE_AUDIO_P     0x2a
#define PHANDLE_NCO         0x2b
#define PHANDLE_ADMAC       0x2c
#define PHANDLE_MCA0_PD     0x2d
#define PHANDLE_NCO_REF     0x62

static ULONG SimTranslateCalls;

BOOLEAN
NTAPI
HalArm64TranslateDeviceTreeInterrupt(
    const CHAR *Compatible,
    ULONG CompatibleLength,
    const ULONG *Cells,
    ULONG CellCount,
    PULONG Gsi,
    PBOOLEAN EdgeTriggered)
{
    SimTranslateCalls++;
    if (!RiscvFdtStringListContains(Compatible, CompatibleLength, "apple,t8122-aic3") || CellCount != 3 || Cells[0])
        return FALSE;
    *Gsi = 32 + Cells[1];
    *EdgeTriggered = (Cells[2] & 3) != 0;
    return TRUE;
}

static
VOID
BuildAudioTree(ULONG I2cFrequency)
{
    memset(&Builder, 0, sizeof(Builder));
    FdtBeginNode("");
    FdtCells("#address-cells", 1, 2);
    FdtCells("#size-cells", 1, 2);
    FdtBeginNode("clock-ref");
    FdtStrings("compatible", LIST("fixed-clock"));
    FdtCells("clock-frequency", 1, 24000000);
    FdtCells("phandle", 1, PHANDLE_CLKREF);
    FdtEndNode();
    FdtBeginNode("clock-ref-nco");
    FdtStrings("compatible", LIST("fixed-clock"));
    FdtCells("clock-frequency", 1, 900000000);
    FdtCells("phandle", 1, PHANDLE_NCO_REF);
    FdtEndNode();
    FdtBeginNode("soc");
    FdtStrings("compatible", LIST("simple-bus"));
    FdtCells("#address-cells", 1, 2);
    FdtCells("#size-cells", 1, 2);
    FdtProperty("ranges", NULL, 0);

    FdtBeginNode("power-management@292280000");
    FdtStrings("compatible", LIST("apple,t8103-pmgr"));
    FdtCells("#address-cells", 1, 1);
    FdtCells("#size-cells", 1, 1);
    FdtCells("reg", 4, 2, 0x92280000, 0, 0x4000);
    FdtBeginNode("power-controller@200");
    FdtStrings("compatible", LIST("apple,t8103-pmgr-pwrstate"));
    FdtCells("reg", 2, 0x200, 4);
    FdtCells("phandle", 1, PHANDLE_I2C2_PD);
    FdtEndNode();
    FdtBeginNode("power-controller@248");
    FdtStrings("compatible", LIST("apple,t8103-pmgr-pwrstate"));
    FdtCells("reg", 2, 0x248, 4);
    FdtCells("phandle", 1, PHANDLE_AUDIO_P);
    FdtEndNode();
    FdtBeginNode("power-controller@2d8");
    FdtStrings("compatible", LIST("apple,t8103-pmgr-pwrstate"));
    FdtCells("reg", 2, 0x2d8, 4);
    FdtCells("power-domains", 1, PHANDLE_AUDIO_P);
    FdtCells("phandle", 1, PHANDLE_SIO_ADMA);
    FdtEndNode();
    FdtBeginNode("power-controller@308");
    FdtStrings("compatible", LIST("apple,t8103-pmgr-pwrstate"));
    FdtCells("reg", 2, 0x308, 4);
    FdtCells("power-domains", 2, PHANDLE_SIO_ADMA, PHANDLE_AUDIO_P);
    FdtProperty("apple,externally-clocked", NULL, 0);
    FdtCells("phandle", 1, PHANDLE_MCA0_PD);
    FdtEndNode();
    FdtEndNode();

    FdtBeginNode("pinctrl@347100000");
    FdtStrings("compatible", LIST("apple,t8103-pinctrl"));
    FdtCells("reg", 4, 3, 0x47100000, 0, 0x4000);
    FdtCells("#gpio-cells", 1, 2);
    FdtCells("apple,npins", 1, 0xe0);
    FdtCells("phandle", 1, PHANDLE_PINCTRL);
    FdtBeginNode("i2c2-pins");
    FdtCells("pinmux", 2, 0x1002b, 0x1002a);
    FdtCells("phandle", 1, PHANDLE_I2C_PINS);
    FdtEndNode();
    FdtEndNode();

    FdtBeginNode("i2c@289018000");
    FdtStrings("compatible", LIST("apple,t6030-i2c\0apple,t8103-i2c"));
    FdtCells("reg", 4, 2, 0x89018000, 0, 0x4000);
    FdtCells("clocks", 1, PHANDLE_CLKREF);
    if (I2cFrequency)
        FdtCells("clock-frequency", 1, I2cFrequency);
    FdtCells("pinctrl-0", 1, PHANDLE_I2C_PINS);
    FdtCells("power-domains", 1, PHANDLE_I2C2_PD);
    FdtCells("#address-cells", 1, 1);
    FdtCells("#size-cells", 1, 0);
    FdtBeginNode("codec@4b");
    FdtStrings("compatible", LIST("cirrus,cs42l84"));
    FdtCells("reg", 1, 0x4b);
    FdtCells("reset-gpios", 3, PHANDLE_PINCTRL, 0x15, 0);
    FdtCells("enable-gpios", 3, PHANDLE_PINCTRL, 0x16, 1);
    FdtCells("broken-gpios", 3, PHANDLE_PINCTRL, 0xe0, 0);
    FdtEndNode();
    FdtEndNode();

    FdtBeginNode("clock-controller@350044000");
    FdtStrings("compatible", LIST("apple,t6030-nco\0apple,t8103-nco"));
    FdtCells("reg", 4, 3, 0x50044000, 0, 0x14000);
    FdtCells("clocks", 1, PHANDLE_NCO_REF);
    FdtCells("#clock-cells", 1, 1);
    FdtCells("phandle", 1, PHANDLE_NCO);
    FdtEndNode();

    FdtBeginNode("interrupt-controller@351000000");
    FdtStrings("compatible", LIST("apple,t6030-aic3\0apple,t8122-aic3"));
    FdtCells("#interrupt-cells", 1, 3);
    FdtProperty("interrupt-controller", NULL, 0);
    FdtCells("phandle", 1, PHANDLE_AIC);
    FdtEndNode();

    FdtBeginNode("dma-controller@28b200000");
    FdtStrings("compatible", LIST("apple,t6030-admac\0apple,t8122-admac"));
    FdtCells("reg", 4, 2, 0x8b200000, 0, 0x34000);
    FdtCells("#dma-cells", 1, 1);
    FdtCells("dma-channels", 1, 24);
    FdtCells("interrupts-extended", 7, 0, PHANDLE_AIC, 0, 0x378, 4, 0, 0);
    FdtCells("power-domains", 1, PHANDLE_SIO_ADMA);
    FdtCells("phandle", 1, PHANDLE_ADMAC);
    FdtEndNode();

    FdtBeginNode("mca@28b400000");
    FdtStrings("compatible", LIST("apple,t6030-mca\0apple,t8103-mca"));
    FdtCells("reg", 8, 2, 0x8b400000, 0, 0x10000, 2, 0x8b300000, 0, 0x20000);
    FdtCells("clocks", 8, PHANDLE_NCO, 0, PHANDLE_NCO, 1, PHANDLE_NCO, 2, PHANDLE_NCO, 3);
    FdtCells("power-domains", 2, PHANDLE_AUDIO_P, PHANDLE_MCA0_PD);
    FdtEndNode();

    FdtEndNode();
    FdtEndNode();
    FdtFinish();
}

static
VOID
ResetAudioSimulation(PFDT_DRIVER_TREE Tree, ULONG I2cFrequency)
{
    ResetSimulation(Tree);
    memset(SimCodec, 0, sizeof(SimCodec));
    memset(SimAdmacChannels, 0, sizeof(SimAdmacChannels));
    SimI2cRxHead = SimI2cRxTail = SimI2cBytes = SimI2cPointer = SimI2cTxCount = 0;
    SimI2cNack = FALSE;
    SimAdmacPushedCount = 0;
    SimMcaStuck = FALSE;
    SimTranslateCalls = 0;
    BuildAudioTree(I2cFrequency);
    ok(Builder.StructureSize < sizeof(Builder.Structure) && Builder.StringsSize < sizeof(Builder.Strings),
       "The audio tree overflowed the builder (%lu, %lu)\n", Builder.StructureSize, Builder.StringsSize);
    memset(Tree, 0, sizeof(*Tree));
    Tree->Blob = Builder.Blob;
    ok(RiscvFdtOpen(Builder.Blob, Builder.BlobSize, &Tree->Fdt), "The audio tree did not open\n");
}

#define I2C(Offset) SimRegions[SIM_I2C_INDEX].Data[(Offset) / sizeof(ULONG)]
#define NCO(Offset) SimRegions[SIM_NCO_INDEX].Data[(Offset) / sizeof(ULONG)]
#define ADMAC(Offset) SimRegions[SIM_ADMAC_INDEX].Data[(Offset) / sizeof(ULONG)]
#define MCA(Offset) SimRegions[SIM_MCA_INDEX].Data[(Offset) / sizeof(ULONG)]
#define MCASWITCH(Offset) SimRegions[SIM_SWITCH_INDEX].Data[(Offset) / sizeof(ULONG)]

static
ULONG
FindChildNode(PFDT_DRIVER_TREE Tree, ULONG Parent, const CHAR *Name)
{
    ULONG Child, Length;
    const CHAR *ChildName;

    for (Child = RiscvFdtFirstChild(&Tree->Fdt, Parent); Child != RISCV_FDT_NO_NODE;
         Child = RiscvFdtNextSibling(&Tree->Fdt, Child))
    {
        ChildName = RiscvFdtNodeName(&Tree->Fdt, Child, &Length);
        if (ChildName && RiscvFdtNameMatches(ChildName, Length, Name, (ULONG)strlen(Name)))
            return Child;
    }
    return RISCV_FDT_NO_NODE;
}

static
VOID
TestGpio(VOID)
{
    FDT_DRIVER_TREE Tree;
    APPLE_GPIO Gpio;
    ULONG I2c, Codec;
    NTSTATUS Status;

    ResetAudioSimulation(&Tree, 0);
    I2c = FindNode(&Tree, "apple,t8103-i2c");
    Codec = FindChildNode(&Tree, I2c, "codec");
    ok(Codec != RISCV_FDT_NO_NODE, "No codec node\n");

    PINCTRL(4 * 0x2b) = 0x00000180;
    PINCTRL(4 * 0x2a) = 0x0000000f;
    Status = AppleGpioApplyPinctrl(&Tree, I2c);
    ok_hex(Status, STATUS_SUCCESS);
    ok_hex(PINCTRL(4 * 0x2b), 0x000003a0);
    ok_hex(PINCTRL(4 * 0x2a), 0x0000022f);
    ok_int(SimMapCount, 0);

    Status = AppleGpioOpen(&Tree, Codec, "reset-gpios", &Gpio);
    ok_hex(Status, STATUS_SUCCESS);
    ok_int(Gpio.Pin, 0x15);
    ok(!Gpio.ActiveLow, "reset-gpios is active high\n");
    PINCTRL(4 * 0x15) = 0x00000268;
    AppleGpioSet(&Gpio, TRUE);
    ok_hex(PINCTRL(4 * 0x15), 0x00000203);
    AppleGpioSet(&Gpio, FALSE);
    ok_hex(PINCTRL(4 * 0x15), 0x00000202);
    AppleGpioClose(&Gpio);
    ok_int(SimMapCount, 0);

    Status = AppleGpioOpen(&Tree, Codec, "enable-gpios", &Gpio);
    ok_hex(Status, STATUS_SUCCESS);
    ok(Gpio.ActiveLow, "enable-gpios is active low\n");
    AppleGpioSet(&Gpio, TRUE);
    ok_hex(PINCTRL(4 * 0x16), 0x00000002);
    AppleGpioSet(&Gpio, FALSE);
    ok_hex(PINCTRL(4 * 0x16), 0x00000003);
    AppleGpioClose(&Gpio);

    ok_hex(AppleGpioOpen(&Tree, Codec, "broken-gpios", &Gpio), STATUS_DEVICE_CONFIGURATION_ERROR);
    ok_hex(AppleGpioOpen(&Tree, Codec, "missing-gpios", &Gpio), STATUS_NOT_FOUND);
    ok_int(SimMapCount, 0);
}

static
VOID
TestI2c(VOID)
{
    static const UCHAR WriteRegister[] = { 0x50, 0x2a, 0x1f };
    static const UCHAR ReadRegister[] = { 0x50, 0x2a };
    FDT_DRIVER_TREE Tree;
    APPLE_I2C I2c;
    UCHAR Value = 0;
    ULONG Node;
    NTSTATUS Status;

    ResetAudioSimulation(&Tree, 0);
    Node = FindNode(&Tree, "apple,t8103-i2c");
    I2C(0x28) = 6;
    Status = AppleI2cOpen(&Tree, Node, &I2c);
    ok_hex(Status, STATUS_SUCCESS);
    ok_hex(I2c.Control, 0x00000f0f);
    ok_hex(I2C(0x1c), 0x00000f0f);
    ok_hex(I2C(0x18), 0);
    ok_hex(PMGR(0x200) & 0xff, 0xff);
    ok_hex(PINCTRL(4 * 0x2b) & 0x260, 0x220);

    Status = AppleI2cTransfer(&I2c, SIM_CODEC_ADDRESS, WriteRegister, sizeof(WriteRegister), NULL, 0);
    ok_hex(Status, STATUS_SUCCESS);
    ok_int(SimI2cTxCount, 4);
    ok_hex(SimI2cTx[0], 0x196);
    ok_hex(SimI2cTx[1], 0x50);
    ok_hex(SimI2cTx[2], 0x2a);
    ok_hex(SimI2cTx[3], 0x21f);
    ok_hex(SimCodec[0x502a], 0x1f);
    ok_hex(I2C(0x14) & 0x08000000, 0);

    SimI2cTxCount = 0;
    Status = AppleI2cTransfer(&I2c, SIM_CODEC_ADDRESS, ReadRegister, sizeof(ReadRegister), &Value, 1);
    ok_hex(Status, STATUS_SUCCESS);
    ok_hex(Value, 0x1f);
    ok_int(SimI2cTxCount, 5);
    ok_hex(SimI2cTx[0], 0x196);
    ok_hex(SimI2cTx[2], 0x2a);
    ok_hex(SimI2cTx[3], 0x197);
    ok_hex(SimI2cTx[4], 0x601);

    I2C(0x1c) = 0;
    SimI2cTxCount = 0;
    Status = AppleI2cTransfer(&I2c, 0x10, ReadRegister, sizeof(ReadRegister), &Value, 1);
    ok_hex(Status, STATUS_NO_SUCH_DEVICE);
    ok_hex(I2C(0x1c), 0x00000f0f);
    ok_hex(SimI2cTx[0], 0x120);

    ok_hex(AppleI2cTransfer(&I2c, SIM_CODEC_ADDRESS, NULL, 0, NULL, 0), STATUS_INVALID_PARAMETER);
    ok_hex(AppleI2cTransfer(&I2c, 0x80, ReadRegister, sizeof(ReadRegister), NULL, 0), STATUS_INVALID_PARAMETER);
    AppleI2cClose(&I2c);
    ok_int(SimMapCount, 0);

    ResetAudioSimulation(&Tree, 400000);
    Node = FindNode(&Tree, "apple,t8103-i2c");
    I2C(0x28) = 5;
    Status = AppleI2cOpen(&Tree, Node, &I2c);
    ok_hex(Status, STATUS_SUCCESS);
    ok_hex(I2c.Control, 0x00000704);
    AppleI2cClose(&I2c);

    ResetAudioSimulation(&Tree, 1000000);
    Node = FindNode(&Tree, "apple,t8103-i2c");
    ok_hex(AppleI2cOpen(&Tree, Node, &I2c), STATUS_DEVICE_CONFIGURATION_ERROR);
    ok_int(SimMapCount, 0);
}

static
ULONG
TestNcoDecodeCoarse(ULONG Code)
{
    ULONG State = 0x7ff, Steps;

    if (!Code)
        return 0;
    for (Steps = 1; Steps <= 0x7ff; ++Steps)
    {
        State = (State & 1) ? ((State >> 1) ^ 0x500) : (State >> 1);
        if (State == Code)
            return 0x7ff - Steps + 1;
    }
    return MAXULONG;
}

static
VOID
TestNco(VOID)
{
    FDT_DRIVER_TREE Tree;
    APPLE_NCO Nco;
    ULONG Mca, Divider, Increment1, Increment2, Base;
    ULONG64 Rate;
    NTSTATUS Status;

    ResetAudioSimulation(&Tree, 0);
    Mca = FindNode(&Tree, "apple,t8103-mca");
    Status = AppleNcoOpen(&Tree, Mca, 2, &Nco);
    ok_hex(Status, STATUS_SUCCESS);
    ok_int(Nco.ParentRate, 900000000);
    ok(Nco.Registers == (PUCHAR)&NCO(2 * 0x4000), "Channel 2 is mapped at %p\n", Nco.Registers);
    AppleNcoClose(&Nco);

    Status = AppleNcoOpen(&Tree, Mca, 0, &Nco);
    ok_hex(Status, STATUS_SUCCESS);
    NCO(0) = 0;
    Status = AppleNcoSetRate(&Nco, 3072000);
    ok_hex(Status, STATUS_SUCCESS);
    Divider = NCO(4);
    Increment1 = NCO(8);
    Increment2 = NCO(12);
    ok_int(TestNcoDecodeCoarse(Divider >> 2) + 2, 146);
    ok_int(Divider & 3, 1);
    ok_int(Increment1, 2880000);
    ok_hex(Increment2, (ULONG)(2880000 - 3072000));
    ok_hex(NCO(16), 0x80000000);
    ok_hex(NCO(0), 0);
    Base = Increment1 - Increment2;
    Rate = 2ULL * 900000000 * Base / ((ULONG64)((TestNcoDecodeCoarse(Divider >> 2) + 2) * 4 + (Divider & 3)) * Base + Increment1);
    ok(Rate == 3072000, "The programmed NCO runs at %I64u Hz\n", Rate);

    AppleNcoEnable(&Nco, TRUE);
    ok_hex(NCO(0), 0x80000000);
    Status = AppleNcoSetRate(&Nco, 2822400);
    ok_hex(Status, STATUS_SUCCESS);
    ok_hex(NCO(0), 0x80000000);
    ok_int(TestNcoDecodeCoarse(NCO(4) >> 2) + 2, 2 * 900000000 / 2822400 / 4);
    AppleNcoEnable(&Nco, FALSE);
    ok_hex(NCO(0), 0);

    ok_hex(AppleNcoSetRate(&Nco, 900000000), STATUS_NOT_SUPPORTED);
    ok_hex(AppleNcoSetRate(&Nco, 100000), STATUS_NOT_SUPPORTED);
    AppleNcoClose(&Nco);
    ok_hex(AppleNcoOpen(&Tree, Mca, 4, &Nco), STATUS_NOT_FOUND);
    ok_int(SimMapCount, 0);
}

static
VOID
TestAdmac(VOID)
{
    FDT_DRIVER_TREE Tree;
    APPLE_ADMAC Admac;
    APPLE_ADMAC_CHANNEL Channel, Second, Third;
    ULONG Node, Index, Gsi;
    BOOLEAN Edge, Global;
    NTSTATUS Status;

    ResetAudioSimulation(&Tree, 0);
    Node = FindNode(&Tree, "apple,t8122-admac");
    Status = AppleAdmacQueryInterrupt(&Tree, Node, &Index, &Gsi, &Edge);
    ok_hex(Status, STATUS_SUCCESS);
    ok_int(Index, 1);
    ok_int(Gsi, 32 + 0x378);
    ok(!Edge, "The ADMAC interrupt is level triggered\n");

    Status = AppleAdmacOpen(&Tree, Node, &Admac);
    ok_hex(Status, STATUS_SUCCESS);
    ok_int(Admac.ChannelCount, 24);
    ok(Admac.SetFifoLimits, "t8122 needs the FIFO limit registers\n");
    ok_int(SimTranslateCalls, 2);
    ok_hex(PMGR(0x2d8) & 0xff, 0xff);
    ok_hex(PMGR(0x248) & 0xff, 0xff);

    ADMAC(0x94) = 0x1000;
    Status = AppleAdmacOpenChannel(&Admac, 0, 2, 2, &Channel);
    ok_hex(Status, STATUS_SUCCESS);
    ok_hex(ADMAC(0x8050), 0x08000000);
    ok_hex(ADMAC(0x8040), 0x11);
    ok_hex(ADMAC(0x8054), 0x00600030);
    ok_hex(ADMAC(0x28), 0x00200000);
    ok_hex(ADMAC(0x2c), 0x00200000);
    Status = AppleAdmacOpenChannel(&Admac, 2, 4, 2, &Second);
    ok_hex(Status, STATUS_SUCCESS);
    ok_hex(ADMAC(0x8450), 0x08000800);
    ok_hex(ADMAC(0x8440), 0x12);
    ok_hex(AppleAdmacOpenChannel(&Admac, 4, 2, 2, &Third), STATUS_INSUFFICIENT_RESOURCES);
    AppleAdmacCloseChannel(&Second);
    ok_hex(Admac.SramUsed[0], 1);
    Status = AppleAdmacOpenChannel(&Admac, 4, 2, 4, &Third);
    ok_hex(Status, STATUS_SUCCESS);
    ok_hex(ADMAC(0x8850), 0x08000800);
    ok_hex(ADMAC(0x8840), 0x21);
    AppleAdmacCloseChannel(&Third);
    ok_hex(AppleAdmacOpenChannel(&Admac, 4, 3, 2, &Third), STATUS_NOT_SUPPORTED);
    ok_hex(AppleAdmacOpenChannel(&Admac, 24, 2, 2, &Third), STATUS_INVALID_PARAMETER);

    ok_hex(AppleAdmacStartCyclic(&Channel, 0x10000000000ULL, 3840, 1000), STATUS_INVALID_PARAMETER);
    Status = AppleAdmacStartCyclic(&Channel, 0x10000000000ULL, 3840, 1920);
    ok_hex(Status, STATUS_SUCCESS);
    ok_int(SimAdmacPushedCount, 4);
    ok_hex(SimAdmacPushed[0][0], 0);
    ok_hex(SimAdmacPushed[0][1], 0x100);
    ok_hex(SimAdmacPushed[0][2], 1920);
    ok_hex(SimAdmacPushed[0][3], 0x10000);
    ok_hex(SimAdmacPushed[1][0], 1920);
    ok_hex(SimAdmacPushed[2][0], 0);
    ok_hex(SimAdmacPushed[3][0], 1920);
    ok_hex(ADMAC(0x0000), 1);
    ok_hex(ADMAC(0x8024), 0x41);

    ok(!AppleAdmacInterruptPending(&Admac, 0, &Global), "No interrupt is pending yet\n");
    ok(!Global, "No global interrupt yet\n");
    SimAdmacComplete(0, 2);
    ADMAC(0x54) = 0x4;
    ok(AppleAdmacInterruptPending(&Admac, 0, &Global), "The completed periods raise the interrupt\n");
    ok(Global, "The global interrupt was reported\n");
    ok_hex(ADMAC(0x54), 0);
    ok_int(AppleAdmacService(&Channel), 2);
    ok_int(Channel.Reclaimed, 3840);
    ok_int(SimAdmacPushedCount, 6);
    ok_hex(SimAdmacPushed[4][0], 0);
    ok_hex(SimAdmacPushed[5][0], 1920);
    ok(!AppleAdmacInterruptPending(&Admac, 0, &Global), "Servicing acknowledged the interrupt\n");
    ok_int(AppleAdmacService(&Channel), 0);

    ADMAC(0x8064) = 960;
    ok_int(AppleAdmacPosition(&Channel), 960);
    SimAdmacComplete(0, 1);
    ADMAC(0x8064) = 1920;
    ok_int(AppleAdmacPosition(&Channel), 1920);

    AppleAdmacStop(&Channel);
    ok_hex(ADMAC(0x0004), 1);
    ok_hex(ADMAC(0x8024), 0);
    ok_int(SimAdmacChannels[0].Count, 0);
    ok_int(AppleAdmacPosition(&Channel), 0);
    AppleAdmacCloseChannel(&Channel);
    ok_hex(Admac.SramUsed[0], 0);
    AppleAdmacClose(&Admac);
    ok_int(SimMapCount, 0);
}

static
VOID
SimAceSet(UCHAR Register, const UCHAR *Data, UCHAR Length)
{
    memcpy(SimAceRegs[Register], Data, Length);
    SimAceLength[Register] = Length;
}

static
VOID
TestAcePd(VOID)
{
    static const UCHAR PowerS5[] = { 5 }, Idle[4] = { 0 }, Event[8] = { 0x02, 0x02 }, Status[4] = { 0x61 };
    static const UCHAR Data[4] = { 0x11 }, Power[2] = { 0x05 };
    FDT_DRIVER_TREE Tree;
    APPLE_ACE_PD_STATE State;
    APPLE_SPMI Spmi;
    APPLE_ACE_PD Pd;
    UCHAR Previous = 0xff, Byte, Window[SIM_ACE_WINDOW_SIZE + 1];
    ULONG64 Mask = 0;
    NTSTATUS Status2;

    ResetSimulation(&Tree);
    Status2 = AppleSpmiOpen(&Tree, FindNode(&Tree, "apple,t8103-spmi"), &Spmi);
    ok_hex(Status2, STATUS_SUCCESS);
    SimAceEnabled = TRUE;
    Pd.Spmi = &Spmi;
    Pd.Sid = 0x0c;
    Pd.WakeInterrupt = 0x13;
    SimAceWakeInterrupt = 0x13;
    SimAceSet(0x20, PowerS5, sizeof(PowerS5));
    SimAceSet(0x08, Idle, sizeof(Idle));

    SimSpmiData[0x60 / sizeof(ULONG)] = 1UL << 0x13;
    SimAceNoWake = TRUE;
    SimSleptMs = 0;
    SimSpmiCommandCount = 0;
    ok_hex(AppleAcePdInitialize(&Pd, &Previous), STATUS_IO_TIMEOUT);
    ok_int(SimSleptMs, 100);
    ok_int(SimSpmiCommandCount, 1);
    ok_hex(SimSpmiCommands[0], 0x8c13);
    ok_int(Previous, 0xff);
    SimAceNoWake = FALSE;

    SimSpmiCommandCount = 0;
    Status2 = AppleAcePdInitialize(&Pd, &Previous);
    ok_hex(Status2, STATUS_SUCCESS);
    ok_int(Previous, 5);
    ok_int(SimAceRegs[0x20][0], 0);
    ok_int(SimAceLength[0x16], 8);
    memcpy(&Mask, SimAceRegs[0x16], sizeof(Mask));
    ok(Mask == 0x602, "Interrupt mask %I64x\n", Mask);
    ok_hex(SimSpmiCommands[0], 0x8c13);
    ok_hex(SimSpmiCommands[1], 0x20008ca0);
    ok_hex(SimSpmiCommands[2], 0x8c60);
    ok_hex(SimSpmiCommands[3], 0x00208c38);
    ok_hex(SimSpmiData[0x60 / sizeof(ULONG)], 0);
    ok_hex(SimSpmiData[0x20 / sizeof(ULONG)], 1UL << 0x13);

    SimSpmiCommandCount = 0;
    ok_hex(AppleSpmiRegisterRead(&Spmi, 0x0c, 0x05, &Byte), STATUS_SUCCESS);
    ok_int(SimSpmiCommandCount, 1);
    ok_hex(SimSpmiCommands[0], 0x00058c65);

    SimAceSet(0x14, Event, sizeof(Event));
    SimAceSet(0x1a, Status, sizeof(Status));
    SimAceSet(0x5f, Data, sizeof(Data));
    SimAceSet(0x3f, Power, sizeof(Power));
    Status2 = AppleAcePdPoll(&Pd, &State);
    ok_hex(Status2, STATUS_SUCCESS);
    ok(State.Events == 0x202, "Events %I64x\n", State.Events);
    ok_hex(State.Status, 0x61);
    ok_hex(State.DataStatus, 0x11);
    ok_hex(State.PowerStatus, 5);
    ok_int(SimAceRegs[0x14][0], 0);
    ok_int(SimAceRegs[0x14][1], 0);

    Status2 = AppleAcePdPoll(&Pd, &State);
    ok_hex(Status2, STATUS_SUCCESS);
    ok(State.Events == 0, "Events after clearing %I64x\n", State.Events);

    SimAceBusyReads = 3;
    SimSleptMs = 0;
    Status2 = AppleAcePdRead(&Pd, 0x1a, &Byte, 1);
    ok_hex(Status2, STATUS_SUCCESS);
    ok_hex(Byte, 0x61);
    ok_int(SimSleptMs, 3);

    SimAceWrongEcho = TRUE;
    ok_hex(AppleAcePdRead(&Pd, 0x1a, &Byte, 1), STATUS_DEVICE_PROTOCOL_ERROR);
    SimAceWrongEcho = FALSE;

    State.DataStatus = 0;
    ok_hex(AppleAcePdRead(&Pd, 0x5f, &State.DataStatus, 4), STATUS_SUCCESS);
    ok_hex(State.DataStatus, 0x11);
    ok_hex(AppleAcePdRead(&Pd, 0x5f, &State.DataStatus, 0), STATUS_INVALID_PARAMETER);
    memset(SimAceRegs[0x1a], 0x5a, SIM_ACE_WINDOW_SIZE);
    SimSpmiCommandCount = 0;
    ok_hex(AppleAcePdRead(&Pd, 0x1a, Window, SIM_ACE_WINDOW_SIZE), STATUS_SUCCESS);
    ok_int(Window[0], 0x5a);
    ok_int(Window[SIM_ACE_WINDOW_SIZE - 1], 0x5a);
    ok_int(SimSpmiCommandCount, 10);
    ok_hex(SimSpmiCommands[9], 0x00588c3f);
    ok_hex(AppleAcePdRead(&Pd, 0x1a, Window, SIM_ACE_WINDOW_SIZE + 1), STATUS_INVALID_PARAMETER);

    SimAceSet(0x20, PowerS5, sizeof(PowerS5));
    SimAceRejectCommand = TRUE;
    ok_hex(AppleAcePdInitialize(&Pd, NULL), STATUS_NOT_SUPPORTED);
    ok_int(SimAceRegs[0x20][0], 5);
    SimAceRejectCommand = FALSE;
    memcpy(SimAceRegs[0x08], "SSPS", 4);
    ok_hex(AppleAcePdCommand(&Pd, "SSPS", NULL, 0), STATUS_DEVICE_BUSY);
    AppleSpmiClose(&Spmi);
}

static
VOID
TestMcaRx(VOID)
{
    FDT_DRIVER_TREE Tree;
    APPLE_MCA Mca;
    ULONG Node;
    NTSTATUS Status;

    ResetAudioSimulation(&Tree, 0);
    Node = FindNode(&Tree, "apple,t8103-mca");
    Status = AppleMcaOpen(&Tree, Node, &Mca);
    ok_hex(Status, STATUS_SUCCESS);

    MCA(0x408) = 0x00004400;
    Status = AppleMcaConfigureRx(&Mca, 0, 2, 32, 1, 16, 1UL << 2);
    ok_hex(Status, STATUS_SUCCESS);
    ok_hex(MCA(0x408), 0x0001b101);
    ok_hex(MCA(0x40c), 1);
    ok_hex(MCA(0x410), 0xffffffff);
    ok_hex(MCA(0x414), 0xfffffffe);
    ok_hex(MCA(0x404), 4);
    ok_hex(MCASWITCH(0x4000), 0x00105050);
    ok_hex(MCASWITCH(0), 0);
    ok_hex(MCA(0x108), 31);
    ok_hex(MCA(0x10c), 31);
    ok_hex(MCA(0x004), 0x100);
    ok_hex(MCA(0x304), 0);

    Status = AppleMcaConfigureRx(&Mca, 1, 2, 32, 2, 32, 1UL << 3);
    ok_hex(Status, STATUS_SUCCESS);
    ok_hex(MCA(0x4000 + 0x408), 0x0002b101);
    ok_hex(MCA(0x4000 + 0x414), 0xfffffffc);
    ok_hex(MCA(0x4000 + 0x404), 8);
    ok_hex(MCASWITCH(0x8000 + 0x4000), 0x00204040);
    ok_hex(AppleMcaConfigureRx(&Mca, 0, 2, 18, 1, 16, 4), STATUS_NOT_SUPPORTED);
    ok_hex(AppleMcaConfigureRx(&Mca, 0, 2, 32, 1, 16, 0), STATUS_INVALID_PARAMETER);
    ok_hex(AppleMcaConfigureRx(&Mca, 4, 2, 32, 1, 16, 4), STATUS_INVALID_PARAMETER);

    SimWriteCount = 0;
    Status = AppleMcaResetRx(&Mca, 0);
    ok_hex(Status, STATUS_SUCCESS);
    ok_int(SimWriteCount, 5);
    ok_hex(SimWrites[0].Value, 0x0000b101);
    ok_hex(SimWrites[1].Value, 0x0007b101);
    ok_hex(SimWrites[4].Value, 0x0001b101);
    ok_hex(MCA(0x400), 0);
    AppleMcaEnableRx(&Mca, 0, TRUE);
    ok_hex(MCA(0x400), 1);
    ok_hex(MCA(0x300), 0);
    AppleMcaEnableRx(&Mca, 0, FALSE);
    ok_hex(MCA(0x400), 0);

    SimMcaStuck = TRUE;
    ok_hex(AppleMcaResetRx(&Mca, 0), STATUS_DEVICE_NOT_READY);
    SimMcaStuck = FALSE;
    AppleMcaClose(&Mca);
}

static
VOID
TestMca(VOID)
{
    FDT_DRIVER_TREE Tree;
    APPLE_MCA Mca;
    ULONG Node;
    NTSTATUS Status;

    ResetAudioSimulation(&Tree, 0);
    Node = FindNode(&Tree, "apple,t8103-mca");
    Status = AppleMcaOpen(&Tree, Node, &Mca);
    ok_hex(Status, STATUS_SUCCESS);
    ok_int(Mca.ClusterCount, 4);

    MCA(0x304) = 0x00000400;
    Status = AppleMcaConfigureTx(&Mca, 0, 2, 32, 2, 16);
    ok_hex(Status, STATUS_SUCCESS);
    ok_hex(MCA(0x304), 0x00017101);
    ok_hex(MCA(0x308), 1);
    ok_hex(MCA(0x30c), 0xffffffff);
    ok_hex(MCA(0x310), 0xfffffffc);
    ok_hex(MCA(0x314), 0xffffffff);
    ok_hex(MCA(0x318), 0xfffffffc);
    ok_hex(MCASWITCH(0), 0x00205050);
    ok_hex(MCA(0x108), 31);
    ok_hex(MCA(0x10c), 31);
    ok_hex(MCA(0x004), 0x100);

    Status = AppleMcaConfigureTx(&Mca, 1, 8, 32, 6, 32);
    ok_hex(Status, STATUS_SUCCESS);
    ok_hex(MCA(0x4000 + 0x304), 0x00027107);
    ok_hex(MCA(0x4000 + 0x310), 0xffffffc0);
    ok_hex(MCA(0x4000 + 0x318), 0xffffff00);
    ok_hex(MCASWITCH(0x8000), 0x00404040);
    ok_hex(MCA(0x4000 + 0x108), 127);
    ok_hex(AppleMcaConfigureTx(&Mca, 0, 2, 18, 2, 16), STATUS_NOT_SUPPORTED);
    ok_hex(AppleMcaConfigureTx(&Mca, 4, 2, 32, 2, 16), STATUS_INVALID_PARAMETER);

    Status = AppleMcaRoutePort(&Mca, 2, 0, TRUE);
    ok_hex(Status, STATUS_SUCCESS);
    ok_hex(MCA(0x8000 + 0x608), 1);
    ok_hex(MCA(0x8000 + 0x604), 0x100);
    ok_hex(MCA(0x8000 + 0x600), 0xe);
    AppleMcaEnableClocks(&Mca, 0, TRUE);
    ok_hex(MCA(0x104), 1);
    ok_hex(MCA(0x100), 1);
    ok_hex(MCA(0x000), 1);

    SimWriteCount = 0;
    Status = AppleMcaResetTx(&Mca, 0);
    ok_hex(Status, STATUS_SUCCESS);
    ok_int(SimWriteCount, 5);
    ok_hex(SimWrites[0].Value, 0x00007101);
    ok_hex(SimWrites[1].Value, 0x00077101);
    ok_hex(SimWrites[2].Value, 0x00000002);
    ok_hex(SimWrites[3].Value, 0x00007101);
    ok_hex(SimWrites[4].Value, 0x00017101);
    ok(SimStalledMicroseconds >= 150, "The serial port settled for %lu us\n", SimStalledMicroseconds);
    AppleMcaEnableTx(&Mca, 0, TRUE);
    ok_hex(MCA(0x300), 1);
    AppleMcaEnableTx(&Mca, 0, FALSE);
    ok_hex(MCA(0x300), 0);

    SimMcaStuck = TRUE;
    ok_hex(AppleMcaResetTx(&Mca, 0), STATUS_DEVICE_NOT_READY);
    SimMcaStuck = FALSE;

    AppleMcaEnableClocks(&Mca, 0, FALSE);
    ok_hex(MCA(0x100), 0);
    ok_hex(MCA(0x000), 0);
    AppleMcaRoutePort(&Mca, 2, 0, FALSE);
    ok_hex(MCA(0x8000 + 0x600), 0);
    ok_hex(MCA(0x8000 + 0x608), 0);
    ok_hex(MCA(0x8000 + 0x604), 0);
    AppleMcaClose(&Mca);
    ok_int(SimMapCount, 0);
}

START_TEST(AppleSoc)
{
    TestPmgr();
    TestDart();
    TestDartLowWindow();
    TestDartLowWindowM3Memory();
    TestDartAdopt();
    TestPmpPower();
    TestUat();
    TestUatAdopt();
    TestSpmi();
    TestAtcphy();
    TestPcie();
    TestGpio();
    TestI2c();
    TestNco();
    TestAdmac();
    TestMca();
    TestMcaRx();
    TestAcePd();
}
