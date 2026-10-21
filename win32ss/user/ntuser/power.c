/*
 * PROJECT:         ReactOS Win32k subsystem
 * LICENSE:         GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:         Power management of the Win32 kernel-mode subsystem
 * COPYRIGHT:       Copyright 2024 George Bișoc <george.bisoc@reactos.org>
 *                  Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

/* INCLUDES ******************************************************************/

#include <win32k.h>
DBG_DEFAULT_CHANNEL(UserPowerManager);

/* GLOBALS *******************************************************************/

LIST_ENTRY gPowerCalloutsQueueList;
PFAST_MUTEX gpPowerCalloutMutexLock;
PKEVENT gpPowerRequestCalloutEvent;
PKTHREAD gpPowerCalloutMutexOwnerThread;

typedef struct _USER_POWER_SETTING
{
    LIST_ENTRY Link;
    GUID Guid;
    PVOID Registration;
    ULONG References;
    BOOLEAN HasValue;
    ULONG ValueLength;
    ULONG64 Value[USER_POWER_SETTING_VALUE_MAX / sizeof(ULONG64)];
} USER_POWER_SETTING, *PUSER_POWER_SETTING;

typedef struct _USER_POWER_NOTIFY
{
    LIST_ENTRY Link;
    PPROCESSINFO ppi;
    HANDLE Recipient;
    BOOLEAN Service;
    GUID Guid;
    ULONG Cookie;
    PUSER_POWER_SETTING Setting;
} USER_POWER_NOTIFY, *PUSER_POWER_NOTIFY;

typedef struct _USER_POWER_DELIVERY
{
    HANDLE Recipient;
    BOOLEAN Service;
    GUID Guid;
    ULONG Length;
    ULONG64 Value[USER_POWER_SETTING_VALUE_MAX / sizeof(ULONG64)];
} USER_POWER_DELIVERY, *PUSER_POWER_DELIVERY;

NTKERNELAPI
BOOLEAN
NTAPI
IoPnPDeliverServicePowerSetting(
    _In_ ULONG PowerNotificationCode,
    _In_ ULONG ServiceStatusHandle,
    _In_reads_bytes_(SettingSize) PVOID Setting,
    _In_ ULONG SettingSize);

static const GUID UserGuidConsoleDisplayState = { 0x6fe69556, 0x704a, 0x47a0, { 0x8f, 0x24, 0xc2, 0x8d, 0x93, 0x6f, 0xda, 0x47 } };
static const GUID UserGuidMonitorPowerOn = { 0x02731015, 0x4510, 0x4526, { 0x99, 0xe6, 0xe5, 0xa1, 0x7e, 0xbd, 0x1a, 0xea } };
static const GUID UserGuidSessionDisplayStatus = { 0x2b84c20e, 0xad23, 0x4ddf, { 0x93, 0xdb, 0x05, 0xff, 0xbd, 0x7e, 0xfc, 0xa5 } };

static LIST_ENTRY gPowerNotifyList = { &gPowerNotifyList, &gPowerNotifyList };
static LIST_ENTRY gPowerSettingList = { &gPowerSettingList, &gPowerSettingList };
static ULONG gPowerNotifyCookie;
static MONITOR_DISPLAY_STATE gConsoleDisplayState = PowerMonitorOn;
static volatile LONG gConsoleDisplayWakePending;

/* PRIVATE FUNCTIONS *********************************************************/

static
BOOLEAN
IntGetDisplaySettingValue(
    _In_ LPCGUID Guid,
    _Out_ PULONG Value)
{
    if (IsEqualGUID(Guid, &UserGuidConsoleDisplayState) || IsEqualGUID(Guid, &UserGuidSessionDisplayStatus))
    {
        *Value = gConsoleDisplayState;
        return TRUE;
    }

    if (IsEqualGUID(Guid, &UserGuidMonitorPowerOn))
    {
        *Value = (gConsoleDisplayState != PowerMonitorOff);
        return TRUE;
    }

    return FALSE;
}

static
VOID
IntQueuePowerSettingCallout(
    _In_ LPCGUID Guid,
    _In_ ULONG Cookie,
    _In_reads_bytes_opt_(Length) const VOID *Value,
    _In_ ULONG Length)
{
    PWIN32POWERCALLOUT pWin32PwrCallout;

    if (gpPowerCalloutMutexLock == NULL || gpPowerRequestCalloutEvent == NULL || Length > USER_POWER_SETTING_VALUE_MAX)
        return;

    pWin32PwrCallout = ExAllocatePoolZero(NonPagedPool, sizeof(WIN32POWERCALLOUT), USERTAG_POWER);
    if (pWin32PwrCallout == NULL)
        return;

    pWin32PwrCallout->Type = POWER_CALLOUT_SETTING;
    pWin32PwrCallout->SettingGuid = *Guid;
    pWin32PwrCallout->SettingCookie = Cookie;
    if (Value != NULL)
    {
        pWin32PwrCallout->SettingHasValue = TRUE;
        pWin32PwrCallout->SettingLength = Length;
        RtlCopyMemory(pWin32PwrCallout->SettingValue, Value, Length);
    }

    IntAcquirePowerCalloutLock();
    InsertTailList(&gPowerCalloutsQueueList, &pWin32PwrCallout->Link);
    IntReleasePowerCalloutLock();
    KeSetEvent(gpPowerRequestCalloutEvent, EVENT_INCREMENT, FALSE);
}

