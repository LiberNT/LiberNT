/*
 * PROJECT:     LiberNT RDP console server
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Console desktop RDP server for an explicitly configured test LAN
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

/*
 * FreeRDP implements the wire protocol and TLS. This backend captures the
 * existing interactive desktop with GDI and injects authenticated input.
 * It does not create a terminal session or replace the display driver.
 */
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <iphlpapi.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <freerdp/freerdp.h>
#include <freerdp/peer.h>
#include <freerdp/listener.h>
#include <freerdp/settings.h>
#include <freerdp/constants.h>
#include <freerdp/crypto/certificate.h>
#include <freerdp/crypto/privatekey.h>
#include <winpr/ssl.h>
#include <freerdp/channels/wtsvc.h>
#include <freerdp/channels/channels.h>
#include "transfer.h"

typedef ULONG (__cdecl *DEBUG_PRINT)(const char *, ...);
static DEBUG_PRINT DebugPrint;
static HANDLE StopEvent;
static volatile LONG ClientConnected;
static char BindAddress[64], AllowedPeer[64], Username[128], Password[256];
static char Certificate[MAX_PATH], PrivateKey[MAX_PATH];
#define MAX_INTERFACE_MACS 8
static BYTE InterfaceMac[MAX_INTERFACE_MACS][6];
static unsigned InterfaceMacCount;
static BOOL ConfigureAddress;
static IPMask InterfaceMask;
static ULONG AddressContext;
static BOOL AddressAdded;
static WCHAR TransferRoot[MAX_PATH];

typedef struct DESKTOP_CONTEXT
{
    rdpContext rdp;
    BOOL Authorized, Active, Suppressed, Refresh;
    int Width, Height;
    HDC ScreenDC, MemoryDC;
    HBITMAP Bitmap;
    HGDIOBJ OldBitmap;
    BYTE *Pixels, *Previous;
    BYTE Tile[64 * 32 * 4];
    BOOL Keys[512];
    DWORD MouseButtons;
    ULONGLONG Frames, Updates, Inputs;
    HANDLE Vcm;
    RDP_TRANSFER *Transfer;
} DESKTOP_CONTEXT;

static void Log(const char *fmt, ...)
{
    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    buffer[sizeof(buffer) - 1] = 0;
    fprintf(stdout, "RDPTEST: %s\n", buffer);
    fflush(stdout);
    if (DebugPrint) DebugPrint("RDPTEST: %s\n", buffer);
}

static BOOL InputReady(rdpInput *input)
{
    DESKTOP_CONTEXT *ctx = (DESKTOP_CONTEXT *)input->context;
    return ctx->Authorized && ctx->Active;
}

static BOOL ConfiguredInterface(const IP_ADAPTER_INFO *adapter)
{
    unsigned i;
    if (adapter->AddressLength != sizeof(InterfaceMac[0])) return FALSE;
    for (i = 0; i < InterfaceMacCount; ++i)
        if (!memcmp(adapter->Address, InterfaceMac[i], sizeof(InterfaceMac[i]))) return TRUE;
    return FALSE;
}

/* Optional, explicit test-LAN setup. Select by MAC, never by adapter order,
 * and retain existing addresses. The temporary alias is removed on exit. */
static BOOL EnsureAddress(void)
{
    IP_ADAPTER_INFO *adapters, *adapter;
    IP_ADDR_STRING *ip;
    ULONG size = 0, instance;
    DWORD status;
    if (!ConfigureAddress || AddressAdded) return TRUE;
    if (GetAdaptersInfo(NULL, &size) != ERROR_BUFFER_OVERFLOW) return FALSE;
    adapters = malloc(size);
    if (!adapters) return FALSE;
    status = GetAdaptersInfo(adapters, &size);
    if (status != NO_ERROR) { free(adapters); return FALSE; }
    for (adapter = adapters; adapter; adapter = adapter->Next)
    {
        if (!ConfiguredInterface(adapter)) continue;
        for (ip = &adapter->IpAddressList; ip; ip = ip->Next)
            if (!strcmp(ip->IpAddress.String, BindAddress))
            { free(adapters); return TRUE; }
        status = AddIPAddress(inet_addr(BindAddress), InterfaceMask,
                              adapter->Index, &AddressContext, &instance);
        Log("Test Ethernet interface %lu: address %s, AddIPAddress=%lu",
            adapter->Index, BindAddress, status);
        AddressAdded = status == NO_ERROR;
        free(adapters);
        return AddressAdded;
    }
    free(adapters);
    return FALSE;
}

