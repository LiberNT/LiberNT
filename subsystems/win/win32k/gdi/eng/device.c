/*
 * COPYRIGHT:         See COPYING in the top level directory
 * PROJECT:           ReactOS kernel
 * PURPOSE:           GDI Driver Device Functions
 * FILE:              win32ss/gdi/eng/device.c
 * PROGRAMER:         Jason Filby
 *                    Timo Kreuzer
 */

#include <win32k.h>
#include <ntddvdeo.h>
#include "drivers/wddm/wddm_bridge.h"

DBG_DEFAULT_CHANNEL(EngDev);

static PGRAPHICS_DEVICE gpPrimaryGraphicsDevice;
static PGRAPHICS_DEVICE gpVgaGraphicsDevice;

static PGRAPHICS_DEVICE gpGraphicsDeviceFirst = NULL;
static PGRAPHICS_DEVICE gpGraphicsDeviceLast = NULL;
static HSEMAPHORE ghsemGraphicsDeviceList;
static ULONG giDevNum = 1;

typedef struct _GRAPHICS_DEVICE_NOTIFY
{
    LIST_ENTRY ListEntry;
    PGRAPHICS_DEVICE pGraphicsDevice;
    PVOID NotificationEntry;
    BOOLEAN bRemoved;
} GRAPHICS_DEVICE_NOTIFY, *PGRAPHICS_DEVICE_NOTIFY;

static PDRIVER_OBJECT gpGraphicsDriverObject;
static LIST_ENTRY gGraphicsDeviceNotifyList;
static LIST_ENTRY gGraphicsDeviceNotifyRetired;

static const GUID GraphicsDeviceQueryRemove =
    { 0xcb3a4006, 0x46f0, 0x11d0, { 0xb0, 0x8f, 0x00, 0x60, 0x97, 0x13, 0x05, 0x3f } };
static const GUID GraphicsDeviceRemoveComplete =
    { 0xcb3a4008, 0x46f0, 0x11d0, { 0xb0, 0x8f, 0x00, 0x60, 0x97, 0x13, 0x05, 0x3f } };

CODE_SEG("INIT")
NTSTATUS
NTAPI
InitDeviceImpl(
    _In_ PDRIVER_OBJECT DriverObject)
{
    ghsemGraphicsDeviceList = EngCreateSemaphore();
    if (!ghsemGraphicsDeviceList)
        return STATUS_INSUFFICIENT_RESOURCES;

    gpGraphicsDriverObject = DriverObject;
    InitializeListHead(&gGraphicsDeviceNotifyList);
    InitializeListHead(&gGraphicsDeviceNotifyRetired);
    return STATUS_SUCCESS;
}

VOID
NTAPI
FreeDeviceImpl(VOID)
{
    ASSERT(gpGraphicsDeviceFirst == NULL);
    ASSERT(IsListEmpty(&gGraphicsDeviceNotifyList));
    if (ghsemGraphicsDeviceList)
    {
        EngDeleteSemaphore(ghsemGraphicsDeviceList);
        ghsemGraphicsDeviceList = NULL;
    }
}

static const WCHAR gszDisconnectedDisplayNtName[] = L"\\Device\\DisconnectedDisplay";

/* The disconnected display is the only graphics device that has no video device object */
static
BOOLEAN
EngpIsDisconnectedGraphicsDevice(
    _In_ PGRAPHICS_DEVICE pGraphicsDevice)
{
    return pGraphicsDevice->DeviceObject == NULL;
}

static
BOOLEAN
EngpIsConsoleSession(VOID)
{
    PUSER_SESSION DisplaySession = ReadPointerAcquire((PVOID volatile *)&gpDisplaySession);

    return DisplaySession != NULL && DisplaySession == UserGetCurrentSession();
}

static
BOOLEAN
EngpHasVgaDriver(
    _In_ PGRAPHICS_DEVICE pGraphicsDevice)
{
    WCHAR awcDeviceKey[256], awcServiceName[100];
    PWSTR lastBkSlash;
    NTSTATUS Status;
    ULONG cbValue;
    HKEY hkey;

    if (EngpIsDisconnectedGraphicsDevice(pGraphicsDevice))
        return FALSE;

    /* Open the key for the adapters */
    Status = RegOpenKey(L"\\Registry\\Machine\\HARDWARE\\DEVICEMAP\\VIDEO", &hkey);
    if (!NT_SUCCESS(Status))
    {
        ERR("Could not open HARDWARE\\DEVICEMAP\\VIDEO registry key: 0x%08lx\n", Status);
        return FALSE;
    }

    /* Read the name of the device key */
    cbValue = sizeof(awcDeviceKey);
    Status = RegQueryValue(hkey, pGraphicsDevice->szNtDeviceName, REG_SZ, awcDeviceKey, &cbValue);
    ZwClose(hkey);
    if (!NT_SUCCESS(Status))
    {
        ERR("Could not read '%S' registry value: 0x%08lx\n", Status);
        return FALSE;
    }

    /* Replace 'DeviceN' by 'Video' */
    lastBkSlash = wcsrchr(awcDeviceKey, L'\\');
    if (!lastBkSlash)
    {
        ERR("Invalid registry key '%S'\n", lastBkSlash);
        return FALSE;
    }
    if (!NT_SUCCESS(RtlStringCchCopyW(lastBkSlash + 1,
                                      ARRAYSIZE(awcDeviceKey) - (lastBkSlash + 1 - awcDeviceKey),
                                      L"Video")))
    {
        ERR("Failed to add 'Video' to registry key '%S'\n", awcDeviceKey);
        return FALSE;
    }

    /* Open device key */
    Status = RegOpenKey(awcDeviceKey, &hkey);
    if (!NT_SUCCESS(Status))
    {
        ERR("Could not open %S registry key: 0x%08lx\n", awcDeviceKey, Status);
        return FALSE;
    }

    /* Read service name */
    cbValue = sizeof(awcServiceName);
    Status = RegQueryValue(hkey, L"Service", REG_SZ, awcServiceName, &cbValue);
    ZwClose(hkey);
    if (!NT_SUCCESS(Status))
    {
        ERR("Could not read Service registry value in %S: 0x%08lx\n", awcDeviceKey, Status);
        return FALSE;
    }

    /* Device is using VGA driver if service name is 'VGASave' (case insensitive) */
    return (_wcsicmp(awcServiceName, L"VGASave") == 0);
}

/*
 * Add a device to gpGraphicsDeviceFirst/gpGraphicsDeviceLast list (if not already present).
 */
_Requires_lock_held_(ghsemGraphicsDeviceList)
static
VOID
EngpLinkGraphicsDevice(
    _In_ PGRAPHICS_DEVICE pToAdd)
{
    PGRAPHICS_DEVICE pGraphicsDevice;

    TRACE("EngLinkGraphicsDevice(%p)\n", pToAdd);

    /* Search if device is not already linked */
    for (pGraphicsDevice = gpGraphicsDeviceFirst;
         pGraphicsDevice;
         pGraphicsDevice = pGraphicsDevice->pNextGraphicsDevice)
    {
        if (pGraphicsDevice == pToAdd)
            return;
    }

    pToAdd->pNextGraphicsDevice = NULL;
    if (gpGraphicsDeviceLast)
        gpGraphicsDeviceLast->pNextGraphicsDevice = pToAdd;
    gpGraphicsDeviceLast = pToAdd;
    if (!gpGraphicsDeviceFirst)
        gpGraphicsDeviceFirst = pToAdd;
}

/*
 * Remove a device from gpGraphicsDeviceFirst/gpGraphicsDeviceLast list.
 */
