/*
 * PROJECT:     LiberNT Hyper-V VMBus Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Hypervisor interface: hypercalls and synthetic interrupt controller
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "vmbus.h"

#define NDEBUG
#include <debug.h>

#define HV_PAGE_MESSAGE   0
#define HV_PAGE_EVENT     1
#define HV_PAGE_MONITOR   2
#define HV_PAGE_POST      4
#define HV_PAGE_INTERRUPT 5
#define HV_PAGE_COUNT     6

ULONG64
NTAPI
HvHypercallPage(
    _In_ ULONG64 Control,
    _In_ ULONG64 Input,
    _In_ ULONG64 Output);

static PVOID HvPages;

BOOLEAN
HvIsPresent(VOID)
{
    INT Registers[4];

    __cpuid(Registers, 1);
    if (!((ULONG)Registers[2] & 0x80000000UL))
        return FALSE;

    __cpuid(Registers, HV_CPUID_VENDOR_AND_MAX);
    if ((ULONG)Registers[0] < HV_CPUID_FEATURES)
        return FALSE;

    __cpuid(Registers, HV_CPUID_INTERFACE);
    if ((ULONG)Registers[0] != HV_CPUID_INTERFACE_SIGNATURE)
        return FALSE;

    __cpuid(Registers, HV_CPUID_FEATURES);
    return ((ULONG)Registers[0] & (HV_FEATURE_ACCESS_SYNIC_REGS | HV_FEATURE_ACCESS_HYPERCALL)) ==
           (HV_FEATURE_ACCESS_SYNIC_REGS | HV_FEATURE_ACCESS_HYPERCALL);
}

NTSTATUS
HvInitialize(
    _Inout_ PVMBUS_FDO_EXTENSION Bus)
{
    PHYSICAL_ADDRESS Highest;
    PHYSICAL_ADDRESS PagesPa;
    PHYSICAL_ADDRESS HypercallPa;
    KAFFINITY OldAffinity;
    ULONG64 Value;
    PUCHAR Pages;

    if (((ULONG_PTR)HvHypercallPage & (PAGE_SIZE - 1)) != 0)
    {
        DPRINT1("VMBUS: hypercall page is not page aligned\n");
        return STATUS_DATA_ERROR;
    }

    Highest.QuadPart = -1;
    Pages = MmAllocateContiguousMemory(HV_PAGE_COUNT * PAGE_SIZE, Highest);
    if (Pages == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(Pages, HV_PAGE_COUNT * PAGE_SIZE);
    HvPages = Pages;
    Bus->MessagePage = (PHV_MESSAGE)(Pages + HV_PAGE_MESSAGE * PAGE_SIZE);
    Bus->EventPage = (PULONG)(Pages + HV_PAGE_EVENT * PAGE_SIZE);
    Bus->MonitorPages = Pages + HV_PAGE_MONITOR * PAGE_SIZE;
    Bus->PostInput = (PHV_POST_MESSAGE_INPUT)(Pages + HV_PAGE_POST * PAGE_SIZE);
    Bus->PostInputPa = MmGetPhysicalAddress(Bus->PostInput);
    Bus->InterruptPage = Pages + HV_PAGE_INTERRUPT * PAGE_SIZE;

    PagesPa = MmGetPhysicalAddress(Pages);
    HypercallPa = MmGetPhysicalAddress((PVOID)HvHypercallPage);

    OldAffinity = KeSetSystemAffinityThreadEx((KAFFINITY)1);

    if (__readmsr(HV_X64_MSR_GUEST_OS_ID) == 0)
        __writemsr(HV_X64_MSR_GUEST_OS_ID, HV_GUEST_OS_ID_LIBERNT);

    Value = __readmsr(HV_X64_MSR_HYPERCALL);
    Value = (Value & 0xFFE) | ((ULONG64)HypercallPa.QuadPart & ~(ULONG64)(PAGE_SIZE - 1)) | 1;
    __writemsr(HV_X64_MSR_HYPERCALL, Value);

    Bus->VpIndex = (ULONG)__readmsr(HV_X64_MSR_VP_INDEX);

    __writemsr(HV_X64_MSR_SIMP, ((ULONG64)PagesPa.QuadPart + HV_PAGE_MESSAGE * PAGE_SIZE) | 1);
    __writemsr(HV_X64_MSR_SIEFP, ((ULONG64)PagesPa.QuadPart + HV_PAGE_EVENT * PAGE_SIZE) | 1);

    if (Bus->Polling)
        Value = HV_SINT_POLLING | 0x30;
    else
        Value = Bus->Vector & 0xFF;
    __writemsr(HV_X64_MSR_SINT0 + VMBUS_MESSAGE_SINT, Value);

    __writemsr(HV_X64_MSR_SCONTROL, __readmsr(HV_X64_MSR_SCONTROL) | 1);

    KeRevertToUserAffinityThreadEx(OldAffinity);

    Bus->HypervisorReady = TRUE;
    return STATUS_SUCCESS;
}

VOID
HvShutdown(
    _Inout_ PVMBUS_FDO_EXTENSION Bus)
{
    KAFFINITY OldAffinity;

    if (!Bus->HypervisorReady)
        return;

    OldAffinity = KeSetSystemAffinityThreadEx((KAFFINITY)1);
    __writemsr(HV_X64_MSR_SINT0 + VMBUS_MESSAGE_SINT, HV_SINT_MASKED);
    __writemsr(HV_X64_MSR_SIMP, __readmsr(HV_X64_MSR_SIMP) & ~1ULL);
    __writemsr(HV_X64_MSR_SIEFP, __readmsr(HV_X64_MSR_SIEFP) & ~1ULL);
    __writemsr(HV_X64_MSR_HYPERCALL, __readmsr(HV_X64_MSR_HYPERCALL) & ~1ULL);
    KeRevertToUserAffinityThreadEx(OldAffinity);

    Bus->HypervisorReady = FALSE;
    Bus->MessagePage = NULL;
    Bus->EventPage = NULL;
    Bus->MonitorPages = NULL;
    Bus->PostInput = NULL;
    Bus->InterruptPage = NULL;
    MmFreeContiguousMemory(HvPages);
    HvPages = NULL;
}

NTSTATUS
HvPostMessage(
    _Inout_ PVMBUS_FDO_EXTENSION Bus,
    _In_reads_bytes_(Size) const VOID *Message,
    _In_ ULONG Size)
{
    LARGE_INTEGER Delay;
    ULONG64 Status = 0;
    ULONG Attempt;

    if (Size > HV_MESSAGE_PAYLOAD_SIZE)
        return STATUS_INVALID_PARAMETER;

    for (Attempt = 0; Attempt < 100; Attempt++)
    {
        Bus->PostInput->ConnectionId = Bus->MessageConnectionId;
        Bus->PostInput->Reserved = 0;
        Bus->PostInput->MessageType = VMBUS_POST_MESSAGE_TYPE;
        Bus->PostInput->PayloadSize = Size;
        RtlCopyMemory(Bus->PostInput->Payload, Message, Size);

        Status = HvHypercallPage(HVCALL_POST_MESSAGE, (ULONG64)Bus->PostInputPa.QuadPart, 0) & 0xFFFF;
        if (Status == HV_STATUS_SUCCESS)
            return STATUS_SUCCESS;

        if (Status != HV_STATUS_INSUFFICIENT_BUFFERS && Status != HV_STATUS_INSUFFICIENT_MEMORY)
            break;

        Delay.QuadPart = -10000;
        KeDelayExecutionThread(KernelMode, FALSE, &Delay);
    }

    DPRINT1("VMBUS: HvPostMessage failed with status 0x%I64x\n", Status);
    return STATUS_UNSUCCESSFUL;
}

VOID
HvSignalEvent(
    _In_ ULONG ConnectionId)
{
    HvHypercallPage(HVCALL_SIGNAL_EVENT | HV_HYPERCALL_FAST, ConnectionId, 0);
}

VOID
HvAcknowledgeMessage(VOID)
{
    __writemsr(HV_X64_MSR_EOM, 0);
}
