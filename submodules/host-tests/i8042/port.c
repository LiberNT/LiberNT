/*
 * PROJECT:     LiberNT host-native tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     PS/2 controller port lifecycle tests
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */
#include "shim.h"
#include "debug.h"
#include <stdio.h>
#include <string.h>

static UCHAR ControlPort, DataPort;
static struct { UCHAR Value; int Mouse; } Queue[8];
static unsigned Head, Count, Writes, Stalls, LockDepth, LockCalls, UnlockCalls;
static int Busy, MouseDisabled, TimerLockHeld, InjectAckDuringTimerRead;
static KSPIN_LOCK *TableLock;
static int TableLockHeld;
static PKINTERRUPT AcquiredInterrupt;
static KIRQL TestIrql = PASSIVE_LEVEL;
static ULONGLONG Clock, Arrival;
static PORT_DEVICE_EXTENSION *ActiveController;
static IRP FirstIrp, NextIrp;
static unsigned Completions, Starts, CancelCalls;
static NTSTATUS ExpectedStatus;
static UCHAR Typematic[2] = { 0xf3, 0x20 };
static UCHAR Keyboard, Mouse;
unsigned TestErrors, TestErrorValue;

VOID IoCompleteRequest(PIRP Irp, int Increment)
{
    assert(Irp == &FirstIrp && Increment == IO_NO_INCREMENT);
    assert(!TimerLockHeld && !LockDepth);
    assert(Irp->IoStatus.Status == ExpectedStatus);
    Completions++;
}

VOID IoStartNextPacket(PDEVICE_OBJECT Device, BOOLEAN Cancelable)
{
    assert(Device == &ControlPort && !Cancelable && Completions == 1);
    assert(!TimerLockHeld && !LockDepth);
    Starts++;
    ActiveController->CurrentIrp = &NextIrp;
    ActiveController->CurrentIrpDevice = Device;
}

UCHAR TestReadPort(PUCHAR Port)
{
    if (Port == &ControlPort)
        return (Busy ? KBD_IBF : 0) | (Count && Clock >= Arrival ? KBD_OBF | (Queue[Head].Mouse ? MOU_OBF : 0) : 0);
    assert(Port == &DataPort && Count);
    Count--;
    return Queue[Head++].Value;
}

VOID TestWritePort(PUCHAR Port, UCHAR Value)
{
    assert(Port == &ControlPort && Value == MOUSE_DISAB);
    Writes++;
    MouseDisabled = 1;
}

BOOLEAN KeCancelTimer(PKTIMER Timer)
{
    BOOLEAN Armed = Timer->Inserted;
    assert(TimerLockHeld && !LockDepth && TestIrql == DISPATCH_LEVEL);
    Timer->Inserted = FALSE;
    CancelCalls++;
    return Armed;
}

BOOLEAN KeReadStateTimer(PKTIMER Timer)
{
    BOOLEAN Signaled = Timer->SignalState != 0;
    assert(TestIrql <= DISPATCH_LEVEL);
    if (InjectAckDuringTimerRead)
    {
        KIRQL OldIrql;
        assert(TimerLockHeld && !LockDepth && ActiveController);
        InjectAckDuringTimerRead = 0;
        OldIrql = KeAcquireInterruptSpinLock(ActiveController->HighestDIRQLInterrupt);
        ActiveController->Packet.State = Idle;
        ActiveController->PacketComplete = TRUE;
        ActiveController->PacketResult = STATUS_SUCCESS;
        KeReleaseInterruptSpinLock(ActiveController->HighestDIRQLInterrupt, OldIrql);
    }
    return Signaled;
}

VOID KeStallExecutionProcessor(ULONG Time)
{
    assert(Time == 50);
    Stalls++;
    Clock += Time * 10;
}

ULONGLONG KeQueryInterruptTime(VOID)
{
    return Clock;
}


KIRQL KeGetCurrentIrql(VOID)
{
    return TestIrql;
}

static VOID RunTimeoutDpc(PPORT_DEVICE_EXTENSION Extension)
{
    TableLock = &Extension->InterruptObjectLock;
    TestIrql = DISPATCH_LEVEL;
    i8042PacketTimeoutDpc(&Extension->PacketTimeoutDpc, Extension, NULL, NULL);
    TestIrql = PASSIVE_LEVEL;
    TableLock = NULL;
}

VOID KeAcquireSpinLock(KSPIN_LOCK *Lock, KIRQL *OldIrql)
{
    assert(!TableLockHeld && !LockDepth && TestIrql <= DISPATCH_LEVEL);
    if (Lock == TableLock)
        TableLockHeld = 1;
    else
    {
        assert(!TimerLockHeld);
        TimerLockHeld = 1;
    }
    *OldIrql = TestIrql;
    TestIrql = DISPATCH_LEVEL;
}