_Requires_lock_held_(ghsemGraphicsDeviceList)
static
VOID
EngpUnlinkGraphicsDevice(
    _In_ PGRAPHICS_DEVICE pToDelete)
{
    PGRAPHICS_DEVICE pPrevGraphicsDevice = NULL;
    PGRAPHICS_DEVICE pGraphicsDevice = gpGraphicsDeviceFirst;

    TRACE("EngpUnlinkGraphicsDevice('%S')\n", pToDelete->szNtDeviceName);

    while (pGraphicsDevice)
    {
        if (pGraphicsDevice != pToDelete)
        {
            /* Keep current device */
            pPrevGraphicsDevice = pGraphicsDevice;
            pGraphicsDevice = pGraphicsDevice->pNextGraphicsDevice;
        }
        else
        {
            /* At first, link again associated VGA Device */
            if (pGraphicsDevice->pVgaDevice)
                EngpLinkGraphicsDevice(pGraphicsDevice->pVgaDevice);

            /* We need to remove current device */
            pGraphicsDevice = pGraphicsDevice->pNextGraphicsDevice;

            /* Unlink chain */
            if (!pPrevGraphicsDevice)
                gpGraphicsDeviceFirst = pToDelete->pNextGraphicsDevice;
            else
                pPrevGraphicsDevice->pNextGraphicsDevice = pToDelete->pNextGraphicsDevice;
            if (gpGraphicsDeviceLast == pToDelete)
                gpGraphicsDeviceLast = pPrevGraphicsDevice;
        }
    }
}

static
BOOLEAN
EngpIsGraphicsDeviceRegistered(
    _In_ PCWSTR pwszNtDeviceName)
{
    PGRAPHICS_DEVICE pGraphicsDevice;
    BOOLEAN bFound = FALSE;

    EngAcquireSemaphoreShared(ghsemGraphicsDeviceList);

    for (pGraphicsDevice = gpGraphicsDeviceFirst;
         pGraphicsDevice && !bFound;
         pGraphicsDevice = pGraphicsDevice->pNextGraphicsDevice)
    {
        bFound = _wcsicmp(pGraphicsDevice->szNtDeviceName, pwszNtDeviceName) == 0 ||
                 (pGraphicsDevice->pVgaDevice &&
                  _wcsicmp(pGraphicsDevice->pVgaDevice->szNtDeviceName, pwszNtDeviceName) == 0);
    }

    EngReleaseSemaphore(ghsemGraphicsDeviceList);

    return bFound;
}

static
VOID
EngpReleaseRetiredNotifications(VOID)
{
    PGRAPHICS_DEVICE_NOTIFY pNotify;

    ASSERT(!UserIsEntered());
    for (;;)
    {
        pNotify = NULL;
        EngAcquireSemaphore(ghsemGraphicsDeviceList);
        if (!IsListEmpty(&gGraphicsDeviceNotifyRetired))
            pNotify = CONTAINING_RECORD(RemoveHeadList(&gGraphicsDeviceNotifyRetired), GRAPHICS_DEVICE_NOTIFY, ListEntry);
        EngReleaseSemaphore(ghsemGraphicsDeviceList);
        if (!pNotify)
            break;
        ASSERT(pNotify->pGraphicsDevice == NULL);
        IoUnregisterPlugPlayNotificationEx(pNotify->NotificationEntry);
        ExFreePoolWithTag(pNotify, GDITAG_GDEVICE);
    }
}

static
VOID
EngpFreeGraphicsDevice(
    _In_ PGRAPHICS_DEVICE pGraphicsDevice)
{
    PGRAPHICS_DEVICE_NOTIFY pNotify;
    PDEVMODEINFO pdminfo, pdmiNext;
    PLIST_ENTRY pEntry;
    ULONG i;

    EngAcquireSemaphore(ghsemGraphicsDeviceList);
    for (pEntry = gGraphicsDeviceNotifyList.Flink; pEntry != &gGraphicsDeviceNotifyList; pEntry = pEntry->Flink)
    {
        pNotify = CONTAINING_RECORD(pEntry, GRAPHICS_DEVICE_NOTIFY, ListEntry);
        if (pNotify->pGraphicsDevice == pGraphicsDevice)
        {
            RemoveEntryList(&pNotify->ListEntry);
            pNotify->pGraphicsDevice = NULL;
            InsertTailList(&gGraphicsDeviceNotifyRetired, &pNotify->ListEntry);
            break;
        }
    }
    EngReleaseSemaphore(ghsemGraphicsDeviceList);

    for (pdminfo = pGraphicsDevice->pdevmodeInfo; pdminfo; pdminfo = pdmiNext)
    {
        pdmiNext = pdminfo->pdmiNext;
        ExFreePoolWithTag(pdminfo, GDITAG_DEVMODE);
    }
    if (pGraphicsDevice->pDevModeList)
        ExFreePoolWithTag(pGraphicsDevice->pDevModeList, GDITAG_GDEVICE);

    for (i = 0; i < pGraphicsDevice->dwMonCnt; i++)
        ObDereferenceObject(pGraphicsDevice->pvMonDev[i].pdo);
    if (pGraphicsDevice->pvMonDev)
        ExFreePoolWithTag(pGraphicsDevice->pvMonDev, GDITAG_GDEVICE);

    if (pGraphicsDevice->PhysDeviceHandle)
        ObDereferenceObject(pGraphicsDevice->PhysDeviceHandle);
    ASSERT(EngpIsDisconnectedGraphicsDevice(pGraphicsDevice) == (pGraphicsDevice->FileObject == NULL));
    if (pGraphicsDevice->FileObject != NULL)
        ObDereferenceObject(pGraphicsDevice->FileObject);
    ExFreePoolWithTag(pGraphicsDevice->pDiplayDrivers, GDITAG_DRVSUP);
    ExFreePoolWithTag(pGraphicsDevice, GDITAG_GDEVICE);
}

static
NTSTATUS
NTAPI
EngpGraphicsDeviceNotification(
    _In_ PVOID NotificationStructure,
    _Inout_opt_ PVOID Context)
{
    PTARGET_DEVICE_REMOVAL_NOTIFICATION pNotification = NotificationStructure;
    PGRAPHICS_DEVICE_NOTIFY pNotify = Context;
    PGRAPHICS_DEVICE pGraphicsDevice;
    WCHAR szWinDeviceName[CCHDEVICENAME / 2] = L"";
    BOOLEAN bQueryRemove;
    BOOLEAN bRemove = FALSE;

    bQueryRemove = IsEqualGUID(&pNotification->Event, &GraphicsDeviceQueryRemove);
    if (!pNotify || (!bQueryRemove && !IsEqualGUID(&pNotification->Event, &GraphicsDeviceRemoveComplete)))
        return STATUS_SUCCESS;

    UserEnterExclusive();
    EngAcquireSemaphore(ghsemGraphicsDeviceList);
    pGraphicsDevice = pNotify->pGraphicsDevice;
    if (pGraphicsDevice)
        RtlCopyMemory(szWinDeviceName, pGraphicsDevice->szWinDeviceName, sizeof(szWinDeviceName));
    if (pGraphicsDevice && gppdevDetached && gpmdev && gpmdev->ppdevGlobal->pGraphicsDevice == pGraphicsDevice)
    {
        TRACE("Moving the desktop off the removed graphics device %S\n", szWinDeviceName);
        if (!bQueryRemove)
            pNotify->bRemoved = TRUE;
        UserQueueDisplayDetach();
    }
    else if (pGraphicsDevice &&
        !(pGraphicsDevice->StateFlags & (DISPLAY_DEVICE_ATTACHED_TO_DESKTOP | DISPLAY_DEVICE_PRIMARY_DEVICE)) &&
        pGraphicsDevice != gpPrimaryGraphicsDevice &&
        pGraphicsDevice != gpVgaGraphicsDevice &&
        !pGraphicsDevice->pVgaDevice &&
        !PDEVOBJ_bGraphicsDeviceInUse(pGraphicsDevice))
    {
        TRACE("Removing graphics device %S (%S)\n", szWinDeviceName, pGraphicsDevice->szNtDeviceName);
        EngpUnlinkGraphicsDevice(pGraphicsDevice);
        bRemove = TRUE;
    }
    EngReleaseSemaphore(ghsemGraphicsDeviceList);
    UserLeave();

    if (bRemove)
    {
        EngpFreeGraphicsDevice(pGraphicsDevice);
        EngpReleaseRetiredNotifications();
        return STATUS_SUCCESS;
    }

    if (pGraphicsDevice && bQueryRemove)
    {
        TRACE("Graphics device %S is in use\n", szWinDeviceName);
        return STATUS_UNSUCCESSFUL;
    }

    if (pGraphicsDevice)
        ERR("Graphics device %S was removed while in use\n", szWinDeviceName);
    return STATUS_SUCCESS;
}

