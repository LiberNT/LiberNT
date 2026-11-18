/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Platform extension plug-in registration
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include <acpiioct.h>
#include <debug.h>
#include <pepfx.h>

#define POP_PEP_TAG                       'pePP'

#include "pepcore.h"

typedef struct _POP_PEP_PLUGIN
{
    LIST_ENTRY ListEntry;
    PEP_INFORMATION Information;
    ULONGLONG Flags;
    WORK_QUEUE_ITEM WorkItem;
    LONG WorkRequests;
} POP_PEP_PLUGIN, *PPOP_PEP_PLUGIN;

static LIST_ENTRY PopPepPluginList = {&PopPepPluginList, &PopPepPluginList};
static KSPIN_LOCK PopPepPluginLock;

static
VOID
NTAPI
PopPepWorker(
    _In_ PVOID Parameter)
{
    PPOP_PEP_PLUGIN Plugin = Parameter;
    PEP_WORK_INFORMATION WorkInformation;
    PEP_WORK Work;

    do
    {
        RtlZeroMemory(&WorkInformation, sizeof(WorkInformation));
        Work.WorkInformation = &WorkInformation;
        Work.NeedWork = FALSE;
        if (Plugin->Information.AcceptDeviceNotification != NULL)
            Plugin->Information.AcceptDeviceNotification(PEP_DPM_WORK, &Work);
    } while (InterlockedDecrement(&Plugin->WorkRequests) != 0);
}

static
VOID
NTAPI
PopPepRequestWorker(
    _In_ POHANDLE PluginHandle)
{
    PPOP_PEP_PLUGIN Plugin = (PPOP_PEP_PLUGIN)PluginHandle;

    if (InterlockedIncrement(&Plugin->WorkRequests) == 1)
        ExQueueWorkItem(&Plugin->WorkItem, DelayedWorkQueue);
}

static
NTSTATUS
NTAPI
PopPepEnumerateUnmaskedInterrupts(
    _In_opt_ POHANDLE PluginHandle,
    _In_opt_ ULONG EnumerateFlags,
    _In_ PPO_ENUMERATE_INTERRUPT_SOURCE_CALLBACK Callback,
    _In_ PVOID CallbackContext,
    _Inout_ PPEP_UNMASKED_INTERRUPT_INFORMATION InterruptInformation)
{
    UNREFERENCED_PARAMETER(PluginHandle);
    UNREFERENCED_PARAMETER(EnumerateFlags);
    UNREFERENCED_PARAMETER(Callback);
    UNREFERENCED_PARAMETER(CallbackContext);
    UNREFERENCED_PARAMETER(InterruptInformation);
    return STATUS_NOT_SUPPORTED;
}

static
NTSTATUS
NTAPI
PopPepProcessorHalt(
    _In_ ULONG Flags,
    _Inout_opt_ PVOID Context,
    _In_ PPROCESSOR_HALT_ROUTINE Halt)
{
    UNREFERENCED_PARAMETER(Flags);
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(Halt);
    return STATUS_NOT_SUPPORTED;
}

static
NTSTATUS
NTAPI
PopPepRequestInterrupt(
    _In_ ULONG Gsiv)
{
    UNREFERENCED_PARAMETER(Gsiv);
    return STATUS_NOT_SUPPORTED;
}

static
VOID
NTAPI
PopPepTransitionCriticalResource(
    _In_ POHANDLE DeviceHandle,
    _In_ ULONG Component,
    _In_ BOOLEAN Active)
{
    UNREFERENCED_PARAMETER(DeviceHandle);
    UNREFERENCED_PARAMETER(Component);
    UNREFERENCED_PARAMETER(Active);
}

static
NTSTATUS
NTAPI
PopPepIdleVeto(
    _In_ POHANDLE ProcessorHandle,
    _In_ ULONG State,
    _In_ ULONG VetoReason,
    _In_ BOOLEAN Increment)
{
    UNREFERENCED_PARAMETER(ProcessorHandle);
    UNREFERENCED_PARAMETER(State);
    UNREFERENCED_PARAMETER(VetoReason);
    UNREFERENCED_PARAMETER(Increment);
    return STATUS_NOT_SUPPORTED;
}

static
NTSTATUS
NTAPI
PopPepUpdateProcessorIdleState(
    _In_ POHANDLE ProcessorHandle,
    _In_ ULONG ProcessorState,
    _In_ PPEP_PROCESSOR_IDLE_STATE_UPDATE Update)
{
    UNREFERENCED_PARAMETER(ProcessorHandle);
    UNREFERENCED_PARAMETER(ProcessorState);
    UNREFERENCED_PARAMETER(Update);
    return STATUS_NOT_SUPPORTED;
}

