/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Verify Direct3D HAL versions against the build 26100 SDK
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#if DIRECT3D_VERSION == 0x0700
#define D3D_OVERLOADS
#endif
#include <windows.h>
#include <d3dhal.h>

static_assert(D3DERR_COMMAND_UNPARSED == (HRESULT)0x88760bb8, "unparsed status");
static_assert(sizeof(D3DHAL_DP2COMMAND) == 4, "command header");
static_assert(sizeof(D3DHAL_DP2SETRENDERTARGET) == 8, "render target");
static_assert(sizeof(*LPD3DVECTOR{}) == 12 && sizeof(*LPD3DCOLOR{}) == 4, "legacy pointer aliases");
static_assert(sizeof(D3DMATRIX) == 64, "matrix");
static_assert(D3DFMT_S1D15 == 72 && D3DFMT_S8D24 == 74 &&
              D3DFMT_X8D24 == 76 && D3DFMT_X4S4D24 == 78, "depth formats");

#if DIRECT3D_VERSION >= 0x0800
static_assert(sizeof(D3DHAL_DP2DRAWINDEXEDPRIMITIVE) == 24, "indexed draw packet");
static_assert(sizeof(D3DHAL_DP2VOLUMEBLT) == 48, "volume blit packet");
static_assert(D3DDP2OP_CREATEVERTEXSHADER == 45, "DX8 vertex shader opcode");
#endif
#if DIRECT3D_VERSION >= 0x0900
static_assert(D3DDP2OP_SURFACEBLT == 96 && DX9_DDI_VERSION == 4, "DX9 DDI");
static_assert(D3DSI_GETREGTYPE(0x30000803u) == D3DSPR_CONST2, "split register type");
static_assert(D3DSI_GETINSTLENGTH(0x02000000u) == 2, "instruction length");
static_assert(D3DDP2OP_SETCONVOLUTIONKERNELMONO == 97 && D3DDP2OP_COMPOSERECTS == 98, "DX9Ex opcodes");
static_assert(sizeof(D3DHAL_DP2RESPONSE) == 8 && sizeof(D3DHAL_DP2SURFACEBLT) == 52, "DX9 packets");
#ifndef D3D_DISABLE_9EX
static_assert(sizeof(D3DHAL_DP2COMPOSERECTS) == 32, "compose rectangles packet");
#endif
static_assert(sizeof(DD_GETDRIVERINFO2DATA) == 16, "driver info header");
#endif
