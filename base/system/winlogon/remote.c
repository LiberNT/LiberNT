/*
 * PROJECT:     LiberNT Winlogon
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Session taken over by a Remote Desktop client and the lock display of the console
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include "winlogon.h"

#include <winnls.h>
#include <winsvc.h>
#include <ntsecapi.h>
#include <ntddkbd.h>
#include <sddl.h>
#include <wtsapi32.h>
#include <winsvc_undoc.h>
#include <reactos/wtssession.h>
#include <remoteconsole.h>

#define CONSOLE_CLOCK_TIMER 0x5243

static HANDLE RemotePipe;
static CRITICAL_SECTION RemotePipeLock;
static DWORD RemoteSessionId;
static BOOL RemoteConnected;
static BOOL RemoteLeftSession;
static BOOL RemoteTakeoverSent;
static WCHAR ConsoleDevice[CCHDEVICENAME];
static WCHAR ConsolePassword[256];
static ULONG ConsolePasswordLength;
static BOOL ConsolePasswordWrong;
static BYTE ConsoleKeys[256];
static HANDLE ConsoleLsa;
static ULONG ConsolePackage;

static
VOID
NotifyWindows(
    _In_ DWORD Event)
{
    NtUserCallTwoParam(Event, RemoteSessionId, ROS_WTS_NOTIFY);
}

static
VOID
NotifyServices(
    _In_ DWORD Event,
    _In_ DWORD SessionId)
{
    WTSSESSION_NOTIFICATION Notification = { sizeof(Notification), SessionId };
    LPENUM_SERVICE_STATUS_PROCESSW Services = NULL;
    SERVICE_STATUS_HANDLE hStatus;
    SC_HANDLE hManager;
    DWORD Size = 0, Count = 0, Resume = 0, i;

    hManager = OpenSCManagerW(NULL, SERVICES_ACTIVE_DATABASEW, SC_MANAGER_CONNECT | SC_MANAGER_ENUMERATE_SERVICE);
    if (!hManager)
        return;
    if (!EnumServicesStatusExW(hManager, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_ACTIVE, NULL, 0,
                               &Size, &Count, &Resume, NULL) &&
        GetLastError() == ERROR_MORE_DATA)
    {
        Services = HeapAlloc(GetProcessHeap(), 0, Size);
        Resume = 0;
        if (Services && !EnumServicesStatusExW(hManager, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_ACTIVE,
                                               (PBYTE)Services, Size, &Size, &Count, &Resume, NULL))
        {
            Count = 0;
        }
    }
    for (i = 0; Services && i < Count; i++)
    {
        if (Services[i].ServiceStatusProcess.dwCurrentState != SERVICE_RUNNING ||
            !(Services[i].ServiceStatusProcess.dwControlsAccepted & SERVICE_ACCEPT_SESSIONCHANGE))
        {
            continue;
        }
        if (I_ScValidatePnpService(NULL, Services[i].lpServiceName, &hStatus) == ERROR_SUCCESS)
            I_ScSendPnPMessage(hStatus, SERVICE_CONTROL_SESSIONCHANGE, Event, &Notification);
    }
    if (Services)
        HeapFree(GetProcessHeap(), 0, Services);
    CloseServiceHandle(hManager);
}

static
VOID
SetRemoteRecord(
    _In_ BOOL Present,
    _In_ BOOL Connected,
    _In_ ULONG Connection)
{
    WCHAR KeyName[MAX_PATH], StationName[32];
    DWORD Value;
    HKEY hKey;

    StringCchPrintfW(KeyName, ARRAYSIZE(KeyName), L"%s\\%lu", REMOTE_CONSOLE_KEY, RemoteSessionId);
    if (!Present)
    {
        RegDeleteKeyW(HKEY_LOCAL_MACHINE, KeyName);
        return;
    }

    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, REMOTE_CONSOLE_KEY, 0, NULL, REG_OPTION_VOLATILE,
                        KEY_CREATE_SUB_KEY, NULL, &hKey, NULL) == ERROR_SUCCESS)
    {
        RegCloseKey(hKey);
    }
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, KeyName, 0, NULL, REG_OPTION_VOLATILE,
                        KEY_SET_VALUE, NULL, &hKey, NULL) != ERROR_SUCCESS)
    {
        ERR("WL: Cannot record the remote connection of session %lu\n", RemoteSessionId);
        return;
    }

    if (Connected)
        StringCchPrintfW(StationName, ARRAYSIZE(StationName), L"RDP-Tcp#%lu", Connection);
    else
        StationName[0] = UNICODE_NULL;
    RegSetValueExW(hKey, L"WinStationName", 0, REG_SZ, (PBYTE)StationName,
                   (DWORD)(wcslen(StationName) + 1) * sizeof(WCHAR));
    Value = 2;
    RegSetValueExW(hKey, L"ProtocolType", 0, REG_DWORD, (PBYTE)&Value, sizeof(Value));
    Value = Connected ? WTSActive : WTSDisconnected;
    RegSetValueExW(hKey, L"ConnectState", 0, REG_DWORD, (PBYTE)&Value, sizeof(Value));
    RegCloseKey(hKey);
}

static
HFONT
CreateConsoleFont(
    _In_ INT Height,
    _In_ INT Weight)
{
    return CreateFontW(-Height, 0, 0, 0, Weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                       CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
}

static
VOID
DrawConsoleText(
    _In_ HDC hdc,
    _In_ PCWSTR Text,
    _In_ INT Height,
    _In_ INT Weight,
    _Inout_ PRECT Rect)
{
    HFONT hFont, hOldFont;
    RECT Line = *Rect;

    hFont = CreateConsoleFont(Height, Weight);
    hOldFont = SelectObject(hdc, hFont);
    DrawTextW(hdc, Text, -1, &Line, DT_CENTER | DT_SINGLELINE | DT_NOPREFIX | DT_TOP);
    Rect->top += Height + Height / 2;
    SelectObject(hdc, hOldFont);
    DeleteObject(hFont);
}

static
VOID
DrawConsoleLock(
    _In_ PWLSESSION Session)
{
    WCHAR Text[256], Format[192];
    HBRUSH hBrush;
    RECT Rect, Box;
    HDC hdc;
    INT Width, Height, Unit;
    ULONG i;

    if (!ConsoleDevice[0])
        return;
    /* FIXME: what is drawn once the console display is detached does not reach its scanout yet */
    hdc = CreateDCW(L"DISPLAY", ConsoleDevice, NULL, NULL);
    if (!hdc)
    {
        ERR("WL: Cannot draw on the console display %S: %lu\n", ConsoleDevice, GetLastError());
        return;
    }

    Width = GetDeviceCaps(hdc, HORZRES);
    Height = GetDeviceCaps(hdc, VERTRES);
    Unit = max(Height / 40, 8);
    SetRect(&Rect, 0, 0, Width, Height);
    hBrush = CreateSolidBrush(RGB(0x10, 0x24, 0x48));
    FillRect(hdc, &Rect, hBrush);
    DeleteObject(hBrush);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(0xFF, 0xFF, 0xFF));

    Rect.top = Height / 6;
    if (GetTimeFormatW(LOCALE_USER_DEFAULT, TIME_NOSECONDS, NULL, NULL, Text, ARRAYSIZE(Text)))
        DrawConsoleText(hdc, Text, Unit * 5, FW_LIGHT, &Rect);
    if (GetDateFormatW(LOCALE_USER_DEFAULT, DATE_LONGDATE, NULL, NULL, Text, ARRAYSIZE(Text)))
        DrawConsoleText(hdc, Text, Unit * 3 / 2, FW_NORMAL, &Rect);

    Rect.top = Height / 2;
    if (Session->UserName)
        DrawConsoleText(hdc, Session->UserName, Unit * 2, FW_SEMIBOLD, &Rect);
    if (LoadStringW(hAppInstance, IDS_REMOTE_CONSOLE_INUSE, Text, ARRAYSIZE(Text)))
        DrawConsoleText(hdc, Text, Unit, FW_NORMAL, &Rect);

    SetRect(&Box, Width / 2 - Unit * 8, Rect.top, Width / 2 + Unit * 8, Rect.top + Unit * 2);
    hBrush = CreateSolidBrush(RGB(0xFF, 0xFF, 0xFF));
    FillRect(hdc, &Box, hBrush);
    DeleteObject(hBrush);
    SetTextColor(hdc, RGB(0x20, 0x20, 0x20));
    for (i = 0; i < ConsolePasswordLength && i < ARRAYSIZE(Text) - 1; i++)
        Text[i] = 0x25CF;
    Text[i] = UNICODE_NULL;
    if (!i && LoadStringW(hAppInstance, IDS_REMOTE_CONSOLE_PASSWORD, Text, ARRAYSIZE(Text)))
        SetTextColor(hdc, RGB(0x70, 0x70, 0x70));
    Rect = Box;
    Rect.top += Unit / 2;
    DrawConsoleText(hdc, Text, Unit, FW_NORMAL, &Rect);

    if (ConsolePasswordWrong && LoadStringW(hAppInstance, IDS_REMOTE_CONSOLE_WRONG, Format, ARRAYSIZE(Format)))
    {
        SetTextColor(hdc, RGB(0xFF, 0xFF, 0xFF));
        SetRect(&Rect, 0, Box.bottom + Unit, Width, Height);
        DrawConsoleText(hdc, Format, Unit, FW_NORMAL, &Rect);
    }

    GdiFlush();
    DeleteDC(hdc);
}

