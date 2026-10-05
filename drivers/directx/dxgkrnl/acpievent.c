/*
 * PROJECT:     LiberNT WDDM DirectX Graphics Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Delivery of ACPI and power-state events to the miniport
 *              through DxgkDdiNotifyAcpiEvent.
 * COPYRIGHT:   Copyright 2026 LiberNT WDDM Team
 *
 * Three sources feed the miniport:
 *
 *   - ACPI notifications on the adapter's own namespace node (display
 *     switch, hot-plug, cycle hotkey, dock, video wake-up), delivered as
 *     DxgkAcpiEvent.  These need GUID_ACPI_INTERFACE_STANDARD2 from the
 *     device stack; where the stack does not provide it, this source is
 *     simply absent.
 *   - The AC/DC power source, delivered as DxgkPowerStateEvent with
 *     PO_CB_AC_STATUS.
 *   - The lid switch, delivered as DxgkPowerStateEvent with
 *     PO_CB_LID_SWITCH_STATE.
 *
 * DxgkDdiNotifyAcpiEvent must be called at PASSIVE_LEVEL, and ACPI
 * notifications arrive at DISPATCH_LEVEL, so every source only queues the
 * event; one work item delivers the queue in order.  Power-setting callbacks
 * are deferred the same way so the miniport is never entered while the
 * power manager holds its notification lock.
 *
 * The miniport's reply flags are acted on as documented: POLL_DISPLAY_CHILDREN
 * re-polls child connectivity, and CHANGE_DISPLAY_MODE / CHANGE_DISPLAY_TOPOLOGY
 * re-run DxgkDdiRecommendFunctionalVidPn and commit the result through the
 * hot-plug rebuild.
 */

#include "dxgkrnl_private.h"
#include "pnp.h"

#define NDEBUG
#include <debug.h>

/* Values from the Windows WDK (ntpoapi.h, wdmguid.h).  The power-setting
 * GUIDs are not instantiated by wdmguid, and adapter.c keeps its interface
 * GUIDs local the same way. */
static CONST GUID DxgkpGuidAcpiInterfaceStandard2 =
    { 0xE8695F63, 0x1831, 0x4870, { 0xA8, 0xCF, 0x9C, 0x2F, 0x03, 0xF9, 0xDC, 0xB5 } };
static CONST GUID DxgkpGuidAcDcPowerSource =
    { 0x5D3E9A59, 0xE9D5, 0x4B00, { 0xA6, 0xBD, 0xFF, 0x34, 0xFF, 0x51, 0x65, 0x48 } };
static CONST GUID DxgkpGuidLidSwitchStateChange =
    { 0xBA3E0F4D, 0xB817, 0x4094, { 0xA2, 0xD1, 0xD5, 0x63, 0x79, 0xE6, 0xA0, 0xF3 } };

typedef struct _DXGKP_ACPI_EVENT_RECORD
{
    LIST_ENTRY      Link;
    DXGK_EVENT_TYPE Type;
    ULONG           Event;
    ULONG           Argument;
    BOOLEAN         HasArgument;
} DXGKP_ACPI_EVENT_RECORD, *PDXGKP_ACPI_EVENT_RECORD;

static IO_WORKITEM_ROUTINE DxgkpAcpiEventWorker;

/*
 * Delivers one event and acts on the flags the miniport returns.
 * PASSIVE_LEVEL.
 */
