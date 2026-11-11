/*
 * PROJECT:     LiberNT Apple PWM Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     LEDs driven by a PWM output, as the device tree describes them
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef UNIT_TEST
#include <ntddk.h>
#endif
#include "fpwm.h"

#define PWM_LED_START_PERCENT 50

static
BOOLEAN
PwmLedReadString(
    _In_ const FDT *Fdt,
    _In_ ULONG Node,
    _In_z_ const CHAR *Name,
    _Out_writes_z_(PWM_LED_MAX_NAME) PCHAR Out)
{
    const CHAR *Value;
    ULONG Length, Index;

    Value = FdtGetProperty(Fdt, Node, Name, &Length);
    if (!Value || Length < 2 || Value[Length - 1])
        return FALSE;
    for (Index = 0; Index + 1 < PWM_LED_MAX_NAME && Value[Index]; ++Index)
        Out[Index] = Value[Index];
    Out[Index] = '\0';
    return TRUE;
}

NTSTATUS
PwmLedParse(
    _In_ const FDT *Fdt,
    _In_ ULONG Node,
    _In_ ULONG ProviderCells,
    _Out_ PPWM_LED Led)
{
    const VOID *Property;
    const CHAR *NodeName;
    ULONG64 Provider, Channel, Period;
    ULONG Length, Index;

    RtlZeroMemory(Led, sizeof(*Led));
    if (ProviderCells != 2)
        return STATUS_NOT_SUPPORTED;
    Property = FdtGetProperty(Fdt, Node, "pwms", &Length);
    if (!Property || Length != (1 + ProviderCells) * sizeof(ULONG) ||
        !FdtReadCells(Property, Length, 0, 1, &Provider) ||
        !FdtReadCells(Property, Length, 1, 1, &Channel) ||
        !FdtReadCells(Property, Length, 2, 1, &Period) || !Period)
    {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    Led->Provider = (ULONG)Provider;
    Led->Channel = (ULONG)Channel;
    Led->PeriodNs = (ULONG)Period;
    if (!FdtReadU32(Fdt, Node, "max-brightness", &Led->MaxBrightness) || !Led->MaxBrightness)
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    if (!FdtReadU32(Fdt, Node, "default-brightness", &Led->DefaultBrightness) ||
        Led->DefaultBrightness > Led->MaxBrightness)
    {
        Led->DefaultBrightness = Led->MaxBrightness;
    }
    Led->ActiveLow = FdtGetProperty(Fdt, Node, "active-low", &Length) != NULL;
    Property = FdtGetProperty(Fdt, Node, "default-state", &Length);
    if (FdtStringListContains(Property, Length, "on"))
        Led->Default = PwmLedDefaultOn;
    else if (FdtStringListContains(Property, Length, "keep"))
        Led->Default = PwmLedDefaultKeep;
    else
        Led->Default = PwmLedDefaultOff;
    if (!PwmLedReadString(Fdt, Node, "label", Led->Name) && !PwmLedReadString(Fdt, Node, "function", Led->Name))
    {
        NodeName = FdtNodeName(Fdt, Node, &Length);
        for (Index = 0; NodeName && Index < Length && Index + 1 < PWM_LED_MAX_NAME; ++Index)
            Led->Name[Index] = NodeName[Index];
        Led->Name[Index] = '\0';
    }
    return STATUS_SUCCESS;
}

ULONG
PwmLedStartBrightness(
    _In_ const PWM_LED *Led,
    _In_ const FPWM_OUTPUT *Current)
{
    ULONG64 Duty, Brightness;

    ASSERT(Led->MaxBrightness && Led->DefaultBrightness <= Led->MaxBrightness);
    switch (Led->Default)
    {
        case PwmLedDefaultOn:
            return Led->DefaultBrightness;

        case PwmLedDefaultKeep:
            if (Current->Enabled && Current->PeriodNs && Current->DutyNs <= Current->PeriodNs)
            {
                Duty = Led->ActiveLow ? Current->PeriodNs - Current->DutyNs : Current->DutyNs;
                Brightness = (ULONG64)Led->MaxBrightness * Duty / Current->PeriodNs;
                if (Brightness)
                    return (ULONG)Brightness;
            }
            return (ULONG)((ULONG64)Led->MaxBrightness * PWM_LED_START_PERCENT / 100);

        default:
            return 0;
    }
}

ULONG64
PwmLedDutyNs(
    _In_ const PWM_LED *Led,
    _In_ ULONG Brightness)
{
    ULONG64 Duty;

    ASSERT(Led->MaxBrightness && Brightness <= Led->MaxBrightness);
    Duty = (ULONG64)Led->PeriodNs * Brightness / Led->MaxBrightness;
    ASSERT(Duty <= Led->PeriodNs);
    return Led->ActiveLow ? Led->PeriodNs - Duty : Duty;
}