static
VOID
ClearConsolePassword(VOID)
{
    SecureZeroMemory(ConsolePassword, sizeof(ConsolePassword));
    ConsolePasswordLength = 0;
}

static
BOOL
IsSessionUser(
    _In_ HANDLE Token,
    _In_ HANDLE SessionToken)
{
    UCHAR Buffer1[sizeof(TOKEN_USER) + SECURITY_MAX_SID_SIZE];
    UCHAR Buffer2[sizeof(TOKEN_USER) + SECURITY_MAX_SID_SIZE];
    DWORD Length;

    return SessionToken &&
           GetTokenInformation(Token, TokenUser, Buffer1, sizeof(Buffer1), &Length) &&
           GetTokenInformation(SessionToken, TokenUser, Buffer2, sizeof(Buffer2), &Length) &&
           EqualSid(((PTOKEN_USER)Buffer1)->User.Sid, ((PTOKEN_USER)Buffer2)->User.Sid);
}

static
BOOL
VerifyConsolePassword(
    _In_ PWLSESSION Session)
{
    static const CHAR SourceName[] = "User32  ";
    PMSV1_0_INTERACTIVE_LOGON AuthInfo;
    PVOID Profile = NULL;
    LSA_STRING Name;
    TOKEN_SOURCE Source;
    QUOTA_LIMITS Quotas;
    HANDLE Token = NULL;
    LUID LogonId;
    ULONG ProfileLength, AuthLength;
    ULONG UserLength, DomainLength, PasswordLength;
    LSA_OPERATIONAL_MODE Mode;
    NTSTATUS Status, SubStatus;
    PBYTE Ptr;
    BOOL bRet;

    if (!Session->UserName)
        return FALSE;

    if (!ConsoleLsa)
    {
        RtlInitAnsiString((PANSI_STRING)&Name, "Winlogon");
        Status = LsaRegisterLogonProcess(&Name, &ConsoleLsa, &Mode);
        if (NT_SUCCESS(Status))
        {
            RtlInitAnsiString((PANSI_STRING)&Name, MSV1_0_PACKAGE_NAME);
            Status = LsaLookupAuthenticationPackage(ConsoleLsa, &Name, &ConsolePackage);
        }
        if (!NT_SUCCESS(Status))
        {
            ERR("WL: Cannot connect to the LSA: 0x%08lx\n", Status);
            if (ConsoleLsa)
                LsaDeregisterLogonProcess(ConsoleLsa);
            ConsoleLsa = NULL;
            return FALSE;
        }
    }

    UserLength = (ULONG)wcslen(Session->UserName) * sizeof(WCHAR);
    DomainLength = Session->Domain ? (ULONG)wcslen(Session->Domain) * sizeof(WCHAR) : 0;
    PasswordLength = ConsolePasswordLength * sizeof(WCHAR);
    AuthLength = sizeof(*AuthInfo) + UserLength + DomainLength + PasswordLength;
    AuthInfo = RtlAllocateHeap(RtlGetProcessHeap(), HEAP_ZERO_MEMORY, AuthLength);
    if (!AuthInfo)
        return FALSE;

    AuthInfo->MessageType = MsV1_0WorkstationUnlockLogon;
    Ptr = (PBYTE)(AuthInfo + 1);
    AuthInfo->LogonDomainName.Length = AuthInfo->LogonDomainName.MaximumLength = (USHORT)DomainLength;
    AuthInfo->LogonDomainName.Buffer = (PWCHAR)Ptr;
    RtlCopyMemory(Ptr, Session->Domain, DomainLength);
    Ptr += DomainLength;
    AuthInfo->UserName.Length = AuthInfo->UserName.MaximumLength = (USHORT)UserLength;
    AuthInfo->UserName.Buffer = (PWCHAR)Ptr;
    RtlCopyMemory(Ptr, Session->UserName, UserLength);
    Ptr += UserLength;
    AuthInfo->Password.Length = AuthInfo->Password.MaximumLength = (USHORT)PasswordLength;
    AuthInfo->Password.Buffer = (PWCHAR)Ptr;
    RtlCopyMemory(Ptr, ConsolePassword, PasswordLength);

    RtlCopyMemory(Source.SourceName, SourceName, sizeof(Source.SourceName));
    AllocateLocallyUniqueId(&Source.SourceIdentifier);
    RtlInitAnsiString((PANSI_STRING)&Name, "Winlogon");
    Status = LsaLogonUser(ConsoleLsa, &Name, Unlock, ConsolePackage, AuthInfo, AuthLength, NULL, &Source,
                          &Profile, &ProfileLength, &LogonId, &Token, &Quotas, &SubStatus);
    SecureZeroMemory(AuthInfo, AuthLength);
    RtlFreeHeap(RtlGetProcessHeap(), 0, AuthInfo);
    if (Profile)
        LsaFreeReturnBuffer(Profile);
    if (!NT_SUCCESS(Status))
    {
        TRACE("WL: Console sign-in failed: 0x%08lx 0x%08lx\n", Status, SubStatus);
        return FALSE;
    }

    bRet = IsSessionUser(Token, Session->UserToken);
    CloseHandle(Token);
    return bRet;
}

