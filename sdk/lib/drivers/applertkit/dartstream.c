/*
 * PROJECT:     LiberNT Apple RTKit Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     DART stream setup for physical device addresses
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef UNIT_TEST
#include <ntddk.h>
#endif
#include "dartint.h"

VOID
AppleDartFlush(
    _In_ PCFDT_DRIVER_IO Io,
    _In_ PUCHAR Base,
    _In_ ULONG Sid,
    _In_z_ PCSTR Owner)
{
    ULONG Spin;

    KeMemoryBarrier();
    AppleDartWrite(Base, DART_T8110_TLB_CMD, DART_T8110_TLB_CMD_FLUSH | Sid);
    for (Spin = 0; Spin < 1000 && (AppleDartRead(Base, DART_T8110_TLB_CMD) & DART_T8110_TLB_CMD_BUSY); ++Spin)
        Io->Stall(1);
    if (Spin == 1000)
        FdtDriverIoPrint(Io, "%s: DART TLB flush did not complete\n", Owner);
}

static
NTSTATUS
AppleDartPhysicalStream(
    _In_ PFDT_DRIVER_TREE Tree,
    _In_ ULONG Dart,
    _In_ ULONG Sid,
    _In_z_ PCSTR Owner)
{
    ULONG Length, Size, Params2, Params3, Streams, Protect;
    const VOID *Compatible;
    ULONG64 Physical;
    PUCHAR Base;
    NTSTATUS Status = STATUS_SUCCESS;

    Compatible = FdtGetProperty(&Tree->Fdt, Dart, "compatible", &Length);
    if (!FdtStringListContains(Compatible, Length, "apple,t8110-dart"))
    {
        FdtDriverPrint(Tree, "%s: DART %lx is not a t8110 DART\n", Owner, Dart);
        return STATUS_NOT_SUPPORTED;
    }
    Base = FdtDriverMapReg(Tree, Dart, 0, &Physical, &Size);
    if (!Base)
        return STATUS_INSUFFICIENT_RESOURCES;
    Params2 = AppleDartRead(Base, DART_PARAMS2);
    Params3 = AppleDartRead(Base, DART_T8110_PARAMS3);
    Streams = AppleDartRead(Base, DART_T8110_PARAMS4) & 0x1ff;
    Protect = AppleDartRead(Base, DART_T8110_PROTECT);
    FdtDriverPrint(Tree, "%s: DART %I64x stream %lu: params %08lx %08lx %08lx, %lu streams, protect %08lx, tcr %08lx ttbr %08lx\n",
             Owner, Physical, Sid, AppleDartRead(Base, DART_PARAMS1), Params2, Params3, Streams, Protect,
             Sid < Streams ? AppleDartRead(Base, DART_T8110_TCR + 4 * Sid) : 0,
             Sid < Streams ? AppleDartRead(Base, DART_T8110_TTBR + 4 * Sid) : 0);
    if (Sid >= Streams)
    {
        Status = STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    else if (Protect & DART_T8110_PROTECT_TCR)
    {
        if ((AppleDartRead(Base, DART_T8110_TCR + 4 * Sid) & DART_T8110_TCR_BYPASS) != DART_T8110_TCR_BYPASS)
        {
            FdtDriverPrint(Tree, "%s: DART stream %lu is locked by the firmware without bypass\n", Owner, Sid);
            Status = STATUS_ACCESS_DENIED;
        }
    }
    else if (Params2 & DART_PARAMS2_BYPASS)
    {
        AppleDartWrite(Base, DART_T8110_TCR + 4 * Sid, DART_T8110_TCR_BYPASS);
        AppleDartWrite(Base, DART_T8110_TTBR + 4 * Sid, 0);
        AppleDartWrite(Base, DART_T8110_ENABLE_STREAMS + 4 * (Sid >> 5),
                       AppleDartRead(Base, DART_T8110_ENABLE_STREAMS + 4 * (Sid >> 5)) | (1UL << (Sid & 31)));
        AppleDartFlush(Tree->Io, Base, Sid, Owner);
    }
    else
    {
        Status = AppleDartIdentityStream(Base, Sid, Params3, Owner);
    }
    Tree->Io->Unmap(Base, Size);
    return Status;
}

NTSTATUS
NTAPI
AppleDartMapStream(
    _In_ PFDT_DRIVER_TREE Tree,
    _In_ ULONG Dart,
    _In_ ULONG Sid,
    _In_z_ PCSTR Owner)
{
    ULONG Cells;

    if (Dart == FDT_NO_NODE || !FdtReadU32(&Tree->Fdt, Dart, "#iommu-cells", &Cells) || Cells != 1)
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    return AppleDartPhysicalStream(Tree, Dart, Sid, Owner);
}

NTSTATUS
NTAPI
AppleDartMapPhysical(
    _In_ PFDT_DRIVER_TREE Tree,
    _In_ ULONG Node,
    _In_z_ PCSTR Owner)
{
    const VOID *Iommus;
    ULONG Length, Index, Dart, Cells = 0;
    ULONG64 Phandle, Sid;
    NTSTATUS Status;

    Iommus = FdtGetProperty(&Tree->Fdt, Node, "iommus", &Length);
    if (!Iommus)
        return STATUS_SUCCESS;
    for (Index = 0; FdtReadCells(Iommus, Length, Index, 1, &Phandle); Index += 1 + Cells)
    {
        Dart = FdtDriverFindPhandle(Tree, (ULONG)Phandle);
        if (Dart == FDT_NO_NODE || !FdtReadU32(&Tree->Fdt, Dart, "#iommu-cells", &Cells) || Cells != 1 ||
            !FdtReadCells(Iommus, Length, Index + 1, 1, &Sid))
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        Status = AppleDartPhysicalStream(Tree, Dart, (ULONG)Sid, Owner);
        if (!NT_SUCCESS(Status))
            return Status;
    }
    return STATUS_SUCCESS;
}