/* A relative certificate or key path names a file next to the configuration,
 * so one configuration works from the live image and from an installed disk. */
static BOOL ConfigRelativePath(const char *config, char *path, size_t size)
{
    const char *separator = strrchr(config, '\\');
    size_t directory, length = strlen(path);
    if (!path[0] || path[0] == '\\' || path[0] == '/' || path[1] == ':') return TRUE;
    if (!separator) return FALSE;
    directory = (size_t)(separator - config) + 1;
    if (directory + length >= size) return FALSE;
    memmove(path + directory, path, length + 1);
    memcpy(path, config, directory);
    return TRUE;
}

static BOOL Keyboard(rdpInput *input, UINT16 flags, UINT8 code)
{
    DESKTOP_CONTEXT *ctx = (DESKTOP_CONTEXT *)input->context;
    INPUT event = {0};
    unsigned index = code | ((flags & KBD_FLAGS_EXTENDED) ? 256 : 0);
    if (!InputReady(input)) return FALSE;
    event.type = INPUT_KEYBOARD;
    event.ki.wScan = code;
    event.ki.dwFlags = KEYEVENTF_SCANCODE;
    if (flags & KBD_FLAGS_EXTENDED) event.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
    if (flags & KBD_FLAGS_RELEASE) event.ki.dwFlags |= KEYEVENTF_KEYUP;
    if (SendInput(1, &event, sizeof(event)) != 1) return FALSE;
    ctx->Keys[index] = !(flags & KBD_FLAGS_RELEASE);
    ++ctx->Inputs;
    return TRUE;
}

static BOOL UnicodeKeyboard(rdpInput *input, UINT16 flags, UINT16 code)
{
    INPUT event = {0};
    if (!InputReady(input)) return FALSE;
    event.type = INPUT_KEYBOARD;
    event.ki.wScan = code;
    event.ki.dwFlags = KEYEVENTF_UNICODE;
    if (flags & KBD_FLAGS_RELEASE) event.ki.dwFlags |= KEYEVENTF_KEYUP;
    return SendInput(1, &event, sizeof(event)) == 1;
}

static BOOL Mouse(rdpInput *input, UINT16 flags, UINT16 x, UINT16 y)
{
    DESKTOP_CONTEXT *ctx = (DESKTOP_CONTEXT *)input->context;
    INPUT event = {0};
    BOOL down = (flags & PTR_FLAGS_DOWN) != 0;
    if (!InputReady(input)) return FALSE;
    event.type = INPUT_MOUSE;
    if (x >= ctx->Width) x = ctx->Width - 1;
    if (y >= ctx->Height) y = ctx->Height - 1;
    event.mi.dx = MulDiv(x, 65535, ctx->Width - 1);
    event.mi.dy = MulDiv(y, 65535, ctx->Height - 1);
    event.mi.dwFlags = MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_MOVE;
    if (flags & (PTR_FLAGS_WHEEL | PTR_FLAGS_HWHEEL))
    {
        LONG delta = flags & 0x1ff;
        if (flags & PTR_FLAGS_WHEEL_NEGATIVE) delta -= 0x200;
        event.mi.mouseData = (DWORD)delta;
        event.mi.dwFlags |= (flags & PTR_FLAGS_HWHEEL) ? MOUSEEVENTF_HWHEEL : MOUSEEVENTF_WHEEL;
    }
    else
    {
        if (flags & PTR_FLAGS_BUTTON1) event.mi.dwFlags |= down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
        if (flags & PTR_FLAGS_BUTTON2) event.mi.dwFlags |= down ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP;
        if (flags & PTR_FLAGS_BUTTON3) event.mi.dwFlags |= down ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP;
        if (down) ctx->MouseButtons |= flags & (PTR_FLAGS_BUTTON1 | PTR_FLAGS_BUTTON2 | PTR_FLAGS_BUTTON3);
        else ctx->MouseButtons &= ~(flags & (PTR_FLAGS_BUTTON1 | PTR_FLAGS_BUTTON2 | PTR_FLAGS_BUTTON3));
    }
    if (SendInput(1, &event, sizeof(event)) != 1) return FALSE;
    ++ctx->Inputs;
    return TRUE;
}

