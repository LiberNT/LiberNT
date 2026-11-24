/*
 * PROJECT:     LiberNT qWAVE
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Private declarations of qwave.dll
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

typedef VOID (*QWAVE_REPORT_STATUS)(DWORD State, DWORD ExitCode);

BOOL QwaveToAddress(_In_ const SOCKADDR *Address, _Out_ PQWAVE_ADDRESS Out);
VOID QwaveReportServiceStatus(_In_ DWORD State, _In_ DWORD ExitCode);
DWORD QwaveStartController(_In_ QWAVE_REPORT_STATUS Report, _Out_ PHANDLE Controller);
VOID QwaveStopController(_In_opt_ HANDLE Controller);
