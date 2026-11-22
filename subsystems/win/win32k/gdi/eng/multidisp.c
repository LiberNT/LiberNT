/*
* PROJECT:     ReactOS Win32k subsystem
* LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
* FILE:        win32ss/gdi/eng/multidisp.c
* PURPOSE:     Multi-Display driver
* PROGRAMMERS:
* COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
*/

/* INCLUDES *******************************************************************/

#include <win32k.h>
#include <reactos/dwmframe.h>
#define NDEBUG
#include <debug.h>

#define TAG_MULTI 'luMG'
#define MULTI_STRIPE_ROWS 64
#define MULTI_HOOKS (HOOK_BITBLT | HOOK_COPYBITS | HOOK_LINETO | HOOK_PAINT | HOOK_STRETCHBLT | HOOK_STRETCHBLTROP | \
                     HOOK_ALPHABLEND | HOOK_TRANSPARENTBLT | HOOK_GRADIENTFILL | HOOK_SYNCHRONIZE)

typedef struct _MULTIDEV
{
    PMDEVOBJ pmdev;
    HDEV hdev;
    RECTL rclDesktop;
    ULONG iBitmapFormat;
    /* The desktop surface GDI draws on; its pixels live in the shadow bitmap */
    HSURF hsurf;
    HSURF hsurfShadow;
    SURFOBJ *psoShadow;
    LONG yStripe;
    /* The surface of each display, held while the desktop surface is enabled */
    PSURFACE *ppsurfDisplay;
} MULTIDEV, *PMULTIDEV;

static ULONG
MultiBitsPerPixel(
    _In_ ULONG iBitmapFormat)
{
    switch (iBitmapFormat)
    {
        case BMF_1BPP: return 1;
        case BMF_4BPP: return 4;
        case BMF_8BPP: return 8;
        case BMF_16BPP: return 16;
        case BMF_24BPP: return 24;
        case BMF_32BPP: return 32;
        default: return 0;
    }
}

VOID
MultiDisplayRect(
    _In_ PPDEVOBJ ppdev,
    _Out_ PRECTL prcl)
{
    prcl->left = 0;
    prcl->top = 0;
    if (ppdev->pdmwDev != NULL && (ppdev->pdmwDev->dmFields & DM_POSITION))
    {
        prcl->left = ppdev->pdmwDev->dmPosition.x;
        prcl->top = ppdev->pdmwDev->dmPosition.y;
    }
    prcl->right = prcl->left + (LONG)ppdev->gdiinfo.ulHorzRes;
    prcl->bottom = prcl->top + (LONG)ppdev->gdiinfo.ulVertRes;
}

PPDEVOBJ
MultiPrimaryDisplay(
    _In_ PMDEVOBJ pmdev)
{
    RECTL rcl;
    ULONG i;

    ASSERT(pmdev->cDev != 0);
    for (i = 0; i < pmdev->cDev; i++)
    {
        MultiDisplayRect(pmdev->dev[i].ppdev, &rcl);
        if (rcl.left == 0 && rcl.top == 0)
            return pmdev->dev[i].ppdev;
    }
    return pmdev->dev[0].ppdev;
}

