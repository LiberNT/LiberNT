/*
 * PROJECT:     LiberNT Apple RTKit Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Power-on and reset of Apple silicon PMGR device domains
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef UNIT_TEST
#include <ntddk.h>
#endif
#include "pmgrint.h"

BOOLEAN
ApplePmgrMapDomain(
    _In_ PFDT_DRIVER_TREE Tree,
    _In_ ULONG Domain,
    _Out_ PPMGR_DOMAIN Mapping)
{
    ULONG64 Base, Size, Offset, OffsetSize;
    ULONG Pmgr, Length;
    const VOID *Compatible;

    RtlZeroMemory(Mapping, sizeof(*Mapping));
    Compatible = FdtGetProperty(&Tree->Fdt, Domain, "compatible", &Length);
    if (!FdtStringListContains(Compatible, Length, "apple,pmgr-pwrstate") &&
        !FdtStringListContains(Compatible, Length, "apple,t8103-pmgr-pwrstate"))
        return FALSE;
    if ((Pmgr = FdtDriverFindParentNode(Tree, Domain)) == FDT_NO_NODE ||
        !FdtDriverReadReg(Tree, Pmgr, 0, &Base, &Size) ||
        !FdtReadReg(&Tree->Fdt, Domain, Pmgr, 0, &Offset, &OffsetSize) ||
        Offset + sizeof(ULONG) > Size)
        return FALSE;
    Mapping->Physical = Base + Offset;
    Mapping->Label = (PCSTR)FdtGetProperty(&Tree->Fdt, Domain, "label", &Length);
    if (!Mapping->Label || !Length || Mapping->Label[Length - 1])
        Mapping->Label = FdtNodeName(&Tree->Fdt, Domain, &Length);
    Mapping->Io = Tree->Io;
    Mapping->Register = Tree->Io ? Tree->Io->Map(Mapping->Physical, sizeof(ULONG)) : NULL;
    return Mapping->Register != NULL;
}

VOID
ApplePmgrUnmapDomain(
    _Inout_ PPMGR_DOMAIN Mapping)
{
    if (Mapping->Register)
        Mapping->Io->Unmap((PVOID)Mapping->Register, sizeof(ULONG));
    Mapping->Register = NULL;
}

static
NTSTATUS
ApplePmgrPowerOnDomains(
    _In_ PFDT_DRIVER_TREE Tree,
    _In_ ULONG Node,
    _In_ ULONG Depth);

static
NTSTATUS
ApplePmgrPowerOnDomain(
    _In_ PFDT_DRIVER_TREE Tree,
    _In_ ULONG Domain,
    _In_ ULONG Depth)
{
    PMGR_DOMAIN Mapping;
    ULONG Value, Wanted, Waited, Length;
    BOOLEAN ClockGatedIsEnough;
    NTSTATUS Status;

    Status = ApplePmgrPowerOnDomains(Tree, Domain, Depth + 1);
    if (!NT_SUCCESS(Status))
        return Status;
    if (!ApplePmgrMapDomain(Tree, Domain, &Mapping))
    {
        FdtDriverPrint(Tree, "Apple PMGR: power domain node %lx cannot be mapped\n", Domain);
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    ClockGatedIsEnough = FdtGetProperty(&Tree->Fdt, Domain, "apple,externally-clocked", &Length) != NULL;
    Value = READ_REGISTER_ULONG(Mapping.Register);
    if (PMGR_PS_ACTUAL(Value) == PMGR_PS_ACTIVE && !(Value & (PMGR_DEV_DISABLE | PMGR_PS_RESET)))
    {
        ApplePmgrUnmapDomain(&Mapping);
        return STATUS_SUCCESS;
    }
    Wanted = (Value & ~(PMGR_DEV_DISABLE | PMGR_PS_RESET | PMGR_AUTO_ENABLE | PMGR_FLAGS | PMGR_PS_TARGET)) | PMGR_PS_ACTIVE;
    WRITE_REGISTER_ULONG(Mapping.Register, Wanted);
    for (Waited = 0; Waited <= PMGR_PS_TIMEOUT_US; ++Waited)
    {
        Value = READ_REGISTER_ULONG(Mapping.Register);
        if (PMGR_PS_ACTUAL(Value) == PMGR_PS_ACTIVE ||
            (ClockGatedIsEnough && PMGR_PS_ACTUAL(Value) >= PMGR_PS_CLKGATE))
            break;
        Tree->Io->Stall(1);
    }
    WRITE_REGISTER_ULONG(Mapping.Register, Wanted | PMGR_AUTO_ENABLE);
    if (Waited > PMGR_PS_TIMEOUT_US)
    {
        FdtDriverPrint(Tree, "Apple PMGR: power domain %s at %I64x did not turn on (%08lx)\n", Mapping.Label, Mapping.Physical, Value);
        ApplePmgrUnmapDomain(&Mapping);
        return STATUS_IO_TIMEOUT;
    }
    FdtDriverPrint(Tree, "Apple PMGR: power domain %s turned on (%08lx)\n", Mapping.Label, Value);
    ApplePmgrUnmapDomain(&Mapping);
    return STATUS_SUCCESS;
}

static
NTSTATUS
ApplePmgrPowerOnDomains(
    _In_ PFDT_DRIVER_TREE Tree,
    _In_ ULONG Node,
    _In_ ULONG Depth)
{
    const VOID *Domains;
    ULONG Length, Index, Domain;
    ULONG64 Phandle;
    NTSTATUS Status;

    if (Depth > PMGR_MAX_DEPTH)
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    Domains = FdtGetProperty(&Tree->Fdt, Node, "power-domains", &Length);
    for (Index = 0; Domains && FdtReadCells(Domains, Length, Index, 1, &Phandle); ++Index)
    {
        Domain = FdtDriverFindPhandle(Tree, (ULONG)Phandle);
        if (Domain == FDT_NO_NODE)
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        Status = ApplePmgrPowerOnDomain(Tree, Domain, Depth);
        if (!NT_SUCCESS(Status))
            return Status;
    }
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
ApplePmgrPowerOnDevice(
    _In_ PFDT_DRIVER_TREE Tree,
    _In_ ULONG Node)
{
    return ApplePmgrPowerOnDomains(Tree, Node, 0);
}

NTSTATUS
NTAPI
ApplePmgrResetDevice(
    _In_ PFDT_DRIVER_TREE Tree,
    _In_ ULONG Node)
{
    PMGR_DOMAIN Mapping;
    ULONG Phandle, Domain, Value;

    if (!FdtReadU32(&Tree->Fdt, Node, "resets", &Phandle) ||
        (Domain = FdtDriverFindPhandle(Tree, Phandle)) == FDT_NO_NODE ||
        !ApplePmgrMapDomain(Tree, Domain, &Mapping))
        return STATUS_NOT_FOUND;
    Value = READ_REGISTER_ULONG(Mapping.Register);
    if (PMGR_PS_ACTUAL(Value) != PMGR_PS_ACTIVE)
    {
        FdtDriverPrint(Tree, "Apple PMGR: cannot reset %s while it is off (%08lx)\n", Mapping.Label, Value);
        ApplePmgrUnmapDomain(&Mapping);
        return STATUS_DEVICE_NOT_READY;
    }
    Value &= ~PMGR_FLAGS;
    WRITE_REGISTER_ULONG(Mapping.Register, Value | PMGR_DEV_DISABLE);
    WRITE_REGISTER_ULONG(Mapping.Register, Value | PMGR_DEV_DISABLE | PMGR_RESET);
    Tree->Io->Stall(1);
    WRITE_REGISTER_ULONG(Mapping.Register, (Value & ~PMGR_RESET) | PMGR_DEV_DISABLE);
    WRITE_REGISTER_ULONG(Mapping.Register, Value & ~(PMGR_RESET | PMGR_DEV_DISABLE));
    ApplePmgrUnmapDomain(&Mapping);
    return STATUS_SUCCESS;
}