static
BOOL
WriteRemoteMessage(
    _In_ PREMOTE_CONSOLE_MESSAGE Message)
{
    OVERLAPPED Overlapped = { 0 };
    DWORD Written = 0;
    BOOL bRet = FALSE;

    EnterCriticalSection(&RemotePipeLock);
    if (RemotePipe)
    {
        Overlapped.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
        if (Overlapped.hEvent)
        {
            bRet = WriteFile(RemotePipe, Message, sizeof(*Message), NULL, &Overlapped) ||
                   GetLastError() == ERROR_IO_PENDING;
            bRet = bRet && GetOverlappedResult(RemotePipe, &Overlapped, &Written, TRUE) &&
                   Written == sizeof(*Message);
            CloseHandle(Overlapped.hEvent);
        }
    }
    LeaveCriticalSection(&RemotePipeLock);
    return bRet;
}

static
VOID
SubmitConsolePassword(
    _In_ PWLSESSION Session)
{
    REMOTE_CONSOLE_MESSAGE Message = { 0 };

    if (RemoteTakeoverSent)
        return;

    ConsolePasswordWrong = !VerifyConsolePassword(Session);
    ClearConsolePassword();
    if (ConsolePasswordWrong)
    {
        DrawConsoleLock(Session);
        return;
    }

    Message.Code = REMOTE_CONSOLE_TAKEOVER;
    RemoteTakeoverSent = WriteRemoteMessage(&Message);
    if (!RemoteTakeoverSent)
        ERR("WL: Cannot ask the RDP server to disconnect its client\n");
}