DHPDEV
APIENTRY
MultiEnablePDEV(
    _In_ PDEVMODEW pdm,
    _In_ LPWSTR pwszLogAddress,
    _In_ ULONG cPat,
    _In_reads_opt_(cPat) HSURF *phsurfPatterns,
    _In_ ULONG cjCaps,
    _Out_writes_bytes_(cjCaps) PULONG pdevcaps,
    _In_ ULONG cjDevInfo,
    _Out_writes_bytes_(cjDevInfo) PDEVINFO pdi,
    _In_ HDEV hdev,
    _In_ LPWSTR pwszDeviceName,
    _In_ HANDLE hDriver)
{
    /* PDEVOBJ_Create passes the MDEV of the displays in place of a DEVMODE */
    PMDEVOBJ pmdev = (PMDEVOBJ)pdm;
    PPDEVOBJ ppdevPrimary;
    PMULTIDEV pmultidev;
    RECTL rcl;
    ULONG i;

    UNREFERENCED_PARAMETER(pwszLogAddress);
    UNREFERENCED_PARAMETER(cPat);
    UNREFERENCED_PARAMETER(phsurfPatterns);
    UNREFERENCED_PARAMETER(hdev);
    UNREFERENCED_PARAMETER(pwszDeviceName);
    UNREFERENCED_PARAMETER(hDriver);

    if (pmdev == NULL || pmdev->cDev < 2 || cjCaps < sizeof(GDIINFO) || cjDevInfo < sizeof(DEVINFO))
        return NULL;

    pmultidev = EngAllocMem(FL_ZERO_MEMORY, sizeof(MULTIDEV), TAG_MULTI);
    if (pmultidev == NULL)
        return NULL;
    pmultidev->ppsurfDisplay = EngAllocMem(FL_ZERO_MEMORY, pmdev->cDev * sizeof(PSURFACE), TAG_MULTI);
    if (pmultidev->ppsurfDisplay == NULL)
    {
        EngFreeMem(pmultidev);
        return NULL;
    }
    pmultidev->pmdev = pmdev;

    ppdevPrimary = MultiPrimaryDisplay(pmdev);
    MultiDisplayRect(ppdevPrimary, &pmultidev->rclDesktop);
    for (i = 0; i < pmdev->cDev; i++)
    {
        MultiDisplayRect(pmdev->dev[i].ppdev, &rcl);
        pmultidev->rclDesktop.left = min(pmultidev->rclDesktop.left, rcl.left);
        pmultidev->rclDesktop.top = min(pmultidev->rclDesktop.top, rcl.top);
        pmultidev->rclDesktop.right = max(pmultidev->rclDesktop.right, rcl.right);
        pmultidev->rclDesktop.bottom = max(pmultidev->rclDesktop.bottom, rcl.bottom);
    }
    /* FIXME: a display left of or above the primary display needs a desktop origin below zero */
    pmultidev->rclDesktop.left = 0;
    pmultidev->rclDesktop.top = 0;

    RtlCopyMemory(pdevcaps, &ppdevPrimary->gdiinfo, sizeof(GDIINFO));
    ((PGDIINFO)pdevcaps)->ulHorzRes = pmultidev->rclDesktop.right;
    ((PGDIINFO)pdevcaps)->ulVertRes = pmultidev->rclDesktop.bottom;
    RtlCopyMemory(pdi, &ppdevPrimary->devinfo, sizeof(DEVINFO));
    pdi->flGraphicsCaps &= ~(GCAPS_PANNING | GCAPS_ASYNCMOVE);
    pmultidev->iBitmapFormat = pdi->iDitherFormat;
    if (MultiBitsPerPixel(pmultidev->iBitmapFormat) == 0)
    {
        EngFreeMem(pmultidev->ppsurfDisplay);
        EngFreeMem(pmultidev);
        return NULL;
    }
    return (DHPDEV)pmultidev;
}

VOID
APIENTRY
MultiCompletePDEV(
    _In_ DHPDEV dhpdev,
    _In_ HDEV hdev)
{
    ((PMULTIDEV)dhpdev)->hdev = hdev;
}

VOID
APIENTRY
MultiDisablePDEV(
    _In_ DHPDEV dhpdev)
{
    PMULTIDEV pmultidev = (PMULTIDEV)dhpdev;
    ULONG i;

    for (i = 0; i < pmultidev->pmdev->cDev; i++)
        ASSERT(pmultidev->ppsurfDisplay[i] == NULL);
    EngFreeMem(pmultidev->ppsurfDisplay);
    EngFreeMem(pmultidev);
}

