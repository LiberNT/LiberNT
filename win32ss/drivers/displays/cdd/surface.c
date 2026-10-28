/*
 * PROJECT:     ReactOS Canonical Display Driver (cdd.dll)
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Primary surface lifetime (DrvEnableSurface/DrvDisableSurface).
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 *
 * This is the one file that differs in substance from a plain framebuffer
 * driver: it decides where the primary drawing surface lives.
 *
 *   Option A (shipped): map dxgkrnl's shadow framebuffer via
 *      IOCTL_VIDEO_MAP_VIDEO_MEMORY on \Device\VideoN and wrap it in a GDI DIB.
 *      GDI draws into a cached shadow; RcddPresent (present.c) streams dirty
 *      rectangles to the mapping, and dxgkrnl's present timer scans them to the
 *      GOP. This reuses the already-verified WDDM scan-out path and is the
 *      reason cdd builds and renders today.
 *
 *   Option B (TODO): open a D3DKMT device, CreateAllocation a primary, lock and
 *      map it, wrap THAT in the GDI DIB, and have RcddPresent issue
 *      D3DKMTPresent / SetVidPnSourceAddress. This is the honest WDDM path but
 *      needs an in-kernel D3DKMT client reachable from a win32k-loaded GDI
 *      driver (win32k currently exports only the Eng* surface/IOCTL DDI, not a
 *      kernel D3DKMT client), so it is deferred. The seam is RcddPresent() plus
 *      the mapping below - nothing else in cdd changes for the upgrade.
 */

#include "cdd.h"

#define RCDD_SURFACE_HOOKS (HOOK_BITBLT | HOOK_COPYBITS | HOOK_SYNCHRONIZE | \
                            HOOK_TEXTOUT | HOOK_LINETO | HOOK_PAINT | HOOK_PLGBLT | \
                            HOOK_STROKEPATH | \
                            HOOK_FILLPATH | HOOK_STROKEANDFILLPATH | HOOK_STRETCHBLT | \
                            HOOK_STRETCHBLTROP | HOOK_ALPHABLEND | HOOK_TRANSPARENTBLT | \
                            HOOK_GRADIENTFILL)

/*
 * RcddEnableSurface
 *
 * Sets the WDDM mode, maps the scan-out, and creates the GDI bitmap surface
 * the engine rasterizer draws into.
 */
