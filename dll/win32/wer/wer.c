/*
 * PROJECT:     LiberNT Windows Error Reporting
 * FILE:        dll/win32/wer/wer.c
 * PURPOSE:     Windows Error Reporting client API
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#define WIN32_NO_STATUS
#include <stdarg.h>
#include <stdlib.h>
#include <windef.h>
#include <winbase.h>
#include <winreg.h>
#include <winver.h>
#include <dbghelp.h>
#include <rpc.h>
#include <strsafe.h>
#include <werapi.h>
#define NTOS_MODE_USER
#include <ndk/rtlfuncs.h>
#include <basewer.h>
#include "werremote.h"

#define WER_SETTINGS_KEY        L"Software\\Microsoft\\Windows\\Windows Error Reporting"
#define WER_EXCLUDED_KEY        WER_SETTINGS_KEY L"\\ExcludedApplications"
#define WER_REPORT_FILE_NAME    L"Report.wer"
#define WER_REPORT_TEXT_MAX     (1024 * 1024)
#define WER_LINE_MAX            1024

typedef BOOL (WINAPI *PFN_MINIDUMP_WRITE_DUMP)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE,
                                               PMINIDUMP_EXCEPTION_INFORMATION,
                                               PMINIDUMP_USER_STREAM_INFORMATION,
                                               PMINIDUMP_CALLBACK_INFORMATION);

typedef struct _WER_REPORT_PARAM
{
    BOOL Set;
    WCHAR Name[WER_MAX_SIGNATURE_NAME_LENGTH + 1];
    WCHAR Value[WER_MAX_PARAM_LENGTH];
} WER_REPORT_PARAM, *PWER_REPORT_PARAM;

typedef struct _WER_REPORT_FILE
{
    LIST_ENTRY Entry;
    WER_FILE_TYPE Type;
    DWORD Flags;
    WCHAR Path[MAX_PATH];
} WER_REPORT_FILE, *PWER_REPORT_FILE;

typedef struct _WER_REPORT_DUMP
{
    LIST_ENTRY Entry;
    HANDLE Process;
    HANDLE Thread;
    WER_DUMP_TYPE Type;
    BOOL HasException;
    WER_EXCEPTION_INFORMATION Exception;
    BOOL HasOptions;
    WER_DUMP_CUSTOM_OPTIONS Options;
    DWORD Flags;
} WER_REPORT_DUMP, *PWER_REPORT_DUMP;

typedef struct _WER_REPORT
{
    LIST_ENTRY Entry;
    WER_REPORT_TYPE Type;
    WCHAR EventType[WER_MAX_EVENT_NAME_LENGTH + 1];
    WER_REPORT_INFORMATION Information;
    HANDLE Process;
    WER_REPORT_PARAM Parameters[WER_MAX_PARAM_COUNT];
    PWSTR UIOptions[WerUIMax];
    LIST_ENTRY Files;
    LIST_ENTRY Dumps;
} WER_REPORT, *PWER_REPORT;

typedef struct _WER_STORE_ITEM
{
    FILETIME Created;
    WCHAR Name[MAX_PATH];
} WER_STORE_ITEM, *PWER_STORE_ITEM;

typedef struct _WER_STORE
{
    REPORT_STORE_TYPES Type;
    WCHAR Path[MAX_PATH];
    PWER_STORE_ITEM Items;
    ULONG ItemCount;
    ULONG NextItem;
    BOOL Listed;
} WER_STORE, *PWER_STORE;

typedef struct _WER_BUFFER
{
    PWSTR Data;
    SIZE_T Length;
    SIZE_T Capacity;
    BOOL Failed;
} WER_BUFFER, *PWER_BUFFER;

typedef struct _WER_DUMP_BLOCKS
{
    PWER_REMOTE_ENTRY Blocks;
    ULONG Count;
    ULONG Next;
} WER_DUMP_BLOCKS, *PWER_DUMP_BLOCKS;

typedef struct _WER_STORED_DATA
{
    PWSTR Path;
    PWER_BUFFER Text;
    ULONG FileIndex;
} WER_STORED_DATA, *PWER_STORED_DATA;

static SRWLOCK WerReportLock = SRWLOCK_INIT;
static LIST_ENTRY WerReports = { &WerReports, &WerReports };

static
PVOID
WerpAlloc(
    _In_ SIZE_T Size)
{
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Size);
}

static
VOID
WerpFree(
    _In_opt_ PVOID Memory)
{
    if (Memory)
        HeapFree(GetProcessHeap(), 0, Memory);
}

static
PCWSTR
WerpFileName(
    _In_ PCWSTR Path)
{
    PCWSTR Name = wcsrchr(Path, L'\\');

    return Name ? Name + 1 : Path;
}

static
DWORD
WerpReadSetting(
    _In_ PCWSTR Name,
    _In_ DWORD Default)
{
    static const HKEY Roots[] = { HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER };
    DWORD Value, Size, Type;
    HKEY Key;
    ULONG Index;
    LONG Error;

    for (Index = 0; Index < ARRAYSIZE(Roots); Index++)
    {
        if (RegOpenKeyExW(Roots[Index], WER_SETTINGS_KEY, 0, KEY_QUERY_VALUE, &Key) != ERROR_SUCCESS)
            continue;

        Size = sizeof(Value);
        Error = RegQueryValueExW(Key, Name, NULL, &Type, (PBYTE)&Value, &Size);
        RegCloseKey(Key);
        if (Error == ERROR_SUCCESS && Type == REG_DWORD && Size == sizeof(Value))
            return Value;
    }

    return Default;
}

static
BOOL
WerpIsExcluded(
    _In_ PCWSTR ImageName)
{
    static const HKEY Roots[] = { HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER };
    ULONG Index;
    HKEY Key;
    LONG Error;

    if (!ImageName[0])
        return FALSE;

    for (Index = 0; Index < ARRAYSIZE(Roots); Index++)
    {
        if (RegOpenKeyExW(Roots[Index], WER_EXCLUDED_KEY, 0, KEY_QUERY_VALUE, &Key) != ERROR_SUCCESS)
            continue;

        Error = RegQueryValueExW(Key, ImageName, NULL, NULL, NULL, NULL);
        RegCloseKey(Key);
        if (Error == ERROR_SUCCESS)
            return TRUE;
    }

    return FALSE;
}

static
BOOL
WerpExpandRoot(
    _In_ PCWSTR Variable,
    _In_ PCWSTR Fallback,
    _Out_writes_(Size) PWSTR Path,
    _In_ DWORD Size)
{
    DWORD Length = ExpandEnvironmentStringsW(Variable, Path, Size);

    if (Length && Length <= Size && Path[0] != L'%')
        return TRUE;

    Length = ExpandEnvironmentStringsW(Fallback, Path, Size);
    return Length && Length <= Size && Path[0] != L'%';
}

static
BOOL
WerpGetStorePath(
    _In_ REPORT_STORE_TYPES Type,
    _Out_writes_(Size) PWSTR Path,
    _In_ DWORD Size)
{
    BOOL Machine = (Type == E_STORE_MACHINE_ARCHIVE || Type == E_STORE_MACHINE_QUEUE);
    BOOL Queue = (Type == E_STORE_USER_QUEUE || Type == E_STORE_MACHINE_QUEUE);
    BOOL Expanded;

    Expanded = Machine ? WerpExpandRoot(L"%ProgramData%", L"%SystemDrive%\\ProgramData", Path, Size) :
                         WerpExpandRoot(L"%LOCALAPPDATA%", L"%USERPROFILE%\\AppData\\Local", Path, Size);
    if (!Expanded)
        return FALSE;

    return SUCCEEDED(StringCchCatW(Path, Size, Queue ? L"\\Microsoft\\Windows\\WER\\ReportQueue" :
                                                       L"\\Microsoft\\Windows\\WER\\ReportArchive"));
}

static
BOOL
WerpCreateDirectoryTree(
    _In_ PCWSTR Path)
{
    WCHAR Partial[MAX_PATH];
    PWSTR Cursor;

    if (FAILED(StringCchCopyW(Partial, ARRAYSIZE(Partial), Path)))
        return FALSE;

    for (Cursor = wcschr(Partial, L'\\'); Cursor; Cursor = wcschr(Cursor + 1, L'\\'))
    {
        if (Cursor - Partial <= 2)
            continue;

        *Cursor = UNICODE_NULL;
        CreateDirectoryW(Partial, NULL);
        *Cursor = L'\\';
    }

    return CreateDirectoryW(Partial, NULL) || GetLastError() == ERROR_ALREADY_EXISTS;
}

static
VOID
WerpDeleteReportDirectory(
    _In_ PCWSTR Path)
{
    WCHAR Pattern[MAX_PATH], File[MAX_PATH];
    WIN32_FIND_DATAW Data;
    HANDLE Find;

    if (FAILED(StringCchPrintfW(Pattern, ARRAYSIZE(Pattern), L"%s\\*", Path)))
        return;

    Find = FindFirstFileW(Pattern, &Data);
    if (Find != INVALID_HANDLE_VALUE)
    {
        do
        {
            if (!(Data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                SUCCEEDED(StringCchPrintfW(File, ARRAYSIZE(File), L"%s\\%s", Path, Data.cFileName)))
            {
                DeleteFileW(File);
            }
        } while (FindNextFileW(Find, &Data));
        FindClose(Find);
    }

    RemoveDirectoryW(Path);
}

static
int
__cdecl
WerpCompareItems(
    _In_ const void *Left,
    _In_ const void *Right)
{
    return CompareFileTime(&((const WER_STORE_ITEM *)Left)->Created, &((const WER_STORE_ITEM *)Right)->Created);
}

static
ULONG
WerpListStore(
    _In_ PCWSTR StorePath,
    _Out_ PWER_STORE_ITEM *Items)
{
    WCHAR Pattern[MAX_PATH], ReportFile[MAX_PATH];
    PWER_STORE_ITEM List = NULL, Grown;
    ULONG Count = 0, Capacity = 0;
    WIN32_FIND_DATAW Data;
    HANDLE Find;

    *Items = NULL;
    if (FAILED(StringCchPrintfW(Pattern, ARRAYSIZE(Pattern), L"%s\\*", StorePath)))
        return 0;

    Find = FindFirstFileW(Pattern, &Data);
    if (Find == INVALID_HANDLE_VALUE)
        return 0;

    do
    {
        if (!(Data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || Data.cFileName[0] == L'.')
            continue;

        if (FAILED(StringCchPrintfW(ReportFile, ARRAYSIZE(ReportFile), L"%s\\%s\\%s",
                                    StorePath, Data.cFileName, WER_REPORT_FILE_NAME)) ||
            GetFileAttributesW(ReportFile) == INVALID_FILE_ATTRIBUTES)
        {
            continue;
        }

        if (Count == Capacity)
        {
            Capacity = Capacity ? Capacity * 2 : 16;
            Grown = List ? HeapReAlloc(GetProcessHeap(), 0, List, Capacity * sizeof(*List)) :
                           HeapAlloc(GetProcessHeap(), 0, Capacity * sizeof(*List));
            if (!Grown)
                break;
            List = Grown;
        }

        List[Count].Created = Data.ftCreationTime;
        StringCchCopyW(List[Count].Name, ARRAYSIZE(List[Count].Name), Data.cFileName);
        Count++;
    } while (FindNextFileW(Find, &Data));
    FindClose(Find);

    if (Count)
        qsort(List, Count, sizeof(*List), WerpCompareItems);

    *Items = List;
    return Count;
}

static
VOID
WerpPruneStore(
    _In_ PCWSTR StorePath,
    _In_ DWORD MaxCount)
{
    WCHAR Path[MAX_PATH];
    PWER_STORE_ITEM Items;
    ULONG Count, Index;

    Count = WerpListStore(StorePath, &Items);
    for (Index = 0; Count > MaxCount && Index < Count - MaxCount; Index++)
    {
        if (SUCCEEDED(StringCchPrintfW(Path, ARRAYSIZE(Path), L"%s\\%s", StorePath, Items[Index].Name)))
            WerpDeleteReportDirectory(Path);
    }

    WerpFree(Items);
}

static
VOID
WerpAppendText(
    _Inout_ PWER_BUFFER Buffer,
    _In_ PCWSTR Text,
    _In_ BOOL Value)
{
    SIZE_T Length = wcslen(Text), Index, Capacity;
    PWSTR Grown;

    if (Buffer->Failed)
        return;

    if (Buffer->Length + Length + 1 > Buffer->Capacity)
    {
        Capacity = max(Buffer->Capacity * 2, Buffer->Length + Length + 1024);
        Grown = Buffer->Data ? HeapReAlloc(GetProcessHeap(), 0, Buffer->Data, Capacity * sizeof(WCHAR)) :
                               HeapAlloc(GetProcessHeap(), 0, Capacity * sizeof(WCHAR));
        if (!Grown)
        {
            Buffer->Failed = TRUE;
            return;
        }
        Buffer->Data = Grown;
        Buffer->Capacity = Capacity;
    }

    for (Index = 0; Index < Length; Index++)
        Buffer->Data[Buffer->Length + Index] = (Value && (Text[Index] == L'\r' || Text[Index] == L'\n')) ? L' ' : Text[Index];
    Buffer->Length += Length;
    Buffer->Data[Buffer->Length] = UNICODE_NULL;
}

static
VOID
WerpAppendPair(
    _Inout_ PWER_BUFFER Buffer,
    _In_ PCWSTR Value,
    _In_ PCWSTR KeyFormat,
    ...)
{
    WCHAR Key[WER_LINE_MAX];
    va_list Arguments;
    HRESULT Result;

    va_start(Arguments, KeyFormat);
    Result = StringCchVPrintfW(Key, ARRAYSIZE(Key), KeyFormat, Arguments);
    va_end(Arguments);
    if (FAILED(Result))
    {
        Buffer->Failed = TRUE;
        return;
    }

    WerpAppendText(Buffer, Key, FALSE);
    WerpAppendText(Buffer, L"=", FALSE);
    WerpAppendText(Buffer, Value, TRUE);
    WerpAppendText(Buffer, L"\r\n", FALSE);
}

static
VOID
WerpAppendNumber(
    _Inout_ PWER_BUFFER Buffer,
    _In_ PCWSTR Key,
    _In_ ULONGLONG Number)
{
    WCHAR Value[24];

    StringCchPrintfW(Value, ARRAYSIZE(Value), L"%I64u", Number);
    WerpAppendPair(Buffer, Value, L"%s", Key);
}

static
BOOL
WerpWriteTextFile(
    _In_ PCWSTR Path,
    _In_ PWER_BUFFER Buffer)
{
    static const WCHAR ByteOrderMark = 0xFEFF;
    DWORD Written;
    HANDLE File;
    BOOL Success;

    if (Buffer->Failed || !Buffer->Data)
        return FALSE;

    File = CreateFileW(Path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (File == INVALID_HANDLE_VALUE)
        return FALSE;

    Success = WriteFile(File, &ByteOrderMark, sizeof(ByteOrderMark), &Written, NULL) &&
              WriteFile(File, Buffer->Data, (DWORD)(Buffer->Length * sizeof(WCHAR)), &Written, NULL);
    CloseHandle(File);
    return Success;
}

static
PFN_MINIDUMP_WRITE_DUMP
WerpGetMiniDumpWriteDump(VOID)
{
    static PFN_MINIDUMP_WRITE_DUMP WriteDump;
    HMODULE DbgHelp;

    if (!ReadPointerAcquire((PVOID *)&WriteDump))
    {
        DbgHelp = LoadLibraryW(L"dbghelp.dll");
        if (DbgHelp)
            InterlockedExchangePointer((PVOID *)&WriteDump, (PVOID)GetProcAddress(DbgHelp, "MiniDumpWriteDump"));
    }

    return WriteDump;
}

static
BOOL
CALLBACK
WerpDumpCallback(
    _In_ PVOID Parameter,
    _In_ const PMINIDUMP_CALLBACK_INPUT Input,
    _Inout_ PMINIDUMP_CALLBACK_OUTPUT Output)
{
    PWER_DUMP_BLOCKS Blocks = Parameter;
    PWER_REMOTE_ENTRY Block;

    switch (Input->CallbackType)
    {
        case ModuleCallback:
        case ThreadCallback:
        case ThreadExCallback:
        case IncludeThreadCallback:
        case IncludeModuleCallback:
            return TRUE;

        case MemoryCallback:
            while (Blocks->Next < Blocks->Count)
            {
                Block = &Blocks->Blocks[Blocks->Next++];
                if (Block->Size && Block->Size <= WER_MAX_MEM_BLOCK_SIZE)
                {
                    Output->MemoryBase = Block->Address;
                    Output->MemorySize = Block->Size;
                    return TRUE;
                }
            }
            return FALSE;

        case ReadMemoryFailureCallback:
            Output->Status = S_OK;
            return TRUE;

        default:
            return FALSE;
    }
}

static
MINIDUMP_TYPE
WerpDumpFlags(
    _In_ WER_DUMP_TYPE Type)
{
    switch (Type)
    {
        case WerDumpTypeMicroDump:
            return MiniDumpWithDataSegs | MiniDumpWithUnloadedModules | MiniDumpWithProcessThreadData |
                   MiniDumpWithoutOptionalData;

        case WerDumpTypeHeapDump:
            return MiniDumpWithDataSegs | MiniDumpWithProcessThreadData | MiniDumpWithHandleData |
                   MiniDumpWithPrivateReadWriteMemory | MiniDumpWithUnloadedModules | MiniDumpWithFullMemoryInfo |
                   MiniDumpWithThreadInfo | MiniDumpWithTokenInformation | MiniDumpWithPrivateWriteCopyMemory;

        default:
            /* FIXME: a triage dump is written with the minidump options (WER_DUMP_TYPE gives no MINIDUMP_TYPE set for it) */
            return MiniDumpWithDataSegs | MiniDumpWithUnloadedModules | MiniDumpWithProcessThreadData |
                   MiniDumpWithTokenInformation;
    }
}