VOID
HandleConsoleKey(
    _In_ PWLSESSION Session,
    _In_ USHORT MakeCode,
    _In_ USHORT Flags)
{
    WCHAR Chars[4];
    UINT Vk;
    INT Count;
    HKL Layout = GetKeyboardLayout(0);

    if (!RemoteConnected)
        return;

    Vk = MapVirtualKeyExW(MakeCode | ((Flags & KEY_E0) ? 0xE000 : 0), MAPVK_VSC_TO_VK_EX, Layout);
    if (!Vk || Vk >= ARRAYSIZE(ConsoleKeys))
        return;

    if (Flags & KEY_BREAK)
    {
        ConsoleKeys[Vk] &= ~0x80;
    }
    else
    {
        if (Vk == VK_CAPITAL && !(ConsoleKeys[Vk] & 0x80))
            ConsoleKeys[Vk] ^= 0x01;
        ConsoleKeys[Vk] |= 0x80;
    }
    ConsoleKeys[VK_SHIFT] = (ConsoleKeys[VK_LSHIFT] | ConsoleKeys[VK_RSHIFT]) & 0x80;
    ConsoleKeys[VK_CONTROL] = (ConsoleKeys[VK_LCONTROL] | ConsoleKeys[VK_RCONTROL]) & 0x80;
    ConsoleKeys[VK_MENU] = (ConsoleKeys[VK_LMENU] | ConsoleKeys[VK_RMENU]) & 0x80;
    if (Flags & KEY_BREAK)
        return;

    switch (Vk)
    {
        case VK_RETURN:
            SubmitConsolePassword(Session);
            return;
        case VK_BACK:
            if (ConsolePasswordLength)
                ConsolePassword[--ConsolePasswordLength] = UNICODE_NULL;
            break;
        case VK_ESCAPE:
            ClearConsolePassword();
            break;
        default:
            if ((ConsoleKeys[VK_CONTROL] & 0x80) && !(ConsoleKeys[VK_MENU] & 0x80))
                return;
            Count = ToUnicodeEx(Vk, MakeCode, ConsoleKeys, Chars, ARRAYSIZE(Chars), 0, Layout);
            if (Count != 1 || Chars[0] < L' ' || ConsolePasswordLength >= ARRAYSIZE(ConsolePassword) - 1)
                return;
            ConsolePassword[ConsolePasswordLength++] = Chars[0];
            break;
    }
    ConsolePasswordWrong = FALSE;
    DrawConsoleLock(Session);
}

