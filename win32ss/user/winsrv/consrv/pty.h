/*
 * PROJECT:     LiberNT Console Server DLL
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Pseudo console support
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

VOID
ConSrvPtyInitSupport(VOID);

VOID
ConSrvPtyWrapFrontEnd(
    _Inout_ PFRONTEND FrontEnd,
    _In_ PVOID PseudoConsole);

VOID
ConSrvPtyDisconnectProcess(
    _In_ PCSR_PROCESS Process);
