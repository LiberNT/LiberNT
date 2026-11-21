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
    WTSTypeSessionInfoLevel1,
    WTSTypeCloudAuthServerNonce = 3,
    WTSTypeSerializedUserCredential = 4,
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

typedef enum _WTS_CONFIG_SOURCE {
    WTSUserConfigSourceSAM = 0
} WTS_CONFIG_SOURCE;

#ifdef __cplusplus
extern "C" {
#endif
HANDLE WINAPI WTSOpenServerExA(LPSTR pServerName);
HANDLE WINAPI WTSOpenServerExW(LPWSTR pServerName);
BOOL WINAPI WTSVirtualChannelPurgeInput(HANDLE hChannelHandle);
BOOL WINAPI WTSVirtualChannelPurgeOutput(HANDLE hChannelHandle);
#ifdef __cplusplus
}
#endif

typedef struct _WTS_CLIENT_ADDRESS {
  DWORD AddressFamily;
  BYTE Address[20];
} WTS_CLIENT_ADDRESS, *PWTS_CLIENT_ADDRESS;

typedef struct _WTS_CLIENT_DISPLAY {
  DWORD HorizontalResolution;
  DWORD VerticalResolution;
  DWORD ColorDepth;
} WTS_CLIENT_DISPLAY, *PWTS_CLIENT_DISPLAY;

typedef struct _WTS_PROCESS_INFO_EXA {
  DWORD SessionId;
  DWORD ProcessId;
  LPSTR pProcessName;
  PSID pUserSid;
  DWORD NumberOfThreads;
  DWORD HandleCount;
  DWORD PagefileUsage;
  DWORD PeakPagefileUsage;
  DWORD WorkingSetSize;
  DWORD PeakWorkingSetSize;
  LARGE_INTEGER UserTime;
  LARGE_INTEGER KernelTime;
} WTS_PROCESS_INFO_EXA, *PWTS_PROCESS_INFO_EXA;

typedef struct _WTS_PROCESS_INFO_EXW {
  DWORD SessionId;
  DWORD ProcessId;
  LPWSTR pProcessName;
  PSID pUserSid;
  DWORD NumberOfThreads;
  DWORD HandleCount;
  DWORD PagefileUsage;
  DWORD PeakPagefileUsage;
  DWORD WorkingSetSize;
  DWORD PeakWorkingSetSize;
  LARGE_INTEGER UserTime;
  LARGE_INTEGER KernelTime;
} WTS_PROCESS_INFO_EXW, *PWTS_PROCESS_INFO_EXW;

typedef struct _WTS_PRODUCT_INFOA {
  CHAR CompanyName[256];
  CHAR ProductID[4];
} PRODUCT_INFOA;

typedef struct _WTS_PRODUCT_INFOW {
  WCHAR CompanyName[256];
  WCHAR ProductID[4];
} PRODUCT_INFOW;

typedef struct _WTS_SERIALIZED_USER_CREDENTIAL {
  ULONG SerializationLength;
  BYTE * Serialization;
} WTS_SERIALIZED_USER_CREDENTIAL, *PWTS_SERIALIZED_USER_CREDENTIAL, WRDS_SERIALIZED_USER_CREDENTIAL, *PWRDS_SERIALIZED_USER_CREDENTIAL;

typedef struct _WTS_SESSION_ADDRESS {
  DWORD AddressFamily;
  BYTE Address[20];
} WTS_SESSION_ADDRESS, *PWTS_SESSION_ADDRESS;

typedef struct _WTS_VALIDATION_INFORMATIONA {
  PRODUCT_INFOA ProductInfo;
  BYTE License[16384];
  DWORD LicenseLength;
  BYTE HardwareID[20];
  DWORD HardwareIDLength;
} WTS_VALIDATION_INFORMATIONA, *PWTS_VALIDATION_INFORMATIONA;

