/*
 * PROJECT:     LiberNT Disconnected Display Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Memory-backed display for sessions that do not own the console display
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#include <ntddk.h>
#include <stdarg.h>
#include <windef.h>
#include <wingdi.h>
#include <winddi.h>

#define DISCDD_TAG 'ddsD'
/* TODO: INFO_NEEDED the mode the disconnected display of a non-console session reports on Windows 11; 1024x768x32 is assumed */
#define DISCDD_WIDTH 1024
#define DISCDD_HEIGHT 768
#define DISCDD_BITS_PER_PEL 32
#define DISCDD_REFRESH_HZ 60
#define DISCDD_DEFAULT_DPI 96

typedef struct _DISCDD_PDEV
{
    HDEV Hdev;
    HPALETTE Palette;
    HSURF Surface;
    ULONG Width;
    ULONG Height;
} DISCDD_PDEV, *PDISCDD_PDEV;

C_ASSERT(DISCDD_BITS_PER_PEL == 32);
C_ASSERT(((ULONGLONG)DISCDD_WIDTH * (DISCDD_BITS_PER_PEL / 8)) <= MAXLONG);

static
VOID
DiscddFillMode(
    _Out_ PDEVMODEW Mode)
{
    RtlZeroMemory(Mode, sizeof(*Mode));
    Mode->dmSpecVersion = DM_SPECVERSION;
    Mode->dmDriverVersion = DM_SPECVERSION;
    Mode->dmSize = sizeof(*Mode);
    Mode->dmFields = DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT | DM_DISPLAYFREQUENCY | DM_DISPLAYFLAGS;
    Mode->dmBitsPerPel = DISCDD_BITS_PER_PEL;
    Mode->dmPelsWidth = DISCDD_WIDTH;
    Mode->dmPelsHeight = DISCDD_HEIGHT;
    Mode->dmDisplayFrequency = DISCDD_REFRESH_HZ;
}

static
BOOLEAN
DiscddIsListedMode(
    _In_ const DEVMODEW *Mode)
{
    if ((Mode->dmFields & DM_PELSWIDTH) && (Mode->dmPelsWidth != 0) && (Mode->dmPelsWidth != DISCDD_WIDTH))
        return FALSE;
    if ((Mode->dmFields & DM_PELSHEIGHT) && (Mode->dmPelsHeight != 0) && (Mode->dmPelsHeight != DISCDD_HEIGHT))
        return FALSE;
    if ((Mode->dmFields & DM_BITSPERPEL) && (Mode->dmBitsPerPel != 0) && (Mode->dmBitsPerPel != DISCDD_BITS_PER_PEL))
        return FALSE;
    return TRUE;
}

ULONG
APIENTRY
DrvGetModes(
    _In_ HANDLE hDriver,
    _In_ ULONG cjSize,
    _Out_writes_bytes_opt_(cjSize) DEVMODEW *pdm)
{
    UNREFERENCED_PARAMETER(hDriver);

    if (pdm == NULL)
        return sizeof(DEVMODEW);
    if (cjSize < sizeof(DEVMODEW))
        return 0;

    DiscddFillMode(pdm);
    ASSERT(pdm->dmSize == sizeof(DEVMODEW));
    return sizeof(DEVMODEW);
}

