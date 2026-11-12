/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Transactional NTFS commit recovery test, fault-injecting minifilter
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <fltkernel.h>

#define NDEBUG
#include <debug.h>

#include "NtfsTxfRecovery.h"

static KMT_MESSAGE_HANDLER TestMessageHandler;

static WCHAR ServicePath[260];
static PDRIVER_OBJECT TestDriverObject;
static PFLT_FILTER Filter;
static PETHREAD TrapThread;
static LONG TrapKind;
static LONG TrapFailAt;
static LONG TrapSeen;
static LONG TrapFired;

static
BOOLEAN
TrapMatches(
    _In_ PFLT_CALLBACK_DATA Data,
    _In_ LONG Kind)
{
    return ReadAcquire(&TrapKind) == Kind &&
           TrapThread == PsGetCurrentThread() &&
           Data->RequestorMode == KernelMode;
}

static
FLT_PREOP_CALLBACK_STATUS
TrapCount(
    _Inout_ PFLT_CALLBACK_DATA Data)
{
    if (FLT_IS_FASTIO_OPERATION(Data))
    {
        return FLT_PREOP_DISALLOW_FASTIO;
    }
    if (InterlockedIncrement(&TrapSeen) != TrapFailAt)
    {
        return FLT_PREOP_SUCCESS_NO_CALLBACK;
    }
    InterlockedIncrement(&TrapFired);
    Data->IoStatus.Status = STATUS_UNEXPECTED_IO_ERROR;
    Data->IoStatus.Information = 0;
    return FLT_PREOP_COMPLETE;
}

static
FLT_PREOP_CALLBACK_STATUS
FLTAPI
PreSetInformation(
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _Out_ PVOID *CompletionContext)
{
    FILE_INFORMATION_CLASS Class = Data->Iopb->Parameters.SetFileInformation.FileInformationClass;

    UNREFERENCED_PARAMETER(FltObjects);

    *CompletionContext = NULL;
    if (!TrapMatches(Data, NTFSTXF_TRAP_NAMESPACE) ||
        (Class != FileRenameInformation && Class != FileRenameInformationEx &&
         Class != FileDispositionInformation && Class != FileDispositionInformationEx &&
         Class != FileLinkInformation && Class != FileLinkInformationEx))
    {
        return FLT_PREOP_SUCCESS_NO_CALLBACK;
    }
    return TrapCount(Data);
}

static
FLT_PREOP_CALLBACK_STATUS
FLTAPI
PreWrite(
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _Out_ PVOID *CompletionContext)
{
    UNREFERENCED_PARAMETER(FltObjects);

    *CompletionContext = NULL;
    if (!TrapMatches(Data, NTFSTXF_TRAP_WRITE) || (Data->Iopb->IrpFlags & IRP_PAGING_IO))
    {
        return FLT_PREOP_SUCCESS_NO_CALLBACK;
    }
    return TrapCount(Data);
}

static
NTSTATUS
FLTAPI
InstanceSetup(
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ FLT_INSTANCE_SETUP_FLAGS Flags,
    _In_ DEVICE_TYPE VolumeDeviceType,
    _In_ FLT_FILESYSTEM_TYPE VolumeFilesystemType)
{
    UNREFERENCED_PARAMETER(FltObjects);
    UNREFERENCED_PARAMETER(Flags);
    UNREFERENCED_PARAMETER(VolumeFilesystemType);

    return VolumeDeviceType == FILE_DEVICE_DISK_FILE_SYSTEM ? STATUS_SUCCESS : STATUS_FLT_DO_NOT_ATTACH;
}

static
NTSTATUS
FLTAPI
InstanceQueryTeardown(
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ FLT_INSTANCE_QUERY_TEARDOWN_FLAGS Flags)
{
    UNREFERENCED_PARAMETER(FltObjects);
    UNREFERENCED_PARAMETER(Flags);

    return STATUS_SUCCESS;
}

static CONST FLT_OPERATION_REGISTRATION Operations[] =
{
    { IRP_MJ_SET_INFORMATION, 0, PreSetInformation, NULL },
    { IRP_MJ_WRITE, 0, PreWrite, NULL },
    { IRP_MJ_OPERATION_END }
};

static CONST FLT_REGISTRATION Registration =
{
    sizeof(FLT_REGISTRATION),
    FLT_REGISTRATION_VERSION,
    FLTFL_REGISTRATION_DO_NOT_SUPPORT_SERVICE_STOP,
    NULL,
    Operations,
    NULL,
    InstanceSetup,
    InstanceQueryTeardown,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL
};