static BOOL Synchronize(rdpInput *input, UINT32 flags)
{
    (void)flags;
    return InputReady(input);
}

static void FreeDesktopCapture(DESKTOP_CONTEXT *ctx)
{
    if (ctx->OldBitmap) SelectObject(ctx->MemoryDC, ctx->OldBitmap);
    if (ctx->Bitmap) DeleteObject(ctx->Bitmap);
    if (ctx->MemoryDC) DeleteDC(ctx->MemoryDC);
    if (ctx->ScreenDC) ReleaseDC(NULL, ctx->ScreenDC);
    free(ctx->Previous);
    ctx->OldBitmap = NULL; ctx->Bitmap = NULL;
    ctx->MemoryDC = ctx->ScreenDC = NULL;
    ctx->Previous = ctx->Pixels = NULL;
}

static BOOL AllocateCapture(DESKTOP_CONTEXT *ctx)
{
    BITMAPINFO info = {0};
    FreeDesktopCapture(ctx);
    ctx->Width = GetSystemMetrics(SM_CXSCREEN);
    ctx->Height = GetSystemMetrics(SM_CYSCREEN);
    if (ctx->Width < 2 || ctx->Height < 2 || ctx->Width > 8192 || ctx->Height > 8192) return FALSE;
    ctx->ScreenDC = GetDC(NULL);
    if (!ctx->ScreenDC) return FALSE;
    ctx->MemoryDC = CreateCompatibleDC(ctx->ScreenDC);
    if (!ctx->MemoryDC) return FALSE;
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = ctx->Width;
    info.bmiHeader.biHeight = -ctx->Height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    ctx->Bitmap = CreateDIBSection(ctx->ScreenDC, &info, DIB_RGB_COLORS,
                                   (void **)&ctx->Pixels, NULL, 0);
    if (!ctx->Bitmap) return FALSE;
    ctx->OldBitmap = SelectObject(ctx->MemoryDC, ctx->Bitmap);
    if (!ctx->OldBitmap || ctx->OldBitmap == HGDI_ERROR) { ctx->OldBitmap = NULL; return FALSE; }
    ctx->Previous = calloc((size_t)ctx->Width * ctx->Height, 4);
    ctx->Refresh = TRUE;
    return ctx->Previous != NULL;
}

static BOOL Capabilities(freerdp_peer *peer)
{
    return freerdp_settings_set_uint32(peer->context->settings, FreeRDP_DesktopWidth,
                                       GetSystemMetrics(SM_CXSCREEN)) &&
           freerdp_settings_set_uint32(peer->context->settings, FreeRDP_DesktopHeight,
                                       GetSystemMetrics(SM_CYSCREEN));
}

static BOOL PostConnect(freerdp_peer *peer)
{
    DESKTOP_CONTEXT *ctx = (DESKTOP_CONTEXT *)peer->context;
    rdpSettings *settings = ctx->rdp.settings;
    const char *user = freerdp_settings_get_string(settings, FreeRDP_Username);
    const char *password = freerdp_settings_get_string(settings, FreeRDP_Password);
    if (!user || !password || strcmp(user, Username) || strcmp(password, Password))
    {
        Log("Authentication rejected for peer %s", peer->hostname);
        return FALSE;
    }
    ctx->Authorized = TRUE;
    if (!AllocateCapture(ctx)) return FALSE;
    if (*TransferRoot && WTSVirtualChannelManagerIsChannelJoined(ctx->Vcm, "rdpdr"))
    {
        ctx->Transfer = TransferStart(ctx->Vcm, &ctx->rdp, TransferRoot, Log);
        if (!ctx->Transfer) { Log("File transfer channel failed to start"); return FALSE; }
    }
    Log("Authenticated peer %s; console desktop %dx%d", peer->hostname, ctx->Width, ctx->Height);
    return TRUE;
}

static BOOL Activate(freerdp_peer *peer)
{
    DESKTOP_CONTEXT *ctx = (DESKTOP_CONTEXT *)peer->context;
    POINTER_SYSTEM_UPDATE pointer = {0};
    if (!ctx->Authorized) return FALSE;
    ctx->Active = TRUE;
    ctx->Refresh = TRUE;
    pointer.type = SYSPTR_NULL;
    return ctx->rdp.update->pointer->PointerSystem(&ctx->rdp, &pointer);
}

