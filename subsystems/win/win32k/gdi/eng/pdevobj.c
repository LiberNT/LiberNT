/*
 * COPYRIGHT:        See COPYING in the top level directory
 * PROJECT:          ReactOS kernel
 * PURPOSE:          Support for physical devices
 * FILE:             win32ss/gdi/eng/pdevobj.c
 * PROGRAMERS:       Timo Kreuzer (timo.kreuzer@reactos.org)
 *                   Oleg Dubinskiy (oleg.dubinskij30@gmail.com)
 */

#include <win32k.h>
#define NDEBUG
#include <debug.h>
DBG_DEFAULT_CHANNEL(EngPDev);

static PPDEVOBJ gppdevList = NULL;
static HSEMAPHORE ghsemPDEV;

BOOL
APIENTRY
MultiEnableDriver(
    _In_ ULONG iEngineVersion,
    _In_ ULONG cj,
    _Inout_bytecount_(cj) PDRVENABLEDATA pded);

extern DRVFN gPanDispDrvFn[];
extern ULONG gPanDispDrvCount;

CODE_SEG("INIT")
NTSTATUS
NTAPI
InitPDEVImpl(VOID)
{
    ghsemPDEV = EngCreateSemaphore();
    if (!ghsemPDEV) return STATUS_INSUFFICIENT_RESOURCES;
    return STATUS_SUCCESS;
}

#if DBG
PPDEVOBJ
NTAPI
DbgLookupDHPDEV(DHPDEV dhpdev)
{
    PPDEVOBJ ppdev;

    /* Lock PDEV list */
    EngAcquireSemaphoreShared(ghsemPDEV);

    /* Walk through the list of PDEVs */
    for (ppdev = gppdevList;  ppdev; ppdev = ppdev->ppdevNext)
    {
        /* Compare with the given DHPDEV */
        if (ppdev->dhpdev == dhpdev) break;
    }

    /* Unlock PDEV list */
    EngReleaseSemaphore(ghsemPDEV);

    return ppdev;
}
#endif

PPDEVOBJ
PDEVOBJ_AllocPDEV(VOID)
{
    PPDEVOBJ ppdev;

    ppdev = ExAllocatePoolWithTag(PagedPool, sizeof(PDEVOBJ), GDITAG_PDEV);
    if (!ppdev)
        return NULL;

    RtlZeroMemory(ppdev, sizeof(PDEVOBJ));

    ppdev->hsemDevLock = EngCreateSemaphore();
    if (ppdev->hsemDevLock == NULL)
    {
        ExFreePoolWithTag(ppdev, GDITAG_PDEV);
        return NULL;
    }

    ppdev->hsemPointer = EngCreateSemaphore();
    if (ppdev->hsemPointer == NULL)
    {
        EngDeleteSemaphore(ppdev->hsemDevLock);
        ExFreePoolWithTag(ppdev, GDITAG_PDEV);
        return NULL;
    }

    /* Allocate EDD_DIRECTDRAW_GLOBAL for our ReactX driver */
    ppdev->pEDDgpl = ExAllocatePoolWithTag(PagedPool, sizeof(EDD_DIRECTDRAW_GLOBAL), GDITAG_PDEV);
    if (ppdev->pEDDgpl)
        RtlZeroMemory(ppdev->pEDDgpl, sizeof(EDD_DIRECTDRAW_GLOBAL));

    ppdev->cPdevRefs = 1;

    return ppdev;
}

static
VOID
PDEVOBJ_vDeletePDEV(
    PPDEVOBJ ppdev)
{
    EngDeleteSemaphore(ppdev->hsemPointer);
    EngDeleteSemaphore(ppdev->hsemDevLock);
    if (ppdev->pdmwDev)
        ExFreePoolWithTag(ppdev->pdmwDev, GDITAG_DEVMODE);
    if (ppdev->pEDDgpl)
        ExFreePoolWithTag(ppdev->pEDDgpl, GDITAG_PDEV);
    ExFreePoolWithTag(ppdev, GDITAG_PDEV);
}

VOID
NTAPI
PDEVOBJ_vRelease(
    _Inout_ PPDEVOBJ ppdev)
{
    /* Lock loader */
    EngAcquireSemaphore(ghsemPDEV);

    /* Decrease reference count */
    InterlockedDecrement(&ppdev->cPdevRefs);
    ASSERT(ppdev->cPdevRefs >= 0);

    /* Check if references are left */
    if (ppdev->cPdevRefs == 0)
    {
        EngpReleasePointer(ppdev);

        /* Do we have a surface? */
        if (ppdev->pSurface)
        {
            /* Release the surface and let the driver free it */
            SURFACE_ShareUnlockSurface(ppdev->pSurface);
            TRACE("DrvDisableSurface(dhpdev %p)\n", ppdev->dhpdev);
            ppdev->pfn.DisableSurface(ppdev->dhpdev);
        }

        /* Do we have a palette? */
        if (ppdev->ppalSurf)
        {
            PALETTE_ShareUnlockPalette(ppdev->ppalSurf);
        }

        /* Check if the PDEV was enabled */
        if (ppdev->dhpdev != NULL)
        {
            /* Disable the PDEV */
            TRACE("DrvDisablePDEV(dhpdev %p)\n", ppdev->dhpdev);
            ppdev->pfn.DisablePDEV(ppdev->dhpdev);
        }

        /* Remove it from list */
        if (ppdev == gppdevList)
        {
            gppdevList = ppdev->ppdevNext;
        }
        else if (gppdevList)
        {
            PPDEVOBJ ppdevCurrent = gppdevList;
            BOOL found = FALSE;
            while (!found && ppdevCurrent->ppdevNext)
            {
                if (ppdevCurrent->ppdevNext == ppdev)
                    found = TRUE;
                else
                    ppdevCurrent = ppdevCurrent->ppdevNext;
            }
            if (found)
                ppdevCurrent->ppdevNext = ppdev->ppdevNext;
        }

        /* Unload display driver */
        EngUnloadImage(ppdev->pldev);

        /* Free it */
        PDEVOBJ_vDeletePDEV(ppdev);
    }

    /* Unlock loader */
    EngReleaseSemaphore(ghsemPDEV);
}

BOOL
NTAPI
PDEVOBJ_bEnablePDEV(
    _In_ PPDEVOBJ ppdev,
    _In_ PDEVMODEW pdevmode,
    _In_ PWSTR pwszLogAddress)
{
    PFN_DrvEnablePDEV pfnEnablePDEV;
    ULONG i;

    /* Get the DrvEnablePDEV function */
    pfnEnablePDEV = ppdev->pfn.EnablePDEV;

    /* Call the drivers DrvEnablePDEV function */
    TRACE("DrvEnablePDEV(pdevmode %p (%dx%dx%d %d Hz) hdev %p (%S))\n",
        pdevmode,
        ppdev->pGraphicsDevice ? pdevmode->dmPelsWidth : 0,
        ppdev->pGraphicsDevice ? pdevmode->dmPelsHeight : 0,
        ppdev->pGraphicsDevice ? pdevmode->dmBitsPerPel : 0,
        ppdev->pGraphicsDevice ? pdevmode->dmDisplayFrequency : 0,
        ppdev,
        ppdev->pGraphicsDevice ? ppdev->pGraphicsDevice->szNtDeviceName : L"");
    ppdev->dhpdev = pfnEnablePDEV(pdevmode,
                                  pwszLogAddress,
                                  HS_DDI_MAX,
                                  ppdev->ahsurf,
                                  sizeof(GDIINFO),
                                  (PULONG)&ppdev->gdiinfo,
                                  sizeof(DEVINFO),
                                  &ppdev->devinfo,
                                  (HDEV)ppdev,
                                  ppdev->pGraphicsDevice ? ppdev->pGraphicsDevice->pwszDescription : NULL,
                                  ppdev->pGraphicsDevice ? ppdev->pGraphicsDevice->DeviceObject : NULL);
    TRACE("DrvEnablePDEV(pdevmode %p hdev %p) => dhpdev %p\n", pdevmode, ppdev, ppdev->dhpdev);
    if (ppdev->dhpdev == NULL)
    {
        ERR("Failed to enable PDEV\n");
        return FALSE;
    }

    /*
     * Logical display DPI is a win32k policy, not a physical monitor
     * dimension. Keep every display driver on the configured value even
     * when a legacy driver reports a fixed 96 DPI.
     */
    if (ppdev->pGraphicsDevice && pdevmode->dmLogPixels)
    {
        ppdev->gdiinfo.ulLogPixelsX = pdevmode->dmLogPixels;
        ppdev->gdiinfo.ulLogPixelsY = pdevmode->dmLogPixels;
    }
    else
    {
        if (ppdev->gdiinfo.ulLogPixelsX == 0)
            ppdev->gdiinfo.ulLogPixelsX = USER_SYSTEM_DPI_DEFAULT;

        if (ppdev->gdiinfo.ulLogPixelsY == 0)
            ppdev->gdiinfo.ulLogPixelsY = USER_SYSTEM_DPI_DEFAULT;
    }

    /* Set raster caps */
    ppdev->gdiinfo.flRaster = RC_OP_DX_OUTPUT | RC_GDI20_OUTPUT | RC_BIGFONT;
    if ((ppdev->gdiinfo.ulTechnology != DT_PLOTTER) && (ppdev->gdiinfo.ulTechnology != DT_CHARSTREAM))
        ppdev->gdiinfo.flRaster |= RC_STRETCHDIB | RC_STRETCHBLT | RC_DIBTODEV | RC_DI_BITMAP | RC_BITMAP64 | RC_BITBLT;
    if (ppdev->gdiinfo.ulTechnology == DT_RASDISPLAY)
        ppdev->gdiinfo.flRaster |= RC_FLOODFILL;
    if (ppdev->devinfo.flGraphicsCaps & GCAPS_PALMANAGED)
        ppdev->gdiinfo.flRaster |= RC_PALETTE;

    /* Setup Palette */
    ppdev->ppalSurf = PALETTE_ShareLockPalette(ppdev->devinfo.hpalDefault);

    /* Setup hatch brushes */
    for (i = 0; i < HS_DDI_MAX; i++)
    {
        if (ppdev->ahsurf[i] == NULL)
            ppdev->ahsurf[i] = gahsurfHatch[i];
    }

    TRACE("PDEVOBJ_bEnablePDEV - dhpdev = %p\n", ppdev->dhpdev);

    return TRUE;
}