static
NTSTATUS
WriteInstanceRegistry(VOID)
{
    static WCHAR InstanceName[] = L"NtfsTxfRecovery Instance";
    static WCHAR Altitude[] = L"370126";
    WCHAR Path[320];
    ULONG Flags = 0;
    NTSTATUS Status;

    RtlStringCbPrintfW(Path, sizeof(Path), L"%ls\\Instances", ServicePath);
    Status = RtlCreateRegistryKey(RTL_REGISTRY_ABSOLUTE, Path);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    Status = RtlWriteRegistryValue(RTL_REGISTRY_ABSOLUTE,
                                   Path,
                                   L"DefaultInstance",
                                   REG_SZ,
                                   InstanceName,
                                   sizeof(InstanceName));
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    RtlStringCbPrintfW(Path, sizeof(Path), L"%ls\\Instances\\%ls", ServicePath, InstanceName);
    Status = RtlCreateRegistryKey(RTL_REGISTRY_ABSOLUTE, Path);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    Status = RtlWriteRegistryValue(RTL_REGISTRY_ABSOLUTE, Path, L"Altitude", REG_SZ, Altitude, sizeof(Altitude));
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    return RtlWriteRegistryValue(RTL_REGISTRY_ABSOLUTE, Path, L"Flags", REG_DWORD, &Flags, sizeof(Flags));
}

static
NTSTATUS
TestRegister(VOID)
{
    NTSTATUS Status;

    Status = WriteInstanceRegistry();
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    Status = FltRegisterFilter(TestDriverObject, &Registration, &Filter);
    if (!NT_SUCCESS(Status))
    {
        Filter = NULL;
        return Status;
    }
    Status = FltStartFiltering(Filter);
    if (!NT_SUCCESS(Status))
    {
        FltUnregisterFilter(Filter);
        Filter = NULL;
    }
    return Status;
}

static
VOID
TestDisarm(VOID)
{
    InterlockedExchange(&TrapKind, NTFSTXF_TRAP_NONE);
    TrapThread = NULL;
}

static
VOID
TestUnregister(VOID)
{
    TestDisarm();
    if (Filter != NULL)
    {
        FltUnregisterFilter(Filter);
        Filter = NULL;
    }
}

NTSTATUS
TestEntry(
    IN PDRIVER_OBJECT DriverObject,
    IN PCUNICODE_STRING RegistryPath,
    OUT PCWSTR *DeviceName,
    IN OUT INT *Flags)
{
    PAGED_CODE();

    UNREFERENCED_PARAMETER(Flags);

    TestDriverObject = DriverObject;
    if (RegistryPath->Length >= sizeof(ServicePath))
    {
        return STATUS_NAME_TOO_LONG;
    }
    RtlCopyMemory(ServicePath, RegistryPath->Buffer, RegistryPath->Length);
    ServicePath[RegistryPath->Length / sizeof(WCHAR)] = UNICODE_NULL;

    *DeviceName = L"NtfsTxfRecovery";
    KmtRegisterMessageHandler(0, NULL, TestMessageHandler);
    return STATUS_SUCCESS;
}

VOID
TestUnload(
    IN PDRIVER_OBJECT DriverObject)
{
    PAGED_CODE();

    UNREFERENCED_PARAMETER(DriverObject);

    TestUnregister();
}

static
NTSTATUS
TestMessageHandler(
    IN PDEVICE_OBJECT DeviceObject,
    IN ULONG ControlCode,
    IN PVOID Buffer OPTIONAL,
    IN SIZE_T InLength,
    IN OUT PSIZE_T OutLength)
{
    PNTFSTXF_TRAP Trap = Buffer;

    UNREFERENCED_PARAMETER(DeviceObject);

    switch (ControlCode)
    {
        case IOCTL_NTFSTXF_REGISTER:
            *OutLength = 0;
            return TestRegister();

        case IOCTL_NTFSTXF_ARM:
            if (!Buffer || InLength < sizeof(*Trap) || Trap->FailAt == 0 ||
                (Trap->Kind != NTFSTXF_TRAP_NAMESPACE && Trap->Kind != NTFSTXF_TRAP_WRITE))
            {
                return STATUS_INVALID_PARAMETER;
            }
            TestDisarm();
            TrapSeen = 0;
            TrapFired = 0;
            TrapFailAt = (LONG)Trap->FailAt;
            TrapThread = PsGetCurrentThread();
            InterlockedExchange(&TrapKind, (LONG)Trap->Kind);
            *OutLength = 0;
            break;

        case IOCTL_NTFSTXF_QUERY:
            if (!Buffer || *OutLength < sizeof(ULONG))
            {
                return STATUS_BUFFER_TOO_SMALL;
            }
            *(PULONG)Buffer = (ULONG)ReadAcquire(&TrapFired);
            *OutLength = sizeof(ULONG);
            break;

        case IOCTL_NTFSTXF_DISARM:
            TestDisarm();
            *OutLength = 0;
            break;

        case IOCTL_NTFSTXF_UNREGISTER:
            TestUnregister();
            *OutLength = 0;
            break;

        default:
            return STATUS_NOT_SUPPORTED;
    }

    return STATUS_SUCCESS;
}
