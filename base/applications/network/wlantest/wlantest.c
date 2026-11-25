/*
 * PROJECT:     LiberNT wlantest utility
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Drive wlanapi from the command line: scan, connect with an
 *              open-auth profile, disconnect, query state, TX flood.
 */

#include <windows.h>
#include <stdio.h>
#include <wlanapi.h>
#include <winsock2.h>

static PWLAN_INTERFACE_INFO
first_iface(HANDLE h, PWLAN_INTERFACE_INFO_LIST *l)
{
    if (WlanEnumInterfaces(h, NULL, l) != ERROR_SUCCESS || *l == NULL ||
        (*l)->dwNumberOfItems == 0)
        return NULL;
    return &(*l)->InterfaceInfo[0];
}

static void
show_state(HANDLE h, const GUID *g)
{
    WLAN_INTERFACE_STATE *pst = NULL;
    PWLAN_CONNECTION_ATTRIBUTES pa = NULL;
    DWORD sz;
    WLAN_OPCODE_VALUE_TYPE vt;

    sz = sizeof(*pst);
    if (WlanQueryInterface(h, g, wlan_intf_opcode_interface_state,
                           NULL, &sz, (PVOID *)&pst, &vt) == ERROR_SUCCESS && pst)
    {
        printf("interface_state=%d\n", *pst);
        WlanFreeMemory(pst);
    }

    sz = sizeof(*pa);
    if (WlanQueryInterface(h, g, wlan_intf_opcode_current_connection,
                           NULL, &sz, (PVOID *)&pa, &vt) == ERROR_SUCCESS && pa)
    {
        printf("ssid=%.32s bssid=%02x:%02x:%02x:%02x:%02x:%02x\n",
               (char *)pa->wlanAssociationAttributes.dot11Ssid.ucSSID,
               pa->wlanAssociationAttributes.dot11Bssid[0],
               pa->wlanAssociationAttributes.dot11Bssid[1],
               pa->wlanAssociationAttributes.dot11Bssid[2],
               pa->wlanAssociationAttributes.dot11Bssid[3],
               pa->wlanAssociationAttributes.dot11Bssid[4],
               pa->wlanAssociationAttributes.dot11Bssid[5]);
        WlanFreeMemory(pa);
    }
}

static int
do_scan(HANDLE h, const GUID *g)
{
    PWLAN_BSS_LIST bss = NULL;
    DWORD i, r;

    r = WlanScan(h, g, NULL, NULL, NULL);
    printf("WlanScan %lu\n", r);
    Sleep(2000);

    r = WlanGetNetworkBssList(h, g, NULL, dot11_BSS_type_any, FALSE, NULL, &bss);
    if (r != ERROR_SUCCESS || !bss)
    {
        printf("WlanGetNetworkBssList %lu\n", r);
        return 1;
    }
    for (i = 0; i < bss->dwNumberOfItems; i++)
    {
        PWLAN_BSS_ENTRY e = &bss->wlanBssEntries[i];
        printf("%.32s bssid=%02x:%02x:%02x:%02x:%02x:%02x ch=%lu rssi=%ld\n",
               (char *)e->dot11Ssid.ucSSID,
               e->dot11Bssid[0], e->dot11Bssid[1], e->dot11Bssid[2],
               e->dot11Bssid[3], e->dot11Bssid[4], e->dot11Bssid[5],
               e->ulChCenterFrequency, e->lRssi);
    }
    WlanFreeMemory(bss);
    return 0;
}

/* TX flood over the association: UDP to the APIPA subnet broadcast, which
 * needs no ARP resolution and reaches the miniport send path directly. */
static int
do_flood(int mb)
{
    WSADATA wd;
    SOCKET s;
    SOCKADDR_IN to;
    UCHAR pkt[1400];
    ULONGLONG bytes = (ULONGLONG)mb * 1024 * 1024, sent = 0, t0, dt;
    int r, one = 1, bufsz = 16 * 1024 * 1024;

    if (WSAStartup(MAKEWORD(2, 2), &wd))
    {
        printf("WSAStartup fail\n");
        return 1;
    }
    s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET)
    {
        printf("socket fail\n");
        return 1;
    }
    memset(&to, 0, sizeof(to));
    to.sin_family = AF_INET;
    to.sin_port = htons(9);
    to.sin_addr.s_addr = inet_addr("169.254.255.255");
    setsockopt(s, SOL_SOCKET, SO_BROADCAST, (const char *)&one, sizeof(one));
    setsockopt(s, SOL_SOCKET, SO_SNDBUF, (const char *)&bufsz, sizeof(bufsz));
    memset(pkt, 0xAB, sizeof(pkt));
    t0 = GetTickCount64();
    while (sent < bytes)
    {
        r = sendto(s, (const char *)pkt, sizeof(pkt), 0, (SOCKADDR *)&to, sizeof(to));
        if (r <= 0)
        {
            int e = WSAGetLastError();
            if (e == WSAENOBUFS || e == WSAEWOULDBLOCK)
            {
                Sleep(0);
                continue;
            }
            printf("sendto failed gle=%d at %llu\n", e, sent);
            break;
        }
        sent += r;
    }
    dt = GetTickCount64() - t0;
    printf("flooded %llu bytes in %llu ms -> %.2f MB/s\n", sent, dt,
           dt ? (double)sent / (double)dt / 1024.0 * 1000.0 / 1024.0 : 0.0);
    closesocket(s);
    WSACleanup();
    return 0;
}