VOID
NTAPI
PDEVOBJ_vCompletePDEV(
    PPDEVOBJ ppdev)
{
    /* Call the drivers DrvCompletePDEV function */
    TRACE("DrvCompletePDEV(dhpdev %p hdev %p)\n", ppdev->dhpdev, ppdev);
    ppdev->pfn.CompletePDEV(ppdev->dhpdev, (HDEV)ppdev);
}

static
VOID
PDEVOBJ_vFilterDriverHooks(
    _In_ PPDEVOBJ ppdev)
{
    PLDEVOBJ pldev = ppdev->pldev;
    ULONG dwAccelerationLevel = ppdev->dwAccelerationLevel;

    if (!pldev->pGdiDriverInfo)
        return;
    if (pldev->ldevtype != LDEV_DEVICE_DISPLAY)
        return;

    if (dwAccelerationLevel >= 1)
    {
        ppdev->apfn[INDEX_DrvSetPointerShape] = NULL;
        ppdev->apfn[INDEX_DrvCreateDeviceBitmap] = NULL;
    }

    if (dwAccelerationLevel >= 2)
    {
        /* Remove sophisticated display accelerations */
        ppdev->pSurface->flags &= ~(HOOK_STRETCHBLT |
                                    HOOK_FILLPATH |
                                    HOOK_GRADIENTFILL |
                                    HOOK_LINETO |
                                    HOOK_ALPHABLEND |
                                    HOOK_TRANSPARENTBLT);
    }

    if (dwAccelerationLevel >= 3)
    {
        /* Disable DirectDraw and Direct3D accelerations */
        /* FIXME: need to call DxDdSetAccelLevel */
        UNIMPLEMENTED;
    }

    if (dwAccelerationLevel >= 4)
    {
        /* Remove almost all display accelerations */
        ppdev->pSurface->flags &= ~HOOK_FLAGS |
                                   HOOK_BITBLT |
                                   HOOK_COPYBITS |
                                   HOOK_TEXTOUT |
                                   HOOK_STROKEPATH |
                                   HOOK_SYNCHRONIZE;

    }

    if (dwAccelerationLevel >= 5)
    {
        /* Disable all display accelerations */
        /* (nothing to do. Already handled in PDEVOBJ_Create) */
    }
}

PSURFACE
NTAPI
PDEVOBJ_pSurface(
    PPDEVOBJ ppdev)
{
    HSURF hsurf;
    PSURFACE pSurface;

    EngAcquireSemaphore(ppdev->hsemPointer);

    /* Check if there is no surface for this PDEV yet */
    if (ppdev->pSurface == NULL)
    {
        /* Call the drivers DrvEnableSurface */
        TRACE("DrvEnableSurface(dhpdev %p)\n", ppdev->dhpdev);
        hsurf = ppdev->pfn.EnableSurface(ppdev->dhpdev);
        TRACE("DrvEnableSurface(dhpdev %p) => hsurf %p\n", ppdev->dhpdev, hsurf);
        if (hsurf== NULL)
        {
            ERR("Failed to create PDEV surface!\n");
            EngReleaseSemaphore(ppdev->hsemPointer);
            return NULL;
        }

        /* Get a reference to the surface */
        ppdev->pSurface = SURFACE_ShareLockSurface(hsurf);
        NT_ASSERT(ppdev->pSurface != NULL);
    }

    /* Increment reference count */
    GDIOBJ_vReferenceObjectByPointer(&ppdev->pSurface->BaseObject);

    pSurface = ppdev->pSurface;
    EngReleaseSemaphore(ppdev->hsemPointer);
    return pSurface;
}

BOOL
PDEVOBJ_bEnableDirectDraw(
    _Inout_ PPDEVOBJ ppdev)
{
    PGD_DXDDENABLEDIRECTDRAW pfnDdEnableDirectDraw = (PGD_DXDDENABLEDIRECTDRAW)gpDxFuncs[DXG_INDEX_DxDdEnableDirectDraw].pfn;
    BOOL Success;

    /* Without the legacy DirectX kernel there is nothing to enable. */
    if (pfnDdEnableDirectDraw == NULL)
        return TRUE;

    /* Enable DirectDraw */
    TRACE("DxDdEnableDirectDraw(ppdev %p)\n", ppdev);
    Success = pfnDdEnableDirectDraw((HDEV)ppdev, TRUE);
    TRACE("DxDdEnableDirectDraw(ppdev %p) => %d\n", ppdev, Success);

    return Success;
}

VOID
PDEVOBJ_vResumeDirectDraw(
    _Inout_ PPDEVOBJ ppdev)
{
    PGD_DXDDRESUMEDIRECTDRAW pfnDdResumeDirectDraw = (PGD_DXDDRESUMEDIRECTDRAW)gpDxFuncs[DXG_INDEX_DxDdResumeDirectDraw].pfn;

    if (pfnDdResumeDirectDraw == NULL)
        return;

    /* Resume DirectDraw after mode change */
    TRACE("DxDdResumeDirectDraw(ppdev %p)\n", ppdev);
    pfnDdResumeDirectDraw((HDEV)ppdev, 0);
}

VOID
PDEVOBJ_vSuspendDirectDraw(
    _Inout_ PPDEVOBJ ppdev)
{
    PGD_DXDDSUSPENDDIRECTDRAW pfnDdSuspendDirectDraw = (PGD_DXDDSUSPENDDIRECTDRAW)gpDxFuncs[DXG_INDEX_DxDdSuspendDirectDraw].pfn;

    if (pfnDdSuspendDirectDraw == NULL)
        return;

    /* Suspend DirectDraw for mode change */
    TRACE("DxDdSuspendDirectDraw(ppdev %p)\n", ppdev);
    pfnDdSuspendDirectDraw((HDEV)ppdev, 0);
}

VOID
PDEVOBJ_vSwitchDirectDraw(
    _Inout_ PPDEVOBJ ppdev,
    _Inout_ PPDEVOBJ ppdev2)
{
    PGD_DXDDDYNAMICMODECHANGE pfnDdDynamicModeChange = (PGD_DXDDDYNAMICMODECHANGE)gpDxFuncs[DXG_INDEX_DxDdDynamicModeChange].pfn;

    if (pfnDdDynamicModeChange == NULL)
        return;

    /* Switch DirectDraw instances between the PDEVs */
    TRACE("DxDdDynamicModeChange(ppdev %p, ppdev2 %p)\n", ppdev, ppdev2);
    pfnDdDynamicModeChange((HDEV)ppdev, (HDEV)ppdev2, 0);
}

