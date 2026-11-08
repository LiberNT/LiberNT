/*
 * PROJECT:     LiberNT NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/arch/amd64/tlb.h
 * PURPOSE:     AMD64 translation lookaside buffer interfaces
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once

#define MI_AMD64_TLB_INVPCID 0x02
#define MI_AMD64_TLB_RANGE_LIMIT 64
#define MI_AMD64_TLB_USER_RANGE_LIMIT 31
#define MI_AMD64_TLB_NON_GLOBAL ((ULONG64)-1)
#define MI_AMD64_USER_END 0x0000800000000000ULL

ULONG MiAmd64TlbCapabilities(VOID);
VOID MiAmd64FlushTargets(ULONG64 Targets, ULONG64 Address, ULONG64 Pages);
VOID MiAmd64ProcessTlbRequest(ULONG Source);
VOID KiSendMemoryIpi(ULONG64 Targets);
