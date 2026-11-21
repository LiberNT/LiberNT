/*
 * PROJECT:     LiberNT TPM Base Services
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     TBS contexts and command submission over the TPM 2.0 driver
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <windef.h>
#include <winbase.h>
#include <winreg.h>
#include <winerror.h>
#include <winioctl.h>
#include <cfgmgr32.h>
#include <initguid.h>
#include <libernt/drivers/tpm2.h>
#include <tbs.h>

#define NDEBUG
#include <debug.h>

#define TBSP_TPM_HEADER_SIZE 10

/* FIXME: contexts share the TPM's transient objects and sessions; Windows virtualizes them per context */
typedef struct _TBSP_CONTEXT
{
    struct _TBSP_CONTEXT *Next;
    LONG References;
    BOOL Closed;
    HANDLE Device;
    UINT32 MaximumCommand;
    UINT32 MaximumResponse;
} TBSP_CONTEXT, *PTBSP_CONTEXT;

static SRWLOCK TbspLock = SRWLOCK_INIT;
static PTBSP_CONTEXT TbspContexts;

static
HANDLE
TbspOpenTpm(
    _Out_ PTPM2_DEVICE_INFORMATION Information)
{
    ULONG Length = 0;
    PWSTR List;
    HANDLE Device = INVALID_HANDLE_VALUE;
    DWORD Returned;

    ZeroMemory(Information, sizeof(*Information));
    if (CM_Get_Device_Interface_List_SizeW(&Length, (LPGUID)&GUID_DEVINTERFACE_REACTOS_TPM2, NULL, CM_GET_DEVICE_INTERFACE_LIST_PRESENT) != CR_SUCCESS || Length <= 1)
        return INVALID_HANDLE_VALUE;
    List = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Length * sizeof(WCHAR));
    if (List == NULL)
        return INVALID_HANDLE_VALUE;
    if (CM_Get_Device_Interface_ListW((LPGUID)&GUID_DEVINTERFACE_REACTOS_TPM2, NULL, List, Length, CM_GET_DEVICE_INTERFACE_LIST_PRESENT) == CR_SUCCESS && List[0] != UNICODE_NULL)
        Device = CreateFileW(List, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    HeapFree(GetProcessHeap(), 0, List);
    if (Device == INVALID_HANDLE_VALUE)
        return INVALID_HANDLE_VALUE;
    if (!DeviceIoControl(Device, IOCTL_TPM2_QUERY_INFORMATION, NULL, 0, Information, sizeof(*Information), &Returned, NULL) ||
        Returned < sizeof(*Information))
    {
        CloseHandle(Device);
        return INVALID_HANDLE_VALUE;
    }
    return Device;
}

static
VOID
TbspDereference(
    _In_ PTBSP_CONTEXT Context)
{
    LONG References = InterlockedDecrement(&Context->References);

    ASSERT(References >= 0);
    if (References == 0)
    {
        ASSERT(Context->Closed);
        CloseHandle(Context->Device);
        HeapFree(GetProcessHeap(), 0, Context);
    }
}

static
PTBSP_CONTEXT
TbspReference(
    _In_opt_ TBS_HCONTEXT Handle)
{
    PTBSP_CONTEXT Context;

    AcquireSRWLockShared(&TbspLock);
    for (Context = TbspContexts; Context != NULL; Context = Context->Next)
    {
        if (Context == (PTBSP_CONTEXT)Handle)
        {
            LONG References = InterlockedIncrement(&Context->References);

            ASSERT(References > 1);
            break;
        }
    }
    ReleaseSRWLockShared(&TbspLock);
    return Context;
}

static
BOOL
TbspPriorityValid(
    _In_ TBS_COMMAND_PRIORITY Priority)
{
    return Priority == TBS_COMMAND_PRIORITY_LOW ||
           Priority == TBS_COMMAND_PRIORITY_NORMAL ||
           Priority == TBS_COMMAND_PRIORITY_HIGH ||
           Priority == TBS_COMMAND_PRIORITY_SYSTEM ||
           Priority == TBS_COMMAND_PRIORITY_MAX;
}

