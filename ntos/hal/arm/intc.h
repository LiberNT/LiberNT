/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ARMv7 interrupt controller backends of the in-kernel HAL
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

/* Vectors are controller numbers; vector KI_ARM_IPI_SGI always carries interprocessor requests. */
#define KI_ARM_MAX_VECTORS 1020

typedef struct _KI_ARM_INTERRUPT_CONTROLLER
{
    /* Boot processor, once device mappings exist: FALSE when the device tree describes no such controller. */
    BOOLEAN (NTAPI *Initialize)(VOID);
    /* State private to the current processor, including its interprocessor request source. */
    VOID (NTAPI *InitializeProcessor)(_In_ ULONG Number);
    /* Admit only the sources whose IRQL is above Irql; Irql is DISPATCH_LEVEL or higher. */
    VOID (NTAPI *SetPriorityMask)(_In_ KIRQL Irql);
    BOOLEAN (NTAPI *EnableLine)(_In_ ULONG Vector, _In_ KIRQL Irql, _In_ KINTERRUPT_MODE Mode);
    VOID (NTAPI *DisableLine)(_In_ ULONG Vector);
    /* Take the highest pending source of the current processor; FALSE when none is pending. */
    BOOLEAN (NTAPI *Acknowledge)(_Out_ PULONG Vector, _Out_ PULONG Token);
    VOID (NTAPI *EndOfInterrupt)(_In_ ULONG Token);
    VOID (NTAPI *SendInterprocessorInterrupt)(_In_ KAFFINITY TargetProcessors);
    ULONG (NTAPI *TimerVector)(VOID);
} KI_ARM_INTERRUPT_CONTROLLER, *PKI_ARM_INTERRUPT_CONTROLLER;

extern const KI_ARM_INTERRUPT_CONTROLLER KiArmGicV2Controller;
extern const KI_ARM_INTERRUPT_CONTROLLER KiArmBcm2836Controller;

PVOID NTAPI KiArmMapDevice(_In_ PHYSICAL_ADDRESS Address, _In_ ULONG Length);
BOOLEAN NTAPI KiArmFindTimerPpi(_Out_ PULONG Vector);