VOID
NTAPI
EngpReleaseAllGraphicsDevices(VOID)
{
    PGRAPHICS_DEVICE pGraphicsDevice;

    for (;;)
    {
        EngAcquireSemaphore(ghsemGraphicsDeviceList);
        pGraphicsDevice = gpGraphicsDeviceFirst;
        if (pGraphicsDevice)
            EngpUnlinkGraphicsDevice(pGraphicsDevice);
        EngReleaseSemaphore(ghsemGraphicsDeviceList);
        if (!pGraphicsDevice)
            break;

        EngpFreeGraphicsDevice(pGraphicsDevice);
    }
    gpPrimaryGraphicsDevice = NULL;
    gpVgaGraphicsDevice = NULL;
    EngpReleaseRetiredNotifications();
}

VOID
NTAPI
EngpReleaseRemovedGraphicsDevices(VOID)
{
    PGRAPHICS_DEVICE_NOTIFY pNotify;
    PGRAPHICS_DEVICE pGraphicsDevice;
    PLIST_ENTRY pEntry;

    for (;;)
    {
        pGraphicsDevice = NULL;
        EngAcquireSemaphore(ghsemGraphicsDeviceList);
        for (pEntry = gGraphicsDeviceNotifyList.Flink; pEntry != &gGraphicsDeviceNotifyList; pEntry = pEntry->Flink)
        {
            pNotify = CONTAINING_RECORD(pEntry, GRAPHICS_DEVICE_NOTIFY, ListEntry);
            if (pNotify->bRemoved &&
                !(pNotify->pGraphicsDevice->StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) &&
                !PDEVOBJ_bGraphicsDeviceInUse(pNotify->pGraphicsDevice))
            {
                pGraphicsDevice = pNotify->pGraphicsDevice;
                EngpUnlinkGraphicsDevice(pGraphicsDevice);
                break;
            }
        }
        EngReleaseSemaphore(ghsemGraphicsDeviceList);
        if (!pGraphicsDevice)
            break;

        TRACE("Releasing removed graphics device %S\n", pGraphicsDevice->szWinDeviceName);
        EngpFreeGraphicsDevice(pGraphicsDevice);
    }

    ASSERT(UserIsEnteredExclusive());
    UserLeave();
    EngpReleaseRetiredNotifications();
    UserEnterExclusive();
}

static
VOID
EngpRegisterGraphicsDeviceNotification(
    _In_ PGRAPHICS_DEVICE pGraphicsDevice)
{
    PGRAPHICS_DEVICE_NOTIFY pNotify;
    NTSTATUS Status;

    pNotify = ExAllocatePoolZero(NonPagedPool, sizeof(*pNotify), GDITAG_GDEVICE);
    if (!pNotify)
        return;

    pNotify->pGraphicsDevice = pGraphicsDevice;
    Status = IoRegisterPlugPlayNotification(EventCategoryTargetDeviceChange,
                                            0,
                                            pGraphicsDevice->FileObject,
                                            gpGraphicsDriverObject,
                                            EngpGraphicsDeviceNotification,
                                            pNotify,
                                            &pNotify->NotificationEntry);
    if (!NT_SUCCESS(Status))
    {
        TRACE("No removal notification for %S: 0x%08lx\n", pGraphicsDevice->szNtDeviceName, Status);
        ExFreePoolWithTag(pNotify, GDITAG_GDEVICE);
        return;
    }

    EngAcquireSemaphore(ghsemGraphicsDeviceList);
    InsertTailList(&gGraphicsDeviceNotifyList, &pNotify->ListEntry);
    EngReleaseSemaphore(ghsemGraphicsDeviceList);
}

