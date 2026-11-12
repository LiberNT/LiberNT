/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     DeviceTopology API parity tests for audio endpoints and their KS filters
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include <apitest.h>

#define COBJMACROS
#include <windows.h>
#include <objbase.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <devicetopology.h>
#include <ks.h>
#include <ksmedia.h>
#include <setupapi.h>

#define FILTER_PREFIX L"{2}."
#define CONNECTOR_ID(Pin) (0x00010000u | (Pin))
#define SUBUNIT_ID(Node) (0x00020000u | (Node))
#define MAX_VISITED 32
#define MAX_LINKS 64

typedef struct _VISITED
{
    UINT Count;
    WCHAR *Ids[MAX_VISITED];
} VISITED;

static IMMDeviceEnumerator *Enumerator;
static const GUID PinSet = { STATIC_KSPROPSETID_Pin };
static const GUID TopologySet = { STATIC_KSPROPSETID_Topology };
static const GUID SpeakerType = { STATIC_KSNODETYPE_SPEAKER };
static const GUID MicrophoneType = { STATIC_KSNODETYPE_MICROPHONE };
static const GUID LineConnectorType = { STATIC_KSNODETYPE_LINE_CONNECTOR };
static const GUID SpdifType = { STATIC_KSNODETYPE_SPDIF_INTERFACE };
static const PROPERTYKEY InterfaceFriendlyName =
    { { 0x026e516e, 0xb814, 0x414b, { 0x83, 0xcd, 0x85, 0x6d, 0x6f, 0xef, 0x48, 0x22 } }, 2 };
static const PROPERTYKEY InterfaceEnabled =
    { { 0x026e516e, 0xb814, 0x414b, { 0x83, 0xcd, 0x85, 0x6d, 0x6f, 0xef, 0x48, 0x22 } }, 3 };
static const PROPERTYKEY InterfaceClassGuid =
    { { 0x026e516e, 0xb814, 0x414b, { 0x83, 0xcd, 0x85, 0x6d, 0x6f, 0xef, 0x48, 0x22 } }, 4 };
static const PROPERTYKEY InterfaceReferenceString =
    { { 0x026e516e, 0xb814, 0x414b, { 0x83, 0xcd, 0x85, 0x6d, 0x6f, 0xef, 0x48, 0x22 } }, 5 };
static const PROPERTYKEY DeviceInstanceId =
    { { 0x78c34fc8, 0x104a, 0x4aca, { 0x9e, 0xa4, 0x52, 0x4d, 0x52, 0x99, 0x6e, 0x57 } }, 256 };
static const PROPERTYKEY ItemNameDisplay =
    { { 0xb725f130, 0x47ef, 0x101a, { 0xa5, 0xf1, 0x02, 0x60, 0x8c, 0x9e, 0xeb, 0xac } }, 10 };
static const GUID AudioCategory = { STATIC_KSCATEGORY_AUDIO };
#define KS_LIST_SIZE 16384

static BOOL KsProperty(HANDLE Filter, void *Request, ULONG RequestSize, void *Data, ULONG DataSize, ULONG *Returned)
{
    DWORD Bytes = 0;
    BOOL Ret;

    Ret = DeviceIoControl(Filter, IOCTL_KS_PROPERTY, Request, RequestSize, Data, DataSize, &Bytes, NULL);
    if (Returned)
        *Returned = Bytes;
    return Ret;
}

static BOOL KsPinProperty(HANDLE Filter, ULONG Id, ULONG Pin, void *Data, ULONG DataSize, ULONG *Returned)
{
    KSP_PIN Request;

    ZeroMemory(&Request, sizeof(Request));
    Request.Property.Set = PinSet;
    Request.Property.Id = Id;
    Request.Property.Flags = KSPROPERTY_TYPE_GET;
    Request.PinId = Pin;
    return KsProperty(Filter, &Request, sizeof(Request), Data, DataSize, Returned);
}

static BOOL KsTopologyProperty(HANDLE Filter, ULONG Id, void *Data, ULONG DataSize)
{
    KSPROPERTY Request;

    Request.Set = TopologySet;
    Request.Id = Id;
    Request.Flags = KSPROPERTY_TYPE_GET;
    return KsProperty(Filter, &Request, sizeof(Request), Data, DataSize, NULL);
}

static void CheckKsList(HANDLE Filter, ULONG Id, ULONG ItemSize, const KSMULTIPLE_ITEM *List)
{
    KSPROPERTY Request;
    ULONG Returned = 0;
    BOOL Ret;

    ok(List->Size == sizeof(KSMULTIPLE_ITEM) + List->Count * ItemSize, "List %lu size %lu, count %lu\n",
       Id, List->Size, List->Count);
    Request.Set = TopologySet;
    Request.Id = Id;
    Request.Flags = KSPROPERTY_TYPE_GET;
    Ret = KsProperty(Filter, &Request, sizeof(Request), NULL, 0, &Returned);
    ok(!Ret && GetLastError() == ERROR_MORE_DATA, "List %lu size query %d, error %lu\n", Id, Ret, GetLastError());
    ok(Returned == List->Size, "List %lu size query returned %lu, expected %lu\n", Id, Returned, List->Size);
}

static BOOL KsNodeName(HANDLE Filter, ULONG Node, WCHAR *Name, ULONG NameSize)
{
    KSP_NODE Request;
    ULONG Returned;

    ZeroMemory(&Request, sizeof(Request));
    Request.Property.Set = TopologySet;
    Request.Property.Id = KSPROPERTY_TOPOLOGY_NAME;
    Request.Property.Flags = KSPROPERTY_TYPE_GET;
    Request.NodeId = Node;
    ZeroMemory(Name, NameSize);
    if (!KsProperty(Filter, &Request, sizeof(Request), Name, NameSize - sizeof(WCHAR), &Returned))
        return FALSE;
    return TRUE;
}