int
wmain(int argc, wchar_t **argv)
{
    HANDLE h;
    DWORD ver = 0, r;
    PWLAN_INTERFACE_INFO_LIST list = NULL;
    PWLAN_INTERFACE_INFO iface;
    WCHAR prof[1024];
    CHAR ssida[DOT11_SSID_MAX_LENGTH + 1];
    int slen;
    DWORD i;
    int ret = 0;

    if (argc < 2)
    {
        printf("usage: wlantest <ssid | disconnect | state | scan | flood [mb]>\n");
        return 2;
    }

    r = WlanOpenHandle(2, NULL, &ver, &h);
    if (r != ERROR_SUCCESS)
    {
        printf("WlanOpenHandle %lu\n", r);
        return 1;
    }

    iface = first_iface(h, &list);
    if (!iface)
    {
        printf("no wlan interface\n");
        return 1;
    }
    printf("iface=%.*ls\n", 40, iface->strInterfaceDescription);

    if (!wcscmp(argv[1], L"disconnect"))
    {
        r = WlanDisconnect(h, &iface->InterfaceGuid, NULL);
        printf("WlanDisconnect %lu\n", r);
        goto out;
    }

    if (!wcscmp(argv[1], L"state"))
    {
        show_state(h, &iface->InterfaceGuid);
        goto out;
    }

    if (!wcscmp(argv[1], L"scan"))
    {
        ret = do_scan(h, &iface->InterfaceGuid);
        goto out;
    }

    if (!wcscmp(argv[1], L"flood"))
    {
        ret = do_flood(argc > 2 ? _wtoi(argv[2]) : 32);
        goto out;
    }

    slen = WideCharToMultiByte(CP_UTF8, 0, argv[1], -1, ssida, sizeof(ssida), NULL, NULL);
    if (slen <= 1)
    {
        printf("bad ssid\n");
        return 2;
    }
    slen--;

    _snwprintf(prof, 1024,
        L"<?xml version=\"1.0\"?>\r\n"
        L"<WLANProfile xmlns=\"http://www.microsoft.com/networking/WLAN/profile/v1\">\r\n"
        L"<name>%hs</name>\r\n"
        L"<SSIDConfig><SSID><name>%hs</name></SSID></SSIDConfig>\r\n"
        L"<connectionType>ESS</connectionType>\r\n"
        L"<connectionMode>manual</connectionMode>\r\n"
        L"<MSM><security>\r\n"
        L"<authEncryption><authentication>open</authentication>"
        L"<encryption>none</encryption><useOneX>false</useOneX></authEncryption>\r\n"
        L"</security></MSM>\r\n"
        L"</WLANProfile>\r\n", ssida, ssida);

    {
        DWORD reason = 0;
        r = WlanSetProfile(h, &iface->InterfaceGuid, 0, prof, NULL, TRUE, NULL, &reason);
        printf("WlanSetProfile %lu (reason %lu)\n", r, reason);
    }
    if (r != ERROR_SUCCESS)
    {
        ret = 1;
        goto out;
    }

    {
        WLAN_CONNECTION_PARAMETERS cp;
        ZeroMemory(&cp, sizeof(cp));
        cp.wlanConnectionMode = wlan_connection_mode_profile;
        cp.strProfile = argv[1];
        r = WlanConnect(h, &iface->InterfaceGuid, &cp, NULL);
        printf("WlanConnect %lu\n", r);
        if (r != ERROR_SUCCESS)
        {
            ret = 1;
            goto out;
        }
    }

    for (i = 0; i < 40; i++)
    {
        WLAN_INTERFACE_STATE *pst = NULL;
        DWORD sz = sizeof(*pst);
        WLAN_OPCODE_VALUE_TYPE vt;
        Sleep(250);
        if (WlanQueryInterface(h, &iface->InterfaceGuid,
                               wlan_intf_opcode_interface_state,
                               NULL, &sz, (PVOID *)&pst, &vt) != ERROR_SUCCESS || !pst)
            continue;
        printf("[%lu] state=%d\n", i, *pst);
        if (*pst == wlan_interface_state_connected ||
            *pst == wlan_interface_state_disconnected)
        {
            WlanFreeMemory(pst);
            if (*pst == wlan_interface_state_disconnected)
                ret = 1;
            break;
        }
        WlanFreeMemory(pst);
    }
    show_state(h, &iface->InterfaceGuid);

out:
    if (list)
        WlanFreeMemory(list);
    WlanCloseHandle(h, NULL);
    return ret;
}