/* TODO: INFO_NEEDED the device name and description Windows 11 reports for the disconnected display of a non-console session */
static
NTSTATUS
EngpRegisterDisconnectedGraphicsDevice(VOID)
{
    static const WCHAR DisplayDrivers[] = L"discdd\0";
    static const WCHAR Description[] = L"Disconnected Display";
    PGRAPHICS_DEVICE pGraphicsDevice, pRegistered;
    PWSTR pwsz;

    ASSERT(!EngpIsConsoleSession());

    if (EngpIsGraphicsDeviceRegistered(gszDisconnectedDisplayNtName))
        return STATUS_SUCCESS;

    pGraphicsDevice = ExAllocatePoolZero(PagedPool, sizeof(GRAPHICS_DEVICE), GDITAG_GDEVICE);
    if (!pGraphicsDevice)
        return STATUS_INSUFFICIENT_RESOURCES;

    /* REG_MULTI_SZ driver list (its literal adds the final terminator), then the description */
    pwsz = ExAllocatePoolWithTag(PagedPool, sizeof(DisplayDrivers) + sizeof(Description), GDITAG_DRVSUP);
    if (!pwsz)
    {
        ExFreePoolWithTag(pGraphicsDevice, GDITAG_GDEVICE);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlCopyMemory(pwsz, DisplayDrivers, sizeof(DisplayDrivers));
    pGraphicsDevice->pDiplayDrivers = pwsz;
    pGraphicsDevice->pwszDescription = pwsz + ARRAYSIZE(DisplayDrivers);
    RtlCopyMemory(pGraphicsDevice->pwszDescription, Description, sizeof(Description));
    RtlStringCbCopyW(pGraphicsDevice->szNtDeviceName, sizeof(pGraphicsDevice->szNtDeviceName), gszDisconnectedDisplayNtName);
    ASSERT(EngpIsDisconnectedGraphicsDevice(pGraphicsDevice));

    EngAcquireSemaphore(ghsemGraphicsDeviceList);
    for (pRegistered = gpGraphicsDeviceFirst; pRegistered; pRegistered = pRegistered->pNextGraphicsDevice)
    {
        if (EngpIsDisconnectedGraphicsDevice(pRegistered))
            break;
    }
    if (pRegistered)
    {
        /* Another thread of this session registered it first */
        EngReleaseSemaphore(ghsemGraphicsDeviceList);
        ExFreePoolWithTag(pGraphicsDevice->pDiplayDrivers, GDITAG_DRVSUP);
        ExFreePoolWithTag(pGraphicsDevice, GDITAG_GDEVICE);
        return STATUS_SUCCESS;
    }
    RtlStringCbPrintfW(pGraphicsDevice->szWinDeviceName,
                       sizeof(pGraphicsDevice->szWinDeviceName),
                       L"\\\\.\\DISPLAY%d",
                       (int)giDevNum);
    EngpLinkGraphicsDevice(pGraphicsDevice);
    giDevNum++;
    if (!gpPrimaryGraphicsDevice)
    {
        pGraphicsDevice->StateFlags |= DISPLAY_DEVICE_PRIMARY_DEVICE;
        gpPrimaryGraphicsDevice = pGraphicsDevice;
    }
    EngReleaseSemaphore(ghsemGraphicsDeviceList);

    TRACE("Registered the disconnected display as %S\n", pGraphicsDevice->szWinDeviceName);
    return STATUS_SUCCESS;
}

/* Goal of this function is to:
 * - detect new graphic devices (from registry) and initialize them
 * - link primary device and VGA device (if available) using pVgaDevice field
 * - handle gbBaseVideo global flag
 * - set DISPLAY_DEVICE_PRIMARY_DEVICE on at least one device
 * - set gpPrimaryGraphicsDevice
 * - set gpVgaGraphicsDevice
 */
NTSTATUS
EngpUpdateGraphicsDeviceList(VOID)
{
    ULONG iDevNum, ulMaxObjectNumber = 0;
    WCHAR awcDeviceName[20];
    WCHAR awcBuffer[256];
    NTSTATUS Status;
    PGRAPHICS_DEVICE pGraphicsDevice, pNewPrimaryGraphicsDevice = NULL;
    ULONG cbValue;
    HKEY hkey;

    /* Services and other sessions without the console have no access to the video driver; they draw to a disconnected display */
    if (!EngpIsConsoleSession())
        return EngpRegisterDisconnectedGraphicsDevice();

    /* Open the key for the adapters */
    Status = RegOpenKey(L"\\Registry\\Machine\\HARDWARE\\DEVICEMAP\\VIDEO", &hkey);
    if (!NT_SUCCESS(Status))
    {
        ERR("Could not open HARDWARE\\DEVICEMAP\\VIDEO registry key:0x%lx\n", Status);
        return Status;
    }

    /* Get the maximum number of adapters */
    if (!RegReadDWORD(hkey, L"MaxObjectNumber", &ulMaxObjectNumber))
    {
        ERR("Could not read MaxObjectNumber, defaulting to 0.\n");
    }

    TRACE("Found %lu devices\n", ulMaxObjectNumber + 1);

    /* Loop through all adapters, to detect new ones */
    for (iDevNum = 0; iDevNum <= ulMaxObjectNumber; iDevNum++)
    {
        /* Create the adapter's key name */
        _swprintf(awcDeviceName, L"\\Device\\Video%lu", iDevNum);

        /* Check if the device exists already */
        if (EngpIsGraphicsDeviceRegistered(awcDeviceName))
        {
            continue;
        }

        /* Read the reg key name */
        cbValue = sizeof(awcBuffer);
        Status = RegQueryValue(hkey, awcDeviceName, REG_SZ, awcBuffer, &cbValue);
        if (!NT_SUCCESS(Status))
        {
            ERR("failed to query the registry path:0x%lx\n", Status);
            continue;
        }

        /* Initialize the driver for this device */
        pGraphicsDevice = InitDisplayDriver(awcDeviceName, awcBuffer);
        if (!pGraphicsDevice) continue;
    }

    /* Close the device map registry key */
    ZwClose(hkey);

    /* Choose a VGA device */
    /* Try a device with DISPLAY_DEVICE_VGA_COMPATIBLE flag. If not found,
     * fall back to current VGA device */
    for (pGraphicsDevice = gpGraphicsDeviceFirst;
         pGraphicsDevice;
         pGraphicsDevice = pGraphicsDevice->pNextGraphicsDevice)
    {
        if (pGraphicsDevice == gpVgaGraphicsDevice)
            continue;
        if (pGraphicsDevice->StateFlags & DISPLAY_DEVICE_VGA_COMPATIBLE && EngpHasVgaDriver(pGraphicsDevice))
        {
            gpVgaGraphicsDevice = pGraphicsDevice;
            break;
        }
    }

    /* Handle gbBaseVideo */
    if (gbBaseVideo)
    {
        PGRAPHICS_DEVICE pToDelete;

        /* Lock list */
        EngAcquireSemaphore(ghsemGraphicsDeviceList);

        /* Remove every device from linked list, except base-video one */
        pGraphicsDevice = gpGraphicsDeviceFirst;
        while (pGraphicsDevice)
        {
            if (!EngpHasVgaDriver(pGraphicsDevice))
            {
                /* Not base-video device. Remove it */
                pToDelete = pGraphicsDevice;
                TRACE("Removing non-base-video device %S (%S)\n", pToDelete->szWinDeviceName, pToDelete->szNtDeviceName);

                EngpUnlinkGraphicsDevice(pGraphicsDevice);
                pGraphicsDevice = pGraphicsDevice->pNextGraphicsDevice;

                if (gpPrimaryGraphicsDevice == pToDelete)
                    gpPrimaryGraphicsDevice = NULL;
                if (gpVgaGraphicsDevice == pToDelete)
                    gpVgaGraphicsDevice = NULL;

                /* Free memory */
                EngpFreeGraphicsDevice(pToDelete);
            }
            else
            {
                pGraphicsDevice = pGraphicsDevice->pNextGraphicsDevice;
            }
        }

        /* Unlock list */
        EngReleaseSemaphore(ghsemGraphicsDeviceList);
    }

    /* Choose a primary device (if none already exists) */
    if (!gpPrimaryGraphicsDevice)
    {
        for (pGraphicsDevice = gpGraphicsDeviceFirst;
             pGraphicsDevice;
             pGraphicsDevice = pGraphicsDevice->pNextGraphicsDevice)
        {
            if (!EngpHasVgaDriver(pGraphicsDevice) &&
                EngpIsDisplayAttached(pGraphicsDevice))
            {
                pNewPrimaryGraphicsDevice = pGraphicsDevice;
                break;
            }
        }
        for (pGraphicsDevice = gpGraphicsDeviceFirst;
             pGraphicsDevice && !pNewPrimaryGraphicsDevice;
             pGraphicsDevice = pGraphicsDevice->pNextGraphicsDevice)
        {
            if (!EngpHasVgaDriver(pGraphicsDevice))
            {
                pNewPrimaryGraphicsDevice = pGraphicsDevice;
                break;
            }
        }
        if (!pNewPrimaryGraphicsDevice)
            pNewPrimaryGraphicsDevice = gpGraphicsDeviceFirst;
        if (pNewPrimaryGraphicsDevice)
        {
            pNewPrimaryGraphicsDevice->StateFlags |= DISPLAY_DEVICE_PRIMARY_DEVICE;
            gpPrimaryGraphicsDevice = pNewPrimaryGraphicsDevice;
        }
    }

    /* Can we link VGA device to primary device? */
    if (gpPrimaryGraphicsDevice &&
        gpVgaGraphicsDevice &&
        gpPrimaryGraphicsDevice != gpVgaGraphicsDevice &&
        !gpPrimaryGraphicsDevice->pVgaDevice)
    {
        /* Yes. Remove VGA device from global list, and attach it to primary device */
        TRACE("Linking VGA device %S to primary device %S\n", gpVgaGraphicsDevice->szNtDeviceName, gpPrimaryGraphicsDevice->szNtDeviceName);
        EngAcquireSemaphore(ghsemGraphicsDeviceList);
        EngpUnlinkGraphicsDevice(gpVgaGraphicsDevice);
        gpPrimaryGraphicsDevice->pVgaDevice = gpVgaGraphicsDevice;
        EngReleaseSemaphore(ghsemGraphicsDeviceList);
    }

    return STATUS_SUCCESS;
}

/* Open display settings registry key
 * Returns NULL in case of error. */
static HKEY
EngpGetRegistryHandleFromDeviceMap(
    _In_ PGRAPHICS_DEVICE pGraphicsDevice)
{
    static const PWCHAR KEY_VIDEO = L"\\Registry\\Machine\\HARDWARE\\DEVICEMAP\\VIDEO";
    HKEY hKey;
    WCHAR szDeviceKey[256];
    ULONG cbSize;
    NTSTATUS Status;

    if (EngpIsDisconnectedGraphicsDevice(pGraphicsDevice))
        return NULL;

    /* Open the device map registry key */
    Status = RegOpenKey(KEY_VIDEO, &hKey);
    if (!NT_SUCCESS(Status))
    {
        ERR("Could not open HARDWARE\\DEVICEMAP\\VIDEO registry key: status 0x%08x\n", Status);
        return NULL;
    }

    /* Query the registry path */
    cbSize = sizeof(szDeviceKey);
    Status = RegQueryValue(hKey,
                           pGraphicsDevice->szNtDeviceName,
                           REG_SZ,
                           szDeviceKey,
                           &cbSize);
    ZwClose(hKey);
    if (!NT_SUCCESS(Status))
    {
        ERR("No device map entry for %S: status 0x%08x\n", pGraphicsDevice->szNtDeviceName, Status);
        return NULL;
    }

    /* Open the registry key */
    Status = RegOpenKey(szDeviceKey, &hKey);
    if (!NT_SUCCESS(Status))
    {
        ERR("Could not open registry key '%S': status 0x%08x\n", szDeviceKey, Status);
        return NULL;
    }

    return hKey;
}

NTSTATUS
EngpGetDisplayDriverParameters(
    _In_ PGRAPHICS_DEVICE pGraphicsDevice,
    _Out_ PDEVMODEW pdm)
{
    HKEY hKey;
    NTSTATUS Status;
    RTL_QUERY_REGISTRY_TABLE DisplaySettingsTable[] =
    {
#define READ(field, str) \
        { \
            NULL, \
            RTL_QUERY_REGISTRY_DIRECT, \
            L ##str, \
            &pdm->field, \
            REG_NONE, NULL, 0 \
        },
    READ(dmBitsPerPel, "DefaultSettings.BitsPerPel")
    READ(dmPelsWidth, "DefaultSettings.XResolution")
    READ(dmPelsHeight, "DefaultSettings.YResolution")
    READ(dmDisplayFlags, "DefaultSettings.Flags")
    READ(dmDisplayFrequency, "DefaultSettings.VRefresh")
    READ(dmPanningWidth, "DefaultSettings.XPanning")
    READ(dmPanningHeight, "DefaultSettings.YPanning")
    READ(dmDisplayOrientation, "DefaultSettings.Orientation")
    READ(dmDisplayFixedOutput, "DefaultSettings.FixedOutput")
    READ(dmPosition.x, "Attach.RelativeX")
    READ(dmPosition.y, "Attach.RelativeY")
#undef READ
        {0}
    };

    if (EngpIsDisconnectedGraphicsDevice(pGraphicsDevice))
        return STATUS_SUCCESS;

    hKey = EngpGetRegistryHandleFromDeviceMap(pGraphicsDevice);
    if (!hKey)
        return STATUS_UNSUCCESSFUL;

    Status = RtlQueryRegistryValues(RTL_REGISTRY_HANDLE,
                                    (PWSTR)hKey,
                                    DisplaySettingsTable,
                                    NULL,
                                    NULL);

    ZwClose(hKey);
    return Status;
}

BOOL
EngpIsDisplayAttached(
    _In_ PGRAPHICS_DEVICE pGraphicsDevice)
{
    DWORD dwAttached = (pGraphicsDevice->StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE) ? 1 : 0;
    RTL_QUERY_REGISTRY_TABLE AttachTable[] =
    {
        { NULL, RTL_QUERY_REGISTRY_DIRECT, L"Attach.ToDesktop", &dwAttached, REG_NONE, NULL, 0 },
        { 0 }
    };
    HKEY hKey;

    hKey = EngpGetRegistryHandleFromDeviceMap(pGraphicsDevice);
    if (hKey)
    {
        RtlQueryRegistryValues(RTL_REGISTRY_HANDLE, (PWSTR)hKey, AttachTable, NULL, NULL);
        ZwClose(hKey);
    }
    /* The primary device always holds the desktop */
    return dwAttached != 0 || (pGraphicsDevice->StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE);
}

DWORD
EngpGetDisplayDriverAccelerationLevel(
    _In_ PGRAPHICS_DEVICE pGraphicsDevice)
{
    HKEY hKey;
    DWORD dwAccelerationLevel = 0;
    RTL_QUERY_REGISTRY_TABLE DisplaySettingsTable[] =
    {
        {
            NULL,
            RTL_QUERY_REGISTRY_DIRECT,
            L"Acceleration.Level",
            &dwAccelerationLevel,
            REG_NONE, NULL, 0
        },
        {0}
    };

    hKey = EngpGetRegistryHandleFromDeviceMap(pGraphicsDevice);
    if (!hKey)
        return 0;

    RtlQueryRegistryValues(RTL_REGISTRY_HANDLE,
                           (PWSTR)hKey,
                           DisplaySettingsTable,
                           NULL,
                           NULL);
    ZwClose(hKey);

    return dwAccelerationLevel;
}

extern VOID
UserRefreshDisplay(IN PPDEVOBJ ppdev);

// PVIDEO_WIN32K_CALLOUT
VOID
NTAPI
VideoPortCallout(
    _In_ PVOID Params)
{
/*
 * IMPORTANT NOTICE!! On Windows XP/2003 this function triggers the creation of
 * a specific VideoPortCalloutThread() system thread using the same mechanism
 * as the RIT/desktop/Ghost system threads.
 */

    PVIDEO_WIN32K_CALLBACKS_PARAMS CallbackParams = (PVIDEO_WIN32K_CALLBACKS_PARAMS)Params;

    TRACE("VideoPortCallout(0x%p, 0x%x)\n",
          CallbackParams, CallbackParams ? CallbackParams->CalloutType : -1);

    if (!CallbackParams)
        return;

    switch (CallbackParams->CalloutType)
    {
        case VideoFindAdapterCallout:
        {
            TRACE("VideoPortCallout: VideoFindAdapterCallout called - Param = %s\n",
                  CallbackParams->Param ? "TRUE" : "FALSE");
            if (CallbackParams->Param == TRUE)
            {
                /* Re-enable the display */
                UserRefreshDisplay(gpmdev->ppdevGlobal);
            }
            else
            {
                /* Disable the display */
                NOTHING; // Nothing to do for the moment...
            }

            CallbackParams->Status = STATUS_SUCCESS;
            break;
        }

        case VideoDxgkMonitorEventCallout:
        {
            if (CallbackParams->PhysDisp)
                InterlockedExchange(&((PGRAPHICS_DEVICE)CallbackParams->PhysDisp)->lMonitorEvent, 1);
            UserQueueDisplayConfigurationUpdate();
            CallbackParams->Status = STATUS_SUCCESS;
            break;
        }

        case VideoPowerNotifyCallout:
        case VideoDisplaySwitchCallout:
        case VideoEnumChildPdoNotifyCallout:
        case VideoWakeupCallout:
        case VideoChangeDisplaySettingsCallout:
        case VideoPnpNotifyCallout:
        case VideoDxgkDisplaySwitchCallout:
        case VideoDxgkFindAdapterTdrCallout:
            ERR("VideoPortCallout: CalloutType 0x%x is UNIMPLEMENTED!\n", CallbackParams->CalloutType);
            CallbackParams->Status = STATUS_NOT_IMPLEMENTED;
            break;

        default:
            ERR("VideoPortCallout: Unknown CalloutType 0x%x\n", CallbackParams->CalloutType);
            CallbackParams->Status = STATUS_UNSUCCESSFUL;
            break;
    }
}

/* Sends a TargetDeviceRelation request to PDO
 * On success, caller needs to free pDeviceRelations with ExFreePool()
 */
static
NTSTATUS
EngpPnPTargetRelationRequest(
    _In_ PDEVICE_OBJECT pDeviceObject,
    _Out_ PDEVICE_RELATIONS *pDeviceRelations)
{
    PIO_STACK_LOCATION IrpSp;
    KEVENT Event;
    PIRP pIrp;
    IO_STATUS_BLOCK Iosb;
    NTSTATUS Status;

    /* Initialize an event */
    KeInitializeEvent(&Event, SynchronizationEvent, FALSE);

    /* Build IRP */
    pIrp = IoBuildSynchronousFsdRequest(IRP_MJ_PNP,
                                        pDeviceObject,
                                        NULL, 0,
                                        NULL,
                                        &Event,
                                        &Iosb);
    if (!pIrp)
        return STATUS_INSUFFICIENT_RESOURCES;

    /* Initialize IRP */
    pIrp->IoStatus.Status = STATUS_NOT_SUPPORTED;
    IrpSp = IoGetNextIrpStackLocation(pIrp);
    IrpSp->MinorFunction = IRP_MN_QUERY_DEVICE_RELATIONS;
    IrpSp->Parameters.QueryDeviceRelations.Type = TargetDeviceRelation;

    /* Call the driver */
    Status = IoCallDriver(pDeviceObject, pIrp);

    /* Wait if neccessary */
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, 0);
        Status = Iosb.Status;
    }

    /* Return information to the caller about the operation. */
    if (NT_SUCCESS(Status))
        *pDeviceRelations = (PDEVICE_RELATIONS)Iosb.Information;

    return Status;
}

