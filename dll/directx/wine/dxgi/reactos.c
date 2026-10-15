/* ReactOS-specific DXGI integration. */

#ifdef __REACTOS__

#include "dxgi_private.h"

#include <d3dkmthk.h>
#include <reactos/rddm/rxgkadvcolor.h>

#ifndef NT_SUCCESS
#define NT_SUCCESS(status) ((NTSTATUS)(status) >= 0)
#endif

WINE_DEFAULT_DEBUG_CHANNEL(dxgi);

HRESULT dxgi_get_wddm_output_index(struct wined3d_adapter *adapter, const LUID *luid, UINT output_idx,
        UINT *wined3d_output_idx)
{
    struct wined3d_adapter_identifier identifier = {0};
    struct wined3d_output_desc desc;
    D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME open_adapter;
    D3DKMT_CLOSEADAPTER close_adapter;
    UINT i, count;
    HRESULT hr;

    wined3d_mutex_lock();
    hr = wined3d_adapter_get_identifier(adapter, 0, &identifier);
    count = wined3d_adapter_get_output_count(adapter);
    wined3d_mutex_unlock();
    if (FAILED(hr))
        return hr;
    if (luid)
        identifier.adapter_luid = *luid;

    /* WineD3D uses desktop outputs to host its GL contexts, including when
     * the ICD renders on a different adapter. DXGI must enumerate only the
     * outputs owned by this adapter, or clients import desktop allocations
     * into the wrong device. Keep the underlying WineD3D ordinal for creation. */
    for (i = 0; i < count; ++i)
    {
        wined3d_mutex_lock();
        hr = wined3d_output_get_desc(wined3d_adapter_get_output(adapter, i), &desc);
        wined3d_mutex_unlock();
        if (FAILED(hr))
            continue;
        memset(&open_adapter, 0, sizeof(open_adapter));
        lstrcpynW(open_adapter.DeviceName, desc.device_name, ARRAY_SIZE(open_adapter.DeviceName));
        if (!NT_SUCCESS(D3DKMTOpenAdapterFromGdiDisplayName(&open_adapter)))
            continue;
        close_adapter.hAdapter = open_adapter.hAdapter;
        D3DKMTCloseAdapter(&close_adapter);
        if (open_adapter.AdapterLuid.LowPart != identifier.adapter_luid.LowPart
                || open_adapter.AdapterLuid.HighPart != identifier.adapter_luid.HighPart)
            continue;
        if (!output_idx--)
        {
            *wined3d_output_idx = i;
            return S_OK;
        }
    }
    return DXGI_ERROR_NOT_FOUND;
}

HRESULT dxgi_get_wddm_adapter_desc(LUID luid, DXGI_ADAPTER_DESC3 *desc)
{
    D3DKMT_OPENADAPTERFROMLUID open_adapter = {0};
    D3DKMT_CLOSEADAPTER close_adapter;
    D3DKMT_QUERYADAPTERINFO query = {0};
    D3DKMT_QUERY_DEVICE_IDS ids = {0};
    D3DKMT_ADAPTERREGISTRYINFO registry = {0};
    D3DKMT_SEGMENTSIZEINFO segments = {0};
    D3DKMT_ADAPTERTYPE type = {0};
    HRESULT hr = E_FAIL;

    open_adapter.AdapterLuid = luid;
    if (!NT_SUCCESS(D3DKMTOpenAdapterFromLuid(&open_adapter)))
        return E_FAIL;
    query.hAdapter = open_adapter.hAdapter;
    query.Type = KMTQAITYPE_ADAPTERTYPE;
    query.pPrivateDriverData = &type;
    query.PrivateDriverDataSize = sizeof(type);
    if (!NT_SUCCESS(D3DKMTQueryAdapterInfo(&query)))
        goto done;
    query.Type = KMTQAITYPE_PHYSICALADAPTERDEVICEIDS;
    query.pPrivateDriverData = &ids;
    query.PrivateDriverDataSize = sizeof(ids);
    if (NT_SUCCESS(D3DKMTQueryAdapterInfo(&query)))
    {
        desc->VendorId = ids.DeviceIds.VendorID;
        desc->DeviceId = ids.DeviceIds.DeviceID;
        desc->SubSysId = ids.DeviceIds.SubVendorID | (ids.DeviceIds.SubSystemID << 16);
        desc->Revision = ids.DeviceIds.RevisionID;
    }
    else if (type.SoftwareDevice)
    {
        desc->VendorId = 0x1414;
        desc->DeviceId = 0x008c;
        desc->SubSysId = 0;
        desc->Revision = 0;
    }
    desc->Flags = type.SoftwareDevice ? DXGI_ADAPTER_FLAG3_SOFTWARE : 0;
    query.Type = KMTQAITYPE_ADAPTERREGISTRYINFO;
    query.pPrivateDriverData = &registry;
    query.PrivateDriverDataSize = sizeof(registry);
    if (NT_SUCCESS(D3DKMTQueryAdapterInfo(&query)) && registry.AdapterString[0])
        lstrcpynW(desc->Description, registry.AdapterString, ARRAY_SIZE(desc->Description));
    query.Type = KMTQAITYPE_GETSEGMENTSIZE;
    query.pPrivateDriverData = &segments;
    query.PrivateDriverDataSize = sizeof(segments);
    if (NT_SUCCESS(D3DKMTQueryAdapterInfo(&query))
            && (segments.DedicatedVideoMemorySize || segments.DedicatedSystemMemorySize
            || segments.SharedSystemMemorySize))
    {
        desc->DedicatedVideoMemory = segments.DedicatedVideoMemorySize;
        desc->DedicatedSystemMemory = segments.DedicatedSystemMemorySize;
        desc->SharedSystemMemory = segments.SharedSystemMemorySize;
    }
    hr = S_OK;
done:
    close_adapter.hAdapter = open_adapter.hAdapter;
    D3DKMTCloseAdapter(&close_adapter);
    return hr;
}

