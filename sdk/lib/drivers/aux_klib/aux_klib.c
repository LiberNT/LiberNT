/*
 * PROJECT:     ReactOS SDK: Auxiliary Kernel-Mode Library
 * LICENSE:     BSD-2-Clause-Views (https://spdx.org/licenses/BSD-2-Clause-Views)
 * PURPOSE:     Main source file
 * COPYRIGHT:   Copyright 2019-2020 Max Korostil <mrmks04@yandex.ru>
 *              Copyright 2021 Victor Perevertkin <victor.perevertkin@reactos.org>
 */

#include <ntifs.h>
#include <ntintsafe.h>
#include <ndk/ntndk.h>
#include <pseh/pseh2.h>
#include <aux_klib.h>

#define TAG_AUXK 'AuxK'

typedef NTSTATUS (NTAPI *PFN_RTLQUERYMODULEINFORMATION)(PULONG, ULONG, PVOID);

PFN_RTLQUERYMODULEINFORMATION pfnRtlQueryModuleInformation;
LONG gKlibInitialized = 0;


CODE_SEG("PAGE")
NTSTATUS
NTAPI
AuxKlibInitialize(VOID)
{
    RTL_OSVERSIONINFOW osVersion;
    UNICODE_STRING strRtlQueryModuleInformation = RTL_CONSTANT_STRING(L"RtlQueryModuleInformation");

    PAGED_CODE();

    if (!gKlibInitialized)
    {
        RtlGetVersion(&osVersion);
        if (osVersion.dwMajorVersion >= 5)
        {
            pfnRtlQueryModuleInformation = MmGetSystemRoutineAddress(&strRtlQueryModuleInformation);
            InterlockedExchange(&gKlibInitialized, 1);
        }
        else
        {
            return STATUS_NOT_SUPPORTED;
        }
    }

    return STATUS_SUCCESS;
}

CODE_SEG("PAGE")
NTSTATUS
NTAPI
AuxKlibQueryModuleInformation(
    _In_ PULONG InformationLength,
    _In_ ULONG SizePerModule,
    _Inout_ PAUX_MODULE_EXTENDED_INFO ModuleInfo)
{
    NTSTATUS status;

    PAGED_CODE();

    if (gKlibInitialized != 1)
    {
        return STATUS_UNSUCCESSFUL;
    }

    // if we have the function exported from the kernel, use it
    if (pfnRtlQueryModuleInformation != NULL)
    {
        return pfnRtlQueryModuleInformation(InformationLength, SizePerModule, ModuleInfo);
    }

    if (SizePerModule != sizeof(AUX_MODULE_BASIC_INFO) &&
        SizePerModule != sizeof(AUX_MODULE_EXTENDED_INFO))
    {
        return STATUS_INVALID_PARAMETER_2;
    }

    if ((ULONG_PTR)ModuleInfo & (TYPE_ALIGNMENT(AUX_MODULE_EXTENDED_INFO) - 1))
    {
        return STATUS_INVALID_PARAMETER_3;
    }

    // first call the function with a place for only 1 module
    RTL_PROCESS_MODULES processModulesMinimal;
    PRTL_PROCESS_MODULES processModules = &processModulesMinimal;
    ULONG sysInfoLength = sizeof(processModulesMinimal);
    ULONG resultLength;

    // loop until we have a large-enough buffer for all modules
    do
    {
        status = ZwQuerySystemInformation(SystemModuleInformation,
                                          processModules,
                                          sysInfoLength,
                                          &resultLength);

        if (status == STATUS_INFO_LENGTH_MISMATCH)
        {
            // free the old buffer if it's not the first one
            if (processModules != &processModulesMinimal)
            {
                ExFreePoolWithTag(processModules, TAG_AUXK);
            }

            _SEH2_TRY
            {
                // allocate the new one
                processModules = ExAllocatePoolWithQuotaTag(PagedPool, resultLength, TAG_AUXK);
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                _SEH2_YIELD(return _SEH2_GetExceptionCode());
            }
            _SEH2_END;

            if (!processModules)
            {
                return STATUS_INSUFFICIENT_RESOURCES;
            }
            sysInfoLength = resultLength;
        }

    } while (status == STATUS_INFO_LENGTH_MISMATCH);

    if (!NT_SUCCESS(status))
    {
        goto Cleanup;
    }

    ULONG modulesSize;
    status = RtlULongMult(SizePerModule, processModules->NumberOfModules, &modulesSize);
    if (!NT_SUCCESS(status))
    {
        goto Cleanup;
    }

    if (ModuleInfo == NULL)
    {
        ASSERT(status == STATUS_SUCCESS);
        *InformationLength = modulesSize;
        goto Cleanup;
    }

    if (*InformationLength < modulesSize)
    {
        status = STATUS_BUFFER_TOO_SMALL;
        *InformationLength = modulesSize;
        goto Cleanup;
    }

    // copy the information to the input array
    for (UINT32 i = 0; i < processModules->NumberOfModules; i++)
    {
        ModuleInfo[i].BasicInfo.ImageBase = processModules->Modules[i].ImageBase;

        if (SizePerModule == sizeof(AUX_MODULE_EXTENDED_INFO))
        {
            ModuleInfo[i].ImageSize = processModules->Modules[i].ImageSize;
            ModuleInfo[i].FileNameOffset = processModules->Modules[i].OffsetToFileName;
            RtlCopyMemory(&ModuleInfo[i].FullPathName,
                          processModules->Modules[i].FullPathName,
                          sizeof(processModules->Modules[i].FullPathName));
        }
    }

Cleanup:
    // don't accidentally free the stack buffer
    if (processModules != NULL && processModules != &processModulesMinimal)
    {
        ExFreePoolWithTag(processModules, TAG_AUXK);
    }

    return status;
}

