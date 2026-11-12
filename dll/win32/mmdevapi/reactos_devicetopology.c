/*
 * PROJECT:     LiberNT Core Audio
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     DeviceTopology API over audio endpoints and their KS filters
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

#define COBJMACROS
#include "windef.h"
#include "winbase.h"
#include "winioctl.h"
#include "winreg.h"
#include "ole2.h"
#include "mmdeviceapi.h"
#include "audioclient.h"
#include "endpointvolume.h"
#include "audiopolicy.h"
#include "devicetopology.h"
#include "setupapi.h"
#include "cfgmgr32.h"
#include "ks.h"
#include "ksmedia.h"
#include "wine/debug.h"

#include "mmdevapi_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(mmdevapi);

#define FILTER_ID_PREFIX L"{2}."
#define FILTER_ID_PREFIX_LENGTH 4
#define PART_KIND_CONNECTOR 1
#define PART_KIND_SUBUNIT 2
#define PART_ID(kind, index) (((UINT)(kind) << 16) | (index))
#define PART_KIND(id) ((id) >> 16)
#define PART_INDEX(id) ((id) & 0xffff)
#define KS_STRING_BYTES 1024
#define KS_PHYSICAL_BYTES 4096

static const GUID audio_category = {STATIC_KSCATEGORY_AUDIO};
static const GUID pin_set = {STATIC_KSPROPSETID_Pin};
static const GUID topology_set = {STATIC_KSPROPSETID_Topology};
static const GUID speaker_type = {STATIC_KSNODETYPE_SPEAKER};
static const GUID microphone_type = {STATIC_KSNODETYPE_MICROPHONE};
static const GUID line_connector_type = {STATIC_KSNODETYPE_LINE_CONNECTOR};
static const GUID spdif_type = {STATIC_KSNODETYPE_SPDIF_INTERFACE};

struct filter_pin
{
    DataFlow flow;
    ConnectorType type;
    GUID category;
    WCHAR *name;
    WCHAR *peer_id;
    ULONG peer_pin;
};

struct filter_node
{
    GUID type;
    WCHAR *name;
};

struct topology
{
    IDeviceTopology IDeviceTopology_iface;
    LONG ref;
    WCHAR *id;
    BOOL endpoint;

    DataFlow endpoint_flow;
    WCHAR *peer_id;
    ULONG peer_pin;
    struct topology *peer;

    WCHAR *path;
    UINT pin_count;
    struct filter_pin *pins;
    UINT node_count;
    struct filter_node *nodes;
    UINT connection_count;
    KSTOPOLOGY_CONNECTION *connections;
};

struct part
{
    IPart IPart_iface;
    IConnector IConnector_iface;
    ISubUnit ISubUnit_iface;
    LONG ref;
    struct topology *topology;
    UINT local_id;
};

struct parts_list
{
    IPartsList IPartsList_iface;
    LONG ref;
    UINT count;
    IPart *parts[];
};

struct filter_device
{
    IMMDevice IMMDevice_iface;
    LONG ref;
    WCHAR *id;
};

static const IDeviceTopologyVtbl topology_vtbl;
static const IPartVtbl part_vtbl;
static const IConnectorVtbl connector_vtbl;
static const ISubUnitVtbl subunit_vtbl;
static const IPartsListVtbl parts_list_vtbl;
static const IMMDeviceVtbl filter_device_vtbl;

static HRESULT filter_topology_create(const WCHAR *id, struct topology **out);

static inline struct topology *impl_from_IDeviceTopology(IDeviceTopology *iface)
{
    return CONTAINING_RECORD(iface, struct topology, IDeviceTopology_iface);
}

static inline struct part *impl_from_IPart(IPart *iface)
{
    return CONTAINING_RECORD(iface, struct part, IPart_iface);
}

static inline struct part *impl_from_IConnector(IConnector *iface)
{
    return CONTAINING_RECORD(iface, struct part, IConnector_iface);
}

static inline struct part *impl_from_ISubUnit(ISubUnit *iface)
{
    return CONTAINING_RECORD(iface, struct part, ISubUnit_iface);
}

static inline struct parts_list *impl_from_IPartsList(IPartsList *iface)
{
    return CONTAINING_RECORD(iface, struct parts_list, IPartsList_iface);
}

static inline struct filter_device *impl_from_IMMDevice(IMMDevice *iface)
{
    return CONTAINING_RECORD(iface, struct filter_device, IMMDevice_iface);
}

static WCHAR *heap_wcsdup(const WCHAR *string)
{
    size_t size = (wcslen(string) + 1) * sizeof(WCHAR);
    WCHAR *copy = malloc(size);

    if (copy)
        memcpy(copy, string, size);
    return copy;
}

static HRESULT co_wcsdup(const WCHAR *string, WCHAR **out)
{
    size_t size = (wcslen(string) + 1) * sizeof(WCHAR);

    if (!(*out = CoTaskMemAlloc(size)))
        return E_OUTOFMEMORY;
    memcpy(*out, string, size);
    return S_OK;
}

static HRESULT format_part_id(const WCHAR *device_id, UINT local_id, WCHAR **out)
{
    size_t length = wcslen(device_id) + 10;

    if (!(*out = CoTaskMemAlloc((length + 1) * sizeof(WCHAR))))
        return E_OUTOFMEMORY;
    swprintf(*out, length + 1, L"%s/%08x", device_id, local_id);
    return S_OK;
}

static const WCHAR *skip_path_prefix(const WCHAR *path)
{
    if ((path[0] == '\\' || path[0] == '/') && (path[1] == '\\' || path[1] == '?') &&
        path[2] == '?' && path[3] == '\\')
        return path + 4;
    return path;
}

/* Interfaces of one KS filter differ only in their class GUID segment and in case. */
static BOOL same_filter_path(const WCHAR *left, const WCHAR *right)
{
    const WCHAR *left_class, *right_class, *left_ref, *right_ref;

    left = skip_path_prefix(left);
    right = skip_path_prefix(right);
    left_class = wcsstr(left, L"#{");
    right_class = wcsstr(right, L"#{");
    if (!left_class || !right_class)
        return !_wcsicmp(left, right);
    if (left_class - left != right_class - right || _wcsnicmp(left, right, left_class - left))
        return FALSE;
    left_ref = wcschr(left_class, '}');
    right_ref = wcschr(right_class, '}');
    if (!left_ref || !right_ref)
        return FALSE;
    return !_wcsicmp(left_ref + 1, right_ref + 1);
}

static WCHAR *filter_id_from_path(const WCHAR *path)
{
    SP_DEVICE_INTERFACE_DETAIL_DATA_W *detail = NULL;
    SP_DEVICE_INTERFACE_DATA data;
    HDEVINFO set;
    WCHAR *id = NULL;
    const WCHAR *found = NULL;
    DWORD index, size;

    set = SetupDiGetClassDevsW(&audio_category, NULL, NULL, DIGCF_DEVICEINTERFACE | DIGCF_PRESENT);
    if (set != INVALID_HANDLE_VALUE)
    {
        data.cbSize = sizeof(data);
        for (index = 0; !found && SetupDiEnumDeviceInterfaces(set, NULL, &audio_category, index, &data); ++index)
        {
            size = 0;
            SetupDiGetDeviceInterfaceDetailW(set, &data, NULL, 0, &size, NULL);
            if (!size)
                continue;
            free(detail);
            if (!(detail = malloc(size)))
                break;
            detail->cbSize = sizeof(*detail);
            if (SetupDiGetDeviceInterfaceDetailW(set, &data, detail, size, NULL, NULL) &&
                same_filter_path(detail->DevicePath, path))
            {
                found = detail->DevicePath;
            }
        }
        SetupDiDestroyDeviceInfoList(set);
    }

    if (!found)
    {
        WARN("No KSCATEGORY_AUDIO interface for %s.\n", debugstr_w(path));
        found = path;
    }

    size = (DWORD)(FILTER_ID_PREFIX_LENGTH + 4 + wcslen(skip_path_prefix(found)) + 1);
    if ((id = malloc(size * sizeof(WCHAR))))
        swprintf(id, size, L"%s\\\\?\\%s", FILTER_ID_PREFIX, skip_path_prefix(found));
    free(detail);
    return id;
}