static
WER_FILE_TYPE
WerpDumpFileType(
    _In_ WER_DUMP_TYPE Type)
{
    switch (Type)
    {
        case WerDumpTypeMicroDump: return WerFileTypeMicrodump;
        case WerDumpTypeHeapDump: return WerFileTypeHeapdump;
        case WerDumpTypeTriageDump: return WerFileTypeTriagedump;
        default: return WerFileTypeMinidump;
    }
}

static
PCWSTR
WerpDumpFileName(
    _In_ WER_DUMP_TYPE Type)
{
    switch (Type)
    {
        case WerDumpTypeMicroDump: return L"microdump.mdmp";
        case WerDumpTypeHeapDump: return L"memory.hdmp";
        case WerDumpTypeTriageDump: return L"triagedump.mdmp";
        default: return L"minidump.mdmp";
    }
}

static
HANDLE
WerpCreateDataFile(
    _In_ PWER_STORED_DATA Stored,
    _In_ PCWSTR Name,
    _Out_writes_(MAX_PATH) PWSTR Path,
    _Out_ PCWSTR *StoredName)
{
    HANDLE File;
    ULONG Attempt;

    for (Attempt = 0; Attempt < 16; Attempt++)
    {
        if (Attempt == 0 ? FAILED(StringCchPrintfW(Path, MAX_PATH, L"%s\\%s", Stored->Path, Name)) :
                           FAILED(StringCchPrintfW(Path, MAX_PATH, L"%s\\%lu_%s", Stored->Path, Attempt, Name)))
        {
            return INVALID_HANDLE_VALUE;
        }

        File = CreateFileW(Path, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        if (File != INVALID_HANDLE_VALUE)
        {
            *StoredName = WerpFileName(Path);
            return File;
        }

        if (GetLastError() != ERROR_FILE_EXISTS && GetLastError() != ERROR_ALREADY_EXISTS)
            return INVALID_HANDLE_VALUE;
    }

    return INVALID_HANDLE_VALUE;
}

static
VOID
WerpRecordFile(
    _In_ PWER_STORED_DATA Stored,
    _In_ PCWSTR StoredName,
    _In_ WER_FILE_TYPE Type,
    _In_ DWORD Flags,
    _In_opt_ PCWSTR OriginalPath)
{
    WCHAR Number[16];
    ULONG Index = Stored->FileIndex++;

    WerpAppendPair(Stored->Text, StoredName, L"File[%lu].CabName", Index);
    WerpAppendPair(Stored->Text, StoredName, L"File[%lu].Path", Index);
    StringCchPrintfW(Number, ARRAYSIZE(Number), L"%lu", Flags);
    WerpAppendPair(Stored->Text, Number, L"File[%lu].Flags", Index);
    StringCchPrintfW(Number, ARRAYSIZE(Number), L"%lu", (ULONG)Type);
    WerpAppendPair(Stored->Text, Number, L"File[%lu].Type", Index);
    if (OriginalPath)
        WerpAppendPair(Stored->Text, OriginalPath, L"File[%lu].Original.Path", Index);
}

static
VOID
WerpStoreDump(
    _In_ PWER_STORED_DATA Stored,
    _In_ PWER_REPORT_DUMP Dump,
    _In_reads_(BlockCount) PWER_REMOTE_ENTRY Blocks,
    _In_ ULONG BlockCount)
{
    PFN_MINIDUMP_WRITE_DUMP WriteDump = WerpGetMiniDumpWriteDump();
    MINIDUMP_EXCEPTION_INFORMATION Exception, *ExceptionParam = NULL;
    MINIDUMP_CALLBACK_INFORMATION Callback, *CallbackParam = NULL;
    WER_DUMP_BLOCKS State = { Blocks, BlockCount, 0 };
    WCHAR Path[MAX_PATH];
    PCWSTR StoredName;
    MINIDUMP_TYPE Type;
    DWORD ThreadId = 0;
    HANDLE File;
    BOOL Success;

    if (!WriteDump)
        return;

    Type = WerpDumpFlags(Dump->Type);
    if (Dump->HasOptions && (Dump->Options.dwMask & WER_DUMP_MASK_DUMPTYPE))
        Type = Dump->Options.dwDumpFlags;
    /* TODO: the thread and module masks of WER_DUMP_CUSTOM_OPTIONS are not applied */

    if (Dump->HasException && Dump->Exception.pExceptionPointers)
    {
        if (Dump->Thread)
            ThreadId = GetThreadId(Dump->Thread);
        else if (GetProcessId(Dump->Process) == GetCurrentProcessId())
            ThreadId = GetCurrentThreadId();

        if (ThreadId)
        {
            Exception.ThreadId = ThreadId;
            Exception.ExceptionPointers = Dump->Exception.pExceptionPointers;
            Exception.ClientPointers = Dump->Exception.bClientPointers;
            ExceptionParam = &Exception;
        }
    }

    if (Dump->Type == WerDumpTypeHeapDump && BlockCount)
    {
        Callback.CallbackRoutine = WerpDumpCallback;
        Callback.CallbackParam = &State;
        CallbackParam = &Callback;
    }

    File = WerpCreateDataFile(Stored, WerpDumpFileName(Dump->Type), Path, &StoredName);
    if (File == INVALID_HANDLE_VALUE)
        return;

    Success = WriteDump(Dump->Process, GetProcessId(Dump->Process), File, Type, ExceptionParam, NULL, CallbackParam);
    CloseHandle(File);
    if (!Success)
    {
        DeleteFileW(Path);
        return;
    }

    WerpRecordFile(Stored, StoredName, WerpDumpFileType(Dump->Type), WER_FILE_ANONYMOUS_DATA, NULL);
}

static
VOID
WerpStoreFile(
    _In_ PWER_STORED_DATA Stored,
    _In_ PCWSTR Source,
    _In_ WER_FILE_TYPE Type,
    _In_ DWORD Flags)
{
    WCHAR Path[MAX_PATH];
    PCWSTR StoredName;
    HANDLE File;

    File = WerpCreateDataFile(Stored, WerpFileName(Source), Path, &StoredName);
    if (File == INVALID_HANDLE_VALUE)
        return;
    CloseHandle(File);

    if (!CopyFileW(Source, Path, FALSE))
    {
        DeleteFileW(Path);
        return;
    }

    WerpRecordFile(Stored, StoredName, Type, Flags & 0xFFFF, Source);
    if (Flags & WER_FILE_DELETE_WHEN_DONE)
        DeleteFileW(Source);
}

static
VOID
WerpResolveApplication(
    _In_ PWER_REPORT Report,
    _Out_writes_(MAX_PATH) PWSTR Path,
    _Out_ PCWSTR *Name)
{
    DWORD Size = MAX_PATH;

    Path[0] = UNICODE_NULL;
    if (Report->Information.wzApplicationPath[0])
        StringCchCopyW(Path, MAX_PATH, Report->Information.wzApplicationPath);
    else if (!QueryFullProcessImageNameW(Report->Process ? Report->Process : GetCurrentProcess(), 0, Path, &Size))
        Path[0] = UNICODE_NULL;

    *Name = Report->Information.wzApplicationName[0] ? Report->Information.wzApplicationName : WerpFileName(Path);
}

static
PCWSTR
WerpReportKind(
    _In_ WER_REPORT_TYPE Type)
{
    switch (Type)
    {
        case WerReportNonCritical: return L"NonCritical";
        case WerReportCritical: return L"Critical";
        case WerReportApplicationCrash: return L"AppCrash";
        case WerReportApplicationHang: return L"AppHang";
        default: return L"Kernel";
    }
}

static
BOOL
WerpCreateReportId(
    _Out_writes_(40) PWSTR Id)
{
    RPC_WSTR Text;
    UUID Uuid;
    HRESULT Result;

    if (UuidCreate(&Uuid) != RPC_S_OK || UuidToStringW(&Uuid, &Text) != RPC_S_OK)
        return FALSE;

    Result = StringCchCopyW(Id, 40, (PCWSTR)Text);
    RpcStringFreeW(&Text);
    return SUCCEEDED(Result);
}

static
BOOL
WerpCreateReportDirectory(
    _In_ PWER_REPORT Report,
    _Inout_ REPORT_STORE_TYPES *Store,
    _In_ PCWSTR Id,
    _Out_writes_(MAX_PATH) PWSTR StorePath,
    _Out_writes_(MAX_PATH) PWSTR ReportPath)
{
    WCHAR Event[WER_MAX_EVENT_NAME_LENGTH + 1];
    ULONG Attempt, Index;

    StringCchCopyW(Event, ARRAYSIZE(Event), Report->EventType);
    for (Index = 0; Event[Index]; Index++)
    {
        if (wcschr(L"\\/:*?\"<>|", Event[Index]) || Event[Index] < L' ')
            Event[Index] = L'_';
    }

    for (Attempt = 0; Attempt < 2; Attempt++)
    {
        if (WerpGetStorePath(*Store, StorePath, MAX_PATH) && WerpCreateDirectoryTree(StorePath) &&
            SUCCEEDED(StringCchPrintfW(ReportPath, MAX_PATH, L"%s\\%s_%s_%s",
                                       StorePath, WerpReportKind(Report->Type), Event, Id)) &&
            CreateDirectoryW(ReportPath, NULL))
        {
            return TRUE;
        }

        if (*Store == E_STORE_MACHINE_QUEUE)
            *Store = E_STORE_USER_QUEUE;
        else if (*Store == E_STORE_MACHINE_ARCHIVE)
            *Store = E_STORE_USER_ARCHIVE;
        else
            return FALSE;
    }

    return FALSE;
}

static
HRESULT
WerpStoreReport(
    _In_ PWER_REPORT Report,
    _In_ REPORT_STORE_TYPES Store,
    _In_ WER_CONSENT Consent,
    _In_ DWORD Flags,
    _In_ BOOL KeepData)
{
    WCHAR StorePath[MAX_PATH], ReportPath[MAX_PATH], FilePath[MAX_PATH], Id[40], AppPath[MAX_PATH];
    HANDLE Process = Report->Process ? Report->Process : GetCurrentProcess();
    PWER_REMOTE_ENTRY Files = NULL, Blocks = NULL;
    ULONG FileCount = 0, BlockCount = 0, Index;
    BASE_WER_REGISTRATION Registration;
    DWORD RegisteredFlags = 0;
    WER_BUFFER Text = { 0 };
    WER_STORED_DATA Stored;
    ULONG64 Address;
    PCWSTR AppName;
    PLIST_ENTRY Link;
    PWER_REPORT_FILE File;
    PWER_REPORT_DUMP Dump;
    FILETIME Now;
    BOOL Queued, Success;
    ULARGE_INTEGER Time;

    if (!WerpCreateReportId(Id))
        return E_FAIL;

    if (!WerpCreateReportDirectory(Report, &Store, Id, StorePath, ReportPath))
        return HRESULT_FROM_WIN32(GetLastError());

    Queued = (Store == E_STORE_USER_QUEUE || Store == E_STORE_MACHINE_QUEUE);
    WerpResolveApplication(Report, AppPath, &AppName);
    GetSystemTimeAsFileTime(&Now);
    Time.LowPart = Now.dwLowDateTime;
    Time.HighPart = Now.dwHighDateTime;

    WerpAppendNumber(&Text, L"Version", 1);
    WerpAppendPair(&Text, Report->EventType, L"EventType");
    WerpAppendNumber(&Text, L"EventTime", Time.QuadPart);
    WerpAppendNumber(&Text, L"ReportType", Report->Type);
    WerpAppendNumber(&Text, L"Consent", Consent);
    WerpAppendPair(&Text, Id, L"ReportIdentifier");
    for (Index = 0; Index < WER_MAX_PARAM_COUNT; Index++)
    {
        if (!Report->Parameters[Index].Set)
            continue;
        WerpAppendPair(&Text, Report->Parameters[Index].Name, L"Sig[%lu].Name", Index);
        WerpAppendPair(&Text, Report->Parameters[Index].Value, L"Sig[%lu].Value", Index);
    }
    for (Index = 0; Index < WerUIMax; Index++)
    {
        if (Report->UIOptions[Index])
            WerpAppendPair(&Text, Report->UIOptions[Index], L"UI[%lu]", Index);
    }
    WerpAppendPair(&Text, Report->Information.wzFriendlyEventName[0] ? Report->Information.wzFriendlyEventName :
                                                                      Report->EventType, L"FriendlyEventName");
    WerpAppendPair(&Text, Report->Information.wzConsentKey[0] ? Report->Information.wzConsentKey :
                                                               Report->EventType, L"ConsentKey");
    WerpAppendPair(&Text, AppName, L"AppName");
    WerpAppendPair(&Text, AppPath, L"AppPath");
    if (Report->Information.wzDescription[0])
        WerpAppendPair(&Text, Report->Information.wzDescription, L"ReportDescription");

    if (KeepData && (Flags & WER_SUBMIT_ADD_REGISTERED_DATA) &&
        WerRemoteReadRegistration(Process, &Address, &Registration) == S_OK)
    {
        WerRemoteReadEntries(Process, Registration.Files, WER_MAX_REGISTERED_ENTRIES, &Files, &FileCount);
        WerRemoteReadEntries(Process, Registration.MemoryBlocks, WER_MAX_REGISTERED_ENTRIES, &Blocks, &BlockCount);
        if (Registration.State & BASE_WER_FLAGS_SET)
            RegisteredFlags = Registration.Flags;
    }

    Stored.Path = ReportPath;
    Stored.Text = &Text;
    Stored.FileIndex = 0;
    if (KeepData)
    {
        for (Link = Report->Dumps.Flink; Link != &Report->Dumps; Link = Link->Flink)
        {
            Dump = CONTAINING_RECORD(Link, WER_REPORT_DUMP, Entry);
            if (Dump->Type == WerDumpTypeHeapDump &&
                ((RegisteredFlags & WER_FAULT_REPORTING_FLAG_NOHEAP) ||
                 (Queued && ((Dump->Flags & WER_DUMP_NOHEAP_ONQUEUE) ||
                             (RegisteredFlags & WER_FAULT_REPORTING_FLAG_NO_HEAP_ON_QUEUE)))))
            {
                continue;
            }
            WerpStoreDump(&Stored, Dump, Blocks, BlockCount);
        }

        for (Link = Report->Files.Flink; Link != &Report->Files; Link = Link->Flink)
        {
            File = CONTAINING_RECORD(Link, WER_REPORT_FILE, Entry);
            WerpStoreFile(&Stored, File->Path, File->Type, File->Flags);
        }

        for (Index = 0; Index < FileCount; Index++)
        {
            if (Files[Index].Name[0])
            {
                WerpStoreFile(&Stored, Files[Index].Name,
                              Files[Index].Type == WerRegFileTypeUserDocument ? WerFileTypeUserDocument : WerFileTypeOther,
                              Files[Index].Flags);
            }
        }
    }

    WerpFree(Files);
    WerpFree(Blocks);

    Success = SUCCEEDED(StringCchPrintfW(FilePath, ARRAYSIZE(FilePath), L"%s\\%s", ReportPath, WER_REPORT_FILE_NAME)) &&
              WerpWriteTextFile(FilePath, &Text);
    WerpFree(Text.Data);
    if (!Success)
    {
        WerpDeleteReportDirectory(ReportPath);
        return E_FAIL;
    }

    WerpPruneStore(StorePath, Queued ? min(max(WerpReadSetting(L"MaxQueueCount", 50), 1), 500) :
                                       min(max(WerpReadSetting(L"MaxArchiveCount", 1000), 1), 5000));
    return S_OK;
}

static
PWER_REPORT
WerpFindReport(
    _In_opt_ HREPORT Handle)
{
    PLIST_ENTRY Link;

    if (!Handle)
        return NULL;

    for (Link = WerReports.Flink; Link != &WerReports; Link = Link->Flink)
    {
        if (CONTAINING_RECORD(Link, WER_REPORT, Entry) == (PWER_REPORT)Handle)
            return (PWER_REPORT)Handle;
    }

    return NULL;
}

static
VOID
WerpDestroyReport(
    _In_ PWER_REPORT Report)
{
    PWER_REPORT_DUMP Dump;
    ULONG Index;

    while (!IsListEmpty(&Report->Files))
        WerpFree(CONTAINING_RECORD(RemoveHeadList(&Report->Files), WER_REPORT_FILE, Entry));

    while (!IsListEmpty(&Report->Dumps))
    {
        Dump = CONTAINING_RECORD(RemoveHeadList(&Report->Dumps), WER_REPORT_DUMP, Entry);
        CloseHandle(Dump->Process);
        if (Dump->Thread)
            CloseHandle(Dump->Thread);
        WerpFree(Dump);
    }

    for (Index = 0; Index < WerUIMax; Index++)
        WerpFree(Report->UIOptions[Index]);

    if (Report->Process)
        CloseHandle(Report->Process);
    WerpFree(Report);
}

HRESULT WINAPI WerAddExcludedApplication(PCWSTR pwzExeName, BOOL bAllUsers)
{
    PCWSTR Name;
    DWORD Value = 1;
    HKEY Key;

    if (!pwzExeName || !pwzExeName[0])
        return E_INVALIDARG;

    Name = WerpFileName(pwzExeName);
    if (!Name[0])
        return E_INVALIDARG;

    if (RegCreateKeyExW(bAllUsers ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER, WER_EXCLUDED_KEY, 0, NULL, 0,
                        KEY_SET_VALUE, NULL, &Key, NULL) != ERROR_SUCCESS)
    {
        return E_ACCESSDENIED;
    }

    RegSetValueExW(Key, Name, 0, REG_DWORD, (const BYTE *)&Value, sizeof(Value));
    RegCloseKey(Key);
    return S_OK;
}

HRESULT WINAPI WerRemoveExcludedApplication(PCWSTR pwzExeName, BOOL bAllUsers)
{
    PCWSTR Name;
    LONG Error;
    HKEY Key;

    if (!pwzExeName || !pwzExeName[0])
        return E_INVALIDARG;

    Name = WerpFileName(pwzExeName);
    if (!Name[0])
        return E_INVALIDARG;

    if (RegCreateKeyExW(bAllUsers ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER, WER_EXCLUDED_KEY, 0, NULL, 0,
                        KEY_SET_VALUE, NULL, &Key, NULL) != ERROR_SUCCESS)
    {
        return E_ACCESSDENIED;
    }

    Error = RegDeleteValueW(Key, Name);
    RegCloseKey(Key);
    return Error ? HRESULT_FROM_WIN32(ERROR_ENVVAR_NOT_FOUND) : S_OK;
}

HRESULT WINAPI WerReportCreate(PCWSTR pwzEventType, WER_REPORT_TYPE repType,
                               PWER_REPORT_INFORMATION pReportInformation, HREPORT *phReportHandle)
{
    HANDLE Process = NULL;
    PWER_REPORT Report;

    if (phReportHandle)
        *phReportHandle = NULL;

    if (!pwzEventType || !pwzEventType[0] || !phReportHandle || (ULONG)repType >= WerReportInvalid)
        return E_INVALIDARG;

    if (wcslen(pwzEventType) > WER_MAX_EVENT_NAME_LENGTH)
        return WER_E_LENGTH_EXCEEDED;

    if (pReportInformation && pReportInformation->dwSize < sizeof(*pReportInformation))
        return E_INVALIDARG;

    if (pReportInformation && pReportInformation->hProcess &&
        !DuplicateHandle(GetCurrentProcess(), pReportInformation->hProcess, GetCurrentProcess(), &Process,
                         0, FALSE, DUPLICATE_SAME_ACCESS))
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    Report = WerpAlloc(sizeof(*Report));
    if (!Report)
    {
        if (Process)
            CloseHandle(Process);
        return E_OUTOFMEMORY;
    }

    Report->Type = repType;
    StringCchCopyW(Report->EventType, ARRAYSIZE(Report->EventType), pwzEventType);
    Report->Process = Process;
    InitializeListHead(&Report->Files);
    InitializeListHead(&Report->Dumps);
    if (pReportInformation)
    {
        Report->Information = *pReportInformation;
        Report->Information.hProcess = NULL;
        Report->Information.wzConsentKey[ARRAYSIZE(Report->Information.wzConsentKey) - 1] = UNICODE_NULL;
        Report->Information.wzFriendlyEventName[ARRAYSIZE(Report->Information.wzFriendlyEventName) - 1] = UNICODE_NULL;
        Report->Information.wzApplicationName[ARRAYSIZE(Report->Information.wzApplicationName) - 1] = UNICODE_NULL;
        Report->Information.wzApplicationPath[ARRAYSIZE(Report->Information.wzApplicationPath) - 1] = UNICODE_NULL;
        Report->Information.wzDescription[ARRAYSIZE(Report->Information.wzDescription) - 1] = UNICODE_NULL;
    }

    AcquireSRWLockExclusive(&WerReportLock);
    InsertHeadList(&WerReports, &Report->Entry);
    ReleaseSRWLockExclusive(&WerReportLock);

    *phReportHandle = Report;
    return S_OK;
}

HRESULT WINAPI WerReportCloseHandle(HREPORT hReportHandle)
{
    PWER_REPORT Report;

    AcquireSRWLockExclusive(&WerReportLock);
    Report = WerpFindReport(hReportHandle);
    if (Report)
        RemoveEntryList(&Report->Entry);
    ReleaseSRWLockExclusive(&WerReportLock);

    if (!Report)
        return E_INVALIDARG;

    WerpDestroyReport(Report);
    return S_OK;
}

HRESULT WINAPI WerReportSetParameter(HREPORT hReportHandle, DWORD dwparamID, PCWSTR pwzName, PCWSTR pwzValue)
{
    PWER_REPORT_PARAM Parameter;
    PWER_REPORT Report;

    AcquireSRWLockExclusive(&WerReportLock);
    Report = WerpFindReport(hReportHandle);
    ReleaseSRWLockExclusive(&WerReportLock);
    if (!Report)
        return E_HANDLE;

    if (dwparamID >= WER_MAX_PARAM_COUNT || !pwzValue)
        return E_INVALIDARG;

    if ((pwzName && wcslen(pwzName) > WER_MAX_SIGNATURE_NAME_LENGTH) || wcslen(pwzValue) >= WER_MAX_PARAM_LENGTH)
        return WER_E_LENGTH_EXCEEDED;

    AcquireSRWLockExclusive(&WerReportLock);
    Report = WerpFindReport(hReportHandle);
    if (!Report)
    {
        ReleaseSRWLockExclusive(&WerReportLock);
        return E_HANDLE;
    }

    Parameter = &Report->Parameters[dwparamID];
    if (pwzName)
        StringCchCopyW(Parameter->Name, ARRAYSIZE(Parameter->Name), pwzName);
    else
        StringCchPrintfW(Parameter->Name, ARRAYSIZE(Parameter->Name), L"P%lu", dwparamID);
    StringCchCopyW(Parameter->Value, ARRAYSIZE(Parameter->Value), pwzValue);
    Parameter->Set = TRUE;
    ReleaseSRWLockExclusive(&WerReportLock);
    return S_OK;
}

HRESULT WINAPI WerReportSetUIOption(HREPORT hReportHandle, WER_REPORT_UI repUITypeID, PCWSTR pwzValue)
{
    PWER_REPORT Report;
    SIZE_T Size;
    PWSTR Value;

    if ((ULONG)repUITypeID < WerUIAdditionalDataDlgHeader || (ULONG)repUITypeID >= WerUIMax || !pwzValue)
        return E_INVALIDARG;

    Size = (wcslen(pwzValue) + 1) * sizeof(WCHAR);
    Value = WerpAlloc(Size);
    if (!Value)
        return E_OUTOFMEMORY;
    RtlCopyMemory(Value, pwzValue, Size);

    AcquireSRWLockExclusive(&WerReportLock);
    Report = WerpFindReport(hReportHandle);
    if (!Report)
    {
        ReleaseSRWLockExclusive(&WerReportLock);
        WerpFree(Value);
        return E_INVALIDARG;
    }

    WerpFree(Report->UIOptions[repUITypeID]);
    Report->UIOptions[repUITypeID] = Value;
    ReleaseSRWLockExclusive(&WerReportLock);
    return S_OK;
}

HRESULT WINAPI WerReportAddFile(HREPORT hReportHandle, PCWSTR pwzPath, WER_FILE_TYPE repFileType, DWORD dwFileFlags)
{
    PWER_REPORT_FILE File;
    PWER_REPORT Report;
    DWORD Length, Attributes;
    WCHAR Path[MAX_PATH];

    if (!pwzPath || !pwzPath[0] || (ULONG)repFileType < WerFileTypeMicrodump || (ULONG)repFileType >= WerFileTypeMax)
        return E_INVALIDARG;

    Length = ExpandEnvironmentStringsW(pwzPath, Path, ARRAYSIZE(Path));
    if (!Length || Length > ARRAYSIZE(Path))
        return WER_E_LENGTH_EXCEEDED;

    Attributes = GetFileAttributesW(Path);
    if (Attributes == INVALID_FILE_ATTRIBUTES || (Attributes & FILE_ATTRIBUTE_DIRECTORY))
        return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);

    if (repFileType == WerFileTypeUserDocument && (Attributes & FILE_ATTRIBUTE_ENCRYPTED))
        return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);

    File = WerpAlloc(sizeof(*File));
    if (!File)
        return E_OUTOFMEMORY;

    File->Type = repFileType;
    File->Flags = dwFileFlags & 0xFFFF;
    StringCchCopyW(File->Path, ARRAYSIZE(File->Path), Path);

    AcquireSRWLockExclusive(&WerReportLock);
    Report = WerpFindReport(hReportHandle);
    if (Report)
        InsertTailList(&Report->Files, &File->Entry);
    ReleaseSRWLockExclusive(&WerReportLock);

    if (!Report)
    {
        WerpFree(File);
        return E_INVALIDARG;
    }

    return S_OK;
}

