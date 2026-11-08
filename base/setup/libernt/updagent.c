/*
 * PROJECT:     LiberNT Setup
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Checks for LiberNT updates, downloads and stages them for Setup
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define COBJMACROS
#include "libernt.h"
#include <shellapi.h>
#include <shlwapi.h>
#include <objbase.h>
#include <ndk/setypes.h>
#include <ndk/exfuncs.h>
#include <bits.h>
#include <bcrypt.h>

#include "resource.h"
#include "update.h"

#define NDEBUG
#include <debug.h>

#define AGENT_MUTEX             L"Local\\LiberNTUpdateAgent"
#define AGENT_CLASS             L"LiberNTUpdateAgent"
#define AGENT_FIRST_DELAY       (10 * 60)
#define AGENT_INTERVAL          (12 * 60 * 60)
#define AGENT_TIMER             1
#define AGENT_ICON              1
#define WM_AGENT_ICON           (WM_APP + 0x200)
#define PM_CHECK_DONE           (WM_APP + 4)
#define PM_STAGE_PROGRESS       (WM_APP + 5)
#define PM_STAGE_DONE           (WM_APP + 6)
#define REMIND_DELAY            (24 * 60 * 60)
#define FILETIME_SECOND         10000000ULL
#define DEFAULT_SERVER          L"https://libernt.com/api/update/"
#define BITS_POLL               500
#define BITS_NO_PROGRESS        (120 * 1000)
#define STAGE_SPACE_MARGIN      (64ULL * 1024 * 1024)
#define SHA256_CHARS            65
#define STAGE_LOADER_PATH       L"\\" UPDATE_STAGE_DIRECTORY L"\\" UPDATE_STAGE_LOADER
#define STAGE_IMAGE_PATH        L"\\" UPDATE_STAGE_DIRECTORY L"\\" UPDATE_STAGE_IMAGE
#define STAGE_STATUS            L"install.log"
#define STAGE_STATUS_SECTION    L"Install"
#define STAGE_BACKUP            L"Rollback"
#define STAGE_MISSED_STARTS     2
#define BOOT_UPDATE_STATE       L"EFI\\LiberNT\\Boot\\update.state"
#define SETUP_KEY               L"SYSTEM\\Setup"
#define RUN_KEY                 L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run"
#define COMPLETE_VALUE          L"LiberNTUpdateComplete"
#define COMPLETE_COMMAND        L"lntupdate.exe /complete"
#define COMPLETE_SECTION        L"LiberNT.Upgrade"
#define TEXT_FILE_MAX           (8 * 1024 * 1024)
#define PM_COMPLETE_STEP        (WM_APP + 7)
#define PM_COMPLETE_DONE        (WM_APP + 8)
#define UPDATE_NOTES_MAX        (64 * 1024)
#define UPDATE_RESPONSE_MAX     (256 * 1024)
#define UPDATE_DESCRIPTION      L"LiberNT Setup"

typedef enum _UPDATE_PAGE
{
    UPDATE_PAGE_CHECK,
    UPDATE_PAGE_AVAILABLE,
    UPDATE_PAGE_DOWNLOAD,
    UPDATE_PAGE_READY,
    UPDATE_PAGE_COUNT
} UPDATE_PAGE;

typedef enum _STAGE_STEP
{
    StageDownload,
    StageVerify,
    StageExtract,
    StagePrepare
} STAGE_STEP;

typedef struct _UPDATE_INFO
{
    BOOL Available;
    BOOL Staged;
    ULONG TagId;
    ULONG Build;
    ULONGLONG Size;
    WCHAR TagName[128];
    WCHAR Url[1024];
    WCHAR Sha256[65];
    PWSTR Notes;
} UPDATE_INFO, *PUPDATE_INFO;

typedef struct _STAGE_CONTEXT
{
    HWND hwndNotify;
    STAGE_STEP Step;
    volatile LONG Cancel;
} STAGE_CONTEXT, *PSTAGE_CONTEXT;

HANDLE ProcessHeap;
SETUPDATA SetupData;

static UPDATE_INFO Update;
static ULONG LocalBuild;
static WCHAR StageDirectory[MAX_PATH];
static STAGE_CONTEXT Stage;

static INT_PTR CALLBACK CheckPageProc(HWND, UINT, WPARAM, LPARAM);
static INT_PTR CALLBACK AvailablePageProc(HWND, UINT, WPARAM, LPARAM);
static INT_PTR CALLBACK DownloadPageProc(HWND, UINT, WPARAM, LPARAM);
static INT_PTR CALLBACK ReadyPageProc(HWND, UINT, WPARAM, LPARAM);
static INT_PTR CALLBACK CompletePageProc(HWND, UINT, WPARAM, LPARAM);

static const SETUP_PAGE_INFO UpdatePages[UPDATE_PAGE_COUNT] =
{
    { IDD_UPDATECHECKPAGE, CheckPageProc,     IDS_UPDATE_CHECKTITLE,    IDS_UPDATE_CHECKSUBTITLE,
      SETUP_PAGE_OPTIONAL | SETUP_PAGE_CLOSE },
    { IDD_UPDATEPAGE,      AvailablePageProc, IDS_UPDATE_TITLE,         0,                           SETUP_PAGE_CLOSE },
    { IDD_PROCESSPAGE,     DownloadPageProc,  IDS_UPDATE_DOWNLOADTITLE, IDS_UPDATE_DOWNLOADSUBTITLE, 0 },
    { IDD_UPDATEREADYPAGE, ReadyPageProc,     IDS_UPDATE_READYTITLE,    IDS_UPDATE_READYSUBTITLE,    SETUP_PAGE_CLOSE },
};

static const SETUP_PAGE_INFO CompletePages[] =
{
    { IDD_PROCESSPAGE, CompletePageProc, IDS_UPDATE_COMPLETETITLE, IDS_UPDATE_COMPLETESUBTITLE, SETUP_PAGE_NO_CANCEL },
};

typedef struct _KEPT_RECORD
{
    struct _KEPT_RECORD* Next;
    HKEY Root;
    BOOL Tree;
    BOOL HasValue;
    BOOL Present;
    DWORD Type;
    DWORD Size;
    PBYTE Data;
    WCHAR Key[MAX_PATH];
    WCHAR Value[MAX_PATH];
} KEPT_RECORD, *PKEPT_RECORD;

static DWORD
ReadSetting(
    _In_ PCWSTR Name,
    _In_ DWORD Default)
{
    DWORD Value, Size = sizeof(Value);

    if (RegGetValueW(HKEY_LOCAL_MACHINE, UPDATE_KEY, Name, RRF_RT_REG_DWORD, NULL, &Value, &Size) != ERROR_SUCCESS)
        return Default;
    return Value;
}

static ULONGLONG
ReadSettingTime(
    _In_ PCWSTR Name)
{
    ULONGLONG Value = 0;
    DWORD Size = sizeof(Value);

    RegGetValueW(HKEY_LOCAL_MACHINE, UPDATE_KEY, Name, RRF_RT_REG_QWORD, NULL, &Value, &Size);
    return Value;
}

static VOID
WriteSetting(
    _In_ PCWSTR Name,
    _In_ DWORD Type,
    _In_reads_bytes_(Size) const VOID* Data,
    _In_ DWORD Size)
{
    HKEY hKey;

    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, UPDATE_KEY, 0, NULL, 0, KEY_SET_VALUE, NULL, &hKey, NULL) == ERROR_SUCCESS)
    {
        RegSetValueExW(hKey, Name, 0, Type, Data, Size);
        RegCloseKey(hKey);
    }
}

static ULONGLONG
CurrentTime(VOID)
{
    FILETIME Now;

    GetSystemTimeAsFileTime(&Now);
    return ((ULONGLONG)Now.dwHighDateTime << 32) | Now.dwLowDateTime;
}

static PCWSTR
GetArchitectureName(VOID)
{
    SYSTEM_INFO Info;

    GetNativeSystemInfo(&Info);
    switch (Info.wProcessorArchitecture)
    {
        case PROCESSOR_ARCHITECTURE_AMD64: return L"amd64";
        case PROCESSOR_ARCHITECTURE_ARM64: return L"arm64";
        case PROCESSOR_ARCHITECTURE_INTEL: return L"i386";
        case PROCESSOR_ARCHITECTURE_RISCV64: return L"riscv64";
        default: return L"unknown";
    }
}

static VOID
StagePath(
    _In_opt_ PCWSTR Name,
    _Out_writes_(MAX_PATH) PWSTR Path)
{
    if (Name)
        StringCchPrintfW(Path, MAX_PATH, L"%s\\%s", StageDirectory, Name);
    else
        StringCchCopyW(Path, MAX_PATH, StageDirectory);
}

static PCSTR
JsonValue(
    _In_ PCSTR Json,
    _In_ PCSTR Key)
{
    CHAR Pattern[64];
    PCSTR Found;

    if (FAILED(StringCchPrintfA(Pattern, ARRAYSIZE(Pattern), "\"%s\"", Key)))
        return NULL;
    Found = strstr(Json, Pattern);
    if (!Found)
        return NULL;
    Found += strlen(Pattern);
    while (*Found == ' ' || *Found == '\t' || *Found == '\r' || *Found == '\n')
        Found++;
    if (*Found++ != ':')
        return NULL;
    while (*Found == ' ' || *Found == '\t' || *Found == '\r' || *Found == '\n')
        Found++;
    return Found;
}

static ULONGLONG
JsonNumber(
    _In_ PCSTR Json,
    _In_ PCSTR Key)
{
    PCSTR Value = JsonValue(Json, Key);

    return (Value && *Value >= '0' && *Value <= '9') ? _strtoui64(Value, NULL, 10) : 0;
}

static BOOL
JsonBoolean(
    _In_ PCSTR Json,
    _In_ PCSTR Key)
{
    PCSTR Value = JsonValue(Json, Key);

    return Value && strncmp(Value, "true", 4) == 0;
}

static ULONG
ReadHex4(
    _In_ PCSTR Text,
    _Out_ PBOOL Valid)
{
    CHAR Digits[5];
    PSTR End;
    ULONG Value;

    *Valid = FALSE;
    if (strlen(Text) < 4)
        return 0;
    memcpy(Digits, Text, 4);
    Digits[4] = ANSI_NULL;
    Value = strtoul(Digits, &End, 16);
    *Valid = (End == Digits + 4);
    return Value;
}

static SIZE_T
PutUtf8(
    _In_ ULONG Code,
    _Out_writes_(4) PSTR Out)
{
    if (Code < 0x80)
    {
        Out[0] = (CHAR)Code;
        return 1;
    }
    if (Code < 0x800)
    {
        Out[0] = (CHAR)(0xC0 | (Code >> 6));
        Out[1] = (CHAR)(0x80 | (Code & 0x3F));
        return 2;
    }
    if (Code < 0x10000)
    {
        Out[0] = (CHAR)(0xE0 | (Code >> 12));
        Out[1] = (CHAR)(0x80 | ((Code >> 6) & 0x3F));
        Out[2] = (CHAR)(0x80 | (Code & 0x3F));
        return 3;
    }
    Out[0] = (CHAR)(0xF0 | (Code >> 18));
    Out[1] = (CHAR)(0x80 | ((Code >> 12) & 0x3F));
    Out[2] = (CHAR)(0x80 | ((Code >> 6) & 0x3F));
    Out[3] = (CHAR)(0x80 | (Code & 0x3F));
    return 4;
}

static PWSTR
JsonString(
    _In_ PCSTR Json,
    _In_ PCSTR Key,
    _In_ SIZE_T MaxChars)
{
    PCSTR Value = JsonValue(Json, Key);
    PSTR Utf8;
    PWSTR Text;
    SIZE_T Length = 0;
    ULONG Code, Low;
    BOOL Valid;
    INT Chars;

    if (!Value || *Value++ != '"')
        return NULL;
    Utf8 = HeapAlloc(ProcessHeap, 0, MaxChars * 4 + 1);
    if (!Utf8)
        return NULL;

    while (*Value && *Value != '"' && Length + 4 < MaxChars * 4)
    {
        if (*Value != '\\')
        {
            Utf8[Length++] = *Value++;
            continue;
        }

        Value++;
        switch (*Value)
        {
            case 'n': Utf8[Length++] = '\n'; Value++; break;
            case 'r': Utf8[Length++] = '\r'; Value++; break;
            case 't': Utf8[Length++] = '\t'; Value++; break;
            case 'u':
                Code = ReadHex4(Value + 1, &Valid);
                if (!Valid)
                {
                    Value++;
                    break;
                }
                Value += 5;
                if (Code >= 0xD800 && Code < 0xDC00 && Value[0] == '\\' && Value[1] == 'u')
                {
                    Low = ReadHex4(Value + 2, &Valid);
                    if (Valid && Low >= 0xDC00 && Low < 0xE000)
                    {
                        Code = 0x10000 + ((Code - 0xD800) << 10) + (Low - 0xDC00);
                        Value += 6;
                    }
                }
                Length += PutUtf8(Code, Utf8 + Length);
                break;
            case ANSI_NULL:
                break;
            default:
                Utf8[Length++] = *Value++;
                break;
        }
    }
    Utf8[Length] = ANSI_NULL;

    Chars = MultiByteToWideChar(CP_UTF8, 0, Utf8, -1, NULL, 0);
    Text = Chars > 0 ? HeapAlloc(ProcessHeap, 0, Chars * sizeof(WCHAR)) : NULL;
    if (Text)
        MultiByteToWideChar(CP_UTF8, 0, Utf8, -1, Text, Chars);
    HeapFree(ProcessHeap, 0, Utf8);
    return Text;
}

static VOID
CopyJsonString(
    _In_ PCSTR Json,
    _In_ PCSTR Key,
    _Out_writes_(cchText) PWSTR Text,
    _In_ SIZE_T cchText)
{
    PWSTR Value = JsonString(Json, Key, cchText);

    Text[0] = UNICODE_NULL;
    if (Value)
    {
        StringCchCopyW(Text, cchText, Value);
        HeapFree(ProcessHeap, 0, Value);
    }
}

static HRESULT
GetJobError(
    _In_ IBackgroundCopyJob* Job)
{
    IBackgroundCopyError* Error;
    BG_ERROR_CONTEXT Context;
    HRESULT hrJob = E_FAIL;

    if (SUCCEEDED(IBackgroundCopyJob_GetError(Job, &Error)))
    {
        IBackgroundCopyError_GetError(Error, &Context, &hrJob);
        IBackgroundCopyError_Release(Error);
    }
    return hrJob;
}

static HRESULT
WaitForJob(
    _In_ IBackgroundCopyJob* Job,
    _In_opt_ PUPDATE_PROGRESS Progress,
    _In_opt_ PVOID Context)
{
    BG_JOB_PROGRESS JobProgress;
    BG_JOB_STATE State, LastState = BG_JOB_STATE_QUEUED;
    UINT64 LastBytes = 0;
    DWORD LastChange = GetTickCount();
    HRESULT hr;

    for (;;)
    {
        hr = IBackgroundCopyJob_GetState(Job, &State);
        if (FAILED(hr))
            return hr;

        if (SUCCEEDED(IBackgroundCopyJob_GetProgress(Job, &JobProgress)))
        {
            if (JobProgress.BytesTransferred != LastBytes)
            {
                LastBytes = JobProgress.BytesTransferred;
                LastChange = GetTickCount();
            }
            if (Progress &&
                !Progress(Context, JobProgress.BytesTransferred,
                          JobProgress.BytesTotal == BG_SIZE_UNKNOWN ? 0 : JobProgress.BytesTotal))
            {
                IBackgroundCopyJob_Cancel(Job);
                return HRESULT_FROM_WIN32(ERROR_CANCELLED);
            }
        }

        if (State != LastState)
        {
            LastState = State;
            LastChange = GetTickCount();
        }

        if (State == BG_JOB_STATE_TRANSFERRED)
            return IBackgroundCopyJob_Complete(Job);

        if (State == BG_JOB_STATE_ERROR)
        {
            hr = GetJobError(Job);
            IBackgroundCopyJob_Cancel(Job);
            return hr;
        }

        if (State == BG_JOB_STATE_CANCELLED ||
            (State != BG_JOB_STATE_TRANSIENT_ERROR && GetTickCount() - LastChange > BITS_NO_PROGRESS))
        {
            if (State != BG_JOB_STATE_CANCELLED)
                IBackgroundCopyJob_Cancel(Job);
            return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
        }

        Sleep(BITS_POLL);
    }
}

static HRESULT
DownloadFile(
    _In_ PCWSTR Url,
    _In_ PCWSTR LocalPath,
    _In_opt_ PUPDATE_PROGRESS Progress,
    _In_opt_ PVOID Context)
{
    IBackgroundCopyManager* Manager = NULL;
    IBackgroundCopyJob* Job = NULL;
    GUID JobId;
    HRESULT hr;

    hr = CoCreateInstance(&CLSID_BackgroundCopyManager, NULL, CLSCTX_LOCAL_SERVER,
                          &IID_IBackgroundCopyManager, (PVOID*)&Manager);
    if (FAILED(hr))
    {
        DPRINT1("BITS is not available, hr 0x%08lx\n", hr);
        return hr;
    }

    DeleteFileW(LocalPath);
    hr = IBackgroundCopyManager_CreateJob(Manager, UPDATE_DESCRIPTION, BG_JOB_TYPE_DOWNLOAD, &JobId, &Job);
    if (SUCCEEDED(hr))
        hr = IBackgroundCopyJob_AddFile(Job, Url, LocalPath);
    if (SUCCEEDED(hr))
        hr = IBackgroundCopyJob_SetNoProgressTimeout(Job, BITS_NO_PROGRESS / 1000);
    if (SUCCEEDED(hr))
        hr = IBackgroundCopyJob_Resume(Job);
    if (SUCCEEDED(hr))
        hr = WaitForJob(Job, Progress, Context);
    else if (Job)
        IBackgroundCopyJob_Cancel(Job);

    if (Job)
        IBackgroundCopyJob_Release(Job);
    IBackgroundCopyManager_Release(Manager);
    DPRINT1("Download of %S finished with 0x%08lx\n", Url, hr);
    return hr;
}

static PSTR
ReadSmallFile(
    _In_ PCWSTR Path)
{
    HANDLE File;
    PSTR Text = NULL;
    DWORD Size, Read;

    File = CreateFileW(Path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (File == INVALID_HANDLE_VALUE)
        return NULL;

    Size = GetFileSize(File, NULL);
    if (Size != INVALID_FILE_SIZE && Size < UPDATE_RESPONSE_MAX)
    {
        Text = HeapAlloc(ProcessHeap, 0, Size + 1);
        if (Text && ReadFile(File, Text, Size, &Read, NULL) && Read == Size)
        {
            Text[Size] = ANSI_NULL;
        }
        else if (Text)
        {
            HeapFree(ProcessHeap, 0, Text);
            Text = NULL;
        }
    }
    CloseHandle(File);
    return Text;
}

static BOOL
ReadStagedRequest(
    _Out_ PULONG TagId,
    _Out_writes_(UPDATE_ID_CHARS) PWSTR EntryId,
    _Out_writes_(UPDATE_ID_CHARS) PWSTR PreviousDefault)
{
    WCHAR Path[MAX_PATH];

    StagePath(UPDATE_STAGE_REQUEST, Path);
    *TagId = GetPrivateProfileIntW(UPDATE_STAGE_SECTION, L"TagId", 0, Path);
    GetPrivateProfileStringW(UPDATE_STAGE_SECTION, L"Entry", L"", EntryId, UPDATE_ID_CHARS, Path);
    GetPrivateProfileStringW(UPDATE_STAGE_SECTION, L"PreviousDefault", L"", PreviousDefault, UPDATE_ID_CHARS, Path);
    return *TagId != 0 && EntryId[0] != UNICODE_NULL;
}

static VOID
SetCompletionCommand(
    _In_ BOOL Enable)
{
    WCHAR Command[MAX_PATH + 32];
    HKEY hKey;

    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, RUN_KEY, 0, NULL, 0, KEY_SET_VALUE, NULL, &hKey, NULL) != ERROR_SUCCESS)
        return;

    if (!Enable)
        RegDeleteValueW(hKey, COMPLETE_VALUE);
    else if (GetSystemDirectoryW(Command, MAX_PATH) &&
             SUCCEEDED(StringCchCatW(Command, ARRAYSIZE(Command), L"\\" COMPLETE_COMMAND)))
        RegSetValueExW(hKey, COMPLETE_VALUE, 0, REG_SZ, (const BYTE*)Command, (DWORD)((wcslen(Command) + 1) * sizeof(WCHAR)));
    RegCloseKey(hKey);
}

static BOOL
GetSystemPartitionPath(
    _In_ PCWSTR Relative,
    _Out_writes_(MAX_PATH) PWSTR Path)
{
    WCHAR SystemPartition[MAX_PATH];
    DWORD Size = sizeof(SystemPartition);

    return RegGetValueW(HKEY_LOCAL_MACHINE, SETUP_KEY, L"SystemPartition", RRF_RT_REG_SZ, NULL, SystemPartition,
                        &Size) == ERROR_SUCCESS &&
           SUCCEEDED(StringCchPrintfW(Path, MAX_PATH, L"\\\\?\\GLOBALROOT%s\\%s", SystemPartition, Relative));
}

static BOOL
IsStageIntact(VOID)
{
    static const PCWSTR Files[] = { UPDATE_STAGE_LOADER, UPDATE_STAGE_IMAGE, UPDATE_STAGE_REQUEST };
    WCHAR Path[MAX_PATH];
    ULONG Index;

    for (Index = 0; Index < ARRAYSIZE(Files); Index++)
    {
        StagePath(Files[Index], Path);
        if (GetFileAttributesW(Path) == INVALID_FILE_ATTRIBUTES)
            return FALSE;
    }
    StagePath(UPDATE_STAGE_REQUEST, Path);
    return GetPrivateProfileIntW(UPDATE_STAGE_SECTION, L"Completed", 0, Path) == 0;
}

static VOID
RemoveStagedBackup(VOID)
{
    WCHAR Directory[MAX_PATH], Path[MAX_PATH];
    WIN32_FIND_DATAW Find;
    HANDLE hFind;

    StagePath(STAGE_BACKUP L"\\*", Path);
    hFind = FindFirstFileW(Path, &Find);
    StagePath(STAGE_BACKUP, Directory);
    if (hFind != INVALID_HANDLE_VALUE)
    {
        do
        {
            if (!(Find.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                SUCCEEDED(StringCchPrintfW(Path, ARRAYSIZE(Path), L"%s\\%s", Directory, Find.cFileName)))
            {
                SetFileAttributesW(Path, FILE_ATTRIBUTE_NORMAL);
                DeleteFileW(Path);
            }
        } while (FindNextFileW(hFind, &Find));
        FindClose(hFind);
    }
    RemoveDirectoryW(Directory);
}

static DWORD
RemoveStage(VOID)
{
    static const PCWSTR WorkFiles[] = { L"update.zip", L"inventory.tmp", L"setup.sif", L"stub.inf", L"registry.tmp",
                                        L"bootmgr.tmp" };
    static const PCWSTR BootFiles[] = { UPDATE_STAGE_LOADER, UPDATE_STAGE_IMAGE, STAGE_STATUS, UPDATE_STAGE_REQUEST };
    WCHAR Path[MAX_PATH], EntryId[UPDATE_ID_CHARS], PreviousDefault[UPDATE_ID_CHARS];
    ULONG TagId, Index;
    HKEY Store;
    DWORD Error;

    for (Index = 0; Index < ARRAYSIZE(WorkFiles); Index++)
    {
        StagePath(WorkFiles[Index], Path);
        SetFileAttributesW(Path, FILE_ATTRIBUTE_NORMAL);
        DeleteFileW(Path);
    }

    StagePath(NULL, Path);
    if (GetFileAttributesW(Path) == INVALID_FILE_ATTRIBUTES)
    {
        SetCompletionCommand(FALSE);
        return ERROR_SUCCESS;
    }

    ReadStagedRequest(&TagId, EntryId, PreviousDefault);
    Error = OpenBcdStore(&Store);
    if (Error == ERROR_SUCCESS)
    {
        Error = RemoveBcdUpgradeEntries(Store, STAGE_LOADER_PATH, PreviousDefault);
        RegCloseKey(Store);
    }
    else
    {
        StagePath(UPDATE_STAGE_LOADER, Path);
        if (GetFileAttributesW(Path) == INVALID_FILE_ATTRIBUTES)
            Error = ERROR_SUCCESS;
    }
    if (Error != ERROR_SUCCESS)
    {
        DPRINT1("The staged boot entry could not be removed (%lu); the staged update is kept\n", Error);
        return Error;
    }

    for (Index = 0; Index < ARRAYSIZE(BootFiles); Index++)
    {
        StagePath(BootFiles[Index], Path);
        SetFileAttributesW(Path, FILE_ATTRIBUTE_NORMAL);
        DeleteFileW(Path);
    }
    RemoveStagedBackup();
    StagePath(NULL, Path);
    SetFileAttributesW(Path, FILE_ATTRIBUTE_NORMAL);
    RemoveDirectoryW(Path);
    if (GetSystemPartitionPath(BOOT_UPDATE_STATE, Path))
        DeleteFileW(Path);
    SetCompletionCommand(FALSE);
    return ERROR_SUCCESS;
}

static BOOL
IsSha256(
    _In_ PCWSTR Text)
{
    ULONG Index;

    for (Index = 0; Index < SHA256_CHARS - 1; Index++)
    {
        if (!iswxdigit(Text[Index]))
            return FALSE;
    }
    return Text[Index] == UNICODE_NULL;
}

static DWORD
CheckForUpdate(
    _Out_ PUPDATE_INFO Info)
{
    WCHAR Server[512], Url[768], TempPath[MAX_PATH], Response[MAX_PATH];
    WCHAR EntryId[UPDATE_ID_CHARS], PreviousDefault[UPDATE_ID_CHARS];
    DWORD Size = sizeof(Server);
    ULONGLONG Now;
    ULONG StagedTag;
    PSTR Json;
    HRESULT hr;

    if (Info->Notes)
        HeapFree(ProcessHeap, 0, Info->Notes);
    ZeroMemory(Info, sizeof(*Info));

    if (RegGetValueW(HKEY_LOCAL_MACHINE, UPDATE_KEY, L"Server", RRF_RT_REG_SZ, NULL, Server, &Size) != ERROR_SUCCESS)
        StringCchCopyW(Server, ARRAYSIZE(Server), DEFAULT_SERVER);
    if (FAILED(StringCchPrintfW(Url, ARRAYSIZE(Url), L"%s?arch=%s&build=%lu", Server, GetArchitectureName(), LocalBuild)) ||
        !GetTempPathW(ARRAYSIZE(TempPath), TempPath) ||
        FAILED(StringCchPrintfW(Response, ARRAYSIZE(Response), L"%slntupdate.json", TempPath)))
    {
        return ERROR_BUFFER_OVERFLOW;
    }

    hr = DownloadFile(Url, Response, NULL, NULL);
    if (FAILED(hr))
        return (DWORD)hr;

    Json = ReadSmallFile(Response);
    DeleteFileW(Response);
    if (!Json)
        return ERROR_INVALID_DATA;

    Info->TagId = (ULONG)JsonNumber(Json, "tag_id");
    Info->Build = (ULONG)JsonNumber(Json, "build");
    Info->Size = JsonNumber(Json, "size");
    CopyJsonString(Json, "tag_name", Info->TagName, ARRAYSIZE(Info->TagName));
    CopyJsonString(Json, "url", Info->Url, ARRAYSIZE(Info->Url));
    CopyJsonString(Json, "sha256", Info->Sha256, ARRAYSIZE(Info->Sha256));
    Info->Notes = JsonString(Json, "notes", UPDATE_NOTES_MAX);
    Info->Available = JsonBoolean(Json, "available") && Info->TagId != 0 && Info->Url[0] &&
                      Info->Size != 0 && IsSha256(Info->Sha256) &&
                      (LocalBuild == 0 || Info->Build > LocalBuild);
    if (JsonBoolean(Json, "available") && !Info->Available && (!IsSha256(Info->Sha256) || Info->Size == 0))
        DPRINT1("The update server offered tag %lu without a size or SHA-256; it is not offered\n", Info->TagId);
    HeapFree(ProcessHeap, 0, Json);

    Info->Staged = Info->Available && IsStageIntact() && ReadStagedRequest(&StagedTag, EntryId, PreviousDefault) &&
                   StagedTag == Info->TagId;

    Now = CurrentTime();
    WriteSetting(L"LastCheck", REG_QWORD, &Now, sizeof(Now));
    if (Info->Available)
    {
        WriteSetting(L"LatestTag", REG_DWORD, &Info->TagId, sizeof(Info->TagId));
        WriteSetting(L"LatestName", REG_SZ, Info->TagName, (DWORD)((wcslen(Info->TagName) + 1) * sizeof(WCHAR)));
    }
    DPRINT1("Update check: build %lu, tag %lu '%S' build %lu, available %d\n",
            LocalBuild, Info->TagId, Info->TagName, Info->Build, Info->Available);
    return ERROR_SUCCESS;
}

static BOOL
HashFile(
    _In_ PCWSTR Path,
    _Out_writes_(SHA256_CHARS) PWSTR Text)
{
    BCRYPT_ALG_HANDLE hAlgorithm = NULL;
    BCRYPT_HASH_HANDLE hHash = NULL;
    BYTE Hash[32];
    PBYTE Buffer;
    HANDLE File;
    DWORD Read, Index;
    BOOL Hashed = FALSE;

    Text[0] = UNICODE_NULL;
    File = CreateFileW(Path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (File == INVALID_HANDLE_VALUE)
        return FALSE;

    Buffer = HeapAlloc(ProcessHeap, 0, 0x100000);
    if (Buffer &&
        BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&hAlgorithm, BCRYPT_SHA256_ALGORITHM, NULL, 0)) &&
        BCRYPT_SUCCESS(BCryptCreateHash(hAlgorithm, &hHash, NULL, 0, NULL, 0, 0)))
    {
        Hashed = TRUE;
        for (;;)
        {
            if (!ReadFile(File, Buffer, 0x100000, &Read, NULL))
            {
                Hashed = FALSE;
                break;
            }
            if (Read == 0)
                break;
            if (!BCRYPT_SUCCESS(BCryptHashData(hHash, Buffer, Read, 0)))
            {
                Hashed = FALSE;
                break;
            }
        }
        Hashed = Hashed && BCRYPT_SUCCESS(BCryptFinishHash(hHash, Hash, sizeof(Hash), 0));
    }

    if (hHash)
        BCryptDestroyHash(hHash);
    if (hAlgorithm)
        BCryptCloseAlgorithmProvider(hAlgorithm, 0);
    if (Buffer)
        HeapFree(ProcessHeap, 0, Buffer);
    CloseHandle(File);
    if (!Hashed)
        return FALSE;

    for (Index = 0; Index < ARRAYSIZE(Hash); Index++)
        StringCchPrintfW(&Text[Index * 2], 3, L"%02x", Hash[Index]);
    return TRUE;
}

static BOOL
HashMatches(
    _In_ PCWSTR Path,
    _In_ PCWSTR Expected)
{
    WCHAR Text[SHA256_CHARS];

    if (!HashFile(Path, Text))
        return FALSE;
    DPRINT1("%S has SHA-256 %S, expected %S\n", Path, Text, Expected);
    return _wcsicmp(Text, Expected) == 0;
}

static BOOL
CALLBACK
StageProgress(
    _In_ PVOID Context,
    _In_ ULONGLONG Done,
    _In_ ULONGLONG Total)
{
    PSTAGE_CONTEXT StageContext = (PSTAGE_CONTEXT)Context;
    ULONG Percent = Total ? (ULONG)(Done * 100 / Total) : 0;

    if (StageContext->hwndNotify)
    {
        PostMessageW(StageContext->hwndNotify, PM_STAGE_PROGRESS, StageContext->Step,
                     (LPARAM)((Percent << 24) | (ULONG)min(Done >> 20, 0xFFFFFF)));
    }
    return StageContext->Cancel == 0;
}

static DWORD
WriteStagedValue(
    _In_ PCWSTR Name,
    _In_ PCWSTR Value)
{
    WCHAR Path[MAX_PATH];

    StagePath(UPDATE_STAGE_REQUEST, Path);
    return WritePrivateProfileStringW(UPDATE_STAGE_SECTION, Name, Value, Path) ? ERROR_SUCCESS : GetLastError();
}

static DWORD
WriteStagedRequest(
    _In_ PCWSTR ImageHash,
    _In_opt_ PCWSTR EntryId,
    _In_opt_ PCWSTR PreviousDefault)
{
    WCHAR Number[16];
    DWORD Error;

    StringCchPrintfW(Number, ARRAYSIZE(Number), L"%lu", Update.TagId);
    Error = WriteStagedValue(L"TagId", Number);
    if (Error == ERROR_SUCCESS)
        Error = WriteStagedValue(L"TagName", Update.TagName);
    StringCchPrintfW(Number, ARRAYSIZE(Number), L"%lu", Update.Build);
    if (Error == ERROR_SUCCESS)
        Error = WriteStagedValue(L"Build", Number);
    if (Error == ERROR_SUCCESS)
        Error = WriteStagedValue(L"ImageSha256", ImageHash);
    if (Error == ERROR_SUCCESS && EntryId)
        Error = WriteStagedValue(L"Entry", EntryId);
    if (Error == ERROR_SUCCESS && PreviousDefault)
        Error = WriteStagedValue(L"PreviousDefault", PreviousDefault);
    return Error;
}

static DWORD
StageUpdate(
    _Inout_ PSTAGE_CONTEXT Context)
{
    WCHAR Zip[MAX_PATH], Image[MAX_PATH], Loader[MAX_PATH], Path[MAX_PATH];
    WCHAR EntryId[UPDATE_ID_CHARS], PreviousDefault[UPDATE_ID_CHARS], ImageHash[SHA256_CHARS];
    WIN32_FILE_ATTRIBUTE_DATA Attributes;
    ULARGE_INTEGER FreeBytes;
    HKEY Store;
    HRESULT hr;
    DWORD Error;

    if (!IsSha256(Update.Sha256) || Update.Size == 0)
        return ERROR_INVALID_DATA;

    Error = RemoveStage();
    if (Error != ERROR_SUCCESS)
        return Error;
    StagePath(NULL, Path);
    if (!CreateDirectoryW(Path, NULL) && GetLastError() != ERROR_ALREADY_EXISTS)
        return GetLastError();
    SetFileAttributesW(Path, FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM);
    if (GetDiskFreeSpaceExW(Path, &FreeBytes, NULL, NULL) &&
        FreeBytes.QuadPart < Update.Size + STAGE_SPACE_MARGIN)
    {
        return ERROR_DISK_FULL;
    }

    StagePath(L"update.zip", Zip);
    StagePath(UPDATE_STAGE_IMAGE, Image);
    StagePath(UPDATE_STAGE_LOADER, Loader);

    Context->Step = StageDownload;
    hr = DownloadFile(Update.Url, Zip, StageProgress, Context);
    if (FAILED(hr))
        return HRESULT_CODE(hr) == ERROR_CANCELLED ? ERROR_CANCELLED : (DWORD)hr;

    Context->Step = StageVerify;
    StageProgress(Context, 0, 0);
    if (!GetFileAttributesExW(Zip, GetFileExInfoStandard, &Attributes) ||
        (((ULONGLONG)Attributes.nFileSizeHigh << 32) | Attributes.nFileSizeLow) != Update.Size)
    {
        DPRINT1("The downloaded package does not have the announced size %I64u\n", Update.Size);
        return ERROR_INVALID_DATA;
    }
    if (!HashMatches(Zip, Update.Sha256))
        return ERROR_INVALID_DATA;

    Context->Step = StageExtract;
    Error = ExtractZipImage(Zip, Image, StageProgress, Context);
    DeleteFileW(Zip);
    if (Error != ERROR_SUCCESS)
        return Error;

    Context->Step = StagePrepare;
    StageProgress(Context, 0, 0);
    if (!HashFile(Image, ImageHash))
        return ERROR_READ_FAULT;
    DPRINT1("The staged image has SHA-256 %S\n", ImageHash);
    Error = ExtractImageFile(Image, UPDATE_IMAGE_LOADER, Loader);
    if (Error == ERROR_SUCCESS)
        Error = WriteStagedRequest(ImageHash, NULL, NULL);
    if (Error != ERROR_SUCCESS)
        return Error;

    /* TODO: systems started by FreeLoader have no boot configuration store to stage the install in */
    Error = OpenBcdStore(&Store);
    if (Error != ERROR_SUCCESS)
        return Error;
    Error = AddBcdUpgradeEntry(Store, UPDATE_DESCRIPTION, STAGE_LOADER_PATH, STAGE_IMAGE_PATH, ImageHash, NULL,
                               EntryId, PreviousDefault);
    RegCloseKey(Store);
    if (Error == ERROR_SUCCESS)
        Error = WriteStagedRequest(ImageHash, EntryId, PreviousDefault);
    if (Error == ERROR_SUCCESS)
        SetCompletionCommand(TRUE);
    return Error;
}

