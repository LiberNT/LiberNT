/*
 * PROJECT:     LiberNT I2C Loopback Test
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Exercises the Intel I2C2 and I2C3 controllers wired to each other in master and target roles
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#include <windows.h>
#include <winioctl.h>
#include <setupapi.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <initguid.h>
#include <reactos/drivers/inteli2c.h>

#define I2CLOOP_I2C2_DEVICE_ID 0x54ea
#define I2CLOOP_I2C3_DEVICE_ID 0x54eb
#define I2CLOOP_TARGET_ADDRESS 0x42
#define I2CLOOP_ABSENT_ADDRESS 0x43
#define I2CLOOP_PATTERN_LENGTH 32
#define I2CLOOP_DEFAULT_SPEED 100000
#define I2CLOOP_TRANSFER_TIMEOUT_MS 1000
#define I2CLOOP_SETTLE_TIMEOUT_MS 1000

typedef struct _I2CLOOP_CONTROLLER
{
    HANDLE Device;
    PCSTR Name;
    INTELI2C_CONTROLLER_INFORMATION Information;
} I2CLOOP_CONTROLLER, *PI2CLOOP_CONTROLLER;

typedef struct _I2CLOOP_TRANSFER_BUFFER
{
    INTELI2C_TRANSFER_REQUEST Request;
    UCHAR Data[INTELI2C_TARGET_BUFFER_SIZE];
} I2CLOOP_TRANSFER_BUFFER;

static CHAR I2cLoopFailure[256];
static ULONG I2cLoopSpeed = I2CLOOP_DEFAULT_SPEED;

static
VOID
I2cLoopPrint(
    _In_z_ _Printf_format_string_ PCSTR Format,
    ...)
{
    CHAR Buffer[512];
    va_list Arguments;

    va_start(Arguments, Format);
    _vsnprintf(Buffer, sizeof(Buffer) - 1, Format, Arguments);
    va_end(Arguments);
    Buffer[sizeof(Buffer) - 1] = ANSI_NULL;
    fputs(Buffer, stdout);
    fflush(stdout);
    OutputDebugStringA(Buffer);
}

static
BOOL
I2cLoopStep(
    _In_z_ PCSTR Step,
    _In_ BOOL Passed,
    _In_z_ _Printf_format_string_ PCSTR Format,
    ...)
{
    CHAR Details[256];
    va_list Arguments;

    va_start(Arguments, Format);
    _vsnprintf(Details, sizeof(Details) - 1, Format, Arguments);
    va_end(Arguments);
    Details[sizeof(Details) - 1] = ANSI_NULL;
    I2cLoopPrint("I2CLOOP STEP %s %s %s\n", Step, Passed ? "PASS" : "FAIL", Details);
    if (!Passed && !I2cLoopFailure[0])
    {
        _snprintf(I2cLoopFailure, sizeof(I2cLoopFailure) - 1, "step %s: %s", Step, Details);
        I2cLoopFailure[sizeof(I2cLoopFailure) - 1] = ANSI_NULL;
    }
    return Passed;
}

static
DWORD
I2cLoopIoctl(
    _In_ HANDLE Device,
    _In_ DWORD IoControlCode,
    _In_reads_bytes_opt_(InputLength) PVOID Input,
    _In_ DWORD InputLength,
    _Out_writes_bytes_opt_(OutputLength) PVOID Output,
    _In_ DWORD OutputLength)
{
    DWORD BytesReturned;

    if (!DeviceIoControl(Device, IoControlCode, Input, InputLength, Output, OutputLength, &BytesReturned, NULL))
        return GetLastError();
    if (Output && BytesReturned < OutputLength)
        return ERROR_INVALID_DATA;
    return ERROR_SUCCESS;
}

static
VOID
I2cLoopOpenControllers(
    _Inout_ PI2CLOOP_CONTROLLER I2c2,
    _Inout_ PI2CLOOP_CONTROLLER I2c3)
{
    SP_DEVICE_INTERFACE_DATA InterfaceData;
    HDEVINFO DeviceInfo;
    DWORD Index;

    DeviceInfo = SetupDiGetClassDevsW(&GUID_DEVINTERFACE_INTEL_I2C, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (DeviceInfo == INVALID_HANDLE_VALUE)
    {
        I2cLoopPrint("I2CLOOP INFO no Intel I2C interfaces, error %lu\n", GetLastError());
        return;
    }
    for (Index = 0;; Index++)
    {
        PSP_DEVICE_INTERFACE_DETAIL_DATA_W DetailData;
        INTELI2C_CONTROLLER_INFORMATION Information;
        PI2CLOOP_CONTROLLER Controller = NULL;
        DWORD RequiredSize = 0;
        HANDLE Device;
        DWORD Error;

        ZeroMemory(&InterfaceData, sizeof(InterfaceData));
        InterfaceData.cbSize = sizeof(InterfaceData);
        if (!SetupDiEnumDeviceInterfaces(DeviceInfo, NULL, &GUID_DEVINTERFACE_INTEL_I2C, Index, &InterfaceData))
            break;
        SetupDiGetDeviceInterfaceDetailW(DeviceInfo, &InterfaceData, NULL, 0, &RequiredSize, NULL);
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || RequiredSize < sizeof(*DetailData))
            continue;
        DetailData = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, RequiredSize);
        if (!DetailData)
            continue;
        DetailData->cbSize = sizeof(*DetailData);
        Device = INVALID_HANDLE_VALUE;
        if (SetupDiGetDeviceInterfaceDetailW(DeviceInfo, &InterfaceData, DetailData, RequiredSize, NULL, NULL))
            Device = CreateFileW(DetailData->DevicePath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (Device == INVALID_HANDLE_VALUE)
        {
            I2cLoopPrint("I2CLOOP INFO interface %lu open failed, error %lu\n", Index, GetLastError());
            HeapFree(GetProcessHeap(), 0, DetailData);
            continue;
        }
        HeapFree(GetProcessHeap(), 0, DetailData);
        ZeroMemory(&Information, sizeof(Information));
        Error = I2cLoopIoctl(Device, IOCTL_INTELI2C_QUERY_CONTROLLER, NULL, 0, &Information, sizeof(Information));
        if (Error != ERROR_SUCCESS || Information.Version != INTELI2C_INTERFACE_VERSION)
        {
            I2cLoopPrint("I2CLOOP INFO interface %lu query failed, error %lu version %lu\n", Index, Error, Information.Version);
            CloseHandle(Device);
            continue;
        }
        I2cLoopPrint("I2CLOOP INFO controller index=%lu pci=8086:%04lx txfifo=%lu rxfifo=%lu\n", Information.ControllerIndex, Information.PciDeviceId, Information.TxFifoDepth, Information.RxFifoDepth);
        if (Information.PciDeviceId == I2CLOOP_I2C2_DEVICE_ID)
            Controller = I2c2;
        else if (Information.PciDeviceId == I2CLOOP_I2C3_DEVICE_ID)
            Controller = I2c3;
        if (!Controller || Controller->Device != INVALID_HANDLE_VALUE)
        {
            CloseHandle(Device);
            continue;
        }
        Controller->Device = Device;
        Controller->Information = Information;
    }
    SetupDiDestroyDeviceInfoList(DeviceInfo);
}

static
DWORD
I2cLoopMasterTransfer(
    _In_ PI2CLOOP_CONTROLLER Controller,
    _In_ USHORT Address,
    _In_ ULONG Direction,
    _Inout_updates_bytes_(Length) PUCHAR Data,
    _In_ ULONG Length,
    _Out_ PDWORD ElapsedMs)
{
    I2CLOOP_TRANSFER_BUFFER Buffer;
    DWORD Start;
    DWORD Error;

    ZeroMemory(&Buffer, sizeof(Buffer));
    Buffer.Request.Version = INTELI2C_INTERFACE_VERSION;
    Buffer.Request.ControllerIndex = Controller->Information.ControllerIndex;
    Buffer.Request.SlaveAddress = Address;
    Buffer.Request.AddressMode = INTELI2C_ADDRESS_MODE_7BIT;
    Buffer.Request.ConnectionSpeed = I2cLoopSpeed;
    Buffer.Request.TimeoutMilliseconds = I2CLOOP_TRANSFER_TIMEOUT_MS;
    Buffer.Request.TransferCount = 1;
    Buffer.Request.Transfers[0].Direction = Direction;
    Buffer.Request.Transfers[0].BufferOffset = FIELD_OFFSET(I2CLOOP_TRANSFER_BUFFER, Data);
    Buffer.Request.Transfers[0].BufferLength = Length;
    if (Direction == IntelI2cTransferDirectionWrite)
        CopyMemory(Buffer.Data, Data, Length);
    Start = GetTickCount();
    Error = I2cLoopIoctl(Controller->Device, IOCTL_INTELI2C_EXECUTE_TRANSFER, &Buffer, sizeof(Buffer), &Buffer, sizeof(Buffer));
    *ElapsedMs = GetTickCount() - Start;
    if (Error == ERROR_SUCCESS && Direction == IntelI2cTransferDirectionRead)
        CopyMemory(Data, Buffer.Data, Length);
    return Error;
}

static
DWORD
I2cLoopTargetStart(
    _In_ PI2CLOOP_CONTROLLER Controller,
    _In_ USHORT Address,
    _In_reads_bytes_(Length) const UCHAR *Response,
    _In_ ULONG Length)
{
    INTELI2C_TARGET_CONFIGURATION Configuration;

    ZeroMemory(&Configuration, sizeof(Configuration));
    Configuration.Version = INTELI2C_INTERFACE_VERSION;
    Configuration.ControllerIndex = Controller->Information.ControllerIndex;
    Configuration.TargetAddress = Address;
    Configuration.AddressMode = INTELI2C_ADDRESS_MODE_7BIT;
    Configuration.ResponseLength = Length;
    CopyMemory(Configuration.Response, Response, Length);
    return I2cLoopIoctl(Controller->Device, IOCTL_INTELI2C_TARGET_START, &Configuration, sizeof(Configuration), NULL, 0);
}

static
DWORD
I2cLoopTargetQuery(
    _In_ PI2CLOOP_CONTROLLER Controller,
    _Out_ PINTELI2C_TARGET_STATUS Status)
{
    ZeroMemory(Status, sizeof(*Status));
    return I2cLoopIoctl(Controller->Device, IOCTL_INTELI2C_TARGET_QUERY, NULL, 0, Status, sizeof(*Status));
}

static
VOID
I2cLoopPrintTarget(
    _In_ PI2CLOOP_CONTROLLER Controller,
    _In_ PCSTR When,
    _In_ const INTELI2C_TARGET_STATUS *Status)
{
    I2cLoopPrint("I2CLOOP INFO %s target %s: active=%lu con=0x%08lx received=%lu/%lu queued=%lu rdreq=%lu rxdone=%lu stops=%lu flushes=%lu errors=%lu intr=0x%08lx abort=0x%08lx\n",
                 Controller->Name, When, Status->Active, Status->Control, Status->ReceivedLength, Status->ReceivedTotal, Status->ResponseQueued, Status->ReadRequests,
                 Status->ReadCompletions, Status->StopConditions, Status->TransmitFlushes, Status->Errors, Status->LastErrorInterrupts, Status->LastAbortSource);
}

static
DWORD
I2cLoopTargetStop(
    _In_ PI2CLOOP_CONTROLLER Controller)
{
    INTELI2C_TARGET_STATUS Status;
    DWORD Error;

    ZeroMemory(&Status, sizeof(Status));
    Error = I2cLoopIoctl(Controller->Device, IOCTL_INTELI2C_TARGET_STOP, NULL, 0, &Status, sizeof(Status));
    if (Error == ERROR_SUCCESS)
        I2cLoopPrintTarget(Controller, "final", &Status);
    return Error;
}

static
DWORD
I2cLoopWaitTarget(
    _In_ PI2CLOOP_CONTROLLER Controller,
    _In_ ULONG StopConditions,
    _In_ ULONG ReceivedLength,
    _Out_ PINTELI2C_TARGET_STATUS Status)
{
    DWORD Start = GetTickCount();
    DWORD Error;

    for (;;)
    {
        Error = I2cLoopTargetQuery(Controller, Status);
        if (Error != ERROR_SUCCESS)
            return Error;
        if (Status->StopConditions > StopConditions && Status->ReceivedLength >= ReceivedLength)
            return ERROR_SUCCESS;
        if (GetTickCount() - Start >= I2CLOOP_SETTLE_TIMEOUT_MS)
            return ERROR_TIMEOUT;
        Sleep(5);
    }
}

static
ULONG
I2cLoopFirstMismatch(
    _In_reads_bytes_(Length) const UCHAR *Expected,
    _In_reads_bytes_(Length) const UCHAR *Actual,
    _In_ ULONG Length)
{
    ULONG Index;

    for (Index = 0; Index < Length; Index++)
    {
        if (Expected[Index] != Actual[Index])
            break;
    }
    return Index;
}

static
VOID
I2cLoopRunPair(
    _In_ PI2CLOOP_CONTROLLER Master,
    _In_ PI2CLOOP_CONTROLLER Target,
    _In_ ULONG Length,
    _In_ UCHAR Seed,
    _In_z_ PCSTR WriteStep,
    _In_z_ PCSTR ReadStep)
{
    UCHAR WritePattern[I2CLOOP_PATTERN_LENGTH];
    UCHAR ResponsePattern[I2CLOOP_PATTERN_LENGTH];
    UCHAR ReadBack[I2CLOOP_PATTERN_LENGTH];
    INTELI2C_TARGET_STATUS Status;
    ULONG StopConditions;
    ULONG ReceivedTotal;
    ULONG Mismatch;
    DWORD Elapsed;
    DWORD Error;
    ULONG Index;

    for (Index = 0; Index < Length; Index++)
    {
        WritePattern[Index] = (UCHAR)(0x11 + Seed * 0x40 + Index * 7);
        ResponsePattern[Index] = (UCHAR)(0xa5 ^ (Seed * 0x1d + Index * 13));
    }

    Error = I2cLoopTargetStart(Target, I2CLOOP_TARGET_ADDRESS, ResponsePattern, Length);
    if (Error != ERROR_SUCCESS)
    {
        I2cLoopStep(WriteStep, FALSE, "%s target start at 0x%02x failed, error %lu", Target->Name, I2CLOOP_TARGET_ADDRESS, Error);
        I2cLoopStep(ReadStep, FALSE, "skipped, %s is not a target", Target->Name);
        if (I2cLoopTargetQuery(Target, &Status) == ERROR_SUCCESS)
            I2cLoopPrintTarget(Target, "start", &Status);
        return;
    }

    Error = I2cLoopMasterTransfer(Master, I2CLOOP_TARGET_ADDRESS, IntelI2cTransferDirectionWrite, WritePattern, Length, &Elapsed);
    if (Error != ERROR_SUCCESS)
    {
        I2cLoopStep(WriteStep, FALSE, "%s master write of %lu bytes to 0x%02x failed, error %lu after %lu ms", Master->Name, Length, I2CLOOP_TARGET_ADDRESS, Error, Elapsed);
        if (I2cLoopTargetQuery(Target, &Status) == ERROR_SUCCESS)
            I2cLoopPrintTarget(Target, "after write", &Status);
    }
    else
    {
        Error = I2cLoopWaitTarget(Target, 0, Length, &Status);
        Mismatch = I2cLoopFirstMismatch(WritePattern, Status.Received, min(Length, Status.ReceivedLength));
        if (Error != ERROR_SUCCESS)
            I2cLoopStep(WriteStep, FALSE, "%s received %lu of %lu bytes, stops=%lu, error %lu", Target->Name, Status.ReceivedLength, Length, Status.StopConditions, Error);
        else if (Status.ReceivedLength != Length || Status.ReceivedTotal != Length)
            I2cLoopStep(WriteStep, FALSE, "%s received %lu bytes (total %lu), expected %lu", Target->Name, Status.ReceivedLength, Status.ReceivedTotal, Length);
        else if (Mismatch != Length)
            I2cLoopStep(WriteStep, FALSE, "%s byte %lu is 0x%02x, expected 0x%02x", Target->Name, Mismatch, Status.Received[Mismatch], WritePattern[Mismatch]);
        else if (Status.Errors)
            I2cLoopStep(WriteStep, FALSE, "%s reported %lu errors, intr=0x%08lx abort=0x%08lx", Target->Name, Status.Errors, Status.LastErrorInterrupts, Status.LastAbortSource);
        else
            I2cLoopStep(WriteStep, TRUE, "%s wrote %lu bytes to %s at 0x%02x in %lu ms", Master->Name, Length, Target->Name, I2CLOOP_TARGET_ADDRESS, Elapsed);
    }

    Error = I2cLoopTargetQuery(Target, &Status);
    StopConditions = Error == ERROR_SUCCESS ? Status.StopConditions : 0;
    ReceivedTotal = Error == ERROR_SUCCESS ? Status.ReceivedTotal : 0;
    ZeroMemory(ReadBack, sizeof(ReadBack));
    Error = I2cLoopMasterTransfer(Master, I2CLOOP_TARGET_ADDRESS, IntelI2cTransferDirectionRead, ReadBack, Length, &Elapsed);
    if (Error != ERROR_SUCCESS)
    {
        I2cLoopStep(ReadStep, FALSE, "%s master read of %lu bytes from 0x%02x failed, error %lu after %lu ms", Master->Name, Length, I2CLOOP_TARGET_ADDRESS, Error, Elapsed);
        if (I2cLoopTargetQuery(Target, &Status) == ERROR_SUCCESS)
            I2cLoopPrintTarget(Target, "after read", &Status);
    }
    else
    {
        Error = I2cLoopWaitTarget(Target, StopConditions, 0, &Status);
        Mismatch = I2cLoopFirstMismatch(ResponsePattern, ReadBack, Length);
        if (Mismatch != Length)
            I2cLoopStep(ReadStep, FALSE, "%s read byte %lu as 0x%02x, expected 0x%02x", Master->Name, Mismatch, ReadBack[Mismatch], ResponsePattern[Mismatch]);
        else if (Error != ERROR_SUCCESS)
            I2cLoopStep(ReadStep, FALSE, "%s saw no stop after the read, error %lu", Target->Name, Error);
        else if (!Status.ReadRequests || Status.ReceivedTotal != ReceivedTotal)
            I2cLoopStep(ReadStep, FALSE, "%s counters inconsistent, rdreq=%lu received=%lu before=%lu", Target->Name, Status.ReadRequests, Status.ReceivedTotal, ReceivedTotal);
        else if (Status.Errors)
            I2cLoopStep(ReadStep, FALSE, "%s reported %lu errors, intr=0x%08lx abort=0x%08lx", Target->Name, Status.Errors, Status.LastErrorInterrupts, Status.LastAbortSource);
        else
            I2cLoopStep(ReadStep, TRUE, "%s read %lu bytes from %s at 0x%02x in %lu ms", Master->Name, Length, Target->Name, I2CLOOP_TARGET_ADDRESS, Elapsed);
    }

    Error = I2cLoopTargetStop(Target);
    if (Error != ERROR_SUCCESS)
        I2cLoopStep(ReadStep, FALSE, "%s target stop failed, error %lu", Target->Name, Error);
}

static
VOID
I2cLoopRunNegative(
    _In_ PI2CLOOP_CONTROLLER Master,
    _In_ PI2CLOOP_CONTROLLER Target)
{
    INTELI2C_TARGET_STATUS Status;
    UCHAR Data = 0x5a;
    DWORD Elapsed;
    DWORD Error;

    Error = I2cLoopTargetStart(Target, I2CLOOP_TARGET_ADDRESS, &Data, 1);
    if (Error != ERROR_SUCCESS)
    {
        I2cLoopStep("d", FALSE, "%s target start at 0x%02x failed, error %lu", Target->Name, I2CLOOP_TARGET_ADDRESS, Error);
        I2cLoopStep("e", FALSE, "skipped, %s is not a target", Target->Name);
        return;
    }

    Error = I2cLoopMasterTransfer(Master, I2CLOOP_ABSENT_ADDRESS, IntelI2cTransferDirectionWrite, &Data, 1, &Elapsed);
    if (Error == ERROR_SUCCESS)
        I2cLoopStep("d", FALSE, "%s write to absent address 0x%02x was acknowledged", Master->Name, I2CLOOP_ABSENT_ADDRESS);
    else if (Error != ERROR_FILE_NOT_FOUND || Elapsed >= I2CLOOP_TRANSFER_TIMEOUT_MS)
        I2cLoopStep("d", FALSE, "%s write to absent address 0x%02x failed with error %lu after %lu ms, expected a NACK abort", Master->Name, I2CLOOP_ABSENT_ADDRESS, Error, Elapsed);
    else if (I2cLoopTargetQuery(Target, &Status) != ERROR_SUCCESS || Status.ReceivedTotal || Status.ReadRequests)
        I2cLoopStep("d", FALSE, "%s answered address 0x%02x while at 0x%02x, received=%lu rdreq=%lu", Target->Name, I2CLOOP_ABSENT_ADDRESS, I2CLOOP_TARGET_ADDRESS, Status.ReceivedTotal, Status.ReadRequests);
    else
        I2cLoopStep("d", TRUE, "%s write to absent address 0x%02x failed with NACK abort (error %lu) in %lu ms", Master->Name, I2CLOOP_ABSENT_ADDRESS, Error, Elapsed);

    Error = I2cLoopMasterTransfer(Target, I2CLOOP_ABSENT_ADDRESS, IntelI2cTransferDirectionWrite, &Data, 1, &Elapsed);
    if (Error != ERROR_BAD_COMMAND)
    {
        I2cLoopStep("e", FALSE, "%s master transfer in target mode returned error %lu, expected %lu", Target->Name, Error, (DWORD)ERROR_BAD_COMMAND);
        I2cLoopTargetStop(Target);
        return;
    }
    Error = I2cLoopTargetStop(Target);
    if (Error != ERROR_SUCCESS)
    {
        I2cLoopStep("e", FALSE, "%s target stop failed, error %lu", Target->Name, Error);
        return;
    }
    Error = I2cLoopMasterTransfer(Target, I2CLOOP_ABSENT_ADDRESS, IntelI2cTransferDirectionWrite, &Data, 1, &Elapsed);
    if (Error != ERROR_FILE_NOT_FOUND || Elapsed >= I2CLOOP_TRANSFER_TIMEOUT_MS)
        I2cLoopStep("e", FALSE, "%s master write after target stop returned error %lu after %lu ms, expected a NACK abort", Target->Name, Error, Elapsed);
    else
        I2cLoopStep("e", TRUE, "%s rejected master transfers in target mode and NACKed 0x%02x as master after stop", Target->Name, I2CLOOP_ABSENT_ADDRESS);
}

int
main(
    int argc,
    char **argv)
{
    I2CLOOP_CONTROLLER I2c2;
    I2CLOOP_CONTROLLER I2c3;
    ULONG Length = I2CLOOP_PATTERN_LENGTH;

    ZeroMemory(&I2c2, sizeof(I2c2));
    ZeroMemory(&I2c3, sizeof(I2c3));
    I2c2.Device = INVALID_HANDLE_VALUE;
    I2c2.Name = "I2C2";
    I2c3.Device = INVALID_HANDLE_VALUE;
    I2c3.Name = "I2C3";
    if (argc > 1)
        I2cLoopSpeed = strtoul(argv[1], NULL, 0);
    I2cLoopPrint("I2CLOOP START speed=%lu Hz target=0x%02x\n", I2cLoopSpeed, I2CLOOP_TARGET_ADDRESS);
    I2cLoopOpenControllers(&I2c2, &I2c3);
    if (I2c2.Device == INVALID_HANDLE_VALUE || I2c3.Device == INVALID_HANDLE_VALUE)
    {
        I2cLoopStep("open", FALSE, "%s%s%s not bound to inteli2c", I2c2.Device == INVALID_HANDLE_VALUE ? "I2C2 (8086:54EA)" : "", I2c2.Device == INVALID_HANDLE_VALUE && I2c3.Device == INVALID_HANDLE_VALUE ? " and " : "", I2c3.Device == INVALID_HANDLE_VALUE ? "I2C3 (8086:54EB)" : "");
    }
    else
    {
        Length = min(Length, min(min(I2c2.Information.TxFifoDepth, I2c2.Information.RxFifoDepth), min(I2c3.Information.TxFifoDepth, I2c3.Information.RxFifoDepth)));
        if (Length != I2CLOOP_PATTERN_LENGTH)
            I2cLoopPrint("I2CLOOP INFO pattern limited to %lu bytes by the FIFO depth\n", Length);
        I2cLoopRunPair(&I2c2, &I2c3, Length, 1, "a", "b");
        I2cLoopRunPair(&I2c3, &I2c2, Length, 2, "c.a", "c.b");
        I2cLoopRunNegative(&I2c2, &I2c3);
    }
    if (I2c2.Device != INVALID_HANDLE_VALUE)
        CloseHandle(I2c2.Device);
    if (I2c3.Device != INVALID_HANDLE_VALUE)
        CloseHandle(I2c3.Device);
    if (I2cLoopFailure[0])
    {
        I2cLoopPrint("I2CLOOP RESULT FAIL %s\n", I2cLoopFailure);
        return 1;
    }
    I2cLoopPrint("I2CLOOP RESULT PASS\n");
    return 0;
}
