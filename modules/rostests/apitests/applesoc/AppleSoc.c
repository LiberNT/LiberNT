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
#include <libernt/fdtlib.h>
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
#define SIM_MAILBOX_BASE    0x292408000ULL
#define SIM_MAILBOX_INDEX   16
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
static ULONG SimMailboxData[SIM_REGION_SIZE / sizeof(ULONG)];

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
    { SIM_MAILBOX_BASE, sizeof(SimMailboxData), SimMailboxData },
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


/* A coprocessor on the other side of the mailbox: it answers the management protocol the way the
 * real ones do, so the whole boot can be driven without the machine. */
#define SIM_MBOX_A2I_CONTROL    0x110
#define SIM_MBOX_A2I_SEND0      0x800
#define SIM_MBOX_A2I_SEND1      0x808
#define SIM_MBOX_I2A_CONTROL    0x114
#define SIM_MBOX_I2A_RECV0      0x830
#define SIM_MBOX_I2A_RECV1      0x838
#define SIM_MBOX_EMPTY          0x00020000
#define SIM_MBOX_QUEUE          32

typedef struct _SIM_MBOX_MESSAGE
{
    ULONG64 Message;
    UCHAR Endpoint;
} SIM_MBOX_MESSAGE;

static SIM_MBOX_MESSAGE SimMboxOut[SIM_MBOX_QUEUE];
static ULONG SimMboxOutHead, SimMboxOutTail;
static SIM_MBOX_MESSAGE SimMboxSeen[SIM_MBOX_QUEUE];
static ULONG SimMboxSeenCount;
static ULONG SimMboxControlReads;
static BOOLEAN SimMboxSilent;

static
ULONG64 *
SimMboxWord(
    _In_ ULONG Offset)
{
    return (ULONG64 *)((PUCHAR)SimMailboxData + Offset);
}

static
VOID
SimMboxPush(
    _In_ UCHAR Endpoint,
    _In_ ULONG64 Message)
{
    if (SimMboxOutTail - SimMboxOutHead >= SIM_MBOX_QUEUE)
        return;
    SimMboxOut[SimMboxOutTail % SIM_MBOX_QUEUE].Endpoint = Endpoint;
    SimMboxOut[SimMboxOutTail % SIM_MBOX_QUEUE].Message = Message;
    ++SimMboxOutTail;
}

static
VOID
SimMboxReset(
    _In_ ULONG HelloMin,
    _In_ ULONG HelloMax)
{
    RtlZeroMemory(SimMailboxData, sizeof(SimMailboxData));
    SimMboxOutHead = 0;
    SimMboxOutTail = 0;
    SimMboxSeenCount = 0;
    SimMboxControlReads = 0;
    SimMboxSilent = FALSE;
    if (HelloMax)
        SimMboxPush(0, (0x01ULL << 52) | ((ULONG64)HelloMax << 16) | HelloMin);
}


/* Takes the message the host just wrote and answers it as the coprocessor would. */
static
VOID
SimMboxConsume(VOID)
{
    ULONG64 Message = *SimMboxWord(SIM_MBOX_A2I_SEND0);
    UCHAR Endpoint = (UCHAR)*SimMboxWord(SIM_MBOX_A2I_SEND1);
    ULONG Type;

    if (!Message)
        return;
    *SimMboxWord(SIM_MBOX_A2I_SEND0) = 0;
    if (SimMboxSeenCount < SIM_MBOX_QUEUE)
    {
        SimMboxSeen[SimMboxSeenCount].Endpoint = Endpoint;
        SimMboxSeen[SimMboxSeenCount++].Message = Message;
    }
    if (SimMboxSilent)
        return;
    Type = (ULONG)((Message >> 52) & 0xff);
    if (Endpoint != 0)
        return;
    if (Type == 0x02)
        SimMboxPush(0, (0x08ULL << 52) | (1ULL << 51) | 0x1f);
    else if (Type == 0x08)
        SimMboxPush(0, (0x07ULL << 52) | 0x20);
    else if (Type == 0x0b)
        SimMboxPush(0, (0x0bULL << 52) | (Message & 0xffff));
}

/* The coprocessor only sees a message once the host next looks at the mailbox. */
static
VOID
SimMboxFlush(VOID)
{
    SimMboxConsume();
}

static
ULONG
SimMboxRead(
    _In_ ULONG Offset)
{
    SimMboxConsume();
    if (Offset == SIM_MBOX_A2I_CONTROL)
        return 0;
    if (Offset != SIM_MBOX_I2A_CONTROL)
        return SimMailboxData[Offset / sizeof(ULONG)];
    /* The boot reads this register once to log it, before it reads anything out of the mailbox. */
    if (SimMboxControlReads++ == 0)
        return SIM_MBOX_EMPTY;
    if (SimMboxOutHead == SimMboxOutTail)
        return SIM_MBOX_EMPTY;
    *SimMboxWord(SIM_MBOX_I2A_RECV0) = SimMboxOut[SimMboxOutHead % SIM_MBOX_QUEUE].Message;
    *SimMboxWord(SIM_MBOX_I2A_RECV1) = SimMboxOut[SimMboxOutHead % SIM_MBOX_QUEUE].Endpoint;
    ++SimMboxOutHead;
    return 0;
}

static
BOOLEAN
SimMboxSent(
    _In_ UCHAR Endpoint,
    _In_ ULONG Type,
    _Out_opt_ PULONG64 Message)
{
    ULONG Index;

    if (Message)
        *Message = 0;
    for (Index = 0; Index < SimMboxSeenCount; ++Index)
    {
        if (SimMboxSeen[Index].Endpoint != Endpoint ||
            (ULONG)((SimMboxSeen[Index].Message >> 52) & 0xff) != Type)
        {
            continue;
        }
        if (Message)
            *Message = SimMboxSeen[Index].Message;
        return TRUE;
    }
    return FALSE;
}

static
ULONG
SimRead(volatile ULONG *Address)
{
    ULONG Offset;
    SIM_REGION *Region = SimFromVirtual(Address, &Offset);

    if (!Region)
        return 0xdeadbeef;
    if (Region == &SimRegions[SIM_MAILBOX_INDEX])
        return SimMboxRead(Offset);
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
#define READ_REGISTER_UCHAR(Address) (*(Address))
#define WRITE_REGISTER_ULONG(Address, Value) SimWrite(Address, Value)
#define KeStallExecutionProcessor(Microseconds) (SimStalledMicroseconds += (Microseconds))
#define KeMemoryBarrier() ((void)0)
#define APC_LEVEL 1
#define KeGetCurrentIrql() 0
#define KeDelayExecutionThread(Mode, Alertable, Interval) \
    (SimSleptMs += (ULONG)(-(Interval)->QuadPart / 10000), STATUS_SUCCESS)
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

static ULONG SimInvalidatedAsid;
static ULONG SimInvalidations;

static
VOID
UatInvalidateAsid(ULONG Asid)
{
    SimInvalidatedAsid = Asid;
    ++SimInvalidations;
}

#include "../../../../sdk/lib/drivers/fdtdrv/fdtdrv.c"
#include "../../../../sdk/lib/drivers/applertkit/pmgr.c"
#include "../../../../sdk/lib/drivers/applertkit/dart.c"
#include "../../../../sdk/lib/drivers/applertkit/uat.c"
#include "../../../../sdk/lib/drivers/applertkit/adt.c"
#include "../../../../sdk/lib/drivers/applertkit/agxinit.c"
#include "../../../../sdk/lib/drivers/applertkit/rtkit.c"
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
    ok(FdtOpen(Builder.Blob, Builder.BlobSize, &Tree->Fdt), "The test tree did not open\n");
}

static
ULONG
FindNode(PFDT_DRIVER_TREE Tree, const CHAR *Compatible)
{
    ULONG Node = FdtRootNode(&Tree->Fdt), Length;
    const VOID *Property;

    while (Node != FDT_NO_NODE)
    {
        if (FdtReadToken(&Tree->Fdt, Node) == FDT_BEGIN_NODE)
        {
            Property = FdtGetProperty(&Tree->Fdt, Node, "compatible", &Length);
            if (FdtStringListContains(Property, Length, Compatible))
                return Node;
        }
        Node = FdtSkipToken(&Tree->Fdt, Node);
    }
    return FDT_NO_NODE;
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
    APPLE_UAT Client;
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

    ok_hex(AppleUatCreate(&Client, FALSE, "test"), STATUS_SUCCESS);
    SimInvalidations = 0;
    ok_hex(AppleUatAdoptedBindSlot(&Adopted, 5, Client.TopPhysical), STATUS_SUCCESS);
    ok(Contexts[10] == (Client.TopPhysical | 1 | (5ULL << 48)), "The lower half of slot 5 is %I64x\n", Contexts[10]);
    ok(Contexts[11] == ((RootPhysical & APPLE_UAT_OUTPUT_MASK) | 1 | (5ULL << 48)),
       "The upper half of slot 5 is %I64x\n", Contexts[11]);
    ok_int(SimInvalidatedAsid, 5);
    ok_int(SimInvalidations, 1);
    ok(Contexts[0] == ((Adopted.Lower.TopPhysical & APPLE_UAT_OUTPUT_MASK) | 1), "Context 0 became %I64x\n",
       Contexts[0]);
    ok(Contexts[12] == 0 && Contexts[13] == 0, "Slot 6 became %I64x %I64x\n", Contexts[12], Contexts[13]);
    ok_int(*(PULONG)((PUCHAR)Handoff + 0x10) & 0xff, 0);
    ok_hex(AppleUatAdoptedUnbindSlot(&Adopted, 5), STATUS_SUCCESS);
    ok(Contexts[10] == 0 && Contexts[11] == 0, "Slot 5 still holds %I64x %I64x\n", Contexts[10], Contexts[11]);
    ok_int(SimInvalidations, 2);
    ok_hex(AppleUatAdoptedBindSlot(&Adopted, 0, Client.TopPhysical), STATUS_INVALID_PARAMETER);
    ok_hex(AppleUatAdoptedBindSlot(&Adopted, APPLE_UAT_CONTEXTS, Client.TopPhysical), STATUS_INVALID_PARAMETER);
    ok_hex(AppleUatAdoptedBindSlot(&Adopted, 5, Client.TopPhysical + 0x1000), STATUS_INVALID_PARAMETER);
    ok_hex(AppleUatAdoptedBindSlot(&Adopted, 5, 1ULL << 42), STATUS_INVALID_PARAMETER);
    ok_hex(AppleUatAdoptedUnbindSlot(&Adopted, 0), STATUS_INVALID_PARAMETER);
    ok_int(SimInvalidations, 2);
    AppleUatDestroy(&Client);

    AppleUatAdoptedRelease(&Adopted);
    ok(Adopted.Handoff == NULL && Adopted.Contexts == NULL && Adopted.KernelRoot == NULL &&
       Adopted.HostLevel == NULL, "The firmware pages are still mapped\n");
    ok(Adopted.Lower.Top == NULL && Adopted.Host.Top == NULL, "The host tables survived the release\n");
}

static UCHAR AdtImage[4096];
static ULONG AdtUsed;

static
VOID
AdtPut(
    _In_ const VOID *Data,
    _In_ ULONG Length)
{
    if (AdtUsed + Length <= sizeof(AdtImage))
        memcpy(AdtImage + AdtUsed, Data, Length);
    AdtUsed += Length;
}

static
VOID
AdtPutU32(
    _In_ ULONG Value)
{
    AdtPut(&Value, sizeof(Value));
}

static
VOID
AdtPutProperty(
    _In_z_ PCSTR Name,
    _In_ const VOID *Value,
    _In_ ULONG Length)
{
    CHAR Padded[APPLE_ADT_NAME_LENGTH] = { 0 };
    ULONG Align = ((Length + 3) & ~3UL) - Length;
    static const UCHAR Zero[4] = { 0 };

    strncpy(Padded, Name, sizeof(Padded) - 1);
    AdtPut(Padded, sizeof(Padded));
    AdtPutU32(Length);
    AdtPut(Value, Length);
    if (Align)
        AdtPut(Zero, Align);
}

static
VOID
AdtPutName(
    _In_z_ PCSTR Name)
{
    AdtPutProperty("name", Name, (ULONG)strlen(Name) + 1);
}

/* The exported tree of this machine, cut down to what the graphics driver reads from it. */
static
VOID
AdtBuild(VOID)
{
    const ULONG64 HandoffBase = 0x108fff70000ULL, HandoffSize = 0x4000;
    const ULONG64 TtbsBase = 0x108fffb8000ULL, TtbsSize = 0x4000;
    const ULONG Perf[4] = { 0x14257880, 0x280, 0x24d5ee80, 0x2a8 };

    AdtUsed = 0;
    /* root: 1 property, 1 child */
    AdtPutU32(1);
    AdtPutU32(1);
    AdtPutName("device-tree");
    /* arm-io: 1 property, 2 children */
    AdtPutU32(1);
    AdtPutU32(2);
    AdtPutName("arm-io");
    /* sgx: name, compatible, four region words, perf states; no children */
    AdtPutU32(7);
    AdtPutU32(0);
    AdtPutName("sgx");
    AdtPutProperty("compatible", "gpu,t6030", 10);
    AdtPutProperty("gfx-handoff-base", &HandoffBase, sizeof(HandoffBase));
    AdtPutProperty("gfx-handoff-size", &HandoffSize, sizeof(HandoffSize));
    AdtPutProperty("gpu-region-base", &TtbsBase, sizeof(TtbsBase));
    AdtPutProperty("gpu-region-size", &TtbsSize, sizeof(TtbsSize));
    AdtPutProperty("perf-states", Perf, sizeof(Perf));
    /* gfx-asc with its own child */
    AdtPutU32(2);
    AdtPutU32(1);
    AdtPutName("gfx-asc");
    AdtPutProperty("segment-names", "__TEXT;__DATA", 14);
    AdtPutU32(2);
    AdtPutU32(0);
    AdtPutName("iop-gfx-nub");
    AdtPutProperty("firmware-name", "GFX", 4);
}

static
VOID
TestAdt(VOID)
{
    APPLE_ADT Adt;
    ULONG Node, Length, Value32;
    ULONG64 Value64;
    const VOID *Property;

    AdtBuild();
    ok(AdtUsed <= sizeof(AdtImage), "The tree needs %lu bytes\n", AdtUsed);

    /* A tree is only accepted whole: a short buffer must not be walked. */
    ok(!AppleAdtOpen(&Adt, AdtImage, 4), "A tree shorter than a node header was accepted\n");
    ok(!AppleAdtOpen(&Adt, AdtImage, AdtUsed - 4), "A truncated tree was accepted\n");
    ok(!AppleAdtOpen(&Adt, NULL, AdtUsed), "A tree with no memory was accepted\n");
    ok(AppleAdtOpen(&Adt, AdtImage, AdtUsed), "The tree was not accepted\n");

    ok_int(AppleAdtRootNode(&Adt), 0);
    ok(AppleAdtNodeName(&Adt, 0) && !strcmp(AppleAdtNodeName(&Adt, 0), "device-tree"),
       "The root is named %s\n", AppleAdtNodeName(&Adt, 0));

    /* The paths the graphics driver uses. */
    Node = AppleAdtPath(&Adt, "/arm-io/sgx");
    ok(Node != APPLE_ADT_NO_NODE, "The graphics node was not found\n");
    ok(AppleAdtIsCompatible(&Adt, Node, "gpu,t6030"), "The graphics node is not the expected part\n");
    ok(!AppleAdtIsCompatible(&Adt, Node, "gpu,t6031"), "The graphics node matched another part\n");
    ok(!AppleAdtIsCompatible(&Adt, Node, "gpu"), "A name prefix matched the compatible list\n");

    ok(AppleAdtReadU64(&Adt, Node, "gfx-handoff-base", &Value64) && Value64 == 0x108fff70000ULL,
       "The handoff page is at %I64x\n", Value64);
    ok(AppleAdtReadU64(&Adt, Node, "gpu-region-base", &Value64) && Value64 == 0x108fffb8000ULL,
       "The address table is at %I64x\n", Value64);
    ok(AppleAdtReadU64(&Adt, Node, "gpu-region-size", &Value64) && Value64 == 0x4000,
       "The address table is %I64x bytes\n", Value64);
    ok(!AppleAdtReadU64(&Adt, Node, "gfx-shared-region-base", &Value64),
       "A property the tree does not carry was read\n");
    ok(!AppleAdtReadU32(&Adt, Node, "name-of-nothing", &Value32), "A missing property was read\n");

    /* A property shorter than the type asked for is refused rather than read past. */
    Property = AppleAdtGetProperty(&Adt, Node, "compatible", &Length);
    ok(Property != NULL && Length == 10, "The compatible list is %lu bytes\n", Length);
    ok(!AppleAdtReadU64(&Adt, Node, "compatible", &Value64), "A 10 byte property was read as 8 bytes\n");

    /* A property of several values is read whole, never through a typed read of the first one. */
    Property = AppleAdtGetProperty(&Adt, Node, "perf-states", &Length);
    ok(Property != NULL && Length == 16, "The performance states are %lu bytes\n", Length);
    ok(!AppleAdtReadU32(&Adt, Node, "perf-states", &Value32), "A 16 byte property was read as 4 bytes\n");
    if (Property)
    {
        memcpy(&Value32, Property, sizeof(Value32));
        ok(Value32 == 0x14257880, "The first performance state is %08lx\n", Value32);
    }

    /* Children and siblings, including a node that has children of its own. */
    Node = AppleAdtPath(&Adt, "/arm-io/gfx-asc");
    ok(Node != APPLE_ADT_NO_NODE, "The coprocessor node was not found\n");
    Property = AppleAdtGetProperty(&Adt, Node, "segment-names", &Length);
    ok(Property != NULL && Length == 14 && !strcmp(Property, "__TEXT;__DATA"), "The segments are %lu bytes\n",
       Length);
    Node = AppleAdtPath(&Adt, "/arm-io/gfx-asc/iop-gfx-nub");
    ok(Node != APPLE_ADT_NO_NODE, "The firmware node was not found\n");
    Property = AppleAdtGetProperty(&Adt, Node, "firmware-name", &Length);
    ok(Property != NULL && !strcmp(Property, "GFX"), "The firmware is named %s\n", (PCSTR)Property);

    ok_hex(AppleAdtPath(&Adt, "/arm-io/missing"), APPLE_ADT_NO_NODE);
    ok_hex(AppleAdtPath(&Adt, "arm-io"), APPLE_ADT_NO_NODE);
    ok_hex(AppleAdtPath(&Adt, "/arm-io/sgx/deeper"), APPLE_ADT_NO_NODE);
    ok_hex(AppleAdtPath(&Adt, "/"), 0);

    /* The two children of the bus follow one another and the walk then ends. */
    Node = AppleAdtFirstChild(&Adt, AppleAdtPath(&Adt, "/arm-io"));
    ok(Node != APPLE_ADT_NO_NODE && !strcmp(AppleAdtNodeName(&Adt, Node), "sgx"), "The first child is %s\n",
       Node != APPLE_ADT_NO_NODE ? AppleAdtNodeName(&Adt, Node) : "absent");
    Node = AppleAdtNextSibling(&Adt, Node);
    ok(Node != APPLE_ADT_NO_NODE && !strcmp(AppleAdtNodeName(&Adt, Node), "gfx-asc"), "The second child is %s\n",
       Node != APPLE_ADT_NO_NODE ? AppleAdtNodeName(&Adt, Node) : "absent");
    ok_hex(AppleAdtNextSibling(&Adt, Node), APPLE_ADT_NO_NODE);
    ok_hex(AppleAdtFirstChild(&Adt, AppleAdtPath(&Adt, "/arm-io/sgx")), APPLE_ADT_NO_NODE);
}

#define ADT_BREAK_ALIGN         0x01
#define ADT_BREAK_OVERLAP       0x02
#define ADT_BREAK_MISSING       0x04
#define ADT_BREAK_TEXT_VA       0x08
#define ADT_BREAK_STATE_SIZE    0x10
#define ADT_BREAK_SRAM_COUNT    0x20
#define ADT_BREAK_PART          0x40

/* The graphics half of this machine's exported tree, with one defect at a time. */
static
VOID
AdtBuildGraphics(
    _In_ ULONG Break)
{
    ULONG64 Base[4] = { 0x108fffb8000ULL, 0x108fff78000ULL, 0x108fff70000ULL, 0x108fff74000ULL };
    ULONG64 Size[4] = { 0x4000, 0x40000, 0x4000, 0x4000 };
    static const CHAR *const BaseNames[4] = { "gpu-region-base", "gfx-shared-region-base", "gfx-handoff-base",
                                              "gfx-shared-l2-region-base" };
    static const CHAR *const SizeNames[4] = { "gpu-region-size", "gfx-shared-region-size", "gfx-handoff-size",
                                              "gfx-shared-l2-region-size" };
    APPLE_AGX_SEGMENT Segment[2];
    ULONG States[8] = { 338712192, 0x280, 618434688, 0x2a8, 795836672, 0x2b7, 831528960, 0x2df };
    ULONG Sram[8] = { 338712192, 0x32a, 618434688, 0x32a, 795836672, 0x32a, 831528960, 0x32a };
    ULONG Properties = (Break & ADT_BREAK_MISSING) ? 6 : 8;
    ULONG StateLength = (Break & ADT_BREAK_STATE_SIZE) ? sizeof(States) - 4 : sizeof(States);
    ULONG SramLength = (Break & ADT_BREAK_SRAM_COUNT) ? sizeof(Sram) - 8 : sizeof(Sram);
    ULONG Index;

    if (Break & ADT_BREAK_ALIGN)
        Base[2] += 0x1000;
    if (Break & ADT_BREAK_OVERLAP)
        Size[2] = 0x10000;
    RtlZeroMemory(Segment, sizeof(Segment));
    Segment[0].Physical = 0x100009d8000ULL;
    Segment[0].Virtual = (Break & ADT_BREAK_TEXT_VA) ? 0xfffffc0000004000ULL : APPLE_AGX_FIRMWARE_UPPER;
    Segment[0].Remap = Segment[0].Physical;
    Segment[0].Size = 0x5c000;
    Segment[0].Flags = 1;
    Segment[1].Physical = 0x100018e8000ULL;
    Segment[1].Virtual = APPLE_AGX_FIRMWARE_UPPER + 0x5c000;
    Segment[1].Remap = Segment[1].Physical;
    Segment[1].Size = 0x114000;

    AdtUsed = 0;
    AdtPutU32(1);
    AdtPutU32(1);
    AdtPutName("device-tree");
    AdtPutU32(1);
    AdtPutU32(2);
    AdtPutName("arm-io");
    AdtPutU32(4 + Properties);
    AdtPutU32(0);
    AdtPutName("sgx");
    AdtPutProperty("compatible", (Break & ADT_BREAK_PART) ? "gpu,t6031" : "gpu,t6030", 10);
    for (Index = 0; Index < Properties / 2; ++Index)
    {
        AdtPutProperty(BaseNames[Index], &Base[Index], sizeof(Base[Index]));
        AdtPutProperty(SizeNames[Index], &Size[Index], sizeof(Size[Index]));
    }
    AdtPutProperty("perf-states", States, StateLength);
    AdtPutProperty("perf-states-sram", Sram, SramLength);
    AdtPutU32(1);
    AdtPutU32(1);
    AdtPutName("gfx-asc");
    AdtPutU32(2);
    AdtPutU32(0);
    AdtPutName("iop-gfx-nub");
    AdtPutProperty("segment-ranges", Segment, sizeof(Segment));
}