static
NTSTATUS
NTAPI
IntPowerSettingCallback(
    _In_ LPCGUID SettingGuid,
    _In_reads_bytes_(ValueLength) PVOID Value,
    _In_ ULONG ValueLength,
    _Inout_opt_ PVOID Context)
{
    UNREFERENCED_PARAMETER(Context);

    IntQueuePowerSettingCallout(SettingGuid, 0, Value, ValueLength);
    return STATUS_SUCCESS;
}

static
VOID
IntDeliverPowerSettings(
    _In_reads_(Count) PUSER_POWER_DELIVERY Deliveries,
    _In_ ULONG Count,
    _In_ BOOLEAN Synchronous)
{
    ULONGLONG Deadline = KeQueryInterruptTime() + 2000ULL * 10000ULL;
    ULONGLONG Now;
    ULONG_PTR Result;
    union
    {
        POWERBROADCAST_SETTING Setting;
        ULONG64 Align[(sizeof(POWERBROADCAST_SETTING) + USER_POWER_SETTING_VALUE_MAX + sizeof(ULONG64) - 1) / sizeof(ULONG64)];
    } Buffer;
    ULONG i;

    for (i = 0; i < Count; i++)
    {
        Buffer.Setting.PowerSetting = Deliveries[i].Guid;
        Buffer.Setting.DataLength = Deliveries[i].Length;
        RtlCopyMemory(Buffer.Setting.Data, Deliveries[i].Value, Deliveries[i].Length);

        if (Deliveries[i].Service)
        {
            IoPnPDeliverServicePowerSetting(PBT_POWERSETTINGCHANGE,
                                            (ULONG)(ULONG_PTR)Deliveries[i].Recipient,
                                            &Buffer.Setting,
                                            FIELD_OFFSET(POWERBROADCAST_SETTING, Data) + Deliveries[i].Length);
            continue;
        }

        if (ValidateHwndNoErr((HWND)Deliveries[i].Recipient) == NULL)
            continue;

        Now = KeQueryInterruptTime();
        if (Synchronous && Now < Deadline)
        {
            co_IntSendMessageTimeout((HWND)Deliveries[i].Recipient,
                                     WM_POWERBROADCAST,
                                     PBT_POWERSETTINGCHANGE,
                                     (LPARAM)&Buffer.Setting,
                                     SMTO_ABORTIFHUNG,
                                     (UINT)max((Deadline - Now) / 10000ULL, 1ULL),
                                     &Result);
            continue;
        }

        co_IntSendMessageWithCallBack((HWND)Deliveries[i].Recipient,
                                      WM_POWERBROADCAST,
                                      PBT_POWERSETTINGCHANGE,
                                      (LPARAM)&Buffer.Setting,
                                      NULL,
                                      0,
                                      NULL);
    }
}

static
VOID
IntBroadcastPowerSetting(
    _In_opt_ PUSER_POWER_SETTING Setting,
    _In_opt_ PUSER_POWER_NOTIFY Target,
    _In_ BOOLEAN Synchronous)
{
    PUSER_POWER_DELIVERY Deliveries;
    PUSER_POWER_NOTIFY Notify;
    PLIST_ENTRY Entry;
    ULONG Count = 0;
    ULONG Value;

    for (Entry = gPowerNotifyList.Flink; Entry != &gPowerNotifyList; Entry = Entry->Flink)
        Count++;
    if (Count == 0)
        return;

    Deliveries = ExAllocatePoolWithTag(PagedPool, Count * sizeof(USER_POWER_DELIVERY), USERTAG_POWER);
    if (Deliveries == NULL)
        return;

    Count = 0;
    for (Entry = gPowerNotifyList.Flink; Entry != &gPowerNotifyList; Entry = Entry->Flink)
    {
        Notify = CONTAINING_RECORD(Entry, USER_POWER_NOTIFY, Link);
        if (Target != NULL && Notify != Target)
            continue;

        if (Notify->Setting == NULL)
        {
            if (Setting != NULL || !IntGetDisplaySettingValue(&Notify->Guid, &Value))
                continue;
            Deliveries[Count].Length = sizeof(Value);
            RtlCopyMemory(Deliveries[Count].Value, &Value, sizeof(Value));
        }
        else
        {
            if ((Setting != NULL && Notify->Setting != Setting) ||
                (Setting == NULL && Target == NULL) ||
                !Notify->Setting->HasValue)
            {
                continue;
            }
            Deliveries[Count].Length = Notify->Setting->ValueLength;
            RtlCopyMemory(Deliveries[Count].Value, Notify->Setting->Value, Notify->Setting->ValueLength);
        }

        Deliveries[Count].Recipient = Notify->Recipient;
        Deliveries[Count].Service = Notify->Service;
        Deliveries[Count].Guid = Notify->Guid;
        Count++;
    }

    IntDeliverPowerSettings(Deliveries, Count, Synchronous);
    ExFreePoolWithTag(Deliveries, USERTAG_POWER);
}