BOOL dxgi_wddm_adapter_has_d3d11_umd(LUID luid)
{
    D3DKMT_OPENADAPTERFROMLUID open_adapter = {0};
    D3DKMT_CLOSEADAPTER close_adapter;
    D3DKMT_QUERYADAPTERINFO query = {0};
    D3DKMT_UMDFILENAMEINFO name = {0};
    BOOL found;

    open_adapter.AdapterLuid = luid;
    if (!NT_SUCCESS(D3DKMTOpenAdapterFromLuid(&open_adapter)))
        return FALSE;
    name.Version = KMTUMDVERSION_DX11;
    query.hAdapter = open_adapter.hAdapter;
    query.Type = KMTQAITYPE_UMDRIVERNAME;
    query.pPrivateDriverData = &name;
    query.PrivateDriverDataSize = sizeof(name);
    found = NT_SUCCESS(D3DKMTQueryAdapterInfo(&query)) && name.UmdFileName[0];
    close_adapter.hAdapter = open_adapter.hAdapter;
    D3DKMTCloseAdapter(&close_adapter);
    return found;
}

static unsigned int dxgi_get_wddm_adapter_priority(const D3DKMT_ADAPTERINFO *adapter)
{
    D3DKMT_QUERYADAPTERINFO query_info = {0};
    D3DKMT_ADAPTERTYPE adapter_type = {0};

    query_info.hAdapter = adapter->hAdapter;
    query_info.Type = KMTQAITYPE_ADAPTERTYPE;
    query_info.pPrivateDriverData = &adapter_type;
    query_info.PrivateDriverDataSize = sizeof(adapter_type);
    if (!NT_SUCCESS(D3DKMTQueryAdapterInfo(&query_info)))
        return 1;

    /* A software display fallback must not hide a separately started hardware
     * render adapter from clients which select the first DXGI adapter. */
    if (adapter_type.SoftwareDevice)
        return 2;
    if (adapter_type.RenderSupported)
        return 0;
    return 1;
}

DXGI_GPU_PREFERENCE dxgi_get_user_gpu_preference(void)
{
    WCHAR path[MAX_PATH], setting[256];
    DWORD size = sizeof(setting) - sizeof(WCHAR), type = 0, length;
    const WCHAR *value;
    HKEY key;
    LONG ret;

    length = GetModuleFileNameW(NULL, path, ARRAY_SIZE(path));
    if (!length || length >= ARRAY_SIZE(path))
        return DXGI_GPU_PREFERENCE_UNSPECIFIED;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\DirectX\\UserGpuPreferences", 0,
            KEY_QUERY_VALUE, &key))
        return DXGI_GPU_PREFERENCE_UNSPECIFIED;
    memset(setting, 0, sizeof(setting));
    ret = RegQueryValueExW(key, path, NULL, &type, (BYTE *)setting, &size);
    RegCloseKey(key);
    if (ret || type != REG_SZ || !(value = wcsstr(setting, L"GpuPreference=")))
        return DXGI_GPU_PREFERENCE_UNSPECIFIED;
    switch (value[14])
    {
        case '1': return DXGI_GPU_PREFERENCE_MINIMUM_POWER;
        case '2': return DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE;
        default: return DXGI_GPU_PREFERENCE_UNSPECIFIED;
    }
}

struct dxgi_wddm_candidate
{
    unsigned int index;
    unsigned int priority;
    BOOL integrated;
    ULONGLONG dedicated_memory;
};

