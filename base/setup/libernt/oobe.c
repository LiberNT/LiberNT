/*
 * PROJECT:     LiberNT Setup
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     First-boot setup of a preinstalled LiberNT
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "libernt.h"
#include <winnls.h>
#include <tzlib.h>
#include <objbase.h>
#include <lm.h>
#include <userenv.h>

#include "oobe.h"
#include "resource.h"

#define NDEBUG
#include <debug.h>

#define OOBE_SKIPPED            1
#define OOBE_DEFAULT_HIVE       L"LiberNT.FirstBoot.Default"
#define OOBE_APPLY_TIMEOUT      5000
#define PM_APPLY_DONE           (WM_APP + 3)

NTSTATUS
WINAPI
SetAccountsDomainSid(
    PSID DomainSid,
    LPCWSTR DomainName);

HANDLE ProcessHeap;
SETUPDATA SetupData;

static INT_PTR CALLBACK FinishPageProc(HWND, UINT, WPARAM, LPARAM);

static const SETUP_PAGE_INFO ComponentPages[] =
{
    { IDD_APPSPAGE,     AppsPageProc,     IDS_APPSTITLE,        IDS_APPSSUBTITLE,        SETUP_PAGE_CLOSE },
    { IDD_PROCESSPAGE,  FinishPageProc,   IDS_OOBE_FINISHTITLE, IDS_OOBE_FINISHSUBTITLE, SETUP_PAGE_CLOSE },
};

static BOOL ComponentsOnly;

static const SETUP_PAGE_INFO OobePages[] =
{
    { IDD_LANGUAGEPAGE, LanguagePageProc, IDS_OOBE_REGIONTITLE, IDS_OOBE_REGIONSUBTITLE, SETUP_PAGE_CLOSE },
    { IDD_ACCOUNTPAGE,  AccountPageProc,  IDS_ACCOUNTTITLE,     IDS_ACCOUNTZONESUBTITLE, SETUP_PAGE_BACK | SETUP_PAGE_CLOSE },
    { IDD_APPSPAGE,     AppsPageProc,     IDS_APPSTITLE,        IDS_APPSSUBTITLE,        SETUP_PAGE_BACK | SETUP_PAGE_CLOSE },
    { IDD_PROCESSPAGE,  FinishPageProc,   IDS_OOBE_FINISHTITLE, IDS_OOBE_FINISHSUBTITLE, SETUP_PAGE_CLOSE },
};

typedef struct _CHOICE_LIST
{
    PSETUP_CHOICE Items;
    ULONG Count;
} CHOICE_LIST, *PCHOICE_LIST;

static CHOICE_LIST LocaleList;

static BOOL
AddChoice(
    _Inout_ PCHOICE_LIST List,
    _In_ PCWSTR Id,
    _In_ PCWSTR Name)
{
    PSETUP_CHOICE Items;

    Items = List->Items ? HeapReAlloc(ProcessHeap, 0, List->Items, (List->Count + 1) * sizeof(*Items))
                        : HeapAlloc(ProcessHeap, 0, sizeof(*Items));
    if (!Items)
        return FALSE;

    StringCchCopyW(Items[List->Count].Id, ARRAYSIZE(Items[List->Count].Id), Id);
    StringCchCopyW(Items[List->Count].Name, ARRAYSIZE(Items[List->Count].Name), Name);
    List->Items = Items;
    List->Count++;
    return TRUE;
}

static BOOL
CALLBACK
AddLocaleChoice(
    LPWSTR LocaleString)
{
    WCHAR Name[128];
    LCID Lcid = wcstoul(LocaleString, NULL, 16);

    if (GetLocaleInfoW(Lcid, LOCALE_SLOCALIZEDDISPLAYNAME, Name, ARRAYSIZE(Name)))
        AddChoice(&LocaleList, LocaleString, Name);
    return TRUE;
}

VOID
LoadLanguageChoices(
    _Inout_ PSETUPDATA pSetupData)
{
    CHOICE_LIST Layouts = { NULL, 0 };
    WCHAR Klid[KL_NAMELENGTH];
    WCHAR Name[128];
    DWORD cchKlid, cbName, Index;
    HKEY hLayouts, hLayout;

    if (pSetupData->Locales)
        return;

    EnumSystemLocalesW(AddLocaleChoice, LCID_INSTALLED);
    pSetupData->Locales = LocaleList.Items;
    pSetupData->LocaleCount = LocaleList.Count;

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\Keyboard Layouts",
                      0, KEY_ENUMERATE_SUB_KEYS, &hLayouts) != ERROR_SUCCESS)
    {
        return;
    }

    for (Index = 0; ; Index++)
    {
        cchKlid = ARRAYSIZE(Klid);
        if (RegEnumKeyExW(hLayouts, Index, Klid, &cchKlid, NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
            break;
        if (RegOpenKeyExW(hLayouts, Klid, 0, KEY_QUERY_VALUE, &hLayout) != ERROR_SUCCESS)
            continue;
        cbName = sizeof(Name);
        if (RegLoadMUIStringW(hLayout, L"Layout Display Name", Name, sizeof(Name), NULL, 0, NULL) == ERROR_SUCCESS ||
            RegGetValueW(hLayout, NULL, L"Layout Text", RRF_RT_REG_SZ, NULL, Name, &cbName) == ERROR_SUCCESS)
        {
            AddChoice(&Layouts, Klid, Name);
        }
        RegCloseKey(hLayout);
    }
    RegCloseKey(hLayouts);

    pSetupData->Layouts = Layouts.Items;
    pSetupData->LayoutCount = Layouts.Count;
}

VOID
ApplyLanguageChoice(
    _Inout_ PSETUPDATA pSetupData,
    _In_ PCWSTR LocaleId,
    _In_ PCWSTR LayoutId)
{
    WCHAR Locale[ARRAYSIZE(pSetupData->DefaultLanguage)];
    WCHAR Layout[ARRAYSIZE(pSetupData->DefaultKBLayout)];

    StringCchCopyW(Locale, ARRAYSIZE(Locale), LocaleId);
    StringCchCopyW(Layout, ARRAYSIZE(Layout), LayoutId);
    StringCchCopyW(pSetupData->DefaultLanguage, ARRAYSIZE(pSetupData->DefaultLanguage), Locale);
    StringCchCopyW(pSetupData->DefaultKBLayout, ARRAYSIZE(pSetupData->DefaultKBLayout), Layout);
}

static BOOL
EnableTokenPrivilege(
    _In_ PCWSTR Privilege)
{
    TOKEN_PRIVILEGES Privileges;
    HANDLE hToken;
    BOOL Success;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES, &hToken))
        return FALSE;

    Privileges.PrivilegeCount = 1;
    Privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    Success = LookupPrivilegeValueW(NULL, Privilege, &Privileges.Privileges[0].Luid) &&
              AdjustTokenPrivileges(hToken, FALSE, &Privileges, 0, NULL, NULL) &&
              GetLastError() == ERROR_SUCCESS;
    CloseHandle(hToken);
    return Success;
}

static VOID
SetStringValue(
    _In_ HKEY hKey,
    _In_ PCWSTR Name,
    _In_ PCWSTR Value)
{
    RegSetValueExW(hKey, Name, 0, REG_SZ, (const BYTE*)Value, (DWORD)((wcslen(Value) + 1) * sizeof(WCHAR)));
}

static VOID
WriteUserRegion(
    _In_ HKEY hUser,
    _In_ LCID Lcid,
    _In_ PCWSTR LayoutId)
{
    static const struct
    {
        LCTYPE Type;
        PCWSTR Name;
    } Values[] =
    {
        { LOCALE_SNAME, L"LocaleName" },
        { LOCALE_SDECIMAL, L"sDecimal" },
        { LOCALE_STHOUSAND, L"sThousand" },
        { LOCALE_SNEGATIVESIGN, L"sNegativeSign" },
        { LOCALE_SPOSITIVESIGN, L"sPositiveSign" },
        { LOCALE_SGROUPING, L"sGrouping" },
        { LOCALE_SLIST, L"sList" },
        { LOCALE_SNATIVEDIGITS, L"sNativeDigits" },
        { LOCALE_INEGNUMBER, L"iNegNumber" },
        { LOCALE_IDIGITS, L"iDigits" },
        { LOCALE_ILZERO, L"iLZero" },
        { LOCALE_IMEASURE, L"iMeasure" },
        { LOCALE_IDIGITSUBSTITUTION, L"NumShape" },
        { LOCALE_SCURRENCY, L"sCurrency" },
        { LOCALE_SMONDECIMALSEP, L"sMonDecimalSep" },
        { LOCALE_SMONTHOUSANDSEP, L"sMonThousandSep" },
        { LOCALE_SMONGROUPING, L"sMonGrouping" },
        { LOCALE_ICURRENCY, L"iCurrency" },
        { LOCALE_INEGCURR, L"iNegCurr" },
        { LOCALE_ICURRDIGITS, L"iCurrDigits" },
        { LOCALE_STIMEFORMAT, L"sTimeFormat" },
        { LOCALE_SSHORTTIME, L"sShortTime" },
        { LOCALE_STIME, L"sTime" },
        { LOCALE_S1159, L"s1159" },
        { LOCALE_S2359, L"s2359" },
        { LOCALE_ITIME, L"iTime" },
        { LOCALE_ITIMEMARKPOSN, L"iTimePrefix" },
        { LOCALE_ITLZERO, L"iTLZero" },
        { LOCALE_SLONGDATE, L"sLongDate" },
        { LOCALE_SSHORTDATE, L"sShortDate" },
        { LOCALE_SYEARMONTH, L"sYearMonth" },
        { LOCALE_SDATE, L"sDate" },
        { LOCALE_IFIRSTDAYOFWEEK, L"iFirstDayOfWeek" },
        { LOCALE_IFIRSTWEEKOFYEAR, L"iFirstWeekOfYear" },
        { LOCALE_IDATE, L"iDate" },
        { LOCALE_ICALENDARTYPE, L"iCalendarType" },
        { LOCALE_SCOUNTRY, L"sCountry" },
        { LOCALE_SABBREVLANGNAME, L"sLanguage" },
        { LOCALE_ICOUNTRY, L"iCountry" },
    };
    WCHAR Text[96];
    HKEY hKey;
    ULONG Index;

    if (RegCreateKeyExW(hUser, L"Control Panel\\International", 0, NULL, 0, KEY_SET_VALUE, NULL, &hKey, NULL) == ERROR_SUCCESS)
    {
        StringCchPrintfW(Text, ARRAYSIZE(Text), L"%08lX", Lcid);
        SetStringValue(hKey, L"Locale", Text);
        for (Index = 0; Index < ARRAYSIZE(Values); Index++)
        {
            if (GetLocaleInfoW(Lcid, Values[Index].Type | LOCALE_NOUSEROVERRIDE, Text, ARRAYSIZE(Text)))
                SetStringValue(hKey, Values[Index].Name, Text);
        }
        RegCloseKey(hKey);
    }

    if (GetLocaleInfoW(Lcid, LOCALE_IGEOID | LOCALE_NOUSEROVERRIDE, Text, ARRAYSIZE(Text)) &&
        RegCreateKeyExW(hUser, L"Control Panel\\International\\Geo", 0, NULL, 0, KEY_SET_VALUE, NULL, &hKey, NULL) == ERROR_SUCCESS)
    {
        SetStringValue(hKey, L"Nation", Text);
        RegCloseKey(hKey);
    }

    RegDeleteTreeW(hUser, L"Keyboard Layout\\Preload");
    if (RegCreateKeyExW(hUser, L"Keyboard Layout\\Preload", 0, NULL, 0, KEY_SET_VALUE, NULL, &hKey, NULL) == ERROR_SUCCESS)
    {
        SetStringValue(hKey, L"1", LayoutId);
        RegCloseKey(hKey);
    }
}

static VOID
ApplyRegion(
    _In_ PSETUPDATA pSetupData)
{
    WCHAR HivePath[MAX_PATH];
    DWORD cchHivePath = ARRAYSIZE(HivePath);
    LCID Lcid = wcstoul(pSetupData->DefaultLanguage, NULL, 16);
    HKEY hUser;
    LONG Error;

    WriteUserRegion(HKEY_CURRENT_USER, Lcid, pSetupData->DefaultKBLayout);

    if (RegOpenKeyExW(HKEY_USERS, L".DEFAULT", 0, KEY_CREATE_SUB_KEY | KEY_SET_VALUE, &hUser) == ERROR_SUCCESS)
    {
        WriteUserRegion(hUser, Lcid, pSetupData->DefaultKBLayout);
        RegCloseKey(hUser);
    }

    if (!GetDefaultUserProfileDirectoryW(HivePath, &cchHivePath) ||
        FAILED(StringCchCatW(HivePath, ARRAYSIZE(HivePath), L"\\NTUSER.DAT")))
    {
        return;
    }

    EnableTokenPrivilege(SE_BACKUP_NAME);
    EnableTokenPrivilege(SE_RESTORE_NAME);
    Error = RegLoadKeyW(HKEY_USERS, OOBE_DEFAULT_HIVE, HivePath);
    if (Error != ERROR_SUCCESS)
    {
        DPRINT1("Could not load %S (error %ld)\n", HivePath, Error);
        return;
    }

    if (RegOpenKeyExW(HKEY_USERS, OOBE_DEFAULT_HIVE, 0, KEY_CREATE_SUB_KEY | KEY_SET_VALUE | DELETE | KEY_ENUMERATE_SUB_KEYS | KEY_QUERY_VALUE, &hUser) == ERROR_SUCCESS)
    {
        WriteUserRegion(hUser, Lcid, pSetupData->DefaultKBLayout);
        RegCloseKey(hUser);
    }
    RegUnLoadKeyW(HKEY_USERS, OOBE_DEFAULT_HIVE);
}

static VOID
ApplyTimeZone(
    _In_ PSETUPDATA pSetupData)
{
    DYNAMIC_TIME_ZONE_INFORMATION Zone;
    REG_TZI_FORMAT ZoneInfo;
    WCHAR KeyName[128];
    ULONG StandardSize, DaylightSize, ZoneIndex;
    DWORD cchKeyName, Index;
    HKEY hZones, hZone;
    BOOL Found = FALSE;

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Time Zones",
                      0, KEY_ENUMERATE_SUB_KEYS, &hZones) != ERROR_SUCCESS)
    {
        return;
    }

    for (Index = 0; !Found; Index++)
    {
        cchKeyName = ARRAYSIZE(KeyName);
        if (RegEnumKeyExW(hZones, Index, KeyName, &cchKeyName, NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
            break;
        if (RegOpenKeyExW(hZones, KeyName, 0, KEY_QUERY_VALUE, &hZone) != ERROR_SUCCESS)
            continue;

        ZeroMemory(&Zone, sizeof(Zone));
        StandardSize = sizeof(Zone.StandardName);
        DaylightSize = sizeof(Zone.DaylightName);
        if (QueryTimeZoneData(hZone, &ZoneIndex, &ZoneInfo, NULL, NULL,
                              Zone.StandardName, &StandardSize,
                              Zone.DaylightName, &DaylightSize) == ERROR_SUCCESS &&
            ZoneIndex == pSetupData->TimeZoneIndex)
        {
            Found = TRUE;
        }
        RegCloseKey(hZone);
    }
    RegCloseKey(hZones);

    if (!Found)
    {
        DPRINT1("Time zone index %lu not found\n", pSetupData->TimeZoneIndex);
        return;
    }

    Zone.Bias = ZoneInfo.Bias;
    Zone.StandardBias = ZoneInfo.StandardBias;
    Zone.DaylightBias = ZoneInfo.DaylightBias;
    Zone.StandardDate = ZoneInfo.StandardDate;
    Zone.DaylightDate = ZoneInfo.DaylightDate;
    StringCchCopyW(Zone.TimeZoneKeyName, ARRAYSIZE(Zone.TimeZoneKeyName), KeyName);
    Zone.DynamicDaylightTimeDisabled = !pSetupData->AutoDaylight;

    EnableTokenPrivilege(SE_TIME_ZONE_NAME);
    if (!SetDynamicTimeZoneInformation(&Zone))
        DPRINT1("SetDynamicTimeZoneInformation(%S) failed, error %lu\n", KeyName, GetLastError());
    SetAutoDaylight(pSetupData->AutoDaylight);
}

static DWORD
ApplyComputerName(
    _In_ PSETUPDATA pSetupData)
{
    WCHAR Current[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD cchCurrent = ARRAYSIZE(Current);
    NTSTATUS Status;

    if (GetComputerNameExW(ComputerNamePhysicalNetBIOS, Current, &cchCurrent) &&
        _wcsicmp(Current, pSetupData->ComputerName) == 0)
    {
        return ERROR_SUCCESS;
    }

    if (!SetComputerNameExW(ComputerNamePhysicalDnsHostname, pSetupData->ComputerName))
        return GetLastError();

    Status = SetAccountsDomainSid(NULL, pSetupData->ComputerName);
    return NT_SUCCESS(Status) ? ERROR_SUCCESS : RtlNtStatusToDosError(Status);
}

static DWORD
AddToLocalGroup(
    _In_ WELL_KNOWN_SID_TYPE GroupType,
    _In_ PSID UserSid)
{
    BYTE GroupSid[SECURITY_MAX_SID_SIZE];
    DWORD cbGroupSid = sizeof(GroupSid);
    WCHAR Group[GNLEN + 1], Domain[DNLEN + 1];
    DWORD cchGroup = ARRAYSIZE(Group), cchDomain = ARRAYSIZE(Domain);
    LOCALGROUP_MEMBERS_INFO_0 Member;
    SID_NAME_USE Use;
    NET_API_STATUS Status;

    if (!CreateWellKnownSid(GroupType, NULL, GroupSid, &cbGroupSid) ||
        !LookupAccountSidW(NULL, GroupSid, Group, &cchGroup, Domain, &cchDomain, &Use))
    {
        return GetLastError();
    }

    Member.lgrmi0_sid = UserSid;
    Status = NetLocalGroupAddMembers(NULL, Group, 0, (LPBYTE)&Member, 1);
    return (Status == ERROR_MEMBER_IN_ALIAS) ? NERR_Success : Status;
}

static DWORD
CreateAccount(
    _In_ PSETUPDATA pSetupData)
{
    USER_INFO_1 Info;
    BYTE UserSid[SECURITY_MAX_SID_SIZE];
    DWORD cbUserSid = sizeof(UserSid);
    WCHAR Domain[DNLEN + 1];
    DWORD cchDomain = ARRAYSIZE(Domain);
    SID_NAME_USE Use;
    NET_API_STATUS Status;
    HKEY hKey;

    ZeroMemory(&Info, sizeof(Info));
    Info.usri1_name = pSetupData->UserName;
    Info.usri1_password = pSetupData->Password;
    Info.usri1_priv = USER_PRIV_USER;
    Info.usri1_flags = UF_SCRIPT | UF_NORMAL_ACCOUNT | UF_DONT_EXPIRE_PASSWD;
    if (!pSetupData->Password[0])
        Info.usri1_flags |= UF_PASSWD_NOTREQD;

    Status = NetUserAdd(NULL, 1, (LPBYTE)&Info, NULL);
    if (Status != NERR_Success)
        return Status;

    if (!LookupAccountNameW(NULL, pSetupData->UserName, UserSid, &cbUserSid, Domain, &cchDomain, &Use))
        return GetLastError();

    Status = AddToLocalGroup(WinBuiltinAdministratorsSid, UserSid);
    if (Status != NERR_Success)
        return Status;
    AddToLocalGroup(WinBuiltinUsersSid, UserSid);

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon",
                      0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS)
    {
        SetStringValue(hKey, L"DefaultUserName", pSetupData->UserName);
        SetStringValue(hKey, L"DefaultDomainName", pSetupData->ComputerName);
        if (pSetupData->Password[0])
        {
            SetStringValue(hKey, L"AutoAdminLogon", L"0");
            RegDeleteValueW(hKey, L"DefaultPassword");
        }
        else
        {
            SetStringValue(hKey, L"AutoAdminLogon", L"1");
            SetStringValue(hKey, L"DefaultPassword", L"");
        }
        RegCloseKey(hKey);
    }

    return NERR_Success;
}

typedef struct _FINISH_CONTEXT
{
    HWND hwndDlg;
    OOBE_PROGRESS Progress;
} FINISH_CONTEXT, *PFINISH_CONTEXT;

static DWORD
WINAPI
FinishThread(
    LPVOID Parameter)
{
    PFINISH_CONTEXT Context = (PFINISH_CONTEXT)Parameter;
    PSETUPDATA pSetupData = &SetupData;
    HRESULT hrCom;
    DWORD Error;
    ULONG Index;

    if (ComponentsOnly)
    {
        pSetupData->bInstalled = TRUE;
        goto Components;
    }

    SetResText(Context->Progress.hwndActivity, IDS_OOBE_APPLY_REGION);
    ApplyRegion(pSetupData);

    SetResText(Context->Progress.hwndActivity, IDS_OOBE_APPLY_TIMEZONE);
    ApplyTimeZone(pSetupData);

    SetResText(Context->Progress.hwndActivity, IDS_OOBE_APPLY_NAME);
    Error = ApplyComputerName(pSetupData);
    if (Error != ERROR_SUCCESS)
        DPRINT1("Could not rename the computer to %S, error %lu\n", pSetupData->ComputerName, Error);

    SetResText(Context->Progress.hwndActivity, IDS_OOBE_APPLY_ACCOUNT);
    Error = CreateAccount(pSetupData);
    SecureZeroMemory(pSetupData->Password, sizeof(pSetupData->Password));
    pSetupData->bInstalled = (Error == NERR_Success);
    DPRINT1("Account %S created with result %lu\n", pSetupData->UserName, Error);
    if (Error != NERR_Success)
    {
        PostMessageW(Context->hwndDlg, PM_INSTALL_DONE, Error, 0);
        return Error;
    }

Components:
    PostMessageW(Context->hwndDlg, PM_APPLY_DONE, 0, 0);
    hrCom = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    for (Index = 0; Index < OobeAppCount; Index++)
    {
        if (OobeApps[Index].Selected)
            InstallOobeApp(&OobeApps[Index], &Context->Progress);
    }
    if (SUCCEEDED(hrCom))
        CoUninitialize();

    PostMessageW(Context->hwndDlg, PM_INSTALL_DONE, ERROR_SUCCESS, 0);
    return ERROR_SUCCESS;
}

static VOID
ReportFinish(
    _In_ HWND hwndDlg,
    _In_ DWORD AccountError)
{
    WCHAR Text[2048];
    WCHAR Line[512];
    ULONG Index;

    if (AccountError != NERR_Success)
    {
        LoadPrintfW(Text, ARRAYSIZE(Text), IDS_OOBE_ACCOUNT_FAILED, AccountError);
        SetupMessage(GetParent(hwndDlg), TD_ERROR_ICON, TDCBF_CLOSE_BUTTON, IDCLOSE, NULL, Text);
        return;
    }

    Text[0] = UNICODE_NULL;
    for (Index = 0; Index < OobeAppCount; Index++)
    {
        if (!OobeApps[Index].Selected || OobeApps[Index].Result == ERROR_SUCCESS)
            continue;
        LoadPrintfW(Line, ARRAYSIZE(Line), IDS_OOBE_APP_FAILED, OobeApps[Index].Name, OobeApps[Index].Result);
        StringCchCatW(Text, ARRAYSIZE(Text), Line);
        StringCchCatW(Text, ARRAYSIZE(Text), L"\n");
    }

    if (Text[0])
    {
        SetupMessage(GetParent(hwndDlg), TD_WARNING_ICON, TDCBF_OK_BUTTON, IDOK,
                     MAKEINTRESOURCEW(IDS_OOBE_APPS_FAILED), Text);
    }
}

static INT_PTR
CALLBACK
FinishPageProc(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam)
{
    static FINISH_CONTEXT Context;
    HANDLE hThread;

    switch (uMsg)
    {
        case WM_INITDIALOG:
            SetDlgItemTextW(hwndDlg, IDC_ACTIVITY, L"");
            SetDlgItemTextW(hwndDlg, IDC_ITEM, L"");
            return FALSE;

        case SPN_SETACTIVE:
            SetupFrameShowButtons(hwndDlg, FALSE, FALSE, TRUE);
            SetTimer(hwndDlg, 1, OOBE_APPLY_TIMEOUT, NULL);
            Context.hwndDlg = hwndDlg;
            Context.Progress.hwndActivity = GetDlgItem(hwndDlg, IDC_ACTIVITY);
            Context.Progress.hwndItem = GetDlgItem(hwndDlg, IDC_ITEM);
            Context.Progress.hwndProgress = GetDlgItem(hwndDlg, IDC_PROCESSPROGRESS);
            hThread = CreateThread(NULL, 0, FinishThread, &Context, 0, NULL);
            if (hThread)
                CloseHandle(hThread);
            else
                PostMessageW(hwndDlg, PM_INSTALL_DONE, GetLastError(), 0);
            return TRUE;

        case SPN_WIZBACK:
        case SPN_WIZNEXT:
            SetWindowLongPtrW(hwndDlg, DWLP_MSGRESULT, -1);
            return TRUE;

        case PM_APPLY_DONE:
            KillTimer(hwndDlg, 1);
            return TRUE;

        case WM_TIMER:
            KillTimer(hwndDlg, 1);
            DPRINT1("First-boot settings not applied within %u ms\n", OOBE_APPLY_TIMEOUT);
            EndDialog(GetParent(hwndDlg), IDCANCEL);
            return TRUE;

        case PM_INSTALL_DONE:
            KillTimer(hwndDlg, 1);
            ReportFinish(hwndDlg, (DWORD)wParam);
            EndDialog(GetParent(hwndDlg), IDOK);
            return TRUE;
    }

    return FALSE;
}

static BOOL
IsUnattendedFirstBoot(VOID)
{
    WCHAR Path[MAX_PATH];
    WCHAR Value[16];

    if (!GetWindowsDirectoryW(Path, ARRAYSIZE(Path)) ||
        FAILED(StringCchCatW(Path, ARRAYSIZE(Path), L"\\unattend.inf")))
    {
        return FALSE;
    }

    GetPrivateProfileStringW(L"Unattend", L"UnattendSetupEnabled", L"no", Value, ARRAYSIZE(Value), Path);
    return _wcsicmp(Value, L"yes") == 0;
}

static BOOL
HasAutomationPayload(VOID)
{
    WCHAR SystemDrive[MAX_PATH];
    WCHAR Path[] = L"?:\\launcher.cmd";
    DWORD Drives = GetLogicalDrives();
    WCHAR Letter;

    if (!GetWindowsDirectoryW(SystemDrive, ARRAYSIZE(SystemDrive)))
        SystemDrive[0] = UNICODE_NULL;

    for (Letter = L'C'; Letter <= L'Z'; Letter++)
    {
        if (!(Drives & (1 << (Letter - L'A'))) || towupper(SystemDrive[0]) == Letter)
            continue;
        Path[0] = Letter;
        if (GetFileAttributesW(Path) != INVALID_FILE_ATTRIBUTES)
        {
            DPRINT1("First-boot setup skipped: %S starts an automated session\n", Path);
            return TRUE;
        }
    }
    return FALSE;
}

static VOID
EndFirstBootSetup(VOID)
{
    HKEY hKey;

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run",
                      0, KEY_SET_VALUE, &hKey) != ERROR_SUCCESS)
    {
        return;
    }

    RegDeleteValueW(hKey, L"OOBE");
    RegCloseKey(hKey);
}

int WINAPI
wWinMain(
    HINSTANCE hInst,
    HINSTANCE hPrevInstance,
    LPWSTR lpszCmdLine,
    int nCmdShow)
{
    INITCOMMONCONTROLSEX iccx;
    HRESULT hrCom;

    ComponentsOnly = (wcsstr(lpszCmdLine, L"/components") != NULL);
    ProcessHeap = GetProcessHeap();
    SetupData.hInstance = hInst;

    EndFirstBootSetup();
    if (IsUnattendedFirstBoot() || HasAutomationPayload())
        return OOBE_SKIPPED;

    iccx.dwSize = sizeof(iccx);
    iccx.dwICC = ICC_LISTVIEW_CLASSES | ICC_PROGRESS_CLASS;
    InitCommonControlsEx(&iccx);
    hrCom = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    StringCchPrintfW(SetupData.DefaultLanguage, ARRAYSIZE(SetupData.DefaultLanguage), L"%08lX", GetUserDefaultLCID());
    if (!GetKeyboardLayoutNameW(SetupData.DefaultKBLayout))
        StringCchCopyW(SetupData.DefaultKBLayout, ARRAYSIZE(SetupData.DefaultKBLayout), L"00000409");
    GenerateComputerName(SetupData.ComputerName);
    SetupData.TimeZoneIndex = (ULONG)-1;
    GetTimeZoneListIndex(&SetupData.TimeZoneIndex);
    SetupData.AutoDaylight = TRUE;
    OobeAppCount = LoadOobeApps(&OobeApps);
    DPRINT1("First-boot setup lists %lu components\n", OobeAppCount);

    if (!ComponentsOnly)
        RunSetupFrame(&SetupData, OobePages, ARRAYSIZE(OobePages));
    else if (OobeAppCount != 0)
        RunSetupFrame(&SetupData, ComponentPages, ARRAYSIZE(ComponentPages));

    if (SUCCEEDED(hrCom))
        CoUninitialize();
    if (ComponentsOnly)
        return ERROR_SUCCESS;
    return SetupData.bInstalled ? ERROR_SUCCESS : OOBE_SKIPPED;
}