NTSTATUS
NTAPI
EngpUpdateMonitorDevices(
    _In_ PGRAPHICS_DEVICE pGraphicsDevice)
{
    PDEVICE_RELATIONS pDeviceRelations = NULL;
    PVIDEO_MONITOR_DEVICE pMonitorDevices, pNewMonitors, pOldMonitors;
    ULONG i, bytesWritten, monitorCount, oldCount;
    NTSTATUS Status;
    HANDLE hkRegistry;

    /* Request right PDO for device relations */
    Status = EngpPnPTargetRelationRequest(pGraphicsDevice->DeviceObject, &pDeviceRelations);
    if (!NT_SUCCESS(Status))
    {
        if (Status != STATUS_INVALID_PARAMETER &&
            Status != STATUS_INVALID_DEVICE_REQUEST &&
            Status != STATUS_NOT_SUPPORTED)
        {
            ERR("EngpPnPTargetRelationRequest() failed with status 0x%08x\n", Status);
            return Status;
        }
    }

    if (pDeviceRelations)
    {
        ASSERT(pDeviceRelations->Count == 1);

        /* Invalidate relations, so that videoprt reenumerates its monitors.
         * Only do this for valid PDOs - check by trying to open registry key. */
        Status = IoOpenDeviceRegistryKey(pDeviceRelations->Objects[0],
                                         PLUGPLAY_REGKEY_DRIVER,
                                         KEY_READ,
                                         &hkRegistry);
        if (NT_SUCCESS(Status))
        {
            ZwClose(hkRegistry);
            IoSynchronousInvalidateDeviceRelations(pDeviceRelations->Objects[0], BusRelations);
        }
        else
        {
            /* Legacy device without valid PDO - skip invalidation.
             * This is expected for non-PnP devices like VGA. */
        }

        /* Free returned structure */
        for (i = 0; i < pDeviceRelations->Count; i++)
            ObDereferenceObject(pDeviceRelations->Objects[i]);
        ExFreePool(pDeviceRelations);
    }

    /* Now, get list of monitor PDOs */
    Status = EngDeviceIoControl(pGraphicsDevice->DeviceObject,
                                IOCTL_VIDEO_ENUM_MONITOR_PDO,
                                NULL, 0,
                                &pMonitorDevices, sizeof(pMonitorDevices),
                                &bytesWritten);
    if (Status != ERROR_SUCCESS)
    {
        ERR("EngDeviceIoControl(IOCTL_VIDEO_ENUM_MONITOR_PDO) failed with status 0x%08x\n", Status);
        return Status;
    }
    ASSERT(bytesWritten == sizeof(pMonitorDevices));

    /* Count number of available monitors */
    for (monitorCount = 0; pMonitorDevices[monitorCount].pdo; ++monitorCount)
        ;

    pNewMonitors = NULL;
    if (monitorCount > 0)
    {
        pNewMonitors = ExAllocatePoolZero(PagedPool,
                                          monitorCount * sizeof(VIDEO_MONITOR_DEVICE),
                                          GDITAG_GDEVICE);
        if (!pNewMonitors)
        {
            for (i = 0; pMonitorDevices[i].pdo; ++i)
                ObDereferenceObject(pMonitorDevices[i].pdo);
            ExFreePool(pMonitorDevices);
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        /* Copy data */
        for (i = 0; i < monitorCount; i++)
        {
            TRACE("%S\\Monitor%u: PDO %p HwID %u\n", pGraphicsDevice->szWinDeviceName, i, pMonitorDevices[i].pdo, pMonitorDevices[i].HwID);
            pNewMonitors[i] = pMonitorDevices[i];
        }
    }
    ExFreePool(pMonitorDevices);

    /* Readers copy an entry and reference its PDO under the list lock. */
    EngAcquireSemaphore(ghsemGraphicsDeviceList);
    pOldMonitors = pGraphicsDevice->pvMonDev;
    oldCount = pGraphicsDevice->dwMonCnt;
    pGraphicsDevice->pvMonDev = pNewMonitors;
    pGraphicsDevice->dwMonCnt = monitorCount;
    EngReleaseSemaphore(ghsemGraphicsDeviceList);

    ASSERT(pOldMonitors != NULL || oldCount == 0);
    for (i = 0; i < oldCount; i++)
        ObDereferenceObject(pOldMonitors[i].pdo);
    if (pOldMonitors)
        ExFreePoolWithTag(pOldMonitors, GDITAG_GDEVICE);
    return STATUS_SUCCESS;
}

/* A display device without a PDEV lists the modes its driver reports; the list is built once and rebuilt only on request */
BOOL
NTAPI
EngpBuildDetachedModeList(
    _Inout_ PGRAPHICS_DEVICE pGraphicsDevice,
    _In_ BOOL bRefresh)
{
    PDEVMODEINFO pdminfo, pdmiNext;
    BOOL bResult;

    EngAcquireSemaphore(ghsemGraphicsDeviceList);
    if (bRefresh)
    {
        for (pdminfo = pGraphicsDevice->pdevmodeInfo; pdminfo; pdminfo = pdmiNext)
        {
            pdmiNext = pdminfo->pdmiNext;
            ExFreePoolWithTag(pdminfo, GDITAG_DEVMODE);
        }
        pGraphicsDevice->pdevmodeInfo = NULL;
        if (pGraphicsDevice->pDevModeList)
            ExFreePoolWithTag(pGraphicsDevice->pDevModeList, GDITAG_GDEVICE);
        pGraphicsDevice->pDevModeList = NULL;
        pGraphicsDevice->cDevModes = 0;
    }
    bResult = LDEVOBJ_bBuildDevmodeList(pGraphicsDevice);
    ASSERT(!bResult || pGraphicsDevice->cDevModes != 0);
    EngReleaseSemaphore(ghsemGraphicsDeviceList);
    return bResult;
}

NTSTATUS
NTAPI
EngpCaptureListedMode(
    _In_ PGRAPHICS_DEVICE pGraphicsDevice,
    _In_ ULONG iModeNum,
    _Outptr_ PDEVMODEW *ppdm)
{
    PDEVMODEW pdmListed;
    SIZE_T cjMode;
    NTSTATUS Status = STATUS_INVALID_PARAMETER_2;

    *ppdm = NULL;
    EngAcquireSemaphoreShared(ghsemGraphicsDeviceList);
    ASSERT(pGraphicsDevice->cDevModes == 0 || pGraphicsDevice->pDevModeList != NULL);
    if (iModeNum < pGraphicsDevice->cDevModes)
    {
        pdmListed = pGraphicsDevice->pDevModeList[iModeNum].pdm;
        ASSERT(pdmListed != NULL);
        cjMode = (SIZE_T)pdmListed->dmSize + pdmListed->dmDriverExtra;
        *ppdm = ExAllocatePoolWithTag(PagedPool, cjMode, GDITAG_DEVMODE);
        if (*ppdm)
        {
            RtlCopyMemory(*ppdm, pdmListed, cjMode);
            Status = STATUS_SUCCESS;
        }
        else
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
        }
    }
    EngReleaseSemaphore(ghsemGraphicsDeviceList);
    return Status;
}

