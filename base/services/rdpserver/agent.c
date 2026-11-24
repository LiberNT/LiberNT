/*
 * PROJECT:     LiberNT RDP console server
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Show a session's desktop to the service and inject the input of its client
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <sddl.h>
#include <stdio.h>
#include <swdevice.h>
#include <cfgmgr32.h>
#include <initguid.h>
#include <ntddvdeo.h>
#include <d3dkmthk.h>
#include <freerdp/constants.h>
#include <freerdp/error.h>
#include <freerdp/settings_types.h>
#include <remoteconsole.h>
#include "agent.h"
#include "desktop.h"
#include "experience.h"

#ifndef NT_SUCCESS
#define NT_SUCCESS(status) ((NTSTATUS)(status) >= 0)
#endif

HCURSOR WINAPI GetCursorFrameInfo(HCURSOR, DWORD, DWORD, PINT, PDWORD);

static AGENT_LOG Log;
static HANDLE Pipe;
static const WCHAR *AdoptedDisplay;
static DESKTOP_THREAD Thread;
static DESKTOP_INPUT Input;
static RDP_EXPERIENCE_STATE Experience;
static HSWDEVICE RemoteDisplay;
static HANDLE RemoteDisplayReady;
static WCHAR RemoteDisplayInstance[MAX_DEVICE_ID_LEN];
static BOOL RemoteDisplayShown;
static BYTE RemoteDisplaySid[SECURITY_MAX_SID_SIZE];
static HANDLE RemoteConsole;
static BOOL ConsoleTakeover;
static ULONG ConnectionNumber;
static int Width, Height, FrameWidth, FrameHeight;
static HANDLE Section;
static BYTE *Pixels, *Frame;
static ULONG Serial;
static D3DKMT_HANDLE FrameAdapter;
static HANDLE FrameEvent;
static BOOL FramePending;
static HCURSOR Pointer;
static BOOL PointerSent, PointerCached;
static UINT32 PointerSteps, PointerStep;
static DWORD PointerStepTick, PointerStepTime;
static UINT32 LargePointerFlag, PointerCacheSize;
static UINT32 PointerPixels[RDPVDD_POINTER_MAX * RDPVDD_POINTER_MAX];
static BYTE PointerTransparent[RDPVDD_POINTER_MAX * RDPVDD_POINTER_MAX];
static BYTE PointerMask[RDPVDD_POINTER_MAX * 2 * (RDPVDD_POINTER_MAX / 8)];
static AGENT_POINTER_REPLY PointerReply;

static BOOL Transfer(BOOL write, void *buffer, DWORD size, DWORD *done)
{
    static HANDLE event;
    OVERLAPPED overlapped = {0};
    BOOL finished;
    if (!event && !(event = CreateEventW(NULL, TRUE, FALSE, NULL))) return FALSE;
    overlapped.hEvent = event;
    finished = write ? WriteFile(Pipe, buffer, size, done, &overlapped) : ReadFile(Pipe, buffer, size, done, &overlapped);
    return finished || (GetLastError() == ERROR_IO_PENDING && GetOverlappedResult(Pipe, &overlapped, done, TRUE));
}

static BOOL Send(const void *message, DWORD size)
{
    DWORD written;
    return Transfer(TRUE, (void *)message, size, &written) && written == size;
}

static void FreeCapture(void)
{
    if (Pixels) UnmapViewOfFile(Pixels);
    if (Section) CloseHandle(Section);
    free(Frame);
    Pixels = Frame = NULL;
    Section = NULL;
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

static void CloseRemoteFrames(void)
{
    D3DKMT_CLOSEADAPTER close = {0};
    if (!FrameAdapter) return;
    close.hAdapter = FrameAdapter;
    D3DKMTCloseAdapter(&close);
    if (FrameEvent) CloseHandle(FrameEvent);
    FrameAdapter = 0;
    FrameEvent = NULL;
    FramePending = FALSE;
    FreeCapture();
}

static BOOL SyncRemoteFrames(void)
{
    RDPVDD_FRAME_ESCAPE request = {0};
    NTSTATUS status;
    request.Code = RDPVDD_ESCAPE_OPEN_FRAME;
    request.Event = (ULONG64)(ULONG_PTR)FrameEvent;
    status = FrameEscape(FrameAdapter, &request);
    if (!NT_SUCCESS(status))
    {
        Log("Cannot read the remote display: 0x%08lx", status);
        CloseRemoteFrames();
        return FALSE;
    }
    FramePending = TRUE;
    return TRUE;
}

static BOOL AllocateCapture(void)
{
    SIZE_T size;
    FreeCapture();
    if (!FrameAdapter) return FALSE;
    Width = FrameWidth;
    Height = FrameHeight;
    if (Width < 2 || Height < 2 || Width > RDPVDD_MAX_WIDTH || Height > RDPVDD_MAX_HEIGHT) return FALSE;
    size = (SIZE_T)Width * Height * 4;
    Frame = calloc(size, 1);
    Section = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, (DWORD)size, NULL);
    Pixels = Section ? MapViewOfFile(Section, FILE_MAP_WRITE, 0, 0, size) : NULL;
    if (!Frame || !Pixels)
    {
        FreeCapture();
        return FALSE;
    }
    ++Serial;
    SyncRemoteFrames();
    return TRUE;
}

static BOOL FollowInputDesktop(void)
{
    int switched = DesktopFollowInput(&Thread, Log);
    if (switched < 0) return FALSE;
    if (!FrameAdapter) return TRUE;
    return (!switched && Pixels) || AllocateCapture();
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

static BOOL RemoteDisplayListsMode(int width, int height)
{
    WCHAR name[ARRAYSIZE(((DISPLAY_DEVICEW *)0)->DeviceName)];
    DEVMODEW mode;
    DWORD index;
    if (!FindRemoteDisplay(name)) return FALSE;
    if (!width) return TRUE;
    for (index = 0;; ++index)
    {
        ZeroMemory(&mode, sizeof(mode));
        mode.dmSize = sizeof(mode);
        if (!EnumDisplaySettingsW(name, index, &mode)) return FALSE;
        if ((int)mode.dmPelsWidth == width && (int)mode.dmPelsHeight == height) return TRUE;
    }
}

static BOOL WaitForRemoteDisplay(int width, int height)
{
    DWORD start = GetTickCount();
    while (!RemoteDisplayListsMode(width, height))
    {
        if (GetTickCount() - start > 15000) return FALSE;
        Sleep(100);
    }
    return TRUE;
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

static BOOL TokenUserSid(HANDLE token, BYTE *sid, DWORD size)
{
    BYTE buffer[sizeof(TOKEN_USER) + SECURITY_MAX_SID_SIZE];
    DWORD length;
    if (!GetTokenInformation(token, TokenUser, buffer, sizeof(buffer), &length)) return FALSE;
    return CopySid(size, sid, ((TOKEN_USER *)buffer)->User.Sid);
}

static BOOL ProcessIsSystem(DWORD id, DWORD session)
{
    BYTE sid[SECURITY_MAX_SID_SIZE];
    DWORD processSession = (DWORD)-1;
    HANDLE process, token;
    BOOL system = FALSE;
    if (!ProcessIdToSessionId(id, &processSession) || processSession != session) return FALSE;
    process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, id);
    if (!process) return FALSE;
    if (OpenProcessToken(process, TOKEN_QUERY, &token))
    {
        system = TokenUserSid(token, sid, sizeof(sid)) && IsWellKnownSid(sid, WinLocalSystemSid);
        CloseHandle(token);
    }
    CloseHandle(process);
    return system;
}

static BOOL CallRemoteConsole(REMOTE_CONSOLE_MESSAGE *message)
{
    REMOTE_CONSOLE_MESSAGE reply;
    DWORD bytes;
    while (ReadFile(RemoteConsole, &reply, sizeof(reply), &bytes, NULL) && bytes == sizeof(reply))
    {
        if (reply.Code == REMOTE_CONSOLE_TAKEOVER)
        {
            ConsoleTakeover = TRUE;
            continue;
        }
        *message = reply;
        return reply.Code == message->Code;
    }
    return FALSE;
}

static BOOL ConnectRemoteConsole(AGENT_ATTACH_REQUEST *request, ULONG *consoleStatus)
{
    REMOTE_CONSOLE_MESSAGE message = {0};
    WCHAR pipe[64];
    DWORD session = 0, server = 0, bytes, mode = PIPE_READMODE_MESSAGE;
    ProcessIdToSessionId(GetCurrentProcessId(), &session);
    _snwprintf(pipe, ARRAYSIZE(pipe), REMOTE_CONSOLE_PIPE_FORMAT, session);
    pipe[ARRAYSIZE(pipe) - 1] = 0;
    RemoteConsole = CreateFileW(pipe, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
                                SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, NULL);
    if (RemoteConsole == INVALID_HANDLE_VALUE && GetLastError() == ERROR_PIPE_BUSY && WaitNamedPipeW(pipe, 10000))
        RemoteConsole = CreateFileW(pipe, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
                                    SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, NULL);
    if (RemoteConsole == INVALID_HANDLE_VALUE)
    {
        Log("Cannot reach Winlogon: %lu", GetLastError());
        RemoteConsole = NULL;
        return FALSE;
    }
    if (!GetNamedPipeServerProcessId(RemoteConsole, &server) || !ProcessIsSystem(server, session))
    {
        Log("The pipe of Winlogon was not made by the system in this session");
        CloseHandle(RemoteConsole);
        RemoteConsole = NULL;
        *consoleStatus = ERROR_ACCESS_DENIED;
        return FALSE;
    }
    SetNamedPipeHandleState(RemoteConsole, &mode, NULL, NULL);
    message.Code = REMOTE_CONSOLE_CONNECT;
    message.Connection = ConnectionNumber++;
    memcpy(message.UserName, request->UserName, sizeof(message.UserName));
    memcpy(message.Domain, request->Domain, sizeof(message.Domain));
    memcpy(message.Password, request->Password, sizeof(message.Password));
    if (!WriteFile(RemoteConsole, &message, sizeof(message), &bytes, NULL) || !CallRemoteConsole(&message))
        message.Status = ERROR_BROKEN_PIPE;
    SecureZeroMemory(message.Password, sizeof(message.Password));
    *consoleStatus = message.Status;
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
    if (!ConsoleTakeover && RemoteConsole && PeekNamedPipe(RemoteConsole, NULL, 0, NULL, &available, NULL) &&
        available && ReadFile(RemoteConsole, &message, sizeof(message), &bytes, NULL) && bytes == sizeof(message) &&
        message.Code == REMOTE_CONSOLE_TAKEOVER)
        ConsoleTakeover = TRUE;
    return ConsoleTakeover;
}

static void CloseRemoteDisplay(void)
{
    if (!RemoteDisplay) return;
    SwDeviceClose(RemoteDisplay);
    RemoteDisplay = NULL;
    Log("Remote display adapter closed");
}

static UINT32 ShowRemoteDisplay(AGENT_ATTACH_REQUEST *request, ULONG *consoleStatus)
{
    static const WCHAR hardwareIds[] = L"RdpVdd_RemoteDisplay\0";
    SW_DEVICE_CREATE_INFO info = {0};
    WCHAR instance[64];
    DWORD session = 0;
    int width = GetSystemMetrics(SM_CXSCREEN), height = GetSystemMetrics(SM_CYSCREEN);
    UINT32 error = ERRINFO_SUCCESS;
    HRESULT hr;
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
    ConsoleTakeover = FALSE;
    if (AdoptedDisplay)
    {
        /* The service made this session's display before the session existed */
        lstrcpynW(RemoteDisplayInstance, AdoptedDisplay, ARRAYSIZE(RemoteDisplayInstance));
        SetEvent(RemoteDisplayReady);
        hr = S_OK;
    }
    else
    {
        hr = SwDeviceCreate(L"RemoteDisplayEnum", L"HTREE\\ROOT\\0", &info, 0, NULL,
                            RemoteDisplayCreated, NULL, &RemoteDisplay);
    }
    if (FAILED(hr))
    {
        Log("Cannot create the remote display adapter: 0x%08lx", hr);
        RemoteDisplay = NULL;
        error = ERRINFO_CLOSE_STACK_ON_DRIVER_FAILURE;
    }
    else if (WaitForSingleObject(RemoteDisplayReady, 15000) == WAIT_OBJECT_0 && WaitForRemoteDisplay(0, 0))
    {
        if (!RemoteDisplayListsMode(width, height) && SetRemoteDisplayMode(width, height) &&
            !WaitForRemoteDisplay(width, height))
            Log("The remote display does not list %dx%d", width, height);
        RemoteDisplayShown = ConnectRemoteConsole(request, consoleStatus);
        if (RemoteDisplayShown)
            CopySid(sizeof(RemoteDisplaySid), RemoteDisplaySid, request->Sid);
        if (!RemoteDisplayShown)
            error = ERRINFO_SERVER_DENIED_CONNECTION;
        else if (SetRemoteDisplayMode(request->ClientWidth, request->ClientHeight) &&
                 !WaitForRemoteDisplayMode(request->ClientWidth, request->ClientHeight))
            Log("The desktop did not take the %dx%d mode", (int)request->ClientWidth, (int)request->ClientHeight);
    }
    else
    {
        Log("The remote display adapter did not start");
        error = ERRINFO_CLOSE_STACK_ON_DRIVER_NOT_READY;
    }
    if (error) CloseRemoteDisplay();
    return error;
}