static
VOID
IntHandlePowerSettingWorker(
    _In_ PWIN32POWERCALLOUT pWin32PwrCallout)
{
    PUSER_POWER_SETTING Setting;
    PUSER_POWER_NOTIFY Notify;
    PLIST_ENTRY Entry;

    if (pWin32PwrCallout->SettingCookie != 0)
    {
        for (Entry = gPowerNotifyList.Flink; Entry != &gPowerNotifyList; Entry = Entry->Flink)
        {
            Notify = CONTAINING_RECORD(Entry, USER_POWER_NOTIFY, Link);
            if (Notify->Cookie == pWin32PwrCallout->SettingCookie)
            {
                IntBroadcastPowerSetting(NULL, Notify, FALSE);
                break;
            }
        }
        return;
    }

    for (Entry = gPowerSettingList.Flink; Entry != &gPowerSettingList; Entry = Entry->Flink)
    {
        Setting = CONTAINING_RECORD(Entry, USER_POWER_SETTING, Link);
        if (!IsEqualGUID(&Setting->Guid, &pWin32PwrCallout->SettingGuid))
            continue;

        Setting->HasValue = TRUE;
        Setting->ValueLength = pWin32PwrCallout->SettingLength;
        RtlCopyMemory(Setting->Value, pWin32PwrCallout->SettingValue, pWin32PwrCallout->SettingLength);
        IntBroadcastPowerSetting(Setting, NULL, FALSE);
        break;
    }
}

static
VOID
IntFreePowerNotify(
    _In_ PUSER_POWER_NOTIFY Notify)
{
    PUSER_POWER_SETTING Setting = Notify->Setting;

    RemoveEntryList(&Notify->Link);
    ExFreePoolWithTag(Notify, USERTAG_POWER);

    if (Setting != NULL && --Setting->References == 0)
    {
        RemoveEntryList(&Setting->Link);
        if (Setting->Registration != NULL)
            PoUnregisterPowerSettingCallback(Setting->Registration);
        ExFreePoolWithTag(Setting, USERTAG_POWER);
    }
}

/**
 * @brief
 * Handles a power event as a result from an incoming power
 * callout from the kernel power manager.
 *
 * @param[in] pParameters
 * A pointer to the power event parameters containing the
 * power event type and sub-code serving as additional datum
 * for that power event type.
 */
static
VOID
IntHandlePowerEventWorker(
    _In_ PWIN32_POWEREVENT_PARAMETERS pParameters)
{
    PSPOWEREVENTTYPE PwrEventType;
    ULONG Code;

    /* Cache the power event parameters and handle the power callout */
    PwrEventType = pParameters->EventNumber;
    Code = pParameters->Code;
    switch (PwrEventType)
    {
        case PsW32DisplayState:
        {
            gConsoleDisplayState = (MONITOR_DISPLAY_STATE)Code;
            InterlockedExchange(&gConsoleDisplayWakePending, 0);
            IntBroadcastPowerSetting(NULL, NULL, FALSE);
            break;
        }

        case PsW32EventCode:
        {
            ULONG Flags;

            if (!(Code & PSW32_EVENT_CODE_SHUTDOWN))
                break;

            Flags = EWX_SHUTDOWN;
            Flags |= (Code & PSW32_EVENT_CODE_SHUTDOWN_RESTART) ? EWX_REBOOT : EWX_POWEROFF;
            if (Code & PSW32_EVENT_CODE_SHUTDOWN_FORCE)
                Flags |= EWX_FORCE;

            if (hwndSAS == NULL ||
                !UserPostMessage(hwndSAS, WM_LOGONNOTIFY, LN_LOGOFF, (LPARAM)Flags))
            {
                ZwInitiatePowerAction((Code & PSW32_EVENT_CODE_SHUTDOWN_RESTART) ?
                                      PowerActionShutdownReset : PowerActionShutdownOff,
                                      PowerSystemShutdown, 0, TRUE);
            }
            break;
        }

        case PsW32SystemTime:
        {
            /*
             * The power manager of the kernel notified us of an impending
             * time change, broadcast this notification to all present windows.
             */
            UserSendNotifyMessage(HWND_BROADCAST,
                                  WM_TIMECHANGE,
                                  0,
                                  0);
            break;
        }

        default:
        {
            TRACE("Power event of type %d is currently UNIMPLEMENTED (code %lu)\n", PwrEventType, Code);
            break;
        }
    }
}