static void CheckIdSuffix(const WCHAR *Id, const WCHAR *DeviceId, UINT LocalId, int Line)
{
    WCHAR Expected[1024];

    swprintf(Expected, ARRAYSIZE(Expected), L"%s/%08x", DeviceId, LocalId);
    ok_(__FILE__, Line)(Id && !wcscmp(Id, Expected), "Got %s, expected %s\n",
                        wine_dbgstr_w(Id), wine_dbgstr_w(Expected));
}

static void CheckPartsList(IPart *Part, BOOL Incoming, const UINT *Expected, UINT ExpectedCount)
{
    IPartsList *List = (IPartsList *)(ULONG_PTR)0xdeadbeef;
    UINT Count = ~0u, i, Id;
    IPart *Item;
    HRESULT hr;

    if (Incoming)
        hr = IPart_EnumPartsIncoming(Part, &List);
    else
        hr = IPart_EnumPartsOutgoing(Part, &List);

    if (!ExpectedCount)
    {
        ok_hr(hr, E_NOTFOUND);
        ok(List == NULL, "List %p\n", List);
        if (SUCCEEDED(hr) && List)
            IPartsList_Release(List);
        return;
    }

    ok_hr(hr, S_OK);
    if (FAILED(hr) || !List)
        return;
    hr = IPartsList_GetCount(List, &Count);
    ok_hr(hr, S_OK);
    ok_eq_uint(Count, ExpectedCount);
    for (i = 0; i < Count && i < ExpectedCount; ++i)
    {
        Item = NULL;
        hr = IPartsList_GetPart(List, i, &Item);
        ok_hr(hr, S_OK);
        if (FAILED(hr) || !Item)
            continue;
        Id = ~0u;
        IPart_GetLocalId(Item, &Id);
        ok(Id == Expected[i], "%s[%u] 0x%08x, expected 0x%08x\n",
           Incoming ? "in" : "out", i, Id, Expected[i]);
        IPart_Release(Item);
    }
    Item = (IPart *)(ULONG_PTR)0xdeadbeef;
    hr = IPartsList_GetPart(List, Count, &Item);
    ok_hr(hr, E_INVALIDARG);
    ok(Item == NULL, "Item %p\n", Item);
    IPartsList_Release(List);
}

static UINT KsLinks(const KSMULTIPLE_ITEM *Connections, BOOL Incoming, UINT Part, UINT *Links)
{
    const KSTOPOLOGY_CONNECTION *Connection = (const KSTOPOLOGY_CONNECTION *)(Connections + 1);
    UINT Count = 0, From, To, i;

    for (i = 0; i < Connections->Count && Count < MAX_LINKS; ++i)
    {
        From = Connection[i].FromNode == KSFILTER_NODE ? CONNECTOR_ID(Connection[i].FromNodePin)
                                                       : SUBUNIT_ID(Connection[i].FromNode);
        To = Connection[i].ToNode == KSFILTER_NODE ? CONNECTOR_ID(Connection[i].ToNodePin)
                                                   : SUBUNIT_ID(Connection[i].ToNode);
        if (Incoming && To == Part)
            Links[Count++] = From;
        else if (!Incoming && From == Part)
            Links[Count++] = To;
    }
    return Count;
}

static void CheckCommonPart(IPart *Part, const WCHAR *DeviceId, UINT LocalId, PartType ExpectedType)
{
    IDeviceTopology *Topology = NULL;
    WCHAR *String;
    PartType Type = -1;
    UINT Id = ~0u;
    HRESULT hr;

    hr = IPart_GetLocalId(Part, &Id);
    ok_hr(hr, S_OK);
    ok(Id == LocalId, "Local id 0x%08x, expected 0x%08x\n", Id, LocalId);
    String = NULL;
    hr = IPart_GetGlobalId(Part, &String);
    ok_hr(hr, S_OK);
    CheckIdSuffix(String, DeviceId, LocalId, __LINE__);
    CoTaskMemFree(String);
    hr = IPart_GetPartType(Part, &Type);
    ok_hr(hr, S_OK);
    ok_eq_int(Type, ExpectedType);
    hr = IPart_GetTopologyObjects(Part, &Topology);
    ok_hr(hr, S_OK);
    if (SUCCEEDED(hr) && Topology)
    {
        String = NULL;
        hr = IDeviceTopology_GetDeviceId(Topology, &String);
        ok_hr(hr, S_OK);
        ok(String && !wcscmp(String, DeviceId), "Topology %s, expected %s\n",
           wine_dbgstr_w(String), wine_dbgstr_w(DeviceId));
        CoTaskMemFree(String);
        IDeviceTopology_Release(Topology);
    }
}

static void CheckNotConnected(IConnector *Conn)
{
    IConnector *To = (IConnector *)(ULONG_PTR)0xdeadbeef;
    WCHAR *String = (WCHAR *)(ULONG_PTR)0xdeadbeef;
    BOOL Connected = -1;
    HRESULT hr;

    hr = IConnector_IsConnected(Conn, &Connected);
    ok_hr(hr, S_OK);
    ok_eq_int(Connected, FALSE);
    hr = IConnector_GetConnectedTo(Conn, &To);
    ok_hr(hr, E_NOTFOUND);
    ok(To == NULL, "To %p\n", To);
    hr = IConnector_GetDeviceIdConnectedTo(Conn, &String);
    ok_hr(hr, E_NOTFOUND);
    ok(String == NULL, "String %p\n", String);
    String = (WCHAR *)(ULONG_PTR)0xdeadbeef;
    hr = IConnector_GetConnectorIdConnectedTo(Conn, &String);
    ok_hr(hr, E_NOTFOUND);
    ok(String == NULL, "String %p\n", String);
}