static void dxgi_describe_wddm_candidate(const D3DKMT_ADAPTERINFO *adapter, struct dxgi_wddm_candidate *candidate)
{
    D3DKMT_QUERYADAPTERINFO query = {0};
    D3DKMT_SEGMENTSIZEINFO segments = {0};
    D3DKMT_ADAPTERTYPE type = {0};

    candidate->priority = dxgi_get_wddm_adapter_priority(adapter);
    query.hAdapter = adapter->hAdapter;
    query.Type = KMTQAITYPE_ADAPTERTYPE;
    query.pPrivateDriverData = &type;
    query.PrivateDriverDataSize = sizeof(type);
    if (NT_SUCCESS(D3DKMTQueryAdapterInfo(&query)))
        candidate->integrated = type.HybridIntegrated;
    query.Type = KMTQAITYPE_GETSEGMENTSIZE;
    query.pPrivateDriverData = &segments;
    query.PrivateDriverDataSize = sizeof(segments);
    if (NT_SUCCESS(D3DKMTQueryAdapterInfo(&query)))
        candidate->dedicated_memory = segments.DedicatedVideoMemorySize;
}

static BOOL dxgi_wddm_candidate_precedes(const struct dxgi_wddm_candidate *a, const struct dxgi_wddm_candidate *b,
        DXGI_GPU_PREFERENCE preference)
{
    if (a->priority != b->priority)
        return a->priority < b->priority;
    if (a->priority || preference == DXGI_GPU_PREFERENCE_UNSPECIFIED)
        return a->index < b->index;
    if (a->integrated != b->integrated)
        return preference == DXGI_GPU_PREFERENCE_MINIMUM_POWER ? a->integrated : b->integrated;
    if (a->dedicated_memory != b->dedicated_memory)
        return preference == DXGI_GPU_PREFERENCE_MINIMUM_POWER ? a->dedicated_memory < b->dedicated_memory
                : a->dedicated_memory > b->dedicated_memory;
    return a->index < b->index;
}

/* Keep D3DKMT as the authoritative adapter set while returning complete Wine
 * DXGI adapter objects.  Preserve order within capability classes, with
 * hardware render adapters before display-only and software fallbacks. */
HRESULT dxgi_get_wddm_adapter(struct wined3d *wined3d, UINT wddm_adapter_idx, DXGI_GPU_PREFERENCE preference,
        UINT *wined3d_adapter_idx, LUID *luid, BOOL *foreign)
{
    struct wined3d_adapter_identifier identifier = {0};
    D3DKMT_ENUMADAPTERS2 enum_adapters = {0};
    struct dxgi_wddm_candidate *candidates = NULL;
    D3DKMT_ADAPTERINFO *adapters;
    unsigned int adapter_count;
    unsigned int capacity;
    unsigned int selected_idx;
    unsigned int i, j;
    NTSTATUS status;
    HRESULT hr;

    status = D3DKMTEnumAdapters2(&enum_adapters);
    if (!NT_SUCCESS(status))
    {
        WARN("D3DKMTEnumAdapters2 count query failed, status %#lx.\n", status);
        return DXGI_ERROR_NOT_FOUND;
    }

    if (wddm_adapter_idx >= enum_adapters.NumAdapters)
        return DXGI_ERROR_NOT_FOUND;

    capacity = enum_adapters.NumAdapters;
    if (!(adapters = calloc(capacity, sizeof(*adapters))))
        return E_OUTOFMEMORY;

    enum_adapters.pAdapters = adapters;
    status = D3DKMTEnumAdapters2(&enum_adapters);
    if (!NT_SUCCESS(status))
    {
        WARN("D3DKMTEnumAdapters2 enumeration failed, status %#lx.\n", status);
        hr = DXGI_ERROR_NOT_FOUND;
        goto done;
    }

    if (enum_adapters.NumAdapters > capacity || wddm_adapter_idx >= enum_adapters.NumAdapters)
    {
        hr = DXGI_ERROR_NOT_FOUND;
        goto done;
    }

    if (!(candidates = calloc(enum_adapters.NumAdapters, sizeof(*candidates))))
    {
        hr = E_OUTOFMEMORY;
        goto done;
    }
    for (i = 0; i < enum_adapters.NumAdapters; ++i)
    {
        struct dxgi_wddm_candidate candidate = {0};

        candidate.index = i;
        dxgi_describe_wddm_candidate(&adapters[i], &candidate);
        for (j = i; j && dxgi_wddm_candidate_precedes(&candidate, &candidates[j - 1], preference); --j)
            candidates[j] = candidates[j - 1];
        candidates[j] = candidate;
    }
    selected_idx = candidates[wddm_adapter_idx].index;

    wined3d_mutex_lock();
    adapter_count = wined3d_get_adapter_count(wined3d);
    wined3d_mutex_unlock();

    *luid = adapters[selected_idx].AdapterLuid;
    *foreign = TRUE;
    *wined3d_adapter_idx = 0;
    hr = adapter_count ? S_OK : DXGI_ERROR_NOT_FOUND;
    for (i = 0; i < adapter_count; ++i)
    {
        if (FAILED(wined3d_adapter_get_identifier(wined3d_get_adapter(wined3d, i), 0, &identifier)))
            continue;

        if (identifier.adapter_luid.LowPart == adapters[selected_idx].AdapterLuid.LowPart && identifier.adapter_luid.HighPart == adapters[selected_idx].AdapterLuid.HighPart)
        {
            *wined3d_adapter_idx = i;
            *foreign = FALSE;
            break;
        }
    }

done:
    for (i = 0; i < enum_adapters.NumAdapters && i < capacity; ++i)
    {
        D3DKMT_CLOSEADAPTER close_adapter;

        if (!adapters[i].hAdapter)
            continue;
        close_adapter.hAdapter = adapters[i].hAdapter;
        D3DKMTCloseAdapter(&close_adapter);
    }
    free(candidates);
    free(adapters);
    return hr;
}