static
VOID
TestAgxDescribe(VOID)
{
    APPLE_AGX_DESCRIPTION Description;
    APPLE_ADT Adt;

    /* The tree of this machine, read whole. */
    AdtBuildGraphics(0);
    ok(AppleAdtOpen(&Adt, AdtImage, AdtUsed), "The graphics tree was not accepted\n");
    ok_hex(AppleAgxDescribe(&Adt, "gpu,t6030", 0x4000, &Description), STATUS_SUCCESS);
    ok(Description.RegionBase[APPLE_AGX_REGION_TABLE] == 0x108fffb8000ULL, "The address table is at %I64x\n",
       Description.RegionBase[APPLE_AGX_REGION_TABLE]);
    ok(Description.RegionBase[APPLE_AGX_REGION_ROOT] == 0x108fff78000ULL, "The root is at %I64x\n",
       Description.RegionBase[APPLE_AGX_REGION_ROOT]);
    ok(Description.RegionBase[APPLE_AGX_REGION_HANDOFF] == 0x108fff70000ULL, "The handoff page is at %I64x\n",
       Description.RegionBase[APPLE_AGX_REGION_HANDOFF]);
    ok(Description.RegionBase[APPLE_AGX_REGION_WINDOW] == 0x108fff74000ULL, "The window level is at %I64x\n",
       Description.RegionBase[APPLE_AGX_REGION_WINDOW]);
    ok(Description.RegionSize[APPLE_AGX_REGION_ROOT] == 0x40000, "The root region is %I64x bytes\n",
       Description.RegionSize[APPLE_AGX_REGION_ROOT]);
    ok(Description.SegmentsKnown && Description.SegmentsPlaced, "The segments are %s and %s\n",
       Description.SegmentsKnown ? "known" : "unknown", Description.SegmentsPlaced ? "placed" : "misplaced");
    ok(Description.Text.Physical == 0x100009d8000ULL && Description.Text.Size == 0x5c000,
       "The text is at %I64x+%lx\n", Description.Text.Physical, Description.Text.Size);
    ok(Description.Data.Physical == 0x100018e8000ULL && Description.Data.Size == 0x114000,
       "The data is at %I64x+%lx\n", Description.Data.Physical, Description.Data.Size);
    ok_int(Description.States, 4);
    ok(Description.State[0].Frequency == 338712192 && Description.State[0].Millivolts == 0x280,
       "The first state is %lu Hz at %lu mV\n", Description.State[0].Frequency, Description.State[0].Millivolts);
    ok(Description.State[3].Frequency == 831528960 && Description.State[3].Millivolts == 0x2df,
       "The last state is %lu Hz at %lu mV\n", Description.State[3].Frequency, Description.State[3].Millivolts);
    ok_int(Description.SramState[0].Millivolts, 0x32a);

    /* Every defect is refused, and nothing is reported as usable. */
    AdtBuildGraphics(ADT_BREAK_ALIGN);
    ok(AppleAdtOpen(&Adt, AdtImage, AdtUsed), "The tree was not accepted\n");
    ok_hex(AppleAgxDescribe(&Adt, "gpu,t6030", 0x4000, &Description), STATUS_INVALID_ADDRESS);
    ok_int(Description.States, 0);

    AdtBuildGraphics(ADT_BREAK_OVERLAP);
    ok(AppleAdtOpen(&Adt, AdtImage, AdtUsed), "The tree was not accepted\n");
    ok_hex(AppleAgxDescribe(&Adt, "gpu,t6030", 0x4000, &Description), STATUS_CONFLICTING_ADDRESSES);

    AdtBuildGraphics(ADT_BREAK_MISSING);
    ok(AppleAdtOpen(&Adt, AdtImage, AdtUsed), "The tree was not accepted\n");
    ok_hex(AppleAgxDescribe(&Adt, "gpu,t6030", 0x4000, &Description), STATUS_DEVICE_CONFIGURATION_ERROR);

    AdtBuildGraphics(ADT_BREAK_STATE_SIZE);
    ok(AppleAdtOpen(&Adt, AdtImage, AdtUsed), "The tree was not accepted\n");
    ok_hex(AppleAgxDescribe(&Adt, "gpu,t6030", 0x4000, &Description), STATUS_DEVICE_CONFIGURATION_ERROR);

    AdtBuildGraphics(ADT_BREAK_SRAM_COUNT);
    ok(AppleAdtOpen(&Adt, AdtImage, AdtUsed), "The tree was not accepted\n");
    ok_hex(AppleAgxDescribe(&Adt, "gpu,t6030", 0x4000, &Description), STATUS_DEVICE_CONFIGURATION_ERROR);

    AdtBuildGraphics(ADT_BREAK_PART);
    ok(AppleAdtOpen(&Adt, AdtImage, AdtUsed), "The tree was not accepted\n");
    ok_hex(AppleAgxDescribe(&Adt, "gpu,t6030", 0x4000, &Description), STATUS_DEVICE_CONFIGURATION_ERROR);

    /* A firmware the boot chain placed wrongly is reported, not used. */
    AdtBuildGraphics(ADT_BREAK_TEXT_VA);
    ok(AppleAdtOpen(&Adt, AdtImage, AdtUsed), "The tree was not accepted\n");
    ok_hex(AppleAgxDescribe(&Adt, "gpu,t6030", 0x4000, &Description), STATUS_SUCCESS);
    ok(Description.SegmentsKnown && !Description.SegmentsPlaced, "A misplaced text segment was accepted\n");

    /* A tree with nothing in it, and a page size that is not a power of two. */
    AdtBuildGraphics(0);
    ok(AppleAdtOpen(&Adt, AdtImage, AdtUsed), "The tree was not accepted\n");
    ok_hex(AppleAgxDescribe(&Adt, "gpu,t6030", 0x3000, &Description), STATUS_INVALID_PARAMETER);
    ok_hex(AppleAgxDescribe(&Adt, "gpu,t6030", 0, &Description), STATUS_INVALID_PARAMETER);
    RtlZeroMemory(&Adt, sizeof(Adt));
    ok_hex(AppleAgxDescribe(&Adt, "gpu,t6030", 0x4000, &Description), STATUS_INVALID_PARAMETER);
}

#define RTKIT_DATA_BASE     0x100018e8000ULL
#define RTKIT_DATA_SIZE      0x114000

static ULONG64 RtkitBufferAddress;
static ULONG RtkitBufferSize;
static ULONG RtkitBufferCount;

/* The graphics firmware places its own buffers inside its data segment and asks the host only to
 * map them: an address it did not place there, or none at all, is refused. */
static
BOOLEAN
NTAPI
RtkitTestShmem(
    _In_ PVOID Context,
    _In_ UCHAR Endpoint,
    _In_ ULONG Size,
    _Inout_ PULONG64 Address,
    _Out_ PVOID *Virtual,
    _Out_ PBOOLEAN Preassigned)
{
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(Endpoint);
    *Virtual = NULL;
    *Preassigned = TRUE;
    if (!*Address || !Size || ((*Address | Size) & 0xfff) || *Address < RTKIT_DATA_BASE ||
        Size > RTKIT_DATA_SIZE || *Address - RTKIT_DATA_BASE > RTKIT_DATA_SIZE - Size)
    {
        return FALSE;
    }
    RtkitBufferAddress = *Address;
    RtkitBufferSize = Size;
    ++RtkitBufferCount;
    *Virtual = SimVirtualForPhysical(*Address);
    return TRUE;
}

static
VOID
TestRtkitBoot(VOID)
{
    ULONG64 Message;
    APPLE_RTKIT Rtkit;
    ULONG Endpoint;

    RtkitBufferCount = 0;
    RtkitBufferAddress = 0;
    RtkitBufferSize = 0;

    /* A coprocessor that speaks the protocol boots, and the host answers every step of it. */
    SimMboxReset(11, 12);
    RtlZeroMemory(&Rtkit, sizeof(Rtkit));
    Rtkit.Name = "test";
    Rtkit.Mailbox = (PUCHAR)SimMailboxData;
    Rtkit.ShmemSetup = RtkitTestShmem;
    ok_hex(AppleRtkitBoot(&Rtkit, 2000), STATUS_SUCCESS);
    ok_int(Rtkit.Version, 12);
    ok_hex(Rtkit.Endpoints[0], 0x1f);
    ok(Rtkit.EpmapDone && Rtkit.IopAcked && Rtkit.ApAcked, "The boot left %s%s%s unanswered\n",
       Rtkit.EpmapDone ? "" : "the endpoints ", Rtkit.IopAcked ? "" : "the coprocessor power ",
       Rtkit.ApAcked ? "" : "the host power");
    ok_hex(Rtkit.IopState, 0x20);
    ok_hex(Rtkit.ApState, 0x20);

    ok(SimMboxSent(0, 0x02, &Message) && (Message & 0xffffffff) == ((12 << 16) | 12),
       "The host greeted the coprocessor with %I64x\n", Message);
    ok(SimMboxSent(0, 0x08, &Message), "The host did not acknowledge the endpoint map\n");
    ok(SimMboxSent(0, 0x0b, &Message) && (Message & 0xffff) == 0x20,
       "The host asked for power state %I64x\n", Message & 0xffff);
    /* Every endpoint the coprocessor announced is started, and none that it did not. */
    for (Endpoint = 1; Endpoint <= 4; ++Endpoint)
    {
        ULONG Index;
        BOOLEAN Started = FALSE;

        for (Index = 0; Index < SimMboxSeenCount; ++Index)
        {
            if (SimMboxSeen[Index].Endpoint == 0 && ((SimMboxSeen[Index].Message >> 52) & 0xff) == 0x05 &&
                ((SimMboxSeen[Index].Message >> 32) & 0xff) == Endpoint)
            {
                Started = TRUE;
            }
        }
        ok(Started, "Endpoint %lu was announced but never started\n", Endpoint);
    }

    /* A buffer the firmware placed in its own data segment is taken as it stands, and a buffer
     * the host would have to choose, or one outside that segment, is refused. */
    SimMboxPush(RTKIT_EP_CRASHLOG, (1ULL << 52) | (1ULL << 44) | RTKIT_DATA_BASE);
    ok(AppleRtkitPoll(&Rtkit), "The buffer request was not received\n");
    SimMboxFlush();
    ok_int(RtkitBufferCount, 1);
    ok(RtkitBufferSize == 0x1000, "The coprocessor asked for %lu bytes\n", RtkitBufferSize);
    ok(RtkitBufferAddress == RTKIT_DATA_BASE, "The buffer is at %I64x\n", RtkitBufferAddress);
    ok(!SimMboxSent(RTKIT_EP_CRASHLOG, 1, &Message), "A buffer the firmware placed was answered\n");

    Rtkit.CrashlogSeen = FALSE;
    SimMboxPush(RTKIT_EP_CRASHLOG, (1ULL << 52) | (1ULL << 44));
    ok(AppleRtkitPoll(&Rtkit), "The second request was not received\n");
    ok_int(RtkitBufferCount, 1);

    Rtkit.CrashlogSeen = FALSE;
    SimMboxPush(RTKIT_EP_CRASHLOG, (1ULL << 52) | (1ULL << 44) | (RTKIT_DATA_BASE - 0x1000));
    ok(AppleRtkitPoll(&Rtkit), "The third request was not received\n");
    ok_int(RtkitBufferCount, 1);

    Rtkit.CrashlogSeen = FALSE;
    SimMboxPush(RTKIT_EP_CRASHLOG, (1ULL << 52) | (0xffULL << 44) | (RTKIT_DATA_BASE + RTKIT_DATA_SIZE - 0x1000));
    ok(AppleRtkitPoll(&Rtkit), "The fourth request was not received\n");
    ok_int(RtkitBufferCount, 1);

    /* A coprocessor that says nothing is given up on rather than waited for for ever. */
    SimMboxReset(0, 0);
    RtlZeroMemory(&Rtkit, sizeof(Rtkit));
    Rtkit.Name = "test";
    Rtkit.Mailbox = (PUCHAR)SimMailboxData;
    SimSleptMs = 0;
    ok_hex(AppleRtkitBoot(&Rtkit, 100), STATUS_IO_TIMEOUT);
    ok(SimSleptMs >= 100, "Waited %lu ms for a silent coprocessor\n", SimSleptMs);
    ok(!Rtkit.EpmapDone, "A silent coprocessor announced its endpoints\n");

    /* A newer protocol is negotiated down to the newest one this host speaks. */
    SimMboxReset(11, 30);
    RtlZeroMemory(&Rtkit, sizeof(Rtkit));
    Rtkit.Name = "test";
    Rtkit.Mailbox = (PUCHAR)SimMailboxData;
    ok_hex(AppleRtkitBoot(&Rtkit, 2000), STATUS_SUCCESS);
    ok_int(Rtkit.Version, 12);
    SimMboxFlush();
    ok(SimMboxSent(0, 0x02, &Message) && (Message & 0xffffffff) == ((12 << 16) | 12),
       "The host answered a newer coprocessor with %I64x\n", Message);

    /* A coprocessor that speaks only a protocol this host does not know is refused. */
    SimMboxReset(20, 30);
    RtlZeroMemory(&Rtkit, sizeof(Rtkit));
    Rtkit.Name = "test";
    Rtkit.Mailbox = (PUCHAR)SimMailboxData;
    ok_hex(AppleRtkitBoot(&Rtkit, 100), STATUS_DEVICE_HARDWARE_ERROR);
    ok(Rtkit.Failed, "A protocol this host cannot speak was accepted\n");
    ok_int(Rtkit.Version, 0);

    /* A coprocessor that greets the host and then stops is reported, not waited on for ever. */
    SimMboxReset(11, 12);
    SimMboxSilent = TRUE;
    RtlZeroMemory(&Rtkit, sizeof(Rtkit));
    Rtkit.Name = "test";
    Rtkit.Mailbox = (PUCHAR)SimMailboxData;
    ok_hex(AppleRtkitBoot(&Rtkit, 100), STATUS_IO_TIMEOUT);
    ok_int(Rtkit.Version, 12);
    ok(!Rtkit.EpmapDone, "A coprocessor that stopped announced its endpoints\n");
}

static PVOID AgxInitBuffers[APPLE_AGX_OBJECTS];

static
ULONG64
AgxQuad(
    _In_ ULONG Object,
    _In_ ULONG Offset)
{
    ULONG64 Value = 0;

    RtlCopyMemory(&Value, (PUCHAR)AgxInitBuffers[Object] + Offset, sizeof(Value));
    return Value;
}

static
ULONG
AgxLong(
    _In_ ULONG Object,
    _In_ ULONG Offset)
{
    ULONG Value = 0;

    RtlCopyMemory(&Value, (PUCHAR)AgxInitBuffers[Object] + Offset, sizeof(Value));
    return Value;
}

static
APPLE_AGX_LAYOUT
AgxLayoutOf(
    _In_ ULONG Object)
{
    APPLE_AGX_LAYOUT Layout;

    AppleAgxLayout(Object, &Layout);
    return Layout;
}

static
VOID
TestAgxInitLayout(VOID)
{
    APPLE_UAT_PROTECTION Protection;
    APPLE_AGX_LAYOUT Layout;
    ULONG Index, Pages = 0;

    /* The addresses this firmware build was linked to look for its objects at. */
    ok(AgxLayoutOf(APPLE_AGX_OBJECT_ROOT).Address == 0xfffffc204062ff40ULL, "The root is at %I64x\n",
       AgxLayoutOf(APPLE_AGX_OBJECT_ROOT).Address);
    ok_int(AgxLayoutOf(APPLE_AGX_OBJECT_ROOT).Size, 0xc0);
    ok(AgxLayoutOf(APPLE_AGX_OBJECT_RUNTIME).Address == 0xfffffc20406b7b4dULL, "The runtime is at %I64x\n",
       AgxLayoutOf(APPLE_AGX_OBJECT_RUNTIME).Address);
    ok_int(AgxLayoutOf(APPLE_AGX_OBJECT_RUNTIME).Size, 0x4b3);
    ok_int(AgxLayoutOf(APPLE_AGX_OBJECT_HWDATA).Size, 35332);
    ok_int(AgxLayoutOf(APPLE_AGX_OBJECT_GLOBALS).Size, 15868);
    ok_int(AgxLayoutOf(APPLE_AGX_OBJECT_POWER).Size, 2292);
    ok(AgxLayoutOf(APPLE_AGX_OBJECT_TIMESTAMPS).Size == 0x4000000, "The timestamps are %lx bytes\n",
       AgxLayoutOf(APPLE_AGX_OBJECT_TIMESTAMPS).Size);
    ok(!AppleAgxLayout(APPLE_AGX_OBJECTS, &Layout), "An object past the last one was described\n");

    /* The twelve submit channels follow one another by the stride of this firmware. */
    Layout = AgxLayoutOf(APPLE_AGX_FIXED_OBJECTS);
    ok(Layout.Address == 0xfffffc2040003fd0ULL, "Channel 0 keeps its state at %I64x\n", Layout.Address);
    Layout = AgxLayoutOf(APPLE_AGX_FIXED_OBJECTS + 1);
    ok(Layout.Address == 0xfffffc2000002800ULL, "Channel 0 keeps its ring at %I64x\n", Layout.Address);
    Layout = AgxLayoutOf(APPLE_AGX_FIXED_OBJECTS + 2 * 11);
    ok(Layout.Address == 0xfffffc2040003fd0ULL + 11 * 0x44000ULL, "Channel 11 keeps its state at %I64x\n",
       Layout.Address);
    ok_int(Layout.Size, 0x30);

    /* Who may reach each kind of page. */
    AppleAgxClassProtection(APPLE_AGX_CLASS_FIRMWARE, &Protection);
    ok(Protection.Access == APPLE_UAT_ACCESS_FIRMWARE && Protection.Memory == APPLE_UAT_MEMORY_UNCACHED &&
       !Protection.Read && Protection.Write, "A firmware page is %u/%u/%u/%u\n", Protection.Access,
       Protection.Memory, Protection.Read, Protection.Write);
    AppleAgxClassProtection(APPLE_AGX_CLASS_FIRMWARE_CACHED, &Protection);
    ok(Protection.Access == APPLE_UAT_ACCESS_FIRMWARE && Protection.Memory == APPLE_UAT_MEMORY_CACHED,
       "A cached firmware page is %u/%u\n", Protection.Access, Protection.Memory);
    AppleAgxClassProtection(APPLE_AGX_CLASS_SHARED, &Protection);
    ok(Protection.Access == APPLE_UAT_ACCESS_BOTH && Protection.Memory == APPLE_UAT_MEMORY_UNCACHED &&
       Protection.Read && Protection.Write, "A shared page is %u/%u/%u/%u\n", Protection.Access,
       Protection.Memory, Protection.Read, Protection.Write);

    /* The initial data needs this much memory, and the doorbell carries the root's low bits. */
    for (Index = 0; Index < APPLE_AGX_OBJECTS; ++Index)
    {
        Layout = AgxLayoutOf(Index);
        Pages += AgxPageCount(Layout.Address, Layout.Size);
    }
    ok(Pages > 4096 && Pages < 8192, "The initial data needs %lu pages\n", Pages);
    ok(AppleAgxInitMessage() == (0x0081ULL << 48 | 0xc204062ff40ULL), "The doorbell is %I64x\n",
       AppleAgxInitMessage());
}

static
VOID
TestAgxInitWrite(VOID)
{
    APPLE_UAT_ADOPTED Adopted;
    APPLE_AGX_INIT Init;
    ULONG Index, Level;

    RtlZeroMemory(&Adopted, sizeof(Adopted));
    Adopted.KernelRootPhysical = 0x108fff78000ULL;
    RtlZeroMemory(&Init, sizeof(Init));
    Init.Adopted = &Adopted;
    Init.Owner = "test";
    for (Index = 0; Index < APPLE_AGX_OBJECTS; ++Index)
    {
        ULONG Size = AgxLayoutOf(Index).Size;

        /* The arena is only pointed at, never written, so a stand-in page is enough. */
        if (Index == APPLE_AGX_OBJECT_TIMESTAMPS || Index == APPLE_AGX_OBJECT_LOG_RING)
            Size = 0x4000;
        AgxInitBuffers[Index] = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Size);
        Init.Virtual[Index] = AgxInitBuffers[Index];
    }
    ok(Init.Virtual[APPLE_AGX_OBJECT_ROOT] != NULL, "The objects were not simulated\n");
    if (!Init.Virtual[APPLE_AGX_OBJECT_ROOT])
        return;

    ok_hex(AppleAgxInitWrite(&Init), STATUS_SUCCESS);

    /* The root names the layout it was built for, the shape of the tables and every object. */
    ok(AgxQuad(APPLE_AGX_OBJECT_ROOT, 0) == 0x0c08e21e83800490ULL, "The root magic is %I64x\n",
       AgxQuad(APPLE_AGX_OBJECT_ROOT, 0));
    ok(AgxQuad(APPLE_AGX_OBJECT_ROOT, APPLE_AGX_ROOT_RUNTIME) == 0xfffffc20406b7b4dULL,
       "The root points at the runtime at %I64x\n", AgxQuad(APPLE_AGX_OBJECT_ROOT, APPLE_AGX_ROOT_RUNTIME));
    ok(AgxQuad(APPLE_AGX_OBJECT_ROOT, APPLE_AGX_ROOT_GLOBALS) == 0xfffffc20407cc204ULL,
       "The root points at the globals at %I64x\n", AgxQuad(APPLE_AGX_OBJECT_ROOT, APPLE_AGX_ROOT_GLOBALS));
    ok_int(AgxLong(APPLE_AGX_OBJECT_ROOT, APPLE_AGX_ROOT_ENABLE), 1);
    ok_hex(AgxLong(APPLE_AGX_OBJECT_ROOT, APPLE_AGX_ROOT_GEOMETRY), 0x030e4000);

    /* One descriptor per level: the entry size, the page shift, this level's index shift, its
     * entry count, the page size, then the leaf address mask and the index mask. */
    for (Level = 0; Level < 3; ++Level)
    {
        ULONG At = APPLE_AGX_ROOT_LEVELS + Level * APPLE_AGX_ROOT_LEVEL_STRIDE;
        static const UCHAR Shift[3] = { 36, 25, 14 };
        static const USHORT Entries[3] = { 64, 2048, 2048 };
        const UCHAR *Bytes = (const UCHAR *)AgxInitBuffers[APPLE_AGX_OBJECT_ROOT] + At;

        ok(Bytes[0] == 8 && Bytes[1] == 14 && Bytes[2] == 14 && Bytes[3] == Shift[Level],
           "Level %lu is described as %u/%u/%u/%u\n", Level, Bytes[0], Bytes[1], Bytes[2], Bytes[3]);
        ok((USHORT)(Bytes[4] | (Bytes[5] << 8)) == Entries[Level], "Level %lu has %u entries\n", Level,
           (USHORT)(Bytes[4] | (Bytes[5] << 8)));
        ok((USHORT)(Bytes[6] | (Bytes[7] << 8)) == 0x4000, "Level %lu uses %u byte pages\n", Level,
           (USHORT)(Bytes[6] | (Bytes[7] << 8)));
        ok(AgxQuad(APPLE_AGX_OBJECT_ROOT, At + 8) == 1, "Level %lu carries %I64x\n", Level,
           AgxQuad(APPLE_AGX_OBJECT_ROOT, At + 8));
        ok(AgxQuad(APPLE_AGX_OBJECT_ROOT, At + 0x10) == 0x3ffffffc000ULL, "Level %lu masks %I64x\n", Level,
           AgxQuad(APPLE_AGX_OBJECT_ROOT, At + 0x10));
        ok(AgxQuad(APPLE_AGX_OBJECT_ROOT, At + 0x18) == ((ULONG64)(Entries[Level] - 1) << Shift[Level]),
           "Level %lu indexes %I64x\n", Level, AgxQuad(APPLE_AGX_OBJECT_ROOT, At + 0x18));
    }

    /* The runtime names the hardware description, every submit channel and every receive ring. */
    ok(AgxQuad(APPLE_AGX_OBJECT_RUNTIME, APPLE_AGX_RUNTIME_HWDATA) == 0xfffffc20406fb5fcULL,
       "The runtime points at the description at %I64x\n",
       AgxQuad(APPLE_AGX_OBJECT_RUNTIME, APPLE_AGX_RUNTIME_HWDATA));
    for (Index = 0; Index < APPLE_AGX_SUBMIT_CHANNELS; ++Index)
    {
        ULONG At = APPLE_AGX_RUNTIME_CHANNELS + Index * APPLE_AGX_RUNTIME_CHANNEL_STRIDE;
        ULONG64 State = 0xfffffc2040003fd0ULL + (ULONG64)Index * 0x44000ULL;
        ULONG64 Ring = 0xfffffc2000002800ULL + (ULONG64)Index * 0x44000ULL;

        ok(AgxQuad(APPLE_AGX_OBJECT_RUNTIME, At) == State &&
           AgxQuad(APPLE_AGX_OBJECT_RUNTIME, At + 8) == State + 0x10 &&
           AgxQuad(APPLE_AGX_OBJECT_RUNTIME, At + 16) == State + 0x20 &&
           AgxQuad(APPLE_AGX_OBJECT_RUNTIME, At + 24) == Ring, "Channel %lu is described wrongly\n", Index);
    }
    ok(AgxQuad(APPLE_AGX_OBJECT_RUNTIME, APPLE_AGX_RUNTIME_RX) == 0xfffffc2040377fd0ULL,
       "The event ring state is at %I64x\n", AgxQuad(APPLE_AGX_OBJECT_RUNTIME, APPLE_AGX_RUNTIME_RX));
    ok(AgxQuad(APPLE_AGX_OBJECT_RUNTIME, APPLE_AGX_RUNTIME_RX + 8) == 0xfffffc20403b8800ULL,
       "The event ring is at %I64x\n", AgxQuad(APPLE_AGX_OBJECT_RUNTIME, APPLE_AGX_RUNTIME_RX + 8));
    ok(AgxQuad(APPLE_AGX_OBJECT_RUNTIME, APPLE_AGX_RUNTIME_RX + 3 * APPLE_AGX_RUNTIME_RX_STRIDE + 8) ==
       0xfffffc20405a4000ULL, "The statistics ring is at %I64x\n",
       AgxQuad(APPLE_AGX_OBJECT_RUNTIME, APPLE_AGX_RUNTIME_RX + 3 * APPLE_AGX_RUNTIME_RX_STRIDE + 8));
    ok_int(AgxLong(APPLE_AGX_OBJECT_RUNTIME, APPLE_AGX_RUNTIME_UNKNOWN_2D0), 4);
    ok_hex(AgxLong(APPLE_AGX_OBJECT_RUNTIME, APPLE_AGX_RUNTIME_UNKNOWN_3B0), 0xff);
    ok(AgxQuad(APPLE_AGX_OBJECT_RUNTIME, APPLE_AGX_RUNTIME_TA_ACTIVITY) == 0xfffffc20406fb5fcULL + 0x1880,
       "The vertex activity is at %I64x\n", AgxQuad(APPLE_AGX_OBJECT_RUNTIME, APPLE_AGX_RUNTIME_TA_ACTIVITY));

    /* The firmware is enabled through both control words and has not announced readiness. */
    ok_int(AgxLong(APPLE_AGX_OBJECT_CONTROL, APPLE_AGX_CONTROL_ENABLE), 1);
    ok_int(AgxLong(APPLE_AGX_OBJECT_CONTROL, APPLE_AGX_CONTROL_ENABLE2), 1);
    ok(AgxQuad(APPLE_AGX_OBJECT_CONTROL, APPLE_AGX_CONTROL_FWCTL_RING) == 0xfffffc2000376c00ULL,
       "The firmware control ring is at %I64x\n",
       AgxQuad(APPLE_AGX_OBJECT_CONTROL, APPLE_AGX_CONTROL_FWCTL_RING));
    ok(AgxQuad(APPLE_AGX_OBJECT_HWDATA, APPLE_AGX_HWDATA_TIMESTAMPS) == 0xfffffc2ffc000000ULL,
       "The timestamp arena is at %I64x\n", AgxQuad(APPLE_AGX_OBJECT_HWDATA, APPLE_AGX_HWDATA_TIMESTAMPS));
    ok(AgxQuad(APPLE_AGX_OBJECT_HWDATA, APPLE_AGX_HWDATA_TTBAT) == 0x108fff78000ULL,
       "The address table root is %I64x\n", AgxQuad(APPLE_AGX_OBJECT_HWDATA, APPLE_AGX_HWDATA_TTBAT));

    ok(!AppleAgxInitReady(&Init), "The firmware announced readiness before it ran\n");
    AgxWriteLong(AgxInitBuffers[APPLE_AGX_OBJECT_RUNTIME], APPLE_AGX_RUNTIME_READY, 0xabcdabcdUL);
    ok(AppleAgxInitReady(&Init), "The readiness the firmware writes was not seen\n");

    for (Index = 0; Index < APPLE_AGX_OBJECTS; ++Index)
        HeapFree(GetProcessHeap(), 0, AgxInitBuffers[Index]);
}