BOOLEAN
NTAPI
EngpReferenceMonitorDevice(
    _In_ PGRAPHICS_DEVICE pGraphicsDevice,
    _In_ ULONG iMonitor,
    _Out_ PVIDEO_MONITOR_DEVICE pMonitor)
{
    BOOLEAN bFound = FALSE;

    EngAcquireSemaphoreShared(ghsemGraphicsDeviceList);
    if (iMonitor < pGraphicsDevice->dwMonCnt)
    {
        *pMonitor = pGraphicsDevice->pvMonDev[iMonitor];
        ASSERT(pMonitor->pdo != NULL);
        ObReferenceObject(pMonitor->pdo);
        bFound = TRUE;
    }
    EngReleaseSemaphore(ghsemGraphicsDeviceList);
    return bFound;
}

/* Session 0 shares the adapters it draws on; any other session asks the device whether it may use it */
static
BOOLEAN
EngpUseGraphicsDeviceInSession(
    _In_ PDEVICE_OBJECT DeviceObject)
{
    VIDEO_DEVICE_SESSION_STATUS SessionStatus;
    ULONG ulReturn = 0;
    DWORD Error;

    if (ReadPointerAcquire((PVOID volatile *)&gpDisplaySession) == NULL)
        return TRUE;

    SessionStatus.bEnable = TRUE;
    SessionStatus.bSuccess = FALSE;
    Error = EngDeviceIoControl((HANDLE)DeviceObject,
                               IOCTL_VIDEO_USE_DEVICE_IN_SESSION,
                               &SessionStatus,
                               sizeof(SessionStatus),
                               &SessionStatus,
                               sizeof(SessionStatus),
                               &ulReturn);
    return Error == ERROR_SUCCESS && ulReturn >= sizeof(SessionStatus) && SessionStatus.bSuccess;
}