static BOOL Refresh(rdpContext *rdp, BYTE count, const RECTANGLE_16 *rects)
{
    (void)count; (void)rects;
    ((DESKTOP_CONTEXT *)rdp)->Refresh = TRUE;
    return TRUE;
}

static BOOL Suppress(rdpContext *rdp, BYTE allow, const RECTANGLE_16 *area)
{
    (void)area;
    ((DESKTOP_CONTEXT *)rdp)->Suppressed = !allow;
    ((DESKTOP_CONTEXT *)rdp)->Refresh = TRUE;
    return TRUE;
}

static BOOL Capture(DESKTOP_CONTEXT *ctx)
{
    int x, y, row;
    CURSORINFO cursor = {0};
    BITMAP_DATA rectangle = {0};
    BITMAP_UPDATE update = {0};
    if (!ctx->Authorized || !ctx->Active || ctx->Suppressed) return TRUE;
    if (ctx->Width != GetSystemMetrics(SM_CXSCREEN) || ctx->Height != GetSystemMetrics(SM_CYSCREEN))
    {
        if (!AllocateCapture(ctx) ||
            !freerdp_settings_set_uint32(ctx->rdp.settings, FreeRDP_DesktopWidth, ctx->Width) ||
            !freerdp_settings_set_uint32(ctx->rdp.settings, FreeRDP_DesktopHeight, ctx->Height)) return FALSE;
        ctx->Active = FALSE;
        return ctx->rdp.update->DesktopResize(&ctx->rdp);
    }
    if (!BitBlt(ctx->MemoryDC, 0, 0, ctx->Width, ctx->Height, ctx->ScreenDC, 0, 0, SRCCOPY | CAPTUREBLT))
    {
        Log("Desktop capture failed error=%lu", GetLastError());
        return FALSE;
    }
    cursor.cbSize = sizeof(cursor);
    if (GetCursorInfo(&cursor) && (cursor.flags & CURSOR_SHOWING))
    {
        ICONINFO icon = {0};
        if (GetIconInfo(cursor.hCursor, &icon))
        {
            DrawIconEx(ctx->MemoryDC, cursor.ptScreenPos.x - icon.xHotspot,
                       cursor.ptScreenPos.y - icon.yHotspot, cursor.hCursor, 0, 0, 0, NULL, DI_NORMAL);
            if (icon.hbmColor) DeleteObject(icon.hbmColor);
            if (icon.hbmMask) DeleteObject(icon.hbmMask);
        }
    }
    GdiFlush();
    update.number = 1; update.rectangles = &rectangle; update.skipCompression = TRUE;
    rectangle.bitsPerPixel = 32; rectangle.bitmapDataStream = ctx->Tile;
    for (y = 0; y < ctx->Height; y += 32)
        for (x = 0; x < ctx->Width; x += 64)
        {
            int width = min(64, ctx->Width - x), height = min(32, ctx->Height - y);
            BOOL changed = ctx->Refresh;
            for (row = 0; !changed && row < height; ++row)
            {
                size_t offset = ((size_t)(y + row) * ctx->Width + x) * 4;
                changed = memcmp(ctx->Pixels + offset, ctx->Previous + offset, width * 4) != 0;
            }
            if (!changed) continue;
            for (row = 0; row < height; ++row)
                memcpy(ctx->Tile + (size_t)row * width * 4,
                       ctx->Pixels + ((size_t)(y + height - 1 - row) * ctx->Width + x) * 4, width * 4);
            rectangle.destLeft = x; rectangle.destTop = y;
            rectangle.destRight = x + width - 1; rectangle.destBottom = y + height - 1;
            rectangle.width = width; rectangle.height = height;
            rectangle.bitmapLength = width * height * 4;
            if (!ctx->rdp.update->BitmapUpdate(&ctx->rdp, &update)) return FALSE;
            ++ctx->Updates;
        }
    memcpy(ctx->Previous, ctx->Pixels, (size_t)ctx->Width * ctx->Height * 4);
    ctx->Refresh = FALSE;
    if (++ctx->Frames == 1) Log("First desktop frame sent, bitmap updates=%llu", ctx->Updates);
    return TRUE;
}

