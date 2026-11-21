#ifndef _POWRPROF_H
#define _POWRPROF_H

#ifndef __WINE_WINTERNL_H
/* FIXME: Include this, so NTSTATUS gets defined.
   Not very clean, but I can't get the hacH^H^H^override used
   by official PSDK to work. */
#include <ntsecapi.h>
#endif

#include <powersetting.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __REACTOS__
#define NEWSCHEME (UINT)-1

typedef enum _POWER_PLATFORM_ROLE {
    PlatformRoleUnspecified,
    PlatformRoleDesktop,
    PlatformRoleMobile,
    PlatformRoleWorkstation,
    PlatformRoleEnterpriseServer,
    PlatformRoleSOHOServer,
    PlatformRoleAppliancePC,
    PlatformRolePerformanceServer,
    PlatformRoleSlate,
    PlatformRoleMaximum
} POWER_PLATFORM_ROLE, *PPOWER_PLATFORM_ROLE;
#endif

#define EnableSysTrayBatteryMeter	0x01
#define EnableMultiBatteryDisplay	0x02
#define EnablePasswordLogon		    0x04
#define EnableWakeOnRing		    0x08
#define EnableVideoDimDisplay		0x10

typedef struct _GLOBAL_MACHINE_POWER_POLICY {
    ULONG Revision;
    SYSTEM_POWER_STATE LidOpenWakeAc;
    SYSTEM_POWER_STATE LidOpenWakeDc;
    ULONG BroadcastCapacityResolution;
} GLOBAL_MACHINE_POWER_POLICY, 
*PGLOBAL_MACHINE_POWER_POLICY;

typedef struct _GLOBAL_USER_POWER_POLICY {
    ULONG Revision;
    POWER_ACTION_POLICY PowerButtonAc;
    POWER_ACTION_POLICY PowerButtonDc;
    POWER_ACTION_POLICY SleepButtonAc;
    POWER_ACTION_POLICY SleepButtonDc;
    POWER_ACTION_POLICY LidCloseAc;
    POWER_ACTION_POLICY LidCloseDc;
    SYSTEM_POWER_LEVEL DischargePolicy[NUM_DISCHARGE_POLICIES];
    ULONG GlobalFlags;
} GLOBAL_USER_POWER_POLICY, 
*PGLOBAL_USER_POWER_POLICY;

typedef struct _GLOBAL_POWER_POLICY {
    GLOBAL_USER_POWER_POLICY user;
    GLOBAL_MACHINE_POWER_POLICY mach;
} GLOBAL_POWER_POLICY, 
*PGLOBAL_POWER_POLICY;

typedef struct _MACHINE_POWER_POLICY {
    ULONG Revision;
    SYSTEM_POWER_STATE MinSleepAc;
    SYSTEM_POWER_STATE MinSleepDc;
    SYSTEM_POWER_STATE ReducedLatencySleepAc;
    SYSTEM_POWER_STATE ReducedLatencySleepDc;
    ULONG DozeTimeoutAc;
    ULONG DozeTimeoutDc;
    ULONG DozeS4TimeoutAc;
    ULONG DozeS4TimeoutDc;
    UCHAR MinThrottleAc;
    UCHAR MinThrottleDc;
    UCHAR pad1[2];
    POWER_ACTION_POLICY OverThrottledAc;
    POWER_ACTION_POLICY OverThrottledDc;
} MACHINE_POWER_POLICY, 
*PMACHINE_POWER_POLICY;

typedef struct _MACHINE_PROCESSOR_POWER_POLICY {
    ULONG Revision;
    PROCESSOR_POWER_POLICY ProcessorPolicyAc;
    PROCESSOR_POWER_POLICY ProcessorPolicyDc;
} MACHINE_PROCESSOR_POWER_POLICY, 
*PMACHINE_PROCESSOR_POWER_POLICY;