static DWORD
WINAPI
StageThread(
    LPVOID Parameter)
{
    PSTAGE_CONTEXT Context = (PSTAGE_CONTEXT)Parameter;
    HRESULT hrCom = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    DWORD Error = StageUpdate(Context);

    if (Error != ERROR_SUCCESS)
        RemoveStage();
    if (SUCCEEDED(hrCom))
        CoUninitialize();
    DPRINT1("Update staging finished with %lu\n", Error);
    if (Context->hwndNotify)
        PostMessageW(Context->hwndNotify, PM_STAGE_DONE, Error, 0);
    return Error;
}

static DWORD
WINAPI
CheckThread(
    LPVOID Parameter)
{
    HWND hwndNotify = (HWND)Parameter;
    HRESULT hrCom = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    DWORD Error = CheckForUpdate(&Update);

    if (SUCCEEDED(hrCom))
        CoUninitialize();
    PostMessageW(hwndNotify, PM_CHECK_DONE, Error, 0);
    return Error;
}

static VOID
RestartToInstall(VOID)
{
    USHORT Current;
    BOOLEAN Enabled;

    RtlAdjustPrivilege(SE_SYSTEM_ENVIRONMENT_PRIVILEGE, TRUE, FALSE, &Enabled);
    if (GetFirmwareEnvironmentVariableExW(L"BootCurrent", EFI_GLOBAL_VARIABLE_GUID, &Current, sizeof(Current),
                                          NULL) == sizeof(Current) &&
        !SetFirmwareEnvironmentVariableExW(L"BootNext", EFI_GLOBAL_VARIABLE_GUID, &Current, sizeof(Current),
                                           EFI_VARIABLE_BOOT_ATTRIBUTES))
    {
        DPRINT1("BootNext Boot%04X not set, error %lu\n", Current, GetLastError());
    }

    RtlAdjustPrivilege(SE_SHUTDOWN_PRIVILEGE, TRUE, FALSE, &Enabled);
    if (!ExitWindowsEx(EWX_REBOOT, SHTDN_REASON_MAJOR_OPERATINGSYSTEM | SHTDN_REASON_MINOR_UPGRADE |
                                   SHTDN_REASON_FLAG_PLANNED))
    {
        DPRINT1("Restart failed, error %lu\n", GetLastError());
    }
}

