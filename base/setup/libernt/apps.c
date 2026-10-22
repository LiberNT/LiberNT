/*
 * PROJECT:     LiberNT Setup
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     First-boot components: table, download, verification and installation
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "libernt.h"

#define COBJMACROS
#include <initguid.h>
#include <objbase.h>
#include <bits.h>
#include <netlistmgr.h>
#include <wintrust.h>
#include <softpub.h>
#include <wincrypt.h>
#include <bcrypt.h>
#include <msi.h>

#include "oobe.h"
#include "resource.h"

#define NDEBUG
#include <debug.h>

#define APP_NO_PROGRESS_TIMEOUT 120000
#define APP_POLL_INTERVAL       250
#define APP_RETRY_DELAY         60

POOBE_APP OobeApps;
ULONG OobeAppCount;

static PCWSTR
GetArchitectureName(VOID)
{
    SYSTEM_INFO Info;

    GetNativeSystemInfo(&Info);
    switch (Info.wProcessorArchitecture)
    {
        case PROCESSOR_ARCHITECTURE_AMD64:
            return L"amd64";
        case PROCESSOR_ARCHITECTURE_ARM64:
            return L"arm64";
        case PROCESSOR_ARCHITECTURE_INTEL:
            return L"x86";
        case PROCESSOR_ARCHITECTURE_RISCV64:
            return L"riscv64";
        default:
            return L"unknown";
    }
}

static BOOL
GetInfString(
    _In_ HINF hInf,
    _In_ PCWSTR Section,
    _In_ PCWSTR Key,
    _Out_writes_(cchValue) PWSTR Value,
    _In_ DWORD cchValue)
{
    INFCONTEXT Context;

    Value[0] = UNICODE_NULL;
    return SetupFindFirstLineW(hInf, Section, Key, &Context) &&
           SetupGetStringFieldW(&Context, 1, Value, cchValue, NULL);
}

static BOOL
IsOobeAppPresent(
    _In_ HINF hInf,
    _In_ PCWSTR Section)
{
    WCHAR Path[MAX_PATH];

    return GetInfString(hInf, Section, L"Detect", Path, ARRAYSIZE(Path)) &&
           GetFileAttributesW(Path) != INVALID_FILE_ATTRIBUTES;
}

ULONG
LoadOobeApps(
    _Out_ POOBE_APP* Apps)
{
    WCHAR Path[MAX_PATH];
    WCHAR Section[64];
    WCHAR AppSection[128];
    WCHAR Type[16];
    WCHAR Space[16];
    INFCONTEXT Context;
    POOBE_APP Items = NULL, NewItems;
    ULONG Count = 0;
    PWSTR Name;
    HINF hInf;
    DWORD Length;

    *Apps = NULL;

    Length = GetModuleFileNameW(NULL, Path, ARRAYSIZE(Path));
    if (Length == 0 || Length >= ARRAYSIZE(Path) || !(Name = wcsrchr(Path, L'\\')))
        return 0;
    *Name = UNICODE_NULL;
    if (FAILED(StringCchCatW(Path, ARRAYSIZE(Path), L"\\oobeapps.inf")))
        return 0;

    hInf = SetupOpenInfFileW(Path, NULL, INF_STYLE_WIN4, NULL);
    if (hInf == INVALID_HANDLE_VALUE)
        return 0;

    StringCchPrintfW(Section, ARRAYSIZE(Section), L"Apps.%s", GetArchitectureName());
    if (SetupFindFirstLineW(hInf, Section, NULL, &Context))
    {
        do
        {
            POOBE_APP App;

            if (!SetupGetStringFieldW(&Context, 0, AppSection, ARRAYSIZE(AppSection), NULL))
                continue;

            NewItems = Items ? HeapReAlloc(ProcessHeap, HEAP_ZERO_MEMORY, Items, (Count + 1) * sizeof(*Items))
                             : HeapAlloc(ProcessHeap, HEAP_ZERO_MEMORY, sizeof(*Items));
            if (!NewItems)
                break;
            Items = NewItems;
            App = &Items[Count];

            if (!GetInfString(hInf, AppSection, L"Name", App->Name, ARRAYSIZE(App->Name)) ||
                !GetInfString(hInf, AppSection, L"Url", App->Url, ARRAYSIZE(App->Url)) ||
                !GetInfString(hInf, AppSection, L"File", App->File, ARRAYSIZE(App->File)) ||
                !GetInfString(hInf, AppSection, L"Type", Type, ARRAYSIZE(Type)))
            {
                DPRINT1("Component %S in %S is incomplete\n", AppSection, Path);
                continue;
            }
            GetInfString(hInf, AppSection, L"Arguments", App->Arguments, ARRAYSIZE(App->Arguments));
            GetInfString(hInf, AppSection, L"Signer", App->Signer, ARRAYSIZE(App->Signer));
            GetInfString(hInf, AppSection, L"Sha256", App->Sha256, ARRAYSIZE(App->Sha256));
            if (GetInfString(hInf, AppSection, L"Space", Space, ARRAYSIZE(Space)))
                App->SpaceMb = wcstoul(Space, NULL, 10);
            if (!App->Signer[0] && !App->Sha256[0])
            {
                DPRINT1("Component %S has neither a signer nor a hash\n", AppSection);
                continue;
            }
            if (IsOobeAppPresent(hInf, AppSection))
            {
                DPRINT("Component %S is already present\n", App->Name);
                continue;
            }

            App->Msi = (_wcsicmp(Type, L"msi") == 0);
            App->Selected = TRUE;
            App->Result = ERROR_SUCCESS;
            Count++;
        } while (SetupFindNextLine(&Context, &Context));
    }

    SetupCloseInfFile(hInf);
    *Apps = Items;
    return Count;
}

BOOL
GetSystemDriveSpace(
    _Out_opt_ PULONGLONG TotalMb,
    _Out_ PULONGLONG FreeMb)
{
    ULARGE_INTEGER FreeBytes, TotalBytes;
    WCHAR Root[MAX_PATH];

    *FreeMb = 0;
    if (TotalMb)
        *TotalMb = 0;
    if (GetWindowsDirectoryW(Root, ARRAYSIZE(Root)) < 3)
        return FALSE;
    Root[3] = UNICODE_NULL;
    if (!GetDiskFreeSpaceExW(Root, &FreeBytes, &TotalBytes, NULL))
        return FALSE;

    *FreeMb = FreeBytes.QuadPart / (1024 * 1024);
    if (TotalMb)
        *TotalMb = TotalBytes.QuadPart / (1024 * 1024);
    return TRUE;
}

static PCWSTR
GetUrlHost(
    _In_ PCWSTR Url,
    _Out_writes_(cchHost) PWSTR Host,
    _In_ SIZE_T cchHost)
{
    PCWSTR Start = wcsstr(Url, L"://");
    PCWSTR End;

    Start = Start ? Start + 3 : Url;
    End = wcschr(Start, L'/');
    StringCchCopyNW(Host, cchHost, Start, End ? (SIZE_T)(End - Start) : wcslen(Start));
    return Host;
}

static VOID
FillAppList(
    _In_ HWND hList)
{
    LVCOLUMNW Column;
    LVITEMW Item;
    WCHAR Text[128];
    ULONG Index;
    RECT rc;

    ListView_SetExtendedListViewStyle(hList, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT);
    GetClientRect(hList, &rc);

    ZeroMemory(&Column, sizeof(Column));
    Column.mask = LVCF_TEXT | LVCF_WIDTH;
    LoadStringW(GetModuleHandleW(NULL), IDS_APPS_COL_NAME, Text, ARRAYSIZE(Text));
    Column.pszText = Text;
    Column.cx = (rc.right - rc.left) * 3 / 5;
    ListView_InsertColumn(hList, 0, &Column);
    LoadStringW(GetModuleHandleW(NULL), IDS_APPS_COL_SOURCE, Text, ARRAYSIZE(Text));
    Column.pszText = Text;
    Column.cx = (rc.right - rc.left) - Column.cx - GetSystemMetrics(SM_CXVSCROLL);
    ListView_InsertColumn(hList, 1, &Column);

    for (Index = 0; Index < OobeAppCount; Index++)
    {
        ZeroMemory(&Item, sizeof(Item));
        Item.mask = LVIF_TEXT | LVIF_PARAM;
        Item.iItem = (INT)Index;
        Item.pszText = OobeApps[Index].Name;
        Item.lParam = Index;
        Item.iItem = ListView_InsertItem(hList, &Item);
        ListView_SetItemText(hList, Item.iItem, 1,
                             (PWSTR)GetUrlHost(OobeApps[Index].Url, Text, ARRAYSIZE(Text)));
        ListView_SetCheckState(hList, Item.iItem, OobeApps[Index].Selected);
    }
}

static ULONG
GetSelectedSpace(
    _In_ HWND hList)
{
    INT iItem, Items = ListView_GetItemCount(hList);
    ULONG SpaceMb = 0;
    LVITEMW Item;

    for (iItem = 0; iItem < Items; iItem++)
    {
        ZeroMemory(&Item, sizeof(Item));
        Item.mask = LVIF_PARAM;
        Item.iItem = iItem;
        if (ListView_GetItem(hList, &Item) && (ULONG)Item.lParam < OobeAppCount &&
            ListView_GetCheckState(hList, iItem))
        {
            SpaceMb += OobeApps[Item.lParam].SpaceMb;
        }
    }
    return SpaceMb;
}

static VOID
ShowAppSpace(
    _In_ HWND hwndDlg)
{
    WCHAR Text[256];
    ULONGLONG FreeMb;

    Text[0] = UNICODE_NULL;
    if (GetSystemDriveSpace(NULL, &FreeMb))
    {
        LoadPrintfW(Text, ARRAYSIZE(Text), IDS_OOBE_APPS_SPACE,
                    GetSelectedSpace(GetDlgItem(hwndDlg, IDC_APPLIST)), FreeMb);
    }
    SetDlgItemTextW(hwndDlg, IDC_APPSPACE, Text);
}

INT_PTR
CALLBACK
AppsPageProc(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam)
{
    HWND hList = GetDlgItem(hwndDlg, IDC_APPLIST);

    switch (uMsg)
    {
        case WM_INITDIALOG:
            FillAppList(hList);
            ShowAppSpace(hwndDlg);
            return FALSE;

        case WM_NOTIFY:
            if (((LPNMHDR)lParam)->idFrom == IDC_APPLIST && ((LPNMHDR)lParam)->code == LVN_ITEMCHANGED)
                ShowAppSpace(hwndDlg);
            break;

        case SPN_SETACTIVE:
            if (OobeAppCount == 0)
            {
                SetupFramePressNext(hwndDlg);
                return TRUE;
            }
            ShowAppSpace(hwndDlg);
            SetFocus(hList);
            return TRUE;

        case SPN_WIZNEXT:
        {
            INT iItem, Items = ListView_GetItemCount(hList);
            ULONG SpaceMb = GetSelectedSpace(hList);
            ULONGLONG FreeMb;
            WCHAR Text[512];
            LVITEMW Item;

            if (GetSystemDriveSpace(NULL, &FreeMb) && SpaceMb > FreeMb)
            {
                LoadPrintfW(Text, ARRAYSIZE(Text), IDS_OOBE_APPS_NOSPACE_TEXT, SpaceMb, FreeMb);
                SetupMessage(GetParent(hwndDlg), TD_WARNING_ICON, TDCBF_OK_BUTTON, IDOK,
                             MAKEINTRESOURCEW(IDS_OOBE_APPS_NOSPACE), Text);
                SetWindowLongPtrW(hwndDlg, DWLP_MSGRESULT, -1);
                return TRUE;
            }

            for (iItem = 0; iItem < Items; iItem++)
            {
                ZeroMemory(&Item, sizeof(Item));
                Item.mask = LVIF_PARAM;
                Item.iItem = iItem;
                if (ListView_GetItem(hList, &Item) && (ULONG)Item.lParam < OobeAppCount)
                    OobeApps[Item.lParam].Selected = ListView_GetCheckState(hList, iItem);
            }
            SetWindowLongPtrW(hwndDlg, DWLP_MSGRESULT, 0);
            return TRUE;
        }
    }

    return FALSE;
}

static VOID
SetProgressText(
    _In_ HWND hWnd,
    _In_ UINT uID,
    _In_ PCWSTR Name)
{
    WCHAR Text[256];

    LoadPrintfW(Text, ARRAYSIZE(Text), uID, Name);
    SetWindowTextW(hWnd, Text);
}

static BOOL
IsInternetConnected(VOID)
{
    INetworkListManager* Manager;
    NLM_CONNECTIVITY Connectivity;
    HRESULT hr;

    hr = CoCreateInstance(&CLSID_NetworkListManager, NULL, CLSCTX_INPROC_SERVER,
                          &IID_INetworkListManager, (PVOID*)&Manager);
    if (FAILED(hr))
    {
        DPRINT1("Network List Manager is not available, hr 0x%08lx\n", hr);
        return TRUE;
    }

    hr = INetworkListManager_GetConnectivity(Manager, &Connectivity);
    INetworkListManager_Release(Manager);
    if (FAILED(hr))
        return TRUE;

    return (Connectivity & (NLM_CONNECTIVITY_IPV4_INTERNET | NLM_CONNECTIVITY_IPV6_INTERNET)) != 0;
}

static HRESULT
GetJobError(
    _In_ IBackgroundCopyJob* Job,
    _In_ HRESULT hrDefault)
{
    IBackgroundCopyError* Error;
    BG_ERROR_CONTEXT ErrorContext;
    HRESULT hrJob = hrDefault;

    if (SUCCEEDED(IBackgroundCopyJob_GetError(Job, &Error)))
    {
        IBackgroundCopyError_GetError(Error, &ErrorContext, &hrJob);
        IBackgroundCopyError_Release(Error);
    }
    return hrJob;
}

static HRESULT
DownloadFile(
    _In_ POOBE_APP App,
    _In_ PCWSTR LocalPath,
    _In_ POOBE_PROGRESS Progress)
{
    IBackgroundCopyManager* Manager = NULL;
    IBackgroundCopyJob* Job = NULL;
    BG_JOB_PROGRESS JobProgress;
    BG_JOB_STATE State;
    BG_JOB_STATE LastState = BG_JOB_STATE_QUEUED;
    UINT64 LastBytes = 0;
    DWORD LastChange = GetTickCount();
    HRESULT hr, hrJob = E_FAIL;
    GUID JobId;

    hr = CoCreateInstance(&CLSID_BackgroundCopyManager, NULL, CLSCTX_LOCAL_SERVER,
                          &IID_IBackgroundCopyManager, (PVOID*)&Manager);
    if (FAILED(hr))
    {
        DPRINT1("BITS is not available, hr 0x%08lx\n", hr);
        return hr;
    }

    hr = IBackgroundCopyManager_CreateJob(Manager, App->Name, BG_JOB_TYPE_DOWNLOAD, &JobId, &Job);
    if (SUCCEEDED(hr))
        hr = IBackgroundCopyJob_AddFile(Job, App->Url, LocalPath);
    if (SUCCEEDED(hr))
        hr = IBackgroundCopyJob_SetMinimumRetryDelay(Job, APP_RETRY_DELAY);
    if (SUCCEEDED(hr))
        hr = IBackgroundCopyJob_SetNoProgressTimeout(Job, APP_NO_PROGRESS_TIMEOUT / 1000);
    if (SUCCEEDED(hr))
        hr = IBackgroundCopyJob_Resume(Job);
    if (FAILED(hr))
        goto Quit;

    SendMessageW(Progress->hwndProgress, PBM_SETRANGE32, 0, 100);
    SendMessageW(Progress->hwndProgress, PBM_SETPOS, 0, 0);

    for (;;)
    {
        hr = IBackgroundCopyJob_GetState(Job, &State);
        if (FAILED(hr))
            break;

        if (SUCCEEDED(IBackgroundCopyJob_GetProgress(Job, &JobProgress)))
        {
            if (JobProgress.BytesTotal != BG_SIZE_UNKNOWN && JobProgress.BytesTotal != 0)
            {
                SendMessageW(Progress->hwndProgress, PBM_SETPOS,
                             (WPARAM)(JobProgress.BytesTransferred * 100 / JobProgress.BytesTotal), 0);
            }
            if (JobProgress.BytesTransferred != LastBytes)
            {
                LastBytes = JobProgress.BytesTransferred;
                LastChange = GetTickCount();
            }
        }

        if (State != LastState)
        {
            LastState = State;
            LastChange = GetTickCount();
        }

        if (State == BG_JOB_STATE_TRANSFERRED)
        {
            hr = IBackgroundCopyJob_Complete(Job);
            hrJob = hr;
            break;
        }

        if (State == BG_JOB_STATE_ERROR)
        {
            hrJob = GetJobError(Job, E_FAIL);
            IBackgroundCopyJob_Cancel(Job);
            break;
        }

        if (State == BG_JOB_STATE_CANCELLED ||
            (State != BG_JOB_STATE_TRANSIENT_ERROR &&
             GetTickCount() - LastChange > APP_NO_PROGRESS_TIMEOUT))
        {
            hrJob = HRESULT_FROM_WIN32(ERROR_TIMEOUT);
            if (State != BG_JOB_STATE_CANCELLED)
                IBackgroundCopyJob_Cancel(Job);
            break;
        }

        Sleep(APP_POLL_INTERVAL);
    }
    hr = hrJob;

Quit:
    if (Job)
        IBackgroundCopyJob_Release(Job);
    IBackgroundCopyManager_Release(Manager);
    return hr;
}

static BOOL
FileHashMatches(
    _In_ PCWSTR Path,
    _In_ PCWSTR Expected)
{
    BCRYPT_ALG_HANDLE hAlgorithm = NULL;
    BCRYPT_HASH_HANDLE hHash = NULL;
    BYTE Hash[32];
    WCHAR Text[ARRAYSIZE(Hash) * 2 + 1];
    PBYTE Buffer;
    HANDLE hFile;
    DWORD Read, Index;
    BOOL Match = FALSE;

    hFile = CreateFileW(Path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
        return FALSE;

    Buffer = HeapAlloc(ProcessHeap, 0, 0x10000);
    if (!Buffer ||
        !BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&hAlgorithm, BCRYPT_SHA256_ALGORITHM, NULL, 0)) ||
        !BCRYPT_SUCCESS(BCryptCreateHash(hAlgorithm, &hHash, NULL, 0, NULL, 0, 0)))
    {
        goto Quit;
    }

    while (ReadFile(hFile, Buffer, 0x10000, &Read, NULL) && Read != 0)
    {
        if (!BCRYPT_SUCCESS(BCryptHashData(hHash, Buffer, Read, 0)))
            goto Quit;
    }

    if (!BCRYPT_SUCCESS(BCryptFinishHash(hHash, Hash, sizeof(Hash), 0)))
        goto Quit;

    for (Index = 0; Index < ARRAYSIZE(Hash); Index++)
        StringCchPrintfW(&Text[Index * 2], 3, L"%02x", Hash[Index]);
    Match = (_wcsicmp(Text, Expected) == 0);
    if (!Match)
        DPRINT1("%S has SHA-256 %S, expected %S\n", Path, Text, Expected);

Quit:
    if (hHash)
        BCryptDestroyHash(hHash);
    if (hAlgorithm)
        BCryptCloseAlgorithmProvider(hAlgorithm, 0);
    if (Buffer)
        HeapFree(ProcessHeap, 0, Buffer);
    CloseHandle(hFile);
    return Match;
}

static BOOL
FileSignedBy(
    _In_ PCWSTR Path,
    _In_ PCWSTR Signer)
{
    GUID Action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    WINTRUST_FILE_INFO FileInfo;
    WINTRUST_DATA Data;
    CRYPT_PROVIDER_DATA* Provider;
    CRYPT_PROVIDER_SGNR* ProviderSigner;
    CRYPT_PROVIDER_CERT* ProviderCert;
    WCHAR Organization[256];
    LONG Status;
    BOOL Match = FALSE;

    ZeroMemory(&FileInfo, sizeof(FileInfo));
    FileInfo.cbStruct = sizeof(FileInfo);
    FileInfo.pcwszFilePath = Path;

    ZeroMemory(&Data, sizeof(Data));
    Data.cbStruct = sizeof(Data);
    Data.dwUIChoice = WTD_UI_NONE;
    Data.fdwRevocationChecks = WTD_REVOKE_NONE;
    Data.dwUnionChoice = WTD_CHOICE_FILE;
    Data.pFile = &FileInfo;
    Data.dwStateAction = WTD_STATEACTION_VERIFY;

    Status = WinVerifyTrust(INVALID_HANDLE_VALUE, &Action, &Data);
    if (Status == ERROR_SUCCESS)
    {
        Provider = WTHelperProvDataFromStateData(Data.hWVTStateData);
        ProviderSigner = Provider ? WTHelperGetProvSignerFromChain(Provider, 0, FALSE, 0) : NULL;
        ProviderCert = ProviderSigner ? WTHelperGetProvCertFromChain(ProviderSigner, 0) : NULL;
        if (ProviderCert &&
            CertGetNameStringW(ProviderCert->pCert, CERT_NAME_ATTR_TYPE, 0, (PVOID)szOID_ORGANIZATION_NAME,
                               Organization, ARRAYSIZE(Organization)) > 1)
        {
            Match = (_wcsicmp(Organization, Signer) == 0);
            if (!Match)
                DPRINT1("%S is signed by %S, expected %S\n", Path, Organization, Signer);
        }
    }
    else
    {
        DPRINT1("WinVerifyTrust(%S) returned 0x%08lx\n", Path, Status);
    }

    Data.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust(INVALID_HANDLE_VALUE, &Action, &Data);
    return Match;
}

static BOOL
IsInstallSuccess(
    _In_ DWORD Code)
{
    return Code == ERROR_SUCCESS ||
           Code == ERROR_SUCCESS_REBOOT_REQUIRED ||
           Code == ERROR_SUCCESS_REBOOT_INITIATED ||
           Code == ERROR_PRODUCT_VERSION;
}

static DWORD
RunPackage(
    _In_ POOBE_APP App,
    _In_ PCWSTR Path)
{
    STARTUPINFOW StartupInfo;
    PROCESS_INFORMATION ProcessInfo;
    WCHAR CommandLine[MAX_PATH + 300];
    DWORD ExitCode;

    if (App->Msi)
    {
        MsiSetInternalUI(INSTALLUILEVEL_NONE, NULL);
        return MsiInstallProductW(Path, L"REBOOT=ReallySuppress");
    }

    if (FAILED(StringCchPrintfW(CommandLine, ARRAYSIZE(CommandLine), L"\"%s\" %s", Path, App->Arguments)))
        return ERROR_BUFFER_OVERFLOW;

    ZeroMemory(&StartupInfo, sizeof(StartupInfo));
    StartupInfo.cb = sizeof(StartupInfo);
    if (!CreateProcessW(Path, CommandLine, NULL, NULL, FALSE, 0, NULL, NULL, &StartupInfo, &ProcessInfo))
        return GetLastError();

    WaitForSingleObject(ProcessInfo.hProcess, INFINITE);
    if (!GetExitCodeProcess(ProcessInfo.hProcess, &ExitCode))
        ExitCode = GetLastError();
    CloseHandle(ProcessInfo.hThread);
    CloseHandle(ProcessInfo.hProcess);
    return ExitCode;
}

VOID
OobeResultText(
    _In_ DWORD Result,
    _Out_writes_(cchMessage) PWSTR Message,
    _In_ SIZE_T cchMessage,
    _Out_writes_(cchCode) PWSTR Code,
    _In_ SIZE_T cchCode)
{
    SIZE_T Length;

    if (!FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_MAX_WIDTH_MASK,
                        NULL, Result, 0, Message, (DWORD)cchMessage, NULL) &&
        !LoadStringW(GetModuleHandleW(NULL), IDS_OOBE_UNKNOWN_ERROR, Message, (INT)cchMessage))
        Message[0] = UNICODE_NULL;
    Length = wcslen(Message);
    while (Length && (Message[Length - 1] == L' ' || Message[Length - 1] == L'\r' || Message[Length - 1] == L'\n'))
        Message[--Length] = UNICODE_NULL;
    if (Result & 0x80000000)
        StringCchPrintfW(Code, cchCode, L"0x%08lx", Result);
    else
        StringCchPrintfW(Code, cchCode, L"%lu", Result);
}

DWORD
InstallOobeApp(
    _Inout_ POOBE_APP App,
    _In_ POOBE_PROGRESS Progress)
{
    WCHAR Folder[MAX_PATH];
    WCHAR Path[MAX_PATH];
    ULONGLONG FreeMb;
    HRESULT hr;
    BOOL Trusted;

    if (GetSystemDriveSpace(NULL, &FreeMb) && App->SpaceMb > FreeMb)
    {
        DPRINT1("Component %S needs %lu MB, drive has %I64u MB free\n", App->Name, App->SpaceMb, FreeMb);
        App->Result = ERROR_DISK_FULL;
        return App->Result;
    }

    if (!GetTempPathW(ARRAYSIZE(Folder), Folder) ||
        FAILED(StringCchCatW(Folder, ARRAYSIZE(Folder), L"LiberNT Components")) ||
        FAILED(StringCchPrintfW(Path, ARRAYSIZE(Path), L"%s\\%s", Folder, App->File)))
    {
        App->Result = ERROR_BUFFER_OVERFLOW;
        return App->Result;
    }
    CreateDirectoryW(Folder, NULL);
    DeleteFileW(Path);

    if (!IsInternetConnected())
    {
        DPRINT1("No Internet connection, %S is not downloaded\n", App->Name);
        App->Result = ERROR_NETWORK_UNREACHABLE;
        return App->Result;
    }

    SetProgressText(Progress->hwndActivity, IDS_OOBE_DOWNLOADING, App->Name);
    SetWindowTextW(Progress->hwndItem, App->Url);
    hr = DownloadFile(App, Path, Progress);
    if (FAILED(hr))
    {
        DPRINT1("Download of %S failed, hr 0x%08lx\n", App->Url, hr);
        App->Result = (DWORD)hr;
        return App->Result;
    }

    Trusted = App->Sha256[0] ? FileHashMatches(Path, App->Sha256) : FileSignedBy(Path, App->Signer);
    if (!Trusted)
    {
        DeleteFileW(Path);
        App->Result = (DWORD)TRUST_E_NOSIGNATURE;
        return App->Result;
    }

    SetProgressText(Progress->hwndActivity, IDS_OOBE_INSTALLING, App->Name);
    SetWindowTextW(Progress->hwndItem, L"");
    SetWindowLongPtrW(Progress->hwndProgress, GWL_STYLE,
                      GetWindowLongPtrW(Progress->hwndProgress, GWL_STYLE) | PBS_MARQUEE);
    SendMessageW(Progress->hwndProgress, PBM_SETMARQUEE, TRUE, 0);
    App->Result = RunPackage(App, Path);
    SendMessageW(Progress->hwndProgress, PBM_SETMARQUEE, FALSE, 0);
    SetWindowLongPtrW(Progress->hwndProgress, GWL_STYLE,
                      GetWindowLongPtrW(Progress->hwndProgress, GWL_STYLE) & ~PBS_MARQUEE);
    DeleteFileW(Path);

    if (IsInstallSuccess(App->Result))
    {
        DPRINT1("Component %S installed\n", App->Name);
        App->Result = ERROR_SUCCESS;
    }
    else
    {
        WCHAR Message[256], Code[16];
        OobeResultText(App->Result, Message, ARRAYSIZE(Message), Code, ARRAYSIZE(Code));
        DPRINT1("Component %S was not installed: %S (%S)\n", App->Name, Message, Code);
    }
    return App->Result;
}