static VOID
DxgkpDeliverAcpiEvent(
    _In_ PDXGKRNL_ADAPTER         Adapter,
    _In_ PDXGKP_ACPI_EVENT_RECORD Record)
{
    PDXGKDDI_NOTIFY_ACPI_EVENT Notify;
    NTSTATUS Status;
    ULONG    Flags = 0;

    PAGED_CODE();

    if (Adapter->MiniportContext == NULL ||
        Adapter->MiniportDeviceContext == NULL)
    {
        return;
    }
    Notify = DXGK_CB_FULL(Adapter, DxgkDdiNotifyAcpiEvent);
    if (Notify == NULL)
        return;

    /* An adapter that is powered down or mid-transition is not taking calls,
     * and the contract ignores the reply flags of an adapter that loses
     * power, so the event is dropped. */
    if (!DxgkAcquireKmdCall(Adapter))
    {
        DXGKRNL_TRACE("DxgkpDeliverAcpiEvent: adapter %p not taking calls; "
                      "event type=%d code=0x%lx dropped\n",
                      Adapter, Record->Type, Record->Event);
        return;
    }
    _SEH2_TRY
    {
        Status = Notify(Adapter->MiniportDeviceContext,
                        Record->Type,
                        Record->Event,
                        Record->HasArgument ? &Record->Argument : NULL,
                        &Flags);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    DxgkReleaseKmdCall(Adapter);

    DXGKRNL_TRACE("DxgkpDeliverAcpiEvent: type=%d code=0x%lx -> 0x%08lx "
                  "flags=0x%lx\n", Record->Type, Record->Event, Status, Flags);

    /* The flags are meaningful only when the call succeeded. */
    if (!NT_SUCCESS(Status))
        return;

    if (Flags & DXGK_ACPI_POLL_DISPLAY_CHILDREN)
    {
        Status = DxgkPnpQueuePollDisplayChildren(Adapter);
        if (!NT_SUCCESS(Status))
        {
            DXGKRNL_WARN("DxgkpDeliverAcpiEvent: child poll not queued "
                         "0x%08lx\n", Status);
        }
    }

    if (Flags & (DXGK_ACPI_CHANGE_DISPLAY_MODE |
                 DXGK_ACPI_CHANGE_DISPLAY_TOPOLOGY))
    {
        Status = DxgkVidPnQueueHotPlugRebuild(Adapter);
        if (!NT_SUCCESS(Status))
        {
            DXGKRNL_WARN("DxgkpDeliverAcpiEvent: mode re-evaluation not "
                         "queued 0x%08lx\n", Status);
        }
    }
}

/*
 * Queues an event for delivery.  Callable at IRQL <= DISPATCH_LEVEL.  Events
 * that arrive while the sources are being torn down are dropped.
 */
static VOID
DxgkpQueueAcpiEvent(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ DXGK_EVENT_TYPE  Type,
    _In_ ULONG            Event,
    _In_ BOOLEAN          HasArgument,
    _In_ ULONG            Argument)
{
    PDXGKP_ACPI_EVENTS       Events = &Adapter->AcpiEvents;
    PDXGKP_ACPI_EVENT_RECORD Record;
    KIRQL                    OldIrql;

    if (!ExAcquireRundownProtection(&Events->Rundown))
        return;

    Record = (PDXGKP_ACPI_EVENT_RECORD)ExAllocatePoolWithTag(
        NonPagedPool, sizeof(*Record), TAG_DXGK_ADAPTER);
    if (Record == NULL)
    {
        DXGKRNL_WARN("DxgkpQueueAcpiEvent: no memory; event type=%d "
                     "code=0x%lx lost\n", Type, Event);
        ExReleaseRundownProtection(&Events->Rundown);
        return;
    }
    Record->Type        = Type;
    Record->Event       = Event;
    Record->Argument    = Argument;
    Record->HasArgument = HasArgument;

    KeAcquireSpinLock(&Events->Lock, &OldIrql);
    InsertTailList(&Events->Queue, &Record->Link);
    KeReleaseSpinLock(&Events->Lock, OldIrql);

    /* One delivery pass at a time.  The worker holds its own rundown
     * reference, so teardown waits for it to finish. */
    if (InterlockedExchange(&Events->WorkQueued, 1) == 0)
    {
        if (ExAcquireRundownProtection(&Events->Rundown))
        {
            IoQueueWorkItem(Events->WorkItem, DxgkpAcpiEventWorker,
                            DelayedWorkQueue, Adapter);
        }
        else
        {
            InterlockedExchange(&Events->WorkQueued, 0);
        }
    }

    ExReleaseRundownProtection(&Events->Rundown);
}

static VOID
NTAPI
DxgkpAcpiEventWorker(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_opt_ PVOID Context)
{
    PDXGKRNL_ADAPTER   Adapter = (PDXGKRNL_ADAPTER)Context;
    PDXGKP_ACPI_EVENTS Events;
    KIRQL              OldIrql;

    PAGED_CODE();
    UNREFERENCED_PARAMETER(DeviceObject);
    ASSERT(Adapter != NULL);
    Events = &Adapter->AcpiEvents;

    for (;;)
    {
        PDXGKP_ACPI_EVENT_RECORD Record = NULL;
        BOOLEAN Empty;

        KeAcquireSpinLock(&Events->Lock, &OldIrql);
        if (!IsListEmpty(&Events->Queue))
        {
            Record = CONTAINING_RECORD(RemoveHeadList(&Events->Queue),
                                       DXGKP_ACPI_EVENT_RECORD, Link);
        }
        KeReleaseSpinLock(&Events->Lock, OldIrql);

        if (Record != NULL)
        {
            DxgkpDeliverAcpiEvent(Adapter, Record);
            ExFreePoolWithTag(Record, TAG_DXGK_ADAPTER);
            continue;
        }

        /* Drained.  Give up the pass, then look once more: an event queued
         * after the last pop but before the flag cleared saw the pass still
         * running and did not start another. */
        InterlockedExchange(&Events->WorkQueued, 0);

        KeAcquireSpinLock(&Events->Lock, &OldIrql);
        Empty = IsListEmpty(&Events->Queue);
        KeReleaseSpinLock(&Events->Lock, OldIrql);

        if (Empty || InterlockedExchange(&Events->WorkQueued, 1) != 0)
            break;
    }

    ExReleaseRundownProtection(&Events->Rundown);
}

/* ---- Sources ---------------------------------------------------------- */

static VOID
NTAPI
DxgkpAcpiDeviceNotify(
    _In_ PVOID NotificationContext,
    _In_ ULONG NotifyCode)
{
    PDXGKRNL_ADAPTER Adapter = (PDXGKRNL_ADAPTER)NotificationContext;

    /* Only these codes are defined for DxgkAcpiEvent; anything else the
     * firmware raises on the node is not the miniport's to receive. */
    switch (NotifyCode)
    {
        case ACPI_NOTIFY_CYCLE_DISPLAY_HOTKEY:
        case ACPI_NOTIFY_DOCK_EVENT:
        case ACPI_NOTIFY_DEVICE_HOTPLUG:
        case ACPI_NOTIFY_PANEL_SWITCH:
        case ACPI_NOTIFY_VIDEO_WAKEUP:
            DxgkpQueueAcpiEvent(Adapter, DxgkAcpiEvent, NotifyCode, FALSE, 0);
            break;

        default:
            break;
    }
}

static NTSTATUS
NTAPI
DxgkpAcDcPowerSourceCallback(
    _In_ LPCGUID SettingGuid,
    _In_reads_bytes_(ValueLength) PVOID Value,
    _In_ ULONG ValueLength,
    _Inout_opt_ PVOID Context)
{
    UNREFERENCED_PARAMETER(SettingGuid);

    if (Context == NULL || Value == NULL ||
        ValueLength < sizeof(SYSTEM_POWER_CONDITION))
    {
        return STATUS_SUCCESS;
    }

    /* The setting reports a SYSTEM_POWER_CONDITION, where PoAc is 0.  The
     * miniport's argument is the opposite sense: 1 on AC, 0 on battery. */
    DxgkpQueueAcpiEvent((PDXGKRNL_ADAPTER)Context, DxgkPowerStateEvent,
                        PO_CB_AC_STATUS, TRUE,
                        (*(SYSTEM_POWER_CONDITION *)Value == PoAc) ? 1 : 0);
    return STATUS_SUCCESS;
}

static NTSTATUS
NTAPI
DxgkpLidSwitchCallback(
    _In_ LPCGUID SettingGuid,
    _In_reads_bytes_(ValueLength) PVOID Value,
    _In_ ULONG ValueLength,
    _Inout_opt_ PVOID Context)
{
    UNREFERENCED_PARAMETER(SettingGuid);

    if (Context == NULL || Value == NULL || ValueLength < sizeof(ULONG))
        return STATUS_SUCCESS;

    /* 1 when the lid opens, 0 when it closes -- the same sense the setting
     * reports. */
    DxgkpQueueAcpiEvent((PDXGKRNL_ADAPTER)Context, DxgkPowerStateEvent,
                        PO_CB_LID_SWITCH_STATE, TRUE,
                        (*(PULONG)Value != 0) ? 1 : 0);
    return STATUS_SUCCESS;
}

/* ---- Lifecycle -------------------------------------------------------- */

/*
 * DxgkAcpiEventsStart
 *
 * Subscribes to the event sources once the adapter has started.  Called as
 * the last step of a successful start, so there is nothing to roll back; a
 * source that cannot be subscribed is logged and skipped, since events are
 * optional to the miniport.
 */
VOID
DxgkAcpiEventsStart(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    PDXGKP_ACPI_EVENTS Events = &Adapter->AcpiEvents;
    NTSTATUS Status;

    PAGED_CODE();

    if (Events->Subscribed)
        return;

    /* Without the DDI there is no one to deliver to. */
    if (Adapter->MiniportContext == NULL ||
        DXGK_CB_FULL(Adapter, DxgkDdiNotifyAcpiEvent) == NULL)
    {
        return;
    }

    RtlZeroMemory(Events, sizeof(*Events));
    ExInitializeRundownProtection(&Events->Rundown);
    KeInitializeSpinLock(&Events->Lock);
    InitializeListHead(&Events->Queue);

    /* Everything a callback touches must exist before the first one can
     * run -- the power manager calls back with the current value from inside
     * the registration itself. */
    Events->WorkItem = IoAllocateWorkItem(Adapter->FunctionalDeviceObject);
    if (Events->WorkItem == NULL)
    {
        DXGKRNL_WARN("DxgkAcpiEventsStart: no work item; ACPI and power "
                     "events will not reach the miniport\n");
        return;
    }
    Events->Subscribed = TRUE;

    Status = PoRegisterPowerSettingCallback(Adapter->FunctionalDeviceObject,
                                            &DxgkpGuidAcDcPowerSource,
                                            DxgkpAcDcPowerSourceCallback,
                                            Adapter,
                                            &Events->AcPowerSettingHandle);
    if (!NT_SUCCESS(Status))
    {
        Events->AcPowerSettingHandle = NULL;
        DXGKRNL_WARN("DxgkAcpiEventsStart: AC/DC source not subscribed "
                     "0x%08lx\n", Status);
    }

    Status = PoRegisterPowerSettingCallback(Adapter->FunctionalDeviceObject,
                                            &DxgkpGuidLidSwitchStateChange,
                                            DxgkpLidSwitchCallback,
                                            Adapter,
                                            &Events->LidPowerSettingHandle);
    if (!NT_SUCCESS(Status))
    {
        Events->LidPowerSettingHandle = NULL;
        DXGKRNL_WARN("DxgkAcpiEventsStart: lid switch not subscribed "
                     "0x%08lx\n", Status);
    }

    /* ACPI notifications on the adapter's own node come through the ACPI
     * interface of its device stack.  A stack that does not provide one
     * leaves this source absent, which is not an error. */
    if (Adapter->PhysicalDeviceObject != NULL)
    {
        Status = DxgkpQueryPdoInterface(Adapter->PhysicalDeviceObject,
                                        &DxgkpGuidAcpiInterfaceStandard2,
                                        sizeof(Events->AcpiInterface),
                                        1,
                                        (PINTERFACE)&Events->AcpiInterface);
        if (!NT_SUCCESS(Status))
        {
            DXGKRNL_TRACE("DxgkAcpiEventsStart: device stack provides no "
                          "ACPI interface (0x%08lx); no ACPI notifications\n",
                          Status);
            RtlZeroMemory(&Events->AcpiInterface,
                          sizeof(Events->AcpiInterface));
            return;
        }

        /* The query succeeded, so the interface holds a reference that is
         * kept only while notifications stay registered through it. */
        if (Events->AcpiInterface.RegisterForDeviceNotifications != NULL &&
            Events->AcpiInterface.UnregisterForDeviceNotifications != NULL)
        {
            Status = Events->AcpiInterface.RegisterForDeviceNotifications(
                Events->AcpiInterface.Context,
                DxgkpAcpiDeviceNotify,
                Adapter);
            if (NT_SUCCESS(Status))
            {
                Events->AcpiNotificationsRegistered = TRUE;
                return;
            }
            DXGKRNL_WARN("DxgkAcpiEventsStart: ACPI notifications not "
                         "registered 0x%08lx\n", Status);
        }

        if (Events->AcpiInterface.InterfaceDereference != NULL)
        {
            Events->AcpiInterface.InterfaceDereference(
                Events->AcpiInterface.Context);
        }
        RtlZeroMemory(&Events->AcpiInterface, sizeof(Events->AcpiInterface));
    }
}

/*
 * DxgkAcpiEventsStop
 *
 * Unsubscribes every source and waits until no callback or delivery pass is
 * still running, so the miniport is never notified after it has stopped.
 * Safe to call when nothing was subscribed.
 */
VOID
DxgkAcpiEventsStop(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    PDXGKP_ACPI_EVENTS Events = &Adapter->AcpiEvents;

    PAGED_CODE();

    if (!Events->Subscribed)
        return;

    if (Events->AcpiNotificationsRegistered)
    {
        Events->AcpiInterface.UnregisterForDeviceNotifications(
            Events->AcpiInterface.Context);
        if (Events->AcpiInterface.InterfaceDereference != NULL)
        {
            Events->AcpiInterface.InterfaceDereference(
                Events->AcpiInterface.Context);
        }
        Events->AcpiNotificationsRegistered = FALSE;
    }
    if (Events->AcPowerSettingHandle != NULL)
    {
        PoUnregisterPowerSettingCallback(Events->AcPowerSettingHandle);
        Events->AcPowerSettingHandle = NULL;
    }
    if (Events->LidPowerSettingHandle != NULL)
    {
        PoUnregisterPowerSettingCallback(Events->LidPowerSettingHandle);
        Events->LidPowerSettingHandle = NULL;
    }

    /* Sources are closed; wait out any callback or delivery pass that had
     * already entered.  New arrivals fail rundown acquisition and drop. */
    ExWaitForRundownProtectionRelease(&Events->Rundown);

    /* Whatever no pass will deliver now is discarded. */
    while (!IsListEmpty(&Events->Queue))
    {
        ExFreePoolWithTag(
            CONTAINING_RECORD(RemoveHeadList(&Events->Queue),
                              DXGKP_ACPI_EVENT_RECORD, Link),
            TAG_DXGK_ADAPTER);
    }

    IoFreeWorkItem(Events->WorkItem);
    Events->WorkItem = NULL;
    Events->Subscribed = FALSE;
}
