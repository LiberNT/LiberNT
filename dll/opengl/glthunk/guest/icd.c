/*
 * PROJECT:     LiberNT OpenGL thunk
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     OpenGL ICD of emulated AMD64 processes that renders with the native OpenGL host
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <windef.h>
#include <winbase.h>
#include <wingdi.h>
#include <libernt/emuhost.h>
#include "../glthunk.h"

#define GLTHUNK_STRING2(Value) #Value
#define GLTHUNK_STRING(Value) GLTHUNK_STRING2(Value)

typedef PVOID DHGLRC;

static PGLTHUNK_TABLE GlThunkTable;

__attribute__((naked))
static
ULONG64
GlThunkLoadHost(
    _In_ PCWSTR Name)
{
    __asm__ __volatile__("movq %rcx, %r10\n\t"
                         "movl $" GLTHUNK_STRING(EMU_HOST_SERVICE_LOAD) ", %eax\n\t"
                         "syscall\n\t"
                         "ret");
}

static
PGLTHUNK_TABLE
GlThunkGetTable(VOID)
{
    PGLTHUNK_TABLE Table = GlThunkTable;

    if (!Table)
    {
        Table = (PGLTHUNK_TABLE)(ULONG_PTR)GlThunkLoadHost(GLTHUNK_HOST_LIBRARY);
        if (Table && Table->Size < sizeof(*Table)) Table = NULL;
        GlThunkTable = Table;
    }
    return Table;
}

#define GLTHUNK_CALL(Member, Failure, Type, ...) \
    do \
    { \
        PGLTHUNK_TABLE Table = GlThunkGetTable(); \
        if (!Table) return Failure; \
        return ((Type)(ULONG_PTR)Table->Member)(__VA_ARGS__); \
    } while (0)

BOOL
WINAPI
DrvValidateVersion(
    _In_ DWORD Version)
{
    GLTHUNK_CALL(DrvValidateVersion, FALSE, BOOL (WINAPI *)(DWORD), Version);
}

VOID
WINAPI
DrvSetCallbackProcs(
    _In_ INT Count,
    _In_reads_(Count) PROC *Procedures)
{
    PGLTHUNK_TABLE Table = GlThunkGetTable();

    if (Table) ((VOID (WINAPI *)(INT, PROC *))(ULONG_PTR)Table->DrvSetCallbackProcs)(Count, Procedures);
}

BOOL
WINAPI
DrvCopyContext(
    _In_ DHGLRC Source,
    _In_ DHGLRC Destination,
    _In_ UINT Mask)
{
    GLTHUNK_CALL(DrvCopyContext, FALSE, BOOL (WINAPI *)(DHGLRC, DHGLRC, UINT), Source, Destination, Mask);
}

DHGLRC
WINAPI
DrvCreateContext(
    _In_ HDC Dc)
{
    GLTHUNK_CALL(DrvCreateContext, NULL, DHGLRC (WINAPI *)(HDC), Dc);
}

DHGLRC
WINAPI
DrvCreateLayerContext(
    _In_ HDC Dc,
    _In_ INT Layer)
{
    GLTHUNK_CALL(DrvCreateLayerContext, NULL, DHGLRC (WINAPI *)(HDC, INT), Dc, Layer);
}

BOOL
WINAPI
DrvDeleteContext(
    _In_ DHGLRC Context)
{
    GLTHUNK_CALL(DrvDeleteContext, FALSE, BOOL (WINAPI *)(DHGLRC), Context);
}

BOOL
WINAPI
DrvDescribeLayerPlane(
    _In_ HDC Dc,
    _In_ INT Format,
    _In_ INT Layer,
    _In_ UINT Size,
    _Out_ LPLAYERPLANEDESCRIPTOR Descriptor)
{
    GLTHUNK_CALL(DrvDescribeLayerPlane, FALSE, BOOL (WINAPI *)(HDC, INT, INT, UINT, LPLAYERPLANEDESCRIPTOR),
                 Dc, Format, Layer, Size, Descriptor);
}

LONG
WINAPI
DrvDescribePixelFormat(
    _In_ HDC Dc,
    _In_ INT Format,
    _In_ ULONG Size,
    _Out_opt_ PIXELFORMATDESCRIPTOR *Descriptor)
{
    GLTHUNK_CALL(DrvDescribePixelFormat, 0, LONG (WINAPI *)(HDC, INT, ULONG, PIXELFORMATDESCRIPTOR *),
                 Dc, Format, Size, Descriptor);
}

int
WINAPI
DrvGetLayerPaletteEntries(
    _In_ HDC Dc,
    _In_ INT Layer,
    _In_ INT Start,
    _In_ INT Count,
    _Out_ COLORREF *Entries)
{
    GLTHUNK_CALL(DrvGetLayerPaletteEntries, 0, int (WINAPI *)(HDC, INT, INT, INT, COLORREF *),
                 Dc, Layer, Start, Count, Entries);
}

PROC
WINAPI
DrvGetProcAddress(
    _In_ LPCSTR Name)
{
    GLTHUNK_CALL(DrvGetProcAddress, NULL, PROC (WINAPI *)(LPCSTR), Name);
}

BOOL
WINAPI
DrvReleaseContext(
    _In_ DHGLRC Context)
{
    GLTHUNK_CALL(DrvReleaseContext, FALSE, BOOL (WINAPI *)(DHGLRC), Context);
}

BOOL
WINAPI
DrvRealizeLayerPalette(
    _In_ HDC Dc,
    _In_ INT Layer,
    _In_ BOOL Realize)
{
    GLTHUNK_CALL(DrvRealizeLayerPalette, FALSE, BOOL (WINAPI *)(HDC, INT, BOOL), Dc, Layer, Realize);
}

PVOID
WINAPI
DrvSetContext(
    _In_ HDC Dc,
    _In_ DHGLRC Context,
    _In_opt_ PVOID Callback)
{
    GLTHUNK_CALL(DrvSetContext, NULL, PVOID (WINAPI *)(HDC, DHGLRC, PVOID), Dc, Context, Callback);
}

int
WINAPI
DrvSetLayerPaletteEntries(
    _In_ HDC Dc,
    _In_ INT Layer,
    _In_ INT Start,
    _In_ INT Count,
    _In_ CONST COLORREF *Entries)
{
    GLTHUNK_CALL(DrvSetLayerPaletteEntries, 0, int (WINAPI *)(HDC, INT, INT, INT, CONST COLORREF *),
                 Dc, Layer, Start, Count, Entries);
}

BOOL
WINAPI
DrvSetPixelFormat(
    _In_ HDC Dc,
    _In_ LONG Format)
{
    GLTHUNK_CALL(DrvSetPixelFormat, FALSE, BOOL (WINAPI *)(HDC, LONG), Dc, Format);
}

BOOL
WINAPI
DrvShareLists(
    _In_ DHGLRC First,
    _In_ DHGLRC Second)
{
    GLTHUNK_CALL(DrvShareLists, FALSE, BOOL (WINAPI *)(DHGLRC, DHGLRC), First, Second);
}

BOOL
WINAPI
DrvSwapBuffers(
    _In_ HDC Dc)
{
    GLTHUNK_CALL(DrvSwapBuffers, FALSE, BOOL (WINAPI *)(HDC), Dc);
}

BOOL
WINAPI
DrvSwapLayerBuffers(
    _In_ HDC Dc,
    _In_ UINT Planes)
{
    GLTHUNK_CALL(DrvSwapLayerBuffers, FALSE, BOOL (WINAPI *)(HDC, UINT), Dc, Planes);
}