static BOOL OpenRemoteFrames(int clientWidth, int clientHeight)
{
    RDPVDD_FRAME_ESCAPE request = {0};
    NTSTATUS status = STATUS_UNSUCCESSFUL;
    if (FrameAdapter) return TRUE;
    if (RemoteDisplayShown && OpenRemoteAdapter(&FrameAdapter))
    {
        FrameEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
        request.Code = RDPVDD_ESCAPE_READ_FRAME;
        status = FrameEvent ? FrameEscape(FrameAdapter, &request) : STATUS_NO_MEMORY;
        if (!NT_SUCCESS(status))
        {
            CloseRemoteFrames();
        }
        else if (!request.Width || !request.Height)
        {
            Log("The remote display has no frame yet");
            FrameWidth = clientWidth;
            FrameHeight = clientHeight;
        }
        else
        {
            FrameWidth = (int)request.Width;
            FrameHeight = (int)request.Height;
        }
    }
    if (!FrameAdapter)
    {
        Log("Cannot read the remote display: 0x%08lx", status);
        return FALSE;
    }
    Log("Sending the remote display");
    return TRUE;
}

static void Detach(void)
{
    DesktopReleaseInput(&Input);
    if (!RemoteDisplayShown) return;
    CloseRemoteFrames();
    if (!ExperienceRestore(&Experience)) Log("Cannot restore the console's visual settings");
    RemoteDisplayShown = FALSE;
    ZeroMemory(RemoteDisplaySid, sizeof(RemoteDisplaySid));
    DisconnectRemoteConsole(ConsoleTakeover);
    ConsoleTakeover = FALSE;
    CloseRemoteDisplay();
}

