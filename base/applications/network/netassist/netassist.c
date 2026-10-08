/*
 * PROJECT:     LiberNT Network Assistance
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Shows the network assistance key of this boot, or how to enable it
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <stdio.h>
#include "resource.h"

static HINSTANCE Instance;
static HFONT BoldFont;

static LONG
AssistReadKey(WCHAR *Key, DWORD Count)
{
    HKEY Handle;
    DWORD Type, Size = (Count - 1) * sizeof(WCHAR);
    LONG Error;

    Key[0] = 0;
    Error = RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                          L"SYSTEM\\CurrentControlSet\\Control\\NetworkAssistance",
                          0, KEY_QUERY_VALUE, &Handle);
    if (Error != ERROR_SUCCESS)
        return Error;

    Error = RegQueryValueExW(Handle, L"Key", NULL, &Type, (LPBYTE)Key, &Size);
    RegCloseKey(Handle);
    if (Error != ERROR_SUCCESS)
    {
        Key[0] = 0;
        return Error;
    }

    Key[Size / sizeof(WCHAR)] = 0;
    if (Type != REG_SZ || !Key[0])
    {
        Key[0] = 0;
        return ERROR_FILE_NOT_FOUND;
    }

    return ERROR_SUCCESS;
}

static void
AssistSetText(HWND Dialog, INT Control, UINT String)
{
    WCHAR Text[768];

    LoadStringW(Instance, String, Text, ARRAYSIZE(Text));
    SetDlgItemTextW(Dialog, Control, Text);
}

static void
AssistShowState(HWND Dialog)
{
    WCHAR Key[64];
    LONG Error = AssistReadKey(Key, ARRAYSIZE(Key));
    BOOL HaveKey = Error == ERROR_SUCCESS;

    if (HaveKey)
    {
        AssistSetText(Dialog, IDC_STATE, IDS_ON);
        AssistSetText(Dialog, IDC_DETAIL, IDS_ON_DETAIL);
    }
    else if (Error == ERROR_ACCESS_DENIED)
    {
        AssistSetText(Dialog, IDC_STATE, IDS_ON);
        AssistSetText(Dialog, IDC_DETAIL, IDS_DENIED_DETAIL);
    }
    else
    {
        AssistSetText(Dialog, IDC_STATE, IDS_OFF);
        AssistSetText(Dialog, IDC_DETAIL, IDS_OFF_DETAIL);
    }

    SetDlgItemTextW(Dialog, IDC_KEY, Key);
    ShowWindow(GetDlgItem(Dialog, IDC_KEY), HaveKey ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(Dialog, IDC_COPY), HaveKey ? SW_SHOW : SW_HIDE);
    SetFocus(GetDlgItem(Dialog, HaveKey ? IDC_COPY : IDCANCEL));
}

static void
AssistCopyKey(HWND Dialog)
{
    WCHAR Key[64];
    HGLOBAL Memory;
    SIZE_T Size;
    PWSTR Text;

    if (!GetDlgItemTextW(Dialog, IDC_KEY, Key, ARRAYSIZE(Key)))
        return;

    Size = (wcslen(Key) + 1) * sizeof(WCHAR);
    Memory = GlobalAlloc(GMEM_MOVEABLE, Size);
    if (!Memory)
        return;

    Text = GlobalLock(Memory);
    if (!Text)
    {
        GlobalFree(Memory);
        return;
    }

    CopyMemory(Text, Key, Size);
    GlobalUnlock(Memory);
    if (!OpenClipboard(Dialog))
    {
        GlobalFree(Memory);
        return;
    }

    EmptyClipboard();
    if (SetClipboardData(CF_UNICODETEXT, Memory))
        AssistSetText(Dialog, IDC_COPY, IDS_COPIED);
    else
        GlobalFree(Memory);

    CloseClipboard();
}

static void
AssistOpenLink(HWND Dialog, PCWSTR Url)
{
    WCHAR Format[160], Text[512];

    if ((INT_PTR)ShellExecuteW(Dialog, L"open", Url, NULL, NULL, SW_SHOWNORMAL) > 32)
        return;

    LoadStringW(Instance, IDS_LINK_FAILED, Format, ARRAYSIZE(Format));
    _snwprintf(Text, ARRAYSIZE(Text), Format, Url);
    Text[ARRAYSIZE(Text) - 1] = 0;
    MessageBoxW(Dialog, Text, NULL, MB_OK | MB_ICONINFORMATION);
}

static INT_PTR CALLBACK
AssistDialogProc(HWND Dialog, UINT Message, WPARAM WParam, LPARAM LParam)
{
    LOGFONTW Font;
    PNMLINK Link;

    switch (Message)
    {
        case WM_INITDIALOG:
            SendMessageW(Dialog, WM_SETICON, ICON_BIG,
                         (LPARAM)LoadIconW(Instance, MAKEINTRESOURCEW(IDI_ASSIST)));
            if (GetObjectW((HFONT)SendMessageW(Dialog, WM_GETFONT, 0, 0), sizeof(Font), &Font))
            {
                Font.lfWeight = FW_BOLD;
                BoldFont = CreateFontIndirectW(&Font);
            }
            if (BoldFont)
            {
                SendDlgItemMessageW(Dialog, IDC_STATE, WM_SETFONT, (WPARAM)BoldFont, FALSE);
                SendDlgItemMessageW(Dialog, IDC_KEY, WM_SETFONT, (WPARAM)BoldFont, FALSE);
            }
            AssistShowState(Dialog);
            return FALSE;

        case WM_COMMAND:
            if (LOWORD(WParam) == IDC_COPY)
            {
                AssistCopyKey(Dialog);
                return TRUE;
            }
            if (LOWORD(WParam) == IDCANCEL || LOWORD(WParam) == IDOK)
            {
                EndDialog(Dialog, 0);
                return TRUE;
            }
            break;

        case WM_NOTIFY:
            Link = (PNMLINK)LParam;
            if (Link->hdr.idFrom == IDC_LINKS &&
                (Link->hdr.code == NM_CLICK || Link->hdr.code == NM_RETURN))
            {
                AssistOpenLink(Dialog, Link->item.szUrl);
                return TRUE;
            }
            break;

        case WM_CLOSE:
            EndDialog(Dialog, 0);
            return TRUE;
    }

    return FALSE;
}

int WINAPI
wWinMain(HINSTANCE CurrentInstance, HINSTANCE PreviousInstance, LPWSTR CommandLine, int ShowCommand)
{
    INITCOMMONCONTROLSEX Controls = {sizeof(Controls), ICC_LINK_CLASS | ICC_STANDARD_CLASSES};

    UNREFERENCED_PARAMETER(PreviousInstance);
    UNREFERENCED_PARAMETER(CommandLine);
    UNREFERENCED_PARAMETER(ShowCommand);
    Instance = CurrentInstance;
    InitCommonControlsEx(&Controls);
    DialogBoxParamW(Instance, MAKEINTRESOURCEW(IDD_ASSIST), NULL, AssistDialogProc, 0);
    if (BoldFont)
        DeleteObject(BoldFont);

    return 0;
}
