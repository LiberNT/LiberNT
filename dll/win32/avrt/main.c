/*
 * PROJECT:     LiberNT Multimedia Class Scheduler
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Registers threads for multimedia tasks with mmcss.sys
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include <stdarg.h>

#define WIN32_NO_STATUS
#define _INC_WINDOWS
#define COM_NO_WINDOWS_H
#include <windef.h>
#include <winbase.h>
#include <winnls.h>
#define NTOS_MODE_USER
#include <ndk/iofuncs.h>
#include <ndk/obfuncs.h>
#include <ndk/rtlfuncs.h>
#include <ntstatus.h>
#include <winioctl.h>
#include <avrt.h>
#include <drivers/mmcssio.h>

static
VOID
AvrtSetStatus(
    _In_ NTSTATUS Status)
{
    switch (Status)
    {
        case STATUS_OBJECT_NAME_NOT_FOUND:
            SetLastError(ERROR_INVALID_TASK_NAME);
            break;
        case STATUS_INVALID_PARAMETER_1:
            SetLastError(ERROR_INVALID_TASK_INDEX);
            break;
        case STATUS_ALREADY_REGISTERED:
            SetLastError(ERROR_THREAD_ALREADY_IN_TASK);
            break;
        default:
            SetLastError(RtlNtStatusToDosError(Status));
            break;
    }
}

static
NTSTATUS
AvrtControl(
    _In_ HANDLE Handle,
    _In_ ULONG Code,
    _In_reads_bytes_opt_(InputLength) PVOID Input,
    _In_ ULONG InputLength,
    _Out_writes_bytes_opt_(OutputLength) PVOID Output,
    _In_ ULONG OutputLength)
{
    IO_STATUS_BLOCK IoStatus;

    return NtDeviceIoControlFile(Handle, NULL, NULL, NULL, &IoStatus, Code,
                                 Input, InputLength, Output, OutputLength);
}

static
BOOL
AvrtCopyTaskName(
    _Out_writes_(MMCSS_MAX_TASK_NAME) PWCHAR Destination,
    _In_ PCWSTR Name)
{
    SIZE_T Length = wcslen(Name);

    if (Length == 0 || Length >= MMCSS_MAX_TASK_NAME)
        return FALSE;

    RtlCopyMemory(Destination, Name, Length * sizeof(WCHAR));
    Destination[Length] = UNICODE_NULL;
    return TRUE;
}

static
HANDLE
AvrtRegister(
    _In_ PCWSTR FirstTask,
    _In_opt_ PCWSTR SecondTask,
    _Inout_ LPDWORD TaskIndex)
{
    UNICODE_STRING DeviceName = RTL_CONSTANT_STRING(MMCSS_DEVICE_NAME);
    MMCSS_REGISTER_INPUT Input = {0};
    MMCSS_REGISTER_OUTPUT Output = {0};
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatus;
    HANDLE Handle;
    NTSTATUS Status;

    Input.TaskIndex = *TaskIndex;
    if (!AvrtCopyTaskName(Input.FirstTask, FirstTask) ||
        (SecondTask != NULL && !AvrtCopyTaskName(Input.SecondTask, SecondTask)))
    {
        SetLastError(ERROR_INVALID_TASK_NAME);
        return NULL;
    }

    InitializeObjectAttributes(&ObjectAttributes, &DeviceName, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtCreateFile(&Handle,
                          FILE_GENERIC_READ,
                          &ObjectAttributes,
                          &IoStatus,
                          NULL,
                          0,
                          FILE_SHARE_READ | FILE_SHARE_WRITE,
                          FILE_OPEN,
                          FILE_SYNCHRONOUS_IO_NONALERT,
                          NULL,
                          0);
    if (!NT_SUCCESS(Status))
    {
        AvrtSetStatus(Status);
        return NULL;
    }

    Status = AvrtControl(Handle, IOCTL_MMCSS_REGISTER, &Input, sizeof(Input), &Output, sizeof(Output));
    if (!NT_SUCCESS(Status))
    {
        NtClose(Handle);
        AvrtSetStatus(Status);
        return NULL;
    }

    *TaskIndex = Output.TaskIndex;
    return Handle;
}

static
PWSTR
AvrtStrDupAW(
    _In_ PCSTR Source)
{
    PWSTR Destination;
    INT Length;

    Length = MultiByteToWideChar(CP_ACP, 0, Source, (INT)strlen(Source) + 1, NULL, 0);
    if (Length == 0)
        return NULL;
    Destination = HeapAlloc(GetProcessHeap(), 0, Length * sizeof(WCHAR));
    if (Destination != NULL)
        MultiByteToWideChar(CP_ACP, 0, Source, -1, Destination, Length);
    return Destination;
}

HANDLE
WINAPI
AvSetMmThreadCharacteristicsW(
    _In_ LPCWSTR TaskName,
    _Inout_ LPDWORD TaskIndex)
{
    return AvrtRegister(TaskName, NULL, TaskIndex);
}

HANDLE
WINAPI
AvSetMmThreadCharacteristicsA(
    _In_ LPCSTR TaskName,
    _Inout_ LPDWORD TaskIndex)
{
    PWSTR TaskNameW;
    HANDLE Handle;

    TaskNameW = AvrtStrDupAW(TaskName);
    if (TaskNameW == NULL)
    {
        SetLastError(ERROR_OUTOFMEMORY);
        return NULL;
    }

    Handle = AvSetMmThreadCharacteristicsW(TaskNameW, TaskIndex);
    HeapFree(GetProcessHeap(), 0, TaskNameW);
    return Handle;
}

HANDLE
WINAPI
AvSetMmMaxThreadCharacteristicsW(
    _In_ LPCWSTR FirstTask,
    _In_opt_ LPCWSTR SecondTask,
    _Inout_ LPDWORD TaskIndex)
{
    return AvrtRegister(FirstTask, SecondTask, TaskIndex);
}

HANDLE
WINAPI
AvSetMmMaxThreadCharacteristicsA(
    _In_ LPCSTR FirstTask,
    _In_opt_ LPCSTR SecondTask,
    _Inout_ LPDWORD TaskIndex)
{
    PWSTR FirstTaskW, SecondTaskW = NULL;
    HANDLE Handle = NULL;

    FirstTaskW = AvrtStrDupAW(FirstTask);
    if (SecondTask != NULL)
        SecondTaskW = AvrtStrDupAW(SecondTask);

    if (FirstTaskW == NULL || (SecondTask != NULL && SecondTaskW == NULL))
        SetLastError(ERROR_OUTOFMEMORY);
    else
        Handle = AvSetMmMaxThreadCharacteristicsW(FirstTaskW, SecondTaskW, TaskIndex);

    if (SecondTaskW != NULL)
        HeapFree(GetProcessHeap(), 0, SecondTaskW);
    if (FirstTaskW != NULL)
        HeapFree(GetProcessHeap(), 0, FirstTaskW);
    return Handle;
}

BOOL
WINAPI
AvRevertMmThreadCharacteristics(
    _In_ HANDLE AvrtHandle)
{
    NTSTATUS Status;

    Status = NtClose(AvrtHandle);
    if (!NT_SUCCESS(Status))
    {
        AvrtSetStatus(Status);
        return FALSE;
    }
    return TRUE;
}

BOOL
WINAPI
AvSetMmThreadPriority(
    _In_ HANDLE AvrtHandle,
    _In_ AVRT_PRIORITY Priority)
{
    MMCSS_SET_PRIORITY_INPUT Input;
    NTSTATUS Status;

    Input.Priority = Priority;
    Status = AvrtControl(AvrtHandle, IOCTL_MMCSS_SET_PRIORITY, &Input, sizeof(Input), NULL, 0);
    if (!NT_SUCCESS(Status))
    {
        AvrtSetStatus(Status);
        return FALSE;
    }
    return TRUE;
}

BOOL
WINAPI
AvQuerySystemResponsiveness(
    _In_ HANDLE AvrtHandle,
    _Out_ PULONG SystemResponsivenessValue)
{
    MMCSS_RESPONSIVENESS_OUTPUT Output;
    NTSTATUS Status;

    Status = AvrtControl(AvrtHandle, IOCTL_MMCSS_QUERY_RESPONSIVENESS, NULL, 0, &Output, sizeof(Output));
    if (!NT_SUCCESS(Status))
    {
        AvrtSetStatus(Status);
        return FALSE;
    }

    *SystemResponsivenessValue = Output.SystemResponsiveness;
    return TRUE;
}