static void FillSurface(AGENT_FRAME_REPLY *reply)
{
    reply->Serial = Serial;
    reply->Section = (ULONG64)(ULONG_PTR)Section;
    reply->Width = Pixels ? Width : 0;
    reply->Height = Pixels ? Height : 0;
}

static void Attach(AGENT_ATTACH_REQUEST *request, AGENT_FRAME_REPLY *reply)
{
    RDP_EXPERIENCE experience;
    BOOL applied;
    reply->Error = ERRINFO_SUCCESS;
    if (!RemoteDisplayShown)
    {
        reply->Error = ShowRemoteDisplay(request, &reply->ConsoleStatus);
    }
    else if (!EqualSid(RemoteDisplaySid, request->Sid))
    {
        Log("The remote display belongs to another user");
        reply->ConsoleStatus = ERROR_ACCESS_DENIED;
        reply->Error = ERRINFO_SERVER_DENIED_CONNECTION;
    }
    else
    {
        DesktopReleaseInput(&Input);
        SetRemoteDisplayMode(request->ClientWidth, request->ClientHeight);
    }
    SecureZeroMemory(request->Password, sizeof(request->Password));
    if (reply->Error) return;
    if (!OpenRemoteFrames(request->ClientWidth, request->ClientHeight) || !FollowInputDesktop() || !Pixels)
    {
        reply->Error = ERRINFO_CLOSE_STACK_ON_DRIVER_FAILURE;
        return;
    }
    ExperienceFromClient(request->PerformanceFlags, request->ConnectionType, &experience);
    applied = ExperienceApply(&Experience, &experience);
    Log("Experience flags 0x%08lx connection type %lu: wallpaper %d, full window drag %d, effects %d%s",
        (ULONG)request->PerformanceFlags, (ULONG)request->ConnectionType, experience.Wallpaper,
        experience.DragFullWindows, experience.Effects, applied ? "" : " (not applied)");
    PointerSent = FALSE;
    FramePending = TRUE;
    FillSurface(reply);
}

