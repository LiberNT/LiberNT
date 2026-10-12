/*
 * PROJECT:     LiberNT SpacemiT K1 PMIC Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     SpacemiT P1 real-time clock and power key over the K1 I2C controller
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntifs.h>
#include <initguid.h>
#include <poclass.h>

#define NDEBUG
#include <debug.h>

#define K1XPMIC_TAG             'mPiK'

#define K1X_I2C_CONTROL         0x00
#define K1X_I2C_STATUS          0x04
#define K1X_I2C_DATA            0x0C
#define K1X_I2C_CR_START        (1UL << 0)
#define K1X_I2C_CR_STOP         (1UL << 1)
#define K1X_I2C_CR_NAK          (1UL << 2)
#define K1X_I2C_CR_BYTE         (1UL << 3)
#define K1X_I2C_CR_FIFO         (1UL << 5)
#define K1X_I2C_CR_DMA          (1UL << 7)
#define K1X_I2C_CR_MASTER_ABORT (1UL << 12)
#define K1X_I2C_CR_CLOCK        (1UL << 13)
#define K1X_I2C_CR_UNIT         (1UL << 14)
#define K1X_I2C_CR_INTERRUPTS   0xFFFC0000UL
#define K1X_I2C_SR_NAK          (1UL << 14)
#define K1X_I2C_SR_UNIT_BUSY    (1UL << 15)
#define K1X_I2C_SR_BUS_BUSY     (1UL << 16)
#define K1X_I2C_SR_LOST         (1UL << 18)
#define K1X_I2C_SR_TX_EMPTY     (1UL << 19)
#define K1X_I2C_SR_RX_FULL      (1UL << 20)
#define K1X_I2C_SR_BUS_ERROR    (1UL << 22)
#define K1X_I2C_SR_EVENTS       0xFFFC0000UL

#define P1_RTC_SECOND           0x0D
#define P1_RTC_FIELDS           6
#define P1_RTC_CONTROL          0x1D
#define P1_RTC_CONTROL_ENABLE   (1U << 2)
#define P1_EVENT_STATUS         0x91
#define P1_EVENT_ENABLE         0x98
#define P1_EVENT_REGISTERS      7
#define P1_POWER_KEY_STATUS     0x97
#define P1_POWER_KEY_ENABLE     0x9E
#define P1_POWER_KEY_SHORT      (1U << 2)

typedef struct _K1XPMIC_COMMON
{
    BOOLEAN IsPowerButton;
    PDEVICE_OBJECT Self;
} K1XPMIC_COMMON, *PK1XPMIC_COMMON;

typedef struct _K1XPMIC_EXTENSION
{
    K1XPMIC_COMMON Common;
    PDEVICE_OBJECT LowerDevice;
    PDEVICE_OBJECT PhysicalDevice;
    PUCHAR Registers;
    ULONG RegisterLength;
    UCHAR Address;
    BOOLEAN HasPmic;
    BOOLEAN ShutdownRegistered;
    BOOLEAN ButtonReported;
    ULONG ButtonInterrupt;
    PDEVICE_OBJECT Button;
    PKINTERRUPT Interrupt;
    FAST_MUTEX Lock;
} K1XPMIC_EXTENSION, *PK1XPMIC_EXTENSION;

typedef struct _K1XPMIC_BUTTON_EXTENSION
{
    K1XPMIC_COMMON Common;
    PK1XPMIC_EXTENSION Parent;
    PKINTERRUPT Interrupt;
    PIO_WORKITEM WorkItem;
    KDPC Dpc;
    KSPIN_LOCK Lock;
    PIRP PendingIrp;
    ULONG PendingEvents;
    BOOLEAN WorkQueued;
    BOOLEAN Started;
    UNICODE_STRING InterfaceName;
} K1XPMIC_BUTTON_EXTENSION, *PK1XPMIC_BUTTON_EXTENSION;

typedef struct _K1XPMIC_ACCESS
{
    PK1XPMIC_EXTENSION Extension;
    UCHAR Register;
    PUCHAR Data;
    ULONG Count;
    BOOLEAN Write;
    NTSTATUS Status;
} K1XPMIC_ACCESS, *PK1XPMIC_ACCESS;

DRIVER_INITIALIZE DriverEntry;

static ULONG
K1PmicBigEndian(_In_reads_bytes_(4) const UCHAR *Bytes)
{
    return ((ULONG)Bytes[0] << 24) | ((ULONG)Bytes[1] << 16) | ((ULONG)Bytes[2] << 8) | Bytes[3];
}

static ULONG
K1I2cRead(_In_ PK1XPMIC_EXTENSION Extension, _In_ ULONG Offset)
{
    return READ_REGISTER_ULONG((PULONG)(Extension->Registers + Offset));
}

static VOID
K1I2cWrite(_In_ PK1XPMIC_EXTENSION Extension, _In_ ULONG Offset, _In_ ULONG Value)
{
    WRITE_REGISTER_ULONG((PULONG)(Extension->Registers + Offset), Value);
}

static ULONG
K1I2cControlBase(_In_ PK1XPMIC_EXTENSION Extension)
{
    return (K1I2cRead(Extension, K1X_I2C_CONTROL) &
            ~(K1X_I2C_CR_START | K1X_I2C_CR_STOP | K1X_I2C_CR_NAK | K1X_I2C_CR_BYTE |
              K1X_I2C_CR_FIFO | K1X_I2C_CR_DMA | K1X_I2C_CR_MASTER_ABORT | K1X_I2C_CR_INTERRUPTS)) |
           K1X_I2C_CR_UNIT | K1X_I2C_CR_CLOCK;
}

static NTSTATUS
K1I2cWaitIdle(_In_ PK1XPMIC_EXTENSION Extension)
{
    ULONG Loops = 20000;

    while (Loops--)
    {
        if (!(K1I2cRead(Extension, K1X_I2C_STATUS) & (K1X_I2C_SR_UNIT_BUSY | K1X_I2C_SR_BUS_BUSY)))
            return STATUS_SUCCESS;
        KeStallExecutionProcessor(5);
    }
    return STATUS_IO_TIMEOUT;
}

static NTSTATUS
K1I2cTransferByte(_In_ PK1XPMIC_EXTENSION Extension, _In_ ULONG Flags, _In_ ULONG Event,
                  _Inout_ PUCHAR Byte)
{
    ULONG Loops = 20000, Status;

    if (Event == K1X_I2C_SR_TX_EMPTY)
        K1I2cWrite(Extension, K1X_I2C_DATA, *Byte);
    K1I2cWrite(Extension, K1X_I2C_CONTROL, K1I2cControlBase(Extension) | Flags | K1X_I2C_CR_BYTE);
    while (Loops--)
    {
        Status = K1I2cRead(Extension, K1X_I2C_STATUS);
        if (Status & (K1X_I2C_SR_LOST | K1X_I2C_SR_BUS_ERROR))
        {
            K1I2cWrite(Extension, K1X_I2C_STATUS, Status & K1X_I2C_SR_EVENTS);
            K1I2cWrite(Extension, K1X_I2C_CONTROL, K1I2cControlBase(Extension) | K1X_I2C_CR_MASTER_ABORT);
            return STATUS_DEVICE_PROTOCOL_ERROR;
        }
        if (Status & Event)
        {
            K1I2cWrite(Extension, K1X_I2C_STATUS, Event);
            if (Event == K1X_I2C_SR_RX_FULL)
                *Byte = (UCHAR)K1I2cRead(Extension, K1X_I2C_DATA);
            else if ((Status & K1X_I2C_SR_NAK) && !(Flags & K1X_I2C_CR_STOP))
                return STATUS_DEVICE_PROTOCOL_ERROR;
            return STATUS_SUCCESS;
        }
        KeStallExecutionProcessor(5);
    }
    K1I2cWrite(Extension, K1X_I2C_CONTROL, K1I2cControlBase(Extension) | K1X_I2C_CR_MASTER_ABORT);
    return STATUS_IO_TIMEOUT;
}

static NTSTATUS
K1PmicAccessUnlocked(_In_ PK1XPMIC_EXTENSION Extension, _In_ UCHAR Register,
                     _Inout_updates_(Count) PUCHAR Data, _In_ ULONG Count, _In_ BOOLEAN Write)
{
    UCHAR Byte;
    ULONG Index;
    NTSTATUS Status;

    Status = K1I2cWaitIdle(Extension);
    if (!NT_SUCCESS(Status))
        return Status;
    K1I2cWrite(Extension, K1X_I2C_STATUS, K1X_I2C_SR_EVENTS);

    Byte = (UCHAR)(Extension->Address << 1);
    Status = K1I2cTransferByte(Extension, K1X_I2C_CR_START, K1X_I2C_SR_TX_EMPTY, &Byte);
    if (NT_SUCCESS(Status))
    {
        Byte = Register;
        Status = K1I2cTransferByte(Extension, (Write || Count) ? 0 : K1X_I2C_CR_STOP,
                                   K1X_I2C_SR_TX_EMPTY, &Byte);
    }
    if (NT_SUCCESS(Status) && Write)
    {
        for (Index = 0; NT_SUCCESS(Status) && Index < Count; ++Index)
        {
            Byte = Data[Index];
            Status = K1I2cTransferByte(Extension, (Index + 1 == Count) ? K1X_I2C_CR_STOP : 0,
                                       K1X_I2C_SR_TX_EMPTY, &Byte);
        }
    }
    else if (NT_SUCCESS(Status) && Count)
    {
        Byte = (UCHAR)((Extension->Address << 1) | 1);
        Status = K1I2cTransferByte(Extension, K1X_I2C_CR_START, K1X_I2C_SR_TX_EMPTY, &Byte);
        for (Index = 0; NT_SUCCESS(Status) && Index < Count; ++Index)
        {
            Status = K1I2cTransferByte(Extension,
                                       (Index + 1 == Count) ? (K1X_I2C_CR_STOP | K1X_I2C_CR_NAK) : 0,
                                       K1X_I2C_SR_RX_FULL, &Data[Index]);
        }
    }
    if (!NT_SUCCESS(Status))
        DPRINT1("K1XPMIC: I2C %s of register 0x%02x failed 0x%08lx\n", Write ? "write" : "read", Register, Status);
    return Status;
}

static BOOLEAN NTAPI
K1PmicAccessSynchronized(_In_ PVOID Context)
{
    PK1XPMIC_ACCESS Access = Context;

    Access->Status = K1PmicAccessUnlocked(Access->Extension, Access->Register, Access->Data,
                                          Access->Count, Access->Write);
    return TRUE;
}

static NTSTATUS
K1PmicAccess(_In_ PK1XPMIC_EXTENSION Extension, _In_ UCHAR Register,
             _Inout_updates_(Count) PUCHAR Data, _In_ ULONG Count, _In_ BOOLEAN Write)
{
    K1XPMIC_ACCESS Access;

    if (!Extension->Interrupt)
        return K1PmicAccessUnlocked(Extension, Register, Data, Count, Write);
    Access.Extension = Extension;
    Access.Register = Register;
    Access.Data = Data;
    Access.Count = Count;
    Access.Write = Write;
    Access.Status = STATUS_UNSUCCESSFUL;
    KeSynchronizeExecution(Extension->Interrupt, K1PmicAccessSynchronized, &Access);
    return Access.Status;
}

static NTSTATUS
K1PmicReadTime(_In_ PK1XPMIC_EXTENSION Extension, _Out_ PLARGE_INTEGER Time)
{
    UCHAR First[P1_RTC_FIELDS], Second[P1_RTC_FIELDS];
    TIME_FIELDS Fields;
    ULONG Attempt;
    NTSTATUS Status;

    Status = K1PmicAccess(Extension, P1_RTC_SECOND, First, sizeof(First), FALSE);
    for (Attempt = 0; NT_SUCCESS(Status) && Attempt < 4; ++Attempt)
    {
        Status = K1PmicAccess(Extension, P1_RTC_SECOND, Second, sizeof(Second), FALSE);
        if (!NT_SUCCESS(Status) || RtlEqualMemory(First, Second, sizeof(First)))
            break;
        RtlCopyMemory(First, Second, sizeof(First));
    }
    if (!NT_SUCCESS(Status))
        return Status;

    RtlZeroMemory(&Fields, sizeof(Fields));
    Fields.Second = First[0] & 0x3F;
    Fields.Minute = First[1] & 0x3F;
    Fields.Hour = First[2] & 0x1F;
    Fields.Day = (First[3] & 0x1F) + 1;
    Fields.Month = (First[4] & 0x0F) + 1;
    Fields.Year = 2000 + (First[5] & 0x3F);
    if (!RtlTimeFieldsToTime(&Fields, Time))
        return STATUS_INVALID_PARAMETER;
    return STATUS_SUCCESS;
}

static NTSTATUS
K1PmicWriteTime(_In_ PK1XPMIC_EXTENSION Extension, _In_ PLARGE_INTEGER Time)
{
    TIME_FIELDS Fields;
    UCHAR Data[P1_RTC_FIELDS], Control;
    NTSTATUS Status;

    RtlTimeToTimeFields(Time, &Fields);
    if (Fields.Year < 2000 || Fields.Year > 2063)
        return STATUS_INVALID_PARAMETER;
    Data[0] = (UCHAR)Fields.Second;
    Data[1] = (UCHAR)Fields.Minute;
    Data[2] = (UCHAR)Fields.Hour;
    Data[3] = (UCHAR)(Fields.Day - 1);
    Data[4] = (UCHAR)(Fields.Month - 1);
    Data[5] = (UCHAR)(Fields.Year - 2000);

    Status = K1PmicAccess(Extension, P1_RTC_CONTROL, &Control, 1, FALSE);
    if (!NT_SUCCESS(Status))
        return Status;
    Control &= ~P1_RTC_CONTROL_ENABLE;
    Status = K1PmicAccess(Extension, P1_RTC_CONTROL, &Control, 1, TRUE);
    if (NT_SUCCESS(Status))
        Status = K1PmicAccess(Extension, P1_RTC_SECOND, Data, sizeof(Data), TRUE);
    Control |= P1_RTC_CONTROL_ENABLE;
    if (NT_SUCCESS(Status))
        Status = K1PmicAccess(Extension, P1_RTC_CONTROL, &Control, 1, TRUE);
    else
        (VOID)K1PmicAccess(Extension, P1_RTC_CONTROL, &Control, 1, TRUE);
    return Status;
}

static NTSTATUS
K1PmicQueryBinary(_In_ HANDLE Key, _In_z_ PCWSTR Name, _Out_writes_bytes_(Size) PUCHAR Buffer,
                  _In_ ULONG Size, _Out_ PULONG Length)
{
    UCHAR Storage[FIELD_OFFSET(KEY_VALUE_PARTIAL_INFORMATION, Data) + 128];
    PKEY_VALUE_PARTIAL_INFORMATION Information = (PKEY_VALUE_PARTIAL_INFORMATION)Storage;
    UNICODE_STRING ValueName;
    ULONG Result;
    NTSTATUS Status;

    *Length = 0;
    RtlInitUnicodeString(&ValueName, Name);
    Status = ZwQueryValueKey(Key, &ValueName, KeyValuePartialInformation, Information, sizeof(Storage), &Result);
    if (!NT_SUCCESS(Status))
        return Status;
    if (Information->DataLength > Size)
        return STATUS_BUFFER_OVERFLOW;
    RtlCopyMemory(Buffer, Information->Data, Information->DataLength);
    *Length = Information->DataLength;
    return STATUS_SUCCESS;
}

static BOOLEAN
K1PmicListContains(_In_reads_bytes_(Length) const UCHAR *List, _In_ ULONG Length, _In_z_ const CHAR *Wanted)
{
    SIZE_T WantedLength = strlen(Wanted);
    ULONG Offset = 0;

    while (Offset < Length)
    {
        SIZE_T Entry = 0;

        while (Offset + Entry < Length && List[Offset + Entry])
            Entry++;
        if (Entry == WantedLength && !memcmp(List + Offset, Wanted, WantedLength))
            return TRUE;
        Offset += (ULONG)Entry + 1;
    }
    return FALSE;
}

static VOID
K1PmicFindChild(_In_ PK1XPMIC_EXTENSION Extension)
{
    UCHAR Buffer[sizeof(KEY_BASIC_INFORMATION) + 128 * sizeof(WCHAR)];
    PKEY_BASIC_INFORMATION Basic = (PKEY_BASIC_INFORMATION)Buffer;
    HANDLE Parameters;
    ULONG Index, Result;

    if (!NT_SUCCESS(IoOpenDeviceRegistryKey(Extension->PhysicalDevice, PLUGPLAY_REGKEY_DEVICE, KEY_READ, &Parameters)))
        return;
    for (Index = 0; !Extension->HasPmic; ++Index)
    {
        UCHAR Value[128];
        OBJECT_ATTRIBUTES Attributes;
        UNICODE_STRING Name;
        HANDLE Child;
        ULONG Length;

        if (!NT_SUCCESS(ZwEnumerateKey(Parameters, Index, KeyBasicInformation, Basic,
                                       sizeof(Buffer) - sizeof(WCHAR), &Result)))
            break;
        Name.Buffer = Basic->Name;
        Name.Length = (USHORT)Basic->NameLength;
        Name.MaximumLength = (USHORT)Basic->NameLength;
        InitializeObjectAttributes(&Attributes, &Name, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, Parameters, NULL);
        if (!NT_SUCCESS(ZwOpenKey(&Child, KEY_READ, &Attributes)))
            continue;
        if (NT_SUCCESS(K1PmicQueryBinary(Child, L"compatible", Value, sizeof(Value), &Length)) &&
            K1PmicListContains(Value, Length, "spacemit,spm8821") &&
            NT_SUCCESS(K1PmicQueryBinary(Child, L"reg", Value, sizeof(Value), &Length)) && Length >= 4 &&
            K1PmicBigEndian(Value) < 0x80)
        {
            Extension->Address = (UCHAR)K1PmicBigEndian(Value);
            Extension->HasPmic = TRUE;
            if (NT_SUCCESS(K1PmicQueryBinary(Child, L"interrupts", Value, sizeof(Value), &Length)) && Length >= 4)
                Extension->ButtonInterrupt = K1PmicBigEndian(Value);
        }
        ZwClose(Child);
    }
    ZwClose(Parameters);
}

static NTSTATUS
K1PmicCopyString(_In_reads_bytes_(Size) PCWSTR Source, _In_ SIZE_T Size, _Out_ PULONG_PTR Information)
{
    PWSTR Copy = ExAllocatePoolWithTag(PagedPool, Size, K1XPMIC_TAG);

    if (!Copy)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlCopyMemory(Copy, Source, Size);
    *Information = (ULONG_PTR)Copy;
    return STATUS_SUCCESS;
}

static VOID
K1PmicCreateButton(_In_ PK1XPMIC_EXTENSION Extension)
{
    PK1XPMIC_BUTTON_EXTENSION Button;
    PDEVICE_OBJECT Pdo;

    if (!Extension->ButtonInterrupt || Extension->Button)
        return;
    if (!NT_SUCCESS(IoCreateDevice(Extension->Common.Self->DriverObject, sizeof(K1XPMIC_BUTTON_EXTENSION), NULL,
                                   FILE_DEVICE_UNKNOWN, FILE_AUTOGENERATED_DEVICE_NAME | FILE_DEVICE_SECURE_OPEN,
                                   FALSE, &Pdo)))
        return;
    Button = Pdo->DeviceExtension;
    RtlZeroMemory(Button, sizeof(*Button));
    Button->Common.IsPowerButton = TRUE;
    Button->Common.Self = Pdo;
    Button->Parent = Extension;
    KeInitializeSpinLock(&Button->Lock);
    Button->WorkItem = IoAllocateWorkItem(Pdo);
    if (!Button->WorkItem)
    {
        IoDeleteDevice(Pdo);
        return;
    }
    Pdo->Flags &= ~DO_DEVICE_INITIALIZING;
    Extension->Button = Pdo;
    Extension->ButtonReported = TRUE;
    IoInvalidateDeviceRelations(Extension->PhysicalDevice, BusRelations);
}

static NTSTATUS
K1PmicStart(_In_ PK1XPMIC_EXTENSION Extension, _In_ PIO_STACK_LOCATION Stack)
{
    PCM_RESOURCE_LIST Resources = Stack->Parameters.StartDevice.AllocatedResourcesTranslated;
    LARGE_INTEGER Time;
    ULONG Index;
    NTSTATUS Status;

    K1PmicFindChild(Extension);
    if (!Extension->HasPmic || !Resources || !Resources->Count)
        return STATUS_SUCCESS;

    for (Index = 0; Index < Resources->List[0].PartialResourceList.Count; ++Index)
    {
        PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor =
            &Resources->List[0].PartialResourceList.PartialDescriptors[Index];

        if (Descriptor->Type == CmResourceTypeMemory && !Extension->Registers)
        {
            Extension->RegisterLength = Descriptor->u.Memory.Length;
            Extension->Registers = MmMapIoSpace(Descriptor->u.Memory.Start, Extension->RegisterLength, MmNonCached);
        }
    }
    if (!Extension->Registers || Extension->RegisterLength < K1X_I2C_DATA + sizeof(ULONG))
        return STATUS_DEVICE_CONFIGURATION_ERROR;

    ExAcquireFastMutex(&Extension->Lock);
    Status = K1PmicReadTime(Extension, &Time);
    ExReleaseFastMutex(&Extension->Lock);
    if (NT_SUCCESS(Status))
    {
        TIME_FIELDS Fields;

        RtlTimeToTimeFields(&Time, &Fields);
        DPRINT("K1XPMIC: RTC %04u-%02u-%02u %02u:%02u:%02u UTC\n",
               Fields.Year, Fields.Month, Fields.Day, Fields.Hour, Fields.Minute, Fields.Second);
        ZwSetSystemTime(&Time, NULL);
    }
    else
    {
        DPRINT1("K1XPMIC: RTC read failed 0x%08lx\n", Status);
    }

    if (NT_SUCCESS(IoRegisterShutdownNotification(Extension->Common.Self)))
        Extension->ShutdownRegistered = TRUE;
    K1PmicCreateButton(Extension);
    return STATUS_SUCCESS;
}

static VOID
K1PmicStop(_In_ PK1XPMIC_EXTENSION Extension)
{
    if (Extension->ShutdownRegistered)
    {
        IoUnregisterShutdownNotification(Extension->Common.Self);
        Extension->ShutdownRegistered = FALSE;
    }
    if (Extension->Registers)
    {
        MmUnmapIoSpace(Extension->Registers, Extension->RegisterLength);
        Extension->Registers = NULL;
    }
}

static NTSTATUS
K1ButtonSetEnable(_In_ PK1XPMIC_EXTENSION Parent, _In_ UCHAR Enable)
{
    return K1PmicAccess(Parent, P1_POWER_KEY_ENABLE, &Enable, 1, TRUE);
}

static NTSTATUS
K1ButtonQuiesce(_In_ PK1XPMIC_EXTENSION Parent)
{
    UCHAR Disable[P1_EVENT_REGISTERS], Clear[P1_EVENT_REGISTERS];
    NTSTATUS Status;

    RtlZeroMemory(Disable, sizeof(Disable));
    RtlFillMemory(Clear, sizeof(Clear), 0xFF);
    Status = K1PmicAccess(Parent, P1_EVENT_ENABLE, Disable, sizeof(Disable), TRUE);
    if (NT_SUCCESS(Status))
        Status = K1PmicAccess(Parent, P1_EVENT_STATUS, Clear, sizeof(Clear), TRUE);
    return Status;
}

static VOID
K1ButtonReport(_In_ PK1XPMIC_BUTTON_EXTENSION Button, _In_ ULONG Events)
{
    PIRP Irp;
    KIRQL Irql;

    KeAcquireSpinLock(&Button->Lock, &Irql);
    Irp = Button->PendingIrp;
    if (Irp && IoSetCancelRoutine(Irp, NULL))
    {
        Button->PendingIrp = NULL;
        Events |= Button->PendingEvents;
        Button->PendingEvents = 0;
    }
    else
    {
        Irp = NULL;
        Button->PendingEvents |= Events;
    }
    KeReleaseSpinLock(&Button->Lock, Irql);

    if (Irp)
    {
        *(PULONG)Irp->AssociatedIrp.SystemBuffer = Events;
        Irp->IoStatus.Information = sizeof(ULONG);
        Irp->IoStatus.Status = STATUS_SUCCESS;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
    }
}

static VOID NTAPI
K1ButtonWork(_In_ PDEVICE_OBJECT DeviceObject, _In_opt_ PVOID Context)
{
    PK1XPMIC_BUTTON_EXTENSION Button = DeviceObject->DeviceExtension;
    PK1XPMIC_EXTENSION Parent = Button->Parent;
    UCHAR Status = 0;
    KIRQL Irql;

    UNREFERENCED_PARAMETER(Context);

    KeAcquireSpinLock(&Button->Lock, &Irql);
    Button->WorkQueued = FALSE;
    KeReleaseSpinLock(&Button->Lock, Irql);
    if (!Parent)
        return;

    ExAcquireFastMutex(&Parent->Lock);
    if (NT_SUCCESS(K1PmicAccess(Parent, P1_POWER_KEY_STATUS, &Status, 1, FALSE)) && Status)
        (VOID)K1PmicAccess(Parent, P1_POWER_KEY_STATUS, &Status, 1, TRUE);
    if (Button->Started)
        (VOID)K1ButtonSetEnable(Parent, P1_POWER_KEY_SHORT);
    ExReleaseFastMutex(&Parent->Lock);

    DPRINT("K1XPMIC: power key events 0x%02x\n", Status);
    if (Status & P1_POWER_KEY_SHORT)
        K1ButtonReport(Button, SYS_BUTTON_POWER);
}

static VOID NTAPI
K1ButtonDpc(_In_ PKDPC Dpc, _In_opt_ PVOID Context, _In_opt_ PVOID Argument1, _In_opt_ PVOID Argument2)
{
    PK1XPMIC_BUTTON_EXTENSION Button = Context;
    BOOLEAN Queue;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(Argument1);
    UNREFERENCED_PARAMETER(Argument2);

    KeAcquireSpinLockAtDpcLevel(&Button->Lock);
    Queue = !Button->WorkQueued;
    Button->WorkQueued = TRUE;
    KeReleaseSpinLockFromDpcLevel(&Button->Lock);
    if (Queue)
        IoQueueWorkItem(Button->WorkItem, K1ButtonWork, DelayedWorkQueue, NULL);
}

static BOOLEAN NTAPI
K1ButtonIsr(_In_ PKINTERRUPT Interrupt, _In_ PVOID Context)
{
    PK1XPMIC_BUTTON_EXTENSION Button = Context;
    UCHAR Disable = 0;

    UNREFERENCED_PARAMETER(Interrupt);

    (VOID)K1PmicAccessUnlocked(Button->Parent, P1_POWER_KEY_ENABLE, &Disable, 1, TRUE);
    KeInsertQueueDpc(&Button->Dpc, NULL, NULL);
    return TRUE;
}

static NTSTATUS
K1ButtonStart(_In_ PK1XPMIC_BUTTON_EXTENSION Button, _In_ PIO_STACK_LOCATION Stack)
{
    PCM_RESOURCE_LIST Resources = Stack->Parameters.StartDevice.AllocatedResourcesTranslated;
    PK1XPMIC_EXTENSION Parent = Button->Parent;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Interrupt = NULL;
    ULONG Index;
    NTSTATUS Status;

    if (!Parent || !Parent->Registers || !Resources || !Resources->Count)
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    for (Index = 0; Index < Resources->List[0].PartialResourceList.Count; ++Index)
    {
        if (Resources->List[0].PartialResourceList.PartialDescriptors[Index].Type == CmResourceTypeInterrupt)
            Interrupt = &Resources->List[0].PartialResourceList.PartialDescriptors[Index];
    }
    if (!Interrupt)
        return STATUS_DEVICE_CONFIGURATION_ERROR;

    ExAcquireFastMutex(&Parent->Lock);
    Status = K1ButtonQuiesce(Parent);
    ExReleaseFastMutex(&Parent->Lock);
    if (NT_SUCCESS(Status))
    {
        KeInitializeDpc(&Button->Dpc, K1ButtonDpc, Button);
        Status = IoConnectInterrupt(&Button->Interrupt, K1ButtonIsr, Button, NULL,
                                    Interrupt->u.Interrupt.Vector, (KIRQL)Interrupt->u.Interrupt.Level,
                                    (KIRQL)Interrupt->u.Interrupt.Level,
                                    (Interrupt->Flags & CM_RESOURCE_INTERRUPT_LATCHED) ? Latched : LevelSensitive,
                                    Interrupt->ShareDisposition == CmResourceShareShared,
                                    Interrupt->u.Interrupt.Affinity, FALSE);
    }
    if (NT_SUCCESS(Status))
    {
        ExAcquireFastMutex(&Parent->Lock);
        Parent->Interrupt = Button->Interrupt;
        Button->Started = TRUE;
        Status = K1ButtonSetEnable(Parent, P1_POWER_KEY_SHORT);
        ExReleaseFastMutex(&Parent->Lock);
    }
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("K1XPMIC: power key start failed 0x%08lx\n", Status);
        return Status;
    }

    Status = IoRegisterDeviceInterface(Button->Common.Self, &GUID_DEVICE_SYS_BUTTON, NULL, &Button->InterfaceName);
    if (NT_SUCCESS(Status))
        Status = IoSetDeviceInterfaceState(&Button->InterfaceName, TRUE);
    return Status;
}

static VOID
K1ButtonStop(_In_ PK1XPMIC_BUTTON_EXTENSION Button)
{
    PK1XPMIC_EXTENSION Parent = Button->Parent;
    PIRP Irp = NULL;
    KIRQL Irql;

    if (Button->InterfaceName.Buffer)
    {
        (VOID)IoSetDeviceInterfaceState(&Button->InterfaceName, FALSE);
        RtlFreeUnicodeString(&Button->InterfaceName);
    }
    if (Button->Started && Parent)
    {
        ExAcquireFastMutex(&Parent->Lock);
        Button->Started = FALSE;
        (VOID)K1ButtonSetEnable(Parent, 0);
        Parent->Interrupt = NULL;
        ExReleaseFastMutex(&Parent->Lock);
    }
    if (Button->Interrupt)
    {
        IoDisconnectInterrupt(Button->Interrupt);
        Button->Interrupt = NULL;
        KeRemoveQueueDpc(&Button->Dpc);
        KeFlushQueuedDpcs();
    }
    KeAcquireSpinLock(&Button->Lock, &Irql);
    if (Button->PendingIrp && IoSetCancelRoutine(Button->PendingIrp, NULL))
    {
        Irp = Button->PendingIrp;
        Button->PendingIrp = NULL;
    }
    KeReleaseSpinLock(&Button->Lock, Irql);
    if (Irp)
    {
        Irp->IoStatus.Status = STATUS_DELETE_PENDING;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
    }
}

static VOID NTAPI
K1ButtonCancel(_In_ PDEVICE_OBJECT DeviceObject, _Inout_ PIRP Irp)
{
    PK1XPMIC_BUTTON_EXTENSION Button = DeviceObject->DeviceExtension;
    KIRQL Irql;

    IoReleaseCancelSpinLock(Irp->CancelIrql);
    KeAcquireSpinLock(&Button->Lock, &Irql);
    if (Button->PendingIrp == Irp)
        Button->PendingIrp = NULL;
    KeReleaseSpinLock(&Button->Lock, Irql);
    Irp->IoStatus.Status = STATUS_CANCELLED;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
}

static NTSTATUS
K1ButtonDeviceControl(_In_ PK1XPMIC_BUTTON_EXTENSION Button, _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    ULONG Events;
    KIRQL Irql;
    NTSTATUS Status;

    if (Stack->Parameters.DeviceIoControl.OutputBufferLength < sizeof(ULONG))
    {
        Status = STATUS_BUFFER_TOO_SMALL;
    }
    else if (Stack->Parameters.DeviceIoControl.IoControlCode == IOCTL_GET_SYS_BUTTON_CAPS)
    {
        *(PULONG)Irp->AssociatedIrp.SystemBuffer = SYS_BUTTON_POWER;
        Irp->IoStatus.Information = sizeof(ULONG);
        Status = STATUS_SUCCESS;
    }
    else if (Stack->Parameters.DeviceIoControl.IoControlCode == IOCTL_GET_SYS_BUTTON_EVENT)
    {
        KeAcquireSpinLock(&Button->Lock, &Irql);
        Events = Button->PendingEvents;
        if (Events)
        {
            Button->PendingEvents = 0;
            KeReleaseSpinLock(&Button->Lock, Irql);
            *(PULONG)Irp->AssociatedIrp.SystemBuffer = Events;
            Irp->IoStatus.Information = sizeof(ULONG);
            Status = STATUS_SUCCESS;
        }
        else if (Button->PendingIrp || !Button->Started)
        {
            KeReleaseSpinLock(&Button->Lock, Irql);
            Status = Button->Started ? STATUS_DEVICE_BUSY : STATUS_DEVICE_NOT_READY;
        }
        else
        {
            IoMarkIrpPending(Irp);
            IoSetCancelRoutine(Irp, K1ButtonCancel);
            if (Irp->Cancel && IoSetCancelRoutine(Irp, NULL))
            {
                KeReleaseSpinLock(&Button->Lock, Irql);
                Irp->IoStatus.Status = STATUS_CANCELLED;
                Irp->IoStatus.Information = 0;
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
                return STATUS_PENDING;
            }
            Button->PendingIrp = Irp;
            KeReleaseSpinLock(&Button->Lock, Irql);
            return STATUS_PENDING;
        }
    }
    else
    {
        Status = STATUS_INVALID_DEVICE_REQUEST;
    }
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

static NTSTATUS
K1ButtonRequirements(_In_ PK1XPMIC_BUTTON_EXTENSION Button, _Out_ PULONG_PTR Information)
{
    SIZE_T Size = FIELD_OFFSET(IO_RESOURCE_REQUIREMENTS_LIST, List[0].Descriptors[1]);
    PIO_RESOURCE_REQUIREMENTS_LIST List = ExAllocatePoolWithTag(PagedPool, Size, K1XPMIC_TAG);
    PIO_RESOURCE_DESCRIPTOR Resource;

    if (!List)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(List, Size);
    List->ListSize = (ULONG)Size;
    List->InterfaceType = Internal;
    List->AlternativeLists = 1;
    List->List[0].Version = 1;
    List->List[0].Revision = 1;
    List->List[0].Count = 1;
    Resource = &List->List[0].Descriptors[0];
    Resource->Type = CmResourceTypeInterrupt;
    Resource->ShareDisposition = CmResourceShareShared;
    Resource->Flags = CM_RESOURCE_INTERRUPT_LEVEL_SENSITIVE;
    Resource->u.Interrupt.MinimumVector = Button->Parent->ButtonInterrupt;
    Resource->u.Interrupt.MaximumVector = Button->Parent->ButtonInterrupt;
    *Information = (ULONG_PTR)List;
    return STATUS_SUCCESS;
}

static NTSTATUS
K1ButtonPnp(_In_ PK1XPMIC_BUTTON_EXTENSION Button, _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status = Irp->IoStatus.Status;

    switch (Stack->MinorFunction)
    {
        case IRP_MN_QUERY_ID:
            switch (Stack->Parameters.QueryId.IdType)
            {
                case BusQueryDeviceID:
                {
                    static const WCHAR Id[] = L"K1XPMIC\\PowerButton";
                    Status = K1PmicCopyString(Id, sizeof(Id), &Irp->IoStatus.Information);
                    break;
                }
                case BusQueryHardwareIDs:
                {
                    static const WCHAR Ids[] = L"K1XPMIC\\PowerButton\0";
                    Status = K1PmicCopyString(Ids, sizeof(Ids), &Irp->IoStatus.Information);
                    break;
                }
                case BusQueryInstanceID:
                {
                    static const WCHAR Id[] = L"0";
                    Status = K1PmicCopyString(Id, sizeof(Id), &Irp->IoStatus.Information);
                    break;
                }
                default:
                    break;
            }
            break;

        case IRP_MN_QUERY_DEVICE_TEXT:
            if (Stack->Parameters.QueryDeviceText.DeviceTextType == DeviceTextDescription)
            {
                static const WCHAR Text[] = L"SpacemiT P1 power button";
                Status = K1PmicCopyString(Text, sizeof(Text), &Irp->IoStatus.Information);
            }
            break;

        case IRP_MN_QUERY_RESOURCE_REQUIREMENTS:
            Status = Button->Parent ? K1ButtonRequirements(Button, &Irp->IoStatus.Information)
                                    : STATUS_NO_SUCH_DEVICE;
            break;

        case IRP_MN_QUERY_CAPABILITIES:
        {
            PDEVICE_CAPABILITIES Capabilities = Stack->Parameters.DeviceCapabilities.Capabilities;

            if (!Capabilities || Capabilities->Version != 1 || Capabilities->Size < sizeof(*Capabilities))
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }
            Capabilities->RawDeviceOK = TRUE;
            Capabilities->SilentInstall = TRUE;
            Capabilities->Removable = FALSE;
            Capabilities->SurpriseRemovalOK = FALSE;
            Status = STATUS_SUCCESS;
            break;
        }

        case IRP_MN_QUERY_DEVICE_RELATIONS:
            if (Stack->Parameters.QueryDeviceRelations.Type == TargetDeviceRelation)
            {
                PDEVICE_RELATIONS Relations = ExAllocatePoolWithTag(PagedPool, sizeof(*Relations), K1XPMIC_TAG);

                if (!Relations)
                {
                    Status = STATUS_INSUFFICIENT_RESOURCES;
                    break;
                }
                Relations->Count = 1;
                Relations->Objects[0] = Button->Common.Self;
                ObReferenceObject(Button->Common.Self);
                Irp->IoStatus.Information = (ULONG_PTR)Relations;
                Status = STATUS_SUCCESS;
            }
            break;

        case IRP_MN_START_DEVICE:
            Status = K1ButtonStart(Button, Stack);
            break;

        case IRP_MN_STOP_DEVICE:
        case IRP_MN_SURPRISE_REMOVAL:
            K1ButtonStop(Button);
            Status = STATUS_SUCCESS;
            break;

        case IRP_MN_QUERY_STOP_DEVICE:
        case IRP_MN_CANCEL_STOP_DEVICE:
        case IRP_MN_QUERY_REMOVE_DEVICE:
        case IRP_MN_CANCEL_REMOVE_DEVICE:
            Status = STATUS_SUCCESS;
            break;

        case IRP_MN_REMOVE_DEVICE:
            K1ButtonStop(Button);
            Status = STATUS_SUCCESS;
            if (!Button->Parent || !Button->Parent->ButtonReported)
            {
                Irp->IoStatus.Status = Status;
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
                if (Button->Parent)
                    Button->Parent->Button = NULL;
                IoFreeWorkItem(Button->WorkItem);
                IoDeleteDevice(Button->Common.Self);
                return Status;
            }
            break;

        default:
            break;
    }

    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

static NTSTATUS NTAPI
K1PmicDispatchShutdown(_In_ PDEVICE_OBJECT DeviceObject, _Inout_ PIRP Irp)
{
    PK1XPMIC_EXTENSION Extension = DeviceObject->DeviceExtension;
    LARGE_INTEGER Time;

    if (!Extension->Common.IsPowerButton && Extension->HasPmic && Extension->Registers)
    {
        KeQuerySystemTime(&Time);
        ExAcquireFastMutex(&Extension->Lock);
        (VOID)K1PmicWriteTime(Extension, &Time);
        ExReleaseFastMutex(&Extension->Lock);
    }
    Irp->IoStatus.Status = STATUS_SUCCESS;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

static NTSTATUS NTAPI
K1PmicDispatchPnp(_In_ PDEVICE_OBJECT DeviceObject, _Inout_ PIRP Irp)
{
    PK1XPMIC_EXTENSION Extension = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status;

    if (Extension->Common.IsPowerButton)
        return K1ButtonPnp(DeviceObject->DeviceExtension, Irp);

    switch (Stack->MinorFunction)
    {
        case IRP_MN_START_DEVICE:
            Status = IoForwardIrpSynchronously(Extension->LowerDevice, Irp) ? Irp->IoStatus.Status
                                                                             : STATUS_UNSUCCESSFUL;
            if (NT_SUCCESS(Status))
                Status = K1PmicStart(Extension, Stack);
            Irp->IoStatus.Status = Status;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;

        case IRP_MN_QUERY_DEVICE_RELATIONS:
            if (Stack->Parameters.QueryDeviceRelations.Type == BusRelations)
            {
                PDEVICE_RELATIONS Previous = (PDEVICE_RELATIONS)Irp->IoStatus.Information;
                PDEVICE_RELATIONS Relations;
                ULONG Count = Previous ? Previous->Count : 0;
                ULONG Extra = (Extension->Button && Extension->ButtonReported) ? 1 : 0;

                Relations = ExAllocatePoolWithTag(PagedPool,
                                                  FIELD_OFFSET(DEVICE_RELATIONS, Objects[Count + Extra + 1]),
                                                  K1XPMIC_TAG);
                if (!Relations)
                {
                    Irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
                    IoCompleteRequest(Irp, IO_NO_INCREMENT);
                    return STATUS_INSUFFICIENT_RESOURCES;
                }
                Relations->Count = Count;
                if (Count)
                    RtlCopyMemory(Relations->Objects, Previous->Objects, Count * sizeof(PDEVICE_OBJECT));
                if (Extra)
                {
                    Relations->Objects[Relations->Count++] = Extension->Button;
                    ObReferenceObject(Extension->Button);
                }
                if (Previous)
                    ExFreePool(Previous);
                Irp->IoStatus.Information = (ULONG_PTR)Relations;
                Irp->IoStatus.Status = STATUS_SUCCESS;
            }
            break;

        case IRP_MN_REMOVE_DEVICE:
            Extension->ButtonReported = FALSE;
            if (Extension->Button)
            {
                PK1XPMIC_BUTTON_EXTENSION Button = Extension->Button->DeviceExtension;

                K1ButtonStop(Button);
                Button->Parent = NULL;
                IoFreeWorkItem(Button->WorkItem);
                IoDeleteDevice(Extension->Button);
                Extension->Button = NULL;
            }
            K1PmicStop(Extension);
            Irp->IoStatus.Status = STATUS_SUCCESS;
            IoSkipCurrentIrpStackLocation(Irp);
            Status = IoCallDriver(Extension->LowerDevice, Irp);
            IoDetachDevice(Extension->LowerDevice);
            IoDeleteDevice(DeviceObject);
            return Status;

        case IRP_MN_QUERY_STOP_DEVICE:
        case IRP_MN_QUERY_REMOVE_DEVICE:
        case IRP_MN_CANCEL_STOP_DEVICE:
        case IRP_MN_CANCEL_REMOVE_DEVICE:
        case IRP_MN_SURPRISE_REMOVAL:
            Irp->IoStatus.Status = STATUS_SUCCESS;
            break;

        default:
            break;
    }
    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(Extension->LowerDevice, Irp);
}

static NTSTATUS NTAPI
K1PmicDispatchPower(_In_ PDEVICE_OBJECT DeviceObject, _Inout_ PIRP Irp)
{
    PK1XPMIC_EXTENSION Extension = DeviceObject->DeviceExtension;
    NTSTATUS Status;

    PoStartNextPowerIrp(Irp);
    if (Extension->Common.IsPowerButton)
    {
        Status = Irp->IoStatus.Status;
        if (IoGetCurrentIrpStackLocation(Irp)->MinorFunction == IRP_MN_SET_POWER ||
            IoGetCurrentIrpStackLocation(Irp)->MinorFunction == IRP_MN_QUERY_POWER)
            Status = STATUS_SUCCESS;
        Irp->IoStatus.Status = Status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return Status;
    }
    IoSkipCurrentIrpStackLocation(Irp);
    return PoCallDriver(Extension->LowerDevice, Irp);
}

static NTSTATUS NTAPI
K1PmicDispatchCreateClose(_In_ PDEVICE_OBJECT DeviceObject, _Inout_ PIRP Irp)
{
    PK1XPMIC_EXTENSION Extension = DeviceObject->DeviceExtension;

    if (!Extension->Common.IsPowerButton)
    {
        IoSkipCurrentIrpStackLocation(Irp);
        return IoCallDriver(Extension->LowerDevice, Irp);
    }
    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

static NTSTATUS NTAPI
K1PmicDispatchDeviceControl(_In_ PDEVICE_OBJECT DeviceObject, _Inout_ PIRP Irp)
{
    PK1XPMIC_EXTENSION Extension = DeviceObject->DeviceExtension;

    if (Extension->Common.IsPowerButton)
        return K1ButtonDeviceControl(DeviceObject->DeviceExtension, Irp);
    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(Extension->LowerDevice, Irp);
}

static NTSTATUS NTAPI
K1PmicDispatchOther(_In_ PDEVICE_OBJECT DeviceObject, _Inout_ PIRP Irp)
{
    PK1XPMIC_EXTENSION Extension = DeviceObject->DeviceExtension;
    NTSTATUS Status;

    if (Extension->Common.IsPowerButton)
    {
        Status = Irp->IoStatus.Status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return Status;
    }
    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(Extension->LowerDevice, Irp);
}

static NTSTATUS NTAPI
K1PmicAddDevice(_In_ PDRIVER_OBJECT DriverObject, _In_ PDEVICE_OBJECT PhysicalDeviceObject)
{
    PK1XPMIC_EXTENSION Extension;
    PDEVICE_OBJECT Fdo;
    NTSTATUS Status;

    Status = IoCreateDevice(DriverObject, sizeof(K1XPMIC_EXTENSION), NULL, FILE_DEVICE_CONTROLLER,
                            FILE_DEVICE_SECURE_OPEN, FALSE, &Fdo);
    if (!NT_SUCCESS(Status))
        return Status;
    Extension = Fdo->DeviceExtension;
    RtlZeroMemory(Extension, sizeof(*Extension));
    Extension->Common.Self = Fdo;
    Extension->PhysicalDevice = PhysicalDeviceObject;
    ExInitializeFastMutex(&Extension->Lock);
    Extension->LowerDevice = IoAttachDeviceToDeviceStack(Fdo, PhysicalDeviceObject);
    if (!Extension->LowerDevice)
    {
        IoDeleteDevice(Fdo);
        return STATUS_NO_SUCH_DEVICE;
    }
    Fdo->Flags |= DO_POWER_PAGABLE;
    Fdo->Flags &= ~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI
DriverEntry(_In_ PDRIVER_OBJECT DriverObject, _In_ PUNICODE_STRING RegistryPath)
{
    ULONG Index;

    UNREFERENCED_PARAMETER(RegistryPath);
    for (Index = 0; Index <= IRP_MJ_MAXIMUM_FUNCTION; ++Index)
        DriverObject->MajorFunction[Index] = K1PmicDispatchOther;
    DriverObject->MajorFunction[IRP_MJ_CREATE] = K1PmicDispatchCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = K1PmicDispatchCreateClose;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = K1PmicDispatchDeviceControl;
    DriverObject->MajorFunction[IRP_MJ_PNP] = K1PmicDispatchPnp;
    DriverObject->MajorFunction[IRP_MJ_POWER] = K1PmicDispatchPower;
    DriverObject->MajorFunction[IRP_MJ_SHUTDOWN] = K1PmicDispatchShutdown;
    DriverObject->DriverExtension->AddDevice = K1PmicAddDevice;
    return STATUS_SUCCESS;
}