static BOOL ks_property(HANDLE filter, void *request, ULONG request_size, void *data, ULONG data_size,
                        ULONG *returned)
{
    DWORD bytes = 0;
    BOOL ret;

    ret = DeviceIoControl(filter, IOCTL_KS_PROPERTY, request, request_size, data, data_size, &bytes, NULL);
    if (returned)
        *returned = bytes;
    return ret;
}

static BOOL ks_pin_property(HANDLE filter, ULONG id, ULONG pin, void *data, ULONG data_size, ULONG *returned)
{
    KSP_PIN request;

    ZeroMemory(&request, sizeof(request));
    request.Property.Set = pin_set;
    request.Property.Id = id;
    request.Property.Flags = KSPROPERTY_TYPE_GET;
    request.PinId = pin;
    return ks_property(filter, &request, sizeof(request), data, data_size, returned);
}

static KSMULTIPLE_ITEM *ks_topology_list(HANDLE filter, ULONG id, ULONG item_size)
{
    KSMULTIPLE_ITEM *item = NULL;
    KSPROPERTY request;
    ULONG size = 0;

    request.Set = topology_set;
    request.Id = id;
    request.Flags = KSPROPERTY_TYPE_GET;
    if (!ks_property(filter, &request, sizeof(request), NULL, 0, &size) && GetLastError() != ERROR_MORE_DATA)
        size = 0;
    if (size < sizeof(KSMULTIPLE_ITEM))
        size = sizeof(KSMULTIPLE_ITEM) + 256 * item_size;
    if (!(item = malloc(size)))
        return NULL;
    if (!ks_property(filter, &request, sizeof(request), item, size, &size) || size < sizeof(KSMULTIPLE_ITEM) ||
        item->Size > size || item->Count > (item->Size - sizeof(KSMULTIPLE_ITEM)) / item_size)
    {
        WARN("KSPROPERTY_TOPOLOGY %u failed, error %lu.\n", id, GetLastError());
        free(item);
        return NULL;
    }
    return item;
}

static WCHAR *ks_string(BOOL ret, WCHAR *buffer, ULONG returned)
{
    if (!ret || returned < sizeof(WCHAR))
        buffer[0] = 0;
    else
        buffer[min(returned, KS_STRING_BYTES - sizeof(WCHAR)) / sizeof(WCHAR)] = 0;
    return heap_wcsdup(buffer);
}

static ConnectorType pin_connector_type(KSPIN_COMMUNICATION communication, BOOL physical, const GUID *category)
{
    if (communication == KSPIN_COMMUNICATION_SINK || communication == KSPIN_COMMUNICATION_SOURCE ||
        communication == KSPIN_COMMUNICATION_BOTH)
        return Software_IO;
    if (physical)
        return Software_Fixed;
    if (IsEqualGUID(category, &speaker_type) || IsEqualGUID(category, &microphone_type) ||
        IsEqualGUID(category, &line_connector_type) || IsEqualGUID(category, &spdif_type))
        return Physical_External;
    return Physical_Internal;
}

static HRESULT load_filter_pin(struct topology *topology, HANDLE filter, ULONG index, BYTE *buffer)
{
    struct filter_pin *pin = &topology->pins[index];
    KSPIN_PHYSICALCONNECTION *physical = (KSPIN_PHYSICALCONNECTION *)buffer;
    KSPIN_COMMUNICATION communication = KSPIN_COMMUNICATION_NONE;
    KSPIN_DATAFLOW flow = KSPIN_DATAFLOW_IN;
    ULONG returned;
    BOOL has_physical, ret;

    if (!ks_pin_property(filter, KSPROPERTY_PIN_DATAFLOW, index, &flow, sizeof(flow), NULL))
        WARN("KSPROPERTY_PIN_DATAFLOW %lu failed, error %lu.\n", index, GetLastError());
    if (!ks_pin_property(filter, KSPROPERTY_PIN_COMMUNICATION, index, &communication, sizeof(communication), NULL))
        WARN("KSPROPERTY_PIN_COMMUNICATION %lu failed, error %lu.\n", index, GetLastError());
    if (!ks_pin_property(filter, KSPROPERTY_PIN_CATEGORY, index, &pin->category, sizeof(pin->category), NULL))
        pin->category = GUID_NULL;

    ZeroMemory(buffer, KS_STRING_BYTES);
    ret = ks_pin_property(filter, KSPROPERTY_PIN_NAME, index, buffer, KS_STRING_BYTES - sizeof(WCHAR), &returned);
    if (!(pin->name = ks_string(ret, (WCHAR *)buffer, returned)))
        return E_OUTOFMEMORY;

    ZeroMemory(buffer, KS_PHYSICAL_BYTES);
    has_physical = ks_pin_property(filter, KSPROPERTY_PIN_PHYSICALCONNECTION, index, physical,
                                   KS_PHYSICAL_BYTES - sizeof(WCHAR), &returned) &&
                   returned > FIELD_OFFSET(KSPIN_PHYSICALCONNECTION, SymbolicLinkName) &&
                   physical->SymbolicLinkName[0];
    if (has_physical)
    {
        if (!(pin->peer_id = filter_id_from_path(physical->SymbolicLinkName)))
            return E_OUTOFMEMORY;
        pin->peer_pin = physical->Pin;
    }

    pin->flow = flow == KSPIN_DATAFLOW_IN ? In : Out;
    pin->type = pin_connector_type(communication, has_physical, &pin->category);
    return S_OK;
}

static HRESULT load_filter(struct topology *topology)
{
    KSMULTIPLE_ITEM *nodes, *connections;
    KSPROPERTY request;
    KSP_NODE node_request;
    HANDLE filter;
    BYTE *buffer;
    ULONG count = 0, returned, i;
    HRESULT hr = S_OK;
    BOOL ret;

    filter = CreateFileW(topology->path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL, NULL);
    if (filter == INVALID_HANDLE_VALUE)
    {
        WARN("Opening %s failed, error %lu.\n", debugstr_w(topology->path), GetLastError());
        return HRESULT_FROM_WIN32(GetLastError());
    }

    request.Set = pin_set;
    request.Id = KSPROPERTY_PIN_CTYPES;
    request.Flags = KSPROPERTY_TYPE_GET;
    if (!ks_property(filter, &request, sizeof(request), &count, sizeof(count), NULL) || count > 0xffff)
    {
        WARN("KSPROPERTY_PIN_CTYPES failed, error %lu.\n", GetLastError());
        CloseHandle(filter);
        return E_FAIL;
    }

    if (!(buffer = malloc(KS_PHYSICAL_BYTES)) ||
        (count && !(topology->pins = calloc(count, sizeof(*topology->pins)))))
    {
        free(buffer);
        CloseHandle(filter);
        return E_OUTOFMEMORY;
    }
    topology->pin_count = count;
    for (i = 0; SUCCEEDED(hr) && i < count; ++i)
        hr = load_filter_pin(topology, filter, i, buffer);

    nodes = ks_topology_list(filter, KSPROPERTY_TOPOLOGY_NODES, sizeof(GUID));
    if (SUCCEEDED(hr) && nodes && nodes->Count <= 0xffff && nodes->Count &&
        !(topology->nodes = calloc(nodes->Count, sizeof(*topology->nodes))))
        hr = E_OUTOFMEMORY;
    if (SUCCEEDED(hr) && nodes && nodes->Count <= 0xffff)
    {
        topology->node_count = nodes->Count;
        for (i = 0; SUCCEEDED(hr) && i < nodes->Count; ++i)
        {
            topology->nodes[i].type = ((const GUID *)(nodes + 1))[i];
            ZeroMemory(&node_request, sizeof(node_request));
            node_request.Property.Set = topology_set;
            node_request.Property.Id = KSPROPERTY_TOPOLOGY_NAME;
            node_request.Property.Flags = KSPROPERTY_TYPE_GET;
            node_request.NodeId = i;
            ZeroMemory(buffer, KS_STRING_BYTES);
            ret = ks_property(filter, &node_request, sizeof(node_request), buffer,
                              KS_STRING_BYTES - sizeof(WCHAR), &returned);
            if (!(topology->nodes[i].name = ks_string(ret, (WCHAR *)buffer, returned)))
                hr = E_OUTOFMEMORY;
        }
    }
    free(nodes);

    connections = ks_topology_list(filter, KSPROPERTY_TOPOLOGY_CONNECTIONS, sizeof(KSTOPOLOGY_CONNECTION));
    if (SUCCEEDED(hr) && connections && connections->Count)
    {
        if (!(topology->connections = malloc(connections->Count * sizeof(KSTOPOLOGY_CONNECTION))))
            hr = E_OUTOFMEMORY;
        else
        {
            memcpy(topology->connections, connections + 1, connections->Count * sizeof(KSTOPOLOGY_CONNECTION));
            topology->connection_count = connections->Count;
        }
    }
    free(connections);

    free(buffer);
    CloseHandle(filter);
    return hr;
}