static BOOL MaskBit(const BYTE *rows, int width, int x, int y)
{
    return (rows[(size_t)y * ((width + 31) / 32 * 4) + x / 8] & (0x80 >> (x % 8))) != 0;
}

static BOOL ReadPointer(HCURSOR handle, UINT16 cacheIndex)
{
    struct { BITMAPINFOHEADER Header; RGBQUAD Colors[2]; } info = {0};
    UINT32 flags = LargePointerFlag;
    int limit = (flags & LARGE_POINTER_FLAG_384x384) ? RDPVDD_POINTER_MAX : (flags & LARGE_POINTER_FLAG_96x96) ? 96 : 32;
    AGENT_POINTER_REPLY *update = &PointerReply;
    ICONINFO icon = {0};
    BITMAP bitmap = {0};
    BOOL ok = FALSE, alpha = FALSE;
    int width, height, targetWidth, targetHeight, stride, x, y;
    BYTE *andMask, *xorMask;
    HDC dc;
    if (!GetIconInfo(handle, &icon)) return FALSE;
    dc = GetDC(NULL);
    GetObjectW(icon.hbmMask, sizeof(bitmap), &bitmap);
    width = bitmap.bmWidth;
    height = icon.hbmColor ? bitmap.bmHeight : bitmap.bmHeight / 2;
    info.Header.biSize = sizeof(info.Header);
    info.Header.biWidth = width;
    info.Header.biHeight = bitmap.bmHeight;
    info.Header.biPlanes = 1;
    info.Header.biBitCount = 1;
    if (dc && width > 0 && height > 0 && width <= RDPVDD_POINTER_MAX && height <= RDPVDD_POINTER_MAX &&
        GetDIBits(dc, icon.hbmMask, 0, bitmap.bmHeight, PointerMask, (BITMAPINFO *)&info, DIB_RGB_COLORS) == bitmap.bmHeight)
    {
        if (icon.hbmColor)
        {
            info.Header.biHeight = height;
            info.Header.biBitCount = 32;
            ok = GetDIBits(dc, icon.hbmColor, 0, height, PointerPixels, (BITMAPINFO *)&info, DIB_RGB_COLORS) == height;
            for (x = 0; ok && x < width * height; ++x) alpha |= (PointerPixels[x] >> 24) != 0;
            for (y = 0; ok && y < height; ++y)
                for (x = 0; x < width; ++x)
                {
                    UINT32 *pixel = &PointerPixels[y * width + x];
                    BOOL transparent = MaskBit(PointerMask, width, x, y);
                    if (alpha) PointerTransparent[y * width + x] = FALSE;
                    else
                    {
                        PointerTransparent[y * width + x] = transparent;
                        *pixel = transparent ? *pixel & 0xFFFFFF : *pixel | 0xFF000000;
                    }
                }
        }
        else
        {
            for (y = 0; y < height; ++y)
                for (x = 0; x < width; ++x)
                {
                    BOOL transparent = MaskBit(PointerMask, width, x, y + height);
                    BOOL white = MaskBit(PointerMask, width, x, y);
                    PointerTransparent[y * width + x] = transparent;
                    PointerPixels[y * width + x] = white ? 0xFFFFFFFF : transparent ? 0 : 0xFF000000;
                }
            ok = TRUE;
        }
    }
    if (ok)
    {
        targetWidth = width;
        targetHeight = height;
        if (max(width, height) > limit)
        {
            targetWidth = max(1, width * limit / max(width, height));
            targetHeight = max(1, height * limit / max(width, height));
        }
        stride = (targetWidth + 15) / 16 * 2;
        andMask = update->Data;
        xorMask = update->Data + (size_t)stride * targetHeight;
        ZeroMemory(andMask, (size_t)stride * targetHeight);
        for (y = 0; y < targetHeight; ++y)
            for (x = 0; x < targetWidth; ++x)
            {
                int source = (y * height / targetHeight) * width + x * width / targetWidth;
                memcpy(xorMask + ((size_t)y * targetWidth + x) * 4, &PointerPixels[source], 4);
                if (PointerTransparent[source]) andMask[y * stride + x / 8] |= 0x80 >> (x % 8);
            }
        update->Code = AGENT_POINTER;
        update->Kind = AGENT_POINTER_SHAPE;
        update->CacheIndex = cacheIndex;
        update->HotSpotX = (UINT16)(icon.xHotspot * targetWidth / width);
        update->HotSpotY = (UINT16)(icon.yHotspot * targetHeight / height);
        update->Width = (UINT16)targetWidth;
        update->Height = (UINT16)targetHeight;
        update->AndLength = (UINT32)(stride * targetHeight);
        update->XorLength = (UINT32)(targetWidth * 4 * targetHeight);
    }
    if (dc) ReleaseDC(NULL, dc);
    if (icon.hbmColor) DeleteObject(icon.hbmColor);
    if (icon.hbmMask) DeleteObject(icon.hbmMask);
    return ok;
}

