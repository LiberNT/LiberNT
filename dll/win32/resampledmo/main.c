/*
 * PROJECT:     LiberNT Audio Resampler DSP
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Class factory and registration of the Audio Resampler DSP
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include "resampledmo_private.h"

#include "mfapi.h"
#include "mftransform.h"
#include "dmoreg.h"
#include "uuids.h"
#include "rpcproxy.h"
#include "wmcodecdsp.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(resampler);

static HRESULT WINAPI class_factory_QueryInterface(IClassFactory *iface, REFIID iid, void **out)
{
    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IClassFactory))
    {
        *out = iface;
        IClassFactory_AddRef(iface);
        return S_OK;
    }

    *out = NULL;
    WARN("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(iid));
    return E_NOINTERFACE;
}

static ULONG WINAPI class_factory_AddRef(IClassFactory *iface)
{
    return 2;
}

static ULONG WINAPI class_factory_Release(IClassFactory *iface)
{
    return 1;
}

static HRESULT WINAPI class_factory_CreateInstance(IClassFactory *iface, IUnknown *outer, REFIID iid, void **out)
{
    TRACE("iface %p, outer %p, iid %s, out %p.\n", iface, outer, debugstr_guid(iid), out);
    return resampler_create(outer, iid, out);
}

static HRESULT WINAPI class_factory_LockServer(IClassFactory *iface, BOOL lock)
{
    TRACE("iface %p, lock %d.\n", iface, lock);
    return S_OK;
}

static const IClassFactoryVtbl class_factory_vtbl =
{
    class_factory_QueryInterface,
    class_factory_AddRef,
    class_factory_Release,
    class_factory_CreateInstance,
    class_factory_LockServer,
};

static IClassFactory resampler_factory = {&class_factory_vtbl};

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, void *reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
        DisableThreadLibraryCalls(instance);
    return TRUE;
}

HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID iid, void **out)
{
    TRACE("clsid %s, iid %s, out %p.\n", debugstr_guid(clsid), debugstr_guid(iid), out);

    *out = NULL;
    if (IsEqualGUID(clsid, &CLSID_CResamplerMediaObject))
        return IClassFactory_QueryInterface(&resampler_factory, iid, out);

    FIXME("Unknown class %s.\n", debugstr_guid(clsid));
    return CLASS_E_CLASSNOTAVAILABLE;
}

HRESULT WINAPI DllRegisterServer(void)
{
    MFT_REGISTER_TYPE_INFO mft_types[] =
    {
        {MFMediaType_Audio, MFAudioFormat_PCM},
        {MFMediaType_Audio, MFAudioFormat_Float},
    };
    DMO_PARTIAL_MEDIATYPE dmo_types[] =
    {
        {MEDIATYPE_Audio, MEDIASUBTYPE_PCM},
        {MEDIATYPE_Audio, MEDIASUBTYPE_IEEE_FLOAT},
    };
    HRESULT hr;

    TRACE(".\n");

    if (FAILED(hr = __wine_register_resources()))
        return hr;
    if (FAILED(hr = DMORegister(L"Resampler DMO", &CLSID_CResamplerMediaObject, &DMOCATEGORY_AUDIO_EFFECT, 0,
            ARRAY_SIZE(dmo_types), dmo_types, ARRAY_SIZE(dmo_types), dmo_types)))
        return hr;
    return MFTRegister(CLSID_CResamplerMediaObject, MFT_CATEGORY_AUDIO_EFFECT, (WCHAR *)L"Resampler MFT", 0,
            ARRAY_SIZE(mft_types), mft_types, ARRAY_SIZE(mft_types), mft_types, NULL);
}

HRESULT WINAPI DllUnregisterServer(void)
{
    HRESULT hr;

    TRACE(".\n");

    if (FAILED(hr = __wine_unregister_resources()))
        return hr;
    MFTUnregister(CLSID_CResamplerMediaObject);
    DMOUnregister(&CLSID_CResamplerMediaObject, &DMOCATEGORY_AUDIO_EFFECT);
    return S_OK;
}