static VOID
ShowError(
    _In_ HWND hwndDlg,
    _In_ UINT MessageId,
    _In_ DWORD Error)
{
    WCHAR Text[512];

    LoadPrintfW(Text, ARRAYSIZE(Text), MessageId, Error);
    SetupMessage(GetParent(hwndDlg), TD_ERROR_ICON, TDCBF_CLOSE_BUTTON, IDCLOSE, NULL, Text);
}

static INT_PTR
CALLBACK
CheckPageProc(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam)
{
    HWND hProgress = GetDlgItem(hwndDlg, IDC_UPDATE_CHECKPROGRESS);
    HANDLE hThread;

    switch (uMsg)
    {
        case SPN_QUERYSKIP:
            SetWindowLongPtrW(hwndDlg, DWLP_MSGRESULT, Update.TagId != 0);
            return TRUE;

        case SPN_SETACTIVE:
            SetupFrameShowButtons(hwndDlg, FALSE, FALSE, TRUE);
            SetResText(GetDlgItem(hwndDlg, IDC_UPDATE_CHECKTEXT), IDS_UPDATE_CHECKING);
            ShowWindow(hProgress, SW_SHOW);
            SendMessageW(hProgress, PBM_SETMARQUEE, TRUE, 0);
            hThread = CreateThread(NULL, 0, CheckThread, hwndDlg, 0, NULL);
            if (hThread)
                CloseHandle(hThread);
            else
                PostMessageW(hwndDlg, PM_CHECK_DONE, GetLastError(), 0);
            return TRUE;

        case PM_CHECK_DONE:
            SendMessageW(hProgress, PBM_SETMARQUEE, FALSE, 0);
            ShowWindow(hProgress, SW_HIDE);
            if (wParam == ERROR_SUCCESS && Update.Available)
            {
                SetupFrameGoTo(hwndDlg, Update.Staged ? UPDATE_PAGE_READY : UPDATE_PAGE_AVAILABLE);
            }
            else
            {
                WCHAR Text[512];

                if (wParam == ERROR_SUCCESS)
                    LoadPrintfW(Text, ARRAYSIZE(Text), IDS_UPDATE_UPTODATE, LocalBuild);
                else
                    LoadPrintfW(Text, ARRAYSIZE(Text), IDS_UPDATE_CHECKFAILED, (DWORD)wParam);
                SetDlgItemTextW(hwndDlg, IDC_UPDATE_CHECKTEXT, Text);
                SetupFrameSetButtonText(hwndDlg, IDCANCEL, IDS_CLOSEBTN);
            }
            return TRUE;
    }

    return FALSE;
}