VOID
PDEVOBJ_vEnableDisplay(
    _Inout_ PPDEVOBJ ppdev)
{
    BOOL assertVal;

    EngAcquireSemaphore(ppdev->hsemPointer);
    if (!(ppdev->flFlags & PDEV_DISABLED))
    {
        EngReleaseSemaphore(ppdev->hsemPointer);
        return;
    }

    /* Try to enable display until success */
    do
    {
        TRACE("DrvAssertMode(dhpdev %p, TRUE)\n", ppdev->dhpdev);
        assertVal = ppdev->pfn.AssertMode(ppdev->dhpdev, TRUE);
        TRACE("DrvAssertMode(dhpdev %p, TRUE) => %d\n", ppdev->dhpdev, assertVal);
    } while (!assertVal);

    ppdev->flFlags &= ~PDEV_DISABLED;
    EngReleaseSemaphore(ppdev->hsemPointer);
}

BOOL
PDEVOBJ_bDisableDisplay(
    _Inout_ PPDEVOBJ ppdev)
{
    BOOL assertVal;

    EngAcquireSemaphore(ppdev->hsemPointer);
    if (ppdev->flFlags & PDEV_DISABLED)
    {
        EngReleaseSemaphore(ppdev->hsemPointer);
        return TRUE;
    }

    PDEVOBJ_vSuspendDirectDraw(ppdev);

    TRACE("DrvAssertMode(dhpdev %p, FALSE)\n", ppdev->dhpdev);
    assertVal = ppdev->pfn.AssertMode(ppdev->dhpdev, FALSE);
    TRACE("DrvAssertMode(dhpdev %p, FALSE) => %d\n", ppdev->dhpdev, assertVal);

    if (assertVal)
    {
        ppdev->flFlags |= PDEV_DISABLED;
        ppdev->pfnAsyncMovePointer = NULL;
    }

    EngReleaseSemaphore(ppdev->hsemPointer);
    return assertVal;
}

VOID
NTAPI
PDEVOBJ_vRefreshModeList(
    PPDEVOBJ ppdev)
{
    PGRAPHICS_DEVICE pGraphicsDevice;

    /* Lock the PDEV */
    EngAcquireSemaphore(ppdev->hsemDevLock);

    pGraphicsDevice = ppdev->pGraphicsDevice;

    /* Update available display mode list */
    EngpBuildDetachedModeList(pGraphicsDevice, TRUE);

    /* Unlock PDEV */
    EngReleaseSemaphore(ppdev->hsemDevLock);
}

BOOL
PDEVOBJ_bGraphicsDeviceInUse(
    _In_ PGRAPHICS_DEVICE pGraphicsDevice)
{
    PPDEVOBJ ppdev;
    BOOL bInUse = FALSE;

    EngAcquireSemaphoreShared(ghsemPDEV);
    for (ppdev = gppdevList; ppdev && !bInUse; ppdev = ppdev->ppdevNext)
        bInUse = ppdev->pGraphicsDevice == pGraphicsDevice;
    EngReleaseSemaphore(ghsemPDEV);

    return bInUse;
}

PPDEVOBJ
PDEVOBJ_Create(
    _In_opt_ PGRAPHICS_DEVICE pGraphicsDevice,
    _In_opt_ PDEVMODEW pdm,
    _In_ ULONG dwAccelerationLevel,
    _In_ ULONG ldevtype)
{
    PPDEVOBJ ppdev, ppdevMatch = NULL;
    PLDEVOBJ pldev;
    PSURFACE pSurface;

    TRACE("PDEVOBJ_Create(%p %p %d)\n", pGraphicsDevice, pdm, ldevtype);

    if (ldevtype == LDEV_DEVICE_DISPLAY && pGraphicsDevice && pdm)
    {
        pdm->dmLogPixels = (WORD)UserGetSystemDpi();
        pdm->dmFields |= DM_LOGPIXELS;
    }

    if (ldevtype != LDEV_DEVICE_META)
    {
        ASSERT(pGraphicsDevice);
        ASSERT(pdm);
        /* Search if we already have a PPDEV with the required characteristics.
         * We will compare the graphics device, the devmode and the desktop
         */
        for (ppdev = gppdevList; ppdev; ppdev = ppdev->ppdevNext)
        {
            if (ppdev->pGraphicsDevice == pGraphicsDevice)
            {
                PDEVOBJ_vReference(ppdev);

                if (RtlEqualMemory(pdm, ppdev->pdmwDev, sizeof(DEVMODEW)) &&
                    ppdev->dwAccelerationLevel == dwAccelerationLevel)
                {
                    PDEVOBJ_vReference(ppdev);
                    ppdevMatch = ppdev;
                }
                else
                {
                    PDEVOBJ_bDisableDisplay(ppdev);
                }

                PDEVOBJ_vRelease(ppdev);
            }
        }

        if (ppdevMatch)
        {
            PDEVOBJ_vEnableDisplay(ppdevMatch);

            return ppdevMatch;
        }
    }

    /* Try to get a display driver */
    if (ldevtype == LDEV_DEVICE_META)
        pldev = LDEVOBJ_pLoadInternal(MultiEnableDriver, ldevtype);
    else
        pldev = LDEVOBJ_pLoadDriver(pdm->dmDeviceName, ldevtype);
    if (!pldev)
    {
        ERR("Could not load display driver '%S'\n",
             (ldevtype == LDEV_DEVICE_META) ? L"" : pdm->dmDeviceName);
        return NULL;
    }

    /* Allocate a new PDEVOBJ */
    ppdev = PDEVOBJ_AllocPDEV();
    if (!ppdev)
    {
        ERR("failed to allocate a PDEV\n");
        return NULL;
    }

    if (ldevtype != LDEV_DEVICE_META)
    {
        ppdev->pGraphicsDevice = pGraphicsDevice;

        // DxEngGetHdevData asks for Graphics DeviceObject in hSpooler field
        ppdev->hSpooler = ppdev->pGraphicsDevice->DeviceObject;

        /* Keep selected resolution */
        if (ppdev->pdmwDev)
            ExFreePoolWithTag(ppdev->pdmwDev, GDITAG_DEVMODE);
        ppdev->pdmwDev = ExAllocatePoolWithTag(PagedPool, pdm->dmSize + pdm->dmDriverExtra, GDITAG_DEVMODE);
        if (ppdev->pdmwDev)
        {
            RtlCopyMemory(ppdev->pdmwDev, pdm, pdm->dmSize + pdm->dmDriverExtra);
            /* FIXME: this must be done in a better way */
            pGraphicsDevice->StateFlags |= DISPLAY_DEVICE_ATTACHED_TO_DESKTOP;
        }
    }

    /* FIXME! */
    ppdev->flFlags = PDEV_DISPLAY;

    /* HACK: Don't use the pointer */
    ppdev->Pointer.Exclude.right = -1;

    /* Initialize PDEV */
    ppdev->pldev = pldev;
    ppdev->dwAccelerationLevel = dwAccelerationLevel;

    /* Copy the function table */
    if (ldevtype == LDEV_DEVICE_DISPLAY && (dwAccelerationLevel >= 5 ||
        pdm->dmFields & (DM_PANNINGWIDTH | DM_PANNINGHEIGHT)))
    {
        ULONG i;

        /* Initialize missing fields */
        if (!(pdm->dmFields & DM_PANNINGWIDTH))
            pdm->dmPanningWidth = pdm->dmPelsWidth;
        if (!(pdm->dmFields & DM_PANNINGHEIGHT))
            pdm->dmPanningHeight = pdm->dmPelsHeight;

        /* Replace vtable by panning vtable */
        for (i = 0; i < gPanDispDrvCount; i++)
            ppdev->apfn[gPanDispDrvFn[i].iFunc] = gPanDispDrvFn[i].pfn;
    }
    else
    {
        ppdev->pfn = ppdev->pldev->pfn;
    }

    /* Call the driver to enable the PDEV */
    if (!PDEVOBJ_bEnablePDEV(ppdev, pdm, NULL))
    {
        ERR("Failed to enable PDEV!\n");
        PDEVOBJ_vRelease(ppdev);
        EngUnloadImage(pldev);
        return NULL;
    }

    /* Tell the driver that the PDEV is ready */
    PDEVOBJ_vCompletePDEV(ppdev);

    /* Create the initial surface */
    pSurface = PDEVOBJ_pSurface(ppdev);
    if (!pSurface)
    {
        ERR("Failed to create surface\n");
        PDEVOBJ_vRelease(ppdev);
        EngUnloadImage(pldev);
        return NULL;
    }

    /* Enable DirectDraw */
    if (!PDEVOBJ_bEnableDirectDraw(ppdev))
    {
        ERR("Failed to enable DirectDraw\n");
        PDEVOBJ_vRelease(ppdev);
        EngUnloadImage(pldev);
        return NULL;
    }

    /* Remove some acceleration capabilities from driver */
    PDEVOBJ_vFilterDriverHooks(ppdev);

    /* Set MovePointer function */
    ppdev->pfnMovePointer = ppdev->pfn.MovePointer;
    if (!ppdev->pfnMovePointer)
        ppdev->pfnMovePointer = EngMovePointer;

    /* Insert the PDEV into the list */
    ppdev->ppdevNext = gppdevList;
    gppdevList = ppdev;

    /* Return the PDEV */
    return ppdev;
}