static
VOID
IntBroadcastPowerMessage(
    _In_ WPARAM Event)
{
    PWINSTATION_OBJECT WinSta;
    PLIST_ENTRY Entry;
    PDESKTOP *Desktops;
    ULONG Count = 0;
    ULONG i, j;
    HWND *List;
    PWND pwnd;
    ULONG_PTR Result;
    ULONGLONG Deadline;
    ULONGLONG Now;

    WinSta = gpDisplaySession ? gpDisplaySession->InputWindowStation : NULL;
    if (WinSta == NULL)
        return;

    for (Entry = WinSta->DesktopListHead.Flink; Entry != &WinSta->DesktopListHead; Entry = Entry->Flink)
        Count++;
    if (Count == 0)
        return;

    Desktops = ExAllocatePoolWithTag(PagedPool, Count * sizeof(PDESKTOP), USERTAG_POWER);
    if (Desktops == NULL)
        return;

    i = 0;
    for (Entry = WinSta->DesktopListHead.Flink; Entry != &WinSta->DesktopListHead; Entry = Entry->Flink)
    {
        Desktops[i] = CONTAINING_RECORD(Entry, DESKTOP, ListEntry);
        ObReferenceObject(Desktops[i]);
        i++;
    }

    Deadline = KeQueryInterruptTime() + 2000ULL * 10000ULL;

    for (i = 0; i < Count; i++)
    {
        if (Desktops[i]->pDeskInfo != NULL && Desktops[i]->pDeskInfo->spwnd != NULL)
        {
            List = IntWinListChildren(Desktops[i]->pDeskInfo->spwnd);
            if (List != NULL)
            {
                for (j = 0; List[j]; j++)
                {
                    pwnd = UserGetWindowObject(List[j]);
                    if (!pwnd ||
                        pwnd->fnid == FNID_MENU ||
                        pwnd->pcls->atomClassName == gpsi->atomSysClass[ICLS_SWITCH])
                    {
                        continue;
                    }

                    Now = KeQueryInterruptTime();
                    if (Now >= Deadline)
                    {
                        UserSendNotifyMessage(List[j], WM_POWERBROADCAST, Event, 0);
                        continue;
                    }

                    co_IntSendMessageTimeout(List[j],
                                             WM_POWERBROADCAST,
                                             Event,
                                             0,
                                             SMTO_ABORTIFHUNG,
                                             (UINT)max((Deadline - Now) / 10000ULL, 1ULL),
                                             &Result);
                }
                ExFreePoolWithTag(List, USERTAG_WINDOWLIST);
            }
        }
        ObDereferenceObject(Desktops[i]);
    }

    ExFreePoolWithTag(Desktops, USERTAG_POWER);
}

static
NTSTATUS
IntHandlePowerStateWorker(
    _In_ PWIN32_POWERSTATE_PARAMETERS pParameters)
{
    ULONG_PTR Result;

    switch (pParameters->PowerStateTask)
    {
        case PowerState_NotifyWL:
        {
            if ((pParameters->Flags & POWER_ACTION_LOCK_CONSOLE) && hwndSAS != NULL)
            {
                co_IntSendMessageTimeout(hwndSAS,
                                         WM_LOGONNOTIFY,
                                         LN_LOCK_WORKSTATION,
                                         0,
                                         SMTO_NOTIMEOUTIFNOTHUNG,
                                         2000,
                                         &Result);
            }
            break;
        }

        case PowerState_SuspendApps:
        {
            IntBroadcastPowerMessage(PBT_APMSUSPEND);
            break;
        }

        case PowerState_MonitorOff:
        {
            gConsoleDisplayState = PowerMonitorOff;
            InterlockedExchange(&gConsoleDisplayWakePending, 0);
            IntBroadcastPowerSetting(NULL, NULL, TRUE);
            break;
        }

        case PowerState_ResumeApps:
        {
            IntBroadcastPowerMessage(PBT_APMRESUMEAUTOMATIC);
            IntBroadcastPowerMessage(PBT_APMRESUMESUSPEND);
            break;
        }

        case PowerState_SuspendServices:
        {
            UserLeave();
            IoPnPDeliverServicePowerNotification(pParameters->SystemAction, PBT_APMSUSPEND, 0, TRUE);
            UserEnterExclusive();
            break;
        }

        case PowerState_ResumeServices:
        {
            IoPnPDeliverServicePowerNotification(pParameters->SystemAction, PBT_APMRESUMEAUTOMATIC, 0, FALSE);
            IoPnPDeliverServicePowerNotification(pParameters->SystemAction, PBT_APMRESUMESUSPEND, 0, FALSE);
            break;
        }

        default:
        {
            break;
        }
    }

    return STATUS_SUCCESS;
}