PGRAPHICS_DEVICE
NTAPI
EngpRegisterGraphicsDevice(
    _In_ PUNICODE_STRING pustrDeviceName,
    _In_ PUNICODE_STRING pustrDiplayDrivers,
    _In_ PUNICODE_STRING pustrDescription)
{
    PGRAPHICS_DEVICE pGraphicsDevice;
    PDEVICE_OBJECT pDeviceObject;
    PFILE_OBJECT pFileObject;
    NTSTATUS Status;
    VIDEO_WIN32K_CALLBACKS Win32kCallbacks;
    PUSER_SESSION DisplaySession;
    PEPROCESS CsrProcess;
    KAPC_STATE ApcState;
    ULONG ulReturn;
    PWSTR pwsz;
    ULONG cj;

    TRACE("EngpRegisterGraphicsDevice(%wZ)\n", pustrDeviceName);

    /* Allocate a GRAPHICS_DEVICE structure */
    pGraphicsDevice = ExAllocatePoolZero(PagedPool,
                                         sizeof(GRAPHICS_DEVICE),
                                         GDITAG_GDEVICE);
    if (!pGraphicsDevice)
    {
        ERR("ExAllocatePoolWithTag failed\n");
        return NULL;
    }

    /* Try to open and enable the device */
    Status = IoGetDeviceObjectPointer(pustrDeviceName,
                                      FILE_READ_DATA | FILE_WRITE_DATA,
                                      &pFileObject,
                                      &pDeviceObject);
    if (!NT_SUCCESS(Status))
    {
        ERR("Could not open device %wZ, 0x%lx\n", pustrDeviceName, Status);
        ExFreePoolWithTag(pGraphicsDevice, GDITAG_GDEVICE);
        return NULL;
    }

    if (!EngpUseGraphicsDeviceInSession(pDeviceObject))
    {
        TRACE("%wZ belongs to another session\n", pustrDeviceName);
        ObDereferenceObject(pFileObject);
        ExFreePoolWithTag(pGraphicsDevice, GDITAG_GDEVICE);
        return NULL;
    }

    /* Copy the device and file object pointers */
    pGraphicsDevice->DeviceObject = pDeviceObject;
    pGraphicsDevice->FileObject = pFileObject;

    /* Initialize and register the device with videoprt for Win32k callbacks */
    Win32kCallbacks.PhysDisp = pGraphicsDevice;
    Win32kCallbacks.Callout = NULL;
    // Reset the data being returned prior to the call.
    Win32kCallbacks.bACPI = FALSE;
    Win32kCallbacks.pPhysDeviceObject = NULL;
    Win32kCallbacks.DualviewFlags = 0;

    /* Each session runs its own win32k: only the display session takes the callouts, registered from its CSRSS to outlive the caller */
    DisplaySession = ReadPointerAcquire((PVOID volatile *)&gpDisplaySession);
    CsrProcess = NULL;
    if (DisplaySession != NULL && DisplaySession == UserGetCurrentSession())
    {
        Win32kCallbacks.Callout = VideoPortCallout;
        CsrProcess = DisplaySession->CsrProcess;
        if (CsrProcess == PsGetCurrentProcess())
            CsrProcess = NULL;
        if (CsrProcess != NULL)
        {
            ObReferenceObject(CsrProcess);
            KeStackAttachProcess(&CsrProcess->Pcb, &ApcState);
        }
    }
    if (WddmHandOverIsActive())
        Status = NT_SUCCESS(WddmHandOverInitGdiDevice(pDeviceObject, &Win32kCallbacks)) ? ERROR_SUCCESS : ERROR_NOT_SUPPORTED;
    else
        Status = (NTSTATUS)EngDeviceIoControl((HANDLE)pDeviceObject,
                                              IOCTL_VIDEO_INIT_WIN32K_CALLBACKS,
                                              &Win32kCallbacks,
                                              sizeof(Win32kCallbacks),
                                              &Win32kCallbacks,
                                              sizeof(Win32kCallbacks),
                                              &ulReturn);
    if (CsrProcess != NULL)
    {
        KeUnstackDetachProcess(&ApcState);
        ObDereferenceObject(CsrProcess);
    }
    if (Status != ERROR_SUCCESS)
    {
        ERR("EngDeviceIoControl(0x%p, IOCTL_VIDEO_INIT_WIN32K_CALLBACKS) failed, Status 0x%lx\n",
            pDeviceObject, Status);
    }
    // TODO: Set flags according to the results.
    // if (Win32kCallbacks.bACPI)
    // if (Win32kCallbacks.DualviewFlags & ???)
    pGraphicsDevice->PhysDeviceHandle = Win32kCallbacks.pPhysDeviceObject;
    if (pGraphicsDevice->PhysDeviceHandle)
        ObReferenceObject(pGraphicsDevice->PhysDeviceHandle);

    /* Copy the device name */
    RtlStringCbCopyNW(pGraphicsDevice->szNtDeviceName,
                      sizeof(pGraphicsDevice->szNtDeviceName),
                      pustrDeviceName->Buffer,
                      pustrDeviceName->Length);

    /* Create a Win32 device name (FIXME: virtual devices!) */
    RtlStringCbPrintfW(pGraphicsDevice->szWinDeviceName,
                       sizeof(pGraphicsDevice->szWinDeviceName),
                       L"\\\\.\\DISPLAY%d",
                       (int)giDevNum);

    /* Allocate a buffer for the strings */
    cj = pustrDiplayDrivers->Length + pustrDescription->Length + sizeof(WCHAR);
    pwsz = ExAllocatePoolWithTag(PagedPool, cj, GDITAG_DRVSUP);
    if (!pwsz)
    {
        ERR("Could not allocate string buffer\n");
        ASSERT(FALSE); // FIXME
        if (pGraphicsDevice->PhysDeviceHandle)
            ObDereferenceObject(pGraphicsDevice->PhysDeviceHandle);
        ExFreePoolWithTag(pGraphicsDevice, GDITAG_GDEVICE);
        return NULL;
    }

    /* Copy the display driver names */
    pGraphicsDevice->pDiplayDrivers = pwsz;
    RtlCopyMemory(pGraphicsDevice->pDiplayDrivers,
                  pustrDiplayDrivers->Buffer,
                  pustrDiplayDrivers->Length);

    /* Copy the description */
    pGraphicsDevice->pwszDescription = pwsz + pustrDiplayDrivers->Length / sizeof(WCHAR);
    RtlCopyMemory(pGraphicsDevice->pwszDescription,
                  pustrDescription->Buffer,
                  pustrDescription->Length);
    pGraphicsDevice->pwszDescription[pustrDescription->Length/sizeof(WCHAR)] = 0;

    /* Update list of connected monitors */
    EngpUpdateMonitorDevices(pGraphicsDevice);

    /* Lock loader */
    EngAcquireSemaphore(ghsemGraphicsDeviceList);

    /* Insert the device into the global list */
    EngpLinkGraphicsDevice(pGraphicsDevice);

    /* Increment the device number */
    giDevNum++;

    /* Unlock loader */
    EngReleaseSemaphore(ghsemGraphicsDeviceList);

    EngpRegisterGraphicsDeviceNotification(pGraphicsDevice);

    /* HACK: already in graphic mode; display wallpaper on this new display */
    if (ScreenDeviceContext)
    {
        UNICODE_STRING DriverName = RTL_CONSTANT_STRING(L"DISPLAY");
        UNICODE_STRING DisplayName;
        HDC hdc;
        RtlInitUnicodeString(&DisplayName, pGraphicsDevice->szWinDeviceName);
        hdc = IntGdiCreateDC(&DriverName, &DisplayName, NULL, NULL, FALSE);
        IntPaintDesktop(hdc);
    }

    return pGraphicsDevice;
}