FORCEINLINE
VOID
SwitchPointer(
    _Inout_ PVOID pvPointer1,
    _Inout_ PVOID pvPointer2)
{
    PVOID *ppvPointer1 = pvPointer1;
    PVOID *ppvPointer2 = pvPointer2;
    PVOID pvTemp;

    pvTemp = *ppvPointer1;
    *ppvPointer1 = *ppvPointer2;
    *ppvPointer2 = pvTemp;
}

BOOL
NTAPI
PDEVOBJ_bDynamicModeChange(
    PPDEVOBJ ppdev,
    PPDEVOBJ ppdev2)
{
    PPDEVOBJ ppdevFirst = (ULONG_PTR)ppdev < (ULONG_PTR)ppdev2 ? ppdev : ppdev2;
    PPDEVOBJ ppdevSecond = ppdevFirst == ppdev ? ppdev2 : ppdev;
    union
    {
        DRIVER_FUNCTIONS pfn;
        GDIINFO gdiinfo;
        DEVINFO devinfo;
        DWORD StateFlags;
    } temp;

    EngAcquireSemaphore(ppdevFirst->hsemPointer);
    EngAcquireSemaphore(ppdevSecond->hsemPointer);
    /* A pointer must be selected for the replacement surface before it can
     * move independently of drawing again. */
    ppdev->pfnAsyncMovePointer = NULL;
    ppdev2->pfnAsyncMovePointer = NULL;

    /* Exchange driver functions */
    temp.pfn = ppdev->pfn;
    ppdev->pfn = ppdev2->pfn;
    ppdev2->pfn = temp.pfn;
    ppdev->pfnMovePointer = ppdev->pfn.MovePointer ? ppdev->pfn.MovePointer : EngMovePointer;
    ppdev2->pfnMovePointer = ppdev2->pfn.MovePointer ? ppdev2->pfn.MovePointer : EngMovePointer;

    /* Exchange LDEVs */
    SwitchPointer(&ppdev->pldev, &ppdev2->pldev);

    /* Exchange DHPDEV */
    SwitchPointer(&ppdev->dhpdev, &ppdev2->dhpdev);

    /* Exchange surfaces and associate them with their new PDEV */
    SwitchPointer(&ppdev->pSurface, &ppdev2->pSurface);
    ppdev->pSurface->SurfObj.hdev = (HDEV)ppdev;
    ppdev2->pSurface->SurfObj.hdev = (HDEV)ppdev2;

    /* Exchange devinfo */
    temp.devinfo = ppdev->devinfo;
    ppdev->devinfo = ppdev2->devinfo;
    ppdev2->devinfo = temp.devinfo;

    /* Exchange gdiinfo */
    temp.gdiinfo = ppdev->gdiinfo;
    ppdev->gdiinfo = ppdev2->gdiinfo;
    ppdev2->gdiinfo = temp.gdiinfo;

    /* Exchange DEVMODE */
    SwitchPointer(&ppdev->pdmwDev, &ppdev2->pdmwDev);

    /* Exchange state flags; the desktop of several displays has no graphics device */
    if (ppdev->pGraphicsDevice != NULL && ppdev2->pGraphicsDevice != NULL)
    {
        temp.StateFlags = ppdev->pGraphicsDevice->StateFlags;
        ppdev->pGraphicsDevice->StateFlags = ppdev2->pGraphicsDevice->StateFlags;
        ppdev2->pGraphicsDevice->StateFlags = temp.StateFlags;
    }

    /* Notify each driver instance of its new HDEV association */
    ppdev->pfn.CompletePDEV(ppdev->dhpdev, (HDEV)ppdev);
    ppdev2->pfn.CompletePDEV(ppdev2->dhpdev, (HDEV)ppdev2);

    /* Switch DirectDraw mode */
    PDEVOBJ_vSwitchDirectDraw(ppdev, ppdev2);

    EngReleaseSemaphore(ppdevSecond->hsemPointer);
    EngReleaseSemaphore(ppdevFirst->hsemPointer);
    return TRUE;
}


BOOL
NTAPI
PDEVOBJ_bSwitchMode(
    PPDEVOBJ ppdev,
    PDEVMODEW pdm)
{
    PPDEVOBJ ppdevTmp;
    PSURFACE pSurface;
    BOOL retval = FALSE;

    /* Lock the PDEV */
    EngAcquireSemaphore(ppdev->hsemDevLock);

    /* And everything else */
    EngAcquireSemaphore(ghsemPDEV);

    DPRINT1("PDEVOBJ_bSwitchMode, ppdev = %p, pSurface = %p\n", ppdev, ppdev->pSurface);

    // Lookup the GraphicsDevice + select DEVMODE
    // pdm = LDEVOBJ_bProbeAndCaptureDevmode(ppdev, pdm);

    /* 1. Temporarily disable the current PDEV and reset video to its default mode */
    if (!PDEVOBJ_bDisableDisplay(ppdev))
    {
        DPRINT1("PDEVOBJ_bDisableDisplay() failed\n");
        /* Resume DirectDraw in case of failure */
        PDEVOBJ_vResumeDirectDraw(ppdev);
        goto leave;
    }

    /* 2. Create new PDEV */
    ppdevTmp = PDEVOBJ_Create(ppdev->pGraphicsDevice, pdm, 0, LDEV_DEVICE_DISPLAY);
    if (!ppdevTmp)
    {
        DPRINT1("Failed to create a new PDEV\n");
        goto leave2;
    }

    /* 3. Create a new surface */
    pSurface = PDEVOBJ_pSurface(ppdevTmp);
    if (!pSurface)
    {
        DPRINT1("PDEVOBJ_pSurface failed\n");
        PDEVOBJ_vRelease(ppdevTmp);
        goto leave2;
    }

    /* 4. Temporarily suspend DirectDraw for mode change */
    PDEVOBJ_vSuspendDirectDraw(ppdev);
    PDEVOBJ_vSuspendDirectDraw(ppdevTmp);

    /* 5. Switch the PDEVs */
    if (!PDEVOBJ_bDynamicModeChange(ppdev, ppdevTmp))
    {
        DPRINT1("PDEVOBJ_bDynamicModeChange() failed\n");
        PDEVOBJ_vRelease(ppdevTmp);
        goto leave2;
    }

    /* 6. Resume DirectDraw */
    PDEVOBJ_vResumeDirectDraw(ppdev);
    PDEVOBJ_vResumeDirectDraw(ppdevTmp);

    /* Release temp PDEV */
    PDEVOBJ_vRelease(ppdevTmp);

    /* Re-initialize DirectDraw data */
    ppdev->pEDDgpl->hDev = (HDEV)ppdev;
    ppdev->pEDDgpl->dhpdev = ppdev->dhpdev;

    /* Update primary display capabilities */
    if (ppdev == gpmdev->ppdevGlobal)
    {
        PDEVOBJ_vGetDeviceCaps(ppdev, &GdiHandleTable->DevCaps);
    }

    /* Success! */
    retval = TRUE;

leave2:
    /* Set the new video mode, or restore the original one in case of failure */
    PDEVOBJ_vEnableDisplay(ppdev);

leave:
    /* Unlock everything else */
    EngReleaseSemaphore(ghsemPDEV);
    /* Unlock the PDEV */
    EngReleaseSemaphore(ppdev->hsemDevLock);

    DPRINT1("leave, ppdev = %p, pSurface = %p\n", ppdev, ppdev->pSurface);

    return retval;
}

/* Adds a display to the desktop. The desktop's PDEV keeps its identity, so its DCs follow: it takes the
 * internals of a multi-display PDEV built over every display, and when it was the only display, the new
 * PDEV takes over that display's internals and identity. */
