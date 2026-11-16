/*
 * PROJECT:     LiberNT Device Manager
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Device status text of the property sheet and the hardware page
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include "properties.h"
#include "resource.h"
#include <strsafe.h>

static BOOL
LoadStatusResource(
    _In_ UINT Id,
    _Out_writes_(Length) LPWSTR Text,
    _In_ DWORD Length)
{
    return LoadStringW(hDllInstance, Id, Text, (int)Length) > 0;
}

static BOOL
LoadProblemStatus(
    _In_opt_ HMACHINE Machine,
    _In_ DEVINST Node,
    _In_ ULONG Problem,
    _Out_writes_(Length) LPWSTR Text,
    _In_ DWORD Length)
{
    UINT Written = DeviceProblemTextW(Machine, Node, Problem, Text, Length);

    return Written != 0 && Written < Length;
}

static BOOL
LoadNotPresentStatus(
    _In_opt_ HMACHINE Machine,
    _In_ DEVINST Node,
    _Out_writes_(Length) LPWSTR Text,
    _In_ DWORD Length)
{
    WCHAR Resolution[256];

    if (!LoadProblemStatus(Machine, Node, CM_PROB_PHANTOM, Text, Length))
        return FALSE;

    if (LoadStringW(hDllInstance, IDS_DEV_PHANTOM_RESOLUTION, Resolution, ARRAYSIZE(Resolution)) > 0)
    {
        StringCchCatW(Text, Length, L"\r\n\r\n");
        StringCchCatW(Text, Length, Resolution);
    }
    return TRUE;
}

BOOL
GetDeviceStatusString(IN DEVINST DevInst,
                      IN HMACHINE hMachine,
                      OUT LPWSTR szBuffer,
                      IN DWORD BufferSize)
{
    ULONG Status = 0;
    ULONG Problem = 0;
    CONFIGRET Result;

    if (szBuffer == NULL || BufferSize == 0)
        return FALSE;
    szBuffer[0] = UNICODE_NULL;

    Result = CM_Get_DevNode_Status_Ex(&Status, &Problem, DevInst, 0, hMachine);
    switch (Result)
    {
        case CR_SUCCESS:
            if (Status & DN_HAS_PROBLEM)
                return LoadProblemStatus(hMachine, DevInst, Problem, szBuffer, BufferSize);
            if (Status & (DN_DRIVER_LOADED | DN_STARTED))
                return LoadStatusResource(IDS_DEV_NO_PROBLEM, szBuffer, BufferSize);
            return LoadStatusResource(IDS_NODRIVERLOADED, szBuffer, BufferSize);

        case CR_NO_SUCH_DEVINST:
            return LoadNotPresentStatus(hMachine, DevInst, szBuffer, BufferSize);

        default:
            return LoadStatusResource(IDS_UNKNOWN, szBuffer, BufferSize);
    }
}
