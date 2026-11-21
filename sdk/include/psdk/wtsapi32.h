/*
 * Copyright 2005 Ulrich Czekalla (For CodeWeavers)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#ifndef __WINE_WTSAPI32_H
#define __WINE_WTSAPI32_H

#ifdef __cplusplus
extern "C" {
#endif


typedef enum _WTS_VIRTUAL_CLASS
{
    WTSVirtualClientData,
    WTSVirtualFileHandle
} WTS_VIRTUAL_CLASS;

typedef enum tagWTS_INFO_CLASS
{
    WTSInitialProgram,
    WTSApplicationName,
    WTSWorkingDirectory,
    WTSOEMId,
    WTSSessionId,
    WTSUserName,
    WTSWinStationName,
    WTSDomainName,
    WTSConnectState,
    WTSClientBuildNumber,
    WTSClientName,
    WTSClientDirectory,
    WTSClientProductId,
    WTSClientHardwareId,
    WTSClientAddress,
    WTSClientDisplay,
    WTSClientProtocolType,
    WTSIdleTime,
    WTSLogonTime,
    WTSIncomingBytes,
    WTSOutgoingBytes,
    WTSIncomingFrames,
    WTSOutgoingFrames,
    WTSClientInfo,
    WTSSessionInfo,
    WTSSessionInfoEx,
    WTSConfigInfo,
    WTSValidationInfo,
    WTSSessionAddressV4,
    WTSIsRemoteSession
} WTS_INFO_CLASS;

typedef enum _WTS_CONNECTSTATE_CLASS
{
    WTSActive,
    WTSConnected,
    WTSConnectQuery,
    WTSShadow,
    WTSDisconnected,
    WTSIdle,
    WTSListen,
    WTSReset,
    WTSDown,
    WTSInit
} WTS_CONNECTSTATE_CLASS;

typedef enum _WTS_CONFIG_CLASS
{
    WTSUserConfigInitialProgram,
    WTSUserConfigWorkingDirectory,
    WTSUserConfigfInheritInitialProgram,
    WTSUserConfigfAllowLogonTerminalServer,
    WTSUserConfigTimeoutSettingsConnections,
    WTSUserConfigTimeoutSettingsDisconnections,
    WTSUserConfigTimeoutSettingsIdle,
    WTSUserConfigfDeviceClientDrives,
    WTSUserConfigfDeviceClientPrinters,
    WTSUserConfigfDeviceClientDefaultPrinter,
    WTSUserConfigBrokenTimeoutSettings,
    WTSUserConfigReconnectSettings,
    WTSUserConfigModemCallbackSettings,
    WTSUserConfigModemCallbackPhoneNumber,
    WTSUserConfigShadowingSettings,
    WTSUserConfigTerminalServerProfilePath,
    WTSUserConfigTerminalServerHomeDir,
    WTSUserConfigTerminalServerHomeDirDrive,
    WTSUserConfigfTerminalServerRemoteHomeDir,
    WTSUserConfigUser
} WTS_CONFIG_CLASS;

typedef enum _WTS_TYPE_CLASS
{
    WTSTypeProcessInfoLevel0,
    WTSTypeProcessInfoLevel1,
    WTSTypeSessionInfoLevel1
} WTS_TYPE_CLASS;

typedef struct _WTS_PROCESS_INFOA
{
    DWORD SessionId;
    DWORD ProcessId;
    LPSTR pProcessName;
    PSID pUserSid;
} WTS_PROCESS_INFOA, *PWTS_PROCESS_INFOA;

typedef struct _WTS_PROCESS_INFOW
{
    DWORD SessionId;
    DWORD ProcessId;
    LPWSTR pProcessName;
    PSID pUserSid;
} WTS_PROCESS_INFOW, *PWTS_PROCESS_INFOW;

DECL_WINELIB_TYPE_AW(WTS_PROCESS_INFO)
DECL_WINELIB_TYPE_AW(PWTS_PROCESS_INFO)

typedef struct _WTS_SESSION_INFOA
{
    DWORD SessionId;
    LPSTR pWinStationName;
    WTS_CONNECTSTATE_CLASS State;
} WTS_SESSION_INFOA, *PWTS_SESSION_INFOA;

typedef struct _WTS_SESSION_INFOW
{
    DWORD SessionId;
    LPWSTR pWinStationName;
    WTS_CONNECTSTATE_CLASS State;
} WTS_SESSION_INFOW, *PWTS_SESSION_INFOW;

DECL_WINELIB_TYPE_AW(WTS_SESSION_INFO)
DECL_WINELIB_TYPE_AW(PWTS_SESSION_INFO)

typedef struct _WTS_SESSION_INFO_1A
{
    DWORD ExecEnvId;
    WTS_CONNECTSTATE_CLASS State;
    DWORD SessionId;
    char *pSessionName;
    char *pHostName;
    char *pUserName;
    char *pDomainName;
    char *pFarmName;
} WTS_SESSION_INFO_1A, *PWTS_SESSION_INFO_1A;

typedef struct _WTS_SESSION_INFO_1W
{
    DWORD ExecEnvId;
    WTS_CONNECTSTATE_CLASS State;
    DWORD SessionId;
    WCHAR *pSessionName;
    WCHAR *pHostName;
    WCHAR *pUserName;
    WCHAR *pDomainName;
    WCHAR *pFarmName;
} WTS_SESSION_INFO_1W, *PWTS_SESSION_INFO_1W;

DECL_WINELIB_TYPE_AW(WTS_SESSION_INFO_1)
DECL_WINELIB_TYPE_AW(PWTS_SESSION_INFO_1)

typedef struct _WTS_SERVER_INFOA
{
    LPSTR pServerName;
} WTS_SERVER_INFOA, *PWTS_SERVER_INFOA;

typedef struct _WTS_SERVER_INFOW
{
    LPWSTR pServerName;
} WTS_SERVER_INFOW, *PWTS_SERVER_INFOW;

DECL_WINELIB_TYPE_AW(WTS_SERVER_INFO)
DECL_WINELIB_TYPE_AW(PWTS_SERVER_INFO)

#define WINSTATIONNAME_LENGTH 32
#define USERNAME_LENGTH 20
#define DOMAIN_LENGTH 17

typedef struct _WTSINFOA {
  WTS_CONNECTSTATE_CLASS State;
  DWORD                  SessionId;
  DWORD                  IncomingBytes;
  DWORD                  OutgoingBytes;
  DWORD                  IncomingFrames;
  DWORD                  OutgoingFrames;
  DWORD                  IncomingCompressedBytes;
  DWORD                  OutgoingCompressedBytes;
  CHAR                   WinStationName[WINSTATIONNAME_LENGTH];
  CHAR                   Domain[DOMAIN_LENGTH];
  CHAR                   UserName[USERNAME_LENGTH + 1];
  LARGE_INTEGER          ConnectTime;
  LARGE_INTEGER          DisconnectTime;
  LARGE_INTEGER          LastInputTime;
  LARGE_INTEGER          LogonTime;
  LARGE_INTEGER          CurrentTime;
} WTSINFOA, *PWTSINFOA;

typedef struct _WTSINFOW {
  WTS_CONNECTSTATE_CLASS State;
  DWORD                  SessionId;
  DWORD                  IncomingBytes;
  DWORD                  OutgoingBytes;
  DWORD                  IncomingFrames;
  DWORD                  OutgoingFrames;
  DWORD                  IncomingCompressedBytes;
  DWORD                  OutgoingCompressedBytes;
  WCHAR                  WinStationName[WINSTATIONNAME_LENGTH];
  WCHAR                  Domain[DOMAIN_LENGTH];
  WCHAR                  UserName[USERNAME_LENGTH + 1];
  LARGE_INTEGER          ConnectTime;
  LARGE_INTEGER          DisconnectTime;
  LARGE_INTEGER          LastInputTime;
  LARGE_INTEGER          LogonTime;
  LARGE_INTEGER          CurrentTime;
} WTSINFOW, *PWTSINFOW;

DECL_WINELIB_TYPE_AW(WTSINFO)
DECL_WINELIB_TYPE_AW(PWTSINFO)

#define WTS_CURRENT_SERVER_HANDLE ((HANDLE)NULL)
#define WTS_CURRENT_SESSION (~0u)
#define WTS_ANY_SESSION ((DWORD)-2)

void WINAPI WTSCloseServer(HANDLE);
BOOL WINAPI WTSConnectSessionA(ULONG, ULONG, PSTR, BOOL);
BOOL WINAPI WTSConnectSessionW(ULONG, ULONG, PWSTR, BOOL);
#define     WTSConnectSession WINELIB_NAME_AW(WTSConnectSession)
BOOL WINAPI WTSDisconnectSession(HANDLE, DWORD, BOOL);
BOOL WINAPI WTSEnableChildSessions(BOOL);
BOOL WINAPI WTSEnumerateProcessesA(HANDLE, DWORD, DWORD, PWTS_PROCESS_INFOA *, DWORD *);
BOOL WINAPI WTSEnumerateProcessesW(HANDLE, DWORD, DWORD, PWTS_PROCESS_INFOW *, DWORD *);
#define     WTSEnumerateProcesses WINELIB_NAME_AW(WTSEnumerateProcesses)
BOOL WINAPI WTSEnumerateServersA( LPSTR, DWORD, DWORD, PWTS_SERVER_INFOA*, DWORD*);
BOOL WINAPI WTSEnumerateServersW( LPWSTR, DWORD, DWORD, PWTS_SERVER_INFOW*, DWORD*);
#define     WTSEnumerateServers WINELIB_NAME_AW(WTSEnumerateServers)
BOOL WINAPI WTSEnumerateSessionsA(HANDLE, DWORD, DWORD, PWTS_SESSION_INFOA *, DWORD *);
BOOL WINAPI WTSEnumerateSessionsW(HANDLE, DWORD, DWORD, PWTS_SESSION_INFOW *, DWORD *);
#define     WTSEnumerateSessions WINELIB_NAME_AW(WTSEnumerateSessions)
void WINAPI WTSFreeMemory(PVOID);
HANDLE WINAPI WTSOpenServerA(LPSTR);
HANDLE WINAPI WTSOpenServerW(LPWSTR);
#define     WTSOpenServer WINELIB_NAME_AW(WTSOpenServer)
BOOL WINAPI WTSQuerySessionInformationA(HANDLE, DWORD, WTS_INFO_CLASS, LPSTR *, DWORD *);
BOOL WINAPI WTSQuerySessionInformationW(HANDLE, DWORD, WTS_INFO_CLASS, LPWSTR *, DWORD *);
#define     WTSQuerySessionInformation WINELIB_NAME_AW(WTSQuerySessionInformation)
BOOL WINAPI WTSQueryUserConfigA(LPSTR,LPSTR,WTS_CONFIG_CLASS,LPSTR*,DWORD*);
BOOL WINAPI WTSQueryUserConfigW(LPWSTR,LPWSTR,WTS_CONFIG_CLASS,LPWSTR*,DWORD*);
#define     WTSQueryUserConfig WINELIB_NAME_AW(WTSQueryUserConfig)
BOOL WINAPI WTSQueryUserToken(ULONG, PHANDLE);
#define NOTIFY_FOR_THIS_SESSION 0
#define NOTIFY_FOR_ALL_SESSIONS  1

BOOL WINAPI WTSRegisterSessionNotification(HWND, DWORD);
BOOL WINAPI WTSRegisterSessionNotificationEx(HANDLE, HWND, DWORD);
BOOL WINAPI WTSStartRemoteControlSessionA(LPSTR, ULONG, BYTE, USHORT);
BOOL WINAPI WTSStartRemoteControlSessionW(LPWSTR, ULONG, BYTE, USHORT);
#define     WTSStartRemoteControlSession WINELIB_NAME_AW(WTSStartRemoteControlSession)
BOOL WINAPI WTSStopRemoteControlSession(ULONG);
BOOL WINAPI WTSTerminateProcess(HANDLE, DWORD, DWORD);
BOOL WINAPI WTSUnRegisterSessionNotification(HWND);
BOOL WINAPI WTSUnRegisterSessionNotificationEx(HANDLE, HWND);
BOOL WINAPI WTSWaitSystemEvent(HANDLE, DWORD, DWORD*);

#ifdef __cplusplus
}
#endif

#ifndef CLIENTADDRESS_LENGTH
#define CLIENTADDRESS_LENGTH 30
#endif
#ifndef CLIENTNAME_LENGTH
#define CLIENTNAME_LENGTH 20
#endif
#ifndef IDASYNC
#define IDASYNC 32001
#endif
#ifndef MAX_DATE_TIME_LENGTH
#define MAX_DATE_TIME_LENGTH 56
#endif
#ifndef MAX_ELAPSED_TIME_LENGTH
#define MAX_ELAPSED_TIME_LENGTH 15
#endif
#ifndef PRODUCTINFO_COMPANYNAME_LENGTH
#define PRODUCTINFO_COMPANYNAME_LENGTH 256
#endif
#ifndef PRODUCTINFO_PRODUCTID_LENGTH
#define PRODUCTINFO_PRODUCTID_LENGTH 4
#endif
#ifndef REMOTECONTROL_KBDALT_HOTKEY
#define REMOTECONTROL_KBDALT_HOTKEY 0x4
#endif
#ifndef REMOTECONTROL_KBDCTRL_HOTKEY
#define REMOTECONTROL_KBDCTRL_HOTKEY 0x2
#endif
#ifndef REMOTECONTROL_KBDSHIFT_HOTKEY
#define REMOTECONTROL_KBDSHIFT_HOTKEY 0x1
#endif
#ifndef VALIDATIONINFORMATION_HARDWAREID_LENGTH
#define VALIDATIONINFORMATION_HARDWAREID_LENGTH 20
#endif
#ifndef VALIDATIONINFORMATION_LICENSE_LENGTH
#define VALIDATIONINFORMATION_LICENSE_LENGTH 16384
#endif
#ifndef WTS_CHANNEL_OPTION_DYNAMIC_NO_COMPRESS
#define WTS_CHANNEL_OPTION_DYNAMIC_NO_COMPRESS 0x00000008
#endif
#ifndef WTS_CHANNEL_OPTION_DYNAMIC_PRI_HIGH
#define WTS_CHANNEL_OPTION_DYNAMIC_PRI_HIGH 0x00000004
#endif
#ifndef WTS_CHANNEL_OPTION_DYNAMIC_PRI_LOW
#define WTS_CHANNEL_OPTION_DYNAMIC_PRI_LOW 0x00000000
#endif
#ifndef WTS_CHANNEL_OPTION_DYNAMIC_PRI_MED
#define WTS_CHANNEL_OPTION_DYNAMIC_PRI_MED 0x00000002
#endif
#ifndef WTS_CHANNEL_OPTION_DYNAMIC_PRI_REAL
#define WTS_CHANNEL_OPTION_DYNAMIC_PRI_REAL 0x00000006
#endif
#ifndef WTS_COMMENT_LENGTH
#define WTS_COMMENT_LENGTH 60
#endif
#ifndef WTS_DRIVE_LENGTH
#define WTS_DRIVE_LENGTH 3
#endif
#ifndef WTS_EVENT_ALL
#define WTS_EVENT_ALL 0x7fffffff
#endif
#ifndef WTS_EVENT_CONNECT
#define WTS_EVENT_CONNECT 0x00000008
#endif
#ifndef WTS_EVENT_CREATE
#define WTS_EVENT_CREATE 0x00000001
#endif
#ifndef WTS_EVENT_DELETE
#define WTS_EVENT_DELETE 0x00000002
#endif
#ifndef WTS_EVENT_DISCONNECT
#define WTS_EVENT_DISCONNECT 0x00000010
#endif
#ifndef WTS_EVENT_FLUSH
#define WTS_EVENT_FLUSH 0x80000000
#endif
#ifndef WTS_EVENT_LICENSE
#define WTS_EVENT_LICENSE 0x00000100
#endif
#ifndef WTS_EVENT_LOGOFF
#define WTS_EVENT_LOGOFF 0x00000040
#endif
#ifndef WTS_EVENT_LOGON
#define WTS_EVENT_LOGON 0x00000020
#endif
#ifndef WTS_EVENT_RENAME
#define WTS_EVENT_RENAME 0x00000004
#endif
#ifndef WTS_EVENT_STATECHANGE
#define WTS_EVENT_STATECHANGE 0x00000080
#endif
#ifndef WTS_LISTENER_CREATE
#define WTS_LISTENER_CREATE 0x00000001
#endif
#ifndef WTS_LISTENER_NAME_LENGTH
#define WTS_LISTENER_NAME_LENGTH 32
#endif
#ifndef WTS_LISTENER_UPDATE
#define WTS_LISTENER_UPDATE 0x00000010
#endif
#ifndef WTS_PROCESS_INFO_LEVEL_0
#define WTS_PROCESS_INFO_LEVEL_0 0
#endif
#ifndef WTS_PROCESS_INFO_LEVEL_1
#define WTS_PROCESS_INFO_LEVEL_1 1
#endif
#ifndef WTS_PROTOCOL_TYPE_ICA
#define WTS_PROTOCOL_TYPE_ICA 1
#endif
#ifndef WTS_PROTOCOL_TYPE_RDP
#define WTS_PROTOCOL_TYPE_RDP 2
#endif
#ifndef WTS_SECURITY_CONNECT
#define WTS_SECURITY_CONNECT 0x00000100
#endif
#ifndef WTS_SECURITY_DISCONNECT
#define WTS_SECURITY_DISCONNECT 0x00000200
#endif
#ifndef WTS_SECURITY_LOGOFF
#define WTS_SECURITY_LOGOFF 0x00000040
#endif
#ifndef WTS_SECURITY_LOGON
#define WTS_SECURITY_LOGON 0x00000020
#endif
#ifndef WTS_SECURITY_MESSAGE
#define WTS_SECURITY_MESSAGE 0x00000080
#endif
#ifndef WTS_SECURITY_QUERY_INFORMATION
#define WTS_SECURITY_QUERY_INFORMATION 0x00000001
#endif
#ifndef WTS_SECURITY_REMOTE_CONTROL
#define WTS_SECURITY_REMOTE_CONTROL 0x00000010
#endif
#ifndef WTS_SECURITY_RESET
#define WTS_SECURITY_RESET 0x00000004
#endif
#ifndef WTS_SECURITY_SET_INFORMATION
#define WTS_SECURITY_SET_INFORMATION 0x00000002
#endif
#ifndef WTS_SECURITY_VIRTUAL_CHANNELS
#define WTS_SECURITY_VIRTUAL_CHANNELS 0x00000008
#endif
#ifndef WTS_SESSIONSTATE_LOCK
#define WTS_SESSIONSTATE_LOCK 0x00000000
#endif
#ifndef WTS_SESSIONSTATE_UNKNOWN
#define WTS_SESSIONSTATE_UNKNOWN 0xFFFFFFFF
#endif
#ifndef WTS_SESSIONSTATE_UNLOCK
#define WTS_SESSIONSTATE_UNLOCK 0x00000001
#endif
#ifndef WTS_WSD_FASTREBOOT
#define WTS_WSD_FASTREBOOT 0x00000010
#endif
#ifndef WTS_WSD_LOGOFF
#define WTS_WSD_LOGOFF 0x00000001
#endif
#ifndef WTS_WSD_POWEROFF
#define WTS_WSD_POWEROFF 0x00000008
#endif
#ifndef WTS_WSD_REBOOT
#define WTS_WSD_REBOOT 0x00000004
#endif
#ifndef WTS_WSD_SHUTDOWN
#define WTS_WSD_SHUTDOWN 0x00000002
#endif

#endif