/**
 * @brief
 * Validates the power event parameters that come from
 * a power callout from the kernel power manager.
 *
 * @param[in] pParameters
 * A pointer to the power event parameters containing the
 * power event type of which is to be validated against
 * valid power events.
 *
 * @return
 * Returns STATUS_INVALID_PARAMETER if the captured power
 * event type is unknown, otherwise STATUS_SUCCESS is returned.
 */
static
NTSTATUS
IntValidateWin32PowerParams(
    _In_ PWIN32_POWEREVENT_PARAMETERS pParameters)
{
    PSPOWEREVENTTYPE PwrEventType;

    /* Capture the event number and check if it is within bounds */
    PwrEventType = pParameters->EventNumber;
    if (PwrEventType < PsW32FullWake || PwrEventType > PsW32MonitorOff)
    {
        TRACE("Unknown event number found -> %d\n", PwrEventType);
        return STATUS_INVALID_PARAMETER;
    }

    return STATUS_SUCCESS;
}

/**
 * @brief
 * Gets the next pending power callout from the global queue
 * list and returns it to the caller. Note that the returned
 * power callout is delisted from the list.
 *
 * @param[in] pPowerCallout
 * A pointer to a power callout entry that was previously returned
 * by the same function. If this parameter is set to NULL the function
 * will return the first callout entry from the list. Otherwise the
 * function will return the next callout entry of the current power
 * callout.
 *
 * @return
 * Returns a pointer to a power callout to the caller. If the list is
 * empty then it will return NULL.
 *
 * @remarks
 * The caller ASSUMES responsibility to lock down the power callout list
 * before it begins enumerating the global list!
 */
static
PWIN32POWERCALLOUT
IntGetNextPowerCallout(
    _In_ PWIN32POWERCALLOUT pPowerCallout)
{
    PLIST_ENTRY Entry;
    PWIN32POWERCALLOUT pPowerCalloutEntry = NULL;

    /* Ensure the current calling thread owns the power callout lock */
    ASSERT_POWER_CALLOUT_LOCK_ACQUIRED();

    /* This list is empty, acknowledge the caller */
    if (IsListEmpty(&gPowerCalloutsQueueList))
    {
        return NULL;
    }

    /* The caller supplied a NULL argument, give them the first entry */
    if (!pPowerCallout)
    {
        Entry = gPowerCalloutsQueueList.Flink;
    }
    else
    {
        /* Otherwise give the caller the next power callout entry from the list */
        Entry = pPowerCallout->Link.Flink;
    }

    /* Delist the power callout entry from the list and give it to caller */
    pPowerCalloutEntry = CONTAINING_RECORD(Entry, WIN32POWERCALLOUT, Link);
    RemoveEntryList(&pPowerCalloutEntry->Link);
    return pPowerCalloutEntry;
}

/**
 * @brief
 * Deploys all pending power callouts to appropriate power
 * callout workers.
 */
static
VOID
IntDeployPowerCallout(VOID)
{
    PWIN32POWERCALLOUT pWin32PwrCallout;

    /* Lock the entire USER subsystem down as we do particular stuff */
    UserEnterExclusive();

    /*
     * FIXME: While we did indeed lock the USER subsystem, there is a risk
     * of the current calling thread might die or hang, so we should probably
     * lock the thread while we deploy our power callout. The thread info
     * provides a thread lock field for this purpose (see the ptl member from
     * the _THREADINFO structure) but ReactOS lacks implementation to handle
     * this. Suppose a thread happens to get into this fate, the power callout
     * would never get signaled...
     */

    /* Deploy all the pending power callouts to the appropriate callout workers */
    for (;;)
    {
        IntAcquirePowerCalloutLock();
        pWin32PwrCallout = IntGetNextPowerCallout(NULL);
        IntReleasePowerCalloutLock();
        if (pWin32PwrCallout == NULL)
            break;

        if (pWin32PwrCallout->Type == POWER_CALLOUT_EVENT)
        {
            IntHandlePowerEventWorker(&pWin32PwrCallout->Params);

            /* We are done with this power callout */
            ExFreePoolWithTag(pWin32PwrCallout, USERTAG_POWER);
        }
        else if (pWin32PwrCallout->Type == POWER_CALLOUT_SETTING)
        {
            IntHandlePowerSettingWorker(pWin32PwrCallout);
            ExFreePoolWithTag(pWin32PwrCallout, USERTAG_POWER);
        }
        else // POWER_CALLOUT_STATE
        {
            pWin32PwrCallout->Status = IntHandlePowerStateWorker(&pWin32PwrCallout->StateParams);
            KeSetEvent(&pWin32PwrCallout->CompletionEvent, IO_NO_INCREMENT, FALSE);
        }
    }

    UserLeave();
}

