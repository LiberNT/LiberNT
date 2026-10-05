/*
 * PROJECT:     ReactOS Timedate Control Panel
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Queries the NTP server
 * COPYRIGHT:   Copyright 2006 Ged Murphy <gedmurphy@gmail.com>
 */

#include "w32time.h"

#include <winsock2.h>

#define TIMEOUT 4000 /* 4 second timeout */

typedef struct _INFO
{
    SOCKET Sock;
    SOCKADDR_IN myAddr;
    SOCKADDR_IN ntpAddr;
    NTPPACKET SendPacket;
    NTPPACKET RecvPacket;
} INFO, *PINFO;


static DWORD
InitConnection(PINFO pInfo,
               LPSTR lpAddress)
{
    WSADATA wsaData;
    HOSTENT *he;
    INT Ret;

    Ret = WSAStartup(MAKEWORD(2, 2),
                     &wsaData);
    if (Ret != 0)
        return Ret;

    pInfo->Sock = socket(AF_INET,
                         SOCK_DGRAM,
                         0);
    if (pInfo->Sock == INVALID_SOCKET)
        return WSAGetLastError();

    /* Setup server info */
    he = gethostbyname(lpAddress);
    if (he != NULL)
    {
        /* Setup server socket info */
        ZeroMemory(&pInfo->ntpAddr, sizeof(SOCKADDR_IN));
        pInfo->ntpAddr.sin_family = AF_INET; // he->h_addrtype;
        pInfo->ntpAddr.sin_port = htons(NTPPORT);
        pInfo->ntpAddr.sin_addr = *((struct in_addr *)he->h_addr);
    }
    else
        return WSAGetLastError();

    return ERROR_SUCCESS;
}


static VOID
DestroyConnection(PINFO pInfo)
{
    if (pInfo->Sock != INVALID_SOCKET)
        closesocket(pInfo->Sock);

    WSACleanup();
}


static BOOL
GetTransmitTime(PTIMEPACKET ptp)
{
    return TRUE;
}


/* Send some data to wake the server up */
static DWORD
SendData(PINFO pInfo)
{
    TIMEPACKET tp = { 0, 0 };
    INT Ret;

    ZeroMemory(&pInfo->SendPacket, sizeof(pInfo->SendPacket));
    pInfo->SendPacket.LiVnMode = 0x1b;        /* 0x1b = 011 011 - version 3 , mode 3 (client) */
    if (!GetTransmitTime(&tp))
        return ERROR_GEN_FAILURE;
    pInfo->SendPacket.TransmitTimestamp = tp;

    Ret = sendto(pInfo->Sock,
                 (char *)&pInfo->SendPacket,
                 sizeof(pInfo->SendPacket),
                 0,
                 (SOCKADDR *)&pInfo->ntpAddr,
                 sizeof(SOCKADDR_IN));

    if (Ret == SOCKET_ERROR)
        return WSAGetLastError();

    return ERROR_SUCCESS;
}


static DWORD
ReceiveData(PINFO pInfo, PULONGLONG pullTime)
{
    TIMEVAL timeVal;
    FD_SET readFDS;
    INT Ret;

    /* Monitor socket for incoming connections */
    FD_ZERO(&readFDS);
    FD_SET(pInfo->Sock, &readFDS);

    /* Set timeout values */
    timeVal.tv_sec  = TIMEOUT / 1000;
    timeVal.tv_usec = TIMEOUT % 1000;

    /* Check for data on the socket for TIMEOUT millisecs */
    Ret = select(0, &readFDS, NULL, NULL, &timeVal);
    if (Ret == SOCKET_ERROR)
        return WSAGetLastError();
    if (Ret == 0)
        return ERROR_TIMEOUT;

    Ret = recvfrom(pInfo->Sock,
                   (char *)&pInfo->RecvPacket,
                   sizeof(pInfo->RecvPacket),
                   0,
                   NULL,
                   NULL);
    if (Ret == SOCKET_ERROR)
        return WSAGetLastError();

    *pullTime = (ULONGLONG)ntohl(pInfo->RecvPacket.TransmitTimestamp.dwInteger) * 10000000;
    *pullTime += ((ULONGLONG)ntohl(pInfo->RecvPacket.TransmitTimestamp.dwFractional) * 10000000) >> 32;

    return ERROR_SUCCESS;
}


DWORD
GetServerTime(LPWSTR lpAddress, PULONGLONG pullTime)
{
    PINFO pInfo;
    LPSTR lpAddr;
    DWORD dwSize = wcslen(lpAddress) + 1;
    DWORD dwError = ERROR_NOT_ENOUGH_MEMORY;

    *pullTime = 0;

    pInfo = (PINFO)HeapAlloc(GetProcessHeap(),
                             HEAP_ZERO_MEMORY,
                             sizeof(INFO));
    lpAddr = (LPSTR)HeapAlloc(GetProcessHeap(),
                              0,
                              dwSize);

    if (pInfo && lpAddr)
    {
        pInfo->Sock = INVALID_SOCKET;

        if (WideCharToMultiByte(CP_ACP,
                                0,
                                lpAddress,
                                -1,
                                lpAddr,
                                dwSize,
                                NULL,
                                NULL))
        {
            dwError = InitConnection(pInfo, lpAddr);
            if (dwError == ERROR_SUCCESS)
            {
                dwError = SendData(pInfo);
                if (dwError == ERROR_SUCCESS)
                {
                    dwError = ReceiveData(pInfo, pullTime);
                }
            }

            DestroyConnection(pInfo);
        }
        else
        {
            dwError = GetLastError();
        }
    }

    if (pInfo)
        HeapFree(GetProcessHeap(), 0, pInfo);
    if (lpAddr)
        HeapFree(GetProcessHeap(), 0, lpAddr);

    return dwError;
}
