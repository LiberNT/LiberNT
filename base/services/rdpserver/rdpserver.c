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
#include <mmsystem.h>
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
#include <freerdp/server/disp.h>
#include <ntsecapi.h>
#include <swdevice.h>
#include <cfgmgr32.h>
#include <initguid.h>
#include <ntddvdeo.h>
#include <d3dkmthk.h>
#include <drivers/directx/rdpvdd.h>
#include <remoteconsole.h>
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
static DWORD FrameInterval;
static CRITICAL_SECTION RemoteDisplayLock;
static volatile LONG ConsoleTakeover, ConnectionNumber;
static HSWDEVICE RemoteDisplay;
static LONG RemoteDisplayUsers;
static HANDLE RemoteDisplayReady;
static WCHAR RemoteDisplayInstance[MAX_DEVICE_ID_LEN];
static BOOL RemoteDisplayShown;
static HANDLE RemoteConsole;

#define SERVICE_NAME "LiberNTRDPServer"
#define SERVICE_STOP_EVENT "Global\\LiberNT-RDPServer-Stop"
#define TERMINAL_SERVER_KEY "SYSTEM\\CurrentControlSet\\Control\\Terminal Server"
#define WINSTATIONS_KEY TERMINAL_SERVER_KEY "\\WinStations"
#define CERTIFICATE_DAYS 183
#define TILE_BATCH 64
#define TILE_WIDTH 64
#define TILE_HEIGHT 32
#define TILE_BYTES (TILE_WIDTH * TILE_HEIGHT * 4 + 2)
/* TS_UPDATE_BITMAP_DATA header, and TS_BITMAP_DATA fields with its TS_CD_HEADER */
#define UPDATE_HEADER_BYTES 4
#define TILE_HEADER_BYTES 26

typedef struct DESKTOP_CONTEXT
{
    rdpContext rdp;
    BOOL Authorized, Active, Suppressed, Refresh, NlaLogon, RemoteDisplay;
    LONG Generation;
    int Width, Height, ClientWidth, ClientHeight, TileColumns, TileRows;
    HDC ScreenDC, MemoryDC;
    HBITMAP Bitmap;
    HGDIOBJ OldBitmap;
    HDESK Desktop, HomeDesktop;
    BYTE *Pixels, *Previous, *Frame, *TileMask;
    D3DKMT_HANDLE FrameAdapter;
    HANDLE FrameEvent;
    BOOL FramePending;
    int FrameWidth, FrameHeight;
    RECT CursorRect;
    POINT CursorPoint;
    HCURSOR CursorShape;
    BITMAP_DATA Batch[TILE_BATCH];
    BYTE Tiles[TILE_BATCH][TILE_BYTES];
    BITMAP_PLANAR_CONTEXT *Planar;
    BOOL Keys[512];
    DWORD MouseButtons;
    ULONGLONG Frames, Updates, Inputs;
    HANDLE Vcm;
    RDP_TRANSFER *Transfer;
    DispServerContext *Disp;
    UINT32 DispChannelId;
    volatile LONG RequestedSize;
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

static ULONGLONG Milliseconds(void)
{
    LARGE_INTEGER counter, frequency;
    QueryPerformanceCounter(&counter);
    QueryPerformanceFrequency(&frequency);
    return (ULONGLONG)(counter.QuadPart / (frequency.QuadPart / 1000));
}

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
    free(ctx->Frame);
    free(ctx->TileMask);
    ctx->OldBitmap = NULL; ctx->Bitmap = NULL;
    ctx->MemoryDC = ctx->ScreenDC = NULL;
    ctx->Previous = ctx->Pixels = ctx->Frame = ctx->TileMask = NULL;
}

static NTSTATUS FrameEscape(D3DKMT_HANDLE adapter, RDPVDD_FRAME_ESCAPE *request)
{
    D3DKMT_ESCAPE escape = {0};
    escape.hAdapter = adapter;
    escape.Type = D3DKMT_ESCAPE_DRIVERPRIVATE;
    escape.pPrivateDriverData = request;
    escape.PrivateDriverDataSize = sizeof(*request);
    return D3DKMTEscape(&escape);
}

static void CloseRemoteFrames(DESKTOP_CONTEXT *ctx)
{
    D3DKMT_CLOSEADAPTER close = {0};
    if (!ctx->FrameAdapter) return;
    close.hAdapter = ctx->FrameAdapter;
    D3DKMTCloseAdapter(&close);
    if (ctx->FrameEvent) CloseHandle(ctx->FrameEvent);
    ctx->FrameAdapter = 0;
    ctx->FrameEvent = NULL;
    ctx->FramePending = FALSE;
    free(ctx->Frame);
    ctx->Frame = NULL;
}

static BOOL SyncRemoteFrames(DESKTOP_CONTEXT *ctx)
{
    RDPVDD_FRAME_ESCAPE request = {0};
    NTSTATUS status;
    request.Code = RDPVDD_ESCAPE_OPEN_FRAME;
    request.Event = (ULONG64)(ULONG_PTR)ctx->FrameEvent;
    status = FrameEscape(ctx->FrameAdapter, &request);
    if (!NT_SUCCESS(status))
    {
        Log("Cannot read the remote display: 0x%08lx", status);
        CloseRemoteFrames(ctx);
        return FALSE;
    }
    ctx->FramePending = TRUE;
    return TRUE;
}