static INT_PTR
CALLBACK
AvailablePageProc(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam)
{
    WCHAR Text[256], Size[32];

    switch (uMsg)
    {
        case SPN_SETACTIVE:
            LoadPrintfW(Text, ARRAYSIZE(Text), IDS_UPDATE_SUBTITLE, Update.TagName, Update.TagId);
            SetDlgItemTextW(GetParent(hwndDlg), IDC_FRAME_SUBTITLE, Text);
            SetDlgItemTextW(hwndDlg, IDC_UPDATE_NOTES, Update.Notes ? Update.Notes : L"");
            StrFormatByteSizeW((LONGLONG)Update.Size, Size, ARRAYSIZE(Size));
            LoadPrintfW(Text, ARRAYSIZE(Text), IDS_UPDATE_SIZE, Size);
            SetDlgItemTextW(hwndDlg, IDC_UPDATE_SIZE, Text);
            SetupFrameSetButtonText(hwndDlg, IDC_FRAME_NEXT, IDS_UPDATE_DOWNLOADBTN);
            SetFocus(GetDlgItem(GetParent(hwndDlg), IDC_FRAME_NEXT));
            return TRUE;

        case WM_COMMAND:
            if (LOWORD(wParam) == IDC_UPDATE_LATER && HIWORD(wParam) == BN_CLICKED)
            {
                ULONGLONG RemindAfter = CurrentTime() + REMIND_DELAY * FILETIME_SECOND;

                WriteSetting(L"RemindTag", REG_DWORD, &Update.TagId, sizeof(Update.TagId));
                WriteSetting(L"RemindAfter", REG_QWORD, &RemindAfter, sizeof(RemindAfter));
                EndDialog(GetParent(hwndDlg), IDCANCEL);
                return TRUE;
            }
            break;
    }

    return FALSE;
}

