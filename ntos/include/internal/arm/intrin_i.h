/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ARMv7 kernel intrinsics
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <intrin.h>

#define KiArmReadCp15(Crn, Op1, Crm, Op2) ({ \
    ULONG Value_; \
    __asm__ __volatile__("mrc p15, " #Op1 ", %0, c" #Crn ", c" #Crm ", " #Op2 : "=r"(Value_) :: "memory"); \
    Value_; })

#define KiArmWriteCp15(Crn, Op1, Crm, Op2, Value) \
    __asm__ __volatile__("mcr p15, " #Op1 ", %0, c" #Crn ", c" #Crm ", " #Op2 :: "r"((ULONG)(Value)) : "memory")

#define KiArmReadCp15_64(Op1, Crm) ({ \
    ULONG Low_, High_; \
    __asm__ __volatile__("mrrc p15, " #Op1 ", %0, %1, c" #Crm : "=r"(Low_), "=r"(High_) :: "memory"); \
    ((ULONGLONG)High_ << 32) | Low_; })

#define KiArmWriteCp15_64(Op1, Crm, Value) \
    __asm__ __volatile__("mcrr p15, " #Op1 ", %0, %1, c" #Crm \
                         :: "r"((ULONG)(Value)), "r"((ULONG)((ULONGLONG)(Value) >> 32)) : "memory")

#define KiArmDsb() __asm__ __volatile__("dsb sy" ::: "memory")
#define KiArmDsbIsh() __asm__ __volatile__("dsb ish" ::: "memory")
#define KiArmDmb() __asm__ __volatile__("dmb sy" ::: "memory")
#define KiArmIsb() __asm__ __volatile__("isb sy" ::: "memory")
