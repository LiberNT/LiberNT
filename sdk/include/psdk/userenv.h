#ifndef _USERENV_H
#define _USERENV_H

#ifdef __cplusplus
extern "C" {
#endif

#define PI_NOUI (1)
#define PI_APPLYPOLICY (2)

#if (WINVER >= 0x0500)
#define RP_FORCE (1)
#endif

/* Values returned by GetProfileType */
#if (WINVER >= 0x0500)
#define PT_TEMPORARY 1
#define PT_ROAMING   2
#define PT_MANDATORY 4
#endif

typedef struct _PROFILEINFOA
{
  DWORD dwSize;
  DWORD dwFlags;
  LPSTR lpUserName;
  LPSTR lpProfilePath;
  LPSTR lpDefaultPath;
  LPSTR lpServerName;
  LPSTR lpPolicyPath;
  HANDLE hProfile;
} PROFILEINFOA, *LPPROFILEINFOA;

typedef struct _PROFILEINFOW
{
  DWORD dwSize;
  DWORD dwFlags;
  LPWSTR lpUserName;
  LPWSTR lpProfilePath;
  LPWSTR lpDefaultPath;
  LPWSTR lpServerName;
  LPWSTR lpPolicyPath;
  HANDLE hProfile;
} PROFILEINFOW, *LPPROFILEINFOW;

#if (WINVER >= 0x0502)
typedef enum _GPO_LINK {
    GPLinkUnknown = 0,
    GPLinkMachine,
    GPLinkSite,
    GPLinkDomain,
    GPLinkOrganizationalUnit
} GPO_LINK, *PGPO_LINK;

typedef struct _GROUP_POLICY_OBJECTA {
    DWORD dwOptions;
    DWORD dwVersion;
    LPSTR lpDSPath;
    LPSTR lpFileSysPath;
    LPSTR lpDisplayName;
    CHAR szGPOName[50];
    GPO_LINK GPOLink;
    LPARAM lParam;
    struct _GROUP_POLICY_OBJECTA *pNext;
    struct _GROUP_POLICY_OBJECTA *pPrev;
    LPSTR lpExtensions;
    LPARAM lParam2;
    LPSTR lpLink;
} GROUP_POLICY_OBJECTA, *PGROUP_POLICY_OBJECTA;

typedef struct _GROUP_POLICY_OBJECTW {
    DWORD dwOptions;
    DWORD dwVersion;
    LPWSTR lpDSPath;
    LPWSTR lpFileSysPath;
    LPWSTR lpDisplayName;
    WCHAR szGPOName[50];
    GPO_LINK GPOLink;
    LPARAM lParam;
    struct _GROUP_POLICY_OBJECTW *pNext;
    struct _GROUP_POLICY_OBJECTW *pPrev;
    LPWSTR lpExtensions;
    LPARAM lParam2;
    LPWSTR lpLink;
} GROUP_POLICY_OBJECTW, *PGROUP_POLICY_OBJECTW;
#endif

/* begin private */
BOOL WINAPI InitializeProfiles (VOID);
BOOL WINAPI CreateUserProfileA (PSID, LPCSTR);
BOOL WINAPI CreateUserProfileW (PSID, LPCWSTR);
BOOL WINAPI CreateUserProfileExA (PSID, LPCSTR, LPCSTR, LPSTR, DWORD, BOOL);
BOOL WINAPI CreateUserProfileExW (PSID, LPCWSTR, LPCWSTR, LPWSTR, DWORD, BOOL);
BOOL WINAPI AddDesktopItemA (BOOL, LPCSTR, LPCSTR, LPCSTR, INT, LPCSTR, WORD, INT);
BOOL WINAPI AddDesktopItemW (BOOL, LPCWSTR, LPCWSTR, LPCWSTR, INT, LPCWSTR, WORD, INT);
BOOL WINAPI DeleteDesktopItemA (BOOL, LPCSTR);
BOOL WINAPI DeleteDesktopItemW (BOOL, LPCWSTR);
BOOL WINAPI CreateGroupA (LPCSTR, BOOL);
BOOL WINAPI CreateGroupW (LPCWSTR, BOOL);
BOOL WINAPI DeleteGroupA (LPCSTR, BOOL);
BOOL WINAPI DeleteGroupW (LPCWSTR, BOOL);
BOOL WINAPI AddItemA (LPCSTR, BOOL, LPCSTR, LPCSTR, LPCSTR, INT, LPCSTR, WORD, INT);
BOOL WINAPI AddItemW (LPCWSTR, BOOL, LPCWSTR, LPCWSTR, LPCWSTR, INT, LPCWSTR, WORD, INT);
BOOL WINAPI DeleteItemA (LPCSTR, BOOL, LPCSTR, BOOL);
BOOL WINAPI DeleteItemW (LPCWSTR, BOOL, LPCWSTR, BOOL);
BOOL WINAPI CopyProfileDirectoryA(LPCSTR, LPCSTR, DWORD);
BOOL WINAPI CopyProfileDirectoryW(LPCWSTR, LPCWSTR, DWORD);
PSID WINAPI GetUserSid(HANDLE);
BOOL WINAPI CopySystemProfile(ULONG);
/* end private */

#if (_WIN32_WINNT >= 0x0602)
HRESULT WINAPI CreateAppContainerProfile(PCWSTR, PCWSTR, PCWSTR, PSID_AND_ATTRIBUTES, DWORD, PSID*);
HRESULT WINAPI DeleteAppContainerProfile(PCWSTR);
HRESULT WINAPI DeriveAppContainerSidFromAppContainerName(PCWSTR, PSID*);
HRESULT WINAPI GetAppContainerFolderPath(_In_ PCWSTR,_Outptr_ PWSTR*);
HRESULT WINAPI GetAppContainerRegistryLocation(DWORD, HKEY *);
#endif

#if(WINVER >= 0x0500)
BOOL WINAPI DeleteProfileA(LPCSTR, LPCSTR, LPCSTR);
BOOL WINAPI DeleteProfileW(LPCWSTR, LPCWSTR, LPCWSTR);
#endif
BOOL WINAPI LoadUserProfileA (HANDLE, LPPROFILEINFOA);
BOOL WINAPI LoadUserProfileW (HANDLE, LPPROFILEINFOW);
BOOL WINAPI UnloadUserProfile (HANDLE, HANDLE);

BOOL WINAPI GetAllUsersProfileDirectoryA (LPSTR, LPDWORD);
BOOL WINAPI GetAllUsersProfileDirectoryW (LPWSTR, LPDWORD);
BOOL WINAPI GetDefaultUserProfileDirectoryA (LPSTR, LPDWORD);
BOOL WINAPI GetDefaultUserProfileDirectoryW (LPWSTR, LPDWORD);
BOOL WINAPI GetProfilesDirectoryA(LPSTR, LPDWORD);
BOOL WINAPI GetProfilesDirectoryW(LPWSTR, LPDWORD);
BOOL WINAPI GetUserProfileDirectoryA(HANDLE, LPSTR, LPDWORD);
BOOL WINAPI GetUserProfileDirectoryW(HANDLE, LPWSTR, LPDWORD);
#if (WINVER >= 0x0500)
BOOL WINAPI GetProfileType(PDWORD);
#endif

BOOL WINAPI CreateEnvironmentBlock(LPVOID*, HANDLE, BOOL);
BOOL WINAPI DestroyEnvironmentBlock(LPVOID);
#if (WINVER >= 0x0500)
BOOL WINAPI ExpandEnvironmentStringsForUserA (HANDLE, LPCSTR, LPSTR, DWORD);
BOOL WINAPI ExpandEnvironmentStringsForUserW (HANDLE, LPCWSTR, LPWSTR, DWORD);
#endif

#if (WINVER >= 0x0502)
DWORD
WINAPI
GetAppliedGPOListA(
    _In_   DWORD dwFlags,
    _In_   LPCSTR pMachineName,
    _In_   PSID pSidUser,
    _In_   GUID *pGuidExtension,
    _Out_  PGROUP_POLICY_OBJECTA *ppGPOList
);
DWORD
WINAPI
GetAppliedGPOListW(
    _In_   DWORD dwFlags,
    _In_   LPCWSTR pMachineName,
    _In_   PSID pSidUser,
    _In_   GUID *pGuidExtension,
    _Out_  PGROUP_POLICY_OBJECTW *ppGPOList
);
#endif

HANDLE WINAPI EnterCriticalPolicySection (BOOL);
BOOL WINAPI LeaveCriticalPolicySection (HANDLE);
BOOL WINAPI RefreshPolicy (BOOL);
#if (WINVER >= 0x0500)
BOOL WINAPI RefreshPolicyEx (BOOL, DWORD);
#endif
BOOL WINAPI RegisterGPNotification (HANDLE, BOOL);
BOOL WINAPI UnregisterGPNotification (HANDLE);

#ifdef UNICODE
typedef PROFILEINFOW PROFILEINFO;
typedef LPPROFILEINFOW LPPROFILEINFO;
/* begin private */
#define CreateUserProfile  CreateUserProfileW
#define CreateUserProfileEx  CreateUserProfileExW
#define AddDesktopItem  AddDesktopItemW
#define DeleteDesktopItem  DeleteDesktopItemW
#define CreateGroup  CreateGroupW
#define DeleteGroup  DeleteGroupW
#define AddItem  AddItemW
#define DeleteItem  DeleteItemW
#define CopyProfileDirectory  CopyProfileDirectoryW
/* end private */
#if (WINVER >= 0x0500)
#define DeleteProfile  DeleteProfileW
#endif
#define LoadUserProfile  LoadUserProfileW
#define GetAllUsersProfileDirectory  GetAllUsersProfileDirectoryW
#define GetDefaultUserProfileDirectory  GetDefaultUserProfileDirectoryW
#define GetProfilesDirectory  GetProfilesDirectoryW
#define GetUserProfileDirectory  GetUserProfileDirectoryW
#if (WINVER >= 0x0500)
#define ExpandEnvironmentStringsForUser ExpandEnvironmentStringsForUserW
#endif
#if (WINVER >= 0x0502)
typedef GROUP_POLICY_OBJECTW GROUP_POLICY_OBJECT;
typedef PGROUP_POLICY_OBJECTW PGROUP_POLICY_OBJECT;
#define GetAppliedGPOList GetAppliedGPOListW
#endif
#else
typedef PROFILEINFOA PROFILEINFO;
typedef LPPROFILEINFOA LPPROFILEINFO;
/* begin private */
#define CreateUserProfile  CreateUserProfileA
#define CreateUserProfileEx  CreateUserProfileExA
#define AddDesktopItem  AddDesktopItemA
#define DeleteDesktopItem  DeleteDesktopItemA
#define CreateGroup  CreateGroupA
#define DeleteGroup  DeleteGroupA
#define AddItem  AddItemA
#define DeleteItem  DeleteItemA
#define CopyProfileDirectory  CopyProfileDirectoryA
/* end private */
#if (WINVER >= 0x0500)
#define DeleteProfile  DeleteProfileA
#endif
#define LoadUserProfile  LoadUserProfileA
#define GetAllUsersProfileDirectory  GetAllUsersProfileDirectoryA
#define GetDefaultUserProfileDirectory  GetDefaultUserProfileDirectoryA
#define GetProfilesDirectory  GetProfilesDirectoryA
#define GetUserProfileDirectory  GetUserProfileDirectoryA
#if (WINVER >= 0x0500)
#define ExpandEnvironmentStringsForUser ExpandEnvironmentStringsForUserA
#endif
#if (WINVER >= 0x0502)
typedef GROUP_POLICY_OBJECTA GROUP_POLICY_OBJECT;
typedef PGROUP_POLICY_OBJECTA PGROUP_POLICY_OBJECT;
#define GetAppliedGPOList GetAppliedGPOListA
#endif
#endif

#ifdef __cplusplus
}
#endif