static VOID
MultiReleaseDisplaySurfaces(
    _Inout_ PMULTIDEV pmultidev)
{
    ULONG i;

    for (i = 0; i < pmultidev->pmdev->cDev; i++)
    {
        if (pmultidev->ppsurfDisplay[i] != NULL)
            SURFACE_ShareUnlockSurface(pmultidev->ppsurfDisplay[i]);
        pmultidev->ppsurfDisplay[i] = NULL;
    }
}

HSURF
APIENTRY
MultiEnableSurface(
    _In_ DHPDEV dhpdev)
{
    PMULTIDEV pmultidev = (PMULTIDEV)dhpdev;
    SIZEL sizl;
    LONG lDelta;
    HBITMAP hbm;
    HSURF hsurf;
    ULONG i;

    ASSERT(pmultidev->hsurf == NULL);
    ASSERT(pmultidev->psoShadow == NULL);
    /* No device context targets a display of the desktop, so the desktop surface enables theirs */
    for (i = 0; i < pmultidev->pmdev->cDev; i++)
    {
        ASSERT(pmultidev->ppsurfDisplay[i] == NULL);
        pmultidev->ppsurfDisplay[i] = PDEVOBJ_pSurface(pmultidev->pmdev->dev[i].ppdev);
        if (pmultidev->ppsurfDisplay[i] == NULL)
        {
            MultiReleaseDisplaySurfaces(pmultidev);
            return NULL;
        }
    }
    sizl.cx = pmultidev->rclDesktop.right - pmultidev->rclDesktop.left;
    sizl.cy = pmultidev->rclDesktop.bottom - pmultidev->rclDesktop.top;
    lDelta = (LONG)(((sizl.cx * MultiBitsPerPixel(pmultidev->iBitmapFormat) + 31) & ~31) >> 3);

    hbm = EngCreateBitmap(sizl, lDelta, pmultidev->iBitmapFormat, BMF_TOPDOWN, NULL);
    if (hbm == NULL)
    {
        MultiReleaseDisplaySurfaces(pmultidev);
        return NULL;
    }
    if (!EngAssociateSurface((HSURF)hbm, pmultidev->hdev, 0))
    {
        EngDeleteSurface((HSURF)hbm);
        MultiReleaseDisplaySurfaces(pmultidev);
        return NULL;
    }
    pmultidev->psoShadow = EngLockSurface((HSURF)hbm);
    if (pmultidev->psoShadow == NULL || pmultidev->psoShadow->pvScan0 == NULL)
    {
        if (pmultidev->psoShadow != NULL)
            EngUnlockSurface(pmultidev->psoShadow);
        pmultidev->psoShadow = NULL;
        EngDeleteSurface((HSURF)hbm);
        MultiReleaseDisplaySurfaces(pmultidev);
        return NULL;
    }
    pmultidev->hsurfShadow = (HSURF)hbm;

    /* Each drawing call reaches the hooks below, which copy what changed to the displays it covers */
    hsurf = EngCreateDeviceSurface((DHSURF)pmultidev, sizl, pmultidev->iBitmapFormat);
    if (hsurf != NULL &&
        !EngModifySurface(hsurf, pmultidev->hdev, MULTI_HOOKS, 0, (DHSURF)pmultidev,
                          pmultidev->psoShadow->pvScan0, pmultidev->psoShadow->lDelta, NULL))
    {
        EngDeleteSurface(hsurf);
        hsurf = NULL;
    }
    if (hsurf == NULL)
    {
        EngUnlockSurface(pmultidev->psoShadow);
        pmultidev->psoShadow = NULL;
        EngDeleteSurface(pmultidev->hsurfShadow);
        pmultidev->hsurfShadow = NULL;
        MultiReleaseDisplaySurfaces(pmultidev);
        return NULL;
    }
    pmultidev->hsurf = hsurf;
    return pmultidev->hsurf;
}

