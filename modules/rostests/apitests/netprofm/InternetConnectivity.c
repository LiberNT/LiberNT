/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests the NA_InternetConnectivityV4/V6 properties of networks and connections
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define COBJMACROS
#include <apitest.h>
#include <ocidl.h>
#include <initguid.h>
#include <netlistmgr.h>

static void
CheckBag(
    _In_ IUnknown *Object,
    _In_ NLM_CONNECTIVITY Connectivity,
    _In_ const char *Kind)
{
    IPropertyBag *Bag = NULL;
    VARIANT Value;
    HRESULT hr;

    hr = IUnknown_QueryInterface(Object, &IID_IPropertyBag, (void **)&Bag);
    ok(hr == S_OK, "%s: QueryInterface(IPropertyBag) returned 0x%lx\n", Kind, hr);
    if (FAILED(hr))
        return;

    VariantInit(&Value);
    hr = IPropertyBag_Read(Bag, NA_InternetConnectivityV4, &Value, NULL);
    ok(hr == S_OK, "%s: Read(V4) returned 0x%lx\n", Kind, hr);
    if (hr == S_OK)
    {
        ok(V_VT(&Value) == VT_UI4 || V_VT(&Value) == VT_I4, "%s: V4 type %u\n", Kind, V_VT(&Value));
        if (Connectivity & NLM_CONNECTIVITY_IPV4_INTERNET)
            ok(!(V_UI4(&Value) & NLM_INTERNET_CONNECTIVITY_WEBHIJACK), "%s: V4 0x%lx on an Internet connection\n",
               Kind, V_UI4(&Value));
        VariantClear(&Value);
    }

    VariantInit(&Value);
    hr = IPropertyBag_Read(Bag, NA_InternetConnectivityV6, &Value, NULL);
    ok(hr == S_OK, "%s: Read(V6) returned 0x%lx\n", Kind, hr);
    if (hr == S_OK)
    {
        ok(V_VT(&Value) == VT_UI4 || V_VT(&Value) == VT_I4, "%s: V6 type %u\n", Kind, V_VT(&Value));
        if (Connectivity & NLM_CONNECTIVITY_IPV6_INTERNET)
            ok(!(V_UI4(&Value) & NLM_INTERNET_CONNECTIVITY_WEBHIJACK), "%s: V6 0x%lx on an Internet connection\n",
               Kind, V_UI4(&Value));
        VariantClear(&Value);
    }

    IPropertyBag_Release(Bag);
}

START_TEST(InternetConnectivity)
{
    INetworkListManager *Manager = NULL;
    IEnumNetworkConnections *Connections = NULL;
    INetworkConnection *Connection;
    INetwork *Network;
    NLM_CONNECTIVITY Connectivity;
    ULONG Fetched, Count = 0;
    HRESULT hr;

    hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    ok(hr == S_OK, "CoInitializeEx returned 0x%lx\n", hr);

    hr = CoCreateInstance(&CLSID_NetworkListManager, NULL, CLSCTX_ALL, &IID_INetworkListManager, (void **)&Manager);
    ok(hr == S_OK, "CoCreateInstance returned 0x%lx\n", hr);
    if (FAILED(hr))
    {
        CoUninitialize();
        return;
    }

    hr = INetworkListManager_GetNetworkConnections(Manager, &Connections);
    ok(hr == S_OK, "GetNetworkConnections returned 0x%lx\n", hr);
    while (Connections && IEnumNetworkConnections_Next(Connections, 1, &Connection, &Fetched) == S_OK && Fetched)
    {
        Connectivity = NLM_CONNECTIVITY_DISCONNECTED;
        hr = INetworkConnection_GetConnectivity(Connection, &Connectivity);
        ok(hr == S_OK, "GetConnectivity returned 0x%lx\n", hr);
        CheckBag((IUnknown *)Connection, Connectivity, "connection");

        hr = INetworkConnection_GetNetwork(Connection, &Network);
        ok(hr == S_OK, "GetNetwork returned 0x%lx\n", hr);
        if (hr == S_OK)
        {
            hr = INetwork_GetConnectivity(Network, &Connectivity);
            ok(hr == S_OK, "INetwork_GetConnectivity returned 0x%lx\n", hr);
            CheckBag((IUnknown *)Network, Connectivity, "network");
            INetwork_Release(Network);
        }
        INetworkConnection_Release(Connection);
        Count++;
    }
    ok(Count != 0, "no network connection\n");

    if (Connections)
        IEnumNetworkConnections_Release(Connections);
    INetworkListManager_Release(Manager);
    CoUninitialize();
}