typedef struct _WTS_VALIDATION_INFORMATIONW {
  PRODUCT_INFOW ProductInfo;
  BYTE License[16384];
  DWORD LicenseLength;
  BYTE HardwareID[20];
  DWORD HardwareIDLength;
} WTS_VALIDATION_INFORMATIONW, *PWTS_VALIDATION_INFORMATIONW;

typedef struct _WTSCLIENTA {
  CHAR ClientName[21];
  CHAR Domain[18];
  CHAR UserName[21];
  CHAR WorkDirectory[261];
  CHAR InitialProgram[261];
  BYTE EncryptionLevel;
  ULONG ClientAddressFamily;
  USHORT ClientAddress[31];
  USHORT HRes;
  USHORT VRes;
  USHORT ColorDepth;
  CHAR ClientDirectory[261];
  ULONG ClientBuildNumber;
  ULONG ClientHardwareId;
  USHORT ClientProductId;
  USHORT OutBufCountHost;
  USHORT OutBufCountClient;
  USHORT OutBufLength;
  CHAR DeviceId[261];
} WTSCLIENTA, *PWTSCLIENTA;

typedef struct _WTSCLIENTW {
  WCHAR ClientName[21];
  WCHAR Domain[18];
  WCHAR UserName[21];
  WCHAR WorkDirectory[261];
  WCHAR InitialProgram[261];
  BYTE EncryptionLevel;
  ULONG ClientAddressFamily;
  USHORT ClientAddress[31];
  USHORT HRes;
  USHORT VRes;
  USHORT ColorDepth;
  WCHAR ClientDirectory[261];
  ULONG ClientBuildNumber;
  ULONG ClientHardwareId;
  USHORT ClientProductId;
  USHORT OutBufCountHost;
  USHORT OutBufCountClient;
  USHORT OutBufLength;
  WCHAR DeviceId[261];
} WTSCLIENTW, *PWTSCLIENTW;

typedef struct _WTSCONFIGINFOA {
  ULONG version;
  ULONG fConnectClientDrivesAtLogon;
  ULONG fConnectPrinterAtLogon;
  ULONG fDisablePrinterRedirection;
  ULONG fDisableDefaultMainClientPrinter;
  ULONG ShadowSettings;
  CHAR LogonUserName[21];
  CHAR LogonDomain[18];
  CHAR WorkDirectory[261];
  CHAR InitialProgram[261];
  CHAR ApplicationName[261];
} WTSCONFIGINFOA, *PWTSCONFIGINFOA;

typedef struct _WTSCONFIGINFOW {
  ULONG version;
  ULONG fConnectClientDrivesAtLogon;
  ULONG fConnectPrinterAtLogon;
  ULONG fDisablePrinterRedirection;
  ULONG fDisableDefaultMainClientPrinter;
  ULONG ShadowSettings;
  WCHAR LogonUserName[21];
  WCHAR LogonDomain[18];
  WCHAR WorkDirectory[261];
  WCHAR InitialProgram[261];
  WCHAR ApplicationName[261];
} WTSCONFIGINFOW, *PWTSCONFIGINFOW;

typedef struct _WTSINFOEX_LEVEL1_A {
  ULONG SessionId;
  WTS_CONNECTSTATE_CLASS SessionState;
  LONG SessionFlags;
  CHAR WinStationName[33];
  CHAR UserName[21];
  CHAR DomainName[18];
  LARGE_INTEGER LogonTime;
  LARGE_INTEGER ConnectTime;
  LARGE_INTEGER DisconnectTime;
  LARGE_INTEGER LastInputTime;
  LARGE_INTEGER CurrentTime;
  DWORD IncomingBytes;
  DWORD OutgoingBytes;
  DWORD IncomingFrames;
  DWORD OutgoingFrames;
  DWORD IncomingCompressedBytes;
  DWORD OutgoingCompressedBytes;
} WTSINFOEX_LEVEL1_A, *PWTSINFOEX_LEVEL1_A;

