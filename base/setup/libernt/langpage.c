/*
 * PROJECT:     LiberNT Setup
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Language and keyboard page
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "libernt.h"

#include "resource.h"

#define NDEBUG
#include <debug.h>

static VOID
FillChoiceList(
    _In_ HWND hCombo,
    _In_reads_(Count) const SETUP_CHOICE* Choices,
    _In_ ULONG Count,
    _In_ PCWSTR Selected)
{
    ULONG Index;
    INT iItem, Items, Current = 0;

    SendMessageW(hCombo, CB_RESETCONTENT, 0, 0);
    for (Index = 0; Index < Count; Index++)
    {
        iItem = (INT)SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)Choices[Index].Name);
        if (iItem >= 0)
            SendMessageW(hCombo, CB_SETITEMDATA, iItem, Index);
    }

    Items = (INT)SendMessageW(hCombo, CB_GETCOUNT, 0, 0);
    for (iItem = 0; iItem < Items; iItem++)
    {
        Index = (ULONG)SendMessageW(hCombo, CB_GETITEMDATA, iItem, 0);
        if (Index < Count && _wcsicmp(Choices[Index].Id, Selected) == 0)
        {
            Current = iItem;
            break;
        }
    }
    SendMessageW(hCombo, CB_SETCURSEL, Current, 0);
}

static const SETUP_CHOICE*
GetSelectedChoice(
    _In_ HWND hCombo,
    _In_reads_(Count) const SETUP_CHOICE* Choices,
    _In_ ULONG Count)
{
    INT iItem = (INT)SendMessageW(hCombo, CB_GETCURSEL, 0, 0);
    ULONG Index;

    if (iItem < 0)
        return NULL;
    Index = (ULONG)SendMessageW(hCombo, CB_GETITEMDATA, iItem, 0);
    return (Index < Count) ? &Choices[Index] : NULL;
}

static BOOL
SelectChoiceById(
    _In_ HWND hCombo,
    _In_reads_(Count) const SETUP_CHOICE* Choices,
    _In_ ULONG Count,
    _In_ PCWSTR Id)
{
    INT iItem, Items = (INT)SendMessageW(hCombo, CB_GETCOUNT, 0, 0);
    ULONG Index;

    for (iItem = 0; iItem < Items; iItem++)
    {
        Index = (ULONG)SendMessageW(hCombo, CB_GETITEMDATA, iItem, 0);
        if (Index < Count && _wcsicmp(Choices[Index].Id, Id) == 0)
        {
            SendMessageW(hCombo, CB_SETCURSEL, iItem, 0);
            return TRUE;
        }
    }
    return FALSE;
}

static VOID
ActivateLayout(
    _In_ HWND hwndDlg,
    _In_ PSETUPDATA pSetupData)
{
    const SETUP_CHOICE* Layout = GetSelectedChoice(GetDlgItem(hwndDlg, IDC_LAYOUTLIST),
                                                   pSetupData->Layouts,
                                                   pSetupData->LayoutCount);

    if (Layout && !LoadKeyboardLayoutW(Layout->Id, KLF_ACTIVATE | KLF_SETFORPROCESS))
        DPRINT1("LoadKeyboardLayoutW(%S) failed, error %lu\n", Layout->Id, GetLastError());
}

static VOID
SelectLayoutForLocale(
    _In_ HWND hwndDlg,
    _In_ PSETUPDATA pSetupData)
{
    HWND hLayouts = GetDlgItem(hwndDlg, IDC_LAYOUTLIST);
    const SETUP_CHOICE* Locale;
    WCHAR Keyboards[256];
    WCHAR LayoutId[KL_NAMELENGTH];
    PWSTR Colon, End;
    LCID Lcid;

    Locale = GetSelectedChoice(GetDlgItem(hwndDlg, IDC_LANGUAGELIST),
                               pSetupData->Locales,
                               pSetupData->LocaleCount);
    if (!Locale)
        return;

    Lcid = wcstoul(Locale->Id, NULL, 16);
    if (GetLocaleInfoW(Lcid, LOCALE_SKEYBOARDSTOINSTALL, Keyboards, ARRAYSIZE(Keyboards)) &&
        (Colon = wcschr(Keyboards, L':')) != NULL)
    {
        End = wcschr(Colon, L';');
        if (End)
            *End = UNICODE_NULL;
        if (SelectChoiceById(hLayouts, pSetupData->Layouts, pSetupData->LayoutCount, Colon + 1))
            return;
    }

    StringCchPrintfW(LayoutId, ARRAYSIZE(LayoutId), L"%08lX", (ULONG)LANGIDFROMLCID(Lcid));
    SelectChoiceById(hLayouts, pSetupData->Layouts, pSetupData->LayoutCount, LayoutId);
}

INT_PTR
CALLBACK
LanguagePageProc(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam)
{
    PSETUPDATA pSetupData = (PSETUPDATA)GetWindowLongPtrW(hwndDlg, GWLP_USERDATA);

    switch (uMsg)
    {
        case WM_INITDIALOG:
            pSetupData = (PSETUPDATA)lParam;
            SetWindowLongPtrW(hwndDlg, GWLP_USERDATA, (LONG_PTR)pSetupData);
            LoadLanguageChoices(pSetupData);
            FillChoiceList(GetDlgItem(hwndDlg, IDC_LANGUAGELIST),
                           pSetupData->Locales, pSetupData->LocaleCount,
                           pSetupData->DefaultLanguage);
            FillChoiceList(GetDlgItem(hwndDlg, IDC_LAYOUTLIST),
                           pSetupData->Layouts, pSetupData->LayoutCount,
                           pSetupData->DefaultKBLayout);
            return FALSE;

        case WM_COMMAND:
            if (HIWORD(wParam) != CBN_SELCHANGE)
                break;
            if (LOWORD(wParam) == IDC_LANGUAGELIST)
            {
                SelectLayoutForLocale(hwndDlg, pSetupData);
                ActivateLayout(hwndDlg, pSetupData);
            }
            else if (LOWORD(wParam) == IDC_LAYOUTLIST)
            {
                ActivateLayout(hwndDlg, pSetupData);
            }
            break;

        case SPN_SETACTIVE:
            SetFocus(GetDlgItem(hwndDlg, IDC_LANGUAGELIST));
            return TRUE;

        case SPN_WIZNEXT:
        {
            const SETUP_CHOICE* Locale = GetSelectedChoice(GetDlgItem(hwndDlg, IDC_LANGUAGELIST),
                                                           pSetupData->Locales,
                                                           pSetupData->LocaleCount);
            const SETUP_CHOICE* Layout = GetSelectedChoice(GetDlgItem(hwndDlg, IDC_LAYOUTLIST),
                                                           pSetupData->Layouts,
                                                           pSetupData->LayoutCount);

            ApplyLanguageChoice(pSetupData,
                                Locale ? Locale->Id : pSetupData->DefaultLanguage,
                                Layout ? Layout->Id : pSetupData->DefaultKBLayout);
            SetWindowLongPtrW(hwndDlg, DWLP_MSGRESULT, 0);
            return TRUE;
        }
    }

    return FALSE;
}