static
VOID
DiscddFillGdiInfo(
    _In_ const DEVMODEW *Mode,
    _In_ ULONG Width,
    _In_ ULONG Height,
    _Out_ GDIINFO *GdiInfo)
{
    ULONG Dpi = ((Mode->dmFields & DM_LOGPIXELS) && (Mode->dmLogPixels != 0)) ? Mode->dmLogPixels : DISCDD_DEFAULT_DPI;

    RtlZeroMemory(GdiInfo, sizeof(*GdiInfo));
    GdiInfo->ulVersion = GDI_DRIVER_VERSION;
    GdiInfo->ulTechnology = DT_RASDISPLAY;
    GdiInfo->ulHorzSize = (Width * 254 + Dpi * 5) / (Dpi * 10);
    GdiInfo->ulVertSize = (Height * 254 + Dpi * 5) / (Dpi * 10);
    GdiInfo->ulHorzRes = Width;
    GdiInfo->ulVertRes = Height;
    GdiInfo->cBitsPixel = DISCDD_BITS_PER_PEL;
    GdiInfo->cPlanes = 1;
    GdiInfo->ulNumColors = (ULONG)-1;
    GdiInfo->ulLogPixelsX = Dpi;
    GdiInfo->ulLogPixelsY = Dpi;
    GdiInfo->flTextCaps = TC_RA_ABLE;
    GdiInfo->ulDACRed = 8;
    GdiInfo->ulDACGreen = 8;
    GdiInfo->ulDACBlue = 8;
    GdiInfo->ulAspectX = 0x24;
    GdiInfo->ulAspectY = 0x24;
    GdiInfo->ulAspectXY = 0x33;
    GdiInfo->xStyleStep = 1;
    GdiInfo->yStyleStep = 1;
    GdiInfo->denStyleStep = 3;
    GdiInfo->szlPhysSize.cx = (LONG)Width;
    GdiInfo->szlPhysSize.cy = (LONG)Height;
    /* sRGB primaries and D65 white (IEC 61966-2-1), in units of 1/10000 */
    GdiInfo->ciDevice.Red.x = 6400;
    GdiInfo->ciDevice.Red.y = 3300;
    GdiInfo->ciDevice.Green.x = 3000;
    GdiInfo->ciDevice.Green.y = 6000;
    GdiInfo->ciDevice.Blue.x = 1500;
    GdiInfo->ciDevice.Blue.y = 600;
    GdiInfo->ciDevice.AlignmentWhite.x = 3127;
    GdiInfo->ciDevice.AlignmentWhite.y = 3290;
    GdiInfo->ciDevice.AlignmentWhite.Y = 10000;
    GdiInfo->ciDevice.RedGamma = 22000;
    GdiInfo->ciDevice.GreenGamma = 22000;
    GdiInfo->ciDevice.BlueGamma = 22000;
    GdiInfo->ulPrimaryOrder = PRIMARY_ORDER_CBA;
    GdiInfo->ulHTPatternSize = HT_PATSIZE_4x4_M;
    GdiInfo->ulHTOutputFormat = HT_FORMAT_32BPP;
    GdiInfo->flHTFlags = HT_FLAG_ADDITIVE_PRIMS;
    GdiInfo->ulVRefresh = DISCDD_REFRESH_HZ;
    GdiInfo->ulPanningHorzRes = Width;
    GdiInfo->ulPanningVertRes = Height;
    GdiInfo->ulPhysicalPixelCharacteristics = PPC_DEFAULT;
    GdiInfo->ulPhysicalPixelGamma = PPG_DEFAULT;
}

DHPDEV
APIENTRY
DrvEnablePDEV(
    _In_ DEVMODEW *pdm,
    _In_ LPWSTR pwszLogAddress,
    _In_ ULONG cPat,
    _Out_opt_ HSURF *phsurfPatterns,
    _In_ ULONG cjCaps,
    _Out_writes_bytes_(cjCaps) ULONG *pdevcaps,
    _In_ ULONG cjDevInfo,
    _Out_writes_bytes_(cjDevInfo) DEVINFO *pdi,
    _In_ HDEV hdev,
    _In_ LPWSTR pwszDeviceName,
    _In_ HANDLE hDriver)
{
    PDISCDD_PDEV Pdev;

    UNREFERENCED_PARAMETER(pwszLogAddress);
    UNREFERENCED_PARAMETER(cPat);
    UNREFERENCED_PARAMETER(phsurfPatterns);
    UNREFERENCED_PARAMETER(hdev);
    UNREFERENCED_PARAMETER(pwszDeviceName);
    UNREFERENCED_PARAMETER(hDriver);

    if ((pdm == NULL) || (pdevcaps == NULL) || (pdi == NULL) ||
        (cjCaps < sizeof(GDIINFO)) || (cjDevInfo < sizeof(DEVINFO)) || !DiscddIsListedMode(pdm))
    {
        return NULL;
    }

    Pdev = EngAllocMem(FL_ZERO_MEMORY, sizeof(*Pdev), DISCDD_TAG);
    if (Pdev == NULL)
        return NULL;

    Pdev->Width = DISCDD_WIDTH;
    Pdev->Height = DISCDD_HEIGHT;
    Pdev->Palette = EngCreatePalette(PAL_BITFIELDS, 0, NULL, 0x00FF0000, 0x0000FF00, 0x000000FF);
    if (Pdev->Palette == NULL)
    {
        EngFreeMem(Pdev);
        return NULL;
    }

    DiscddFillGdiInfo(pdm, Pdev->Width, Pdev->Height, (GDIINFO *)pdevcaps);

    RtlZeroMemory(pdi, sizeof(*pdi));
    pdi->iDitherFormat = BMF_32BPP;
    pdi->hpalDefault = Pdev->Palette;

    ASSERT(Pdev->Surface == NULL);
    return (DHPDEV)Pdev;
}