typedef struct _USER_POWER_POLICY {
    ULONG Revision;
    POWER_ACTION_POLICY IdleAc;
    POWER_ACTION_POLICY IdleDc;
    ULONG IdleTimeoutAc;
    ULONG IdleTimeoutDc;
    UCHAR IdleSensitivityAc;
    UCHAR IdleSensitivityDc;
    UCHAR ThrottlePolicyAc;
    UCHAR ThrottlePolicyDc;
    SYSTEM_POWER_STATE MaxSleepAc;
    SYSTEM_POWER_STATE MaxSleepDc;
    ULONG Reserved[2];
    ULONG VideoTimeoutAc;
    ULONG VideoTimeoutDc;
    ULONG SpindownTimeoutAc;
    ULONG SpindownTimeoutDc;
    BOOLEAN OptimizeForPowerAc;
    BOOLEAN OptimizeForPowerDc;
    UCHAR FanThrottleToleranceAc;
    UCHAR FanThrottleToleranceDc;
    UCHAR ForcedThrottleAc;
    UCHAR ForcedThrottleDc;
} USER_POWER_POLICY, 
*PUSER_POWER_POLICY;

typedef struct _POWER_POLICY {
    USER_POWER_POLICY user;
    MACHINE_POWER_POLICY mach;
} POWER_POLICY, 
*PPOWER_POLICY;

typedef enum _POWER_DATA_ACCESSOR {
    ACCESS_AC_POWER_SETTING_INDEX,
    ACCESS_DC_POWER_SETTING_INDEX,
    ACCESS_FRIENDLY_NAME,
    ACCESS_DESCRIPTION,
    ACCESS_POSSIBLE_POWER_SETTING,
    ACCESS_POSSIBLE_POWER_SETTING_FRIENDLY_NAME,
    ACCESS_POSSIBLE_POWER_SETTING_DESCRIPTION,
    ACCESS_DEFAULT_AC_POWER_SETTING,
    ACCESS_DEFAULT_DC_POWER_SETTING,
    ACCESS_POSSIBLE_VALUE_MIN,
    ACCESS_POSSIBLE_VALUE_MAX,
    ACCESS_POSSIBLE_VALUE_INCREMENT,
    ACCESS_POSSIBLE_VALUE_UNITS,
    ACCESS_ICON_RESOURCE,
    ACCESS_DEFAULT_SECURITY_DESCRIPTOR,
    ACCESS_ATTRIBUTES,
    ACCESS_SCHEME,
    ACCESS_SUBGROUP,
    ACCESS_INDIVIDUAL_SETTING,
    ACCESS_ACTIVE_SCHEME,
    ACCESS_CREATE_SCHEME,
    ACCESS_AC_POWER_SETTING_MAX,
    ACCESS_DC_POWER_SETTING_MAX,
    ACCESS_AC_POWER_SETTING_MIN,
    ACCESS_DC_POWER_SETTING_MIN,
    ACCESS_PROFILE,
    ACCESS_OVERLAY_SCHEME,
    ACCESS_ACTIVE_OVERLAY_SCHEME,
} POWER_DATA_ACCESSOR, *PPOWER_DATA_ACCESSOR;

typedef BOOLEAN (CALLBACK* PWRSCHEMESENUMPROC)(UINT, DWORD, LPWSTR, DWORD, LPWSTR, PPOWER_POLICY, LPARAM);

#ifndef _HPOWERNOTIFY_DEF_
#define _HPOWERNOTIFY_DEF_
typedef PVOID HPOWERNOTIFY, *PHPOWERNOTIFY;
#endif

