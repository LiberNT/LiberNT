#ifndef _WINSVC_
#define _WINSVC_

#ifdef __cplusplus
extern "C" {
#endif
#define SERVICES_ACTIVE_DATABASEA "ServicesActive"
#define SERVICES_ACTIVE_DATABASEW L"ServicesActive"
#define SERVICES_FAILED_DATABASEA "ServicesFailed"
#define SERVICES_FAILED_DATABASEW L"ServicesFailed"
#define SC_GROUP_IDENTIFIERA '+'
#define SC_GROUP_IDENTIFIERW L'+'
#define SC_MANAGER_ALL_ACCESS	0xf003f
#define SC_MANAGER_CONNECT	1
#define SC_MANAGER_CREATE_SERVICE	2
#define SC_MANAGER_ENUMERATE_SERVICE	4
#define SC_MANAGER_LOCK	8
#define SC_MANAGER_QUERY_LOCK_STATUS	16
#define SC_MANAGER_MODIFY_BOOT_CONFIG	32
#define SERVICE_NO_CHANGE 0xffffffff
#define SERVICE_STOPPED	1
#define SERVICE_START_PENDING	2
#define SERVICE_STOP_PENDING	3
#define SERVICE_RUNNING	4
#define SERVICE_CONTINUE_PENDING	5
#define SERVICE_PAUSE_PENDING	6
#define SERVICE_PAUSED	7
#define SERVICE_ACCEPT_STOP	1
#define SERVICE_ACCEPT_PAUSE_CONTINUE	2
#define SERVICE_ACCEPT_SHUTDOWN 4
#define SERVICE_ACCEPT_PARAMCHANGE    8
#define SERVICE_ACCEPT_NETBINDCHANGE  16
#define SERVICE_ACCEPT_HARDWAREPROFILECHANGE   32
#define SERVICE_ACCEPT_POWEREVENT              64
#define SERVICE_ACCEPT_SESSIONCHANGE           128
#define SERVICE_ACCEPT_PRESHUTDOWN             256
#define SERVICE_ACCEPT_TIMECHANGE              512
#define SERVICE_ACCEPT_TRIGGEREVENT            1024
#define SERVICE_ACCEPT_USER_LOGOFF             2048
#define SERVICE_ACCEPT_LOWRESOURCES            8192
#define SERVICE_ACCEPT_SYSTEMLOWRESOURCES      16384
#define SERVICE_CONTROL_STOP	1
#define SERVICE_CONTROL_PAUSE	2
#define SERVICE_CONTROL_CONTINUE	3
#define SERVICE_CONTROL_INTERROGATE	4
#define SERVICE_CONTROL_SHUTDOWN	5
#define SERVICE_CONTROL_PARAMCHANGE     6
#define SERVICE_CONTROL_NETBINDADD      7
#define SERVICE_CONTROL_NETBINDREMOVE   8
#define SERVICE_CONTROL_NETBINDENABLE   9
#define SERVICE_CONTROL_NETBINDDISABLE  10
#define SERVICE_CONTROL_DEVICEEVENT     11
#define SERVICE_CONTROL_HARDWAREPROFILECHANGE 12
#define SERVICE_CONTROL_POWEREVENT            13
#define SERVICE_CONTROL_SESSIONCHANGE         14
#define SERVICE_ACTIVE 1
#define SERVICE_INACTIVE 2
#define SERVICE_STATE_ALL 3
#define SERVICE_QUERY_CONFIG 1
#define SERVICE_CHANGE_CONFIG 2
#define SERVICE_QUERY_STATUS 4
#define SERVICE_ENUMERATE_DEPENDENTS 8
#define SERVICE_START 16
#define SERVICE_STOP 32
#define SERVICE_PAUSE_CONTINUE 64
#define SERVICE_INTERROGATE 128
#define SERVICE_USER_DEFINED_CONTROL 256
#define SERVICE_ALL_ACCESS (STANDARD_RIGHTS_REQUIRED|SERVICE_QUERY_CONFIG|SERVICE_CHANGE_CONFIG|SERVICE_QUERY_STATUS|SERVICE_ENUMERATE_DEPENDENTS|SERVICE_START|SERVICE_STOP|SERVICE_PAUSE_CONTINUE|SERVICE_INTERROGATE|SERVICE_USER_DEFINED_CONTROL)
#define SERVICE_RUNS_IN_SYSTEM_PROCESS 1

#define SERVICE_CONFIG_DESCRIPTION              1
#define SERVICE_CONFIG_FAILURE_ACTIONS          2
#define SERVICE_CONFIG_DELAYED_AUTO_START_INFO  3
#define SERVICE_CONFIG_FAILURE_ACTIONS_FLAG     4
#define SERVICE_CONFIG_SERVICE_SID_INFO         5
#define SERVICE_CONFIG_REQUIRED_PRIVILEGES_INFO 6
#define SERVICE_CONFIG_PRESHUTDOWN_INFO         7
#define SERVICE_CONFIG_TRIGGER_INFO             8
#define SERVICE_CONFIG_PREFERRED_NODE           9
#define SERVICE_CONFIG_RUNLEVEL_INFO            10

#define SERVICE_NOTIFY_STATUS_CHANGE_1 1
#define SERVICE_NOTIFY_STATUS_CHANGE_2 2
#define SERVICE_NOTIFY_STATUS_CHANGE   SERVICE_NOTIFY_STATUS_CHANGE_2

#define SERVICE_NOTIFY_STOPPED          1
#define SERVICE_NOTIFY_START_PENDING    2
#define SERVICE_NOTIFY_STOP_PENDING     4
#define SERVICE_NOTIFY_RUNNING          8
#define SERVICE_NOTIFY_CONTINUE_PENDING 16
#define SERVICE_NOTIFY_PAUSE_PENDING    32
#define SERVICE_NOTIFY_PAUSED           64
#define SERVICE_NOTIFY_CREATED          128
#define SERVICE_NOTIFY_DELETED          256
#define SERVICE_NOTIFY_DELETE_PENDING   512

#define SERVICE_STOP_REASON_FLAG_MIN                            0x00000000
#define SERVICE_STOP_REASON_FLAG_UNPLANNED                      0x10000000
#define SERVICE_STOP_REASON_FLAG_CUSTOM                         0x20000000
#define SERVICE_STOP_REASON_FLAG_PLANNED                        0x40000000
#define SERVICE_STOP_REASON_FLAG_MAX                            0x80000000

#define SERVICE_STOP_REASON_MAJOR_MIN                           0x00000000
#define SERVICE_STOP_REASON_MAJOR_OTHER                         0x00010000
#define SERVICE_STOP_REASON_MAJOR_HARDWARE                      0x00020000
#define SERVICE_STOP_REASON_MAJOR_OPERATINGSYSTEM               0x00030000
#define SERVICE_STOP_REASON_MAJOR_SOFTWARE                      0x00040000
#define SERVICE_STOP_REASON_MAJOR_APPLICATION                   0x00050000
#define SERVICE_STOP_REASON_MAJOR_NONE                          0x00060000
#define SERVICE_STOP_REASON_MAJOR_MAX                           0x00070000
#define SERVICE_STOP_REASON_MAJOR_MIN_CUSTOM                    0x00400000
#define SERVICE_STOP_REASON_MAJOR_MAX_CUSTOM                    0x00ff0000

#define SERVICE_STOP_REASON_MINOR_MIN                           0x00000000
#define SERVICE_STOP_REASON_MINOR_OTHER                         0x00000001
#define SERVICE_STOP_REASON_MINOR_MAINTENANCE                   0x00000002
#define SERVICE_STOP_REASON_MINOR_INSTALLATION                  0x00000003
#define SERVICE_STOP_REASON_MINOR_UPGRADE                       0x00000004
#define SERVICE_STOP_REASON_MINOR_RECONFIG                      0x00000005
#define SERVICE_STOP_REASON_MINOR_HUNG                          0x00000006
#define SERVICE_STOP_REASON_MINOR_UNSTABLE                      0x00000007
#define SERVICE_STOP_REASON_MINOR_DISK                          0x00000008
#define SERVICE_STOP_REASON_MINOR_NETWORKCARD                   0x00000009
#define SERVICE_STOP_REASON_MINOR_ENVIRONMENT                   0x0000000a
#define SERVICE_STOP_REASON_MINOR_HARDWARE_DRIVER               0x0000000b
#define SERVICE_STOP_REASON_MINOR_OTHERDRIVER                   0x0000000c
#define SERVICE_STOP_REASON_MINOR_SERVICEPACK                   0x0000000d
#define SERVICE_STOP_REASON_MINOR_SOFTWARE_UPDATE               0x0000000e
#define SERVICE_STOP_REASON_MINOR_SECURITYFIX                   0x0000000f
#define SERVICE_STOP_REASON_MINOR_SECURITY                      0x00000010
#define SERVICE_STOP_REASON_MINOR_NETWORK_CONNECTIVITY          0x00000011
#define SERVICE_STOP_REASON_MINOR_WMI                           0x00000012
#define SERVICE_STOP_REASON_MINOR_SERVICEPACK_UNINSTALL         0x00000013
#define SERVICE_STOP_REASON_MINOR_SOFTWARE_UPDATE_UNINSTALL     0x00000014
#define SERVICE_STOP_REASON_MINOR_SECURITYFIX_UNINSTALL         0x00000015
#define SERVICE_STOP_REASON_MINOR_MMC                           0x00000016
#define SERVICE_STOP_REASON_MINOR_NONE                          0x00000017
#define SERVICE_STOP_REASON_MINOR_MEMOTYLIMIT                   0x00000018
#define SERVICE_STOP_REASON_MINOR_MAX                           0x00000019
#define SERVICE_STOP_REASON_MINOR_MIN_CUSTOM                    0x00000100
#define SERVICE_STOP_REASON_MINOR_MAX_CUSTOM                    0x0000FFFF

#define SERVICE_CONTROL_STATUS_REASON_INFO      1

#ifndef _SERVICE_PRESHUTDOWN_INFO_DEFINED_
#define _SERVICE_PRESHUTDOWN_INFO_DEFINED_
typedef struct _SERVICE_PRESHUTDOWN_INFO {
    DWORD dwPreshutdownTimeout;
} SERVICE_PRESHUTDOWN_INFO, *LPSERVICE_PRESHUTDOWN_INFO;
#endif

#ifndef _SERVICE_FAILURE_ACTIONS_FLAG_DEFINED_
#define _SERVICE_FAILURE_ACTIONS_FLAG_DEFINED_
typedef struct _SERVICE_FAILURE_ACTIONS_FLAG {
    BOOL fFailureActionsOnNonCrashFailures;
} SERVICE_FAILURE_ACTIONS_FLAG, *LPSERVICE_FAILURE_ACTIONS_FLAG;
#endif

#define SERVICE_TRIGGER_TYPE_DEVICE_INTERFACE_ARRIVAL   1
#define SERVICE_TRIGGER_TYPE_IP_ADDRESS_AVAILABILITY    2
#define SERVICE_TRIGGER_TYPE_DOMAIN_JOIN                3
#define SERVICE_TRIGGER_TYPE_FIREWALL_PORT_EVENT        4
#define SERVICE_TRIGGER_TYPE_GROUP_POLICY               5
#define SERVICE_TRIGGER_TYPE_NETWORK_ENDPOINT           6
#define SERVICE_TRIGGER_TYPE_CUSTOM_SYSTEM_STATE_CHANGE 7
#define SERVICE_TRIGGER_TYPE_CUSTOM                     20
#define SERVICE_TRIGGER_TYPE_AGGREGATE                  30

#define SERVICE_TRIGGER_DATA_TYPE_BINARY      1
#define SERVICE_TRIGGER_DATA_TYPE_STRING      2
#define SERVICE_TRIGGER_DATA_TYPE_LEVEL       3
#define SERVICE_TRIGGER_DATA_TYPE_KEYWORD_ANY 4
#define SERVICE_TRIGGER_DATA_TYPE_KEYWORD_ALL 5

#define SERVICE_TRIGGER_ACTION_SERVICE_START 1
#define SERVICE_TRIGGER_ACTION_SERVICE_STOP  2

#define SERVICE_TRIGGER_STARTED_ARGUMENT L"TriggerStarted"

DEFINE_GUID(NETWORK_MANAGER_FIRST_IP_ADDRESS_ARRIVAL_GUID, 0x4f27f2de, 0x14e2, 0x430b, 0xa5, 0x49, 0x7c, 0xd4, 0x8c, 0xbc, 0x82, 0x45);
DEFINE_GUID(NETWORK_MANAGER_LAST_IP_ADDRESS_REMOVAL_GUID, 0xcc4ba62a, 0x162e, 0x4648, 0x84, 0x7a, 0xb6, 0xbd, 0xf9, 0x93, 0xe3, 0x35);
DEFINE_GUID(DOMAIN_JOIN_GUID, 0x1ce20aba, 0x9851, 0x4421, 0x94, 0x30, 0x1d, 0xde, 0xb7, 0x66, 0xe8, 0x09);
DEFINE_GUID(DOMAIN_LEAVE_GUID, 0xddaf516e, 0x58c2, 0x4866, 0x95, 0x74, 0xc3, 0xb6, 0x15, 0xd4, 0x2e, 0xa1);
DEFINE_GUID(FIREWALL_PORT_OPEN_GUID, 0xb7569e07, 0x8421, 0x4ee0, 0xad, 0x10, 0x86, 0x91, 0x5a, 0xfd, 0xad, 0x09);
DEFINE_GUID(FIREWALL_PORT_CLOSE_GUID, 0xa144ed38, 0x8e12, 0x4de4, 0x9d, 0x96, 0xe6, 0x47, 0x40, 0xb1, 0xa5, 0x24);
DEFINE_GUID(MACHINE_POLICY_PRESENT_GUID, 0x659fcae6, 0x5bdb, 0x4da9, 0xb1, 0xff, 0xca, 0x2a, 0x17, 0x8d, 0x46, 0xe0);
DEFINE_GUID(USER_POLICY_PRESENT_GUID, 0x54fb46c8, 0xf089, 0x464c, 0xb1, 0xfd, 0x59, 0xd1, 0xb6, 0x2c, 0x3b, 0x50);
DEFINE_GUID(RPC_INTERFACE_EVENT_GUID, 0xbc90d167, 0x9470, 0x4139, 0xa9, 0xba, 0xbe, 0x0b, 0xbb, 0xf5, 0xb7, 0x4d);
DEFINE_GUID(NAMED_PIPE_EVENT_GUID, 0x1f81d131, 0x3fac, 0x4537, 0x9e, 0x0c, 0x7e, 0x7b, 0x0c, 0x2f, 0x4b, 0x55);
DEFINE_GUID(CUSTOM_SYSTEM_STATE_CHANGE_EVENT_GUID, 0x2d7a2816, 0x0c5e, 0x45fc, 0x9c, 0xe7, 0x57, 0x0e, 0x5e, 0xcd, 0xe9, 0xc9);

#ifndef _SERVICE_TRIGGER_INFO_DEFINED_
#define _SERVICE_TRIGGER_INFO_DEFINED_
typedef struct _SERVICE_TRIGGER_SPECIFIC_DATA_ITEM {
    DWORD dwDataType;
    DWORD cbData;
    PBYTE pData;
} SERVICE_TRIGGER_SPECIFIC_DATA_ITEM, *PSERVICE_TRIGGER_SPECIFIC_DATA_ITEM;

typedef struct _SERVICE_TRIGGER {
    DWORD dwTriggerType;
    DWORD dwAction;
    GUID *pTriggerSubtype;
    DWORD cDataItems;
    PSERVICE_TRIGGER_SPECIFIC_DATA_ITEM pDataItems;
} SERVICE_TRIGGER, *PSERVICE_TRIGGER;

typedef struct _SERVICE_TRIGGER_INFO {
    DWORD cTriggers;
    PSERVICE_TRIGGER pTriggers;
    PBYTE pReserved;
} SERVICE_TRIGGER_INFO, *PSERVICE_TRIGGER_INFO;
#endif

typedef struct _SERVICE_STATUS {
	DWORD dwServiceType;
	DWORD dwCurrentState;
	DWORD dwControlsAccepted;
	DWORD dwWin32ExitCode;
	DWORD dwServiceSpecificExitCode;
	DWORD dwCheckPoint;
	DWORD dwWaitHint;
} SERVICE_STATUS,*LPSERVICE_STATUS;
typedef struct _SERVICE_STATUS_PROCESS {
	DWORD dwServiceType;
	DWORD dwCurrentState;
	DWORD dwControlsAccepted;
	DWORD dwWin32ExitCode;
	DWORD dwServiceSpecificExitCode;
	DWORD dwCheckPoint;
	DWORD dwWaitHint;
	DWORD dwProcessId;
	DWORD dwServiceFlags;
} SERVICE_STATUS_PROCESS, *LPSERVICE_STATUS_PROCESS;
typedef enum _SC_STATUS_TYPE {
	SC_STATUS_PROCESS_INFO = 0
} SC_STATUS_TYPE;
typedef enum _SC_ENUM_TYPE {
        SC_ENUM_PROCESS_INFO = 0
} SC_ENUM_TYPE;
typedef struct _ENUM_SERVICE_STATUSA {
	LPSTR lpServiceName;
	LPSTR lpDisplayName;
	SERVICE_STATUS ServiceStatus;
} ENUM_SERVICE_STATUSA,*LPENUM_SERVICE_STATUSA;
typedef struct _ENUM_SERVICE_STATUSW {
	LPWSTR lpServiceName;
	LPWSTR lpDisplayName;
	SERVICE_STATUS ServiceStatus;
} ENUM_SERVICE_STATUSW,*LPENUM_SERVICE_STATUSW;
typedef struct _ENUM_SERVICE_STATUS_PROCESSA {
	LPSTR lpServiceName;
	LPSTR lpDisplayName;
	SERVICE_STATUS_PROCESS ServiceStatusProcess;
} ENUM_SERVICE_STATUS_PROCESSA,*LPENUM_SERVICE_STATUS_PROCESSA;
typedef struct _ENUM_SERVICE_STATUS_PROCESSW {
	LPWSTR lpServiceName;
	LPWSTR lpDisplayName;
	SERVICE_STATUS_PROCESS ServiceStatusProcess;
} ENUM_SERVICE_STATUS_PROCESSW,*LPENUM_SERVICE_STATUS_PROCESSW;
typedef struct _QUERY_SERVICE_CONFIGA {
	DWORD dwServiceType;
	DWORD dwStartType;
	DWORD dwErrorControl;
	LPSTR lpBinaryPathName;
	LPSTR lpLoadOrderGroup;
	DWORD dwTagId;
	LPSTR lpDependencies;
	LPSTR lpServiceStartName;
	LPSTR lpDisplayName;
} QUERY_SERVICE_CONFIGA,*LPQUERY_SERVICE_CONFIGA;
typedef struct _QUERY_SERVICE_CONFIGW {
	DWORD dwServiceType;
	DWORD dwStartType;
	DWORD dwErrorControl;
	LPWSTR lpBinaryPathName;
	LPWSTR lpLoadOrderGroup;
	DWORD dwTagId;
	LPWSTR lpDependencies;
	LPWSTR lpServiceStartName;
	LPWSTR lpDisplayName;
} QUERY_SERVICE_CONFIGW,*LPQUERY_SERVICE_CONFIGW;
typedef struct _QUERY_SERVICE_LOCK_STATUSA {
	DWORD fIsLocked;
	LPSTR lpLockOwner;
	DWORD dwLockDuration;
} QUERY_SERVICE_LOCK_STATUSA,*LPQUERY_SERVICE_LOCK_STATUSA;
typedef struct _QUERY_SERVICE_LOCK_STATUSW {
	DWORD fIsLocked;
	LPWSTR lpLockOwner;
	DWORD dwLockDuration;
} QUERY_SERVICE_LOCK_STATUSW,*LPQUERY_SERVICE_LOCK_STATUSW;
typedef void (WINAPI *LPSERVICE_MAIN_FUNCTIONA)(DWORD,LPSTR*);
typedef void (WINAPI *LPSERVICE_MAIN_FUNCTIONW)(DWORD,LPWSTR*);
typedef struct _SERVICE_TABLE_ENTRYA {
	LPSTR lpServiceName;
	LPSERVICE_MAIN_FUNCTIONA lpServiceProc;
} SERVICE_TABLE_ENTRYA,*LPSERVICE_TABLE_ENTRYA;
typedef struct _SERVICE_TABLE_ENTRYW {
	LPWSTR lpServiceName;
	LPSERVICE_MAIN_FUNCTIONW lpServiceProc;
} SERVICE_TABLE_ENTRYW,*LPSERVICE_TABLE_ENTRYW;
DECLARE_HANDLE(SC_HANDLE);
typedef SC_HANDLE *LPSC_HANDLE;
typedef PVOID SC_LOCK;
DECLARE_HANDLE(SERVICE_STATUS_HANDLE);
typedef VOID(WINAPI *LPHANDLER_FUNCTION)(DWORD);
typedef DWORD (WINAPI *LPHANDLER_FUNCTION_EX)(DWORD,DWORD,LPVOID,LPVOID);
typedef struct _SERVICE_DESCRIPTIONA {
	LPSTR lpDescription;
} SERVICE_DESCRIPTIONA,*LPSERVICE_DESCRIPTIONA;
typedef struct _SERVICE_DESCRIPTIONW {
	LPWSTR lpDescription;
} SERVICE_DESCRIPTIONW,*LPSERVICE_DESCRIPTIONW;
typedef enum _SC_ACTION_TYPE {
        SC_ACTION_NONE          = 0,
        SC_ACTION_RESTART       = 1,
        SC_ACTION_REBOOT        = 2,
        SC_ACTION_RUN_COMMAND   = 3
} SC_ACTION_TYPE;
typedef struct _SC_ACTION {
	SC_ACTION_TYPE	Type;
	DWORD		Delay;
} SC_ACTION,*LPSC_ACTION;
typedef struct _SERVICE_FAILURE_ACTIONSA {
	DWORD	dwResetPeriod;
	LPSTR	lpRebootMsg;
	LPSTR	lpCommand;
	DWORD	cActions;
	SC_ACTION * lpsaActions;
} SERVICE_FAILURE_ACTIONSA,*LPSERVICE_FAILURE_ACTIONSA;
typedef struct _SERVICE_FAILURE_ACTIONSW {
	DWORD	dwResetPeriod;
	LPWSTR	lpRebootMsg;
	LPWSTR	lpCommand;
	DWORD	cActions;
	SC_ACTION * lpsaActions;
} SERVICE_FAILURE_ACTIONSW,*LPSERVICE_FAILURE_ACTIONSW;

typedef struct _SERVICE_REQUIRED_PRIVILEGES_INFOA {
  LPSTR pmszRequiredPrivileges;
} SERVICE_REQUIRED_PRIVILEGES_INFOA, *LPSERVICE_REQUIRED_PRIVILEGES_INFOA;

typedef struct _SERVICE_REQUIRED_PRIVILEGES_INFOW {
  LPWSTR pmszRequiredPrivileges;
} SERVICE_REQUIRED_PRIVILEGES_INFOW, *LPSERVICE_REQUIRED_PRIVILEGES_INFOW;

typedef VOID (CALLBACK *PFN_SC_NOTIFY_CALLBACK)(_In_ PVOID);
typedef struct _SERVICE_NOTIFY_1 {
  DWORD dwVersion;
  PFN_SC_NOTIFY_CALLBACK pfnNotifyCallback;
  PVOID pContext;
  DWORD dwNotificationStatus;
  SERVICE_STATUS_PROCESS ServiceStatus;
} SERVICE_NOTIFY_1, *PSERVICE_NOTIFY_1;
typedef struct _SERVICE_NOTIFY_2A {
  DWORD dwVersion;
  PFN_SC_NOTIFY_CALLBACK pfnNotifyCallback;
  PVOID pContext;
  DWORD dwNotificationStatus;
  SERVICE_STATUS_PROCESS ServiceStatus;
  DWORD dwNotificationTriggered;
  LPSTR pszServiceNames;
} SERVICE_NOTIFY_2A, *PSERVICE_NOTIFY_2A;
typedef struct _SERVICE_NOTIFY_2W {
  DWORD dwVersion;
  PFN_SC_NOTIFY_CALLBACK pfnNotifyCallback;
  PVOID pContext;
  DWORD dwNotificationStatus;
  SERVICE_STATUS_PROCESS ServiceStatus;
  DWORD dwNotificationTriggered;
  LPWSTR pszServiceNames;
} SERVICE_NOTIFY_2W, *PSERVICE_NOTIFY_2W;
typedef SERVICE_NOTIFY_2A SERVICE_NOTIFYA, *PSERVICE_NOTIFYA;
typedef SERVICE_NOTIFY_2W SERVICE_NOTIFYW, *PSERVICE_NOTIFYW;
typedef struct _SERVICE_CONTROL_STATUS_REASON_PARAMSA {
  DWORD dwReason;
  LPSTR pszComment;
  SERVICE_STATUS_PROCESS ServiceStatus;
} SERVICE_CONTROL_STATUS_REASON_PARAMSA, *PSERVICE_CONTROL_STATUS_REASON_PARAMSA;
typedef struct _SERVICE_CONTROL_STATUS_REASON_PARAMSW {
  DWORD dwReason;
  LPWSTR pszComment;
  SERVICE_STATUS_PROCESS ServiceStatus;
} SERVICE_CONTROL_STATUS_REASON_PARAMSW, *PSERVICE_CONTROL_STATUS_REASON_PARAMSW;

BOOL WINAPI ChangeServiceConfigA(_In_ SC_HANDLE, _In_ DWORD, _In_ DWORD, _In_ DWORD, _In_opt_ LPCSTR, _In_opt_ LPCSTR, _Out_opt_ LPDWORD, _In_opt_ LPCSTR, _In_opt_ LPCSTR, _In_opt_ LPCSTR, _In_opt_ LPCSTR);
BOOL WINAPI ChangeServiceConfigW(_In_ SC_HANDLE, _In_ DWORD, _In_ DWORD, _In_ DWORD, _In_opt_ LPCWSTR, _In_opt_ LPCWSTR, _Out_opt_ LPDWORD, _In_opt_ LPCWSTR, _In_opt_ LPCWSTR, _In_opt_ LPCWSTR, _In_opt_ LPCWSTR);
BOOL WINAPI ChangeServiceConfig2A(_In_ SC_HANDLE, _In_ DWORD, _In_opt_ LPVOID);
BOOL WINAPI ChangeServiceConfig2W(_In_ SC_HANDLE, _In_ DWORD, _In_opt_ LPVOID);
BOOL WINAPI CloseServiceHandle(_In_ SC_HANDLE);
BOOL WINAPI ControlService(_In_ SC_HANDLE, _In_ DWORD, _Out_ LPSERVICE_STATUS);
BOOL WINAPI ControlServiceExA(_In_ SC_HANDLE, _In_ DWORD, _In_ DWORD, _Inout_ PVOID);
BOOL WINAPI ControlServiceExW(_In_ SC_HANDLE, _In_ DWORD, _In_ DWORD, _Inout_ PVOID);
_Must_inspect_result_ SC_HANDLE WINAPI CreateServiceA(_In_ SC_HANDLE, _In_ LPCSTR, _In_opt_ LPCSTR, _In_ DWORD, _In_ DWORD, _In_ DWORD, _In_ DWORD, _In_opt_ LPCSTR, _In_opt_ LPCSTR, _Out_opt_ PDWORD, _In_opt_ LPCSTR, _In_opt_ LPCSTR, _In_opt_ LPCSTR);
_Must_inspect_result_ SC_HANDLE WINAPI CreateServiceW(_In_ SC_HANDLE, _In_ LPCWSTR, _In_opt_ LPCWSTR, _In_ DWORD, _In_ DWORD, _In_ DWORD, _In_ DWORD, _In_opt_ LPCWSTR, _In_opt_ LPCWSTR, _Out_opt_ PDWORD, _In_opt_ LPCWSTR, _In_opt_ LPCWSTR, _In_opt_ LPCWSTR);
BOOL WINAPI DeleteService(_In_ SC_HANDLE);

_Must_inspect_result_
BOOL
WINAPI
EnumDependentServicesA(
  _In_ SC_HANDLE hService,
  _In_ DWORD dwServiceState,
  _Out_writes_bytes_opt_(cbBufSize) LPENUM_SERVICE_STATUSA lpServices,
  _In_ DWORD cbBufSize,
  _Out_ LPDWORD pcbBytesNeeded,
  _Out_ LPDWORD lpServicesReturned);

_Must_inspect_result_
BOOL
WINAPI
EnumDependentServicesW(
  _In_ SC_HANDLE hService,
  _In_ DWORD dwServiceState,
  _Out_writes_bytes_opt_(cbBufSize) LPENUM_SERVICE_STATUSW lpServices,
  _In_ DWORD cbBufSize,
  _Out_ LPDWORD pcbBytesNeeded,
  _Out_ LPDWORD lpServicesReturned);

_Must_inspect_result_
BOOL
WINAPI
EnumServicesStatusA(
  _In_ SC_HANDLE hSCManager,
  _In_ DWORD dwServiceType,
  _In_ DWORD dwServiceState,
  _Out_writes_bytes_opt_(cbBufSize) LPENUM_SERVICE_STATUSA lpServices,
  _In_ DWORD cbBufSize,
  _Out_ LPDWORD pcbBytesNeeded,
  _Out_ LPDWORD lpServicesReturned,
  _Inout_opt_ LPDWORD lpResumeHandle);

_Must_inspect_result_
BOOL
WINAPI
EnumServicesStatusW(
  _In_ SC_HANDLE hSCManager,
  _In_ DWORD dwServiceType,
  _In_ DWORD dwServiceState,
  _Out_writes_bytes_opt_(cbBufSize) LPENUM_SERVICE_STATUSW lpServices,
  _In_ DWORD cbBufSize,
  _Out_ LPDWORD pcbBytesNeeded,
  _Out_ LPDWORD lpServicesReturned,
  _Inout_opt_ LPDWORD lpResumeHandle);

_Must_inspect_result_
BOOL
WINAPI
EnumServicesStatusExA(
  _In_ SC_HANDLE hSCManager,
  _In_ SC_ENUM_TYPE InfoLevel,
  _In_ DWORD dwServiceType,
  _In_ DWORD dwServiceState,
  _Out_writes_bytes_opt_(cbBufSize) LPBYTE lpServices,
  _In_ DWORD cbBufSize,
  _Out_ LPDWORD pcbBytesNeeded,
  _Out_ LPDWORD lpServicesReturned,
  _Inout_opt_ LPDWORD lpResumeHandle,
  _In_opt_ LPCSTR pszGroupName);

_Must_inspect_result_
BOOL
WINAPI
EnumServicesStatusExW(
  _In_ SC_HANDLE hSCManager,
  _In_ SC_ENUM_TYPE InfoLevel,
  _In_ DWORD dwServiceType,
  _In_ DWORD dwServiceState,
  _Out_writes_bytes_opt_(cbBufSize) LPBYTE lpServices,
  _In_ DWORD cbBufSize,
  _Out_ LPDWORD pcbBytesNeeded,
  _Out_ LPDWORD lpServicesReturned,
  _Inout_opt_ LPDWORD lpResumeHandle,
  _In_opt_ LPCWSTR pszGroupName);

_Must_inspect_result_
BOOL
WINAPI
GetServiceDisplayNameA(
  _In_ SC_HANDLE hSCManager,
  _In_ LPCSTR lpServiceName,
  _Out_writes_opt_(*lpcchBuffer) LPSTR lpDisplayName,
  _Inout_ LPDWORD lpcchBuffer);

_Must_inspect_result_
BOOL
WINAPI
GetServiceDisplayNameW(
  _In_ SC_HANDLE hSCManager,
  _In_ LPCWSTR lpServiceName,
  _Out_writes_opt_(*lpcchBuffer) LPWSTR lpDisplayName,
  _Inout_ LPDWORD lpcchBuffer);

_Must_inspect_result_
BOOL
WINAPI
GetServiceKeyNameA(
  _In_ SC_HANDLE hSCManager,
  _In_ LPCSTR lpDisplayName,
  _Out_writes_opt_(*lpcchBuffer) LPSTR lpServiceName,
  _Inout_ LPDWORD lpcchBuffer);

_Must_inspect_result_
BOOL
WINAPI
GetServiceKeyNameW(
  _In_ SC_HANDLE hSCManager,
  _In_ LPCWSTR lpDisplayName,
  _Out_writes_opt_(*lpcchBuffer) LPWSTR lpServiceName,
  _Inout_ LPDWORD lpcchBuffer);

SC_LOCK WINAPI LockServiceDatabase(_In_ SC_HANDLE);
BOOL WINAPI NotifyBootConfigStatus(_In_ BOOL);
_Must_inspect_result_ SC_HANDLE WINAPI OpenSCManagerA(_In_opt_ LPCSTR, _In_opt_ LPCSTR, _In_ DWORD);
_Must_inspect_result_ SC_HANDLE WINAPI OpenSCManagerW(_In_opt_ LPCWSTR, _In_opt_ LPCWSTR, _In_ DWORD);
_Must_inspect_result_ SC_HANDLE WINAPI OpenServiceA(_In_ SC_HANDLE, _In_ LPCSTR, _In_ DWORD);
_Must_inspect_result_ SC_HANDLE WINAPI OpenServiceW(_In_ SC_HANDLE, _In_ LPCWSTR, _In_ DWORD);

#if (NTDDI_VERSION >= NTDDI_VISTA)
DWORD WINAPI NotifyServiceStatusChangeA(_In_ SC_HANDLE, _In_ DWORD, _In_ PSERVICE_NOTIFYA);
DWORD WINAPI NotifyServiceStatusChangeW(_In_ SC_HANDLE, _In_ DWORD, _In_ PSERVICE_NOTIFYW);
#endif

_Must_inspect_result_
BOOL
WINAPI
QueryServiceConfigA(
  _In_ SC_HANDLE hService,
  _Out_writes_bytes_opt_(cbBufSize) LPQUERY_SERVICE_CONFIGA lpServiceConfig,
  _In_ DWORD cbBufSize,
  _Out_ LPDWORD pcbBytesNeeded);

_Must_inspect_result_
BOOL
WINAPI
QueryServiceConfigW(
  _In_ SC_HANDLE hService,
  _Out_writes_bytes_opt_(cbBufSize) LPQUERY_SERVICE_CONFIGW lpServiceConfig,
  _In_ DWORD cbBufSize,
  _Out_ LPDWORD pcbBytesNeeded);

_When_(dwInfoLevel == SERVICE_CONFIG_DESCRIPTION, _At_(cbBufSize, _In_range_(>=, sizeof(LPSERVICE_DESCRIPTIONA))))
_When_(dwInfoLevel == SERVICE_CONFIG_FAILURE_ACTIONS, _At_(cbBufSize, _In_range_(>=, sizeof(LPSERVICE_FAILURE_ACTIONSA))))
_When_(dwInfoLevel == SERVICE_CONFIG_REQUIRED_PRIVILEGES_INFO, _At_(cbBufSize, _In_range_(>=, sizeof(LPSERVICE_REQUIRED_PRIVILEGES_INFOA))))
_Must_inspect_result_
BOOL
WINAPI
QueryServiceConfig2A(
  _In_ SC_HANDLE hService,
  _In_ DWORD dwInfoLevel,
  _Out_writes_bytes_opt_(cbBufSize) LPBYTE lpBuffer,
  _In_ DWORD cbBufSize,
  _Out_ LPDWORD pcbBytesNeeded);

_When_(dwInfoLevel == SERVICE_CONFIG_DESCRIPTION, _At_(cbBufSize, _In_range_(>=, sizeof(LPSERVICE_DESCRIPTIONW))))
_When_(dwInfoLevel == SERVICE_CONFIG_FAILURE_ACTIONS, _At_(cbBufSize, _In_range_(>=, sizeof(LPSERVICE_FAILURE_ACTIONSW))))
_When_(dwInfoLevel == SERVICE_CONFIG_REQUIRED_PRIVILEGES_INFO, _At_(cbBufSize, _In_range_(>=, sizeof(LPSERVICE_REQUIRED_PRIVILEGES_INFOW))))
_Must_inspect_result_
BOOL
WINAPI
QueryServiceConfig2W(
  _In_ SC_HANDLE hService,
  _In_ DWORD dwInfoLevel,
  _Out_writes_bytes_opt_(cbBufSize) LPBYTE lpBuffer,
  _In_ DWORD cbBufSize,
  _Out_ LPDWORD pcbBytesNeeded);

_Must_inspect_result_
BOOL
WINAPI
QueryServiceLockStatusA(
  _In_ SC_HANDLE hSCManager,
  _Out_writes_bytes_opt_(cbBufSize) LPQUERY_SERVICE_LOCK_STATUSA lpLockStatus,
  _In_ DWORD cbBufSize,
  _Out_ LPDWORD pcbBytesNeeded);

_Must_inspect_result_
BOOL
WINAPI
QueryServiceLockStatusW(
  _In_ SC_HANDLE hSCManager,
  _Out_writes_bytes_opt_(cbBufSize) LPQUERY_SERVICE_LOCK_STATUSW lpLockStatus,
  _In_ DWORD cbBufSize,
  _Out_ LPDWORD pcbBytesNeeded);

_Must_inspect_result_
BOOL
WINAPI
QueryServiceObjectSecurity(
  _In_ SC_HANDLE hService,
  _In_ SECURITY_INFORMATION dwSecurityInformation,
  _Out_writes_bytes_opt_(cbBufSize) PSECURITY_DESCRIPTOR lpSecurityDescriptor,
  _In_ DWORD cbBufSize,
  _Out_ LPDWORD pcbBytesNeeded);

_Must_inspect_result_ BOOL WINAPI QueryServiceStatus(_In_ SC_HANDLE, _Out_ LPSERVICE_STATUS);

_Must_inspect_result_
BOOL
WINAPI
QueryServiceStatusEx(
  _In_ SC_HANDLE hService,
  _In_ SC_STATUS_TYPE InfoLevel,
  _Out_writes_bytes_opt_(cbBufSize) LPBYTE lpBuffer,
  _In_ DWORD cbBufSize,
  _Out_ LPDWORD pcbBytesNeeded);

_Must_inspect_result_ SERVICE_STATUS_HANDLE WINAPI RegisterServiceCtrlHandlerA(_In_ LPCSTR, _In_ LPHANDLER_FUNCTION);
_Must_inspect_result_ SERVICE_STATUS_HANDLE WINAPI RegisterServiceCtrlHandlerW(_In_ LPCWSTR, _In_ LPHANDLER_FUNCTION);
_Must_inspect_result_ SERVICE_STATUS_HANDLE WINAPI RegisterServiceCtrlHandlerExA(_In_ LPCSTR, _In_ LPHANDLER_FUNCTION_EX, _In_opt_ LPVOID);
_Must_inspect_result_ SERVICE_STATUS_HANDLE WINAPI RegisterServiceCtrlHandlerExW(_In_ LPCWSTR, _In_ LPHANDLER_FUNCTION_EX, _In_opt_ LPVOID);
BOOL WINAPI SetServiceObjectSecurity(_In_ SC_HANDLE, _In_ SECURITY_INFORMATION, _In_ PSECURITY_DESCRIPTOR);
BOOL WINAPI SetServiceStatus(_In_ SERVICE_STATUS_HANDLE, _In_ LPSERVICE_STATUS);

BOOL
WINAPI
StartServiceA(
  _In_ SC_HANDLE hService,
  _In_ DWORD dwNumServiceArgs,
  _In_reads_opt_(dwNumServiceArgs) LPCSTR *lpServiceArgVectors);

BOOL
WINAPI
StartServiceW(
  _In_ SC_HANDLE hService,
  _In_ DWORD dwNumServiceArgs,
  _In_reads_opt_(dwNumServiceArgs) LPCWSTR *lpServiceArgVectors);

BOOL WINAPI StartServiceCtrlDispatcherA(_In_ const SERVICE_TABLE_ENTRYA*);
BOOL WINAPI StartServiceCtrlDispatcherW(_In_ const SERVICE_TABLE_ENTRYW*);
BOOL WINAPI UnlockServiceDatabase(_In_ SC_LOCK);

#ifdef UNICODE
typedef ENUM_SERVICE_STATUSW ENUM_SERVICE_STATUS,*LPENUM_SERVICE_STATUS;
typedef ENUM_SERVICE_STATUS_PROCESSW ENUM_SERVICE_STATUS_PROCESS;
typedef LPENUM_SERVICE_STATUS_PROCESSW LPENUM_SERVICE_STATUS_PROCESS;
typedef QUERY_SERVICE_CONFIGW QUERY_SERVICE_CONFIG,*LPQUERY_SERVICE_CONFIG;
typedef QUERY_SERVICE_LOCK_STATUSW QUERY_SERVICE_LOCK_STATUS,*LPQUERY_SERVICE_LOCK_STATUS;
typedef SERVICE_TABLE_ENTRYW SERVICE_TABLE_ENTRY,*LPSERVICE_TABLE_ENTRY;
typedef LPSERVICE_MAIN_FUNCTIONW LPSERVICE_MAIN_FUNCTION;
typedef SERVICE_DESCRIPTIONW SERVICE_DESCRIPTION;
typedef LPSERVICE_DESCRIPTIONW LPSERVICE_DESCRIPTION;
typedef SERVICE_FAILURE_ACTIONSW SERVICE_FAILURE_ACTIONS;
typedef LPSERVICE_FAILURE_ACTIONSW LPSERVICE_FAILURE_ACTIONS;
typedef SERVICE_REQUIRED_PRIVILEGES_INFOW SERVICE_REQUIRED_PRIVILEGES_INFO;
typedef LPSERVICE_REQUIRED_PRIVILEGES_INFOW LPSERVICE_REQUIRED_PRIVILEGES_INFO;
typedef SERVICE_NOTIFY_2W SERVICE_NOTIFY_2;
typedef PSERVICE_NOTIFY_2W PSERVICE_NOTIFY_2;
typedef SERVICE_NOTIFYW SERVICE_NOTIFY;
typedef PSERVICE_NOTIFYW PSERVICE_NOTIFY;
typedef SERVICE_CONTROL_STATUS_REASON_PARAMSW SERVICE_CONTROL_STATUS_REASON_PARAMS;
typedef PSERVICE_CONTROL_STATUS_REASON_PARAMSW PSERVICE_CONTROL_STATUS_REASON_PARAMS;
#define SERVICES_ACTIVE_DATABASE SERVICES_ACTIVE_DATABASEW
#define SERVICES_FAILED_DATABASE SERVICES_FAILED_DATABASEW
#define SC_GROUP_IDENTIFIER SC_GROUP_IDENTIFIERW
#define ChangeServiceConfig ChangeServiceConfigW
#define ChangeServiceConfig2 ChangeServiceConfig2W
#define ControlServiceEx ControlServiceExW
#define CreateService CreateServiceW
#define EnumDependentServices EnumDependentServicesW
#define EnumServicesStatus EnumServicesStatusW
#define EnumServicesStatusEx  EnumServicesStatusExW
#define GetServiceDisplayName GetServiceDisplayNameW
#define GetServiceKeyName GetServiceKeyNameW
#if (NTDDI_VERSION >= NTDDI_VISTA)
#define NotifyServiceStatusChange NotifyServiceStatusChangeW
#endif
#define OpenSCManager OpenSCManagerW
#define OpenService OpenServiceW
#define QueryServiceConfig QueryServiceConfigW
#define QueryServiceConfig2 QueryServiceConfig2W
#define QueryServiceLockStatus QueryServiceLockStatusW
#define RegisterServiceCtrlHandler RegisterServiceCtrlHandlerW
#define RegisterServiceCtrlHandlerEx RegisterServiceCtrlHandlerExW
#define StartService StartServiceW
#define StartServiceCtrlDispatcher StartServiceCtrlDispatcherW
#else
typedef ENUM_SERVICE_STATUSA ENUM_SERVICE_STATUS,*LPENUM_SERVICE_STATUS;
typedef ENUM_SERVICE_STATUS_PROCESSA ENUM_SERVICE_STATUS_PROCESS;
typedef LPENUM_SERVICE_STATUS_PROCESSA LPENUM_SERVICE_STATUS_PROCESS;
typedef QUERY_SERVICE_CONFIGA QUERY_SERVICE_CONFIG,*LPQUERY_SERVICE_CONFIG;
typedef QUERY_SERVICE_LOCK_STATUSA QUERY_SERVICE_LOCK_STATUS,*LPQUERY_SERVICE_LOCK_STATUS;
typedef SERVICE_TABLE_ENTRYA SERVICE_TABLE_ENTRY,*LPSERVICE_TABLE_ENTRY;
typedef LPSERVICE_MAIN_FUNCTIONA LPSERVICE_MAIN_FUNCTION;
typedef SERVICE_DESCRIPTIONA SERVICE_DESCRIPTION;
typedef LPSERVICE_DESCRIPTIONA LPSERVICE_DESCRIPTION;
typedef SERVICE_FAILURE_ACTIONSA SERVICE_FAILURE_ACTIONS;
typedef LPSERVICE_FAILURE_ACTIONSA LPSERVICE_FAILURE_ACTIONS;
typedef SERVICE_REQUIRED_PRIVILEGES_INFOA SERVICE_REQUIRED_PRIVILEGES_INFO;
typedef LPSERVICE_REQUIRED_PRIVILEGES_INFOA LPSERVICE_REQUIRED_PRIVILEGES_INFO;
typedef SERVICE_NOTIFY_2A SERVICE_NOTIFY_2;
typedef PSERVICE_NOTIFY_2A PSERVICE_NOTIFY_2;
typedef SERVICE_NOTIFYA SERVICE_NOTIFY;
typedef PSERVICE_NOTIFYA PSERVICE_NOTIFY;
typedef SERVICE_CONTROL_STATUS_REASON_PARAMSA SERVICE_CONTROL_STATUS_REASON_PARAMS;
typedef PSERVICE_CONTROL_STATUS_REASON_PARAMSA PSERVICE_CONTROL_STATUS_REASON_PARAMS;
#define SERVICES_ACTIVE_DATABASE SERVICES_ACTIVE_DATABASEA
#define SERVICES_FAILED_DATABASE SERVICES_FAILED_DATABASEA
#define SC_GROUP_IDENTIFIER SC_GROUP_IDENTIFIERA
#define ChangeServiceConfig ChangeServiceConfigA
#define ChangeServiceConfig2 ChangeServiceConfig2A
#define ControlServiceEx ControlServiceExA
#define CreateService CreateServiceA
#define EnumDependentServices EnumDependentServicesA
#define EnumServicesStatus EnumServicesStatusA
#define EnumServicesStatusEx  EnumServicesStatusExA
#define GetServiceDisplayName GetServiceDisplayNameA
#define GetServiceKeyName GetServiceKeyNameA
#define OpenSCManager OpenSCManagerA
#define OpenService OpenServiceA
#if (NTDDI_VERSION >= NTDDI_VISTA)
#define NotifyServiceStatusChange NotifyServiceStatusChangeA
#endif
#define QueryServiceConfig QueryServiceConfigA
#define QueryServiceConfig2 QueryServiceConfig2A
#define QueryServiceLockStatus QueryServiceLockStatusA
#define RegisterServiceCtrlHandler RegisterServiceCtrlHandlerA
#define RegisterServiceCtrlHandlerEx RegisterServiceCtrlHandlerExA
#define StartService StartServiceA
#define StartServiceCtrlDispatcher StartServiceCtrlDispatcherA
#endif
#ifdef __cplusplus
}
#endif
#endif /* _WINSVC_H */
