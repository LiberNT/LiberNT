/*
 * PROJECT:     LiberNT Driver Installer
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
#include <wctype.h>
#include <map>
#include <set>

namespace lntdrv
{

namespace
{

enum : UINT
{
    WM_APP_PROGRESS = WM_APP + 1,
    WM_APP_FOUND,
    WM_APP_INSTALLED,
    WM_APP_ACTION,
    WM_APP_NAME,
    WM_APP_SCAN_DONE,
};

enum Page
{
    PageDevices,
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
    { IDD_PROGRESS, IDS_INSTALL_TITLE, IDS_INSTALL_SUBTITLE },
    { IDD_DONE, IDS_DONE_TITLE, IDS_DONE_SUBTITLE },
    { IDD_HISTORY, IDS_HISTORY_TITLE, IDS_HISTORY_SUBTITLE },
};

const DWORD DownloadsKeptDays = 14;
const UINT_PTR SpinnerTimer = 1;
const UINT SpinnerInterval = 90;
const int SpinnerDots[8][2] = { { 1000, 0 }, { 707, 707 }, { 0, 1000 }, { -707, 707 },
                                { -1000, 0 }, { -707, -707 }, { 0, -1000 }, { 707, -707 } };
const size_t MaximumCandidates = 3;

struct ProgressMessage
{
    std::wstring Text;
    std::wstring Detail;
    int Percent;
};

struct FoundMessage
{
    std::wstring InstanceId;
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
    std::wstring Note;
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

class Reporter
{
public:
    virtual void operator()(const std::wstring &text, int percent) const = 0;
};

class CancelFlag
{
public:
    CancelFlag() : m_Value(0), m_References(1) {}
    CancelFlag(const CancelFlag &) = delete;
    CancelFlag &operator=(const CancelFlag &) = delete;

    void AddRef()
    {
        InterlockedIncrement(&m_References);
    }

    void Release()
    {
        if (!InterlockedDecrement(&m_References))
            delete this;
    }

    volatile LONG *Value()
    {
        return &m_Value;
    }

private:
    volatile LONG m_Value;
    volatile LONG m_References;
};

template <class T>
void Post(HWND hwnd, UINT message, UINT job, T *payload)
{
    if (!PostMessageW(hwnd, message, job, (LPARAM)payload))
        delete payload;
}

bool IsCancelled(CancelFlag *cancel)
{
    return cancel && InterlockedCompareExchange(cancel->Value(), 0, 0) != 0;
}

std::wstring CatalogNote(const DeviceInfo &device, const std::vector<CatalogUpdate> &updates, DWORD error,
                         const std::wstring &others)
{
    if (error == ERROR_NOT_SUPPORTED)
        return LoadText(IDS_NOTE_NOT_SEARCHABLE);
    if (error == ERROR_LNTDRV_OTHER_ARCH)
        return FormatText(IDS_NOTE_OTHER_ARCH, NativeArchitecture(), others);
    if (error == ERROR_LNTDRV_OTHER_WINDOWS)
        return LoadText(IDS_NOTE_OTHER_WINDOWS);
    if (error == ERROR_NOT_FOUND || (!error && updates.empty()))
        return LoadText(IDS_NOTE_NOT_FOUND);
    if (error)
        return FormatText(IDS_NOTE_FAILED, ErrorText(error));
    switch (CompareVersionText(device, updates.front().Version))
    {
        case DriverOrder::Missing:
            return FormatText(IDS_NOTE_AVAILABLE, updates.front().Version);
        case DriverOrder::Newer:
            return FormatText(IDS_NOTE_NEWER, updates.front().Version);
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

std::wstring OfferedVersion(const CatalogUpdate &update)
{
    return !update.Version.empty() && iswdigit(update.Version[0]) ? update.Version : update.LastUpdated;
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
                 std::wstring &detail, const Reporter &report, CancelFlag *cancel)
{
    class DownloadProgress : public ProgressCallback
    {
    public:
        DownloadProgress(const Reporter &reporter, const std::wstring &file)
            : m_Report(reporter), m_Name(file), m_Last(-2)
        {
        }

        void operator()(ULONGLONG received, ULONGLONG total)
        {
            int step = total ? (int)(received * 100 / total) : (int)(received >> 18);
            if (step != m_Last)
            {
                m_Last = step;
                m_Report(FormatText(IDS_PROGRESS_DOWNLOAD, m_Name, NumberText(received / 1024)), total ? step : -1);
            }
        }

    private:
        const Reporter &m_Report;
        const std::wstring &m_Name;
        int m_Last;
    };
    Catalog catalog;
    std::vector<CatalogFile> files;
    std::wstring directory = PackageDirectory(update.Id), package = directory + L"\\package";
    HistoryEntry entry;
    PackageDriver driver;
    DWORD error;

    reboot = FALSE;
    detail.clear();
    catalog.SetCancel(cancel ? cancel->Value() : NULL);
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
        DownloadProgress progress(report, name);

        if (name.empty())
        {
            error = ERROR_INVALID_DATA;
            break;
        }
        error = catalog.Download(files[i], path, &progress);
        if (!error)
        {
            report(FormatText(IDS_PROGRESS_UNPACK, name), -1);
            error = ExtractPackage(path, package);
        }
    }
    if (!error && IsCancelled(cancel))
        error = ERROR_CANCELLED;
    if (!error)
    {
        report(LoadText(IDS_PROGRESS_VERIFY), -1);
        error = VerifyPackage(package, detail);
        if (error && error != ERROR_LNTDRV_NOT_COVERED)
        {
            OutputDebugStringW((L"[LNTDRV] " + device.Name + L": " + ErrorText(error) + L"\n").c_str());
            detail = LoadText(IDS_RESULT_UNSIGNED);
            error = ERROR_SUCCESS;
        }
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
        report(FormatText(IDS_PROGRESS_INSTALL, device.Name), -1);
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

    if (!outcome.Note.empty())
        return outcome.Note;
    if (outcome.Skipped)
        return LoadText(IDS_RESULT_SKIPPED);
    if (!outcome.Error)
    {
        text = LoadText(outcome.Reboot ? IDS_RESULT_REBOOT : IDS_RESULT_INSTALLED);
        return outcome.Detail.empty() ? text : FormatText(IDS_RESULT_FILE, text, outcome.Detail);
    }
    text = ErrorText(outcome.Error);
    return outcome.Detail.empty() ? text : FormatText(IDS_RESULT_FILE, text, outcome.Detail);
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

template <size_t Count>
void InitList(HWND list, DWORD extraStyle, const std::pair<UINT, int> (&columns)[Count])
{
    RECT client;
    int width;

    ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP | extraStyle);
    GetClientRect(list, &client);
    width = client.right - GetSystemMetrics(SM_CXVSCROLL);
    for (size_t i = 0; i < Count; i++)
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

class Job
{
public:
    virtual ~Job() {}
    virtual void Run() = 0;
};

class ScanJob : public Job
{
public:
    ScanJob(HWND hwnd, const std::vector<DeviceInfo> &devices, UINT scan, CancelFlag *cancel)
        : m_hwnd(hwnd), m_Devices(devices), m_Scan(scan), m_Cancel(cancel)
    {
        m_Cancel->AddRef();
    }

    ~ScanJob()
    {
        m_Cancel->Release();
    }

    void Run()
    {
        Catalog catalog;
        DWORD failure = ERROR_SUCCESS;

        catalog.SetCancel(m_Cancel->Value());
        for (const DeviceInfo &device : m_Devices)
        {
            FoundMessage *found;
            std::wstring name;
            bool unnamed = NeedsDriver(device) && !device.NameResolved;
            DWORD error;

            if (IsCancelled(m_Cancel))
                break;
            found = new FoundMessage{ device.InstanceId, std::vector<CatalogUpdate>() };
            if (unnamed)
                name = LocalDeviceName(device);
            error = SearchDevice(catalog, device, found->Updates);
            if (unnamed && name.empty())
                name = found->Updates.empty() ? catalog.OtherModel() : found->Updates.front().Model;
            if (!name.empty())
            {
                StoreDeviceName(device, name);
                Post(m_hwnd, WM_APP_NAME, m_Scan, new NameMessage{ device.InstanceId, name });
            }
            OutputDebugStringW((L"[LNTDRV] " + (name.empty() ? device.Name : name) + L" (" + device.InstanceId +
                                L"): " + CatalogNote(device, found->Updates, error, catalog.OtherArchitectures()) +
                                L"\n").c_str());
            Post(m_hwnd, WM_APP_FOUND, m_Scan, found);
            if (IsOfflineError(error) || error == ERROR_CANCELLED)
            {
                failure = error == ERROR_CANCELLED ? ERROR_SUCCESS : error;
                break;
            }
        }
        PostMessageW(m_hwnd, WM_APP_SCAN_DONE, m_Scan, failure);
    }

private:
    HWND m_hwnd;
    std::vector<DeviceInfo> m_Devices;
    UINT m_Scan;
    CancelFlag *m_Cancel;
};

class PostProgress : public Reporter
{
public:
    PostProgress(HWND hwnd, UINT job, const std::wstring &step) : m_hwnd(hwnd), m_Job(job), m_Step(step) {}

    void operator()(const std::wstring &text, int percent) const
    {
        Post(m_hwnd, WM_APP_PROGRESS, m_Job, new ProgressMessage{ m_Step, text, percent });
    }

private:
    HWND m_hwnd;
    UINT m_Job;
    std::wstring m_Step;
};

class InstallJob : public Job
{
public:
    InstallJob(HWND hwnd, const std::vector<Choice> &chosen, UINT job, CancelFlag *cancel)
        : m_hwnd(hwnd), m_Chosen(chosen), m_Job(job), m_Cancel(cancel)
    {
        m_Cancel->AddRef();
    }

    ~InstallJob()
    {
        m_Cancel->Release();
    }

    void Run()
    {
        InstalledMessage *installed = new InstalledMessage;

        installed->Cancelled = false;
        for (size_t i = 0; i < m_Chosen.size(); i++)
        {
            const Choice &choice = m_Chosen[i];
            Outcome outcome = { choice.Device.Name, choice.Update.Title, std::wstring(), ERROR_SUCCESS, FALSE,
                                false, std::wstring() };
            std::wstring step = FormatText(IDS_INSTALL_STEP, NumberText(i + 1), NumberText(m_Chosen.size()),
                                           choice.Device.Name);
            DeviceInfo current;

            if (IsCancelled(m_Cancel))
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
                outcome.Error = RunInstall(m_hwnd, choice.Device, choice.Update, outcome.Reboot, outcome.Detail,
                                           PostProgress(m_hwnd, m_Job, step), m_Cancel);
                if (outcome.Error == ERROR_CANCELLED)
                    installed->Cancelled = true;
            }
            OutputDebugStringW((L"[LNTDRV] " + choice.Device.Name + L": " + choice.Update.Title + L": " +
                                OutcomeText(outcome) + L"\n").c_str());
            installed->Outcomes.push_back(outcome);
            Post(m_hwnd, WM_APP_PROGRESS, m_Job,
                 new ProgressMessage{ step, std::wstring(), (int)((i + 1) * 100 / m_Chosen.size()) });
        }
        Post(m_hwnd, WM_APP_INSTALLED, m_Job, installed);
    }

private:
    HWND m_hwnd;
    std::vector<Choice> m_Chosen;
    UINT m_Job;
    CancelFlag *m_Cancel;
};

class ActionJob : public Job
{
public:
    ActionJob(HWND hwnd, const HistoryEntry &entry, bool rollback, UINT job)
        : m_hwnd(hwnd), m_Entry(entry), m_Rollback(rollback), m_Job(job)
    {
    }

    void Run()
    {
        ActionMessage *action = new ActionMessage;
        HistoryEntry record = m_Entry;

        action->Reboot = FALSE;
        action->Error = m_Rollback ? RollBackDriver(m_hwnd, m_Entry, action->Reboot)
                                   : RemoveCatalogDriver(m_Entry, action->Reboot);
        record.Action = m_Rollback ? L"rollback" : L"remove";
        record.Result = action->Error;
        record.Reboot = action->Reboot;
        SaveHistory(record);
        Post(m_hwnd, WM_APP_ACTION, m_Job, action);
    }

private:
    HWND m_hwnd;
    HistoryEntry m_Entry;
    bool m_Rollback;
    UINT m_Job;
};

class DriversWizard
{
public:
    DriversWizard() = default;
    DriversWizard(const DriversWizard &) = delete;
    DriversWizard &operator=(const DriversWizard &) = delete;

    ~DriversWizard()
    {
        m_Cancel->Release();
        m_ScanCancel->Release();
    }

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
                ProgressMessage *progress = (ProgressMessage *)lParam;
                if ((UINT)wParam == m_Job && m_Current == PageInstall)
                {
                    SetProgress(m_Pages[m_Current], progress->Text,
                                IsCancelled(m_Cancel) ? LoadText(IDS_INSTALL_CANCELLING) : progress->Detail);
                }
                delete progress;
                return TRUE;
            }
            case WM_APP_FOUND:
            {
                FoundMessage *found = (FoundMessage *)lParam;
                if ((UINT)wParam == m_Scan)
                    OnFound(*found);
                delete found;
                return TRUE;
            }
            case WM_APP_INSTALLED:
            {
                InstalledMessage *installed = (InstalledMessage *)lParam;
                if ((UINT)wParam == m_Job)
                {
                    m_Outcomes = installed->Outcomes;
                    FillDone(installed->Cancelled);
                    m_Selected.clear();
                    LoadDevices();
                    Show(PageDone);
                }
                delete installed;
                return TRUE;
            }
            case WM_APP_ACTION:
            {
                ActionMessage *action = (ActionMessage *)lParam;
                if ((UINT)wParam == m_Job)
                {
                    m_Busy = false;
                    LoadDevices();
                    StartScan();
                    FillHistory();
                    UpdateButtons();
                    SetDlgItemTextW(m_Pages[PageHistory], IDC_HISTORY_NOTE, L"");
                    if (action->Error)
                        Message(TD_ERROR_ICON, FormatText(IDS_ACTION_FAILED, ErrorText(action->Error)));
                    else
                        Message(TD_INFORMATION_ICON,
                                LoadText(action->Reboot ? IDS_ACTION_DONE_REBOOT : IDS_ACTION_DONE));
                }
                delete action;
                return TRUE;
            }
            case WM_APP_NAME:
            {
                NameMessage *name = (NameMessage *)lParam;
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
                delete name;
                return TRUE;
            }
            case WM_APP_SCAN_DONE:
                if ((UINT)wParam == m_Scan)
                {
                    m_Scanning = false;
                    m_ScanError = (DWORD)lParam;
                    m_Resolving.clear();
                    KillTimer(m_hwnd, SpinnerTimer);
                    InvalidateRect(GetDlgItem(m_Pages[PageDevices], IDC_DEVICES), NULL, FALSE);
                    UpdateScanStatus();
                    UpdateButtons();
                }
                return TRUE;
            case WM_TIMER:
                if (wParam == SpinnerTimer)
                {
                    HWND list = GetDlgItem(m_Pages[PageDevices], IDC_DEVICES);

                    m_Frame++;
                    for (size_t i = 0; i < m_Devices.size(); i++)
                    {
                        if (m_Resolving.count(m_Devices[i].InstanceId))
                            ListView_RedrawItems(list, (int)i, (int)i);
                    }
                    if (m_Resolving.empty())
                        KillTimer(m_hwnd, SpinnerTimer);
                    return TRUE;
                }
                break;
            case WM_DESTROY:
                KillTimer(m_hwnd, SpinnerTimer);
                InterlockedExchange(m_ScanCancel->Value(), 1);
                if (m_Cancel)
                    InterlockedExchange(m_Cancel->Value(), 1);
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
                case IDC_SELECT_ALL:
                case IDC_SELECT_NONE:
                    SelectAll(page, LOWORD(wParam) == IDC_SELECT_ALL);
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
        }
        return FALSE;
    }

    INT_PTR OnDeviceNotify(HWND page, NMHDR *header)
    {
        NMLISTVIEW *change = (NMLISTVIEW *)header;
        bool image = (header->code == LVN_ITEMCHANGING || header->code == LVN_ITEMCHANGED) &&
                     change->iItem >= 0 && change->iItem < (int)m_Devices.size() && (change->uChanged & LVIF_STATE) &&
                     ((change->uNewState ^ change->uOldState) & LVIS_STATEIMAGEMASK);

        if (header->code == NM_CUSTOMDRAW)
        {
            NMLVCUSTOMDRAW *draw = (NMLVCUSTOMDRAW *)header;
            LRESULT result = CDRF_DODEFAULT;
            size_t row = (size_t)draw->nmcd.dwItemSpec;

            if (draw->nmcd.dwDrawStage == CDDS_PREPAINT)
                result = CDRF_NOTIFYITEMDRAW;
            else if (draw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT && row < m_Devices.size() &&
                     m_Resolving.count(m_Devices[row].InstanceId))
                result = CDRF_NOTIFYPOSTPAINT;
            else if (draw->nmcd.dwDrawStage == CDDS_ITEMPOSTPAINT)
                PaintSpinner(header->hwndFrom, draw->nmcd.hdc, (int)row);
            SetWindowLongPtrW(page, DWLP_MSGRESULT, result);
            return TRUE;
        }
        if (header->code == NM_DBLCLK)
            ShowDetails();
        else if (header->code == LVN_ITEMCHANGING && image && !m_Filling &&
                 !m_Found.count(m_Devices[change->iItem].InstanceId))
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

    void PaintSpinner(HWND list, HDC hdc, int row)
    {
        RECT cell;
        COLORREF fore = ListView_GetTextColor(list), back = ListView_GetTextBkColor(list);
        HGDIOBJ pen;
        int size, radius, dot, x, y;

        if (!ListView_GetItemRect(list, row, &cell, LVIR_LABEL))
            return;
        size = cell.bottom - cell.top - 4;
        if (size < 8)
            return;
        pen = SelectObject(hdc, GetStockObject(NULL_PEN));
        if (fore == CLR_DEFAULT)
            fore = GetSysColor(COLOR_WINDOWTEXT);
        if (back == CLR_DEFAULT || back == CLR_NONE)
            back = GetSysColor(COLOR_WINDOW);
        radius = size / 2 - 1;
        dot = std::max(1, size / 8);
        x = cell.right - size / 2 - 4;
        y = (cell.top + cell.bottom) / 2;
        for (int i = 0; i < 8; i++)
        {
            int age = (int)((m_Frame + 8 - i) % 8), weight = 255 - age * 28;
            COLORREF color = RGB((GetRValue(fore) * weight + GetRValue(back) * (255 - weight)) / 255,
                                 (GetGValue(fore) * weight + GetGValue(back) * (255 - weight)) / 255,
                                 (GetBValue(fore) * weight + GetBValue(back) * (255 - weight)) / 255);
            HBRUSH brush = CreateSolidBrush(color);
            HGDIOBJ old = SelectObject(hdc, brush);
            int cx = x + SpinnerDots[i][0] * radius / 1000, cy = y + SpinnerDots[i][1] * radius / 1000;

            Ellipse(hdc, cx - dot, cy - dot, cx + dot + 1, cy + dot + 1);
            SelectObject(hdc, old);
            DeleteObject(brush);
        }
        SelectObject(hdc, pen);
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
        const std::pair<UINT, int> deviceColumns[] = { { IDS_COL_DEVICE, 46 }, { IDS_COL_DRIVER, 36 },
                                                       { IDS_COL_VERSION, 18 } };
        const std::pair<UINT, int> resultColumns[] = { { IDS_COL_DEVICE, 34 }, { IDS_COL_DRIVER, 34 },
                                                       { IDS_COL_RESULT, 32 } };
        const std::pair<UINT, int> historyColumns[] = { { IDS_COL_DATE, 22 }, { IDS_COL_ACTION, 14 },
                                                        { IDS_COL_DEVICE, 24 }, { IDS_COL_DRIVER, 24 },
                                                        { IDS_COL_RESULT, 16 } };
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
        InitList(GetDlgItem(m_Pages[PageDevices], IDC_DEVICES), LVS_EX_CHECKBOXES, deviceColumns);
        InitList(GetDlgItem(m_Pages[PageDone], IDC_RESULTS), 0, resultColumns);
        InitList(GetDlgItem(m_Pages[PageHistory], IDC_HISTORY_LIST), 0, historyColumns);

        Center();
        if (IsUserAnAdmin())
            CleanupDownloads(DownloadsKeptDays);
        LoadDevices();
        Show(PageDevices);
        SetFocus(GetDlgItem(m_Pages[PageDevices], IDC_DEVICES));
        StartScan();
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
                        LoadText(page == PageDevices ? IDS_INSTALL : page == PageDone ? IDS_FINISH : IDS_NEXT).c_str());
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
        const DeviceInfo *device = SelectedDevice();
        const HistoryEntry *entry = SelectedEntry();
        bool back = m_Current == PageHistory;
        bool next = m_Current != PageHistory && m_Current != PageInstall;
        bool cancel = m_Current != PageDone;
        bool cancelEnabled = m_Current != PageInstall || !IsCancelled(m_Cancel);
        bool nextEnabled = m_Current == PageDone;
        bool usable = entry && !m_Busy && entry->Action == L"install" && !entry->Result && EntryInUse(*entry);

        if (m_Current == PageDevices)
            nextEnabled = !InstallList().empty();
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
        {
            if (EnsureAdministrator())
                PrepareInstall();
        }
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
        if (m_Current == PageInstall)
        {
            InterlockedExchange(m_Cancel->Value(), 1);
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

        if (!m_Found.count(device.InstanceId))
            return false;
        return selected == m_Selected.end() || selected->second;
    }

    std::vector<Choice> InstallList() const
    {
        std::vector<Choice> choices;

        for (const DeviceInfo &device : m_Devices)
        {
            auto found = m_Found.find(device.InstanceId);

            if (found != m_Found.end() && Ticked(device))
            {
                Choice choice = found->second;

                choice.Device = device;
                choices.push_back(choice);
            }
        }
        return choices;
    }

    void LoadDevices()
    {
        HWND list = GetDlgItem(m_Pages[PageDevices], IDC_DEVICES);

        m_History = LoadHistory();
        m_Found.clear();
        EnumerateDevices(m_Devices);
        m_Devices.erase(std::remove_if(m_Devices.begin(), m_Devices.end(),
                                       [](const DeviceInfo &device) { return !Updatable(device); }),
                        m_Devices.end());
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

            ListView_SetItemState(list, row, INDEXTOSTATEIMAGEMASK(0), LVIS_STATEIMAGEMASK);
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
                        FormatText(IDS_DEVICES_NOTE, NumberText(m_Devices.size()), NumberText(missing),
                                   NumberText(InstallList().size()), NativeArchitecture()).c_str());
    }

    void UpdateScanStatus()
    {
        std::wstring text;

        if (m_Scanning)
            text = FormatText(IDS_SCAN_PROGRESS, NumberText(std::min(m_Scanned + 1, m_ScanTotal)),
                              NumberText(m_ScanTotal));
        else if (m_ScanError)
            text = ErrorText(m_ScanError);
        else if (!m_Found.empty())
            text = FormatText(IDS_SCAN_FOUND, NumberText(m_Found.size()));
        else
            text = LoadText(IDS_SCAN_NONE);
        SetDlgItemTextW(m_Pages[PageDevices], IDC_DEVICES_STATUS, text.c_str());
    }

    void StartScan()
    {
        std::vector<DeviceInfo> devices = m_Devices;
        HWND hwnd = m_hwnd;
        UINT scan = ++m_Scan;
        CancelFlag *cancel;

        InterlockedExchange(m_ScanCancel->Value(), 1);
        m_ScanCancel->Release();
        m_ScanCancel = new CancelFlag;
        cancel = m_ScanCancel;
        m_Found.clear();
        m_Resolving.clear();
        m_Scanned = 0;
        m_ScanTotal = devices.size();
        m_ScanError = ERROR_SUCCESS;
        m_Scanning = !devices.empty();
        for (const DeviceInfo &device : devices)
            m_Resolving.insert(device.InstanceId);
        UpdateScanStatus();
        UpdateButtons();
        if (devices.empty())
            return;
        SetTimer(m_hwnd, SpinnerTimer, SpinnerInterval, NULL);
        if (!StartJob(new ScanJob(hwnd, devices, scan, cancel)))
        {
            m_Scanning = false;
            m_Resolving.clear();
            KillTimer(m_hwnd, SpinnerTimer);
            UpdateScanStatus();
        }
    }

    void OnFound(const FoundMessage &found)
    {
        HWND list = GetDlgItem(m_Pages[PageDevices], IDC_DEVICES);

        m_Resolving.erase(found.InstanceId);
        m_Scanned++;
        for (size_t i = 0; i < m_Devices.size(); i++)
        {
            const DeviceInfo &device = m_Devices[i];

            if (device.InstanceId != found.InstanceId)
                continue;
            for (const CatalogUpdate &update : found.Updates)
            {
                ChoiceState state = StateOf(device, update, m_History);

                if (Selectable(state))
                {
                    m_Found[device.InstanceId] = Choice{ device, update, state };
                    break;
                }
            }
            if (m_Found.count(device.InstanceId))
            {
                m_Filling = true;
                SetCell(list, (int)i, 1, m_Found[device.InstanceId].Update.Title);
                SetCell(list, (int)i, 2, OfferedVersion(m_Found[device.InstanceId].Update));
                ListView_SetCheckState(list, (int)i, Ticked(device));
                m_Filling = false;
                ListView_RedrawItems(list, (int)i, (int)i);
            }
            else
            {
                ListView_DeleteItem(list, (int)i);
                m_Devices.erase(m_Devices.begin() + i);
            }
            break;
        }
        UpdateScanStatus();
        UpdateDevicesNote();
        UpdateButtons();
    }

    void SelectAll(HWND page, bool check)
    {
        HWND list = GetDlgItem(page, IDC_DEVICES);

        m_Filling = true;
        for (size_t i = 0; i < m_Devices.size(); i++)
        {
            if (!m_Found.count(m_Devices[i].InstanceId))
                continue;
            m_Selected[m_Devices[i].InstanceId] = check;
            ListView_SetCheckState(list, (int)i, check);
        }
        m_Filling = false;
        UpdateDevicesNote();
        UpdateButtons();
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

    void SetProgress(HWND page, const std::wstring &text, const std::wstring &detail)
    {
        SetDlgItemTextW(page, IDC_PROGRESS_TEXT, text.c_str());
        SetDlgItemTextW(page, IDC_PROGRESS_DETAIL, detail.c_str());
    }

    bool StartJob(Job *job)
    {
        HANDLE thread = CreateThread(NULL, 0, JobMain, job, 0, NULL);

        if (!thread)
        {
            delete job;
            Message(TD_ERROR_ICON, FormatText(IDS_WORKER_FAILED, ErrorText(GetLastError())));
            return false;
        }
        CloseHandle(thread);
        return true;
    }

    static DWORD WINAPI JobMain(LPVOID parameter)
    {
        Job *job = (Job *)parameter;

        job->Run();
        delete job;
        return 0;
    }

    UINT NewJob()
    {
        m_Cancel->Release();
        m_Cancel = new CancelFlag;
        return ++m_Job;
    }

    void PrepareInstall()
    {
        std::vector<Choice> chosen = InstallList();

        if (chosen.empty())
            return;
        InterlockedExchange(m_ScanCancel->Value(), 1);
        m_Scan++;
        m_Scanning = false;
        m_Resolving.clear();
        KillTimer(m_hwnd, SpinnerTimer);
        StartInstall(chosen);
    }

    void StartInstall(const std::vector<Choice> &chosen)
    {
        HWND hwnd = m_hwnd;
        CancelFlag *cancel;
        UINT job;

        job = NewJob();
        cancel = m_Cancel;
        SetProgress(m_Pages[PageInstall], FormatText(IDS_INSTALL_START, NumberText(chosen.size())), std::wstring());
        Show(PageInstall);
        if (!StartJob(new InstallJob(hwnd, chosen, job, cancel)))
        {
            Show(PageDevices);
        }
    }

    void FillDone(bool cancelled)
    {
        HWND list = GetDlgItem(m_Pages[PageDone], IDC_RESULTS);
        size_t failed = 0, installed = 0;
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
            else if (!outcome.Error && !outcome.Skipped && outcome.Note.empty())
                installed++;
            if (outcome.Reboot)
                reboot = true;
        }
        if (cancelled)
            note = LoadText(IDS_DONE_CANCELLED);
        else if (failed)
            note = FormatText(IDS_DONE_FAILED, NumberText(failed));
        else if (!installed)
            note = LoadText(IDS_DONE_NOTHING);
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
                 rollback ? FormatText(IDS_ROLLBACK_CONFIRM, entry.Device,
                                       entry.PreviousVersion.empty() ? LoadText(IDS_DETAIL_NONE) : entry.PreviousVersion)
                          : FormatText(IDS_REMOVE_CONFIRM, entry.Device)) ||
            !EnsureAdministrator())
        {
            return;
        }
        job = NewJob();
        m_Busy = true;
        UpdateButtons();
        SetDlgItemTextW(m_Pages[PageHistory], IDC_HISTORY_NOTE, LoadText(IDS_HISTORY_WORKING).c_str());
        if (!StartJob(new ActionJob(hwnd, entry, rollback, job)))
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
    CancelFlag *m_Cancel = new CancelFlag;
    CancelFlag *m_ScanCancel = new CancelFlag;
    UINT m_Scan = 0;
    bool m_Scanning = false;
    size_t m_Scanned = 0;
    size_t m_ScanTotal = 0;
    DWORD m_ScanError = ERROR_SUCCESS;
    bool m_Busy = false;
    UINT m_Frame = 0;
    bool m_Filling = false;
    std::vector<DeviceInfo> m_Devices;
    std::vector<HistoryEntry> m_History;
    std::map<std::wstring, Choice> m_Found;
    std::map<std::wstring, bool> m_Selected;
    std::map<std::wstring, std::wstring> m_Names;
    std::set<std::wstring> m_Resolving;
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

class PrintProgress : public Reporter
{
public:
    void operator()(const std::wstring &text, int percent) const
    {
        if (percent < 0 || percent % 25 == 0)
            Print(L"  " + text + (percent >= 0 ? L" " + NumberText(percent) + L"%" : std::wstring()));
    }
};

std::wstring GroupKey(const DeviceInfo &device)
{
    return device.HardwareIds.empty() ? device.InstanceId : device.HardwareIds.front();
}

std::vector<std::vector<DeviceInfo>> MissingGroups(const std::vector<DeviceInfo> &devices, bool searchable)
{
    std::vector<std::vector<DeviceInfo>> groups;

    for (const DeviceInfo &device : devices)
    {
        std::wstring key = GroupKey(device);
        auto group = std::find_if(groups.begin(), groups.end(), [&key](const std::vector<DeviceInfo> &other) {
            return _wcsicmp(GroupKey(other.front()).c_str(), key.c_str()) == 0;
        });

        if (!NeedsDriver(device) || (searchable && CatalogQueries(device).empty()))
            continue;
        if (group == groups.end())
            groups.push_back(std::vector<DeviceInfo>(1, device));
        else
            group->push_back(device);
    }
    return groups;
}

std::wstring GroupName(const std::vector<DeviceInfo> &group)
{
    const DeviceInfo &first = group.front();

    if (group.size() == 1)
        return first.Name + L" (" + first.InstanceId + L")";
    return FormatText(IDS_CLI_GROUP, first.Name, GroupKey(first), NumberText(group.size()));
}

std::wstring ErrorWithDetail(DWORD error, const std::wstring &detail)
{
    return detail.empty() ? ErrorText(error) : FormatText(IDS_RESULT_FILE, ErrorText(error), detail);
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
        Print(FormatText(IDS_CLI_LIST_ERROR, ErrorText(error)));
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
        devices.erase(std::remove_if(devices.begin(), devices.end(),
                                     [](const DeviceInfo &current) { return !Updatable(current); }),
                      devices.end());
        for (const DeviceInfo &current : devices)
        {
            Print(current.Name + L" | " + DeviceStatus(current) + L" | " + current.Class + L" | " +
                  current.DriverVersion + L" | " + current.Provider + L" | " + current.Inf + L" | " +
                  DriverSource(current, history) + L" | " + current.InstanceId);
        }
        Print(FormatText(IDS_CLI_LIST_TOTAL, NumberText(devices.size()), NativeArchitecture()));
        return 0;
    }
    if (_wcsicmp(command.c_str(), L"scan") == 0)
    {
        Catalog catalog;
        std::vector<std::vector<DeviceInfo>> groups = MissingGroups(devices, false);
        size_t missing = 0;

        for (const std::vector<DeviceInfo> &group : groups)
        {
            std::vector<CatalogUpdate> updates;

            missing += group.size();
            error = SearchDevice(catalog, group.front(), updates);
            Print(GroupName(group) + L": " + CatalogNote(group.front(), updates, error, catalog.OtherArchitectures()));
            if (!updates.empty())
                PrintUpdate(updates.front());
            if (IsOfflineError(error))
                break;
        }
        Print(FormatText(IDS_CLI_MISSING_TOTAL, NumberText(missing)));
        return IsOfflineError(error) ? (int)error : 0;
    }
    if (installAll)
    {
        Catalog catalog;
        std::vector<std::vector<DeviceInfo>> groups = MissingGroups(devices, true);
        size_t installed = 0, failed = 0, absent = 0, missing = 0;
        bool restart = false;

        for (const std::vector<DeviceInfo> &group : groups)
            missing += group.size();
        Print(FormatText(IDS_CLI_MISSING_TOTAL, NumberText(missing)));
        for (const std::vector<DeviceInfo> &group : groups)
        {
            std::vector<CatalogUpdate> updates;
            DeviceInfo target;
            std::wstring detail;
            BOOL reboot = FALSE;

            if (!FindDevice(group.front().InstanceId, target) || !NeedsDriver(target))
            {
                Print(FormatText(IDS_CLI_ALREADY, GroupName(group)));
                continue;
            }
            error = SearchDevice(catalog, target, updates);
            if (updates.empty())
            {
                Print(GroupName(group) + L": " + CatalogNote(target, updates, error, catalog.OtherArchitectures()));
                if (error && error != ERROR_NOT_FOUND && error != ERROR_LNTDRV_OTHER_ARCH &&
                    error != ERROR_LNTDRV_OTHER_WINDOWS)
                {
                    failed++;
                }
                else
                {
                    absent++;
                }
                if (IsOfflineError(error))
                    break;
                continue;
            }
            Print(FormatText(IDS_CLI_INSTALLING, GroupName(group), target.InstanceId));
            for (size_t i = 0; i < updates.size() && i < MaximumCandidates; i++)
            {
                PrintUpdate(updates[i]);
                error = RunInstall(NULL, target, updates[i], reboot, detail, PrintProgress(), NULL);
                if (!error)
                {
                    installed++;
                    restart = restart || reboot;
                    Print(FormatText(reboot ? IDS_CLI_INSTALLED_REBOOT : IDS_CLI_INSTALLED, target.Name,
                                     updates[i].Title));
                    break;
                }
                Print(FormatText(IDS_CLI_FAILED, target.Name, ErrorWithDetail(error, detail)));
                if (IsOfflineError(error))
                    break;
            }
            if (error)
                failed++;
            if (IsOfflineError(error))
                break;
        }
        Print(FormatText(IDS_CLI_SUMMARY, NumberText(installed), NumberText(absent), NumberText(failed)) +
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
            Print(FormatText(IDS_CLI_NO_DEVICE, argc > 2 ? std::wstring(argv[2]) : std::wstring()));
            return ERROR_NOT_FOUND;
        }
        error = SearchDevice(catalog, *device, updates);
        Print(device->Name + L": " + CatalogNote(*device, updates, error, catalog.OtherArchitectures()));
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
        error = RunInstall(NULL, *device, *update, reboot, detail, PrintProgress(), NULL);
        Print(error ? FormatText(IDS_CLI_FAILED, device->Name, ErrorWithDetail(error, detail))
                    : FormatText(reboot ? IDS_CLI_INSTALLED_REBOOT : IDS_CLI_INSTALLED, device->Name, update->Title));
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