static UCHAR AgxRingState[0x30];
static UCHAR AgxRingRecords[APPLE_AGX_RING_SLOTS * APPLE_AGX_DEVCTL_RECORD];

static
VOID
AgxRingSetIndex(
    _In_ ULONG Offset,
    _In_ ULONG Value)
{
    RtlCopyMemory(AgxRingState + Offset, &Value, sizeof(Value));
}

static
VOID
TestAgxRing(VOID)
{
    APPLE_AGX_RING Ring;
    UCHAR Record[APPLE_AGX_DEVCTL_RECORD];
    UCHAR Taken[APPLE_AGX_DEVCTL_RECORD];
    ULONG Index;

    RtlZeroMemory(AgxRingState, sizeof(AgxRingState));
    RtlZeroMemory(AgxRingRecords, sizeof(AgxRingRecords));
    RtlZeroMemory(Record, sizeof(Record));
    Record[0] = 0x5a;
    Record[APPLE_AGX_SUBMIT_RECORD - 1] = 0xa5;

    /* A ring takes the producer the state block already carries. */
    AgxRingSetIndex(APPLE_AGX_STATE_WRITE, 7);
    AppleAgxRingBind(&Ring, AgxRingState, AgxRingRecords, APPLE_AGX_SUBMIT_RECORD);
    ok_int(Ring.Producer, 7);
    ok_int(AppleAgxRingIndex(&Ring, APPLE_AGX_STATE_WRITE), 7);

    RtlZeroMemory(AgxRingState, sizeof(AgxRingState));
    AppleAgxRingBind(&Ring, AgxRingState, AgxRingRecords, APPLE_AGX_SUBMIT_RECORD);
    ok_int(Ring.Producer, 0);

    /* A record lands in the producer's slot and the producer moves on. */
    ok_hex(AppleAgxRingPost(&Ring, Record, FALSE, NULL), STATUS_SUCCESS);
    ok_int(AgxRingRecords[0], 0x5a);
    ok_int(AgxRingRecords[APPLE_AGX_SUBMIT_RECORD - 1], 0xa5);
    ok_int(Ring.Producer, 1);
    ok_int(AppleAgxRingIndex(&Ring, APPLE_AGX_STATE_WRITE), 1);

    /* Without the record being consumed, a second one may not follow it. */
    ok_hex(AppleAgxRingPost(&Ring, Record, FALSE, NULL), STATUS_DEVICE_BUSY);
    AgxRingSetIndex(APPLE_AGX_STATE_READ, 1);
    ok_hex(AppleAgxRingPost(&Ring, Record, FALSE, NULL), STATUS_SUCCESS);
    ok_int(Ring.Producer, 2);

    /* Behind unconsumed records it goes only while fewer than the allowed number are out. */
    AgxRingSetIndex(APPLE_AGX_STATE_READ, 2);
    for (Index = 0; Index < APPLE_AGX_RING_AHEAD; ++Index)
        ok_hex(AppleAgxRingPost(&Ring, Record, TRUE, NULL), STATUS_SUCCESS);
    ok_hex(AppleAgxRingPost(&Ring, Record, TRUE, NULL), STATUS_DEVICE_BUSY);
    AgxRingSetIndex(APPLE_AGX_STATE_READ, 3);
    ok_hex(AppleAgxRingPost(&Ring, Record, TRUE, NULL), STATUS_SUCCESS);

    /* The producer wraps at the end of the ring rather than running past it. */
    RtlZeroMemory(AgxRingState, sizeof(AgxRingState));
    AgxRingSetIndex(APPLE_AGX_STATE_WRITE, APPLE_AGX_RING_SLOTS - 1);
    AgxRingSetIndex(APPLE_AGX_STATE_READ, APPLE_AGX_RING_SLOTS - 1);
    AppleAgxRingBind(&Ring, AgxRingState, AgxRingRecords, APPLE_AGX_SUBMIT_RECORD);
    ok_hex(AppleAgxRingPost(&Ring, Record, FALSE, NULL), STATUS_SUCCESS);
    ok_int(Ring.Producer, 0);
    ok_int(AgxRingRecords[(APPLE_AGX_RING_SLOTS - 1) * APPLE_AGX_SUBMIT_RECORD], 0x5a);

    /* A state block that disagrees with the host's own producer is not written to. */
    AgxRingSetIndex(APPLE_AGX_STATE_WRITE, 42);
    ok_hex(AppleAgxRingPost(&Ring, Record, FALSE, NULL), STATUS_DEVICE_DATA_ERROR);

    /* On a receive ring the firmware produces: every waiting record is taken, then none. */
    RtlZeroMemory(AgxRingState, sizeof(AgxRingState));
    RtlZeroMemory(AgxRingRecords, sizeof(AgxRingRecords));
    for (Index = 0; Index < 3; ++Index)
        AgxRingRecords[Index * APPLE_AGX_EVENT_RECORD] = (UCHAR)(0x10 + Index);
    AgxRingSetIndex(APPLE_AGX_STATE_WRITE, 3);
    AppleAgxRingBind(&Ring, AgxRingState, AgxRingRecords, APPLE_AGX_EVENT_RECORD);
    for (Index = 0; Index < 3; ++Index)
    {
        ok(AppleAgxRingTake(&Ring, Taken), "Record %lu was not waiting\n", Index);
        ok_int(Taken[0], 0x10 + Index);
    }
    ok(!AppleAgxRingTake(&Ring, Taken), "An empty receive ring gave a record\n");
    ok_int(AppleAgxRingIndex(&Ring, APPLE_AGX_STATE_READ), 3);

    /* A ring may also be consumed whole, which is how the log and statistics rings are read. */
    AgxRingSetIndex(APPLE_AGX_STATE_WRITE, 200);
    AppleAgxRingConsume(&Ring);
    ok_int(AppleAgxRingIndex(&Ring, APPLE_AGX_STATE_READ), 200);
    ok(!AppleAgxRingTake(&Ring, Taken), "A consumed ring still gave a record\n");

    /* A ring with nowhere to put a record refuses rather than writing somewhere. */
    RtlZeroMemory(&Ring, sizeof(Ring));
    ok_hex(AppleAgxRingPost(&Ring, Record, FALSE, NULL), STATUS_INVALID_DEVICE_STATE);
    ok(!AppleAgxRingTake(&Ring, Taken), "A ring with no memory gave a record\n");

    /* A posted record is done only once both the consumer and the completion index pass it. */
    RtlZeroMemory(AgxRingState, sizeof(AgxRingState));
    AppleAgxRingBind(&Ring, AgxRingState, AgxRingRecords, APPLE_AGX_DEVCTL_RECORD);
    ok_hex(AppleAgxRingPost(&Ring, Record, FALSE, &Index), STATUS_SUCCESS);
    ok_int(Index, 1);
    ok(!AppleAgxRingDone(&Ring, Index), "A record was done before the firmware read it\n");
    AgxRingSetIndex(APPLE_AGX_STATE_READ, 1);
    ok(!AppleAgxRingDone(&Ring, Index), "A record was done before it completed\n");
    AgxRingSetIndex(APPLE_AGX_STATE_READ, 0);
    AgxRingSetIndex(APPLE_AGX_STATE_COMPLETION, 1);
    ok(!AppleAgxRingDone(&Ring, Index), "A record was done before it was read\n");
    AgxRingSetIndex(APPLE_AGX_STATE_READ, 1);
    ok(AppleAgxRingDone(&Ring, Index), "A read and completed record was not done\n");

    /* The doorbells: one per submit channel, and the device control ring's own. */
    ok(AppleAgxRingDoorbell(2) == 0x0083000000000002ULL, "Channel 2 rings %I64x\n",
       AppleAgxRingDoorbell(2));
    ok(AppleAgxRingDoorbell(APPLE_AGX_DOORBELL_DEVCTL) == 0x0083000000000011ULL,
       "The device control ring rings %I64x\n", AppleAgxRingDoorbell(APPLE_AGX_DOORBELL_DEVCTL));
}

static
VOID
TestAgxRegisters(VOID)
{
    ULONG64 HandoffPhysical, ContextsPhysical, RootPhysical, LevelPhysical, Physical;
    APPLE_UAT_ADOPTED Adopted;
    APPLE_AGX_INIT Init;
    PVOID Hwdata;
    ULONG Index, Set;

    /* The windows of this part, at the addresses its firmware reaches them by. */
    ok(AppleAgxIoMap(0)->Physical == 0x290d00000ULL && AppleAgxIoMap(0)->Size == 0x144000 &&
       AppleAgxIoMap(0)->Slot == 0 && AppleAgxIoMap(0)->Address == 0xfffffc2068000000ULL,
       "The first window is %I64x+%lx at %I64x slot %u\n", AppleAgxIoMap(0)->Physical,
       AppleAgxIoMap(0)->Size, AppleAgxIoMap(0)->Address, AppleAgxIoMap(0)->Slot);
    /* The graphics block's own ranges, which the machine's tree names as well. */
    ok(AppleAgxIoMap(3)->Physical == 0x290000000ULL && AppleAgxIoMap(3)->Slot == 3,
       "The register range is %I64x slot %u\n", AppleAgxIoMap(3)->Physical, AppleAgxIoMap(3)->Slot);
    /* The window the machine's tree points its software interrupt into. */
    ok(AppleAgxIoMap(2)->Physical == 0x351014000ULL, "The interrupt window is %I64x\n",
       AppleAgxIoMap(2)->Physical);
    /* One window is reached at an offset inside its page. */
    ok(AppleAgxIoMap(1)->Address == 0xfffffc2068149000ULL, "The second window is at %I64x\n",
       AppleAgxIoMap(1)->Address);
    ok(AppleAgxIoMap(APPLE_AGX_IOMAPS) == NULL, "A window past the last one was described\n");
    /* Thirteen of the fifteen windows were checked against the ranges this machine reports, and
     * slot 23 starts inside the graphics block's own range; slot 1 lies below every reported
     * range and is the one this table cannot vouch for. */
    ok(AppleAgxIoMap(1)->Physical == 0x20e100000ULL, "The unconfirmed window moved to %I64x\n",
       AppleAgxIoMap(1)->Physical);
    ok(AppleAgxIoMap(6)->Physical == 0x220000000ULL && AppleAgxIoMap(6)->Slot == 11,
       "Slot 11 is %I64x at index 6\n", AppleAgxIoMap(6)->Physical);
    ok(AppleAgxIoMap(11)->Physical == 0x293000000ULL && AppleAgxIoMap(11)->Slot == 23,
       "Slot 23 is %I64x at index 11\n", AppleAgxIoMap(11)->Physical);
    for (Index = 0; Index < APPLE_AGX_IOMAPS; ++Index)
    {
        ok(AppleAgxIoMap(Index)->Slot < APPLE_AGX_IOMAP_SLOTS && AppleAgxIoMap(Index)->Size != 0 &&
           (AppleAgxIoMap(Index)->Physical & (APPLE_UAT_PAGE_SIZE - 1)) == 0,
           "Window %lu is %I64x+%lx slot %u\n", Index, AppleAgxIoMap(Index)->Physical,
           AppleAgxIoMap(Index)->Size, AppleAgxIoMap(Index)->Slot);
    }

    UatFirmwarePage(&HandoffPhysical);
    UatFirmwarePage(&ContextsPhysical);
    UatFirmwarePage(&RootPhysical);
    UatFirmwarePage(&LevelPhysical);
    RtlZeroMemory(&Init, sizeof(Init));
    ok_hex(AppleUatAdopt(&Adopted, HandoffPhysical, ContextsPhysical, RootPhysical, LevelPhysical, "test"),
           STATUS_SUCCESS);
    ok_hex(AppleUatAdoptedInstall(&Adopted), STATUS_SUCCESS);
    Init.Adopted = &Adopted;
    Hwdata = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, APPLE_AGX_HWDATA_SIZE);
    Init.Virtual[APPLE_AGX_OBJECT_HWDATA] = Hwdata;
    ok(Hwdata != NULL, "The hardware description was not simulated\n");
    if (!Hwdata)
        return;

    /* Every window is both mapped for the firmware and recorded in its own slot. */
    ok_hex(AppleAgxMapRegisters(&Init), STATUS_SUCCESS);
    for (Index = 0; Index < APPLE_AGX_IOMAPS; ++Index)
    {
        const APPLE_AGX_IOMAP *Map = AppleAgxIoMap(Index);
        ULONG At = APPLE_AGX_HWDATA_IOMAPS + (ULONG)Map->Slot * APPLE_AGX_HWDATA_IOMAP_STRIDE;
        ULONG64 Recorded = 0;
        ULONG Total = 0, Writable = 0;

        RtlCopyMemory(&Recorded, (PUCHAR)Hwdata + At + APPLE_AGX_IOMAP_PHYSICAL, sizeof(Recorded));
        ok(Recorded == Map->Physical, "Slot %u records %I64x\n", Map->Slot, Recorded);
        RtlCopyMemory(&Recorded, (PUCHAR)Hwdata + At + APPLE_AGX_IOMAP_ADDRESS, sizeof(Recorded));
        ok(Recorded == Map->Address, "Slot %u is reached at %I64x\n", Map->Slot, Recorded);
        RtlCopyMemory(&Total, (PUCHAR)Hwdata + At + APPLE_AGX_IOMAP_TOTAL, sizeof(Total));
        RtlCopyMemory(&Writable, (PUCHAR)Hwdata + At + APPLE_AGX_IOMAP_WRITABLE, sizeof(Writable));
        ok(Total == Map->Size && Writable == 1, "Slot %u records %lu bytes, writable %lu\n", Map->Slot,
           Total, Writable);
        /* The window really translates, from the page its address falls in. */
        ok(AppleUatAdoptedTranslate(&Adopted, Map->Address & ~(APPLE_UAT_PAGE_SIZE - 1), &Physical) &&
           Physical == Map->Physical, "Slot %u translates to %I64x, not %I64x\n", Map->Slot, Physical,
           Map->Physical);
    }

    /* A slot no window uses is left alone. */
    for (Index = 0, Set = 0; Index < APPLE_AGX_IOMAP_SLOTS; ++Index)
    {
        ULONG At = APPLE_AGX_HWDATA_IOMAPS + Index * APPLE_AGX_HWDATA_IOMAP_STRIDE;
        ULONG64 Recorded = 0;

        RtlCopyMemory(&Recorded, (PUCHAR)Hwdata + At, sizeof(Recorded));
        if (Recorded)
            ++Set;
    }
    ok_int(Set, APPLE_AGX_IOMAPS);

    /* Without a hardware description there is nowhere to record a window. */
    Init.Virtual[APPLE_AGX_OBJECT_HWDATA] = NULL;
    ok_hex(AppleAgxMapRegisters(&Init), STATUS_INVALID_DEVICE_STATE);

    HeapFree(GetProcessHeap(), 0, Hwdata);
    AppleUatAdoptedRelease(&Adopted);
}

static
VOID
TestAgxStates(VOID)
{
    APPLE_AGX_DESCRIPTION Description;
    APPLE_AGX_INIT Init;
    PVOID Hwdata;
    ULONG Index, Top = 0, Repeat;

    /* The nine states each table names, and that they are not the same nine. */
    ok(AppleAgxDvfsStates(FALSE)[0] == 0 && AppleAgxDvfsStates(FALSE)[4] == 5 &&
       AppleAgxDvfsStates(FALSE)[8] == 13, "The first table names %u/%u/%u\n",
       AppleAgxDvfsStates(FALSE)[0], AppleAgxDvfsStates(FALSE)[4], AppleAgxDvfsStates(FALSE)[8]);
    ok(AppleAgxDvfsStates(TRUE)[4] == 4 && AppleAgxDvfsStates(TRUE)[8] == 12,
       "The second table names %u/%u\n", AppleAgxDvfsStates(TRUE)[4], AppleAgxDvfsStates(TRUE)[8]);

    RtlZeroMemory(&Description, sizeof(Description));
    Description.States = 14;
    for (Index = 0; Index < Description.States; ++Index)
    {
        Description.State[Index].Frequency = (Index + 1) * 100000000;
        Description.State[Index].Millivolts = 600 + Index * 10;
        Description.SramState[Index].Millivolts = 800 + Index * 5;
    }
    RtlZeroMemory(&Init, sizeof(Init));
    Hwdata = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, APPLE_AGX_HWDATA_SIZE);
    Init.Virtual[APPLE_AGX_OBJECT_HWDATA] = Hwdata;
    ok(Hwdata != NULL, "The hardware description was not simulated\n");
    if (!Hwdata)
        return;

    ok_hex(AppleAgxWriteStates(&Init, &Description, &Top), STATUS_SUCCESS);
    /* The top state of the first table is state 13: 1400 MHz. */
    ok_int(Top, 1400000);
    ok_int(AgxLong(APPLE_AGX_OBJECT_HWDATA, APPLE_AGX_HWDATA_TOP_MHZ), 1400);
    ok_int(AgxLong(APPLE_AGX_OBJECT_HWDATA, APPLE_AGX_HWDATA_DVFS_COUNT), APPLE_AGX_DVFS_STATES - 1);
    ok_int(AgxLong(APPLE_AGX_OBJECT_HWDATA, APPLE_AGX_HWDATA_CHECK_A38), 1);

    AgxInitBuffers[APPLE_AGX_OBJECT_HWDATA] = Hwdata;
    for (Index = 0; Index < APPLE_AGX_DVFS_STATES; ++Index)
    {
        ULONG A = AppleAgxDvfsStates(FALSE)[Index], B = AppleAgxDvfsStates(TRUE)[Index];

        ok_int(AgxLong(APPLE_AGX_OBJECT_HWDATA, APPLE_AGX_HWDATA_DVFS_MHZ_A + 4 * Index), (A + 1) * 100);
        ok_int(AgxLong(APPLE_AGX_OBJECT_HWDATA, APPLE_AGX_HWDATA_DVFS_MHZ_B + 4 * Index), (B + 1) * 100);
        ok_int(AgxLong(APPLE_AGX_OBJECT_HWDATA, APPLE_AGX_HWDATA_DVFS_MHZ_B2 + 4 * Index), (B + 1) * 100);
        /* One voltage per state, written into all eight of the state's cells. */
        for (Repeat = 0; Repeat < APPLE_AGX_HWDATA_DVFS_REPEAT; ++Repeat)
        {
            ok_int(AgxLong(APPLE_AGX_OBJECT_HWDATA, APPLE_AGX_HWDATA_DVFS_CORE_MV +
                                                        APPLE_AGX_HWDATA_DVFS_STRIDE * Index + 4 * Repeat),
                   600 + A * 10);
            ok_int(AgxLong(APPLE_AGX_OBJECT_HWDATA, APPLE_AGX_HWDATA_DVFS_SRAM_MV +
                                                        APPLE_AGX_HWDATA_DVFS_STRIDE * Index + 4 * Repeat),
                   800 + A * 5);
        }
    }

    /* A machine whose states do not rise, or has too few of them, is refused. */
    Description.State[5].Frequency = Description.State[3].Frequency;
    ok_hex(AppleAgxWriteStates(&Init, &Description, &Top), STATUS_INVALID_PARAMETER);
    ok_int(Top, 0);
    Description.State[5].Frequency = 600000000;
    Description.SramState[5].Millivolts = 1;
    ok_hex(AppleAgxWriteStates(&Init, &Description, &Top), STATUS_INVALID_PARAMETER);
    Description.SramState[5].Millivolts = 825;
    Description.States = 13;
    ok_hex(AppleAgxWriteStates(&Init, &Description, &Top), STATUS_INVALID_PARAMETER);
    Description.States = 14;
    ok_hex(AppleAgxWriteStates(&Init, &Description, &Top), STATUS_SUCCESS);
    Init.Virtual[APPLE_AGX_OBJECT_HWDATA] = NULL;
    ok_hex(AppleAgxWriteStates(&Init, &Description, &Top), STATUS_INVALID_DEVICE_STATE);

    HeapFree(GetProcessHeap(), 0, Hwdata);
    AgxInitBuffers[APPLE_AGX_OBJECT_HWDATA] = NULL;
}

static UCHAR AgxQueueInfo[APPLE_AGX_QINFO_SIZE];
static UCHAR AgxQueueState[APPLE_AGX_QSTATE_SIZE];
static ULONG64 AgxQueueRing[16];