static
TBS_RESULT
TbspTransmit(
    _In_ PTBSP_CONTEXT Context,
    _In_reads_bytes_(CommandSize) const BYTE *Command,
    _In_ UINT32 CommandSize,
    _Out_writes_bytes_(*ResultSize) PBYTE Result,
    _Inout_ PUINT32 ResultSize)
{
    UINT32 BufferSize = max(CommandSize, Context->MaximumResponse);
    PBYTE Buffer;
    DWORD Returned = 0;
    TBS_RESULT Status = TBS_SUCCESS;

    Buffer = HeapAlloc(GetProcessHeap(), 0, BufferSize);
    if (Buffer == NULL)
        return TBS_E_INTERNAL_ERROR;
    CopyMemory(Buffer, Command, CommandSize);
    if (!DeviceIoControl(Context->Device, IOCTL_TPM2_SUBMIT_COMMAND, Buffer, CommandSize, Buffer, Context->MaximumResponse, &Returned, NULL))
    {
        DWORD Error = GetLastError();

        if (Error == ERROR_OPERATION_ABORTED || Error == ERROR_CANCELLED)
            Status = TBS_E_COMMAND_CANCELED;
        else if (Error == ERROR_INVALID_PARAMETER || Error == ERROR_INVALID_USER_BUFFER)
            Status = TBS_E_BAD_PARAMETER;
        else
            Status = TBS_E_IOERROR;
    }
    else if (Returned < TBSP_TPM_HEADER_SIZE)
    {
        Status = TBS_E_IOERROR;
    }
    else if (Returned > *ResultSize)
    {
        *ResultSize = Returned;
        Status = TBS_E_INSUFFICIENT_BUFFER;
    }
    else
    {
        CopyMemory(Result, Buffer, Returned);
        *ResultSize = Returned;
    }
    HeapFree(GetProcessHeap(), 0, Buffer);
    return Status;
}

TBS_RESULT
WINAPI
Tbsi_Context_Create(
    _In_ PCTBS_CONTEXT_PARAMS pContextParams,
    _Out_ PTBS_HCONTEXT phContext)
{
    TPM2_DEVICE_INFORMATION Information;
    PTBSP_CONTEXT Context;
    HANDLE Device;

    if (pContextParams == NULL)
        return TBS_E_BAD_PARAMETER;
    if (phContext == NULL)
        return TBS_E_INVALID_OUTPUT_POINTER;
    if (pContextParams->version == TBS_CONTEXT_VERSION_ONE)
        return TBS_E_TPM_NOT_FOUND;
    if (pContextParams->version != TBS_CONTEXT_VERSION_TWO)
        return TBS_E_INVALID_CONTEXT_PARAM;
    if (!((PCTBS_CONTEXT_PARAMS2)pContextParams)->includeTpm20)
        return TBS_E_TPM_NOT_FOUND;

    Device = TbspOpenTpm(&Information);
    if (Device == INVALID_HANDLE_VALUE)
        return TBS_E_TPM_NOT_FOUND;
    Context = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Context));
    if (Context == NULL)
    {
        CloseHandle(Device);
        return TBS_E_INTERNAL_ERROR;
    }
    Context->References = 1;
    Context->Device = Device;
    Context->MaximumCommand = min(Information.MaximumCommandSize, TBS_IN_OUT_BUF_SIZE_MAX);
    Context->MaximumResponse = min(Information.MaximumResponseSize, TBS_IN_OUT_BUF_SIZE_MAX);
    ASSERT(Context->MaximumResponse >= TBSP_TPM_HEADER_SIZE);

    AcquireSRWLockExclusive(&TbspLock);
    Context->Next = TbspContexts;
    TbspContexts = Context;
    ReleaseSRWLockExclusive(&TbspLock);
    *phContext = Context;
    return TBS_SUCCESS;
}

