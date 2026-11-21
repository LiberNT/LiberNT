/*
 * PROJECT:     LiberNT Apple RTKit Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     PMGR power-state register layout shared by the power helpers
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <libernt/drivers/applepmgr.h>

#define PMGR_RESET              0x80000000
#define PMGR_AUTO_ENABLE        0x10000000
#define PMGR_PS_RESET           0x00001000
#define PMGR_PS_MIN             0x000f0000
#define PMGR_PS_MIN_SHIFT       16
#define PMGR_DEV_DISABLE        0x00000400
#define PMGR_FLAGS              0x00000300
#define PMGR_PS_ACTUAL(Value)   (((Value) >> 4) & 0xf)
#define PMGR_PS_TARGET          0x0000000f
#define PMGR_PS_ACTIVE          0xf
#define PMGR_PS_CLKGATE         0x4
#define PMGR_PS_PWRGATE         0x0
#define PMGR_PS_TIMEOUT_US      100
#define PMGR_MAX_DEPTH          8

typedef struct _PMGR_DOMAIN
{
    volatile ULONG *Register;
    ULONG64 Physical;
    PCSTR Label;
    PCFDT_DRIVER_IO Io;
} PMGR_DOMAIN, *PPMGR_DOMAIN;

BOOLEAN
ApplePmgrMapDomain(
    _In_ PFDT_DRIVER_TREE Tree,
    _In_ ULONG Domain,
    _Out_ PPMGR_DOMAIN Mapping);

VOID
ApplePmgrUnmapDomain(
    _Inout_ PPMGR_DOMAIN Mapping);