NTSTATUS WINAPI CallNtPowerInformation(POWER_INFORMATION_LEVEL, PVOID, ULONG, PVOID, ULONG);
BOOLEAN WINAPI CanUserWritePwrScheme(VOID);
BOOLEAN WINAPI DeletePwrScheme(UINT);
BOOLEAN WINAPI EnumPwrSchemes(PWRSCHEMESENUMPROC, LPARAM);
BOOLEAN WINAPI GetActivePwrScheme(PUINT);
BOOLEAN WINAPI GetCurrentPowerPolicies(PGLOBAL_POWER_POLICY, PPOWER_POLICY);
BOOLEAN WINAPI GetPwrCapabilities(PSYSTEM_POWER_CAPABILITIES);
BOOLEAN WINAPI GetPwrDiskSpindownRange(PUINT, PUINT);
BOOLEAN WINAPI IsAdminOverrideActive(PADMINISTRATOR_POWER_POLICY);
BOOLEAN WINAPI IsPwrHibernateAllowed(VOID);
BOOLEAN WINAPI IsPwrShutdownAllowed(VOID);
BOOLEAN WINAPI IsPwrSuspendAllowed(VOID);
DWORD   WINAPI PowerEnumerate(HKEY, const GUID *, const GUID *, POWER_DATA_ACCESSOR, ULONG, UCHAR *, DWORD *);
#if (NTDDI_VERSION >= NTDDI_VISTA)
DWORD   WINAPI PowerDeleteScheme(_In_opt_ HKEY, _In_ const GUID *);
DWORD   WINAPI PowerDuplicateScheme(_In_opt_ HKEY, _In_ const GUID *, _Inout_ GUID **);
DWORD   WINAPI PowerReadFriendlyName(_In_opt_ HKEY, _In_opt_ const GUID *, _In_opt_ const GUID *, _In_opt_ const GUID *, _Out_writes_bytes_opt_(*BufferSize) PUCHAR, _Inout_ LPDWORD BufferSize);
DWORD   WINAPI PowerReadDescription(_In_opt_ HKEY, _In_opt_ const GUID *, _In_opt_ const GUID *, _In_opt_ const GUID *, _Out_writes_bytes_opt_(*BufferSize) PUCHAR, _Inout_ LPDWORD BufferSize);
DWORD   WINAPI PowerReadACDefaultIndex(_In_opt_ HKEY, _In_ const GUID *, _In_opt_ const GUID *, _In_ const GUID *, _Out_ LPDWORD);
DWORD   WINAPI PowerReadACValueIndex(_In_opt_ HKEY, _In_opt_ const GUID *, _In_opt_ const GUID *, _In_opt_ const GUID *, _Out_ LPDWORD);
DWORD   WINAPI PowerReadDCDefaultIndex(_In_opt_ HKEY, _In_ const GUID *, _In_opt_ const GUID *, _In_ const GUID *, _Out_ LPDWORD);
DWORD   WINAPI PowerReadDCValueIndex(_In_opt_ HKEY, _In_opt_ const GUID *, _In_opt_ const GUID *, _In_opt_ const GUID *, _Out_ LPDWORD);
#endif
DWORD   WINAPI PowerRegisterSuspendResumeNotification(DWORD, HANDLE, PHPOWERNOTIFY);
DWORD   WINAPI PowerUnregisterSuspendResumeNotification(HPOWERNOTIFY);
#if (NTDDI_VERSION >= NTDDI_VISTA)
DWORD   WINAPI PowerSettingAccessCheck(_In_ POWER_DATA_ACCESSOR, _In_opt_ const GUID *);
#endif
#if (NTDDI_VERSION >= NTDDI_WIN7)
DWORD   WINAPI PowerSettingAccessCheckEx(_In_ POWER_DATA_ACCESSOR, _In_opt_ const GUID *, _In_ REGSAM);
#endif
DWORD   WINAPI PowerSettingRegisterNotification(const GUID *, DWORD, HANDLE, PHPOWERNOTIFY);
DWORD   WINAPI PowerSettingUnregisterNotification(HPOWERNOTIFY);
DWORD   WINAPI PowerCreatePossibleSetting(_In_opt_ HKEY, _In_ const GUID *, _In_ const GUID *, _In_ ULONG);
DWORD   WINAPI PowerCreateSetting(_In_opt_ HKEY, _In_ const GUID *, _In_ const GUID *);
DWORD   WINAPI PowerRemovePowerSetting(_In_ const GUID *, _In_ const GUID *);
DWORD   WINAPI PowerWriteACDefaultIndex(_In_opt_ HKEY, _In_ const GUID *, _In_opt_ const GUID *, _In_ const GUID *, _In_ DWORD);
DWORD   WINAPI PowerWriteDCDefaultIndex(_In_opt_ HKEY, _In_ const GUID *, _In_opt_ const GUID *, _In_ const GUID *, _In_ DWORD);
DWORD   WINAPI PowerWriteDescription(_In_opt_ HKEY, _In_ const GUID *, _In_opt_ const GUID *, _In_opt_ const GUID *, _In_reads_bytes_(BufferSize) UCHAR *, _In_ DWORD BufferSize);
DWORD   WINAPI PowerWriteFriendlyName(_In_opt_ HKEY, _In_ const GUID *, _In_opt_ const GUID *, _In_opt_ const GUID *, _In_reads_bytes_(BufferSize) UCHAR *, _In_ DWORD BufferSize);
DWORD   WINAPI PowerWritePossibleDescription(_In_opt_ HKEY, _In_opt_ const GUID *, _In_opt_ const GUID *, _In_ ULONG, _In_reads_bytes_(BufferSize) UCHAR *, _In_ DWORD BufferSize);
DWORD   WINAPI PowerWritePossibleFriendlyName(_In_opt_ HKEY, _In_opt_ const GUID *, _In_opt_ const GUID *, _In_ ULONG, _In_reads_bytes_(BufferSize) UCHAR *, _In_ DWORD BufferSize);
DWORD   WINAPI PowerWritePossibleValue(_In_opt_ HKEY, _In_opt_ const GUID *, _In_opt_ const GUID *, _In_ ULONG, _In_ ULONG, _In_reads_bytes_(BufferSize) UCHAR *, _In_ DWORD BufferSize);
DWORD   WINAPI PowerWriteSettingAttributes(_In_opt_ const GUID *, _In_opt_ const GUID *, _In_ DWORD);
DWORD   WINAPI PowerWriteValueIncrement(_In_opt_ HKEY, _In_opt_ const GUID *, _In_opt_ const GUID *, _In_ DWORD);
DWORD   WINAPI PowerWriteValueMax(_In_opt_ HKEY, _In_opt_ const GUID *, _In_opt_ const GUID *, _In_ DWORD);
DWORD   WINAPI PowerWriteValueMin(_In_opt_ HKEY, _In_opt_ const GUID *, _In_opt_ const GUID *, _In_ DWORD);
DWORD   WINAPI PowerWriteValueUnitsSpecifier(_In_opt_ HKEY, _In_opt_ const GUID *, _In_opt_ const GUID *, _In_reads_bytes_(BufferSize) UCHAR *, _In_ DWORD BufferSize);
BOOLEAN WINAPI ReadGlobalPwrPolicy(PGLOBAL_POWER_POLICY);
BOOLEAN WINAPI ReadProcessorPwrScheme(UINT, PMACHINE_PROCESSOR_POWER_POLICY);
BOOLEAN WINAPI ReadPwrScheme(UINT, PPOWER_POLICY);
BOOLEAN WINAPI SetActivePwrScheme(UINT, PGLOBAL_POWER_POLICY, PPOWER_POLICY);
BOOLEAN WINAPI SetSuspendState(BOOLEAN, BOOLEAN, BOOLEAN);
BOOLEAN WINAPI WriteGlobalPwrPolicy(PGLOBAL_POWER_POLICY);
BOOLEAN WINAPI WriteProcessorPwrScheme(UINT, PMACHINE_PROCESSOR_POWER_POLICY);
BOOLEAN WINAPI ValidatePowerPolicies(PGLOBAL_POWER_POLICY, PPOWER_POLICY);
BOOLEAN WINAPI WritePwrScheme(PUINT, LPWSTR, LPWSTR, PPOWER_POLICY);

