/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests the AF_UNIX provider and the stream socket path used by the JDK
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include "ws2_32.h"
#include <afunix.h>

#ifndef IO_REPARSE_TAG_AF_UNIX
#define IO_REPARSE_TAG_AF_UNIX 0x80000023L
#endif

static const GUID AfUnixProviderId = {0xA00943D9, 0x9C2E, 0x4633, {0x9B, 0x59, 0x00, 0x57, 0xA3, 0x16, 0x09, 0x94}};

static BOOL
FindAfUnixProvider(
    _Out_ WSAPROTOCOL_INFOW *Provider)
{
    WSAPROTOCOL_INFOW *Info;
    DWORD Length = 0;
    int Count, Index, Found = 0;

    ok(WSAEnumProtocolsW(NULL, NULL, &Length) == SOCKET_ERROR, "WSAEnumProtocolsW with no buffer succeeded\n");
    ok(WSAGetLastError() == WSAENOBUFS, "WSAEnumProtocolsW error %d\n", WSAGetLastError());
    Info = HeapAlloc(GetProcessHeap(), 0, Length);
    if (!Info)
        return FALSE;

    Count = WSAEnumProtocolsW(NULL, Info, &Length);
    ok(Count > 0, "WSAEnumProtocolsW returned %d, error %d\n", Count, WSAGetLastError());
    for (Index = 0; Index < Count; Index++)
    {
        if (Info[Index].iAddressFamily != AF_UNIX)
            continue;

        trace("AF_UNIX entry %lu: %s %ls flags1 0x%lx provider flags 0x%lx type %d protocol %d addr %d..%d chain %d msg %lu\n",
              Info[Index].dwCatalogEntryId, wine_dbgstr_guid(&Info[Index].ProviderId), Info[Index].szProtocol,
              Info[Index].dwServiceFlags1, Info[Index].dwProviderFlags, Info[Index].iSocketType,
              Info[Index].iProtocol, Info[Index].iMinSockAddr, Info[Index].iMaxSockAddr,
              Info[Index].ProtocolChain.ChainLen, Info[Index].dwMessageSize);
        if (!Found++)
            *Provider = Info[Index];
    }
    HeapFree(GetProcessHeap(), 0, Info);

    ok(Found == 1, "%d AF_UNIX entries\n", Found);
    if (!Found)
        return FALSE;

    ok(IsEqualGUID(&Provider->ProviderId, &AfUnixProviderId), "AF_UNIX provider %s\n", wine_dbgstr_guid(&Provider->ProviderId));
    ok(Provider->iSocketType == SOCK_STREAM, "iSocketType %d\n", Provider->iSocketType);
    ok(Provider->iProtocol == 0, "iProtocol %d\n", Provider->iProtocol);
    ok(Provider->iMinSockAddr == 2, "iMinSockAddr %d\n", Provider->iMinSockAddr);
    ok(Provider->iMaxSockAddr == sizeof(SOCKADDR_UN), "iMaxSockAddr %d\n", Provider->iMaxSockAddr);
    ok(Provider->ProtocolChain.ChainLen == BASE_PROTOCOL, "ChainLen %d\n", Provider->ProtocolChain.ChainLen);
    ok(Provider->iVersion == 2, "iVersion %d\n", Provider->iVersion);
    ok(Provider->dwServiceFlags1 == 0x20026, "dwServiceFlags1 0x%lx\n", Provider->dwServiceFlags1);
    ok(Provider->dwProviderFlags == PFL_MATCHES_PROTOCOL_ZERO, "dwProviderFlags 0x%lx\n", Provider->dwProviderFlags);
    ok(!wcscmp(Provider->szProtocol, L"AF_UNIX"), "szProtocol %ls\n", Provider->szProtocol);
    return TRUE;
}

static int
MakeAddress(
    _Out_ SOCKADDR_UN *Address,
    _In_ PCSTR Path)
{
    ZeroMemory(Address, sizeof(*Address));
    Address->sun_family = AF_UNIX;
    strcpy(Address->sun_path, Path);
    return (int)(FIELD_OFFSET(SOCKADDR_UN, sun_path) + strlen(Path));
}

static void
CheckSocketFile(
    _In_ PCSTR Path)
{
    WIN32_FIND_DATAA Find;
    HANDLE Handle;

    Handle = FindFirstFileA(Path, &Find);
    ok(Handle != INVALID_HANDLE_VALUE, "%s not found, error %lu\n", Path, GetLastError());
    if (Handle == INVALID_HANDLE_VALUE)
        return;

    FindClose(Handle);
    ok(Find.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT, "attributes 0x%lx\n", Find.dwFileAttributes);
    ok(Find.dwReserved0 == IO_REPARSE_TAG_AF_UNIX, "reparse tag 0x%lx\n", Find.dwReserved0);
    ok(Find.nFileSizeLow == 0 && Find.nFileSizeHigh == 0, "size %lu:%lu\n", Find.nFileSizeHigh, Find.nFileSizeLow);
}

