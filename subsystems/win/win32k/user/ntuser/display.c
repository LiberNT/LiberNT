/*
 * COPYRIGHT:        See COPYING in the top level directory
 * PROJECT:          ReactOS kernel
 * PURPOSE:          Video initialization and display settings
 * FILE:             win32ss/user/ntuser/display.c
 * PROGRAMER:        Timo Kreuzer (timo.kreuzer@reactos.org)
 */

#include <win32k.h>
DBG_DEFAULT_CHANNEL(UserDisplay);

BOOL gbBaseVideo = FALSE;
static PPROCESSINFO gpFullscreen = NULL;

static const PWCHAR KEY_VIDEO = L"\\Registry\\Machine\\HARDWARE\\DEVICEMAP\\VIDEO";

VOID
RegWriteDisplaySettings(HKEY hkey, PDEVMODEW pdm)
{
    RegWriteDWORD(hkey, L"DefaultSettings.BitsPerPel", pdm->dmBitsPerPel);
    RegWriteDWORD(hkey, L"DefaultSettings.XResolution", pdm->dmPelsWidth);
    RegWriteDWORD(hkey, L"DefaultSettings.YResolution", pdm->dmPelsHeight);
    RegWriteDWORD(hkey, L"DefaultSettings.Flags", pdm->dmDisplayFlags);
    RegWriteDWORD(hkey, L"DefaultSettings.VRefresh", pdm->dmDisplayFrequency);
    RegWriteDWORD(hkey, L"DefaultSettings.XPanning", pdm->dmPanningWidth);
    RegWriteDWORD(hkey, L"DefaultSettings.YPanning", pdm->dmPanningHeight);
    RegWriteDWORD(hkey, L"DefaultSettings.Orientation", pdm->dmDisplayOrientation);
    RegWriteDWORD(hkey, L"DefaultSettings.FixedOutput", pdm->dmDisplayFixedOutput);
    RegWriteDWORD(hkey, L"Attach.RelativeX", pdm->dmPosition.x);
    RegWriteDWORD(hkey, L"Attach.RelativeY", pdm->dmPosition.y);
}

