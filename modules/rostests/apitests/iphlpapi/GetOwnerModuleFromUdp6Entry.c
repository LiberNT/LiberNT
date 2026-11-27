/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Test for GetOwnerModuleFromUdp6Entry
 * COPYRIGHT:   Copyright 2026 LiberNT Contributors
 */

#include <apitest.h>

#define WIN32_NO_STATUS
#include <winsock2.h>
#include <ws2ipdef.h>
#include <iphlpapi.h>

static DWORD GetExtendedUdp6TableWithAlloc(PVOID *UdpTable, BOOL Order)
{
    DWORD ret;
    DWORD Size = 0;

    *UdpTable = NULL;

    ret = GetExtendedUdpTable(*UdpTable, &Size, Order, AF_INET6, UDP_TABLE_OWNER_MODULE, 0);
    if (ret == ERROR_INSUFFICIENT_BUFFER)
    {
        *UdpTable = HeapAlloc(GetProcessHeap(), 0, Size);
        if (*UdpTable == NULL)
        {
            return ERROR_OUTOFMEMORY;
        }

        ret = GetExtendedUdpTable(*UdpTable, &Size, Order, AF_INET6, UDP_TABLE_OWNER_MODULE, 0);
        if (ret != NO_ERROR)
        {
            HeapFree(GetProcessHeap(), 0, *UdpTable);
            *UdpTable = NULL;
        }
    }

    return ret;
}

START_TEST(GetOwnerModuleFromUdp6Entry)
{
    WSADATA wsaData;
    SOCKET sock;
    SOCKADDR_IN6 server;
    PMIB_UDP6TABLE_OWNER_MODULE Udp6TableOwnerMod;
    DWORD i;
    BOOLEAN Found;
    DWORD Pid = GetCurrentProcessId();
    DWORD Ret;

    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        skip("Failed to init WS2\n");
        return;
    }

    /* Invalid parameter must be rejected before anything else */
    Ret = GetOwnerModuleFromUdp6Entry(NULL, TCPIP_OWNER_MODULE_INFO_BASIC, NULL, NULL);
    ok(Ret == ERROR_INVALID_PARAMETER, "Got %lu for NULL entry\n", Ret);

    sock = socket(AF_INET6, SOCK_DGRAM, 0);
    if (sock == INVALID_SOCKET)
    {
        skip("Cannot create UDP6 socket\n");
        WSACleanup();
        return;
    }

    ZeroMemory(&server, sizeof(SOCKADDR_IN6));
    server.sin6_family = AF_INET6;
    server.sin6_port = htons(9877);

    if (bind(sock, (SOCKADDR*)&server, sizeof(SOCKADDR_IN6)) == SOCKET_ERROR)
    {
        skip("Cannot bind UDP6 socket\n");
        goto quit;
    }

    if (GetExtendedUdp6TableWithAlloc((PVOID *)&Udp6TableOwnerMod, TRUE) == ERROR_SUCCESS)
    {
        Found = FALSE;
        for (i = 0; i < Udp6TableOwnerMod->dwNumEntries; ++i)
        {
            if (Udp6TableOwnerMod->table[i].dwLocalPort == htons(9877) &&
                Udp6TableOwnerMod->table[i].dwOwningPid == Pid)
            {
                Found = TRUE;
                break;
            }
        }

        if (!Found)
        {
            skip("Our UDP6 socket wasn't found!\n");
        }
        else
        {
            DWORD Size = 0;
            PTCPIP_OWNER_MODULE_BASIC_INFO BasicInfo = NULL;

            if (GetOwnerModuleFromUdp6Entry(&Udp6TableOwnerMod->table[i], TCPIP_OWNER_MODULE_INFO_BASIC, NULL, &Size) == ERROR_INSUFFICIENT_BUFFER)
            {
                BasicInfo = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Size);
                ok(BasicInfo != NULL, "HeapAlloc failed\n");

                if (GetOwnerModuleFromUdp6Entry(&Udp6TableOwnerMod->table[i], TCPIP_OWNER_MODULE_INFO_BASIC, BasicInfo, &Size) == ERROR_SUCCESS)
                {
                    WCHAR CurrentModule[MAX_PATH];
                    PWSTR FileName;

                    if (GetModuleFileNameW(NULL, CurrentModule, MAX_PATH) != 0)
                    {
                        FileName = wcsrchr(CurrentModule, L'\\');
                        ++FileName;

                        ok(_wcsicmp(CurrentModule, BasicInfo->pModulePath) == 0, "Mismatching names (%S, %S)\n", CurrentModule, BasicInfo->pModulePath);
                        ok(_wcsicmp(FileName, BasicInfo->pModuleName) == 0, "Mismatching names (%S, %S)\n", FileName, BasicInfo->pModuleName);
                    }
                    else
                    {
                        skip("GetModuleFileNameW failed\n");
                    }
                }
                else
                {
                    ok(0, "GetOwnerModuleFromUdp6Entry failed\n");
                }

                if (BasicInfo != NULL)
                    HeapFree(GetProcessHeap(), 0, BasicInfo);
            }
            else
            {
                ok(0, "GetOwnerModuleFromUdp6Entry did not report required size\n");
            }
        }

        HeapFree(GetProcessHeap(), 0, Udp6TableOwnerMod);
    }
    else
    {
        skip("GetExtendedUdp6TableWithAlloc failure\n");
    }

quit:
    closesocket(sock);
    WSACleanup();
}