static BOOL SendPointerShape(void)
{
    return Send(&PointerReply, FIELD_OFFSET(AGENT_POINTER_REPLY, Data) + PointerReply.AndLength + PointerReply.XorLength);
}

static BOOL SendPointerKind(ULONG kind, UINT16 cacheIndex)
{
    AGENT_POINTER_REPLY *reply = &PointerReply;
    ZeroMemory(reply, FIELD_OFFSET(AGENT_POINTER_REPLY, Data));
    reply->Code = AGENT_POINTER;
    reply->Kind = kind;
    reply->CacheIndex = cacheIndex;
    return Send(reply, FIELD_OFFSET(AGENT_POINTER_REPLY, Data));
}

static BOOL ShowPointerStep(UINT32 step)
{
    INT rate = 0;
    DWORD steps = 0;
    HCURSOR frame = GetCursorFrameInfo(Pointer, 0, step, &rate, &steps);
    PointerStep = step;
    PointerStepTick = GetTickCount();
    PointerStepTime = rate > 0 ? (DWORD)rate * 1000 / 60 : 1000 / 60;
    if (PointerCached) return SendPointerKind(AGENT_POINTER_CACHED, (UINT16)step);
    return !frame || !ReadPointer(frame, 0) || SendPointerShape();
}

static BOOL UpdatePointer(void)
{
    CURSORINFO cursor = {0};
    UINT32 cacheSize = PointerCacheSize;
    INT rate = 0;
    DWORD steps = 0, step;
    cursor.cbSize = sizeof(cursor);
    if (!GetCursorInfo(&cursor) || !(cursor.flags & CURSOR_SHOWING)) cursor.hCursor = NULL;
    if (PointerSent && cursor.hCursor == Pointer)
    {
        if (PointerSteps > 1 && GetTickCount() - PointerStepTick >= PointerStepTime)
            return ShowPointerStep((PointerStep + 1) % PointerSteps);
        return TRUE;
    }
    Pointer = cursor.hCursor;
    PointerSent = TRUE;
    PointerSteps = 0;
    PointerCached = FALSE;
    if (cursor.hCursor && !GetCursorFrameInfo(cursor.hCursor, 0, 0, &rate, &steps)) steps = 1;
    if (steps > 1 && steps <= cacheSize)
    {
        for (step = 0; step < steps; ++step)
        {
            HCURSOR frame = GetCursorFrameInfo(cursor.hCursor, 0, step, &rate, &steps);
            if (!frame || !ReadPointer(frame, (UINT16)step)) break;
            if (!SendPointerShape()) return FALSE;
        }
        PointerCached = step == steps;
    }
    if (steps > 1 && (PointerCached || steps > cacheSize))
    {
        PointerSteps = steps;
        return ShowPointerStep(0);
    }
    if (cursor.hCursor && ReadPointer(cursor.hCursor, 0)) return SendPointerShape();
    return SendPointerKind(AGENT_POINTER_HIDDEN, 0);
}