typedef struct _WTSINFOEX_LEVEL1_W {
  ULONG SessionId;
  WTS_CONNECTSTATE_CLASS SessionState;
  LONG SessionFlags;
  WCHAR WinStationName[33];
  WCHAR UserName[21];
  WCHAR DomainName[18];
  LARGE_INTEGER LogonTime;
  LARGE_INTEGER ConnectTime;
  LARGE_INTEGER DisconnectTime;
  LARGE_INTEGER LastInputTime;
  LARGE_INTEGER CurrentTime;
  DWORD IncomingBytes;
  DWORD OutgoingBytes;
  DWORD IncomingFrames;
  DWORD OutgoingFrames;
  DWORD IncomingCompressedBytes;
  DWORD OutgoingCompressedBytes;
} WTSINFOEX_LEVEL1_W, *PWTSINFOEX_LEVEL1_W;

typedef union _WTSINFOEX_LEVEL_A {
  WTSINFOEX_LEVEL1_A WTSInfoExLevel1;
} WTSINFOEX_LEVEL_A, *PWTSINFOEX_LEVEL_A;

typedef union _WTSINFOEX_LEVEL_W {
  WTSINFOEX_LEVEL1_W WTSInfoExLevel1;
} WTSINFOEX_LEVEL_W, *PWTSINFOEX_LEVEL_W;

typedef struct _WTSINFOEXA {
  DWORD Level;
  WTSINFOEX_LEVEL_A Data;
} WTSINFOEXA, *PWTSINFOEXA;

typedef struct _WTSINFOEXW {
  DWORD Level;
  WTSINFOEX_LEVEL_W Data;
} WTSINFOEXW, *PWTSINFOEXW;

typedef struct _WTSLISTENERCONFIGA {
  ULONG version;
  ULONG fEnableListener;
  ULONG MaxConnectionCount;
  ULONG fPromptForPassword;
  ULONG fInheritColorDepth;
  ULONG ColorDepth;
  ULONG fInheritBrokenTimeoutSettings;
  ULONG BrokenTimeoutSettings;
  ULONG fDisablePrinterRedirection;
  ULONG fDisableDriveRedirection;
  ULONG fDisableComPortRedirection;
  ULONG fDisableLPTPortRedirection;
  ULONG fDisableClipboardRedirection;
  ULONG fDisableAudioRedirection;
  ULONG fDisablePNPRedirection;
  ULONG fDisableDefaultMainClientPrinter;
  ULONG LanAdapter;
  ULONG PortNumber;
  ULONG fInheritShadowSettings;
  ULONG ShadowSettings;
  ULONG TimeoutSettingsConnection;
  ULONG TimeoutSettingsDisconnection;
  ULONG TimeoutSettingsIdle;
  ULONG SecurityLayer;
  ULONG MinEncryptionLevel;
  ULONG UserAuthentication;
  CHAR Comment[61];
  CHAR LogonUserName[21];
  CHAR LogonDomain[18];
  CHAR WorkDirectory[261];
  CHAR InitialProgram[261];
} WTSLISTENERCONFIGA, *PWTSLISTENERCONFIGA;

typedef struct _WTSLISTENERCONFIGW {
  ULONG version;
  ULONG fEnableListener;
  ULONG MaxConnectionCount;
  ULONG fPromptForPassword;
  ULONG fInheritColorDepth;
  ULONG ColorDepth;
  ULONG fInheritBrokenTimeoutSettings;
  ULONG BrokenTimeoutSettings;
  ULONG fDisablePrinterRedirection;
  ULONG fDisableDriveRedirection;
  ULONG fDisableComPortRedirection;
  ULONG fDisableLPTPortRedirection;
  ULONG fDisableClipboardRedirection;
  ULONG fDisableAudioRedirection;
  ULONG fDisablePNPRedirection;
  ULONG fDisableDefaultMainClientPrinter;
  ULONG LanAdapter;
  ULONG PortNumber;
  ULONG fInheritShadowSettings;
  ULONG ShadowSettings;
  ULONG TimeoutSettingsConnection;
  ULONG TimeoutSettingsDisconnection;
  ULONG TimeoutSettingsIdle;
  ULONG SecurityLayer;
  ULONG MinEncryptionLevel;
  ULONG UserAuthentication;
  WCHAR Comment[61];
  WCHAR LogonUserName[21];
  WCHAR LogonDomain[18];
  WCHAR WorkDirectory[261];
  WCHAR InitialProgram[261];
} WTSLISTENERCONFIGW, *PWTSLISTENERCONFIGW;