static void topology_free(struct topology *topology)
{
    UINT i;

    if (topology->peer)
        IDeviceTopology_Release(&topology->peer->IDeviceTopology_iface);
    for (i = 0; i < topology->pin_count; ++i)
    {
        free(topology->pins[i].name);
        free(topology->pins[i].peer_id);
    }
    for (i = 0; i < topology->node_count; ++i)
        free(topology->nodes[i].name);
    free(topology->pins);
    free(topology->nodes);
    free(topology->connections);
    free(topology->peer_id);
    free(topology->path);
    free(topology->id);
    free(topology);
}

static struct topology *topology_alloc(void)
{
    struct topology *topology = calloc(1, sizeof(*topology));

    if (topology)
    {
        topology->IDeviceTopology_iface.lpVtbl = &topology_vtbl;
        topology->ref = 1;
    }
    return topology;
}

static HRESULT filter_topology_create(const WCHAR *id, struct topology **out)
{
    struct topology *topology;
    HRESULT hr;

    assert(id && !wcsncmp(id, FILTER_ID_PREFIX, FILTER_ID_PREFIX_LENGTH));
    *out = NULL;
    if (!(topology = topology_alloc()))
        return E_OUTOFMEMORY;
    if (!(topology->id = heap_wcsdup(id)) || !(topology->path = heap_wcsdup(id + FILTER_ID_PREFIX_LENGTH)))
    {
        topology_free(topology);
        return E_OUTOFMEMORY;
    }
    if (FAILED(hr = load_filter(topology)))
    {
        topology_free(topology);
        return hr;
    }
    *out = topology;
    return S_OK;
}

HRESULT reactos_device_topology_create(MMDevice *device, IDeviceTopology **out)
{
    struct topology *topology;
    WCHAR *id = NULL, *path = NULL;
    ULONG pin;
    HRESULT hr;

    assert(device && out);
    *out = NULL;
    if (!(topology = topology_alloc()))
        return E_OUTOFMEMORY;

    topology->endpoint = TRUE;
    topology->endpoint_flow = device->flow == eCapture ? Out : In;
    if (FAILED(hr = IMMDevice_GetId(&device->IMMDevice_iface, &id)))
    {
        topology_free(topology);
        return hr;
    }
    topology->id = heap_wcsdup(id);
    CoTaskMemFree(id);
    if (!topology->id)
    {
        topology_free(topology);
        return E_OUTOFMEMORY;
    }

    hr = reactos_wave_connector(&device->devguid, &path, &pin);
    if (SUCCEEDED(hr))
    {
        topology->peer_id = filter_id_from_path(path);
        topology->peer_pin = pin;
        free(path);
        if (!topology->peer_id)
        {
            topology_free(topology);
            return E_OUTOFMEMORY;
        }
    }
    else
        WARN("Endpoint %s has no KS connector, hr %#lx.\n", debugstr_w(topology->id), hr);

    *out = &topology->IDeviceTopology_iface;
    return S_OK;
}

static UINT topology_connector_count(const struct topology *topology)
{
    return topology->endpoint ? 1 : topology->pin_count;
}

static struct topology *endpoint_peer(struct topology *topology)
{
    struct topology *peer;

    assert(topology->endpoint);
    if (!topology->peer_id)
        return NULL;
    if ((peer = topology->peer))
        return peer;
    if (FAILED(filter_topology_create(topology->peer_id, &peer)))
        return NULL;
    if (InterlockedCompareExchangePointer((void **)&topology->peer, peer, NULL))
    {
        IDeviceTopology_Release(&peer->IDeviceTopology_iface);
        peer = topology->peer;
    }
    if (topology->peer_pin >= peer->pin_count)
    {
        WARN("Connector pin %lu is outside %s.\n", topology->peer_pin, debugstr_w(peer->id));
        return NULL;
    }
    return peer;
}

static HRESULT part_create(struct topology *topology, UINT local_id, IPart **out)
{
    struct part *part;

    assert(PART_KIND(local_id) == PART_KIND_CONNECTOR || PART_KIND(local_id) == PART_KIND_SUBUNIT);
    assert(PART_KIND(local_id) != PART_KIND_CONNECTOR || PART_INDEX(local_id) < topology_connector_count(topology));
    assert(PART_KIND(local_id) != PART_KIND_SUBUNIT || PART_INDEX(local_id) < topology->node_count);

    if (!(part = calloc(1, sizeof(*part))))
        return E_OUTOFMEMORY;
    part->IPart_iface.lpVtbl = &part_vtbl;
    part->IConnector_iface.lpVtbl = &connector_vtbl;
    part->ISubUnit_iface.lpVtbl = &subunit_vtbl;
    part->ref = 1;
    part->topology = topology;
    part->local_id = local_id;
    IDeviceTopology_AddRef(&topology->IDeviceTopology_iface);
    *out = &part->IPart_iface;
    return S_OK;
}

static HRESULT get_connector(struct topology *topology, UINT index, IConnector **out)
{
    IPart *part;
    HRESULT hr;

    *out = NULL;
    if (FAILED(hr = part_create(topology, PART_ID(PART_KIND_CONNECTOR, index), &part)))
        return hr;
    *out = &impl_from_IPart(part)->IConnector_iface;
    return S_OK;
}

static HRESULT find_endpoint(const struct topology *topology, ULONG pin, MMDevice **out)
{
    IMMDeviceEnumerator *enumerator;
    IMMDeviceCollection *collection;
    IMMDevice *device;
    MMDevice *endpoint;
    WCHAR *path;
    ULONG connector_pin;
    UINT count = 0, i;
    HRESULT hr;

    *out = NULL;
    if (FAILED(hr = MMDevEnum_Create(&IID_IMMDeviceEnumerator, (void **)&enumerator)))
        return hr;
    hr = IMMDeviceEnumerator_EnumAudioEndpoints(enumerator, eAll, DEVICE_STATEMASK_ALL, &collection);
    IMMDeviceEnumerator_Release(enumerator);
    if (FAILED(hr))
        return hr;

    IMMDeviceCollection_GetCount(collection, &count);
    for (i = 0; !*out && i < count; ++i)
    {
        if (FAILED(IMMDeviceCollection_Item(collection, i, &device)))
            continue;
        endpoint = CONTAINING_RECORD(device, MMDevice, IMMDevice_iface);
        if (SUCCEEDED(reactos_wave_connector(&endpoint->devguid, &path, &connector_pin)))
        {
            if (connector_pin == pin && same_filter_path(path, topology->path))
            {
                *out = endpoint;
                IMMDevice_AddRef(device);
            }
            free(path);
        }
        IMMDevice_Release(device);
    }
    IMMDeviceCollection_Release(collection);
    return *out ? S_OK : E_NOTFOUND;
}