HRESULT WINAPI WerReportAddDump(HREPORT hReportHandle, HANDLE hProcess, HANDLE hThread, WER_DUMP_TYPE dumpType,
                                PWER_EXCEPTION_INFORMATION pExceptionParam,
                                PWER_DUMP_CUSTOM_OPTIONS pDumpCustomOptions, DWORD dwFlags)
{
    PWER_REPORT_DUMP Dump;
    PWER_REPORT Report;
    HRESULT Result;

    if (!hProcess || (ULONG)dumpType < WerDumpTypeMicroDump || (ULONG)dumpType >= WerDumpTypeMax ||
        (dumpType == WerDumpTypeMicroDump && !hThread) ||
        (pDumpCustomOptions && pDumpCustomOptions->dwSize < sizeof(*pDumpCustomOptions)))
    {
        return E_INVALIDARG;
    }

    Dump = WerpAlloc(sizeof(*Dump));
    if (!Dump)
        return E_OUTOFMEMORY;

    if (!DuplicateHandle(GetCurrentProcess(), hProcess, GetCurrentProcess(), &Dump->Process,
                         0, FALSE, DUPLICATE_SAME_ACCESS))
    {
        Result = HRESULT_FROM_WIN32(GetLastError());
        WerpFree(Dump);
        return Result;
    }

    if (hThread && !DuplicateHandle(GetCurrentProcess(), hThread, GetCurrentProcess(), &Dump->Thread,
                                    0, FALSE, DUPLICATE_SAME_ACCESS))
    {
        Result = HRESULT_FROM_WIN32(GetLastError());
        CloseHandle(Dump->Process);
        WerpFree(Dump);
        return Result;
    }

    Dump->Type = dumpType;
    Dump->Flags = dwFlags;
    if (pExceptionParam)
    {
        Dump->HasException = TRUE;
        Dump->Exception = *pExceptionParam;
    }
    if (pDumpCustomOptions)
    {
        Dump->HasOptions = TRUE;
        Dump->Options = *pDumpCustomOptions;
    }

    AcquireSRWLockExclusive(&WerReportLock);
    Report = WerpFindReport(hReportHandle);
    if (Report)
        InsertTailList(&Report->Dumps, &Dump->Entry);
    ReleaseSRWLockExclusive(&WerReportLock);

    if (!Report)
    {
        CloseHandle(Dump->Process);
        if (Dump->Thread)
            CloseHandle(Dump->Thread);
        WerpFree(Dump);
        return E_INVALIDARG;
    }

    return S_OK;
}

