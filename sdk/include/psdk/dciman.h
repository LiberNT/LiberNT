/*
 * DCI driver interface
 *
 * Copyright (C) 2005 Francois Gouget
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#ifndef _INC_DCIMAN
#define _INC_DCIMAN

#include <dciddi.h>

#ifdef __cplusplus
extern "C" {
#endif

HDC         WINAPI DCIOpenProvider(void);
void        WINAPI DCICloseProvider(HDC);
int         WINAPI DCICreatePrimary(HDC,LPDCISURFACEINFO*);

#ifdef __cplusplus
} /* extern "C" */
#endif

#ifndef WINWATCHNOTIFY_CHANGED
#define WINWATCHNOTIFY_CHANGED 4
#endif
#ifndef WINWATCHNOTIFY_CHANGING
#define WINWATCHNOTIFY_CHANGING 3
#endif
#ifndef WINWATCHNOTIFY_DESTROY
#define WINWATCHNOTIFY_DESTROY 2
#endif
#ifndef WINWATCHNOTIFY_START
#define WINWATCHNOTIFY_START 0
#endif
#ifndef WINWATCHNOTIFY_STOP
#define WINWATCHNOTIFY_STOP 1
#endif

typedef struct HWINWATCH__ {
  int unused;
} *HWINWATCH;

typedef void (WINAPI *WINWATCHNOTIFYPROC)(HWINWATCH, HWND, DWORD, LPARAM);

#ifdef __cplusplus
extern "C" {
#endif

/* TODO: LiberNT does not implement the functions below yet */
DCIRVAL WINAPI DCIBeginAccess(LPDCISURFACEINFO pdci, int x, int y, int dx, int dy);
int WINAPI DCICreateOffscreen(HDC hdc, DWORD dwCompression, DWORD dwRedMask, DWORD dwGreenMask, DWORD dwBlueMask, DWORD dwWidth, DWORD dwHeight, DWORD dwDCICaps, DWORD dwBitCount, LPDCIOFFSCREEN *lplpSurface);
int WINAPI DCICreateOverlay(HDC hdc, LPVOID lpOffscreenSurf, LPDCIOVERLAY *lplpSurface);
void WINAPI DCIDestroy(LPDCISURFACEINFO pdci);
DCIRVAL WINAPI DCIDraw(LPDCIOFFSCREEN pdci);
void WINAPI DCIEndAccess(LPDCISURFACEINFO pdci);
int WINAPI DCIEnum(HDC hdc, LPRECT lprDst, LPRECT lprSrc, LPVOID lpFnCallback, LPVOID lpContext);
DCIRVAL WINAPI DCISetClipList(LPDCIOFFSCREEN pdci, LPRGNDATA prd);
DCIRVAL WINAPI DCISetDestination(LPDCIOFFSCREEN pdci, LPRECT dst, LPRECT src);
DCIRVAL WINAPI DCISetSrcDestClip(LPDCIOFFSCREEN pdci, LPRECT srcrc, LPRECT destrc, LPRGNDATA prd);
DWORD WINAPI GetDCRegionData(HDC hdc, DWORD size, LPRGNDATA prd);
DWORD WINAPI GetWindowRegionData(HWND hwnd, DWORD size, LPRGNDATA prd);
void WINAPI WinWatchClose(HWINWATCH hWW);
BOOL WINAPI WinWatchDidStatusChange(HWINWATCH hWW);
UINT WINAPI WinWatchGetClipList(HWINWATCH hWW, LPRECT prc, UINT size, LPRGNDATA prd);
BOOL WINAPI WinWatchNotify(HWINWATCH hWW, WINWATCHNOTIFYPROC NotifyCallback, LPARAM NotifyParam);
HWINWATCH WINAPI WinWatchOpen(HWND hwnd);

#ifdef __cplusplus
}
#endif

#endif /* _INC_DCIMAN */