static HRESULT get_parts_list(struct topology *topology, UINT local_id, BOOL incoming, IPartsList **out)
{
    struct parts_list *list;
    UINT from, to, count = 0, i;
    HRESULT hr = S_OK;

    *out = NULL;
    if (topology->endpoint || !topology->connection_count)
        return E_NOTFOUND;

    if (!(list = malloc(offsetof(struct parts_list, parts[topology->connection_count]))))
        return E_OUTOFMEMORY;
    list->IPartsList_iface.lpVtbl = &parts_list_vtbl;
    list->ref = 1;
    list->count = 0;

    for (i = 0; SUCCEEDED(hr) && i < topology->connection_count; ++i)
    {
        const KSTOPOLOGY_CONNECTION *connection = &topology->connections[i];

        from = connection->FromNode == KSFILTER_NODE ? PART_ID(PART_KIND_CONNECTOR, connection->FromNodePin)
                                                     : PART_ID(PART_KIND_SUBUNIT, connection->FromNode);
        to = connection->ToNode == KSFILTER_NODE ? PART_ID(PART_KIND_CONNECTOR, connection->ToNodePin)
                                                 : PART_ID(PART_KIND_SUBUNIT, connection->ToNode);
        if (incoming ? to != local_id : from != local_id)
            continue;
        if (connection->FromNode == KSFILTER_NODE ? connection->FromNodePin >= topology->pin_count
                                                  : connection->FromNode >= topology->node_count)
            continue;
        if (connection->ToNode == KSFILTER_NODE ? connection->ToNodePin >= topology->pin_count
                                                : connection->ToNode >= topology->node_count)
            continue;
        hr = part_create(topology, incoming ? from : to, &list->parts[count]);
        if (SUCCEEDED(hr))
            list->count = ++count;
    }

    if (FAILED(hr) || !count)
    {
        IPartsList_Release(&list->IPartsList_iface);
        return FAILED(hr) ? hr : E_NOTFOUND;
    }
    *out = &list->IPartsList_iface;
    return S_OK;
}

static HRESULT WINAPI topology_QueryInterface(IDeviceTopology *iface, REFIID riid, void **ppv)
{
    struct topology *This = impl_from_IDeviceTopology(iface);

    TRACE("(%p)->(%s, %p)\n", This, debugstr_guid(riid), ppv);

    if (!ppv)
        return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IDeviceTopology))
    {
        *ppv = &This->IDeviceTopology_iface;
        IDeviceTopology_AddRef(iface);
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI topology_AddRef(IDeviceTopology *iface)
{
    struct topology *This = impl_from_IDeviceTopology(iface);
    ULONG ref = InterlockedIncrement(&This->ref);

    TRACE("(%p) new ref %lu\n", This, ref);
    assert(ref > 1);
    return ref;
}

static ULONG WINAPI topology_Release(IDeviceTopology *iface)
{
    struct topology *This = impl_from_IDeviceTopology(iface);
    ULONG ref = InterlockedDecrement(&This->ref);

    TRACE("(%p) new ref %lu\n", This, ref);
    assert(ref != ~0u);
    if (!ref)
        topology_free(This);
    return ref;
}

static HRESULT WINAPI topology_GetConnectorCount(IDeviceTopology *iface, UINT *count)
{
    struct topology *This = impl_from_IDeviceTopology(iface);

    TRACE("(%p)->(%p)\n", This, count);

    if (!count)
        return E_POINTER;
    *count = topology_connector_count(This);
    return S_OK;
}

static HRESULT WINAPI topology_GetConnector(IDeviceTopology *iface, UINT index, IConnector **connector)
{
    struct topology *This = impl_from_IDeviceTopology(iface);

    TRACE("(%p)->(%u, %p)\n", This, index, connector);

    if (!connector)
        return E_POINTER;
    *connector = NULL;
    if (index >= topology_connector_count(This))
        return E_INVALIDARG;
    return get_connector(This, index, connector);
}

static HRESULT WINAPI topology_GetSubunitCount(IDeviceTopology *iface, UINT *count)
{
    struct topology *This = impl_from_IDeviceTopology(iface);

    TRACE("(%p)->(%p)\n", This, count);

    if (!count)
        return E_POINTER;
    *count = This->node_count;
    return S_OK;
}

static HRESULT WINAPI topology_GetSubunit(IDeviceTopology *iface, UINT index, ISubUnit **subunit)
{
    struct topology *This = impl_from_IDeviceTopology(iface);
    IPart *part;
    HRESULT hr;

    TRACE("(%p)->(%u, %p)\n", This, index, subunit);

    if (!subunit)
        return E_POINTER;
    *subunit = NULL;
    if (index >= This->node_count)
        return E_INVALIDARG;
    if (FAILED(hr = part_create(This, PART_ID(PART_KIND_SUBUNIT, index), &part)))
        return hr;
    *subunit = &impl_from_IPart(part)->ISubUnit_iface;
    return S_OK;
}

static HRESULT WINAPI topology_GetPartById(IDeviceTopology *iface, UINT id, IPart **part)
{
    struct topology *This = impl_from_IDeviceTopology(iface);

    TRACE("(%p)->(0x%08x, %p)\n", This, id, part);

    if (!part)
        return E_POINTER;
    *part = NULL;
    if (PART_KIND(id) == PART_KIND_CONNECTOR)
    {
        if (PART_INDEX(id) >= topology_connector_count(This))
            return E_NOTFOUND;
    }
    else if (PART_KIND(id) == PART_KIND_SUBUNIT)
    {
        if (PART_INDEX(id) >= This->node_count)
            return E_NOTFOUND;
    }
    else
        return E_INVALIDARG;
    return part_create(This, id, part);
}

static HRESULT WINAPI topology_GetDeviceId(IDeviceTopology *iface, LPWSTR *id)
{
    struct topology *This = impl_from_IDeviceTopology(iface);

    TRACE("(%p)->(%p)\n", This, id);

    if (!id)
        return E_POINTER;
    return co_wcsdup(This->id, id);
}

static HRESULT WINAPI topology_GetSignalPath(IDeviceTopology *iface, IPart *from, IPart *to,
                                             BOOL reject_mixed, IPartsList **parts)
{
    struct topology *This = impl_from_IDeviceTopology(iface);

    FIXME("(%p)->(%p, %p, %d, %p) - stub\n", This, from, to, reject_mixed, parts);

    if (!from || !to || !parts)
        return E_POINTER;
    *parts = NULL;
    /* TODO: walk the part links between the two parts */
    return E_NOTIMPL;
}

static const IDeviceTopologyVtbl topology_vtbl =
{
    topology_QueryInterface,
    topology_AddRef,
    topology_Release,
    topology_GetConnectorCount,
    topology_GetConnector,
    topology_GetSubunitCount,
    topology_GetSubunit,
    topology_GetPartById,
    topology_GetDeviceId,
    topology_GetSignalPath,
};

static BOOL part_is_connector(const struct part *part)
{
    return PART_KIND(part->local_id) == PART_KIND_CONNECTOR;
}

static const struct filter_pin *part_pin(const struct part *part)
{
    assert(part_is_connector(part) && !part->topology->endpoint);
    assert(PART_INDEX(part->local_id) < part->topology->pin_count);
    return &part->topology->pins[PART_INDEX(part->local_id)];
}

static HRESULT WINAPI part_QueryInterface(IPart *iface, REFIID riid, void **ppv)
{
    struct part *This = impl_from_IPart(iface);

    TRACE("(%p)->(%s, %p)\n", This, debugstr_guid(riid), ppv);

    if (!ppv)
        return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IPart))
        *ppv = &This->IPart_iface;
    else if (part_is_connector(This) && IsEqualIID(riid, &IID_IConnector))
        *ppv = &This->IConnector_iface;
    else if (!part_is_connector(This) && IsEqualIID(riid, &IID_ISubUnit))
        *ppv = &This->ISubUnit_iface;
    else
    {
        *ppv = NULL;
        return E_NOINTERFACE;
    }
    IPart_AddRef(iface);
    return S_OK;
}

