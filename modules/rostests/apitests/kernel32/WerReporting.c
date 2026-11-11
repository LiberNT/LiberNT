/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for Windows Error Reporting: report API and store, crash reports, hang recovery
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <winuser.h>
#include <winver.h>
#include <dbghelp.h>
#include <werapi.h>
#include <errorrep.h>
#include "werhandler/werhandler.h"

#define CHILD_EXIT_RETURNED     0x600D
#define CHILD_SAFETY_MS         10000
#define HANG_EXIT_STATUS        0xCFFFFFFF
#define BLOCK_SIZE              4096
#define HELPERS_KEY             L"Software\\Microsoft\\Windows\\Windows Error Reporting\\RuntimeExceptionHelperModules"
#define LOCAL_DUMPS_KEY         L"Software\\Microsoft\\Windows\\Windows Error Reporting\\LocalDumps"
#define LOCAL_DUMP_FLAGS        (MiniDumpWithDataSegs | MiniDumpWithUnloadedModules | MiniDumpWithProcessThreadData)

typedef HRESULT (WINAPI *FN_WerReportCreate)(PCWSTR, WER_REPORT_TYPE, PWER_REPORT_INFORMATION, HREPORT *);
typedef HRESULT (WINAPI *FN_WerReportCloseHandle)(HREPORT);
typedef HRESULT (WINAPI *FN_WerReportSetParameter)(HREPORT, DWORD, PCWSTR, PCWSTR);
typedef HRESULT (WINAPI *FN_WerReportSetUIOption)(HREPORT, WER_REPORT_UI, PCWSTR);
typedef HRESULT (WINAPI *FN_WerReportAddFile)(HREPORT, PCWSTR, WER_FILE_TYPE, DWORD);
typedef HRESULT (WINAPI *FN_WerReportAddDump)(HREPORT, HANDLE, HANDLE, WER_DUMP_TYPE, PWER_EXCEPTION_INFORMATION,
                                              PWER_DUMP_CUSTOM_OPTIONS, DWORD);
typedef HRESULT (WINAPI *FN_WerReportSubmit)(HREPORT, WER_CONSENT, DWORD, PWER_SUBMIT_RESULT);
typedef HRESULT (WINAPI *FN_WerAddExcludedApplication)(PCWSTR, BOOL);
typedef HRESULT (WINAPI *FN_WerRemoveExcludedApplication)(PCWSTR, BOOL);
typedef HRESULT (*FN_WerStoreOpen)(REPORT_STORE_TYPES, PHREPORTSTORE);
typedef VOID (*FN_WerStoreClose)(HREPORTSTORE);
typedef HRESULT (*FN_WerStoreGetReportKey)(HREPORTSTORE, PCWSTR *);
typedef HRESULT (*FN_WerStoreQueryReportMetadataV2)(HREPORTSTORE, PCWSTR, PWER_REPORT_METADATA_V2);
typedef HRESULT (*FN_WerStoreGetReportCount)(HREPORTSTORE, DWORD *);
typedef HRESULT (*FN_WerStoreGetSizeOnDisk)(HREPORTSTORE, ULONGLONG *);
typedef VOID (*FN_WerFreeString)(PCWSTR);
typedef HRESULT (WINAPI *FN_WerReportHang)(HWND, PCWSTR);
typedef HRESULT (WINAPI *FN_WerSetFlags)(DWORD);
typedef HRESULT (WINAPI *FN_WerRegisterFile)(PCWSTR, WER_REGISTER_FILE_TYPE, DWORD);
typedef HRESULT (WINAPI *FN_WerUnregisterFile)(PCWSTR);
typedef HRESULT (WINAPI *FN_WerRegisterMemoryBlock)(PVOID, DWORD);
typedef HRESULT (WINAPI *FN_WerRegisterRuntimeExceptionModule)(PCWSTR, PVOID);
typedef HRESULT (WINAPI *FN_RegisterApplicationRecoveryCallback)(APPLICATION_RECOVERY_CALLBACK, PVOID, DWORD, DWORD);
typedef HRESULT (WINAPI *FN_ApplicationRecoveryInProgress)(PBOOL);
typedef VOID (WINAPI *FN_ApplicationRecoveryFinished)(BOOL);
typedef BOOL (WINAPI *FN_MiniDumpReadDumpStream)(PVOID, ULONG, PMINIDUMP_DIRECTORY *, PVOID *, ULONG *);

static FN_WerReportCreate pWerReportCreate;
static FN_WerReportCloseHandle pWerReportCloseHandle;
static FN_WerReportSetParameter pWerReportSetParameter;
static FN_WerReportSetUIOption pWerReportSetUIOption;
static FN_WerReportAddFile pWerReportAddFile;
static FN_WerReportAddDump pWerReportAddDump;
static FN_WerReportSubmit pWerReportSubmit;
static FN_WerAddExcludedApplication pWerAddExcludedApplication;
static FN_WerRemoveExcludedApplication pWerRemoveExcludedApplication;
static FN_WerStoreOpen pWerStoreOpen;
static FN_WerStoreClose pWerStoreClose;
static FN_WerStoreGetReportKey pWerStoreGetFirstReportKey;
static FN_WerStoreGetReportKey pWerStoreGetNextReportKey;
static FN_WerStoreQueryReportMetadataV2 pWerStoreQueryReportMetadataV2;
static FN_WerStoreGetReportCount pWerStoreGetReportCount;
static FN_WerStoreGetSizeOnDisk pWerStoreGetSizeOnDisk;
static FN_WerFreeString pWerFreeString;
static FN_WerReportHang pWerReportHang;
static FN_WerSetFlags pWerSetFlags;
static FN_WerRegisterFile pWerRegisterFile;
static FN_WerUnregisterFile pWerUnregisterFile;
static FN_WerRegisterMemoryBlock pWerRegisterMemoryBlock;
static FN_WerRegisterRuntimeExceptionModule pWerRegisterRuntimeExceptionModule;
static FN_RegisterApplicationRecoveryCallback pRegisterApplicationRecoveryCallback;
static FN_ApplicationRecoveryInProgress pApplicationRecoveryInProgress;
static FN_ApplicationRecoveryFinished pApplicationRecoveryFinished;
static FN_MiniDumpReadDumpStream pMiniDumpReadDumpStream;

typedef struct _FOUND_REPORT
{
    WER_REPORT_METADATA_V2 Metadata;
    WCHAR Names[1024];
    WCHAR Directory[MAX_PATH];
} FOUND_REPORT, *PFOUND_REPORT;

typedef struct _CRASH_SHARED
{
    WER_HANDLER_RESULT Handler;
    ULONG64 ContextAddress;
    ULONG64 BlockAddress;
    ULONG CrashThreadId;
    HRESULT SetFlags;
    HRESULT RegisterKeep;
    HRESULT RegisterDelete;
    HRESULT RegisterBlock;
    HRESULT RegisterUnlisted;
    HRESULT RegisterHandler;
} CRASH_SHARED, *PCRASH_SHARED;

typedef struct _HANG_SHARED
{
    ULONG64 Window;
    HRESULT Register;
    HRESULT SetFlags;
    LONG CallbackCalls;
    ULONG64 Parameter;
    HRESULT InProgress;
    BOOL Cancelled;
    HRESULT InProgressAfterClick;
    BOOL CancelledAfterClick;
    LONG AfterWait;
} HANG_SHARED, *PHANG_SHARED;

static WER_HANDLER_CONTEXT HandlerContext;
static PHANG_SHARED HangShared;
static HANDLE HangStarted, HangClicked, HangNever;
static LONG *volatile CrashPointer;

#define LOAD(Module, Name) (p##Name = (void *)GetProcAddress(Module, #Name))

static
BOOL
LoadFunctions(VOID)
{
    HMODULE Wer = LoadLibraryW(L"wer.dll");
    HMODULE FaultRep = LoadLibraryW(L"faultrep.dll");
    HMODULE Kernel32 = GetModuleHandleW(L"kernel32.dll");
    HMODULE DbgHelp = LoadLibraryW(L"dbghelp.dll");

    if (Wer)
    {
        LOAD(Wer, WerReportCreate);
        LOAD(Wer, WerReportCloseHandle);
        LOAD(Wer, WerReportSetParameter);
        LOAD(Wer, WerReportSetUIOption);
        LOAD(Wer, WerReportAddFile);
        LOAD(Wer, WerReportAddDump);
        LOAD(Wer, WerReportSubmit);
        LOAD(Wer, WerAddExcludedApplication);
        LOAD(Wer, WerRemoveExcludedApplication);
        LOAD(Wer, WerStoreOpen);
        LOAD(Wer, WerStoreClose);
        LOAD(Wer, WerStoreGetFirstReportKey);
        LOAD(Wer, WerStoreGetNextReportKey);
        LOAD(Wer, WerStoreQueryReportMetadataV2);
        LOAD(Wer, WerStoreGetReportCount);
        LOAD(Wer, WerStoreGetSizeOnDisk);
        LOAD(Wer, WerFreeString);
    }
    if (FaultRep)
        LOAD(FaultRep, WerReportHang);
    if (DbgHelp)
        LOAD(DbgHelp, MiniDumpReadDumpStream);
    LOAD(Kernel32, WerSetFlags);
    LOAD(Kernel32, WerRegisterFile);
    LOAD(Kernel32, WerUnregisterFile);
    LOAD(Kernel32, WerRegisterMemoryBlock);
    LOAD(Kernel32, WerRegisterRuntimeExceptionModule);
    LOAD(Kernel32, RegisterApplicationRecoveryCallback);
    LOAD(Kernel32, ApplicationRecoveryInProgress);
    LOAD(Kernel32, ApplicationRecoveryFinished);

    return pWerReportCreate && pWerReportCloseHandle && pWerReportSetParameter && pWerReportSetUIOption &&
           pWerReportAddFile && pWerReportAddDump && pWerReportSubmit && pWerAddExcludedApplication &&
           pWerRemoveExcludedApplication && pWerSetFlags && pWerRegisterFile && pWerUnregisterFile &&
           pWerRegisterMemoryBlock && pWerRegisterRuntimeExceptionModule && pRegisterApplicationRecoveryCallback &&
           pApplicationRecoveryInProgress && pApplicationRecoveryFinished && pMiniDumpReadDumpStream;
}