VOID
APIENTRY
MultiDisableSurface(
    _In_ DHPDEV dhpdev)
{
    PMULTIDEV pmultidev = (PMULTIDEV)dhpdev;

    if (pmultidev->hsurf != NULL)
    {
        EngDeleteSurface(pmultidev->hsurf);
        pmultidev->hsurf = NULL;
    }
    if (pmultidev->psoShadow != NULL)
    {
        EngUnlockSurface(pmultidev->psoShadow);
        pmultidev->psoShadow = NULL;
    }
    if (pmultidev->hsurfShadow != NULL)
    {
        EngDeleteSurface(pmultidev->hsurfShadow);
        pmultidev->hsurfShadow = NULL;
    }
    MultiReleaseDisplaySurfaces(pmultidev);
}

static SURFOBJ *
MultiShadow(
    _In_opt_ SURFOBJ *pso)
{
    PMULTIDEV pmultidev;

    if (pso == NULL || pso->dhpdev == NULL || pso->dhsurf != (DHSURF)pso->dhpdev)
        return pso;
    pmultidev = (PMULTIDEV)pso->dhpdev;
    ASSERT(pmultidev->psoShadow != NULL);
    return pmultidev->psoShadow;
}

/* Copies a rectangle of the desktop to every display it covers, through that display's own driver */
static VOID
MultiFlush(
    _In_ PMULTIDEV pmultidev,
    _In_ const RECTL *prcl)
{
    PPDEVOBJ ppdev;
    PSURFACE psurf;
    RECTL rclDisplay;
    RECTL rclCopy;
    RECTL rclDest;
    POINTL ptlSrc;
    ULONG i;

    ASSERT(pmultidev->psoShadow != NULL);
    for (i = 0; i < pmultidev->pmdev->cDev; i++)
    {
        ppdev = pmultidev->pmdev->dev[i].ppdev;
        MultiDisplayRect(ppdev, &rclDisplay);
        if (!RECTL_bIntersectRect(&rclCopy, prcl, &rclDisplay))
            continue;
        rclDest.left = rclCopy.left - rclDisplay.left;
        rclDest.top = rclCopy.top - rclDisplay.top;
        rclDest.right = rclCopy.right - rclDisplay.left;
        rclDest.bottom = rclCopy.bottom - rclDisplay.top;
        ptlSrc.x = rclCopy.left - pmultidev->rclDesktop.left;
        ptlSrc.y = rclCopy.top - pmultidev->rclDesktop.top;

        EngAcquireSemaphore(ppdev->hsemDevLock);
        psurf = pmultidev->ppsurfDisplay[i];
        /* FIXME: a display whose format differs from the desktop's gets no translation */
        if (psurf != NULL && psurf->SurfObj.iBitmapFormat == pmultidev->psoShadow->iBitmapFormat)
        {
            if ((psurf->flags & HOOK_COPYBITS) && ppdev->DriverFunctions.CopyBits != NULL)
                ppdev->DriverFunctions.CopyBits(&psurf->SurfObj, pmultidev->psoShadow, NULL, NULL, &rclDest, &ptlSrc);
            else
                EngCopyBits(&psurf->SurfObj, pmultidev->psoShadow, NULL, NULL, &rclDest, &ptlSrc);
        }
        EngReleaseSemaphore(ppdev->hsemDevLock);
    }
}

static VOID
MultiPublish(
    _In_ SURFOBJ *pso,
    _In_ const RECTL *prcl,
    _In_opt_ CLIPOBJ *pco)
{
    PMULTIDEV pmultidev;
    RECTL rcl;

    if (pso == NULL || prcl == NULL || pso->dhpdev == NULL || pso->dhsurf != (DHSURF)pso->dhpdev)
        return;
    pmultidev = (PMULTIDEV)pso->dhpdev;
    rcl.left = min(prcl->left, prcl->right);
    rcl.right = max(prcl->left, prcl->right);
    rcl.top = min(prcl->top, prcl->bottom);
    rcl.bottom = max(prcl->top, prcl->bottom);
    if (pco != NULL && pco->iDComplexity != DC_TRIVIAL && !RECTL_bIntersectRect(&rcl, &rcl, &pco->rclBounds))
        return;
    MultiFlush(pmultidev, &rcl);
}