static void ContextFree(freerdp_peer *peer, rdpContext *rdp)
{
    DESKTOP_CONTEXT *ctx = (DESKTOP_CONTEXT *)rdp;
    unsigned i;
    INPUT event = {0};
    (void)peer;
    TransferStop(ctx->Transfer);
    if (ctx->Vcm) WTSCloseServer(ctx->Vcm);
    /* A disconnected controller must not leave Ctrl/Alt or mouse buttons held. */
    for (i = 0; i < 512; ++i)
        if (ctx->Keys[i])
        {
            event.type = INPUT_KEYBOARD;
            event.ki.wScan = i & 255;
            event.ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;
            if (i & 256) event.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
            SendInput(1, &event, sizeof(event));
        }
    memset(&event, 0, sizeof(event));
    event.type = INPUT_MOUSE;
    if (ctx->MouseButtons & PTR_FLAGS_BUTTON1) event.mi.dwFlags |= MOUSEEVENTF_LEFTUP;
    if (ctx->MouseButtons & PTR_FLAGS_BUTTON2) event.mi.dwFlags |= MOUSEEVENTF_RIGHTUP;
    if (ctx->MouseButtons & PTR_FLAGS_BUTTON3) event.mi.dwFlags |= MOUSEEVENTF_MIDDLEUP;
    if (event.mi.dwFlags) SendInput(1, &event, sizeof(event));
    Log("Session closed: frames=%llu updates=%llu input events=%llu", ctx->Frames, ctx->Updates, ctx->Inputs);
    FreeDesktopCapture(ctx);
}

static DWORD WINAPI ClientThread(void *arg)
{
    freerdp_peer *peer = arg;
    rdpSettings *settings;
    rdpPrivateKey *key;
    rdpCertificate *cert;
    DWORD count, status, started = GetTickCount(), lastFrame = 0;
    HANDLE events[32];
    DESKTOP_CONTEXT *ctx;
    peer->ContextSize = sizeof(DESKTOP_CONTEXT);
    peer->ContextFree = ContextFree;
    if (!freerdp_peer_context_new(peer)) goto done;
    ctx = (DESKTOP_CONTEXT *)peer->context;
    ctx->Vcm = WTSOpenServerA((LPSTR)peer->context);
    if (!ctx->Vcm) goto cleanup;
    settings = ctx->rdp.settings;
    key = freerdp_key_new_from_file_enc(PrivateKey, NULL);
    cert = freerdp_certificate_new_from_file(Certificate);
    if (!key || !cert) { freerdp_key_free(key); freerdp_certificate_free(cert); goto cleanup; }
    if (!freerdp_settings_set_pointer_len(settings, FreeRDP_RdpServerRsaKey, key, 1))
    { freerdp_key_free(key); freerdp_certificate_free(cert); goto cleanup; }
    if (!freerdp_settings_set_pointer_len(settings, FreeRDP_RdpServerCertificate, cert, 1))
    { freerdp_certificate_free(cert); goto cleanup; }
    if (!freerdp_settings_set_bool(settings, FreeRDP_RdpSecurity, FALSE) ||
        !freerdp_settings_set_bool(settings, FreeRDP_TlsSecurity, TRUE) ||
        !freerdp_settings_set_bool(settings, FreeRDP_NlaSecurity, FALSE) ||
        !freerdp_settings_set_bool(settings, FreeRDP_ExtSecurity, FALSE) ||
        !freerdp_settings_set_bool(settings, FreeRDP_RemoteFxCodec, FALSE) ||
        !freerdp_settings_set_bool(settings, FreeRDP_NSCodec, FALSE) ||
        !freerdp_settings_set_bool(settings, FreeRDP_RefreshRect, TRUE) ||
        !freerdp_settings_set_bool(settings, FreeRDP_SuppressOutput, TRUE) ||
        !freerdp_settings_set_uint32(settings, FreeRDP_ColorDepth, 32)) goto cleanup;
    peer->Capabilities = Capabilities;
    peer->PostConnect = PostConnect; peer->Activate = Activate;
    ctx->rdp.input->SynchronizeEvent = Synchronize;
    ctx->rdp.input->KeyboardEvent = Keyboard;
    ctx->rdp.input->UnicodeKeyboardEvent = UnicodeKeyboard;
    ctx->rdp.input->MouseEvent = Mouse;
    ctx->rdp.update->RefreshRect = Refresh;
    ctx->rdp.update->SuppressOutput = Suppress;
    if (!peer->Initialize(peer)) goto cleanup;
    for (;;)
    {
        events[0] = StopEvent;
        count = peer->GetEventHandles(peer, events + 1, 30);
        if (!count) break;
        events[++count] = WTSVirtualChannelManagerGetEventHandle(ctx->Vcm);
        status = WaitForMultipleObjects(count + 1, events, FALSE, 100);
        if (status == WAIT_OBJECT_0 || status == WAIT_FAILED) break;
        if (!peer->CheckFileDescriptor(peer)) break;
        if (!WTSVirtualChannelManagerCheckFileDescriptor(ctx->Vcm)) break;
        if (!ctx->Authorized && GetTickCount() - started > 30000) break;
        if (ctx->Active && peer->activated && GetTickCount() - lastFrame >= 100)
        {
            if (!Capture(ctx)) break;
            lastFrame = GetTickCount();
        }
    }
    peer->Disconnect(peer);
cleanup:
    freerdp_peer_context_free(peer);
done:
    freerdp_peer_free(peer);
    InterlockedExchange(&ClientConnected, 0);
    return 0;
}