BOOL
NTAPI
PDEVOBJ_bAttachDisplay(
    _Inout_ PMDEVOBJ *ppmdev,
    _In_ PGRAPHICS_DEVICE pGraphicsDevice,
    _In_ PDEVMODEW pdm,
    _Out_ PPDEVOBJ *pppdevDisplay)
{
    PMDEVOBJ pmdevOld = *ppmdev, pmdevNew;
    PPDEVOBJ ppdevGlobal = pmdevOld->ppdevGlobal;
    PPDEVOBJ ppdevDisplay, ppdevMeta;
    BOOL bSingle = pmdevOld->cDev == 1;
    BOOL bSuccess = FALSE;
    ULONG i;

    ASSERT(pmdevOld->cDev != 0);
    ASSERT(bSingle == (ppdevGlobal == pmdevOld->dev[0].ppdev));
    ASSERT(pGraphicsDevice != NULL);
    *pppdevDisplay = NULL;

    pmdevNew = ExAllocatePoolZero(PagedPool, sizeof(MDEVOBJ) + (pmdevOld->cDev + 1) * sizeof(MDEVDISPLAY), GDITAG_MDEV);
    if (pmdevNew == NULL)
        return FALSE;

    EngAcquireSemaphore(ppdevGlobal->hsemDevLock);
    EngAcquireSemaphore(ghsemPDEV);
    ppdevDisplay = PDEVOBJ_Create(pGraphicsDevice, pdm, EngpGetDisplayDriverAccelerationLevel(pGraphicsDevice),
                                  LDEV_DEVICE_DISPLAY);
    if (ppdevDisplay != NULL && PDEVOBJ_pSurface(ppdevDisplay) != NULL)
    {
        for (i = 0; i < pmdevOld->cDev; i++)
        {
            PDEVOBJ_vReference(pmdevOld->dev[i].ppdev);
            pmdevNew->dev[i].ppdev = pmdevOld->dev[i].ppdev;
        }
        pmdevNew->dev[i].ppdev = ppdevDisplay;
        pmdevNew->cDev = pmdevOld->cDev + 1;

        ppdevMeta = PDEVOBJ_Create(NULL, (PDEVMODEW)pmdevNew, 0, LDEV_DEVICE_META);
        if (ppdevMeta != NULL && PDEVOBJ_pSurface(ppdevMeta) != NULL)
        {
            PDEVOBJ_vSuspendDirectDraw(ppdevGlobal);
            PDEVOBJ_vSuspendDirectDraw(ppdevMeta);
            bSuccess = PDEVOBJ_bDynamicModeChange(ppdevGlobal, ppdevMeta);
            PDEVOBJ_vResumeDirectDraw(ppdevGlobal);
            PDEVOBJ_vResumeDirectDraw(ppdevMeta);
        }
        if (bSuccess && bSingle)
        {
            /* The former single display now lives in the new PDEV: it takes that display's identity too */
            SwitchPointer(&ppdevGlobal->pGraphicsDevice, &ppdevMeta->pGraphicsDevice);
            SwitchPointer(&ppdevGlobal->hSpooler, &ppdevMeta->hSpooler);
            SwitchPointer(&ppdevGlobal->ppalSurf, &ppdevMeta->ppalSurf);
            ppdevMeta->dwAccelerationLevel = ppdevGlobal->dwAccelerationLevel;
            ppdevGlobal->dwAccelerationLevel = 0;
            ASSERT(ppdevGlobal->pGraphicsDevice == NULL);
            ASSERT(pmdevNew->dev[0].ppdev == ppdevGlobal);
            pmdevNew->dev[0].ppdev = ppdevMeta;
            PDEVOBJ_vRelease(ppdevGlobal);
            ppdevMeta = NULL;
        }
        if (ppdevMeta != NULL)
            PDEVOBJ_vRelease(ppdevMeta);
        if (bSuccess)
        {
            PDEVOBJ_vReference(ppdevGlobal);
            pmdevNew->ppdevGlobal = ppdevGlobal;
            PDEVOBJ_vGetDeviceCaps(ppdevGlobal, &GdiHandleTable->DevCaps);
            *pppdevDisplay = ppdevDisplay;
        }
        else
        {
            for (i = 0; i < pmdevNew->cDev; i++)
                PDEVOBJ_vRelease(pmdevNew->dev[i].ppdev);
            ppdevDisplay = NULL;
        }
    }
    else if (ppdevDisplay != NULL)
    {
        PDEVOBJ_vRelease(ppdevDisplay);
    }
    EngReleaseSemaphore(ghsemPDEV);
    EngReleaseSemaphore(ppdevGlobal->hsemDevLock);

    if (!bSuccess)
    {
        ExFreePoolWithTag(pmdevNew, GDITAG_MDEV);
        return FALSE;
    }
    *ppmdev = pmdevNew;
    MDEVOBJ_vDestroy(pmdevOld);
    return TRUE;
}

/* Removes a display from a desktop of several displays. The desktop's PDEV keeps its identity: it takes the
 * internals of a multi-display PDEV over the remaining displays, or those of the last remaining display. */
BOOL
NTAPI
PDEVOBJ_bDetachDisplay(
    _Inout_ PMDEVOBJ *ppmdev,
    _In_ PPDEVOBJ ppdevDisplay,
    _Out_ PPDEVOBJ *pppdevRemaining)
{
    PMDEVOBJ pmdevOld = *ppmdev, pmdevNew;
    PPDEVOBJ ppdevGlobal = pmdevOld->ppdevGlobal;
    PPDEVOBJ ppdevSwap = NULL;
    BOOL bSingle = pmdevOld->cDev == 2;
    BOOL bSuccess = FALSE;
    ULONG i, j;

    ASSERT(pmdevOld->cDev > 1);
    ASSERT(ppdevGlobal != ppdevDisplay);
    ASSERT(ppdevGlobal->pGraphicsDevice == NULL);
    *pppdevRemaining = NULL;
    for (i = 0; i < pmdevOld->cDev && pmdevOld->dev[i].ppdev != ppdevDisplay; i++)
        ;
    if (i == pmdevOld->cDev)
        return FALSE;

    pmdevNew = ExAllocatePoolZero(PagedPool, sizeof(MDEVOBJ) + (pmdevOld->cDev - 1) * sizeof(MDEVDISPLAY), GDITAG_MDEV);
    if (pmdevNew == NULL)
        return FALSE;
    for (i = 0, j = 0; i < pmdevOld->cDev; i++)
    {
        if (pmdevOld->dev[i].ppdev == ppdevDisplay)
            continue;
        PDEVOBJ_vReference(pmdevOld->dev[i].ppdev);
        pmdevNew->dev[j++].ppdev = pmdevOld->dev[i].ppdev;
    }
    pmdevNew->cDev = j;
    ASSERT(j == pmdevOld->cDev - 1);

    EngAcquireSemaphore(ppdevGlobal->hsemDevLock);
    EngAcquireSemaphore(ghsemPDEV);
    if (bSingle)
    {
        /* The last display moves back into the desktop's PDEV, with its identity */
        ppdevSwap = pmdevNew->dev[0].ppdev;
        PDEVOBJ_vSuspendDirectDraw(ppdevGlobal);
        PDEVOBJ_vSuspendDirectDraw(ppdevSwap);
        bSuccess = PDEVOBJ_bDynamicModeChange(ppdevGlobal, ppdevSwap);
        PDEVOBJ_vResumeDirectDraw(ppdevGlobal);
        PDEVOBJ_vResumeDirectDraw(ppdevSwap);
        if (bSuccess)
        {
            SwitchPointer(&ppdevGlobal->pGraphicsDevice, &ppdevSwap->pGraphicsDevice);
            SwitchPointer(&ppdevGlobal->hSpooler, &ppdevSwap->hSpooler);
            SwitchPointer(&ppdevGlobal->ppalSurf, &ppdevSwap->ppalSurf);
            ppdevGlobal->dwAccelerationLevel = ppdevSwap->dwAccelerationLevel;
            ppdevSwap->dwAccelerationLevel = 0;
            ASSERT(ppdevSwap->pGraphicsDevice == NULL);
            PDEVOBJ_vReference(ppdevGlobal);
            pmdevNew->dev[0].ppdev = ppdevGlobal;
            pmdevNew->ppdevGlobal = ppdevGlobal;
            *pppdevRemaining = ppdevSwap;
            /* The old MDEV still references the PDEV that now holds the multi-display internals */
            PDEVOBJ_vRelease(ppdevSwap);
        }
    }
    else
    {
        ppdevSwap = PDEVOBJ_Create(NULL, (PDEVMODEW)pmdevNew, 0, LDEV_DEVICE_META);
        if (ppdevSwap != NULL && PDEVOBJ_pSurface(ppdevSwap) != NULL)
        {
            PDEVOBJ_vSuspendDirectDraw(ppdevGlobal);
            PDEVOBJ_vSuspendDirectDraw(ppdevSwap);
            bSuccess = PDEVOBJ_bDynamicModeChange(ppdevGlobal, ppdevSwap);
            PDEVOBJ_vResumeDirectDraw(ppdevGlobal);
            PDEVOBJ_vResumeDirectDraw(ppdevSwap);
        }
        if (ppdevSwap != NULL)
            PDEVOBJ_vRelease(ppdevSwap);
        if (bSuccess)
        {
            PDEVOBJ_vReference(ppdevGlobal);
            pmdevNew->ppdevGlobal = ppdevGlobal;
        }
    }
    if (bSuccess)
    {
        ppdevDisplay->pGraphicsDevice->StateFlags &= ~DISPLAY_DEVICE_ATTACHED_TO_DESKTOP;
        PDEVOBJ_vGetDeviceCaps(ppdevGlobal, &GdiHandleTable->DevCaps);
    }
    EngReleaseSemaphore(ghsemPDEV);
    EngReleaseSemaphore(ppdevGlobal->hsemDevLock);

    if (!bSuccess)
    {
        for (i = 0; i < pmdevNew->cDev; i++)
            PDEVOBJ_vRelease(pmdevNew->dev[i].ppdev);
        ExFreePoolWithTag(pmdevNew, GDITAG_MDEV);
        return FALSE;
    }
    *ppmdev = pmdevNew;
    MDEVOBJ_vDestroy(pmdevOld);
    return TRUE;
}

