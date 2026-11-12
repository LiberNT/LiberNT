/*
 * PROJECT:     LiberNT Storport NVMe miniport
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Apple ANS storage controller of Apple silicon machines
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "stornvme.h"
#include <reactos/drivers/fdtdrv.h>
#include <reactos/drivers/applertkit.h>
#include <reactos/drivers/applepmgr.h>

#define ANS_TAG 'snAA'

#define ANS_CPU_CONTROL             0x44
#define ANS_CPU_CONTROL_RUN         0x10
#define ANS_ACQ_DB                  0x1004
#define ANS_IOCQ_DB                 0x100c
#define ANS_MAX_PEND_CMDS_CTRL      0x1210
#define ANS_BOOT_STATUS             0x1300
#define ANS_BOOT_STATUS_OK          0xde71ce55
#define ANS_LINEAR_SQ_CTRL          0x24908
#define ANS_LINEAR_SQ_EN            0x1
#define ANS_LINEAR_ASQ_DB           0x2490c
#define ANS_LINEAR_IOSQ_DB          0x24910
#define ANS_NVMMU_NUM_TCBS          0x28100
#define ANS_NVMMU_ASQ_TCB_BASE      0x28108
#define ANS_NVMMU_IOSQ_TCB_BASE     0x28110
#define ANS_NVMMU_TCB_INVAL         0x28118
#define ANS_NVMMU_TCB_STAT          0x28120

#define ANS_QUEUE_DEPTH             64
#define ANS_ADMIN_DEPTH             2
#define ANS_TCB_DMA_FROM_DEVICE     0x1
#define ANS_TCB_DMA_TO_DEVICE       0x2
#define ANS_BOOT_TIMEOUT_MS         1000
#define ANS_POLL_INTERVAL_US        2000

#define SART_MAX_ENTRIES            16
#define SART_CONFIG(Index)          (0x00 + 4 * (Index))
#define SART_PADDR(Index)           (0x40 + 4 * (Index))
#define SART_SIZE(Index)            (0x80 + 4 * (Index))
#define SART_FLAGS_ALLOW            0xff

#define ANS_MAX_BUFFERS             8
#define ANS_BUFFER_ALIGNMENT        0x4000

#pragma pack(push, 1)
typedef struct _ANS_TCB
{
    UCHAR Opcode;
    UCHAR DmaFlags;
    UCHAR CommandId;
    UCHAR Reserved0;
    USHORT Length;
    UCHAR Reserved1[18];
    ULONGLONG Prp1;
    ULONGLONG Prp2;
    UCHAR Reserved2[16];
    UCHAR AesIv[8];
    UCHAR AesReserved[64];
} ANS_TCB, *PANS_TCB;
#pragma pack(pop)

C_ASSERT(sizeof(ANS_TCB) == 128);

typedef struct _ANS_BUFFER
{
    PVOID Allocation;
    ULONG AllocationSize;
    PVOID Virtual;
    ULONGLONG Physical;
    ULONG Size;
    ULONG SartIndex;
} ANS_BUFFER, *PANS_BUFFER;

typedef struct _ANS_CONTEXT
{
    FDT_DRIVER_TREE Tree;
    PUCHAR Coproc;
    ULONG CoprocSize;
    PUCHAR Mailbox;
    ULONG MailboxSize;
    PUCHAR Sart;
    ULONG SartSize;
    APPLE_RTKIT Rtkit;
    PANS_TCB AdminTcb;
    ULONGLONG AdminTcbPhysical;
    PANS_TCB IoTcb;
    ULONGLONG IoTcbPhysical;
    ANS_BUFFER Buffers[ANS_MAX_BUFFERS];
    ULONG BufferCount;
} ANS_CONTEXT, *PANS_CONTEXT;

static
PANS_CONTEXT
AnsContext(
    _In_ PNVME_DEVICE_EXTENSION Device)
{
    return (PANS_CONTEXT)Device->TransportContext;
}

static
BOOLEAN
AnsSartAllow(
    _Inout_ PANS_CONTEXT Ans,
    _In_ ULONGLONG Physical,
    _In_ ULONG Size,
    _Out_ PULONG Entry)
{
    ULONG Index;

    for (Index = 0; Index < SART_MAX_ENTRIES; ++Index)
    {
        if (READ_REGISTER_ULONG((volatile ULONG *)(Ans->Sart + SART_CONFIG(Index))))
            continue;
        WRITE_REGISTER_ULONG((volatile ULONG *)(Ans->Sart + SART_PADDR(Index)), (ULONG)(Physical >> 12));
        WRITE_REGISTER_ULONG((volatile ULONG *)(Ans->Sart + SART_SIZE(Index)), Size >> 12);
        WRITE_REGISTER_ULONG((volatile ULONG *)(Ans->Sart + SART_CONFIG(Index)), SART_FLAGS_ALLOW);
        DPRINT1("stornvme: ANS may reach %I64x+%lx through SART entry %lu\n", Physical, Size, Index);
        *Entry = Index;
        return TRUE;
    }
    DPRINT1("stornvme: no free SART entry for the ANS buffer\n");
    return FALSE;
}

static
BOOLEAN
NTAPI
AnsShmemSetup(
    _In_ PVOID Context,
    _In_ UCHAR Endpoint,
    _In_ ULONG Size,
    _Inout_ PULONG64 Iova,
    _Out_ PVOID *Virtual,
    _Out_ PBOOLEAN Preassigned)
{
    PANS_CONTEXT Ans = Context;
    PHYSICAL_ADDRESS Low, High, Boundary;
    PANS_BUFFER Buffer;
    ULONGLONG Physical;
    ULONG Skip;

    UNREFERENCED_PARAMETER(Endpoint);
    *Virtual = NULL;
    *Preassigned = FALSE;
    if (*Iova || Ans->BufferCount == ANS_MAX_BUFFERS)
        return FALSE;
    Size = (Size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    Low.QuadPart = 0;
    High.QuadPart = -1;
    Boundary.QuadPart = 0;
    Buffer = &Ans->Buffers[Ans->BufferCount];
    Buffer->AllocationSize = Size + ANS_BUFFER_ALIGNMENT - PAGE_SIZE;
    Buffer->Allocation = MmAllocateContiguousMemorySpecifyCache(Buffer->AllocationSize, Low, High, Boundary,
                                                                MmNonCached);
    if (!Buffer->Allocation)
        return FALSE;
    Physical = (ULONGLONG)MmGetPhysicalAddress(Buffer->Allocation).QuadPart;
    Skip = (ULONG)(((Physical + ANS_BUFFER_ALIGNMENT - 1) & ~(ULONGLONG)(ANS_BUFFER_ALIGNMENT - 1)) - Physical);
    Buffer->Virtual = (PUCHAR)Buffer->Allocation + Skip;
    Buffer->Physical = Physical + Skip;
    RtlZeroMemory(Buffer->Virtual, Size);
    Buffer->Size = Size;
    if (!AnsSartAllow(Ans, Buffer->Physical, Size, &Buffer->SartIndex))
    {
        MmFreeContiguousMemorySpecifyCache(Buffer->Allocation, Buffer->AllocationSize, MmNonCached);
        RtlZeroMemory(Buffer, sizeof(*Buffer));
        return FALSE;
    }
    Ans->BufferCount++;
    *Iova = Buffer->Physical;
    *Virtual = Buffer->Virtual;
    return TRUE;
}

static
VOID
AnsSubmit(
    _In_ PNVME_DEVICE_EXTENSION Device,
    _In_ PNVME_QUEUE Queue,
    _In_ ULONG Slot,
    _In_ PNVME_COMMAND Command)
{
    PANS_CONTEXT Ans = AnsContext(Device);
    PANS_TCB Tcb = (Queue->QueueId == 0 ? Ans->AdminTcb : Ans->IoTcb) + Slot;
    UCHAR Opcode = (UCHAR)(Command->CDW0 & 0xff);

    RtlZeroMemory(Tcb, sizeof(*Tcb));
    Tcb->CommandId = (UCHAR)Slot;
    Tcb->Length = (USHORT)Command->CDW12;
    Tcb->Prp1 = Command->PRP1;
    Tcb->Prp2 = Command->PRP2;
    if (!Command->PRP1)
        Tcb->DmaFlags = 0;
    else if (Opcode & 1)
        Tcb->DmaFlags = ANS_TCB_DMA_TO_DEVICE;
    else
        Tcb->DmaFlags = ANS_TCB_DMA_FROM_DEVICE;
    Queue->Sq[Slot] = *Command;
    KeMemoryBarrier();
    NvmeWriteRegister(Device, Queue->SqDoorbell, Slot);
}

static
VOID
AnsCompleted(
    _In_ PNVME_DEVICE_EXTENSION Device,
    _In_ PNVME_QUEUE Queue,
    _In_ USHORT CommandId)
{
    UNREFERENCED_PARAMETER(Queue);
    NvmeWriteRegister(Device, ANS_NVMMU_TCB_INVAL, CommandId);
    if (NvmeReadRegister(Device, ANS_NVMMU_TCB_STAT))
        DPRINT1("stornvme: ANS could not invalidate the TCB of command %u\n", CommandId);
}

static
VOID
AnsPrepareIoQueue(
    _In_ PNVME_DEVICE_EXTENSION Device,
    _Inout_ PNVME_QUEUE Queue)
{
    UNREFERENCED_PARAMETER(Device);
    Queue->SqDoorbell = ANS_LINEAR_IOSQ_DB;
    Queue->CqDoorbell = ANS_IOCQ_DB;
}

static
VOID
AnsConfigureController(
    _In_ PNVME_DEVICE_EXTENSION Device)
{
    PANS_CONTEXT Ans = AnsContext(Device);

    RtlZeroMemory(Ans->AdminTcb, ANS_QUEUE_DEPTH * sizeof(ANS_TCB));
    RtlZeroMemory(Ans->IoTcb, ANS_QUEUE_DEPTH * sizeof(ANS_TCB));
    NvmeWriteRegister(Device, ANS_LINEAR_SQ_CTRL, ANS_LINEAR_SQ_EN);
    NvmeWriteRegister(Device, ANS_MAX_PEND_CMDS_CTRL, ANS_QUEUE_DEPTH | (ANS_QUEUE_DEPTH << 16));
    NvmeWriteRegister(Device, ANS_NVMMU_NUM_TCBS, ANS_QUEUE_DEPTH - 1);
    NvmeWriteRegister64(Device, ANS_NVMMU_ASQ_TCB_BASE, Ans->AdminTcbPhysical);
    NvmeWriteRegister64(Device, ANS_NVMMU_IOSQ_TCB_BASE, Ans->IoTcbPhysical);
}

static const NVME_TRANSPORT AnsTransport =
{
    AnsSubmit,
    AnsCompleted,
    AnsPrepareIoQueue,
    AnsConfigureController,
    FALSE
};

static
BOOLEAN
AnsBootCoprocessor(
    _In_ PNVME_DEVICE_EXTENSION Device,
    _Inout_ PANS_CONTEXT Ans)
{
    ULONG Control, BootStatus, Waited;
    NTSTATUS Status;

    Control = READ_REGISTER_ULONG((volatile ULONG *)(Ans->Coproc + ANS_CPU_CONTROL));
    BootStatus = NvmeReadRegister(Device, ANS_BOOT_STATUS);
    DPRINT1("stornvme: ANS control %08lx, boot status %08lx, CSTS %08lx\n", Control, BootStatus,
            NvmeReadRegister(Device, NVME_REG_CSTS));

    Ans->Rtkit.Name = "stornvme";
    Ans->Rtkit.Mailbox = Ans->Mailbox;
    Ans->Rtkit.Context = Ans;
    Ans->Rtkit.ShmemSetup = AnsShmemSetup;
    Ans->Rtkit.Receive = NULL;
    Status = ApplePmgrResetDevice(&Ans->Tree, Ans->Tree.Node);
    if (!NT_SUCCESS(Status) && (Control & ANS_CPU_CONTROL_RUN))
    {
        Status = AppleRtkitWake(&Ans->Rtkit, ANS_BOOT_TIMEOUT_MS);
    }
    else
    {
        WRITE_REGISTER_ULONG((volatile ULONG *)(Ans->Coproc + ANS_CPU_CONTROL), ANS_CPU_CONTROL_RUN);
        Status = AppleRtkitBoot(&Ans->Rtkit, ANS_BOOT_TIMEOUT_MS);
    }
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("stornvme: ANS did not boot: %lx\n", Status);
        return FALSE;
    }
    for (Waited = 0; Waited < ANS_BOOT_TIMEOUT_MS; ++Waited)
    {
        BootStatus = NvmeReadRegister(Device, ANS_BOOT_STATUS);
        if (BootStatus == ANS_BOOT_STATUS_OK)
            break;
        AppleRtkitPoll(&Ans->Rtkit);
        StorPortStallExecution(1000);
    }
    DPRINT1("stornvme: ANS boot status %08lx after %lu ms\n", BootStatus, Waited);
    return BootStatus == ANS_BOOT_STATUS_OK;
}

static
VOID
AnsReleaseContext(
    _Inout_ PNVME_DEVICE_EXTENSION Device)
{
    PANS_CONTEXT Ans = AnsContext(Device);
    ULONG Index;

    if (!Ans)
        return;
    if (Ans->BufferCount && Ans->Sart)
    {
        for (Index = 0; Index < Ans->BufferCount; ++Index)
        {
            WRITE_REGISTER_ULONG((volatile ULONG *)(Ans->Sart + SART_CONFIG(Ans->Buffers[Index].SartIndex)), 0);
            WRITE_REGISTER_ULONG((volatile ULONG *)(Ans->Sart + SART_PADDR(Ans->Buffers[Index].SartIndex)), 0);
            WRITE_REGISTER_ULONG((volatile ULONG *)(Ans->Sart + SART_SIZE(Ans->Buffers[Index].SartIndex)), 0);
        }
        KeMemoryBarrier();
        for (Index = 0; Index < Ans->BufferCount; ++Index)
            MmFreeContiguousMemorySpecifyCache(Ans->Buffers[Index].Allocation, Ans->Buffers[Index].AllocationSize,
                                               MmNonCached);
        Ans->BufferCount = 0;
    }
    if (Ans->Coproc)
        MmUnmapIoSpace(Ans->Coproc, Ans->CoprocSize);
    if (Ans->Mailbox)
        MmUnmapIoSpace(Ans->Mailbox, Ans->MailboxSize);
    if (Ans->Sart)
        MmUnmapIoSpace(Ans->Sart, Ans->SartSize);
    FdtDriverClose(&Ans->Tree);
    ExFreePoolWithTag(Ans, ANS_TAG);
    Device->TransportContext = NULL;
    Device->Transport = NULL;
}

static
BOOLEAN
AnsParseDecimal(_Inout_ PCWSTR *Cursor, _Out_ PULONGLONG Value)
{
    PCWSTR Text = *Cursor;
    ULONGLONG Number = 0;

    if (*Text < L'0' || *Text > L'9')
        return FALSE;
    while (*Text >= L'0' && *Text <= L'9')
    {
        if (Number > (MAXULONGLONG - 9) / 10)
            return FALSE;
        Number = Number * 10 + (*Text++ - L'0');
    }
    *Cursor = Text;
    *Value = Number;
    return TRUE;
}

static
VOID
AnsReadWriteWindow(_Inout_ PNVME_DEVICE_EXTENSION Device)
{
    UNICODE_STRING KeyName = RTL_CONSTANT_STRING(L"\\Registry\\Machine\\System\\CurrentControlSet\\Control");
    UNICODE_STRING ValueName = RTL_CONSTANT_STRING(L"SystemStartOptions");
    static const WCHAR Option[] = L"NVMEWRITEWINDOW=";
    OBJECT_ATTRIBUTES Attributes;
    PKEY_VALUE_PARTIAL_INFORMATION Value;
    ULONG Length = FIELD_OFFSET(KEY_VALUE_PARTIAL_INFORMATION, Data) + 1024 * sizeof(WCHAR);
    ULONGLONG First[NVME_MAX_WRITE_WINDOWS], Last[NVME_MAX_WRITE_WINDOWS];
    ULONG Count, Index;
    PCWSTR Text, Cursor;
    HANDLE Key;

    InitializeObjectAttributes(&Attributes, &KeyName, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    if (!NT_SUCCESS(ZwOpenKey(&Key, KEY_QUERY_VALUE, &Attributes)))
        return;
    Value = ExAllocatePoolZero(PagedPool, Length + sizeof(WCHAR), ANS_TAG);
    if (Value && NT_SUCCESS(ZwQueryValueKey(Key, &ValueName, KeyValuePartialInformation, Value, Length, &Length)) &&
        Value->Type == REG_SZ)
    {
        Text = (PCWSTR)Value->Data;
        for (Cursor = Text; *Cursor; ++Cursor)
        {
            if ((Cursor == Text || Cursor[-1] == L' ') && !_wcsnicmp(Cursor, Option, RTL_NUMBER_OF(Option) - 1))
                break;
        }
        if (*Cursor)
        {
            Cursor += RTL_NUMBER_OF(Option) - 1;
            for (Count = 0; Count < NVME_MAX_WRITE_WINDOWS; )
            {
                if (!AnsParseDecimal(&Cursor, &First[Count]) || *Cursor++ != L'-' ||
                    !AnsParseDecimal(&Cursor, &Last[Count]) || First[Count] > Last[Count])
                {
                    Count = 0;
                    break;
                }
                ++Count;
                if (*Cursor != L',')
                    break;
                ++Cursor;
            }
            if (Count != 0 && (*Cursor == UNICODE_NULL || *Cursor == L' '))
            {
                for (Index = 0; Index < Count; ++Index)
                {
                    Device->WriteWindowFirst[Index] = First[Index];
                    Device->WriteWindowLast[Index] = Last[Index];
                    DPRINT1("stornvme: ANS writes are allowed only in blocks %I64u-%I64u\n", First[Index], Last[Index]);
                }
                Device->WriteWindowCount = Count;
                Device->WriteWindowValid = TRUE;
            }
            else
            {
                DPRINT1("stornvme: ignoring a malformed NVMEWRITEWINDOW option, the ANS stays read-only\n");
            }
        }
    }
    if (Value)
        ExFreePoolWithTag(Value, ANS_TAG);
    ZwClose(Key);
}

static
ULONG
NTAPI
AnsFindAdapter(_In_ PVOID DeviceExtension,
               _In_ PVOID HwContext,
               _In_ PVOID BusInformation,
               _In_ PCHAR ArgumentString,
               _Inout_ PPORT_CONFIGURATION_INFORMATION ConfigInfo,
               _Out_ PBOOLEAN Again)
{
    PNVME_DEVICE_EXTENSION Device = (PNVME_DEVICE_EXTENSION)DeviceExtension;
    PACCESS_RANGE AccessRange;
    STOR_PHYSICAL_ADDRESS Physical;
    PANS_CONTEXT Ans;
    PUCHAR Uncached;
    ULONG UncachedSize, Length, Offset, Phandle, Node, CapHigh;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(HwContext);
    UNREFERENCED_PARAMETER(BusInformation);
    UNREFERENCED_PARAMETER(ArgumentString);

    *Again = FALSE;
    AccessRange = &(*ConfigInfo->AccessRanges)[0];
    if (AccessRange->RangeLength == 0 || !AccessRange->RangeInMemory)
    {
        DPRINT1("stornvme: ANS has no memory range (%lx bytes)\n", AccessRange->RangeLength);
        return SP_RETURN_NOT_FOUND;
    }

    Ans = ExAllocatePoolZero(NonPagedPool, sizeof(*Ans), ANS_TAG);
    if (!Ans)
        return SP_RETURN_ERROR;
    Device->TransportContext = Ans;
    Status = FdtDriverOpenByAddress("apple,nvme-ans2", (ULONG64)AccessRange->RangeStart.QuadPart, &Ans->Tree);
    if (!NT_SUCCESS(Status))
        Status = FdtDriverOpenByAddress("apple,t8103-nvme-ans2", (ULONG64)AccessRange->RangeStart.QuadPart, &Ans->Tree);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("stornvme: no ANS in the device tree at %I64x: %lx\n", AccessRange->RangeStart.QuadPart, Status);
        goto Failure;
    }
    Node = Ans->Tree.Node;
    Status = ApplePmgrPowerOnDevice(&Ans->Tree, Node);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("stornvme: ANS power domains did not turn on: %lx\n", Status);
        goto Failure;
    }
    Ans->Coproc = FdtDriverMapReg(&Ans->Tree, Node, 1, NULL, &Ans->CoprocSize);
    if (FdtReadU32(&Ans->Tree.Fdt, Node, "mboxes", &Phandle))
        Ans->Mailbox = FdtDriverMapReg(&Ans->Tree, FdtDriverFindPhandle(&Ans->Tree, Phandle), 0, NULL,
                                       &Ans->MailboxSize);
    if (FdtReadU32(&Ans->Tree.Fdt, Node, "apple,sart", &Phandle))
        Ans->Sart = FdtDriverMapReg(&Ans->Tree, FdtDriverFindPhandle(&Ans->Tree, Phandle), 0, NULL, &Ans->SartSize);
    if (!Ans->Coproc || !Ans->Mailbox || !Ans->Sart)
    {
        DPRINT1("stornvme: ANS coprocessor %p, mailbox %p, SART %p\n", Ans->Coproc, Ans->Mailbox, Ans->Sart);
        goto Failure;
    }

    Device->Bar0 = (PUCHAR)StorPortGetDeviceBase(Device, ConfigInfo->AdapterInterfaceType,
                                                 ConfigInfo->SystemIoBusNumber, AccessRange->RangeStart,
                                                 AccessRange->RangeLength, FALSE);
    if (Device->Bar0 == NULL)
    {
        DPRINT1("stornvme: ANS registers at %I64x could not be mapped\n", AccessRange->RangeStart.QuadPart);
        goto Failure;
    }
    if (!AnsBootCoprocessor(Device, Ans))
        goto Failure;

    CapHigh = NvmeReadRegister(Device, NVME_REG_CAP + 4);
    Device->Transport = &AnsTransport;
    Device->Version = NvmeReadRegister(Device, NVME_REG_VS);
    Device->DoorbellStride = 4 << NVME_CAP_HIGH_DSTRD(CapHigh);
    Device->TimeoutMilliseconds = 5000;
    Device->MaxQueueEntries = ANS_QUEUE_DEPTH;
    Device->IoQueueCount = 1;
    /* TODO: lift the write protection of the Apple internal SSD once its write path is verified on hardware */
    Device->WriteProtected = TRUE;
    Device->IdentifyCnsLimited = TRUE;
    AnsReadWriteWindow(Device);

    UncachedSize = 6 * PAGE_SIZE + 2 * ROUND_TO_PAGES(ANS_QUEUE_DEPTH * sizeof(ANS_TCB));
    ConfigInfo->ScatterGather = TRUE;
    ConfigInfo->Master = TRUE;
    if (ConfigInfo->Dma64BitAddresses & SCSI_DMA64_SYSTEM_SUPPORTED)
        ConfigInfo->Dma64BitAddresses = SCSI_DMA64_MINIPORT_FULL64BIT_NO_BOUNDARY_REQ_SUPPORTED;
    Uncached = (PUCHAR)StorPortGetUncachedExtension(Device, ConfigInfo, UncachedSize);
    if (Uncached == NULL)
    {
        DPRINT1("stornvme: no %lu byte uncached extension for ANS\n", UncachedSize);
        goto Failure;
    }
    RtlZeroMemory(Uncached, UncachedSize);
    Device->UncachedBase = Uncached;
    Device->UncachedSize = UncachedSize;
    Physical = StorPortGetPhysicalAddress(Device, NULL, Uncached, &Length);
    Device->UncachedPhysical = (ULONGLONG)Physical.QuadPart;
    if (Device->UncachedPhysical == 0 || (Device->UncachedPhysical & (PAGE_SIZE - 1)) != 0)
    {
        DPRINT1("stornvme: ANS uncached extension at physical %I64x is unusable\n", Device->UncachedPhysical);
        goto Failure;
    }

    Offset = 0;
    Device->AdminQueue.Sq = (PNVME_COMMAND)(Uncached + Offset);
    Device->AdminQueue.SqPhysical = Device->UncachedPhysical + Offset;
    Offset += PAGE_SIZE;
    Device->AdminQueue.Cq = (PNVME_COMPLETION)(Uncached + Offset);
    Device->AdminQueue.CqPhysical = Device->UncachedPhysical + Offset;
    Offset += PAGE_SIZE;
    Device->IoQueues[0].Sq = (PNVME_COMMAND)(Uncached + Offset);
    Device->IoQueues[0].SqPhysical = Device->UncachedPhysical + Offset;
    Offset += PAGE_SIZE;
    Device->IoQueues[0].Cq = (PNVME_COMPLETION)(Uncached + Offset);
    Device->IoQueues[0].CqPhysical = Device->UncachedPhysical + Offset;
    Offset += PAGE_SIZE;
    Device->IoQueues[0].Entries = ANS_QUEUE_DEPTH;
    Device->IdentifyBuffer = Uncached + Offset;
    Device->IdentifyPhysical = Device->UncachedPhysical + Offset;
    Offset += PAGE_SIZE;
    Device->SmartLog = (PNVME_SMART_LOG)(Uncached + Offset);
    Device->SmartLogPhysical = Device->UncachedPhysical + Offset;
    Offset += PAGE_SIZE;
    Ans->AdminTcb = (PANS_TCB)(Uncached + Offset);
    Ans->AdminTcbPhysical = Device->UncachedPhysical + Offset;
    Offset += (ULONG)ROUND_TO_PAGES(ANS_QUEUE_DEPTH * sizeof(ANS_TCB));
    Ans->IoTcb = (PANS_TCB)(Uncached + Offset);
    Ans->IoTcbPhysical = Device->UncachedPhysical + Offset;

    Device->AdminQueue.QueueId = 0;
    Device->AdminQueue.Vector = 0;
    Device->AdminQueue.Entries = ANS_ADMIN_DEPTH;
    Device->AdminQueue.Phase = 1;
    Device->AdminQueue.SqDoorbell = ANS_LINEAR_ASQ_DB;
    Device->AdminQueue.CqDoorbell = ANS_ACQ_DB;

    if (!NvmeStartController(Device, TRUE))
    {
        DPRINT1("stornvme: ANS NVMe controller did not start\n");
        goto Failure;
    }
    DPRINT1("stornvme: Apple ANS storage %s, %lu namespace(s)\n", Device->Model, Device->NamespaceCount);

    ConfigInfo->MaximumTransferLength = Device->MaximumTransferLength;
    ConfigInfo->NumberOfPhysicalBreaks = ((Device->MaximumTransferLength + PAGE_SIZE - 1) / PAGE_SIZE) + 1;
    ConfigInfo->AlignmentMask = 0x3;
    ConfigInfo->CachesData = Device->VolatileWriteCache;
    ConfigInfo->MaximumNumberOfTargets = 1;
    ConfigInfo->MaximumNumberOfLogicalUnits = NVME_MAX_NAMESPACES;
    ConfigInfo->NumberOfBuses = 1;
    ConfigInfo->SynchronizationModel = StorSynchronizeFullDuplex;
    ConfigInfo->InterruptSynchronizationMode = InterruptSynchronizeAll;
    return SP_RETURN_FOUND;