static BOOL Accepted(freerdp_listener *listener, freerdp_peer *peer)
{
    HANDLE thread;
    struct sockaddr_in address;
    int length = sizeof(address);
    (void)listener;
    if (getpeername((SOCKET)peer->sockfd, (struct sockaddr *)&address, &length) ||
        address.sin_family != AF_INET || address.sin_addr.s_addr != inet_addr(AllowedPeer)) return FALSE;
    if (InterlockedCompareExchange(&ClientConnected, 1, 0)) return FALSE;
    thread = CreateThread(NULL, 0, ClientThread, peer, 0, NULL);
    if (!thread) { InterlockedExchange(&ClientConnected, 0); return FALSE; }
    CloseHandle(thread);
    return TRUE;
}

static BOOL WINAPI Stop(DWORD event)
{
    (void)event;
    SetEvent(StopEvent);
    return TRUE;
}

int main(int argc, char **argv)
{
    char config[MAX_PATH], modules[MAX_PATH], *separator;
    char mac[MAX_INTERFACE_MACS * 18 + 16], mask[64], tail;
    unsigned octets[6], i;
    typedef int (__cdecl *SET_PROVIDER_PATH)(void *, const char *);
    SET_PROVIDER_PATH setProviderPath;
    freerdp_listener *listener;
    HANDLE events[32], singleton;
    DWORD count, status;
    WSADATA winsock;
    DebugPrint = (DEBUG_PRINT)(void *)GetProcAddress(GetModuleHandleA("ntdll.dll"), "DbgPrint");
    if (argc != 2 || !GetFullPathNameA(argv[1], sizeof(config), config, NULL))
    { Log("Usage: rdpserver.exe path-to-test-config.ini"); return 2; }
    if (!GetPrivateProfileIntA("server", "enabled", 0, config)) return 0;
    GetPrivateProfileStringA("server", "bind", "", BindAddress, sizeof(BindAddress), config);
    GetPrivateProfileStringA("server", "allow", "", AllowedPeer, sizeof(AllowedPeer), config);
    GetPrivateProfileStringA("server", "username", "", Username, sizeof(Username), config);
    GetPrivateProfileStringA("server", "password", "", Password, sizeof(Password), config);
    GetPrivateProfileStringA("server", "certificate", "", Certificate, sizeof(Certificate), config);
    GetPrivateProfileStringA("server", "privatekey", "", PrivateKey, sizeof(PrivateKey), config);
    GetPrivateProfileStringA("server", "interface_mac", "", mac, sizeof(mac), config);
    GetPrivateProfileStringA("server", "netmask", "", mask, sizeof(mask), config);
    {
        char root[MAX_PATH];
        GetPrivateProfileStringA("server", "transfer_root", "", root, sizeof(root), config);
        if (*root && !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, root, -1,
                                         TransferRoot, MAX_PATH)) return 2;
    }
    if (mac[0])
    {
        char *entry, *next;
        if ((InterfaceMask = inet_addr(mask)) == INADDR_NONE || !InterfaceMask)
        { Log("Invalid test Ethernet MAC or netmask"); return 2; }
        for (entry = mac; entry; entry = next)
        {
            next = strchr(entry, ',');
            if (next) *next++ = 0;
            if (InterfaceMacCount == MAX_INTERFACE_MACS ||
                sscanf(entry, " %x:%x:%x:%x:%x:%x %c", &octets[0], &octets[1], &octets[2],
                       &octets[3], &octets[4], &octets[5], &tail) != 6)
            { Log("Invalid test Ethernet MAC or netmask"); return 2; }
            for (i = 0; i < 6; ++i)
            {
                if (octets[i] > 255) return 2;
                InterfaceMac[InterfaceMacCount][i] = (BYTE)octets[i];
            }
            ++InterfaceMacCount;
        }
        ConfigureAddress = TRUE;
    }
    if (!ConfigRelativePath(config, Certificate, sizeof(Certificate)) ||
        !ConfigRelativePath(config, PrivateKey, sizeof(PrivateKey)))
    { Log("Certificate or private key path is too long"); return 2; }
    if (!Username[0] || strlen(Password) < 16 || inet_addr(BindAddress) == INADDR_NONE ||
        inet_addr(BindAddress) == INADDR_ANY || inet_addr(AllowedPeer) == INADDR_NONE ||
        inet_addr(AllowedPeer) == INADDR_ANY || !Certificate[0] || !PrivateKey[0])
    { Log("Invalid configuration: explicit bind/peer IPv4, credentials and TLS files required"); return 2; }
    singleton = CreateMutexA(NULL, FALSE, "Local\\LiberNT-RDPServer-Console");
    if (!singleton || GetLastError() == ERROR_ALREADY_EXISTS) { if (singleton) CloseHandle(singleton); return 1; }
    if (WSAStartup(MAKEWORD(2, 2), &winsock)) return 1;
    if (!GetModuleFileNameA(NULL, modules, sizeof(modules))) return 1;
    separator = strrchr(modules, '\\');
    if (!separator || (size_t)(separator - modules) + sizeof("\\ossl-modules") > sizeof(modules)) return 1;
    strcpy(separator, "\\ossl-modules");
    SetEnvironmentVariableA("OPENSSL_MODULES", modules);
    /* OpenSSL's CRT may cache its environment before main. Set its provider
     * path directly so ReactOS does not fall back to the build-machine path. */
    setProviderPath = (SET_PROVIDER_PATH)(void *)GetProcAddress(
        GetModuleHandleA("libcrypto-3-x64.dll"), "OSSL_PROVIDER_set_default_search_path");
    if (!setProviderPath || !setProviderPath(NULL, modules))
    { Log("Cannot configure the bundled OpenSSL provider directory"); return 1; }
    if (!winpr_InitializeSSL(WINPR_SSL_INIT_DEFAULT)) return 1;
    StopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!StopEvent) return 1;
    SetConsoleCtrlHandler(Stop, TRUE);
    listener = freerdp_listener_new();
    if (!listener) return 1;
    listener->PeerAccepted = Accepted;
    if (!WTSRegisterWtsApiFunctionTable(FreeRDP_InitWtsApi())) return 1;
    Log("Starting console server; waiting for %s:3389", BindAddress);
    /* Autostart may run before PnP has finished configuring Ethernet. */
    while (!EnsureAddress() || !listener->Open(listener, BindAddress, 3389))
        if (WaitForSingleObject(StopEvent, 2000) != WAIT_TIMEOUT) goto done;
    Log("Listening on %s:3389; TLS; allowed controller %s", BindAddress, AllowedPeer);
    for (;;)
    {
        events[0] = StopEvent;
        count = listener->GetEventHandles(listener, events + 1, 31);
        if (!count) break;
        status = WaitForMultipleObjects(count + 1, events, FALSE, INFINITE);
        if (status == WAIT_OBJECT_0 || status == WAIT_FAILED) break;
        if (!listener->CheckFileDescriptor(listener)) break;
    }
    listener->Close(listener);
done:
    SetEvent(StopEvent);
    while (InterlockedCompareExchange(&ClientConnected, 0, 0)) Sleep(50);
    freerdp_listener_free(listener);
    SetConsoleCtrlHandler(Stop, FALSE);
    CloseHandle(StopEvent); CloseHandle(singleton);
    SecureZeroMemory(Password, sizeof(Password));
    if (AddressAdded) DeleteIPAddress(AddressContext);
    WSACleanup();
    return 0;
}