/* The colour description of a display output, from dxgkrnl: the monitor's
 * primaries and luminance from its EDID, and the colour space and depth of
 * the signal it is driven with.  Left as SDR defaults when unknown. */
void dxgi_get_wddm_output_color(const WCHAR *device_name, DXGI_OUTPUT_DESC1 *desc)
{
    D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME open_adapter;
    D3DKMT_CLOSEADAPTER close_adapter;
    RXGK_ADVANCED_COLOR_PACKET color;
    D3DKMT_ESCAPE escape;
    NTSTATUS status;

    memset(&open_adapter, 0, sizeof(open_adapter));
    lstrcpynW(open_adapter.DeviceName, device_name, ARRAY_SIZE(open_adapter.DeviceName));
    if (!NT_SUCCESS(D3DKMTOpenAdapterFromGdiDisplayName(&open_adapter)))
        return;

    memset(&color, 0, sizeof(color));
    color.Size = sizeof(color);
    color.Version = RXGK_ADVANCED_COLOR_VERSION_1;
    color.Operation = RXGK_ADVANCED_COLOR_GET;
    color.VidPnSourceId = open_adapter.VidPnSourceId;
    memset(&escape, 0, sizeof(escape));
    escape.hAdapter = open_adapter.hAdapter;
    escape.Type = (D3DKMT_ESCAPETYPE)RXGK_ESCAPE_ADVANCED_COLOR;
    escape.pPrivateDriverData = &color;
    escape.PrivateDriverDataSize = sizeof(color);
    status = D3DKMTEscape(&escape);

    close_adapter.hAdapter = open_adapter.hAdapter;
    D3DKMTCloseAdapter(&close_adapter);
    if (!NT_SUCCESS(status))
    {
        WARN("No colour description for %s, status %#lx.\n", debugstr_w(device_name), status);
        return;
    }

    desc->BitsPerColor = color.BitsPerColorChannel;
    desc->ColorSpace = (color.Flags & RXGK_ADVANCED_COLOR_ENABLED)
            ? DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 : DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
    /* Chromaticity in 1/1024, luminance in 1/10000 nit. */
    desc->RedPrimary[0] = color.RedPrimary[0] / 1024.0f;
    desc->RedPrimary[1] = color.RedPrimary[1] / 1024.0f;
    desc->GreenPrimary[0] = color.GreenPrimary[0] / 1024.0f;
    desc->GreenPrimary[1] = color.GreenPrimary[1] / 1024.0f;
    desc->BluePrimary[0] = color.BluePrimary[0] / 1024.0f;
    desc->BluePrimary[1] = color.BluePrimary[1] / 1024.0f;
    desc->WhitePoint[0] = color.WhitePoint[0] / 1024.0f;
    desc->WhitePoint[1] = color.WhitePoint[1] / 1024.0f;
    if (color.Flags & RXGK_ADVANCED_COLOR_EDID_LUMINANCE)
    {
        desc->MinLuminance = color.MinLuminance / 10000.0f;
        desc->MaxLuminance = color.MaxLuminance / 10000.0f;
        desc->MaxFullFrameLuminance = color.MaxFullFrameLuminance / 10000.0f;
    }
}

static LONG dxgi_adapter_removal_support;

HRESULT WINAPI DXGIDeclareAdapterRemovalSupport(void)
{
    if (InterlockedExchange(&dxgi_adapter_removal_support, 1))
        return DXGI_ERROR_ALREADY_EXISTS;
    return S_OK;
}

#endif /* __REACTOS__ */
