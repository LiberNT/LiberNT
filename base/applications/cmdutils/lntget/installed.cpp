/*
 * PROJECT:     ReactOS package manager
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Installed applications manager used by the lntget GUI
 * COPYRIGHT:   Copyright 2026 Ahmed Arif
 */

#define NOMINMAX
#include <windows.h>
#include <commctrl.h>

#include "arp.hpp"
#include "installed.hpp"
#include "resource.h"
#include "util.hpp"

#include <algorithm>
#include <cstdio>

namespace lntget
{

namespace
{

constexpr UINT MessageUninstallDone = WM_APP + 1;

enum : int
{
    IdInstalledList = 2001,
    IdInstalledStatus,
    IdUninstall,
    IdInstalledRefresh,
    IdInstalledClose,
};

struct InstalledWindow
{
    HWND window = nullptr;
    HWND owner = nullptr;
    HWND list = nullptr;
    HWND status = nullptr;
    HWND uninstall = nullptr;
    HWND refresh = nullptr;
    HWND close = nullptr;
    HFONT font = nullptr;
    HANDLE thread = nullptr;
    bool busy = false;
    std::vector<InstalledPackage> apps;
};

struct UninstallJob
{
    HWND window = nullptr;
    InstalledPackage package;
    Status status;
};

std::wstring FormatInstalledSize(unsigned long long bytes)
{
    if (!bytes)
        return L"";
    if (bytes >= 1024ull * 1024 * 1024)
        return DecimalWideString(bytes / (1024ull * 1024 * 1024)) + L" GB";
    if (bytes >= 1024ull * 1024)
        return DecimalWideString(bytes / (1024ull * 1024)) + L" MB";
    return DecimalWideString(bytes / 1024) + L" KB";
}

void RefreshInstalled(InstalledWindow &state, const std::wstring &message = std::wstring())
{
    CollectInstalledPackages(nullptr, state.apps);
    ListView_DeleteAllItems(state.list);
    for (std::size_t index = 0; index < state.apps.size(); ++index)
    {
        const ArpEntry &app = state.apps[index].entry;
        LVITEMW item{};
        item.mask = LVIF_TEXT | LVIF_PARAM;
        item.iItem = static_cast<int>(index);
        item.pszText = const_cast<LPWSTR>(app.name.c_str());
        item.lParam = static_cast<LPARAM>(index);
        const int row = ListView_InsertItem(state.list, &item);
        ListView_SetItemText(state.list, row, 1, const_cast<LPWSTR>(app.version.c_str()));
        ListView_SetItemText(state.list, row, 2, const_cast<LPWSTR>(app.publisher.c_str()));
        std::wstring size = FormatInstalledSize(app.estimatedBytes);
        ListView_SetItemText(state.list, row, 3, const_cast<LPWSTR>(size.c_str()));
        std::wstring package = WideFromUtf8(state.apps[index].packageId);
        ListView_SetItemText(state.list, row, 4, const_cast<LPWSTR>(package.c_str()));
    }
    const std::wstring status = message.empty() ? DecimalWideString(state.apps.size()) + L" installed applications" : message;
    SetWindowTextW(state.status, status.c_str());
    EnableWindow(state.uninstall, FALSE);
}

DWORD WINAPI UninstallThread(LPVOID parameter)
{
    UninstallJob *job = static_cast<UninstallJob *>(parameter);
    DWORD exitCode = 0;
    job->status = UninstallArpEntry(job->package.entry, job->package.installerType, InstallMode::Default, exitCode);
    if (job->status && !job->package.installerType.empty())
        RemoveTrackedPackage(job->package.packageId);
    std::printf("LNTGET_GUI_UNINSTALL_DONE id=%s status=%lu\n", job->package.entry.Identity().c_str(),
                job->status ? 0ul : job->status.code);
    if (!PostMessageW(job->window, MessageUninstallDone, 0, reinterpret_cast<LPARAM>(job)))
        delete job;
    return 0;
}

void RunUninstaller(InstalledWindow &state)
{
    const int selected = ListView_GetNextItem(state.list, -1, LVNI_SELECTED);
    if (state.busy || selected < 0 || static_cast<std::size_t>(selected) >= state.apps.size())
        return;
    const InstalledPackage &app = state.apps[selected];
    const std::wstring prompt = L"Run the uninstaller registered by \u201c" + app.entry.name +
                                L"\u201d?\n\nThe application publisher controls the uninstaller.";
    if (MessageBoxW(state.window, prompt.c_str(), L"Confirm uninstall", MB_OKCANCEL | MB_ICONWARNING) != IDOK)
        return;

    UninstallJob *job = new UninstallJob();
    job->window = state.window;
    job->package = app;
    HANDLE thread = CreateThread(nullptr, 0, UninstallThread, job, 0, nullptr);
    if (!thread)
    {
        const DWORD error = GetLastError();
        delete job;
        const std::wstring message = L"Cannot start the uninstaller.\n\n" + WideFromUtf8(WindowsErrorMessage(error));
        MessageBoxW(state.window, message.c_str(), app.entry.name.c_str(), MB_OK | MB_ICONERROR);
        return;
    }
    state.thread = thread;
    state.busy = true;
    EnableWindow(state.uninstall, FALSE);
    EnableWindow(state.refresh, FALSE);
    EnableWindow(state.close, FALSE);
    const std::wstring status = L"Uninstalling " + app.entry.name + L"\u2026";
    SetWindowTextW(state.status, status.c_str());
}

void LayoutInstalled(InstalledWindow &state)
{
    RECT client{};
    GetClientRect(state.window, &client);
    const int margin = 12;
    const int buttonWidth = 92;
    const int buttonHeight = 28;
    const int bottom = client.bottom - margin - buttonHeight;
    MoveWindow(state.list, margin, margin, client.right - margin * 2, std::max(80, bottom - margin * 2 - 24), TRUE);
    MoveWindow(state.status, margin, bottom - 22, std::max(80, static_cast<int>(client.right) - margin * 2), 20, TRUE);
    int right = client.right - margin;
    MoveWindow(state.close, right - buttonWidth, bottom, buttonWidth, buttonHeight, TRUE);
    right -= buttonWidth + 8;
    MoveWindow(state.refresh, right - buttonWidth, bottom, buttonWidth, buttonHeight, TRUE);
    right -= buttonWidth + 8;
    MoveWindow(state.uninstall, right - buttonWidth, bottom, buttonWidth, buttonHeight, TRUE);
}

LRESULT CALLBACK InstalledProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    InstalledWindow *state = reinterpret_cast<InstalledWindow *>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE)
    {
        const CREATESTRUCTW *create = reinterpret_cast<const CREATESTRUCTW *>(lParam);
        state = static_cast<InstalledWindow *>(create->lpCreateParams);
        state->window = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }
    if (!state)
        return DefWindowProcW(window, message, wParam, lParam);

