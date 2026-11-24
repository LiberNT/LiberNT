/*
 * COPYRIGHT:         See COPYING in the top level directory
 * PROJECT:           ReactOS kernel
 * PURPOSE:           Functions for creation and destruction of DCs
 * FILE:              win32ss/gdi/ntgdi/device.c
 * PROGRAMER:         Timo Kreuzer (timo.kreuzer@rectos.org)
 */

#include <win32k.h>

#define NDEBUG
#include <debug.h>

PDC defaultDCstate = NULL;
static PSURFACE gpsurfPrimary;

BOOL FASTCALL
IntCreatePrimarySurface(VOID)
{
    /* Create surface */
    if (gpsurfPrimary)
        SURFACE_ShareUnlockSurface(gpsurfPrimary);
    gpsurfPrimary = PDEVOBJ_pSurface(gpmdev->ppdevGlobal);

    DPRINT("IntCreatePrimarySurface, ppdevGlobal=%p, ppdevGlobal->pSurface = %p\n",
        gpmdev->ppdevGlobal, gpmdev->ppdevGlobal->pSurface);

    // Init Primary Displays Device Capabilities.
    PDEVOBJ_vGetDeviceCaps(gpmdev->ppdevGlobal, &GdiHandleTable->DevCaps);

    return TRUE;
}

VOID FASTCALL
IntDestroyPrimarySurface(VOID)
{
    /* Give back the reference IntCreatePrimarySurface took, so that the
     * device can let its driver free the surface once nothing draws on it */
    if (gpsurfPrimary != NULL)
        SURFACE_ShareUnlockSurface(gpsurfPrimary);
    gpsurfPrimary = NULL;
}

PPDEVOBJ FASTCALL
IntEnumHDev(VOID)
{
// I guess we will soon have more than one primary surface.
// This will do for now.
    return gpmdev->ppdevGlobal;
}


INT
APIENTRY
NtGdiDrawEscape(
    IN HDC hdc,
    IN INT iEsc,
    IN INT cjIn,
    IN OPTIONAL LPSTR pjIn)
{
    UNIMPLEMENTED;
    return 0;
}