VOID KeReleaseSpinLock(KSPIN_LOCK *Lock, KIRQL OldIrql)
{
    assert(!LockDepth && TestIrql == DISPATCH_LEVEL);
    if (Lock == TableLock)
    {
        assert(TableLockHeld);
        TableLockHeld = 0;
    }
    else
    {
        assert(TimerLockHeld && !TableLockHeld);
        TimerLockHeld = 0;
    }
    TestIrql = OldIrql;
}

KIRQL KeAcquireInterruptSpinLock(PKINTERRUPT Interrupt)
{
    KIRQL OldIrql = TestIrql;
    assert(Interrupt && LockDepth == 0);
    AcquiredInterrupt = Interrupt;
    LockDepth++;
    LockCalls++;
    TestIrql = DISPATCH_LEVEL + 1;
    return OldIrql;
}

VOID KeReleaseInterruptSpinLock(PKINTERRUPT Interrupt, KIRQL OldIrql)
{
    assert(Interrupt == AcquiredInterrupt && TestIrql > DISPATCH_LEVEL && LockDepth == 1);
    LockDepth--;
    UnlockCalls++;
    TestIrql = OldIrql;
}

static PORT_DEVICE_EXTENSION NewController(void)
{
    PORT_DEVICE_EXTENSION Device;
    memset(&Device, 0, sizeof(Device));
    Head = Count = Writes = Stalls = LockDepth = LockCalls = UnlockCalls = 0;
    Busy = MouseDisabled = TimerLockHeld = InjectAckDuringTimerRead = 0;
    TestIrql = PASSIVE_LEVEL;
    Clock = Arrival = 0;
    Completions = Starts = CancelCalls = TestErrors = TestErrorValue = 0;
    Device.DataPort = &DataPort;
    Device.ControlPort = &ControlPort;
    Device.Settings.PollingIterations = 3;
    Device.HighestDIRQLInterrupt = &ControlPort;
    return Device;
}

static void TestInterruptHandoff(void)
{
    PORT_DEVICE_EXTENSION Device = NewController();
    I8042_INTERRUPT_LOCK Lock;
    PKINTERRUPT Current = &ControlPort, Other = &DataPort;

    Device.Flags = 3;
    TableLock = &Device.InterruptObjectLock;
    assert(i8042AcquireInterrupt(&Device, &Lock));
    assert(TableLockHeld && AcquiredInterrupt == Current);
    i8042ReleaseInterrupt(&Device, &Lock);
    assert(!TableLockHeld && !LockDepth && TestIrql == PASSIVE_LEVEL);

    assert(i8042UnpublishInterrupt(&Device, &Current, Other, 1) == &ControlPort);
    assert(!Current && Device.Flags == 2);
    assert(i8042AcquireInterrupt(&Device, &Lock));
    assert(TableLockHeld && AcquiredInterrupt == Other);
    i8042ReleaseInterrupt(&Device, &Lock);
    assert(i8042UnpublishInterrupt(&Device, &Other, NULL, 2) == &DataPort);
    assert(!Other && Device.Flags == 0);
    assert(!i8042AcquireInterrupt(&Device, &Lock));
    assert(!TableLockHeld && !LockDepth && TestIrql == PASSIVE_LEVEL);
    TableLock = NULL;
}