static VOID
ShowStageProgress(
    _In_ HWND hwndDlg,
    _In_ STAGE_STEP Step,
    _In_ LPARAM Value)
{
    static const UINT Activities[] =
    {
        IDS_UPDATE_DOWNLOADING, IDS_UPDATE_VERIFYING, IDS_UPDATE_EXTRACTING, IDS_UPDATE_PREPARING
    };
    WCHAR Text[256], Done[32], Total[32];
    ULONG Percent = (ULONG)Value >> 24;
    ULONGLONG Megabytes = (ULONG)Value & 0xFFFFFF;

    LoadPrintfW(Text, ARRAYSIZE(Text), Activities[Step], Update.TagName);
    SetDlgItemTextW(hwndDlg, IDC_ACTIVITY, Text);
    if (Step == StageDownload)
    {
        StrFormatByteSizeW((LONGLONG)(Megabytes << 20), Done, ARRAYSIZE(Done));
        StrFormatByteSizeW((LONGLONG)Update.Size, Total, ARRAYSIZE(Total));
        LoadPrintfW(Text, ARRAYSIZE(Text), IDS_UPDATE_DOWNLOADED, Done, Total);
        SetDlgItemTextW(hwndDlg, IDC_ITEM, Text);
    }
    else
    {
        SetDlgItemTextW(hwndDlg, IDC_ITEM, L"");
    }
    SendDlgItemMessageW(hwndDlg, IDC_PROCESSPROGRESS, PBM_SETPOS, Percent, 0);
}

static INT_PTR
CALLBACK
DownloadPageProc(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam)
{
    HANDLE hThread;

    switch (uMsg)
    {
        case SPN_SETACTIVE:
            SetupFrameShowButtons(hwndDlg, FALSE, FALSE, TRUE);
            SendDlgItemMessageW(hwndDlg, IDC_PROCESSPROGRESS, PBM_SETRANGE32, 0, 100);
            ShowStageProgress(hwndDlg, StageDownload, 0);
            Stage.hwndNotify = hwndDlg;
            Stage.Cancel = 0;
            hThread = CreateThread(NULL, 0, StageThread, &Stage, 0, NULL);
            if (hThread)
                CloseHandle(hThread);
            else
                PostMessageW(hwndDlg, PM_STAGE_DONE, GetLastError(), 0);
            return TRUE;

        case PM_STAGE_PROGRESS:
            ShowStageProgress(hwndDlg, (STAGE_STEP)wParam, lParam);
            return TRUE;

        case PM_STAGE_DONE:
            if (wParam == ERROR_SUCCESS)
            {
                Update.Staged = TRUE;
                SetupFrameGoTo(hwndDlg, UPDATE_PAGE_READY);
            }
            else
            {
                if (wParam != ERROR_CANCELLED)
                    ShowError(hwndDlg, IDS_UPDATE_STAGEFAILED, (DWORD)wParam);
                EndDialog(GetParent(hwndDlg), IDCANCEL);
            }
            return TRUE;

        case SPN_QUERYCANCEL:
            InterlockedExchange(&Stage.Cancel, 1);
            EnableSetupCancel(GetParent(hwndDlg), FALSE);
            SetWindowLongPtrW(hwndDlg, DWLP_MSGRESULT, TRUE);
            return TRUE;

        case SPN_WIZBACK:
        case SPN_WIZNEXT:
            SetWindowLongPtrW(hwndDlg, DWLP_MSGRESULT, -1);
            return TRUE;
    }

    return FALSE;
}

static INT_PTR
CALLBACK
ReadyPageProc(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam)
{
    WCHAR Text[512];

    switch (uMsg)
    {
        case SPN_SETACTIVE:
            LoadPrintfW(Text, ARRAYSIZE(Text), IDS_UPDATE_READYTEXT, Update.TagName);
            SetDlgItemTextW(hwndDlg, IDC_UPDATE_READYTEXT, Text);
            SetupFrameShowButtons(hwndDlg, FALSE, TRUE, TRUE);
            SetupFrameSetButtonText(hwndDlg, IDC_FRAME_NEXT, IDS_RESTARTBTN);
            SetupFrameSetButtonText(hwndDlg, IDCANCEL, IDS_UPDATE_LATERBTN);
            return TRUE;

        case SPN_WIZNEXT:
            RestartToInstall();
            EndDialog(GetParent(hwndDlg), IDOK);
            SetWindowLongPtrW(hwndDlg, DWLP_MSGRESULT, -1);
            return TRUE;
    }

    return FALSE;
}