static
BOOL
StoreAvailable(VOID)
{
    if (pWerStoreOpen && pWerStoreClose && pWerStoreGetFirstReportKey && pWerStoreGetNextReportKey &&
        pWerStoreQueryReportMetadataV2 && pWerStoreGetReportCount && pWerStoreGetSizeOnDisk && pWerFreeString)
    {
        return TRUE;
    }

    skip("WER report store exports are missing\n");
    return FALSE;
}

static
BOOL
HasName(
    _In_ PCWSTR Names,
    _In_ PCWSTR Name)
{
    for (; *Names; Names += wcslen(Names) + 1)
    {
        if (!_wcsicmp(Names, Name))
            return TRUE;
    }

    return FALSE;
}

static
BOOL
StoreDirectory(
    _In_ REPORT_STORE_TYPES Type,
    _In_ PCWSTR Key,
    _Out_writes_(MAX_PATH) PWSTR Directory)
{
    WCHAR Root[MAX_PATH];

    if (!ExpandEnvironmentStringsW(Type == E_STORE_MACHINE_QUEUE ? L"%ProgramData%" : L"%LOCALAPPDATA%",
                                   Root, ARRAYSIZE(Root)) || Root[0] == L'%')
    {
        return FALSE;
    }

    return SUCCEEDED(StringCchPrintfW(Directory, MAX_PATH, L"%s\\Microsoft\\Windows\\WER\\ReportQueue\\%s", Root, Key));
}

static
BOOL
FindReport(
    _In_ PCWSTR EventName,
    _In_opt_ PCWSTR ApplicationName,
    _In_ const FILETIME *NotBefore,
    _Out_ PFOUND_REPORT Found)
{
    static const REPORT_STORE_TYPES Stores[] = { E_STORE_MACHINE_QUEUE, E_STORE_USER_QUEUE };
    WER_REPORT_METADATA_V2 Metadata;
    HREPORTSTORE Store;
    PCWSTR Key;
    BOOL Match = FALSE;
    ULONG Index;
    HRESULT hr;

    RtlZeroMemory(Found, sizeof(*Found));
    if (!pWerStoreOpen || !pWerStoreClose || !pWerStoreGetFirstReportKey || !pWerStoreGetNextReportKey ||
        !pWerStoreQueryReportMetadataV2 || !pWerFreeString)
    {
        return FALSE;
    }

    for (Index = 0; Index < ARRAYSIZE(Stores); Index++)
    {
        if (FAILED(pWerStoreOpen(Stores[Index], &Store)))
            continue;

        for (hr = pWerStoreGetFirstReportKey(Store, &Key); SUCCEEDED(hr) && Key;
             hr = pWerStoreGetNextReportKey(Store, &Key))
        {
            RtlZeroMemory(&Metadata, sizeof(Metadata));
            hr = pWerStoreQueryReportMetadataV2(Store, Key, &Metadata);
            if ((hr == S_OK || hr == HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER)) &&
                !wcscmp(Metadata.Signature.EventName, EventName) &&
                CompareFileTime(&Metadata.CreationTime, NotBefore) >= 0 &&
                (!ApplicationName || !_wcsicmp(Metadata.Signature.Parameters[0].Value, ApplicationName)) &&
                (!Match || CompareFileTime(&Metadata.CreationTime, &Found->Metadata.CreationTime) > 0) &&
                Metadata.SizeOfFileNames <= ARRAYSIZE(Found->Names))
            {
                Found->Metadata = Metadata;
                Found->Metadata.FileNames = Found->Names;
                RtlZeroMemory(Found->Names, sizeof(Found->Names));
                if (Metadata.SizeOfFileNames)
                    ok_hex(hr, HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER));
                hr = pWerStoreQueryReportMetadataV2(Store, Key, &Found->Metadata);
                ok_hex(hr, S_OK);
                Match = StoreDirectory(Stores[Index], Key, Found->Directory);
            }
            pWerFreeString(Key);
        }

        pWerStoreClose(Store);
    }

    return Match;
}

