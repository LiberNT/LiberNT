/*
 * PROJECT:     LiberNT Apple MTP HID Transport Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Internal keyboard of Apple silicon laptops behind the MTP coprocessor and DockChannel
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntddk.h>
#include <hidport.h>
#include <ntstrsafe.h>
#include <reactos/drivers/fdtdrv.h>
#include <reactos/drivers/applertkit.h>
#include <reactos/drivers/appleadmac.h>
#include <reactos/drivers/applesmc.h>
#include <reactos/drivers/dumpstor.h>

#define MTP_TAG 'pTMA'

#define MTP_ASC_CPU_CONTROL         0x44
#define MTP_ASC_CPU_CONTROL_RUN     0x10

#define DART_PARAMS1                0x00
#define DART_PARAMS2                0x04
#define DART_T8110_PARAMS3          0x08
#define DART_T8110_PARAMS4          0x0c
#define DART_T8110_TLB_CMD          0x80
#define DART_T8110_TLB_CMD_BUSY     0x80000000
#define DART_T8110_ERROR            0x100
#define DART_T8110_ERROR_ADDR_LO    0x170
#define DART_T8110_ERROR_ADDR_HI    0x174
#define DART_T8110_PROTECT          0x200
#define DART_T8110_ENABLE_STREAMS   0xc00
#define DART_T8110_TCR              0x1000
#define DART_T8110_TCR_TRANSLATE    0x1
#define DART_T8110_TTBR             0x1400
#define DART_T8110_TTBR_VALID       0x1
#define DART_PAGE_SHIFT             14
#define DART_PAGE_SIZE              (1UL << DART_PAGE_SHIFT)
#define DART_TABLE_ENTRIES          2048
#define DART_PTE_VALID              1ULL
#define DART_PTE_SP_END             (0xfffULL << 40)
#define DART_IOVA_BASE              0x01000000ULL
#define DART_MAX_L2                 8
#define MTP_SMC_WAIT_MS             5000
#define MTP_SMC_WAIT_STEP_MS        50

#define DC_IRQ_MASK                 0x0
#define DC_IRQ_FLAG                 0x4
#define DC_CONFIG_TX_THRESH         0x0
#define DC_CONFIG_RX_THRESH         0x4
#define DC_DATA_TX8                 0x04
#define DC_DATA_TX32                0x10
#define DC_DATA_TX_FREE             0x14
#define DC_DATA_RX8                 0x1c
#define DC_DATA_RX32                0x28
#define DC_DATA_RX_COUNT            0x2c

#define DCHID_CHANNEL_CMD           0x11
#define DCHID_CHANNEL_REPORT        0x12
#define DCHID_IFACE_COMM            0
#define DCHID_MAX_INTERFACES        16
#define DCHID_MAX_PACKET            (0x10000 + 16)
#define DCHID_REPORT_INPUT          0
#define DCHID_REPORT_FEATURE        2
#define DCHID_REQ_SET_REPORT        0
#define DCHID_REQ_GET_REPORT        1
#define DCHID_EVENT_GPIO_CMD        0xa0
#define DCHID_EVENT_INIT            0xf0
#define DCHID_EVENT_READY           0xf1
#define DCHID_INIT_HID_DESCRIPTOR   0
#define DCHID_INIT_GPIO_REQUEST     1
#define DCHID_INIT_TERMINATOR       2
#define DCHID_INIT_PRODUCT_NAME     7
#define DCHID_CMD_ENABLE_INTERFACE  0xb4
#define DCHID_CMD_ACK_GPIO_CMD      0xa1
#define DCHID_STM_REPORT_ID         0x10
#define DCHID_CMD_RESET_INTERFACE   0x40
#define DCHID_CMD_SEND_FIRMWARE     0x95
#define DCHID_FW_MAGIC              0x46444948
#define DCHID_MAX_GPIO_NAME         32

#define TP_HEADER_SIZE              38
#define TP_FINGER_SIZE              30
#define TP_FINGERS_OFFSET           22
#define TP_BUTTONS_OFFSET           23
#define TP_FINGER_X                 4
#define TP_FINGER_Y                 6
#define TP_FINGER_TOUCH_MAJOR       18
#define TP_MOTION_DIVISOR           4
#define TP_SCROLL_DIVISOR           48
#define MTP_MOUSE_REPORT_LENGTH     5

#define MTP_QUEUED_REPORTS          64
#define MTP_MAX_REPORT              512
#define MTP_COMMAND_TIMEOUT_MS      1000
#define MTP_BOOT_TIMEOUT_MS         2000
#define MTP_KEYBOARD_TIMEOUT_MS     4000
#define MTP_POLL_MS                 1
#define MTP_MAX_BUFFERS             8

#pragma pack(push, 1)
typedef struct _DCHID_HEADER
{
    UCHAR HeaderLength;
    UCHAR Channel;
    USHORT Length;
    UCHAR Sequence;
    UCHAR Interface;
    USHORT Pad;
} DCHID_HEADER, *PDCHID_HEADER;

typedef struct _DCHID_SUBHEADER
{
    UCHAR Flags;
    UCHAR Unknown;
    USHORT Length;
    ULONG ReturnCode;
} DCHID_SUBHEADER, *PDCHID_SUBHEADER;

typedef struct _DCHID_INIT_HEADER
{
    UCHAR Type;
    UCHAR Unknown1;
    UCHAR Unknown2;
    UCHAR Interface;
    CHAR Name[16];
    UCHAR MorePackets;
    UCHAR Pad;
} DCHID_INIT_HEADER, *PDCHID_INIT_HEADER;

typedef struct _DCHID_INIT_BLOCK
{
    USHORT Type;
    USHORT Length;
} DCHID_INIT_BLOCK, *PDCHID_INIT_BLOCK;

typedef struct _DCHID_STM_ID
{
    UCHAR Unknown;
    USHORT VendorId;
    USHORT ProductId;
    USHORT Version;
    UCHAR Unknown2;
    UCHAR Unknown3;
    UCHAR KeyboardType;
    UCHAR SerialLength;
} DCHID_STM_ID, *PDCHID_STM_ID;
#pragma pack(pop)

C_ASSERT(sizeof(DCHID_HEADER) == 8);
C_ASSERT(sizeof(DCHID_SUBHEADER) == 8);
C_ASSERT(sizeof(DCHID_INIT_HEADER) == 22);

typedef struct _MTP_BUFFER
{
    PVOID Raw;
    PVOID Virtual;
    PHYSICAL_ADDRESS Physical;
    ULONG Size;
    ULONG64 Iova;
} MTP_BUFFER, *PMTP_BUFFER;

typedef struct _MTP_INTERFACE
{
    BOOLEAN Present;
    BOOLEAN InitComplete;
    BOOLEAN Enabled;
    BOOLEAN EnableRequested;
    BOOLEAN Ready;
    BOOLEAN Deferred;
    UCHAR TxSequence;
    CHAR Name[17];
    PUCHAR Descriptor;
    ULONG DescriptorLength;
    USHORT GpioId;
    CHAR GpioName[DCHID_MAX_GPIO_NAME + 1];
    BOOLEAN FirmwareSent;
} MTP_INTERFACE, *PMTP_INTERFACE;

typedef struct _MTP_DEVICE
{
    PDEVICE_OBJECT DeviceObject;
    PDEVICE_OBJECT LowerDevice;
    PDEVICE_OBJECT Pdo;

    FDT_DRIVER_TREE Tree;

    PUCHAR Asc;
    ULONG AscSize;
    PUCHAR Mbox;
    ULONG MboxSize;
    PUCHAR Sram;
    ULONG64 SramBase;
    ULONG SramSize;
    PUCHAR Dart;
    ULONG DartSize;
    ULONG DartSid;
    PUCHAR FifoIrq;
    ULONG FifoIrqSize;
    PUCHAR Config;
    ULONG ConfigSize;
    PUCHAR Data;
    ULONG DataSize;
    ULONG FifoSize;

    MTP_BUFFER DartL1;
    MTP_BUFFER DartL2[DART_MAX_L2];
    ULONG DartL2Index[DART_MAX_L2];
    ULONG DartL2Count;
    ULONG64 NextIova;
    BOOLEAN DartReady;

    MTP_BUFFER Buffers[MTP_MAX_BUFFERS];
    ULONG BufferCount;

    APPLE_RTKIT Rtkit;

    PUCHAR Packet;
    ULONG PacketFill;
    ULONG PacketNeed;

    MTP_INTERFACE Interfaces[DCHID_MAX_INTERFACES];
    BOOLEAN IdReady;
    BOOLEAN StmIdRequested;
    DCHID_STM_ID StmId;

    BOOLEAN CommandPending;
    UCHAR CommandInterface;
    UCHAR CommandFlags;
    UCHAR CommandReport;
    ULONG CommandReturnCode;
    BOOLEAN CommandDone;
    PUCHAR ResponseBuffer;
    ULONG ResponseSize;

    UCHAR KeyboardInterface;
    KEVENT KeyboardEvent;
    PUCHAR ReportDescriptor;
    ULONG ReportDescriptorLength;

    UCHAR TouchInterface;
    BOOLEAN TouchReady;
    UCHAR MouseReportId;
    BOOLEAN KeyboardUsesReportIds;
    BOOLEAN TouchActive;
    LONG TouchLastX;
    LONG TouchLastY;
    LONG ScrollRemainder;
    UCHAR TouchFingers;

    BOOLEAN GpioPending;
    UCHAR GpioCommand[64];
    ULONG GpioCommandLength;

    PKTHREAD Thread;
    KEVENT StopEvent;
    KEVENT DataEvent;
    volatile LONG Stop;
    BOOLEAN Started;

    PKINTERRUPT Interrupt;
    KDPC Dpc;
    ULONG FifoGsi;
    BOOLEAN FifoEdge;
    ULONG RxIrqBit;
    ULONG TxIrqBit;
    volatile LONG Interrupts;
    volatile LONG InterruptLogged;

    KSPIN_LOCK QueueLock;
    LIST_ENTRY PendingReads;
    UCHAR ReportQueue[MTP_QUEUED_REPORTS][MTP_MAX_REPORT];
    USHORT ReportLength[MTP_QUEUED_REPORTS];
    ULONG ReportHead;
    ULONG ReportCount;

    ROS_USB_KEYBOARD_INTERFACE DebuggerKeyboard;
    PCALLBACK_OBJECT DebuggerKeyboardCallback;
    BOOLEAN DebuggerPolling;
    BOOLEAN DebuggerReportValid;
    BOOLEAN DebuggerLayoutKnown;
    UCHAR DebuggerReportId;
    UCHAR DebuggerModifierOffset;
    UCHAR DebuggerKeysOffset;
    UCHAR DebuggerKeysCount;
    UCHAR DebuggerReport[ROS_USB_KEYBOARD_BOOT_REPORT_SIZE];
} MTP_DEVICE, *PMTP_DEVICE;

static
PMTP_DEVICE
MtpGetDevice(
    _In_ PDEVICE_OBJECT DeviceObject)
{
    return ((PHID_DEVICE_EXTENSION)DeviceObject->DeviceExtension)->MiniDeviceExtension;
}

static
ULONG64
MtpNowMs(VOID)
{
    return KeQueryInterruptTime() / 10000;
}

static
VOID
MtpSleepMs(
    _In_ ULONG Milliseconds)
{
    LARGE_INTEGER Interval;

    Interval.QuadPart = -(LONGLONG)Milliseconds * 10000;
    KeDelayExecutionThread(KernelMode, FALSE, &Interval);
}

static
ULONG
MtpRead32(
    _In_ PUCHAR Base,
    _In_ ULONG Offset)
{
    return READ_REGISTER_ULONG((volatile ULONG *)(Base + Offset));
}

static
VOID
MtpWrite32(
    _In_ PUCHAR Base,
    _In_ ULONG Offset,
    _In_ ULONG Value)
{
    WRITE_REGISTER_ULONG((volatile ULONG *)(Base + Offset), Value);
}

static
PUCHAR
MtpMap(
    _In_ PMTP_DEVICE Device,
    _In_ ULONG Node,
    _In_ ULONG Index,
    _In_z_ PCSTR What,
    _Out_opt_ PULONG64 Base,
    _Out_ PULONG Size)
{
    ULONG64 Address = 0;
    PUCHAR Va = FdtDriverMapReg(&Device->Tree, Node, Index, &Address, Size);

    DbgPrint("applemtp: %s at %I64x+%lx mapped at %p\n", What, Address, *Size, Va);
    if (Base)
        *Base = Address;
    return Va;
}

static
BOOLEAN
MtpAllocateDma(
    _In_ ULONG Size,
    _Out_ PMTP_BUFFER Buffer)
{
    PHYSICAL_ADDRESS Low, High, Boundary;
    PUCHAR Raw;
    ULONG64 Physical, Aligned;

    RtlZeroMemory(Buffer, sizeof(*Buffer));
    Size = (Size + DART_PAGE_SIZE - 1) & ~(DART_PAGE_SIZE - 1);
    Low.QuadPart = 0;
    High.QuadPart = -1;
    Boundary.QuadPart = 0;
    Raw = MmAllocateContiguousMemorySpecifyCache(Size + DART_PAGE_SIZE, Low, High, Boundary, MmNonCached);
    if (!Raw)
        return FALSE;
    Physical = (ULONG64)MmGetPhysicalAddress(Raw).QuadPart;
    Aligned = (Physical + DART_PAGE_SIZE - 1) & ~(ULONG64)(DART_PAGE_SIZE - 1);
    Buffer->Raw = Raw;
    Buffer->Virtual = Raw + (Aligned - Physical);
    Buffer->Physical.QuadPart = (LONGLONG)Aligned;
    Buffer->Size = Size;
    RtlZeroMemory(Buffer->Virtual, Size);
    return TRUE;
}

static
VOID
MtpFreeDma(
    _Inout_ PMTP_BUFFER Buffer)
{
    if (Buffer->Raw)
        MmFreeContiguousMemorySpecifyCache(Buffer->Raw, Buffer->Size + DART_PAGE_SIZE, MmNonCached);
    RtlZeroMemory(Buffer, sizeof(*Buffer));
}

static
VOID
MtpDartFlush(
    _In_ PMTP_DEVICE Device)
{
    ULONG Spin;

    KeMemoryBarrier();
    MtpWrite32(Device->Dart, DART_T8110_TLB_CMD, (1UL << 8) | (Device->DartSid & 0xff));
    for (Spin = 0; Spin < 100000; ++Spin)
    {
        if (!(MtpRead32(Device->Dart, DART_T8110_TLB_CMD) & DART_T8110_TLB_CMD_BUSY))
            return;
        KeStallExecutionProcessor(1);
    }
    DbgPrint("applemtp: DART TLB flush did not complete\n");
}

static
BOOLEAN
MtpDartInitialize(
    _Inout_ PMTP_DEVICE Device)
{
    ULONG Params1, Params2, Params3, Params4, Protect, Tcr, Ttbr;

    Params1 = MtpRead32(Device->Dart, DART_PARAMS1);
    Params2 = MtpRead32(Device->Dart, DART_PARAMS2);
    Params3 = MtpRead32(Device->Dart, DART_T8110_PARAMS3);
    Params4 = MtpRead32(Device->Dart, DART_T8110_PARAMS4);
    Protect = MtpRead32(Device->Dart, DART_T8110_PROTECT);
    Tcr = MtpRead32(Device->Dart, DART_T8110_TCR + 4 * Device->DartSid);
    Ttbr = MtpRead32(Device->Dart, DART_T8110_TTBR + 4 * Device->DartSid);
    DbgPrint("applemtp: DART params %08lx %08lx %08lx %08lx, page %lu, va %lu bits, pa %lu bits, %lu streams\n",
             Params1, Params2, Params3, Params4, 1UL << ((Params1 >> 24) & 0xf), (Params3 >> 16) & 0x3f,
             (Params3 >> 24) & 0x3f, Params4 & 0x1ff);
    DbgPrint("applemtp: DART stream %lu: protect %08lx tcr %08lx ttbr %08lx\n", Device->DartSid, Protect, Tcr, Ttbr);

    if (((Params1 >> 24) & 0xf) != DART_PAGE_SHIFT)
    {
        DbgPrint("applemtp: unexpected DART page size, DMA buffers disabled\n");
        return FALSE;
    }
    if (Protect & 1)
    {
        DbgPrint("applemtp: DART is locked by the firmware, DMA buffers disabled\n");
        return FALSE;
    }
    if (!MtpAllocateDma(DART_PAGE_SIZE, &Device->DartL1))
        return FALSE;

    MtpWrite32(Device->Dart, DART_T8110_TCR + 4 * Device->DartSid, 0);
    MtpWrite32(Device->Dart, DART_T8110_TTBR + 4 * Device->DartSid,
               (ULONG)(((ULONG64)Device->DartL1.Physical.QuadPart >> DART_PAGE_SHIFT) << 2) | DART_T8110_TTBR_VALID);
    MtpWrite32(Device->Dart, DART_T8110_ENABLE_STREAMS + 4 * (Device->DartSid >> 5), 1UL << (Device->DartSid & 31));
    MtpDartFlush(Device);
    MtpWrite32(Device->Dart, DART_T8110_TCR + 4 * Device->DartSid, DART_T8110_TCR_TRANSLATE);
    Device->NextIova = DART_IOVA_BASE;
    Device->DartReady = TRUE;
    DbgPrint("applemtp: DART stream %lu translates through %I64x\n", Device->DartSid,
             Device->DartL1.Physical.QuadPart);
    return TRUE;
}

static
BOOLEAN
MtpDartMap(
    _Inout_ PMTP_DEVICE Device,
    _Inout_ PMTP_BUFFER Buffer)
{
    volatile ULONG64 *L1 = Device->DartL1.Virtual;
    ULONG64 Iova = Device->NextIova, Offset;
    ULONG L1Index, L2Index, Slot;

    if (!Device->DartReady)
        return FALSE;
    for (Offset = 0; Offset < Buffer->Size; Offset += DART_PAGE_SIZE)
    {
        volatile ULONG64 *L2 = NULL;

        L1Index = (ULONG)((Iova + Offset) >> 25) & (DART_TABLE_ENTRIES - 1);
        L2Index = (ULONG)((Iova + Offset) >> DART_PAGE_SHIFT) & (DART_TABLE_ENTRIES - 1);
        for (Slot = 0; Slot < Device->DartL2Count; ++Slot)
        {
            if (Device->DartL2Index[Slot] == L1Index)
                L2 = Device->DartL2[Slot].Virtual;
        }
        if (!L2)
        {
            if (Device->DartL2Count == DART_MAX_L2 ||
                !MtpAllocateDma(DART_PAGE_SIZE, &Device->DartL2[Device->DartL2Count]))
                return FALSE;
            L2 = Device->DartL2[Device->DartL2Count].Virtual;
            Device->DartL2Index[Device->DartL2Count] = L1Index;
            L1[L1Index] = ((((ULONG64)Device->DartL2[Device->DartL2Count].Physical.QuadPart) >> DART_PAGE_SHIFT) << 10) |
                          DART_PTE_VALID;
            Device->DartL2Count++;
        }
        L2[L2Index] = ((((ULONG64)Buffer->Physical.QuadPart + Offset) >> DART_PAGE_SHIFT) << 10) |
                      DART_PTE_SP_END | DART_PTE_VALID;
    }
    Buffer->Iova = Iova;
    Device->NextIova = Iova + Buffer->Size;
    MtpDartFlush(Device);
    return TRUE;
}

static
VOID
MtpDartCheckFault(
    _In_ PMTP_DEVICE Device)
{
    ULONG Error;

    if (!Device->Dart)
        return;
    Error = MtpRead32(Device->Dart, DART_T8110_ERROR);
    if (!(Error & 0x80000000))
        return;
    DbgPrint("applemtp: DART fault %08lx on stream %lu at %08lx%08lx\n", Error, (Error >> 20) & 0xff,
             MtpRead32(Device->Dart, DART_T8110_ERROR_ADDR_HI), MtpRead32(Device->Dart, DART_T8110_ERROR_ADDR_LO));
    MtpWrite32(Device->Dart, DART_T8110_ERROR, Error);
}

static
BOOLEAN
NTAPI
MtpShmemSetup(
    _In_ PVOID Context,
    _In_ UCHAR Endpoint,
    _In_ ULONG Size,
    _Inout_ PULONG64 Iova,
    _Out_ PVOID *Virtual,
    _Out_ PBOOLEAN Preassigned)
{
    PMTP_DEVICE Device = Context;
    PMTP_BUFFER Buffer;

    UNREFERENCED_PARAMETER(Endpoint);
    *Virtual = NULL;
    *Preassigned = FALSE;
    if (*Iova)
    {
        if (!Device->Sram || *Iova < Device->SramBase || *Iova + Size > Device->SramBase + Device->SramSize)
            return FALSE;
        *Virtual = Device->Sram + (*Iova - Device->SramBase);
        *Preassigned = TRUE;
        return TRUE;
    }
    if (Device->BufferCount == MTP_MAX_BUFFERS)
        return FALSE;
    Buffer = &Device->Buffers[Device->BufferCount];
    if (!MtpAllocateDma(Size, Buffer))
        return FALSE;
    if (!MtpDartMap(Device, Buffer))
    {
        MtpFreeDma(Buffer);
        return FALSE;
    }
    Device->BufferCount++;
    *Iova = Buffer->Iova;
    *Virtual = Buffer->Virtual;
    return TRUE;
}

static
NTSTATUS
MtpBootCoprocessor(
    _Inout_ PMTP_DEVICE Device)
{
    DbgPrint("applemtp: coprocessor control %08lx\n", MtpRead32(Device->Asc, MTP_ASC_CPU_CONTROL));
    Device->Rtkit.Name = "applemtp";
    Device->Rtkit.Mailbox = Device->Mbox;
    Device->Rtkit.Context = Device;
    Device->Rtkit.ShmemSetup = MtpShmemSetup;
    Device->Rtkit.Receive = NULL;
    MtpWrite32(Device->Asc, MTP_ASC_CPU_CONTROL, MTP_ASC_CPU_CONTROL_RUN);
    return AppleRtkitWake(&Device->Rtkit, MTP_BOOT_TIMEOUT_MS);
}

static
ULONG
MtpChecksum(
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length)
{
    ULONG Sum = 0, Index;

    for (Index = 0; Index + 4 <= Length; Index += 4)
        Sum += (ULONG)Data[Index] | ((ULONG)Data[Index + 1] << 8) | ((ULONG)Data[Index + 2] << 16) |
               ((ULONG)Data[Index + 3] << 24);
    return Sum;
}

static
BOOLEAN
MtpFifoSend(
    _In_ PMTP_DEVICE Device,
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length)
{
    ULONG64 Deadline = MtpNowMs() + MTP_COMMAND_TIMEOUT_MS;
    ULONG Free, Value;

    while (Length)
    {
        Free = MtpRead32(Device->Data, DC_DATA_TX_FREE);
        if (!Free)
        {
            if (MtpNowMs() > Deadline)
            {
                DbgPrint("applemtp: DockChannel transmit FIFO stayed full\n");
                return FALSE;
            }
            KeStallExecutionProcessor(10);
            continue;
        }
        while (Free && Length)
        {
            if (Length >= 4 && Free >= 4)
            {
                Value = (ULONG)Data[0] | ((ULONG)Data[1] << 8) | ((ULONG)Data[2] << 16) | ((ULONG)Data[3] << 24);
                MtpWrite32(Device->Data, DC_DATA_TX32, Value);
                Data += 4;
                Length -= 4;
                Free -= 4;
            }
            else
            {
                MtpWrite32(Device->Data, DC_DATA_TX8, *Data++);
                Length--;
                Free--;
            }
        }
    }
    return TRUE;
}

static
BOOLEAN
MtpSendCommand(
    _Inout_ PMTP_DEVICE Device,
    _In_ UCHAR Interface,
    _In_ UCHAR Flags,
    _In_reads_bytes_(Length) const UCHAR *Message,
    _In_ ULONG Length)
{
    UCHAR Frame[16 + 64 + 4];
    PDCHID_HEADER Header = (PDCHID_HEADER)Frame;
    PDCHID_SUBHEADER Sub = (PDCHID_SUBHEADER)(Frame + sizeof(*Header));
    ULONG Padded = (Length + 3) & ~3UL, Total, Checksum;

    if (Padded > 64)
        return FALSE;
    RtlZeroMemory(Frame, sizeof(Frame));
    Header->HeaderLength = sizeof(*Header);
    Header->Channel = DCHID_CHANNEL_CMD;
    Header->Length = (USHORT)(Padded + sizeof(*Sub));
    Header->Sequence = Device->Interfaces[Interface].TxSequence;
    Header->Interface = Interface;
    Sub->Flags = Flags;
    Sub->Length = (USHORT)Length;
    RtlCopyMemory(Frame + sizeof(*Header) + sizeof(*Sub), Message, Length);
    Total = sizeof(*Header) + sizeof(*Sub) + Padded;
    Checksum = 0xffffffff - MtpChecksum(Frame, Total);
    RtlCopyMemory(Frame + Total, &Checksum, sizeof(Checksum));
    return MtpFifoSend(Device, Frame, Total + sizeof(Checksum));
}

static VOID MtpPollFifo(_Inout_ PMTP_DEVICE Device);

static
NTSTATUS
MtpCommand(
    _Inout_ PMTP_DEVICE Device,
    _In_ UCHAR Interface,
    _In_ UCHAR Type,
    _In_ UCHAR Request,
    _In_reads_bytes_(Length) const UCHAR *Message,
    _In_ ULONG Length,
    _Out_writes_bytes_opt_(ResponseSize) PUCHAR Response,
    _In_ ULONG ResponseSize,
    _Out_opt_ PULONG Returned)
{
    ULONG64 Deadline;
    NTSTATUS Status;

    if (Device->CommandPending)
        return STATUS_DEVICE_BUSY;
    Device->CommandPending = TRUE;
    Device->CommandDone = FALSE;
    Device->CommandInterface = Interface;
    Device->CommandFlags = (UCHAR)((Type << 6) | (Request & 0x3f));
    Device->CommandReport = Message[0];
    Device->ResponseBuffer = Response;
    Device->ResponseSize = ResponseSize;
    Device->CommandReturnCode = 0;

    if (!MtpSendCommand(Device, Interface, Device->CommandFlags, Message, Length))
    {
        Status = STATUS_IO_DEVICE_ERROR;
        goto Done;
    }
    Deadline = MtpNowMs() + MTP_COMMAND_TIMEOUT_MS;
    while (!Device->CommandDone)
    {
        MtpPollFifo(Device);
        if (Device->CommandDone)
            break;
        if (MtpNowMs() > Deadline)
        {
            DbgPrint("applemtp: command %02x to interface %u timed out\n", Message[0], Interface);
            Status = STATUS_IO_TIMEOUT;
            goto Done;
        }
        MtpSleepMs(1);
    }
    if (Returned)
        *Returned = Device->ResponseSize;
    Status = Device->CommandReturnCode ? STATUS_IO_DEVICE_ERROR : STATUS_SUCCESS;
    if (Device->CommandReturnCode)
        DbgPrint("applemtp: command %02x to interface %u failed with %08lx\n", Message[0], Interface,
                 Device->CommandReturnCode);

Done:
    Device->Interfaces[Interface].TxSequence++;
    Device->CommandPending = FALSE;
    Device->ResponseBuffer = NULL;
    return Status;
}

static
VOID
MtpQueueReport(
    _Inout_ PMTP_DEVICE Device,
    _In_reads_bytes_(Length) const UCHAR *Report,
    _In_ ULONG Length)
{
    PIRP Irp = NULL;
    KIRQL OldIrql;

    if (!Length || Length > MTP_MAX_REPORT)
        return;
    KeAcquireSpinLock(&Device->QueueLock, &OldIrql);
    while (!IsListEmpty(&Device->PendingReads))
    {
        PLIST_ENTRY Entry = RemoveHeadList(&Device->PendingReads);

        Irp = CONTAINING_RECORD(Entry, IRP, Tail.Overlay.ListEntry);
        if (IoSetCancelRoutine(Irp, NULL))
            break;
        InitializeListHead(&Irp->Tail.Overlay.ListEntry);
        Irp = NULL;
    }
    if (!Irp)
    {
        ULONG Slot;

        if (Device->ReportCount == MTP_QUEUED_REPORTS)
        {
            Device->ReportHead = (Device->ReportHead + 1) % MTP_QUEUED_REPORTS;
            Device->ReportCount--;
        }
        Slot = (Device->ReportHead + Device->ReportCount) % MTP_QUEUED_REPORTS;
        RtlCopyMemory(Device->ReportQueue[Slot], Report, Length);
        Device->ReportLength[Slot] = (USHORT)Length;
        Device->ReportCount++;
    }
    KeReleaseSpinLock(&Device->QueueLock, OldIrql);
    if (Irp)
    {
        PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);

        Length = min(Length, Stack->Parameters.DeviceIoControl.OutputBufferLength);
        RtlCopyMemory(Irp->UserBuffer, Report, Length);
        Irp->IoStatus.Information = Length;
        Irp->IoStatus.Status = STATUS_SUCCESS;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
    }
}

static
VOID
MtpQueueKeyboardReport(
    _Inout_ PMTP_DEVICE Device,
    _In_reads_bytes_(Length) const UCHAR *Report,
    _In_ ULONG Length)
{
    UCHAR Buffer[MTP_MAX_REPORT];

    if (Device->KeyboardUsesReportIds)
    {
        MtpQueueReport(Device, Report, Length);
        return;
    }
    if (Length + 1 > sizeof(Buffer))
        return;
    Buffer[0] = 1;
    RtlCopyMemory(Buffer + 1, Report, Length);
    MtpQueueReport(Device, Buffer, Length + 1);
}

static
SHORT
MtpLe16(
    _In_reads_bytes_(2) const UCHAR *Data)
{
    return (SHORT)(Data[0] | (Data[1] << 8));
}

static
CHAR
MtpClamp(
    _In_ LONG Value)
{
    return (CHAR)(Value > 127 ? 127 : (Value < -127 ? -127 : Value));
}

static
VOID
MtpTranslateTouch(
    _Inout_ PMTP_DEVICE Device,
    _In_reads_bytes_(Length) const UCHAR *Report,
    _In_ ULONG Length)
{
    UCHAR Mouse[MTP_MOUSE_REPORT_LENGTH] = { 0 };
    const UCHAR *Finger = NULL;
    ULONG Points, Index, Touching = 0;
    UCHAR Fingers, Buttons;
    LONG X = 0, Y = 0, DeltaX = 0, DeltaY = 0, Wheel = 0;

    if (Length < TP_HEADER_SIZE || (Length - TP_HEADER_SIZE) % TP_FINGER_SIZE)
        return;
    Points = (Length - TP_HEADER_SIZE) / TP_FINGER_SIZE;
    Fingers = Report[TP_FINGERS_OFFSET];
    Buttons = Report[TP_BUTTONS_OFFSET];
    if (Fingers > Points)
        return;
    for (Index = 0; Index < Fingers; ++Index)
    {
        const UCHAR *Candidate = Report + TP_HEADER_SIZE + Index * TP_FINGER_SIZE;

        if (!MtpLe16(Candidate + TP_FINGER_TOUCH_MAJOR))
            continue;
        if (!Finger)
            Finger = Candidate;
        Touching++;
    }
    if (Finger)
    {
        X = MtpLe16(Finger + TP_FINGER_X);
        Y = MtpLe16(Finger + TP_FINGER_Y);
        if (Device->TouchActive && Device->TouchFingers == Touching)
        {
            DeltaX = X - Device->TouchLastX;
            DeltaY = Y - Device->TouchLastY;
        }
        else
        {
            Device->ScrollRemainder = 0;
        }
        Device->TouchActive = TRUE;
        Device->TouchLastX = X;
        Device->TouchLastY = Y;
    }
    else
    {
        Device->TouchActive = FALSE;
    }
    Device->TouchFingers = (UCHAR)Touching;

    if (Touching >= 2 && !(Buttons & 1))
    {
        Device->ScrollRemainder += DeltaY;
        Wheel = Device->ScrollRemainder / TP_SCROLL_DIVISOR;
        Device->ScrollRemainder -= Wheel * TP_SCROLL_DIVISOR;
        DeltaX = DeltaY = 0;
    }
    Mouse[0] = Device->MouseReportId;
    if (Buttons & 1)
        Mouse[1] = (Touching >= 2) ? 0x02 : 0x01;
    Mouse[2] = (UCHAR)MtpClamp(DeltaX / TP_MOTION_DIVISOR);
    Mouse[3] = (UCHAR)MtpClamp(-DeltaY / TP_MOTION_DIVISOR);
    Mouse[4] = (UCHAR)MtpClamp(Wheel);
    MtpQueueReport(Device, Mouse, sizeof(Mouse));
}

static
VOID
MtpHandleInit(
    _Inout_ PMTP_DEVICE Device,
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length)
{
    const DCHID_INIT_HEADER *Header = (const DCHID_INIT_HEADER *)Data;
    PMTP_INTERFACE Interface;
    ULONG Offset;

    if (Length < sizeof(*Header) || Header->Interface >= DCHID_MAX_INTERFACES)
        return;
    Interface = &Device->Interfaces[Header->Interface];
    Interface->Present = TRUE;
    RtlCopyMemory(Interface->Name, Header->Name, sizeof(Header->Name));
    Interface->Name[sizeof(Header->Name)] = '\0';

    for (Offset = sizeof(*Header); Offset + sizeof(DCHID_INIT_BLOCK) <= Length;)
    {
        const DCHID_INIT_BLOCK *Block = (const DCHID_INIT_BLOCK *)(Data + Offset);
        const UCHAR *Payload = Data + Offset + sizeof(*Block);

        Offset += sizeof(*Block);
        if (Block->Length > Length - Offset)
            break;
        switch (Block->Type)
        {
            case DCHID_INIT_HID_DESCRIPTOR:
                if (!Interface->Descriptor && Block->Length)
                {
                    Interface->Descriptor = ExAllocatePoolWithTag(NonPagedPool, Block->Length, MTP_TAG);
                    if (Interface->Descriptor)
                    {
                        RtlCopyMemory(Interface->Descriptor, Payload, Block->Length);
                        Interface->DescriptorLength = Block->Length;
                    }
                }
                break;

            case DCHID_INIT_GPIO_REQUEST:
                if (Block->Length >= 4 + DCHID_MAX_GPIO_NAME)
                {
                    Interface->GpioId = (USHORT)(Payload[2] | (Payload[3] << 8));
                    RtlCopyMemory(Interface->GpioName, Payload + 4, DCHID_MAX_GPIO_NAME);
                    Interface->GpioName[DCHID_MAX_GPIO_NAME] = '\0';
                    DbgPrint("applemtp: interface %s uses GPIO %u (%s)\n", Interface->Name, Interface->GpioId,
                             Interface->GpioName);
                }
                break;

            case DCHID_INIT_PRODUCT_NAME:
                if (Block->Length && Payload[Block->Length - 1] == '\0')
                    DbgPrint("applemtp: interface %s is %s\n", Interface->Name, (PCSTR)Payload);
                break;

            default:
                break;
        }
        Offset += Block->Length;
        if (Block->Type == DCHID_INIT_TERMINATOR)
            break;
    }
    if (Header->MorePackets)
        return;
    Interface->InitComplete = TRUE;
    DbgPrint("applemtp: interface %u is %s, descriptor %lu bytes\n", Header->Interface, Interface->Name,
             Interface->DescriptorLength);
}

static
VOID
MtpHandleEvent(
    _Inout_ PMTP_DEVICE Device,
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length)
{
    if (!Length)
        return;
    switch (Data[0])
    {
        case DCHID_EVENT_INIT:
            MtpHandleInit(Device, Data, Length);
            break;

        case DCHID_EVENT_READY:
            if (Length >= 2 && Data[1] < DCHID_MAX_INTERFACES)
            {
                Device->Interfaces[Data[1]].Ready = TRUE;
                DbgPrint("applemtp: interface %s is ready\n", Device->Interfaces[Data[1]].Name);
            }
            break;

        case DCHID_EVENT_GPIO_CMD:
            if (Length >= 5 && Length <= sizeof(Device->GpioCommand) && !Device->GpioPending)
            {
                RtlCopyMemory(Device->GpioCommand, Data, Length);
                Device->GpioCommandLength = Length;
                Device->GpioPending = TRUE;
            }
            break;

        default:
            break;
    }
}

static
VOID
MtpCaptureDebuggerKeyboard(
    _Inout_ PMTP_DEVICE Device)
{
    const DCHID_HEADER *Header = (const DCHID_HEADER *)Device->Packet;
    const DCHID_SUBHEADER *Sub = (const DCHID_SUBHEADER *)(Device->Packet + sizeof(*Header));
    const UCHAR *Payload = (const UCHAR *)(Sub + 1);
    ULONG Base = Device->KeyboardUsesReportIds ? 1 : 0;
    ULONG Index;

    if (MtpChecksum(Device->Packet, sizeof(*Header) + Header->Length + 4) != 0xffffffff ||
        Header->Channel == DCHID_CHANNEL_CMD || Header->Interface != Device->KeyboardInterface ||
        Header->Length < sizeof(*Sub) || Sub->Length + sizeof(*Sub) > Header->Length ||
        (Sub->Flags >> 6) != DCHID_REPORT_INPUT)
    {
        return;
    }
    if ((Base && (Sub->Length < 1 || Payload[0] != Device->DebuggerReportId)) ||
        Sub->Length < Base + Device->DebuggerModifierOffset + 1 ||
        Sub->Length < Base + Device->DebuggerKeysOffset + Device->DebuggerKeysCount)
    {
        return;
    }

    RtlZeroMemory(Device->DebuggerReport, sizeof(Device->DebuggerReport));
    Device->DebuggerReport[0] = Payload[Base + Device->DebuggerModifierOffset];
    for (Index = 0; Index < Device->DebuggerKeysCount; ++Index)
        Device->DebuggerReport[2 + Index] = Payload[Base + Device->DebuggerKeysOffset + Index];
    Device->DebuggerReportValid = TRUE;
}

static
VOID
MtpHandlePacket(
    _Inout_ PMTP_DEVICE Device)
{
    const DCHID_HEADER *Header = (const DCHID_HEADER *)Device->Packet;
    const UCHAR *Body = Device->Packet + sizeof(*Header);
    const DCHID_SUBHEADER *Sub = (const DCHID_SUBHEADER *)Body;
    const UCHAR *Payload = Body + sizeof(*Sub);
    ULONG Total = sizeof(*Header) + Header->Length + 4;

    if (MtpChecksum(Device->Packet, Total) != 0xffffffff)
    {
        DbgPrint("applemtp: packet for interface %u failed its checksum\n", Header->Interface);
        return;
    }
    if (Header->Interface >= DCHID_MAX_INTERFACES || Header->Length < sizeof(*Sub) ||
        Sub->Length + sizeof(*Sub) > Header->Length)
        return;

    if (Header->Channel == DCHID_CHANNEL_CMD)
    {
        if (!Device->CommandPending || Device->CommandDone || Header->Interface != Device->CommandInterface ||
            Sub->Flags != Device->CommandFlags || Sub->Length < 1 || Payload[0] != Device->CommandReport ||
            Header->Sequence != Device->Interfaces[Header->Interface].TxSequence)
        {
            DbgPrint("applemtp: unexpected acknowledgement on interface %u\n", Header->Interface);
            return;
        }
        if (Device->ResponseBuffer && Device->ResponseSize)
            RtlCopyMemory(Device->ResponseBuffer, Payload + 1, min((ULONG)Sub->Length - 1, Device->ResponseSize));
        Device->ResponseSize = Sub->Length;
        Device->CommandReturnCode = Sub->ReturnCode;
        Device->CommandDone = TRUE;
        return;
    }

    if ((Sub->Flags >> 6) != DCHID_REPORT_INPUT)
        return;
    if (Header->Interface == DCHID_IFACE_COMM)
    {
        MtpHandleEvent(Device, Payload, Sub->Length);
        return;
    }
    if (Header->Interface == Device->KeyboardInterface && Device->Interfaces[Header->Interface].Enabled)
        MtpQueueKeyboardReport(Device, Payload, Sub->Length);
    else if (Header->Interface == Device->TouchInterface && Device->TouchReady)
        MtpTranslateTouch(Device, Payload, Sub->Length);
}

static
NTSTATUS
MtpQueryNodeInterrupt(
    _In_ PFDT_DRIVER_TREE Tree,
    _In_ ULONG Node,
    _Out_ PULONG Gsi,
    _Out_ PBOOLEAN EdgeTriggered)
{
    const UCHAR *Interrupts;
    const CHAR *Compatible;
    ULONG Length, CompatibleLength, Parent, CellCount, Cell, Phandle;
    ULONG Cells[4];
    ULONG64 Value;

    *Gsi = 0;
    *EdgeTriggered = FALSE;
    if (!FdtReadU32(&Tree->Fdt, Node, "interrupt-parent", &Phandle))
        return STATUS_NOT_FOUND;
    Parent = FdtDriverFindPhandle(Tree, Phandle);
    if (Parent == FDT_NO_NODE ||
        !FdtReadU32(&Tree->Fdt, Parent, "#interrupt-cells", &CellCount) ||
        CellCount == 0 || CellCount > RTL_NUMBER_OF(Cells))
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    Interrupts = FdtGetProperty(&Tree->Fdt, Node, "interrupts", &Length);
    if (!Interrupts || Length < CellCount * sizeof(ULONG))
        return STATUS_NOT_FOUND;
    for (Cell = 0; Cell < CellCount; ++Cell)
    {
        if (!FdtReadCells(Interrupts, Length, Cell, 1, &Value))
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        Cells[Cell] = (ULONG)Value;
    }
    Compatible = FdtGetProperty(&Tree->Fdt, Parent, "compatible", &CompatibleLength);
    if (!Compatible ||
        !HalArm64TranslateDeviceTreeInterrupt(Compatible, CompatibleLength, Cells, CellCount,
                                              Gsi, EdgeTriggered))
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    return STATUS_SUCCESS;
}

static
VOID
MtpQueryChannelIrqBits(
    _In_ PFDT_DRIVER_TREE Tree,
    _In_ ULONG Node,
    _Inout_ PMTP_DEVICE Device)
{
    const UCHAR *Interrupts;
    const CHAR *Names;
    ULONG Length, NamesLength, Offset = 0, Index = 0;
    ULONG64 Value;

    Interrupts = FdtGetProperty(&Tree->Fdt, Node, "interrupts", &Length);
    Names = FdtGetProperty(&Tree->Fdt, Node, "interrupt-names", &NamesLength);
    if (!Interrupts || !Names)
        return;
    while (Offset < NamesLength && (Index + 1) * 2 * sizeof(ULONG) <= Length)
    {
        const CHAR *Name = Names + Offset;
        SIZE_T NameLength = strnlen(Name, NamesLength - Offset);

        if (!FdtReadCells(Interrupts, Length, Index * 2, 1, &Value))
            return;
        if (NameLength == 2 && Name[0] == 'r' && Name[1] == 'x')
            Device->RxIrqBit = 1u << (ULONG)Value;
        else if (NameLength == 2 && Name[0] == 't' && Name[1] == 'x')
            Device->TxIrqBit = 1u << (ULONG)Value;
        Offset += NameLength + 1;
        Index++;
    }
}

static
VOID
NTAPI
MtpDpcRoutine(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID Context,
    _In_opt_ PVOID Argument1,
    _In_opt_ PVOID Argument2)
{
    PMTP_DEVICE Device = Context;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(Argument1);
    UNREFERENCED_PARAMETER(Argument2);
    ASSERT(Device && KeGetCurrentIrql() == DISPATCH_LEVEL);
    if (!InterlockedExchange(&Device->InterruptLogged, 1))
        DbgPrint("applemtp: DockChannel interrupt fired\n");
    KeSetEvent(&Device->DataEvent, IO_NO_INCREMENT, FALSE);
}

static
BOOLEAN
NTAPI
MtpIsr(
    _In_ PKINTERRUPT Interrupt,
    _In_opt_ PVOID Context)
{
    PMTP_DEVICE Device = Context;
    ULONG Pending, Handled;

    UNREFERENCED_PARAMETER(Interrupt);
    if (!Device || !Device->FifoIrq)
        return FALSE;

    Pending = MtpRead32(Device->FifoIrq, DC_IRQ_FLAG);
    Handled = Pending & (Device->RxIrqBit | Device->TxIrqBit);
    if (!Handled)
        return FALSE;

    MtpWrite32(Device->FifoIrq, DC_IRQ_MASK,
               MtpRead32(Device->FifoIrq, DC_IRQ_MASK) & ~Handled);
    MtpWrite32(Device->FifoIrq, DC_IRQ_FLAG, Handled);
    InterlockedIncrement(&Device->Interrupts);
    KeInsertQueueDpc(&Device->Dpc, NULL, NULL);
    return TRUE;
}

static
BOOLEAN
NTAPI
MtpArmReceiveSynchronized(
    _In_opt_ PVOID Context)
{
    PMTP_DEVICE Device = Context;

    MtpWrite32(Device->Config, DC_CONFIG_RX_THRESH, 1);
    MtpWrite32(Device->FifoIrq, DC_IRQ_FLAG, Device->RxIrqBit);
    MtpWrite32(Device->FifoIrq, DC_IRQ_MASK,
               MtpRead32(Device->FifoIrq, DC_IRQ_MASK) | Device->RxIrqBit);
    return TRUE;
}

static
VOID
MtpArmReceive(
    _Inout_ PMTP_DEVICE Device)
{
    if (Device->Interrupt == NULL || Device->RxIrqBit == 0)
        return;
    KeSynchronizeExecution(Device->Interrupt, MtpArmReceiveSynchronized, Device);
}

static
VOID
MtpPollFifo(
    _Inout_ PMTP_DEVICE Device)
{
    ULONG Available, Value;

    for (;;)
    {
        Available = MtpRead32(Device->Data, DC_DATA_RX_COUNT);
        if (!Available)
            return;
        while (Available && Device->PacketFill < Device->PacketNeed)
        {
            if (Available >= 4 && Device->PacketNeed - Device->PacketFill >= 4)
            {
                Value = MtpRead32(Device->Data, DC_DATA_RX32);
                RtlCopyMemory(Device->Packet + Device->PacketFill, &Value, 4);
                Device->PacketFill += 4;
                Available -= 4;
            }
            else
            {
                Device->Packet[Device->PacketFill++] = (UCHAR)(MtpRead32(Device->Data, DC_DATA_RX8) >> 8);
                Available--;
            }
        }
        if (Device->PacketFill < Device->PacketNeed)
            continue;
        if (Device->PacketNeed == sizeof(DCHID_HEADER))
        {
            const DCHID_HEADER *Header = (const DCHID_HEADER *)Device->Packet;

            if (Header->HeaderLength != sizeof(DCHID_HEADER))
            {
                if (!Device->DebuggerPolling)
                    DbgPrint("applemtp: bad DockChannel header length %u\n", Header->HeaderLength);
                Device->PacketFill = 0;
                continue;
            }
            Device->PacketNeed = sizeof(DCHID_HEADER) + Header->Length + 4;
            continue;
        }
        if (Device->DebuggerPolling)
            MtpCaptureDebuggerKeyboard(Device);
        else
            MtpHandlePacket(Device);
        Device->PacketFill = 0;
        Device->PacketNeed = sizeof(DCHID_HEADER);
        if (Device->DebuggerPolling && Device->DebuggerReportValid)
            return;
    }
}

static
NTSTATUS
MtpEnableInterface(
    _Inout_ PMTP_DEVICE Device,
    _In_ UCHAR Index)
{
    UCHAR Message[2] = { DCHID_CMD_ENABLE_INTERFACE, Index };
    NTSTATUS Status;

    Status = MtpCommand(Device, DCHID_IFACE_COMM, DCHID_REPORT_FEATURE, DCHID_REQ_SET_REPORT,
                        Message, sizeof(Message), NULL, 0, NULL);
    DbgPrint("applemtp: enabling interface %s: %lx\n", Device->Interfaces[Index].Name, Status);
    if (NT_SUCCESS(Status))
        Device->Interfaces[Index].Enabled = TRUE;
    return Status;
}

static
ULONG
MtpFindInterfaceNode(
    _In_ PMTP_DEVICE Device,
    _In_ UCHAR Index)
{
    ULONG HidNode, Child, NameLength, Length;
    const CHAR *Name;

    HidNode = FdtDriverFindChild(&Device->Tree, Device->Tree.Node, "apple,dockchannel-hid");
    if (HidNode == FDT_NO_NODE)
        return FDT_NO_NODE;
    Length = (ULONG)strlen(Device->Interfaces[Index].Name);
    for (Child = FdtFirstChild(&Device->Tree.Fdt, HidNode); Child != FDT_NO_NODE;
         Child = FdtNextSibling(&Device->Tree.Fdt, Child))
    {
        Name = FdtNodeName(&Device->Tree.Fdt, Child, &NameLength);
        if (Name && NameLength == Length && !memcmp(Name, Device->Interfaces[Index].Name, Length))
            return Child;
    }
    return FDT_NO_NODE;
}

static
NTSTATUS
MtpLoadFirmware(
    _Inout_ PMTP_DEVICE Device,
    _In_ UCHAR Index,
    _Out_ PMTP_BUFFER Buffer)
{
    ULONG Node, Length, Offset, Magic, Version, HeaderLength, DataLength, InterfaceOffset;
    WCHAR PathBuffer[160];
    UNICODE_STRING Path;
    OBJECT_ATTRIBUTES Attributes;
    IO_STATUS_BLOCK Io;
    FILE_STANDARD_INFORMATION Info;
    const CHAR *Name;
    PUCHAR File = NULL;
    HANDLE Handle;
    NTSTATUS Status;

    RtlZeroMemory(Buffer, sizeof(*Buffer));
    Node = MtpFindInterfaceNode(Device, Index);
    if (Node == FDT_NO_NODE)
        return STATUS_NOT_FOUND;
    Name = FdtGetProperty(&Device->Tree.Fdt, Node, "firmware-name", &Length);
    if (!Name || !Length || Name[Length - 1] != '\0')
        return STATUS_NOT_FOUND;
    Status = RtlStringCbPrintfW(PathBuffer, sizeof(PathBuffer), L"\\SystemRoot\\System32\\drivers\\firmware\\%S", Name);
    if (!NT_SUCCESS(Status))
        return Status;
    for (Offset = 0; PathBuffer[Offset]; ++Offset)
    {
        if (PathBuffer[Offset] == L'/')
            PathBuffer[Offset] = L'\\';
    }
    RtlInitUnicodeString(&Path, PathBuffer);
    InitializeObjectAttributes(&Attributes, &Path, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = ZwCreateFile(&Handle, GENERIC_READ | SYNCHRONIZE, &Attributes, &Io, NULL, FILE_ATTRIBUTE_NORMAL,
                          FILE_SHARE_READ, FILE_OPEN, FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE, NULL, 0);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint("applemtp: cannot open %wZ: %lx\n", &Path, Status);
        return Status;
    }
    Status = ZwQueryInformationFile(Handle, &Io, &Info, sizeof(Info), FileStandardInformation);
    if (NT_SUCCESS(Status) && (Info.EndOfFile.QuadPart < 20 || Info.EndOfFile.QuadPart > 0x100000))
        Status = STATUS_INVALID_IMAGE_FORMAT;
    if (NT_SUCCESS(Status))
    {
        Length = Info.EndOfFile.LowPart;
        File = ExAllocatePoolWithTag(PagedPool, Length, MTP_TAG);
        Status = File ? ZwReadFile(Handle, NULL, NULL, NULL, &Io, File, Length, NULL, NULL) :
                        STATUS_INSUFFICIENT_RESOURCES;
    }
    ZwClose(Handle);
    if (!NT_SUCCESS(Status))
        goto Done;

    RtlCopyMemory(&Magic, File, 4);
    RtlCopyMemory(&Version, File + 4, 4);
    RtlCopyMemory(&HeaderLength, File + 8, 4);
    RtlCopyMemory(&DataLength, File + 12, 4);
    RtlCopyMemory(&InterfaceOffset, File + 16, 4);
    if (Magic != DCHID_FW_MAGIC || Version != 1 || HeaderLength < 20 || HeaderLength > Length ||
        DataLength > Length - HeaderLength || InterfaceOffset >= DataLength)
    {
        DbgPrint("applemtp: %wZ has an invalid header\n", &Path);
        Status = STATUS_INVALID_IMAGE_FORMAT;
        goto Done;
    }
    if (!MtpAllocateDma(DataLength, Buffer) || !MtpDartMap(Device, Buffer))
    {
        MtpFreeDma(Buffer);
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Done;
    }
    RtlCopyMemory(Buffer->Virtual, File + HeaderLength, DataLength);
    if (InterfaceOffset)
        ((PUCHAR)Buffer->Virtual)[InterfaceOffset] = Index;
    Buffer->Size = DataLength;
    KeMemoryBarrier();
    DbgPrint("applemtp: firmware %wZ, %lu bytes at device address %I64x\n", &Path, DataLength, Buffer->Iova);

Done:
    if (File)
        ExFreePoolWithTag(File, MTP_TAG);
    return Status;
}

static
NTSTATUS
MtpCommCommand(
    _Inout_ PMTP_DEVICE Device,
    _In_reads_bytes_(Length) const UCHAR *Message,
    _In_ ULONG Length)
{
    return MtpCommand(Device, DCHID_IFACE_COMM, DCHID_REPORT_FEATURE, DCHID_REQ_SET_REPORT, Message, Length,
                      NULL, 0, NULL);
}

static
VOID
MtpStartTouch(
    _Inout_ PMTP_DEVICE Device,
    _In_ UCHAR Index)
{
    PMTP_INTERFACE Interface = &Device->Interfaces[Index];
    UCHAR Message[16];
    MTP_BUFFER Firmware;
    ULONG64 Address;
    NTSTATUS Status;

    Status = MtpLoadFirmware(Device, Index, &Firmware);
    if (!NT_SUCCESS(Status))
        return;
    RtlZeroMemory(Message, sizeof(Message));
    Message[0] = DCHID_CMD_SEND_FIRMWARE;
    Message[1] = 2;
    Message[2] = 0;
    Message[3] = Index;
    Address = Firmware.Iova;
    RtlCopyMemory(Message + 4, &Address, sizeof(Address));
    RtlCopyMemory(Message + 12, &Firmware.Size, sizeof(ULONG));
    Status = MtpCommCommand(Device, Message, 16);
    DbgPrint("applemtp: sending the %s firmware: %lx\n", Interface->Name, Status);
    if (Device->BufferCount < MTP_MAX_BUFFERS)
        Device->Buffers[Device->BufferCount++] = Firmware;
    if (!NT_SUCCESS(Status))
        return;
    Interface->FirmwareSent = TRUE;
    Interface->Ready = FALSE;
    Message[0] = DCHID_CMD_RESET_INTERFACE;
    Message[1] = 1;
    Message[2] = Index;
    Message[3] = 0;
    MtpCommCommand(Device, Message, 4);
    Message[3] = 2;
    MtpCommCommand(Device, Message, 4);
}

static
NTSTATUS
MtpSmcSetGpio(
    _In_ PAPPLE_SMC_GPIO Gpio)
{
    UNICODE_STRING Name = RTL_CONSTANT_STRING(APPLE_SMC_DEVICE_NAME);
    PFILE_OBJECT FileObject;
    PDEVICE_OBJECT SmcDevice;
    IO_STATUS_BLOCK Io;
    KEVENT Event;
    PIRP Irp;
    NTSTATUS Status;

    Status = IoGetDeviceObjectPointer(&Name, FILE_READ_DATA, &FileObject, &SmcDevice);
    if (!NT_SUCCESS(Status))
        return Status;
    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp = IoBuildDeviceIoControlRequest(IOCTL_APPLE_SMC_SET_GPIO, SmcDevice, Gpio, sizeof(*Gpio), NULL, 0, TRUE,
                                        &Event, &Io);
    if (!Irp)
    {
        ObDereferenceObject(FileObject);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    Status = IoCallDriver(SmcDevice, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = Io.Status;
    }
    ObDereferenceObject(FileObject);
    return Status;
}

static
BOOLEAN
MtpPulseGpio(
    _Inout_ PMTP_DEVICE Device,
    _In_ PMTP_INTERFACE Interface)
{
    CHAR Property[DCHID_MAX_GPIO_NAME + 16];
    APPLE_SMC_GPIO Gpio;
    const UCHAR *Spec;
    ULONG Length, HidNode, Controller, CompatibleLength, Waited;
    const VOID *Compatible;
    BOOLEAN ActiveLow;
    NTSTATUS Status;

    HidNode = FdtDriverFindChild(&Device->Tree, Device->Tree.Node, "apple,dockchannel-hid");
    if (HidNode == FDT_NO_NODE ||
        !NT_SUCCESS(RtlStringCbPrintfA(Property, sizeof(Property), "apple,%s-gpios", Interface->GpioName)))
        return FALSE;
    Spec = FdtGetProperty(&Device->Tree.Fdt, HidNode, Property, &Length);
    if (!Spec || Length < 12)
    {
        DbgPrint("applemtp: no %s property for the GPIO request\n", Property);
        return FALSE;
    }
    Controller = FdtDriverFindPhandle(&Device->Tree, FdtReadBigEndian32(Spec));
    Compatible = Controller == FDT_NO_NODE ? NULL :
                 FdtGetProperty(&Device->Tree.Fdt, Controller, "compatible", &CompatibleLength);
    if (!Compatible)
        return FALSE;
    Gpio.LowBank = FdtStringListContains(Compatible, CompatibleLength, "apple,smc-low-gpio");
    if (!Gpio.LowBank && !FdtStringListContains(Compatible, CompatibleLength, "apple,smc-gpio"))
        return FALSE;
    Gpio.Pin = (UCHAR)FdtReadBigEndian32(Spec + 4);
    ActiveLow = (FdtReadBigEndian32(Spec + 8) & 1) != 0;

    Gpio.Value = !ActiveLow;
    for (Waited = 0; ; Waited += MTP_SMC_WAIT_STEP_MS)
    {
        Status = MtpSmcSetGpio(&Gpio);
        if ((Status != STATUS_OBJECT_NAME_NOT_FOUND && Status != STATUS_DEVICE_NOT_READY) ||
            Waited >= MTP_SMC_WAIT_MS)
            break;
        MtpSleepMs(MTP_SMC_WAIT_STEP_MS);
    }
    if (NT_SUCCESS(Status))
    {
        MtpSleepMs(10);
        Gpio.Value = ActiveLow;
        Status = MtpSmcSetGpio(&Gpio);
    }
    DbgPrint("applemtp: pulsed GPIO %s (pin %u of the %s bank) after %lu ms waiting for the SMC: %lx\n",
             Interface->GpioName, Gpio.Pin, Gpio.LowBank ? "low" : "main", Waited, Status);
    return NT_SUCCESS(Status);
}

static
VOID
MtpServiceGpio(
    _Inout_ PMTP_DEVICE Device)
{
    UCHAR Ack[1 + 4 + sizeof(Device->GpioCommand)];
    ULONG ReturnCode = 0xe000f00d;
    UCHAR InterfaceIndex, Gpio, Command;

    if (!Device->GpioPending)
        return;
    InterfaceIndex = Device->GpioCommandLength > 1 ? Device->GpioCommand[1] : 0xff;
    Gpio = Device->GpioCommandLength > 2 ? Device->GpioCommand[2] : 0xff;
    Command = Device->GpioCommandLength > 4 ? Device->GpioCommand[4] : 0xff;
    DbgPrint("applemtp: GPIO command %u for interface %u GPIO %u\n", Command, InterfaceIndex, Gpio);
    if (InterfaceIndex < DCHID_MAX_INTERFACES && Command == 3 &&
        Device->Interfaces[InterfaceIndex].GpioId == Gpio &&
        MtpPulseGpio(Device, &Device->Interfaces[InterfaceIndex]))
    {
        ReturnCode = 0;
    }
    Ack[0] = DCHID_CMD_ACK_GPIO_CMD;
    RtlCopyMemory(Ack + 1, &ReturnCode, sizeof(ReturnCode));
    RtlCopyMemory(Ack + 5, Device->GpioCommand, Device->GpioCommandLength);
    Device->GpioPending = FALSE;
    MtpCommCommand(Device, Ack, 5 + Device->GpioCommandLength);
}

static
VOID
MtpServiceInterfaces(
    _Inout_ PMTP_DEVICE Device)
{
    ULONG Index;

    MtpServiceGpio(Device);
    for (Index = 1; Index < DCHID_MAX_INTERFACES; ++Index)
    {
        PMTP_INTERFACE Interface = &Device->Interfaces[Index];
        BOOLEAN IsStm = !strcmp(Interface->Name, "stm");
        BOOLEAN IsKeyboard = !strcmp(Interface->Name, "keyboard");
        BOOLEAN IsTouch = !strcmp(Interface->Name, "multi-touch");

        if (IsTouch && Interface->Enabled && !Interface->FirmwareSent)
        {
            Interface->FirmwareSent = TRUE;
            MtpStartTouch(Device, (UCHAR)Index);
            continue;
        }
        if (IsTouch && Interface->Enabled && Interface->Ready && !Device->TouchReady)
        {
            Device->TouchInterface = (UCHAR)Index;
            Device->TouchReady = TRUE;
            DbgPrint("applemtp: trackpad is up on interface %lu\n", Index);
        }
        if (!Interface->InitComplete || Interface->EnableRequested)
            continue;
        if (!IsStm && !IsKeyboard && !IsTouch)
            continue;
        if (!IsStm && !Device->IdReady)
            continue;
        Interface->EnableRequested = TRUE;
        MtpEnableInterface(Device, (UCHAR)Index);
        if (IsKeyboard && Interface->Enabled && Interface->Descriptor)
        {
            Device->KeyboardInterface = (UCHAR)Index;
            KeSetEvent(&Device->KeyboardEvent, IO_NO_INCREMENT, FALSE);
        }
    }

    if (!Device->IdReady)
    {
        for (Index = 1; Index < DCHID_MAX_INTERFACES; ++Index)
        {
            PMTP_INTERFACE Interface = &Device->Interfaces[Index];
            UCHAR Report = DCHID_STM_REPORT_ID;
            ULONG Returned = 0;

            if (strcmp(Interface->Name, "stm") || !Interface->Ready || Device->StmIdRequested)
                continue;
            Device->StmIdRequested = TRUE;
            if (NT_SUCCESS(MtpCommand(Device, (UCHAR)Index, DCHID_REPORT_FEATURE, DCHID_REQ_GET_REPORT, &Report, 1,
                                      (PUCHAR)&Device->StmId, sizeof(Device->StmId), &Returned)) &&
                Returned >= sizeof(Device->StmId))
            {
                DbgPrint("applemtp: keyboard controller %04x:%04x version %04x, keyboard type %u\n",
                         Device->StmId.VendorId, Device->StmId.ProductId, Device->StmId.Version,
                         Device->StmId.KeyboardType);
            }
            else
            {
                RtlZeroMemory(&Device->StmId, sizeof(Device->StmId));
                Device->StmId.VendorId = 0x05ac;
            }
            Device->IdReady = TRUE;
        }
    }
}

static
VOID
NTAPI
MtpWorker(
    _In_ PVOID Context)
{
    PMTP_DEVICE Device = Context;
    LARGE_INTEGER Interval;
    ULONG WaitMs;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    while (!Device->Stop)
    {
        MtpDartCheckFault(Device);
        AppleRtkitPoll(&Device->Rtkit);
        MtpPollFifo(Device);
        MtpServiceInterfaces(Device);
        MtpArmReceive(Device);
        WaitMs = AppleRtkitPollInterval(&Device->Rtkit, MTP_POLL_MS);
        if (!Device->Interrupt || !Device->Interrupts)
            WaitMs = MTP_POLL_MS;
        Interval.QuadPart = -(LONGLONG)WaitMs * 10000;
        KeWaitForSingleObject(&Device->DataEvent, Executive, KernelMode, FALSE,
                              WaitMs == APPLE_RTKIT_NO_POLL ? NULL : &Interval);
    }
    PsTerminateSystemThread(STATUS_SUCCESS);
}

static
VOID
MtpFindDebuggerKeyboardLayout(
    _Inout_ PMTP_DEVICE Device)
{
    const UCHAR *Descriptor = Device->Interfaces[Device->KeyboardInterface].Descriptor;
    ULONG Length = Device->Interfaces[Device->KeyboardInterface].DescriptorLength;
    USHORT InputBits[256] = { 0 };
    ULONG Offset = 0, Size, Value, Index, Page = 0, ReportSize = 0, ReportCount = 0, UsageMin = 0;
    UCHAR ReportId = 0, Item;
    BOOLEAN Modifiers = FALSE, Keys = FALSE;

    Device->DebuggerLayoutKnown = FALSE;
    while (Descriptor && Offset < Length)
    {
        Item = Descriptor[Offset];
        if (Item == 0xfe)
        {
            if (Offset + 2 >= Length)
                break;
            Offset += 3 + Descriptor[Offset + 1];
            continue;
        }
        Size = (Item & 3) == 3 ? 4 : (Item & 3);
        if (Offset + 1 + Size > Length)
            break;
        for (Value = 0, Index = 0; Index < Size; ++Index)
            Value |= (ULONG)Descriptor[Offset + 1 + Index] << (8 * Index);
        switch (Item & 0xfc)
        {
            case 0x04:
                Page = Value;
                break;
            case 0x74:
                ReportSize = Value;
                break;
            case 0x84:
                ReportId = (UCHAR)Value;
                break;
            case 0x94:
                ReportCount = Value;
                break;
            case 0x18:
                UsageMin = Value;
                break;
            case 0x80:
                if (Page == 0x07 && (Value & 2) && UsageMin == 0xe0 && ReportSize == 1 && ReportCount == 8 &&
                    !(InputBits[ReportId] % 8) && !Modifiers)
                {
                    Device->DebuggerReportId = ReportId;
                    Device->DebuggerModifierOffset = (UCHAR)(InputBits[ReportId] / 8);
                    Modifiers = TRUE;
                }
                else if (Page == 0x07 && !(Value & 2) && ReportSize == 8 && ReportCount != 0 &&
                         !(InputBits[ReportId] % 8) && Modifiers && !Keys && ReportId == Device->DebuggerReportId)
                {
                    Device->DebuggerKeysOffset = (UCHAR)(InputBits[ReportId] / 8);
                    Device->DebuggerKeysCount = (UCHAR)min(ReportCount, ROS_USB_KEYBOARD_BOOT_REPORT_SIZE - 2);
                    Keys = TRUE;
                }
                InputBits[ReportId] = (USHORT)min(InputBits[ReportId] + ReportSize * ReportCount, MAXUSHORT);
                UsageMin = 0;
                break;
            case 0x90:
            case 0xa0:
            case 0xb0:
            case 0xc0:
                UsageMin = 0;
                break;
        }
        Offset += 1 + Size;
    }
    Device->DebuggerLayoutKnown = Modifiers && Keys && Device->DebuggerModifierOffset < 64 &&
                                  Device->DebuggerKeysOffset < 64;
}

static
NTSTATUS
NTAPI
MtpDebuggerKeyboardPrepare(
    _In_ PVOID Context)
{
    UNREFERENCED_PARAMETER(Context);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
MtpDebuggerKeyboardPoll(
    _In_ PVOID Context,
    _Out_writes_bytes_(Length) PUCHAR Report,
    _In_ ULONG Length,
    _Out_ PULONG ReturnedLength)
{
    PMTP_DEVICE Device = Context;

    *ReturnedLength = 0;
    if (Length < ROS_USB_KEYBOARD_BOOT_REPORT_SIZE)
        return STATUS_BUFFER_TOO_SMALL;
    if (!Device->Started || !Device->Data || !Device->Packet || !Device->DebuggerLayoutKnown)
        return STATUS_DEVICE_NOT_READY;

    Device->DebuggerReportValid = FALSE;
    Device->DebuggerPolling = TRUE;
    MtpPollFifo(Device);
    Device->DebuggerPolling = FALSE;
    if (!Device->DebuggerReportValid)
        return STATUS_NO_DATA_DETECTED;

    RtlCopyMemory(Report, Device->DebuggerReport, ROS_USB_KEYBOARD_BOOT_REPORT_SIZE);
    *ReturnedLength = ROS_USB_KEYBOARD_BOOT_REPORT_SIZE;
    return STATUS_SUCCESS;
}

static
VOID
NTAPI
MtpDebuggerKeyboardRelease(
    _In_ PVOID Context)
{
    UNREFERENCED_PARAMETER(Context);
}

static
VOID
MtpRegisterDebuggerKeyboard(
    _Inout_ PMTP_DEVICE Device)
{
    UNICODE_STRING CallbackName = RTL_CONSTANT_STRING(ROS_USB_KEYBOARD_CALLBACK_NAME);
    OBJECT_ATTRIBUTES ObjectAttributes;
    NTSTATUS Status;

    MtpFindDebuggerKeyboardLayout(Device);
    if (!Device->DebuggerLayoutKnown)
    {
        DbgPrint("applemtp: the keyboard report has no boot keyboard layout for the kernel debugger\n");
        return;
    }

    RtlZeroMemory(&Device->DebuggerKeyboard, sizeof(Device->DebuggerKeyboard));
    Device->DebuggerKeyboard.Version = ROS_USB_KEYBOARD_INTERFACE_VERSION;
    Device->DebuggerKeyboard.Size = sizeof(Device->DebuggerKeyboard);
    Device->DebuggerKeyboard.Flags = ROS_USB_KEYBOARD_POLL_WHILE_RUNNING;
    Device->DebuggerKeyboard.Context = Device;
    Device->DebuggerKeyboard.Prepare = MtpDebuggerKeyboardPrepare;
    Device->DebuggerKeyboard.Poll = MtpDebuggerKeyboardPoll;
    Device->DebuggerKeyboard.Release = MtpDebuggerKeyboardRelease;

    InitializeObjectAttributes(&ObjectAttributes, &CallbackName, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = ExCreateCallback(&Device->DebuggerKeyboardCallback, &ObjectAttributes, FALSE, TRUE);
    if (!NT_SUCCESS(Status))
    {
        Device->DebuggerKeyboardCallback = NULL;
        DbgPrint("applemtp: the kernel debugger keyboard callback is missing (0x%08lx)\n", Status);
        return;
    }
    ExNotifyCallback(Device->DebuggerKeyboardCallback, &Device->DebuggerKeyboard, (PVOID)TRUE);
    DbgPrint("applemtp: kernel debugger keyboard on report %u, modifiers at %u, %u keys at %u\n",
             Device->DebuggerReportId, Device->DebuggerModifierOffset, Device->DebuggerKeysCount,
             Device->DebuggerKeysOffset);
}

static
VOID
MtpUnregisterDebuggerKeyboard(
    _Inout_ PMTP_DEVICE Device)
{
    if (!Device->DebuggerKeyboardCallback)
        return;
    ExNotifyCallback(Device->DebuggerKeyboardCallback, &Device->DebuggerKeyboard, (PVOID)FALSE);
    ObDereferenceObject(Device->DebuggerKeyboardCallback);
    Device->DebuggerKeyboardCallback = NULL;
}

static
VOID
MtpReleaseHardware(
    _Inout_ PMTP_DEVICE Device)
{
    PIRP Irp;
    KIRQL OldIrql;

    MtpUnregisterDebuggerKeyboard(Device);

    if (Device->Interrupt)
    {
        if (Device->FifoIrq)
            MtpWrite32(Device->FifoIrq, DC_IRQ_MASK, 0);
        IoDisconnectInterrupt(Device->Interrupt);
        Device->Interrupt = NULL;
        KeFlushQueuedDpcs();
    }

    if (Device->Thread)
    {
        InterlockedExchange(&Device->Stop, 1);
        KeSetEvent(&Device->StopEvent, IO_NO_INCREMENT, FALSE);
        KeSetEvent(&Device->DataEvent, IO_NO_INCREMENT, FALSE);
        KeWaitForSingleObject(Device->Thread, Executive, KernelMode, FALSE, NULL);
        ObDereferenceObject(Device->Thread);
        Device->Thread = NULL;
    }
    for (;;)
    {
        KeAcquireSpinLock(&Device->QueueLock, &OldIrql);
        Device->Started = FALSE;
        Irp = NULL;
        while (!IsListEmpty(&Device->PendingReads))
        {
            PLIST_ENTRY Entry = RemoveHeadList(&Device->PendingReads);

            Irp = CONTAINING_RECORD(Entry, IRP, Tail.Overlay.ListEntry);
            if (IoSetCancelRoutine(Irp, NULL))
                break;
            InitializeListHead(&Irp->Tail.Overlay.ListEntry);
            Irp = NULL;
        }
        KeReleaseSpinLock(&Device->QueueLock, OldIrql);
        if (!Irp)
            break;
        Irp->IoStatus.Status = STATUS_DEVICE_NOT_CONNECTED;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
    }
}

static const UCHAR MtpMouseDescriptor[] =
{
    0x05, 0x01, 0x09, 0x02, 0xa1, 0x01, 0x85, 0x00, 0x09, 0x01, 0xa1, 0x00,
    0x05, 0x09, 0x19, 0x01, 0x29, 0x03, 0x15, 0x00, 0x25, 0x01, 0x95, 0x03, 0x75, 0x01, 0x81, 0x02,
    0x95, 0x01, 0x75, 0x05, 0x81, 0x01,
    0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x09, 0x38, 0x15, 0x81, 0x25, 0x7f, 0x75, 0x08, 0x95, 0x03, 0x81, 0x06,
    0xc0, 0xc0
};

static
NTSTATUS
MtpBuildReportDescriptor(
    _Inout_ PMTP_DEVICE Device)
{
    const UCHAR *Keyboard = Device->Interfaces[Device->KeyboardInterface].Descriptor;
    ULONG KeyboardLength = Device->Interfaces[Device->KeyboardInterface].DescriptorLength;
    UCHAR Used[256] = { 0 };
    ULONG Offset = 0, Size, Prefix, Length;
    PUCHAR Out;

    while (Offset < KeyboardLength)
    {
        UCHAR Item = Keyboard[Offset];

        if (Item == 0xfe)
        {
            if (Offset + 2 >= KeyboardLength)
                break;
            Offset += 3 + Keyboard[Offset + 1];
            continue;
        }
        Size = (Item & 3) == 3 ? 4 : (Item & 3);
        if ((Item & 0xfc) == 0x84 && Size >= 1 && Offset + 1 < KeyboardLength)
        {
            Used[Keyboard[Offset + 1]] = 1;
            Device->KeyboardUsesReportIds = TRUE;
        }
        Offset += 1 + Size;
    }
    Device->MouseReportId = 0x7e;
    while (Device->MouseReportId > 1 && Used[Device->MouseReportId])
        Device->MouseReportId--;

    Prefix = Device->KeyboardUsesReportIds ? 0 : 2;
    Length = Prefix + KeyboardLength + sizeof(MtpMouseDescriptor);
    Out = ExAllocatePoolWithTag(NonPagedPool, Length, MTP_TAG);
    if (!Out)
        return STATUS_INSUFFICIENT_RESOURCES;
    if (Prefix)
    {
        Out[0] = 0x85;
        Out[1] = 0x01;
        if (Device->MouseReportId == 1)
            Device->MouseReportId = 2;
    }
    RtlCopyMemory(Out + Prefix, Keyboard, KeyboardLength);
    RtlCopyMemory(Out + Prefix + KeyboardLength, MtpMouseDescriptor, sizeof(MtpMouseDescriptor));
    Out[Prefix + KeyboardLength + 7] = Device->MouseReportId;
    Device->ReportDescriptor = Out;
    Device->ReportDescriptorLength = Length;
    DbgPrint("applemtp: report descriptor: keyboard %lu bytes%s, trackpad as mouse report %u\n", KeyboardLength,
             Device->KeyboardUsesReportIds ? "" : " given report 1", Device->MouseReportId);
    return STATUS_SUCCESS;
}

static
VOID
MtpUnmap(
    _Inout_ PUCHAR *Base,
    _In_ SIZE_T Size)
{
    if (*Base)
        MmUnmapIoSpace(*Base, Size);
    *Base = NULL;
}

static
VOID
MtpTeardown(
    _Inout_ PMTP_DEVICE Device)
{
    ULONG Index;

    if (Device->Dart && Device->DartReady)
        MtpWrite32(Device->Dart, DART_T8110_TCR + 4 * Device->DartSid, 0);
    Device->DartReady = FALSE;
    MtpUnmap(&Device->Asc, Device->AscSize);
    MtpUnmap(&Device->Sram, Device->SramSize);
    AppleRtkitDisconnectInterrupt(&Device->Rtkit);
    MtpUnmap(&Device->Mbox, Device->MboxSize);
    MtpUnmap(&Device->Dart, Device->DartSize);
    MtpUnmap(&Device->FifoIrq, Device->FifoIrqSize);
    MtpUnmap(&Device->Config, Device->ConfigSize);
    MtpUnmap(&Device->Data, Device->DataSize);
    for (Index = 0; Index < Device->BufferCount; ++Index)
        MtpFreeDma(&Device->Buffers[Index]);
    Device->BufferCount = 0;
    for (Index = 0; Index < Device->DartL2Count; ++Index)
        MtpFreeDma(&Device->DartL2[Index]);
    Device->DartL2Count = 0;
    MtpFreeDma(&Device->DartL1);
    for (Index = 0; Index < DCHID_MAX_INTERFACES; ++Index)
    {
        if (Device->Interfaces[Index].Descriptor)
            ExFreePoolWithTag(Device->Interfaces[Index].Descriptor, MTP_TAG);
    }
    RtlZeroMemory(Device->Interfaces, sizeof(Device->Interfaces));
    if (Device->Packet)
        ExFreePoolWithTag(Device->Packet, MTP_TAG);
    Device->Packet = NULL;
    FdtDriverClose(&Device->Tree);
    if (Device->ReportDescriptor)
        ExFreePoolWithTag(Device->ReportDescriptor, MTP_TAG);
    Device->ReportDescriptor = NULL;
    Device->ReportDescriptorLength = 0;
}

static
NTSTATUS
MtpStartHardware(
    _Inout_ PMTP_DEVICE Device,
    _In_opt_ PCM_RESOURCE_LIST Raw,
    _In_opt_ PCM_RESOURCE_LIST Translated)
{
    ULONG FifoNode, HidNode, HelperNode, MboxNode, DartNode, Phandle, Length, Gsi;
    const UCHAR *Iommus;
    HANDLE ThreadHandle;
    LARGE_INTEGER Timeout;
    BOOLEAN Edge;
    NTSTATUS Status;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    ASSERT(!Device->Thread && !Device->Rtkit.Interrupt);

    Status = FdtDriverOpen(Device->Pdo, &Device->Tree);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint("applemtp: device tree unavailable: %lx\n", Status);
        return Status;
    }
    FifoNode = Device->Tree.Node;
    HidNode = FdtDriverFindChild(&Device->Tree, FifoNode, "apple,dockchannel-hid");
    if (HidNode == FDT_NO_NODE ||
        !FdtReadU32(&Device->Tree.Fdt, HidNode, "apple,helper-cpu", &Phandle) ||
        (HelperNode = FdtDriverFindPhandle(&Device->Tree, Phandle)) == FDT_NO_NODE ||
        !FdtReadU32(&Device->Tree.Fdt, HelperNode, "mboxes", &Phandle) ||
        (MboxNode = FdtDriverFindPhandle(&Device->Tree, Phandle)) == FDT_NO_NODE)
    {
        DbgPrint("applemtp: the DockChannel HID description is incomplete\n");
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    Iommus = FdtGetProperty(&Device->Tree.Fdt, HidNode, "iommus", &Length);
    if (!Iommus || Length < 8 ||
        (DartNode = FdtDriverFindPhandle(&Device->Tree, FdtReadBigEndian32(Iommus))) == FDT_NO_NODE)
    {
        DbgPrint("applemtp: the DockChannel HID has no DART\n");
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    Device->DartSid = FdtReadBigEndian32(Iommus + 4);
    if (!FdtReadU32(&Device->Tree.Fdt, HidNode, "apple,fifo-size", &Device->FifoSize))
        Device->FifoSize = 0x800;

    Device->Asc = MtpMap(Device, HelperNode, 0, "MTP ASC", NULL, &Device->AscSize);
    Device->Sram = MtpMap(Device, HelperNode, 1, "MTP SRAM", &Device->SramBase, &Device->SramSize);
    Device->Mbox = MtpMap(Device, MboxNode, 0, "MTP mailbox", NULL, &Device->MboxSize);
    Device->Dart = MtpMap(Device, DartNode, 0, "MTP DART", NULL, &Device->DartSize);
    Device->FifoIrq = MtpMap(Device, FifoNode, 0, "DockChannel interrupts", NULL, &Device->FifoIrqSize);
    Device->Config = MtpMap(Device, HidNode, 2, "DockChannel configuration", NULL, &Device->ConfigSize);
    Device->Data = MtpMap(Device, HidNode, 3, "DockChannel data", NULL, &Device->DataSize);
    if (!Device->Asc || !Device->Mbox || !Device->Dart || !Device->FifoIrq || !Device->Config || !Device->Data)
        return STATUS_INSUFFICIENT_RESOURCES;

    MtpDartInitialize(Device);
    MtpWrite32(Device->FifoIrq, DC_IRQ_MASK, 0);
    MtpWrite32(Device->FifoIrq, DC_IRQ_FLAG, 0xffffffff);

    Device->Packet = ExAllocatePoolWithTag(NonPagedPool, DCHID_MAX_PACKET, MTP_TAG);
    if (!Device->Packet)
        return STATUS_INSUFFICIENT_RESOURCES;
    Device->PacketFill = 0;
    Device->PacketNeed = sizeof(DCHID_HEADER);
    RtlCopyMemory(Device->Interfaces[0].Name, "comm", 5);

    KeInitializeEvent(&Device->DataEvent, SynchronizationEvent, FALSE);
    Device->Rtkit.Mailbox = Device->Mbox;
    Device->Rtkit.Name = "applemtp";
    Status = AppleRtkitQueryInterrupt(&Device->Tree, HelperNode, &Gsi, &Edge);
    if (NT_SUCCESS(Status))
        AppleRtkitConnectInterrupt(&Device->Rtkit, Raw, Translated, Gsi, &Device->DataEvent);
    else
        DbgPrint("applemtp: the MTP mailbox declares no receive interrupt (%lx), polling\n", Status);
    Status = MtpBootCoprocessor(Device);
    if (!NT_SUCCESS(Status))
        return Status;

    KeInitializeEvent(&Device->StopEvent, NotificationEvent, FALSE);
    KeInitializeEvent(&Device->KeyboardEvent, NotificationEvent, FALSE);
    KeInitializeDpc(&Device->Dpc, MtpDpcRoutine, Device);
    MtpQueryChannelIrqBits(&Device->Tree, HidNode, Device);
    Device->Stop = 0;
    Status = PsCreateSystemThread(&ThreadHandle, THREAD_ALL_ACCESS, NULL, NULL, NULL, MtpWorker, Device);
    if (!NT_SUCCESS(Status))
        return Status;
    ObReferenceObjectByHandle(ThreadHandle, THREAD_ALL_ACCESS, NULL, KernelMode, (PVOID *)&Device->Thread, NULL);
    ZwClose(ThreadHandle);

    Timeout.QuadPart = -(LONGLONG)MTP_KEYBOARD_TIMEOUT_MS * 10000;
    Status = KeWaitForSingleObject(&Device->KeyboardEvent, Executive, KernelMode, FALSE, &Timeout);
    if (Status == STATUS_TIMEOUT)
    {
        ULONG Index;

        DbgPrint("applemtp: the keyboard did not come up\n");
        for (Index = 0; Index < DCHID_MAX_INTERFACES; ++Index)
        {
            if (Device->Interfaces[Index].Present)
                DbgPrint("applemtp: interface %lu %s: init %u enabled %u ready %u\n", Index,
                         Device->Interfaces[Index].Name, Device->Interfaces[Index].InitComplete,
                         Device->Interfaces[Index].Enabled, Device->Interfaces[Index].Ready);
        }
        MtpReleaseHardware(Device);
        return STATUS_DEVICE_NOT_READY;
    }
    Status = MtpBuildReportDescriptor(Device);
    if (!NT_SUCCESS(Status))
    {
        MtpReleaseHardware(Device);
        return Status;
    }
    Device->Started = TRUE;
    DbgPrint("applemtp: keyboard is up on interface %u with a %lu byte report descriptor\n",
             Device->KeyboardInterface, Device->ReportDescriptorLength);
    MtpRegisterDebuggerKeyboard(Device);
    return STATUS_SUCCESS;
}

static
VOID
NTAPI
MtpCancelRead(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PMTP_DEVICE Device = MtpGetDevice(DeviceObject);
    KIRQL OldIrql;

    IoReleaseCancelSpinLock(Irp->CancelIrql);
    KeAcquireSpinLock(&Device->QueueLock, &OldIrql);
    RemoveEntryList(&Irp->Tail.Overlay.ListEntry);
    KeReleaseSpinLock(&Device->QueueLock, OldIrql);
    Irp->IoStatus.Status = STATUS_CANCELLED;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
}

static
NTSTATUS
MtpReadReport(
    _In_ PMTP_DEVICE Device,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    KIRQL OldIrql;
    ULONG Length;

    if (!Irp->UserBuffer || !Stack->Parameters.DeviceIoControl.OutputBufferLength)
    {
        Irp->IoStatus.Status = STATUS_INVALID_USER_BUFFER;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_INVALID_USER_BUFFER;
    }
    KeAcquireSpinLock(&Device->QueueLock, &OldIrql);
    if (!Device->Started)
    {
        KeReleaseSpinLock(&Device->QueueLock, OldIrql);
        Irp->IoStatus.Status = STATUS_DEVICE_NOT_READY;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_DEVICE_NOT_READY;
    }
    if (Device->ReportCount)
    {
        Length = min((ULONG)Device->ReportLength[Device->ReportHead], Stack->Parameters.DeviceIoControl.OutputBufferLength);
        RtlCopyMemory(Irp->UserBuffer, Device->ReportQueue[Device->ReportHead], Length);
        Device->ReportHead = (Device->ReportHead + 1) % MTP_QUEUED_REPORTS;
        Device->ReportCount--;
        KeReleaseSpinLock(&Device->QueueLock, OldIrql);
        Irp->IoStatus.Information = Length;
        Irp->IoStatus.Status = STATUS_SUCCESS;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_SUCCESS;
    }
    IoMarkIrpPending(Irp);
    InsertTailList(&Device->PendingReads, &Irp->Tail.Overlay.ListEntry);
    IoSetCancelRoutine(Irp, MtpCancelRead);
    if (Irp->Cancel && IoSetCancelRoutine(Irp, NULL))
    {
        RemoveEntryList(&Irp->Tail.Overlay.ListEntry);
        KeReleaseSpinLock(&Device->QueueLock, OldIrql);
        Irp->IoStatus.Status = STATUS_CANCELLED;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_PENDING;
    }
    KeReleaseSpinLock(&Device->QueueLock, OldIrql);
    return STATUS_PENDING;
}

static
NTSTATUS
MtpCompleteWithBuffer(
    _Inout_ PIRP Irp,
    _In_reads_bytes_(Length) const VOID *Data,
    _In_ ULONG Length)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status = STATUS_SUCCESS;

    if (!Irp->UserBuffer || Stack->Parameters.DeviceIoControl.OutputBufferLength < Length)
    {
        Status = STATUS_BUFFER_TOO_SMALL;
        Irp->IoStatus.Information = 0;
    }
    else
    {
        RtlCopyMemory(Irp->UserBuffer, Data, Length);
        Irp->IoStatus.Information = Length;
    }
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

static
NTSTATUS
NTAPI
MtpInternalDeviceControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PMTP_DEVICE Device = MtpGetDevice(DeviceObject);
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    ULONG Code = Stack->Parameters.DeviceIoControl.IoControlCode;
    NTSTATUS Status;

    if (Code == IOCTL_HID_READ_REPORT)
        return MtpReadReport(Device, Irp);

    Irp->IoStatus.Information = 0;
    if (!Device->Started)
    {
        Irp->IoStatus.Status = STATUS_DEVICE_NOT_READY;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_DEVICE_NOT_READY;
    }
    switch (Code)
    {
        case IOCTL_HID_GET_DEVICE_DESCRIPTOR:
        {
            HID_DESCRIPTOR Descriptor;

            RtlZeroMemory(&Descriptor, sizeof(Descriptor));
            Descriptor.bLength = sizeof(Descriptor);
            Descriptor.bDescriptorType = HID_HID_DESCRIPTOR_TYPE;
            Descriptor.bcdHID = 0x0111;
            Descriptor.bNumDescriptors = 1;
            Descriptor.DescriptorList[0].bReportType = HID_REPORT_DESCRIPTOR_TYPE;
            Descriptor.DescriptorList[0].wReportLength = (USHORT)Device->ReportDescriptorLength;
            return MtpCompleteWithBuffer(Irp, &Descriptor, sizeof(Descriptor));
        }

        case IOCTL_HID_GET_REPORT_DESCRIPTOR:
            return MtpCompleteWithBuffer(Irp, Device->ReportDescriptor, Device->ReportDescriptorLength);

        case IOCTL_HID_GET_DEVICE_ATTRIBUTES:
        {
            HID_DEVICE_ATTRIBUTES Attributes;

            RtlZeroMemory(&Attributes, sizeof(Attributes));
            Attributes.Size = sizeof(Attributes);
            Attributes.VendorID = Device->StmId.VendorId ? Device->StmId.VendorId : 0x05ac;
            Attributes.ProductID = Device->StmId.ProductId;
            Attributes.VersionNumber = Device->StmId.Version;
            return MtpCompleteWithBuffer(Irp, &Attributes, sizeof(Attributes));
        }

        case IOCTL_HID_ACTIVATE_DEVICE:
        case IOCTL_HID_DEACTIVATE_DEVICE:
            Status = STATUS_SUCCESS;
            break;

        default:
            Status = STATUS_NOT_SUPPORTED;
            break;
    }
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

static
NTSTATUS
NTAPI
MtpPnpCompletion(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_ PVOID Context)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Irp);
    KeSetEvent((PKEVENT)Context, IO_NO_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

static
NTSTATUS
MtpFilterResourceRequirements(
    _Inout_ PMTP_DEVICE Device,
    _Inout_ PIRP Irp)
{
    FDT_DRIVER_TREE Tree;
    ULONG Gsi;
    BOOLEAN Edge = FALSE;
    NTSTATUS Status;

    Status = FdtDriverOpen(Device->Pdo, &Tree);
    if (!NT_SUCCESS(Status))
        return STATUS_NOT_SUPPORTED;
    Status = MtpQueryNodeInterrupt(&Tree, Tree.Node, &Gsi, &Edge);
    FdtDriverClose(&Tree);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint("applemtp: the DockChannel node declares no interrupt (%lx), polling\n", Status);
        return STATUS_NOT_SUPPORTED;
    }

    Device->FifoGsi = Gsi;
    Device->FifoEdge = Edge;
    Status = AppleAddInterruptRequirement(Irp, Gsi, Edge);
    DbgPrint("applemtp: requesting the DockChannel interrupt %lu: %lx\n", Gsi, Status);
    return Status;
}

static
NTSTATUS
MtpAddMailboxRequirement(
    _Inout_ PMTP_DEVICE Device,
    _Inout_ PIRP Irp)
{
    FDT_DRIVER_TREE Tree;
    ULONG HidNode, HelperNode = FDT_NO_NODE, Phandle, Gsi = 0;
    BOOLEAN Edge = FALSE;
    NTSTATUS Status;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    Status = FdtDriverOpen(Device->Pdo, &Tree);
    if (!NT_SUCCESS(Status))
        return Status;
    HidNode = FdtDriverFindChild(&Tree, Tree.Node, "apple,dockchannel-hid");
    if (HidNode != FDT_NO_NODE && FdtReadU32(&Tree.Fdt, HidNode, "apple,helper-cpu", &Phandle))
        HelperNode = FdtDriverFindPhandle(&Tree, Phandle);
    Status = (HelperNode != FDT_NO_NODE) ? AppleRtkitQueryInterrupt(&Tree, HelperNode, &Gsi, &Edge)
                                         : STATUS_DEVICE_CONFIGURATION_ERROR;
    FdtDriverClose(&Tree);
    if (NT_SUCCESS(Status))
        Status = AppleAddInterruptRequirement(Irp, Gsi, Edge);
    DbgPrint("applemtp: requesting the mailbox interrupt %lu: %lx\n", Gsi, Status);
    return Status;
}

static
VOID
MtpConnectInterrupt(
    _Inout_ PMTP_DEVICE Device,
    _In_opt_ PCM_RESOURCE_LIST Raw,
    _In_opt_ PCM_RESOURCE_LIST Translated)
{
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Partial;
    ULONG Index;
    NTSTATUS Status;

    if (Device->RxIrqBit == 0)
    {
        DbgPrint("applemtp: the DockChannel HID names no receive interrupt, polling\n");
        return;
    }
    if (Raw == NULL || Translated == NULL || Device->Interrupt != NULL ||
        Raw->List[0].PartialResourceList.Count != Translated->List[0].PartialResourceList.Count)
        return;

    for (Index = 0; Index < Translated->List[0].PartialResourceList.Count; ++Index)
    {
        Partial = &Translated->List[0].PartialResourceList.PartialDescriptors[Index];
        if (Partial->Type != CmResourceTypeInterrupt ||
            Raw->List[0].PartialResourceList.PartialDescriptors[Index].u.Interrupt.Vector != Device->FifoGsi)
            continue;
        Status = IoConnectInterrupt(&Device->Interrupt, MtpIsr, Device, NULL,
                                    Partial->u.Interrupt.Vector,
                                    (KIRQL)Partial->u.Interrupt.Level,
                                    (KIRQL)Partial->u.Interrupt.Level,
                                    (Partial->Flags & CM_RESOURCE_INTERRUPT_LATCHED) ? Latched
                                                                                     : LevelSensitive,
                                    TRUE, Partial->u.Interrupt.Affinity, FALSE);
        DbgPrint("applemtp: DockChannel interrupt vector %lu level %lu connected: %lx, rx bit %08lx\n",
                 Partial->u.Interrupt.Vector, Partial->u.Interrupt.Level, Status, Device->RxIrqBit);
        if (NT_SUCCESS(Status))
        {
            MtpArmReceive(Device);
            return;
        }
        Device->Interrupt = NULL;
    }
    DbgPrint("applemtp: no DockChannel interrupt was assigned, polling\n");
}

static
NTSTATUS
NTAPI
MtpPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PMTP_DEVICE Device = MtpGetDevice(DeviceObject);
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    KEVENT Event;
    NTSTATUS Status;

    switch (Stack->MinorFunction)
    {
        case IRP_MN_FILTER_RESOURCE_REQUIREMENTS:
            KeInitializeEvent(&Event, NotificationEvent, FALSE);
            IoCopyCurrentIrpStackLocationToNext(Irp);
            IoSetCompletionRoutine(Irp, MtpPnpCompletion, &Event, TRUE, TRUE, TRUE);
            Status = IoCallDriver(Device->LowerDevice, Irp);
            if (Status == STATUS_PENDING)
            {
                KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
                Status = Irp->IoStatus.Status;
            }
            if (NT_SUCCESS(Status) || Status == STATUS_NOT_SUPPORTED)
            {
                if (NT_SUCCESS(MtpFilterResourceRequirements(Device, Irp)))
                    Status = STATUS_SUCCESS;
                if (NT_SUCCESS(MtpAddMailboxRequirement(Device, Irp)))
                    Status = STATUS_SUCCESS;
            }
            Irp->IoStatus.Status = Status;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;

        case IRP_MN_START_DEVICE:
            KeInitializeEvent(&Event, NotificationEvent, FALSE);
            IoCopyCurrentIrpStackLocationToNext(Irp);
            IoSetCompletionRoutine(Irp, MtpPnpCompletion, &Event, TRUE, TRUE, TRUE);
            Status = IoCallDriver(Device->LowerDevice, Irp);
            if (Status == STATUS_PENDING)
            {
                KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
                Status = Irp->IoStatus.Status;
            }
            if (NT_SUCCESS(Status))
            {
                Status = MtpStartHardware(Device, Stack->Parameters.StartDevice.AllocatedResources,
                                          Stack->Parameters.StartDevice.AllocatedResourcesTranslated);
                if (NT_SUCCESS(Status))
                    MtpConnectInterrupt(Device, Stack->Parameters.StartDevice.AllocatedResources,
                                        Stack->Parameters.StartDevice.AllocatedResourcesTranslated);
                else
                    MtpTeardown(Device);
            }
            Irp->IoStatus.Status = Status;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;

        case IRP_MN_STOP_DEVICE:
        case IRP_MN_SURPRISE_REMOVAL:
        case IRP_MN_REMOVE_DEVICE:
            MtpReleaseHardware(Device);
            MtpTeardown(Device);
            Irp->IoStatus.Status = STATUS_SUCCESS;
            break;

        case IRP_MN_QUERY_STOP_DEVICE:
        case IRP_MN_QUERY_REMOVE_DEVICE:
            Irp->IoStatus.Status = STATUS_UNSUCCESSFUL;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return STATUS_UNSUCCESSFUL;
    }
    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(Device->LowerDevice, Irp);
}

static
NTSTATUS
NTAPI
MtpPower(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PMTP_DEVICE Device = MtpGetDevice(DeviceObject);

    PoStartNextPowerIrp(Irp);
    IoSkipCurrentIrpStackLocation(Irp);
    return PoCallDriver(Device->LowerDevice, Irp);
}

static
NTSTATUS
NTAPI
MtpPassThrough(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PMTP_DEVICE Device = MtpGetDevice(DeviceObject);

    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(Device->LowerDevice, Irp);
}

static
NTSTATUS
NTAPI
MtpCreateClose(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
MtpAddDevice(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT DeviceObject)
{
    PHID_DEVICE_EXTENSION HidExtension = DeviceObject->DeviceExtension;
    PMTP_DEVICE Device = HidExtension->MiniDeviceExtension;

    UNREFERENCED_PARAMETER(DriverObject);
    RtlZeroMemory(Device, sizeof(*Device));
    Device->DeviceObject = DeviceObject;
    Device->LowerDevice = HidExtension->NextDeviceObject;
    Device->Pdo = HidExtension->PhysicalDeviceObject;
    KeInitializeSpinLock(&Device->QueueLock);
    InitializeListHead(&Device->PendingReads);
    return STATUS_SUCCESS;
}

static
VOID
NTAPI
MtpUnload(
    _In_ PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);
}

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    HID_MINIDRIVER_REGISTRATION Registration;

    DriverObject->MajorFunction[IRP_MJ_CREATE] = MtpCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = MtpCreateClose;
    DriverObject->MajorFunction[IRP_MJ_INTERNAL_DEVICE_CONTROL] = MtpInternalDeviceControl;
    DriverObject->MajorFunction[IRP_MJ_PNP] = MtpPnp;
    DriverObject->MajorFunction[IRP_MJ_POWER] = MtpPower;
    DriverObject->MajorFunction[IRP_MJ_SYSTEM_CONTROL] = MtpPassThrough;
    DriverObject->DriverExtension->AddDevice = MtpAddDevice;
    DriverObject->DriverUnload = MtpUnload;

    RtlZeroMemory(&Registration, sizeof(Registration));
    Registration.Revision = HID_REVISION;
    Registration.DriverObject = DriverObject;
    Registration.RegistryPath = RegistryPath;
    Registration.DeviceExtensionSize = sizeof(MTP_DEVICE);
    Registration.DevicesArePolled = FALSE;
    return HidRegisterMinidriver(&Registration);
}
