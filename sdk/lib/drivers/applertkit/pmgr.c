/*
 * PROJECT:     LiberNT Apple RTKit Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Power states and resets of Apple silicon PMGR device domains
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef UNIT_TEST
#include <ntddk.h>
#endif
#include <reactos/drivers/applepmgr.h>

#define DPRINT1 DbgPrint

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
} PMGR_DOMAIN, *PPMGR_DOMAIN;

static
BOOLEAN
ApplePmgrMapDomain(
    _In_ PFDT_DRIVER_TREE Tree,
    _In_ ULONG Domain,
    _Out_ PPMGR_DOMAIN Mapping)
{
    PHYSICAL_ADDRESS Physical;
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
    Physical.QuadPart = (LONGLONG)Mapping->Physical;
    Mapping->Register = MmMapIoSpace(Physical, sizeof(ULONG), MmNonCached);
    return Mapping->Register != NULL;
}

static
VOID
ApplePmgrUnmapDomain(
    _Inout_ PPMGR_DOMAIN Mapping)
{
    if (Mapping->Register)
        MmUnmapIoSpace((PVOID)Mapping->Register, sizeof(ULONG));
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
        DPRINT1("Apple PMGR: power domain node %lx cannot be mapped\n", Domain);
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
        KeStallExecutionProcessor(1);
    }
    WRITE_REGISTER_ULONG(Mapping.Register, Wanted | PMGR_AUTO_ENABLE);
    if (Waited > PMGR_PS_TIMEOUT_US)
    {
        DPRINT1("Apple PMGR: power domain %s at %I64x did not turn on (%08lx)\n", Mapping.Label, Mapping.Physical, Value);
        ApplePmgrUnmapDomain(&Mapping);
        return STATUS_IO_TIMEOUT;
    }
    DPRINT1("Apple PMGR: power domain %s turned on (%08lx)\n", Mapping.Label, Value);
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
        DPRINT1("Apple PMGR: cannot reset %s while it is off (%08lx)\n", Mapping.Label, Value);
        ApplePmgrUnmapDomain(&Mapping);
        return STATUS_DEVICE_NOT_READY;
    }
    Value &= ~PMGR_FLAGS;
    WRITE_REGISTER_ULONG(Mapping.Register, Value | PMGR_DEV_DISABLE);
    WRITE_REGISTER_ULONG(Mapping.Register, Value | PMGR_DEV_DISABLE | PMGR_RESET);
    KeStallExecutionProcessor(1);
    WRITE_REGISTER_ULONG(Mapping.Register, (Value & ~PMGR_RESET) | PMGR_DEV_DISABLE);
    WRITE_REGISTER_ULONG(Mapping.Register, Value & ~(PMGR_RESET | PMGR_DEV_DISABLE));
    ApplePmgrUnmapDomain(&Mapping);
    return STATUS_SUCCESS;
}

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