VOID
APIENTRY
DrvCompletePDEV(
    _In_ DHPDEV dhpdev,
    _In_ HDEV hdev)
{
    PDISCDD_PDEV Pdev = (PDISCDD_PDEV)dhpdev;

    ASSERT(Pdev != NULL);
    ASSERT(hdev != NULL);
    ASSERT(Pdev->Hdev == NULL);
    Pdev->Hdev = hdev;
}

HSURF
APIENTRY
DrvEnableSurface(
    _In_ DHPDEV dhpdev)
{
    PDISCDD_PDEV Pdev = (PDISCDD_PDEV)dhpdev;
    SIZEL Size;
    HBITMAP Bitmap;

    ASSERT(Pdev != NULL);
    ASSERT(Pdev->Hdev != NULL);
    ASSERT(Pdev->Surface == NULL);

    Size.cx = (LONG)Pdev->Width;
    Size.cy = (LONG)Pdev->Height;
    Bitmap = EngCreateBitmap(Size, (LONG)(Pdev->Width * (DISCDD_BITS_PER_PEL / 8)), BMF_32BPP, BMF_TOPDOWN, NULL);
    if (Bitmap == NULL)
        return NULL;

    if (!EngAssociateSurface((HSURF)Bitmap, Pdev->Hdev, 0))
    {
        EngDeleteSurface((HSURF)Bitmap);
        return NULL;
    }

    Pdev->Surface = (HSURF)Bitmap;
    return Pdev->Surface;
}

VOID
APIENTRY
DrvDisableSurface(
    _In_ DHPDEV dhpdev)
{
    PDISCDD_PDEV Pdev = (PDISCDD_PDEV)dhpdev;

    ASSERT(Pdev != NULL);
    if (Pdev->Surface == NULL)
        return;

    EngDeleteSurface(Pdev->Surface);
    Pdev->Surface = NULL;
}

VOID
APIENTRY
DrvDisablePDEV(
    _In_ DHPDEV dhpdev)
{
    PDISCDD_PDEV Pdev = (PDISCDD_PDEV)dhpdev;

    ASSERT(Pdev != NULL);
    ASSERT(Pdev->Surface == NULL);
    ASSERT(Pdev->Palette != NULL);

    EngDeletePalette(Pdev->Palette);
    EngFreeMem(Pdev);
}

BOOL
APIENTRY
DrvAssertMode(
    _In_ DHPDEV dhpdev,
    _In_ BOOL bEnable)
{
    UNREFERENCED_PARAMETER(bEnable);

    ASSERT(dhpdev != NULL);
    return TRUE;
}

static DRVFN DiscddFunctions[] =
{
    { INDEX_DrvEnablePDEV, (PFN)DrvEnablePDEV },
    { INDEX_DrvCompletePDEV, (PFN)DrvCompletePDEV },
    { INDEX_DrvDisablePDEV, (PFN)DrvDisablePDEV },
    { INDEX_DrvEnableSurface, (PFN)DrvEnableSurface },
    { INDEX_DrvDisableSurface, (PFN)DrvDisableSurface },
    { INDEX_DrvAssertMode, (PFN)DrvAssertMode },
    { INDEX_DrvGetModes, (PFN)DrvGetModes },
};

BOOL
APIENTRY
DrvEnableDriver(
    _In_ ULONG iEngineVersion,
    _In_ ULONG cj,
    _Inout_updates_bytes_(cj) DRVENABLEDATA *pded)
{
    if ((pded == NULL) || (cj < sizeof(DRVENABLEDATA)) || (iEngineVersion < DDI_DRIVER_VERSION_NT5_01))
        return FALSE;

    pded->iDriverVersion = DDI_DRIVER_VERSION_NT5_01;
    pded->c = ARRAYSIZE(DiscddFunctions);
    pded->pdrvfn = DiscddFunctions;
    return TRUE;
}
