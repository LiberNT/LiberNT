/*
 * Copyright (C) 2002 Jason Edmeades
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

#ifndef _D3D8FORMAT_H_
#define _D3D8FORMAT_H_

typedef enum _D3DFORMAT {
    D3DFMT_UNKNOWN              =   0,

    D3DFMT_R8G8B8               =  20,
    D3DFMT_A8R8G8B8             =  21,
    D3DFMT_X8R8G8B8             =  22,
    D3DFMT_R5G6B5               =  23,
    D3DFMT_X1R5G5B5             =  24,
    D3DFMT_A1R5G5B5             =  25,
    D3DFMT_A4R4G4B4             =  26,
    D3DFMT_R3G3B2               =  27,
    D3DFMT_A8                   =  28,
    D3DFMT_A8R3G3B2             =  29,
    D3DFMT_X4R4G4B4             =  30,
    D3DFMT_A2B10G10R10          =  31,
    D3DFMT_G16R16               =  34,

    D3DFMT_A8P8                 =  40,
    D3DFMT_P8                   =  41,

    D3DFMT_L8                   =  50,
    D3DFMT_A8L8                 =  51,
    D3DFMT_A4L4                 =  52,

    D3DFMT_V8U8                 =  60,
    D3DFMT_L6V5U5               =  61,
    D3DFMT_X8L8V8U8             =  62,
    D3DFMT_Q8W8V8U8             =  63,
    D3DFMT_V16U16               =  64,
    D3DFMT_W11V11U10            =  65,
    D3DFMT_A2W10V10U10          =  67,

    D3DFMT_UYVY                 =  MAKEFOURCC('U', 'Y', 'V', 'Y'),
    D3DFMT_YUY2                 =  MAKEFOURCC('Y', 'U', 'Y', '2'),
    D3DFMT_DXT1                 =  MAKEFOURCC('D', 'X', 'T', '1'),
    D3DFMT_DXT2                 =  MAKEFOURCC('D', 'X', 'T', '2'),
    D3DFMT_DXT3                 =  MAKEFOURCC('D', 'X', 'T', '3'),
    D3DFMT_DXT4                 =  MAKEFOURCC('D', 'X', 'T', '4'),
    D3DFMT_DXT5                 =  MAKEFOURCC('D', 'X', 'T', '5'),

    D3DFMT_D16_LOCKABLE         =  70,
    D3DFMT_D32                  =  71,
    D3DFMT_D15S1                =  73,
    D3DFMT_D24S8                =  75,
    D3DFMT_D16                  =  80,
    D3DFMT_D24X8                =  77,
    D3DFMT_D24X4S4              =  79,

    D3DFMT_VERTEXDATA           = 100,
    D3DFMT_INDEX16              = 101,
    D3DFMT_INDEX32              = 102,

    D3DFMT_FORCE_DWORD          = 0xFFFFFFFF
} D3DFORMAT;

#endif
