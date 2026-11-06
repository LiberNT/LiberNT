/*
 * COPYRIGHT:        See COPYING in the top level directory
 * PROJECT:          ReactOS Win32k subsystem
 * PURPOSE:          General input functions
 * FILE:             win32ss/user/ntuser/input.c
 * PROGRAMERS:       Casper S. Hornstrup (chorns@users.sourceforge.net)
 *                   Rafal Harabien (rafalh@reactos.org)
 */

#include <win32k.h>
DBG_DEFAULT_CHANNEL(UserInput);

/* LastRITEventTickCount update cycle has been adjusted from 60s to 1s since NT6 */
#if (NTDDI_VERSION < NTDDI_VISTA)
#define LAST_RIT_EVENT_UPDATE_INTERVAL 60000UL
#else
#define LAST_RIT_EVENT_UPDATE_INTERVAL 1000UL
#endif

#define MOUSE_INPUT_BUFFER_SIZE 64

/* GLOBALS *******************************************************************/

PTHREADINFO ptiRawInput;
PKTIMER MasterTimer = NULL;
PATTACHINFO gpai = NULL;
INT paiCount = 0;

static DWORD LastInputTick = 0;
static DWORD ScreenSaverRequestTick = 0;
static BOOL ScreenSaverRequested = FALSE;

/* FUNCTIONS *****************************************************************/

/*
 * IntLastInputTick
 *
 * Updates or gets last input tick count
 */
static DWORD FASTCALL
IntLastInputTick(BOOL bUpdate)
{
    if (bUpdate)
    {
        LastInputTick = EngGetTickCount32();
        UserConsoleDisplayInput();
        if (gpsi)
        {
            gpsi->dwLastRITEventTickCount = LastInputTick;
            if (gpsi->dwLastRITEventTickCount - gpsi->dwLastSystemRITEventTickCountUpdate >
                LAST_RIT_EVENT_UPDATE_INTERVAL)
            {
                MmWriteableSharedUserData->LastSystemRITEventTickCount = LastInputTick;
                gpsi->dwLastSystemRITEventTickCountUpdate = LastInputTick;
            }
        }
    }
    return LastInputTick;
}

/*
 * DoTheScreenSaver
 *
 * Check if screensaver should be started and sends message to SAS window
 */
VOID FASTCALL
DoTheScreenSaver(VOID)
{
    DWORD Test, TO;

    if (gspv.iScrSaverTimeout > 0 && gspv.bScrSaverActive) // Zero means Off.
    {
        Test = EngGetTickCount32();
        Test = Test - LastInputTick;
        TO = 1000 * gspv.iScrSaverTimeout;
        if (Test > TO)
        {
            TRACE("Screensaver Message Start! Tick %lu Timeout %d \n", Test, gspv.iScrSaverTimeout);

            if (ppiScrnSaver) // We are or we are not the screensaver, prevent reentry...
            {
                if (!(ppiScrnSaver->W32PF_flags & W32PF_IDLESCREENSAVER))
                {
                    ppiScrnSaver->W32PF_flags |= W32PF_IDLESCREENSAVER;
                    ERR("Screensaver is Idle\n");
                }
            }
            else if (!ScreenSaverRequested || ScreenSaverRequestTick != LastInputTick)
            {
                PUSER_MESSAGE_QUEUE ForegroundQueue = IntGetFocusMessageQueue();
                ScreenSaverRequested = TRUE;
                ScreenSaverRequestTick = LastInputTick;
                if (ForegroundQueue && ForegroundQueue->spwndActive)
                    UserPostMessage(hwndSAS, WM_LOGONNOTIFY, LN_START_SCREENSAVE, 1); // lParam 1 == Secure
                else
                    UserPostMessage(hwndSAS, WM_LOGONNOTIFY, LN_START_SCREENSAVE, 0);
            }
        }
    }
}

static const GUID InputKeyboardInterface =
    { 0x884b96c3, 0x56ef, 0x11d1, { 0xbc, 0x8c, 0x00, 0xa0, 0xc9, 0x14, 0x05, 0xdd } };
static const GUID InputMouseInterface =
    { 0x378de44c, 0x56ef, 0x11d1, { 0xbc, 0x8c, 0x00, 0xa0, 0xc9, 0x14, 0x05, 0xdd } };
static const GUID InputInterfaceArrival =
    { 0xCB3A4004, 0x46F0, 0x11D0, { 0xB0, 0x8F, 0x00, 0x60, 0x97, 0x13, 0x05, 0x3F } };

#define INPUT_MAX_DEVICES 16

typedef struct _INPUT_DEVICE
{
    HANDLE Handle;
    PFILE_OBJECT FileObject;
    IO_STATUS_BLOCK Iosb;
    BOOLEAN Keyboard;
    BOOLEAN ReadPending;
    BOOLEAN ReadDone;
    UNICODE_STRING Path;
    union
    {
        KEYBOARD_INPUT_DATA Keyboard;
        MOUSE_INPUT_DATA Mouse[MOUSE_INPUT_BUFFER_SIZE];
    } Data;
} INPUT_DEVICE, *PINPUT_DEVICE;

typedef struct _INPUT_ARRIVAL
{
    LIST_ENTRY ListEntry;
    BOOLEAN Keyboard;
    UNICODE_STRING Path;
} INPUT_ARRIVAL, *PINPUT_ARRIVAL;

