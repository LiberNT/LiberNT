/*
 * PROJECT:     LiberNT Setup
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Precompiled header
 * COPYRIGHT:   Copyright 2008-2010 Matthias Kupfer <mkupfer@reactos.org>
 *              Copyright 2008-2009 Dmitry Chapyshev <dmitry@reactos.org>
 *              Copyright 2018-2024 Hermès Bélusca-Maïto <hermes.belusca-maito@reactos.org>
 */

#ifndef _LIBERNT_PCH_
#define _LIBERNT_PCH_

/* C Headers */
#include <stdlib.h>
#include <stdarg.h>
#include <tchar.h>

/* PSDK/NDK */
#define WIN32_NO_STATUS
#include <windef.h>
#include <winbase.h>
#include <winreg.h>
#include <wingdi.h>
#include <winuser.h>

#include <strsafe.h>

#include <commctrl.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <lmcons.h>

#define EnableDlgItem(hDlg, nID, bEnable)   \
    EnableWindow(GetDlgItem((hDlg), (nID)), (bEnable))

#define ShowDlgItem(hDlg, nID, nCmdShow)    \
    ShowWindow(GetDlgItem((hDlg), (nID)), (nCmdShow))

#define SetDlgItemFont(hDlg, nID, hFont, bRedraw)   \
    SetWindowFont(GetDlgItem((hDlg), (nID)), (hFont), (bRedraw))

/**/#include <setupapi.h>/**/
#include <devguid.h>

#define NTOS_MODE_USER
#include <ndk/cmtypes.h> // For CM_DISK stuff
#include <ndk/iofuncs.h> // For NtCreate/OpenFile
#include <ndk/rtlfuncs.h>


/* Setup library headers */
// #include <reactos/rosioctl.h>
#include <../lib/setuplib.h>


/* UI elements */
typedef struct _UI_CONTEXT
{
    HWND hwndDlg;   // Install progress page
    HWND hWndItem;  // Progress action
    HWND hWndProgress;  // Progress gauge
    LONG_PTR dwPbStyle; // Progress gauge style
} UI_CONTEXT, *PUI_CONTEXT;

extern UI_CONTEXT UiContext;


#define InterlockedFlagsTestAndSet8(Target, Flags) \
    !!(InterlockedOr8((PCHAR)(Target), (Flags)) & (Flags))

#define InterlockedFlagsTestAndReset8(Target, Flags) \
    !!(InterlockedAnd8((PCHAR)(Target), ~(Flags)) & (Flags))

typedef struct _SETUP_CHOICE
{
    WCHAR Id[KL_NAMELENGTH];
    WCHAR Name[128];
} SETUP_CHOICE, *PSETUP_CHOICE;

typedef struct _SETUPDATA
{
    /* General */
    HINSTANCE hInstance;
    BOOL bUnattend;
    BOOL bUnattendTried;
    BOOL bMustReboot;

    HFONT hTitleFont;
    HFONT hBoldFont;

    HANDLE hInstallThread;
    HANDLE hHaltInstallEvent;
    union
    {
        struct
        {
#define SETUP_ABORT_INSTALL     1
            UCHAR bAbortInstall  :2;
#define SETUP_IS_CANCELLING     4
            UCHAR bIsCancelling  :2;
#define SETUP_PAGE_SWITCHING    16
            UCHAR bPageSwitching :2;
            UCHAR bReserved      :2;
        };
        BOOLEAN bStopInstall;
    };

    USETUP_DATA USetupData;

    PPARTLIST PartitionList;
    BOOL bInstalled;
    BOOL bAlongside;
    BOOL bWindowsFirst;
    WCHAR WindowsName[32];

    /* txtsetup.sif data */
    // LONG DefaultLang;     // default language (table index)
    // LONG DefaultKBLayout; // default keyboard layout (table index)
    PCWSTR SelectedLanguageId;
    WCHAR DefaultLanguage[20];   // Copy of string inside LanguageList
    WCHAR DefaultKBLayout[20];   // Copy of string inside KeyboardList
    PSETUP_CHOICE Locales;
    ULONG LocaleCount;
    PSETUP_CHOICE Layouts;
    ULONG LayoutCount;

    WCHAR ComputerName[MAX_COMPUTERNAME_LENGTH + 1];
    WCHAR UserName[UNLEN + 1];
    WCHAR Password[PWLEN + 1];
    DWORD TimeZoneIndex;
    BOOL AutoDaylight;
    WCHAR TimeZoneName[128];

} SETUPDATA, *PSETUPDATA;

ULONG
RunSetupSelfTest(
    _In_ PSETUPDATA SetupData);

extern HANDLE ProcessHeap;
extern SETUPDATA SetupData;

extern PPARTENTRY InstallPartition;
extern PPARTENTRY SystemPartition;

/**
 * @brief   Data structure stored when a partition/volume needs to be formatted.
 **/
typedef struct _VOL_CREATE_INFO
{
    PVOLENTRY Volume;

    /* Volume-related parameters:
     * Cached input information that will be set to
     * the FORMAT_VOLUME_INFO structure given to the
     * 'FSVOLNOTIFY_STARTFORMAT' step */
    // PCWSTR FileSystemName;
    WCHAR FileSystemName[MAX_PATH+1];
    FMIFS_MEDIA_FLAG MediaFlag;
    PCWSTR Label;
    BOOLEAN QuickFormat;
    ULONG ClusterSize;
} VOL_CREATE_INFO, *PVOL_CREATE_INFO;