NTSTATUS
AuxKlibGetBugCheckData(
    _Inout_ PKBUGCHECK_DATA BugCheckData)
{
    if (BugCheckData->BugCheckDataSize != sizeof(*BugCheckData))
    {
        return STATUS_INFO_LENGTH_MISMATCH;
    }

    BugCheckData->BugCheckCode = KiBugCheckData[0];
    BugCheckData->Parameter1 = KiBugCheckData[1];
    BugCheckData->Parameter2 = KiBugCheckData[2];
    BugCheckData->Parameter3 = KiBugCheckData[3];
    BugCheckData->Parameter4 = KiBugCheckData[4];

    return STATUS_SUCCESS;
}

PIMAGE_EXPORT_DIRECTORY
AuxKlibGetImageExportDirectory(
    _In_ PVOID ImageBase)
{
    ULONG size;
    return RtlImageDirectoryEntryToData(ImageBase, TRUE, IMAGE_DIRECTORY_ENTRY_EXPORT, &size);
}

/* Both firmware table requests go through SystemFirmwareTableInformation, whose answer follows a fixed header */
_IRQL_requires_max_(PASSIVE_LEVEL)
CODE_SEG("PAGE")
static
NTSTATUS
AuxKlibQueryFirmwareTable(
    _In_ SYSTEM_FIRMWARE_TABLE_ACTION Action,
    _In_ ULONG FirmwareTableProviderSignature,
    _In_ ULONG FirmwareTableID,
    _Out_writes_bytes_to_opt_(BufferLength, *ReturnLength) PVOID FirmwareTableBuffer,
    _In_ ULONG BufferLength,
    _Out_opt_ PULONG ReturnLength)
{
    const ULONG Header = FIELD_OFFSET(SYSTEM_FIRMWARE_TABLE_INFORMATION, TableBuffer);
    PSYSTEM_FIRMWARE_TABLE_INFORMATION Request;
    ULONG Length, Returned;
    NTSTATUS Status;

    PAGED_CODE();

    if (gKlibInitialized != 1)
        return STATUS_UNSUCCESSFUL;

    if ((FirmwareTableBuffer == NULL) && (BufferLength != 0))
        return STATUS_INVALID_PARAMETER;

    if (!NT_SUCCESS(RtlULongAdd(Header, BufferLength, &Length)))
        return STATUS_INVALID_BUFFER_SIZE;

    Request = ExAllocatePoolZero(PagedPool, Length, TAG_AUXK);
    if (Request == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    Request->ProviderSignature = FirmwareTableProviderSignature;
    Request->Action = Action;
    Request->TableID = FirmwareTableID;
    Request->TableBufferLength = BufferLength;

    Status = ZwQuerySystemInformation(SystemFirmwareTableInformation, Request, Length, &Returned);
    if (NT_SUCCESS(Status) || (Status == STATUS_BUFFER_TOO_SMALL))
    {
        if (ReturnLength != NULL)
            *ReturnLength = Request->TableBufferLength;

        if (NT_SUCCESS(Status))
            RtlCopyMemory(FirmwareTableBuffer, Request->TableBuffer, min(Request->TableBufferLength, BufferLength));
    }

    ExFreePoolWithTag(Request, TAG_AUXK);
    return Status;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
CODE_SEG("PAGE")
NTSTATUS
NTAPI
AuxKlibEnumerateSystemFirmwareTables (
    _In_ ULONG FirmwareTableProviderSignature,
    _Out_writes_bytes_to_opt_(BufferLength, *ReturnLength) PVOID FirmwareTableBuffer,
    _In_ ULONG BufferLength,
    _Out_opt_ PULONG ReturnLength)
{
    PAGED_CODE();

    return AuxKlibQueryFirmwareTable(SystemFirmwareTable_Enumerate,
                                     FirmwareTableProviderSignature,
                                     0,
                                     FirmwareTableBuffer,
                                     BufferLength,
                                     ReturnLength);
}

_IRQL_requires_max_(PASSIVE_LEVEL)
CODE_SEG("PAGE")
NTSTATUS
NTAPI
AuxKlibGetSystemFirmwareTable (
    _In_ ULONG FirmwareTableProviderSignature,
    _In_ ULONG FirmwareTableID,
    _Out_writes_bytes_to_opt_(BufferLength, *ReturnLength) PVOID FirmwareTableBuffer,
    _In_ ULONG BufferLength,
    _Out_opt_ PULONG ReturnLength)
{
    PAGED_CODE();

    return AuxKlibQueryFirmwareTable(SystemFirmwareTable_Get,
                                     FirmwareTableProviderSignature,
                                     FirmwareTableID,
                                     FirmwareTableBuffer,
                                     BufferLength,
                                     ReturnLength);
}