static IConnector *CheckConnected(IConnector *Conn, const WCHAR *PeerDeviceId, UINT PeerLocalId,
                                  ConnectorType Type, DataFlow Flow)
{
    IConnector *To = NULL;
    IPart *ToPart = NULL;
    WCHAR *String;
    BOOL Connected = -1;
    ConnectorType ToType = -1;
    DataFlow ToFlow = -1;
    UINT Id = ~0u;
    HRESULT hr;

    hr = IConnector_IsConnected(Conn, &Connected);
    ok_hr(hr, S_OK);
    ok_eq_int(Connected, TRUE);
    String = NULL;
    hr = IConnector_GetDeviceIdConnectedTo(Conn, &String);
    ok_hr(hr, S_OK);
    if (PeerDeviceId)
        ok(String && !wcscmp(String, PeerDeviceId), "Device id to %s, expected %s\n",
           wine_dbgstr_w(String), wine_dbgstr_w(PeerDeviceId));
    if (String)
    {
        WCHAR *ConnectorId = NULL;

        hr = IConnector_GetConnectorIdConnectedTo(Conn, &ConnectorId);
        ok_hr(hr, S_OK);
        CheckIdSuffix(ConnectorId, String, PeerLocalId, __LINE__);
        CoTaskMemFree(ConnectorId);
    }
    CoTaskMemFree(String);

    hr = IConnector_GetConnectedTo(Conn, &To);
    ok_hr(hr, S_OK);
    if (FAILED(hr) || !To)
        return NULL;
    hr = IConnector_GetType(To, &ToType);
    ok_hr(hr, S_OK);
    ok_eq_int(ToType, Type);
    hr = IConnector_GetDataFlow(To, &ToFlow);
    ok_hr(hr, S_OK);
    ok_eq_int(ToFlow, Flow == In ? Out : In);
    hr = IConnector_QueryInterface(To, &IID_IPart, (void **)&ToPart);
    ok_hr(hr, S_OK);
    if (SUCCEEDED(hr))
    {
        IPart_GetLocalId(ToPart, &Id);
        ok(Id == PeerLocalId, "Peer local id 0x%08x, expected 0x%08x\n", Id, PeerLocalId);
        IPart_Release(ToPart);
    }
    return To;
}

static void TestFilter(IDeviceTopology *Topology, const WCHAR *DeviceId, VISITED *Visited);

static void CheckBadIndexes(IDeviceTopology *Topology, UINT Connectors, UINT Subunits)
{
    IConnector *Conn = (IConnector *)(ULONG_PTR)0xdeadbeef;
    ISubUnit *Subunit = (ISubUnit *)(ULONG_PTR)0xdeadbeef;
    IPart *Part = (IPart *)(ULONG_PTR)0xdeadbeef;
    HRESULT hr;

    hr = IDeviceTopology_GetConnector(Topology, Connectors, &Conn);
    ok_hr(hr, E_INVALIDARG);
    ok(Conn == NULL, "Conn %p\n", Conn);
    hr = IDeviceTopology_GetSubunit(Topology, Subunits, &Subunit);
    ok_hr(hr, E_INVALIDARG);
    ok(Subunit == NULL, "Subunit %p\n", Subunit);
    hr = IDeviceTopology_GetPartById(Topology, 0x7fffffff, &Part);
    ok_hr(hr, E_INVALIDARG);
    ok(Part == NULL, "Part %p\n", Part);
    Part = (IPart *)(ULONG_PTR)0xdeadbeef;
    hr = IDeviceTopology_GetPartById(Topology, CONNECTOR_ID(Connectors), &Part);
    ok_hr(hr, E_NOTFOUND);
    ok(Part == NULL, "Part %p\n", Part);
    Part = (IPart *)(ULONG_PTR)0xdeadbeef;
    hr = IDeviceTopology_GetPartById(Topology, SUBUNIT_ID(Subunits), &Part);
    ok_hr(hr, E_NOTFOUND);
    ok(Part == NULL, "Part %p\n", Part);
    Part = (IPart *)(ULONG_PTR)0xdeadbeef;
    hr = IDeviceTopology_GetPartById(Topology, 0, &Part);
    ok_hr(hr, E_INVALIDARG);
    ok(Part == NULL, "Part %p\n", Part);
    Part = (IPart *)(ULONG_PTR)0xdeadbeef;
    hr = IDeviceTopology_GetPartById(Topology, 0x00030000, &Part);
    ok_hr(hr, E_INVALIDARG);
    ok(Part == NULL, "Part %p\n", Part);

    hr = IDeviceTopology_GetConnectorCount(Topology, NULL);
    ok_hr(hr, E_POINTER);
    hr = IDeviceTopology_GetSubunitCount(Topology, NULL);
    ok_hr(hr, E_POINTER);
    hr = IDeviceTopology_GetConnector(Topology, 0, NULL);
    ok_hr(hr, E_POINTER);
    hr = IDeviceTopology_GetDeviceId(Topology, NULL);
    ok_hr(hr, E_POINTER);
}

static BOOL Visit(VISITED *Visited, const WCHAR *Id)
{
    UINT i;

    for (i = 0; i < Visited->Count; ++i)
    {
        if (!wcscmp(Visited->Ids[i], Id))
            return FALSE;
    }
    if (Visited->Count < MAX_VISITED)
        Visited->Ids[Visited->Count++] = _wcsdup(Id);
    return TRUE;
}