VOID
HandleConsoleClock(
    _In_ PWLSESSION Session)
{
    if (RemoteConnected)
        DrawConsoleLock(Session);
}

static
BOOL
FindConsoleDevice(VOID)
{
    DISPLAY_DEVICEW Device;
    DWORD Index;

    for (Index = 0;; Index++)
    {
        Device.cb = sizeof(Device);
        if (!EnumDisplayDevicesW(NULL, Index, &Device, 0))
            return FALSE;
        if (Device.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE)
        {
            StringCchCopyW(ConsoleDevice, ARRAYSIZE(ConsoleDevice), Device.DeviceName);
            return TRUE;
        }
    }
}

static
DWORD
ConnectRemote(
    _In_ PWLSESSION Session,
    _In_ PREMOTE_CONSOLE_MESSAGE Message)
{
    BOOL bLocked;
    LONG lRet;

    if (RemoteConnected)
        return ERROR_ALREADY_EXISTS;
    if (Session->LogonState != STATE_LOGGED_ON && Session->LogonState != STATE_LOGGED_ON_SAS &&
        Session->LogonState != STATE_LOCKED && Session->LogonState != STATE_LOCKED_SAS)
    {
        /* TODO: connect a client to a session nobody is signed in to */
        return ERROR_NOT_SUPPORTED;
    }
    if (!Session->UserName || _wcsicmp(Session->UserName, Message->UserName) ||
        (Session->Domain && Message->Domain[0] && _wcsicmp(Session->Domain, Message->Domain)))
    {
        /* TODO: offer to sign out the user of the console, as Windows 11 does */
        return ERROR_ACCESS_DENIED;
    }
    if (!FindConsoleDevice())
        return ERROR_GEN_FAILURE;

    bLocked = Session->LogonState == STATE_LOCKED || Session->LogonState == STATE_LOCKED_SAS;
    if (!RemoteLeftSession)
    {
        NotifyWindows(WTS_CONSOLE_DISCONNECT);
        NotifyServices(WTS_CONSOLE_DISCONNECT, RemoteSessionId);
    }
    if (!bLocked)
        NotifyWindows(WTS_SESSION_LOCK);
    SetRemoteRecord(TRUE, TRUE, Message->Connection);
    NotifyWindows(WTS_REMOTE_CONNECT);
    NotifyServices(WTS_REMOTE_CONNECT, RemoteSessionId);
    if (!bLocked)
        NotifyServices(WTS_SESSION_LOCK, RemoteSessionId);

    RemoteLeftSession = FALSE;
    RemoteTakeoverSent = FALSE;
    ConsolePasswordWrong = FALSE;
    ClearConsolePassword();
    RtlZeroMemory(ConsoleKeys, sizeof(ConsoleKeys));
    SwitchDesktop(Session->WinlogonDesktop);
    DrawConsoleLock(Session);

    lRet = SetDisplayConfig(0, NULL, 0, NULL, SDC_APPLY | SDC_TOPOLOGY_EXTERNAL);
    if (lRet != ERROR_SUCCESS)
        ERR("WL: Cannot move the desktop to the remote display: %ld\n", lRet);

    RemoteConnected = TRUE;
    SetTimer(Session->SASWindow, CONSOLE_CLOCK_TIMER, 15000, NULL);
    if (!bLocked)
        SwitchDesktop(Session->ApplicationDesktop);

    if (bLocked)
    {
        CloseAllDialogWindows();
        CallNotificationDlls(Session, UnlockHandler);
        SwitchDesktop(Session->ApplicationDesktop);
        Session->LogonState = STATE_LOGGED_ON;
    }
    else
    {
        NotifyWindows(WTS_SESSION_UNLOCK);
    }
    NotifyServices(WTS_SESSION_UNLOCK, RemoteSessionId);
    return lRet;
}

