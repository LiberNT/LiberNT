/*
 * PROJECT:     LiberNT Apple AOP Driver
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Ambient light and lid angle sensors behind the Apple Always-On Processor
 * COPYRIGHT:   Copyright (C) The Asahi Linux Contributors
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "appleaop.h"

#define EPIC_SUBTYPE_SET_ALS_PROPERTY   0x04
#define EPIC_SUBTYPE_GET_AOP_PROPERTY   0x0a
#define AOP_PROPERTY_SENSOR_NAME        0x0f
#define AOP_SENSOR_NAME_LENGTH          16
#define ALS_PROPERTY_0                  0x00
#define ALS_PROPERTY_0_VALUE            200000
#define ALS_PROPERTY_CALIBRATION        0x0b
#define ALS_LUX_OFFSET_CT720            0x1d
#define ALS_LUX_OFFSET_VD6286           0x28
#define ALS_CALIBRATION_MAX             0x1000
#define ALS_CALIBRATION_PATH            L"\\SystemRoot\\System32\\drivers\\firmware\\apple\\aop-als-cal.bin"
#define ALS_LOG_MIN_LUX                 10
#define LAS_ANGLE_OFFSET                1
#define LAS_LOG_MIN_DEGREES             5

static const PCSTR AopSensorNames[AOP_SENSORS] = { "ambient light", "lid angle" };

static
ULONG
AopFloatToUnsigned(
    _In_ ULONG Bits)
{
    LONG Exponent;
    ULONG Mantissa;

    if (Bits & 0x80000000)
        return 0;
    Exponent = (LONG)((Bits & 0x7f800000) >> 23) - 127;
    if (Exponent < 0)
        return 0;
    if (Exponent == 128 && (Bits & 0x7fffff))
        return 0;
    Mantissa = (Bits & 0x7fffff) | 0x800000;
    if (Exponent <= 23)
        return Mantissa >> (23 - Exponent);
    if (Exponent >= 32)
        return MAXULONG;
    return Mantissa << (Exponent - 23);
}

static
NTSTATUS
AopGetProperty(
    _Inout_ PAOP_DEVICE Device,
    _In_ ULONG Service,
    _In_ ULONG Tag,
    _Out_writes_bytes_(Length) PUCHAR Value,
    _In_ ULONG Length,
    _Out_ PULONG Returned,
    _Out_ PULONG ReturnCode)
{
    UCHAR Request[8];

    RtlZeroMemory(Request, sizeof(Request));
    RtlCopyMemory(Request + 4, &Tag, sizeof(Tag));
    return AopEpicCall(Device, Service, EPIC_SUBTYPE_GET_AOP_PROPERTY, Request, sizeof(Request), Value, Length,
                       Returned, ReturnCode);
}

static
NTSTATUS
AopSetAlsProperty(
    _Inout_ PAOP_DEVICE Device,
    _In_ ULONG Service,
    _In_ ULONG Tag,
    _In_reads_bytes_(Length) const VOID *Data,
    _In_ ULONG Length,
    _Out_ PULONG ReturnCode)
{
    PUCHAR Request;
    NTSTATUS Status;

    *ReturnCode = 0;
    if (Length > ALS_CALIBRATION_MAX)
        return STATUS_INVALID_PARAMETER;
    Request = ExAllocatePoolWithTag(PagedPool, Length + 8, AOP_TAG);
    if (!Request)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(Request, 8);
    RtlCopyMemory(Request + 4, &Tag, sizeof(Tag));
    RtlCopyMemory(Request + 8, Data, Length);
    Status = AopEpicCall(Device, Service, EPIC_SUBTYPE_SET_ALS_PROPERTY, Request, Length + 8, NULL, 0, NULL,
                         ReturnCode);
    ExFreePoolWithTag(Request, AOP_TAG);
    return Status;
}

static
NTSTATUS
AopLoadAlsCalibration(
    _Outptr_result_bytebuffer_(*Length) PUCHAR *Data,
    _Out_ PULONG Length)
{
    UNICODE_STRING Path = RTL_CONSTANT_STRING(ALS_CALIBRATION_PATH);
    OBJECT_ATTRIBUTES Attributes;
    FILE_STANDARD_INFORMATION Info;
    IO_STATUS_BLOCK Io;
    PUCHAR File = NULL;
    HANDLE Handle;
    NTSTATUS Status;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    *Data = NULL;
    *Length = 0;
    InitializeObjectAttributes(&Attributes, &Path, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = ZwCreateFile(&Handle, GENERIC_READ | SYNCHRONIZE, &Attributes, &Io, NULL, FILE_ATTRIBUTE_NORMAL,
                          FILE_SHARE_READ, FILE_OPEN, FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE, NULL, 0);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = ZwQueryInformationFile(Handle, &Io, &Info, sizeof(Info), FileStandardInformation);
    if (NT_SUCCESS(Status) && (Info.EndOfFile.QuadPart <= 0 || Info.EndOfFile.QuadPart > ALS_CALIBRATION_MAX))
        Status = STATUS_INVALID_IMAGE_FORMAT;
    if (NT_SUCCESS(Status))
    {
        File = ExAllocatePoolWithTag(PagedPool, Info.EndOfFile.LowPart, AOP_TAG);
        Status = File ? ZwReadFile(Handle, NULL, NULL, NULL, &Io, File, Info.EndOfFile.LowPart, NULL, NULL)
                      : STATUS_INSUFFICIENT_RESOURCES;
        if (NT_SUCCESS(Status) && Io.Information != Info.EndOfFile.LowPart)
            Status = STATUS_END_OF_FILE;
    }
    ZwClose(Handle);
    if (!NT_SUCCESS(Status))
    {
        if (File)
            ExFreePoolWithTag(File, AOP_TAG);
        return Status;
    }
    *Data = File;
    *Length = Info.EndOfFile.LowPart;
    return STATUS_SUCCESS;
}

static
VOID
AopSensorAttach(
    _Inout_ PAOP_DEVICE Device,
    _In_ ULONG Sensor,
    _In_ LONG Service,
    _In_ ULONG ReportOffset)
{
    PAOP_SENSOR Entry = &Device->Sensors[Sensor];
    KIRQL OldIrql;

    ASSERT(Sensor < AOP_FUNCTIONS);
    KeAcquireSpinLock(&Device->SensorLock, &OldIrql);
    Entry->Service = Service;
    Entry->ReportOffset = ReportOffset;
    Entry->Listening = (Service >= 0);
    KeReleaseSpinLock(&Device->SensorLock, OldIrql);
}

static
VOID
AopStartAmbientLight(
    _Inout_ PAOP_DEVICE Device,
    _In_ ULONG Service)
{
    CHAR Name[AOP_SENSOR_NAME_LENGTH + 1];
    ULONG Returned, ReturnCode, Offset, Length, Value = ALS_PROPERTY_0_VALUE;
    PUCHAR Calibration;
    NTSTATUS Status;

    RtlZeroMemory(Name, sizeof(Name));
    Status = AopGetProperty(Device, Service, AOP_PROPERTY_SENSOR_NAME, (PUCHAR)Name, AOP_SENSOR_NAME_LENGTH,
                            &Returned, &ReturnCode);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint("appleaop: ambient light sensor name not read: %lx\n", Status);
        return;
    }
    Name[min(Returned, (ULONG)AOP_SENSOR_NAME_LENGTH)] = '\0';
    if (!strcmp(Name, "Redbird"))
    {
        Offset = ALS_LUX_OFFSET_VD6286;
    }
    else if (!strcmp(Name, "FireFish2"))
    {
        Offset = ALS_LUX_OFFSET_CT720;
    }
    else
    {
        DbgPrint("appleaop: ambient light sensor %s (code %lx, %lu bytes) is unknown, left off\n", Name, ReturnCode,
                 Returned);
        return;
    }
    DbgPrint("appleaop: ambient light sensor %s, lux at report byte %#lx\n", Name, Offset);

    Status = AopLoadAlsCalibration(&Calibration, &Length);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint("appleaop: ambient light calibration %ws not loaded: %lx, the sensor stays off\n",
                 ALS_CALIBRATION_PATH, Status);
        return;
    }
    AopSensorAttach(Device, AOP_SENSOR_ALS, (LONG)Service, Offset);
    Status = AopSetAlsProperty(Device, Service, ALS_PROPERTY_CALIBRATION, Calibration, Length, &ReturnCode);
    ExFreePoolWithTag(Calibration, AOP_TAG);
    DbgPrint("appleaop: ambient light calibration of %lu bytes sent: %lx, code %lx\n", Length, Status, ReturnCode);
    if (NT_SUCCESS(Status))
    {
        Status = AopSetAlsProperty(Device, Service, ALS_PROPERTY_0, &Value, sizeof(Value), &ReturnCode);
        DbgPrint("appleaop: ambient light property 0 set to %lu: %lx, code %lx\n", Value, Status, ReturnCode);
    }
    if (!NT_SUCCESS(Status))
        AopSensorAttach(Device, AOP_SENSOR_ALS, -1, 0);
}

static
VOID
AopStartLidAngle(
    _Inout_ PAOP_DEVICE Device,
    _In_ ULONG Service)
{
    AopSensorAttach(Device, AOP_SENSOR_LAS, (LONG)Service, LAS_ANGLE_OFFSET);
    DbgPrint("appleaop: lid angle sensor listening\n");
}

VOID
AopSensorsSetup(
    _Inout_ PAOP_DEVICE Device)
{
    CHAR Name[AOP_SERVICE_NAME + 1];
    ULONG Index;
    BOOLEAN Found;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    for (;;)
    {
        Found = FALSE;
        AopLock(Device);
        for (Index = 0; Index < Device->ServiceCount; ++Index)
        {
            if (Device->Services[Index].Handled)
                continue;
            Device->Services[Index].Handled = TRUE;
            RtlCopyMemory(Name, Device->Services[Index].Name, sizeof(Name));
            Found = TRUE;
            break;
        }
        AopUnlock(Device);
        if (!Found)
            break;
        if (!strcmp(Name, "als"))
        {
            AopStartAmbientLight(Device, Index);
        }
        else if (!strcmp(Name, "las"))
        {
            AopStartLidAngle(Device, Index);
        }
        else if (!strcmp(Name, "aop-audio"))
        {
            AopSensorAttach(Device, AOP_FUNCTION_AUDIO, (LONG)Index, 0);
            DbgPrint("appleaop: microphone service ready\n");
        }
    }
}

VOID
AopSensorReport(
    _Inout_ PAOP_DEVICE Device,
    _In_ ULONG Service,
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length)
{
    PAOP_SENSOR Entry = NULL;
    ULONG Sensor, Value, Bits, Delta, Reports = 0;
    BOOLEAN Log = FALSE, Short = FALSE;
    KIRQL OldIrql;

    ASSERT(Device->LockOwner == KeGetCurrentThread());
    KeAcquireSpinLock(&Device->SensorLock, &OldIrql);
    for (Sensor = 0; Sensor < AOP_SENSORS; ++Sensor)
    {
        if (Device->Sensors[Sensor].Listening && Device->Sensors[Sensor].Service == (LONG)Service)
        {
            Entry = &Device->Sensors[Sensor];
            break;
        }
    }
    if (!Entry)
    {
        KeReleaseSpinLock(&Device->SensorLock, OldIrql);
        return;
    }
    if (Length < Entry->ReportOffset + (Sensor == AOP_SENSOR_ALS ? sizeof(ULONG) : sizeof(UCHAR)))
    {
        Short = (Entry->Malformed++ == 0);
        KeReleaseSpinLock(&Device->SensorLock, OldIrql);
        if (Short)
            DbgPrint("appleaop: %s report of %lu bytes is too short\n", AopSensorNames[Sensor], Length);
        return;
    }
    if (Sensor == AOP_SENSOR_ALS)
    {
        RtlCopyMemory(&Bits, Data + Entry->ReportOffset, sizeof(Bits));
        Value = AopFloatToUnsigned(Bits);
    }
    else
    {
        Value = Data[Entry->ReportOffset];
    }
    Entry->Value = Value;
    Entry->HaveValue = TRUE;
    Reports = ++Entry->Reports;
    Delta = (Value > Entry->LoggedValue) ? Value - Entry->LoggedValue : Entry->LoggedValue - Value;
    if (!Entry->Logged ||
        (Sensor == AOP_SENSOR_ALS ? Delta >= max((ULONG)ALS_LOG_MIN_LUX, Entry->LoggedValue / 4)
                                  : Delta >= LAS_LOG_MIN_DEGREES))
    {
        Entry->Logged = TRUE;
        Entry->LoggedValue = Value;
        Log = TRUE;
    }
    if (Entry->Callback)
        Entry->Callback(Entry->CallbackContext, Value);
    KeReleaseSpinLock(&Device->SensorLock, OldIrql);
    if (Log)
    {
        DbgPrint("appleaop: %s %lu %s (report %lu)\n", AopSensorNames[Sensor], Value,
                 Sensor == AOP_SENSOR_ALS ? "lux" : "degrees", Reports);
    }
}