/* Switches one display of a multi-display desktop, then rebuilds the desktop surface for the new layout */
BOOL
NTAPI
PDEVOBJ_bSwitchMultiDisplayMode(
    _Inout_ PMDEVOBJ pmdev,
    _Inout_ PPDEVOBJ ppdevDisplay,
    _In_ PDEVMODEW pdm)
{
    PPDEVOBJ ppdevGlobal = pmdev->ppdevGlobal;
    PPDEVOBJ ppdevTmp;
    PDEVMODEW pdmOld = NULL;
    BOOL bSuccess = FALSE;
    ULONG i;

    ASSERT(pmdev->cDev > 1);
    ASSERT(ppdevGlobal != ppdevDisplay);
    ASSERT(ppdevGlobal->pGraphicsDevice == NULL);
    ASSERT(ppdevDisplay->pGraphicsDevice != NULL);
    for (i = 0; i < pmdev->cDev && pmdev->dev[i].ppdev != ppdevDisplay; i++)
        ;
    ASSERT(i < pmdev->cDev);
    if (i == pmdev->cDev || ppdevDisplay->pdmwDev == NULL)
        return FALSE;

    pdmOld = ExAllocatePoolWithTag(PagedPool, ppdevDisplay->pdmwDev->dmSize + ppdevDisplay->pdmwDev->dmDriverExtra, GDITAG_DEVMODE);
    if (pdmOld == NULL)
        return FALSE;
    RtlCopyMemory(pdmOld, ppdevDisplay->pdmwDev, ppdevDisplay->pdmwDev->dmSize + ppdevDisplay->pdmwDev->dmDriverExtra);

    /* Drawing to the desktop waits on its device lock until the new desktop surface is in place */
    EngAcquireSemaphore(ppdevGlobal->hsemDevLock);
    EngAcquireSemaphore(ghsemPDEV);
    if (PDEVOBJ_bSwitchMode(ppdevDisplay, pdm))
    {
        ppdevTmp = PDEVOBJ_Create(NULL, (PDEVMODEW)pmdev, 0, LDEV_DEVICE_META);
        if (ppdevTmp != NULL)
        {
            if (PDEVOBJ_pSurface(ppdevTmp) != NULL)
            {
                PDEVOBJ_vSuspendDirectDraw(ppdevGlobal);
                PDEVOBJ_vSuspendDirectDraw(ppdevTmp);
                bSuccess = PDEVOBJ_bDynamicModeChange(ppdevGlobal, ppdevTmp);
                PDEVOBJ_vResumeDirectDraw(ppdevGlobal);
                PDEVOBJ_vResumeDirectDraw(ppdevTmp);
            }
            PDEVOBJ_vRelease(ppdevTmp);
        }
        if (!bSuccess && !PDEVOBJ_bSwitchMode(ppdevDisplay, pdmOld))
        {
            /* FIXME: the display keeps its new mode while the desktop surface keeps the old layout */
            ERR("Failed to restore the mode of display %p\n", ppdevDisplay);
        }
    }
    if (bSuccess)
        PDEVOBJ_vGetDeviceCaps(ppdevGlobal, &GdiHandleTable->DevCaps);
    EngReleaseSemaphore(ghsemPDEV);
    EngReleaseSemaphore(ppdevGlobal->hsemDevLock);
    ExFreePoolWithTag(pdmOld, GDITAG_DEVMODE);
    return bSuccess;
}

NTSTATUS
NTAPI
EngpGetPrimaryDisplayNtName(
    _Out_writes_(cchName) PWSTR pwszName,
    _In_ SIZE_T cchName)
{
    PPDEVOBJ ppdevPrimary;
    NTSTATUS Status = STATUS_NOT_FOUND;

    ASSERT(pwszName != NULL);
    ASSERT(cchName != 0);
    EngAcquireSemaphoreShared(ghsemPDEV);
    if (gpmdev && gpmdev->cDev != 0)
    {
        ppdevPrimary = MultiPrimaryDisplay(gpmdev);
        ASSERT(ppdevPrimary != NULL);
        if (ppdevPrimary->pGraphicsDevice && ppdevPrimary->pGraphicsDevice->szNtDeviceName[0] != L'\0')
            Status = RtlStringCchCopyW(pwszName, cchName, ppdevPrimary->pGraphicsDevice->szNtDeviceName);
    }
    EngReleaseSemaphore(ghsemPDEV);
    return Status;
}

PPDEVOBJ
NTAPI
EngpGetPDEV(
    _In_opt_ PUNICODE_STRING pustrDeviceName)
{
    UNICODE_STRING ustrCurrent;
    PPDEVOBJ ppdev = NULL;
    PGRAPHICS_DEVICE pGraphicsDevice;
    ULONG i;

    /* Acquire PDEV lock */
    EngAcquireSemaphoreShared(ghsemPDEV);

    /* Did the caller pass a device name after mode devices were initialized? */
    if (pustrDeviceName && gpmdev)
    {
        /* Loop all present PDEVs */
        for (i = 0; i < gpmdev->cDev; i++)
        {
            /* Get a pointer to the GRAPHICS_DEVICE */
            pGraphicsDevice = gpmdev->dev[i].ppdev->pGraphicsDevice;

            /* Compare the name */
            RtlInitUnicodeString(&ustrCurrent, pGraphicsDevice->szWinDeviceName);
            if (RtlEqualUnicodeString(pustrDeviceName, &ustrCurrent, TRUE))
            {
                /* Found! */
                ppdev = gpmdev->dev[i].ppdev;
                break;
            }
        }
    }
    else if (gpmdev)
    {
        /* Otherwise use the primary PDEV */
        ppdev = gpmdev->ppdevGlobal;
    }

    /* Did we find one? */
    if (ppdev)
    {
        /* Yes, reference the PDEV */
        PDEVOBJ_vReference(ppdev);
    }

    /* Release PDEV lock */
    EngReleaseSemaphore(ghsemPDEV);

    return ppdev;
}