VOID
RegReadDisplaySettings(HKEY hkey, PDEVMODEW pdm)
{
    DWORD dwValue;

    /* Zero out the structure */
    RtlZeroMemory(pdm, sizeof(DEVMODEW));

/* Helper macro */
#define READ(field, str, flag) \
    if (RegReadDWORD(hkey, L##str, &dwValue)) \
    { \
        pdm->field = dwValue; \
        pdm->dmFields |= flag; \
    }

    /* Read all present settings */
    READ(dmBitsPerPel, "DefaultSettings.BitsPerPel", DM_BITSPERPEL);
    READ(dmPelsWidth, "DefaultSettings.XResolution", DM_PELSWIDTH);
    READ(dmPelsHeight, "DefaultSettings.YResolution", DM_PELSHEIGHT);
    READ(dmDisplayFlags, "DefaultSettings.Flags", DM_DISPLAYFLAGS);
    READ(dmDisplayFrequency, "DefaultSettings.VRefresh", DM_DISPLAYFREQUENCY);
    READ(dmPanningWidth, "DefaultSettings.XPanning", DM_PANNINGWIDTH);
    READ(dmPanningHeight, "DefaultSettings.YPanning", DM_PANNINGHEIGHT);
    READ(dmDisplayOrientation, "DefaultSettings.Orientation", DM_DISPLAYORIENTATION);
    READ(dmDisplayFixedOutput, "DefaultSettings.FixedOutput", DM_DISPLAYFIXEDOUTPUT);
    READ(dmPosition.x, "Attach.RelativeX", DM_POSITION);
    READ(dmPosition.y, "Attach.RelativeY", DM_POSITION);
}

PGRAPHICS_DEVICE
NTAPI
InitDisplayDriver(
    IN PWSTR pwszDeviceName,
    IN PWSTR pwszRegKey)
{
    PGRAPHICS_DEVICE pGraphicsDevice;
    UNICODE_STRING ustrDeviceName, ustrDisplayDrivers, ustrDescription;
    NTSTATUS Status;
    WCHAR awcBuffer[128];
    ULONG cbSize;
    HKEY hkey;
    DWORD dwVga;

    TRACE("InitDisplayDriver(%S, %S);\n",
          pwszDeviceName, pwszRegKey);

    /* Open the driver's registry key */
    Status = RegOpenKey(pwszRegKey, &hkey);
    if (!NT_SUCCESS(Status))
    {
        ERR("Failed to open registry key: %ls\n", pwszRegKey);
        return NULL;
    }

    /* Query the diplay drivers */
    cbSize = sizeof(awcBuffer) - 10;
    Status = RegQueryValue(hkey,
                           L"InstalledDisplayDrivers",
                           REG_MULTI_SZ,
                           awcBuffer,
                           &cbSize);
    if (!NT_SUCCESS(Status))
    {
        ERR("Didn't find 'InstalledDisplayDrivers', status = 0x%lx\n", Status);
        ZwClose(hkey);
        return NULL;
    }

    /* Initialize the UNICODE_STRING */
    ustrDisplayDrivers.Buffer = awcBuffer;
    ustrDisplayDrivers.MaximumLength = (USHORT)cbSize;
    ustrDisplayDrivers.Length = (USHORT)cbSize;

    /* Set Buffer for description and size of remaining buffer */
    ustrDescription.Buffer = awcBuffer + (cbSize / sizeof(WCHAR));
    cbSize = sizeof(awcBuffer) - cbSize;

    /* Query the device string */
    Status = RegQueryValue(hkey,
                           L"Device Description",
                           REG_SZ,
                           ustrDescription.Buffer,
                           &cbSize);
    if (NT_SUCCESS(Status))
    {
        ustrDescription.MaximumLength = (USHORT)cbSize;
        ustrDescription.Length = (USHORT)cbSize;
    }
    else
    {
        RtlInitUnicodeString(&ustrDescription, L"<unknown>");
    }

    /* Query if this is a VGA compatible driver */
    cbSize = sizeof(DWORD);
    Status = RegQueryValue(hkey, L"VgaCompatible", REG_DWORD, &dwVga, &cbSize);
    if (!NT_SUCCESS(Status)) dwVga = 0;

    /* Close the registry key */
    ZwClose(hkey);

    /* Register the device with GDI */
    RtlInitUnicodeString(&ustrDeviceName, pwszDeviceName);
    pGraphicsDevice = EngpRegisterGraphicsDevice(&ustrDeviceName,
                                                 &ustrDisplayDrivers,
                                                 &ustrDescription);
    if (pGraphicsDevice && dwVga)
    {
        pGraphicsDevice->StateFlags |= DISPLAY_DEVICE_VGA_COMPATIBLE;
    }

    return pGraphicsDevice;
}

NTSTATUS
NTAPI
InitVideo(VOID)
{
    NTSTATUS Status;
    HKEY hkey;

    TRACE("----------------------------- InitVideo() -------------------------------\n");

    /* Check if VGA mode is requested, by finding the special volatile key created by VIDEOPRT */
    Status = RegOpenKey(L"\\Registry\\Machine\\System\\CurrentControlSet\\Control\\GraphicsDrivers\\BaseVideo", &hkey);
    if (NT_SUCCESS(Status))
        ZwClose(hkey);
    gbBaseVideo = NT_SUCCESS(Status);
    if (gbBaseVideo)
        ERR("VGA mode requested.\n");

    /*
     * Initialize all display devices.
     *
     * The display miniport (dxgkrnl for a WDDM display-only driver such as the
     * virtio-gpu viogpudo, or videoprt) creates the HARDWARE\DEVICEMAP\VIDEO
     * registry map asynchronously while its adapter starts.  On a fast boot
     * win32k can reach here before the driver has registered, so
     * EngpUpdateGraphicsDeviceList() returns STATUS_OBJECT_NAME_NOT_FOUND and
     * our caller bugchecks VIDEO_DRIVER_INIT_FAILURE (0xB4) — an intermittent
     * boot-order race that makes desktop init flaky.  Wait (bounded) for the
     * display driver to publish its DEVICEMAP\VIDEO entry before giving up.
     */
    {
        LARGE_INTEGER Interval;
        ULONG Attempt;

        Interval.QuadPart = -(100LL * 10000LL); /* 100 ms, relative */
        for (Attempt = 0; ; Attempt++)
        {
            Status = EngpUpdateGraphicsDeviceList();
            if ((Status != STATUS_OBJECT_NAME_NOT_FOUND &&
                 (!NT_SUCCESS(Status) || EngpFindGraphicsDevice(NULL, 0) != NULL)) ||
                Attempt >= 150)
                break;

            /* Not ready yet — wait for the display driver to register. */
            KeDelayExecutionThread(KernelMode, FALSE, &Interval);
        }
    }
    if (!NT_SUCCESS(Status))
        return Status;

    InitSysParams();

    return STATUS_SUCCESS;
}

VOID
UserRefreshDisplay(IN PPDEVOBJ ppdev)
{
    ULONG_PTR ulResult;
    // PVOID pvOldCursor;

    // TODO: Re-enable the cursor reset code once this function becomes called
    // from within a Win32 thread... Indeed UserSetCursor() requires this, but
    // at the moment this function is directly called from a separate thread
    // from within videoprt, instead of by a separate win32k system thread.

    if (!ppdev)
        return;

    PDEVOBJ_vReference(ppdev);

    /* Remove mouse pointer */
    // pvOldCursor = UserSetCursor(NULL, TRUE);

    /* Do the mode switch -- Use the actual same current mode */
    ulResult = PDEVOBJ_bSwitchMode(ppdev, ppdev->pdmwDev);
    ASSERT(ulResult);

    /* Restore mouse pointer, no hooks called */
    // pvOldCursor = UserSetCursor(pvOldCursor, TRUE);
    // ASSERT(pvOldCursor == NULL);

    /* Update the system metrics */
    InitMetrics();

    /* Set new size of the monitor */
    // UserUpdateMonitorSize((HDEV)ppdev);

    //co_IntShowDesktop(pdesk, ppdev->gdiinfo.ulHorzRes, ppdev->gdiinfo.ulVertRes);
    UserRedrawDesktop();

    PDEVOBJ_vRelease(ppdev);
}

/* GUID_DEVINTERFACE_MONITOR (ntddvdeo.h) */
static const GUID UserMonitorInterfaceGuid =
    { 0xe6f07b5f, 0xee97, 0x4a90, { 0xb0, 0x76, 0x33, 0xf5, 0x7b, 0xf4, 0xea, 0xa7 } };

/* The monitor's device interface path in its user-mode form (\\?\...). */
static VOID
UserMonitorInterfaceName(
    _In_ PDEVICE_OBJECT Pdo,
    _Out_writes_(Capacity) PWCHAR Buffer,
    _In_ ULONG Capacity)
{
    PWSTR List;

    Buffer[0] = UNICODE_NULL;
    if (!NT_SUCCESS(IoGetDeviceInterfaces(&UserMonitorInterfaceGuid, Pdo, 0, &List)))
        return;
    if (List[0] == L'\\' && List[1] == L'?' && List[2] == L'?' && List[3] == L'\\')
    {
        RtlStringCchCopyW(Buffer, Capacity, List);
        Buffer[1] = L'\\';
    }
    ExFreePool(List);
}

/* A monitor's DeviceKey is its driver key under the device class keys. */
static VOID
UserMonitorDeviceKey(
    _In_ PDEVICE_OBJECT Pdo,
    _Out_writes_(Capacity) PWCHAR Buffer,
    _In_ ULONG Capacity)
{
    static const WCHAR ClassRoot[] = L"\\Registry\\Machine\\System\\CurrentControlSet\\Control\\Class\\";
    WCHAR DriverKey[64];
    ULONG Length;

    Buffer[0] = UNICODE_NULL;
    if (!NT_SUCCESS(IoGetDeviceProperty(Pdo, DevicePropertyDriverKeyName, sizeof(DriverKey), DriverKey, &Length)))
        return;
    RtlStringCchCopyW(Buffer, Capacity, ClassRoot);
    RtlStringCchCatW(Buffer, Capacity, DriverKey);
}

static
PGRAPHICS_DEVICE
UserGetRemoteDisplayDevice(VOID)
{
    if (gulRemoteSessionId != PsGetCurrentProcessSessionId() || !gppdevDetached ||
        UserIsLogonProcess(PsGetCurrentProcessId()) || !gpmdev || !gpmdev->ppdevGlobal)
    {
        return NULL;
    }
    return gpmdev->ppdevGlobal->pGraphicsDevice;
}

static
PGRAPHICS_DEVICE
UserFindDisplaySettingsDevice(
    _In_opt_ PUNICODE_STRING pustrDevice)
{
    PGRAPHICS_DEVICE pGraphicsDevice = NULL;
    PPDEVOBJ ppdev;

    if (pustrDevice)
        return EngpFindGraphicsDevice(pustrDevice, 0);

    ppdev = EngpGetPDEV(NULL);
    if (ppdev)
    {
        pGraphicsDevice = ppdev->pGraphicsDevice;
        PDEVOBJ_vRelease(ppdev);
    }
    return pGraphicsDevice ? pGraphicsDevice : EngpFindGraphicsDevice(NULL, 0);
}

NTSTATUS
NTAPI
UserEnumDisplayDevices(
    PUNICODE_STRING pustrDevice,
    DWORD iDevNum,
    PDISPLAY_DEVICEW pdispdev,
    DWORD dwFlags)
{
    PGRAPHICS_DEVICE pGraphicsDevice, pRemoteDevice;
    PDEVICE_OBJECT pdo;
    VIDEO_MONITOR_DEVICE Monitor = { 0 };
    PWCHAR pHardwareId;
    ULONG cbSize, dwLength;
    HKEY hkey;
    NTSTATUS Status;

    if (!pustrDevice)
    {
        /* Check if some devices have been added since last time */
        EngpUpdateGraphicsDeviceList();
    }

    /* Ask gdi for the GRAPHICS_DEVICE */
    pRemoteDevice = UserGetRemoteDisplayDevice();
    if (pRemoteDevice && !pustrDevice)
        pGraphicsDevice = iDevNum == 0 ? pRemoteDevice : NULL;
    else
        pGraphicsDevice = EngpFindGraphicsDevice(pustrDevice, iDevNum);
    if (pRemoteDevice && pGraphicsDevice != pRemoteDevice)
        pGraphicsDevice = NULL;
    if (!pGraphicsDevice)
    {
        /* No device found */
        if (iDevNum == 0)
            ERR("No GRAPHICS_DEVICE found for '%wZ', iDevNum %lu\n", pustrDevice, iDevNum);
        else
            TRACE("No GRAPHICS_DEVICE found for '%wZ', iDevNum %lu\n", pustrDevice, iDevNum);
        return STATUS_UNSUCCESSFUL;
    }

    if (!pustrDevice)
    {
        pdo = pGraphicsDevice->PhysDeviceHandle;
    }
    else
    {
        EngpUpdateMonitorDevices(pGraphicsDevice);
        if (!EngpReferenceMonitorDevice(pGraphicsDevice, iDevNum, &Monitor))
        {
            TRACE("No monitor #%u for '%wZ'\n", iDevNum + 1, pustrDevice);
            return STATUS_UNSUCCESSFUL;
        }
        pdo = Monitor.pdo;
    }


    /* Open the device map registry key */
    Status = RegOpenKey(KEY_VIDEO, &hkey);
    if (!NT_SUCCESS(Status))
    {
        /* No device found */
        ERR("Could not open reg key\n");
        if (Monitor.pdo)
            ObDereferenceObject(Monitor.pdo);
        return STATUS_UNSUCCESSFUL;
    }

    /* Query the registry path */
    cbSize = sizeof(pdispdev->DeviceKey);
    RegQueryValue(hkey,
                  pGraphicsDevice->szNtDeviceName,
                  REG_SZ,
                  pdispdev->DeviceKey,
                  &cbSize);

    /* Close registry key */
    ZwClose(hkey);

    /* Copy device name, device string and StateFlags */
    if (!pustrDevice)
    {
        RtlStringCbCopyW(pdispdev->DeviceName, sizeof(pdispdev->DeviceName), pGraphicsDevice->szWinDeviceName);
        RtlStringCbCopyW(pdispdev->DeviceString, sizeof(pdispdev->DeviceString), pGraphicsDevice->pwszDescription);
        pdispdev->StateFlags = pGraphicsDevice->StateFlags;
        if (pRemoteDevice)
            pdispdev->StateFlags |= DISPLAY_DEVICE_REMOTE;
    }
    else
    {
        _swprintf(pdispdev->DeviceName, L"%ws\\Monitor%u", pGraphicsDevice->szWinDeviceName, iDevNum);
        if (pdo)
        {
            Status = IoGetDeviceProperty(pdo,
                                         DevicePropertyDeviceDescription,
                                         sizeof(pdispdev->DeviceString),
                                         pdispdev->DeviceString,
                                         &dwLength);
            if (!NT_SUCCESS(Status))
                pdispdev->DeviceString[0] = UNICODE_NULL;
            UserMonitorDeviceKey(pdo, pdispdev->DeviceKey, ARRAYSIZE(pdispdev->DeviceKey));
        }
        pdispdev->StateFlags = Monitor.flag;
    }
    pdispdev->DeviceID[0] = UNICODE_NULL;

    /* Fill in DeviceID */
    if (pdo != NULL && pustrDevice && (dwFlags & EDD_GET_DEVICE_INTERFACE_NAME))
    {
        UserMonitorInterfaceName(pdo, pdispdev->DeviceID, ARRAYSIZE(pdispdev->DeviceID));
    }
    else if (pdo != NULL && (pustrDevice || !(dwFlags & EDD_GET_DEVICE_INTERFACE_NAME)))
    {
        Status = IoGetDeviceProperty(pdo,
                                     DevicePropertyHardwareID,
                                     0,
                                     NULL,
                                     &dwLength);

        if (Status == STATUS_BUFFER_TOO_SMALL)
        {
            pHardwareId = ExAllocatePoolWithTag(PagedPool,
                                                dwLength,
                                                USERTAG_DISPLAYINFO);
            if (!pHardwareId)
            {
                EngSetLastError(ERROR_NOT_ENOUGH_MEMORY);
                if (Monitor.pdo)
                    ObDereferenceObject(Monitor.pdo);
                return STATUS_INSUFFICIENT_RESOURCES;
            }

            Status = IoGetDeviceProperty(pdo,
                                         DevicePropertyHardwareID,
                                         dwLength,
                                         pHardwareId,
                                         &dwLength);

            if (!NT_SUCCESS(Status))
            {
                ERR("IoGetDeviceProperty() failed with status 0x%08lx\n", Status);
            }
            else
            {
                /* For video adapters it should be the first Hardware ID
                 * which usually is the longest one and unique enough */
                RtlStringCbCopyW(pdispdev->DeviceID, sizeof(pdispdev->DeviceID), pHardwareId);

                if (pustrDevice)
                {
                    /* For monitors it should be the first Hardware ID,
                     * which we already have obtained above,
                     * concatenated with the unique driver registry key */

                    RtlStringCbCatW(pdispdev->DeviceID, sizeof(pdispdev->DeviceID), L"\\");

                    dwLength = wcslen(pdispdev->DeviceID) + 1;
                    Status = IoGetDeviceProperty(pdo,
                                                 DevicePropertyDriverKeyName,
                                                 (ARRAYSIZE(pdispdev->DeviceID) - dwLength) * sizeof(WCHAR),
                                                 pdispdev->DeviceID + dwLength - 1,
                                                 &dwLength);
                }

                TRACE("Hardware ID: %ls\n", pdispdev->DeviceID);
            }

            ExFreePoolWithTag(pHardwareId, USERTAG_DISPLAYINFO);
        }
        else
        {
            ERR("IoGetDeviceProperty() failed with status 0x%08lx\n", Status);
        }
    }

    if (Monitor.pdo)
        ObDereferenceObject(Monitor.pdo);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
NtUserEnumDisplayDevices(
    PUNICODE_STRING pustrDevice,
    DWORD iDevNum,
    PDISPLAY_DEVICEW pDisplayDevice,
    DWORD dwFlags)
{
    UNICODE_STRING ustrDevice;
    WCHAR awcDevice[CCHDEVICENAME];
    DISPLAY_DEVICEW dispdev;
    NTSTATUS Status;

    TRACE("Enter NtUserEnumDisplayDevices(%wZ, %lu)\n",
          pustrDevice, iDevNum);

    dispdev.cb = sizeof(dispdev);

    if (pustrDevice)
    {
        /* Initialize destination string */
        RtlInitEmptyUnicodeString(&ustrDevice, awcDevice, sizeof(awcDevice));

        _SEH2_TRY
        {
            /* Probe the UNICODE_STRING and the buffer */
            ProbeForRead(pustrDevice, sizeof(UNICODE_STRING), 1);
            ProbeForRead(pustrDevice->Buffer, pustrDevice->Length, 1);

            /* Copy the string */
            RtlCopyUnicodeString(&ustrDevice, pustrDevice);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END

        if (ustrDevice.Length > 0)
            pustrDevice = &ustrDevice;
        else
            pustrDevice = NULL;
   }

    /* Acquire global USER lock */
    UserEnterShared();

    /* Call the internal function */
    Status = UserEnumDisplayDevices(pustrDevice, iDevNum, &dispdev, dwFlags);

    /* Release lock */
    UserLeave();

    /* On success copy data to caller */
    if (NT_SUCCESS(Status))
    {
        /* Enter SEH */
        _SEH2_TRY
        {
            /* First probe the cb field */
            ProbeForWrite(&pDisplayDevice->cb, sizeof(DWORD), 1);

            /* Check the buffer size */
            if (pDisplayDevice->cb)
            {
                /* Probe the output buffer */
                pDisplayDevice->cb = min(pDisplayDevice->cb, sizeof(dispdev));
                ProbeForWrite(pDisplayDevice, pDisplayDevice->cb, 1);

                /* Copy as much as the given buffer allows */
                RtlCopyMemory(pDisplayDevice, &dispdev, pDisplayDevice->cb);
            }
            else
            {
                Status = STATUS_UNSUCCESSFUL;
            }
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = STATUS_UNSUCCESSFUL;
        }
        _SEH2_END
    }

    TRACE("Leave NtUserEnumDisplayDevices, Status = 0x%lx\n", Status);
    return Status;
}

NTSTATUS
NTAPI
UserEnumCurrentDisplaySettings(
    PUNICODE_STRING pustrDevice,
    PDEVMODEW *ppdm,
    PPOINTL pptlPosition)
{
    PGRAPHICS_DEVICE pRemoteDevice;
    PPDEVOBJ ppdev;
    PMONITOR pMonitor;

    pRemoteDevice = UserGetRemoteDisplayDevice();
    if (pRemoteDevice && pustrDevice && EngpFindGraphicsDevice(pustrDevice, 0) != pRemoteDevice)
        return STATUS_INVALID_PARAMETER_1;

    /* Get the PDEV for the device */
    ppdev = EngpGetPDEV(pustrDevice);
    if (!ppdev)
    {
        /* No device found */
        ERR("No PDEV found!\n");
        return STATUS_INVALID_PARAMETER_1;
    }

    *ppdm = ppdev->pdmwDev;
    pptlPosition->x = pptlPosition->y = 0;
    pMonitor = UserGetMonitorFromHDev((HDEV)ppdev);
    if (pMonitor)
    {
        pptlPosition->x = pMonitor->rcMonitor.left;
        pptlPosition->y = pMonitor->rcMonitor.top;
    }
    PDEVOBJ_vRelease(ppdev);

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
UserEnumDisplaySettings(
   PUNICODE_STRING pustrDevice,
   DWORD iModeNum,
   LPDEVMODEW *ppdm,
   DWORD dwFlags)
{
    PGRAPHICS_DEVICE pGraphicsDevice;
    PDEVMODEENTRY pdmentry;
    ULONG i, iFoundMode;
    PPDEVOBJ ppdev;

    TRACE("Enter UserEnumDisplaySettings('%wZ', %lu)\n",
          pustrDevice, iModeNum);

    /* Ask GDI for the GRAPHICS_DEVICE */
    pGraphicsDevice = UserFindDisplaySettingsDevice(pustrDevice);
    if (!pGraphicsDevice || (UserGetRemoteDisplayDevice() && pGraphicsDevice != UserGetRemoteDisplayDevice()))
    {
        /* No device found */
        ERR("No device found!\n");
        return STATUS_INVALID_PARAMETER_1;
    }

    ppdev = EngpGetPDEV(pustrDevice);
    if (ppdev)
    {
        /* let's politely ask the driver for an updated mode list,
           just in case there's something new in there (vbox) */

        if (iModeNum == 0)
            PDEVOBJ_vRefreshModeList(ppdev);
        PDEVOBJ_vRelease(ppdev);
    }
    else if (!EngpBuildDetachedModeList(pGraphicsDevice, FALSE))
    {
        /* A display device that is not attached to the desktop still lists the modes of its output */
        return STATUS_INVALID_PARAMETER_2;
    }

    iFoundMode = 0;
    for (i = 0; i < pGraphicsDevice->cDevModes; i++)
    {
        pdmentry = &pGraphicsDevice->pDevModeList[i];

        /* FIXME: Consider EDS_RAWMODE */
#if 0
        if ((!(dwFlags & EDS_RAWMODE) && (pdmentry->dwFlags & 1)) ||!
            (dwFlags & EDS_RAWMODE))
#endif
        {
            /* Is this the one we want? */
            if (iFoundMode == iModeNum)
            {
                *ppdm = pdmentry->pdm;
                return STATUS_SUCCESS;
            }

            /* Increment number of found modes */
            iFoundMode++;
        }
    }

    /* Nothing was found */
    return STATUS_INVALID_PARAMETER_2;
}

NTSTATUS
NTAPI
UserOpenDisplaySettingsKey(
    OUT PHKEY phkey,
    IN PUNICODE_STRING pustrDevice,
    IN BOOL bGlobal)
{
    HKEY hkey, hkeyMap;
    PGRAPHICS_DEVICE pGraphicsDevice;
    WCHAR szDeviceKey[128];
    ULONG cbSize;
    UNICODE_STRING ustrKeyName;
    OBJECT_ATTRIBUTES ObjectAttributes;
    NTSTATUS Status;

    if (!pustrDevice)
        EngpUpdateGraphicsDeviceList();

    pGraphicsDevice = UserFindDisplaySettingsDevice(pustrDevice);
    if (!pGraphicsDevice)
        return STATUS_UNSUCCESSFUL;

    if (bGlobal)
    {
        // FIXME: Need to fix the registry key somehow
    }

    Status = RegOpenKey(KEY_VIDEO, &hkeyMap);
    if (!NT_SUCCESS(Status))
        return Status;

    RtlZeroMemory(szDeviceKey, sizeof(szDeviceKey));
    cbSize = sizeof(szDeviceKey) - sizeof(WCHAR);
    Status = RegQueryValue(hkeyMap, pGraphicsDevice->szNtDeviceName, REG_SZ, szDeviceKey, &cbSize);
    ZwClose(hkeyMap);
    if (!NT_SUCCESS(Status))
        return Status;

    /* Open the registry key */
    RtlInitUnicodeString(&ustrKeyName, szDeviceKey);
    InitializeObjectAttributes(&ObjectAttributes,
                               &ustrKeyName,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);
    Status = ZwOpenKey((PHANDLE)&hkey, KEY_READ | KEY_SET_VALUE, &ObjectAttributes);
    if (!NT_SUCCESS(Status))
        return Status;

    *phkey = hkey;

    return Status;
}

NTSTATUS
NTAPI
UserEnumRegistryDisplaySettings(
    IN PUNICODE_STRING pustrDevice,
    OUT LPDEVMODEW pdm)
{
    HKEY hkey;
    NTSTATUS Status = UserOpenDisplaySettingsKey(&hkey, pustrDevice, 0);
    if(NT_SUCCESS(Status))
    {
        RegReadDisplaySettings(hkey, pdm);
        ZwClose(hkey);
        return STATUS_SUCCESS;
    }
    return Status;
}

NTSTATUS
APIENTRY
NtUserEnumDisplaySettings(
    IN PUNICODE_STRING pustrDevice,
    IN DWORD iModeNum,
    OUT LPDEVMODEW lpDevMode,
    IN DWORD dwFlags)
{
    UNICODE_STRING ustrDeviceUser;
    UNICODE_STRING ustrDevice;
    WCHAR awcDevice[CCHDEVICENAME];
    NTSTATUS Status;
    ULONG cbSize = 0, cbExtra = 0;
    DEVMODEW dmReg, dmDetached, *pdm;
    POINTL ptlPosition;
    BOOL bCurrent = FALSE;

    TRACE("Enter NtUserEnumDisplaySettings(%wZ, %lu, %p, 0x%lx)\n",
          pustrDevice, iModeNum, lpDevMode, dwFlags);

    _SEH2_TRY
    {
        ProbeForRead(lpDevMode, sizeof(DEVMODEW), sizeof(UCHAR));

        cbSize = lpDevMode->dmSize;
        cbExtra = lpDevMode->dmDriverExtra;

        ProbeForWrite(lpDevMode, cbSize + cbExtra, sizeof(UCHAR));
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        _SEH2_YIELD(return _SEH2_GetExceptionCode());
    }
    _SEH2_END;

    if (cbSize != sizeof(DEVMODEW))
    {
        return STATUS_BUFFER_TOO_SMALL;
    }

    if (pustrDevice)
    {
        /* Initialize destination string */
        RtlInitEmptyUnicodeString(&ustrDevice, awcDevice, sizeof(awcDevice));

        _SEH2_TRY
        {
            /* Probe the UNICODE_STRING and the buffer */
            ustrDeviceUser = ProbeForReadUnicodeString(pustrDevice);

            if (!ustrDeviceUser.Length || !ustrDeviceUser.Buffer)
                ExRaiseStatus(STATUS_NO_MEMORY);

            ProbeForRead(ustrDeviceUser.Buffer,
                         ustrDeviceUser.Length,
                         sizeof(UCHAR));

            /* Copy the string */
            RtlCopyUnicodeString(&ustrDevice, &ustrDeviceUser);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return STATUS_INVALID_PARAMETER_1);
        }
        _SEH2_END;

        pustrDevice = &ustrDevice;
    }

    /* Acquire global USER lock */
    UserEnterShared();

    if (iModeNum == ENUM_REGISTRY_SETTINGS)
    {
        /* Get the registry settings */
        Status = UserEnumRegistryDisplaySettings(pustrDevice, &dmReg);
        pdm = &dmReg;
        pdm->dmSize = sizeof(DEVMODEW);
    }
    else if (iModeNum == ENUM_CURRENT_SETTINGS || iModeNum == (DWORD)-3)
    {
        /* Get the current settings */
        Status = UserEnumCurrentDisplaySettings(pustrDevice, &pdm, &ptlPosition);
        if (Status == STATUS_INVALID_PARAMETER_1 && pustrDevice && EngpFindGraphicsDevice(pustrDevice, 0))
        {
            /* A display device that is not attached to the desktop is current in the detached mode, at its registry position */
            RtlZeroMemory(&dmDetached, sizeof(dmDetached));
            dmDetached.dmSize = sizeof(dmDetached);
            ptlPosition.x = ptlPosition.y = 0;
            if (NT_SUCCESS(UserEnumRegistryDisplaySettings(pustrDevice, &dmReg)))
                ptlPosition = dmReg.dmPosition;
            dmDetached.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT;
            pdm = &dmDetached;
            Status = STATUS_SUCCESS;
        }
        bCurrent = TRUE;
    }
    else
    {
        /* Get specified settings */
        Status = UserEnumDisplaySettings(pustrDevice, iModeNum, &pdm, dwFlags);
    }

    /* Release lock */
    UserLeave();

    /* Did we succeed? */
    if (NT_SUCCESS(Status))
    {
        /* Copy some information back */
        _SEH2_TRY
        {
            /* Output what we got */
            RtlCopyMemory(lpDevMode, pdm, min(cbSize, pdm->dmSize));

            if (bCurrent)
            {
                lpDevMode->dmFields |= DM_POSITION | DM_DISPLAYORIENTATION;
                lpDevMode->dmPosition = ptlPosition;
                lpDevMode->dmDisplayOrientation = DMDO_DEFAULT;
            }
            else
            {
                lpDevMode->dmFields |= DM_DISPLAYORIENTATION;
            }

            /* Output private/extra driver data */
            if (cbExtra > 0 && pdm->dmDriverExtra > 0)
            {
                RtlCopyMemory((PCHAR)lpDevMode + cbSize,
                              (PCHAR)pdm + pdm->dmSize,
                              min(cbExtra, pdm->dmDriverExtra));
            }
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;
    }

    return Status;
}

VOID
UserUpdateFullscreen(
    DWORD flags)
{
    if (flags & CDS_FULLSCREEN)
        gpFullscreen = gptiCurrent->ppi;
    else
        gpFullscreen = NULL;
}

static LONG
UserStoreDisplayTopology(
    _In_ PUNICODE_STRING pustrDevice,
    _In_opt_ PDEVMODEW pdm,
    _In_ BOOL bAttached,
    _In_ DWORD flags)
{
    HKEY hkey;
    NTSTATUS Status = UserOpenDisplaySettingsKey(&hkey, pustrDevice, flags & CDS_GLOBAL);

    if (!NT_SUCCESS(Status))
        return DISP_CHANGE_NOTUPDATED;
    if (pdm != NULL)
        RegWriteDisplaySettings(hkey, pdm);
    RegWriteDWORD(hkey, L"Attach.ToDesktop", bAttached ? 1 : 0);
    ZwClose(hkey);
    return DISP_CHANGE_SUCCESSFUL;
}

/* Whether a display's stored attachment differs from the desktop's displays */
static BOOL
UserDisplayTopologyChanged(VOID)
{
    UNICODE_STRING ustrName;
    PGRAPHICS_DEVICE pGraphicsDevice;
    PPDEVOBJ ppdev;
    ULONG iDevNum;
    BOOL bChanged = FALSE;

    for (iDevNum = 0; !bChanged && (pGraphicsDevice = EngpFindGraphicsDevice(NULL, iDevNum)) != NULL; iDevNum++)
    {
        RtlInitUnicodeString(&ustrName, pGraphicsDevice->szWinDeviceName);
        ppdev = EngpGetPDEV(&ustrName);
        bChanged = (ppdev != NULL) != EngpIsDisplayAttached(pGraphicsDevice);
        if (ppdev != NULL)
            PDEVOBJ_vRelease(ppdev);
    }
    return bChanged;
}

/* A detach, a display outside the desktop, or any display of a multi-display desktop.
 * Returns FALSE for an ordinary mode change of a single-display desktop. */
static BOOL
UserChangeDisplayTopology(
    _In_opt_ PUNICODE_STRING pustrDevice,
    _In_ PDEVMODEW pdm,
    _In_ DWORD flags,
    _Out_ PLONG plResult)
{
    PGRAPHICS_DEVICE pGraphicsDevice;
    PDEVMODEW newDevMode = NULL;
    UNICODE_STRING ustrPrimary;
    PPDEVOBJ ppdev;
    BOOL bOnDesktop, bDetach, bCurrent;
    LONG lResult;

    if (gpmdev == NULL)
        return FALSE;
    if (pustrDevice == NULL)
    {
        if (gpmdev->cDev == 1)
            return FALSE;
        RtlInitUnicodeString(&ustrPrimary, MultiPrimaryDisplay(gpmdev)->pGraphicsDevice->szWinDeviceName);
        pustrDevice = &ustrPrimary;
    }
    pGraphicsDevice = EngpFindGraphicsDevice(pustrDevice, 0);
    if (pGraphicsDevice == NULL)
        return FALSE;
    ppdev = EngpGetPDEV(pustrDevice);
    bOnDesktop = ppdev != NULL;
    bDetach = pdm->dmPelsWidth == 0 && pdm->dmPelsHeight == 0;
    if (!bDetach && bOnDesktop && gpmdev->cDev == 1)
    {
        PDEVOBJ_vRelease(ppdev);
        return FALSE;
    }

    if (bDetach)
    {
        if (ppdev != NULL)
            PDEVOBJ_vRelease(ppdev);
        /* TODO: make another display primary when the primary display is detached */
        if (pGraphicsDevice->StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE)
        {
            *plResult = DISP_CHANGE_BADPARAM;
            return TRUE;
        }
        lResult = DISP_CHANGE_SUCCESSFUL;
        if (!(flags & CDS_TEST) && (flags & CDS_UPDATEREGISTRY))
            lResult = UserStoreDisplayTopology(pustrDevice, NULL, FALSE, flags);
        /* FIXME: a display leaves the desktop at the next start */
        if (lResult == DISP_CHANGE_SUCCESSFUL && !(flags & (CDS_TEST | CDS_NORESET)) && bOnDesktop)
            lResult = DISP_CHANGE_RESTART;
        *plResult = lResult;
        return TRUE;
    }

    if (!LDEVOBJ_bProbeAndCaptureDevmode(pGraphicsDevice, pdm, &newDevMode, FALSE))
    {
        if (ppdev != NULL)
            PDEVOBJ_vRelease(ppdev);
        *plResult = DISP_CHANGE_BADMODE;
        return TRUE;
    }
    bCurrent = ppdev != NULL && ppdev->pdmwDev != NULL &&
               ppdev->pdmwDev->dmPelsWidth == newDevMode->dmPelsWidth &&
               ppdev->pdmwDev->dmPelsHeight == newDevMode->dmPelsHeight &&
               ppdev->pdmwDev->dmBitsPerPel == newDevMode->dmBitsPerPel &&
               ppdev->pdmwDev->dmDisplayFrequency == newDevMode->dmDisplayFrequency &&
               ppdev->pdmwDev->dmPosition.x == newDevMode->dmPosition.x &&
               ppdev->pdmwDev->dmPosition.y == newDevMode->dmPosition.y;
    if (ppdev != NULL)
        PDEVOBJ_vRelease(ppdev);
    lResult = DISP_CHANGE_SUCCESSFUL;
    if (!(flags & CDS_TEST) && (flags & CDS_UPDATEREGISTRY))
        lResult = UserStoreDisplayTopology(pustrDevice, newDevMode, TRUE, flags);
    /* FIXME: a display joins a desktop, or a multi-display desktop changes, at the next start */
    if (lResult == DISP_CHANGE_SUCCESSFUL && !(flags & (CDS_TEST | CDS_NORESET)) && !bCurrent)
        lResult = DISP_CHANGE_RESTART;
    ExFreePoolWithTag(newDevMode, GDITAG_DEVMODE);
    *plResult = lResult;
    return TRUE;
}

static
VOID
UserUpdateDisplayInfo(
    _In_ PPDEVOBJ ppdev)
{
    TEXTMETRICW tmw;

    /* Update the system metrics */
    InitMetrics();

    /* Set new size of the monitor */
    UserUpdateMonitorSize((HDEV)ppdev);

    /* Update the SERVERINFO */
    gpsi->dmLogPixels = ppdev->gdiinfo.ulLogPixelsY;
    gpsi->Planes      = ppdev->gdiinfo.cPlanes;
    gpsi->BitsPixel   = ppdev->gdiinfo.cBitsPixel;
    gpsi->BitCount    = gpsi->Planes * gpsi->BitsPixel;
    gpsi->aiSysMet[SM_CXSCREEN] = ppdev->gdiinfo.ulHorzRes;
    gpsi->aiSysMet[SM_CYSCREEN] = ppdev->gdiinfo.ulVertRes;
    if (ppdev->gdiinfo.flRaster & RC_PALETTE)
    {
        gpsi->PUSIFlags |= PUSIF_PALETTEDISPLAY;
    }
    else
    {
        gpsi->PUSIFlags &= ~PUSIF_PALETTEDISPLAY;
    }
    // Font is realized and this dc was previously set to internal DC_ATTR.
    gpsi->cxSysFontChar = IntGetCharDimensions(hSystemBM, &tmw, (DWORD*)&gpsi->cySysFontChar);
    gpsi->tmSysFont     = tmw;
}

static
VOID
UserNotifyDisplayChange(
    _In_ WORD OrigBC,
    _In_ DWORD flags)
{
    /* Remove all cursor clipping */
    UserClipCursor(NULL);

    //pdesk = IntGetActiveDesktop();
    //IntHideDesktop(pdesk);

    /* Send WM_DISPLAYCHANGE to all toplevel windows */
    UserSendNotifyMessage(HWND_BROADCAST,
                          WM_DISPLAYCHANGE,
                          gpsi->BitCount,
                          MAKELONG(gpsi->aiSysMet[SM_CXSCREEN], gpsi->aiSysMet[SM_CYSCREEN]));

    ERR("BitCount New %d Orig %d\n", gpsi->BitCount, OrigBC);

    /* Not full screen and different bit count, send messages */
    if (!(flags & CDS_FULLSCREEN) &&
        gpsi->BitCount != OrigBC)
    {
        ERR("Detect settings changed.\n");
        UserSendNotifyMessage(HWND_BROADCAST, WM_SETTINGCHANGE, 0, 0);
        UserSendNotifyMessage(HWND_BROADCAST, WM_SYSCOLORCHANGE, 0, 0);
    }

    //co_IntShowDesktop(pdesk, ppdev->gdiinfo.ulHorzRes, ppdev->gdiinfo.ulVertRes);

    UserRedrawDesktop();
}

LONG
APIENTRY
UserChangeDisplaySettings(
   PUNICODE_STRING pustrDevice,
   LPDEVMODEW pdm,
   DWORD flags,
   LPVOID lParam)
{
    DEVMODEW dm;
    LONG lResult = DISP_CHANGE_SUCCESSFUL;
    HKEY hkey;
    NTSTATUS Status;
    PPDEVOBJ ppdev;
    WORD OrigBC;
    //PDESKTOP pdesk;
    PDEVMODEW newDevMode = NULL;

    /* If no DEVMODE is given, use registry settings */
    if (!pdm)
    {
        /* Get the registry settings */
        Status = UserEnumRegistryDisplaySettings(pustrDevice, &dm);
        if (!NT_SUCCESS(Status))
        {
            ERR("Could not load registry settings\n");
            return DISP_CHANGE_BADPARAM;
        }
    }
    else if (pdm->dmSize < FIELD_OFFSET(DEVMODEW, dmICMMethod))
    {
        return DISP_CHANGE_BADMODE;
    }
    else
    {
        dm = *pdm;
    }

    /* Save original bit count */
    OrigBC = gpsi->BitCount;

    /* Check params */
    if ((dm.dmFields & (DM_PELSWIDTH | DM_PELSHEIGHT)) != (DM_PELSWIDTH | DM_PELSHEIGHT))
    {
        ERR("Devmode doesn't specify the resolution.\n");
        return DISP_CHANGE_BADMODE;
    }

    if (UserChangeDisplayTopology(pustrDevice, &dm, flags, &lResult))
        return lResult;

    /* FIXME: the stored displays of the desktop apply at the next start */
    if (!pdm && !pustrDevice && UserDisplayTopologyChanged())
        return DISP_CHANGE_RESTART;

    /* Get the PDEV */
    ppdev = EngpGetPDEV(pustrDevice);
    if (!ppdev)
    {
        ERR("Failed to get PDEV\n");
        return DISP_CHANGE_BADPARAM;
    }

    /* Fixup values */
    if (dm.dmBitsPerPel == 0 || !(dm.dmFields & DM_BITSPERPEL))
    {
        dm.dmBitsPerPel = ppdev->pdmwDev->dmBitsPerPel;
        dm.dmFields |= DM_BITSPERPEL;
    }

    if ((dm.dmFields & DM_DISPLAYFREQUENCY) && (dm.dmDisplayFrequency == 0))
        dm.dmDisplayFrequency = ppdev->pdmwDev->dmDisplayFrequency;

    PDEVOBJ_vRefreshModeList(ppdev);

    /* Look for the requested DEVMODE */
    if (!LDEVOBJ_bProbeAndCaptureDevmode(ppdev->pGraphicsDevice, &dm, &newDevMode, FALSE))
    {
        ERR("Could not find a matching DEVMODE\n");
        lResult = DISP_CHANGE_BADMODE;
        goto leave;
    }
    else if (flags & CDS_TEST)
    {
        /* It's possible, go ahead! */
        lResult = DISP_CHANGE_SUCCESSFUL;
        goto leave;
    }

    /* Shall we update the registry? */
    if (flags & CDS_UPDATEREGISTRY)
    {
        /* Open the local or global settings key */
        Status = UserOpenDisplaySettingsKey(&hkey, pustrDevice, flags & CDS_GLOBAL);
        if (NT_SUCCESS(Status))
        {
            /* Store the settings */
            RegWriteDisplaySettings(hkey, newDevMode);

            /* Close the registry key */
            ZwClose(hkey);
        }
        else
        {
            ERR("Could not open registry key\n");
            lResult = DISP_CHANGE_NOTUPDATED;
        }
    }

    /* Check if DEVMODE matches the current mode */
    if (newDevMode->dmSize == ppdev->pdmwDev->dmSize &&
        RtlCompareMemory(newDevMode, ppdev->pdmwDev, newDevMode->dmSize) == newDevMode->dmSize &&
        !(flags & CDS_RESET))
    {
        ERR("DEVMODE matches, nothing to do\n");
        goto leave;
    }

    /* Shall we apply the settings? */
    if (!(flags & CDS_NORESET) &&
        ((flags & CDS_RESET) ||
         newDevMode->dmPelsWidth != ppdev->pdmwDev->dmPelsWidth ||
         newDevMode->dmPelsHeight != ppdev->pdmwDev->dmPelsHeight ||
         newDevMode->dmBitsPerPel != ppdev->pdmwDev->dmBitsPerPel ||
         newDevMode->dmDisplayFrequency != ppdev->pdmwDev->dmDisplayFrequency ||
         newDevMode->dmDisplayFlags != ppdev->pdmwDev->dmDisplayFlags ||
         newDevMode->dmPanningWidth != ppdev->pdmwDev->dmPanningWidth ||
         newDevMode->dmPanningHeight != ppdev->pdmwDev->dmPanningHeight))
    {
        ULONG_PTR ulResult;
        PVOID pvOldCursor;

        /* Remove mouse pointer */
        pvOldCursor = UserSetCursor(NULL, TRUE);

        IntCompositionOnDisplayChangeBegin();

        /* Do the mode switch */
        ulResult = PDEVOBJ_bSwitchMode(ppdev, newDevMode);

        IntCompositionOnDisplayChangeEnd();

        /* Restore mouse pointer, no hooks called */
        pvOldCursor = UserSetCursor(pvOldCursor, TRUE);
        ASSERT(pvOldCursor == NULL);

        /* Check for success or failure */
        if (!ulResult)
        {
            /* Setting mode failed */
            ERR("Failed to set mode\n");

            /* Set the correct return value */
            if ((flags & CDS_UPDATEREGISTRY) && (lResult != DISP_CHANGE_NOTUPDATED))
                lResult = DISP_CHANGE_RESTART;
            else
                lResult = DISP_CHANGE_FAILED;
        }
        else
        {
            /* Setting mode succeeded */
            lResult = DISP_CHANGE_SUCCESSFUL;
            ExFreePoolWithTag(ppdev->pdmwDev, GDITAG_DEVMODE);
            ppdev->pdmwDev = newDevMode;

            UserUpdateFullscreen(flags);

            UserUpdateDisplayInfo(ppdev);
        }

        /*
         * Refresh the display on success and even on failure,
         * since the display may have been messed up.
         */
        UserNotifyDisplayChange(OrigBC, flags);
    }

leave:
    if (newDevMode && newDevMode != ppdev->pdmwDev)
        ExFreePoolWithTag(newDevMode, GDITAG_DEVMODE);

    /* Release the PDEV */
    PDEVOBJ_vRelease(ppdev);

    return lResult;
}

static
BOOL
UserIsExternalDisplay(
    _In_ PGRAPHICS_DEVICE pGraphicsDevice,
    _In_ PPDEVOBJ ppdev)
{
    return pGraphicsDevice != ppdev->pGraphicsDevice &&
           pGraphicsDevice != ppdev->pGraphicsDevice->pVgaDevice &&
           !(pGraphicsDevice->StateFlags & (DISPLAY_DEVICE_MIRRORING_DRIVER | DISPLAY_DEVICE_ATTACHED_TO_DESKTOP));
}

static
PDEVMODEW
UserSelectExternalDisplayMode(
    _In_ PGRAPHICS_DEVICE pGraphicsDevice,
    _In_ PPDEVOBJ ppdev)
{
    UNICODE_STRING ustrDevice;
    PDEVMODEW pdm = NULL;
    DEVMODEW dm;
    ULONG i;

    if (!EngpBuildDetachedModeList(pGraphicsDevice, TRUE) || pGraphicsDevice->cDevModes == 0)
        return NULL;

    RtlInitUnicodeString(&ustrDevice, pGraphicsDevice->szWinDeviceName);
    if (NT_SUCCESS(UserEnumRegistryDisplaySettings(&ustrDevice, &dm)) &&
        dm.dmPelsWidth != 0 && dm.dmPelsHeight != 0)
    {
        dm.dmSize = sizeof(dm);
        if (LDEVOBJ_bProbeAndCaptureDevmode(pGraphicsDevice, &dm, &pdm, FALSE))
            return pdm;
    }

    for (i = 0; i < pGraphicsDevice->cDevModes; i++)
    {
        if (pGraphicsDevice->pDevModeList[i].pdm->dmBitsPerPel == ppdev->pdmwDev->dmBitsPerPel)
            break;
    }
    if (i == pGraphicsDevice->cDevModes)
        i = 0;

    RtlZeroMemory(&dm, sizeof(dm));
    dm.dmSize = sizeof(dm);
    dm.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL | DM_DISPLAYFREQUENCY;
    dm.dmPelsWidth = pGraphicsDevice->pDevModeList[i].pdm->dmPelsWidth;
    dm.dmPelsHeight = pGraphicsDevice->pDevModeList[i].pdm->dmPelsHeight;
    dm.dmBitsPerPel = pGraphicsDevice->pDevModeList[i].pdm->dmBitsPerPel;
    dm.dmDisplayFrequency = pGraphicsDevice->pDevModeList[i].pdm->dmDisplayFrequency;
    if (!LDEVOBJ_bProbeAndCaptureDevmode(pGraphicsDevice, &dm, &pdm, FALSE))
        return NULL;
    return pdm;
}

static
BOOL
UserMoveDesktopDisplay(
    _In_ PPDEVOBJ ppdev,
    _In_opt_ PGRAPHICS_DEVICE pGraphicsDevice,
    _In_opt_ PDEVMODEW pdm)
{
    WORD OrigBC = gpsi->BitCount;
    PVOID pvOldCursor;
    BOOL bRet;

    pvOldCursor = UserSetCursor(NULL, TRUE);
    IntCompositionOnDisplayChangeBegin();
    if (pGraphicsDevice)
        bRet = PDEVOBJ_bMoveToDisplay(ppdev, pGraphicsDevice, pdm);
    else
        bRet = PDEVOBJ_bReturnToDetachedDisplay(ppdev);
    IntCompositionOnDisplayChangeEnd();
    pvOldCursor = UserSetCursor(pvOldCursor, TRUE);
    ASSERT(pvOldCursor == NULL);

    if (!bRet)
        return FALSE;

    UserUpdateDisplayInfo(ppdev);
    UserNotifyDisplayChange(OrigBC, 0);
    return TRUE;
}

static
LONG
UserSetDisplayTopology(
    _In_ UINT32 Topology,
    _In_ BOOL bApply)
{
    PGRAPHICS_DEVICE pGraphicsDevice = NULL;
    PDEVMODEW pdm;
    PPDEVOBJ ppdev;
    LONG lRet = ERROR_SUCCESS;
    ULONG i;

    ppdev = EngpGetPDEV(NULL);
    if (!ppdev || !ppdev->pGraphicsDevice || gpmdev->cDev != 1)
    {
        /* TODO: topologies of a desktop that spans several displays */
        lRet = ppdev ? ERROR_NOT_SUPPORTED : ERROR_GEN_FAILURE;
        goto leave;
    }

    if (Topology == SDC_TOPOLOGY_INTERNAL)
    {
        if (bApply && gppdevDetached && !UserMoveDesktopDisplay(ppdev, NULL, NULL))
            lRet = ERROR_GEN_FAILURE;
        goto leave;
    }

    if (Topology != SDC_TOPOLOGY_EXTERNAL)
    {
        /* TODO: clone and extend the desktop to other displays */
        EngpUpdateGraphicsDeviceList();
        for (i = 0; (pGraphicsDevice = EngpFindGraphicsDevice(NULL, i)) != NULL; i++)
        {
            if (UserIsExternalDisplay(pGraphicsDevice, ppdev))
                break;
        }
        lRet = (pGraphicsDevice || gppdevDetached) ? ERROR_NOT_SUPPORTED : ERROR_GEN_FAILURE;
        goto leave;
    }

    if (gppdevDetached)
        goto leave;

    EngpUpdateGraphicsDeviceList();
    for (i = 0; (pGraphicsDevice = EngpFindGraphicsDevice(NULL, i)) != NULL; i++)
    {
        if (UserIsExternalDisplay(pGraphicsDevice, ppdev))
            break;
    }
    if (!pGraphicsDevice)
    {
        lRet = ERROR_GEN_FAILURE;
        goto leave;
    }
    if (!bApply)
        goto leave;

    pdm = UserSelectExternalDisplayMode(pGraphicsDevice, ppdev);
    if (!pdm || !UserMoveDesktopDisplay(ppdev, pGraphicsDevice, pdm))
        lRet = ERROR_GEN_FAILURE;
    if (pdm)
        ExFreePoolWithTag(pdm, GDITAG_DEVMODE);

leave:
    if (ppdev)
        PDEVOBJ_vRelease(ppdev);
    return lRet;
}

LONG
APIENTRY
NtUserSetDisplayConfig(
    _In_ UINT32 numPathArrayElements,
    _In_reads_opt_(numPathArrayElements) DISPLAYCONFIG_PATH_INFO *pathArray,
    _In_ UINT32 numModeInfoArrayElements,
    _In_reads_opt_(numModeInfoArrayElements) DISPLAYCONFIG_MODE_INFO *modeInfoArray,
    _In_ UINT32 flags)
{
    const UINT32 TopologyFlags = SDC_TOPOLOGY_INTERNAL | SDC_TOPOLOGY_CLONE | SDC_TOPOLOGY_EXTEND | SDC_TOPOLOGY_EXTERNAL;
    UINT32 Topology = flags & TopologyFlags;
    LONG lRet;

    if (!(flags & (SDC_APPLY | SDC_VALIDATE)) || ((flags & SDC_APPLY) && (flags & SDC_VALIDATE)))
        return ERROR_INVALID_PARAMETER;
    if (numPathArrayElements || pathArray || numModeInfoArrayElements || modeInfoArray)
        return ERROR_NOT_SUPPORTED;
    if (!Topology || (Topology & (Topology - 1)))
        return ERROR_INVALID_PARAMETER;

    if (IntIsJobUiLimited(JOB_OBJECT_UILIMIT_DISPLAYSETTINGS))
        return ERROR_ACCESS_DENIED;

    UserEnterExclusive();
    if (UserGetRemoteDisplayDevice())
        lRet = ERROR_SUCCESS;
    else
        lRet = UserSetDisplayTopology(Topology, (flags & SDC_APPLY) != 0);
    UserLeave();
    return lRet;
}

static volatile LONG gUserDisplayConfigurationPending;
static volatile LONG gUserDisplayDetachPending;

static
VOID
UserQueueDisplayConfigurationWorker(VOID)
{
    PUSER_SESSION Session;
    PTHREADINFO DesktopThread;

    /* The display driver reports from any thread, and the user lock may be held by one that waits for that driver */
    Session = ReadPointerAcquire((PVOID volatile *)&gpDisplaySession);
    if (Session == NULL)
        return;

    DesktopThread = ReadPointerAcquire((PVOID volatile *)&Session->DesktopThread);
    if (DesktopThread != NULL)
        KeSetEvent(DesktopThread->pEventQueueServer, IO_NO_INCREMENT, FALSE);
}

VOID
NTAPI
UserQueueDisplayConfigurationUpdate(VOID)
{
    InterlockedExchange(&gUserDisplayConfigurationPending, 1);
    UserQueueDisplayConfigurationWorker();
}

VOID
NTAPI
UserQueueDisplayDetach(VOID)
{
    InterlockedExchange(&gUserDisplayDetachPending, 1);
    UserQueueDisplayConfigurationWorker();
}

VOID
NTAPI
co_UserProcessDisplayConfigurationUpdate(VOID)
{
    PGRAPHICS_DEVICE pGraphicsDevice;
    VIDEO_MODE_INFORMATION ModeInfo;
    UNICODE_STRING ustrDevice;
    DEVMODEW dm;
    PPDEVOBJ ppdev;
    ULONG iDevNum, cbReturned;
    LONG lResult;
    BOOL bEventDevice;

    if (InterlockedExchange(&gUserDisplayDetachPending, 0) != 0)
    {
        lResult = UserSetDisplayTopology(SDC_TOPOLOGY_INTERNAL, TRUE);
        if (lResult != ERROR_SUCCESS)
            ERR("Could not move the desktop off a removed display: %ld\n", lResult);
        EngpReleaseRemovedGraphicsDevices();
    }

    if (InterlockedExchange(&gUserDisplayConfigurationPending, 0) == 0)
        return;

    /* A source that starts driving an output has a display device that may be new to win32k */
    EngpUpdateGraphicsDeviceList();
    for (iDevNum = 0; (pGraphicsDevice = EngpFindGraphicsDevice(NULL, iDevNum)) != NULL; ++iDevNum)
    {
        bEventDevice = InterlockedExchange(&pGraphicsDevice->lMonitorEvent, 0) != 0;
        RtlInitUnicodeString(&ustrDevice, pGraphicsDevice->szWinDeviceName);
        ppdev = EngpGetPDEV(&ustrDevice);
        if (ppdev == NULL)
        {
            /* A detached display device lists the modes of whatever output it drives now */
            (VOID)EngpBuildDetachedModeList(pGraphicsDevice, TRUE);
            continue;
        }

        RtlZeroMemory(&ModeInfo, sizeof(ModeInfo));
        if (EngDeviceIoControl((HANDLE)pGraphicsDevice->DeviceObject,
                               IOCTL_VIDEO_QUERY_CURRENT_MODE,
                               NULL, 0,
                               &ModeInfo, sizeof(ModeInfo),
                               &cbReturned) != ERROR_SUCCESS ||
            cbReturned < sizeof(ModeInfo) ||
            ModeInfo.VisScreenWidth == 0 || ModeInfo.VisScreenHeight == 0)
        {
            PDEVOBJ_vRelease(ppdev);
            continue;
        }

        if (!bEventDevice &&
            ModeInfo.VisScreenWidth == ppdev->pdmwDev->dmPelsWidth &&
            ModeInfo.VisScreenHeight == ppdev->pdmwDev->dmPelsHeight &&
            (ModeInfo.Frequency == 0 || ModeInfo.Frequency == ppdev->pdmwDev->dmDisplayFrequency))
        {
            PDEVOBJ_vRelease(ppdev);
            continue;
        }

        RtlZeroMemory(&dm, sizeof(dm));
        dm.dmSize = sizeof(dm);
        dm.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL;
        dm.dmPelsWidth = ModeInfo.VisScreenWidth;
        dm.dmPelsHeight = ModeInfo.VisScreenHeight;
        dm.dmBitsPerPel = ppdev->pdmwDev->dmBitsPerPel;
        if (ModeInfo.Frequency != 0)
        {
            dm.dmFields |= DM_DISPLAYFREQUENCY;
            dm.dmDisplayFrequency = ModeInfo.Frequency;
        }
        PDEVOBJ_vRefreshModeList(ppdev);
        PDEVOBJ_vRelease(ppdev);

        lResult = UserChangeDisplaySettings(&ustrDevice, &dm, CDS_RESET, NULL);
        if (lResult != DISP_CHANGE_SUCCESSFUL)
        {
            ERR("Display %wZ: monitor change to %lux%lu failed (%ld)\n",
                &ustrDevice, dm.dmPelsWidth, dm.dmPelsHeight, lResult);
        }
    }
}

VOID
UserDisplayNotifyShutdown(
    PPROCESSINFO ppiCurrent)
{
    if (ppiCurrent == gpFullscreen)
    {
        UserChangeDisplaySettings(NULL, NULL, 0, NULL);
        if (gpFullscreen)
            ERR("Failed to restore display mode!\n");
    }
}

LONG
APIENTRY
NtUserChangeDisplaySettings(
    PUNICODE_STRING pustrDevice,
    LPDEVMODEW lpDevMode,
    DWORD dwflags,
    LPVOID lParam)
{
    WCHAR awcDevice[CCHDEVICENAME];
    UNICODE_STRING ustrDevice;
    DEVMODEW dmLocal;
    LONG lRet;

    if (IntIsJobUiLimited(JOB_OBJECT_UILIMIT_DISPLAYSETTINGS))
    {
        EngSetLastError(ERROR_ACCESS_DENIED);
        return DISP_CHANGE_FAILED;
    }

    /* Check arguments */
    if ((dwflags != CDS_VIDEOPARAMETERS) && (lParam != NULL))
    {
        EngSetLastError(ERROR_INVALID_PARAMETER);
        return DISP_CHANGE_BADPARAM;
    }

    /* Check flags */
    if ((dwflags & (CDS_GLOBAL|CDS_NORESET)) && !(dwflags & CDS_UPDATEREGISTRY))
    {
        return DISP_CHANGE_BADFLAGS;
    }

    if ((dwflags & CDS_RESET) && (dwflags & CDS_NORESET))
    {
        return DISP_CHANGE_BADFLAGS;
    }

    /* Copy the device name */
    if (pustrDevice)
    {
        /* Initialize destination string */
        RtlInitEmptyUnicodeString(&ustrDevice, awcDevice, sizeof(awcDevice));

        _SEH2_TRY
        {
            /* Probe the UNICODE_STRING and the buffer */
            ProbeForRead(pustrDevice, sizeof(UNICODE_STRING), 1);
            ProbeForRead(pustrDevice->Buffer, pustrDevice->Length, 1);

            /* Copy the string */
            RtlCopyUnicodeString(&ustrDevice, pustrDevice);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            /* Set and return error */
            SetLastNtError(_SEH2_GetExceptionCode());
            _SEH2_YIELD(return DISP_CHANGE_BADPARAM);
        }
        _SEH2_END

        pustrDevice = &ustrDevice;
   }

    /* Copy devmode */
    if (lpDevMode)
    {
        _SEH2_TRY
        {
            /* Probe the size field of the structure */
            ProbeForRead(lpDevMode, sizeof(dmLocal.dmSize), 1);

            /* Calculate usable size */
            dmLocal.dmSize = min(sizeof(dmLocal), lpDevMode->dmSize);

            /* Probe and copy the full DEVMODE */
            ProbeForRead(lpDevMode, dmLocal.dmSize, 1);
            RtlCopyMemory(&dmLocal, lpDevMode, dmLocal.dmSize);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            /* Set and return error */
            SetLastNtError(_SEH2_GetExceptionCode());
            _SEH2_YIELD(return DISP_CHANGE_BADPARAM);
        }
        _SEH2_END

        /* Check for extra parameters */
        if (dmLocal.dmDriverExtra > 0)
        {
            /* FIXME: TODO */
            ERR("lpDevMode->dmDriverExtra is IGNORED!\n");
            dmLocal.dmDriverExtra = 0;
        }

        /* Use the local structure */
        lpDevMode = &dmLocal;
    }

    // FIXME: Copy videoparameters

    /* Acquire global USER lock */
    UserEnterExclusive();

    /* A remote session keeps the display of its client */
    if (UserGetRemoteDisplayDevice())
        lRet = DISP_CHANGE_SUCCESSFUL;
    else
        lRet = UserChangeDisplaySettings(pustrDevice, lpDevMode, dwflags, NULL);

    /* Release lock */
    UserLeave();

    return lRet;
}
