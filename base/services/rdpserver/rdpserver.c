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
#include <iphlpapi.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <assert.h>
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
#include <cfgmgr32.h>
#include "transfer.h"
#include "gfx.h"
#include "broker.h"
#include "agent.h"
#include "desktop.h"
#include "openh264.h"
#include "secret.h"

typedef ULONG (__cdecl *DEBUG_PRINT)(const char *, ...);
static DEBUG_PRINT DebugPrint;
static HANDLE StopEvent;
static volatile LONG ClientCount, RemoteAllowed = TRUE;
static char Certificate[MAX_PATH], PrivateKey[MAX_PATH];
static WCHAR TransferRoot[MAX_PATH];
static SERVICE_STATUS_HANDLE ServiceHandle;
static SERVICE_STATUS ServiceState;
static HANDLE ServiceStopEvent, SessionStopEvent;
static char ServiceConfig[MAX_PATH];
static DWORD FrameInterval;
static BOOL ControllerMode;
static volatile LONG ControllerConnected;
static char BindAddress[64], AllowedPeer[64], Username[128], Password[256];
#define MAX_INTERFACE_MACS 8
static BYTE InterfaceMac[MAX_INTERFACE_MACS][6];
static unsigned InterfaceMacCount;
static BOOL ConfigureAddress;
static IPMask InterfaceMask;
static ULONG AddressContext;
static BOOL AddressAdded;

#define SERVICE_NAME "LiberNTRDPServer"
#define SERVICE_PARAMETERS_KEY "SYSTEM\\CurrentControlSet\\Services\\" SERVICE_NAME "\\Parameters"
#define SERVICE_STOP_EVENT "Global\\LiberNT-RDPServer-Stop"
#define TERMINAL_SERVER_KEY "SYSTEM\\CurrentControlSet\\Control\\Terminal Server"
#define WINSTATIONS_KEY TERMINAL_SERVER_KEY "\\WinStations"
#define CERTIFICATE_DAYS 183
#define TILE_BATCH 64
#define TILE_WIDTH 64
#define TILE_HEIGHT 64
#define TILE_BYTES (TILE_WIDTH * TILE_HEIGHT * 4 + 2)
/* TS_UPDATE_BITMAP_DATA header, and TS_BITMAP_DATA fields with its TS_CD_HEADER */
#define UPDATE_HEADER_BYTES 4
#define TILE_HEADER_BYTES 26
#define AGENT_STATUS_INTERVAL 500

typedef struct DESKTOP_CONTEXT
{
    rdpContext rdp;
    BOOL Authorized, Active, Suppressed, Refresh, NlaLogon;
    char NlaUser[128], NlaDomain[256], NlaPassword[256];
    BYTE NlaSid[SECURITY_MAX_SID_SIZE];
    int Width, Height, ClientWidth, ClientHeight, TileColumns, TileRows;
    HDC ScreenDC, MemoryDC;
    HBITMAP Bitmap;
    HGDIOBJ OldBitmap;
    DESKTOP_THREAD Thread;
    DESKTOP_INPUT Input;
    BYTE *Pixels, *Previous, *TileMask;
    GFX_TILE *GfxTiles;
    AGENT *Agent;
    LONG Owner;
    BOOL TakenOver;
    ULONG Serial;
    AGENT_FRAME_REPLY Frame;
    AGENT_POINTER_REPLY *PointerReply;
    BOOL PointerSent;
    BITMAP_DATA Batch[TILE_BATCH];
    BYTE Tiles[TILE_BATCH][TILE_BYTES];
    BITMAP_PLANAR_CONTEXT *Planar;
    ULONGLONG Frames, Updates, Inputs;
    HANDLE Vcm;
    RDP_TRANSFER *Transfer;
    DispServerContext *Disp;
    RDP_GFX *Gfx;
    BOOL GfxTried, Deferred;
    RECT RefreshRect;
    FRAME_MOVE Moves[RDPVDD_FRAME_MAX_MOVES];
    UINT32 MoveCount;
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
    return ctx->Owner && !AgentOwnedBy(ctx->Agent, ctx->Owner);
}

/* A client may send input before the connection is active; that input is dropped, not the connection */
static BOOL InputReady(rdpInput *input)
{
    DESKTOP_CONTEXT *ctx = (DESKTOP_CONTEXT *)input->context;
    if (!ctx->Authorized || !ctx->Active || Superseded(ctx)) return FALSE;
    return !ControllerMode || FollowInputDesktop(ctx);
}

static BOOL SendAgentInput(DESKTOP_CONTEXT *ctx, ULONG kind, UINT16 flags, UINT16 value, UINT16 x, UINT16 y)
{
    AGENT_INPUT_REQUEST request = {AGENT_INPUT};
    BOOL sent;
    request.Kind = kind;
    request.Flags = flags;
    request.Value = value;
    request.X = x;
    request.Y = y;
    if (!AgentEnter(ctx->Agent, ctx->Owner)) return !AgentBroken(ctx->Agent);
    sent = AgentWrite(ctx->Agent, &request, sizeof(request));
    AgentLeave(ctx->Agent);
    if (sent) ++ctx->Inputs;
    return sent;
}

static BOOL Keyboard(rdpInput *input, UINT16 flags, UINT8 code)
{
    DESKTOP_CONTEXT *ctx = (DESKTOP_CONTEXT *)input->context;
    if (!InputReady(input)) return TRUE;
    if (!ControllerMode) return SendAgentInput(ctx, AGENT_KEYBOARD, flags, code, 0, 0);
    return DesktopKeyboard(&ctx->Input, flags, code);
}

static BOOL UnicodeKeyboard(rdpInput *input, UINT16 flags, UINT16 code)
{
    DESKTOP_CONTEXT *ctx = (DESKTOP_CONTEXT *)input->context;
    if (!InputReady(input)) return TRUE;
    if (!ControllerMode) return SendAgentInput(ctx, AGENT_UNICODE, flags, code, 0, 0);
    return DesktopUnicode(flags, code);
}

static BOOL Mouse(rdpInput *input, UINT16 flags, UINT16 x, UINT16 y)
{
    DESKTOP_CONTEXT *ctx = (DESKTOP_CONTEXT *)input->context;
    if (!InputReady(input)) return TRUE;
    if (!ControllerMode) return SendAgentInput(ctx, AGENT_MOUSE, flags, 0, x, y);
    return DesktopMouse(&ctx->Input, ctx->Width, ctx->Height, flags, x, y);
}

static BOOL Synchronize(rdpInput *input, UINT32 flags)
{
    (void)flags;
    (void)InputReady(input);
    return TRUE;
}

static void FreeDesktopCapture(DESKTOP_CONTEXT *ctx)
{
    if (ctx->OldBitmap) SelectObject(ctx->MemoryDC, ctx->OldBitmap);
    if (ctx->Bitmap) DeleteObject(ctx->Bitmap);
    if (ctx->MemoryDC) DeleteDC(ctx->MemoryDC);
    if (ctx->ScreenDC) ReleaseDC(NULL, ctx->ScreenDC);
    if (ctx->Agent && ctx->Pixels) UnmapViewOfFile(ctx->Pixels);
    free(ctx->Previous);
    free(ctx->TileMask);
    free(ctx->GfxTiles);
    ctx->GfxTiles = NULL;
    ctx->OldBitmap = NULL; ctx->Bitmap = NULL;
    ctx->MemoryDC = ctx->ScreenDC = NULL;
    ctx->Previous = ctx->Pixels = ctx->TileMask = NULL;
    ctx->MoveCount = 0;
}