    switch (message)
    {
        case WM_CREATE:
        {
            HINSTANCE instance = GetModuleHandleW(nullptr);
            state->list = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                                           WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                                           0, 0, 10, 10, window, reinterpret_cast<HMENU>(IdInstalledList), instance, nullptr);
            ListView_SetExtendedListViewStyle(state->list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
            static const struct { const wchar_t *name; int width; } columns[] = {
                {L"Name", 240}, {L"Version", 100}, {L"Publisher", 180}, {L"Size", 80}, {L"Package", 160},
            };
            LVCOLUMNW column{};
            column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
            for (int index = 0; index < static_cast<int>(ARRAYSIZE(columns)); ++index)
            {
                column.iSubItem = index;
                column.pszText = const_cast<LPWSTR>(columns[index].name);
                column.cx = columns[index].width;
                ListView_InsertColumn(state->list, index, &column);
            }
            state->status = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT,
                                             0, 0, 10, 10, window, reinterpret_cast<HMENU>(IdInstalledStatus), instance, nullptr);
            state->uninstall = CreateWindowExW(0, L"BUTTON", L"Uninstall", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_DISABLED,
                                                0, 0, 10, 10, window, reinterpret_cast<HMENU>(IdUninstall), instance, nullptr);
            state->refresh = CreateWindowExW(0, L"BUTTON", L"Refresh", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                              0, 0, 10, 10, window, reinterpret_cast<HMENU>(IdInstalledRefresh), instance, nullptr);
            state->close = CreateWindowExW(0, L"BUTTON", L"Close", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                                            0, 0, 10, 10, window, reinterpret_cast<HMENU>(IdInstalledClose), instance, nullptr);
            NONCLIENTMETRICSW metrics{};
            metrics.cbSize = sizeof(metrics);
            if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0))
                state->font = CreateFontIndirectW(&metrics.lfMessageFont);
            const HWND children[] = {state->list, state->status, state->uninstall, state->refresh, state->close};
            for (HWND child : children)
                SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(state->font), TRUE);
            RefreshInstalled(*state);
            return 0;
        }
        case WM_SIZE:
            LayoutInstalled(*state);
            return 0;
        case WM_GETMINMAXINFO:
        {
            MINMAXINFO *info = reinterpret_cast<MINMAXINFO *>(lParam);
            info->ptMinTrackSize.x = 640;
            info->ptMinTrackSize.y = 380;
            return 0;
        }
        case WM_NOTIFY:
            if (reinterpret_cast<NMHDR *>(lParam)->idFrom == IdInstalledList &&
                reinterpret_cast<NMHDR *>(lParam)->code == LVN_ITEMCHANGED)
            {
                const int selected = ListView_GetNextItem(state->list, -1, LVNI_SELECTED);
                EnableWindow(state->uninstall, !state->busy && selected >= 0 &&
                             static_cast<std::size_t>(selected) < state->apps.size() &&
                             !BuildUninstallCommand(state->apps[selected].entry, state->apps[selected].installerType,
                                                    InstallMode::Default).empty());
            }
            return 0;
        case MessageUninstallDone:
        {
            UninstallJob *job = reinterpret_cast<UninstallJob *>(lParam);
            WaitForSingleObject(state->thread, INFINITE);
            CloseHandle(state->thread);
            state->thread = nullptr;
            state->busy = false;
            EnableWindow(state->refresh, TRUE);
            EnableWindow(state->close, TRUE);
            const std::wstring message = job->status ? job->package.entry.name + L" was uninstalled." :
                                         job->package.entry.name + L" was not uninstalled: " + WideFromUtf8(job->status.message);
            delete job;
            RefreshInstalled(*state, message);
            return 0;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == IdUninstall)
                RunUninstaller(*state);
            else if (LOWORD(wParam) == IdInstalledRefresh && !state->busy)
                RefreshInstalled(*state);
            else if ((LOWORD(wParam) == IdInstalledClose || LOWORD(wParam) == IDCANCEL) && !state->busy)
                DestroyWindow(window);
            return 0;
        case WM_CLOSE:
            if (!state->busy)
                DestroyWindow(window);
            return 0;
        case WM_DESTROY:
            if (state->font)
                DeleteObject(state->font);
            state->font = nullptr;
            return 0;
        default:
            break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

} // namespace