static void ExpectString(IPropertyStore *Store, const PROPERTYKEY *Key, const WCHAR *Expected, BOOL IgnoreCase)
{
    PROPVARIANT Value;
    HRESULT hr;

    PropVariantInit(&Value);
    hr = IPropertyStore_GetValue(Store, Key, &Value);
    ok_hr(hr, S_OK);
    ok(Value.vt == VT_LPWSTR && Value.pwszVal && Expected &&
       !(IgnoreCase ? _wcsicmp(Value.pwszVal, Expected) : wcscmp(Value.pwszVal, Expected)),
       "%s,%lu vt %u %s, expected %s\n", wine_dbgstr_guid(&Key->fmtid), Key->pid, Value.vt,
       Value.vt == VT_LPWSTR ? wine_dbgstr_w(Value.pwszVal) : "", wine_dbgstr_w(Expected));
    PropVariantClear(&Value);
}

static void CheckFilterProperties(IPropertyStore *Store, const WCHAR *Path)
{
    SP_DEVICE_INTERFACE_DATA Data;
    WCHAR FriendlyName[256], InstanceId[256], *Out;
    const WCHAR *Class, *Reference;
    PROPVARIANT Value;
    HDEVINFO Set;
    HKEY Key;
    DWORD Size, Type;
    HRESULT hr;

    FriendlyName[0] = 0;
    Set = SetupDiCreateDeviceInfoList(NULL, NULL);
    Data.cbSize = sizeof(Data);
    ok(SetupDiOpenDeviceInterfaceW(Set, Path, 0, &Data), "SetupDiOpenDeviceInterfaceW failed: %lu\n", GetLastError());
    Key = SetupDiOpenDeviceInterfaceRegKey(Set, &Data, 0, KEY_READ);
    ok(Key != INVALID_HANDLE_VALUE, "SetupDiOpenDeviceInterfaceRegKey failed: %lu\n", GetLastError());
    if (Key != INVALID_HANDLE_VALUE)
    {
        Size = sizeof(FriendlyName) - sizeof(WCHAR);
        ok(!RegQueryValueExW(Key, L"FriendlyName", NULL, &Type, (BYTE *)FriendlyName, &Size), "No FriendlyName\n");
        FriendlyName[Size / sizeof(WCHAR)] = 0;
        RegCloseKey(Key);
    }
    SetupDiDestroyDeviceInfoList(Set);

    Class = wcsstr(Path, L"#{");
    Reference = Class ? wcschr(Class, '}') : NULL;
    ok(Class && Reference && Reference[1] == '\\', "Path %s\n", wine_dbgstr_w(Path));
    if (!Class || !Reference || Reference[1] != '\\')
        return;
    lstrcpynW(InstanceId, Path + 4, min((size_t)(Class - Path - 4) + 1, ARRAYSIZE(InstanceId)));
    for (Out = InstanceId; *Out; ++Out)
    {
        if (*Out == '#')
            *Out = '\\';
    }

    ExpectString(Store, &InterfaceFriendlyName, FriendlyName, FALSE);
    ExpectString(Store, &ItemNameDisplay, FriendlyName, FALSE);
    ExpectString(Store, &InterfaceReferenceString, Reference + 2, FALSE);
    ExpectString(Store, &DeviceInstanceId, InstanceId, TRUE);

    PropVariantInit(&Value);
    hr = IPropertyStore_GetValue(Store, &InterfaceClassGuid, &Value);
    ok_hr(hr, S_OK);
    ok(Value.vt == VT_CLSID && Value.puuid && IsEqualGUID(Value.puuid, &AudioCategory), "Class vt %u\n", Value.vt);
    PropVariantClear(&Value);
    PropVariantInit(&Value);
    hr = IPropertyStore_GetValue(Store, &InterfaceEnabled, &Value);
    ok_hr(hr, S_OK);
    ok(Value.vt == VT_BOOL && (USHORT)Value.boolVal == 0xff, "Enabled vt %u 0x%x\n", Value.vt, (USHORT)Value.boolVal);
    PropVariantClear(&Value);
}

static void TestFilterDevice(const WCHAR *DeviceId)
{
    IDeviceTopology *Topology = NULL;
    IPropertyStore *Store = NULL;
    IMMEndpoint *MMEndpoint = NULL;
    IAudioClient *Client = NULL;
    IMMDevice *Device = NULL;
    WCHAR *String = NULL;
    DWORD State = 0;
    HRESULT hr;

    hr = IMMDeviceEnumerator_GetDevice(Enumerator, DeviceId, &Device);
    ok_hr(hr, S_OK);
    if (FAILED(hr) || !Device)
        return;
    hr = IMMDevice_GetState(Device, &State);
    ok_hr(hr, S_OK);
    ok_eq_ulong(State, (ULONG)DEVICE_STATE_ACTIVE);
    hr = IMMDevice_GetId(Device, &String);
    ok_hr(hr, S_OK);
    ok(String && !wcscmp(String, DeviceId), "Id %s, expected %s\n", wine_dbgstr_w(String), wine_dbgstr_w(DeviceId));
    CoTaskMemFree(String);
    hr = IMMDevice_Activate(Device, &IID_IDeviceTopology, CLSCTX_ALL, NULL, (void **)&Topology);
    ok_hr(hr, S_OK);
    if (SUCCEEDED(hr) && Topology)
    {
        String = NULL;
        hr = IDeviceTopology_GetDeviceId(Topology, &String);
        ok_hr(hr, S_OK);
        ok(String && !wcscmp(String, DeviceId), "Id %s, expected %s\n", wine_dbgstr_w(String), wine_dbgstr_w(DeviceId));
        CoTaskMemFree(String);
        IDeviceTopology_Release(Topology);
    }
    MMEndpoint = (IMMEndpoint *)(ULONG_PTR)0xdeadbeef;
    hr = IMMDevice_QueryInterface(Device, &IID_IMMEndpoint, (void **)&MMEndpoint);
    ok_hr(hr, E_NOINTERFACE);
    ok(MMEndpoint == NULL, "IMMEndpoint %p\n", MMEndpoint);
    if (SUCCEEDED(hr) && MMEndpoint)
        IMMEndpoint_Release(MMEndpoint);
    hr = IMMDevice_Activate(Device, &IID_IAudioClient, CLSCTX_ALL, NULL, (void **)&Client);
    ok_hr(hr, E_NOINTERFACE);
    if (SUCCEEDED(hr))
        IAudioClient_Release(Client);
    hr = IMMDevice_OpenPropertyStore(Device, STGM_READ, &Store);
    ok_hr(hr, S_OK);
    if (SUCCEEDED(hr))
    {
        CheckFilterProperties(Store, DeviceId + 4);
        IPropertyStore_Release(Store);
    }
    IMMDevice_Release(Device);
}

