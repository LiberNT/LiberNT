/*
 * PROJECT:     LiberNT DirectX Diagnostic Tool
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Rotating LiberNT mark drawn by the Direct3D tests
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef D3DMARK_H__
#define D3DMARK_H__

#define D3DTEST_MARK_VERTEX_COUNT 96
#define D3DTEST_HUD_VERTEX_COUNT 3072
#define D3DTEST_MAX_VERTEX_COUNT (D3DTEST_MARK_VERTEX_COUNT + D3DTEST_HUD_VERTEX_COUNT)
#define D3DTEST_HUD_TEXT_COLOR 0xffffffff
#define D3DTEST_HUD_BACK_COLOR 0xff1c1c1c

typedef struct
{
    FLOAT x;
    FLOAT y;
    FLOAT z;
    FLOAT rhw;
    DWORD color;
} D3DTEST_VERTEX, *PD3DTEST_VERTEX;

typedef struct
{
    FLOAT Position[4];
    FLOAT Color[4];
} D3DTEST_CLIP_VERTEX, *PD3DTEST_CLIP_VERTEX;

UINT D3DTestBuildMark(PD3DTEST_VERTEX Vertices, DWORD Elapsed, UINT Width, UINT Height);
UINT D3DTestHudRects(RECT *Rects, UINT Capacity, const PCSTR *Lines, UINT LineCount, UINT Height);
UINT D3DTestBuildHud(PD3DTEST_VERTEX Vertices, UINT Capacity, const PCSTR *Lines, UINT LineCount, UINT Height);
VOID D3DTestToClipSpace(PD3DTEST_CLIP_VERTEX Output, const D3DTEST_VERTEX *Input, UINT Count, UINT Width, UINT Height);

#endif /* D3DMARK_H__ */
