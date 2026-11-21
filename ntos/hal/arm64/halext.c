/*
 * PROJECT:     LiberNT HAL — ARM64 platform-extension manager
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Detects the SoC platform and holds the services registered
 *              by the matching platform extension (see halext.h for the
 *              contract).  This file is the only place in the generic HAL
 *              that knows which platforms exist; everything else consumes
 *              the registered interfaces blindly.
 *
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntifs.h>
#include <arc/arc.h>
#include <ndk/kefuncs.h>
#include <ioaccess.h>
#include <halfuncs.h>
#include "halext.h"
#include "gic_internal.h"

/* Platform probes — the one allowed list of platform knowledge. */
#include "bcm2711_pci.h"
#include "bcm2711_vc.h"
#include "bcm2712_pci.h"
#include "bcm2837.h"
#include "apple_aic.h"

#define NDEBUG
#include <debug.h>

/* ================================================================== */
/*  Platform table                                                    */
/* ================================================================== */

typedef struct _HAL_ARM64_PLATFORM
{
    PCSTR Name;
    BOOLEAN (*Probe)(_In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock);
} HAL_ARM64_PLATFORM;

static const HAL_ARM64_PLATFORM HalpArm64Platforms[] =
{
    { "BCM2711 (Raspberry Pi 4)", Bcm2711PciProbe },
    { "BCM2712 (Raspberry Pi 5)", Bcm2712PciProbe },
    { "BCM2837 (Raspberry Pi 3)", Bcm2837InterruptProbe },
    { "Apple silicon (AIC2/AIC3)", AppleAicInterruptProbe },
};

static BOOLEAN HalpArm64PlatformMatched = FALSE;

/* ================================================================== */
/*  Registered services                                               */
/* ================================================================== */

const HAL_ARM64_INTERRUPT_CONTROLLER *HalpArm64InterruptController = NULL;
const HAL_ARM64_PCI_CONFIG_BACKEND *HalpArm64PciConfigBackend = NULL;
const HAL_ARM64_SYSTEM_RESET *HalpArm64SystemReset = NULL;
const HAL_ARM64_MSI_DOORBELL *HalpArm64MsiDoorbell = NULL;
const HAL_ARM64_PROCESSOR_IDLE *HalpArm64ProcessorIdle = NULL;

/* ================================================================== */
/*  Registration                                                      */
/* ================================================================== */

NTSTATUS
HalpArm64RegisterInterruptController(
    _In_ const HAL_ARM64_INTERRUPT_CONTROLLER *Controller)
{
    if (!Controller)
        return STATUS_INVALID_PARAMETER;

    if (HalpArm64InterruptController)
        return STATUS_TOO_LATE;

    HalpArm64InterruptController = Controller;

    /*
     * The GIC layer must stop trusting its discovery defaults: on GIC-less
     * platforms the firmware MADT often carries compatibility decoys and
     * the GICD/GICC globals default to QEMU-virt physical addresses.
     */
    HalpGicAdoptExternalInterruptController();

    DPRINT1("[arm64][HALEXT] Interrupt controller: %s\n", Controller->Name);
    return STATUS_SUCCESS;
}

NTSTATUS
HalpArm64RegisterPciConfigBackend(
    _In_ const HAL_ARM64_PCI_CONFIG_BACKEND *Backend)
{
    if (!Backend)
        return STATUS_INVALID_PARAMETER;

    if (HalpArm64PciConfigBackend)
        return STATUS_TOO_LATE;

    HalpArm64PciConfigBackend = Backend;

    DPRINT1("[arm64][HALEXT] PCI config-space backend: %s\n", Backend->Name);
    return STATUS_SUCCESS;
}

NTSTATUS
HalpArm64RegisterSystemReset(
    _In_ const HAL_ARM64_SYSTEM_RESET *Reset)
{
    if (!Reset || !Reset->Restart)
        return STATUS_INVALID_PARAMETER;

    if (HalpArm64SystemReset)
        return STATUS_TOO_LATE;

    HalpArm64SystemReset = Reset;

    DPRINT1("[arm64][HALEXT] System reset: %s\n", Reset->Name);
    return STATUS_SUCCESS;
}

NTSTATUS
HalpArm64RegisterMsiDoorbell(
    _In_ const HAL_ARM64_MSI_DOORBELL *Doorbell)
{
    if (!Doorbell || !Doorbell->VectorCount)
        return STATUS_INVALID_PARAMETER;

    if (HalpArm64MsiDoorbell)
        return STATUS_TOO_LATE;

    HalpArm64MsiDoorbell = Doorbell;

    DPRINT1("[arm64][HALEXT] MSI doorbell: %s at 0x%I64x for vectors %lu+%lu\n", Doorbell->Name, Doorbell->Address,
            Doorbell->FirstVector, Doorbell->VectorCount);
    return STATUS_SUCCESS;
}

NTSTATUS
HalpArm64RegisterProcessorIdle(
    _In_ const HAL_ARM64_PROCESSOR_IDLE *Idle)
{
    if (!Idle || !Idle->Idle)
        return STATUS_INVALID_PARAMETER;

    if (HalpArm64ProcessorIdle)
        return STATUS_TOO_LATE;

    HalpArm64ProcessorIdle = Idle;

    DPRINT1("[arm64][HALEXT] Processor idle: %s\n", Idle->Name);
    return STATUS_SUCCESS;
}

/* ================================================================== */
/*  Generic HAL consumption                                           */
/* ================================================================== */

VOID
HalpArm64ProbePlatforms(
    _In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    ULONG Index;

    if (HalpArm64PlatformMatched)
        return;

    for (Index = 0; Index < RTL_NUMBER_OF(HalpArm64Platforms); Index++)
    {
        if (HalpArm64Platforms[Index].Probe(LoaderBlock))
        {
            HalpArm64PlatformMatched = TRUE;
            DPRINT1("[arm64][HALEXT] Platform: %s\n",
                    HalpArm64Platforms[Index].Name);
            return;
        }
    }
}

VOID
HalpArm64Phase1PlatformInit(VOID)
{
    HalpBcm2711VcInit();
    AppleIdleProbe();
}