static void
TestStream(
    _In_ WSAPROTOCOL_INFOW *Provider)
{
    CHAR Path[MAX_PATH], Buffer[16];
    SOCKADDR_UN Address, Peer;
    SOCKET Listener, Client, Server, Other;
    int Length, PathLength, Result;

    Length = GetTempPathA(sizeof(Path), Path);
    ok(Length > 0, "GetTempPathA failed, error %lu\n", GetLastError());
    Result = _snprintf(Path + Length, sizeof(Path) - Length, "afunix_%lu.sock", GetCurrentProcessId());
    ok(Result > 0 && Length + Result < UNIX_PATH_MAX, "path too long: %d\n", Length + Result);
    DeleteFileA(Path);

    Listener = WSASocketW(PF_UNIX, SOCK_STREAM, 0, Provider, 0, WSA_FLAG_OVERLAPPED);
    ok(Listener != INVALID_SOCKET, "WSASocketW failed, error %d\n", WSAGetLastError());
    if (Listener == INVALID_SOCKET)
        return;

    PathLength = MakeAddress(&Address, Path);
    ok(bind(Listener, (SOCKADDR *)&Address, PathLength) == 0, "bind failed, error %d\n", WSAGetLastError());
    CheckSocketFile(Path);

    ZeroMemory(&Peer, sizeof(Peer));
    Length = sizeof(Peer);
    ok(getsockname(Listener, (SOCKADDR *)&Peer, &Length) == 0, "getsockname failed, error %d\n", WSAGetLastError());
    ok(Peer.sun_family == AF_UNIX, "getsockname family %u\n", Peer.sun_family);
    ok(!strcmp(Peer.sun_path, Path), "getsockname path %s\n", Peer.sun_path);
    ok(Length == PathLength + 1, "getsockname length %d for a %d byte address\n", Length, PathLength);

    Other = socket(AF_UNIX, SOCK_STREAM, 0);
    ok(Other != INVALID_SOCKET, "socket failed, error %d\n", WSAGetLastError());
    Result = bind(Other, (SOCKADDR *)&Address, PathLength);
    ok(Result == SOCKET_ERROR && WSAGetLastError() == WSAEADDRINUSE,
       "second bind returned %d, error %d\n", Result, WSAGetLastError());
    closesocket(Other);

    ok(listen(Listener, SOMAXCONN) == 0, "listen failed, error %d\n", WSAGetLastError());

    Client = WSASocketW(PF_UNIX, SOCK_STREAM, 0, Provider, 0, WSA_FLAG_OVERLAPPED);
    ok(Client != INVALID_SOCKET, "WSASocketW failed, error %d\n", WSAGetLastError());
    Length = FIELD_OFFSET(SOCKADDR_UN, sun_path);
    ZeroMemory(&Peer, sizeof(Peer));
    Peer.sun_family = AF_UNIX;
    Result = bind(Client, (SOCKADDR *)&Peer, Length);
    ok(Result == 0, "unnamed bind failed, error %d\n", WSAGetLastError());
    ok(connect(Client, (SOCKADDR *)&Address, PathLength) == 0, "connect failed, error %d\n", WSAGetLastError());

    ZeroMemory(&Peer, sizeof(Peer));
    Length = sizeof(Peer);
    Server = accept(Listener, (SOCKADDR *)&Peer, &Length);
    ok(Server != INVALID_SOCKET, "accept failed, error %d\n", WSAGetLastError());
    ok(Peer.sun_family == AF_UNIX, "accept family %u\n", Peer.sun_family);
    ok(Peer.sun_path[0] == 0, "accept path %s\n", Peer.sun_path);
    ok(Length == sizeof(SOCKADDR_UN), "accept length %d\n", Length);

    ZeroMemory(&Peer, sizeof(Peer));
    Length = sizeof(Peer);
    ok(getpeername(Client, (SOCKADDR *)&Peer, &Length) == 0, "getpeername failed, error %d\n", WSAGetLastError());
    ok(!strcmp(Peer.sun_path, Path), "getpeername path %s\n", Peer.sun_path);

    ok(send(Client, "ping", 4, 0) == 4, "send failed, error %d\n", WSAGetLastError());
    ZeroMemory(Buffer, sizeof(Buffer));
    ok(recv(Server, Buffer, sizeof(Buffer), 0) == 4, "recv failed, error %d\n", WSAGetLastError());
    ok(!memcmp(Buffer, "ping", 4), "received %s\n", Buffer);
    ok(send(Server, "pong", 4, 0) == 4, "send failed, error %d\n", WSAGetLastError());
    ZeroMemory(Buffer, sizeof(Buffer));
    ok(recv(Client, Buffer, sizeof(Buffer), 0) == 4, "recv failed, error %d\n", WSAGetLastError());
    ok(!memcmp(Buffer, "pong", 4), "received %s\n", Buffer);

    closesocket(Client);
    ok(recv(Server, Buffer, sizeof(Buffer), 0) == 0, "recv after close failed, error %d\n", WSAGetLastError());
    closesocket(Server);
    closesocket(Listener);

    CheckSocketFile(Path);
    Client = socket(AF_UNIX, SOCK_STREAM, 0);
    Result = connect(Client, (SOCKADDR *)&Address, PathLength);
    ok(Result == SOCKET_ERROR && WSAGetLastError() == WSAECONNREFUSED,
       "connect to a closed listener returned %d, error %d\n", Result, WSAGetLastError());
    closesocket(Client);

    ok(DeleteFileA(Path), "DeleteFileA failed, error %lu\n", GetLastError());
    Client = socket(AF_UNIX, SOCK_STREAM, 0);
    Result = connect(Client, (SOCKADDR *)&Address, PathLength);
    ok(Result == SOCKET_ERROR && WSAGetLastError() == WSAECONNREFUSED,
       "connect to a missing path returned %d, error %d\n", Result, WSAGetLastError());
    closesocket(Client);
}

START_TEST(afunix)
{
    WSAPROTOCOL_INFOW Provider;
    WSADATA Data;

    ok(WSAStartup(MAKEWORD(2, 2), &Data) == 0, "WSAStartup failed\n");
    if (FindAfUnixProvider(&Provider))
        TestStream(&Provider);
    WSACleanup();
}