static void TestFilterConnector(IDeviceTopology *Topology, HANDLE Filter, const WCHAR *DeviceId,
                                const KSMULTIPLE_ITEM *Connections, UINT Pin, VISITED *Visited)
{
    BYTE PhysicalBuffer[2048];
    KSPIN_PHYSICALCONNECTION *Physical = (KSPIN_PHYSICALCONNECTION *)PhysicalBuffer;
    WCHAR KsName[512], *Name;
    KSPIN_DATAFLOW KsFlow = 0;
    KSPIN_COMMUNICATION Communication = 0;
    GUID Category = GUID_NULL, SubType;
    ConnectorType Type = -1, ExpectedType;
    DataFlow Flow = -1, ExpectedFlow;
    UINT Links[MAX_LINKS], LinkCount;
    IConnector *Conn = NULL, *To;
    IPart *Part = NULL, *ById = NULL;
    BOOL HasPhysical, HasName;
    ULONG Returned;
    HRESULT hr;

    hr = IDeviceTopology_GetConnector(Topology, Pin, &Conn);
    ok_hr(hr, S_OK);
    if (FAILED(hr) || !Conn)
        return;
    hr = IConnector_QueryInterface(Conn, &IID_IPart, (void **)&Part);
    ok_hr(hr, S_OK);
    if (FAILED(hr))
    {
        IConnector_Release(Conn);
        return;
    }
    CheckCommonPart(Part, DeviceId, CONNECTOR_ID(Pin), Connector);

    ok(KsPinProperty(Filter, KSPROPERTY_PIN_DATAFLOW, Pin, &KsFlow, sizeof(KsFlow), NULL),
       "KSPROPERTY_PIN_DATAFLOW %u failed: %lu\n", Pin, GetLastError());
    ok(KsPinProperty(Filter, KSPROPERTY_PIN_COMMUNICATION, Pin, &Communication, sizeof(Communication), NULL),
       "KSPROPERTY_PIN_COMMUNICATION %u failed: %lu\n", Pin, GetLastError());
    if (!KsPinProperty(Filter, KSPROPERTY_PIN_CATEGORY, Pin, &Category, sizeof(Category), NULL))
        Category = GUID_NULL;
    ZeroMemory(PhysicalBuffer, sizeof(PhysicalBuffer));
    HasPhysical = KsPinProperty(Filter, KSPROPERTY_PIN_PHYSICALCONNECTION, Pin,
                                Physical, sizeof(PhysicalBuffer) - sizeof(WCHAR), &Returned);
    ZeroMemory(KsName, sizeof(KsName));
    HasName = KsPinProperty(Filter, KSPROPERTY_PIN_NAME, Pin, KsName, sizeof(KsName) - sizeof(WCHAR), NULL);

    ExpectedFlow = KsFlow == KSPIN_DATAFLOW_IN ? In : Out;
    if (Communication == KSPIN_COMMUNICATION_SINK || Communication == KSPIN_COMMUNICATION_SOURCE ||
        Communication == KSPIN_COMMUNICATION_BOTH)
        ExpectedType = Software_IO;
    else if (HasPhysical)
        ExpectedType = Software_Fixed;
    else if (IsEqualGUID(&Category, &SpeakerType) || IsEqualGUID(&Category, &MicrophoneType) ||
             IsEqualGUID(&Category, &LineConnectorType) || IsEqualGUID(&Category, &SpdifType))
        ExpectedType = Physical_External;
    else
        ExpectedType = Physical_Internal;

    hr = IConnector_GetType(Conn, &Type);
    ok_hr(hr, S_OK);
    ok(Type == ExpectedType, "Pin %u type %d, expected %d (communication %d)\n", Pin, Type, ExpectedType, Communication);
    hr = IConnector_GetDataFlow(Conn, &Flow);
    ok_hr(hr, S_OK);
    ok(Flow == ExpectedFlow, "Pin %u flow %d, KS dataflow %d\n", Pin, Flow, KsFlow);
    SubType = GUID_NULL;
    hr = IPart_GetSubType(Part, &SubType);
    ok_hr(hr, S_OK);
    ok(IsEqualGUID(&SubType, &Category), "Pin %u subtype %s, category %s\n", Pin,
       wine_dbgstr_guid(&SubType), wine_dbgstr_guid(&Category));
    Name = NULL;
    hr = IPart_GetName(Part, &Name);
    ok_hr(hr, S_OK);
    ok(Name && !wcscmp(Name, HasName ? KsName : L""), "Pin %u name %s, KS name %d %s\n", Pin,
       wine_dbgstr_w(Name), HasName, wine_dbgstr_w(KsName));
    CoTaskMemFree(Name);

    LinkCount = KsLinks(Connections, TRUE, CONNECTOR_ID(Pin), Links);
    CheckPartsList(Part, TRUE, Links, LinkCount);
    LinkCount = KsLinks(Connections, FALSE, CONNECTOR_ID(Pin), Links);
    CheckPartsList(Part, FALSE, Links, LinkCount);

    hr = IDeviceTopology_GetPartById(Topology, CONNECTOR_ID(Pin), &ById);
    ok_hr(hr, S_OK);
    if (SUCCEEDED(hr) && ById)
    {
        UINT Id = ~0u;

        IPart_GetLocalId(ById, &Id);
        ok(Id == CONNECTOR_ID(Pin), "GetPartById 0x%08x\n", Id);
        IPart_Release(ById);
    }

    if (Type == Software_IO)
        CheckNotConnected(Conn);
    else if (Type == Software_Fixed)
    {
        trace("%s pin %u physical connection to %s pin %lu\n", wine_dbgstr_w(DeviceId), Pin,
              wine_dbgstr_w(Physical->SymbolicLinkName), Physical->Pin);
        To = CheckConnected(Conn, NULL, CONNECTOR_ID(Physical->Pin), Type, Flow);
        if (To)
        {
            IPart *ToPart = NULL;
            IDeviceTopology *ToTopology = NULL;
            WCHAR *ToId = NULL;

            if (SUCCEEDED(IConnector_QueryInterface(To, &IID_IPart, (void **)&ToPart)) &&
                SUCCEEDED(IPart_GetTopologyObjects(ToPart, &ToTopology)) &&
                SUCCEEDED(IDeviceTopology_GetDeviceId(ToTopology, &ToId)))
            {
                ok(!wcsncmp(ToId, FILTER_PREFIX, 4), "Peer id %s\n", wine_dbgstr_w(ToId));
                TestFilter(ToTopology, ToId, Visited);
            }
            CoTaskMemFree(ToId);
            if (ToTopology)
                IDeviceTopology_Release(ToTopology);
            if (ToPart)
                IPart_Release(ToPart);
            IConnector_Release(To);
        }
    }
    else
    {
        BOOL Connected = -1;

        hr = IConnector_IsConnected(Conn, &Connected);
        ok_hr(hr, S_OK);
        trace("%s pin %u type %d connected %d\n", wine_dbgstr_w(DeviceId), Pin, Type, Connected);
        if (Connected)
        {
            To = CheckConnected(Conn, NULL, CONNECTOR_ID(0), Type, Flow);
            if (To)
                IConnector_Release(To);
        }
    }

    IPart_Release(Part);
    IConnector_Release(Conn);
}

