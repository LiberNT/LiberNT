/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

typedef struct _LDRP_SCP_CFG_HEADER
{
    ULONG CfgDispatchRva;
    ULONG CfgDispatchESRva;
    ULONG CfgCheckRva;
    ULONG CfgCheckESRva;
    ULONG InvalidCallHandlerRva;
    ULONG FnTableRva;
} LDRP_SCP_CFG_HEADER;