/**
 * @brief
 * Enlists a newly allocated power callout into the queue list
 * for later processing.
 *
 * @param[in] pPowerCallout
 * A pointer to a power callout that is to be inserted into the
 * queue list.
 */
static
VOID
IntEnlistPowerCallout(
    _In_ PWIN32POWERCALLOUT pPowerCallout)
{
    PETHREAD CurrentThread;

    /* Enlist it to the queue already */
    IntAcquirePowerCalloutLock();
    InsertTailList(&gPowerCalloutsQueueList, &pPowerCallout->Link);
    IntReleasePowerCalloutLock();

    /*
     * We have to let CSRSS process this power callout if one of the
     * following conditions is TRUE for the current calling thread:
     *
     * - The process of the calling thread is a system process;
     * - The process of the calling thread is attached;
     * - The current calling thread is not a Win32 thread.
     *
     * For the second point, we cannot process the power callout ourselves
     * as we must lock down USER exclusively for our own purpose, which requires
     * us to be in a critical section. So we do not want to fiddle with a process
     * that is attached with others.
     */
    CurrentThread = PsGetCurrentThread();
    if (PsIsSystemThread(CurrentThread) ||
        KeIsAttachedProcess() ||
        !IntIsThreadWin32Thread(CurrentThread) ||
        ExGetPreviousMode() != UserMode)
    {
        /* Alert CSRSS of the presence of an enqueued power callout */
        KeSetEvent(gpPowerRequestCalloutEvent, EVENT_INCREMENT, FALSE);
        return;
    }

    /* Handle this power callout ourselves */
    IntDeployPowerCallout();
}

/* PUBLIC FUNCTIONS **********************************************************/

/**
 * @brief
 * Initializes the power management side of Win32 kernel-mode
 * subsystem component. This enables communication between
 * the power manager of the NT kernel and Win32k.
 *
 * @param[in] hPowerRequestEvent
 * A handle to the global power request event, provided by the
 * Winsrv module. This allows CSRSS to be notified of power callouts
 * that cannot be handled by Win32k.
 *
 * @return
 * Returns STATUS_INSUFFICIENT_RESOURCES if pool allocation for the
 * power callout lock has failed due to lack of necessary memory.
 * A failure NTSTATUS code is returned if the power request event
 * could not be referenced. Otherwise STATUS_SUCCESS is returned to
 * indicate the power management has initialized successfully.
 */