static BOOL AllocateCapture(DESKTOP_CONTEXT *ctx)
{
    BITMAPINFO info = {0};
    FreeDesktopCapture(ctx);
    ctx->Width = ctx->FrameAdapter ? ctx->FrameWidth : GetSystemMetrics(SM_CXSCREEN);
    ctx->Height = ctx->FrameAdapter ? ctx->FrameHeight : GetSystemMetrics(SM_CYSCREEN);
    if (ctx->Width < 2 || ctx->Height < 2 || ctx->Width > 8192 || ctx->Height > 8192) return FALSE;
    ctx->TileColumns = (ctx->Width + TILE_WIDTH - 1) / TILE_WIDTH;
    ctx->TileRows = (ctx->Height + TILE_HEIGHT - 1) / TILE_HEIGHT;
    ctx->TileMask = calloc((size_t)ctx->TileColumns * ctx->TileRows, 1);
    if (!ctx->TileMask) return FALSE;
    SetRectEmpty(&ctx->CursorRect);
    if (ctx->FrameAdapter)
    {
        ctx->Frame = calloc((size_t)ctx->Width * ctx->Height, 4);
        if (!ctx->Frame) return FALSE;
    }
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
    if (!ctx->Previous) return FALSE;
    if (ctx->FrameAdapter) SyncRemoteFrames(ctx);
    return TRUE;
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
    DESKTOP_CONTEXT *ctx = (DESKTOP_CONTEXT *)peer->context;
    if (ctx->Authorized) return TRUE;
    ctx->ClientWidth = (int)freerdp_settings_get_uint32(peer->context->settings, FreeRDP_DesktopWidth);
    ctx->ClientHeight = (int)freerdp_settings_get_uint32(peer->context->settings, FreeRDP_DesktopHeight);
    if (ctx->ClientWidth >= RDPVDD_MIN_WIDTH && ctx->ClientWidth <= RDPVDD_MAX_WIDTH &&
        ctx->ClientHeight >= RDPVDD_MIN_HEIGHT && ctx->ClientHeight <= RDPVDD_MAX_HEIGHT) return TRUE;
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

static VOID WINAPI RemoteDisplayCreated(HSWDEVICE device, HRESULT result, PVOID context, PCWSTR instance)
{
    (void)device; (void)context;
    Log("Remote display adapter %ls: 0x%08lx", instance ? instance : L"", result);
    if (SUCCEEDED(result) && instance)
    {
        lstrcpynW(RemoteDisplayInstance, instance, ARRAYSIZE(RemoteDisplayInstance));
        SetEvent(RemoteDisplayReady);
    }
}

static BOOL FindRemoteDisplay(WCHAR *name)
{
    DISPLAY_DEVICEW device;
    DWORD index;
    for (index = 0;; ++index)
    {
        device.cb = sizeof(device);
        if (!EnumDisplayDevicesW(NULL, index, &device, 0)) return FALSE;
        if (!lstrcmpiW(device.DeviceID, L"RdpVdd_RemoteDisplay"))
        {
            lstrcpynW(name, device.DeviceName, ARRAYSIZE(device.DeviceName));
            return TRUE;
        }
    }
}

static BOOL WaitForRemoteDisplay(int width, int height)
{
    WCHAR name[ARRAYSIZE(((DISPLAY_DEVICEW *)0)->DeviceName)];
    DEVMODEW mode;
    DWORD index, start = GetTickCount();
    for (;;)
    {
        if (FindRemoteDisplay(name))
        {
            if (!width) return TRUE;
            for (index = 0;; ++index)
            {
                ZeroMemory(&mode, sizeof(mode));
                mode.dmSize = sizeof(mode);
                if (!EnumDisplaySettingsW(name, index, &mode)) break;
                if ((int)mode.dmPelsWidth == width && (int)mode.dmPelsHeight == height) return TRUE;
            }
        }
        if (GetTickCount() - start > 15000) return FALSE;
        Sleep(100);
    }
}

static BOOL WaitForRemoteDisplayMode(int width, int height)
{
    WCHAR name[ARRAYSIZE(((DISPLAY_DEVICEW *)0)->DeviceName)];
    DEVMODEW mode;
    DWORD start = GetTickCount();
    for (;;)
    {
        ZeroMemory(&mode, sizeof(mode));
        mode.dmSize = sizeof(mode);
        if (FindRemoteDisplay(name) && EnumDisplaySettingsW(name, ENUM_CURRENT_SETTINGS, &mode) &&
            (int)mode.dmPelsWidth == width && (int)mode.dmPelsHeight == height) return TRUE;
        if (GetTickCount() - start > 5000) return FALSE;
        Sleep(50);
    }
}

static BOOL OpenRemoteAdapter(D3DKMT_HANDLE *adapter)
{
    WCHAR interfaces[512];
    D3DKMT_OPENADAPTERFROMDEVICENAME open = {0};
    CONFIGRET cr;
    NTSTATUS status;
    cr = CM_Get_Device_Interface_ListW((LPGUID)&GUID_DISPLAY_DEVICE_ARRIVAL, RemoteDisplayInstance, interfaces,
                                       ARRAYSIZE(interfaces), CM_GET_DEVICE_INTERFACE_LIST_PRESENT);
    if (cr != CR_SUCCESS || !interfaces[0])
    {
        Log("No display interface for %ls: %lu", RemoteDisplayInstance, cr);
        return FALSE;
    }
    open.pDeviceName = interfaces;
    status = D3DKMTOpenAdapterFromDeviceName(&open);
    if (!NT_SUCCESS(status))
    {
        Log("Cannot open the remote display adapter: 0x%08lx", status);
        return FALSE;
    }
    *adapter = open.hAdapter;
    return TRUE;
}

static BOOL SetRemoteDisplayMode(int width, int height)
{
    D3DKMT_HANDLE adapter;
    D3DKMT_CLOSEADAPTER close = {0};
    D3DKMT_ESCAPE escape = {0};
    RDPVDD_ESCAPE request = {0};
    NTSTATUS status;
    if (width < RDPVDD_MIN_WIDTH || width > RDPVDD_MAX_WIDTH ||
        height < RDPVDD_MIN_HEIGHT || height > RDPVDD_MAX_HEIGHT || !OpenRemoteAdapter(&adapter)) return FALSE;
    request.Code = RDPVDD_ESCAPE_SET_MODE;
    request.Width = width;
    request.Height = height;
    escape.hAdapter = adapter;
    escape.Type = D3DKMT_ESCAPE_DRIVERPRIVATE;
    escape.pPrivateDriverData = &request;
    escape.PrivateDriverDataSize = sizeof(request);
    status = D3DKMTEscape(&escape);
    Log("Remote display mode %dx%d: 0x%08lx", width, height, status);
    close.hAdapter = adapter;
    D3DKMTCloseAdapter(&close);
    return NT_SUCCESS(status);
}

static BOOL CallRemoteConsole(REMOTE_CONSOLE_MESSAGE *message)
{
    REMOTE_CONSOLE_MESSAGE reply;
    DWORD bytes;
    while (ReadFile(RemoteConsole, &reply, sizeof(reply), &bytes, NULL) && bytes == sizeof(reply))
    {
        if (reply.Code == REMOTE_CONSOLE_TAKEOVER)
        {
            InterlockedExchange(&ConsoleTakeover, TRUE);
            continue;
        }
        *message = reply;
        return reply.Code == message->Code;
    }
    return FALSE;
}

static BOOL ConnectRemoteConsole(DESKTOP_CONTEXT *ctx)
{
    REMOTE_CONSOLE_MESSAGE message = {0};
    WCHAR pipe[64];
    DWORD session = 0, bytes, mode = PIPE_READMODE_MESSAGE;
    const char *user = freerdp_settings_get_string(ctx->rdp.settings, FreeRDP_Username);
    const char *domain = freerdp_settings_get_string(ctx->rdp.settings, FreeRDP_Domain);
    ProcessIdToSessionId(GetCurrentProcessId(), &session);
    _snwprintf(pipe, ARRAYSIZE(pipe), REMOTE_CONSOLE_PIPE_FORMAT, session);
    pipe[ARRAYSIZE(pipe) - 1] = 0;
    RemoteConsole = CreateFileW(pipe, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (RemoteConsole == INVALID_HANDLE_VALUE && GetLastError() == ERROR_PIPE_BUSY && WaitNamedPipeW(pipe, 10000))
        RemoteConsole = CreateFileW(pipe, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (RemoteConsole == INVALID_HANDLE_VALUE)
    {
        Log("Cannot reach Winlogon: %lu", GetLastError());
        RemoteConsole = NULL;
        return FALSE;
    }
    SetNamedPipeHandleState(RemoteConsole, &mode, NULL, NULL);
    message.Code = REMOTE_CONSOLE_CONNECT;
    message.Connection = (ULONG)InterlockedIncrement(&ConnectionNumber) - 1;
    if (user) MultiByteToWideChar(CP_UTF8, 0, user, -1, message.UserName, ARRAYSIZE(message.UserName) - 1);
    if (domain) MultiByteToWideChar(CP_UTF8, 0, domain, -1, message.Domain, ARRAYSIZE(message.Domain) - 1);
    if (!WriteFile(RemoteConsole, &message, sizeof(message), &bytes, NULL) || !CallRemoteConsole(&message))
        message.Status = ERROR_BROKEN_PIPE;
    Log("Session handed to RDP-Tcp#%lu: %lu", message.Connection, message.Status);
    if (message.Status == ERROR_SUCCESS) return TRUE;
    CloseHandle(RemoteConsole);
    RemoteConsole = NULL;
    return FALSE;
}

static void DisconnectRemoteConsole(BOOL takenOver)
{
    REMOTE_CONSOLE_MESSAGE message = {0};
    DWORD bytes;
    if (!RemoteConsole) return;
    message.Code = REMOTE_CONSOLE_DISCONNECT;
    message.TakenOver = takenOver;
    if (!WriteFile(RemoteConsole, &message, sizeof(message), &bytes, NULL) || !CallRemoteConsole(&message))
        message.Status = ERROR_BROKEN_PIPE;
    Log("Session back on the console display%s: %lu", takenOver ? " after a console sign-in" : "", message.Status);
    CloseHandle(RemoteConsole);
    RemoteConsole = NULL;
}

static BOOL ConsoleSignedIn(void)
{
    REMOTE_CONSOLE_MESSAGE message;
    DWORD bytes, available = 0;
    if (InterlockedCompareExchange(&ConsoleTakeover, 0, 0)) return TRUE;
    EnterCriticalSection(&RemoteDisplayLock);
    if (RemoteConsole && PeekNamedPipe(RemoteConsole, NULL, 0, NULL, &available, NULL) && available &&
        ReadFile(RemoteConsole, &message, sizeof(message), &bytes, NULL) && bytes == sizeof(message) &&
        message.Code == REMOTE_CONSOLE_TAKEOVER)
    {
        InterlockedExchange(&ConsoleTakeover, TRUE);
    }
    LeaveCriticalSection(&RemoteDisplayLock);
    return InterlockedCompareExchange(&ConsoleTakeover, 0, 0) != 0;
}

static UINT32 AcquireRemoteDisplay(DESKTOP_CONTEXT *ctx)
{
    static const WCHAR hardwareIds[] = L"RdpVdd_RemoteDisplay\0";
    SW_DEVICE_CREATE_INFO info = {0};
    WCHAR instance[64];
    DWORD session = 0;
    int width = GetSystemMetrics(SM_CXSCREEN), height = GetSystemMetrics(SM_CYSCREEN);
    UINT32 error = ERRINFO_SUCCESS;
    HRESULT hr;

    EnterCriticalSection(&RemoteDisplayLock);
    if (!RemoteDisplayUsers)
    {
        ProcessIdToSessionId(GetCurrentProcessId(), &session);
        _snwprintf(instance, ARRAYSIZE(instance), L"RdpVdd_RemoteDisplay&SessionId_%04lu", session);
        instance[ARRAYSIZE(instance) - 1] = 0;
        info.cbSize = sizeof(info);
        info.pszInstanceId = instance;
        info.pszzHardwareIds = hardwareIds;
        info.pszzCompatibleIds = hardwareIds;
        info.CapabilityFlags = SWDeviceCapabilitiesRemovable | SWDeviceCapabilitiesSilentInstall |
                               SWDeviceCapabilitiesDriverRequired;
        info.pszDeviceDescription = L"LiberNT Remote Display Adapter";
        ResetEvent(RemoteDisplayReady);
        InterlockedExchange(&ConsoleTakeover, FALSE);
        hr = SwDeviceCreate(L"RemoteDisplayEnum", L"HTREE\\ROOT\\0", &info, 0, NULL,
                            RemoteDisplayCreated, NULL, &RemoteDisplay);
        if (FAILED(hr))
        {
            Log("Cannot create the remote display adapter: 0x%08lx", hr);
            RemoteDisplay = NULL;
            error = ERRINFO_CLOSE_STACK_ON_DRIVER_FAILURE;
        }
        else if (WaitForSingleObject(RemoteDisplayReady, 15000) == WAIT_OBJECT_0 && WaitForRemoteDisplay(0, 0))
        {
            if (SetRemoteDisplayMode(width, height) && !WaitForRemoteDisplay(width, height))
                Log("The remote display does not list %dx%d", width, height);
            RemoteDisplayShown = ConnectRemoteConsole(ctx);
            if (!RemoteDisplayShown)
                error = ERRINFO_SERVER_DENIED_CONNECTION;
            else if (SetRemoteDisplayMode(ctx->ClientWidth, ctx->ClientHeight) &&
                     !WaitForRemoteDisplayMode(ctx->ClientWidth, ctx->ClientHeight))
                Log("The desktop did not take the %dx%d mode", ctx->ClientWidth, ctx->ClientHeight);
        }
        else
        {
            Log("The remote display adapter did not start");
            error = ERRINFO_CLOSE_STACK_ON_DRIVER_NOT_READY;
        }
        if (error && RemoteDisplay)
        {
            SwDeviceClose(RemoteDisplay);
            RemoteDisplay = NULL;
            Log("Remote display adapter closed");
        }
    }
    else if (RemoteDisplayShown)
    {
        SetRemoteDisplayMode(ctx->ClientWidth, ctx->ClientHeight);
    }
    if (!error)
    {
        RemoteDisplayUsers++;
        ctx->RemoteDisplay = TRUE;
    }
    LeaveCriticalSection(&RemoteDisplayLock);
    return error;
}

static BOOL OpenRemoteFrames(DESKTOP_CONTEXT *ctx)
{
    RDPVDD_FRAME_ESCAPE request = {0};
    NTSTATUS status = STATUS_UNSUCCESSFUL;
    EnterCriticalSection(&RemoteDisplayLock);
    if (RemoteDisplayShown && OpenRemoteAdapter(&ctx->FrameAdapter))
    {
        ctx->FrameEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
        request.Code = RDPVDD_ESCAPE_READ_FRAME;
        status = ctx->FrameEvent ? FrameEscape(ctx->FrameAdapter, &request) : STATUS_NO_MEMORY;
        if (!NT_SUCCESS(status))
        {
            CloseRemoteFrames(ctx);
        }
        else if (!request.Width || !request.Height)
        {
            Log("The remote display has no frame yet");
            ctx->FrameWidth = ctx->ClientWidth;
            ctx->FrameHeight = ctx->ClientHeight;
        }
        else
        {
            ctx->FrameWidth = (int)request.Width;
            ctx->FrameHeight = (int)request.Height;
        }
    }
    LeaveCriticalSection(&RemoteDisplayLock);
    if (!ctx->FrameAdapter)
    {
        Log("Cannot read the remote display: 0x%08lx", status);
        return FALSE;
    }
    Log("Sending the remote display");
    return TRUE;
}

static void ReleaseRemoteDisplay(DESKTOP_CONTEXT *ctx)
{
    if (!ctx->RemoteDisplay) return;
    ctx->RemoteDisplay = FALSE;
    EnterCriticalSection(&RemoteDisplayLock);
    if (!--RemoteDisplayUsers)
    {
        RemoteDisplayShown = FALSE;
        DisconnectRemoteConsole(InterlockedExchange(&ConsoleTakeover, FALSE) != 0);
        SwDeviceClose(RemoteDisplay);
        RemoteDisplay = NULL;
        Log("Remote display adapter closed");
    }
    LeaveCriticalSection(&RemoteDisplayLock);
}

static BOOL PostConnect(freerdp_peer *peer)
{
    DESKTOP_CONTEXT *ctx = (DESKTOP_CONTEXT *)peer->context;
    UINT32 error;
    if (!ctx->NlaLogon)
    {
        Log("Authentication rejected for peer %s", peer->hostname);
        return FALSE;
    }
    ctx->Generation = InterlockedIncrement(&ClientGeneration);
    ctx->Authorized = TRUE;
    error = AcquireRemoteDisplay(ctx);
    if (!error && !OpenRemoteFrames(ctx)) error = ERRINFO_CLOSE_STACK_ON_DRIVER_FAILURE;
    if (error)
    {
        Log("Peer %s refused: the session has no remote display", peer->hostname);
        freerdp_set_error_info(ctx->rdp.rdp, error);
        if (!freerdp_send_error_info(ctx->rdp.rdp)) Log("Cannot tell peer %s why it is disconnected", peer->hostname);
        return FALSE;
    }
    if (!FollowInputDesktop(ctx)) return FALSE;
    if (*TransferRoot && WTSVirtualChannelManagerIsChannelJoined(ctx->Vcm, "rdpdr"))
    {
        ctx->Transfer = TransferStart(ctx->Vcm, &ctx->rdp, TransferRoot, Log);
        if (!ctx->Transfer) { Log("File transfer channel failed to start"); return FALSE; }
    }
    Log("Authenticated peer %s; desktop %dx%d", peer->hostname, ctx->Width, ctx->Height);
    return TRUE;
}

static BOOL Activate(freerdp_peer *peer)
{
    DESKTOP_CONTEXT *ctx = (DESKTOP_CONTEXT *)peer->context;
    POINTER_SYSTEM_UPDATE pointer = {0};
    if (!ctx->Authorized) return FALSE;
    if (!ctx->Planar)
    {
        ctx->Planar = freerdp_bitmap_planar_context_new(PLANAR_FORMAT_HEADER_RLE, 64, 32);
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

static BOOL SendTiles(DESKTOP_CONTEXT *ctx, UINT32 *count, size_t *length)
{
    BITMAP_UPDATE update = {0};
    if (!*count) return TRUE;
    update.number = *count; update.rectangles = ctx->Batch; update.skipCompression = TRUE;
    if (!ctx->rdp.update->BitmapUpdate(&ctx->rdp, &update)) return FALSE;
    ctx->Updates += *count;
    *count = 0; *length = 0;
    return TRUE;
}

static UINT MonitorLayout(DispServerContext *disp, const DISPLAY_CONTROL_MONITOR_LAYOUT_PDU *pdu)
{
    DESKTOP_CONTEXT *ctx = disp->custom;
    const DISPLAY_CONTROL_MONITOR_LAYOUT *monitor;
    UINT32 i;
    if (!pdu->NumMonitors) return CHANNEL_RC_OK;
    monitor = &pdu->Monitors[0];
    for (i = 0; i < pdu->NumMonitors; ++i)
        if (pdu->Monitors[i].Flags & DISPLAY_CONTROL_MONITOR_PRIMARY) monitor = &pdu->Monitors[i];
    Log("Client requested a %lux%lu desktop", (ULONG)monitor->Width, (ULONG)monitor->Height);
    InterlockedExchange(&ctx->RequestedSize, (LONG)MAKELONG(monitor->Height, monitor->Width));
    return CHANNEL_RC_OK;
}

static BOOL DispChannelIdAssigned(DispServerContext *disp, UINT32 channelId)
{
    ((DESKTOP_CONTEXT *)disp->custom)->DispChannelId = channelId;
    return TRUE;
}

static BOOL DvcCreated(void *userdata, UINT32 channelId, INT32 status)
{
    DESKTOP_CONTEXT *ctx = userdata;
    if (!ctx->Disp || channelId != ctx->DispChannelId) return TRUE;
    if (status < 0) { Log("Client declined the display control channel status=%ld", (long)status); return TRUE; }
    return ctx->Disp->DisplayControlCaps(ctx->Disp) == CHANNEL_RC_OK;
}

static void OpenDisplayControl(DESKTOP_CONTEXT *ctx)
{
    ctx->Disp = disp_server_context_new(ctx->Vcm);
    if (!ctx->Disp) return;
    ctx->Disp->custom = ctx;
    ctx->Disp->rdpcontext = &ctx->rdp;
    ctx->Disp->MaxNumMonitors = 1;
    ctx->Disp->MaxMonitorAreaFactorA = DISPLAY_CONTROL_MAX_MONITOR_WIDTH;
    ctx->Disp->MaxMonitorAreaFactorB = DISPLAY_CONTROL_MAX_MONITOR_HEIGHT;
    ctx->Disp->DispMonitorLayout = MonitorLayout;
    ctx->Disp->ChannelIdAssigned = DispChannelIdAssigned;
    WTSVirtualChannelManagerSetDVCCreationCallback(ctx->Vcm, DvcCreated, ctx);
    if (ctx->Disp->Open(ctx->Disp) != CHANNEL_RC_OK) Log("Cannot open the display control channel");
}

static void ApplyRequestedMode(DESKTOP_CONTEXT *ctx)
{
    LONG size = InterlockedExchange(&ctx->RequestedSize, 0);
    if (!size || !ctx->FrameAdapter) return;
    EnterCriticalSection(&RemoteDisplayLock);
    SetRemoteDisplayMode(HIWORD(size), LOWORD(size));
    LeaveCriticalSection(&RemoteDisplayLock);
}

static void MarkTiles(DESKTOP_CONTEXT *ctx, const RECT *rect)
{
    int x, y, left = max(rect->left, 0), top = max(rect->top, 0);
    int right = min(rect->right, ctx->Width), bottom = min(rect->bottom, ctx->Height);
    if (left >= right || top >= bottom) return;
    for (y = top / TILE_HEIGHT; y <= (bottom - 1) / TILE_HEIGHT; ++y)
        for (x = left / TILE_WIDTH; x <= (right - 1) / TILE_WIDTH; ++x)
            ctx->TileMask[y * ctx->TileColumns + x] = 1;
}

static void CopyFrameRect(DESKTOP_CONTEXT *ctx, const RECT *rect)
{
    int y, left = max(rect->left, 0), top = max(rect->top, 0);
    int right = min(rect->right, ctx->Width), bottom = min(rect->bottom, ctx->Height);
    if (left >= right || top >= bottom) return;
    for (y = top; y < bottom; ++y)
    {
        size_t offset = ((size_t)y * ctx->Width + left) * 4;
        memcpy(ctx->Pixels + offset, ctx->Frame + offset, (size_t)(right - left) * 4);
    }
    MarkTiles(ctx, rect);
}

static POINT CursorPosition(DESKTOP_CONTEXT *ctx, POINT desktop)
{
    (void)ctx;
    return desktop;
}

static void DrawCursor(DESKTOP_CONTEXT *ctx, const CURSORINFO *cursor)
{
    ICONINFO icon = {0};
    BITMAP bitmap = {0};
    POINT position;
    int width, height;
    SetRectEmpty(&ctx->CursorRect);
    if (!(cursor->flags & CURSOR_SHOWING) || !GetIconInfo(cursor->hCursor, &icon)) return;
    position = CursorPosition(ctx, cursor->ptScreenPos);
    GetObjectW(icon.hbmColor ? icon.hbmColor : icon.hbmMask, sizeof(bitmap), &bitmap);
    width = bitmap.bmWidth;
    height = icon.hbmColor ? bitmap.bmHeight : bitmap.bmHeight / 2;
    position.x -= icon.xHotspot;
    position.y -= icon.yHotspot;
    DrawIconEx(ctx->MemoryDC, position.x, position.y, cursor->hCursor, 0, 0, 0, NULL, DI_NORMAL);
    SetRect(&ctx->CursorRect, position.x, position.y, position.x + width, position.y + height);
    if (icon.hbmColor) DeleteObject(icon.hbmColor);
    if (icon.hbmMask) DeleteObject(icon.hbmMask);
}

static BOOL ReadRemoteFrame(DESKTOP_CONTEXT *ctx, RDPVDD_FRAME_ESCAPE *frame)
{
    NTSTATUS status;
    memset(frame, 0, sizeof(*frame));
    frame->Code = RDPVDD_ESCAPE_READ_FRAME;
    frame->Buffer = (ULONG64)(ULONG_PTR)ctx->Frame;
    frame->BufferWidth = ctx->Frame ? ctx->Width : 0;
    frame->BufferHeight = ctx->Frame ? ctx->Height : 0;
    status = FrameEscape(ctx->FrameAdapter, frame);
    if (NT_SUCCESS(status)) return TRUE;
    Log("The remote display is gone: 0x%08lx", status);
    CloseRemoteFrames(ctx);
    return FALSE;
}

static BOOL Capture(DESKTOP_CONTEXT *ctx)
{
    int x, y, row, width, height;
    UINT32 i, count = 0;
    CURSORINFO cursor = {0};
    RDPVDD_FRAME_ESCAPE frame = {0};
    BITMAP_DATA *rectangle;
    RECT old;
    POINT position;
    size_t length = 0, limit = freerdp_settings_get_uint32(ctx->rdp.settings, FreeRDP_MultifragMaxRequestSize);
    if (!ctx->Authorized || !ctx->Active || ctx->Suppressed) return TRUE;
    if (!FollowInputDesktop(ctx) || !ctx->FrameAdapter) return FALSE;
    ApplyRequestedMode(ctx);
    if (WaitForSingleObject(ctx->FrameEvent, 0) == WAIT_OBJECT_0) ctx->FramePending = TRUE;
    frame.Width = ctx->Width;
    frame.Height = ctx->Height;
    if (ctx->FramePending)
    {
        ctx->FramePending = FALSE;
        if (!ReadRemoteFrame(ctx, &frame)) return FALSE;
    }
    if (!frame.Width)
    {
        ctx->FramePending = TRUE;
        return TRUE;
    }
    ctx->FrameWidth = width = (int)frame.Width;
    ctx->FrameHeight = height = (int)frame.Height;
    if (ctx->Width != width || ctx->Height != height)
    {
        if (!AllocateCapture(ctx) ||
            !freerdp_settings_set_uint32(ctx->rdp.settings, FreeRDP_DesktopWidth, ctx->Width) ||
            !freerdp_settings_set_uint32(ctx->rdp.settings, FreeRDP_DesktopHeight, ctx->Height)) return FALSE;
        Log("Desktop resized to %dx%d", ctx->Width, ctx->Height);
        ctx->Active = FALSE;
        return ctx->rdp.update->DesktopResize(&ctx->rdp);
    }
    cursor.cbSize = sizeof(cursor);
    if (!GetCursorInfo(&cursor)) cursor.flags = 0;
    old = ctx->CursorRect;
    position = CursorPosition(ctx, cursor.ptScreenPos);
    if (!frame.RectCount && !ctx->Refresh && !IsRectEmpty(&old) == !!(cursor.flags & CURSOR_SHOWING) &&
        cursor.hCursor == ctx->CursorShape && position.x == ctx->CursorPoint.x && position.y == ctx->CursorPoint.y)
        return TRUE;
    ctx->CursorShape = cursor.hCursor;
    ctx->CursorPoint = position;
    for (i = 0; i < frame.RectCount; ++i) CopyFrameRect(ctx, &frame.Rects[i]);
    CopyFrameRect(ctx, &old);
    DrawCursor(ctx, &cursor);
    MarkTiles(ctx, &ctx->CursorRect);
    GdiFlush();
    for (y = 0; y < ctx->Height; y += TILE_HEIGHT)
        for (x = 0; x < ctx->Width; x += TILE_WIDTH)
        {
            int tileWidth = min(TILE_WIDTH, ctx->Width - x), tileHeight = min(TILE_HEIGHT, ctx->Height - y);
            BYTE *mask = &ctx->TileMask[(y / TILE_HEIGHT) * ctx->TileColumns + x / TILE_WIDTH];
            BOOL changed = ctx->Refresh;
            if (!changed && !*mask) continue;
            *mask = 0;
            for (row = 0; !changed && row < tileHeight; ++row)
            {
                size_t offset = ((size_t)(y + row) * ctx->Width + x) * 4;
                changed = memcmp(ctx->Pixels + offset, ctx->Previous + offset, tileWidth * 4) != 0;
            }
            if (!changed) continue;
            if ((count == TILE_BATCH || UPDATE_HEADER_BYTES + length + TILE_HEADER_BYTES + TILE_BYTES > limit) &&
                !SendTiles(ctx, &count, &length)) return FALSE;
            rectangle = &ctx->Batch[count];
            memset(rectangle, 0, sizeof(*rectangle));
            rectangle->bitsPerPixel = 32; rectangle->compressed = TRUE;
            rectangle->bitmapDataStream = freerdp_bitmap_compress_planar(ctx->Planar,
                ctx->Pixels + ((size_t)y * ctx->Width + x) * 4, PIXEL_FORMAT_BGRX32, tileWidth, tileHeight,
                ctx->Width * 4, ctx->Tiles[count], &rectangle->bitmapLength);
            if (!rectangle->bitmapDataStream) return FALSE;
            rectangle->destLeft = x; rectangle->destTop = y;
            rectangle->destRight = x + tileWidth - 1; rectangle->destBottom = y + tileHeight - 1;
            rectangle->width = tileWidth; rectangle->height = tileHeight;
            rectangle->cbScanWidth = tileWidth * 4;
            rectangle->cbUncompressedSize = tileWidth * tileHeight * 4;
            rectangle->cbCompMainBodySize = rectangle->bitmapLength;
            length += TILE_HEADER_BYTES + rectangle->bitmapLength;
            ++count;
            for (row = 0; row < tileHeight; ++row)
            {
                size_t offset = ((size_t)(y + row) * ctx->Width + x) * 4;
                memcpy(ctx->Previous + offset, ctx->Pixels + offset, tileWidth * 4);
            }
        }
    if (!SendTiles(ctx, &count, &length)) return FALSE;
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
    disp_server_context_free(ctx->Disp);
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
    CloseRemoteFrames(ctx);
    FreeDesktopCapture(ctx);
    if (ctx->Desktop && SetThreadDesktop(ctx->HomeDesktop)) CloseDesktop(ctx->Desktop);
    ReleaseRemoteDisplay(ctx);
}

static DWORD WINAPI ClientThread(void *arg)
{
    freerdp_peer *peer = arg;
    rdpSettings *settings;
    rdpPrivateKey *key;
    rdpCertificate *cert;
    DWORD count, status, started = GetTickCount(), wait, frameIndex;
    ULONGLONG lastFrame = 0, elapsed;
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
    timeBeginPeriod(1);
    for (;;)
    {
        events[0] = StopEvent;
        count = peer->GetEventHandles(peer, events + 1, 30);
        if (!count) break;
        events[++count] = WTSVirtualChannelManagerGetEventHandle(ctx->Vcm);
        frameIndex = 0;
        if (ctx->FrameEvent) events[frameIndex = ++count] = ctx->FrameEvent;
        wait = 100;
        if (ctx->Active && peer->activated)
        {
            elapsed = Milliseconds() - lastFrame;
            wait = elapsed >= FrameInterval ? 0 : (DWORD)(FrameInterval - elapsed);
        }
        status = WaitForMultipleObjects(count + 1, events, FALSE, wait);
        if (status == WAIT_OBJECT_0 || status == WAIT_FAILED) break;
        if (frameIndex && status == WAIT_OBJECT_0 + frameIndex) ctx->FramePending = TRUE;
        if (!peer->CheckFileDescriptor(peer)) break;
        if (!WTSVirtualChannelManagerCheckFileDescriptorEx(ctx->Vcm, peer->activated)) break;
        if (ctx->Authorized && !ctx->Disp &&
            WTSVirtualChannelManagerGetDrdynvcState(ctx->Vcm) == DRDYNVC_STATE_READY) OpenDisplayControl(ctx);
        if (!ctx->Authorized && GetTickCount() - started > 30000) break;
        if (ctx->RemoteDisplay && ConsoleSignedIn())
        {
            Log("Peer %s disconnected by a sign-in at the console", peer->hostname);
            freerdp_set_error_info(ctx->rdp.rdp, ERRINFO_DISCONNECTED_BY_OTHER_CONNECTION);
            if (!freerdp_send_error_info(ctx->rdp.rdp)) Log("Cannot tell peer %s why it is disconnected", peer->hostname);
            break;
        }
        if (Superseded(ctx))
        {
            Log("Peer %s replaced by a newer connection", peer->hostname);
            freerdp_set_error_info(ctx->rdp.rdp, ERRINFO_DISCONNECTED_BY_OTHER_CONNECTION);
            if (!freerdp_send_error_info(ctx->rdp.rdp)) Log("Cannot tell peer %s why it is disconnected", peer->hostname);
            break;
        }
        if (ctx->Active && peer->activated && Milliseconds() - lastFrame >= FrameInterval)
        {
            lastFrame = Milliseconds();
            if (!Capture(ctx)) break;
        }
    }
    timeEndPeriod(1);
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

/* DWMFRAMEINTERVAL counts 4 frames per second each and defaults to 30 frames per second. */
static DWORD ReadFrameInterval(void)
{
    DWORD value = 0, size = sizeof(value), type;
    HKEY key;
    if (!RegOpenKeyExA(HKEY_LOCAL_MACHINE, WINSTATIONS_KEY, 0, KEY_QUERY_VALUE, &key))
    {
        if (RegQueryValueExA(key, "DWMFRAMEINTERVAL", NULL, &type, (BYTE *)&value, &size) || type != REG_DWORD)
            value = 0;
        RegCloseKey(key);
    }
    if (!value) return 1000 / 30;
    return 1000 / (min(value, 15) * 4);
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
        if (server && (!allowed || (console != session && console != 0xFFFFFFFF))) StopSessionServer(&server);
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
    InitializeCriticalSection(&RemoteDisplayLock);
    RemoteDisplayReady = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!RemoteDisplayReady) return 1;
    StopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!StopEvent) return 1;
    FrameInterval = ReadFrameInterval();
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