static
NTSTATUS
NTAPI
PopPepUpdatePlatformIdleState(
    _In_ POHANDLE ProcessorHandle,
    _In_ ULONG PlatformState,
    _In_ PPEP_PLATFORM_IDLE_STATE_UPDATE Update)
{
    UNREFERENCED_PARAMETER(ProcessorHandle);
    UNREFERENCED_PARAMETER(PlatformState);
    UNREFERENCED_PARAMETER(Update);
    return STATUS_NOT_SUPPORTED;
}

static
NTSTATUS
NTAPI
PopPepRequestCommon(
    _In_ ULONG RequestId,
    _Inout_opt_ PVOID Data)
{
    UNREFERENCED_PARAMETER(RequestId);
    UNREFERENCED_PARAMETER(Data);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
PoFxRegisterPluginEx(
    _In_ PPEP_INFORMATION PepInformation,
    _In_ ULONGLONG Flags,
    _Inout_ PPEP_KERNEL_INFORMATION KernelInformation)
{
    PEP_INFORMATION Information;
    PPOP_PEP_PLUGIN Plugin;
    PLIST_ENTRY Entry;
    ULONG InformationSize;
    ULONG KernelSize;
    KIRQL OldIrql;

    PAGED_CODE();

    if ((PepInformation == NULL) || (KernelInformation == NULL))
        return STATUS_INVALID_PARAMETER;
    InformationSize = PopPepInformationSize(PepInformation->Version);
    if ((InformationSize == 0) || (PepInformation->Size < InformationSize))
        return STATUS_INVALID_PEP_INFO_VERSION;
    RtlZeroMemory(&Information, sizeof(Information));
    RtlCopyMemory(&Information, PepInformation, min((ULONG)PepInformation->Size, (ULONG)sizeof(Information)));
    KernelSize = PopPepKernelInformationSize(KernelInformation->Version);
    if ((KernelSize == 0) ||
        (KernelInformation->Size < KernelSize) ||
        ((Information.AcceptDeviceNotification == NULL) &&
         (Information.AcceptProcessorNotification == NULL) &&
         (Information.AcceptAcpiNotification == NULL)))
    {
        return STATUS_INVALID_PARAMETER;
    }
    ASSERT(KernelSize >= FIELD_OFFSET(PEP_KERNEL_INFORMATION, RequestCommon));

    Plugin = ExAllocatePoolZero(NonPagedPool, sizeof(*Plugin), POP_PEP_TAG);
    if (Plugin == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    Plugin->Information = Information;
    Plugin->Flags = Flags;
    ExInitializeWorkItem(&Plugin->WorkItem, PopPepWorker, Plugin);

    KeAcquireSpinLock(&PopPepPluginLock, &OldIrql);
    for (Entry = PopPepPluginList.Flink; Entry != &PopPepPluginList; Entry = Entry->Flink)
    {
        PPOP_PEP_PLUGIN Other = CONTAINING_RECORD(Entry, POP_PEP_PLUGIN, ListEntry);

        if ((Other->Information.AcceptDeviceNotification == Information.AcceptDeviceNotification) &&
            (Other->Information.AcceptProcessorNotification == Information.AcceptProcessorNotification) &&
            (Other->Information.AcceptAcpiNotification == Information.AcceptAcpiNotification))
        {
            KeReleaseSpinLock(&PopPepPluginLock, OldIrql);
            ExFreePoolWithTag(Plugin, POP_PEP_TAG);
            return STATUS_INVALID_PARAMETER;
        }
    }
    InsertTailList(&PopPepPluginList, &Plugin->ListEntry);
    KeReleaseSpinLock(&PopPepPluginLock, OldIrql);

    KernelInformation->Plugin = (POHANDLE)Plugin;
    KernelInformation->RequestWorker = PopPepRequestWorker;
    KernelInformation->EnumerateUnmaskedInterrupts = PopPepEnumerateUnmaskedInterrupts;
    KernelInformation->ProcessorHalt = PopPepProcessorHalt;
    KernelInformation->RequestInterrupt = PopPepRequestInterrupt;
    KernelInformation->TransitionCriticalResource = PopPepTransitionCriticalResource;
    KernelInformation->ProcessorIdleVeto = PopPepIdleVeto;
    KernelInformation->PlatformIdleVeto = PopPepIdleVeto;
    KernelInformation->UpdateProcessorIdleState = PopPepUpdateProcessorIdleState;
    KernelInformation->UpdatePlatformIdleState = PopPepUpdatePlatformIdleState;
    if (KernelSize >= sizeof(PEP_KERNEL_INFORMATION))
        KernelInformation->RequestCommon = PopPepRequestCommon;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
PoFxRegisterPlugin(
    _In_ PPEP_INFORMATION PepInformation,
    _Inout_ PPEP_KERNEL_INFORMATION KernelInformation)
{
    return PoFxRegisterPluginEx(PepInformation, 0, KernelInformation);
}