HSURF APIENTRY
RcddEnableSurface(
   IN DHPDEV dhpdev)
{
   PRCDD_PDEV ppdev = (PRCDD_PDEV)dhpdev;
   HSURF hSurface;
   ULONG BitmapType;
   SIZEL ScreenSize;
   VIDEO_MEMORY VideoMemory;
   VIDEO_MEMORY_INFORMATION VideoMemoryInfo;
   ULONGLONG MinimumStride;
   ULONGLONG RequiredBytes;
   ULONG ulTemp;
   FLONG flHooks = 0;
   PVOID SurfaceBits;

   RtlZeroMemory(&VideoMemoryInfo, sizeof(VideoMemoryInfo));

   /* Commit the requested mode on the WDDM display device. */
   if (EngDeviceIoControl(ppdev->hDriver, IOCTL_VIDEO_SET_CURRENT_MODE,
                          &(ppdev->ModeIndex), sizeof(ULONG), NULL, 0,
                          &ulTemp))
   {
      return NULL;
   }

   /*
    * Re-query the mode dxgkrnl actually committed. RcddInitScreenInfo captured
    * the geometry at EnablePDEV time, BEFORE CommitVidPn ran — if the committed
    * native mode differs, drawing with the stale stride would shear the
    * scan-out. Adopt the committed stride; refuse a width/height mismatch
    * (GDI was already promised the EnablePDEV geometry).
    */
   {
      VIDEO_MODE_INFORMATION CommittedMode;

      RtlZeroMemory(&CommittedMode, sizeof(CommittedMode));
      if (!EngDeviceIoControl(ppdev->hDriver, IOCTL_VIDEO_QUERY_CURRENT_MODE,
                              NULL, 0, &CommittedMode, sizeof(CommittedMode),
                              &ulTemp))
      {
         if (ulTemp < sizeof(CommittedMode))
            return NULL;
         if (CommittedMode.VisScreenWidth != (ULONG)ppdev->ScreenWidth ||
             CommittedMode.VisScreenHeight != (ULONG)ppdev->ScreenHeight)
         {
            /* Committed native mode differs from what EnablePDEV promised
             * GDI — a sheared scan-out is worse than no surface. */
            return NULL;
         }

         if (CommittedMode.ScreenStride != 0)
            ppdev->ScreenDelta = CommittedMode.ScreenStride;
      }
   }

   /*
    * Map the DOD primary (dxgkrnl's system-memory scan-out surface) into our
    * address space. GDI draws straight into it and RcddPresent scans dirty
    * rectangles out through the miniport's DxgkDdiPresentDisplayOnly.
    */
   VideoMemory.RequestedVirtualAddress = NULL;
   if (EngDeviceIoControl(ppdev->hDriver, IOCTL_VIDEO_MAP_VIDEO_MEMORY,
                          &VideoMemory, sizeof(VIDEO_MEMORY),
                          &VideoMemoryInfo, sizeof(VIDEO_MEMORY_INFORMATION),
                          &ulTemp) ||
       ulTemp < sizeof(VIDEO_MEMORY_INFORMATION))
   {
      return NULL;
   }

   ppdev->ScreenPtr = VideoMemoryInfo.FrameBufferBase;
   if (ppdev->ScreenPtr == NULL)
      goto CleanupMapping;

   MinimumStride = (((ULONGLONG)ppdev->ScreenWidth * ppdev->BitsPerPixel) + 7) / 8;
   RequiredBytes = (ULONGLONG)ppdev->ScreenDelta * ppdev->ScreenHeight;
   if (ppdev->ScreenDelta < MinimumStride ||
       RequiredBytes > VideoMemoryInfo.FrameBufferLength)
   {
      goto CleanupMapping;
   }

   switch (ppdev->BitsPerPixel)
   {
      case 8:
         RcddSetPaletteEntries(dhpdev, ppdev->PaletteEntries, 0, 256);
         BitmapType = BMF_8BPP;
         break;

      case 16:
         BitmapType = BMF_16BPP;
         break;

      case 24:
         BitmapType = BMF_24BPP;
         break;

      case 32:
         BitmapType = BMF_32BPP;
         break;

      default:
         goto CleanupMapping;
   }

   ScreenSize.cx = ppdev->ScreenWidth;
   ScreenSize.cy = ppdev->ScreenHeight;

   /*
    * Hand GDI a plain DIB straight over the mapped DOD primary: the GDI engine
    * draws every operation (blits, text, fills, alpha) directly into the buffer
    * dxgkrnl's DxgkDdiPresentDisplayOnly reads. There is no separate CPU shadow
    * or cache, so nothing can be stranded unpresented. We hook
    * BitBlt/CopyBits/Synchronize only to learn which rectangles changed and then
    * drive an explicit WDDM dirty-rect present (RcddPresent ->
    * IOCTL_VIDEO_DXGK_PRESENT_DIRTY_RECT -> the miniport's PresentDisplayOnly).
    * The completed SynchronizeSurface callback submits the stable primary
    * directly to the synchronous display-only present DDI. The compositor
    * seam is escape.c.
    */
   SurfaceBits = ppdev->ScreenPtr;
   flHooks = RCDD_SURFACE_HOOKS;

   /*
    * Hand GDI a plain DIB over the draw buffer. No raster ops are implemented
    * here - the engine paints into this bitmap and we only present it.
    */
   hSurface = (HSURF)EngCreateBitmap(ScreenSize, ppdev->ScreenDelta, BitmapType,
                                     (ppdev->ScreenDelta > 0) ? BMF_TOPDOWN : 0,
                                     SurfaceBits);
   if (hSurface == NULL)
   {
      goto CleanupMapping;
   }

   if (!EngAssociateSurface(hSurface, ppdev->hDevEng, flHooks))
   {
      EngDeleteSurface(hSurface);
      VideoMemory.RequestedVirtualAddress = ppdev->ScreenPtr;
      EngDeviceIoControl(ppdev->hDriver, IOCTL_VIDEO_UNMAP_VIDEO_MEMORY,
                         &VideoMemory, sizeof(VIDEO_MEMORY), NULL, 0, &ulTemp);
      ppdev->ScreenPtr = NULL;
      return NULL;
   }

   ppdev->hSurfEng = hSurface;

   return hSurface;

CleanupMapping:
   VideoMemory.RequestedVirtualAddress = ppdev->ScreenPtr;
   EngDeviceIoControl(ppdev->hDriver, IOCTL_VIDEO_UNMAP_VIDEO_MEMORY,
                      &VideoMemory, sizeof(VIDEO_MEMORY), NULL, 0, &ulTemp);
   ppdev->ScreenPtr = NULL;
   return NULL;
}