static ULONG WINAPI part_AddRef(IPart *iface)
{
    struct part *This = impl_from_IPart(iface);
    ULONG ref = InterlockedIncrement(&This->ref);

    TRACE("(%p) new ref %lu\n", This, ref);
    assert(ref > 1);
    return ref;
}

static ULONG WINAPI part_Release(IPart *iface)
{
    struct part *This = impl_from_IPart(iface);
    ULONG ref = InterlockedDecrement(&This->ref);

    TRACE("(%p) new ref %lu\n", This, ref);
    assert(ref != ~0u);
    if (!ref)
    {
        IDeviceTopology_Release(&This->topology->IDeviceTopology_iface);
        free(This);
    }
    return ref;
}

static HRESULT WINAPI part_GetName(IPart *iface, LPWSTR *name)
{
    struct part *This = impl_from_IPart(iface);
    const struct topology *topology = This->topology;

    TRACE("(%p)->(%p)\n", This, name);

    if (!name)
        return E_POINTER;
    if (topology->endpoint)
        return co_wcsdup(L"", name);
    if (part_is_connector(This))
        return co_wcsdup(part_pin(This)->name, name);
    return co_wcsdup(topology->nodes[PART_INDEX(This->local_id)].name, name);
}

static HRESULT WINAPI part_GetLocalId(IPart *iface, UINT *id)
{
    struct part *This = impl_from_IPart(iface);

    TRACE("(%p)->(%p)\n", This, id);

    if (!id)
        return E_POINTER;
    *id = This->local_id;
    return S_OK;
}

static HRESULT WINAPI part_GetGlobalId(IPart *iface, LPWSTR *id)
{
    struct part *This = impl_from_IPart(iface);

    TRACE("(%p)->(%p)\n", This, id);

    if (!id)
        return E_POINTER;
    return format_part_id(This->topology->id, This->local_id, id);
}

static HRESULT WINAPI part_GetPartType(IPart *iface, PartType *type)
{
    struct part *This = impl_from_IPart(iface);

    TRACE("(%p)->(%p)\n", This, type);

    if (!type)
        return E_POINTER;
    *type = part_is_connector(This) ? Connector : Subunit;
    return S_OK;
}

static HRESULT WINAPI part_GetSubType(IPart *iface, GUID *subtype)
{
    struct part *This = impl_from_IPart(iface);
    const struct topology *topology = This->topology;

    TRACE("(%p)->(%p)\n", This, subtype);

    if (!subtype)
        return E_POINTER;
    if (topology->endpoint)
        *subtype = GUID_NULL;
    else if (part_is_connector(This))
        *subtype = part_pin(This)->category;
    else
        *subtype = topology->nodes[PART_INDEX(This->local_id)].type;
    return S_OK;
}

static HRESULT WINAPI part_GetControlInterfaceCount(IPart *iface, UINT *count)
{
    struct part *This = impl_from_IPart(iface);

    TRACE("(%p)->(%p)\n", This, count);

    if (!count)
        return E_POINTER;
    /* FIXME: adapter parts report no control interfaces; Windows lists the KS controls of each part */
    *count = 0;
    return S_OK;
}

static HRESULT WINAPI part_GetControlInterface(IPart *iface, UINT index, IControlInterface **control)
{
    struct part *This = impl_from_IPart(iface);

    TRACE("(%p)->(%u, %p)\n", This, index, control);

    if (!control)
        return E_POINTER;
    *control = NULL;
    return E_INVALIDARG;
}

static HRESULT WINAPI part_EnumPartsIncoming(IPart *iface, IPartsList **parts)
{
    struct part *This = impl_from_IPart(iface);

    TRACE("(%p)->(%p)\n", This, parts);

    if (!parts)
        return E_POINTER;
    return get_parts_list(This->topology, This->local_id, TRUE, parts);
}

static HRESULT WINAPI part_EnumPartsOutgoing(IPart *iface, IPartsList **parts)
{
    struct part *This = impl_from_IPart(iface);

    TRACE("(%p)->(%p)\n", This, parts);

    if (!parts)
        return E_POINTER;
    return get_parts_list(This->topology, This->local_id, FALSE, parts);
}

static HRESULT WINAPI part_GetTopologyObjects(IPart *iface, IDeviceTopology **topology)
{
    struct part *This = impl_from_IPart(iface);

    TRACE("(%p)->(%p)\n", This, topology);

    if (!topology)
        return E_POINTER;
    *topology = &This->topology->IDeviceTopology_iface;
    IDeviceTopology_AddRef(*topology);
    return S_OK;
}

static HRESULT WINAPI part_Activate(IPart *iface, DWORD clsctx, REFIID riid, void **ppv)
{
    struct part *This = impl_from_IPart(iface);

    FIXME("(%p)->(%#lx, %s, %p) - stub\n", This, clsctx, debugstr_guid(riid), ppv);

    if (!ppv)
        return E_POINTER;
    *ppv = NULL;
    /* TODO: control interfaces (IAudioVolumeLevel, IAudioMute, IKsJackDescription, ...) */
    return E_NOINTERFACE;
}

static HRESULT WINAPI part_RegisterControlChangeCallback(IPart *iface, REFGUID riid, IControlChangeNotify *notify)
{
    struct part *This = impl_from_IPart(iface);

    FIXME("(%p)->(%s, %p) - stub\n", This, debugstr_guid(riid), notify);
    /* TODO: control change notifications */
    return E_NOTIMPL;
}

static HRESULT WINAPI part_UnregisterControlChangeCallback(IPart *iface, IControlChangeNotify *notify)
{
    struct part *This = impl_from_IPart(iface);

    FIXME("(%p)->(%p) - stub\n", This, notify);
    /* TODO: control change notifications */
    return E_NOTIMPL;
}

static const IPartVtbl part_vtbl =
{
    part_QueryInterface,
    part_AddRef,
    part_Release,
    part_GetName,
    part_GetLocalId,
    part_GetGlobalId,
    part_GetPartType,
    part_GetSubType,
    part_GetControlInterfaceCount,
    part_GetControlInterface,
    part_EnumPartsIncoming,
    part_EnumPartsOutgoing,
    part_GetTopologyObjects,
    part_Activate,
    part_RegisterControlChangeCallback,
    part_UnregisterControlChangeCallback,
};

static HRESULT WINAPI connector_QueryInterface(IConnector *iface, REFIID riid, void **ppv)
{
    return part_QueryInterface(&impl_from_IConnector(iface)->IPart_iface, riid, ppv);
}

static ULONG WINAPI connector_AddRef(IConnector *iface)
{
    return part_AddRef(&impl_from_IConnector(iface)->IPart_iface);
}

static ULONG WINAPI connector_Release(IConnector *iface)
{
    return part_Release(&impl_from_IConnector(iface)->IPart_iface);
}

static HRESULT WINAPI connector_GetType(IConnector *iface, ConnectorType *type)
{
    struct part *This = impl_from_IConnector(iface);
    struct topology *peer;

    TRACE("(%p)->(%p)\n", This, type);

    if (!type)
        return E_POINTER;
    if (!This->topology->endpoint)
        *type = part_pin(This)->type;
    else if ((peer = endpoint_peer(This->topology)))
        *type = peer->pins[This->topology->peer_pin].type;
    else
        *type = Unknown_Connector;
    return S_OK;
}

static HRESULT WINAPI connector_GetDataFlow(IConnector *iface, DataFlow *flow)
{
    struct part *This = impl_from_IConnector(iface);

    TRACE("(%p)->(%p)\n", This, flow);

    if (!flow)
        return E_POINTER;
    *flow = This->topology->endpoint ? This->topology->endpoint_flow : part_pin(This)->flow;
    return S_OK;
}