static
VOID
DeleteReport(
    _In_ PFOUND_REPORT Found)
{
    WCHAR Pattern[MAX_PATH], File[MAX_PATH];
    WIN32_FIND_DATAW Data;
    HANDLE Find;

    if (!Found->Directory[0] || FAILED(StringCchPrintfW(Pattern, ARRAYSIZE(Pattern), L"%s\\*", Found->Directory)))
        return;

    Find = FindFirstFileW(Pattern, &Data);
    if (Find != INVALID_HANDLE_VALUE)
    {
        do
        {
            if (!(Data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                SUCCEEDED(StringCchPrintfW(File, ARRAYSIZE(File), L"%s\\%s", Found->Directory, Data.cFileName)))
            {
                DeleteFileW(File);
            }
        } while (FindNextFileW(Find, &Data));
        FindClose(Find);
    }

    RemoveDirectoryW(Found->Directory);
}

static
PVOID
MapReportFile(
    _In_ PFOUND_REPORT Found,
    _In_ PCWSTR Name,
    _Out_ HANDLE *File,
    _Out_ HANDLE *Mapping)
{
    WCHAR Path[MAX_PATH];
    PVOID View = NULL;

    *Mapping = NULL;
    *File = INVALID_HANDLE_VALUE;
    if (FAILED(StringCchPrintfW(Path, ARRAYSIZE(Path), L"%s\\%s", Found->Directory, Name)))
        return NULL;

    *File = CreateFileW(Path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (*File == INVALID_HANDLE_VALUE)
        return NULL;

    *Mapping = CreateFileMappingW(*File, NULL, PAGE_READONLY, 0, 0, NULL);
    if (*Mapping)
        View = MapViewOfFile(*Mapping, FILE_MAP_READ, 0, 0, 0);
    return View;
}

static
VOID
UnmapReportFile(
    _In_opt_ PVOID View,
    _In_ HANDLE File,
    _In_opt_ HANDLE Mapping)
{
    if (View)
        UnmapViewOfFile(View);
    if (Mapping)
        CloseHandle(Mapping);
    if (File != INVALID_HANDLE_VALUE)
        CloseHandle(File);
}

static
PMINIDUMP_MEMORY_DESCRIPTOR
FindDumpRange(
    _In_ PVOID Dump,
    _In_ ULONG64 Address)
{
    PMINIDUMP_MEMORY_LIST List;
    PMINIDUMP_DIRECTORY Directory;
    ULONG Size, Index;

    if (!pMiniDumpReadDumpStream(Dump, MemoryListStream, &Directory, (PVOID *)&List, &Size))
        return NULL;

    for (Index = 0; Index < List->NumberOfMemoryRanges; Index++)
    {
        if (Address >= List->MemoryRanges[Index].StartOfMemoryRange &&
            Address - List->MemoryRanges[Index].StartOfMemoryRange < List->MemoryRanges[Index].Memory.DataSize)
        {
            return &List->MemoryRanges[Index];
        }
    }

    return NULL;
}

static
BOOL
WriteTestFile(
    _In_ PCWSTR Path,
    _In_ PCSTR Text)
{
    HANDLE File;
    DWORD Written;
    BOOL Success;

    File = CreateFileW(Path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (File == INVALID_HANDLE_VALUE)
        return FALSE;

    Success = WriteFile(File, Text, (DWORD)strlen(Text), &Written, NULL);
    CloseHandle(File);
    return Success;
}

static
BOOL
ReportFileEquals(
    _In_ PFOUND_REPORT Found,
    _In_ PCWSTR Name,
    _In_ PCSTR Text)
{
    HANDLE File, Mapping;
    PVOID View;
    BOOL Equal;

    View = MapReportFile(Found, Name, &File, &Mapping);
    Equal = View && GetFileSize(File, NULL) == strlen(Text) && !memcmp(View, Text, strlen(Text));
    UnmapReportFile(View, File, Mapping);
    return Equal;
}

static
VOID
TestReportHandles(VOID)
{
    WCHAR LongName[WER_MAX_EVENT_NAME_LENGTH + 2];
    HREPORT Report, Table[4];
    ULONG Index;
    HRESULT hr;

    Report = (HREPORT)(ULONG_PTR)0xdeadbeef;
    hr = pWerReportCreate(L"APPCRASH", WerReportCritical, NULL, &Report);
    ok_hex(hr, S_OK);
    ok(Report != NULL, "Report is NULL\n");
    hr = pWerReportCloseHandle(Report);
    ok_hex(hr, S_OK);
    hr = pWerReportCloseHandle(Report);
    ok_hex(hr, E_INVALIDARG);
    hr = pWerReportCloseHandle(NULL);
    ok_hex(hr, E_INVALIDARG);

    hr = pWerReportCreate(L"APPCRASH", WerReportCritical, NULL, NULL);
    ok_hex(hr, E_INVALIDARG);
    Report = (HREPORT)(ULONG_PTR)0xdeadbeef;
    hr = pWerReportCreate(NULL, WerReportCritical, NULL, &Report);
    ok_hex(hr, E_INVALIDARG);
    ok(Report == NULL, "Report %p\n", Report);
    Report = (HREPORT)(ULONG_PTR)0xdeadbeef;
    hr = pWerReportCreate(L"", WerReportCritical, NULL, &Report);
    ok_hex(hr, E_INVALIDARG);
    ok(Report == NULL, "Report %p\n", Report);
    hr = pWerReportCreate(L"APPCRASH", WerReportInvalid, NULL, &Report);
    ok_hex(hr, E_INVALIDARG);
    hr = pWerReportCreate(L"APPCRASH", 42, NULL, &Report);
    ok_hex(hr, E_INVALIDARG);

    for (Index = 0; Index < WerReportInvalid; Index++)
    {
        hr = pWerReportCreate(L"APPCRASH", Index, NULL, &Report);
        ok(hr == S_OK, "type %lu: 0x%lx\n", Index, hr);
        hr = pWerReportCloseHandle(Report);
        ok(hr == S_OK, "type %lu: close 0x%lx\n", Index, hr);
    }

    for (Index = 0; Index < ARRAYSIZE(Table); Index++)
    {
        hr = pWerReportCreate(L"APPCRASH", WerReportCritical, NULL, &Table[Index]);
        ok(hr == S_OK, "report %lu: 0x%lx\n", Index, hr);
    }
    while (Index--)
    {
        hr = pWerReportCloseHandle(Table[Index]);
        ok(hr == S_OK, "close %lu: 0x%lx\n", Index, hr);
    }

    for (Index = 0; Index < WER_MAX_EVENT_NAME_LENGTH; Index++)
        LongName[Index] = L'a' + (WCHAR)(Index % 26);
    LongName[WER_MAX_EVENT_NAME_LENGTH] = UNICODE_NULL;
    hr = pWerReportCreate(LongName, WerReportNonCritical, NULL, &Report);
    ok_hex(hr, S_OK);
    if (SUCCEEDED(hr))
        pWerReportCloseHandle(Report);
    LongName[WER_MAX_EVENT_NAME_LENGTH] = L'z';
    LongName[WER_MAX_EVENT_NAME_LENGTH + 1] = UNICODE_NULL;
    hr = pWerReportCreate(LongName, WerReportNonCritical, NULL, &Report);
    ok_hex(hr, WER_E_LENGTH_EXCEEDED);
}

static
VOID
TestReportArguments(
    _In_ PCWSTR TempFile)
{
    WCHAR Text[MAX_PATH + 2];
    HREPORT Report;
    ULONG Index;
    HRESULT hr;

    hr = pWerReportCreate(L"LntWerArguments", WerReportNonCritical, NULL, &Report);
    ok_hex(hr, S_OK);
    if (FAILED(hr))
        return;

    hr = pWerReportSetParameter(NULL, WER_P0, L"Name", L"Value");
    ok_hex(hr, E_HANDLE);
    hr = pWerReportSetParameter(Report, WER_P0, L"Name", L"Value");
    ok_hex(hr, S_OK);
    hr = pWerReportSetParameter(Report, WER_P9, NULL, L"Value");
    ok_hex(hr, S_OK);
    hr = pWerReportSetParameter(Report, WER_MAX_PARAM_COUNT, L"Name", L"Value");
    ok_hex(hr, E_INVALIDARG);
    hr = pWerReportSetParameter(Report, WER_P1, L"Name", NULL);
    ok_hex(hr, E_INVALIDARG);

    for (Index = 0; Index < ARRAYSIZE(Text) - 1; Index++)
        Text[Index] = L'n';
    Text[WER_MAX_SIGNATURE_NAME_LENGTH] = UNICODE_NULL;
    hr = pWerReportSetParameter(Report, WER_P1, Text, L"Value");
    ok_hex(hr, S_OK);
    Text[WER_MAX_SIGNATURE_NAME_LENGTH] = L'n';
    Text[WER_MAX_SIGNATURE_NAME_LENGTH + 1] = UNICODE_NULL;
    hr = pWerReportSetParameter(Report, WER_P1, Text, L"Value");
    ok_hex(hr, WER_E_LENGTH_EXCEEDED);
    Text[WER_MAX_SIGNATURE_NAME_LENGTH + 1] = L'n';
    Text[WER_MAX_PARAM_LENGTH - 1] = UNICODE_NULL;
    hr = pWerReportSetParameter(Report, WER_P2, L"Name", Text);
    ok_hex(hr, S_OK);
    Text[WER_MAX_PARAM_LENGTH - 1] = L'n';
    Text[WER_MAX_PARAM_LENGTH] = UNICODE_NULL;
    hr = pWerReportSetParameter(Report, WER_P2, L"Name", Text);
    ok_hex(hr, WER_E_LENGTH_EXCEEDED);

    hr = pWerReportSetUIOption(Report, WerUIConsentDlgBody, L"Body");
    ok_hex(hr, S_OK);
    hr = pWerReportSetUIOption(Report, 0, L"Body");
    ok_hex(hr, E_INVALIDARG);
    hr = pWerReportSetUIOption(Report, WerUIMax, L"Body");
    ok_hex(hr, E_INVALIDARG);
    hr = pWerReportSetUIOption(NULL, WerUIConsentDlgBody, L"Body");
    ok_hex(hr, E_INVALIDARG);

    hr = pWerReportAddFile(Report, L"C:\\LntWerMissing\\missing.txt", WerFileTypeOther, 0);
    ok_hex(hr, HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND));
    hr = pWerReportAddFile(Report, TempFile, 0, 0);
    ok_hex(hr, E_INVALIDARG);
    hr = pWerReportAddFile(Report, TempFile, WerFileTypeMax, 0);
    ok_hex(hr, E_INVALIDARG);
    hr = pWerReportAddFile(NULL, TempFile, WerFileTypeOther, 0);
    ok_hex(hr, E_INVALIDARG);
    hr = pWerReportAddFile(Report, L"%SystemRoot%\\system32\\kernel32.dll", WerFileTypeOther, 0);
    ok_hex(hr, S_OK);

    hr = pWerReportAddDump(Report, NULL, NULL, WerDumpTypeMiniDump, NULL, NULL, 0);
    ok_hex(hr, E_INVALIDARG);
    hr = pWerReportAddDump(Report, GetCurrentProcess(), NULL, WerDumpTypeMicroDump, NULL, NULL, 0);
    ok_hex(hr, E_INVALIDARG);
    hr = pWerReportAddDump(Report, GetCurrentProcess(), NULL, 0, NULL, NULL, 0);
    ok_hex(hr, E_INVALIDARG);
    hr = pWerReportAddDump(Report, GetCurrentProcess(), NULL, WerDumpTypeMax, NULL, NULL, 0);
    ok_hex(hr, E_INVALIDARG);
    hr = pWerReportAddDump(Report, GetCurrentProcess(), GetCurrentThread(), WerDumpTypeMicroDump, NULL, NULL, 0);
    ok_hex(hr, S_OK);

    hr = pWerReportSubmit(Report, 0, WER_SUBMIT_QUEUE, NULL);
    ok_hex(hr, E_INVALIDARG);
    hr = pWerReportSubmit(Report, WerConsentMax, WER_SUBMIT_QUEUE, NULL);
    ok_hex(hr, E_INVALIDARG);
    hr = pWerReportSubmit(NULL, WerConsentNotAsked, WER_SUBMIT_QUEUE, NULL);
    ok_hex(hr, E_INVALIDARG);

    hr = pWerReportCloseHandle(Report);
    ok_hex(hr, S_OK);
}

static
VOID
TestExclusion(VOID)
{
    WCHAR Windows[MAX_PATH + 2];
    HRESULT hr;

    pWerRemoveExcludedApplication(L"lntwer_test.exe", FALSE);
    hr = pWerAddExcludedApplication(NULL, FALSE);
    ok_hex(hr, E_INVALIDARG);
    hr = pWerAddExcludedApplication(L"", FALSE);
    ok_hex(hr, E_INVALIDARG);
    hr = pWerAddExcludedApplication(L"lntwer_test.exe", FALSE);
    ok_hex(hr, S_OK);
    hr = pWerAddExcludedApplication(L"lntwer_test.exe", FALSE);
    ok_hex(hr, S_OK);
    hr = pWerRemoveExcludedApplication(NULL, FALSE);
    ok_hex(hr, E_INVALIDARG);
    hr = pWerRemoveExcludedApplication(L"lntwer_test.exe", FALSE);
    ok_hex(hr, S_OK);
    hr = pWerRemoveExcludedApplication(L"lntwer_test.exe", FALSE);
    ok(hr == E_FAIL || hr == HRESULT_FROM_WIN32(ERROR_ENVVAR_NOT_FOUND), "second remove 0x%lx\n", hr);

    if (GetWindowsDirectoryW(Windows, MAX_PATH))
    {
        StringCchCatW(Windows, ARRAYSIZE(Windows), L"\\");
        hr = pWerAddExcludedApplication(Windows, FALSE);
        ok_hex(hr, E_INVALIDARG);
        hr = pWerRemoveExcludedApplication(Windows, FALSE);
        ok_hex(hr, E_INVALIDARG);
    }
}

static
VOID
TestStoreArguments(VOID)
{
    WER_REPORT_METADATA_V2 Metadata;
    HREPORTSTORE Store = (HREPORTSTORE)(ULONG_PTR)0xdeadbeef;
    ULONGLONG Size = 0;
    DWORD Count = 0;
    PCWSTR Key;
    HRESULT hr;

    hr = pWerStoreOpen(E_STORE_INVALID, &Store);
    ok_hex(hr, E_INVALIDARG);
    hr = pWerStoreOpen(E_STORE_MACHINE_QUEUE, NULL);
    ok_hex(hr, E_INVALIDARG);
    hr = pWerStoreGetFirstReportKey(NULL, &Key);
    ok_hex(hr, E_INVALIDARG);
    pWerStoreClose(NULL);

    hr = pWerStoreOpen(E_STORE_MACHINE_QUEUE, &Store);
    ok_hex(hr, S_OK);
    if (FAILED(hr))
        return;

    hr = pWerStoreGetFirstReportKey(Store, NULL);
    ok_hex(hr, E_INVALIDARG);
    hr = pWerStoreGetReportCount(Store, &Count);
    ok_hex(hr, S_OK);
    ok(Count >= 1, "Count %lu\n", Count);
    hr = pWerStoreGetSizeOnDisk(Store, &Size);
    ok_hex(hr, S_OK);
    ok(Size > 0, "Size %I64u\n", Size);
    RtlZeroMemory(&Metadata, sizeof(Metadata));
    hr = pWerStoreQueryReportMetadataV2(Store, L"..\\..\\Report", &Metadata);
    ok(FAILED(hr), "relative key returned 0x%lx\n", hr);
    hr = pWerStoreQueryReportMetadataV2(Store, L"LntWerNoSuchReport", &Metadata);
    ok(FAILED(hr), "missing key returned 0x%lx\n", hr);
    pWerStoreClose(Store);
}

static
VOID
TestSubmit(
    _In_ PCWSTR TempFile,
    _In_ PCWSTR RegisteredFile)
{
    static const GUID ZeroGuid;
    WER_REPORT_INFORMATION Information;
    WCHAR EventName[64], ImagePath[MAX_PATH];
    WER_SUBMIT_RESULT Result;
    FOUND_REPORT Found;
    FILETIME Start;
    HREPORT Report;
    HRESULT hr;

    GetSystemTimeAsFileTime(&Start);
    StringCchPrintfW(EventName, ARRAYSIZE(EventName), L"LntWerApi%lu", GetCurrentProcessId());
    RtlZeroMemory(&Information, sizeof(Information));
    Information.dwSize = sizeof(Information);
    StringCchCopyW(Information.wzDescription, ARRAYSIZE(Information.wzDescription), L"LiberNT report test");
    hr = pWerReportCreate(EventName, WerReportNonCritical, &Information, &Report);
    ok_hex(hr, S_OK);
    if (FAILED(hr))
        return;

    ok_hex(pWerReportSetParameter(Report, WER_P0, L"TestName", L"TestValue"), S_OK);
    ok_hex(pWerReportSetParameter(Report, WER_P1, NULL, L"Second"), S_OK);
    ok_hex(pWerReportAddFile(Report, TempFile, WerFileTypeOther, WER_FILE_ANONYMOUS_DATA), S_OK);
    ok_hex(pWerReportAddDump(Report, GetCurrentProcess(), GetCurrentThread(), WerDumpTypeMiniDump, NULL, NULL, 0), S_OK);
    Result = 0;
    hr = pWerReportSubmit(Report, WerConsentNotAsked, WER_SUBMIT_QUEUE, &Result);
    ok_hex(hr, S_OK);
    ok_int(Result, WerReportQueued);

    if (FindReport(EventName, NULL, &Start, &Found))
    {
        ok_wstr(Found.Metadata.Signature.Parameters[0].Name, L"TestName");
        ok_wstr(Found.Metadata.Signature.Parameters[0].Value, L"TestValue");
        ok_wstr(Found.Metadata.Signature.Parameters[1].Name, L"P1");
        ok_wstr(Found.Metadata.Signature.Parameters[1].Value, L"Second");
        ok_wstr(Found.Metadata.Signature.Parameters[2].Value, L"");
        ok_long(Found.Metadata.NumberOfFiles, 2);
        ok(HasName(Found.Names, L"minidump.mdmp"), "no minidump in the report\n");
        ok(HasName(Found.Names, wcsrchr(TempFile, L'\\') + 1), "added file missing from the report\n");
        ok(ReportFileEquals(&Found, wcsrchr(TempFile, L'\\') + 1, "werapi test file"), "added file content differs\n");
        ok(memcmp(&Found.Metadata.ReportId, &ZeroGuid, sizeof(ZeroGuid)) != 0, "ReportId is GUID_NULL\n");
        ok(memcmp(&Found.Metadata.BucketId, &ZeroGuid, sizeof(ZeroGuid)) == 0, "BucketId is set\n");
        ok(Found.Metadata.SizeInBytes > 0, "SizeInBytes 0\n");
        ok(GetFileAttributesW(TempFile) != INVALID_FILE_ATTRIBUTES, "added file was deleted\n");
        TestStoreArguments();
        DeleteReport(&Found);
    }
    else if (StoreAvailable())
    {
        ok(0, "queued report %S not found in the store\n", EventName);
    }

    Result = 0;
    hr = pWerReportSubmit(Report, WerConsentDenied, WER_SUBMIT_NO_ARCHIVE, &Result);
    ok_hex(hr, S_OK);
    ok_int(Result, WerReportCancelled);
    Result = 0;
    hr = pWerReportSubmit(Report, WerConsentApproved, WER_SUBMIT_NO_QUEUE, &Result);
    ok_hex(hr, S_OK);
    ok_int(Result, WerReportFailed);

    if (GetModuleFileNameW(NULL, ImagePath, ARRAYSIZE(ImagePath)) &&
        SUCCEEDED(pWerAddExcludedApplication(ImagePath, FALSE)))
    {
        Result = 0;
        hr = pWerReportSubmit(Report, WerConsentNotAsked, WER_SUBMIT_QUEUE, &Result);
        ok_hex(hr, S_OK);
        ok_int(Result, WerDisabled);
        ok_hex(pWerRemoveExcludedApplication(ImagePath, FALSE), S_OK);
    }
    pWerReportCloseHandle(Report);

    GetSystemTimeAsFileTime(&Start);
    StringCchPrintfW(EventName, ARRAYSIZE(EventName), L"LntWerReg%lu", GetCurrentProcessId());
    hr = pWerReportCreate(EventName, WerReportNonCritical, NULL, &Report);
    ok_hex(hr, S_OK);
    if (FAILED(hr))
        return;

    ok_hex(pWerReportSetParameter(Report, WER_P0, NULL, L"Registered"), S_OK);
    ok_hex(pWerRegisterFile(RegisteredFile, WerRegFileTypeOther, 0), S_OK);
    hr = pWerReportSubmit(Report, WerConsentNotAsked, WER_SUBMIT_QUEUE | WER_SUBMIT_ADD_REGISTERED_DATA, &Result);
    ok_hex(hr, S_OK);
    ok_hex(pWerUnregisterFile(RegisteredFile), S_OK);
    pWerReportCloseHandle(Report);

    if (FindReport(EventName, NULL, &Start, &Found))
    {
        ok_wstr(Found.Metadata.Signature.Parameters[0].Name, L"P0");
        ok_long(Found.Metadata.NumberOfFiles, 1);
        ok(HasName(Found.Names, wcsrchr(RegisteredFile, L'\\') + 1), "registered file missing from the report\n");
        ok(ReportFileEquals(&Found, wcsrchr(RegisteredFile, L'\\') + 1, "registered file"), "registered file content differs\n");
        DeleteReport(&Found);
    }
    else if (StoreAvailable())
    {
        ok(0, "report %S with registered data not found\n", EventName);
    }
}

START_TEST(WerReport)
{
    WCHAR TempDirectory[MAX_PATH], TempFile[MAX_PATH], RegisteredFile[MAX_PATH];

    if (!LoadFunctions())
    {
        skip("Windows Error Reporting exports are missing\n");
        return;
    }

    GetTempPathW(ARRAYSIZE(TempDirectory), TempDirectory);
    StringCchPrintfW(TempFile, ARRAYSIZE(TempFile), L"%slntwerapi_%lu.txt", TempDirectory, GetCurrentProcessId());
    StringCchPrintfW(RegisteredFile, ARRAYSIZE(RegisteredFile), L"%slntwerreg_%lu.txt", TempDirectory,
                     GetCurrentProcessId());
    ok(WriteTestFile(TempFile, "werapi test file"), "cannot create %S\n", TempFile);
    ok(WriteTestFile(RegisteredFile, "registered file"), "cannot create %S\n", RegisteredFile);

    TestReportHandles();
    TestReportArguments(TempFile);
    TestExclusion();
    TestSubmit(TempFile, RegisteredFile);

    DeleteFileW(TempFile);
    DeleteFileW(RegisteredFile);
}

static
DWORD
WINAPI
CrashThread(
    _In_ PVOID Parameter)
{
    PCRASH_SHARED Shared = Parameter;

    Shared->CrashThreadId = GetCurrentThreadId();
    *CrashPointer = 1;
    return 0;
}

static
VOID
CrashChild(
    _In_ ULONG Mode,
    _In_ DWORD ParentId)
{
    WCHAR Name[64], Path[MAX_PATH], Temp[MAX_PATH], File[MAX_PATH];
    PCRASH_SHARED Shared;
    HANDLE Mapping, Thread;
    PUCHAR Block;
    DWORD Protect;
    ULONG Index;

    StringCchPrintfW(Name, ARRAYSIZE(Name), L"Local\\LntWerCrash%lu", ParentId);
    Mapping = OpenFileMappingW(FILE_MAP_WRITE, FALSE, Name);
    Shared = Mapping ? MapViewOfFile(Mapping, FILE_MAP_WRITE, 0, 0, sizeof(*Shared)) : NULL;
    if (!Shared || !GetModuleFileNameW(NULL, Path, ARRAYSIZE(Path)) || !GetTempPathW(ARRAYSIZE(Temp), Temp))
        ExitProcess(0xBAD);

    HandlerContext.Magic = WER_HANDLER_MAGIC;
    HandlerContext.Mode = Mode;
    StringCchCopyW(HandlerContext.MappingName, ARRAYSIZE(HandlerContext.MappingName), Name);
    StringCchPrintfW(HandlerContext.DebuggerLaunch, ARRAYSIZE(HandlerContext.DebuggerLaunch),
                     L"\"%s\" WerCrashReport debugger %%ld %%ld", Path);
    Shared->ContextAddress = (ULONG_PTR)&HandlerContext;

    if (Mode == WER_HANDLER_MODE_CLAIM)
    {
        Shared->SetFlags = pWerSetFlags(WER_FAULT_REPORTING_FLAG_QUEUE);
        StringCchPrintfW(File, ARRAYSIZE(File), L"%slntwer_%lu_keep.txt", Temp, ParentId);
        Shared->RegisterKeep = pWerRegisterFile(File, WerRegFileTypeOther, 0);
        StringCchPrintfW(File, ARRAYSIZE(File), L"%slntwer_%lu_delete.txt", Temp, ParentId);
        Shared->RegisterDelete = pWerRegisterFile(File, WerRegFileTypeUserDocument, WER_FILE_DELETE_WHEN_DONE);

        Block = VirtualAlloc(NULL, BLOCK_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (Block)
        {
            for (Index = 0; Index < BLOCK_SIZE; Index++)
                Block[Index] = (UCHAR)(Index * 7 + 0x5A);
            VirtualProtect(Block, BLOCK_SIZE, PAGE_READONLY, &Protect);
            Shared->BlockAddress = (ULONG_PTR)Block;
            Shared->RegisterBlock = pWerRegisterMemoryBlock(Block, BLOCK_SIZE);
        }
    }

    StringCchPrintfW(File, ARRAYSIZE(File), L"%slntwer_%lu_unlisted.dll", Temp, ParentId);
    Shared->RegisterUnlisted = pWerRegisterRuntimeExceptionModule(File, &HandlerContext);
    *wcsrchr(Path, L'\\') = UNICODE_NULL;
    StringCchCatW(Path, ARRAYSIZE(Path), L"\\kernel32_werhandler.dll");
    Shared->RegisterHandler = pWerRegisterRuntimeExceptionModule(Path, &HandlerContext);

    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    Thread = CreateThread(NULL, 0, CrashThread, Shared, 0, NULL);
    if (Thread)
        WaitForSingleObject(Thread, CHILD_SAFETY_MS);
    ExitProcess(CHILD_EXIT_RETURNED);
}

static
VOID
DebuggerChild(
    _In_ DWORD ProcessId,
    _In_ HANDLE Event)
{
    WCHAR Name[64];
    HANDLE Ran;

    StringCchPrintfW(Name, ARRAYSIZE(Name), L"Local\\LntWerDbg%lu", ProcessId);
    Ran = OpenEventW(EVENT_MODIFY_STATE, FALSE, Name);
    if (Ran && SetEvent(Event))
        SetEvent(Ran);
    ExitProcess(0);
}

static
HANDLE
StartCrashChild(
    _In_ ULONG Mode,
    _In_ BOOL Debugger,
    _Out_ HANDLE *Ran)
{
    CHAR Path[MAX_PATH], CommandLine[MAX_PATH * 2];
    STARTUPINFOA StartupInfo = { sizeof(StartupInfo) };
    PROCESS_INFORMATION ProcessInfo;
    WCHAR Name[64];

    *Ran = NULL;
    if (!GetModuleFileNameA(NULL, Path, ARRAYSIZE(Path)))
        return NULL;

    StringCbPrintfA(CommandLine, sizeof(CommandLine), "\"%s\" WerCrashReport child %lu %lu",
                    Path, Mode, GetCurrentProcessId());
    if (!CreateProcessA(NULL, CommandLine, NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, NULL, &StartupInfo, &ProcessInfo))
    {
        ok(0, "CreateProcess failed: %lu\n", GetLastError());
        return NULL;
    }

    if (Debugger)
    {
        StringCchPrintfW(Name, ARRAYSIZE(Name), L"Local\\LntWerDbg%lu", ProcessInfo.dwProcessId);
        *Ran = CreateEventW(NULL, TRUE, FALSE, Name);
    }

    ResumeThread(ProcessInfo.hThread);
    CloseHandle(ProcessInfo.hThread);
    return ProcessInfo.hProcess;
}

static
DWORD
WaitChild(
    _In_ HANDLE Process,
    _Out_opt_ PDWORD Elapsed)
{
    DWORD Start = GetTickCount(), Wait, ExitCode = 0xDEADBEEF;

    Wait = WaitForSingleObject(Process, CHILD_SAFETY_MS);
    ok(Wait == WAIT_OBJECT_0, "child did not exit (%lu)\n", Wait);
    if (Wait != WAIT_OBJECT_0)
        TerminateProcess(Process, 0xDEAD);
    else
        GetExitCodeProcess(Process, &ExitCode);
    if (Elapsed)
        *Elapsed = GetTickCount() - Start;
    return ExitCode;
}

static
VOID
CheckCrashDumps(
    _In_ PFOUND_REPORT Found,
    _In_ PCRASH_SHARED Shared)
{
    PMINIDUMP_EXCEPTION_STREAM Exception;
    PMINIDUMP_MEMORY_DESCRIPTOR Range;
    PMINIDUMP_DIRECTORY Directory;
    HANDLE File, Mapping;
    PUCHAR View, Data;
    ULONG Size, Index;
    BOOL Same = TRUE;

    View = MapReportFile(Found, L"memory.hdmp", &File, &Mapping);
    ok(View != NULL, "heap dump not readable\n");
    if (View)
    {
        Range = FindDumpRange(View, Shared->BlockAddress);
        ok(Range != NULL, "registered memory block missing from the heap dump\n");
        if (Range)
        {
            ok(Range->StartOfMemoryRange == Shared->BlockAddress, "range starts at %I64x, expected %I64x\n",
               Range->StartOfMemoryRange, Shared->BlockAddress);
            ok(Range->Memory.DataSize >= BLOCK_SIZE, "range size %lu\n", (ULONG)Range->Memory.DataSize);
            Data = View + Range->Memory.Rva;
            for (Index = 0; Index < BLOCK_SIZE && Index < Range->Memory.DataSize; Index++)
                Same &= (Data[Index] == (UCHAR)(Index * 7 + 0x5A));
            ok(Same, "registered memory block content differs\n");
        }
    }
    UnmapReportFile(View, File, Mapping);

    View = MapReportFile(Found, L"minidump.mdmp", &File, &Mapping);
    ok(View != NULL, "minidump not readable\n");
    if (View)
    {
        ok(FindDumpRange(View, Shared->BlockAddress) == NULL, "registered memory block in the minidump\n");
        if (pMiniDumpReadDumpStream(View, ExceptionStream, &Directory, (PVOID *)&Exception, &Size))
        {
            ok_hex(Exception->ExceptionRecord.ExceptionCode, STATUS_ACCESS_VIOLATION);
            ok_long(Exception->ThreadId, Shared->CrashThreadId);
        }
        else
        {
            ok(0, "minidump has no exception stream\n");
        }
    }
    UnmapReportFile(View, File, Mapping);
}

static
VOID
TestClaimedCrash(
    _In_ PCRASH_SHARED Shared,
    _In_ BOOL HandlerListed,
    _In_ PCWSTR Temp)
{
    WCHAR Keep[MAX_PATH], Delete[MAX_PATH], EventName[64], Value[16];
    FOUND_REPORT Found;
    HANDLE Process, Ran;
    FILETIME Start;
    DWORD ExitCode, Elapsed, ChildId;

    StringCchPrintfW(Keep, ARRAYSIZE(Keep), L"%slntwer_%lu_keep.txt", Temp, GetCurrentProcessId());
    StringCchPrintfW(Delete, ARRAYSIZE(Delete), L"%slntwer_%lu_delete.txt", Temp, GetCurrentProcessId());
    ok(WriteTestFile(Keep, "keep this file"), "cannot create %S\n", Keep);
    ok(WriteTestFile(Delete, "delete this file"), "cannot create %S\n", Delete);

    RtlZeroMemory(Shared, sizeof(*Shared));
    GetSystemTimeAsFileTime(&Start);
    Process = StartCrashChild(WER_HANDLER_MODE_CLAIM, FALSE, &Ran);
    if (!Process)
        return;

    ChildId = GetProcessId(Process);
    ExitCode = WaitChild(Process, &Elapsed);
    CloseHandle(Process);
    trace("claimed crash: child exited after %lu ms\n", Elapsed);
    ok_hex(ExitCode, STATUS_ACCESS_VIOLATION);
    ok_hex(Shared->SetFlags, S_OK);
    ok_hex(Shared->RegisterKeep, S_OK);
    ok_hex(Shared->RegisterDelete, S_OK);
    ok_hex(Shared->RegisterBlock, S_OK);
    ok_hex(Shared->RegisterUnlisted, S_OK);
    ok_hex(Shared->RegisterHandler, S_OK);

    if (!HandlerListed)
    {
        skip("handler module not listed in RuntimeExceptionHelperModules\n");
        DeleteFileW(Keep);
        DeleteFileW(Delete);
        return;
    }

    ok_long(Shared->Handler.EventCalls, 1);
    ok_long(Shared->Handler.SignatureCalls, 2);
    ok_long(Shared->Handler.SignatureIndexMask, 3);
    ok_long(Shared->Handler.SignatureOrderErrors, 0);
    ok_long(Shared->Handler.DebuggerCalls, 1);
    ok_long(Shared->Handler.ContextRead, TRUE);
    ok(Shared->Handler.Context == Shared->ContextAddress, "context %I64x, expected %I64x\n",
       Shared->Handler.Context, Shared->ContextAddress);
    ok_long(Shared->Handler.InfoSize, sizeof(WER_RUNTIME_EXCEPTION_INFORMATION));
    ok_long(Shared->Handler.ProcessId, ChildId);
    ok_long(Shared->Handler.ThreadId, Shared->CrashThreadId);
    ok_hex(Shared->Handler.ExceptionCode, STATUS_ACCESS_VIOLATION);
    ok(Shared->Handler.ExceptionAddress != 0, "exception address is 0\n");
    ok(Shared->Handler.EventNameSize >= 64, "event name buffer %lu\n", Shared->Handler.EventNameSize);
    ok(Shared->Handler.ReportId[0] != UNICODE_NULL, "report id is empty\n");

    ok(GetFileAttributesW(Keep) != INVALID_FILE_ATTRIBUTES, "registered file was deleted\n");
    ok(GetFileAttributesW(Delete) == INVALID_FILE_ATTRIBUTES, "WER_FILE_DELETE_WHEN_DONE file still exists\n");

    StringCchPrintfW(EventName, ARRAYSIZE(EventName), L"LntWerCrash%lu", ChildId);
    if (FindReport(EventName, NULL, &Start, &Found))
    {
        ok_wstr(Found.Metadata.Signature.Parameters[0].Name, L"HandlerMode");
        StringCchPrintfW(Value, ARRAYSIZE(Value), L"%lu", WER_HANDLER_MODE_CLAIM);
        ok_wstr(Found.Metadata.Signature.Parameters[0].Value, Value);
        ok_wstr(Found.Metadata.Signature.Parameters[1].Name, L"HandlerProcess");
        StringCchPrintfW(Value, ARRAYSIZE(Value), L"%lu", ChildId);
        ok_wstr(Found.Metadata.Signature.Parameters[1].Value, Value);
        ok_wstr(Found.Metadata.Signature.Parameters[2].Name, L"");
        ok_long(Found.Metadata.NumberOfFiles, 4);
        ok(HasName(Found.Names, L"minidump.mdmp"), "minidump missing\n");
        ok(HasName(Found.Names, L"memory.hdmp"), "heap dump missing\n");
        ok(HasName(Found.Names, wcsrchr(Keep, L'\\') + 1), "registered file missing\n");
        ok(HasName(Found.Names, wcsrchr(Delete, L'\\') + 1), "registered user document missing\n");
        ok(ReportFileEquals(&Found, wcsrchr(Keep, L'\\') + 1, "keep this file"), "registered file content differs\n");
        ok(ReportFileEquals(&Found, wcsrchr(Delete, L'\\') + 1, "delete this file"), "user document content differs\n");
        CheckCrashDumps(&Found, Shared);
        DeleteReport(&Found);
    }
    else if (StoreAvailable())
    {
        ok(0, "crash report %S not found\n", EventName);
    }

    DeleteFileW(Keep);
    DeleteFileW(Delete);
}

static
BOOL
EnableLocalDumps(
    _In_ PCWSTR Folder,
    _Out_ PBOOL CreatedRoot)
{
    DWORD Disposition, Count = 1, Type = 0, Flags = LOCAL_DUMP_FLAGS;
    HKEY Root, Key;
    BOOL Success;

    *CreatedRoot = FALSE;
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, LOCAL_DUMPS_KEY, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &Root,
                        &Disposition) != ERROR_SUCCESS)
    {
        return FALSE;
    }

    *CreatedRoot = Disposition == REG_CREATED_NEW_KEY;
    Success = RegCreateKeyExW(Root, L"kernel32_apitest.exe", 0, NULL, 0, KEY_SET_VALUE, NULL, &Key, NULL) == ERROR_SUCCESS;
    if (Success)
    {
        Success = RegSetValueExW(Key, L"DumpFolder", 0, REG_EXPAND_SZ, (const BYTE *)Folder,
                                 (DWORD)(wcslen(Folder) + 1) * sizeof(WCHAR)) == ERROR_SUCCESS &&
                  RegSetValueExW(Key, L"DumpCount", 0, REG_DWORD, (const BYTE *)&Count, sizeof(Count)) == ERROR_SUCCESS &&
                  RegSetValueExW(Key, L"DumpType", 0, REG_DWORD, (const BYTE *)&Type, sizeof(Type)) == ERROR_SUCCESS &&
                  RegSetValueExW(Key, L"CustomDumpFlags", 0, REG_DWORD, (const BYTE *)&Flags, sizeof(Flags)) == ERROR_SUCCESS;
        RegCloseKey(Key);
    }

    RegCloseKey(Root);
    return Success;
}

static
VOID
DisableLocalDumps(
    _In_ BOOL CreatedRoot)
{
    HKEY Root;

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, LOCAL_DUMPS_KEY, 0, KEY_ALL_ACCESS, &Root) != ERROR_SUCCESS)
        return;

    RegDeleteKeyW(Root, L"kernel32_apitest.exe");
    RegCloseKey(Root);
    if (CreatedRoot)
        RegDeleteKeyW(HKEY_LOCAL_MACHINE, LOCAL_DUMPS_KEY);
}