static void TestFilterSubunit(IDeviceTopology *Topology, HANDLE Filter, const WCHAR *DeviceId,
                              const KSMULTIPLE_ITEM *Connections, const GUID *NodeType, UINT Node)
{
    UINT Links[MAX_LINKS], LinkCount;
    ISubUnit *Unit = NULL;
    IPart *Part = NULL;
    WCHAR KsName[512], *Name;
    GUID SubType;
    BOOL HasName;
    HRESULT hr;

    hr = IDeviceTopology_GetSubunit(Topology, Node, &Unit);
    ok_hr(hr, S_OK);
    if (FAILED(hr) || !Unit)
        return;
    hr = ISubUnit_QueryInterface(Unit, &IID_IPart, (void **)&Part);
    ok_hr(hr, S_OK);
    if (SUCCEEDED(hr))
    {
        CheckCommonPart(Part, DeviceId, SUBUNIT_ID(Node), Subunit);
        SubType = GUID_NULL;
        hr = IPart_GetSubType(Part, &SubType);
        ok_hr(hr, S_OK);
        ok(IsEqualGUID(&SubType, NodeType), "Node %u subtype %s, expected %s\n", Node,
           wine_dbgstr_guid(&SubType), wine_dbgstr_guid(NodeType));
        HasName = KsNodeName(Filter, Node, KsName, sizeof(KsName));
        Name = NULL;
        hr = IPart_GetName(Part, &Name);
        ok_hr(hr, S_OK);
        ok(Name && !wcscmp(Name, HasName ? KsName : L""), "Node %u name %s, KS name %d %s\n", Node,
           wine_dbgstr_w(Name), HasName, wine_dbgstr_w(KsName));
        CoTaskMemFree(Name);
        LinkCount = KsLinks(Connections, TRUE, SUBUNIT_ID(Node), Links);
        CheckPartsList(Part, TRUE, Links, LinkCount);
        LinkCount = KsLinks(Connections, FALSE, SUBUNIT_ID(Node), Links);
        CheckPartsList(Part, FALSE, Links, LinkCount);
        IPart_Release(Part);
    }
    ISubUnit_Release(Unit);
}