static HRESULT WINAPI connector_ConnectTo(IConnector *iface, IConnector *other)
{
    struct part *This = impl_from_IConnector(iface);

    FIXME("(%p)->(%p) - stub\n", This, other);

    if (!other)
        return E_POINTER;
    /* TODO: connections between device topologies are fixed by the KS filters */
    return E_NOTIMPL;
}

static HRESULT WINAPI connector_Disconnect(IConnector *iface)
{
    struct part *This = impl_from_IConnector(iface);

    FIXME("(%p) - stub\n", This);
    /* TODO: connections between device topologies are fixed by the KS filters */
    return E_NOTIMPL;
}

static HRESULT connector_peer(struct part *This, WCHAR **device_id, UINT *local_id, MMDevice **endpoint)
{
    const struct topology *topology = This->topology;
    const struct filter_pin *pin;
    HRESULT hr;

    *device_id = NULL;
    *endpoint = NULL;
    if (topology->endpoint)
    {
        if (!topology->peer_id)
            return E_NOTFOUND;
        *device_id = heap_wcsdup(topology->peer_id);
        *local_id = PART_ID(PART_KIND_CONNECTOR, topology->peer_pin);
        return *device_id ? S_OK : E_OUTOFMEMORY;
    }

    pin = part_pin(This);
    if (pin->peer_id)
    {
        *device_id = heap_wcsdup(pin->peer_id);
        *local_id = PART_ID(PART_KIND_CONNECTOR, pin->peer_pin);
        return *device_id ? S_OK : E_OUTOFMEMORY;
    }
    if (pin->type == Software_IO)
        return E_NOTFOUND;

    if (FAILED(hr = find_endpoint(topology, PART_INDEX(This->local_id), endpoint)))
        return hr;
    hr = IMMDevice_GetId(&(*endpoint)->IMMDevice_iface, device_id);
    if (SUCCEEDED(hr))
    {
        WCHAR *copy = heap_wcsdup(*device_id);

        CoTaskMemFree(*device_id);
        *device_id = copy;
        hr = copy ? S_OK : E_OUTOFMEMORY;
    }
    if (FAILED(hr))
    {
        IMMDevice_Release(&(*endpoint)->IMMDevice_iface);
        *endpoint = NULL;
        return hr;
    }
    *local_id = PART_ID(PART_KIND_CONNECTOR, 0);
    return S_OK;
}

static HRESULT WINAPI connector_IsConnected(IConnector *iface, BOOL *connected)
{
    struct part *This = impl_from_IConnector(iface);
    MMDevice *endpoint;
    WCHAR *device_id;
    UINT local_id;

    TRACE("(%p)->(%p)\n", This, connected);

    if (!connected)
        return E_POINTER;
    *connected = SUCCEEDED(connector_peer(This, &device_id, &local_id, &endpoint));
    free(device_id);
    if (endpoint)
        IMMDevice_Release(&endpoint->IMMDevice_iface);
    return S_OK;
}

static HRESULT WINAPI connector_GetConnectedTo(IConnector *iface, IConnector **other)
{
    struct part *This = impl_from_IConnector(iface);
    IDeviceTopology *endpoint_topology;
    struct topology *peer;
    MMDevice *endpoint;
    WCHAR *device_id;
    UINT local_id;
    HRESULT hr;

    TRACE("(%p)->(%p)\n", This, other);

    if (!other)
        return E_POINTER;
    *other = NULL;
    if (FAILED(hr = connector_peer(This, &device_id, &local_id, &endpoint)))
        return hr;

    if (endpoint)
    {
        if (endpoint->state != DEVICE_STATE_ACTIVE)
            hr = HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND);
        else if (SUCCEEDED(hr = reactos_device_topology_create(endpoint, &endpoint_topology)))
        {
            hr = get_connector(impl_from_IDeviceTopology(endpoint_topology), 0, other);
            IDeviceTopology_Release(endpoint_topology);
        }
        IMMDevice_Release(&endpoint->IMMDevice_iface);
    }
    else if (This->topology->endpoint)
    {
        if (!(peer = endpoint_peer(This->topology)))
            hr = HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND);
        else
            hr = get_connector(peer, PART_INDEX(local_id), other);
    }
    else if (SUCCEEDED(hr = filter_topology_create(device_id, &peer)))
    {
        if (PART_INDEX(local_id) < peer->pin_count)
            hr = get_connector(peer, PART_INDEX(local_id), other);
        else
            hr = E_NOTFOUND;
        IDeviceTopology_Release(&peer->IDeviceTopology_iface);
    }
    free(device_id);
    return hr;
}

static HRESULT WINAPI connector_GetConnectorIdConnectedTo(IConnector *iface, LPWSTR *id)
{
    struct part *This = impl_from_IConnector(iface);
    MMDevice *endpoint;
    WCHAR *device_id;
    UINT local_id;
    HRESULT hr;

    TRACE("(%p)->(%p)\n", This, id);

    if (!id)
        return E_POINTER;
    *id = NULL;
    if (FAILED(hr = connector_peer(This, &device_id, &local_id, &endpoint)))
        return hr;
    if (endpoint)
        IMMDevice_Release(&endpoint->IMMDevice_iface);
    hr = format_part_id(device_id, local_id, id);
    free(device_id);
    return hr;
}

static HRESULT WINAPI connector_GetDeviceIdConnectedTo(IConnector *iface, LPWSTR *id)
{
    struct part *This = impl_from_IConnector(iface);
    MMDevice *endpoint;
    WCHAR *device_id;
    UINT local_id;
    HRESULT hr;

    TRACE("(%p)->(%p)\n", This, id);

    if (!id)
        return E_POINTER;
    *id = NULL;
    if (FAILED(hr = connector_peer(This, &device_id, &local_id, &endpoint)))
        return hr;
    if (endpoint)
        IMMDevice_Release(&endpoint->IMMDevice_iface);
    hr = co_wcsdup(device_id, id);
    free(device_id);
    return hr;
}

static const IConnectorVtbl connector_vtbl =
{
    connector_QueryInterface,
    connector_AddRef,
    connector_Release,
    connector_GetType,
    connector_GetDataFlow,
    connector_ConnectTo,
    connector_Disconnect,
    connector_IsConnected,
    connector_GetConnectedTo,
    connector_GetConnectorIdConnectedTo,
    connector_GetDeviceIdConnectedTo,
};

static HRESULT WINAPI subunit_QueryInterface(ISubUnit *iface, REFIID riid, void **ppv)
{
    return part_QueryInterface(&impl_from_ISubUnit(iface)->IPart_iface, riid, ppv);
}

static ULONG WINAPI subunit_AddRef(ISubUnit *iface)
{
    return part_AddRef(&impl_from_ISubUnit(iface)->IPart_iface);
}

static ULONG WINAPI subunit_Release(ISubUnit *iface)
{
    return part_Release(&impl_from_ISubUnit(iface)->IPart_iface);
}

static const ISubUnitVtbl subunit_vtbl =
{
    subunit_QueryInterface,
    subunit_AddRef,
    subunit_Release,
};