static BOOL AllocateTiles(DESKTOP_CONTEXT *ctx)
{
    if (ctx->Width < 2 || ctx->Height < 2 || ctx->Width > 8192 || ctx->Height > 8192) return FALSE;
    ctx->TileColumns = (ctx->Width + TILE_WIDTH - 1) / TILE_WIDTH;
    ctx->TileRows = (ctx->Height + TILE_HEIGHT - 1) / TILE_HEIGHT;
    ctx->TileMask = calloc((size_t)ctx->TileColumns * ctx->TileRows, 1);
    ctx->GfxTiles = calloc((size_t)ctx->TileColumns * ctx->TileRows, sizeof(*ctx->GfxTiles));
    ctx->Previous = calloc((size_t)ctx->Width * ctx->Height, 4);
    ctx->Refresh = TRUE;
    return ctx->TileMask && ctx->GfxTiles && ctx->Previous;
}

static BOOL AllocateCapture(DESKTOP_CONTEXT *ctx)
{
    BITMAPINFO info = {0};
    assert(ControllerMode && !ctx->Agent);
    FreeDesktopCapture(ctx);
    ctx->Width = GetSystemMetrics(SM_CXSCREEN);
    ctx->Height = GetSystemMetrics(SM_CYSCREEN);
    if (!AllocateTiles(ctx)) return FALSE;
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
    return TRUE;
}

/* The agent made a new surface for its desktop; its pixels are read where the agent wrote them */
static BOOL MapSurface(DESKTOP_CONTEXT *ctx, const AGENT_FRAME_REPLY *frame)
{
    HANDLE section;
    if (!frame->Width || frame->Serial == ctx->Serial) return TRUE;
    FreeDesktopCapture(ctx);
    ctx->Serial = frame->Serial;
    ctx->Width = frame->Width;
    ctx->Height = frame->Height;
    section = AgentDuplicate(ctx->Agent, frame->Section, FILE_MAP_READ);
    if (!section) return FALSE;
    ctx->Pixels = MapViewOfFile(section, FILE_MAP_READ, 0, 0, (SIZE_T)ctx->Width * ctx->Height * 4);
    CloseHandle(section);
    return ctx->Pixels && AllocateTiles(ctx);
}

static BOOL FollowInputDesktop(DESKTOP_CONTEXT *ctx)
{
    int switched = DesktopFollowInput(&ctx->Thread, Log);
    if (switched < 0) return FALSE;
    if (switched) FreeDesktopCapture(ctx);
    return ctx->ScreenDC || AllocateCapture(ctx);
}