static
VOID
CheckLocalDump(
    _In_ PCWSTR Folder,
    _In_ PCWSTR OldDump,
    _In_ DWORD ChildId,
    _In_ PCRASH_SHARED Shared)
{
    PMINIDUMP_EXCEPTION_STREAM Exception;
    PMINIDUMP_DIRECTORY Directory;
    FOUND_REPORT Location;
    HANDLE File, Mapping;
    WCHAR Name[64];
    PVOID View;
    ULONG Size;

    ok(GetFileAttributesW(OldDump) == INVALID_FILE_ATTRIBUTES, "oldest local dump was not replaced\n");
    RtlZeroMemory(&Location, sizeof(Location));
    StringCchCopyW(Location.Directory, ARRAYSIZE(Location.Directory), Folder);
    StringCchPrintfW(Name, ARRAYSIZE(Name), L"kernel32_apitest.exe.%lu.dmp", ChildId);
    View = MapReportFile(&Location, Name, &File, &Mapping);
    ok(View != NULL, "local dump %S not written\n", Name);
    if (View)
    {
        ok_hex((ULONG)((PMINIDUMP_HEADER)View)->Flags, LOCAL_DUMP_FLAGS);
        if (pMiniDumpReadDumpStream(View, ExceptionStream, &Directory, (PVOID *)&Exception, &Size))
        {
            ok_hex(Exception->ExceptionRecord.ExceptionCode, STATUS_ACCESS_VIOLATION);
            ok_long(Exception->ThreadId, Shared->CrashThreadId);
        }
        else
        {
            ok(0, "local dump has no exception stream\n");
        }
    }
    UnmapReportFile(View, File, Mapping);
}