/* Power scheme */
POWER_PLATFORM_ROLE WINAPI PowerDeterminePlatformRole(void);

#ifdef __cplusplus
}
#endif

#ifndef DEVICEPOWER_AND_OPERATION
#define DEVICEPOWER_AND_OPERATION 0x40000000
#endif
#ifndef DEVICEPOWER_CLEAR_WAKEENABLED
#define DEVICEPOWER_CLEAR_WAKEENABLED 0x00000002
#endif
#ifndef DEVICEPOWER_FILTER_DEVICES_PRESENT
#define DEVICEPOWER_FILTER_DEVICES_PRESENT 0x20000000
#endif
#ifndef DEVICEPOWER_FILTER_HARDWARE
#define DEVICEPOWER_FILTER_HARDWARE 0x10000000
#endif
#ifndef DEVICEPOWER_FILTER_ON_NAME
#define DEVICEPOWER_FILTER_ON_NAME 0x02000000
#endif
#ifndef DEVICEPOWER_FILTER_WAKEENABLED
#define DEVICEPOWER_FILTER_WAKEENABLED 0x08000000
#endif
#ifndef DEVICEPOWER_FILTER_WAKEPROGRAMMABLE
#define DEVICEPOWER_FILTER_WAKEPROGRAMMABLE 0x04000000
#endif
#ifndef DEVICEPOWER_HARDWAREID
#define DEVICEPOWER_HARDWAREID 0x80000000
#endif
#ifndef DEVICEPOWER_SET_WAKEENABLED
#define DEVICEPOWER_SET_WAKEENABLED 0x00000001
#endif
#ifndef DEVICE_NOTIFY_CALLBACK
#define DEVICE_NOTIFY_CALLBACK 2
#endif
#ifndef PDCAP_S0_SUPPORTED
#define PDCAP_S0_SUPPORTED 0x00010000
#endif
#ifndef PDCAP_S1_SUPPORTED
#define PDCAP_S1_SUPPORTED 0x00020000
#endif
#ifndef PDCAP_S2_SUPPORTED
#define PDCAP_S2_SUPPORTED 0x00040000
#endif
#ifndef PDCAP_S3_SUPPORTED
#define PDCAP_S3_SUPPORTED 0x00080000
#endif
#ifndef PDCAP_S4_SUPPORTED
#define PDCAP_S4_SUPPORTED 0x01000000
#endif
#ifndef PDCAP_S5_SUPPORTED
#define PDCAP_S5_SUPPORTED 0x02000000
#endif
#ifndef PDCAP_WAKE_FROM_S0_SUPPORTED
#define PDCAP_WAKE_FROM_S0_SUPPORTED 0x00100000
#endif
#ifndef PDCAP_WAKE_FROM_S1_SUPPORTED
#define PDCAP_WAKE_FROM_S1_SUPPORTED 0x00200000
#endif
#ifndef PDCAP_WAKE_FROM_S2_SUPPORTED
#define PDCAP_WAKE_FROM_S2_SUPPORTED 0x00400000
#endif
#ifndef PDCAP_WAKE_FROM_S3_SUPPORTED
#define PDCAP_WAKE_FROM_S3_SUPPORTED 0x00800000
#endif
#ifndef POWER_ATTRIBUTE_HIDE
#define POWER_ATTRIBUTE_HIDE 0x00000001
#endif
#ifndef POWER_ATTRIBUTE_SHOW_AOAC
#define POWER_ATTRIBUTE_SHOW_AOAC 0x00000002
#endif
#ifndef THERMAL_EVENT_VERSION
#define THERMAL_EVENT_VERSION 1
#endif

#endif /* _POWRPROF_H */
