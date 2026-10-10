/*
 * PROJECT:     LiberNT AMD64 emulation host
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Calls between the felix86 sources and the NT side of the CPU core
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FELIX86_NT_EXT_V           0x00000001
#define FELIX86_NT_EXT_ZBA         0x00000002
#define FELIX86_NT_EXT_ZBB         0x00000004
#define FELIX86_NT_EXT_ZBS         0x00000008
#define FELIX86_NT_EXT_ZBC         0x00000010
#define FELIX86_NT_EXT_ZICOND      0x00000020
#define FELIX86_NT_EXT_ZFA         0x00000040
#define FELIX86_NT_EXT_ZACAS       0x00000080
#define FELIX86_NT_EXT_ZABHA       0x00000100
#define FELIX86_NT_EXT_ZVBB        0x00000200
#define FELIX86_NT_EXT_ZVBC        0x00000400
#define FELIX86_NT_EXT_ZVKNED      0x00000800
#define FELIX86_NT_EXT_ZVKNHA      0x00001000
#define FELIX86_NT_EXT_ZVFHMIN     0x00002000
#define FELIX86_NT_EXT_ZKND        0x00004000
#define FELIX86_NT_EXT_ZICBOM      0x00008000
#define FELIX86_NT_EXT_ZICCLSM     0x00010000

#define FELIX86_NT_FAULT_UNRELATED 0
#define FELIX86_NT_FAULT_RESUME    1
#define FELIX86_NT_FAULT_GUEST     2

#define FELIX86_NT_NO_INTERRUPT    0xFFFFFFFF

typedef struct _FELIX86_NT_CPU FELIX86_NT_CPU;

typedef struct _FELIX86_NT_NATIVE
{
    uint64_t *Pc;
    uint64_t *X;
    uint64_t *F;
    uint32_t Code;
    uint32_t Parameters;
    uint64_t Information[4];
    int InService;
    int InModule;
} FELIX86_NT_NATIVE;

typedef struct _FELIX86_NT_GUEST
{
    uint32_t Code;
    uint32_t Parameters;
    uint64_t Information[4];
    uint64_t Address;
    uint32_t Interrupt;
} FELIX86_NT_GUEST;

int Felix86NtInitialize(void);
FELIX86_NT_CPU *Felix86NtCreateCpu(void *Owner, uint64_t Teb);
void Felix86NtDestroyCpu(FELIX86_NT_CPU *Cpu);
uint64_t *Felix86NtRegisters(FELIX86_NT_CPU *Cpu);
uint64_t Felix86NtGetFlags(FELIX86_NT_CPU *Cpu);
void Felix86NtSetFlags(FELIX86_NT_CPU *Cpu, uint64_t Flags);
void Felix86NtSaveFloating(FELIX86_NT_CPU *Cpu, void *Area);
void Felix86NtRestoreFloating(FELIX86_NT_CPU *Cpu, const void *Area);
void Felix86NtRun(FELIX86_NT_CPU *Cpu) __attribute__((noreturn));
void Felix86NtInvalidate(uint64_t Start, uint64_t End);
int Felix86NtFault(FELIX86_NT_CPU *Cpu, FELIX86_NT_NATIVE *Native, FELIX86_NT_GUEST *Guest);
void Felix86NtProcessor(uint16_t *Level, uint16_t *Revision);

void Felix86NtSystemService(void *Owner, uint64_t *Registers);
void Felix86NtPrint(const char *Text);
void Felix86NtTerminate(uint32_t Status) __attribute__((noreturn));
void Felix86NtYield(void);
uint32_t Felix86NtProbeExtensions(void);
uint32_t Felix86NtVectorBytes(void);
void Felix86NtStoreVectors(void *Buffer, uint32_t VectorBytes);
void Felix86NtRunConstructors(void);

#ifdef __cplusplus
}
#endif