static BOOL Capabilities(freerdp_peer *peer)
{
    DESKTOP_CONTEXT *ctx = (DESKTOP_CONTEXT *)peer->context;
    if (ctx->Authorized) return TRUE;
    ctx->ClientWidth = (int)freerdp_settings_get_uint32(peer->context->settings, FreeRDP_DesktopWidth);
    ctx->ClientHeight = (int)freerdp_settings_get_uint32(peer->context->settings, FreeRDP_DesktopHeight);
    if (ctx->ClientWidth >= RDPVDD_MIN_WIDTH && ctx->ClientWidth <= RDPVDD_MAX_WIDTH &&
        ctx->ClientHeight >= RDPVDD_MIN_HEIGHT && ctx->ClientHeight <= RDPVDD_MAX_HEIGHT) return TRUE;
    if (!ControllerMode)
    {
        ctx->ClientWidth = min(max(ctx->ClientWidth, RDPVDD_MIN_WIDTH), RDPVDD_MAX_WIDTH);
        ctx->ClientHeight = min(max(ctx->ClientHeight, RDPVDD_MIN_HEIGHT), RDPVDD_MAX_HEIGHT);
        return freerdp_settings_set_uint32(peer->context->settings, FreeRDP_DesktopWidth, ctx->ClientWidth) &&
               freerdp_settings_set_uint32(peer->context->settings, FreeRDP_DesktopHeight, ctx->ClientHeight);
    }
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

static BOOL TokenUserSid(HANDLE token, BYTE *sid, DWORD size)
{
    BYTE buffer[sizeof(TOKEN_USER) + SECURITY_MAX_SID_SIZE];
    DWORD length;
    if (!GetTokenInformation(token, TokenUser, buffer, sizeof(buffer), &length)) return FALSE;
    return CopySid(size, sid, ((TOKEN_USER *)buffer)->User.Sid);
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

static void ForgetPassword(DESKTOP_CONTEXT *ctx)
{
    SEC_WINNT_AUTH_IDENTITY *identity = &ctx->rdp.peer->identity;
    SecureZeroMemory(ctx->NlaPassword, sizeof(ctx->NlaPassword));
    if (identity->Password)
        SecureZeroMemory(identity->Password, identity->PasswordLength *
                         ((identity->Flags & SEC_WINNT_AUTH_IDENTITY_UNICODE) ? sizeof(WCHAR) : sizeof(char)));
    if (freerdp_settings_get_string(ctx->rdp.settings, FreeRDP_Password))
        freerdp_settings_set_string(ctx->rdp.settings, FreeRDP_Password, NULL);
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
    ctx->NlaLogon = LogonUserW(wuser, wdomain, wpassword, LOGON32_LOGON_NETWORK, LOGON32_PROVIDER_DEFAULT, &token);
    SecureZeroMemory(wpassword, sizeof(wpassword));
    if (ctx->NlaLogon)
    {
        ctx->NlaLogon = RemoteLogonAllowed(token) && TokenUserSid(token, ctx->NlaSid, sizeof(ctx->NlaSid));
        if (!ctx->NlaLogon) SetLastError(ERROR_LOGON_TYPE_NOT_GRANTED);
        CloseHandle(token);
    }
    if (ctx->NlaLogon)
    {
        lstrcpynA(ctx->NlaUser, user, sizeof(ctx->NlaUser));
        lstrcpynA(ctx->NlaDomain, domain ? domain : "", sizeof(ctx->NlaDomain));
        lstrcpynA(ctx->NlaPassword, password, sizeof(ctx->NlaPassword));
    }
    Log("NLA logon of %s for peer %s: %s (%lu)", user, peer->hostname, ctx->NlaLogon ? "accepted" : "rejected",
        ctx->NlaLogon ? 0UL : GetLastError());
    return ctx->NlaLogon;
}

/* The agent of the user's session shows its desktop; the agent signs the user in through Winlogon with the NLA credentials */
static UINT32 AttachAgent(DESKTOP_CONTEXT *ctx)
{
    rdpSettings *settings = ctx->rdp.settings;
    AGENT_ATTACH_REQUEST *request;
    AGENT_FRAME_REPLY *reply = &ctx->Frame;
    WCHAR user[ARRAYSIZE(request->UserName)];
    DWORD read;
    UINT32 error = ERRINFO_CLOSE_STACK_ON_DRIVER_FAILURE;
    if (!MultiByteToWideChar(CP_UTF8, 0, ctx->NlaUser, -1, user, ARRAYSIZE(user)))
        return ERRINFO_SERVER_DENIED_CONNECTION;
    ctx->Agent = BrokerConnect(ctx->NlaSid, user);
    if (!ctx->Agent) return ERRINFO_SERVER_DENIED_CONNECTION;
    request = calloc(1, sizeof(*request));
    if (!request) return ERRINFO_OUT_OF_MEMORY;
    request->Code = AGENT_ATTACH;
    request->ClientWidth = ctx->ClientWidth;
    request->ClientHeight = ctx->ClientHeight;
    request->PerformanceFlags = freerdp_settings_get_uint32(settings, FreeRDP_PerformanceFlags);
    request->ConnectionType = freerdp_settings_get_uint32(settings, FreeRDP_ConnectionType);
    CopySid(sizeof(request->Sid), request->Sid, ctx->NlaSid);
    lstrcpynW(request->UserName, user, ARRAYSIZE(request->UserName));
    MultiByteToWideChar(CP_UTF8, 0, ctx->NlaDomain, -1, request->Domain, ARRAYSIZE(request->Domain) - 1);
    MultiByteToWideChar(CP_UTF8, 0, ctx->NlaPassword, -1, request->Password, ARRAYSIZE(request->Password) - 1);
    if (AgentEnter(ctx->Agent, 0))
    {
        if (AgentWrite(ctx->Agent, request, sizeof(*request)) &&
            AgentRead(ctx->Agent, reply, sizeof(*reply), &read, AGENT_ATTACH_TIMEOUT))
        {
            if (read == sizeof(*reply) && reply->Code == AGENT_ATTACH) error = reply->Error;
            else AgentAbandon(ctx->Agent);
        }
        if (!error && !MapSurface(ctx, reply)) error = ERRINFO_CLOSE_STACK_ON_DRIVER_FAILURE;
        if (!error) ctx->Owner = AgentTakeOver(ctx->Agent);
        AgentLeave(ctx->Agent);
    }
    SecureZeroMemory(request, sizeof(*request));
    free(request);
    if (error == ERRINFO_SERVER_DENIED_CONNECTION && reply->ConsoleStatus == ERROR_ACCESS_DENIED)
        Log("Session %lu refused %s", AgentSession(ctx->Agent), ctx->NlaUser);
    return error;
}

static BOOL OpenH264Ready(void)
{
    static INIT_ONCE once = INIT_ONCE_STATIC_INIT;
    BOOL pending = FALSE, enabled, loaded;
    PVOID context = NULL;
    if (!InitOnceBeginInitialize(&once, 0, &pending, &context)) return FALSE;
    if (!pending) return context != NULL;
    enabled = OpenH264Enabled();
    loaded = enabled && OpenH264Load() != NULL;
    if (enabled && !loaded) Log("OpenH264 is turned on, but its library is not Cisco's binary");
    InitOnceComplete(&once, 0, loaded ? (PVOID)1 : NULL);
    return loaded;
}

static BOOL StartTransfer(DESKTOP_CONTEXT *ctx)
{
    if (!*TransferRoot || !WTSVirtualChannelManagerIsChannelJoined(ctx->Vcm, "rdpdr")) return TRUE;
    ctx->Transfer = TransferStart(ctx->Vcm, &ctx->rdp, TransferRoot, Log);
    if (!ctx->Transfer) Log("File transfer channel failed to start");
    return ctx->Transfer != NULL;
}

static BOOL ControllerPostConnect(freerdp_peer *peer)
{
    DESKTOP_CONTEXT *ctx = (DESKTOP_CONTEXT *)peer->context;
    rdpSettings *settings = ctx->rdp.settings;
    const char *user = freerdp_settings_get_string(settings, FreeRDP_Username);
    const char *password = freerdp_settings_get_string(settings, FreeRDP_Password);
    assert(ControllerMode && !ctx->Agent);
    if (!user || !password || !SecretEqual(user, Username) || !SecretEqual(password, Password))
    {
        Log("Authentication rejected for peer %s", peer->hostname);
        return FALSE;
    }
    ctx->Authorized = TRUE;
    if (!FollowInputDesktop(ctx) || !StartTransfer(ctx)) return FALSE;
    Log("Authenticated peer %s; desktop %dx%d", peer->hostname, ctx->Width, ctx->Height);
    return TRUE;
}

static BOOL PostConnect(freerdp_peer *peer)
{
    DESKTOP_CONTEXT *ctx = (DESKTOP_CONTEXT *)peer->context;
    UINT32 error;
    BOOL authorized;
    if (ControllerMode)
    {
        authorized = ControllerPostConnect(peer);
        ForgetPassword(ctx);
        return authorized;
    }
    if (!ctx->NlaLogon)
    {
        ForgetPassword(ctx);
        Log("Authentication rejected for peer %s", peer->hostname);
        return FALSE;
    }
    ctx->Authorized = TRUE;
    error = AttachAgent(ctx);
    ForgetPassword(ctx);
    if (error)
    {
        Log("Peer %s refused: the session has no remote display", peer->hostname);
        freerdp_set_error_info(ctx->rdp.rdp, error);
        if (!freerdp_send_error_info(ctx->rdp.rdp)) Log("Cannot tell peer %s why it is disconnected", peer->hostname);
        return FALSE;
    }
    if (!StartTransfer(ctx)) return FALSE;
    Log("Authenticated peer %s; desktop %dx%d", peer->hostname, ctx->Width, ctx->Height);
    return TRUE;
}

static BOOL Activate(freerdp_peer *peer)
{
    DESKTOP_CONTEXT *ctx = (DESKTOP_CONTEXT *)peer->context;
    if (!ctx->Authorized) return FALSE;
    if (!ctx->Planar)
    {
        ctx->Planar = freerdp_bitmap_planar_context_new(PLANAR_FORMAT_HEADER_RLE, TILE_WIDTH, TILE_HEIGHT);
        if (!ctx->Planar) return FALSE;
    }
    ctx->Active = TRUE;
    ctx->Refresh = TRUE;
    ctx->PointerSent = FALSE;
    if (ControllerMode)
    {
        POINTER_SYSTEM_UPDATE pointer = {0};
        pointer.type = SYSPTR_NULL;
        return ctx->rdp.update->pointer->PointerSystem(&ctx->rdp, &pointer);
    }
    return TRUE;
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
    if (ctx->Gfx && channelId == GfxChannelId(ctx->Gfx)) return GfxChannelCreated(ctx->Gfx, status);
    if (!ctx->Disp || channelId != ctx->DispChannelId) return TRUE;
    if (status < 0) { Log("Client declined the display control channel status=%ld", (long)status); return TRUE; }
    return ctx->Disp->DisplayControlCaps(ctx->Disp) == CHANNEL_RC_OK;
}

static void OpenGraphicsPipeline(DESKTOP_CONTEXT *ctx)
{
    ctx->GfxTried = TRUE;
    ctx->Gfx = GfxCreate(ctx->Vcm, &ctx->rdp, Log, OpenH264Ready());
    if (!ctx->Gfx) { Log("Cannot create the graphics pipeline"); return; }
    WTSVirtualChannelManagerSetDVCCreationCallback(ctx->Vcm, DvcCreated, ctx);
    if (!GfxOpen(ctx->Gfx)) { GfxFree(ctx->Gfx); ctx->Gfx = NULL; }
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

static void MarkTiles(DESKTOP_CONTEXT *ctx, const RECT *rect)
{
    int x, y, left = max(rect->left, 0), top = max(rect->top, 0);
    int right = min(rect->right, ctx->Width), bottom = min(rect->bottom, ctx->Height);
    if (left >= right || top >= bottom) return;
    for (y = top / TILE_HEIGHT; y <= (bottom - 1) / TILE_HEIGHT; ++y)
        for (x = left / TILE_WIDTH; x <= (right - 1) / TILE_WIDTH; ++x)
            ctx->TileMask[y * ctx->TileColumns + x] = 1;
}

static BOOL SendPointer(DESKTOP_CONTEXT *ctx, const POINTER_LARGE_UPDATE *pointer)
{
    POINTER_NEW_UPDATE update = {0};
    if (pointer->width > 96 || pointer->height > 96) return ctx->rdp.update->pointer->PointerLarge(&ctx->rdp, pointer);
    update.xorBpp = pointer->xorBpp;
    update.colorPtrAttr.cacheIndex = pointer->cacheIndex;
    update.colorPtrAttr.hotSpotX = pointer->hotSpotX;
    update.colorPtrAttr.hotSpotY = pointer->hotSpotY;
    update.colorPtrAttr.width = pointer->width;
    update.colorPtrAttr.height = pointer->height;
    update.colorPtrAttr.lengthAndMask = (UINT16)pointer->lengthAndMask;
    update.colorPtrAttr.lengthXorMask = (UINT16)pointer->lengthXorMask;
    update.colorPtrAttr.andMaskData = pointer->andMaskData;
    update.colorPtrAttr.xorMaskData = pointer->xorMaskData;
    return ctx->rdp.update->pointer->PointerNew(&ctx->rdp, &update);
}

static BOOL TileChanged(DESKTOP_CONTEXT *ctx, int x, int y, int width, int height, RECT *changed)
{
    int row, column;
    SetRect(changed, x + width, y + height, x, y);
    for (row = 0; row < height; ++row)
    {
        const UINT32 *current = (const UINT32 *)ctx->Pixels + (size_t)(y + row) * ctx->Width + x;
        const UINT32 *previous = (const UINT32 *)ctx->Previous + (size_t)(y + row) * ctx->Width + x;
        if (!memcmp(current, previous, (size_t)width * 4)) continue;
        for (column = 0; current[column] == previous[column]; ++column);
        changed->left = min(changed->left, x + column);
        for (column = width - 1; current[column] == previous[column]; --column);
        changed->right = max(changed->right, x + column + 1);
        changed->top = min(changed->top, y + row);
        changed->bottom = y + row + 1;
    }
    return changed->left < changed->right;
}

static BOOL ComposeMove(DESKTOP_CONTEXT *ctx, const FRAME_MOVE *move)
{
    UINT32 i;
    RECT source, overlap;
    SetRect(&source, move->SourceLeft, move->SourceTop, move->SourceLeft + move->Dest.right - move->Dest.left,
            move->SourceTop + move->Dest.bottom - move->Dest.top);
    for (i = ctx->MoveCount; i-- > 0;)
    {
        FRAME_MOVE *earlier = &ctx->Moves[i];
        if (!IntersectRect(&overlap, &source, &earlier->Dest)) continue;
        earlier->SourceLeft += overlap.left - earlier->Dest.left;
        earlier->SourceTop += overlap.top - earlier->Dest.top;
        earlier->Dest = overlap;
        OffsetRect(&earlier->Dest, move->Dest.left - move->SourceLeft, move->Dest.top - move->SourceTop);
        if (!FrameMoveClip(earlier, ctx->Width, ctx->Height))
            memmove(earlier, earlier + 1, (--ctx->MoveCount - i) * sizeof(*earlier));
        return TRUE;
    }
    return FALSE;
}

static void QueueMoves(DESKTOP_CONTEXT *ctx, const RDPVDD_MOVE *moves, UINT32 count)
{
    UINT32 i;
    for (i = 0; i < count && i < RDPVDD_FRAME_MAX_MOVES; ++i)
    {
        FRAME_MOVE move;
        move.SourceLeft = moves[i].SourcePoint.x;
        move.SourceTop = moves[i].SourcePoint.y;
        move.Dest = moves[i].DestRect;
        if (!FrameMoveClip(&move, ctx->Width, ctx->Height) || ComposeMove(ctx, &move)) continue;
        if (ctx->MoveCount == RDPVDD_FRAME_MAX_MOVES) memmove(ctx->Moves, ctx->Moves + 1, --ctx->MoveCount * sizeof(move));
        ctx->Moves[ctx->MoveCount++] = move;
    }
}

static UINT32 ApplyMoves(DESKTOP_CONTEXT *ctx)
{
    UINT32 i, applied = 0;
    for (i = 0; i < ctx->MoveCount; ++i)
    {
        const FRAME_MOVE *move = &ctx->Moves[i];
        if (!FrameMoveMatches(ctx->Previous, ctx->Pixels, (UINT32)ctx->Width * 4, move)) continue;
        FrameMoveApply(ctx->Previous, (UINT32)ctx->Width * 4, move);
        MarkTiles(ctx, &move->Dest);
        ctx->Moves[applied++] = *move;
    }
    ctx->MoveCount = 0;
    return applied;
}

static BOOL SendDesktop(DESKTOP_CONTEXT *ctx, BOOL masked)
{
    int x, y, row, width, height;
    UINT32 count = 0, moves = 0;
    RECT changed;
    BITMAP_DATA *rectangle;
    BOOL gfx = GfxActive(ctx->Gfx);
    size_t length = 0, limit = freerdp_settings_get_uint32(ctx->rdp.settings, FreeRDP_MultifragMaxRequestSize);
    if (gfx)
    {
        if (!GfxSurfaceReady(ctx->Gfx, (UINT32)ctx->Width, (UINT32)ctx->Height))
        {
            if (!GfxResetSurface(ctx->Gfx, (UINT32)ctx->Width, (UINT32)ctx->Height)) return FALSE;
            ctx->Refresh = TRUE;
        }
        ctx->Deferred = !GfxFrameAllowed(ctx->Gfx);
        if (ctx->Deferred) return TRUE;
        if (!ctx->Refresh) moves = ApplyMoves(ctx);
    }
    ctx->MoveCount = 0;
    for (y = 0; y < ctx->Height; y += TILE_HEIGHT)
        for (x = 0; x < ctx->Width; x += TILE_WIDTH)
        {
            BYTE *mask = &ctx->TileMask[(y / TILE_HEIGHT) * ctx->TileColumns + x / TILE_WIDTH];
            width = min(TILE_WIDTH, ctx->Width - x);
            height = min(TILE_HEIGHT, ctx->Height - y);
            RECT tileRect, overlap;
            BOOL force;
            SetRect(&tileRect, x, y, x + width, y + height);
            force = ctx->Refresh || IntersectRect(&overlap, &tileRect, &ctx->RefreshRect);
            if (masked && !force && !*mask) continue;
            *mask = 0;
            if (force) changed = tileRect;
            else if (!TileChanged(ctx, x, y, width, height, &changed)) continue;
            if (gfx)
            {
                GFX_TILE *tile = &ctx->GfxTiles[count++];
                tile->Left = (UINT16)changed.left; tile->Top = (UINT16)changed.top;
                tile->Width = (UINT16)(changed.right - changed.left);
                tile->Height = (UINT16)(changed.bottom - changed.top);
            }
            else
            {
                if ((count == TILE_BATCH || UPDATE_HEADER_BYTES + length + TILE_HEADER_BYTES + TILE_BYTES > limit) &&
                    !SendTiles(ctx, &count, &length)) return FALSE;
                rectangle = &ctx->Batch[count];
                memset(rectangle, 0, sizeof(*rectangle));
                rectangle->bitsPerPixel = 32; rectangle->compressed = TRUE;
                rectangle->bitmapDataStream = freerdp_bitmap_compress_planar(ctx->Planar,
                    ctx->Pixels + ((size_t)y * ctx->Width + x) * 4, PIXEL_FORMAT_BGRX32, width, height,
                    ctx->Width * 4, ctx->Tiles[count], &rectangle->bitmapLength);
                if (!rectangle->bitmapDataStream) return FALSE;
                rectangle->destLeft = x; rectangle->destTop = y;
                rectangle->destRight = x + width - 1; rectangle->destBottom = y + height - 1;
                rectangle->width = width; rectangle->height = height;
                rectangle->cbScanWidth = width * 4;
                rectangle->cbUncompressedSize = width * height * 4;
                rectangle->cbCompMainBodySize = rectangle->bitmapLength;
                length += TILE_HEADER_BYTES + rectangle->bitmapLength;
                ++count;
            }
            for (row = 0; row < height; ++row)
            {
                size_t offset = ((size_t)(y + row) * ctx->Width + x) * 4;
                memcpy(ctx->Previous + offset, ctx->Pixels + offset, (size_t)width * 4);
            }
        }
    SetRectEmpty(&ctx->RefreshRect);
    if (gfx)
    {
        if (!GfxSendFrame(ctx->Gfx, ctx->Pixels, (UINT32)ctx->Width * 4, ctx->Moves, moves, ctx->GfxTiles, count,
                          &ctx->RefreshRect))
            return FALSE;
        ctx->Updates += count + moves;
    }
    else if (!SendTiles(ctx, &count, &length)) return FALSE;
    ctx->Refresh = FALSE;
    if (++ctx->Frames == 1) Log("First desktop frame sent, bitmap updates=%llu", ctx->Updates);
    return TRUE;
}

static BOOL CaptureConsole(DESKTOP_CONTEXT *ctx)
{
    CURSORINFO cursor = {0};
    assert(ControllerMode && !ctx->Agent);
    if (!FollowInputDesktop(ctx)) return FALSE;
    if (ctx->Width != GetSystemMetrics(SM_CXSCREEN) || ctx->Height != GetSystemMetrics(SM_CYSCREEN))
    {
        if (!AllocateCapture(ctx) ||
            !freerdp_settings_set_uint32(ctx->rdp.settings, FreeRDP_DesktopWidth, ctx->Width) ||
            !freerdp_settings_set_uint32(ctx->rdp.settings, FreeRDP_DesktopHeight, ctx->Height)) return FALSE;
        Log("Desktop resized to %dx%d", ctx->Width, ctx->Height);
        if (GfxActive(ctx->Gfx)) return TRUE;
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
    return SendDesktop(ctx, FALSE);
}

static BOOL ShowAgentPointer(DESKTOP_CONTEXT *ctx, const AGENT_POINTER_REPLY *reply, DWORD size)
{
    POINTER_SYSTEM_UPDATE hidden = {0};
    POINTER_CACHED_UPDATE cached = {0};
    POINTER_LARGE_UPDATE update = {0};
    if (reply->Kind == AGENT_POINTER_HIDDEN)
    {
        hidden.type = SYSPTR_NULL;
        return ctx->rdp.update->pointer->PointerSystem(&ctx->rdp, &hidden);
    }
    if (reply->Kind == AGENT_POINTER_CACHED)
    {
        cached.cacheIndex = reply->CacheIndex;
        return ctx->rdp.update->pointer->PointerCached(&ctx->rdp, &cached);
    }
    if (reply->Kind != AGENT_POINTER_SHAPE || !reply->Width || !reply->Height ||
        reply->Width > RDPVDD_POINTER_MAX || reply->Height > RDPVDD_POINTER_MAX ||
        reply->AndLength != (UINT32)(reply->Width + 15) / 16 * 2 * reply->Height ||
        reply->XorLength != (UINT32)reply->Width * 4 * reply->Height ||
        size != FIELD_OFFSET(AGENT_POINTER_REPLY, Data) + reply->AndLength + reply->XorLength)
        return FALSE;
    update.xorBpp = 32;
    update.cacheIndex = reply->CacheIndex;
    update.hotSpotX = reply->HotSpotX;
    update.hotSpotY = reply->HotSpotY;
    update.width = reply->Width;
    update.height = reply->Height;
    update.lengthAndMask = reply->AndLength;
    update.lengthXorMask = reply->XorLength;
    update.andMaskData = (BYTE *)reply->Data;
    update.xorMaskData = (BYTE *)reply->Data + reply->AndLength;
    return SendPointer(ctx, &update);
}

static BOOL ShowAgentFrame(DESKTOP_CONTEXT *ctx, const AGENT_FRAME_REPLY *frame, int width, int height)
{
    UINT32 i;
    if (width != ctx->Width || height != ctx->Height)
    {
        if (!freerdp_settings_set_uint32(ctx->rdp.settings, FreeRDP_DesktopWidth, ctx->Width) ||
            !freerdp_settings_set_uint32(ctx->rdp.settings, FreeRDP_DesktopHeight, ctx->Height)) return FALSE;
        Log("Desktop resized to %dx%d", ctx->Width, ctx->Height);
        if (GfxActive(ctx->Gfx)) return TRUE;
        ctx->Active = FALSE;
        return ctx->rdp.update->DesktopResize(&ctx->rdp);
    }
    if (frame->Status != AGENT_FRAME_CHANGED) return TRUE;
    if (GfxActive(ctx->Gfx))
    {
        RECT ended;
        if (GfxPoll(ctx->Gfx, &ended)) UnionRect(&ctx->RefreshRect, &ctx->RefreshRect, &ended);
    }
    if (!frame->RectCount && !ctx->Refresh && !ctx->Deferred && IsRectEmpty(&ctx->RefreshRect)) return TRUE;
    for (i = 0; i < frame->RectCount && i < RDPVDD_FRAME_MAX_RECTS; ++i) MarkTiles(ctx, &frame->Rects[i]);
    QueueMoves(ctx, frame->Moves, frame->MoveCount);
    return SendDesktop(ctx, TRUE);
}

/* The agent answers a capture with pointer updates, then the frame it copied to the shared surface; the frame goes to the client after the agent is released, so a slow client holds up no other connection */
static BOOL CaptureAgent(DESKTOP_CONTEXT *ctx, BOOL frame)
{
    AGENT_CAPTURE_REQUEST request = {AGENT_CAPTURE};
    AGENT_FRAME_REPLY *reply = &ctx->Frame;
    int width = ctx->Width, height = ctx->Height;
    DWORD read;
    BOOL ok, shown = TRUE;
    request.Frame = frame;
    request.RequestedSize = frame ? InterlockedExchange(&ctx->RequestedSize, 0) : 0;
    request.LargePointerFlag = freerdp_settings_get_uint32(ctx->rdp.settings, FreeRDP_LargePointerFlag);
    request.PointerCacheSize = freerdp_settings_get_uint32(ctx->rdp.settings, FreeRDP_PointerCacheSize);
    request.ResendPointer = !ctx->PointerSent;
    if (!AgentEnter(ctx->Agent, ctx->Owner)) return !AgentBroken(ctx->Agent);
    ok = AgentWrite(ctx->Agent, &request, sizeof(request));
    while (ok)
    {
        ok = AgentRead(ctx->Agent, ctx->PointerReply, sizeof(*ctx->PointerReply), &read, AGENT_TIMEOUT);
        if (!ok) break;
        if (ctx->PointerReply->Code == AGENT_FRAME && read == sizeof(*reply))
        {
            memcpy(reply, ctx->PointerReply, sizeof(*reply));
            break;
        }
        if (ctx->PointerReply->Code != AGENT_POINTER || read < FIELD_OFFSET(AGENT_POINTER_REPLY, Data))
        {
            AgentAbandon(ctx->Agent);
            ok = FALSE;
            break;
        }
        if (shown && !ShowAgentPointer(ctx, ctx->PointerReply, read)) shown = FALSE;
    }
    if (ok && frame && reply->Status != AGENT_FRAME_GONE && !MapSurface(ctx, reply)) ok = FALSE;
    AgentLeave(ctx->Agent);
    if (!ok || !shown) return FALSE;
    ctx->TakenOver = reply->TakenOver;
    if (!frame) return TRUE;
    if (reply->Status == AGENT_FRAME_GONE) return FALSE;
    if (reply->Status == AGENT_FRAME_CHANGED) ctx->PointerSent = TRUE;
    return ShowAgentFrame(ctx, reply, width, height);
}

static void DetachAgent(DESKTOP_CONTEXT *ctx)
{
    AGENT_FRAME_REPLY reply;
    ULONG code = AGENT_DETACH;
    DWORD read;
    if (!ctx->Agent) return;
    if (ctx->Owner && AgentEnter(ctx->Agent, ctx->Owner))
    {
        if (!AgentWrite(ctx->Agent, &code, sizeof(code)) ||
            !AgentRead(ctx->Agent, &reply, sizeof(reply), &read, AGENT_TIMEOUT) ||
            read != sizeof(reply) || reply.Code != AGENT_DETACH)
        {
            AgentAbandon(ctx->Agent);
            Log("The agent of session %lu did not leave the display", AgentSession(ctx->Agent));
        }
        AgentLeave(ctx->Agent);
    }
    FreeDesktopCapture(ctx);
    BrokerDisconnect(ctx->Agent);
    ctx->Agent = NULL;
}

static void ContextFree(freerdp_peer *peer, rdpContext *rdp)
{
    DESKTOP_CONTEXT *ctx = (DESKTOP_CONTEXT *)rdp;
    (void)peer;
    SecureZeroMemory(ctx->NlaPassword, sizeof(ctx->NlaPassword));
    TransferStop(ctx->Transfer);
    disp_server_context_free(ctx->Disp);
    GfxFree(ctx->Gfx);
    freerdp_bitmap_planar_context_free(ctx->Planar);
    if (ctx->Vcm) WTSCloseServer(ctx->Vcm);
    if (ControllerMode) DesktopReleaseInput(&ctx->Input);
    Log("Session closed: frames=%llu updates=%llu input events=%llu", ctx->Frames,
        ctx->Updates, ControllerMode ? ctx->Input.Inputs : ctx->Inputs);
    DetachAgent(ctx);
    FreeDesktopCapture(ctx);
    DesktopLeave(&ctx->Thread);
    free(ctx->PointerReply);
}

static BOOL Disconnected(DESKTOP_CONTEXT *ctx, const char *reason)
{
    Log("Peer %s %s", ctx->rdp.peer->hostname, reason);
    freerdp_set_error_info(ctx->rdp.rdp, ERRINFO_DISCONNECTED_BY_OTHER_CONNECTION);
    if (!freerdp_send_error_info(ctx->rdp.rdp)) Log("Cannot tell peer %s why it is disconnected", ctx->rdp.peer->hostname);
    return TRUE;
}

static DWORD WINAPI ClientThread(void *arg)
{
    freerdp_peer *peer = arg;
    rdpSettings *settings;
    rdpPrivateKey *key;
    rdpCertificate *cert;
    DWORD count, status, started = GetTickCount(), wait;
    ULONGLONG lastFrame = 0, lastStatus = 0, elapsed;
    HANDLE events[32];
    DESKTOP_CONTEXT *ctx;
    peer->ContextSize = sizeof(DESKTOP_CONTEXT);
    peer->ContextFree = ContextFree;
    if (!freerdp_peer_context_new(peer)) goto done;
    ctx = (DESKTOP_CONTEXT *)peer->context;
    if (!ControllerMode && !(ctx->PointerReply = malloc(sizeof(*ctx->PointerReply)))) goto cleanup;
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
        !freerdp_settings_set_bool(settings, FreeRDP_TlsSecurity, ControllerMode) ||
        !freerdp_settings_set_bool(settings, FreeRDP_NlaSecurity, !ControllerMode) ||
        /* TODO: HYBRID_EX, which needs the Early User Authorization Result PDU from the FreeRDP server */
        !freerdp_settings_set_bool(settings, FreeRDP_ExtSecurity, FALSE) ||
        !freerdp_settings_set_bool(settings, FreeRDP_RemoteFxCodec, FALSE) ||
        !freerdp_settings_set_bool(settings, FreeRDP_NSCodec, FALSE) ||
        !freerdp_settings_set_bool(settings, FreeRDP_RefreshRect, TRUE) ||
        !freerdp_settings_set_bool(settings, FreeRDP_SuppressOutput, TRUE) ||
        !freerdp_settings_set_uint32(settings, FreeRDP_ColorDepth, 32)) goto cleanup;
    peer->Capabilities = Capabilities;
    if (!ControllerMode) peer->Logon = Logon;
    peer->PostConnect = PostConnect; peer->Activate = Activate;
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
        BOOL capture;
        events[0] = StopEvent;
        count = peer->GetEventHandles(peer, events + 1, 30);
        if (!count) break;
        events[++count] = WTSVirtualChannelManagerGetEventHandle(ctx->Vcm);
        if (ctx->Gfx && GfxEventHandle(ctx->Gfx)) events[++count] = GfxEventHandle(ctx->Gfx);
        wait = 100;
        if (ctx->Active && peer->activated)
        {
            elapsed = Milliseconds() - lastFrame;
            wait = elapsed >= FrameInterval ? 0 : (DWORD)(FrameInterval - elapsed);
        }
        status = WaitForMultipleObjects(count + 1, events, FALSE, wait);
        if (status == WAIT_OBJECT_0 || status == WAIT_FAILED) break;
        if (!InterlockedCompareExchange(&RemoteAllowed, 0, 0)) break;
        if (!peer->CheckFileDescriptor(peer)) break;
        if (!WTSVirtualChannelManagerCheckFileDescriptorEx(ctx->Vcm, peer->activated)) break;
        if (ctx->Gfx && !GfxHandleMessages(ctx->Gfx)) break;
        if (ctx->Authorized && !ctx->GfxTried && freerdp_settings_get_bool(ctx->rdp.settings, FreeRDP_SupportGraphicsPipeline) &&
            WTSVirtualChannelManagerGetDrdynvcState(ctx->Vcm) == DRDYNVC_STATE_READY) OpenGraphicsPipeline(ctx);
        if (!ControllerMode && ctx->Authorized && !ctx->Disp &&
            WTSVirtualChannelManagerGetDrdynvcState(ctx->Vcm) == DRDYNVC_STATE_READY) OpenDisplayControl(ctx);
        if (!ctx->Authorized && GetTickCount() - started > 30000) break;
        capture = ctx->Active && peer->activated && Milliseconds() - lastFrame >= FrameInterval;
        if (capture) lastFrame = Milliseconds();
        if (ctx->Agent && (capture || Milliseconds() - lastStatus >= AGENT_STATUS_INTERVAL))
        {
            lastStatus = Milliseconds();
            if (!CaptureAgent(ctx, capture && !ctx->Suppressed)) break;
        }
        if (ctx->TakenOver && Disconnected(ctx, "disconnected by a sign-in at the console")) break;
        if (Superseded(ctx) && Disconnected(ctx, "replaced by a newer connection")) break;
        if (capture && ControllerMode && ctx->Authorized && !ctx->Suppressed && !CaptureConsole(ctx)) break;
    }
    timeEndPeriod(1);
    peer->Disconnect(peer);
cleanup:
    freerdp_peer_context_free(peer);
done:
    freerdp_peer_free(peer);
    if (ControllerMode) InterlockedExchange(&ControllerConnected, 0);
    InterlockedDecrement(&ClientCount);
    return 0;
}

static BOOL Accepted(freerdp_listener *listener, freerdp_peer *peer)
{
    HANDLE thread;
    struct sockaddr_in address;
    int length = sizeof(address);
    (void)listener;
    if (ControllerMode)
    {
        if (getpeername((SOCKET)peer->sockfd, (struct sockaddr *)&address, &length) ||
            address.sin_family != AF_INET || address.sin_addr.s_addr != inet_addr(AllowedPeer)) return FALSE;
        if (InterlockedCompareExchange(&ControllerConnected, 1, 0)) return FALSE;
    }
    InterlockedIncrement(&ClientCount);
    thread = CreateThread(NULL, 0, ClientThread, peer, 0, NULL);
    if (!thread)
    {
        InterlockedDecrement(&ClientCount);
        if (ControllerMode) InterlockedExchange(&ControllerConnected, 0);
        return FALSE;
    }
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

static BOOL ConfiguredInterface(const IP_ADAPTER_INFO *adapter)
{
    unsigned i;
    if (adapter->AddressLength != sizeof(InterfaceMac[0])) return FALSE;
    for (i = 0; i < InterfaceMacCount; ++i)
        if (!memcmp(adapter->Address, InterfaceMac[i], sizeof(InterfaceMac[i]))) return TRUE;
    return FALSE;
}

/* Select the controller interface by MAC, never by adapter order, and keep its other addresses. */
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

static BOOL ControllerEnabled(const char *config)
{
    return *config && GetPrivateProfileIntA("server", "enabled", 0, config) != 0;
}

static int ReadControllerConfig(const char *config)
{
    char mac[MAX_INTERFACE_MACS * 18 + 16], mask[64], tail, *entry, *next;
    unsigned octets[6], i;
    if (!ProtectSecretFile(config))
    { Log("Cannot keep %s to administrators: %lu", config, GetLastError()); return 2; }
    GetPrivateProfileStringA("server", "bind", "", BindAddress, sizeof(BindAddress), config);
    GetPrivateProfileStringA("server", "allow", "", AllowedPeer, sizeof(AllowedPeer), config);
    GetPrivateProfileStringA("server", "username", "", Username, sizeof(Username), config);
    GetPrivateProfileStringA("server", "password", "", Password, sizeof(Password), config);
    GetPrivateProfileStringA("server", "certificate", "", Certificate, sizeof(Certificate), config);
    GetPrivateProfileStringA("server", "privatekey", "", PrivateKey, sizeof(PrivateKey), config);
    GetPrivateProfileStringA("server", "interface_mac", "", mac, sizeof(mac), config);
    GetPrivateProfileStringA("server", "netmask", "", mask, sizeof(mask), config);
    if (mac[0])
    {
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
                if (octets[i] > 255) { Log("Invalid test Ethernet MAC or netmask"); return 2; }
                InterfaceMac[InterfaceMacCount][i] = (BYTE)octets[i];
            }
            ++InterfaceMacCount;
        }
        ConfigureAddress = TRUE;
    }
    if (!ConfigRelativePath(config, Certificate, sizeof(Certificate)) ||
        !ConfigRelativePath(config, PrivateKey, sizeof(PrivateKey)))
    { Log("Certificate or private key path is too long"); return 2; }
    if (PrivateKey[0] && !ProtectSecretFile(PrivateKey))
    { Log("Cannot keep %s to administrators: %lu", PrivateKey, GetLastError()); return 2; }
    if (!Username[0] || strlen(Password) < 16 || inet_addr(BindAddress) == INADDR_NONE ||
        inet_addr(BindAddress) == INADDR_ANY || inet_addr(AllowedPeer) == INADDR_NONE ||
        inet_addr(AllowedPeer) == INADDR_ANY || !Certificate[0] || !PrivateKey[0])
    { Log("Invalid configuration: explicit bind/peer IPv4, credentials and TLS files required"); return 2; }
    ControllerMode = TRUE;
    return 0;
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

/* Session 0 has no interactive desktop; a process of the service runs in the session as SYSTEM. */
static HANDLE StartSessionProcess(DWORD session, const char *arguments)
{
    char path[MAX_PATH], command[3 * MAX_PATH + 64];
    HANDLE token = NULL, primary = NULL;
    STARTUPINFOA startup = {0};
    PROCESS_INFORMATION process = {0};
    BOOL started = FALSE;
    startup.cb = sizeof(startup);
    startup.lpDesktop = "winsta0\\default";
    if (GetModuleFileNameA(NULL, path, sizeof(path)) &&
        snprintf(command, sizeof(command), "\"%s\" %s", path, arguments) < (int)sizeof(command) &&
        OpenProcessToken(GetCurrentProcess(), TOKEN_DUPLICATE, &token) &&
        DuplicateTokenEx(token, TOKEN_ALL_ACCESS, NULL, SecurityImpersonation, TokenPrimary, &primary) &&
        SetTokenInformation(primary, TokenSessionId, &session, sizeof(session)))
        started = CreateProcessAsUserA(primary, path, command, NULL, NULL, FALSE, CREATE_NO_WINDOW,
                                       NULL, NULL, &startup, &process);
    if (!started) Log("Cannot start %s in session %lu error=%lu", arguments, session, GetLastError());
    if (primary) CloseHandle(primary);
    if (token) CloseHandle(token);
    if (!started) return NULL;
    CloseHandle(process.hThread);
    return process.hProcess;
}

static HANDLE StartSessionServer(DWORD session)
{
    char arguments[MAX_PATH + 16];
    HANDLE server;
    if (snprintf(arguments, sizeof(arguments), "/session \"%s\"", ServiceConfig) >= (int)sizeof(arguments)) return NULL;
    server = StartSessionProcess(session, arguments);
    if (server) Log("Server started in console session %lu", session);
    return server;
}

static HANDLE StartAgent(DWORD session, const WCHAR *pipe, const WCHAR *display)
{
    char arguments[MAX_DEVICE_ID_LEN + AGENT_PIPE_KEY_CHARS + 48], instance[MAX_DEVICE_ID_LEN];
    HANDLE agent;
    if (display && !WideCharToMultiByte(CP_UTF8, 0, display, -1, instance, sizeof(instance), NULL, NULL)) return NULL;
    if (snprintf(arguments, sizeof(arguments), display ? "/agent %lu %ls \"%s\"" : "/agent %lu %ls",
                 GetCurrentProcessId(), pipe, display ? instance : "") >= (int)sizeof(arguments)) return NULL;
    agent = StartSessionProcess(session, arguments);
    if (agent) Log("Agent started in %s session %lu", display ? "new" : "console", session);
    return agent;
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

static BOOL BrokerAllowed(void)
{
    return InterlockedCompareExchange(&RemoteAllowed, 0, 0) != 0;
}

static BOOL ReadTransferRoot(const char *config)
{
    char root[MAX_PATH];
    if (!*config) return TRUE;
    GetPrivateProfileStringA("server", "transfer_root", "", root, sizeof(root), config);
    return !*root || MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, root, -1, TransferRoot, MAX_PATH);
}

static int StartServer(const char *config)
{
    char directory[MAX_PATH], *separator;
    WSADATA winsock;
    if (!ReadTransferRoot(config)) return 2;
    if (!GetModuleFileNameA(NULL, directory, sizeof(directory))) return 1;
    separator = strrchr(directory, '\\');
    if (!separator) return 1;
    *separator = 0;
    if (!ControllerMode &&
        (snprintf(Certificate, sizeof(Certificate), "%s\\server.crt", directory) >= (int)sizeof(Certificate) ||
         snprintf(PrivateKey, sizeof(PrivateKey), "%s\\server.key", directory) >= (int)sizeof(PrivateKey))) return 1;
    if (WSAStartup(MAKEWORD(2, 2), &winsock)) return 1;
    if (!winpr_InitializeSSL(WINPR_SSL_INIT_DEFAULT)) return 1;
    if (!ControllerMode && !EnsureCertificate()) return 2;
    StopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!StopEvent) return 1;
    FrameInterval = ReadFrameInterval();
    if (!WTSRegisterWtsApiFunctionTable(FreeRDP_InitWtsApi())) return 1;
    return 0;
}

static void TakePeer(freerdp_peer *peer, struct sockaddr_storage *address)
{
    if (address->ss_family == AF_INET6 && IN6_IS_ADDR_V4MAPPED(&((struct sockaddr_in6 *)address)->sin6_addr))
    {
        struct sockaddr_in mapped = {0};
        mapped.sin_family = AF_INET;
        mapped.sin_port = ((struct sockaddr_in6 *)address)->sin6_port;
        memcpy(&mapped.sin_addr, &((struct sockaddr_in6 *)address)->sin6_addr.s6_addr[12], 4);
        ZeroMemory(address, sizeof(*address));
        memcpy(address, &mapped, sizeof(mapped));
    }
    if (!freerdp_peer_set_local_and_hostname(peer, address) || !Accepted(NULL, peer)) freerdp_peer_free(peer);
}

/* The service authenticates every connection in session 0 before it reaches any session */
static BOOL AcceptConnection(SOCKET socket)
{
    struct sockaddr_storage address;
    u_long nonblocking = 1;
    int length = sizeof(address);
    freerdp_peer *peer;
    if (WSAEventSelect(socket, NULL, 0) || ioctlsocket(socket, FIONBIO, &nonblocking) ||
        getpeername(socket, (struct sockaddr *)&address, &length))
        return FALSE;
    peer = freerdp_peer_new((int)socket);
    if (!peer)
    {
        Log("Cannot take a connection error=%lu", WSAGetLastError());
        return FALSE;
    }
    TakePeer(peer, &address);
    return TRUE;
}

static void RunController(HKEY key, HANDLE changed)
{
    HANDLE server = NULL, waits[2] = {ServiceStopEvent, changed};
    DWORD session = 0, console, code = 0, wait;
    BOOL armed = FALSE, failed = FALSE;
    for (;;)
    {
        if (!armed)
            armed = !RegNotifyChangeKeyValue(key, FALSE, REG_NOTIFY_CHANGE_LAST_SET, changed, TRUE);
        console = WTSGetActiveConsoleSessionId();
        if (server && WaitForSingleObject(server, 0) == WAIT_OBJECT_0)
        {
            GetExitCodeProcess(server, &code);
            CloseHandle(server);
            server = NULL;
            Log("Server in console session %lu exited with %lu", session, code);
            failed = code == 2;
        }
        if (server && console != session && console != 0xFFFFFFFF) StopSessionServer(&server);
        if (!server && !failed && console != 0 && console != 0xFFFFFFFF)
        {
            server = StartSessionServer(console);
            session = console;
        }
        wait = WaitForMultipleObjects(2, waits, FALSE, 2000);
        if (wait == WAIT_OBJECT_0 || wait == WAIT_FAILED) break;
        if (wait == WAIT_OBJECT_0 + 1) armed = failed = FALSE;
    }
    if (server) StopSessionServer(&server);
}

static DWORD RunBroker(HKEY key, HANDLE changed)
{
    HANDLE waits[2] = {ServiceStopEvent, changed};
    DWORD code = StartServer(ServiceConfig), wait;
    BOOL armed = FALSE;
    if (code) return code;
    InterlockedExchange(&RemoteAllowed, RemoteDesktopAllowed(key));
    if (!BrokerStart(Log, StartAgent, BrokerAllowed, AcceptConnection)) return GetLastError();
    ReportService(SERVICE_RUNNING, NO_ERROR);
    for (;;)
    {
        if (!armed)
            armed = !RegNotifyChangeKeyValue(key, FALSE, REG_NOTIFY_CHANGE_LAST_SET, changed, TRUE);
        InterlockedExchange(&RemoteAllowed, RemoteDesktopAllowed(key));
        BrokerSetConsoleSession(WTSGetActiveConsoleSessionId());
        wait = WaitForMultipleObjects(2, waits, FALSE, 2000);
        if (wait == WAIT_OBJECT_0 || wait == WAIT_FAILED) break;
        if (wait == WAIT_OBJECT_0 + 1) armed = FALSE;
    }
    BrokerClose();
    SetEvent(StopEvent);
    while (InterlockedCompareExchange(&ClientCount, 0, 0)) Sleep(50);
    BrokerStop();
    return NO_ERROR;
}

static void WINAPI ServiceMain(DWORD argc, char **argv)
{
    HANDLE changed;
    HKEY key = NULL;
    DWORD code = 0;
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
    /* The controller connects to the console session's server directly */
    if (ControllerEnabled(ServiceConfig))
    {
        ReportService(SERVICE_RUNNING, NO_ERROR);
        RunController(key, changed);
    }
    else
    {
        code = RunBroker(key, changed);
    }
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
    char config[MAX_PATH] = "";
    freerdp_listener *listener;
    HANDLE events[32], singleton;
    DWORD count, status;
    HANDLE serviceStop = NULL, thread;
    WCHAR display[MAX_DEVICE_ID_LEN], pipe[AGENT_PIPE_KEY_CHARS + 1];
    BOOL serviceMode = argc >= 2 && !_stricmp(argv[1], "/service");
    BOOL sessionMode = argc >= 2 && !_stricmp(argv[1], "/session");
    int first = (serviceMode || sessionMode) ? 2 : 1, code;
    DebugPrint = (DEBUG_PRINT)(void *)GetProcAddress(GetModuleHandleA("ntdll.dll"), "DbgPrint");
    if (argc >= 4 && argc <= 5 && !_stricmp(argv[1], "/agent"))
    {
        if (strlen(argv[3]) != AGENT_PIPE_KEY_CHARS || strspn(argv[3], "0123456789abcdef") != AGENT_PIPE_KEY_CHARS ||
            !MultiByteToWideChar(CP_UTF8, 0, argv[3], -1, pipe, ARRAYSIZE(pipe)) ||
            (argc == 5 && !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, argv[4], -1, display, ARRAYSIZE(display))))
            return 2;
        return AgentMain(strtoul(argv[2], NULL, 10), pipe, argc == 5 ? display : NULL, Log);
    }
    if (argc > first + 1 || (argc == first + 1 && !GetFullPathNameA(argv[first], sizeof(config), config, NULL)))
    { Log("Usage: rdpserver.exe [/service] [rdpserver.ini]"); return 2; }
    if (serviceMode)
    {
        SERVICE_TABLE_ENTRYA table[] = {{(char *)SERVICE_NAME, ServiceMain}, {NULL, NULL}};
        strcpy(ServiceConfig, config);
        return StartServiceCtrlDispatcherA(table) ? 0 : 1;
    }
    if (!ControllerEnabled(config))
    { Log("A server started by hand needs a controller configuration"); return 2; }
    if (ReadControllerConfig(config)) return 2;
    singleton = CreateMutexA(NULL, FALSE, "Local\\LiberNT-RDPServer-Console");
    if (!singleton || GetLastError() == ERROR_ALREADY_EXISTS) { if (singleton) CloseHandle(singleton); return 1; }
    code = StartServer(config);
    if (code) return code;
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
    Log("Starting console server; waiting for %s:3389", BindAddress);
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
    while (InterlockedCompareExchange(&ClientCount, 0, 0)) Sleep(50);
    freerdp_listener_free(listener);
    SetConsoleCtrlHandler(Stop, FALSE);
    CloseHandle(StopEvent); CloseHandle(singleton);
    SecureZeroMemory(Password, sizeof(Password));
    if (AddressAdded) DeleteIPAddress(AddressContext);
    WSACleanup();
    return 0;
}