static HRESULT WINAPI parts_list_QueryInterface(IPartsList *iface, REFIID riid, void **ppv)
{
    struct parts_list *This = impl_from_IPartsList(iface);

    TRACE("(%p)->(%s, %p)\n", This, debugstr_guid(riid), ppv);

    if (!ppv)
        return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IPartsList))
    {
        *ppv = &This->IPartsList_iface;
        IPartsList_AddRef(iface);
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI parts_list_AddRef(IPartsList *iface)
{
    struct parts_list *This = impl_from_IPartsList(iface);
    ULONG ref = InterlockedIncrement(&This->ref);

    TRACE("(%p) new ref %lu\n", This, ref);
    assert(ref > 1);
    return ref;
}

static ULONG WINAPI parts_list_Release(IPartsList *iface)
{
    struct parts_list *This = impl_from_IPartsList(iface);
    ULONG ref = InterlockedDecrement(&This->ref);
    UINT i;

    TRACE("(%p) new ref %lu\n", This, ref);
    assert(ref != ~0u);
    if (!ref)
    {
        for (i = 0; i < This->count; ++i)
            IPart_Release(This->parts[i]);
        free(This);
    }
    return ref;
}

static HRESULT WINAPI parts_list_GetCount(IPartsList *iface, UINT *count)
{
    struct parts_list *This = impl_from_IPartsList(iface);

    TRACE("(%p)->(%p)\n", This, count);

    if (!count)
        return E_POINTER;
    *count = This->count;
    return S_OK;
}

static HRESULT WINAPI parts_list_GetPart(IPartsList *iface, UINT index, IPart **part)
{
    struct parts_list *This = impl_from_IPartsList(iface);

    TRACE("(%p)->(%u, %p)\n", This, index, part);

    if (!part)
        return E_POINTER;
    if (index >= This->count)
    {
        *part = NULL;
        return E_INVALIDARG;
    }
    *part = This->parts[index];
    IPart_AddRef(*part);
    return S_OK;
}

static const IPartsListVtbl parts_list_vtbl =
{
    parts_list_QueryInterface,
    parts_list_AddRef,
    parts_list_Release,
    parts_list_GetCount,
    parts_list_GetPart,
};

static HRESULT WINAPI filter_device_QueryInterface(IMMDevice *iface, REFIID riid, void **ppv)
{
    struct filter_device *This = impl_from_IMMDevice(iface);

    TRACE("(%p)->(%s, %p)\n", This, debugstr_guid(riid), ppv);

    if (!ppv)
        return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IMMDevice))
    {
        *ppv = &This->IMMDevice_iface;
        IMMDevice_AddRef(iface);
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI filter_device_AddRef(IMMDevice *iface)
{
    struct filter_device *This = impl_from_IMMDevice(iface);
    ULONG ref = InterlockedIncrement(&This->ref);

    TRACE("(%p) new ref %lu\n", This, ref);
    assert(ref > 1);
    return ref;
}

static ULONG WINAPI filter_device_Release(IMMDevice *iface)
{
    struct filter_device *This = impl_from_IMMDevice(iface);
    ULONG ref = InterlockedDecrement(&This->ref);

    TRACE("(%p) new ref %lu\n", This, ref);
    assert(ref != ~0u);
    if (!ref)
    {
        free(This->id);
        free(This);
    }
    return ref;
}

static HRESULT WINAPI filter_device_Activate(IMMDevice *iface, REFIID riid, DWORD clsctx,
                                             PROPVARIANT *params, void **ppv)
{
    struct filter_device *This = impl_from_IMMDevice(iface);
    struct topology *topology;
    HRESULT hr;

    TRACE("(%p)->(%s, %#lx, %p, %p)\n", This, debugstr_guid(riid), clsctx, params, ppv);

    if (!ppv)
        return E_POINTER;
    *ppv = NULL;
    if (!IsEqualIID(riid, &IID_IDeviceTopology))
        return E_NOINTERFACE;
    if (FAILED(hr = filter_topology_create(This->id, &topology)))
        return hr;
    *ppv = &topology->IDeviceTopology_iface;
    return S_OK;
}

#define FILTER_STORE_KEYS 6

struct filter_store
{
    IPropertyStore IPropertyStore_iface;
    LONG ref;
    UINT count;
    PROPERTYKEY keys[FILTER_STORE_KEYS];
    PROPVARIANT values[FILTER_STORE_KEYS];
};

static const PROPERTYKEY interface_friendly_name_key =
    {{0x026e516e, 0xb814, 0x414b, {0x83, 0xcd, 0x85, 0x6d, 0x6f, 0xef, 0x48, 0x22}}, 2};
static const PROPERTYKEY interface_enabled_key =
    {{0x026e516e, 0xb814, 0x414b, {0x83, 0xcd, 0x85, 0x6d, 0x6f, 0xef, 0x48, 0x22}}, 3};
static const PROPERTYKEY interface_class_key =
    {{0x026e516e, 0xb814, 0x414b, {0x83, 0xcd, 0x85, 0x6d, 0x6f, 0xef, 0x48, 0x22}}, 4};
static const PROPERTYKEY interface_reference_key =
    {{0x026e516e, 0xb814, 0x414b, {0x83, 0xcd, 0x85, 0x6d, 0x6f, 0xef, 0x48, 0x22}}, 5};
static const PROPERTYKEY instance_id_key =
    {{0x78c34fc8, 0x104a, 0x4aca, {0x9e, 0xa4, 0x52, 0x4d, 0x52, 0x99, 0x6e, 0x57}}, 256};
static const PROPERTYKEY item_name_display_key =
    {{0xb725f130, 0x47ef, 0x101a, {0xa5, 0xf1, 0x02, 0x60, 0x8c, 0x9e, 0xeb, 0xac}}, 10};

static inline struct filter_store *impl_from_IPropertyStore(IPropertyStore *iface)
{
    return CONTAINING_RECORD(iface, struct filter_store, IPropertyStore_iface);
}

static HRESULT WINAPI filter_store_QueryInterface(IPropertyStore *iface, REFIID riid, void **ppv)
{
    struct filter_store *This = impl_from_IPropertyStore(iface);

    TRACE("(%p)->(%s, %p)\n", This, debugstr_guid(riid), ppv);

    if (!ppv)
        return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IPropertyStore))
    {
        *ppv = &This->IPropertyStore_iface;
        IPropertyStore_AddRef(iface);
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI filter_store_AddRef(IPropertyStore *iface)
{
    struct filter_store *This = impl_from_IPropertyStore(iface);
    ULONG ref = InterlockedIncrement(&This->ref);

    TRACE("(%p) new ref %lu\n", This, ref);
    assert(ref > 1);
    return ref;
}

static ULONG WINAPI filter_store_Release(IPropertyStore *iface)
{
    struct filter_store *This = impl_from_IPropertyStore(iface);
    ULONG ref = InterlockedDecrement(&This->ref);
    UINT i;

    TRACE("(%p) new ref %lu\n", This, ref);
    assert(ref != ~0u);
    if (!ref)
    {
        for (i = 0; i < This->count; ++i)
            PropVariantClear(&This->values[i]);
        free(This);
    }
    return ref;
}

static HRESULT WINAPI filter_store_GetCount(IPropertyStore *iface, DWORD *count)
{
    struct filter_store *This = impl_from_IPropertyStore(iface);

    TRACE("(%p)->(%p)\n", This, count);

    if (!count)
        return E_POINTER;
    *count = This->count;
    return S_OK;
}

static HRESULT WINAPI filter_store_GetAt(IPropertyStore *iface, DWORD index, PROPERTYKEY *key)
{
    struct filter_store *This = impl_from_IPropertyStore(iface);

    TRACE("(%p)->(%lu, %p)\n", This, index, key);

    if (!key)
        return E_POINTER;
    if (index >= This->count)
        return E_INVALIDARG;
    *key = This->keys[index];
    return S_OK;
}

static HRESULT WINAPI filter_store_GetValue(IPropertyStore *iface, REFPROPERTYKEY key, PROPVARIANT *value)
{
    struct filter_store *This = impl_from_IPropertyStore(iface);
    UINT i;

    TRACE("(%p)->(%p, %p)\n", This, key, value);

    if (!key || !value)
        return E_POINTER;
    for (i = 0; i < This->count; ++i)
    {
        if (IsEqualGUID(&This->keys[i].fmtid, &key->fmtid) && This->keys[i].pid == key->pid)
            return PropVariantCopy(value, &This->values[i]);
    }
    PropVariantInit(value);
    return S_OK;
}

static HRESULT WINAPI filter_store_SetValue(IPropertyStore *iface, REFPROPERTYKEY key, REFPROPVARIANT value)
{
    struct filter_store *This = impl_from_IPropertyStore(iface);

    TRACE("(%p)->(%p, %p)\n", This, key, value);
    return STG_E_ACCESSDENIED;
}

static HRESULT WINAPI filter_store_Commit(IPropertyStore *iface)
{
    struct filter_store *This = impl_from_IPropertyStore(iface);

    TRACE("(%p)\n", This);
    return STG_E_ACCESSDENIED;
}

static const IPropertyStoreVtbl filter_store_vtbl =
{
    filter_store_QueryInterface,
    filter_store_AddRef,
    filter_store_Release,
    filter_store_GetCount,
    filter_store_GetAt,
    filter_store_GetValue,
    filter_store_SetValue,
    filter_store_Commit,
};

static HRESULT filter_store_add_string(struct filter_store *store, const PROPERTYKEY *key, const WCHAR *string)
{
    PROPVARIANT *value = &store->values[store->count];

    assert(store->count < FILTER_STORE_KEYS);
    if (!string)
        return S_OK;
    PropVariantInit(value);
    value->vt = VT_LPWSTR;
    if (FAILED(co_wcsdup(string, &value->pwszVal)))
        return E_OUTOFMEMORY;
    store->keys[store->count++] = *key;
    return S_OK;
}

static HRESULT filter_store_fill(struct filter_store *store, const WCHAR *path)
{
    SP_DEVICE_INTERFACE_DATA data;
    SP_DEVINFO_DATA device;
    WCHAR friendly_name[256], instance_id[MAX_DEVICE_ID_LEN];
    const WCHAR *class_part, *reference = NULL;
    PROPVARIANT *value;
    HDEVINFO set;
    HKEY key;
    DWORD size, type;
    BOOL has_name = FALSE, has_instance;
    HRESULT hr = S_OK;

    set = SetupDiCreateDeviceInfoList(NULL, NULL);
    if (set == INVALID_HANDLE_VALUE)
        return HRESULT_FROM_WIN32(GetLastError());
    data.cbSize = sizeof(data);
    if (!SetupDiOpenDeviceInterfaceW(set, path, 0, &data))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        SetupDiDestroyDeviceInfoList(set);
        return hr;
    }

    key = SetupDiOpenDeviceInterfaceRegKey(set, &data, 0, KEY_READ);
    if (key != INVALID_HANDLE_VALUE)
    {
        size = sizeof(friendly_name) - sizeof(WCHAR);
        has_name = !RegQueryValueExW(key, L"FriendlyName", NULL, &type, (BYTE *)friendly_name, &size) &&
                   type == REG_SZ;
        friendly_name[has_name ? size / sizeof(WCHAR) : 0] = 0;
        RegCloseKey(key);
    }

    device.cbSize = sizeof(device);
    has_instance = SetupDiGetDeviceInterfaceDetailW(set, &data, NULL, 0, NULL, &device) ||
                   GetLastError() == ERROR_INSUFFICIENT_BUFFER;
    has_instance = has_instance &&
                   SetupDiGetDeviceInstanceIdW(set, &device, instance_id, ARRAY_SIZE(instance_id), NULL);

    if ((class_part = wcsstr(path, L"#{")) && (reference = wcschr(class_part, '}')) && reference[1] == '\\')
        reference += 2;
    else
        reference = NULL;

    hr = filter_store_add_string(store, &interface_friendly_name_key, has_name ? friendly_name : NULL);

    if (SUCCEEDED(hr))
    {
        value = &store->values[store->count];
        PropVariantInit(value);
        value->vt = VT_BOOL;
        value->boolVal = (data.Flags & SPINT_ACTIVE) ? 0xff : 0;
        store->keys[store->count++] = interface_enabled_key;
    }
    if (SUCCEEDED(hr))
        hr = filter_store_add_string(store, &instance_id_key, has_instance ? instance_id : NULL);
    if (SUCCEEDED(hr))
        hr = filter_store_add_string(store, &item_name_display_key, has_name ? friendly_name : NULL);
    if (SUCCEEDED(hr))
    {
        value = &store->values[store->count];
        PropVariantInit(value);
        if (!(value->puuid = CoTaskMemAlloc(sizeof(GUID))))
            hr = E_OUTOFMEMORY;
        else
        {
            value->vt = VT_CLSID;
            *value->puuid = data.InterfaceClassGuid;
            store->keys[store->count++] = interface_class_key;
        }
    }
    if (SUCCEEDED(hr))
        hr = filter_store_add_string(store, &interface_reference_key, reference);
    /* TODO: PKEY_Device_ContainerId and the private keys Windows also lists */

    SetupDiDestroyDeviceInfoList(set);
    return hr;
}

