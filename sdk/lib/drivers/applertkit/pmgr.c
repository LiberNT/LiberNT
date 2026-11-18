/*
 * PROJECT:     LiberNT Apple RTKit Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Power states and resets of Apple silicon PMGR device domains
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef UNIT_TEST
#include <ntddk.h>
#endif
#include "pmgrint.h"

#define DPRINT1 DbgPrint

static
ULONG
ApplePmgrFindLabel(
    _In_ PFDT_DRIVER_TREE Tree,
    _In_ ULONG Node,
    _In_z_ PCSTR Label,
    _In_ ULONG Depth)
{
    const VOID *Found, *Compatible;
    ULONG Length, CompatibleLength, Child, Match;

    Found = FdtGetProperty(&Tree->Fdt, Node, "label", &Length);
    if (Found && Length == strlen(Label) + 1 && RtlEqualMemory(Found, Label, Length))
    {
        Compatible = FdtGetProperty(&Tree->Fdt, Node, "compatible", &CompatibleLength);
        if (FdtStringListContains(Compatible, CompatibleLength, "apple,pmgr-pwrstate") ||
            FdtStringListContains(Compatible, CompatibleLength, "apple,t8103-pmgr-pwrstate"))
            return Node;
    }
    if (Depth == PMGR_MAX_DEPTH)
        return FDT_NO_NODE;
    for (Child = FdtFirstChild(&Tree->Fdt, Node); Child != FDT_NO_NODE; Child = FdtNextSibling(&Tree->Fdt, Child))
    {
        Match = ApplePmgrFindLabel(Tree, Child, Label, Depth + 1);
        if (Match != FDT_NO_NODE)
            return Match;
    }
    return FDT_NO_NODE;
}

NTSTATUS
NTAPI
ApplePmgrRaiseMinimumState(
    _In_ PFDT_DRIVER_TREE Tree,
    _In_z_ PCSTR Label,
    _In_ ULONG State,
    _Out_opt_ PULONG Previous)
{
    PMGR_DOMAIN Mapping;
    ULONG Domain, Value, Minimum;

    ASSERT(KeGetCurrentIrql() <= APC_LEVEL);
    ASSERT(State <= PMGR_PS_ACTIVE);
    if (Previous)
        *Previous = 0;
    Domain = ApplePmgrFindLabel(Tree, FdtRootNode(&Tree->Fdt), Label, 0);
    if (Domain == FDT_NO_NODE)
        return STATUS_NOT_FOUND;
    if (!ApplePmgrMapDomain(Tree, Domain, &Mapping))
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    Value = READ_REGISTER_ULONG(Mapping.Register);
    Minimum = (Value & PMGR_PS_MIN) >> PMGR_PS_MIN_SHIFT;
    if (Previous)
        *Previous = Minimum;
    if (Minimum < State)
        WRITE_REGISTER_ULONG(Mapping.Register, (Value & ~(PMGR_FLAGS | PMGR_PS_MIN)) | (State << PMGR_PS_MIN_SHIFT));
    ApplePmgrUnmapDomain(&Mapping);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
ApplePmgrPowerOffDomains(
    _In_ PFDT_DRIVER_TREE Tree,
    _In_ ULONG Node,
    _In_ ULONG FirstIndex)
{
    PMGR_DOMAIN Mapping;
    const VOID *Domains;
    ULONG Length, Count, Index, Domain, Value, Wanted, Waited, Unused;
    ULONG64 Phandle;
    NTSTATUS Status = STATUS_SUCCESS;

    ASSERT(KeGetCurrentIrql() <= APC_LEVEL);
    Domains = FdtGetProperty(&Tree->Fdt, Node, "power-domains", &Length);
    Count = Domains ? Length / sizeof(ULONG) : 0;
    for (Index = Count; Index-- > FirstIndex;)
    {
        if (!FdtReadCells(Domains, Length, Index, 1, &Phandle) ||
            (Domain = FdtDriverFindPhandle(Tree, (ULONG)Phandle)) == FDT_NO_NODE)
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        if (FdtGetProperty(&Tree->Fdt, Domain, "apple,always-on", &Unused) != NULL)
            continue;
        if (!ApplePmgrMapDomain(Tree, Domain, &Mapping))
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        Value = READ_REGISTER_ULONG(Mapping.Register);
        if (PMGR_PS_ACTUAL(Value) == PMGR_PS_PWRGATE && (Value & PMGR_PS_TARGET) == PMGR_PS_PWRGATE)
        {
            ApplePmgrUnmapDomain(&Mapping);
            continue;
        }
        Wanted = (Value & ~(PMGR_AUTO_ENABLE | PMGR_FLAGS | PMGR_PS_TARGET)) | PMGR_PS_PWRGATE;
        WRITE_REGISTER_ULONG(Mapping.Register, Wanted);
        for (Waited = 0; Waited <= PMGR_PS_TIMEOUT_US; ++Waited)
        {
            Value = READ_REGISTER_ULONG(Mapping.Register);
            if (PMGR_PS_ACTUAL(Value) == PMGR_PS_PWRGATE)
                break;
            KeStallExecutionProcessor(1);
        }
        if (Waited > PMGR_PS_TIMEOUT_US)
        {
            DPRINT1("Apple PMGR: power domain %s at %I64x did not turn off (%08lx)\n", Mapping.Label, Mapping.Physical,
                    Value);
            Status = STATUS_IO_TIMEOUT;
        }
        else
        {
            DPRINT1("Apple PMGR: power domain %s turned off (%08lx)\n", Mapping.Label, Value);
        }
        ApplePmgrUnmapDomain(&Mapping);
    }
    return Status;
}