LONG
PDEVOBJ_lChangeDisplaySettings(
    _In_opt_ PUNICODE_STRING pustrDeviceName,
    _In_opt_ PDEVMODEW RequestedMode,
    _In_opt_ PMDEVOBJ pmdevOld,
    _Out_ PMDEVOBJ *ppmdevNew,
    _In_ BOOL bSearchClosestMode)
{
    PGRAPHICS_DEVICE pGraphicsDevice = NULL;
    PMDEVOBJ pmdev = NULL;
    PDEVMODEW pdm = NULL;
    ULONG lRet = DISP_CHANGE_SUCCESSFUL;
    ULONG i, j;

    TRACE("PDEVOBJ_lChangeDisplaySettings('%wZ' '%dx%dx%d (%d Hz)' %p %p)\n",
        pustrDeviceName,
        RequestedMode ? RequestedMode->dmPelsWidth : 0,
        RequestedMode ? RequestedMode->dmPelsHeight : 0,
        RequestedMode ? RequestedMode->dmBitsPerPel : 0,
        RequestedMode ? RequestedMode->dmDisplayFrequency : 0,
        pmdevOld, ppmdevNew);

    if (pustrDeviceName)
    {
        pGraphicsDevice = EngpFindGraphicsDevice(pustrDeviceName, 0);
        if (!pGraphicsDevice)
        {
            ERR("Wrong device name provided: '%wZ'\n", pustrDeviceName);
            lRet = DISP_CHANGE_BADPARAM;
            goto cleanup;
        }
    }
    else if (RequestedMode)
    {
        pGraphicsDevice = EngpFindGraphicsDevice(NULL, 0);
        if (!pGraphicsDevice)
        {
            ERR("Wrong device'\n");
            lRet = DISP_CHANGE_BADPARAM;
            goto cleanup;
        }
    }

    if (pGraphicsDevice)
    {
        if (!LDEVOBJ_bProbeAndCaptureDevmode(pGraphicsDevice, RequestedMode, &pdm, bSearchClosestMode))
        {
            ERR("DrvProbeAndCaptureDevmode() failed\n");
            lRet = DISP_CHANGE_BADMODE;
            goto cleanup;
        }
    }

    /* Here, we know that input parameters were correct */

    {
        /* Create new MDEV. Note that if we provide a device name,
         * MDEV will only contain one device.
         * */

        if (pmdevOld)
        {
            /* Disable old MDEV */
            if (MDEVOBJ_bDisable(pmdevOld))
            {
                /* Create new MDEV. On failure, reenable old MDEV */
                pmdev = MDEVOBJ_Create(pustrDeviceName, pdm);
                if (!pmdev)
                    MDEVOBJ_vEnable(pmdevOld);
            }
        }
        else
        {
            pmdev = MDEVOBJ_Create(pustrDeviceName, pdm);
        }

        if (!pmdev)
        {
            ERR("Failed to create new MDEV\n");
            lRet = DISP_CHANGE_FAILED;
            goto cleanup;
        }

        lRet = DISP_CHANGE_SUCCESSFUL;
        *ppmdevNew = pmdev;

        /* We now have to do the mode switch */

        if (pustrDeviceName && pmdevOld)
        {
            /* We changed settings of one device. Add other devices which were already present */
            for (i = 0; i < pmdevOld->cDev; i++)
            {
                for (j = 0; j < pmdev->cDev; j++)
                {
                    if (pmdev->dev[j].ppdev->pGraphicsDevice == pmdevOld->dev[i].ppdev->pGraphicsDevice)
                    {
                        if (PDEVOBJ_bDynamicModeChange(pmdevOld->dev[i].ppdev, pmdev->dev[j].ppdev))
                        {
                            PPDEVOBJ tmp = pmdevOld->dev[i].ppdev;
                            pmdevOld->dev[i].ppdev = pmdev->dev[j].ppdev;
                            pmdev->dev[j].ppdev = tmp;
                        }
                        else
                        {
                            ERR("Failed to apply new settings\n");
                            UNIMPLEMENTED;
                            ASSERT(FALSE);
                        }
                        break;
                    }
                }
                if (j == pmdev->cDev)
                {
                    PDEVOBJ_vReference(pmdevOld->dev[i].ppdev);
                    pmdev->dev[pmdev->cDev].ppdev = pmdevOld->dev[i].ppdev;
                    pmdev->cDev++;
                }
            }
        }

        if (pmdev->cDev == 1)
        {
            pmdev->ppdevGlobal = pmdev->dev[0].ppdev;
        }
        else
        {
            /* Enable MultiDriver */
            pmdev->ppdevGlobal = PDEVOBJ_Create(NULL, (PDEVMODEW)pmdev, 0, LDEV_DEVICE_META);
            if (!pmdev->ppdevGlobal)
            {
                WARN("Failed to create meta-device. Using only first display\n");
                PDEVOBJ_vReference(pmdev->dev[0].ppdev);
                pmdev->ppdevGlobal = pmdev->dev[0].ppdev;
            }
        }

        if (pmdevOld)
        {
            /* Search PDEVs which were in pmdevOld, but are not anymore in pmdev, and disable them */
            for (i = 0; i < pmdevOld->cDev; i++)
            {
                for (j = 0; j < pmdev->cDev; j++)
                {
                    if (pmdev->dev[j].ppdev->pGraphicsDevice == pmdevOld->dev[i].ppdev->pGraphicsDevice)
                        break;
                }
                if (j == pmdev->cDev)
                    PDEVOBJ_bDisableDisplay(pmdevOld->dev[i].ppdev);
            }
        }
    }

cleanup:
    if (lRet != DISP_CHANGE_SUCCESSFUL)
    {
        *ppmdevNew = NULL;
        if (pmdev)
            MDEVOBJ_vDestroy(pmdev);
        if (pdm && pdm != RequestedMode)
            ExFreePoolWithTag(pdm, GDITAG_DEVMODE);
    }

    return lRet;
}

INT
NTAPI
PDEVOBJ_iGetColorManagementCaps(PPDEVOBJ ppdev)
{
    INT ret = CM_NONE;

    if (ppdev->flFlags & PDEV_DISPLAY)
    {
        if (ppdev->devinfo.iDitherFormat == BMF_8BPP ||
            ppdev->devinfo.flGraphicsCaps2 & GCAPS2_CHANGEGAMMARAMP)
            ret = CM_GAMMA_RAMP;
    }

    if (ppdev->devinfo.flGraphicsCaps & GCAPS_CMYKCOLOR)
        ret |= CM_CMYK_COLOR;
    if (ppdev->devinfo.flGraphicsCaps & GCAPS_ICM)
        ret |= CM_DEVICE_ICM;

    return ret;
}

VOID
NTAPI
PDEVOBJ_vGetDeviceCaps(
    IN PPDEVOBJ ppdev,
    OUT PDEVCAPS pDevCaps)
{
    PGDIINFO pGdiInfo = &ppdev->gdiinfo;

    pDevCaps->ulVersion = pGdiInfo->ulVersion;
    pDevCaps->ulTechnology = pGdiInfo->ulTechnology;
    pDevCaps->ulHorzSizeM = (pGdiInfo->ulHorzSize + 500) / 1000;
    pDevCaps->ulVertSizeM = (pGdiInfo->ulVertSize + 500) / 1000;
    pDevCaps->ulHorzSize = pGdiInfo->ulHorzSize;
    pDevCaps->ulVertSize = pGdiInfo->ulVertSize;
    pDevCaps->ulHorzRes = pGdiInfo->ulHorzRes;
    pDevCaps->ulVertRes = pGdiInfo->ulVertRes;
    pDevCaps->ulBitsPixel = pGdiInfo->cBitsPixel;
    if (pDevCaps->ulBitsPixel == 15) pDevCaps->ulBitsPixel = 16;
    pDevCaps->ulPlanes = pGdiInfo->cPlanes;
    pDevCaps->ulNumPens = pGdiInfo->ulNumColors;
    if (pDevCaps->ulNumPens != -1) pDevCaps->ulNumPens *= 5;
    pDevCaps->ulNumFonts = 0; // PDEVOBJ_cFonts(ppdev);
    pDevCaps->ulNumColors = pGdiInfo->ulNumColors;
    pDevCaps->ulRasterCaps = pGdiInfo->flRaster;
    pDevCaps->ulAspectX = pGdiInfo->ulAspectX;
    pDevCaps->ulAspectY = pGdiInfo->ulAspectY;
    pDevCaps->ulAspectXY = pGdiInfo->ulAspectXY;
    pDevCaps->ulLogPixelsX = pGdiInfo->ulLogPixelsX;
    pDevCaps->ulLogPixelsY = pGdiInfo->ulLogPixelsY;
    pDevCaps->ulSizePalette = pGdiInfo->ulNumPalReg;
    pDevCaps->ulColorRes = pGdiInfo->ulDACRed +
                           pGdiInfo->ulDACGreen +
                           pGdiInfo->ulDACBlue;
    pDevCaps->ulPhysicalWidth = pGdiInfo->szlPhysSize.cx;
    pDevCaps->ulPhysicalHeight = pGdiInfo->szlPhysSize.cy;
    pDevCaps->ulPhysicalOffsetX = pGdiInfo->ptlPhysOffset.x;
    pDevCaps->ulPhysicalOffsetY = pGdiInfo->ptlPhysOffset.y;
    pDevCaps->ulTextCaps = pGdiInfo->flTextCaps;
    pDevCaps->ulTextCaps |= (TC_SO_ABLE|TC_UA_ABLE|TC_CP_STROKE|TC_OP_STROKE|TC_OP_CHARACTER);
    if (pGdiInfo->ulTechnology != DT_PLOTTER)
        pDevCaps->ulTextCaps |= TC_VA_ABLE;
    pDevCaps->ulVRefresh = pGdiInfo->ulVRefresh;
    pDevCaps->ulDesktopHorzRes = pGdiInfo->ulHorzRes;
    pDevCaps->ulDesktopVertRes = pGdiInfo->ulVertRes;
    pDevCaps->ulBltAlignment = pGdiInfo->ulBltAlignment;
    pDevCaps->ulPanningHorzRes = pGdiInfo->ulPanningHorzRes;
    pDevCaps->ulPanningVertRes = pGdiInfo->ulPanningVertRes;
    pDevCaps->xPanningAlignment = pGdiInfo->xPanningAlignment;
    pDevCaps->yPanningAlignment = pGdiInfo->yPanningAlignment;
    pDevCaps->ulShadeBlend = pGdiInfo->flShadeBlend;
    pDevCaps->ulColorMgmtCaps = PDEVOBJ_iGetColorManagementCaps(ppdev);
}