TBS_RESULT
WINAPI
Tbsip_Context_Close(
    _In_ TBS_HCONTEXT hContext)
{
    PTBSP_CONTEXT *Link;
    PTBSP_CONTEXT Context = NULL;

    AcquireSRWLockExclusive(&TbspLock);
    for (Link = &TbspContexts; *Link != NULL; Link = &(*Link)->Next)
    {
        if (*Link == (PTBSP_CONTEXT)hContext)
        {
            Context = *Link;
            *Link = Context->Next;
            ASSERT(!Context->Closed);
            Context->Closed = TRUE;
            break;
        }
    }
    ReleaseSRWLockExclusive(&TbspLock);
    if (Context == NULL)
        return TBS_E_INVALID_CONTEXT;
    TbspDereference(Context);
    return TBS_SUCCESS;
}

TBS_RESULT
WINAPI
Tbsip_Submit_Command(
    _In_ TBS_HCONTEXT hContext,
    _In_ TBS_COMMAND_LOCALITY Locality,
    _In_ TBS_COMMAND_PRIORITY Priority,
    _In_reads_bytes_(cbCommand) const BYTE *pabCommand,
    _In_ UINT32 cbCommand,
    _Out_writes_bytes_(*pcbResult) PBYTE pabResult,
    _Inout_ PUINT32 pcbResult)
{
    PTBSP_CONTEXT Context = TbspReference(hContext);
    TBS_RESULT Status;

    if (Context == NULL)
        return TBS_E_INVALID_CONTEXT;
    if (Locality != TBS_COMMAND_LOCALITY_ZERO || !TbspPriorityValid(Priority) ||
        pabCommand == NULL || cbCommand < TBSP_TPM_HEADER_SIZE)
    {
        Status = TBS_E_BAD_PARAMETER;
    }
    else if (cbCommand > Context->MaximumCommand)
    {
        Status = TBS_E_BUFFER_TOO_LARGE;
    }
    else if (pabResult == NULL || pcbResult == NULL)
    {
        Status = TBS_E_INVALID_OUTPUT_POINTER;
    }
    else
    {
        Status = TbspTransmit(Context, pabCommand, cbCommand, pabResult, pcbResult);
    }
    TbspDereference(Context);
    return Status;
}

TBS_RESULT
WINAPI
Tbsip_Cancel_Commands(
    _In_ TBS_HCONTEXT hContext)
{
    PTBSP_CONTEXT Context = TbspReference(hContext);
    DWORD Returned;
    TBS_RESULT Status = TBS_SUCCESS;

    if (Context == NULL)
        return TBS_E_INVALID_CONTEXT;
    if (!DeviceIoControl(Context->Device, IOCTL_TPM2_CANCEL_COMMAND, NULL, 0, NULL, 0, &Returned, NULL))
        Status = TBS_E_IOERROR;
    TbspDereference(Context);
    return Status;
}

TBS_RESULT
WINAPI
Tbsi_GetDeviceInfo(
    _In_ UINT32 Size,
    _Out_writes_bytes_(Size) PVOID Info)
{
    TPM2_DEVICE_INFORMATION Information;
    PTPM_DEVICE_INFO DeviceInfo = Info;
    HANDLE Device;

    if (Info == NULL || Size < sizeof(TPM_DEVICE_INFO))
        return TBS_E_BAD_PARAMETER;
    Device = TbspOpenTpm(&Information);
    if (Device == INVALID_HANDLE_VALUE)
        return TBS_E_TPM_NOT_FOUND;
    CloseHandle(Device);
    DeviceInfo->structVersion = 1;
    DeviceInfo->tpmVersion = TPM_VERSION_20;
    DeviceInfo->tpmInterfaceType = TPM_IFTYPE_HW;
    /* TODO: report the TPM implementation revision */
    DeviceInfo->tpmImpRevision = 0;
    return TBS_SUCCESS;
}

TBS_RESULT
WINAPI
Tbsi_Get_TCG_Log(
    _In_ TBS_HCONTEXT hContext,
    _Out_writes_bytes_to_opt_(*pOutputBufLen, *pOutputBufLen) PBYTE pOutputBuf,
    _Inout_ PUINT32 pOutputBufLen)
{
    PTBSP_CONTEXT Context = TbspReference(hContext);

    UNREFERENCED_PARAMETER(pOutputBuf);
    if (Context == NULL)
        return TBS_E_INVALID_CONTEXT;
    TbspDereference(Context);
    if (pOutputBufLen == NULL)
        return TBS_E_BAD_PARAMETER;
    /* TODO: return the measured boot event log once the boot path records one */
    return TBS_E_NO_EVENT_LOG;
}