static
VOID
TestDefaultCrash(
    _In_ PCRASH_SHARED Shared,
    _In_ BOOL HandlerListed,
    _In_ PCWSTR Temp)
{
    WCHAR Folder[MAX_PATH], OldDump[MAX_PATH], DumpPath[MAX_PATH];
    FOUND_REPORT Found;
    HANDLE Process, Ran;
    FILETIME Start;
    DWORD ExitCode, Elapsed, ChildId;
    BOOL LocalDumps, CreatedRoot = FALSE;

    StringCchPrintfW(Folder, ARRAYSIZE(Folder), L"%slntwerdumps_%lu", Temp, GetCurrentProcessId());
    StringCchPrintfW(OldDump, ARRAYSIZE(OldDump), L"%s\\old.dmp", Folder);
    CreateDirectoryW(Folder, NULL);
    ok(WriteTestFile(OldDump, "old dump"), "cannot create %S\n", OldDump);
    LocalDumps = EnableLocalDumps(Folder, &CreatedRoot);
    if (!LocalDumps)
        skip("cannot configure LocalDumps\n");

    RtlZeroMemory(Shared, sizeof(*Shared));
    GetSystemTimeAsFileTime(&Start);
    Process = StartCrashChild(WER_HANDLER_MODE_NO_CLAIM, FALSE, &Ran);
    if (!Process)
    {
        DisableLocalDumps(CreatedRoot);
        return;
    }

    ChildId = GetProcessId(Process);
    ExitCode = WaitChild(Process, &Elapsed);
    CloseHandle(Process);
    DisableLocalDumps(CreatedRoot);
    if (LocalDumps)
        CheckLocalDump(Folder, OldDump, ChildId, Shared);
    StringCchPrintfW(DumpPath, ARRAYSIZE(DumpPath), L"%s\\kernel32_apitest.exe.%lu.dmp", Folder, ChildId);
    DeleteFileW(DumpPath);
    DeleteFileW(OldDump);
    RemoveDirectoryW(Folder);
    trace("unclaimed crash: child exited after %lu ms\n", Elapsed);
    ok_hex(ExitCode, STATUS_ACCESS_VIOLATION);
    if (HandlerListed)
    {
        ok_long(Shared->Handler.EventCalls, 1);
        ok_long(Shared->Handler.SignatureCalls, 0);
        ok_long(Shared->Handler.DebuggerCalls, 0);
    }

    if (FindReport(APPCRASH_EVENT, L"kernel32_apitest.exe", &Start, &Found))
    {
        ok_wstr(Found.Metadata.Signature.Parameters[0].Name, L"Application Name");
        ok_wstr(Found.Metadata.Signature.Parameters[3].Name, L"Fault Module Name");
        ok(!_wcsicmp(Found.Metadata.Signature.Parameters[3].Value, L"kernel32_apitest.exe"),
           "fault module %S\n", Found.Metadata.Signature.Parameters[3].Value);
        ok_wstr(Found.Metadata.Signature.Parameters[6].Name, L"Exception Code");
        ok_wstr(Found.Metadata.Signature.Parameters[6].Value, L"c0000005");
        ok_long(Found.Metadata.NumberOfFiles, 1);
        ok(HasName(Found.Names, L"minidump.mdmp"), "minidump missing\n");
        DeleteReport(&Found);
    }
    else if (StoreAvailable())
    {
        ok(0, "APPCRASH report not found\n");
    }
}

