/*
 * PROJECT:     LiberNT RDP console server
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Remote Desktop server of the console session
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
#include <sddl.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <freerdp/freerdp.h>
#include <freerdp/peer.h>
#include <freerdp/listener.h>
#include <freerdp/settings.h>
#include <freerdp/constants.h>
#include <freerdp/codec/color.h>
#include <freerdp/codec/planar.h>
#include <freerdp/crypto/certificate.h>
#include <freerdp/crypto/privatekey.h>
#include <winpr/ssl.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/x509v3.h>
#include <freerdp/channels/wtsvc.h>
#include <freerdp/channels/channels.h>
#include <ntsecapi.h>
#include "transfer.h"

typedef ULONG (__cdecl *DEBUG_PRINT)(const char *, ...);
static DEBUG_PRINT DebugPrint;
static HANDLE StopEvent;
static volatile LONG ClientCount, ClientGeneration;
static char Certificate[MAX_PATH], PrivateKey[MAX_PATH];
static WCHAR TransferRoot[MAX_PATH];
static SERVICE_STATUS_HANDLE ServiceHandle;
static SERVICE_STATUS ServiceState;
static HANDLE ServiceStopEvent, SessionStopEvent;
static char ServiceConfig[MAX_PATH];

#define SERVICE_NAME "LiberNTRDPServer"
#define SERVICE_STOP_EVENT "Global\\LiberNT-RDPServer-Stop"
#define TERMINAL_SERVER_KEY "SYSTEM\\CurrentControlSet\\Control\\Terminal Server"
#define CERTIFICATE_DAYS 183

typedef struct DESKTOP_CONTEXT
{
    rdpContext rdp;
    BOOL Authorized, Active, Suppressed, Refresh, NlaLogon;
    LONG Generation;
    int Width, Height;
    HDC ScreenDC, MemoryDC;
    HBITMAP Bitmap;
    HGDIOBJ OldBitmap;
    HDESK Desktop, HomeDesktop;
    BYTE *Pixels, *Previous;
    BYTE Tile[64 * 32 * 4 + 2];
    BITMAP_PLANAR_CONTEXT *Planar;
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

static BOOL FollowInputDesktop(DESKTOP_CONTEXT *ctx);

static BOOL Superseded(DESKTOP_CONTEXT *ctx)
{
    return ctx->Authorized && ctx->Generation != InterlockedCompareExchange(&ClientGeneration, 0, 0);
}

static BOOL InputReady(rdpInput *input)
{
    DESKTOP_CONTEXT *ctx = (DESKTOP_CONTEXT *)input->context;
    return ctx->Authorized && ctx->Active && !Superseded(ctx) && FollowInputDesktop(ctx);
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
    ctx->Refresh = TRUE;
    return ctx->Previous != NULL;
}

/* Winlogon owns input before logon and while locked; follow the input desktop. */
static BOOL FollowInputDesktop(DESKTOP_CONTEXT *ctx)
{
    char inputName[64], currentName[64];
    DWORD length;
    HDESK current = GetThreadDesktop(GetCurrentThreadId());
    HDESK input = OpenInputDesktop(0, FALSE, GENERIC_ALL);
    if (!input) return ctx->ScreenDC || AllocateCapture(ctx);
    if (GetUserObjectInformationA(input, UOI_NAME, inputName, sizeof(inputName), &length) &&
        GetUserObjectInformationA(current, UOI_NAME, currentName, sizeof(currentName), &length) &&
        !_stricmp(inputName, currentName))
    {
        CloseDesktop(input);
        return ctx->ScreenDC || AllocateCapture(ctx);
    }
    FreeDesktopCapture(ctx);
    if (!ctx->HomeDesktop) ctx->HomeDesktop = current;
    if (!SetThreadDesktop(input))
    {
        Log("Cannot switch to the input desktop error=%lu", GetLastError());
        CloseDesktop(input);
        return FALSE;
    }
    if (ctx->Desktop) CloseDesktop(ctx->Desktop);
    ctx->Desktop = input;
    Log("Following input desktop %s", inputName);
    return AllocateCapture(ctx);
}