NTSTATUS
NTAPI
IntInitWin32PowerManagement(
    _In_ HANDLE hPowerRequestEvent)
{
    NTSTATUS Status;

    /* Allocate memory pool for the power callout mutex */
    gpPowerCalloutMutexLock = ExAllocatePoolZero(NonPagedPool,
                                                 sizeof(FAST_MUTEX),
                                                 USERTAG_POWER);
    if (gpPowerCalloutMutexLock == NULL)
    {
        ERR("Failed to allocate pool of memory for the power callout mutex\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* Initialize the mutex and owner thread */
    ExInitializeFastMutex(gpPowerCalloutMutexLock);
    gpPowerCalloutMutexOwnerThread = NULL;

    /* Initialize the global queue list and the power callout (aka request) event object */
    InitializeListHead(&gPowerCalloutsQueueList);
    Status = ObReferenceObjectByHandle(hPowerRequestEvent,
                                       EVENT_ALL_ACCESS,
                                       *ExEventObjectType,
                                       KernelMode,
                                       (PVOID *)&gpPowerRequestCalloutEvent,
                                       NULL);
    if (!NT_SUCCESS(Status))
    {
        ERR("Failed to reference the power callout event handle (Status 0x%08lx)\n", Status);
        ExFreePoolWithTag(gpPowerCalloutMutexLock, USERTAG_POWER);
        return Status;
    }

    return STATUS_SUCCESS;
}

/**
 * @brief
 * Cleanup procedure that frees all the allocated resources by
 * the power manager. It is triggered during Win32k subsystem unloading.
 */
NTSTATUS
NTAPI
IntWin32PowerManagementCleanup(VOID)
{
    PWIN32POWERCALLOUT pWin32PwrCallout;

    /* Dereference the power request event */
    ObDereferenceObject(gpPowerRequestCalloutEvent);
    gpPowerRequestCalloutEvent = NULL;

    /*
     * Enumerate all pending power callouts and free them. We do not
     * need to do this with the lock held as the CSR process is tore
     * apart during Win32k cleanup, so future power callouts would not
     * be allowed anyway, therefore we are safe.
     */
    while ((pWin32PwrCallout = IntGetNextPowerCallout(NULL)) != NULL)
    {
        if (pWin32PwrCallout->Type == POWER_CALLOUT_STATE)
        {
            pWin32PwrCallout->Status = STATUS_UNSUCCESSFUL;
            KeSetEvent(&pWin32PwrCallout->CompletionEvent, IO_NO_INCREMENT, FALSE);
            continue;
        }

        ExFreePoolWithTag(pWin32PwrCallout, USERTAG_POWER);
    }

    /* Tear apart the power callout lock mutex */
    ExFreePoolWithTag(gpPowerCalloutMutexLock, USERTAG_POWER);
    gpPowerCalloutMutexLock = NULL;
    return STATUS_SUCCESS;
}

/**
 * @brief
 * Handles an incoming power event callout from the NT power
 * manager.
 *
 * @param[in] pWin32PwrEventParams
 * A pointer to power event parameters that is given by the
 * NT power manager of the kernel.
 *
 * @return
 * Returns STATUS_UNSUCCESSFUL if the Client/Server subsystem is
 * not running, of which power callouts cannot be handled.
 * Returns STATUS_INVALID_PARAMETER if the provided power event
 * parameters are not valid. Returns STATUS_INSUFFICIENT_RESOURCES
 * if there is a lack of memory to allocate for the power callout.
 * Otherwise it returns STATUS_SUCCESS to indicate the power callout
 * was handled successfully.
 */
NTSTATUS
NTAPI
IntHandlePowerEvent(
    _In_ PWIN32_POWEREVENT_PARAMETERS pWin32PwrEventParams)
{
    PWIN32POWERCALLOUT pWin32PwrCallout;
    NTSTATUS Status;

    /*
     * CSRSS is not running. As a consequence, the USER subsystem is neither
     * up and running as the Client/Server subsystem is responsible to fire
     * up other subsystems. Another case is that the system undergoes shutdown
     * and Win32k cleanup is currently in effect. Either way, just quit.
     */
    if (!gpDisplaySession || !gpDisplaySession->CsrProcess)
    {
        TRACE("CSRSS is not running, bailing out\n");
        return STATUS_UNSUCCESSFUL;
    }

    /* Validate the power event parameters, just to be sure we have not gotten anything else */
    Status = IntValidateWin32PowerParams(pWin32PwrEventParams);
    if (!NT_SUCCESS(Status))
    {
        ERR("Could not deploy power callout, invalid Win32 power parameters!\n");
        return Status;
    }

    /* Allocate pool of memory for this power callout */
    pWin32PwrCallout = ExAllocatePoolZero(NonPagedPool,
                                          sizeof(WIN32POWERCALLOUT),
                                          USERTAG_POWER);
    if (pWin32PwrCallout == NULL)
    {
        ERR("Allocating memory for Win32 power callout failed\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* Fill the necessary power datum */
    pWin32PwrCallout->Type = POWER_CALLOUT_EVENT;
    pWin32PwrCallout->Params.EventNumber = pWin32PwrEventParams->EventNumber;
    pWin32PwrCallout->Params.Code = pWin32PwrEventParams->Code;

    /* Enqueue this power request for later processing */
    IntEnlistPowerCallout(pWin32PwrCallout);
    return STATUS_SUCCESS;
}

/**
 * @brief
 * Handles an incoming power state callout from the NT power
 * manager.
 *
 * @param[in] pWin32PwrStateParams
 * A pointer to power state parameters that is given by the
 * NT power manager of the kernel.
 */
NTSTATUS
NTAPI
IntHandlePowerState(
    _In_ PWIN32_POWERSTATE_PARAMETERS pWin32PwrStateParams)
{
    PWIN32POWERCALLOUT pWin32PwrCallout;
    NTSTATUS Status;

    if (!gpDisplaySession || !gpDisplaySession->CsrProcess)
    {
        TRACE("CSRSS is not running, bailing out\n");
        return STATUS_UNSUCCESSFUL;
    }

    pWin32PwrCallout = ExAllocatePoolZero(NonPagedPool,
                                          sizeof(WIN32POWERCALLOUT),
                                          USERTAG_POWER);
    if (pWin32PwrCallout == NULL)
    {
        ERR("Allocating memory for Win32 power callout failed\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    pWin32PwrCallout->Type = POWER_CALLOUT_STATE;
    pWin32PwrCallout->StateParams = *pWin32PwrStateParams;
    pWin32PwrCallout->Status = STATUS_PENDING;
    KeInitializeEvent(&pWin32PwrCallout->CompletionEvent, NotificationEvent, FALSE);

    IntEnlistPowerCallout(pWin32PwrCallout);
    KeWaitForSingleObject(&pWin32PwrCallout->CompletionEvent, Executive, KernelMode, FALSE, NULL);

    Status = pWin32PwrCallout->Status;
    ExFreePoolWithTag(pWin32PwrCallout, USERTAG_POWER);
    return Status;
}

VOID
NTAPI
IntUserPowerCalloutWorker(VOID)
{
    if (!UserIsCsrProcess(PsGetCurrentProcess()))
        return;

    IntDeployPowerCallout();
}

ULONG_PTR
FASTCALL
UserRegisterPowerSettingNotification(
    _In_ HANDLE Recipient,
    _In_ LPCGUID UserGuid,
    _In_ BOOLEAN Service)
{
    PUSER_POWER_SETTING Setting = NULL;
    PUSER_POWER_NOTIFY Notify;
    PLIST_ENTRY Entry;
    GUID Guid;
    ULONG Value;
    NTSTATUS Status;

    _SEH2_TRY
    {
        ProbeForRead(UserGuid, sizeof(GUID), sizeof(UCHAR));
        Guid = *UserGuid;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        SetLastNtError(_SEH2_GetExceptionCode());
        _SEH2_YIELD(return 0);
    }
    _SEH2_END;

    if (Service && IsEqualGUID(&Guid, &UserGuidSessionDisplayStatus))
    {
        EngSetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }

    Notify = ExAllocatePoolZero(PagedPool, sizeof(USER_POWER_NOTIFY), USERTAG_POWER);
    if (Notify == NULL)
    {
        EngSetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return 0;
    }

    if (!IntGetDisplaySettingValue(&Guid, &Value))
    {
        for (Entry = gPowerSettingList.Flink; Entry != &gPowerSettingList; Entry = Entry->Flink)
        {
            Setting = CONTAINING_RECORD(Entry, USER_POWER_SETTING, Link);
            if (IsEqualGUID(&Setting->Guid, &Guid))
                break;
            Setting = NULL;
        }

        if (Setting != NULL)
        {
            Setting->References++;
        }
        else
        {
            Setting = ExAllocatePoolZero(PagedPool, sizeof(USER_POWER_SETTING), USERTAG_POWER);
            if (Setting == NULL)
            {
                ExFreePoolWithTag(Notify, USERTAG_POWER);
                EngSetLastError(ERROR_NOT_ENOUGH_MEMORY);
                return 0;
            }

            Setting->Guid = Guid;
            Setting->References = 1;
            InsertTailList(&gPowerSettingList, &Setting->Link);
            Status = PoRegisterPowerSettingCallback(NULL, &Setting->Guid, IntPowerSettingCallback, NULL, &Setting->Registration);
            if (!NT_SUCCESS(Status))
                Setting->Registration = NULL;
        }
    }

    if (++gPowerNotifyCookie == 0)
        gPowerNotifyCookie = 1;

    Notify->ppi = PsGetCurrentProcessWin32Process();
    Notify->Recipient = Recipient;
    Notify->Service = Service;
    Notify->Guid = Guid;
    Notify->Cookie = gPowerNotifyCookie;
    Notify->Setting = Setting;
    InsertTailList(&gPowerNotifyList, &Notify->Link);

    if (Setting == NULL || Setting->HasValue)
        IntQueuePowerSettingCallout(&Guid, Notify->Cookie, NULL, 0);

    return Notify->Cookie;
}

BOOL
FASTCALL
UserUnregisterPowerSettingNotification(
    _In_ ULONG Cookie)
{
    PPROCESSINFO ppi = PsGetCurrentProcessWin32Process();
    PUSER_POWER_NOTIFY Notify;
    PLIST_ENTRY Entry;

    for (Entry = gPowerNotifyList.Flink; Entry != &gPowerNotifyList; Entry = Entry->Flink)
    {
        Notify = CONTAINING_RECORD(Entry, USER_POWER_NOTIFY, Link);
        if (Notify->Cookie == Cookie && Notify->ppi == ppi)
        {
            IntFreePowerNotify(Notify);
            return TRUE;
        }
    }

    EngSetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
}

VOID
FASTCALL
UserCleanupPowerSettingNotifications(
    _In_ PPROCESSINFO ppi)
{
    PUSER_POWER_NOTIFY Notify;
    PLIST_ENTRY Entry;

    Entry = gPowerNotifyList.Flink;
    while (Entry != &gPowerNotifyList)
    {
        Notify = CONTAINING_RECORD(Entry, USER_POWER_NOTIFY, Link);
        Entry = Entry->Flink;
        if (Notify->ppi == ppi)
            IntFreePowerNotify(Notify);
    }
}

VOID
FASTCALL
UserRequestConsoleDisplayState(
    _In_ MONITOR_DISPLAY_STATE State)
{
    ULONG Value = State;
    NTSTATUS Status;

    Status = ZwPowerInformation(SessionDisplayState, &Value, sizeof(Value), NULL, 0);
    if (!NT_SUCCESS(Status))
        ERR("Console display state %lu not requested: 0x%08lx\n", Value, Status);
}

VOID
FASTCALL
UserConsoleDisplayInput(VOID)
{
    if (gConsoleDisplayState == PowerMonitorOff &&
        InterlockedExchange(&gConsoleDisplayWakePending, 1) == 0)
    {
        UserRequestConsoleDisplayState(PowerMonitorOn);
    }
}

/* EOF */