BOOL
APIENTRY
MultiBitBlt(
    _Inout_ SURFOBJ *psoTrg,
    _In_opt_ SURFOBJ *psoSrc,
    _In_opt_ SURFOBJ *psoMask,
    _In_ CLIPOBJ *pco,
    _In_opt_ XLATEOBJ *pxlo,
    _In_ RECTL *prclTrg,
    _In_opt_ POINTL *pptlSrc,
    _In_opt_ POINTL *pptlMask,
    _In_opt_ BRUSHOBJ *pbo,
    _In_opt_ POINTL *pptlBrush,
    _In_ ROP4 rop4)
{
    if (!EngBitBlt(MultiShadow(psoTrg), MultiShadow(psoSrc), MultiShadow(psoMask), pco, pxlo, prclTrg, pptlSrc,
                   pptlMask, pbo, pptlBrush, rop4))
    {
        return FALSE;
    }
    MultiPublish(psoTrg, prclTrg, pco);
    return TRUE;
}

BOOL
APIENTRY
MultiCopyBits(
    _Inout_ SURFOBJ *psoDest,
    _In_ SURFOBJ *psoSrc,
    _In_opt_ CLIPOBJ *pco,
    _In_opt_ XLATEOBJ *pxlo,
    _In_ RECTL *prclDest,
    _In_ POINTL *pptlSrc)
{
    if (!EngCopyBits(MultiShadow(psoDest), MultiShadow(psoSrc), pco, pxlo, prclDest, pptlSrc))
        return FALSE;
    MultiPublish(psoDest, prclDest, pco);
    return TRUE;
}

BOOL
APIENTRY
MultiLineTo(
    _Inout_ SURFOBJ *pso,
    _In_ CLIPOBJ *pco,
    _In_ BRUSHOBJ *pbo,
    _In_ LONG x1,
    _In_ LONG y1,
    _In_ LONG x2,
    _In_ LONG y2,
    _In_opt_ RECTL *prclBounds,
    _In_ MIX mix)
{
    RECTL rcl;

    if (!EngLineTo(MultiShadow(pso), pco, pbo, x1, y1, x2, y2, prclBounds, mix))
        return FALSE;
    if (prclBounds != NULL)
        rcl = *prclBounds;
    else
    {
        rcl.left = min(x1, x2);
        rcl.top = min(y1, y2);
        rcl.right = max(x1, x2) + 1;
        rcl.bottom = max(y1, y2) + 1;
    }
    MultiPublish(pso, &rcl, pco);
    return TRUE;
}

BOOL
APIENTRY
MultiPaint(
    _Inout_ SURFOBJ *pso,
    _In_ CLIPOBJ *pco,
    _In_ BRUSHOBJ *pbo,
    _In_ POINTL *pptlBrushOrg,
    _In_ MIX mix)
{
    RECTL rcl;

    if (!EngPaint(MultiShadow(pso), pco, pbo, pptlBrushOrg, mix))
        return FALSE;
    if (pco != NULL)
        rcl = pco->rclBounds;
    else
    {
        rcl.left = 0;
        rcl.top = 0;
        rcl.right = pso->sizlBitmap.cx;
        rcl.bottom = pso->sizlBitmap.cy;
    }
    MultiPublish(pso, &rcl, NULL);
    return TRUE;
}

BOOL
APIENTRY
MultiStretchBlt(
    _Inout_ SURFOBJ *psoDest,
    _Inout_ SURFOBJ *psoSrc,
    _In_opt_ SURFOBJ *psoMask,
    _In_opt_ CLIPOBJ *pco,
    _In_opt_ XLATEOBJ *pxlo,
    _In_opt_ COLORADJUSTMENT *pca,
    _In_ POINTL *pptlHTOrg,
    _In_ RECTL *prclDest,
    _In_ RECTL *prclSrc,
    _In_opt_ POINTL *pptlMask,
    _In_ ULONG iMode)
{
    if (!EngStretchBlt(MultiShadow(psoDest), MultiShadow(psoSrc), MultiShadow(psoMask), pco, pxlo, pca, pptlHTOrg,
                       prclDest, prclSrc, pptlMask, iMode))
    {
        return FALSE;
    }
    MultiPublish(psoDest, prclDest, pco);
    return TRUE;
}

