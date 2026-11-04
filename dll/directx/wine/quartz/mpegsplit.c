/*
 * PROJECT:     LiberNT DirectShow
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     MPEG-1 stream splitter filter
 * COPYRIGHT:   Copyright 2024 Rémi Bernon for CodeWeavers
 */

#include "quartz_private.h"

HRESULT mpeg1_splitter_create(IUnknown *outer, IUnknown **out)
{
    /* FIXME: Still created by the legacy winegstreamer splitter. */
    static const GUID CLSID_wg_mpeg1_splitter = {0xa8edbf98,0x2442,0x42c5,{0x85,0xa1,0xab,0x05,0xa5,0x80,0xdf,0x53}};
    return CoCreateInstance(&CLSID_wg_mpeg1_splitter, outer, CLSCTX_INPROC_SERVER, &IID_IUnknown, (void **)out);
}