Failure:
    AnsReleaseContext(Device);
    return SP_RETURN_ERROR;
}

static
VOID
NTAPI
AnsPollQueues(_In_ PVOID DeviceExtension)
{
    PNVME_DEVICE_EXTENSION Device = (PNVME_DEVICE_EXTENSION)DeviceExtension;
    ULONG Index;

    if (!Device->ControllerStarted || !Device->Transport)
        return;
    NvmeDrainAdminQueue(Device);
    for (Index = 0; Index < Device->IoQueueCount; Index++)
        NvmeDrainIoQueue(Device, &Device->IoQueues[Index]);
    StorPortNotification(RequestTimerCall, Device, AnsPollQueues, ANS_POLL_INTERVAL_US);
}

static
BOOLEAN
NTAPI
AnsHwInitialize(_In_ PVOID DeviceExtension)
{
    PNVME_DEVICE_EXTENSION Device = (PNVME_DEVICE_EXTENSION)DeviceExtension;

    if (!NvmeHwInitialize(DeviceExtension))
        return FALSE;
    StorPortNotification(RequestTimerCall, Device, AnsPollQueues, ANS_POLL_INTERVAL_US);
    return TRUE;
}

ULONG
NvmeRegisterPlatformAdapters(_In_ PVOID DriverObject, _In_ PVOID RegistryPath, _In_ PHW_INITIALIZATION_DATA PciInitData)
{
    HW_INITIALIZATION_DATA InitData = *PciInitData;

    InitData.AdapterInterfaceType = Internal;
    InitData.HwFindAdapter = AnsFindAdapter;
    InitData.HwInitialize = AnsHwInitialize;
    InitData.NumberOfAccessRanges = 2;
    return StorPortInitialize(DriverObject, RegistryPath, &InitData, NULL);
}