static
VOID
TestAgxQueue(VOID)
{
    UCHAR Record[APPLE_AGX_SUBMIT_RECORD];
    APPLE_AGX_QUEUE Queue;
    ULONG64 Value;
    USHORT Head;
    ULONG Index, Read, Kind;

    RtlZeroMemory(&Queue, sizeof(Queue));
    Queue.Info = 0xfffffc2100000000ULL;
    Queue.State = 0xfffffc2100004000ULL;
    Queue.Ring = 0xfffffc2100008000ULL;
    Queue.JobList = 0xfffffc210000c000ULL;
    Queue.Scratch = 0xfffffc2100010000ULL;
    Queue.SchedContext = 0xfffffc2100014000ULL;
    Queue.InfoVirtual = AgxQueueInfo;
    Queue.StateVirtual = AgxQueueState;
    Queue.RingVirtual = AgxQueueRing;
    Queue.Entries = RTL_NUMBER_OF(AgxQueueRing);

    /* A queue tells the firmware where everything of its own is. */
    ok_hex(AppleAgxQueueDescribe(&Queue), STATUS_SUCCESS);
    RtlCopyMemory(&Value, AgxQueueInfo + APPLE_AGX_QINFO_STATE, sizeof(Value));
    ok(Value == Queue.State, "The queue state is at %I64x\n", Value);
    RtlCopyMemory(&Value, AgxQueueInfo + APPLE_AGX_QINFO_RING, sizeof(Value));
    ok(Value == Queue.Ring, "The work ring is at %I64x\n", Value);
    RtlCopyMemory(&Value, AgxQueueInfo + APPLE_AGX_QINFO_JOB_LIST, sizeof(Value));
    ok(Value == Queue.JobList, "The job list is at %I64x\n", Value);
    RtlCopyMemory(&Value, AgxQueueInfo + APPLE_AGX_QINFO_SCRATCH, sizeof(Value));
    ok(Value == Queue.Scratch, "The scratch is at %I64x\n", Value);
    RtlCopyMemory(&Value, AgxQueueInfo + APPLE_AGX_QINFO_SCHED_CONTEXT, sizeof(Value));
    ok(Value == Queue.SchedContext, "The scheduling context is at %I64x\n", Value);
    RtlCopyMemory(&Value, AgxQueueInfo + APPLE_AGX_QINFO_UNKNOWN_AC, sizeof(Value));
    ok(Value == APPLE_AGX_QINFO_UNKNOWN_AC_VALUE, "The queue carries %I64x\n", Value);
    RtlCopyMemory(&Read, AgxQueueState + APPLE_AGX_QSTATE_CAPACITY, sizeof(Read));
    ok_int(Read, RTL_NUMBER_OF(AgxQueueRing));

    /* Work goes into the ring at the host's cursor and the cursor names the head to run to. */
    ok_hex(AppleAgxQueuePost(&Queue, 0xfffffc2140000000ULL, &Head), STATUS_SUCCESS);
    ok_int(Head, 1);
    ok(AgxQueueRing[0] == 0xfffffc2140000000ULL, "The first work is at %I64x\n", AgxQueueRing[0]);
    RtlCopyMemory(&Read, AgxQueueState + APPLE_AGX_QSTATE_WRITE, sizeof(Read));
    ok_int(Read, 1);
    ok_hex(AppleAgxQueuePost(&Queue, 0xfffffc2140004000ULL, &Head), STATUS_SUCCESS);
    ok_int(Head, 2);
    ok(AgxQueueRing[1] == 0xfffffc2140004000ULL, "The second work is at %I64x\n", AgxQueueRing[1]);

    /* The ring fills and does not write over work the firmware has not read. */
    for (Index = 2; Index < RTL_NUMBER_OF(AgxQueueRing) - 1; ++Index)
        ok_hex(AppleAgxQueuePost(&Queue, 0xfffffc2140000000ULL + Index, &Head), STATUS_SUCCESS);
    ok_hex(AppleAgxQueuePost(&Queue, 0xfffffc2140000000ULL, &Head), STATUS_DEVICE_BUSY);
    Read = 4;
    RtlCopyMemory(AgxQueueState + APPLE_AGX_QSTATE_READ, &Read, sizeof(Read));
    ok_hex(AppleAgxQueuePost(&Queue, 0xfffffc2140008000ULL, &Head), STATUS_SUCCESS);
    ok_int(Head, 0);
    ok(AgxQueueRing[RTL_NUMBER_OF(AgxQueueRing) - 1] == 0xfffffc2140008000ULL, "The last slot holds %I64x\n",
       AgxQueueRing[RTL_NUMBER_OF(AgxQueueRing) - 1]);

    /* A cursor the firmware could not have written is refused, and so is nothing to run. */
    Read = 99;
    RtlCopyMemory(AgxQueueState + APPLE_AGX_QSTATE_READ, &Read, sizeof(Read));
    ok_hex(AppleAgxQueuePost(&Queue, 0xfffffc2140000000ULL, &Head), STATUS_DEVICE_DATA_ERROR);
    Read = 0;
    RtlCopyMemory(AgxQueueState + APPLE_AGX_QSTATE_READ, &Read, sizeof(Read));
    ok_hex(AppleAgxQueuePost(&Queue, 0, &Head), STATUS_INVALID_PARAMETER);

    /* What the firmware reports as finished. */
    Read = 7;
    RtlCopyMemory(AgxQueueState + APPLE_AGX_QSTATE_DONE, &Read, sizeof(Read));
    ok_int(AppleAgxQueueDone(&Queue), 7);

    /* The record a submission carries: the queue, what to run, and how far. */
    for (Kind = APPLE_AGX_WORK_VERTEX; Kind <= APPLE_AGX_WORK_COMPUTE; ++Kind)
    {
        ULONG Written = 0;
        USHORT At = 0;

        AppleAgxSubmitRecord(Record, Queue.Info, Kind, (USHORT)(300 + Kind), 5, Kind == APPLE_AGX_WORK_COMPUTE);
        RtlCopyMemory(&Value, Record + APPLE_AGX_SUBMIT_QUEUE, sizeof(Value));
        ok(Value == Queue.Info, "The record names the queue at %I64x\n", Value);
        RtlCopyMemory(&Written, Record + APPLE_AGX_SUBMIT_KIND, sizeof(Written));
        ok_int(Written, Kind);
        RtlCopyMemory(&At, Record + APPLE_AGX_SUBMIT_HEAD, sizeof(At));
        ok_int(At, 300 + Kind);
        ok_int(Record[APPLE_AGX_SUBMIT_EVENT], 5);
        ok_int(Record[APPLE_AGX_SUBMIT_NEW_QUEUE], Kind == APPLE_AGX_WORK_COMPUTE ? 1 : 0);
        RtlCopyMemory(&Value, Record, sizeof(Value));
        ok(Value == 0, "The record's first word is %I64x\n", Value);
    }

    /* The objects the graph shares: the notifier, the job list and the scheduling context. */
    {
        UCHAR Notifier[APPLE_AGX_NOTIFIER_SIZE], List[APPLE_AGX_JOB_LIST_SIZE];
        UCHAR Context[APPLE_AGX_SCHED_CONTEXT_SIZE];
        ULONG Mask = 0;

        RtlFillMemory(Notifier, sizeof(Notifier), 0x5a);
        RtlFillMemory(List, sizeof(List), 0x5a);
        RtlFillMemory(Context, sizeof(Context), 0x5a);
        ok_hex(AppleAgxDescribeGraph(Notifier, 0xfffffc2100018000ULL, List, 0xfffffc210000c000ULL, Context),
               STATUS_SUCCESS);
        RtlCopyMemory(&Value, Notifier + APPLE_AGX_NOTIFIER_THRESHOLD, sizeof(Value));
        ok(Value == 0xfffffc2100018000ULL, "The notifier signals through %I64x\n", Value);
        RtlCopyMemory(&Mask, Notifier + APPLE_AGX_NOTIFIER_UNKNOWN_10, sizeof(Mask));
        ok_hex(Mask, APPLE_AGX_NOTIFIER_UNKNOWN_10_VALUE);
        for (Index = 0, Read = 0; Index < 8; ++Index)
        {
            if (Notifier[APPLE_AGX_NOTIFIER_MASK + Index] == 0xff)
                ++Read;
        }
        ok_int(Read, 8);
        ok_int(Notifier[APPLE_AGX_NOTIFIER_MASK + 8], 0);
        RtlCopyMemory(&Value, List + APPLE_AGX_JOB_LIST_SELF, sizeof(Value));
        ok(Value == 0xfffffc210000c000ULL, "The job list names itself %I64x\n", Value);
        RtlCopyMemory(&Value, List, sizeof(Value));
        ok(Value == 0, "The job list starts with %I64x\n", Value);
        for (Index = 0, Read = 0; Index < sizeof(Context); ++Index)
        {
            if (Context[Index])
                ++Read;
        }
        ok_int(Read, 0);
        ok_hex(AppleAgxDescribeGraph(NULL, 1, List, 1, Context), STATUS_INVALID_PARAMETER);
        ok_hex(AppleAgxDescribeGraph(Notifier, 0, List, 1, Context), STATUS_INVALID_PARAMETER);
    }

    /* The sequence opcodes the firmware interprets. */
    ok_hex(APPLE_AGX_OP_COMPUTE_START, 0x0b);
    ok_hex(APPLE_AGX_OP_COMPUTE_FINALIZE, 0x0c);
    ok_hex(APPLE_AGX_OP_END, 0x40000002);
    ok_hex(APPLE_AGX_OP_TIMESTAMP, 0x80000003);

    /* A queue with nothing of its own is refused rather than described. */
    RtlZeroMemory(&Queue, sizeof(Queue));
    ok_hex(AppleAgxQueueDescribe(&Queue), STATUS_INVALID_PARAMETER);
    ok_hex(AppleAgxQueuePost(&Queue, 0xfffffc2140000000ULL, &Head), STATUS_INVALID_PARAMETER);
    ok_int(AppleAgxQueueDone(&Queue), 0);
}

static UCHAR AgxManager[APPLE_AGX_MANAGER_SIZE];
static UCHAR AgxPoolTable[APPLE_AGX_POOLS * APPLE_AGX_POOL_STRIDE];
static ULONG64 AgxFreelist[APPLE_AGX_FREELIST_SEEDS];

static
VOID
TestAgxPools(VOID)
{
    const ULONG64 First = 0xfffffc2d00000000ULL;
    ULONG64 Value;
    ULONG Index, Seeds = 0, Word;

    /* The allocator names its freelist, its pool table and its counter. */
    RtlFillMemory(AgxManager, sizeof(AgxManager), 0x5a);
    ok_hex(AppleAgxDescribeManager(AgxManager, 0xfffffc2d10000000ULL, 0xfffffc2d20000000ULL,
                                   0xfffffc2100020000ULL), STATUS_SUCCESS);
    RtlCopyMemory(&Word, AgxManager + APPLE_AGX_MANAGER_ID, sizeof(Word));
    ok_int(Word, APPLE_AGX_MANAGER_COMPUTE_ID);
    RtlCopyMemory(&Word, AgxManager + APPLE_AGX_MANAGER_INDEX, sizeof(Word));
    ok_int(Word, APPLE_AGX_MANAGER_COMPUTE_INDEX);
    RtlCopyMemory(&Word, AgxManager + APPLE_AGX_MANAGER_ONE, sizeof(Word));
    ok_int(Word, 1);
    RtlCopyMemory(&Value, AgxManager + APPLE_AGX_MANAGER_FREELIST, sizeof(Value));
    ok(Value == 0xfffffc2d10000000ULL, "The freelist is at %I64x\n", Value);
    RtlCopyMemory(&Word, AgxManager + APPLE_AGX_MANAGER_UNKNOWN_1C, sizeof(Word));
    ok_hex(Word, APPLE_AGX_MANAGER_UNKNOWN_1C_VALUE);
    RtlCopyMemory(&Word, AgxManager + APPLE_AGX_MANAGER_SEEDS_A, sizeof(Word));
    ok_int(Word, APPLE_AGX_FREELIST_SEEDS);
    RtlCopyMemory(&Word, AgxManager + APPLE_AGX_MANAGER_SEEDS_B, sizeof(Word));
    ok_int(Word, APPLE_AGX_FREELIST_SEEDS);
    RtlCopyMemory(&Value, AgxManager + APPLE_AGX_MANAGER_POOL_TABLE, sizeof(Value));
    ok(Value == 0xfffffc2d20000000ULL, "The pool table is at %I64x\n", Value);
    RtlCopyMemory(&Word, AgxManager + APPLE_AGX_MANAGER_UNKNOWN_44, sizeof(Word));
    ok_hex(Word, APPLE_AGX_MANAGER_UNKNOWN_44_VALUE);
    RtlCopyMemory(&Value, AgxManager + APPLE_AGX_MANAGER_COUNTER, sizeof(Value));
    ok(Value == 0xfffffc2100020000ULL, "The counter is at %I64x\n", Value);
    ok_hex(AppleAgxDescribeManager(AgxManager, 0, 1, 1), STATUS_INVALID_PARAMETER);

    /* Every pool is named with its valid bit, and only the first eight are broken into units. */
    ok_hex(AppleAgxSeedPools(AgxPoolTable, AgxFreelist, First, &Seeds), STATUS_SUCCESS);
    ok_int(Seeds, APPLE_AGX_FREELIST_SEEDS);
    for (Index = 0; Index < APPLE_AGX_POOLS; ++Index)
    {
        RtlCopyMemory(&Value, AgxPoolTable + Index * APPLE_AGX_POOL_STRIDE, sizeof(Value));
        ok(Value == ((First + (ULONG64)Index * APPLE_AGX_POOL_SIZE) | APPLE_AGX_POOL_VALID),
           "Pool %lu is %I64x\n", Index, Value);
    }
    /* The rest of a row is left alone. */
    RtlCopyMemory(&Value, AgxPoolTable + 8, sizeof(Value));
    ok(Value == 0, "A pool row carries %I64x past its address\n", Value);

    /* The freelist walks every unit of each seeded pool in turn. */
    ok(AgxFreelist[0] == First, "The first unit is %I64x\n", AgxFreelist[0]);
    ok(AgxFreelist[1] == First + APPLE_AGX_POOL_UNIT, "The second unit is %I64x\n", AgxFreelist[1]);
    ok(AgxFreelist[1023] == First + 1023 * APPLE_AGX_POOL_UNIT, "The last unit of the first pool is %I64x\n",
       AgxFreelist[1023]);
    ok(AgxFreelist[1024] == First + APPLE_AGX_POOL_SIZE, "The first unit of the second pool is %I64x\n",
       AgxFreelist[1024]);
    ok(AgxFreelist[APPLE_AGX_FREELIST_SEEDS - 1] ==
       First + (APPLE_AGX_SEEDED_POOLS - 1) * (ULONG64)APPLE_AGX_POOL_SIZE +
           1023 * APPLE_AGX_POOL_UNIT, "The last seed is %I64x\n",
       AgxFreelist[APPLE_AGX_FREELIST_SEEDS - 1]);
    /* The seeded pools are exactly the memory the firmware may hand out. */
    ok_int(APPLE_AGX_SEEDED_POOLS * (APPLE_AGX_POOL_SIZE / APPLE_AGX_POOL_UNIT), APPLE_AGX_FREELIST_SEEDS);

    ok_hex(AppleAgxSeedPools(AgxPoolTable, AgxFreelist, First + 1, &Seeds), STATUS_INVALID_PARAMETER);
    ok_int(Seeds, 0);
    ok_hex(AppleAgxSeedPools(NULL, AgxFreelist, First, &Seeds), STATUS_INVALID_PARAMETER);
}

static UCHAR AgxSequence[APPLE_AGX_COMPUTE_SEQUENCE_SIZE];

static
VOID
TestAgxEncodeCompute(VOID)
{
    APPLE_AGX_ATTACHMENT Attachment[2];
    APPLE_AGX_COMPUTE_REFS Refs;
    ULONG64 Value;
    ULONG Word, Final = APPLE_AGX_COMPUTE_FINALIZE_AT, Index;
    USHORT Half;

    RtlZeroMemory(&Refs, sizeof(Refs));
    Refs.Hwdata = 0xfffffc20406fb5fcULL;
    Refs.Notifier = 0xfffffc2100018000ULL;
    Refs.Queue = 0xfffffc2100000000ULL;
    Refs.FirmwareStamp = 0xfffffc210001c000ULL;
    Refs.Manager = 0xfffffc2100024000ULL;
    Refs.Work = 0xfffffc2140000000ULL;
    for (Index = 0; Index < 5; ++Index)
        Refs.Scratch[Index] = 0xfffffc2140100000ULL + Index * 0x1000;
    /* An attachment is named by the grain its memory falls in, not by its own bounds. */
    Attachment[0].Pointer = 0xfffffc2d00000040ULL;
    Attachment[0].Size = 0x200;
    Attachment[1].Pointer = 0xfffffc2d00010000ULL;
    Attachment[1].Size = 0x80;

    RtlFillMemory(AgxSequence, sizeof(AgxSequence), 0x5a);
    ok_hex(AppleAgxEncodeCompute(AgxSequence, &Refs, Attachment, 2, 7, 0x1234, TRUE), STATUS_SUCCESS);

    /* The sequence starts the work and names what it runs from. */
    RtlCopyMemory(&Word, AgxSequence + 0x00, sizeof(Word));
    ok_hex(Word, APPLE_AGX_OP_COMPUTE_START);
    RtlCopyMemory(&Value, AgxSequence + 0x14, sizeof(Value));
    ok(Value == Refs.Work + APPLE_AGX_COMPUTE_REGISTERS, "The registers are at %I64x\n", Value);
    RtlCopyMemory(&Value, AgxSequence + 0x1c, sizeof(Value));
    ok(Value == Refs.Hwdata + APPLE_AGX_COMPUTE_HWDATA, "The activity record is at %I64x\n", Value);
    RtlCopyMemory(&Value, AgxSequence + 0x24, sizeof(Value));
    ok(Value == Refs.Queue, "The queue is at %I64x\n", Value);
    RtlCopyMemory(&Word, AgxSequence + 0x2c, sizeof(Word));
    ok_int(Word, 7);
    RtlCopyMemory(&Word, AgxSequence + 0x30, sizeof(Word));
    ok_int(Word, 1);
    RtlCopyMemory(&Word, AgxSequence + 0x50, sizeof(Word));
    ok_hex(Word, APPLE_AGX_COMPUTE_UUID);

    /* Both attachments, rounded out to their grain. */
    RtlCopyMemory(&Value, AgxSequence + APPLE_AGX_COMPUTE_ATTACHMENTS, sizeof(Value));
    ok(Value == 0xfffffc2d00000000ULL, "The first attachment starts at %I64x\n", Value);
    RtlCopyMemory(&Word, AgxSequence + APPLE_AGX_COMPUTE_ATTACHMENTS + 8, sizeof(Word));
    /* 0x40 + 0x200 rounds up to 0x280, which is five grains from the grain it starts in. */
    ok_int(Word, 5);
    RtlCopyMemory(&Half, AgxSequence + APPLE_AGX_COMPUTE_ATTACHMENTS + 0x0c, sizeof(Half));
    ok_hex(Half, 0x17);
    RtlCopyMemory(&Half, AgxSequence + APPLE_AGX_COMPUTE_ATTACHMENTS + 0x0e, sizeof(Half));
    ok_int(Half, 1);
    RtlCopyMemory(&Value, AgxSequence + APPLE_AGX_COMPUTE_ATTACHMENTS +
                              APPLE_AGX_COMPUTE_ATTACHMENT_STRIDE, sizeof(Value));
    ok(Value == 0xfffffc2d00010000ULL, "The second attachment starts at %I64x\n", Value);
    RtlCopyMemory(&Word, AgxSequence + 0x154, sizeof(Word));
    ok_int(Word, 2);

    /* The allocator, the scratch and the mask the firmware signals through. */
    RtlCopyMemory(&Value, AgxSequence + 0x15c, sizeof(Value));
    ok(Value == Refs.Manager, "The allocator is at %I64x\n", Value);
    RtlCopyMemory(&Value, AgxSequence + 0x174, sizeof(Value));
    ok(Value == Refs.Scratch[0], "The first scratch is at %I64x\n", Value);
    RtlCopyMemory(&Value, AgxSequence + 0x19c, sizeof(Value));
    ok(Value == Refs.Scratch[4], "The last scratch is at %I64x\n", Value);
    RtlCopyMemory(&Value, AgxSequence + 0x1b4, sizeof(Value));
    ok(Value == Refs.Notifier + APPLE_AGX_NOTIFIER_MASK, "The notifier mask is at %I64x\n", Value);
    RtlCopyMemory(&Word, AgxSequence + 0x1f8, sizeof(Word));
    ok_int(Word, 1);

    /* The finalize step, and the end of the sequence. */
    RtlCopyMemory(&Word, AgxSequence + Final, sizeof(Word));
    ok_hex(Word, APPLE_AGX_OP_COMPUTE_FINALIZE);
    RtlCopyMemory(&Value, AgxSequence + Final + 0x0c, sizeof(Value));
    ok(Value == Refs.Queue, "The finalize names the queue at %I64x\n", Value);
    RtlCopyMemory(&Value, AgxSequence + Final + 0x28, sizeof(Value));
    ok(Value == Refs.FirmwareStamp, "The stamp is at %I64x\n", Value);
    RtlCopyMemory(&Word, AgxSequence + Final + 0x30, sizeof(Word));
    ok_hex(Word, 0x1234);
    RtlCopyMemory(&Word, AgxSequence + Final + 0x60, sizeof(Word));
    ok_hex(Word, (ULONG)(0u - APPLE_AGX_COMPUTE_FINALIZE_AT));
    RtlCopyMemory(&Value, AgxSequence + Final + 0x6d, sizeof(Value));
    ok(Value == Refs.Work + 0x860, "The finalize names %I64x\n", Value);
    RtlCopyMemory(&Word, AgxSequence + APPLE_AGX_COMPUTE_END_AT, sizeof(Word));
    ok_hex(Word, APPLE_AGX_OP_END);

    /* Work with nothing to run, or more attachments than a sequence holds, is refused. */
    ok_hex(AppleAgxEncodeCompute(AgxSequence, &Refs, Attachment, APPLE_AGX_MAX_ATTACHMENTS + 1, 0, 0, FALSE),
           STATUS_INVALID_PARAMETER);
    ok_hex(AppleAgxEncodeCompute(AgxSequence, &Refs, NULL, 1, 0, 0, FALSE), STATUS_INVALID_PARAMETER);
    Refs.Work = 0;
    ok_hex(AppleAgxEncodeCompute(AgxSequence, &Refs, Attachment, 1, 0, 0, FALSE), STATUS_INVALID_PARAMETER);
    Refs.Work = 0xfffffc2140000000ULL;
    ok_hex(AppleAgxEncodeCompute(AgxSequence, &Refs, NULL, 0, 0, 0, FALSE), STATUS_SUCCESS);
    RtlCopyMemory(&Word, AgxSequence + 0x154, sizeof(Word));
    ok_int(Word, 0);
}

static UCHAR AgxWork[APPLE_AGX_COMPUTE_WORK_SIZE];