#ifndef FLAG_ASSUME_COMP_WQLFILTER_TRUE
#define FLAG_ASSUME_COMP_WQLFILTER_TRUE 0x02000000
#endif
#ifndef FLAG_ASSUME_SLOW_LINK
#define FLAG_ASSUME_SLOW_LINK 0x20000000
#endif
#ifndef FLAG_ASSUME_USER_WQLFILTER_TRUE
#define FLAG_ASSUME_USER_WQLFILTER_TRUE 0x04000000
#endif
#ifndef FLAG_FORCE_CREATENAMESPACE
#define FLAG_FORCE_CREATENAMESPACE 0x00000004
#endif
#ifndef FLAG_LOOPBACK_MERGE
#define FLAG_LOOPBACK_MERGE 0x10000000
#endif
#ifndef FLAG_LOOPBACK_REPLACE
#define FLAG_LOOPBACK_REPLACE 0x08000000
#endif
#ifndef FLAG_NO_COMPUTER
#define FLAG_NO_COMPUTER 0x00000002
#endif
#ifndef FLAG_NO_CSE_INVOKE
#define FLAG_NO_CSE_INVOKE 0x40000000
#endif
#ifndef FLAG_NO_USER
#define FLAG_NO_USER 0x00000001
#endif
#ifndef FLAG_PLANNING_MODE
#define FLAG_PLANNING_MODE 0x01000000
#endif
#ifndef GPC_BLOCK_POLICY
#define GPC_BLOCK_POLICY 0x00000001
#endif
#ifndef GPO_FLAG_DISABLE
#define GPO_FLAG_DISABLE 0x00000001
#endif
#ifndef GPO_FLAG_FORCE
#define GPO_FLAG_FORCE 0x00000002
#endif
#ifndef GPO_INFO_FLAG_ASYNC_FOREGROUND
#define GPO_INFO_FLAG_ASYNC_FOREGROUND 0x00001000
#endif
#ifndef GPO_INFO_FLAG_BACKGROUND
#define GPO_INFO_FLAG_BACKGROUND 0x00000010
#endif
#ifndef GPO_INFO_FLAG_FORCED_REFRESH
#define GPO_INFO_FLAG_FORCED_REFRESH 0x00000400
#endif
#ifndef GPO_INFO_FLAG_LINKTRANSITION
#define GPO_INFO_FLAG_LINKTRANSITION 0x00000100
#endif
#ifndef GPO_INFO_FLAG_LOGRSOP_TRANSITION
#define GPO_INFO_FLAG_LOGRSOP_TRANSITION 0x00000200
#endif
#ifndef GPO_INFO_FLAG_NOCHANGES
#define GPO_INFO_FLAG_NOCHANGES 0x00000080
#endif
#ifndef GPO_INFO_FLAG_SAFEMODE_BOOT
#define GPO_INFO_FLAG_SAFEMODE_BOOT 0x00000800
#endif
#ifndef GPO_INFO_FLAG_SLOWLINK
#define GPO_INFO_FLAG_SLOWLINK 0x00000020
#endif
#ifndef GPO_INFO_FLAG_VERBOSE
#define GPO_INFO_FLAG_VERBOSE 0x00000040
#endif
#ifndef GPO_LIST_FLAG_MACHINE
#define GPO_LIST_FLAG_MACHINE 0x00000001
#endif
#ifndef GPO_LIST_FLAG_NO_SECURITYFILTERS
#define GPO_LIST_FLAG_NO_SECURITYFILTERS 0x00000008
#endif
#ifndef GPO_LIST_FLAG_NO_WMIFILTERS
#define GPO_LIST_FLAG_NO_WMIFILTERS 0x00000004
#endif
#ifndef GPO_LIST_FLAG_SITEONLY
#define GPO_LIST_FLAG_SITEONLY 0x00000002
#endif
#ifndef PT_ROAMING_PREEXISTING
#define PT_ROAMING_PREEXISTING 0x00000008
#endif
#ifndef RP_SYNC
#define RP_SYNC 2
#endif
#ifndef RSOP_COMPUTER_ACCESS_DENIED
#define RSOP_COMPUTER_ACCESS_DENIED 0x00000002
#endif
#ifndef RSOP_TEMPNAMESPACE_EXISTS
#define RSOP_TEMPNAMESPACE_EXISTS 0x00000004
#endif
#ifndef RSOP_USER_ACCESS_DENIED
#define RSOP_USER_ACCESS_DENIED 0x00000001
#endif

typedef enum _SETTINGSTATUS {
    RSOPUnspecified = 0,
    RSOPApplied = 1,
    RSOPIgnored = 2,
    RSOPFailed = 3,
    RSOPSubsettingFailed = 4
} SETTINGSTATUS;

typedef UINT_PTR ASYNCCOMPLETIONHANDLE;
typedef PVOID PRSOPTOKEN;
typedef GUID * REFGPEXTENSIONID;

typedef struct _POLICYSETTINGSTATUSINFO {
  LPWSTR szKey;
  LPWSTR szEventSource;
  LPWSTR szEventLogName;
  DWORD dwEventID;
  DWORD dwErrorCode;
  SETTINGSTATUS status;
  SYSTEMTIME timeLogged;
} POLICYSETTINGSTATUSINFO, *LPPOLICYSETTINGSTATUSINFO;

#endif /* _USERENV_H */