BOOL
APIENTRY
MultiStretchBltROP(
    _Inout_ SURFOBJ *psoDest,
    _Inout_ SURFOBJ *psoSrc,
    _In_opt_ SURFOBJ *psoMask,
    _In_opt_ CLIPOBJ *pco,
    _In_opt_ XLATEOBJ *pxlo,
    _In_opt_ COLORADJUSTMENT *pca,
    _In_ POINTL *pptlHTOrg,
    _In_ RECTL *prclDest,
    _In_ RECTL *prclSrc,
    _In_opt_ POINTL *pptlMask,
    _In_ ULONG iMode,
    _In_ BRUSHOBJ *pbo,
    _In_ DWORD rop4)
{
    if (!EngStretchBltROP(MultiShadow(psoDest), MultiShadow(psoSrc), MultiShadow(psoMask), pco, pxlo, pca, pptlHTOrg,
                          prclDest, prclSrc, pptlMask, iMode, pbo, rop4))
    {
        return FALSE;
    }
    MultiPublish(psoDest, prclDest, pco);
    return TRUE;
}

BOOL
APIENTRY
MultiAlphaBlend(
    _Inout_ SURFOBJ *psoDest,
    _In_ SURFOBJ *psoSrc,
    _In_ CLIPOBJ *pco,
    _In_opt_ XLATEOBJ *pxlo,
    _In_ RECTL *prclDest,
    _In_ RECTL *prclSrc,
    _In_ BLENDOBJ *pBlendObj)
{
    if (!EngAlphaBlend(MultiShadow(psoDest), MultiShadow(psoSrc), pco, pxlo, prclDest, prclSrc, pBlendObj))
        return FALSE;
    MultiPublish(psoDest, prclDest, pco);
    return TRUE;
}

BOOL
APIENTRY
MultiTransparentBlt(
    _Inout_ SURFOBJ *psoDst,
    _In_ SURFOBJ *psoSrc,
    _In_ CLIPOBJ *pco,
    _In_opt_ XLATEOBJ *pxlo,
    _In_ RECTL *prclDst,
    _In_ RECTL *prclSrc,
    _In_ ULONG iTransColor,
    _In_ ULONG ulReserved)
{
    if (!EngTransparentBlt(MultiShadow(psoDst), MultiShadow(psoSrc), pco, pxlo, prclDst, prclSrc, iTransColor,
                           ulReserved))
    {
        return FALSE;
    }
    MultiPublish(psoDst, prclDst, pco);
    return TRUE;
}

BOOL
APIENTRY
MultiGradientFill(
    _Inout_ SURFOBJ *psoDest,
    _In_ CLIPOBJ *pco,
    _In_opt_ XLATEOBJ *pxlo,
    _In_ TRIVERTEX *pVertex,
    _In_ ULONG nVertex,
    _In_ PVOID pMesh,
    _In_ ULONG nMesh,
    _In_ RECTL *prclExtents,
    _In_ POINTL *pptlDitherOrg,
    _In_ ULONG ulMode)
{
    if (!EngGradientFill(MultiShadow(psoDest), pco, pxlo, pVertex, nVertex, pMesh, nMesh, prclExtents,
                         pptlDitherOrg, ulMode))
    {
        return FALSE;
    }
    MultiPublish(psoDest, prclExtents, pco);
    return TRUE;
}