typedef enum _SETUP_PAGE
{
    SETUP_PAGE_LANGUAGE,
    SETUP_PAGE_TYPE,
    SETUP_PAGE_DISK,
    SETUP_PAGE_ACCOUNT,
    SETUP_PAGE_READY,
    SETUP_PAGE_INSTALL,
    SETUP_PAGE_DONE,
    SETUP_PAGE_COUNT
} SETUP_PAGE;

#define SETUP_MAX_PAGES             8
#define SETUP_PAGE_BACK             0x1
#define SETUP_PAGE_SKIP_UNATTEND    0x2
#define SETUP_PAGE_NO_CANCEL        0x4
#define SETUP_PAGE_CLOSE            0x8
#define SETUP_PAGE_OPTIONAL         0x10

typedef struct _SETUP_PAGE_INFO
{
    UINT Template;
    DLGPROC Proc;
    UINT Title;
    UINT Subtitle;
    UINT Flags;
} SETUP_PAGE_INFO;

#define PM_INSTALL_START    (WM_APP + 1)
#define PM_INSTALL_DONE     (WM_APP + 2)

#define SPN_SETACTIVE       (WM_APP + 0x100)
#define SPN_KILLACTIVE      (WM_APP + 0x101)
#define SPN_WIZNEXT         (WM_APP + 0x102)
#define SPN_WIZBACK         (WM_APP + 0x103)
#define SPN_QUERYCANCEL     (WM_APP + 0x104)
#define SPN_QUERYSKIP       (WM_APP + 0x105)

PVOL_CREATE_INFO
FindVolumeFormatPlan(
    _In_ PVOLENTRY Volume);

VOID
FormatDiskSize(
    _In_ ULONGLONG Bytes,
    _Out_writes_(cchText) PWSTR Text,
    _In_ SIZE_T cchText);

VOID
DescribeDiskRegion(
    _In_ PPARTENTRY PartEntry,
    _Out_writes_(cchText) PWSTR Text,
    _In_ SIZE_T cchText);

VOID
DescribeDiskChanges(
    _Out_writes_(cchText) PWSTR Text,
    _In_ SIZE_T cchText);

INT_PTR
CALLBACK
DiskPageProc(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam);

PPARTENTRY
FindPartitionAt(
    _In_ PPARTLIST List,
    _In_ ULONG DiskNumber,
    _In_ ULONGLONG StartSector);

ULONGLONG
GetFollowingFreeBytes(
    _In_ PPARTENTRY PartEntry);

BOOLEAN
PlanAlongsideInstall(
    _In_ HWND hwndDlg,
    _Inout_ PSETUPDATA pSetupData,
    _In_ ULONG DiskNumber,
    _In_ ULONGLONG StartSector,
    _In_ ULONGLONG ShrinkBytes);

INT_PTR
CALLBACK
InstallTypePageProc(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam);

VOID
DiskInfoRefresh(VOID);

VOID
DiskInfoFree(VOID);

BOOL
DiskInfoGetDescription(
    _In_ ULONG DiskNumber,
    _Out_writes_(cchText) PWSTR Text,
    _In_ SIZE_T cchText);

INT_PTR
RunSetupFrame(
    _In_ PSETUPDATA pSetupData,
    _In_reads_(PageCount) const SETUP_PAGE_INFO* PageInfo,
    _In_ INT PageCount);

INT_PTR
RunSetupWizard(
    _In_ PSETUPDATA pSetupData);

INT_PTR
CALLBACK
AccountPageProc(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam);

VOID
SetResText(
    _In_ HWND hWnd,
    _In_ UINT uID);

INT_PTR
CALLBACK
LanguagePageProc(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam);

VOID
LoadLanguageChoices(
    _Inout_ PSETUPDATA pSetupData);

VOID
ApplyLanguageChoice(
    _Inout_ PSETUPDATA pSetupData,
    _In_ PCWSTR LocaleId,
    _In_ PCWSTR LayoutId);

VOID
EnableSetupCancel(
    _In_ HWND hFrame,
    _In_ BOOL Enable);

VOID
SetupFrameSetButtons(
    _In_ HWND hPage,
    _In_ BOOL Back,
    _In_ BOOL Next);

VOID
SetupFrameShowButtons(
    _In_ HWND hPage,
    _In_ BOOL Back,
    _In_ BOOL Next,
    _In_ BOOL Cancel);

VOID
SetupFrameSetButtonText(
    _In_ HWND hPage,
    _In_ INT ButtonId,
    _In_ UINT uID);

VOID
SetupFrameSetTitle(
    _In_ HWND hPage,
    _In_ UINT Title,
    _In_ UINT Subtitle);

VOID
SetupFramePressNext(
    _In_ HWND hPage);

VOID
SetupFrameGoTo(
    _In_ HWND hPage,
    _In_ INT Page);



/* libernt.c */

INT_PTR
CALLBACK
InstallPageProc(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam);

VOID
GenerateComputerName(
    _Out_writes_(MAX_COMPUTERNAME_LENGTH + 1) PWSTR Buffer);

BOOL
IsValidSetupComputerName(
    _In_ PCWSTR Name);

BOOL
IsValidSetupUserName(
    _In_ PCWSTR Name);

VOID
LoadPrintfW(
    _Out_writes_(cchText) PWSTR Text,
    _In_ SIZE_T cchText,
    _In_ UINT uID,
    ...);

INT
SetupMessage(
    _In_opt_ HWND hwndOwner,
    _In_opt_ PCWSTR Icon,
    _In_ TASKDIALOG_COMMON_BUTTON_FLAGS Buttons,
    _In_ INT DefaultButton,
    _In_opt_ PCWSTR MainInstruction,
    _In_opt_ PCWSTR Content);

#endif /* _LIBERNT_PCH_ */

/* EOF */