static
VOID
TestDebuggerCrash(
    _In_ PCRASH_SHARED Shared)
{
    WCHAR EventName[64];
    FOUND_REPORT Found;
    HANDLE Process, Ran;
    FILETIME Start;
    DWORD ExitCode, Elapsed, ChildId;

    RtlZeroMemory(Shared, sizeof(*Shared));
    GetSystemTimeAsFileTime(&Start);
    Process = StartCrashChild(WER_HANDLER_MODE_DEBUGGER, TRUE, &Ran);
    if (!Process)
        return;

    ChildId = GetProcessId(Process);
    ExitCode = WaitChild(Process, &Elapsed);
    CloseHandle(Process);
    trace("custom debugger crash: child exited after %lu ms\n", Elapsed);
    ok_hex(ExitCode, STATUS_ACCESS_VIOLATION);
    ok_long(Shared->Handler.DebuggerCalls, 1);
    ok(Ran && WaitForSingleObject(Ran, 0) == WAIT_OBJECT_0, "custom debugger was not launched with the event\n");
    if (Ran)
        CloseHandle(Ran);

    StringCchPrintfW(EventName, ARRAYSIZE(EventName), L"LntWerCrash%lu", ChildId);
    if (FindReport(EventName, NULL, &Start, &Found))
        DeleteReport(&Found);
    else if (StoreAvailable())
        ok(0, "crash report %S not found\n", EventName);
}