static void TestFilter(IDeviceTopology *Topology, const WCHAR *DeviceId, VISITED *Visited)
{
    KSMULTIPLE_ITEM *Nodes, *Connections;
    UINT Count = ~0u, i;
    KSPROPERTY Request;
    ULONG PinCount = 0;
    HANDLE Filter;
    HRESULT hr;

    if (!Visit(Visited, DeviceId))
        return;
    trace("filter %s\n", wine_dbgstr_w(DeviceId));
    TestFilterDevice(DeviceId);

    Filter = CreateFileW(DeviceId + 4, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL, NULL);
    ok(Filter != INVALID_HANDLE_VALUE, "Opening %s failed: %lu\n", wine_dbgstr_w(DeviceId + 4), GetLastError());
    if (Filter == INVALID_HANDLE_VALUE)
        return;

    Nodes = HeapAlloc(GetProcessHeap(), 0, KS_LIST_SIZE);
    Connections = HeapAlloc(GetProcessHeap(), 0, KS_LIST_SIZE);
    Request.Set = PinSet;
    Request.Id = KSPROPERTY_PIN_CTYPES;
    Request.Flags = KSPROPERTY_TYPE_GET;
    ok(KsProperty(Filter, &Request, sizeof(Request), &PinCount, sizeof(PinCount), NULL),
       "KSPROPERTY_PIN_CTYPES failed: %lu\n", GetLastError());
    ZeroMemory(Nodes, KS_LIST_SIZE);
    ok(KsTopologyProperty(Filter, KSPROPERTY_TOPOLOGY_NODES, Nodes, KS_LIST_SIZE),
       "KSPROPERTY_TOPOLOGY_NODES failed: %lu\n", GetLastError());
    ZeroMemory(Connections, KS_LIST_SIZE);
    ok(KsTopologyProperty(Filter, KSPROPERTY_TOPOLOGY_CONNECTIONS, Connections, KS_LIST_SIZE),
       "KSPROPERTY_TOPOLOGY_CONNECTIONS failed: %lu\n", GetLastError());

    CheckKsList(Filter, KSPROPERTY_TOPOLOGY_NODES, sizeof(GUID), Nodes);
    CheckKsList(Filter, KSPROPERTY_TOPOLOGY_CONNECTIONS, sizeof(KSTOPOLOGY_CONNECTION), Connections);

    hr = IDeviceTopology_GetConnectorCount(Topology, &Count);
    ok_hr(hr, S_OK);
    ok_eq_uint(Count, PinCount);
    for (i = 0; i < Count; ++i)
        TestFilterConnector(Topology, Filter, DeviceId, Connections, i, Visited);
    CheckBadIndexes(Topology, Count, Nodes->Count);

    Count = ~0u;
    hr = IDeviceTopology_GetSubunitCount(Topology, &Count);
    ok_hr(hr, S_OK);
    ok_eq_uint(Count, Nodes->Count);
    for (i = 0; i < Count && i < Nodes->Count; ++i)
        TestFilterSubunit(Topology, Filter, DeviceId, Connections, (const GUID *)(Nodes + 1) + i, i);

    HeapFree(GetProcessHeap(), 0, Connections);
    HeapFree(GetProcessHeap(), 0, Nodes);
    CloseHandle(Filter);
}