HRESULT WINAPI WerReportSubmit(HREPORT hReportHandle, WER_CONSENT consent, DWORD dwFlags,
                               PWER_SUBMIT_RESULT pSubmitResult)
{
    WER_SUBMIT_RESULT Outcome = WerReportFailed;
    REPORT_STORE_TYPES Store = E_STORE_MACHINE_QUEUE;
    WCHAR AppPath[MAX_PATH];
    PWER_REPORT Report;
    HRESULT Result = S_OK;
    BOOL Keep = FALSE, Write = FALSE;
    PCWSTR AppName;

    if ((ULONG)consent < WerConsentNotAsked || (ULONG)consent >= WerConsentMax)
        return E_INVALIDARG;

    AcquireSRWLockExclusive(&WerReportLock);
    Report = WerpFindReport(hReportHandle);
    if (!Report)
    {
        ReleaseSRWLockExclusive(&WerReportLock);
        return E_INVALIDARG;
    }

    WerpResolveApplication(Report, AppPath, &AppName);
    /* TODO: no consent dialog, recovery or restart run, and critical reports do not end the reporting process */
    /* FIXME: no upload transport: every report that may be queued stays in the queue, WER_SUBMIT_OUTOFPROCESS_ASYNC is synchronous */
    if (WerpReadSetting(L"Disabled", 0) || WerpIsExcluded(WerpFileName(AppPath)))
    {
        Outcome = WerDisabled;
    }
    else if (consent == WerConsentDenied)
    {
        Outcome = WerReportCancelled;
        if (!(dwFlags & WER_SUBMIT_NO_ARCHIVE) && !WerpReadSetting(L"DisableArchive", 0))
        {
            Store = E_STORE_MACHINE_ARCHIVE;
            Keep = !(dwFlags & WER_SUBMIT_ARCHIVE_PARAMETERS_ONLY) && WerpReadSetting(L"ConfigureArchive", 1) == 2;
            Write = TRUE;
        }
    }
    else if ((dwFlags & WER_SUBMIT_NO_QUEUE) && !(dwFlags & WER_SUBMIT_QUEUE))
    {
        Outcome = WerReportFailed;
    }
    else if (WerpReadSetting(L"DisableQueue", 0))
    {
        Outcome = WerDisabledQueue;
    }
    else
    {
        Outcome = WerReportQueued;
        Store = WerpReadSetting(L"ForceQueue", 0) ? E_STORE_USER_QUEUE : E_STORE_MACHINE_QUEUE;
        Keep = TRUE;
        Write = TRUE;
    }

    if (Write)
        Result = WerpStoreReport(Report, Store, consent, dwFlags, Keep);
    ReleaseSRWLockExclusive(&WerReportLock);

    if (FAILED(Result))
        return Result;

    if (pSubmitResult)
        *pSubmitResult = Outcome;
    return S_OK;
}