static
VOID
TestAgxComputeWork(VOID)
{
    APPLE_AGX_COMPUTE_REFS Refs;
    ULONG64 Register[APPLE_AGX_COMPUTE_REGISTER_COUNT];
    ULONG64 Value;
    ULONG Index, Word;
    USHORT Half;

    RtlZeroMemory(&Refs, sizeof(Refs));
    Refs.Notifier = 0xfffffc2100018000ULL;
    Refs.Manager = 0xfffffc2100024000ULL;
    Refs.FirmwareStamp = 0xfffffc210001c000ULL;
    Refs.UserStamp = 0xfffffc2d00100000ULL;
    Refs.WorkClient = 0xfffffc2d00200000ULL;
    Refs.Sequence = 0xfffffc2140008000ULL;
    Refs.Timestamps = 0xfffffc2140010000ULL;
    Refs.Tail = 0xfffffc2140014000ULL;
    Refs.Preempt = 0xfffffc2d00300000ULL;
    Refs.StreamEnd = 0xfffffc2d00400100ULL;
    for (Index = 0; Index < APPLE_AGX_COMPUTE_REGISTER_COUNT; ++Index)
        Register[Index] = 0x1000ULL * (Index + 1);

    RtlFillMemory(AgxWork, sizeof(AgxWork), 0x5a);
    ok_hex(AppleAgxEncodeComputeWork(AgxWork, &Refs, Register, 7, 3, 0x1234), STATUS_SUCCESS);

    /* What kind of work it is, which context it belongs to and how completion is signalled. */
    RtlCopyMemory(&Word, AgxWork + 0x000, sizeof(Word));
    ok_int(Word, APPLE_AGX_WORK_COMPUTE);
    RtlCopyMemory(&Value, AgxWork + 0x004, sizeof(Value));
    ok(Value == 2, "The ordinal is %I64x\n", Value);
    RtlCopyMemory(&Word, AgxWork + 0x010, sizeof(Word));
    ok_int(Word, 7);
    RtlCopyMemory(&Value, AgxWork + 0x014, sizeof(Value));
    ok(Value == Refs.Notifier, "The notifier is at %I64x\n", Value);

    /* Every register is written beside the selector the firmware writes it to. */
    for (Index = 0; Index < APPLE_AGX_COMPUTE_REGISTER_COUNT; ++Index)
    {
        ULONG At = APPLE_AGX_COMPUTE_REGISTERS + APPLE_AGX_REGISTER_RECORD * Index;

        RtlCopyMemory(&Word, AgxWork + At, sizeof(Word));
        ok(Word == AppleAgxComputeSelectors()[Index], "Register %lu selects %lx\n", Index, Word);
        RtlCopyMemory(&Value, AgxWork + At + 4, sizeof(Value));
        ok(Value == Register[Index], "Register %lu carries %I64x\n", Index, Value);
    }
    ok_hex(AppleAgxComputeSelectors()[0], 0x1a510);
    ok_hex(AppleAgxComputeSelectors()[APPLE_AGX_COMPUTE_REGISTER_COUNT - 1], 0x0d411);

    /* Where the registers, the sequence and the command stream are. */
    RtlCopyMemory(&Value, AgxWork + 0x720, sizeof(Value));
    ok(Value == Refs.WorkClient + APPLE_AGX_COMPUTE_REGISTERS, "The registers are at %I64x\n", Value);
    RtlCopyMemory(&Half, AgxWork + 0x728, sizeof(Half));
    ok_int(Half, APPLE_AGX_COMPUTE_REGISTER_COUNT);
    RtlCopyMemory(&Half, AgxWork + 0x72a, sizeof(Half));
    ok_int(Half, APPLE_AGX_COMPUTE_REGISTER_COUNT * APPLE_AGX_REGISTER_RECORD);
    RtlCopyMemory(&Value, AgxWork + 0x760, sizeof(Value));
    ok(Value == Refs.Sequence, "The sequence is at %I64x\n", Value);
    RtlCopyMemory(&Word, AgxWork + 0x768, sizeof(Word));
    ok_hex(Word, APPLE_AGX_COMPUTE_SEQUENCE_SIZE);
    RtlCopyMemory(&Value, AgxWork + 0x794, sizeof(Value));
    ok(Value == Refs.Preempt, "The preemption buffer is at %I64x\n", Value);
    /* The stream end is named one word short of itself. */
    RtlCopyMemory(&Value, AgxWork + 0x79c, sizeof(Value));
    ok(Value == Refs.StreamEnd - 4, "The command stream ends at %I64x\n", Value);

    /* The stamps, the event slot and the timestamp cells. */
    RtlCopyMemory(&Value, AgxWork + 0x7e8, sizeof(Value));
    ok(Value == Refs.UserStamp, "The user stamp is at %I64x\n", Value);
    RtlCopyMemory(&Value, AgxWork + 0x7f0, sizeof(Value));
    ok(Value == Refs.FirmwareStamp, "The firmware stamp is at %I64x\n", Value);
    RtlCopyMemory(&Word, AgxWork + 0x7f8, sizeof(Word));
    ok_hex(Word, 0x1234);
    RtlCopyMemory(&Word, AgxWork + 0x7fc, sizeof(Word));
    ok_int(Word, APPLE_AGX_EVENT_SLOT);
    RtlCopyMemory(&Word, AgxWork + 0x808, sizeof(Word));
    ok_hex(Word, APPLE_AGX_COMPUTE_UUID);
    RtlCopyMemory(&Value, AgxWork + 0x818, sizeof(Value));
    ok(Value == Refs.Timestamps, "The first cell is at %I64x\n", Value);
    RtlCopyMemory(&Value, AgxWork + 0x820, sizeof(Value));
    ok(Value == Refs.Timestamps + 8, "The second cell is at %I64x\n", Value);
    RtlCopyMemory(&Value, AgxWork + 0x83e, sizeof(Value));
    ok(Value == Refs.Manager, "The allocator is at %I64x\n", Value);
    RtlCopyMemory(&Value, AgxWork + 0x857, sizeof(Value));
    ok(Value == Refs.Tail, "The tail is at %I64x\n", Value);
    RtlCopyMemory(&Word, AgxWork + 0x7e0, sizeof(Word));
    ok_hex(Word, 0xffffffff);

    /* Work that names nothing to run is refused. */
    Refs.Sequence = 0;
    ok_hex(AppleAgxEncodeComputeWork(AgxWork, &Refs, Register, 0, 1, 0), STATUS_INVALID_PARAMETER);
    Refs.Sequence = 0xfffffc2140008000ULL;
    ok_hex(AppleAgxEncodeComputeWork(AgxWork, &Refs, NULL, 0, 1, 0), STATUS_INVALID_PARAMETER);
    ok_hex(AppleAgxEncodeComputeWork(AgxWork, &Refs, Register, 0, 1, 0), STATUS_SUCCESS);
}

static UCHAR AgxRenderWork[APPLE_AGX_FRAGMENT_WORK_SIZE];

static
VOID
TestAgxRenderRegisters(VOID)
{
    static const ULONG Offsets[3] = { APPLE_AGX_VERTEX_REGISTERS, APPLE_AGX_FRAGMENT_REGISTERS,
                                      APPLE_AGX_COMPUTE_REGISTERS };
    static const ULONG Kinds[3] = { APPLE_AGX_WORK_VERTEX, APPLE_AGX_WORK_FRAGMENT,
                                    APPLE_AGX_WORK_COMPUTE };
    static const ULONG Counts[3] = { APPLE_AGX_VERTEX_REGISTER_COUNT, APPLE_AGX_FRAGMENT_REGISTER_COUNT,
                                     APPLE_AGX_COMPUTE_REGISTER_COUNT };
    ULONG64 Value[APPLE_AGX_FRAGMENT_REGISTER_COUNT];
    ULONG64 Read;
    ULONG Kind, Index, Count, Word;

    /* Each engine has its own register set, and each set is a different size. */
    for (Kind = 0; Kind < 3; ++Kind)
    {
        ok(AppleAgxSelectors(Kinds[Kind], &Count) != NULL, "Work kind %lu has no registers\n", Kinds[Kind]);
        ok_int(Count, Counts[Kind]);
    }
    ok(AppleAgxSelectors(2, &Count) == NULL, "An unknown work kind had registers\n");
    ok_int(Count, 0);
    ok_hex(AppleAgxSelectors(APPLE_AGX_WORK_VERTEX, &Count)[0], 0x01748);
    ok_hex(AppleAgxSelectors(APPLE_AGX_WORK_VERTEX, &Count)[APPLE_AGX_VERTEX_REGISTER_COUNT - 1], 0x1c910);
    ok_hex(AppleAgxSelectors(APPLE_AGX_WORK_FRAGMENT, &Count)[0], 0x01739);
    ok_hex(AppleAgxSelectors(APPLE_AGX_WORK_FRAGMENT, &Count)[APPLE_AGX_FRAGMENT_REGISTER_COUNT - 1], 0x0d429);
    /* The register that asks for the initial parameter buffer. */
    ok_hex(AppleAgxSelectors(APPLE_AGX_WORK_VERTEX, &Count)[APPLE_AGX_VERTEX_PARAMETER_REQUEST], 0x1c9a8);

    /* No selector repeats inside a set, which would make a register unreachable. */
    for (Kind = 0; Kind < 3; ++Kind)
    {
        const ULONG *Selector = AppleAgxSelectors(Kinds[Kind], &Count);
        ULONG Repeats = 0, Other;

        for (Index = 0; Index < Count; ++Index)
        {
            for (Other = Index + 1; Other < Count; ++Other)
            {
                if (Selector[Index] == Selector[Other])
                    ++Repeats;
            }
        }
        ok_int(Repeats, 0);
    }

    /* Every engine's registers are written at its own offset, in records of selector and value. */
    for (Kind = 0; Kind < 3; ++Kind)
    {
        const ULONG *Selector = AppleAgxSelectors(Kinds[Kind], &Count);

        for (Index = 0; Index < Count; ++Index)
            Value[Index] = 0x5000ULL * (Index + 1) + Kind;
        RtlZeroMemory(AgxRenderWork, sizeof(AgxRenderWork));
        ok_hex(AppleAgxWriteRegisters(AgxRenderWork, Kinds[Kind], Value), STATUS_SUCCESS);
        for (Index = 0; Index < Count; ++Index)
        {
            ULONG At = Offsets[Kind] + APPLE_AGX_REGISTER_RECORD * Index;

            RtlCopyMemory(&Word, AgxRenderWork + At, sizeof(Word));
            ok(Word == Selector[Index], "Kind %lu register %lu selects %lx\n", Kinds[Kind], Index, Word);
            RtlCopyMemory(&Read, AgxRenderWork + At + 4, sizeof(Read));
            ok(Read == Value[Index], "Kind %lu register %lu carries %I64x\n", Kinds[Kind], Index, Read);
        }
        /* The records stop where the count says they do. */
        RtlCopyMemory(&Word, AgxRenderWork + Offsets[Kind] + APPLE_AGX_REGISTER_RECORD * Count, sizeof(Word));
        ok_int(Word, 0);
    }

    /* The work images are large enough to hold their own registers and tails. */
    ok(APPLE_AGX_VERTEX_REGISTERS + APPLE_AGX_REGISTER_RECORD * APPLE_AGX_VERTEX_REGISTER_COUNT <
       APPLE_AGX_VERTEX_TAIL, "The vertex registers run into its tail\n");
    ok(APPLE_AGX_FRAGMENT_REGISTERS + APPLE_AGX_REGISTER_RECORD * APPLE_AGX_FRAGMENT_REGISTER_COUNT <
       APPLE_AGX_FRAGMENT_TAIL, "The fragment registers run into its tail\n");
    ok(APPLE_AGX_VERTEX_TAIL < APPLE_AGX_VERTEX_WORK_SIZE &&
       APPLE_AGX_FRAGMENT_TAIL < APPLE_AGX_FRAGMENT_WORK_SIZE, "A tail lies past its work image\n");
    ok(APPLE_AGX_VERTEX_END_AT < APPLE_AGX_VERTEX_SEQUENCE_SIZE &&
       APPLE_AGX_FRAGMENT_END_AT < APPLE_AGX_FRAGMENT_SEQUENCE_SIZE &&
       APPLE_AGX_COMPUTE_END_AT < APPLE_AGX_COMPUTE_SEQUENCE_SIZE,
       "A sequence ends past its own length\n");

    ok_hex(AppleAgxWriteRegisters(NULL, APPLE_AGX_WORK_VERTEX, Value), STATUS_INVALID_PARAMETER);
    ok_hex(AppleAgxWriteRegisters(AgxRenderWork, 2, Value), STATUS_INVALID_PARAMETER);
}

static UCHAR AgxPbManager[APPLE_AGX_PB_MANAGER_SIZE];
static UCHAR AgxBlockControl[APPLE_AGX_BLOCK_CONTROL_SIZE];
static UCHAR AgxMisc[APPLE_AGX_MISC_SIZE];

static UCHAR AgxRenderSequence[APPLE_AGX_SEQUENCE_STORAGE];

static
ULONG64
SeqQuad(
    _In_ ULONG Offset)
{
    ULONG64 Value;

    RtlCopyMemory(&Value, AgxRenderSequence + Offset, sizeof(Value));
    return Value;
}

static
ULONG
SeqLong(
    _In_ ULONG Offset)
{
    ULONG Value;

    RtlCopyMemory(&Value, AgxRenderSequence + Offset, sizeof(Value));
    return Value;
}

static
ULONG
SeqNonZeroFrom(
    _In_ ULONG Offset)
{
    ULONG Count = 0;

    for (; Offset < sizeof(AgxRenderSequence); ++Offset)
        Count += AgxRenderSequence[Offset] != 0;
    return Count;
}

static
VOID
TestAgxRenderSequences(VOID)
{
    APPLE_AGX_RENDER_REFS Refs, Bad;
    ULONG64 Tail;
    ULONG Index;

    RtlZeroMemory(&Refs, sizeof(Refs));
    Refs.Work = 0xfffffc2140000000ULL;
    Refs.Sequence = 0xfffffc2140010000ULL;
    Refs.Hwdata = 0xfffffc20406f0000ULL;
    Refs.Notifier = 0xfffffc2100018000ULL;
    Refs.Queue = 0xfffffc2100000000ULL;
    Refs.Scene = 0xfffffc2100030000ULL;
    Refs.Manager = 0xfffffc2100024000ULL;
    Refs.Pool = 0xfffffc2100040000ULL;
    Refs.FirmwareStamp = 0xfffffc210001c000ULL;
    Refs.Scratch = 0xfffffc2140100000ULL;
    Refs.ScratchBytes = 0x10000;

    RtlFillMemory(AgxRenderSequence, sizeof(AgxRenderSequence), 0x5a);
    ok_hex(AppleAgxEncodeVertexSequence(AgxRenderSequence, &Refs, 9, 0x4321), STATUS_SUCCESS);
    Tail = Refs.Work + 0x8a8;
    ok_hex(SeqLong(0x00), 5);
    ok(SeqQuad(0x14) == Refs.Work + 0x40, "The vertex registers are at %I64x\n", SeqQuad(0x14));
    ok(SeqQuad(0x1c) == Refs.Manager, "The manager is at %I64x\n", SeqQuad(0x1c));
    ok(SeqQuad(0x24) == Refs.Scene, "The scene is at %I64x\n", SeqQuad(0x24));
    ok(SeqQuad(0x2c) == Refs.Hwdata + 0x1884, "The vertex statistics are at %I64x\n", SeqQuad(0x2c));
    ok(SeqQuad(0x34) == Refs.Queue, "The queue is at %I64x\n", SeqQuad(0x34));
    ok(SeqQuad(0x3c) == Refs.Work + 0x84c, "The sampler array is at %I64x\n", SeqQuad(0x3c));
    ok_int(SeqLong(0x44), 9);
    ok(SeqQuad(0x64) == Refs.Work + 0x784, "The vertex state is at %I64x\n", SeqQuad(0x64));
    ok(SeqQuad(0x6c) == Refs.Work + 0x890, "The scratch state is at %I64x\n", SeqQuad(0x6c));
    ok(SeqQuad(0x78) == 0x7a000000000000ULL, "The retained identifier is %I64x\n", SeqQuad(0x78));
    ok(SeqQuad(0x18c) == Refs.Pool, "The pool is at %I64x\n", SeqQuad(0x18c));
    ok(SeqQuad(0x1a4) == Refs.Scratch, "The scratch is at %I64x\n", SeqQuad(0x1a4));
    ok(SeqQuad(0x1b4) == 1, "The flag before the notifier is %I64x\n", SeqQuad(0x1b4));
    ok(SeqQuad(0x1bc) == Refs.Notifier + 0xa8, "The notifier mask is at %I64x\n", SeqQuad(0x1bc));
    ok_hex(SeqLong(0x1cc), 0x80000003);
    ok(SeqQuad(0x1cc + 0x04) == Tail, "The start time is at %I64x\n", SeqQuad(0x1d0));
    ok(SeqQuad(0x1cc + 0x14) == Tail + 0x08, "The start packet selects %I64x\n", SeqQuad(0x1e0));
    ok(SeqQuad(0x1cc + 0x2c) == Tail + 0x58, "The completion is at %I64x\n", SeqQuad(0x1f8));
    ok_hex(SeqLong(0x1cc + 0x34), 0x7a0000);
    ok_int(SeqLong(0x208), 1);
    ok_hex(SeqLong(0x20c), 3);
    ok(SeqQuad(0x20c + 0x14) == Tail + 0x10, "The end packet selects %I64x\n", SeqQuad(0x220));
    ok(SeqQuad(0x20c + 0x1c) == Refs.Queue, "The end packet names the queue at %I64x\n", SeqQuad(0x228));
    ok_hex(SeqLong(0x248), 6);
    ok(SeqQuad(0x248 + 0x04) == Refs.Scene, "FinalizeTA names the scene at %I64x\n", SeqQuad(0x24c));
    ok(SeqQuad(0x248 + 0x14) == Refs.Hwdata + 0x1884, "FinalizeTA statistics at %I64x\n", SeqQuad(0x25c));
    ok_int(SeqLong(0x248 + 0x2c), 9);
    ok_hex(SeqLong(0x248 + 0x40), 0x7a0000);
    ok(SeqQuad(0x248 + 0x44) == Refs.FirmwareStamp, "The firmware stamp is at %I64x\n", SeqQuad(0x28c));
    ok_hex(SeqLong(0x248 + 0x4c), 0x4321);
    ok(SeqQuad(0x248 + 0x74) == Refs.Sequence + 0x18c, "The pool field is at %I64x\n", SeqQuad(0x2bc));
    ok_hex(SeqLong(0x248 + 0x7c), (ULONG)-0x248);
    ok(SeqQuad(0x248 + 0x84) == Tail + 0x50, "The request is at %I64x\n", SeqQuad(0x2cc));
    ok_hex(SeqLong(0x2d4), 0x40000002);
    ok_int(SeqNonZeroFrom(0x2d8), 0);

    RtlFillMemory(AgxRenderSequence, sizeof(AgxRenderSequence), 0x5a);
    ok_hex(AppleAgxEncodeFragmentSequence(AgxRenderSequence, &Refs, 9, 0x4321, 3), STATUS_SUCCESS);
    Tail = Refs.Work + 0xbf0;
    ok_hex(SeqLong(0x00), 7);
    ok(SeqQuad(0x14) == Refs.Work + 0x80, "The fragment registers are at %I64x\n", SeqQuad(0x14));
    ok(SeqQuad(0x1c) == Refs.Scene, "The scene is at %I64x\n", SeqQuad(0x1c));
    ok(SeqQuad(0x24) == Refs.Hwdata + 0x24c8, "The fragment statistics are at %I64x\n", SeqQuad(0x24));
    ok(SeqQuad(0x2c) == Refs.Work + 0xb70, "The busy word is at %I64x\n", SeqQuad(0x2c));
    ok(SeqQuad(0x34) == Refs.Work + 0xb74, "The sixth structure is at %I64x\n", SeqQuad(0x34));
    ok(SeqQuad(0x3c) == Refs.Work + 0xba0, "The seventh structure is at %I64x\n", SeqQuad(0x3c));
    ok(SeqQuad(0x44) == Refs.Queue, "The queue is at %I64x\n", SeqQuad(0x44));
    ok(SeqQuad(0x4c) == Refs.Work, "The command is at %I64x\n", SeqQuad(0x4c));
    ok_int(SeqLong(0x54), 9);
    ok_int(SeqLong(0x58), 1);
    ok(SeqQuad(0x68) == 3, "The queue holds %I64u commands\n", SeqQuad(0x68));
    ok(SeqQuad(0x74) == Refs.Work + 0xa58, "The scratch flag is at %I64x\n", SeqQuad(0x74));
    ok(SeqQuad(0x7c) == Refs.Work + 0xbd8, "The scratch state is at %I64x\n", SeqQuad(0x7c));
    ok_hex(SeqLong(0x98), 0x3d0000);
    ok(SeqQuad(0x1a4) == Refs.Pool, "The pool is at %I64x\n", SeqQuad(0x1a4));
    ok(SeqQuad(0x1bc) == Refs.Scratch, "The scratch is at %I64x\n", SeqQuad(0x1bc));
    ok(SeqQuad(0x1d4) == Refs.Notifier + 0xa8, "The notifier mask is at %I64x\n", SeqQuad(0x1d4));
    ok_hex(SeqLong(0x1dc), 0x80000003);
    ok(SeqQuad(0x1dc + 0x04) == Tail, "The start time is at %I64x\n", SeqQuad(0x1e0));
    ok_hex(SeqLong(0x1dc + 0x34), 0x3d0000);
    ok_int(SeqLong(0x218), 1);
    ok_hex(SeqLong(0x21c), 3);
    ok(SeqQuad(0x21c + 0x2c) == Tail + 0x58, "The completion is at %I64x\n", SeqQuad(0x248));
    ok_hex(SeqLong(0x258), 8);
    ok_hex(SeqLong(0x258 + 0x04), 0x3d0000);
    ok(SeqQuad(0x258 + 0x0c) == Refs.FirmwareStamp, "The firmware stamp is at %I64x\n", SeqQuad(0x264));
    ok_hex(SeqLong(0x258 + 0x14), 0x4321);
    ok(SeqQuad(0x258 + 0x24) == Refs.Manager, "Finalize3D names the manager at %I64x\n", SeqQuad(0x27c));
    ok(SeqQuad(0x258 + 0x2c) == 1, "Finalize3D flag is %I64x\n", SeqQuad(0x284));
    ok(SeqQuad(0x258 + 0x54) == Refs.Work, "Finalize3D names the command at %I64x\n", SeqQuad(0x2ac));
    ok(SeqQuad(0x258 + 0x5c) == 9, "Finalize3D context is %I64x\n", SeqQuad(0x2b4));
    ok(SeqQuad(0x258 + 0x9c) == Refs.Sequence + 0x1a4, "The pool field is at %I64x\n", SeqQuad(0x2f4));
    ok_hex(SeqLong(0x258 + 0xa4), (ULONG)-0x258);
    ok_int(SeqLong(0x258 + 0xa8), 1);
    ok(SeqQuad(0x258 + 0xac) == Tail + 0x50, "The request is at %I64x\n", SeqQuad(0x304));
    ok_hex(SeqLong(0x30c), 0x40000002);
    ok_int(SeqNonZeroFrom(0x310), 0);

    ok_hex(AppleAgxEncodeVertexSequence(AgxRenderSequence, &Refs, 64, 0), STATUS_INVALID_PARAMETER);
    ok_hex(AppleAgxEncodeFragmentSequence(AgxRenderSequence, &Refs, 64, 0, 0), STATUS_INVALID_PARAMETER);
    for (Index = 0; Index < 10; ++Index)
    {
        Bad = Refs;
        ((PULONG64)&Bad)[Index] = 0;
        ok_hex(AppleAgxEncodeVertexSequence(AgxRenderSequence, &Bad, 0, 0), STATUS_INVALID_PARAMETER);
    }
    Bad = Refs;
    Bad.ScratchBytes = 0xffff;
    ok_hex(AppleAgxEncodeFragmentSequence(AgxRenderSequence, &Bad, 0, 0, 0), STATUS_INVALID_PARAMETER);
    ok_hex(AppleAgxEncodeVertexSequence(AgxRenderSequence, &Bad, 0, 0), STATUS_SUCCESS);
}

static UCHAR AgxVertexWork[APPLE_AGX_VERTEX_WORK_SIZE];

static
ULONG64
BufQuad(
    _In_ const VOID *Buffer,
    _In_ ULONG Offset)
{
    ULONG64 Value;

    RtlCopyMemory(&Value, (const UCHAR *)Buffer + Offset, sizeof(Value));
    return Value;
}

static
ULONG
BufLong(
    _In_ const VOID *Buffer,
    _In_ ULONG Offset)
{
    ULONG Value;

    RtlCopyMemory(&Value, (const UCHAR *)Buffer + Offset, sizeof(Value));
    return Value;
}

static
USHORT
BufWord(
    _In_ const VOID *Buffer,
    _In_ ULONG Offset)
{
    USHORT Value;

    RtlCopyMemory(&Value, (const UCHAR *)Buffer + Offset, sizeof(Value));
    return Value;
}

static
VOID
TestAgxGeometry(VOID)
{
    APPLE_AGX_GEOMETRY Geometry;

    ok_hex(AppleAgxDescribeGeometry(1920, 1080, 32, 32, 1, 1, 2, &Geometry), STATUS_SUCCESS);
    ok_int(Geometry.Layers, 1);
    ok_int(Geometry.Tiles, 60 * 34);
    ok_hex(Geometry.Pixels, 0x0437077f);
    ok_hex(Geometry.Screen, 0x2103b);
    ok_hex(Geometry.XBlocks, 0x404030);
    ok_hex(Geometry.YBlocks, 0x303024);
    ok_int(Geometry.Blocks, 192);
    ok_int(Geometry.RegionStride, 240);
    ok_int(Geometry.TpcStride, 384);
    ok(Geometry.UtileConfig == 0xa000, "The utile configuration is %I64x\n", Geometry.UtileConfig);
    ok(Geometry.TilemapBytes == 0x3c00, "The tilemap takes %I64x bytes\n", Geometry.TilemapBytes);
    ok(Geometry.TpcBytes == 0xc000, "The TPC takes %I64x bytes\n", Geometry.TpcBytes);

    ok_hex(AppleAgxDescribeGeometry(100, 50, 16, 16, 4, 3, 1, &Geometry), STATUS_SUCCESS);
    ok_hex(Geometry.Pixels, 0x00310063);
    ok_hex(Geometry.Screen, 0x1003);
    ok_hex(Geometry.XBlocks, 0x10100c);
    ok_hex(Geometry.YBlocks, 0x10100c);
    ok_int(Geometry.Blocks, 16);
    ok_int(Geometry.RegionStride, 80);
    ok_int(Geometry.TpcStride, 128);
    ok(Geometry.UtileConfig == 0x5002, "The utile configuration is %I64x\n", Geometry.UtileConfig);
    ok(Geometry.TilemapBytes == 15360, "The tilemap takes %I64u bytes\n", Geometry.TilemapBytes);
    ok(Geometry.TpcBytes == 24576, "The TPC takes %I64u bytes\n", Geometry.TpcBytes);

    ok_hex(AppleAgxDescribeGeometry(16384, 16384, 32, 16, 2, 2048, 2, &Geometry), STATUS_SUCCESS);
    ok_hex(Geometry.Pixels, 0x3fff3fff);
    ok(Geometry.UtileConfig == 0x6001, "The utile configuration is %I64x\n", Geometry.UtileConfig);

    ok_hex(AppleAgxDescribeGeometry(64, 64, 16, 32, 1, 1, 1, &Geometry), STATUS_INVALID_PARAMETER);
    ok_hex(AppleAgxDescribeGeometry(64, 64, 32, 32, 3, 1, 1, &Geometry), STATUS_INVALID_PARAMETER);
    ok_hex(AppleAgxDescribeGeometry(0, 64, 32, 32, 1, 1, 1, &Geometry), STATUS_INVALID_PARAMETER);
    ok_hex(AppleAgxDescribeGeometry(16385, 64, 32, 32, 1, 1, 1, &Geometry), STATUS_INVALID_PARAMETER);
    ok_hex(AppleAgxDescribeGeometry(64, 64, 32, 32, 1, 0, 1, &Geometry), STATUS_INVALID_PARAMETER);
    ok_hex(AppleAgxDescribeGeometry(64, 64, 32, 32, 1, 2049, 1, &Geometry), STATUS_INVALID_PARAMETER);
    ok_hex(AppleAgxDescribeGeometry(64, 64, 32, 32, 1, 1, 0, &Geometry), STATUS_INVALID_PARAMETER);
    ok_hex(AppleAgxDescribeGeometry(16384, 16384, 16, 16, 4, 2048, 0xffffffff, &Geometry), STATUS_INVALID_PARAMETER);
}

