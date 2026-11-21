/*
 * Copyright (c) 2015 Andrew Eikum for CodeWeavers
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

/* CLSIDs used by CreateFX, but never registered */

#ifndef _XAPOFX_H
#define _XAPOFX_H

/* xapofx 1.0 through 1.5 */
DEFINE_GUID(CLSID_FXEQ27, 0xa90bc001, 0xe897, 0xe897, 0x74, 0x39, 0x43, 0x55, 0x00, 0x00, 0x00, 0x00);
/* xaudio >= 2.8 */
DEFINE_GUID(CLSID_FXEQ, 0xf5e01117, 0xd6c4, 0x485a, 0xa3, 0xf5, 0x69, 0x51, 0x96, 0xf3, 0xdb, 0xfa);

/* xapofx 1.0 through 1.5 */
DEFINE_GUID(CLSID_FXMasteringLimiter27, 0xa90bc001, 0xe897, 0xe897, 0x74, 0x39, 0x43, 0x55, 0x00, 0x00, 0x00, 0x01);
/* xaudio >= 2.8 */
DEFINE_GUID(CLSID_FXMasteringLimiter, 0xc4137916, 0x2be1, 0x46fd, 0x85, 0x99, 0x44, 0x15, 0x36, 0xf4, 0x98, 0x56);

/* xapofx 1.0 through 1.5 */
DEFINE_GUID(CLSID_FXReverb27, 0xa90bc001, 0xe897, 0xe897, 0x74, 0x39, 0x43, 0x55, 0x00, 0x00, 0x00, 0x02);
/* xaudio >= 2.8 */
DEFINE_GUID(CLSID_FXReverb, 0x7d9aca56, 0xcb68, 0x4807, 0xb6, 0x32, 0xb1, 0x37, 0x35, 0x2e, 0x85, 0x96);

/* xapofx 1.0 through 1.5 */
DEFINE_GUID(CLSID_FXEcho27, 0xa90bc001, 0xe897, 0xe897, 0x74, 0x39, 0x43, 0x55, 0x00, 0x00, 0x00, 0x03);
/* xaudio >= 2.8 */
DEFINE_GUID(CLSID_FXEcho, 0x5039d740, 0xf736, 0x449a, 0x84, 0xd3, 0xa5, 0x62, 0x02, 0x55, 0x7b, 0x87);

#ifndef FXEQ_MAX_FRAMERATE
#define FXEQ_MAX_FRAMERATE 48000
#endif
#ifndef FXEQ_MIN_FRAMERATE
#define FXEQ_MIN_FRAMERATE 22000
#endif
#ifndef FXLOUDNESS_DEFAULT_MOMENTARY_MS
#define FXLOUDNESS_DEFAULT_MOMENTARY_MS 400
#endif
#ifndef FXLOUDNESS_DEFAULT_SHORTTERM_MS
#define FXLOUDNESS_DEFAULT_SHORTTERM_MS 3000
#endif
#ifndef FXMASTERINGLIMITER_DEFAULT_LOUDNESS
#define FXMASTERINGLIMITER_DEFAULT_LOUDNESS 1000
#endif
#ifndef FXMASTERINGLIMITER_DEFAULT_RELEASE
#define FXMASTERINGLIMITER_DEFAULT_RELEASE 6
#endif
#ifndef FXMASTERINGLIMITER_MAX_LOUDNESS
#define FXMASTERINGLIMITER_MAX_LOUDNESS 1800
#endif
#ifndef FXMASTERINGLIMITER_MAX_RELEASE
#define FXMASTERINGLIMITER_MAX_RELEASE 20
#endif
#ifndef FXMASTERINGLIMITER_MIN_LOUDNESS
#define FXMASTERINGLIMITER_MIN_LOUDNESS 1
#endif
#ifndef FXMASTERINGLIMITER_MIN_RELEASE
#define FXMASTERINGLIMITER_MIN_RELEASE 1
#endif

#pragma pack(push, 1)
typedef struct FXECHO_INITDATA {
  float MaxDelay;
} FXECHO_INITDATA;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct FXECHO_PARAMETERS {
  float WetDryMix;
  float Feedback;
  float Delay;
} FXECHO_PARAMETERS;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct FXEQ_PARAMETERS {
  float FrequencyCenter0;
  float Gain0;
  float Bandwidth0;
  float FrequencyCenter1;
  float Gain1;
  float Bandwidth1;
  float FrequencyCenter2;
  float Gain2;
  float Bandwidth2;
  float FrequencyCenter3;
  float Gain3;
  float Bandwidth3;
} FXEQ_PARAMETERS;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct FXMASTERINGLIMITER_PARAMETERS {
  UINT32 Release;
  UINT32 Loudness;
} FXMASTERINGLIMITER_PARAMETERS;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct FXREVERB_PARAMETERS {
  float Diffusion;
  float RoomSize;
} FXREVERB_PARAMETERS;
#pragma pack(pop)

#define FX_API_type) STDAPIV_(type)
#define FXECHO_DEFAULT_DELAY 0x1F4
#define FXECHO_DEFAULT_FEEDBACK 0
#define FXECHO_DEFAULT_WETDRYMIX 0
#define FXECHO_MAX_DELAY 0x7D0
#define FXECHO_MAX_FEEDBACK 1
#define FXECHO_MAX_WETDRYMIX 1
#define FXECHO_MIN_DELAY 1
#define FXECHO_MIN_FEEDBACK 0
#define FXECHO_MIN_WETDRYMIX 0
#define FXEQ_DEFAULT_BANDWIDTH 1
#define FXEQ_DEFAULT_FREQUENCY_CENTER_0 0x64
#define FXEQ_DEFAULT_FREQUENCY_CENTER_1 0x320
#define FXEQ_DEFAULT_FREQUENCY_CENTER_2 0x7D0
#define FXEQ_DEFAULT_FREQUENCY_CENTER_3 0x2710
#define FXEQ_DEFAULT_GAIN 1
#define FXEQ_MAX_BANDWIDTH 2
#define FXEQ_MAX_FREQUENCY_CENTER 0x4E20
#define FXEQ_MAX_GAIN 7
#define FXEQ_MIN_BANDWIDTH 0
#define FXEQ_MIN_FREQUENCY_CENTER 0x14
#define FXEQ_MIN_GAIN 0
#define FXREVERB_DEFAULT_DIFFUSION 0
#define FXREVERB_DEFAULT_ROOMSIZE 0
#define FXREVERB_MAX_DIFFUSION 1
#define FXREVERB_MAX_ROOMSIZE 1
#define FXREVERB_MIN_DIFFUSION 0
#define FXREVERB_MIN_ROOMSIZE 0

#ifdef __cplusplus
extern "C" {
#endif

HRESULT __cdecl CreateFX(const IID *const clsid, IUnknown **pEffect, const void *pInitDat, UINT32 InitDataByteSize);

#ifdef __cplusplus
}
#endif

#define FX_API_type) STDAPIV_(type)

#define FX_API_type) STDAPIV_(type)

#define FX_API_type) STDAPIV_(type)

#endif