static
DWORD
DisconnectRemote(
    _In_ PWLSESSION Session,
    _In_ BOOL TakenOver)
{
    BOOL bLocked;
    LONG lRet;

    if (!RemoteConnected)
        return ERROR_SUCCESS;

    KillTimer(Session->SASWindow, CONSOLE_CLOCK_TIMER);
    RemoteConnected = FALSE;
    RemoteTakeoverSent = FALSE;
    ClearConsolePassword();
    bLocked = Session->LogonState == STATE_LOCKED || Session->LogonState == STATE_LOCKED_SAS;

    SetRemoteRecord(TRUE, FALSE, 0);
    NotifyWindows(WTS_REMOTE_DISCONNECT);
    NotifyServices(WTS_REMOTE_DISCONNECT, RemoteSessionId);
    if (TakenOver)
    {
        if (!bLocked)
            NotifyWindows(WTS_SESSION_LOCK);
        NotifyServices(WTS_SESSION_LOCK, RemoteSessionId);
    }
    else if (!bLocked)
    {
        Session->LogonState = STATE_LOCKED;
        SwitchDesktop(Session->WinlogonDesktop);
        CloseAllDialogWindows();
        CallNotificationDlls(Session, LockHandler);
        NotifyServices(WTS_SESSION_LOCK, RemoteSessionId);
    }

    lRet = SetDisplayConfig(0, NULL, 0, NULL, SDC_APPLY | SDC_TOPOLOGY_INTERNAL);
    if (lRet != ERROR_SUCCESS)
        ERR("WL: Cannot move the desktop back to the console display: %ld\n", lRet);

    if (!TakenOver)
    {
        RemoteLeftSession = TRUE;
        PostMessageW(Session->SASWindow, WLX_WM_REMOTE_LOCKED, 0, 0);
        return lRet;
    }

    RemoteLeftSession = FALSE;
    SetRemoteRecord(FALSE, FALSE, 0);
    NotifyWindows(WTS_CONSOLE_CONNECT);
    NotifyServices(WTS_CONSOLE_CONNECT, RemoteSessionId);
    if (bLocked)
    {
        CloseAllDialogWindows();
        CallNotificationDlls(Session, UnlockHandler);
        SwitchDesktop(Session->ApplicationDesktop);
        Session->LogonState = STATE_LOGGED_ON;
    }
    else
    {
        NotifyWindows(WTS_SESSION_UNLOCK);
    }
    NotifyServices(WTS_SESSION_UNLOCK, RemoteSessionId);
    return lRet;
}

LRESULT
HandleRemoteConsoleMessage(
    _In_ PWLSESSION Session,
    _Inout_ PREMOTE_CONSOLE_MESSAGE Message)
{
    switch (Message->Code)
    {
        case REMOTE_CONSOLE_CONNECT:
            return ConnectRemote(Session, Message);
        case REMOTE_CONSOLE_DISCONNECT:
            return DisconnectRemote(Session, Message->TakenOver != 0);
        default:
            return ERROR_INVALID_FUNCTION;
    }
}