static BOOL ReadRemoteFrame(RDPVDD_FRAME_ESCAPE *frame)
{
    NTSTATUS status;
    memset(frame, 0, sizeof(*frame));
    frame->Code = RDPVDD_ESCAPE_READ_FRAME;
    frame->Buffer = (ULONG64)(ULONG_PTR)Frame;
    frame->BufferWidth = Frame ? Width : 0;
    frame->BufferHeight = Frame ? Height : 0;
    status = FrameEscape(FrameAdapter, frame);
    if (NT_SUCCESS(status)) return TRUE;
    Log("The remote display is gone: 0x%08lx", status);
    CloseRemoteFrames();
    return FALSE;
}

static void CopyFrameRect(const RECT *rect)
{
    int y, left = max(rect->left, 0), top = max(rect->top, 0);
    int right = min(rect->right, Width), bottom = min(rect->bottom, Height);
    if (left >= right || top >= bottom) return;
    for (y = top; y < bottom; ++y)
    {
        size_t offset = ((size_t)y * Width + left) * 4;
        memcpy(Pixels + offset, Frame + offset, (size_t)(right - left) * 4);
    }
}

static BOOL Capture(const AGENT_CAPTURE_REQUEST *request, AGENT_FRAME_REPLY *reply)
{
    RDPVDD_FRAME_ESCAPE frame = {0};
    LONG size = request->RequestedSize;
    UINT32 i;
    reply->Status = AGENT_FRAME_NONE;
    reply->TakenOver = ConsoleSignedIn();
    if (!request->Frame || !RemoteDisplayShown) return TRUE;
    reply->Status = AGENT_FRAME_GONE;
    if (!FollowInputDesktop() || !FrameAdapter) return TRUE;
    if (size) SetRemoteDisplayMode(HIWORD(size), LOWORD(size));
    LargePointerFlag = request->LargePointerFlag;
    PointerCacheSize = request->PointerCacheSize;
    if (WaitForSingleObject(FrameEvent, 0) == WAIT_OBJECT_0) FramePending = TRUE;
    frame.Width = Width;
    frame.Height = Height;
    if (FramePending)
    {
        FramePending = FALSE;
        if (!ReadRemoteFrame(&frame)) return TRUE;
    }
    reply->Status = AGENT_FRAME_NONE;
    if (!frame.Width)
    {
        FramePending = TRUE;
        FillSurface(reply);
        return TRUE;
    }
    FrameWidth = (int)frame.Width;
    FrameHeight = (int)frame.Height;
    if (Width != FrameWidth || Height != FrameHeight)
    {
        if (!AllocateCapture())
        {
            reply->Status = AGENT_FRAME_GONE;
            return TRUE;
        }
        FillSurface(reply);
        return TRUE;
    }
    if (request->ResendPointer) PointerSent = FALSE;
    if (!UpdatePointer()) return FALSE;
    for (i = 0; i < frame.RectCount && i < RDPVDD_FRAME_MAX_RECTS; ++i)
    {
        CopyFrameRect(&frame.Rects[i]);
        reply->Rects[i] = frame.Rects[i];
    }
    reply->RectCount = i;
    reply->MoveCount = min(frame.MoveCount, RDPVDD_FRAME_MAX_MOVES);
    memcpy(reply->Moves, frame.Moves, reply->MoveCount * sizeof(reply->Moves[0]));
    reply->Status = AGENT_FRAME_CHANGED;
    FillSurface(reply);
    return TRUE;
}

