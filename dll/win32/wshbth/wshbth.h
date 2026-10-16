/*
 * PROJECT:     LiberNT Windows Sockets Helper for Bluetooth
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Shared definitions
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <winsock2.h>
#include <ws2spi.h>
#include <wsahelp.h>
#include <winioctl.h>
#include <bthdef.h>
#include <bthsdpdef.h>
#include <ws2bth.h>
#include <bluetoothapis.h>
#include <bthioctl.h>
#include <reactos/tdibth.h>

#define EXPORT WINAPI

BOOL
WshBthFormatAddress(
    _In_ BTH_ADDR Address,
    _In_ ULONG Port,
    _Out_writes_(Length) LPWSTR String,
    _In_ DWORD Length);

BOOL
WshBthParseAddress(
    _In_ LPCWSTR String,
    _Out_ BTH_ADDR *Address,
    _Out_ PULONG Port);

VOID
WshBthNspInitialize(VOID);

HANDLE
WshBthOpenRadio(
    _Out_opt_ BTH_ADDR *Address);