static
VOID
TestAgxVertexWork(VOID)
{
    APPLE_AGX_GEOMETRY Geometry;
    APPLE_AGX_VERTEX_INPUT Input, Bad;
    const ULONG Tail = 0x8a8, State = 0x784, Records = 0x40;
    ULONG Count;
    const ULONG *Selector = AppleAgxSelectors(APPLE_AGX_WORK_VERTEX, &Count);

    ok_hex(AppleAgxDescribeGeometry(1920, 1080, 32, 32, 1, 1, 2, &Geometry), STATUS_SUCCESS);
    RtlZeroMemory(&Input, sizeof(Input));
    Input.Notifier = 0xfffffc2100018000ULL;
    Input.Manager = 0xfffffc2100024000ULL;
    Input.Scene = 0xfffffc2100030000ULL;
    Input.Empty = 0xfffffc2100034000ULL;
    Input.WorkClient = 0x0000001500000000ULL;
    Input.Sequence = 0xfffffc2140010000ULL;
    Input.Pool = 0xfffffc2100040000ULL;
    Input.Scratch = 0xfffffc2140100000ULL;
    Input.Stamp = 0xfffffc210001c000ULL;
    Input.FirmwareStamp = 0xfffffc210001c008ULL;
    Input.Timestamps[0] = 0xfffffc2100050000ULL;
    Input.Timestamps[1] = 0xfffffc2100050008ULL;
    Input.UserTimestamps[0] = 0xfffffc2100060000ULL;
    Input.UserTimestamps[1] = 0xfffffc2100060008ULL;
    Input.Preempt[0] = 0x1006080000ULL;
    Input.Preempt[1] = 0x1006098000ULL;
    Input.Preempt[2] = 0x10060b0000ULL;
    Input.Tilemap = 0x1100000000ULL;
    Input.Tpc = 0x1200000000ULL;
    Input.Heap = 0x1300000000ULL;
    Input.SceneUser = 0x1400000000ULL;
    Input.InitialSceneEntry = 0xfffffc2123456780ULL;
    Input.Vdm = 0x0040001000ULL;
    Input.Multisample = 0x11;
    Input.Ppp = 0xffffffff;
    Input.PageCount = 0x400;

    RtlFillMemory(AgxVertexWork, sizeof(AgxVertexWork), 0x5a);
    ok_hex(AppleAgxEncodeVertexWork(AgxVertexWork, &Input, &Geometry, 11, 0x99), STATUS_SUCCESS);
    ok_hex(BufLong(AgxVertexWork, 0x000), 0);
    ok(BufQuad(AgxVertexWork, 0x004) == 1, "The command counter is %I64x\n", BufQuad(AgxVertexWork, 4));
    ok_int(BufLong(AgxVertexWork, 0x00c), 11);
    ok(BufQuad(AgxVertexWork, 0x014) == Input.Notifier, "The notifier is %I64x\n", BufQuad(AgxVertexWork, 0x14));
    ok(BufQuad(AgxVertexWork, 0x024) == Input.Manager, "The manager is %I64x\n", BufQuad(AgxVertexWork, 0x24));
    ok(BufQuad(AgxVertexWork, 0x02c) == Input.Scene, "The scene is %I64x\n", BufQuad(AgxVertexWork, 0x2c));
    ok(BufQuad(AgxVertexWork, 0x034) == Input.Empty, "The empty object is %I64x\n", BufQuad(AgxVertexWork, 0x34));
    ok_hex(BufLong(AgxVertexWork, Records), Selector[0]);
    ok_hex(BufLong(AgxVertexWork, Records + 12 * 71), 0x1c910);
    ok(BufQuad(AgxVertexWork, Records + 12 * 2 + 4) == 0x100000000ULL, "The tilemap register is %I64x\n",
       BufQuad(AgxVertexWork, Records + 12 * 2 + 4));
    ok(BufQuad(AgxVertexWork, Records + 12 * 6 + 4) == 0x200000000ULL, "The TPC register is %I64x\n",
       BufQuad(AgxVertexWork, Records + 12 * 6 + 4));
    ok(BufQuad(AgxVertexWork, Records + 12 * 7 + 4) == (0x300000000ULL | (1ULL << 63)), "The heap register is %I64x\n",
       BufQuad(AgxVertexWork, Records + 12 * 7 + 4));
    ok(BufQuad(AgxVertexWork, Records + 12 * 18 + 4) == (0x60b0000ULL | (1ULL << 50)), "The third preemption register is %I64x\n",
       BufQuad(AgxVertexWork, Records + 12 * 18 + 4));
    ok(BufQuad(AgxVertexWork, Records + 12 * 20 + 4) == Input.Vdm, "The VDM register is %I64x\n",
       BufQuad(AgxVertexWork, Records + 12 * 20 + 4));
    ok(BufQuad(AgxVertexWork, Records + 12 * 26 + 4) == 0x300000000ULL, "The layer metadata register is %I64x\n",
       BufQuad(AgxVertexWork, Records + 12 * 26 + 4));
    ok_hex(BufLong(AgxVertexWork, Records + 12 * 32), 0x1c9a8);
    ok(BufQuad(AgxVertexWork, Records + 12 * 32 + 4) == 0x400, "The parameter request is %I64x\n",
       BufQuad(AgxVertexWork, Records + 12 * 32 + 4));
    ok(BufQuad(AgxVertexWork, Records + 12 * 41 + 4) == 240, "The region stride is %I64u\n",
       BufQuad(AgxVertexWork, Records + 12 * 41 + 4));
    ok(BufQuad(AgxVertexWork, Records + 12 * 44 + 4) == 0x203, "The PPP control is %I64x\n",
       BufQuad(AgxVertexWork, Records + 12 * 44 + 4));
    ok(BufQuad(AgxVertexWork, Records + 12 * 45 + 4) == 0x0437077f, "The pixel bounds are %I64x\n",
       BufQuad(AgxVertexWork, Records + 12 * 45 + 4));
    ok(BufQuad(AgxVertexWork, Records + 12 * 52 + 4) == 0x8000, "The layer mode is %I64x\n",
       BufQuad(AgxVertexWork, Records + 12 * 52 + 4));
    ok(BufQuad(AgxVertexWork, Records + 12 * 69 + 4) == 0x400000040ULL, "The scene user register is %I64x\n",
       BufQuad(AgxVertexWork, Records + 12 * 69 + 4));
    ok(BufQuad(AgxVertexWork, Records + 12 * 71 + 4) == 0xa123456781ULL, "The initial scene entry is %I64x\n",
       BufQuad(AgxVertexWork, Records + 12 * 71 + 4));
    ok(BufQuad(AgxVertexWork, 0x740) == Input.WorkClient + Records, "The register list is at %I64x\n",
       BufQuad(AgxVertexWork, 0x740));
    ok_int(BufWord(AgxVertexWork, 0x748), 72);
    ok_int(BufWord(AgxVertexWork, 0x74a), 72 * 12);
    ok(BufQuad(AgxVertexWork, 0x760) == Input.Tpc, "The TPC is at %I64x\n", BufQuad(AgxVertexWork, 0x760));
    ok(BufQuad(AgxVertexWork, 0x768) == 0xc000, "The TPC takes %I64x bytes\n", BufQuad(AgxVertexWork, 0x768));
    ok(BufQuad(AgxVertexWork, 0x770) == Input.Sequence, "The sequence is at %I64x\n", BufQuad(AgxVertexWork, 0x770));
    ok_hex(BufLong(AgxVertexWork, 0x778), 0x2d8);
    ok_int(BufLong(AgxVertexWork, 0x77c), 1);
    ok_hex(BufLong(AgxVertexWork, 0x780), 0x99);
    ok(BufQuad(AgxVertexWork, State + 0x24) == 0x6080000ULL, "The first preemption buffer is %I64x\n",
       BufQuad(AgxVertexWork, State + 0x24));
    ok_hex(BufLong(AgxVertexWork, State + 0xbc), 0x67);
    ok_hex(BufLong(AgxVertexWork, State + 0xc4), 0xffffffff);
    ok_int(BufLong(AgxVertexWork, State + 0xd0), 1);
    ok(BufQuad(AgxVertexWork, State + 0xe0) == Input.Stamp, "The stamp is at %I64x\n", BufQuad(AgxVertexWork, State + 0xe0));
    ok(BufQuad(AgxVertexWork, State + 0xe8) == Input.FirmwareStamp, "The firmware stamp is at %I64x\n",
       BufQuad(AgxVertexWork, State + 0xe8));
    ok_hex(BufLong(AgxVertexWork, State + 0xf0), 0x99);
    ok_int(BufLong(AgxVertexWork, State + 0xfc), 1);
    ok_hex(BufLong(AgxVertexWork, State + 0x100), 0x7a0000);
    ok(BufQuad(AgxVertexWork, Tail + 0x08) == Input.Timestamps[0], "The first timestamp is at %I64x\n",
       BufQuad(AgxVertexWork, Tail + 0x08));
    ok(BufQuad(AgxVertexWork, Tail + 0x20) == Input.UserTimestamps[1], "The second user timestamp is at %I64x\n",
       BufQuad(AgxVertexWork, Tail + 0x20));
    ok(BufQuad(AgxVertexWork, Tail + 0x2e) == Input.Pool, "The pool is at %I64x\n", BufQuad(AgxVertexWork, Tail + 0x2e));
    ok_int(AgxVertexWork[Tail + 0x36], 1);
    ok(BufQuad(AgxVertexWork, Tail + 0x37) == 0xad0000, "The first tail constant is %I64x\n", BufQuad(AgxVertexWork, Tail + 0x37));
    ok(BufQuad(AgxVertexWork, Tail + 0x3f) == 0x1600000, "The second tail constant is %I64x\n", BufQuad(AgxVertexWork, Tail + 0x3f));
    ok(BufQuad(AgxVertexWork, Tail + 0x47) == Input.Scratch, "The scratch is at %I64x\n", BufQuad(AgxVertexWork, Tail + 0x47));
    ok_int(AgxVertexWork[Tail + 0x4f], 1);
    ok_int(AgxVertexWork[APPLE_AGX_VERTEX_WORK_SIZE - 1], 0);

    ok_hex(AppleAgxDescribeGeometry(256, 256, 32, 32, 1, 4, 2, &Geometry), STATUS_SUCCESS);
    ok_hex(AppleAgxEncodeVertexWork(AgxVertexWork, &Input, &Geometry, 11, 0x99), STATUS_SUCCESS);
    ok(BufQuad(AgxVertexWork, Records + 12 * 52 + 4) == 0xd003, "The layered mode is %I64x\n",
       BufQuad(AgxVertexWork, Records + 12 * 52 + 4));
    ok(BufQuad(AgxVertexWork, Records + 12 * 26 + 4) == 0x300000200ULL, "The layer metadata register is %I64x\n",
       BufQuad(AgxVertexWork, Records + 12 * 26 + 4));
    Bad = Input;
    Bad.ProcessEmptyTiles = TRUE;
    ok_hex(AppleAgxEncodeVertexWork(AgxVertexWork, &Bad, &Geometry, 11, 0x99), STATUS_SUCCESS);
    ok(BufQuad(AgxVertexWork, Records + 12 * 52 + 4) == 0xe003, "The layered mode is %I64x\n",
       BufQuad(AgxVertexWork, Records + 12 * 52 + 4));

    ok_hex(AppleAgxEncodeVertexWork(AgxVertexWork, &Input, &Geometry, 64, 0), STATUS_INVALID_PARAMETER);
    Bad = Input;
    Bad.Tilemap = 0x0fffffffffULL;
    ok_hex(AppleAgxEncodeVertexWork(AgxVertexWork, &Bad, &Geometry, 0, 0), STATUS_INVALID_PARAMETER);
    Bad = Input;
    Bad.Heap = 0x9000000000ULL;
    ok_hex(AppleAgxEncodeVertexWork(AgxVertexWork, &Bad, &Geometry, 0, 0), STATUS_INVALID_PARAMETER);
    Bad = Input;
    Bad.Vdm = 0x8000000000ULL;
    ok_hex(AppleAgxEncodeVertexWork(AgxVertexWork, &Bad, &Geometry, 0, 0), STATUS_INVALID_PARAMETER);
    Bad = Input;
    Bad.Vdm = 0x1002;
    ok_hex(AppleAgxEncodeVertexWork(AgxVertexWork, &Bad, &Geometry, 0, 0), STATUS_INVALID_PARAMETER);
    Bad = Input;
    Bad.Vdm = 0;
    ok_hex(AppleAgxEncodeVertexWork(AgxVertexWork, &Bad, &Geometry, 0, 0), STATUS_SUCCESS);
    Bad = Input;
    Bad.WorkClient = 0x0000001500000002ULL;
    ok_hex(AppleAgxEncodeVertexWork(AgxVertexWork, &Bad, &Geometry, 0, 0), STATUS_INVALID_PARAMETER);
    Bad = Input;
    Bad.WorkClient = (1ULL << 42) - 0x900;
    ok_hex(AppleAgxEncodeVertexWork(AgxVertexWork, &Bad, &Geometry, 0, 0), STATUS_INVALID_PARAMETER);
    Bad = Input;
    Bad.PageCount = 0;
    ok_hex(AppleAgxEncodeVertexWork(AgxVertexWork, &Bad, &Geometry, 0, 0), STATUS_INVALID_PARAMETER);
    Bad = Input;
    Bad.Preempt[1] = 0;
    ok_hex(AppleAgxEncodeVertexWork(AgxVertexWork, &Bad, &Geometry, 0, 0), STATUS_INVALID_PARAMETER);
}

static UCHAR AgxFragmentWork[APPLE_AGX_FRAGMENT_WORK_SIZE];

static
ULONG64
FragRegister(
    _In_ ULONG Index)
{
    return BufQuad(AgxFragmentWork, 0x80 + 12 * Index + 4);
}

static
VOID
TestAgxFragmentWork(VOID)
{
    APPLE_AGX_GEOMETRY Geometry;
    APPLE_AGX_FRAGMENT_INPUT Input, Bad;
    const ULONG Parameters = 0x790, Meta = 0xbac, Tail = 0xbf0;

    ok_hex(AppleAgxDescribeGeometry(1920, 1080, 32, 32, 1, 1, 2, &Geometry), STATUS_SUCCESS);
    ok_hex(Geometry.IspBlocks, 0x10000c);
    RtlZeroMemory(&Input, sizeof(Input));
    Input.Notifier = 0xfffffc2100018000ULL;
    Input.Manager = 0xfffffc2100024000ULL;
    Input.Scene = 0xfffffc2100030000ULL;
    Input.Empty = 0xfffffc2100034000ULL;
    Input.Sequence = 0xfffffc2140020000ULL;
    Input.Pool = 0xfffffc2100040000ULL;
    Input.Stamp = 0xfffffc210001c000ULL;
    Input.FirmwareStamp = 0xfffffc210001c008ULL;
    Input.Timestamps[0] = 0xfffffc2100050010ULL;
    Input.Timestamps[1] = 0xfffffc2100050018ULL;
    Input.UserTimestamps[0] = 0xfffffc2100060010ULL;
    Input.UserTimestamps[1] = 0xfffffc2100060018ULL;
    Input.WorkClient = 0x0000001500001000ULL;
    Input.Tilemap = 0x1100000000ULL;
    Input.Heap = 0x1300000000ULL;
    Input.Auxiliary = 0x1500000000ULL;
    Input.SceneUser = 0x1400000008ULL;
    Input.Scratch = 0x1600000000ULL;
    Input.State.Multisample = 0x22;
    Input.State.MergeUpper[0] = 0x3f800000;
    Input.State.MergeUpper[1] = 0x40000000;
    Input.State.EndOfTile.Address = 0x1700000100ULL;
    Input.State.EndOfTile.Resources = 0x1700000200ULL;
    Input.State.Background.Address = 0x1700000300ULL;
    Input.State.Background.Resources = 0x1700000400ULL;
    Input.State.PartialEndOfTile.Address = 0x1700000500ULL;
    Input.State.PartialEndOfTile.Resources = 0x123456789ULL;
    Input.State.PartialBackground.Address = 0x1700000700ULL;
    Input.State.PartialBackground.Resources = 0x1700000800ULL;
    Input.State.Scissor = 0x1800000000ULL;
    Input.State.DepthBias = 0x1800001000ULL;
    Input.State.Query = 0x180000201fULL;
    Input.State.IspControl = 0x11;
    Input.State.TilebufferControl = 0x21;
    Input.State.MirrorTilebufferControl = 0x22;
    Input.State.TileMode = 0x31;
    Input.State.MirrorTileMode = 0x32;
    Input.State.Depth.Base = 0x1900000000ULL;
    Input.State.Depth.Stride = 0x2000;
    Input.State.Stencil.Base = 0x1a00000000ULL;
    Input.State.Stencil.Stride = 0x1000;
    Input.State.DepthCompression.Base = 0x1b00000000ULL;
    Input.State.DepthCompression.Stride = 0x400;
    Input.State.StencilCompression.Base = 0x1c00000000ULL;
    Input.State.StencilCompression.Stride = 0x200;
    Input.State.ZlsControl = 0x41;
    Input.State.DepthDimensions = 0x04370777;
    Input.State.DepthClear = 0x3f800000;
    Input.State.StencilClear = 0x80;
    Input.State.SampleSize = 8;
    Input.State.ProcessEmptyTiles = TRUE;

    RtlFillMemory(AgxFragmentWork, sizeof(AgxFragmentWork), 0x5a);
    ok_hex(AppleAgxEncodeFragmentWork(AgxFragmentWork, &Input, &Geometry, 13, 0x77), STATUS_SUCCESS);
    ok_int(BufLong(AgxFragmentWork, 0x000), 1);
    ok(BufQuad(AgxFragmentWork, 0x004) == 0, "The work counter is %I64x\n", BufQuad(AgxFragmentWork, 4));
    ok_int(BufLong(AgxFragmentWork, 0x00c), 13);
    ok(BufQuad(AgxFragmentWork, 0x014) == Input.Sequence, "The sequence is %I64x\n", BufQuad(AgxFragmentWork, 0x14));
    ok_hex(BufLong(AgxFragmentWork, 0x01c), 0x310);
    ok(BufQuad(AgxFragmentWork, 0x020) == Input.Notifier, "The notifier is %I64x\n", BufQuad(AgxFragmentWork, 0x20));
    ok(BufQuad(AgxFragmentWork, 0x038) == Input.Empty, "The empty object is %I64x\n", BufQuad(AgxFragmentWork, 0x38));
    ok(BufQuad(AgxFragmentWork, 0x040) == Input.Tilemap, "The tilemap is %I64x\n", BufQuad(AgxFragmentWork, 0x40));
    ok(BufQuad(AgxFragmentWork, 0x048) == 0x88, "The header constant is %I64x\n", BufQuad(AgxFragmentWork, 0x48));
    ok_int(BufLong(AgxFragmentWork, 0x050), 1);
    ok_int(BufWord(AgxFragmentWork, 0x054), 12);
    ok_int(BufWord(AgxFragmentWork, 0x056), 16);
    ok_hex(BufLong(AgxFragmentWork, 0x068), 0x3f800000);
    ok_hex(BufLong(AgxFragmentWork, 0x06c), 0x40000000);
    ok(BufQuad(AgxFragmentWork, 0x078) == 60 * 34, "The frame has %I64u tiles\n", BufQuad(AgxFragmentWork, 0x78));
    ok_hex(BufLong(AgxFragmentWork, 0x80), 0x1739);
    ok(FragRegister(1) == 0xa000, "The utile configuration is %I64x\n", FragRegister(1));
    ok(FragRegister(2) == Input.State.EndOfTile.Resources, "The end of tile resources are %I64x\n", FragRegister(2));
    ok(FragRegister(13) == 0x10000c, "The ISP macrotile size is %I64x\n", FragRegister(13));
    ok(FragRegister(17) == Input.Heap, "The heap is %I64x\n", FragRegister(17));
    ok(FragRegister(21) == 0x43800000780ULL, "The frame size is %I64x\n", FragRegister(21));
    ok(FragRegister(27) == 0x1800002010ULL, "The query is %I64x\n", FragRegister(27));
    ok(FragRegister(51) == Input.Tilemap, "The tilemap register is %I64x\n", FragRegister(51));
    ok(FragRegister(52) == Input.Heap, "The layer metadata is %I64x\n", FragRegister(52));
    ok(FragRegister(53) == 0x3c0000000ULL, "The region stride register is %I64x\n", FragRegister(53));
    ok(FragRegister(59) == Input.Auxiliary, "The auxiliary buffer is %I64x\n", FragRegister(59));
    ok(FragRegister(64) == 0x423ba2000036011ULL, "The tile configuration is %I64x\n", FragRegister(64));
    ok(FragRegister(76) == 0x400000040ULL, "The scene user register is %I64x\n", FragRegister(76));
    ok_hex(BufLong(AgxFragmentWork, 0x80 + 12 * 78), 0xd429);
    ok(BufQuad(AgxFragmentWork, 0x780) == Input.WorkClient + 0x80, "The register list is at %I64x\n",
       BufQuad(AgxFragmentWork, 0x780));
    ok_int(BufWord(AgxFragmentWork, 0x788), 79);
    ok_int(BufWord(AgxFragmentWork, 0x78a), 79 * 12);
    ok(BufQuad(AgxFragmentWork, Parameters) == Input.State.DepthBias, "The depth bias is %I64x\n",
       BufQuad(AgxFragmentWork, Parameters));
    ok(BufQuad(AgxFragmentWork, Parameters + 0x20) == Input.State.Query, "The mirrored query is %I64x\n",
       BufQuad(AgxFragmentWork, Parameters + 0x20));
    ok(BufQuad(AgxFragmentWork, Parameters + 0x1a0) == Input.State.PartialBackground.Address,
       "The partial background is %I64x\n", BufQuad(AgxFragmentWork, Parameters + 0x1a0));
    ok_hex(BufLong(AgxFragmentWork, Parameters + 0x1b0), 0x04040404);
    ok(BufQuad(AgxFragmentWork, Parameters + 0x1e0) == Input.State.DepthCompression.Base,
       "The depth compression is %I64x\n", BufQuad(AgxFragmentWork, Parameters + 0x1e0));
    ok(BufQuad(AgxFragmentWork, Parameters + 0x210) == Input.State.StencilCompression.Base,
       "The stencil compression is %I64x\n", BufQuad(AgxFragmentWork, Parameters + 0x210));
    ok_hex(BufLong(AgxFragmentWork, Parameters + 0x228), 0x22);
    ok_int(BufLong(AgxFragmentWork, Parameters + 0x238), 1920);
    ok_int(BufLong(AgxFragmentWork, Parameters + 0x23c), 1080);
    ok_hex(BufLong(AgxFragmentWork, Parameters + 0x248), 0x32);
    ok_hex(BufLong(AgxFragmentWork, Parameters + 0x264), 0x23456789);
    ok(BufQuad(AgxFragmentWork, Parameters + 0x28c) == Input.State.PartialEndOfTile.Address,
       "The partial end of tile is %I64x\n", BufQuad(AgxFragmentWork, Parameters + 0x28c));
    ok_int(BufLong(AgxFragmentWork, Parameters + 0x2a0), 8);
    ok(BufQuad(AgxFragmentWork, Parameters + 0x2c0) == 0x04370777, "The depth size is %I64x\n",
       BufQuad(AgxFragmentWork, Parameters + 0x2c0));
    ok_hex(BufLong(AgxFragmentWork, 0xb88), 0x67);
    ok_hex(BufLong(AgxFragmentWork, 0xb90), 0xffffffff);
    ok_int(BufLong(AgxFragmentWork, 0xb94), 1);
    ok_int(BufLong(AgxFragmentWork, 0xb98), 1);
    ok(BufQuad(AgxFragmentWork, Meta) == Input.Stamp, "The stamp is at %I64x\n", BufQuad(AgxFragmentWork, Meta));
    ok_hex(BufLong(AgxFragmentWork, Meta + 0x10), 0x77);
    ok_int(BufLong(AgxFragmentWork, Meta + 0x14), 1);
    ok_int(BufLong(AgxFragmentWork, Meta + 0x1c), 1);
    ok_hex(BufLong(AgxFragmentWork, Meta + 0x20), 0x3d0000);
    ok_int(BufLong(AgxFragmentWork, 0xbe8), 1);
    ok(BufQuad(AgxFragmentWork, Tail + 0x10) == Input.Timestamps[1], "The second timestamp is at %I64x\n",
       BufQuad(AgxFragmentWork, Tail + 0x10));
    ok(BufQuad(AgxFragmentWork, Tail + 0x47) == Input.Scratch, "The scratch is at %I64x\n",
       BufQuad(AgxFragmentWork, Tail + 0x47));
    ok_int(AgxFragmentWork[Tail + 0x4f], 1);
    ok_int(AgxFragmentWork[APPLE_AGX_FRAGMENT_WORK_SIZE - 1], 0);

    ok_hex(AppleAgxDescribeGeometry(64, 64, 32, 32, 1, 2, 2, &Geometry), STATUS_SUCCESS);
    ok_hex(AppleAgxEncodeFragmentWork(AgxFragmentWork, &Input, &Geometry, 13, 0x77), STATUS_SUCCESS);
    ok(FragRegister(52) == Input.Heap + 0x200, "The layered metadata is %I64x\n", FragRegister(52));
    ok(FragRegister(64) == (0x36011ULL | (1ULL << 32) | (1ULL << 44) | (1ULL << 53) | 0x2000000000ULL | (0xa000ULL << 28)),
       "The layered tile configuration is %I64x\n", FragRegister(64));

    ok_hex(AppleAgxEncodeFragmentWork(AgxFragmentWork, &Input, &Geometry, 64, 0), STATUS_INVALID_PARAMETER);
    Bad = Input;
    Bad.SceneUser = 0x0fffffffc0ULL;
    ok_hex(AppleAgxEncodeFragmentWork(AgxFragmentWork, &Bad, &Geometry, 0, 0), STATUS_SUCCESS);
    Bad.SceneUser = 0x0fffffff00ULL;
    ok_hex(AppleAgxEncodeFragmentWork(AgxFragmentWork, &Bad, &Geometry, 0, 0), STATUS_INVALID_PARAMETER);
    Bad = Input;
    Bad.Heap = 0x1300000002ULL;
    ok_hex(AppleAgxEncodeFragmentWork(AgxFragmentWork, &Bad, &Geometry, 0, 0), STATUS_INVALID_PARAMETER);
    Bad = Input;
    Bad.WorkClient = (1ULL << 42) - 0xc00;
    ok_hex(AppleAgxEncodeFragmentWork(AgxFragmentWork, &Bad, &Geometry, 0, 0), STATUS_INVALID_PARAMETER);
    Bad = Input;
    Bad.Sequence = 0;
    ok_hex(AppleAgxEncodeFragmentWork(AgxFragmentWork, &Bad, &Geometry, 0, 0), STATUS_INVALID_PARAMETER);
}