START_TEST(WerCrashReport)
{
    WCHAR Name[64], Temp[MAX_PATH], Handler[MAX_PATH], Unlisted[MAX_PATH];
    PCRASH_SHARED Shared;
    HANDLE Mapping;
    char **Arguments;
    int ArgumentCount;
    DWORD Value = 1;
    BOOL Listed = FALSE;
    HKEY Key;

    if (!LoadFunctions())
    {
        skip("Windows Error Reporting exports are missing\n");
        return;
    }

    ArgumentCount = winetest_get_mainargs(&Arguments);
    if (ArgumentCount >= 5 && !strcmp(Arguments[2], "child"))
        CrashChild(strtoul(Arguments[3], NULL, 10), strtoul(Arguments[4], NULL, 10));
    if (ArgumentCount >= 5 && !strcmp(Arguments[2], "debugger"))
        DebuggerChild(strtoul(Arguments[3], NULL, 10), (HANDLE)(ULONG_PTR)strtoul(Arguments[4], NULL, 10));

    StringCchPrintfW(Name, ARRAYSIZE(Name), L"Local\\LntWerCrash%lu", GetCurrentProcessId());
    Mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(*Shared), Name);
    Shared = Mapping ? MapViewOfFile(Mapping, FILE_MAP_WRITE, 0, 0, sizeof(*Shared)) : NULL;
    ok(Shared != NULL, "cannot create the shared mapping: %lu\n", GetLastError());
    if (!Shared)
        return;

    GetTempPathW(ARRAYSIZE(Temp), Temp);
    GetModuleFileNameW(NULL, Handler, ARRAYSIZE(Handler));
    *wcsrchr(Handler, L'\\') = UNICODE_NULL;
    StringCchCatW(Handler, ARRAYSIZE(Handler), L"\\kernel32_werhandler.dll");
    StringCchPrintfW(Unlisted, ARRAYSIZE(Unlisted), L"%slntwer_%lu_unlisted.dll", Temp, GetCurrentProcessId());
    ok(CopyFileW(Handler, Unlisted, FALSE), "cannot copy the handler module: %lu\n", GetLastError());

    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, HELPERS_KEY, 0, NULL, 0, KEY_SET_VALUE, NULL, &Key, NULL) == ERROR_SUCCESS)
    {
        Listed = RegSetValueExW(Key, Handler, 0, REG_DWORD, (PBYTE)&Value, sizeof(Value)) == ERROR_SUCCESS;
        RegCloseKey(Key);
    }

    TestClaimedCrash(Shared, Listed, Temp);
    TestDefaultCrash(Shared, Listed, Temp);
    if (Listed)
        TestDebuggerCrash(Shared);

    if (Listed && RegOpenKeyExW(HKEY_LOCAL_MACHINE, HELPERS_KEY, 0, KEY_SET_VALUE, &Key) == ERROR_SUCCESS)
    {
        RegDeleteValueW(Key, Handler);
        RegCloseKey(Key);
    }
    DeleteFileW(Unlisted);
    UnmapViewOfFile(Shared);
    CloseHandle(Mapping);
}

static
DWORD
WINAPI
FinishCallback(
    _In_ PVOID Parameter)
{
    InterlockedIncrement(&HangShared->CallbackCalls);
    HangShared->Parameter = (ULONG_PTR)Parameter;
    HangShared->InProgress = pApplicationRecoveryInProgress(&HangShared->Cancelled);
    pApplicationRecoveryFinished(TRUE);
    return 0;
}

static
DWORD
WINAPI
NoPingCallback(
    _In_ PVOID Parameter)
{
    InterlockedIncrement(&HangShared->CallbackCalls);
    HangShared->Parameter = (ULONG_PTR)Parameter;
    WaitForSingleObject(HangNever, CHILD_SAFETY_MS);
    InterlockedExchange(&HangShared->AfterWait, 1);
    return 0;
}

static
DWORD
WINAPI
CancelRecoveryCallback(
    _In_ PVOID Parameter)
{
    InterlockedIncrement(&HangShared->CallbackCalls);
    HangShared->Parameter = (ULONG_PTR)Parameter;
    HangShared->InProgress = pApplicationRecoveryInProgress(&HangShared->Cancelled);
    SetEvent(HangStarted);
    WaitForSingleObject(HangClicked, CHILD_SAFETY_MS);
    HangShared->InProgressAfterClick = pApplicationRecoveryInProgress(&HangShared->CancelledAfterClick);
    pApplicationRecoveryFinished(FALSE);
    return 0;
}

static
HANDLE
OpenHangEvent(
    _In_ PCWSTR Prefix,
    _In_ DWORD ParentId,
    _In_ BOOL Create)
{
    WCHAR Name[64];

    StringCchPrintfW(Name, ARRAYSIZE(Name), L"Local\\%s%lu", Prefix, ParentId);
    return Create ? CreateEventW(NULL, FALSE, FALSE, Name) : OpenEventW(EVENT_ALL_ACCESS, FALSE, Name);
}

static
VOID
HangChild(
    _In_ PCSTR Mode,
    _In_ DWORD ParentId)
{
    WNDCLASSW Class = { 0 };
    WCHAR Name[64];
    HANDLE Mapping, Ready;
    HWND Window;

    StringCchPrintfW(Name, ARRAYSIZE(Name), L"Local\\LntWerHang%lu", ParentId);
    Mapping = OpenFileMappingW(FILE_MAP_WRITE, FALSE, Name);
    HangShared = Mapping ? MapViewOfFile(Mapping, FILE_MAP_WRITE, 0, 0, sizeof(*HangShared)) : NULL;
    Ready = OpenHangEvent(L"LntWerHangReady", ParentId, FALSE);
    HangStarted = OpenHangEvent(L"LntWerHangStarted", ParentId, FALSE);
    HangClicked = OpenHangEvent(L"LntWerHangClicked", ParentId, FALSE);
    HangNever = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!HangShared || !Ready || !HangStarted || !HangClicked || !HangNever)
        ExitProcess(0xBAD);

    Class.lpfnWndProc = DefWindowProcW;
    Class.hInstance = GetModuleHandleW(NULL);
    Class.lpszClassName = L"LntWerHangWindow";
    RegisterClassW(&Class);
    Window = CreateWindowExW(0, Class.lpszClassName, L"LntWerHang", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64,
                             NULL, NULL, Class.hInstance, NULL);
    if (!Window)
        ExitProcess(0xBAD);

    if (!strcmp(Mode, "finish"))
    {
        HangShared->Register = pRegisterApplicationRecoveryCallback(FinishCallback, &HangShared->Parameter, 1000, 0);
        HangShared->SetFlags = pWerSetFlags(WER_FAULT_REPORTING_NO_UI);
    }
    else if (!strcmp(Mode, "noping"))
    {
        HangShared->Register = pRegisterApplicationRecoveryCallback(NoPingCallback, NULL, 1, 0);
        HangShared->SetFlags = pWerSetFlags(WER_FAULT_REPORTING_NO_UI);
    }
    else if (!strcmp(Mode, "cancel"))
    {
        HangShared->Register = pRegisterApplicationRecoveryCallback(CancelRecoveryCallback, NULL, 5000, 0);
    }
    else
    {
        HangShared->SetFlags = pWerSetFlags(WER_FAULT_REPORTING_NO_UI);
    }

    HangShared->Window = (ULONG_PTR)Window;
    SetEvent(Ready);
    WaitForSingleObject(HangNever, CHILD_SAFETY_MS);
    ExitProcess(CHILD_EXIT_RETURNED);
}