static BOOL Capabilities(freerdp_peer *peer)
{
    return freerdp_settings_set_uint32(peer->context->settings, FreeRDP_DesktopWidth,
                                       GetSystemMetrics(SM_CXSCREEN)) &&
           freerdp_settings_set_uint32(peer->context->settings, FreeRDP_DesktopHeight,
                                       GetSystemMetrics(SM_CYSCREEN));
}

static BOOL HasAccountRight(LSA_HANDLE policy, PSID sid, const WCHAR *right)
{
    LSA_UNICODE_STRING *rights;
    ULONG count, i;
    BOOL found = FALSE;
    if (LsaEnumerateAccountRights(policy, sid, &rights, &count)) return FALSE;
    for (i = 0; i < count && !found; ++i)
        found = rights[i].Length == wcslen(right) * sizeof(WCHAR) &&
                !_wcsnicmp(rights[i].Buffer, right, wcslen(right));
    LsaFreeMemory(rights);
    return found;
}

static BOOL RemoteLogonAllowed(HANDLE token)
{
    LSA_OBJECT_ATTRIBUTES attributes = {0};
    LSA_HANDLE policy;
    TOKEN_GROUPS *groups = NULL;
    BYTE user[SECURITY_MAX_SID_SIZE + sizeof(TOKEN_USER)];
    DWORD length = 0, i;
    BOOL allowed = FALSE, denied = FALSE;
    if (!GetTokenInformation(token, TokenUser, user, sizeof(user), &length)) return FALSE;
    GetTokenInformation(token, TokenGroups, NULL, 0, &length);
    groups = length ? malloc(length) : NULL;
    if (!groups || !GetTokenInformation(token, TokenGroups, groups, length, &length) ||
        LsaOpenPolicy(NULL, &attributes, POLICY_LOOKUP_NAMES, &policy))
    {
        free(groups);
        return FALSE;
    }
    denied = HasAccountRight(policy, ((TOKEN_USER *)user)->User.Sid, L"SeDenyRemoteInteractiveLogonRight");
    allowed = HasAccountRight(policy, ((TOKEN_USER *)user)->User.Sid, L"SeRemoteInteractiveLogonRight");
    for (i = 0; i < groups->GroupCount && !denied; ++i)
    {
        denied = HasAccountRight(policy, groups->Groups[i].Sid, L"SeDenyRemoteInteractiveLogonRight");
        if (!allowed && (groups->Groups[i].Attributes & SE_GROUP_ENABLED))
            allowed = HasAccountRight(policy, groups->Groups[i].Sid, L"SeRemoteInteractiveLogonRight");
    }
    LsaClose(policy);
    free(groups);
    return allowed && !denied;
}

static BOOL Logon(freerdp_peer *peer, const SEC_WINNT_AUTH_IDENTITY *identity, BOOL automatic)
{
    DESKTOP_CONTEXT *ctx = (DESKTOP_CONTEXT *)peer->context;
    rdpSettings *settings = ctx->rdp.settings;
    const char *user = freerdp_settings_get_string(settings, FreeRDP_Username);
    const char *domain = freerdp_settings_get_string(settings, FreeRDP_Domain);
    const char *password = freerdp_settings_get_string(settings, FreeRDP_Password);
    WCHAR wuser[128], wdomain[256], wpassword[256];
    HANDLE token;
    (void)identity;
    if (!automatic || !user || !password ||
        !MultiByteToWideChar(CP_UTF8, 0, user, -1, wuser, ARRAYSIZE(wuser)) ||
        !MultiByteToWideChar(CP_UTF8, 0, (domain && *domain) ? domain : ".", -1, wdomain, ARRAYSIZE(wdomain)) ||
        !MultiByteToWideChar(CP_UTF8, 0, password, -1, wpassword, ARRAYSIZE(wpassword)))
    {
        Log("NLA logon rejected for peer %s", peer->hostname);
        return FALSE;
    }
    ctx->NlaLogon = LogonUserW(wuser, wdomain, wpassword, LOGON32_LOGON_INTERACTIVE, LOGON32_PROVIDER_DEFAULT, &token);
    SecureZeroMemory(wpassword, sizeof(wpassword));
    if (ctx->NlaLogon)
    {
        ctx->NlaLogon = RemoteLogonAllowed(token);
        if (!ctx->NlaLogon) SetLastError(ERROR_LOGON_TYPE_NOT_GRANTED);
        CloseHandle(token);
    }
    Log("NLA logon of %s for peer %s: %s (%lu)", user, peer->hostname, ctx->NlaLogon ? "accepted" : "rejected",
        ctx->NlaLogon ? 0UL : GetLastError());
    return ctx->NlaLogon;
}

