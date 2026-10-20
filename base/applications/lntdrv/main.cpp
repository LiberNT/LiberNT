/*
 * PROJECT:     LiberNT Drivers
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Driver inventory and Microsoft Update Catalog installer
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "catalog.hpp"
#include "devices.hpp"
#include "resource.h"
#include "text.hpp"
#include "trust.hpp"

#include <commctrl.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>

#include <algorithm>
#include <climits>
#include <map>
#include <memory>

namespace lntdrv
{

namespace
{

enum : UINT
{
    WM_APP_PROGRESS = WM_APP + 1,
    WM_APP_RESULTS,
    WM_APP_SEARCHED,
    WM_APP_INSTALLED,
    WM_APP_ACTION,
    WM_APP_NAME,
};

enum Page
{
    PageDevices,
    PageSearch,
    PageChoose,
    PageInstall,
    PageDone,
    PageHistory,
    PageCount
};

enum class ChoiceState
{
    Missing,
    Newer,
    Same,
    Older
};

struct PageInfo
{
    UINT Template;
    UINT Title;
    UINT Subtitle;
};

const PageInfo Pages[PageCount] =
{
    { IDD_DEVICES, IDS_DEVICES_TITLE, IDS_DEVICES_SUBTITLE },
    { IDD_PROGRESS, IDS_SEARCH_TITLE, IDS_SEARCH_SUBTITLE },
    { IDD_CHOOSE, IDS_CHOOSE_TITLE, IDS_CHOOSE_SUBTITLE },
    { IDD_PROGRESS, IDS_INSTALL_TITLE, IDS_INSTALL_SUBTITLE },
    { IDD_DONE, IDS_DONE_TITLE, IDS_DONE_SUBTITLE },
    { IDD_HISTORY, IDS_HISTORY_TITLE, IDS_HISTORY_SUBTITLE },
};

const DWORD DownloadsKeptDays = 14;
const size_t MaximumCandidates = 3;

struct ProgressMessage
{
    std::wstring Text;
    std::wstring Detail;
    int Percent;
};

struct ResultsMessage
{
    std::wstring InstanceId;
    std::wstring Note;
    DWORD Error;
    std::vector<CatalogUpdate> Updates;
};

struct Choice
{
    DeviceInfo Device;
    CatalogUpdate Update;
    ChoiceState State;
};

struct Outcome
{
    std::wstring Device;
    std::wstring Driver;
    std::wstring Detail;
    DWORD Error;
    BOOL Reboot;
    bool Skipped;
};

struct InstalledMessage
{
    std::vector<Outcome> Outcomes;
    bool Cancelled;
};

struct ActionMessage
{
    DWORD Error;
    BOOL Reboot;
};

struct NameMessage
{
    std::wstring InstanceId;
    std::wstring Name;
};

using Reporter = std::function<void(const std::wstring &text, int percent)>;
using CancelFlag = std::shared_ptr<volatile LONG>;

template <class T>
void Post(HWND hwnd, UINT message, UINT job, T *payload)
{
    if (!PostMessageW(hwnd, message, job, (LPARAM)payload))
        delete payload;
}

bool IsCancelled(const CancelFlag &cancel)
{
    return cancel && InterlockedCompareExchange(cancel.get(), 0, 0) != 0;
}

std::wstring CatalogNote(const DeviceInfo &device, const std::vector<CatalogUpdate> &updates, DWORD error)
{
    if (error == ERROR_NOT_SUPPORTED)
        return LoadText(IDS_NOTE_NOT_SEARCHABLE);
    if (error == ERROR_NOT_FOUND || (!error && updates.empty()))
        return LoadText(IDS_NOTE_NOT_FOUND);
    if (error)
        return FormatText(IDS_NOTE_FAILED, { ErrorText(error) });
    switch (CompareVersionText(device, updates.front().Version))
    {
        case DriverOrder::Missing:
            return FormatText(IDS_NOTE_AVAILABLE, { updates.front().Version });
        case DriverOrder::Newer:
            return FormatText(IDS_NOTE_NEWER, { updates.front().Version });
        default:
            return LoadText(IDS_NOTE_CURRENT);
    }
}

ChoiceState StateOf(const DeviceInfo &device, const CatalogUpdate &update, const std::vector<HistoryEntry> &history)
{
    const HistoryEntry *entry = CatalogEntryInUse(device, history);

    if (entry && _wcsicmp(entry->UpdateId.c_str(), update.Id.c_str()) == 0)
        return ChoiceState::Same;
    switch (CompareVersionText(device, update.Version))
    {
        case DriverOrder::Missing:
            return ChoiceState::Missing;
        case DriverOrder::Newer:
            return ChoiceState::Newer;
        case DriverOrder::Same:
            return ChoiceState::Same;
        default:
            return ChoiceState::Older;
    }
}

bool Updatable(const DeviceInfo &device)
{
    if (CatalogQueries(device).empty())
        return false;
    return NeedsDriver(device) || _wcsnicmp(device.Inf.c_str(), L"oem", 3) == 0;
}

bool Selectable(ChoiceState state)
{
    return state == ChoiceState::Missing || state == ChoiceState::Newer;
}

DWORD SearchDevice(Catalog &catalog, const DeviceInfo &device, std::vector<CatalogUpdate> &updates)
{
    std::vector<std::wstring> queries = CatalogQueries(device);

    updates.clear();
    if (queries.empty())
        return ERROR_NOT_SUPPORTED;
    return catalog.FindUpdates(queries, updates);
}

std::wstring SafeFileName(const std::wstring &name)
{
    size_t slash = name.find_last_of(L"\\/");
    std::wstring file = slash == std::wstring::npos ? name : name.substr(slash + 1);

    if (file.empty() || file == L"." || file == L".." || file.find(L':') != std::wstring::npos)
        return std::wstring();
    return file;
}

DWORD RunInstall(HWND owner, const DeviceInfo &device, const CatalogUpdate &update, BOOL &reboot,
                 std::wstring &detail, const Reporter &report, const CancelFlag &cancel)
{
    Catalog catalog;
    std::vector<CatalogFile> files;
    std::wstring directory = PackageDirectory(update.Id), package = directory + L"\\package";
    HistoryEntry entry;
    PackageDriver driver;
    DWORD error;

    reboot = FALSE;
    detail.clear();
    catalog.SetCancel(cancel.get());
    entry.Action = L"install";
    entry.UpdateId = update.Id;
    entry.Title = update.Title;
    entry.Version = update.Version;
    entry.Device = device.Name;
    entry.InstanceId = device.InstanceId;
    entry.PreviousInf = device.Inf;
    entry.PreviousVersion = device.DriverVersion;

    report(LoadText(IDS_PROGRESS_QUERY), -1);
    error = PrepareDownloads();
    if (!error)
        error = catalog.QueryFiles(update.Id, files);
    if (!error && directory.empty())
        error = ERROR_PATH_NOT_FOUND;
    if (!error)
    {
        int created = SHCreateDirectoryExW(NULL, directory.c_str(), NULL);
        if (created != ERROR_SUCCESS && created != ERROR_ALREADY_EXISTS && created != ERROR_FILE_EXISTS)
            error = (DWORD)created;
        DeleteTree(package);
    }
    for (size_t i = 0; !error && i < files.size(); i++)
    {
        std::wstring name = SafeFileName(files[i].FileName), path = directory + L"\\" + name;
        int last = -2;

        if (name.empty())
        {
            error = ERROR_INVALID_DATA;
            break;
        }
        error = catalog.Download(files[i], path, [&](ULONGLONG received, ULONGLONG total) {
            int step = total ? (int)(received * 100 / total) : (int)(received >> 18);
            if (step != last)
            {
                last = step;
                report(FormatText(IDS_PROGRESS_DOWNLOAD, { name, std::to_wstring(received / 1024) }),
                       total ? step : -1);
            }
        });
        if (!error)
        {
            report(FormatText(IDS_PROGRESS_UNPACK, { name }), -1);
            error = ExtractPackage(path, package);
        }
    }
    if (!error && IsCancelled(cancel))
        error = ERROR_CANCELLED;
    if (!error)
    {
        report(LoadText(IDS_PROGRESS_VERIFY), -1);
        error = VerifyPackage(package, detail);
    }
    if (!error)
    {
        report(LoadText(IDS_PROGRESS_CHECK), -1);
        error = FindPackageDriver(device, PackageInfs(package), driver);
    }
    if (!error)
    {
        DriverOrder order = CompareWithInstalled(device, driver);

        entry.HardwareId = driver.HardwareId;
        if (order == DriverOrder::Same)
            error = ERROR_LNTDRV_ALREADY_INSTALLED;
        else if (order == DriverOrder::Older)
            error = ERROR_LNTDRV_NOT_NEWER;
    }
    if (!error && IsCancelled(cancel))
        error = ERROR_CANCELLED;
    if (!error)
    {
        report(FormatText(IDS_PROGRESS_INSTALL, { device.Name }), -1);
        error = InstallPackageDriver(owner, driver, false, reboot);
    }
    entry.Result = error;
    entry.Reboot = reboot;
    if (!error)
    {
        DeviceInfo current;
        if (FindDevice(device.InstanceId, current))
            entry.Inf = current.Inf;
        DeleteTree(directory);
    }
    SaveHistory(entry);
    return error;
}

std::wstring OutcomeText(const Outcome &outcome)
{
    std::wstring text;

    if (outcome.Skipped)
        return LoadText(IDS_RESULT_SKIPPED);
    if (!outcome.Error)
        return LoadText(outcome.Reboot ? IDS_RESULT_REBOOT : IDS_RESULT_INSTALLED);
    text = ErrorText(outcome.Error);
    return outcome.Detail.empty() ? text : FormatText(IDS_RESULT_FILE, { text, outcome.Detail });
}

std::wstring HistoryResult(const HistoryEntry &entry)
{
    if (entry.Result)
        return ErrorText(entry.Result);
    return LoadText(entry.Reboot ? IDS_RESULT_REBOOT : IDS_RESULT_INSTALLED);
}

UINT ActionText(const std::wstring &action)
{
    if (action == L"rollback")
        return IDS_ACTION_ROLLBACK;
    if (action == L"remove")
        return IDS_ACTION_REMOVE;
    return IDS_ACTION_INSTALL;
}

void SetCell(HWND list, int item, int column, const std::wstring &text)
{
    LVITEMW cell = { 0 };

    cell.iSubItem = column;
    cell.pszText = (LPWSTR)text.c_str();
    SendMessageW(list, LVM_SETITEMTEXTW, item, (LPARAM)&cell);
}

int AddRow(HWND list, const std::wstring &text)
{
    LVITEMW item = { 0 };

    item.mask = LVIF_TEXT;
    item.iItem = INT_MAX;
    item.pszText = (LPWSTR)text.c_str();
    return (int)SendMessageW(list, LVM_INSERTITEMW, 0, (LPARAM)&item);
}

void InitList(HWND list, DWORD extraStyle, const std::vector<std::pair<UINT, int>> &columns)
{
    RECT client;
    int width;

    ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP | extraStyle);
    GetClientRect(list, &client);
    width = client.right - GetSystemMetrics(SM_CXVSCROLL);
    for (size_t i = 0; i < columns.size(); i++)
    {
        LVCOLUMNW column = { 0 };
        std::wstring title = LoadText(columns[i].first);

        column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        column.pszText = (LPWSTR)title.c_str();
        column.cx = width * columns[i].second / 100;
        column.iSubItem = (int)i;
        SendMessageW(list, LVM_INSERTCOLUMNW, i, (LPARAM)&column);
    }
}

int SelectedRow(HWND list)
{
    return ListView_GetNextItem(list, -1, LVNI_SELECTED);
}

void PaintPanelEdge(HDC hdc, int x, int y, int cx, int cy, int radius, COLORREF color)
{
    BITMAPINFO bmi = {};
    ULONG *bits = NULL;
    HBITMAP bitmap;
    HDC memory;
    LONG limit = (LONG)radius * 8 * (LONG)radius * 8;

    if (cx <= 0 || cy <= 0)
        return;
    bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
    bmi.bmiHeader.biWidth = cx;
    bmi.bmiHeader.biHeight = -cy;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    bitmap = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, (void **)&bits, NULL, 0);
    if (!bitmap)
        return;
    for (int j = 0; j < cy; j++)
    {
        for (int i = 0; i < cx; i++)
        {
            ULONG alpha = 255;

            if (i < radius && j < radius)
            {
                ULONG inside = 0;

                for (int sy = 0; sy < 4; sy++)
                {
                    for (int sx = 0; sx < 4; sx++)
                    {
                        LONG dx = radius * 8 - (i * 8 + sx * 2 + 1);
                        LONG dy = radius * 8 - (j * 8 + sy * 2 + 1);

                        if (dx * dx + dy * dy <= limit)
                            inside++;
                    }
                }
                alpha = inside * 255 / 16;
            }
            bits[(size_t)j * cx + i] = (alpha << 24) | ((GetRValue(color) * alpha / 255) << 16) |
                                       ((GetGValue(color) * alpha / 255) << 8) | (GetBValue(color) * alpha / 255);
        }
    }
    memory = CreateCompatibleDC(hdc);
    if (memory)
    {
        HGDIOBJ old = SelectObject(memory, bitmap);

        BitBlt(hdc, x, y, cx, cy, memory, 0, 0, SRCCOPY);
        SelectObject(memory, old);
        DeleteDC(memory);
    }
    DeleteObject(bitmap);
}

INT_PTR CALLBACK DetailsProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
        case WM_INITDIALOG:
            SetDlgItemTextW(hwnd, IDC_DETAILS_TEXT, (LPCWSTR)lParam);
            return TRUE;
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

class DriversWizard
{
public:
    INT_PTR Run(HINSTANCE instance)
    {
        INITCOMMONCONTROLSEX controls = { sizeof(controls), ICC_LISTVIEW_CLASSES | ICC_PROGRESS_CLASS };

        InitCommonControlsEx(&controls);
        m_Instance = instance;
        return DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_FRAME), NULL, FrameProc, (LPARAM)this);
    }

private:
    static INT_PTR CALLBACK FrameProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        DriversWizard *self;

        if (msg == WM_INITDIALOG)
        {
            self = (DriversWizard *)lParam;
            self->m_hwnd = hwnd;
            SetWindowLongPtrW(hwnd, DWLP_USER, (LONG_PTR)self);
        }
        else
        {
            self = (DriversWizard *)GetWindowLongPtrW(hwnd, DWLP_USER);
        }
        return self ? self->OnFrame(msg, wParam, lParam) : FALSE;
    }

    static INT_PTR CALLBACK PageProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        DriversWizard *self;

        if (msg == WM_INITDIALOG)
        {
            SetWindowLongPtrW(hwnd, DWLP_USER, lParam);
            return FALSE;
        }
        self = (DriversWizard *)GetWindowLongPtrW(hwnd, DWLP_USER);
        return self ? self->OnPage(hwnd, msg, wParam, lParam) : FALSE;
    }

    INT_PTR OnFrame(UINT msg, WPARAM wParam, LPARAM lParam)
    {
        switch (msg)
        {
            case WM_INITDIALOG:
                InitFrame();
                return FALSE;
            case WM_PAINT:
                PaintColumn();
                return TRUE;
            case WM_COMMAND:
                switch (LOWORD(wParam))
                {
                    case IDOK:
                    case IDC_FRAME_NEXT:
                        Next();
                        return TRUE;
                    case IDC_FRAME_BACK:
                        Back();
                        return TRUE;
                    case IDCANCEL:
                        Cancel();
                        return TRUE;
                }
                break;
            case WM_APP_PROGRESS:
            {
                std::unique_ptr<ProgressMessage> progress((ProgressMessage *)lParam);
                if ((UINT)wParam == m_Job && (m_Current == PageSearch || m_Current == PageInstall))
                {
                    SetProgress(m_Pages[m_Current], progress->Text,
                                IsCancelled(m_Cancel) ? LoadText(IDS_INSTALL_CANCELLING) : progress->Detail,
                                progress->Percent);
                }
                return TRUE;
            }
            case WM_APP_RESULTS:
            {
                std::unique_ptr<ResultsMessage> results((ResultsMessage *)lParam);
                if ((UINT)wParam == m_Job)
                {
                    m_Found[results->InstanceId] = results->Updates;
                    if (results->Updates.empty() && results->Error != ERROR_NOT_FOUND)
                        m_SearchErrors.push_back(results->Note);
                }
                return TRUE;
            }
            case WM_APP_SEARCHED:
                if ((UINT)wParam == m_Job)
                {
                    FillChoices();
                    Show(PageChoose);
                }
                return TRUE;
            case WM_APP_INSTALLED:
            {
                std::unique_ptr<InstalledMessage> installed((InstalledMessage *)lParam);
                if ((UINT)wParam == m_Job)
                {
                    m_Outcomes = installed->Outcomes;
                    FillDone(installed->Cancelled);
                    m_Selected.clear();
                    LoadDevices();
                    Show(PageDone);
                }
                return TRUE;
            }
            case WM_APP_ACTION:
            {
                std::unique_ptr<ActionMessage> action((ActionMessage *)lParam);
                if ((UINT)wParam == m_Job)
                {
                    m_Busy = false;
                    LoadDevices();
                    FillHistory();
                    UpdateButtons();
                    SetDlgItemTextW(m_Pages[PageHistory], IDC_HISTORY_NOTE, L"");
                    if (action->Error)
                        Message(TD_ERROR_ICON, FormatText(IDS_ACTION_FAILED, { ErrorText(action->Error) }));
                    else
                        Message(TD_INFORMATION_ICON, LoadText(action->Reboot ? IDS_ACTION_DONE_REBOOT : IDS_ACTION_DONE));
                }
                return TRUE;
            }
            case WM_APP_NAME:
            {
                std::unique_ptr<NameMessage> name((NameMessage *)lParam);
                HWND list = GetDlgItem(m_Pages[PageDevices], IDC_DEVICES);

                for (size_t i = 0; i < m_Devices.size(); i++)
                {
                    if (m_Devices[i].InstanceId == name->InstanceId)
                    {
                        m_Devices[i].Name = name->Name;
                        m_Devices[i].NameResolved = true;
                        m_Names[name->InstanceId] = name->Name;
                        SetCell(list, (int)i, 0, name->Name);
                    }
                }
                return TRUE;
            }
            case WM_DESTROY:
                InterlockedExchange(m_NameCancel.get(), 1);
                if (m_Cancel)
                    InterlockedExchange(m_Cancel.get(), 1);
                if (m_TitleFont)
                    DeleteObject(m_TitleFont);
                if (m_Watermark)
                    DeleteObject(m_Watermark);
                m_TitleFont = NULL;
                m_Watermark = NULL;
                break;
        }
        return FALSE;
    }

    INT_PTR OnPage(HWND page, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        if (msg == WM_COMMAND && HIWORD(wParam) == BN_CLICKED)
        {
            switch (LOWORD(wParam))
            {
                case IDC_HISTORY:
                    FillHistory();
                    Show(PageHistory);
                    return TRUE;
                case IDC_DETAILS:
                    ShowDetails();
                    return TRUE;
                case IDC_ROLLBACK:
                    StartAction(true);
                    return TRUE;
                case IDC_REMOVE:
                    StartAction(false);
                    return TRUE;
            }
        }
        else if (msg == WM_NOTIFY)
        {
            NMHDR *header = (NMHDR *)lParam;

            if (header->idFrom == IDC_DEVICES)
                return OnDeviceNotify(page, header);
            else if (header->idFrom == IDC_HISTORY_LIST && header->code == LVN_ITEMCHANGED)
                UpdateButtons();
            else if (header->idFrom == IDC_CHOICES)
                return OnChoiceNotify(page, header);
        }
        return FALSE;
    }

    INT_PTR OnDeviceNotify(HWND page, NMHDR *header)
    {
        NMLISTVIEW *change = (NMLISTVIEW *)header;
        bool image = (header->code == LVN_ITEMCHANGING || header->code == LVN_ITEMCHANGED) &&
                     change->iItem >= 0 && change->iItem < (int)m_Devices.size() && (change->uChanged & LVIF_STATE) &&
                     ((change->uNewState ^ change->uOldState) & LVIS_STATEIMAGEMASK);

        if (header->code == NM_DBLCLK)
            ShowDetails();
        else if (header->code == LVN_ITEMCHANGING && image && !m_Filling && !Updatable(m_Devices[change->iItem]))
        {
            SetWindowLongPtrW(page, DWLP_MSGRESULT, TRUE);
            return TRUE;
        }
        else if (header->code == LVN_ITEMCHANGED)
        {
            if (image && !m_Filling)
            {
                m_Selected[m_Devices[change->iItem].InstanceId] =
                    ListView_GetCheckState(header->hwndFrom, change->iItem) == 1;
                UpdateDevicesNote();
            }
            UpdateButtons();
        }
        return FALSE;
    }

    INT_PTR OnChoiceNotify(HWND page, NMHDR *header)
    {
        HWND list = header->hwndFrom;
        NMLISTVIEW *change = (NMLISTVIEW *)header;

        UNREFERENCED_PARAMETER(page);
        if (header->code == LVN_ITEMCHANGED && !m_Filling)
        {
            if (change->iItem >= 0 && change->iItem < (int)m_Choices.size() && (change->uChanged & LVIF_STATE) &&
                ((change->uNewState ^ change->uOldState) & LVIS_STATEIMAGEMASK) &&
                ListView_GetCheckState(list, change->iItem) == 1)
            {
                m_Filling = true;
                for (int i = 0; i < (int)m_Choices.size(); i++)
                {
                    if (i != change->iItem &&
                        m_Choices[i].Device.InstanceId == m_Choices[change->iItem].Device.InstanceId)
                    {
                        ListView_SetCheckState(list, i, FALSE);
                    }
                }
                m_Filling = false;
            }
            UpdateButtons();
        }
        return FALSE;
    }

    void Message(PCWSTR icon, const std::wstring &text)
    {
        TASKDIALOGCONFIG config = { sizeof(config) };

        config.hwndParent = m_hwnd;
        config.hInstance = m_Instance;
        config.dwFlags = TDF_POSITION_RELATIVE_TO_WINDOW;
        config.dwCommonButtons = TDCBF_OK_BUTTON;
        config.pszWindowTitle = MAKEINTRESOURCEW(IDS_CAPTION);
        config.pszMainIcon = icon;
        config.pszContent = text.c_str();
        TaskDialogIndirect(&config, NULL, NULL, NULL);
    }

    bool Ask(const std::wstring &instruction, const std::wstring &text)
    {
        TASKDIALOGCONFIG config = { sizeof(config) };
        int button = IDNO;

        config.hwndParent = m_hwnd;
        config.hInstance = m_Instance;
        config.dwFlags = TDF_POSITION_RELATIVE_TO_WINDOW;
        config.dwCommonButtons = TDCBF_YES_BUTTON | TDCBF_NO_BUTTON;
        config.nDefaultButton = IDNO;
        config.pszWindowTitle = MAKEINTRESOURCEW(IDS_CAPTION);
        config.pszMainIcon = TD_WARNING_ICON;
        config.pszMainInstruction = instruction.c_str();
        config.pszContent = text.c_str();
        return SUCCEEDED(TaskDialogIndirect(&config, &button, NULL, NULL)) && button == IDYES;
    }

    bool EnsureAdministrator()
    {
        WCHAR path[MAX_PATH];
        SHELLEXECUTEINFOW execute = { sizeof(execute) };

        if (IsUserAnAdmin())
            return true;
        if (!Ask(LoadText(IDS_ADMIN_REQUIRED), LoadText(IDS_ADMIN_RESTART)) ||
            !GetModuleFileNameW(NULL, path, _countof(path)))
        {
            return false;
        }
        execute.hwnd = m_hwnd;
        execute.lpVerb = L"runas";
        execute.lpFile = path;
        execute.nShow = SW_SHOWNORMAL;
        if (ShellExecuteExW(&execute))
            EndDialog(m_hwnd, IDCANCEL);
        return false;
    }

    void InitFrame()
    {
        NONCLIENTMETRICSW metrics = { sizeof(metrics) };
        MARGINS margins = { 0, 0, 0, 0 };
        HICON icon = LoadIconW(m_Instance, MAKEINTRESOURCEW(IDI_LNTDRV));
        BITMAP bm;
        RECT rc;
        HDC hdc;

        SendMessageW(m_hwnd, WM_SETICON, ICON_BIG, (LPARAM)icon);
        SendMessageW(m_hwnd, WM_SETICON, ICON_SMALL, (LPARAM)icon);
        if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0))
        {
            LOGFONTW font = metrics.lfMessageFont;

            font.lfWeight = FW_BOLD;
            hdc = GetDC(NULL);
            font.lfHeight = -MulDiv(12, hdc ? GetDeviceCaps(hdc, LOGPIXELSY) : 96, 72);
            if (hdc)
                ReleaseDC(NULL, hdc);
            m_TitleFont = CreateFontIndirectW(&font);
            SendDlgItemMessageW(m_hwnd, IDC_FRAME_TITLE, WM_SETFONT, (WPARAM)m_TitleFont, FALSE);
        }

        m_Watermark = (HBITMAP)LoadImageW(m_Instance, MAKEINTRESOURCEW(IDB_WATERMARK), IMAGE_BITMAP, 0, 0,
                                          LR_CREATEDIBSECTION);
        if (m_Watermark && GetObjectW(m_Watermark, sizeof(bm), &bm))
        {
            m_WatermarkSize.cx = bm.bmWidth;
            m_WatermarkSize.cy = bm.bmHeight;
        }
        hdc = GetDC(m_hwnd);
        m_Corner = MulDiv(8, hdc ? GetDeviceCaps(hdc, LOGPIXELSX) : 96, 96);
        if (hdc)
            ReleaseDC(m_hwnd, hdc);
        margins.cxLeftWidth = m_WatermarkSize.cx + m_Corner;
        margins.cyTopHeight = m_Corner;
        DwmExtendFrameIntoClientArea(m_hwnd, &margins);

        GetWindowRect(GetDlgItem(m_hwnd, IDC_FRAME_PAGE), &rc);
        MapWindowPoints(HWND_DESKTOP, m_hwnd, (LPPOINT)&rc, 2);
        for (int page = 0; page < PageCount; page++)
        {
            m_Pages[page] = CreateDialogParamW(m_Instance, MAKEINTRESOURCEW(Pages[page].Template), m_hwnd, PageProc,
                                               (LPARAM)this);
            if (!m_Pages[page])
            {
                EndDialog(m_hwnd, IDABORT);
                return;
            }
            SetWindowPos(m_Pages[page], HWND_TOP, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
                         SWP_NOACTIVATE);
        }
        InitList(GetDlgItem(m_Pages[PageDevices], IDC_DEVICES), LVS_EX_CHECKBOXES,
                 { { IDS_COL_DEVICE, 42 }, { IDS_COL_STATUS, 20 }, { IDS_COL_VERSION, 17 }, { IDS_COL_SOURCE, 21 } });
        InitList(GetDlgItem(m_Pages[PageChoose], IDC_CHOICES), LVS_EX_CHECKBOXES,
                 { { IDS_COL_DEVICE, 30 }, { IDS_COL_DRIVER, 34 }, { IDS_COL_OFFERED, 14 }, { IDS_COL_STATUS, 22 } });
        InitList(GetDlgItem(m_Pages[PageDone], IDC_RESULTS), 0,
                 { { IDS_COL_DEVICE, 34 }, { IDS_COL_DRIVER, 34 }, { IDS_COL_RESULT, 32 } });
        InitList(GetDlgItem(m_Pages[PageHistory], IDC_HISTORY_LIST), 0,
                 { { IDS_COL_DATE, 22 }, { IDS_COL_ACTION, 14 }, { IDS_COL_DEVICE, 24 }, { IDS_COL_DRIVER, 24 },
                   { IDS_COL_RESULT, 16 } });
        SendDlgItemMessageW(m_Pages[PageSearch], IDC_PROGRESS_BAR, PBM_SETRANGE32, 0, 100);
        SendDlgItemMessageW(m_Pages[PageInstall], IDC_PROGRESS_BAR, PBM_SETRANGE32, 0, 100);

        Center();
        if (IsUserAnAdmin())
            CleanupDownloads(DownloadsKeptDays);
        LoadDevices();
        Show(PageDevices);
        SetFocus(GetDlgItem(m_Pages[PageDevices], IDC_DEVICES));
        ResolveNames();
    }

    void Center()
    {
        RECT work, window;

        SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
        GetWindowRect(m_hwnd, &window);
        SetWindowPos(m_hwnd, HWND_TOP, work.left + ((work.right - work.left) - (window.right - window.left)) / 2,
                     work.top + ((work.bottom - work.top) - (window.bottom - window.top)) / 2, 0, 0, SWP_NOSIZE);
    }

    void PaintColumn()
    {
        PAINTSTRUCT ps;
        RECT rc;
        HDC hdc = BeginPaint(m_hwnd, &ps);

        GetClientRect(m_hwnd, &rc);
        rc.right = m_WatermarkSize.cx;
        FillRect(hdc, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));
        if (m_Watermark)
        {
            HDC memory = CreateCompatibleDC(hdc);

            if (memory)
            {
                HGDIOBJ old = SelectObject(memory, m_Watermark);

                BitBlt(hdc, 0, 0, m_WatermarkSize.cx, std::min<LONG>(m_WatermarkSize.cy, rc.bottom), memory, 0, 0,
                       SRCCOPY);
                SelectObject(memory, old);
                DeleteDC(memory);
            }
        }
        GetClientRect(m_hwnd, &rc);
        PaintPanelEdge(hdc, m_WatermarkSize.cx, 0, rc.right - m_WatermarkSize.cx, m_Corner, m_Corner,
                       GetSysColor(COLOR_3DFACE));
        PaintPanelEdge(hdc, m_WatermarkSize.cx, m_Corner, m_Corner, rc.bottom - m_Corner, 0,
                       GetSysColor(COLOR_3DFACE));
        EndPaint(m_hwnd, &ps);
    }

    void Show(int page)
    {
        if (m_Current >= 0 && m_Current != page)
            ShowWindow(m_Pages[m_Current], SW_HIDE);
        m_Current = page;
        SetDlgItemTextW(m_hwnd, IDC_FRAME_TITLE, LoadText(Pages[page].Title).c_str());
        SetDlgItemTextW(m_hwnd, IDC_FRAME_SUBTITLE, LoadText(Pages[page].Subtitle).c_str());
        SetDlgItemTextW(m_hwnd, IDC_FRAME_BACK, LoadText(IDS_BACK).c_str());
        SetDlgItemTextW(m_hwnd, IDC_FRAME_NEXT,
                        LoadText(page == PageChoose ? IDS_INSTALL : page == PageDone ? IDS_FINISH : IDS_NEXT).c_str());
        SetDlgItemTextW(m_hwnd, IDCANCEL, LoadText(IDS_CANCEL).c_str());
        ShowWindow(m_Pages[page], SW_SHOW);
        UpdateButtons();
    }

    const DeviceInfo *SelectedDevice() const
    {
        int row = SelectedRow(GetDlgItem(m_Pages[PageDevices], IDC_DEVICES));
        return row >= 0 && row < (int)m_Devices.size() ? &m_Devices[row] : NULL;
    }

    const HistoryEntry *SelectedEntry() const
    {
        int row = SelectedRow(GetDlgItem(m_Pages[PageHistory], IDC_HISTORY_LIST));
        return row >= 0 && row < (int)m_History.size() ? &m_History[row] : NULL;
    }

    bool EntryInUse(const HistoryEntry &entry) const
    {
        for (const DeviceInfo &device : m_Devices)
        {
            if (_wcsicmp(device.InstanceId.c_str(), entry.InstanceId.c_str()) == 0)
                return !entry.Inf.empty() && _wcsicmp(device.Inf.c_str(), entry.Inf.c_str()) == 0;
        }
        return false;
    }

    void UpdateButtons()
    {
        HWND list = GetDlgItem(m_Pages[PageChoose], IDC_CHOICES);
        const DeviceInfo *device = SelectedDevice();
        const HistoryEntry *entry = SelectedEntry();
        bool back = m_Current == PageChoose || m_Current == PageHistory;
        bool next = m_Current != PageHistory && m_Current != PageSearch && m_Current != PageInstall;
        bool cancel = m_Current != PageDone;
        bool cancelEnabled = m_Current != PageInstall || !IsCancelled(m_Cancel);
        bool nextEnabled = m_Current == PageDone;
        bool usable = entry && !m_Busy && entry->Action == L"install" && !entry->Result && EntryInUse(*entry);

        if (m_Current == PageDevices)
            nextEnabled = !SearchList().empty();
        else if (m_Current == PageChoose)
        {
            for (int i = 0; i < ListView_GetItemCount(list) && !nextEnabled; i++)
                nextEnabled = ListView_GetCheckState(list, i) == 1;
        }
        ShowWindow(GetDlgItem(m_hwnd, IDC_FRAME_BACK), back ? SW_SHOW : SW_HIDE);
        ShowWindow(GetDlgItem(m_hwnd, IDC_FRAME_NEXT), next ? SW_SHOW : SW_HIDE);
        ShowWindow(GetDlgItem(m_hwnd, IDCANCEL), cancel ? SW_SHOW : SW_HIDE);
        EnableWindow(GetDlgItem(m_hwnd, IDC_FRAME_BACK), back && !m_Busy);
        EnableWindow(GetDlgItem(m_hwnd, IDC_FRAME_NEXT), nextEnabled);
        EnableWindow(GetDlgItem(m_hwnd, IDCANCEL), cancelEnabled && !m_Busy);
        EnableMenuItem(GetSystemMenu(m_hwnd, FALSE), SC_CLOSE,
                       MF_BYCOMMAND | (m_Current != PageInstall && !m_Busy ? MF_ENABLED : MF_GRAYED));
        EnableWindow(GetDlgItem(m_Pages[PageDevices], IDC_DETAILS), device != NULL);
        EnableWindow(GetDlgItem(m_Pages[PageHistory], IDC_ROLLBACK), usable);
        EnableWindow(GetDlgItem(m_Pages[PageHistory], IDC_REMOVE), usable);
    }

    void Next()
    {
        HWND next = GetDlgItem(m_hwnd, IDC_FRAME_NEXT);

        if (!IsWindowVisible(next) || !IsWindowEnabled(next))
            return;
        if (m_Current == PageDevices)
            StartSearch();
        else if (m_Current == PageChoose)
            StartInstall();
        else if (m_Current == PageDone)
            EndDialog(m_hwnd, IDOK);
    }

    void Back()
    {
        HWND back = GetDlgItem(m_hwnd, IDC_FRAME_BACK);

        if (IsWindowVisible(back) && IsWindowEnabled(back))
            Show(PageDevices);
    }

    void Cancel()
    {
        if (m_Current == PageDone)
        {
            EndDialog(m_hwnd, IDOK);
            return;
        }
        if (!IsWindowEnabled(GetDlgItem(m_hwnd, IDCANCEL)))
            return;
        if (m_Current == PageSearch)
        {
            InterlockedExchange(m_Cancel.get(), 1);
            m_Job++;
            Show(PageDevices);
        }
        else if (m_Current == PageInstall)
        {
            InterlockedExchange(m_Cancel.get(), 1);
            SetDlgItemTextW(m_Pages[PageInstall], IDC_PROGRESS_DETAIL, LoadText(IDS_INSTALL_CANCELLING).c_str());
            UpdateButtons();
        }
        else
        {
            EndDialog(m_hwnd, IDCANCEL);
        }
    }

    bool Ticked(const DeviceInfo &device) const
    {
        auto selected = m_Selected.find(device.InstanceId);

        if (!Updatable(device))
            return false;
        return selected != m_Selected.end() ? selected->second : NeedsDriver(device);
    }

    std::vector<DeviceInfo> SearchList() const
    {
        std::vector<DeviceInfo> devices;

        for (const DeviceInfo &device : m_Devices)
        {
            if (Ticked(device))
                devices.push_back(device);
        }
        return devices;
    }

    void LoadDevices()
    {
        HWND list = GetDlgItem(m_Pages[PageDevices], IDC_DEVICES);

        m_History = LoadHistory();
        EnumerateDevices(m_Devices);
        for (DeviceInfo &device : m_Devices)
        {
            auto name = m_Names.find(device.InstanceId);
            if (name != m_Names.end() && NeedsDriver(device))
            {
                device.Name = name->second;
                device.NameResolved = true;
            }
        }
        m_Filling = true;
        SendMessageW(list, WM_SETREDRAW, FALSE, 0);
        ListView_DeleteAllItems(list);
        for (const DeviceInfo &device : m_Devices)
        {
            int row = AddRow(list, device.Name);

            SetCell(list, row, 1, DeviceStatus(device));
            SetCell(list, row, 2, device.DriverVersion);
            SetCell(list, row, 3, DriverSource(device, m_History));
            if (Updatable(device))
            {
                ListView_SetCheckState(list, row, Ticked(device));
            }
            else
            {
                ListView_SetItemState(list, row, INDEXTOSTATEIMAGEMASK(0), LVIS_STATEIMAGEMASK);
            }
        }
        SendMessageW(list, WM_SETREDRAW, TRUE, 0);
        m_Filling = false;
        UpdateDevicesNote();
    }

    void UpdateDevicesNote()
    {
        size_t missing = 0;

        for (const DeviceInfo &device : m_Devices)
        {
            if (NeedsDriver(device))
                missing++;
        }
        SetDlgItemTextW(m_Pages[PageDevices], IDC_DEVICES_NOTE,
                        FormatText(IDS_DEVICES_NOTE, { std::to_wstring(m_Devices.size()), std::to_wstring(missing),
                                                       std::to_wstring(SearchList().size()),
                                                       NativeArchitecture() }).c_str());
    }

    void ResolveNames()
    {
        std::vector<DeviceInfo> unknown;
        HWND hwnd = m_hwnd;
        CancelFlag cancel = m_NameCancel;

        for (const DeviceInfo &device : m_Devices)
        {
            if (NeedsDriver(device) && !device.NameResolved && !CatalogQueries(device).empty())
                unknown.push_back(device);
        }
        if (unknown.empty())
            return;
        StartJob([hwnd, unknown, cancel]() {
            Catalog catalog;

            catalog.SetCancel(cancel.get());
            for (const DeviceInfo &device : unknown)
            {
                std::wstring name;

                if (IsCancelled(cancel))
                    break;
                name = LocalDeviceName(device);
                if (name.empty())
                {
                    std::vector<CatalogUpdate> updates;
                    DWORD error = SearchDevice(catalog, device, updates);

                    if (!updates.empty())
                        name = updates.front().Model;
                    if (IsOfflineError(error))
                        break;
                }
                if (!name.empty())
                {
                    StoreDeviceName(device, name);
                    Post(hwnd, WM_APP_NAME, 0, new NameMessage{ device.InstanceId, name });
                }
            }
        });
    }

    void ShowDetails()
    {
        const DeviceInfo *device = SelectedDevice();
        std::wstring text;

        if (!device)
            return;
        text = DeviceDetails(*device, m_History);
        DialogBoxParamW(m_Instance, MAKEINTRESOURCEW(IDD_DETAILS), m_hwnd, DetailsProc, (LPARAM)text.c_str());
    }

    void SetProgress(HWND page, const std::wstring &text, const std::wstring &detail, int percent)
    {
        HWND bar = GetDlgItem(page, IDC_PROGRESS_BAR);
        LONG_PTR style = GetWindowLongPtrW(bar, GWL_STYLE);

        SetDlgItemTextW(page, IDC_PROGRESS_TEXT, text.c_str());
        SetDlgItemTextW(page, IDC_PROGRESS_DETAIL, detail.c_str());
        if (percent < 0)
        {
            if (!(style & PBS_MARQUEE))
            {
                SetWindowLongPtrW(bar, GWL_STYLE, style | PBS_MARQUEE);
                SendMessageW(bar, PBM_SETMARQUEE, TRUE, 30);
            }
        }
        else
        {
            if (style & PBS_MARQUEE)
            {
                SendMessageW(bar, PBM_SETMARQUEE, FALSE, 0);
                SetWindowLongPtrW(bar, GWL_STYLE, style & ~PBS_MARQUEE);
            }
            SendMessageW(bar, PBM_SETPOS, percent, 0);
        }
    }

    bool StartJob(std::function<void()> work)
    {
        std::function<void()> *job = new std::function<void()>(std::move(work));
        HANDLE thread = CreateThread(NULL, 0, JobMain, job, 0, NULL);

        if (!thread)
        {
            delete job;
            Message(TD_ERROR_ICON, FormatText(IDS_WORKER_FAILED, { ErrorText(GetLastError()) }));
            return false;
        }
        CloseHandle(thread);
        return true;
    }

    static DWORD WINAPI JobMain(LPVOID parameter)
    {
        std::unique_ptr<std::function<void()>> job((std::function<void()> *)parameter);
        (*job)();
        return 0;
    }

    UINT NewJob()
    {
        m_Cancel = std::make_shared<LONG>(0);
        return ++m_Job;
    }

    void StartSearch()
    {
        std::vector<DeviceInfo> devices = SearchList();
        HWND hwnd = m_hwnd;
        CancelFlag cancel;
        UINT job;

        if (devices.empty())
            return;
        job = NewJob();
        cancel = m_Cancel;
        m_Single = devices.size() == 1;
        m_Found.clear();
        m_SearchErrors.clear();
        SetProgress(m_Pages[PageSearch], FormatText(IDS_SEARCH_START, { std::to_wstring(devices.size()) }),
                    std::wstring(), -1);
        Show(PageSearch);
        if (!StartJob([hwnd, devices, job, cancel]() {
            Catalog catalog;

            catalog.SetCancel(cancel.get());
            for (size_t i = 0; i < devices.size() && !IsCancelled(cancel); i++)
            {
                ResultsMessage *results = new ResultsMessage;

                Post(hwnd, WM_APP_PROGRESS, job,
                     new ProgressMessage{ FormatText(IDS_SEARCH_STEP, { std::to_wstring(i + 1),
                                                                         std::to_wstring(devices.size()) }),
                                          devices[i].Name, (int)(i * 100 / devices.size()) });
                results->Error = SearchDevice(catalog, devices[i], results->Updates);
                results->InstanceId = devices[i].InstanceId;
                results->Note = devices[i].Name + L": " + CatalogNote(devices[i], results->Updates, results->Error);
                Post(hwnd, WM_APP_RESULTS, job, results);
                if (IsOfflineError(results->Error))
                    break;
            }
            PostMessageW(hwnd, WM_APP_SEARCHED, job, 0);
        }))
        {
            Show(PageDevices);
        }
    }

    void FillChoices()
    {
        HWND list = GetDlgItem(m_Pages[PageChoose], IDC_CHOICES);
        size_t searched = m_Found.size(), absent = 0, current = 0;
        std::wstring note;
        std::map<std::wstring, bool> checked;

        m_Choices.clear();
        for (const DeviceInfo &device : m_Devices)
        {
            auto found = m_Found.find(device.InstanceId);
            bool offered = false;

            if (found == m_Found.end())
                continue;
            if (found->second.empty())
            {
                absent++;
                continue;
            }
            for (size_t i = 0; i < found->second.size() && (m_Single || !offered); i++)
            {
                ChoiceState state = StateOf(device, found->second[i], m_History);

                if (!Selectable(state))
                    continue;
                m_Choices.push_back(Choice{ device, found->second[i], state });
                offered = true;
            }
            if (!offered)
                current++;
        }
        m_Filling = true;
        SendMessageW(list, WM_SETREDRAW, FALSE, 0);
        ListView_DeleteAllItems(list);
        for (const Choice &choice : m_Choices)
        {
            int row = AddRow(list, choice.Device.Name);

            SetCell(list, row, 1, choice.Update.Title);
            SetCell(list, row, 2, choice.Update.Version);
            SetCell(list, row, 3, LoadText(choice.State == ChoiceState::Missing ? IDS_STATE_MISSING : IDS_STATE_NEWER));
            ListView_SetCheckState(list, row, !checked[choice.Device.InstanceId]);
            checked[choice.Device.InstanceId] = true;
        }
        SendMessageW(list, WM_SETREDRAW, TRUE, 0);
        m_Filling = false;
        if (m_Choices.empty())
            note = FormatText(IDS_CHOOSE_NONE, { std::to_wstring(searched) });
        else
            note = FormatText(IDS_CHOOSE_FOUND, { std::to_wstring(checked.size()), std::to_wstring(searched) });
        if (current)
            note += L" " + FormatText(IDS_CHOOSE_CURRENT, { std::to_wstring(current) });
        if (absent)
            note += L" " + FormatText(IDS_CHOOSE_ABSENT, { std::to_wstring(absent) });
        if (!m_SearchErrors.empty())
            note += L" " + m_SearchErrors.front();
        SetDlgItemTextW(m_Pages[PageChoose], IDC_CHOOSE_NOTE, note.c_str());
    }

    void StartInstall()
    {
        HWND list = GetDlgItem(m_Pages[PageChoose], IDC_CHOICES), hwnd = m_hwnd;
        std::vector<Choice> chosen;
        CancelFlag cancel;
        UINT job;

        for (int i = 0; i < ListView_GetItemCount(list) && i < (int)m_Choices.size(); i++)
        {
            if (ListView_GetCheckState(list, i) == 1)
                chosen.push_back(m_Choices[i]);
        }
        if (chosen.empty() || !EnsureAdministrator())
            return;
        job = NewJob();
        cancel = m_Cancel;
        SetProgress(m_Pages[PageInstall], FormatText(IDS_INSTALL_START, { std::to_wstring(chosen.size()) }),
                    std::wstring(), -1);
        Show(PageInstall);
        if (!StartJob([hwnd, chosen, job, cancel]() {
            InstalledMessage *installed = new InstalledMessage;

            installed->Cancelled = false;
            for (size_t i = 0; i < chosen.size(); i++)
            {
                const Choice &choice = chosen[i];
                Outcome outcome = { choice.Device.Name, choice.Update.Title, std::wstring(), ERROR_SUCCESS, FALSE,
                                    false };
                std::wstring step = FormatText(IDS_INSTALL_STEP, { std::to_wstring(i + 1),
                                                                   std::to_wstring(chosen.size()),
                                                                   choice.Device.Name });
                DeviceInfo current;

                if (IsCancelled(cancel))
                {
                    outcome.Error = ERROR_CANCELLED;
                    installed->Cancelled = true;
                }
                else if (choice.State == ChoiceState::Missing && FindDevice(choice.Device.InstanceId, current) &&
                         !NeedsDriver(current))
                {
                    outcome.Skipped = true;
                }
                else
                {
                    outcome.Error = RunInstall(hwnd, choice.Device, choice.Update, outcome.Reboot, outcome.Detail,
                                               [hwnd, job, step](const std::wstring &text, int percent) {
                        Post(hwnd, WM_APP_PROGRESS, job, new ProgressMessage{ step, text, percent });
                    }, cancel);
                    if (outcome.Error == ERROR_CANCELLED)
                        installed->Cancelled = true;
                }
                installed->Outcomes.push_back(outcome);
                Post(hwnd, WM_APP_PROGRESS, job,
                     new ProgressMessage{ step, std::wstring(), (int)((i + 1) * 100 / chosen.size()) });
            }
            Post(hwnd, WM_APP_INSTALLED, job, installed);
        }))
        {
            Show(PageChoose);
        }
    }

    void FillDone(bool cancelled)
    {
        HWND list = GetDlgItem(m_Pages[PageDone], IDC_RESULTS);
        size_t failed = 0;
        bool reboot = false;
        std::wstring note;

        ListView_DeleteAllItems(list);
        for (const Outcome &outcome : m_Outcomes)
        {
            int row = AddRow(list, outcome.Device);

            SetCell(list, row, 1, outcome.Driver);
            SetCell(list, row, 2, OutcomeText(outcome));
            if (outcome.Error && outcome.Error != ERROR_CANCELLED)
                failed++;
            if (outcome.Reboot)
                reboot = true;
        }
        if (cancelled)
            note = LoadText(IDS_DONE_CANCELLED);
        else if (failed)
            note = FormatText(IDS_DONE_FAILED, { std::to_wstring(failed) });
        else
            note = LoadText(IDS_DONE_OK);
        if (reboot)
            note += L" " + LoadText(IDS_DONE_REBOOT);
        SetDlgItemTextW(m_Pages[PageDone], IDC_DONE_NOTE, note.c_str());
    }

    void FillHistory()
    {
        HWND list = GetDlgItem(m_Pages[PageHistory], IDC_HISTORY_LIST);

        m_History = LoadHistory();
        SendMessageW(list, WM_SETREDRAW, FALSE, 0);
        ListView_DeleteAllItems(list);
        for (const HistoryEntry &entry : m_History)
        {
            int row = AddRow(list, entry.Installed);

            SetCell(list, row, 1, LoadText(ActionText(entry.Action)));
            SetCell(list, row, 2, entry.Device);
            SetCell(list, row, 3, entry.Title.empty() ? entry.Inf : entry.Title + L" " + entry.Version);
            SetCell(list, row, 4, HistoryResult(entry));
        }
        SendMessageW(list, WM_SETREDRAW, TRUE, 0);
        UpdateButtons();
    }

    void StartAction(bool rollback)
    {
        const HistoryEntry *selected = SelectedEntry();
        HistoryEntry entry;
        HWND hwnd = m_hwnd;
        UINT job;

        if (!selected || m_Busy)
            return;
        entry = *selected;
        if (!Ask(LoadText(rollback ? IDS_ACTION_ROLLBACK : IDS_ACTION_REMOVE),
                 rollback ? FormatText(IDS_ROLLBACK_CONFIRM, { entry.Device, entry.PreviousVersion.empty()
                                                                                 ? LoadText(IDS_DETAIL_NONE)
                                                                                 : entry.PreviousVersion })
                          : FormatText(IDS_REMOVE_CONFIRM, { entry.Device })) ||
            !EnsureAdministrator())
        {
            return;
        }
        job = NewJob();
        m_Busy = true;
        UpdateButtons();
        SetDlgItemTextW(m_Pages[PageHistory], IDC_HISTORY_NOTE, LoadText(IDS_HISTORY_WORKING).c_str());
        if (!StartJob([hwnd, entry, rollback, job]() {
            ActionMessage *action = new ActionMessage;
            HistoryEntry record = entry;

            action->Reboot = FALSE;
            action->Error = rollback ? RollBackDriver(hwnd, entry, action->Reboot)
                                     : RemoveCatalogDriver(entry, action->Reboot);
            record.Action = rollback ? L"rollback" : L"remove";
            record.Result = action->Error;
            record.Reboot = action->Reboot;
            SaveHistory(record);
            Post(hwnd, WM_APP_ACTION, job, action);
        }))
        {
            m_Busy = false;
            UpdateButtons();
        }
    }

    HINSTANCE m_Instance = NULL;
    HWND m_hwnd = NULL;
    HWND m_Pages[PageCount] = { 0 };
    int m_Current = -1;
    HFONT m_TitleFont = NULL;
    HBITMAP m_Watermark = NULL;
    SIZE m_WatermarkSize = { 0, 0 };
    int m_Corner = 8;
    UINT m_Job = 0;
    CancelFlag m_Cancel = std::make_shared<LONG>(0);
    CancelFlag m_NameCancel = std::make_shared<LONG>(0);
    bool m_Busy = false;
    bool m_Single = false;
    bool m_Filling = false;
    std::vector<DeviceInfo> m_Devices;
    std::vector<HistoryEntry> m_History;
    std::map<std::wstring, std::vector<CatalogUpdate>> m_Found;
    std::map<std::wstring, bool> m_Selected;
    std::map<std::wstring, std::wstring> m_Names;
    std::vector<std::wstring> m_SearchErrors;
    std::vector<Choice> m_Choices;
    std::vector<Outcome> m_Outcomes;
};

HANDLE g_Output = NULL;

void Print(const std::wstring &line)
{
    std::wstring text = line + L"\r\n";
    DWORD written;

    OutputDebugStringW((L"[LNTDRV] " + line + L"\n").c_str());
    if (!g_Output || g_Output == INVALID_HANDLE_VALUE)
        return;
    if (!WriteConsoleW(g_Output, text.c_str(), (DWORD)text.size(), &written, NULL))
    {
        int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), NULL, 0, NULL, NULL);
        std::string bytes(size, '\0');
        WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), &bytes[0], size, NULL, NULL);
        WriteFile(g_Output, bytes.data(), (DWORD)bytes.size(), &written, NULL);
    }
}

void PrintUpdate(const CatalogUpdate &update)
{
    Print(L"  " + update.Title + L" | " + update.Version + L" | " + update.LastUpdated + L" | " + update.Architecture +
          L" | " + update.Size + L" | " + update.Id + L" | " + update.Query);
}

void PrintProgress(const std::wstring &text, int percent)
{
    if (percent < 0 || percent % 25 == 0)
        Print(L"  " + text + (percent >= 0 ? L" " + std::to_wstring(percent) + L"%" : std::wstring()));
}

std::wstring ErrorWithDetail(DWORD error, const std::wstring &detail)
{
    return detail.empty() ? ErrorText(error) : FormatText(IDS_RESULT_FILE, { ErrorText(error), detail });
}

int RunCommand(int argc, LPWSTR *argv)
{
    std::wstring command = argv[1] + 1;
    std::vector<DeviceInfo> devices;
    std::vector<HistoryEntry> history = LoadHistory();
    const DeviceInfo *device = NULL;
    bool install = _wcsicmp(command.c_str(), L"install") == 0;
    bool installAll = _wcsicmp(command.c_str(), L"installall") == 0;
    DWORD error;

    g_Output = GetStdHandle(STD_OUTPUT_HANDLE);
    if ((!g_Output || g_Output == INVALID_HANDLE_VALUE) && AttachConsole(ATTACH_PARENT_PROCESS))
        g_Output = CreateFileW(L"CONOUT$", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0,
                               NULL);
    if ((install || installAll) && !IsUserAnAdmin())
    {
        Print(LoadText(IDS_ADMIN_REQUIRED));
        return ERROR_ACCESS_DENIED;
    }
    if (install || installAll)
        CleanupDownloads(DownloadsKeptDays);
    error = EnumerateDevices(devices);
    if (error)
    {
        Print(FormatText(IDS_CLI_LIST_ERROR, { ErrorText(error) }));
        return (int)error;
    }
    if (argc > 2)
    {
        for (const DeviceInfo &current : devices)
        {
            if (_wcsicmp(current.InstanceId.c_str(), argv[2]) == 0)
                device = &current;
        }
    }

    if (_wcsicmp(command.c_str(), L"list") == 0)
    {
        for (const DeviceInfo &current : devices)
        {
            Print(current.Name + L" | " + DeviceStatus(current) + L" | " + current.Class + L" | " +
                  current.DriverVersion + L" | " + current.Provider + L" | " + current.Inf + L" | " +
                  DriverSource(current, history) + L" | " + current.InstanceId);
        }
        Print(FormatText(IDS_CLI_LIST_TOTAL, { std::to_wstring(devices.size()), NativeArchitecture() }));
        return 0;
    }
    if (_wcsicmp(command.c_str(), L"scan") == 0)
    {
        Catalog catalog;
        size_t missing = 0;

        for (const DeviceInfo &current : devices)
        {
            std::vector<CatalogUpdate> updates;

            if (!NeedsDriver(current))
                continue;
            missing++;
            error = SearchDevice(catalog, current, updates);
            Print(current.Name + L" (" + current.InstanceId + L"): " + CatalogNote(current, updates, error));
            if (!updates.empty())
                PrintUpdate(updates.front());
            if (IsOfflineError(error))
                break;
        }
        Print(FormatText(IDS_CLI_MISSING_TOTAL, { std::to_wstring(missing) }));
        return IsOfflineError(error) ? (int)error : 0;
    }
    if (installAll)
    {
        Catalog catalog;
        std::vector<std::wstring> pending;
        size_t installed = 0, failed = 0, absent = 0;
        bool restart = false;

        for (const DeviceInfo &current : devices)
        {
            if (NeedsDriver(current) && !CatalogQueries(current).empty())
                pending.push_back(current.InstanceId);
        }
        Print(FormatText(IDS_CLI_MISSING_TOTAL, { std::to_wstring(pending.size()) }));
        for (const std::wstring &instanceId : pending)
        {
            std::vector<CatalogUpdate> updates;
            DeviceInfo target;
            std::wstring detail;
            BOOL reboot = FALSE;

            if (!FindDevice(instanceId, target) || !NeedsDriver(target))
            {
                Print(FormatText(IDS_CLI_ALREADY, { instanceId }));
                continue;
            }
            error = SearchDevice(catalog, target, updates);
            if (updates.empty())
            {
                Print(target.Name + L" (" + instanceId + L"): " + CatalogNote(target, updates, error));
                if (error && error != ERROR_NOT_FOUND)
                    failed++;
                else
                    absent++;
                if (IsOfflineError(error))
                    break;
                continue;
            }
            Print(FormatText(IDS_CLI_INSTALLING, { target.Name, instanceId }));
            for (size_t i = 0; i < updates.size() && i < MaximumCandidates; i++)
            {
                PrintUpdate(updates[i]);
                error = RunInstall(NULL, target, updates[i], reboot, detail, PrintProgress, CancelFlag());
                if (!error)
                {
                    installed++;
                    restart = restart || reboot;
                    Print(FormatText(reboot ? IDS_CLI_INSTALLED_REBOOT : IDS_CLI_INSTALLED,
                                     { target.Name, updates[i].Title }));
                    break;
                }
                Print(FormatText(IDS_CLI_FAILED, { target.Name, ErrorWithDetail(error, detail) }));
                if (IsOfflineError(error))
                    break;
            }
            if (error)
                failed++;
            if (IsOfflineError(error))
                break;
        }
        Print(FormatText(IDS_CLI_SUMMARY, { std::to_wstring(installed), std::to_wstring(absent),
                                            std::to_wstring(failed) }) +
              (restart ? L" " + LoadText(IDS_CLI_RESTART) : std::wstring()));
        return (int)failed;
    }
    if (_wcsicmp(command.c_str(), L"search") == 0 || install)
    {
        Catalog catalog;
        std::vector<CatalogUpdate> updates;
        const CatalogUpdate *update = NULL;
        std::wstring detail;
        BOOL reboot = FALSE;

        if (!device)
        {
            Print(FormatText(IDS_CLI_NO_DEVICE, { argc > 2 ? std::wstring(argv[2]) : std::wstring() }));
            return ERROR_NOT_FOUND;
        }
        error = SearchDevice(catalog, *device, updates);
        Print(device->Name + L": " + CatalogNote(*device, updates, error));
        for (const CatalogUpdate &current : updates)
            PrintUpdate(current);
        if (!install || updates.empty())
            return error;
        update = &updates.front();
        for (const CatalogUpdate &current : updates)
        {
            if (argc > 3 && _wcsicmp(current.Id.c_str(), argv[3]) == 0)
                update = &current;
        }
        error = RunInstall(NULL, *device, *update, reboot, detail, PrintProgress, CancelFlag());
        Print(error ? FormatText(IDS_CLI_FAILED, { device->Name, ErrorWithDetail(error, detail) })
                    : FormatText(reboot ? IDS_CLI_INSTALLED_REBOOT : IDS_CLI_INSTALLED,
                                 { device->Name, update->Title }));
        return (int)error;
    }
    Print(LoadText(IDS_CLI_USAGE));
    return ERROR_INVALID_PARAMETER;
}

}

}

int WINAPI
wWinMain(HINSTANCE instance, HINSTANCE previous, LPWSTR commandLine, int show)
{
    int argc = 0, result;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);

    UNREFERENCED_PARAMETER(previous);
    UNREFERENCED_PARAMETER(commandLine);
    UNREFERENCED_PARAMETER(show);
    if (argv && argc > 1 && (argv[1][0] == L'/' || argv[1][0] == L'-'))
        result = lntdrv::RunCommand(argc, argv);
    else
        result = (int)lntdrv::DriversWizard().Run(instance);
    if (argv)
        LocalFree(argv);
    return result;
}
