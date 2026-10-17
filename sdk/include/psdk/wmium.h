/*
 * PROJECT:     LiberNT SDK
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     User-mode WMI data block interface
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef _WMIUM_
#define _WMIUM_

#include <wmistr.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef WMIAPI
#ifdef _WMI_SOURCE_
#define WMIAPI __stdcall
#else
#define WMIAPI DECLSPEC_IMPORT __stdcall
#endif
#endif

typedef PVOID WMIHANDLE, *PWMIHANDLE, MOFHANDLE, *PMOFHANDLE;

typedef struct _MOFRESOURCEINFOA
{
    LPSTR ImagePath;
    LPSTR ResourceName;
    ULONG ResourceSize;
    PUCHAR ResourceBuffer;
} MOFRESOURCEINFOA, *PMOFRESOURCEINFOA;

typedef struct _MOFRESOURCEINFOW
{
    LPWSTR ImagePath;
    LPWSTR ResourceName;
    ULONG ResourceSize;
    PUCHAR ResourceBuffer;
} MOFRESOURCEINFOW, *PMOFRESOURCEINFOW;

ULONG WMIAPI WmiOpenBlock(_In_ GUID *Guid, _In_ ULONG DesiredAccess, _Out_ WMIHANDLE *DataBlockHandle);
ULONG WMIAPI WmiCloseBlock(_In_ WMIHANDLE DataBlockHandle);

ULONG WMIAPI WmiQueryAllDataA(_In_ WMIHANDLE DataBlockHandle, _Inout_ ULONG *BufferSize, _Out_writes_bytes_opt_(*BufferSize) LPVOID Buffer);
ULONG WMIAPI WmiQueryAllDataW(_In_ WMIHANDLE DataBlockHandle, _Inout_ ULONG *BufferSize, _Out_writes_bytes_opt_(*BufferSize) LPVOID Buffer);

ULONG WMIAPI WmiQuerySingleInstanceA(_In_ WMIHANDLE DataBlockHandle, _In_ LPCSTR InstanceName, _Inout_ PULONG BufferSize, _Out_writes_bytes_to_opt_(*BufferSize, *BufferSize) LPVOID Buffer);
ULONG WMIAPI WmiQuerySingleInstanceW(_In_ WMIHANDLE DataBlockHandle, _In_ LPCWSTR InstanceName, _Inout_ PULONG BufferSize, _Out_writes_bytes_to_opt_(*BufferSize, *BufferSize) LPVOID Buffer);

ULONG WMIAPI WmiSetSingleInstanceA(_In_ WMIHANDLE DataBlockHandle, _In_ LPCSTR InstanceName, _In_ ULONG Reserved, _In_ ULONG ValueBufferSize, _In_reads_bytes_(ValueBufferSize) PVOID ValueBuffer);
ULONG WMIAPI WmiSetSingleInstanceW(_In_ WMIHANDLE DataBlockHandle, _In_ LPCWSTR InstanceName, _In_ ULONG Reserved, _In_ ULONG ValueBufferSize, _In_reads_bytes_(ValueBufferSize) PVOID ValueBuffer);

ULONG WMIAPI WmiSetSingleItemA(_In_ WMIHANDLE DataBlockHandle, _In_ LPCSTR InstanceName, _In_ ULONG DataItemId, _In_ ULONG Reserved, _In_ ULONG ValueBufferSize, _In_reads_bytes_(ValueBufferSize) PVOID ValueBuffer);
ULONG WMIAPI WmiSetSingleItemW(_In_ WMIHANDLE DataBlockHandle, _In_ LPCWSTR InstanceName, _In_ ULONG DataItemId, _In_ ULONG Reserved, _In_ ULONG ValueBufferSize, _In_reads_bytes_(ValueBufferSize) PVOID ValueBuffer);

ULONG WMIAPI WmiExecuteMethodA(_In_ WMIHANDLE MethodDataBlockHandle, _In_ LPCSTR MethodInstanceName, _In_ ULONG MethodId, _In_ ULONG InputValueBufferSize, _In_reads_bytes_opt_(InputValueBufferSize) PVOID InputValueBuffer, _Inout_opt_ ULONG *OutputBufferSize, _Out_writes_bytes_opt_(*OutputBufferSize) PVOID OutputBuffer);
ULONG WMIAPI WmiExecuteMethodW(_In_ WMIHANDLE MethodDataBlockHandle, _In_ LPCWSTR MethodInstanceName, _In_ ULONG MethodId, _In_ ULONG InputValueBufferSize, _In_reads_bytes_opt_(InputValueBufferSize) PVOID InputValueBuffer, _Inout_opt_ ULONG *OutputBufferSize, _Out_writes_bytes_opt_(*OutputBufferSize) PVOID OutputBuffer);

#ifdef UNICODE
#define WmiQueryAllData WmiQueryAllDataW
#define WmiQuerySingleInstance WmiQuerySingleInstanceW
#define WmiSetSingleInstance WmiSetSingleInstanceW
#define WmiSetSingleItem WmiSetSingleItemW
#define WmiExecuteMethod WmiExecuteMethodW
#else
#define WmiQueryAllData WmiQueryAllDataA
#define WmiQuerySingleInstance WmiQuerySingleInstanceA
#define WmiSetSingleInstance WmiSetSingleInstanceA
#define WmiSetSingleItem WmiSetSingleItemA
#define WmiExecuteMethod WmiExecuteMethodA
#endif

#ifdef __cplusplus
}
#endif

#endif