static
VOID
TestAgxScene(VOID)
{
    UCHAR Scene[APPLE_AGX_SCENE_SIZE + 8];

    RtlFillMemory(Scene, sizeof(Scene), 0x5a);
    ok_hex(AppleAgxDescribeScene(Scene, 0xfffffc2100024060ULL, 0xfffffc2100025000ULL, 0x1040000000ULL, 0x1000),
           STATUS_SUCCESS);
    ok(BufQuad(Scene, 0x00) == 0xfffffc2100024060ULL, "The scene list entry is %I64x\n", BufQuad(Scene, 0));
    ok(BufQuad(Scene, 0x08) == 0xfffffc2100024060ULL, "The mirrored entry is %I64x\n", BufQuad(Scene, 8));
    ok(BufQuad(Scene, 0x10) == 0, "The scene holds %I64x after its entries\n", BufQuad(Scene, 0x10));
    ok(BufQuad(Scene, 0x28) == 0x40000040ULL, "The user header is %I64x\n", BufQuad(Scene, 0x28));
    ok_hex(BufLong(Scene, 0x30), 0x40000900);
    ok(BufQuad(Scene, 0x40) == 0xfffffc2100025000ULL, "The manager state is %I64x\n", BufQuad(Scene, 0x40));
    ok(BufQuad(Scene, 0x78) == 0, "The scene ends with %I64x\n", BufQuad(Scene, 0x78));
    ok_int(Scene[APPLE_AGX_SCENE_SIZE], 0x5a);

    ok_hex(AppleAgxDescribeScene(Scene, 0xfffffc2100024060ULL, 0xfffffc2100025000ULL, 0x1000000000ULL, 0x904),
           STATUS_SUCCESS);
    ok(BufQuad(Scene, 0x28) == 0x40, "The user header is %I64x\n", BufQuad(Scene, 0x28));
    ok_hex(AppleAgxDescribeScene(Scene, 0xfffffc2100024060ULL, 0xfffffc2100025000ULL, 0x1000000000ULL, 0x903),
           STATUS_INVALID_PARAMETER);
    ok_hex(AppleAgxDescribeScene(Scene, 0xfffffc2100024060ULL, 0xfffffc2100025000ULL, 0x0fffffffc0ULL, 0x1000),
           STATUS_INVALID_PARAMETER);
    ok_hex(AppleAgxDescribeScene(Scene, 0xfffffc2100024060ULL, 0xfffffc2100025000ULL, 0x1400000020ULL, 0x1000),
           STATUS_INVALID_PARAMETER);
    ok_hex(AppleAgxDescribeScene(Scene, 0xfffffc2100024060ULL, 0xfffffc2100025000ULL, 0x10fffff800ULL, 0x1000),
           STATUS_INVALID_PARAMETER);
    ok_hex(AppleAgxDescribeScene(Scene, 0xfffffc2100024060ULL, 0xfffffc2100025000ULL, 0x1400000000ULL, 0x1000),
           STATUS_INVALID_PARAMETER);
    ok_hex(AppleAgxDescribeScene(Scene, 0, 0xfffffc2100025000ULL, 0x1040000000ULL, 0x1000), STATUS_INVALID_PARAMETER);
}

static
VOID
TestAgxTranslateRender(VOID)
{
    DRM_ASAHI_CMD_RENDER Command, Bad;
    APPLE_AGX_RENDER_STATE Render;
    const ULONG64 Usc = 0x1d00000000ULL;

    ok_int(sizeof(DRM_ASAHI_CMD_RENDER), 0xf0);
    RtlZeroMemory(&Command, sizeof(Command));
    Command.Flags = DRM_ASAHI_RENDER_DBIAS_IS_INT;
    Command.IspZlsPixels = 0x04370777;
    Command.VdmCtrlStreamBase = 0x1040001000ULL;
    Command.IspScissorBase = 0x1800000000ULL;
    Command.IspDbiasBase = 0x1800001000ULL;
    Command.IspOclqryBase = 0x1800002000ULL;
    Command.Depth.Base = 0x1900000000ULL;
    Command.Depth.CompBase = 0x1b00000000ULL;
    Command.Depth.Stride = 0x2000;
    Command.Depth.CompStride = 0x400;
    Command.Stencil.Base = 0x1a00000000ULL;
    Command.Stencil.CompBase = 0x1c00000000ULL;
    Command.Stencil.Stride = 0x1000;
    Command.Stencil.CompStride = 0x200;
    Command.ZlsCtrl = 0x41;
    Command.PppMultisamplectl = 0x22;
    Command.PppCtrl = 0x201;
    Command.WidthPx = 1920;
    Command.HeightPx = 1080;
    Command.Layers = 1;
    Command.UtileWidthPx = 32;
    Command.UtileHeightPx = 32;
    Command.Samples = 1;
    Command.SampleSizeB = 8;
    Command.IspMergeUpperX = 0x3f800000;
    Command.IspMergeUpperY = 0x40000000;
    Command.Bg.Usc = 0x1047;
    Command.Bg.RsrcSpec = 0x11;
    Command.Eot.Usc = 0x2000;
    Command.Eot.RsrcSpec = 0x22;
    Command.PartialBg.Usc = 0x3010;
    Command.PartialBg.RsrcSpec = 0x33;
    Command.PartialEot.Usc = 0x4000;
    Command.PartialEot.RsrcSpec = 0x44;
    Command.IspBgobjdepth = 0x3f800000;
    Command.IspBgobjvals = 0x80;

    ok_hex(AppleAgxTranslateRender(&Command, Usc, 2, &Render), STATUS_SUCCESS);
    ok_int(Render.Geometry.Blocks, 192);
    ok(Render.Geometry.TpcBytes == 0xc000, "The TPC takes %I64x bytes\n", Render.Geometry.TpcBytes);
    ok(Render.Vdm == 0x40001000ULL, "The VDM stream is %I64x\n", Render.Vdm);
    ok(Render.Multisample == 0x22, "The multisample control is %I64x\n", Render.Multisample);
    ok_hex(Render.Ppp, 0x201);
    ok_int(Render.ProcessEmptyTiles, FALSE);
    ok(Render.Fragment.Background.Address == Usc + 0x1040, "The background program is at %I64x\n",
       Render.Fragment.Background.Address);
    ok(Render.Fragment.Background.Resources == 0x11, "The background resources are %I64x\n",
       Render.Fragment.Background.Resources);
    ok(Render.Fragment.EndOfTile.Address == Usc + 0x2000, "The end of tile program is at %I64x\n",
       Render.Fragment.EndOfTile.Address);
    ok(Render.Fragment.PartialBackground.Address == Usc + 0x3000, "The partial background is at %I64x\n",
       Render.Fragment.PartialBackground.Address);
    ok(Render.Fragment.PartialEndOfTile.Resources == 0x44, "The partial end of tile resources are %I64x\n",
       Render.Fragment.PartialEndOfTile.Resources);
    ok_hex(Render.Fragment.IspControl, 0x4c000);
    ok_hex(Render.Fragment.TilebufferControl, 4);
    ok_hex(Render.Fragment.MirrorTilebufferControl, 4);
    ok(Render.Fragment.TileMode == 0x280, "The tile mode is %I64x\n", Render.Fragment.TileMode);
    ok_hex(Render.Fragment.MirrorTileMode, 0x280);
    ok(Render.Fragment.Depth.Stride == 0x2000, "The depth stride is %I64x\n", Render.Fragment.Depth.Stride);
    ok(Render.Fragment.DepthCompression.Base == 0x1b00000000ULL, "The depth compression is at %I64x\n",
       Render.Fragment.DepthCompression.Base);
    ok(Render.Fragment.StencilCompression.Stride == 0x200, "The stencil compression stride is %I64x\n",
       Render.Fragment.StencilCompression.Stride);
    ok(Render.Fragment.Query == 0x1800002000ULL, "The query is at %I64x\n", Render.Fragment.Query);
    ok_hex(Render.Fragment.DepthDimensions, 0x04370777);
    ok_hex(Render.Fragment.DepthClear, 0x3f800000);
    ok_hex(Render.Fragment.StencilClear, 0x80);
    ok_int(Render.Fragment.SampleSize, 8);
    ok_hex(Render.Fragment.MergeUpper[1], 0x40000000);

    Bad = Command;
    Bad.Flags = DRM_ASAHI_RENDER_PROCESS_EMPTY_TILES;
    Bad.Layers = 3;
    Bad.Samples = 4;
    Bad.UtileWidthPx = 16;
    Bad.UtileHeightPx = 16;
    Bad.SampleSizeB = 16;
    ok_hex(AppleAgxTranslateRender(&Bad, Usc, 2, &Render), STATUS_SUCCESS);
    ok_hex(Render.Fragment.TilebufferControl, 0x40008);
    ok(Render.Fragment.TileMode == 0x10281, "The layered tile mode is %I64x\n", Render.Fragment.TileMode);
    ok_hex(Render.Fragment.IspControl, 0xc000);
    ok_int(Render.ProcessEmptyTiles, TRUE);
    ok_int(Render.Fragment.ProcessEmptyTiles, TRUE);

    Bad = Command;
    Bad.Flags = 0x8;
    ok_hex(AppleAgxTranslateRender(&Bad, Usc, 2, &Render), STATUS_INVALID_PARAMETER);
    Bad = Command;
    Bad.VdmCtrlStreamBase = 0x0040001000ULL;
    ok_hex(AppleAgxTranslateRender(&Bad, Usc, 2, &Render), STATUS_INVALID_PARAMETER);
    Bad.VdmCtrlStreamBase = 0x1040001002ULL;
    ok_hex(AppleAgxTranslateRender(&Bad, Usc, 2, &Render), STATUS_INVALID_PARAMETER);
    Bad = Command;
    Bad.WidthPx = 0;
    ok_hex(AppleAgxTranslateRender(&Bad, Usc, 2, &Render), STATUS_INVALID_PARAMETER);
    Bad = Command;
    Bad.Samples = 8;
    ok_hex(AppleAgxTranslateRender(&Bad, Usc, 2, &Render), STATUS_INVALID_PARAMETER);
    ok_hex(AppleAgxTranslateRender(&Command, 0, 2, &Render), STATUS_INVALID_PARAMETER);
}

static UCHAR AgxJobSequence[2][APPLE_AGX_SEQUENCE_STORAGE];
static UCHAR AgxJobScene[APPLE_AGX_SCENE_SIZE];
static UCHAR AgxJobBarrier[APPLE_AGX_BARRIER_SIZE];

static
VOID
TestAgxBuildRender(VOID)
{
    DRM_ASAHI_CMD_RENDER Command;
    APPLE_AGX_RENDER_STATE Render;
    APPLE_AGX_RENDER_JOB Job, Bad;
    const ULONG VertexStamp = 0x7a000500, FragmentStamp = 0x3d000500;

    ok_hex(AppleAgxStamp(APPLE_AGX_SEED_VERTEX, 0), 0x7a000000);
    ok_hex(AppleAgxStamp(APPLE_AGX_SEED_FRAGMENT, 3), 0x3d000300);
    ok_hex(AppleAgxStamp(APPLE_AGX_SEED_VERTEX, 0x860000), 0);
    ok_hex(AppleAgxStamp(APPLE_AGX_SEED_COMPUTE, 0x1000000), 0xc1000000);

    RtlFillMemory(AgxJobBarrier, sizeof(AgxJobBarrier), 0x5a);
    ok_hex(AppleAgxEncodeBarrier(AgxJobBarrier, 0xfffffc210001c010ULL, 0x1234, APPLE_AGX_EVENT_COMPUTE, 0x5678,
                                 APPLE_AGX_RENDER_WAIT_UUID, TRUE), STATUS_SUCCESS);
    ok_hex(BufLong(AgxJobBarrier, 0x00), 4);
    ok(BufQuad(AgxJobBarrier, 0x04) == 0xfffffc210001c010ULL, "The barrier waits on %I64x\n", BufQuad(AgxJobBarrier, 4));
    ok(BufQuad(AgxJobBarrier, 0x0c) == 0xfffffc210001c010ULL, "The barrier repeats %I64x\n", BufQuad(AgxJobBarrier, 0xc));
    ok_hex(BufLong(AgxJobBarrier, 0x14), 0x1234);
    ok_int(BufLong(AgxJobBarrier, 0x18), 2);
    ok_hex(BufLong(AgxJobBarrier, 0x1c), 0x5678);
    ok_hex(BufLong(AgxJobBarrier, 0x20), 0x3d420);
    ok_int(BufLong(AgxJobBarrier, 0x2a), 1);
    ok_int(BufWord(AgxJobBarrier, 0x3c), 0);
    ok_hex(AppleAgxEncodeBarrier(AgxJobBarrier, 0, 0, 0, 0, 0, FALSE), STATUS_INVALID_PARAMETER);
    ok_hex(AppleAgxEncodeBarrier(AgxJobBarrier, 0xfffffc210001c010ULL, 0, 3, 0, 0, FALSE), STATUS_INVALID_PARAMETER);

    RtlZeroMemory(&Command, sizeof(Command));
    Command.VdmCtrlStreamBase = 0x1040001000ULL;
    Command.WidthPx = 1920;
    Command.HeightPx = 1080;
    Command.Layers = 1;
    Command.UtileWidthPx = 32;
    Command.UtileHeightPx = 32;
    Command.Samples = 1;
    Command.SampleSizeB = 8;
    ok_hex(AppleAgxTranslateRender(&Command, 0x1d00000000ULL, 2, &Render), STATUS_SUCCESS);

    RtlZeroMemory(&Job, sizeof(Job));
    Job.VertexWork = AgxVertexWork;
    Job.VertexSequence = AgxJobSequence[0];
    Job.FragmentWork = AgxFragmentWork;
    Job.FragmentSequence = AgxJobSequence[1];
    Job.Scene = AgxJobScene;
    Job.FragmentBarrier = AgxJobBarrier;
    Job.VertexWorkAddress = 0xfffffc2140000000ULL;
    Job.VertexWorkClient = 0x0000001500000000ULL;
    Job.VertexSequenceAddress = 0xfffffc2140010000ULL;
    Job.FragmentWorkAddress = 0xfffffc2140020000ULL;
    Job.FragmentWorkClient = 0x0000001500001000ULL;
    Job.FragmentSequenceAddress = 0xfffffc2140030000ULL;
    Job.SceneAddress = 0xfffffc2140040000ULL;
    Job.Hwdata = 0xfffffc20406f0000ULL;
    Job.Notifier = 0xfffffc2100018000ULL;
    Job.Empty = 0xfffffc2100034000ULL;
    Job.Manager = 0xfffffc2100024000ULL;
    Job.ManagerEntry = 0xfffffc2100024c00ULL;
    Job.ManagerMisc = 0xfffffc2100025000ULL;
    Job.Pool = 0xfffffc2100040000ULL;
    Job.Queue[0] = 0xfffffc2100000000ULL;
    Job.Queue[1] = 0xfffffc2100000100ULL;
    Job.UserStamp[0] = 0xfffffc210001c000ULL;
    Job.UserStamp[1] = 0xfffffc210001c008ULL;
    Job.FirmwareStamp[0] = 0xfffffc210001c010ULL;
    Job.FirmwareStamp[1] = 0xfffffc210001c018ULL;
    Job.Timestamps[0][0] = 0xfffffc2100050000ULL;
    Job.Timestamps[0][1] = 0xfffffc2100050008ULL;
    Job.Timestamps[1][0] = 0xfffffc2100050010ULL;
    Job.Timestamps[1][1] = 0xfffffc2100050018ULL;
    Job.VertexScratch = 0xfffffc2140100000ULL;
    Job.FragmentScratch = 0x1600000000ULL;
    Job.FragmentScratchBytes = 0x10000;
    Job.Tilemap = 0x1100000000ULL;
    Job.Tpc = 0x1200000000ULL;
    Job.Heap = 0x1300000000ULL;
    Job.Auxiliary = 0x1500000000ULL;
    Job.SceneUser = 0x1040100000ULL;
    Job.SceneUserBytes = 0x1000;
    Job.Preempt[0] = 0x1006080000ULL;
    Job.Preempt[1] = 0x1006098000ULL;
    Job.Preempt[2] = 0x10060b0000ULL;
    Job.PageCount = 0x400;
    Job.QueueCommands = 2;

    ok_hex(AppleAgxBuildRender(&Job, &Render, 5, 5), STATUS_SUCCESS);
    ok(BufQuad(AgxJobScene, 0x00) == Job.ManagerEntry, "The scene names entry %I64x\n", BufQuad(AgxJobScene, 0));
    ok(BufQuad(AgxJobScene, 0x40) == Job.ManagerMisc, "The scene names state %I64x\n", BufQuad(AgxJobScene, 0x40));
    ok(BufQuad(AgxVertexWork, 0x02c) == Job.SceneAddress, "The vertex work names scene %I64x\n", BufQuad(AgxVertexWork, 0x2c));
    ok(BufQuad(AgxVertexWork, 0x770) == Job.VertexSequenceAddress, "The vertex work runs %I64x\n",
       BufQuad(AgxVertexWork, 0x770));
    ok(BufQuad(AgxVertexWork, 0x740) == Job.VertexWorkClient + 0x40, "The vertex registers are at %I64x\n",
       BufQuad(AgxVertexWork, 0x740));
    ok_hex(BufLong(AgxVertexWork, 0x780), VertexStamp);
    ok(BufQuad(AgxVertexWork, 0x784 + 0xe0) == Job.UserStamp[0], "The vertex user stamp is %I64x\n",
       BufQuad(AgxVertexWork, 0x784 + 0xe0));
    ok(BufQuad(AgxVertexWork, 0x40 + 12 * 71 + 4) == (0xa000000000ULL | (Job.ManagerEntry & 0xfffffffffULL) | 1),
       "The initial scene entry is %I64x\n", BufQuad(AgxVertexWork, 0x40 + 12 * 71 + 4));
    ok(BufQuad(AgxVertexWork, 0x8a8 + 0x08) == Job.Timestamps[0][0], "The vertex timestamp is %I64x\n",
       BufQuad(AgxVertexWork, 0x8a8 + 0x08));
    ok(BufQuad(AgxJobSequence[0], 0x14) == Job.VertexWorkAddress + 0x40, "The vertex sequence reads %I64x\n",
       BufQuad(AgxJobSequence[0], 0x14));
    ok(BufQuad(AgxJobSequence[0], 0x34) == Job.Queue[0], "The vertex sequence queue is %I64x\n",
       BufQuad(AgxJobSequence[0], 0x34));
    ok(BufQuad(AgxJobSequence[0], 0x248 + 0x44) == Job.FirmwareStamp[0], "FinalizeTA stamps %I64x\n",
       BufQuad(AgxJobSequence[0], 0x248 + 0x44));
    ok_hex(BufLong(AgxJobSequence[0], 0x248 + 0x4c), VertexStamp);
    ok(BufQuad(AgxFragmentWork, 0x014) == Job.FragmentSequenceAddress, "The fragment work runs %I64x\n",
       BufQuad(AgxFragmentWork, 0x14));
    ok(BufQuad(AgxFragmentWork, 0x780) == Job.FragmentWorkClient + 0x80, "The fragment registers are at %I64x\n",
       BufQuad(AgxFragmentWork, 0x780));
    ok_hex(BufLong(AgxFragmentWork, 0xbac + 0x10), FragmentStamp);
    ok(BufQuad(AgxFragmentWork, 0xbac + 0x08) == Job.FirmwareStamp[1], "The fragment firmware stamp is %I64x\n",
       BufQuad(AgxFragmentWork, 0xbac + 0x08));
    ok(BufQuad(AgxFragmentWork, 0xbf0 + 0x10) == Job.Timestamps[1][1], "The fragment timestamp is %I64x\n",
       BufQuad(AgxFragmentWork, 0xbf0 + 0x10));
    ok(BufQuad(AgxJobSequence[1], 0x4c) == Job.FragmentWorkAddress, "The fragment sequence runs %I64x\n",
       BufQuad(AgxJobSequence[1], 0x4c));
    ok(BufQuad(AgxJobSequence[1], 0x44) == Job.Queue[1], "The fragment sequence queue is %I64x\n",
       BufQuad(AgxJobSequence[1], 0x44));
    ok(BufQuad(AgxJobSequence[1], 0x68) == 2, "The fragment queue holds %I64u commands\n", BufQuad(AgxJobSequence[1], 0x68));
    ok_hex(BufLong(AgxJobSequence[1], 0x258 + 0x14), FragmentStamp);
    ok(BufQuad(AgxJobSequence[1], 0x1bc) == Job.FragmentScratch, "The fragment scratch is %I64x\n",
       BufQuad(AgxJobSequence[1], 0x1bc));
    ok(BufQuad(AgxJobBarrier, 0x04) == Job.FirmwareStamp[0], "The fragment waits on %I64x\n", BufQuad(AgxJobBarrier, 4));
    ok_hex(BufLong(AgxJobBarrier, 0x14), VertexStamp);
    ok_int(BufLong(AgxJobBarrier, 0x18), 0);
    ok_hex(BufLong(AgxJobBarrier, 0x1c), FragmentStamp);
    ok_hex(BufLong(AgxJobBarrier, 0x20), 0x3d0000);
    ok_int(BufLong(AgxJobBarrier, 0x2a), 0);

    Bad = Job;
    Bad.FragmentBarrier = NULL;
    ok_hex(AppleAgxBuildRender(&Bad, &Render, 5, 5), STATUS_INVALID_PARAMETER);
    Bad = Job;
    Bad.FragmentScratchBytes = 0x8000;
    ok_hex(AppleAgxBuildRender(&Bad, &Render, 5, 5), STATUS_INVALID_PARAMETER);
    Bad = Job;
    Bad.SceneUser = 0x1400000000ULL;
    ok_hex(AppleAgxBuildRender(&Bad, &Render, 5, 5), STATUS_INVALID_PARAMETER);
    ok_hex(AppleAgxBuildRender(&Job, &Render, 64, 5), STATUS_INVALID_PARAMETER);
}