typedef struct _INPUT_DEVICE_STATE
{
    KEVENT ArrivalEvent;
    FAST_MUTEX ArrivalLock;
    LIST_ENTRY Arrivals;
    PVOID KeyboardNotification;
    PVOID MouseNotification;
    PINPUT_DEVICE Devices[INPUT_MAX_DEVICES];
} INPUT_DEVICE_STATE, *PINPUT_DEVICE_STATE;

static PDRIVER_OBJECT gInputDriverObject;
static PINPUT_DEVICE_STATE gInputDevices;
static BOOL gKeyboardConnected;

static
NTSTATUS
NTAPI
InputQueueArrival(
    _In_ PINPUT_DEVICE_STATE State,
    _In_ PCUNICODE_STRING Path,
    _In_ BOOLEAN Keyboard)
{
    PINPUT_ARRIVAL Arrival;

    Arrival = ExAllocatePoolWithTag(PagedPool, sizeof(*Arrival) + Path->Length, USERTAG_DEVICEINFO);
    if (!Arrival)
        return STATUS_INSUFFICIENT_RESOURCES;

    Arrival->Keyboard = Keyboard;
    Arrival->Path.Buffer = (PWCH)(Arrival + 1);
    Arrival->Path.Length = Path->Length;
    Arrival->Path.MaximumLength = Path->Length;
    RtlCopyMemory(Arrival->Path.Buffer, Path->Buffer, Path->Length);

    ExAcquireFastMutex(&State->ArrivalLock);
    InsertTailList(&State->Arrivals, &Arrival->ListEntry);
    ExReleaseFastMutex(&State->ArrivalLock);
    KeSetEvent(&State->ArrivalEvent, IO_NO_INCREMENT, FALSE);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
InputInterfaceNotification(
    _In_ PVOID NotificationStructure,
    _Inout_opt_ PVOID Context)
{
    PDEVICE_INTERFACE_CHANGE_NOTIFICATION Notification = NotificationStructure;

    if (Context && IsEqualGUID(&Notification->Event, &InputInterfaceArrival))
    {
        InputQueueArrival(Context, Notification->SymbolicLinkName,
                          IsEqualGUID(&Notification->InterfaceClassGuid, &InputKeyboardInterface));
    }
    return STATUS_SUCCESS;
}

static
VOID
InputDevicesStart(VOID)
{
    static const UNICODE_STRING LegacyKeyboard = RTL_CONSTANT_STRING(L"\\Device\\KeyboardClass0");
    static const UNICODE_STRING LegacyMouse = RTL_CONSTANT_STRING(L"\\Device\\PointerClass0");
    PINPUT_DEVICE_STATE State;
    NTSTATUS Status;

    State = ExAllocatePoolZero(NonPagedPool, sizeof(*State), USERTAG_DEVICEINFO);
    if (!State)
        return;

    KeInitializeEvent(&State->ArrivalEvent, SynchronizationEvent, FALSE);
    ExInitializeFastMutex(&State->ArrivalLock);
    InitializeListHead(&State->Arrivals);
    gInputDevices = State;

    Status = IoRegisterPlugPlayNotification(EventCategoryDeviceInterfaceChange,
                                            PNPNOTIFY_DEVICE_INTERFACE_INCLUDE_EXISTING_INTERFACES,
                                            (PVOID)&InputKeyboardInterface,
                                            gInputDriverObject,
                                            InputInterfaceNotification,
                                            State,
                                            &State->KeyboardNotification);
    if (!NT_SUCCESS(Status))
    {
        ERR("Keyboard interface notification not registered: 0x%08lx\n", Status);
        State->KeyboardNotification = NULL;
        InputQueueArrival(State, &LegacyKeyboard, TRUE);
    }

    Status = IoRegisterPlugPlayNotification(EventCategoryDeviceInterfaceChange,
                                            PNPNOTIFY_DEVICE_INTERFACE_INCLUDE_EXISTING_INTERFACES,
                                            (PVOID)&InputMouseInterface,
                                            gInputDriverObject,
                                            InputInterfaceNotification,
                                            State,
                                            &State->MouseNotification);
    if (!NT_SUCCESS(Status))
    {
        ERR("Mouse interface notification not registered: 0x%08lx\n", Status);
        State->MouseNotification = NULL;
        InputQueueArrival(State, &LegacyMouse, FALSE);
    }
}

static
VOID
InputDeviceClose(
    _In_ PINPUT_DEVICE_STATE State,
    _In_ ULONG Index)
{
    PINPUT_DEVICE Device = State->Devices[Index];
    IO_STATUS_BLOCK Iosb;

    UserEnterExclusive();
    State->Devices[Index] = NULL;
    UserLeave();

    if (Device->ReadPending)
    {
        (void)ZwCancelIoFile(Device->Handle, &Iosb);
        KeWaitForSingleObject(&Device->FileObject->Event, Executive, KernelMode, FALSE, NULL);
    }
    ObDereferenceObject(Device->FileObject);
    ObCloseHandle(Device->Handle, KernelMode);
    ExFreePoolWithTag(Device, USERTAG_DEVICEINFO);
}

static
VOID
InputDevicesStop(VOID)
{
    PINPUT_DEVICE_STATE State = gInputDevices;
    PLIST_ENTRY Entry;
    ULONG Index;

    if (!State)
        return;

    if (State->KeyboardNotification)
        IoUnregisterPlugPlayNotification(State->KeyboardNotification);
    if (State->MouseNotification)
        IoUnregisterPlugPlayNotification(State->MouseNotification);

    for (Index = 0; Index < INPUT_MAX_DEVICES; Index++)
    {
        if (State->Devices[Index])
            InputDeviceClose(State, Index);
    }

    while (!IsListEmpty(&State->Arrivals))
    {
        Entry = RemoveHeadList(&State->Arrivals);
        ExFreePoolWithTag(CONTAINING_RECORD(Entry, INPUT_ARRIVAL, ListEntry), USERTAG_DEVICEINFO);
    }

    gInputDevices = NULL;
    ExFreePoolWithTag(State, USERTAG_DEVICEINFO);
}

static
NTSTATUS
InputDeviceControl(
    _In_ PINPUT_DEVICE Device,
    _In_ ULONG IoControlCode,
    _In_reads_bytes_(InputLength) PVOID Input,
    _In_ ULONG InputLength)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK Block;
    HANDLE Handle;
    NTSTATUS Status;

    InitializeObjectAttributes(&ObjectAttributes, &Device->Path, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = ZwOpenFile(&Handle, SYNCHRONIZE | FILE_READ_DATA, &ObjectAttributes, &Block,
                        FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_SYNCHRONOUS_IO_NONALERT);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = ZwDeviceIoControlFile(Handle, NULL, NULL, NULL, &Block, IoControlCode, Input, InputLength, NULL, 0);
    ZwClose(Handle);
    return Status;
}

/* Callers hold the USER lock or run on the raw input thread, which owns the device table. */
BOOL
NTAPI
UserKeyboardPresent(VOID)
{
    PINPUT_DEVICE_STATE State = gInputDevices;
    ULONG Index;

    for (Index = 0; State && Index < INPUT_MAX_DEVICES; Index++)
    {
        if (State->Devices[Index] && State->Devices[Index]->Keyboard)
            return TRUE;
    }
    return FALSE;
}

VOID
NTAPI
UserSendKeyboardControl(
    _In_ ULONG IoControlCode,
    _In_reads_bytes_(InputLength) PVOID Input,
    _In_ ULONG InputLength)
{
    PINPUT_DEVICE_STATE State = gInputDevices;
    NTSTATUS Status;
    ULONG Index;

    for (Index = 0; State && Index < INPUT_MAX_DEVICES; Index++)
    {
        if (!State->Devices[Index] || !State->Devices[Index]->Keyboard)
            continue;
        Status = InputDeviceControl(State->Devices[Index], IoControlCode, Input, InputLength);
        if (!NT_SUCCESS(Status))
            WARN("Keyboard control 0x%08lx failed on %wZ: 0x%08lx\n", IoControlCode, &State->Devices[Index]->Path, Status);
    }
}

static
VOID
InputKeyboardConnected(
    _In_ PINPUT_DEVICE Device)
{
    if (gKeyboardConnected)
    {
        UserEnterShared();
        UserRefreshKeyboardIndicators();
        UserLeave();
        UserSetKeyboardTypematic();
        return;
    }

    gKeyboardConnected = TRUE;
    TRACE("Keyboard connected!\n");
    UserInitKeyboard(Device->Handle);
    UserEnterExclusive();
    UserRegisterHotKey(PWND_BOTTOM, IDHK_WINKEY, MOD_WIN, 0);
    UserRegisterHotKey(PWND_BOTTOM, IDHK_SNAP_LEFT, MOD_WIN, VK_LEFT);
    UserRegisterHotKey(PWND_BOTTOM, IDHK_SNAP_RIGHT, MOD_WIN, VK_RIGHT);
    UserRegisterHotKey(PWND_BOTTOM, IDHK_SNAP_UP, MOD_WIN, VK_UP);
    UserRegisterHotKey(PWND_BOTTOM, IDHK_SNAP_DOWN, MOD_WIN, VK_DOWN);
    SetDebugHotKeys();
    UserLeave();
}

static
VOID
InputDeviceAttach(
    _In_ PINPUT_DEVICE_STATE State,
    _In_ PINPUT_ARRIVAL Arrival)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK Iosb;
    PINPUT_DEVICE Device;
    ULONG Index, Free = INPUT_MAX_DEVICES;
    NTSTATUS Status;

    for (Index = 0; Index < INPUT_MAX_DEVICES; Index++)
    {
        if (!State->Devices[Index])
        {
            if (Free == INPUT_MAX_DEVICES)
                Free = Index;
        }
        else if (RtlEqualUnicodeString(&State->Devices[Index]->Path, &Arrival->Path, TRUE))
        {
            return;
        }
    }
    if (Free == INPUT_MAX_DEVICES)
    {
        ERR("No room for input device %wZ\n", &Arrival->Path);
        return;
    }

    Device = ExAllocatePoolZero(NonPagedPool, sizeof(*Device) + Arrival->Path.Length, USERTAG_DEVICEINFO);
    if (!Device)
        return;
    Device->Keyboard = Arrival->Keyboard;
    Device->Path.Buffer = (PWCH)(Device + 1);
    Device->Path.Length = Arrival->Path.Length;
    Device->Path.MaximumLength = Arrival->Path.Length;
    RtlCopyMemory(Device->Path.Buffer, Arrival->Path.Buffer, Arrival->Path.Length);

    InitializeObjectAttributes(&ObjectAttributes, &Device->Path, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = ZwOpenFile(&Device->Handle, FILE_ALL_ACCESS, &ObjectAttributes, &Iosb, 0, 0);
    if (NT_SUCCESS(Status))
    {
        Status = ObReferenceObjectByHandle(Device->Handle, SYNCHRONIZE, NULL, KernelMode, (PVOID *)&Device->FileObject, NULL);
        if (!NT_SUCCESS(Status))
            ObCloseHandle(Device->Handle, KernelMode);
    }
    if (!NT_SUCCESS(Status))
    {
        WARN("Opening input device %wZ failed: 0x%08lx\n", &Arrival->Path, Status);
        ExFreePoolWithTag(Device, USERTAG_DEVICEINFO);
        return;
    }

    UserEnterExclusive();
    State->Devices[Free] = Device;
    UserLeave();

    if (Device->Keyboard)
        InputKeyboardConnected(Device);
    else
        RawInputSetMouseAttributes(Device->Handle);
    RawInputUpdateDevices();
}

static
VOID
InputDevicesAttachArrivals(
    _In_ PINPUT_DEVICE_STATE State)
{
    PLIST_ENTRY Entry;

    for (;;)
    {
        ExAcquireFastMutex(&State->ArrivalLock);
        Entry = IsListEmpty(&State->Arrivals) ? NULL : RemoveHeadList(&State->Arrivals);
        ExReleaseFastMutex(&State->ArrivalLock);
        if (!Entry)
            break;
        InputDeviceAttach(State, CONTAINING_RECORD(Entry, INPUT_ARRIVAL, ListEntry));
        ExFreePoolWithTag(CONTAINING_RECORD(Entry, INPUT_ARRIVAL, ListEntry), USERTAG_DEVICEINFO);
    }
}

static
ULONG
InputDevicesPrepareWait(
    _Inout_ PVOID *WaitObjects,
    _Inout_ PULONG WaitCount)
{
    PINPUT_DEVICE_STATE State = gInputDevices;
    LARGE_INTEGER ByteOffset;
    PINPUT_DEVICE Device;
    ULONG Index, Expected = 0;
    NTSTATUS Status;

    if (!State)
        return 0;

    InputDevicesAttachArrivals(State);
    WaitObjects[(*WaitCount)++] = &State->ArrivalEvent;
    Expected++;

    ByteOffset.QuadPart = 0;
    for (Index = 0; Index < INPUT_MAX_DEVICES; Index++)
    {
        Device = State->Devices[Index];
        if (!Device)
            continue;
        Expected++;
        if (!Device->ReadPending && !Device->ReadDone)
        {
            Status = ZwReadFile(Device->Handle,
                                NULL,
                                NULL,
                                NULL,
                                &Device->Iosb,
                                &Device->Data,
                                Device->Keyboard ? sizeof(KEYBOARD_INPUT_DATA) : sizeof(Device->Data.Mouse),
                                &ByteOffset,
                                NULL);
            if (Status == STATUS_PENDING)
            {
                Device->ReadPending = TRUE;
            }
            else
            {
                Device->Iosb.Status = Status;
                Device->ReadDone = TRUE;
            }
        }
        if (Device->ReadPending)
            WaitObjects[(*WaitCount)++] = &Device->FileObject->Event;
    }
    return Expected;
}

static
BOOLEAN
InputDevicesSignaled(
    _In_ PVOID Object)
{
    PINPUT_DEVICE_STATE State = gInputDevices;
    PINPUT_DEVICE Device;
    ULONG Index;

    if (!State)
        return FALSE;
    if (Object == &State->ArrivalEvent)
        return TRUE;

    for (Index = 0; Index < INPUT_MAX_DEVICES; Index++)
    {
        Device = State->Devices[Index];
        if (Device && Device->ReadPending && Object == &Device->FileObject->Event)
        {
            Device->ReadPending = FALSE;
            Device->ReadDone = TRUE;
            return TRUE;
        }
    }
    return FALSE;
}

static
VOID
InputProcessMouse(
    _In_ PINPUT_DEVICE Device)
{
    ULONG MouseInputCount, i;
    BOOL MouseInputProcessed = FALSE;

    if ((Device->Iosb.Information > sizeof(Device->Data.Mouse)) || (Device->Iosb.Information % sizeof(MOUSE_INPUT_DATA)))
    {
        ERR("Mouse returned an invalid byte count: %Iu.\n", Device->Iosb.Information);
        return;
    }

    MouseInputCount = (ULONG)(Device->Iosb.Information / sizeof(MOUSE_INPUT_DATA));
    for (i = 0; i < MouseInputCount; ++i)
    {
        PMOUSE_INPUT_DATA MouseInput = &Device->Data.Mouse[i];

        if (!MouseInput->Flags &&
            !MouseInput->ButtonFlags &&
            !MouseInput->ButtonData &&
            !MouseInput->LastX &&
            !MouseInput->LastY)
            continue;

        if (!MouseInputProcessed)
        {
            IntLastInputTick(TRUE);
            UserEnterExclusive();
            RawInputSetSourceDevice(&Device->Path);
            MouseInputProcessed = TRUE;
        }

        UserProcessMouseInput(MouseInput);
    }

    if (MouseInputProcessed)
    {
        RawInputSetSourceDevice(NULL);
        UserLeave();
    }
}

static
VOID
InputDevicesProcess(VOID)
{
    PINPUT_DEVICE_STATE State = gInputDevices;
    PINPUT_DEVICE Device;
    ULONG Index;

    for (Index = 0; State && Index < INPUT_MAX_DEVICES; Index++)
    {
        Device = State->Devices[Index];
        if (!Device || !Device->ReadDone)
            continue;
        Device->ReadDone = FALSE;

        if (!NT_SUCCESS(Device->Iosb.Status))
        {
            WARN("Input device %wZ stopped: 0x%08lx\n", &Device->Path, Device->Iosb.Status);
            InputDeviceClose(State, Index);
            continue;
        }

        if (!Device->Keyboard)
        {
            TRACE("MouseEvent\n");
            InputProcessMouse(Device);
            continue;
        }

        TRACE("KeyboardEvent: %s %04x\n",
              (Device->Data.Keyboard.Flags & KEY_BREAK) ? "up" : "down",
              Device->Data.Keyboard.MakeCode);

        IntLastInputTick(TRUE);

        UserEnterExclusive();
        RawInputSetSourceDevice(&Device->Path);
        UserProcessKeyboardInput(&Device->Data.Keyboard);
        RawInputSetSourceDevice(NULL);
        UserLeave();
    }
}

/*
 * RawInputThreadMain
 *
 * Reads data from input devices and supports win32 timers
 */
VOID NTAPI
RawInputThreadMain(VOID)
{
    NTSTATUS Status;
    PVOID WaitObjects[7 + HIDINPUT_MAX_DEVICES + HIDINPUT_MAX_RAW_DEVICES + TOUCHINPUT_MAX_DEVICES + INPUT_MAX_DEVICES + 1];
    PVOID pSignaledObject = NULL;
    KWAIT_BLOCK WaitBlockArray[RTL_NUMBER_OF(WaitObjects)];
    ULONG cWaitObjects = 0, cExpectedWaitObjects;
    PVOID ShutdownEvent;
    HWINSTA hWinSta;
    BOOL SyncDisplay;

    ptiRawInput = GetW32ThreadInfo();
    ptiRawInput->TIF_flags |= TIF_SYSTEMTHREAD;
    ptiRawInput->pClientInfo->dwTIFlags = ptiRawInput->TIF_flags;

    TRACE("Raw Input Thread %p\n", ptiRawInput);

    KeSetPriorityThread(&PsGetCurrentThread()->Tcb,
                        LOW_REALTIME_PRIORITY + 3);

    Status = ObOpenObjectByPointer(UserGetDisplayInputWindowStation(),
                                   0,
                                   NULL,
                                   MAXIMUM_ALLOWED,
                                   ExWindowStationObjectType,
                                   UserMode,
                                   (PHANDLE)&hWinSta);
    if (NT_SUCCESS(Status))
    {
        UserSetProcessWindowStation(hWinSta);
    }
    else
    {
        ASSERT(FALSE);
        /* Failed to open the interactive winsta! What now? */
    }

    UserEnterExclusive();
    StartTheTimers();
    UserLeave();

    HidInputStart();
    TouchInputStart();
    InputDevicesStart();

    PoRequestShutdownEvent(&ShutdownEvent);
    for (;;)
    {
        SyncDisplay = FALSE;

        /* Reset WaitHandles array */
        cWaitObjects = 0;
        WaitObjects[cWaitObjects++] = ShutdownEvent;
        WaitObjects[cWaitObjects++] = MasterTimer;
        cExpectedWaitObjects = 2 + HidInputPrepareWait(WaitObjects, &cWaitObjects);
        cExpectedWaitObjects += TouchInputPrepareWait(WaitObjects, &cWaitObjects);
        cExpectedWaitObjects += InputDevicesPrepareWait(WaitObjects, &cWaitObjects);

        /* If all objects are pending, wait for them */
        if (cWaitObjects == cExpectedWaitObjects)
        {
            Status = KeWaitForMultipleObjects(cWaitObjects,
                                              WaitObjects,
                                              WaitAny,
                                              UserRequest,
                                              KernelMode,
                                              TRUE,
                                              NULL,
                                              WaitBlockArray);

            if ((Status >= STATUS_WAIT_0) &&
                (Status < (STATUS_WAIT_0 + (LONG)cWaitObjects)))
            {
                /* Some device has finished reading */
                pSignaledObject = WaitObjects[Status - STATUS_WAIT_0];

                if (pSignaledObject == MasterTimer)
                {
                    UserEnterShared();
                    if (!ProcessTimers(FALSE))
                    {
                        UserLeave();
                        UserEnterExclusive();
                        ProcessTimers(TRUE);
                    }
                    UserLeave();
                    SyncDisplay = TRUE;
                }
                else if (pSignaledObject == ShutdownEvent)
                {
                    break;
                }
                else if (!InputDevicesSignaled(pSignaledObject) &&
                         !HidInputSignaled(pSignaledObject) &&
                         !TouchInputSignaled(pSignaledObject))
                {
                    ASSERT(FALSE);
                }
            }
        }

        InputDevicesProcess();

        if (HidInputProcess())
            IntLastInputTick(TRUE);
        if (TouchInputProcess())
            IntLastInputTick(TRUE);

        /* A display-only synchronization can copy a whole frame. Do not
         * hold USER across it or delay input already in this batch. */
        if (SyncDisplay)
            SynchronizeDriver(GCAPS2_SYNCTIMER);
    }

    InputDevicesStop();
    HidInputStop();
    TouchInputStop();

    TRACE("Raw Input Thread Exit!\n");
}

/*
 * InitInputImpl
 *
 * Inits input implementation
 */
CODE_SEG("INIT")
NTSTATUS
NTAPI
InitInputImpl(
    _In_ PDRIVER_OBJECT DriverObject)
{
    gInputDriverObject = DriverObject;
    MasterTimer = ExAllocatePoolWithTag(NonPagedPool, sizeof(KTIMER), USERTAG_SYSTEM);
    if (!MasterTimer)
    {
        ERR("Failed to allocate memory\n");
        ASSERT(FALSE);
        return STATUS_UNSUCCESSFUL;
    }
    KeInitializeTimer(MasterTimer);

    return STATUS_SUCCESS;
}

BOOL FASTCALL
IntBlockInput(PTHREADINFO pti, BOOL BlockIt)
{
    PTHREADINFO OldBlock;
    ASSERT(pti);

    if(!pti->rpdesk || ((pti->TIF_flags & TIF_INCLEANUP) && BlockIt))
    {
        /*
         * Fail blocking if exiting the thread
         */

        return FALSE;
    }

    /*
     * FIXME: Check access rights of the window station
     *        e.g. services running in the service window station cannot block input
     */
    if(!ThreadHasInputAccess(pti) ||
       !IntIsActiveDesktop(pti->rpdesk))
    {
        EngSetLastError(ERROR_ACCESS_DENIED);
        return FALSE;
    }

    ASSERT(pti->rpdesk);
    OldBlock = pti->rpdesk->BlockInputThread;
    if(OldBlock)
    {
        if(OldBlock != pti)
        {
            EngSetLastError(ERROR_ACCESS_DENIED);
            return FALSE;
        }
        pti->rpdesk->BlockInputThread = (BlockIt ? pti : NULL);
        return OldBlock == NULL;
    }

    pti->rpdesk->BlockInputThread = (BlockIt ? pti : NULL);
    return OldBlock == NULL;
}

BOOL
APIENTRY
NtUserBlockInput(
    BOOL BlockIt)
{
    BOOL ret;

    TRACE("Enter NtUserBlockInput\n");
    UserEnterExclusive();

    ret = IntBlockInput(PsGetCurrentThreadWin32Thread(), BlockIt);

    UserLeave();
    TRACE("Leave NtUserBlockInput, ret=%i\n", ret);

    return ret;
}

BOOL
FASTCALL
IsRemoveAttachThread(PTHREADINFO pti)
{
    NTSTATUS Status;
    PATTACHINFO pai;
    BOOL Ret = TRUE;
    PTHREADINFO ptiFrom = NULL, ptiTo = NULL;

    do
    {
       if (!gpai) return TRUE;

       pai = gpai; // Bottom of the list.

       do
       {
          if (pai->pti2 == pti)
          {
             ptiFrom = pai->pti1;
             ptiTo = pti;
             break;
          }
          if (pai->pti1 == pti)
          {
             ptiFrom = pti;
             ptiTo = pai->pti2;
             break;
          }
          pai = pai->paiNext;

       } while (pai);

       if (!pai && !ptiFrom && !ptiTo) break;

       Status = UserAttachThreadInput(ptiFrom, ptiTo, FALSE);
       if (!NT_SUCCESS(Status)) Ret = FALSE;

    } while (Ret);

    return Ret;
}

NTSTATUS FASTCALL
UserAttachThreadInput(PTHREADINFO ptiFrom, PTHREADINFO ptiTo, BOOL fAttach)
{
    MSG msg;
    PATTACHINFO pai;
    PCURICON_OBJECT CurIcon;

    /* Can not be the same thread. */
    if (ptiFrom == ptiTo) return STATUS_INVALID_PARAMETER;

    /* Do not attach to system threads or between different desktops. */
    if (ptiFrom->TIF_flags & TIF_DONTATTACHQUEUE ||
        ptiTo->TIF_flags & TIF_DONTATTACHQUEUE ||
        ptiFrom->rpdesk != ptiTo->rpdesk)
        return STATUS_ACCESS_DENIED;

    /* MSDN Note:
       Keyboard and mouse events received by both threads are processed by the thread specified by the idAttachTo.
     */

    /* If Attach set, allocate and link. */
    if (fAttach)
    {
        pai = ExAllocatePoolWithTag(PagedPool, sizeof(ATTACHINFO), USERTAG_ATTACHINFO);
        if (!pai) return STATUS_NO_MEMORY;

        pai->paiNext = gpai;
        pai->pti1 = ptiFrom;
        pai->pti2 = ptiTo;
        gpai = pai;
        paiCount++;
        TRACE("Attach Allocated! ptiFrom 0x%p  ptiTo 0x%p paiCount %d\n",ptiFrom,ptiTo,paiCount);

        if (ptiTo->MessageQueue != ptiFrom->MessageQueue)
        {

           ptiTo->MessageQueue->iCursorLevel -= ptiFrom->iCursorLevel;

           if (ptiFrom->MessageQueue == gpqForeground)
           {
              TRACE("ptiFrom is Foreground\n");
              ptiTo->MessageQueue->spwndActive  = ptiFrom->MessageQueue->spwndActive;
              ptiTo->MessageQueue->spwndFocus   = ptiFrom->MessageQueue->spwndFocus;
              ptiTo->MessageQueue->spwndCapture = ptiFrom->MessageQueue->spwndCapture;
              ptiTo->MessageQueue->QF_flags    ^= ((ptiTo->MessageQueue->QF_flags ^ ptiFrom->MessageQueue->QF_flags) & QF_CAPTURELOCKED);
              RtlCopyMemory(&ptiTo->MessageQueue->CaretInfo,
                            &ptiFrom->MessageQueue->CaretInfo,
                            sizeof(ptiTo->MessageQueue->CaretInfo));
              IntSetFocusMessageQueue(NULL);
              IntSetFocusMessageQueue(ptiTo->MessageQueue);
              IntSetForegroundThread(ptiTo);
           }
           else
           {
              TRACE("ptiFrom NOT Foreground\n");
              if ( ptiTo->MessageQueue->spwndActive == 0 )
                  ptiTo->MessageQueue->spwndActive = ptiFrom->MessageQueue->spwndActive;
              if ( ptiTo->MessageQueue->spwndFocus == 0 )
                  ptiTo->MessageQueue->spwndFocus  = ptiFrom->MessageQueue->spwndFocus;
           }

           CurIcon = ptiFrom->MessageQueue->CursorObject;

           MsqDestroyMessageQueue(ptiFrom);

           if (CurIcon)
           {
              // Could be global. Keep it above the water line!
              UserReferenceObject(CurIcon);
           }

           if (CurIcon && UserObjectInDestroy(UserHMGetHandle(CurIcon)))
           {
              UserDereferenceObject(CurIcon);
              CurIcon = NULL;
           }

           ptiFrom->MessageQueue = ptiTo->MessageQueue;

           // Pass cursor From if To is null. Pass test_SetCursor parent_id == current pti ID.
           if (CurIcon && ptiTo->MessageQueue->CursorObject == NULL)
           {
              TRACE("ptiTo receiving ptiFrom Cursor\n");
              ptiTo->MessageQueue->CursorObject = CurIcon;
           }

           ptiFrom->MessageQueue->cThreads++;
           TRACE("ptiTo S Share count %u\n", ptiFrom->MessageQueue->cThreads);

           IntReferenceMessageQueue(ptiTo->MessageQueue);
        }
        else
        {
           TRACE("Attach Threads are already associated!\n");
        }
    }
    else /* If clear, unlink and free it. */
    {
        BOOL Hit = FALSE;
        PATTACHINFO *ppai;

        if (!gpai) return STATUS_INVALID_PARAMETER;

        /* Search list and free if found or return false. */
        ppai = &gpai;
        while (*ppai != NULL)
        {
           if ( (*ppai)->pti2 == ptiTo && (*ppai)->pti1 == ptiFrom )
           {
              pai = *ppai;
              /* Remove it from the list */
              *ppai = (*ppai)->paiNext;
              ExFreePoolWithTag(pai, USERTAG_ATTACHINFO);
              paiCount--;
              Hit = TRUE;
              break;
           }
           ppai = &((*ppai)->paiNext);
        }

        if (!Hit) return STATUS_INVALID_PARAMETER;

        TRACE("Attach Free! ptiFrom 0x%p  ptiTo 0x%p paiCount %d\n",ptiFrom,ptiTo,paiCount);

        if (ptiTo->MessageQueue == ptiFrom->MessageQueue)
        {
           PWND spwndActive = ptiTo->MessageQueue->spwndActive;
           PWND spwndFocus  = ptiTo->MessageQueue->spwndFocus;
           PWND spwndCapture = ptiTo->MessageQueue->spwndCapture;
           BOOL bForeground = (ptiTo->MessageQueue == gpqForeground);

           if (gptiForeground == ptiFrom)
           {
              TRACE("ptiTo is now pti FG.\n");
              // MessageQueue foreground is set so switch threads.
              IntSetForegroundThread(ptiTo);
           }
           ptiTo->MessageQueue->cThreads--;
           TRACE("ptiTo E Share count %u\n", ptiTo->MessageQueue->cThreads);
           ASSERT(ptiTo->MessageQueue->cThreads >= 1);

           IntDereferenceMessageQueue(ptiTo->MessageQueue);

           ptiFrom->MessageQueue = MsqCreateMessageQueue(ptiFrom);

           if (spwndActive)
           {
              if (spwndActive->head.pti == ptiFrom)
              {
                 ptiFrom->MessageQueue->spwndActive = spwndActive;
                 ptiTo->MessageQueue->spwndActive = 0;
                 if (ptiTo->MessageQueue == gpqForeground)
                 {
                    IntSetFocusMessageQueue(NULL);
                    IntSetFocusMessageQueue(ptiFrom->MessageQueue);
                    IntSetForegroundThread(ptiFrom);
                 }
              }
           }
           if (spwndFocus)
           {
              if (spwndFocus->head.pti == ptiFrom)
              {
                 ptiFrom->MessageQueue->spwndFocus = spwndFocus;
                 ptiTo->MessageQueue->spwndFocus = 0;
              }
           }
           if (spwndCapture && spwndCapture->head.pti == ptiFrom)
           {
              ptiFrom->MessageQueue->spwndCapture = spwndCapture;
              ptiTo->MessageQueue->spwndCapture = NULL;
           }
           if (bForeground && spwndActive &&
               (spwndActive->head.pti->TIF_flags & TIF_INCLEANUP))
           {
              PTHREADINFO ptiLive = (spwndActive->head.pti == ptiTo) ? ptiFrom : ptiTo;
              PWND pwnd = NULL;

              if (!(ptiLive->TIF_flags & TIF_INCLEANUP) && ptiLive->pDeskInfo && ptiLive->pDeskInfo->spwnd)
              {
                 for (pwnd = ptiLive->pDeskInfo->spwnd->spwndChild; pwnd; pwnd = pwnd->spwndNext)
                 {
                    if (pwnd->head.pti == ptiLive && can_activate_window(pwnd))
                       break;
                 }
              }
              if (pwnd)
              {
                 spwndActive->head.pti->MessageQueue->spwndActive = NULL;
                 ptiLive->MessageQueue->spwndActive = pwnd;
                 ptiLive->MessageQueue->spwndFocus = pwnd;
                 IntSetFocusMessageQueue(NULL);
                 IntSetFocusMessageQueue(ptiLive->MessageQueue);
                 IntSetForegroundThread(ptiLive);
              }
           }
           ptiTo->MessageQueue->iCursorLevel -= ptiFrom->iCursorLevel;
        }
        else
        {
           ERR("Detaching Threads are not associated!\n");
        }
    }
    /* Note that key state, which can be ascertained by calls to the GetKeyState
       or GetKeyboardState function, is reset after a call to AttachThreadInput.
       ATM which one?
     */
    RtlCopyMemory(ptiTo->MessageQueue->afKeyState, gafAsyncKeyState, sizeof(gafAsyncKeyState));
    RtlCopyMemory(ptiTo->MessageQueue->afKeyStateSync, gafAsyncKeyState, sizeof(gafAsyncKeyState));

    ptiTo->MessageQueue->msgDblClk.message = 0;

    /* Generate mouse move message */
    msg.message = WM_MOUSEMOVE;
    msg.wParam = UserGetMouseButtonsState();
    msg.lParam = MAKELPARAM(gpsi->ptCursor.x, gpsi->ptCursor.y);
    msg.pt = gpsi->ptCursor;
    co_MsqInsertMouseMessage(&msg, 0, 0, TRUE);

    return STATUS_SUCCESS;
}

BOOL
APIENTRY
NtUserAttachThreadInput(
    IN DWORD idAttach,
    IN DWORD idAttachTo,
    IN BOOL fAttach)
{
  NTSTATUS Status;
  PTHREADINFO pti, ptiTo;
  BOOL Ret = FALSE;

  UserEnterExclusive();
  TRACE("Enter NtUserAttachThreadInput %s\n",(fAttach ? "TRUE" : "FALSE" ));

  pti = IntTID2PTI(UlongToHandle(idAttach));
  ptiTo = IntTID2PTI(UlongToHandle(idAttachTo));

  if ( !pti || !ptiTo )
  {
     TRACE("AttachThreadInput pti or ptiTo NULL.\n");
     EngSetLastError(ERROR_INVALID_PARAMETER);
     goto Exit;
  }

  Status = UserAttachThreadInput( pti, ptiTo, fAttach);
  if (!NT_SUCCESS(Status))
  {
     TRACE("AttachThreadInput Error Status 0x%x. \n",Status);
     EngSetLastError(RtlNtStatusToDosError(Status));
  }
  else Ret = TRUE;

Exit:
  TRACE("Leave NtUserAttachThreadInput, ret=%d\n",Ret);
  UserLeave();
  return Ret;
}

/*
 * NtUserSendInput
 *
 * Generates input events from software
 */
UINT
APIENTRY
NtUserSendInput(
    UINT nInputs,
    LPINPUT pInput,
    INT cbSize)
{
    PTHREADINFO pti;
    UINT uRet = 0;
    UINT i;

    TRACE("Enter NtUserSendInput\n");
    UserEnterExclusive();

    pti = PsGetCurrentThreadWin32Thread();
    ASSERT(pti);

    if (!pti->rpdesk)
    {
        goto cleanup;
    }

    if (!nInputs || !pInput || cbSize != sizeof(INPUT))
    {
        EngSetLastError(ERROR_INVALID_PARAMETER);
        goto cleanup;
    }

    /*
     * FIXME: Check access rights of the window station
     *        e.g. services running in the service window station cannot block input
     */
    if (!ThreadHasInputAccess(pti) ||
        !IntIsActiveDesktop(pti->rpdesk))
    {
        EngSetLastError(ERROR_ACCESS_DENIED);
        goto cleanup;
    }

    for (i = 0; i < nInputs; i++)
    {
        DWORD Type;
        NTSTATUS Status;

        Status = MmCopyFromCaller(&Type, &pInput[i].type, sizeof(Type));
        if (!NT_SUCCESS(Status))
        {
            SetLastNtError(Status);
            goto cleanup;
        }
        if (Type == INPUT_HARDWARE)
        {
            EngSetLastError(ERROR_CALL_NOT_IMPLEMENTED);
            goto cleanup;
        }
    }

    while (nInputs--)
    {
        INPUT SafeInput;
        NTSTATUS Status;

        Status = MmCopyFromCaller(&SafeInput, pInput++, sizeof(INPUT));
        if (!NT_SUCCESS(Status))
        {
            SetLastNtError(Status);
            goto cleanup;
        }

        switch (SafeInput.type)
        {
            case INPUT_MOUSE:
                if (UserSendMouseInput(&SafeInput.mi, TRUE))
                    uRet++;
                break;
            case INPUT_KEYBOARD:
                if (UserSendKeyboardInput(&SafeInput.ki, TRUE))
                    uRet++;
                break;
            default:
                uRet++;
                break;
        }
    }

    if (uRet)
        IntLastInputTick(TRUE);

cleanup:
    TRACE("Leave NtUserSendInput, ret=%u\n", uRet);
    UserLeave();
    return uRet;
}

/* EOF */
