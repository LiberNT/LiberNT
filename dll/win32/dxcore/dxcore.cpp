/*
 * PROJECT:     LiberNT DXCore
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     DXCore adapter enumeration over D3DKMT
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntstatus.h>
#define WIN32_NO_STATUS
#include <windows.h>
#include <winternl.h>
#include <d3dkmthk.h>
#include <stdint.h>
#include <string.h>
#include <dxgi.h>
#include <initguid.h>
#include <dxcore.h>

#define DXCORE_MAX_ADAPTERS 16

class Factory;

struct AdapterData
{
    LUID Luid;
    D3DKMT_ADAPTERTYPE Type;
    D3DKMT_DEVICE_IDS Ids;
    char Description[sizeof(D3DKMT_DRIVER_DESCRIPTION::DriverDescription) / sizeof(WCHAR) * 3];
    LARGE_INTEGER DriverVersion;
    BOOL HaveDriverVersion;
    UINT KmdVersion;
    D3DKMDT_PREEMPTION_CAPS Preemption;
    D3DKMT_SEGMENTSIZEINFO Segments;
    UINT PhysicalCount;
};

static NTSTATUS query_adapter(D3DKMT_HANDLE adapter, KMTQUERYADAPTERINFOTYPE type, void *data, UINT size)
{
    D3DKMT_QUERYADAPTERINFO query = {};

    query.hAdapter = adapter;
    query.Type = type;
    query.pPrivateDriverData = data;
    query.PrivateDriverDataSize = size;
    return D3DKMTQueryAdapterInfo(&query);
}

static void close_adapter(D3DKMT_HANDLE adapter)
{
    D3DKMT_CLOSEADAPTER close = {};

    close.hAdapter = adapter;
    D3DKMTCloseAdapter(&close);
}

static BOOL parse_version(const WCHAR *str, LARGE_INTEGER *version)
{
    ULONGLONG parts[4] = { 0, 0, 0, 0 };
    unsigned i = 0;

    for (; *str && i < 4; str++)
    {
        if (*str == '.')
            i++;
        else if (*str >= '0' && *str <= '9')
            parts[i] = parts[i] * 10 + (*str - '0');
        else
            return FALSE;
    }
    if (i != 3) return FALSE;
    version->QuadPart = (LONGLONG)(((parts[0] & 0xffff) << 48) | ((parts[1] & 0xffff) << 32) |
                                   ((parts[2] & 0xffff) << 16) | (parts[3] & 0xffff));
    return TRUE;
}

static BOOL query_driver_version(D3DKMT_HANDLE adapter, LARGE_INTEGER *version)
{
    D3DKMT_UMD_DRIVER_VERSION umd = {};
    BYTE buffer[sizeof(D3DDDI_QUERYREGISTRY_INFO) + MAX_PATH * sizeof(WCHAR)];
    D3DDDI_QUERYREGISTRY_INFO *info = (D3DDDI_QUERYREGISTRY_INFO *)buffer;

    if (NT_SUCCESS(query_adapter(adapter, KMTQAITYPE_UMD_DRIVER_VERSION, &umd, sizeof(umd))))
    {
        *version = umd.DriverVersion;
        return TRUE;
    }

    memset(buffer, 0, sizeof(buffer));
    info->QueryType = D3DDDI_QUERYREGISTRY_ADAPTERKEY;
    info->ValueType = REG_SZ;
    lstrcpyW(info->ValueName, L"DriverVersion");
    if (NT_SUCCESS(query_adapter(adapter, KMTQAITYPE_QUERYREGISTRY, buffer, sizeof(buffer))) &&
        info->Status == D3DDDI_QUERYREGISTRY_STATUS_SUCCESS)
    {
        info->OutputString[MAX_PATH - 1] = 0;
        return parse_version(info->OutputString, version);
    }
    return FALSE;
}

static BOOL read_adapter(D3DKMT_HANDLE adapter, const LUID *luid, AdapterData *data)
{
    D3DKMT_QUERY_DEVICE_IDS ids = {};
    D3DKMT_DRIVER_DESCRIPTION *description;
    D3DKMT_ADAPTERREGISTRYINFO *registry;
    D3DKMT_WDDM_1_2_CAPS caps = {};
    D3DKMT_DRIVERVERSION kmd = (D3DKMT_DRIVERVERSION)0;

    memset(data, 0, sizeof(*data));
    data->Luid = *luid;
    if (!NT_SUCCESS(query_adapter(adapter, KMTQAITYPE_ADAPTERTYPE, &data->Type, sizeof(data->Type))))
        return FALSE;

    if (NT_SUCCESS(query_adapter(adapter, KMTQAITYPE_PHYSICALADAPTERDEVICEIDS, &ids, sizeof(ids))))
        data->Ids = ids.DeviceIds;

    description = (D3DKMT_DRIVER_DESCRIPTION *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*description));
    if (description)
    {
        if (NT_SUCCESS(query_adapter(adapter, KMTQAITYPE_DRIVER_DESCRIPTION, description, sizeof(*description))))
        {
            description->DriverDescription[ARRAYSIZE(description->DriverDescription) - 1] = 0;
            WideCharToMultiByte(CP_ACP, 0, description->DriverDescription, -1, data->Description,
                                sizeof(data->Description), NULL, NULL);
        }
        HeapFree(GetProcessHeap(), 0, description);
    }

    registry = data->Description[0] ? NULL :
        (D3DKMT_ADAPTERREGISTRYINFO *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*registry));
    if (registry)
    {
        if (NT_SUCCESS(query_adapter(adapter, KMTQAITYPE_ADAPTERREGISTRYINFO, registry, sizeof(*registry))))
        {
            registry->AdapterString[MAX_PATH - 1] = 0;
            WideCharToMultiByte(CP_ACP, 0, registry->AdapterString, -1, data->Description,
                                sizeof(data->Description), NULL, NULL);
        }
        HeapFree(GetProcessHeap(), 0, registry);
    }

    data->HaveDriverVersion = query_driver_version(adapter, &data->DriverVersion);
    if (NT_SUCCESS(query_adapter(adapter, KMTQAITYPE_DRIVERVERSION, &kmd, sizeof(kmd))))
        data->KmdVersion = (UINT)kmd;
    if (NT_SUCCESS(query_adapter(adapter, KMTQAITYPE_WDDM_1_2_CAPS, &caps, sizeof(caps))))
        data->Preemption = caps.PreemptionCaps;
    query_adapter(adapter, KMTQAITYPE_GETSEGMENTSIZE, &data->Segments, sizeof(data->Segments));
    if (!NT_SUCCESS(query_adapter(adapter, KMTQAITYPE_PHYSICALADAPTERCOUNT, &data->PhysicalCount,
                                  sizeof(data->PhysicalCount))) || !data->PhysicalCount)
        data->PhysicalCount = 1;
    return TRUE;
}

static BOOL adapter_renders(const AdapterData *data)
{
    return data->Type.RenderSupported || data->Type.DisplaySupported;
}

static BOOL adapter_has_attribute(const AdapterData *data, REFGUID attribute)
{
    BOOL compute_only = data->Type.ComputeOnly;

    if (IsEqualGUID(attribute, DXCORE_ADAPTER_ATTRIBUTE_D3D11_GRAPHICS) ||
        IsEqualGUID(attribute, DXCORE_ADAPTER_ATTRIBUTE_D3D12_GRAPHICS))
        return adapter_renders(data) && !compute_only;
    if (IsEqualGUID(attribute, DXCORE_ADAPTER_ATTRIBUTE_D3D12_CORE_COMPUTE) ||
        IsEqualGUID(attribute, DXCORE_ADAPTER_ATTRIBUTE_D3D12_GENERIC_ML))
        return adapter_renders(data);
    if (IsEqualGUID(attribute, DXCORE_HARDWARE_TYPE_ATTRIBUTE_GPU))
        return adapter_renders(data) && !compute_only;
    if (IsEqualGUID(attribute, DXCORE_HARDWARE_TYPE_ATTRIBUTE_COMPUTE_ACCELERATOR))
        return compute_only;
    return FALSE;
}

static UINT enum_adapters(D3DKMT_ADAPTERINFO *infos, UINT count)
{
    D3DKMT_ENUMADAPTERS2 enumerate = {};

    enumerate.NumAdapters = count;
    enumerate.pAdapters = infos;
    if (!NT_SUCCESS(D3DKMTEnumAdapters2(&enumerate)))
        return 0;
    return enumerate.NumAdapters;
}

static size_t property_size(const AdapterData *data, DXCoreAdapterProperty property)
{
    switch (property)
    {
        case DXCoreAdapterProperty::InstanceLuid: return sizeof(LUID);
        case DXCoreAdapterProperty::DriverVersion: return sizeof(LARGE_INTEGER);
        case DXCoreAdapterProperty::DriverDescription: return strlen(data->Description) + 1;
        case DXCoreAdapterProperty::HardwareID: return sizeof(DXCoreHardwareID);
        case DXCoreAdapterProperty::KmdModelVersion: return sizeof(uint32_t);
        case DXCoreAdapterProperty::ComputePreemptionGranularity: return sizeof(uint32_t);
        case DXCoreAdapterProperty::GraphicsPreemptionGranularity: return sizeof(uint32_t);
        case DXCoreAdapterProperty::DedicatedAdapterMemory: return sizeof(uint64_t);
        case DXCoreAdapterProperty::DedicatedSystemMemory: return sizeof(uint64_t);
        case DXCoreAdapterProperty::SharedSystemMemory: return sizeof(uint64_t);
        case DXCoreAdapterProperty::AcgCompatible: return sizeof(bool);
        case DXCoreAdapterProperty::IsHardware: return sizeof(bool);
        case DXCoreAdapterProperty::IsIntegrated: return sizeof(bool);
        case DXCoreAdapterProperty::IsDetachable: return sizeof(bool);
        case DXCoreAdapterProperty::HardwareIDParts: return sizeof(DXCoreHardwareIDParts);
        case DXCoreAdapterProperty::PhysicalAdapterCount: return sizeof(uint32_t);
        case DXCoreAdapterProperty::AdapterEngineCount: return sizeof(uint32_t);
        case DXCoreAdapterProperty::AdapterEngineName: return sizeof(uint32_t);
        default: return 0;
    }
}

class Adapter final : public IDXCoreAdapter1
{
public:
    static void *operator new(size_t size) noexcept { return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size); }
    static void operator delete(void *ptr) noexcept { HeapFree(GetProcessHeap(), 0, ptr); }

    Adapter(Factory *factory, const AdapterData &data);
    ~Adapter();

    const AdapterData &Data() const { return m_data; }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **out) override;
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&m_ref); }
    ULONG STDMETHODCALLTYPE Release() override;

    bool STDMETHODCALLTYPE IsValid() override;
    bool STDMETHODCALLTYPE IsAttributeSupported(REFGUID attributeGUID) override
    {
        return adapter_has_attribute(&m_data, attributeGUID) != FALSE;
    }
    bool STDMETHODCALLTYPE IsPropertySupported(DXCoreAdapterProperty property) override;
    HRESULT STDMETHODCALLTYPE GetProperty(DXCoreAdapterProperty property, size_t bufferSize, void *propertyData) override;
    HRESULT STDMETHODCALLTYPE GetPropertySize(DXCoreAdapterProperty property, size_t *bufferSize) override;
    bool STDMETHODCALLTYPE IsQueryStateSupported(DXCoreAdapterState state) override;
    HRESULT STDMETHODCALLTYPE QueryState(DXCoreAdapterState state, size_t inputStateDetailsSize,
                                         const void *inputStateDetails, size_t outputBufferSize,
                                         void *outputBuffer) override;
    bool STDMETHODCALLTYPE IsSetStateSupported(DXCoreAdapterState) override { return false; }
    HRESULT STDMETHODCALLTYPE SetState(DXCoreAdapterState, size_t, const void *, size_t, const void *) override
    {
        return DXGI_ERROR_UNSUPPORTED;
    }
    HRESULT STDMETHODCALLTYPE GetFactory(REFIID riid, void **ppvFactory) override;
    HRESULT STDMETHODCALLTYPE GetPropertyWithInput(DXCoreAdapterProperty property, size_t inputPropertyDetailsSize,
                                                   const void *inputPropertyDetails, size_t outputBufferSize,
                                                   void *outputBuffer) override;

private:
    LONG m_ref;
    Factory *m_factory;
    AdapterData m_data;
};

class AdapterList final : public IDXCoreAdapterList
{
public:
    static void *operator new(size_t size) noexcept { return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size); }
    static void operator delete(void *ptr) noexcept { HeapFree(GetProcessHeap(), 0, ptr); }

    AdapterList(Factory *factory);
    ~AdapterList();

    void Add(Adapter *adapter)
    {
        if (m_count < DXCORE_MAX_ADAPTERS)
        {
            adapter->AddRef();
            m_adapters[m_count++] = adapter;
        }
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **out) override;
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&m_ref); }
    ULONG STDMETHODCALLTYPE Release() override;

    HRESULT STDMETHODCALLTYPE GetAdapter(uint32_t index, REFIID riid, void **ppvAdapter) override;
    uint32_t STDMETHODCALLTYPE GetAdapterCount() override { return m_count; }
    bool STDMETHODCALLTYPE IsStale() override;
    HRESULT STDMETHODCALLTYPE GetFactory(REFIID riid, void **ppvFactory) override;
    HRESULT STDMETHODCALLTYPE Sort(uint32_t numPreferences, const DXCoreAdapterPreference *preferences) override;
    bool STDMETHODCALLTYPE IsAdapterPreferenceSupported(DXCoreAdapterPreference preference) override
    {
        return (uint32_t)preference <= (uint32_t)DXCoreAdapterPreference::HighPerformance;
    }

private:
    LONG m_ref;
    Factory *m_factory;
    UINT m_count;
    Adapter *m_adapters[DXCORE_MAX_ADAPTERS];
};

struct Notification
{
    uint32_t Cookie;
    IUnknown *Object;
    DXCoreNotificationType Type;
    PFN_DXCORE_NOTIFICATION_CALLBACK Callback;
    void *Context;
};

class Factory final : public IDXCoreAdapterFactory1
{
public:
    static void *operator new(size_t size) noexcept { return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size); }
    static void operator delete(void *ptr) noexcept { HeapFree(GetProcessHeap(), 0, ptr); }

    Factory() : m_ref(1) { InitializeSRWLock(&m_lock); }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **out) override;
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&m_ref); }
    ULONG STDMETHODCALLTYPE Release() override;

    HRESULT STDMETHODCALLTYPE CreateAdapterList(uint32_t numAttributes, const GUID *filterAttributes,
                                                REFIID riid, void **ppvAdapterList) override;
    HRESULT STDMETHODCALLTYPE GetAdapterByLuid(const LUID &adapterLUID, REFIID riid, void **ppvAdapter) override;
    bool STDMETHODCALLTYPE IsNotificationTypeSupported(DXCoreNotificationType notificationType) override
    {
        return (uint32_t)notificationType <=
               (uint32_t)DXCoreNotificationType::AdapterHardwareContentProtectionTeardown;
    }
    HRESULT STDMETHODCALLTYPE RegisterEventNotification(IUnknown *dxCoreObject, DXCoreNotificationType notificationType,
                                                        PFN_DXCORE_NOTIFICATION_CALLBACK callbackFunction,
                                                        void *callbackContext, uint32_t *eventCookie) override;
    HRESULT STDMETHODCALLTYPE UnregisterEventNotification(uint32_t eventCookie) override;
    HRESULT STDMETHODCALLTYPE CreateAdapterListByWorkload(DXCoreWorkload workload,
                                                          DXCoreRuntimeFilterFlags runtimeFilter,
                                                          DXCoreHardwareTypeFilterFlags hardwareTypeFilter,
                                                          REFIID riid, void **ppvAdapterList) override;

    Adapter *FindOrCreate(const LUID &luid, const AdapterData *data);
    ULONG ReleaseAdapter(Adapter *adapter, LONG *ref);
    UINT Snapshot(LUID *luids, UINT count);

private:
    HRESULT BuildList(const GUID *const *attributes, uint32_t count, REFIID riid, void **out);

    LONG m_ref;
    SRWLOCK m_lock;
    Adapter *m_adapters[DXCORE_MAX_ADAPTERS];
    Notification m_notifications[32];
    uint32_t m_next_cookie;
};

static SRWLOCK g_factory_lock = SRWLOCK_INIT;
static Factory *g_factory;

Adapter::Adapter(Factory *factory, const AdapterData &data) : m_ref(1), m_factory(factory), m_data(data)
{
    m_factory->AddRef();
}

Adapter::~Adapter()
{
    m_factory->Release();
}

HRESULT STDMETHODCALLTYPE Adapter::QueryInterface(REFIID riid, void **out)
{
    if (!out) return E_POINTER;
    if (IsEqualGUID(riid, IID_IUnknown) || IsEqualGUID(riid, IID_IDXCoreAdapter) ||
        IsEqualGUID(riid, IID_IDXCoreAdapter1))
    {
        AddRef();
        *out = static_cast<IDXCoreAdapter1 *>(this);
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE Adapter::Release()
{
    ULONG ref = m_factory->ReleaseAdapter(this, &m_ref);

    if (!ref) delete this;
    return ref;
}

bool STDMETHODCALLTYPE Adapter::IsValid()
{
    D3DKMT_OPENADAPTERFROMLUID open = {};

    open.AdapterLuid = m_data.Luid;
    if (!NT_SUCCESS(D3DKMTOpenAdapterFromLuid(&open)))
        return false;
    close_adapter(open.hAdapter);
    return true;
}

bool STDMETHODCALLTYPE Adapter::IsPropertySupported(DXCoreAdapterProperty property)
{
    if (property == DXCoreAdapterProperty::DriverVersion)
        return m_data.HaveDriverVersion != FALSE;
    return property_size(&m_data, property) != 0;
}

HRESULT STDMETHODCALLTYPE Adapter::GetPropertySize(DXCoreAdapterProperty property, size_t *bufferSize)
{
    if (!bufferSize) return E_POINTER;
    if (!IsPropertySupported(property))
    {
        *bufferSize = 0;
        return DXGI_ERROR_INVALID_CALL;
    }
    *bufferSize = property_size(&m_data, property);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE Adapter::GetProperty(DXCoreAdapterProperty property, size_t bufferSize, void *propertyData)
{
    union
    {
        LUID luid;
        LARGE_INTEGER version;
        DXCoreHardwareID id;
        DXCoreHardwareIDParts parts;
        uint32_t u32;
        uint64_t u64;
        bool flag;
    } value;
    const void *source = &value;
    size_t size;

    if (!IsPropertySupported(property)) return DXGI_ERROR_INVALID_CALL;
    if (!propertyData) return E_POINTER;
    size = property_size(&m_data, property);
    memset(&value, 0, sizeof(value));

    switch (property)
    {
        case DXCoreAdapterProperty::InstanceLuid: value.luid = m_data.Luid; break;
        case DXCoreAdapterProperty::DriverVersion: value.version = m_data.DriverVersion; break;
        case DXCoreAdapterProperty::DriverDescription: source = m_data.Description; break;
        case DXCoreAdapterProperty::HardwareID:
            value.id.vendorID = m_data.Ids.VendorID;
            value.id.deviceID = m_data.Ids.DeviceID;
            value.id.subSysID = (m_data.Ids.SubSystemID << 16) | (m_data.Ids.SubVendorID & 0xffff);
            value.id.revision = m_data.Ids.RevisionID;
            break;
        case DXCoreAdapterProperty::KmdModelVersion: value.u32 = m_data.KmdVersion; break;
        case DXCoreAdapterProperty::ComputePreemptionGranularity:
        case DXCoreAdapterProperty::GraphicsPreemptionGranularity:
            value.u32 = (property == DXCoreAdapterProperty::ComputePreemptionGranularity)
                        ? (uint32_t)m_data.Preemption.ComputePreemptionGranularity
                        : (uint32_t)m_data.Preemption.GraphicsPreemptionGranularity;
            memcpy(propertyData, &value, bufferSize < size ? bufferSize : size);
            return S_OK;
        case DXCoreAdapterProperty::DedicatedAdapterMemory: value.u64 = m_data.Segments.DedicatedVideoMemorySize; break;
        case DXCoreAdapterProperty::DedicatedSystemMemory: value.u64 = m_data.Segments.DedicatedSystemMemorySize; break;
        case DXCoreAdapterProperty::SharedSystemMemory: value.u64 = m_data.Segments.SharedSystemMemorySize; break;
        case DXCoreAdapterProperty::AcgCompatible: value.flag = m_data.Type.ACGSupported != 0; break;
        case DXCoreAdapterProperty::IsHardware: value.flag = !m_data.Type.SoftwareDevice; break;
        case DXCoreAdapterProperty::IsIntegrated: value.flag = m_data.Type.HybridIntegrated != 0; break;
        case DXCoreAdapterProperty::IsDetachable: value.flag = m_data.Type.Detachable != 0; break;
        case DXCoreAdapterProperty::HardwareIDParts:
            value.parts.vendorID = m_data.Ids.VendorID;
            value.parts.deviceID = m_data.Ids.DeviceID;
            value.parts.subSystemID = m_data.Ids.SubSystemID;
            value.parts.subVendorID = m_data.Ids.SubVendorID;
            value.parts.revisionID = m_data.Ids.RevisionID;
            break;
        case DXCoreAdapterProperty::PhysicalAdapterCount: value.u32 = m_data.PhysicalCount; break;
        case DXCoreAdapterProperty::AdapterEngineCount:
        case DXCoreAdapterProperty::AdapterEngineName:
            return E_INVALIDARG;
        default:
            return DXGI_ERROR_INVALID_CALL;
    }

    if (bufferSize < size) return E_INVALIDARG;
    memcpy(propertyData, source, size);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE Adapter::GetPropertyWithInput(DXCoreAdapterProperty property, size_t inputPropertyDetailsSize,
                                                        const void *inputPropertyDetails, size_t outputBufferSize,
                                                        void *outputBuffer)
{
    D3DKMT_OPENADAPTERFROMLUID open = {};
    D3DKMT_NODEMETADATA node;
    uint32_t physical, count;

    if (property != DXCoreAdapterProperty::AdapterEngineCount)
        return GetProperty(property, outputBufferSize, outputBuffer);
    if (!inputPropertyDetails || inputPropertyDetailsSize < sizeof(uint32_t) || !outputBuffer ||
        outputBufferSize < sizeof(uint32_t))
        return E_INVALIDARG;
    physical = *(const uint32_t *)inputPropertyDetails;
    if (physical >= m_data.PhysicalCount) return E_INVALIDARG;

    open.AdapterLuid = m_data.Luid;
    if (!NT_SUCCESS(D3DKMTOpenAdapterFromLuid(&open)))
        return DXGI_ERROR_DEVICE_REMOVED;
    for (count = 0; count < 0xffff; count++)
    {
        memset(&node, 0, sizeof(node));
        node.NodeOrdinalAndAdapterIndex = (physical << 16) | count;
        if (!NT_SUCCESS(query_adapter(open.hAdapter, KMTQAITYPE_NODEMETADATA, &node, sizeof(node))))
            break;
    }
    close_adapter(open.hAdapter);
    *(uint32_t *)outputBuffer = count;
    return S_OK;
}

bool STDMETHODCALLTYPE Adapter::IsQueryStateSupported(DXCoreAdapterState state)
{
    return state == DXCoreAdapterState::IsDriverUpdateInProgress ||
           state == DXCoreAdapterState::AdapterMemoryBudget;
}

HRESULT STDMETHODCALLTYPE Adapter::QueryState(DXCoreAdapterState state, size_t inputStateDetailsSize,
                                              const void *inputStateDetails, size_t outputBufferSize,
                                              void *outputBuffer)
{
    if (state == DXCoreAdapterState::IsDriverUpdateInProgress)
    {
        if (!outputBuffer || outputBufferSize < sizeof(bool)) return E_INVALIDARG;
        *(bool *)outputBuffer = false;
        return S_OK;
    }
    if (state == DXCoreAdapterState::AdapterMemoryBudget)
    {
        const DXCoreAdapterMemoryBudgetNodeSegmentGroup *input =
            (const DXCoreAdapterMemoryBudgetNodeSegmentGroup *)inputStateDetails;
        DXCoreAdapterMemoryBudget *output = (DXCoreAdapterMemoryBudget *)outputBuffer;
        D3DKMT_OPENADAPTERFROMLUID open = {};
        D3DKMT_QUERYVIDEOMEMORYINFO info = {};
        NTSTATUS status;

        if (!input || inputStateDetailsSize < sizeof(*input) || !output || outputBufferSize < sizeof(*output))
            return E_INVALIDARG;
        open.AdapterLuid = m_data.Luid;
        if (!NT_SUCCESS(D3DKMTOpenAdapterFromLuid(&open)))
            return DXGI_ERROR_DEVICE_REMOVED;
        info.hAdapter = open.hAdapter;
        info.MemorySegmentGroup = (input->segmentGroup == DXCoreSegmentGroup::Local)
                                  ? D3DKMT_MEMORY_SEGMENT_GROUP_LOCAL : D3DKMT_MEMORY_SEGMENT_GROUP_NON_LOCAL;
        info.PhysicalAdapterIndex = input->nodeIndex;
        status = D3DKMTQueryVideoMemoryInfo(&info);
        close_adapter(open.hAdapter);
        if (!NT_SUCCESS(status)) return E_INVALIDARG;
        output->budget = info.Budget;
        output->currentUsage = info.CurrentUsage;
        output->availableForReservation = info.AvailableForReservation;
        output->currentReservation = info.CurrentReservation;
        return S_OK;
    }
    return DXGI_ERROR_UNSUPPORTED;
}

HRESULT STDMETHODCALLTYPE Adapter::GetFactory(REFIID riid, void **ppvFactory)
{
    if (!ppvFactory) return E_POINTER;
    return m_factory->QueryInterface(riid, ppvFactory);
}

AdapterList::AdapterList(Factory *factory) : m_ref(1), m_factory(factory), m_count(0)
{
    m_factory->AddRef();
}

AdapterList::~AdapterList()
{
    UINT i;

    for (i = 0; i < m_count; i++)
        m_adapters[i]->Release();
    m_factory->Release();
}

HRESULT STDMETHODCALLTYPE AdapterList::QueryInterface(REFIID riid, void **out)
{
    if (!out) return E_POINTER;
    if (IsEqualGUID(riid, IID_IUnknown) || IsEqualGUID(riid, IID_IDXCoreAdapterList))
    {
        AddRef();
        *out = static_cast<IDXCoreAdapterList *>(this);
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE AdapterList::Release()
{
    ULONG ref = InterlockedDecrement(&m_ref);

    if (!ref) delete this;
    return ref;
}

HRESULT STDMETHODCALLTYPE AdapterList::GetAdapter(uint32_t index, REFIID riid, void **ppvAdapter)
{
    if (!ppvAdapter) return E_POINTER;
    *ppvAdapter = NULL;
    if (index >= m_count) return E_INVALIDARG;
    return m_adapters[index]->QueryInterface(riid, ppvAdapter);
}

bool STDMETHODCALLTYPE AdapterList::IsStale()
{
    LUID current[DXCORE_MAX_ADAPTERS];
    UINT count = m_factory->Snapshot(current, DXCORE_MAX_ADAPTERS), i, j;

    for (i = 0; i < m_count; i++)
    {
        const LUID &luid = m_adapters[i]->Data().Luid;
        for (j = 0; j < count; j++)
        {
            if (current[j].LowPart == luid.LowPart && current[j].HighPart == luid.HighPart)
                break;
        }
        if (j == count) return true;
    }
    return false;
}

HRESULT STDMETHODCALLTYPE AdapterList::GetFactory(REFIID riid, void **ppvFactory)
{
    if (!ppvFactory) return E_POINTER;
    return m_factory->QueryInterface(riid, ppvFactory);
}

static int preference_rank(const AdapterData &data, DXCoreAdapterPreference preference)
{
    switch (preference)
    {
        case DXCoreAdapterPreference::Hardware: return data.Type.SoftwareDevice ? 1 : 0;
        case DXCoreAdapterPreference::MinimumPower: return data.Type.HybridIntegrated ? 0 : 1;
        case DXCoreAdapterPreference::HighPerformance: return data.Type.HybridIntegrated ? 1 : 0;
        default: return 0;
    }
}

HRESULT STDMETHODCALLTYPE AdapterList::Sort(uint32_t numPreferences, const DXCoreAdapterPreference *preferences)
{
    UINT i, j, k;

    if (numPreferences && !preferences) return E_INVALIDARG;
    for (k = 0; k < numPreferences; k++)
    {
        if (!IsAdapterPreferenceSupported(preferences[k])) return E_INVALIDARG;
    }
    for (i = 1; i < m_count; i++)
    {
        Adapter *adapter = m_adapters[i];
        for (j = i; j > 0; j--)
        {
            int order = 0;
            for (k = 0; k < numPreferences && !order; k++)
                order = preference_rank(adapter->Data(), preferences[k]) -
                        preference_rank(m_adapters[j - 1]->Data(), preferences[k]);
            if (order >= 0) break;
            m_adapters[j] = m_adapters[j - 1];
        }
        m_adapters[j] = adapter;
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE Factory::QueryInterface(REFIID riid, void **out)
{
    if (!out) return E_POINTER;
    if (IsEqualGUID(riid, IID_IUnknown) || IsEqualGUID(riid, IID_IDXCoreAdapterFactory) ||
        IsEqualGUID(riid, IID_IDXCoreAdapterFactory1))
    {
        AddRef();
        *out = static_cast<IDXCoreAdapterFactory1 *>(this);
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE Factory::Release()
{
    ULONG ref;

    AcquireSRWLockExclusive(&g_factory_lock);
    ref = InterlockedDecrement(&m_ref);
    if (!ref && g_factory == this) g_factory = NULL;
    ReleaseSRWLockExclusive(&g_factory_lock);
    if (!ref) delete this;
    return ref;
}

UINT Factory::Snapshot(LUID *luids, UINT count)
{
    D3DKMT_ADAPTERINFO infos[DXCORE_MAX_ADAPTERS];
    UINT found = enum_adapters(infos, DXCORE_MAX_ADAPTERS), i;

    for (i = 0; i < found; i++)
    {
        if (i < count) luids[i] = infos[i].AdapterLuid;
        close_adapter(infos[i].hAdapter);
    }
    return found < count ? found : count;
}

Adapter *Factory::FindOrCreate(const LUID &luid, const AdapterData *data)
{
    Adapter *adapter = NULL;
    UINT i, slot = DXCORE_MAX_ADAPTERS;

    AcquireSRWLockExclusive(&m_lock);
    for (i = 0; i < DXCORE_MAX_ADAPTERS; i++)
    {
        Adapter *cached = m_adapters[i];
        if (!cached)
        {
            if (slot == DXCORE_MAX_ADAPTERS) slot = i;
            continue;
        }
        if (cached->Data().Luid.LowPart == luid.LowPart && cached->Data().Luid.HighPart == luid.HighPart)
        {
            adapter = cached;
            adapter->AddRef();
            break;
        }
    }
    if (!adapter && data && slot < DXCORE_MAX_ADAPTERS && (adapter = new Adapter(this, *data)))
        m_adapters[slot] = adapter;
    ReleaseSRWLockExclusive(&m_lock);
    return adapter;
}

ULONG Factory::ReleaseAdapter(Adapter *adapter, LONG *ref)
{
    ULONG count;
    UINT i;

    AcquireSRWLockExclusive(&m_lock);
    count = InterlockedDecrement(ref);
    for (i = 0; !count && i < DXCORE_MAX_ADAPTERS; i++)
    {
        if (m_adapters[i] == adapter) m_adapters[i] = NULL;
    }
    ReleaseSRWLockExclusive(&m_lock);
    return count;
}

HRESULT Factory::BuildList(const GUID *const *attributes, uint32_t count, REFIID riid, void **out)
{
    D3DKMT_ADAPTERINFO infos[DXCORE_MAX_ADAPTERS];
    AdapterList *list;
    UINT found, i, j;
    HRESULT hr;

    if (!(list = new AdapterList(this))) return E_OUTOFMEMORY;
    found = enum_adapters(infos, DXCORE_MAX_ADAPTERS);
    for (i = 0; i < found; i++)
    {
        AdapterData data;
        Adapter *adapter;

        if (read_adapter(infos[i].hAdapter, &infos[i].AdapterLuid, &data))
        {
            for (j = 0; j < count; j++)
            {
                if (!adapter_has_attribute(&data, *attributes[j])) break;
            }
            if (j == count && (adapter = FindOrCreate(data.Luid, &data)))
            {
                list->Add(adapter);
                adapter->Release();
            }
        }
        close_adapter(infos[i].hAdapter);
    }
    hr = list->QueryInterface(riid, out);
    list->Release();
    return hr;
}

HRESULT STDMETHODCALLTYPE Factory::CreateAdapterList(uint32_t numAttributes, const GUID *filterAttributes,
                                                     REFIID riid, void **ppvAdapterList)
{
    const GUID *attributes[DXCORE_MAX_ADAPTERS];
    uint32_t i;

    if (!ppvAdapterList) return E_POINTER;
    *ppvAdapterList = NULL;
    if (!numAttributes || !filterAttributes || numAttributes > DXCORE_MAX_ADAPTERS) return E_INVALIDARG;
    for (i = 0; i < numAttributes; i++)
        attributes[i] = &filterAttributes[i];
    return BuildList(attributes, numAttributes, riid, ppvAdapterList);
}

HRESULT STDMETHODCALLTYPE Factory::CreateAdapterListByWorkload(DXCoreWorkload workload,
                                                               DXCoreRuntimeFilterFlags runtimeFilter,
                                                               DXCoreHardwareTypeFilterFlags hardwareTypeFilter,
                                                               REFIID riid, void **ppvAdapterList)
{
    const GUID *attributes[4];
    uint32_t count = 0;

    if (!ppvAdapterList) return E_POINTER;
    *ppvAdapterList = NULL;
    switch (workload)
    {
        case DXCoreWorkload::Graphics:
            attributes[count++] = ((uint32_t)runtimeFilter & (uint32_t)DXCoreRuntimeFilterFlags::D3D12)
                                  ? &DXCORE_ADAPTER_ATTRIBUTE_D3D12_GRAPHICS : &DXCORE_ADAPTER_ATTRIBUTE_D3D11_GRAPHICS;
            break;
        case DXCoreWorkload::Compute:
            attributes[count++] = &DXCORE_ADAPTER_ATTRIBUTE_D3D12_CORE_COMPUTE;
            break;
        case DXCoreWorkload::Media:
            attributes[count++] = &DXCORE_ADAPTER_ATTRIBUTE_D3D12_GENERIC_MEDIA;
            break;
        case DXCoreWorkload::MachineLearning:
            attributes[count++] = &DXCORE_ADAPTER_ATTRIBUTE_D3D12_GENERIC_ML;
            break;
        default:
            return E_INVALIDARG;
    }
    if ((uint32_t)hardwareTypeFilter == (uint32_t)DXCoreHardwareTypeFilterFlags::GPU)
        attributes[count++] = &DXCORE_HARDWARE_TYPE_ATTRIBUTE_GPU;
    else if ((uint32_t)hardwareTypeFilter == (uint32_t)DXCoreHardwareTypeFilterFlags::ComputeAccelerator)
        attributes[count++] = &DXCORE_HARDWARE_TYPE_ATTRIBUTE_COMPUTE_ACCELERATOR;
    return BuildList(attributes, count, riid, ppvAdapterList);
}

HRESULT STDMETHODCALLTYPE Factory::GetAdapterByLuid(const LUID &adapterLUID, REFIID riid, void **ppvAdapter)
{
    D3DKMT_OPENADAPTERFROMLUID open = {};
    AdapterData data;
    Adapter *adapter;
    HRESULT hr;

    if (!ppvAdapter) return E_POINTER;
    *ppvAdapter = NULL;
    if (!(adapter = FindOrCreate(adapterLUID, NULL)))
    {
        open.AdapterLuid = adapterLUID;
        if (!NT_SUCCESS(D3DKMTOpenAdapterFromLuid(&open))) return E_INVALIDARG;
        if (read_adapter(open.hAdapter, &adapterLUID, &data))
            adapter = FindOrCreate(adapterLUID, &data);
        close_adapter(open.hAdapter);
        if (!adapter) return E_INVALIDARG;
    }
    hr = adapter->QueryInterface(riid, ppvAdapter);
    adapter->Release();
    return hr;
}

HRESULT STDMETHODCALLTYPE Factory::RegisterEventNotification(IUnknown *dxCoreObject,
                                                             DXCoreNotificationType notificationType,
                                                             PFN_DXCORE_NOTIFICATION_CALLBACK callbackFunction,
                                                             void *callbackContext, uint32_t *eventCookie)
{
    UINT i;

    if (!dxCoreObject || !callbackFunction || !eventCookie) return E_INVALIDARG;
    if (!IsNotificationTypeSupported(notificationType)) return E_INVALIDARG;

    AcquireSRWLockExclusive(&m_lock);
    for (i = 0; i < ARRAYSIZE(m_notifications); i++)
    {
        if (!m_notifications[i].Cookie)
        {
            if (!++m_next_cookie) ++m_next_cookie;
            m_notifications[i].Cookie = m_next_cookie;
            m_notifications[i].Object = dxCoreObject;
            m_notifications[i].Type = notificationType;
            m_notifications[i].Callback = callbackFunction;
            m_notifications[i].Context = callbackContext;
            *eventCookie = m_next_cookie;
            break;
        }
    }
    ReleaseSRWLockExclusive(&m_lock);
    return i < ARRAYSIZE(m_notifications) ? S_OK : E_OUTOFMEMORY;
}

HRESULT STDMETHODCALLTYPE Factory::UnregisterEventNotification(uint32_t eventCookie)
{
    HRESULT hr = E_INVALIDARG;
    UINT i;

    AcquireSRWLockExclusive(&m_lock);
    for (i = 0; eventCookie && i < ARRAYSIZE(m_notifications); i++)
    {
        if (m_notifications[i].Cookie == eventCookie)
        {
            memset(&m_notifications[i], 0, sizeof(m_notifications[i]));
            hr = S_OK;
            break;
        }
    }
    ReleaseSRWLockExclusive(&m_lock);
    return hr;
}

extern "C" HRESULT WINAPI DXCoreCreateAdapterFactory(REFIID riid, void **ppvFactory)
{
    Factory *factory;
    HRESULT hr;

    if (!ppvFactory) return E_POINTER;
    *ppvFactory = NULL;

    AcquireSRWLockExclusive(&g_factory_lock);
    if ((factory = g_factory))
        factory->AddRef();
    else if ((factory = g_factory = new Factory()))
        ;
    ReleaseSRWLockExclusive(&g_factory_lock);
    if (!factory) return E_OUTOFMEMORY;

    hr = factory->QueryInterface(riid, ppvFactory);
    factory->Release();
    return hr;
}