static PBYTE
ReadWholeFile(
    _In_ PCWSTR Path,
    _Out_ PDWORD Size)
{
    HANDLE File;
    PBYTE Data = NULL;
    DWORD Read = 0;

    *Size = 0;
    File = CreateFileW(Path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (File == INVALID_HANDLE_VALUE)
        return NULL;

    *Size = GetFileSize(File, NULL);
    if (*Size != INVALID_FILE_SIZE && *Size < TEXT_FILE_MAX)
        Data = HeapAlloc(ProcessHeap, 0, *Size + 1);
    if (Data && (!ReadFile(File, Data, *Size, &Read, NULL) || Read != *Size))
    {
        HeapFree(ProcessHeap, 0, Data);
        Data = NULL;
    }
    CloseHandle(File);
    if (!Data)
        *Size = 0;
    return Data;
}

static BOOL
SameFileContents(
    _In_ PCWSTR First,
    _In_ PCWSTR Second)
{
    PBYTE A, B = NULL;
    DWORD SizeA, SizeB = 0;
    BOOL Same;

    A = ReadWholeFile(First, &SizeA);
    if (A)
        B = ReadWholeFile(Second, &SizeB);
    Same = A && B && SizeA == SizeB && memcmp(A, B, SizeA) == 0;
    if (A)
        HeapFree(ProcessHeap, 0, A);
    if (B)
        HeapFree(ProcessHeap, 0, B);
    return Same;
}

static HINF
OpenImageSetupInf(
    _In_ PCWSTR Image,
    _Out_writes_(MAX_PATH) PWSTR SystemMedia)
{
    WCHAR Name[MAX_PATH], Path[MAX_PATH], Target[MAX_PATH];
    INFCONTEXT Context;
    HINF hInf;

    SystemMedia[0] = UNICODE_NULL;
    StagePath(L"setup.sif", Path);
    if (FAILED(StringCchPrintfW(Name, ARRAYSIZE(Name), L"%s\\txtsetup.sif", GetArchitectureName())) ||
        ExtractImageFile(Image, Name, Path) != ERROR_SUCCESS)
    {
        return INVALID_HANDLE_VALUE;
    }

    hInf = SetupOpenInfFileW(Path, NULL, INF_STYLE_OLDNT | INF_STYLE_WIN4, NULL);
    if (hInf == INVALID_HANDLE_VALUE || !SetupFindFirstLineW(hInf, L"SystemTrees", NULL, &Context))
        return hInf;

    do
    {
        if (SetupGetStringFieldW(&Context, 1, Target, ARRAYSIZE(Target), NULL) &&
            (Target[0] == UNICODE_NULL || wcscmp(Target, L"\\") == 0))
        {
            SetupGetStringFieldW(&Context, 0, SystemMedia, MAX_PATH, NULL);
            break;
        }
    } while (SetupFindNextLine(&Context, &Context));
    return hInf;
}

static BOOL
IsImageInstalled(
    _In_ PCWSTR Image,
    _In_ PCWSTR SystemMedia)
{
    WCHAR Name[MAX_PATH], Extracted[MAX_PATH], Installed[MAX_PATH];
    BOOL Same = FALSE;

    StagePath(L"inventory.tmp", Extracted);
    if (SUCCEEDED(StringCchPrintfW(Name, ARRAYSIZE(Name), L"%s\\" UPGRADE_INVENTORY_PATH, SystemMedia)) &&
        ExtractImageFile(Image, Name, Extracted) == ERROR_SUCCESS &&
        GetWindowsDirectoryW(Installed, ARRAYSIZE(Installed)) &&
        SUCCEEDED(StringCchCatW(Installed, ARRAYSIZE(Installed), L"\\" UPGRADE_INVENTORY_PATH)))
    {
        Same = SameFileContents(Extracted, Installed);
    }
    DeleteFileW(Extracted);
    return Same;
}

static HKEY
KeptRoot(
    _In_ PCWSTR Name,
    _In_ HKEY DefaultUser)
{
    if (_wcsicmp(Name, L"HKLM") == 0)
        return HKEY_LOCAL_MACHINE;
    if (_wcsicmp(Name, L"HKCU") == 0)
        return DefaultUser;
    if (_wcsicmp(Name, L"HKU") == 0)
        return HKEY_USERS;
    if (_wcsicmp(Name, L"HKCR") == 0)
        return HKEY_CLASSES_ROOT;
    return NULL;
}

static PKEPT_RECORD
AddKept(
    _Inout_ PKEPT_RECORD** Tail,
    _In_ HKEY Root,
    _In_ PCWSTR Key,
    _In_opt_ PCWSTR Value,
    _In_ BOOL Tree)
{
    PKEPT_RECORD Record = HeapAlloc(ProcessHeap, HEAP_ZERO_MEMORY, sizeof(*Record));

    if (!Record)
        return NULL;
    Record->Root = Root;
    Record->Tree = Tree;
    Record->HasValue = (Value != NULL);
    StringCchCopyW(Record->Key, ARRAYSIZE(Record->Key), Key);
    if (Value)
        StringCchCopyW(Record->Value, ARRAYSIZE(Record->Value), Value);
    **Tail = Record;
    *Tail = &Record->Next;
    return Record;
}

static BOOL
CaptureKeptValue(
    _Inout_ PKEPT_RECORD** Tail,
    _In_ HKEY Root,
    _In_ HKEY hKey,
    _In_ PCWSTR Key,
    _In_ PCWSTR Value)
{
    PKEPT_RECORD Record = AddKept(Tail, Root, Key, Value, FALSE);
    DWORD Size = 0;

    if (!Record)
        return FALSE;
    if (!hKey || RegQueryValueExW(hKey, Value, NULL, &Record->Type, NULL, &Size) != ERROR_SUCCESS)
        return TRUE;

    Record->Data = HeapAlloc(ProcessHeap, 0, Size ? Size : 1);
    if (!Record->Data)
        return FALSE;
    Record->Size = Size;
    Record->Present = (RegQueryValueExW(hKey, Value, NULL, &Record->Type, Record->Data, &Record->Size) == ERROR_SUCCESS);
    return TRUE;
}

static BOOL
CaptureKeptKey(
    _Inout_ PKEPT_RECORD** Tail,
    _In_ HKEY Root,
    _In_ PCWSTR Key)
{
    WCHAR Name[256], Child[MAX_PATH];
    DWORD Index, Chars;
    HKEY hKey;
    BOOL Ok;

    if (RegOpenKeyExW(Root, Key, 0, KEY_READ, &hKey) != ERROR_SUCCESS)
        return FALSE;

    Ok = AddKept(Tail, Root, Key, NULL, FALSE) != NULL;
    for (Index = 0; Ok; Index++)
    {
        Chars = ARRAYSIZE(Name);
        if (RegEnumValueW(hKey, Index, Name, &Chars, NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
            break;
        Ok = CaptureKeptValue(Tail, Root, hKey, Key, Name);
    }
    for (Index = 0; Ok; Index++)
    {
        Chars = ARRAYSIZE(Name);
        if (RegEnumKeyExW(hKey, Index, Name, &Chars, NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
            break;
        Ok = SUCCEEDED(StringCchPrintfW(Child, ARRAYSIZE(Child), L"%s\\%s", Key, Name)) &&
             CaptureKeptKey(Tail, Root, Child);
    }
    RegCloseKey(hKey);
    return Ok;
}

static PKEPT_RECORD
SaveKeptSettings(
    _In_ HINF hSetup,
    _In_ HKEY DefaultUser)
{
    WCHAR RootName[16], Key[MAX_PATH], Value[MAX_PATH];
    PKEPT_RECORD Head = NULL, *Tail = &Head;
    INFCONTEXT Context;
    HKEY Root, hKey;

    if (!SetupFindFirstLineW(hSetup, L"HiveInfs.Upgrade.Keep", NULL, &Context))
        return NULL;

    do
    {
        if (!SetupGetStringFieldW(&Context, 1, RootName, ARRAYSIZE(RootName), NULL) ||
            !SetupGetStringFieldW(&Context, 2, Key, ARRAYSIZE(Key), NULL) ||
            !(Root = KeptRoot(RootName, DefaultUser)))
        {
            continue;
        }

        if (SetupGetFieldCount(&Context) >= 3 && SetupGetStringFieldW(&Context, 3, Value, ARRAYSIZE(Value), NULL))
        {
            if (RegOpenKeyExW(Root, Key, 0, KEY_QUERY_VALUE, &hKey) != ERROR_SUCCESS)
                hKey = NULL;
            CaptureKeptValue(&Tail, Root, hKey, Key, Value);
            if (hKey)
                RegCloseKey(hKey);
        }
        else if (AddKept(&Tail, Root, Key, NULL, TRUE) && !CaptureKeptKey(&Tail, Root, Key))
        {
            DPRINT1("Setting %S\\%S is not kept\n", RootName, Key);
        }
    } while (SetupFindNextLine(&Context, &Context));

    return Head;
}

static VOID
RestoreKeptSettings(
    _In_opt_ PKEPT_RECORD Head)
{
    PKEPT_RECORD Record, Next;
    HKEY hKey;

    for (Record = Head; Record; Record = Next)
    {
        Next = Record->Next;
        if (Record->Tree)
        {
            RegDeleteTreeW(Record->Root, Record->Key);
            RegDeleteKeyW(Record->Root, Record->Key);
        }
        else if (!Record->HasValue || Record->Present)
        {
            if (RegCreateKeyExW(Record->Root, Record->Key, 0, NULL, 0, KEY_SET_VALUE, NULL, &hKey, NULL) == ERROR_SUCCESS)
            {
                if (Record->HasValue)
                    RegSetValueExW(hKey, Record->Value, 0, Record->Type, Record->Data, Record->Size);
                RegCloseKey(hKey);
            }
        }
        else if (RegOpenKeyExW(Record->Root, Record->Key, 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS)
        {
            RegDeleteValueW(hKey, Record->Value);
            RegCloseKey(hKey);
        }

        if (Record->Data)
            HeapFree(ProcessHeap, 0, Record->Data);
        HeapFree(ProcessHeap, 0, Record);
    }
}

static DWORD
ImportRegistryInf(
    _In_ PCWSTR InfPath,
    _In_ PCWSTR Action,
    _In_ PCWSTR Section)
{
    WCHAR Stub[MAX_PATH];
    CHAR Text[512];
    HANDLE File;
    DWORD Written, Error;
    HINF hInf;

    StagePath(L"stub.inf", Stub);
    if (FAILED(StringCchPrintfA(Text, ARRAYSIZE(Text),
                                "[Version]\r\nSignature=\"$Windows NT$\"\r\n\r\n[%S]\r\n%S=%S\r\n",
                                COMPLETE_SECTION, Action, Section)))
    {
        return ERROR_BUFFER_OVERFLOW;
    }

    File = CreateFileW(Stub, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (File == INVALID_HANDLE_VALUE)
        return GetLastError();
    Error = WriteFile(File, Text, (DWORD)strlen(Text), &Written, NULL) ? ERROR_SUCCESS : GetLastError();
    CloseHandle(File);
    if (Error != ERROR_SUCCESS)
        return Error;

    hInf = SetupOpenInfFileW(Stub, NULL, INF_STYLE_WIN4, NULL);
    if (hInf == INVALID_HANDLE_VALUE)
        return GetLastError();
    Error = (SetupOpenAppendInfFileW(InfPath, hInf, NULL) &&
             SetupInstallFromInfSectionW(NULL, hInf, COMPLETE_SECTION, SPINST_REGISTRY, NULL, NULL, 0,
                                         NULL, NULL, NULL, NULL)) ? ERROR_SUCCESS : GetLastError();
    SetupCloseInfFile(hInf);
    DeleteFileW(Stub);
    return Error;
}

static DWORD
MigrateRegistry(
    _In_ PCWSTR Image,
    _In_ HINF hSetup)
{
    WCHAR Action[32], File[MAX_PATH], Section[128], Name[MAX_PATH], Path[MAX_PATH];
    PKEPT_RECORD Kept;
    INFCONTEXT Context;
    HKEY DefaultUser;
    DWORD Error = ERROR_SUCCESS;

    Error = RegOpenKeyExW(HKEY_USERS, L".DEFAULT", 0, KEY_ALL_ACCESS, &DefaultUser);
    if (Error != ERROR_SUCCESS)
        return Error;

    Kept = SaveKeptSettings(hSetup, DefaultUser);
    RegOverridePredefKey(HKEY_CURRENT_USER, DefaultUser);
    StagePath(L"registry.tmp", Path);
    if (SetupFindFirstLineW(hSetup, L"HiveInfs.Upgrade", NULL, &Context))
    {
        do
        {
            if (!SetupGetStringFieldW(&Context, 0, Action, ARRAYSIZE(Action), NULL) ||
                !SetupGetStringFieldW(&Context, 1, File, ARRAYSIZE(File), NULL) ||
                !SetupGetStringFieldW(&Context, 2, Section, ARRAYSIZE(Section), NULL) ||
                FAILED(StringCchPrintfW(Name, ARRAYSIZE(Name), L"%s\\%s", GetArchitectureName(), File)))
            {
                continue;
            }

            Error = ExtractImageFile(Image, Name, Path);
            if (Error == ERROR_SUCCESS)
                Error = ImportRegistryInf(Path, Action, Section);
            DeleteFileW(Path);
            DPRINT1("Upgrade registry %S = %S,%S: %lu\n", Action, File, Section, Error);
        } while (Error == ERROR_SUCCESS && SetupFindNextLine(&Context, &Context));
    }
    RegOverridePredefKey(HKEY_CURRENT_USER, NULL);
    RestoreKeptSettings(Kept);
    RegCloseKey(DefaultUser);
    return Error;
}

static DWORD
RunTargetConfiguration(VOID)
{
    SECURITY_ATTRIBUTES Attributes = { sizeof(Attributes), NULL, TRUE };
    WCHAR Application[MAX_PATH], CommandLine[MAX_PATH + 16], SystemRoot[MAX_PATH], Volume[MAX_PATH], Drive[3];
    WCHAR Settings[1024];
    HANDLE InputRead = NULL, InputWrite = NULL, Null;
    STARTUPINFOW Startup;
    PROCESS_INFORMATION Process;
    DWORD Written, ExitCode;

    if (!GetSystemDirectoryW(Application, ARRAYSIZE(Application)) ||
        FAILED(StringCchCatW(Application, ARRAYSIZE(Application), L"\\setup.exe")) ||
        FAILED(StringCchPrintfW(CommandLine, ARRAYSIZE(CommandLine), L"\"%s\" -target", Application)) ||
        !GetWindowsDirectoryW(SystemRoot, ARRAYSIZE(SystemRoot)))
    {
        return ERROR_BUFFER_OVERFLOW;
    }

    StringCchCopyNW(Drive, ARRAYSIZE(Drive), SystemRoot, 2);
    if (!QueryDosDeviceW(Drive, Volume, ARRAYSIZE(Volume)) ||
        FAILED(StringCchPrintfW(Settings, ARRAYSIZE(Settings),
                                L"MachineKey=\\Registry\\Machine\nUserKey=\\Registry\\User\nSystemVolume=%s\n"
                                L"SystemDrive=%s\nSystemRoot=%s\nComputerName=\nUserName=\nPassword=\n"
                                L"TimeZoneIndex=0\nAutoDaylight=1\nComponents=0\nUpgrade=1\n",
                                Volume, Drive, SystemRoot)))
    {
        return ERROR_PATH_NOT_FOUND;
    }

    Null = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &Attributes, OPEN_EXISTING, 0, NULL);
    if (!CreatePipe(&InputRead, &InputWrite, &Attributes, 0) ||
        !SetHandleInformation(InputWrite, HANDLE_FLAG_INHERIT, 0))
    {
        ExitCode = GetLastError();
        if (InputRead)
            CloseHandle(InputRead);
        if (InputWrite)
            CloseHandle(InputWrite);
        if (Null != INVALID_HANDLE_VALUE)
            CloseHandle(Null);
        return ExitCode;
    }

    ZeroMemory(&Startup, sizeof(Startup));
    Startup.cb = sizeof(Startup);
    Startup.dwFlags = STARTF_USESTDHANDLES;
    Startup.hStdInput = InputRead;
    Startup.hStdOutput = Null;
    Startup.hStdError = Null;
    ExitCode = CreateProcessW(Application, CommandLine, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL,
                              &Startup, &Process) ? ERROR_SUCCESS : GetLastError();
    CloseHandle(InputRead);
    if (Null != INVALID_HANDLE_VALUE)
        CloseHandle(Null);

    if (ExitCode == ERROR_SUCCESS)
    {
        WriteFile(InputWrite, Settings, (DWORD)(wcslen(Settings) * sizeof(WCHAR)), &Written, NULL);
        CloseHandle(InputWrite);
        InputWrite = NULL;
        WaitForSingleObject(Process.hProcess, INFINITE);
        if (!GetExitCodeProcess(Process.hProcess, &ExitCode))
            ExitCode = GetLastError();
        CloseHandle(Process.hThread);
        CloseHandle(Process.hProcess);
    }
    if (InputWrite)
        CloseHandle(InputWrite);
    return ExitCode;
}

static PCWSTR
GetRemovableBootName(VOID)
{
    SYSTEM_INFO Info;

    GetNativeSystemInfo(&Info);
    switch (Info.wProcessorArchitecture)
    {
        case PROCESSOR_ARCHITECTURE_AMD64: return L"efi\\boot\\bootx64.efi";
        case PROCESSOR_ARCHITECTURE_ARM64: return L"efi\\boot\\bootaa64.efi";
        case PROCESSOR_ARCHITECTURE_INTEL: return L"efi\\boot\\bootia32.efi";
        case PROCESSOR_ARCHITECTURE_RISCV64: return L"efi\\boot\\bootriscv64.efi";
        default: return NULL;
    }
}

static BOOL
ReplaceVerified(
    _In_ PCWSTR Source,
    _In_ PCWSTR Target)
{
    WCHAR Temporary[MAX_PATH];
    BOOL Replaced;

    if (FAILED(StringCchPrintfW(Temporary, ARRAYSIZE(Temporary), L"%s.new", Target)))
        return FALSE;
    Replaced = CopyFileW(Source, Temporary, FALSE) && SameFileContents(Source, Temporary) &&
               MoveFileExW(Temporary, Target, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    if (!Replaced)
        DeleteFileW(Temporary);
    return Replaced;
}

static VOID
RefreshBootManager(
    _In_ PCWSTR Image)
{
    WCHAR SystemPartition[MAX_PATH], Manager[MAX_PATH], Fallback[MAX_PATH], Extracted[MAX_PATH];
    PCWSTR Source = GetRemovableBootName();
    DWORD Size = sizeof(SystemPartition);
    BOOL FallbackIsOurs;

    if (!Source ||
        RegGetValueW(HKEY_LOCAL_MACHINE, SETUP_KEY, L"SystemPartition", RRF_RT_REG_SZ, NULL, SystemPartition,
                     &Size) != ERROR_SUCCESS ||
        FAILED(StringCchPrintfW(Manager, ARRAYSIZE(Manager), L"\\\\?\\GLOBALROOT%s\\EFI\\LiberNT\\Boot\\bootmgfw.efi",
                                SystemPartition)) ||
        FAILED(StringCchPrintfW(Fallback, ARRAYSIZE(Fallback), L"\\\\?\\GLOBALROOT%s\\%s", SystemPartition, Source)) ||
        GetFileAttributesW(Manager) == INVALID_FILE_ATTRIBUTES)
    {
        return;
    }

    StagePath(L"bootmgr.tmp", Extracted);
    if (ExtractImageFile(Image, Source, Extracted) == ERROR_SUCCESS)
    {
        FallbackIsOurs = SameFileContents(Manager, Fallback);
        DPRINT1("Boot manager refreshed: %d, fallback %d\n",
                ReplaceVerified(Extracted, Manager),
                FallbackIsOurs && ReplaceVerified(Extracted, Fallback));
    }
    DeleteFileW(Extracted);
}

static DWORD
CompleteUpdate(
    _In_opt_ HWND hwndNotify)
{
    WCHAR Image[MAX_PATH], SystemMedia[MAX_PATH];
    HINF hSetup;
    DWORD Error = ERROR_SUCCESS;
    BOOLEAN Enabled;

    RtlAdjustPrivilege(SE_BACKUP_PRIVILEGE, TRUE, FALSE, &Enabled);
    RtlAdjustPrivilege(SE_RESTORE_PRIVILEGE, TRUE, FALSE, &Enabled);

    StagePath(UPDATE_STAGE_IMAGE, Image);
    hSetup = OpenImageSetupInf(Image, SystemMedia);
    if (hSetup == INVALID_HANDLE_VALUE || !SystemMedia[0] || !IsImageInstalled(Image, SystemMedia))
    {
        DPRINT1("The staged image %S is not the installed one\n", Image);
        Error = ERROR_INSTALL_FAILURE;
    }

    if (Error == ERROR_SUCCESS)
    {
        if (hwndNotify)
            PostMessageW(hwndNotify, PM_COMPLETE_STEP, IDS_UPDATE_COMPLETE_REGISTRY, 0);
        Error = MigrateRegistry(Image, hSetup);
        DPRINT1("Upgrade registry migration finished with %lu\n", Error);
    }
    if (Error == ERROR_SUCCESS)
    {
        if (hwndNotify)
            PostMessageW(hwndNotify, PM_COMPLETE_STEP, IDS_UPDATE_COMPLETE_COMPONENTS, 0);
        Error = RunTargetConfiguration();
        DPRINT1("Upgrade component configuration finished with %lu\n", Error);
    }
    if (Error == ERROR_SUCCESS)
    {
        RefreshBootManager(Image);
        Error = WriteStagedValue(L"Completed", L"1");
    }
    if (hSetup != INVALID_HANDLE_VALUE)
        SetupCloseInfFile(hSetup);

    if (Error == ERROR_SUCCESS)
    {
        if (hwndNotify)
            PostMessageW(hwndNotify, PM_COMPLETE_STEP, IDS_UPDATE_COMPLETE_CLEANUP, 0);
        DPRINT1("Update staging removed with %lu\n", RemoveStage());
    }
    DPRINT1("Update completion finished with %lu\n", Error);
    return Error;
}

static DWORD
WINAPI
CompleteThread(
    LPVOID Parameter)
{
    HWND hwndDlg = (HWND)Parameter;
    DWORD Error = CompleteUpdate(hwndDlg);

    PostMessageW(hwndDlg, PM_COMPLETE_DONE, Error, 0);
    return Error;
}

static INT_PTR
CALLBACK
CompletePageProc(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam)
{
    HWND hProgress = GetDlgItem(hwndDlg, IDC_PROCESSPROGRESS);
    HANDLE hThread;
    WCHAR Text[512];

    switch (uMsg)
    {
        case SPN_SETACTIVE:
            SetupFrameShowButtons(hwndDlg, FALSE, FALSE, FALSE);
            SetWindowLongPtrW(hProgress, GWL_STYLE, GetWindowLongPtrW(hProgress, GWL_STYLE) | PBS_MARQUEE);
            SendMessageW(hProgress, PBM_SETMARQUEE, TRUE, 0);
            SetDlgItemTextW(hwndDlg, IDC_ITEM, L"");
            hThread = CreateThread(NULL, 0, CompleteThread, hwndDlg, 0, NULL);
            if (hThread)
                CloseHandle(hThread);
            else
                PostMessageW(hwndDlg, PM_COMPLETE_DONE, GetLastError(), 0);
            return TRUE;

        case PM_COMPLETE_STEP:
            SetResText(GetDlgItem(hwndDlg, IDC_ACTIVITY), (UINT)wParam);
            return TRUE;

        case PM_COMPLETE_DONE:
            SendMessageW(hProgress, PBM_SETMARQUEE, FALSE, 0);
            if (wParam != ERROR_SUCCESS)
            {
                LoadPrintfW(Text, ARRAYSIZE(Text), IDS_UPDATE_COMPLETE_RETRYLOGON, Update.TagName);
                SetupMessage(GetParent(hwndDlg), TD_WARNING_ICON, TDCBF_CLOSE_BUTTON, IDCLOSE, NULL, Text);
            }
            EndDialog(GetParent(hwndDlg), wParam == ERROR_SUCCESS ? IDOK : IDCANCEL);
            return TRUE;

        case SPN_QUERYCANCEL:
        case SPN_WIZBACK:
        case SPN_WIZNEXT:
            SetWindowLongPtrW(hwndDlg, DWLP_MSGRESULT, uMsg == SPN_QUERYCANCEL ? TRUE : -1);
            return TRUE;
    }

    return FALSE;
}

static BOOL
IsAdministrator(VOID)
{
    SID_IDENTIFIER_AUTHORITY Authority = { SECURITY_NT_AUTHORITY };
    PSID Administrators;
    BOOL Member = FALSE;

    if (!AllocateAndInitializeSid(&Authority, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS,
                                  0, 0, 0, 0, 0, 0, &Administrators))
    {
        return FALSE;
    }
    if (!CheckTokenMembership(NULL, Administrators, &Member))
        Member = FALSE;
    FreeSid(Administrators);
    return Member;
}

static BOOL
IsStagedImageInstalled(VOID)
{
    WCHAR Image[MAX_PATH], SystemMedia[MAX_PATH];
    BOOL Installed;
    HINF hSetup;

    StagePath(UPDATE_STAGE_IMAGE, Image);
    hSetup = OpenImageSetupInf(Image, SystemMedia);
    if (hSetup == INVALID_HANDLE_VALUE)
        return FALSE;
    Installed = SystemMedia[0] && IsImageInstalled(Image, SystemMedia);
    SetupCloseInfFile(hSetup);
    return Installed;
}

static BOOL
WasStartedByUpdate(VOID)
{
    WCHAR Options[1024];
    DWORD Size = sizeof(Options);

    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control", L"SystemStartOptions",
                     RRF_RT_REG_SZ, NULL, Options, &Size) != ERROR_SUCCESS)
    {
        return FALSE;
    }
    _wcsupr(Options);
    return wcsstr(Options, L"INSTALLIMAGE=") != NULL;
}

static ULONG
CountMissedStart(VOID)
{
    SYSTEM_TIMEOFDAY_INFORMATION TimeOfDay;
    WCHAR Path[MAX_PATH], Boot[32], Seen[32];
    ULONG Missed;

    StagePath(UPDATE_STAGE_REQUEST, Path);
    Missed = GetPrivateProfileIntW(UPDATE_STAGE_SECTION, L"MissedStarts", 0, Path);
    if (!NT_SUCCESS(NtQuerySystemInformation(SystemTimeOfDayInformation, &TimeOfDay, sizeof(TimeOfDay), NULL)) ||
        FAILED(StringCchPrintfW(Boot, ARRAYSIZE(Boot), L"%I64u", TimeOfDay.BootTime.QuadPart)))
    {
        return Missed;
    }

    GetPrivateProfileStringW(UPDATE_STAGE_SECTION, L"LastStart", L"", Seen, ARRAYSIZE(Seen), Path);
    if (wcscmp(Seen, Boot) != 0)
    {
        Missed++;
        StringCchPrintfW(Seen, ARRAYSIZE(Seen), L"%lu", Missed);
        WriteStagedValue(L"MissedStarts", Seen);
        WriteStagedValue(L"LastStart", Boot);
    }
    return Missed;
}

static VOID
ShowCompleteMessage(
    _In_ UINT MessageId,
    _In_ PCWSTR Icon)
{
    WCHAR Text[512];

    LoadPrintfW(Text, ARRAYSIZE(Text), MessageId, Update.TagName);
    SetupMessage(NULL, Icon, TDCBF_CLOSE_BUTTON, IDCLOSE, NULL, Text);
}

static INT
RunComplete(VOID)
{
    WCHAR Path[MAX_PATH], Stage[32], ImageHash[SHA256_CHARS];
    INT_PTR Result;
    UINT MessageId;

    if (!IsAdministrator())
    {
        DPRINT1("The update is completed at the next logon of an administrator\n");
        return ERROR_ACCESS_DENIED;
    }

    StagePath(UPDATE_STAGE_REQUEST, Path);
    GetPrivateProfileStringW(UPDATE_STAGE_SECTION, L"TagName", L"", Update.TagName, ARRAYSIZE(Update.TagName), Path);
    GetPrivateProfileStringW(UPDATE_STAGE_SECTION, L"ImageSha256", L"", ImageHash, ARRAYSIZE(ImageHash), Path);
    if (ImageHash[0] == UNICODE_NULL)
        return RemoveStage() == ERROR_SUCCESS ? ERROR_NOT_FOUND : ERROR_RETRY;
    if (GetPrivateProfileIntW(UPDATE_STAGE_SECTION, L"Completed", 0, Path) != 0)
        return RemoveStage();

    StagePath(STAGE_STATUS, Path);
    GetPrivateProfileStringW(STAGE_STATUS_SECTION, L"Stage", L"", Stage, ARRAYSIZE(Stage), Path);
    DPRINT1("The boot loader left the update at stage '%S'\n", Stage);
    if (Stage[0] == UNICODE_NULL && IsStagedImageInstalled())
        StringCchCopyW(Stage, ARRAYSIZE(Stage), L"done");
    if (Stage[0] == UNICODE_NULL)
    {
        if (WasStartedByUpdate())
        {
            DPRINT1("The update entry started but left no install status\n");
            StringCchCopyW(Stage, ARRAYSIZE(Stage), L"open");
        }
        else if (CountMissedStart() >= STAGE_MISSED_STARTS)
        {
            DPRINT1("The update entry did not run at %u starts\n", STAGE_MISSED_STARTS);
            StringCchCopyW(Stage, ARRAYSIZE(Stage), L"start");
        }
        else
        {
            return ERROR_RETRY;
        }
    }

    if (_wcsicmp(Stage, L"write") == 0 || _wcsicmp(Stage, L"rollback") == 0)
    {
        ShowCompleteMessage(IDS_UPDATE_COMPLETE_RETRYBOOT, TD_WARNING_ICON);
        return ERROR_RETRY;
    }

    if (_wcsicmp(Stage, L"done") != 0 && _wcsicmp(Stage, L"current") != 0)
    {
        if (_wcsicmp(Stage, L"space") == 0)
            MessageId = IDS_UPDATE_COMPLETE_NOSPACE;
        else if (_wcsicmp(Stage, L"checksum") == 0 || _wcsicmp(Stage, L"image") == 0)
            MessageId = IDS_UPDATE_COMPLETE_DAMAGED;
        else
            MessageId = IDS_UPDATE_COMPLETE_FAILED;
        DPRINT1("Update staging removed with %lu\n", RemoveStage());
        ShowCompleteMessage(MessageId, TD_ERROR_ICON);
        return ERROR_INSTALL_FAILURE;
    }

    Result = RunSetupFrame(&SetupData, CompletePages, ARRAYSIZE(CompletePages));
    if (Result == IDOK)
        RestartToInstall();
    return Result == IDOK ? ERROR_SUCCESS : ERROR_RETRY;
}

static VOID
RunUpdateWindow(VOID)
{
    RunSetupFrame(&SetupData, UpdatePages, ARRAYSIZE(UpdatePages));
}

static BOOL
ShouldNotify(VOID)
{
    return Update.Available &&
           !(ReadSetting(L"RemindTag", 0) == Update.TagId && CurrentTime() < ReadSettingTime(L"RemindAfter"));
}

static VOID
ShowNotification(
    _In_ HWND hwnd)
{
    NOTIFYICONDATAW Icon;

    ZeroMemory(&Icon, sizeof(Icon));
    Icon.cbSize = sizeof(Icon);
    Icon.hWnd = hwnd;
    Icon.uID = AGENT_ICON;
    Icon.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_INFO;
    Icon.uCallbackMessage = WM_AGENT_ICON;
    Icon.hIcon = LoadIconW(SetupData.hInstance, MAKEINTRESOURCEW(IDI_MAIN));
    Icon.dwInfoFlags = NIIF_INFO;
    LoadStringW(SetupData.hInstance, IDS_UPDATE_NOTIFYTITLE, Icon.szInfoTitle, ARRAYSIZE(Icon.szInfoTitle));
    LoadPrintfW(Icon.szInfo, ARRAYSIZE(Icon.szInfo),
                Update.Staged ? IDS_UPDATE_NOTIFYSTAGED : IDS_UPDATE_NOTIFYTEXT, Update.TagName);
    StringCchCopyW(Icon.szTip, ARRAYSIZE(Icon.szTip), Icon.szInfoTitle);
    if (!Shell_NotifyIconW(NIM_MODIFY, &Icon))
        Shell_NotifyIconW(NIM_ADD, &Icon);
}

static VOID
HideNotification(
    _In_ HWND hwnd)
{
    NOTIFYICONDATAW Icon;

    ZeroMemory(&Icon, sizeof(Icon));
    Icon.cbSize = sizeof(Icon);
    Icon.hWnd = hwnd;
    Icon.uID = AGENT_ICON;
    Shell_NotifyIconW(NIM_DELETE, &Icon);
}

static LRESULT
CALLBACK
AgentWindowProc(
    _In_ HWND hwnd,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam)
{
    HANDLE hThread;

    switch (uMsg)
    {
        case WM_TIMER:
            KillTimer(hwnd, AGENT_TIMER);
            SetTimer(hwnd, AGENT_TIMER, AGENT_INTERVAL * 1000, NULL);
            if (ReadSetting(L"Enabled", 1) == 0)
                return 0;
            hThread = CreateThread(NULL, 0, CheckThread, hwnd, 0, NULL);
            if (hThread)
                CloseHandle(hThread);
            return 0;

        case PM_CHECK_DONE:
            if (wParam == ERROR_SUCCESS && ShouldNotify())
                ShowNotification(hwnd);
            return 0;

        case WM_AGENT_ICON:
            if (LOWORD(lParam) == NIN_BALLOONUSERCLICK || LOWORD(lParam) == WM_LBUTTONUP ||
                LOWORD(lParam) == WM_LBUTTONDBLCLK)
            {
                HideNotification(hwnd);
                RunUpdateWindow();
            }
            return 0;

        case WM_DESTROY:
            HideNotification(hwnd);
            PostQuitMessage(0);
            return 0;
    }

    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

static INT
RunAgent(VOID)
{
    WNDCLASSW Class;
    HANDLE hMutex;
    HWND hwnd;
    MSG msg;

    if (IsLiveSystem())
        return 0;

    hMutex = CreateMutexW(NULL, FALSE, AGENT_MUTEX);
    if (!hMutex || GetLastError() == ERROR_ALREADY_EXISTS)
        return 0;

    ZeroMemory(&Class, sizeof(Class));
    Class.lpfnWndProc = AgentWindowProc;
    Class.hInstance = SetupData.hInstance;
    Class.lpszClassName = AGENT_CLASS;
    if (!RegisterClassW(&Class))
        return GetLastError();

    hwnd = CreateWindowExW(0, AGENT_CLASS, L"", 0, 0, 0, 0, 0, NULL, NULL, SetupData.hInstance, NULL);
    if (!hwnd)
        return GetLastError();
    SetTimer(hwnd, AGENT_TIMER, ReadSetting(L"FirstCheckDelay", AGENT_FIRST_DELAY) * 1000, NULL);

    while (GetMessageW(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    CloseHandle(hMutex);
    return 0;
}

static INT
RunInstall(VOID)
{
    DWORD Error;

    Error = CheckForUpdate(&Update);
    if (Error != ERROR_SUCCESS)
        return Error;
    if (!Update.Available)
        return ERROR_NOT_FOUND;

    Stage.hwndNotify = NULL;
    Stage.Cancel = 0;
    Error = StageThread(&Stage);
    if (Error == ERROR_SUCCESS)
        RestartToInstall();
    return Error;
}

int WINAPI
wWinMain(
    HINSTANCE hInst,
    HINSTANCE hPrevInstance,
    LPWSTR lpszCmdLine,
    int nCmdShow)
{
    INITCOMMONCONTROLSEX iccx;
    WCHAR Path[MAX_PATH], Drive[MAX_PATH];
    HRESULT hrCom;
    INT Result = 0;

    ProcessHeap = GetProcessHeap();
    SetupData.hInstance = hInst;

    if (GetWindowsDirectoryW(Path, ARRAYSIZE(Path)) &&
        SUCCEEDED(StringCchCatW(Path, ARRAYSIZE(Path), L"\\" UPGRADE_INVENTORY_PATH)))
    {
        LocalBuild = ReadInventoryBuild(Path);
    }
    if (!GetEnvironmentVariableW(L"SystemDrive", Drive, ARRAYSIZE(Drive)))
        StringCchCopyW(Drive, ARRAYSIZE(Drive), L"C:");
    StringCchPrintfW(StageDirectory, ARRAYSIZE(StageDirectory), L"%s\\%s", Drive, UPDATE_STAGE_DIRECTORY);

    iccx.dwSize = sizeof(iccx);
    iccx.dwICC = ICC_PROGRESS_CLASS;
    InitCommonControlsEx(&iccx);
    hrCom = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    if (wcsstr(lpszCmdLine, L"/agent"))
        Result = RunAgent();
    else if (wcsstr(lpszCmdLine, L"/install"))
        Result = RunInstall();
    else if (wcsstr(lpszCmdLine, L"/complete"))
        Result = RunComplete();
    else
        RunUpdateWindow();

    if (SUCCEEDED(hrCom))
        CoUninitialize();
    return Result;
}

/* EOF */
