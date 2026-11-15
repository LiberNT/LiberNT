#ifndef _INPUT_H
#define _INPUT_H

#include <stdlib.h>
#include <wchar.h>

#define WIN32_NO_STATUS
#include <windef.h>
#include <winbase.h>
#include <winnls.h>
#include <winreg.h>
#include <winuser.h>
#include <wingdi.h>
#include <commctrl.h>
#include <windowsx.h>
#include <setupapi.h>
#include <strsafe.h>
#include <cpl.h>
#include <imm32_undoc.h>
#define COBJMACROS
#include <objbase.h>
#include <msctf.h>

#include "resource.h"

typedef struct
{
    int idIcon;
    int idName;
    int idDescription;
    APPLET_PROC AppletProc;
} APPLET, *PAPPLET;

extern HINSTANCE hApplet;
extern BOOL g_bRebootNeeded;

// Character Count of a layout ID like "00000409"
#define CCH_LAYOUT_ID    8

// Maximum Character Count of a ULONG in decimal
#define CCH_ULONG_DEC    10

#define MAX_STR_LEN      256

/* settings_page.c */
INT_PTR CALLBACK
SettingsPageProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);
BOOL EnableProcessPrivileges(LPCWSTR lpPrivilegeName, BOOL bEnable);

/* advanced_settings_page.c */
INT_PTR CALLBACK
AdvancedSettingsPageProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);

/* add_dialog.c */
INT_PTR CALLBACK
AddDialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);

/* edit_dialog.c */
INT_PTR CALLBACK
EditDialogProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);

/* key_settings_dialog.c */

typedef struct
{
    DWORD dwAttributes;
    DWORD dwLanguage;
    DWORD dwLayout;
} KEY_SETTINGS;

INT_PTR CALLBACK
KeySettingsDialogProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);

DWORD
ReadAttributes(VOID);

/* key_sequence_dialog.c */
INT_PTR CALLBACK
ChangeKeySeqDialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);


static inline DWORD
DWORDfromString(const WCHAR *pszString)
{
    WCHAR *pszEnd;

    return wcstoul(pszString, &pszEnd, 16);
}

VOID GetSystemLibraryPath(LPWSTR pszPath, INT cchPath, LPCWSTR pszFileName);

/* layout_or_tip.c */
#define LOTP_INPUTPROCESSOR             1
#define LOTP_KEYBOARDLAYOUT             2

#define ILOT_UNINSTALL                  0x00000001
#define ILOT_DEFPROFILE                 0x00000002
#define ILOT_NOAPPLYTOCURRENTSESSION    0x00000020
#define ILOT_CLEANINSTALL               0x00000040
#define ILOT_DISABLED                   0x00000080

typedef struct tagLAYOUTORTIPPROFILE
{
    DWORD dwProfileType;
    LANGID langid;
    CLSID clsid;
    GUID guidProfile;
    GUID catid;
    DWORD dwSubstituteLayout;
    DWORD dwFlags;
    WCHAR szId[MAX_PATH];
} LAYOUTORTIPPROFILE;

ITfInputProcessorProfiles *
CreateProfiles(_Out_ HMODULE *phMsctf);

VOID
ReleaseProfiles(_In_opt_ ITfInputProcessorProfiles *pProfiles, _In_opt_ HMODULE hMsctf);

BOOL WINAPI
InstallLayoutOrTip(_In_ LPCWSTR psz, _In_ DWORD dwFlags);

UINT WINAPI
EnumEnabledLayoutOrTip(
    _In_opt_ LPCWSTR pszUserReg,
    _In_opt_ LPCWSTR pszSystemReg,
    _In_opt_ LPCWSTR pszSoftwareReg,
    _Out_writes_opt_(uBufLength) LAYOUTORTIPPROFILE *pLayoutOrTipProfile,
    _In_ UINT uBufLength);

#endif /* _INPUT_H */