TBS_RESULT
WINAPI
Tbsi_Get_TCG_Log_Ex(
    _In_ UINT32 logType,
    _Out_writes_bytes_opt_(*pcbOutput) PBYTE pbOutput,
    _Inout_ PUINT32 pcbOutput)
{
    UNREFERENCED_PARAMETER(pbOutput);
    if (pcbOutput == NULL || logType > TBS_TCGLOG_DRTM_RESUME)
        return TBS_E_BAD_PARAMETER;
    /* TODO: return the measured boot event log once the boot path records one */
    return TBS_E_NO_EVENT_LOG;
}

TBS_RESULT
WINAPI
Tbsi_Get_OwnerAuth(
    _In_ TBS_HCONTEXT hContext,
    _In_ TBS_OWNERAUTH_TYPE ownerauthType,
    _Out_writes_bytes_to_opt_(*pOutputBufLen, *pOutputBufLen) PBYTE pOutputBuf,
    _Inout_ PUINT32 pOutputBufLen)
{
    PTBSP_CONTEXT Context = TbspReference(hContext);

    UNREFERENCED_PARAMETER(ownerauthType);
    UNREFERENCED_PARAMETER(pOutputBuf);
    if (Context == NULL)
        return TBS_E_INVALID_CONTEXT;
    TbspDereference(Context);
    if (pOutputBufLen == NULL)
        return TBS_E_BAD_PARAMETER;
    return TBS_E_OWNERAUTH_NOT_FOUND;
}

TBS_RESULT
WINAPI
Tbsi_Physical_Presence_Command(
    _In_ TBS_HCONTEXT hContext,
    _In_reads_bytes_(cbInput) const BYTE *pabInput,
    _In_ UINT32 cbInput,
    _Out_writes_bytes_(*pcbOutput) PBYTE pabOutput,
    _Out_ PUINT32 pcbOutput)
{
    PTBSP_CONTEXT Context = TbspReference(hContext);

    UNREFERENCED_PARAMETER(pabInput);
    UNREFERENCED_PARAMETER(cbInput);
    UNREFERENCED_PARAMETER(pabOutput);
    UNREFERENCED_PARAMETER(pcbOutput);
    if (Context == NULL)
        return TBS_E_INVALID_CONTEXT;
    TbspDereference(Context);
    /* TODO: forward physical presence requests to the platform's ACPI interface */
    return TBS_E_PPI_NOT_SUPPORTED;
}

HRESULT
WINAPI
GetDeviceID(
    _Out_writes_bytes_to_opt_(cbWindowsAIK, *pcbResult) PBYTE pbWindowsAIK,
    _In_ UINT32 cbWindowsAIK,
    _Out_ PUINT32 pcbResult,
    _Out_opt_ BOOL *pfProtectedByTPM)
{
    UNREFERENCED_PARAMETER(pbWindowsAIK);
    UNREFERENCED_PARAMETER(cbWindowsAIK);
    UNREFERENCED_PARAMETER(pcbResult);
    UNREFERENCED_PARAMETER(pfProtectedByTPM);
    /* TODO: derive the device identity from the Windows AIK */
    return E_NOTIMPL;
}

HRESULT
WINAPI
GetDeviceIDString(
    _Out_writes_to_opt_(cchWindowsAIK, *pcchResult) PWSTR pszWindowsAIK,
    _In_ UINT32 cchWindowsAIK,
    _Out_ PUINT32 pcchResult,
    _Out_opt_ BOOL *pfProtectedByTPM)
{
    UNREFERENCED_PARAMETER(pszWindowsAIK);
    UNREFERENCED_PARAMETER(cchWindowsAIK);
    UNREFERENCED_PARAMETER(pcchResult);
    UNREFERENCED_PARAMETER(pfProtectedByTPM);
    /* TODO: derive the device identity from the Windows AIK */
    return E_NOTIMPL;
}
