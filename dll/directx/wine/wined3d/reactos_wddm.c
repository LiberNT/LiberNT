/*
 * PROJECT:     LiberNT Direct3D
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Adapter identity of a WDDM adapter
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define COBJMACROS
#include <stdarg.h>
#include <stdlib.h>
#include <windef.h>
#include <winbase.h>
#include <winnls.h>
#include <wingdi.h>
#include <objbase.h>
#include <d3dkmthk.h>
#include "wine/wined3d.h"

void wined3d_get_wddm_adapter_identifier(D3DKMT_HANDLE kmt_adapter,
        struct wined3d_adapter_identifier *identifier)
{
    D3DKMT_QUERYADAPTERINFO query = {0};
    D3DKMT_QUERY_DEVICE_IDS ids = {0};
    D3DKMT_UMDFILENAMEINFO umd = {0};
    D3DKMT_ADAPTERREGISTRYINFO registry = {0};
    D3DKMT_DRIVER_DESCRIPTION *description;
    LARGE_INTEGER version;
    const WCHAR *name;

    if (!kmt_adapter)
        return;
    query.hAdapter = kmt_adapter;

    query.Type = KMTQAITYPE_PHYSICALADAPTERDEVICEIDS;
    query.pPrivateDriverData = &ids;
    query.PrivateDriverDataSize = sizeof(ids);
    if (!D3DKMTQueryAdapterInfo(&query))
    {
        identifier->vendor_id = ids.DeviceIds.VendorID;
        identifier->device_id = ids.DeviceIds.DeviceID;
        identifier->subsystem_id = ids.DeviceIds.SubVendorID | (ids.DeviceIds.SubSystemID << 16);
        identifier->revision = ids.DeviceIds.RevisionID;
    }

    if (identifier->description_size && (description = calloc(1, sizeof(*description))))
    {
        query.Type = KMTQAITYPE_DRIVER_DESCRIPTION;
        query.pPrivateDriverData = description;
        query.PrivateDriverDataSize = sizeof(*description);
        if (!D3DKMTQueryAdapterInfo(&query) && description->DriverDescription[0])
        {
            WideCharToMultiByte(CP_ACP, 0, description->DriverDescription, -1, identifier->description,
                    identifier->description_size, NULL, NULL);
            identifier->description[identifier->description_size - 1] = 0;
        }
        else
        {
            query.Type = KMTQAITYPE_ADAPTERREGISTRYINFO;
            query.pPrivateDriverData = &registry;
            query.PrivateDriverDataSize = sizeof(registry);
            if (!D3DKMTQueryAdapterInfo(&query) && registry.AdapterString[0])
            {
                WideCharToMultiByte(CP_ACP, 0, registry.AdapterString, -1, identifier->description,
                        identifier->description_size, NULL, NULL);
                identifier->description[identifier->description_size - 1] = 0;
            }
        }
        free(description);
    }

    umd.Version = KMTUMDVERSION_DX9;
    query.Type = KMTQAITYPE_UMDRIVERNAME;
    query.pPrivateDriverData = &umd;
    query.PrivateDriverDataSize = sizeof(umd);
    if (identifier->driver_size && !D3DKMTQueryAdapterInfo(&query) && umd.UmdFileName[0])
    {
        umd.UmdFileName[ARRAYSIZE(umd.UmdFileName) - 1] = 0;
        name = wcsrchr(umd.UmdFileName, '\\');
        name = name ? name + 1 : umd.UmdFileName;
        WideCharToMultiByte(CP_ACP, 0, name, -1, identifier->driver, identifier->driver_size, NULL, NULL);
        identifier->driver[identifier->driver_size - 1] = 0;
    }

    query.Type = KMTQAITYPE_UMD_DRIVER_VERSION;
    query.pPrivateDriverData = &version;
    query.PrivateDriverDataSize = sizeof(version);
    if (!D3DKMTQueryAdapterInfo(&query) && version.QuadPart)
        identifier->driver_version = version;
}