static
VOID
TestAgxParams(VOID)
{
    DRM_ASAHI_PARAMS_GLOBAL Params;

    ok_int(sizeof(DRM_ASAHI_PARAMS_GLOBAL), 0x250);
    RtlFillMemory(&Params, sizeof(Params), 0x5a);
    ok_hex(AppleAgxDescribeParams(0x07031100, 2, 0xffffeULL, 0x6030, 1398000, &Params), STATUS_SUCCESS);
    ok(Params.Features == 0, "The features are %I64x\n", Params.Features);
    ok_int(Params.GpuGeneration, 15);
    ok_int(Params.GpuVariant, 'S');
    ok_hex(Params.GpuRevision, 0x11);
    ok_hex(Params.ChipId, 0x6030);
    ok_int(Params.NumDies, 1);
    ok_int(Params.NumClustersTotal, 2);
    ok_int(Params.NumCoresPerCluster, 10);
    ok_int(Params.MaxFrequencyKhz, 1398000);
    ok(Params.CoreMasks[0] == 0x3fe, "The first cluster's cores are %I64x\n", Params.CoreMasks[0]);
    ok(Params.CoreMasks[1] == 0x3ff, "The second cluster's cores are %I64x\n", Params.CoreMasks[1]);
    ok(Params.CoreMasks[2] == 0, "A third cluster has cores %I64x\n", Params.CoreMasks[2]);
    ok(Params.CoreMasks[DRM_ASAHI_MAX_CLUSTERS - 1] == 0, "The last cluster has cores %I64x\n",
       Params.CoreMasks[DRM_ASAHI_MAX_CLUSTERS - 1]);
    ok(Params.VmStart == 0x4000, "The user range starts at %I64x\n", Params.VmStart);
    ok(Params.VmEnd == 0x2ffffff8000ULL, "The user range ends at %I64x\n", Params.VmEnd);
    ok(Params.VmKernelMinSize == 0x20000000, "The kernel range takes at least %I64x\n", Params.VmKernelMinSize);
    ok_int(Params.MaxCommandsPerSubmission, 64);
    ok_int(Params.MaxAttachments, 16);
    ok(Params.CommandTimestampFrequencyHz == 1000000000ULL, "Timestamps tick at %I64u Hz\n",
       Params.CommandTimestampFrequencyHz);

    ok_hex(AppleAgxDescribeParams(0x07021100, 1, 0x3ff, 0x8122, 1000000, &Params), STATUS_NOT_SUPPORTED);
    ok_hex(AppleAgxDescribeParams(0x06031100, 2, 0xfffff, 0x6030, 1000000, &Params), STATUS_NOT_SUPPORTED);
    ok_hex(AppleAgxDescribeParams(0x07031100, 0, 0, 0x6030, 1000000, &Params), STATUS_INVALID_PARAMETER);
    ok_hex(AppleAgxDescribeParams(0x07031100, 7, 0, 0x6030, 1000000, &Params), STATUS_INVALID_PARAMETER);
}

static
VOID
TestAgxMemoryCalls(VOID)
{
    DRM_ASAHI_VM_CREATE Vm;
    DRM_ASAHI_GEM_CREATE Gem;
    DRM_ASAHI_GEM_BIND_OP Op, Bad;
    APPLE_AGX_VM_RANGES Ranges;
    APPLE_UAT_PROTECTION Protection;
    ULONG64 Size;

    RtlZeroMemory(&Vm, sizeof(Vm));
    Vm.KernelStart = 0x1000000000ULL;
    Vm.KernelEnd = 0x1040000000ULL;
    ok_hex(AppleAgxDescribeVm(&Vm, &Ranges), STATUS_SUCCESS);
    ok(Ranges.UserStart == 0x4000 && Ranges.UserEnd == 0x2ffffff8000ULL, "The user range is %I64x..%I64x\n",
       Ranges.UserStart, Ranges.UserEnd);
    ok(Ranges.KernelStart == 0x1000000000ULL, "The kernel range starts at %I64x\n", Ranges.KernelStart);
    ok(Ranges.KernelGpuEnd == 0x1020000000ULL, "The GPU-only half ends at %I64x\n", Ranges.KernelGpuEnd);
    ok(Ranges.KernelEnd == 0x1040000000ULL, "The kernel range ends at %I64x\n", Ranges.KernelEnd);
    Vm.KernelStart = 0x1000004000ULL;
    Vm.KernelEnd = 0x102000c000ULL;
    ok_hex(AppleAgxDescribeVm(&Vm, &Ranges), STATUS_SUCCESS);
    ok(Ranges.KernelStart == 0x1000008000ULL, "The aligned kernel range starts at %I64x\n", Ranges.KernelStart);
    ok(Ranges.KernelEnd == 0x1020008000ULL, "The aligned kernel range ends at %I64x\n", Ranges.KernelEnd);
    ok(Ranges.KernelGpuEnd == 0x1010008000ULL, "The GPU-only half ends at %I64x\n", Ranges.KernelGpuEnd);
    Vm.KernelStart = 0x1000000000ULL;
    Vm.KernelEnd = 0x101fffc000ULL;
    ok_hex(AppleAgxDescribeVm(&Vm, &Ranges), STATUS_INVALID_PARAMETER);
    Vm.KernelEnd = 0x1040002000ULL;
    ok_hex(AppleAgxDescribeVm(&Vm, &Ranges), STATUS_INVALID_PARAMETER);
    Vm.KernelStart = 0;
    Vm.KernelEnd = 0x40000000ULL;
    ok_hex(AppleAgxDescribeVm(&Vm, &Ranges), STATUS_INVALID_PARAMETER);
    Vm.KernelStart = 0x2ffe0000000ULL;
    Vm.KernelEnd = 0x30000000000ULL;
    ok_hex(AppleAgxDescribeVm(&Vm, &Ranges), STATUS_INVALID_PARAMETER);
    Vm.KernelStart = 0x1000000000ULL;
    Vm.KernelEnd = 0x1040000000ULL;
    Vm.Pad = 1;
    ok_hex(AppleAgxDescribeVm(&Vm, &Ranges), STATUS_INVALID_PARAMETER);
    Vm.Pad = 0;
    ok_hex(AppleAgxDescribeVm(&Vm, &Ranges), STATUS_SUCCESS);

    RtlZeroMemory(&Gem, sizeof(Gem));
    Gem.Size = 0x5001;
    ok_hex(AppleAgxGemSize(&Gem, &Size), STATUS_SUCCESS);
    ok(Size == 0x8000, "The object takes %I64x bytes\n", Size);
    Gem.Flags = DRM_ASAHI_GEM_WRITEBACK | DRM_ASAHI_GEM_VM_PRIVATE;
    Gem.VmId = 3;
    ok_hex(AppleAgxGemSize(&Gem, &Size), STATUS_SUCCESS);
    Gem.Flags = DRM_ASAHI_GEM_WRITEBACK;
    ok_hex(AppleAgxGemSize(&Gem, &Size), STATUS_INVALID_PARAMETER);
    Gem.VmId = 0;
    Gem.Flags = 4;
    ok_hex(AppleAgxGemSize(&Gem, &Size), STATUS_INVALID_PARAMETER);
    Gem.Flags = 0;
    Gem.Size = 0;
    ok_hex(AppleAgxGemSize(&Gem, &Size), STATUS_INVALID_PARAMETER);
    Gem.Size = MAXULONG64;
    ok_hex(AppleAgxGemSize(&Gem, &Size), STATUS_INVALID_PARAMETER);

    RtlZeroMemory(&Op, sizeof(Op));
    Op.Flags = DRM_ASAHI_BIND_READ | DRM_ASAHI_BIND_WRITE;
    Op.Handle = 1;
    Op.Offset = 0x4000;
    Op.Range = 0x8000;
    Op.Addr = 0x2000000000ULL;
    ok_hex(AppleAgxCheckBind(&Op, &Ranges, 0xc000, &Protection), STATUS_SUCCESS);
    ok_int(Protection.Access, APPLE_UAT_ACCESS_GPU);
    ok_int(Protection.Memory, APPLE_UAT_MEMORY_UNCACHED);
    ok_int(Protection.Read, TRUE);
    ok_int(Protection.Write, TRUE);
    ok_hex(AppleAgxCheckBind(&Op, &Ranges, 0x8000, &Protection), STATUS_INVALID_PARAMETER);
    Bad = Op;
    Bad.Flags = DRM_ASAHI_BIND_READ;
    ok_hex(AppleAgxCheckBind(&Bad, &Ranges, 0xc000, &Protection), STATUS_SUCCESS);
    ok_int(Protection.Write, FALSE);
    Bad.Flags = DRM_ASAHI_BIND_WRITE;
    ok_hex(AppleAgxCheckBind(&Bad, &Ranges, 0xc000, &Protection), STATUS_SUCCESS);
    ok_int(Protection.Read, FALSE);
    Bad.Flags = DRM_ASAHI_BIND_SINGLE_PAGE;
    ok_hex(AppleAgxCheckBind(&Bad, &Ranges, 0xc000, &Protection), STATUS_INVALID_PARAMETER);
    Bad.Flags = DRM_ASAHI_BIND_READ | DRM_ASAHI_BIND_SINGLE_PAGE;
    Bad.Range = 0x100000;
    ok_hex(AppleAgxCheckBind(&Bad, &Ranges, 0x8000, &Protection), STATUS_SUCCESS);
    Bad.Flags = DRM_ASAHI_BIND_READ | 0x10;
    ok_hex(AppleAgxCheckBind(&Bad, &Ranges, 0x100000, &Protection), STATUS_INVALID_PARAMETER);
    Bad = Op;
    Bad.Addr = 0x2000002000ULL;
    ok_hex(AppleAgxCheckBind(&Bad, &Ranges, 0xc000, &Protection), STATUS_INVALID_PARAMETER);
    Bad = Op;
    Bad.Range = 0;
    ok_hex(AppleAgxCheckBind(&Bad, &Ranges, 0xc000, &Protection), STATUS_INVALID_PARAMETER);
    Bad = Op;
    Bad.Addr = 0x103fffc000ULL;
    ok_hex(AppleAgxCheckBind(&Bad, &Ranges, 0xc000, &Protection), STATUS_INVALID_PARAMETER);
    Bad.Addr = 0x0fffffc000ULL;
    ok_hex(AppleAgxCheckBind(&Bad, &Ranges, 0xc000, &Protection), STATUS_INVALID_PARAMETER);
    Bad.Addr = 0x0fffff8000ULL;
    ok_hex(AppleAgxCheckBind(&Bad, &Ranges, 0xc000, &Protection), STATUS_SUCCESS);
    Bad.Addr = 0x2ffffff4000ULL;
    ok_hex(AppleAgxCheckBind(&Bad, &Ranges, 0xc000, &Protection), STATUS_INVALID_PARAMETER);
    Bad.Addr = 0;
    ok_hex(AppleAgxCheckBind(&Bad, &Ranges, 0xc000, &Protection), STATUS_INVALID_PARAMETER);

    RtlZeroMemory(&Bad, sizeof(Bad));
    Bad.Flags = DRM_ASAHI_BIND_UNBIND;
    Bad.Range = 0x8000;
    Bad.Addr = 0x2000000000ULL;
    ok_hex(AppleAgxCheckBind(&Bad, &Ranges, 0, &Protection), STATUS_SUCCESS);
    Bad.Handle = 1;
    ok_hex(AppleAgxCheckBind(&Bad, &Ranges, 0, &Protection), STATUS_INVALID_PARAMETER);
    Bad.Handle = 0;
    Bad.Offset = 0x4000;
    ok_hex(AppleAgxCheckBind(&Bad, &Ranges, 0, &Protection), STATUS_INVALID_PARAMETER);
    Bad.Offset = 0;
    Bad.Flags = DRM_ASAHI_BIND_UNBIND | DRM_ASAHI_BIND_READ;
    ok_hex(AppleAgxCheckBind(&Bad, &Ranges, 0, &Protection), STATUS_INVALID_PARAMETER);
}

static LONG AgxObjectReferences[2];

static
VOID
NTAPI
AgxCountReference(
    _In_ PVOID Object,
    _In_ LONG Delta)
{
    *(PLONG)Object += Delta;
}

static
VOID
TestAgxBindings(VOID)
{
    APPLE_AGX_BINDING Entry[4], New;
    APPLE_AGX_BINDINGS Bindings;
    const APPLE_AGX_BINDING *Found;

    Bindings.Entry = Entry;
    Bindings.Count = 0;
    Bindings.Capacity = 4;
    Bindings.Reference = AgxCountReference;
    AgxObjectReferences[0] = AgxObjectReferences[1] = 0;

    RtlZeroMemory(&New, sizeof(New));
    New.Address = 0x100000;
    New.Range = 0x40000;
    New.Offset = 0x8000;
    New.Object = &AgxObjectReferences[0];
    ok_hex(AppleAgxBindingInsert(&Bindings, &New), STATUS_SUCCESS);
    ok_int(AgxObjectReferences[0], 1);
    New.Address = 0x13c000;
    New.Range = 0x4000;
    ok_hex(AppleAgxBindingInsert(&Bindings, &New), STATUS_CONFLICTING_ADDRESSES);
    New.Address = 0xfc000;
    New.Range = 0x8000;
    ok_hex(AppleAgxBindingInsert(&Bindings, &New), STATUS_CONFLICTING_ADDRESSES);
    New.Address = 0x140000;
    New.Range = 0x10000;
    New.Offset = 0;
    New.Object = &AgxObjectReferences[1];
    New.SinglePage = TRUE;
    ok_hex(AppleAgxBindingInsert(&Bindings, &New), STATUS_SUCCESS);
    ok_int(Bindings.Count, 2);

    ok_hex(AppleAgxBindingRemove(&Bindings, 0x110000, 0x8000), STATUS_SUCCESS);
    ok_int(Bindings.Count, 3);
    ok_int(AgxObjectReferences[0], 2);
    Found = AppleAgxBindingFind(&Bindings, 0x100000);
    ok(Found && Found->Address == 0x100000 && Found->Range == 0x10000 && Found->Offset == 0x8000,
       "The head is %I64x+%I64x at offset %I64x\n", Found ? Found->Address : 0, Found ? Found->Range : 0,
       Found ? Found->Offset : 0);
    ok(AppleAgxBindingFind(&Bindings, 0x110000) == NULL, "The removed hole is still bound\n");
    ok(AppleAgxBindingFind(&Bindings, 0x117fff) == NULL, "The end of the hole is still bound\n");
    Found = AppleAgxBindingFind(&Bindings, 0x118000);
    ok(Found && Found->Address == 0x118000 && Found->Range == 0x28000 && Found->Offset == 0x20000,
       "The tail is %I64x+%I64x at offset %I64x\n", Found ? Found->Address : 0, Found ? Found->Range : 0,
       Found ? Found->Offset : 0);

    ok_hex(AppleAgxBindingRemove(&Bindings, 0x130000, 0x14000), STATUS_SUCCESS);
    ok_int(Bindings.Count, 3);
    Found = AppleAgxBindingFind(&Bindings, 0x118000);
    ok(Found && Found->Range == 0x18000, "The trimmed tail ends at %I64x\n", Found ? Found->Address + Found->Range : 0);
    Found = AppleAgxBindingFind(&Bindings, 0x144000);
    ok(Found && Found->Address == 0x144000 && Found->Range == 0xc000 && Found->Offset == 0 && Found->SinglePage,
       "The trimmed single page binding is %I64x+%I64x at offset %I64x\n", Found ? Found->Address : 0,
       Found ? Found->Range : 0, Found ? Found->Offset : 0);
    ok_int(AgxObjectReferences[1], 1);

    New.Address = 0x200000;
    New.Range = 0x4000;
    ok_hex(AppleAgxBindingInsert(&Bindings, &New), STATUS_SUCCESS);
    ok_int(AgxObjectReferences[1], 2);
    ok_hex(AppleAgxBindingRemove(&Bindings, 0x104000, 0x4000), STATUS_INSUFFICIENT_RESOURCES);
    ok_int(Bindings.Count, 4);
    ok_hex(AppleAgxBindingInsert(&Bindings, &New), STATUS_CONFLICTING_ADDRESSES);
    New.Address = 0x300000;
    ok_hex(AppleAgxBindingInsert(&Bindings, &New), STATUS_INSUFFICIENT_RESOURCES);
    ok_int(AgxObjectReferences[1], 2);

    ok_hex(AppleAgxBindingRemove(&Bindings, 0, 0x1000000), STATUS_SUCCESS);
    ok_int(Bindings.Count, 0);
    ok_int(AgxObjectReferences[0], 0);
    ok_int(AgxObjectReferences[1], 0);
    ok_hex(AppleAgxBindingRemove(&Bindings, 0x1000, 0), STATUS_INVALID_PARAMETER);
    New.Object = NULL;
    ok_hex(AppleAgxBindingInsert(&Bindings, &New), STATUS_INVALID_PARAMETER);
}

static
VOID
TestAgxParameterBuffer(VOID)
{
    APPLE_AGX_PARAMETER_BUFFER Buffer;
    ULONG64 Value;
    ULONG Word;

    RtlZeroMemory(&Buffer, sizeof(Buffer));
    Buffer.PageList = 0xfffffc2d01000000ULL;
    Buffer.BlockList = 0xfffffc2d02000000ULL;
    Buffer.BlockControl = 0xfffffc2100028000ULL;
    Buffer.Counter = 0xfffffc210002c000ULL;
    Buffer.Pages = APPLE_AGX_PB_PAGES;
    Buffer.Blocks = APPLE_AGX_PB_BLOCKS;
    Buffer.BlockBytes = 0x4000;

    RtlFillMemory(AgxPbManager, sizeof(AgxPbManager), 0x5a);
    ok_hex(AppleAgxDescribeParameterBuffer(AgxPbManager, &Buffer), STATUS_SUCCESS);
    RtlCopyMemory(&Value, AgxPbManager + APPLE_AGX_PB_PAGE_LIST, sizeof(Value));
    ok(Value == Buffer.PageList, "The page list is at %I64x\n", Value);
    RtlCopyMemory(&Word, AgxPbManager + APPLE_AGX_PB_PAGE_LIST_BYTES, sizeof(Word));
    ok_int(Word, APPLE_AGX_PB_PAGES * 4);
    RtlCopyMemory(&Word, AgxPbManager + APPLE_AGX_PB_PAGE_COUNT, sizeof(Word));
    ok_int(Word, APPLE_AGX_PB_PAGES);
    RtlCopyMemory(&Word, AgxPbManager + APPLE_AGX_PB_RECORDS, sizeof(Word));
    ok_int(Word, APPLE_AGX_PB_LIST_RECORDS);
    RtlCopyMemory(&Word, AgxPbManager + APPLE_AGX_PB_BLOCK_COUNT, sizeof(Word));
    ok_int(Word, APPLE_AGX_PB_BLOCKS);
    RtlCopyMemory(&Value, AgxPbManager + APPLE_AGX_PB_BLOCK_LIST, sizeof(Value));
    ok(Value == Buffer.BlockList, "The block list is at %I64x\n", Value);
    RtlCopyMemory(&Value, AgxPbManager + APPLE_AGX_PB_BLOCK_CONTROL, sizeof(Value));
    ok(Value == Buffer.BlockControl, "The block control is at %I64x\n", Value);
    /* The last page is named by index, not by count. */
    RtlCopyMemory(&Word, AgxPbManager + APPLE_AGX_PB_LAST_PAGE, sizeof(Word));
    ok_int(Word, APPLE_AGX_PB_PAGES - 1);
    RtlCopyMemory(&Word, AgxPbManager + APPLE_AGX_PB_BLOCK_BYTES, sizeof(Word));
    ok_hex(Word, 0x4000);
    RtlCopyMemory(&Value, AgxPbManager + APPLE_AGX_PB_COUNTER, sizeof(Value));
    ok(Value == Buffer.Counter, "The counter is at %I64x\n", Value);
    RtlCopyMemory(&Word, AgxPbManager + APPLE_AGX_PB_MAX_PAGES_A, sizeof(Word));
    ok_int(Word, APPLE_AGX_PB_PAGES);
    RtlCopyMemory(&Word, AgxPbManager + APPLE_AGX_PB_MAX_PAGES_B, sizeof(Word));
    ok_int(Word, APPLE_AGX_PB_PAGES);

    /* A buffer larger than the firmware's own bound, or with nothing to record on, is refused. */
    Buffer.Pages = APPLE_AGX_PB_PAGES + 1;
    ok_hex(AppleAgxDescribeParameterBuffer(AgxPbManager, &Buffer), STATUS_INVALID_PARAMETER);
    Buffer.Pages = APPLE_AGX_PB_PAGES;
    Buffer.Blocks = APPLE_AGX_PB_BLOCKS + 1;
    ok_hex(AppleAgxDescribeParameterBuffer(AgxPbManager, &Buffer), STATUS_INVALID_PARAMETER);
    Buffer.Blocks = APPLE_AGX_PB_BLOCKS;
    Buffer.BlockList = 0;
    ok_hex(AppleAgxDescribeParameterBuffer(AgxPbManager, &Buffer), STATUS_INVALID_PARAMETER);

    /* A block's record cycles through the list. */
    ok_int(AppleAgxBlockRecord(0), 0);
    ok_int(AppleAgxBlockRecord(1), 8);
    ok_int(AppleAgxBlockRecord(APPLE_AGX_PB_LIST_RECORDS - 1), 8 * (APPLE_AGX_PB_LIST_RECORDS - 1));
    ok_int(AppleAgxBlockRecord(APPLE_AGX_PB_LIST_RECORDS), 0);
    ok_int(AppleAgxBlockRecord(APPLE_AGX_PB_LIST_RECORDS + 3), 24);

    /* The block control starts with every block free and the producer at the start. */
    RtlFillMemory(AgxBlockControl, sizeof(AgxBlockControl), 0x5a);
    RtlFillMemory(AgxMisc, sizeof(AgxMisc), 0x5a);
    ok_hex(AppleAgxDescribeBlockControl(AgxBlockControl, AgxMisc, APPLE_AGX_PB_BLOCKS), STATUS_SUCCESS);
    RtlCopyMemory(&Word, AgxBlockControl + APPLE_AGX_BLOCK_CONTROL_BLOCKS, sizeof(Word));
    ok_int(Word, APPLE_AGX_PB_BLOCKS);
    RtlCopyMemory(&Word, AgxBlockControl + APPLE_AGX_BLOCK_CONTROL_PRODUCER, sizeof(Word));
    ok_int(Word, 0);
    RtlCopyMemory(&Word, AgxMisc + APPLE_AGX_MISC_ENABLE, sizeof(Word));
    ok_int(Word, 1);
    RtlCopyMemory(&Word, AgxMisc, sizeof(Word));
    ok_int(Word, 0);
    ok_hex(AppleAgxDescribeBlockControl(AgxBlockControl, AgxMisc, 0), STATUS_INVALID_PARAMETER);

    /* The scene list is cycled by draw, so its slot count bounds the ordinal. */
    ok_int(APPLE_AGX_SCENE_LIST_SLOTS, 48);
    ok_int(APPLE_AGX_RENDER_POOLS, 8);
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
    if (!FdtStringListContains(Compatible, CompatibleLength, "apple,t8122-aic3") || CellCount != 3 || Cells[0])
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
    ok(FdtOpen(Builder.Blob, Builder.BlobSize, &Tree->Fdt), "The audio tree did not open\n");
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

    for (Child = FdtFirstChild(&Tree->Fdt, Parent); Child != FDT_NO_NODE;
         Child = FdtNextSibling(&Tree->Fdt, Child))
    {
        ChildName = FdtNodeName(&Tree->Fdt, Child, &Length);
        if (ChildName && FdtNameMatches(ChildName, Length, Name, (ULONG)strlen(Name)))
            return Child;
    }
    return FDT_NO_NODE;
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
    ok(Codec != FDT_NO_NODE, "No codec node\n");

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
    TestAdt();
    TestAgxDescribe();
    TestRtkitBoot();
    TestAgxInitLayout();
    TestAgxInitWrite();
    TestAgxRing();
    TestAgxRegisters();
    TestAgxStates();
    TestAgxQueue();
    TestAgxPools();
    TestAgxEncodeCompute();
    TestAgxComputeWork();
    TestAgxRenderRegisters();
    TestAgxParameterBuffer();
    TestAgxRenderSequences();
    TestAgxGeometry();
    TestAgxVertexWork();
    TestAgxFragmentWork();
    TestAgxScene();
    TestAgxTranslateRender();
    TestAgxBuildRender();
    TestAgxParams();
    TestAgxMemoryCalls();
    TestAgxBindings();
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