/*
 * RcddDisableSurface
 *
 * Notifies the driver that the surface created by RcddEnableSurface is no
 * longer needed and unmaps the scan-out.
 */
VOID APIENTRY
RcddDisableSurface(
   IN DHPDEV dhpdev)
{
   DWORD ulTemp;
   VIDEO_MEMORY VideoMemory;
   PRCDD_PDEV ppdev = (PRCDD_PDEV)dhpdev;

   KeWaitForSingleObject(&ppdev->CaptureMutex, Executive, KernelMode, FALSE, NULL);
   if (ppdev->CaptureSurface != NULL)
   {
      EngUnlockSurface(ppdev->CaptureSurface);
      ppdev->CaptureSurface = NULL;
   }
   if (ppdev->CaptureBitmap != NULL)
   {
      EngDeleteSurface(ppdev->CaptureBitmap);
      ppdev->CaptureBitmap = NULL;
   }
   KeReleaseMutex(&ppdev->CaptureMutex, FALSE);

   if (ppdev->hSurfEng != NULL)
   {
      EngDeleteSurface(ppdev->hSurfEng);
      ppdev->hSurfEng = NULL;
   }

   if (ppdev->ScreenPtr != NULL)
   {
      VideoMemory.RequestedVirtualAddress = ppdev->ScreenPtr;
      EngDeviceIoControl(ppdev->hDriver, IOCTL_VIDEO_UNMAP_VIDEO_MEMORY,
                         &VideoMemory, sizeof(VIDEO_MEMORY), NULL, 0, &ulTemp);
      ppdev->ScreenPtr = NULL;
   }
}

VOID
RcddRemapSurface(
   IN PRCDD_PDEV ppdev)
{
   VIDEO_MEMORY VideoMemory;
   VIDEO_MEMORY_INFORMATION VideoMemoryInfo;
   ULONGLONG RequiredBytes;
   PVOID OldPtr = ppdev->ScreenPtr;
   ULONG ulTemp;

   if (ppdev->hSurfEng == NULL || OldPtr == NULL || ppdev->ScreenDelta == 0)
      return;

   RtlZeroMemory(&VideoMemoryInfo, sizeof(VideoMemoryInfo));
   VideoMemory.RequestedVirtualAddress = NULL;
   if (EngDeviceIoControl(ppdev->hDriver, IOCTL_VIDEO_MAP_VIDEO_MEMORY,
                          &VideoMemory, sizeof(VIDEO_MEMORY),
                          &VideoMemoryInfo, sizeof(VIDEO_MEMORY_INFORMATION),
                          &ulTemp) ||
       ulTemp < sizeof(VIDEO_MEMORY_INFORMATION) ||
       VideoMemoryInfo.FrameBufferBase == NULL)
   {
      return;
   }

   RequiredBytes = (ULONGLONG)ppdev->ScreenDelta * ppdev->ScreenHeight;
   if (VideoMemoryInfo.FrameBufferBase != OldPtr &&
       RequiredBytes <= VideoMemoryInfo.FrameBufferLength)
   {
      RtlCopyMemory(VideoMemoryInfo.FrameBufferBase, OldPtr, (SIZE_T)RequiredBytes);
   }
   if (VideoMemoryInfo.FrameBufferBase == OldPtr ||
       RequiredBytes > VideoMemoryInfo.FrameBufferLength ||
       !EngModifySurface(ppdev->hSurfEng, ppdev->hDevEng, RCDD_SURFACE_HOOKS, 0,
                         NULL, VideoMemoryInfo.FrameBufferBase,
                         (LONG)ppdev->ScreenDelta, NULL))
   {
      VideoMemory.RequestedVirtualAddress = VideoMemoryInfo.FrameBufferBase;
      EngDeviceIoControl(ppdev->hDriver, IOCTL_VIDEO_UNMAP_VIDEO_MEMORY,
                         &VideoMemory, sizeof(VIDEO_MEMORY), NULL, 0, &ulTemp);
      return;
   }

   ppdev->ScreenPtr = VideoMemoryInfo.FrameBufferBase;
   VideoMemory.RequestedVirtualAddress = OldPtr;
   EngDeviceIoControl(ppdev->hDriver, IOCTL_VIDEO_UNMAP_VIDEO_MEMORY,
                      &VideoMemory, sizeof(VIDEO_MEMORY), NULL, 0, &ulTemp);
}