static BOOL PostConnect(freerdp_peer *peer)
{
    DESKTOP_CONTEXT *ctx = (DESKTOP_CONTEXT *)peer->context;
    if (!ctx->NlaLogon)
    {
        Log("Authentication rejected for peer %s", peer->hostname);
        return FALSE;
    }
    ctx->Generation = InterlockedIncrement(&ClientGeneration);
    ctx->Authorized = TRUE;
    if (!FollowInputDesktop(ctx)) return FALSE;
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
    if (!ctx->Planar)
    {
        DWORD flags = PLANAR_FORMAT_HEADER_RLE;
        if (freerdp_settings_get_bool(ctx->rdp.settings, FreeRDP_DrawAllowSkipAlpha)) flags |= PLANAR_FORMAT_HEADER_NA;
        ctx->Planar = freerdp_bitmap_planar_context_new(flags, 64, 32);
        if (!ctx->Planar) return FALSE;
    }
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
    if (!FollowInputDesktop(ctx)) return FALSE;
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
    rectangle.bitsPerPixel = 32; rectangle.compressed = TRUE;
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
            rectangle.bitmapLength = 0;
            rectangle.bitmapDataStream = freerdp_bitmap_compress_planar(ctx->Planar,
                ctx->Pixels + ((size_t)y * ctx->Width + x) * 4, PIXEL_FORMAT_BGRX32, width, height,
                ctx->Width * 4, ctx->Tile, &rectangle.bitmapLength);
            if (!rectangle.bitmapDataStream) return FALSE;
            rectangle.destLeft = x; rectangle.destTop = y;
            rectangle.destRight = x + width - 1; rectangle.destBottom = y + height - 1;
            rectangle.width = width; rectangle.height = height;
            rectangle.cbScanWidth = width * 4;
            rectangle.cbUncompressedSize = width * height * 4;
            rectangle.cbCompMainBodySize = rectangle.bitmapLength;
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
    freerdp_bitmap_planar_context_free(ctx->Planar);
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
    if (ctx->Desktop && SetThreadDesktop(ctx->HomeDesktop)) CloseDesktop(ctx->Desktop);
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
        !freerdp_settings_set_bool(settings, FreeRDP_TlsSecurity, FALSE) ||
        !freerdp_settings_set_bool(settings, FreeRDP_NlaSecurity, TRUE) ||
        /* TODO: HYBRID_EX, which needs the Early User Authorization Result PDU from the FreeRDP server */
        !freerdp_settings_set_bool(settings, FreeRDP_ExtSecurity, FALSE) ||
        !freerdp_settings_set_bool(settings, FreeRDP_RemoteFxCodec, FALSE) ||
        !freerdp_settings_set_bool(settings, FreeRDP_NSCodec, FALSE) ||
        !freerdp_settings_set_bool(settings, FreeRDP_RefreshRect, TRUE) ||
        !freerdp_settings_set_bool(settings, FreeRDP_SuppressOutput, TRUE) ||
        !freerdp_settings_set_uint32(settings, FreeRDP_ColorDepth, 32)) goto cleanup;
    peer->Capabilities = Capabilities;
    peer->Logon = Logon; peer->PostConnect = PostConnect; peer->Activate = Activate;
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
        if (Superseded(ctx))
        {
            Log("Peer %s replaced by a newer connection", peer->hostname);
            freerdp_set_error_info(ctx->rdp.rdp, ERRINFO_DISCONNECTED_BY_OTHER_CONNECTION);
            if (!freerdp_send_error_info(ctx->rdp.rdp)) Log("Cannot tell peer %s why it is disconnected", peer->hostname);
            break;
        }
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
    InterlockedDecrement(&ClientCount);
    return 0;
}