/* Each display's driver presents what it was given when it is synchronized, which the engine does for the desktop alone */
static VOID
MultiForwardSynchronize(
    _In_ PMULTIDEV pmultidev,
    _In_opt_ const RECTL *prcl,
    _In_ FLONG fl)
{
    PPDEVOBJ ppdev;
    PSURFACE psurf;
    RECTL rclDisplay;
    RECTL rclLocal;
    ULONG i;

    for (i = 0; i < pmultidev->pmdev->cDev; i++)
    {
        ppdev = pmultidev->pmdev->dev[i].ppdev;
        psurf = pmultidev->ppsurfDisplay[i];
        if (psurf == NULL || !(psurf->flags & HOOK_SYNCHRONIZE) || ppdev->DriverFunctions.SynchronizeSurface == NULL)
            continue;
        if (prcl != NULL)
        {
            MultiDisplayRect(ppdev, &rclDisplay);
            if (!RECTL_bIntersectRect(&rclLocal, prcl, &rclDisplay))
                continue;
            RECTL_vOffsetRect(&rclLocal, -rclDisplay.left, -rclDisplay.top);
        }
        /* The timer runs on the raw input thread, which must not wait behind a present */
        if (fl & DSS_TIMER_EVENT)
        {
            if (!EngAcquireSemaphoreNoWait(ppdev->hsemDevLock))
                continue;
        }
        else
            EngAcquireSemaphore(ppdev->hsemDevLock);
        ppdev->DriverFunctions.SynchronizeSurface(&psurf->SurfObj, prcl != NULL ? &rclLocal : NULL, fl);
        EngReleaseSemaphore(ppdev->hsemDevLock);
    }
}

VOID
APIENTRY
MultiSynchronizeSurface(
    _In_ SURFOBJ *pso,
    _In_opt_ RECTL *prcl,
    _In_ FLONG fl)
{
    PMULTIDEV pmultidev;
    RECTL rcl;

    if (pso == NULL || pso->dhpdev == NULL || pso->dhsurf != (DHSURF)pso->dhpdev)
        return;
    pmultidev = (PMULTIDEV)pso->dhpdev;
    if (fl & DSS_TIMER_EVENT)
    {
        /* Writes that reach the desktop's pixels without a drawing call reach the displays a stripe per tick */
        if (pmultidev->yStripe >= pso->sizlBitmap.cy)
            pmultidev->yStripe = 0;
        rcl.left = 0;
        rcl.top = pmultidev->yStripe;
        rcl.right = pso->sizlBitmap.cx;
        rcl.bottom = min(pmultidev->yStripe + MULTI_STRIPE_ROWS, pso->sizlBitmap.cy);
        pmultidev->yStripe = rcl.bottom;
        MultiFlush(pmultidev, &rcl);
        MultiForwardSynchronize(pmultidev, NULL, fl);
        return;
    }
    if ((fl & DSS_FLUSH_EVENT) && prcl != NULL)
        MultiPublish(pso, prcl, NULL);
    MultiForwardSynchronize(pmultidev, prcl, fl);
}

BOOL
APIENTRY
MultiAssertMode(
    _In_ DHPDEV dhpdev,
    _In_ BOOL bEnable)
{
    UNREFERENCED_PARAMETER(dhpdev);
    UNREFERENCED_PARAMETER(bEnable);
    return TRUE;
}

