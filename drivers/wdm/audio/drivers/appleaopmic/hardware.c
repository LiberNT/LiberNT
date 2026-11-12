/*
 * PROJECT:     LiberNT Apple AOP Microphone Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Device tree, DART and ADMAC access of the built-in microphones
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntddk.h>
#include <reactos/drivers/fdtdrv.h>
#include <reactos/drivers/appleadmac.h>
#include <reactos/drivers/appledart.h>
#include <reactos/drivers/applertkit.h>
#include "hardware.h"

#define AOP_MIC_HW_TAG 'hOpA'

struct _AOP_MIC_HW
{
    FDT_DRIVER_TREE Tree;
    BOOLEAN TreeOpen;
    ULONG AdmacNode;
    ULONG DmaChannel;
    APPLE_ADMAC Admac;
    BOOLEAN AdmacOpen;
    APPLE_ADMAC_CHANNEL Channel;
    BOOLEAN ChannelOpen;
};

static
PCSTR
AopMicHwString(
    _In_ PAOP_MIC_HW Hardware,
    _In_z_ PCSTR Name)
{
    ULONG Length;
    const CHAR *Value = FdtGetProperty(&Hardware->Tree.Fdt, Hardware->Tree.Node, Name, &Length);

    return (Value && Length && !Value[Length - 1]) ? Value : "?";
}

static
NTSTATUS
AopMicHwFindAdmac(
    _In_ PFDT_DRIVER_TREE Tree,
    _Out_ PULONG AdmacNode,
    _Out_ PULONG DmaChannel)
{
    const VOID *Dmas;
    ULONG Length, Cells;
    ULONG64 Phandle, Channel;

    *AdmacNode = FDT_NO_NODE;
    *DmaChannel = 0;
    Dmas = FdtGetProperty(&Tree->Fdt, Tree->Node, "dmas", &Length);
    if (!Dmas || !FdtReadCells(Dmas, Length, 0, 1, &Phandle) || !FdtReadCells(Dmas, Length, 1, 1, &Channel) ||
        (*AdmacNode = FdtDriverFindPhandle(Tree, (ULONG)Phandle)) == FDT_NO_NODE ||
        !FdtReadU32(&Tree->Fdt, *AdmacNode, "#dma-cells", &Cells) || Cells != 1)
    {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    *DmaChannel = (ULONG)Channel;
    return STATUS_SUCCESS;
}

NTSTATUS
AopMicHwOpen(
    _Out_ PAOP_MIC_HW *Result)
{
    PAOP_MIC_HW Hardware;
    NTSTATUS Status;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    *Result = NULL;
    Hardware = ExAllocatePoolZero(NonPagedPool, sizeof(*Hardware), AOP_MIC_HW_TAG);
    if (!Hardware)
        return STATUS_INSUFFICIENT_RESOURCES;
    Hardware->AdmacNode = FDT_NO_NODE;
    Status = FdtDriverOpenByCompatible("apple,t6030-aop-audio", &Hardware->Tree);
    if (!NT_SUCCESS(Status))
    {
        if (NT_SUCCESS(FdtDriverOpenByCompatible("apple,aop-audio", &Hardware->Tree)))
        {
            FdtDriverClose(&Hardware->Tree);
            DbgPrint("appleaopmic: only the t6030 microphone configuration is supported\n");
            Status = STATUS_NOT_SUPPORTED;
        }
        else
        {
            DbgPrint("appleaopmic: no AOP audio node in the device tree: %lx\n", Status);
        }
        ExFreePoolWithTag(Hardware, AOP_MIC_HW_TAG);
        return Status;
    }
    Hardware->TreeOpen = TRUE;
    *Result = Hardware;

    Status = AopMicHwFindAdmac(&Hardware->Tree, &Hardware->AdmacNode, &Hardware->DmaChannel);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint("appleaopmic: the AOP audio node names no ADMAC channel\n");
        return Status;
    }
    DbgPrint("appleaopmic: %s %s microphones on ADMAC channel %lu\n", AopMicHwString(Hardware, "apple,machine-kind"),
             AopMicHwString(Hardware, "apple,chassis-name"), Hardware->DmaChannel);

    Status = AppleDartMapPhysical(&Hardware->Tree, Hardware->AdmacNode, "appleaopmic");
    DbgPrint("appleaopmic: ADMAC DART streams set up: %lx\n", Status);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = AppleAdmacOpen(&Hardware->Tree, Hardware->AdmacNode, &Hardware->Admac);
    DbgPrint("appleaopmic: ADMAC with %lu channels, interrupt output %lu: %lx\n", Hardware->Admac.ChannelCount,
             Hardware->Admac.InterruptIndex, Status);
    if (!NT_SUCCESS(Status))
        return Status;
    Hardware->AdmacOpen = TRUE;
    if (!(Hardware->DmaChannel & 1) || Hardware->DmaChannel >= Hardware->Admac.ChannelCount)
    {
        DbgPrint("appleaopmic: ADMAC channel %lu is not a receive channel\n", Hardware->DmaChannel);
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    return STATUS_SUCCESS;
}

VOID
AopMicHwClose(
    _In_opt_ PAOP_MIC_HW Hardware)
{
    if (!Hardware)
        return;
    AopMicHwCloseChannel(Hardware);
    if (Hardware->AdmacOpen)
        AppleAdmacClose(&Hardware->Admac);
    if (Hardware->TreeOpen)
        FdtDriverClose(&Hardware->Tree);
    ExFreePoolWithTag(Hardware, AOP_MIC_HW_TAG);
}

NTSTATUS
AopMicHwOpenChannel(
    _Inout_ PAOP_MIC_HW Hardware,
    _In_ ULONG WordBytes,
    _In_ ULONG FrameWords)
{
    NTSTATUS Status;

    if (!Hardware->AdmacOpen || Hardware->ChannelOpen)
        return STATUS_INVALID_DEVICE_STATE;
    Status = AppleAdmacOpenChannel(&Hardware->Admac, Hardware->DmaChannel, WordBytes, FrameWords, &Hardware->Channel);
    DbgPrint("appleaopmic: ADMAC channel %lu opened: %lx\n", Hardware->DmaChannel, Status);
    Hardware->ChannelOpen = NT_SUCCESS(Status);
    return Status;
}

VOID
AopMicHwCloseChannel(
    _Inout_ PAOP_MIC_HW Hardware)
{
    if (Hardware->ChannelOpen)
        AppleAdmacCloseChannel(&Hardware->Channel);
    Hardware->ChannelOpen = FALSE;
}

NTSTATUS
AopMicHwStart(
    _Inout_ PAOP_MIC_HW Hardware,
    _In_ ULONG64 Buffer,
    _In_ ULONG BufferLength,
    _In_ ULONG PeriodLength)
{
    if (!Hardware->ChannelOpen)
        return STATUS_INVALID_DEVICE_STATE;
    return AppleAdmacStartCyclic(&Hardware->Channel, Buffer, BufferLength, PeriodLength);
}

VOID
AopMicHwStop(
    _Inout_ PAOP_MIC_HW Hardware)
{
    if (Hardware->ChannelOpen)
        AppleAdmacStop(&Hardware->Channel);
}

ULONG
AopMicHwService(
    _Inout_ PAOP_MIC_HW Hardware)
{
    return Hardware->ChannelOpen ? AppleAdmacService(&Hardware->Channel) : 0;
}

ULONG
AopMicHwPosition(
    _In_ PAOP_MIC_HW Hardware)
{
    return Hardware->ChannelOpen ? AppleAdmacPosition(&Hardware->Channel) : 0;
}

BOOLEAN
AopMicHwInterrupt(
    _Inout_ PAOP_MIC_HW Hardware,
    _Out_ PULONG Reports)
{
    BOOLEAN Global, Mine;

    *Reports = 0;
    if (!Hardware->AdmacOpen)
        return FALSE;
    Mine = AppleAdmacInterruptPending(&Hardware->Admac, Hardware->DmaChannel, &Global);
    if (Mine && Hardware->ChannelOpen)
        *Reports = AppleAdmacService(&Hardware->Channel);
    return Mine || Global;
}

NTSTATUS
AopMicHwFilterRequirements(
    _Inout_ PIRP Irp)
{
    FDT_DRIVER_TREE Tree;
    ULONG AdmacNode, DmaChannel, Index, Gsi = 0;
    BOOLEAN Edge = FALSE;
    NTSTATUS Status;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    Status = FdtDriverOpenByCompatible("apple,t6030-aop-audio", &Tree);
    if (NT_SUCCESS(Status))
    {
        Status = AopMicHwFindAdmac(&Tree, &AdmacNode, &DmaChannel);
        if (NT_SUCCESS(Status))
            Status = AppleAdmacQueryInterrupt(&Tree, AdmacNode, &Index, &Gsi, &Edge);
        FdtDriverClose(&Tree);
    }
    if (NT_SUCCESS(Status))
        Status = AppleAddInterruptRequirement(Irp, Gsi, Edge);
    DbgPrint("appleaopmic: requesting the ADMAC interrupt %lu: %lx\n", Gsi, Status);
    return Status;
}