typedef struct _WTSUSERCONFIGA {
  DWORD Source;
  DWORD InheritInitialProgram;
  DWORD AllowLogonTerminalServer;
  DWORD TimeoutSettingsConnections;
  DWORD TimeoutSettingsDisconnections;
  DWORD TimeoutSettingsIdle;
  DWORD DeviceClientDrives;
  DWORD DeviceClientPrinters;
  DWORD ClientDefaultPrinter;
  DWORD BrokenTimeoutSettings;
  DWORD ReconnectSettings;
  DWORD ShadowingSettings;
  DWORD TerminalServerRemoteHomeDir;
  CHAR InitialProgram[261];
  CHAR WorkDirectory[261];
  CHAR TerminalServerProfilePath[261];
  CHAR TerminalServerHomeDir[261];
  CHAR TerminalServerHomeDirDrive[4];
} WTSUSERCONFIGA, *PWTSUSERCONFIGA;

typedef struct _WTSUSERCONFIGW {
  DWORD Source;
  DWORD InheritInitialProgram;
  DWORD AllowLogonTerminalServer;
  DWORD TimeoutSettingsConnections;
  DWORD TimeoutSettingsDisconnections;
  DWORD TimeoutSettingsIdle;
  DWORD DeviceClientDrives;
  DWORD DeviceClientPrinters;
  DWORD ClientDefaultPrinter;
  DWORD BrokenTimeoutSettings;
  DWORD ReconnectSettings;
  DWORD ShadowingSettings;
  DWORD TerminalServerRemoteHomeDir;
  WCHAR InitialProgram[261];
  WCHAR WorkDirectory[261];
  WCHAR TerminalServerProfilePath[261];
  WCHAR TerminalServerHomeDir[261];
  WCHAR TerminalServerHomeDirDrive[4];
} WTSUSERCONFIGW, *PWTSUSERCONFIGW;

typedef struct WTS_CLOUD_AUTH_HANDLE__ {
  int unused;
} *WTS_CLOUD_AUTH_HANDLE;