static
HRESULT
WerpNextStoreKey(
    _In_ PWER_STORE Store,
    _Out_ PCWSTR *Key)
{
    PCWSTR Name;
    SIZE_T Size;
    PWSTR Copy;

    if (!Store->Listed)
    {
        Store->ItemCount = WerpListStore(Store->Path, &Store->Items);
        Store->NextItem = 0;
        Store->Listed = TRUE;
    }

    if (Store->NextItem >= Store->ItemCount)
        return HRESULT_FROM_WIN32(ERROR_NO_MORE_FILES);

    Name = Store->Items[Store->NextItem].Name;
    Size = (wcslen(Name) + 1) * sizeof(WCHAR);
    Copy = WerpAlloc(Size);
    if (!Copy)
        return E_OUTOFMEMORY;

    RtlCopyMemory(Copy, Name, Size);
    Store->NextItem++;
    *Key = Copy;
    return S_OK;
}

static
ULONGLONG
WerpDirectorySize(
    _In_ PCWSTR Path)
{
    WCHAR Pattern[MAX_PATH];
    WIN32_FIND_DATAW Data;
    ULARGE_INTEGER Size;
    ULONGLONG Total = 0;
    HANDLE Find;

    if (FAILED(StringCchPrintfW(Pattern, ARRAYSIZE(Pattern), L"%s\\*", Path)))
        return 0;

    Find = FindFirstFileW(Pattern, &Data);
    if (Find == INVALID_HANDLE_VALUE)
        return 0;

    do
    {
        if (Data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            continue;
        Size.LowPart = Data.nFileSizeLow;
        Size.HighPart = Data.nFileSizeHigh;
        Total += Size.QuadPart;
    } while (FindNextFileW(Find, &Data));
    FindClose(Find);
    return Total;
}

static
PWSTR
WerpLoadReportText(
    _In_ PCWSTR Path)
{
    LARGE_INTEGER Size;
    DWORD Read = 0;
    HANDLE File;
    PWSTR Text;

    File = CreateFileW(Path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
    if (File == INVALID_HANDLE_VALUE)
        return NULL;

    if (!GetFileSizeEx(File, &Size) || Size.QuadPart > WER_REPORT_TEXT_MAX)
    {
        CloseHandle(File);
        return NULL;
    }

    Text = WerpAlloc(Size.LowPart + 2 * sizeof(WCHAR));
    if (Text && !ReadFile(File, Text, Size.LowPart, &Read, NULL))
    {
        WerpFree(Text);
        Text = NULL;
    }
    CloseHandle(File);

    if (Text)
        Text[Read / sizeof(WCHAR)] = UNICODE_NULL;
    return Text;
}

static
BOOL
WerpParseIndexedKey(
    _In_ PCWSTR Key,
    _In_ PCWSTR Prefix,
    _Out_ PULONG Index,
    _Out_ PCWSTR *Suffix)
{
    SIZE_T Length = wcslen(Prefix);
    PWSTR End;

    if (wcsncmp(Key, Prefix, Length) != 0 || Key[Length] < L'0' || Key[Length] > L'9')
        return FALSE;

    *Index = wcstoul(Key + Length, &End, 10);
    if (*End != L']')
        return FALSE;

    *Suffix = End + 1;
    return TRUE;
}

static
HRESULT
WerpQueryReport(
    _In_ HREPORTSTORE hReportStore,
    _In_ PCWSTR pszReportKey,
    _Out_ PWER_REPORT_METADATA_V1 Metadata,
    _Out_opt_ PWER_BUFFER FileNames,
    _Out_opt_ PULONG FileCount)
{
    PWER_STORE Store = hReportStore;
    WCHAR ReportPath[MAX_PATH], FilePath[MAX_PATH];
    PWSTR Text, Cursor, Line, Value, End;
    ULARGE_INTEGER Time;
    PCWSTR Suffix;
    ULONG Index;

    if (!Store || !pszReportKey || !pszReportKey[0] || !Metadata || wcschr(pszReportKey, L'\\') ||
        wcschr(pszReportKey, L'/') || wcsstr(pszReportKey, L".."))
    {
        return E_INVALIDARG;
    }

    if (FAILED(StringCchPrintfW(ReportPath, ARRAYSIZE(ReportPath), L"%s\\%s", Store->Path, pszReportKey)) ||
        FAILED(StringCchPrintfW(FilePath, ARRAYSIZE(FilePath), L"%s\\%s", ReportPath, WER_REPORT_FILE_NAME)))
    {
        return E_INVALIDARG;
    }

    Text = WerpLoadReportText(FilePath);
    if (!Text)
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);

    RtlZeroMemory(Metadata, sizeof(*Metadata));
    if (FileCount)
        *FileCount = 0;

    Cursor = (Text[0] == 0xFEFF) ? Text + 1 : Text;
    while (*Cursor)
    {
        Line = Cursor;
        End = wcspbrk(Line, L"\r\n");
        if (End)
        {
            Cursor = End + ((End[0] == L'\r' && End[1] == L'\n') ? 2 : 1);
            *End = UNICODE_NULL;
        }
        else
        {
            Cursor = Line + wcslen(Line);
        }

        Value = wcschr(Line, L'=');
        if (!Value)
            continue;
        *Value++ = UNICODE_NULL;

        if (!wcscmp(Line, L"EventType"))
        {
            StringCchCopyW(Metadata->Signature.EventName, ARRAYSIZE(Metadata->Signature.EventName), Value);
        }
        else if (!wcscmp(Line, L"EventTime"))
        {
            Time.QuadPart = _wcstoui64(Value, NULL, 10);
            Metadata->CreationTime.dwLowDateTime = Time.LowPart;
            Metadata->CreationTime.dwHighDateTime = Time.HighPart;
        }
        else if (!wcscmp(Line, L"ReportIdentifier"))
        {
            UuidFromStringW((RPC_WSTR)Value, &Metadata->ReportId);
        }
        else if (WerpParseIndexedKey(Line, L"Sig[", &Index, &Suffix) && Index < WER_MAX_PARAM_COUNT)
        {
            if (!wcscmp(Suffix, L".Name"))
                StringCchCopyW(Metadata->Signature.Parameters[Index].Name,
                               ARRAYSIZE(Metadata->Signature.Parameters[Index].Name), Value);
            else if (!wcscmp(Suffix, L".Value"))
                StringCchCopyW(Metadata->Signature.Parameters[Index].Value,
                               ARRAYSIZE(Metadata->Signature.Parameters[Index].Value), Value);
        }
        else if (WerpParseIndexedKey(Line, L"File[", &Index, &Suffix) && !wcscmp(Suffix, L".Path"))
        {
            if (FileNames)
            {
                WerpAppendText(FileNames, Value, FALSE);
                WerpAppendText(FileNames, L"|", FALSE);
            }
            if (FileCount)
                (*FileCount)++;
        }
    }

    WerpFree(Text);
    Metadata->SizeInBytes = WerpDirectorySize(ReportPath);
    return (FileNames && FileNames->Failed) ? E_OUTOFMEMORY : S_OK;
}

HRESULT WerStoreOpen(REPORT_STORE_TYPES repStoreType, PHREPORTSTORE phReportStore)
{
    PWER_STORE Store;

    if (!phReportStore)
        return E_INVALIDARG;

    *phReportStore = NULL;
    if ((ULONG)repStoreType >= E_STORE_INVALID)
        return E_INVALIDARG;

    Store = WerpAlloc(sizeof(*Store));
    if (!Store)
        return E_OUTOFMEMORY;

    Store->Type = repStoreType;
    if (!WerpGetStorePath(repStoreType, Store->Path, ARRAYSIZE(Store->Path)))
    {
        WerpFree(Store);
        return E_FAIL;
    }

    *phReportStore = Store;
    return S_OK;
}

VOID WerStoreClose(HREPORTSTORE hReportStore)
{
    PWER_STORE Store = hReportStore;

    if (!Store)
        return;

    WerpFree(Store->Items);
    WerpFree(Store);
}

HRESULT WerStoreGetFirstReportKey(HREPORTSTORE hReportStore, PCWSTR *ppszReportKey)
{
    PWER_STORE Store = hReportStore;

    if (!Store || !ppszReportKey)
        return E_INVALIDARG;

    *ppszReportKey = NULL;
    WerpFree(Store->Items);
    Store->Items = NULL;
    Store->Listed = FALSE;
    return WerpNextStoreKey(Store, ppszReportKey);
}

HRESULT WerStoreGetNextReportKey(HREPORTSTORE hReportStore, PCWSTR *ppszReportKey)
{
    PWER_STORE Store = hReportStore;

    if (!Store || !ppszReportKey)
        return E_INVALIDARG;

    *ppszReportKey = NULL;
    return WerpNextStoreKey(Store, ppszReportKey);
}

VOID WerFreeString(PCWSTR pwszStr)
{
    WerpFree((PVOID)pwszStr);
}

HRESULT WerStoreGetReportCount(HREPORTSTORE hReportStore, DWORD *pdwReportCount)
{
    PWER_STORE Store = hReportStore;
    PWER_STORE_ITEM Items;

    if (!Store || !pdwReportCount)
        return E_INVALIDARG;

    *pdwReportCount = WerpListStore(Store->Path, &Items);
    WerpFree(Items);
    return S_OK;
}

HRESULT WerStoreGetSizeOnDisk(HREPORTSTORE hReportStore, ULONGLONG *pqwSizeInBytes)
{
    PWER_STORE Store = hReportStore;
    WCHAR Path[MAX_PATH];
    PWER_STORE_ITEM Items;
    ULONG Count, Index;

    if (!Store || !pqwSizeInBytes)
        return E_INVALIDARG;

    *pqwSizeInBytes = 0;
    Count = WerpListStore(Store->Path, &Items);
    for (Index = 0; Index < Count; Index++)
    {
        if (SUCCEEDED(StringCchPrintfW(Path, ARRAYSIZE(Path), L"%s\\%s", Store->Path, Items[Index].Name)))
            *pqwSizeInBytes += WerpDirectorySize(Path);
    }

    WerpFree(Items);
    return S_OK;
}

HRESULT WerStoreQueryReportMetadataV1(HREPORTSTORE hReportStore, PCWSTR pszReportKey,
                                      PWER_REPORT_METADATA_V1 pReportMetadata)
{
    return WerpQueryReport(hReportStore, pszReportKey, pReportMetadata, NULL, NULL);
}

HRESULT WerStoreQueryReportMetadataV2(HREPORTSTORE hReportStore, PCWSTR pszReportKey,
                                      PWER_REPORT_METADATA_V2 pReportMetadata)
{
    WER_REPORT_METADATA_V1 Metadata;
    WER_BUFFER Names = { 0 };
    ULONG Count, Index;
    DWORD Required;
    HRESULT Result;

    if (!pReportMetadata)
        return E_INVALIDARG;

    Result = WerpQueryReport(hReportStore, pszReportKey, &Metadata, &Names, &Count);
    if (FAILED(Result))
    {
        WerpFree(Names.Data);
        return Result;
    }

    pReportMetadata->Signature = Metadata.Signature;
    pReportMetadata->BucketId = Metadata.BucketId;
    pReportMetadata->ReportId = Metadata.ReportId;
    pReportMetadata->CreationTime = Metadata.CreationTime;
    pReportMetadata->SizeInBytes = Metadata.SizeInBytes;
    pReportMetadata->CabId[0] = UNICODE_NULL;
    pReportMetadata->ReportStatus = 0;
    RtlZeroMemory(&pReportMetadata->ReportIntegratorId, sizeof(pReportMetadata->ReportIntegratorId));
    pReportMetadata->NumberOfFiles = Count;

    Required = Count ? (DWORD)Names.Length + 1 : 0;
    if (pReportMetadata->SizeOfFileNames < Required || (Required && !pReportMetadata->FileNames))
    {
        pReportMetadata->SizeOfFileNames = Required;
        WerpFree(Names.Data);
        return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
    }

    for (Index = 0; Index < Names.Length; Index++)
        pReportMetadata->FileNames[Index] = (Names.Data[Index] == L'|') ? UNICODE_NULL : Names.Data[Index];
    if (Required)
        pReportMetadata->FileNames[Names.Length] = UNICODE_NULL;
    pReportMetadata->SizeOfFileNames = Required;
    WerpFree(Names.Data);
    return S_OK;
}