ULONG
APIENTRY
MultiEscape(
    _In_ SURFOBJ *pso,
    _In_ ULONG iEsc,
    _In_ ULONG cjIn,
    _In_reads_bytes_opt_(cjIn) PVOID pvIn,
    _In_ ULONG cjOut,
    _Out_writes_bytes_opt_(cjOut) PVOID pvOut)
{
    PMULTIDEV pmultidev = (PMULTIDEV)pso->dhpdev;
    PPDEVOBJ ppdevPrimary;
    PSURFACE psurf;
    ULONG Result = 0;
    RECTL rcl;

    if (iEsc == CDD_ESCAPE_MIRROR_NEEDED)
    {
        if (pvOut == NULL || cjOut < sizeof(LONG))
            return 0;
        *(PLONG)pvOut = 1;
        return 1;
    }

    if (iEsc == DWM_ESCAPE_MIRROR_FRAME)
    {
        const DWM_MIRROR_FRAME *pFrame = pvIn;
        ULONG cx, cy, y;

        if (pFrame == NULL || cjIn < sizeof(*pFrame) || pso->iBitmapFormat != BMF_32BPP || pso->pvScan0 == NULL ||
            pFrame->Pitch < pFrame->Width * sizeof(ULONG) ||
            (ULONGLONG)pFrame->Pitch * pFrame->Height > cjIn - sizeof(*pFrame))
        {
            return 0;
        }
        cx = min(pFrame->Width, (ULONG)pso->sizlBitmap.cx);
        cy = min(pFrame->Height, (ULONG)pso->sizlBitmap.cy);
        for (y = 0; y < cy; y++)
        {
            RtlCopyMemory((PBYTE)pso->pvScan0 + (LONG_PTR)y * pso->lDelta,
                          (const BYTE *)(pFrame + 1) + (SIZE_T)y * pFrame->Pitch,
                          cx * sizeof(ULONG));
        }
        rcl.left = 0;
        rcl.top = 0;
        rcl.right = (LONG)cx;
        rcl.bottom = (LONG)cy;
        MultiPublish(pso, &rcl, NULL);
        return 1;
    }

    /* The compositor's other channels belong to the primary display's driver */
    ppdevPrimary = MultiPrimaryDisplay(pmultidev->pmdev);
    if (ppdevPrimary->DriverFunctions.Escape == NULL)
        return 0;
    EngAcquireSemaphore(ppdevPrimary->hsemDevLock);
    psurf = ppdevPrimary->pSurface;
    if (psurf != NULL)
        Result = ppdevPrimary->DriverFunctions.Escape(&psurf->SurfObj, iEsc, cjIn, pvIn, cjOut, pvOut);
    EngReleaseSemaphore(ppdevPrimary->hsemDevLock);
    return Result;
}

static DRVFN gMultiDrvFn[] =
{
    { INDEX_DrvEnablePDEV,     (PFN) MultiEnablePDEV     },
    { INDEX_DrvCompletePDEV,   (PFN) MultiCompletePDEV   },
    { INDEX_DrvDisablePDEV,    (PFN) MultiDisablePDEV    },
    { INDEX_DrvEnableSurface,  (PFN) MultiEnableSurface  },
    { INDEX_DrvDisableSurface, (PFN) MultiDisableSurface },
    { INDEX_DrvAssertMode,     (PFN) MultiAssertMode     },
    { INDEX_DrvEscape,         (PFN) MultiEscape         },
    { INDEX_DrvBitBlt,         (PFN) MultiBitBlt         },
    { INDEX_DrvCopyBits,       (PFN) MultiCopyBits       },
    { INDEX_DrvLineTo,         (PFN) MultiLineTo         },
    { INDEX_DrvPaint,          (PFN) MultiPaint          },
    { INDEX_DrvStretchBlt,     (PFN) MultiStretchBlt     },
    { INDEX_DrvStretchBltROP,  (PFN) MultiStretchBltROP  },
    { INDEX_DrvAlphaBlend,     (PFN) MultiAlphaBlend     },
    { INDEX_DrvTransparentBlt, (PFN) MultiTransparentBlt },
    { INDEX_DrvGradientFill,   (PFN) MultiGradientFill   },
    { INDEX_DrvSynchronizeSurface, (PFN) MultiSynchronizeSurface },
};

BOOL
APIENTRY
MultiEnableDriver(
    _In_ ULONG iEngineVersion,
    _In_ ULONG cj,
    _Inout_bytecount_(cj) PDRVENABLEDATA pded)
{
    UNREFERENCED_PARAMETER(iEngineVersion);

    if (cj < sizeof(DRVENABLEDATA))
        return FALSE;
    pded->iDriverVersion = DDI_DRIVER_VERSION_NT5_01;
    pded->c = RTL_NUMBER_OF(gMultiDrvFn);
    pded->pdrvfn = gMultiDrvFn;
    return TRUE;
}