static void HandleInput(const AGENT_INPUT_REQUEST *request)
{
    if (!RemoteDisplayShown || FollowInputDesktop() == FALSE) return;
    if (request->Kind == AGENT_KEYBOARD) DesktopKeyboard(&Input, request->Flags, (UINT8)request->Value);
    else if (request->Kind == AGENT_UNICODE) DesktopUnicode(request->Flags, request->Value);
    else if (request->Kind == AGENT_MOUSE) DesktopMouse(&Input, Width, Height, request->Flags, request->X, request->Y);
}

static BOOL Serve(BYTE *message, DWORD size)
{
    AGENT_FRAME_REPLY reply;
    ULONG code = size >= sizeof(ULONG) ? *(ULONG *)message : 0;
    ZeroMemory(&reply, sizeof(reply));
    reply.Code = code;
    switch (code)
    {
        case AGENT_ATTACH:
            if (size != sizeof(AGENT_ATTACH_REQUEST)) return FALSE;
            Attach((AGENT_ATTACH_REQUEST *)message, &reply);
            SecureZeroMemory(message, size);
            return Send(&reply, sizeof(reply));
        case AGENT_DETACH:
            Detach();
            return Send(&reply, sizeof(reply));
        case AGENT_INPUT:
            if (size != sizeof(AGENT_INPUT_REQUEST)) return FALSE;
            HandleInput((AGENT_INPUT_REQUEST *)message);
            return TRUE;
        case AGENT_CAPTURE:
            if (size != sizeof(AGENT_CAPTURE_REQUEST)) return FALSE;
            reply.Code = AGENT_FRAME;
            return Capture((AGENT_CAPTURE_REQUEST *)message, &reply) && Send(&reply, sizeof(reply));
        default:
            return FALSE;
    }
}