/** Exported functions ********************************************************/

/*
 * @implemented
 */
BOOL
APIENTRY
EngQueryDeviceAttribute(
    _In_ HDEV hdev,
    _In_ ENG_DEVICE_ATTRIBUTE devAttr,
    _In_reads_bytes_(cjInSize) PVOID pvIn,
    _In_ ULONG cjInSize,
    _Out_writes_bytes_(cjOutSize) PVOID pvOut,
    _In_ ULONG cjOutSize)
{
    PPDEVOBJ ppdev = (PPDEVOBJ)hdev;

    if (devAttr != QDA_ACCELERATION_LEVEL)
        return FALSE;

    if (cjOutSize >= sizeof(DWORD))
    {
        /* Set all Accelerations Level Key to enabled Full 0 to 5 turned off. */
        *(DWORD*)pvOut = ppdev->dwAccelerationLevel;
        return TRUE;
    }

    return FALSE;
}

_Must_inspect_result_ _Ret_z_
LPWSTR
APIENTRY
EngGetDriverName(_In_ HDEV hdev)
{
    PPDEVOBJ ppdev = (PPDEVOBJ)hdev;

    ASSERT(ppdev);
    ASSERT(ppdev->pldev);
    ASSERT(ppdev->pldev->pGdiDriverInfo);
    ASSERT(ppdev->pldev->pGdiDriverInfo->DriverName.Buffer);

    return ppdev->pldev->pGdiDriverInfo->DriverName.Buffer;
}


static
VOID
PDEVOBJ_vVirtualizeDeviceCaps(
    _In_ PPDEVOBJ ppdev,
    _Inout_ PDEVCAPS pDevCaps)
{
    if (!(ppdev->flFlags & PDEV_DISPLAY) || !UserDpiIsVirtualCaller())
        return;

    pDevCaps->ulLogPixelsX = USER_SYSTEM_DPI_DEFAULT;
    pDevCaps->ulLogPixelsY = USER_SYSTEM_DPI_DEFAULT;
    pDevCaps->ulHorzRes = UserDpiToLogical(pDevCaps->ulHorzRes);
    pDevCaps->ulVertRes = UserDpiToLogical(pDevCaps->ulVertRes);
}

INT
APIENTRY
NtGdiGetDeviceCaps(
    HDC hdc,
    INT Index)
{
    PDC pdc;
    DEVCAPS devcaps;

    /* Lock the given DC */
    pdc = DC_LockDc(hdc);
    if (!pdc)
    {
        EngSetLastError(ERROR_INVALID_HANDLE);
        return 0;
    }

    /* Get the data */
    PDEVOBJ_vGetDeviceCaps(pdc->ppdev, &devcaps);
    PDEVOBJ_vVirtualizeDeviceCaps(pdc->ppdev, &devcaps);

    /* Unlock the DC */
    DC_UnlockDc(pdc);

    /* Return capability */
    switch (Index)
    {
        case DRIVERVERSION:
            return devcaps.ulVersion;

        case TECHNOLOGY:
            return devcaps.ulTechnology;

        case HORZSIZE:
            return devcaps.ulHorzSize;

        case VERTSIZE:
            return devcaps.ulVertSize;

        case HORZRES:
            return devcaps.ulHorzRes;

        case VERTRES:
            return devcaps.ulVertRes;

        case LOGPIXELSX:
            return devcaps.ulLogPixelsX;

        case LOGPIXELSY:
            return devcaps.ulLogPixelsY;

        case BITSPIXEL:
            return devcaps.ulBitsPixel;

        case PLANES:
            return devcaps.ulPlanes;

        case NUMBRUSHES:
            return -1;

        case NUMPENS:
            return devcaps.ulNumPens;

        case NUMFONTS:
            return devcaps.ulNumFonts;

        case NUMCOLORS:
            return devcaps.ulNumColors;

        case ASPECTX:
            return devcaps.ulAspectX;

        case ASPECTY:
            return devcaps.ulAspectY;

        case ASPECTXY:
            return devcaps.ulAspectXY;

        case CLIPCAPS:
            return CP_RECTANGLE;

        case SIZEPALETTE:
            return devcaps.ulSizePalette;

        case NUMRESERVED:
            return 20;

        case COLORRES:
            return devcaps.ulColorRes;

        case DESKTOPVERTRES:
            return devcaps.ulVertRes;

        case DESKTOPHORZRES:
            return devcaps.ulHorzRes;

        case BLTALIGNMENT:
            return devcaps.ulBltAlignment;

        case SHADEBLENDCAPS:
            return devcaps.ulShadeBlend;

        case COLORMGMTCAPS:
            return devcaps.ulColorMgmtCaps;

        case PHYSICALWIDTH:
            return devcaps.ulPhysicalWidth;

        case PHYSICALHEIGHT:
            return devcaps.ulPhysicalHeight;

        case PHYSICALOFFSETX:
            return devcaps.ulPhysicalOffsetX;

        case PHYSICALOFFSETY:
            return devcaps.ulPhysicalOffsetY;

        case VREFRESH:
            return devcaps.ulVRefresh;

        case RASTERCAPS:
            return devcaps.ulRasterCaps;

        case CURVECAPS:
            return (CC_CIRCLES | CC_PIE | CC_CHORD | CC_ELLIPSES | CC_WIDE |
                    CC_STYLED | CC_WIDESTYLED | CC_INTERIORS | CC_ROUNDRECT);

        case LINECAPS:
            return (LC_POLYLINE | LC_MARKER | LC_POLYMARKER | LC_WIDE |
                    LC_STYLED | LC_WIDESTYLED | LC_INTERIORS);

        case POLYGONALCAPS:
            return (PC_POLYGON | PC_RECTANGLE | PC_WINDPOLYGON | PC_SCANLINE |
                    PC_WIDE | PC_STYLED | PC_WIDESTYLED | PC_INTERIORS);

        case TEXTCAPS:
            return devcaps.ulTextCaps;

        case CAPS1:
        case PDEVICESIZE:
        case SCALINGFACTORX:
        case SCALINGFACTORY:
        default:
            return 0;
    }

    return 0;
}

_Success_(return!=FALSE)
BOOL
APIENTRY
NtGdiGetDeviceCapsAll(
    IN HDC hDC,
    OUT PDEVCAPS pDevCaps)
{
    PDC pdc;
    DEVCAPS devcaps;
    BOOL bResult = TRUE;

    /* Lock the given DC */
    pdc = DC_LockDc(hDC);
    if (!pdc)
    {
        EngSetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }

    /* Get the data */
    PDEVOBJ_vGetDeviceCaps(pdc->ppdev, &devcaps);
    PDEVOBJ_vVirtualizeDeviceCaps(pdc->ppdev, &devcaps);

    /* Unlock the DC */
    DC_UnlockDc(pdc);

    /* Copy data to caller */
    _SEH2_TRY
    {
        ProbeForWrite(pDevCaps, sizeof(DEVCAPS), 1);
        RtlCopyMemory(pDevCaps, &devcaps, sizeof(DEVCAPS));
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        SetLastNtError(_SEH2_GetExceptionCode());
        bResult = FALSE;
    }
    _SEH2_END;

    return bResult;
}

DHPDEV
APIENTRY
NtGdiGetDhpdev(
    IN HDEV hdev)
{
    PPDEVOBJ ppdev;
    DHPDEV dhpdev = NULL;

    /* Check parameter */
    if (!hdev || (PCHAR)hdev < (PCHAR)MmSystemRangeStart)
        return NULL;

    /* Lock PDEV list */
    EngAcquireSemaphoreShared(ghsemPDEV);

    /* Walk through the list of PDEVs */
    for (ppdev = gppdevList;  ppdev; ppdev = ppdev->ppdevNext)
    {
        /* Compare with the given HDEV */
        if (ppdev == (PPDEVOBJ)hdev)
        {
            /* Found the PDEV! Get it's dhpdev and break */
            dhpdev = ppdev->dhpdev;
            break;
        }
    }

    /* Unlock PDEV list */
    EngReleaseSemaphore(ghsemPDEV);

    return dhpdev;
}

PSIZEL
FASTCALL
PDEVOBJ_sizl(PPDEVOBJ ppdev, PSIZEL psizl)
{
    if (ppdev->flFlags & PDEV_META_DEVICE)
    {
        *psizl = ppdev->szlMetaRes;
    }
    else
    {
        psizl->cx = ppdev->gdiinfo.ulHorzRes;
        psizl->cy = ppdev->gdiinfo.ulVertRes;
    }
    return psizl;
}
