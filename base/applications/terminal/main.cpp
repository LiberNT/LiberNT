/*
 * PROJECT:     LiberNT Terminal
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Entry point, command line, shared helpers and small dialogs
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "terminal.h"

HINSTANCE g_Instance;
Settings g_Settings;

namespace
{

TerminalWindow *g_Window;

struct PromptData
{
    std::wstring Title;
    std::wstring *Text;
};

INT_PTR CALLBACK PromptProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    PromptData *data = (PromptData *)GetWindowLongPtrW(hwnd, DWLP_USER);

    switch (msg)
    {
        case WM_INITDIALOG:
            data = (PromptData *)lParam;
            SetWindowLongPtrW(hwnd, DWLP_USER, (LONG_PTR)data);
            SetWindowTextW(hwnd, data->Title.c_str());
            SetDlgItemTextW(hwnd, IDC_RENAME_EDIT, data->Text->c_str());
            SendDlgItemMessageW(hwnd, IDC_RENAME_EDIT, EM_SETSEL, 0, -1);
            return TRUE;
        case WM_COMMAND:
            if (LOWORD(wParam) == IDOK)
            {
                wchar_t text[256];
                GetDlgItemTextW(hwnd, IDC_RENAME_EDIT, text, ARRAYSIZE(text));
                *data->Text = text;
                EndDialog(hwnd, IDOK);
                return TRUE;
            }
            if (LOWORD(wParam) == IDCANCEL)
            {
                EndDialog(hwnd, IDCANCEL);
                return TRUE;
            }
            break;
    }
    return FALSE;
}

INT_PTR CALLBACK AboutProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (msg)
    {
        case WM_INITDIALOG:
        {
            HICON icon = (HICON)LoadImageW(g_Instance, MAKEINTRESOURCEW(IDI_TERMINAL), IMAGE_ICON, 48, 48, LR_SHARED);
            SendDlgItemMessageW(hwnd, IDC_ABOUT_ICON, STM_SETICON, (WPARAM)icon, 0);
            SetDlgItemTextW(hwnd, IDC_ABOUT_NAME, LoadStr(IDS_APP_TITLE).c_str());
            SetDlgItemTextW(hwnd, IDC_ABOUT_VERSION, LoadStr(IDS_VERSION_LINE).c_str());
            SetDlgItemTextW(hwnd, IDC_ABOUT_COPYRIGHT, LoadStr(IDS_COPYRIGHT).c_str());
            SetDlgItemTextW(hwnd, IDC_ABOUT_DESCRIPTION, LoadStr(IDS_DESCRIPTION).c_str());
            return TRUE;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
            {
                EndDialog(hwnd, IDOK);
                return TRUE;
            }
            break;
    }
    return FALSE;
}

/* Joins the arguments from the given index back into one command line. */
std::wstring JoinArguments(LPWSTR *argv, int argc, int first)
{
    std::wstring line;

    for (int i = first; i < argc; i++)
    {
        std::wstring argument = argv[i];

        if (!line.empty())
            line += L' ';
        if (argument.find_first_of(L" \t\"") != std::wstring::npos)
        {
            std::wstring quoted = L"\"";
            for (wchar_t ch : argument)
            {
                if (ch == L'"')
                    quoted += L'\\';
                quoted += ch;
            }
            line += quoted + L"\"";
        }
        else
        {
            line += argument;
        }
    }
    return line;
}

}

std::wstring
LoadStr(UINT id)
{
    const wchar_t *text = NULL;
    int length = LoadStringW(g_Instance, id, (LPWSTR)&text, 0);

    return length > 0 ? std::wstring(text, length) : std::wstring();
}

std::wstring
ToWide(const std::basic_string<char32_t> &text)
{
    std::basic_string<char16_t> utf16 = Vt::EncodeUtf16(text);
    return std::wstring(reinterpret_cast<const wchar_t *>(utf16.c_str()), utf16.size());
}

std::basic_string<char32_t>
FromWide(const std::wstring &text)
{
    return Vt::DecodeUtf16(reinterpret_cast<const char16_t *>(text.c_str()), text.size());
}

void
BroadcastSettingsChanged()
{
    if (g_Window)
        g_Window->ApplySettings();
}

bool
PromptText(HWND owner, const std::wstring &title, std::wstring &text)
{
    PromptData data = { title, &text };

    return DialogBoxParamW(g_Instance, MAKEINTRESOURCEW(IDD_RENAME), owner, PromptProc, (LPARAM)&data) == IDOK;
}

void
ShowAbout(HWND owner)
{
    DialogBoxW(g_Instance, MAKEINTRESOURCEW(IDD_ABOUT), owner, AboutProc);
}

int WINAPI
wWinMain(HINSTANCE instance, HINSTANCE previous, LPWSTR commandLine, int showCmd)
{
    INITCOMMONCONTROLSEX controls = { sizeof(controls), ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES | ICC_LISTVIEW_CLASSES };
    std::wstring profile, directory, command, error;
    LPWSTR *argv;
    int argc = 0;
    MSG msg;

    UNREFERENCED_PARAMETER(previous);
    UNREFERENCED_PARAMETER(commandLine);
    g_Instance = instance;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    InitCommonControlsEx(&controls);
    ShellFrame::Startup();

    argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; argv && i < argc; i++)
    {
        std::wstring argument = argv[i];

        if ((argument == L"new-tab" || argument == L"nt") && i == 1)
            continue;
        if ((argument == L"-p" || argument == L"--profile") && i + 1 < argc)
        {
            profile = argv[++i];
        }
        else if ((argument == L"-d" || argument == L"--startingDirectory") && i + 1 < argc)
        {
            directory = argv[++i];
        }
        else if (argument == L"--")
        {
            command = JoinArguments(argv, argc, i + 1);
            break;
        }
        else
        {
            command = JoinArguments(argv, argc, i);
            break;
        }
    }
    if (argv)
        LocalFree(argv);

    g_Settings = SettingsStore::Load(&error);

    {
        TerminalWindow window;

        g_Window = &window;
        if (!window.Open(showCmd, profile, command, directory))
        {
            g_Window = NULL;
            ShellFrame::Shutdown();
            CoUninitialize();
            return 1;
        }
        if (!error.empty())
            MessageBoxW(window.Window(), error.c_str(), LoadStr(IDS_SETTINGS_ERROR).c_str(), MB_OK | MB_ICONWARNING);

        while (GetMessageW(&msg, NULL, 0, 0) > 0)
        {
            if (SettingsPreTranslate(&msg) || window.PreTranslate(&msg))
                continue;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        g_Window = NULL;
    }

    ShellFrame::Shutdown();
    CoUninitialize();
    return (int)msg.wParam;
}
