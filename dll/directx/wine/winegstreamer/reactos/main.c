/*
 * ReactOS native adapter for Wine GStreamer parser classes
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * COPYRIGHT:   Adapted from Wine dlls/winegstreamer/main.c
 *
 * This keeps Wine's private winegstreamer COM boundary on systems without
 * a Unix GStreamer backend.
 */

#include <stdarg.h>

#define COBJMACROS
#include "windef.h"
#include "winbase.h"
#include "objbase.h"
#include "rpcproxy.h"
#include "wine/debug.h"

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, void *reserved)
{
    UNREFERENCED_PARAMETER(reserved);
    if (reason == DLL_PROCESS_ATTACH)
        DisableThreadLibraryCalls(instance);
    return TRUE;
}

HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID iid, void **out)
{
    UNREFERENCED_PARAMETER(clsid);
    UNREFERENCED_PARAMETER(iid);
    if (out)
        *out = NULL;
    return CLASS_E_CLASSNOTAVAILABLE;
}

HRESULT WINAPI DllCanUnloadNow(void)
{
    return S_OK;
}

HRESULT WINAPI DllRegisterServer(void)
{
    return __wine_register_resources();
}

HRESULT WINAPI DllUnregisterServer(void)
{
    return __wine_unregister_resources();
}

HRESULT WINAPI winegstreamer_create_wm_sync_reader(IUnknown *outer, void **out)
{
    UNREFERENCED_PARAMETER(outer);
    if (out)
        *out = NULL;
    return E_NOTIMPL;
}

HRESULT WINAPI winegstreamer_create_video_decoder(void **out)
{
    if (out)
        *out = NULL;
    return E_NOTIMPL;
}