static HANDLE OpenServiceProcess(DWORD id)
{
    HANDLE process;
    if (!ProcessIsSystem(id, 0)) return NULL;
    process = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, id);
    if (process && !ProcessIsSystem(id, 0))
    {
        CloseHandle(process);
        process = NULL;
    }
    return process;
}

static BOOL WaitForService(HANDLE service)
{
    OVERLAPPED overlapped = {0};
    HANDLE waits[2];
    DWORD wait, done;
    BOOL connected;
    waits[0] = overlapped.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    waits[1] = service;
    if (!overlapped.hEvent) return FALSE;
    connected = ConnectNamedPipe(Pipe, &overlapped);
    if (connected || GetLastError() == ERROR_PIPE_CONNECTED || GetLastError() != ERROR_IO_PENDING)
    {
        connected = connected || GetLastError() == ERROR_PIPE_CONNECTED;
        CloseHandle(overlapped.hEvent);
        return connected;
    }
    wait = WaitForMultipleObjects(2, waits, FALSE, 60000);
    if (wait != WAIT_OBJECT_0) CancelIo(Pipe);
    connected = GetOverlappedResult(Pipe, &overlapped, &done, TRUE) && wait == WAIT_OBJECT_0;
    CloseHandle(overlapped.hEvent);
    return connected;
}

int AgentMain(DWORD serviceId, const WCHAR *pipeName, const WCHAR *display, AGENT_LOG log)
{
    SECURITY_ATTRIBUTES security = {sizeof(security), NULL, FALSE};
    WCHAR name[128];
    DWORD session = 0, client = 0, read;
    HANDLE service;
    static AGENT_ATTACH_REQUEST request;
    BYTE *message = (BYTE *)&request;
    const DWORD capacity = sizeof(request);
    Log = log;
    AdoptedDisplay = display;
    service = OpenServiceProcess(serviceId);
    if (!service)
    {
        Log("Process %lu is not the service", serviceId);
        return 2;
    }
    RemoteDisplayReady = CreateEventW(NULL, TRUE, FALSE, NULL);
    ProcessIdToSessionId(GetCurrentProcessId(), &session);
    _snwprintf(name, ARRAYSIZE(name), AGENT_PIPE_FORMAT, session, pipeName);
    name[ARRAYSIZE(name) - 1] = 0;
    if (!RemoteDisplayReady ||
        !ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;;GA;;;SY)", SDDL_REVISION_1,
                                                              &security.lpSecurityDescriptor, NULL))
        return 1;
    Pipe = CreateNamedPipeW(name, PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE | FILE_FLAG_OVERLAPPED,
                            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT, 1,
                            65536, capacity, 0, &security);
    LocalFree(security.lpSecurityDescriptor);
    if (Pipe == INVALID_HANDLE_VALUE)
    {
        Log("Cannot open the pipe of session %lu error=%lu", session, GetLastError());
        return 1;
    }
    if (!WaitForService(service) || !GetNamedPipeClientProcessId(Pipe, &client) || client != serviceId)
    {
        Log("The service did not open the pipe of session %lu (client %lu)", session, client);
        CloseHandle(Pipe);
        return 1;
    }
    Log("Agent of session %lu serves the service", session);
    while (Transfer(FALSE, message, capacity, &read) && Serve(message, read));
    SecureZeroMemory(message, capacity);
    Detach();
    DesktopLeave(&Thread);
    CloseHandle(Pipe);
    CloseHandle(service);
    Log("Agent of session %lu ended", session);
    return 0;
}