static BOOL Accepted(freerdp_listener *listener, freerdp_peer *peer)
{
    HANDLE thread;
    (void)listener;
    InterlockedIncrement(&ClientCount);
    thread = CreateThread(NULL, 0, ClientThread, peer, 0, NULL);
    if (!thread) { InterlockedDecrement(&ClientCount); return FALSE; }
    CloseHandle(thread);
    return TRUE;
}

static BOOL WINAPI Stop(DWORD event)
{
    (void)event;
    SetEvent(StopEvent);
    return TRUE;
}

static BOOL WriteFileData(const char *path, BIO *bio, const char *sddl)
{
    SECURITY_ATTRIBUTES security = {sizeof(security), NULL, FALSE};
    char *data;
    long size = BIO_get_mem_data(bio, &data);
    DWORD written = 0;
    HANDLE file;
    BOOL ok;
    if (size <= 0) return FALSE;
    if (sddl && !ConvertStringSecurityDescriptorToSecurityDescriptorA(sddl, SDDL_REVISION_1,
                                                                      &security.lpSecurityDescriptor, NULL))
        return FALSE;
    DeleteFileA(path);
    file = CreateFileA(path, GENERIC_WRITE, 0, &security, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    LocalFree(security.lpSecurityDescriptor);
    if (file == INVALID_HANDLE_VALUE) return FALSE;
    ok = WriteFile(file, data, (DWORD)size, &written, NULL) && written == (DWORD)size;
    CloseHandle(file);
    return ok;
}

static BOOL AddExtension(X509 *x509, int nid, const char *value)
{
    X509V3_CTX v3;
    X509_EXTENSION *extension;
    BOOL ok;
    X509V3_set_ctx(&v3, x509, x509, NULL, NULL, 0);
    extension = X509V3_EXT_conf_nid(NULL, &v3, nid, value);
    ok = extension && X509_add_ext(x509, extension, -1);
    X509_EXTENSION_free(extension);
    return ok;
}

/* FIXME: Windows keeps this certificate in the Remote Desktop store of the machine. */
static BOOL CreateCertificate(void)
{
    WCHAR wide[MAX_COMPUTERNAME_LENGTH + 1];
    char name[3 * (MAX_COMPUTERNAME_LENGTH + 1)];
    DWORD length = ARRAYSIZE(wide);
    unsigned char serial[16];
    EVP_PKEY *key = NULL;
    X509 *x509 = NULL;
    X509_NAME *subject;
    BIGNUM *number = NULL;
    BIO *keyData = BIO_new(BIO_s_mem()), *certificateData = BIO_new(BIO_s_mem());
    BOOL ok = keyData && certificateData && GetComputerNameW(wide, &length) &&
              WideCharToMultiByte(CP_UTF8, 0, wide, -1, name, sizeof(name), NULL, NULL) &&
              RAND_bytes(serial, sizeof(serial)) == 1 &&
              (key = EVP_RSA_gen(2048)) && (x509 = X509_new());
    if (ok)
    {
        serial[0] &= 0x7f;
        number = BN_bin2bn(serial, sizeof(serial), NULL);
        subject = X509_get_subject_name(x509);
        ok = number && BN_to_ASN1_INTEGER(number, X509_get_serialNumber(x509)) &&
             X509_set_version(x509, 2) &&
             X509_gmtime_adj(X509_getm_notBefore(x509), 0) &&
             X509_gmtime_adj(X509_getm_notAfter(x509), (long)CERTIFICATE_DAYS * 24 * 60 * 60) &&
             X509_NAME_add_entry_by_txt(subject, "CN", MBSTRING_UTF8, (const unsigned char *)name, -1, -1, 0) &&
             X509_set_issuer_name(x509, subject) && X509_set_pubkey(x509, key) &&
             AddExtension(x509, NID_ext_key_usage, "serverAuth") &&
             AddExtension(x509, NID_key_usage, "keyEncipherment,dataEncipherment") &&
             X509_sign(x509, key, EVP_sha256()) &&
             PEM_write_bio_PrivateKey(keyData, key, NULL, NULL, 0, NULL, NULL) &&
             PEM_write_bio_X509(certificateData, x509) &&
             WriteFileData(PrivateKey, keyData, "D:P(A;;FA;;;SY)(A;;FA;;;BA)") &&
             WriteFileData(Certificate, certificateData, NULL);
    }
    Log("Self-signed certificate for %s: %s", ok ? name : "this computer", ok ? "created" : "failed");
    BN_free(number);
    X509_free(x509);
    EVP_PKEY_free(key);
    BIO_free(keyData);
    BIO_free(certificateData);
    return ok;
}

static BOOL EnsureCertificate(void)
{
    BIO *data = BIO_new_file(Certificate, "r");
    X509 *x509 = data ? PEM_read_bio_X509(data, NULL, NULL, NULL) : NULL;
    BOOL valid = x509 && GetFileAttributesA(PrivateKey) != INVALID_FILE_ATTRIBUTES &&
                 X509_cmp_current_time(X509_get0_notAfter(x509)) > 0;
    X509_free(x509);
    BIO_free(data);
    return valid || CreateCertificate();
}

static void ReportService(DWORD state, DWORD exitCode)
{
    ServiceState.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    ServiceState.dwCurrentState = state;
    ServiceState.dwControlsAccepted = state == SERVICE_RUNNING ? SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN : 0;
    ServiceState.dwWin32ExitCode = exitCode;
    ServiceState.dwWaitHint = state == SERVICE_STOP_PENDING ? 15000 : 0;
    SetServiceStatus(ServiceHandle, &ServiceState);
}

static DWORD WINAPI ServiceControl(DWORD control, DWORD type, void *data, void *context)
{
    (void)type; (void)data; (void)context;
    if (control != SERVICE_CONTROL_STOP && control != SERVICE_CONTROL_SHUTDOWN) return NO_ERROR;
    ReportService(SERVICE_STOP_PENDING, NO_ERROR);
    SetEvent(ServiceStopEvent);
    return NO_ERROR;
}

/* Session 0 has no interactive desktop; serve the console session as SYSTEM. */
static HANDLE StartSessionServer(DWORD session)
{
    char path[MAX_PATH], command[2 * MAX_PATH + 16];
    HANDLE token = NULL, primary = NULL;
    STARTUPINFOA startup = {0};
    PROCESS_INFORMATION process = {0};
    BOOL started = FALSE;
    startup.cb = sizeof(startup);
    startup.lpDesktop = "winsta0\\default";
    if (GetModuleFileNameA(NULL, path, sizeof(path)) &&
        snprintf(command, sizeof(command), *ServiceConfig ? "\"%s\" /session \"%s\"" : "\"%s\" /session",
                 path, ServiceConfig) < (int)sizeof(command) &&
        OpenProcessToken(GetCurrentProcess(), TOKEN_DUPLICATE, &token) &&
        DuplicateTokenEx(token, TOKEN_ALL_ACCESS, NULL, SecurityImpersonation, TokenPrimary, &primary) &&
        SetTokenInformation(primary, TokenSessionId, &session, sizeof(session)))
        started = CreateProcessAsUserA(primary, path, command, NULL, NULL, FALSE, CREATE_NO_WINDOW,
                                       NULL, NULL, &startup, &process);
    if (!started) Log("Cannot start the server in console session %lu error=%lu", session, GetLastError());
    if (primary) CloseHandle(primary);
    if (token) CloseHandle(token);
    if (!started) return NULL;
    CloseHandle(process.hThread);
    Log("Server started in console session %lu", session);
    return process.hProcess;
}

static void StopSessionServer(HANDLE *server)
{
    SetEvent(SessionStopEvent);
    if (WaitForSingleObject(*server, 10000) == WAIT_TIMEOUT) TerminateProcess(*server, 1);
    CloseHandle(*server);
    *server = NULL;
    ResetEvent(SessionStopEvent);
}

static BOOL RemoteDesktopAllowed(HKEY key)
{
    DWORD deny = 1, size = sizeof(deny), type;
    if (RegQueryValueExA(key, "fDenyTSConnections", NULL, &type, (BYTE *)&deny, &size) || type != REG_DWORD)
        deny = 1;
    return !deny;
}

static void WINAPI ServiceMain(DWORD argc, char **argv)
{
    HANDLE server = NULL, changed, waits[2];
    HKEY key = NULL;
    DWORD session = 0, console, code = 0, wait;
    BOOL armed = FALSE, failed = FALSE, allowed;
    (void)argc; (void)argv;
    ServiceHandle = RegisterServiceCtrlHandlerExA(SERVICE_NAME, ServiceControl, NULL);
    if (!ServiceHandle) return;
    ServiceStopEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
    SessionStopEvent = CreateEventA(NULL, TRUE, FALSE, SERVICE_STOP_EVENT);
    changed = CreateEventA(NULL, FALSE, FALSE, NULL);
    if (!ServiceStopEvent || !SessionStopEvent || !changed ||
        RegOpenKeyExA(HKEY_LOCAL_MACHINE, TERMINAL_SERVER_KEY, 0, KEY_QUERY_VALUE | KEY_NOTIFY, &key))
    {
        code = GetLastError();
        goto done;
    }
    waits[0] = ServiceStopEvent;
    waits[1] = changed;
    ReportService(SERVICE_RUNNING, NO_ERROR);
    for (;;)
    {
        if (!armed)
            armed = !RegNotifyChangeKeyValue(key, FALSE, REG_NOTIFY_CHANGE_LAST_SET, changed, TRUE);
        allowed = RemoteDesktopAllowed(key);
        console = WTSGetActiveConsoleSessionId();
        if (server && WaitForSingleObject(server, 0) == WAIT_OBJECT_0)
        {
            GetExitCodeProcess(server, &code);
            CloseHandle(server);
            server = NULL;
            Log("Server in console session %lu exited with %lu", session, code);
            failed = code == 2;
        }
        if (server && (!allowed || console != session)) StopSessionServer(&server);
        if (!server && !failed && allowed && console != 0 && console != 0xFFFFFFFF)
        {
            server = StartSessionServer(console);
            session = console;
        }
        wait = WaitForMultipleObjects(2, waits, FALSE, 2000);
        if (wait == WAIT_OBJECT_0 || wait == WAIT_FAILED) break;
        if (wait == WAIT_OBJECT_0 + 1) armed = failed = FALSE;
    }
    if (server) StopSessionServer(&server);
    code = NO_ERROR;
done:
    if (key) RegCloseKey(key);
    if (changed) CloseHandle(changed);
    if (SessionStopEvent) CloseHandle(SessionStopEvent);
    if (ServiceStopEvent) CloseHandle(ServiceStopEvent);
    ReportService(SERVICE_STOPPED, code);
}

static DWORD WINAPI WaitServiceStop(void *event)
{
    if (WaitForSingleObject(event, INFINITE) == WAIT_OBJECT_0) SetEvent(StopEvent);
    return 0;
}

int main(int argc, char **argv)
{
    char config[MAX_PATH] = "", directory[MAX_PATH], *separator;
    freerdp_listener *listener;
    HANDLE events[32], singleton;
    DWORD count, status;
    WSADATA winsock;
    HANDLE serviceStop = NULL, thread;
    BOOL serviceMode = argc >= 2 && !_stricmp(argv[1], "/service");
    BOOL sessionMode = argc >= 2 && !_stricmp(argv[1], "/session");
    int first = (serviceMode || sessionMode) ? 2 : 1;
    DebugPrint = (DEBUG_PRINT)(void *)GetProcAddress(GetModuleHandleA("ntdll.dll"), "DbgPrint");
    if (argc > first + 1 || (argc == first + 1 && !GetFullPathNameA(argv[first], sizeof(config), config, NULL)))
    { Log("Usage: rdpserver.exe [/service] [rdpserver.ini]"); return 2; }
    if (serviceMode)
    {
        SERVICE_TABLE_ENTRYA table[] = {{(char *)SERVICE_NAME, ServiceMain}, {NULL, NULL}};
        strcpy(ServiceConfig, config);
        return StartServiceCtrlDispatcherA(table) ? 0 : 1;
    }
    if (*config)
    {
        char root[MAX_PATH];
        GetPrivateProfileStringA("server", "transfer_root", "", root, sizeof(root), config);
        if (*root && !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, root, -1,
                                         TransferRoot, MAX_PATH)) return 2;
    }
    if (!GetModuleFileNameA(NULL, directory, sizeof(directory))) return 1;
    separator = strrchr(directory, '\\');
    if (!separator) return 1;
    *separator = 0;
    if (snprintf(Certificate, sizeof(Certificate), "%s\\server.crt", directory) >= (int)sizeof(Certificate) ||
        snprintf(PrivateKey, sizeof(PrivateKey), "%s\\server.key", directory) >= (int)sizeof(PrivateKey)) return 1;
    singleton = CreateMutexA(NULL, FALSE, "Local\\LiberNT-RDPServer-Console");
    if (!singleton || GetLastError() == ERROR_ALREADY_EXISTS) { if (singleton) CloseHandle(singleton); return 1; }
    if (WSAStartup(MAKEWORD(2, 2), &winsock)) return 1;
    if (!winpr_InitializeSSL(WINPR_SSL_INIT_DEFAULT)) return 1;
    if (!EnsureCertificate()) return 2;
    StopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!StopEvent) return 1;
    if (sessionMode)
    {
        serviceStop = OpenEventA(SYNCHRONIZE, FALSE, SERVICE_STOP_EVENT);
        thread = serviceStop ? CreateThread(NULL, 0, WaitServiceStop, serviceStop, 0, NULL) : NULL;
        if (!thread) { Log("Cannot watch the service stop event error=%lu", GetLastError()); return 1; }
        CloseHandle(thread);
    }
    SetConsoleCtrlHandler(Stop, TRUE);
    listener = freerdp_listener_new();
    if (!listener) return 1;
    listener->PeerAccepted = Accepted;
    if (!WTSRegisterWtsApiFunctionTable(FreeRDP_InitWtsApi())) return 1;
    Log("Starting console server on port 3389");
    /* Autostart may run before PnP has finished configuring the network. */
    while (!listener->Open(listener, NULL, 3389))
        if (WaitForSingleObject(StopEvent, 2000) != WAIT_TIMEOUT) goto done;
    Log("Listening on port 3389 with NLA");
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
    while (InterlockedCompareExchange(&ClientCount, 0, 0)) Sleep(50);
    freerdp_listener_free(listener);
    SetConsoleCtrlHandler(Stop, FALSE);
    CloseHandle(StopEvent); CloseHandle(singleton);
    WSACleanup();
    return 0;
}