#ifdef UNICODE
#define PRODUCT_INFO PRODUCT_INFOW
#else
#ifdef UNICODE
#define PRODUCT_INFO PRODUCT_INFOW
#else
#define PRODUCT_INFO PRODUCT_INFOA
#endif
#endif
#ifdef UNICODE
#define PWTS_PROCESS_INFO_EX PWTS_PROCESS_INFO_EXW
#else
#ifdef UNICODE
#define PWTS_PROCESS_INFO_EX PWTS_PROCESS_INFO_EXW
#else
#define PWTS_PROCESS_INFO_EX PWTS_PROCESS_INFO_EXA
#endif
#endif
#ifdef UNICODE
#define PWTS_VALIDATION_INFORMATION PWTS_VALIDATION_INFORMATIONW
#else
#ifdef UNICODE
#define PWTS_VALIDATION_INFORMATION PWTS_VALIDATION_INFORMATIONW
#else
#define PWTS_VALIDATION_INFORMATION PWTS_VALIDATION_INFORMATIONA
#endif
#endif
#ifdef UNICODE
#define PWTSCLIENT PWTSCLIENTW
#else
#ifdef UNICODE
#define PWTSCLIENT PWTSCLIENTW
#else
#define PWTSCLIENT PWTSCLIENTA
#endif
#endif
#ifdef UNICODE
#define PWTSCONFIGINFO PWTSCONFIGINFOW
#else
#ifdef UNICODE
#define PWTSCONFIGINFO PWTSCONFIGINFOW
#else
#define PWTSCONFIGINFO PWTSCONFIGINFOA
#endif
#endif
#ifdef UNICODE
#define PWTSINFOEX PWTSINFOEXW
#else
#ifdef UNICODE
#define PWTSINFOEX PWTSINFOEXW
#else
#define PWTSINFOEX PWTSINFOEXA
#endif
#endif
#ifdef UNICODE
#define PWTSINFOEX_LEVEL PWTSINFOEX_LEVEL_W
#else
#define PWTSINFOEX_LEVEL PWTSINFOEX_LEVEL_A
#endif
#ifdef UNICODE
#define PWTSINFOEX_LEVEL1 PWTSINFOEX_LEVEL1_W
#else
#define PWTSINFOEX_LEVEL1 PWTSINFOEX_LEVEL1_A
#endif
#ifdef UNICODE
#define PWTSLISTENERCONFIG PWTSLISTENERCONFIGW
#else
#ifdef UNICODE
#define PWTSLISTENERCONFIG PWTSLISTENERCONFIGW
#else
#define PWTSLISTENERCONFIG PWTSLISTENERCONFIGA
#endif
#endif
#ifdef UNICODE
#define PWTSLISTENERNAME PWTSLISTENERNAMEW
#else
#ifdef UNICODE
#define PWTSLISTENERNAME PWTSLISTENERNAMEW
#else
#define PWTSLISTENERNAME PWTSLISTENERNAMEA
#endif
#endif
#ifdef UNICODE
#define PWTSUSERCONFIG PWTSUSERCONFIGW
#else
#ifdef UNICODE
#define PWTSUSERCONFIG PWTSUSERCONFIGW
#else
#define PWTSUSERCONFIG PWTSUSERCONFIGA
#endif
#endif
#define WTS_CHANNEL_OPTION_DYNAMIC 0x00000001
#define WTS_CURRENT_SERVER ((HANDLE) NULL)
#define WTS_CURRENT_SERVER_NAME 0
#define WTS_EVENT_NONE 0x00000000
#define WTS_PROCESS_INFO_EX 0
#define WTS_PROTOCOL_TYPE_CONSOLE 0
#define WTS_SECURITY_ALL_ACCESS (STANDARD_RIGHTS_REQUIRED | WTS_SECURITY_QUERY_INFORMATION | WTS_SECURITY_SET_INFORMATION | WTS_SECURITY_RESET | WTS_SECURITY_VIRTUAL_CHANNELS | WTS_SECURITY_REMOTE_CONTROL | WTS_SECURITY_LOGON | WTS_SECURITY_MESSAGE | WTS_SECURITY_CONNECT | WTS_SECURITY_DISCONNECT)
#define WTS_SECURITY_CURRENT_GUEST_ACCESS 0x48
#define WTS_SECURITY_CURRENT_USER_ACCESS 6
#define WTS_SECURITY_GUEST_ACCESS 0x20
#define WTS_SECURITY_USER_ACCESS 0x149
#ifdef UNICODE
#define WTS_VALIDATION_INFORMATION WTS_VALIDATION_INFORMATIONW
#else
#ifdef UNICODE
#define WTS_VALIDATION_INFORMATION WTS_VALIDATION_INFORMATIONW
#else
#define WTS_VALIDATION_INFORMATION WTS_VALIDATION_INFORMATIONA
#endif
#endif
#ifdef UNICODE
#define WTSCLIENT WTSCLIENTW
#else
#ifdef UNICODE
#define WTSCLIENT WTSCLIENTW
#else
#define WTSCLIENT WTSCLIENTA
#endif
#endif
#define WTSCONFIGINFO 0
#ifdef UNICODE
#define WTSCreateListener WTSCreateListenerW
#else
#ifdef UNICODE
#define WTSCreateListener WTSCreateListenerW
#else
#define WTSCreateListener WTSCreateListenerA
#endif
#endif
#ifdef UNICODE
#define WTSEnumerateListeners WTSEnumerateListenersW
#else
#ifdef UNICODE
#define WTSEnumerateListeners WTSEnumerateListenersW
#else
#define WTSEnumerateListeners WTSEnumerateListenersA
#endif
#endif
#ifdef UNICODE
#define WTSEnumerateProcessesEx WTSEnumerateProcessesExW
#else
#ifdef UNICODE
#define WTSEnumerateProcessesEx WTSEnumerateProcessesExW
#else
#define WTSEnumerateProcessesEx WTSEnumerateProcessesExA
#endif
#endif
#ifdef UNICODE
#define WTSEnumerateSessionsEx WTSEnumerateSessionsExW
#else
#ifdef UNICODE
#define WTSEnumerateSessionsEx WTSEnumerateSessionsExW
#else
#define WTSEnumerateSessionsEx WTSEnumerateSessionsExA
#endif
#endif
#ifdef UNICODE
#define WTSFreeMemoryEx WTSFreeMemoryExW
#else
#ifdef UNICODE
#define WTSFreeMemoryEx WTSFreeMemoryExW
#else
#define WTSFreeMemoryEx WTSFreeMemoryExA
#endif
#endif
#ifdef UNICODE
#define WTSGetListenerSecurity WTSGetListenerSecurityW
#else
#ifdef UNICODE
#define WTSGetListenerSecurity WTSGetListenerSecurityW
#else
#define WTSGetListenerSecurity WTSGetListenerSecurityA
#endif
#endif
#ifdef UNICODE
#define WTSINFOEX WTSINFOEXW
#else
#ifdef UNICODE
#define WTSINFOEX WTSINFOEXW
#else
#define WTSINFOEX WTSINFOEXA
#endif
#endif
#ifdef UNICODE
#define WTSINFOEX_LEVEL WTSINFOEX_LEVEL_W
#else
#define WTSINFOEX_LEVEL WTSINFOEX_LEVEL_A
#endif
#ifdef UNICODE
#define WTSINFOEX_LEVEL1 WTSINFOEX_LEVEL1_W
#else
#define WTSINFOEX_LEVEL1 WTSINFOEX_LEVEL1_A
#endif
#ifdef UNICODE
#define WTSLISTENERCONFIG WTSLISTENERCONFIGW
#else
#ifdef UNICODE
#define WTSLISTENERCONFIG WTSLISTENERCONFIGW
#else
#define WTSLISTENERCONFIG WTSLISTENERCONFIGA
#endif
#endif
#ifdef UNICODE
#define WTSLISTENERNAME WTSLISTENERNAMEW
#else
#ifdef UNICODE
#define WTSLISTENERNAME WTSLISTENERNAMEW
#else
#define WTSLISTENERNAME WTSLISTENERNAMEA
#endif
#endif
#ifdef UNICODE
#define WTSOpenServerEx WTSOpenServerExW
#else
#ifdef UNICODE
#define WTSOpenServerEx WTSOpenServerExW
#else
#define WTSOpenServerEx WTSOpenServerExA
#endif
#endif
#ifdef UNICODE
#define WTSQueryListenerConfig WTSQueryListenerConfigW
#else
#ifdef UNICODE
#define WTSQueryListenerConfig WTSQueryListenerConfigW
#else
#define WTSQueryListenerConfig WTSQueryListenerConfigA
#endif
#endif
#ifdef UNICODE
#define WTSSendMessage WTSSendMessageW
#else
#ifdef UNICODE
#define WTSSendMessage WTSSendMessageW
#else
#define WTSSendMessage WTSSendMessageA
#endif
#endif
#ifdef UNICODE
#define WTSSetListenerSecurity WTSSetListenerSecurityW
#else
#ifdef UNICODE
#define WTSSetListenerSecurity WTSSetListenerSecurityW
#else
#define WTSSetListenerSecurity WTSSetListenerSecurityA
#endif
#endif
#ifdef UNICODE
#define WTSSetUserConfig WTSSetUserConfigW
#else
#ifdef UNICODE
#define WTSSetUserConfig WTSSetUserConfigW
#else
#define WTSSetUserConfig WTSSetUserConfigA
#endif
#endif
#ifdef UNICODE
#define WTSUSERCONFIG WTSUSERCONFIGW
#else
#ifdef UNICODE
#define WTSUSERCONFIG WTSUSERCONFIGW
#else
#define WTSUSERCONFIG WTSUSERCONFIGA
#endif
#endif