int main(void)
{
    PORT_DEVICE_EXTENSION Device = NewController();
    UCHAR Value, PortStatus, InvalidCommand = 0xab;
    TestInterruptHandoff();
    Device = NewController();
    Busy = 1;
    assert(!i8042Write(&Device, Device.ControlPort, MOUSE_DISAB));
    assert(Writes == 0 && Stalls == 3);
    Busy = 0;
    assert(i8042Write(&Device, Device.ControlPort, MOUSE_DISAB));
    assert(Writes == 1);
#ifndef WRITE_BASELINE
    Device = NewController();
    Queue[0].Mouse = 1; Queue[0].Value = 0x08;
    Queue[1].Mouse = 0; Queue[1].Value = 0x1e;
    Count = 2;
    Value = 0x55;
    assert(i8042ReadDeviceData(&Device, FALSE, &Value, &PortStatus) == STATUS_NOT_FOUND);
    assert(Count == 2 && Head == 0 && Value == 0x55 && PortStatus == (KBD_OBF | MOU_OBF));
    assert(i8042ReadDeviceData(&Device, TRUE, &Value, &PortStatus) == STATUS_SUCCESS);
    assert(Value == 0x08 && Count == 1 && Head == 1);
    assert(i8042ReadDeviceData(&Device, TRUE, &Value, &PortStatus) == STATUS_NOT_FOUND);
    assert(Count == 1 && Head == 1 && PortStatus == KBD_OBF);
    assert(i8042ReadDeviceData(&Device, FALSE, &Value, &PortStatus) == STATUS_SUCCESS);
    assert(Value == 0x1e && Count == 0);

    Device = NewController();
    Queue[0].Mouse = 1; Queue[0].Value = 0xfa;
    Count = 1;
    assert(i8042ReadDataWait(&Device, &Value) == STATUS_SUCCESS && Value == 0xfa && Count == 0);

    Device = NewController();
    Queue[0].Mouse = 1; Queue[0].Value = 0xfa;
    Queue[1].Mouse = 1; Queue[1].Value = 0xaa;
    Queue[2].Mouse = 0; Queue[2].Value = 0xfa;
    Count = 3;
    assert(i8042QuiesceMouse(&Device));
    assert(MouseDisabled && Count == 1 && Head == 2 && Queue[Head].Value == 0xfa);
    assert(LockCalls == 1 && UnlockCalls == 1 && LockDepth == 0);

    Device = NewController();
    Device.HighestDIRQLInterrupt = NULL;
    Queue[0].Mouse = 0; Queue[0].Value = 0x1e;
    Count = 1;
    assert(i8042QuiesceMouse(&Device));
    assert(MouseDisabled && Count == 1 && Head == 0 && LockCalls == 0);

    Device = NewController();
    Device.Settings.PollingIterations = 1;
    Queue[0].Mouse = 1; Queue[1].Mouse = 1;
    Count = 2;
    assert(!i8042QuiesceMouse(&Device));
    assert(MouseDisabled && Count == 1 && LockCalls == 1 && UnlockCalls == 1);

    Device = NewController();
    Busy = 1;
    assert(!i8042QuiesceMouse(&Device));
    assert(Writes == 0 && LockCalls == 1 && UnlockCalls == 1);

    Device = NewController();
    ActiveController = &Device;
    Device.CurrentIrp = &FirstIrp;
    Device.CurrentIrpDevice = &ControlPort;
    FirstIrp.IoStatus.Status = STATUS_UNSUCCESSFUL;
    Completions = Starts = 0;
    ExpectedStatus = STATUS_SUCCESS;
    Device.PacketTimer.Inserted = TRUE;
    i8042FinishPacket(&Device, STATUS_SUCCESS);
    assert(Completions == 1 && Starts == 1);
    assert(Device.CurrentIrp == &NextIrp && Device.CurrentIrpDevice == &ControlPort);
    /* A finished packet cancels its own timer */
    assert(CancelCalls == 1 && !Device.PacketTimer.Inserted);

    Device = NewController();
    ActiveController = &Device;
    FirstIrp.IoStatus.Status = STATUS_SUCCESS;
    Completions = Starts = 0;
    ExpectedStatus = STATUS_IO_TIMEOUT;
    i8042FailPacket(&ControlPort, &FirstIrp, STATUS_IO_TIMEOUT);
    assert(Completions == 1 && Starts == 1);
    assert(Device.CurrentIrp == &NextIrp && Device.CurrentIrpDevice == &ControlPort);

    /* A command the device never acknowledges fails with STATUS_IO_TIMEOUT once its timer expires */
    Device = NewController();
    ActiveController = &Device;
    Device.CurrentIrp = &FirstIrp;
    Device.CurrentIrpDevice = &ControlPort;
    Device.Packet.Bytes = Typematic;
    Device.Packet.ByteCount = 2;
    Device.Packet.CurrentByte = 1;
    Device.Packet.State = SendingBytes;
    Device.PacketResends = 2;
    Device.PacketTimer.SignalState = 1;
    FirstIrp.IoStatus.Status = STATUS_SUCCESS;
    ExpectedStatus = STATUS_IO_TIMEOUT;
    RunTimeoutDpc(&Device);
    assert(Device.Packet.State == Idle && Device.PacketResends == 0);
    assert(Completions == 1 && Starts == 1 && CancelCalls == 1);
    assert(TestErrors == 1 && TestErrorValue == 0xf3);
    assert(Device.CurrentIrp == &NextIrp);
    assert(LockCalls == 1 && UnlockCalls == 1 && LockDepth == 0);

    Device = NewController();
    ActiveController = &Device;
    Device.CurrentIrp = &FirstIrp;
    Device.CurrentIrpDevice = &ControlPort;
    Device.Packet.Bytes = &InvalidCommand;
    Device.Packet.ByteCount = 0;
    Device.Packet.State = SendingBytes;
    Device.PacketTimer.SignalState = 1;
    ExpectedStatus = STATUS_IO_TIMEOUT;
    RunTimeoutDpc(&Device);
    assert(Device.Packet.State == Idle && Completions == 1 && Starts == 1);
    assert(TestErrors == 1 && TestErrorValue == 0);

    Device = NewController();
    ActiveController = &Device;
    Device.CurrentIrp = &FirstIrp;
    Device.CurrentIrpDevice = &ControlPort;
    Device.Packet.Bytes = NULL;
    Device.Packet.ByteCount = 0;
    Device.Packet.State = SendingBytes;
    Device.PacketTimer.SignalState = 1;
    ExpectedStatus = STATUS_IO_TIMEOUT;
    RunTimeoutDpc(&Device);
    assert(Device.Packet.State == Idle && Completions == 1 && Starts == 1);
    assert(TestErrors == 1 && TestErrorValue == 0);

    Device = NewController();
    ActiveController = &Device;
    Device.CurrentIrp = &FirstIrp;
    Device.CurrentIrpDevice = &ControlPort;
    Device.Packet.Bytes = Typematic;
    Device.Packet.ByteCount = 2;
    Device.Packet.CurrentByte = 2;
    Device.Packet.State = SendingBytes;
    Device.PacketTimer.SignalState = 1;
    InjectAckDuringTimerRead = 1;
    RunTimeoutDpc(&Device);
    assert(!InjectAckDuringTimerRead && Device.Packet.State == Idle && Device.PacketComplete);
    assert(Completions == 0 && Device.CurrentIrp == &FirstIrp);
    Device.PacketComplete = FALSE;
    ExpectedStatus = STATUS_SUCCESS;
    i8042FinishPacket(&Device, STATUS_SUCCESS);
    assert(Completions == 1 && Starts == 1 && Device.CurrentIrp == &NextIrp);

    /* An expiry of a timer the next packet re-armed leaves that packet alone */
    Device = NewController();
    ActiveController = &Device;
    Device.CurrentIrp = &FirstIrp;
    Device.CurrentIrpDevice = &ControlPort;
    Device.Packet.Bytes = Typematic;
    Device.Packet.State = SendingBytes;
    Device.PacketTimer.Inserted = TRUE;
    RunTimeoutDpc(&Device);
    assert(Device.Packet.State == SendingBytes && Device.PacketTimer.Inserted);
    assert(Completions == 0 && CancelCalls == 0 && LockCalls == 1 && UnlockCalls == 1 && TestErrors == 0);

    /* Nor does an expiry after the packet already finished */
    Device = NewController();
    Device.PacketTimer.SignalState = 1;
    RunTimeoutDpc(&Device);
    assert(Device.Packet.State == Idle && Completions == 0 && CancelCalls == 0);

    /* Without a connected interrupt there is nothing to time out */
    Device = NewController();
    Device.HighestDIRQLInterrupt = NULL;
    Device.Packet.State = SendingBytes;
    Device.PacketTimer.SignalState = 1;
    RunTimeoutDpc(&Device);
    assert(Device.Packet.State == SendingBytes && Completions == 0 && LockCalls == 0);

    /* A packet is refused unless its device's interrupt is initialized and a synchronizing interrupt is connected */
    assert(I8042CorePacketStatus(FALSE, &Keyboard) == STATUS_DEVICE_NOT_READY);
    assert(I8042CorePacketStatus(TRUE, NULL) == STATUS_DEVICE_NOT_READY);
    assert(I8042CorePacketStatus(FALSE, NULL) == STATUS_DEVICE_NOT_READY);
    assert(I8042CorePacketStatus(TRUE, &Keyboard) == STATUS_SUCCESS);

    /* The first connected interrupt synchronizes until one at the highest DIRQL connects */
    assert(I8042CoreSyncInterrupt(NULL, &Keyboard, 5, 6) == &Keyboard);
    assert(I8042CoreSyncInterrupt(&Keyboard, &Mouse, 6, 6) == &Mouse);
    assert(I8042CoreSyncInterrupt(&Mouse, &Keyboard, 5, 6) == &Mouse);

    Device = NewController();
    Device.Settings.PollingIterations = 12000;
    assert(i8042ReadResetReply(&Device, 0, &Value) == STATUS_IO_TIMEOUT);
    assert(Clock == 10000000 && Stalls == 20000);

    Device = NewController();
    Device.Settings.PollingIterations = 12000;
    assert(i8042ReadResetReply(&Device, 1, &Value) == STATUS_IO_TIMEOUT);
    assert(Clock == 40000000 && Stalls == 80000);

    Device = NewController();
    Queue[0].Mouse = 1; Queue[0].Value = 0xaa;
    Count = 1;
    Arrival = 25000000;
    assert(i8042ReadResetReply(&Device, 1, &Value) == STATUS_SUCCESS);
    assert(Value == 0xaa && Clock == 25000000 && Count == 0);

    Device = NewController();
    Queue[0].Mouse = 1; Queue[0].Value = 0xfa;
    Count = 1;
    assert(i8042ReadResetReply(&Device, 0, &Value) == STATUS_SUCCESS);
    assert(Value == 0xfa && Stalls == 0 && Count == 0);
#endif
    puts("i8042 controller tests passed");
    return 0;
}