PGRAPHICS_DEVICE
NTAPI
EngpFindGraphicsDevice(
    _In_opt_ PUNICODE_STRING pustrDevice,
    _In_ ULONG iDevNum)
{
    UNICODE_STRING ustrCurrent;
    PGRAPHICS_DEVICE pGraphicsDevice;
    ULONG i;
    TRACE("EngpFindGraphicsDevice('%wZ', %lu)\n",
           pustrDevice, iDevNum);

    /* Lock list */
    EngAcquireSemaphoreShared(ghsemGraphicsDeviceList);

    if (pustrDevice && pustrDevice->Buffer)
    {
        /* Find specified video adapter by name */
        for (pGraphicsDevice = gpGraphicsDeviceFirst;
             pGraphicsDevice;
             pGraphicsDevice = pGraphicsDevice->pNextGraphicsDevice)
        {
            /* Compare the device name */
            RtlInitUnicodeString(&ustrCurrent, pGraphicsDevice->szWinDeviceName);
            if (RtlEqualUnicodeString(&ustrCurrent, pustrDevice, TRUE))
            {
                break;
            }
        }

        if (pGraphicsDevice)
        {
            /* Validate selected monitor number */
#if 0
            if (iDevNum >= pGraphicsDevice->dwMonCnt)
                pGraphicsDevice = NULL;
#else
            /* FIXME: dwMonCnt not initialized, see EngpRegisterGraphicsDevice */
#endif
        }
    }
    else
    {
        /* Select video adapter by device number */
        for (pGraphicsDevice = gpGraphicsDeviceFirst, i = 0;
             pGraphicsDevice && i < iDevNum;
             pGraphicsDevice = pGraphicsDevice->pNextGraphicsDevice, i++);
    }

    /* Unlock list */
    EngReleaseSemaphore(ghsemGraphicsDeviceList);

    return pGraphicsDevice;
}

static
NTSTATUS
EngpFileIoRequest(
    _In_ PFILE_OBJECT pFileObject,
    _In_ ULONG ulMajorFunction,
    _In_reads_(nBufferSize) PVOID lpBuffer,
    _In_ SIZE_T nBufferSize,
    _In_ ULONGLONG ullStartOffset,
    _Out_ PULONG_PTR lpInformation)
{
    PDEVICE_OBJECT pDeviceObject;
    KEVENT Event;
    PIRP pIrp;
    IO_STATUS_BLOCK Iosb;
    NTSTATUS Status;
    LARGE_INTEGER liStartOffset;

    /* Get corresponding device object */
    pDeviceObject = IoGetRelatedDeviceObject(pFileObject);
    if (!pDeviceObject)
    {
        return STATUS_INVALID_PARAMETER;
    }

    /* Initialize an event */
    KeInitializeEvent(&Event, SynchronizationEvent, FALSE);

    /* Build IRP */
    liStartOffset.QuadPart = ullStartOffset;
    pIrp = IoBuildSynchronousFsdRequest(ulMajorFunction,
                                        pDeviceObject,
                                        lpBuffer,
                                        (ULONG)nBufferSize,
                                        &liStartOffset,
                                        &Event,
                                        &Iosb);
    if (!pIrp)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* Call the driver */
    Status = IoCallDriver(pDeviceObject, pIrp);

    /* Wait if neccessary */
    if (STATUS_PENDING == Status)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, TRUE, 0);
        Status = Iosb.Status;
    }

    /* Return information to the caller about the operation. */
    *lpInformation = Iosb.Information;

    /* Return NTSTATUS */
    return Status;
}

VOID
APIENTRY
EngFileWrite(
    _In_ PFILE_OBJECT pFileObject,
    _In_reads_(nLength) PVOID lpBuffer,
    _In_ SIZE_T nLength,
    _Out_ PSIZE_T lpBytesWritten)
{
    NTSTATUS status;

    status = EngpFileIoRequest(pFileObject,
                               IRP_MJ_WRITE,
                               lpBuffer,
                               nLength,
                               0,
                               lpBytesWritten);
    if (!NT_SUCCESS(status))
    {
        *lpBytesWritten = 0;
    }
}

_Success_(return>=0)
NTSTATUS
APIENTRY
EngFileIoControl(
    _In_ PFILE_OBJECT pFileObject,
    _In_ DWORD dwIoControlCode,
    _In_reads_(nInBufferSize) PVOID lpInBuffer,
    _In_ SIZE_T nInBufferSize,
    _Out_writes_(nOutBufferSize) PVOID lpOutBuffer,
    _In_ SIZE_T nOutBufferSize,
    _Out_ PULONG_PTR lpInformation)
{
    PDEVICE_OBJECT pDeviceObject;
    KEVENT Event;
    PIRP pIrp;
    IO_STATUS_BLOCK Iosb;
    NTSTATUS Status;

    /* Get corresponding device object */
    pDeviceObject = IoGetRelatedDeviceObject(pFileObject);
    if (!pDeviceObject)
    {
        return STATUS_INVALID_PARAMETER;
    }

    /* Initialize an event */
    KeInitializeEvent(&Event, SynchronizationEvent, FALSE);

    /* Build IO control IRP */
    pIrp = IoBuildDeviceIoControlRequest(dwIoControlCode,
                                         pDeviceObject,
                                         lpInBuffer,
                                         (ULONG)nInBufferSize,
                                         lpOutBuffer,
                                         (ULONG)nOutBufferSize,
                                         FALSE,
                                         &Event,
                                         &Iosb);
    if (!pIrp)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* Call the driver */
    Status = IoCallDriver(pDeviceObject, pIrp);

    /* Wait if neccessary */
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, TRUE, 0);
        Status = Iosb.Status;
    }

    /* Return information to the caller about the operation. */
    *lpInformation = Iosb.Information;

    /* This function returns NTSTATUS */
    return Status;
}

/*
 * @implemented
 */
_Success_(return==0)
DWORD
APIENTRY
EngDeviceIoControl(
    _In_ HANDLE hDevice,
    _In_ DWORD dwIoControlCode,
    _In_reads_bytes_opt_(cjInBufferSize) LPVOID lpInBuffer,
    _In_ DWORD cjInBufferSize,
    _Out_writes_bytes_opt_(cjOutBufferSize) LPVOID lpOutBuffer,
    _In_ DWORD cjOutBufferSize,
    _Out_ LPDWORD lpBytesReturned)
{
    PIRP Irp;
    NTSTATUS Status;
    KEVENT Event;
    IO_STATUS_BLOCK Iosb;
    PDEVICE_OBJECT DeviceObject;

    TRACE("EngDeviceIoControl() called\n");

    if (!hDevice)
    {
        return ERROR_INVALID_HANDLE;
    }

    KeInitializeEvent(&Event, SynchronizationEvent, FALSE);
    RtlZeroMemory(&Iosb, sizeof(Iosb));

    DeviceObject = (PDEVICE_OBJECT) hDevice;

    Irp = IoBuildDeviceIoControlRequest(dwIoControlCode,
                                        DeviceObject,
                                        lpInBuffer,
                                        cjInBufferSize,
                                        lpOutBuffer,
                                        cjOutBufferSize,
                                        FALSE,
                                        &Event,
                                        &Iosb);
    if (!Irp) return ERROR_NOT_ENOUGH_MEMORY;

    Status = IoCallDriver(DeviceObject, Irp);

    if (Status == STATUS_PENDING)
    {
        (VOID)KeWaitForSingleObject(&Event, Executive, KernelMode, TRUE, 0);
        Status = Iosb.Status;
    }

    TRACE("EngDeviceIoControl(): Returning %X/%X\n", Iosb.Status,
           Iosb.Information);

    /* Return information to the caller about the operation. */
    *lpBytesReturned = (DWORD)Iosb.Information;

    /* Convert NT status values to win32 error codes. */
    switch (Status)
    {
        case STATUS_INSUFFICIENT_RESOURCES:
            return ERROR_NOT_ENOUGH_MEMORY;

        case STATUS_BUFFER_OVERFLOW:
            return ERROR_MORE_DATA;

        case STATUS_NOT_IMPLEMENTED:
            return ERROR_INVALID_FUNCTION;

        case STATUS_INVALID_PARAMETER:
            return ERROR_INVALID_PARAMETER;

        case STATUS_BUFFER_TOO_SMALL:
            return ERROR_INSUFFICIENT_BUFFER;

        case STATUS_DEVICE_DOES_NOT_EXIST:
            return ERROR_DEV_NOT_EXIST;

        case STATUS_PENDING:
            return ERROR_IO_PENDING;
    }

    return Status;
}

/* EOF */