static
BOOL
CALLBACK
FindRecoveryDialog(
    _In_ HWND Window,
    _In_ LPARAM Parameter)
{
    WCHAR Path[MAX_PATH];
    DWORD ProcessId = 0, Size = ARRAYSIZE(Path);
    HANDLE Process;
    PCWSTR Name;
    BOOL Host = FALSE;

    GetWindowThreadProcessId(Window, &ProcessId);
    if (!IsWindowVisible(Window) || !GetDlgItem(Window, IDCANCEL))
        return TRUE;

    Process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, ProcessId);
    if (Process)
    {
        if (QueryFullProcessImageNameW(Process, 0, Path, &Size))
        {
            Name = wcsrchr(Path, L'\\');
            Host = !_wcsicmp(Name ? Name + 1 : Path, L"werfault.exe");
        }
        CloseHandle(Process);
    }

    if (!Host)
        return TRUE;

    *(HWND *)Parameter = Window;
    return FALSE;
}

static
DWORD
RunHangChild(
    _In_ PCSTR Mode,
    _In_ PHANG_SHARED Shared,
    _In_ HANDLE Ready,
    _In_ HANDLE Started,
    _In_ HANDLE Clicked,
    _Out_ PDWORD Elapsed)
{
    CHAR Path[MAX_PATH], CommandLine[MAX_PATH * 2];
    STARTUPINFOA StartupInfo = { sizeof(StartupInfo) };
    PROCESS_INFORMATION ProcessInfo;
    HWND Dialog = NULL;
    DWORD Start, ExitCode = 0xDEADBEEF;
    DWORD_PTR Answer;
    HRESULT hr;

    *Elapsed = 0;
    RtlZeroMemory(Shared, sizeof(*Shared));
    if (!GetModuleFileNameA(NULL, Path, ARRAYSIZE(Path)))
        return ExitCode;

    StringCbPrintfA(CommandLine, sizeof(CommandLine), "\"%s\" WerHangReport child %s %lu",
                    Path, Mode, GetCurrentProcessId());
    if (!CreateProcessA(NULL, CommandLine, NULL, NULL, FALSE, 0, NULL, NULL, &StartupInfo, &ProcessInfo))
    {
        ok(0, "CreateProcess(%s) failed: %lu\n", Mode, GetLastError());
        return ExitCode;
    }
    CloseHandle(ProcessInfo.hThread);

    ok(WaitForSingleObject(Ready, CHILD_SAFETY_MS) == WAIT_OBJECT_0, "%s child not ready\n", Mode);
    Start = GetTickCount();
    hr = pWerReportHang((HWND)(ULONG_PTR)Shared->Window, NULL);
    ok(hr == S_OK, "%s: WerReportHang returned 0x%lx\n", Mode, hr);

    if (!strcmp(Mode, "cancel"))
    {
        ok(WaitForSingleObject(Started, CHILD_SAFETY_MS) == WAIT_OBJECT_0, "recovery did not start\n");
        EnumWindows(FindRecoveryDialog, (LPARAM)&Dialog);
        ok(Dialog != NULL, "recovery dialog not found\n");
        if (Dialog)
        {
            SendMessageTimeoutW(Dialog, WM_COMMAND, MAKEWPARAM(IDCANCEL, BN_CLICKED),
                                (LPARAM)GetDlgItem(Dialog, IDCANCEL), SMTO_ABORTIFHUNG, CHILD_SAFETY_MS, &Answer);
        }
        SetEvent(Clicked);
    }

    ExitCode = WaitChild(ProcessInfo.hProcess, NULL);
    *Elapsed = GetTickCount() - Start;
    CloseHandle(ProcessInfo.hProcess);
    trace("hang %s: child exited %lu ms after WerReportHang\n", Mode, *Elapsed);
    return ExitCode;
}

START_TEST(WerHangReport)
{
    WCHAR Name[64], LongName[130];
    HANDLE Mapping, Ready, Started, Clicked;
    PHANG_SHARED Shared;
    FOUND_REPORT Found;
    char **Arguments;
    int ArgumentCount;
    DWORD ExitCode, Elapsed;
    FILETIME Start;
    ULONG Index;
    HRESULT hr;

    if (!LoadFunctions() || !pWerReportHang)
    {
        skip("Windows Error Reporting exports are missing\n");
        return;
    }

    ArgumentCount = winetest_get_mainargs(&Arguments);
    if (ArgumentCount >= 5 && !strcmp(Arguments[2], "child"))
        HangChild(Arguments[3], strtoul(Arguments[4], NULL, 10));

    hr = pWerReportHang(NULL, NULL);
    ok_hex(hr, HRESULT_FROM_WIN32(ERROR_INVALID_WINDOW_HANDLE));
    for (Index = 0; Index < ARRAYSIZE(LongName) - 1; Index++)
        LongName[Index] = L'n';
    LongName[128] = UNICODE_NULL;
    hr = pWerReportHang(GetDesktopWindow(), LongName);
    ok_hex(hr, E_INVALIDARG);

    StringCchPrintfW(Name, ARRAYSIZE(Name), L"Local\\LntWerHang%lu", GetCurrentProcessId());
    Mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(*Shared), Name);
    Shared = Mapping ? MapViewOfFile(Mapping, FILE_MAP_WRITE, 0, 0, sizeof(*Shared)) : NULL;
    Ready = OpenHangEvent(L"LntWerHangReady", GetCurrentProcessId(), TRUE);
    Started = OpenHangEvent(L"LntWerHangStarted", GetCurrentProcessId(), TRUE);
    Clicked = OpenHangEvent(L"LntWerHangClicked", GetCurrentProcessId(), TRUE);
    ok(Shared && Ready && Started && Clicked, "cannot create the shared objects: %lu\n", GetLastError());
    if (!Shared || !Ready || !Started || !Clicked)
        return;

    GetSystemTimeAsFileTime(&Start);
    ExitCode = RunHangChild("finish", Shared, Ready, Started, Clicked, &Elapsed);
    ok_hex(ExitCode, HANG_EXIT_STATUS);
    ok_hex(Shared->Register, S_OK);
    ok_hex(Shared->SetFlags, S_OK);
    ok_long(Shared->CallbackCalls, 1);
    ok(Shared->Parameter != 0, "recovery parameter not passed\n");
    ok_hex(Shared->InProgress, S_OK);
    ok_long(Shared->Cancelled, FALSE);
    if (FindReport(L"AppHangB1", L"kernel32_apitest.exe", &Start, &Found))
    {
        ok_wstr(Found.Metadata.Signature.Parameters[0].Name, L"Application Name");
        ok(HasName(Found.Names, L"minidump.mdmp"), "hang report has no minidump\n");
        DeleteReport(&Found);
    }
    else if (StoreAvailable())
    {
        ok(0, "hang report not found\n");
    }

    ExitCode = RunHangChild("noping", Shared, Ready, Started, Clicked, &Elapsed);
    ok_hex(ExitCode, HANG_EXIT_STATUS);
    ok_hex(Shared->Register, S_OK);
    ok(Shared->CallbackCalls <= 1, "CallbackCalls %ld\n", Shared->CallbackCalls);
    ok_long(Shared->AfterWait, 0);
    ok(Elapsed < CHILD_SAFETY_MS / 2, "recovery without ping ran %lu ms\n", Elapsed);

    ExitCode = RunHangChild("none", Shared, Ready, Started, Clicked, &Elapsed);
    ok_hex(ExitCode, HANG_EXIT_STATUS);
    ok_long(Shared->CallbackCalls, 0);

    ExitCode = RunHangChild("cancel", Shared, Ready, Started, Clicked, &Elapsed);
    ok_hex(ExitCode, HANG_EXIT_STATUS);
    ok_hex(Shared->Register, S_OK);
    ok_long(Shared->CallbackCalls, 1);
    ok_hex(Shared->InProgress, S_OK);
    ok_long(Shared->Cancelled, FALSE);
    ok_hex(Shared->InProgressAfterClick, S_OK);
    ok_long(Shared->CancelledAfterClick, TRUE);

    UnmapViewOfFile(Shared);
    CloseHandle(Mapping);
    CloseHandle(Ready);
    CloseHandle(Started);
    CloseHandle(Clicked);
}