VOID
HandleConsoleSignIn(
    _In_ PWLSESSION Session)
{
    if (RemoteLeftSession && Session->LogonState == STATE_LOGGED_ON)
    {
        RemoteLeftSession = FALSE;
        SetRemoteRecord(FALSE, FALSE, 0);
        NotifyWindows(WTS_CONSOLE_CONNECT);
        NotifyServices(WTS_CONSOLE_CONNECT, RemoteSessionId);
        NotifyServices(WTS_SESSION_UNLOCK, RemoteSessionId);
    }
}

static
BOOL
ReadRemoteMessage(
    _In_ HANDLE Pipe,
    _Out_ PREMOTE_CONSOLE_MESSAGE Message)
{
    OVERLAPPED Overlapped = { 0 };
    DWORD Read = 0;
    BOOL bRet;

    Overlapped.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!Overlapped.hEvent)
        return FALSE;
    bRet = ReadFile(Pipe, Message, sizeof(*Message), NULL, &Overlapped) || GetLastError() == ERROR_IO_PENDING;
    bRet = bRet && GetOverlappedResult(Pipe, &Overlapped, &Read, TRUE) && Read == sizeof(*Message);
    CloseHandle(Overlapped.hEvent);
    return bRet;
}

static
DWORD
WINAPI
RemoteConsoleThread(
    _In_ PVOID Parameter)
{
    PWLSESSION Session = Parameter;
    REMOTE_CONSOLE_MESSAGE Message;
    SECURITY_ATTRIBUTES Attributes = { sizeof(Attributes), NULL, FALSE };
    OVERLAPPED Overlapped = { 0 };
    WCHAR PipeName[64];
    HANDLE Pipe;
    DWORD Bytes;
    BOOL bConnected;

    StringCchPrintfW(PipeName, ARRAYSIZE(PipeName), REMOTE_CONSOLE_PIPE_FORMAT, RemoteSessionId);
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;;GA;;;SY)", SDDL_REVISION_1,
                                                              &Attributes.lpSecurityDescriptor, NULL))
    {
        return GetLastError();
    }
    Overlapped.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!Overlapped.hEvent)
        return GetLastError();

    for (;;)
    {
        Pipe = CreateNamedPipeW(PipeName, PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
                                PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
                                1, sizeof(Message), sizeof(Message), 0, &Attributes);
        if (Pipe == INVALID_HANDLE_VALUE)
        {
            ERR("WL: Cannot create %S: %lu\n", PipeName, GetLastError());
            break;
        }

        ResetEvent(Overlapped.hEvent);
        bConnected = ConnectNamedPipe(Pipe, &Overlapped) || GetLastError() == ERROR_PIPE_CONNECTED ||
                     (GetLastError() == ERROR_IO_PENDING && GetOverlappedResult(Pipe, &Overlapped, &Bytes, TRUE));
        if (bConnected)
        {
            EnterCriticalSection(&RemotePipeLock);
            RemotePipe = Pipe;
            LeaveCriticalSection(&RemotePipeLock);
            while (ReadRemoteMessage(Pipe, &Message))
            {
                Message.Status = (ULONG)SendMessageW(Session->SASWindow, WLX_WM_REMOTE_CONSOLE, 0, (LPARAM)&Message);
                if (!WriteRemoteMessage(&Message))
                    break;
            }
            EnterCriticalSection(&RemotePipeLock);
            RemotePipe = NULL;
            LeaveCriticalSection(&RemotePipeLock);
            RtlZeroMemory(&Message, sizeof(Message));
            Message.Code = REMOTE_CONSOLE_DISCONNECT;
            SendMessageW(Session->SASWindow, WLX_WM_REMOTE_CONSOLE, 0, (LPARAM)&Message);
            DisconnectNamedPipe(Pipe);
        }
        CloseHandle(Pipe);
    }

    CloseHandle(Overlapped.hEvent);
    LocalFree(Attributes.lpSecurityDescriptor);
    return 0;
}

BOOL
StartRemoteConsole(
    _In_ PWLSESSION Session)
{
    HANDLE hThread;

    if (!ProcessIdToSessionId(GetCurrentProcessId(), &RemoteSessionId) || RemoteSessionId == 0)
        return FALSE;

    InitializeCriticalSection(&RemotePipeLock);
    SetRemoteRecord(FALSE, FALSE, 0);
    hThread = CreateThread(NULL, 0, RemoteConsoleThread, Session, 0, NULL);
    if (!hThread)
        return FALSE;
    CloseHandle(hThread);
    return TRUE;
}