static void TestEndpoint(IMMDevice *Endpoint)
{
    IDeviceTopology *Topology = NULL, *ToTopology;
    IConnector *Conn = NULL, *To;
    IPart *Part = NULL, *ToPart;
    IMMEndpoint *MMEndpoint;
    VISITED Visited = {0};
    WCHAR *Id = NULL, *String, *PeerId = NULL;
    EDataFlow EndpointFlow = eRender;
    ConnectorType Type = -1;
    DataFlow Flow = -1;
    GUID SubType;
    UINT Count, i, PeerLocalId;
    HRESULT hr;

    hr = IMMDevice_GetId(Endpoint, &Id);
    ok_hr(hr, S_OK);
    if (FAILED(hr))
        return;
    if (SUCCEEDED(IMMDevice_QueryInterface(Endpoint, &IID_IMMEndpoint, (void **)&MMEndpoint)))
    {
        IMMEndpoint_GetDataFlow(MMEndpoint, &EndpointFlow);
        IMMEndpoint_Release(MMEndpoint);
    }
    trace("endpoint %s flow %d\n", wine_dbgstr_w(Id), EndpointFlow);

    hr = IMMDevice_Activate(Endpoint, &IID_IDeviceTopology, CLSCTX_ALL, NULL, (void **)&Topology);
    ok_hr(hr, S_OK);
    if (FAILED(hr))
        goto done;

    String = NULL;
    hr = IDeviceTopology_GetDeviceId(Topology, &String);
    ok_hr(hr, S_OK);
    ok(String && !wcscmp(String, Id), "Device id %s\n", wine_dbgstr_w(String));
    CoTaskMemFree(String);
    Count = ~0u;
    hr = IDeviceTopology_GetConnectorCount(Topology, &Count);
    ok_hr(hr, S_OK);
    ok_eq_uint(Count, 1);
    Count = ~0u;
    hr = IDeviceTopology_GetSubunitCount(Topology, &Count);
    ok_hr(hr, S_OK);
    ok_eq_uint(Count, 0);
    CheckBadIndexes(Topology, 1, 0);

    hr = IDeviceTopology_GetConnector(Topology, 0, &Conn);
    ok_hr(hr, S_OK);
    if (FAILED(hr))
        goto done;
    hr = IConnector_QueryInterface(Conn, &IID_IPart, (void **)&Part);
    ok_hr(hr, S_OK);
    if (SUCCEEDED(hr))
    {
        CheckCommonPart(Part, Id, CONNECTOR_ID(0), Connector);
        String = NULL;
        hr = IPart_GetName(Part, &String);
        ok_hr(hr, S_OK);
        ok(String && !wcscmp(String, L""), "Name %s\n", wine_dbgstr_w(String));
        CoTaskMemFree(String);
        SubType = SpeakerType;
        hr = IPart_GetSubType(Part, &SubType);
        ok_hr(hr, S_OK);
        ok(IsEqualGUID(&SubType, &GUID_NULL), "Subtype %s\n", wine_dbgstr_guid(&SubType));
        Count = ~0u;
        hr = IPart_GetControlInterfaceCount(Part, &Count);
        ok_hr(hr, S_OK);
        ok_eq_uint(Count, 0);
        CheckPartsList(Part, TRUE, NULL, 0);
        CheckPartsList(Part, FALSE, NULL, 0);
        IPart_Release(Part);
    }

    hr = IConnector_GetType(Conn, NULL);
    ok_hr(hr, E_POINTER);
    hr = IConnector_GetDataFlow(Conn, NULL);
    ok_hr(hr, E_POINTER);
    hr = IConnector_IsConnected(Conn, NULL);
    ok_hr(hr, E_POINTER);
    hr = IConnector_GetConnectedTo(Conn, NULL);
    ok_hr(hr, E_POINTER);
    hr = IConnector_GetDeviceIdConnectedTo(Conn, NULL);
    ok_hr(hr, E_POINTER);
    hr = IConnector_GetConnectorIdConnectedTo(Conn, NULL);
    ok_hr(hr, E_POINTER);

    hr = IConnector_GetDataFlow(Conn, &Flow);
    ok_hr(hr, S_OK);
    ok_eq_int(Flow, EndpointFlow == eRender ? In : Out);
    hr = IConnector_GetType(Conn, &Type);
    ok_hr(hr, S_OK);
    trace("endpoint connector type %d\n", Type);

    hr = IConnector_GetDeviceIdConnectedTo(Conn, &PeerId);
    ok_hr(hr, S_OK);
    if (FAILED(hr) || !PeerId)
        goto done;
    ok(!wcsncmp(PeerId, FILTER_PREFIX, 4), "Peer id %s\n", wine_dbgstr_w(PeerId));
    String = NULL;
    hr = IConnector_GetConnectorIdConnectedTo(Conn, &String);
    ok_hr(hr, S_OK);
    PeerLocalId = ~0u;
    if (String && wcslen(String) > wcslen(PeerId) + 1)
        PeerLocalId = wcstoul(String + wcslen(PeerId) + 1, NULL, 16);
    ok((PeerLocalId & 0xffff0000) == CONNECTOR_ID(0), "Connector id %s\n", wine_dbgstr_w(String));
    CheckIdSuffix(String, PeerId, PeerLocalId, __LINE__);
    CoTaskMemFree(String);

    To = CheckConnected(Conn, PeerId, PeerLocalId, Type, Flow);
    if (!To)
        goto done;
    String = NULL;
    hr = IConnector_GetDeviceIdConnectedTo(To, &String);
    ok_hr(hr, S_OK);
    ok(String && !wcscmp(String, Id), "Back device id %s\n", wine_dbgstr_w(String));
    CoTaskMemFree(String);
    String = NULL;
    hr = IConnector_GetConnectorIdConnectedTo(To, &String);
    ok_hr(hr, S_OK);
    CheckIdSuffix(String, Id, CONNECTOR_ID(0), __LINE__);
    CoTaskMemFree(String);

    ToPart = NULL;
    ToTopology = NULL;
    if (SUCCEEDED(IConnector_QueryInterface(To, &IID_IPart, (void **)&ToPart)))
    {
        hr = IPart_GetTopologyObjects(ToPart, &ToTopology);
        ok_hr(hr, S_OK);
        if (SUCCEEDED(hr) && ToTopology)
        {
            TestFilter(ToTopology, PeerId, &Visited);
            IDeviceTopology_Release(ToTopology);
        }
        IPart_Release(ToPart);
    }
    IConnector_Release(To);

done:
    CoTaskMemFree(PeerId);
    if (Conn)
        IConnector_Release(Conn);
    if (Topology)
        IDeviceTopology_Release(Topology);
    for (i = 0; i < Visited.Count; ++i)
        free(Visited.Ids[i]);
    CoTaskMemFree(Id);
}

START_TEST(devicetopology)
{
    IMMDeviceCollection *Collection = NULL;
    IMMDevice *Endpoint, *Device;
    UINT Count = 0, i;
    HRESULT hr;

    hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    ok_hr(hr, S_OK);

    hr = CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_INPROC_SERVER,
                          &IID_IMMDeviceEnumerator, (void **)&Enumerator);
    ok_hr(hr, S_OK);
    if (FAILED(hr))
        goto done;

    Device = (IMMDevice *)(ULONG_PTR)0xdeadbeef;
    hr = IMMDeviceEnumerator_GetDevice(Enumerator, FILTER_PREFIX L"\\\\?\\none#0#{6994ad04-93ef-11d0-a3cc-00a0c9223196}\\none", &Device);
    ok_hr(hr, (HRESULT)0xe0000225);
    ok(Device == NULL, "Device %p\n", Device);

    hr = IMMDeviceEnumerator_EnumAudioEndpoints(Enumerator, eAll, DEVICE_STATE_ACTIVE, &Collection);
    ok_hr(hr, S_OK);
    if (SUCCEEDED(hr))
        IMMDeviceCollection_GetCount(Collection, &Count);
    if (!Count)
        skip("No active audio endpoint\n");

    for (i = 0; i < Count; ++i)
    {
        Endpoint = NULL;
        if (FAILED(IMMDeviceCollection_Item(Collection, i, &Endpoint)))
            continue;
        TestEndpoint(Endpoint);
        IMMDevice_Release(Endpoint);
    }

    if (Collection)
        IMMDeviceCollection_Release(Collection);
    IMMDeviceEnumerator_Release(Enumerator);
done:
    CoUninitialize();
}
