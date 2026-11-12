/*
 * PROJECT:     LiberNT NetIO driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     WSK kernel name resolution, internal declarations
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <drivers/netio/wsknameio.h>

#define TAG_WSKNAME 'nKsW'

NTSTATUS
WskNameCreateDevice(
    _In_ PDRIVER_OBJECT DriverObject);

NTSTATUS
WskNameForward(
    _In_ const WSKNAME_REQUEST *Request,
    _Out_ PWSKNAME_REPLY Reply);

NTSTATUS
WSKAPI
WskGetAddressInfo(
    _In_ PWSK_CLIENT Client,
    _In_opt_ PUNICODE_STRING NodeName,
    _In_opt_ PUNICODE_STRING ServiceName,
    _In_opt_ ULONG NameSpace,
    _In_opt_ GUID *Provider,
    _In_opt_ PADDRINFOEXW Hints,
    _Outptr_ PADDRINFOEXW *Result,
    _In_opt_ PEPROCESS OwningProcess,
    _In_opt_ PETHREAD OwningThread,
    _Inout_ PIRP Irp);

VOID
WSKAPI
WskFreeAddressInfo(
    _In_ PWSK_CLIENT Client,
    _In_ PADDRINFOEXW AddrInfo);

NTSTATUS
WSKAPI
WskGetNameInfo(
    _In_ PWSK_CLIENT Client,
    _In_ PSOCKADDR SockAddr,
    _In_ ULONG SockAddrLength,
    _Out_opt_ PUNICODE_STRING NodeName,
    _Out_opt_ PUNICODE_STRING ServiceName,
    _In_ ULONG Flags,
    _In_opt_ PEPROCESS OwningProcess,
    _In_opt_ PETHREAD OwningThread,
    _Inout_ PIRP Irp);