typedef CHAR WTSLISTENERNAMEA[33];

typedef WTSLISTENERNAMEA *PWTSLISTENERNAMEA;

typedef WCHAR WTSLISTENERNAMEW[33];

typedef WTSLISTENERNAMEW *PWTSLISTENERNAMEW;

#ifdef __cplusplus
extern "C" {
#endif

BOOL WINAPI WTSEnumerateProcessesExA(HANDLE hServer, DWORD *pLevel, DWORD SessionId, LPSTR *ppProcessInfo, DWORD *pCount);
BOOL WINAPI WTSEnumerateProcessesExW(HANDLE hServer, DWORD *pLevel, DWORD SessionId, LPWSTR *ppProcessInfo, DWORD *pCount);
BOOL WINAPI WTSEnumerateSessionsExA(HANDLE hServer, DWORD *pLevel, DWORD Filter, PWTS_SESSION_INFO_1A *ppSessionInfo, DWORD *pCount);
BOOL WINAPI WTSEnumerateSessionsExW(HANDLE hServer, DWORD *pLevel, DWORD Filter, PWTS_SESSION_INFO_1W *ppSessionInfo, DWORD *pCount);
BOOL WINAPI WTSFreeMemoryExA(WTS_TYPE_CLASS WTSTypeClass, PVOID pMemory, ULONG NumberOfEntries);
BOOL WINAPI WTSFreeMemoryExW(WTS_TYPE_CLASS WTSTypeClass, PVOID pMemory, ULONG NumberOfEntries);
BOOL WINAPI WTSLogoffSession(HANDLE hServer, DWORD SessionId, BOOL bWait);
BOOL WINAPI WTSSendMessageA(HANDLE hServer, DWORD SessionId, LPSTR pTitle, DWORD TitleLength, LPSTR pMessage, DWORD MessageLength, DWORD Style, DWORD Timeout, DWORD *pResponse, BOOL bWait);
BOOL WINAPI WTSSendMessageW(HANDLE hServer, DWORD SessionId, LPWSTR pTitle, DWORD TitleLength, LPWSTR pMessage, DWORD MessageLength, DWORD Style, DWORD Timeout, DWORD *pResponse, BOOL bWait);
BOOL WINAPI WTSSetUserConfigA(LPSTR pServerName, LPSTR pUserName, WTS_CONFIG_CLASS WTSConfigClass, LPSTR pBuffer, DWORD DataLength);
BOOL WINAPI WTSSetUserConfigW(LPWSTR pServerName, LPWSTR pUserName, WTS_CONFIG_CLASS WTSConfigClass, LPWSTR pBuffer, DWORD DataLength);
BOOL WINAPI WTSShutdownSystem(HANDLE hServer, DWORD ShutdownFlag);
HANDLE WINAPI WTSVirtualChannelOpen(HANDLE hServer, DWORD SessionId, LPSTR pVirtualName);
HANDLE WINAPI WTSVirtualChannelOpenEx(DWORD SessionId, LPSTR pVirtualName, DWORD flags);
BOOL WINAPI WTSVirtualChannelQuery(HANDLE hChannelHandle, WTS_VIRTUAL_CLASS, PVOID *ppBuffer, DWORD *pBytesReturned);
BOOL WINAPI WTSVirtualChannelRead(HANDLE hChannelHandle, ULONG TimeOut, PCHAR Buffer, ULONG BufferSize, PULONG pBytesRead);
BOOL WINAPI WTSVirtualChannelWrite(HANDLE hChannelHandle, PCHAR Buffer, ULONG Length, PULONG pBytesWritten);

/* TODO: LiberNT does not implement the functions below yet */
BOOL WINAPI WTSActiveSessionExists(PBOOL pbActiveSessionExists);
void WINAPI WTSCloudAuthClose(WTS_CLOUD_AUTH_HANDLE cloudAuthHandle);
BOOL WINAPI WTSCloudAuthConvertAssertionToSerializedUserCredential(WTS_CLOUD_AUTH_HANDLE cloudAuthHandle, PCSTR assertion, ULONG assertionLength, PCWSTR resourceId, WTS_SERIALIZED_USER_CREDENTIAL **userCredential);
BOOL WINAPI WTSCloudAuthDuplicateSerializedUserCredential(const WTS_SERIALIZED_USER_CREDENTIAL *userCredential, WTS_SERIALIZED_USER_CREDENTIAL **duplicatedUserCredential);
BOOL WINAPI WTSCloudAuthGetServerNonce(WTS_CLOUD_AUTH_HANDLE cloudAuthHandle, PWSTR *serverNonce);
BOOL WINAPI WTSCloudAuthNetworkLogonWithSerializedCredential(WTS_CLOUD_AUTH_HANDLE cloudAuthHandle, WTS_SERIALIZED_USER_CREDENTIAL *userCredential, HANDLE *token);
WTS_CLOUD_AUTH_HANDLE WINAPI WTSCloudAuthOpen(const GUID *activityId);
BOOL WINAPI WTSCreateListenerA(HANDLE hServer, PVOID pReserved, DWORD Reserved, LPSTR pListenerName, PWTSLISTENERCONFIGA pBuffer, DWORD flag);
BOOL WINAPI WTSCreateListenerW(HANDLE hServer, PVOID pReserved, DWORD Reserved, LPWSTR pListenerName, PWTSLISTENERCONFIGW pBuffer, DWORD flag);
BOOL WINAPI WTSEnumerateListenersA(HANDLE hServer, PVOID pReserved, DWORD Reserved, PWTSLISTENERNAMEA pListeners, DWORD *pCount);
BOOL WINAPI WTSEnumerateListenersW(HANDLE hServer, PVOID pReserved, DWORD Reserved, PWTSLISTENERNAMEW pListeners, DWORD *pCount);
BOOL WTSGetChildSessionId(PULONG pSessionId);
BOOL WINAPI WTSGetListenerSecurityA(HANDLE hServer, PVOID pReserved, DWORD Reserved, LPSTR pListenerName, SECURITY_INFORMATION SecurityInformation, PSECURITY_DESCRIPTOR pSecurityDescriptor, DWORD nLength, LPDWORD lpnLengthNeeded);
BOOL WINAPI WTSGetListenerSecurityW(HANDLE hServer, PVOID pReserved, DWORD Reserved, LPWSTR pListenerName, SECURITY_INFORMATION SecurityInformation, PSECURITY_DESCRIPTOR pSecurityDescriptor, DWORD nLength, LPDWORD lpnLengthNeeded);
BOOL WTSIsChildSessionsEnabled(PBOOL pbEnabled);
BOOL WINAPI WTSQueryListenerConfigA(HANDLE hServer, PVOID pReserved, DWORD Reserved, LPSTR pListenerName, PWTSLISTENERCONFIGA pBuffer);
BOOL WINAPI WTSQueryListenerConfigW(HANDLE hServer, PVOID pReserved, DWORD Reserved, LPWSTR pListenerName, PWTSLISTENERCONFIGW pBuffer);
BOOL WINAPI WTSSetListenerSecurityA(HANDLE hServer, PVOID pReserved, DWORD Reserved, LPSTR pListenerName, SECURITY_INFORMATION SecurityInformation, PSECURITY_DESCRIPTOR pSecurityDescriptor);
BOOL WINAPI WTSSetListenerSecurityW(HANDLE hServer, PVOID pReserved, DWORD Reserved, LPWSTR pListenerName, SECURITY_INFORMATION SecurityInformation, PSECURITY_DESCRIPTOR pSecurityDescriptor);

#ifdef __cplusplus
}
#endif

enum _WTS_INFO_CLASS
{
    WTSSessionActivityId = 0x1E,
    WTSCapabilityCheck = 0x1F,
};

#endif