Status ShowInstalledAppsManager(HWND owner)
{
    HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = InstalledProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_LNTGET));
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    windowClass.lpszClassName = L"LntGetInstalledApps";
    if (!RegisterClassExW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return Status::Fail(GetLastError(), "cannot register the installed applications window");

    InstalledWindow state;
    state.owner = owner;
    RECT ownerRect{};
    GetWindowRect(owner, &ownerRect);
    HWND window = CreateWindowExW(WS_EX_DLGMODALFRAME, windowClass.lpszClassName, L"Installed apps — lntget",
                                  WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                  ownerRect.left + 36, ownerRect.top + 36, 780, 520,
                                  owner, nullptr, instance, &state);
    if (!window)
        return Status::Fail(GetLastError(), "cannot create the installed applications window");

    EnableWindow(owner, FALSE);
    MSG message{};
    int messageResult = 1;
    while (IsWindow(window) && (messageResult = GetMessageW(&message, nullptr, 0, 0)) > 0)
    {
        if (IsDialogMessageW(window, &message))
            continue;
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (state.thread)
    {
        WaitForSingleObject(state.thread, INFINITE);
        CloseHandle(state.thread);
        state.thread = nullptr;
    }
    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);
    if (!messageResult)
        PostQuitMessage(static_cast<int>(message.wParam));
    return Status::Ok();
}

} // namespace lntget