static HRESULT WINAPI filter_device_OpenPropertyStore(IMMDevice *iface, DWORD access, IPropertyStore **store)
{
    struct filter_device *This = impl_from_IMMDevice(iface);
    struct filter_store *object;
    HRESULT hr;

    TRACE("(%p)->(%#lx, %p)\n", This, access, store);

    if (!store)
        return E_POINTER;
    *store = NULL;
    if (access != STGM_READ)
        return STG_E_ACCESSDENIED;

    if (!(object = calloc(1, sizeof(*object))))
        return E_OUTOFMEMORY;
    object->IPropertyStore_iface.lpVtbl = &filter_store_vtbl;
    object->ref = 1;
    if (FAILED(hr = filter_store_fill(object, This->id + FILTER_ID_PREFIX_LENGTH)))
    {
        IPropertyStore_Release(&object->IPropertyStore_iface);
        return hr;
    }
    *store = &object->IPropertyStore_iface;
    return S_OK;
}

static HRESULT WINAPI filter_device_GetId(IMMDevice *iface, WCHAR **id)
{
    struct filter_device *This = impl_from_IMMDevice(iface);

    TRACE("(%p)->(%p)\n", This, id);

    if (!id)
        return E_POINTER;
    return co_wcsdup(This->id, id);
}

static HRESULT WINAPI filter_device_GetState(IMMDevice *iface, DWORD *state)
{
    struct filter_device *This = impl_from_IMMDevice(iface);

    TRACE("(%p)->(%p)\n", This, state);

    if (!state)
        return E_POINTER;
    *state = DEVICE_STATE_ACTIVE;
    return S_OK;
}

static const IMMDeviceVtbl filter_device_vtbl =
{
    filter_device_QueryInterface,
    filter_device_AddRef,
    filter_device_Release,
    filter_device_Activate,
    filter_device_OpenPropertyStore,
    filter_device_GetId,
    filter_device_GetState,
};

HRESULT reactos_filter_device_get(const WCHAR *id, IMMDevice **out)
{
    SP_DEVICE_INTERFACE_DATA data;
    struct filter_device *device;
    HDEVINFO set;
    BOOL found;

    assert(id && out);
    *out = NULL;
    if (wcsncmp(id, FILTER_ID_PREFIX, FILTER_ID_PREFIX_LENGTH))
        return E_INVALIDARG;

    set = SetupDiCreateDeviceInfoList(NULL, NULL);
    if (set == INVALID_HANDLE_VALUE)
        return HRESULT_FROM_WIN32(GetLastError());
    data.cbSize = sizeof(data);
    found = SetupDiOpenDeviceInterfaceW(set, id + FILTER_ID_PREFIX_LENGTH, 0, &data);
    if (!found)
    {
        DWORD error = GetLastError();

        SetupDiDestroyDeviceInfoList(set);
        return HRESULT_FROM_WIN32(error);
    }
    SetupDiDestroyDeviceInfoList(set);

    if (!(device = calloc(1, sizeof(*device))) || !(device->id = heap_wcsdup(id)))
    {
        free(device);
        return E_OUTOFMEMORY;
    }
    device->IMMDevice_iface.lpVtbl = &filter_device_vtbl;
    device->ref = 1;
    *out = &device->IMMDevice_iface;
    return S_OK;
}
